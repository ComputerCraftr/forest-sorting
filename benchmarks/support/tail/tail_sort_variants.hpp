#ifndef FOREST_SORTING_SUPPORT_TAIL_SORT_VARIANTS_HPP
#define FOREST_SORTING_SUPPORT_TAIL_SORT_VARIANTS_HPP

#include "full/adaptive_sort_variants.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace forest_sorting::test_support {

template <std::size_t... Gaps> consteval bool validShellGapSequence() {
    constexpr std::array<std::size_t, sizeof...(Gaps)> gaps = {Gaps...};
    if (gaps.empty() || gaps.back() != 1) {
        return false;
    }
    for (std::size_t gapIdx = 0; gapIdx < gaps.size(); ++gapIdx) {
        if (gaps[gapIdx] == 0) {
            return false;
        }
        if (gapIdx > 0 && gaps[gapIdx - 1] <= gaps[gapIdx]) {
            return false;
        }
    }
    return true;
}

template <std::size_t... Gaps, typename ScratchAccessor>
void sortRangeSmallShellInternal(std::vector<std::size_t> &order,
                                 std::size_t rangeBegin, std::size_t rangeEnd,
                                 ScratchAccessor &accessor) {
    static_assert(validShellGapSequence<Gaps...>());
    constexpr std::array<std::size_t, sizeof...(Gaps)> gaps = {Gaps...};
    const std::size_t rangeSize = rangeEnd - rangeBegin;

    for (std::size_t gap : gaps) {
        if (gap >= rangeSize) {
            continue;
        }
        for (std::size_t rangeIdx = rangeBegin + gap; rangeIdx < rangeEnd;
             ++rangeIdx) {
            const std::size_t localIdx = rangeIdx - rangeBegin;
            const std::size_t itemIndex = order[rangeIdx];
            accessor.save(localIdx);

            std::size_t innerIdx = rangeIdx;
            while (innerIdx >= rangeBegin + gap) {
                const std::size_t previousIdx = innerIdx - gap;
                const std::size_t previousLocalIdx = previousIdx - rangeBegin;
                if (accessor.isLessOrEqual(previousLocalIdx)) {
                    break;
                }
                order[innerIdx] = order[previousIdx];
                accessor.move(previousLocalIdx, innerIdx - rangeBegin);
                innerIdx = previousIdx;
            }
            order[innerIdx] = itemIndex;
            accessor.writeSaved(innerIdx - rangeBegin);
        }
    }
}

template <std::size_t... Gaps> struct ShellGapSmallSorterDynamic {
    static_assert(validShellGapSequence<Gaps...>());

    template <typename Nodes, typename IdTraits>
    void operator()(std::vector<std::size_t> &order, const Nodes &nodes,
                    const IdTraits &traits, std::size_t rangeBegin,
                    std::size_t rangeEnd) const {
        withDynamicNodeSmallSortAccessor(
            order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
                sortRangeSmallShellInternal<Gaps...>(order, rangeBegin,
                                                     rangeEnd, accessor);
            });
    }
};

using ShellGap10_4_1SmallSorterDynamic = ShellGapSmallSorterDynamic<10, 4, 1>;
using ShellGap3_2_1SmallSorterDynamic = ShellGapSmallSorterDynamic<3, 2, 1>;
using ShellGap16_7_3_1SmallSorterDynamic =
    ShellGapSmallSorterDynamic<16, 7, 3, 1>;

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_TAIL_SORT_VARIANTS_HPP
