#include "raft.hpp"
#include <iostream>

RaftNode::RaftNode(int id, const std::vector<NodeAddress>& peers,
                   const std::string& dataDir, int monitoringPort)
    : id_(id)
    , state_(NodeState::FOLLOWER)
    , currentTerm_(0)
    , votedFor_(-1)
    , network_(id, peers[id].port)
    , peers_(peers)
    , running_(false)
    , gen_(rd_())
    , electionTimeout_(MIN_ELECTION_TIMEOUT, MAX_ELECTION_TIMEOUT)
    , storage_(std::make_unique<PersistentStorage>(dataDir, id))
    , metrics_(std::make_unique<MetricsCollector>())
    , health_(std::make_unique<HealthChecker>())
    , monitoringServer_(std::make_unique<MonitoringServer>(
          monitoringPort, *metrics_, *health_)) {
    
    // initialize metrics
    metrics_->setGauge("cluster_size", peers.size());
    
    // initialize health checking
    health_->registerComponent("raft");
    health_->registerComponent("storage");
    health_->registerComponent("network");
    
    network_.setMessageCallback([this](const RaftMessage& msg) {
        LatencyTimer timer(*metrics_, "message_processing");
        processMessage(msg);
    });
    
    loadPersistedState();
    
    monitoringServer_->start();
}

RaftNode::~RaftNode() {
    if (running_) {
        stop();
    }
}

void RaftNode::start() {
    running_ = true;
    network_.start();
    electionThread_ = std::thread(&RaftNode::runElectionTimer, this);
    
    metrics_->recordRate("node_starts");
    health_->reportHealth("raft", true, "Node started successfully");
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
    monitoringServer_->stop();
    
    metrics_->recordRate("node_stops");
    health_->reportHealth("raft", false, "Node stopped");
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
    
    LatencyTimer timer(*metrics_, "election_duration");
    
    currentTerm_++;
    state_ = NodeState::CANDIDATE;
    votedFor_ = id_;
    votesGranted_ = 1;  // vote for self
    
    persistCurrentTerm();
    persistVotedFor();
    
    metrics_->incrementCounter("leader_elections");
    
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
            metrics_->incrementCounter("vote_requests_sent");
        }
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

bool RaftNode::handleVoteRequest(const RaftMessage& message) {
    if (message.term < currentTerm_) {
        return false;
    }
    
    bool logIsUpToDate = message.lastLogTerm > getLastLogTerm() ||
                        (message.lastLogTerm == getLastLogTerm() && 
                         message.lastLogIndex >= static_cast<int>(log_.size() - 1));
                         
    if ((votedFor_ == -1 || votedFor_ == message.senderId) && logIsUpToDate) {
        votedFor_ = message.senderId;
        persistVotedFor();
        return true;
    }
    
    return false;
}

bool RaftNode::appendEntry(const Command& command) {
    if (state_ != NodeState::LEADER) {
        return false;
    }

    LatencyTimer timer(*metrics_, "append_latency");
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    LogEntry entry;
    entry.term = currentTerm_;
    entry.command = command;
    log_.push_back(entry);
    
    persistLogEntries({entry}, log_.size() - 1);
    
    metrics_->incrementCounter("log_entries");
    metrics_->recordRate("operations");
    
    return replicateLog();
}

bool RaftNode::replicateLog() {
    if (state_ != NodeState::LEADER) {
        return false;
    }

    for (const auto& peer : peers_) {
        if (peer.port == peers_[id_].port) continue;
        
        RaftMessage message;
        message.type = RaftMessage::Type::APPEND_ENTRIES;
        message.term = currentTerm_;
        message.senderId = id_;
        message.prevLogIndex = nextIndex_[peer.port] - 1;
        message.prevLogTerm = message.prevLogIndex >= 0 ? 
            log_[message.prevLogIndex].term : 0;
        
        // Add entries starting from nextIndex
        for (size_t i = nextIndex_[peer.port]; i < log_.size(); i++) {
            message.entries.push_back(std::to_string(log_[i].term));
        }
        
        message.leaderCommit = commitIndex_;
        network_.sendMessage(peer, message);
    }
    
    return true;
}

bool RaftNode::handleAppendEntries(const RaftMessage& message) {
    if (message.term < currentTerm_) {
        return false;
    }

    cv_.notify_all();  // Reset election timer
    
    // Check if we have the previous log entry
    if (message.prevLogIndex >= 0) {
        if (log_.size() <= static_cast<size_t>(message.prevLogIndex) ||
            log_[message.prevLogIndex].term != message.prevLogTerm) {
            return false;
        }
        
        // Delete any conflicting entries
        log_.resize(message.prevLogIndex + 1);
    }
    
    // Append new entries
    for (const auto& entryStr : message.entries) {
        LogEntry entry;
        entry.term = std::stoi(entryStr);  // Simple serialization
        log_.push_back(entry);
    }
    
    // Update commit index
    if (message.leaderCommit > commitIndex_) {
        commitIndex_ = std::min(message.leaderCommit, static_cast<int>(log_.size()) - 1);
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
        
        // Check if we can commit any entries
        std::vector<int> matchIndexes;
        for (const auto& [_, index] : matchIndex_) {
            matchIndexes.push_back(index);
        }
        std::sort(matchIndexes.begin(), matchIndexes.end());
        int newCommitIndex = matchIndexes[matchIndexes.size() / 2];
        
        if (newCommitIndex > commitIndex_ && 
            log_[newCommitIndex].term == currentTerm_) {
            commitIndex_ = newCommitIndex;
            applyLogEntries(commitIndex_);
        }
    } else {
        // Decrement nextIndex and retry
        nextIndex_[message.senderId] = std::max(0, nextIndex_[message.senderId] - 1);
        replicateLog();  // Retry with lower nextIndex
    }
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
            metrics_->incrementCounter("votes_received");
        }
    }
    
    int majority = (peers_.size() + 1) / 2 + 1;
    if (votesGranted_ >= majority) {
        becomeLeader();
        metrics_->incrementCounter("leader_transitions");
    }
}

void RaftNode::becomeLeader() {
    if (state_ != NodeState::CANDIDATE) {
        return;
    }
    
    std::cout << "Node " << id_ << " becoming leader for term " << currentTerm_ << std::endl;
    
    state_ = NodeState::LEADER;
    metrics_->setGauge("is_leader", 1);
    health_->reportHealth("raft", true, "Node is leader");
    
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
            metrics_->incrementCounter("heartbeats_sent");
        }
    });
}

void RaftNode::processMessage(const RaftMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    metrics_->incrementCounter("messages_received");
    
    if (message.term > currentTerm_) {
        currentTerm_ = message.term;
        state_ = NodeState::FOLLOWER;
        votedFor_ = -1;
        metrics_->setGauge("is_leader", 0);
        persistCurrentTerm();
        persistVotedFor();
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
            metrics_->incrementCounter("vote_responses_sent");
            break;
        }
        
        case RaftMessage::Type::VOTE_RESPONSE:
            handleVoteResponse(message);
            break;
            
        case RaftMessage::Type::APPEND_ENTRIES:
            cv_.notify_all();
            handleAppendEntries(message);
            metrics_->incrementCounter("append_entries_received");
            break;
            
        case RaftMessage::Type::APPEND_RESPONSE:
            handleAppendResponse(message);
            metrics_->incrementCounter("append_responses_received");
            break;
    }
}

void RaftNode::sendHeartbeat() {
    LatencyTimer timer(*metrics_, "heartbeat_latency");
    
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

void RaftNode::loadPersistedState() {
    LatencyTimer timer(*metrics_, "load_state_latency");
    
    int term, votedFor;
    if (storage_->getPersistedState(term, votedFor)) {
        currentTerm_ = term;
        votedFor_ = votedFor;
        metrics_->incrementCounter("successful_state_loads");
    } else {
        metrics_->incrementCounter("failed_state_loads");
        health_->reportHealth("storage", false, "Failed to load persisted state");
    }
    
    std::vector<LogEntry> entries;
    if (storage_->getLogEntries(entries)) {
        log_ = std::move(entries);
        metrics_->setGauge("log_size", log_.size());
    }
}

void RaftNode::applyLogEntries(int upToIndex) {
    for (int i = lastApplied_ + 1; i <= upToIndex; i++) {
        // Apply each entry to the state machine
        if (i < static_cast<int>(log_.size())) {
            if (applyCallback_) {
                applyCallback_(log_[i].command);
            }
            lastApplied_ = i;
        }
    }
}

int RaftNode::getLastLogTerm() const {
    if (log_.empty()) {
        return 0;
    }
    return log_.back().term;
}