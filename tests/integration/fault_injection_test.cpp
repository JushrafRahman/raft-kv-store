#include <gtest/gtest.h>
#include "../../src/raft/raft.hpp"

class FaultInjectionTest : public ::testing::Test {
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
};

TEST_F(FaultInjectionTest, NetworkPartition) {
    // simulate network partition
    // TODO: implement network partition simulation
}

TEST_F(FaultInjectionTest, NodeCrash) {
    // simulate node crash and recovery
    // TODO: implement node crash simulation
}