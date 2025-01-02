#include "client_handler.hpp"

ClientHandler::ClientHandler(KeyValueStore& store, NetworkManager& network)
    : store_(store), network_(network) {}

void ClientHandler::handleRequest(const ClientRequest& request, const NodeAddress& clientAddress) {
    ClientResponse response;
    response.requestId = request.requestId;
    
    if (!store_.isLeader()) {
        response.success = false;
        response.isLeader = false;
        response.leaderHint = store_.getLeaderAddress();
        network_.sendResponse(clientAddress, response);
        return;
    }

    response.isLeader = true;

    switch (request.type) {
        case ClientRequest::Type::PUT: {
            bool success = store_.put(request.key, request.value);
            response.success = success;
            break;
        }
        case ClientRequest::Type::GET: {
            std::string value;
            bool success = store_.get(request.key, value);
            response.success = success;
            response.value = value;
            break;
        }
        case ClientRequest::Type::DELETE: {
            bool success = store_.remove(request.key);
            response.success = success;
            break;
        }
    }

    if (request.type != ClientRequest::Type::GET) {
        std::lock_guard<std::mutex> lock(requestMutex_);
        pendingRequests_[request.requestId] = {request, clientAddress};
    } else {
        network_.sendResponse(clientAddress, response);
    }
}

void ClientHandler::processResponse(const ClientResponse& response, const NodeAddress& clientAddress) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    auto it = pendingRequests_.find(response.requestId);
    if (it != pendingRequests_.end()) {
        network_.sendResponse(it->second.second, response);
        pendingRequests_.erase(it);
    }
}