#include "forest.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <ratio>
#include <stdexcept>
#include <vector>

UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

constexpr std::size_t kUInt128ByteCount = 16;
constexpr std::size_t kRadixBits = 8;
constexpr std::size_t kRadixBucketCount = 256;

uint8_t idByte(UInt128 value, std::size_t byteIndex) noexcept {
    return static_cast<uint8_t>(value >> (byteIndex * kRadixBits));
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

UInt128 checksumIds(const std::vector<Node> &nodes) {
    UInt128 checksum = 0;
    for (const auto &node : nodes) {
        checksum ^= node.id;
        checksum = (checksum << 1) | (checksum >> 127);
    }
    return checksum;
}

template <typename Sorter>
double timeSortMs(const std::vector<Node> &nodes, Sorter sorter,
                  UInt128 &checksum) {
    const auto start = std::chrono::steady_clock::now();
    const auto sorted = sorter(nodes);
    const auto end = std::chrono::steady_clock::now();
    checksum = checksumIds(sorted);
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void runBenchmark(std::size_t nodeCount, bool includeOutliers) {
    constexpr uint32_t commonMaxDepth = 30;
    const auto nodes =
        includeOutliers
            ? makeGeneratedForestWithOutliers(nodeCount, commonMaxDepth)
            : makeGeneratedForest(nodeCount, commonMaxDepth);

    UInt128 comparisonChecksum = 0;
    const double comparisonMs =
        timeSortMs(nodes, sortForestByComparison, comparisonChecksum);

    UInt128 bucketedChecksum = 0;
    const double bucketedMs =
        timeSortMs(nodes, sortForestByBucketedRadix, bucketedChecksum);

    UInt128 compositeChecksum = 0;
    const double compositeMs =
        timeSortMs(nodes, sortForestByDepthAndId, compositeChecksum);

    std::cout << std::setw(8) << nodeCount << " nodes";
    if (includeOutliers) {
        std::cout << " + outliers";
    } else {
        std::cout << "           ";
    }
    std::cout << "  comparison " << std::setw(10) << comparisonMs
              << " ms  bucketed " << std::setw(10) << bucketedMs
              << " ms  composite " << std::setw(10) << compositeMs << " ms";
    if (comparisonChecksum != bucketedChecksum ||
        comparisonChecksum != compositeChecksum) {
        std::cout << "  checksum-mismatch";
    }
    std::cout << "\n";
}

int main() {
    try {
        std::cout << "forest sorting benchmark\n";
        runBenchmark(10000, false);
        runBenchmark(10000, true);
        runBenchmark(100000, false);
        runBenchmark(100000, true);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-bench failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-bench failed: unknown exception\n";
        return 1;
    }
}
