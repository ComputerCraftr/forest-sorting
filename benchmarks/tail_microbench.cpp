#include "adaptive_sort_variants.hpp"
#include "benchmark_stats.hpp"

#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <ratio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using forest_sorting::makeId;
using forest_sorting::makeRandomId;
using forest_sorting::Node;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using namespace forest_sorting::test_support;

enum class Pattern : uint8_t {
    AlreadySorted,
    ReverseSorted,
    Random,
    NearlySorted,
    SameHigh32,
    SameHigh64,
    LongCommonPrefix,
    FirstByteDiffers,
    LastByteDiffers
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

struct Options {
    OutputFormat format = OutputFormat::Table;
    std::vector<std::size_t> sizes = {4, 8, 16, 24, 32};
    std::vector<Pattern> patterns = {
        Pattern::AlreadySorted,    Pattern::ReverseSorted,
        Pattern::Random,           Pattern::NearlySorted,
        Pattern::SameHigh32,       Pattern::SameHigh64,
        Pattern::LongCommonPrefix, Pattern::FirstByteDiffers,
        Pattern::LastByteDiffers};
    std::vector<std::string> sorts = {"linear", "linear-chunk-cache", "binary",
                                      "exponential", "branchless-bitwise"};
    int iterations = 100;
    int warmup = 10;
    std::size_t numRanges = 1000;
    uint32_t seed = 0x5eed1234U;
    std::string baselineSort = "linear";
    bool help = false;
};

struct MicroResult {
    std::string pattern;
    std::size_t rangeSize = 0;
    std::string algorithm;
    std::vector<double> samples; // nanoseconds per range
    SampleStats stats;
    double deltaMedianPct = 0.0;
    ConfidenceInterval deltaPctCi95;
    std::string winner = "none";
    std::string baselineName = "none";
    std::string status = "ok";
};

inline std::vector<Node> generateTailPattern(Pattern pattern,
                                             std::size_t rangeSize,
                                             std::size_t numRanges,
                                             uint32_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Node> nodes;
    nodes.reserve(numRanges * rangeSize);

    for (std::size_t rangeIdx = 0; rangeIdx < numRanges; ++rangeIdx) {
        std::vector<UInt128> ids;
        ids.reserve(rangeSize);

        switch (pattern) {
        case Pattern::AlreadySorted: {
            for (std::size_t i = 0; i < rangeSize; ++i) {
                ids.push_back(i + 1);
            }
            break;
        }
        case Pattern::ReverseSorted: {
            for (std::size_t i = 0; i < rangeSize; ++i) {
                ids.push_back(rangeSize - i);
            }
            break;
        }
        case Pattern::Random: {
            for (std::size_t i = 0; i < rangeSize; ++i) {
                ids.push_back(makeRandomId(rng));
            }
            break;
        }
        case Pattern::NearlySorted: {
            for (std::size_t i = 0; i < rangeSize; ++i) {
                ids.push_back(i + 1);
            }
            std::size_t swaps = std::max<std::size_t>(1, rangeSize / 10);
            for (std::size_t swapIdx = 0; swapIdx < swaps; ++swapIdx) {
                std::size_t idx = rng() % (rangeSize - 1);
                std::swap(ids[idx], ids[idx + 1]);
            }
            break;
        }
        case Pattern::SameHigh32: {
            uint64_t sharedHigh32 = rng() & 0xffffffff00000000ULL;
            for (std::size_t i = 0; i < rangeSize; ++i) {
                uint64_t high = sharedHigh32 | (rng() & 0xffffffffULL);
                uint64_t low = rng();
                ids.push_back(makeId(high, low));
            }
            break;
        }
        case Pattern::SameHigh64: {
            uint64_t sharedHigh = rng();
            for (std::size_t i = 0; i < rangeSize; ++i) {
                ids.push_back(makeId(sharedHigh, rng()));
            }
            break;
        }
        case Pattern::LongCommonPrefix: {
            uint64_t sharedHigh = rng();
            uint64_t sharedLowBase = rng() & 0xffffffffffff0000ULL;
            for (std::size_t i = 0; i < rangeSize; ++i) {
                uint64_t low = sharedLowBase | (rng() & 0xffffULL);
                ids.push_back(makeId(sharedHigh, low));
            }
            break;
        }
        case Pattern::FirstByteDiffers: {
            uint64_t sharedHighBase =
                0x0011223344556677ULL & 0x00ffffffffffffffULL;
            for (std::size_t i = 0; i < rangeSize; ++i) {
                uint64_t firstByte = rng() & 0xffULL;
                uint64_t high = (firstByte << 56) | sharedHighBase;
                ids.push_back(makeId(high, 0));
            }
            break;
        }
        case Pattern::LastByteDiffers: {
            uint64_t sharedHigh = rng();
            uint64_t sharedLowBase = rng() & 0xffffffffffffff00ULL;
            for (std::size_t i = 0; i < rangeSize; ++i) {
                uint64_t low = sharedLowBase | (rng() & 0xffULL);
                ids.push_back(makeId(sharedHigh, low));
            }
            break;
        }
        }

        for (UInt128 nodeId : ids) {
            nodes.push_back(Node{nodeId, 0});
        }
    }

    return nodes;
}

template <typename Sorter>
std::vector<double> sampleSorter(const Sorter &sorter,
                                 const std::vector<Node> &nodes,
                                 std::size_t rangeSize, std::size_t numRanges,
                                 int iterations, int warmup) {
    std::vector<std::size_t> order(nodes.size());
    UInt128NodeTraits traits;

    for (int iter = 0; iter < warmup; ++iter) {
        std::iota(order.begin(), order.end(), std::size_t{0});
        for (std::size_t start = 0; start < nodes.size(); start += rangeSize) {
            sorter(order, nodes, traits, start, start + rangeSize);
        }
    }

    std::vector<double> samples(iterations);
    for (int iter = 0; iter < iterations; ++iter) {
        std::iota(order.begin(), order.end(), std::size_t{0});
        const auto startClock = std::chrono::high_resolution_clock::now();
        for (std::size_t start = 0; start < nodes.size(); start += rangeSize) {
            sorter(order, nodes, traits, start, start + rangeSize);
        }
        const auto endClock = std::chrono::high_resolution_clock::now();
        samples[iter] =
            (std::chrono::duration<double, std::nano>(endClock - startClock)
                 .count()) /
            static_cast<double>(numRanges);
    }
    return samples;
}

Pattern parsePattern(std::string_view value) {
    if (value == "already-sorted" || value == "already sorted") {
        return Pattern::AlreadySorted;
    }
    if (value == "reverse-sorted" || value == "reverse sorted") {
        return Pattern::ReverseSorted;
    }
    if (value == "random") {
        return Pattern::Random;
    }
    if (value == "nearly-sorted" || value == "nearly sorted") {
        return Pattern::NearlySorted;
    }
    if (value == "same-high32") {
        return Pattern::SameHigh32;
    }
    if (value == "same-high64") {
        return Pattern::SameHigh64;
    }
    if (value == "long-common-prefix" || value == "long common prefix") {
        return Pattern::LongCommonPrefix;
    }
    if (value == "first-byte-differs" || value == "first-byte differs") {
        return Pattern::FirstByteDiffers;
    }
    if (value == "last-byte-differs" || value == "last-byte differs") {
        return Pattern::LastByteDiffers;
    }
    throw std::runtime_error("unknown pattern: " + std::string(value));
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customSizes = false;
    bool customPatterns = false;
    bool customSorts = false;

    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string_view option = argv[argIndex];
        if (option == "--help") {
            options.help = true;
            return options;
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
            options.sizes.push_back(
                static_cast<std::size_t>(std::stoull(std::string(value))));
        } else if (option == "--pattern") {
            if (!customPatterns) {
                options.patterns.clear();
                customPatterns = true;
            }
            if (value == "all") {
                options.patterns = {
                    Pattern::AlreadySorted,    Pattern::ReverseSorted,
                    Pattern::Random,           Pattern::NearlySorted,
                    Pattern::SameHigh32,       Pattern::SameHigh64,
                    Pattern::LongCommonPrefix, Pattern::FirstByteDiffers,
                    Pattern::LastByteDiffers};
            } else {
                options.patterns.push_back(parsePattern(value));
            }
        } else if (option == "--sort") {
            if (!customSorts) {
                options.sorts.clear();
                customSorts = true;
            }
            options.sorts.push_back(std::string(value));
        } else if (option == "--iterations") {
            options.iterations = std::stoi(std::string(value));
        } else if (option == "--warmup") {
            options.warmup = std::stoi(std::string(value));
        } else if (option == "--ranges") {
            options.numRanges = std::stoull(std::string(value));
        } else if (option == "--seed") {
            options.seed = static_cast<uint32_t>(
                std::stoul(std::string(value), nullptr, 0));
        } else if (option == "--baseline-sort") {
            options.baselineSort = std::string(value);
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    for (std::size_t size : options.sizes) {
        if (size == 0) {
            throw std::runtime_error("range size must be greater than 0");
        }
    }
    if (options.numRanges == 0) {
        throw std::runtime_error("number of ranges must be greater than 0");
    }
    if (options.iterations <= 0) {
        throw std::runtime_error("iterations must be greater than 0");
    }
    if (options.warmup < 0) {
        throw std::runtime_error("warmup iterations cannot be negative");
    }

    return options;
}

void printHelp() {
    std::cout
        << "usage: forest-sorting-tail-bench [options]\n"
        << "\n"
        << "options:\n"
        << "  --format table|csv|tsv|json\n"
        << "  --size N                         repeatable (default: 4, 8, 16, "
           "24, 32)\n"
        << "  --pattern PATTERN                repeatable (default: all)\n"
        << "                                   "
           "already-sorted|reverse-sorted|random|nearly-sorted|\n"
        << "                                   "
           "same-high32|same-high64|long-common-prefix|\n"
        << "                                   "
           "first-byte-differs|last-byte-differs|all\n"
        << "  --sort NAME                      repeatable (default: all)\n"
        << "                                   "
           "linear|linear-chunk-cache|binary|exponential|branchless-"
           "bitwise\n"
        << "  --iterations N                   (default: 100)\n"
        << "  --warmup N                       (default: 10)\n"
        << "  --ranges N                       (default: 1000)\n"
        << "  --seed N                         (default: 0x5eed1234)\n"
        << "  --baseline-sort NAME             (default: linear)\n"
        << "  --help\n";
}

std::vector<MicroResult> runMicrobenchmarks(const Options &options) {
    std::vector<MicroResult> results;

    std::vector<std::size_t> sizes = options.sizes;
    std::vector<Pattern> patterns = options.patterns;

    struct Algo {
        std::string name;
        std::function<void(std::vector<std::size_t> &,
                           const std::vector<Node> &, const UInt128NodeTraits &,
                           std::size_t, std::size_t)>
            func;
    };

    std::vector<Algo> algos = {
        {"linear",
         [](auto &order, const auto &nodes, const auto &traits, auto begin,
            auto end) {
             LinearSmallSorter{}(order, nodes, traits, begin, end);
         }},
        {"linear-chunk-cache",
         [](auto &order, const auto &nodes, const auto &traits, auto begin,
            auto end) {
             LinearCachedChunksSmallSorterDynamic{}(order, nodes, traits, begin,
                                                    end);
         }},
        {"binary",
         [](auto &order, const auto &nodes, const auto &traits, auto begin,
            auto end) {
             BinarySmallSorterDynamic{}(order, nodes, traits, begin, end);
         }},
        {"exponential",
         [](auto &order, const auto &nodes, const auto &traits, auto begin,
            auto end) {
             ExponentialSmallSorterDynamic{}(order, nodes, traits, begin, end);
         }},
        {"branchless-bitwise", [](auto &order, const auto &nodes,
                                  const auto &traits, auto begin, auto end) {
             BranchlessBitwiseSmallSorterDynamic{}(order, nodes, traits, begin,
                                                   end);
         }}};

    if (!options.sorts.empty()) {
        std::vector<Algo> filtered;
        for (const auto &sortName : options.sorts) {
            bool found = false;
            for (const auto &algo : algos) {
                if (algo.name == sortName) {
                    filtered.push_back(algo);
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("unknown sort algorithm: " + sortName);
            }
        }
        algos = filtered;
    }

    // Verify baseline is in the list of algorithms to benchmark
    bool baselineFound = false;
    for (const auto &algo : algos) {
        if (algo.name == options.baselineSort) {
            baselineFound = true;
            break;
        }
    }
    if (!baselineFound) {
        throw std::runtime_error(
            "baseline sort '" + options.baselineSort +
            "' is not included in the sort algorithms being benchmarked");
    }

    for (Pattern patternKind : patterns) {
        for (std::size_t size : sizes) {
            std::vector<Node> nodes = generateTailPattern(
                patternKind, size, options.numRanges, options.seed);

            for (const auto &algo : algos) {
                MicroResult res;
                res.pattern = std::string(patternName(patternKind));
                res.rangeSize = size;
                res.algorithm = algo.name;

                try {
                    res.samples =
                        sampleSorter(algo.func, nodes, size, options.numRanges,
                                     options.iterations, options.warmup);
                    res.stats = computeSampleStats(res.samples);
                } catch (const std::exception &exception) {
                    res.status = exception.what();
                }

                results.push_back(std::move(res));
            }
        }
    }

    return results;
}

void computeDeltas(std::vector<MicroResult> &results,
                   std::string_view baselineName) {
    for (auto &res : results) {
        if (res.algorithm == baselineName) {
            continue;
        }

        const MicroResult *baseline = nullptr;
        for (const auto &other : results) {
            if (other.pattern == res.pattern &&
                other.rangeSize == res.rangeSize &&
                other.algorithm == baselineName) {
                baseline = &other;
                break;
            }
        }

        if (baseline != nullptr && baseline->status == "ok" &&
            res.status == "ok") {
            res.deltaMedianPct = medianOfSamples(
                pairedRelativeDeltas(res.samples, baseline->samples));
            res.deltaPctCi95 = bootstrapPairedRelativeDeltaCi95(
                res.samples, baseline->samples);
            res.winner = std::string(
                classifyBenchmarkWinner(res.deltaMedianPct, res.deltaPctCi95));
        }
    }
}

inline std::string formatDouble(double val, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << val;
    return oss.str();
}

void printTable(const std::vector<MicroResult> &results,
                std::string_view baselineSort) {
    std::cout << std::left << std::setw(24) << "pattern" << std::right << "  "
              << std::setw(6) << "size"
              << "  " << std::left << std::setw(22) << "algorithm"
              << "  " << std::right << std::setw(12) << "median_ns"
              << "  " << std::setw(22) << "timing_ci95_ns"
              << "  " << std::setw(12) << "delta_%"
              << "  " << std::setw(22) << "delta_ci95_%"
              << "  " << std::left << std::setw(12) << "winner"
              << "  status\n";

    for (const auto &res : results) {
        std::cout << std::left << std::setw(24) << res.pattern << std::right
                  << "  " << std::setw(6) << res.rangeSize << "  " << std::left
                  << std::setw(22) << res.algorithm << std::right << std::fixed
                  << std::setprecision(1);
        if (res.status == "ok") {
            std::string timingCi = "[" + formatDouble(res.stats.ci95.low, 1) +
                                   ", " + formatDouble(res.stats.ci95.high, 1) +
                                   "]";
            std::cout << "  " << std::setw(12) << res.stats.median << "  "
                      << std::setw(22) << timingCi;
            if (res.algorithm == baselineSort) {
                std::cout << "  " << std::setw(12) << "0.0"
                          << "  " << std::setw(22) << "[0.0, 0.0]"
                          << "  " << std::left << std::setw(12) << "baseline";
            } else {
                std::string deltaCi =
                    "[" + formatDouble(res.deltaPctCi95.low, 1) + ", " +
                    formatDouble(res.deltaPctCi95.high, 1) + "]";
                std::cout << "  " << std::setw(12) << res.deltaMedianPct << "  "
                          << std::setw(22) << deltaCi << "  " << std::left
                          << std::setw(12) << res.winner;
            }
        } else {
            std::cout << "  " << std::setw(12) << "n/a"
                      << "  " << std::setw(22) << "n/a"
                      << "  " << std::setw(12) << "n/a"
                      << "  " << std::setw(22) << "n/a"
                      << "  " << std::left << std::setw(12) << "n/a";
        }
        std::cout << "  " << res.status << "\n";
    }
}

void printDelimited(const std::vector<MicroResult> &results, char delimiter,
                    std::string_view baselineSort) {
    std::cout << "pattern" << delimiter << "size" << delimiter << "algorithm"
              << delimiter << "median_ns" << delimiter << "mean_ns" << delimiter
              << "min_ns" << delimiter << "max_ns" << delimiter << "ci95_low_ns"
              << delimiter << "ci95_high_ns" << delimiter << "delta_pct"
              << delimiter << "delta_ci95_low_pct" << delimiter
              << "delta_ci95_high_pct" << delimiter << "winner" << delimiter
              << "status\n";
    for (const auto &res : results) {
        std::cout << res.pattern << delimiter << res.rangeSize << delimiter
                  << res.algorithm << delimiter;
        if (res.status == "ok") {
            std::cout << std::fixed << std::setprecision(3) << res.stats.median
                      << delimiter << res.stats.mean << delimiter
                      << res.stats.min << delimiter << res.stats.max
                      << delimiter << res.stats.ci95.low << delimiter
                      << res.stats.ci95.high << delimiter;
            if (res.algorithm == baselineSort) {
                std::cout << "0.0" << delimiter << "0.0" << delimiter << "0.0"
                          << delimiter << "baseline" << delimiter;
            } else {
                std::cout << res.deltaMedianPct << delimiter
                          << res.deltaPctCi95.low << delimiter
                          << res.deltaPctCi95.high << delimiter << res.winner
                          << delimiter;
            }
        } else {
            std::cout << "" << delimiter << "" << delimiter << "" << delimiter
                      << "" << delimiter << "" << delimiter << "" << delimiter
                      << "" << delimiter << "" << delimiter << "" << delimiter
                      << "" << delimiter << "" << delimiter;
        }
        std::cout << res.status << "\n";
    }
}

void printJson(const std::vector<MicroResult> &results,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"warmup\": " << options.warmup
              << ",\n  \"num_ranges\": " << options.numRanges
              << ",\n  \"seed\": " << options.seed
              << ",\n  \"baseline_sort\": \"" << options.baselineSort << "\""
              << ",\n  \"results\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto &res = results[i];
        std::cout << "    {\n"
                  << "      \"pattern\": \"" << res.pattern << "\",\n"
                  << "      \"size\": " << res.rangeSize << ",\n"
                  << "      \"algorithm\": \"" << res.algorithm << "\",\n";
        if (res.status == "ok") {
            std::cout << "      \"median_ns\": " << res.stats.median << ",\n"
                      << "      \"mean_ns\": " << res.stats.mean << ",\n"
                      << "      \"min_ns\": " << res.stats.min << ",\n"
                      << "      \"max_ns\": " << res.stats.max << ",\n"
                      << "      \"ci95_low_ns\": " << res.stats.ci95.low
                      << ",\n"
                      << "      \"ci95_high_ns\": " << res.stats.ci95.high
                      << ",\n"
                      << "      \"delta_pct\": "
                      << (res.algorithm == options.baselineSort
                              ? 0.0
                              : res.deltaMedianPct)
                      << ",\n"
                      << "      \"delta_ci95_low_pct\": "
                      << (res.algorithm == options.baselineSort
                              ? 0.0
                              : res.deltaPctCi95.low)
                      << ",\n"
                      << "      \"delta_ci95_high_pct\": "
                      << (res.algorithm == options.baselineSort
                              ? 0.0
                              : res.deltaPctCi95.high)
                      << ",\n"
                      << "      \"winner\": \""
                      << (res.algorithm == options.baselineSort ? "baseline"
                                                                : res.winner)
                      << "\",\n";
        }
        std::cout << "      \"status\": \"" << res.status << "\"\n"
                  << "    }";
        if (i + 1 < results.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

int main(int argc, char **argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.help) {
            printHelp();
            return 0;
        }

        auto results = runMicrobenchmarks(options);
        computeDeltas(results, options.baselineSort);

        switch (options.format) {
        case OutputFormat::Table:
            printTable(results, options.baselineSort);
            break;
        case OutputFormat::Csv:
            printDelimited(results, ',', options.baselineSort);
            break;
        case OutputFormat::Tsv:
            printDelimited(results, '\t', options.baselineSort);
            break;
        case OutputFormat::Json:
            printJson(results, options);
            break;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "forest-sorting-tail-bench failed: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tail-bench failed: unknown exception\n";
        return 1;
    }
}
