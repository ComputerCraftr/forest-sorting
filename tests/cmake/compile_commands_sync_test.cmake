if(NOT DEFINED SYNC_SCRIPT OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "SYNC_SCRIPT and TEST_DIRECTORY are required")
endif()

set(source_root "${TEST_DIRECTORY}/source")
set(debug_root "${TEST_DIRECTORY}/debug")
set(release_root "${TEST_DIRECTORY}/release")
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${source_root}" "${debug_root}" "${release_root}")
file(WRITE "${debug_root}/compile_commands.json" "debug")
file(WRITE "${release_root}/compile_commands.json" "release")

function(sync_and_require_target binary_root expected_target)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -DSOURCE_ROOT=${source_root}
            -DBINARY_ROOT=${binary_root} -P "${SYNC_SCRIPT}"
            COMMAND_ERROR_IS_FATAL ANY)
    file(REAL_PATH "${source_root}/compile_commands.json" actual_target)
    file(REAL_PATH "${expected_target}" expected_target_real)
    if(NOT actual_target STREQUAL expected_target_real)
        message(
            FATAL_ERROR
                "compile database points at ${actual_target}, expected ${expected_target_real}"
        )
    endif()
endfunction()

sync_and_require_target("${debug_root}" "${debug_root}/compile_commands.json")
sync_and_require_target("${release_root}"
                        "${release_root}/compile_commands.json")

file(REMOVE "${source_root}/compile_commands.json")
file(WRITE "${source_root}/compile_commands.json" "do not replace")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${source_root}
            -DBINARY_ROOT=${debug_root} -P "${SYNC_SCRIPT}"
    RESULT_VARIABLE regular_file_result
    OUTPUT_QUIET ERROR_QUIET)
if(regular_file_result EQUAL 0)
    message(FATAL_ERROR "sync replaced a regular compile database")
endif()
