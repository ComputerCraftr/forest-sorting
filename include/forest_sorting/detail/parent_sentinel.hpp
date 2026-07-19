#ifndef FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP
#define FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP

#include "forest_sorting/traits.hpp"

namespace forest_sorting::detail {

template <typename Traits, typename IdType>
inline bool isParentSentinel(const Traits &traits, const IdType &parentId) {
    if constexpr (forest_sorting::ForestTraitsParentSentinel<Traits>) {
        return traits.is_parent_sentinel(parentId);
    } else {
        return false;
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP
