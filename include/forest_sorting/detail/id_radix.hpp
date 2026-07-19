#ifndef FOREST_SORTING_DETAIL_ID_RADIX_HPP
#define FOREST_SORTING_DETAIL_ID_RADIX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_small_sort.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

inline constexpr std::size_t small_id_range_sort_threshold = 32;

// Width, in bytes, of each MSD radix partition chunk. A 4-byte chunk is the
// benchmark label's `chunk32` radix partition. This is unrelated to cached
// comparison chunks; each partition chunk is sorted by stable byte-LSD passes.
inline constexpr std::size_t production_id_radix_chunk_bytes = 4;

inline constexpr std::size_t production_touched_count_max_range_size = 512;
using ProductionIdCountPolicy = IdCountPolicy<
    BitmaskTouchedCountsUpTo<production_touched_count_max_range_size>>;

struct NoopTerminalRangeObserver {
    static constexpr void observe(std::span<const std::size_t> indices,
                                  std::size_t byteOffset) noexcept {
        (void)indices;
        (void)byteOffset;
    }
};

struct IdMsdChunkRange {
    std::size_t begin;
    std::size_t end;
    std::size_t chunkIndex;
};

// Per-entry buffer used by the ID radix sorter. `RadixChunkBytes` controls the
// packed radix partition width, not the cached-ID comparison width.
template <std::size_t RadixChunkBytes> struct IdMsdChunkEntry {
    ChunkValueType<RadixChunkBytes> chunk;
    std::size_t index;
};

template <typename ScratchAccessor>
void stableSortIndexRangeSmallLinearInternal(std::vector<std::size_t> &order,
                                             std::size_t rangeBegin,
                                             std::size_t rangeEnd,
                                             ScratchAccessor &accessor) {
    for (std::size_t rangeIdx = rangeBegin + 1; rangeIdx < rangeEnd;
         ++rangeIdx) {
        const std::size_t localIdx = rangeIdx - rangeBegin;
        const std::size_t itemIndex = order[rangeIdx];
        accessor.save(localIdx);
        std::size_t innerIdx = rangeIdx;
        for (; innerIdx > rangeBegin; --innerIdx) {
            const std::size_t previousLocalIdx = innerIdx - 1 - rangeBegin;
            if (accessor.isLessOrEqual(previousLocalIdx)) {
                break;
            }
            const std::size_t innerLocalIdx = innerIdx - rangeBegin;
            order[innerIdx] = order[innerIdx - 1];
            accessor.move(previousLocalIdx, innerLocalIdx);
        }
        order[innerIdx] = itemIndex;
        accessor.writeSaved(innerIdx - rangeBegin);
    }
}

template <std::size_t MaxRangeSize = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits>
void stableSortIndexRangeSmallLinear(std::vector<std::size_t> &order,
                                     IdForIndex idForIndex,
                                     const IdTraits &traits,
                                     std::size_t rangeBegin,
                                     std::size_t rangeEnd) {
    withFixedSmallSortAccessor<MaxRangeSize>(
        order, idForIndex, traits, rangeBegin, rangeEnd, [&](auto &accessor) {
            stableSortIndexRangeSmallLinearInternal(order, rangeBegin, rangeEnd,
                                                    accessor);
        });
}

// Sorts one packed MSD chunk by running stable LSD passes over the bytes inside
// that chunk. For example, `RadixChunkBytes == 4` behaves like a 32-bit radix
// chunk but is implemented as four base-256 stable counting passes.
template <std::size_t RadixChunkBytes, typename CountScratch>
IdMsdChunkEntry<RadixChunkBytes> *
stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled(
    std::vector<std::size_t> &order, std::size_t rangeBegin,
    std::size_t rangeEnd, IdMsdChunkEntry<RadixChunkBytes> *current,
    IdMsdChunkEntry<RadixChunkBytes> *next, CountScratch &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return current;
    }

    IdMsdChunkEntry<RadixChunkBytes> *source = current;
    IdMsdChunkEntry<RadixChunkBytes> *destination = next;

    for (std::size_t bytePass = 0; bytePass < RadixChunkBytes; ++bytePass) {
        resetRadixCounts(countScratch);
        for (std::size_t offset = 0; offset < rangeSize; ++offset) {
            noteRadixDigit(countScratch,
                           wordByte(source[offset].chunk, bytePass));
        }

        prefixRadixCounts(countScratch);

        for (std::size_t offset = 0; offset < rangeSize; ++offset) {
            const uint8_t digit = wordByte(source[offset].chunk, bytePass);
            destination[countScratch.counts[digit]++] = source[offset];
        }

        clearRadixCounts(countScratch);
        std::swap(source, destination);
    }

    for (std::size_t offset = 0, orderOffset = rangeBegin; offset < rangeSize;
         ++offset, ++orderOffset) {
        order[orderOffset] = source[offset].index;
    }

    return source;
}

template <std::size_t RadixChunkBytes, typename CountPolicy, typename Scratch>
IdMsdChunkEntry<RadixChunkBytes> *dispatchLsdIndexMsdChunkSortPreFilled(
    std::vector<std::size_t> &order, std::size_t rangeBegin,
    std::size_t rangeEnd, IdMsdChunkEntry<RadixChunkBytes> *current,
    IdMsdChunkEntry<RadixChunkBytes> *next, Scratch &touchedScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    using CounterTraits = RadixCounterTraits<CountPolicy>;
    if constexpr (CounterTraits::alwaysUsePolicy) {
        typename CounterTraits::policy_scratch_type countScratch;
        return stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled<
            RadixChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                             countScratch);
    } else {
        if (CounterTraits::usePolicyForRange(rangeSize)) {
            return stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled<
                RadixChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                                 touchedScratch);
        }
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled<
            RadixChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                             fullClearScratch);
    }
}

template <std::size_t RadixChunkBytes,
          IdRadixCountPolicy Policy = IdCountPolicy<FullClearCounts>>
struct IdMsdChunkSortWorkspace {
    static constexpr std::size_t radix_chunk_bytes = RadixChunkBytes;
    using CountPolicy = Policy::counter_policy;

    std::vector<IdMsdChunkRange> pending;
    std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]> current;
    std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]> next;
    std::size_t capacity = 0;

    using ScratchType = RadixCounterTraits<CountPolicy>::workspace_scratch_type;
    ScratchType touchedScratch;

    void allocate(std::size_t rangeSize) {
        pending.reserve(initial_range_stack_capacity);
        if (rangeSize <= capacity) {
            return;
        }
        auto newCurrent =
            std::make_unique<IdMsdChunkEntry<RadixChunkBytes>[]>(rangeSize);
        auto newNext =
            std::make_unique<IdMsdChunkEntry<RadixChunkBytes>[]>(rangeSize);
        current = std::move(newCurrent);
        next = std::move(newNext);
        capacity = rangeSize;
    }
};

template <std::size_t RadixChunkBytes, IdRadixCountPolicy Policy,
          std::size_t SmallThreshold, typename IdForIndex, typename IdTraits,
          typename ChunkExtractor, typename PushNextRange,
          typename SmallRangeSorter, typename TerminalRangeObserver>
void processIdMsdChunkRange(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    IdMsdChunkSortWorkspace<RadixChunkBytes, Policy> &workspace,
    ChunkExtractor chunkExtractor, bool hasNextRange,
    PushNextRange pushNextRange, SmallRangeSorter smallRangeSorter,
    TerminalRangeObserver &observer, std::size_t byteOffset) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    if (rangeSize <= SmallThreshold) {
        observer.observe(
            std::span<const std::size_t>{order.data() + rangeBegin, rangeSize},
            byteOffset);
        smallRangeSorter(order, idForIndex, traits, rangeBegin, rangeEnd);
        return;
    }

    workspace.allocate(rangeSize);
    ChunkValueType<RadixChunkBytes> firstChunk = 0;
    ChunkValueType<RadixChunkBytes> differingBits = 0;
    for (std::size_t offset = 0; offset < rangeSize; ++offset) {
        const std::size_t itemIndex = order[rangeBegin + offset];
        const ChunkValueType<RadixChunkBytes> currentChunk =
            chunkExtractor(itemIndex);
        workspace.current[offset] = {currentChunk, itemIndex};
        if (offset == 0) {
            firstChunk = currentChunk;
        } else {
            differingBits |= currentChunk ^ firstChunk;
        }
    }

    if (differingBits == 0) {
        if (hasNextRange) {
            pushNextRange(rangeBegin, rangeEnd);
        }
        return;
    }

    IdMsdChunkEntry<RadixChunkBytes> *sortedChunks =
        dispatchLsdIndexMsdChunkSortPreFilled<RadixChunkBytes,
                                              typename Policy::counter_policy>(
            order, rangeBegin, rangeEnd, workspace.current.get(),
            workspace.next.get(), workspace.touchedScratch);

    if (!hasNextRange) {
        return;
    }

    std::size_t equalChunkBegin = rangeBegin;
    ChunkValueType<RadixChunkBytes> previousChunk = sortedChunks->chunk;
    for (std::size_t offset = 1; offset < rangeSize; ++offset) {
        const ChunkValueType<RadixChunkBytes> currentChunk =
            sortedChunks[offset].chunk;
        if (currentChunk != previousChunk) {
            const std::size_t splitOffset = rangeBegin + offset;
            pushNextRange(equalChunkBegin, splitOffset);
            equalChunkBegin = splitOffset;
            previousChunk = currentChunk;
        }
    }

    pushNextRange(equalChunkBegin, rangeEnd);
}

template <std::size_t RadixChunkBytes,
          IdRadixCountPolicy Policy = IdCountPolicy<FullClearCounts>,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits,
          typename TerminalRangeObserver = NoopTerminalRangeObserver>
void sortIndexRangeByIdMsdChunks(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex,
    IdMsdChunkSortWorkspace<RadixChunkBytes, Policy> &workspace,
    TerminalRangeObserver observer = {}) {
    workspace.allocate(rangeEnd - rangeBegin);
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, auto sortIdForIndex,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortIndexRangeSmallLinear<SmallThreshold>(
                sortOrder, sortIdForIndex, sortTraits, sortBegin, sortEnd);
        };
    sortIndexRangeByIdMsdChunksWithSmallSorter<RadixChunkBytes, Policy,
                                               SmallThreshold>(
        order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex, workspace,
        linearSmallRangeSorter, std::move(observer));
}

template <std::size_t MaxRangeSize = small_id_range_sort_threshold,
          typename Nodes, typename IdTraits>
void stableSortRangeSmallLinear(std::vector<std::size_t> &order,
                                const Nodes &nodes, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd) {
    auto idForIndex = [&](std::size_t itemIndex) {
        return traits.id(nodes[itemIndex]);
    };
    stableSortIndexRangeSmallLinear<MaxRangeSize>(order, idForIndex, traits,
                                                  rangeBegin, rangeEnd);
}

template <std::size_t RadixChunkBytes,
          IdRadixCountPolicy Policy = IdCountPolicy<FullClearCounts>,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits, typename SmallRangeSorter,
          typename TerminalRangeObserver = NoopTerminalRangeObserver>
void sortIndexRangeByIdMsdChunksWithSmallSorter(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex,
    IdMsdChunkSortWorkspace<RadixChunkBytes, Policy> &workspace,
    SmallRangeSorter smallRangeSorter, TerminalRangeObserver observer = {}) {
    assert(rangeBegin <= rangeEnd);
    assert(rangeEnd <= order.size());
    const std::size_t initialRangeSize = rangeEnd - rangeBegin;
    if (initialRangeSize <= 1) {
        return;
    }

    workspace.pending.clear();
    workspace.pending.push_back(
        IdMsdChunkRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount = idChunkCount<RadixChunkBytes, IdTraits>;
    assert(chunkIndex <= chunkCount);

    while (!workspace.pending.empty()) {
        const IdMsdChunkRange currentRange = workspace.pending.back();
        workspace.pending.pop_back();
        const std::size_t currentBegin = currentRange.begin;
        const std::size_t currentEnd = currentRange.end;
        const std::size_t currentChunkIndex = currentRange.chunkIndex;
        const std::size_t rangeSize = currentEnd - currentBegin;
        if (rangeSize <= 1 || currentChunkIndex >= chunkCount) {
            continue;
        }

        const std::size_t nextChunkIndex = currentChunkIndex + 1;
        const bool hasNextRange = nextChunkIndex < chunkCount;
        auto chunkExtractor = [&](std::size_t itemIndex) {
            return static_cast<ChunkValueType<RadixChunkBytes>>(
                chunkMsbFirst<RadixChunkBytes>(idForIndex(itemIndex),
                                               currentChunkIndex, traits));
        };
        auto pushNextRange = [&](std::size_t childBegin, std::size_t childEnd) {
            workspace.pending.push_back(
                IdMsdChunkRange{childBegin, childEnd, nextChunkIndex});
        };
        processIdMsdChunkRange<RadixChunkBytes, Policy, SmallThreshold>(
            order, idForIndex, traits, currentBegin, currentEnd, workspace,
            chunkExtractor, hasNextRange, pushNextRange, smallRangeSorter,
            observer, currentChunkIndex * RadixChunkBytes);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_RADIX_HPP
