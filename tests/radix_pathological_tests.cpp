#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/parent_registry.hpp"
#include "full/sort_registry.hpp"
#include "sort_baselines.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using forest_sorting::Node;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using namespace forest_sorting::test_support;

std::vector<Node> rootNodesFromIds(const std::vector<UInt128> &ids) {
    std::vector<Node> nodes;
    nodes.reserve(ids.size());
    for (UInt128 nodeId : ids) {
        nodes.push_back({nodeId, 0});
    }
    return nodes;
}

void requireRegisteredSortsMatchComparison(const std::vector<Node> &inputNodes,
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
    require(order == expected, "max-digit radix output was not materialized");
}

void test_registered_sorts_handle_recursive_small_ranges() {
    auto check = [](const std::vector<Node> &inputNodes,
                    std::string_view caseName) {
        requireRegisteredSortsMatchComparison(inputNodes, caseName);
    };

    // Force an initial radix split followed by ranges below the small-sort
    // cutoff. This guards the scheduler-to-tail-sort handoff without assuming
    // any particular scratch ownership implementation.
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
        check(rootNodesFromIds(ids), "recursive small ranges");
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

    requireRegisteredSortsMatchComparison(rootNodesFromIds(sortedIds),
                                          "already sorted input");
    requireRegisteredSortsMatchComparison(rootNodesFromIds(reversedIds),
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

    requireRegisteredSortsMatchComparison(rootNodesFromIds(sameHighByte),
                                          "same high byte IDs");
    requireRegisteredSortsMatchComparison(rootNodesFromIds(sameHigh32),
                                          "same high 32-bit IDs");
    requireRegisteredSortsMatchComparison(rootNodesFromIds(longPrefix),
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

        requireRegisteredSortsMatchComparison(rootNodesFromIds(ids),
                                              "small threshold boundary");
    }
}

void test_production_radix_parent_rejects_duplicate_ids() {
    const UInt128 duplicateId = makeId(0xDEADBEEFULL, 0xCAFEBABEULL);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    bool rejected = false;
    try {
        (void)buildParentIndexForKind(ParentKind::RadixJoinIdMsdChunk32, nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "production radix parent path accepted duplicate IDs");
}

void runRadixPathologicalTests() {
    runTest("radix MSD materializes output at max digit",
            test_radix_msd_partition_materializes_scratch_at_max_digit);
    runTest("registered sorts handle recursive small ranges",
            test_registered_sorts_handle_recursive_small_ranges);
    runTest("chunk MSD handles sorted and reverse inputs",
            test_chunk_msd_handles_sorted_and_reverse_inputs);
    runTest("chunk MSD handles high-prefix collisions",
            test_chunk_msd_handles_high_prefix_collisions);
    runTest("chunk MSD handles small threshold boundaries",
            test_chunk_msd_handles_small_threshold_boundaries);
    runTest("production radix parent rejects duplicate IDs",
            test_production_radix_parent_rejects_duplicate_ids);
}
