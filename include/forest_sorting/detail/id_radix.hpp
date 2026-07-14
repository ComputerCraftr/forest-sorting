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
// benchmark label's `chunk32` radix partition. This is unrelated to cached
// comparison chunks; each partition chunk is sorted by stable byte-LSD passes.
inline constexpr std::size_t production_id_radix_chunk_bytes = 4;

inline constexpr std::size_t production_touched_count_max_range_size = 512;
using ProductionIdCountPolicy =
    BitmaskTouchedCountsUpTo<production_touched_count_max_range_size>;

enum class AdaptiveRadixChunkWidth : uint8_t {
    Chunk8 = 1,
    Chunk16 = 2,
    Chunk32 = 4,
};

template <std::size_t Chunk8MaxRangeSize, std::size_t Chunk16MaxRangeSize>
struct RangeLadder {
    static_assert(Chunk8MaxRangeSize < Chunk16MaxRangeSize);

    static constexpr AdaptiveRadixChunkWidth
    chunkWidthForRange(std::size_t rangeSize) noexcept {
        if (rangeSize <= Chunk8MaxRangeSize) {
            return AdaptiveRadixChunkWidth::Chunk8;
        }
        if (rangeSize <= Chunk16MaxRangeSize) {
            return AdaptiveRadixChunkWidth::Chunk16;
        }
        return AdaptiveRadixChunkWidth::Chunk32;
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

template <std::size_t RadixChunkBytes, typename IdForIndex, typename IdTraits,
          typename CountScratch>
IdMsdChunkEntry<RadixChunkBytes> *
stableLsdSortIndexRangeByIdMsdChunkWithCounter(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex, IdMsdChunkEntry<RadixChunkBytes> *current,
    IdMsdChunkEntry<RadixChunkBytes> *next, CountScratch &countScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return current;
    }

    for (std::size_t offset = 0, orderOffset = rangeBegin; offset < rangeSize;
         ++offset, ++orderOffset) {
        const std::size_t itemIndex = order[orderOffset];
        current[offset] = {chunkMsbFirst<RadixChunkBytes>(idForIndex(itemIndex),
                                                          chunkIndex, traits),
                           itemIndex};
    }

    return stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled<
        RadixChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                         countScratch);
}

template <std::size_t RadixChunkBytes, typename CountPolicy, typename Scratch>
IdMsdChunkEntry<RadixChunkBytes> *dispatchLsdIndexMsdChunkSortPreFilled(
    std::vector<std::size_t> &order, std::size_t rangeBegin,
    std::size_t rangeEnd, IdMsdChunkEntry<RadixChunkBytes> *current,
    IdMsdChunkEntry<RadixChunkBytes> *next, Scratch &touchedScratch) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if constexpr (std::is_same_v<CountPolicy, FullClearCounts>) {
        FullClearCountScratch fullClearScratch;
        return stableLsdSortIndexRangeByIdMsdChunkWithCounterPreFilled<
            RadixChunkBytes>(order, rangeBegin, rangeEnd, current, next,
                             fullClearScratch);
    } else {
        if (rangeSize <= CountPolicy::max_size) {
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

template <std::size_t RadixChunkBytes, typename CountPolicy = FullClearCounts>
struct IdMsdChunkSortWorkspace {
    static constexpr std::size_t radix_chunk_bytes = RadixChunkBytes;

    std::vector<IdMsdChunkRange> pending;
    std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]> current;
    std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]> next;
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
        current = std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]>(
            new IdMsdChunkEntry<RadixChunkBytes>[rangeSize]);
        next = std::unique_ptr<IdMsdChunkEntry<RadixChunkBytes>[]>(
            new IdMsdChunkEntry<RadixChunkBytes>[rangeSize]);
        capacity = rangeSize;
    }
};

template <typename CountPolicy> struct IdMsdChunkLadderSortWorkspace {
    IdMsdChunkSortWorkspace<1, CountPolicy> chunk8;
    IdMsdChunkSortWorkspace<2, CountPolicy> chunk16;
    IdMsdChunkSortWorkspace<4, CountPolicy> chunk32;
};

template <std::size_t RadixChunkBytes, typename CountPolicy,
          std::size_t SmallThreshold, typename IdForIndex, typename IdTraits,
          typename ChunkExtractor, typename PushNextRange,
          typename SmallRangeSorter>
void processIdMsdChunkRange(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> &workspace,
    ChunkExtractor chunkExtractor, bool hasNextRange,
    PushNextRange pushNextRange, SmallRangeSorter smallRangeSorter) {
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    if (rangeSize <= SmallThreshold) {
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
        dispatchLsdIndexMsdChunkSortPreFilled<RadixChunkBytes, CountPolicy>(
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

template <std::size_t RadixChunkBytes, typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits>
void sortIndexRangeByIdMsdChunks(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex,
    IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> &workspace) {
    workspace.allocate(rangeEnd - rangeBegin);
    auto linearSmallRangeSorter =
        [](std::vector<std::size_t> &sortOrder, auto sortIdForIndex,
           const IdTraits &sortTraits, std::size_t sortBegin,
           std::size_t sortEnd) {
            stableSortIndexRangeSmallLinear<SmallThreshold>(
                sortOrder, sortIdForIndex, sortTraits, sortBegin, sortEnd);
        };
    sortIndexRangeByIdMsdChunksWithSmallSorter<RadixChunkBytes, CountPolicy,
                                               SmallThreshold>(
        order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex, workspace,
        linearSmallRangeSorter);
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

template <std::size_t RadixChunkBytes, typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename IdForIndex, typename IdTraits, typename SmallRangeSorter>
void sortIndexRangeByIdMsdChunksWithSmallSorter(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    std::size_t chunkIndex,
    IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> &workspace,
    SmallRangeSorter smallRangeSorter) {
    const std::size_t initialRangeSize = rangeEnd - rangeBegin;
    if (initialRangeSize <= 1) {
        return;
    }

    if (initialRangeSize <= SmallThreshold) {
        smallRangeSorter(order, idForIndex, traits, rangeBegin, rangeEnd);
        return;
    }

    workspace.pending.clear();
    workspace.pending.push_back(
        IdMsdChunkRange{rangeBegin, rangeEnd, chunkIndex});
    constexpr std::size_t chunkCount =
        (IdTraits::id_byte_count + RadixChunkBytes - 1) / RadixChunkBytes;

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
        processIdMsdChunkRange<RadixChunkBytes, CountPolicy, SmallThreshold>(
            order, idForIndex, traits, currentBegin, currentEnd, workspace,
            chunkExtractor, hasNextRange, pushNextRange, smallRangeSorter);
    }
}

template <std::size_t RadixChunkBytes, typename CountPolicy = FullClearCounts,
          std::size_t SmallThreshold = small_id_range_sort_threshold,
          typename Nodes, typename IdTraits, typename SmallRangeSorter>
void sortRangeByIdMsdChunksWithSmallSorter(
    std::vector<std::size_t> &order, const Nodes &nodes, const IdTraits &traits,
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t chunkIndex,
    IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> &workspace,
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
    sortIndexRangeByIdMsdChunksWithSmallSorter<RadixChunkBytes, CountPolicy,
                                               SmallThreshold>(
        order, idForIndex, traits, rangeBegin, rangeEnd, chunkIndex, workspace,
        adaptedSmallRangeSorter);
}

template <typename LadderPolicy, typename CountPolicy, typename IdForIndex,
          typename IdTraits>
void sortIndexRangeByIdMsdChunkLadder(
    std::vector<std::size_t> &order, IdForIndex idForIndex,
    const IdTraits &traits, std::size_t rangeBegin, std::size_t rangeEnd,
    IdMsdChunkLadderSortWorkspace<CountPolicy> &workspace) {
    // Select once for the submitted range, then run the exact fixed-width
    // kernel. Re-selecting for recursive prefix children can create unaligned
    // chunk windows and makes the ladder incomparable with standalone rows.
    switch (LadderPolicy::chunkWidthForRange(rangeEnd - rangeBegin)) {
    case AdaptiveRadixChunkWidth::Chunk8:
        sortIndexRangeByIdMsdChunks<1, CountPolicy>(order, idForIndex, traits,
                                                    rangeBegin, rangeEnd, 0,
                                                    workspace.chunk8);
        break;
    case AdaptiveRadixChunkWidth::Chunk16:
        sortIndexRangeByIdMsdChunks<2, CountPolicy>(order, idForIndex, traits,
                                                    rangeBegin, rangeEnd, 0,
                                                    workspace.chunk16);
        break;
    case AdaptiveRadixChunkWidth::Chunk32:
        sortIndexRangeByIdMsdChunks<4, CountPolicy>(order, idForIndex, traits,
                                                    rangeBegin, rangeEnd, 0,
                                                    workspace.chunk32);
        break;
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_RADIX_HPP
