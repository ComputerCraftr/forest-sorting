#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

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
    require(shouldUseDenseDepthGrouping(100, kCommonFixtureMaxDepth),
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
    require(!shouldUseDenseDepthGrouping(0, kCommonFixtureMaxDepth),
            "zero nodes should not prefer dense depth grouping");
    require(!shouldUseDenseDepthGrouping(0, 0xFFFFFFFFU),
            "zero nodes accepted huge dense depth grouping");

    // One node: threshold should not be a semantic requirement.
    require(!shouldUseDenseDepthGrouping(1, kCommonFixtureMaxDepth),
            "one node should reject disproportionate dense grouping");
    require(!shouldUseDenseDepthGrouping(1, 0xFFFFFFFFU),
            "one node accepted huge dense depth grouping");

    // Normal common-depth case.
    require(shouldUseDenseDepthGrouping(100, kCommonFixtureMaxDepth),
            "failed on 100 nodes, common fixture depth");
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

void runGenericApiAndDepthTests() {
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
    runTest("sort accepts depth over two-byte prefix limit with three-byte",
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
}
