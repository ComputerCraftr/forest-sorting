#include "control_parent_index.hpp"
#include "forest_sorting/algorithms.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/adaptive_sort_variants.hpp"
#include "full/parent_registry.hpp"
#include "full/sort_registry.hpp"
#include "sort_baselines.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using forest_sorting::Node;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using namespace forest_sorting::test_support;
using forest_sorting::detail::buildParentIndexRadixJoin;

void assertParentBuildersMatch(const std::vector<Node> &nodes) {
    const auto expected = buildParentIndexForKind(ParentKind::Unordered, nodes);
    for (ParentKind parentKind : registeredParentKinds()) {
        const auto actual = buildParentIndexForKind(parentKind, nodes);
        if (actual != expected) {
            throw std::runtime_error(
                std::string(parentName(parentKind)) +
                " parent builder differs from unordered map");
        }
    }
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
        ParentKind::RadixJoinIdMsdSizeLadderChunk8Le1024Chunk16Le16384,
        ParentKind::RadixJoinIdMsdSizeLadderChunk8Le2048Chunk16Le32768,
        ParentKind::RadixJoinIdMsdSizeLadderChunk8Le4096Chunk16Le65536,
        ParentKind::RadixJoinIdMsdSizeLadderChunk16Le10000,
        ParentKind::RadixJoinIdMsdSizeLadderChunk16Le16384,
        ParentKind::RadixJoinIdMsdSizeLadderChunk16Le32768,
        ParentKind::RadixJoinIdMsdBytePartitionCore,
        ParentKind::RadixDirectoryIdMsdChunk32Prefix8,
        ParentKind::RadixDirectoryIdMsdChunk32Prefix16};
    const UInt128NodeTraits traits;
    auto verifyArtifacts = [&](const std::vector<Node> &nodes) {
        const auto expectedParent =
            buildParentIndexForKind(ParentKind::Unordered, nodes);
        for (ParentKind parentKind : kRadixKinds) {
            const auto artifacts =
                buildParentArtifactsForKind(parentKind, nodes);
            require(artifacts.parentIndex == expectedParent,
                    "radix artifact parent index differs from independent map");
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

void test_global_id_first_rejects_malformed_supplied_permutations() {
    const std::vector<Node> nodes = {
        {makeId(0, 30), 0},
        {makeId(0, 10), 0},
        {makeId(0, 20), 0},
    };
    const auto parentIndex =
        buildParentIndexForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);

    const auto requireRejected =
        [&](const std::vector<std::size_t> &permutation,
            std::string_view message) {
            bool rejected = false;
            try {
                (void)sortForestByGlobalIdPermutationThenDepthStable(
                    nodes, parentIndex, &permutation);
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            require(rejected, message);
        };

    requireRejected({1, 2, 3},
                    "same-sized out-of-range ID permutation was accepted");
    requireRejected({1, 1, 0},
                    "same-sized duplicate ID permutation was accepted");
    requireRejected({0, 1, 2},
                    "same-sized unsorted ID permutation was accepted");

    const std::vector<std::size_t> canonical = {1, 2, 0};
    const auto sorted = sortForestByGlobalIdPermutationThenDepthStable(
        nodes, parentIndex, &canonical);
    require(sorted[0].id == makeId(0, 10) && sorted[1].id == makeId(0, 20) &&
                sorted[2].id == makeId(0, 30),
            "canonical supplied ID permutation was rejected or changed");
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

void runParentIndexTests() {
    runTest("parent builders match for registered datasets",
            test_parent_builders_match_for_registered_datasets);
    runTest("radix parent artifacts retain sorted node permutations",
            test_radix_parent_artifacts_retain_sorted_node_permutations);
    runTest("radix directory parent lookup shapes",
            test_radix_directory_parent_lookup_shapes);
    runTest("global-ID-first sort computes or reuses ID permutation",
            test_global_id_first_sort_computes_or_reuses_id_permutation);
    runTest("global-ID-first rejects malformed supplied permutations",
            test_global_id_first_rejects_malformed_supplied_permutations);
    runTest("parent builders reject duplicate full UInt128 ID",
            test_parent_builders_reject_duplicate_full_uint128_id);
    runTest("parent builder falls back on insert probe limit",
            test_parent_builder_falls_back_when_insert_probe_limit_exceeded);
    runTest("parent builder falls back on lookup probe limit",
            test_parent_builder_falls_back_when_lookup_probe_limit_exceeded);
    runTest("parent builder identity hash rejects duplicate full ID",
            test_parent_builder_identity_hash_rejects_duplicate_full_id);
}
