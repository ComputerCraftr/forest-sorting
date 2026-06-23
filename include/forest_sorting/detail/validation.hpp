#ifndef FOREST_SORTING_DETAIL_VALIDATION_HPP
#define FOREST_SORTING_DETAIL_VALIDATION_HPP

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forest_sorting::detail {

inline void requireMatchingCount(std::size_t actual, std::size_t expected,
                                 std::string_view message) {
    if (actual != expected) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Permutation, typename Equal>
void rejectAdjacentDuplicates(const Permutation &permutation, Equal equal,
                              std::string_view message) {
    for (std::size_t offset = 1; offset < permutation.size(); ++offset) {
        if (equal(permutation[offset - 1], permutation[offset])) {
            throw std::runtime_error(std::string(message));
        }
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_VALIDATION_HPP
