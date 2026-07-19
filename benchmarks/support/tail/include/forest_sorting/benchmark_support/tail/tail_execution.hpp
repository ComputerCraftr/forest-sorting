#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_EXECUTION_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_EXECUTION_HPP

#include "forest_sorting/benchmark_support/common/dataset.hpp"
#include "forest_sorting/benchmark_support/common/uint128_fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace forest_sorting::benchmark_support {

enum class Workload : uint8_t {
    Synthetic,
    CapturedNodeIds,
    CapturedParentQueries,
};

inline std::string_view workloadName(Workload workload) {
    switch (workload) {
    case Workload::Synthetic:
        return "synthetic";
    case Workload::CapturedNodeIds:
        return "captured-node-ids";
    case Workload::CapturedParentQueries:
        return "captured-parent-queries";
    }
    return "unknown";
}

inline constexpr std::array kAllWorkloads = {
    Workload::Synthetic,
    Workload::CapturedNodeIds,
    Workload::CapturedParentQueries,
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
        return "already-sorted";
    case Pattern::ReverseSorted:
        return "reverse-sorted";
    case Pattern::Random:
        return "random";
    case Pattern::NearlySorted:
        return "nearly-sorted";
    case Pattern::SameHigh32:
        return "same-high32";
    case Pattern::SameHigh64:
        return "same-high64";
    case Pattern::LongCommonPrefix:
        return "long-common-prefix";
    case Pattern::FirstByteDiffers:
        return "first-byte-differs";
    case Pattern::LastByteDiffers:
        return "last-byte-differs";
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

struct TailCorpusDescriptor {
    Workload workload = Workload::Synthetic;
    Pattern pattern = Pattern::Random;
    DatasetKind dataset = DatasetKind::Random;
    std::size_t itemCount = 0;
    std::size_t resultBegin = 0;
    std::size_t resultCount = 0;
    std::size_t baselineResultIndex = 0;
};

inline uint64_t
tailDescriptorIdentity(const TailCorpusDescriptor &descriptor) noexcept {
    uint64_t identity = mixDeterministicUInt128Word(
        static_cast<uint64_t>(descriptor.itemCount));
    identity ^= static_cast<uint64_t>(descriptor.workload) << 56U;
    identity ^= static_cast<uint64_t>(descriptor.pattern) << 48U;
    identity ^= static_cast<uint64_t>(descriptor.dataset) << 40U;
    return mixDeterministicUInt128Word(identity);
}

inline uint32_t tailCorpusOrderSeed(uint32_t orderSeed) noexcept {
    return static_cast<uint32_t>(
        mixFixtureSeed(orderSeed, 0x7461696c2d637001ULL));
}

inline uint32_t
tailAlgorithmScheduleSeed(const TailCorpusDescriptor &descriptor,
                          uint32_t orderSeed) noexcept {
    return static_cast<uint32_t>(mixFixtureSeed(
        orderSeed, tailDescriptorIdentity(descriptor) ^ 0x7461696c2d616c02ULL));
}

inline uint64_t
tailSyntheticGenerationSeed(const TailCorpusDescriptor &descriptor,
                            uint32_t dataSeed) noexcept {
    return mixFixtureSeed(dataSeed, tailDescriptorIdentity(descriptor) ^
                                        0x7461696c2d737903ULL);
}

inline uint64_t tailNodeReservoirSeed(const TailCorpusDescriptor &descriptor,
                                      uint32_t dataSeed) noexcept {
    return mixFixtureSeed(dataSeed, tailDescriptorIdentity(descriptor) ^
                                        0x7461696c2d6e6f04ULL);
}

inline uint64_t tailParentReservoirSeed(const TailCorpusDescriptor &descriptor,
                                        uint32_t dataSeed) noexcept {
    return mixFixtureSeed(dataSeed, tailDescriptorIdentity(descriptor) ^
                                        0x7461696c2d706105ULL);
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_TAIL_EXECUTION_HPP
