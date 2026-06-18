#include "benchmark_stats.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "parent_index_baselines.hpp"
#include "sort_registry.hpp"
#include "uint128_fixtures.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <ratio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using forest_sorting::Node;
using forest_sorting::UInt128;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

constexpr int kDatasetColumnWidth = 24;
constexpr int kCountColumnWidth = 12;
constexpr int kNameColumnWidth = 16;
constexpr int kTimingColumnWidth = 14;
constexpr int kTimingValueWidth = 14;
constexpr int kDeltaColumnWidth = 14;
constexpr int kWinnerColumnWidth = 10;
constexpr uint32_t kDefaultOrderSeed = 0x5eedU;

enum class OutputFormat : uint8_t {
    Table,
    Csv,
    Tsv,
    Json,
};

enum class SampleOutput : uint8_t {
    None,
    Summary,
    Raw,
};

template <typename Value, std::size_t Count>
std::vector<Value> vectorFromArray(const std::array<Value, Count> &values) {
    return {values.begin(), values.end()};
}

struct Options {
    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> sizes = {10000, 100000};
    std::vector<DatasetKind> datasets = vectorFromArray(allDatasetKinds());
    std::vector<ParentKind> parents = vectorFromArray(allParentKinds());
    std::vector<SortKind> sorts = allSortKinds();
    int iterations = 7;
    int warmup = 1;
    bool shuffle = false;
    uint32_t orderSeed = kDefaultOrderSeed;
    std::vector<uint32_t> dataSeeds = {kDefaultBenchmarkDataSeed};
    SampleOutput sampleOutput = SampleOutput::Summary;
    bool hasBaselineSort = false;
    SortKind baselineSort = SortKind::Comparison;
    bool hasBaselineParent = false;
    ParentKind baselineParent = ParentKind::Control;
    bool help = false;
};

struct DatasetContext {
    std::size_t nodeCount = 0;
    DatasetKind datasetKind = DatasetKind::Random;
    uint32_t dataSeed = kDefaultBenchmarkDataSeed;
    std::vector<Node> nodes;
    std::vector<std::size_t> expectedParent;
    std::vector<UInt128> expectedIds;
};

struct BenchmarkResult {
    std::string dataset;
    std::size_t nodeCount = 0;
    uint32_t dataSeed = kDefaultBenchmarkDataSeed;
    std::string parentBuilder;
    std::string sortAlgorithm;
    std::vector<double> parentSamples;
    std::vector<double> sortSamples;
    std::vector<double> verifySamples;
    SampleStats parentStats;
    SampleStats sortStats;
    SampleStats verifyStats;
    std::string sortBaseline;
    std::string sortComparisonStatus = "none";
    std::string sortWinner = "none";
    double sortDeltaMedianMs = 0.0;
    double sortDeltaMedianPct = 0.0;
    ConfidenceInterval sortDeltaPctCi95;
    std::string parentBaseline;
    std::string parentComparisonStatus = "none";
    std::string parentWinner = "none";
    double parentDeltaMedianMs = 0.0;
    double parentDeltaMedianPct = 0.0;
    ConfidenceInterval parentDeltaPctCi95;
    std::string status = "ok";
};

double timeParentBuildMs(const std::vector<Node> &nodes, ParentKind parentKind,
                         std::vector<std::size_t> &parentIndex) {
    const auto start = std::chrono::steady_clock::now();
    parentIndex = buildParentIndexForKind(parentKind, nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double timeSortMs(const std::vector<Node> &nodes,
                  const std::vector<std::size_t> &parentIndex,
                  SortKind sortKind, std::vector<Node> &sorted) {
    const auto start = std::chrono::steady_clock::now();
    sorted = sortForestForKind(sortKind, nodes, parentIndex);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double timeVerifyMs(const std::vector<Node> &nodes, bool &verified) {
    const auto start = std::chrono::steady_clock::now();
    verified = verifySortedByDepthAndId(nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

OutputFormat parseFormat(std::string_view value) {
    if (value == "table") {
        return OutputFormat::Table;
    }
    if (value == "csv") {
        return OutputFormat::Csv;
    }
    if (value == "tsv") {
        return OutputFormat::Tsv;
    }
    if (value == "json") {
        return OutputFormat::Json;
    }
    throw std::runtime_error("unknown format: " + std::string(value));
}

SampleOutput parseSampleOutput(std::string_view value) {
    if (value == "none") {
        return SampleOutput::None;
    }
    if (value == "summary") {
        return SampleOutput::Summary;
    }
    if (value == "raw") {
        return SampleOutput::Raw;
    }
    throw std::runtime_error("unknown sample output: " + std::string(value));
}

std::string_view sampleOutputName(SampleOutput sampleOutput) {
    switch (sampleOutput) {
    case SampleOutput::None:
        return "none";
    case SampleOutput::Summary:
        return "summary";
    case SampleOutput::Raw:
        return "raw";
    }
    return "unknown";
}

DatasetKind parseDataset(std::string_view value) {
    for (DatasetKind datasetKind : allDatasetKinds()) {
        if (value == datasetName(datasetKind)) {
            return datasetKind;
        }
    }
    throw std::runtime_error("unknown dataset: " + std::string(value));
}

ParentKind parseParent(std::string_view value) {
    for (ParentKind parentKind : allParentKinds()) {
        if (value == parentName(parentKind)) {
            return parentKind;
        }
    }
    throw std::runtime_error("unknown parent builder: " + std::string(value));
}

SortKind parseSort(std::string_view value) { return parseSortKind(value); }

std::size_t parseSize(std::string_view value) {
    return static_cast<std::size_t>(std::stoull(std::string(value)));
}

int parsePositiveInt(std::string_view value, const char *optionName) {
    const int parsed = std::stoi(std::string(value));
    if (parsed < 1) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be at least 1");
    }
    return parsed;
}

int parseNonNegativeInt(std::string_view value, const char *optionName) {
    const int parsed = std::stoi(std::string(value));
    if (parsed < 0) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be non-negative");
    }
    return parsed;
}

uint32_t parseSeed(std::string_view value, const char *optionName) {
    if (value == "random") {
        std::random_device device;
        std::uniform_int_distribution<uint32_t> distribution(
            std::numeric_limits<uint32_t>::min(),
            std::numeric_limits<uint32_t>::max());
        return distribution(device);
    }

    std::size_t parsedLength = 0;
    const unsigned long parsed =
        std::stoul(std::string(value), &parsedLength, 0);
    if (parsedLength != value.size() ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be a uint32 value or random");
    }
    return static_cast<uint32_t>(parsed);
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customSizes = false;
    bool customDatasets = false;
    bool customParents = false;
    bool customSorts = false;
    bool customDataSeeds = false;

    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string_view option = argv[argIndex];
        if (option == "--help") {
            options.help = true;
            return options;
        }
        if (option == "--shuffle") {
            options.shuffle = true;
            continue;
        }

        if (argIndex + 1 >= argc) {
            throw std::runtime_error("missing value for " +
                                     std::string(option));
        }
        const std::string_view value = argv[++argIndex];

        if (option == "--format") {
            options.format = parseFormat(value);
        } else if (option == "--size") {
            if (!customSizes) {
                options.sizes.clear();
                customSizes = true;
            }
            options.sizes.push_back(parseSize(value));
        } else if (option == "--dataset") {
            if (!customDatasets) {
                options.datasets.clear();
                customDatasets = true;
            }
            if (value == "all") {
                options.datasets = vectorFromArray(allDatasetKinds());
            } else {
                options.datasets.push_back(parseDataset(value));
            }
        } else if (option == "--parent") {
            if (!customParents) {
                options.parents.clear();
                customParents = true;
            }
            if (value == "all") {
                options.parents = vectorFromArray(allParentKinds());
            } else {
                options.parents.push_back(parseParent(value));
            }
        } else if (option == "--sort") {
            if (!customSorts) {
                options.sorts.clear();
                customSorts = true;
            }
            if (value == "all") {
                options.sorts = allSortKinds();
            } else {
                options.sorts.push_back(parseSort(value));
            }
        } else if (option == "--iterations") {
            options.iterations = parsePositiveInt(value, "--iterations");
        } else if (option == "--warmup") {
            options.warmup = parseNonNegativeInt(value, "--warmup");
        } else if (option == "--baseline-sort") {
            options.hasBaselineSort = true;
            options.baselineSort = parseSort(value);
        } else if (option == "--baseline-parent") {
            options.hasBaselineParent = true;
            options.baselineParent = parseParent(value);
        } else if (option == "--sample-output") {
            options.sampleOutput = parseSampleOutput(value);
        } else if (option == "--order-seed") {
            options.orderSeed = parseSeed(value, "--order-seed");
        } else if (option == "--data-seed") {
            if (!customDataSeeds) {
                options.dataSeeds.clear();
                customDataSeeds = true;
            }
            options.dataSeeds.push_back(parseSeed(value, "--data-seed"));
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    return options;
}

void printHelp() {
    std::cout << "usage: forest-sorting-bench [options]\n"
              << "\n"
              << "options:\n"
              << "  --format table|csv|tsv|json\n"
              << "  --size N                         repeatable\n"
              << "  --dataset "
                 "random|outliers|same-high64|same-high32|sequential|"
                 "external-parents|siblings|all\n"
              << "  --parent unordered|flat|control|radix|all\n"
              << "  --sort ";
    for (std::size_t entryIdx = 0; entryIdx < kSortRegistry.size();
         ++entryIdx) {
        if (entryIdx > 0) {
            std::cout << "|";
        }
        std::cout << kSortRegistry[entryIdx].name;
    }
    std::cout
        << "|all\n"
        << "                                   depth2 labels are fixed-prefix "
           "benchmarks; adaptive byte/u32/chunk labels use the unified "
           "1/4/8-byte chunk-MSD engine; all selects the default set and "
           "excludes opt-in tuning experiments\n"
        << "  --iterations N\n"
        << "  --warmup N\n"
        << "  --baseline-sort NAME             compare selected sorts against "
           "NAME\n"
        << "  --baseline-parent NAME           compare selected parents "
           "against "
           "NAME\n"
        << "  --sample-output none|summary|raw JSON sample detail (default "
           "summary)\n"
        << "  --shuffle                        randomize algorithm execution "
           "order per sample\n"
        << "  --order-seed N|random            shuffle seed (default 0x5eed)\n"
        << "  --data-seed N|random             generated data seed, repeatable "
           "(default "
           "0x5eed1234)\n"
        << "  --help\n";
}

std::string appendStatus(std::string status, std::string_view marker) {
    if (status.find(marker) != std::string::npos) {
        return status;
    }
    if (status == "ok") {
        return std::string(marker);
    }
    status += "|";
    status += marker;
    return status;
}

void recordBenchmarkSample(BenchmarkResult &result,
                           const DatasetContext &context, ParentKind parentKind,
                           SortKind sortKind, bool recordSample) {
    std::vector<std::size_t> parentIndex;
    std::vector<Node> sorted;
    bool verified = false;

    const double parentMs =
        timeParentBuildMs(context.nodes, parentKind, parentIndex);
    const double sortMs =
        timeSortMs(context.nodes, parentIndex, sortKind, sorted);
    const double verifyMs = timeVerifyMs(sorted, verified);

    if (recordSample) {
        result.parentSamples.push_back(parentMs);
        result.sortSamples.push_back(sortMs);
        result.verifySamples.push_back(verifyMs);
    }

    if (parentIndex != context.expectedParent) {
        result.status = appendStatus(result.status, "parent-mismatch");
    }

    bool sortMatch = sorted.size() == context.expectedIds.size();
    if (sortMatch) {
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i].id != context.expectedIds[i]) {
                sortMatch = false;
                break;
            }
        }
    }

    if (!sortMatch) {
        result.status = appendStatus(result.status, "sort-mismatch");
    }

    if (!verified) {
        result.status = appendStatus(result.status, "verify-failed");
    }
}

void summarizeBenchmarkResult(BenchmarkResult &result) {
    result.parentStats = computeSampleStats(result.parentSamples);
    result.sortStats = computeSampleStats(result.sortSamples);
    result.verifyStats = computeSampleStats(result.verifySamples);
}

struct Job {
    const DatasetContext *context;
    ParentKind parent;
    SortKind sort;
    std::size_t resultIndex;
};

bool sameComparisonGroup(const BenchmarkResult &result,
                         const BenchmarkResult &candidate) {
    return result.dataset == candidate.dataset &&
           result.nodeCount == candidate.nodeCount &&
           result.dataSeed == candidate.dataSeed;
}

BenchmarkResult *findSortBaseline(std::vector<BenchmarkResult> &results,
                                  const BenchmarkResult &result,
                                  std::string_view baselineSort) {
    for (BenchmarkResult &candidate : results) {
        if (sameComparisonGroup(result, candidate) &&
            result.parentBuilder == candidate.parentBuilder &&
            candidate.sortAlgorithm == baselineSort) {
            return &candidate;
        }
    }
    return nullptr;
}

BenchmarkResult *findParentBaseline(std::vector<BenchmarkResult> &results,
                                    const BenchmarkResult &result,
                                    std::string_view baselineParent) {
    for (BenchmarkResult &candidate : results) {
        if (sameComparisonGroup(result, candidate) &&
            result.sortAlgorithm == candidate.sortAlgorithm &&
            candidate.parentBuilder == baselineParent) {
            return &candidate;
        }
    }
    return nullptr;
}

void applySortBaseline(BenchmarkResult &result,
                       const BenchmarkResult &baseline) {
    result.sortBaseline = baseline.sortAlgorithm;
    result.sortComparisonStatus = "ok";
    result.sortDeltaMedianMs = medianOfSamples(
        pairedAbsoluteDeltas(result.sortSamples, baseline.sortSamples));
    result.sortDeltaMedianPct = medianOfSamples(
        pairedRelativeDeltas(result.sortSamples, baseline.sortSamples));
    result.sortDeltaPctCi95 = bootstrapPairedRelativeDeltaCi95(
        result.sortSamples, baseline.sortSamples);
    result.sortWinner = std::string(classifyBenchmarkWinner(
        result.sortDeltaMedianPct, result.sortDeltaPctCi95));
}

void applyParentBaseline(BenchmarkResult &result,
                         const BenchmarkResult &baseline) {
    result.parentBaseline = baseline.parentBuilder;
    result.parentComparisonStatus = "ok";
    result.parentDeltaMedianMs = medianOfSamples(
        pairedAbsoluteDeltas(result.parentSamples, baseline.parentSamples));
    result.parentDeltaMedianPct = medianOfSamples(
        pairedRelativeDeltas(result.parentSamples, baseline.parentSamples));
    result.parentDeltaPctCi95 = bootstrapPairedRelativeDeltaCi95(
        result.parentSamples, baseline.parentSamples);
    result.parentWinner = std::string(classifyBenchmarkWinner(
        result.parentDeltaMedianPct, result.parentDeltaPctCi95));
}

std::vector<BenchmarkResult> runBenchmarks(const Options &options) {
    std::vector<DatasetContext> contexts;
    for (std::size_t nodeCount : options.sizes) {
        for (DatasetKind datasetKind : options.datasets) {
            for (uint32_t dataSeed : options.dataSeeds) {
                DatasetContext context;
                context.nodeCount = nodeCount;
                context.datasetKind = datasetKind;
                context.dataSeed = dataSeed;
                context.nodes = makeGeneratedForestForKind(datasetKind,
                                                           nodeCount, dataSeed);
                context.expectedParent =
                    buildParentIndexForKind(ParentKind::Control, context.nodes);
                const auto canonicalSorted =
                    sortForestForKind(SortKind::Comparison, context.nodes,
                                      context.expectedParent);

                context.expectedIds.reserve(canonicalSorted.size());
                for (const auto &node : canonicalSorted) {
                    context.expectedIds.push_back(node.id);
                }
                contexts.push_back(std::move(context));
            }
        }
    }

    std::vector<Job> jobs;
    std::vector<BenchmarkResult> results;

    for (const auto &context : contexts) {
        for (ParentKind parentKind : options.parents) {
            for (SortKind sortKind : options.sorts) {
                BenchmarkResult result;
                result.dataset = std::string(datasetName(context.datasetKind));
                result.nodeCount = context.nodeCount;
                result.dataSeed = context.dataSeed;
                result.parentBuilder = std::string(parentName(parentKind));
                result.sortAlgorithm = std::string(sortName(sortKind));
                result.parentSamples.reserve(
                    static_cast<std::size_t>(options.iterations));
                result.sortSamples.reserve(
                    static_cast<std::size_t>(options.iterations));
                result.verifySamples.reserve(
                    static_cast<std::size_t>(options.iterations));

                jobs.push_back(
                    {&context, parentKind, sortKind, results.size()});
                results.push_back(std::move(result));
            }
        }
    }

    std::vector<std::size_t> jobOrder = makeSequentialIndexOrder(jobs.size());

    const int totalPasses = options.warmup + options.iterations;
    for (int passIdx = 0; passIdx < totalPasses; ++passIdx) {
        if (options.shuffle) {
            jobOrder = makeSequentialIndexOrder(jobs.size());
            shuffleBenchmarkOrder(jobOrder, options.orderSeed, passIdx);
        }
        const bool recordSample = passIdx >= options.warmup;
        for (std::size_t jobIdx : jobOrder) {
            const Job &job = jobs[jobIdx];
            recordBenchmarkSample(results[job.resultIndex], *job.context,
                                  job.parent, job.sort, recordSample);
        }
    }

    for (BenchmarkResult &result : results) {
        summarizeBenchmarkResult(result);
    }

    if (options.hasBaselineSort) {
        const std::string_view baselineSort = sortName(options.baselineSort);
        for (BenchmarkResult &result : results) {
            if (result.sortAlgorithm == baselineSort) {
                result.sortBaseline = std::string(baselineSort);
                result.sortComparisonStatus = "baseline";
                continue;
            }
            BenchmarkResult *baseline =
                findSortBaseline(results, result, baselineSort);
            if (baseline == nullptr) {
                result.sortComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "sort-baseline-missing");
            } else {
                applySortBaseline(result, *baseline);
            }
        }
    }

    if (options.hasBaselineParent) {
        const std::string_view baselineParent =
            parentName(options.baselineParent);
        for (BenchmarkResult &result : results) {
            if (result.parentBuilder == baselineParent) {
                result.parentBaseline = std::string(baselineParent);
                result.parentComparisonStatus = "baseline";
                continue;
            }
            BenchmarkResult *baseline =
                findParentBaseline(results, result, baselineParent);
            if (baseline == nullptr) {
                result.parentComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "parent-baseline-missing");
            } else {
                applyParentBaseline(result, *baseline);
            }
        }
    }

    return results;
}

std::string csvEscape(std::string_view value) {
    bool needsQuotes = false;
    std::string escaped;
    for (char character : value) {
        if (character == '"' || character == ',' || character == '\n' ||
            character == '\r') {
            needsQuotes = true;
        }
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    if (!needsQuotes) {
        return escaped;
    }
    return "\"" + escaped + "\"";
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    for (char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::string seedHex(uint32_t seed) {
    std::ostringstream output;
    output << "0x" << std::hex << seed;
    return output.str();
}

void printBenchmarkHeader() {
    std::cout << std::left << std::setw(kDatasetColumnWidth) << "dataset"
              << std::right << "  " << std::setw(kCountColumnWidth)
              << "node_count"
              << "  " << std::setw(kNameColumnWidth) << "data_seed"
              << "  " << std::setw(kNameColumnWidth) << "parent_builder"
              << "  " << std::setw(kNameColumnWidth) << "sort_algorithm"
              << "  " << std::setw(kTimingColumnWidth) << "parent_med_ms"
              << "  " << std::setw(kTimingColumnWidth) << "sort_med_ms"
              << "  " << std::setw(kTimingColumnWidth) << "verify_med_ms"
              << "  " << std::setw(kDeltaColumnWidth) << "sort_delta_%"
              << "  " << std::setw(kDeltaColumnWidth) << "parent_delta_%"
              << "  " << std::setw(kWinnerColumnWidth) << "sort_win"
              << "  " << std::setw(kWinnerColumnWidth) << "parent_win"
              << "  status\n";
}

void printTiming(double milliseconds) {
    std::cout << "  " << std::setw(kTimingValueWidth) << milliseconds << " ms";
}

void printTable(const std::vector<BenchmarkResult> &results) {
    printBenchmarkHeader();
    for (const BenchmarkResult &result : results) {
        std::cout << std::left << std::setw(kDatasetColumnWidth)
                  << result.dataset << std::right << "  "
                  << std::setw(kCountColumnWidth) << result.nodeCount << "  "
                  << std::setw(kNameColumnWidth) << seedHex(result.dataSeed)
                  << "  " << std::setw(kNameColumnWidth) << result.parentBuilder
                  << "  " << std::setw(kNameColumnWidth)
                  << result.sortAlgorithm;
        printTiming(result.parentStats.median);
        printTiming(result.sortStats.median);
        printTiming(result.verifyStats.median);
        std::cout << "  " << std::setw(kDeltaColumnWidth)
                  << result.sortDeltaMedianPct << "  "
                  << std::setw(kDeltaColumnWidth) << result.parentDeltaMedianPct
                  << "  " << std::setw(kWinnerColumnWidth) << result.sortWinner
                  << "  " << std::setw(kWinnerColumnWidth)
                  << result.parentWinner;
        std::cout << "  " << result.status << "\n";
    }
}

void printStatsHeader(std::string_view prefix, char delimiter) {
    std::cout << prefix << "_min_ms" << delimiter << prefix << "_median_ms"
              << delimiter << prefix << "_mean_ms" << delimiter << prefix
              << "_stddev_ms" << delimiter << prefix << "_max_ms" << delimiter
              << prefix << "_ci95_low_ms" << delimiter << prefix
              << "_ci95_high_ms";
}

void printStatsDelimited(const SampleStats &stats, char delimiter) {
    std::cout << stats.min << delimiter << stats.median << delimiter
              << stats.mean << delimiter << stats.stddev << delimiter
              << stats.max << delimiter << stats.ci95.low << delimiter
              << stats.ci95.high;
}

void printDelimited(const std::vector<BenchmarkResult> &results,
                    char delimiter) {
    const bool csv = delimiter == ',';
    std::cout << "dataset" << delimiter << "node_count" << delimiter
              << "data_seed" << delimiter << "parent_builder" << delimiter
              << "sort_algorithm" << delimiter << "samples" << delimiter
              << "sort_baseline" << delimiter << "sort_comparison_status"
              << delimiter << "sort_winner" << delimiter
              << "sort_delta_median_ms" << delimiter << "sort_delta_median_pct"
              << delimiter << "sort_delta_ci95_low_pct" << delimiter
              << "sort_delta_ci95_high_pct" << delimiter << "parent_baseline"
              << delimiter << "parent_comparison_status" << delimiter
              << "parent_winner" << delimiter << "parent_delta_median_ms"
              << delimiter << "parent_delta_median_pct" << delimiter
              << "parent_delta_ci95_low_pct" << delimiter
              << "parent_delta_ci95_high_pct" << delimiter;
    printStatsHeader("parent", delimiter);
    std::cout << delimiter;
    printStatsHeader("sort", delimiter);
    std::cout << delimiter;
    printStatsHeader("verify", delimiter);
    std::cout << delimiter << "status\n";
    for (const BenchmarkResult &result : results) {
        if (csv) {
            std::cout << csvEscape(result.dataset) << delimiter;
        } else {
            std::cout << result.dataset << delimiter;
        }
        std::cout << result.nodeCount << delimiter << seedHex(result.dataSeed)
                  << delimiter;
        if (csv) {
            std::cout << csvEscape(result.parentBuilder) << delimiter
                      << csvEscape(result.sortAlgorithm) << delimiter;
        } else {
            std::cout << result.parentBuilder << delimiter
                      << result.sortAlgorithm << delimiter;
        }
        std::cout << result.sortSamples.size() << delimiter;
        if (csv) {
            std::cout << csvEscape(result.sortBaseline) << delimiter;
        } else {
            std::cout << result.sortBaseline << delimiter;
        }
        if (csv) {
            std::cout << csvEscape(result.sortComparisonStatus) << delimiter;
        } else {
            std::cout << result.sortComparisonStatus << delimiter;
        }
        if (csv) {
            std::cout << csvEscape(result.sortWinner) << delimiter;
        } else {
            std::cout << result.sortWinner << delimiter;
        }
        std::cout << result.sortDeltaMedianMs << delimiter
                  << result.sortDeltaMedianPct << delimiter
                  << result.sortDeltaPctCi95.low << delimiter
                  << result.sortDeltaPctCi95.high << delimiter;
        if (csv) {
            std::cout << csvEscape(result.parentBaseline) << delimiter;
        } else {
            std::cout << result.parentBaseline << delimiter;
        }
        if (csv) {
            std::cout << csvEscape(result.parentComparisonStatus) << delimiter;
        } else {
            std::cout << result.parentComparisonStatus << delimiter;
        }
        if (csv) {
            std::cout << csvEscape(result.parentWinner) << delimiter;
        } else {
            std::cout << result.parentWinner << delimiter;
        }
        std::cout << result.parentDeltaMedianMs << delimiter
                  << result.parentDeltaMedianPct << delimiter
                  << result.parentDeltaPctCi95.low << delimiter
                  << result.parentDeltaPctCi95.high << delimiter;
        printStatsDelimited(result.parentStats, delimiter);
        std::cout << delimiter;
        printStatsDelimited(result.sortStats, delimiter);
        std::cout << delimiter;
        printStatsDelimited(result.verifyStats, delimiter);
        std::cout << delimiter;
        if (csv) {
            std::cout << csvEscape(result.status);
        } else {
            std::cout << result.status;
        }
        std::cout << "\n";
    }
}

void printJsonStats(std::string_view name, const SampleStats &stats) {
    std::cout << "\"" << name << "\": {"
              << "\"min_ms\": " << stats.min
              << ", \"median_ms\": " << stats.median
              << ", \"mean_ms\": " << stats.mean
              << ", \"stddev_ms\": " << stats.stddev
              << ", \"max_ms\": " << stats.max
              << ", \"ci95_low_ms\": " << stats.ci95.low
              << ", \"ci95_high_ms\": " << stats.ci95.high << "}";
}

void printJsonSamples(std::string_view name,
                      const std::vector<double> &samples) {
    std::cout << "\"" << name << "\": [";
    for (std::size_t sampleIdx = 0; sampleIdx < samples.size(); ++sampleIdx) {
        if (sampleIdx > 0) {
            std::cout << ", ";
        }
        std::cout << samples[sampleIdx];
    }
    std::cout << "]";
}

void printJsonDataSeeds(const std::vector<uint32_t> &dataSeeds) {
    std::cout << "\"data_seeds\": [";
    for (std::size_t seedIdx = 0; seedIdx < dataSeeds.size(); ++seedIdx) {
        if (seedIdx > 0) {
            std::cout << ", ";
        }
        std::cout << "\"" << seedHex(dataSeeds[seedIdx]) << "\"";
    }
    std::cout << "]";
}

void printJson(const std::vector<BenchmarkResult> &results,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"warmup\": " << options.warmup
              << ",\n  \"sample_output\": \""
              << sampleOutputName(options.sampleOutput) << "\""
              << ",\n  \"shuffle\": " << (options.shuffle ? "true" : "false")
              << ",\n  \"order_seed\": \"" << seedHex(options.orderSeed)
              << "\",\n  ";
    printJsonDataSeeds(options.dataSeeds);
    std::cout << ",\n  \"results\": [\n";
    for (std::size_t resultIndex = 0; resultIndex < results.size();
         ++resultIndex) {
        const BenchmarkResult &result = results[resultIndex];
        std::cout
            << "    {"
            << "\"dataset\": \"" << jsonEscape(result.dataset)
            << "\", \"node_count\": " << result.nodeCount
            << ", \"data_seed\": \"" << seedHex(result.dataSeed) << "\""
            << ", \"parent_builder\": \"" << jsonEscape(result.parentBuilder)
            << "\", \"sort_algorithm\": \"" << jsonEscape(result.sortAlgorithm)
            << "\", \"samples\": " << result.sortSamples.size()
            << ", \"sort_baseline\": \"" << jsonEscape(result.sortBaseline)
            << "\", \"sort_comparison_status\": \""
            << jsonEscape(result.sortComparisonStatus)
            << "\", \"sort_winner\": \"" << jsonEscape(result.sortWinner)
            << "\", \"sort_delta_median_ms\": " << result.sortDeltaMedianMs
            << ", \"sort_delta_median_pct\": " << result.sortDeltaMedianPct
            << ", \"sort_delta_ci95_low_pct\": " << result.sortDeltaPctCi95.low
            << ", \"sort_delta_ci95_high_pct\": "
            << result.sortDeltaPctCi95.high << ", \"parent_baseline\": \""
            << jsonEscape(result.parentBaseline)
            << "\", \"parent_comparison_status\": \""
            << jsonEscape(result.parentComparisonStatus)
            << "\", \"parent_winner\": \"" << jsonEscape(result.parentWinner)
            << "\", \"parent_delta_median_ms\": " << result.parentDeltaMedianMs
            << ", \"parent_delta_median_pct\": " << result.parentDeltaMedianPct
            << ", \"parent_delta_ci95_low_pct\": "
            << result.parentDeltaPctCi95.low
            << ", \"parent_delta_ci95_high_pct\": "
            << result.parentDeltaPctCi95.high;
        if (options.sampleOutput != SampleOutput::None) {
            std::cout << ", ";
            printJsonStats("parent", result.parentStats);
            std::cout << ", ";
            printJsonStats("sort", result.sortStats);
            std::cout << ", ";
            printJsonStats("verify", result.verifyStats);
        }
        if (options.sampleOutput == SampleOutput::Raw) {
            std::cout << ", ";
            printJsonSamples("parent_samples_ms", result.parentSamples);
            std::cout << ", ";
            printJsonSamples("sort_samples_ms", result.sortSamples);
            std::cout << ", ";
            printJsonSamples("verify_samples_ms", result.verifySamples);
        }
        std::cout << ", \"status\": \"" << jsonEscape(result.status) << "\"}";
        if (resultIndex + 1 < results.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

void printResults(const std::vector<BenchmarkResult> &results,
                  const Options &options) {
    switch (options.format) {
    case OutputFormat::Table:
        printTable(results);
        break;
    case OutputFormat::Csv:
        printDelimited(results, ',');
        break;
    case OutputFormat::Tsv:
        printDelimited(results, '\t');
        break;
    case OutputFormat::Json:
        printJson(results, options);
        break;
    }
}

int main(int argc, char **argv) {
    try {
        validateSortRegistry();
        const Options options = parseOptions(argc, argv);
        if (options.help) {
            printHelp();
            return 0;
        }
        const auto results = runBenchmarks(options);
        printResults(results, options);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-bench failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-bench failed: unknown exception\n";
        return 1;
    }
}
