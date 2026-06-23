#ifndef FOREST_SORTING_TRAITS_HPP
#define FOREST_SORTING_TRAITS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace forest_sorting {

template <typename Traits, typename Node>
concept ForestTraits = requires(Traits traits, Node node, Traits::Id nodeId,
                                std::size_t byteIndex) {
    typename Traits::Id;
    { Traits::id_byte_count } -> std::convertible_to<std::size_t>;
    { traits.id(node) } -> std::same_as<typename Traits::Id>;
    { traits.parent_id(node) } -> std::same_as<typename Traits::Id>;
    {
        traits.byte_msb_first(nodeId, byteIndex)
    } -> std::convertible_to<uint8_t>;
};

template <typename Traits> using ForestTraitsId = Traits::Id;

} // namespace forest_sorting

#endif // FOREST_SORTING_TRAITS_HPP
