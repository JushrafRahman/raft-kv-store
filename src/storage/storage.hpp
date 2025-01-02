#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include "../raft/raft.hpp"

class KeyValueStore {
public:
    KeyValueStore(RaftNode& raft);
    
    // api
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool remove(const std::string& key);

private:
    void applyCommand(const Command& cmd);
    
    std::unordered_map<std::string, std::string> store_;
    std::mutex mutex_;
    RaftNode& raft_;
};