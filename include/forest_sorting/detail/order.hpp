#ifndef FOREST_SORTING_DETAIL_ORDER_HPP
#define FOREST_SORTING_DETAIL_ORDER_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace forest_sorting::detail {

// Callers must derive observedMaxDepth from depths after validating the depth
// vector size and the selected prefix width.
template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits,
          std::unsigned_integral Depth>
std::vector<std::size_t> sortedOrderByDepthAndIdWithDepthsChecked(
    const Nodes &nodes, const Traits &traits, const std::vector<Depth> &depths,
    uint32_t observedMaxDepth) {
    const std::size_t nodeCount = nodes.size();
    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    if (nodeCount <= 1) {
        return order;
    }

    std::vector<std::size_t> scratch(nodeCount);
    sortOrderByDepthAndId<DepthPrefixBytes>(order, scratch, nodes, traits,
                                            depths, observedMaxDepth);
    return order;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ORDER_HPP
