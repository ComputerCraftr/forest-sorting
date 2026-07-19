if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED MAX_SOURCE_LINES)
    set(MAX_SOURCE_LINES 1000)
endif()

if(DEFINED SOURCE_LINE_LIMIT_PATHS)
    set(source_files ${SOURCE_LINE_LIMIT_PATHS})
else()
    set(source_files "${SOURCE_ROOT}/CMakeLists.txt")
    foreach(root IN ITEMS include src tests benchmarks cmake)
        file(
            GLOB_RECURSE root_files
            LIST_DIRECTORIES FALSE
            "${SOURCE_ROOT}/${root}/*.c"
            "${SOURCE_ROOT}/${root}/*.cc"
            "${SOURCE_ROOT}/${root}/*.cpp"
            "${SOURCE_ROOT}/${root}/*.cxx"
            "${SOURCE_ROOT}/${root}/*.h"
            "${SOURCE_ROOT}/${root}/*.hpp"
            "${SOURCE_ROOT}/${root}/*.hxx"
            "${SOURCE_ROOT}/${root}/*.py"
            "${SOURCE_ROOT}/${root}/*.cmake"
            "${SOURCE_ROOT}/${root}/CMakeLists.txt")
        list(APPEND source_files ${root_files})
    endforeach()
endif()

list(REMOVE_DUPLICATES source_files)
list(SORT source_files)
set(offenders)
foreach(path IN LISTS source_files)
    if(path MATCHES "/(third_party|vendor|generated)/")
        continue()
    endif()
    file(READ "${path}" contents)
    string(REGEX MATCHALL "\n" newlines "${contents}")
    list(LENGTH newlines line_count)
    if(NOT contents STREQUAL "" AND NOT contents MATCHES "\n$")
        math(EXPR line_count "${line_count} + 1")
    endif()
    if(line_count GREATER MAX_SOURCE_LINES)
        file(RELATIVE_PATH relative_path "${SOURCE_ROOT}" "${path}")
        list(APPEND offenders "${relative_path}: ${line_count} lines")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " offender_text)
    message(
        FATAL_ERROR
            "source files exceed ${MAX_SOURCE_LINES} lines:\n  ${offender_text}"
    )
endif()
