#include "metrics.hpp"
#include <algorithm>
#include <cmath>

void MetricsCollector::incrementCounter(const std::string& name) {
    counters_[name]++;
}

void MetricsCollector::decrementCounter(const std::string& name) {
    counters_[name]--;
}

int64_t MetricsCollector::getCounter(const std::string& name) const {
    auto it = counters_.find(name);
    if (it != counters_.end()) {
        return it->second.load();
    }
    return 0;
}

void MetricsCollector::setGauge(const std::string& name, double value) {
    gauges_[name].store(value);
}

double MetricsCollector::getGauge(const std::string& name) const {
    auto it = gauges_.find(name);
    if (it != gauges_.end()) {
        return it->second.load();
    }
    return 0.0;
}

void MetricsCollector::recordLatency(const std::string& name, double milliseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    histograms_[name].push_back(milliseconds);
    
    const size_t MAX_SAMPLES = 1000;
    if (histograms_[name].size() > MAX_SAMPLES) {
        histograms_[name].erase(histograms_[name].begin());
    }
}

std::pair<double, double> MetricsCollector::getLatencyPercentiles(
    const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = histograms_.find(name);
    if (it == histograms_.end() || it->second.empty()) {
        return {0.0, 0.0};
    }

    std::vector<double> sorted = it->second;
    std::sort(sorted.begin(), sorted.end());

    size_t p50_idx = sorted.size() * 0.5;
    size_t p99_idx = sorted.size() * 0.99;

    return {sorted[p50_idx], sorted[p99_idx]};
}

void MetricsCollector::recordRate(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    rates_[name].push_back(Clock::now());
    
    auto& times = rates_[name];
    auto cutoff = Clock::now() - std::chrono::minutes(1);
    times.erase(
        std::remove_if(times.begin(), times.end(),
            [cutoff](const TimePoint& tp) { return tp < cutoff; }),
        times.end()
    );
}

double MetricsCollector::getCurrentRate(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rates_.find(name);
    if (it == rates_.end() || it->second.empty()) {
        return 0.0;
    }

    auto now = Clock::now();
    auto cutoff = now - std::chrono::seconds(60);
    int count = std::count_if(it->second.begin(), it->second.end(),
        [cutoff](const TimePoint& tp) { return tp >= cutoff; });
    
    return count / 60.0;  // Events per second
}

void HealthChecker::registerComponent(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    componentHealth_[name] = {true, "Initialized"};
}

void HealthChecker::reportHealth(
    const std::string& component,
    bool isHealthy,
    const std::string& message) {
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