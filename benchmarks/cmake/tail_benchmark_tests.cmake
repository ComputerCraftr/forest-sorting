add_test(NAME bench.tail.help COMMAND forest-sorting-tail-bench --help)
add_test(NAME bench.tail.matrix-smoke
         COMMAND forest-sorting-tail-bench --iterations 2 --warmup 1 --ranges
                 10 --size 4 --size 8 --pattern random)

add_test(NAME bench.tail.output.csv
         COMMAND forest-sorting-tail-bench --format csv --iterations 1 --warmup
                 0 --ranges 2 --size 4 --pattern random)
set_tests_properties(
    bench.tail.output.csv PROPERTIES PASS_REGULAR_EXPRESSION
                                     "workload,pattern,source_size,size")

add_test(NAME bench.tail.output.tsv
         COMMAND forest-sorting-tail-bench --format tsv --iterations 1 --warmup
                 0 --ranges 2 --size 4 --pattern random)
set_tests_properties(bench.tail.output.tsv PROPERTIES PASS_REGULAR_EXPRESSION
                                                      "median_ns")

add_test(NAME bench.tail.output.json
         COMMAND forest-sorting-tail-bench --format json --iterations 1
                 --warmup 0 --ranges 2 --size 4 --pattern random)
set_tests_properties(bench.tail.output.json PROPERTIES PASS_REGULAR_EXPRESSION
                                                       "\"results\"")

add_test(
    NAME bench.tail.policy-smoke
    COMMAND
        forest-sorting-tail-bench --sort linear --sort binary --sort
        exponential --sort branchless-bitwise --sort shell-gap-10-4-1 --sort
        shell-gap-3-2-1 --sort shell-gap-16-7-3-1 --size 32 --baseline-sort
        linear --iterations 1 --warmup 0 --ranges 2 --pattern random)
set_tests_properties(bench.tail.policy-smoke PROPERTIES PASS_REGULAR_EXPRESSION
                                                        "shell-gap-16-7-3-1")

add_test(
    NAME bench.tail.captured-smoke
    COMMAND
        forest-sorting-tail-bench --workload captured-node-ids --workload
        captured-parent-queries --dataset random --source-size 100000 --sort
        linear --sort shell-gap-10-4-1 --baseline-sort linear --iterations 1
        --warmup 0 --ranges 10 --shuffle --order-seed 0x5eed --data-seed
        0x5eed1234)
set_tests_properties(
    bench.tail.captured-smoke
    PROPERTIES PASS_REGULAR_EXPRESSION
               "captured-node-ids.*shell-gap-10-4-1.*ok")
add_failing_benchmark_test(bench.tail.invalid.size forest-sorting-tail-bench
                           --size 0)

add_failing_benchmark_test(bench.tail.invalid.ranges forest-sorting-tail-bench
                           --ranges 0)

add_failing_benchmark_test(
    bench.tail.invalid.baseline forest-sorting-tail-bench --baseline-sort
    non-existent)

add_failing_benchmark_test(
    bench.tail.invalid.baseline-without-candidate forest-sorting-tail-bench
    --sort linear --baseline-sort linear)

add_failing_benchmark_test(bench.tail.invalid.trailing-iterations
                           forest-sorting-tail-bench --iterations 2runs)
