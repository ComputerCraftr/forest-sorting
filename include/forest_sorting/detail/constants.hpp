#ifndef FOREST_SORTING_DETAIL_CONSTANTS_HPP
#define FOREST_SORTING_DETAIL_CONSTANTS_HPP

#include <cstddef>
#include <limits>

namespace forest_sorting::detail {

inline constexpr std::size_t no_parent =
    std::numeric_limits<std::size_t>::max();

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_CONSTANTS_HPP
