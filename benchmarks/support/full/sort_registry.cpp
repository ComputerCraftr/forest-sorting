#include "forest_sorting/benchmark_support/full/sort_registry.hpp"
#include "forest_sorting/benchmark_support/full/adaptive_sort_variants.hpp"
#include "forest_sorting/benchmark_support/full/radix_policies.hpp"
#include "forest_sorting/benchmark_support/full/sort_baselines.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::benchmark_support {

namespace {

inline constexpr std::size_t sortRegistrySize =
    static_cast<std::size_t>(
        SortKind::
            Depth2FirstThenIdRangeLadderChunk8Le4096Chunk16Le65536BitmaskLe512) +
    1;

struct SortRegistryBuilder {
    std::array<SortRegistryEntry, sortRegistrySize> entries{};
    std::size_t size = 0;

    void push_back(SortRegistryEntry entry) {
        assert(size < entries.size());
        entries[size++] = entry;
    }

    std::array<SortRegistryEntry, sortRegistrySize> finish() const {
        assert(size == entries.size());
        return entries;
    }
};

} // namespace

inline bool defaultIncludeForKind(SortKind kind) {
    return kind == SortKind::GlobalIdPermutationThenDepthStable ||
           kind == SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512 ||
           kind == SortKind::Comparison;
}

inline void addEntry(SortRegistryBuilder &registry, SortKind kind,
                     std::string_view name, SortFunction sortFunc,
                     SortCategory category) {
    registry.push_back(
        {kind, name, sortFunc, nullptr, category, defaultIncludeForKind(kind)});
}

inline void
addOptionalIdPermutationEntry(SortRegistryBuilder &registry, SortKind kind,
                              std::string_view name,
                              OptionalIdPermutationSortFunction sortFunc) {
    registry.push_back({kind, name, nullptr, sortFunc, SortCategory::Production,
                        defaultIncludeForKind(kind)});
}

template <SortKind Kind, std::size_t Threshold>
void addBitmaskThresholdEntry(SortRegistryBuilder &registry,
                              std::string_view name) {
    const auto func =
        sortForestByDepth2FirstThenIdMsdChunk32BitmaskLeWithParent<Threshold>;
    addEntry(registry, Kind, name, func, SortCategory::CounterPolicyExperiment);
}

template <SortKind FullClearKind, SortKind BitmaskKind, std::size_t Chunk8Max,
          std::size_t Chunk16Max>
void addRangeLadderEntries(SortRegistryBuilder &registry,
                           std::string_view fullClearName,
                           std::string_view bitmaskName) {
    addEntry(registry, FullClearKind, fullClearName,
             sortForestByDepth2FirstThenIdMsdLadderWithParent<
                 Chunk8Max, Chunk16Max, FullClearIdCountPolicy>,
             SortCategory::RangeLadderExperiment);
    addEntry(registry, BitmaskKind, bitmaskName,
             sortForestByDepth2FirstThenIdMsdLadderWithParent<
                 Chunk8Max, Chunk16Max,
                 TouchedIdCountPolicy<
                     detail::production_touched_count_max_range_size>>,
             SortCategory::RangeLadderExperiment);
}

std::span<const SortRegistryEntry> sortRegistry() noexcept {
    static const std::array registry = []() {
        SortRegistryBuilder reg;

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
        addEntry(reg, SortKind::CompositeDepth2IdByteMsdPartitionCore,
                 "composite-depth2-id-byte-msd-partition-core",
                 sortForestByCompositeDepth2IdByteMsdPartitionCoreWithParent,
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
        // Production global-ID-first row. In pipeline benchmarks this reuses
        // the parent builder's retained ID permutation, so the sort label must
        // not claim a radix chunk width. The parent row owns the radix width
        // being benchmarked.
        addOptionalIdPermutationEntry(
            reg, SortKind::GlobalIdPermutationThenDepthStable,
            "global-id-permutation-then-depth-stable",
            sortForestByTrustedGlobalIdPermutationThenDepthStable);

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

        addBitmaskThresholdEntry<
            SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe128, 128>(
            reg, "depth2-first-then-id-msd-chunk32-bitmask-le128");
        addBitmaskThresholdEntry<
            SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe256, 256>(
            reg, "depth2-first-then-id-msd-chunk32-bitmask-le256");
        addBitmaskThresholdEntry<
            SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe1024, 1024>(
            reg, "depth2-first-then-id-msd-chunk32-bitmask-le1024");
        addBitmaskThresholdEntry<
            SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe4096, 4096>(
            reg, "depth2-first-then-id-msd-chunk32-bitmask-le4096");
        addRangeLadderEntries<
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le1024Chunk16Le16384FullClear,
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le1024Chunk16Le16384BitmaskLe512,
            1024, 16384>(
            reg,
            "depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-"
            "chunk32-otherwise-full-clear",
            "depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-"
            "chunk32-otherwise-bitmask-le512");
        addRangeLadderEntries<
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le2048Chunk16Le32768FullClear,
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le2048Chunk16Le32768BitmaskLe512,
            2048, 32768>(
            reg,
            "depth2-first-then-id-range-ladder-chunk8-le2048-chunk16-le32768-"
            "chunk32-otherwise-full-clear",
            "depth2-first-then-id-range-ladder-chunk8-le2048-chunk16-le32768-"
            "chunk32-otherwise-bitmask-le512");
        addRangeLadderEntries<
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le4096Chunk16Le65536FullClear,
            SortKind::
                Depth2FirstThenIdRangeLadderChunk8Le4096Chunk16Le65536BitmaskLe512,
            4096, 65536>(
            reg,
            "depth2-first-then-id-range-ladder-chunk8-le4096-chunk16-le65536-"
            "chunk32-otherwise-full-clear",
            "depth2-first-then-id-range-ladder-chunk8-le4096-chunk16-le65536-"
            "chunk32-otherwise-bitmask-le512");
        return reg.finish();
    }();
    return registry;
}

std::vector<SortKind> defaultSortKinds() {
    std::vector<SortKind> sorts;
    sorts.reserve(sortRegistry().size());
    for (const SortRegistryEntry &entry : sortRegistry()) {
        if (entry.includeByDefault) {
            sorts.push_back(entry.kind);
        }
    }
    return sorts;
}

std::vector<SortKind> registeredSortKinds() {
    std::vector<SortKind> sorts;
    sorts.reserve(sortRegistry().size());
    for (const SortRegistryEntry &entry : sortRegistry()) {
        sorts.push_back(entry.kind);
    }
    return sorts;
}

std::string_view sortName(SortKind sortKind) noexcept {
    for (const SortRegistryEntry &entry : sortRegistry()) {
        if (entry.kind == sortKind) {
            return entry.name;
        }
    }
    return "unknown";
}

std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex,
                  const std::vector<std::size_t> *idPermutation) {
    for (const SortRegistryEntry &entry : sortRegistry()) {
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

std::vector<Node>
sortForestForKind(SortKind sortKind, const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex) {
    return sortForestForKind(sortKind, nodes, parentIndex, nullptr);
}

SortKind parseSortKind(std::string_view value) {
    for (const SortRegistryEntry &entry : sortRegistry()) {
        if (value == entry.name) {
            return entry.kind;
        }
    }
    throw std::runtime_error("unknown sort algorithm: " + std::string(value));
}

inline bool containsStaleRadixChunkLabelPattern(std::string_view name) {
    return name.find("-u8-msd") != std::string_view::npos ||
           name.find("-u16-msd") != std::string_view::npos ||
           name.find("-u32-msd") != std::string_view::npos ||
           name.find("-u64-msd") != std::string_view::npos ||
           name.find("global-id-u32-msd") != std::string_view::npos ||
           name.find("global-id-msd-chunk32-radix") != std::string_view::npos ||
           name.find("range-ladder-u8-le") != std::string_view::npos ||
           name.find("-u16-le") != std::string_view::npos;
}

void validateSortRegistry() {
    for (std::size_t entryIdx = 0; entryIdx < sortRegistry().size();
         ++entryIdx) {
        const SortRegistryEntry &entry = sortRegistry()[entryIdx];
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
            entry.kind != SortKind::GlobalIdPermutationThenDepthStable &&
            entry.kind != SortKind::Depth2FirstThenIdMsdChunk32BitmaskLe512 &&
            entry.kind != SortKind::Comparison) {
            throw std::runtime_error(
                std::string(entry.name) +
                " is not part of the curated default sort set");
        }
        // This is a broad active-registry invariant, not the authoritative
        // exact removed-label list tested by the CLI rejection test.
        if (containsStaleRadixChunkLabelPattern(entry.name)) {
            throw std::runtime_error(
                "sort registry contains stale radix chunk label: " +
                std::string(entry.name));
        }
        if (entry.name.find("lowcopy") != std::string_view::npos) {
            throw std::runtime_error(
                "sort registry contains a removed low-copy experiment: " +
                std::string(entry.name));
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
             otherIdx < sortRegistry().size(); ++otherIdx) {
            if (entry.name == sortRegistry()[otherIdx].name) {
                throw std::runtime_error(
                    "sort registry contains duplicate name: " +
                    std::string(entry.name));
            }
            if (entry.kind == sortRegistry()[otherIdx].kind) {
                throw std::runtime_error(
                    "sort registry contains duplicate kind");
            }
        }
    }
}

} // namespace forest_sorting::benchmark_support
