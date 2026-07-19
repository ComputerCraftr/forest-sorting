#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OUTPUT_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OUTPUT_HPP

#include "forest_sorting/benchmark_support/common/benchmark_output.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_stats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace forest_sorting::benchmark_support {

struct BenchmarkOutputRow {
    std::string_view dataset;
    std::size_t nodeCount = 0;
    std::string dataSeed;
    std::string_view parentBuilder;
    std::string_view sortAlgorithm;
    std::size_t samples = 0;
    std::string_view sortBaseline;
    std::string_view sortComparisonStatus;
    std::string_view sortWinner;
    bool sortDeltaAvailable = false;
    double sortDeltaMedianMs = 0.0;
    double sortDeltaMedianPct = 0.0;
    ConfidenceInterval sortDeltaPctCi95;
    std::string_view parentBaseline;
    std::string_view parentComparisonStatus;
    std::string_view parentWinner;
    bool parentDeltaAvailable = false;
    double parentDeltaMedianMs = 0.0;
    double parentDeltaMedianPct = 0.0;
    ConfidenceInterval parentDeltaPctCi95;
    std::string_view pipelineBaselineParent;
    std::string_view pipelineBaselineSort;
    std::string_view pipelineComparisonStatus;
    std::string_view pipelineWinner;
    bool pipelineDeltaAvailable = false;
    double pipelineDeltaMedianMs = 0.0;
    double pipelineDeltaMedianPct = 0.0;
    ConfidenceInterval pipelineDeltaPctCi95;
    SampleStats parentStats;
    SampleStats sortStats;
    SampleStats pipelineStats;
    SampleStats verifyStats;
    std::span<const double> parentSamples;
    std::span<const double> sortSamples;
    std::span<const double> pipelineSamples;
    std::span<const double> verifySamples;
    std::string_view status;
};

template <typename Result>
BenchmarkOutputRow makeBenchmarkOutputRow(const Result &result,
                                          std::string dataSeed) {
    return {
        result.dataset,
        result.nodeCount,
        std::move(dataSeed),
        result.parentBuilder,
        result.sortAlgorithm,
        result.sortSamples.size(),
        result.sortBaseline,
        result.sortComparisonStatus,
        result.sortWinner,
        result.sortDeltaAvailable,
        result.sortDeltaMedianMs,
        result.sortDeltaMedianPct,
        result.sortDeltaPctCi95,
        result.parentBaseline,
        result.parentComparisonStatus,
        result.parentWinner,
        result.parentDeltaAvailable,
        result.parentDeltaMedianMs,
        result.parentDeltaMedianPct,
        result.parentDeltaPctCi95,
        result.pipelineBaselineParent,
        result.pipelineBaselineSort,
        result.pipelineComparisonStatus,
        result.pipelineWinner,
        result.pipelineDeltaAvailable,
        result.pipelineDeltaMedianMs,
        result.pipelineDeltaMedianPct,
        result.pipelineDeltaPctCi95,
        result.parentStats,
        result.sortStats,
        result.pipelineStats,
        result.verifyStats,
        result.parentSamples,
        result.sortSamples,
        result.pipelineSamples,
        result.verifySamples,
        result.status,
    };
}

enum class BenchmarkFieldId : std::uint8_t {
    Dataset,
    NodeCount,
    DataSeed,
    ParentBuilder,
    SortAlgorithm,
    Samples,
    SortBaseline,
    SortComparisonStatus,
    SortWinner,
    SortDeltaMedianMs,
    SortDeltaMedianPct,
    SortDeltaCi95LowPct,
    SortDeltaCi95HighPct,
    ParentBaseline,
    ParentComparisonStatus,
    ParentWinner,
    ParentDeltaMedianMs,
    ParentDeltaMedianPct,
    ParentDeltaCi95LowPct,
    ParentDeltaCi95HighPct,
    PipelineBaselineParent,
    PipelineBaselineSort,
    PipelineComparisonStatus,
    PipelineWinner,
    PipelineDeltaMedianMs,
    PipelineDeltaMedianPct,
    PipelineDeltaCi95LowPct,
    PipelineDeltaCi95HighPct,
    Status,
};

struct BenchmarkFieldDescriptor {
    BenchmarkFieldId id;
    std::string_view delimitedName;
    std::string_view jsonName;
};

inline constexpr std::array<BenchmarkFieldDescriptor, 29>
    kBenchmarkRootOutputSchema = {{
        {BenchmarkFieldId::Dataset, "dataset", "dataset"},
        {BenchmarkFieldId::NodeCount, "node_count", "node_count"},
        {BenchmarkFieldId::DataSeed, "data_seed", "data_seed"},
        {BenchmarkFieldId::ParentBuilder, "parent_builder", "parent_builder"},
        {BenchmarkFieldId::SortAlgorithm, "sort_algorithm", "sort_algorithm"},
        {BenchmarkFieldId::Samples, "samples", "samples"},
        {BenchmarkFieldId::SortBaseline, "sort_baseline", "sort_baseline"},
        {BenchmarkFieldId::SortComparisonStatus, "sort_comparison_status",
         "sort_comparison_status"},
        {BenchmarkFieldId::SortWinner, "sort_winner", "sort_winner"},
        {BenchmarkFieldId::SortDeltaMedianMs, "sort_delta_median_ms",
         "sort_delta_median_ms"},
        {BenchmarkFieldId::SortDeltaMedianPct, "sort_delta_median_pct",
         "sort_delta_median_pct"},
        {BenchmarkFieldId::SortDeltaCi95LowPct, "sort_delta_ci95_low_pct",
         "sort_delta_ci95_low_pct"},
        {BenchmarkFieldId::SortDeltaCi95HighPct, "sort_delta_ci95_high_pct",
         "sort_delta_ci95_high_pct"},
        {BenchmarkFieldId::ParentBaseline, "parent_baseline",
         "parent_baseline"},
        {BenchmarkFieldId::ParentComparisonStatus, "parent_comparison_status",
         "parent_comparison_status"},
        {BenchmarkFieldId::ParentWinner, "parent_winner", "parent_winner"},
        {BenchmarkFieldId::ParentDeltaMedianMs, "parent_delta_median_ms",
         "parent_delta_median_ms"},
        {BenchmarkFieldId::ParentDeltaMedianPct, "parent_delta_median_pct",
         "parent_delta_median_pct"},
        {BenchmarkFieldId::ParentDeltaCi95LowPct, "parent_delta_ci95_low_pct",
         "parent_delta_ci95_low_pct"},
        {BenchmarkFieldId::ParentDeltaCi95HighPct, "parent_delta_ci95_high_pct",
         "parent_delta_ci95_high_pct"},
        {BenchmarkFieldId::PipelineBaselineParent, "pipeline_baseline_parent",
         "pipeline_baseline_parent"},
        {BenchmarkFieldId::PipelineBaselineSort, "pipeline_baseline_sort",
         "pipeline_baseline_sort"},
        {BenchmarkFieldId::PipelineComparisonStatus,
         "pipeline_comparison_status", "pipeline_comparison_status"},
        {BenchmarkFieldId::PipelineWinner, "pipeline_winner",
         "pipeline_winner"},
        {BenchmarkFieldId::PipelineDeltaMedianMs, "pipeline_delta_median_ms",
         "pipeline_delta_median_ms"},
        {BenchmarkFieldId::PipelineDeltaMedianPct, "pipeline_delta_median_pct",
         "pipeline_delta_median_pct"},
        {BenchmarkFieldId::PipelineDeltaCi95LowPct,
         "pipeline_delta_ci95_low_pct", "pipeline_delta_ci95_low_pct"},
        {BenchmarkFieldId::PipelineDeltaCi95HighPct,
         "pipeline_delta_ci95_high_pct", "pipeline_delta_ci95_high_pct"},
        {BenchmarkFieldId::Status, "status", "status"},
    }};

struct BenchmarkPhaseDescriptor {
    std::string_view name;
    SampleStats BenchmarkOutputRow::*stats;
};

inline constexpr std::array<BenchmarkPhaseDescriptor, 4> kBenchmarkPhases = {{
    {"parent", &BenchmarkOutputRow::parentStats},
    {"sort", &BenchmarkOutputRow::sortStats},
    {"pipeline", &BenchmarkOutputRow::pipelineStats},
    {"verify", &BenchmarkOutputRow::verifyStats},
}};

inline constexpr std::size_t benchmark_output_field_count =
    kBenchmarkRootOutputSchema.size() +
    (kBenchmarkPhases.size() * kStatFields.size());

template <typename RootVisitor, typename StatVisitor>
void visitBenchmarkOutputSchema(RootVisitor rootVisitor,
                                StatVisitor statVisitor) {
    for (const BenchmarkFieldDescriptor &field : kBenchmarkRootOutputSchema) {
        if (field.id != BenchmarkFieldId::Status) {
            rootVisitor(field);
        }
    }
    for (const BenchmarkPhaseDescriptor &phase : kBenchmarkPhases) {
        for (const StatFieldDescriptor &field : kStatFields) {
            statVisitor(phase, field);
        }
    }
    rootVisitor(kBenchmarkRootOutputSchema.back());
}

inline BenchmarkScalarValue benchmarkFieldValue(const BenchmarkOutputRow &row,
                                                BenchmarkFieldId fieldId) {
    switch (fieldId) {
    case BenchmarkFieldId::Dataset:
        return row.dataset;
    case BenchmarkFieldId::NodeCount:
        return row.nodeCount;
    case BenchmarkFieldId::DataSeed:
        return std::string_view(row.dataSeed);
    case BenchmarkFieldId::ParentBuilder:
        return row.parentBuilder;
    case BenchmarkFieldId::SortAlgorithm:
        return row.sortAlgorithm;
    case BenchmarkFieldId::Samples:
        return row.samples;
    case BenchmarkFieldId::SortBaseline:
        return row.sortBaseline;
    case BenchmarkFieldId::SortComparisonStatus:
        return row.sortComparisonStatus;
    case BenchmarkFieldId::SortWinner:
        return row.sortDeltaAvailable ? BenchmarkScalarValue(row.sortWinner)
                                      : BenchmarkScalarValue{};
    case BenchmarkFieldId::SortDeltaMedianMs:
        return row.sortDeltaAvailable
                   ? BenchmarkScalarValue(row.sortDeltaMedianMs)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::SortDeltaMedianPct:
        return row.sortDeltaAvailable
                   ? BenchmarkScalarValue(row.sortDeltaMedianPct)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::SortDeltaCi95LowPct:
        return row.sortDeltaAvailable
                   ? BenchmarkScalarValue(row.sortDeltaPctCi95.low)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::SortDeltaCi95HighPct:
        return row.sortDeltaAvailable
                   ? BenchmarkScalarValue(row.sortDeltaPctCi95.high)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::ParentBaseline:
        return row.parentBaseline;
    case BenchmarkFieldId::ParentComparisonStatus:
        return row.parentComparisonStatus;
    case BenchmarkFieldId::ParentWinner:
        return row.parentDeltaAvailable ? BenchmarkScalarValue(row.parentWinner)
                                        : BenchmarkScalarValue{};
    case BenchmarkFieldId::ParentDeltaMedianMs:
        return row.parentDeltaAvailable
                   ? BenchmarkScalarValue(row.parentDeltaMedianMs)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::ParentDeltaMedianPct:
        return row.parentDeltaAvailable
                   ? BenchmarkScalarValue(row.parentDeltaMedianPct)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::ParentDeltaCi95LowPct:
        return row.parentDeltaAvailable
                   ? BenchmarkScalarValue(row.parentDeltaPctCi95.low)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::ParentDeltaCi95HighPct:
        return row.parentDeltaAvailable
                   ? BenchmarkScalarValue(row.parentDeltaPctCi95.high)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::PipelineBaselineParent:
        return row.pipelineBaselineParent;
    case BenchmarkFieldId::PipelineBaselineSort:
        return row.pipelineBaselineSort;
    case BenchmarkFieldId::PipelineComparisonStatus:
        return row.pipelineComparisonStatus;
    case BenchmarkFieldId::PipelineWinner:
        return row.pipelineDeltaAvailable
                   ? BenchmarkScalarValue(row.pipelineWinner)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::PipelineDeltaMedianMs:
        return row.pipelineDeltaAvailable
                   ? BenchmarkScalarValue(row.pipelineDeltaMedianMs)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::PipelineDeltaMedianPct:
        return row.pipelineDeltaAvailable
                   ? BenchmarkScalarValue(row.pipelineDeltaMedianPct)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::PipelineDeltaCi95LowPct:
        return row.pipelineDeltaAvailable
                   ? BenchmarkScalarValue(row.pipelineDeltaPctCi95.low)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::PipelineDeltaCi95HighPct:
        return row.pipelineDeltaAvailable
                   ? BenchmarkScalarValue(row.pipelineDeltaPctCi95.high)
                   : BenchmarkScalarValue{};
    case BenchmarkFieldId::Status:
        return row.status;
    }
    return {};
}

inline void printBenchmarkDelimited(std::ostream &output,
                                    const std::vector<BenchmarkOutputRow> &rows,
                                    char delimiter) {
    bool firstField = true;
    auto printSeparator = [&] {
        if (!firstField) {
            output << delimiter;
        }
        firstField = false;
    };
    visitBenchmarkOutputSchema(
        [&](const BenchmarkFieldDescriptor &field) {
            printSeparator();
            output << field.delimitedName;
        },
        [&](const BenchmarkPhaseDescriptor &phase,
            const StatFieldDescriptor &field) {
            printSeparator();
            output << phase.name << '_' << statFieldName(field, "ms");
        });
    output << '\n';

    for (const BenchmarkOutputRow &row : rows) {
        firstField = true;
        visitBenchmarkOutputSchema(
            [&](const BenchmarkFieldDescriptor &field) {
                if (!firstField) {
                    output << delimiter;
                }
                firstField = false;
                printDelimitedScalar(output, benchmarkFieldValue(row, field.id),
                                     delimiter);
            },
            [&](const BenchmarkPhaseDescriptor &phase,
                const StatFieldDescriptor &field) {
                if (!firstField) {
                    output << delimiter;
                }
                firstField = false;
                output << statFieldValue(row.*phase.stats, field.id);
            });
        output << '\n';
    }
}

inline void printJsonNumberArray(std::ostream &output,
                                 std::span<const double> values) {
    output << '[';
    for (std::size_t valueIdx = 0; valueIdx < values.size(); ++valueIdx) {
        if (valueIdx != 0) {
            output << ", ";
        }
        output << values[valueIdx];
    }
    output << ']';
}

inline void printBenchmarkJsonField(std::ostream &output,
                                    const BenchmarkOutputRow &row,
                                    const BenchmarkFieldDescriptor &field) {
    output << '"' << field.jsonName << "\": ";
    printJsonScalar(output, benchmarkFieldValue(row, field.id));
}

inline void printBenchmarkJsonRows(std::ostream &output,
                                   const std::vector<BenchmarkOutputRow> &rows,
                                   bool includeSummary, bool includeRaw) {
    output << "[\n";
    for (std::size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
        const BenchmarkOutputRow &row = rows[rowIdx];
        output << "    {";
        bool firstField = true;
        for (const BenchmarkFieldDescriptor &field :
             kBenchmarkRootOutputSchema) {
            if (field.id == BenchmarkFieldId::Status) {
                continue;
            }
            if (!firstField) {
                output << ", ";
            }
            printBenchmarkJsonField(output, row, field);
            firstField = false;
        }

        if (includeSummary) {
            for (const BenchmarkPhaseDescriptor &phase : kBenchmarkPhases) {
                output << ", \"" << phase.name << "\": {";
                for (std::size_t statIdx = 0; statIdx < kStatFields.size();
                     ++statIdx) {
                    if (statIdx != 0) {
                        output << ", ";
                    }
                    const StatFieldDescriptor &field = kStatFields[statIdx];
                    output << '"' << statFieldName(field, "ms") << "\": "
                           << statFieldValue(row.*phase.stats, field.id);
                }
                output << '}';
            }
        }

        if (includeRaw) {
            constexpr std::array<std::string_view, 4> kSampleNames = {
                "parent_samples_ms",
                "sort_samples_ms",
                "pipeline_samples_ms",
                "verify_samples_ms",
            };
            const std::array<std::span<const double>, 4> samples = {
                row.parentSamples,
                row.sortSamples,
                row.pipelineSamples,
                row.verifySamples,
            };
            for (std::size_t sampleIdx = 0; sampleIdx < samples.size();
                 ++sampleIdx) {
                output << ", \"" << kSampleNames[sampleIdx] << "\": ";
                printJsonNumberArray(output, samples[sampleIdx]);
            }
        }

        output << ", \"status\": \"" << jsonEscape(row.status) << "\"}";
        if (rowIdx + 1 < rows.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]";
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_FOREST_BENCHMARK_OUTPUT_HPP
