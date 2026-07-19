#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ADAPTIVE_SORT_VARIANTS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ADAPTIVE_SORT_VARIANTS_HPP

#include "forest_sorting/benchmark_support/full/order_materialization.hpp"
#include "forest_sorting/benchmark_support/full/radix_ladder_variants.hpp"
#include "forest_sorting/benchmark_support/tail/tail_sort_variants.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/order.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace forest_sorting::benchmark_support {

template <std::size_t DepthPrefixBytes,
          detail::DepthRadixCountPolicy DepthPolicy =
              detail::ProductionDepthCountPolicy,
          typename RangeSorter>
inline std::vector<Node> sortForestByAdaptiveRangeSorterWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    bool allowDenseDepthGrouping, RangeSorter rangeSorter) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    auto computed = detail::computeDepths<DepthPrefixBytes>(
        nodes, parentIndex, UInt128NodeTraits{});
    const auto &depths = computed.values;
    const uint32_t observedMaxDepth = computed.observedMax;

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    using Depth = detail::DepthValue<DepthPrefixBytes>;
    std::vector<detail::DepthRange<Depth>> depthRanges;
    depthRanges.reserve(detail::initial_range_stack_capacity);
    std::vector<std::size_t> depthStarts;
    std::vector<std::size_t> depthOffsets;
    if (allowDenseDepthGrouping &&
        detail::shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        detail::groupOrderByDepthDense(order, scratch, depths,
                                       static_cast<Depth>(observedMaxDepth),
                                       depthRanges, depthStarts, depthOffsets);
    } else {
        detail::groupOrderByDepthMsd<DepthPrefixBytes, DepthPolicy>(
            order, scratch, depths, depthRanges);
    }

    rangeSorter(order, nodes, depthRanges);

    return materializeOrder(nodes, order);
}

template <std::size_t RadixChunkBytes,
          detail::IdRadixCountPolicy IdPolicy = FullClearIdCountPolicy,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter, typename Depth>
inline void sortDepthRangesByIdMsdChunks(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges,
    SmallRangeSorter smallRangeSorter) {
    std::size_t maxRadixRangeSize = 0;
    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize > SmallThreshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    detail::IdMsdChunkSortWorkspace<RadixChunkBytes, IdPolicy> workspace;
    workspace.allocate(maxRadixRangeSize);

    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        auto idForIndex = [&](std::size_t itemIndex) {
            return UInt128NodeTraits::id(nodes[itemIndex]);
        };
        auto adaptedSmallRangeSorter = [&](std::vector<std::size_t> &sortOrder,
                                           auto, const auto &sortTraits,
                                           std::size_t sortBegin,
                                           std::size_t sortEnd) {
            smallRangeSorter(sortOrder, nodes, sortTraits, sortBegin, sortEnd);
        };
        detail::sortIndexRangeByIdMsdChunksWithSmallSorter<
            RadixChunkBytes, IdPolicy, SmallThreshold>(
            order, idForIndex, UInt128NodeTraits{}, rangeBegin, rangeEnd, 0,
            workspace, adaptedSmallRangeSorter);
    }
}

template <typename LadderPolicy, detail::IdRadixCountPolicy IdPolicy,
          typename Depth>
inline void sortDepthRangesByIdMsdLadder(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges) {
    IdMsdLadderWorkspace<IdPolicy> workspace;
    auto idForIndex = [&](std::size_t itemIndex) {
        return UInt128NodeTraits::id(nodes[itemIndex]);
    };
    for (const detail::DepthRange<Depth> &range : depthRanges) {
        sortIndexRangeByIdMsdLadder<LadderPolicy, IdPolicy>(
            order, idForIndex, UInt128NodeTraits{}, range.begin, range.end,
            workspace);
    }
}

template <std::size_t Chunk8Max, std::size_t Chunk16Max,
          detail::IdRadixCountPolicy IdPolicy,
          detail::DepthRadixCountPolicy DepthPolicy =
              detail::ProductionDepthCountPolicy>
inline std::vector<Node> sortForestByDepth2FirstThenIdMsdLadderWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    using Ladder = Chunk8Chunk16Chunk32Ladder<Chunk8Max, Chunk16Max>;
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdLadder<Ladder, IdPolicy>(order, sortNodes,
                                                       depthRanges);
    };
    return sortForestByAdaptiveRangeSorterWithParent<2, DepthPolicy>(
        nodes, parentIndex, true, rangeSorter);
}

template <std::size_t DepthPrefixBytes, std::size_t RadixChunkBytes,
          detail::IdRadixCountPolicy IdPolicy = FullClearIdCountPolicy,
          detail::DepthRadixCountPolicy DepthPolicy =
              detail::ProductionDepthCountPolicy,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter<SmallThreshold>>
inline std::vector<Node> sortForestByAdaptiveIdMsdChunkWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    bool allowDenseDepthGrouping) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdChunks<RadixChunkBytes, IdPolicy, SmallThreshold>(
            order, sortNodes, depthRanges, SmallRangeSorter{});
    };

    return sortForestByAdaptiveRangeSorterWithParent<DepthPrefixBytes,
                                                     DepthPolicy>(
        nodes, parentIndex, allowDenseDepthGrouping, rangeSorter);
}

inline void validateIdPermutation(const std::vector<Node> &nodes,
                                  const std::vector<std::size_t> &permutation) {
    if (permutation.size() != nodes.size()) {
        throw std::runtime_error("ID permutation size must match node count");
    }
    std::vector<bool> seen(nodes.size(), false);
    const UInt128NodeTraits traits;
    for (std::size_t offset = 0; offset < permutation.size(); ++offset) {
        const std::size_t nodeIndex = permutation[offset];
        if (nodeIndex >= nodes.size() || seen[nodeIndex]) {
            throw std::runtime_error("invalid ID permutation");
        }
        seen[nodeIndex] = true;
        if (offset != 0) {
            const std::size_t previousIndex = permutation[offset - 1];
            if (!detail::idLess(nodes[previousIndex].id, nodes[nodeIndex].id,
                                traits)) {
                throw std::runtime_error("ID permutation is not canonical");
            }
        }
    }
}

inline std::vector<Node> sortForestByTrustedGlobalIdPermutationThenDepthStable(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    const std::vector<std::size_t> *idPermutation) {
    auto computed =
        detail::computeDepths<2>(nodes, parentIndex, UInt128NodeTraits{});
    std::vector<std::size_t> order;
    if (idPermutation != nullptr) {
        order = *idPermutation;
    } else {
        order = detail::makeValidatedGlobalIdPermutation(nodes,
                                                         UInt128NodeTraits{});
    }
    std::vector<std::size_t> scratch(order.size());
    detail::stableGroupOrderByDepth<2, detail::ProductionDepthCountPolicy>(
        order, scratch, computed.values, computed.observedMax);
    return materializeOrder(nodes, order);
}

inline std::vector<Node> sortForestByGlobalIdPermutationThenDepthStable(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    const std::vector<std::size_t> *idPermutation) {
    if (idPermutation != nullptr) {
        validateIdPermutation(nodes, *idPermutation);
    }
    return sortForestByTrustedGlobalIdPermutationThenDepthStable(
        nodes, parentIndex, idPermutation);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512NoDenseWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4,
        TouchedIdCountPolicy<detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, false);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4,
        TouchedIdCountPolicy<detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 4,
                                                    FullClearIdCountPolicy>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk8FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 1,
                                                    FullClearIdCountPolicy>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk8BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 1,
        TouchedIdCountPolicy<detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk16FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 2,
                                                    FullClearIdCountPolicy>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk16BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 2,
        TouchedIdCountPolicy<detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk64FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 8>(nodes, parentIndex,
                                                          true);
}

inline std::vector<Node>
sortForestByDepth4FirstThenIdMsdChunk32FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<4, 4>(nodes, parentIndex,
                                                          true);
}

template <std::size_t BitmaskMaxRangeSize>
inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLeWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdChunks<4,
                                     TouchedIdCountPolicy<BitmaskMaxRangeSize>,
                                     detail::small_id_range_sort_threshold>(
            order, sortNodes, depthRanges, LinearSmallSorter<>{});
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ADAPTIVE_SORT_VARIANTS_HPP
