#pragma once
#include <vector>
#include <set>
#include "../network/network.hpp"

struct ClusterConfig {
    int configIndex;
    std::vector<NodeAddress> servers;
    bool isJoint;  
    
    std::vector<NodeAddress> oldServers;
    std::vector<NodeAddress> newServers;
};

class ClusterConfigManager {
public:
    ClusterConfigManager(int nodeId);
    
    bool addServer(const NodeAddress& server);
    bool removeServer(const NodeAddress& server);
    
    bool isVotingMember(const NodeAddress& server) const;
    int getQuorumSize() const;
    bool hasQuorum(const std::set<int>& votes) const;
    
    bool isInJointConsensus() const;
    bool isInConfiguration(const NodeAddress& server) const;
    
    const ClusterConfig& getCurrentConfig() const;
    void setConfig(const ClusterConfig& config);
    
private:
    bool startJointConsensus(const std::vector<NodeAddress>& newServers);
    bool endJointConsensus();
    
    int nodeId_;
    ClusterConfig currentConfig_;
};
