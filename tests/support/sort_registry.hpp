#ifndef FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
#define FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP

#include "adaptive_sort_variants.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "sort_baselines.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::test_support {

enum class SortKind : uint8_t {
    Comparison,
    DepthBucketDepth2Lsd,
    CompositeLsd,
    DepthBucketDepth2ChunkMsd,
    CompositeByteMsdCopyback,
    CompositeByteMsdLowcopyBranchy,
    CompositeByteMsdLowcopyFlattened,
    CompositeByteMsdLowcopyBatched,
    AdaptiveDepth2U32ChunkMsdNoDense,
    AdaptiveDepth2U32ChunkMsd,
    AdaptiveDepth2U32ChunkMsdFullClear,
    AdaptiveDepth2U8ChunkMsd,
    AdaptiveDepth2U64ChunkMsd,
    AdaptiveDepth2U64ChunkMsdBinarySmall,
    AdaptiveDepth4U32ChunkMsd,

    // Track A2: Tail search strategy at the production threshold.
    AdaptiveDepth2U32ChunkMsdBinarySmall32,
    AdaptiveDepth2U32ChunkMsdExponentialSmall32,

    // Track B2: Branch-free touched bucket counting thresholds.
    AdaptiveDepth2U32ChunkMsdTouchedBitmask128,
    AdaptiveDepth2U32ChunkMsdTouchedBitmask256,
    AdaptiveDepth2U32ChunkMsdTouchedBitmask1024,
    AdaptiveDepth2U32ChunkMsdTouchedBitmask4096,
};

using SortFunction = std::vector<Node> (*)(const std::vector<Node> &,
                                           const std::vector<std::size_t> &);

struct SortRegistryEntry {
    SortKind kind;
    std::string_view name;
    SortFunction sortFunction;
    bool includeByDefault;
};

inline constexpr std::array<SortRegistryEntry, 21> kSortRegistry = {{
    {SortKind::Comparison, "comparison", sortForestByComparisonWithParent,
     true},
    {SortKind::DepthBucketDepth2Lsd, "depth-bucket-depth2-lsd",
     sortForestByDenseDepth2BucketedLsdWithParent, true},
    {SortKind::CompositeLsd, "composite-depth2-lsd",
     sortForestByCompositeDepth2LsdWithParent, true},
    {SortKind::DepthBucketDepth2ChunkMsd, "depth-bucket-depth2-chunk-msd",
     sortForestByDenseDepth2BucketedMsdWithParent, true},
    {SortKind::CompositeByteMsdCopyback, "composite-depth2-byte-msd-copyback",
     sortForestByCompositeDepth2MsdCopybackWithParent, true},
    {SortKind::CompositeByteMsdLowcopyBranchy,
     "composite-depth2-byte-msd-lowcopy-branchy",
     sortForestByCompositeDepth2MsdLowcopyBranchyWithParent, true},
    {SortKind::CompositeByteMsdLowcopyFlattened,
     "composite-depth2-byte-msd-lowcopy-flattened",
     sortForestByCompositeDepth2MsdLowcopyFlattenedWithParent, true},
    {SortKind::CompositeByteMsdLowcopyBatched,
     "composite-depth2-byte-msd-lowcopy-batched",
     sortForestByCompositeDepth2MsdLowcopyBatchedWithParent, true},
    {SortKind::AdaptiveDepth2U32ChunkMsdNoDense,
     "adaptive-depth2-u32-chunk-msd-no-dense",
     sortForestByAdaptiveDepth2U32ChunkNoDenseWithParent, true},
    {SortKind::AdaptiveDepth2U32ChunkMsd, "adaptive-depth2-u32-chunk-msd",
     sortForestByAdaptiveDepth2U32ChunkWithParent, true},
    {SortKind::AdaptiveDepth2U32ChunkMsdFullClear,
     "adaptive-depth2-u32-chunk-msd-full-clear",
     sortForestByAdaptiveDepth2U32ChunkFullClearWithParent, false},
    {SortKind::AdaptiveDepth2U8ChunkMsd, "adaptive-depth2-u8-chunk-msd",
     sortForestByAdaptiveDepth2U8ChunkWithParent, true},
    {SortKind::AdaptiveDepth2U64ChunkMsd, "adaptive-depth2-u64-chunk-msd",
     sortForestByAdaptiveDepth2U64ChunkWithParent, true},
    {SortKind::AdaptiveDepth2U64ChunkMsdBinarySmall,
     "adaptive-depth2-u64-chunk-msd-binary-small",
     sortForestByAdaptiveDepth2U64ChunkBinarySmallWithParent, true},
    {SortKind::AdaptiveDepth4U32ChunkMsd, "adaptive-depth4-u32-chunk-msd",
     sortForestByAdaptiveDepth4U32ChunkWithParent, true},

    // Track A2: Tail search strategy at the production threshold.
    {SortKind::AdaptiveDepth2U32ChunkMsdBinarySmall32,
     "adaptive-depth2-u32-chunk-msd-binary-small32",
     sortForestByAdaptiveDepth2U32ChunkTailTunedWithParent<BinarySmallSorter,
                                                           32>,
     false},
    {SortKind::AdaptiveDepth2U32ChunkMsdExponentialSmall32,
     "adaptive-depth2-u32-chunk-msd-exponential-small32",
     sortForestByAdaptiveDepth2U32ChunkTailTunedWithParent<
         ExponentialSmallSorter, 32>,
     false},

    // Track B2: Branch-free touched bucket counting thresholds.
    {SortKind::AdaptiveDepth2U32ChunkMsdTouchedBitmask128,
     "adaptive-depth2-u32-chunk-msd-touched-bitmask-128",
     sortForestByAdaptiveDepth2U32ChunkTouchedBitmaskWithParent<128>, false},
    {SortKind::AdaptiveDepth2U32ChunkMsdTouchedBitmask256,
     "adaptive-depth2-u32-chunk-msd-touched-bitmask-256",
     sortForestByAdaptiveDepth2U32ChunkTouchedBitmaskWithParent<256>, false},
    {SortKind::AdaptiveDepth2U32ChunkMsdTouchedBitmask1024,
     "adaptive-depth2-u32-chunk-msd-touched-bitmask-1024",
     sortForestByAdaptiveDepth2U32ChunkTouchedBitmaskWithParent<1024>, false},
    {SortKind::AdaptiveDepth2U32ChunkMsdTouchedBitmask4096,
     "adaptive-depth2-u32-chunk-msd-touched-bitmask-4096",
     sortForestByAdaptiveDepth2U32ChunkTouchedBitmaskWithParent<4096>, false},
}};

inline std::vector<SortKind> allSortKinds() {
    std::vector<SortKind> sorts;
    sorts.reserve(kSortRegistry.size());
    for (const SortRegistryEntry &entry : kSortRegistry) {
        if (entry.includeByDefault) {
            sorts.push_back(entry.kind);
        }
    }
    return sorts;
}

inline std::string_view sortName(SortKind sortKind) {
    for (const SortRegistryEntry &entry : kSortRegistry) {
        if (entry.kind == sortKind) {
            return entry.name;
        }
    }
    return "unknown";
}

inline std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex) {
    for (const SortRegistryEntry &entry : kSortRegistry) {
        if (entry.kind == sortKind) {
            return entry.sortFunction(nodes, parentIndex);
        }
    }
    throw std::runtime_error("unknown sort algorithm");
}

inline SortKind parseSortKind(std::string_view value) {
    for (const SortRegistryEntry &entry : kSortRegistry) {
        if (value == entry.name) {
            return entry.kind;
        }
    }
    throw std::runtime_error("unknown sort algorithm: " + std::string(value));
}

inline void validateSortRegistry() {
    for (std::size_t entryIdx = 0; entryIdx < kSortRegistry.size();
         ++entryIdx) {
        const SortRegistryEntry &entry = kSortRegistry[entryIdx];
        if (entry.name.empty()) {
            throw std::runtime_error("sort registry contains an empty name");
        }
        if (entry.sortFunction == nullptr) {
            throw std::runtime_error("sort registry contains a null function");
        }
        for (std::size_t otherIdx = entryIdx + 1;
             otherIdx < kSortRegistry.size(); ++otherIdx) {
            if (entry.name == kSortRegistry[otherIdx].name) {
                throw std::runtime_error(
                    "sort registry contains duplicate name: " +
                    std::string(entry.name));
            }
            if (entry.kind == kSortRegistry[otherIdx].kind) {
                throw std::runtime_error(
                    "sort registry contains duplicate kind");
            }
        }
    }
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
