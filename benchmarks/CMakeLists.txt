add_executable(forest-sorting-bench forest_bench.cpp)

target_link_libraries(forest-sorting-bench PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-bench
                           PRIVATE "${CMAKE_SOURCE_DIR}/tests/support")
forest_sorting_apply_common_options(forest-sorting-bench)

option(FOREST_SORTING_NATIVE_BENCH
       "Use -march=native/-flto for benchmarks" OFF)

if(NOT MSVC)
    if(FOREST_SORTING_NATIVE_BENCH)
        target_compile_options(
            forest-sorting-bench PRIVATE
            $<$<CONFIG:Release>:-march=native;-flto>)
        target_link_options(
            forest-sorting-bench PRIVATE
            $<$<CONFIG:Release>:-march=native;-flto>)
    endif()
endif()

add_test(NAME forest-sorting-bench-help COMMAND forest-sorting-bench --help)
add_test(NAME forest-sorting-bench-csv
         COMMAND forest-sorting-bench
                 --format csv
                 --size 10000
                 --dataset random
                 --parent all
                 --sort adaptive-depth4-u32-chunk-msd
                 --iterations 2
                 --warmup 1
                 --baseline-parent control
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-tsv
         COMMAND forest-sorting-bench
                 --format tsv
                 --size 10000
                 --dataset siblings
                 --parent control
                 --sort all
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-json
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output summary
                 --size 10000
                 --dataset same-high64
                 --parent all
                 --sort adaptive-depth4-u32-chunk-msd
                 --iterations 2
                 --warmup 1
                 --baseline-parent control
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234
                 --data-seed 0x5eed1235)
set_tests_properties(forest-sorting-bench-json
                     PROPERTIES FAIL_REGULAR_EXPRESSION "samples_ms")
add_test(NAME forest-sorting-bench-json-raw
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output raw
                 --size 10000
                 --dataset random
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
set_tests_properties(forest-sorting-bench-json-raw
                     PROPERTIES PASS_REGULAR_EXPRESSION "sort_samples_ms")
add_test(NAME forest-sorting-bench-json-none
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output none
                 --size 10000
                 --dataset random
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
set_tests_properties(forest-sorting-bench-json-none
                     PROPERTIES FAIL_REGULAR_EXPRESSION
                                "samples_ms|\"parent\": \\{")
add_test(NAME forest-sorting-bench-default-sorts
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --iterations 1
                 --warmup 0
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-fixed-prefix-sorts
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --sort depth-bucket-depth2-lsd
                 --sort depth-bucket-depth2-chunk-msd
                 --sort adaptive-depth2-u64-chunk-msd
                 --sort adaptive-depth2-u8-chunk-msd
                 --sort adaptive-depth2-u32-chunk-msd
                 --sort adaptive-depth2-u64-chunk-msd-binary-small
                 --iterations 2
                 --warmup 1
                 --baseline-sort adaptive-depth2-u32-chunk-msd
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-composite-byte-msd-ab
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --sort composite-depth2-byte-msd-copyback
                 --sort composite-depth2-byte-msd-lowcopy-branchy
                 --sort composite-depth2-byte-msd-lowcopy-flattened
                 --sort composite-depth2-byte-msd-lowcopy-batched
                 --baseline-sort composite-depth2-byte-msd-copyback
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-touched-bitmask-ab
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd-full-clear
                 --sort adaptive-depth2-u32-chunk-msd
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-128
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-1024
                 --baseline-sort adaptive-depth2-u32-chunk-msd-full-clear
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-u32-tail-tuning
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high32
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd
                 --sort adaptive-depth2-u32-chunk-msd-binary-small32
                 --sort adaptive-depth2-u32-chunk-msd-exponential-small32
                 --baseline-sort adaptive-depth2-u32-chunk-msd
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-u32-touched-threshold-tuning
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high32
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-128
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-256
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-1024
                 --sort adaptive-depth2-u32-chunk-msd-touched-bitmask-4096
                 --baseline-sort adaptive-depth2-u32-chunk-msd
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
