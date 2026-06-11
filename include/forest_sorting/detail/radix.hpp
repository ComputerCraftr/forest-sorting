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

inline uint8_t depthByteMsbFirst(uint32_t depth,
                                 std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(depth >> ((3U - byteIndex) * 8U));
}

struct RadixRange {
    std::size_t begin;
    std::size_t end;
    std::size_t digitIndex;
};

template <typename DigitForIndex, typename RangeDone>
void radixMsdPartitionRanges(std::vector<std::size_t> &order,
                             std::vector<std::size_t> &scratch,
                             std::size_t begin, std::size_t end,
                             std::size_t firstDigit, std::size_t digitCount,
                             DigitForIndex digitForIndex, RangeDone rangeDone) {
    std::vector<RadixRange> stack;
    stack.reserve(128);
    stack.push_back({begin, end, firstDigit});

    while (!stack.empty()) {
        const auto currentRange = stack.back();
        stack.pop_back();

        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.digitIndex == digitCount) {
            rangeDone(currentRange.begin, currentRange.end);
            continue;
        }

        std::array<std::size_t, radix_bucket_count> counts{};
        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            ++counts[digitForIndex(order[offset], currentRange.digitIndex)];
        }

        std::size_t nonZeroBuckets = 0;
        for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
             ++bucketIdx) {
            if (counts[bucketIdx] > 0) {
                nonZeroBuckets++;
            }
        }

        if (nonZeroBuckets == 0) {
            continue;
        }

        if (nonZeroBuckets == 1) {
            stack.push_back({currentRange.begin, currentRange.end,
                             currentRange.digitIndex + 1});
            continue;
        }

        std::array<std::size_t, radix_bucket_count> bucketStarts{};
        std::size_t currentOffset = currentRange.begin;
        for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
             ++bucketIdx) {
            bucketStarts[bucketIdx] = currentOffset;
            currentOffset += counts[bucketIdx];
        }

        std::array<std::size_t, radix_bucket_count> bucketOffsets =
            bucketStarts;
        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            const std::size_t nodeIdx = order[offset];
            const uint8_t digit =
                digitForIndex(nodeIdx, currentRange.digitIndex);
            scratch[bucketOffsets[digit]++] = nodeIdx;
        }

        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            order[offset] = scratch[offset];
        }

        // Push in reverse order so that bucket 0 is popped and processed first
        for (std::size_t reverseBucketIdx = 0;
             reverseBucketIdx < radix_bucket_count; ++reverseBucketIdx) {
            const std::size_t bucketIdx =
                radix_bucket_count - 1 - reverseBucketIdx;
            if (counts[bucketIdx] > 0) {
                stack.push_back({bucketStarts[bucketIdx],
                                 bucketOffsets[bucketIdx],
                                 currentRange.digitIndex + 1});
            }
        }
    }
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

template <typename Entry, typename IdForEntry, typename IdTraits>
void radixMsdSortEntriesById(std::vector<Entry> &entries, IdForEntry idForEntry,
                             const IdTraits &idTraits) {
    if (entries.size() <= 1) {
        return;
    }

    std::vector<Entry> scratch(entries.size());
    std::vector<RadixRange> stack;
    stack.reserve(128);
    stack.push_back({0, entries.size(), 0});

    while (!stack.empty()) {
        const RadixRange currentRange = stack.back();
        stack.pop_back();

        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.digitIndex == IdTraits::id_byte_count) {
            continue;
        }

        std::array<std::size_t, radix_bucket_count> counts{};
        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            ++counts[idTraits.byte_msb_first(idForEntry(entries[offset]),
                                             currentRange.digitIndex)];
        }

        std::size_t nonZeroBuckets = 0;
        for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
             ++bucketIdx) {
            if (counts[bucketIdx] > 0) {
                ++nonZeroBuckets;
            }
        }
        if (nonZeroBuckets == 0) {
            continue;
        }
        if (nonZeroBuckets == 1) {
            stack.push_back({currentRange.begin, currentRange.end,
                             currentRange.digitIndex + 1});
            continue;
        }

        std::array<std::size_t, radix_bucket_count> bucketStarts{};
        std::size_t writeOffset = currentRange.begin;
        for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
             ++bucketIdx) {
            bucketStarts[bucketIdx] = writeOffset;
            writeOffset += counts[bucketIdx];
        }

        std::array<std::size_t, radix_bucket_count> bucketOffsets =
            bucketStarts;
        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            const Entry &entry = entries[offset];
            const uint8_t digit = idTraits.byte_msb_first(
                idForEntry(entry), currentRange.digitIndex);
            scratch[bucketOffsets[digit]++] = entry;
        }

        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            entries[offset] = scratch[offset];
        }

        for (std::size_t reverseBucketIdx = 0;
             reverseBucketIdx < radix_bucket_count; ++reverseBucketIdx) {
            const std::size_t bucketIdx =
                radix_bucket_count - 1 - reverseBucketIdx;
            if (counts[bucketIdx] > 0) {
                stack.push_back({bucketStarts[bucketIdx],
                                 bucketOffsets[bucketIdx],
                                 currentRange.digitIndex + 1});
            }
        }
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_HPP
