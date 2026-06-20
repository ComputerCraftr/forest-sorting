#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <random>
#include <vector>

// main builds a random forest, sorts it, prints, and verifies the order.
int main() {
    try {
        using forest_sorting::makeRandomId;
        using forest_sorting::Node;
        using forest_sorting::sortForestByDepthAndId;
        using forest_sorting::toHex;
        using forest_sorting::UInt128;
        using forest_sorting::verifySortedByDepthAndId;

        std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> indexPicker;

        std::vector<Node> nodes;
        constexpr std::size_t nodeCount = 30;
        nodes.reserve(nodeCount);

        // First node is the root with parent 0.
        nodes.push_back(Node{makeRandomId(rng), 0});

        for (std::size_t nodeIdx = 1; nodeIdx < nodeCount; ++nodeIdx) {
            indexPicker =
                std::uniform_int_distribution<std::size_t>(0, nodeIdx - 1);
            const auto parentIndex = indexPicker(rng);
            nodes.push_back(Node{makeRandomId(rng), nodes[parentIndex].id});
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
