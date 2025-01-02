#include "monitoring_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <iostream>

MonitoringServer::MonitoringServer(
    int port,
    MetricsCollector& metrics,
    HealthChecker& health)
    : port_(port)
    , metrics_(metrics)
    , health_(health)
    , running_(false) {
    
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        throw std::runtime_error("Failed to create monitoring server socket");
    }

    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(serverSocket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Failed to bind monitoring server socket");
    }
}

MonitoringServer::~MonitoringServer() {
    stop();
    close(serverSocket_);
}

void MonitoringServer::start() {
    running_ = true;
    serverThread_ = std::thread(&MonitoringServer::runServer, this);
}

void MonitoringServer::stop() {
    running_ = false;
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void MonitoringServer::runServer() {
    listen(serverSocket_, 5);

    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket_, 
                                (struct sockaddr*)&clientAddr, 
                                &clientLen);
        
        if (clientSocket < 0) {
            if (running_) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }

        handleRequest(clientSocket);
        close(clientSocket);
    }
}

void MonitoringServer::handleRequest(int clientSocket) {
    char buffer[1024] = {0};
    read(clientSocket, buffer, sizeof(buffer));

    std::string response;
    if (strstr(buffer, "GET /metrics") != nullptr) {
        response = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n\r\n" +
                  generateMetricsResponse();
    }
    else if (strstr(buffer, "GET /health") != nullptr) {
        response = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n\r\n" +
                  generateHealthResponse();
    }
    else {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
    }

    write(clientSocket, response.c_str(), response.length());
}

std::string MonitoringServer::generateMetricsResponse() {
    std::ostringstream ss;

    // Counter metrics
    ss << "# HELP raft_leader_elections_total Total number of leader elections\n";
    ss << "raft_leader_elections_total " 
       << metrics_.getCounter("leader_elections") << "\n";

    ss << "# HELP raft_log_entries_total Total number of log entries\n";
    ss << "raft_log_entries_total " 
       << metrics_.getCounter("log_entries") << "\n";

    // Gauge metrics
    ss << "# HELP raft_cluster_size Current number of nodes in the cluster\n";
    ss << "raft_cluster_size " 
       << metrics_.getGauge("cluster_size") << "\n";

    // Latency metrics
    auto [p50, p99] = metrics_.getLatencyPercentiles("append_latency");
    ss << "# HELP raft_append_latency_p50 Append latency 50th percentile\n";
    ss << "raft_append_latency_p50 " << p50 << "\n";
    ss << "# HELP raft_append_latency_p99 Append latency 99th percentile\n";
    ss << "raft_append_latency_p99 " << p99 << "\n";

    // Rate metrics
    ss << "# HELP raft_operations_per_second Current operations per second\n";
    ss << "raft_operations_per_second " 
       << metrics_.getCurrentRate("operations") << "\n";

    return ss.str();
}

std::string MonitoringServer::generateHealthResponse() {
    auto status = health_.getStatus();
    std::ostringstream ss;

    ss << "{\n";
    ss << "  \"status\": \"" << (status.isHealthy ? "healthy" : "unhealthy") << "\",\n";
    ss << "  \"message\": \"" << status.message << "\",\n";
    ss << "  \"components\": {\n";

    bool first = true;
    for (const auto& [component, healthy] : status.componentStatus) {
        if (!first) ss << ",\n";
        ss << "    \"" << component << "\": " << (healthy ? "true" : "false");
        first = false;
    }

    ss << "\n  }\n}";
    return ss.str();
}