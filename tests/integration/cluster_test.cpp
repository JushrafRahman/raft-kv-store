#include <gtest/gtest.h>
#include "../../src/raft/raft.hpp"

class ClusterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // create a 3-node cluster
        std::vector<NodeAddress> peers = {
            {"127.0.0.1", 50051},
            {"127.0.0.1", 50052},
            {"127.0.0.1", 50053}
        };

        for (int i = 0; i < 3; i++) {
            nodes.push_back(std::make_unique<RaftNode>(
                i, peers, "/tmp/test_data_" + std::to_string(i), 8080 + i));
        }
    }

    std::vector<std::unique_ptr<RaftNode>> nodes;
};

TEST_F(ClusterTest, LeaderElection) {
    // start all nodes
    for (auto& node : nodes) {
        node->start();
    }

    // wait for leader election
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // verify only one leader
    int leaderCount = 0;
    for (const auto& node : nodes) {
        if (node->getState() == NodeState::LEADER) {
            leaderCount++;
        }
    }
    EXPECT_EQ(leaderCount, 1);
}
