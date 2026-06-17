#ifndef FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace forest_sorting::test_support {

inline std::vector<Node>
materializeOrder(const std::vector<Node> &nodes,
                 const std::vector<std::size_t> &order) {
    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }
    return sorted;
}

template <std::size_t DepthPrefixBytes>
inline uint32_t validateDepthLimit(const std::vector<uint32_t> &depths) {
    uint32_t observedMaxDepth = 0;
    for (uint32_t depth : depths) {
        if (depth > detail::maxDepthForPrefix<DepthPrefixBytes>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        observedMaxDepth = std::max(observedMaxDepth, depth);
    }
    return observedMaxDepth;
}

inline std::vector<Node>
sortForestByComparisonWithParent(const std::vector<Node> &nodes,
                                 const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    std::sort(order.begin(), order.end(),
              [&](std::size_t lhsIndex, std::size_t rhsIndex) {
                  if (depths[lhsIndex] != depths[rhsIndex]) {
                      return depths[lhsIndex] < depths[rhsIndex];
                  }
                  return nodes[lhsIndex].id < nodes[rhsIndex].id;
              });

    return materializeOrder(nodes, order);
}

inline uint8_t idByteLsbFirst(UInt128 nodeId, std::size_t byteOffset) noexcept {
    return UInt128Traits::byte_msb_first(nodeId, UInt128Traits::id_byte_count -
                                                     1 - byteOffset);
}

inline uint8_t depthByteLsbFirst(uint32_t depth,
                                 std::size_t byteOffset) noexcept {
    return static_cast<uint8_t>(depth >> (byteOffset * 8U));
}

template <typename DigitForIndex>
void radixLsdPass(std::vector<std::size_t> &order,
                  std::vector<std::size_t> &scratch,
                  DigitForIndex digitForIndex, std::size_t digitIndex) {
    std::array<std::size_t, detail::radix_bucket_count> counts{};
    for (std::size_t nodeIndex : order) {
        ++counts[digitForIndex(nodeIndex, digitIndex)];
    }

    std::size_t writeOffset = 0;
    for (std::size_t &count : counts) {
        const std::size_t bucketSize = count;
        count = writeOffset;
        writeOffset += bucketSize;
    }

    for (std::size_t nodeIndex : order) {
        scratch[counts[digitForIndex(nodeIndex, digitIndex)]++] = nodeIndex;
    }

    order.swap(scratch);
}

inline void radixLsdSortBucketById(std::vector<std::size_t> &bucket,
                                   std::vector<std::size_t> &scratch,
                                   const std::vector<Node> &nodes) {
    if (bucket.size() <= 1) {
        return;
    }

    scratch.resize(bucket.size());
    auto idDigitForIndex = [&](std::size_t nodeIndex, std::size_t byteOffset) {
        return idByteLsbFirst(nodes[nodeIndex].id, byteOffset);
    };

    for (std::size_t byteOffset = 0; byteOffset < UInt128Traits::id_byte_count;
         ++byteOffset) {
        radixLsdPass(bucket, scratch, idDigitForIndex, byteOffset);
    }
}

template <typename BucketSorter>
inline std::vector<Node> sortForestByDenseDepth2BucketsWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    BucketSorter bucketSorter) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    const uint32_t observedMaxDepth = validateDepthLimit<2>(depths);
    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(observedMaxDepth) + 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        buckets[depths[nodeIdx]].push_back(nodeIdx);
    }

    std::size_t maxBucketSize = 0;
    for (const auto &bucket : buckets) {
        maxBucketSize = std::max(maxBucketSize, bucket.size());
    }

    bucketSorter(buckets, maxBucketSize);

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (const auto &bucket : buckets) {
        for (std::size_t nodeIndex : bucket) {
            sorted.push_back(nodes[nodeIndex]);
        }
    }

    return sorted;
}

// Benchmark-only baseline
// Dense vector-of-buckets by depth
// Fixed 2-byte depth prefix limit
// No sparse-depth MSD fallback
// Unsafe without the validation guard for very large observed depths.
inline std::vector<Node> sortForestByDenseDepth2BucketedLsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto sortBuckets = [&](std::vector<std::vector<std::size_t>> &buckets,
                           std::size_t maxBucketSize) {
        std::vector<std::size_t> scratch;
        scratch.reserve(maxBucketSize);

        for (auto &bucket : buckets) {
            radixLsdSortBucketById(bucket, scratch, nodes);
        }
    };

    return sortForestByDenseDepth2BucketsWithParent(nodes, parentIndex,
                                                    sortBuckets);
}

// Benchmark wrapper for Composite LSD
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison.
inline std::vector<Node> sortForestByCompositeDepth2LsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    validateDepthLimit<2>(depths);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    std::vector<std::size_t> scratch(order.size());
    auto idDigitForIndex = [&](std::size_t nodeIndex, std::size_t byteOffset) {
        return idByteLsbFirst(nodes[nodeIndex].id, byteOffset);
    };
    auto depthDigitForIndex = [&](std::size_t nodeIndex,
                                  std::size_t byteOffset) {
        return depthByteLsbFirst(depths[nodeIndex], byteOffset);
    };

    for (std::size_t byteOffset = 0; byteOffset < UInt128Traits::id_byte_count;
         ++byteOffset) {
        radixLsdPass(order, scratch, idDigitForIndex, byteOffset);
    }
    for (std::size_t byteOffset = 0; byteOffset < 2; ++byteOffset) {
        radixLsdPass(order, scratch, depthDigitForIndex, byteOffset);
    }

    return materializeOrder(nodes, order);
}

inline void radixMsdSortBucketById(std::vector<std::size_t> &bucket,
                                   const std::vector<Node> &nodes,
                                   std::vector<detail::IdWordRange> &pending,
                                   detail::ChunkedIndex *chunkBufferCurrent,
                                   detail::ChunkedIndex *chunkBufferNext) {
    if (bucket.size() <= 1) {
        return;
    }

#ifndef NDEBUG
    assert(bucket.size() <= detail::small_id_range_sort_threshold ||
           (chunkBufferCurrent != nullptr && chunkBufferNext != nullptr));
#endif

    detail::sortRangeByIdWords(bucket, nodes, UInt128NodeTraits{}, 0,
                               bucket.size(), 0, pending, chunkBufferCurrent,
                               chunkBufferNext);
}

inline void stableSortRangeSmallBinary(std::vector<std::size_t> &order,
                                       const std::vector<Node> &nodes,
                                       std::size_t rangeBegin,
                                       std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto idValue = nodes[nodeIndex].id;

        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx;
        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const int comparison = detail::compareIdsMsbFirst(
                nodes[order[middle]].id, idValue, UInt128NodeTraits{});
            if (comparison <= 0) {
                searchBegin = middle + 1;
            } else {
                searchEnd = middle;
            }
        }

        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            order[moveIdx] = order[moveIdx - 1];
        }
        order[searchBegin] = nodeIndex;
    }
}

// Benchmark-only baseline
// Dense vector-of-buckets by depth
// Fixed 2-byte depth prefix limit
// No sparse-depth MSD fallback
// Unsafe without the validation guard for very large observed depths.
inline std::vector<Node> sortForestByDenseDepth2BucketedMsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto sortBuckets = [&](std::vector<std::vector<std::size_t>> &buckets,
                           std::size_t maxBucketSize) {
        std::vector<detail::IdWordRange> pending;
        pending.reserve(detail::initial_range_stack_capacity);
        std::unique_ptr<detail::ChunkedIndex[]> chunkBufferCurrent;
        std::unique_ptr<detail::ChunkedIndex[]> chunkBufferNext;
        if (maxBucketSize > detail::small_id_range_sort_threshold) {
            chunkBufferCurrent = std::unique_ptr<detail::ChunkedIndex[]>(
                new detail::ChunkedIndex[maxBucketSize]);
            chunkBufferNext = std::unique_ptr<detail::ChunkedIndex[]>(
                new detail::ChunkedIndex[maxBucketSize]);
        }

        for (auto &bucket : buckets) {
            radixMsdSortBucketById(bucket, nodes, pending,
                                   chunkBufferCurrent.get(),
                                   chunkBufferNext.get());
        }
    };

    return sortForestByDenseDepth2BucketsWithParent(nodes, parentIndex,
                                                    sortBuckets);
}

// Benchmark wrapper for Composite MSD
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison.
// Implements a full byte-MSD sort over the composite key (2 bytes depth + 16
// bytes ID).
inline std::vector<Node> sortForestByCompositeDepth2MsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    validateDepthLimit<2>(depths);

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    auto digitForIndex = [&](std::size_t nodeIndex, std::size_t digitIndex) {
        if (digitIndex == 0) {
            return static_cast<uint8_t>(depths[nodeIndex] >> 8U);
        }
        if (digitIndex == 1) {
            return static_cast<uint8_t>(depths[nodeIndex]);
        }
        return UInt128Traits::byte_msb_first(nodes[nodeIndex].id,
                                             digitIndex - 2);
    };

    auto recordCompletedRange = [](std::size_t, std::size_t) {};

    detail::radixMsdPartitionRanges(order, scratch, 0, order.size(), 0, 18,
                                    digitForIndex, recordCompletedRange);

    return materializeOrder(nodes, order);
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

template <typename SmallRangeSorter>
inline void
sortDepthRangesByChunkMsd(std::vector<std::size_t> &order,
                          const std::vector<Node> &nodes,
                          const std::vector<detail::DepthRange> &depthRanges,
                          SmallRangeSorter smallRangeSorter) {
    std::vector<detail::IdWordRange> pending;
    pending.reserve(detail::initial_range_stack_capacity);

    std::size_t maxRadixRangeSize = 0;
    for (const detail::DepthRange &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
        if (rangeSize > detail::small_id_range_sort_threshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferCurrent;
    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferNext;
    if (maxRadixRangeSize > 0) {
        chunkBufferCurrent = std::unique_ptr<detail::ChunkedIndex[]>(
            new detail::ChunkedIndex[maxRadixRangeSize]);
        chunkBufferNext = std::unique_ptr<detail::ChunkedIndex[]>(
            new detail::ChunkedIndex[maxRadixRangeSize]);
    }

    for (const detail::DepthRange &range : depthRanges) {
        detail::sortRangeByIdWordsWithSmallSorter(
            order, nodes, UInt128NodeTraits{}, range.begin, range.end, 0,
            pending, chunkBufferCurrent.get(), chunkBufferNext.get(),
            smallRangeSorter);
    }
}

inline void
sortDepthRangesByByteMsd(std::vector<std::size_t> &order,
                         std::vector<std::size_t> &scratch,
                         const std::vector<Node> &nodes,
                         const std::vector<detail::DepthRange> &depthRanges) {
    std::vector<detail::RadixRange> pending;
    pending.reserve(detail::initial_range_stack_capacity);

    for (const detail::DepthRange &range : depthRanges) {
        detail::sortRangeByIdBytes(order, scratch, nodes, UInt128NodeTraits{},
                                   range.begin, range.end, 0, pending);
    }
}

inline void sortDepthRangesByChunkMsdLinearSmall(
    std::vector<std::size_t> &order, std::vector<std::size_t> &unusedScratch,
    const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange> &depthRanges) {
    (void)unusedScratch;
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder,
           const std::vector<Node> &sortNodes, const UInt128NodeTraits &traits,
           std::size_t sortBegin, std::size_t sortEnd) {
            detail::stableSortRangeSmall(sortOrder, sortNodes, traits,
                                         sortBegin, sortEnd);
        };
    sortDepthRangesByChunkMsd(order, nodes, depthRanges,
                              linearSmallRangeSorter);
}

inline void sortDepthRangesByChunkMsdBinarySmall(
    std::vector<std::size_t> &order, std::vector<std::size_t> &unusedScratch,
    const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange> &depthRanges) {
    (void)unusedScratch;
    auto binarySmallRangeSorter = [](std::vector<std::size_t> &sortOrder,
                                     const std::vector<Node> &sortNodes,
                                     const UInt128NodeTraits &,
                                     std::size_t sortBegin,
                                     std::size_t sortEnd) {
        stableSortRangeSmallBinary(sortOrder, sortNodes, sortBegin, sortEnd);
    };
    sortDepthRangesByChunkMsd(order, nodes, depthRanges,
                              binarySmallRangeSorter);
}

// Benchmark wrapper for Adaptive MSD (forced Depth-MSD grouping)
// Locked to 2-byte depth prefix. Bypasses the dense grouping shortcut.
inline std::vector<Node> sortForestByAdaptiveDepth2NoDenseMsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveRangeSorterWithParent<2>(
        nodes, parentIndex, false, sortDepthRangesByChunkMsdLinearSmall);
}

// Benchmark wrapper for Adaptive MSD
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison,
// although the underlying public API supports 1-4 byte prefixes.
inline std::vector<Node> sortForestByAdaptiveDepth2WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveRangeSorterWithParent<2>(
        nodes, parentIndex, true, sortDepthRangesByChunkMsdLinearSmall);
}

inline std::vector<Node> sortForestByAdaptiveDepth2BinarySmallWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveRangeSorterWithParent<2>(
        nodes, parentIndex, true, sortDepthRangesByChunkMsdBinarySmall);
}

inline std::vector<Node> sortForestByAdaptiveDepth2ByteMsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveRangeSorterWithParent<2>(
        nodes, parentIndex, true, sortDepthRangesByByteMsd);
}

// Benchmark wrapper for Adaptive MSD (4-byte depth prefix)
// Demonstrates production-like capabilities with full 32-bit depth support.
inline std::vector<Node> sortForestByAdaptiveDepth4WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveRangeSorterWithParent<4>(
        nodes, parentIndex, true, sortDepthRangesByChunkMsdLinearSmall);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
