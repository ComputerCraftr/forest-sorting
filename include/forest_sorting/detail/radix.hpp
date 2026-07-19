#ifndef FOREST_SORTING_DETAIL_RADIX_HPP
#define FOREST_SORTING_DETAIL_RADIX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::detail {

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

    using CounterTraits = RadixCounterTraits<CountPolicy>;
    typename CounterTraits::policy_scratch_type policyScratch;
    FullClearCountScratch fallbackScratch;

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

        if constexpr (CounterTraits::alwaysUsePolicy) {
            partitionSingleRange(currentRange, stack, policyScratch,
                                 digitForOffset, moveToScratch,
                                 copyFromScratch);
        } else {
            const std::size_t rangeSize = currentRange.end - currentRange.begin;
            if (CounterTraits::usePolicyForRange(rangeSize)) {
                partitionSingleRange(currentRange, stack, policyScratch,
                                     digitForOffset, moveToScratch,
                                     copyFromScratch);
            } else {
                partitionSingleRange(currentRange, stack, fallbackScratch,
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
    assert(begin <= end);
    assert(end <= order.size());
    assert(end <= scratch.size());
    assert(firstDigit <= digitCount);
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

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_HPP
