add_test(NAME bench.forest.help COMMAND forest-sorting-bench --help)
add_test(
    NAME bench.forest.output.csv
    COMMAND
        forest-sorting-bench --format csv --size 10000 --dataset random
        --parent default --sort depth4-first-then-id-msd-chunk32-full-clear
        --iterations 2 --warmup 1 --order-seed 0x5eed --data-seed 0x5eed1234)
add_test(
    NAME bench.forest.output.tsv
    COMMAND
        forest-sorting-bench --format tsv --size 10000 --dataset siblings
        --parent control --sort default --iterations 1 --warmup 0 --order-seed
        0x5eed --data-seed 0x5eed1234)
add_test(
    NAME bench.forest.output.json
    COMMAND
        forest-sorting-bench --format json --sample-output summary --size 10000
        --dataset same-high64 --parent default --sort
        depth4-first-then-id-msd-chunk32-full-clear --iterations 2 --warmup 1
        --order-seed 0x5eed --data-seed 0x5eed1234 --data-seed 0x5eed1235)
set_tests_properties(bench.forest.output.json PROPERTIES FAIL_REGULAR_EXPRESSION
                                                         "samples_ms")
add_test(
    NAME bench.forest.output.json-raw
    COMMAND
        forest-sorting-bench --format json --sample-output raw --size 10000
        --dataset random --parent control --sort
        depth2-first-then-id-msd-chunk32-bitmask-le512 --iterations 1 --warmup
        0 --order-seed 0x5eed --data-seed 0x5eed1234)
set_tests_properties(bench.forest.output.json-raw
                     PROPERTIES PASS_REGULAR_EXPRESSION "sort_samples_ms")
add_test(
    NAME bench.forest.output.json-none
    COMMAND
        forest-sorting-bench --format json --sample-output none --size 10000
        --dataset random --parent control --sort
        depth2-first-then-id-msd-chunk32-bitmask-le512 --iterations 1 --warmup
        0 --order-seed 0x5eed --data-seed 0x5eed1234)
set_tests_properties(
    bench.forest.output.json-none PROPERTIES FAIL_REGULAR_EXPRESSION
                                             "samples_ms|\"parent\": \\{")
add_test(NAME bench.forest.defaults
         COMMAND forest-sorting-bench --size 10000 --dataset random --parent
                 control --iterations 1 --warmup 0 --data-seed 0x5eed1234)
add_test(
    NAME bench.forest.dataset-cardinality-smoke
    COMMAND
        forest-sorting-bench --size 100 --dataset all --parent
        radix-join-id-msd-chunk32 --sort
        global-id-permutation-then-depth-stable --baseline-parent
        radix-join-id-msd-chunk32 --baseline-sort
        global-id-permutation-then-depth-stable --iterations 1 --warmup 0
        --data-seed 1 --shuffle --order-seed 0x5eed)
add_test(
    NAME bench.forest.sort-family-smoke
    COMMAND
        forest-sorting-bench --size 10000 --dataset random --sort
        dense-depth2-buckets-then-id-lsd --sort
        dense-depth2-buckets-then-id-msd-chunk64-full-clear --sort
        depth2-first-then-id-msd-chunk64-full-clear --sort
        depth2-first-then-id-msd-chunk8-full-clear --sort
        depth2-first-then-id-msd-chunk32-bitmask-le512 --iterations 2 --warmup
        1 --order-seed 0x5eed --data-seed 0x5eed1234)
add_test(
    NAME bench.forest.bitmask-le512-vs-full-clear
    COMMAND
        forest-sorting-bench --size 10000 --dataset same-high64 --parent
        control --sort depth2-first-then-id-msd-chunk32-full-clear --sort
        depth2-first-then-id-msd-chunk32-bitmask-le512 --baseline-sort
        depth2-first-then-id-msd-chunk32-full-clear --iterations 1 --warmup 0
        --order-seed 0x5eed --data-seed 0x5eed1234)
add_test(
    NAME bench.forest.parent-radix-chunk-widths
    COMMAND
        forest-sorting-bench --size 10000 --dataset same-high64 --parent
        radix-join-id-msd-chunk8 --parent radix-join-id-msd-chunk16 --parent
        radix-join-id-msd-chunk32 --parent radix-join-id-msd-chunk64 --parent
        radix-join-id-msd-byte-partition-core --sort
        depth2-first-then-id-msd-chunk32-bitmask-le512 --baseline-parent
        radix-join-id-msd-chunk32 --iterations 1 --warmup 0 --order-seed 0x5eed
        --data-seed 0x5eed1234)

add_test(
    NAME bench.forest.parent-radix-directory
    COMMAND
        forest-sorting-bench --size 10000 --dataset same-high64 --parent
        radix-join-id-msd-chunk32 --parent
        radix-directory-id-msd-chunk32-prefix8 --parent
        radix-directory-id-msd-chunk32-prefix16 --sort
        global-id-permutation-then-depth-stable --baseline-parent
        radix-join-id-msd-chunk32 --iterations 1 --warmup 0 --order-seed 0x5eed
        --data-seed 0x5eed1234)

add_test(
    NAME bench.forest.global-id-first
    COMMAND
        forest-sorting-bench --format json --sample-output raw --size 10000
        --dataset random --parent control --parent radix-join-id-msd-chunk32
        --sort depth2-first-then-id-msd-chunk32-bitmask-le512 --sort
        global-id-permutation-then-depth-stable --baseline-parent control
        --baseline-sort depth2-first-then-id-msd-chunk32-bitmask-le512
        --iterations 1 --warmup 0 --order-seed 0x5eed --data-seed 0x5eed1234)
set_tests_properties(
    bench.forest.global-id-first
    PROPERTIES PASS_REGULAR_EXPRESSION
               "pipeline_comparison_status.*pipeline_samples_ms")

add_test(
    NAME bench.forest.radix-ladder-smoke
    COMMAND
        forest-sorting-bench --size 10000 --dataset random --parent
        radix-join-id-msd-chunk32 --parent
        radix-join-id-msd-size-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise
        --sort depth2-first-then-id-msd-chunk32-full-clear --sort
        depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise-full-clear
        --baseline-parent radix-join-id-msd-chunk32 --baseline-sort
        depth2-first-then-id-msd-chunk32-full-clear --iterations 1 --warmup 0
        --order-seed 0x5eed --data-seed 0x5eed1234)
# Invalid CLI validation tests
add_failing_benchmark_test(bench.forest.invalid.size forest-sorting-bench
                           --size 0)

add_failing_benchmark_test(bench.forest.invalid.iterations forest-sorting-bench
                           --iterations 0)

add_failing_benchmark_test(bench.forest.invalid.baseline forest-sorting-bench
                           --baseline-sort non-existent)

add_test(
    NAME bench.forest.baseline-only
    COMMAND
        forest-sorting-bench --size 100 --dataset random --parent
        radix-join-id-msd-chunk32 --sort comparison --baseline-parent
        radix-join-id-msd-chunk32 --baseline-sort comparison --iterations 1
        --warmup 0 --data-seed 1 --format json)
set_tests_properties(
    bench.forest.baseline-only PROPERTIES PASS_REGULAR_EXPRESSION
                                          "\"pipeline_delta_median_pct\": null")

add_test(
    NAME bench.forest.parent-only-comparison
    COMMAND
        forest-sorting-bench --size 100 --dataset random --parent
        radix-join-id-msd-chunk16 --sort
        global-id-permutation-then-depth-stable --baseline-parent
        radix-join-id-msd-chunk32 --baseline-sort
        global-id-permutation-then-depth-stable --iterations 1 --warmup 0
        --data-seed 1 --format json)
set_tests_properties(
    bench.forest.parent-only-comparison
    PROPERTIES PASS_REGULAR_EXPRESSION "\"parent_comparison_status\": \"ok\"")

add_test(
    NAME bench.forest.sort-only-comparison
    COMMAND
        forest-sorting-bench --size 100 --dataset random --parent
        radix-join-id-msd-chunk32 --sort comparison --baseline-parent
        radix-join-id-msd-chunk32 --baseline-sort
        global-id-permutation-then-depth-stable --iterations 1 --warmup 0
        --data-seed 1 --format json)
set_tests_properties(
    bench.forest.sort-only-comparison
    PROPERTIES PASS_REGULAR_EXPRESSION "\"sort_comparison_status\": \"ok\"")

add_test(
    NAME bench.forest.sort-all
    COMMAND
        forest-sorting-bench --size 100 --dataset random --parent
        radix-join-id-msd-chunk32 --sort all --iterations 1 --warmup 0
        --data-seed 1)

add_test(NAME bench.forest.parent-all
         COMMAND forest-sorting-bench --size 100 --dataset random --parent all
                 --sort comparison --iterations 1 --warmup 0 --data-seed 1)

add_failing_benchmark_test(bench.forest.invalid.legacy-parent-radix
                           forest-sorting-bench --parent radix)

add_failing_benchmark_test(bench.forest.invalid.legacy-parent-byte-msd
                           forest-sorting-bench --parent radix-byte-msd)

add_failing_benchmark_test(bench.forest.invalid.legacy-parent-join-byte-msd
                           forest-sorting-bench --parent radix-join-id-byte-msd)
add_failing_benchmark_test(bench.forest.invalid.trailing-size
                           forest-sorting-bench --size 10nodes)
add_failing_benchmark_test(bench.forest.invalid.oversized-warmup
                           forest-sorting-bench --warmup 99999999999999999999)
