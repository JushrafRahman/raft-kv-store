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
    
    // increment term and transition to candidate
    currentTerm_++;
    state_ = NodeState::CANDIDATE;
    votedFor_ = id_;  // vote for self
    
    RaftMessage voteRequest;
    voteRequest.type = RaftMessage::VOTE_REQUEST;
    voteRequest.term = currentTerm_;
    voteRequest.senderId = id_;
    
    // send RequestVote to all peers
    for (const auto& peer : peers_) {
        if (peer.port != peers_[id_].port) {  // don't send to self
            network_.sendMessage(peer, voteRequest);
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
        case RaftMessage::VOTE_REQUEST:
            // TODO: implement vote request handling
            break;
            
        case RaftMessage::APPEND_ENTRIES:
            cv_.notify_all();
            break;
            
        // TODO: handle other message types...
    }
}