#ifndef FOREST_SORTING_SUPPORT_BENCHMARK_OUTPUT_HPP
#define FOREST_SORTING_SUPPORT_BENCHMARK_OUTPUT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

namespace forest_sorting::test_support {

using BenchmarkScalarValue =
    std::variant<std::monostate, std::string_view, std::size_t, double>;

enum class StatFieldId : std::uint8_t {
    Min,
    Median,
    Mean,
    Stddev,
    Max,
    Ci95Low,
    Ci95High,
};

struct StatFieldDescriptor {
    StatFieldId id;
    std::string_view name;
};

inline constexpr std::array<StatFieldDescriptor, 7> kStatFields = {{
    {StatFieldId::Min, "min"},
    {StatFieldId::Median, "median"},
    {StatFieldId::Mean, "mean"},
    {StatFieldId::Stddev, "stddev"},
    {StatFieldId::Max, "max"},
    {StatFieldId::Ci95Low, "ci95_low"},
    {StatFieldId::Ci95High, "ci95_high"},
}};

template <typename Stats>
double statFieldValue(const Stats &stats, StatFieldId fieldId) {
    switch (fieldId) {
    case StatFieldId::Min:
        return stats.min;
    case StatFieldId::Median:
        return stats.median;
    case StatFieldId::Mean:
        return stats.mean;
    case StatFieldId::Stddev:
        return stats.stddev;
    case StatFieldId::Max:
        return stats.max;
    case StatFieldId::Ci95Low:
        return stats.ci95.low;
    case StatFieldId::Ci95High:
        return stats.ci95.high;
    }
    return 0.0;
}

inline std::string statFieldName(const StatFieldDescriptor &field,
                                 std::string_view unit) {
    return std::string(field.name) + "_" + std::string(unit);
}

inline const StatFieldDescriptor &statFieldDescriptor(StatFieldId fieldId) {
    return kStatFields[static_cast<std::size_t>(fieldId)];
}

inline std::string csvEscape(std::string_view value) {
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

inline std::string jsonEscape(std::string_view value) {
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

inline void printDelimitedString(std::ostream &output, std::string_view value,
                                 char delimiter) {
    if (delimiter == ',') {
        output << csvEscape(value);
    } else {
        output << value;
    }
}

inline void printDelimitedScalar(std::ostream &output,
                                 const BenchmarkScalarValue &value,
                                 char delimiter) {
    if (const auto *text = std::get_if<std::string_view>(&value)) {
        printDelimitedString(output, *text, delimiter);
    } else if (const auto *integer = std::get_if<std::size_t>(&value)) {
        output << *integer;
    } else if (const auto *number = std::get_if<double>(&value)) {
        output << *number;
    }
}

inline void printJsonScalar(std::ostream &output,
                            const BenchmarkScalarValue &value) {
    if (const auto *text = std::get_if<std::string_view>(&value)) {
        output << '"' << jsonEscape(*text) << '"';
    } else if (const auto *integer = std::get_if<std::size_t>(&value)) {
        output << *integer;
    } else if (const auto *number = std::get_if<double>(&value)) {
        output << *number;
    }
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_BENCHMARK_OUTPUT_HPP
