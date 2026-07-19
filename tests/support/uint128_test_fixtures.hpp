#ifndef FOREST_SORTING_TEST_SUPPORT_UINT128_TEST_FIXTURES_HPP
#define FOREST_SORTING_TEST_SUPPORT_UINT128_TEST_FIXTURES_HPP

#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::test_support {

inline bool sameNodes(const std::vector<Node> &lhs,
                      const std::vector<Node> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t nodeIndex = 0; nodeIndex < lhs.size(); ++nodeIndex) {
        if (lhs[nodeIndex].id != rhs[nodeIndex].id ||
            lhs[nodeIndex].parentId != rhs[nodeIndex].parentId) {
            return false;
        }
    }
    return true;
}

struct UInt128LowIdentityHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        return static_cast<std::size_t>(nodeId);
    }
};

struct UInt128HighIdentityHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        return static_cast<std::size_t>(nodeId >> 64U);
    }
};

inline std::vector<Node> makeHighIdentityCollisionRoots(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        nodes.push_back(
            Node{makeId(1, static_cast<uint64_t>(nodeIndex) + 1ULL), 0});
    }
    return nodes;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_TEST_SUPPORT_UINT128_TEST_FIXTURES_HPP
