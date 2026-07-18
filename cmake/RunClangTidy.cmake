foreach(required_variable IN ITEMS RUN_CLANG_TIDY CLANG_TIDY BUILD_ROOT
                                   SOURCE_ROOT LOG_FILE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL
                                           "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT DEFINED JOB_COUNT)
    set(JOB_COUNT 32)
endif()

string(
    RANDOM
    LENGTH 12
    ALPHABET 0123456789abcdef log_suffix)
set(temporary_log "${LOG_FILE}.tmp-${log_suffix}")
set(tidy_command
    "${RUN_CLANG_TIDY}" -p "${BUILD_ROOT}" -j "${JOB_COUNT}" -quiet
    "-clang-tidy-binary=${CLANG_TIDY}" "-header-filter=^${SOURCE_ROOT}/")
if(DEFINED EXTRA_ARG AND NOT EXTRA_ARG STREQUAL "")
    list(APPEND tidy_command "-extra-arg=${EXTRA_ARG}")
endif()
list(APPEND tidy_command .)

execute_process(
    COMMAND ${tidy_command}
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE tidy_result
    OUTPUT_FILE "${temporary_log}"
    ERROR_FILE "${temporary_log}")

file(RENAME "${temporary_log}" "${LOG_FILE}" RESULT rename_result)
if(NOT rename_result STREQUAL "0")
    message(FATAL_ERROR "failed to publish clang-tidy log: ${rename_result}")
endif()

file(STRINGS "${LOG_FILE}" tidy_diagnostics REGEX "(warning|error):")
if(tidy_diagnostics)
    list(JOIN tidy_diagnostics "\n" diagnostic_text)
    message("${diagnostic_text}")
endif()

if(NOT tidy_result STREQUAL "0")
    if(NOT tidy_diagnostics)
        file(READ "${LOG_FILE}" tidy_output)
        message("${tidy_output}")
    endif()
    message(
        FATAL_ERROR
            "run-clang-tidy failed with status ${tidy_result}; see ${LOG_FILE}")
endif()

if(tidy_diagnostics)
    message(FATAL_ERROR "clang-tidy emitted diagnostics; see ${LOG_FILE}")
endif()

message(STATUS "clang-tidy completed without diagnostics; log: ${LOG_FILE}")
