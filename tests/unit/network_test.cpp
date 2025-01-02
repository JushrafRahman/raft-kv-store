#include <gtest/gtest.h>
#include "../../src/network/network.hpp"

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        network = std::make_unique<NetworkManager>(0, 50051);
    }

    std::unique_ptr<NetworkManager> network;
};

TEST_F(NetworkTest, MessageSending) {
    NodeAddress target{"127.0.0.1", 50052};
    RaftMessage message;
    message.type = RaftMessage::Type::APPEND_ENTRIES;
    
    EXPECT_TRUE(network->sendMessage(target, message));
}