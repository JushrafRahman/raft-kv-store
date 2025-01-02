#pragma once
#include <string>
#include <map>
#include <mutex>

class HealthChecker {
public:
    struct HealthStatus {
        bool isHealthy;
        std::string message;
        std::map<std::string, bool> componentStatus;
    };

    void registerComponent(const std::string& name);
    void reportHealth(const std::string& component, bool isHealthy, const std::string& message);
    HealthStatus getStatus() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::pair<bool, std::string>> componentHealth_;
};