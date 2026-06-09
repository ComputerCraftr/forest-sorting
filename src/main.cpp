#include "forest.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <random>
#include <vector>

// main builds a random forest, sorts it, prints, and verifies the order.
int main() {
    try {
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> indexPicker;

        auto makeId = [&rng]() -> UInt128 {
            UInt128 high = static_cast<UInt128>(rng());
            UInt128 low = static_cast<UInt128>(rng());
            return (high << 64) | low;
        };

        std::vector<Node> nodes;
        constexpr std::size_t nodeCount = 30;
        nodes.reserve(nodeCount);

        // First node is the root with parent 0.
        nodes.push_back(Node{makeId(), 0});

        for (std::size_t i = 1; i < nodeCount; ++i) {
            indexPicker = std::uniform_int_distribution<std::size_t>(0, i - 1);
            const auto parentIndex = indexPicker(rng);
            nodes.push_back(Node{makeId(), nodes[parentIndex].id});
        }

        auto sorted = sortForestByDepthAndId(nodes);

        std::cout
            << "Deterministically sorted forest nodes (by depth, then id):\n";
        for (const auto &node : sorted) {
            std::cout << "node " << toHex(node.id) << " (parent "
                      << toHex(node.parentId) << ")\n";
        }

        // Verify that the sorted vector is indeed in canonical order.
        const bool sortedOk = verifySortedByDepthAndId(sorted);
        std::cout << "\nVerification of sorted vector: "
                  << (sortedOk ? "OK" : "FAILED") << "\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting failed: unknown exception\n";
        return 1;
    }
}
