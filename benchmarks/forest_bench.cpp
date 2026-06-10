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
constexpr std::size_t kDepthByteCount = 2;
constexpr std::size_t kRadixBits = 8;
constexpr std::size_t kRadixBucketCount = 256;
constexpr int kDatasetColumnWidth = 28;
constexpr int kTimingColumnWidth = 15;
constexpr int kTimingValueWidth = 14;

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

std::vector<Node>
makeGeneratedForestWithHighWordCollisions(std::size_t nodeCount,
                                          uint32_t maxDepth) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    constexpr uint64_t sharedHighWord = 0x123456789abcdef0ULL;
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

        const uint64_t low = static_cast<uint64_t>(nodeCount - i);
        nodes.push_back(Node{makeId(sharedHighWord, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = i;
    }

    return shuffledCopy(nodes, 0xfeedfaceULL);
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

double timeVerifyMs(const std::vector<Node> &nodes, bool &verified) {
    const auto start = std::chrono::steady_clock::now();
    verified = verifySortedByDepthAndId(nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void printBenchmarkHeader() {
    std::cout << std::left << std::setw(kDatasetColumnWidth) << "dataset"
              << std::right << "  " << std::setw(kTimingColumnWidth)
              << "cmp_sort_ms"
              << "  " << std::setw(kTimingColumnWidth) << "bucket_lsd_ms"
              << "  " << std::setw(kTimingColumnWidth) << "composite_lsd_ms"
              << "  " << std::setw(kTimingColumnWidth) << "adaptive_msd_ms"
              << "  " << std::setw(kTimingColumnWidth) << "verify_ms"
              << "  status\n";
}

void printTiming(double milliseconds) {
    std::cout << "  " << std::setw(kTimingValueWidth) << milliseconds << " ms";
}

void runBenchmark(const std::vector<Node> &nodes, const char *label) {
    UInt128 comparisonChecksum = 0;
    const double comparisonMs =
        timeSortMs(nodes, sortForestByComparison, comparisonChecksum);

    UInt128 bucketedChecksum = 0;
    const double bucketedMs =
        timeSortMs(nodes, sortForestByBucketedRadix, bucketedChecksum);

    UInt128 compositeChecksum = 0;
    const double compositeMs =
        timeSortMs(nodes, sortForestByCompositeRadix, compositeChecksum);

    UInt128 adaptiveChecksum = 0;
    const double adaptiveMs =
        timeSortMs(nodes, sortForestByDepthAndId, adaptiveChecksum);
    const auto adaptiveSorted = sortForestByDepthAndId(nodes);
    bool verified = false;
    const double verifyMs = timeVerifyMs(adaptiveSorted, verified);

    std::cout << std::left << std::setw(kDatasetColumnWidth) << label
              << std::right;
    printTiming(comparisonMs);
    printTiming(bucketedMs);
    printTiming(compositeMs);
    printTiming(adaptiveMs);
    printTiming(verifyMs);

    bool passed = true;
    if (comparisonChecksum != bucketedChecksum ||
        comparisonChecksum != compositeChecksum ||
        comparisonChecksum != adaptiveChecksum) {
        passed = false;
        std::cout << "  checksum-mismatch";
    }
    if (!verified) {
        passed = false;
        std::cout << "  verify-failed";
    }
    if (passed) {
        std::cout << "  ok";
    }
    std::cout << "\n";
}

int main() {
    try {
        constexpr uint32_t commonMaxDepth = 30;
        std::cout << "forest sorting benchmark\n";
        printBenchmarkHeader();
        runBenchmark(makeGeneratedForest(10000, commonMaxDepth), "10000 nodes");
        runBenchmark(makeGeneratedForestWithOutliers(10000, commonMaxDepth),
                     "10000 nodes + outliers");
        runBenchmark(
            makeGeneratedForestWithHighWordCollisions(10000, commonMaxDepth),
            "10000 nodes same high64");
        runBenchmark(makeGeneratedForest(100000, commonMaxDepth),
                     "100000 nodes");
        runBenchmark(makeGeneratedForestWithOutliers(100000, commonMaxDepth),
                     "100000 nodes + outliers");
        runBenchmark(
            makeGeneratedForestWithHighWordCollisions(100000, commonMaxDepth),
            "100000 nodes same high64");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-bench failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-bench failed: unknown exception\n";
        return 1;
    }
}
