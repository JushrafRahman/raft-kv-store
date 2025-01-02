#include <gtest/gtest.h>
#include "../../src/storage/storage.hpp"

class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        store = std::make_unique<KeyValueStore>();
    }

    std::unique_ptr<KeyValueStore> store;
};

TEST_F(StorageTest, PutAndGet) {
    EXPECT_TRUE(store->put("key1", "value1"));
    
    std::string value;
    EXPECT_TRUE(store->get("key1", value));
    EXPECT_EQ(value, "value1");
}

TEST_F(StorageTest, DeleteKey) {
    store->put("key1", "value1");
    EXPECT_TRUE(store->remove("key1"));
    
    std::string value;
    EXPECT_FALSE(store->get("key1", value));
}