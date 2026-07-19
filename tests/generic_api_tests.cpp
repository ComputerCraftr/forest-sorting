#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/benchmark_support/common/uint128_fixtures.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/validation.hpp"
#include "forest_sorting/traits.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"

#include "forest_sorting/detail/constants.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using forest_sorting::Node;
using forest_sorting::sortedCopyByDepthAndId;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;
using namespace forest_sorting::benchmark_support;

template <typename Nodes, typename Traits>
concept PublicPrecomputedDepthApiAcceptsObservedMaximum =
    requires(const Nodes &nodes, const Traits &traits,
             const std::vector<uint32_t> &depths, uint32_t observedMaxDepth) {
        forest_sorting::sortedOrderByDepthAndIdWithDepths(nodes, traits, depths,
                                                          observedMaxDepth);
    };

static_assert(!PublicPrecomputedDepthApiAcceptsObservedMaximum<
              std::vector<Node>, UInt128NodeTraits>);

template <typename Depth>
concept PublicPrecomputedDepthApiAccepts = requires(
    const std::vector<Node> &nodes, const UInt128NodeTraits &traits,
    const std::vector<Depth> &depths) {
    forest_sorting::sortedOrderByDepthAndIdWithDepths(nodes, traits, depths);
};

template <typename Nodes, typename Traits>
concept PublicComputedOrderAcceptsWidth =
    requires(const Nodes &nodes, const Traits &traits) {
        forest_sorting::sortedOrderByDepthAndId<1>(nodes, traits);
    };

static_assert(std::same_as<forest_sorting::detail::DepthValue<1>, uint8_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<2>, uint16_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<3>, uint32_t>);
static_assert(std::same_as<forest_sorting::detail::DepthValue<4>, uint32_t>);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(0U) == 1);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(255U) == 1);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(256U) == 2);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(65535U) == 2);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(65536U) == 3);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(16777215U) == 3);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(16777216U) == 4);
static_assert(forest_sorting::detail::depthPrefixBytesForMax(UINT32_MAX) == 4);
static_assert(PublicPrecomputedDepthApiAccepts<uint8_t>);
static_assert(PublicPrecomputedDepthApiAccepts<uint16_t>);
static_assert(PublicPrecomputedDepthApiAccepts<uint32_t>);
static_assert(PublicPrecomputedDepthApiAccepts<uint64_t>);
static_assert(!PublicPrecomputedDepthApiAccepts<bool>);
static_assert(
    !PublicComputedOrderAcceptsWidth<std::vector<Node>, UInt128NodeTraits>);

template <typename Value> struct IndexedOnlyContainer {
    std::vector<Value> values;

    std::size_t size() const noexcept { return values.size(); }
    const Value &operator[](std::size_t index) const noexcept {
        return values[index];
    }
    Value &operator[](std::size_t index) noexcept { return values[index]; }
};

static_assert(forest_sorting::IndexedNodeInput<IndexedOnlyContainer<Node>>);
static_assert(forest_sorting::CopyableNodeInput<IndexedOnlyContainer<Node>>);
static_assert(forest_sorting::MutableNodeInput<IndexedOnlyContainer<Node>>);

struct NonDefaultId {
    explicit NonDefaultId(uint32_t initialValue) : value(initialValue) {}

    uint32_t value;
};

struct NonDefaultNode {
    NonDefaultId id;
    NonDefaultId parentId;
};

struct NonDefaultIdTraits {
    using Id = NonDefaultId;
    static constexpr std::size_t id_byte_count = sizeof(uint32_t);

    static Id id(const NonDefaultNode &node) { return node.id; }
    static Id parent_id(const NonDefaultNode &node) { return node.parentId; }
    static bool is_parent_sentinel(const Id &nodeId) noexcept {
        return nodeId.value == 0;
    }
    static uint8_t byte_msb_first(const Id &nodeId,
                                  std::size_t byteIndex) noexcept {
        const std::size_t shift =
            (id_byte_count - 1U - byteIndex) * static_cast<std::size_t>(8);
        return static_cast<uint8_t>(nodeId.value >> shift);
    }
};

struct MutableOnlyTraits {
    using Id = NonDefaultId;
    static constexpr std::size_t id_byte_count = sizeof(uint32_t);

    std::size_t calls = 0;

    Id id(NonDefaultNode &node) {
        ++calls;
        return node.id;
    }
    static Id parent_id(const NonDefaultNode &node) { return node.parentId; }
    static bool is_parent_sentinel(const Id &nodeId) noexcept {
        return nodeId.value == 0;
    }
    static uint8_t byte_msb_first(const Id &nodeId,
                                  std::size_t byteIndex) noexcept {
        return NonDefaultIdTraits::byte_msb_first(nodeId, byteIndex);
    }
};

struct ZeroWidthTraits : NonDefaultIdTraits {
    static constexpr std::size_t id_byte_count = 0;
};

struct RuntimeWidthTraits : NonDefaultIdTraits {
    static inline std::size_t id_byte_count = sizeof(uint32_t);
};

struct ThrowingByteTraits {
    using Id = NonDefaultId;
    static constexpr std::size_t id_byte_count = sizeof(uint32_t);

    static Id id(const NonDefaultNode &node) { return node.id; }
    static Id parent_id(const NonDefaultNode &node) { return node.parentId; }
    static bool is_parent_sentinel(const Id &nodeId) noexcept {
        return nodeId.value == 0;
    }
    static uint8_t byte_msb_first(const Id &nodeId, std::size_t byteIndex) {
        (void)nodeId;
        (void)byteIndex;
        throw std::logic_error("byte extraction failed");
    }
};

static_assert(forest_sorting::ForestTraits<NonDefaultIdTraits, NonDefaultNode>);
static_assert(!forest_sorting::ForestTraits<MutableOnlyTraits, NonDefaultNode>);
static_assert(!forest_sorting::ForestTraits<ZeroWidthTraits, NonDefaultNode>);
static_assert(
    !forest_sorting::ForestTraits<RuntimeWidthTraits, NonDefaultNode>);

void test_public_api_accepts_non_default_constructible_ids() {
    const std::vector<NonDefaultNode> nodes = {
        {NonDefaultId{3}, NonDefaultId{1}},
        {NonDefaultId{2}, NonDefaultId{0}},
        {NonDefaultId{1}, NonDefaultId{0}},
    };

    const auto sorted =
        forest_sorting::sortedCopyByDepthAndId(nodes, NonDefaultIdTraits{});
    require(sorted.size() == nodes.size());
    require(sorted[0].id.value == 1);
    require(sorted[1].id.value == 2);
    require(sorted[2].id.value == 3);
    require(
        forest_sorting::verifySortedByDepthAndId(sorted, NonDefaultIdTraits{}));
}

void test_public_api_accepts_indexed_non_range_containers() {
    IndexedOnlyContainer<Node> nodes{{
        {makeId(0, 3), makeId(0, 1)},
        {makeId(0, 2), 0},
        {makeId(0, 1), 0},
    }};
    const IndexedOnlyContainer<uint32_t> depths{{1, 0, 0}};

    const auto explicitOrder =
        forest_sorting::sortedOrderByDepthAndIdWithDepths(
            nodes, UInt128NodeTraits{}, depths);
    require((explicitOrder == std::vector<std::size_t>{2, 1, 0}),
            "indexed-only depth input produced the wrong order");

    const auto sorted =
        forest_sorting::sortedCopyByDepthAndId(nodes, UInt128NodeTraits{});
    require(sorted[0].id == makeId(0, 1) && sorted[1].id == makeId(0, 2) &&
                sorted[2].id == makeId(0, 3),
            "indexed-only node input produced the wrong copy");

    forest_sorting::sortInPlaceByDepthAndId(nodes, UInt128NodeTraits{});
    require(nodes[0].id == makeId(0, 1) && nodes[1].id == makeId(0, 2) &&
                nodes[2].id == makeId(0, 3),
            "indexed-only mutable input produced the wrong in-place order");
}

void test_caller_trait_exceptions_are_not_forced_to_terminate() {
    std::vector<NonDefaultNode> nodes;
    nodes.reserve(33);
    for (uint32_t value = 1; value <= 33; ++value) {
        nodes.push_back({NonDefaultId{value}, NonDefaultId{0}});
    }

    bool propagated = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndId(nodes,
                                                      ThrowingByteTraits{});
    } catch (const std::logic_error &) {
        propagated = true;
    }
    require(propagated, "caller trait exception did not propagate");
}

struct CountingValidationTraits {
    using Id = TestBytes<4>;
    static constexpr std::size_t id_byte_count = 4;

    std::size_t *idCalls = nullptr;
    std::size_t *equalCalls = nullptr;
    std::size_t *parentCalls = nullptr;

    CountingValidationTraits(std::size_t *idCallCount = nullptr,
                             std::size_t *equalCallCount = nullptr,
                             std::size_t *parentCallCount = nullptr)
        : idCalls(idCallCount), equalCalls(equalCallCount),
          parentCalls(parentCallCount) {}

    Id id(const TestNode<4> &node) const noexcept {
        if (idCalls != nullptr) {
            ++*idCalls;
        }
        return node.id;
    }

    Id parent_id(const TestNode<4> &node) const noexcept {
        if (parentCalls != nullptr) {
            ++*parentCalls;
        }
        return node.parentId;
    }

    bool is_parent_sentinel(const Id &nodeId) const noexcept {
        return delegate_.is_parent_sentinel(nodeId);
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        return delegate_.byte_msb_first(nodeId, byteIndex);
    }

    template <std::size_t ChunkBytes>
    auto chunk_msb_first(const Id &nodeId,
                         std::size_t chunkIndex) const noexcept {
        return delegate_.template chunk_msb_first<ChunkBytes>(nodeId,
                                                              chunkIndex);
    }

    bool equal(const Id &lhs, const Id &rhs) const noexcept {
        if (equalCalls != nullptr) {
            ++*equalCalls;
        }
        return lhs == rhs;
    }

  private:
    HashFreeTestBytesTraits<4> delegate_;
};

void test_validation_boundaries_and_loop_counts() {
    const std::vector<TestNode<4>> nodes = {
        {makeTestBytes<4>(0, 4), {}},
        {makeTestBytes<4>(0, 3), {}},
        {makeTestBytes<4>(0, 2), {}},
        {makeTestBytes<4>(0, 1), {}},
    };

    std::size_t idCalls = 0;
    bool rejectedCount = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(
            nodes, CountingValidationTraits{&idCalls, nullptr},
            std::vector<uint8_t>{0, 0, 0});
    } catch (const std::runtime_error &) {
        rejectedCount = true;
    }
    require(rejectedCount, "depth count mismatch was accepted");
    require(idCalls == 0,
            "depth count mismatch performed ID sorting before rejection");

    bool rejectedParentCount = false;
    try {
        (void)forest_sorting::detail::computeDepths<1>(
            nodes, std::vector<std::size_t>{forest_sorting::detail::no_parent},
            CountingValidationTraits{});
    } catch (const std::runtime_error &) {
        rejectedParentCount = true;
    }
    require(rejectedParentCount, "parent count mismatch was accepted");

    std::size_t equalCalls = 0;
    const auto order = forest_sorting::sortedOrderByDepthAndId(
        nodes, CountingValidationTraits{nullptr, &equalCalls});
    require(order.size() == nodes.size());
    require(equalCalls == nodes.size() - 1,
            "computed sorting did not perform exactly one uniqueness scan");

    const uint32_t observedMax =
        forest_sorting::detail::validatePrecomputedDepthInput(
            nodes.size(), std::vector<uint16_t>{0, 7, 3, 2});
    require(observedMax == 7,
            "depth validation did not return the observed maximum");

    std::size_t adjacentChecks = 0;
    forest_sorting::detail::rejectAdjacentDuplicates(
        std::vector<std::size_t>{0, 1, 2, 3},
        [&](std::size_t, std::size_t) {
            ++adjacentChecks;
            return false;
        },
        "duplicate");
    require(adjacentChecks == 3,
            "duplicate validation did not make one adjacent pass");
}

void test_precomputed_depths_reject_duplicate_ids() {
    const TestBytes<4> duplicateId = makeTestBytes<4>(0, 1);
    const std::vector<TestNode<4>> nodes = {
        {duplicateId, {}},
        {duplicateId, {}},
    };

    bool rejected = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(
            nodes, HashFreeTestBytesTraits<4>{}, std::vector<uint8_t>{0, 0});
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "precomputed-depth API accepted duplicate IDs");
}

template <std::size_t ByteCount>
void assert_generic_hash_free_api_orders_by_depth_then_id() {
    using Id = TestBytes<ByteCount>;
    using Node = TestNode<ByteCount>;
    using Traits = HashFreeTestBytesTraits<ByteCount>;

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
        forest_sorting::sortedOrderByDepthAndId(nodes, Traits{});
    const auto order = forest_sorting::sortedOrderByDepthAndId(nodes, Traits{});
    const std::vector<uint8_t> depths = {1, 0, 1, 0};
    const auto precomputedOrder =
        forest_sorting::sortedOrderByDepthAndIdWithDepths(nodes, Traits{},
                                                          depths);

    require(order == explicitOrder);
    require(order == precomputedOrder);

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

static_assert(
    forest_sorting::ForestTraits<HashFreeTestBytesTraits<16>, TestNode<16>>);

#define FS_GENERIC_ID_BYTE_WIDTHS(X)                                           \
    X(16)                                                                      \
    X(20)                                                                      \
    X(28)                                                                      \
    X(32)                                                                      \
    X(37)                                                                      \
    X(64)

#define X(width)                                                               \
    void test_generic_##width##_byte_public_api_forms() {                      \
        assert_generic_hash_free_api_orders_by_depth_then_id<(width)>();       \
    }
FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X

void test_inferred_depth_dispatch_matches_reference_order() {
    const std::vector<Node> nodes = {
        {makeId(0, 1), 0},
        {makeId(0, 2), 0},
    };
    constexpr std::array<std::array<uint32_t, 2>, 11> depthCases = {
        std::array<uint32_t, 2>{0U, 0U},
        std::array<uint32_t, 2>{255U, 0U},
        std::array<uint32_t, 2>{256U, 0U},
        std::array<uint32_t, 2>{65535U, 0U},
        std::array<uint32_t, 2>{65536U, 0U},
        std::array<uint32_t, 2>{16777215U, 0U},
        std::array<uint32_t, 2>{16777216U, 0U},
        std::array<uint32_t, 2>{UINT32_MAX, 0U},
        std::array<uint32_t, 2>{255U, 256U},
        std::array<uint32_t, 2>{65535U, 65536U},
        std::array<uint32_t, 2>{16777215U, 16777216U},
    };

    const UInt128NodeTraits traits;
    for (const auto &depths : depthCases) {
        std::vector<std::size_t> expected = {0, 1};
        std::stable_sort(
            expected.begin(), expected.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                if (depths[lhs] != depths[rhs]) {
                    return depths[lhs] < depths[rhs];
                }
                for (std::size_t byte = 0;
                     byte < UInt128NodeTraits::id_byte_count; ++byte) {
                    const uint8_t lhsByte =
                        UInt128NodeTraits::byte_msb_first(nodes[lhs].id, byte);
                    const uint8_t rhsByte =
                        UInt128NodeTraits::byte_msb_first(nodes[rhs].id, byte);
                    if (lhsByte != rhsByte) {
                        return lhsByte < rhsByte;
                    }
                }
                return false;
            });

        const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
            nodes, traits, depths);
        require(order == expected,
                "inferred depth dispatch differed from reference order");
    }
}

void test_precomputed_depth_api_validates_inputs() {
    const std::vector<Node> nodes = {
        {makeId(0, 1), 0},
        {makeId(0, 2), 0},
    };

    bool rejectedSize = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(
            nodes, UInt128NodeTraits{}, std::vector<uint32_t>{0});
    } catch (const std::runtime_error &) {
        rejectedSize = true;
    }
    require(rejectedSize, "precomputed depth API accepted wrong depth count");

    std::size_t idCalls = 0;
    std::size_t parentCalls = 0;
    bool rejectedOverflow = false;
    try {
        (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(
            std::vector<TestNode<4>>{{makeTestBytes<4>(0, 1), {}},
                                     {makeTestBytes<4>(0, 2), {}}},
            CountingValidationTraits{&idCalls, nullptr, &parentCalls},
            std::vector<uint64_t>{0, uint64_t{UINT32_MAX} + 1U});
    } catch (const std::runtime_error &) {
        rejectedOverflow = true;
    }
    require(rejectedOverflow, "precomputed depth API accepted uint32 overflow");
    require(idCalls == 0 && parentCalls == 0,
            "depth overflow was validated after forest access");

    const std::vector<TestNode<4>> suppliedNodes = {
        {makeTestBytes<4>(0, 1), makeTestBytes<4>(0, 2)},
        {makeTestBytes<4>(0, 2), {}},
    };
    const auto suppliedOrder =
        forest_sorting::sortedOrderByDepthAndIdWithDepths(
            suppliedNodes,
            CountingValidationTraits{nullptr, nullptr, &parentCalls},
            std::vector<uint32_t>{0, 1});
    require((suppliedOrder == std::vector<std::size_t>{0, 1}),
            "supplied depths were not used");
    require(parentCalls == 0, "supplied depths were recalculated from parents");
}

void test_precomputed_depth_payload_widths_match() {
    const std::vector<Node> nodes = {
        {makeId(0, 3), 0},
        {makeId(0, 1), 0},
        {makeId(0, 2), 0},
    };
    const std::vector<uint16_t> narrowDepths = {1, 0, 1};
    const std::vector<uint32_t> wideDepths = {1, 0, 1};

    const auto narrowOrder = forest_sorting::sortedOrderByDepthAndIdWithDepths(
        nodes, UInt128NodeTraits{}, narrowDepths);
    const auto wideOrder = forest_sorting::sortedOrderByDepthAndIdWithDepths(
        nodes, UInt128NodeTraits{}, wideDepths);
    require(narrowOrder == wideOrder,
            "narrow and wide precomputed depths produced different orders");
}

void test_observed_depth_prefix_boundaries() {
    using forest_sorting::detail::maxDepthForPrefix;

    require(forest_sorting::detail::validatePrecomputedDepthInput(
                0, std::vector<uint32_t>{}) == 0,
            "empty depths produced a nonzero maximum");
    require(forest_sorting::detail::validatePrecomputedDepthInput(
                1, std::vector<uint32_t>{0xFFU}) <= maxDepthForPrefix<1>());
    require(forest_sorting::detail::validatePrecomputedDepthInput(
                1, std::vector<uint32_t>{0x100U}) > maxDepthForPrefix<1>());
    require(forest_sorting::detail::validatePrecomputedDepthInput(
                1, std::vector<uint32_t>{0x10000U}) > maxDepthForPrefix<2>());
    require(forest_sorting::detail::validatePrecomputedDepthInput(
                1, std::vector<uint32_t>{0x1000000U}) > maxDepthForPrefix<3>());
    require(forest_sorting::detail::validatePrecomputedDepthInput(
                1, std::vector<uint32_t>{UINT32_MAX}) <=
            maxDepthForPrefix<4>());
}

void test_sort_accepts_singleton_with_four_byte_prefix() {
    std::vector<Node> nodes;
    nodes.push_back(Node{makeId(0, 1), 0});
    // A singleton at depth 0 is valid under the 4-byte prefix configuration.
    const auto sorted = sortedCopyByDepthAndId(nodes);
    require(verifySortedByDepthAndId(sorted));
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
    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
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
            forest_sorting::sortedOrderByDepthAndId(emptyNodes, traits);
        require(order2.empty(),
                "sortedOrderByDepthAndId(empty) did not return empty");
    }

    // sortedOrderByDepthAndIdWithDepths(empty, emptyDepths) returns empty
    {
        std::vector<uint32_t> emptyDepths;
        const auto order1 = forest_sorting::sortedOrderByDepthAndIdWithDepths(
            emptyNodes, traits, emptyDepths);
        require(order1.empty(), "sortedOrderByDepthAndIdWithDepths(empty, "
                                "emptyDepths) did not return empty");
    }

    // sortedOrderByDepthAndIdWithDepths(empty, nonemptyDepths) throws size
    // mismatch
    {
        std::vector<uint32_t> nonemptyDepths = {1, 2};
        bool threwMismatch = false;
        try {
            (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(
                emptyNodes, traits, nonemptyDepths);
        } catch (const std::runtime_error &) {
            threwMismatch = true;
        }
        require(threwMismatch, "sortedOrderByDepthAndIdWithDepths(empty, "
                               "nonemptyDepths) did not throw size mismatch");
    }

    // sortedCopyByDepthAndId(empty) returns empty
    {
        const auto copy1 =
            forest_sorting::sortedCopyByDepthAndId(emptyNodes, traits);
        require(copy1.empty(),
                "sortedCopyByDepthAndId(empty) did not return empty");

        const auto copy2 =
            forest_sorting::sortedCopyByDepthAndId(emptyNodes, traits);
        require(copy2.empty(),
                "sortedCopyByDepthAndId(empty) did not return empty");
    }

    // sortedCopyByDepthAndId(empty) returns empty
    {
        const auto forestCopy1 =
            forest_sorting::sortedCopyByDepthAndId(emptyNodes);
        require(forestCopy1.empty(),
                "sortedCopyByDepthAndId(empty) did not return empty");
        const auto forestCopy2 =
            forest_sorting::sortedCopyByDepthAndId(emptyNodes);
        require(forestCopy2.empty(),
                "sortedCopyByDepthAndId(empty) did not return empty");
    }

    // sortInPlaceByDepthAndId(empty) does not crash
    {
        auto nodesToSort1 = emptyNodes;
        forest_sorting::sortInPlaceByDepthAndId(nodesToSort1, traits);
        require(nodesToSort1.empty(),
                "sortInPlaceByDepthAndId(empty) modified elements");

        auto nodesToSort2 = emptyNodes;
        forest_sorting::sortInPlaceByDepthAndId(nodesToSort2, traits);
        require(nodesToSort2.empty(),
                "sortInPlaceByDepthAndId(empty) modified elements");
    }

    // verifySortedByDepthAndId(empty) returns true
    {
        require(forest_sorting::verifySortedByDepthAndId(emptyNodes, traits),
                "verifySortedByDepthAndId(empty) did not return true");
        require(forest_sorting::verifySortedByDepthAndId(emptyNodes, traits),
                "verifySortedByDepthAndId(empty) did not return true");
    }
}

void test_precomputed_depth_api_accepts_singleton_uint32_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},
    };
    std::vector<uint32_t> depths = {0xFFFFFFFFU};
    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
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

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
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

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
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

    const auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths(
        nodes, UInt128NodeTraits{}, depths);

    requireSortedByDepthThenId(order, nodes, depths);
}

void runGenericApiAndDepthTestsImpl() {
    runTest("public API accepts non-default-constructible IDs",
            test_public_api_accepts_non_default_constructible_ids);
    runTest("public API accepts indexed non-range containers",
            test_public_api_accepts_indexed_non_range_containers);
    runTest("caller trait exceptions propagate",
            test_caller_trait_exceptions_are_not_forced_to_terminate);
    runTest("validation boundaries and loop counts",
            test_validation_boundaries_and_loop_counts);
    runTest("precomputed depths reject duplicate IDs",
            test_precomputed_depths_reject_duplicate_ids);
#define X(width)                                                               \
    runTest("generic " #width "-byte public API forms",                        \
            test_generic_##width##_byte_public_api_forms);
    FS_GENERIC_ID_BYTE_WIDTHS(X)
#undef X
    runTest("inferred depth dispatch matches reference order",
            test_inferred_depth_dispatch_matches_reference_order);
    runTest("precomputed depth API validates inputs",
            test_precomputed_depth_api_validates_inputs);
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
}

} // namespace

void runGenericApiAndDepthTests() { runGenericApiAndDepthTestsImpl(); }
