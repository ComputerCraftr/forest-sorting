#include "forest_sorting/benchmark_support/common/benchmark_execution.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_output.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_stats.hpp"
#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/tail/tail_benchmark_output.hpp"
#include "forest_sorting/benchmark_support/tail/tail_corpus.hpp"
#include "forest_sorting/benchmark_support/tail/tail_execution.hpp"
#include "forest_sorting/benchmark_support/tail/tail_sort_variants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "small_sort_test_types.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace forest_sorting::test_support;
using namespace forest_sorting::benchmark_support;
using forest_sorting::Node;
using forest_sorting::UInt128;

struct RecordingTerminalObserver {
    std::vector<std::vector<std::size_t>> *ranges;
    std::vector<std::size_t> *byteOffsets;

    void observe(std::span<const std::size_t> indices,
                 std::size_t byteOffset) const {
        ranges->emplace_back(indices.begin(), indices.end());
        byteOffsets->push_back(byteOffset);
    }
};

void test_tail_micro_output_schema_and_escaping() {
    constexpr std::array<std::string_view, 20> kExpectedNames = {
        "workload",
        "pattern",
        "source_size",
        "tail_size",
        "min_tail_size",
        "max_tail_size",
        "tail_count",
        "algorithm",
        "median_ns",
        "mean_ns",
        "min_ns",
        "stddev_ns",
        "max_ns",
        "ci95_low_ns",
        "ci95_high_ns",
        "delta_pct",
        "delta_ci95_low_pct",
        "delta_ci95_high_pct",
        "winner",
        "status"};
    std::vector<std::string> actualNames;
    actualNames.reserve(micro_output_field_count);
    visitMicroOutputSchema(
        [&](const MicroFieldDescriptor &field) {
            actualNames.emplace_back(field.name);
        },
        [&](const StatFieldDescriptor &field) {
            actualNames.push_back(statFieldName(field, "ns"));
        });
    require(actualNames.size() == kExpectedNames.size(),
            "tail micro output schema has the wrong field count");
    for (std::size_t fieldIdx = 0; fieldIdx < kExpectedNames.size();
         ++fieldIdx) {
        require(actualNames[fieldIdx] == kExpectedNames[fieldIdx],
                "tail micro output schema field order changed");
        for (std::size_t otherIdx = fieldIdx + 1;
             otherIdx < kExpectedNames.size(); ++otherIdx) {
            require(actualNames[fieldIdx] != actualNames[otherIdx],
                    "tail micro output schema contains duplicate fields");
        }
    }

    require(csvEscape("plain") == "plain", "plain CSV text was changed");
    require(csvEscape("a,b") == "\"a,b\"", "CSV comma was not escaped");
    require(csvEscape("a\"b") == "\"a\"\"b\"", "CSV quote was not escaped");
    require(csvEscape("a\nb") == "\"a\nb\"", "CSV newline was not escaped");
    require(csvEscape("a\rb") == "\"a\rb\"",
            "CSV carriage return was not escaped");
    require(jsonEscape("a\"b\\c\nd\re\tf") == "a\\\"b\\\\c\\nd\\re\\tf",
            "JSON string escaping was incorrect");
}

void test_tail_micro_output_renderers_share_normalized_schema() {
    struct TestMicroResult {
        std::string workload;
        std::string pattern;
        std::optional<std::size_t> sourceSize;
        std::optional<std::size_t> tailSize;
        std::size_t minTailSize;
        std::size_t maxTailSize;
        std::size_t tailCount;
        std::string algorithm;
        SampleStats stats;
        double deltaMedianPct;
        ConfidenceInterval deltaPctCi95;
        bool hasDelta;
        std::string winner;
        std::string status;
    };

    TestMicroResult baseline{
        "synthetic", "a,b", {},           8,    8,           8,   4, "linear",
        {},          12.0,  {10.0, 14.0}, true, "candidate", "ok"};
    baseline.stats = computeSampleStats({10.0, 12.0, 14.0});
    TestMicroResult failed{"synthetic", "failed",     {}, 8,   8,  8,
                           4,           "binary",     {}, 0.0, {}, false,
                           "none",      "sort failed"};
    const std::vector<MicroOutputRow> rows = {
        makeMicroOutputRow(baseline, "linear"),
        makeMicroOutputRow(failed, "linear")};

    require(rows[0].deltaPct == 0.0 && rows[0].winner == "baseline",
            "baseline row was not normalized to zero delta");
    require(!rows[1].stats && !rows[1].winner,
            "failed row retained unavailable metrics");

    std::ostringstream csv;
    printMicroDelimited(csv, rows, ',');
    require(csv.str().find("synthetic,\"a,b\",,8,8,8,4,linear") !=
                std::string::npos,
            "tail CSV did not escape string fields");
    require(csv.str().find(",0.0,0.0,0.0,baseline,ok") != std::string::npos,
            "tail CSV changed baseline delta formatting");
    const std::size_t failedRowBegin =
        csv.str().find("synthetic,failed,,8,8,8,4,binary");
    require(failedRowBegin != std::string::npos,
            "tail CSV omitted the failed row");
    const std::size_t failedRowEnd = csv.str().find('\n', failedRowBegin);
    const std::string failedRow =
        csv.str().substr(failedRowBegin, failedRowEnd - failedRowBegin);
    require(static_cast<std::size_t>(
                std::count(failedRow.begin(), failedRow.end(), ',')) ==
                micro_output_field_count - 1,
            "failed tail CSV row has the wrong field count");

    std::ostringstream json;
    printMicroJsonRows(json, rows);
    const std::size_t failedJsonBegin = json.str().find("\"failed\"");
    require(failedJsonBegin != std::string::npos,
            "tail JSON omitted the failed row");
    require(json.str().find("\"median_ns\"", failedJsonBegin) ==
                std::string::npos,
            "tail JSON emitted unavailable failed-row metrics");

    std::ostringstream table;
    printMicroTable(table, rows);
    require(table.str().find("timing_ci95_ns") != std::string::npos &&
                table.str().find("n/a") != std::string::npos,
            "compact tail table projection changed");
}

template <typename Sorter>
void requireSorterForAllTailSizes(Sorter sorter, std::string_view sorterName) {
    for (std::size_t rangeSize = 1;
         rangeSize <= forest_sorting::detail::small_id_range_sort_threshold;
         ++rangeSize) {
        std::vector<Node> nodes(rangeSize);
        for (std::size_t nodeIdx = 0; nodeIdx < rangeSize; ++nodeIdx) {
            nodes[nodeIdx] = {forest_sorting::makeId(
                                  0x123456789abcdef0ULL,
                                  static_cast<uint64_t>(rangeSize - nodeIdx)),
                              0};
        }
        std::vector<std::size_t> order(rangeSize);
        std::iota(order.begin(), order.end(), std::size_t{0});
        sorter(order, nodes, forest_sorting::UInt128NodeTraits{}, 0,
               order.size());
        for (std::size_t offset = 0; offset < order.size(); ++offset) {
            require(order[offset] == rangeSize - 1 - offset,
                    std::string(sorterName) +
                        " failed a reverse tail-size boundary");
        }
    }
}

void test_shell_gap_tail_sorters() {
    static_assert(validShellGapSequence<10, 4, 1>());
    static_assert(validShellGapSequence<3, 2, 1>());
    static_assert(validShellGapSequence<16, 7, 3, 1>());
    static_assert(!validShellGapSequence<10, 4>());
    static_assert(!validShellGapSequence<4, 4, 1>());
    static_assert(!validShellGapSequence<4, 0, 1>());

    requireSorterForAllTailSizes(LinearSmallSorter<32>{}, "linear");
    requireSorterForAllTailSizes(BinarySmallSorter<32>{}, "binary");
    requireSorterForAllTailSizes(ExponentialSmallSorter<32>{}, "exponential");
    requireSorterForAllTailSizes(BranchlessBitwiseSmallSorter<32>{},
                                 "branchless-bitwise");
    requireSorterForAllTailSizes(ShellGap10_4_1SmallSorter{},
                                 "shell-gap-10-4-1");
    requireSorterForAllTailSizes(ShellGap3_2_1SmallSorter{}, "shell-gap-3-2-1");
    requireSorterForAllTailSizes(ShellGap16_7_3_1SmallSorter{},
                                 "shell-gap-16-7-3-1");

    std::vector<CachedScratchTestNode> cachedNodes(32);
    for (std::size_t nodeIdx = 0; nodeIdx < cachedNodes.size(); ++nodeIdx) {
        cachedNodes[nodeIdx].id.bytes[0] = 0x12;
        cachedNodes[nodeIdx].id.bytes[1] = 0x34;
        cachedNodes[nodeIdx].id.bytes.back() =
            static_cast<uint8_t>(cachedNodes.size() - nodeIdx);
    }
    std::vector<std::size_t> order(cachedNodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    ShellGap10_4_1SmallSorter{}(order, cachedNodes, CachedScratchTestTraits{},
                                0, order.size());
    require(std::is_sorted(order.begin(), order.end(), std::greater<>()),
            "shell sorter failed cached long-prefix IDs");
}

void test_production_msd_tail_capture() {
    std::vector<UInt128> ids;
    ids.reserve(40);
    for (std::size_t idIdx = 0; idIdx < 8; ++idIdx) {
        ids.push_back(forest_sorting::makeId(
            (uint64_t{1} << 32U) | static_cast<uint64_t>(idIdx + 1), idIdx));
    }
    for (std::size_t idIdx = 0; idIdx < 32; ++idIdx) {
        ids.push_back(forest_sorting::makeId(
            (uint64_t{2} << 32U) | static_cast<uint64_t>(idIdx + 1), idIdx));
    }
    auto idForIndex = [&](std::size_t index) { return ids[index]; };
    auto tails = captureProductionMsdTails(ids.size(), idForIndex, 10, 123U);
    std::vector<std::size_t> sizes;
    sizes.reserve(tails.size());
    for (const auto &tail : tails) {
        sizes.push_back(tail.size);
    }
    std::sort(sizes.begin(), sizes.end());
    require((sizes == std::vector<std::size_t>{8, 32}),
            "production MSD tail capture missed terminal callback ranges");

    const auto capped =
        captureProductionMsdTails(ids.size(), idForIndex, 1, 123U);
    require(capped.size() == 1,
            "production MSD tail capture ignored its reservoir cap");
    require(capped ==
                captureProductionMsdTails(ids.size(), idForIndex, 1, 123U),
            "production MSD tail capture is not deterministic");

    std::size_t copiedIds = 0;
    std::vector<CapturedTailSample> noSelection;
    std::mt19937_64 noSelectionRng(std::random_device{}());
    std::size_t observedTailCount = 0;
    TerminalTailCollector noSelectionCollector{[&](std::size_t index) {
                                                   ++copiedIds;
                                                   return ids[index];
                                               },
                                               noSelection, 0, noSelectionRng,
                                               observedTailCount};
    const std::array<std::size_t, 2> discardedIndices = {0, 1};
    noSelectionCollector.observe(discardedIndices, 0);
    require(noSelection.empty() && copiedIds == 0 && observedTailCount == 1,
            "discarded tail observation copied candidate IDs");

    const TailCorpus firstNodes = makeCapturedNodeIdTailCorpus(
        DatasetKind::Random, 100000, 20, kDefaultBenchmarkDataSeed, 123U);
    const TailCorpus secondNodes = makeCapturedNodeIdTailCorpus(
        DatasetKind::Random, 100000, 20, kDefaultBenchmarkDataSeed, 123U);
    const TailCorpus parentQueries = makeCapturedParentQueryTailCorpus(
        DatasetKind::Random, 100000, 20, kDefaultBenchmarkDataSeed, 456U);
    const TailCorpus secondParentQueries = makeCapturedParentQueryTailCorpus(
        DatasetKind::Random, 100000, 20, kDefaultBenchmarkDataSeed, 456U);
    require(!firstNodes.nodes.empty() && !parentQueries.nodes.empty(),
            "capture independence fixture did not produce terminal tails");
    require(firstNodes.nodes.size() == secondNodes.nodes.size() &&
                firstNodes.ranges.size() == secondNodes.ranges.size(),
            "captured node reservoir shape is not deterministic");
    for (std::size_t index = 0; index < firstNodes.nodes.size(); ++index) {
        require(firstNodes.nodes[index].id == secondNodes.nodes[index].id,
                "captured node reservoir contents are not deterministic");
    }
    require(parentQueries.nodes.size() == secondParentQueries.nodes.size(),
            "captured parent-query reservoir shape is not deterministic");
    for (std::size_t index = 0; index < parentQueries.nodes.size(); ++index) {
        require(parentQueries.nodes[index].id ==
                    secondParentQueries.nodes[index].id,
                "captured parent-query reservoir contents are not "
                "deterministic");
    }
    require(firstNodes.workload != parentQueries.workload &&
                firstNodes.nodes.data() != parentQueries.nodes.data(),
            "node and parent-query capture reservoirs share ownership");
}

void test_synthetic_tail_patterns_are_unique() {
    for (Pattern pattern : kAllPatterns) {
        for (std::size_t tailSize = 1; tailSize <= 32; ++tailSize) {
            const TailCorpus corpus =
                makeSyntheticCorpus(pattern, tailSize, 3, 0x12345678U);
            require(corpus.ranges.size() == 3 &&
                        corpus.nodes.size() == tailSize * 3,
                    "synthetic tail corpus changed requested cardinality");
            std::vector<forest_sorting::UInt128> ids;
            ids.reserve(corpus.nodes.size());
            for (const forest_sorting::Node &node : corpus.nodes) {
                ids.push_back(node.id);
            }
            std::sort(ids.begin(), ids.end());
            require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
                    "synthetic tail pattern generated duplicate IDs");
        }
    }
}

void test_tail_descriptor_streaming_and_seed_domains() {
    const TailCorpusDescriptor first{
        Workload::Synthetic, Pattern::Random, DatasetKind::Random, 16, 0, 2, 0};
    const TailCorpusDescriptor same = first;
    const TailCorpusDescriptor changed{Workload::Synthetic,
                                       Pattern::SameHigh64,
                                       DatasetKind::Random,
                                       16,
                                       2,
                                       2,
                                       2};

    require(tailAlgorithmScheduleSeed(first, 123U) ==
                tailAlgorithmScheduleSeed(same, 123U),
            "tail algorithm seed was not deterministic");
    require(tailAlgorithmScheduleSeed(first, 123U) !=
                    tailAlgorithmScheduleSeed(changed, 123U) &&
                tailAlgorithmScheduleSeed(first, 123U) !=
                    tailAlgorithmScheduleSeed(first, 124U),
            "tail descriptor identity did not affect algorithm scheduling");
    require(tailSyntheticGenerationSeed(first, 123U) !=
                    tailNodeReservoirSeed(first, 123U) &&
                tailNodeReservoirSeed(first, 123U) !=
                    tailParentReservoirSeed(first, 123U) &&
                tailCorpusOrderSeed(123U) !=
                    tailAlgorithmScheduleSeed(first, 123U),
            "tail seed domains were not independent");

    struct LifetimeState {
        std::size_t live = 0;
        std::size_t maximumLive = 0;
        std::vector<std::size_t> resultWrites;
    };
    struct TrackedCorpus {
        LifetimeState *state;

        explicit TrackedCorpus(LifetimeState &lifetimeState)
            : state(&lifetimeState) {
            ++state->live;
            state->maximumLive = std::max(state->maximumLive, state->live);
        }
        TrackedCorpus(const TrackedCorpus &) = delete;
        TrackedCorpus &operator=(const TrackedCorpus &) = delete;
        ~TrackedCorpus() { --state->live; }
    };

    const std::vector<TailCorpusDescriptor> descriptors = {
        first, changed,
        TailCorpusDescriptor{Workload::CapturedNodeIds, Pattern::Random,
                             DatasetKind::SameHigh32, 100, 4, 2, 4}};
    LifetimeState state;
    forEachMaterializedContext(
        descriptors, std::vector<std::size_t>{2, 0, 1},
        [&](const TailCorpusDescriptor &) { return TrackedCorpus(state); },
        [&](const TailCorpusDescriptor &descriptor, const TrackedCorpus &) {
            require(state.live == 1,
                    "tail runner retained more than one corpus");
            state.resultWrites.push_back(descriptor.resultBegin);
        });
    require(state.live == 0 && state.maximumLive == 1 &&
                (state.resultWrites == std::vector<std::size_t>{4, 0, 2}),
            "tail descriptor execution lost scoped or canonical result slots");
}

void test_actual_capture_lifecycle() {
    constexpr std::size_t ownerCount = 4;
    auto ownerIndex = [](CaptureOwner owner) {
        return static_cast<std::size_t>(owner);
    };
    struct LifecycleState {
        std::array<std::size_t, ownerCount> live{};
        std::array<std::size_t, ownerCount> constructed{};
        std::array<std::size_t, ownerCount> destroyed{};
        std::array<CaptureOwner, 8> destructionOrder{};
        std::size_t destructionCount = 0;
        std::optional<CapturePhase> throwAt;
        bool reservoirObserved = false;
        bool finalConstructionBegan = false;
        bool finalConstructionCompleted = false;
        bool finalBeganWithNoOwners = false;
    };
    struct LifecycleObserver {
        LifecycleState *state;

        void ownerConstructed(CaptureOwner owner) const noexcept {
            const std::size_t index = static_cast<std::size_t>(owner);
            ++state->live[index];
            ++state->constructed[index];
        }

        void ownerDestroyed(CaptureOwner owner) const noexcept {
            const std::size_t index = static_cast<std::size_t>(owner);
            --state->live[index];
            ++state->destroyed[index];
            state->destructionOrder[state->destructionCount++] = owner;
        }

        void checkpoint(CapturePhase phase) const {
            if (phase == CapturePhase::ReservoirComplete) {
                state->reservoirObserved = true;
            } else if (phase == CapturePhase::FinalCorpusConstructionBegin) {
                state->finalConstructionBegan = true;
                state->finalBeganWithNoOwners = std::ranges::all_of(
                    state->live, [](std::size_t count) { return count == 0; });
            } else if (phase == CapturePhase::FinalCorpusConstructed) {
                state->finalConstructionCompleted = true;
            }
            if (state->throwAt == phase) {
                throw std::runtime_error("injected capture checkpoint failure");
            }
        }
    };
    auto requireBalanced = [&](const LifecycleState &state,
                               std::string_view context) {
        require(std::ranges::all_of(
                    state.live, [](std::size_t count) { return count == 0; }),
                std::string(context) + " left a real capture owner alive");
        require(state.constructed == state.destroyed,
                std::string(context) +
                    " did not balance owner construction and destruction");
    };

    LifecycleState nodeState;
    const TailCorpus nodeCorpus = makeCapturedNodeIdTailCorpus(
        DatasetKind::Random, 1000, 0, kDefaultBenchmarkDataSeed, 123U,
        LifecycleObserver{&nodeState});
    require(nodeCorpus.nodes.empty() && nodeState.reservoirObserved &&
                nodeState.finalConstructionBegan &&
                nodeState.finalConstructionCompleted &&
                nodeState.finalBeganWithNoOwners,
            "node capture did not preserve the zero-reservoir lifecycle");
    require(
        nodeState.constructed[ownerIndex(CaptureOwner::SourceForest)] == 1 &&
            nodeState.constructed[ownerIndex(CaptureOwner::ParentQueries)] ==
                0 &&
            nodeState.destructionCount == 3 &&
            nodeState.destructionOrder[0] == CaptureOwner::RadixWorkspace &&
            nodeState.destructionOrder[1] == CaptureOwner::RadixOrder &&
            nodeState.destructionOrder[2] == CaptureOwner::SourceForest,
        "node capture owner destruction order changed");
    requireBalanced(nodeState, "node capture");

    LifecycleState parentState;
    const TailCorpus parentCorpus = makeCapturedParentQueryTailCorpus(
        DatasetKind::Random, 1000, 8, kDefaultBenchmarkDataSeed, 456U,
        LifecycleObserver{&parentState});
    require(
        parentState.finalBeganWithNoOwners &&
            parentState.finalConstructionCompleted &&
            parentState.constructed[ownerIndex(CaptureOwner::ParentQueries)] ==
                1 &&
            parentState.destructionCount == 4 &&
            parentState.destructionOrder[2] == CaptureOwner::ParentQueries &&
            parentState.destructionOrder[3] == CaptureOwner::SourceForest &&
            parentCorpus.sourceSize == 1000,
        "parent-query capture did not release its real inputs");
    requireBalanced(parentState, "parent-query capture");

    LifecycleState reservoirFailure;
    reservoirFailure.throwAt = CapturePhase::ReservoirComplete;
    bool reservoirThrew = false;
    try {
        (void)makeCapturedParentQueryTailCorpus(
            DatasetKind::Random, 1000, 8, kDefaultBenchmarkDataSeed, 456U,
            LifecycleObserver{&reservoirFailure});
    } catch (const std::runtime_error &) {
        reservoirThrew = true;
    }
    require(reservoirThrew && reservoirFailure.reservoirObserved &&
                !reservoirFailure.finalConstructionBegan,
            "reservoir checkpoint exception was not injected");
    requireBalanced(reservoirFailure, "reservoir checkpoint exception");

    LifecycleState finalFailure;
    finalFailure.throwAt = CapturePhase::FinalCorpusConstructionBegin;
    bool finalThrew = false;
    try {
        (void)makeCapturedNodeIdTailCorpus(DatasetKind::Random, 1000, 8,
                                           kDefaultBenchmarkDataSeed, 123U,
                                           LifecycleObserver{&finalFailure});
    } catch (const std::runtime_error &) {
        finalThrew = true;
    }
    require(finalThrew && finalFailure.finalConstructionBegan &&
                finalFailure.finalBeganWithNoOwners &&
                !finalFailure.finalConstructionCompleted,
            "final-corpus checkpoint exception saw live capture inputs");
    requireBalanced(finalFailure, "final-corpus checkpoint exception");

    LifecycleState constructionFailure;
    bool constructionThrew = false;
    try {
        (void)makeCapturedNodeIdTailCorpus(
            DatasetKind::Random, std::numeric_limits<std::size_t>::max(), 1,
            kDefaultBenchmarkDataSeed, 123U,
            LifecycleObserver{&constructionFailure});
    } catch (const std::length_error &) {
        constructionThrew = true;
    } catch (const std::bad_alloc &) {
        constructionThrew = true;
    }
    require(constructionThrew && constructionFailure.destructionCount == 0,
            "failed owner construction armed a lifecycle guard");
    requireBalanced(constructionFailure, "owner construction exception");
}

void test_production_terminal_observer_contract() {
    std::vector<UInt128> ids = {forest_sorting::makeId(0, 2),
                                forest_sorting::makeId(0, 1)};
    auto idForIndex = [&](std::size_t index) { return ids[index]; };
    std::vector<std::size_t> observedOrder = {0, 1};
    std::vector<std::size_t> productionOrder = observedOrder;
    std::vector<std::vector<std::size_t>> ranges;
    std::vector<std::size_t> byteOffsets;
    RecordingTerminalObserver observer{&ranges, &byteOffsets};
    forest_sorting::detail::IdMsdChunkSortWorkspace<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>
        observedWorkspace;
    forest_sorting::detail::sortIndexRangeByIdMsdChunks<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>(
        observedOrder, idForIndex, forest_sorting::UInt128Traits{}, 0,
        observedOrder.size(), 0, observedWorkspace, observer);

    forest_sorting::detail::IdMsdChunkSortWorkspace<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>
        productionWorkspace;
    forest_sorting::detail::sortIndexRangeByIdMsdChunks<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>(
        productionOrder, idForIndex, forest_sorting::UInt128Traits{}, 0,
        productionOrder.size(), 0, productionWorkspace);

    require(ranges == std::vector<std::vector<std::size_t>>{{0, 1}},
            "observer did not see the initial range before sorting");
    require(byteOffsets == std::vector<std::size_t>{0},
            "initial terminal range reported the wrong byte offset");
    require(observedOrder == productionOrder &&
                observedOrder == std::vector<std::size_t>{1, 0},
            "observation changed the production permutation");

    ranges.clear();
    byteOffsets.clear();
    std::vector<UInt128> nestedIds;
    nestedIds.reserve(40);
    for (std::size_t index = 0; index < 8; ++index) {
        nestedIds.push_back(forest_sorting::makeId(
            (uint64_t{1} << 32U) | static_cast<uint64_t>(index + 1), index));
    }
    for (std::size_t index = 0; index < 32; ++index) {
        nestedIds.push_back(forest_sorting::makeId(
            (uint64_t{2} << 32U) | static_cast<uint64_t>(index + 1), index));
    }
    std::vector<std::size_t> nestedOrder(nestedIds.size());
    std::iota(nestedOrder.begin(), nestedOrder.end(), std::size_t{0});
    auto nestedIdForIndex = [&](std::size_t index) { return nestedIds[index]; };
    forest_sorting::detail::IdMsdChunkSortWorkspace<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>
        nestedWorkspace;
    forest_sorting::detail::sortIndexRangeByIdMsdChunks<
        forest_sorting::detail::production_id_radix_chunk_bytes,
        forest_sorting::detail::ProductionIdCountPolicy>(
        nestedOrder, nestedIdForIndex, forest_sorting::UInt128Traits{}, 0,
        nestedOrder.size(), 0, nestedWorkspace, observer);
    std::vector<std::size_t> nestedSizes;
    nestedSizes.reserve(ranges.size());
    for (const auto &range : ranges) {
        nestedSizes.push_back(range.size());
    }
    std::sort(nestedSizes.begin(), nestedSizes.end());
    require((nestedSizes == std::vector<std::size_t>{8, 32}),
            "observer missed nested terminal ranges");
    require(std::ranges::all_of(byteOffsets,
                                [](std::size_t offset) { return offset > 0; }),
            "nested terminal range reported an initial byte offset");

    for (std::size_t rangeSize :
         {std::size_t{0}, std::size_t{1}, std::size_t{33}}) {
        ranges.clear();
        byteOffsets.clear();
        std::vector<UInt128> cutoffIds(rangeSize);
        for (std::size_t index = 0; index < rangeSize; ++index) {
            cutoffIds[index] = forest_sorting::makeId(
                static_cast<uint64_t>(index) << 32U, index);
        }
        std::vector<std::size_t> cutoffOrder(rangeSize);
        std::iota(cutoffOrder.begin(), cutoffOrder.end(), std::size_t{0});
        auto cutoffIdForIndex = [&](std::size_t index) {
            return cutoffIds[index];
        };
        forest_sorting::detail::IdMsdChunkSortWorkspace<
            forest_sorting::detail::production_id_radix_chunk_bytes,
            forest_sorting::detail::ProductionIdCountPolicy>
            cutoffWorkspace;
        forest_sorting::detail::sortIndexRangeByIdMsdChunks<
            forest_sorting::detail::production_id_radix_chunk_bytes,
            forest_sorting::detail::ProductionIdCountPolicy>(
            cutoffOrder, cutoffIdForIndex, forest_sorting::UInt128Traits{}, 0,
            cutoffOrder.size(), 0, cutoffWorkspace, observer);
        require(ranges.empty(),
                "observer was called outside the 2-32 terminal cutoff");
    }
}

void runTailMicrobenchmarkTestsImpl() {
    runTest("tail micro output schema and escaping",
            test_tail_micro_output_schema_and_escaping);
    runTest("tail micro output renderers share normalized schema",
            test_tail_micro_output_renderers_share_normalized_schema);
    runTest("shell gap tail sorters", test_shell_gap_tail_sorters);
    runTest("production MSD tail capture", test_production_msd_tail_capture);
    runTest("synthetic tail patterns have unique IDs",
            test_synthetic_tail_patterns_are_unique);
    runTest("tail descriptor streaming and seed domains",
            test_tail_descriptor_streaming_and_seed_domains);
    runTest("actual capture lifecycle", test_actual_capture_lifecycle);
    runTest("production terminal observer contract",
            test_production_terminal_observer_contract);
}

} // namespace

void runTailMicrobenchmarkTests() { runTailMicrobenchmarkTestsImpl(); }
