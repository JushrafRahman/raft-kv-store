#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <random>
#include <iostream>
#include <memory>
#include <functional>
#include "../common/types.hpp"
#include "../network/network.hpp"
#include "../cluster/cluster_config.hpp"
#include "../storage/storage.hpp"
#include "../persistence/persistence.hpp"
#include "../metrics/metrics.hpp"
#include "../monitoring/monitoring_server.hpp"

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

class RaftNode {
public:
    RaftNode(int id, const std::vector<NodeAddress>& peers, 
             const std::string& dataDir, int monitoringPort);
    virtual ~RaftNode();

    // Public interface
    virtual void start();
    virtual void stop();
    virtual bool appendEntry(const Command& command);
    virtual bool replicateLog();
    
    // Getters
    NodeState getState() const { return state_; }
    bool isLeader() const { return state_ == NodeState::LEADER; }
    int getCurrentTerm() const { return currentTerm_; }
    const std::vector<LogEntry>& getLog() const { return log_; }
    
    // Configuration changes
    virtual bool addServer(const NodeAddress& server);
    virtual bool removeServer(const NodeAddress& server);

    // Callbacks
    void setApplyCallback(std::function<void(const Command&)> callback) {
        applyCallback_ = std::move(callback);
    }

    // Made protected for testing
protected:
    virtual void runElectionTimer();
    virtual void startElection();
    virtual void processMessage(const RaftMessage& message);
    virtual bool handleVoteRequest(const RaftMessage& message);
    virtual void handleVoteResponse(const RaftMessage& message);
    virtual void becomeLeader();
    virtual void sendHeartbeat();
    virtual int getLastLogTerm() const;

    virtual bool handleAppendEntries(const RaftMessage& message);
    virtual void handleAppendResponse(const RaftMessage& message);
    virtual void applyLogEntries(int lastApplied);

    virtual void persistCurrentTerm();
    virtual void persistVotedFor();
    virtual void persistLogEntries(const std::vector<LogEntry>& entries, int startIndex);
    virtual void loadPersistedState();

    virtual void handleConfigChange(const LogEntry& entry);
    virtual bool replicateConfigChange(const ClusterConfig& newConfig);

    // Constants
    static constexpr int MIN_ELECTION_TIMEOUT = 150;  // ms
    static constexpr int MAX_ELECTION_TIMEOUT = 300;  // ms
    static constexpr int HEARTBEAT_INTERVAL = 50;     // ms

    // State
    int id_;
    NodeState state_;
    int currentTerm_;
    int votedFor_;
    std::vector<LogEntry> log_;
    int commitIndex_ = -1;
    int lastApplied_ = -1;
    
    // Network and peers
    NetworkManager network_;
    std::vector<NodeAddress> peers_;
    
    // Leader state
    std::map<int, int> nextIndex_;
    std::map<int, int> matchIndex_;
    
    // Election state
    std::map<int, bool> votesReceived_;
    int votesGranted_;
    
    // Threading
    std::thread electionThread_;
    std::thread heartbeatThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
    
    // Random number generation
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<> electionTimeout_;

    // Components
    std::unique_ptr<PersistentStorage> storage_;
    std::unique_ptr<ClusterConfigManager> configManager_;
    std::unique_ptr<MetricsCollector> metrics_;
    std::unique_ptr<HealthChecker> health_;
    std::unique_ptr<MonitoringServer> monitoringServer_;

    // Callbacks
    std::function<void(const Command&)> applyCallback_;
};