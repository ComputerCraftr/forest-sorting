#ifndef FOREST_SORTING_TRAITS_HPP
#define FOREST_SORTING_TRAITS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace forest_sorting {

namespace detail {

template <typename Values>
concept IndexedInput = requires(const Values &values, std::size_t index) {
    { values.size() } -> std::convertible_to<std::size_t>;
    values[index];
};

template <IndexedInput Values>
using IndexedValue = std::remove_cvref_t<
    decltype(std::declval<const Values &>()[std::size_t{}])>;

} // namespace detail

template <typename Nodes>
concept IndexedNodeInput = detail::IndexedInput<Nodes>;

template <typename Nodes>
concept CopyableNodeInput =
    IndexedNodeInput<Nodes> &&
    std::copy_constructible<detail::IndexedValue<Nodes>>;

template <typename Nodes>
concept MutableNodeInput =
    IndexedNodeInput<Nodes> && std::movable<detail::IndexedValue<Nodes>> &&
    std::swappable<detail::IndexedValue<Nodes>> &&
    requires(Nodes &nodes, std::size_t index,
             detail::IndexedValue<Nodes> value) {
        { nodes[index] } -> std::same_as<detail::IndexedValue<Nodes> &>;
        nodes[index] = std::move(value);
    };

template <typename Depths>
concept IndexedDepthInput =
    detail::IndexedInput<Depths> &&
    std::unsigned_integral<detail::IndexedValue<Depths>> &&
    (!std::same_as<detail::IndexedValue<Depths>, bool>);

namespace detail {

template <typename Traits>
concept ValidForestIdByteCount = requires {
    typename std::integral_constant<std::size_t, static_cast<std::size_t>(
                                                     Traits::id_byte_count)>;
    requires(static_cast<std::size_t>(Traits::id_byte_count) > 0);
};

template <std::size_t ChunkBytes>
using ForestTraitsChunkValue = std::conditional_t<
    ChunkBytes == 1, uint8_t,
    std::conditional_t<
        ChunkBytes == 2, uint16_t,
        std::conditional_t<ChunkBytes == 4, uint32_t, uint64_t>>>;

} // namespace detail

template <typename Traits, typename Node>
concept ForestTraits =
    detail::ValidForestIdByteCount<Traits> &&
    std::copyable<typename Traits::Id> &&
    requires(const Traits &traits, const Node &node, const Traits::Id &nodeId,
             std::size_t byteIndex) {
        typename Traits::Id;
        { traits.id(node) } -> std::same_as<typename Traits::Id>;
        { traits.parent_id(node) } -> std::same_as<typename Traits::Id>;
        {
            traits.byte_msb_first(nodeId, byteIndex)
        } -> std::convertible_to<uint8_t>;
    };

// Optional comparison hooks are optimized views of byte_msb_first, not
// alternative semantics. Implementations must preserve that byte-defined order.
template <typename Traits>
concept ForestTraitsLess = requires(const Traits &traits, const Traits::Id &lhs,
                                    const Traits::Id &rhs) {
    { traits.less(lhs, rhs) } -> std::convertible_to<bool>;
};

template <typename Traits>
concept ForestTraitsEqual = requires(
    const Traits &traits, const Traits::Id &lhs, const Traits::Id &rhs) {
    { traits.equal(lhs, rhs) } -> std::convertible_to<bool>;
};

template <typename Traits>
concept ForestTraitsParentSentinel =
    requires(const Traits &traits, const Traits::Id &parentId) {
        { traits.is_parent_sentinel(parentId) } -> std::convertible_to<bool>;
    };

// Each supported width is detected independently. Missing widths fall back to
// byte_msb_first assembly.
template <std::size_t ChunkBytes, typename Traits>
concept ForestTraitsChunkAccess =
    (ChunkBytes == 1 || ChunkBytes == 2 || ChunkBytes == 4 ||
     ChunkBytes == 8) &&
    requires(const Traits &traits, const Traits::Id &nodeId,
             std::size_t chunkIndex) {
        {
            traits.template chunk_msb_first<ChunkBytes>(nodeId, chunkIndex)
        } -> std::convertible_to<detail::ForestTraitsChunkValue<ChunkBytes>>;
    };

} // namespace forest_sorting

#endif // FOREST_SORTING_TRAITS_HPP
