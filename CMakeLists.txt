cmake_minimum_required(VERSION 3.24)

project(
    forest-sorting
    VERSION 0.1.0
    LANGUAGES CXX)

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

# Common compiler options
function(forest_sorting_apply_common_options target)
    add_dependencies(${target} sync-compile-commands)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE -Wall
                -Wextra
                -Werror
                -Wpedantic
                -Wcast-align
                -Wcast-qual
                -Wconversion
                -Wdouble-promotion
                -Wfloat-equal
                -Wformat=2
                -Wimplicit-fallthrough
                -Wpointer-arith
                -Wsign-conversion
                -Wswitch-enum
                -Wundef
                -Wunused-but-set-parameter
                -Wunused-but-set-variable
                -Wunused-const-variable
                -Wunused-function
                -Wunused-local-typedefs
                -Wunused-macros
                -Wunused-parameter
                -Wunused-value
                -Wunused-variable
                -Wvla
                -Wwrite-strings)

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
        target_compile_options(
            ${target}
            PRIVATE -Wnewline-eof
                    -Wnull-dereference
                    -Wshadow-all
                    -Wunreachable-code
                    -Wunreachable-code-break
                    -Wunreachable-code-return
                    -Wunused-lambda-capture
                    -Wunused-private-field
                    -Wunneeded-internal-declaration)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(
            ${target}
            PRIVATE -Wduplicated-branches
                    -Wduplicated-cond
                    -Wformat-overflow=2
                    -Wformat-truncation=2
                    -Wlogical-op
                    -Wshadow=local
                    -Wstringop-overflow=4)
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
