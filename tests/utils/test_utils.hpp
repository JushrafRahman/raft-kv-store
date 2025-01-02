#pragma once
#include <random>
#include <chrono>
#include <thread>
#include <functional>
#include "../../src/network/network.hpp"

class NetworkPartitioner {
public:
    NetworkPartitioner(std::vector<NodeAddress>& nodes) : nodes_(nodes) {}

    void partition(const std::vector<int>& group1, const std::vector<int>& group2) {
        partitioned_ = true;
        group1_ = group1;
        group2_ = group2;
    }

    void heal() {
        partitioned_ = false;
    }

    bool canCommunicate(int from, int to) {
        if (!partitioned_) return true;
        
        bool fromInGroup1 = std::find(group1_.begin(), group1_.end(), from) != group1_.end();
        bool toInGroup1 = std::find(group1_.begin(), group1_.end(), to) != group1_.end();
        
        return (fromInGroup1 == toInGroup1);
    }

private:
    std::vector<NodeAddress>& nodes_;
    bool partitioned_ = false;
    std::vector<int> group1_;
    std::vector<int> group2_;
};

class ChaosMonkey {
public:
    ChaosMonkey(std::vector<std::unique_ptr<RaftNode>>& nodes) 
        : nodes_(nodes), running_(false) {}

    void start() {
        running_ = true;
        thread_ = std::thread(&ChaosMonkey::run, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void run() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> nodeDist(0, nodes_.size() - 1);
        std::uniform_int_distribution<> actionDist(0, 2);

        while (running_) {
            int nodeIdx = nodeDist(gen);
            int action = actionDist(gen);

            switch (action) {
                case 0:
                    nodes_[nodeIdx]->stop();
                    break;
                case 1:
                    nodes_[nodeIdx]->start();
                    break;
                case 2: 
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    break;
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::vector<std::unique_ptr<RaftNode>>& nodes_;
    bool running_;
    std::thread thread_;
};

class TestCluster {
public:
    TestCluster(int size) {
        for (int i = 0; i < size; i++) {
            peers_.push_back({"127.0.0.1", 50051 + i});
        }

        for (int i = 0; i < size; i++) {
            nodes_.push_back(std::make_unique<RaftNode>(
                i, peers_, "/tmp/test_data_" + std::to_string(i), 8080 + i));
        }

        networkPartitioner_ = std::make_unique<NetworkPartitioner>(peers_);
        chaosMonkey_ = std::make_unique<ChaosMonkey>(nodes_);
    }

    void start() {
        for (auto& node : nodes_) {
            node->start();
        }
    }

    void stop() {
        for (auto& node : nodes_) {
            node->stop();
        }
    }

    RaftNode* getLeader() {
        for (auto& node : nodes_) {
            if (node->getState() == NodeState::LEADER) {
                return node.get();
            }
        }
        return nullptr;
    }

    void injectFault(const std::function<void()>& fault) {
        fault();
    }

    void waitForLeader(int timeoutSeconds = 5) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (getLeader() != nullptr) break;
            
            if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > timeoutSeconds) {
                throw std::runtime_error("Timeout waiting for leader");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    NetworkPartitioner* getNetworkPartitioner() { return networkPartitioner_.get(); }
    ChaosMonkey* getChaosMonkey() { return chaosMonkey_.get(); }

private:
    std::vector<NodeAddress> peers_;
    std::vector<std::unique_ptr<RaftNode>> nodes_;
    std::unique_ptr<NetworkPartitioner> networkPartitioner_;
    std::unique_ptr<ChaosMonkey> chaosMonkey_;
};