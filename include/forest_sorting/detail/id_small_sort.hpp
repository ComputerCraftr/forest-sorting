#ifndef FOREST_SORTING_DETAIL_ID_SMALL_SORT_HPP
#define FOREST_SORTING_DETAIL_ID_SMALL_SORT_HPP

#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_compare.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

template <typename IdForIndex, typename IdTraits, typename CachedScratch>
struct CachedKeyAccessor {
    const std::vector<std::size_t> &order;
    IdForIndex idForIndex;
    const IdTraits &traits;
    std::size_t rangeBegin;
    CachedScratch &idChunks;

    using CachedIdType = std::remove_reference_t<CachedScratch>::value_type;
    CachedIdType savedId{};

    void initialize(std::size_t keyCount) {
        for (std::size_t offset = 0; offset < keyCount; ++offset) {
            fillCachedChunkId(idChunks[offset],
                              idForIndex(order[rangeBegin + offset]), traits);
        }
    }

    void save(std::size_t localIdx) { savedId = idChunks[localIdx]; }

    bool isLessOrEqual(std::size_t otherLocalIdx) const noexcept {
        return compareCachedIdChunks(idChunks[otherLocalIdx], savedId) <= 0;
    }

    void move(std::size_t srcLocalIdx, std::size_t destLocalIdx) noexcept {
        idChunks[destLocalIdx] = idChunks[srcLocalIdx];
    }

    void writeSaved(std::size_t destLocalIdx) noexcept {
        idChunks[destLocalIdx] = savedId;
    }
};

template <typename IdForIndex, typename IdTraits> struct DirectKeyAccessor {
    const std::vector<std::size_t> &order;
    IdForIndex idForIndex;
    const IdTraits &traits;
    std::size_t rangeBegin;

    using IdType = std::decay_t<decltype(std::declval<IdForIndex &>()(
        std::declval<std::size_t>()))>;
    IdType savedId;

    DirectKeyAccessor(const std::vector<std::size_t> &sortOrder,
                      IdForIndex sortIdForIndex, const IdTraits &sortTraits,
                      std::size_t sortRangeBegin)
        : order(sortOrder), idForIndex(std::move(sortIdForIndex)),
          traits(sortTraits), rangeBegin(sortRangeBegin),
          savedId(idForIndex(order[rangeBegin])) {}

    void initialize([[maybe_unused]] std::size_t keyCount) noexcept {}

    void save(std::size_t localIdx) {
        savedId = idForIndex(order[rangeBegin + localIdx]);
    }

    bool isLessOrEqual(std::size_t otherLocalIdx) const {
        return !idLess(savedId, idForIndex(order[rangeBegin + otherLocalIdx]),
                       traits);
    }

    void move([[maybe_unused]] std::size_t srcLocalIdx,
              [[maybe_unused]] std::size_t destLocalIdx) noexcept {}

    void writeSaved([[maybe_unused]] std::size_t destLocalIdx) noexcept {}
};

template <std::size_t MaxRangeSize>
void requireFixedSmallSortCapacity(std::size_t rangeSize) {
    if (rangeSize > MaxRangeSize) {
        throw std::runtime_error(
            "small sorter range exceeds fixed scratch capacity");
    }
}

template <std::size_t MaxRangeSize, typename IdForIndex, typename IdTraits,
          typename Algorithm>
void withFixedSmallSortAccessor(const std::vector<std::size_t> &order,
                                IdForIndex idForIndex, const IdTraits &traits,
                                std::size_t rangeBegin, std::size_t rangeEnd,
                                Algorithm algorithm) {
    assert(rangeBegin <= rangeEnd);
    assert(rangeEnd <= order.size());
    const std::size_t rangeSize = rangeEnd - rangeBegin;
    if (rangeSize <= 1) {
        return;
    }

    if constexpr (shouldCacheChunkIds<IdTraits>) {
        requireFixedSmallSortCapacity<MaxRangeSize>(rangeSize);
        constexpr std::size_t chunkCount =
            idChunkCount<cached_comparison_chunk_bytes, IdTraits>;
        using CachedId = CachedChunkId<chunkCount>;
        std::array<CachedId, MaxRangeSize> idChunks;
        CachedKeyAccessor<IdForIndex, IdTraits, decltype(idChunks)> accessor{
            order, idForIndex, traits, rangeBegin, idChunks};
        accessor.initialize(rangeSize);
        algorithm(accessor);
    } else {
        DirectKeyAccessor<IdForIndex, IdTraits> accessor{order, idForIndex,
                                                         traits, rangeBegin};
        accessor.initialize(rangeSize);
        algorithm(accessor);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_SMALL_SORT_HPP
