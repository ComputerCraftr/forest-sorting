#ifndef FOREST_SORTING_BENCHMARK_FOREST_BENCHMARK_OPTIONS_HPP
#define FOREST_SORTING_BENCHMARK_FOREST_BENCHMARK_OPTIONS_HPP

#include "common/benchmark_cli.hpp"
#include "common/benchmark_stats.hpp"
#include "full/parent_registry.hpp"
#include "full/sort_registry.hpp"
#include "uint128_fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace forest_sorting::test_support;

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
    std::vector<ParentKind> parents = defaultParentKinds();
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
    ParentKind baselineParent = ParentKind::RadixJoinIdMsdChunk32;
    bool help = false;
};
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
            appendUniqueSelection(options.sizes,
                                  parsePositiveSizeOption(value, "--size"));
        } else if (option == "--dataset") {
            if (!customDatasets) {
                options.datasets.clear();
                customDatasets = true;
            }
            if (value == "all") {
                options.datasets = vectorFromArray(allDatasetKinds());
            } else {
                appendUniqueSelection(options.datasets, parseDataset(value));
            }
        } else if (option == "--parent") {
            if (!customParents) {
                options.parents.clear();
                customParents = true;
            }
            if (value == "default") {
                options.parents = defaultParentKinds();
            } else {
                const ParentKind parsedParent = parseParent(value);
                appendUniqueSelection(options.parents, parsedParent);
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
                appendUniqueSelection(options.sorts, parsedSort);
            }
        } else if (option == "--iterations") {
            options.iterations = parsePositiveIntOption(value, "--iterations");
        } else if (option == "--warmup") {
            options.warmup = parseNonNegativeIntOption(value, "--warmup");
        } else if (option == "--baseline-sort") {
            options.hasBaselineSort = true;
            options.baselineSort = parseSort(value);
        } else if (option == "--baseline-parent") {
            options.hasBaselineParent = true;
            options.baselineParent = parseParent(value);
        } else if (option == "--sample-output") {
            options.sampleOutput = parseSampleOutput(value);
        } else if (option == "--order-seed") {
            options.orderSeed = parseSeedOption(value, "--order-seed");
        } else if (option == "--data-seed") {
            if (!customDataSeeds) {
                options.dataSeeds.clear();
                customDataSeeds = true;
            }
            appendUniqueSelection(options.dataSeeds,
                                  parseSeedOption(value, "--data-seed"));
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
    (void)checkedBenchmarkPassCount(options.warmup, options.iterations);

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

    const bool hasAlternateSort =
        hasSelectionOtherThan(options.sorts, options.baselineSort);
    const bool hasAlternateParent =
        hasSelectionOtherThan(options.parents, options.baselineParent);
    if (options.hasBaselineSort && options.hasBaselineParent) {
        if (!hasAlternateSort && !hasAlternateParent) {
            throw std::runtime_error(
                "pipeline baseline has no non-baseline job to compare");
        }
    } else if (options.hasBaselineSort && !hasAlternateSort) {
        throw std::runtime_error(
            "baseline-sort has no alternate sort to compare");
    } else if (options.hasBaselineParent && !hasAlternateParent) {
        throw std::runtime_error(
            "baseline-parent has no alternate parent to compare");
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
              << "  --parent ";
    bool first = true;
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        if (!first) {
            std::cout << "|";
        }
        std::cout << entry.name;
        first = false;
    }
    std::cout << "|default\n" << "  --sort ";
    first = true;
    for (std::size_t entryIdx = 0; entryIdx < getSortRegistry().size();
         ++entryIdx) {
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
           "depth payloads; chunk8/chunk16/chunk32/chunk64 labels fix packed "
           "ID radix width; "
           "full-clear and bitmask-le512 suffixes identify counter policy; "
           "default excludes opt-in tuning experiments\n"
        << "  --iterations N\n"
        << "  --warmup N\n"
        << "  --baseline-sort NAME             compare selected sorts against "
           "NAME\n"
        << "  --baseline-parent NAME           compare selected parents "
           "against "
           "NAME\n"
        << "                                   specifying both baseline flags "
           "also compares the full parent+sort pipeline\n"
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

#endif // FOREST_SORTING_BENCHMARK_FOREST_BENCHMARK_OPTIONS_HPP
