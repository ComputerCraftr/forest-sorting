#include "forest_sorting/detail/id_radix.hpp"
#include "forest_sorting/detail/radix_counts.hpp"
#include "forest_sorting/uint128.hpp"
#include "full/parent_registry.hpp"
#include "full/radix_ladder_variants.hpp"
#include "full/sort_registry.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>

using namespace forest_sorting::test_support;

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

    using CountPolicy = forest_sorting::detail::FullClearCounts;
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
    for (const SortRegistryEntry &entry : getSortRegistry()) {
        if (entry.category == SortCategory::RangeLadderExperiment) {
            require(!entry.includeByDefault,
                    "sort radix ladder unexpectedly entered defaults");
        }
    }
    for (const ParentRegistryEntry &entry : getParentRegistry()) {
        if (entry.name.find("range-ladder") != std::string_view::npos ||
            entry.name.find("size-ladder") != std::string_view::npos) {
            require(!entry.includeByDefault,
                    "parent radix ladder unexpectedly entered defaults");
        }
    }
}

void runRadixLadderTests() {
    runTest("radix ladders match fixed kernels at every boundary",
            test_radix_ladders_match_fixed_width_kernels_at_boundaries);
    runTest("radix ladder registry rows remain opt-in",
            test_radix_ladder_registry_rows_are_opt_in);
}
