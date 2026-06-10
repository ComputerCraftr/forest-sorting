#ifndef FOREST_SORTING_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_PARENT_INDEX_BASELINES_HPP

#include "forest.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <vector>

inline std::vector<std::size_t>
buildParentIndexStdUnorderedMap(const std::vector<Node> &nodes) {
    std::unordered_map<UInt128, std::size_t, UInt128Hash> idToIndex;
    idToIndex.reserve(nodes.size() * 2);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto inserted = idToIndex.emplace(nodes[i].id, i);
        if (!inserted.second) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const UInt128 parentId = nodes[i].parentId;
        if (parentId == 0) {
            continue;
        }
        const auto parentIt = idToIndex.find(parentId);
        if (parentIt != idToIndex.end()) {
            parent[i] = parentIt->second;
        }
    }

    return parent;
}

#endif // FOREST_SORTING_PARENT_INDEX_BASELINES_HPP
