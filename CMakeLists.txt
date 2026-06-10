cmake_minimum_required(VERSION 3.16)

project(forest-sorting VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(FOREST_CLANG_TIDY
    NAMES clang-tidy
    DOC "Path to clang-tidy"
)

add_library(forest-sorting-lib STATIC
    src/forest.cpp
)

target_include_directories(forest-sorting-lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

add_executable(forest-sorting
    src/main.cpp
)

target_link_libraries(forest-sorting PRIVATE forest-sorting-lib)

add_executable(forest-sorting-tests
    tests/forest_tests.cpp
)

target_link_libraries(forest-sorting-tests PRIVATE forest-sorting-lib)

add_executable(forest-sorting-bench
    benchmarks/forest_bench.cpp
)

target_link_libraries(forest-sorting-bench PRIVATE forest-sorting-lib)

set(_forest_targets
    forest-sorting-lib
    forest-sorting
    forest-sorting-tests
    forest-sorting-bench
)

foreach(target ${_forest_targets})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-fsanitize=address,undefined -fno-omit-frame-pointer>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    )
    target_link_options(${target} PRIVATE
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-fsanitize=address,undefined>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    )
endforeach()

enable_testing()
add_test(NAME forest-sorting-tests COMMAND forest-sorting-tests)

if(FOREST_CLANG_TIDY)
    add_custom_target(tidy
        COMMAND "${FOREST_CLANG_TIDY}"
                -p "${CMAKE_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/forest.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/forest_tests.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/forest_bench.cpp"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        VERBATIM
    )
else()
    add_custom_target(tidy
        COMMAND "${CMAKE_COMMAND}" -E echo
                "clang-tidy not found; set FOREST_CLANG_TIDY or put clang-tidy on PATH"
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM
    )
endif()
