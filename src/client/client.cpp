#include "client.hpp"
#include <iostream>
#include <thread>

Client::Client(const std::string& serverIp, int serverPort) 
    : currentServer_{serverIp, serverPort}
    , gen_(rd_()) {
    std::uniform_int_distribution<> dis(1, 1000000);
    clientId_ = dis(gen_);
}

Client::~Client() {}

bool Client::put(const std::string& key, const std::string& value) {
    ClientRequest request;
    request.type = ClientRequest::Type::PUT;
    request.key = key;
    request.value = value;
    request.requestId = ++requestId_;
    request.clientId = clientId_;

    ClientResponse response;
    return sendRequest(request, response);
}

bool Client::get(const std::string& key, std::string& value) {
    ClientRequest request;
    request.type = ClientRequest::Type::GET;
    request.key = key;
    request.requestId = ++requestId_;
    request.clientId = clientId_;

    ClientResponse response;
    if (sendRequest(request, response)) {
        value = response.value;
        return true;
    }
    return false;
}

bool Client::remove(const std::string& key) {
    ClientRequest request;
    request.type = ClientRequest::Type::DELETE;
    request.key = key;
    request.requestId = ++requestId_;
    request.clientId = clientId_;

    ClientResponse response;
    return sendRequest(request, response);
}

bool Client::sendRequest(const ClientRequest& request, ClientResponse& response) {
    for (int retry = 0; retry < MAX_RETRIES; ++retry) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            continue;
        }

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(currentServer_.port);
        inet_pton(AF_INET, currentServer_.ip.c_str(), &serverAddr.sin_addr);

        if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Failed to connect to server" << std::endl;
            close(sock);
            continue;
        }

        close(sock);

        if (!response.success && !response.isLeader && !response.leaderHint.empty()) {
            retryWithLeader(response.leaderHint);
            continue;
        }

        return response.success;
    }

    return false;
}

void Client::retryWithLeader(const std::string& leaderHint) {
    size_t colonPos = leaderHint.find(':');
    if (colonPos != std::string::npos) {
        currentServer_.ip = leaderHint.substr(0, colonPos);
        currentServer_.port = std::stoi(leaderHint.substr(colonPos + 1));
    }
}