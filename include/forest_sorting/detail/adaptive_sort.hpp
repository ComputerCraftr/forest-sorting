#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/radix.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::detail {

struct IdWordRange {
    std::size_t begin;
    std::size_t end;
    std::size_t chunkIndex;
};

inline std::vector<std::size_t>
groupOrderByDepth(std::vector<std::size_t> &order,
                  std::vector<std::size_t> &scratch,
                  const std::vector<uint32_t> &depths, uint32_t maxDepth) {
    std::vector<std::size_t> depthStarts(static_cast<std::size_t>(maxDepth) + 2,
                                         0);
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

template <typename Nodes, typename IdTraits>
void stableSortRangeByIdWord(std::vector<std::size_t> &order,
                             std::vector<std::size_t> &scratch,
                             const Nodes &nodes, const IdTraits &traits,
                             std::size_t rangeBegin, std::size_t rangeEnd,
                             std::size_t chunkIndex) {
    for (std::size_t byteIndex = 0; byteIndex < chunk_byte_count; ++byteIndex) {
        std::array<std::size_t, radix_bucket_count> counts{};
        std::size_t offset = rangeBegin;
        for (; offset + 1 < rangeEnd; offset += 2) {
            const uint64_t chunk0 = chunkMsbFirst(
                traits.id(nodes[order[offset]]), chunkIndex, traits);
            const uint64_t chunk1 = chunkMsbFirst(
                traits.id(nodes[order[offset + 1]]), chunkIndex, traits);

            ++counts[wordByte(chunk0, byteIndex)];
            ++counts[wordByte(chunk1, byteIndex)];
        }
        if (offset < rangeEnd) {
            ++counts[wordByte(chunkMsbFirst(traits.id(nodes[order[offset]]),
                                            chunkIndex, traits),
                              byteIndex)];
        }

        std::size_t writeOffset = rangeBegin;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = writeOffset;
            writeOffset += bucketSize;
        }

        offset = rangeBegin;
        for (; offset + 1 < rangeEnd; offset += 2) {
            const std::size_t nodeIndex0 = order[offset];
            const std::size_t nodeIndex1 = order[offset + 1];

            const uint64_t chunk0 =
                chunkMsbFirst(traits.id(nodes[nodeIndex0]), chunkIndex, traits);
            const uint64_t chunk1 =
                chunkMsbFirst(traits.id(nodes[nodeIndex1]), chunkIndex, traits);

            const uint8_t digit0 = wordByte(chunk0, byteIndex);
            const uint8_t digit1 = wordByte(chunk1, byteIndex);

            scratch[counts[digit0]++] = nodeIndex0;
            scratch[counts[digit1]++] = nodeIndex1;
        }
        if (offset < rangeEnd) {
            const std::size_t nodeIndex = order[offset];
            const uint8_t digit = wordByte(
                chunkMsbFirst(traits.id(nodes[nodeIndex]), chunkIndex, traits),
                byteIndex);
            scratch[counts[digit]++] = nodeIndex;
        }

        for (std::size_t nodeIdx = rangeBegin; nodeIdx < rangeEnd; ++nodeIdx) {
            order[nodeIdx] = scratch[nodeIdx];
        }
    }
}

template <typename Nodes, typename IdTraits>
void sortRangeByIdWords(std::vector<std::size_t> &order,
                        std::vector<std::size_t> &scratch, const Nodes &nodes,
                        const IdTraits &traits, std::size_t rangeBegin,
                        std::size_t rangeEnd, std::size_t chunkIndex) {
    if (rangeEnd - rangeBegin <= 1) {
        return;
    }

    std::vector<IdWordRange> pending;
    pending.push_back(IdWordRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (IdTraits::id_byte_count + chunk_byte_count - 1) / chunk_byte_count;

    while (!pending.empty()) {
        const IdWordRange currentRange = pending.back();
        pending.pop_back();
        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.chunkIndex >= chunkCount) {
            continue;
        }

        stableSortRangeByIdWord(order, scratch, nodes, traits,
                                currentRange.begin, currentRange.end,
                                currentRange.chunkIndex);

        const std::size_t nextChunkIndex = currentRange.chunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalWordBegin = currentRange.begin;
        uint64_t previousChunk =
            chunkMsbFirst(traits.id(nodes[order[currentRange.begin]]),
                          currentRange.chunkIndex, traits);
        for (std::size_t offset = currentRange.begin + 1;
             offset < currentRange.end; ++offset) {
            const uint64_t currentChunk =
                chunkMsbFirst(traits.id(nodes[order[offset]]),
                              currentRange.chunkIndex, traits);
            if (currentChunk != previousChunk) {
                pending.push_back(
                    IdWordRange{equalWordBegin, offset, nextChunkIndex});
                equalWordBegin = offset;
                previousChunk = currentChunk;
            }
        }

        pending.push_back(
            IdWordRange{equalWordBegin, currentRange.end, nextChunkIndex});
    }
}

template <typename Nodes, typename IdTraits>
void sortOrderByDepthAndId(std::vector<std::size_t> &order,
                           std::vector<std::size_t> &scratch,
                           const Nodes &nodes, const IdTraits &traits,
                           const std::vector<uint32_t> &depths,
                           uint32_t maxDepth) {
    if (order.size() <= 1) {
        return;
    }

    const auto depthStarts =
        groupOrderByDepth(order, scratch, depths, maxDepth);
    for (std::size_t depth = 0; depth <= maxDepth; ++depth) {
        sortRangeByIdWords(order, scratch, nodes, traits, depthStarts[depth],
                           depthStarts[depth + 1], 0);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
