#include "benchmark_stats.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "test_harness.hpp"
#include "uint128_fixtures.hpp"

#include <cstddef>
#include <cstdint>
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

void runBenchmarkSupportTests() {
    runTest("benchmark stats median and stddev",
            test_benchmark_stats_median_and_stddev);
    runTest("benchmark bootstrap CI is deterministic",
            test_benchmark_bootstrap_ci_is_deterministic);
    runTest("benchmark paired relative delta",
            test_benchmark_paired_relative_delta);
    runTest("benchmark winner classification is CI-aware",
            test_benchmark_winner_classification_is_ci_aware);
    runTest("benchmark order seed controls shuffle",
            test_benchmark_order_seed_controls_shuffle);
    runTest("benchmark data seed controls generated data",
            test_benchmark_data_seed_controls_generated_data);
    runTest("same-high32 dataset shape", test_same_high32_dataset_shape);
}
