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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using forest_sorting::Node;
using forest_sorting::sortForestByDepthAndId;
using forest_sorting::UInt128;
using forest_sorting::UInt128NodeTraits;
using forest_sorting::verifySortedByDepthAndId;
using namespace forest_sorting::test_support;

constexpr int kDatasetColumnWidth = 24;
constexpr int kCountColumnWidth = 12;
constexpr int kNameColumnWidth = 16;
constexpr int kTimingColumnWidth = 14;
constexpr int kTimingValueWidth = 14;

enum class OutputFormat : uint8_t {
    Table,
    Csv,
    Tsv,
    Json,
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
    int iterations = 1;
    bool shuffle = false;
    uint32_t seed = 0x5eed;
    bool help = false;
};

struct DatasetContext {
    std::size_t nodeCount = 0;
    DatasetKind datasetKind = DatasetKind::Random;
    std::vector<Node> nodes;
    std::vector<std::size_t> expectedParent;
    std::vector<UInt128> expectedIds;
};

struct BenchmarkResult {
    std::string dataset;
    std::size_t nodeCount = 0;
    std::string parentBuilder;
    std::string sortAlgorithm;
    double parentMs = 0.0;
    double sortMs = 0.0;
    double verifyMs = 0.0;
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

uint32_t parseSeed(std::string_view value) {
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
        throw std::runtime_error("--seed must be a uint32 value or random");
    }
    return static_cast<uint32_t>(parsed);
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool customSizes = false;
    bool customDatasets = false;
    bool customParents = false;
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
        } else if (option == "--seed") {
            options.seed = parseSeed(value);
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    return options;
}

void printHelp() {
    std::cout
        << "usage: forest-sorting-bench [options]\n"
        << "\n"
        << "options:\n"
        << "  --format table|csv|tsv|json\n"
        << "  --size N                         repeatable\n"
        << "  --dataset "
           "random|outliers|same-high64|sequential|external-parents|siblings|"
           "all\n"
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
           "benchmarks; chunk-msd labels sort IDs by MSB-first 64-bit "
           "chunks; byte-msd labels use true byte-level MSD\n"
        << "  --iterations N\n"
        << "  --shuffle                        randomize algorithm execution "
           "order\n"
        << "  --seed N|random                  shuffle seed (default 0x5eed); "
           "accepts decimal, hex, or random\n"
        << "  --help\n";
}

std::string appendStatus(std::string status, std::string_view marker) {
    if (status == "ok") {
        return std::string(marker);
    }
    status += "|";
    status += marker;
    return status;
}

BenchmarkResult runBenchmarkRow(const DatasetContext &context,
                                ParentKind parentKind, SortKind sortKind,
                                int iterations) {
    BenchmarkResult result;
    result.dataset = std::string(datasetName(context.datasetKind));
    result.nodeCount = context.nodeCount;
    result.parentBuilder = std::string(parentName(parentKind));
    result.sortAlgorithm = std::string(sortName(sortKind));

    std::vector<std::size_t> parentIndex;
    std::vector<Node> sorted;
    bool verified = false;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        result.parentMs +=
            timeParentBuildMs(context.nodes, parentKind, parentIndex);
        result.sortMs +=
            timeSortMs(context.nodes, parentIndex, sortKind, sorted);
        result.verifyMs += timeVerifyMs(sorted, verified);
    }

    result.parentMs /= static_cast<double>(iterations);
    result.sortMs /= static_cast<double>(iterations);
    result.verifyMs /= static_cast<double>(iterations);

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

    return result;
}

std::vector<BenchmarkResult> runBenchmarks(const Options &options) {
    std::vector<DatasetContext> contexts;
    for (std::size_t nodeCount : options.sizes) {
        for (DatasetKind datasetKind : options.datasets) {
            DatasetContext context;
            context.nodeCount = nodeCount;
            context.datasetKind = datasetKind;
            context.nodes = makeGeneratedForestForKind(datasetKind, nodeCount);
            context.expectedParent =
                buildParentIndexForKind(ParentKind::Control, context.nodes);
            const auto canonicalSorted = sortForestForKind(
                SortKind::Comparison, context.nodes, context.expectedParent);

            context.expectedIds.reserve(canonicalSorted.size());
            for (const auto &node : canonicalSorted) {
                context.expectedIds.push_back(node.id);
            }
            contexts.push_back(std::move(context));
        }
    }

    struct Job {
        const DatasetContext *context;
        ParentKind parent;
        SortKind sort;
    };
    std::vector<Job> jobs;

    for (const auto &context : contexts) {
        for (ParentKind parentKind : options.parents) {
            for (SortKind sortKind : options.sorts) {
                jobs.push_back({&context, parentKind, sortKind});
            }
        }
    }

    if (options.shuffle) {
        std::mt19937 engine(options.seed);
        std::shuffle(jobs.begin(), jobs.end(), engine);
    }

    std::vector<BenchmarkResult> results;
    results.reserve(jobs.size());
    for (const auto &job : jobs) {
        results.push_back(runBenchmarkRow(*job.context, job.parent, job.sort,
                                          options.iterations));
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

void printBenchmarkHeader() {
    std::cout << std::left << std::setw(kDatasetColumnWidth) << "dataset"
              << std::right << "  " << std::setw(kCountColumnWidth)
              << "node_count"
              << "  " << std::setw(kNameColumnWidth) << "parent_builder"
              << "  " << std::setw(kNameColumnWidth) << "sort_algorithm"
              << "  " << std::setw(kTimingColumnWidth) << "parent_ms"
              << "  " << std::setw(kTimingColumnWidth) << "sort_ms"
              << "  " << std::setw(kTimingColumnWidth) << "verify_ms"
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
                  << std::setw(kNameColumnWidth) << result.parentBuilder << "  "
                  << std::setw(kNameColumnWidth) << result.sortAlgorithm;
        printTiming(result.parentMs);
        printTiming(result.sortMs);
        printTiming(result.verifyMs);
        std::cout << "  " << result.status << "\n";
    }
}

void printDelimited(const std::vector<BenchmarkResult> &results,
                    char delimiter) {
    const bool csv = delimiter == ',';
    std::cout << "dataset" << delimiter << "node_count" << delimiter
              << "parent_builder" << delimiter << "sort_algorithm" << delimiter
              << "parent_ms" << delimiter << "sort_ms" << delimiter
              << "verify_ms" << delimiter << "status\n";
    for (const BenchmarkResult &result : results) {
        if (csv) {
            std::cout << csvEscape(result.dataset) << delimiter;
        } else {
            std::cout << result.dataset << delimiter;
        }
        std::cout << result.nodeCount << delimiter;
        if (csv) {
            std::cout << csvEscape(result.parentBuilder) << delimiter
                      << csvEscape(result.sortAlgorithm) << delimiter;
        } else {
            std::cout << result.parentBuilder << delimiter
                      << result.sortAlgorithm << delimiter;
        }
        std::cout << result.parentMs << delimiter << result.sortMs << delimiter
                  << result.verifyMs << delimiter;
        if (csv) {
            std::cout << csvEscape(result.status);
        } else {
            std::cout << result.status;
        }
        std::cout << "\n";
    }
}

void printJson(const std::vector<BenchmarkResult> &results,
               const Options &options) {
    std::cout << "{\n  \"iterations\": " << options.iterations
              << ",\n  \"shuffle\": " << (options.shuffle ? "true" : "false")
              << ",\n  \"seed\": \"0x" << std::hex << options.seed << std::dec
              << "\",\n  \"results\": [\n";
    for (std::size_t resultIndex = 0; resultIndex < results.size();
         ++resultIndex) {
        const BenchmarkResult &result = results[resultIndex];
        std::cout << "    {"
                  << "\"dataset\": \"" << jsonEscape(result.dataset)
                  << "\", \"node_count\": " << result.nodeCount
                  << ", \"parent_builder\": \""
                  << jsonEscape(result.parentBuilder)
                  << "\", \"sort_algorithm\": \""
                  << jsonEscape(result.sortAlgorithm)
                  << "\", \"parent_ms\": " << result.parentMs
                  << ", \"sort_ms\": " << result.sortMs
                  << ", \"verify_ms\": " << result.verifyMs
                  << ", \"status\": \"" << jsonEscape(result.status) << "\"}";
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
