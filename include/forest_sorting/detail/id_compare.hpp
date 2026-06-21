#ifndef FOREST_SORTING_DETAIL_ID_COMPARE_HPP
#define FOREST_SORTING_DETAIL_ID_COMPARE_HPP

#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/traits.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::detail {

template <typename Traits, typename IdType>
concept HasForestTraitsLess =
    requires(const Traits &traits, const IdType &lhs, const IdType &rhs) {
        { traits.less(lhs, rhs) } -> std::convertible_to<bool>;
    };

template <typename IdType>
concept HasNativeIdLess = requires(const IdType &lhs, const IdType &rhs) {
    { lhs < rhs } -> std::convertible_to<bool>;
};

template <typename Traits>
concept HasCheapForestTraitsIdOrder =
    HasForestTraitsLess<Traits, forest_sorting::ForestTraitsId<Traits>> ||
    HasNativeIdLess<forest_sorting::ForestTraitsId<Traits>>;

template <typename Traits, typename IdType>
concept HasForestTraitsEqual =
    requires(const Traits &traits, const IdType &lhs, const IdType &rhs) {
        { traits.equal(lhs, rhs) } -> std::convertible_to<bool>;
    };

template <typename IdType>
concept HasNativeIdEqual = requires(const IdType &lhs, const IdType &rhs) {
    { lhs == rhs } -> std::convertible_to<bool>;
};

template <typename Traits>
inline constexpr bool shouldCacheChunkIds =
    ((Traits::id_byte_count + chunk_byte_count - 1) / chunk_byte_count) > 1 &&
    !HasCheapForestTraitsIdOrder<Traits>;

template <std::size_t ChunkCount, typename LhsChunkAt, typename RhsChunkAt>
inline int compareChunkSequence(LhsChunkAt lhsChunkAt,
                                RhsChunkAt rhsChunkAt) noexcept {
    for (std::size_t chunkIdx = 0; chunkIdx < ChunkCount; ++chunkIdx) {
        const uint64_t lhsChunk = lhsChunkAt(chunkIdx);
        const uint64_t rhsChunk = rhsChunkAt(chunkIdx);
        if (lhsChunk < rhsChunk) {
            return -1;
        }
        if (lhsChunk > rhsChunk) {
            return 1;
        }
    }
    return 0;
}

template <std::size_t ChunkCount>
inline int
compareCachedIdChunks(const CachedChunkId<ChunkCount> &lhs,
                      const CachedChunkId<ChunkCount> &rhs) noexcept {
    return compareChunkSequence<ChunkCount>(
        [&](std::size_t chunkIdx) noexcept { return lhs.chunks[chunkIdx]; },
        [&](std::size_t chunkIdx) noexcept { return rhs.chunks[chunkIdx]; });
}

template <typename NodeId, typename NodeTraits>
inline int compareIdsMsbFirst(const NodeId &lhs, const NodeId &rhs,
                              const NodeTraits &traits) noexcept {
    constexpr std::size_t chunkCount =
        (NodeTraits::id_byte_count + chunk_byte_count - 1) / chunk_byte_count;
    return compareChunkSequence<chunkCount>(
        [&](std::size_t chunkIdx) noexcept {
            return chunkMsbFirst(lhs, chunkIdx, traits);
        },
        [&](std::size_t chunkIdx) noexcept {
            return chunkMsbFirst(rhs, chunkIdx, traits);
        });
}

template <typename NodeId, typename NodeTraits>
inline bool idLess(const NodeId &lhs, const NodeId &rhs,
                   const NodeTraits &traits) noexcept {
    if constexpr (HasForestTraitsLess<NodeTraits, NodeId>) {
        return traits.less(lhs, rhs);
    } else if constexpr (HasNativeIdLess<NodeId>) {
        return lhs < rhs;
    } else {
        return compareIdsMsbFirst(lhs, rhs, traits) < 0;
    }
}

template <typename NodeId, typename NodeTraits>
inline bool idEqual(const NodeId &lhs, const NodeId &rhs,
                    const NodeTraits &traits) noexcept {
    if constexpr (HasForestTraitsEqual<NodeTraits, NodeId>) {
        return traits.equal(lhs, rhs);
    } else if constexpr (HasNativeIdEqual<NodeId>) {
        return lhs == rhs;
    } else {
        return compareIdsMsbFirst(lhs, rhs, traits) == 0;
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_COMPARE_HPP
