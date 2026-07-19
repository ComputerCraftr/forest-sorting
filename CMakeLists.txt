cmake_minimum_required(VERSION 3.24)

project(
    forest-sorting
    VERSION 0.1.0
    LANGUAGES CXX)

option(FOREST_SORTING_WARNING_PROBE_ONLY
       "Configure only the active frontend's fatal-warning policy probes" OFF)
include(cmake/ForestSortingWarnings.cmake)
if(FOREST_SORTING_WARNING_PROBE_ONLY)
    forest_sorting_run_warning_policy_probes()
    return()
endif()

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_GENERATOR MATCHES "^Ninja")
    message(
        FATAL_ERROR
            "Top-level forest-sorting builds require Ninja. Use cmake --preset debug or cmake --preset release."
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_forest_compile_commands_sync_script
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncCompileCommands.cmake")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_ROOT=${CMAKE_BINARY_DIR} -P
        "${_forest_compile_commands_sync_script}" COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(
    sync-compile-commands
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_ROOT=${CMAKE_BINARY_DIR} -P
        "${_forest_compile_commands_sync_script}"
    COMMENT "Pointing compile_commands.json at the active build tree"
    VERBATIM)

find_program(
    FOREST_CLANG_TIDY
    NAMES clang-tidy
    HINTS "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin"
    DOC "Path to clang-tidy")
find_program(
    FOREST_RUN_CLANG_TIDY
    NAMES run-clang-tidy
    HINTS "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin"
    DOC "Path to run-clang-tidy")
if(PROJECT_IS_TOP_LEVEL)
    find_program(
        FOREST_API_MANIFEST_CLANG
        NAMES clang++-22 clang++
        HINTS "/opt/homebrew/opt/llvm@22/bin" "/usr/local/opt/llvm@22/bin"
              "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin"
        DOC "Clang C++ frontend used to generate the public API AST manifest")
    if(FOREST_API_MANIFEST_CLANG)
        execute_process(
            COMMAND "${FOREST_API_MANIFEST_CLANG}" --version
            OUTPUT_VARIABLE _forest_api_manifest_clang_version
            ERROR_VARIABLE _forest_api_manifest_clang_version_error
            RESULT_VARIABLE _forest_api_manifest_clang_result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _forest_api_manifest_clang_result EQUAL 0
           OR _forest_api_manifest_clang_version MATCHES "Apple clang"
           OR NOT _forest_api_manifest_clang_version MATCHES
              "clang version 22([.]| )")
            message(
                FATAL_ERROR
                    "Public API manifests require upstream Clang 22. "
                    "FOREST_API_MANIFEST_CLANG=${FOREST_API_MANIFEST_CLANG}\n"
                    "${_forest_api_manifest_clang_version}\n"
                    "${_forest_api_manifest_clang_version_error}")
        endif()
    endif()
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED)
endif()

forest_sorting_run_warning_policy_probes()
enable_testing()

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)

if(FOREST_CLANG_TIDY AND FOREST_RUN_CLANG_TIDY)
    set(_forest_tidy_extra_arg)
    if(APPLE)
        if(CMAKE_OSX_SYSROOT)
            set(_forest_sysroot "${CMAKE_OSX_SYSROOT}")
        else()
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE _forest_sysroot
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        set(_forest_tidy_extra_arg "-isysroot${_forest_sysroot}")
    endif()

    add_custom_target(
        tidy
        COMMAND
            "${CMAKE_COMMAND}" "-DRUN_CLANG_TIDY=${FOREST_RUN_CLANG_TIDY}"
            "-DCLANG_TIDY=${FOREST_CLANG_TIDY}"
            "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DLOG_FILE=${CMAKE_BINARY_DIR}/clang-tidy.log" -DJOB_COUNT=32
            "-DEXTRA_ARG=${_forest_tidy_extra_arg}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunClangTidy.cmake"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy with atomic log capture"
        VERBATIM)
else()
    add_custom_target(
        tidy
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "clang-tidy or run-clang-tidy not found; set FOREST_CLANG_TIDY and FOREST_RUN_CLANG_TIDY or put both on PATH"
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
endif()
add_dependencies(tidy sync-compile-commands)
