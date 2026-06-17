add_executable(forest-sorting-tests
    forest_tests.cpp
    benchmark_support_tests.cpp
    generic_api_tests.cpp
)

target_link_libraries(forest-sorting-tests PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
forest_sorting_apply_common_options(forest-sorting-tests)

add_executable(forest-sorting-portable-include-tests
    portable_include_tests.cpp
)

target_link_libraries(forest-sorting-portable-include-tests
    PRIVATE forest-sorting-lib
)
target_include_directories(forest-sorting-portable-include-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
forest_sorting_apply_common_options(forest-sorting-portable-include-tests)

add_test(NAME forest-sorting-tests COMMAND forest-sorting-tests)
add_test(NAME forest-sorting-portable-include-tests
    COMMAND forest-sorting-portable-include-tests
)
