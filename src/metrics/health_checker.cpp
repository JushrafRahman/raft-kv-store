#include "health_checker.hpp"

void HealthChecker::registerComponent(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    componentHealth_[name] = {true, "Initialized"};
}

void HealthChecker::reportHealth(const std::string& component, bool isHealthy, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    componentHealth_[component] = {isHealthy, message};
}

HealthChecker::HealthStatus HealthChecker::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HealthStatus status;
    status.isHealthy = true;
    
    for (const auto& [component, health] : componentHealth_) {
        status.componentStatus[component] = health.first;
        if (!health.first) {
            status.isHealthy = false;
            status.message += component + ": " + health.second + "; ";
        }
    }
    
    if (status.isHealthy) {
        status.message = "All components healthy";
    }
    
    return status;
}