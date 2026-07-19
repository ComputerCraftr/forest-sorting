file(
    GLOB_RECURSE benchmark_sources
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/benchmarks/*.cpp" "${SOURCE_ROOT}/benchmarks/*.hpp")
file(
    GLOB_RECURSE common_sources
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/benchmarks/support/common/*.cpp"
    "${SOURCE_ROOT}/benchmarks/support/common/*.hpp")
file(
    GLOB_RECURSE tail_sources
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/benchmarks/support/tail/*.cpp"
    "${SOURCE_ROOT}/benchmarks/support/tail/*.hpp")
file(
    GLOB_RECURSE public_headers
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/include/*.hpp")

foreach(source IN LISTS benchmark_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "tests/support|forest_sorting::test_support")
        message(FATAL_ERROR "${source} depends on test support")
    endif()
endforeach()

foreach(source IN LISTS common_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "benchmark_support/(tail|full)/"
       OR contents MATCHES "#include[ \t]+[<\"](tail|full)/")
        message(
            FATAL_ERROR "${source} depends on tail or full benchmark support")
    endif()
    if(contents
       MATCHES
       "sameNodes|UInt128LowIdentityHashTraits|UInt128HighIdentityHashTraits|makeHighIdentityCollisionRoots"
    )
        message(FATAL_ERROR "${source} contains test-only UInt128 fixtures")
    endif()
endforeach()

foreach(source IN LISTS tail_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "benchmark_support/full/" OR contents MATCHES
                                                     "#include[ \t]+[<\"]full/")
        message(FATAL_ERROR "${source} depends on full benchmark support")
    endif()
    if(contents MATCHES "std::function")
        message(FATAL_ERROR "${source} uses type-erased std::function")
    endif()
endforeach()

set(lightweight_full_headers
    "${SOURCE_ROOT}/benchmarks/support/full/include/forest_sorting/benchmark_support/full/forest_benchmark_options.hpp"
    "${SOURCE_ROOT}/benchmarks/support/full/include/forest_sorting/benchmark_support/full/parent_registry.hpp"
    "${SOURCE_ROOT}/benchmarks/support/full/include/forest_sorting/benchmark_support/full/sort_registry.hpp"
)
foreach(source IN LISTS lightweight_full_headers)
    file(READ "${source}" contents)
    if(contents
       MATCHES
       "adaptive_sort_variants|control_parent_baseline|hash_variants|parent_index_baselines|radix_ladder_variants|sort_baselines|uint128_fixtures|benchmark_stats"
    )
        message(
            FATAL_ERROR
                "${source} exposes benchmark implementation dependencies")
    endif()
endforeach()

foreach(source IN LISTS public_headers)
    file(READ "${source}" contents)
    if(contents MATCHES
       "benchmarks/support|tests/support|benchmark_support|test_support")
        message(FATAL_ERROR "${source} depends on non-production support")
    endif()
endforeach()

file(READ "${SOURCE_ROOT}/include/forest_sorting/detail/adaptive_sort.hpp"
     depth_grouping_source)
if(depth_grouping_source MATCHES "ProductionIdCountPolicy")
    message(
        FATAL_ERROR
            "depth grouping directly references the production ID count policy")
endif()
