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
