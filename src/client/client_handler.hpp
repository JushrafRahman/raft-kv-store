#pragma once
#include "client_request.hpp"
#include "../network/network.hpp"
#include "../storage/storage.hpp"
#include <queue>
#include <map>

class ClientHandler {
public:
    ClientHandler(KeyValueStore& store, NetworkManager& network);
    void handleRequest(const ClientRequest& request, const NodeAddress& clientAddress);
    void processResponse(const ClientResponse& response, const NodeAddress& clientAddress);

private:
    KeyValueStore& store_;
    NetworkManager& network_;
    std::map<int, std::pair<ClientRequest, NodeAddress>> pendingRequests_;
    std::mutex requestMutex_;
};