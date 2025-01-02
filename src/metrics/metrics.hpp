#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

class MetricsCollector {
public:
    void incrementCounter(const std::string& name);
    void decrementCounter(const std::string& name);
    int64_t getCounter(const std::string& name) const;

    void setGauge(const std::string& name, double value);
    double getGauge(const std::string& name) const;

    void recordLatency(const std::string& name, double milliseconds);
    std::pair<double, double> getLatencyPercentiles(const std::string& name) const;

    void recordRate(const std::string& name);
    double getCurrentRate(const std::string& name) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::atomic<int64_t>> counters_;
    std::map<std::string, std::atomic<double>> gauges_;
    std::map<std::string, std::vector<double>> histograms_;
    std::map<std::string, std::vector<TimePoint>> rates_;
};

class LatencyTimer {
public:
    LatencyTimer(MetricsCollector& collector, const std::string& operation)
        : collector_(collector), operation_(operation), start_(Clock::now()) {}
    
    ~LatencyTimer() {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start_).count();
        collector_.recordLatency(operation_, duration);
    }

private:
    MetricsCollector& collector_;
    std::string operation_;
    TimePoint start_;
};

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
