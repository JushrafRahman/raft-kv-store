#pragma once
#include <string>
#include <vector>

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

class RaftNode {
public:
    RaftNode(int id);
    
    // Basic getters
    int getId() const { return id_; }
    NodeState getState() const { return state_; }
    int getCurrentTerm() const { return currentTerm_; }

private:
    int id_;
    NodeState state_;
    int currentTerm_;
    int votedFor_;
    std::vector<std::string> log_;
};