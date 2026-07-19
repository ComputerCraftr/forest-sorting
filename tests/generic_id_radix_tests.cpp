#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_permutation_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "hashed_test_bytes.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <type_traits>
#include <vector>

namespace {

using namespace forest_sorting::test_support;

template <std::size_t ByteCount> void assert_chunk_comparison_matches_bytes() {
    using Id = TestBytes<ByteCount>;
    const TestBytesTraits<ByteCount> traits;

    const auto requireSameComparison = [&](const Id &lhs, const Id &rhs) {
        const int chunkComparison =
            forest_sorting::detail::compareIdsMsbFirst(lhs, rhs, traits);
        int byteComparison = 0;
        if (lhs < rhs) {
            byteComparison = -1;
        } else if (rhs < lhs) {
            byteComparison = 1;
        }
        require(chunkComparison == byteComparison,
                "chunk comparison differed from byte lexicographic order");
        require(forest_sorting::detail::idLess(lhs, rhs, traits) ==
                    (byteComparison < 0),
                "idLess differed from chunk comparison");
    };

    Id equal{};
    requireSameComparison(equal, equal);

    Id firstChunkLow{};
    Id firstChunkHigh{};
    firstChunkLow.bytes[0] = 1;
    firstChunkHigh.bytes[0] = 2;
    requireSameComparison(firstChunkLow, firstChunkHigh);
    requireSameComparison(firstChunkHigh, firstChunkLow);

    Id laterChunkLow{};
    Id laterChunkHigh{};
    constexpr std::size_t laterByte = ByteCount > 8 ? 8 : ByteCount - 1;
    laterChunkLow.bytes[laterByte] = 1;
    laterChunkHigh.bytes[laterByte] = 2;
    requireSameComparison(laterChunkLow, laterChunkHigh);

    Id finalByteLow{};
    Id finalByteHigh{};
    finalByteLow.bytes[ByteCount - 1] = 1;
    finalByteHigh.bytes[ByteCount - 1] = 2;
    requireSameComparison(finalByteLow, finalByteHigh);
}

template <std::size_t ByteCount> void assert_id_permutation_comparator_paths() {
    using Id = TestBytes<ByteCount>;
    using Traits = HashFreeTestBytesTraits<ByteCount>;

    Id low{};
    low.bytes[ByteCount - 1] = 1;
    Id sameLow = low;
    Id middle{};
    middle.bytes[ByteCount / 2] = 1;
    Id high{};
    high.bytes[0] = 1;

    const std::vector<Id> leftIds = {low, sameLow, middle, high};
    const std::vector<Id> rightIds = {low, middle, high};
    bool callbackInvoked = false;
    forest_sorting::detail::withIdPermutationComparator(
        leftIds, rightIds, Traits{}, [&](const auto &comparator) {
            callbackInvoked = true;
            using SortId =
                std::remove_cvref_t<decltype(comparator.leftIdForSort(
                    std::size_t{0}))>;
            if constexpr (forest_sorting::detail::shouldCacheChunkIds<Traits>) {
                require(!std::same_as<SortId, Id>,
                        "cached comparator exposed the original ID type");
            } else {
                require(std::same_as<SortId, Id>,
                        "direct comparator did not expose the original ID");
            }

            require(comparator.compare(0, 0) == 0,
                    "cross comparator rejected equal IDs");
            require(comparator.compare(0, 1) < 0,
                    "cross comparator misordered a later chunk");
            require(comparator.compare(3, 2) == 0,
                    "cross comparator misordered the first chunk");
            require(comparator.crossEqual(0, 0),
                    "cross equality rejected equal IDs");
            require(!comparator.crossEqual(0, 1),
                    "cross equality accepted different IDs");
            require(comparator.leftEqual(0, 1),
                    "left equality rejected a duplicate ID");
            require(!comparator.leftEqual(1, 2),
                    "left equality accepted different IDs");

            const auto &sortTraits = comparator.sortTraits();
            require(sortTraits.byte_msb_first(comparator.leftIdForSort(3), 0) ==
                        1,
                    "sort accessor or traits changed ID bytes");
        });
    require(callbackInvoked, "ID permutation comparator callback was skipped");
}

void test_id_permutation_comparator_cached_and_direct_paths() {
    assert_id_permutation_comparator_paths<4>();
    assert_id_permutation_comparator_paths<16>();
    assert_id_permutation_comparator_paths<20>();
    assert_id_permutation_comparator_paths<28>();
    assert_id_permutation_comparator_paths<37>();
    assert_id_permutation_comparator_paths<64>();
}

void test_merge_join_sorted_permutations_uses_injected_predicates() {
    const std::vector<int> ids = {1, 3, 5};
    const std::vector<int> queries = {0, 1, 2, 3, 4, 5, 6};
    const std::vector<std::size_t> idPermutation = {0, 1, 2};
    const std::vector<std::size_t> queryPermutation = {0, 1, 2, 3, 4, 5, 6};
    std::vector<std::size_t> matches(queries.size(),
                                     forest_sorting::detail::no_parent);

    auto compare = [&](std::size_t idIndex, std::size_t queryIndex) {
        // mergeJoinSortedPermutations advances the ID cursor when cmp < 0,
        // so return the standard three-way comparison sign for id vs query.
        return static_cast<int>(ids[idIndex] > queries[queryIndex]) -
               static_cast<int>(ids[idIndex] < queries[queryIndex]);
    };
    auto equal = [&](std::size_t idIndex, std::size_t queryIndex) {
        return ids[idIndex] == queries[queryIndex];
    };
    auto setMatch = [&](std::size_t queryIndex, std::size_t idIndex) {
        matches[queryIndex] = idIndex;
    };

    forest_sorting::detail::mergeJoinSortedPermutations(
        queryPermutation, idPermutation, compare, equal, setMatch);

    require(matches[0] == forest_sorting::detail::no_parent);
    require(matches[1] == 0);
    require(matches[2] == forest_sorting::detail::no_parent);
    require(matches[3] == 1);
    require(matches[4] == forest_sorting::detail::no_parent);
    require(matches[5] == 2);
    require(matches[6] == forest_sorting::detail::no_parent);
}

#define FS_GENERIC_ID_BYTE_WIDTHS(X)                                           \
    X(16)                                                                      \
    X(20)                                                                      \
    X(28)                                                                      \
    X(32)                                                                      \
    X(37)                                                                      \
    X(64)

void test_chunk_comparison_matches_byte_lexicographic_order() {
#define X(width) assert_chunk_comparison_matches_bytes<(width)>();
    FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X
}

template <std::size_t ByteCount>
void assert_chunk_permutation_sort_matches_stable_comparison() {
    using Id = TestBytes<ByteCount>;
    const TestBytesTraits<ByteCount> traits;
    std::vector<Id> ids(80);
    for (std::size_t index = 0; index < ids.size(); ++index) {
        ids[index].bytes[0] = static_cast<uint8_t>((index / 10) % 4);
        ids[index].bytes[ByteCount - 1] =
            static_cast<uint8_t>((index / 2) % 17);
    }

    std::vector<std::size_t> expected(ids.size());
    std::iota(expected.begin(), expected.end(), 0);
    std::reverse(expected.begin(), expected.end());
    std::vector<std::size_t> actual = expected;
    std::stable_sort(
        expected.begin(), expected.end(),
        [&](std::size_t lhs, std::size_t rhs) { return ids[lhs] < ids[rhs]; });

    forest_sorting::detail::IdMsdChunkSortWorkspace<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>
        workspace;
    auto idForIndex = [&](std::size_t index) { return ids[index]; };
    forest_sorting::detail::sortIndexRangeByIdMsdChunks<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>(
        actual, idForIndex, traits, 0, actual.size(), 0, workspace);

    require(actual == expected,
            "chunk permutation sort differed from stable comparison");
}

void test_chunk_permutation_sort_generic_id_widths() {
#define X(width)                                                               \
    assert_chunk_permutation_sort_matches_stable_comparison<(width)>();
    FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X
}

void runGenericIdRadixTestsImpl() {
    runTest("chunk comparison matches byte lexicographic order",
            test_chunk_comparison_matches_byte_lexicographic_order);
    runTest("chunk permutation sort supports generic ID widths",
            test_chunk_permutation_sort_generic_id_widths);
    runTest("ID permutation comparator cached and direct paths",
            test_id_permutation_comparator_cached_and_direct_paths);
    runTest("merge join uses injected predicates",
            test_merge_join_sorted_permutations_uses_injected_predicates);
}

} // namespace

void runGenericIdRadixTests() { runGenericIdRadixTestsImpl(); }
