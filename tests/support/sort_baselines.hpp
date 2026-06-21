#ifndef FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
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

inline std::vector<Node>
sortForestByComparisonWithParent(const std::vector<Node> &nodes,
                                 const std::vector<std::size_t> &parentIndex) {
    const auto depths = computeDepthsForUInt128<4>(nodes, parentIndex);

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

struct UInt128IdKey {
    static constexpr std::size_t byte_count = UInt128Traits::id_byte_count;

    const std::vector<Node> &nodes;

    uint8_t byte_msb_first(std::size_t nodeIndex,
                           std::size_t byteIndex) const noexcept {
        return UInt128Traits::byte_msb_first(nodes[nodeIndex].id, byteIndex);
    }
};

struct CompositeDepth2UInt128Key {
    using Depth = uint16_t;

    static constexpr std::size_t depth_byte_count = 2;
    static constexpr std::size_t byte_count =
        depth_byte_count + UInt128Traits::id_byte_count;

    const std::vector<Node> &nodes;
    const std::vector<Depth> &depths;

    uint8_t byte_msb_first(std::size_t nodeIndex,
                           std::size_t byteIndex) const noexcept {
        if (byteIndex == 0) {
            return static_cast<uint8_t>(depths[nodeIndex] >> 8U);
        }
        if (byteIndex == 1) {
            return static_cast<uint8_t>(depths[nodeIndex]);
        }
        return UInt128Traits::byte_msb_first(nodes[nodeIndex].id,
                                             byteIndex - depth_byte_count);
    }
};

template <typename FixedKey>
inline uint8_t lsbByteForFixedKey(const FixedKey &key, std::size_t nodeIndex,
                                  std::size_t byteOffset) noexcept {
    return key.byte_msb_first(nodeIndex, FixedKey::byte_count - 1 - byteOffset);
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

template <typename FixedKey>
void radixLsdSortByFixedKey(std::vector<std::size_t> &order,
                            std::vector<std::size_t> &scratch,
                            const FixedKey &key) {
    if (order.size() <= 1) {
        return;
    }

    auto digitForIndex = [&](std::size_t nodeIndex, std::size_t byteOffset) {
        return lsbByteForFixedKey(key, nodeIndex, byteOffset);
    };

    for (std::size_t byteOffset = 0; byteOffset < FixedKey::byte_count;
         ++byteOffset) {
        radixLsdPass(order, scratch, digitForIndex, byteOffset);
    }
}

inline void radixLsdSortBucketById(std::vector<std::size_t> &bucket,
                                   std::vector<std::size_t> &scratch,
                                   const std::vector<Node> &nodes) {
    if (bucket.size() <= 1) {
        return;
    }

    scratch.resize(bucket.size());
    radixLsdSortByFixedKey(bucket, scratch, UInt128IdKey{nodes});
}

template <typename BucketSorter>
inline std::vector<Node> sortForestByDenseDepth2BucketsWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    BucketSorter bucketSorter) {
    auto computed =
        detail::computeDepths<2>(nodes, parentIndex, UInt128NodeTraits{});
    const auto &depths = computed.values;
    const uint16_t observedMaxDepth = computed.observedMax;
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
    const auto depths = computeDepthsForUInt128<2>(nodes, parentIndex);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    std::vector<std::size_t> scratch(order.size());
    radixLsdSortByFixedKey(order, scratch,
                           CompositeDepth2UInt128Key{nodes, depths});

    return materializeOrder(nodes, order);
}

inline void radixMsdSortBucketById(
    std::vector<std::size_t> &bucket, const std::vector<Node> &nodes,
    std::vector<detail::IdChunkRange> &pending,
    detail::ChunkedIndex<detail::chunk_byte_count> *chunkBufferCurrent,
    detail::ChunkedIndex<detail::chunk_byte_count> *chunkBufferNext) {
    if (bucket.size() <= 1) {
        return;
    }

#ifndef NDEBUG
    assert(bucket.size() <= detail::small_id_range_sort_threshold ||
           (chunkBufferCurrent != nullptr && chunkBufferNext != nullptr));
#endif

    detail::sortRangeByIdChunks(bucket, nodes, UInt128NodeTraits{}, 0,
                                bucket.size(), 0, pending, chunkBufferCurrent,
                                chunkBufferNext);
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
        std::vector<detail::IdChunkRange> pending;
        pending.reserve(detail::initial_range_stack_capacity);
        std::unique_ptr<detail::ChunkedIndex<detail::chunk_byte_count>[]>
            chunkBufferCurrent;
        std::unique_ptr<detail::ChunkedIndex<detail::chunk_byte_count>[]>
            chunkBufferNext;
        if (maxBucketSize > detail::small_id_range_sort_threshold) {
            chunkBufferCurrent = std::unique_ptr<
                detail::ChunkedIndex<detail::chunk_byte_count>[]>(
                new detail::ChunkedIndex<
                    detail::chunk_byte_count>[maxBucketSize]);
            chunkBufferNext = std::unique_ptr<
                detail::ChunkedIndex<detail::chunk_byte_count>[]>(
                new detail::ChunkedIndex<
                    detail::chunk_byte_count>[maxBucketSize]);
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

template <typename DigitForIndex>
inline void radixMsdPartitionRangesBranchyLowcopy(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    std::size_t begin, std::size_t end, std::size_t firstDigit,
    std::size_t digitCount, DigitForIndex digitForIndex) {
    struct Range {
        std::size_t begin;
        std::size_t end;
        std::size_t digitIndex;
        bool sourceIsOrder;
    };

    std::vector<Range> pending;
    pending.reserve(detail::initial_range_stack_capacity);
    pending.push_back({begin, end, firstDigit, true});

    while (!pending.empty()) {
        const Range range = pending.back();
        pending.pop_back();
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t digitIndex = range.digitIndex;
        const bool sourceIsOrder = range.sourceIsOrder;
        const std::size_t rangeSize = rangeEnd - rangeBegin;

        if (rangeSize <= 1 || digitIndex == digitCount) {
            if (!sourceIsOrder) {
                for (std::size_t offset = rangeBegin; offset < rangeEnd;
                     ++offset) {
                    order[offset] = scratch[offset];
                }
            }
            continue;
        }

        std::array<std::size_t, detail::radix_bucket_count> counts{};
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            const std::size_t nodeIndex =
                sourceIsOrder ? order[offset] : scratch[offset];
            ++counts[digitForIndex(nodeIndex, digitIndex)];
        }

        const std::size_t nonZeroBuckets =
            detail::countNonZeroBuckets(detail::FullClearCountScratch{counts});
        if (nonZeroBuckets == 0) {
            continue;
        }

        if (nonZeroBuckets == 1) {
            pending.push_back(
                {rangeBegin, rangeEnd, digitIndex + 1, sourceIsOrder});
            continue;
        }

        std::array<std::size_t, detail::radix_bucket_count> bucketStarts{};
        std::size_t currentOffset = rangeBegin;
        for (std::size_t bucketIdx = 0; bucketIdx < detail::radix_bucket_count;
             ++bucketIdx) {
            bucketStarts[bucketIdx] = currentOffset;
            currentOffset += counts[bucketIdx];
        }

        std::array<std::size_t, detail::radix_bucket_count> bucketOffsets =
            bucketStarts;
        if (sourceIsOrder) {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                const std::size_t nodeIndex = order[offset];
                const uint8_t digit = digitForIndex(nodeIndex, digitIndex);
                scratch[bucketOffsets[digit]++] = nodeIndex;
            }
        } else {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                const std::size_t nodeIndex = scratch[offset];
                const uint8_t digit = digitForIndex(nodeIndex, digitIndex);
                order[bucketOffsets[digit]++] = nodeIndex;
            }
        }

        for (std::size_t reverseBucketIdx = 0;
             reverseBucketIdx < detail::radix_bucket_count;
             ++reverseBucketIdx) {
            const std::size_t bucketIdx =
                detail::radix_bucket_count - 1 - reverseBucketIdx;
            if (counts[bucketIdx] > 0) {
                pending.push_back({bucketStarts[bucketIdx],
                                   bucketOffsets[bucketIdx], digitIndex + 1,
                                   !sourceIsOrder});
            }
        }
    }
}

struct SupportLowcopyRange {
    std::size_t begin;
    std::size_t end;
    std::size_t digitIndex;
    bool sourceIsOrder;
};

template <typename DigitForOffset, typename MoveToOther>
inline void radixMsdProcessSupportLowcopyRange(
    std::size_t begin, std::size_t end, std::size_t digitIndex,
    std::vector<SupportLowcopyRange> &pending, DigitForOffset digitForOffset,
    MoveToOther moveToOther, bool childSourceIsOrder) {
    std::array<std::size_t, detail::radix_bucket_count> counts{};
    for (std::size_t offset = begin; offset < end; ++offset) {
        ++counts[digitForOffset(offset, digitIndex)];
    }

    const std::size_t nonZeroBuckets =
        detail::countNonZeroBuckets(detail::FullClearCountScratch{counts});
    if (nonZeroBuckets == 0) {
        return;
    }

    if (nonZeroBuckets == 1) {
        pending.push_back({begin, end, digitIndex + 1, !childSourceIsOrder});
        return;
    }

    std::array<std::size_t, detail::radix_bucket_count> bucketStarts{};
    std::size_t currentOffset = begin;
    for (std::size_t bucketIdx = 0; bucketIdx < detail::radix_bucket_count;
         ++bucketIdx) {
        bucketStarts[bucketIdx] = currentOffset;
        currentOffset += counts[bucketIdx];
    }

    std::array<std::size_t, detail::radix_bucket_count> bucketOffsets =
        bucketStarts;
    for (std::size_t offset = begin; offset < end; ++offset) {
        const uint8_t digit = digitForOffset(offset, digitIndex);
        moveToOther(offset, bucketOffsets[digit]++);
    }

    for (std::size_t reverseBucketIdx = 0;
         reverseBucketIdx < detail::radix_bucket_count; ++reverseBucketIdx) {
        const std::size_t bucketIdx =
            detail::radix_bucket_count - 1 - reverseBucketIdx;
        if (counts[bucketIdx] > 0) {
            pending.push_back({bucketStarts[bucketIdx],
                               bucketOffsets[bucketIdx], digitIndex + 1,
                               childSourceIsOrder});
        }
    }
}

template <typename DigitForIndex>
inline void radixMsdPartitionRangesFlattenedLowcopy(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    std::size_t begin, std::size_t end, std::size_t firstDigit,
    std::size_t digitCount, DigitForIndex digitForIndex) {
    std::vector<SupportLowcopyRange> pending;
    pending.reserve(detail::initial_range_stack_capacity);
    pending.push_back({begin, end, firstDigit, true});

    while (!pending.empty()) {
        const SupportLowcopyRange range = pending.back();
        pending.pop_back();
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t digitIndex = range.digitIndex;
        const bool sourceIsOrder = range.sourceIsOrder;
        const std::size_t rangeSize = rangeEnd - rangeBegin;

        if (rangeSize <= 1 || digitIndex == digitCount) {
            if (!sourceIsOrder) {
                for (std::size_t offset = rangeBegin; offset < rangeEnd;
                     ++offset) {
                    order[offset] = scratch[offset];
                }
            }
            continue;
        }

        if (sourceIsOrder) {
            auto digitFromOrder = [&](std::size_t offset,
                                      std::size_t digitIndex) {
                return digitForIndex(order[offset], digitIndex);
            };
            auto moveOrderToScratch = [&](std::size_t offset,
                                          std::size_t scratchOffset) {
                scratch[scratchOffset] = order[offset];
            };
            radixMsdProcessSupportLowcopyRange(rangeBegin, rangeEnd, digitIndex,
                                               pending, digitFromOrder,
                                               moveOrderToScratch, false);
        } else {
            auto digitFromScratch = [&](std::size_t offset,
                                        std::size_t digitIndex) {
                return digitForIndex(scratch[offset], digitIndex);
            };
            auto moveScratchToOrder = [&](std::size_t offset,
                                          std::size_t orderOffset) {
                order[orderOffset] = scratch[offset];
            };
            radixMsdProcessSupportLowcopyRange(rangeBegin, rangeEnd, digitIndex,
                                               pending, digitFromScratch,
                                               moveScratchToOrder, true);
        }
    }
}

inline constexpr std::size_t batched_lowcopy_digit_budget = 2;
inline constexpr std::size_t batched_lowcopy_min_range = 4096;

struct BatchedLowcopyRange {
    std::size_t begin;
    std::size_t end;
    std::size_t digitIndex;
    std::size_t remainingBudget;
    bool sourceIsOrder;
};

inline void coalescedCopyScratchRangesToOrder(
    std::vector<std::size_t> &order, const std::vector<std::size_t> &scratch,
    std::vector<SupportLowcopyRange> &scratchRanges) {
    if (scratchRanges.empty()) {
        return;
    }

    std::sort(
        scratchRanges.begin(), scratchRanges.end(),
        [](const SupportLowcopyRange &lhs, const SupportLowcopyRange &rhs) {
            return lhs.begin < rhs.begin;
        });

    std::size_t rangeBegin = scratchRanges.front().begin;
    std::size_t rangeEnd = scratchRanges.front().end;
    for (std::size_t rangeIdx = 1; rangeIdx < scratchRanges.size();
         ++rangeIdx) {
        const SupportLowcopyRange range = scratchRanges[rangeIdx];
        if (range.begin <= rangeEnd) {
            rangeEnd = std::max(rangeEnd, range.end);
            continue;
        }
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            order[offset] = scratch[offset];
        }
        rangeBegin = range.begin;
        rangeEnd = range.end;
    }

    for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
        order[offset] = scratch[offset];
    }
}

template <typename DigitForIndex>
inline void radixMsdPartitionRangesBatchedLowcopy(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    std::size_t begin, std::size_t end, std::size_t firstDigit,
    std::size_t digitCount, DigitForIndex digitForIndex) {
    std::vector<BatchedLowcopyRange> pending;
    pending.reserve(detail::initial_range_stack_capacity);
    pending.push_back(
        {begin, end, firstDigit, batched_lowcopy_digit_budget, true});

    std::vector<SupportLowcopyRange> scratchOwnedCompletedRanges;
    scratchOwnedCompletedRanges.reserve(detail::initial_range_stack_capacity);

    auto copyScratchToOrder = [&](std::size_t rangeBegin,
                                  std::size_t rangeEnd) {
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            order[offset] = scratch[offset];
        }
    };

    while (!pending.empty()) {
        const BatchedLowcopyRange range = pending.back();
        pending.pop_back();
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t digitIndex = range.digitIndex;
        const std::size_t remainingBudget = range.remainingBudget;
        const bool sourceIsOrder = range.sourceIsOrder;
        const std::size_t rangeSize = rangeEnd - rangeBegin;

        if (rangeSize <= 1 || digitIndex == digitCount) {
            if (!sourceIsOrder) {
                scratchOwnedCompletedRanges.push_back(
                    {rangeBegin, rangeEnd, digitIndex, false});
            }
            continue;
        }

        if (remainingBudget == 0 || rangeSize < batched_lowcopy_min_range) {
            if (!sourceIsOrder) {
                copyScratchToOrder(rangeBegin, rangeEnd);
            }
            auto recordCompletedRange = [](std::size_t, std::size_t) {};
            detail::radixMsdPartitionRanges(
                order, scratch, rangeBegin, rangeEnd, digitIndex, digitCount,
                digitForIndex, recordCompletedRange);
            continue;
        }

        std::array<std::size_t, detail::radix_bucket_count> counts{};
        if (sourceIsOrder) {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                ++counts[digitForIndex(order[offset], digitIndex)];
            }
        } else {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                ++counts[digitForIndex(scratch[offset], digitIndex)];
            }
        }

        const std::size_t nonZeroBuckets =
            detail::countNonZeroBuckets(detail::FullClearCountScratch{counts});
        if (nonZeroBuckets == 0) {
            continue;
        }

        if (nonZeroBuckets == 1) {
            pending.push_back({rangeBegin, rangeEnd, digitIndex + 1,
                               remainingBudget, sourceIsOrder});
            continue;
        }

        std::array<std::size_t, detail::radix_bucket_count> bucketStarts{};
        std::size_t currentOffset = rangeBegin;
        for (std::size_t bucketIdx = 0; bucketIdx < detail::radix_bucket_count;
             ++bucketIdx) {
            bucketStarts[bucketIdx] = currentOffset;
            currentOffset += counts[bucketIdx];
        }

        std::array<std::size_t, detail::radix_bucket_count> bucketOffsets =
            bucketStarts;
        if (sourceIsOrder) {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                const std::size_t nodeIndex = order[offset];
                const uint8_t digit = digitForIndex(nodeIndex, digitIndex);
                scratch[bucketOffsets[digit]++] = nodeIndex;
            }
        } else {
            for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
                const std::size_t nodeIndex = scratch[offset];
                const uint8_t digit = digitForIndex(nodeIndex, digitIndex);
                order[bucketOffsets[digit]++] = nodeIndex;
            }
        }

        for (std::size_t reverseBucketIdx = 0;
             reverseBucketIdx < detail::radix_bucket_count;
             ++reverseBucketIdx) {
            const std::size_t bucketIdx =
                detail::radix_bucket_count - 1 - reverseBucketIdx;
            if (counts[bucketIdx] > 0) {
                pending.push_back({bucketStarts[bucketIdx],
                                   bucketOffsets[bucketIdx], digitIndex + 1,
                                   remainingBudget - 1, !sourceIsOrder});
            }
        }
    }

    coalescedCopyScratchRangesToOrder(order, scratch,
                                      scratchOwnedCompletedRanges);
}

template <typename Partitioner>
inline std::vector<Node> sortForestByCompositeDepth2MsdWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    Partitioner partitioner) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128<2>(nodes, parentIndex);

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    const CompositeDepth2UInt128Key key{nodes, depths};
    auto digitForIndex = [&](std::size_t nodeIndex, std::size_t digitIndex) {
        return key.byte_msb_first(nodeIndex, digitIndex);
    };
    partitioner(order, scratch, digitForIndex);

    return materializeOrder(nodes, order);
}

// Benchmark wrapper for Composite MSD with full copyback after each scatter.
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison.
inline std::vector<Node> sortForestByCompositeDepth2MsdCopybackWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto partitioner = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const auto &digitForIndex) {
        auto recordCompletedRange = [](std::size_t, std::size_t) {};
        detail::radixMsdPartitionRanges(order, scratch, 0, order.size(), 0,
                                        CompositeDepth2UInt128Key::byte_count,
                                        digitForIndex, recordCompletedRange);
    };
    return sortForestByCompositeDepth2MsdWithParent(nodes, parentIndex,
                                                    partitioner);
}

// Benchmark wrapper for the previous branchy low-copy implementation. It keeps
// source selection inside the count/scatter loops to provide direct A/B data
// against the copyback default and the flattened low-copy contender.
inline std::vector<Node> sortForestByCompositeDepth2MsdLowcopyBranchyWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto partitioner = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const auto &digitForIndex) {
        radixMsdPartitionRangesBranchyLowcopy(
            order, scratch, 0, order.size(), 0,
            CompositeDepth2UInt128Key::byte_count, digitForIndex);
    };
    return sortForestByCompositeDepth2MsdWithParent(nodes, parentIndex,
                                                    partitioner);
}

// Benchmark wrapper for the flattened low-copy implementation that was tested
// as a shipped candidate before copyback won the target-workload A/B run.
// Locked to 2-byte depth prefix for apples-to-apples benchmark comparison.
inline std::vector<Node>
sortForestByCompositeDepth2MsdLowcopyFlattenedWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto partitioner = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const auto &digitForIndex) {
        radixMsdPartitionRangesFlattenedLowcopy(
            order, scratch, 0, order.size(), 0,
            CompositeDepth2UInt128Key::byte_count, digitForIndex);
    };
    return sortForestByCompositeDepth2MsdWithParent(nodes, parentIndex,
                                                    partitioner);
}

// Benchmark wrapper for depth-limited low-copy. It keeps ownership state only
// for large ranges and then falls back to the measured copyback default.
inline std::vector<Node> sortForestByCompositeDepth2MsdLowcopyBatchedWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto partitioner = [](std::vector<std::size_t> &order,
                          std::vector<std::size_t> &scratch,
                          const auto &digitForIndex) {
        radixMsdPartitionRangesBatchedLowcopy(
            order, scratch, 0, order.size(), 0,
            CompositeDepth2UInt128Key::byte_count, digitForIndex);
    };
    return sortForestByCompositeDepth2MsdWithParent(nodes, parentIndex,
                                                    partitioner);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
