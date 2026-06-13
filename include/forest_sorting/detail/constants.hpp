#ifndef FOREST_SORTING_DETAIL_CONSTANTS_HPP
#define FOREST_SORTING_DETAIL_CONSTANTS_HPP

#include <cstddef>
#include <limits>

namespace forest_sorting::detail {

inline constexpr std::size_t no_parent =
    std::numeric_limits<std::size_t>::max();

// Initial capacity for explicit radix/depth work stacks. This avoids repeating
// magic reserve values while still allowing the vectors to grow for unusual
// inputs.
inline constexpr std::size_t initial_range_stack_capacity = 128;

inline std::size_t nextPowerOfTwo(std::size_t value) noexcept {
    std::size_t capacity = 1;
    while (capacity < value) {
        capacity <<= 1;
    }
    return capacity;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_CONSTANTS_HPP
