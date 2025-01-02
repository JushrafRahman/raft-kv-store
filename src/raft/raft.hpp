#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <random>
#include <iostream>
#include "../network/network.hpp"

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

struct LogEntry {
    int term;
    std::string command;
};

class RaftNode {
public:
    RaftNode(int id, const std::vector<NodeAddress>& peers);
    void start();
    void stop();

private:
    void runElectionTimer();
    void startElection();
    void processMessage(const RaftMessage& message);
    bool handleVoteRequest(const RaftMessage& message);
    void handleVoteResponse(const RaftMessage& message);
    void becomeLeader();
    void sendHeartbeat();
    int getLastLogTerm() const;

    static constexpr int MIN_ELECTION_TIMEOUT = 150;  // ms
    static constexpr int MAX_ELECTION_TIMEOUT = 300;  // ms
    static constexpr int HEARTBEAT_INTERVAL = 50;    // ms

    int id_;
    NodeState state_;
    int currentTerm_;
    int votedFor_;
    std::vector<LogEntry> log_;
    
    NetworkManager network_;
    std::vector<NodeAddress> peers_;
    
    std::map<int, int> nextIndex_;
    std::map<int, int> matchIndex_;
    
    std::map<int, bool> votesReceived_;
    int votesGranted_;
    
    std::thread electionThread_;
    std::thread heartbeatThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
    
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<> electionTimeout_;
};