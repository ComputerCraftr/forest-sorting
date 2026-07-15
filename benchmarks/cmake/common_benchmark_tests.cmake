function(add_failing_benchmark_test test_name benchmark_target)
    add_test(NAME "${test_name}" COMMAND "${benchmark_target}" ${ARGN})
    set_tests_properties("${test_name}" PROPERTIES WILL_FAIL TRUE)
endfunction()
