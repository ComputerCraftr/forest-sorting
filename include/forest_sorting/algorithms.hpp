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
std::vector<std::size_t>
sortedOrderByDepthAndIdWithDepths(const Nodes &nodes, const Traits &traits,
                                  const std::vector<uint32_t> &depths) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    const std::size_t nodeCount = nodes.size();
    if (depths.size() != nodeCount) {
        throw std::runtime_error("depth vector size does not match nodes");
    }

    uint32_t observedMaxDepth = 0;
    for (uint32_t nodeDepth : depths) {
        if (nodeDepth > detail::maxDepthForPrefix<DepthPrefixBytes>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        observedMaxDepth = std::max(observedMaxDepth, nodeDepth);
    }

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    if (nodeCount <= 1) {
        return order;
    }

    std::vector<std::size_t> scratch(nodeCount);
    detail::sortOrderByDepthAndId<DepthPrefixBytes>(
        order, scratch, nodes, traits, depths, observedMaxDepth);
    return order;
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
    requires ForestTraits<Traits,
                          std::decay_t<decltype(std::declval<Nodes>()[0])>>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    const auto parentIndex = detail::buildParentIndex(nodes, traits);
    const auto depths = detail::computeDepths(nodes, parentIndex, traits);
    return sortedOrderByDepthAndIdWithDepths<DepthPrefixBytes>(nodes, traits,
                                                               depths);
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    const auto parentIndex = detail::buildParentIndex(nodes, traits);
    const auto depths = detail::computeDepths(nodes, parentIndex, traits);

    uint32_t observedMaxDepth = 0;
    for (uint32_t nodeDepth : depths) {
        observedMaxDepth = std::max(observedMaxDepth, nodeDepth);
    }

    if (observedMaxDepth <= detail::maxDepthForPrefix<1>()) {
        return sortedOrderByDepthAndIdWithDepths<1>(nodes, traits, depths);
    }
    if (observedMaxDepth <= detail::maxDepthForPrefix<2>()) {
        return sortedOrderByDepthAndIdWithDepths<2>(nodes, traits, depths);
    }
    if (observedMaxDepth <= detail::maxDepthForPrefix<3>()) {
        return sortedOrderByDepthAndIdWithDepths<3>(nodes, traits, depths);
    }
    return sortedOrderByDepthAndIdWithDepths<4>(nodes, traits, depths);
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
    using Node = std::decay_t<decltype(nodes[0])>;
    const auto order = sortedOrderByDepthAndId(nodes, traits);

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }
    return sorted;
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    auto sorted = sortedCopyByDepthAndId<DepthPrefixBytes>(nodes, traits);
    std::move(sorted.begin(), sorted.end(), nodes.begin());
}

template <typename Nodes, typename Traits>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    auto sorted = sortedCopyByDepthAndId(nodes, traits);
    std::move(sorted.begin(), sorted.end(), nodes.begin());
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    std::vector<std::size_t> parentIndex;
    try {
        parentIndex = detail::buildParentIndex(nodes, traits);
    } catch (const std::runtime_error &) {
        return false;
    }

    return detail::verifyWithParentIndex<DepthPrefixBytes>(nodes, parentIndex,
                                                           traits);
}

template <typename Nodes, typename Traits>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    return verifySortedByDepthAndId<4>(nodes, traits);
}

} // namespace forest_sorting

#endif // FOREST_SORTING_ALGORITHMS_HPP
