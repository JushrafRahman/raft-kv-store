#pragma once
#include "../raft/raft.hpp"
#include "../storage/storage.hpp"
#include <vector>

class Node {
public:
    Node(int id, const std::vector<NodeAddress>& peers);
    void start();
    void stop();

private:
    RaftNode raft_;
    KeyValueStore storage_;
};