#ifndef FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP

#include "control_parent_index.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_permutation_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/detail/validation.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "hash_support.hpp"
#include "uint128_fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forest_sorting::test_support {

struct ParentBuildArtifacts {
    std::vector<std::size_t> parentIndex;
    std::vector<std::size_t> idPermutation;
    bool hasIdPermutation = false;
};

enum class ParentKind : uint8_t {
    Unordered,
    Flat,
    Control,
    ControlFinalizerHash,
    RadixJoinIdMsdChunk8,
    RadixJoinIdMsdChunk16,
    RadixJoinIdMsdChunk32,
    RadixJoinIdMsdChunk64,
    RadixJoinIdMsdRangeLadder1024_16384,
    RadixJoinIdMsdRangeLadder2048_32768,
    RadixJoinIdMsdRangeLadder4096_65536,
    RadixJoinIdMsdBytePartitionCore,
    RadixDirectoryIdMsdChunk32Prefix8,
    RadixDirectoryIdMsdChunk32Prefix16,
};

using ParentBuildFunction =
    ParentBuildArtifacts (*)(const std::vector<Node> &nodes);

struct ParentRegistryEntry {
    ParentKind kind;
    std::string_view name;
    ParentBuildFunction build;
    bool includeByDefault;
};

const std::vector<ParentRegistryEntry> &getParentRegistry();

inline std::vector<ParentKind> defaultParentKinds() {
    std::vector<ParentKind> kinds;
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        if (entry.includeByDefault) {
            kinds.push_back(entry.kind);
        }
    }
    return kinds;
}

inline std::vector<ParentKind> registeredParentKinds() {
    std::vector<ParentKind> kinds;
    kinds.reserve(getParentRegistry().size());
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        kinds.push_back(entry.kind);
    }
    return kinds;
}

inline std::string_view parentName(ParentKind parentKind) {
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        if (entry.kind == parentKind) {
            return entry.name;
        }
    }
    return "unknown";
}

template <typename Id> struct FlatIdIndexSlot {
    Id id{};
    std::size_t nodeIndex = detail::no_parent;
    bool occupied = false;
};

template <typename Id, typename IdTraits> class FlatIdIndex {
  public:
    FlatIdIndex(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits),
          slots_(detail::nextPowerOfTwo((itemCount * 2) + 1)) {}

    ControlInsertResult insertBounded(const Id &nodeId, std::size_t nodeIndex,
                                      std::size_t maxProbeLimit) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (std::size_t probeIdx = 0; probeIdx < maxProbeLimit; ++probeIdx) {
            FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                slot = FlatIdIndexSlot<Id>{nodeId, nodeIndex, true};
                return ControlInsertResult::Inserted;
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return ControlInsertResult::Duplicate;
            }
            slotIndex = (slotIndex + 1) & mask;
        }
        return ControlInsertResult::ProbeLimitExceeded;
    }

    ControlFindResult findBounded(const Id &nodeId,
                                  std::size_t maxProbeLimit) const noexcept {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (std::size_t probeIdx = 0; probeIdx < maxProbeLimit; ++probeIdx) {
            const FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                return {detail::no_parent, false, false};
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return {slot.nodeIndex, true, false};
            }
            slotIndex = (slotIndex + 1) & mask;
        }
        return {detail::no_parent, false, true};
    }

  private:
    const IdTraits &idTraits_;
    std::vector<FlatIdIndexSlot<Id>> slots_;
};

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexFlatHash(const Nodes &nodes,
                                                  const Traits &traits) {
    using Id = Traits::Id;
    FlatIdIndex<Id, Traits> idToIndex(nodes.size(), traits);
    const std::size_t maxProbeLimit = max_probe_groups_before_fallback * 8;
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const ControlInsertResult result = idToIndex.insertBounded(
            traits.id(nodes[nodeIdx]), nodeIdx, maxProbeLimit);
        if (result == ControlInsertResult::Duplicate) {
            throw std::runtime_error("duplicate node id");
        }
        if (result == ControlInsertResult::ProbeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (detail::isParentSentinel(traits, parentId)) {
            continue;
        }
        const ControlFindResult result =
            idToIndex.findBounded(parentId, maxProbeLimit);
        if (result.probeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
        if (result.found) {
            parent[nodeIdx] = result.nodeIndex;
        }
    }

    return parent;
}

struct UInt128BaselineHash {
    std::size_t operator()(UInt128 value) const noexcept {
        return fnvHashUInt128(value);
    }
};

inline std::vector<std::size_t>
buildParentIndexUnorderedMap(const std::vector<Node> &nodes) {
    std::unordered_map<UInt128, std::size_t, UInt128BaselineHash> idToIndex;
    idToIndex.reserve(nodes.size() * 2);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const auto inserted = idToIndex.emplace(nodes[nodeIdx].id, nodeIdx);
        if (!inserted.second) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const UInt128 parentId = nodes[nodeIdx].parentId;
        if (parentId == 0) {
            continue;
        }
        const auto parentIt = idToIndex.find(parentId);
        if (parentIt != idToIndex.end()) {
            parent[nodeIdx] = parentIt->second;
        }
    }

    return parent;
}

inline detail::RadixParentIndexResult
buildParentIndexRadixMsdBytePartitionCoreResult(
    const std::vector<Node> &nodes) {
    std::vector<std::size_t> scratch;
    auto sortPermutation = [&](std::vector<std::size_t> &permutation,
                               auto idForIndex, const auto &sortTraits) {
        scratch.resize(permutation.size());
        auto digitForIndex = [&](std::size_t entryIndex,
                                 std::size_t digitIndex) {
            return sortTraits.byte_msb_first(idForIndex(entryIndex),
                                             digitIndex);
        };
        auto rangeDone = [](std::size_t, std::size_t) {};
        detail::radixMsdPartitionRanges(
            permutation, scratch, 0, permutation.size(), 0,
            sortTraits.id_byte_count, digitForIndex, rangeDone);
    };
    return detail::buildParentIndexRadixJoinWithPermutationSorter(
        nodes, UInt128NodeTraits{}, sortPermutation);
}

struct UInt128NodeFinalizerHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        const std::uint64_t highVal = static_cast<uint64_t>(nodeId >> 64);
        const std::uint64_t lowVal = static_cast<uint64_t>(nodeId);

        std::uint64_t mixed = lowVal ^ (highVal * golden_ratio_64);
        mixed = mixDeterministicUInt128Word(mixed);

        if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
            return static_cast<std::size_t>(mixed);
        } else {
            return static_cast<std::size_t>(mixed ^ (mixed >> 32));
        }
    }
};

template <std::size_t RadixChunkBytes>
ParentBuildArtifacts
buildRadixJoinIdMsdChunkParentArtifacts(const std::vector<Node> &nodes) {
    auto result =
        detail::buildParentIndexRadixJoinResultByMsdChunks<RadixChunkBytes>(
            nodes, UInt128NodeTraits{});
    return {std::move(result.parentIndex), std::move(result.idPermutation),
            true};
}

template <typename LadderPolicy>
ParentBuildArtifacts
buildRadixJoinIdMsdRangeLadderParentArtifacts(const std::vector<Node> &nodes) {
    detail::IdMsdChunkLadderSortWorkspace<detail::ProductionIdCountPolicy>
        workspace;
    auto sortPermutation = [&](std::vector<std::size_t> &permutation,
                               auto idForIndex, const auto &sortTraits) {
        detail::sortIndexRangeByIdMsdChunkLadder<
            LadderPolicy, detail::ProductionIdCountPolicy>(
            permutation, idForIndex, sortTraits, 0, permutation.size(),
            workspace);
    };
    auto result = detail::buildParentIndexRadixJoinWithPermutationSorter(
        nodes, UInt128NodeTraits{}, sortPermutation);
    return {std::move(result.parentIndex), std::move(result.idPermutation),
            true};
}

struct PrefixDirectoryRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

template <std::size_t IdPrefixBytes, typename Id, typename Traits>
std::size_t prefixDirectoryKey(const Id &nodeId, const Traits &traits) {
    return static_cast<std::size_t>(
        detail::chunkMsbFirst<IdPrefixBytes>(nodeId, 0, traits));
}

template <std::size_t IdPrefixBytes>
ParentBuildArtifacts
buildRadixDirectoryIdMsdChunk32ParentArtifacts(const std::vector<Node> &nodes) {
    using Id = UInt128NodeTraits::Id;
    constexpr std::size_t directorySize = std::size_t{1}
                                          << (IdPrefixBytes * 8U);
    const UInt128NodeTraits traits;

    std::vector<Id> ids;
    ids.reserve(nodes.size());
    std::vector<Id> parentIds;
    parentIds.reserve(nodes.size());
    std::vector<std::size_t> childIndexes;
    childIndexes.reserve(nodes.size());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        ids.push_back(UInt128NodeTraits::id(nodes[nodeIndex]));
        const Id parentId = UInt128NodeTraits::parent_id(nodes[nodeIndex]);
        if (!detail::isParentSentinel(traits, parentId)) {
            parentIds.push_back(parentId);
            childIndexes.push_back(nodeIndex);
        }
    }

    std::vector<std::size_t> idPermutation(ids.size());
    std::iota(idPermutation.begin(), idPermutation.end(), 0);

    return detail::withIdPermutationComparator(
        ids, parentIds, traits, [&](const auto &comparator) {
            detail::IdMsdChunkSortWorkspace<
                detail::production_id_radix_chunk_bytes,
                detail::ProductionIdCountPolicy>
                workspace;
            auto idForEntryIndex =
                [&](std::size_t entryIndex) -> decltype(auto) {
                return comparator.leftIdForSort(entryIndex);
            };
            detail::sortIndexRangeByIdMsdChunks<
                detail::production_id_radix_chunk_bytes,
                detail::ProductionIdCountPolicy>(
                idPermutation, idForEntryIndex, comparator.sortTraits(), 0,
                idPermutation.size(), 0, workspace);

            detail::rejectAdjacentDuplicates(
                idPermutation,
                [&](std::size_t lhsIndex, std::size_t rhsIndex) noexcept {
                    return comparator.leftEqual(lhsIndex, rhsIndex);
                },
                "duplicate node id");

            std::vector<PrefixDirectoryRange> directory(directorySize);
            std::size_t rangeBegin = 0;
            while (rangeBegin < idPermutation.size()) {
                const std::size_t key = prefixDirectoryKey<IdPrefixBytes>(
                    ids[idPermutation[rangeBegin]], traits);
                std::size_t rangeEnd = rangeBegin + 1;
                while (rangeEnd < idPermutation.size() &&
                       prefixDirectoryKey<IdPrefixBytes>(
                           ids[idPermutation[rangeEnd]], traits) == key) {
                    ++rangeEnd;
                }
                directory[key] = {rangeBegin, rangeEnd};
                rangeBegin = rangeEnd;
            }

            std::vector<std::size_t> parentIndex(nodes.size(),
                                                 detail::no_parent);
            for (std::size_t queryIndex = 0; queryIndex < parentIds.size();
                 ++queryIndex) {
                const std::size_t key = prefixDirectoryKey<IdPrefixBytes>(
                    parentIds[queryIndex], traits);
                const PrefixDirectoryRange range = directory[key];
                std::size_t low = range.begin;
                std::size_t high = range.end;
                while (low < high) {
                    const std::size_t mid = low + ((high - low) / 2U);
                    const std::size_t nodeIndex = idPermutation[mid];
                    if (comparator.compare(nodeIndex, queryIndex) < 0) {
                        low = mid + 1;
                    } else {
                        high = mid;
                    }
                }
                if (low < range.end) {
                    const std::size_t nodeIndex = idPermutation[low];
                    if (comparator.compare(nodeIndex, queryIndex) == 0 &&
                        comparator.crossEqual(nodeIndex, queryIndex)) {
                        parentIndex[childIndexes[queryIndex]] = nodeIndex;
                    }
                }
            }

            return ParentBuildArtifacts{std::move(parentIndex),
                                        std::move(idPermutation), true};
        });
}

inline const std::vector<ParentRegistryEntry> &getParentRegistry() {
    static const std::vector<ParentRegistryEntry> registry = {
        {ParentKind::Unordered, "unordered",
         [](const std::vector<Node> &nodes) {
             return ParentBuildArtifacts{
                 buildParentIndexUnorderedMap(nodes), {}, false};
         },
         false},
        {ParentKind::Flat, "flat",
         [](const std::vector<Node> &nodes) {
             return ParentBuildArtifacts{
                 buildParentIndexFlatHash(nodes, UInt128NodeHashedTraits{}),
                 {},
                 false};
         },
         false},
        {ParentKind::Control, "control",
         [](const std::vector<Node> &nodes) {
             return ParentBuildArtifacts{
                 buildParentIndexControl(nodes, UInt128NodeHashedTraits{}),
                 {},
                 false};
         },
         false},
        {ParentKind::ControlFinalizerHash, "control-finalizer-hash",
         [](const std::vector<Node> &nodes) {
             return ParentBuildArtifacts{
                 buildParentIndexControl(nodes,
                                         UInt128NodeFinalizerHashTraits{}),
                 {},
                 false};
         },
         false},
        {ParentKind::RadixJoinIdMsdChunk8, "radix-join-id-msd-chunk8",
         buildRadixJoinIdMsdChunkParentArtifacts<1>, false},
        {ParentKind::RadixJoinIdMsdChunk16, "radix-join-id-msd-chunk16",
         buildRadixJoinIdMsdChunkParentArtifacts<2>, false},
        {ParentKind::RadixJoinIdMsdChunk32, "radix-join-id-msd-chunk32",
         buildRadixJoinIdMsdChunkParentArtifacts<4>, true},
        {ParentKind::RadixJoinIdMsdChunk64, "radix-join-id-msd-chunk64",
         buildRadixJoinIdMsdChunkParentArtifacts<8>, false},
        {ParentKind::RadixJoinIdMsdRangeLadder1024_16384,
         "radix-join-id-msd-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-"
         "otherwise",
         buildRadixJoinIdMsdRangeLadderParentArtifacts<
             detail::RangeLadder<1024, 16384>>,
         false},
        {ParentKind::RadixJoinIdMsdRangeLadder2048_32768,
         "radix-join-id-msd-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-"
         "otherwise",
         buildRadixJoinIdMsdRangeLadderParentArtifacts<
             detail::RangeLadder<2048, 32768>>,
         false},
        {ParentKind::RadixJoinIdMsdRangeLadder4096_65536,
         "radix-join-id-msd-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-"
         "otherwise",
         buildRadixJoinIdMsdRangeLadderParentArtifacts<
             detail::RangeLadder<4096, 65536>>,
         false},
        {ParentKind::RadixJoinIdMsdBytePartitionCore,
         "radix-join-id-msd-byte-partition-core",
         [](const std::vector<Node> &nodes) {
             auto result =
                 buildParentIndexRadixMsdBytePartitionCoreResult(nodes);
             return ParentBuildArtifacts{std::move(result.parentIndex),
                                         std::move(result.idPermutation), true};
         },
         false},
        {ParentKind::RadixDirectoryIdMsdChunk32Prefix8,
         "radix-directory-id-msd-chunk32-prefix8",
         buildRadixDirectoryIdMsdChunk32ParentArtifacts<1>, false},
        {ParentKind::RadixDirectoryIdMsdChunk32Prefix16,
         "radix-directory-id-msd-chunk32-prefix16",
         buildRadixDirectoryIdMsdChunk32ParentArtifacts<2>, false},
    };
    return registry;
}

inline ParentBuildArtifacts
buildParentArtifactsForKind(ParentKind parentKind,
                            const std::vector<Node> &nodes) {
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        if (entry.kind == parentKind) {
            return entry.build(nodes);
        }
    }
    throw std::runtime_error("unknown parent builder");
}

inline std::vector<std::size_t>
buildParentIndexForKind(ParentKind parentKind, const std::vector<Node> &nodes) {
    auto artifacts = buildParentArtifactsForKind(parentKind, nodes);
    return std::move(artifacts.parentIndex);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
