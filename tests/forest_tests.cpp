#include "adaptive_sort_variants.hpp"
#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/hash.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "parent_index_baselines.hpp"
#include "sort_baselines.hpp"
#include "sort_registry.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::UInt128Traits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

using forest_sorting::detail::buildParentIndex;
using forest_sorting::detail::buildParentIndexRadixJoin;

static_assert(std::is_same_v<CompositeDepth2UInt128Key::Depth, uint16_t>);

void runGenericApiAndDepthTests();
void runBenchmarkSupportTests();

void requireUniqueDatasetParentAndSortRegistries() {
    const auto datasetKinds = allDatasetKinds();
    require(!datasetKinds.empty(), "dataset registry is empty");
    for (std::size_t kindIdx = 0; kindIdx < datasetKinds.size(); ++kindIdx) {
        require(!datasetName(datasetKinds[kindIdx]).empty(),
                "dataset registry contains an empty name");
        for (std::size_t otherIdx = kindIdx + 1; otherIdx < datasetKinds.size();
             ++otherIdx) {
            require(datasetKinds[kindIdx] != datasetKinds[otherIdx],
                    "dataset registry contains duplicate kind");
            require(datasetName(datasetKinds[kindIdx]) !=
                        datasetName(datasetKinds[otherIdx]),
                    "dataset registry contains duplicate name");
        }
    }

    const auto parentKinds = registeredParentKinds();
    require(!parentKinds.empty(), "parent registry is empty");
    for (std::size_t kindIdx = 0; kindIdx < parentKinds.size(); ++kindIdx) {
        require(!parentName(parentKinds[kindIdx]).empty(),
                "parent registry contains an empty name");
        for (std::size_t otherIdx = kindIdx + 1; otherIdx < parentKinds.size();
             ++otherIdx) {
            require(parentKinds[kindIdx] != parentKinds[otherIdx],
                    "parent registry contains duplicate kind");
            require(parentName(parentKinds[kindIdx]) !=
                        parentName(parentKinds[otherIdx]),
                    "parent registry contains duplicate name");
        }
    }
    const auto defParentKinds = defaultParentKinds();
    require(std::ranges::find(defParentKinds, ParentKind::RadixByteMsd) ==
                defParentKinds.end(),
            "byte-MSD parent baseline must remain opt-in");

    validateSortRegistry();
}

void requireSortIsExplicitOptIn(SortKind sortKind, std::string_view message) {
    const auto defaultSorts = defaultSortKinds();
    require(std::ranges::find(defaultSorts, sortKind) == defaultSorts.end(),
            message);
}

void assertParentBuildersMatch(const std::vector<Node> &nodes) {
    const auto expected = buildParentIndexForKind(ParentKind::Unordered, nodes);
    for (ParentKind parentKind : registeredParentKinds()) {
        const auto actual = buildParentIndexForKind(parentKind, nodes);
        if (actual != expected) {
            throw std::runtime_error(std::string(parentName(parentKind)) +
                                     " parent builder differs from unordered "
                                     "map");
        }
    }
}

void requireAllRegisteredSortsMatch(const std::vector<Node> &nodes,
                                    const std::vector<Node> &expected) {
    const auto artifacts =
        buildParentArtifactsForKind(ParentKind::Radix, nodes);
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.category == SortCategory::Alias) {
            continue;
        }
        const auto sorted = sortForestForKind(
            entry.kind, nodes, artifacts.parentIndex, &artifacts.idPermutation);
        if (!sameNodes(sorted, expected)) {
            throw std::runtime_error(std::string(entry.name) +
                                     " sort differed from canonical order");
        }
        if (!verifySortedByDepthAndId(sorted)) {
            throw std::runtime_error(std::string(entry.name) +
                                     " output failed verification");
        }
    }
}

void test_support_registries_are_unique() {
    requireUniqueDatasetParentAndSortRegistries();

    require(datasetName(DatasetKind::SameHigh32) == "same-high32",
            "same-high32 dataset label is not registered");
}

void test_removed_generation_touched_count_labels_do_not_parse() {
    constexpr std::string_view kRemovedLabels[] = {
        "adaptive-depth2-u8-chunk-msd-touched-counts",
        "adaptive-depth2-u32-chunk-msd-touched-counts",
        "adaptive-depth2-u64-chunk-msd-touched-counts",
        "adaptive-depth2-u32-chunk-msd-touched-counts-128",
        "adaptive-depth2-u32-chunk-msd-binary-small48",
        "adaptive-depth2-u32-chunk-msd-exponential-small48"};

    for (std::string_view removedLabel : kRemovedLabels) {
        bool rejected = false;
        try {
            (void)parseSortKind(removedLabel);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected,
                "removed label still parsed: " + std::string(removedLabel));
    }
}

void test_bitmask_touched_chunk_sort_reuses_scratch() {
    std::vector<Node> nodes = {
        {makeId(0xF000000000000000ULL, 1), 0},
        {makeId(0x1000000000000000ULL, 2), 0},
        {makeId(0xA000000000000000ULL, 3), 0},
        {makeId(0x2000000000000000ULL, 4), 0},
    };
    std::vector<std::size_t> order = {0, 1, 2, 3};
    std::vector<forest_sorting::detail::ChunkedIndex<1>> current(order.size());
    std::vector<forest_sorting::detail::ChunkedIndex<1>> next(order.size());
    forest_sorting::detail::BitmaskTouchedCountScratch scratch;
    const UInt128NodeTraits traits;
    auto idForIndex = [&](std::size_t nodeIndex) {
        return UInt128NodeTraits::id(nodes[nodeIndex]);
    };

    forest_sorting::detail::stableLsdSortIndexRangeByIdChunkWithCounter(
        order, idForIndex, traits, 0, order.size(), 0, current.data(),
        next.data(), scratch);
    require((order == std::vector<std::size_t>{1, 3, 2, 0}),
            "first bitmask touched chunk sort produced wrong order");

    nodes = {
        {makeId(0x0300000000000000ULL, 1), 0},
        {makeId(0x0100000000000000ULL, 2), 0},
        {makeId(0x0200000000000000ULL, 3), 0},
        {makeId(0x0400000000000000ULL, 4), 0},
    };
    order = {0, 1, 2, 3};
    forest_sorting::detail::stableLsdSortIndexRangeByIdChunkWithCounter(
        order, idForIndex, traits, 0, order.size(), 0, current.data(),
        next.data(), scratch);
    require((order == std::vector<std::size_t>{1, 2, 0, 3}),
            "second bitmask touched chunk sort reused stale counts");
}

void test_parent_builders_match_for_registered_datasets() {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        assertParentBuildersMatch(
            makeGeneratedForestForKind(datasetKind, 10000));
    }
}

void test_radix_parent_artifacts_retain_sorted_node_permutations() {
    constexpr std::array<ParentKind, 2> kRadixKinds = {
        ParentKind::Radix, ParentKind::RadixByteMsd};
    const UInt128NodeTraits traits;

    for (DatasetKind datasetKind : allDatasetKinds()) {
        const auto nodes = makeGeneratedForestForKind(datasetKind, 1000);
        const auto expectedParent =
            buildParentIndexForKind(ParentKind::Control, nodes);
        for (ParentKind parentKind : kRadixKinds) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            require(artifacts.parentIndex == expectedParent,
                    "radix artifact parent index differs from control");
            require(artifacts.idPermutation.size() == nodes.size(),
                    "radix artifact ID permutation has wrong size");

            std::vector<bool> seen(nodes.size(), false);
            for (std::size_t offset = 0;
                 offset < artifacts.idPermutation.size(); ++offset) {
                const std::size_t nodeIndex = artifacts.idPermutation[offset];
                require(nodeIndex < nodes.size(),
                        "radix artifact ID permutation index is invalid");
                require(!seen[nodeIndex],
                        "radix artifact ID permutation contains duplicates");
                seen[nodeIndex] = true;
                if (offset > 0) {
                    const std::size_t previousIndex =
                        artifacts.idPermutation[offset - 1];
                    require(!forest_sorting::detail::idLess(
                                nodes[nodeIndex].id, nodes[previousIndex].id,
                                traits),
                            "radix artifact ID permutation is not sorted");
                }
            }
        }
    }
}

void test_global_id_first_sort_computes_or_reuses_id_permutation() {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        const auto nodes = makeGeneratedForestForKind(datasetKind, 1000);
        for (ParentKind parentKind : registeredParentKinds()) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            const auto expected =
                sortForestByComparisonWithParent(nodes, artifacts.parentIndex);
            const std::vector<std::size_t> *idPermutation =
                artifacts.hasIdPermutation ? &artifacts.idPermutation : nullptr;
            const auto actual = sortForestForKind(
                SortKind::AdaptiveDepth2GlobalIdRadixStableDepth, nodes,
                artifacts.parentIndex, idPermutation);
            require(sameNodes(actual, expected),
                    std::string(parentName(parentKind)) +
                        " global-ID-first sort differed from comparison");

            const auto computed = sortForestForKind(
                SortKind::AdaptiveDepth2GlobalIdRadixStableDepth, nodes,
                artifacts.parentIndex, nullptr);
            require(sameNodes(computed, actual),
                    "computed and reused ID permutations produced different "
                    "orders");
        }
    }
}

void test_parent_builders_reject_duplicate_full_uint128_id() {
    const UInt128 duplicateId = makeId(7, 11);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    for (ParentKind parentKind : registeredParentKinds()) {
        bool rejected = false;
        try {
            (void)buildParentIndexForKind(parentKind, nodes);
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        if (!rejected) {
            throw std::runtime_error(std::string(parentName(parentKind)) +
                                     " parent builder accepted duplicate full "
                                     "id");
        }
    }
}

template <typename Traits>
void requireUInt128IdentityHashProductionMatchesRadix(
    const std::vector<Node> &nodes, const Traits &traits,
    std::string_view message) {
    const auto productionParent = buildParentIndex(nodes, traits);
    const auto radixParent = buildParentIndexRadixJoin(nodes, traits);
    require(productionParent == radixParent, message);
}

void test_parent_builder_falls_back_when_insert_probe_limit_exceeded() {
    constexpr std::size_t groupSize = 8;
    const std::size_t nodeCount =
        (forest_sorting::detail::max_probe_groups_before_fallback * groupSize) +
        1;
    requireUInt128IdentityHashProductionMatchesRadix(
        makeHighIdentityCollisionRoots(nodeCount),
        UInt128HighIdentityHashTraits{},
        "insert probe-limit fallback differed from radix join");
}

void test_parent_builder_falls_back_when_lookup_probe_limit_exceeded() {
    constexpr std::size_t groupSize = 8;
    const std::size_t nodeCount =
        forest_sorting::detail::max_probe_groups_before_fallback * groupSize;
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    const UInt128 externalParent = makeId(50000, 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL),
                             externalParent});
    }

    requireUInt128IdentityHashProductionMatchesRadix(
        nodes, UInt128LowIdentityHashTraits{},
        "lookup probe-limit fallback differed from radix join");
}

void test_parent_builder_identity_hash_rejects_duplicate_full_id() {
    const UInt128 duplicateId = makeId(7, 11);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    bool rejected = false;
    try {
        (void)buildParentIndex(nodes, UInt128HighIdentityHashTraits{});
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "identity-hash production parent builder accepted "
                      "duplicate full id");
}

void test_compute_depths_simple_chain() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},            // depth 0
        {makeId(0, 2), makeId(0, 1)}, // depth 1
        {makeId(0, 3), makeId(0, 2)}, // depth 2
    };

    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    require(depths.size() == 3);
    require(depths[0] == 0);
    require(depths[1] == 1);
    require(depths[2] == 2);
}

void requireSortRejectsParentCycle(const std::vector<Node> &nodes,
                                   std::string_view caseName) {
    bool rejected = false;
    try {
        (void)sortForestByDepthAndId(nodes);
    } catch (const std::runtime_error &error) {
        rejected = std::string_view(error.what()) == "parent cycle";
    }
    require(rejected, std::string(caseName) + " was not rejected");
}

void test_sort_rejects_self_parent_cycle() {
    const UInt128 nodeId = makeId(0, 1);
    requireSortRejectsParentCycle({Node{nodeId, nodeId}}, "self-parent cycle");
}

void test_sort_rejects_two_node_parent_cycle() {
    const UInt128 firstId = makeId(0, 1);
    const UInt128 secondId = makeId(0, 2);
    requireSortRejectsParentCycle(
        {Node{firstId, secondId}, Node{secondId, firstId}},
        "two-node parent cycle");
}

void test_sort_rejects_long_parent_cycle_and_tail() {
    const UInt128 firstId = makeId(0, 1);
    const UInt128 secondId = makeId(0, 2);
    const UInt128 thirdId = makeId(0, 3);
    const UInt128 tailId = makeId(0, 4);
    requireSortRejectsParentCycle(
        {Node{tailId, firstId}, Node{firstId, secondId},
         Node{secondId, thirdId}, Node{thirdId, firstId}},
        "tail entering parent cycle");
}

void test_copy_and_in_place_sort_propagate_parent_cycle() {
    const UInt128 firstId = makeId(0, 1);
    const UInt128 secondId = makeId(0, 2);
    const std::vector<Node> cycle = {
        {firstId, secondId},
        {secondId, firstId},
    };

    bool copyRejected = false;
    try {
        (void)forest_sorting::sortedCopyByDepthAndId(cycle,
                                                     UInt128NodeTraits{});
    } catch (const std::runtime_error &error) {
        copyRejected = std::string_view(error.what()) == "parent cycle";
    }
    require(copyRejected, "sorted copy did not propagate parent cycle");

    auto inPlaceCycle = cycle;
    bool inPlaceRejected = false;
    try {
        forest_sorting::sortInPlaceByDepthAndId(inPlaceCycle,
                                                UInt128NodeTraits{});
    } catch (const std::runtime_error &error) {
        inPlaceRejected = std::string_view(error.what()) == "parent cycle";
    }
    require(inPlaceRejected, "in-place sort did not propagate parent cycle");
}

void test_verify_rejects_parent_cycles() {
    const UInt128 firstId = makeId(0, 1);
    const UInt128 secondId = makeId(0, 2);
    const UInt128 thirdId = makeId(0, 3);

    require(!verifySortedByDepthAndId({Node{firstId, firstId}}),
            "verifier accepted self-parent cycle");
    require(!verifySortedByDepthAndId(
                {Node{firstId, secondId}, Node{secondId, firstId}}),
            "verifier accepted two-node parent cycle");
    require(!verifySortedByDepthAndId({Node{firstId, secondId},
                                       Node{secondId, thirdId},
                                       Node{thirdId, firstId}}),
            "verifier accepted longer parent cycle");
}

void test_sort_and_verify_multi_root() {
    std::vector<Node> nodes = {
        {makeId(0, 10), 0}, // root A
        {makeId(0, 20), 0}, // root B
        {makeId(0, 11), makeId(0, 10)},
        {makeId(0, 12), makeId(0, 10)},
        {makeId(0, 21), makeId(0, 20)},
    };

    auto sorted = sortForestByDepthAndId(nodes);
    require(verifySortedByDepthAndId(sorted));

    // Roots should be in id order.
    require(sorted[0].id == makeId(0, 10));
    require(sorted[1].id == makeId(0, 20));

    // Depth-1 nodes from the first root should come before depth-1 nodes of the
    // second root.
    require(sorted[2].id == makeId(0, 11));
    require(sorted[3].id == makeId(0, 12));
    require(sorted[4].id == makeId(0, 21));
}

void test_parent_radix_cached_path_differentiates_low64() {
    const UInt128 highSame = 0x1122334455667788ULL;
    // Nodes: Node 0 (ID B), Node 1 (ID A), Node 2 (Child of A), Node 3 (Child
    // of B)
    std::vector<Node> nodes = {
        {makeId(highSame, 2), 0},
        {makeId(highSame, 1), 0},
        {makeId(highSame, 3), makeId(highSame, 1)},
        {makeId(highSame, 4), makeId(highSame, 2)},
    };

    const auto artifacts =
        buildParentArtifactsForKind(ParentKind::Radix, nodes);

    require(artifacts.parentIndex[2] == 1,
            "child of A pointed to wrong parent index");
    require(artifacts.parentIndex[3] == 0,
            "child of B pointed to wrong parent index");

    std::size_t posA = nodes.size();
    std::size_t posB = nodes.size();
    for (std::size_t nodeIdx = 0; nodeIdx < artifacts.idPermutation.size();
         ++nodeIdx) {
        if (artifacts.idPermutation[nodeIdx] == 1) {
            posA = nodeIdx;
        } else if (artifacts.idPermutation[nodeIdx] == 0) {
            posB = nodeIdx;
        }
    }
    require(posA < posB, "ID A was not ordered before ID B in idPermutation");

    const auto expected =
        sortForestByComparisonWithParent(nodes, artifacts.parentIndex);
    requireAllRegisteredSortsMatch(nodes, expected);
}

void test_adaptive_sort_orders_by_high64_before_low64() {
    std::vector<Node> nodes = {
        {makeId(2, 0), 0},
        {makeId(1, UINT64_MAX), 0},
        {makeId(1, 0), 0},
        {makeId(0, UINT64_MAX), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(sorted[0].id == makeId(0, UINT64_MAX));
    require(sorted[1].id == makeId(1, 0));
    require(sorted[2].id == makeId(1, UINT64_MAX));
    require(sorted[3].id == makeId(2, 0));
}

void test_adaptive_sort_uses_low64_when_high64_matches() {
    std::vector<Node> nodes = {
        {makeId(9, 3), 0},
        {makeId(8, UINT64_MAX), 0},
        {makeId(9, 1), 0},
        {makeId(9, 2), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(verifySortedByDepthAndId(sorted));
    require(sorted[0].id == makeId(8, UINT64_MAX));
    require(sorted[1].id == makeId(9, 1));
    require(sorted[2].id == makeId(9, 2));
    require(sorted[3].id == makeId(9, 3));
}

void test_public_sort_matches_u32_chunk_support_path() {
    constexpr std::size_t nodeCount = 10000;
    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);

    const auto productionSorted = sortForestByDepthAndId(nodes);
    const auto u32SupportSorted =
        sortForestByAdaptiveDepth2U32ChunkWithParent(nodes, parentIndex);

    require(sameNodes(productionSorted, u32SupportSorted));
    require(verifySortedByDepthAndId(productionSorted));
}

void test_adaptive_sort_matches_comparison_for_shuffled_input() {
    std::vector<Node> nodes = {
        {makeId(0, 40), 0},
        {makeId(0, 10), 0},
        {makeId(0, 11), makeId(0, 10)},
        {makeId(0, 42), makeId(0, 40)},
        {makeId(0, 12), makeId(0, 10)},
        {makeId(0, 41), makeId(0, 40)},
    };
    std::vector<Node> shuffled = {
        nodes[5], nodes[2], nodes[0], nodes[4], nodes[1], nodes[3],
    };

    const auto sorted = sortForestByDepthAndId(shuffled);
    const auto expectedParent =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto expected =
        sortForestByComparisonWithParent(nodes, expectedParent);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(shuffled, expected);
    require(verifySortedByDepthAndId(sorted));
}

void test_adaptive_sort_matches_baselines_for_100k_common_depth_forest() {
    constexpr std::size_t nodeCount = 100000;

    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(verifySortedByDepthAndId(sorted));
}

void test_all_sort_methods_match_canonical_order_across_permutations() {
    constexpr std::size_t nodeCount = 10000;

    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto canonicalParent =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto canonical =
        sortForestByComparisonWithParent(nodes, canonicalParent);

    const std::vector<std::vector<Node>> permutations = {
        nodes,
        shuffledCopy(nodes, 0x12345678ULL),
        shuffledCopy(nodes, 0x87654321ULL),
    };

    for (const auto &permutation : permutations) {
        const auto adaptive = sortForestByDepthAndId(permutation);

        requireAllRegisteredSortsMatch(permutation, canonical);
        if (!sameNodes(adaptive, canonical)) {
            throw std::runtime_error(
                "adaptive radix sort changed across input permutations");
        }
        if (!verifySortedByDepthAndId(adaptive)) {
            throw std::runtime_error("sorted output failed verification");
        }
    }
}

void test_adaptive_sort_matches_baselines_with_deep_depth_outliers() {
    constexpr std::size_t nodeCount = 10000;

    const auto nodes =
        makeGeneratedForestWithOutliers(nodeCount, kCommonFixtureMaxDepth);
    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(verifySortedByDepthAndId(sorted));
}

void test_sort_rejects_duplicate_full_uint128_id() {
    const UInt128 duplicateId = makeId(7, 11);
    std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    bool rejected = false;
    try {
        (void)sortForestByDepthAndId(nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error("sort accepted a duplicate full id");
    }
}

void test_verify_rejects_duplicate_full_uint128_id() {
    const UInt128 duplicateId = makeId(7, 11);
    std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    require(!verifySortedByDepthAndId(nodes));
}

void test_sort_rejects_depth_over_one_byte_prefix_limit() {
    std::vector<Node> nodes;
    constexpr uint32_t rejectedDepth = 256;
    nodes.reserve(static_cast<std::size_t>(rejectedDepth) + 1);

    nodes.push_back(Node{makeId(0, 1), 0});
    for (uint32_t depth = 1; depth <= rejectedDepth; ++depth) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(depth) + 1ULL),
                             makeId(0, static_cast<uint64_t>(depth))});
    }

    bool rejected = false;
    try {
        (void)sortForestByDepthAndId<1>(nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "sort accepted a forest deeper than the limit");
    }
}

void test_sort_accepts_depth_1024_with_two_byte_prefix() {
    std::vector<Node> nodes;
    constexpr uint32_t acceptedDepth = 1024;
    nodes.reserve(static_cast<std::size_t>(acceptedDepth) + 1);

    nodes.push_back(Node{makeId(0, 1), 0});
    for (uint32_t depth = 1; depth <= acceptedDepth; ++depth) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(depth) + 1ULL),
                             makeId(0, static_cast<uint64_t>(depth))});
    }

    const auto sorted = sortForestByDepthAndId<2>(nodes);
    require(verifySortedByDepthAndId<2>(sorted));
}

void test_verify_accepts_sorted_common_forest() {
    constexpr std::size_t nodeCount = 10000;

    const auto sorted = sortForestByDepthAndId(
        makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth));

    require(verifySortedByDepthAndId(sorted));
}

void test_verify_rejects_unsorted_by_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},
        {makeId(0, 2), makeId(0, 1)},
    };

    std::swap(nodes[0], nodes[1]);

    require(!verifySortedByDepthAndId(nodes));
}

void test_verify_rejects_unsorted_by_id_within_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 20), 0},
        {makeId(0, 10), 0},
    };

    require(!verifySortedByDepthAndId(nodes));
}

void test_verify_rejects_child_before_existing_parent() {
    std::vector<Node> nodes = {
        {makeId(0, 2), makeId(0, 1)},
        {makeId(0, 1), 0},
    };

    require(!verifySortedByDepthAndId(nodes));
}

void test_verify_treats_missing_parent_as_root() {
    std::vector<Node> nodes = {
        {makeId(0, 1), makeId(0, 99)},
        {makeId(0, 2), 0},
    };

    require(verifySortedByDepthAndId(nodes));
}

void test_verify_rejects_depth_over_one_byte_prefix_limit() {
    std::vector<Node> nodes;
    constexpr uint32_t rejectedDepth = 256;
    nodes.reserve(static_cast<std::size_t>(rejectedDepth) + 1);

    nodes.push_back(Node{makeId(0, 1), 0});
    for (uint32_t depth = 1; depth <= rejectedDepth; ++depth) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(depth) + 1ULL),
                             makeId(0, static_cast<uint64_t>(depth))});
    }

    require(!verifySortedByDepthAndId<1>(nodes));
}

void test_fnv1a_128_hash() {
    using forest_sorting::UInt128Traits;

    const UInt128 val0 = 0;
    const UInt128 val1 = 1;
    const UInt128 val2 = 2;

    const std::size_t hash0 = UInt128Traits::hash(val0);
    const std::size_t hash1 = UInt128Traits::hash(val1);
    const std::size_t hash2 = UInt128Traits::hash(val2);

    require(hash0 != hash1, "hash collision for 0 and 1");
    require(hash1 != hash2, "hash collision for 1 and 2");
    require(hash0 != hash2, "hash collision for 0 and 2");

    // FNV-1a is deterministic
    require(hash0 == UInt128Traits::hash(val0), "hash not deterministic for 0");
    require(hash1 == UInt128Traits::hash(val1), "hash not deterministic for 1");

    // Test with high bits
    const UInt128 valHigh = static_cast<UInt128>(1) << 100;
    require(UInt128Traits::hash(valHigh) != hash0,
            "hash collision for high bit and 0");
}

#ifdef __SIZEOF_INT128__
void test_uint128_hash_matches_portable_word_hash() {
    auto check = [](uint64_t high, uint64_t low) {
        const UInt128 value =
            (static_cast<UInt128>(high) << 64U) | static_cast<UInt128>(low);
        const auto traitHash = UInt128Traits::hash(value);
        const auto portable =
            forest_sorting::detail::hashUint128Words(high, low);
        require(traitHash == portable,
                "UInt128 hash did not match portable word hash");
    };

    check(0, 0);
    check(0, 1);
    check(0xffffffffffffffffULL, 0xffffffffffffffffULL);
    check(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
    check(0x8000000000000000ULL, 0);
    check(0, 0x8000000000000000ULL);
}
#endif

void test_dense_depth2_baseline_limits() {
    using namespace forest_sorting::test_support;

    // Accepts depth 65535 (2-byte limit)
    {
        std::vector<Node> nodes;
        appendDeepChain(nodes, 65535, 0x111ULL);
        const auto parentIndex =
            buildParentIndexForKind(ParentKind::Control, nodes);
        const auto sorted =
            sortForestByDenseDepth2BucketedMsdWithParent(nodes, parentIndex);
        require(verifySortedByDepthAndId(sorted));
    }

    // Rejects depth 65536
    {
        std::vector<Node> nodes;
        appendDeepChain(nodes, 65536, 0x222ULL);
        const auto parentIndex =
            buildParentIndexForKind(ParentKind::Control, nodes);
        bool rejected = false;
        try {
            (void)sortForestByDenseDepth2BucketedMsdWithParent(nodes,
                                                               parentIndex);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(
            rejected,
            "depth-bucket-depth2 baseline unexpectedly accepted depth 65536");
    }
}

std::vector<Node> rootNodesFromIds(const std::vector<UInt128> &ids) {
    std::vector<Node> nodes;
    nodes.reserve(ids.size());
    for (UInt128 nodeId : ids) {
        nodes.push_back({nodeId, 0});
    }
    return nodes;
}

void requireChunkMsdSortsMatchComparison(const std::vector<Node> &inputNodes,
                                         std::string_view caseName) {
    const auto artifacts =
        buildParentArtifactsForKind(ParentKind::Radix, inputNodes);
    const auto expected =
        sortForestByComparisonWithParent(inputNodes, artifacts.parentIndex);

    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.category == SortCategory::Alias) {
            continue;
        }
        const auto sorted =
            sortForestForKind(entry.kind, inputNodes, artifacts.parentIndex,
                              &artifacts.idPermutation);
        require(sameNodes(sorted, expected),
                std::string(entry.name) + " failed " + std::string(caseName));
    }
}

void test_radix_msd_partition_materializes_scratch_at_max_digit() {
    std::vector<std::size_t> order = {0, 1, 2, 3};
    std::vector<std::size_t> scratch(order.size());
    const std::vector<uint16_t> keys = {0x0201U, 0x0201U, 0x0101U, 0x0101U};

    auto digitForIndex = [&](std::size_t keyIndex, std::size_t digitIndex) {
        if (digitIndex == 0) {
            return static_cast<uint8_t>(keys[keyIndex] >> 8U);
        }
        return static_cast<uint8_t>(keys[keyIndex]);
    };
    auto rangeDone = [](std::size_t, std::size_t) {};

    forest_sorting::detail::radixMsdPartitionRanges(
        order, scratch, 0, order.size(), 0, 2, digitForIndex, rangeDone);

    const std::vector<std::size_t> expected = {2, 3, 0, 1};
    require(order == expected,
            "scratch-owned max-digit completion was not materialized");
}

void test_chunk_msd_materializes_scratch_owned_small_ranges() {
    auto check = [](const std::vector<Node> &inputNodes,
                    std::string_view caseName) {
        requireChunkMsdSortsMatchComparison(inputNodes, caseName);
    };

    // Construct a dataset designed to create a small range that is owned by
    // the scratch buffer when the small range sorter is invoked.
    for (uint64_t low = 0; low < 2; ++low) {
        std::vector<UInt128> ids;
        ids.reserve(forest_sorting::detail::small_id_range_sort_threshold + 4U);
        // Make enough IDs to trigger a chunk split
        for (uint64_t i = 0;
             i < forest_sorting::detail::small_id_range_sort_threshold + 2U;
             ++i) {
            ids.push_back(makeId(0xAA00000000000000ULL, i));
        }

        // Add a cluster that will fall into a specific bucket
        ids.push_back(
            makeId(0xAA01000000000000ULL,
                   static_cast<uint64_t>(
                       forest_sorting::detail::small_id_range_sort_threshold) -
                       low));
        ids.push_back(makeId(
            0xAA01000000000000ULL,
            1000ULL +
                static_cast<uint64_t>(
                    forest_sorting::detail::small_id_range_sort_threshold -
                    low)));
        check(rootNodesFromIds(ids), "scratch-owned small ranges");
    }
}

void test_chunk_msd_handles_sorted_and_reverse_inputs() {
    std::vector<UInt128> sortedIds;
    sortedIds.reserve(96);
    for (uint64_t index = 0; index < 96; ++index) {
        sortedIds.push_back(makeId(index >> 4U, index + 1ULL));
    }

    std::vector<UInt128> reversedIds = sortedIds;
    std::reverse(reversedIds.begin(), reversedIds.end());

    requireChunkMsdSortsMatchComparison(rootNodesFromIds(sortedIds),
                                        "already sorted input");
    requireChunkMsdSortsMatchComparison(rootNodesFromIds(reversedIds),
                                        "reverse sorted input");
}

void test_chunk_msd_handles_high_prefix_collisions() {
    std::vector<UInt128> sameHighByte;
    std::vector<UInt128> sameHigh32;
    std::vector<UInt128> longPrefix;
    sameHighByte.reserve(128);
    sameHigh32.reserve(128);
    longPrefix.reserve(128);

    for (uint64_t index = 0; index < 128; ++index) {
        sameHighByte.push_back(makeId(
            0x7F00000000000000ULL | ((127ULL - index) << 48U), index + 1ULL));
        sameHigh32.push_back(makeId(
            0x1122334400000000ULL | ((127ULL - index) << 24U), index + 1ULL));
        longPrefix.push_back(makeId(0x1122334455667788ULL,
                                    0xFF00000000000000ULL | (127ULL - index)));
    }

    requireChunkMsdSortsMatchComparison(rootNodesFromIds(sameHighByte),
                                        "same high byte IDs");
    requireChunkMsdSortsMatchComparison(rootNodesFromIds(sameHigh32),
                                        "same high 32-bit IDs");
    requireChunkMsdSortsMatchComparison(rootNodesFromIds(longPrefix),
                                        "long equal-prefix IDs");
}

void test_chunk_msd_handles_small_threshold_boundaries() {
    for (std::size_t rangeSize :
         {forest_sorting::detail::small_id_range_sort_threshold - 1U,
          forest_sorting::detail::small_id_range_sort_threshold,
          forest_sorting::detail::small_id_range_sort_threshold + 1U,
          forest_sorting::detail::production_touched_count_max_range_size - 1U,
          forest_sorting::detail::production_touched_count_max_range_size,
          forest_sorting::detail::production_touched_count_max_range_size +
              1U}) {
        std::vector<UInt128> ids;
        ids.reserve(rangeSize);
        for (std::size_t index = 0; index < rangeSize; ++index) {
            ids.push_back(makeId(0xABCD000000000000ULL,
                                 static_cast<uint64_t>(rangeSize - index)));
        }

        requireChunkMsdSortsMatchComparison(rootNodesFromIds(ids),
                                            "small threshold boundary");
    }
}

void test_chunk_msd_duplicate_ids_are_rejected_before_sorting() {
    const UInt128 duplicateId = makeId(0xDEADBEEFULL, 0xCAFEBABEULL);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    bool rejected = false;
    try {
        (void)buildParentIndexForKind(ParentKind::Control, nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "duplicate IDs reached chunk MSD sorting path");
}

int main() {
    try {
        std::cout << "forest sorting tests\n";
        runTest("support registries are unique",
                test_support_registries_are_unique);
        runTest("removed generation touched-count labels do not parse",
                test_removed_generation_touched_count_labels_do_not_parse);
        runTest("bitmask touched chunk sort reuses scratch",
                test_bitmask_touched_chunk_sort_reuses_scratch);
        runTest("radix MSD materializes scratch at max digit",
                test_radix_msd_partition_materializes_scratch_at_max_digit);
        runTest("chunk MSD materializes scratch-owned small ranges",
                test_chunk_msd_materializes_scratch_owned_small_ranges);
        runTest("chunk MSD handles sorted and reverse inputs",
                test_chunk_msd_handles_sorted_and_reverse_inputs);
        runTest("chunk MSD handles high-prefix collisions",
                test_chunk_msd_handles_high_prefix_collisions);
        runTest("chunk MSD handles small threshold boundaries",
                test_chunk_msd_handles_small_threshold_boundaries);
        runTest("chunk MSD duplicate IDs are rejected before sorting",
                test_chunk_msd_duplicate_ids_are_rejected_before_sorting);
        runBenchmarkSupportTests();
        runTest("compute depths for simple parent chain",
                test_compute_depths_simple_chain);
        runTest("sort rejects self-parent cycle",
                test_sort_rejects_self_parent_cycle);
        runTest("sort rejects two-node parent cycle",
                test_sort_rejects_two_node_parent_cycle);
        runTest("sort rejects long parent cycle and tail",
                test_sort_rejects_long_parent_cycle_and_tail);
        runTest("copy and in-place sort propagate parent cycle",
                test_copy_and_in_place_sort_propagate_parent_cycle);
        runTest("verify rejects parent cycles",
                test_verify_rejects_parent_cycles);
        runTest("parent builders match for registered datasets",
                test_parent_builders_match_for_registered_datasets);
        runTest("radix parent artifacts retain sorted node permutations",
                test_radix_parent_artifacts_retain_sorted_node_permutations);
        runTest("global-ID-first sort computes or reuses ID permutation",
                test_global_id_first_sort_computes_or_reuses_id_permutation);
        runTest("parent builders reject duplicate full UInt128 ID",
                test_parent_builders_reject_duplicate_full_uint128_id);
        runTest(
            "parent builder falls back on insert probe limit",
            test_parent_builder_falls_back_when_insert_probe_limit_exceeded);
        runTest(
            "parent builder falls back on lookup probe limit",
            test_parent_builder_falls_back_when_lookup_probe_limit_exceeded);
        runTest("parent builder identity hash rejects duplicate full ID",
                test_parent_builder_identity_hash_rejects_duplicate_full_id);
        runTest("sort and verify multiple roots",
                test_sort_and_verify_multi_root);
        runTest("adaptive sort orders high64 before low64",
                test_adaptive_sort_orders_by_high64_before_low64);
        runTest("adaptive sort uses low64 when high64 matches",
                test_adaptive_sort_uses_low64_when_high64_matches);
        runTest("parent radix cached path differentiates low64",
                test_parent_radix_cached_path_differentiates_low64);
        runTest("public sort matches u32 chunk support path",
                test_public_sort_matches_u32_chunk_support_path);
        runTest("adaptive sort matches comparison for shuffled input",
                test_adaptive_sort_matches_comparison_for_shuffled_input);
        runTest(
            "adaptive sort matches baselines for 100k common-depth forest",
            test_adaptive_sort_matches_baselines_for_100k_common_depth_forest);
        runTest(
            "all sort methods match canonical order across permutations",
            test_all_sort_methods_match_canonical_order_across_permutations);
        runTest("adaptive sort matches baselines with deep depth outliers",
                test_adaptive_sort_matches_baselines_with_deep_depth_outliers);
        runTest("sort rejects duplicate full UInt128 ID",
                test_sort_rejects_duplicate_full_uint128_id);
        runTest("sort rejects depth over one-byte prefix limit",
                test_sort_rejects_depth_over_one_byte_prefix_limit);
        runTest("sort accepts depth 1024 with two-byte prefix",
                test_sort_accepts_depth_1024_with_two_byte_prefix);
        runGenericApiAndDepthTests();
        runTest("fnv1a 128 hash correctness", test_fnv1a_128_hash);
#ifdef __SIZEOF_INT128__
        runTest("uint128 hash matches portable word hash",
                test_uint128_hash_matches_portable_word_hash);
#endif
        runTest("depth-bucket-depth2 baseline limits",
                test_dense_depth2_baseline_limits);

        runTest("verify accepts sorted common forest",

                test_verify_accepts_sorted_common_forest);
        runTest("verify rejects unsorted depth order",
                test_verify_rejects_unsorted_by_depth);
        runTest("verify rejects unsorted ID order within depth",
                test_verify_rejects_unsorted_by_id_within_depth);
        runTest("verify rejects child before existing parent",
                test_verify_rejects_child_before_existing_parent);
        runTest("verify treats missing parent as root",
                test_verify_treats_missing_parent_as_root);
        runTest("verify rejects duplicate full UInt128 ID",
                test_verify_rejects_duplicate_full_uint128_id);
        runTest("verify rejects depth over one-byte prefix limit",
                test_verify_rejects_depth_over_one_byte_prefix_limit);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-tests failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tests failed: unknown exception\n";
        return 1;
    }
}
