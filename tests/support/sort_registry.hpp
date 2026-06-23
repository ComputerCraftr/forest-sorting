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
};

enum class SortKind : uint8_t {
    Comparison,
    DenseDepth2BucketsThenIdLsd,
    CompositeDepth2IdLsd,
    DenseDepth2BucketsThenIdMsdChunk64FullClear,
    CompositeDepth2IdByteMsdCopyback,
    CompositeDepth2IdByteMsdLowcopyBranchy,
    CompositeDepth2IdByteMsdLowcopyFlattened,
    CompositeDepth2IdByteMsdLowcopyBatched,
    Depth2FirstThenIdMsdChunk32BitmaskLe512NoDense,
    Depth2FirstThenIdMsdChunk32BitmaskLe512,
    GlobalIdMsdChunk32RadixThenDepthStable,

    // Comparators
    Depth2FirstThenIdMsdChunk8FullClear,
    Depth2FirstThenIdMsdChunk8BitmaskLe512,
    Depth2FirstThenIdMsdChunk16FullClear,
    Depth2FirstThenIdMsdChunk16BitmaskLe512,
    Depth2FirstThenIdMsdChunk32FullClear,
    Depth2FirstThenIdMsdChunk64FullClear,
    Depth4FirstThenIdMsdChunk32FullClear,

    // Counter Policy Experiments
    Depth2FirstThenIdMsdChunk32BitmaskLe128,
    Depth2FirstThenIdMsdChunk32BitmaskLe256,
    Depth2FirstThenIdMsdChunk32BitmaskLe1024,
    Depth2FirstThenIdMsdChunk32BitmaskLe4096,

    // Range Ladder Experiments
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

inline bool defaultIncludeForKind(SortKind kind) {
    return kind == SortKind::GlobalIdMsdChunk32RadixThenDepthStable ||
           kind == SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512 ||
           kind == SortKind::Comparison;
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

template <SortKind Kind, std::size_t Threshold>
void addBitmaskThresholdEntry(std::vector<SortRegistryEntry> &registry,
                              std::string_view name) {
    const auto func =
        sortForestByDepth2FirstThenIdMsdChunk32BitmaskLeWithParent<Threshold>;
    addEntry(registry, Kind, name, func, SortCategory::CounterPolicyExperiment);
}

template <SortKind FullClearKind, SortKind BitmaskLe512Kind,
          std::size_t Chunk8Max, std::size_t Chunk16Max>
void addRangeLadderEntries(std::vector<SortRegistryEntry> &registry,
                           std::string_view fullClearName,
                           std::string_view bitmaskLe512Name) {
    const auto fcFunc = sortForestByDepth2FirstThenIdRangeLadderWithParent<
        Chunk8Max, Chunk16Max, detail::FullClearCounts>;
    const auto bmFunc = sortForestByDepth2FirstThenIdRangeLadderWithParent<
        Chunk8Max, Chunk16Max,
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
        SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe##Threshold, Threshold>( \
        reg, "depth2-first-then-id-msd-chunk32-bitmask-le" #Threshold)

#define FS_ADD_RANGE_LADDER(Chunk8Max, Chunk16Max)                                                \
    addRangeLadderEntries<                                                                        \
        SortKind::                                                                                \
            Depth2FirstThenIdRangeLadderChunk8Le##Chunk8Max##Chunk16Le##Chunk16Max##FullClear,    \
        SortKind::                                                                                \
            Depth2FirstThenIdRangeLadderChunk8Le##Chunk8Max##Chunk16Le##Chunk16Max##BitmaskLe512, \
        Chunk8Max, Chunk16Max>(                                                                   \
        reg,                                                                                      \
        "depth2-first-then-id-range-ladder-chunk8-le" #Chunk8Max                                  \
        "-chunk16-le" #Chunk16Max "-chunk32-otherwise-full-clear",                                \
        "depth2-first-then-id-range-ladder-chunk8-le" #Chunk8Max                                  \
        "-chunk16-le" #Chunk16Max "-chunk32-otherwise-bitmask-le512")

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
        addEntry(
            reg, SortKind::DenseDepth2BucketsThenIdMsdChunk64FullClear,
            "dense-depth2-buckets-then-id-msd-chunk64-full-clear",
            sortForestByDenseDepth2BucketsThenIdMsdChunk64FullClearWithParent,
            SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdByteMsdCopyback,
                 "composite-depth2-id-byte-msd-copyback",
                 sortForestByCompositeDepth2IdByteMsdCopybackWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdByteMsdLowcopyBranchy,
                 "composite-depth2-id-byte-msd-lowcopy-branchy",
                 sortForestByCompositeDepth2IdByteMsdLowcopyBranchyWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdByteMsdLowcopyFlattened,
                 "composite-depth2-id-byte-msd-lowcopy-flattened",
                 sortForestByCompositeDepth2IdByteMsdLowcopyFlattenedWithParent,
                 SortCategory::Baseline);
        addEntry(reg, SortKind::CompositeDepth2IdByteMsdLowcopyBatched,
                 "composite-depth2-id-byte-msd-lowcopy-batched",
                 sortForestByCompositeDepth2IdByteMsdLowcopyBatchedWithParent,
                 SortCategory::Baseline);

        // Comparators
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512NoDense,
            "depth2-first-then-id-msd-chunk32-bitmask-le512-no-dense",
            sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512NoDenseWithParent,
            SortCategory::Comparator);

        // Default depth-first comparator retained for production A/B evidence.
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512,
            "depth2-first-then-id-msd-chunk32-bitmask-le512",
            sortForestByDepth2FirstThenIdMsdChunk32BitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);
        // Production global-ID-first row.
        addOptionalIdPermutationEntry(
            reg, SortKind::GlobalIdMsdChunk32RadixThenDepthStable,
            "global-id-msd-chunk32-radix-then-depth-stable",
            sortForestByGlobalIdMsdChunk32RadixThenDepthStable);

        // Chunk32 full-clear comparator
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk32FullClear,
            "depth2-first-then-id-msd-chunk32-full-clear",
            sortForestByDepth2FirstThenIdMsdChunk32FullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // Chunk8 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk8FullClear,
            "depth2-first-then-id-msd-chunk8-full-clear",
            sortForestByDepth2FirstThenIdMsdChunk8FullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // Chunk8 bitmask-le512
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk8BitmaskLe512,
            "depth2-first-then-id-msd-chunk8-bitmask-le512",
            sortForestByDepth2FirstThenIdMsdChunk8BitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);

        // Chunk16 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk16FullClear,
            "depth2-first-then-id-msd-chunk16-full-clear",
            sortForestByDepth2FirstThenIdMsdChunk16FullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // Chunk16 bitmask-le512
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk16BitmaskLe512,
            "depth2-first-then-id-msd-chunk16-bitmask-le512",
            sortForestByDepth2FirstThenIdMsdChunk16BitmaskLe512TailLinear32WithParent,
            SortCategory::Comparator);

        // Chunk64 full-clear
        addEntry(
            reg, SortKind::Depth2FirstThenIdMsdChunk64FullClear,
            "depth2-first-then-id-msd-chunk64-full-clear",
            sortForestByDepth2FirstThenIdMsdChunk64FullClearTailLinear32WithParent,
            SortCategory::Comparator);

        // Depth4 chunk32
        addEntry(
            reg, SortKind::Depth4FirstThenIdMsdChunk32FullClear,
            "depth4-first-then-id-msd-chunk32-full-clear",
            sortForestByDepth4FirstThenIdMsdChunk32FullClearTailLinear32WithParent,
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
        if (entry.kind == sortKind) {
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
        if (parseSortKind(entry.name) != entry.kind) {
            throw std::runtime_error(
                "parseSortKind failed to parse registered name: " +
                std::string(entry.name));
        }
        if (entry.includeByDefault &&
            entry.kind != SortKind::GlobalIdMsdChunk32RadixThenDepthStable &&
            entry.kind != SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512 &&
            entry.kind != SortKind::Comparison) {
            throw std::runtime_error(
                std::string(entry.name) +
                " is not part of the curated default sort set");
        }
        if (entry.category == SortCategory::CounterPolicyExperiment ||
            entry.category == SortCategory::RangeLadderExperiment ||
            entry.kind == SortKind::Depth2FirstThenIdMsdChunk16FullClear ||
            entry.kind == SortKind::Depth2FirstThenIdMsdChunk16BitmaskLe512 ||
            entry.kind == SortKind::Depth2FirstThenIdMsdChunk32FullClear) {
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
            if (entry.kind == getSortRegistry()[otherIdx].kind) {
                throw std::runtime_error(
                    "sort registry contains duplicate kind");
            }
        }
    }
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SORT_REGISTRY_HPP
