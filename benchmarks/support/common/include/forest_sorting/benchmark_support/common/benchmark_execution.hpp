#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_EXECUTION_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_EXECUTION_HPP

#include "forest_sorting/benchmark_support/common/uint128_fixtures.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace forest_sorting::benchmark_support {

namespace benchmark_execution_detail {

inline uint32_t benchmarkScheduleSeed(std::size_t itemCount, uint8_t kind,
                                      uint32_t dataSeed, uint32_t orderSeed,
                                      uint64_t domainSalt) noexcept {
    uint64_t identity =
        mixDeterministicUInt128Word(static_cast<uint64_t>(itemCount));
    identity ^= static_cast<uint64_t>(kind) << 56U;
    identity ^= static_cast<uint64_t>(dataSeed) << 16U;
    return static_cast<uint32_t>(
        mixFixtureSeed(orderSeed, identity ^ domainSalt));
}

} // namespace benchmark_execution_detail

inline uint32_t benchmarkContextOrderSeed(uint32_t orderSeed) noexcept {
    return static_cast<uint32_t>(
        mixFixtureSeed(orderSeed, 0x636f6e7465787401ULL));
}

inline uint32_t benchmarkParentScheduleSeed(std::size_t nodeCount,
                                            uint8_t datasetKind,
                                            uint32_t dataSeed,
                                            uint32_t orderSeed) noexcept {
    return benchmark_execution_detail::benchmarkScheduleSeed(
        nodeCount, datasetKind, dataSeed, orderSeed, 0x706172656e740002ULL);
}

inline uint32_t benchmarkSortScheduleSeed(std::size_t nodeCount,
                                          uint8_t datasetKind,
                                          uint32_t dataSeed,
                                          uint32_t orderSeed) noexcept {
    return benchmark_execution_detail::benchmarkScheduleSeed(
        nodeCount, datasetKind, dataSeed, orderSeed, 0x736f727400000003ULL);
}

template <typename Descriptor, typename Materialize, typename Process>
void forEachMaterializedContext(const std::vector<Descriptor> &descriptors,
                                const std::vector<std::size_t> &executionOrder,
                                Materialize materialize, Process process) {
    for (std::size_t descriptorIndex : executionOrder) {
        assert(descriptorIndex < descriptors.size());
        auto context = materialize(descriptors[descriptorIndex]);
        process(descriptors[descriptorIndex], context);
    }
}

template <typename Artifact, typename Build>
double replaceRetainedArtifactMs(Artifact &retained, Build build) {
    retained = Artifact{};
    const auto start = std::chrono::steady_clock::now();
    Artifact next = build();
    const auto end = std::chrono::steady_clock::now();
    retained = std::move(next);
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_COMMON_BENCHMARK_EXECUTION_HPP
