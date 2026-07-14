#ifndef FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP

#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
inline std::vector<Node> sortForestByDenseDepth2BucketsThenIdLsdWithParent(
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
inline std::vector<Node> sortForestByCompositeDepth2IdLsdWithParent(
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

inline constexpr std::size_t dense_bucket_id_msd_radix_chunk_bytes = 8;

// Benchmark-only baseline
// Dense vector-of-buckets by depth
// Fixed 2-byte depth prefix limit
// No sparse-depth MSD fallback
// Unsafe without the validation guard for very large observed depths.
inline std::vector<Node>
sortForestByDenseDepth2BucketsThenIdMsdChunk64FullClearWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto sortBuckets = [&](std::vector<std::vector<std::size_t>> &buckets,
                           std::size_t maxBucketSize) {
        detail::IdMsdChunkSortWorkspace<dense_bucket_id_msd_radix_chunk_bytes,
                                        detail::FullClearCounts>
            workspace;
        workspace.allocate(maxBucketSize);
        const UInt128NodeTraits traits;
        auto idForIndex = [&](std::size_t nodeIndex) {
            return UInt128NodeTraits::id(nodes[nodeIndex]);
        };

        for (auto &bucket : buckets) {
            detail::sortIndexRangeByIdMsdChunks<
                dense_bucket_id_msd_radix_chunk_bytes, detail::FullClearCounts>(
                bucket, idForIndex, traits, 0, bucket.size(), 0, workspace);
        }
    };

    return sortForestByDenseDepth2BucketsWithParent(nodes, parentIndex,
                                                    sortBuckets);
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
inline std::vector<Node> sortForestByCompositeDepth2IdByteMsdCopybackWithParent(
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

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_BASELINES_HPP
