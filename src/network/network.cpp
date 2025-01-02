#include "network.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sstream>

// Helper functions for serialization
void serializeRaftMessage(std::vector<char>& buffer, const RaftMessage& message) {
    // Reserve space for the serialized data
    buffer.clear();
    std::stringstream ss;
    
    // Serialize base fields
    int type = static_cast<int>(message.type);
    ss.write(reinterpret_cast<const char*>(&type), sizeof(type));
    ss.write(reinterpret_cast<const char*>(&message.term), sizeof(message.term));
    ss.write(reinterpret_cast<const char*>(&message.senderId), sizeof(message.senderId));
    ss.write(reinterpret_cast<const char*>(&message.lastLogIndex), sizeof(message.lastLogIndex));
    ss.write(reinterpret_cast<const char*>(&message.lastLogTerm), sizeof(message.lastLogTerm));
    ss.write(reinterpret_cast<const char*>(&message.voteGranted), sizeof(message.voteGranted));
    ss.write(reinterpret_cast<const char*>(&message.prevLogIndex), sizeof(message.prevLogIndex));
    ss.write(reinterpret_cast<const char*>(&message.prevLogTerm), sizeof(message.prevLogTerm));
    ss.write(reinterpret_cast<const char*>(&message.success), sizeof(message.success));

    // Serialize entries vector
    size_t entriesSize = message.entries.size();
    ss.write(reinterpret_cast<const char*>(&entriesSize), sizeof(entriesSize));
    for (const auto& entry : message.entries) {
        size_t entrySize = entry.size();
        ss.write(reinterpret_cast<const char*>(&entrySize), sizeof(entrySize));
        ss.write(entry.data(), entrySize);
    }

    // Copy to buffer
    std::string str = ss.str();
    buffer.assign(str.begin(), str.end());
}

bool deserializeRaftMessage(const std::vector<char>& buffer, RaftMessage& message) {
    std::stringstream ss;
    ss.write(buffer.data(), buffer.size());

    // Deserialize base fields
    int type;
    if (!ss.read(reinterpret_cast<char*>(&type), sizeof(type))) return false;
    message.type = static_cast<RaftMessage::Type>(type);

    if (!ss.read(reinterpret_cast<char*>(&message.term), sizeof(message.term))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.senderId), sizeof(message.senderId))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.lastLogIndex), sizeof(message.lastLogIndex))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.lastLogTerm), sizeof(message.lastLogTerm))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.voteGranted), sizeof(message.voteGranted))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.prevLogIndex), sizeof(message.prevLogIndex))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.prevLogTerm), sizeof(message.prevLogTerm))) return false;
    if (!ss.read(reinterpret_cast<char*>(&message.success), sizeof(message.success))) return false;

    // Deserialize entries vector
    size_t entriesSize;
    if (!ss.read(reinterpret_cast<char*>(&entriesSize), sizeof(entriesSize))) return false;
    
    message.entries.clear();
    for (size_t i = 0; i < entriesSize; i++) {
        size_t entrySize;
        if (!ss.read(reinterpret_cast<char*>(&entrySize), sizeof(entrySize))) return false;
        
        std::string entry;
        entry.resize(entrySize);
        if (!ss.read(&entry[0], entrySize)) return false;
        
        message.entries.push_back(std::move(entry));
    }

    return true;
}

NetworkManager::NetworkManager(int nodeId, int port) 
    : nodeId_(nodeId), port_(port), running_(false) {
    
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
            // Handle client response if needed
            break;
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

    // Send message type
    MessageType type = MessageType::RAFT;
    send(sock, &type, sizeof(type), 0);

    // Serialize and send message
    std::vector<char> buffer;
    serializeRaftMessage(buffer, message);
    
    uint32_t messageSize = buffer.size();
    send(sock, &messageSize, sizeof(messageSize), 0);
    send(sock, buffer.data(), buffer.size(), 0);

    close(sock);
    return true;
}

void NetworkManager::handleRaftMessage(const std::vector<char>& buffer) {
    RaftMessage message;
    if (deserializeRaftMessage(buffer, message) && messageCallback_) {
        messageCallback_(message);
    }
}

void NetworkManager::handleClientRequest(const std::vector<char>& buffer, const NodeAddress& clientAddr) {
    ClientRequest request;
    // Deserialize client request
    // ... implement client request deserialization ...
    
    if (clientCallback_) {
        clientCallback_(request, clientAddr);
    }
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

    // Send message type
    MessageType type = MessageType::CLIENT_RESPONSE;
    send(sock, &type, sizeof(type), 0);

    // Serialize and send response
    std::vector<char> buffer;
    // ... implement response serialization ...
    
    uint32_t responseSize = buffer.size();
    send(sock, &responseSize, sizeof(responseSize), 0);
    send(sock, buffer.data(), buffer.size(), 0);

    close(sock);
    return true;
}

void NetworkManager::setMessageCallback(std::function<void(const RaftMessage&)> callback) {
    messageCallback_ = std::move(callback);
}

void NetworkManager::setClientCallback(std::function<void(const ClientRequest&, const NodeAddress&)> callback) {
    clientCallback_ = std::move(callback);
}