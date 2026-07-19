add_test(NAME bench.tail.help COMMAND forest-sorting-tail-bench --help)
add_test(NAME bench.tail.matrix-smoke
         COMMAND forest-sorting-tail-bench --iterations 2 --warmup 1
                 --tail-count 10 --tail-size 4 --tail-size 8 --pattern random)

add_test(NAME bench.tail.output.csv
         COMMAND forest-sorting-tail-bench --format csv --iterations 1 --warmup
                 0 --tail-count 2 --tail-size 4 --pattern random)
set_tests_properties(
    bench.tail.output.csv PROPERTIES PASS_REGULAR_EXPRESSION
                                     "workload,pattern,source_size,tail_size")

add_test(NAME bench.tail.output.tsv
         COMMAND forest-sorting-tail-bench --format tsv --iterations 1 --warmup
                 0 --tail-count 2 --tail-size 4 --pattern random)
set_tests_properties(bench.tail.output.tsv PROPERTIES PASS_REGULAR_EXPRESSION
                                                      "median_ns")

add_test(NAME bench.tail.output.json
         COMMAND forest-sorting-tail-bench --format json --iterations 1
                 --warmup 0 --tail-count 2 --tail-size 4 --pattern random)
set_tests_properties(bench.tail.output.json PROPERTIES PASS_REGULAR_EXPRESSION
                                                       "\"results\"")

add_test(
    NAME bench.tail.policy-smoke
    COMMAND
        forest-sorting-tail-bench --algorithm linear --algorithm binary
        --algorithm exponential --algorithm branchless-bitwise --algorithm
        shell-gap-10-4-1 --algorithm shell-gap-3-2-1 --algorithm
        shell-gap-16-7-3-1 --tail-size 32 --baseline-algorithm linear
        --iterations 1 --warmup 0 --tail-count 2 --pattern random)
set_tests_properties(bench.tail.policy-smoke PROPERTIES PASS_REGULAR_EXPRESSION
                                                        "shell-gap-16-7-3-1")

add_test(
    NAME bench.tail.captured-smoke
    COMMAND
        forest-sorting-tail-bench --workload captured-node-ids --workload
        captured-parent-queries --dataset all --source-size 100000 --algorithm
        linear --algorithm shell-gap-10-4-1 --baseline-algorithm linear
        --iterations 1 --warmup 0 --tail-count 10 --shuffle --order-seed 0x5eed
        --data-seed 0x5eed1234)
set_tests_properties(
    bench.tail.captured-smoke
    PROPERTIES PASS_REGULAR_EXPRESSION
               "captured-node-ids.*shell-gap-10-4-1.*ok")
add_failing_benchmark_test(bench.tail.invalid.tail-size
                           forest-sorting-tail-bench --tail-size 0)

add_failing_benchmark_test(bench.tail.invalid.tail-count
                           forest-sorting-tail-bench --tail-count 0)

add_failing_benchmark_test(
    bench.tail.invalid.baseline forest-sorting-tail-bench --baseline-algorithm
    non-existent)

add_test(
    NAME bench.tail.baseline-only
    COMMAND
        forest-sorting-tail-bench --algorithm linear --baseline-algorithm
        linear --tail-size 4 --tail-count 2 --iterations 1 --warmup 0 --pattern
        random --format json)
set_tests_properties(bench.tail.baseline-only PROPERTIES FAIL_REGULAR_EXPRESSION
                                                         "delta_pct")

foreach(legacy_option IN ITEMS --size --ranges --sort --baseline-sort --seed)
    string(REPLACE "--" "" legacy_name "${legacy_option}")
    add_failing_benchmark_test("bench.tail.invalid.legacy-${legacy_name}"
                               forest-sorting-tail-bench "${legacy_option}" 1)
endforeach()

add_failing_benchmark_test(bench.tail.invalid.trailing-iterations
                           forest-sorting-tail-bench --iterations 2runs)
