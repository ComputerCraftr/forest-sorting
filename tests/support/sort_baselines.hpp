#ifndef FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP

#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
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
inline void validateDepthLimit(const std::vector<uint32_t> &depths) {
    for (uint32_t depth : depths) {
        if (depth > detail::maxDepthForPrefix<DepthPrefixBytes>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
    }
}

inline uint32_t observedMaxDepthValue(const std::vector<uint32_t> &depths) {
    uint32_t observedMaxDepth = 0;
    for (uint32_t depth : depths) {
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

inline std::vector<Node>
sortForestByComparison(const std::vector<Node> &nodes) {
    return sortForestByComparisonWithParent(nodes,
                                            buildParentIndexForUInt128(nodes));
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
                                   const std::vector<Node> &nodes) {
    if (bucket.size() <= 1) {
        return;
    }

    std::vector<std::size_t> scratch(bucket.size());
    auto idDigitForIndex = [&](std::size_t nodeIndex, std::size_t byteOffset) {
        return idByteLsbFirst(nodes[nodeIndex].id, byteOffset);
    };

    for (std::size_t byteOffset = 0; byteOffset < UInt128Traits::id_byte_count;
         ++byteOffset) {
        radixLsdPass(bucket, scratch, idDigitForIndex, byteOffset);
    }
}

// Benchmark-only baseline
// Dense vector-of-buckets by depth
// Fixed 2-byte depth prefix limit
// No sparse-depth MSD fallback
// Unsafe without the validation guard for very large observed depths.
inline std::vector<Node> sortForestByDenseDepth2BucketedLsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    validateDepthLimit<2>(depths);

    const uint32_t observedMaxDepth = observedMaxDepthValue(depths);
    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(observedMaxDepth) + 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        buckets[depths[nodeIdx]].push_back(nodeIdx);
    }

    for (auto &bucket : buckets) {
        radixLsdSortBucketById(bucket, nodes);
    }

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
inline std::vector<Node>
sortForestByDenseDepth2BucketedLsd(const std::vector<Node> &nodes) {
    return sortForestByDenseDepth2BucketedLsdWithParent(
        nodes, buildParentIndexForUInt128(nodes));
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

inline std::vector<Node>
sortForestByCompositeDepth2Lsd(const std::vector<Node> &nodes) {
    return sortForestByCompositeDepth2LsdWithParent(
        nodes, buildParentIndexForUInt128(nodes));
}

inline void radixMsdSortBucketById(std::vector<std::size_t> &bucket,
                                   const std::vector<Node> &nodes) {
    if (bucket.size() <= 1) {
        return;
    }

    std::vector<detail::IdWordRange> pending;
    pending.reserve(128);
    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferCurrent(
        new detail::ChunkedIndex[bucket.size()]);
    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferNext(
        new detail::ChunkedIndex[bucket.size()]);

    detail::sortRangeByIdWords(bucket, nodes, UInt128NodeTraits{}, 0,
                               bucket.size(), 0, pending,
                               chunkBufferCurrent.get(), chunkBufferNext.get());
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

inline void sortRangeByIdWordsBinarySmall(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    std::vector<detail::IdWordRange> &pending,
    detail::ChunkedIndex *chunkBufferCurrent,
    detail::ChunkedIndex *chunkBufferNext) {
    if (rangeEnd - rangeBegin <= 1) {
        return;
    }

    if (rangeEnd - rangeBegin <= detail::small_id_range_sort_threshold) {
        stableSortRangeSmallBinary(order, nodes, rangeBegin, rangeEnd);
        return;
    }

    pending.clear();
    pending.push_back(detail::IdWordRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (UInt128NodeTraits::id_byte_count + detail::chunk_byte_count - 1) /
        detail::chunk_byte_count;

    while (!pending.empty()) {
        const detail::IdWordRange currentRange = pending.back();
        pending.pop_back();
        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.chunkIndex >= chunkCount) {
            continue;
        }

        if (currentRange.end - currentRange.begin <=
            detail::small_id_range_sort_threshold) {
            stableSortRangeSmallBinary(order, nodes, currentRange.begin,
                                       currentRange.end);
        } else {
            detail::stableSortRangeByIdWord(
                order, nodes, UInt128NodeTraits{}, currentRange.begin,
                currentRange.end, currentRange.chunkIndex, chunkBufferCurrent,
                chunkBufferNext);
        }

        const std::size_t nextChunkIndex = currentRange.chunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalWordBegin = currentRange.begin;
        uint64_t previousChunk =
            detail::chunkMsbFirst(nodes[order[currentRange.begin]].id,
                                  currentRange.chunkIndex, UInt128NodeTraits{});
        for (std::size_t offset = currentRange.begin + 1;
             offset < currentRange.end; ++offset) {
            const uint64_t currentChunk = detail::chunkMsbFirst(
                nodes[order[offset]].id, currentRange.chunkIndex,
                UInt128NodeTraits{});
            if (currentChunk != previousChunk) {
                pending.push_back(detail::IdWordRange{equalWordBegin, offset,
                                                      nextChunkIndex});
                equalWordBegin = offset;
                previousChunk = currentChunk;
            }
        }

        pending.push_back(detail::IdWordRange{equalWordBegin, currentRange.end,
                                              nextChunkIndex});
    }
}

template <std::size_t DepthPrefixBytes>
inline std::vector<Node> sortForestByAdaptiveBinarySmallWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    validateDepthLimit<DepthPrefixBytes>(depths);
    const uint32_t observedMaxDepth = observedMaxDepthValue(depths);

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    std::vector<detail::DepthRange> depthRanges;
    if (detail::shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        depthRanges = detail::groupOrderByDepthDense(order, scratch, depths,
                                                     observedMaxDepth);
    } else {
        depthRanges = detail::groupOrderByDepthMsd<DepthPrefixBytes>(
            order, scratch, depths);
    }

    std::size_t maxRadixRangeSize = 0;
    for (const detail::DepthRange &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
        if (rangeSize > detail::small_id_range_sort_threshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    std::vector<detail::IdWordRange> pending;
    pending.reserve(128);
    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferCurrent;
    std::unique_ptr<detail::ChunkedIndex[]> chunkBufferNext;
    if (maxRadixRangeSize > 0) {
        chunkBufferCurrent = std::unique_ptr<detail::ChunkedIndex[]>(
            new detail::ChunkedIndex[maxRadixRangeSize]);
        chunkBufferNext = std::unique_ptr<detail::ChunkedIndex[]>(
            new detail::ChunkedIndex[maxRadixRangeSize]);
    }

    for (const detail::DepthRange &range : depthRanges) {
        sortRangeByIdWordsBinarySmall(order, nodes, range.begin, range.end, 0,
                                      pending, chunkBufferCurrent.get(),
                                      chunkBufferNext.get());
    }

    return materializeOrder(nodes, order);
}

// Benchmark-only baseline
// Dense vector-of-buckets by depth
// Fixed 2-byte depth prefix limit
// No sparse-depth MSD fallback
// Unsafe without the validation guard for very large observed depths.
inline std::vector<Node> sortForestByDenseDepth2BucketedMsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    validateDepthLimit<2>(depths);

    const uint32_t observedMaxDepth = observedMaxDepthValue(depths);
    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(observedMaxDepth) + 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        buckets[depths[nodeIdx]].push_back(nodeIdx);
    }

    for (auto &bucket : buckets) {
        radixMsdSortBucketById(bucket, nodes);
    }

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
inline std::vector<Node>
sortForestByDenseDepth2BucketedMsd(const std::vector<Node> &nodes) {
    return sortForestByDenseDepth2BucketedMsdWithParent(
        nodes, buildParentIndexForUInt128(nodes));
}

// Benchmark wrapper for Composite MSD
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison.
inline std::vector<Node> sortForestByCompositeDepth2MsdWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    const auto order = sortedOrderByDepthAndIdWithDepths<2>(
        nodes, UInt128NodeTraits{}, depths);
    return materializeOrder(nodes, order);
}

inline std::vector<Node>
sortForestByCompositeDepth2Msd(const std::vector<Node> &nodes) {
    return sortForestByCompositeDepth2MsdWithParent(
        nodes, buildParentIndexForUInt128(nodes));
}

// Benchmark wrapper for Adaptive MSD
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison,
// although the underlying public API supports 1-4 byte prefixes.
inline std::vector<Node> sortForestByAdaptiveDepth2WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    const auto order = sortedOrderByDepthAndIdWithDepths<2>(
        nodes, UInt128NodeTraits{}, depths);
    return materializeOrder(nodes, order);
}

inline std::vector<Node> sortForestByAdaptiveDepth2BinarySmallWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveBinarySmallWithParent<2>(nodes, parentIndex);
}

// Benchmark wrapper for Adaptive MSD (4-byte depth prefix)
// Demonstrates production-like capabilities with full 32-bit depth support.
inline std::vector<Node> sortForestByAdaptiveDepth4WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);
    const auto order = sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);
    return materializeOrder(nodes, order);
}

inline UInt128 checksumIds(const std::vector<Node> &nodes) {
    UInt128 checksum = 0;
    for (const auto &node : nodes) {
        checksum ^= node.id;
        checksum = (checksum << 1) | (checksum >> 127);
    }
    return checksum;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
