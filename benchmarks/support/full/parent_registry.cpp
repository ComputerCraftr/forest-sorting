#include "forest_sorting/benchmark_support/full/parent_registry.hpp"
#include "forest_sorting/benchmark_support/full/control_parent_baseline.hpp"
#include "forest_sorting/benchmark_support/full/hash_variants.hpp"
#include "forest_sorting/benchmark_support/full/parent_index_baselines.hpp"
#include "forest_sorting/benchmark_support/full/radix_ladder_variants.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <array>
#include <cstddef>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace forest_sorting::benchmark_support {

std::vector<ParentKind> defaultParentKinds() {
    std::vector<ParentKind> kinds;
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        if (entry.includeByDefault) {
            kinds.push_back(entry.kind);
        }
    }
    return kinds;
}

std::vector<ParentKind> registeredParentKinds() {
    std::vector<ParentKind> kinds;
    kinds.reserve(parentRegistry().size());
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        kinds.push_back(entry.kind);
    }
    return kinds;
}

std::string_view parentName(ParentKind parentKind) noexcept {
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        if (entry.kind == parentKind) {
            return entry.name;
        }
    }
    return "unknown";
}

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
buildRadixJoinIdMsdLadderParentArtifacts(const std::vector<Node> &nodes) {
    IdMsdLadderWorkspace<detail::ProductionIdCountPolicy> workspace;
    auto sortPermutation = [&](std::vector<std::size_t> &permutation,
                               auto idForIndex, const auto &sortTraits) {
        sortIndexRangeByIdMsdLadder<LadderPolicy,
                                    detail::ProductionIdCountPolicy>(
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
    static_assert(IdPrefixBytes == 1U || IdPrefixBytes == 2U,
                  "radix directory supports 8-bit or 16-bit ID prefixes");
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

std::span<const ParentRegistryEntry> parentRegistry() noexcept {
    static const auto registry = std::to_array<ParentRegistryEntry>({
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
        {ParentKind::RadixJoinIdMsdSizeLadderChunk8Le1024Chunk16Le16384,
         "radix-join-id-msd-size-ladder-chunk8-le1024-chunk16-le16384-"
         "chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<
             Chunk8Chunk16Chunk32Ladder<1024, 16384>>,
         false},
        {ParentKind::RadixJoinIdMsdSizeLadderChunk8Le2048Chunk16Le32768,
         "radix-join-id-msd-size-ladder-chunk8-le2048-chunk16-le32768-"
         "chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<
             Chunk8Chunk16Chunk32Ladder<2048, 32768>>,
         false},
        {ParentKind::RadixJoinIdMsdSizeLadderChunk8Le4096Chunk16Le65536,
         "radix-join-id-msd-size-ladder-chunk8-le4096-chunk16-le65536-"
         "chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<
             Chunk8Chunk16Chunk32Ladder<4096, 65536>>,
         false},
        {ParentKind::RadixJoinIdMsdSizeLadderChunk16Le10000,
         "radix-join-id-msd-size-ladder-chunk16-le10000-chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<Chunk16Chunk32Ladder<10000>>,
         false},
        {ParentKind::RadixJoinIdMsdSizeLadderChunk16Le16384,
         "radix-join-id-msd-size-ladder-chunk16-le16384-chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<Chunk16Chunk32Ladder<16384>>,
         false},
        {ParentKind::RadixJoinIdMsdSizeLadderChunk16Le32768,
         "radix-join-id-msd-size-ladder-chunk16-le32768-chunk32-otherwise",
         buildRadixJoinIdMsdLadderParentArtifacts<Chunk16Chunk32Ladder<32768>>,
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
    });
    return registry;
}

ParentBuildArtifacts
buildParentArtifactsForKind(ParentKind parentKind,
                            const std::vector<Node> &nodes) {
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        if (entry.kind == parentKind) {
            return entry.build(nodes);
        }
    }
    throw std::runtime_error("unknown parent builder");
}

std::vector<std::size_t>
buildParentIndexForKind(ParentKind parentKind, const std::vector<Node> &nodes) {
    auto artifacts = buildParentArtifactsForKind(parentKind, nodes);
    return std::move(artifacts.parentIndex);
}

} // namespace forest_sorting::benchmark_support
