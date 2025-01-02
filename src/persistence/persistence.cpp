#include "persistence.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

// helper functions for Command serialization/deserialization
void serializeCommand(std::ofstream& file, const Command& cmd) {
    int type = static_cast<int>(cmd.type);
    file.write(reinterpret_cast<const char*>(&type), sizeof(type));

    size_t keySize = cmd.key.size();
    file.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
    file.write(cmd.key.data(), keySize);

    size_t valueSize = cmd.value.size();
    file.write(reinterpret_cast<const char*>(&valueSize), sizeof(valueSize));
    file.write(cmd.value.data(), valueSize);

    size_t opSize = cmd.operation.size();
    file.write(reinterpret_cast<const char*>(&opSize), sizeof(opSize));
    file.write(cmd.operation.data(), opSize);

    // serialize NodeAddress
    size_t ipSize = cmd.server.ip.size();
    file.write(reinterpret_cast<const char*>(&ipSize), sizeof(ipSize));
    file.write(cmd.server.ip.data(), ipSize);
    file.write(reinterpret_cast<const char*>(&cmd.server.port), sizeof(cmd.server.port));
}

bool deserializeCommand(std::ifstream& file, Command& cmd) {
    int type;
    if (!file.read(reinterpret_cast<char*>(&type), sizeof(type))) return false;
    cmd.type = static_cast<Command::Type>(type);

    // read key
    size_t keySize;
    if (!file.read(reinterpret_cast<char*>(&keySize), sizeof(keySize))) return false;
    cmd.key.resize(keySize);
    if (!file.read(&cmd.key[0], keySize)) return false;

    // read value
    size_t valueSize;
    if (!file.read(reinterpret_cast<char*>(&valueSize), sizeof(valueSize))) return false;
    cmd.value.resize(valueSize);
    if (!file.read(&cmd.value[0], valueSize)) return false;

    // read operation
    size_t opSize;
    if (!file.read(reinterpret_cast<char*>(&opSize), sizeof(opSize))) return false;
    cmd.operation.resize(opSize);
    if (!file.read(&cmd.operation[0], opSize)) return false;

    // read NodeAddress
    size_t ipSize;
    if (!file.read(reinterpret_cast<char*>(&ipSize), sizeof(ipSize))) return false;
    cmd.server.ip.resize(ipSize);
    if (!file.read(&cmd.server.ip[0], ipSize)) return false;
    if (!file.read(reinterpret_cast<char*>(&cmd.server.port), sizeof(cmd.server.port))) return false;

    return true;
}

PersistentStorage::PersistentStorage(const std::string& dataDir, int nodeId)
    : dataDir_(dataDir), nodeId_(nodeId) {
    ensureDirectoryExists();
}

bool PersistentStorage::saveCurrentTerm(int term) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(getStateFile(), std::ios::binary);
    if (!file) return false;
    
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&term), sizeof(term));
    if (file.fail()) return false;
    
    return true;
}

bool PersistentStorage::saveVotedFor(int votedFor) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(getStateFile(), std::ios::binary | std::ios::app);
    if (!file) return false;
    
    file.seekp(sizeof(int));
    file.write(reinterpret_cast<char*>(&votedFor), sizeof(votedFor));
    return !file.fail();
}

bool PersistentStorage::getPersistedState(int& term, int& votedFor) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(getStateFile(), std::ios::binary);
    if (!file) {
        term = 0;
        votedFor = -1;
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&term), sizeof(term));
    file.read(reinterpret_cast<char*>(&votedFor), sizeof(votedFor));
    
    return !file.fail();
}

bool PersistentStorage::appendLogEntries(const std::vector<LogEntry>& entries, int startIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(getLogFile(), std::ios::binary | std::ios::app);
    if (!file) return false;
    
    for (const auto& entry : entries) {
        file.write(reinterpret_cast<const char*>(&entry.term), sizeof(entry.term));
        serializeCommand(file, entry.command);
        if (file.fail()) return false;
    }
    
    return file.flush().good();
}

bool PersistentStorage::truncateLog(int lastIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> entries;
    if (!getLogEntries(entries)) return false;
    
    if (lastIndex >= static_cast<int>(entries.size())) return true;
    
    std::ofstream file(getLogFile(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    
    for (int i = 0; i <= lastIndex; i++) {
        const auto& entry = entries[i];
        file.write(reinterpret_cast<const char*>(&entry.term), sizeof(entry.term));
        serializeCommand(file, entry.command);
        if (file.fail()) return false;
    }
    
    return file.flush().good();
}

bool PersistentStorage::getLogEntries(std::vector<LogEntry>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(getLogFile(), std::ios::binary);
    if (!file) return true;  // Empty log is valid
    
    entries.clear();
    while (file && !file.eof()) {
        LogEntry entry;
        
        if (!file.read(reinterpret_cast<char*>(&entry.term), sizeof(entry.term))) {
            if (file.eof()) break;
            return false;
        }
        
        if (!deserializeCommand(file, entry.command)) {
            return false;
        }
        
        entries.push_back(std::move(entry));
    }
    
    return true;
}

bool PersistentStorage::createSnapshot(int lastIncludedIndex, int lastIncludedTerm) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(getSnapshotFile(), std::ios::binary);
    if (!file) return false;
    
    file.write(reinterpret_cast<const char*>(&lastIncludedIndex), sizeof(lastIncludedIndex));
    file.write(reinterpret_cast<const char*>(&lastIncludedTerm), sizeof(lastIncludedTerm));
    
    return file.flush().good();
}

bool PersistentStorage::getLatestSnapshot(int& lastIncludedIndex, int& lastIncludedTerm) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(getSnapshotFile(), std::ios::binary);
    if (!file) {
        lastIncludedIndex = 0;
        lastIncludedTerm = 0;
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&lastIncludedIndex), sizeof(lastIncludedIndex));
    file.read(reinterpret_cast<char*>(&lastIncludedTerm), sizeof(lastIncludedTerm));
    
    return !file.fail();
}

std::string PersistentStorage::getStateFile() const {
    return dataDir_ + "/node" + std::to_string(nodeId_) + "_state";
}

std::string PersistentStorage::getLogFile() const {
    return dataDir_ + "/node" + std::to_string(nodeId_) + "_log";
}

std::string PersistentStorage::getSnapshotFile() const {
    return dataDir_ + "/node" + std::to_string(nodeId_) + "_snapshot";
}

bool PersistentStorage::ensureDirectoryExists() const {
    try {
        if (!fs::exists(dataDir_)) {
            return fs::create_directories(dataDir_);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directory: " << e.what() << std::endl;
        return false;
    }
}