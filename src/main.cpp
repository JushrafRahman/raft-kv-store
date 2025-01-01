#include "node/node.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <node-id>" << std::endl;
        return 1;
    }

    int nodeId = std::stoi(argv[1]);
    Node node(nodeId);
    
    std::cout << "Starting node " << nodeId << std::endl;
    node.start();
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}