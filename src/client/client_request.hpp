#pragma once
#include <string>

struct ClientRequest {
    enum class Type {
        PUT,
        GET,
        DELETE
    };

    Type type;
    std::string key;
    std::string value;
    int requestId;
    int clientId;
};

struct ClientResponse {
    bool success;
    std::string value;
    int requestId;
    bool isLeader;
    std::string leaderHint;
};
