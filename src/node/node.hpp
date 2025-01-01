#pragma once
#include "../raft/raft.hpp"
#include "../storage/storage.hpp"

class Node {
public:
    Node(int id);
    void start();
    void stop();

private:
    RaftNode raft_;
    KeyValueStore storage_;
};