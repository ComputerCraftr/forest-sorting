#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

struct Node {
    uint64_t id;
    uint64_t parentId;
};

std::vector<Node> sortForest(const std::vector<Node> &nodes) {
    std::vector<Node> sorted = nodes;
    std::sort(sorted.begin(), sorted.end(), [](const Node &a, const Node &b) {
        if (a.parentId == b.parentId) {
            return a.id < b.id;
        }
        return a.parentId < b.parentId;
    });
    return sorted;
}

int main() {
    std::vector<Node> nodes = {
        {3, 1}, {2, 0}, {5, 3}, {4, 1}, {1, 0},
    };

    const auto sorted = sortForest(nodes);

    std::cout << "Deterministically sorted forest nodes:\n";
    for (const auto &node : sorted) {
        std::cout << "node " << node.id << " (parent " << node.parentId
                  << ")\n";
    }

    return 0;
}
