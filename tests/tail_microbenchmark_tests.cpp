#include "common/benchmark_output.hpp"
#include "common/benchmark_stats.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "small_sort_test_types.hpp"
#include "tail/tail_benchmark_output.hpp"
#include "tail/tail_corpus.hpp"
#include "tail/tail_sort_variants.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace forest_sorting::test_support;
using forest_sorting::Node;
using forest_sorting::UInt128;

void test_tail_micro_output_schema_and_escaping() {
    constexpr std::array<std::string_view, 20> kExpectedNames = {
        "workload",
        "pattern",
        "source_size",
        "size",
        "min_tail_size",
        "max_tail_size",
        "ranges",
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
        std::optional<std::size_t> rangeSize;
        std::size_t minTailSize;
        std::size_t maxTailSize;
        std::size_t rangeCount;
        std::string algorithm;
        SampleStats stats;
        double deltaMedianPct;
        ConfidenceInterval deltaPctCi95;
        std::string winner;
        std::string status;
    };

    TestMicroResult baseline{
        "synthetic", "a,b", {},           8,           8,   8, 4, "linear",
        {},          12.0,  {10.0, 14.0}, "candidate", "ok"};
    baseline.stats = computeSampleStats({10.0, 12.0, 14.0});
    TestMicroResult failed{"synthetic", "failed", {},           8,  8,
                           8,           4,        "binary",     {}, 0.0,
                           {},          "none",   "sort failed"};
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
void requireShellSorterForAllTailSizes(Sorter sorter,
                                       std::string_view sorterName) {
    for (std::size_t rangeSize = 0;
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

    requireShellSorterForAllTailSizes(ShellGap10_4_1SmallSorterDynamic{},
                                      "shell-gap-10-4-1");
    requireShellSorterForAllTailSizes(ShellGap3_2_1SmallSorterDynamic{},
                                      "shell-gap-3-2-1");
    requireShellSorterForAllTailSizes(ShellGap16_7_3_1SmallSorterDynamic{},
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
    ShellGap10_4_1SmallSorterDynamic{}(
        order, cachedNodes, CachedScratchTestTraits{}, 0, order.size());
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
        sizes.push_back(tail.size());
    }
    std::sort(sizes.begin(), sizes.end());
    require((sizes == std::vector<std::size_t>{8, 32}),
            "production MSD tail capture missed terminal callback ranges");

    const auto capped =
        captureProductionMsdTails(ids.size(), idForIndex, 1, 123U);
    require(capped.size() == 1,
            "production MSD tail capture ignored its reservoir cap");
}

void runTailMicrobenchmarkTests() {
    runTest("tail micro output schema and escaping",
            test_tail_micro_output_schema_and_escaping);
    runTest("tail micro output renderers share normalized schema",
            test_tail_micro_output_renderers_share_normalized_schema);
    runTest("shell gap tail sorters", test_shell_gap_tail_sorters);
    runTest("production MSD tail capture", test_production_msd_tail_capture);
}
