# Fault-Tolerant Distributed Key-Value Store

A distributed key-value store implementation using the Raft consensus algorithm. This project provides a fault-tolerant, consistent, and highly available key-value storage system.

## Features

- **Raft Consensus Implementation**
  - Leader Election
  - Log Replication
  - Membership Changes
  - Persistent Storage
  - Heartbeat Mechanism

- **Key-Value Store**
  - Basic operations (PUT, GET, DELETE)
  - Consistent replication across nodes
  - Atomic operations

- **Fault Tolerance**
  - Survives node failures
  - Automatic leader election
  - Data persistence across restarts

- **Monitoring & Metrics**
  - Performance metrics
  - Health checking
  - Operation latency tracking
  - Throughput monitoring

## Architecture

- **RaftNode**: Core implementation of the Raft consensus algorithm
- **NetworkManager**: Handles network communication between nodes
- **KeyValueStore**: Manages the actual key-value storage
- **PersistentStorage**: Handles data persistence and recovery
- **MetricsCollector**: Collects and exposes system metrics

## Building the Project

### Prerequisites
- C++17 compatible compiler
- CMake 3.10 or higher
- POSIX-compliant operating system
- GoogleTest (automatically fetched by CMake)

### Build Instructions
```bash
mkdir build
cd build
cmake ..
make
```

### Running Tests
```bash
./tests/raft_tests
```

## Running the System

### Starting a Node
```bash
./raft-kv-store <node-id> <data-dir> <monitoring-port>
```

Example for a 3-node cluster:
```bash
# Terminal 1
./raft-kv-store 0 /tmp/raft-data-0 8080

# Terminal 2
./raft-kv-store 1 /tmp/raft-data-1 8081

# Terminal 3
./raft-kv-store 2 /tmp/raft-data-2 8082
```

### Monitoring
Access metrics and health status:
```bash
curl http://localhost:8080/metrics  # For metrics
curl http://localhost:8080/health   # For health status
```
