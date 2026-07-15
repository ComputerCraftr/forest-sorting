#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/adaptive_sort_variants.hpp"
#include "full/parent_registry.hpp"
#include "full/sort_registry.hpp"
#include "sort_baselines.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"
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
    std::size_t defaultParentCount = 0;
    for (std::size_t kindIdx = 0; kindIdx < parentKinds.size(); ++kindIdx) {
        require(!parentName(parentKinds[kindIdx]).empty(),
                "parent registry contains an empty name");
        const ParentRegistryEntry &entry = getParentRegistry()[kindIdx];
        require(entry.kind == parentKinds[kindIdx],
                "parent registry kind projection is inconsistent");
        require(entry.build != nullptr,
                "parent registry contains an empty builder");
        defaultParentCount += entry.includeByDefault ? 1U : 0U;
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
    require(defaultParentCount == 1 && defParentKinds.size() == 1 &&
                defParentKinds.front() == ParentKind::RadixJoinIdMsdChunk32,
            "chunk32 radix join must be the sole default parent builder");

    validateSortRegistry();

    const auto &registry = getSortRegistry();
    const auto globalIdFirstIt =
        std::ranges::find_if(registry, [](const SortRegistryEntry &entry) {
            return entry.kind == SortKind::GlobalIdPermutationThenDepthStable;
        });
    require(globalIdFirstIt != registry.end(),
            "global-ID-first sort is missing from the registry");
    const auto &globalIdFirst = *globalIdFirstIt;
    require(globalIdFirst.category == SortCategory::Production &&
                globalIdFirst.includeByDefault,
            "global-ID-first sorting must be the production benchmark row");
    require(globalIdFirst.sortFunction == nullptr &&
                globalIdFirst.optionalIdPermutationSortFunction ==
                    sortForestByTrustedGlobalIdPermutationThenDepthStable,
            "global-ID-first registry row is wired to the wrong pipeline");
    const auto depthFirstIt =
        std::ranges::find_if(registry, [](const SortRegistryEntry &entry) {
            return entry.kind ==
                   SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512;
        });
    require(depthFirstIt != registry.end(),
            "depth-first comparator is missing from the registry");
    const auto &depthFirst = *depthFirstIt;
    require(depthFirst.category == SortCategory::Comparator,
            "depth-first per-range ID sorting must remain a comparator");
    require(
        depthFirst.sortFunction ==
                sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512TailLinear32WithParent &&
            depthFirst.optionalIdPermutationSortFunction == nullptr,
        "depth-first comparator is wired to the wrong pipeline");
}

void requireSortIsExplicitOptIn(SortKind sortKind, std::string_view message) {
    const auto defaultSorts = defaultSortKinds();
    require(std::ranges::find(defaultSorts, sortKind) == defaultSorts.end(),
            message);
}

void requireAllRegisteredSortsMatch(const std::vector<Node> &nodes,
                                    const std::vector<Node> &expected) {
    const auto artifacts =
        buildParentArtifactsForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);
    for (const SortRegistryEntry &entry : getSortRegistry()) {
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

void test_removed_benchmark_labels_do_not_parse() {
    constexpr std::string_view kRemovedLabelRepresentatives[] = {
        "dense-depth2-buckets-then-id-msd",
        "composite-depth2-id-msd-copyback",
        "composite-depth2-id-byte-msd-copyback",
        "depth2-first-then-id-u8-msd-full-clear",
        "depth2-first-then-id-u16-msd-bitmask-le512",
        "depth2-first-then-id-u32-msd-bitmask-le512",
        "depth2-first-then-id-u64-msd-full-clear",
        "global-id-u32-msd-radix-then-depth-stable",
        "global-id-msd-chunk32-radix-then-depth-stable",
        "depth2-first-then-id-range-ladder-u8-le1024-u16-le16384-full-clear",
        "composite-depth2-id-byte-msd-lowcopy-branchy",
        "radix-join-id-byte-msd"};

    for (std::string_view removedLabel : kRemovedLabelRepresentatives) {
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
    std::vector<forest_sorting::detail::IdMsdChunkEntry<1>> current(
        order.size());
    std::vector<forest_sorting::detail::IdMsdChunkEntry<1>> next(order.size());
    forest_sorting::detail::BitmaskTouchedCountScratch scratch;
    const UInt128NodeTraits traits;
    auto idForIndex = [&](std::size_t nodeIndex) {
        return UInt128NodeTraits::id(nodes[nodeIndex]);
    };

    forest_sorting::detail::stableLsdSortIndexRangeByIdMsdChunkWithCounter(
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
    forest_sorting::detail::stableLsdSortIndexRangeByIdMsdChunkWithCounter(
        order, idForIndex, traits, 0, order.size(), 0, current.data(),
        next.data(), scratch);
    require((order == std::vector<std::size_t>{1, 2, 0, 3}),
            "second bitmask touched chunk sort reused stale counts");
}

void test_compute_depths_simple_chain() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},            // depth 0
        {makeId(0, 2), makeId(0, 1)}, // depth 1
        {makeId(0, 3), makeId(0, 2)}, // depth 2
    };

    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
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
        buildParentArtifactsForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);

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

void test_production_sort_orders_by_high64_before_low64() {
    std::vector<Node> nodes = {
        {makeId(2, 0), 0},
        {makeId(1, UINT64_MAX), 0},
        {makeId(1, 0), 0},
        {makeId(0, UINT64_MAX), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(sorted[0].id == makeId(0, UINT64_MAX));
    require(sorted[1].id == makeId(1, 0));
    require(sorted[2].id == makeId(1, UINT64_MAX));
    require(sorted[3].id == makeId(2, 0));
}

void test_production_sort_uses_low64_when_high64_matches() {
    std::vector<Node> nodes = {
        {makeId(9, 3), 0},
        {makeId(8, UINT64_MAX), 0},
        {makeId(9, 1), 0},
        {makeId(9, 2), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(verifySortedByDepthAndId(sorted));
    require(sorted[0].id == makeId(8, UINT64_MAX));
    require(sorted[1].id == makeId(9, 1));
    require(sorted[2].id == makeId(9, 2));
    require(sorted[3].id == makeId(9, 3));
}

void test_public_sort_matches_retained_permutation_production_path() {
    constexpr std::size_t nodeCount = 10000;
    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto artifacts =
        buildParentArtifactsForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);

    const auto productionSorted = sortForestByDepthAndId(nodes);
    const auto retainedPermutationSorted =
        sortForestForKind(SortKind::GlobalIdPermutationThenDepthStable, nodes,
                          artifacts.parentIndex, &artifacts.idPermutation);

    require(sameNodes(productionSorted, retainedPermutationSorted),
            "public sort did not match radix-parent retained-permutation "
            "production path");
    require(verifySortedByDepthAndId(productionSorted));
}

void test_production_sort_matches_comparison_for_shuffled_input() {
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
        buildParentIndexForKind(ParentKind::Unordered, nodes);
    const auto expected =
        sortForestByComparisonWithParent(nodes, expectedParent);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(shuffled, expected);
    require(verifySortedByDepthAndId(sorted));
}

void test_production_sort_and_registered_rows_match_100k_common_depth_forest() {
    constexpr std::size_t nodeCount = 100000;

    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
    const auto expected = sortForestByComparisonWithParent(nodes, parentIndex);

    require(sameNodes(sorted, expected));
    requireAllRegisteredSortsMatch(nodes, expected);
    require(verifySortedByDepthAndId(sorted));
}

void test_registered_sort_rows_match_canonical_order_across_permutations() {
    constexpr std::size_t nodeCount = 10000;

    const auto nodes = makeGeneratedForest(nodeCount, kCommonFixtureMaxDepth);
    const auto canonicalParent =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
    const auto canonical =
        sortForestByComparisonWithParent(nodes, canonicalParent);

    const std::vector<std::vector<Node>> permutations = {
        nodes,
        shuffledCopy(nodes, 0x12345678ULL),
        shuffledCopy(nodes, 0x87654321ULL),
    };

    for (const auto &permutation : permutations) {
        const auto productionSorted = sortForestByDepthAndId(permutation);

        requireAllRegisteredSortsMatch(permutation, canonical);
        if (!sameNodes(productionSorted, canonical)) {
            throw std::runtime_error(
                "production sort changed across input permutations");
        }
        if (!verifySortedByDepthAndId(productionSorted)) {
            throw std::runtime_error("sorted output failed verification");
        }
    }
}

void test_production_sort_and_registered_rows_match_deep_depth_outliers() {
    constexpr std::size_t nodeCount = 10000;

    const auto nodes =
        makeGeneratedForestWithOutliers(nodeCount, kCommonFixtureMaxDepth);
    const auto sorted = sortForestByDepthAndId(nodes);
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Unordered, nodes);
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

void test_dense_depth2_baseline_limits() {
    using namespace forest_sorting::test_support;

    // Accepts depth 65535 (2-byte limit)
    {
        std::vector<Node> nodes;
        appendDeepChain(nodes, 65535, 0x111ULL);
        const auto parentIndex =
            buildParentIndexForKind(ParentKind::Unordered, nodes);
        const auto sorted =
            sortForestByDenseDepth2BucketsThenIdMsdChunk64FullClearWithParent(
                nodes, parentIndex);
        require(verifySortedByDepthAndId(sorted));
    }

    // Rejects depth 65536
    {
        std::vector<Node> nodes;
        appendDeepChain(nodes, 65536, 0x222ULL);
        const auto parentIndex =
            buildParentIndexForKind(ParentKind::Unordered, nodes);
        bool rejected = false;
        try {
            (void)
                sortForestByDenseDepth2BucketsThenIdMsdChunk64FullClearWithParent(
                    nodes, parentIndex);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "dense-depth2-buckets-then-id baseline unexpectedly "
                          "accepted depth 65536");
    }
}

int main() {
    try {
        std::cout << "forest sorting tests\n";
        runTest("support registries are unique",
                test_support_registries_are_unique);
        runTest("removed benchmark labels do not parse",
                test_removed_benchmark_labels_do_not_parse);
        runTest("bitmask touched chunk sort reuses scratch",
                test_bitmask_touched_chunk_sort_reuses_scratch);
        runRadixPathologicalTests();
        runRadixLadderTests();
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
        runParentIndexTests();
        runTest("sort and verify multiple roots",
                test_sort_and_verify_multi_root);
        runTest("production sort orders high64 before low64",
                test_production_sort_orders_by_high64_before_low64);
        runTest("production sort uses low64 when high64 matches",
                test_production_sort_uses_low64_when_high64_matches);
        runTest("parent radix cached path differentiates low64",
                test_parent_radix_cached_path_differentiates_low64);
        runTest("public sort matches retained-permutation production path",
                test_public_sort_matches_retained_permutation_production_path);
        runTest("production sort matches comparison for shuffled input",
                test_production_sort_matches_comparison_for_shuffled_input);
        runTest(
            "production sort and registered rows match 100k common-depth "
            "forest",
            test_production_sort_and_registered_rows_match_100k_common_depth_forest);
        runTest(
            "registered sort rows match canonical order across "
            "permutations",
            test_registered_sort_rows_match_canonical_order_across_permutations);
        runTest(
            "production sort and registered rows match deep depth outliers",
            test_production_sort_and_registered_rows_match_deep_depth_outliers);
        runTest("sort rejects duplicate full UInt128 ID",
                test_sort_rejects_duplicate_full_uint128_id);
        runTest("sort rejects depth over one-byte prefix limit",
                test_sort_rejects_depth_over_one_byte_prefix_limit);
        runTest("sort accepts depth 1024 with two-byte prefix",
                test_sort_accepts_depth_1024_with_two_byte_prefix);
        runGenericApiAndDepthTests();
        runGenericIdParentTests();
        runGenericIdRadixTests();
        runTailMicrobenchmarkTests();

        runTest("dense-depth2-buckets-then-id baseline limits",
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
