#ifndef FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP
#define FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP

#include <concepts>

namespace forest_sorting::detail {

template <typename Traits, typename IdType>
concept HasForestTraitsParentSentinel =
    requires(const Traits &traits, const IdType &parentId) {
        { traits.is_parent_sentinel(parentId) } -> std::convertible_to<bool>;
    };

template <typename Traits, typename IdType>
inline bool isParentSentinel(const Traits &traits,
                             const IdType &parentId) noexcept {
    if constexpr (HasForestTraitsParentSentinel<Traits, IdType>) {
        return traits.is_parent_sentinel(parentId);
    } else {
        return false;
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_PARENT_SENTINEL_HPP
