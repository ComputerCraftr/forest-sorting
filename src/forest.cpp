#include "forest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::size_t kUInt128ByteCount = 16;
constexpr std::size_t kDepthByteCount = 2;
constexpr std::size_t kRadixBits = 8;
constexpr std::size_t kRadixBucketCount = 256;

uint8_t idByte(UInt128 value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

uint8_t depthByte(uint32_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

template <typename DigitForIndex>
void radixPass(std::vector<std::size_t> &order,
               std::vector<std::size_t> &scratch, DigitForIndex digitForIndex) {
    std::array<std::size_t, kRadixBucketCount> counts{};
    for (std::size_t nodeIndex : order) {
        ++counts[digitForIndex(nodeIndex)];
    }

    std::size_t offset = 0;
    for (std::size_t &count : counts) {
        const std::size_t bucketSize = count;
        count = offset;
        offset += bucketSize;
    }

    for (std::size_t nodeIndex : order) {
        const uint8_t digit = digitForIndex(nodeIndex);
        scratch[counts[digit]] = nodeIndex;
        ++counts[digit];
    }

    order.swap(scratch);
}

void radixSortByDepthAndId(std::vector<std::size_t> &order,
                           const std::vector<Node> &nodes,
                           const std::vector<uint32_t> &depths) {
    if (order.size() <= 1) {
        return;
    }

    std::vector<std::size_t> scratch(order.size());
    for (std::size_t byteIndex = 0; byteIndex < kUInt128ByteCount;
         ++byteIndex) {
        radixPass(order, scratch, [&](std::size_t nodeIndex) {
            return idByte(nodes[nodeIndex].id, byteIndex);
        });
    }
    for (std::size_t byteIndex = 0; byteIndex < kDepthByteCount; ++byteIndex) {
        radixPass(order, scratch, [&](std::size_t nodeIndex) {
            return depthByte(depths[nodeIndex], byteIndex);
        });
    }
}

} // namespace

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

using IdIndexMap = std::unordered_map<UInt128, std::size_t, UInt128Hash>;

// buildParentIndex maps each node index to its parent index; kNoParent for
// roots.
std::vector<std::size_t> buildParentIndex(const std::vector<Node> &nodes) {
    IdIndexMap idToIndex;
    idToIndex.reserve(nodes.size() * 2); // load factor headroom
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIndex.emplace(nodes[i].id, i);
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const UInt128 parentId = nodes[i].parentId;
        if (parentId == 0) {
            continue;
        }
        auto parentIt = idToIndex.find(parentId);
        if (parentIt != idToIndex.end()) {
            parent[i] = parentIt->second;
        }
    }

    return parent;
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

    // 3) Stable LSD radix sort by composite key: id first, then depth.
    radixSortByDepthAndId(order, nodes, depth);

    // 4) Materialize sorted nodes.
    std::vector<Node> sorted;
    sorted.reserve(nodeCount);
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }

    return sorted;
}

// verifySortedByDepthAndId checks the sorted order by recomputing depths.
bool verifySortedByDepthAndId(const std::vector<Node> &nodes) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount <= 1) {
        return true;
    }

    const auto parentIndex = buildParentIndex(nodes);
    std::vector<uint32_t> depth = computeDepths(nodes, parentIndex);

    // First element must be at depth 0 (a root).
    if (depth[0] != 0) {
        return false;
    }

    uint32_t prevDepth = depth[0];
    UInt128 prevId = nodes[0].id;

    for (std::size_t i = 1; i < nodeCount; ++i) {
        const uint32_t currentDepth = depth[i];
        const UInt128 currentId = nodes[i].id;

        // Depth must be non-decreasing. Within the same depth, id must be
        // non-decreasing.
        if (currentDepth < prevDepth ||
            (currentDepth == prevDepth && currentId < prevId)) {
            return false;
        }

        prevDepth = currentDepth;
        prevId = currentId;
    }

    return true;
}
