#include "persistence.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

PersistentStorage::PersistentStorage(const std::string& dataDir, int nodeId)
    : dataDir_(dataDir), nodeId_(nodeId) {
    ensureDirectoryExists();
}

bool PersistentStorage::saveCurrentTerm(int term) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(getStateFile(), std::ios::binary);
    if (!file) return false;
    
    int votedFor;
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
        
        size_t cmdSize = entry.command.size();
        file.write(reinterpret_cast<const char*>(&cmdSize), sizeof(cmdSize));
        file.write(entry.command.data(), cmdSize);
        
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
        
        size_t cmdSize = entry.command.size();
        file.write(reinterpret_cast<const char*>(&cmdSize), sizeof(cmdSize));
        file.write(entry.command.data(), cmdSize);
        
        if (file.fail()) return false;
    }
    
    return file.flush().good();
}

bool PersistentStorage::getLogEntries(std::vector<LogEntry>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(getLogFile(), std::ios::binary);
    if (!file) return true;
    
    entries.clear();
    while (file && !file.eof()) {
        LogEntry entry;
        
        if (!file.read(reinterpret_cast<char*>(&entry.term), sizeof(entry.term))) {
            if (file.eof()) break;
            return false;
        }
        
        size_t cmdSize;
        if (!file.read(reinterpret_cast<char*>(&cmdSize), sizeof(cmdSize))) {
            return false;
        }
        
        std::string command;
        command.resize(cmdSize);
        if (!file.read(&command[0], cmdSize)) {
            return false;
        }
        
        entry.command = std::move(command);
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