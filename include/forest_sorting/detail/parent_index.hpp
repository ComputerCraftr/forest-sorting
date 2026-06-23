#ifndef FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
#define FOREST_SORTING_DETAIL_PARENT_INDEX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_permutation_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/detail/validation.hpp"

#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

namespace forest_sorting::detail {

struct RadixParentIndexResult {
    std::vector<std::size_t> parentIndex;
    std::vector<std::size_t> idPermutation;
};

template <typename Compare, typename Equal, typename SetParent>
inline void
mergeJoinSortedPermutations(const std::vector<std::size_t> &queryPermutation,
                            const std::vector<std::size_t> &idPermutation,
                            Compare compare, Equal equal, SetParent setParent) {
    std::size_t idOffset = 0;
    const std::size_t idSize = idPermutation.size();
    for (std::size_t queryIndex : queryPermutation) {
        while (idOffset < idSize) {
            const std::size_t idIndex = idPermutation[idOffset];
            const int cmp = compare(idIndex, queryIndex);
            const bool advance = cmp < 0;
            idOffset += advance ? 1U : 0U;
            if (!advance) {
                if (cmp == 0 && equal(idIndex, queryIndex)) {
                    setParent(queryIndex, idIndex);
                }
                break;
            }
        }
    }
}

template <typename Nodes, typename Traits, typename SortPermutation>
RadixParentIndexResult buildParentIndexRadixJoinWithPermutationSorter(
    const Nodes &nodes, const Traits &traits, SortPermutation sortPermutation) {
    using Id = Traits::Id;
    std::vector<Id> ids;
    ids.reserve(nodes.size());
    std::vector<Id> parentIds;
    parentIds.reserve(nodes.size());
    std::vector<std::size_t> childIndexes;
    childIndexes.reserve(nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        ids.push_back(traits.id(nodes[nodeIndex]));
        const Id parentId = traits.parent_id(nodes[nodeIndex]);
        if (!isParentSentinel(traits, parentId)) {
            parentIds.push_back(parentId);
            childIndexes.push_back(nodeIndex);
        }
    }

    std::vector<std::size_t> idPermutation(ids.size());
    std::iota(idPermutation.begin(), idPermutation.end(), 0);
    std::vector<std::size_t> queryPermutation(parentIds.size());
    std::iota(queryPermutation.begin(), queryPermutation.end(), 0);

    return withIdPermutationComparator(
        ids, parentIds, traits, [&](const auto &comparator) {
            auto idForEntryIndex =
                [&](std::size_t entryIndex) -> decltype(auto) {
                return comparator.leftIdForSort(entryIndex);
            };
            sortPermutation(idPermutation, idForEntryIndex,
                            comparator.sortTraits());

            auto idForQueryIndex =
                [&](std::size_t queryIndex) -> decltype(auto) {
                return comparator.rightIdForSort(queryIndex);
            };
            sortPermutation(queryPermutation, idForQueryIndex,
                            comparator.sortTraits());

            rejectAdjacentDuplicates(
                idPermutation,
                [&](std::size_t lhsIndex, std::size_t rhsIndex) noexcept {
                    return comparator.leftEqual(lhsIndex, rhsIndex);
                },
                "duplicate node id");

            std::vector<std::size_t> parentIndex(nodes.size(), no_parent);
            auto compare = [&](std::size_t idIndex,
                               std::size_t queryIndex) noexcept {
                return comparator.compare(idIndex, queryIndex);
            };
            auto equal = [&](std::size_t idIndex,
                             std::size_t queryIndex) noexcept {
                return comparator.crossEqual(idIndex, queryIndex);
            };
            auto setParent = [&](std::size_t queryIndex,
                                 std::size_t parentNodeIndex) noexcept {
                parentIndex[childIndexes[queryIndex]] = parentNodeIndex;
            };

            mergeJoinSortedPermutations(queryPermutation, idPermutation,
                                        compare, equal, setParent);
            return RadixParentIndexResult{std::move(parentIndex),
                                          std::move(idPermutation)};
        });
}

template <std::size_t RadixChunkBytes,
          typename CountPolicy = ProductionIdCountPolicy, typename Nodes,
          typename Traits>
RadixParentIndexResult
buildParentIndexRadixJoinResultByMsdChunks(const Nodes &nodes,
                                           const Traits &traits) {
    IdMsdChunkSortWorkspace<RadixChunkBytes, CountPolicy> workspace;
    auto sortPermutation = [&](std::vector<std::size_t> &permutation,
                               auto idForIndex, const auto &sortTraits) {
        sortIndexRangeByIdMsdChunks<RadixChunkBytes, CountPolicy>(
            permutation, idForIndex, sortTraits, 0, permutation.size(), 0,
            workspace);
    };
    return buildParentIndexRadixJoinWithPermutationSorter(nodes, traits,
                                                          sortPermutation);
}

template <typename Nodes, typename Traits>
RadixParentIndexResult buildParentIndexRadixJoinResult(const Nodes &nodes,
                                                       const Traits &traits) {
    return buildParentIndexRadixJoinResultByMsdChunks<
        production_id_radix_chunk_bytes>(nodes, traits);
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexRadixJoin(const Nodes &nodes,
                                                   const Traits &traits) {
    auto result = buildParentIndexRadixJoinResult(nodes, traits);
    return std::move(result.parentIndex);
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndex(const Nodes &nodes,
                                          const Traits &traits) {
    return buildParentIndexRadixJoin(nodes, traits);
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
