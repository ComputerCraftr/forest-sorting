#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/radix.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

// -----------------------------------------------------------------------------
// Adaptive sort tuning
// -----------------------------------------------------------------------------

// Hard cap for dense depth histograms. Prevents sparse high-depth outliers from
// allocating depthStarts[observedMaxDepth + 2] when observed depth is large.
inline constexpr std::size_t max_dense_depth_buckets = std::size_t{1} << 20;

// Dense depth grouping is used only when the histogram bucket count is small
// relative to node count, so a sparse depth outlier does not force a wide
// array.
inline constexpr std::size_t dense_depth_bucket_multiplier = 4;

// Equal-depth ID ranges at or below this size use stable insertion sort instead
// of allocating/counting radix buckets.
inline constexpr std::size_t small_id_range_sort_threshold = 32;

// -----------------------------------------------------------------------------
// Internal range/buffer records
// -----------------------------------------------------------------------------

struct DepthRange {
    uint32_t depth;
    std::size_t begin;
    std::size_t end;
};

struct IdWordRange {
    std::size_t begin;
    std::size_t end;
    std::size_t chunkIndex;
};

struct ChunkedIndex {
    uint64_t chunk;
    std::size_t index;
};

// -----------------------------------------------------------------------------
// Depth grouping policy
// -----------------------------------------------------------------------------

inline bool shouldUseDenseDepthGrouping(std::size_t nodeCount,
                                        uint32_t observedMaxDepth) noexcept {
    const std::size_t bucketCount =
        static_cast<std::size_t>(observedMaxDepth) + std::size_t{2};
    if (bucketCount > max_dense_depth_buckets) {
        return false;
    }
    if (nodeCount >= max_dense_depth_buckets) {
        return true;
    }
    return bucketCount <= nodeCount * dense_depth_bucket_multiplier;
}

inline std::vector<DepthRange> groupOrderByDepthDense(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    const std::vector<uint32_t> &depths, uint32_t observedMaxDepth) {
    std::vector<std::size_t> depthStarts(
        static_cast<std::size_t>(observedMaxDepth) + 2, 0);
    for (std::size_t nodeIndex : order) {
        ++depthStarts[static_cast<std::size_t>(depths[nodeIndex]) + 1];
    }

    for (std::size_t depthIdx = 1; depthIdx < depthStarts.size(); ++depthIdx) {
        depthStarts[depthIdx] += depthStarts[depthIdx - 1];
    }

    std::vector<std::size_t> depthOffsets = depthStarts;
    for (std::size_t nodeIndex : order) {
        const std::size_t depthValue =
            static_cast<std::size_t>(depths[nodeIndex]);
        scratch[depthOffsets[depthValue]] = nodeIndex;
        ++depthOffsets[depthValue];
    }

    order.swap(scratch);

    std::vector<DepthRange> ranges;
    ranges.reserve(128);
    for (std::size_t depthIdx = 0; depthIdx <= observedMaxDepth; ++depthIdx) {
        const std::size_t rangeBegin = depthStarts[depthIdx];
        const std::size_t rangeEnd = depthStarts[depthIdx + 1];
        if (rangeBegin != rangeEnd) {
            ranges.push_back(
                {static_cast<uint32_t>(depthIdx), rangeBegin, rangeEnd});
        }
    }
    return ranges;
}

template <std::size_t DepthPrefixBytes>
inline std::vector<DepthRange>
groupOrderByDepthMsd(std::vector<std::size_t> &order,
                     std::vector<std::size_t> &scratch,
                     const std::vector<uint32_t> &depths) {
    std::vector<DepthRange> ranges;
    ranges.reserve(128);

    constexpr std::size_t firstDepthByte = 4 - DepthPrefixBytes;

    auto digitForIndex = [&](std::size_t nodeIdx, std::size_t digitIndex) {
        return depthByteMsbFirst(depths[nodeIdx], digitIndex);
    };

    auto rangeDone = [&](std::size_t rangeBegin, std::size_t rangeEnd) {
        if (rangeEnd > rangeBegin) {
            ranges.push_back({depths[order[rangeBegin]], rangeBegin, rangeEnd});
        }
    };

    radixMsdPartitionRanges(order, scratch, 0, order.size(), firstDepthByte, 4,
                            digitForIndex, rangeDone);

    return ranges;
}

// -----------------------------------------------------------------------------
// ID ordering helpers
// -----------------------------------------------------------------------------

template <typename Id, typename IdTraits>
inline int compareIdsMsbFirst(const Id &lhs, const Id &rhs,
                              const IdTraits &traits) noexcept {
    constexpr std::size_t chunkCount =
        (IdTraits::id_byte_count + chunk_byte_count - 1) / chunk_byte_count;
    for (std::size_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {
        const uint64_t lVal = chunkMsbFirst(lhs, chunkIdx, traits);
        const uint64_t rVal = chunkMsbFirst(rhs, chunkIdx, traits);
        if (lVal < rVal) {
            return -1;
        }
        if (lVal > rVal) {
            return 1;
        }
    }
    return 0;
}

template <typename Nodes, typename IdTraits>
void stableSortRangeSmall(std::vector<std::size_t> &order, const Nodes &nodes,
                          const IdTraits &traits, std::size_t rangeBegin,
                          std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);
        std::size_t innerIdx = rangeIdx;
        while (innerIdx > rangeBegin &&
               compareIdsMsbFirst(traits.id(nodes[order[innerIdx - 1]]),
                                  idValue, traits) > 0) {
            order[innerIdx] = order[innerIdx - 1];
            --innerIdx;
        }
        order[innerIdx] = nodeIndex;
    }
}

template <typename Nodes, typename IdTraits>
void stableSortRangeByIdWord(std::vector<std::size_t> &order,
                             const Nodes &nodes, const IdTraits &traits,
                             std::size_t rangeBegin, std::size_t rangeEnd,
                             std::size_t chunkIndex, ChunkedIndex *current,
                             ChunkedIndex *next) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
        const std::size_t index = order[rangeBegin + nodeIdx];
        current[nodeIdx] = {
            chunkMsbFirst(traits.id(nodes[index]), chunkIndex, traits), index};
    }

    for (std::size_t byteIndex = 0; byteIndex < chunk_byte_count; ++byteIndex) {
        std::array<std::size_t, radix_bucket_count> counts{};
        for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
            ++counts[wordByte(current[nodeIdx].chunk, byteIndex)];
        }

        std::size_t writeOffset = 0;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = writeOffset;
            writeOffset += bucketSize;
        }

        for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
            const auto entry = current[nodeIdx];
            const uint8_t digit = wordByte(entry.chunk, byteIndex);
            next[counts[digit]++] = entry;
        }

        std::swap(current, next);
    }

    for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
        order[rangeBegin + nodeIdx] = current[nodeIdx].index;
    }
}

template <typename Nodes, typename IdTraits>
void sortRangeByIdWords(std::vector<std::size_t> &order, const Nodes &nodes,
                        const IdTraits &traits, std::size_t rangeBegin,
                        std::size_t rangeEnd, std::size_t chunkIndex,
                        std::vector<IdWordRange> &pending,
                        ChunkedIndex *chunkBufferCurrent,
                        ChunkedIndex *chunkBufferNext) {
    if (rangeEnd - rangeBegin <= 1) {
        return;
    }

    if (rangeEnd - rangeBegin <= small_id_range_sort_threshold) {
        stableSortRangeSmall(order, nodes, traits, rangeBegin, rangeEnd);
        return;
    }

    pending.clear();
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

        if (currentRange.end - currentRange.begin <=
            small_id_range_sort_threshold) {
            stableSortRangeSmall(order, nodes, traits, currentRange.begin,
                                 currentRange.end);
        } else {
            stableSortRangeByIdWord(order, nodes, traits, currentRange.begin,
                                    currentRange.end, currentRange.chunkIndex,
                                    chunkBufferCurrent, chunkBufferNext);
        }

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

// -----------------------------------------------------------------------------
// Public detail entry point
// -----------------------------------------------------------------------------

template <std::size_t DepthPrefixBytes, typename Nodes, typename IdTraits>
void sortOrderByDepthAndId(std::vector<std::size_t> &order,
                           std::vector<std::size_t> &scratch,
                           const Nodes &nodes, const IdTraits &traits,
                           const std::vector<uint32_t> &depths,
                           uint32_t observedMaxDepth) {
    if (order.size() <= 1) {
        return;
    }

    // observedMaxDepth is derived from depths. Small dense ranges use a compact
    // histogram; sparse high-depth ranges avoid huge allocations and use MSD
    // depth grouping instead.
    std::vector<DepthRange> depthRanges;
    if (shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        depthRanges =
            groupOrderByDepthDense(order, scratch, depths, observedMaxDepth);
    } else {
        depthRanges =
            groupOrderByDepthMsd<DepthPrefixBytes>(order, scratch, depths);
    }

    std::vector<IdWordRange> pending;
    pending.reserve(128);

    std::size_t maxRadixRangeSize = 0;
    for (const DepthRange &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
        if (rangeSize > small_id_range_sort_threshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    std::unique_ptr<ChunkedIndex[]> chunkBufferCurrent;
    std::unique_ptr<ChunkedIndex[]> chunkBufferNext;
    if (maxRadixRangeSize > 0) {
        chunkBufferCurrent = std::unique_ptr<ChunkedIndex[]>(
            new ChunkedIndex[maxRadixRangeSize]);
        chunkBufferNext = std::unique_ptr<ChunkedIndex[]>(
            new ChunkedIndex[maxRadixRangeSize]);
    }

    for (const DepthRange &range : depthRanges) {
#ifndef NDEBUG
        for (std::size_t offset = range.begin; offset < range.end; ++offset) {
            assert(depths[order[offset]] == range.depth);
        }
#endif
        sortRangeByIdWords(order, nodes, traits, range.begin, range.end, 0,
                           pending, chunkBufferCurrent.get(),
                           chunkBufferNext.get());
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
