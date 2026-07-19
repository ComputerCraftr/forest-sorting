#include "forest_sorting/benchmark_support/full/forest_benchmark_options.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_cli.hpp"
#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/full/parent_registry.hpp"
#include "forest_sorting/benchmark_support/full/sort_registry.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::benchmark_support {

Options::Options()
    : datasets(kAllDatasetKinds.begin(), kAllDatasetKinds.end()),
      parents(defaultParentKinds()), sorts(defaultSortKinds()) {}

namespace {

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

} // namespace

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
            const std::vector<DatasetKind> allDatasets(kAllDatasetKinds.begin(),
                                                       kAllDatasetKinds.end());
            applyRegistrySelection(options.datasets, customDatasets, value,
                                   allDatasets, allDatasets, parseDataset);
        } else if (option == "--parent") {
            applyRegistrySelection(options.parents, customParents, value,
                                   defaultParentKinds(),
                                   registeredParentKinds(), parseParent);
        } else if (option == "--sort") {
            applyRegistrySelection(options.sorts, customSorts, value,
                                   defaultSortKinds(), registeredSortKinds(),
                                   parseSortKind);
        } else if (option == "--iterations") {
            options.iterations = parsePositiveIntOption(value, "--iterations");
        } else if (option == "--warmup") {
            options.warmup = parseNonNegativeIntOption(value, "--warmup");
        } else if (option == "--baseline-sort") {
            options.hasBaselineSort = true;
            options.baselineSort = parseSortKind(value);
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
        appendMissingBaseline(options.sorts, options.baselineSort);
    }
    if (options.hasBaselineParent) {
        appendMissingBaseline(options.parents, options.baselineParent);
    }
    return options;
}

void printHelp() {
    std::cout << "usage: forest-sorting-bench [options]\n"
              << "\n"
              << "options:\n"
              << "  --format table|csv|tsv|json\n"
              << "  --size N                         repeatable\n"
              << "  --dataset ";
    bool first = true;
    for (DatasetKind dataset : allDatasetKinds()) {
        std::cout << (first ? "" : "|") << datasetName(dataset);
        first = false;
    }
    std::cout << "|default|all\n" << "  --parent ";
    first = true;
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        if (!first) {
            std::cout << "|";
        }
        std::cout << entry.name;
        first = false;
    }
    std::cout << "|default|all\n"
              << "  --sort ";
    first = true;
    for (const SortRegistryEntry &entry : sortRegistry()) {
        if (!first) {
            std::cout << "|";
        }
        std::cout << entry.name;
        first = false;
    }
    std::cout
        << "|default|all\n"
        << "                                   depth2 labels use typed "
           "uint16_t depth payloads; chunk8/chunk16/chunk32/chunk64 labels fix "
           "packed ID radix width; full-clear and bitmask-le512 suffixes "
           "identify ID counter policy; default excludes opt-in tuning "
           "experiments\n"
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
           "(default 0x5eed1234)\n"
        << "  --help\n";
}

} // namespace forest_sorting::benchmark_support
