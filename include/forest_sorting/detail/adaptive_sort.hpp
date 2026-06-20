#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

// -----------------------------------------------------------------------------
// Adaptive sort tuning
// -----------------------------------------------------------------------------

// Hard resource cap for dense depth histograms. Structurally valid depths are
// bounded by node count, but very large valid forests still use depth MSD to
// avoid oversized histogram allocation and scanning.
inline constexpr std::size_t max_dense_depth_buckets = std::size_t{1} << 20;

// Equal-depth ID ranges at or below this size use stable insertion sort instead
// of allocating/counting radix buckets.
inline constexpr std::size_t small_id_range_sort_threshold = 32;

// Production equal-depth ID sorting uses MSB-first 4-byte chunks. This is
// independent of the public depth-prefix width.
inline constexpr std::size_t production_id_chunk_bytes = 4;

// The production u32 chunk sorter uses bitmask touched counters only for medium
// ranges. Larger ranges keep the full-clear counter path.
inline constexpr std::size_t production_touched_count_max_range_size = 512;

// Primitives for counting and prefixing radix buckets are shared via
// radix_counts.hpp

// -----------------------------------------------------------------------------
// Internal range/buffer records
// -----------------------------------------------------------------------------

template <typename Depth> struct DepthRange {
    Depth depth;
    std::size_t begin;
    std::size_t end;
};

struct IdChunkRange {
    std::size_t begin;
    std::size_t end;
    std::size_t chunkIndex;
};

template <std::size_t ChunkBytes> struct ChunkedIndex {
    ChunkValueType<ChunkBytes> chunk;
    std::size_t index;
};

// -----------------------------------------------------------------------------
// Depth grouping policy
// -----------------------------------------------------------------------------

template <typename Depth>
inline bool shouldUseDenseDepthGrouping(std::size_t nodeCount,
                                        Depth observedMaxDepth) noexcept {
    if (nodeCount == 0 ||
        static_cast<std::size_t>(observedMaxDepth) >= nodeCount) {
        return false;
    }
    return static_cast<std::size_t>(observedMaxDepth) <=
           max_dense_depth_buckets - 2;
}

template <typename Depth>
inline void groupOrderByDepthDense(std::vector<std::size_t> &order,
                                   std::vector<std::size_t> &scratch,
                                   const std::vector<Depth> &depths,
                                   Depth observedMaxDepth,
                                   std::vector<DepthRange<Depth>> &ranges,
                                   std::vector<std::size_t> &depthStarts,
                                   std::vector<std::size_t> &depthOffsets) {
    depthStarts.assign(static_cast<std::size_t>(observedMaxDepth) + 2, 0);
    for (std::size_t nodeIndex : order) {
        ++depthStarts[static_cast<std::size_t>(depths[nodeIndex]) + 1];
    }

    for (std::size_t depthIdx = 1; depthIdx < depthStarts.size(); ++depthIdx) {
        depthStarts[depthIdx] += depthStarts[depthIdx - 1];
    }

    depthOffsets = depthStarts;
    for (std::size_t nodeIndex : order) {
        const std::size_t depthValue =
            static_cast<std::size_t>(depths[nodeIndex]);
        scratch[depthOffsets[depthValue]] = nodeIndex;
        ++depthOffsets[depthValue];
    }

    order.swap(scratch);

    ranges.clear();
    ranges.reserve(initial_range_stack_capacity);
    for (std::size_t depthIdx = 0;
         depthIdx <= static_cast<std::size_t>(observedMaxDepth); ++depthIdx) {
        const std::size_t rangeBegin = depthStarts[depthIdx];
        const std::size_t rangeEnd = depthStarts[depthIdx + 1];
        if (rangeBegin != rangeEnd) {
            ranges.push_back(
                {static_cast<Depth>(depthIdx), rangeBegin, rangeEnd});
        }
    }
}

template <std::size_t DepthPrefixBytes, typename CountPolicy = FullClearCounts,
          typename Depth>
inline void groupOrderByDepthMsd(std::vector<std::size_t> &order,
                                 std::vector<std::size_t> &scratch,
                                 const std::vector<Depth> &depths,
                                 std::vector<DepthRange<Depth>> &ranges) {
    ranges.clear();
    ranges.reserve(initial_range_stack_capacity);

    auto digitForIndex = [&](std::size_t nodeIdx, std::size_t digitIndex) {
        return depthByteMsbFirst<DepthPrefixBytes>(depths[nodeIdx], digitIndex);
    };

    auto rangeDone = [&](std::size_t rangeBegin, std::size_t rangeEnd) {
        if (rangeEnd > rangeBegin) {
            ranges.push_back({depths[order[rangeBegin]], rangeBegin, rangeEnd});
        }
    };

    radixMsdPartitionRanges<CountPolicy>(order, scratch, 0, order.size(), 0,
                                         DepthPrefixBytes, digitForIndex,
                                         rangeDone);
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
void stableSortRangeSmallLinear(std::vector<std::size_t> &order,
                                const Nodes &nodes, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);
        std::size_t innerIdx = rangeIdx;
        for (; innerIdx > rangeBegin; --innerIdx) {
            const std::size_t previousOffset = innerIdx - 1;
            const std::size_t previousIndex = order[previousOffset];
            if (compareIdsMsbFirst(traits.id(nodes[previousIndex]), idValue,
                                   traits) <= 0) {
                break;
            }
            order[innerIdx] = previousIndex;
        }
        order[innerIdx] = nodeIndex;
    }
}

template <std::size_t ChunkBytes, typename Nodes, typename IdTraits,
          typename CountScratch>
ChunkedIndex<ChunkBytes> *stableLsdSortRangeByIdChunkWithCounter(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    ChunkedIndex<ChunkBytes> *current, ChunkedIndex<ChunkBytes> *next,
    CountScratch &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return current;
    }

    for (std::size_t offset = 0, orderOffset = rangeBegin; offset < rangeSize;
         ++offset, ++orderOffset) {
        const std::size_t nodeIndex = order[orderOffset];
        current[offset] = {chunkMsbFirst<ChunkBytes>(
                               traits.id(nodes[nodeIndex]), chunkIndex, traits),
                           nodeIndex};
    }

    ChunkedIndex<ChunkBytes> *source = current;
    ChunkedIndex<ChunkBytes> *destination = next;

    for (std::size_t bytePass = 0; bytePass < ChunkBytes; ++bytePass) {
        resetRadixCounts(countScratch);
        for (std::size_t offset = 0; offset < rangeSize; ++offset) {
            noteRadixDigit(countScratch,
                           wordByte(source[offset].chunk, bytePass));
        }

        prefixRadixCounts(countScratch);

        for (std::size_t offset = 0; offset < rangeSize; ++offset) {
            const uint8_t digit = wordByte(source[offset].chunk, bytePass);
            destination[countScratch.counts[digit]++] = source[offset];
        }

        clearRadixCounts(countScratch);
        std::swap(source, destination);
    }

    for (std::size_t offset = 0, orderOffset = rangeBegin; offset < rangeSize;
         ++offset, ++orderOffset) {
        order[orderOffset] = source[offset].index;
    }

    return source;
}

template <std::size_t ChunkBytes, typename Nodes, typename IdTraits>
ChunkedIndex<ChunkBytes> *stableLsdSortRangeByIdChunk(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    ChunkedIndex<ChunkBytes> *current, ChunkedIndex<ChunkBytes> *next) {
    FullClearCountScratch countScratch;
    return stableLsdSortRangeByIdChunkWithCounter(
        order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, current, next,
        countScratch);
}

template <std::size_t ChunkBytes, typename Nodes, typename IdTraits>
ChunkedIndex<ChunkBytes> *stableLsdSortRangeByIdChunkBitmaskTouched(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    ChunkedIndex<ChunkBytes> *current, ChunkedIndex<ChunkBytes> *next,
    BitmaskTouchedCountScratch &touchedScratch) {
    return stableLsdSortRangeByIdChunkWithCounter(
        order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, current, next,
        touchedScratch);
}

template <std::size_t ChunkBytes, typename CountPolicy, typename Nodes,
          typename IdTraits, typename Scratch>
inline ChunkedIndex<ChunkBytes> *
dispatchLsdChunkSort(std::vector<std::size_t> &order, const Nodes &nodes,
                     const IdTraits &traits, std::size_t rangeBegin,
                     std::size_t rangeEnd, std::size_t chunkIndex,
                     ChunkedIndex<ChunkBytes> *current,
                     ChunkedIndex<ChunkBytes> *next, Scratch &touchedScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
        return stableLsdSortRangeByIdChunk(order, nodes, traits, rangeBegin,
                                           rangeEnd, chunkIndex, current, next);
    } else {
        if (rangeSize <= CountPolicy::max_size) {
            return stableLsdSortRangeByIdChunkBitmaskTouched(
                order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, current,
                next, touchedScratch);
        }
        return stableLsdSortRangeByIdChunk(order, nodes, traits, rangeBegin,
                                           rangeEnd, chunkIndex, current, next);
    }
}

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename Nodes, typename IdTraits, typename SmallRangeSorter,
          typename Scratch = BitmaskTouchedCountScratch>
void sortRangeByIdChunksWithSmallSorter(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    std::vector<IdChunkRange> &pending,
    ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
    ChunkedIndex<ChunkBytes> *chunkBufferNext, Scratch &touchedScratch,
    SmallRangeSorter smallRangeSorter) {
    const std::size_t initialRangeSize = rangeEnd - rangeBegin;
    if (initialRangeSize <= 1) {
        return;
    }

    if (initialRangeSize <= SmallThreshold) {
        smallRangeSorter(order, nodes, traits, rangeBegin, rangeEnd);
        return;
    }

    pending.clear();
    pending.push_back(IdChunkRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (IdTraits::id_byte_count + ChunkBytes - 1) / ChunkBytes;

    while (!pending.empty()) {
        const IdChunkRange currentRange = pending.back();
        pending.pop_back();
        const std::size_t currentBegin = currentRange.begin;
        const std::size_t currentEnd = currentRange.end;
        const std::size_t currentChunkIndex = currentRange.chunkIndex;
        const std::size_t rangeSize = currentEnd - currentBegin;
        if (rangeSize <= 1 || currentChunkIndex >= chunkCount) {
            continue;
        }

        ChunkedIndex<ChunkBytes> *sortedChunks = nullptr;
        if (rangeSize <= SmallThreshold) {
            smallRangeSorter(order, nodes, traits, currentBegin, currentEnd);
        } else {
            sortedChunks = dispatchLsdChunkSort<ChunkBytes, CountPolicy>(
                order, nodes, traits, currentBegin, currentEnd,
                currentChunkIndex, chunkBufferCurrent, chunkBufferNext,
                touchedScratch);
        }

        const std::size_t nextChunkIndex = currentChunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalChunkBegin = currentBegin;
        ChunkValueType<ChunkBytes> previousChunk;
        if (sortedChunks != nullptr) {
            previousChunk = sortedChunks[0].chunk;
            for (std::size_t offset = 1; offset < rangeSize; ++offset) {
                const ChunkValueType<ChunkBytes> currentChunk =
                    sortedChunks[offset].chunk;
                if (currentChunk != previousChunk) {
                    const std::size_t splitOffset = currentBegin + offset;
                    pending.push_back(IdChunkRange{equalChunkBegin, splitOffset,
                                                   nextChunkIndex});
                    equalChunkBegin = splitOffset;
                    previousChunk = currentChunk;
                }
            }
        } else {
            const std::size_t firstNodeIndex = order[currentBegin];
            previousChunk = chunkMsbFirst<ChunkBytes>(
                traits.id(nodes[firstNodeIndex]), currentChunkIndex, traits);
            for (std::size_t offset = currentBegin + 1; offset < currentEnd;
                 ++offset) {
                const std::size_t nodeIndex = order[offset];
                const ChunkValueType<ChunkBytes> currentChunk =
                    chunkMsbFirst<ChunkBytes>(traits.id(nodes[nodeIndex]),
                                              currentChunkIndex, traits);
                if (currentChunk != previousChunk) {
                    pending.push_back(
                        IdChunkRange{equalChunkBegin, offset, nextChunkIndex});
                    equalChunkBegin = offset;
                    previousChunk = currentChunk;
                }
            }
        }

        pending.push_back(
            IdChunkRange{equalChunkBegin, currentEnd, nextChunkIndex});
    }
}

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts, typename Nodes,
          typename IdTraits>
void sortRangeByIdChunks(std::vector<std::size_t> &order, const Nodes &nodes,
                         const IdTraits &traits, std::size_t rangeBegin,
                         std::size_t rangeEnd, std::size_t chunkIndex,
                         std::vector<IdChunkRange> &pending,
                         ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
                         ChunkedIndex<ChunkBytes> *chunkBufferNext) {
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, const Nodes &sortNodes,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortRangeSmallLinear(sortOrder, sortNodes, sortTraits,
                                       sortBegin, sortEnd);
        };
    BitmaskTouchedCountScratch touchedScratch;

    sortRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy>(
        order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, pending,
        chunkBufferCurrent, chunkBufferNext, touchedScratch,
        linearSmallRangeSorter);
}

// -----------------------------------------------------------------------------
// Public detail entry point
// -----------------------------------------------------------------------------

template <std::size_t DepthPrefixBytes, typename Nodes, typename IdTraits,
          typename Depth>
void sortOrderByDepthAndId(std::vector<std::size_t> &order,
                           std::vector<std::size_t> &scratch,
                           const Nodes &nodes, const IdTraits &traits,
                           const std::vector<Depth> &depths,
                           uint32_t observedMaxDepth) {
    if (order.size() <= 1) {
        return;
    }

    // Dense grouping is limited to structurally possible depth values and a
    // fixed histogram resource cap. Arbitrary precomputed or very large valid
    // depths use MSD depth grouping instead.
    std::vector<DepthRange<Depth>> depthRanges;
    depthRanges.reserve(initial_range_stack_capacity);
    std::vector<std::size_t> depthStarts;
    std::vector<std::size_t> depthOffsets;

    if (shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        groupOrderByDepthDense(order, scratch, depths,
                               static_cast<Depth>(observedMaxDepth),
                               depthRanges, depthStarts, depthOffsets);
    } else {
        groupOrderByDepthMsd<
            DepthPrefixBytes,
            BitmaskTouchedCountsUpTo<production_touched_count_max_range_size>>(
            order, scratch, depths, depthRanges);
    }

    std::vector<IdChunkRange> pending;
    pending.reserve(initial_range_stack_capacity);

    std::size_t maxRadixRangeSize = 0;
    for (const DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize > small_id_range_sort_threshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    std::unique_ptr<ChunkedIndex<production_id_chunk_bytes>[]>
        chunkBufferCurrent;
    std::unique_ptr<ChunkedIndex<production_id_chunk_bytes>[]> chunkBufferNext;
    if (maxRadixRangeSize > 0) {
        chunkBufferCurrent =
            std::unique_ptr<ChunkedIndex<production_id_chunk_bytes>[]>(
                new ChunkedIndex<production_id_chunk_bytes>[maxRadixRangeSize]);
        chunkBufferNext =
            std::unique_ptr<ChunkedIndex<production_id_chunk_bytes>[]>(
                new ChunkedIndex<production_id_chunk_bytes>[maxRadixRangeSize]);
    }

    BitmaskTouchedCountScratch touchedScratch;
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, const Nodes &sortNodes,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortRangeSmallLinear(sortOrder, sortNodes, sortTraits,
                                       sortBegin, sortEnd);
        };

    for (const DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
#ifndef NDEBUG
        const Depth rangeDepth = range.depth;
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            assert(depths[order[offset]] == rangeDepth);
        }
#endif
        sortRangeByIdChunksWithSmallSorter<
            production_id_chunk_bytes,
            BitmaskTouchedCountsUpTo<production_touched_count_max_range_size>>(
            order, nodes, traits, rangeBegin, rangeEnd, 0, pending,
            chunkBufferCurrent.get(), chunkBufferNext.get(), touchedScratch,
            linearSmallRangeSorter);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
