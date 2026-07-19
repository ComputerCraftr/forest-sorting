add_library(forest-sorting-benchmark-common INTERFACE)
target_link_libraries(forest-sorting-benchmark-common
                      INTERFACE forest-sorting-lib)
target_include_directories(
    forest-sorting-benchmark-common
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support/common/include")

add_library(forest-sorting-benchmark-tail INTERFACE)
target_link_libraries(forest-sorting-benchmark-tail
                      INTERFACE forest-sorting-benchmark-common)
target_include_directories(
    forest-sorting-benchmark-tail
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support/tail/include")

add_library(forest-sorting-benchmark-full STATIC
            support/full/parent_registry.cpp support/full/sort_registry.cpp)
target_link_libraries(
    forest-sorting-benchmark-full PUBLIC forest-sorting-benchmark-common
                                         forest-sorting-benchmark-tail)
target_include_directories(
    forest-sorting-benchmark-full
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support/full/include")
forest_sorting_apply_common_options(forest-sorting-benchmark-full)

add_library(forest-sorting-full-app-object OBJECT
            support/full/forest_benchmark_app.cpp)
target_link_libraries(forest-sorting-full-app-object
                      PRIVATE forest-sorting-benchmark-full)
forest_sorting_apply_common_options(forest-sorting-full-app-object)

add_executable(
    forest-sorting-bench
    forest_bench.cpp support/full/forest_benchmark_options.cpp
    $<TARGET_OBJECTS:forest-sorting-full-app-object>)
target_link_libraries(forest-sorting-bench
                      PRIVATE forest-sorting-benchmark-full)
forest_sorting_apply_common_options(forest-sorting-bench)

add_library(forest-sorting-tail-app-object OBJECT
            support/tail/tail_benchmark_app.cpp)
target_link_libraries(forest-sorting-tail-app-object
                      PRIVATE forest-sorting-benchmark-tail)
forest_sorting_apply_common_options(forest-sorting-tail-app-object)

add_executable(
    forest-sorting-tail-bench tail_microbench.cpp
                              $<TARGET_OBJECTS:forest-sorting-tail-app-object>)
target_link_libraries(forest-sorting-tail-bench
                      PRIVATE forest-sorting-benchmark-tail)
forest_sorting_apply_common_options(forest-sorting-tail-bench)

option(FOREST_SORTING_NATIVE_BENCH "Use -march=native/-flto for benchmarks" OFF)

if(NOT MSVC)
    if(FOREST_SORTING_NATIVE_BENCH)
        target_compile_options(forest-sorting-bench
                               PRIVATE $<$<CONFIG:Release>:-march=native;-flto>)
        target_link_options(forest-sorting-bench PRIVATE
                            $<$<CONFIG:Release>:-march=native;-flto>)
    endif()
endif()

include(cmake/common_benchmark_tests.cmake)
include(cmake/full_benchmark_tests.cmake)
include(cmake/tail_benchmark_tests.cmake)

find_program(FOREST_CXXFILT NAMES llvm-cxxfilt c++filt)
add_library(forest-sorting-app-symbol-negative-object OBJECT
            "${CMAKE_SOURCE_DIR}/tests/cmake/app_symbol_negative_fixture.cpp")
target_compile_features(forest-sorting-app-symbol-negative-object
                        PRIVATE cxx_std_20)
add_library(forest-sorting-app-symbol-positive-object OBJECT
            "${CMAKE_SOURCE_DIR}/tests/cmake/app_symbol_positive_fixture.cpp")
target_include_directories(forest-sorting-app-symbol-positive-object
                           PRIVATE "${CMAKE_SOURCE_DIR}/tests/cmake")
target_compile_features(forest-sorting-app-symbol-positive-object
                        PRIVATE cxx_std_20)
add_executable(
    forest-sorting-app-symbol-positive
    "${CMAKE_SOURCE_DIR}/tests/cmake/app_symbol_positive_main.cpp"
    $<TARGET_OBJECTS:forest-sorting-app-symbol-positive-object>)
target_compile_features(forest-sorting-app-symbol-positive PRIVATE cxx_std_20)

if(PROJECT_IS_TOP_LEVEL)
    set(_forest_app_audit_script
        "${CMAKE_SOURCE_DIR}/tests/tools/app_linkage_audit.py")
    set(_forest_app_audit_directory "${CMAKE_BINARY_DIR}/audit")
    add_test(
        NAME app-linkage.full
        COMMAND
            "${Python3_EXECUTABLE}" "${_forest_app_audit_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${CMAKE_CURRENT_SOURCE_DIR}/support/full/forest_benchmark_app.cpp"
            --object "$<TARGET_OBJECTS:forest-sorting-full-app-object>"
            --runner forest_sorting::benchmark_app::full::runForestBenchmark
            --report "${_forest_app_audit_directory}/full.json")
    add_test(
        NAME app-linkage.tail
        COMMAND
            "${Python3_EXECUTABLE}" "${_forest_app_audit_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${CMAKE_CURRENT_SOURCE_DIR}/support/tail/tail_benchmark_app.cpp"
            --object "$<TARGET_OBJECTS:forest-sorting-tail-app-object>"
            --runner forest_sorting::benchmark_app::tail::runTailBenchmark
            --report "${_forest_app_audit_directory}/tail.json")
    add_test(
        NAME app-linkage.positive-header-emission
        COMMAND
            "${Python3_EXECUTABLE}" "${_forest_app_audit_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${CMAKE_SOURCE_DIR}/tests/cmake/app_symbol_positive_fixture.cpp"
            --object
            "$<TARGET_OBJECTS:forest-sorting-app-symbol-positive-object>"
            --runner forest_sorting::benchmark_app::fixture::runFixture
            --report "${_forest_app_audit_directory}/positive.json")
    add_test(
        NAME app-linkage.negative
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/tools/app_linkage_negative_test.py"
            --python "${Python3_EXECUTABLE}" --audit-script
            "${_forest_app_audit_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${CMAKE_SOURCE_DIR}/tests/cmake/app_symbol_negative_fixture.cpp"
            --object
            "$<TARGET_OBJECTS:forest-sorting-app-symbol-negative-object>"
            --report "${_forest_app_audit_directory}/negative.json")
endif()
add_test(
    NAME app-symbols.full
    COMMAND
        "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DCXXFILT=${FOREST_CXXFILT}"
        "-DOBJECT_FILE=$<TARGET_OBJECTS:forest-sorting-full-app-object>"
        "-DALLOWED_ENTRY=forest_sorting::benchmark_app::full::runForestBenchmark"
        -P "${CMAKE_SOURCE_DIR}/cmake/AuditAppSymbols.cmake")
add_test(
    NAME app-symbols.tail
    COMMAND
        "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DCXXFILT=${FOREST_CXXFILT}"
        "-DOBJECT_FILE=$<TARGET_OBJECTS:forest-sorting-tail-app-object>"
        "-DALLOWED_ENTRY=forest_sorting::benchmark_app::tail::runTailBenchmark"
        -P "${CMAKE_SOURCE_DIR}/cmake/AuditAppSymbols.cmake")
add_test(
    NAME app-symbols.positive-header-emission
    COMMAND
        "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DCXXFILT=${FOREST_CXXFILT}"
        "-DOBJECT_FILE=$<TARGET_OBJECTS:forest-sorting-app-symbol-positive-object>"
        "-DALLOWED_ENTRY=forest_sorting::benchmark_app::fixture::runFixture" -P
        "${CMAKE_SOURCE_DIR}/cmake/AuditAppSymbols.cmake")
