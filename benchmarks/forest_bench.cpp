#include "../tests/parent_index_baselines.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

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
#include <string>
#include <string_view>
#include <vector>

using forest_sorting::Node;
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

constexpr int kDatasetColumnWidth = 24;
constexpr int kCountColumnWidth = 12;
constexpr int kNameColumnWidth = 16;
constexpr int kTimingColumnWidth = 14;
constexpr int kTimingValueWidth = 14;

enum class OutputFormat : uint8_t {
    Table,
    Csv,
    Tsv,
    Json,
};

enum class DatasetKind : uint8_t {
    Random,
    Outliers,
    SameHigh64,
    Sequential,
    ExternalParents,
    Siblings,
};

enum class ParentKind : uint8_t {
    Unordered,
    Flat,
    Control,
    Radix,
};

enum class SortKind : uint8_t {
    Comparison,
    BucketLsd,
    CompositeLsd,
    AdaptiveMsd,
};

struct Options {
    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> sizes = {10000, 100000};
    std::vector<DatasetKind> datasets = {
        DatasetKind::Random,          DatasetKind::Outliers,
        DatasetKind::SameHigh64,      DatasetKind::Sequential,
        DatasetKind::ExternalParents, DatasetKind::Siblings,
    };
    std::vector<ParentKind> parents = {
        ParentKind::Unordered,
        ParentKind::Flat,
        ParentKind::Control,
        ParentKind::Radix,
    };
    std::vector<SortKind> sorts = {
        SortKind::Comparison,
        SortKind::BucketLsd,
        SortKind::CompositeLsd,
        SortKind::AdaptiveMsd,
    };
    int iterations = 1;
    bool help = false;
};

struct BenchmarkResult {
    std::string dataset;
    std::size_t nodeCount = 0;
    std::string parentBuilder;
    std::string sortAlgorithm;
    double parentMs = 0.0;
    double sortMs = 0.0;
    double verifyMs = 0.0;
    std::string status = "ok";
};

using forest_sorting::detail::buildParentIndexControlByteFlatHash;
using forest_sorting::detail::buildParentIndexFlatHash;
using forest_sorting::detail::buildParentIndexRadixJoin;
using forest_sorting::detail::depthByte;
using forest_sorting::detail::radix_bucket_count;
using forest_sorting::detail::radixPass;
using forest_sorting::detail::sortOrderByDepthAndId;

std::vector<std::size_t>
buildParentIndexForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexControlByteFlatHash(nodes, UInt128NodeTraits{});
}

std::vector<uint32_t>
computeDepthsForUInt128(const std::vector<Node> &nodes,
                        const std::vector<std::size_t> &parentIndex) {
    return forest_sorting::detail::computeDepths(nodes, parentIndex,
                                                 UInt128NodeTraits{});
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

std::vector<Node>
makeGeneratedForestWithHighWordCollisions(std::size_t nodeCount,
                                          uint32_t maxDepth) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    constexpr uint64_t sharedHighWord = 0x123456789abcdef0ULL;
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

        const uint64_t low = static_cast<uint64_t>(nodeCount - nodeIdx);
        nodes.push_back(Node{makeId(sharedHighWord, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    return shuffledCopy(nodes, 0xfeedfaceULL);
}

std::vector<Node> makeSequentialIdForest(std::size_t nodeCount,
                                         uint32_t maxDepth) {
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

        nodes.push_back(
            Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
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

std::string_view datasetName(DatasetKind datasetKind) {
    switch (datasetKind) {
    case DatasetKind::Random:
        return "random";
    case DatasetKind::Outliers:
        return "outliers";
    case DatasetKind::SameHigh64:
        return "same-high64";
    case DatasetKind::Sequential:
        return "sequential";
    case DatasetKind::ExternalParents:
        return "external-parents";
    case DatasetKind::Siblings:
        return "siblings";
    }
    return "unknown";
}

std::string_view parentName(ParentKind parentKind) {
    switch (parentKind) {
    case ParentKind::Unordered:
        return "unordered";
    case ParentKind::Flat:
        return "flat";
    case ParentKind::Control:
        return "control";
    case ParentKind::Radix:
        return "radix";
    }
    return "unknown";
}

std::string_view sortName(SortKind sortKind) {
    switch (sortKind) {
    case SortKind::Comparison:
        return "comparison";
    case SortKind::BucketLsd:
        return "bucket-lsd";
    case SortKind::CompositeLsd:
        return "composite-lsd";
    case SortKind::AdaptiveMsd:
        return "adaptive-msd";
    }
    return "unknown";
}

std::vector<Node> makeDataset(DatasetKind datasetKind, std::size_t nodeCount) {
    constexpr uint32_t commonMaxDepth = 30;
    switch (datasetKind) {
    case DatasetKind::Random:
        return makeGeneratedForest(nodeCount, commonMaxDepth);
    case DatasetKind::Outliers:
        return makeGeneratedForestWithOutliers(nodeCount, commonMaxDepth);
    case DatasetKind::SameHigh64:
        return makeGeneratedForestWithHighWordCollisions(nodeCount,
                                                         commonMaxDepth);
    case DatasetKind::Sequential:
        return makeSequentialIdForest(nodeCount, commonMaxDepth);
    case DatasetKind::ExternalParents:
        return makeManyExternalParentForest(nodeCount);
    case DatasetKind::Siblings:
        return makeManySiblingsForest(nodeCount);
    }
    throw std::runtime_error("unknown dataset");
}

std::vector<std::size_t>
buildParentIndexForKind(ParentKind parentKind, const std::vector<Node> &nodes) {
    switch (parentKind) {
    case ParentKind::Unordered:
        return buildParentIndexStdUnorderedMap(nodes);
    case ParentKind::Flat:
        return buildParentIndexFlatHash(nodes, UInt128NodeTraits{});
    case ParentKind::Control:
        return buildParentIndexControlByteFlatHash(nodes, UInt128NodeTraits{});
    case ParentKind::Radix:
        return buildParentIndexRadixJoin(nodes, UInt128NodeTraits{});
    }
    throw std::runtime_error("unknown parent builder");
}

std::vector<Node>
sortForestByComparisonWithParent(const std::vector<Node> &nodes,
                                 const std::vector<std::size_t> &parentIndex) {
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

std::vector<Node> sortForestByComparison(const std::vector<Node> &nodes) {
    return sortForestByComparisonWithParent(nodes,
                                            buildParentIndexForUInt128(nodes));
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

std::vector<Node> sortForestByBucketedRadixWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
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

std::vector<Node> sortForestByBucketedRadix(const std::vector<Node> &nodes) {
    return sortForestByBucketedRadixWithParent(
        nodes, buildParentIndexForUInt128(nodes));
}

std::vector<Node> sortForestByCompositeRadixWithParent(
    const std::vector<Node> &nodes,
    const std::vector<std::size_t> &parentIndex) {
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

std::vector<Node> sortForestByCompositeRadix(const std::vector<Node> &nodes) {
    return sortForestByCompositeRadixWithParent(
        nodes, buildParentIndexForUInt128(nodes));
}

std::vector<Node>
sortForestByAdaptiveWithParent(const std::vector<Node> &nodes,
                               const std::vector<std::size_t> &parentIndex) {
    const std::size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const auto depths = computeDepthsForUInt128(nodes, parentIndex);

    std::vector<std::size_t> order(nodeCount);
    std::iota(order.begin(), order.end(), 0);
    for (uint32_t depth : depths) {
        if (depth > forest_sorting::detail::maxDepthForPrefix<2>()) {
            throw std::runtime_error(
                "forest depth exceeds sortable depth limit");
        }
    }

    std::vector<std::size_t> scratch(nodeCount);
    uint32_t maxDepth = 0;
    for (uint32_t depth : depths) {
        maxDepth = std::max(maxDepth, depth);
    }
    sortOrderByDepthAndId(order, scratch, nodes, UInt128NodeTraits{}, depths,
                          maxDepth);

    std::vector<Node> sorted;
    sorted.reserve(nodeCount);
    for (std::size_t nodeIndex : order) {
        sorted.push_back(nodes[nodeIndex]);
    }

    return sorted;
}

std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex) {
    switch (sortKind) {
    case SortKind::Comparison:
        return sortForestByComparisonWithParent(nodes, parentIndex);
    case SortKind::BucketLsd:
        return sortForestByBucketedRadixWithParent(nodes, parentIndex);
    case SortKind::CompositeLsd:
        return sortForestByCompositeRadixWithParent(nodes, parentIndex);
    case SortKind::AdaptiveMsd:
        return sortForestByAdaptiveWithParent(nodes, parentIndex);
    }
    throw std::runtime_error("unknown sort algorithm");
}

UInt128 checksumIds(const std::vector<Node> &nodes) {
    UInt128 checksum = 0;
    for (const auto &node : nodes) {
        checksum ^= node.id;
        checksum = (checksum << 1) | (checksum >> 127);
    }
    return checksum;
}

double timeParentBuildMs(const std::vector<Node> &nodes, ParentKind parentKind,
                         std::vector<std::size_t> &parentIndex) {
    const auto start = std::chrono::steady_clock::now();
    parentIndex = buildParentIndexForKind(parentKind, nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double timeSortMs(const std::vector<Node> &nodes, ParentKind parentKind,
                  SortKind sortKind, std::vector<Node> &sorted,
                  UInt128 &checksum) {
    const auto start = std::chrono::steady_clock::now();
    const auto parentIndex = buildParentIndexForKind(parentKind, nodes);
    sorted = sortForestForKind(sortKind, nodes, parentIndex);
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

void appendAllDatasets(std::vector<DatasetKind> &datasets) {
    datasets = {DatasetKind::Random,          DatasetKind::Outliers,
                DatasetKind::SameHigh64,      DatasetKind::Sequential,
                DatasetKind::ExternalParents, DatasetKind::Siblings};
}

void appendAllParents(std::vector<ParentKind> &parents) {
    parents = {ParentKind::Unordered, ParentKind::Flat, ParentKind::Control,
               ParentKind::Radix};
}

void appendAllSorts(std::vector<SortKind> &sorts) {
    sorts = {SortKind::Comparison, SortKind::BucketLsd, SortKind::CompositeLsd,
             SortKind::AdaptiveMsd};
}

OutputFormat parseFormat(std::string_view value) {
    if (value == "table") {
        return OutputFormat::Table;
    }
    if (value == "csv") {
        return OutputFormat::Csv;
    }
    if (value == "tsv") {
        return OutputFormat::Tsv;
    }
    if (value == "json") {
        return OutputFormat::Json;
    }
    throw std::runtime_error("unknown format: " + std::string(value));
}

DatasetKind parseDataset(std::string_view value) {
    if (value == "random") {
        return DatasetKind::Random;
    }
    if (value == "outliers") {
        return DatasetKind::Outliers;
    }
    if (value == "same-high64") {
        return DatasetKind::SameHigh64;
    }
    if (value == "sequential") {
        return DatasetKind::Sequential;
    }
    if (value == "external-parents") {
        return DatasetKind::ExternalParents;
    }
    if (value == "siblings") {
        return DatasetKind::Siblings;
    }
    throw std::runtime_error("unknown dataset: " + std::string(value));
}

ParentKind parseParent(std::string_view value) {
    if (value == "unordered") {
        return ParentKind::Unordered;
    }
    if (value == "flat") {
        return ParentKind::Flat;
    }
    if (value == "control") {
        return ParentKind::Control;
    }
    if (value == "radix") {
        return ParentKind::Radix;
    }
    throw std::runtime_error("unknown parent builder: " + std::string(value));
}

SortKind parseSort(std::string_view value) {
    if (value == "comparison") {
        return SortKind::Comparison;
    }
    if (value == "bucket-lsd") {
        return SortKind::BucketLsd;
    }
    if (value == "composite-lsd") {
        return SortKind::CompositeLsd;
    }
    if (value == "adaptive-msd") {
        return SortKind::AdaptiveMsd;
    }
    throw std::runtime_error("unknown sort algorithm: " + std::string(value));
}

std::size_t parseSize(std::string_view value) {
    return static_cast<std::size_t>(std::stoull(std::string(value)));
}

int parsePositiveInt(std::string_view value, const char *optionName) {
    const int parsed = std::stoi(std::string(value));
    if (parsed < 1) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be at least 1");
    }
    return parsed;
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customSizes = false;
    bool customDatasets = false;
    bool customParents = false;
    bool customSorts = false;

    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string_view option = argv[argIndex];
        if (option == "--help") {
            options.help = true;
            return options;
        }

        if (argIndex + 1 >= argc) {
            throw std::runtime_error("missing value for " +
                                     std::string(option));
        }
        const std::string_view value = argv[++argIndex];

        if (option == "--format") {
            options.format = parseFormat(value);
        } else if (option == "--size") {
            if (!customSizes) {
                options.sizes.clear();
                customSizes = true;
            }
            options.sizes.push_back(parseSize(value));
        } else if (option == "--dataset") {
            if (!customDatasets) {
                options.datasets.clear();
                customDatasets = true;
            }
            if (value == "all") {
                appendAllDatasets(options.datasets);
            } else {
                options.datasets.push_back(parseDataset(value));
            }
        } else if (option == "--parent") {
            if (!customParents) {
                options.parents.clear();
                customParents = true;
            }
            if (value == "all") {
                appendAllParents(options.parents);
            } else {
                options.parents.push_back(parseParent(value));
            }
        } else if (option == "--sort") {
            if (!customSorts) {
                options.sorts.clear();
                customSorts = true;
            }
            if (value == "all") {
                appendAllSorts(options.sorts);
            } else {
                options.sorts.push_back(parseSort(value));
            }
        } else if (option == "--iterations") {
            options.iterations = parsePositiveInt(value, "--iterations");
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    return options;
}

void printHelp() {
    std::cout
        << "usage: forest-sorting-bench [options]\n"
        << "\n"
        << "options:\n"
        << "  --format table|csv|tsv|json\n"
        << "  --size N                         repeatable\n"
        << "  --dataset "
           "random|outliers|same-high64|sequential|external-parents|siblings|"
           "all\n"
        << "  --parent unordered|flat|control|radix|all\n"
        << "  --sort comparison|bucket-lsd|composite-lsd|adaptive-msd|all\n"
        << "  --iterations N\n"
        << "  --help\n";
}

std::string appendStatus(std::string status, std::string_view marker) {
    if (status == "ok") {
        return std::string(marker);
    }
    status += "|";
    status += marker;
    return status;
}

BenchmarkResult runBenchmarkRow(const std::vector<Node> &nodes,
                                DatasetKind datasetKind, std::size_t nodeCount,
                                ParentKind parentKind, SortKind sortKind,
                                int iterations,
                                const std::vector<std::size_t> &expectedParent,
                                UInt128 expectedChecksum) {
    BenchmarkResult result;
    result.dataset = std::string(datasetName(datasetKind));
    result.nodeCount = nodeCount;
    result.parentBuilder = std::string(parentName(parentKind));
    result.sortAlgorithm = std::string(sortName(sortKind));

    std::vector<std::size_t> parentIndex;
    std::vector<Node> sorted;
    UInt128 checksum = 0;
    bool verified = false;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        result.parentMs += timeParentBuildMs(nodes, parentKind, parentIndex);
        result.sortMs +=
            timeSortMs(nodes, parentKind, sortKind, sorted, checksum);
        result.verifyMs += timeVerifyMs(sorted, verified);
    }

    result.parentMs /= static_cast<double>(iterations);
    result.sortMs /= static_cast<double>(iterations);
    result.verifyMs /= static_cast<double>(iterations);

    if (parentIndex != expectedParent) {
        result.status = appendStatus(result.status, "parent-mismatch");
    }
    if (checksum != expectedChecksum) {
        result.status = appendStatus(result.status, "checksum-mismatch");
    }
    if (!verified) {
        result.status = appendStatus(result.status, "verify-failed");
    }

    return result;
}

std::vector<BenchmarkResult> runBenchmarks(const Options &options) {
    std::vector<BenchmarkResult> results;

    for (std::size_t nodeCount : options.sizes) {
        for (DatasetKind datasetKind : options.datasets) {
            const auto nodes = makeDataset(datasetKind, nodeCount);
            const auto expectedParent =
                buildParentIndexControlByteFlatHash(nodes, UInt128NodeTraits{});
            const auto canonicalSorted =
                sortForestForKind(SortKind::Comparison, nodes, expectedParent);
            const UInt128 expectedChecksum = checksumIds(canonicalSorted);

            for (ParentKind parentKind : options.parents) {
                for (SortKind sortKind : options.sorts) {
                    results.push_back(runBenchmarkRow(
                        nodes, datasetKind, nodeCount, parentKind, sortKind,
                        options.iterations, expectedParent, expectedChecksum));
                }
            }
        }
    }

    return results;
}

std::string csvEscape(std::string_view value) {
    bool needsQuotes = false;
    std::string escaped;
    for (char character : value) {
        if (character == '"' || character == ',' || character == '\n' ||
            character == '\r') {
            needsQuotes = true;
        }
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    if (!needsQuotes) {
        return escaped;
    }
    return "\"" + escaped + "\"";
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    for (char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

void printBenchmarkHeader() {
    std::cout << std::left << std::setw(kDatasetColumnWidth) << "dataset"
              << std::right << "  " << std::setw(kCountColumnWidth)
              << "node_count"
              << "  " << std::setw(kNameColumnWidth) << "parent_builder"
              << "  " << std::setw(kNameColumnWidth) << "sort_algorithm"
              << "  " << std::setw(kTimingColumnWidth) << "parent_ms"
              << "  " << std::setw(kTimingColumnWidth) << "sort_ms"
              << "  " << std::setw(kTimingColumnWidth) << "verify_ms"
              << "  status\n";
}

void printTiming(double milliseconds) {
    std::cout << "  " << std::setw(kTimingValueWidth) << milliseconds << " ms";
}

void printTable(const std::vector<BenchmarkResult> &results) {
    printBenchmarkHeader();
    for (const BenchmarkResult &result : results) {
        std::cout << std::left << std::setw(kDatasetColumnWidth)
                  << result.dataset << std::right << "  "
                  << std::setw(kCountColumnWidth) << result.nodeCount << "  "
                  << std::setw(kNameColumnWidth) << result.parentBuilder << "  "
                  << std::setw(kNameColumnWidth) << result.sortAlgorithm;
        printTiming(result.parentMs);
        printTiming(result.sortMs);
        printTiming(result.verifyMs);
        std::cout << "  " << result.status << "\n";
    }
}

void printDelimited(const std::vector<BenchmarkResult> &results,
                    char delimiter) {
    const bool csv = delimiter == ',';
    std::cout << "dataset" << delimiter << "node_count" << delimiter
              << "parent_builder" << delimiter << "sort_algorithm" << delimiter
              << "parent_ms" << delimiter << "sort_ms" << delimiter
              << "verify_ms" << delimiter << "status\n";
    for (const BenchmarkResult &result : results) {
        if (csv) {
            std::cout << csvEscape(result.dataset) << delimiter;
        } else {
            std::cout << result.dataset << delimiter;
        }
        std::cout << result.nodeCount << delimiter;
        if (csv) {
            std::cout << csvEscape(result.parentBuilder) << delimiter
                      << csvEscape(result.sortAlgorithm) << delimiter;
        } else {
            std::cout << result.parentBuilder << delimiter
                      << result.sortAlgorithm << delimiter;
        }
        std::cout << result.parentMs << delimiter << result.sortMs << delimiter
                  << result.verifyMs << delimiter;
        if (csv) {
            std::cout << csvEscape(result.status);
        } else {
            std::cout << result.status;
        }
        std::cout << "\n";
    }
}

void printJson(const std::vector<BenchmarkResult> &results, int iterations) {
    std::cout << "{\n  \"iterations\": " << iterations
              << ",\n  \"results\": [\n";
    for (std::size_t resultIndex = 0; resultIndex < results.size();
         ++resultIndex) {
        const BenchmarkResult &result = results[resultIndex];
        std::cout << "    {"
                  << "\"dataset\": \"" << jsonEscape(result.dataset)
                  << "\", \"node_count\": " << result.nodeCount
                  << ", \"parent_builder\": \""
                  << jsonEscape(result.parentBuilder)
                  << "\", \"sort_algorithm\": \""
                  << jsonEscape(result.sortAlgorithm)
                  << "\", \"parent_ms\": " << result.parentMs
                  << ", \"sort_ms\": " << result.sortMs
                  << ", \"verify_ms\": " << result.verifyMs
                  << ", \"status\": \"" << jsonEscape(result.status) << "\"}";
        if (resultIndex + 1 < results.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

void printResults(const std::vector<BenchmarkResult> &results,
                  const Options &options) {
    switch (options.format) {
    case OutputFormat::Table:
        printTable(results);
        break;
    case OutputFormat::Csv:
        printDelimited(results, ',');
        break;
    case OutputFormat::Tsv:
        printDelimited(results, '\t');
        break;
    case OutputFormat::Json:
        printJson(results, options.iterations);
        break;
    }
}

int main(int argc, char **argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.help) {
            printHelp();
            return 0;
        }
        const auto results = runBenchmarks(options);
        printResults(results, options);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-bench failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-bench failed: unknown exception\n";
        return 1;
    }
}
