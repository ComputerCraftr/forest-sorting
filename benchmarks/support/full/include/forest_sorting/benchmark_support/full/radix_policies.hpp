#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_POLICIES_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_POLICIES_HPP

#include "forest_sorting/detail/radix_counts.hpp"

#include <cstddef>

namespace forest_sorting::benchmark_support {

using FullClearIdCountPolicy = detail::IdCountPolicy<detail::FullClearCounts>;

template <std::size_t MaxRangeSize>
using TouchedIdCountPolicy =
    detail::IdCountPolicy<detail::BitmaskTouchedCountsUpTo<MaxRangeSize>>;

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_RADIX_POLICIES_HPP
