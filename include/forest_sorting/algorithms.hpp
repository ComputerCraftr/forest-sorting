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

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits,
          std::unsigned_integral Depth>
    requires ForestTraits<Traits,
                          std::decay_t<decltype(std::declval<Nodes>()[0])>> &&
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

    const uint32_t observedMaxDepth =
        detail::validateAndFindObservedMaxDepth<DepthPrefixBytes>(depths);
    return detail::sortedOrderByDepthAndIdWithDepthsChecked<DepthPrefixBytes>(
        nodes, traits, depths, observedMaxDepth);
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
    requires ForestTraits<Traits,
                          std::decay_t<decltype(std::declval<Nodes>()[0])>>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
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
