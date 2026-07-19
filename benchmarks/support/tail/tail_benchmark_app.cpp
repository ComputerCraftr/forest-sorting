#include "forest_sorting/benchmark_support/tail/tail_benchmark_app.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_cli.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_execution.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_output.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_stats.hpp"
#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/tail/tail_benchmark_output.hpp"
#include "forest_sorting/benchmark_support/tail/tail_corpus.hpp"
#include "forest_sorting/benchmark_support/tail/tail_execution.hpp"
#include "forest_sorting/benchmark_support/tail/tail_sort_variants.hpp"

#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numeric>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace forest_sorting::benchmark_support {

namespace detail = forest_sorting::detail;

namespace {

struct Options {
    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> tailSizes = {4, 8, 16, 24, 32};
    std::vector<Pattern> patterns =
        std::vector<Pattern>(kAllPatterns.begin(), kAllPatterns.end());
    std::vector<Workload> workloads = {Workload::Synthetic};
    std::vector<DatasetKind> datasets = std::vector<DatasetKind>(
        kCapturedDatasets.begin(), kCapturedDatasets.end());
    std::vector<std::string> algorithms = {"linear",
                                           "binary",
                                           "exponential",
                                           "branchless-bitwise",
                                           "shell-gap-10-4-1",
                                           "shell-gap-3-2-1",
                                           "shell-gap-16-7-3-1"};
    int iterations = 100;
    int warmup = 10;
    std::size_t tailCountLimit = 1000;
    std::size_t sourceSize = 100000;
    uint32_t dataSeed = kDefaultBenchmarkDataSeed;
    uint32_t orderSeed = 0x5eedU;
    std::string baselineAlgorithm = "linear";
    bool shuffle = false;
    bool help = false;
};

struct MicroResult {
    std::string workload;
    std::string pattern;
    std::optional<std::size_t> sourceSize;
    std::optional<std::size_t> tailSize;
    std::size_t minTailSize = 0;
    std::size_t maxTailSize = 0;
    std::size_t tailCount = 0;
    std::string algorithm;
    std::vector<double> samples;
    SampleStats stats;
    double deltaMedianPct = 0.0;
    ConfidenceInterval deltaPctCi95;
    bool hasDelta = false;
    std::string winner = "none";
    std::string status = "ok";
};

using MeasureFunction = double (*)(const TailCorpus &);
using VerifyFunction = void (*)(const TailCorpus &);

template <typename Sorter> void verifySorterOnCorpus(const TailCorpus &corpus);
template <typename Sorter> double timeAlgorithm(const TailCorpus &corpus);

struct Algorithm {
    std::string_view name;
    MeasureFunction measure;
    VerifyFunction verify;
};

std::vector<Algorithm> allAlgorithms() {
    return {
        {"linear", timeAlgorithm<LinearSmallSorter<32>>,
         verifySorterOnCorpus<LinearSmallSorter<32>>},
        {"binary", timeAlgorithm<BinarySmallSorter<32>>,
         verifySorterOnCorpus<BinarySmallSorter<32>>},
        {"exponential", timeAlgorithm<ExponentialSmallSorter<32>>,
         verifySorterOnCorpus<ExponentialSmallSorter<32>>},
        {"branchless-bitwise", timeAlgorithm<BranchlessBitwiseSmallSorter<32>>,
         verifySorterOnCorpus<BranchlessBitwiseSmallSorter<32>>},
        {"shell-gap-10-4-1", timeAlgorithm<ShellGap10_4_1SmallSorter>,
         verifySorterOnCorpus<ShellGap10_4_1SmallSorter>},
        {"shell-gap-3-2-1", timeAlgorithm<ShellGap3_2_1SmallSorter>,
         verifySorterOnCorpus<ShellGap3_2_1SmallSorter>},
        {"shell-gap-16-7-3-1", timeAlgorithm<ShellGap16_7_3_1SmallSorter>,
         verifySorterOnCorpus<ShellGap16_7_3_1SmallSorter>},
    };
}

Pattern parsePattern(std::string_view value) {
    for (Pattern pattern : kAllPatterns) {
        if (value == patternName(pattern)) {
            return pattern;
        }
    }
    throw std::runtime_error("unknown pattern: " + std::string(value));
}

DatasetKind parseCapturedDataset(std::string_view value) {
    for (DatasetKind dataset : kCapturedDatasets) {
        if (value == datasetName(dataset)) {
            return dataset;
        }
    }
    throw std::runtime_error("unknown captured dataset: " + std::string(value));
}

Workload parseWorkload(std::string_view value) {
    for (Workload workload : kAllWorkloads) {
        if (value == workloadName(workload)) {
            return workload;
        }
    }
    throw std::runtime_error("unknown workload: " + std::string(value));
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customTailSizes = false;
    bool customPatterns = false;
    bool customWorkloads = false;
    bool customDatasets = false;
    bool customAlgorithms = false;

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
        } else if (option == "--tail-size") {
            if (!customTailSizes) {
                options.tailSizes.clear();
                customTailSizes = true;
            }
            appendUniqueSelection(options.tailSizes, parsePositiveSizeOption(
                                                         value, "--tail-size"));
        } else if (option == "--source-size") {
            options.sourceSize =
                parsePositiveSizeOption(value, "--source-size");
        } else if (option == "--pattern") {
            const std::vector<Pattern> allPatterns(kAllPatterns.begin(),
                                                   kAllPatterns.end());
            applyRegistrySelection(options.patterns, customPatterns, value,
                                   allPatterns, allPatterns, parsePattern);
        } else if (option == "--dataset") {
            const std::vector<DatasetKind> allDatasets(
                kCapturedDatasets.begin(), kCapturedDatasets.end());
            applyRegistrySelection(options.datasets, customDatasets, value,
                                   allDatasets, allDatasets,
                                   parseCapturedDataset);
        } else if (option == "--workload") {
            const std::vector<Workload> defaults = {Workload::Synthetic};
            const std::vector<Workload> allWorkloads(kAllWorkloads.begin(),
                                                     kAllWorkloads.end());
            applyRegistrySelection(options.workloads, customWorkloads, value,
                                   defaults, allWorkloads, parseWorkload);
        } else if (option == "--algorithm") {
            std::vector<std::string> registered;
            for (const Algorithm &algorithm : allAlgorithms()) {
                registered.emplace_back(algorithm.name);
            }
            applyRegistrySelection(
                options.algorithms, customAlgorithms, value, registered,
                registered, [&](std::string_view name) {
                    const auto found =
                        std::find_if(registered.begin(), registered.end(),
                                     [&](const std::string &entry) {
                                         return entry == name;
                                     });
                    if (found == registered.end()) {
                        throw std::runtime_error("unknown algorithm: " +
                                                 std::string(name));
                    }
                    return *found;
                });
        } else if (option == "--iterations") {
            options.iterations = parsePositiveIntOption(value, "--iterations");
        } else if (option == "--warmup") {
            options.warmup = parseNonNegativeIntOption(value, "--warmup");
        } else if (option == "--tail-count") {
            options.tailCountLimit =
                parsePositiveSizeOption(value, "--tail-count");
        } else if (option == "--data-seed") {
            options.dataSeed = parseSeedOption(value, "--data-seed");
        } else if (option == "--order-seed") {
            options.orderSeed = parseSeedOption(value, "--order-seed");
        } else if (option == "--baseline-algorithm") {
            options.baselineAlgorithm = std::string(value);
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    for (std::size_t size : options.tailSizes) {
        if (size == 0 || size > detail::small_id_range_sort_threshold) {
            throw std::runtime_error("tail size must be between 1 and 32");
        }
    }
    const std::vector<Algorithm> available = allAlgorithms();
    const auto baseline = std::find_if(
        available.begin(), available.end(), [&](const Algorithm &algorithm) {
            return algorithm.name == options.baselineAlgorithm;
        });
    if (baseline == available.end()) {
        throw std::runtime_error("unknown baseline algorithm: " +
                                 options.baselineAlgorithm);
    }
    appendMissingBaseline(options.algorithms, options.baselineAlgorithm);
    (void)checkedBenchmarkPassCount(options.warmup, options.iterations);
    return options;
}

void printHelp() {
    std::cout << "usage: forest-sorting-tail-bench [options]\n\n"
              << "  --format table|csv|tsv|json\n"
              << "  --workload ";
    bool first = true;
    for (Workload workload : kAllWorkloads) {
        std::cout << (first ? "" : "|") << workloadName(workload);
        first = false;
    }
    std::cout
        << "|default|all\n"
        << "  --tail-size N            synthetic tail size, repeatable "
           "(default: 4,8,16,24,32)\n"
        << "  --source-size N          captured source size (default: 100000)\n"
        << "  --pattern ";
    first = true;
    for (Pattern pattern : kAllPatterns) {
        std::cout << (first ? "" : "|") << patternName(pattern);
        first = false;
    }
    std::cout << "|default|all\n" << "  --dataset ";
    first = true;
    for (DatasetKind dataset : kCapturedDatasets) {
        std::cout << (first ? "" : "|") << datasetName(dataset);
        first = false;
    }
    std::cout << "|default|all\n" << "  --algorithm ";
    first = true;
    for (const Algorithm &algorithm : allAlgorithms()) {
        std::cout << (first ? "" : "|") << algorithm.name;
        first = false;
    }
    std::cout
        << "|default|all\n"
        << "  --iterations N           (default: 100)\n"
        << "  --warmup N               (default: 10)\n"
        << "  --tail-count N           generated/captured tail cap "
           "(default: 1000)\n"
        << "  --data-seed N            corpus seed (default: 0x5eed1234)\n"
        << "  --order-seed N           shuffled schedule seed (default: "
           "0x5eed)\n"
        << "  --shuffle                shuffle algorithm order per sample\n"
        << "  --baseline-algorithm NAME (default: linear)\n"
        << "  --help\n";
}

std::vector<Algorithm> selectAlgorithms(const Options &options) {
    const std::vector<Algorithm> available = allAlgorithms();
    std::vector<Algorithm> selected;
    for (const std::string &name : options.algorithms) {
        const auto found = std::find_if(
            available.begin(), available.end(),
            [&](const Algorithm &algo) { return algo.name == name; });
        if (found == available.end()) {
            throw std::runtime_error("unknown algorithm: " + name);
        }
        selected.push_back(*found);
    }
    return selected;
}

std::vector<TailCorpusDescriptor> initializeCorpusDescriptors(
    const Options &options, const std::vector<Algorithm> &algorithms,
    std::size_t baselineAlgorithmIndex, std::vector<MicroResult> &results) {
    std::vector<TailCorpusDescriptor> descriptors;
    auto appendDescriptor = [&](TailCorpusDescriptor descriptor) {
        descriptor.resultBegin = results.size();
        descriptor.resultCount = algorithms.size();
        descriptor.baselineResultIndex =
            descriptor.resultBegin + baselineAlgorithmIndex;
        descriptors.push_back(descriptor);

        for (const Algorithm &algorithm : algorithms) {
            MicroResult result;
            result.workload = std::string(workloadName(descriptor.workload));
            if (descriptor.workload == Workload::Synthetic) {
                result.pattern = std::string(patternName(descriptor.pattern));
                result.tailSize = descriptor.itemCount;
            } else {
                result.pattern = std::string(datasetName(descriptor.dataset));
                result.sourceSize = descriptor.itemCount;
            }
            result.algorithm = algorithm.name;
            result.samples.resize(static_cast<std::size_t>(options.iterations));
            results.push_back(std::move(result));
        }
    };

    for (Workload workload : options.workloads) {
        if (workload == Workload::Synthetic) {
            for (Pattern pattern : options.patterns) {
                for (std::size_t size : options.tailSizes) {
                    appendDescriptor(TailCorpusDescriptor{
                        workload, pattern, DatasetKind::Random, size});
                }
            }
            continue;
        }
        for (DatasetKind dataset : options.datasets) {
            appendDescriptor(TailCorpusDescriptor{workload, Pattern::Random,
                                                  dataset, options.sourceSize});
        }
    }
    return descriptors;
}

TailCorpus materializeCorpus(const TailCorpusDescriptor &descriptor,
                             const Options &options) {
    if (descriptor.workload == Workload::Synthetic) {
        return makeSyntheticCorpus(
            descriptor.pattern, descriptor.itemCount, options.tailCountLimit,
            tailSyntheticGenerationSeed(descriptor, options.dataSeed));
    }
    if (descriptor.workload == Workload::CapturedNodeIds) {
        return makeCapturedNodeIdTailCorpus(
            descriptor.dataset, descriptor.itemCount, options.tailCountLimit,
            options.dataSeed,
            tailNodeReservoirSeed(descriptor, options.dataSeed));
    }
    return makeCapturedParentQueryTailCorpus(
        descriptor.dataset, descriptor.itemCount, options.tailCountLimit,
        options.dataSeed,
        tailParentReservoirSeed(descriptor, options.dataSeed));
}

template <typename Sorter> void verifySorterOnCorpus(const TailCorpus &corpus) {
    std::vector<std::size_t> order(corpus.nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::vector<std::size_t> expected = order;
    const UInt128NodeTraits traits;
    for (TailRange range : corpus.ranges) {
        std::stable_sort(
            expected.begin() + static_cast<std::ptrdiff_t>(range.begin),
            expected.begin() + static_cast<std::ptrdiff_t>(range.end),
            [&](std::size_t lhs, std::size_t rhs) {
                return detail::idLess(corpus.nodes[lhs].id,
                                      corpus.nodes[rhs].id, traits);
            });
        Sorter{}(order, corpus.nodes, traits, range.begin, range.end);
        if (!std::equal(
                order.begin() + static_cast<std::ptrdiff_t>(range.begin),
                order.begin() + static_cast<std::ptrdiff_t>(range.end),
                expected.begin() + static_cast<std::ptrdiff_t>(range.begin))) {
            throw std::runtime_error(
                "sorter failed exact stable ID-order oracle");
        }
    }
    std::sort(order.begin(), order.end());
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (order[index] != index) {
            throw std::runtime_error("sorter did not preserve permutation");
        }
    }
}

template <typename Sorter> double timeAlgorithm(const TailCorpus &corpus) {
    if (corpus.ranges.empty()) {
        throw std::runtime_error("cannot time an empty tail corpus");
    }
    std::vector<std::size_t> order(corpus.nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const UInt128NodeTraits traits;
    const auto start = std::chrono::high_resolution_clock::now();
    for (TailRange range : corpus.ranges) {
        Sorter{}(order, corpus.nodes, traits, range.begin, range.end);
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() /
           static_cast<double>(corpus.ranges.size());
}

void applyCorpusDeltas(const TailCorpusDescriptor &descriptor,
                       std::vector<MicroResult> &results) {
    MicroResult &baseline = results[descriptor.baselineResultIndex];
    for (std::size_t resultIndex = descriptor.resultBegin;
         resultIndex < descriptor.resultBegin + descriptor.resultCount;
         ++resultIndex) {
        MicroResult &result = results[resultIndex];
        if (resultIndex == descriptor.baselineResultIndex ||
            result.status != "ok" || baseline.status != "ok") {
            continue;
        }
        result.deltaMedianPct = medianOfSamples(
            pairedRelativeDeltas(result.samples, baseline.samples));
        result.deltaPctCi95 =
            bootstrapPairedRelativeDeltaCi95(result.samples, baseline.samples);
        result.hasDelta = true;
        baseline.hasDelta = true;
        result.winner = std::string(classifyBenchmarkWinner(
            result.deltaMedianPct, result.deltaPctCi95));
    }
}

void runCorpus(const TailCorpusDescriptor &descriptor, const TailCorpus &corpus,
               const std::vector<Algorithm> &algorithms, const Options &options,
               std::vector<MicroResult> &results) {
    for (std::size_t algorithmIndex = 0; algorithmIndex < algorithms.size();
         ++algorithmIndex) {
        MicroResult &result = results[descriptor.resultBegin + algorithmIndex];
        result.minTailSize = corpus.minTailSize;
        result.maxTailSize = corpus.maxTailSize;
        result.tailCount = corpus.ranges.size();
        if (corpus.ranges.empty()) {
            result.status = "empty";
            result.samples.clear();
            continue;
        }
        try {
            algorithms[algorithmIndex].verify(corpus);
        } catch (const std::exception &exception) {
            result.status = exception.what();
            result.samples.clear();
        }
    }

    if (!corpus.ranges.empty()) {
        std::vector<std::size_t> schedule(algorithms.size());
        std::iota(schedule.begin(), schedule.end(), std::size_t{0});
        const int totalPasses =
            checkedBenchmarkPassCount(options.warmup, options.iterations);
        const uint32_t scheduleSeed =
            tailAlgorithmScheduleSeed(descriptor, options.orderSeed);
        for (int pass = 0; pass < totalPasses; ++pass) {
            if (options.shuffle) {
                std::iota(schedule.begin(), schedule.end(), std::size_t{0});
                shuffleBenchmarkOrder(schedule, scheduleSeed, pass);
            }
            for (std::size_t algorithmIdx : schedule) {
                MicroResult &result =
                    results[descriptor.resultBegin + algorithmIdx];
                if (result.status != "ok") {
                    continue;
                }
                const double elapsed = algorithms[algorithmIdx].measure(corpus);
                if (pass >= options.warmup) {
                    result.samples[static_cast<std::size_t>(
                        pass - options.warmup)] = elapsed;
                }
            }
        }
    }
    for (std::size_t algorithmIdx = 0; algorithmIdx < algorithms.size();
         ++algorithmIdx) {
        MicroResult &result = results[descriptor.resultBegin + algorithmIdx];
        if (result.status == "ok") {
            result.stats = computeSampleStats(result.samples);
        }
    }
    applyCorpusDeltas(descriptor, results);
}

std::vector<MicroResult> runMicrobenchmarks(const Options &options) {
    const std::vector<Algorithm> algorithms = selectAlgorithms(options);
    const auto baseline = std::find_if(
        algorithms.begin(), algorithms.end(), [&](const Algorithm &algorithm) {
            return algorithm.name == options.baselineAlgorithm;
        });
    if (baseline == algorithms.end()) {
        throw std::logic_error(
            "baseline algorithm is missing from tail execution");
    }
    const std::size_t baselineAlgorithmIndex =
        static_cast<std::size_t>(baseline - algorithms.begin());

    std::vector<MicroResult> results;
    const std::vector<TailCorpusDescriptor> descriptors =
        initializeCorpusDescriptors(options, algorithms, baselineAlgorithmIndex,
                                    results);
    std::vector<std::size_t> executionOrder =
        makeSequentialIndexOrder(descriptors.size());
    if (options.shuffle) {
        shuffleBenchmarkOrder(executionOrder,
                              tailCorpusOrderSeed(options.orderSeed), 0);
    }
    forEachMaterializedContext(
        descriptors, executionOrder,
        [&](const TailCorpusDescriptor &descriptor) {
            return materializeCorpus(descriptor, options);
        },
        [&](const TailCorpusDescriptor &descriptor, const TailCorpus &corpus) {
            runCorpus(descriptor, corpus, algorithms, options, results);
        });
    return results;
}

std::vector<MicroOutputRow>
makeMicroOutputRows(const std::vector<MicroResult> &results,
                    std::string_view baselineAlgorithm) {
    std::vector<MicroOutputRow> rows;
    rows.reserve(results.size());
    for (const MicroResult &result : results) {
        rows.push_back(makeMicroOutputRow(result, baselineAlgorithm));
    }
    return rows;
}

void printJson(const std::vector<MicroOutputRow> &rows,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"warmup\": " << options.warmup
              << ",\n  \"tail_count_limit\": " << options.tailCountLimit
              << ",\n  \"source_size\": " << options.sourceSize
              << ",\n  \"data_seed\": " << options.dataSeed
              << ",\n  \"order_seed\": " << options.orderSeed
              << ",\n  \"shuffle\": " << (options.shuffle ? "true" : "false")
              << ",\n  \"baseline_algorithm\": \""
              << jsonEscape(options.baselineAlgorithm) << "\""
              << ",\n  \"results\": ";
    printMicroJsonRows(std::cout, rows);
    std::cout << "\n}\n";
}

} // namespace

} // namespace forest_sorting::benchmark_support

namespace forest_sorting::benchmark_app::tail {

int runTailBenchmark(int argc, char **argv) {
    try {
        const auto options = benchmark_support::parseOptions(argc, argv);
        if (options.help) {
            benchmark_support::printHelp();
            return 0;
        }

        const auto results = benchmark_support::runMicrobenchmarks(options);
        const auto rows = benchmark_support::makeMicroOutputRows(
            results, options.baselineAlgorithm);

        switch (options.format) {
        case benchmark_support::OutputFormat::Table:
            benchmark_support::printMicroTable(std::cout, rows);
            break;
        case benchmark_support::OutputFormat::Csv:
            benchmark_support::printMicroDelimited(std::cout, rows, ',');
            break;
        case benchmark_support::OutputFormat::Tsv:
            benchmark_support::printMicroDelimited(std::cout, rows, '\t');
            break;
        case benchmark_support::OutputFormat::Json:
            benchmark_support::printJson(rows, options);
            break;
        }
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "forest-sorting-tail-bench failed: " << exception.what()
                  << '\n';
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tail-bench failed: unknown exception\n";
        return 1;
    }
}

} // namespace forest_sorting::benchmark_app::tail
