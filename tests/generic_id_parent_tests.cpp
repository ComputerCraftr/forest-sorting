#include "control_parent_index.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_small_sort.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "hash_support.hpp"
#include "hashed_test_bytes.hpp"
#include "id_dispatch_oracle.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace forest_sorting::test_support;
using forest_sorting::UInt128NodeTraits;

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
        return forest_sorting::test_support::fnvHashBytes(nodeId.bytes);
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

void test_comparison_and_equality_dispatch_priority() {
    // 1. Test trait preference: less and equal hooks.
    IdDispatchCounters counters;
    InstrumentedTraitTraits traits{&counters};
    InstrumentedTraitId id1{10};
    InstrumentedTraitId id2{20};

    require(forest_sorting::detail::idLess(id1, id2, traits), "idLess failed");
    requireDispatchUsed(counters, IdDispatchPath::Trait,
                        "idLess did not use the Trait path");
    requireDispatchUnused(counters, IdDispatchPath::ByteFallback,
                          "trait idLess used the MSB byte fallback");

    counters.reset();
    require(!forest_sorting::detail::idEqual(id1, id2, traits),
            "idEqual failed");
    requireDispatchUsed(counters, IdDispatchPath::Trait,
                        "idEqual did not use the Trait path");
    requireDispatchUnused(counters, IdDispatchPath::ByteFallback,
                          "trait idEqual used the MSB byte fallback");

    counters.reset();
    require(forest_sorting::detail::compareNodeIds(id1, id2, traits) == -1,
            "compareNodeIds failed");
    requireDispatchUsed(counters, IdDispatchPath::Trait,
                        "compareNodeIds did not use the Trait path");
    requireDispatchUnused(counters, IdDispatchPath::ByteFallback,
                          "trait compareNodeIds used the MSB byte fallback");

    // 2. Test native operator preference when traits lack hooks.
    counters.reset();
    InstrumentedNativeId::counters = &counters;
    InstrumentedNativeTraits nTraits;
    InstrumentedNativeId nid1{10};
    InstrumentedNativeId nid2{20};

    require(forest_sorting::detail::idLess(nid1, nid2, nTraits),
            "idLess native failed");
    requireDispatchUsed(counters, IdDispatchPath::Native,
                        "idLess did not use the Native path");

    counters.reset();
    require(!forest_sorting::detail::idEqual(nid1, nid2, nTraits),
            "idEqual native failed");
    requireDispatchUsed(counters, IdDispatchPath::Native,
                        "idEqual did not use the Native path");

    counters.reset();
    require(forest_sorting::detail::compareNodeIds(nid1, nid2, nTraits) == -1,
            "compareNodeIds native failed");
    requireDispatchUsed(counters, IdDispatchPath::Native,
                        "compareNodeIds did not use the Native path");
    InstrumentedNativeId::counters = nullptr;

    // 3. Test that MSB fallback is used when both traits and native are absent.
    {
        using MockId = InstrumentedByteId<12>;
        const InstrumentedByteTraits<12> mTraits{&counters, true};
        MockId mid1{};
        MockId mid2{};
        mid1.bytes[0] = 1;
        mid1.bytes[11] = 2;
        mid2.bytes[0] = 1;
        mid2.bytes[11] = 3;

        counters.reset();
        require(forest_sorting::detail::idLess(mid1, mid2, mTraits),
                "idLess fallback failed");
        requireDispatchUsed(counters, IdDispatchPath::MsbFallback,
                            "idLess did not use the MSB fallback path");
        requireDispatchUsed(counters, IdDispatchPath::Chunk8,
                            "idLess fallback did not use chunk access");

        counters.reset();
        require(!forest_sorting::detail::idEqual(mid1, mid2, mTraits),
                "idEqual fallback failed");
        requireDispatchUsed(counters, IdDispatchPath::MsbFallback,
                            "idEqual did not use the MSB fallback path");
        requireDispatchUsed(counters, IdDispatchPath::Chunk8,
                            "idEqual fallback did not use chunk access");

        counters.reset();
        require(forest_sorting::detail::compareNodeIds(mid1, mid2, mTraits) ==
                    -1,
                "compareNodeIds fallback failed");
        requireDispatchUsed(counters, IdDispatchPath::MsbFallback,
                            "compareNodeIds did not use the MSB fallback path");
        requireDispatchUsed(counters, IdDispatchPath::Chunk8,
                            "compareNodeIds fallback did not use chunk access");
    }

    // 4. Test that traits/native hooks suppress caching
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<InstrumentedTraitTraits>);
    static_assert(
        !forest_sorting::detail::shouldCacheChunkIds<InstrumentedNativeTraits>);

    // 5. Test that caching works and cached path avoids slow MSB fallback
    static_assert(forest_sorting::detail::shouldCacheChunkIds<
                  InstrumentedByteTraits<12>>);

    counters.reset();
    const InstrumentedByteTraits<12> cTraits{&counters};
    std::vector<std::size_t> order = {0, 1};
    std::vector<InstrumentedByteId<12>> nodes(2);
    nodes[0].bytes[0] = 2;
    nodes[1].bytes[0] = 1;
    auto idForIndex = [&](std::size_t idx) { return nodes[idx]; };

    bool comparisonDone = false;
    forest_sorting::detail::withFixedSmallSortAccessor<2>(
        order, idForIndex, cTraits, 0, 2, [&](auto &accessor) {
            requireDispatchUsed(counters, IdDispatchPath::Chunk8,
                                "caching initialization did not fill cached "
                                "chunks");
            requireDispatchUnused(counters, IdDispatchPath::ByteFallback,
                                  "cached chunk initialization used the byte "
                                  "fallback despite chunk access support");

            counters.reset();
            accessor.save(0);
            require(accessor.isLessOrEqual(1), "cached comparison failed");
            comparisonDone = true;

            requireNoDispatch(counters,
                              "cached accessor comparison called ID traits");

            compareCachedIdChunksWithOracle(accessor.idChunks[0],
                                            accessor.savedId, counters);
            requireDispatchUsed(counters, IdDispatchPath::CachedChunk,
                                "cached chunk comparison was not observed");
        });
    require(comparisonDone, "accessor lambda did not run");
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
    const auto chunk8 =
        forest_sorting::detail::buildParentIndexRadixJoinResultByMsdChunks<1>(
            nodes, traits);
    const auto chunk16 =
        forest_sorting::detail::buildParentIndexRadixJoinResultByMsdChunks<2>(
            nodes, traits);
    const auto chunk32 =
        forest_sorting::detail::buildParentIndexRadixJoinResultByMsdChunks<4>(
            nodes, traits);
    const auto chunk64 =
        forest_sorting::detail::buildParentIndexRadixJoinResultByMsdChunks<8>(
            nodes, traits);
    const auto defaultResult =
        forest_sorting::detail::buildParentIndex(nodes, traits);
    require(defaultResult == result,
            "default parent builder differed from radix join");
    require(result[0] == forest_sorting::detail::no_parent);
    require(result[1] == 0);
    require(result[2] == 1);
    require(result[3] == forest_sorting::detail::no_parent);
    require(chunk8.parentIndex == result && chunk16.parentIndex == result &&
                chunk32.parentIndex == result && chunk64.parentIndex == result,
            "radix join chunk widths produced different parent indexes");
    require(chunk8.idPermutation == chunk16.idPermutation &&
                chunk16.idPermutation == chunk32.idPermutation &&
                chunk32.idPermutation == chunk64.idPermutation,
            "radix join chunk widths produced different ID permutations");

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

    auto requireChunkDuplicateRejected = [&]<std::size_t RadixChunkBytes>() {
        bool rejected = false;
        try {
            (void)forest_sorting::detail::
                buildParentIndexRadixJoinResultByMsdChunks<RadixChunkBytes>(
                    dupNodes, traits);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected,
                "radix join chunk width accepted a duplicate node ID");
    };
    requireChunkDuplicateRejected.template operator()<1>();
    requireChunkDuplicateRejected.template operator()<2>();
    requireChunkDuplicateRejected.template operator()<4>();
    requireChunkDuplicateRejected.template operator()<8>();
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

        // Test with the optional control-table path.
        auto parentIdxControl =
            forest_sorting::test_support::buildParentIndexControl(nodes,
                                                                  traits);
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
        const forest_sorting::test_support::UInt128NodeHashedTraits
            hashedTraits;

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

        // Test with the optional control-table path.
        auto parentIdxControl =
            forest_sorting::test_support::buildParentIndexControl(nodes,
                                                                  hashedTraits);
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

        // Test with the optional control-table path.
        auto parentIdxControl =
            forest_sorting::test_support::buildParentIndexControl(nodes,
                                                                  traits);
        require(parentIdxControl[0] == forest_sorting::detail::no_parent);
        require(parentIdxControl[1] == forest_sorting::detail::no_parent);

        // Test with buildParentIndexRadixJoin
        auto parentIdxRadix =
            forest_sorting::detail::buildParentIndexRadixJoin(nodes, traits);
        require(parentIdxRadix[0] == forest_sorting::detail::no_parent);
        require(parentIdxRadix[1] == forest_sorting::detail::no_parent);
    }
}

void runGenericIdParentTests() {
    runTest("idEqual falls back without equal hook",
            test_id_equal_falls_back_to_msb_chunks_without_equal_hook);
    runTest("comparison and equality dispatch priority",
            test_comparison_and_equality_dispatch_priority);
    runTest("parent radix join compile paths and correctness",
            test_parent_radix_join_compile_paths_and_correctness);
    runTest("parent index lookup semantics",
            test_parent_index_lookup_semantics);
}
