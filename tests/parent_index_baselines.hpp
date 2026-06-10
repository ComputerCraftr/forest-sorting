#ifndef FOREST_SORTING_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_PARENT_INDEX_BASELINES_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <vector>

struct UInt128BaselineHash {
    std::size_t operator()(forest_sorting::UInt128 value) const noexcept {
        return forest_sorting::UInt128Traits::hash(value);
    }
};

inline std::vector<std::size_t> buildParentIndexStdUnorderedMap(
    const std::vector<forest_sorting::Node> &nodes) {
    std::unordered_map<forest_sorting::UInt128, std::size_t,
                       UInt128BaselineHash>
        idToIndex;
    idToIndex.reserve(nodes.size() * 2);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const auto inserted = idToIndex.emplace(nodes[nodeIdx].id, nodeIdx);
        if (!inserted.second) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(),
                                    forest_sorting::detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const forest_sorting::UInt128 parentId = nodes[nodeIdx].parentId;
        if (parentId == 0) {
            continue;
        }
        const auto parentIt = idToIndex.find(parentId);
        if (parentIt != idToIndex.end()) {
            parent[nodeIdx] = parentIt->second;
        }
    }

    return parent;
}

#endif // FOREST_SORTING_PARENT_INDEX_BASELINES_HPP
