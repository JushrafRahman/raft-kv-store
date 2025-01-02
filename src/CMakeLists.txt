set(SOURCE_FILES
    main.cpp
    node/node.cpp
    raft/raft.cpp
    storage/storage.cpp
    network/network.cpp
)

add_library(raft_lib
    ${SOURCE_FILES}
)

target_include_directories(raft_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})