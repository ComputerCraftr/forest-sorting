#ifndef FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
#define FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
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

    std::size_t maxRadixRangeSize = 0;
    for (const DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize > small_id_range_sort_threshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    IdChunkSortWorkspace<production_id_chunk_bytes, ProductionIdCountPolicy>
        idWorkspace;
    idWorkspace.allocate(maxRadixRangeSize);
    auto idForIndex = [&](std::size_t nodeIndex) {
        return traits.id(nodes[nodeIndex]);
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
        sortIndexRangeByIdChunks<production_id_chunk_bytes,
                                 ProductionIdCountPolicy>(
            order, idForIndex, traits, rangeBegin, rangeEnd, 0, idWorkspace);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ADAPTIVE_SORT_HPP
