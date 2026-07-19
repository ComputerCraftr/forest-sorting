#ifndef FOREST_SORTING_TEST_SUPPORT_INSTRUMENTED_RADIX_COUNTS_HPP
#define FOREST_SORTING_TEST_SUPPORT_INSTRUMENTED_RADIX_COUNTS_HPP

#include "forest_sorting/detail/radix.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forest_sorting::test_support {

struct RadixCountOperations {
    std::size_t reset = 0;
    std::size_t note = 0;
    std::size_t prefix = 0;
    std::size_t clear = 0;
};

struct InstrumentedCounterPolicy {
    static inline RadixCountOperations *operations = nullptr;
};

struct InstrumentedCountScratch {
    std::array<std::size_t, detail::radix_bucket_count> counts{};
};

inline void resetRadixCounts(InstrumentedCountScratch &scratch) {
    scratch.counts.fill(0);
    ++InstrumentedCounterPolicy::operations->reset;
}

inline void noteRadixDigit(InstrumentedCountScratch &scratch, uint8_t digit) {
    ++scratch.counts[digit];
    ++InstrumentedCounterPolicy::operations->note;
}

inline void prefixRadixCounts(InstrumentedCountScratch &scratch,
                              std::size_t startOffset = 0) {
    std::size_t writeOffset = startOffset;
    for (std::size_t &count : scratch.counts) {
        const std::size_t bucketSize = count;
        count = writeOffset;
        writeOffset += bucketSize;
    }
    ++InstrumentedCounterPolicy::operations->prefix;
}

inline void clearRadixCounts(InstrumentedCountScratch &scratch) {
    (void)scratch;
    ++InstrumentedCounterPolicy::operations->clear;
}

inline std::size_t
countNonZeroBuckets(const InstrumentedCountScratch &scratch) noexcept {
    std::size_t count = 0;
    for (std::size_t bucketSize : scratch.counts) {
        count += static_cast<std::size_t>(bucketSize != 0);
    }
    return count;
}

inline void pushPartitionedRanges(
    std::vector<detail::RadixRange> &stack,
    const std::array<std::size_t, detail::radix_bucket_count> &bucketStarts,
    const InstrumentedCountScratch &scratch, std::size_t nextDigitIndex) {
    for (std::size_t reverseBucket = 0;
         reverseBucket < detail::radix_bucket_count; ++reverseBucket) {
        const std::size_t bucket =
            detail::radix_bucket_count - 1U - reverseBucket;
        const std::size_t begin = bucketStarts[bucket];
        const std::size_t end = scratch.counts[bucket];
        if (end > begin) {
            stack.push_back({begin, end, nextDigitIndex});
        }
    }
}

} // namespace forest_sorting::test_support

namespace forest_sorting::detail {

template <> struct RadixCounterTraits<test_support::InstrumentedCounterPolicy> {
    using policy_scratch_type = test_support::InstrumentedCountScratch;
    using workspace_scratch_type = EmptyScratch;
    static constexpr bool alwaysUsePolicy = true;

    [[nodiscard]] static constexpr bool
    usePolicyForRange(std::size_t rangeSize) noexcept {
        (void)rangeSize;
        return true;
    }
};

} // namespace forest_sorting::detail

#endif
