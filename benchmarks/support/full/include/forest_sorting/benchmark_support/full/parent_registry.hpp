#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_PARENT_REGISTRY_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_PARENT_REGISTRY_HPP

#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace forest_sorting::benchmark_support {

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
    RadixJoinIdMsdSizeLadderChunk8Le1024Chunk16Le16384,
    RadixJoinIdMsdSizeLadderChunk8Le2048Chunk16Le32768,
    RadixJoinIdMsdSizeLadderChunk8Le4096Chunk16Le65536,
    RadixJoinIdMsdSizeLadderChunk16Le10000,
    RadixJoinIdMsdSizeLadderChunk16Le16384,
    RadixJoinIdMsdSizeLadderChunk16Le32768,
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

std::span<const ParentRegistryEntry> parentRegistry() noexcept;
std::vector<ParentKind> defaultParentKinds();
std::vector<ParentKind> registeredParentKinds();
std::string_view parentName(ParentKind parentKind) noexcept;
ParentBuildArtifacts
buildParentArtifactsForKind(ParentKind parentKind,
                            const std::vector<Node> &nodes);
std::vector<std::size_t>
buildParentIndexForKind(ParentKind parentKind, const std::vector<Node> &nodes);

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_PARENT_REGISTRY_HPP
