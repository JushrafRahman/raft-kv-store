#include "raft/raft.hpp"
#include <iostream>
#include <vector>
#include <filesystem>

std::vector<NodeAddress> setupClusterConfig() {
    // configure a 3-node cluster for testing
    std::vector<NodeAddress> nodes = {
        {"127.0.0.1", 50051},
        {"127.0.0.1", 50052},
        {"127.0.0.1", 50053}
    };
    return nodes;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <node-id> <data-dir> <monitoring-port>" << std::endl;
        return 1;
    }

    try {
        int nodeId = std::stoi(argv[1]);
        std::string dataDir = argv[2];
        int monitoringPort = std::stoi(argv[3]);

        if (nodeId < 0 || nodeId > 2) {
            std::cerr << "Node ID must be between 0 and 2" << std::endl;
            return 1;
        }

        std::filesystem::create_directories(dataDir);

        auto clusterConfig = setupClusterConfig();
        RaftNode node(nodeId, clusterConfig, dataDir, monitoringPort);
        
        std::cout << "Starting node " << nodeId << std::endl;
        node.start();

        std::string input;
        while (std::getline(std::cin, input)) {
            if (input == "quit") {
                break;
            }
        }

        node.stop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}