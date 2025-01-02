#include "node.hpp"

Node::Node(int id, const std::vector<NodeAddress>& peers, 
           const std::string& dataDir, int monitoringPort)
    : raft_(id, peers, dataDir, monitoringPort) {
    storage_ = std::make_unique<KeyValueStore>(raft_);
}

void Node::start() {
    raft_.start();
}

void Node::stop() {
    raft_.stop();
}