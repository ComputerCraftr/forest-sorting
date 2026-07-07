#include "adaptive_sort_variants.hpp"
#include "control_parent_index.hpp"
#include "forest_sorting/algorithms.hpp"
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
                    sortForestByGlobalIdPermutationThenDepthStable,
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
        "depth2-first-then-id-u8-msd-full-clear",
        "depth2-first-then-id-u16-msd-bitmask-le512",
        "depth2-first-then-id-u32-msd-bitmask-le512",
        "depth2-first-then-id-u64-msd-full-clear",
        "global-id-u32-msd-radix-then-depth-stable",
        "global-id-msd-chunk32-radix-then-depth-stable",
        "depth2-first-then-id-range-ladder-u8-le1024-u16-le16384-full-clear",
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

void test_parent_builders_match_for_registered_datasets() {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        assertParentBuildersMatch(
            makeGeneratedForestForKind(datasetKind, 10000));
    }
}

void test_radix_parent_artifacts_retain_sorted_node_permutations() {
    constexpr std::array<ParentKind, 13> kRadixKinds = {
        ParentKind::RadixJoinIdMsdChunk8,
        ParentKind::RadixJoinIdMsdChunk16,
        ParentKind::RadixJoinIdMsdChunk32,
        ParentKind::RadixJoinIdMsdChunk64,
        ParentKind::RadixJoinIdMsdRangeLadder1024_16384,
        ParentKind::RadixJoinIdMsdRangeLadder2048_32768,
        ParentKind::RadixJoinIdMsdRangeLadder4096_65536,
        ParentKind::RadixJoinIdMsdSizeLadder10000,
        ParentKind::RadixJoinIdMsdSizeLadder16384,
        ParentKind::RadixJoinIdMsdSizeLadder32768,
        ParentKind::RadixJoinIdMsdBytePartitionCore,
        ParentKind::RadixDirectoryIdMsdChunk32Prefix8,
        ParentKind::RadixDirectoryIdMsdChunk32Prefix16};
    constexpr std::array<ParentKind, 6> kChunk32EquivalentKinds = {
        ParentKind::RadixJoinIdMsdRangeLadder1024_16384,
        ParentKind::RadixJoinIdMsdRangeLadder2048_32768,
        ParentKind::RadixJoinIdMsdRangeLadder4096_65536,
        ParentKind::RadixJoinIdMsdSizeLadder10000,
        ParentKind::RadixJoinIdMsdSizeLadder16384,
        ParentKind::RadixJoinIdMsdSizeLadder32768};
    const UInt128NodeTraits traits;
    auto verifyArtifacts = [&](const std::vector<Node> &nodes) {
        const auto expectedParent =
            buildParentIndexForKind(ParentKind::Control, nodes);
        const auto chunk32Artifacts = buildParentArtifactsForKind(
            ParentKind::RadixJoinIdMsdChunk32, nodes);
        for (ParentKind parentKind : kRadixKinds) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            require(artifacts.parentIndex == expectedParent,
                    "radix artifact parent index differs from control");
            require(artifacts.hasIdPermutation,
                    "radix artifact did not retain an ID permutation");
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

        for (ParentKind parentKind : kChunk32EquivalentKinds) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            require(artifacts.parentIndex == chunk32Artifacts.parentIndex,
                    std::string(parentName(parentKind)) +
                        " parent index differs from chunk32 radix join");
            require(artifacts.idPermutation == chunk32Artifacts.idPermutation,
                    std::string(parentName(parentKind)) +
                        " retained permutation differs from chunk32 radix "
                        "join");
        }

        const auto chunk8Artifacts = buildParentArtifactsForKind(
            ParentKind::RadixJoinIdMsdChunk8, nodes);
        const auto bytePartitionArtifacts = buildParentArtifactsForKind(
            ParentKind::RadixJoinIdMsdBytePartitionCore, nodes);
        require(chunk8Artifacts.parentIndex ==
                        bytePartitionArtifacts.parentIndex &&
                    chunk8Artifacts.idPermutation ==
                        bytePartitionArtifacts.idPermutation,
                "chunk8 and byte-partition-core parent radix paths differ");
    };

    for (DatasetKind datasetKind : allDatasetKinds()) {
        verifyArtifacts(makeGeneratedForestForKind(datasetKind, 1000));
    }

    auto sortedNodes = makeGeneratedForestForKind(DatasetKind::Random, 1000);
    std::ranges::sort(sortedNodes, [&](const Node &lhs, const Node &rhs) {
        return forest_sorting::detail::idLess(lhs.id, rhs.id, traits);
    });
    verifyArtifacts(sortedNodes);
    std::ranges::reverse(sortedNodes);
    verifyArtifacts(sortedNodes);
}

void requireRadixDirectoryBuildersMatchChunk32(const std::vector<Node> &nodes,
                                               std::string_view scenario) {
    const auto expected =
        buildParentArtifactsForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);
    for (ParentKind parentKind :
         {ParentKind::RadixDirectoryIdMsdChunk32Prefix8,
          ParentKind::RadixDirectoryIdMsdChunk32Prefix16}) {
        const auto actual = buildParentArtifactsForKind(parentKind, nodes);
        require(actual.parentIndex == expected.parentIndex,
                std::string(parentName(parentKind)) +
                    " directory parent index differs for " +
                    std::string(scenario));
        require(actual.idPermutation == expected.idPermutation,
                std::string(parentName(parentKind)) +
                    " directory retained permutation differs for " +
                    std::string(scenario));
        require(actual.hasIdPermutation,
                std::string(parentName(parentKind)) +
                    " did not retain an ID permutation");
    }
}

void test_radix_directory_parent_lookup_shapes() {
    std::vector<Node> onePrefix;
    onePrefix.reserve(128);
    const uint64_t sharedHigh = 0x1234567800000000ULL;
    for (std::size_t idx = 0; idx < 128; ++idx) {
        const UInt128 nodeId =
            makeId(sharedHigh, static_cast<uint64_t>(idx) + 1ULL);
        const UInt128 parent =
            idx == 0 ? UInt128{0}
                     : makeId(sharedHigh, static_cast<uint64_t>(idx));
        onePrefix.push_back({nodeId, parent});
    }
    requireRadixDirectoryBuildersMatchChunk32(onePrefix, "one prefix bucket");

    std::vector<Node> allHighBytePrefixes;
    allHighBytePrefixes.reserve(512);
    for (std::size_t highByte = 0; highByte < 256; ++highByte) {
        const uint64_t high =
            (static_cast<uint64_t>(highByte) << 56U) | 0x0001000000000000ULL;
        const UInt128 rootId =
            makeId(high, static_cast<uint64_t>(highByte) + 1ULL);
        const UInt128 childId =
            makeId(high, static_cast<uint64_t>(highByte) + 0x1000ULL);
        allHighBytePrefixes.push_back({rootId, 0});
        allHighBytePrefixes.push_back({childId, rootId});
    }
    requireRadixDirectoryBuildersMatchChunk32(allHighBytePrefixes,
                                              "all high-byte prefixes");

    std::vector<Node> sparsePrefixes = {
        {makeId(0x0100000000000000ULL, 10), 0},
        {makeId(0x7F00000000000000ULL, 10), 0},
        {makeId(0xF000000000000000ULL, 10), 0},
        {makeId(0x0100000000000000ULL, 11), makeId(0x0100000000000000ULL, 10)},
        {makeId(0x7F00000000000000ULL, 11), makeId(0x7F00000000000000ULL, 10)},
        {makeId(0xF000000000000000ULL, 11), makeId(0xF000000000000000ULL, 10)},
    };
    requireRadixDirectoryBuildersMatchChunk32(sparsePrefixes,
                                              "sparse prefixes");

    std::vector<Node> longSharedPrefix = {
        {makeId(0xABCDEF1234567890ULL, 4), makeId(0xABCDEF1234567890ULL, 1)},
        {makeId(0xABCDEF1234567890ULL, 1), 0},
        {makeId(0xABCDEF1234567890ULL, 3), makeId(0xABCDEF1234567890ULL, 1)},
        {makeId(0xABCDEF1234567890ULL, 2), makeId(0xABCDEF1234567890ULL, 1)},
    };
    requireRadixDirectoryBuildersMatchChunk32(longSharedPrefix,
                                              "long shared prefix");
}

void test_global_id_first_sort_computes_or_reuses_id_permutation() {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        const auto nodes = makeGeneratedForestForKind(datasetKind, 1000);
        const auto publicProduction = forest_sorting::sortedCopyByDepthAndId<2>(
            nodes, UInt128NodeTraits{});
        for (ParentKind parentKind : registeredParentKinds()) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            const auto expected =
                sortForestByComparisonWithParent(nodes, artifacts.parentIndex);
            const std::vector<std::size_t> *idPermutation =
                artifacts.hasIdPermutation ? &artifacts.idPermutation : nullptr;
            const auto actual =
                sortForestForKind(SortKind::GlobalIdPermutationThenDepthStable,
                                  nodes, artifacts.parentIndex, idPermutation);
            require(sameNodes(actual, expected),
                    std::string(parentName(parentKind)) +
                        " global-ID-first sort differed from comparison");

            const auto computed =
                sortForestForKind(SortKind::GlobalIdPermutationThenDepthStable,
                                  nodes, artifacts.parentIndex, nullptr);
            require(sameNodes(computed, actual),
                    "computed and reused ID permutations produced different "
                    "orders");
            require(sameNodes(publicProduction, actual),
                    "public production sort differed from global-ID-first "
                    "benchmark path");
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
void requireUInt128IdentityHashControlMatchesRadix(
    const std::vector<Node> &nodes, const Traits &traits,
    std::string_view message) {
    const auto controlParent = buildParentIndexControl(nodes, traits);
    const auto radixParent = buildParentIndexRadixJoin(nodes, traits);
    require(controlParent == radixParent, message);
}

void test_parent_builder_falls_back_when_insert_probe_limit_exceeded() {
    constexpr std::size_t groupSize = 8;
    const std::size_t nodeCount =
        (max_probe_groups_before_fallback * groupSize) + 1;
    requireUInt128IdentityHashControlMatchesRadix(
        makeHighIdentityCollisionRoots(nodeCount),
        UInt128HighIdentityHashTraits{},
        "insert probe-limit fallback differed from radix join");
}

void test_parent_builder_falls_back_when_lookup_probe_limit_exceeded() {
    constexpr std::size_t groupSize = 8;
    const std::size_t nodeCount = max_probe_groups_before_fallback * groupSize;
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    const UInt128 externalParent = makeId(50000, 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL),
                             externalParent});
    }

    requireUInt128IdentityHashControlMatchesRadix(
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
        (void)buildParentIndexControl(nodes, UInt128HighIdentityHashTraits{});
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "identity-hash control parent builder accepted "
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
        buildParentIndexForKind(ParentKind::Control, nodes);
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
        buildParentIndexForKind(ParentKind::Control, nodes);
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

void test_dense_depth2_baseline_limits() {
    using namespace forest_sorting::test_support;

    // Accepts depth 65535 (2-byte limit)
    {
        std::vector<Node> nodes;
        appendDeepChain(nodes, 65535, 0x111ULL);
        const auto parentIndex =
            buildParentIndexForKind(ParentKind::Control, nodes);
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
            buildParentIndexForKind(ParentKind::Control, nodes);
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
    const auto artifacts = buildParentArtifactsForKind(
        ParentKind::RadixJoinIdMsdChunk32, inputNodes);
    const auto expected =
        sortForestByComparisonWithParent(inputNodes, artifacts.parentIndex);

    for (const SortRegistryEntry &entry : getSortRegistry()) {
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
        runTest("removed benchmark labels do not parse",
                test_removed_benchmark_labels_do_not_parse);
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
        runTest("radix directory parent lookup shapes",
                test_radix_directory_parent_lookup_shapes);
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
            "all sort methods match canonical order across permutations",
            test_all_sort_methods_match_canonical_order_across_permutations);
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
