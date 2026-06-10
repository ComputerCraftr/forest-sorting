#ifndef FOREST_SORTING_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_ADAPTIVE_SORT_HPP

#include "forest.hpp"
#include "radix.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_internal {

struct IdWordRange {
    std::size_t begin;
    std::size_t end;
    std::size_t wordIndex;
};

inline std::vector<std::size_t>
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

inline void stableSortRangeByIdWord(std::vector<std::size_t> &order,
                                    std::vector<std::size_t> &scratch,
                                    const std::vector<Node> &nodes,
                                    std::size_t rangeBegin,
                                    std::size_t rangeEnd,
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

inline void sortRangeByIdWords(std::vector<std::size_t> &order,
                               std::vector<std::size_t> &scratch,
                               const std::vector<Node> &nodes,
                               std::size_t rangeBegin, std::size_t rangeEnd,
                               std::size_t wordIndex) {
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

inline void sortOrderByDepthAndId(std::vector<std::size_t> &order,
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

} // namespace forest_internal

#endif // FOREST_SORTING_ADAPTIVE_SORT_HPP
