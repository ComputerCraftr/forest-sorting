#ifndef FOREST_SORTING_DETAIL_ID_PERMUTATION_COMPARE_HPP
#define FOREST_SORTING_DETAIL_ID_PERMUTATION_COMPARE_HPP

#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_compare.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

template <typename Id, typename Traits> class DirectIdPermutationComparator {
  public:
    DirectIdPermutationComparator(const std::vector<Id> &leftIds,
                                  const std::vector<Id> &rightIds,
                                  const Traits &traits)
        : leftIds_(leftIds), rightIds_(rightIds), traits_(traits) {}

    const Id &leftIdForSort(std::size_t index) const noexcept {
        return leftIds_[index];
    }

    const Id &rightIdForSort(std::size_t index) const noexcept {
        return rightIds_[index];
    }

    const Traits &sortTraits() const noexcept { return traits_; }

    int compare(std::size_t leftIndex, std::size_t rightIndex) const noexcept {
        return compareNodeIds(leftIds_[leftIndex], rightIds_[rightIndex],
                              traits_);
    }

    bool crossEqual(std::size_t leftIndex,
                    std::size_t rightIndex) const noexcept {
        return idEqual(leftIds_[leftIndex], rightIds_[rightIndex], traits_);
    }

    bool leftEqual(std::size_t lhsIndex, std::size_t rhsIndex) const noexcept {
        return idEqual(leftIds_[lhsIndex], leftIds_[rhsIndex], traits_);
    }

  private:
    const std::vector<Id> &leftIds_;
    const std::vector<Id> &rightIds_;
    const Traits &traits_;
};

template <typename Id, typename Traits, std::size_t ChunkCount>
class CachedIdPermutationComparator {
  public:
    using CachedId = CachedChunkId<ChunkCount>;
    using CachedTraits = CachedChunkIdTraits<Traits, ChunkCount>;

    CachedIdPermutationComparator(const std::vector<Id> &leftIds,
                                  const std::vector<Id> &rightIds,
                                  const Traits &traits)
        : leftIds_(leftIds), rightIds_(rightIds), traits_(traits),
          cachedLeftIds_(leftIds.size()), cachedRightIds_(rightIds.size()) {
        for (std::size_t index = 0; index < leftIds_.size(); ++index) {
            fillCachedChunkId(cachedLeftIds_[index], leftIds_[index], traits_);
        }
        for (std::size_t index = 0; index < rightIds_.size(); ++index) {
            fillCachedChunkId(cachedRightIds_[index], rightIds_[index],
                              traits_);
        }
    }

    const CachedId &leftIdForSort(std::size_t index) const noexcept {
        return cachedLeftIds_[index];
    }

    const CachedId &rightIdForSort(std::size_t index) const noexcept {
        return cachedRightIds_[index];
    }

    const CachedTraits &sortTraits() const noexcept { return cachedTraits_; }

    int compare(std::size_t leftIndex, std::size_t rightIndex) const noexcept {
        return compareCachedIdChunks(cachedLeftIds_[leftIndex],
                                     cachedRightIds_[rightIndex]);
    }

    bool crossEqual(std::size_t leftIndex,
                    std::size_t rightIndex) const noexcept {
        return idEqual(leftIds_[leftIndex], rightIds_[rightIndex], traits_);
    }

    bool leftEqual(std::size_t lhsIndex, std::size_t rhsIndex) const noexcept {
        return compareCachedIdChunks(cachedLeftIds_[lhsIndex],
                                     cachedLeftIds_[rhsIndex]) == 0 &&
               idEqual(leftIds_[lhsIndex], leftIds_[rhsIndex], traits_);
    }

  private:
    const std::vector<Id> &leftIds_;
    const std::vector<Id> &rightIds_;
    const Traits &traits_;
    std::vector<CachedId> cachedLeftIds_;
    std::vector<CachedId> cachedRightIds_;
    CachedTraits cachedTraits_;
};

template <typename Id, typename Traits, typename Callback>
decltype(auto) withIdPermutationComparator(const std::vector<Id> &leftIds,
                                           const std::vector<Id> &rightIds,
                                           const Traits &traits,
                                           Callback callback) {
    if constexpr (shouldCacheChunkIds<Traits>) {
        constexpr std::size_t chunkCount =
            (Traits::id_byte_count + cached_comparison_chunk_bytes - 1) /
            cached_comparison_chunk_bytes;
        CachedIdPermutationComparator<Id, Traits, chunkCount> comparator(
            leftIds, rightIds, traits);
        return std::forward<Callback>(callback)(comparator);
    } else {
        DirectIdPermutationComparator<Id, Traits> comparator(leftIds, rightIds,
                                                             traits);
        return std::forward<Callback>(callback)(comparator);
    }
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_ID_PERMUTATION_COMPARE_HPP
