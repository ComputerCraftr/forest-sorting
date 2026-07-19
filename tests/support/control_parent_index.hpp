#ifndef FOREST_SORTING_TEST_SUPPORT_CONTROL_PARENT_INDEX_HPP
#define FOREST_SORTING_TEST_SUPPORT_CONTROL_PARENT_INDEX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace forest_sorting::test_support {

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexControl(const Nodes &nodes,
                                                 const Traits &traits) {
    const std::size_t nodeCount = static_cast<std::size_t>(nodes.size());
    for (std::size_t lhs = 0; lhs < nodeCount; ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < nodeCount; ++rhs) {
            if (detail::idEqual(traits.id(nodes[lhs]), traits.id(nodes[rhs]),
                                traits)) {
                throw std::runtime_error("duplicate node id");
            }
        }
    }

    std::vector<std::size_t> parentIndex(nodeCount, detail::no_parent);
    for (std::size_t child = 0; child < nodeCount; ++child) {
        const auto parentId = traits.parent_id(nodes[child]);
        if (detail::isParentSentinel(traits, parentId)) {
            continue;
        }
        for (std::size_t candidate = 0; candidate < nodeCount; ++candidate) {
            if (detail::idEqual(traits.id(nodes[candidate]), parentId,
                                traits)) {
                parentIndex[child] = candidate;
                break;
            }
        }
    }
    return parentIndex;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_TEST_SUPPORT_CONTROL_PARENT_INDEX_HPP
