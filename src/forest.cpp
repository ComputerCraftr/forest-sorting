#include "forest.hpp"
#include "adaptive_sort.hpp"
#include "parent_index.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// toHex converts a 128-bit value to a hex string with a 0x prefix.
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

uint64_t UInt128Hash::mix64(uint64_t input) noexcept {
    input += 0x9e3779b97f4a7c15ULL;
    input = (input ^ (input >> 30)) * 0xbf58476d1ce4e5b9ULL;
    input = (input ^ (input >> 27)) * 0x94d049bb133111ebULL;
    return input ^ (input >> 31);
}

std::size_t UInt128Hash::operator()(const UInt128 &value) const noexcept {
    const auto high = static_cast<uint64_t>(value >> 64);
    const auto low = static_cast<uint64_t>(value);

    // SplitMix64 on each half, then xor-combine.
    const uint64_t mixedHigh = mix64(high);
    const uint64_t mixedLow = mix64(low + 0x9e3779b97f4a7c15ULL);
    return mixedHigh ^ mixedLow;
}

// buildParentIndex performs the only id-keyed parent lookup. Later working
// data stays indexed by original node position.
std::vector<std::size_t> buildParentIndex(const std::vector<Node> &nodes) {
    return forest_internal::buildParentIndexControlByteFlatHash(nodes);
}

// computeDepths returns per-node depth in O(N); kNoParent/parentId == 0 is
// depth 0.
std::vector<uint32_t> computeDepths(const std::vector<Node> &nodes,
                                    const std::vector<std::size_t> &parent) {
    const std::size_t nodeCount = nodes.size();
    std::vector<uint32_t> depth(nodeCount, UINT32_MAX); // UINT32_MAX = unknown

    if (nodeCount == 0) {
        return depth;
    }

    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (depth[i] != UINT32_MAX) {
            continue; // already computed
        }

        std::size_t current = i;
        std::vector<std::size_t> stack;
        stack.reserve(32);

        // Walk up parent chain until we hit a root or a node with known depth.
        uint32_t baseDepth = UINT32_MAX;
        while (true) {
            if (depth[current] != UINT32_MAX) {
                // We found a node whose depth is already known.
                baseDepth = depth[current];
                break;
            }

            stack.push_back(current);

            if (nodes[current].parentId == 0 || parent[current] == kNoParent) {
                // Reached a root.
                baseDepth = UINT32_MAX; // special: next assigned becomes 0
                break;
            }

            current = parent[current];
        }

        // Walk back down and assign depths.
        while (!stack.empty()) {
            std::size_t nodeIndex = stack.back();
            stack.pop_back();

            if (baseDepth == UINT32_MAX) {
                baseDepth = 0; // first node above "root"
            } else {
                baseDepth += 1;
            }

            depth[nodeIndex] = baseDepth;
        }
    }

    return depth;
}

// sortForestByDepthAndId returns nodes sorted by depth then id.
std::vector<Node> sortForestByDepthAndId(const std::vector<Node> &nodes) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    // 1) Compute depth per node (O(N)).
    const auto parentIndex = buildParentIndex(nodes);
    std::vector<uint32_t> depth = computeDepths(nodes, parentIndex);

    // 2) Validate depth and make an index vector 0..N-1.
    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (depth[i] > kMaxSortableDepth) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
    }

    // 3) Group by depth, then adaptively sort equal-depth ranges by id words.
    std::vector<std::size_t> scratch(nodeCount);
    forest_internal::sortOrderByDepthAndId(order, scratch, nodes, depth);

    // 4) Materialize sorted nodes.
    std::vector<Node> sorted;
    sorted.reserve(nodeCount);
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }

    return sorted;
}

// verifySortedByDepthAndId checks the sorted order in one forward pass after
// mapping ids. Missing parents are root-equivalent, matching computeDepths.
bool verifySortedByDepthAndId(const std::vector<Node> &nodes) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount <= 1) {
        return true;
    }

    std::vector<std::size_t> parentIndex;
    try {
        parentIndex = buildParentIndex(nodes);
    } catch (const std::runtime_error &) {
        return false;
    }

    std::vector<uint32_t> verifiedDepth(nodeCount, UINT32_MAX);

    uint32_t prevDepth = 0;
    UInt128 prevId = 0;

    for (std::size_t i = 0; i < nodeCount; ++i) {
        const UInt128 currentId = nodes[i].id;

        uint32_t currentDepth = 0;
        if (nodes[i].parentId != 0 && parentIndex[i] != kNoParent) {
            const std::size_t parentNodeIndex = parentIndex[i];
            if (verifiedDepth[parentNodeIndex] == UINT32_MAX) {
                return false;
            }

            currentDepth = verifiedDepth[parentNodeIndex] + 1;
            if (currentDepth > kMaxSortableDepth) {
                return false;
            }
        }

        if (i > 0 && (currentDepth < prevDepth ||
                      (currentDepth == prevDepth && currentId < prevId))) {
            return false;
        }

        verifiedDepth[i] = currentDepth;
        prevDepth = currentDepth;
        prevId = currentId;
    }

    return true;
}
