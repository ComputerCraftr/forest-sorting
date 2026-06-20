#ifndef FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
#define FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP

#include "adaptive_sort_variants.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "sort_baselines.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::test_support {

enum class SortCategory : uint8_t {
    Production,
    Baseline,
    Comparator,
    CounterPolicyExperiment,
    RangeLadderExperiment,
    TailExperiment,
    Alias,
};

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
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear32,

    // Comparators
    AdaptiveDepth2U8ChunkMsdFullClearTailLinear32,
    AdaptiveDepth2U8ChunkMsdBitmaskLe512TailLinear32,
    AdaptiveDepth2U16ChunkMsdFullClearTailLinear32,
    AdaptiveDepth2U16ChunkMsdBitmaskLe512TailLinear32,
    AdaptiveDepth2U32ChunkMsdFullClearTailLinear32,
    AdaptiveDepth2U64ChunkMsdFullClearTailLinear32,
    AdaptiveDepth2U64ChunkMsdFullClearTailBinary32,
    AdaptiveDepth4U32ChunkMsdFullClearTailLinear32,

    // Counter Policy Experiments
    AdaptiveDepth2U32ChunkMsdBitmaskLe128TailLinear32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe256TailLinear32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe1024TailLinear32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe4096TailLinear32,

    // Tail Experiments
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear16,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear48,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBinary32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailExponential16,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailExponential32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailExponential48,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBranchlessBitwise16,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBranchlessBitwise32,
    AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBranchlessBitwise48,

    // Range Ladder Experiments
    AdaptiveDepth2RangeLadderU8Le1024U16Le16384FullClearTailLinear32,
    AdaptiveDepth2RangeLadderU8Le2048U16Le32768FullClearTailLinear32,
    AdaptiveDepth2RangeLadderU8Le4096U16Le65536FullClearTailLinear32,
    AdaptiveDepth2RangeLadderU8Le1024U16Le16384BitmaskLe512TailLinear32,
    AdaptiveDepth2RangeLadderU8Le2048U16Le32768BitmaskLe512TailLinear32,
    AdaptiveDepth2RangeLadderU8Le4096U16Le65536BitmaskLe512TailLinear32,
};

using SortFunction = std::vector<Node> (*)(const std::vector<Node> &,
                                           const std::vector<std::size_t> &);

struct SortRegistryEntry {
    SortKind kind;
    std::string_view name;
    SortFunction sortFunction;
    SortCategory category;
    bool includeByDefault;
};

template <std::size_t DepthPrefixBytes, std::size_t ChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter,
          bool AllowDenseDepthGrouping = true>
inline std::vector<Node>
sortForestByAdaptiveChunkWrapper(const std::vector<Node> &nodes,
                                 const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<DepthPrefixBytes, ChunkBytes,
                                               CountPolicy, SmallThreshold,
                                               SmallRangeSorter>(
        nodes, parentIndex, AllowDenseDepthGrouping);
}

inline bool defaultIncludeForCategory(SortCategory category, SortKind kind) {
    if (category == SortCategory::Production ||
        category == SortCategory::Baseline) {
        return true;
    }
    if (category == SortCategory::Comparator) {
        return kind !=
                   SortKind::AdaptiveDepth2U16ChunkMsdFullClearTailLinear32 &&
               kind != SortKind::
                           AdaptiveDepth2U16ChunkMsdBitmaskLe512TailLinear32 &&
               kind != SortKind::AdaptiveDepth2U32ChunkMsdFullClearTailLinear32;
    }
    return false;
}

inline void addEntry(std::vector<SortRegistryEntry> &registry, SortKind kind,
                     std::string_view name, SortFunction sortFunc,
                     SortCategory category) {
    registry.push_back({kind, name, sortFunc, category,
                        defaultIncludeForCategory(category, kind)});
}

inline void addAlias(std::vector<SortRegistryEntry> &registry, SortKind kind,
                     std::string_view name, SortFunction sortFunc) {
    registry.push_back({kind, name, sortFunc, SortCategory::Alias, false});
}

template <SortKind Kind, std::size_t Threshold>
void addBitmaskThresholdEntry(std::vector<SortRegistryEntry> &registry,
                              std::string_view name) {
    const auto func =
        sortForestByAdaptiveDepth2U32ChunkBitmaskLeWithParent<Threshold>;
    addEntry(registry, Kind, name, func, SortCategory::CounterPolicyExperiment);
}

template <SortKind Kind, typename SmallSorter, std::size_t Threshold>
void addTailExperimentEntry(std::vector<SortRegistryEntry> &registry,
                            std::string_view name) {
    const auto func = sortForestByAdaptiveChunkWrapper<
        2, 4,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>,
        Threshold, SmallSorter>;
    addEntry(registry, Kind, name, func, SortCategory::TailExperiment);
}

template <SortKind FullClearKind, SortKind BitmaskLe512Kind, std::size_t U8Max,
          std::size_t U16Max>
void addRangeLadderEntries(std::vector<SortRegistryEntry> &registry,
                           std::string_view fullClearName,
                           std::string_view bitmaskLe512Name) {
    const auto fcFunc = sortForestByAdaptiveDepth2RangeLadderWithParent<
        U8Max, U16Max, detail::FullClearCounts>;
    const auto bmFunc = sortForestByAdaptiveDepth2RangeLadderWithParent<
        U8Max, U16Max,
        detail::BitmaskTouchedCountsUpTo<
            detail::production_touched_count_max_range_size>>;

    addEntry(registry, FullClearKind, fullClearName, fcFunc,
             SortCategory::RangeLadderExperiment);
    addEntry(registry, BitmaskLe512Kind, bitmaskLe512Name, bmFunc,
             SortCategory::RangeLadderExperiment);
}

inline const std::vector<SortRegistryEntry> &getSortRegistry() {
    static const std::vector<SortRegistryEntry> registry = []() {
        std::vector<SortRegistryEntry> reg;

        // Local X-style macros for repetitive registry rows
#define FS_ADD_TAIL_LINEAR(Threshold)                                          \
    addTailExperimentEntry<                                                    \
        SortKind::AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear##Threshold,  \
        LinearSmallSorter, Threshold>(                                         \
        reg,                                                                   \
        "adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear" #Threshold)

#define FS_ADD_TAIL_EXPONENTIAL(Threshold)                                     \
    addTailExperimentEntry<                                                    \
        SortKind::                                                             \
            AdaptiveDepth2U32ChunkMsdBitmaskLe512TailExponential##Threshold,   \
        ExponentialSmallSorter, Threshold>(                                    \
        reg, "adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-"               \
             "exponential" #Threshold)

#define FS_ADD_TAIL_BRANCHLESS_BITWISE(Threshold)                                  \
    addTailExperimentEntry<                                                        \
        SortKind::                                                                 \
            AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBranchlessBitwise##Threshold, \
        BranchlessBitwiseSmallSorter<Threshold>, Threshold>(                       \
        reg, "adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-branchless-"        \
             "bitwise" #Threshold)

#define FS_ADD_BITMASK_THRESHOLD(Threshold)                                    \
    addBitmaskThresholdEntry<                                                  \
        SortKind::AdaptiveDepth2U32ChunkMsdBitmaskLe##Threshold##TailLinear32, \
        Threshold>(reg, "adaptive-depth2-u32-chunk-msd-bitmask-le" #Threshold  \
                        "-tail-linear32")

#define FS_ADD_RANGE_LADDER(U8Max, U16Max)                                                 \
    addRangeLadderEntries<                                                                 \
        SortKind::                                                                         \
            AdaptiveDepth2RangeLadderU8Le##U8Max##U16Le##U16Max##FullClearTailLinear32,    \
        SortKind::                                                                         \
            AdaptiveDepth2RangeLadderU8Le##U8Max##U16Le##U16Max##BitmaskLe512TailLinear32, \
        U8Max, U16Max>(reg,                                                                \
                       "adaptive-depth2-range-ladder-u8-le" #U8Max                         \
                       "-u16-le" #U16Max "-full-clear-tail-linear32",                      \
                       "adaptive-depth2-range-ladder-u8-le" #U8Max                         \
                       "-u16-le" #U16Max "-bitmask-le512-tail-linear32")

        // Baselines
        addEntry(reg, SortKind::Comparison, "comparison",
                 sortForestByComparisonWithParent, SortCategory::Baseline);
        addEntry(reg, SortKind::DepthBucketDepth2Lsd, "depth-bucket-depth2-lsd",
                 sortForestByDenseDepth2BucketedLsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeLsd, "composite-depth2-lsd",
                 sortForestByCompositeDepth2LsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::DepthBucketDepth2ChunkMsd,
                 "depth-bucket-depth2-chunk-msd",
                 sortForestByDenseDepth2BucketedMsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeByteMsdCopyback,
                 "composite-depth2-byte-msd-copyback",
                 sortForestByCompositeDepth2MsdCopybackWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeByteMsdLowcopyBranchy,
                 "composite-depth2-byte-msd-lowcopy-branchy",
                 sortForestByCompositeDepth2MsdLowcopyBranchyWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeByteMsdLowcopyFlattened,
                 "composite-depth2-byte-msd-lowcopy-flattened",
                 sortForestByCompositeDepth2MsdLowcopyFlattenedWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeByteMsdLowcopyBatched,
                 "composite-depth2-byte-msd-lowcopy-batched",
                 sortForestByCompositeDepth2MsdLowcopyBatchedWithParent,
                 SortCategory::Baseline);

        // Comparators
        addEntry(reg, SortKind::AdaptiveDepth2U32ChunkMsdNoDense,
                 "adaptive-depth2-u32-chunk-msd-no-dense",
                 sortForestByAdaptiveDepth2U32ChunkNoDenseWithParent,
                 SortCategory::Comparator);

        // Production Default Row
        addEntry(reg,
                 SortKind::AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear32,
                 "adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32",
                 sortForestByAdaptiveDepth2U32ChunkBitmaskLe512WithParent,
                 SortCategory::Production);

        // Deprecated Production Aliases (Legacy alias compatibility)
        addAlias(reg,
                 SortKind::AdaptiveDepth2U32ChunkMsdBitmaskLe512TailLinear32,
                 "adaptive-depth2-u32-chunk-msd",
                 sortForestByAdaptiveDepth2U32ChunkBitmaskLe512WithParent);

        // U32 full-clear comparator
        addEntry(reg, SortKind::AdaptiveDepth2U32ChunkMsdFullClearTailLinear32,
                 "adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32",
                 sortForestByAdaptiveDepth2U32ChunkFullClearWithParent,
                 SortCategory::Comparator);

        // U8 full-clear
        addEntry(reg, SortKind::AdaptiveDepth2U8ChunkMsdFullClearTailLinear32,
                 "adaptive-depth2-u8-chunk-msd-full-clear-tail-linear32",
                 sortForestByAdaptiveDepth2U8ChunkFullClearWithParent,
                 SortCategory::Comparator);

        // U8 bitmask-le512
        addEntry(reg,
                 SortKind::AdaptiveDepth2U8ChunkMsdBitmaskLe512TailLinear32,
                 "adaptive-depth2-u8-chunk-msd-bitmask-le512-tail-linear32",
                 sortForestByAdaptiveDepth2U8ChunkBitmaskLe512WithParent,
                 SortCategory::Comparator);

        // U16 full-clear
        addEntry(reg, SortKind::AdaptiveDepth2U16ChunkMsdFullClearTailLinear32,
                 "adaptive-depth2-u16-chunk-msd-full-clear-tail-linear32",
                 sortForestByAdaptiveDepth2U16ChunkFullClearWithParent,
                 SortCategory::Comparator);

        // U16 bitmask-le512
        addEntry(reg,
                 SortKind::AdaptiveDepth2U16ChunkMsdBitmaskLe512TailLinear32,
                 "adaptive-depth2-u16-chunk-msd-bitmask-le512-tail-linear32",
                 sortForestByAdaptiveDepth2U16ChunkBitmaskLe512WithParent,
                 SortCategory::Comparator);

        // U64 full-clear
        addEntry(reg, SortKind::AdaptiveDepth2U64ChunkMsdFullClearTailLinear32,
                 "adaptive-depth2-u64-chunk-msd-full-clear-tail-linear32",
                 sortForestByAdaptiveDepth2U64ChunkWithParent,
                 SortCategory::Comparator);

        // U64 tail-binary
        addEntry(reg, SortKind::AdaptiveDepth2U64ChunkMsdFullClearTailBinary32,
                 "adaptive-depth2-u64-chunk-msd-full-clear-tail-binary32",
                 sortForestByAdaptiveDepth2U64ChunkBinarySmallWithParent,
                 SortCategory::Comparator);

        // Depth4 U32
        addEntry(reg, SortKind::AdaptiveDepth4U32ChunkMsdFullClearTailLinear32,
                 "adaptive-depth4-u32-chunk-msd-full-clear-tail-linear32",
                 sortForestByAdaptiveDepth4U32ChunkWithParent,
                 SortCategory::Comparator);

        // Tail Experiments
        FS_ADD_TAIL_LINEAR(16);
        FS_ADD_TAIL_LINEAR(48);
        addTailExperimentEntry<
            SortKind::AdaptiveDepth2U32ChunkMsdBitmaskLe512TailBinary32,
            BinarySmallSorter, 32>(
            reg, "adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-binary32");
        FS_ADD_TAIL_EXPONENTIAL(16);
        FS_ADD_TAIL_EXPONENTIAL(32);
        FS_ADD_TAIL_EXPONENTIAL(48);
        FS_ADD_TAIL_BRANCHLESS_BITWISE(16);
        FS_ADD_TAIL_BRANCHLESS_BITWISE(32);
        FS_ADD_TAIL_BRANCHLESS_BITWISE(48);

        // Parameterized bitmask thresholds
        FS_ADD_BITMASK_THRESHOLD(128);
        FS_ADD_BITMASK_THRESHOLD(256);
        FS_ADD_BITMASK_THRESHOLD(1024);
        FS_ADD_BITMASK_THRESHOLD(4096);

        // Parameterized range ladders
        FS_ADD_RANGE_LADDER(1024, 16384);
        FS_ADD_RANGE_LADDER(2048, 32768);
        FS_ADD_RANGE_LADDER(4096, 65536);

        // Undefine local macros before returning
#undef FS_ADD_RANGE_LADDER
#undef FS_ADD_BITMASK_THRESHOLD
#undef FS_ADD_TAIL_BRANCHLESS_BITWISE
#undef FS_ADD_TAIL_EXPONENTIAL
#undef FS_ADD_TAIL_LINEAR
        return reg;
    }();
    return registry;
}

inline std::vector<SortKind> allSortKinds() {
    std::vector<SortKind> sorts;
    sorts.reserve(getSortRegistry().size());
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.includeByDefault) {
            sorts.push_back(entry.kind);
        }
    }
    return sorts;
}

inline std::string_view sortName(SortKind sortKind) {
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.kind == sortKind && entry.category != SortCategory::Alias) {
            return entry.name;
        }
    }
    return "unknown";
}

inline std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex) {
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.kind == sortKind) {
            return entry.sortFunction(nodes, parentIndex);
        }
    }
    throw std::runtime_error("unknown sort algorithm");
}

inline SortKind parseSortKind(std::string_view value) {
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (value == entry.name) {
            return entry.kind;
        }
    }
    throw std::runtime_error("unknown sort algorithm: " + std::string(value));
}

inline void validateSortRegistry() {
    std::size_t aliasCount = 0;
    for (std::size_t entryIdx = 0; entryIdx < getSortRegistry().size();
         ++entryIdx) {
        const SortRegistryEntry &entry = getSortRegistry()[entryIdx];
        if (entry.name.empty()) {
            throw std::runtime_error("sort registry contains an empty name");
        }
        if (entry.sortFunction == nullptr) {
            throw std::runtime_error("sort registry contains a null function");
        }
        if (entry.category == SortCategory::Alias) {
            ++aliasCount;
            if (entry.name != "adaptive-depth2-u32-chunk-msd") {
                throw std::runtime_error(
                    "unauthorized alias in sort registry: " +
                    std::string(entry.name));
            }
        }
        for (std::size_t otherIdx = entryIdx + 1;
             otherIdx < getSortRegistry().size(); ++otherIdx) {
            if (entry.name == getSortRegistry()[otherIdx].name) {
                throw std::runtime_error(
                    "sort registry contains duplicate name: " +
                    std::string(entry.name));
            }
            if (entry.category != SortCategory::Alias &&
                getSortRegistry()[otherIdx].category != SortCategory::Alias &&
                entry.kind == getSortRegistry()[otherIdx].kind) {
                throw std::runtime_error(
                    "sort registry contains duplicate kind");
            }
        }
    }
    if (aliasCount > 1) {
        throw std::runtime_error("sort registry contains too many aliases");
    }
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
