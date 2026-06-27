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

add_test(NAME bench.forest.help COMMAND forest-sorting-bench --help)
add_test(NAME bench.forest.output.csv
         COMMAND forest-sorting-bench
                 --format csv
                 --size 10000
                 --dataset random
                 --parent default
                 --sort depth4-first-then-id-msd-chunk32-full-clear
                 --iterations 2
                 --warmup 1
                 --baseline-parent radix-join-id-msd-chunk32
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME bench.forest.output.tsv
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
add_test(NAME bench.forest.output.json
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output summary
                 --size 10000
                 --dataset same-high64
                 --parent default
                 --sort depth4-first-then-id-msd-chunk32-full-clear
                 --iterations 2
                 --warmup 1
                 --baseline-parent radix-join-id-msd-chunk32
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234
                 --data-seed 0x5eed1235)
set_tests_properties(bench.forest.output.json
                      PROPERTIES FAIL_REGULAR_EXPRESSION "samples_ms")
add_test(NAME bench.forest.output.json-raw
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output raw
                 --size 10000
                 --dataset random
                 --parent control
                  --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                  --iterations 1
                  --warmup 0
                  --order-seed 0x5eed
                  --data-seed 0x5eed1234)
set_tests_properties(bench.forest.output.json-raw
                      PROPERTIES PASS_REGULAR_EXPRESSION "sort_samples_ms")
add_test(NAME bench.forest.output.json-none
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output none
                 --size 10000
                 --dataset random
                 --parent control
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
set_tests_properties(bench.forest.output.json-none
                      PROPERTIES FAIL_REGULAR_EXPRESSION
                                 "samples_ms|\"parent\": \\{")
add_test(NAME bench.forest.defaults
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --iterations 1
                 --warmup 0
                 --data-seed 0x5eed1234)
add_test(NAME bench.forest.sort-family-smoke
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --sort dense-depth2-buckets-then-id-lsd
                 --sort dense-depth2-buckets-then-id-msd-chunk64-full-clear
                 --sort depth2-first-then-id-msd-chunk64-full-clear
                 --sort depth2-first-then-id-msd-chunk8-full-clear
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --iterations 2
                 --warmup 1
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME bench.forest.bitmask-le512-vs-full-clear
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent control
                 --sort depth2-first-then-id-msd-chunk32-full-clear
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --baseline-sort depth2-first-then-id-msd-chunk32-full-clear
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME bench.forest.range-ladder-full-clear
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --sort depth2-first-then-id-msd-chunk32-full-clear
                 --sort depth2-first-then-id-msd-chunk16-full-clear
                 --sort depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise-full-clear
                 --sort depth2-first-then-id-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-otherwise-full-clear
                 --sort depth2-first-then-id-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-otherwise-full-clear
                 --baseline-sort depth2-first-then-id-msd-chunk32-full-clear
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
add_test(NAME bench.forest.range-ladder-bitmask-le512
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset random
                 --parent control
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --sort depth2-first-then-id-msd-chunk16-bitmask-le512
                 --sort depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise-bitmask-le512
                 --sort depth2-first-then-id-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-otherwise-bitmask-le512
                 --sort depth2-first-then-id-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-otherwise-bitmask-le512
                 --baseline-sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)

add_test(NAME bench.forest.parent-radix-chunk-widths
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent radix-join-id-msd-chunk8
                 --parent radix-join-id-msd-chunk16
                 --parent radix-join-id-msd-chunk32
                 --parent radix-join-id-msd-chunk64
                 --parent radix-join-id-msd-byte-partition-core
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --baseline-parent radix-join-id-msd-chunk32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)

add_test(NAME bench.forest.parent-radix-directory
         COMMAND forest-sorting-bench
                 --size 10000
                 --dataset same-high64
                 --parent radix-join-id-msd-chunk32
                 --parent radix-directory-id-msd-chunk32-prefix8
                 --parent radix-directory-id-msd-chunk32-prefix16
                 --sort global-id-permutation-then-depth-stable
                 --baseline-parent radix-join-id-msd-chunk32
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)

add_test(NAME bench.forest.global-id-first
         COMMAND forest-sorting-bench
                 --format json
                 --sample-output raw
                 --size 10000
                 --dataset random
                 --parent control
                 --parent radix-join-id-msd-chunk32
                 --sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --sort global-id-permutation-then-depth-stable
                 --baseline-parent control
                 --baseline-sort depth2-first-then-id-msd-chunk32-bitmask-le512
                 --iterations 1
                 --warmup 0
                 --order-seed 0x5eed
                 --data-seed 0x5eed1234)
set_tests_properties(
    bench.forest.global-id-first
    PROPERTIES PASS_REGULAR_EXPRESSION
               "pipeline_comparison_status.*pipeline_samples_ms")

add_test(NAME bench.tail.help COMMAND forest-sorting-tail-bench --help)
add_test(NAME bench.tail.matrix-smoke
         COMMAND forest-sorting-tail-bench
                 --iterations 2
                 --warmup 1
                 --ranges 10
                 --size 4
                 --size 8
                 --pattern random)

add_test(NAME bench.tail.output.csv
         COMMAND forest-sorting-tail-bench
                 --format csv
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(bench.tail.output.csv
                     PROPERTIES PASS_REGULAR_EXPRESSION
                                "pattern,size,algorithm,median_ns")

add_test(NAME bench.tail.output.tsv
         COMMAND forest-sorting-tail-bench
                 --format tsv
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(bench.tail.output.tsv
                     PROPERTIES PASS_REGULAR_EXPRESSION "median_ns")

add_test(NAME bench.tail.output.json
         COMMAND forest-sorting-tail-bench
                 --format json
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --size 4
                 --pattern random)
set_tests_properties(bench.tail.output.json
                     PROPERTIES PASS_REGULAR_EXPRESSION "\"results\"")

add_test(NAME bench.tail.policy-smoke
         COMMAND forest-sorting-tail-bench
                 --sort linear
                 --sort binary
                 --sort exponential
                 --sort branchless-bitwise
                 --size 32
                 --baseline-sort linear
                 --iterations 1
                 --warmup 0
                 --ranges 2
                 --pattern random)
set_tests_properties(bench.tail.policy-smoke
                      PROPERTIES PASS_REGULAR_EXPRESSION "binary")

# Invalid CLI validation tests
add_test(NAME bench.forest.invalid.size
         COMMAND forest-sorting-bench --size 0)
set_tests_properties(bench.forest.invalid.size
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.iterations
         COMMAND forest-sorting-bench --iterations 0)
set_tests_properties(bench.forest.invalid.iterations
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.baseline
         COMMAND forest-sorting-bench --baseline-sort non-existent)
set_tests_properties(bench.forest.invalid.baseline
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.sort-all
         COMMAND forest-sorting-bench --sort all)
set_tests_properties(bench.forest.invalid.sort-all
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.parent-all
         COMMAND forest-sorting-bench --parent all)
set_tests_properties(bench.forest.invalid.parent-all
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.legacy-parent-radix
         COMMAND forest-sorting-bench --parent radix)
set_tests_properties(bench.forest.invalid.legacy-parent-radix
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.legacy-parent-byte-msd
         COMMAND forest-sorting-bench --parent radix-byte-msd)
set_tests_properties(bench.forest.invalid.legacy-parent-byte-msd
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.forest.invalid.legacy-parent-join-byte-msd
         COMMAND forest-sorting-bench --parent radix-join-id-byte-msd)
set_tests_properties(bench.forest.invalid.legacy-parent-join-byte-msd
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.tail.invalid.size
         COMMAND forest-sorting-tail-bench --size 0)
set_tests_properties(bench.tail.invalid.size
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.tail.invalid.ranges
         COMMAND forest-sorting-tail-bench --ranges 0)
set_tests_properties(bench.tail.invalid.ranges
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME bench.tail.invalid.baseline
         COMMAND forest-sorting-tail-bench --baseline-sort non-existent)
set_tests_properties(bench.tail.invalid.baseline
                     PROPERTIES WILL_FAIL TRUE)
