#include "common/benchmark_cli.hpp"
#include "common/benchmark_output.hpp"
#include "common/benchmark_stats.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/adaptive_sort_variants.hpp"
#include "full/forest_benchmark_output.hpp"
#include "hashed_test_bytes.hpp"
#include "small_sort_test_types.hpp"
#include "test_bytes.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace forest_sorting::test_support;
using forest_sorting::Node;
using forest_sorting::UInt128;

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
    std::vector<int> values;
    appendUniqueSelection(values, 1);
    appendUniqueSelection(values, 1);
    require((values == std::vector<int>{1}),
            "benchmark CLI retained a duplicate selection");
    require(!hasSelectionOtherThan(values, 1),
            "single baseline selection reported a candidate");
    appendUniqueSelection(values, 2);
    require(hasSelectionOtherThan(values, 1),
            "alternate benchmark selection was not detected");
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
    baseline.parentBaseline = "control";
    baseline.parentComparisonStatus = "baseline";
    baseline.parentWinner = "none";
    baseline.pipelineBaselineParent = "control";
    baseline.pipelineBaselineSort = "candidate";
    baseline.pipelineComparisonStatus = "baseline";
    baseline.pipelineWinner = "none";
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
    requireSmallSorterMatchesExpected(LinearSmallSorterDynamic{},
                                      "dynamic linear");
    requireSmallSorterMatchesExpected(BinarySmallSorterDynamic{},
                                      "dynamic binary");
    requireSmallSorterMatchesExpected(ExponentialSmallSorterDynamic{},
                                      "dynamic exponential");
    requireSmallSorterMatchesExpected(BranchlessBitwiseSmallSorterDynamic{},
                                      "dynamic branchless-bitwise");

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

void runBenchmarkSupportTests() {
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
