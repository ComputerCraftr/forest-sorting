#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_SORT_VARIANTS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_SORT_VARIANTS_HPP

#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/id_small_sort.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace forest_sorting::benchmark_support {

template <std::size_t MaxRangeSize, typename Nodes, typename IdTraits,
          typename Algorithm>
void withFixedNodeSmallSortAccessor(const std::vector<std::size_t> &order,
                                    const Nodes &nodes, const IdTraits &traits,
                                    std::size_t rangeBegin,
                                    std::size_t rangeEnd, Algorithm algorithm) {
    auto idForIndex = [&](std::size_t itemIndex) {
        return traits.id(nodes[itemIndex]);
    };
    detail::withFixedSmallSortAccessor<MaxRangeSize>(
        order, idForIndex, traits, rangeBegin, rangeEnd, algorithm);
}

template <typename ScratchAccessor>
void stableSortRangeSmallBinaryInternal(std::vector<std::size_t> &order,
                                        std::size_t rangeBegin,
                                        std::size_t rangeEnd,
                                        ScratchAccessor &accessor) {
    for (std::size_t rangeIndex = rangeBegin + 1; rangeIndex < rangeEnd;
         ++rangeIndex) {
        const std::size_t localIndex = rangeIndex - rangeBegin;
        const std::size_t nodeIndex = order[rangeIndex];
        accessor.save(localIndex);
        if (accessor.isLessOrEqual(localIndex - 1)) {
            continue;
        }

        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIndex;
        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            if (accessor.isLessOrEqual(middle - rangeBegin)) {
                searchBegin = middle + 1;
            } else {
                searchEnd = middle;
            }
        }

        for (std::size_t moveIndex = rangeIndex; moveIndex > searchBegin;
             --moveIndex) {
            const std::size_t localMoveIndex = moveIndex - rangeBegin;
            order[moveIndex] = order[moveIndex - 1];
            accessor.move(localMoveIndex - 1, localMoveIndex);
        }
        order[searchBegin] = nodeIndex;
        accessor.writeSaved(searchBegin - rangeBegin);
    }
}

template <std::size_t MaxRangeSize, typename Nodes, typename IdTraits>
void stableSortRangeSmallBinary(std::vector<std::size_t> &order,
                                const Nodes &nodes, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd) {
    withFixedNodeSmallSortAccessor<MaxRangeSize>(
        order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
            stableSortRangeSmallBinaryInternal(order, rangeBegin, rangeEnd,
                                               accessor);
        });
}

template <typename ScratchAccessor>
void stableSortRangeSmallExponentialInternal(std::vector<std::size_t> &order,
                                             std::size_t rangeBegin,
                                             std::size_t rangeEnd,
                                             ScratchAccessor &accessor) {
    for (std::size_t rangeIndex = rangeBegin + 1; rangeIndex < rangeEnd;
         ++rangeIndex) {
        const std::size_t localIndex = rangeIndex - rangeBegin;
        const std::size_t nodeIndex = order[rangeIndex];
        accessor.save(localIndex);
        if (accessor.isLessOrEqual(localIndex - 1)) {
            continue;
        }

        const std::size_t sortedPrefixSize = rangeIndex - rangeBegin;
        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIndex - 1;
        for (std::size_t bound = 2; bound <= sortedPrefixSize; bound <<= 1) {
            const std::size_t probe = rangeIndex - bound;
            if (accessor.isLessOrEqual(probe - rangeBegin)) {
                searchBegin = probe + 1;
                break;
            }
            searchEnd = probe;
        }

        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            if (accessor.isLessOrEqual(middle - rangeBegin)) {
                searchBegin = middle + 1;
            } else {
                searchEnd = middle;
            }
        }

        for (std::size_t moveIndex = rangeIndex; moveIndex > searchBegin;
             --moveIndex) {
            const std::size_t localMoveIndex = moveIndex - rangeBegin;
            order[moveIndex] = order[moveIndex - 1];
            accessor.move(localMoveIndex - 1, localMoveIndex);
        }
        order[searchBegin] = nodeIndex;
        accessor.writeSaved(searchBegin - rangeBegin);
    }
}

template <std::size_t MaxRangeSize, typename Nodes, typename IdTraits>
void stableSortRangeSmallExponential(std::vector<std::size_t> &order,
                                     const Nodes &nodes, const IdTraits &traits,
                                     std::size_t rangeBegin,
                                     std::size_t rangeEnd) {
    withFixedNodeSmallSortAccessor<MaxRangeSize>(
        order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
            stableSortRangeSmallExponentialInternal(order, rangeBegin, rangeEnd,
                                                    accessor);
        });
}

struct PendingBitRange {
    std::size_t begin;
    std::size_t end;
    std::size_t bitIndex;
};

template <std::size_t MaxRangeSize, typename Nodes, typename IdTraits>
void stableSortRangeSmallBranchlessBitwise(std::vector<std::size_t> &order,
                                           const Nodes &nodes,
                                           const IdTraits &traits,
                                           std::size_t rangeBegin,
                                           std::size_t rangeEnd) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }
    if (rangeSize > MaxRangeSize) {
        throw std::runtime_error(
            "small sorter range exceeds fixed scratch capacity");
    }

    constexpr std::size_t byteCount = IdTraits::id_byte_count;
    constexpr std::size_t totalBits = byteCount * 8;
    std::array<PendingBitRange, MaxRangeSize> pending{};
    std::array<std::size_t, MaxRangeSize> localOrder{};
    std::array<std::size_t, MaxRangeSize> scratchOrder{};
    std::array<std::size_t, MaxRangeSize> nodeIndices{};
    std::array<uint8_t, MaxRangeSize> bits{};
    std::array<std::array<uint8_t, byteCount>, MaxRangeSize> idBytes{};

    for (std::size_t index = 0; index < rangeSize; ++index) {
        localOrder[index] = index;
        const std::size_t nodeIndex = order[rangeBegin + index];
        nodeIndices[index] = nodeIndex;
        const auto nodeId = traits.id(nodes[nodeIndex]);
        for (std::size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
            idBytes[index][byteIndex] =
                traits.byte_msb_first(nodeId, byteIndex);
        }
    }

    std::size_t pendingCount = 1;
    pending[0] = {0, rangeSize, 0};
    auto pushRange = [&](std::size_t begin, std::size_t end,
                         std::size_t nextBit) {
        if (end - begin > 1 && nextBit < totalBits) {
            pending[pendingCount++] = {begin, end, nextBit};
        }
    };

    while (pendingCount > 0) {
        const PendingBitRange current = pending[--pendingCount];
        const std::size_t currentSize = current.end - current.begin;
        std::size_t splitBit = totalBits;
        for (std::size_t byteIndex = current.bitIndex / 8;
             byteIndex < byteCount; ++byteIndex) {
            const uint8_t baseByte =
                idBytes[localOrder[current.begin]][byteIndex];
            uint8_t differingBits = 0;
            for (std::size_t index = 1; index < currentSize; ++index) {
                differingBits |= static_cast<uint8_t>(
                    baseByte ^
                    idBytes[localOrder[current.begin + index]][byteIndex]);
            }
            if (byteIndex == current.bitIndex / 8) {
                const std::size_t startingBit = 7 - (current.bitIndex % 8);
                differingBits &= static_cast<uint8_t>(
                    (uint32_t{1} << (startingBit + 1U)) - 1U);
            }
            if (differingBits == 0) {
                continue;
            }
            for (std::size_t bit = 0; bit < 8; ++bit) {
                if (((differingBits >> (7U - bit)) & 1U) != 0U) {
                    splitBit = (byteIndex * 8) + bit;
                    break;
                }
            }
            break;
        }
        if (splitBit == totalBits) {
            continue;
        }

        const std::size_t byteIndex = splitBit / 8;
        const std::size_t bitOffset = 7 - (splitBit % 8);
        std::size_t zeroCount = 0;
        for (std::size_t index = 0; index < currentSize; ++index) {
            const std::size_t ordinal = localOrder[current.begin + index];
            const std::size_t bit = static_cast<std::size_t>(
                (idBytes[ordinal][byteIndex] >> bitOffset) & 1U);
            bits[index] = static_cast<uint8_t>(bit);
            zeroCount += bit ^ 1U;
        }

        std::size_t zeroWrite = 0;
        std::size_t oneWrite = zeroCount;
        for (std::size_t index = 0; index < currentSize; ++index) {
            const std::size_t ordinal = localOrder[current.begin + index];
            const std::size_t bit = bits[index];
            const std::size_t mask = std::size_t{0} - bit;
            const std::size_t destination =
                zeroWrite ^ ((zeroWrite ^ oneWrite) & mask);
            scratchOrder[destination] = ordinal;
            zeroWrite += bit ^ 1U;
            oneWrite += bit;
        }
        for (std::size_t index = 0; index < currentSize; ++index) {
            localOrder[current.begin + index] = scratchOrder[index];
        }

        const std::size_t nextBit = splitBit + 1;
        pushRange(current.begin + zeroCount, current.end, nextBit);
        pushRange(current.begin, current.begin + zeroCount, nextBit);
    }

    for (std::size_t index = 0; index < rangeSize; ++index) {
        order[rangeBegin + index] = nodeIndices[localOrder[index]];
    }
}

template <std::size_t... Gaps> consteval bool validShellGapSequence() {
    constexpr std::array<std::size_t, sizeof...(Gaps)> gaps = {Gaps...};
    if (gaps.empty() || gaps.back() != 1) {
        return false;
    }
    for (std::size_t index = 0; index < gaps.size(); ++index) {
        if (gaps[index] == 0 || (index > 0 && gaps[index - 1] <= gaps[index])) {
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
        for (std::size_t rangeIndex = rangeBegin + gap; rangeIndex < rangeEnd;
             ++rangeIndex) {
            const std::size_t localIndex = rangeIndex - rangeBegin;
            const std::size_t itemIndex = order[rangeIndex];
            accessor.save(localIndex);
            std::size_t innerIndex = rangeIndex;
            while (innerIndex >= rangeBegin + gap) {
                const std::size_t previousIndex = innerIndex - gap;
                if (accessor.isLessOrEqual(previousIndex - rangeBegin)) {
                    break;
                }
                order[innerIndex] = order[previousIndex];
                accessor.move(previousIndex - rangeBegin,
                              innerIndex - rangeBegin);
                innerIndex = previousIndex;
            }
            order[innerIndex] = itemIndex;
            accessor.writeSaved(innerIndex - rangeBegin);
        }
    }
}

template <std::size_t MaxRangeSize = detail::small_id_range_sort_threshold>
struct LinearSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        detail::stableSortRangeSmallLinear<MaxRangeSize>(order, nodes, traits,
                                                         begin, end);
    }
};

template <std::size_t MaxRangeSize> struct BinarySmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBinary<MaxRangeSize>(order, nodes, traits, begin,
                                                 end);
    }
};

template <std::size_t MaxRangeSize> struct ExponentialSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallExponential<MaxRangeSize>(order, nodes, traits,
                                                      begin, end);
    }
};

template <std::size_t MaxRangeSize> struct BranchlessBitwiseSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBranchlessBitwise<MaxRangeSize>(order, nodes,
                                                            traits, begin, end);
    }
};

template <std::size_t MaxRangeSize, std::size_t... Gaps>
struct ShellGapSmallSorter {
    static_assert(validShellGapSequence<Gaps...>());

    template <typename Nodes, typename IdTraits>
    void operator()(std::vector<std::size_t> &order, const Nodes &nodes,
                    const IdTraits &traits, std::size_t rangeBegin,
                    std::size_t rangeEnd) const {
        withFixedNodeSmallSortAccessor<MaxRangeSize>(
            order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
                sortRangeSmallShellInternal<Gaps...>(order, rangeBegin,
                                                     rangeEnd, accessor);
            });
    }
};

using ShellGap10_4_1SmallSorter = ShellGapSmallSorter<32, 10, 4, 1>;
using ShellGap3_2_1SmallSorter = ShellGapSmallSorter<32, 3, 2, 1>;
using ShellGap16_7_3_1SmallSorter = ShellGapSmallSorter<32, 16, 7, 3, 1>;

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_SORT_VARIANTS_HPP
