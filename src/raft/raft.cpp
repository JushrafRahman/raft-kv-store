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
    
    // Send RequestVote to all peers
    for (const auto& peer : peers_) {
        if (peer.port != peers_[id_].port) {
            network_.sendMessage(peer, voteRequest);
            metrics_->incrementCounter("vote_requests_sent");
        }
    }
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
