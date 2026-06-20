#ifndef FOREST_SORTING_DETAIL_RADIX_HPP
#define FOREST_SORTING_DETAIL_RADIX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace forest_sorting::detail {

inline constexpr std::size_t chunk_byte_count = 8;

template <typename Word>
inline uint8_t wordByte(Word value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * radix_bits));
}

template <std::size_t DepthPrefixBytes, typename Depth>
inline uint8_t depthByteMsbFirst(Depth depth, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(
        depth >> ((DepthPrefixBytes - 1U - byteIndex) * radix_bits));
}

struct RadixRange {
    std::size_t begin;
    std::size_t end;
    std::size_t digitIndex;
};

inline void pushPartitionedRanges(
    std::vector<RadixRange> &stack,
    const std::array<std::size_t, radix_bucket_count> &bucketStarts,
    const FullClearCountScratch &countScratch, std::size_t nextDigitIndex) {
    for (std::size_t reverseBucketIdx = 0;
         reverseBucketIdx < radix_bucket_count; ++reverseBucketIdx) {
        const std::size_t bucketIdx = radix_bucket_count - 1 - reverseBucketIdx;
        const std::size_t start = bucketStarts[bucketIdx];
        const std::size_t end = countScratch.counts[bucketIdx];
        if (end > start) {
            stack.push_back({start, end, nextDigitIndex});
        }
    }
}

inline void pushPartitionedRanges(
    std::vector<RadixRange> &stack,
    const std::array<std::size_t, radix_bucket_count> &bucketStarts,
    const BitmaskTouchedCountScratch &countScratch,
    std::size_t nextDigitIndex) {
    for (std::size_t reverseMaskIdx = 0; reverseMaskIdx < 4; ++reverseMaskIdx) {
        const std::size_t maskIdx = 3 - reverseMaskIdx;
        uint64_t bits = countScratch.touchedMask[maskIdx];
        while (bits != 0) {
            const auto leadingZeros = std::countl_zero(bits);
            const std::size_t bit =
                63U - static_cast<std::size_t>(leadingZeros);
            const std::size_t bucketIdx = (maskIdx * 64U) + bit;
            const std::size_t start = bucketStarts[bucketIdx];
            const std::size_t end = countScratch.counts[bucketIdx];
            if (end > start) {
                stack.push_back({start, end, nextDigitIndex});
            }
            bits ^= uint64_t{1} << bit;
        }
    }
}

template <typename CountScratch, typename DigitForOffset,
          typename MoveToScratch, typename CopyFromScratch>
void partitionSingleRange(const RadixRange &currentRange,
                          std::vector<RadixRange> &stack,
                          CountScratch &countScratch,
                          DigitForOffset digitForOffset,
                          MoveToScratch moveToScratch,
                          CopyFromScratch copyFromScratch) {
    resetRadixCounts(countScratch);
    for (std::size_t offset = currentRange.begin; offset < currentRange.end;
         ++offset) {
        noteRadixDigit(countScratch,
                       digitForOffset(offset, currentRange.digitIndex));
    }

    const std::size_t nonZeroBuckets = countNonZeroBuckets(countScratch);

    if (nonZeroBuckets == 0) {
        return;
    }

    if (nonZeroBuckets == 1) {
        stack.push_back({currentRange.begin, currentRange.end,
                         currentRange.digitIndex + 1});
        clearRadixCounts(countScratch);
        return;
    }

    prefixRadixCounts(countScratch, currentRange.begin);

    std::array<std::size_t, radix_bucket_count> bucketStarts =
        countScratch.counts;

    for (std::size_t offset = currentRange.begin; offset < currentRange.end;
         ++offset) {
        const uint8_t digit = digitForOffset(offset, currentRange.digitIndex);
        moveToScratch(offset, countScratch.counts[digit]++);
    }

    copyFromScratch(currentRange.begin, currentRange.end);

    pushPartitionedRanges(stack, bucketStarts, countScratch,
                          currentRange.digitIndex + 1);

    clearRadixCounts(countScratch);
}

template <typename CountPolicy = FullClearCounts, typename DigitForOffset,
          typename MoveToScratch, typename CopyFromScratch, typename RangeDone,
          typename SmallRangeHandler>
void radixMsdPartitionCoreWithStack(
    std::size_t begin, std::size_t end, std::size_t firstDigit,
    std::size_t digitCount, std::vector<RadixRange> &stack,
    DigitForOffset digitForOffset, MoveToScratch moveToScratch,
    CopyFromScratch copyFromScratch, RangeDone rangeDone,
    SmallRangeHandler smallRangeHandler) {
    stack.clear();
    stack.push_back({begin, end, firstDigit});

    FullClearCountScratch fullClearScratch;
    using BitmaskScratchType =
        std::conditional_t<std::is_same_v<CountPolicy, FullClearCounts>,
                           EmptyScratch, BitmaskTouchedCountScratch>;
    BitmaskScratchType bitmaskScratch;

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

        const std::size_t rangeSize = currentRange.end - currentRange.begin;
        if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
            partitionSingleRange(currentRange, stack, fullClearScratch,
                                 digitForOffset, moveToScratch,
                                 copyFromScratch);
        } else {
            if (rangeSize <= CountPolicy::max_size) {
                partitionSingleRange(currentRange, stack, bitmaskScratch,
                                     digitForOffset, moveToScratch,
                                     copyFromScratch);
            } else {
                partitionSingleRange(currentRange, stack, fullClearScratch,
                                     digitForOffset, moveToScratch,
                                     copyFromScratch);
            }
        }
    }
}

template <typename CountPolicy = FullClearCounts, typename DigitForOffset,
          typename MoveToScratch, typename CopyFromScratch, typename RangeDone>
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
    radixMsdPartitionCoreWithStack<CountPolicy>(
        begin, end, firstDigit, digitCount, stack, digitForOffset,
        moveToScratch, copyFromScratch, rangeDone, noSmallRange);
}

template <typename CountPolicy = FullClearCounts, typename DigitForIndex,
          typename RangeDone>
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

    radixMsdPartitionCore<CountPolicy>(begin, end, firstDigit, digitCount,
                                       digitForOffset, moveToScratch,
                                       copyFromScratch, rangeDone);
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

template <typename Id, typename Traits>
inline int compareIdsMsbFirst(const Id &lhs, const Id &rhs,
                              const Traits &traits) noexcept {
    constexpr std::size_t chunkCount =
        (Traits::id_byte_count + chunk_byte_count - 1) / chunk_byte_count;
    for (std::size_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {
        const uint64_t lhsChunk = chunkMsbFirst(lhs, chunkIdx, traits);
        const uint64_t rhsChunk = chunkMsbFirst(rhs, chunkIdx, traits);
        if (lhsChunk < rhsChunk) {
            return -1;
        }
        if (lhsChunk > rhsChunk) {
            return 1;
        }
    }
    return 0;
}

template <typename Id, typename Traits>
bool idLess(const Id &lhs, const Id &rhs, const Traits &traits) noexcept {
    return compareIdsMsbFirst(lhs, rhs, traits) < 0;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_HPP
