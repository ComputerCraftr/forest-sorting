add_executable(forest-sorting-bench
    forest_bench.cpp
)

target_link_libraries(forest-sorting-bench PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-bench
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/support"
)
forest_sorting_apply_common_options(forest-sorting-bench)

option(FOREST_SORTING_NATIVE_BENCH "Use -march=native/-flto for benchmarks" OFF)

if(NOT MSVC)
    if(FOREST_SORTING_NATIVE_BENCH)
        target_compile_options(forest-sorting-bench PRIVATE
            $<$<CONFIG:Release>:-march=native;-flto>
        )
        target_link_options(forest-sorting-bench PRIVATE
            $<$<CONFIG:Release>:-march=native;-flto>
        )
    endif()
endif()

add_test(NAME forest-sorting-bench-help
    COMMAND forest-sorting-bench --help
)
add_test(NAME forest-sorting-bench-csv
    COMMAND forest-sorting-bench
            --format csv
            --size 10000
            --dataset random
            --parent all
            --sort adaptive-depth4-msd
)
add_test(NAME forest-sorting-bench-tsv
    COMMAND forest-sorting-bench
            --format tsv
            --size 10000
            --dataset siblings
            --parent control
            --sort all
)
add_test(NAME forest-sorting-bench-json
    COMMAND forest-sorting-bench
            --format json
            --size 10000
            --dataset same-high64
            --parent all
            --sort adaptive-depth4-msd
)
add_test(NAME forest-sorting-bench-fixed-prefix-sorts
    COMMAND forest-sorting-bench
            --size 10000
            --dataset random
            --sort depth-bucket-depth2-lsd
            --sort depth-bucket-depth2-msd
            --sort adaptive-depth2-msd
            --sort adaptive-depth2-msd-binary-small
)
