#pragma once
#include "../metrics/metrics.hpp"
#include <thread>
#include <string>

class MonitoringServer {
public:
    MonitoringServer(int port, MetricsCollector& metrics, HealthChecker& health);
    ~MonitoringServer();

    void start();
    void stop();

private:
    void runServer();
    std::string generateMetricsResponse();
    std::string generateHealthResponse();
    void handleRequest(int clientSocket);

    int port_;
    int serverSocket_;
    bool running_;
    std::thread serverThread_;
    MetricsCollector& metrics_;
    HealthChecker& health_;
};