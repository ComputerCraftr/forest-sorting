#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_DATASET_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_DATASET_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace forest_sorting::benchmark_support {

inline constexpr uint32_t kDefaultBenchmarkDataSeed = 0x5eed1234U;

enum class DatasetKind : uint8_t {
    Random,
    Outliers,
    SameHigh64,
    SameHigh32,
    Sequential,
    ExternalParents,
    Siblings,
};

inline constexpr std::array<DatasetKind, 7> kAllDatasetKinds = {
    DatasetKind::Random,     DatasetKind::Outliers,
    DatasetKind::SameHigh64, DatasetKind::SameHigh32,
    DatasetKind::Sequential, DatasetKind::ExternalParents,
    DatasetKind::Siblings,
};

constexpr std::array<DatasetKind, 7> allDatasetKinds() noexcept {
    return kAllDatasetKinds;
}

inline std::string_view datasetName(DatasetKind datasetKind) {
    switch (datasetKind) {
    case DatasetKind::Random:
        return "random";
    case DatasetKind::Outliers:
        return "outliers";
    case DatasetKind::SameHigh64:
        return "same-high64";
    case DatasetKind::SameHigh32:
        return "same-high32";
    case DatasetKind::Sequential:
        return "sequential";
    case DatasetKind::ExternalParents:
        return "external-parents";
    case DatasetKind::Siblings:
        return "siblings";
    }
    return "unknown";
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_DATASET_HPP
