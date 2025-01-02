#pragma once
#include "cluster_config.hpp"
#include "../raft/raft.hpp"
#include <memory>
#include <string>

class ClusterManager {
public:
    ClusterManager(RaftNode& raft);
    
    bool addNode(const std::string& ip, int port);
    bool removeNode(const std::string& ip, int port);
    
    std::vector<NodeAddress> getClusterMembers() const;
    NodeAddress getLeader() const;
    bool isLeader() const;
    
    struct ClusterStatus {
        int totalNodes;
        int activeNodes;
        std::string leaderAddress;
        bool isStable;
        std::string currentPhase;
    };
    
    ClusterStatus getStatus() const;

private:
    RaftNode& raft_;
    std::mutex mutex_;
};
