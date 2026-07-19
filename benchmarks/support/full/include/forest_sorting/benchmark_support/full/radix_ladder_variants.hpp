#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_LADDER_VARIANTS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_LADDER_VARIANTS_HPP

#include "forest_sorting/benchmark_support/full/radix_policies.hpp"
#include "forest_sorting/detail/id_radix.hpp"

#include <cstddef>
#include <vector>

namespace forest_sorting::benchmark_support {

enum class LadderRadixWidth : unsigned char {
    Chunk8,
    Chunk16,
    Chunk32,
};

template <std::size_t Chunk8Max, std::size_t Chunk16Max>
struct Chunk8Chunk16Chunk32Ladder {
    static_assert(Chunk8Max < Chunk16Max);

    static constexpr LadderRadixWidth widthForSize(std::size_t size) noexcept {
        if (size <= Chunk8Max) {
            return LadderRadixWidth::Chunk8;
        }
        if (size <= Chunk16Max) {
            return LadderRadixWidth::Chunk16;
        }
        return LadderRadixWidth::Chunk32;
    }
};

template <std::size_t Chunk16Max> struct Chunk16Chunk32Ladder {
    static constexpr LadderRadixWidth widthForSize(std::size_t size) noexcept {
        return size <= Chunk16Max ? LadderRadixWidth::Chunk16
                                  : LadderRadixWidth::Chunk32;
    }
};

template <detail::IdRadixCountPolicy Policy> struct IdMsdLadderWorkspace {
    detail::IdMsdChunkSortWorkspace<1, Policy> chunk8;
    detail::IdMsdChunkSortWorkspace<2, Policy> chunk16;
    detail::IdMsdChunkSortWorkspace<4, Policy> chunk32;
};

template <typename LadderPolicy, detail::IdRadixCountPolicy Policy,
          typename IdForIndex, typename IdTraits>
void sortIndexRangeByIdMsdLadder(std::vector<std::size_t> &order,
                                 IdForIndex idForIndex, const IdTraits &traits,
                                 std::size_t rangeBegin, std::size_t rangeEnd,
                                 IdMsdLadderWorkspace<Policy> &workspace) {
    switch (LadderPolicy::widthForSize(rangeEnd - rangeBegin)) {
    case LadderRadixWidth::Chunk8:
        detail::sortIndexRangeByIdMsdChunks<1, Policy>(
            order, idForIndex, traits, rangeBegin, rangeEnd, 0,
            workspace.chunk8);
        return;
    case LadderRadixWidth::Chunk16:
        detail::sortIndexRangeByIdMsdChunks<2, Policy>(
            order, idForIndex, traits, rangeBegin, rangeEnd, 0,
            workspace.chunk16);
        return;
    case LadderRadixWidth::Chunk32:
        detail::sortIndexRangeByIdMsdChunks<4, Policy>(
            order, idForIndex, traits, rangeBegin, rangeEnd, 0,
            workspace.chunk32);
        return;
    }
}

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_LADDER_VARIANTS_HPP
