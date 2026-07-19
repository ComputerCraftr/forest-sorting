if(NOT DEFINED SOURCE_ROOT
   OR NOT DEFINED TEST_ROOT
   OR NOT DEFINED CXX_COMPILER
   OR NOT DEFINED GENERATOR)
    message(FATAL_ERROR "warning-only configure test inputs are required")
endif()

function(run_warning_only_case case_name manifest_compiler)
    set(build_root "${TEST_ROOT}/${case_name}")
    file(REMOVE_RECURSE "${build_root}")
    file(MAKE_DIRECTORY "${build_root}/warning-policy-probes")
    file(WRITE "${build_root}/warning-policy-probes/completed" "stale\n")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" --fresh -S "${SOURCE_ROOT}" -B "${build_root}" -G
            "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
            -DFOREST_SORTING_WARNING_PROBE_ONLY=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
            "-DFOREST_API_MANIFEST_CLANG=${manifest_compiler}"
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_output
        ERROR_VARIABLE configure_error)
    if(NOT configure_result EQUAL 0)
        message(
            FATAL_ERROR
                "${case_name} warning-only configure failed:\n${configure_output}\n${configure_error}"
        )
    endif()

    set(marker "${build_root}/warning-policy-probes/completed")
    if(NOT EXISTS "${marker}")
        message(
            FATAL_ERROR "${case_name} did not publish its completion marker")
    endif()
    file(READ "${marker}" marker_contents)
    if(marker_contents STREQUAL "stale\n")
        message(FATAL_ERROR "${case_name} accepted a stale completion marker")
    endif()
    if(EXISTS "${build_root}/warning-policy-probes/completed.tmp")
        message(FATAL_ERROR "${case_name} left a temporary marker behind")
    endif()
    if(EXISTS "${build_root}/CTestTestfile.cmake"
       OR EXISTS "${build_root}/benchmarks"
       OR EXISTS "${build_root}/tests"
       OR EXISTS "${build_root}/compile_commands.json")
        message(
            FATAL_ERROR
                "${case_name} entered normal project configuration machinery")
    endif()
endfunction()

run_warning_only_case(absent-manifest "")
run_warning_only_case(invalid-manifest "${CMAKE_COMMAND}")
