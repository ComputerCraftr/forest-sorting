#include "forest_sorting/benchmark_support/full/adaptive_sort_variants.hpp"
#include "forest_sorting/benchmark_support/full/parent_registry.hpp"
#include "forest_sorting/benchmark_support/full/radix_ladder_variants.hpp"
#include "forest_sorting/benchmark_support/full/radix_policies.hpp"
#include "forest_sorting/benchmark_support/full/sort_registry.hpp"
#include "forest_sorting/detail/adaptive_sort.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "instrumented_radix_counts.hpp"
#include "test_harness.hpp"
#include "test_suites.hpp"
#include "uint128_test_fixtures.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace forest_sorting::test_support;
using namespace forest_sorting::benchmark_support;

template <typename Policy>
concept AcceptsIdWorkspace = requires {
    typename forest_sorting::detail::IdMsdChunkSortWorkspace<1, Policy>;
};

template <typename Policy>
concept AcceptsDepthGrouping = requires(
    std::vector<std::size_t> &order, std::vector<std::size_t> &scratch,
    const std::vector<uint32_t> &depths,
    std::vector<forest_sorting::detail::DepthRange<uint32_t>> &ranges) {
    forest_sorting::detail::groupOrderByDepthMsd<1, Policy>(order, scratch,
                                                            depths, ranges);
};

using InstrumentedIdPolicy =
    forest_sorting::detail::IdCountPolicy<InstrumentedCounterPolicy>;
using InstrumentedDepthPolicy =
    forest_sorting::detail::DepthCountPolicy<InstrumentedCounterPolicy>;

static_assert(AcceptsIdWorkspace<InstrumentedIdPolicy>);
static_assert(!AcceptsIdWorkspace<InstrumentedDepthPolicy>);
static_assert(!AcceptsIdWorkspace<InstrumentedCounterPolicy>);
static_assert(AcceptsDepthGrouping<InstrumentedDepthPolicy>);
static_assert(!AcceptsDepthGrouping<InstrumentedIdPolicy>);
static_assert(!AcceptsDepthGrouping<InstrumentedCounterPolicy>);
static_assert(
    std::same_as<
        forest_sorting::detail::ProductionIdCountPolicy::counter_policy,
        forest_sorting::detail::BitmaskTouchedCountsUpTo<
            forest_sorting::detail::production_touched_count_max_range_size>>);
static_assert(
    std::same_as<
        forest_sorting::detail::ProductionDepthCountPolicy::counter_policy,
        forest_sorting::detail::FullClearCounts>);

template <typename LadderPolicy>
void requireLadderMatchesSelectedKernel(std::size_t rangeSize) {
    std::vector<forest_sorting::UInt128> ids(rangeSize);
    for (std::size_t index = 0; index < rangeSize; ++index) {
        ids[index] = forest_sorting::makeId(
            0x1234567800000000ULL, static_cast<uint64_t>(rangeSize - index));
    }
    auto idForIndex = [&](std::size_t index) { return ids[index]; };

    std::vector<std::size_t> ladderOrder(rangeSize);
    std::iota(ladderOrder.begin(), ladderOrder.end(), std::size_t{0});
    auto fixedOrder = ladderOrder;

    using CountPolicy = FullClearIdCountPolicy;
    IdMsdLadderWorkspace<CountPolicy> ladderWorkspace;
    sortIndexRangeByIdMsdLadder<LadderPolicy, CountPolicy>(
        ladderOrder, idForIndex, forest_sorting::UInt128Traits{}, 0, rangeSize,
        ladderWorkspace);

    switch (LadderPolicy::widthForSize(rangeSize)) {
    case LadderRadixWidth::Chunk8: {
        forest_sorting::detail::IdMsdChunkSortWorkspace<1, CountPolicy>
            workspace;
        forest_sorting::detail::sortIndexRangeByIdMsdChunks<1, CountPolicy>(
            fixedOrder, idForIndex, forest_sorting::UInt128Traits{}, 0,
            rangeSize, 0, workspace);
        break;
    }
    case LadderRadixWidth::Chunk16: {
        forest_sorting::detail::IdMsdChunkSortWorkspace<2, CountPolicy>
            workspace;
        forest_sorting::detail::sortIndexRangeByIdMsdChunks<2, CountPolicy>(
            fixedOrder, idForIndex, forest_sorting::UInt128Traits{}, 0,
            rangeSize, 0, workspace);
        break;
    }
    case LadderRadixWidth::Chunk32: {
        forest_sorting::detail::IdMsdChunkSortWorkspace<4, CountPolicy>
            workspace;
        forest_sorting::detail::sortIndexRangeByIdMsdChunks<4, CountPolicy>(
            fixedOrder, idForIndex, forest_sorting::UInt128Traits{}, 0,
            rangeSize, 0, workspace);
        break;
    }
    }

    require(ladderOrder == fixedOrder,
            "radix ladder differed from its selected fixed-width kernel");
    require(std::is_sorted(ladderOrder.begin(), ladderOrder.end(),
                           [&](std::size_t lhs, std::size_t rhs) {
                               return ids[lhs] < ids[rhs];
                           }),
            "radix ladder differed from the independent ID-order oracle");
}

template <typename LadderPolicy, std::size_t... Thresholds>
void requireLadderThresholdBoundaries(
    [[maybe_unused]] std::index_sequence<Thresholds...> thresholds) {
    ((requireLadderMatchesSelectedKernel<LadderPolicy>(Thresholds - 1),
      requireLadderMatchesSelectedKernel<LadderPolicy>(Thresholds),
      requireLadderMatchesSelectedKernel<LadderPolicy>(Thresholds + 1)),
     ...);
}

void test_radix_ladders_match_fixed_width_kernels_at_boundaries() {
    requireLadderThresholdBoundaries<Chunk8Chunk16Chunk32Ladder<1024, 16384>>(
        std::index_sequence<1024, 16384>{});
    requireLadderThresholdBoundaries<Chunk8Chunk16Chunk32Ladder<2048, 32768>>(
        std::index_sequence<2048, 32768>{});
    requireLadderThresholdBoundaries<Chunk8Chunk16Chunk32Ladder<4096, 65536>>(
        std::index_sequence<4096, 65536>{});
    requireLadderThresholdBoundaries<Chunk16Chunk32Ladder<10000>>(
        std::index_sequence<10000>{});
    requireLadderThresholdBoundaries<Chunk16Chunk32Ladder<16384>>(
        std::index_sequence<16384>{});
    requireLadderThresholdBoundaries<Chunk16Chunk32Ladder<32768>>(
        std::index_sequence<32768>{});
}

void test_radix_ladder_registry_rows_are_opt_in() {
    for (const SortRegistryEntry &entry : sortRegistry()) {
        if (entry.category == SortCategory::RangeLadderExperiment) {
            require(!entry.includeByDefault,
                    "sort radix ladder unexpectedly entered defaults");
        }
    }
    for (const ParentRegistryEntry &entry : parentRegistry()) {
        if (entry.name.find("range-ladder") != std::string_view::npos ||
            entry.name.find("size-ladder") != std::string_view::npos) {
            require(!entry.includeByDefault,
                    "parent radix ladder unexpectedly entered defaults");
        }
    }
}

void test_id_count_policy_is_independent_from_depth_policy() {
    using TouchedIdCounts = TouchedIdCountPolicy<
        forest_sorting::detail::production_touched_count_max_range_size>;
    using DepthCounts = forest_sorting::detail::ProductionDepthCountPolicy;

    std::vector<forest_sorting::Node> nodes;
    nodes.reserve(64);
    for (std::size_t index = 0; index < 64; ++index) {
        nodes.push_back(
            {forest_sorting::makeId(0, static_cast<uint64_t>(64 - index)), 0});
    }
    const std::vector<std::size_t> parentIndex(
        nodes.size(), forest_sorting::detail::no_parent);

    const auto fixed = sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4, TouchedIdCounts, DepthCounts>(nodes, parentIndex, false);
    const auto ladder = sortForestByDepth2FirstThenIdMsdLadderWithParent<
        1024, 16384, TouchedIdCounts, DepthCounts>(nodes, parentIndex);
    const auto control = sortForestByAdaptiveIdMsdChunkWithParent<
        2, 4, FullClearIdCountPolicy, DepthCounts>(nodes, parentIndex, false);

    require(sameNodes(fixed, control) && sameNodes(ladder, control),
            "explicit ID/depth count policies changed benchmark ordering");
}

void test_id_and_depth_wrappers_route_their_underlying_counter() {
    std::vector<forest_sorting::UInt128> ids(64);
    for (std::size_t index = 0; index < ids.size(); ++index) {
        ids[index] = forest_sorting::makeId(index << 56U, ids.size() - index);
    }
    auto idForIndex = [&](std::size_t index) { return ids[index]; };
    std::vector<std::size_t> idOrder(ids.size());
    std::iota(idOrder.begin(), idOrder.end(), std::size_t{0});

    RadixCountOperations idOperations;
    InstrumentedCounterPolicy::operations = &idOperations;
    forest_sorting::detail::IdMsdChunkSortWorkspace<2, InstrumentedIdPolicy>
        idWorkspace;
    forest_sorting::detail::sortIndexRangeByIdMsdChunks<2,
                                                        InstrumentedIdPolicy>(
        idOrder, idForIndex, forest_sorting::UInt128Traits{}, 0, idOrder.size(),
        0, idWorkspace);
    require(idOperations.reset > 0 && idOperations.note > 0 &&
                idOperations.prefix > 0 && idOperations.clear > 0,
            "ID wrapper did not route through its underlying counter");

    std::vector<std::size_t> depthOrder(ids.size());
    std::iota(depthOrder.begin(), depthOrder.end(), std::size_t{0});
    std::vector<std::size_t> depthScratch(ids.size());
    std::vector<uint32_t> depths(ids.size());
    std::vector<forest_sorting::detail::DepthRange<uint32_t>> ranges;
    for (std::size_t index = 0; index < depths.size(); ++index) {
        depths[index] = static_cast<uint32_t>(index % 8);
    }

    RadixCountOperations depthOperations;
    InstrumentedCounterPolicy::operations = &depthOperations;
    forest_sorting::detail::groupOrderByDepthMsd<1, InstrumentedDepthPolicy>(
        depthOrder, depthScratch, depths, ranges);
    require(depthOperations.reset > 0 && depthOperations.note > 0 &&
                depthOperations.prefix > 0 && depthOperations.clear > 0,
            "depth wrapper did not route through its underlying counter");
    InstrumentedCounterPolicy::operations = nullptr;
}

void runRadixLadderTestsImpl() {
    runTest("radix ladders match fixed kernels at every boundary",
            test_radix_ladders_match_fixed_width_kernels_at_boundaries);
    runTest("radix ladder registry rows remain opt-in",
            test_radix_ladder_registry_rows_are_opt_in);
    runTest("ID and depth count policies are independent",
            test_id_count_policy_is_independent_from_depth_policy);
    runTest("ID and depth wrappers route their counters",
            test_id_and_depth_wrappers_route_their_underlying_counter);
}

} // namespace

void runRadixLadderTests() { runRadixLadderTestsImpl(); }
