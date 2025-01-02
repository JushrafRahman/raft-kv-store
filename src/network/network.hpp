#pragma once
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../client/client_request.hpp"

class NetworkManager {
public:
    NetworkManager(int nodeId, int port);
    ~NetworkManager();

    void start();
    void stop();
    bool sendMessage(const NodeAddress& target, const RaftMessage& message);
    bool sendResponse(const NodeAddress& target, const ClientResponse& response);
    void setMessageCallback(std::function<void(const RaftMessage&)> callback);
    void setClientCallback(std::function<void(const ClientRequest&, const NodeAddress&)> callback);

private:
    void listenThread();
    void handleConnection(int clientSocket);
    void handleRaftMessage(const std::vector<char>& buffer);
    void handleClientRequest(const std::vector<char>& buffer, const NodeAddress& clientAddr);

    enum class MessageType {
        RAFT,
        CLIENT_REQUEST,
        CLIENT_RESPONSE
    };

    int nodeId_;
    int serverSocket_;
    int port_;
    bool running_;
    std::thread listenerThread_;
    std::function<void(const RaftMessage&)> messageCallback_;
    std::function<void(const ClientRequest&, const NodeAddress&)> clientCallback_;
};
