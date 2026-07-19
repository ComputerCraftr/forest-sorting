#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_CLI_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_CLI_HPP

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace forest_sorting::benchmark_support {

enum class OutputFormat : uint8_t {
    Table,
    Csv,
    Tsv,
    Json,
};

inline OutputFormat parseFormat(std::string_view value) {
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

template <typename Value>
void appendUniqueSelection(std::vector<Value> &values, const Value &value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

template <typename Value, typename Parse>
void applyRegistrySelection(std::vector<Value> &selection, bool &selectorSeen,
                            std::string_view value,
                            const std::vector<Value> &defaults,
                            const std::vector<Value> &allValues, Parse parse) {
    if (!selectorSeen) {
        selection.clear();
        selectorSeen = true;
    }
    if (value == "default") {
        selection = defaults;
    } else if (value == "all") {
        selection = allValues;
    } else {
        appendUniqueSelection(selection, parse(value));
    }
}

template <typename Value>
void appendMissingBaseline(std::vector<Value> &selection,
                           const Value &baseline) {
    appendUniqueSelection(selection, baseline);
}

template <typename Unsigned>
    requires std::is_unsigned_v<Unsigned>
Unsigned parseUnsignedOption(std::string_view value,
                             std::string_view optionName) {
    int base = 10;
    std::string_view digits = value;
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        base = 16;
        digits.remove_prefix(2);
    }
    if (digits.empty() || digits.front() == '-') {
        throw std::runtime_error(std::string(optionName) +
                                 " must be an unsigned integer");
    }

    Unsigned parsed = 0;
    const auto result = std::from_chars(
        digits.data(), digits.data() + digits.size(), parsed, base);
    if (result.ec != std::errc{} ||
        result.ptr != digits.data() + digits.size()) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be an unsigned integer");
    }
    return parsed;
}

inline std::size_t parsePositiveSizeOption(std::string_view value,
                                           std::string_view optionName) {
    const std::size_t parsed =
        parseUnsignedOption<std::size_t>(value, optionName);
    if (parsed == 0) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be greater than 0");
    }
    return parsed;
}

inline int parseNonNegativeIntOption(std::string_view value,
                                     std::string_view optionName) {
    const unsigned int parsed =
        parseUnsignedOption<unsigned int>(value, optionName);
    if (parsed > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string(optionName) + " is too large");
    }
    return static_cast<int>(parsed);
}

inline int parsePositiveIntOption(std::string_view value,
                                  std::string_view optionName) {
    const int parsed = parseNonNegativeIntOption(value, optionName);
    if (parsed == 0) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be at least 1");
    }
    return parsed;
}

inline int checkedBenchmarkPassCount(int warmup, int iterations) {
    if (warmup > std::numeric_limits<int>::max() - iterations) {
        throw std::runtime_error("warmup plus iterations is too large");
    }
    return warmup + iterations;
}

inline std::size_t checkedSizeProduct(std::size_t lhs, std::size_t rhs,
                                      std::string_view description) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::length_error(std::string(description) + " is too large");
    }
    return lhs * rhs;
}

inline uint32_t parseSeedOption(std::string_view value,
                                std::string_view optionName) {
    if (value == "random") {
        std::random_device device;
        std::uniform_int_distribution<uint32_t> distribution;
        return distribution(device);
    }
    try {
        return parseUnsignedOption<uint32_t>(value, optionName);
    } catch (const std::runtime_error &) {
        throw std::runtime_error(std::string(optionName) +
                                 " must be a uint32 value or random");
    }
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_CLI_HPP
