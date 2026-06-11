#ifndef FOREST_SORTING_SUPPORT_UINT128_FIXTURES_HPP
#define FOREST_SORTING_SUPPORT_UINT128_FIXTURES_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include "parent_index_baselines.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace forest_sorting::test_support {

inline UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

inline std::vector<std::size_t>
buildParentIndexFlatHashForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexFlatHashBaseline(nodes, UInt128NodeTraits{});
}

inline std::vector<std::size_t>
buildParentIndexTableForUInt128(const std::vector<Node> &nodes) {
    return detail::buildParentIndex(nodes, UInt128NodeTraits{});
}

inline std::vector<std::size_t>
buildParentIndexRadixJoinForUInt128(const std::vector<Node> &nodes) {
    return detail::buildParentIndexRadixJoin(nodes, UInt128NodeTraits{});
}

inline std::vector<std::size_t>
buildParentIndexForUInt128(const std::vector<Node> &nodes) {
    return buildParentIndexTableForUInt128(nodes);
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
