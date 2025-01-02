#include "raft.hpp"
#include <iostream>

RaftNode::RaftNode(int id, const std::vector<NodeAddress>& peers, const std::string& dataDir)
    : id_(id)
    , peers_(peers)
    , network_(id, peers[id].port)
    , storage_(std::make_unique<PersistentStorage>(dataDir, id))
    , running_(false)
    , gen_(rd_())
    , electionTimeout_(MIN_ELECTION_TIMEOUT, MAX_ELECTION_TIMEOUT) {
    
    loadPersistedState();
    
    network_.setMessageCallback([this](const RaftMessage& msg) {
        processMessage(msg);
    });
}

void RaftNode::start() {
    running_ = true;
    network_.start();
    electionThread_ = std::thread(&RaftNode::runElectionTimer, this);
}

void RaftNode::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    
    if (electionThread_.joinable()) {
        electionThread_.join();
    }
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    network_.stop();
}

void RaftNode::runElectionTimer() {
    while (running_) {
        int timeout = electionTimeout_(gen_);
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_for(lock, std::chrono::milliseconds(timeout), 
                [this] { return !running_; })) {
                break;
            }
        }
        
        if (state_ != NodeState::LEADER) {
            startElection();
        }
    }
}

void RaftNode::startElection() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    currentTerm_++;
    persistCurrentTerm();
    state_ = NodeState::CANDIDATE;
    votedFor_ = id_;
    persistVotedFor();
    votesGranted_ = 1; 
    
    std::cout << "Node " << id_ << " starting election for term " << currentTerm_ << std::endl;
    
    RaftMessage voteRequest;
    voteRequest.type = RaftMessage::Type::VOTE_REQUEST;
    voteRequest.term = currentTerm_;
    voteRequest.senderId = id_;
    voteRequest.lastLogIndex = log_.size() - 1;
    voteRequest.lastLogTerm = log_.empty() ? 0 : log_.back().term;
    
    for (const auto& peer : peers_) {
        if (peer.port != peers_[id_].port) {
            network_.sendMessage(peer, voteRequest);
        }
    }
}

bool RaftNode::handleVoteRequest(const RaftMessage& message) {
    if (message.term < currentTerm_) {
        return false;
    }
    
    bool logIsUpToDate = (message.lastLogTerm > getLastLogTerm()) ||
                        (message.lastLogTerm == getLastLogTerm() && 
                         message.lastLogIndex >= log_.size() - 1);
                         
    if ((votedFor_ == -1 || votedFor_ == message.senderId) && logIsUpToDate) {
        votedFor_ = message.senderId;
        return true;
    }
    
    return false;
}

void RaftNode::handleVoteResponse(const RaftMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != NodeState::CANDIDATE || message.term != currentTerm_) {
        return;
    }
    
    if (!votesReceived_[message.senderId]) {
        votesReceived_[message.senderId] = true;
        if (message.voteGranted) {
            votesGranted_++;
        }
    }
    
    int majority = (peers_.size() + 1) / 2 + 1;
    if (votesGranted_ >= majority) {
        becomeLeader();
    }
}

void RaftNode::becomeLeader() {
    if (state_ != NodeState::CANDIDATE) {
        return;
    }
    
    std::cout << "Node " << id_ << " becoming leader for term " << currentTerm_ << std::endl;
    
    state_ = NodeState::LEADER;
    
    nextIndex_.clear();
    matchIndex_.clear();
    for (const auto& peer : peers_) {
        if (peer.port != peers_[id_].port) {
            nextIndex_[peer.port] = log_.size();
            matchIndex_[peer.port] = 0;
        }
    }
    
    heartbeatThread_ = std::thread([this]() {
        while (running_ && state_ == NodeState::LEADER) {
            sendHeartbeat();
            std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL));
        }
    });
}

void RaftNode::sendHeartbeat() {
    RaftMessage heartbeat;
    heartbeat.type = RaftMessage::Type::APPEND_ENTRIES;
    heartbeat.term = currentTerm_;
    heartbeat.senderId = id_;
    heartbeat.entries.clear(); 
    
    for (const auto& peer : peers_) {
        if (peer.port != peers_[id_].port) {
            network_.sendMessage(peer, heartbeat);
        }
    }
}

void RaftNode::processMessage(const RaftMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (message.term > currentTerm_) {
        currentTerm_ = message.term;
        state_ = NodeState::FOLLOWER;
        votedFor_ = -1;
    }
    
    switch (message.type) {
        case RaftMessage::Type::VOTE_REQUEST: {
            bool voteGranted = handleVoteRequest(message);
        
            RaftMessage response;
            response.type = RaftMessage::Type::VOTE_RESPONSE;
            response.term = currentTerm_;
            response.senderId = id_;
            response.voteGranted = voteGranted;
            
            network_.sendMessage(peers_[message.senderId], response);
            break;
        }
        
        case RaftMessage::Type::VOTE_RESPONSE:
            handleVoteResponse(message);
            break;
            
        case RaftMessage::Type::APPEND_ENTRIES:
            cv_.notify_all();
            break;
    }
}

int RaftNode::getLastLogTerm() const {
    if (log_.empty()) {
        return 0;
    }
    return log_.back().term;
}

bool RaftNode::appendEntry(const Command& command) {
    if (state_ != NodeState::LEADER) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    LogEntry entry;
    entry.term = currentTerm_;
    entry.command = command;
    log_.push_back(entry);
    
    return replicateLog();
}

bool RaftNode::replicateLog() {
    for (const auto& peer : peers_) {
        if (peer.port == peers_[id_].port) continue;
        
        RaftMessage appendMsg;
        appendMsg.type = RaftMessage::Type::APPEND_ENTRIES;
        appendMsg.term = currentTerm_;
        appendMsg.senderId = id_;
        
        int nextIdx = nextIndex_[peer.port];
        appendMsg.prevLogIndex = nextIdx - 1;
        appendMsg.prevLogTerm = (nextIdx > 0 && !log_.empty()) ? log_[nextIdx - 1].term : 0;
        
        for (size_t i = nextIdx; i < log_.size(); i++) {
            appendMsg.entries.push_back(log_[i]);
        }
        
        appendMsg.leaderCommit = commitIndex_;
        network_.sendMessage(peer, appendMsg);
    }
    return true;
}

bool RaftNode::handleAppendEntries(const RaftMessage& message) {
    if (message.term < currentTerm_) {
        return false;
    }

    if (message.term > currentTerm_) {
        currentTerm_ = message.term;
        persistCurrentTerm();
        state_ = NodeState::FOLLOWER;
        votedFor_ = -1;
        persistVotedFor();
    }

    cv_.notify_all();
    
    if (message.prevLogIndex >= 0) {
        if (log_.size() <= static_cast<size_t>(message.prevLogIndex) ||
            log_[message.prevLogIndex].term != message.prevLogTerm) {
            return false;
        }
    }
    
    size_t newEntryIndex = message.prevLogIndex + 1;
    for (const auto& newEntry : message.entries) {
        if (log_.size() > newEntryIndex) {
            if (log_[newEntryIndex].term != newEntry.term) {
                log_.erase(log_.begin() + newEntryIndex, log_.end());
                break;
            }
        } else {
            break;
        }
        newEntryIndex++;
    }
    
    for (size_t i = 0; i < message.entries.size(); i++) {
        if (newEntryIndex + i >= log_.size()) {
            log_.push_back(message.entries[i]);
        }
    }
    
    if (message.leaderCommit > commitIndex_) {
        commitIndex_ = std::min(message.leaderCommit, static_cast<int>(log_.size() - 1));
        applyLogEntries(commitIndex_);
    }
    
    if (!message.entries.empty()) {
        persistLogEntries(message.entries, message.prevLogIndex + 1);
    }
    
    if (message.leaderCommit > commitIndex_) {
        commitIndex_ = std::min(message.leaderCommit, static_cast<int>(log_.size() - 1));
        applyLogEntries(commitIndex_);
    }
    
    return true;
}

void RaftNode::handleAppendResponse(const RaftMessage& message) {
    if (state_ != NodeState::LEADER) {
        return;
    }

    if (message.success) {
        nextIndex_[message.senderId] = message.lastLogIndex + 1;
        matchIndex_[message.senderId] = message.lastLogIndex;
        
        std::vector<int> matchIndexes;
        for (const auto& pair : matchIndex_) {
            matchIndexes.push_back(pair.second);
        }
        matchIndexes.push_back(log_.size() - 1); 
        
        std::sort(matchIndexes.begin(), matchIndexes.end());
        int newCommitIndex = matchIndexes[matchIndexes.size() / 2];
        
        if (newCommitIndex > commitIndex_ && log_[newCommitIndex].term == currentTerm_) {
            commitIndex_ = newCommitIndex;
            applyLogEntries(commitIndex_);
        }
    } else {
        nextIndex_[message.senderId]--;
        if (nextIndex_[message.senderId] < 0) {
            nextIndex_[message.senderId] = 0;
        }
        replicateLog();
    }
}

void RaftNode::applyLogEntries(int upToIndex) {
    while (lastApplied_ < upToIndex) {
        lastApplied_++;
        if (applyCallback_) {
            applyCallback_(log_[lastApplied_].command);
        }
    }
}

void RaftNode::loadPersistedState() {
    int term, votedFor;
    if (storage_->getPersistedState(term, votedFor)) {
        currentTerm_ = term;
        votedFor_ = votedFor;
    } else {
        currentTerm_ = 0;
        votedFor_ = -1;
    }
    
    std::vector<LogEntry> entries;
    if (storage_->getLogEntries(entries)) {
        log_ = std::move(entries);
    }
    
    int lastIncludedIndex, lastIncludedTerm;
    if (storage_->getLatestSnapshot(lastIncludedIndex, lastIncludedTerm)) {
        commitIndex_ = lastIncludedIndex;
        lastApplied_ = lastIncludedIndex;
    }
}

void RaftNode::persistCurrentTerm() {
    storage_->saveCurrentTerm(currentTerm_);
}

void RaftNode::persistVotedFor() {
    storage_->saveVotedFor(votedFor_);
}

void RaftNode::persistLogEntries(const std::vector<LogEntry>& entries, int startIndex) {
    storage_->appendLogEntries(entries, startIndex);
}

void RaftNode::createSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (commitIndex_ <= 0 || log_.empty()) return;
    
    int lastIncludedIndex = commitIndex_;
    int lastIncludedTerm = log_[lastIncludedIndex].term;
    
    if (storage_->createSnapshot(lastIncludedIndex, lastIncludedTerm)) {
        log_.erase(log_.begin(), log_.begin() + lastIncludedIndex + 1);
        storage_->truncateLog(log_.size() - 1);
    }
}

bool RaftNode::addServer(const NodeAddress& server) {
    if (state_ != NodeState::LEADER) {
        return false;
    }
    
    LogEntry configEntry;
    configEntry.term = currentTerm_;
    
    Command cmd;
    cmd.type = Command::Type::CONFIG_CHANGE;
    cmd.operation = "add_server";
    cmd.server = server;
    configEntry.command = cmd;
    
    std::lock_guard<std::mutex> lock(mutex_);
    log_.push_back(configEntry);
    persistLogEntries({configEntry}, log_.size() - 1);
    
    return replicateLog();
}

bool RaftNode::removeServer(const NodeAddress& server) {
    if (state_ != NodeState::LEADER) {
        return false;
    }
    
    if (configManager_->getCurrentConfig().servers.size() <= 1 ||
        (server.ip == peers_[id_].ip && server.port == peers_[id_].port)) {
        return false;
    }
    
    LogEntry configEntry;
    configEntry.term = currentTerm_;
    
    Command cmd;
    cmd.type = Command::Type::CONFIG_CHANGE;
    cmd.operation = "remove_server";
    cmd.server = server;
    configEntry.command = cmd;
    
    std::lock_guard<std::mutex> lock(mutex_);
    log_.push_back(configEntry);
    persistLogEntries({configEntry}, log_.size() - 1);
    
    return replicateLog();
}

void RaftNode::handleConfigChange(const LogEntry& entry) {
    const Command& cmd = entry.command;
    if (cmd.type != Command::Type::CONFIG_CHANGE) {
        return;
    }
    
    if (cmd.operation == "add_server") {
        if (configManager_->addServer(cmd.server)) {
            replicateConfigChange(configManager_->getCurrentConfig());
        }
    } else if (cmd.operation == "remove_server") {
        if (configManager_->removeServer(cmd.server)) {
            replicateConfigChange(configManager_->getCurrentConfig());
        }
    } else if (cmd.operation == "end_joint") {
        configManager_->endJointConsensus();
    }
}

bool RaftNode::replicateConfigChange(const ClusterConfig& newConfig) {
    LogEntry endJointEntry;
    endJointEntry.term = currentTerm_;
    
    Command cmd;
    cmd.type = Command::Type::CONFIG_CHANGE;
    cmd.operation = "end_joint";
    endJointEntry.command = cmd;
    
    log_.push_back(endJointEntry);
    persistLogEntries({endJointEntry}, log_.size() - 1);
    
    return replicateLog();
}

void RaftNode::applyLogEntries(int upToIndex) {
    while (lastApplied_ < upToIndex) {
        lastApplied_++;
        const LogEntry& entry = log_[lastApplied_];
        
        if (entry.command.type == Command::Type::CONFIG_CHANGE) {
            handleConfigChange(entry);
        } else if (applyCallback_) {
            applyCallback_(entry.command);
        }
    }
}

bool RaftNode::hasQuorum(const std::set<int>& votes) const {
    return configManager_->hasQuorum(votes);
}