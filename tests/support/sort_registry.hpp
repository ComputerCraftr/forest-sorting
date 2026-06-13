#ifndef FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
#define FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP

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
    CompositeByteMsd,
    AdaptiveDepth2ChunkMsd,
    AdaptiveDepth2NoDenseChunkMsd,
    AdaptiveDepth2ByteMsd,
    AdaptiveDepth2ChunkMsdBinarySmall,
    AdaptiveDepth4ChunkMsd,
};

using SortFunction = std::vector<Node> (*)(const std::vector<Node> &,
                                           const std::vector<std::size_t> &);

struct SortRegistryEntry {
    SortKind kind;
    std::string_view name;
    SortFunction sortFunction;
};

inline constexpr std::array<SortRegistryEntry, 10> kSortRegistry = {{
    {SortKind::Comparison, "comparison", sortForestByComparisonWithParent},
    {SortKind::DepthBucketDepth2Lsd, "depth-bucket-depth2-lsd",
     sortForestByDenseDepth2BucketedLsdWithParent},
    {SortKind::CompositeLsd, "composite-depth2-lsd",
     sortForestByCompositeDepth2LsdWithParent},
    {SortKind::DepthBucketDepth2ChunkMsd, "depth-bucket-depth2-chunk-msd",
     sortForestByDenseDepth2BucketedMsdWithParent},
    {SortKind::CompositeByteMsd, "composite-depth2-byte-msd",
     sortForestByCompositeDepth2MsdWithParent},
    {SortKind::AdaptiveDepth2ChunkMsd, "adaptive-depth2-chunk-msd",
     sortForestByAdaptiveDepth2WithParent},
    {SortKind::AdaptiveDepth2NoDenseChunkMsd,
     "adaptive-depth2-no-dense-chunk-msd",
     sortForestByAdaptiveDepth2NoDenseMsdWithParent},
    {SortKind::AdaptiveDepth2ByteMsd, "adaptive-depth2-byte-msd",
     sortForestByAdaptiveDepth2ByteMsdWithParent},
    {SortKind::AdaptiveDepth2ChunkMsdBinarySmall,
     "adaptive-depth2-chunk-msd-binary-small",
     sortForestByAdaptiveDepth2BinarySmallWithParent},
    {SortKind::AdaptiveDepth4ChunkMsd, "adaptive-depth4-chunk-msd",
     sortForestByAdaptiveDepth4WithParent},
}};

inline std::vector<SortKind> allSortKinds() {
    std::vector<SortKind> sorts;
    sorts.reserve(kSortRegistry.size());
    for (const SortRegistryEntry &entry : kSortRegistry) {
        sorts.push_back(entry.kind);
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
