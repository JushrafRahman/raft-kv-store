#pragma once
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct NodeAddress {
    std::string ip;
    int port;
};

struct RaftMessage {
    enum Type {
        VOTE_REQUEST,
        VOTE_RESPONSE,
        APPEND_ENTRIES,
        APPEND_RESPONSE
    };

    Type type;
    int term;
    int senderId;
    std::vector<std::string> entries;
    int prevLogIndex;
    int prevLogTerm;
    bool success;
};

class NetworkManager {
public:
    NetworkManager(int nodeId, int port);
    ~NetworkManager();

    // to listen for incoming messages
    void start();
    
    // stop network manager
    void stop();

    // to send message to a specific node
    bool sendMessage(const NodeAddress& target, const RaftMessage& message);

    // callback for receiving messages
    void setMessageCallback(std::function<void(const RaftMessage&)> callback);

private:
    void listenThread();
    void handleConnection(int clientSocket);

    int nodeId_;
    int serverSocket_;
    int port_;
    bool running_;
    std::thread listenerThread_;
    std::function<void(const RaftMessage&)> messageCallback_;
};
