if(NOT DEFINED CHECKER OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "CHECKER and TEST_DIRECTORY are required")
endif()

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(exact_file "${TEST_DIRECTORY}/exact.cpp")
set(over_file "${TEST_DIRECTORY}/over.cpp")
string(REPEAT "// line\n" 1000 exact_contents)
string(REPEAT "// line\n" 1001 over_contents)
file(WRITE "${exact_file}" "${exact_contents}")
file(WRITE "${over_file}" "${over_contents}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${TEST_DIRECTORY}
        -DMAX_SOURCE_LINES=1000 -DSOURCE_LINE_LIMIT_PATHS=${exact_file} -P
        "${CHECKER}"
    RESULT_VARIABLE exact_result)
if(NOT exact_result EQUAL 0)
    message(FATAL_ERROR "the line-limit checker rejected exactly 1000 lines")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${TEST_DIRECTORY}
        -DMAX_SOURCE_LINES=1000 -DSOURCE_LINE_LIMIT_PATHS=${over_file} -P
        "${CHECKER}"
    RESULT_VARIABLE over_result
    OUTPUT_QUIET ERROR_QUIET)
if(over_result EQUAL 0)
    message(FATAL_ERROR "the line-limit checker accepted 1001 lines")
endif()
