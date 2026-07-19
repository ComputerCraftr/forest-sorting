#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_SORT_REGISTRY_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_SORT_REGISTRY_HPP

#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace forest_sorting::benchmark_support {

enum class SortCategory : uint8_t {
    Production,
    Baseline,
    Comparator,
    CounterPolicyExperiment,
    RangeLadderExperiment,
};

enum class SortKind : uint8_t {
    Comparison,
    DenseDepth2BucketsThenIdLsd,
    CompositeDepth2IdLsd,
    DenseDepth2BucketsThenIdMsdChunk64FullClear,
    CompositeDepth2IdByteMsdPartitionCore,
    Depth2FirstThenIdMsdChunk32BitmaskLe512NoDense,
    Depth2FirstThenIdMsdChunk32BitmaskLe512,
    GlobalIdPermutationThenDepthStable,
    Depth2FirstThenIdMsdChunk8FullClear,
    Depth2FirstThenIdMsdChunk8BitmaskLe512,
    Depth2FirstThenIdMsdChunk16FullClear,
    Depth2FirstThenIdMsdChunk16BitmaskLe512,
    Depth2FirstThenIdMsdChunk32FullClear,
    Depth2FirstThenIdMsdChunk64FullClear,
    Depth4FirstThenIdMsdChunk32FullClear,
    Depth2FirstThenIdMsdChunk32BitmaskLe128,
    Depth2FirstThenIdMsdChunk32BitmaskLe256,
    Depth2FirstThenIdMsdChunk32BitmaskLe1024,
    Depth2FirstThenIdMsdChunk32BitmaskLe4096,
    Depth2FirstThenIdRangeLadderChunk8Le1024Chunk16Le16384FullClear,
    Depth2FirstThenIdRangeLadderChunk8Le2048Chunk16Le32768FullClear,
    Depth2FirstThenIdRangeLadderChunk8Le4096Chunk16Le65536FullClear,
    Depth2FirstThenIdRangeLadderChunk8Le1024Chunk16Le16384BitmaskLe512,
    Depth2FirstThenIdRangeLadderChunk8Le2048Chunk16Le32768BitmaskLe512,
    Depth2FirstThenIdRangeLadderChunk8Le4096Chunk16Le65536BitmaskLe512,
};

using SortFunction = std::vector<Node> (*)(const std::vector<Node> &,
                                           const std::vector<std::size_t> &);
using OptionalIdPermutationSortFunction = std::vector<Node> (*)(
    const std::vector<Node> &, const std::vector<std::size_t> &,
    const std::vector<std::size_t> *);

struct SortRegistryEntry {
    SortKind kind;
    std::string_view name;
    SortFunction sortFunction;
    OptionalIdPermutationSortFunction optionalIdPermutationSortFunction;
    SortCategory category;
    bool includeByDefault;
};

std::span<const SortRegistryEntry> sortRegistry() noexcept;
std::vector<SortKind> defaultSortKinds();
std::vector<SortKind> registeredSortKinds();
std::string_view sortName(SortKind sortKind) noexcept;
std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex,
                  const std::vector<std::size_t> *idPermutation);
std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex);
SortKind parseSortKind(std::string_view value);
void validateSortRegistry();

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_SORT_REGISTRY_HPP
