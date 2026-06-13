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

inline UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

enum class DatasetKind : uint8_t {
    Random,
    Outliers,
    SameHigh64,
    Sequential,
    ExternalParents,
    Siblings,
};

inline constexpr std::array<DatasetKind, 6> kAllDatasetKinds = {
    DatasetKind::Random,          DatasetKind::Outliers,
    DatasetKind::SameHigh64,      DatasetKind::Sequential,
    DatasetKind::ExternalParents, DatasetKind::Siblings,
};

constexpr std::array<DatasetKind, 6> allDatasetKinds() noexcept {
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
    case DatasetKind::Sequential:
        return "sequential";
    case DatasetKind::ExternalParents:
        return "external-parents";
    case DatasetKind::Siblings:
        return "siblings";
    }
    return "unknown";
}

inline std::vector<uint32_t>
computeDepthsForUInt128(const std::vector<Node> &nodes,
                        const std::vector<std::size_t> &parentIndex) {
    return detail::computeDepths(nodes, parentIndex, UInt128NodeTraits{});
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

inline std::vector<Node> makeGeneratedForest(std::size_t nodeCount,
                                             uint32_t depthCycleMax) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937_64 rng(0x5eed1234ULL);
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

        const uint64_t high = rng();
        const uint64_t low = static_cast<uint64_t>(nodeIdx) + 1ULL;
        nodes.push_back(Node{makeId(high, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    std::shuffle(nodes.begin(), nodes.end(), rng);
    return nodes;
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

inline std::vector<Node>
makeGeneratedForestWithOutliers(std::size_t nodeCount,
                                uint32_t commonMaxDepth) {
    std::vector<Node> nodes = makeGeneratedForest(nodeCount, commonMaxDepth);
    appendDeepChain(nodes, 128, 0x1000ULL);
    appendDeepChain(nodes, 512, 0x2000ULL);
    appendDeepChain(nodes, 1024, 0x3000ULL);
    return shuffledCopy(nodes, 0xabcdef00ULL);
}

inline std::vector<Node> makeSameHigh64Forest(std::size_t nodeCount) {
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

inline std::vector<Node>
makeGeneratedForestWithHighWordCollisions(std::size_t nodeCount,
                                          uint32_t depthCycleMax) {
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);

    constexpr uint64_t sharedHighWord = 0x123456789abcdef0ULL;
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

        const uint64_t low = static_cast<uint64_t>(nodeCount - nodeIdx);
        nodes.push_back(Node{makeId(sharedHighWord, low), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    return shuffledCopy(nodes, 0xfeedfaceULL);
}

inline std::vector<Node> makeSequentialIdForest(std::size_t nodeCount) {
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

inline std::vector<Node> makeSequentialIdForest(std::size_t nodeCount,
                                                uint32_t depthCycleMax) {
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

        nodes.push_back(
            Node{makeId(0, static_cast<uint64_t>(nodeIdx) + 1ULL), parentId});
        lastIndexAtDepth[static_cast<std::size_t>(targetDepth)] = nodeIdx;
    }

    return shuffledCopy(nodes, 0x1234abcdULL);
}

inline std::vector<Node> makeManyExternalParentForest(std::size_t nodeCount) {
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

inline std::vector<Node> makeManySiblingsForest(std::size_t nodeCount) {
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

inline std::vector<Node> makeGeneratedForestForKind(DatasetKind datasetKind,
                                                    std::size_t nodeCount) {
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
