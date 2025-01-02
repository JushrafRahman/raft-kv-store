#pragma once
#include <string>
#include <vector>

struct NodeAddress {
    std::string ip;
    int port;

    bool operator==(const NodeAddress& other) const {
        return ip == other.ip && port == other.port;
    }
};

struct RaftMessage {
    enum class Type {
        VOTE_REQUEST,
        VOTE_RESPONSE,
        APPEND_ENTRIES,
        APPEND_RESPONSE
    };

    Type type;
    int term;
    int senderId;
    int lastLogIndex;
    int lastLogTerm;
    bool voteGranted;
    std::vector<std::string> entries;
    int prevLogIndex;
    int prevLogTerm;
    bool success;
    int leaderCommit; 
    int lastApplied; 
};

// struct ClientRequest {
//     enum class Type {
//         PUT,
//         GET,
//         DELETE
//     };

//     Type type;
//     std::string key;
//     std::string value;
//     int requestId;
//     int clientId;
// };

// struct ClientResponse {
//     bool success;
//     std::string value;
//     int requestId;
//     bool isLeader;
//     std::string leaderHint;
// };

struct Command {
    enum class Type {
        PUT,
        GET,
        DELETE,
        CONFIG_CHANGE
    };
    
    Type type;
    std::string key;
    std::string value;
    std::string operation;
    NodeAddress server;
};

struct LogEntry {
    int term;
    Command command;
};