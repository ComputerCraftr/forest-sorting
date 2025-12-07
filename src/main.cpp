#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

using UInt128 = unsigned __int128;

struct Node {
    UInt128 id;
    UInt128 parentId;
};

std::string toHex(UInt128 value) {
    if (value == 0) {
        return "0x0";
    }

    std::ostringstream oss;
    oss << "0x" << std::hex;

    const auto high = static_cast<uint64_t>(value >> 64);
    const auto low = static_cast<uint64_t>(value);

    if (high != 0) {
        oss << high << std::setw(16) << std::setfill('0') << low;
    } else {
        oss << low;
    }

    return oss.str();
}

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
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> indexPicker;

    auto makeId = [&rng]() -> UInt128 {
        UInt128 high = static_cast<UInt128>(rng());
        UInt128 low = static_cast<UInt128>(rng());
        return (high << 64) | low;
    };

    std::vector<Node> nodes;
    constexpr size_t nodeCount = 30;
    nodes.reserve(nodeCount);

    // First node is the root with parent 0.
    nodes.push_back(Node{makeId(), 0});

    for (size_t i = 1; i < nodeCount; ++i) {
        indexPicker = std::uniform_int_distribution<size_t>(0, i - 1);
        const auto parentIndex = indexPicker(rng);
        nodes.push_back(Node{makeId(), nodes[parentIndex].id});
    }

    const auto sorted = sortForest(nodes);

    std::cout << "Deterministically sorted forest nodes:\n";
    for (const auto &node : sorted) {
        std::cout << "node " << toHex(node.id) << " (parent "
                  << toHex(node.parentId) << ")\n";
    }

    return 0;
}
