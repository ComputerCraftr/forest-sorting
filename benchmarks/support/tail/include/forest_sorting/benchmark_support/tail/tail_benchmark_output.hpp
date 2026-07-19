#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_BENCHMARK_OUTPUT_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_BENCHMARK_OUTPUT_HPP

#include "forest_sorting/benchmark_support/common/benchmark_output.hpp"
#include "forest_sorting/benchmark_support/common/benchmark_stats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace forest_sorting::benchmark_support {

struct MicroOutputRow {
    std::string workload;
    std::string pattern;
    std::optional<std::size_t> sourceSize;
    std::optional<std::size_t> tailSize;
    std::size_t minTailSize = 0;
    std::size_t maxTailSize = 0;
    std::size_t tailCount = 0;
    std::string algorithm;
    std::optional<SampleStats> stats;
    std::optional<double> deltaPct;
    std::optional<double> deltaCi95LowPct;
    std::optional<double> deltaCi95HighPct;
    std::optional<std::string> winner;
    std::string status;
    bool baseline = false;
};

enum class MicroFieldId : std::uint8_t {
    Workload,
    Pattern,
    SourceSize,
    TailSize,
    MinTailSize,
    MaxTailSize,
    TailCount,
    Algorithm,
    DeltaPct,
    DeltaCi95LowPct,
    DeltaCi95HighPct,
    Winner,
    Status,
};

struct MicroFieldDescriptor {
    MicroFieldId id;
    std::string_view name;
};

inline constexpr std::array<MicroFieldDescriptor, 13> kMicroRootOutputSchema = {
    {
        {MicroFieldId::Workload, "workload"},
        {MicroFieldId::Pattern, "pattern"},
        {MicroFieldId::SourceSize, "source_size"},
        {MicroFieldId::TailSize, "tail_size"},
        {MicroFieldId::MinTailSize, "min_tail_size"},
        {MicroFieldId::MaxTailSize, "max_tail_size"},
        {MicroFieldId::TailCount, "tail_count"},
        {MicroFieldId::Algorithm, "algorithm"},
        {MicroFieldId::DeltaPct, "delta_pct"},
        {MicroFieldId::DeltaCi95LowPct, "delta_ci95_low_pct"},
        {MicroFieldId::DeltaCi95HighPct, "delta_ci95_high_pct"},
        {MicroFieldId::Winner, "winner"},
        {MicroFieldId::Status, "status"},
    }};

inline constexpr std::array kMicroStatFieldOrder = {
    StatFieldId::Median,   StatFieldId::Mean, StatFieldId::Min,
    StatFieldId::Stddev,   StatFieldId::Max,  StatFieldId::Ci95Low,
    StatFieldId::Ci95High,
};

inline constexpr std::size_t micro_output_field_count =
    kMicroRootOutputSchema.size() + kMicroStatFieldOrder.size();

template <typename RootVisitor, typename StatVisitor>
void visitMicroOutputSchema(RootVisitor rootVisitor, StatVisitor statVisitor) {
    constexpr std::size_t leadingRootFieldCount = 8;
    for (std::size_t fieldIdx = 0; fieldIdx < leadingRootFieldCount;
         ++fieldIdx) {
        rootVisitor(kMicroRootOutputSchema[fieldIdx]);
    }
    for (StatFieldId fieldId : kMicroStatFieldOrder) {
        statVisitor(statFieldDescriptor(fieldId));
    }
    for (std::size_t fieldIdx = leadingRootFieldCount;
         fieldIdx < kMicroRootOutputSchema.size(); ++fieldIdx) {
        rootVisitor(kMicroRootOutputSchema[fieldIdx]);
    }
}

using MicroFieldValue = BenchmarkScalarValue;

inline MicroFieldValue microFieldValue(const MicroOutputRow &row,
                                       MicroFieldId fieldId) {
    switch (fieldId) {
    case MicroFieldId::Workload:
        return std::string_view(row.workload);
    case MicroFieldId::Pattern:
        return std::string_view(row.pattern);
    case MicroFieldId::SourceSize:
        return row.sourceSize ? MicroFieldValue(*row.sourceSize)
                              : MicroFieldValue{};
    case MicroFieldId::TailSize:
        return row.tailSize ? MicroFieldValue(*row.tailSize)
                            : MicroFieldValue{};
    case MicroFieldId::MinTailSize:
        return row.minTailSize;
    case MicroFieldId::MaxTailSize:
        return row.maxTailSize;
    case MicroFieldId::TailCount:
        return row.tailCount;
    case MicroFieldId::Algorithm:
        return std::string_view(row.algorithm);
    case MicroFieldId::DeltaPct:
        return row.deltaPct ? MicroFieldValue(*row.deltaPct)
                            : MicroFieldValue{};
    case MicroFieldId::DeltaCi95LowPct:
        return row.deltaCi95LowPct ? MicroFieldValue(*row.deltaCi95LowPct)
                                   : MicroFieldValue{};
    case MicroFieldId::DeltaCi95HighPct:
        return row.deltaCi95HighPct ? MicroFieldValue(*row.deltaCi95HighPct)
                                    : MicroFieldValue{};
    case MicroFieldId::Winner:
        return row.winner ? MicroFieldValue(std::string_view(*row.winner))
                          : MicroFieldValue{};
    case MicroFieldId::Status:
        return std::string_view(row.status);
    }
    return {};
}

inline MicroFieldValue microStatFieldValue(const MicroOutputRow &row,
                                           StatFieldId fieldId) {
    if (!row.stats.has_value()) {
        return {};
    }
    return statFieldValue(row.stats.value_or(SampleStats{}), fieldId);
}

template <typename Result>
MicroOutputRow makeMicroOutputRow(const Result &result,
                                  std::string_view baselineAlgorithm) {
    MicroOutputRow row;
    row.workload = result.workload;
    row.pattern = result.pattern;
    row.sourceSize = result.sourceSize;
    row.tailSize = result.tailSize;
    row.minTailSize = result.minTailSize;
    row.maxTailSize = result.maxTailSize;
    row.tailCount = result.tailCount;
    row.algorithm = result.algorithm;
    row.status = result.status;
    if (result.status != "ok") {
        return row;
    }

    row.stats = result.stats;
    const bool baseline = result.algorithm == baselineAlgorithm;
    row.baseline = baseline;
    if (result.hasDelta) {
        row.deltaPct = baseline ? 0.0 : result.deltaMedianPct;
        row.deltaCi95LowPct = baseline ? 0.0 : result.deltaPctCi95.low;
        row.deltaCi95HighPct = baseline ? 0.0 : result.deltaPctCi95.high;
        row.winner = baseline ? "baseline" : result.winner;
    }
    return row;
}

inline void printMicroFieldDelimited(std::ostream &output,
                                     const MicroFieldValue &value,
                                     char delimiter, bool baselineDelta) {
    if (std::holds_alternative<double>(value)) {
        if (baselineDelta) {
            output << "0.0";
            return;
        }
    }
    printDelimitedScalar(output, value, delimiter);
}

inline void printMicroDelimited(std::ostream &output,
                                const std::vector<MicroOutputRow> &rows,
                                char delimiter) {
    bool firstField = true;
    auto printSeparator = [&] {
        if (!firstField) {
            output << delimiter;
        }
        firstField = false;
    };
    visitMicroOutputSchema(
        [&](const MicroFieldDescriptor &field) {
            printSeparator();
            output << field.name;
        },
        [&](const StatFieldDescriptor &field) {
            printSeparator();
            output << statFieldName(field, "ns");
        });
    output << '\n';

    output << std::fixed << std::setprecision(3);
    for (const MicroOutputRow &row : rows) {
        firstField = true;
        visitMicroOutputSchema(
            [&](const MicroFieldDescriptor &field) {
                printSeparator();
                const bool baselineDelta =
                    row.baseline &&
                    (field.id == MicroFieldId::DeltaPct ||
                     field.id == MicroFieldId::DeltaCi95LowPct ||
                     field.id == MicroFieldId::DeltaCi95HighPct);
                printMicroFieldDelimited(output, microFieldValue(row, field.id),
                                         delimiter, baselineDelta);
            },
            [&](const StatFieldDescriptor &field) {
                printSeparator();
                printMicroFieldDelimited(output,
                                         microStatFieldValue(row, field.id),
                                         delimiter, false);
            });
        output << '\n';
    }
}

inline std::string formatMicroDouble(double value, int precision) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

inline void printMicroTable(std::ostream &output,
                            const std::vector<MicroOutputRow> &rows) {
    output << std::left << std::setw(24) << "workload" << "  " << std::setw(18)
           << "pattern" << std::right << "  " << std::setw(8) << "source"
           << "  " << std::setw(6) << "tail" << "  " << std::setw(7) << "tails"
           << "  " << std::left << std::setw(22) << "algorithm" << "  "
           << std::right << std::setw(12) << "median_ns" << "  "
           << std::setw(22) << "timing_ci95_ns" << "  " << std::setw(12)
           << "delta_%" << "  " << std::setw(22) << "delta_ci95_%" << "  "
           << std::left << std::setw(12) << "winner" << "  status\n";

    for (const MicroOutputRow &row : rows) {
        const std::string source =
            row.sourceSize ? std::to_string(*row.sourceSize) : "-";
        const std::string size =
            row.tailSize ? std::to_string(*row.tailSize) : "mixed";
        output << std::left << std::setw(24) << row.workload << "  "
               << std::setw(18) << row.pattern << std::right << "  "
               << std::setw(8) << source << "  " << std::setw(6) << size << "  "
               << std::setw(7) << row.tailCount << "  " << std::left
               << std::setw(22) << row.algorithm << std::right << std::fixed
               << std::setprecision(1);
        if (row.stats.has_value()) {
            const SampleStats stats = row.stats.value_or(SampleStats{});
            const std::string timingCi =
                "[" +
                formatMicroDouble(statFieldValue(stats, StatFieldId::Ci95Low),
                                  1) +
                ", " +
                formatMicroDouble(statFieldValue(stats, StatFieldId::Ci95High),
                                  1) +
                "]";
            const std::string deltaCi =
                "[" + formatMicroDouble(row.deltaCi95LowPct.value_or(0.0), 1) +
                ", " +
                formatMicroDouble(row.deltaCi95HighPct.value_or(0.0), 1) + "]";
            output << "  " << std::setw(12)
                   << statFieldValue(stats, StatFieldId::Median) << "  "
                   << std::setw(22) << timingCi << "  " << std::setw(12)
                   << row.deltaPct.value_or(0.0) << "  " << std::setw(22)
                   << deltaCi << "  " << std::left << std::setw(12)
                   << row.winner.value_or("n/a");
        } else {
            output << "  " << std::setw(12) << "n/a" << "  " << std::setw(22)
                   << "n/a" << "  " << std::setw(12) << "n/a" << "  "
                   << std::setw(22) << "n/a" << "  " << std::left
                   << std::setw(12) << "n/a";
        }
        output << "  " << row.status << '\n';
    }
}

inline void printMicroJsonValue(std::ostream &output,
                                const MicroFieldValue &value) {
    printJsonScalar(output, value);
}

inline void printMicroJsonRows(std::ostream &output,
                               const std::vector<MicroOutputRow> &rows) {
    output << "[\n";
    for (std::size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
        const MicroOutputRow &row = rows[rowIdx];
        output << "    {\n";
        bool firstField = true;
        auto printField = [&](std::string_view name,
                              const MicroFieldValue &value) {
            if (std::holds_alternative<std::monostate>(value)) {
                return;
            }
            if (!firstField) {
                output << ",\n";
            }
            output << "      \"" << name << "\": ";
            printMicroJsonValue(output, value);
            firstField = false;
        };
        visitMicroOutputSchema(
            [&](const MicroFieldDescriptor &field) {
                printField(field.name, microFieldValue(row, field.id));
            },
            [&](const StatFieldDescriptor &field) {
                printField(statFieldName(field, "ns"),
                           microStatFieldValue(row, field.id));
            });
        output << "\n    }";
        if (rowIdx + 1 < rows.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]";
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_BENCHMARK_OUTPUT_HPP
