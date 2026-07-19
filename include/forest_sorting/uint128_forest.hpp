#ifndef FOREST_SORTING_UINT128_FOREST_HPP
#define FOREST_SORTING_UINT128_FOREST_HPP

#ifndef __SIZEOF_INT128__
#error                                                                         \
    "forest_sorting UInt128 forest compatibility requires unsigned __int128. Include forest_sorting/algorithms.hpp and use caller-owned ID types with custom traits for portable code."
#endif

#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/uint128.hpp"

#include <cstddef>
#include <vector>

namespace forest_sorting {

struct Node {
    UInt128 id;
    UInt128 parentId;
};

struct UInt128NodeTraits : UInt128Traits {
    static UInt128 id(const Node &node) noexcept { return node.id; }
    static UInt128 parent_id(const Node &node) noexcept {
        return node.parentId;
    }
};

inline std::vector<std::size_t>
sortedOrderByDepthAndId(const std::vector<Node> &nodes) {
    return forest_sorting::sortedOrderByDepthAndId(nodes, UInt128NodeTraits{});
}

inline std::vector<Node>
sortedCopyByDepthAndId(const std::vector<Node> &nodes) {
    return forest_sorting::sortedCopyByDepthAndId(nodes, UInt128NodeTraits{});
}

inline void sortInPlaceByDepthAndId(std::vector<Node> &nodes) {
    forest_sorting::sortInPlaceByDepthAndId(nodes, UInt128NodeTraits{});
}

inline bool verifySortedByDepthAndId(const std::vector<Node> &nodes) {
    return forest_sorting::verifySortedByDepthAndId(nodes, UInt128NodeTraits{});
}

} // namespace forest_sorting

#endif // FOREST_SORTING_UINT128_FOREST_HPP
