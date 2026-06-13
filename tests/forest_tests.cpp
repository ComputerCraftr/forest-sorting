#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/hash.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "parent_index_baselines.hpp"
#include "sort_baselines.hpp"
#include "sort_registry.hpp"
#include "test_bytes.hpp"
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
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortedCopyByDepthAndId;
using forest_sorting::sortedOrderByDepthAndId;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::UInt128Traits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

inline void require(bool condition,
                    std::string_view message = "test condition failed") {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void requireSortedByDepthThenId(const std::vector<std::size_t> &order,
                                const std::vector<Node> &nodes,
                                const std::vector<uint32_t> &depths) {
    for (std::size_t nodeIdx = 1; nodeIdx < order.size(); ++nodeIdx) {
        const uint32_t depth0 = depths[order[nodeIdx - 1]];
        const uint32_t depth1 = depths[order[nodeIdx]];
        if (depth0 != depth1) {
            require(depth0 < depth1, "depths not in ascending order");
        } else {
            require(nodes[order[nodeIdx - 1]].id < nodes[order[nodeIdx]].id,
                    "ids not in ascending order for same depth");
        }
    }
}

using forest_sorting::detail::buildParentIndex;
using forest_sorting::detail::buildParentIndexRadixJoin;

void runTest(const char *testName, void (*testFunction)()) {
    std::cout << "RUN  " << testName << "\n";
    testFunction();
    std::cout << "PASS " << testName << "\n";
}

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

    const auto parentKinds = allParentKinds();
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

    validateSortRegistry();
}

void assertParentBuildersMatch(const std::vector<Node> &nodes) {
    const auto expected = buildParentIndexForKind(ParentKind::Unordered, nodes);
    for (ParentKind parentKind : allParentKinds()) {
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
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::Control, nodes);
    for (const SortRegistryEntry &entry : kSortRegistry) {
        const auto sorted = entry.sortFunction(nodes, parentIndex);
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
}

void test_parent_builders_match_for_registered_datasets() {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        assertParentBuildersMatch(
            makeGeneratedForestForKind(datasetKind, 10000));
    }
}

void test_parent_builders_reject_duplicate_full_uint128_id() {
    const UInt128 duplicateId = makeId(7, 11);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    for (ParentKind parentKind : allParentKinds()) {
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
    constexpr uint32_t commonMaxDepth = 30;

    const auto nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
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
    constexpr uint32_t commonMaxDepth = 30;

    const auto nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
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
    constexpr uint32_t commonMaxDepth = 30;

    const auto nodes =
        makeGeneratedForestWithOutliers(nodeCount, commonMaxDepth);
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
    constexpr uint32_t commonMaxDepth = 30;

    const auto sorted =
        sortForestByDepthAndId(makeGeneratedForest(nodeCount, commonMaxDepth));

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

template <std::size_t ByteCount>
void assert_generic_fixed_hash_api_orders_by_depth_then_id() {
    using Id = TestBytes<ByteCount>;
    using Node = TestNode<ByteCount>;
    using Traits = TestBytesTraits<ByteCount>;

    const Id root = makeTestBytes<ByteCount>(0, 10);
    const Id childLow = makeTestBytes<ByteCount>(0, 20);
    const Id childHigh = makeTestBytes<ByteCount>(1, 1);
    const Id sibling = makeTestBytes<ByteCount>(0, 5);

    std::vector<Node> nodes = {
        {childHigh, root},
        {sibling, Id{}},
        {childLow, root},
        {root, Id{}},
    };

    const auto explicitOrder =
        forest_sorting::sortedOrderByDepthAndId<1>(nodes, Traits{});
    const auto order = forest_sorting::sortedOrderByDepthAndId(nodes, Traits{});

    require(order == explicitOrder);

    require(order.size() == nodes.size());
    require(order[0] == 1);
    require(order[1] == 3);
    require(order[2] == 2);
    require(order[3] == 0);

    const auto sorted = forest_sorting::sortedCopyByDepthAndId(nodes, Traits{});
    require(sorted[0].id == sibling);
    require(sorted[1].id == root);
    require(sorted[2].id == childLow);
    require(sorted[3].id == childHigh);

    auto inPlace = nodes;
    forest_sorting::sortInPlaceByDepthAndId(inPlace, Traits{});
    require(inPlace[0].id == sibling);
    require(inPlace[1].id == root);
    require(inPlace[2].id == childLow);
    require(inPlace[3].id == childHigh);

    require(forest_sorting::verifySortedByDepthAndId(sorted, Traits{}));
    require(!forest_sorting::verifySortedByDepthAndId(nodes, Traits{}));
}

void test_generic_16_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<16>();
}

void test_generic_20_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<20>();
}

void test_generic_28_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<28>();
}

void test_generic_32_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<32>();
}

void test_generic_64_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<64>();
}

void test_generic_37_byte_public_api_forms() {
    assert_generic_fixed_hash_api_orders_by_depth_then_id<37>();
}

void test_sort_accepts_depth_over_two_byte_prefix_limit_with_three_byte() {
    std::vector<Node> nodes;
    constexpr uint32_t depthLimit = 65536;
    nodes.reserve(depthLimit + 1);

    appendDeepChain(nodes, depthLimit, 0x5555ULL);

    const auto dynamicSorted = sortForestByDepthAndId(nodes);
    require(dynamicSorted.size() == static_cast<std::size_t>(depthLimit) + 1);
    require(verifySortedByDepthAndId(dynamicSorted));

    const auto sorted = sortForestByDepthAndId<3>(nodes);
    require(sorted.size() == static_cast<std::size_t>(depthLimit) + 1);
    require(verifySortedByDepthAndId<3>(sorted));

    bool rejected = false;
    try {
        (void)sortForestByDepthAndId<2>(nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "two-byte prefix accepted depth 65536");
}

void test_precomputed_depth_api_validates_inputs() {
    const std::vector<Node> nodes = {
        {makeId(0, 1), 0},
        {makeId(0, 2), 0},
    };

    bool rejectedSize = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
            nodes, UInt128NodeTraits{}, std::vector<uint32_t>{0});
    } catch (const std::runtime_error &) {
        rejectedSize = true;
    }
    require(rejectedSize, "precomputed depth API accepted wrong depth count");

    bool rejectedDepth = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths<1>(
            nodes, UInt128NodeTraits{}, std::vector<uint32_t>{0, 256});
    } catch (const std::runtime_error &) {
        rejectedDepth = true;
    }
    require(rejectedDepth, "precomputed depth API accepted prefix overflow");

    bool rejectedSingletonDepth = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths<1>(
            std::vector<Node>{{makeId(0, 1), 0}}, UInt128NodeTraits{},
            std::vector<uint32_t>{256});
    } catch (const std::runtime_error &) {
        rejectedSingletonDepth = true;
    }
    require(rejectedSingletonDepth,
            "precomputed depth API accepted singleton prefix overflow");
}

void test_sort_accepts_singleton_with_four_byte_prefix() {
    std::vector<Node> nodes;
    nodes.push_back(Node{makeId(0, 1), 0});
    // A singleton at depth 0 is valid under the 4-byte prefix configuration.
    const auto sorted = sortForestByDepthAndId<4>(nodes);
    require(verifySortedByDepthAndId<4>(sorted));
}

void test_sort_accepts_sparse_huge_depth_outlier() {
    using forest_sorting::detail::shouldUseDenseDepthGrouping;

    // Test threshold logic
    require(shouldUseDenseDepthGrouping(100, 30),
            "dense threshold rejected normal depth range");
    require(!shouldUseDenseDepthGrouping(100, 0xFFFFFFFFU),
            "dense threshold accepted UINT32_MAX sparse range");

    std::vector<Node> nodes = {
        {makeId(0, 9), 0}, // Index 0, Depth 0x01000000 (after manual patch)
        {makeId(0, 2), 0}, // Index 1, Depth 0x01000000
        {makeId(0, 5), 0}, // Index 2, Depth 0x01000000
        {makeId(0, 1), 0}, // Index 3, Depth 0
        {makeId(0, 3), 0}, // Index 4, Depth 0x01000001
        {makeId(0, 4), 0}, // Index 5, Depth 0x01000100
        {makeId(0, 6), 0}, // Index 6, Depth 0x01010000
        {makeId(0, 7), 0}, // Index 7, Depth 0x01FFFFFF
        {makeId(0, 8), 0}, // Index 8, Depth 0x80000000
        {makeId(0, 0), 0}  // Index 9, Depth 0xFFFFFFFF
    };

    std::vector<uint32_t> depths = {
        0x01000000, 0x01000000, 0x01000000, 0,          0x01000001,
        0x01000100, 0x01010000, 0x01FFFFFF, 0x80000000, 0xFFFFFFFF};

    // This should trigger sparse-depth MSD grouping because observed max depth
    // is large.
    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);

    // Expected order:
    // 0: depth 0,          id 1 (Input index 3)
    // 1: depth 0x01000000, id 2 (Input index 1)
    // 2: depth 0x01000000, id 5 (Input index 2)
    // 3: depth 0x01000000, id 9 (Input index 0)
    // 4: depth 0x01000001, id 3 (Input index 4)
    // 5: depth 0x01000100, id 4 (Input index 5)
    // 6: depth 0x01010000, id 6 (Input index 6)
    // 7: depth 0x01FFFFFF, id 7 (Input index 7)
    // 8: depth 0x80000000, id 8 (Input index 8)
    // 9: depth 0xFFFFFFFF, id 0 (Input index 9)

    require(order[0] == 3, "unexpected sparse depth order at index 0");
    require(order[1] == 1, "unexpected sparse depth order at index 1");
    require(order[2] == 2, "unexpected sparse depth order at index 2");
    require(order[3] == 0, "unexpected sparse depth order at index 3");
    require(order[4] == 4, "unexpected sparse depth order at index 4");
    require(order[5] == 5, "unexpected sparse depth order at index 5");
    require(order[6] == 6, "unexpected sparse depth order at index 6");
    require(order[7] == 7, "unexpected sparse depth order at index 7");
    require(order[8] == 8, "unexpected sparse depth order at index 8");
    require(order[9] == 9, "unexpected sparse depth order at index 9");

    requireSortedByDepthThenId(order, nodes, depths);
}

void test_dense_threshold_boundaries() {
    using forest_sorting::detail::shouldUseDenseDepthGrouping;

    const std::size_t maxDense = std::size_t{1} << 20;

    // Zero nodes: threshold should not select dense work.
    require(!shouldUseDenseDepthGrouping(0, 30),
            "zero nodes should not prefer dense depth grouping");
    require(!shouldUseDenseDepthGrouping(0, 0xFFFFFFFFU),
            "zero nodes accepted huge dense depth grouping");

    // One node: threshold should not be a semantic requirement.
    require(!shouldUseDenseDepthGrouping(1, 30),
            "one node should reject disproportionate dense grouping");
    require(!shouldUseDenseDepthGrouping(1, 0xFFFFFFFFU),
            "one node accepted huge dense depth grouping");

    // Normal case (100 nodes, 30 depth)
    require(shouldUseDenseDepthGrouping(100, 30),
            "failed on 100 nodes, 30 depth");
    require(!shouldUseDenseDepthGrouping(100, 0xFFFFFFFFU),
            "failed on 100 nodes, huge depth");

    // Exact hard cap boundary
    require(shouldUseDenseDepthGrouping(maxDense,
                                        static_cast<uint32_t>(maxDense - 2)),
            "failed at hard cap boundary");
    require(!shouldUseDenseDepthGrouping(maxDense,
                                         static_cast<uint32_t>(maxDense - 1)),
            "failed just over hard cap boundary");

    // Proportionality boundary (multiplier 4)
    require(shouldUseDenseDepthGrouping(100, 398),
            "failed on proportional dense boundary");
    require(!shouldUseDenseDepthGrouping(100, 399),
            "failed on non-proportional sparse boundary");

    // Proportionality bypass when node count is high enough
    require(shouldUseDenseDepthGrouping(maxDense,
                                        static_cast<uint32_t>(maxDense - 2)),
            "failed on large node count proportionality bypass");
}

void test_precomputed_depth_api_accepts_empty_input() {
    std::vector<Node> nodes;
    std::vector<uint32_t> depths;
    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);
    require(order.empty(), "empty precomputed-depth order changed");
}

void test_precomputed_depth_api_accepts_singleton_uint32_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},
    };
    std::vector<uint32_t> depths = {0xFFFFFFFFU};
    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);
    require(order.size() == 1, "singleton order size changed");
    require(order[0] == 0, "singleton order changed");
}

void test_sort_accepts_shared_prefix_huge_depths() {
    std::vector<Node> nodes = {
        {makeId(0, 3), 0}, // Index 0, Depth 0x01020303
        {makeId(0, 1), 0}, // Index 1, Depth 0x01020301
        {makeId(0, 0), 0}, // Index 2, Depth 0x01020300
        {makeId(0, 2), 0}  // Index 3, Depth 0x01020302
    };

    std::vector<uint32_t> depths = {0x01020303, 0x01020301, 0x01020300,
                                    0x01020302};

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);

    require(order[0] == 2, "shared prefix failed at index 0");
    require(order[1] == 1, "shared prefix failed at index 1");
    require(order[2] == 3, "shared prefix failed at index 2");
    require(order[3] == 0, "shared prefix failed at index 3");

    requireSortedByDepthThenId(order, nodes, depths);
}

void test_sort_accepts_all_equal_huge_depths() {
    std::vector<Node> nodes = {{makeId(0, 9), 0},
                               {makeId(0, 1), 0},
                               {makeId(0, 5), 0},
                               {makeId(0, 2), 0}};

    std::vector<uint32_t> depths(nodes.size(), 0xFFFFFFFFU);

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);

    require(nodes[order[0]].id == makeId(0, 1),
            "equal huge depths failed at index 0");
    require(nodes[order[1]].id == makeId(0, 2),
            "equal huge depths failed at index 1");
    require(nodes[order[2]].id == makeId(0, 5),
            "equal huge depths failed at index 2");
    require(nodes[order[3]].id == makeId(0, 9),
            "equal huge depths failed at index 3");

    requireSortedByDepthThenId(order, nodes, depths);
}

void test_sort_accepts_many_sparse_singletons() {
    std::vector<Node> nodes;
    std::vector<uint32_t> depths;
    for (uint32_t nodeIdx = 0; nodeIdx < 100; ++nodeIdx) {
        nodes.push_back({makeId(0, 100 - nodeIdx), 0});
        depths.push_back(nodeIdx * 0x01000000U);
    }

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<4>(
        nodes, UInt128NodeTraits{}, depths);

    requireSortedByDepthThenId(order, nodes, depths);
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

int main() {
    try {
        std::cout << "forest sorting tests\n";
        runTest("support registries are unique",
                test_support_registries_are_unique);
        runTest("compute depths for simple parent chain",
                test_compute_depths_simple_chain);
        runTest("parent builders match for registered datasets",
                test_parent_builders_match_for_registered_datasets);
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
        runTest(
            "sort accepts depth over two-byte prefix limit with three-byte",
            test_sort_accepts_depth_over_two_byte_prefix_limit_with_three_byte);
        runTest("precomputed depth API validates inputs",
                test_precomputed_depth_api_validates_inputs);
        runTest("sort accepts singleton with four-byte prefix",
                test_sort_accepts_singleton_with_four_byte_prefix);
        runTest("sort accepts sparse huge depth outlier",
                test_sort_accepts_sparse_huge_depth_outlier);
        runTest("dense threshold boundaries", test_dense_threshold_boundaries);
        runTest("precomputed depth API accepts empty input",
                test_precomputed_depth_api_accepts_empty_input);
        runTest("precomputed depth API accepts singleton uint32 depth",
                test_precomputed_depth_api_accepts_singleton_uint32_depth);
        runTest("sort accepts shared prefix huge depths",
                test_sort_accepts_shared_prefix_huge_depths);
        runTest("sort accepts all equal huge depths",
                test_sort_accepts_all_equal_huge_depths);
        runTest("sort accepts many sparse singletons",
                test_sort_accepts_many_sparse_singletons);
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
        runTest("generic 16-byte public API forms",
                test_generic_16_byte_public_api_forms);
        runTest("generic 20-byte public API forms",
                test_generic_20_byte_public_api_forms);
        runTest("generic 28-byte public API forms",
                test_generic_28_byte_public_api_forms);
        runTest("generic 32-byte public API forms",
                test_generic_32_byte_public_api_forms);
        runTest("generic 64-byte public API forms",
                test_generic_64_byte_public_api_forms);
        runTest("generic 37-byte public API forms",
                test_generic_37_byte_public_api_forms);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-tests failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tests failed: unknown exception\n";
        return 1;
    }
}
