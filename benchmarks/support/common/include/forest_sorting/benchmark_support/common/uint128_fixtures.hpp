#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_UINT128_FIXTURES_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_UINT128_FIXTURES_HPP

#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace forest_sorting::benchmark_support {

inline uint64_t mixDeterministicUInt128Word(uint64_t value) noexcept {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
}

inline constexpr uint32_t kCommonFixtureMaxDepth = 30U;
inline constexpr uint64_t kDefaultGeneratedForestSeed = 0x5eed1234ULL;
inline constexpr uint64_t kDefaultOutlierShuffleSeed = 0xabcdef00ULL;

inline constexpr uint64_t kRandomDatasetSeedSalt = 0x200001ULL;
inline constexpr uint64_t kOutlierBaseSeedSalt = 0x100001ULL;
inline constexpr uint64_t kOutlierShuffleSeedSalt = 0x100002ULL;
inline constexpr uint64_t kSameHigh64DatasetSeedSalt = 0x200003ULL;
inline constexpr uint64_t kSameHigh32DatasetSeedSalt = 0x200004ULL;
inline constexpr uint64_t kSequentialDatasetSeedSalt = 0x200005ULL;
inline constexpr uint64_t kExternalParentsDatasetSeedSalt = 0x200006ULL;
inline constexpr uint64_t kSiblingsDatasetSeedSalt = 0x200007ULL;

using forest_sorting::makeId;

template <typename Rng> inline forest_sorting::UInt128 makeRandomId(Rng &rng) {
    const uint64_t high = static_cast<uint64_t>(rng());
    const uint64_t low = static_cast<uint64_t>(rng());
    return makeId(high, low);
}

inline constexpr uint64_t golden_ratio_64 = 0x9e3779b97f4a7c15ULL;

inline forest_sorting::UInt128 makeRandomId(uint64_t seed,
                                            std::size_t nodeIdx) {
    const uint64_t kHighSalt = golden_ratio_64;
    constexpr uint64_t kLowSalt = 0xbf58476d1ce4e5b9ULL;
    constexpr uint64_t kIndexStride = 0x94d049bb133111ebULL;

    const uint64_t indexWord = static_cast<uint64_t>(nodeIdx) + 1ULL;
    const uint64_t high = mixDeterministicUInt128Word(
        seed ^ kHighSalt ^ (indexWord * kIndexStride));
    uint64_t low = mixDeterministicUInt128Word(seed ^ kLowSalt ^
                                               (indexWord * kIndexStride));
    if (high == 0 && low == 0) {
        low = 1;
    }
    return makeId(high, low);
}

inline uint64_t mixFixtureSeed(uint32_t seed, uint64_t salt) {
    return mixDeterministicUInt128Word((static_cast<uint64_t>(seed) << 32U) ^
                                       salt);
}

template <std::size_t DepthPrefixBytes = 4>
inline std::vector<detail::DepthValue<DepthPrefixBytes>>
computeDepthsForUInt128(const std::vector<Node> &nodes,
                        const std::vector<std::size_t> &parentIndex) {
    return detail::computeDepths<DepthPrefixBytes>(nodes, parentIndex,
                                                   UInt128NodeTraits{})
        .values;
}

inline std::vector<Node> shuffledCopy(std::vector<Node> nodes, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
}

template <typename IdGenerator>
inline std::vector<Node> makeDepthLinkedForest(std::size_t nodeCount,
                                               uint32_t depthCycleMax,
                                               IdGenerator idGenerator) {
    if (nodeCount == 0) {
        return {};
    }

    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    const std::size_t selectedMaxDepth =
        std::min(static_cast<std::size_t>(depthCycleMax), nodeCount - 1);
    const std::size_t depthEntryCount = selectedMaxDepth + 1;
    std::vector<std::size_t> lastIndexAtDepth(depthEntryCount,
                                              detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        std::size_t targetDepth = nodeIdx % depthEntryCount;
        UInt128 parentId = 0;
        if (targetDepth > 0 &&
            lastIndexAtDepth[targetDepth - 1] != detail::no_parent) {
            parentId = nodes[lastIndexAtDepth[targetDepth - 1]].id;
        } else {
            targetDepth = 0;
        }

        nodes.push_back(Node{idGenerator(nodeIdx), parentId});
        lastIndexAtDepth[targetDepth] = nodeIdx;
    }

    return nodes;
}

inline std::vector<Node> makeGeneratedForest(std::size_t nodeCount,
                                             uint32_t depthCycleMax,
                                             uint64_t seed) {
    auto idGenerator = [&](std::size_t nodeIdx) {
        return makeRandomId(seed, nodeIdx);
    };

    std::vector<Node> nodes =
        makeDepthLinkedForest(nodeCount, depthCycleMax, idGenerator);
    std::mt19937_64 shuffleRng(seed);
    std::shuffle(nodes.begin(), nodes.end(), shuffleRng);
    return nodes;
}

inline std::vector<Node>
makeGeneratedForest(std::size_t nodeCount,
                    uint32_t depthCycleMax = kCommonFixtureMaxDepth) {
    return makeGeneratedForest(nodeCount, depthCycleMax,
                               kDefaultGeneratedForestSeed);
}

[[nodiscard]] constexpr uint64_t
deepChainNodeCount(uint32_t chainDepth) noexcept {
    return static_cast<uint64_t>(chainDepth) + 1U;
}

inline void appendDeepChain(std::vector<Node> &nodes, uint32_t chainDepth,
                            uint64_t idBase) {
    const uint64_t wideCount = deepChainNodeCount(chainDepth);
    if (wideCount > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("deep-chain fixture is too large");
    }
    const std::size_t count = static_cast<std::size_t>(wideCount);
    if (count > nodes.max_size() - nodes.size()) {
        throw std::length_error("deep-chain fixture is too large");
    }
    nodes.reserve(nodes.size() + count);

    UInt128 parentId = 0;
    for (std::size_t depth = 0; depth < count; ++depth) {
        const UInt128 nodeId =
            makeId(idBase, static_cast<uint64_t>(depth) + 1ULL);
        nodes.push_back(Node{nodeId, parentId});
        parentId = nodeId;
    }
}

inline constexpr std::array<std::size_t, 3> kDepthOutlierChainNodeCounts = {
    129, 513, 1025};
inline constexpr std::size_t kDepthOutlierNodeCount = 1667;
static_assert(kDepthOutlierChainNodeCounts[0] +
                      kDepthOutlierChainNodeCounts[1] +
                      kDepthOutlierChainNodeCounts[2] ==
                  kDepthOutlierNodeCount,
              "outlier chain budget must match its component chains");

inline void appendDepthOutlierChains(std::vector<Node> &nodes,
                                     std::size_t nodeBudget) {
    constexpr std::array<uint64_t, 3> kIdBases = {0x1000ULL, 0x2000ULL,
                                                  0x3000ULL};
    for (std::size_t chainIndex = 0;
         chainIndex < kDepthOutlierChainNodeCounts.size() && nodeBudget > 0;
         ++chainIndex) {
        const std::size_t chainNodeCount =
            std::min(nodeBudget, kDepthOutlierChainNodeCounts[chainIndex]);
        appendDeepChain(nodes, static_cast<uint32_t>(chainNodeCount - 1),
                        kIdBases[chainIndex]);
        nodeBudget -= chainNodeCount;
    }
}

inline std::vector<Node>
makeGeneratedForestWithOutliers(std::size_t nodeCount, uint32_t commonMaxDepth,
                                uint32_t dataSeed) {
    const std::size_t outlierNodeCount =
        std::min(nodeCount, kDepthOutlierNodeCount);
    std::vector<Node> nodes =
        makeGeneratedForest(nodeCount - outlierNodeCount, commonMaxDepth,
                            mixFixtureSeed(dataSeed, kOutlierBaseSeedSalt));
    appendDepthOutlierChains(nodes, outlierNodeCount);
    return shuffledCopy(nodes,
                        mixFixtureSeed(dataSeed, kOutlierShuffleSeedSalt));
}

inline std::vector<Node> makeGeneratedForestWithOutliers(
    std::size_t nodeCount, uint32_t commonMaxDepth = kCommonFixtureMaxDepth) {
    const std::size_t outlierNodeCount =
        std::min(nodeCount, kDepthOutlierNodeCount);
    std::vector<Node> nodes =
        makeGeneratedForest(nodeCount - outlierNodeCount, commonMaxDepth);
    appendDepthOutlierChains(nodes, outlierNodeCount);
    return shuffledCopy(nodes, kDefaultOutlierShuffleSeed);
}

inline std::vector<Node> makeGeneratedForestWithHighWordCollisions(
    std::size_t nodeCount, uint32_t depthCycleMax, uint64_t shuffleSeed) {
    constexpr uint64_t sharedHighWord = 0x123456789abcdef0ULL;
    auto idGenerator = [&](std::size_t nodeIdx) {
        const uint64_t low = static_cast<uint64_t>(nodeCount - nodeIdx);
        return makeId(sharedHighWord, low);
    };

    std::vector<Node> nodes =
        makeDepthLinkedForest(nodeCount, depthCycleMax, idGenerator);
    return shuffledCopy(nodes, shuffleSeed);
}

inline std::vector<Node> makeGeneratedForestWithHigh32Collisions(
    std::size_t nodeCount, uint32_t depthCycleMax, uint64_t shuffleSeed) {
    constexpr uint64_t sharedHigh32 = 0x12345678ULL;
    auto idGenerator = [&](std::size_t nodeIdx) {
        const uint64_t high =
            (sharedHigh32 << 32U) |
            static_cast<uint64_t>((nodeCount - nodeIdx) & 0xffffffffULL);
        const uint64_t low = static_cast<uint64_t>(nodeIdx) + 1ULL;
        return makeId(high, low);
    };

    std::vector<Node> nodes =
        makeDepthLinkedForest(nodeCount, depthCycleMax, idGenerator);
    return shuffledCopy(nodes, shuffleSeed);
}

inline std::vector<Node> makeSequentialIdForest(std::size_t nodeCount,
                                                uint32_t depthCycleMax,
                                                uint64_t shuffleSeed) {
    auto idGenerator = [](std::size_t nodeIdx) {
        return makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL);
    };

    std::vector<Node> nodes =
        makeDepthLinkedForest(nodeCount, depthCycleMax, idGenerator);
    return shuffledCopy(nodes, shuffleSeed);
}

inline std::vector<Node> makeManyExternalParentForest(std::size_t nodeCount,
                                                      uint64_t shuffleSeed) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        const UInt128 nodeId =
            makeId(0x1000ULL, static_cast<uint64_t>(nodeIdx) + 1ULL);
        const UInt128 parentId =
            makeId(0x2000ULL, static_cast<uint64_t>(nodeIdx) + 1ULL);
        nodes.push_back(Node{nodeId, parentId});
    }
    return shuffledCopy(nodes, shuffleSeed);
}

inline std::vector<Node> makeManySiblingsForest(std::size_t nodeCount,
                                                uint64_t shuffleSeed) {
    if (nodeCount == 0) {
        return {};
    }

    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    const UInt128 rootId = makeId(0, 1);
    nodes.push_back(Node{rootId, 0});
    for (std::size_t nodeIdx = 1; nodeIdx < nodeCount; ++nodeIdx) {
        nodes.push_back(
            Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL), rootId});
    }
    return shuffledCopy(nodes, shuffleSeed);
}

inline std::vector<Node>
makeGeneratedForestForKind(DatasetKind datasetKind, std::size_t nodeCount,
                           uint32_t dataSeed = kDefaultBenchmarkDataSeed) {
    switch (datasetKind) {
    case DatasetKind::Random:
        return makeGeneratedForest(
            nodeCount, kCommonFixtureMaxDepth,
            mixFixtureSeed(dataSeed, kRandomDatasetSeedSalt));
    case DatasetKind::Outliers:
        return makeGeneratedForestWithOutliers(
            nodeCount, kCommonFixtureMaxDepth, dataSeed);
    case DatasetKind::SameHigh64:
        return makeGeneratedForestWithHighWordCollisions(
            nodeCount, kCommonFixtureMaxDepth,
            mixFixtureSeed(dataSeed, kSameHigh64DatasetSeedSalt));
    case DatasetKind::SameHigh32:
        return makeGeneratedForestWithHigh32Collisions(
            nodeCount, kCommonFixtureMaxDepth,
            mixFixtureSeed(dataSeed, kSameHigh32DatasetSeedSalt));
    case DatasetKind::Sequential:
        return makeSequentialIdForest(
            nodeCount, kCommonFixtureMaxDepth,
            mixFixtureSeed(dataSeed, kSequentialDatasetSeedSalt));
    case DatasetKind::ExternalParents:
        return makeManyExternalParentForest(
            nodeCount,
            mixFixtureSeed(dataSeed, kExternalParentsDatasetSeedSalt));
    case DatasetKind::Siblings:
        return makeManySiblingsForest(
            nodeCount, mixFixtureSeed(dataSeed, kSiblingsDatasetSeedSalt));
    }
    throw std::runtime_error("unknown dataset");
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_UINT128_FIXTURES_HPP
