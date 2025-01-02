#pragma once
#include <string>
#include <vector>
#include <fstream>
#include "../raft/raft.hpp"

class PersistentStorage {
public:
    PersistentStorage(const std::string& dataDir, int nodeId);
    
    bool saveCurrentTerm(int term);
    bool saveVotedFor(int votedFor);
    bool getPersistedState(int& term, int& votedFor);

    bool appendLogEntries(const std::vector<LogEntry>& entries, int startIndex);
    bool truncateLog(int lastIndex);
    bool getLogEntries(std::vector<LogEntry>& entries);
    
    bool createSnapshot(int lastIncludedIndex, int lastIncludedTerm);
    bool getLatestSnapshot(int& lastIncludedIndex, int& lastIncludedTerm);

private:
    std::string getStateFile() const;
    std::string getLogFile() const;
    std::string getSnapshotFile() const;
    bool ensureDirectoryExists() const;
    
    std::string dataDir_;
    int nodeId_;
    std::mutex mutex_;
};
