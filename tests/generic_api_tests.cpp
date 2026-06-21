#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/hash.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

template <typename Nodes, typename Traits>
concept PublicPrecomputedDepthApiAcceptsObservedMaximum =
    requires(const Nodes &nodes, const Traits &traits,
             const std::vector<uint32_t> &depths, uint32_t observedMaxDepth) {
        forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
            nodes, traits, depths, observedMaxDepth);
    };

static_assert(!PublicPrecomputedDepthApiAcceptsObservedMaximum<
              std::vector<Node>, UInt128NodeTraits>);

template <std::size_t DepthPrefixBytes, typename Depth>
concept PublicPrecomputedDepthApiAccepts =
    requires(const std::vector<Node> &nodes, const UInt128NodeTraits &traits,
             const std::vector<Depth> &depths) {
        forest_sorting::sortedOrderByDepthAndIdWithDepths<DepthPrefixBytes>(
            nodes, traits, depths);
    };

static_assert(std::same_as<forest_sorting::detail::DepthValue<1>, uint8_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<2>, uint16_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<3>, uint32_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<4>, uint32_t>);
static_assert(PublicPrecomputedDepthApiAccepts<2, uint16_t>);
static_assert(PublicPrecomputedDepthApiAccepts<2, uint32_t>);
static_assert(!PublicPrecomputedDepthApiAccepts<2, uint8_t>);

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

    forest_sorting::detail::IdChunkSortWorkspace<
        forest_sorting::detail::production_id_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>
        workspace;
    auto idForIndex = [&](std::size_t index) { return ids[index]; };
    forest_sorting::detail::sortIndexRangeByIdChunks<
        forest_sorting::detail::production_id_chunk_bytes,
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

#define X(width)                                                               \
    void test_generic_##width##_byte_public_api_forms() {                      \
        assert_generic_fixed_hash_api_orders_by_depth_then_id<(width)>();      \
    }
FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X

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

void test_dynamic_and_explicit_prefix_orders_match() {
    const std::vector<Node> nodes = {
        {makeId(0, 4), makeId(0, 2)},
        {makeId(0, 3), 0},
        {makeId(0, 2), 0},
        {makeId(0, 1), makeId(0, 2)},
    };

    const auto dynamicOrder =
        forest_sorting::sortedOrderByDepthAndId(nodes, UInt128NodeTraits{});
    const auto explicitOrder =
        forest_sorting::sortedOrderByDepthAndId<1>(nodes, UInt128NodeTraits{});
    require(dynamicOrder == explicitOrder,
            "dynamic and explicit prefix orders differ");
}

void test_precomputed_depth_payload_widths_match() {
    const std::vector<Node> nodes = {
        {makeId(0, 3), 0},
        {makeId(0, 1), 0},
        {makeId(0, 2), 0},
    };
    const std::vector<uint16_t> narrowDepths = {1, 0, 1};
    const std::vector<uint32_t> wideDepths = {1, 0, 1};

    const auto narrowOrder =
        forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
            nodes, UInt128NodeTraits{}, narrowDepths);
    const auto wideOrder = forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
        nodes, UInt128NodeTraits{}, wideDepths);
    require(narrowOrder == wideOrder,
            "narrow and wide precomputed depths produced different orders");
}

void test_observed_depth_prefix_boundaries() {
    using forest_sorting::detail::findObservedMaxDepth;
    using forest_sorting::detail::maxDepthForPrefix;

    require(findObservedMaxDepth(std::vector<uint32_t>{}) == 0,
            "empty depths produced a nonzero maximum");
    require(findObservedMaxDepth(std::vector<uint32_t>{0xFFU}) <=
            maxDepthForPrefix<1>());
    require(findObservedMaxDepth(std::vector<uint32_t>{0x100U}) >
            maxDepthForPrefix<1>());
    require(findObservedMaxDepth(std::vector<uint32_t>{0x10000U}) >
            maxDepthForPrefix<2>());
    require(findObservedMaxDepth(std::vector<uint32_t>{0x1000000U}) >
            maxDepthForPrefix<3>());
    require(findObservedMaxDepth(std::vector<uint32_t>{UINT32_MAX}) <=
            maxDepthForPrefix<4>());
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

    // A valid singleton has structural depth zero.
    require(shouldUseDenseDepthGrouping(1, 0),
            "singleton structural depth rejected dense grouping");
    require(!shouldUseDenseDepthGrouping(1, 1),
            "singleton accepted structurally impossible depth");
    require(!shouldUseDenseDepthGrouping(1, 0xFFFFFFFFU),
            "one node accepted huge dense depth grouping");

    // Normal common-depth case.
    require(shouldUseDenseDepthGrouping(100, kCommonFixtureMaxDepth),
            "failed on 100 nodes, common fixture depth");
    require(!shouldUseDenseDepthGrouping(100, 0xFFFFFFFFU),
            "failed on 100 nodes, huge depth");

    // Exact hard resource-cap boundary.
    require(shouldUseDenseDepthGrouping(maxDense,
                                        static_cast<uint32_t>(maxDense - 2)),
            "failed at hard cap boundary");
    require(!shouldUseDenseDepthGrouping(maxDense,
                                         static_cast<uint32_t>(maxDense - 1)),
            "failed just over hard cap boundary");

    // Structural forest-depth boundary.
    require(shouldUseDenseDepthGrouping(100, 99),
            "maximum structural depth rejected dense grouping");
    require(!shouldUseDenseDepthGrouping(100, 100),
            "structurally impossible depth accepted dense grouping");

    // A structurally valid depth can still exceed the histogram resource cap.
    require(!shouldUseDenseDepthGrouping(maxDense + 1,
                                         static_cast<uint32_t>(maxDense - 1)),
            "depth above histogram cap accepted dense grouping");
}

void test_empty_forest_handling() {
    std::vector<Node> emptyNodes;
    UInt128NodeTraits traits;

    // sortedOrderByDepthAndId(empty) returns empty
    {
        const auto order1 =
            forest_sorting::sortedOrderByDepthAndId(emptyNodes, traits);
        require(order1.empty(),
                "sortedOrderByDepthAndId(empty) did not return empty");

        const auto order2 =
            forest_sorting::sortedOrderByDepthAndId<2>(emptyNodes, traits);
        require(order2.empty(),
                "sortedOrderByDepthAndId<2>(empty) did not return empty");
    }

    // sortedOrderByDepthAndIdWithDepths(empty, emptyDepths) returns empty
    {
        std::vector<uint32_t> emptyDepths;
        const auto order1 =
            forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
                emptyNodes, traits, emptyDepths);
        require(order1.empty(), "sortedOrderByDepthAndIdWithDepths<2>(empty, "
                                "emptyDepths) did not return empty");
    }

    // sortedOrderByDepthAndIdWithDepths(empty, nonemptyDepths) throws size
    // mismatch
    {
        std::vector<uint32_t> nonemptyDepths = {1, 2};
        bool threwMismatch = false;
        try {
            (void)forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
                emptyNodes, traits, nonemptyDepths);
        } catch (const std::runtime_error &) {
            threwMismatch = true;
        }
        require(threwMismatch, "sortedOrderByDepthAndIdWithDepths<2>(empty, "
                               "nonemptyDepths) did not throw size mismatch");
    }

    // sortedCopyByDepthAndId(empty) returns empty
    {
        const auto copy1 =
            forest_sorting::sortedCopyByDepthAndId(emptyNodes, traits);
        require(copy1.empty(),
                "sortedCopyByDepthAndId(empty) did not return empty");

        const auto copy2 =
            forest_sorting::sortedCopyByDepthAndId<2>(emptyNodes, traits);
        require(copy2.empty(),
                "sortedCopyByDepthAndId<2>(empty) did not return empty");
    }

    // sortForestByDepthAndId(empty) returns empty
    {
        const auto forestCopy1 =
            forest_sorting::sortForestByDepthAndId(emptyNodes);
        require(forestCopy1.empty(),
                "sortForestByDepthAndId(empty) did not return empty");
        const auto forestCopy2 =
            forest_sorting::sortForestByDepthAndId<2>(emptyNodes);
        require(forestCopy2.empty(),
                "sortForestByDepthAndId<2>(empty) did not return empty");
    }

    // sortInPlaceByDepthAndId(empty) does not crash
    {
        auto nodesToSort1 = emptyNodes;
        forest_sorting::sortInPlaceByDepthAndId(nodesToSort1, traits);
        require(nodesToSort1.empty(),
                "sortInPlaceByDepthAndId(empty) modified elements");

        auto nodesToSort2 = emptyNodes;
        forest_sorting::sortInPlaceByDepthAndId<2>(nodesToSort2, traits);
        require(nodesToSort2.empty(),
                "sortInPlaceByDepthAndId<2>(empty) modified elements");
    }

    // verifySortedByDepthAndId(empty) returns true
    {
        require(forest_sorting::verifySortedByDepthAndId(emptyNodes, traits),
                "verifySortedByDepthAndId(empty) did not return true");
        require(forest_sorting::verifySortedByDepthAndId<2>(emptyNodes, traits),
                "verifySortedByDepthAndId<2>(empty) did not return true");
    }
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

template <std::size_t ByteCount> struct CustomMockId {
    std::array<uint8_t, ByteCount> bytes{};
};

template <std::size_t ByteCount> struct CustomMockNode {
    CustomMockId<ByteCount> id;
    CustomMockId<ByteCount> parentId;
};

template <std::size_t ByteCount> struct CustomMockTraits {
    using Id = CustomMockId<ByteCount>;
    static constexpr std::size_t id_byte_count = ByteCount;

    Id id(const CustomMockNode<ByteCount> &node) const noexcept {
        return node.id;
    }
    Id parent_id(const CustomMockNode<ByteCount> &node) const noexcept {
        return node.parentId;
    }
    std::size_t hash(const Id &nodeId) const noexcept {
        return forest_sorting::detail::fold_fnv1a128(
            forest_sorting::detail::fnv1a128_hash_bytes(nodeId.bytes.data(),
                                                        ByteCount));
    }
    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        return nodeId.bytes[byteIndex];
    }
};

template <std::size_t ByteCount>
CustomMockId<ByteCount> makeMockBytes(uint8_t high, uint8_t low) {
    CustomMockId<ByteCount> nodeId{};
    nodeId.bytes[0] = high;
    nodeId.bytes[ByteCount - 1] = low;
    return nodeId;
}

void test_id_equal_falls_back_to_msb_chunks_without_equal_hook() {
    using IdType = CustomMockId<12>;
    using TraitsType = CustomMockTraits<12>;
    const TraitsType traits;

    static_assert(
        !forest_sorting::detail::HasForestTraitsEqual<TraitsType, IdType>);
    static_assert(!forest_sorting::detail::HasNativeIdEqual<IdType>);
    static_assert(forest_sorting::detail::shouldCacheChunkIds<TraitsType>);

    const IdType low = makeMockBytes<12>(1, 2);
    const IdType sameLow = makeMockBytes<12>(1, 2);
    const IdType high = makeMockBytes<12>(1, 3);

    require(forest_sorting::detail::idEqual(low, sameLow, traits),
            "idEqual did not fall back to MSB chunk equality");
    require(!forest_sorting::detail::idEqual(low, high, traits),
            "idEqual treated different byte IDs as equal");
}

template <typename Traits, typename Node, typename Maker>
void assert_parent_join_correctness_and_paths(Maker maker) {
    using Id = decltype(std::declval<Traits>().id(std::declval<Node>()));
    Traits traits;

    // 1. Correctness: Root parent, child parent, and missing parent resolutions
    std::vector<Node> nodes(4);
    nodes[0].id = maker(1, 1);
    nodes[0].parentId = Id{}; // Root

    nodes[1].id = maker(1, 2); // Child of 0
    nodes[1].parentId = maker(1, 1);

    nodes[2].id = maker(2, 3); // Child of 1
    nodes[2].parentId = maker(1, 2);

    nodes[3].id = maker(3, 4); // Child of non-existent parent
    nodes[3].parentId = maker(9, 9);

    auto result =
        forest_sorting::detail::buildParentIndexRadixJoin(nodes, traits);
    require(result[0] == forest_sorting::detail::no_parent);
    require(result[1] == 0);
    require(result[2] == 1);
    require(result[3] == forest_sorting::detail::no_parent);

    // 2. Duplicate ID detection
    std::vector<Node> dupNodes = nodes;
    dupNodes.push_back(Node{maker(1, 1), Id{}});
    bool threwDuplicate = false;
    try {
        forest_sorting::detail::buildParentIndexRadixJoin(dupNodes, traits);
    } catch (const std::runtime_error &) {
        threwDuplicate = true;
    }
    require(threwDuplicate, "duplicate ID did not throw runtime_error");
}

void test_parent_radix_join_compile_paths_and_correctness() {
    // A. Verify compile-time branching decisions via static assertions
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<TestBytesTraits<4>>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<TestBytesTraits<8>>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<TestBytesTraits<12>>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<TestBytesTraits<16>>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<UInt128NodeTraits>);

    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<CustomMockTraits<4>>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<CustomMockTraits<8>>);
    static_assert(
        forest_sorting::detail::shouldCacheChunkIds<CustomMockTraits<12>>);
    static_assert(
        forest_sorting::detail::shouldCacheChunkIds<CustomMockTraits<16>>);

    struct CustomTraitsWithLess : UInt128NodeTraits {
        static bool less(forest_sorting::UInt128 lhs,
                         forest_sorting::UInt128 rhs) noexcept {
            return lhs < rhs;
        }
    };
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<CustomTraitsWithLess>);

    // B. Run correctness test across both cached and non-cached branches for
    // different sizes
    assert_parent_join_correctness_and_paths<TestBytesTraits<4>, TestNode<4>>(
        [](uint8_t high, uint8_t low) { return makeTestBytes<4>(high, low); });
    assert_parent_join_correctness_and_paths<TestBytesTraits<8>, TestNode<8>>(
        [](uint8_t high, uint8_t low) { return makeTestBytes<8>(high, low); });
    assert_parent_join_correctness_and_paths<TestBytesTraits<12>, TestNode<12>>(
        [](uint8_t high, uint8_t low) { return makeTestBytes<12>(high, low); });
    assert_parent_join_correctness_and_paths<TestBytesTraits<16>, TestNode<16>>(
        [](uint8_t high, uint8_t low) { return makeTestBytes<16>(high, low); });

    // Custom mock IDs lack native comparison and equality hooks. They exercise
    // cached ordering for >8-byte IDs and MSB-chunk equality fallback.
    assert_parent_join_correctness_and_paths<CustomMockTraits<4>,
                                             CustomMockNode<4>>(
        [](uint8_t high, uint8_t low) { return makeMockBytes<4>(high, low); });
    assert_parent_join_correctness_and_paths<CustomMockTraits<8>,
                                             CustomMockNode<8>>(
        [](uint8_t high, uint8_t low) { return makeMockBytes<8>(high, low); });
    assert_parent_join_correctness_and_paths<CustomMockTraits<12>,
                                             CustomMockNode<12>>(
        [](uint8_t high, uint8_t low) { return makeMockBytes<12>(high, low); });
    assert_parent_join_correctness_and_paths<CustomMockTraits<16>,
                                             CustomMockNode<16>>(
        [](uint8_t high, uint8_t low) { return makeMockBytes<16>(high, low); });

    // C. Verify UInt128 (native comparable)
    std::vector<forest_sorting::Node> uint128Nodes(4);
    UInt128NodeTraits uint128Traits;

    uint128Nodes[0].id = forest_sorting::makeId(1, 1);
    uint128Nodes[0].parentId = 0; // Root

    uint128Nodes[1].id = forest_sorting::makeId(1, 2);
    uint128Nodes[1].parentId = forest_sorting::makeId(1, 1);

    uint128Nodes[2].id = forest_sorting::makeId(2, 3);
    uint128Nodes[2].parentId = forest_sorting::makeId(1, 2);

    uint128Nodes[3].id = forest_sorting::makeId(3, 4);
    uint128Nodes[3].parentId = forest_sorting::makeId(9, 9);

    auto result = forest_sorting::detail::buildParentIndexRadixJoin(
        uint128Nodes, uint128Traits);
    require(result[0] == forest_sorting::detail::no_parent);
    require(result[1] == 0);
    require(result[2] == 1);
    require(result[3] == forest_sorting::detail::no_parent);
}

void test_parent_index_lookup_semantics() {
    // 1. Custom trait with no sentinel hook: compiles and sorts/resolves
    // correctly. parent ID == 0 and node with ID 0 exists: child attaches to
    // node 0 (because there's no sentinel hook)
    {
        using MockNode = CustomMockNode<8>;
        using MockTraits = CustomMockTraits<8>;
        MockTraits traits;

        std::vector<MockNode> nodes(3);
        // Node 0: ID = 0, Parent = 99 (missing -> root)
        nodes[0].id = makeMockBytes<8>(0, 0);
        nodes[0].parentId = makeMockBytes<8>(0, 99);

        // Node 1: ID = 1, Parent = 0 (exists -> should attach to Node 0)
        nodes[1].id = makeMockBytes<8>(0, 1);
        nodes[1].parentId = makeMockBytes<8>(0, 0);

        // Node 2: ID = 2, Parent = 99 (missing -> root)
        nodes[2].id = makeMockBytes<8>(0, 2);
        nodes[2].parentId = makeMockBytes<8>(0, 99);

        // Test with buildParentIndex (control table path)
        auto parentIdxControl =
            forest_sorting::detail::buildParentIndex(nodes, traits);
        require(parentIdxControl[0] == forest_sorting::detail::no_parent);
        require(parentIdxControl[1] == 0); // Attaches to Node 0!
        require(parentIdxControl[2] == forest_sorting::detail::no_parent);

        // Test with buildParentIndexRadixJoin (radix join path)
        auto parentIdxRadix =
            forest_sorting::detail::buildParentIndexRadixJoin(nodes, traits);
        require(parentIdxRadix[0] == forest_sorting::detail::no_parent);
        require(parentIdxRadix[1] == 0); // Attaches to Node 0!
        require(parentIdxRadix[2] == forest_sorting::detail::no_parent);
    }

    // 2. legacy UInt128Traits with optional sentinel hook (parent 0 is a
    // sentinel): parent ID == 0 and node with ID 0 exists: child becomes
    // root/no_parent (because 0 is sentinel)
    {
        using Node128 = forest_sorting::Node;
        using Traits128 = UInt128NodeTraits;
        Traits128 traits;

        std::vector<Node128> nodes(3);
        // Node 0: ID = 0, Parent = 99 (missing -> root)
        nodes[0].id = 0;
        nodes[0].parentId = 99;

        // Node 1: ID = 1, Parent = 0 (sentinel -> should skip lookup and become
        // root/no_parent)
        nodes[1].id = 1;
        nodes[1].parentId = 0;

        // Node 2: ID = 2, Parent = 99 (missing -> root)
        nodes[2].id = 2;
        nodes[2].parentId = 99;

        // Test with buildParentIndex
        auto parentIdxControl =
            forest_sorting::detail::buildParentIndex(nodes, traits);
        require(parentIdxControl[0] == forest_sorting::detail::no_parent);
        require(parentIdxControl[1] ==
                forest_sorting::detail::no_parent); // Sentinel skipped lookup!
        require(parentIdxControl[2] == forest_sorting::detail::no_parent);

        // Test with buildParentIndexRadixJoin
        auto parentIdxRadix =
            forest_sorting::detail::buildParentIndexRadixJoin(nodes, traits);
        require(parentIdxRadix[0] == forest_sorting::detail::no_parent);
        require(parentIdxRadix[1] ==
                forest_sorting::detail::no_parent); // Sentinel skipped lookup!
        require(parentIdxRadix[2] == forest_sorting::detail::no_parent);
    }

    // 3. parent ID missing from list: child becomes root/no_parent
    // parent ID == 0 and no node with ID 0 exists: child becomes root/no_parent
    {
        using MockNode = CustomMockNode<8>;
        using MockTraits = CustomMockTraits<8>;
        MockTraits traits;

        std::vector<MockNode> nodes(2);
        // Node 0: ID = 1, Parent = 0 (missing -> root)
        nodes[0].id = makeMockBytes<8>(0, 1);
        nodes[0].parentId = makeMockBytes<8>(0, 0);

        // Node 1: ID = 2, Parent = 99 (missing -> root)
        nodes[1].id = makeMockBytes<8>(0, 2);
        nodes[1].parentId = makeMockBytes<8>(0, 99);

        // Test with buildParentIndex
        auto parentIdxControl =
            forest_sorting::detail::buildParentIndex(nodes, traits);
        require(parentIdxControl[0] == forest_sorting::detail::no_parent);
        require(parentIdxControl[1] == forest_sorting::detail::no_parent);

        // Test with buildParentIndexRadixJoin
        auto parentIdxRadix =
            forest_sorting::detail::buildParentIndexRadixJoin(nodes, traits);
        require(parentIdxRadix[0] == forest_sorting::detail::no_parent);
        require(parentIdxRadix[1] == forest_sorting::detail::no_parent);
    }
}

void runGenericApiAndDepthTests() {
    runTest("chunk comparison matches byte lexicographic order",
            test_chunk_comparison_matches_byte_lexicographic_order);
    runTest("chunk permutation sort supports generic ID widths",
            test_chunk_permutation_sort_generic_id_widths);
#define X(width)                                                               \
    runTest("generic " #width "-byte public API forms",                        \
            test_generic_##width##_byte_public_api_forms);
    FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X
    runTest("sort accepts depth over two-byte prefix limit with three-byte",
            test_sort_accepts_depth_over_two_byte_prefix_limit_with_three_byte);
    runTest("precomputed depth API validates inputs",
            test_precomputed_depth_api_validates_inputs);
    runTest("dynamic and explicit prefix orders match",
            test_dynamic_and_explicit_prefix_orders_match);
    runTest("precomputed depth payload widths match",
            test_precomputed_depth_payload_widths_match);
    runTest("observed depth prefix boundaries",
            test_observed_depth_prefix_boundaries);
    runTest("sort accepts singleton with four-byte prefix",
            test_sort_accepts_singleton_with_four_byte_prefix);
    runTest("sort accepts sparse huge depth outlier",
            test_sort_accepts_sparse_huge_depth_outlier);
    runTest("dense threshold boundaries", test_dense_threshold_boundaries);
    runTest("empty forest handling", test_empty_forest_handling);
    runTest("precomputed depth API accepts singleton uint32 depth",
            test_precomputed_depth_api_accepts_singleton_uint32_depth);
    runTest("sort accepts shared prefix huge depths",
            test_sort_accepts_shared_prefix_huge_depths);
    runTest("sort accepts all equal huge depths",
            test_sort_accepts_all_equal_huge_depths);
    runTest("sort accepts many sparse singletons",
            test_sort_accepts_many_sparse_singletons);
    runTest("idEqual falls back without equal hook",
            test_id_equal_falls_back_to_msb_chunks_without_equal_hook);
    runTest("parent radix join compile paths and correctness",
            test_parent_radix_join_compile_paths_and_correctness);
    runTest("parent index lookup semantics",
            test_parent_index_lookup_semantics);
}
