#pragma once
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../common/types.hpp"
#include "../client/client_request.hpp"

class NetworkManager {
public:
    NetworkManager(int nodeId, int port);
    virtual ~NetworkManager();

    enum class MessageType {
        RAFT,
        CLIENT_REQUEST,
        CLIENT_RESPONSE
    };

    virtual void start();
    virtual void stop();
    virtual bool sendMessage(const NodeAddress& target, const RaftMessage& message);
    virtual bool sendResponse(const NodeAddress& target, const ClientResponse& response);
    virtual void setMessageCallback(std::function<void(const RaftMessage&)> callback);
    virtual void setClientCallback(std::function<void(const ClientRequest&, const NodeAddress&)> callback);

protected:
    void listenThread();
    virtual void handleConnection(int clientSocket);
    virtual void handleRaftMessage(const std::vector<char>& buffer);
    virtual void handleClientRequest(const std::vector<char>& buffer, const NodeAddress& clientAddr);

    int nodeId_;
    int serverSocket_;
    int port_;
    bool running_;
    std::thread listenerThread_;
    std::function<void(const RaftMessage&)> messageCallback_;
    std::function<void(const ClientRequest&, const NodeAddress&)> clientCallback_;
};