#include "forest_sorting/benchmark_support/full/forest_benchmark_options.hpp"
#include "options_header_link_test.hpp"

int main() {
    return optionsHeaderSizeFromFirstTranslationUnit() ==
                   sizeof(forest_sorting::benchmark_support::Options)
               ? 0
               : 1;
}
