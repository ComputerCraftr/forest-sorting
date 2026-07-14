cmake_minimum_required(VERSION 3.24)

project(
    forest-sorting
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(
    FOREST_CLANG_TIDY
    NAMES clang-tidy
    HINTS "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin"
    DOC "Path to clang-tidy")

# Common compiler options
function(forest_sorting_apply_common_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE -Wall
                -Wextra
                -pedantic
                -Wcast-align
                -Wcast-qual
                -Wconversion
                -Wdouble-promotion
                -Wfloat-equal
                -Wformat=2
                -Wimplicit-fallthrough
                -Wpointer-arith
                -Wsign-conversion
                -Wunused-but-set-parameter
                -Wunused-but-set-variable)

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
        target_compile_options(
            ${target}
            PRIVATE -Werror
                    -Wnewline-eof
                    -Wnull-dereference
                    -Wshadow-all
                    -Wswitch-enum
                    -Wundef
                    -Wunreachable-code
                    -Wunreachable-code-break
                    -Wunreachable-code-return
                    -Wunused-const-variable
                    -Wunused-function
                    -Wunused-lambda-capture
                    -Wunused-local-typedef
                    -Wunused-parameter
                    -Wunused-private-field
                    -Wunused-value
                    -Wunused-variable
                    -Wunneeded-internal-declaration
                    -Wvla)
    endif()

    target_compile_options(
        ${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                          $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
    target_link_options(${target} PRIVATE
                        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endfunction()

enable_testing()

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)

if(FOREST_CLANG_TIDY)
    file(GLOB_RECURSE _forest_tidy_headers CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/include/forest_sorting/*.hpp")
    file(GLOB_RECURSE _forest_tidy_support_headers CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/*.hpp")

    set(_forest_tidy_files
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/forest_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_support_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_api_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/portable_include_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/forest_bench.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/tail_microbench.cpp"
        ${_forest_tidy_headers}
        ${_forest_tidy_support_headers})
    set(_forest_tidy_extra_args)
    if(APPLE)
        if(CMAKE_OSX_SYSROOT)
            set(_forest_sysroot "${CMAKE_OSX_SYSROOT}")
        else()
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE _forest_sysroot
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        list(APPEND _forest_tidy_extra_args
             "--extra-arg=-isysroot${_forest_sysroot}")
    endif()

    add_custom_target(
        tidy
        COMMAND
            "${FOREST_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}"
            --extra-arg-before=-xc++ ${_forest_tidy_extra_args}
            ${_forest_tidy_files}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        VERBATIM)
else()
    add_custom_target(
        tidy
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "clang-tidy not found; set FOREST_CLANG_TIDY or put clang-tidy on PATH"
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
endif()
