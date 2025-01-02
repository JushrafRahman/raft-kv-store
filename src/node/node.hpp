#pragma once
#include "../raft/raft.hpp"
#include "../storage/storage.hpp"
#include <vector>

class Node {
public:
    Node(int id, const std::vector<NodeAddress>& peers, 
         const std::string& dataDir, int monitoringPort);
    void start();
    void stop();

private:
    RaftNode raft_;
    std::unique_ptr<KeyValueStore> storage_;
};