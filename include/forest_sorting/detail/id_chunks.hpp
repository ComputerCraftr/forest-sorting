#ifndef FOREST_SORTING_DETAIL_ID_CHUNKS_HPP
#define FOREST_SORTING_DETAIL_ID_CHUNKS_HPP

#include "forest_sorting/detail/radix_counts.hpp"
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::detail {

inline constexpr std::size_t chunk_byte_count = 8;

template <typename NodeTraits, typename NodeId>
concept HasChunkMsbFirst = requires(
    const NodeTraits &traits, const NodeId &nodeId, std::size_t chunkIndex) {
    {
        traits.chunk_msb_first(nodeId, chunkIndex)
    } -> std::convertible_to<std::uint64_t>;
};

template <std::size_t SorterChunkBytes> struct ChunkValue;
template <> struct ChunkValue<1> {
    using Type = uint8_t;
};
template <> struct ChunkValue<2> {
    using Type = uint16_t;
};
template <> struct ChunkValue<4> {
    using Type = uint32_t;
};
template <> struct ChunkValue<8> {
    using Type = uint64_t;
};

template <std::size_t SorterChunkBytes>
using ChunkValueType = ChunkValue<SorterChunkBytes>::Type;

template <std::size_t SorterChunkBytes, typename NodeId, typename NodeTraits>
ChunkValueType<SorterChunkBytes>
buildChunkFromBytes(const NodeId &nodeId, std::size_t chunkIndex,
                    const NodeTraits &traits) noexcept {
    uint64_t value = 0;
    const std::size_t firstByte = chunkIndex * SorterChunkBytes;
    for (std::size_t offset = 0; offset < SorterChunkBytes; ++offset) {
        const std::size_t byteIndex = firstByte + offset;
        value <<= radix_bits;
        if (byteIndex < NodeTraits::id_byte_count) {
            value |= traits.byte_msb_first(nodeId, byteIndex);
        }
    }
    return static_cast<ChunkValueType<SorterChunkBytes>>(value);
}

template <typename NodeTraits, typename NodeId, std::size_t SorterChunkBytes>
concept HasTemplatedChunkMsbFirst = requires(
    const NodeTraits &traits, const NodeId &nodeId, std::size_t chunkIndex) {
    {
        traits.template chunk_msb_first<SorterChunkBytes>(nodeId, chunkIndex)
    } -> std::convertible_to<ChunkValueType<SorterChunkBytes>>;
};

template <std::size_t SorterChunkBytes = chunk_byte_count, typename NodeId,
          typename NodeTraits>
ChunkValueType<SorterChunkBytes>
chunkMsbFirst(const NodeId &nodeId, std::size_t chunkIndex,
              const NodeTraits &traits) noexcept {
    if constexpr (HasTemplatedChunkMsbFirst<NodeTraits, NodeId,
                                            SorterChunkBytes>) {
        return static_cast<ChunkValueType<SorterChunkBytes>>(
            traits.template chunk_msb_first<SorterChunkBytes>(nodeId,
                                                              chunkIndex));
    } else if constexpr (SorterChunkBytes == chunk_byte_count &&
                         HasChunkMsbFirst<NodeTraits, NodeId>) {
        return static_cast<ChunkValueType<SorterChunkBytes>>(
            traits.chunk_msb_first(nodeId, chunkIndex));
    } else {
        return buildChunkFromBytes<SorterChunkBytes>(nodeId, chunkIndex,
                                                     traits);
    }
}

template <std::size_t ChunkCount> struct CachedChunkId {
    static_assert(
        ChunkCount > 1,
        "do not build cached ID chunks for IDs that fit in one chunk");
    std::array<uint64_t, ChunkCount> chunks;
};

template <typename OriginalTraits, std::size_t ChunkCount>
struct CachedChunkIdTraits {
    static_assert(
        ChunkCount > 1,
        "do not build cached ID chunks for IDs that fit in one chunk");

    using Id = CachedChunkId<ChunkCount>;
    static constexpr std::size_t id_byte_count = OriginalTraits::id_byte_count;

    template <std::size_t SorterChunkBytes>
    ChunkValueType<SorterChunkBytes>
    chunk_msb_first(const Id &nodeId, std::size_t chunkIndex) const noexcept {
        if constexpr (SorterChunkBytes == 8) {
            return static_cast<ChunkValueType<SorterChunkBytes>>(
                nodeId.chunks[chunkIndex]);
        } else if constexpr (SorterChunkBytes == 4) {
            const std::size_t arrayIdx = chunkIndex / 2;
            const std::size_t subIdx = chunkIndex % 2;
            const uint64_t chunk = nodeId.chunks[arrayIdx];
            if (subIdx == 0) {
                return static_cast<ChunkValueType<SorterChunkBytes>>(chunk >>
                                                                     32);
            }
            return static_cast<ChunkValueType<SorterChunkBytes>>(chunk &
                                                                 0xFFFFFFFFULL);
        } else {
            return buildChunkFromBytes<SorterChunkBytes>(nodeId, chunkIndex,
                                                         *this);
        }
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        const std::size_t chunkIndex = byteIndex / 8;
        const std::size_t byteInChunk = byteIndex % 8;
        const uint64_t chunk = nodeId.chunks[chunkIndex];
        return static_cast<uint8_t>((chunk >> ((7 - byteInChunk) * 8)) & 0xFF);
    }
};

template <typename NodeId, typename NodeTraits, std::size_t ChunkCount>
void fillCachedChunkId(CachedChunkId<ChunkCount> &cachedId,
                       const NodeId &nodeId,
                       const NodeTraits &traits) noexcept {
    for (std::size_t chunkIdx = 0; chunkIdx < ChunkCount; ++chunkIdx) {
        cachedId.chunks[chunkIdx] =
            chunkMsbFirst<chunk_byte_count>(nodeId, chunkIdx, traits);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_CHUNKS_HPP
