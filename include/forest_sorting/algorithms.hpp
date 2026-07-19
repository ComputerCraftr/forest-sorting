#ifndef FOREST_SORTING_ALGORITHMS_HPP
#define FOREST_SORTING_ALGORITHMS_HPP

#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/order.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/traits.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace forest_sorting {

namespace detail {

template <IndexedNodeInput Nodes> using ForestSortingNode = IndexedValue<Nodes>;

} // namespace detail

template <IndexedNodeInput Nodes, typename Traits, IndexedDepthInput Depths>
    requires ForestTraits<Traits, detail::ForestSortingNode<Nodes>>
std::vector<std::size_t>
sortedOrderByDepthAndIdWithDepths(const Nodes &nodes, const Traits &traits,
                                  const Depths &depths) {
    const std::size_t nodeCount = static_cast<std::size_t>(nodes.size());
    const uint32_t observedMaxDepth =
        detail::validatePrecomputedDepthInput(nodeCount, depths);
    const std::size_t depthPrefixBytes =
        detail::depthPrefixBytesForMax(observedMaxDepth);
    if (nodeCount == 0) {
        return {};
    }

    switch (depthPrefixBytes) {
    case 1:
        return detail::sortedOrderByDepthAndIdWithValidatedDepths<1>(
            nodes, traits, detail::narrowIndexedDepths<1>(depths),
            observedMaxDepth);
    case 2:
        return detail::sortedOrderByDepthAndIdWithValidatedDepths<2>(
            nodes, traits, detail::narrowIndexedDepths<2>(depths),
            observedMaxDepth);
    case 3:
        return detail::sortedOrderByDepthAndIdWithValidatedDepths<3>(
            nodes, traits, detail::narrowIndexedDepths<3>(depths),
            observedMaxDepth);
    case 4:
        return detail::sortedOrderByDepthAndIdWithValidatedDepths<4>(
            nodes, traits, detail::narrowIndexedDepths<4>(depths),
            observedMaxDepth);
    default:
        throw std::logic_error("invalid internal depth prefix width");
    }
}

template <IndexedNodeInput Nodes, typename Traits>
    requires ForestTraits<Traits, detail::ForestSortingNode<Nodes>>
std::vector<std::size_t> sortedOrderByDepthAndId(const Nodes &nodes,
                                                 const Traits &traits) {
    if (nodes.size() == 0) {
        return {};
    }
    auto parentResult = detail::buildParentIndexRadixJoinResult(nodes, traits);
    auto computed =
        detail::computeDepths<4>(nodes, parentResult.parentIndex, traits);
    const uint32_t observedMaxDepth = computed.observedMax;
    const std::size_t depthPrefixBytes =
        detail::depthPrefixBytesForMax(observedMaxDepth);

    switch (depthPrefixBytes) {
    case 1: {
        auto depths = detail::narrowDepths<1>(computed.values);
        return detail::stableDepthGroupTrustedIdPermutation<1>(
            std::move(parentResult.idPermutation), depths, observedMaxDepth);
    }
    case 2: {
        auto depths = detail::narrowDepths<2>(computed.values);
        return detail::stableDepthGroupTrustedIdPermutation<2>(
            std::move(parentResult.idPermutation), depths, observedMaxDepth);
    }
    case 3:
        return detail::stableDepthGroupTrustedIdPermutation<3>(
            std::move(parentResult.idPermutation), computed.values,
            observedMaxDepth);
    case 4:
        return detail::stableDepthGroupTrustedIdPermutation<4>(
            std::move(parentResult.idPermutation), computed.values,
            observedMaxDepth);
    default:
        throw std::logic_error("invalid internal depth prefix width");
    }
}

template <CopyableNodeInput Nodes, typename Traits>
    requires ForestTraits<Traits, detail::ForestSortingNode<Nodes>>
auto sortedCopyByDepthAndId(const Nodes &nodes, const Traits &traits) {
    using Node = detail::ForestSortingNode<Nodes>;
    if (nodes.size() == 0) {
        return std::vector<Node>{};
    }
    const auto order = sortedOrderByDepthAndId(nodes, traits);

    std::vector<Node> sorted;
    sorted.reserve(static_cast<std::size_t>(nodes.size()));
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }
    return sorted;
}

template <MutableNodeInput Nodes, typename Traits>
    requires ForestTraits<Traits, detail::ForestSortingNode<Nodes>>
void sortInPlaceByDepthAndId(Nodes &nodes, const Traits &traits) {
    if (nodes.size() <= 1) {
        return;
    }
    const std::vector<std::size_t> order =
        sortedOrderByDepthAndId(nodes, traits);
    std::vector<std::size_t> destination(order.size());
    for (std::size_t outputIndex = 0; outputIndex < order.size();
         ++outputIndex) {
        destination[order[outputIndex]] = outputIndex;
    }
    for (std::size_t index = 0; index < destination.size(); ++index) {
        while (destination[index] != index) {
            const std::size_t target = destination[index];
            std::swap(nodes[index], nodes[target]);
            std::swap(destination[index], destination[target]);
        }
    }
}

template <IndexedNodeInput Nodes, typename Traits>
    requires ForestTraits<Traits, detail::ForestSortingNode<Nodes>>
bool verifySortedByDepthAndId(const Nodes &nodes, const Traits &traits) {
    if (nodes.size() == 0) {
        return true;
    }
    std::vector<std::size_t> parentIndex;
    try {
        parentIndex = detail::buildParentIndex(nodes, traits);
    } catch (const std::runtime_error &) {
        return false;
    }
    return detail::verifyWithParentIndex<4>(nodes, parentIndex, traits);
}

} // namespace forest_sorting

#endif // FOREST_SORTING_ALGORITHMS_HPP
