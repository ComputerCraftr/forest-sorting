#ifndef FOREST_SORTING_DETAIL_ID_RADIX_HPP
#define FOREST_SORTING_DETAIL_ID_RADIX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_small_sort.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

inline constexpr std::size_t small_id_range_sort_threshold = 32;

// Width, in bytes, of each MSD radix partition chunk. A 4-byte chunk is the
// benchmark label's `u32` radix chunk. This is unrelated to cached comparison
// chunks; each partition chunk is still sorted by stable LSD byte passes.
inline constexpr std::size_t production_id_chunk_bytes = 4;

inline constexpr std::size_t production_touched_count_max_range_size = 512;
using ProductionIdCountPolicy =
    BitmaskTouchedCountsUpTo<production_touched_count_max_range_size>;

struct IdChunkRange {
    std::size_t begin;
    std::size_t end;
    std::size_t chunkIndex;
};

// Per-entry buffer used by the ID radix sorter. `ChunkBytes` controls the
// packed radix partition width, not the cached-ID comparison width.
template <std::size_t ChunkBytes> struct ChunkedIndex {
    ChunkValueType<ChunkBytes> chunk;
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
// that chunk. For example, `ChunkBytes == 4` behaves like a 32-bit radix chunk
// but is implemented as four base-256 stable counting passes.
template <std::size_t ChunkBytes, typename CountScratch>
ChunkedIndex<ChunkBytes> *stableLsdSortIndexRangeByIdChunkWithCounterPreFilled(
    std::vector<std::size_t> &order, std::size_t rangeBegin,
    std::size_t rangeEnd, ChunkedIndex<ChunkBytes> *current,
    ChunkedIndex<ChunkBytes> *next, CountScratch &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return current;
    }

    ChunkedIndex<ChunkBytes> *source = current;
    ChunkedIndex<ChunkBytes> *destination = next;

    for (std::size_t bytePass = 0; bytePass < ChunkBytes; ++bytePass) {
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

template <std::size_t ChunkBytes, typename IdForIndex, typename IdTraits,
          typename CountScratch>
ChunkedIndex<ChunkBytes> *stableLsdSortIndexRangeByIdChunkWithCounter(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex, ChunkedIndex<ChunkBytes> *current,
    ChunkedIndex<ChunkBytes> *next, CountScratch &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return current;
    }

    for (std::size_t offset = 0, orderOffset = rangeBegin; offset < rangeSize;
         ++offset, ++orderOffset) {
        const std::size_t itemIndex = order[orderOffset];
        current[offset] = {chunkMsbFirst<ChunkBytes>(idForIndex(itemIndex),
                                                     chunkIndex, traits),
                           itemIndex};
    }

    return stableLsdSortIndexRangeByIdChunkWithCounterPreFilled<ChunkBytes>(
        order, rangeBegin, rangeEnd, current, next, countScratch);
}

template <std::size_t ChunkBytes, typename CountPolicy, typename IdForIndex,
          typename IdTraits, typename Scratch>
ChunkedIndex<ChunkBytes> *dispatchLsdIndexChunkSort(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex, ChunkedIndex<ChunkBytes> *current,
    ChunkedIndex<ChunkBytes> *next, Scratch &touchedScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdChunkWithCounter(
            order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex,
            current, next, fullClearScratch);
    } else {
        if (rangeSize <= CountPolicy::max_size) {
            return stableLsdSortIndexRangeByIdChunkWithCounter(
                order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex,
                current, next, touchedScratch);
        }
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdChunkWithCounter(
            order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex,
            current, next, fullClearScratch);
    }
}

template <std::size_t ChunkBytes, typename CountPolicy, typename Scratch>
ChunkedIndex<ChunkBytes> *dispatchLsdIndexChunkSortPreFilled(
    std::vector<std::size_t> &order, std::size_t rangeBegin,
    std::size_t rangeEnd, ChunkedIndex<ChunkBytes> *current,
    ChunkedIndex<ChunkBytes> *next, Scratch &touchedScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdChunkWithCounterPreFilled<ChunkBytes>(
            order, rangeBegin, rangeEnd, current, next, fullClearScratch);
    } else {
        if (rangeSize <= CountPolicy::max_size) {
            return stableLsdSortIndexRangeByIdChunkWithCounterPreFilled<
                ChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                            touchedScratch);
        }
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdChunkWithCounterPreFilled<ChunkBytes>(
            order, rangeBegin, rangeEnd, current, next, fullClearScratch);
    }
}

template <std::size_t ChunkBytes, typename CountPolicy = FullClearCounts>
struct IdChunkSortWorkspace {
    std::vector<IdChunkRange> pending;
    std::unique_ptr<ChunkedIndex<ChunkBytes>[]> current;
    std::unique_ptr<ChunkedIndex<ChunkBytes>[]> next;
    std::size_t capacity = 0;

    using ScratchType =
        std::conditional_t<std::is_same_v<CountPolicy, FullClearCounts>,
                           EmptyScratch, BitmaskTouchedCountScratch>;
    ScratchType touchedScratch;

    void allocate(std::size_t rangeSize) {
        pending.reserve(initial_range_stack_capacity);
        if (rangeSize <= capacity) {
            return;
        }
        current = std::unique_ptr<ChunkedIndex<ChunkBytes>[]>(
            new ChunkedIndex<ChunkBytes>[rangeSize]);
        next = std::unique_ptr<ChunkedIndex<ChunkBytes>[]>(
            new ChunkedIndex<ChunkBytes>[rangeSize]);
        capacity = rangeSize;
    }
};

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits>
void sortIndexRangeByIdChunks(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex,
    IdChunkSortWorkspace<ChunkBytes, CountPolicy> &workspace) {
    workspace.allocate(rangeEnd - rangeBegin);
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, auto sortIdForIndex,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortIndexRangeSmallLinear<SmallThreshold>(
                sortOrder, sortIdForIndex, sortTraits, sortBegin, sortEnd);
        };
    sortIndexRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy,
                                            SmallThreshold>(
        order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex,
        workspace.pending, workspace.current.get(), workspace.next.get(),
        workspace.touchedScratch, linearSmallRangeSorter);
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

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits, typename SmallRangeSorter,
          typename Scratch = BitmaskTouchedCountScratch>
void sortIndexRangeByIdChunksWithSmallSorter(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex, std::vector<IdChunkRange> &pending,
    ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
    ChunkedIndex<ChunkBytes> *chunkBufferNext, Scratch &touchedScratch,
    SmallRangeSorter smallRangeSorter) {
    const std::size_t initialRangeSize = rangeEnd - rangeBegin;
    if (initialRangeSize <= 1) {
        return;
    }

    if (initialRangeSize <= SmallThreshold) {
        smallRangeSorter(order, idForIndex, traits, rangeBegin, rangeEnd);
        return;
    }

    pending.clear();
    pending.push_back(IdChunkRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (IdTraits::id_byte_count + ChunkBytes - 1) / ChunkBytes;

    while (!pending.empty()) {
        const IdChunkRange currentRange = pending.back();
        pending.pop_back();
        const std::size_t currentBegin = currentRange.begin;
        const std::size_t currentEnd = currentRange.end;
        const std::size_t currentChunkIndex = currentRange.chunkIndex;
        const std::size_t rangeSize = currentEnd - currentBegin;
        if (rangeSize <= 1 || currentChunkIndex >= chunkCount) {
            continue;
        }

        if (rangeSize <= SmallThreshold) {
            smallRangeSorter(order, idForIndex, traits, currentBegin,
                             currentEnd);
            continue;
        }

        const ChunkValueType<ChunkBytes> firstChunk = chunkMsbFirst<ChunkBytes>(
            idForIndex(order[currentBegin]), currentChunkIndex, traits);
        ChunkValueType<ChunkBytes> differingBits = 0;
        for (std::size_t offset = 0; offset < rangeSize; ++offset) {
            const std::size_t itemIndex = order[currentBegin + offset];
            const ChunkValueType<ChunkBytes> currentChunk =
                chunkMsbFirst<ChunkBytes>(idForIndex(itemIndex),
                                          currentChunkIndex, traits);
            chunkBufferCurrent[offset] = {currentChunk, itemIndex};
            differingBits |= currentChunk ^ firstChunk;
        }

        if (differingBits == 0) {
            const std::size_t nextChunkIndex = currentChunkIndex + 1;
            if (nextChunkIndex < chunkCount) {
                pending.push_back(
                    IdChunkRange{currentBegin, currentEnd, nextChunkIndex});
            }
            continue;
        }

        ChunkedIndex<ChunkBytes> *sortedChunks =
            dispatchLsdIndexChunkSortPreFilled<ChunkBytes, CountPolicy>(
                order, currentBegin, currentEnd, chunkBufferCurrent,
                chunkBufferNext, touchedScratch);

        const std::size_t nextChunkIndex = currentChunkIndex + 1;
        if (nextChunkIndex >= chunkCount) {
            continue;
        }

        std::size_t equalChunkBegin = currentBegin;
        ChunkValueType<ChunkBytes> previousChunk = sortedChunks->chunk;
        for (std::size_t offset = 1; offset < rangeSize; ++offset) {
            const ChunkValueType<ChunkBytes> currentChunk =
                sortedChunks[offset].chunk;
            if (currentChunk != previousChunk) {
                const std::size_t splitOffset = currentBegin + offset;
                pending.push_back(
                    IdChunkRange{equalChunkBegin, splitOffset, nextChunkIndex});
                equalChunkBegin = splitOffset;
                previousChunk = currentChunk;
            }
        }

        pending.push_back(
            IdChunkRange{equalChunkBegin, currentEnd, nextChunkIndex});
    }
}

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename Nodes, typename IdTraits, typename SmallRangeSorter,
          typename Scratch = BitmaskTouchedCountScratch>
void sortRangeByIdChunksWithSmallSorter(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    std::vector<IdChunkRange> &pending,
    ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
    ChunkedIndex<ChunkBytes> *chunkBufferNext, Scratch &touchedScratch,
    SmallRangeSorter smallRangeSorter) {
    auto idForIndex = [&](std::size_t itemIndex) {
        return traits.id(nodes[itemIndex]);
    };
    auto adaptedSmallRangeSorter = [&](std::vector<std::size_t> &sortOrder,
                                       auto, const IdTraits &sortTraits,
                                       std::size_t sortBegin,
                                       std::size_t sortEnd) {
        smallRangeSorter(sortOrder, nodes, sortTraits, sortBegin, sortEnd);
    };
    sortIndexRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy,
                                            SmallThreshold>(
        order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex, pending,
        chunkBufferCurrent, chunkBufferNext, touchedScratch,
        adaptedSmallRangeSorter);
}

template <std::size_t ChunkBytes = chunk_byte_count,
          typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename Nodes, typename IdTraits>
void sortRangeByIdChunks(std::vector<std::size_t> &order, const Nodes &nodes,
                         const IdTraits &traits, std::size_t rangeBegin,
                         std::size_t rangeEnd, std::size_t chunkIndex,
                         std::vector<IdChunkRange> &pending,
                         ChunkedIndex<ChunkBytes> *chunkBufferCurrent,
                         ChunkedIndex<ChunkBytes> *chunkBufferNext) {
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, const Nodes &sortNodes,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortRangeSmallLinear<SmallThreshold>(
                sortOrder, sortNodes, sortTraits, sortBegin, sortEnd);
        };
    using ScratchType =
        std::conditional_t<std::is_same_v<CountPolicy, FullClearCounts>,
                           EmptyScratch, BitmaskTouchedCountScratch>;
    ScratchType touchedScratch;
    sortRangeByIdChunksWithSmallSorter<ChunkBytes, CountPolicy, SmallThreshold>(
        order, nodes, traits, rangeBegin, rangeEnd, chunkIndex, pending,
        chunkBufferCurrent, chunkBufferNext, touchedScratch,
        linearSmallRangeSorter);
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_RADIX_HPP
