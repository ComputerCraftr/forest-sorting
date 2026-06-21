#ifndef FOREST_SORTING_ALGORITHMS_HPP
#define FOREST_SORTING_ALGORITHMS_HPP

#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/order.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/traits.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting {

template <typename Nodes>
using ForestSortingNode =
    std::decay_t<decltype(*std::begin(std::declval<const Nodes &>()))>;

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits,
          std::unsigned_integral Depth>
    requires ForestTraits<Traits, ForestSortingNode<Nodes>> &&
             (!std::same_as<Depth, bool>) && (sizeof(Depth) >= DepthPrefixBytes)
std::vector<std::size_t>
sortedOrderByDepthAndIdWithDepths(const Nodes &nodes, const Traits &traits,
                                  const std::vector<Depth> &depths) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    const std::size_t nodeCount = nodes.size();
    if (depths.size() != nodeCount) {
        throw std::runtime_error("depth vector size does not match nodes");
    }
    if (nodeCount == 0) {
        return {};
    }

    const uint32_t observedMaxDepth =
        detail::validateAndFindObservedMaxDepth<DepthPrefixBytes>(depths);
    return detail::sortedOrderByDepthAndIdWithDepthsChecked<DepthPrefixBytes>(
        nodes, traits, depths, observedMaxDepth);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
    requires ForestTraits<Traits, ForestSortingNode<Nodes>>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    if (nodes.size() == 0) {
        return {};
    }
    const auto parentIndex = detail::buildParentIndex(nodes, traits);
    auto computed =
        detail::computeDepths<DepthPrefixBytes>(nodes, parentIndex, traits);
    return detail::sortedOrderByDepthAndIdWithDepthsChecked<DepthPrefixBytes>(
        nodes, traits, computed.values,
        static_cast<uint32_t>(computed.observedMax));
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    if (nodes.size() == 0) {
        return {};
    }
    const auto parentIndex = detail::buildParentIndex(nodes, traits);
    auto computed = detail::computeDepths<4>(nodes, parentIndex, traits);
    const uint32_t observedMaxDepth = computed.observedMax;

    if (observedMaxDepth <= detail::maxDepthForPrefix<1>()) {
        auto depths = detail::narrowDepths<1>(computed.values);
        return detail::sortedOrderByDepthAndIdWithDepthsChecked<1>(
            nodes, traits, depths, observedMaxDepth);
    }
    if (observedMaxDepth <= detail::maxDepthForPrefix<2>()) {
        auto depths = detail::narrowDepths<2>(computed.values);
        return detail::sortedOrderByDepthAndIdWithDepthsChecked<2>(
            nodes, traits, depths, observedMaxDepth);
    }
    if (observedMaxDepth <= detail::maxDepthForPrefix<3>()) {
        return detail::sortedOrderByDepthAndIdWithDepthsChecked<3>(
            nodes, traits, computed.values, observedMaxDepth);
    }
    return detail::sortedOrderByDepthAndIdWithDepthsChecked<4>(
        nodes, traits, computed.values, observedMaxDepth);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
auto sortedCopyByDepthAndId(const Nodes &nodes, const Traits &traits) {
    using Node = ForestSortingNode<Nodes>;
    if (nodes.size() == 0) {
        return std::vector<Node>{};
    }
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
    using Node = ForestSortingNode<Nodes>;
    if (nodes.size() == 0) {
        return std::vector<Node>{};
    }
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
    if (nodes.size() == 0) {
        return;
    }
    auto sorted = sortedCopyByDepthAndId<DepthPrefixBytes>(nodes, traits);
    std::move(sorted.begin(), sorted.end(), nodes.begin());
}

template <typename Nodes, typename Traits>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    if (nodes.size() == 0) {
        return;
    }
    auto sorted = sortedCopyByDepthAndId(nodes, traits);
    std::move(sorted.begin(), sorted.end(), nodes.begin());
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    if (nodes.size() == 0) {
        return true;
    }
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
    if (nodes.size() == 0) {
        return true;
    }
    return verifySortedByDepthAndId<4>(nodes, traits);
}

} // namespace forest_sorting

#endif // FOREST_SORTING_ALGORITHMS_HPP
