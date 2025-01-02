#include <benchmark/benchmark.h>
#include "../../src/raft/raft.hpp"

static void BenchmarkLogAppend(benchmark::State& state) {
    std::vector<NodeAddress> peers = {
        {"127.0.0.1", 50051},
        {"127.0.0.1", 50052},
        {"127.0.0.1", 50053}
    };
    RaftNode node(0, peers, "/tmp/bench_data", 8080);
    node.start();

    for (auto _ : state) {
        Command cmd;
        cmd.type = Command::Type::PUT;
        cmd.key = "test_key";
        cmd.value = "test_value";
        node.appendEntry(cmd);
    }
}
BENCHMARK(BenchmarkLogAppend);