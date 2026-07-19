#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OPTIONS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OPTIONS_HPP

#include "forest_sorting/benchmark_support/common/benchmark_cli.hpp"
#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/full/parent_registry.hpp"
#include "forest_sorting/benchmark_support/full/sort_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace forest_sorting::benchmark_support {

constexpr uint32_t kDefaultOrderSeed = 0x5eedU;

enum class SampleOutput : uint8_t {
    None,
    Summary,
    Raw,
};

struct Options {
    Options();

    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> sizes = {10000, 100000};
    std::vector<DatasetKind> datasets;
    std::vector<ParentKind> parents;
    std::vector<SortKind> sorts;
    int iterations = 7;
    int warmup = 1;
    bool shuffle = false;
    uint32_t orderSeed = kDefaultOrderSeed;
    std::vector<uint32_t> dataSeeds = {kDefaultBenchmarkDataSeed};
    SampleOutput sampleOutput = SampleOutput::Summary;
    bool hasBaselineSort = false;
    SortKind baselineSort = SortKind::Comparison;
    bool hasBaselineParent = false;
    ParentKind baselineParent = ParentKind::RadixJoinIdMsdChunk32;
    bool help = false;
};

SampleOutput parseSampleOutput(std::string_view value);
std::string_view sampleOutputName(SampleOutput sampleOutput);
Options parseOptions(int argc, char **argv);
void printHelp();

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OPTIONS_HPP
