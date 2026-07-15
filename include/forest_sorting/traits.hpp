#ifndef FOREST_SORTING_TRAITS_HPP
#define FOREST_SORTING_TRAITS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace forest_sorting {

template <typename Traits>
concept ValidForestIdByteCount = requires {
    typename std::integral_constant<std::size_t, static_cast<std::size_t>(
                                                     Traits::id_byte_count)>;
    requires(static_cast<std::size_t>(Traits::id_byte_count) > 0);
};

template <typename Traits, typename Node>
concept ForestTraits =
    ValidForestIdByteCount<Traits> && std::copyable<typename Traits::Id> &&
    requires(const Traits &traits, const Node &node, const Traits::Id &nodeId,
             std::size_t byteIndex) {
        typename Traits::Id;
        { traits.id(node) } -> std::same_as<typename Traits::Id>;
        { traits.parent_id(node) } -> std::same_as<typename Traits::Id>;
        {
            traits.byte_msb_first(nodeId, byteIndex)
        } -> std::convertible_to<uint8_t>;
    };

template <typename Traits> using ForestTraitsId = Traits::Id;

} // namespace forest_sorting

#endif // FOREST_SORTING_TRAITS_HPP
