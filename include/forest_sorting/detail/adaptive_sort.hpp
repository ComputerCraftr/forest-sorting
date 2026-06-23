#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include <cassert>
#include <cstddef>
#include <vector>

namespace forest_sorting::detail {

// -----------------------------------------------------------------------------
// Adaptive sort tuning
// -----------------------------------------------------------------------------

// Hard resource cap for dense depth histograms. Structurally valid depths are
// bounded by node count, but very large valid forests still use depth MSD to
// avoid oversized histogram allocation and scanning.
inline constexpr std::size_t max_dense_depth_buckets = std::size_t{1} << 20;

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

template <typename Depth, typename RangeConsumer>
inline void groupOrderByDepthDenseWithConsumer(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    const std::vector<Depth> &depths, Depth observedMaxDepth,
    std::vector<std::size_t> &depthStarts,
    std::vector<std::size_t> &depthOffsets, RangeConsumer rangeConsumer) {
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

    for (std::size_t depthIdx = 0;
         depthIdx <= static_cast<std::size_t>(observedMaxDepth); ++depthIdx) {
        const std::size_t rangeBegin = depthStarts[depthIdx];
        const std::size_t rangeEnd = depthStarts[depthIdx + 1];
        if (rangeBegin != rangeEnd) {
            rangeConsumer(static_cast<Depth>(depthIdx), rangeBegin, rangeEnd);
        }
    }
}

template <typename Depth>
inline void groupOrderByDepthDense(std::vector<std::size_t> &order,
                                   std::vector<std::size_t> &scratch,
                                   const std::vector<Depth> &depths,
                                   Depth observedMaxDepth,
                                   std::vector<DepthRange<Depth>> &ranges,
                                   std::vector<std::size_t> &depthStarts,
                                   std::vector<std::size_t> &depthOffsets) {
    ranges.clear();
    ranges.reserve(initial_range_stack_capacity);
    auto collectRange = [&](Depth depth, std::size_t begin, std::size_t end) {
        ranges.push_back({depth, begin, end});
    };
    groupOrderByDepthDenseWithConsumer(order, scratch, depths, observedMaxDepth,
                                       depthStarts, depthOffsets, collectRange);
}

template <std::size_t DepthPrefixBytes, typename CountPolicy = FullClearCounts,
          typename Depth, typename RangeConsumer>
inline void groupOrderByDepthMsdWithConsumer(std::vector<std::size_t> &order,
                                             std::vector<std::size_t> &scratch,
                                             const std::vector<Depth> &depths,
                                             RangeConsumer rangeConsumer) {
    auto digitForIndex = [&](std::size_t nodeIdx, std::size_t digitIndex) {
        return depthByteMsbFirst<DepthPrefixBytes>(depths[nodeIdx], digitIndex);
    };

    auto rangeDone = [&](std::size_t rangeBegin, std::size_t rangeEnd) {
        if (rangeEnd > rangeBegin) {
            rangeConsumer(depths[order[rangeBegin]], rangeBegin, rangeEnd);
        }
    };

    radixMsdPartitionRanges<CountPolicy>(order, scratch, 0, order.size(), 0,
                                         DepthPrefixBytes, digitForIndex,
                                         rangeDone);
}

template <std::size_t DepthPrefixBytes, typename CountPolicy = FullClearCounts,
          typename Depth>
inline void groupOrderByDepthMsd(std::vector<std::size_t> &order,
                                 std::vector<std::size_t> &scratch,
                                 const std::vector<Depth> &depths,
                                 std::vector<DepthRange<Depth>> &ranges) {
    ranges.clear();
    ranges.reserve(initial_range_stack_capacity);
    auto collectRange = [&](Depth depth, std::size_t begin, std::size_t end) {
        ranges.push_back({depth, begin, end});
    };
    groupOrderByDepthMsdWithConsumer<DepthPrefixBytes, CountPolicy>(
        order, scratch, depths, collectRange);
}

template <std::size_t DepthPrefixBytes,
          typename CountPolicy = ProductionIdCountPolicy, typename Depth>
inline void stableGroupOrderByDepth(std::vector<std::size_t> &order,
                                    std::vector<std::size_t> &scratch,
                                    const std::vector<Depth> &depths,
                                    Depth observedMaxDepth) {
    auto ignoreRange = [](Depth, std::size_t, std::size_t) {};
    if (shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        std::vector<std::size_t> depthStarts;
        std::vector<std::size_t> depthOffsets;
        groupOrderByDepthDenseWithConsumer(order, scratch, depths,
                                           observedMaxDepth, depthStarts,
                                           depthOffsets, ignoreRange);
    } else {
        groupOrderByDepthMsdWithConsumer<DepthPrefixBytes, CountPolicy>(
            order, scratch, depths, ignoreRange);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
