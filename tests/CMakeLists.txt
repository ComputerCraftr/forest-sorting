add_executable(
    forest-sorting-tests
    forest_tests.cpp
    benchmark_support_tests.cpp
    generic_api_tests.cpp
    generic_id_parent_tests.cpp
    generic_id_radix_tests.cpp
    parent_index_tests.cpp
    radix_ladder_tests.cpp
    radix_pathological_tests.cpp
    tail_microbenchmark_tests.cpp)

target_link_libraries(forest-sorting-tests PRIVATE forest-sorting-lib)
target_include_directories(
    forest-sorting-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
                                 "${CMAKE_SOURCE_DIR}/benchmarks/support")
forest_sorting_apply_common_options(forest-sorting-tests)

add_executable(forest-sorting-portable-include-tests portable_include_tests.cpp)

target_link_libraries(forest-sorting-portable-include-tests
                      PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-portable-include-tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
forest_sorting_apply_common_options(forest-sorting-portable-include-tests)

add_executable(forest-sorting-hash-support-tests hash_support_tests.cpp)
target_link_libraries(forest-sorting-hash-support-tests
                      PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-hash-support-tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
forest_sorting_apply_common_options(forest-sorting-hash-support-tests)

add_test(NAME forest-sorting-tests COMMAND forest-sorting-tests)
add_test(NAME forest-sorting-portable-include-tests
         COMMAND forest-sorting-portable-include-tests)
add_test(NAME forest-sorting-hash-support-tests
         COMMAND forest-sorting-hash-support-tests)

add_test(
    NAME source.file-line-limit
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DMAX_SOURCE_LINES=1000 -P
        "${CMAKE_SOURCE_DIR}/cmake/CheckSourceLineLimit.cmake")
add_test(
    NAME source.file-line-limit-self-test
    COMMAND
        "${CMAKE_COMMAND}"
        -DCHECKER=${CMAKE_SOURCE_DIR}/cmake/CheckSourceLineLimit.cmake
        -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/line-limit-test -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/source_line_limit_test.cmake")
add_test(
    NAME build.compile-commands-sync
    COMMAND
        "${CMAKE_COMMAND}"
        -DSYNC_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/SyncCompileCommands.cmake
        -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/compile-commands-sync-test
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_commands_sync_test.cmake")
