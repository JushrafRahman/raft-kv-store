#pragma once
#include "../network/network.hpp"
#include <random>
#include <chrono>
#include <mutex>
#include <condition_variable>

class RaftNode {
public:
    RaftNode(int id, const std::vector<NodeAddress>& peers);
    void start();
    void stop();

private:
    void runElectionTimer();
    void startElection();
    void processMessage(const RaftMessage& message);
    
    // Election timer configuration
    static constexpr int MIN_ELECTION_TIMEOUT = 150;  // ms
    static constexpr int MAX_ELECTION_TIMEOUT = 300;  // ms
    
    int id_;
    NodeState state_;
    int currentTerm_;
    int votedFor_;
    std::vector<std::string> log_;
    
    // Networking
    NetworkManager network_;
    std::vector<NodeAddress> peers_;
    
    // Election timer
    std::thread electionThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<> electionTimeout_;
};
