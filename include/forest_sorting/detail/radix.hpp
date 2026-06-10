#ifndef FOREST_SORTING_DETAIL_RADIX_HPP
#define FOREST_SORTING_DETAIL_RADIX_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::detail {

inline constexpr std::size_t radix_bits = 8;
inline constexpr std::size_t radix_bucket_count = 256;
inline constexpr std::size_t chunk_byte_count = 8;

inline uint8_t wordByte(uint64_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * radix_bits));
}

inline uint8_t depthByte(uint32_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * radix_bits));
}

template <typename Traits, typename Id>
concept HasChunkMsbFirst =
    requires(const Traits &traits, const Id &nodeId, std::size_t chunkIndex) {
        {
            traits.chunk_msb_first(nodeId, chunkIndex)
        } -> std::convertible_to<std::uint64_t>;
    };

template <typename Id, typename Traits>
bool idLess(const Id &lhs, const Id &rhs, const Traits &traits) noexcept {
    for (std::size_t byteIndex = 0; byteIndex < Traits::id_byte_count;
         ++byteIndex) {
        const uint8_t lhsByte = traits.byte_msb_first(lhs, byteIndex);
        const uint8_t rhsByte = traits.byte_msb_first(rhs, byteIndex);
        if (lhsByte != rhsByte) {
            return lhsByte < rhsByte;
        }
    }
    return false;
}

template <typename Id, typename Traits>
uint64_t buildChunkFromBytes(const Id &nodeId, std::size_t chunkIndex,
                             const Traits &traits) noexcept {
    uint64_t value = 0;
    const std::size_t firstByte = chunkIndex * chunk_byte_count;
    for (std::size_t offset = 0; offset < chunk_byte_count; ++offset) {
        const std::size_t byteIndex = firstByte + offset;
        value <<= radix_bits;
        if (byteIndex < Traits::id_byte_count) {
            value |= traits.byte_msb_first(nodeId, byteIndex);
        }
    }
    return value;
}

template <typename Id, typename Traits>
uint64_t chunkMsbFirst(const Id &nodeId, std::size_t chunkIndex,
                       const Traits &traits) noexcept {
    if constexpr (HasChunkMsbFirst<Traits, Id>) {
        return traits.chunk_msb_first(nodeId, chunkIndex);
    } else {
        return buildChunkFromBytes(nodeId, chunkIndex, traits);
    }
}

template <typename Id, typename Traits>
uint8_t idByteLsbPass(const Id &nodeId, std::size_t byteOffset,
                      const Traits &traits) noexcept {
    const std::size_t totalBytes = Traits::id_byte_count;
    const std::size_t byteIndexFromMsb = totalBytes - 1 - byteOffset;
    const std::size_t chunkIndex = byteIndexFromMsb / chunk_byte_count;
    const std::size_t byteInChunk = byteIndexFromMsb % chunk_byte_count;

    const uint64_t chunk = chunkMsbFirst(nodeId, chunkIndex, traits);
    return wordByte(chunk, chunk_byte_count - 1 - byteInChunk);
}

template <typename DigitForIndex>
void radixPass(std::vector<std::size_t> &order,
               std::vector<std::size_t> &scratch, DigitForIndex digitForIndex) {
    std::array<std::size_t, radix_bucket_count> counts{};
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

template <typename Entry, typename IdForEntry, typename IdTraits>
void radixSortEntriesById(std::vector<Entry> &entries, IdForEntry idForEntry,
                          const IdTraits &idTraits) {
    if (entries.size() <= 1) {
        return;
    }

    std::vector<Entry> scratch(entries.size());
    for (std::size_t byteOffset = 0; byteOffset < IdTraits::id_byte_count;
         ++byteOffset) {
        std::array<std::size_t, radix_bucket_count> counts{};
        for (const Entry &entry : entries) {
            ++counts[idByteLsbPass(idForEntry(entry), byteOffset, idTraits)];
        }

        std::size_t offset = 0;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = offset;
            offset += bucketSize;
        }

        for (const Entry &entry : entries) {
            const uint8_t digit =
                idByteLsbPass(idForEntry(entry), byteOffset, idTraits);
            scratch[counts[digit]] = entry;
            ++counts[digit];
        }

        entries.swap(scratch);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_HPP
