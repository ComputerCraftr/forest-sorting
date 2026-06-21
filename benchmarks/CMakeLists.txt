add_executable(forest-sorting-bench forest_bench.cpp)

target_link_libraries(forest-sorting-bench PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-bench
                           PRIVATE "${CMAKE_SOURCE_DIR}/tests/support")
forest_sorting_apply_common_options(forest-sorting-bench)

add_executable(forest-sorting-tail-bench tail_microbench.cpp)
target_link_libraries(forest-sorting-tail-bench PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-tail-bench
                           PRIVATE "${CMAKE_SOURCE_DIR}/tests/support")
forest_sorting_apply_common_options(forest-sorting-tail-bench)


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
                 --parent default
                 --sort adaptive-depth4-u32-chunk-msd-full-clear-tail-linear32
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
                 --sort default
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
                 --parent default
                 --sort adaptive-depth4-u32-chunk-msd-full-clear-tail-linear32
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
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
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
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
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
                 --sort adaptive-depth2-u64-chunk-msd-full-clear-tail-linear32
                 --sort adaptive-depth2-u8-chunk-msd-full-clear-tail-linear32
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-u64-chunk-msd-full-clear-tail-binary32
                 --iterations 2
                 --warmup 1
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-bitmask-le512-vs-full-clear
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --baseline-sort adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-range-ladder-full-clear
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32
                 --sort adaptive-depth2-u16-chunk-msd-full-clear-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le1024-u16-le16384-full-clear-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le2048-u16-le32768-full-clear-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le4096-u16-le65536-full-clear-tail-linear32
                 --baseline-sort adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-range-ladder-bitmask-le512
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-u16-chunk-msd-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le1024-u16-le16384-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le2048-u16-le32768-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-range-ladder-u8-le4096-u16-le65536-bitmask-le512-tail-linear32
                 --baseline-sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME forest-sorting-bench-tail-default
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high32
                 --parent control
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-binary32
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-exponential32
                 --baseline-sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)

add_test(NAME forest-sorting-bench-parent-radix-chunk-vs-byte
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent radix
                 --parent radix-byte-msd
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --baseline-parent radix-byte-msd
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)

add_test(NAME forest-sorting-bench-global-id-first
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output raw
                 --size 10000
                 --dataset random
                 --parent control
                 --parent radix
                 --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --sort adaptive-depth2-global-id-radix-stable-depth
                 --baseline-parent control
                 --baseline-sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
set_tests_properties(
    forest-sorting-bench-global-id-first
    PROPERTIES PASS_REGULAR_EXPRESSION
               "pipeline_comparison_status.*pipeline_samples_ms")

add_test(NAME forest-sorting-tail-bench-help COMMAND forest-sorting-tail-bench --help)
add_test(NAME forest-sorting-tail-bench-smoke
         COMMAND forest-sorting-tail-bench
                 --iterations 2
                 --warmup 1
                 --ranges 10
                 --size 4
                 --size 8
                 --pattern random)

add_test(NAME forest-sorting-tail-bench-csv
         COMMAND forest-sorting-tail-bench
                 --format csv
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(forest-sorting-tail-bench-csv
                     PROPERTIES PASS_REGULAR_EXPRESSION
                                "pattern,size,algorithm,median_ns")

add_test(NAME forest-sorting-tail-bench-tsv
         COMMAND forest-sorting-tail-bench
                 --format tsv
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(forest-sorting-tail-bench-tsv
                     PROPERTIES PASS_REGULAR_EXPRESSION "median_ns")

add_test(NAME forest-sorting-tail-bench-json
         COMMAND forest-sorting-tail-bench
                 --format json
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(forest-sorting-tail-bench-json
                     PROPERTIES PASS_REGULAR_EXPRESSION "\"results\"")

# Invalid CLI validation tests
add_test(NAME forest-sorting-bench-invalid-size
         COMMAND forest-sorting-bench --size 0)
set_tests_properties(forest-sorting-bench-invalid-size
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME forest-sorting-bench-invalid-iterations
         COMMAND forest-sorting-bench --iterations 0)
set_tests_properties(forest-sorting-bench-invalid-iterations
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME forest-sorting-bench-invalid-baseline
         COMMAND forest-sorting-bench --baseline-sort non-existent)
set_tests_properties(forest-sorting-bench-invalid-baseline
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME forest-sorting-tail-bench-invalid-size
         COMMAND forest-sorting-tail-bench --size 0)
set_tests_properties(forest-sorting-tail-bench-invalid-size
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME forest-sorting-tail-bench-invalid-ranges
         COMMAND forest-sorting-tail-bench --ranges 0)
set_tests_properties(forest-sorting-tail-bench-invalid-ranges
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME forest-sorting-tail-bench-invalid-baseline
         COMMAND forest-sorting-tail-bench --baseline-sort non-existent)
set_tests_properties(forest-sorting-tail-bench-invalid-baseline
                     PROPERTIES WILL_FAIL TRUE)
