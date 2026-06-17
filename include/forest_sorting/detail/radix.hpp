#ifndef FOREST_SORTING_DETAIL_RADIX_HPP
#define FOREST_SORTING_DETAIL_RADIX_HPP

#include "forest_sorting/detail/constants.hpp"
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::detail {

inline constexpr std::size_t radix_bits = 8;
inline constexpr std::size_t radix_bucket_count = 256;
inline constexpr std::size_t chunk_byte_count = 8;

template <typename Word>
inline uint8_t wordByte(Word value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * radix_bits));
}

inline uint8_t depthByteMsbFirst(uint32_t depth,
                                 std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(depth >> ((3U - byteIndex) * 8U));
}

inline std::size_t countNonZeroRadixBuckets(
    const std::array<std::size_t, radix_bucket_count> &counts) noexcept {
    std::size_t nonZeroBuckets = 0;
    for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
         bucketIdx += 4) {
        nonZeroBuckets += static_cast<std::size_t>(counts[bucketIdx + 0] != 0);
        nonZeroBuckets += static_cast<std::size_t>(counts[bucketIdx + 1] != 0);
        nonZeroBuckets += static_cast<std::size_t>(counts[bucketIdx + 2] != 0);
        nonZeroBuckets += static_cast<std::size_t>(counts[bucketIdx + 3] != 0);
    }
    return nonZeroBuckets;
}

struct RadixRange {
    std::size_t begin;
    std::size_t end;
    std::size_t digitIndex;
};

template <typename DigitForOffset, typename MoveToScratch,
          typename CopyFromScratch, typename RangeDone,
          typename SmallRangeHandler>
void radixMsdPartitionCoreWithStack(
    std::size_t begin, std::size_t end, std::size_t firstDigit,
    std::size_t digitCount, std::vector<RadixRange> &stack,
    DigitForOffset digitForOffset, MoveToScratch moveToScratch,
    CopyFromScratch copyFromScratch, RangeDone rangeDone,
    SmallRangeHandler smallRangeHandler) {
    stack.clear();
    stack.push_back({begin, end, firstDigit});

    while (!stack.empty()) {
        const auto currentRange = stack.back();
        stack.pop_back();

        if (currentRange.end - currentRange.begin <= 1 ||
            currentRange.digitIndex == digitCount) {
            rangeDone(currentRange.begin, currentRange.end);
            continue;
        }

        if (smallRangeHandler(currentRange.begin, currentRange.end,
                              currentRange.digitIndex)) {
            continue;
        }

        std::array<std::size_t, radix_bucket_count> counts{};
        for (std::size_t offset = currentRange.begin; offset < currentRange.end;
             ++offset) {
            ++counts[digitForOffset(offset, currentRange.digitIndex)];
        }

        const std::size_t nonZeroBuckets = countNonZeroRadixBuckets(counts);

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
            const uint8_t digit =
                digitForOffset(offset, currentRange.digitIndex);
            moveToScratch(offset, bucketOffsets[digit]++);
        }

        copyFromScratch(currentRange.begin, currentRange.end);

        // Push in reverse order so that bucket 0 is popped and processed first.
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

template <typename DigitForOffset, typename MoveToScratch,
          typename CopyFromScratch, typename RangeDone>
void radixMsdPartitionCore(std::size_t begin, std::size_t end,
                           std::size_t firstDigit, std::size_t digitCount,
                           DigitForOffset digitForOffset,
                           MoveToScratch moveToScratch,
                           CopyFromScratch copyFromScratch,
                           RangeDone rangeDone) {
    std::vector<RadixRange> stack;
    stack.reserve(initial_range_stack_capacity);
    auto noSmallRange = [](std::size_t, std::size_t, std::size_t) {
        return false;
    };
    radixMsdPartitionCoreWithStack(begin, end, firstDigit, digitCount, stack,
                                   digitForOffset, moveToScratch,
                                   copyFromScratch, rangeDone, noSmallRange);
}

template <typename DigitForIndex, typename RangeDone>
void radixMsdPartitionRanges(std::vector<std::size_t> &order,
                             std::vector<std::size_t> &scratch,
                             std::size_t begin, std::size_t end,
                             std::size_t firstDigit, std::size_t digitCount,
                             DigitForIndex digitForIndex, RangeDone rangeDone) {
    auto digitForOffset = [&](std::size_t offset, std::size_t digitIndex) {
        return digitForIndex(order[offset], digitIndex);
    };
    auto moveToScratch = [&](std::size_t offset, std::size_t scratchOffset) {
        scratch[scratchOffset] = order[offset];
    };
    auto copyFromScratch = [&](std::size_t rangeBegin, std::size_t rangeEnd) {
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            order[offset] = scratch[offset];
        }
    };

    radixMsdPartitionCore(begin, end, firstDigit, digitCount, digitForOffset,
                          moveToScratch, copyFromScratch, rangeDone);
}

template <typename Traits, typename Id>
concept HasChunkMsbFirst =
    requires(const Traits &traits, const Id &nodeId, std::size_t chunkIndex) {
        {
            traits.chunk_msb_first(nodeId, chunkIndex)
        } -> std::convertible_to<std::uint64_t>;
    };

template <std::size_t ChunkBytes> struct ChunkValue;
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

template <std::size_t ChunkBytes>
using ChunkValueType = ChunkValue<ChunkBytes>::Type;

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

template <std::size_t ChunkBytes, typename Id, typename Traits>
ChunkValueType<ChunkBytes> buildChunkFromBytes(const Id &nodeId,
                                               std::size_t chunkIndex,
                                               const Traits &traits) noexcept {
    uint64_t value = 0;
    const std::size_t firstByte = chunkIndex * ChunkBytes;
    for (std::size_t offset = 0; offset < ChunkBytes; ++offset) {
        const std::size_t byteIndex = firstByte + offset;
        value <<= radix_bits;
        if (byteIndex < Traits::id_byte_count) {
            value |= traits.byte_msb_first(nodeId, byteIndex);
        }
    }
    return static_cast<ChunkValueType<ChunkBytes>>(value);
}

template <typename Traits, typename Id, std::size_t ChunkBytes>
concept HasTemplatedChunkMsbFirst =
    requires(const Traits &traits, const Id &nodeId, std::size_t chunkIndex) {
        {
            traits.template chunk_msb_first<ChunkBytes>(nodeId, chunkIndex)
        } -> std::convertible_to<ChunkValueType<ChunkBytes>>;
    };

template <std::size_t ChunkBytes = chunk_byte_count, typename Id,
          typename Traits>
ChunkValueType<ChunkBytes> chunkMsbFirst(const Id &nodeId,
                                         std::size_t chunkIndex,
                                         const Traits &traits) noexcept {
    if constexpr (HasTemplatedChunkMsbFirst<Traits, Id, ChunkBytes>) {
        return static_cast<ChunkValueType<ChunkBytes>>(
            traits.template chunk_msb_first<ChunkBytes>(nodeId, chunkIndex));
    } else if constexpr (ChunkBytes == chunk_byte_count &&
                         HasChunkMsbFirst<Traits, Id>) {
        return static_cast<ChunkValueType<ChunkBytes>>(
            traits.chunk_msb_first(nodeId, chunkIndex));
    } else {
        return buildChunkFromBytes<ChunkBytes>(nodeId, chunkIndex, traits);
    }
}

template <typename Entry, typename IdForEntry, typename IdTraits>
void radixMsdSortEntriesById(std::vector<Entry> &entries, IdForEntry idForEntry,
                             const IdTraits &idTraits) {
    if (entries.size() <= 1) {
        return;
    }

    std::vector<Entry> scratch(entries.size());
    auto digitForOffset = [&](std::size_t offset, std::size_t digitIndex) {
        return idTraits.byte_msb_first(idForEntry(entries[offset]), digitIndex);
    };
    auto moveToScratch = [&](std::size_t offset, std::size_t scratchOffset) {
        scratch[scratchOffset] = entries[offset];
    };
    auto copyFromScratch = [&](std::size_t rangeBegin, std::size_t rangeEnd) {
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            entries[offset] = scratch[offset];
        }
    };
    auto rangeDone = [](std::size_t, std::size_t) {};

    radixMsdPartitionCore(0, entries.size(), 0, IdTraits::id_byte_count,
                          digitForOffset, moveToScratch, copyFromScratch,
                          rangeDone);
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_HPP
