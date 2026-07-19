#ifndef FOREST_SORTING_SUPPORT_TEST_BYTES_HPP
#define FOREST_SORTING_SUPPORT_TEST_BYTES_HPP

#include "forest_sorting/detail/id_chunks.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

template <std::size_t ByteCount> struct TestBytes {
    std::array<uint8_t, ByteCount> bytes{};

    friend constexpr bool operator==(const TestBytes &lhs,
                                     const TestBytes &rhs) noexcept = default;

    friend constexpr bool operator<(const TestBytes &lhs,
                                    const TestBytes &rhs) noexcept {
        return lhs.bytes < rhs.bytes;
    }
};

template <std::size_t ByteCount> struct TestNode {
    TestBytes<ByteCount> id;
    TestBytes<ByteCount> parentId;
};

template <std::size_t ByteCount> struct HashFreeTestBytesTraits {
    using Id = TestBytes<ByteCount>;
    static constexpr std::size_t id_byte_count = ByteCount;

    Id id(const TestNode<ByteCount> &node) const noexcept { return node.id; }

    Id parent_id(const TestNode<ByteCount> &node) const noexcept {
        return node.parentId;
    }

    bool is_parent_sentinel(const Id &nodeId) const noexcept {
        return std::ranges::all_of(
            nodeId.bytes, [](uint8_t byteValue) { return byteValue == 0; });
    }

    bool less(const Id &lhs, const Id &rhs) const noexcept {
        return lhs.bytes < rhs.bytes;
    }

    bool equal(const Id &lhs, const Id &rhs) const noexcept {
        return lhs.bytes == rhs.bytes;
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        return nodeId.bytes[byteIndex];
    }

    template <std::size_t ChunkBytes>
    auto chunk_msb_first(const Id &nodeId,
                         std::size_t chunkIndex) const noexcept {
        static_assert(ChunkBytes == 1 || ChunkBytes == 2 || ChunkBytes == 4 ||
                          ChunkBytes == 8,
                      "chunk_msb_first supports 1, 2, 4, and 8-byte chunks");
        if constexpr (ChunkBytes == 1) {
            return nodeId.bytes[chunkIndex];
        } else {
            return forest_sorting::detail::buildChunkFromBytes<ChunkBytes>(
                nodeId, chunkIndex, *this);
        }
    }
};

template <std::size_t ByteCount>
TestBytes<ByteCount> makeTestBytes(uint8_t high, uint8_t low) {
    TestBytes<ByteCount> nodeId{};
    nodeId.bytes[0] = high;
    nodeId.bytes[ByteCount - 1] = low;
    return nodeId;
}

#endif // FOREST_SORTING_SUPPORT_TEST_BYTES_HPP
