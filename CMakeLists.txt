cmake_minimum_required(VERSION 3.10)
project(raft-kv-store)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# main executable
add_executable(raft-kv-store src/main.cpp)

# source files
set(SOURCE_FILES
    src/node/node.cpp
    src/raft/raft.cpp
    src/storage/storage.cpp
)

# library
add_library(raft_lib ${SOURCE_FILES})

# linking library to executable
target_link_libraries(raft-kv-store PRIVATE raft_lib)