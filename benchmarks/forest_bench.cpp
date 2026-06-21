#include "benchmark_stats.hpp"
#include "forest_benchmark_output.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "parent_index_baselines.hpp"
#include "sort_registry.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
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
    std::vector<ParentKind> parents = vectorFromArray(defaultParentKinds());
    std::vector<SortKind> sorts = defaultSortKinds();
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
    std::vector<double> pipelineSamples;
    std::vector<double> verifySamples;
    SampleStats parentStats;
    SampleStats sortStats;
    SampleStats pipelineStats;
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
    std::string pipelineBaselineParent;
    std::string pipelineBaselineSort;
    std::string pipelineComparisonStatus = "none";
    std::string pipelineWinner = "none";
    double pipelineDeltaMedianMs = 0.0;
    double pipelineDeltaMedianPct = 0.0;
    ConfidenceInterval pipelineDeltaPctCi95;
    std::string status = "ok";
};

double timeParentBuildMs(const std::vector<Node> &nodes, ParentKind parentKind,
                         ParentBuildArtifacts &artifacts) {
    const auto start = std::chrono::steady_clock::now();
    artifacts = buildParentArtifactsForKind(parentKind, nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double timeSortMs(const std::vector<Node> &nodes,
                  const ParentBuildArtifacts &artifacts, SortKind sortKind,
                  std::vector<Node> &sorted) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<std::size_t> *idPermutation =
        artifacts.hasIdPermutation ? &artifacts.idPermutation : nullptr;
    sorted = sortForestForKind(sortKind, nodes, artifacts.parentIndex,
                               idPermutation);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double timeVerifyMs(const std::vector<Node> &nodes, bool &verified) {
    const auto start = std::chrono::steady_clock::now();
    verified = verifySortedByDepthAndId(nodes);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
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
    for (ParentKind parentKind : registeredParentKinds()) {
        if (value == parentName(parentKind)) {
            return parentKind;
        }
    }
    throw std::runtime_error("unknown parent builder: " + std::string(value));
}

SortKind parseSort(std::string_view value) { return parseSortKind(value); }

std::size_t parseSize(std::string_view value) {
    const std::size_t size =
        static_cast<std::size_t>(std::stoull(std::string(value)));
    if (size == 0) {
        throw std::runtime_error("dataset size must be greater than 0");
    }
    return size;
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
            if (value == "default") {
                options.parents = vectorFromArray(defaultParentKinds());
            } else {
                const ParentKind parsedParent = parseParent(value);
                if (std::find(options.parents.begin(), options.parents.end(),
                              parsedParent) == options.parents.end()) {
                    options.parents.push_back(parsedParent);
                }
            }
        } else if (option == "--sort") {
            if (!customSorts) {
                options.sorts.clear();
                customSorts = true;
            }
            if (value == "default") {
                options.sorts = defaultSortKinds();
            } else {
                const SortKind parsedSort = parseSort(value);
                if (std::find(options.sorts.begin(), options.sorts.end(),
                              parsedSort) == options.sorts.end()) {
                    options.sorts.push_back(parsedSort);
                }
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

    if (options.sizes.empty()) {
        throw std::runtime_error("must benchmark at least one size");
    }
    if (options.datasets.empty()) {
        throw std::runtime_error("must benchmark at least one dataset kind");
    }
    if (options.parents.empty()) {
        throw std::runtime_error(
            "must benchmark at least one parent builder kind");
    }
    if (options.sorts.empty()) {
        throw std::runtime_error(
            "must benchmark at least one sort algorithm kind");
    }
    if (options.dataSeeds.empty()) {
        throw std::runtime_error("must specify at least one data seed");
    }

    if (options.hasBaselineSort) {
        bool found = false;
        for (SortKind sort : options.sorts) {
            if (sort == options.baselineSort) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("baseline-sort is not included in the "
                                     "sort algorithms being benchmarked");
        }
    }

    if (options.hasBaselineParent) {
        bool found = false;
        for (ParentKind parent : options.parents) {
            if (parent == options.baselineParent) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("baseline-parent is not included in the "
                                     "parent builders being benchmarked");
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
              << "  --parent "
                 "unordered|flat|control|control-xor-hash|radix|radix-byte-msd|"
                 "default\n"
              << "  --sort ";
    bool first = true;
    for (std::size_t entryIdx = 0; entryIdx < getSortRegistry().size();
         ++entryIdx) {
        if (getSortRegistry()[entryIdx].category == SortCategory::Alias) {
            continue;
        }
        if (!first) {
            std::cout << "|";
        }
        std::cout << getSortRegistry()[entryIdx].name;
        first = false;
    }
    std::cout
        << "|default\n"
        << "                                   depth2 labels use typed "
           "uint16_t "
           "depth payloads; u8/u16/u32 chunk labels fix ID chunk width; "
           "range-ladder labels choose u8/u16/u32 once per equal-depth range; "
           "full-clear and bitmask-le512 suffixes identify counter policy; "
           "default excludes opt-in tuning experiments\n"
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
    ParentBuildArtifacts parentArtifacts;
    std::vector<Node> sorted;
    bool verified = false;

    const double parentMs =
        timeParentBuildMs(context.nodes, parentKind, parentArtifacts);
    const double sortMs =
        timeSortMs(context.nodes, parentArtifacts, sortKind, sorted);
    const double verifyMs = timeVerifyMs(sorted, verified);

    if (recordSample) {
        result.parentSamples.push_back(parentMs);
        result.sortSamples.push_back(sortMs);
        result.pipelineSamples.push_back(parentMs + sortMs);
        result.verifySamples.push_back(verifyMs);
    }

    if (parentArtifacts.parentIndex != context.expectedParent) {
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
    result.pipelineStats = computeSampleStats(result.pipelineSamples);
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

BenchmarkResult *findPipelineBaseline(std::vector<BenchmarkResult> &results,
                                      const BenchmarkResult &result,
                                      std::string_view baselineParent,
                                      std::string_view baselineSort) {
    for (BenchmarkResult &candidate : results) {
        if (sameComparisonGroup(result, candidate) &&
            candidate.parentBuilder == baselineParent &&
            candidate.sortAlgorithm == baselineSort) {
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

void applyPipelineBaseline(BenchmarkResult &result,
                           const BenchmarkResult &baseline) {
    result.pipelineBaselineParent = baseline.parentBuilder;
    result.pipelineBaselineSort = baseline.sortAlgorithm;
    result.pipelineComparisonStatus = "ok";
    result.pipelineDeltaMedianMs = medianOfSamples(
        pairedAbsoluteDeltas(result.pipelineSamples, baseline.pipelineSamples));
    result.pipelineDeltaMedianPct = medianOfSamples(
        pairedRelativeDeltas(result.pipelineSamples, baseline.pipelineSamples));
    result.pipelineDeltaPctCi95 = bootstrapPairedRelativeDeltaCi95(
        result.pipelineSamples, baseline.pipelineSamples);
    result.pipelineWinner = std::string(classifyBenchmarkWinner(
        result.pipelineDeltaMedianPct, result.pipelineDeltaPctCi95));
}

std::string seedHex(uint32_t seed) {
    std::ostringstream output;
    output << "0x" << std::hex << seed;
    return output.str();
}

void printDepthRangeStats(const DatasetContext &context) {
    if (context.nodes.empty()) {
        return;
    }
    auto computed = forest_sorting::detail::computeDepths<2>(
        context.nodes, context.expectedParent,
        forest_sorting::UInt128NodeTraits{});
    const auto &depths = computed.values;
    if (depths.empty()) {
        return;
    }

    std::vector<std::size_t> counts(
        static_cast<std::size_t>(computed.observedMax) + 1, 0);
    for (auto depthVal : depths) {
        counts[depthVal]++;
    }

    std::size_t depth_range_count = 0;
    std::size_t depth_range_max = 0;
    std::size_t nodes_le_256 = 0;
    std::size_t nodes_le_512 = 0;
    std::size_t nodes_le_1024 = 0;
    std::size_t nodes_le_2048 = 0;
    std::size_t nodes_le_4096 = 0;
    std::size_t nodes_le_16384 = 0;
    std::size_t nodes_le_32768 = 0;
    std::size_t nodes_le_65536 = 0;

    for (std::size_t size : counts) {
        if (size == 0) {
            continue;
        }
        depth_range_count++;
        depth_range_max = std::max(depth_range_max, size);
        if (size <= 256) {
            nodes_le_256 += size;
        }
        if (size <= 512) {
            nodes_le_512 += size;
        }
        if (size <= 1024) {
            nodes_le_1024 += size;
        }
        if (size <= 2048) {
            nodes_le_2048 += size;
        }
        if (size <= 4096) {
            nodes_le_4096 += size;
        }
        if (size <= 16384) {
            nodes_le_16384 += size;
        }
        if (size <= 32768) {
            nodes_le_32768 += size;
        }
        if (size <= 65536) {
            nodes_le_65536 += size;
        }
    }

    std::cout << "Depth-range stats for dataset "
              << datasetName(context.datasetKind) << " (size "
              << context.nodeCount << ", seed " << seedHex(context.dataSeed)
              << "):\n"
              << "  depth_range_count:        " << depth_range_count << "\n"
              << "  depth_range_max:          " << depth_range_max << "\n"
              << "  nodes_in_ranges_le_256:   " << nodes_le_256 << "\n"
              << "  nodes_in_ranges_le_512:   " << nodes_le_512 << "\n"
              << "  nodes_in_ranges_le_1024:  " << nodes_le_1024 << "\n"
              << "  nodes_in_ranges_le_2048:  " << nodes_le_2048 << "\n"
              << "  nodes_in_ranges_le_4096:  " << nodes_le_4096 << "\n"
              << "  nodes_in_ranges_le_16384: " << nodes_le_16384 << "\n"
              << "  nodes_in_ranges_le_32768: " << nodes_le_32768 << "\n"
              << "  nodes_in_ranges_le_65536: " << nodes_le_65536 << "\n\n";
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
                if (options.format == OutputFormat::Table) {
                    printDepthRangeStats(context);
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
                result.pipelineSamples.reserve(
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

    if (options.hasBaselineParent && options.hasBaselineSort) {
        const std::string_view baselineParent =
            parentName(options.baselineParent);
        const std::string_view baselineSort = sortName(options.baselineSort);
        for (BenchmarkResult &result : results) {
            if (result.parentBuilder == baselineParent &&
                result.sortAlgorithm == baselineSort) {
                result.pipelineBaselineParent = std::string(baselineParent);
                result.pipelineBaselineSort = std::string(baselineSort);
                result.pipelineComparisonStatus = "baseline";
                continue;
            }
            BenchmarkResult *baseline = findPipelineBaseline(
                results, result, baselineParent, baselineSort);
            if (baseline == nullptr) {
                result.pipelineComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "pipeline-baseline-missing");
            } else {
                applyPipelineBaseline(result, *baseline);
            }
        }
    }

    return results;
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
              << "  " << std::setw(kTimingColumnWidth) << "pipeline_med_ms"
              << "  " << std::setw(kTimingColumnWidth) << "verify_med_ms"
              << "  " << std::setw(kDeltaColumnWidth) << "sort_delta_%"
              << "  " << std::setw(kDeltaColumnWidth) << "parent_delta_%"
              << "  " << std::setw(kDeltaColumnWidth) << "pipeline_delta_%"
              << "  " << std::setw(kWinnerColumnWidth) << "sort_win"
              << "  " << std::setw(kWinnerColumnWidth) << "parent_win"
              << "  " << std::setw(kWinnerColumnWidth) << "pipeline_win"
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
        printTiming(result.pipelineStats.median);
        printTiming(result.verifyStats.median);
        std::cout << "  " << std::setw(kDeltaColumnWidth)
                  << result.sortDeltaMedianPct << "  "
                  << std::setw(kDeltaColumnWidth) << result.parentDeltaMedianPct
                  << "  " << std::setw(kDeltaColumnWidth)
                  << result.pipelineDeltaMedianPct << "  "
                  << std::setw(kWinnerColumnWidth) << result.sortWinner << "  "
                  << std::setw(kWinnerColumnWidth) << result.parentWinner;
        std::cout << "  " << std::setw(kWinnerColumnWidth)
                  << result.pipelineWinner;
        std::cout << "  " << result.status << "\n";
    }
}

std::vector<BenchmarkOutputRow>
makeBenchmarkOutputRows(const std::vector<BenchmarkResult> &results) {
    std::vector<BenchmarkOutputRow> rows;
    rows.reserve(results.size());
    for (const BenchmarkResult &result : results) {
        rows.push_back(
            makeBenchmarkOutputRow(result, seedHex(result.dataSeed)));
    }
    return rows;
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

void printJson(const std::vector<BenchmarkOutputRow> &rows,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"warmup\": " << options.warmup
              << ",\n  \"sample_output\": \""
              << sampleOutputName(options.sampleOutput) << "\""
              << ",\n  \"shuffle\": " << (options.shuffle ? "true" : "false")
              << ",\n  \"order_seed\": \"" << seedHex(options.orderSeed)
              << "\",\n  ";
    printJsonDataSeeds(options.dataSeeds);
    std::cout << ",\n  \"results\": ";
    printBenchmarkJsonRows(std::cout, rows,
                           options.sampleOutput != SampleOutput::None,
                           options.sampleOutput == SampleOutput::Raw);
    std::cout << "\n}\n";
}

void printResults(const std::vector<BenchmarkResult> &results,
                  const Options &options) {
    switch (options.format) {
    case OutputFormat::Table:
        printTable(results);
        break;
    case OutputFormat::Csv:
        printBenchmarkDelimited(std::cout, makeBenchmarkOutputRows(results),
                                ',');
        break;
    case OutputFormat::Tsv:
        printBenchmarkDelimited(std::cout, makeBenchmarkOutputRows(results),
                                '\t');
        break;
    case OutputFormat::Json:
        printJson(makeBenchmarkOutputRows(results), options);
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
