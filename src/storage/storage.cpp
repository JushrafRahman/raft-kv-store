#include "storage.hpp"

KeyValueStore::KeyValueStore(RaftNode& raft) : raft_(raft) {
    raft_.setApplyCallback([this](const Command& cmd) {
        applyCommand(cmd);
    });
}

bool KeyValueStore::put(const std::string& key, const std::string& value) {
    Command cmd;
    cmd.type = Command::Type::PUT;
    cmd.key = key;
    cmd.value = value;
    return raft_.appendEntry(cmd);
}

bool KeyValueStore::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it != store_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

bool KeyValueStore::remove(const std::string& key) {
    Command cmd;
    cmd.type = Command::Type::DELETE;
    cmd.key = key;
    return raft_.appendEntry(cmd);
}

void KeyValueStore::applyCommand(const Command& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (cmd.type) {
        case Command::Type::PUT:
            store_[cmd.key] = cmd.value;
            break;
            
        case Command::Type::DELETE:
            store_.erase(cmd.key);
            break;
            
        case Command::Type::GET:
            break;
    }
}