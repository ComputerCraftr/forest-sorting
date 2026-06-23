#ifndef FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
#define FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP

#include "adaptive_sort_variants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
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
    Alias,
};

enum class SortKind : uint8_t {
    Comparison,
    DenseDepth2BucketsThenIdLsd,
    CompositeDepth2IdLsd,
    DenseDepth2BucketsThenIdMsd,
    CompositeDepth2IdMsdCopyback,
    CompositeDepth2IdMsdLowcopyBranchy,
    CompositeDepth2IdMsdLowcopyFlattened,
    CompositeDepth2IdMsdLowcopyBatched,
    Depth2FirstThenIdU32MsdNoDense,
    Depth2FirstThenIdU32MsdBitmaskLe512,
    GlobalIdU32MsdRadixThenDepthStable,

    // Comparators
    Depth2FirstThenIdU8MsdFullClear,
    Depth2FirstThenIdU8MsdBitmaskLe512,
    Depth2FirstThenIdU16MsdFullClear,
    Depth2FirstThenIdU16MsdBitmaskLe512,
    Depth2FirstThenIdU32MsdFullClear,
    Depth2FirstThenIdU64MsdFullClear,
    Depth4FirstThenIdU32MsdFullClear,

    // Counter Policy Experiments
    Depth2FirstThenIdU32MsdBitmaskLe128,
    Depth2FirstThenIdU32MsdBitmaskLe256,
    Depth2FirstThenIdU32MsdBitmaskLe1024,
    Depth2FirstThenIdU32MsdBitmaskLe4096,

    // Range Ladder Experiments
    Depth2FirstThenIdRangeLadderU8Le1024U16Le16384FullClear,
    Depth2FirstThenIdRangeLadderU8Le2048U16Le32768FullClear,
    Depth2FirstThenIdRangeLadderU8Le4096U16Le65536FullClear,
    Depth2FirstThenIdRangeLadderU8Le1024U16Le16384BitmaskLe512,
    Depth2FirstThenIdRangeLadderU8Le2048U16Le32768BitmaskLe512,
    Depth2FirstThenIdRangeLadderU8Le4096U16Le65536BitmaskLe512,
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

template <std::size_t DepthPrefixBytes, std::size_t ChunkBytes,
          typename CountPolicy = detail::FullClearCounts,
          std::size_t SmallThreshold = detail::small_id_range_sort_threshold,
          typename SmallRangeSorter = LinearSmallSorter<SmallThreshold>,
          bool AllowDenseDepthGrouping = true>
inline std::vector<Node>
sortForestByAdaptiveChunkWrapper(const std::vector<Node> &nodes,
                                 const std::vector<std::size_t> &parentIndex) {
    return sortForestByAdaptiveChunkWithParent<DepthPrefixBytes, ChunkBytes,
                                               CountPolicy, SmallThreshold,
                                               SmallRangeSorter>(
        nodes, parentIndex, AllowDenseDepthGrouping);
}

inline bool defaultIncludeForKind(SortKind kind) {
    switch (kind) {
    case SortKind::GlobalIdU32MsdRadixThenDepthStable:
    case SortKind::Depth2FirstThenIdU32MsdBitmaskLe512:
    case SortKind::Comparison:
        return true;
    default:
        return false;
    }
}

inline void addEntry(std::vector<SortRegistryEntry> &registry, SortKind kind,
                     std::string_view name, SortFunction sortFunc,
                     SortCategory category) {
    registry.push_back(
        {kind, name, sortFunc, nullptr, category, defaultIncludeForKind(kind)});
}

inline void
addOptionalIdPermutationEntry(std::vector<SortRegistryEntry> &registry,
                              SortKind kind, std::string_view name,
                              OptionalIdPermutationSortFunction sortFunc) {
    registry.push_back({kind, name, nullptr, sortFunc, SortCategory::Production,
                        defaultIncludeForKind(kind)});
}

inline void addAlias(std::vector<SortRegistryEntry> &registry, SortKind kind,
                     std::string_view name, SortFunction sortFunc) {
    registry.push_back(
        {kind, name, sortFunc, nullptr, SortCategory::Alias, false});
}

template <SortKind Kind, std::size_t Threshold>
void addBitmaskThresholdEntry(std::vector<SortRegistryEntry> &registry,
                              std::string_view name) {
    const auto func =
        sortForestByDepth2FirstThenIdU32MsdBitmaskLeWithParent<Threshold>;
    addEntry(registry, Kind, name, func, SortCategory::CounterPolicyExperiment);
}

template <SortKind FullClearKind, SortKind BitmaskLe512Kind, std::size_t U8Max,
          std::size_t U16Max>
void addRangeLadderEntries(std::vector<SortRegistryEntry> &registry,
                           std::string_view fullClearName,
                           std::string_view bitmaskLe512Name) {
    const auto fcFunc = sortForestByDepth2FirstThenIdRangeLadderWithParent<
        U8Max, U16Max, detail::FullClearCounts>;
    const auto bmFunc = sortForestByDepth2FirstThenIdRangeLadderWithParent<
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
#define FS_ADD_BITMASK_THRESHOLD(Threshold)                                    \
    addBitmaskThresholdEntry<                                                  \
        SortKind::Depth2FirstThenIdU32MsdBitmaskLe##Threshold, Threshold>(     \
        reg, "depth2-first-then-id-u32-msd-bitmask-le" #Threshold)

#define FS_ADD_RANGE_LADDER(U8Max, U16Max)                                        \
    addRangeLadderEntries<                                                        \
        SortKind::                                                                \
            Depth2FirstThenIdRangeLadderU8Le##U8Max##U16Le##U16Max##FullClear,    \
        SortKind::                                                                \
            Depth2FirstThenIdRangeLadderU8Le##U8Max##U16Le##U16Max##BitmaskLe512, \
        U8Max, U16Max>(reg,                                                       \
                       "depth2-first-then-id-range-ladder-u8-le" #U8Max           \
                       "-u16-le" #U16Max "-full-clear",                           \
                       "depth2-first-then-id-range-ladder-u8-le" #U8Max           \
                       "-u16-le" #U16Max "-bitmask-le512")

        // Support baselines and implementation comparators. These are explicit
        // opt-in rows unless defaultIncludeForKind names them directly.
        addEntry(reg, SortKind::Comparison, "comparison",
                 sortForestByComparisonWithParent, SortCategory::Baseline);
        addEntry(reg, SortKind::DenseDepth2BucketsThenIdLsd,
                 "dense-depth2-buckets-then-id-lsd",
                 sortForestByDenseDepth2BucketsThenIdLsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdLsd, "composite-depth2-id-lsd",
                 sortForestByCompositeDepth2IdLsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::DenseDepth2BucketsThenIdMsd,
                 "dense-depth2-buckets-then-id-msd",
                 sortForestByDenseDepth2BucketsThenIdMsdWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdMsdCopyback,
                 "composite-depth2-id-msd-copyback",
                 sortForestByCompositeDepth2IdMsdCopybackWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdMsdLowcopyBranchy,
                 "composite-depth2-id-msd-lowcopy-branchy",
                 sortForestByCompositeDepth2IdMsdLowcopyBranchyWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdMsdLowcopyFlattened,
                 "composite-depth2-id-msd-lowcopy-flattened",
                 sortForestByCompositeDepth2IdMsdLowcopyFlattenedWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdMsdLowcopyBatched,
                 "composite-depth2-id-msd-lowcopy-batched",
                 sortForestByCompositeDepth2IdMsdLowcopyBatchedWithParent,
                 SortCategory::Baseline);

        // Comparators
        addEntry(reg, SortKind::Depth2FirstThenIdU32MsdNoDense,
                 "depth2-first-then-id-u32-msd-no-dense",
                 sortForestByDepth2FirstThenIdU32MsdNoDenseWithParent,
                 SortCategory::Comparator);

        // Default depth-first comparator retained for production A/B evidence.
        addEntry(
            reg, SortKind::Depth2FirstThenIdU32MsdBitmaskLe512,
            "depth2-first-then-id-u32-msd-bitmask-le512",
            sortForestByDepth2FirstThenIdU32MsdBitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);
        // Production global-ID-first row.
        addOptionalIdPermutationEntry(
            reg, SortKind::GlobalIdU32MsdRadixThenDepthStable,
            "global-id-u32-msd-radix-then-depth-stable",
            sortForestByGlobalIdU32MsdRadixThenDepthStable);

        // Deprecated Production Aliases (Legacy alias compatibility)
        addAlias(reg, SortKind::Depth2FirstThenIdU32MsdBitmaskLe512,
                 "depth2-first-then-id-u32-msd",
                 sortForestByDepth2FirstThenIdU32MsdWithParent);

        // U32 full-clear comparator
        addEntry(
            reg, SortKind::Depth2FirstThenIdU32MsdFullClear,
            "depth2-first-then-id-u32-msd-full-clear",
            sortForestByDepth2FirstThenIdU32MsdFullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // U8 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdU8MsdFullClear,
            "depth2-first-then-id-u8-msd-full-clear",
            sortForestByDepth2FirstThenIdU8MsdFullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // U8 bitmask-le512
        addEntry(
            reg, SortKind::Depth2FirstThenIdU8MsdBitmaskLe512,
            "depth2-first-then-id-u8-msd-bitmask-le512",
            sortForestByDepth2FirstThenIdU8MsdBitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);

        // U16 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdU16MsdFullClear,
            "depth2-first-then-id-u16-msd-full-clear",
            sortForestByDepth2FirstThenIdU16MsdFullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // U16 bitmask-le512
        addEntry(
            reg, SortKind::Depth2FirstThenIdU16MsdBitmaskLe512,
            "depth2-first-then-id-u16-msd-bitmask-le512",
            sortForestByDepth2FirstThenIdU16MsdBitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);

        // U64 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdU64MsdFullClear,
            "depth2-first-then-id-u64-msd-full-clear",
            sortForestByDepth2FirstThenIdU64MsdFullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // Depth4 U32
        addEntry(
            reg, SortKind::Depth4FirstThenIdU32MsdFullClear,
            "depth4-first-then-id-u32-msd-full-clear",
            sortForestByDepth4FirstThenIdU32MsdFullClearTailLinear32WithParent,
            SortCategory::Comparator);

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
        return reg;
    }();
    return registry;
}

inline std::vector<SortKind> defaultSortKinds() {
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
                  const std::vector<std::size_t> &parentIndex,
                  const std::vector<std::size_t> *idPermutation) {
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.kind == sortKind) {
            if (entry.optionalIdPermutationSortFunction != nullptr) {
                return entry.optionalIdPermutationSortFunction(
                    nodes, parentIndex, idPermutation);
            }
            return entry.sortFunction(nodes, parentIndex);
        }
    }
    throw std::runtime_error("unknown sort algorithm");
}

inline std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex) {
    return sortForestForKind(sortKind, nodes, parentIndex, nullptr);
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
        if ((entry.sortFunction == nullptr) ==
            (entry.optionalIdPermutationSortFunction == nullptr)) {
            throw std::runtime_error(
                "sort registry entry must contain exactly one function");
        }
        if (entry.category == SortCategory::Alias) {
            ++aliasCount;
            if (entry.name != "depth2-first-then-id-u32-msd") {
                throw std::runtime_error(
                    "unauthorized alias in sort registry: " +
                    std::string(entry.name));
            }
        }
        if (parseSortKind(entry.name) != entry.kind) {
            throw std::runtime_error(
                "parseSortKind failed to parse registered name: " +
                std::string(entry.name));
        }
        if (entry.includeByDefault &&
            entry.kind != SortKind::GlobalIdU32MsdRadixThenDepthStable &&
            entry.kind != SortKind::Depth2FirstThenIdU32MsdBitmaskLe512 &&
            entry.kind != SortKind::Comparison) {
            throw std::runtime_error(
                std::string(entry.name) +
                " is not part of the curated default sort set");
        }
        if (entry.category == SortCategory::CounterPolicyExperiment ||
            entry.category == SortCategory::RangeLadderExperiment ||
            entry.kind == SortKind::Depth2FirstThenIdU16MsdFullClear ||
            entry.kind == SortKind::Depth2FirstThenIdU16MsdBitmaskLe512 ||
            entry.kind == SortKind::Depth2FirstThenIdU32MsdFullClear) {
            if (entry.includeByDefault) {
                throw std::runtime_error(std::string(entry.name) +
                                         " should be explicit opt-in");
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
