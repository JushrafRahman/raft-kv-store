#include "network.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>

NetworkManager::NetworkManager(int nodeId, int port) 
    : nodeId_(nodeId), port_(port), running_(false) {
    
    // create server socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("Failed to set socket options");
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(serverSocket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Failed to bind socket");
    }
}

NetworkManager::~NetworkManager() {
    stop();
    close(serverSocket_);
}

void NetworkManager::start() {
    running_ = true;
    listenerThread_ = std::thread(&NetworkManager::listenThread, this);
}

void NetworkManager::stop() {
    running_ = false;
    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }
}

void NetworkManager::listenThread() {
    listen(serverSocket_, 5);

    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (running_) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }

        std::thread(&NetworkManager::handleConnection, this, clientSocket).detach();
    }
}

void NetworkManager::handleConnection(int clientSocket) {
    uint32_t messageSize;
    if (recv(clientSocket, &messageSize, sizeof(messageSize), 0) != sizeof(messageSize)) {
        close(clientSocket);
        return;
    }

    // read message data
    std::vector<char> buffer(messageSize);
    if (recv(clientSocket, buffer.data(), messageSize, 0) != messageSize) {
        close(clientSocket);
        return;
    }

    // deserialize message
    RaftMessage message;
    // ... deserialize from buffer ...

    if (messageCallback_) {
        messageCallback_(message);
    }

    close(clientSocket);
}

bool NetworkManager::sendMessage(const NodeAddress& target, const RaftMessage& message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return false;
    }

    // serialize message
    std::vector<char> buffer;
    // ... serialize message to buffer ...

    uint32_t messageSize = buffer.size();
    send(sock, &messageSize, sizeof(messageSize), 0);
    send(sock, buffer.data(), buffer.size(), 0);

    close(sock);
    return true;
}

void NetworkManager::setMessageCallback(std::function<void(const RaftMessage&)> callback) {
    messageCallback_ = std::move(callback);
}

void NetworkManager::handleConnection(int clientSocket) {
    MessageType type;
    if (recv(clientSocket, &type, sizeof(type), 0) != sizeof(type)) {
        close(clientSocket);
        return;
    }

    uint32_t messageSize;
    if (recv(clientSocket, &messageSize, sizeof(messageSize), 0) != sizeof(messageSize)) {
        close(clientSocket);
        return;
    }

    std::vector<char> buffer(messageSize);
    if (recv(clientSocket, buffer.data(), messageSize, 0) != messageSize) {
        close(clientSocket);
        return;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(clientSocket, (struct sockaddr*)&addr, &len);
    NodeAddress clientAddr{
        inet_ntoa(addr.sin_addr),
        ntohs(addr.sin_port)
    };

    switch (type) {
        case MessageType::RAFT:
            handleRaftMessage(buffer);
            break;
        case MessageType::CLIENT_REQUEST:
            handleClientRequest(buffer, clientAddr);
            break;
        case MessageType::CLIENT_RESPONSE:
            break;
    }

    close(clientSocket);
}

bool NetworkManager::sendResponse(const NodeAddress& target, const ClientResponse& response) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return false;
    }

    MessageType type = MessageType::CLIENT_RESPONSE;
    send(sock, &type, sizeof(type), 0);

    close(sock);
    return true;
}

void NetworkManager::setClientCallback(std::function<void(const ClientRequest&, const NodeAddress&)> callback) {
    clientCallback_ = std::move(callback);
}