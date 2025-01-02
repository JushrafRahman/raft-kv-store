#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "raft/raft.hpp"

class MockNetworkManager : public NetworkManager {
public:
    MockNetworkManager(int nodeId, int port) : NetworkManager(nodeId, port) {}

    MOCK_METHOD(bool, sendMessage, (const NodeAddress& target, const RaftMessage& message));
    MOCK_METHOD(void, start, ());
    MOCK_METHOD(void, stop, ());
};

class TestRaftNode : public RaftNode {
public:
    TestRaftNode(int id, const std::vector<NodeAddress>& peers)
        : RaftNode(id, peers, "/tmp/test_data", 8080) {}
    
    // Make processMessage public for testing
    using RaftNode::processMessage;
};

class RaftTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<NodeAddress> peers = {
            {"127.0.0.1", 50051},
            {"127.0.0.1", 50052},
            {"127.0.0.1", 50053}
        };
        node = std::make_unique<TestRaftNode>(0, peers);
    }

    std::unique_ptr<TestRaftNode> node;
};

TEST_F(RaftTest, InitialStateIsFollower) {
    EXPECT_EQ(node->getState(), NodeState::FOLLOWER);
}

TEST_F(RaftTest, LeaderElection) {
    node->start();

    RaftMessage message;
    message.type = RaftMessage::Type::VOTE_RESPONSE;
    message.term = 1;
    message.voteGranted = true;
    message.senderId = 1;

    node->processMessage(message);  // from node 1
    message.senderId = 2;
    node->processMessage(message);  // from node 2

    EXPECT_EQ(node->getState(), NodeState::LEADER);
}

TEST_F(RaftTest, RejectOldTermMessages) {
    RaftMessage message;
    message.type = RaftMessage::Type::APPEND_ENTRIES;
    message.term = 0;  // old term
    
    node->processMessage(message);
    EXPECT_EQ(node->getCurrentTerm(), 0);  // term shouldn't change
}