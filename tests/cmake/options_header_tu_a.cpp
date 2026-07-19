#include "forest_sorting/benchmark_support/full/forest_benchmark_options.hpp"
#include "options_header_link_test.hpp"

#include <cstddef>

std::size_t optionsHeaderSizeFromFirstTranslationUnit() {
    return sizeof(forest_sorting::benchmark_support::Options);
}
