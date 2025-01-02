cmake_minimum_required(VERSION 3.10)
project(raft-kv-store)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_subdirectory(src)
add_subdirectory(tests)

# main executable
add_executable(raft-kv-store src/main.cpp)

# libraries
target_link_libraries(raft-kv-store 
    PRIVATE 
        raft_lib
        Threads::Threads
)
