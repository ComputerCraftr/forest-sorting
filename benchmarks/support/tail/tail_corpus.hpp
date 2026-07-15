#ifndef FOREST_SORTING_SUPPORT_TAIL_CORPUS_HPP
#define FOREST_SORTING_SUPPORT_TAIL_CORPUS_HPP

#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace forest_sorting::test_support {

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

inline void appendTail(TailCorpus &corpus, const std::vector<UInt128> &ids) {
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

inline void reservoirSelectTail(std::vector<std::vector<UInt128>> &selected,
                                std::vector<UInt128> tail,
                                std::size_t observedTailCount,
                                std::size_t maxTailCount,
                                std::mt19937_64 &rng) {
    if (selected.size() < maxTailCount) {
        selected.push_back(std::move(tail));
        return;
    }
    const std::size_t replacement = static_cast<std::size_t>(
        rng() % static_cast<uint64_t>(observedTailCount));
    if (replacement < maxTailCount) {
        selected[replacement] = std::move(tail);
    }
}

template <typename IdForIndex>
std::vector<std::vector<UInt128>>
captureProductionMsdTails(std::size_t idCount, IdForIndex idForIndex,
                          std::size_t maxTailCount, uint64_t reservoirSeed) {
    std::vector<std::size_t> order(idCount);
    std::iota(order.begin(), order.end(), std::size_t{0});
    detail::IdMsdChunkSortWorkspace<detail::production_id_radix_chunk_bytes,
                                    detail::ProductionIdCountPolicy>
        workspace;
    std::vector<std::vector<UInt128>> selected;
    selected.reserve(std::min(idCount, maxTailCount));
    std::mt19937_64 rng(reservoirSeed);
    std::size_t observedTailCount = 0;

    auto collector = [&](std::vector<std::size_t> &sortOrder, auto sortId,
                         const auto &, std::size_t rangeBegin,
                         std::size_t rangeEnd) {
        const std::size_t rangeSize = rangeEnd - rangeBegin;
        if (rangeSize < 2 ||
            rangeSize > detail::small_id_range_sort_threshold) {
            return;
        }
        ++observedTailCount;
        std::vector<UInt128> ids;
        ids.reserve(rangeSize);
        for (std::size_t offset = rangeBegin; offset < rangeEnd; ++offset) {
            ids.push_back(sortId(sortOrder[offset]));
        }
        reservoirSelectTail(selected, std::move(ids), observedTailCount,
                            maxTailCount, rng);
    };

    detail::sortIndexRangeByIdMsdChunksWithSmallSorter<
        detail::production_id_radix_chunk_bytes,
        detail::ProductionIdCountPolicy, detail::small_id_range_sort_threshold>(
        order, idForIndex, UInt128Traits{}, 0, order.size(), 0, workspace,
        collector);
    return selected;
}

inline TailCorpus makeCapturedNodeIdTailCorpus(DatasetKind datasetKind,
                                               std::size_t sourceSize,
                                               std::size_t maxTailCount,
                                               uint32_t dataSeed) {
    const std::vector<Node> source =
        makeGeneratedForestForKind(datasetKind, sourceSize, dataSeed);
    auto idForIndex = [&](std::size_t nodeIndex) {
        return source[nodeIndex].id;
    };
    auto tails = captureProductionMsdTails(
        source.size(), idForIndex, maxTailCount,
        mixFixtureSeed(dataSeed, 0x7461696c2d6e6f64ULL));

    TailCorpus corpus{"captured-node-ids",
                      std::string(datasetName(datasetKind)), source.size()};
    for (const auto &tail : tails) {
        appendTail(corpus, tail);
    }
    return corpus;
}

inline TailCorpus makeCapturedParentQueryTailCorpus(DatasetKind datasetKind,
                                                    std::size_t sourceSize,
                                                    std::size_t maxTailCount,
                                                    uint32_t dataSeed) {
    const std::vector<Node> source =
        makeGeneratedForestForKind(datasetKind, sourceSize, dataSeed);
    std::vector<UInt128> parentIds;
    parentIds.reserve(source.size());
    for (const Node &node : source) {
        if (!detail::isParentSentinel(UInt128NodeTraits{}, node.parentId)) {
            parentIds.push_back(node.parentId);
        }
    }
    auto idForIndex = [&](std::size_t queryIndex) {
        return parentIds[queryIndex];
    };
    auto tails = captureProductionMsdTails(
        parentIds.size(), idForIndex, maxTailCount,
        mixFixtureSeed(dataSeed, 0x7461696c2d717279ULL));

    TailCorpus corpus{"captured-parent-queries",
                      std::string(datasetName(datasetKind)), source.size()};
    for (const auto &tail : tails) {
        appendTail(corpus, tail);
    }
    return corpus;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_TAIL_CORPUS_HPP
