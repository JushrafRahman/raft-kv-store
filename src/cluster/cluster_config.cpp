#include "cluster_config.hpp"
#include <algorithm>

ClusterConfigManager::ClusterConfigManager(int nodeId) : nodeId_(nodeId) {}

bool ClusterConfigManager::addServer(const NodeAddress& server) {
    if (isInConfiguration(server)) {
        return false;
    }
    
    std::vector<NodeAddress> newServers = currentConfig_.servers;
    newServers.push_back(server);
    
    return startJointConsensus(newServers);
}

bool ClusterConfigManager::removeServer(const NodeAddress& server) {
    if (!isInConfiguration(server)) {
        return false;
    }
    
    std::vector<NodeAddress> newServers = currentConfig_.servers;
    newServers.erase(
        std::remove_if(newServers.begin(), newServers.end(),
            [&server](const NodeAddress& addr) {
                return addr.port == server.port && addr.ip == server.ip;
            }
        ),
        newServers.end()
    );
    
    return startJointConsensus(newServers);
}

bool ClusterConfigManager::isVotingMember(const NodeAddress& server) const {
    if (!currentConfig_.isJoint) {
        return isInConfiguration(server);
    }
    
    return std::find_if(currentConfig_.oldServers.begin(), currentConfig_.oldServers.end(),
               [&server](const NodeAddress& addr) {
                   return addr.port == server.port && addr.ip == server.ip;
               }) != currentConfig_.oldServers.end()
        || std::find_if(currentConfig_.newServers.begin(), currentConfig_.newServers.end(),
               [&server](const NodeAddress& addr) {
                   return addr.port == server.port && addr.ip == server.ip;
               }) != currentConfig_.newServers.end();
}

int ClusterConfigManager::getQuorumSize() const {
    if (!currentConfig_.isJoint) {
        return (currentConfig_.servers.size() / 2) + 1;
    }
    
    return std::max(
        (currentConfig_.oldServers.size() / 2) + 1,
        (currentConfig_.newServers.size() / 2) + 1
    );
}

bool ClusterConfigManager::hasQuorum(const std::set<int>& votes) const {
    if (!currentConfig_.isJoint) {
        return votes.size() >= getQuorumSize();
    }
    
    int oldConfigVotes = 0;
    int newConfigVotes = 0;
    
    for (int nodeId : votes) {
        for (const auto& server : currentConfig_.oldServers) {
            if (server.port == nodeId) oldConfigVotes++;
        }
        for (const auto& server : currentConfig_.newServers) {
            if (server.port == nodeId) newConfigVotes++;
        }
    }
    
    return oldConfigVotes >= ((currentConfig_.oldServers.size() / 2) + 1) &&
           newConfigVotes >= ((currentConfig_.newServers.size() / 2) + 1);
}

bool ClusterConfigManager::isInJointConsensus() const {
    return currentConfig_.isJoint;
}

bool ClusterConfigManager::isInConfiguration(const NodeAddress& server) const {
    return std::find_if(currentConfig_.servers.begin(), currentConfig_.servers.end(),
        [&server](const NodeAddress& addr) {
            return addr.port == server.port && addr.ip == server.ip;
        }) != currentConfig_.servers.end();
}

const ClusterConfig& ClusterConfigManager::getCurrentConfig() const {
    return currentConfig_;
}

void ClusterConfigManager::setConfig(const ClusterConfig& config) {
    currentConfig_ = config;
}

bool ClusterConfigManager::startJointConsensus(const std::vector<NodeAddress>& newServers) {
    if (currentConfig_.isJoint) {
        return false;
    }
    
    ClusterConfig jointConfig;
    jointConfig.isJoint = true;
    jointConfig.oldServers = currentConfig_.servers;
    jointConfig.newServers = newServers;
    jointConfig.configIndex = currentConfig_.configIndex + 1;
    
    currentConfig_ = jointConfig;
    return true;
}

bool ClusterConfigManager::endJointConsensus() {
    if (!currentConfig_.isJoint) {
        return false;
    }
    
    ClusterConfig newConfig;
    newConfig.isJoint = false;
    newConfig.servers = currentConfig_.newServers;
    newConfig.configIndex = currentConfig_.configIndex + 1;
    
    currentConfig_ = newConfig;
    return true;
}