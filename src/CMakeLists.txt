set(SOURCE_FILES
    main.cpp
    node/node.cpp
    raft/raft.cpp
    storage/storage.cpp
    network/network.cpp
    metrics/metrics.cpp
    metrics/health_checker.cpp
    monitoring/monitoring_server.cpp
    persistence/persistence.cpp
)

add_library(raft_lib ${SOURCE_FILES})

target_include_directories(raft_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link against required libraries
target_link_libraries(raft_lib PUBLIC Threads::Threads)