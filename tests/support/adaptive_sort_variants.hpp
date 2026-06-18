#ifndef FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
#define FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "sort_baselines.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <vector>

namespace forest_sorting::test_support {

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallBinary(std::vector<std::size_t> &order,
                                const Nodes &nodes, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);

        const std::size_t previousIndex = order[rangeIdx - 1];
        if (detail::compareIdsMsbFirst(traits.id(nodes[previousIndex]), idValue,
                                       traits) <= 0) {
            continue;
        }

        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx;
        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const bool insertAfterMiddle =
                detail::compareIdsMsbFirst(traits.id(nodes[order[middle]]),
                                           idValue, traits) <= 0;
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            order[moveIdx] = order[moveIdx - 1];
        }
        order[searchBegin] = nodeIndex;
    }
}

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallExponential(std::vector<std::size_t> &order,
                                     const Nodes &nodes, const IdTraits &traits,
                                     std::size_t rangeBegin,
                                     std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);

        const std::size_t previousIndex = order[rangeIdx - 1];
        if (detail::compareIdsMsbFirst(traits.id(nodes[previousIndex]), idValue,
                                       traits) <= 0) {
            continue;
        }

        const std::size_t sortedPrefixSize = rangeIdx - rangeBegin;
        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx - 1;
        for (std::size_t bound = 2; bound <= sortedPrefixSize; bound <<= 1) {
            const std::size_t probe = rangeIdx - bound;
            if (detail::compareIdsMsbFirst(traits.id(nodes[order[probe]]),
                                           idValue, traits) <= 0) {
                searchBegin = probe + 1;
                break;
            }
            searchEnd = probe;
        }

        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const bool insertAfterMiddle =
                detail::compareIdsMsbFirst(traits.id(nodes[order[middle]]),
                                           idValue, traits) <= 0;
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            order[moveIdx] = order[moveIdx - 1];
        }
        order[searchBegin] = nodeIndex;
    }
}

template <std::size_t ChunkBytes = detail::chunk_byte_count,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter>
void sortRangeByIdChunksWithSmallSorterSupport(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    std::vector<detail::IdChunkRange> &pending,
    detail::ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
    detail::ChunkedIndex<ChunkBytes> *chunkBufferNext,
    detail::BitmaskTouchedCountScratch &touchedScratch,
    SmallRangeSorter smallRangeSorter) {
    const UInt128NodeTraits traits{};
    const std::size_t initialRangeSize = rangeEnd - rangeBegin;
    if (initialRangeSize <= 1) {
        return;
    }

    if (initialRangeSize <= SmallThreshold) {
        smallRangeSorter(order, nodes, traits, rangeBegin, rangeEnd);
        return;
    }

    pending.clear();
    pending.push_back(detail::IdChunkRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (UInt128NodeTraits::id_byte_count + ChunkBytes - 1) / ChunkBytes;

    while (!pending.empty()) {
        const detail::IdChunkRange currentRange = pending.back();
        pending.pop_back();
        const std::size_t currentBegin = currentRange.begin;
        const std::size_t currentEnd = currentRange.end;
        const std::size_t currentChunkIndex = currentRange.chunkIndex;
        const std::size_t rangeSize = currentEnd - currentBegin;
        if (rangeSize <= 1 || currentChunkIndex >= chunkCount) {
            continue;
        }

        if (rangeSize <= SmallThreshold) {
            smallRangeSorter(order, nodes, traits, currentBegin, currentEnd);
            continue;
        }

        detail::ChunkedIndex<ChunkBytes> *sortedChunks =
            detail::dispatchLsdChunkSort<ChunkBytes, CountPolicy>(
                order, nodes, traits, currentBegin, currentEnd,
                currentChunkIndex, chunkBufferCurrent, chunkBufferNext,
                touchedScratch);

        const std::size_t nextChunkIndex = currentChunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalChunkBegin = currentBegin;
        detail::ChunkValueType<ChunkBytes> previousChunk =
            sortedChunks[0].chunk;
        for (std::size_t offset = 1; offset < rangeSize; ++offset) {
            const detail::ChunkValueType<ChunkBytes> currentChunk =
                sortedChunks[offset].chunk;
            if (currentChunk != previousChunk) {
                const std::size_t splitOffset = currentBegin + offset;
                pending.push_back(detail::IdChunkRange{
                    equalChunkBegin, splitOffset, nextChunkIndex});
                equalChunkBegin = splitOffset;
                previousChunk = currentChunk;
            }
        }

        pending.push_back(
            detail::IdChunkRange{equalChunkBegin, currentEnd, nextChunkIndex});
    }
}

template <std::size_t DepthPrefixBytes, typename RangeSorter>
inline std::vector<Node> sortForestByAdaptiveRangeSorterWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    bool allowDenseDepthGrouping, RangeSorter rangeSorter) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    const uint32_t observedMaxDepth =
        validateDepthLimit<DepthPrefixBytes>(depths);

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    std::vector<detail::DepthRange> depthRanges;
    depthRanges.reserve(detail::initial_range_stack_capacity);
    std::vector<std::size_t> depthStarts;
    std::vector<std::size_t> depthOffsets;
    if (allowDenseDepthGrouping &&
        detail::shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        detail::groupOrderByDepthDense(order, scratch, depths, observedMaxDepth,
                                       depthRanges, depthStarts, depthOffsets);
    } else {
        detail::groupOrderByDepthMsd<DepthPrefixBytes>(order, scratch, depths,
                                                       depthRanges);
    }

    rangeSorter(order, scratch, nodes, depthRanges);

    return materializeOrder(nodes, order);
}

template <std::size_t ChunkBytes = detail::chunk_byte_count,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter>
inline void
sortDepthRangesByChunkMsd(std::vector<std::size_t> &order,
                          const std::vector<Node> &nodes,
                          const std::vector<detail::DepthRange> &depthRanges,
                          SmallRangeSorter smallRangeSorter) {
    std::vector<detail::IdChunkRange> pending;
    pending.reserve(detail::initial_range_stack_capacity);
    detail::BitmaskTouchedCountScratch touchedScratch;

    std::size_t maxRadixRangeSize = 0;
    for (const detail::DepthRange &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize > SmallThreshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]> chunkBufferCurrent;
    std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]> chunkBufferNext;
    if (maxRadixRangeSize > 0) {
        chunkBufferCurrent =
            std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]>(
                new detail::ChunkedIndex<ChunkBytes>[maxRadixRangeSize]);
        chunkBufferNext = std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]>(
            new detail::ChunkedIndex<ChunkBytes>[maxRadixRangeSize]);
    }

    for (const detail::DepthRange &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        sortRangeByIdChunksWithSmallSorterSupport<ChunkBytes, CountPolicy,
                                                  SmallThreshold>(
            order, nodes, rangeBegin, rangeEnd, 0, pending,
            chunkBufferCurrent.get(), chunkBufferNext.get(), touchedScratch,
            smallRangeSorter);
    }
}

template <std::size_t ChunkBytes, typename CountPolicy,
          std::size_t SmallThreshold, typename SmallRangeSorter>
inline void sortDepthRangesWithTunedParams(
    std::vector<std::size_t> &order, std::vector<std::size_t> &unusedScratch,
    const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange> &depthRanges) {
    (void)unusedScratch;
    SmallRangeSorter smallRangeSorter;
    sortDepthRangesByChunkMsd<ChunkBytes, CountPolicy, SmallThreshold>(
        order, nodes, depthRanges, smallRangeSorter);
}

struct LinearSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        detail::stableSortRangeSmallLinear(order, nodes, traits, begin, end);
    }
};

struct BinarySmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBinary(order, nodes, traits, begin, end);
    }
};

struct ExponentialSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallExponential(order, nodes, traits, begin, end);
    }
};

template <std::size_t DepthPrefixBytes, std::size_t ChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter>
inline std::vector<Node>
sortForestByAdaptiveChunkWithParent(const std::vector<Node> &nodes,
                                    const std::vector<std::size_t> &parentIndex,
                                    bool allowDenseDepthGrouping) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const std::vector<Node> &sortNodes,
                          const std::vector<detail::DepthRange> &depthRanges) {
        sortDepthRangesWithTunedParams<ChunkBytes, CountPolicy, SmallThreshold,
                                       SmallRangeSorter>(
            order, scratch, sortNodes, depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<DepthPrefixBytes>(
        nodes, parentIndex, allowDenseDepthGrouping, rangeSorter);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkNoDenseWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, false);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkFullClearWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 4, detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U8ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 1>(nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U64ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 8>(nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByAdaptiveDepth2U64ChunkBinarySmallWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 8, detail::FullClearCounts, detail::small_id_range_sort_threshold,
        BinarySmallSorter>(nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth4U32ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<4, 4>(nodes, parentIndex, true);
}

template <typename SmallRangeSorter, std::size_t SmallThreshold>
inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkTailTunedWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const std::vector<Node> &sortNodes,
                          const std::vector<detail::DepthRange> &depthRanges) {
        sortDepthRangesWithTunedParams<4, detail::FullClearCounts,
                                       SmallThreshold, SmallRangeSorter>(
            order, scratch, sortNodes, depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

template <std::size_t TouchedCountMaxRangeSize>
inline std::vector<Node>
sortForestByAdaptiveDepth2U32ChunkTouchedBitmaskWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const std::vector<Node> &sortNodes,
                          const std::vector<detail::DepthRange> &depthRanges) {
        sortDepthRangesWithTunedParams<
            4, detail::BitmaskTouchedCountsUpTo<TouchedCountMaxRangeSize>,
            detail::small_id_range_sort_threshold, LinearSmallSorter>(
            order, scratch, sortNodes, depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
