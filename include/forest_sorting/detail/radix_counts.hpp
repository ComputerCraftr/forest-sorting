#ifndef FOREST_SORTING_DETAIL_RADIX_COUNTS_HPP
#define FOREST_SORTING_DETAIL_RADIX_COUNTS_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::detail {

inline constexpr std::size_t radix_bits = 8;
inline constexpr std::size_t radix_bucket_count = std::size_t{1} << radix_bits;

struct FullClearCounts {};

template <std::size_t MaxRangeSize> struct BitmaskTouchedCountsUpTo {
    static constexpr std::size_t max_size = MaxRangeSize;
};

struct FullClearCountScratch {
    std::array<std::size_t, radix_bucket_count> counts{};
};

struct BitmaskTouchedCountScratch {
    static constexpr std::size_t mask_word_count = 4;

    std::array<std::size_t, radix_bucket_count> counts{};
    std::array<uint64_t, mask_word_count> touchedMask{};
};

inline void resetRadixCounts(FullClearCountScratch &countScratch) {
    countScratch.counts.fill(0);
}

inline void noteRadixDigit(FullClearCountScratch &countScratch, uint8_t digit) {
    ++countScratch.counts[digit];
}

inline void prefixRadixCounts(FullClearCountScratch &countScratch,
                              std::size_t startOffset = 0) {
    std::size_t writeOffset = startOffset;
    for (std::size_t &count : countScratch.counts) {
        const std::size_t bucketSize = count;
        count = writeOffset;
        writeOffset += bucketSize;
    }
}

inline void clearRadixCounts(FullClearCountScratch &countScratch) {
    (void)countScratch;
}

inline std::size_t
countNonZeroBuckets(const FullClearCountScratch &countScratch) noexcept {
    std::size_t nonZeroBuckets = 0;
    for (std::size_t bucketIdx = 0; bucketIdx < radix_bucket_count;
         bucketIdx += 4) {
        nonZeroBuckets +=
            static_cast<std::size_t>(countScratch.counts[bucketIdx + 0] != 0);
        nonZeroBuckets +=
            static_cast<std::size_t>(countScratch.counts[bucketIdx + 1] != 0);
        nonZeroBuckets +=
            static_cast<std::size_t>(countScratch.counts[bucketIdx + 2] != 0);
        nonZeroBuckets +=
            static_cast<std::size_t>(countScratch.counts[bucketIdx + 3] != 0);
    }
    return nonZeroBuckets;
}

inline void resetRadixCounts(BitmaskTouchedCountScratch &countScratch) {
    (void)countScratch;
}

inline void noteRadixDigit(BitmaskTouchedCountScratch &countScratch,
                           uint8_t digit) {
    ++countScratch.counts[digit];
    countScratch.touchedMask[digit >> 6U] |= uint64_t{1} << (digit & 63U);
}

inline void prefixRadixCounts(BitmaskTouchedCountScratch &countScratch,
                              std::size_t startOffset = 0) {
    std::size_t writeOffset = startOffset;
    for (std::size_t maskIdx = 0; maskIdx < countScratch.touchedMask.size();
         ++maskIdx) {
        uint64_t bits = countScratch.touchedMask[maskIdx];
        while (bits != 0) {
            const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
            const std::size_t bucketIdx = (maskIdx * 64U) + bit;
            const std::size_t bucketSize = countScratch.counts[bucketIdx];
            countScratch.counts[bucketIdx] = writeOffset;
            writeOffset += bucketSize;
            bits &= bits - uint64_t{1};
        }
    }
}

inline void clearRadixCounts(BitmaskTouchedCountScratch &countScratch) {
    for (std::size_t maskIdx = 0; maskIdx < countScratch.touchedMask.size();
         ++maskIdx) {
        uint64_t bits = countScratch.touchedMask[maskIdx];
        while (bits != 0) {
            const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
            countScratch.counts[(maskIdx * 64U) + bit] = 0;
            bits &= bits - uint64_t{1};
        }
        countScratch.touchedMask[maskIdx] = 0;
    }
}

inline std::size_t
countNonZeroBuckets(const BitmaskTouchedCountScratch &countScratch) noexcept {
    std::size_t nonZero = 0;
    for (uint64_t bits : countScratch.touchedMask) {
        nonZero += static_cast<std::size_t>(std::popcount(bits));
    }
    return nonZero;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_RADIX_COUNTS_HPP
