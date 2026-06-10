#include "forest.hpp"

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
#include <vector>

UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

constexpr std::size_t kUInt128ByteCount = 16;
constexpr std::size_t kDepthByteCount = 2;
constexpr std::size_t kRadixBits = 8;
constexpr std::size_t kRadixBucketCount = 256;

uint8_t idByte(UInt128 value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

uint8_t depthByte(uint32_t value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
}

template <typename DigitForIndex>
void radixPass(std::vector<std::size_t> &order,
               std::vector<std::size_t> &scratch, DigitForIndex digitForIndex) {
    std::array<std::size_t, kRadixBucketCount> counts{};
    for (std::size_t nodeIndex : order) {
        ++counts[digitForIndex(nodeIndex)];
    }

    std::size_t offset = 0;
    for (std::size_t &count : counts) {
        const std::size_t bucketSize = count;
        count = offset;
        offset += bucketSize;
    }

    for (std::size_t nodeIndex : order) {
        const uint8_t digit = digitForIndex(nodeIndex);
        scratch[counts[digit]] = nodeIndex;
        ++counts[digit];
    }

    order.swap(scratch);
}

bool sameNodes(const std::vector<Node> &lhs, const std::vector<Node> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].id != rhs[i].id || lhs[i].parentId != rhs[i].parentId) {
            return false;
        }
    }

    return true;
}

std::vector<Node> sortForestByComparison(const std::vector<Node> &nodes) {
    const auto parentIndex = buildParentIndex(nodes);
    const auto depths = computeDepths(nodes, parentIndex);

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
    for (std::size_t byteIndex = 0; byteIndex < kUInt128ByteCount;
         ++byteIndex) {
        std::array<std::size_t, kRadixBucketCount> counts{};
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
    const auto parentIndex = buildParentIndex(nodes);
    const auto depths = computeDepths(nodes, parentIndex);

    uint32_t maxDepth = 0;
    for (uint32_t depth : depths) {
        if (depth > kMaxSortableDepth) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
        maxDepth = std::max(maxDepth, depth);
    }

    std::vector<std::vector<std::size_t>> buckets(
        static_cast<std::size_t>(maxDepth) + 1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        buckets[depths[i]].push_back(i);
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
    const auto parentIndex = buildParentIndex(nodes);
    const auto depths = computeDepths(nodes, parentIndex);

    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    for (uint32_t depth : depths) {
        if (depth > kMaxSortableDepth) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
    }

    std::vector<std::size_t> scratch(order.size());
    for (std::size_t byteIndex = 0; byteIndex < kUInt128ByteCount;
         ++byteIndex) {
        radixPass(order, scratch, [&](std::size_t nodeIndex) {
            return idByte(nodes[nodeIndex].id, byteIndex);
        });
    }
    for (std::size_t byteIndex = 0; byteIndex < kDepthByteCount; ++byteIndex) {
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
        static_cast<std::size_t>(maxDepth) + 1, kNoParent);
    for (std::size_t i = 0; i < nodeCount; ++i) {
        uint32_t targetDepth =
            static_cast<uint32_t>(i % (static_cast<std::size_t>(maxDepth) + 1));
        UInt128 parentId = 0;
        if (targetDepth > 0 &&
            lastIndexAtDepth[static_cast<std::size_t>(targetDepth - 1)] !=
                kNoParent) {
            parentId = nodes[lastIndexAtDepth[static_cast<std::size_t>(
                                 targetDepth - 1)]]
                           .id;
        } else {
            targetDepth = 0;
        }

        const uint64_t high = rng();
        const uint64_t low = static_cast<uint64_t>(i) + 1ULL;
        nodes.push_back(Node{makeId(high, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = i;
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
    appendDeepChain(nodes, kMaxSortableDepth, 0x3000ULL);
    return shuffledCopy(nodes, 0xabcdef00ULL);
}

void test_compute_depths_simple_chain() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},            // depth 0
        {makeId(0, 2), makeId(0, 1)}, // depth 1
        {makeId(0, 3), makeId(0, 2)}, // depth 2
    };

    const auto parentIndex = buildParentIndex(nodes);
    const auto depths = computeDepths(nodes, parentIndex);

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

void test_sort_orders_high_64_bits() {
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

void test_sort_handles_high_word_collisions() {
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

void test_sort_is_deterministic_for_shuffled_input() {
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

void test_large_generated_forest_matches_comparison_oracle() {
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

void test_all_sort_methods_are_permutation_deterministic() {
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

void test_generated_forest_with_deep_outliers_matches_comparison_oracle() {
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

void test_sort_rejects_duplicate_full_id() {
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

void test_verify_rejects_duplicate_full_id() {
    const UInt128 duplicateId = makeId(7, 11);
    std::vector<Node> nodes = {
        {duplicateId, 0},
        {duplicateId, 0},
    };

    assert(!verifySortedByDepthAndId(nodes));
}

void test_sort_rejects_depth_over_limit() {
    std::vector<Node> nodes;
    nodes.reserve(static_cast<std::size_t>(kMaxSortableDepth) + 2);

    nodes.push_back(Node{makeId(0, 1), 0});
    for (uint32_t depth = 1; depth <= kMaxSortableDepth + 1; ++depth) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(depth) + 1ULL),
                             makeId(0, static_cast<uint64_t>(depth))});
    }

    bool rejected = false;
    try {
        (void)sortForestByDepthAndId(nodes);
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "sort accepted a forest deeper than the limit");
    }
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

void test_verify_rejects_depth_over_limit() {
    std::vector<Node> nodes;
    nodes.reserve(static_cast<std::size_t>(kMaxSortableDepth) + 2);

    nodes.push_back(Node{makeId(0, 1), 0});
    for (uint32_t depth = 1; depth <= kMaxSortableDepth + 1; ++depth) {
        nodes.push_back(Node{makeId(0, static_cast<uint64_t>(depth) + 1ULL),
                             makeId(0, static_cast<uint64_t>(depth))});
    }

    assert(!verifySortedByDepthAndId(nodes));
}

int main() {
    try {
        test_compute_depths_simple_chain();
        test_sort_and_verify_multi_root();
        test_sort_orders_high_64_bits();
        test_sort_handles_high_word_collisions();
        test_sort_is_deterministic_for_shuffled_input();
        test_large_generated_forest_matches_comparison_oracle();
        test_all_sort_methods_are_permutation_deterministic();
        test_generated_forest_with_deep_outliers_matches_comparison_oracle();
        test_sort_rejects_duplicate_full_id();
        test_sort_rejects_depth_over_limit();
        test_verify_accepts_sorted_common_forest();
        test_verify_rejects_unsorted_by_depth();
        test_verify_rejects_unsorted_by_id_within_depth();
        test_verify_rejects_child_before_existing_parent();
        test_verify_treats_missing_parent_as_root();
        test_verify_rejects_duplicate_full_id();
        test_verify_rejects_depth_over_limit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-tests failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tests failed: unknown exception\n";
        return 1;
    }
}
