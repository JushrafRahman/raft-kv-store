#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../src/raft/raft.hpp"

class MockNetworkManager : public NetworkManager {
public:
    MOCK_METHOD(bool, sendMessage, (const NodeAddress& target, const RaftMessage& message), (override));
    MOCK_METHOD(void, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
};

class RaftTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<NodeAddress> peers = {
            {"127.0.0.1", 50051},
            {"127.0.0.1", 50052},
            {"127.0.0.1", 50053}
        };
        node = std::make_unique<RaftNode>(0, peers, "/tmp/test_data", 8080);
    }

    void TearDown() override {
        node.reset();
    }

    std::unique_ptr<RaftNode> node;
};

TEST_F(RaftTest, InitialStateIsFollower) {
    EXPECT_EQ(node->getState(), NodeState::FOLLOWER);
}

TEST_F(RaftTest, ElectionTimeout) {
    node->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(node->getState(), NodeState::CANDIDATE);
}

TEST_F(RaftTest, LeaderElection) {
    auto message = RaftMessage{};
    message.type = RaftMessage::Type::VOTE_RESPONSE;
    message.term = 1;
    message.voteGranted = true;

    node->processMessage(message);  // From node 1
    message.senderId = 2;
    node->processMessage(message);  // From node 2

    EXPECT_EQ(node->getState(), NodeState::LEADER);
}