#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"
#include <algorithm>
#include <array>
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
inline constexpr std::size_t production_id_chunk_bytes = 4;

// Touched/generation count variants are only intended for medium ranges where
// clearing all 256 counters can dominate useful work. Larger ranges keep the
// simpler full-clear counter path.
inline constexpr std::size_t touched_count_min_range_size =
    small_id_range_sort_threshold + 1;
inline constexpr std::size_t touched_count_max_range_size = 4096;

inline bool shouldUseTouchedCounts(std::size_t rangeSize) noexcept {
    return rangeSize >= touched_count_min_range_size &&
           rangeSize <= touched_count_max_range_size;
}

// -----------------------------------------------------------------------------
// Internal range/buffer records
// -----------------------------------------------------------------------------

struct DepthRange {
    uint32_t depth;
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

struct FullClearCounts {};
struct TouchedCounts {};

struct TouchedCountScratch {
    std::array<std::size_t, radix_bucket_count> counts{};
    std::array<uint32_t, radix_bucket_count> seenGeneration{};
    uint32_t generation = 1;

    void advanceGeneration() noexcept {
        ++generation;
        if (generation == 0) {
            seenGeneration.fill(0);
            generation = 1;
        }
    }
};

template <typename CountPolicy> struct CountScratch {};
template <> struct CountScratch<TouchedCounts> {
    TouchedCountScratch touched;
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

inline void groupOrderByDepthDense(std::vector<std::size_t> &order,
                                   std::vector<std::size_t> &scratch,
                                   const std::vector<uint32_t> &depths,
                                   uint32_t observedMaxDepth,
                                   std::vector<DepthRange> &ranges,
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
    for (std::size_t depthIdx = 0; depthIdx <= observedMaxDepth; ++depthIdx) {
        const std::size_t rangeBegin = depthStarts[depthIdx];
        const std::size_t rangeEnd = depthStarts[depthIdx + 1];
        if (rangeBegin != rangeEnd) {
            ranges.push_back(
                {static_cast<uint32_t>(depthIdx), rangeBegin, rangeEnd});
        }
    }
}

template <std::size_t DepthPrefixBytes>
inline void groupOrderByDepthMsd(std::vector<std::size_t> &order,
                                 std::vector<std::size_t> &scratch,
                                 const std::vector<uint32_t> &depths,
                                 std::vector<DepthRange> &ranges) {
    ranges.clear();
    ranges.reserve(initial_range_stack_capacity);

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

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts, typename Nodes,
          typename IdTraits>
void stableLsdSortRangeByIdChunk(std::vector<std::size_t> &order,
                                 const Nodes &nodes, const IdTraits &traits,
                                 std::size_t rangeBegin, std::size_t rangeEnd,
                                 std::size_t chunkIndex,
                                 ChunkedIndex<ChunkBytes> *current,
                                 ChunkedIndex<ChunkBytes> *next,
                                 CountScratch<CountPolicy> &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
        const std::size_t index = order[rangeBegin + nodeIdx];
        current[nodeIdx] = {chunkMsbFirst<ChunkBytes>(traits.id(nodes[index]),
                                                      chunkIndex, traits),
                            index};
    }

    for (std::size_t byteIndex = 0; byteIndex < ChunkBytes; ++byteIndex) {
        if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
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
        } else {
            auto &touched = countScratch.touched;
            for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
                const uint8_t digit =
                    wordByte(current[nodeIdx].chunk, byteIndex);
                if (touched.seenGeneration[digit] != touched.generation) {
                    touched.seenGeneration[digit] = touched.generation;
                    touched.counts[digit] = 0;
                }
                ++touched.counts[digit];
            }

            std::size_t writeOffset = 0;
            for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
                 ++bucketIdx) {
                const std::size_t bucketSize =
                    touched.seenGeneration[bucketIdx] == touched.generation
                        ? touched.counts[bucketIdx]
                        : 0;
                touched.counts[bucketIdx] = writeOffset;
                writeOffset += bucketSize;
            }

            for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
                const auto entry = current[nodeIdx];
                const uint8_t digit = wordByte(entry.chunk, byteIndex);
                next[touched.counts[digit]++] = entry;
            }

            touched.advanceGeneration();
        }

        std::swap(current, next);
    }

    for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
        order[rangeBegin + nodeIdx] = current[nodeIdx].index;
    }
}

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts, typename Nodes,
          typename IdTraits, typename SmallRangeSorter>
void sortRangeByIdChunksWithSmallSorter(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    std::vector<IdChunkRange> &pending,
    ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
    ChunkedIndex<ChunkBytes> *chunkBufferNext,
    CountScratch<CountPolicy> &countScratch,
    SmallRangeSorter smallRangeSorter) {
    // ChunkBytes = 1, 4, and 8 all use this scheduler. A one-byte chunk is
    // byte-MSD behavior without a separate byte-partition implementation.
    if (rangeEnd - rangeBegin <= 1) {
        return;
    }

    if (rangeEnd - rangeBegin <= small_id_range_sort_threshold) {
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
        const std::size_t rangeSize = currentRange.end - currentRange.begin;
        if (rangeSize <= 1 || currentRange.chunkIndex >= chunkCount) {
            continue;
        }

        if (rangeSize <= small_id_range_sort_threshold) {
            smallRangeSorter(order, nodes, traits, currentRange.begin,
                             currentRange.end);
        } else {
            if constexpr (std::is_same_v<CountPolicy, TouchedCounts>) {
                if (shouldUseTouchedCounts(rangeSize)) {
                    stableLsdSortRangeByIdChunk<ChunkBytes, CountPolicy>(
                        order, nodes, traits, currentRange.begin,
                        currentRange.end, currentRange.chunkIndex,
                        chunkBufferCurrent, chunkBufferNext, countScratch);
                } else {
                    CountScratch<FullClearCounts> fullClearScratch;
                    stableLsdSortRangeByIdChunk<ChunkBytes, FullClearCounts>(
                        order, nodes, traits, currentRange.begin,
                        currentRange.end, currentRange.chunkIndex,
                        chunkBufferCurrent, chunkBufferNext, fullClearScratch);
                }
            } else {
                stableLsdSortRangeByIdChunk<ChunkBytes, CountPolicy>(
                    order, nodes, traits, currentRange.begin, currentRange.end,
                    currentRange.chunkIndex, chunkBufferCurrent,
                    chunkBufferNext, countScratch);
            }
        }

        const std::size_t nextChunkIndex = currentRange.chunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalChunkBegin = currentRange.begin;
        ChunkValueType<ChunkBytes> previousChunk = chunkMsbFirst<ChunkBytes>(
            traits.id(nodes[order[currentRange.begin]]),
            currentRange.chunkIndex, traits);
        for (std::size_t offset = currentRange.begin + 1;
             offset < currentRange.end; ++offset) {
            const ChunkValueType<ChunkBytes> currentChunk =
                chunkMsbFirst<ChunkBytes>(traits.id(nodes[order[offset]]),
                                          currentRange.chunkIndex, traits);
            if (currentChunk != previousChunk) {
                pending.push_back(
                    IdChunkRange{equalChunkBegin, offset, nextChunkIndex});
                equalChunkBegin = offset;
                previousChunk = currentChunk;
            }
        }

        pending.push_back(
            IdChunkRange{equalChunkBegin, currentRange.end, nextChunkIndex});
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
            stableSortRangeSmall(sortOrder, sortNodes, sortTraits, sortBegin,
                                 sortEnd);
        };
    CountScratch<CountPolicy> countScratch;

    sortRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy>(
        order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, pending,
        chunkBufferCurrent, chunkBufferNext, countScratch,
        linearSmallRangeSorter);
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
    depthRanges.reserve(initial_range_stack_capacity);
    std::vector<std::size_t> depthStarts;
    std::vector<std::size_t> depthOffsets;

    if (shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        groupOrderByDepthDense(order, scratch, depths, observedMaxDepth,
                               depthRanges, depthStarts, depthOffsets);
    } else {
        groupOrderByDepthMsd<DepthPrefixBytes>(order, scratch, depths,
                                               depthRanges);
    }

    std::vector<IdChunkRange> pending;
    pending.reserve(initial_range_stack_capacity);

    std::size_t maxRadixRangeSize = 0;
    for (const DepthRange &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
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

    for (const DepthRange &range : depthRanges) {
#ifndef NDEBUG
        for (std::size_t offset = range.begin; offset < range.end; ++offset) {
            assert(depths[order[offset]] == range.depth);
        }
#endif
        sortRangeByIdChunks<production_id_chunk_bytes>(
            order, nodes, traits, range.begin, range.end, 0, pending,
            chunkBufferCurrent.get(), chunkBufferNext.get());
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
