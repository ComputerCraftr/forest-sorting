#ifndef FOREST_SORTING_SUPPORT_UINT128_FIXTURES_HPP
#define FOREST_SORTING_SUPPORT_UINT128_FIXTURES_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace forest_sorting::test_support {

inline constexpr uint32_t kDefaultBenchmarkDataSeed = 0x5eed1234U;
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

inline UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

inline uint64_t mixFixtureSeed(uint32_t seed, uint64_t salt) {
    uint64_t value = (static_cast<uint64_t>(seed) << 32U) ^ salt;
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
}

enum class DatasetKind : uint8_t {
    Random,
    Outliers,
    SameHigh64,
    SameHigh32,
    Sequential,
    ExternalParents,
    Siblings,
};

inline constexpr std::array<DatasetKind, 7> kAllDatasetKinds = {
    DatasetKind::Random,     DatasetKind::Outliers,
    DatasetKind::SameHigh64, DatasetKind::SameHigh32,
    DatasetKind::Sequential, DatasetKind::ExternalParents,
    DatasetKind::Siblings,
};

constexpr std::array<DatasetKind, 7> allDatasetKinds() noexcept {
    return kAllDatasetKinds;
}

inline std::string_view datasetName(DatasetKind datasetKind) {
    switch (datasetKind) {
    case DatasetKind::Random:
        return "random";
    case DatasetKind::Outliers:
        return "outliers";
    case DatasetKind::SameHigh64:
        return "same-high64";
    case DatasetKind::SameHigh32:
        return "same-high32";
    case DatasetKind::Sequential:
        return "sequential";
    case DatasetKind::ExternalParents:
        return "external-parents";
    case DatasetKind::Siblings:
        return "siblings";
    }
    return "unknown";
}

template <std::size_t DepthPrefixBytes = 4>
inline std::vector<detail::DepthValue<DepthPrefixBytes>>
computeDepthsForUInt128(const std::vector<Node> &nodes,
                        const std::vector<std::size_t> &parentIndex) {
    return detail::computeDepths<DepthPrefixBytes>(nodes, parentIndex,
                                                   UInt128NodeTraits{})
        .values;
}

inline bool sameNodes(const std::vector<Node> &lhs,
                      const std::vector<Node> &rhs) {
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

inline std::vector<Node> shuffledCopy(std::vector<Node> nodes, uint64_t seed) {
    std::mt19937_64 rng(seed); // NOLINT(bugprone-random-generator-seed)
    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
}

template <typename IdGenerator>
inline std::vector<Node> makeDepthLinkedForest(std::size_t nodeCount,
                                               uint32_t depthCycleMax,
                                               IdGenerator idGenerator) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    std::vector<std::size_t> lastIndexAtDepth(
        static_cast<std::size_t>(depthCycleMax) + 1, detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        uint32_t targetDepth = static_cast<uint32_t>(
            nodeIdx % (static_cast<std::size_t>(depthCycleMax) + 1));
        UInt128 parentId = 0;
        if (targetDepth > 0 &&
            lastIndexAtDepth[static_cast<std::size_t>(targetDepth - 1)] !=
                detail::no_parent) {
            parentId = nodes[lastIndexAtDepth[static_cast<std::size_t>(
                                 targetDepth - 1)]]
                           .id;
        } else {
            targetDepth = 0;
        }

        nodes.push_back(Node{idGenerator(nodeIdx), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    return nodes;
}

inline std::vector<Node> makeGeneratedForest(std::size_t nodeCount,
                                             uint32_t depthCycleMax,
                                             uint64_t rngSeed) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937_64 rng(rngSeed);
    auto idGenerator = [&](std::size_t nodeIdx) {
        const uint64_t high = rng();
        const uint64_t low = static_cast<uint64_t>(nodeIdx) + 1ULL;
        return makeId(high, low);
    };

    std::vector<Node> nodes =
        makeDepthLinkedForest(nodeCount, depthCycleMax, idGenerator);
    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
}

inline std::vector<Node>
makeGeneratedForest(std::size_t nodeCount,
                    uint32_t depthCycleMax = kCommonFixtureMaxDepth) {
    return makeGeneratedForest(nodeCount, depthCycleMax,
                               kDefaultGeneratedForestSeed);
}

inline void appendDeepChain(std::vector<Node> &nodes, uint32_t chainDepth,
                            uint64_t idBase) {
    UInt128 parentId = 0;
    for (uint32_t depth = 0; depth <= chainDepth; ++depth) {
        const UInt128 nodeId =
            makeId(idBase, static_cast<uint64_t>(depth) + 1ULL);
        nodes.push_back(Node{nodeId, parentId});
        parentId = nodeId;
    }
}

inline void appendDepthOutlierChains(std::vector<Node> &nodes) {
    appendDeepChain(nodes, 128, 0x1000ULL);
    appendDeepChain(nodes, 512, 0x2000ULL);
    appendDeepChain(nodes, 1024, 0x3000ULL);
}

inline std::vector<Node>
makeGeneratedForestWithOutliers(std::size_t nodeCount, uint32_t commonMaxDepth,
                                uint32_t dataSeed) {
    std::vector<Node> nodes =
        makeGeneratedForest(nodeCount, commonMaxDepth,
                            mixFixtureSeed(dataSeed, kOutlierBaseSeedSalt));
    appendDepthOutlierChains(nodes);
    return shuffledCopy(nodes,
                        mixFixtureSeed(dataSeed, kOutlierShuffleSeedSalt));
}

inline std::vector<Node> makeGeneratedForestWithOutliers(
    std::size_t nodeCount, uint32_t commonMaxDepth = kCommonFixtureMaxDepth) {
    std::vector<Node> nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
    appendDepthOutlierChains(nodes);
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

struct UInt128LowIdentityHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        return static_cast<std::size_t>(nodeId);
    }
};

struct UInt128HighIdentityHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        return static_cast<std::size_t>(nodeId >> 64U);
    }
};

inline std::vector<Node> makeHighIdentityCollisionRoots(std::size_t nodeCount) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        nodes.push_back(
            Node{makeId(1, static_cast<uint64_t>(nodeIdx) + 1ULL), 0});
    }
    return nodes;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_UINT128_FIXTURES_HPP
