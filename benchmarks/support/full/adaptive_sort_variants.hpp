#ifndef FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
#define FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP

#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/id_small_sort.hpp"
#include "forest_sorting/detail/order.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/radix_ladder_variants.hpp"
#include "sort_baselines.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace forest_sorting::test_support {

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

template <typename Nodes, typename IdTraits, typename Algorithm>
void withDynamicNodeSmallSortAccessor(const std::vector<std::size_t> &order,
                                      const Nodes &nodes,
                                      const IdTraits &traits,
                                      std::size_t rangeBegin,
                                      std::size_t rangeEnd,
                                      Algorithm algorithm) {
    auto idForIndex = [&](std::size_t itemIndex) {
        return traits.id(nodes[itemIndex]);
    };
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }
    if constexpr (detail::shouldCacheChunkIds<IdTraits>) {
        constexpr std::size_t chunkCount =
            detail::idChunkCount<detail::cached_comparison_chunk_bytes,
                                 IdTraits>;
        using CachedId = detail::CachedChunkId<chunkCount>;
        std::vector<CachedId> idChunks(rangeSize);
        detail::CachedKeyAccessor<decltype(idForIndex), IdTraits,
                                  decltype(idChunks)>
            accessor{order, idForIndex, traits, rangeBegin, idChunks};
        accessor.initialize(rangeSize);
        algorithm(accessor);
    } else {
        detail::DirectKeyAccessor<decltype(idForIndex), IdTraits> accessor{
            order, idForIndex, traits, rangeBegin};
        accessor.initialize(rangeSize);
        algorithm(accessor);
    }
}

struct PendingBitRange {
    std::size_t begin;
    std::size_t end;
    std::size_t bitIndex;
};

template <typename PendingStorage, typename IndexStorage, typename BitStorage,
          typename IdStorage>
struct BranchlessBitwiseScratch {
    PendingStorage pending;
    IndexStorage localOrder;
    IndexStorage scratchOrder;
    IndexStorage nodeIndices;
    BitStorage bits;
    IdStorage idBytes;
};

template <typename ScratchAccessor>
void stableSortRangeSmallBinaryInternal(std::vector<std::size_t> &order,
                                        std::size_t rangeBegin,
                                        std::size_t rangeEnd,
                                        ScratchAccessor &accessor) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t localIdx = rangeIdx - rangeBegin;
        const std::size_t nodeIndex = order[rangeIdx];

        accessor.save(localIdx);

        const std::size_t previousLocalIdx = localIdx - 1;
        if (accessor.isLessOrEqual(previousLocalIdx)) {
            continue;
        }

        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx;
        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const std::size_t middleLocalIdx = middle - rangeBegin;
            const bool insertAfterMiddle =
                accessor.isLessOrEqual(middleLocalIdx);
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        const std::size_t insertLocalIdx = searchBegin - rangeBegin;
        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            const std::size_t moveLocalIdx = moveIdx - rangeBegin;
            order[moveIdx] = order[moveIdx - 1];
            accessor.move(moveLocalIdx - 1, moveLocalIdx);
        }
        order[searchBegin] = nodeIndex;
        accessor.writeSaved(insertLocalIdx);
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

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallBinaryDynamic(std::vector<std::size_t> &order,
                                       const Nodes &nodes,
                                       const IdTraits &traits,
                                       std::size_t rangeBegin,
                                       std::size_t rangeEnd) {
    withDynamicNodeSmallSortAccessor(
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
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t localIdx = rangeIdx - rangeBegin;
        const std::size_t nodeIndex = order[rangeIdx];

        accessor.save(localIdx);

        const std::size_t previousLocalIdx = localIdx - 1;
        if (accessor.isLessOrEqual(previousLocalIdx)) {
            continue;
        }

        const std::size_t sortedPrefixSize = rangeIdx - rangeBegin;
        std::size_t searchBegin = rangeBegin;
        std::size_t searchEnd = rangeIdx - 1;
        for (std::size_t bound = 2; bound <= sortedPrefixSize; bound <<= 1) {
            const std::size_t probe = rangeIdx - bound;
            const std::size_t probeLocalIdx = probe - rangeBegin;
            if (accessor.isLessOrEqual(probeLocalIdx)) {
                searchBegin = probe + 1;
                break;
            }
            searchEnd = probe;
        }

        while (searchBegin < searchEnd) {
            const std::size_t middle =
                searchBegin + ((searchEnd - searchBegin) / 2);
            const std::size_t middleLocalIdx = middle - rangeBegin;
            const bool insertAfterMiddle =
                accessor.isLessOrEqual(middleLocalIdx);
            searchBegin = insertAfterMiddle ? middle + 1 : searchBegin;
            searchEnd = insertAfterMiddle ? searchEnd : middle;
        }

        const std::size_t insertLocalIdx = searchBegin - rangeBegin;
        for (std::size_t moveIdx = rangeIdx; moveIdx > searchBegin; --moveIdx) {
            const std::size_t moveLocalIdx = moveIdx - rangeBegin;
            order[moveIdx] = order[moveIdx - 1];
            accessor.move(moveLocalIdx - 1, moveLocalIdx);
        }
        order[searchBegin] = nodeIndex;
        accessor.writeSaved(insertLocalIdx);
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

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallExponentialDynamic(std::vector<std::size_t> &order,
                                            const Nodes &nodes,
                                            const IdTraits &traits,
                                            std::size_t rangeBegin,
                                            std::size_t rangeEnd) {
    withDynamicNodeSmallSortAccessor(
        order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
            stableSortRangeSmallExponentialInternal(order, rangeBegin, rangeEnd,
                                                    accessor);
        });
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

template <std::size_t RadixChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter, typename Depth>
inline void sortDepthRangesByIdMsdChunks(
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

    detail::IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> workspace;
    workspace.allocate(maxRadixRangeSize);

    for (const detail::DepthRange<Depth> &range : depthRanges) {
        const std::size_t rangeBegin = range.begin;
        const std::size_t rangeEnd = range.end;
        auto idForIndex = [&](std::size_t itemIndex) {
            return UInt128NodeTraits::id(nodes[itemIndex]);
        };
        auto adaptedSmallRangeSorter = [&](std::vector<std::size_t> &sortOrder,
                                           auto, const auto &sortTraits,
                                           std::size_t sortBegin,
                                           std::size_t sortEnd) {
            smallRangeSorter(sortOrder, nodes, sortTraits, sortBegin, sortEnd);
        };
        detail::sortIndexRangeByIdMsdChunksWithSmallSorter<
            RadixChunkBytes, CountPolicy, SmallThreshold>(
            order, idForIndex, UInt128NodeTraits{}, rangeBegin, rangeEnd, 0,
            workspace, adaptedSmallRangeSorter);
    }
}

template <typename LadderPolicy, typename CountPolicy, typename Depth>
inline void sortDepthRangesByIdMsdLadder(
    std::vector<std::size_t> &order, const std::vector<Node> &nodes,
    const std::vector<detail::DepthRange<Depth>> &depthRanges) {
    IdMsdLadderWorkspace<CountPolicy> workspace;
    auto idForIndex = [&](std::size_t itemIndex) {
        return UInt128NodeTraits::id(nodes[itemIndex]);
    };
    for (const detail::DepthRange<Depth> &range : depthRanges) {
        sortIndexRangeByIdMsdLadder<LadderPolicy, CountPolicy>(
            order, idForIndex, UInt128NodeTraits{}, range.begin, range.end,
            workspace);
    }
}

template <std::size_t Chunk8Max, std::size_t Chunk16Max, typename CountPolicy>
inline std::vector<Node> sortForestByDepth2FirstThenIdMsdLadderWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    using Ladder = Chunk8Chunk16Chunk32Ladder<Chunk8Max, Chunk16Max>;
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdLadder<Ladder, CountPolicy>(order, sortNodes,
                                                          depthRanges);
    };
    return sortForestByAdaptiveRangeSorterWithParent<2, CountPolicy>(
        nodes, parentIndex, true, rangeSorter);
}

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallLinearDynamic(std::vector<std::size_t> &order,
                                       const Nodes &nodes,
                                       const IdTraits &traits,
                                       std::size_t rangeBegin,
                                       std::size_t rangeEnd) {
    withDynamicNodeSmallSortAccessor(
        order, nodes, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
            detail::stableSortIndexRangeSmallLinearInternal(order, rangeBegin,
                                                            rangeEnd, accessor);
        });
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

struct LinearSmallSorterDynamic {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallLinearDynamic(order, nodes, traits, begin, end);
    }
};

template <std::size_t MaxRangeSize = detail::small_id_range_sort_threshold>
struct BinarySmallSorter {
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

struct BinarySmallSorterDynamic {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBinaryDynamic(order, nodes, traits, begin, end);
    }
};

struct ExponentialSmallSorterDynamic {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallExponentialDynamic(order, nodes, traits, begin,
                                               end);
    }
};

template <typename IdTraits, typename Nodes, typename Scratch>
void stableSortRangeSmallBranchlessBitwiseWithScratch(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, Scratch &scratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    auto &pending = scratch.pending;
    auto &localOrder = scratch.localOrder;
    auto &scratchOrder = scratch.scratchOrder;
    auto &nodeIndices = scratch.nodeIndices;
    auto &bits = scratch.bits;
    auto &idBytes = scratch.idBytes;

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

    std::size_t pendingCount = 0;
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
                    (uint32_t{1} << (startingBitOffset + 1U)) - uint32_t{1});
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
    detail::requireFixedSmallSortCapacity<MaxRangeSize>(rangeSize);

    using CachedIdBytes = std::array<uint8_t, IdTraits::id_byte_count>;
    using Scratch =
        BranchlessBitwiseScratch<std::array<PendingBitRange, MaxRangeSize>,
                                 std::array<std::size_t, MaxRangeSize>,
                                 std::array<uint8_t, MaxRangeSize>,
                                 std::array<CachedIdBytes, MaxRangeSize>>;
    Scratch scratch;

    stableSortRangeSmallBranchlessBitwiseWithScratch<IdTraits>(
        order, nodes, traits, rangeBegin, rangeEnd, scratch);
}

template <typename Nodes, typename IdTraits>
void stableSortRangeSmallBranchlessBitwiseDynamic(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    using CachedIdBytes = std::array<uint8_t, IdTraits::id_byte_count>;
    using Scratch =
        BranchlessBitwiseScratch<std::vector<PendingBitRange>,
                                 std::vector<std::size_t>, std::vector<uint8_t>,
                                 std::vector<CachedIdBytes>>;
    Scratch scratch{
        std::vector<PendingBitRange>(rangeSize),
        std::vector<std::size_t>(rangeSize),
        std::vector<std::size_t>(rangeSize),
        std::vector<std::size_t>(rangeSize),
        std::vector<uint8_t>(rangeSize),
        std::vector<CachedIdBytes>(rangeSize),
    };

    stableSortRangeSmallBranchlessBitwiseWithScratch<IdTraits>(
        order, nodes, traits, rangeBegin, rangeEnd, scratch);
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

struct BranchlessBitwiseSmallSorterDynamic {
    void operator()(std::vector<std::size_t> &order,
                    const std::vector<Node> &nodes,
                    const UInt128NodeTraits &traits, std::size_t begin,
                    std::size_t end) const {
        stableSortRangeSmallBranchlessBitwiseDynamic(order, nodes, traits,
                                                     begin, end);
    }
};

template <std::size_t DepthPrefixBytes, std::size_t RadixChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter<SmallThreshold>>
inline std::vector<Node> sortForestByAdaptiveIdMsdChunkWithParent(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    bool allowDenseDepthGrouping) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdChunks<RadixChunkBytes, CountPolicy,
                                     SmallThreshold>(
            order, sortNodes, depthRanges, SmallRangeSorter{});
    };

    return sortForestByAdaptiveRangeSorterWithParent<DepthPrefixBytes>(
        nodes, parentIndex, allowDenseDepthGrouping, rangeSorter);
}

inline void validateIdPermutation(const std::vector<Node> &nodes,
                                  const std::vector<std::size_t> &permutation) {
    if (permutation.size() != nodes.size()) {
        throw std::runtime_error("ID permutation size must match node count");
    }
    std::vector<bool> seen(nodes.size(), false);
    const UInt128NodeTraits traits;
    for (std::size_t offset = 0; offset < permutation.size(); ++offset) {
        const std::size_t nodeIndex = permutation[offset];
        if (nodeIndex >= nodes.size() || seen[nodeIndex]) {
            throw std::runtime_error("invalid ID permutation");
        }
        seen[nodeIndex] = true;
        if (offset != 0) {
            const std::size_t previousIndex = permutation[offset - 1];
            if (!detail::idLess(nodes[previousIndex].id, nodes[nodeIndex].id,
                                traits)) {
                throw std::runtime_error("ID permutation is not canonical");
            }
        }
    }
}

inline std::vector<Node> sortForestByTrustedGlobalIdPermutationThenDepthStable(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    const std::vector<std::size_t> *idPermutation) {
    auto computed =
        detail::computeDepths<2>(nodes, parentIndex, UInt128NodeTraits{});
    std::vector<std::size_t> order;
    if (idPermutation != nullptr) {
        order = *idPermutation;
    } else {
        order = detail::makeValidatedGlobalIdPermutation(nodes,
                                                         UInt128NodeTraits{});
    }
    std::vector<std::size_t> scratch(order.size());
    detail::stableGroupOrderByDepth<2, detail::ProductionIdCountPolicy>(
        order, scratch, computed.values, computed.observedMax);
    return materializeOrder(nodes, order);
}

inline std::vector<Node> sortForestByGlobalIdPermutationThenDepthStable(
    const std::vector<Node> &nodes, const std::vector<std::size_t> &parentIndex,
    const std::vector<std::size_t> *idPermutation) {
    if (idPermutation != nullptr) {
        validateIdPermutation(nodes, *idPermutation);
    }
    return sortForestByTrustedGlobalIdPermutationThenDepthStable(
        nodes, parentIndex, idPermutation);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512NoDenseWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, false);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 4,
                                                    detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk8FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 1,
                                                    detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk8BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 1,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk16FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 2,
                                                    detail::FullClearCounts>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk16BitmaskLe512TailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<
        2, 2,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>(
        nodes, parentIndex, true);
}

inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk64FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<2, 8>(nodes, parentIndex,
                                                          true);
}

inline std::vector<Node>
sortForestByDepth4FirstThenIdMsdChunk32FullClearTailLinear32WithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveIdMsdChunkWithParent<4, 4>(nodes, parentIndex,
                                                          true);
}

template <std::size_t BitmaskMaxRangeSize>
inline std::vector<Node>
sortForestByDepth2FirstThenIdMsdChunk32BitmaskLeWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
    auto rangeSorter = [](std::vector<std::size_t> &order,
                          const std::vector<Node> &sortNodes,
                          const auto &depthRanges) {
        sortDepthRangesByIdMsdChunks<
            4, detail::BitmaskTouchedCountsUpTo<BitmaskMaxRangeSize>,
            detail::small_id_range_sort_threshold>(
            order, sortNodes, depthRanges, LinearSmallSorter<>{});
    };

    return sortForestByAdaptiveRangeSorterWithParent<2>(nodes, parentIndex,
                                                        true, rangeSorter);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_ADAPTIVE_SORT_VARIANTS_HPP
