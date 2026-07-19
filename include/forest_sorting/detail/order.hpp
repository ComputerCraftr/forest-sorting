#ifndef FOREST_SORTING_DETAIL_ORDER_HPP
#define FOREST_SORTING_DETAIL_ORDER_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/validation.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace forest_sorting::detail {

template <typename Nodes, typename Traits>
std::vector<std::size_t>
makeValidatedGlobalIdPermutation(const Nodes &nodes, const Traits &traits) {
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);
    if (order.size() <= 1) {
        return order;
    }

    IdMsdChunkSortWorkspace<production_id_radix_chunk_bytes,
                            ProductionIdCountPolicy>
        workspace;
    auto idForIndex = [&](std::size_t nodeIndex) {
        return traits.id(nodes[nodeIndex]);
    };
    sortIndexRangeByIdMsdChunks<production_id_radix_chunk_bytes,
                                ProductionIdCountPolicy>(
        order, idForIndex, traits, 0, order.size(), 0, workspace);
    rejectAdjacentDuplicates(
        order,
        [&](std::size_t lhsIndex, std::size_t rhsIndex) {
            return idEqual(traits.id(nodes[lhsIndex]),
                           traits.id(nodes[rhsIndex]), traits);
        },
        "duplicate node id");
    return order;
}

template <std::size_t DepthPrefixBytes, typename Depth>
std::vector<std::size_t>
stableDepthGroupTrustedIdPermutation(std::vector<std::size_t> order,
                                     const std::vector<Depth> &depths,
                                     uint32_t observedMaxDepth) {
    assert(order.size() == depths.size());
    if (order.size() <= 1) {
        return order;
    }

    std::vector<std::size_t> scratch(order.size());
    stableGroupOrderByDepth<DepthPrefixBytes, ProductionDepthCountPolicy>(
        order, scratch, depths, static_cast<Depth>(observedMaxDepth));
    return order;
}

// Callers must derive observedMaxDepth from depths after validating the depth
// vector size and the selected prefix width.
template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits,
          std::unsigned_integral Depth>
std::vector<std::size_t> sortedOrderByDepthAndIdWithValidatedDepths(
    const Nodes &nodes, const Traits &traits, const std::vector<Depth> &depths,
    uint32_t observedMaxDepth) {
    return stableDepthGroupTrustedIdPermutation<DepthPrefixBytes>(
        makeValidatedGlobalIdPermutation(nodes, traits), depths,
        observedMaxDepth);
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ORDER_HPP
