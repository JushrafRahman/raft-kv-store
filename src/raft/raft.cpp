#include "raft.hpp"

RaftNode::RaftNode(int id, const std::vector<NodeAddress>& peers)
    : id_(id)
    , state_(NodeState::FOLLOWER)
    , currentTerm_(0)
    , votedFor_(-1)
    , network_(id, peers[id].port)
    , peers_(peers)
    , running_(false)
    , gen_(rd_())
    , electionTimeout_(MIN_ELECTION_TIMEOUT, MAX_ELECTION_TIMEOUT) {
    
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
    
    votesReceived_.clear();
    votesGranted_ = 1;  // Vote for self
    
    std::cout << "Node " << id_ << " starting election for term " << currentTerm_ << std::endl;
    
    RaftMessage voteRequest;
    voteRequest.type = RaftMessage::VOTE_REQUEST;
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

void RaftNode::processMessage(const RaftMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (message.term > currentTerm_) {
        currentTerm_ = message.term;
        state_ = NodeState::FOLLOWER;
        votedFor_ = -1;
    }
    
    switch (message.type) {
        case RaftMessage::VOTE_REQUEST:
            // TODO: implement vote request handling
            break;
            
        case RaftMessage::APPEND_ENTRIES:
            cv_.notify_all();
            break;
            
        // TODO: handle other message types...
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
        }
    }
    
    // check if we have majority
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
    heartbeat.type = RaftMessage::APPEND_ENTRIES;
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
        case RaftMessage::VOTE_REQUEST: {
            bool voteGranted = handleVoteRequest(message);
            
            // Send response
            RaftMessage response;
            response.type = RaftMessage::VOTE_RESPONSE;
            response.term = currentTerm_;
            response.senderId = id_;
            response.voteGranted = voteGranted;
            
            network_.sendMessage(peers_[message.senderId], response);
            break;
        }
        
        case RaftMessage::VOTE_RESPONSE:
            handleVoteResponse(message);
            break;
            
        case RaftMessage::APPEND_ENTRIES:
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