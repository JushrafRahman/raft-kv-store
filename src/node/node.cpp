#include "node.hpp"

Node::Node(int id, const std::vector<NodeAddress>& peers) 
    : raft_(id, peers) {}

void Node::start() {
    raft_.start();
}

void Node::stop() {
    raft_.stop();
}