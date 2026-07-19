#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_CORPUS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_CORPUS_HPP

#include "forest_sorting/benchmark_support/common/benchmark_cli.hpp"
#include "forest_sorting/benchmark_support/common/uint128_fixtures.hpp"
#include "forest_sorting/benchmark_support/tail/tail_execution.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace forest_sorting::benchmark_support {

struct TailRange {
    std::size_t begin;
    std::size_t end;
};

struct TailCorpus {
    std::string workload;
    std::string pattern;
    std::size_t sourceSize = 0;
    std::vector<Node> nodes;
    std::vector<TailRange> ranges;
    std::size_t minTailSize = 0;
    std::size_t maxTailSize = 0;

    TailCorpus(std::string workloadName, std::string patternName,
               std::size_t sourceNodeCount)
        : workload(std::move(workloadName)), pattern(std::move(patternName)),
          sourceSize(sourceNodeCount) {}
};

inline void appendTail(TailCorpus &corpus, std::span<const UInt128> ids) {
    if (ids.empty()) {
        return;
    }
    const std::size_t begin = corpus.nodes.size();
    for (UInt128 nodeId : ids) {
        corpus.nodes.push_back(Node{nodeId, 0});
    }
    corpus.ranges.push_back(TailRange{begin, corpus.nodes.size()});
    corpus.minTailSize = std::min(
        corpus.minTailSize == 0 ? ids.size() : corpus.minTailSize, ids.size());
    corpus.maxTailSize = std::max(corpus.maxTailSize, ids.size());
}

struct CapturedTailSample {
    std::array<UInt128, detail::small_id_range_sort_threshold> ids{};
    std::size_t size = 0;

    friend bool operator==(const CapturedTailSample &,
                           const CapturedTailSample &) = default;
};

enum class CaptureOwner : uint8_t {
    SourceForest,
    ParentQueries,
    RadixOrder,
    RadixWorkspace
};

enum class CapturePhase : uint8_t {
    ReservoirComplete,
    FinalCorpusConstructionBegin,
    FinalCorpusConstructed
};

struct NoopCaptureLifecycleObserver {
    static constexpr void ownerConstructed(CaptureOwner owner) noexcept {
        (void)owner;
    }

    static constexpr void ownerDestroyed(CaptureOwner owner) noexcept {
        (void)owner;
    }

    static constexpr void checkpoint(CapturePhase phase) noexcept {
        (void)phase;
    }
};

template <CaptureOwner Owner, typename Observer> class CaptureOwnerGuard {
  public:
    explicit CaptureOwnerGuard(Observer &observer) noexcept
        : observer_(observer) {
        static_assert(noexcept(observer_.ownerConstructed(Owner)));
        static_assert(noexcept(observer_.ownerDestroyed(Owner)));
    }

    CaptureOwnerGuard(const CaptureOwnerGuard &) = delete;
    CaptureOwnerGuard &operator=(const CaptureOwnerGuard &) = delete;

    ~CaptureOwnerGuard() noexcept {
        if (armed_) {
            observer_.ownerDestroyed(Owner);
        }
    }

    void arm() noexcept {
        armed_ = true;
        observer_.ownerConstructed(Owner);
    }

  private:
    Observer &observer_;
    bool armed_ = false;
};

inline std::optional<std::size_t>
selectReservoirSlot(std::size_t selectedCount, std::size_t observedTailCount,
                    std::size_t maxTailCount, std::mt19937_64 &rng) {
    if (selectedCount < maxTailCount) {
        return selectedCount;
    }
    const std::size_t replacement = static_cast<std::size_t>(
        rng() % static_cast<uint64_t>(observedTailCount));
    if (replacement < maxTailCount) {
        return replacement;
    }
    return std::nullopt;
}

template <typename IdForIndex> struct TerminalTailCollector {
    IdForIndex idForIndex;
    std::vector<CapturedTailSample> &selected;
    std::size_t maxTailCount;
    std::mt19937_64 &rng;
    std::size_t &observedTailCount;

    void observe(std::span<const std::size_t> indices,
                 std::size_t byteOffset) const {
        (void)byteOffset;
        if (indices.size() > detail::small_id_range_sort_threshold) {
            throw std::logic_error("captured terminal range exceeds capacity");
        }
        ++observedTailCount;
        const std::optional<std::size_t> selectedSlot = selectReservoirSlot(
            selected.size(), observedTailCount, maxTailCount, rng);
        if (!selectedSlot.has_value()) {
            return;
        }

        CapturedTailSample sample;
        sample.size = indices.size();
        for (std::size_t index = 0; index < indices.size(); ++index) {
            sample.ids[index] = idForIndex(indices[index]);
        }
        if (*selectedSlot == selected.size()) {
            selected.push_back(sample);
        } else {
            selected[*selectedSlot] = sample;
        }
    }
};

template <typename IdForIndex, typename LifecycleObserver>
std::vector<CapturedTailSample>
captureProductionMsdTails(std::size_t idCount, IdForIndex idForIndex,
                          std::size_t maxTailCount, uint64_t reservoirSeed,
                          LifecycleObserver &lifecycleObserver) {
    CaptureOwnerGuard<CaptureOwner::RadixOrder, LifecycleObserver> orderGuard(
        lifecycleObserver);
    std::vector<std::size_t> order(idCount);
    orderGuard.arm();
    std::iota(order.begin(), order.end(), std::size_t{0});
    CaptureOwnerGuard<CaptureOwner::RadixWorkspace, LifecycleObserver>
        workspaceGuard(lifecycleObserver);
    detail::IdMsdChunkSortWorkspace<detail::production_id_radix_chunk_bytes,
                                    detail::ProductionIdCountPolicy>
        workspace;
    workspaceGuard.arm();
    std::vector<CapturedTailSample> selected;
    selected.reserve(std::min(idCount, maxTailCount));
    std::mt19937_64 rng(reservoirSeed);
    std::size_t observedTailCount = 0;

    TerminalTailCollector<IdForIndex> collector{
        idForIndex, selected, maxTailCount, rng, observedTailCount};
    detail::sortIndexRangeByIdMsdChunks<detail::production_id_radix_chunk_bytes,
                                        detail::ProductionIdCountPolicy,
                                        detail::small_id_range_sort_threshold>(
        order, idForIndex, UInt128Traits{}, 0, order.size(), 0, workspace,
        collector);
    lifecycleObserver.checkpoint(CapturePhase::ReservoirComplete);
    return selected;
}

template <typename IdForIndex>
std::vector<CapturedTailSample>
captureProductionMsdTails(std::size_t idCount, IdForIndex idForIndex,
                          std::size_t maxTailCount, uint64_t reservoirSeed) {
    NoopCaptureLifecycleObserver lifecycleObserver;
    return captureProductionMsdTails(idCount, idForIndex, maxTailCount,
                                     reservoirSeed, lifecycleObserver);
}

template <typename LifecycleObserver>
TailCorpus consumeCapturedTails(std::string workload, std::string pattern,
                                std::size_t sourceSize,
                                std::vector<CapturedTailSample> &tails,
                                LifecycleObserver &lifecycleObserver) {
    lifecycleObserver.checkpoint(CapturePhase::FinalCorpusConstructionBegin);
    TailCorpus corpus{std::move(workload), std::move(pattern), sourceSize};
    std::size_t nodeCount = 0;
    for (const CapturedTailSample &tail : tails) {
        if (tail.size > corpus.nodes.max_size() - nodeCount) {
            throw std::length_error("captured tail corpus is too large");
        }
        nodeCount += tail.size;
    }
    corpus.nodes.reserve(nodeCount);
    corpus.ranges.reserve(tails.size());
    for (CapturedTailSample &tail : tails) {
        appendTail(corpus,
                   std::span<const UInt128>{tail.ids.data(), tail.size});
        tail.size = 0;
    }
    lifecycleObserver.checkpoint(CapturePhase::FinalCorpusConstructed);
    return corpus;
}

template <typename LifecycleObserver = NoopCaptureLifecycleObserver>
TailCorpus
makeCapturedNodeIdTailCorpus(DatasetKind datasetKind, std::size_t sourceSize,
                             std::size_t maxTailCount, uint32_t dataSeed,
                             uint64_t reservoirSeed,
                             LifecycleObserver lifecycleObserver = {}) {
    std::vector<CapturedTailSample> tails;
    {
        CaptureOwnerGuard<CaptureOwner::SourceForest, LifecycleObserver>
            sourceGuard(lifecycleObserver);
        const std::vector<Node> source =
            makeGeneratedForestForKind(datasetKind, sourceSize, dataSeed);
        sourceGuard.arm();
        auto idForIndex = [&](std::size_t nodeIndex) {
            return source[nodeIndex].id;
        };
        tails =
            captureProductionMsdTails(source.size(), idForIndex, maxTailCount,
                                      reservoirSeed, lifecycleObserver);
    }

    return consumeCapturedTails("captured-node-ids",
                                std::string(datasetName(datasetKind)),
                                sourceSize, tails, lifecycleObserver);
}

template <typename LifecycleObserver = NoopCaptureLifecycleObserver>
TailCorpus makeCapturedParentQueryTailCorpus(
    DatasetKind datasetKind, std::size_t sourceSize, std::size_t maxTailCount,
    uint32_t dataSeed, uint64_t reservoirSeed,
    LifecycleObserver lifecycleObserver = {}) {
    std::vector<CapturedTailSample> tails;
    {
        CaptureOwnerGuard<CaptureOwner::SourceForest, LifecycleObserver>
            sourceGuard(lifecycleObserver);
        const std::vector<Node> source =
            makeGeneratedForestForKind(datasetKind, sourceSize, dataSeed);
        sourceGuard.arm();
        CaptureOwnerGuard<CaptureOwner::ParentQueries, LifecycleObserver>
            parentQueriesGuard(lifecycleObserver);
        std::vector<UInt128> parentIds;
        parentQueriesGuard.arm();
        parentIds.reserve(source.size());
        for (const Node &node : source) {
            if (!detail::isParentSentinel(UInt128NodeTraits{}, node.parentId)) {
                parentIds.push_back(node.parentId);
            }
        }
        auto idForIndex = [&](std::size_t queryIndex) {
            return parentIds[queryIndex];
        };
        tails = captureProductionMsdTails(parentIds.size(), idForIndex,
                                          maxTailCount, reservoirSeed,
                                          lifecycleObserver);
    }

    return consumeCapturedTails("captured-parent-queries",
                                std::string(datasetName(datasetKind)),
                                sourceSize, tails, lifecycleObserver);
}

inline void makeIdsUnique(std::vector<UInt128> &ids, uint64_t highBase,
                          uint64_t lowBase) {
    for (std::size_t idIdx = 0; idIdx < ids.size(); ++idIdx) {
        ids[idIdx] = makeId(highBase, lowBase + idIdx + 1);
    }
}

inline TailCorpus makeSyntheticCorpus(Pattern pattern, std::size_t rangeSize,
                                      std::size_t tailCountLimit,
                                      uint64_t generationSeed) {
    TailCorpus corpus{"synthetic", std::string(patternName(pattern)), 0};
    const std::size_t totalNodeCount =
        checkedSizeProduct(rangeSize, tailCountLimit, "synthetic tail corpus");
    corpus.nodes.reserve(totalNodeCount);
    corpus.ranges.reserve(tailCountLimit);
    std::mt19937_64 rng(generationSeed);

    for (std::size_t rangeIdx = 0; rangeIdx < tailCountLimit; ++rangeIdx) {
        std::vector<UInt128> ids(rangeSize);
        const std::size_t firstGlobalIndex = rangeIdx * rangeSize;
        switch (pattern) {
        case Pattern::AlreadySorted:
        case Pattern::ReverseSorted:
        case Pattern::NearlySorted:
            makeIdsUnique(ids, 0, firstGlobalIndex);
            break;
        case Pattern::Random: {
            const uint64_t randomSeed =
                mixDeterministicUInt128Word(generationSeed ^ 0x72616e646f6dULL);
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                ids[idIdx] = makeRandomId(randomSeed, firstGlobalIndex + idIdx);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }
        case Pattern::SameHigh32: {
            constexpr uint64_t sharedHigh32 = 0x12345678ULL;
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                const uint64_t high =
                    (sharedHigh32 << 32U) |
                    static_cast<uint64_t>(firstGlobalIndex + idIdx + 1);
                ids[idIdx] = makeId(high, mixDeterministicUInt128Word(
                                              firstGlobalIndex + idIdx));
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }
        case Pattern::SameHigh64:
            makeIdsUnique(ids, 0x123456789abcdef0ULL, firstGlobalIndex);
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::LongCommonPrefix:
            makeIdsUnique(ids, 0x123456789abcdef0ULL,
                          0xfedcba9876540000ULL + firstGlobalIndex);
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::FirstByteDiffers:
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                const uint64_t high =
                    (static_cast<uint64_t>(idIdx + 1) << 56U) |
                    static_cast<uint64_t>(rangeIdx + 1);
                ids[idIdx] = makeId(high, 1);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::LastByteDiffers:
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                ids[idIdx] = makeId(0x123456789abcdef0ULL ^
                                        static_cast<uint64_t>(rangeIdx),
                                    0xfedcba9876540000ULL + idIdx + 1);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }

        if (pattern == Pattern::ReverseSorted) {
            std::reverse(ids.begin(), ids.end());
        } else if (pattern == Pattern::NearlySorted && rangeSize > 1) {
            const std::size_t swapCount =
                std::max<std::size_t>(1, rangeSize / 10);
            for (std::size_t swapIdx = 0; swapIdx < swapCount; ++swapIdx) {
                const std::size_t index =
                    static_cast<std::size_t>(rng() % (rangeSize - 1));
                std::swap(ids[index], ids[index + 1]);
            }
        }
        appendTail(corpus, ids);
    }
    return corpus;
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_CORPUS_HPP
