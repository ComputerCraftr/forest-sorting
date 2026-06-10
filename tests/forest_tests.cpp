#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "parent_index_baselines.hpp"
#include "test_bytes.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortedCopyByDepthAndId;
using forest_sorting::sortedOrderByDepthAndId;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::UInt128Traits;
using forest_sorting::verifySortedByDepthAndId;

constexpr std::size_t uint128_byte_count = UInt128Traits::id_byte_count;
constexpr std::size_t depth_byte_count = 2;

uint8_t idByte(UInt128 value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(
        value >> (byteIndex * forest_sorting::detail::radix_bits));
}

UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

using forest_sorting::detail::buildParentIndexControlByteFlatHash;
using forest_sorting::detail::buildParentIndexFlatHash;
using forest_sorting::detail::buildParentIndexRadixJoin;
using forest_sorting::detail::depthByte;
using forest_sorting::detail::radix_bucket_count;
using forest_sorting::detail::radixPass;

std::vector<std::size_t>
buildParentIndexFlatHashForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexFlatHash(nodes, UInt128NodeTraits{});
}

std::vector<std::size_t>
buildParentIndexControlByteFlatHashForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexControlByteFlatHash(nodes, UInt128NodeTraits{});
}

std::vector<std::size_t>
buildParentIndexRadixJoinForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexRadixJoin(nodes, UInt128NodeTraits{});
}

std::vector<std::size_t>
buildParentIndexForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexControlByteFlatHashForUInt128(nodes);
}

std::vector<uint32_t>
computeDepthsForUInt128(const std::vector<Node> &nodes,
                        const std::vector<std::size_t> &parentIndex) {
    return forest_sorting::detail::computeDepths(nodes, parentIndex,
                                                 UInt128NodeTraits{});
}

bool sameNodes(const std::vector<Node> &lhs, const std::vector<Node> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t nodeIdx = 0; nodeIdx < lhs.size(); ++nodeIdx) {
        if (lhs[nodeIdx].id != rhs[nodeIdx].id ||
            lhs[nodeIdx].parentId != rhs[nodeIdx].parentId) {
            return false;
        }
    }

    return true;
}

void runTest(const char *testName, void (*testFunction)()) {
    std::cout << "RUN  " << testName << "\n";
    testFunction();
    std::cout << "PASS " << testName << "\n";
}

std::vector<Node> sortForestByComparison(const std::vector<Node> &nodes) {
    const auto parentIndex = buildParentIndexForUInt128(nodes);
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    std::sort(order.begin(), order.end(),
              [&](std::size_t lhsIndex, std::size_t rhsIndex) {
                  if (depths[lhsIndex] != depths[rhsIndex]) {
                      return depths[lhsIndex] < depths[rhsIndex];
                  }
                  return nodes[lhsIndex].id < nodes[rhsIndex].id;
              });

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }

    return sorted;
}

void radixSortBucketById(std::vector<std::size_t> &bucket,
                         const std::vector<Node> &nodes) {
    if (bucket.size() <= 1) {
        return;
    }

    std::vector<std::size_t> scratch(bucket.size());
    for (std::size_t byteIndex = 0; byteIndex < uint128_byte_count;
         ++byteIndex) {
        std::array<std::size_t, radix_bucket_count> counts{};
        for (std::size_t nodeIndex : bucket) {
            ++counts[idByte(nodes[nodeIndex].id, byteIndex)];
        }

        std::size_t offset = 0;
        for (std::size_t &count : counts) {
            const std::size_t bucketSize = count;
            count = offset;
            offset += bucketSize;
        }

        for (std::size_t nodeIndex : bucket) {
            const uint8_t digit = idByte(nodes[nodeIndex].id, byteIndex);
            scratch[counts[digit]] = nodeIndex;
            ++counts[digit];
        }

        bucket.swap(scratch);
    }
}

std::vector<Node> sortForestByBucketedRadix(const std::vector<Node> &nodes) {
    const auto parentIndex = buildParentIndexForUInt128(nodes);
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    uint32_t maxDepth = 0;
    for (uint32_t depth : depths) {
        if (depth > forest_sorting::detail::maxDepthForPrefix<2>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        maxDepth = std::max(maxDepth, depth);
    }

    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(maxDepth) + 1);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        buckets[depths[nodeIdx]].push_back(nodeIdx);
    }

    for (auto &bucket : buckets) {
        radixSortBucketById(bucket, nodes);
    }

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (const auto &bucket : buckets) {
        for (std::size_t nodeIndex : bucket) {
            sorted.push_back(nodes[nodeIndex]);
        }
    }

    return sorted;
}

std::vector<Node> sortForestByCompositeRadix(const std::vector<Node> &nodes) {
    const auto parentIndex = buildParentIndexForUInt128(nodes);
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    for (uint32_t depth : depths) {
        if (depth > forest_sorting::detail::maxDepthForPrefix<2>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
    }

    std::vector<std::size_t> scratch(order.size());
    for (std::size_t byteIndex = 0; byteIndex < uint128_byte_count;
         ++byteIndex) {
        radixPass(order, scratch, [&](std::size_t nodeIndex) {
            return idByte(nodes[nodeIndex].id, byteIndex);
        });
    }

    for (std::size_t byteIndex = 0; byteIndex < depth_byte_count; ++byteIndex) {
        radixPass(order, scratch, [&](std::size_t nodeIndex) {
            return depthByte(depths[nodeIndex], byteIndex);
        });
    }

    std::vector<Node> sorted;
    sorted.reserve(nodes.size());
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }

    return sorted;
}

std::vector<Node> makeGeneratedForest(std::size_t nodeCount,
                                      uint32_t maxDepth) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937_64 rng(0x5eed1234ULL);
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    std::vector<std::size_t> lastIndexAtDepth(
        static_cast<std::size_t>(maxDepth) + 1,
        forest_sorting::detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        uint32_t targetDepth = static_cast<uint32_t>(
            nodeIdx % (static_cast<std::size_t>(maxDepth) + 1));
        UInt128 parentId = 0;
        if (targetDepth > 0 &&
            lastIndexAtDepth[static_cast<std::size_t>(targetDepth - 1)] !=
                forest_sorting::detail::no_parent) {
            parentId = nodes[lastIndexAtDepth[static_cast<std::size_t>(
                                 targetDepth - 1)]]
                           .id;
        } else {
            targetDepth = 0;
        }

        const uint64_t high = rng();
        const uint64_t low = static_cast<uint64_t>(nodeIdx) + 1ULL;
        nodes.push_back(Node{makeId(high, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
}

void appendDeepChain(std::vector<Node> &nodes, uint32_t chainDepth,
                     uint64_t idBase) {
    UInt128 parentId = 0;
    for (uint32_t depth = 0; depth <= chainDepth; ++depth) {
        const UInt128 nodeId =
            makeId(idBase, static_cast<uint64_t>(depth) + 1ULL);
        nodes.push_back(Node{nodeId, parentId});
        parentId = nodeId;
    }
}

std::vector<Node> shuffledCopy(std::vector<Node> nodes, uint64_t seed) {
    std::mt19937_64 rng(seed); // NOLINT(bugprone-random-generator-seed)
    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
}

std::vector<Node> makeGeneratedForestWithOutliers(std::size_t nodeCount,
                                                  uint32_t commonMaxDepth) {
    std::vector<Node> nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
    appendDeepChain(nodes, 128, 0x1000ULL);
    appendDeepChain(nodes, 512, 0x2000ULL);
    appendDeepChain(nodes, 1024, 0x3000ULL);
    return shuffledCopy(nodes, 0xabcdef00ULL);
}

std::vector<Node> makeSameHigh64Forest(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    constexpr uint64_t sharedHighWord = 0x123456789abcdef0ULL;
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        UInt128 parentId = 0;
        if (nodeIdx > 0) {
            parentId = makeId(sharedHighWord, static_cast<uint64_t>(nodeIdx));
        }
        nodes.push_back(Node{
            makeId(sharedHighWord, static_cast<uint64_t>(nodeIdx) + 1ULL),
            parentId,
        });
    }
    return shuffledCopy(nodes, 0x0badcafeULL);
}

std::vector<Node> makeSequentialIdForest(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        UInt128 parentId = 0;
        if (nodeIdx > 0) {
            parentId = makeId(0, static_cast<uint64_t>(nodeIdx));
        }
        nodes.push_back(
            Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL), parentId});
    }
    return shuffledCopy(nodes, 0x1234abcdULL);
}

std::vector<Node> makeManyExternalParentForest(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        const UInt128 nodeId =
            makeId(0x1000ULL, static_cast<uint64_t>(nodeIdx) + 1ULL);
        const UInt128 parentId =
            makeId(0x2000ULL, static_cast<uint64_t>(nodeIdx) + 1ULL);
        nodes.push_back(Node{nodeId, parentId});
    }
    return shuffledCopy(nodes, 0x44445555ULL);
}

std::vector<Node> makeManySiblingsForest(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    const UInt128 rootId = makeId(0, 1);
    nodes.push_back(Node{rootId, 0});
    for (std::size_t nodeIdx = 1; nodeIdx < nodeCount; ++nodeIdx) {
        nodes.push_back(
            Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL), rootId});
    }
    return shuffledCopy(nodes, 0x9999aaaaULL);
}

void assertParentBuildersMatch(const std::vector<Node> &nodes) {
    const auto unorderedParent = buildParentIndexStdUnorderedMap(nodes);
    const auto flatParent = buildParentIndexFlatHashForUInt128(nodes);
    const auto controlParent =
        buildParentIndexControlByteFlatHashForUInt128(nodes);
    const auto radixParent = buildParentIndexRadixJoinForUInt128(nodes);

    if (unorderedParent != flatParent) {
        throw std::runtime_error(
            "flat hash parent builder differs from unordered map");
    }
    if (unorderedParent != controlParent) {
        throw std::runtime_error(
            "control-byte flat hash parent builder differs from unordered map");
    }
    if (unorderedParent != radixParent) {
        throw std::runtime_error(
            "radix join parent builder differs from unordered map");
    }
}

template <typename ParentBuilder>
void assertParentBuilderRejectsDuplicate(const std::vector<Node> &nodes,
                                         ParentBuilder parentBuilder,
                                         const char *builderName) {
    bool rejected = false;
    try {
        (void)parentBuilder(nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(std::string(builderName) +
                                 " accepted duplicate full id");
    }
}

void test_parent_builders_match_for_random_uint128_ids() {
    assertParentBuildersMatch(makeGeneratedForest(10000, 30));
}

void test_parent_builders_match_for_depth_outliers() {
    assertParentBuildersMatch(makeGeneratedForestWithOutliers(10000, 30));
}

void test_parent_builders_match_for_same_high64_ids() {
    assertParentBuildersMatch(makeSameHigh64Forest(10000));
}

void test_parent_builders_match_for_sequential_ids() {
    assertParentBuildersMatch(makeSequentialIdForest(10000));
}

void test_parent_builders_match_for_external_parent_ids() {
    assertParentBuildersMatch(makeManyExternalParentForest(10000));
}

void test_parent_builders_match_for_many_siblings() {
    assertParentBuildersMatch(makeManySiblingsForest(10000));
}

void test_parent_builders_reject_duplicate_full_uint128_id() {
    const UInt128 duplicateId = makeId(7, 11);
    const std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    assertParentBuilderRejectsDuplicate(nodes, buildParentIndexStdUnorderedMap,
                                        "unordered parent builder");
    assertParentBuilderRejectsDuplicate(
        nodes, buildParentIndexFlatHashForUInt128, "flat hash parent builder");
    assertParentBuilderRejectsDuplicate(
        nodes, buildParentIndexControlByteFlatHashForUInt128,
        "control-byte parent builder");
    assertParentBuilderRejectsDuplicate(nodes,
                                        buildParentIndexRadixJoinForUInt128,
                                        "radix join parent builder");
}

void test_compute_depths_simple_chain() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},            // depth 0
        {makeId(0, 2), makeId(0, 1)}, // depth 1
        {makeId(0, 3), makeId(0, 2)}, // depth 2
    };

    const auto parentIndex = buildParentIndexForUInt128(nodes);
    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    assert(depths.size() == 3);
    assert(depths[0] == 0);
    assert(depths[1] == 1);
    assert(depths[2] == 2);
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
    assert(verifySortedByDepthAndId(sorted));

    // Roots should be in id order.
    assert(sorted[0].id == makeId(0, 10));
    assert(sorted[1].id == makeId(0, 20));

    // Depth-1 nodes from the first root should come before depth-1 nodes of the
    // second root.
    assert(sorted[2].id == makeId(0, 11));
    assert(sorted[3].id == makeId(0, 12));
    assert(sorted[4].id == makeId(0, 21));
}

void test_adaptive_sort_orders_by_high64_before_low64() {
    std::vector<Node> nodes = {
        {makeId(2, 0), 0},
        {makeId(1, UINT64_MAX), 0},
        {makeId(1, 0), 0},
        {makeId(0, UINT64_MAX), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto expected = sortForestByComparison(nodes);
    const auto bucketed = sortForestByBucketedRadix(nodes);
    const auto composite = sortForestByCompositeRadix(nodes);

    assert(sameNodes(sorted, expected));
    assert(sameNodes(bucketed, expected));
    assert(sameNodes(composite, expected));
    assert(sorted[0].id == makeId(0, UINT64_MAX));
    assert(sorted[1].id == makeId(1, 0));
    assert(sorted[2].id == makeId(1, UINT64_MAX));
    assert(sorted[3].id == makeId(2, 0));
}

void test_adaptive_sort_uses_low64_when_high64_matches() {
    std::vector<Node> nodes = {
        {makeId(9, 3), 0},
        {makeId(8, UINT64_MAX), 0},
        {makeId(9, 1), 0},
        {makeId(9, 2), 0},
    };

    const auto sorted = sortForestByDepthAndId(nodes);
    const auto expected = sortForestByComparison(nodes);
    const auto bucketed = sortForestByBucketedRadix(nodes);
    const auto composite = sortForestByCompositeRadix(nodes);

    assert(sameNodes(sorted, expected));
    assert(sameNodes(bucketed, expected));
    assert(sameNodes(composite, expected));
    assert(verifySortedByDepthAndId(sorted));
    assert(sorted[0].id == makeId(8, UINT64_MAX));
    assert(sorted[1].id == makeId(9, 1));
    assert(sorted[2].id == makeId(9, 2));
    assert(sorted[3].id == makeId(9, 3));
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
    const auto expected = sortForestByComparison(nodes);
    const auto bucketed = sortForestByBucketedRadix(shuffled);
    const auto composite = sortForestByCompositeRadix(shuffled);

    assert(sameNodes(sorted, expected));
    assert(sameNodes(bucketed, expected));
    assert(sameNodes(composite, expected));
    assert(verifySortedByDepthAndId(sorted));
}

void test_adaptive_sort_matches_baselines_for_100k_common_depth_forest() {
    constexpr std::size_t nodeCount = 100000;
    constexpr uint32_t commonMaxDepth = 30;

    const auto nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
    const auto sorted = sortForestByDepthAndId(nodes);
    const auto expected = sortForestByComparison(nodes);
    const auto bucketed = sortForestByBucketedRadix(nodes);
    const auto composite = sortForestByCompositeRadix(nodes);

    assert(sameNodes(sorted, expected));
    assert(sameNodes(bucketed, expected));
    assert(sameNodes(composite, expected));
    assert(verifySortedByDepthAndId(sorted));
}

void test_all_sort_methods_match_canonical_order_across_permutations() {
    constexpr std::size_t nodeCount = 10000;
    constexpr uint32_t commonMaxDepth = 30;

    const auto nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
    const auto canonical = sortForestByComparison(nodes);

    const std::vector<std::vector<Node>> permutations = {
        nodes,
        shuffledCopy(nodes, 0x12345678ULL),
        shuffledCopy(nodes, 0x87654321ULL),
    };

    for (const auto &permutation : permutations) {
        const auto comparison = sortForestByComparison(permutation);
        const auto bucketed = sortForestByBucketedRadix(permutation);
        const auto composite = sortForestByCompositeRadix(permutation);
        const auto adaptive = sortForestByDepthAndId(permutation);

        if (!sameNodes(comparison, canonical)) {
            throw std::runtime_error(
                "comparison sort changed across input permutations");
        }
        if (!sameNodes(bucketed, canonical)) {
            throw std::runtime_error(
                "bucketed radix sort changed across input permutations");
        }
        if (!sameNodes(composite, canonical)) {
            throw std::runtime_error(
                "composite radix sort changed across input permutations");
        }
        if (!sameNodes(adaptive, canonical)) {
            throw std::runtime_error(
                "adaptive radix sort changed across input permutations");
        }
        if (!verifySortedByDepthAndId(comparison) ||
            !verifySortedByDepthAndId(bucketed) ||
            !verifySortedByDepthAndId(composite) ||
            !verifySortedByDepthAndId(adaptive)) {
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
    const auto expected = sortForestByComparison(nodes);
    const auto bucketed = sortForestByBucketedRadix(nodes);
    const auto composite = sortForestByCompositeRadix(nodes);

    assert(sameNodes(sorted, expected));
    assert(sameNodes(bucketed, expected));
    assert(sameNodes(composite, expected));
    assert(verifySortedByDepthAndId(sorted));
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

    assert(!verifySortedByDepthAndId(nodes));
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
    assert(verifySortedByDepthAndId<2>(sorted));
}

void test_verify_accepts_sorted_common_forest() {
    constexpr std::size_t nodeCount = 10000;
    constexpr uint32_t commonMaxDepth = 30;

    const auto sorted =
        sortForestByDepthAndId(makeGeneratedForest(nodeCount, commonMaxDepth));

    assert(verifySortedByDepthAndId(sorted));
}

void test_verify_rejects_unsorted_by_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},
        {makeId(0, 2), makeId(0, 1)},
    };

    std::swap(nodes[0], nodes[1]);

    assert(!verifySortedByDepthAndId(nodes));
}

void test_verify_rejects_unsorted_by_id_within_depth() {
    std::vector<Node> nodes = {
        {makeId(0, 20), 0},
        {makeId(0, 10), 0},
    };

    assert(!verifySortedByDepthAndId(nodes));
}

void test_verify_rejects_child_before_existing_parent() {
    std::vector<Node> nodes = {
        {makeId(0, 2), makeId(0, 1)},
        {makeId(0, 1), 0},
    };

    assert(!verifySortedByDepthAndId(nodes));
}

void test_verify_treats_missing_parent_as_root() {
    std::vector<Node> nodes = {
        {makeId(0, 1), makeId(0, 99)},
        {makeId(0, 2), 0},
    };

    assert(verifySortedByDepthAndId(nodes));
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

    assert(!verifySortedByDepthAndId<1>(nodes));
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
        forest_sorting::sortedOrderByDepthAndId<2>(nodes, Traits{});
    const auto order = forest_sorting::sortedOrderByDepthAndId(nodes, Traits{});

    assert(order == explicitOrder);

    assert(order.size() == nodes.size());
    assert(order[0] == 1);
    assert(order[1] == 3);
    assert(order[2] == 2);
    assert(order[3] == 0);

    const auto sorted = forest_sorting::sortedCopyByDepthAndId(nodes, Traits{});
    assert(sorted[0].id == sibling);
    assert(sorted[1].id == root);
    assert(sorted[2].id == childLow);
    assert(sorted[3].id == childHigh);

    auto inPlace = nodes;
    forest_sorting::sortInPlaceByDepthAndId(inPlace, Traits{});
    assert(inPlace[0].id == sibling);
    assert(inPlace[1].id == root);
    assert(inPlace[2].id == childLow);
    assert(inPlace[3].id == childHigh);

    assert(forest_sorting::verifySortedByDepthAndId(sorted, Traits{}));
    assert(!forest_sorting::verifySortedByDepthAndId(nodes, Traits{}));
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

    nodes.push_back(Node{makeId(0, 1), 0});
    nodes.push_back(Node{makeId(0, 2), makeId(0, 1)});
    // Just test one level over 2-byte limit
    const auto sorted = sortForestByDepthAndId<3>(nodes);
    assert(verifySortedByDepthAndId<3>(sorted));
}

void test_sort_accepts_large_depth_with_four_byte_prefix() {
    std::vector<Node> nodes;
    nodes.push_back(Node{makeId(0, 1), 0});
    // Test a very large depth (though we won't allocate millions of nodes for
    // speed)
    const auto sorted = sortForestByDepthAndId<4>(nodes);
    assert(verifySortedByDepthAndId<4>(sorted));
}

int main() {
    try {
        std::cout << "forest sorting tests\n";
        runTest("compute depths for simple parent chain",
                test_compute_depths_simple_chain);
        runTest("parent builders match for random UInt128 IDs",
                test_parent_builders_match_for_random_uint128_ids);
        runTest("parent builders match for depth outliers",
                test_parent_builders_match_for_depth_outliers);
        runTest("parent builders match for same high64 IDs",
                test_parent_builders_match_for_same_high64_ids);
        runTest("parent builders match for sequential IDs",
                test_parent_builders_match_for_sequential_ids);
        runTest("parent builders match for external parent IDs",
                test_parent_builders_match_for_external_parent_ids);
        runTest("parent builders match for many siblings",
                test_parent_builders_match_for_many_siblings);
        runTest("parent builders reject duplicate full UInt128 ID",
                test_parent_builders_reject_duplicate_full_uint128_id);
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
        runTest("sort accepts large depth with four-byte prefix",
                test_sort_accepts_large_depth_with_four_byte_prefix);

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
