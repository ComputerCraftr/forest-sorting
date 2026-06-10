#ifndef FOREST_SORTING_RADIX_HPP
#define FOREST_SORTING_RADIX_HPP

#include "forest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_internal {

inline constexpr std::size_t kUInt128ByteCount = 16;
inline constexpr std::size_t kDepthByteCount = 2;
inline constexpr std::size_t kIdWordCount = 2;
inline constexpr std::size_t kWordByteCount = 8;
inline constexpr std::size_t kRadixBits = 8;
inline constexpr std::size_t kRadixBucketCount = 256;

inline uint8_t idByte(UInt128 value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

inline uint8_t depthByte(uint32_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

inline uint64_t idWordMsbFirst(UInt128 value, std::size_t wordIndex) noexcept {
    if (wordIndex == 0) {
        return static_cast<uint64_t>(value >> 64);
    }
    return static_cast<uint64_t>(value);
}

inline uint8_t wordByte(uint64_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

template <typename DigitForIndex>
void radixPass(std::vector<std::size_t> &order,
               std::vector<std::size_t> &scratch, DigitForIndex digitForIndex) {
    std::array<std::size_t, kRadixBucketCount> counts{};
    for (std::size_t nodeIndex : order) {
        ++counts[digitForIndex(nodeIndex)];
    }

    std::size_t offset = 0;
    for (std::size_t &count : counts) {
        const std::size_t bucketSize = count;
        count = offset;
        offset += bucketSize;
    }

    for (std::size_t nodeIndex : order) {
        const uint8_t digit = digitForIndex(nodeIndex);
        scratch[counts[digit]] = nodeIndex;
        ++counts[digit];
    }

    order.swap(scratch);
}

template <typename Entry, typename IdForEntry>
void radixSortEntriesById(std::vector<Entry> &entries, IdForEntry idForEntry) {
    if (entries.size() <= 1) {
        return;
    }

    std::vector<Entry> scratch(entries.size());
    for (std::size_t byteIndex = 0; byteIndex < kUInt128ByteCount;
         ++byteIndex) {
        std::array<std::size_t, kRadixBucketCount> counts{};
        for (const Entry &entry : entries) {
            ++counts[idByte(idForEntry(entry), byteIndex)];
        }

        std::size_t offset = 0;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = offset;
            offset += bucketSize;
        }

        for (const Entry &entry : entries) {
            const uint8_t digit = idByte(idForEntry(entry), byteIndex);
            scratch[counts[digit]] = entry;
            ++counts[digit];
        }

        entries.swap(scratch);
    }
}

} // namespace forest_internal

#endif // FOREST_SORTING_RADIX_HPP
