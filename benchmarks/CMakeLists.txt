add_executable(forest-sorting-bench forest_bench.cpp
                                    support/full/forest_benchmark_app.cpp)

target_link_libraries(forest-sorting-bench PRIVATE forest-sorting-lib)
target_include_directories(
    forest-sorting-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
                                 "${CMAKE_SOURCE_DIR}/tests/support")
forest_sorting_apply_common_options(forest-sorting-bench)

add_executable(forest-sorting-tail-bench tail_microbench.cpp
                                         support/tail/tail_benchmark_app.cpp)
target_link_libraries(forest-sorting-tail-bench PRIVATE forest-sorting-lib)
target_include_directories(
    forest-sorting-tail-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
                                      "${CMAKE_SOURCE_DIR}/tests/support")
forest_sorting_apply_common_options(forest-sorting-tail-bench)

option(FOREST_SORTING_NATIVE_BENCH "Use -march=native/-flto for benchmarks" OFF)

if(NOT MSVC)
    if(FOREST_SORTING_NATIVE_BENCH)
        target_compile_options(forest-sorting-bench
                               PRIVATE $<$<CONFIG:Release>:-march=native;-flto>)
        target_link_options(forest-sorting-bench PRIVATE
                            $<$<CONFIG:Release>:-march=native;-flto>)
    endif()
endif()

include(cmake/common_benchmark_tests.cmake)
include(cmake/full_benchmark_tests.cmake)
include(cmake/tail_benchmark_tests.cmake)
