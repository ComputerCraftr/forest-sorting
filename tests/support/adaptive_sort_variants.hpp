#ifndef FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
#define FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "sort_baselines.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <type_traits>
#include <vector>

namespace forest_sorting::test_support {

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallBinary(std::vector<std::size_t> &order,
                                const Nodes &nodes, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);

        const std::size_t previousIndex = order[rangeIdx - 1];
        if (detail::compareIdsMsbFirst(traits.id(nodes[previousIndex]), idValue,
                                       traits) <= 0) {
            continue;
        }

        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx;
        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const bool insertAfterMiddle =
                detail::compareIdsMsbFirst(traits.id(nodes[order[middle]]),
                                           idValue, traits) <= 0;
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            order[moveIdx] = order[moveIdx - 1];
        }
        order[searchBegin] = nodeIndex;
    }
}

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallExponential(std::vector<std::size_t> &order,
                                     const Nodes &nodes, const IdTraits &traits,
                                     std::size_t rangeBegin,
                                     std::size_t rangeEnd) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t nodeIndex = order[rangeIdx];
        const auto &idValue = traits.id(nodes[nodeIndex]);

        const std::size_t previousIndex = order[rangeIdx - 1];
        if (detail::compareIdsMsbFirst(traits.id(nodes[previousIndex]), idValue,
                                       traits) <= 0) {
            continue;
        }

        const std::size_t sortedPrefixSize = rangeIdx - rangeBegin;
        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx - 1;
        for (std::size_t bound = 2; bound <= sortedPrefixSize; bound <<= 1) {
            const std::size_t probe = rangeIdx - bound;
            if (detail::compareIdsMsbFirst(traits.id(nodes[order[probe]]),
                                           idValue, traits) <= 0) {
                searchBegin = probe + 1;
                break;
            }
            searchEnd = probe;
        }

        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const bool insertAfterMiddle =
                detail::compareIdsMsbFirst(traits.id(nodes[order[middle]]),
                                           idValue, traits) <= 0;
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            order[moveIdx] = order[moveIdx - 1];
        }
        order[searchBegin] = nodeIndex;
    }
}

template <std::size_t DepthPrefixBytes,
          typename CountPolicy = detail::FullClearCounts, typename RangeSorter>
inline std::vector<Node> sortForestByAdaptiveRangeSorterWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    bool allowDenseDepthGrouping, RangeSorter rangeSorter) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    auto computed = detail::computeDepths<DepthPrefixBytes>(
        nodes, parentIndex, UInt128NodeTraits{});
    const auto &depths = computed.values;
    const uint32_t observedMaxDepth = computed.observedMax;

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    std::vector<std::size_t> scratch(nodeCount);

    using Depth = detail::DepthValue<DepthPrefixBytes>;
    std::vector<detail::DepthRange<Depth>> depthRanges;
    depthRanges.reserve(detail::initial_range_stack_capacity);
    std::vector<std::size_t> depthStarts;
    std::vector<std::size_t> depthOffsets;
    if (allowDenseDepthGrouping &&
        detail::shouldUseDenseDepthGrouping(order.size(), observedMaxDepth)) {
        detail::groupOrderByDepthDense(order, scratch, depths,
                                       static_cast<Depth>(observedMaxDepth),
                                       depthRanges, depthStarts, depthOffsets);
    } else {
        detail::groupOrderByDepthMsd<DepthPrefixBytes, CountPolicy>(
            order, scratch, depths, depthRanges);
    }

    rangeSorter(order, nodes, depthRanges);

    return materializeOrder(nodes, order);
}

using detail::EmptyScratch;

template <std::size_t ChunkBytes,
          typename CountPolicy = detail::FullClearCounts>
struct AdaptiveChunkWorkspace {
    std::vector<detail::IdChunkRange> pending;
    std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]> current;
    std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]> next;

    using ScratchType =
        std::conditional_t<std::is_same_v<CountPolicy, detail::FullClearCounts>,
                           EmptyScratch, detail::BitmaskTouchedCountScratch>;

    ScratchType touchedScratch;

    void allocate(std::size_t rangeSize) {
        pending.reserve(detail::initial_range_stack_capacity);
        if (rangeSize == 0) {
            return;
        }
        current = std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]>(
            new detail::ChunkedIndex<ChunkBytes>[rangeSize]);
        next = std::unique_ptr<detail::ChunkedIndex<ChunkBytes>[]>(
            new detail::ChunkedIndex<ChunkBytes>[rangeSize]);
    }
};

template <std::size_t ChunkBytes = detail::chunk_byte_count,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter, typename Depth>
inline void sortDepthRangesByChunkMsd(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges,
    SmallRangeSorter smallRangeSorter) {
    std::size_t maxRadixRangeSize = 0;
    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize > SmallThreshold) {
            maxRadixRangeSize = std::max(maxRadixRangeSize, rangeSize);
        }
    }

    AdaptiveChunkWorkspace<ChunkBytes, CountPolicy> workspace;
    workspace.allocate(maxRadixRangeSize);

    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        detail::sortRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy,
                                                   SmallThreshold>(
            order, nodes, UInt128NodeTraits{}, rangeBegin, rangeEnd, 0,
            workspace.pending, workspace.current.get(), workspace.next.get(),
            workspace.touchedScratch, smallRangeSorter);
    }
}

template <std::size_t ChunkBytes, typename CountPolicy,
          std::size_t SmallThreshold, typename SmallRangeSorter, typename Depth>
inline void sortDepthRangesWithTunedParams(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges) {
    SmallRangeSorter smallRangeSorter;
    sortDepthRangesByChunkMsd<ChunkBytes, CountPolicy, SmallThreshold>(
        order, nodes, depthRanges, smallRangeSorter);
}

struct LinearSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        detail::stableSortRangeSmallLinear(order, nodes, traits, begin, end);
    }
};

struct BinarySmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBinary(order, nodes, traits, begin, end);
    }
};

struct ExponentialSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallExponential(order, nodes, traits, begin, end);
    }
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

    struct PendingBitRange {
        std::size_t begin;
        std::size_t end;
        std::size_t bitIndex;
    };

    using CachedIdBytes = std::array<uint8_t, IdTraits::id_byte_count>;

    std::array<PendingBitRange, MaxRangeSize> pending;
    std::array<std::size_t, MaxRangeSize> localOrder;
    std::array<std::size_t, MaxRangeSize> scratchOrder;
    std::array<std::size_t, MaxRangeSize> nodeIndices;
    std::array<uint8_t, MaxRangeSize> bits;
    std::array<CachedIdBytes, MaxRangeSize> idBytes;
    std::size_t pendingCount = 0;

    const std::size_t totalBits = IdTraits::id_byte_count * 8;
    for (std::size_t i = 0; i < rangeSize; ++i) {
        localOrder[i] = i;
        const std::size_t nodeIdx = order[rangeBegin + i];
        nodeIndices[i] = nodeIdx;
        const auto &idVal = traits.id(nodes[nodeIdx]);
        for (std::size_t byteIdx = 0; byteIdx < IdTraits::id_byte_count;
             ++byteIdx) {
            idBytes[i][byteIdx] = traits.byte_msb_first(idVal, byteIdx);
        }
    }
    pending[pendingCount++] = PendingBitRange{0, rangeSize, 0};

    auto pushPendingRange = [&](std::size_t begin, std::size_t end,
                                std::size_t nextBitIndex) {
        if (end - begin > 1 && nextBitIndex < totalBits) {
            pending[pendingCount++] = PendingBitRange{begin, end, nextBitIndex};
        }
    };

    while (pendingCount > 0) {
        const PendingBitRange current = pending[--pendingCount];
        const std::size_t currentSize = current.end - current.begin;
        if (currentSize <= 1 || current.bitIndex >= totalBits) {
            continue;
        }

        std::size_t splitBitIndex = totalBits;
        for (std::size_t byteIdx = current.bitIndex / 8;
             byteIdx < IdTraits::id_byte_count; ++byteIdx) {
            const std::size_t baseOrdinal = localOrder[current.begin];
            const uint8_t baseByte = idBytes[baseOrdinal][byteIdx];
            uint8_t diffByte = 0;
            for (std::size_t i = 1; i < currentSize; ++i) {
                const std::size_t ordinal = localOrder[current.begin + i];
                diffByte |=
                    static_cast<uint8_t>(baseByte ^ idBytes[ordinal][byteIdx]);
            }

            if (byteIdx == current.bitIndex / 8) {
                const std::size_t startingBitOffset =
                    7 - (current.bitIndex % 8);
                const uint8_t unprocessedMask = static_cast<uint8_t>(
                    (uint16_t{1} << (startingBitOffset + 1U)) - 1U);
                diffByte &= unprocessedMask;
            }

            if (diffByte == 0) {
                continue;
            }

            for (std::size_t bit = 0; bit < 8; ++bit) {
                const std::size_t bitOffset = 7 - bit;
                if (((diffByte >> bitOffset) & 1U) != 0U) {
                    splitBitIndex = (byteIdx * 8) + bit;
                    break;
                }
            }
            break;
        }

        if (splitBitIndex == totalBits) {
            continue;
        }

        const std::size_t byteIdx = splitBitIndex / 8;
        const std::size_t bitOffset = 7 - (splitBitIndex % 8);
        std::size_t zeroCount = 0;

        for (std::size_t i = 0; i < currentSize; ++i) {
            const std::size_t ordinal = localOrder[current.begin + i];
            const uint8_t byteVal = idBytes[ordinal][byteIdx];
            const std::size_t bit =
                static_cast<std::size_t>((byteVal >> bitOffset) & 1U);
            bits[i] = static_cast<uint8_t>(bit);
            zeroCount += bit ^ 1U;
        }

        const std::size_t nextBitIndex = splitBitIndex + 1;

        std::size_t zeroWrite = 0;
        std::size_t oneWrite = zeroCount;
        for (std::size_t i = 0; i < currentSize; ++i) {
            const std::size_t ordinal = localOrder[current.begin + i];
            const std::size_t bit = bits[i];

            const std::size_t zeroMask = bit ^ 1U;
            const std::size_t oneMask = bit;
            const std::size_t zeroDest = zeroWrite;
            const std::size_t oneDest = oneWrite;
            const std::size_t mask = std::size_t{0} - bit;
            const std::size_t dest = zeroDest ^ ((zeroDest ^ oneDest) & mask);

            scratchOrder[dest] = ordinal;

            zeroWrite += zeroMask;
            oneWrite += oneMask;
        }

        for (std::size_t i = 0; i < currentSize; ++i) {
            localOrder[current.begin + i] = scratchOrder[i];
        }

        pushPendingRange(current.begin + zeroCount, current.end, nextBitIndex);
        pushPendingRange(current.begin, current.begin + zeroCount,
                         nextBitIndex);
    }

    for (std::size_t i = 0; i < rangeSize; ++i) {
        order[rangeBegin + i] = nodeIndices[localOrder[i]];
    }
}

template <std::size_t MaxRangeSize> struct BranchlessBitwiseSmallSorter {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBranchlessBitwise<MaxRangeSize>(order, nodes,
                                                            traits, begin, end);
    }
};

enum class AdaptiveChunkWidth : uint8_t {
    U8 = 1,
    U16 = 2,
    U32 = 4,
};

template <std::size_t U8MaxRangeSize, std::size_t U16MaxRangeSize>
struct RangeLadder {
    static_assert(U8MaxRangeSize < U16MaxRangeSize);

    static constexpr AdaptiveChunkWidth
    chunkWidthForRange(std::size_t rangeSize) noexcept {
        if (rangeSize <= U8MaxRangeSize) {
            return AdaptiveChunkWidth::U8;
        }
        if (rangeSize <= U16MaxRangeSize) {
            return AdaptiveChunkWidth::U16;
        }
        return AdaptiveChunkWidth::U32;
    }
};

template <std::size_t ChunkBytes, typename CountPolicy,
          typename SmallRangeSorter>
inline void sortOneDepthRangeByChunkMsd(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    std::size_t rangeBegin, std::size_t rangeEnd,
    AdaptiveChunkWorkspace<ChunkBytes, CountPolicy> &workspace,
    SmallRangeSorter smallRangeSorter) {
    detail::sortRangeByIdChunksWithSmallSorter<
        ChunkBytes, CountPolicy, detail::small_id_range_sort_threshold>(
        order, nodes, UInt128NodeTraits{}, rangeBegin, rangeEnd, 0,
        workspace.pending, workspace.current.get(), workspace.next.get(),
        workspace.touchedScratch, smallRangeSorter);
}

template <typename LadderPolicy, typename CountPolicy, typename Depth>
inline void sortDepthRangesByRangeLadderChunkMsd(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges) {
    std::size_t maxU8RangeSize = 0;
    std::size_t maxU16RangeSize = 0;
    std::size_t maxU32RangeSize = 0;
    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
        if (rangeSize <= detail::small_id_range_sort_threshold) {
            continue;
        }
        switch (LadderPolicy::chunkWidthForRange(rangeSize)) {
        case AdaptiveChunkWidth::U8:
            maxU8RangeSize = std::max(maxU8RangeSize, rangeSize);
            break;
        case AdaptiveChunkWidth::U16:
            maxU16RangeSize = std::max(maxU16RangeSize, rangeSize);
            break;
        case AdaptiveChunkWidth::U32:
            maxU32RangeSize = std::max(maxU32RangeSize, rangeSize);
            break;
        }
    }

    AdaptiveChunkWorkspace<1, CountPolicy> u8Workspace;
    AdaptiveChunkWorkspace<2, CountPolicy> u16Workspace;
    AdaptiveChunkWorkspace<4, CountPolicy> u32Workspace;
    u8Workspace.allocate(maxU8RangeSize);
    u16Workspace.allocate(maxU16RangeSize);
    u32Workspace.allocate(maxU32RangeSize);
    const LinearSmallSorter smallRangeSorter;

    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeSize = range.end - range.begin;
        switch (LadderPolicy::chunkWidthForRange(rangeSize)) {
        case AdaptiveChunkWidth::U8:
            sortOneDepthRangeByChunkMsd<1, CountPolicy>(
                order, nodes, range.begin, range.end, u8Workspace,
                smallRangeSorter);
            break;
        case AdaptiveChunkWidth::U16:
            sortOneDepthRangeByChunkMsd<2, CountPolicy>(
                order, nodes, range.begin, range.end, u16Workspace,
                smallRangeSorter);
            break;
        case AdaptiveChunkWidth::U32:
            sortOneDepthRangeByChunkMsd<4, CountPolicy>(
                order, nodes, range.begin, range.end, u32Workspace,
                smallRangeSorter);
            break;
        }
    }
}

template <std::size_t U8MaxRangeSize, std::size_t U16MaxRangeSize,
          typename CountPolicy>
inline std::vector<Node> sortForestByAdaptiveDepth2RangeLadderWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByRangeLadderChunkMsd<
            RangeLadder<U8MaxRangeSize, U16MaxRangeSize>, CountPolicy>(
            order, sortNodes, depthRanges);
    };
    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

template <std::size_t DepthPrefixBytes, std::size_t ChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter>
inline std::vector<Node>
sortForestByAdaptiveChunkWithParent(const std::vector<Node> &nodes,
                                    const std::vector<std::size_t> &parentIndex,
                                    bool allowDenseDepthGrouping) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesWithTunedParams<ChunkBytes, CountPolicy, SmallThreshold,
                                       SmallRangeSorter>(order, sortNodes,
                                                         depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<DepthPrefixBytes>(
        nodes, parentIndex, allowDenseDepthGrouping, rangeSorter);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkNoDenseWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, false);
}

inline std::vector<Node>
sortForestByAdaptiveDepth2U32ChunkBitmaskLe512WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveDepth2U32ChunkBitmaskLe512WithParent(
        nodes, parentIndex);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkFullClearWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 4, detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U8ChunkFullClearWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 1, detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByAdaptiveDepth2U8ChunkBitmaskLe512WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 1,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U16ChunkFullClearWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 2, detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByAdaptiveDepth2U16ChunkBitmaskLe512WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 2,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth2U64ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<2, 8>(nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByAdaptiveDepth2U64ChunkBinarySmallWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<
        2, 8, detail::FullClearCounts, detail::small_id_range_sort_threshold,
        BinarySmallSorter>(nodes, parentIndex, true);
}

inline std::vector<Node> sortForestByAdaptiveDepth4U32ChunkWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<4, 4>(nodes, parentIndex, true);
}

template <typename SmallRangeSorter, std::size_t SmallThreshold,
          typename CountPolicy = detail::FullClearCounts>
inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkTailTunedWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesWithTunedParams<4, CountPolicy, SmallThreshold,
                                       SmallRangeSorter>(order, sortNodes,
                                                         depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

template <std::size_t BitmaskMaxRangeSize>
inline std::vector<Node> sortForestByAdaptiveDepth2U32ChunkBitmaskLeWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesWithTunedParams<
            4, detail::BitmaskTouchedCountsUpTo<BitmaskMaxRangeSize>,
            detail::small_id_range_sort_threshold, LinearSmallSorter>(
            order, sortNodes, depthRanges);
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
