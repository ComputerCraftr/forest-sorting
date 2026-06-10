#ifndef FOREST_SORTING_TEST_BYTES_HPP
#define FOREST_SORTING_TEST_BYTES_HPP

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

template <std::size_t ByteCount>
std::size_t testHashBytes(const TestBytes<ByteCount> &nodeId) noexcept {
    uint64_t hashValue = 14695981039346656037ULL;
    for (uint8_t byteValue : nodeId.bytes) {
        hashValue ^= byteValue;
        hashValue *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hashValue);
}

template <std::size_t ByteCount> struct TestBytesTraits {
    using Id = TestBytes<ByteCount>;
    static constexpr std::size_t id_byte_count = ByteCount;

    Id id(const TestNode<ByteCount> &node) const noexcept { return node.id; }

    Id parent_id(const TestNode<ByteCount> &node) const noexcept {
        return node.parentId;
    }

    bool is_root_parent(const Id &nodeId) const noexcept {
        return std::ranges::all_of(
            nodeId.bytes, [](uint8_t byteValue) { return byteValue == 0; });
    }

    bool equal(const Id &lhs, const Id &rhs) const noexcept {
        return lhs == rhs;
    }

    std::size_t hash(const Id &nodeId) const noexcept {
        return testHashBytes(nodeId);
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        return nodeId.bytes[byteIndex];
    }

    uint64_t chunk_msb_first(const Id &nodeId,
                             std::size_t chunkIndex) const noexcept {
        uint64_t value = 0;
        const std::size_t start = chunkIndex * 8;
        for (std::size_t byteIdx = 0; byteIdx < 8; ++byteIdx) {
            value <<= 8;
            if (start + byteIdx < ByteCount) {
                value |= nodeId.bytes[start + byteIdx];
            }
        }
        return value;
    }
};

template <std::size_t ByteCount>
TestBytes<ByteCount> makeTestBytes(uint8_t high, uint8_t low) {
    TestBytes<ByteCount> nodeId{};
    nodeId.bytes[0] = high;
    nodeId.bytes[ByteCount - 1] = low;
    return nodeId;
}

#endif // FOREST_SORTING_TEST_BYTES_HPP
