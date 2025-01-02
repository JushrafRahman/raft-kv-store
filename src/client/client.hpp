#pragma once
#include "client_request.hpp"
#include "../network/network.hpp"
#include <atomic>
#include <random>

class Client {
public:
    Client(const std::string& serverIp, int serverPort);
    ~Client();

    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool remove(const std::string& key);

private:
    bool sendRequest(const ClientRequest& request, ClientResponse& response);
    void handleResponse(const ClientResponse& response);
    void retryWithLeader(const std::string& leaderHint);

    NodeAddress currentServer_;
    std::atomic<int> requestId_{0};
    static constexpr int MAX_RETRIES = 3;
    int clientId_;
    std::random_device rd_;
    std::mt19937 gen_;
};
