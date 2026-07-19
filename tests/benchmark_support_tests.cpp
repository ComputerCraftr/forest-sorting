#include "forest_sorting/benchmark_support/common/benchmark_cli.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_execution.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_output.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_stats.hpp"
#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/common/uint128_fixtures.hpp"
#include "forest_sorting/benchmark_support/full/forest_benchmark_output.hpp"
#include "forest_sorting/benchmark_support/full/parent_registry.hpp"
#include "forest_sorting/benchmark_support/tail/tail_sort_variants.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "hashed_test_bytes.hpp"
#include "small_sort_test_types.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"
#include "uint128_test_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <numeric>
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

static_assert(deepChainNodeCount(0) == 1);
static_assert(deepChainNodeCount(UINT32_MAX) == (uint64_t{1} << 32U));

void test_materialized_context_lifetimes_and_schedule_seeds() {
    struct LifetimeState {
        std::size_t live = 0;
        std::size_t maximumLive = 0;
        std::size_t liveArtifacts = 0;
        int artifactOwner = 0;
        bool overlappingArtifactOwners = false;
        std::vector<int> processed;
    };
    struct TrackedContext {
        LifetimeState &state;
        int descriptor;

        TrackedContext(LifetimeState &lifetimeState, int value)
            : state(lifetimeState), descriptor(value) {
            ++state.live;
            state.maximumLive = std::max(state.maximumLive, state.live);
        }
        ~TrackedContext() { --state.live; }

        TrackedContext(const TrackedContext &) = delete;
        TrackedContext &operator=(const TrackedContext &) = delete;
    };
    struct TrackedArtifact {
        LifetimeState &state;

        TrackedArtifact(LifetimeState &lifetimeState, int descriptor)
            : state(lifetimeState) {
            if (state.liveArtifacts != 0 && state.artifactOwner != descriptor) {
                state.overlappingArtifactOwners = true;
            }
            state.artifactOwner = descriptor;
            ++state.liveArtifacts;
        }
        ~TrackedArtifact() {
            --state.liveArtifacts;
            if (state.liveArtifacts == 0) {
                state.artifactOwner = 0;
            }
        }

        TrackedArtifact(const TrackedArtifact &) = delete;
        TrackedArtifact &operator=(const TrackedArtifact &) = delete;
    };

    const std::vector<int> descriptors = {10, 20, 30};
    const std::vector<std::size_t> executionOrder = {2, 0, 1};
    LifetimeState state;
    forEachMaterializedContext(
        descriptors, executionOrder,
        [&](int descriptor) {
            require(state.liveArtifacts == 0,
                    "prior context artifacts survived materialization");
            return std::make_unique<TrackedContext>(state, descriptor);
        },
        [&](int descriptor, const auto &context) {
            require(state.live == 1,
                    "more than one materialized context remained alive");
            require(context->descriptor == descriptor,
                    "materialized context did not match its descriptor");
            const auto firstArtifact =
                std::make_unique<TrackedArtifact>(state, descriptor);
            const auto secondArtifact =
                std::make_unique<TrackedArtifact>(state, descriptor);
            require(state.liveArtifacts == 2,
                    "context artifacts were not scoped to processing");
            state.processed.push_back(descriptor);
        });
    require(state.live == 0 && state.maximumLive == 1 &&
                state.liveArtifacts == 0 && !state.overlappingArtifactOwners,
            "materialized context lifetime escaped one execution iteration");
    require((state.processed == std::vector<int>{30, 10, 20}),
            "materialized contexts ignored execution order");

    LifetimeState sequentialState;
    forEachMaterializedContext(
        descriptors, std::vector<std::size_t>{0, 1, 2},
        [&](int descriptor) {
            return std::make_unique<TrackedContext>(sequentialState,
                                                    descriptor);
        },
        [&](int descriptor, const auto &) {
            sequentialState.processed.push_back(descriptor);
        });
    require(sequentialState.live == 0 && sequentialState.maximumLive == 1 &&
                sequentialState.processed == descriptors,
            "unshuffled contexts did not preserve descriptor order and scope");

    LifetimeState exceptionalState;
    bool caught = false;
    try {
        forEachMaterializedContext(
            descriptors, std::vector<std::size_t>{0, 1, 2},
            [&](int descriptor) {
                return std::make_unique<TrackedContext>(exceptionalState,
                                                        descriptor);
            },
            [&](int descriptor, const auto &) {
                if (descriptor == 20) {
                    throw std::runtime_error("expected lifecycle test error");
                }
                exceptionalState.processed.push_back(descriptor);
            });
    } catch (const std::runtime_error &) {
        caught = true;
    }
    require(caught && exceptionalState.live == 0 &&
                exceptionalState.maximumLive == 1 &&
                exceptionalState.processed == std::vector<int>{10},
            "materialized context escaped during exception unwinding");

    const uint32_t first = benchmarkParentScheduleSeed(10000, 2, 7, 0x5eedU);
    require(first == benchmarkParentScheduleSeed(10000, 2, 7, 0x5eedU),
            "context schedule seed was not deterministic");
    require(first != benchmarkParentScheduleSeed(10001, 2, 7, 0x5eedU) &&
                first != benchmarkParentScheduleSeed(10000, 2, 8, 0x5eedU) &&
                first != benchmarkParentScheduleSeed(10000, 2, 7, 0x5eeeU) &&
                first != benchmarkSortScheduleSeed(10000, 2, 7, 0x5eedU),
            "context identity did not affect its schedule seed");
    require(benchmarkContextOrderSeed(0x5eedU) ==
                    benchmarkContextOrderSeed(0x5eedU) &&
                benchmarkContextOrderSeed(0x5eedU) !=
                    benchmarkContextOrderSeed(0x5eeeU),
            "context order seed was not deterministic and domain-separated");

    std::vector<std::size_t> firstSchedule(16);
    std::iota(firstSchedule.begin(), firstSchedule.end(), std::size_t{0});
    auto matchingSchedule = firstSchedule;
    auto changedSchedule = firstSchedule;
    shuffleBenchmarkOrder(firstSchedule, first, 3);
    shuffleBenchmarkOrder(matchingSchedule, first, 3);
    shuffleBenchmarkOrder(changedSchedule,
                          benchmarkParentScheduleSeed(10000, 2, 7, 0x5eeeU), 3);
    require(firstSchedule == matchingSchedule,
            "identical context seeds produced different schedules");
    require(firstSchedule != changedSchedule,
            "different order seeds produced the same context schedule");
}

void test_deep_chain_uses_checked_count_semantics() {
    std::vector<Node> nodes;
    appendDeepChain(nodes, 0, 0x100U);
    require(nodes.size() == 1 && nodes.front().parentId == 0,
            "zero-depth chain did not append exactly its root");

    appendDeepChain(nodes, 3, 0x200U);
    require(nodes.size() == 5,
            "deep-chain fixture changed maximum-depth semantics");
    require(nodes[1].parentId == 0 && nodes[2].parentId == nodes[1].id &&
                nodes[3].parentId == nodes[2].id &&
                nodes[4].parentId == nodes[3].id,
            "deep-chain fixture did not link its checked append range");
}

void test_dataset_cardinality_and_bounded_depth_cycles() {
    constexpr std::array<std::size_t, 13> kSizes = {
        0, 1, 31, 128, 129, 130, 641, 642, 643, 1666, 1667, 2000, 10000};
    for (DatasetKind dataset : allDatasetKinds()) {
        for (std::size_t size : kSizes) {
            const auto nodes =
                makeGeneratedForestForKind(dataset, size, 0x1234U);
            const auto repeated =
                makeGeneratedForestForKind(dataset, size, 0x1234U);
            require(nodes.size() == size,
                    std::string(datasetName(dataset)) +
                        " dataset did not preserve requested cardinality");
            require(sameNodes(nodes, repeated),
                    std::string(datasetName(dataset)) +
                        " dataset generation was not deterministic");

            std::vector<UInt128> sortedIds;
            sortedIds.reserve(nodes.size());
            for (const Node &node : nodes) {
                sortedIds.push_back(node.id);
            }
            std::sort(sortedIds.begin(), sortedIds.end());
            require(std::adjacent_find(sortedIds.begin(), sortedIds.end()) ==
                        sortedIds.end(),
                    std::string(datasetName(dataset)) +
                        " dataset generated duplicate node IDs");

            for (const Node &node : nodes) {
                const bool sentinel = forest_sorting::detail::isParentSentinel(
                    forest_sorting::UInt128NodeTraits{}, node.parentId);
                const bool resolves = std::binary_search(
                    sortedIds.begin(), sortedIds.end(), node.parentId);
                if (dataset == DatasetKind::ExternalParents) {
                    require(!sentinel && !resolves,
                            "external parent was a sentinel or generated ID");
                } else {
                    require(sentinel || resolves,
                            std::string(datasetName(dataset)) +
                                " dataset contains an unintended unresolved "
                                "parent");
                }
            }

            if (dataset == DatasetKind::ExternalParents) {
                const auto control =
                    buildParentIndexForKind(ParentKind::Control, nodes);
                const auto production = buildParentIndexForKind(
                    ParentKind::RadixJoinIdMsdChunk32, nodes);
                require(control == production &&
                            std::all_of(
                                control.begin(), control.end(),
                                [](std::size_t parentIndex) {
                                    return parentIndex ==
                                           forest_sorting::detail::no_parent;
                                }),
                        "external parents did not remain unresolved in parent "
                        "builders");
            }
        }
    }

    auto makeSequentialId = [](std::size_t index) {
        return forest_sorting::makeId(0, static_cast<uint64_t>(index) + 1U);
    };
    require(makeDepthLinkedForest(0, UINT32_MAX, makeSequentialId).empty(),
            "empty depth-cycle fixture changed behavior");
    const auto oneNode = makeDepthLinkedForest(1, UINT32_MAX, makeSequentialId);
    require(oneNode.size() == 1 && oneNode.front().parentId == 0,
            "single-node fixture allocated unreachable depth state");
    const auto clamped = makeDepthLinkedForest(4, UINT32_MAX, makeSequentialId);
    require(clamped.size() == 4 && clamped[1].parentId == clamped[0].id &&
                clamped[2].parentId == clamped[1].id &&
                clamped[3].parentId == clamped[2].id,
            "depth-cycle clamp changed reachable chain behavior");
}

void test_outlier_dataset_uses_requested_budget() {
    struct BudgetCase {
        std::size_t requested;
        std::array<std::size_t, 3> expectedChains;
    };
    constexpr std::array kCases = {
        BudgetCase{0, {0, 0, 0}},           BudgetCase{1, {1, 0, 0}},
        BudgetCase{129, {129, 0, 0}},       BudgetCase{130, {129, 1, 0}},
        BudgetCase{642, {129, 513, 0}},     BudgetCase{643, {129, 513, 1}},
        BudgetCase{1666, {129, 513, 1024}}, BudgetCase{1667, {129, 513, 1025}},
        BudgetCase{2000, {129, 513, 1025}},
    };

    for (const BudgetCase &testCase : kCases) {
        const auto nodes = makeGeneratedForestWithOutliers(
            testCase.requested, kCommonFixtureMaxDepth, 42U);
        require(nodes.size() == testCase.requested,
                "outlier dataset exceeded its requested budget");
        std::array<std::size_t, 3> chainCounts{};
        for (const Node &node : nodes) {
            const uint64_t high = static_cast<uint64_t>(node.id >> 64U);
            if (high == 0x1000ULL) {
                ++chainCounts[0];
            } else if (high == 0x2000ULL) {
                ++chainCounts[1];
            } else if (high == 0x3000ULL) {
                ++chainCounts[2];
            }
        }
        require(chainCounts == testCase.expectedChains,
                "outlier dataset changed its bounded chain allocation");
        require(sameNodes(nodes,
                          makeGeneratedForestWithOutliers(
                              testCase.requested, kCommonFixtureMaxDepth, 42U)),
                "outlier dataset was not deterministic");
    }
}

void test_retained_artifact_replacement_releases_old_payload() {
    struct LifetimeState {
        std::size_t liveOwners = 0;
        std::size_t maximumOwners = 0;
        bool buildSawRetainedPayload = false;
    };
    struct TrackedPayload {
        LifetimeState *state = nullptr;
        bool owns = false;

        TrackedPayload() = default;
        explicit TrackedPayload(LifetimeState &lifetimeState)
            : state(&lifetimeState), owns(true) {
            ++state->liveOwners;
            state->maximumOwners =
                std::max(state->maximumOwners, state->liveOwners);
        }
        TrackedPayload(TrackedPayload &&other) noexcept
            : state(other.state), owns(other.owns) {
            other.owns = false;
        }
        TrackedPayload &operator=(TrackedPayload &&other) noexcept {
            if (owns) {
                --state->liveOwners;
            }
            state = other.state;
            owns = other.owns;
            other.owns = false;
            return *this;
        }
        TrackedPayload(const TrackedPayload &) = delete;
        TrackedPayload &operator=(const TrackedPayload &) = delete;
        ~TrackedPayload() {
            if (owns) {
                --state->liveOwners;
            }
        }
    };

    LifetimeState state;
    TrackedPayload retained;
    auto rebuild = [&] {
        state.buildSawRetainedPayload =
            state.buildSawRetainedPayload || state.liveOwners != 0;
        return TrackedPayload(state);
    };
    (void)replaceRetainedArtifactMs(retained, rebuild);
    (void)replaceRetainedArtifactMs(retained, rebuild);
    require(!state.buildSawRetainedPayload && state.maximumOwners == 1 &&
                state.liveOwners == 1,
            "repeated build overlapped old and new retained payloads");
    retained = TrackedPayload{};
    require(state.liveOwners == 0,
            "retained artifact payload escaped its owner");
}

void test_benchmark_stats_median_and_stddev() {
    const auto oddStats = computeSampleStats({3.0, 1.0, 2.0});
    requireNear(oddStats.median, 2.0, 0.0000001,
                "odd benchmark median was wrong");
    requireNear(oddStats.mean, 2.0, 0.0000001, "benchmark mean was wrong");
    requireNear(oddStats.stddev, 1.0, 0.0000001,
                "benchmark sample stddev was wrong");

    const auto evenStats = computeSampleStats({4.0, 1.0, 2.0, 3.0});
    requireNear(evenStats.median, 2.5, 0.0000001,
                "even benchmark median was wrong");

    const auto singleStats = computeSampleStats({7.0});
    requireNear(singleStats.stddev, 0.0, 0.0000001,
                "single-sample stddev should be zero");
}

void test_benchmark_cli_selection_helpers() {
    const std::vector<int> defaults = {1, 2};
    const std::vector<int> all = {1, 2, 3, 4};
    std::vector<int> values = defaults;
    bool seen = false;
    auto parse = [](std::string_view value) {
        return value == "three" ? 3 : 4;
    };
    applyRegistrySelection(values, seen, "three", defaults, all, parse);
    applyRegistrySelection(values, seen, "three", defaults, all, parse);
    require((values == std::vector<int>{3}),
            "first selector did not replace defaults uniquely");
    applyRegistrySelection(values, seen, "default", defaults, all, parse);
    require(values == defaults, "default selector did not reset selection");
    applyRegistrySelection(values, seen, "four", defaults, all, parse);
    require((values == std::vector<int>{1, 2, 4}),
            "concrete selector did not append after default reset");
    applyRegistrySelection(values, seen, "all", defaults, all, parse);
    require(values == all, "all selector did not reset to full registry");
    appendMissingBaseline(values, 5);
    appendMissingBaseline(values, 5);
    require(values.back() == 5 && static_cast<std::size_t>(std::count(
                                      values.begin(), values.end(), 5)) == 1,
            "missing baseline was not appended exactly once");
}

void test_benchmark_cli_numeric_parsing() {
    require(parsePositiveSizeOption("42", "--size") == 42,
            "decimal benchmark size parsed incorrectly");
    require(parseSeedOption("0x2a", "--data-seed") == 42U,
            "hexadecimal benchmark seed parsed incorrectly");
    require(parseNonNegativeIntOption("0", "--warmup") == 0,
            "zero benchmark warmup was rejected");

    auto requireRejected = [](auto parse, std::string_view value,
                              std::string_view message) {
        bool rejected = false;
        try {
            parse(value);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, message);
    };
    requireRejected(
        [](std::string_view value) {
            return parsePositiveSizeOption(value, "--size");
        },
        "12nodes", "numeric parser accepted trailing characters");
    requireRejected(
        [](std::string_view value) {
            return parseNonNegativeIntOption(value, "--warmup");
        },
        "99999999999999999999", "numeric parser accepted an overflow");
    requireRejected(
        [](std::string_view value) {
            return parsePositiveIntOption(value, "--iterations");
        },
        "0", "positive numeric parser accepted zero");
    requireRejected(
        [](std::string_view) {
            return checkedSizeProduct(std::numeric_limits<std::size_t>::max(),
                                      2, "test product");
        },
        "ignored", "checked size product accepted an overflow");
}

void test_benchmark_comparison_eligibility() {
    require(comparisonEligibility(true, true) == ComparisonEligibility::Ok,
            "valid A/B rows were rejected");
    require(comparisonEligibility(false, true) ==
                ComparisonEligibility::InvalidCandidate,
            "invalid candidate was eligible for A/B reporting");
    require(comparisonEligibility(true, false) ==
                ComparisonEligibility::InvalidBaseline,
            "invalid baseline was eligible for A/B reporting");
}

void test_benchmark_bootstrap_ci_is_deterministic() {
    const std::vector<double> samples = {1.0, 2.0, 4.0, 8.0};
    const auto ci0 = bootstrapMeanCi95(samples, 123U, 128U);
    const auto ci1 = bootstrapMeanCi95(samples, 123U, 128U);
    requireNear(ci0.low, ci1.low, 0.0000001,
                "bootstrap low CI was not deterministic");
    requireNear(ci0.high, ci1.high, 0.0000001,
                "bootstrap high CI was not deterministic");
}

void test_benchmark_paired_relative_delta() {
    const std::vector<double> samples = {8.0, 16.0, 24.0};
    const std::vector<double> baseline = {10.0, 20.0, 30.0};
    const auto absoluteDeltas = pairedAbsoluteDeltas(samples, baseline);
    require(absoluteDeltas.size() == 3U,
            "paired absolute delta count was wrong");
    requireNear(medianOfSamples(absoluteDeltas), -4.0, 0.0000001,
                "paired absolute delta median was wrong");

    const auto deltas = pairedRelativeDeltas(samples, baseline);
    require(deltas.size() == 3U, "paired delta count was wrong");
    for (double delta : deltas) {
        requireNear(delta, -20.0, 0.0000001,
                    "paired relative delta did not use matching samples");
    }

    const auto interval =
        bootstrapPairedRelativeDeltaCi95(samples, baseline, 456U, 128U);
    requireNear(interval.low, -20.0, 0.0000001,
                "paired relative delta CI low was wrong");
    requireNear(interval.high, -20.0, 0.0000001,
                "paired relative delta CI high was wrong");
}

void test_benchmark_relative_delta_rejects_zero_baselines() {
    constexpr std::string_view kExpectedMessage =
        "cannot compute relative benchmark delta from a zero baseline sample";
    auto requireRejected = [kExpectedMessage](auto compute,
                                              std::string_view message) {
        bool rejected = false;
        try {
            compute();
        } catch (const std::runtime_error &error) {
            rejected = error.what() == kExpectedMessage;
        }
        require(rejected, message);
    };

    requireRejected([] { (void)pairedRelativeDeltas({1.0}, {0.0}); },
                    "paired relative delta accepted a positive zero baseline");
    requireRejected([] { (void)pairedRelativeDeltas({1.0}, {-0.0}); },
                    "paired relative delta accepted a negative zero baseline");
    requireRejected(
        [] {
            (void)bootstrapPairedRelativeDeltaCi95(
                {8.0, 16.0, 24.0, 32.0}, {10.0, 20.0, 0.0, 40.0}, 456U, 1U);
        },
        "bootstrap relative delta did not prevalidate every baseline");
}

void test_benchmark_pipeline_samples_preserve_pairing() {
    const std::vector<double> parentSamples = {2.0, 4.0, 6.0};
    const std::vector<double> sortSamples = {1.0, 2.0, 3.0};
    const std::vector<double> baselinePipeline = {4.0, 8.0, 12.0};
    std::vector<double> pipelineSamples;
    pipelineSamples.reserve(parentSamples.size());
    for (std::size_t sampleIdx = 0; sampleIdx < parentSamples.size();
         ++sampleIdx) {
        pipelineSamples.push_back(parentSamples[sampleIdx] +
                                  sortSamples[sampleIdx]);
    }

    require((pipelineSamples == std::vector<double>{3.0, 6.0, 9.0}),
            "pipeline samples did not preserve parent/sort pairing");
    const auto relativeDeltas =
        pairedRelativeDeltas(pipelineSamples, baselinePipeline);
    for (double delta : relativeDeltas) {
        requireNear(delta, -25.0, 0.0000001,
                    "pipeline relative delta used mismatched samples");
    }
}

void test_shared_benchmark_stat_schema() {
    constexpr std::array<std::string_view, 7> kExpectedNames = {
        "min", "median", "mean", "stddev", "max", "ci95_low", "ci95_high"};
    require(kStatFields.size() == kExpectedNames.size(),
            "shared stat schema has the wrong field count");
    for (std::size_t fieldIdx = 0; fieldIdx < kExpectedNames.size();
         ++fieldIdx) {
        require(kStatFields[fieldIdx].name == kExpectedNames[fieldIdx],
                "shared stat schema field order changed");
        for (std::size_t otherIdx = fieldIdx + 1;
             otherIdx < kExpectedNames.size(); ++otherIdx) {
            require(kStatFields[fieldIdx].id != kStatFields[otherIdx].id &&
                        kStatFields[fieldIdx].name !=
                            kStatFields[otherIdx].name,
                    "shared stat schema contains duplicate fields");
        }
    }

    SampleStats stats;
    stats.min = 1.0;
    stats.median = 2.0;
    stats.mean = 3.0;
    stats.stddev = 4.0;
    stats.max = 5.0;
    stats.ci95 = {6.0, 7.0};
    for (std::size_t fieldIdx = 0; fieldIdx < kStatFields.size(); ++fieldIdx) {
        requireNear(statFieldValue(stats, kStatFields[fieldIdx].id),
                    static_cast<double>(fieldIdx + 1), 0.0000001,
                    "shared stat schema returned the wrong value");
    }
}

void test_full_benchmark_output_schema() {
    constexpr std::array<std::string_view, 57> kExpectedNames = {
        "dataset",
        "node_count",
        "data_seed",
        "parent_builder",
        "sort_algorithm",
        "samples",
        "sort_baseline",
        "sort_comparison_status",
        "sort_winner",
        "sort_delta_median_ms",
        "sort_delta_median_pct",
        "sort_delta_ci95_low_pct",
        "sort_delta_ci95_high_pct",
        "parent_baseline",
        "parent_comparison_status",
        "parent_winner",
        "parent_delta_median_ms",
        "parent_delta_median_pct",
        "parent_delta_ci95_low_pct",
        "parent_delta_ci95_high_pct",
        "pipeline_baseline_parent",
        "pipeline_baseline_sort",
        "pipeline_comparison_status",
        "pipeline_winner",
        "pipeline_delta_median_ms",
        "pipeline_delta_median_pct",
        "pipeline_delta_ci95_low_pct",
        "pipeline_delta_ci95_high_pct",
        "parent_min_ms",
        "parent_median_ms",
        "parent_mean_ms",
        "parent_stddev_ms",
        "parent_max_ms",
        "parent_ci95_low_ms",
        "parent_ci95_high_ms",
        "sort_min_ms",
        "sort_median_ms",
        "sort_mean_ms",
        "sort_stddev_ms",
        "sort_max_ms",
        "sort_ci95_low_ms",
        "sort_ci95_high_ms",
        "pipeline_min_ms",
        "pipeline_median_ms",
        "pipeline_mean_ms",
        "pipeline_stddev_ms",
        "pipeline_max_ms",
        "pipeline_ci95_low_ms",
        "pipeline_ci95_high_ms",
        "verify_min_ms",
        "verify_median_ms",
        "verify_mean_ms",
        "verify_stddev_ms",
        "verify_max_ms",
        "verify_ci95_low_ms",
        "verify_ci95_high_ms",
        "status",
    };

    std::vector<std::string> actualNames;
    actualNames.reserve(benchmark_output_field_count);
    visitBenchmarkOutputSchema(
        [&](const BenchmarkFieldDescriptor &field) {
            actualNames.emplace_back(field.delimitedName);
        },
        [&](const BenchmarkPhaseDescriptor &phase,
            const StatFieldDescriptor &field) {
            actualNames.push_back(std::string(phase.name) + "_" +
                                  statFieldName(field, "ms"));
        });
    require(actualNames.size() == kExpectedNames.size(),
            "full benchmark output schema has the wrong field count");
    for (std::size_t fieldIdx = 0; fieldIdx < kExpectedNames.size();
         ++fieldIdx) {
        require(actualNames[fieldIdx] == kExpectedNames[fieldIdx],
                "full benchmark output schema field order changed");
        for (std::size_t otherIdx = fieldIdx + 1;
             otherIdx < kExpectedNames.size(); ++otherIdx) {
            require(actualNames[fieldIdx] != actualNames[otherIdx],
                    "full benchmark output schema contains duplicate fields");
        }
    }
}

void test_full_benchmark_output_renderers() {
    const std::vector<double> parentSamples = {1.0, 2.0};
    const std::vector<double> sortSamples = {3.0, 4.0};
    const std::vector<double> pipelineSamples = {4.0, 6.0};
    const std::vector<double> verifySamples = {5.0, 6.0};

    BenchmarkOutputRow baseline;
    baseline.dataset = "a,b\"c\\d";
    baseline.nodeCount = 8;
    baseline.dataSeed = "0x1";
    baseline.parentBuilder = "control";
    baseline.sortAlgorithm = "candidate";
    baseline.samples = sortSamples.size();
    baseline.sortBaseline = "candidate";
    baseline.sortComparisonStatus = "baseline";
    baseline.sortWinner = "none";
    baseline.sortDeltaAvailable = true;
    baseline.parentBaseline = "control";
    baseline.parentComparisonStatus = "baseline";
    baseline.parentWinner = "none";
    baseline.parentDeltaAvailable = true;
    baseline.pipelineBaselineParent = "control";
    baseline.pipelineBaselineSort = "candidate";
    baseline.pipelineComparisonStatus = "baseline";
    baseline.pipelineWinner = "none";
    baseline.pipelineDeltaAvailable = true;
    baseline.parentStats = computeSampleStats(parentSamples);
    baseline.sortStats = computeSampleStats(sortSamples);
    baseline.pipelineStats = computeSampleStats(pipelineSamples);
    baseline.verifyStats = computeSampleStats(verifySamples);
    baseline.parentSamples = parentSamples;
    baseline.sortSamples = sortSamples;
    baseline.pipelineSamples = pipelineSamples;
    baseline.verifySamples = verifySamples;
    baseline.status = "ok";

    BenchmarkOutputRow candidate = baseline;
    candidate.dataset = "candidate";
    candidate.sortComparisonStatus = "ok";
    candidate.sortWinner = "candidate";
    candidate.sortDeltaMedianPct = -5.0;

    BenchmarkOutputRow missing = baseline;
    missing.dataset = "missing";
    missing.sortComparisonStatus = "missing-baseline";
    missing.status = "sort-baseline-missing";

    BenchmarkOutputRow failed = baseline;
    failed.dataset = "failed";
    failed.status = "verify-failed";

    const std::vector<BenchmarkOutputRow> rows = {baseline, candidate, missing,
                                                  failed};

    std::ostringstream csv;
    printBenchmarkDelimited(csv, rows, ',');
    const std::size_t headerEnd = csv.str().find('\n');
    require(headerEnd != std::string::npos,
            "full benchmark CSV omitted its header terminator");
    const std::string header = csv.str().substr(0, headerEnd);
    require(static_cast<std::size_t>(
                std::count(header.begin(), header.end(), ',')) ==
                benchmark_output_field_count - 1,
            "full benchmark CSV header has the wrong field count");
    require(csv.str().find("\"a,b\"\"c\\d\"") != std::string::npos,
            "full benchmark CSV did not escape its string field");
    require(csv.str().find("sort-baseline-missing") != std::string::npos &&
                csv.str().find("verify-failed") != std::string::npos,
            "full benchmark CSV omitted non-ok statuses");

    std::ostringstream tsv;
    printBenchmarkDelimited(tsv, rows, '\t');
    require(tsv.str().find("a,b\"c\\d\t8") != std::string::npos,
            "full benchmark TSV unexpectedly CSV-escaped text");

    std::ostringstream summaryJson;
    printBenchmarkJsonRows(summaryJson, rows, true, false);
    require(summaryJson.str().find("\"a,b\\\"c\\\\d\"") != std::string::npos,
            "full benchmark JSON did not escape its string field");
    require(summaryJson.str().find("\"parent\": {") != std::string::npos &&
                summaryJson.str().find("\"verify\": {") != std::string::npos,
            "summary JSON omitted nested phase statistics");
    require(summaryJson.str().find("samples_ms") == std::string::npos,
            "summary JSON emitted raw samples");

    const std::size_t datasetPosition = summaryJson.str().find("\"dataset\"");
    const std::size_t parentPosition = summaryJson.str().find("\"parent\": {");
    const std::size_t statusPosition = summaryJson.str().find("\"status\"");
    require(datasetPosition < parentPosition && parentPosition < statusPosition,
            "full benchmark JSON field order changed");

    std::ostringstream rawJson;
    printBenchmarkJsonRows(rawJson, rows, true, true);
    require(rawJson.str().find("\"parent_samples_ms\": [1, 2]") !=
                    std::string::npos &&
                rawJson.str().find("\"verify_samples_ms\": [5, 6]") !=
                    std::string::npos,
            "raw JSON omitted sample arrays");

    std::ostringstream noneJson;
    printBenchmarkJsonRows(noneJson, rows, false, false);
    require(noneJson.str().find("\"parent\": {") == std::string::npos &&
                noneJson.str().find("samples_ms") == std::string::npos,
            "none JSON emitted summary or raw sample fields");
    require(noneJson.str().find("missing-baseline") != std::string::npos &&
                noneJson.str().find("verify-failed") != std::string::npos,
            "none JSON omitted comparison or result statuses");
}

void test_benchmark_winner_classification_is_ci_aware() {
    require(classifyBenchmarkWinner(-5.0, {-8.0, -1.0}) == "candidate",
            "candidate should win when CI is entirely below zero");
    require(classifyBenchmarkWinner(5.0, {1.0, 8.0}) == "baseline",
            "baseline should win when CI is entirely above zero");
    require(classifyBenchmarkWinner(-5.0, {-8.0, 1.0}) == "tie",
            "candidate should not win when CI crosses zero");
    require(classifyBenchmarkWinner(5.0, {-1.0, 8.0}) == "tie",
            "baseline should not win when CI crosses zero");
    require(classifyBenchmarkWinner(0.0, {0.0, 0.0}) == "tie",
            "zero delta should classify as tie");
}

void test_benchmark_order_seed_controls_shuffle() {
    std::vector<std::size_t> order0 = makeSequentialIndexOrder(16);
    std::vector<std::size_t> order1 = makeSequentialIndexOrder(16);
    std::vector<std::size_t> order2 = makeSequentialIndexOrder(16);

    shuffleBenchmarkOrder(order0, 123U, 0);
    shuffleBenchmarkOrder(order1, 123U, 0);
    shuffleBenchmarkOrder(order2, 124U, 0);

    require(order0 == order1,
            "same order seed did not reproduce shuffled schedule");
    require(order0 != order2,
            "different order seed unexpectedly produced same schedule");
}

void test_benchmark_data_seed_controls_generated_data() {
    const auto nodes0 =
        makeGeneratedForestForKind(DatasetKind::Random, 1000, 123U);
    const auto nodes1 =
        makeGeneratedForestForKind(DatasetKind::Random, 1000, 123U);
    const auto nodes2 =
        makeGeneratedForestForKind(DatasetKind::Random, 1000, 124U);

    require(sameNodes(nodes0, nodes1),
            "same data seed did not reproduce generated data");
    require(!sameNodes(nodes0, nodes2),
            "different data seed unexpectedly produced same generated data");
}

void test_same_high32_dataset_shape() {
    const auto nodes =
        makeGeneratedForestForKind(DatasetKind::SameHigh32, 1000, 123U);
    require(!nodes.empty(), "same-high32 dataset was empty");

    const uint64_t expectedHigh32 =
        static_cast<uint64_t>(nodes.front().id >> 96U);
    bool sawDifferentLowerBits = false;
    const UInt128 firstLowerBits =
        nodes.front().id &
        ((static_cast<UInt128>(1) << 96U) - static_cast<UInt128>(1));

    for (const Node &node : nodes) {
        const uint64_t high32 = static_cast<uint64_t>(node.id >> 96U);
        require(high32 == expectedHigh32,
                "same-high32 dataset changed the top 32 bits");
        const UInt128 lowerBits = node.id & ((static_cast<UInt128>(1) << 96U) -
                                             static_cast<UInt128>(1));
        sawDifferentLowerBits =
            sawDifferentLowerBits || (lowerBits != firstLowerBits);
    }

    require(sawDifferentLowerBits,
            "same-high32 dataset did not vary lower ID bits");
}

template <typename Nodes, typename Traits, typename Sorter>
void requireFixedSmallSorterRejectsOverflow(const Nodes &nodes,
                                            const Traits &traits, Sorter sorter,
                                            std::string_view sorterName) {
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);

    bool rejected = false;
    try {
        sorter(order, nodes, traits, 0, order.size());
    } catch (const std::runtime_error &error) {
        rejected = std::string_view(error.what()) ==
                   "small sorter range exceeds fixed scratch capacity";
    }
    require(rejected,
            std::string(sorterName) + " did not reject fixed scratch overflow");
}

template <typename Sorter>
void requireSmallSorterMatchesExpected(Sorter sorter,
                                       std::string_view sorterName) {
    constexpr std::size_t rangeSize = 4;
    std::vector<Node> nodes(rangeSize);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        nodes[nodeIdx] = {static_cast<UInt128>(nodes.size() - nodeIdx), 0};
    }
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);
    sorter(order, nodes, forest_sorting::UInt128NodeTraits{}, 0, order.size());
    require((order == std::vector<std::size_t>{3, 2, 1, 0}),
            std::string(sorterName) +
                " produced the wrong exact-capacity order");
}

void test_small_sort_scratch_policies() {
    std::vector<CachedScratchTestNode> cachedNodes(5);
    for (std::size_t nodeIdx = 0; nodeIdx < cachedNodes.size(); ++nodeIdx) {
        cachedNodes[nodeIdx].id.bytes.back() =
            static_cast<uint8_t>(cachedNodes.size() - nodeIdx);
    }
    const CachedScratchTestTraits cachedTraits;
    requireFixedSmallSorterRejectsOverflow(
        cachedNodes, cachedTraits,
        [](auto &order, const auto &nodes, const auto &traits,
           std::size_t begin, std::size_t end) {
            forest_sorting::detail::stableSortRangeSmallLinear<4>(
                order, nodes, traits, begin, end);
        },
        "linear");
    requireFixedSmallSorterRejectsOverflow(
        cachedNodes, cachedTraits,
        [](auto &order, const auto &nodes, const auto &traits,
           std::size_t begin, std::size_t end) {
            stableSortRangeSmallBinary<4>(order, nodes, traits, begin, end);
        },
        "binary");
    requireFixedSmallSorterRejectsOverflow(
        cachedNodes, cachedTraits,
        [](auto &order, const auto &nodes, const auto &traits,
           std::size_t begin, std::size_t end) {
            stableSortRangeSmallExponential<4>(order, nodes, traits, begin,
                                               end);
        },
        "exponential");
    requireFixedSmallSorterRejectsOverflow(
        cachedNodes, cachedTraits,
        [](auto &order, const auto &nodes, const auto &traits,
           std::size_t begin, std::size_t end) {
            stableSortRangeSmallBranchlessBitwise<4>(order, nodes, traits,
                                                     begin, end);
        },
        "branchless-bitwise");

    requireSmallSorterMatchesExpected(LinearSmallSorter<4>{}, "fixed linear");
    requireSmallSorterMatchesExpected(BinarySmallSorter<4>{}, "fixed binary");
    requireSmallSorterMatchesExpected(ExponentialSmallSorter<4>{},
                                      "fixed exponential");
    requireSmallSorterMatchesExpected(BranchlessBitwiseSmallSorter<4>{},
                                      "fixed branchless-bitwise");
    using Traits = TestBytesTraits<16>;
    std::vector<TestNode<16>> nodes(5);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        nodes[nodeIdx].id =
            makeTestBytes<16>(0, static_cast<uint8_t>(nodes.size() - nodeIdx));
    }
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);
    forest_sorting::detail::stableSortRangeSmallLinear<4>(
        order, nodes, Traits{}, 0, order.size());
    require(
        (order == std::vector<std::size_t>{4, 3, 2, 1, 0}),
        "uncached linear sorting incorrectly enforced fixed cache capacity");

    std::iota(order.begin(), order.end(), 0);
    stableSortRangeSmallBinary<4>(order, nodes, Traits{}, 0, order.size());
    require(
        (order == std::vector<std::size_t>{4, 3, 2, 1, 0}),
        "uncached binary sorting incorrectly enforced fixed cache capacity");

    std::iota(order.begin(), order.end(), 0);
    stableSortRangeSmallExponential<4>(order, nodes, Traits{}, 0, order.size());
    require((order == std::vector<std::size_t>{4, 3, 2, 1, 0}),
            "uncached exponential sorting incorrectly enforced fixed cache "
            "capacity");
}

void runBenchmarkSupportTestsImpl() {
    runTest("materialized benchmark context lifetime and schedule",
            test_materialized_context_lifetimes_and_schedule_seeds);
    runTest("deep-chain fixture count semantics",
            test_deep_chain_uses_checked_count_semantics);
    runTest("dataset cardinality and bounded depth cycles",
            test_dataset_cardinality_and_bounded_depth_cycles);
    runTest("outlier dataset requested budget",
            test_outlier_dataset_uses_requested_budget);
    runTest("retained artifact replacement lifecycle",
            test_retained_artifact_replacement_releases_old_payload);
    runTest("benchmark CLI selection helpers",
            test_benchmark_cli_selection_helpers);
    runTest("benchmark CLI numeric parsing",
            test_benchmark_cli_numeric_parsing);
    runTest("benchmark comparison eligibility",
            test_benchmark_comparison_eligibility);
    runTest("benchmark stats median and stddev",
            test_benchmark_stats_median_and_stddev);
    runTest("benchmark bootstrap CI is deterministic",
            test_benchmark_bootstrap_ci_is_deterministic);
    runTest("benchmark paired relative delta",
            test_benchmark_paired_relative_delta);
    runTest("benchmark relative delta rejects zero baselines",
            test_benchmark_relative_delta_rejects_zero_baselines);
    runTest("benchmark pipeline samples preserve pairing",
            test_benchmark_pipeline_samples_preserve_pairing);
    runTest("shared benchmark stat schema", test_shared_benchmark_stat_schema);
    runTest("full benchmark output schema", test_full_benchmark_output_schema);
    runTest("full benchmark output renderers",
            test_full_benchmark_output_renderers);
    runTest("benchmark winner classification is CI-aware",
            test_benchmark_winner_classification_is_ci_aware);
    runTest("benchmark order seed controls shuffle",
            test_benchmark_order_seed_controls_shuffle);
    runTest("benchmark data seed controls generated data",
            test_benchmark_data_seed_controls_generated_data);
    runTest("same-high32 dataset shape", test_same_high32_dataset_shape);
    runTest("small sort scratch policies", test_small_sort_scratch_policies);
}

} // namespace

void runBenchmarkSupportTests() { runBenchmarkSupportTestsImpl(); }
