#include "full/forest_benchmark_app.hpp"
#include "common/benchmark_cli.hpp"
#include "common/benchmark_stats.hpp"
#include "forest_sorting/detail/depth.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "full/forest_benchmark_options.hpp"
#include "full/forest_benchmark_output.hpp"
#include "full/parent_registry.hpp"
#include "full/sort_registry.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <ratio>
#include <sstream>
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
    ParentBuildArtifacts built = buildParentArtifactsForKind(parentKind, nodes);
    const auto end = std::chrono::steady_clock::now();
    artifacts = std::move(built);
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

void recordParentSample(BenchmarkResult &result, const DatasetContext &context,
                        const ParentBuildArtifacts &parentArtifacts,
                        double parentMs, bool recordSample) {
    if (recordSample) {
        result.parentSamples.push_back(parentMs);
    }
    if (parentArtifacts.parentIndex != context.expectedParent) {
        result.status = appendStatus(result.status, "parent-mismatch");
    }
}

void recordSortSample(BenchmarkResult &result, const DatasetContext &context,
                      const ParentBuildArtifacts &parentArtifacts,
                      SortKind sortKind, double parentMs, bool recordSample) {
    std::vector<Node> sorted;
    bool verified = false;

    const double sortMs =
        timeSortMs(context.nodes, parentArtifacts, sortKind, sorted);
    const double verifyMs = timeVerifyMs(sorted, verified);

    if (recordSample) {
        result.sortSamples.push_back(sortMs);
        result.pipelineSamples.push_back(parentMs + sortMs);
        result.verifySamples.push_back(verifyMs);
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
    std::size_t parentJobIndex;
    SortKind sort;
    std::size_t resultIndex;
};

struct ParentJob {
    const DatasetContext *context;
    ParentKind parent;
    ParentBuildArtifacts artifacts;
    double elapsedMs = 0.0;
    std::vector<std::size_t> resultIndexes;
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

bool parentPhaseValid(const BenchmarkResult &result) {
    return result.status.find("parent-mismatch") == std::string::npos;
}

bool sortPhaseValid(const BenchmarkResult &result) {
    return result.status.find("sort-mismatch") == std::string::npos &&
           result.status.find("verify-failed") == std::string::npos;
}

bool pipelineValid(const BenchmarkResult &result) {
    return parentPhaseValid(result) && sortPhaseValid(result);
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
                context.expectedParent = buildParentIndexForKind(
                    ParentKind::Unordered, context.nodes);
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
    std::vector<ParentJob> parentJobs;
    std::vector<BenchmarkResult> results;

    for (const auto &context : contexts) {
        for (ParentKind parentKind : options.parents) {
            const std::size_t parentJobIndex = parentJobs.size();
            parentJobs.push_back(ParentJob{&context, parentKind, {}, 0.0, {}});
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

                jobs.push_back({parentJobIndex, sortKind, results.size()});
                parentJobs.back().resultIndexes.push_back(results.size());
                results.push_back(std::move(result));
            }
        }
    }

    std::vector<std::size_t> parentOrder =
        makeSequentialIndexOrder(parentJobs.size());
    std::vector<std::size_t> sortOrder = makeSequentialIndexOrder(jobs.size());

    const int totalPasses =
        checkedBenchmarkPassCount(options.warmup, options.iterations);
    for (int passIdx = 0; passIdx < totalPasses; ++passIdx) {
        if (options.shuffle) {
            parentOrder = makeSequentialIndexOrder(parentJobs.size());
            sortOrder = makeSequentialIndexOrder(jobs.size());
            shuffleBenchmarkOrder(parentOrder, options.orderSeed ^ 0x70617265U,
                                  passIdx);
            shuffleBenchmarkOrder(sortOrder, options.orderSeed ^ 0x736f7274U,
                                  passIdx);
        }
        const bool recordSample = passIdx >= options.warmup;
        for (std::size_t parentJobIdx : parentOrder) {
            ParentJob &parentJob = parentJobs[parentJobIdx];
            parentJob.elapsedMs =
                timeParentBuildMs(parentJob.context->nodes, parentJob.parent,
                                  parentJob.artifacts);
            for (std::size_t resultIndex : parentJob.resultIndexes) {
                recordParentSample(results[resultIndex], *parentJob.context,
                                   parentJob.artifacts, parentJob.elapsedMs,
                                   recordSample);
            }
        }
        for (std::size_t jobIdx : sortOrder) {
            const Job &job = jobs[jobIdx];
            const ParentJob &parentJob = parentJobs[job.parentJobIndex];
            recordSortSample(results[job.resultIndex], *parentJob.context,
                             parentJob.artifacts, job.sort, parentJob.elapsedMs,
                             recordSample);
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
                result.sortComparisonStatus =
                    sortPhaseValid(result) ? "baseline" : "invalid-result";
                continue;
            }
            if (!sortPhaseValid(result)) {
                result.sortComparisonStatus = "invalid-result";
                continue;
            }
            BenchmarkResult *baseline =
                findSortBaseline(results, result, baselineSort);
            if (baseline == nullptr) {
                result.sortComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "sort-baseline-missing");
            } else if (comparisonEligibility(sortPhaseValid(result),
                                             sortPhaseValid(*baseline)) ==
                       ComparisonEligibility::InvalidBaseline) {
                result.sortComparisonStatus = "baseline-failed";
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
                result.parentComparisonStatus =
                    parentPhaseValid(result) ? "baseline" : "invalid-result";
                continue;
            }
            if (!parentPhaseValid(result)) {
                result.parentComparisonStatus = "invalid-result";
                continue;
            }
            BenchmarkResult *baseline =
                findParentBaseline(results, result, baselineParent);
            if (baseline == nullptr) {
                result.parentComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "parent-baseline-missing");
            } else if (comparisonEligibility(parentPhaseValid(result),
                                             parentPhaseValid(*baseline)) ==
                       ComparisonEligibility::InvalidBaseline) {
                result.parentComparisonStatus = "baseline-failed";
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
                result.pipelineComparisonStatus =
                    pipelineValid(result) ? "baseline" : "invalid-result";
                continue;
            }
            if (!pipelineValid(result)) {
                result.pipelineComparisonStatus = "invalid-result";
                continue;
            }
            BenchmarkResult *baseline = findPipelineBaseline(
                results, result, baselineParent, baselineSort);
            if (baseline == nullptr) {
                result.pipelineComparisonStatus = "missing-baseline";
                result.status =
                    appendStatus(result.status, "pipeline-baseline-missing");
            } else if (comparisonEligibility(pipelineValid(result),
                                             pipelineValid(*baseline)) ==
                       ComparisonEligibility::InvalidBaseline) {
                result.pipelineComparisonStatus = "baseline-failed";
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

int runForestBenchmark(int argc, char **argv) {
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
