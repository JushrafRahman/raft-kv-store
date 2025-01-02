#include "cluster_manager.hpp"

ClusterManager::ClusterManager(RaftNode& raft) : raft_(raft) {}

bool ClusterManager::addNode(const std::string& ip, int port) {
    NodeAddress newNode{ip, port};
    return raft_.addServer(newNode);
}

bool ClusterManager::removeNode(const std::string& ip, int port) {
    NodeAddress node{ip, port};
    return raft_.removeServer(node);
}

std::vector<NodeAddress> ClusterManager::getClusterMembers() const {
    return raft_.getClusterConfig().servers;
}

NodeAddress ClusterManager::getLeader() const {
    return raft_.getLeaderAddress();
}

bool ClusterManager::isLeader() const {
    return raft_.isLeader();
}

ClusterManager::ClusterStatus ClusterManager::getStatus() const {
    ClusterStatus status;
    const auto& config = raft_.getClusterConfig();
    
    status.totalNodes = config.servers.size();
    status.activeNodes = raft_.getActiveNodes();
    status.leaderAddress = raft_.getLeaderAddress().toString();
    status.isStable = !config.isJoint;
    status.currentPhase = config.isJoint ? "joint_consensus" : "normal";
    
    return status;
}