#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ORDER_MATERIALIZATION_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ORDER_MATERIALIZATION_HPP

#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <vector>

namespace forest_sorting::benchmark_support {

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

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_ORDER_MATERIALIZATION_HPP
