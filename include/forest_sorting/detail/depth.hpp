#ifndef FOREST_SORTING_DETAIL_DEPTH_HPP
#define FOREST_SORTING_DETAIL_DEPTH_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/detail/validation.hpp"
#include "forest_sorting/traits.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

enum class DepthVisitState : uint8_t {
    Unvisited,
    Visiting,
    Complete,
};

template <std::size_t DepthPrefixBytes>
using DepthValue = std::conditional_t<
    DepthPrefixBytes == 1, uint8_t,
    std::conditional_t<DepthPrefixBytes == 2, uint16_t, uint32_t>>;

template <typename Depth> struct ComputedDepths {
    using value_type = Depth;

    std::vector<Depth> values;
    Depth observedMax = 0;
};

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

[[nodiscard]] constexpr std::size_t
depthPrefixBytesForMax(uint32_t maximumDepth) noexcept {
    if (maximumDepth <= maxDepthForPrefix<1>()) {
        return 1;
    }
    if (maximumDepth <= maxDepthForPrefix<2>()) {
        return 2;
    }
    if (maximumDepth <= maxDepthForPrefix<3>()) {
        return 3;
    }
    return 4;
}

template <forest_sorting::IndexedDepthInput Depths>
uint32_t validatePrecomputedDepthInput(std::size_t nodeCount,
                                       const Depths &depths) {
    requireMatchingCount(static_cast<std::size_t>(depths.size()), nodeCount,
                         "depth vector size does not match nodes");
    uint32_t observedMaxDepth = 0;
    for (std::size_t index = 0; index < nodeCount; ++index) {
        const auto depth = depths[index];
        if (depth > UINT32_MAX) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        observedMaxDepth =
            std::max(observedMaxDepth, static_cast<uint32_t>(depth));
    }
    return observedMaxDepth;
}

template <std::size_t DepthPrefixBytes,
          forest_sorting::IndexedDepthInput Depths>
std::vector<DepthValue<DepthPrefixBytes>>
narrowIndexedDepths(const Depths &depths) {
    std::vector<DepthValue<DepthPrefixBytes>> narrowed;
    narrowed.reserve(static_cast<std::size_t>(depths.size()));
    for (std::size_t index = 0; index < static_cast<std::size_t>(depths.size());
         ++index) {
        narrowed.push_back(
            static_cast<DepthValue<DepthPrefixBytes>>(depths[index]));
    }
    return narrowed;
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
ComputedDepths<DepthValue<DepthPrefixBytes>>
computeDepths(const Nodes &nodes, const std::vector<std::size_t> &parent,
              const Traits &traits) {
    static_assert(DepthPrefixBytes >= 1 && DepthPrefixBytes <= 4,
                  "DepthPrefixBytes must be between 1 and 4");
    using Depth = DepthValue<DepthPrefixBytes>;
    const std::size_t nodeCount = nodes.size();
    requireMatchingCount(parent.size(), nodeCount,
                         "parent index size does not match nodes");
    ComputedDepths<Depth> result;
    result.values.resize(nodeCount);
    std::vector<DepthVisitState> visitState(nodeCount,
                                            DepthVisitState::Unvisited);

    if (nodeCount == 0) {
        return result;
    }

    std::vector<std::size_t> stack;
    stack.reserve(initial_range_stack_capacity);

    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        if (visitState[nodeIdx] == DepthVisitState::Complete) {
            continue;
        }

        std::size_t current = nodeIdx;
        stack.clear();

        Depth baseDepth = 0;
        bool reachedRoot = false;
        bool traversalComplete = false;
        for (std::size_t remaining = nodeCount; remaining > 0; --remaining) {
            if (visitState[current] == DepthVisitState::Complete) {
                baseDepth = result.values[current];
                traversalComplete = true;
                break;
            }
            if (visitState[current] == DepthVisitState::Visiting) {
                throw std::runtime_error("parent cycle");
            }

            visitState[current] = DepthVisitState::Visiting;
            stack.push_back(current);

            if (isParentSentinel(traits, traits.parent_id(nodes[current])) ||
                parent[current] == no_parent) {
                reachedRoot = true;
                traversalComplete = true;
                break;
            }

            const std::size_t parentIndex = parent[current];
            if (parentIndex >= nodeCount) {
                throw std::runtime_error("parent index out of range");
            }
            current = parentIndex;
        }
        if (!traversalComplete) {
            throw std::runtime_error("parent cycle");
        }

        while (!stack.empty()) {
            const std::size_t nodeIndex = stack.back();
            stack.pop_back();

            if (reachedRoot) {
                baseDepth = 0;
                reachedRoot = false;
            } else {
                if (baseDepth ==
                    static_cast<Depth>(maxDepthForPrefix<DepthPrefixBytes>())) {
                    throw std::runtime_error(
                        "forest depth exceeds sortable depth limit");
                }
                baseDepth = static_cast<Depth>(baseDepth + 1);
            }

            result.values[nodeIndex] = baseDepth;
            result.observedMax = std::max(result.observedMax, baseDepth);
            visitState[nodeIndex] = DepthVisitState::Complete;
        }
    }

    return result;
}

template <std::size_t DepthPrefixBytes, std::unsigned_integral SourceDepth>
std::vector<DepthValue<DepthPrefixBytes>>
narrowDepths(const std::vector<SourceDepth> &depths) {
    using Depth = DepthValue<DepthPrefixBytes>;
    std::vector<Depth> narrowed;
    narrowed.reserve(depths.size());
    for (SourceDepth depth : depths) {
        narrowed.push_back(static_cast<Depth>(depth));
    }
    return narrowed;
}

template <std::size_t DepthPrefixBytes, typename Nodes, typename Traits>
bool verifyWithParentIndex(const Nodes &nodes,
                           const std::vector<std::size_t> &parentIndex,
                           const Traits &traits) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return true;
    }

    using Depth = DepthValue<DepthPrefixBytes>;
    std::vector<Depth> verifiedDepth(nodeCount);
    std::vector<bool> depthReady(nodeCount, false);

    Depth previousDepth = 0;
    std::optional<typename Traits::Id> previousId;

    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        typename Traits::Id currentId = traits.id(nodes[nodeIdx]);

        Depth currentDepth = 0;
        if (!isParentSentinel(traits, traits.parent_id(nodes[nodeIdx])) &&
            parentIndex[nodeIdx] != no_parent) {
            const std::size_t parentNodeIndex = parentIndex[nodeIdx];
            if (!depthReady[parentNodeIndex]) {
                return false;
            }

            if (verifiedDepth[parentNodeIndex] ==
                static_cast<Depth>(maxDepthForPrefix<DepthPrefixBytes>())) {
                return false;
            }
            currentDepth =
                static_cast<Depth>(verifiedDepth[parentNodeIndex] + 1);
        }

        if (previousId && (currentDepth < previousDepth ||
                           (currentDepth == previousDepth &&
                            idLess(currentId, *previousId, traits)))) {
            return false;
        }

        verifiedDepth[nodeIdx] = currentDepth;
        depthReady[nodeIdx] = true;
        previousDepth = currentDepth;
        previousId.emplace(std::move(currentId));
    }

    return true;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_DEPTH_HPP
