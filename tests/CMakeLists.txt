include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip
)
FetchContent_MakeAvailable(googletest)

set(TEST_FILES
    unit/raft_test.cpp
    unit/storage_test.cpp
    unit/network_test.cpp
    integration/cluster_test.cpp
    integration/fault_injection_test.cpp
    performance/benchmark_test.cpp
)

add_executable(raft_tests ${TEST_FILES})

target_link_libraries(raft_tests
    PRIVATE
        raft_lib
        GTest::gtest_main
        GTest::gmock_main
)

include(GoogleTest)
gtest_discover_tests(raft_tests)