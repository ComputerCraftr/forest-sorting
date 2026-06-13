#ifndef FOREST_SORTING_DETAIL_DEPTH_HPP
#define FOREST_SORTING_DETAIL_DEPTH_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::detail {

template <std::size_t DepthPrefixBytes> constexpr uint32_t maxDepthForPrefix() {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    if constexpr (DepthPrefixBytes == 1) {
        return 0xFFU;
    } else if constexpr (DepthPrefixBytes == 2) {
        return 0xFFFFU;
    } else if constexpr (DepthPrefixBytes == 3) {
        return 0xFFFFFFU;
    } else {
        return UINT32_MAX;
    }
}

template <typename Nodes, typename Traits>
std::vector<uint32_t> computeDepths(const Nodes &nodes,
                                    const std::vector<std::size_t> &parent,
                                    const Traits &traits) {
    const std::size_t nodeCount = nodes.size();
    std::vector<uint32_t> depth(nodeCount, UINT32_MAX);

    if (nodeCount == 0) {
        return depth;
    }

    std::vector<std::size_t> stack;
    stack.reserve(initial_range_stack_capacity);

    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        if (depth[nodeIdx] != UINT32_MAX) {
            continue;
        }

        std::size_t current = nodeIdx;
        stack.clear();

        uint32_t baseDepth = UINT32_MAX;
        while (true) {
            if (depth[current] != UINT32_MAX) {
                baseDepth = depth[current];
                break;
            }

            stack.push_back(current);

            if (traits.is_root_parent(traits.parent_id(nodes[current])) ||
                parent[current] == no_parent) {
                baseDepth = UINT32_MAX;
                break;
            }

            current = parent[current];
        }

        while (!stack.empty()) {
            const std::size_t nodeIndex = stack.back();
            stack.pop_back();

            if (baseDepth == UINT32_MAX) {
                baseDepth = 0;
            } else {
                baseDepth += 1;
            }

            depth[nodeIndex] = baseDepth;
        }
    }

    return depth;
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
bool verifyWithParentIndex(const Nodes &nodes,
                           const std::vector<std::size_t> &parentIndex,
                           const Traits &traits) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount <= 1) {
        return true;
    }

    std::vector<uint32_t> verifiedDepth(nodeCount, UINT32_MAX);

    uint32_t previousDepth = 0;
    typename Traits::Id previousId{};

    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        const typename Traits::Id currentId = traits.id(nodes[nodeIdx]);

        uint32_t currentDepth = 0;
        if (!traits.is_root_parent(traits.parent_id(nodes[nodeIdx])) &&
            parentIndex[nodeIdx] != no_parent) {
            const std::size_t parentNodeIndex = parentIndex[nodeIdx];
            if (verifiedDepth[parentNodeIndex] == UINT32_MAX) {
                return false;
            }

            currentDepth = verifiedDepth[parentNodeIndex] + 1;
            if (currentDepth > maxDepthForPrefix<DepthPrefixBytes>()) {
                return false;
            }
        }

        if (nodeIdx > 0 && (currentDepth < previousDepth ||
                            (currentDepth == previousDepth &&
                             idLess(currentId, previousId, traits)))) {
            return false;
        }

        verifiedDepth[nodeIdx] = currentDepth;
        previousDepth = currentDepth;
        previousId = currentId;
    }

    return true;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_DEPTH_HPP
