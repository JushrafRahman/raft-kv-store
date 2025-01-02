#include "raft.hpp"
#include <iostream>

RaftNode::RaftNode(int id, const std::vector<NodeAddress>& peers)
    : id_(id)
    , state_(NodeState::FOLLOWER)
    , currentTerm_(0)
    , votedFor_(-1)
    , network_(id, peers[id].port)
    , peers_(peers)
    , running_(false)
    , gen_(rd_())
    , electionTimeout_(MIN_ELECTION_TIMEOUT, MAX_ELECTION_TIMEOUT)
    , votesGranted_(0) {
    
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
    state_ = NodeState::CANDIDATE;
    votedFor_ = id_;
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