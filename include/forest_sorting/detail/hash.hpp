#ifndef FOREST_SORTING_DETAIL_HASH_HPP
#define FOREST_SORTING_DETAIL_HASH_HPP

#include <cstddef>
#include <cstdint>

namespace forest_sorting::detail {

inline uint64_t mix64(uint64_t input) noexcept {
    input += 0x9e3779b97f4a7c15ULL;
    input = (input ^ (input >> 30)) * 0xbf58476d1ce4e5b9ULL;
    input = (input ^ (input >> 27)) * 0x94d049bb133111ebULL;
    return input ^ (input >> 31);
}

template <typename Id, typename Traits>
std::size_t hashBytes(const Id &nodeId, const Traits &traits) noexcept {
    uint64_t mixed = 0;
    for (std::size_t byteIndex = 0; byteIndex < Traits::id_byte_count;
         ++byteIndex) {
        const uint64_t byteValue = traits.byte_msb_first(nodeId, byteIndex);
        mixed ^= mix64(byteValue + 0x9e3779b97f4a7c15ULL + (mixed << 6U) +
                       (mixed >> 2U));
    }
    return static_cast<std::size_t>(mixed);
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_HASH_HPP
