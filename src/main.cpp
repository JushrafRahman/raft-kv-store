#include "raft/raft.hpp"
#include <iostream>
#include <vector>

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
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <node-id>" << std::endl;
        return 1;
    }

    int nodeId = std::stoi(argv[1]);
    if (nodeId < 0 || nodeId > 2) {
        std::cerr << "Node ID must be between 0 and 2" << std::endl;
        return 1;
    }

    try {
        auto clusterConfig = setupClusterConfig();
        RaftNode node(nodeId, clusterConfig);
        
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