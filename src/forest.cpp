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

constexpr std::size_t kIdWordCount = 2;
constexpr std::size_t kWordByteCount = 8;
constexpr std::size_t kRadixBits = 8;
constexpr std::size_t kRadixBucketCount = 256;

struct IdWordRange {
    std::size_t begin;
    std::size_t end;
    std::size_t wordIndex;
};

uint64_t idWordMsbFirst(UInt128 value, std::size_t wordIndex) noexcept {
    if (wordIndex == 0) {
        return static_cast<uint64_t>(value >> 64);
    }
    return static_cast<uint64_t>(value);
}

uint8_t wordByte(uint64_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

std::vector<std::size_t>
groupOrderByDepth(std::vector<std::size_t> &order,
                  std::vector<std::size_t> &scratch,
                  const std::vector<uint32_t> &depths) {
    std::vector<std::size_t> depthStarts(
        static_cast<std::size_t>(kMaxSortableDepth) + 2, 0);
    for (std::size_t nodeIndex : order) {
        ++depthStarts[static_cast<std::size_t>(depths[nodeIndex]) + 1];
    }

    for (std::size_t depth = 1; depth < depthStarts.size(); ++depth) {
        depthStarts[depth] += depthStarts[depth - 1];
    }

    std::vector<std::size_t> depthOffsets = depthStarts;
    for (std::size_t nodeIndex : order) {
        const std::size_t depth = static_cast<std::size_t>(depths[nodeIndex]);
        scratch[depthOffsets[depth]] = nodeIndex;
        ++depthOffsets[depth];
    }

    order.swap(scratch);
    return depthStarts;
}

void stableSortRangeByIdWord(std::vector<std::size_t> &order,
                             std::vector<std::size_t> &scratch,
                             const std::vector<Node> &nodes,
                             std::size_t rangeBegin, std::size_t rangeEnd,
                             std::size_t wordIndex) {
    for (std::size_t byteIndex = 0; byteIndex < kWordByteCount; ++byteIndex) {
        std::array<std::size_t, kRadixBucketCount> counts{};
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            const std::size_t nodeIndex = order[offset];
            ++counts[wordByte(idWordMsbFirst(nodes[nodeIndex].id, wordIndex),
                              byteIndex)];
        }

        std::size_t writeOffset = rangeBegin;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = writeOffset;
            writeOffset += bucketSize;
        }

        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            const std::size_t nodeIndex = order[offset];
            const uint8_t digit = wordByte(
                idWordMsbFirst(nodes[nodeIndex].id, wordIndex), byteIndex);
            scratch[counts[digit]] = nodeIndex;
            ++counts[digit];
        }

        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            order[offset] = scratch[offset];
        }
    }
}

void sortRangeByIdWords(std::vector<std::size_t> &order,
                        std::vector<std::size_t> &scratch,
                        const std::vector<Node> &nodes, std::size_t rangeBegin,
                        std::size_t rangeEnd, std::size_t wordIndex) {
    if (rangeEnd - rangeBegin <= 1) {
        return;
    }

    std::vector<IdWordRange> pending;
    pending.push_back(IdWordRange{rangeBegin, rangeEnd, wordIndex});

    while (!pending.empty()) {
        const IdWordRange currentRange = pending.back();
        pending.pop_back();
        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.wordIndex >= kIdWordCount) {
            continue;
        }

        stableSortRangeByIdWord(order, scratch, nodes, currentRange.begin,
                                currentRange.end, currentRange.wordIndex);

        const std::size_t nextWordIndex = currentRange.wordIndex + 1;
        if (nextWordIndex >= kIdWordCount) {
            continue;
        }

        std::size_t equalWordBegin = currentRange.begin;
        uint64_t previousWord = idWordMsbFirst(
            nodes[order[currentRange.begin]].id, currentRange.wordIndex);
        for (std::size_t offset = currentRange.begin + 1;
             offset < currentRange.end; ++offset) {
            const uint64_t currentWord =
                idWordMsbFirst(nodes[order[offset]].id, currentRange.wordIndex);
            if (currentWord != previousWord) {
                pending.push_back(
                    IdWordRange{equalWordBegin, offset, nextWordIndex});
                equalWordBegin = offset;
                previousWord = currentWord;
            }
        }

        pending.push_back(
            IdWordRange{equalWordBegin, currentRange.end, nextWordIndex});
    }
}

void sortOrderByDepthAndId(std::vector<std::size_t> &order,
                           std::vector<std::size_t> &scratch,
                           const std::vector<Node> &nodes,
                           const std::vector<uint32_t> &depths) {
    if (order.size() <= 1) {
        return;
    }

    const auto depthStarts = groupOrderByDepth(order, scratch, depths);
    for (std::size_t depth = 0; depth <= kMaxSortableDepth; ++depth) {
        sortRangeByIdWords(order, scratch, nodes, depthStarts[depth],
                           depthStarts[depth + 1], 0);
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

// buildParentIndex performs the only id-keyed parent lookup. Later working
// data stays indexed by original node position.
std::vector<std::size_t> buildParentIndex(const std::vector<Node> &nodes) {
    IdIndexMap idToIndex;
    idToIndex.reserve(nodes.size() * 2); // load factor headroom
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto inserted = idToIndex.emplace(nodes[i].id, i);
        if (!inserted.second) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const UInt128 parentId = nodes[i].parentId;
        if (parentId == 0) {
            continue;
        }
        const auto parentIt = idToIndex.find(parentId);
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

    // 3) Group by depth, then adaptively sort equal-depth ranges by id words.
    std::vector<std::size_t> scratch(nodeCount);
    sortOrderByDepthAndId(order, scratch, nodes, depth);

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
