#ifndef FOREST_SORTING_ALGORITHMS_HPP
#define FOREST_SORTING_ALGORITHMS_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/traits.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting {

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
    requires ForestTraits<Traits,
                          std::decay_t<decltype(std::declval<Nodes>()[0])>>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    const std::size_t nodeCount = nodes.size();
    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    if (nodeCount <= 1) {
        return order;
    }

    const auto parentIndex =
        detail::buildParentIndexControlByteFlatHash(nodes, traits);
    const auto depth = detail::computeDepths(nodes, parentIndex, traits);

    uint32_t maxDepth = 0;
    for (uint32_t nodeDepth : depth) {
        if (nodeDepth > detail::maxDepthForPrefix<DepthPrefixBytes>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        maxDepth = std::max(maxDepth, nodeDepth);
    }

    std::vector<std::size_t> scratch(nodeCount);
    detail::sortOrderByDepthAndId(order, scratch, nodes, traits, depth,
                                  maxDepth);
    return order;
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    return sortedOrderByDepthAndId<2>(nodes, traits);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
auto sortedCopyByDepthAndId(const Nodes &nodes, const Traits &traits) {
    using Node = std::decay_t<decltype(nodes[0])>;
    const auto order = sortedOrderByDepthAndId<DepthPrefixBytes>(nodes, traits);

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }
    return sorted;
}

template <typename Nodes, typename Traits>
auto sortedCopyByDepthAndId(const Nodes &nodes, const Traits &traits) {
    return sortedCopyByDepthAndId<2>(nodes, traits);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    auto sorted = sortedCopyByDepthAndId<DepthPrefixBytes>(nodes, traits);
    std::move(sorted.begin(), sorted.end(), nodes.begin());
}

template <typename Nodes, typename Traits>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    sortInPlaceByDepthAndId<2>(nodes, traits);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    std::vector<std::size_t> parentIndex;
    try {
        parentIndex =
            detail::buildParentIndexControlByteFlatHash(nodes, traits);
    } catch (const std::runtime_error &) {
        return false;
    }

    return detail::verifyWithParentIndex<DepthPrefixBytes>(nodes, parentIndex,
                                                           traits);
}

template <typename Nodes, typename Traits>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    return verifySortedByDepthAndId<2>(nodes, traits);
}

} // namespace forest_sorting

#endif // FOREST_SORTING_ALGORITHMS_HPP
