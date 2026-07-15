#include "tail/tail_benchmark_app.hpp"
#include "common/benchmark_cli.hpp"
#include "common/benchmark_output.hpp"
#include "common/benchmark_stats.hpp"
#include "full/adaptive_sort_variants.hpp"
#include "tail/tail_benchmark_output.hpp"
#include "tail/tail_corpus.hpp"
#include "tail/tail_sort_variants.hpp"

#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "uint128_fixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <ratio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using forest_sorting::makeId;
using forest_sorting::Node;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using namespace forest_sorting::test_support;
namespace detail = forest_sorting::detail;

enum class Workload : uint8_t {
    Synthetic,
    CapturedNodeIds,
    CapturedParentQueries,
};

enum class Pattern : uint8_t {
    AlreadySorted,
    ReverseSorted,
    Random,
    NearlySorted,
    SameHigh32,
    SameHigh64,
    LongCommonPrefix,
    FirstByteDiffers,
    LastByteDiffers,
};

inline std::string_view patternName(Pattern pattern) {
    switch (pattern) {
    case Pattern::AlreadySorted:
        return "already sorted";
    case Pattern::ReverseSorted:
        return "reverse sorted";
    case Pattern::Random:
        return "random";
    case Pattern::NearlySorted:
        return "nearly sorted";
    case Pattern::SameHigh32:
        return "same-high32";
    case Pattern::SameHigh64:
        return "same-high64";
    case Pattern::LongCommonPrefix:
        return "long common prefix";
    case Pattern::FirstByteDiffers:
        return "first-byte differs";
    case Pattern::LastByteDiffers:
        return "last-byte differs";
    }
    return "unknown";
}

inline constexpr std::array kAllPatterns = {
    Pattern::AlreadySorted,    Pattern::ReverseSorted,
    Pattern::Random,           Pattern::NearlySorted,
    Pattern::SameHigh32,       Pattern::SameHigh64,
    Pattern::LongCommonPrefix, Pattern::FirstByteDiffers,
    Pattern::LastByteDiffers,
};

inline constexpr std::array kCapturedDatasets = {
    DatasetKind::Random,
    DatasetKind::SameHigh32,
    DatasetKind::SameHigh64,
    DatasetKind::Outliers,
};

struct Options {
    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> sizes = {4, 8, 16, 24, 32};
    std::vector<Pattern> patterns =
        std::vector<Pattern>(kAllPatterns.begin(), kAllPatterns.end());
    std::vector<Workload> workloads = {Workload::Synthetic};
    std::vector<DatasetKind> datasets = std::vector<DatasetKind>(
        kCapturedDatasets.begin(), kCapturedDatasets.end());
    std::vector<std::string> sorts = {"linear",
                                      "binary",
                                      "exponential",
                                      "branchless-bitwise",
                                      "shell-gap-10-4-1",
                                      "shell-gap-3-2-1",
                                      "shell-gap-16-7-3-1"};
    int iterations = 100;
    int warmup = 10;
    std::size_t numRanges = 1000;
    std::size_t sourceSize = 100000;
    uint32_t dataSeed = kDefaultBenchmarkDataSeed;
    uint32_t orderSeed = 0x5eedU;
    std::string baselineSort = "linear";
    bool shuffle = false;
    bool help = false;
};

struct MicroResult {
    std::string workload;
    std::string pattern;
    std::optional<std::size_t> sourceSize;
    std::optional<std::size_t> rangeSize;
    std::size_t minTailSize = 0;
    std::size_t maxTailSize = 0;
    std::size_t rangeCount = 0;
    std::string algorithm;
    std::vector<double> samples;
    SampleStats stats;
    double deltaMedianPct = 0.0;
    ConfidenceInterval deltaPctCi95;
    std::string winner = "none";
    std::string status = "ok";
};

using TailSortFunction =
    std::function<void(std::vector<std::size_t> &, const std::vector<Node> &,
                       const UInt128NodeTraits &, std::size_t, std::size_t)>;

struct Algorithm {
    std::string name;
    TailSortFunction sort;
};

std::vector<Algorithm> allAlgorithms() {
    return {
        {"linear", LinearSmallSorterDynamic{}},
        {"binary", BinarySmallSorterDynamic{}},
        {"exponential", ExponentialSmallSorterDynamic{}},
        {"branchless-bitwise", BranchlessBitwiseSmallSorterDynamic{}},
        {"shell-gap-10-4-1", ShellGap10_4_1SmallSorterDynamic{}},
        {"shell-gap-3-2-1", ShellGap3_2_1SmallSorterDynamic{}},
        {"shell-gap-16-7-3-1", ShellGap16_7_3_1SmallSorterDynamic{}},
    };
}

void makeIdsUnique(std::vector<UInt128> &ids, uint64_t highBase,
                   uint64_t lowBase) {
    for (std::size_t idIdx = 0; idIdx < ids.size(); ++idIdx) {
        ids[idIdx] = makeId(highBase, lowBase + idIdx + 1);
    }
}

TailCorpus makeSyntheticCorpus(Pattern pattern, std::size_t rangeSize,
                               std::size_t numRanges, uint32_t dataSeed) {
    TailCorpus corpus{"synthetic", std::string(patternName(pattern)), 0};
    const std::size_t totalNodeCount =
        checkedSizeProduct(rangeSize, numRanges, "synthetic tail corpus");
    corpus.nodes.reserve(totalNodeCount);
    corpus.ranges.reserve(numRanges);
    std::mt19937_64 rng(mixFixtureSeed(dataSeed, 0x7461696c2d73796eULL));

    for (std::size_t rangeIdx = 0; rangeIdx < numRanges; ++rangeIdx) {
        std::vector<UInt128> ids(rangeSize);
        const std::size_t firstGlobalIndex = rangeIdx * rangeSize;
        switch (pattern) {
        case Pattern::AlreadySorted:
        case Pattern::ReverseSorted:
        case Pattern::NearlySorted:
            makeIdsUnique(ids, 0, firstGlobalIndex);
            break;
        case Pattern::Random: {
            const uint64_t randomSeed =
                mixFixtureSeed(dataSeed, 0x72616e646f6dULL);
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                ids[idIdx] = makeRandomId(randomSeed, firstGlobalIndex + idIdx);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }
        case Pattern::SameHigh32: {
            constexpr uint64_t sharedHigh32 = 0x12345678ULL;
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                const uint64_t high =
                    (sharedHigh32 << 32U) |
                    static_cast<uint64_t>(firstGlobalIndex + idIdx + 1);
                ids[idIdx] = makeId(high, mixDeterministicUInt128Word(
                                              firstGlobalIndex + idIdx));
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }
        case Pattern::SameHigh64:
            makeIdsUnique(ids, 0x123456789abcdef0ULL, firstGlobalIndex);
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::LongCommonPrefix:
            makeIdsUnique(ids, 0x123456789abcdef0ULL,
                          0xfedcba9876540000ULL + firstGlobalIndex);
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::FirstByteDiffers:
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                const uint64_t high =
                    (static_cast<uint64_t>(idIdx + 1) << 56U) |
                    static_cast<uint64_t>(rangeIdx + 1);
                ids[idIdx] = makeId(high, 1);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        case Pattern::LastByteDiffers:
            for (std::size_t idIdx = 0; idIdx < rangeSize; ++idIdx) {
                ids[idIdx] = makeId(0x123456789abcdef0ULL ^
                                        static_cast<uint64_t>(rangeIdx),
                                    0xfedcba9876540000ULL + idIdx + 1);
            }
            std::shuffle(ids.begin(), ids.end(), rng);
            break;
        }

        if (pattern == Pattern::ReverseSorted) {
            std::reverse(ids.begin(), ids.end());
        } else if (pattern == Pattern::NearlySorted && rangeSize > 1) {
            const std::size_t swapCount =
                std::max<std::size_t>(1, rangeSize / 10);
            for (std::size_t swapIdx = 0; swapIdx < swapCount; ++swapIdx) {
                const std::size_t index =
                    static_cast<std::size_t>(rng() % (rangeSize - 1));
                std::swap(ids[index], ids[index + 1]);
            }
        }
        appendTail(corpus, ids);
    }

    std::vector<UInt128> allIds;
    allIds.reserve(corpus.nodes.size());
    for (const Node &node : corpus.nodes) {
        allIds.push_back(node.id);
    }
    std::sort(allIds.begin(), allIds.end());
    if (std::adjacent_find(allIds.begin(), allIds.end()) != allIds.end()) {
        throw std::runtime_error(
            "synthetic tail corpus contains duplicate IDs");
    }
    return corpus;
}

Pattern parsePattern(std::string_view value) {
    for (Pattern pattern : kAllPatterns) {
        if (value == patternName(pattern)) {
            return pattern;
        }
    }
    if (value == "already-sorted") {
        return Pattern::AlreadySorted;
    }
    if (value == "reverse-sorted") {
        return Pattern::ReverseSorted;
    }
    if (value == "nearly-sorted") {
        return Pattern::NearlySorted;
    }
    if (value == "long-common-prefix") {
        return Pattern::LongCommonPrefix;
    }
    if (value == "first-byte-differs") {
        return Pattern::FirstByteDiffers;
    }
    if (value == "last-byte-differs") {
        return Pattern::LastByteDiffers;
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

void appendWorkload(std::vector<Workload> &workloads, std::string_view value) {
    if (value == "synthetic") {
        appendUniqueSelection(workloads, Workload::Synthetic);
    } else if (value == "captured-node-ids") {
        appendUniqueSelection(workloads, Workload::CapturedNodeIds);
    } else if (value == "captured-parent-queries") {
        appendUniqueSelection(workloads, Workload::CapturedParentQueries);
    } else if (value == "all") {
        workloads = {Workload::Synthetic, Workload::CapturedNodeIds,
                     Workload::CapturedParentQueries};
    } else {
        throw std::runtime_error("unknown workload: " + std::string(value));
    }
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customSizes = false;
    bool customPatterns = false;
    bool customWorkloads = false;
    bool customDatasets = false;
    bool customSorts = false;

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
            appendUniqueSelection(options.sizes,
                                  parsePositiveSizeOption(value, "--size"));
        } else if (option == "--source-size") {
            options.sourceSize =
                parsePositiveSizeOption(value, "--source-size");
        } else if (option == "--pattern") {
            if (!customPatterns) {
                options.patterns.clear();
                customPatterns = true;
            }
            if (value == "all") {
                options.patterns.assign(kAllPatterns.begin(),
                                        kAllPatterns.end());
            } else {
                appendUniqueSelection(options.patterns, parsePattern(value));
            }
        } else if (option == "--dataset") {
            if (!customDatasets) {
                options.datasets.clear();
                customDatasets = true;
            }
            if (value == "all") {
                options.datasets.assign(kCapturedDatasets.begin(),
                                        kCapturedDatasets.end());
            } else {
                appendUniqueSelection(options.datasets,
                                      parseCapturedDataset(value));
            }
        } else if (option == "--workload") {
            if (!customWorkloads) {
                options.workloads.clear();
                customWorkloads = true;
            }
            appendWorkload(options.workloads, value);
        } else if (option == "--sort") {
            if (!customSorts) {
                options.sorts.clear();
                customSorts = true;
            }
            if (value == "all") {
                options.sorts.clear();
                for (const Algorithm &algorithm : allAlgorithms()) {
                    options.sorts.push_back(algorithm.name);
                }
            } else {
                appendUniqueSelection(options.sorts, std::string(value));
            }
        } else if (option == "--iterations") {
            options.iterations = parsePositiveIntOption(value, "--iterations");
        } else if (option == "--warmup") {
            options.warmup = parseNonNegativeIntOption(value, "--warmup");
        } else if (option == "--ranges") {
            options.numRanges = parsePositiveSizeOption(value, "--ranges");
        } else if (option == "--seed" || option == "--data-seed") {
            options.dataSeed = parseSeedOption(value, option);
        } else if (option == "--order-seed") {
            options.orderSeed = parseSeedOption(value, "--order-seed");
        } else if (option == "--baseline-sort") {
            options.baselineSort = std::string(value);
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    for (std::size_t size : options.sizes) {
        if (size == 0 || size > detail::small_id_range_sort_threshold) {
            throw std::runtime_error("range size must be between 1 and 32");
        }
    }
    (void)checkedBenchmarkPassCount(options.warmup, options.iterations);
    return options;
}

void printHelp() {
    std::cout
        << "usage: forest-sorting-tail-bench [options]\n\n"
        << "  --format table|csv|tsv|json\n"
        << "  --workload synthetic|captured-node-ids|"
           "captured-parent-queries|all\n"
        << "  --size N                 synthetic tail size, repeatable "
           "(default: 4,8,16,24,32)\n"
        << "  --source-size N          captured source size (default: 100000)\n"
        << "  --pattern PATTERN        synthetic pattern, repeatable\n"
        << "  --dataset DATASET        captured dataset: random|same-high32|"
           "same-high64|outliers|all\n"
        << "  --sort NAME              repeatable; includes linear, binary, "
           "exponential,\n"
        << "                           branchless-bitwise, shell-gap-10-4-1, "
           "shell-gap-3-2-1,\n"
        << "                           shell-gap-16-7-3-1, or all\n"
        << "  --iterations N           (default: 100)\n"
        << "  --warmup N               (default: 10)\n"
        << "  --ranges N               generated/captured range cap "
           "(default: 1000)\n"
        << "  --data-seed N            corpus seed (default: 0x5eed1234)\n"
        << "  --seed N                 compatibility alias for --data-seed\n"
        << "  --order-seed N           shuffled schedule seed (default: "
           "0x5eed)\n"
        << "  --shuffle                shuffle algorithm order per sample\n"
        << "  --baseline-sort NAME     (default: linear)\n"
        << "  --help\n";
}

std::vector<Algorithm> selectAlgorithms(const Options &options) {
    const std::vector<Algorithm> available = allAlgorithms();
    std::vector<Algorithm> selected;
    for (const std::string &name : options.sorts) {
        const auto found = std::find_if(
            available.begin(), available.end(),
            [&](const Algorithm &algo) { return algo.name == name; });
        if (found == available.end()) {
            throw std::runtime_error("unknown sort algorithm: " + name);
        }
        selected.push_back(*found);
    }
    const bool hasBaseline =
        std::any_of(selected.begin(), selected.end(), [&](const auto &algo) {
            return algo.name == options.baselineSort;
        });
    if (!hasBaseline) {
        throw std::runtime_error("baseline sort '" + options.baselineSort +
                                 "' is not selected");
    }
    if (!hasSelectionOtherThan(options.sorts, options.baselineSort)) {
        throw std::runtime_error(
            "tail baseline-sort has no alternate sort to compare");
    }
    return selected;
}

std::vector<TailCorpus> buildCorpora(const Options &options) {
    std::vector<TailCorpus> corpora;
    for (Workload workload : options.workloads) {
        if (workload == Workload::Synthetic) {
            for (Pattern pattern : options.patterns) {
                for (std::size_t size : options.sizes) {
                    corpora.push_back(makeSyntheticCorpus(
                        pattern, size, options.numRanges, options.dataSeed));
                }
            }
            continue;
        }
        for (DatasetKind dataset : options.datasets) {
            if (workload == Workload::CapturedNodeIds) {
                corpora.push_back(makeCapturedNodeIdTailCorpus(
                    dataset, options.sourceSize, options.numRanges,
                    options.dataSeed));
            } else {
                corpora.push_back(makeCapturedParentQueryTailCorpus(
                    dataset, options.sourceSize, options.numRanges,
                    options.dataSeed));
            }
        }
    }
    return corpora;
}

void verifySorterOnCorpus(const Algorithm &algorithm,
                          const TailCorpus &corpus) {
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
        algorithm.sort(order, corpus.nodes, traits, range.begin, range.end);
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

double timeAlgorithm(const Algorithm &algorithm, const TailCorpus &corpus) {
    if (corpus.ranges.empty()) {
        throw std::runtime_error("cannot time an empty tail corpus");
    }
    std::vector<std::size_t> order(corpus.nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const UInt128NodeTraits traits;
    const auto start = std::chrono::high_resolution_clock::now();
    for (TailRange range : corpus.ranges) {
        algorithm.sort(order, corpus.nodes, traits, range.begin, range.end);
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() /
           static_cast<double>(corpus.ranges.size());
}

std::vector<MicroResult> runMicrobenchmarks(const Options &options) {
    const std::vector<Algorithm> algorithms = selectAlgorithms(options);
    const std::vector<TailCorpus> corpora = buildCorpora(options);
    std::vector<MicroResult> results;
    std::mt19937_64 scheduleRng(options.orderSeed);

    for (const TailCorpus &corpus : corpora) {
        const std::size_t resultBegin = results.size();
        for (const Algorithm &algorithm : algorithms) {
            MicroResult result;
            result.workload = corpus.workload;
            result.pattern = corpus.pattern;
            if (corpus.sourceSize != 0) {
                result.sourceSize = corpus.sourceSize;
            } else {
                result.rangeSize = corpus.minTailSize;
            }
            result.minTailSize = corpus.minTailSize;
            result.maxTailSize = corpus.maxTailSize;
            result.rangeCount = corpus.ranges.size();
            result.algorithm = algorithm.name;
            result.samples.resize(static_cast<std::size_t>(options.iterations));
            if (corpus.ranges.empty()) {
                result.status = "no MSD tails captured";
                result.samples.clear();
            } else {
                try {
                    verifySorterOnCorpus(algorithm, corpus);
                } catch (const std::exception &exception) {
                    result.status = exception.what();
                    result.samples.clear();
                }
            }
            results.push_back(std::move(result));
        }

        std::vector<std::size_t> schedule(algorithms.size());
        std::iota(schedule.begin(), schedule.end(), std::size_t{0});
        const int totalPasses =
            checkedBenchmarkPassCount(options.warmup, options.iterations);
        for (int pass = 0; pass < totalPasses; ++pass) {
            if (options.shuffle) {
                std::shuffle(schedule.begin(), schedule.end(), scheduleRng);
            }
            for (std::size_t algorithmIdx : schedule) {
                MicroResult &result = results[resultBegin + algorithmIdx];
                if (result.status != "ok") {
                    continue;
                }
                const double elapsed =
                    timeAlgorithm(algorithms[algorithmIdx], corpus);
                if (pass >= options.warmup) {
                    result.samples[static_cast<std::size_t>(
                        pass - options.warmup)] = elapsed;
                }
            }
        }
        for (std::size_t algorithmIdx = 0; algorithmIdx < algorithms.size();
             ++algorithmIdx) {
            MicroResult &result = results[resultBegin + algorithmIdx];
            if (result.status == "ok") {
                result.stats = computeSampleStats(result.samples);
            }
        }
    }
    return results;
}

bool sameContext(const MicroResult &lhs, const MicroResult &rhs) {
    return lhs.workload == rhs.workload && lhs.pattern == rhs.pattern &&
           lhs.sourceSize == rhs.sourceSize && lhs.rangeSize == rhs.rangeSize;
}

void computeDeltas(std::vector<MicroResult> &results,
                   std::string_view baselineName) {
    for (MicroResult &result : results) {
        if (result.algorithm == baselineName || result.status != "ok") {
            continue;
        }
        const auto baseline =
            std::find_if(results.begin(), results.end(), [&](const auto &row) {
                return row.algorithm == baselineName &&
                       sameContext(row, result);
            });
        if (baseline == results.end() || baseline->status != "ok") {
            continue;
        }
        result.deltaMedianPct = medianOfSamples(
            pairedRelativeDeltas(result.samples, baseline->samples));
        result.deltaPctCi95 =
            bootstrapPairedRelativeDeltaCi95(result.samples, baseline->samples);
        result.winner = std::string(classifyBenchmarkWinner(
            result.deltaMedianPct, result.deltaPctCi95));
    }
}

std::vector<MicroOutputRow>
makeMicroOutputRows(const std::vector<MicroResult> &results,
                    std::string_view baselineSort) {
    std::vector<MicroOutputRow> rows;
    rows.reserve(results.size());
    for (const MicroResult &result : results) {
        rows.push_back(makeMicroOutputRow(result, baselineSort));
    }
    return rows;
}

void printJson(const std::vector<MicroOutputRow> &rows,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"warmup\": " << options.warmup
              << ",\n  \"range_cap\": " << options.numRanges
              << ",\n  \"source_size\": " << options.sourceSize
              << ",\n  \"data_seed\": " << options.dataSeed
              << ",\n  \"order_seed\": " << options.orderSeed
              << ",\n  \"shuffle\": " << (options.shuffle ? "true" : "false")
              << ",\n  \"baseline_sort\": \""
              << jsonEscape(options.baselineSort) << "\""
              << ",\n  \"results\": ";
    printMicroJsonRows(std::cout, rows);
    std::cout << "\n}\n";
}

int runTailBenchmark(int argc, char **argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.help) {
            printHelp();
            return 0;
        }

        auto results = runMicrobenchmarks(options);
        computeDeltas(results, options.baselineSort);
        const auto rows = makeMicroOutputRows(results, options.baselineSort);

        switch (options.format) {
        case OutputFormat::Table:
            printMicroTable(std::cout, rows);
            break;
        case OutputFormat::Csv:
            printMicroDelimited(std::cout, rows, ',');
            break;
        case OutputFormat::Tsv:
            printMicroDelimited(std::cout, rows, '\t');
            break;
        case OutputFormat::Json:
            printJson(rows, options);
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
