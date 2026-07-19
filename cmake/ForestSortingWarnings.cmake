set(_forest_common_gnu_warning_options
    -Wall
    -Wextra
    -Wpedantic
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
set(_forest_gnu_fatal_warning_options -Werror)

set(_forest_clang_warning_options
    -Wcast-align
    -Wmissing-prototypes
    -Wnewline-eof
    -Wnull-dereference
    -Wshadow-all
    -Wunreachable-code
    -Wunreachable-code-break
    -Wunreachable-code-return
    -Wunused-lambda-capture
    -Wunused-private-field
    -Wunneeded-internal-declaration)

set(_forest_gcc_warning_options
    -Wcast-align=strict
    -Wduplicated-branches
    -Wduplicated-cond
    -Wformat-overflow=2
    -Wformat-truncation=2
    -Wlogical-op
    -Wmissing-declarations
    -Wshadow=local
    -Wstringop-overflow=4)
set(_forest_msvc_warning_options /W4 /permissive-)
set(_forest_msvc_fatal_warning_options /WX)
set(_forest_clang_cl_warning_options /clang:-Wunused-parameter)

# Common compiler options
function(forest_sorting_apply_common_options target)
    add_dependencies(${target} sync-compile-commands)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target}
                               PRIVATE ${_forest_msvc_warning_options})
        if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
            target_compile_options(${target}
                                   PRIVATE ${_forest_clang_cl_warning_options})
        endif()
        target_compile_options(${target}
                               PRIVATE ${_forest_msvc_fatal_warning_options})
        return()
    endif()

    target_compile_options(
        ${target} PRIVATE ${_forest_common_gnu_warning_options}
                          ${_forest_gnu_fatal_warning_options})

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
        target_compile_options(${target}
                               PRIVATE ${_forest_clang_warning_options})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE ${_forest_gcc_warning_options})
    endif()

    target_compile_options(
        ${target} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                          $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
    target_link_options(${target} PRIVATE
                        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endfunction()

function(forest_sorting_complete_warning_policy_probe probe_root)
    set(marker "${probe_root}/completed")
    set(temporary_marker "${probe_root}/completed.tmp")
    file(
        WRITE "${temporary_marker}"
        "compiler=${CMAKE_CXX_COMPILER_ID}\nfrontend=${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}\n"
    )
    file(RENAME "${temporary_marker}" "${marker}" RESULT rename_result)
    if(rename_result)
        message(
            FATAL_ERROR
                "failed to publish warning probe marker: ${rename_result}")
    endif()
endfunction()

function(forest_sorting_run_warning_policy_probes)
    set(_probe_root "${CMAKE_BINARY_DIR}/warning-policy-probes")
    set(_probe_marker "${_probe_root}/completed")
    set(_probe_temporary_marker "${_probe_root}/completed.tmp")
    file(MAKE_DIRECTORY "${_probe_root}")
    file(REMOVE "${_probe_marker}" "${_probe_temporary_marker}")

    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_diagnostic_options ${_forest_msvc_warning_options})
        if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
            list(APPEND _diagnostic_options ${_forest_clang_cl_warning_options})
            set(_expected_diagnostic "unused parameter|-Wunused-parameter")
        else()
            set(_expected_diagnostic "C4100")
        endif()

        file(WRITE "${_probe_root}/clean.cpp"
             "int forestWarningProbe(int value) { return value; }\n")
        file(WRITE "${_probe_root}/unused-parameter.cpp"
             "int forestWarningProbe(int unused) { return 0; }\n")
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

        try_compile(
            _clean_probe_compiles "${_probe_root}/msvc-clean-build"
            SOURCES "${_probe_root}/clean.cpp"
            COMPILE_DEFINITIONS
                ${_diagnostic_options} ${_forest_msvc_fatal_warning_options}
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _clean_probe_output)
        if(NOT _clean_probe_compiles)
            message(
                FATAL_ERROR
                    "clean MSVC-frontend warning probe failed:\n${_clean_probe_output}"
            )
        endif()

        try_compile(
            _warning_probe_compiles "${_probe_root}/msvc-warning-build"
            SOURCES "${_probe_root}/unused-parameter.cpp"
            COMPILE_DEFINITIONS ${_diagnostic_options} CXX_STANDARD 20
                                CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _warning_probe_output)
        if(NOT _warning_probe_compiles OR NOT _warning_probe_output MATCHES
                                          "${_expected_diagnostic}")
            message(
                FATAL_ERROR
                    "MSVC-frontend warning-only probe did not emit the intended warning:\n${_warning_probe_output}"
            )
        endif()

        try_compile(
            _fatal_probe_compiles "${_probe_root}/msvc-fatal-build"
            SOURCES "${_probe_root}/unused-parameter.cpp"
            COMPILE_DEFINITIONS
                ${_diagnostic_options} ${_forest_msvc_fatal_warning_options}
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _fatal_probe_output)
        if(_fatal_probe_compiles OR NOT _fatal_probe_output MATCHES
                                    "${_expected_diagnostic}")
            message(
                FATAL_ERROR
                    "MSVC-frontend fatal-warning probe did not fail for the intended warning:\n${_fatal_probe_output}"
            )
        endif()
        forest_sorting_complete_warning_policy_probe("${_probe_root}")
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
        set(_frontend_warning_options ${_forest_clang_warning_options})
        set(_expected_diagnostic "missing-prototypes|no previous prototype")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_frontend_warning_options ${_forest_gcc_warning_options})
        set(_expected_diagnostic "missing-declarations|no previous declaration")
    else()
        message(
            FATAL_ERROR
                "warning policy probes do not support ${CMAKE_CXX_COMPILER_ID} with ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} frontend"
        )
    endif()

    file(WRITE "${_probe_root}/declared.cpp"
         "int forestWarningProbe();\nint forestWarningProbe() { return 0; }\n")
    file(WRITE "${_probe_root}/undeclared.cpp"
         "int forestWarningProbe() { return 0; }\n")
    set(_probe_diagnostic_options ${_forest_common_gnu_warning_options}
                                  ${_frontend_warning_options})
    set(_probe_options ${_probe_diagnostic_options}
                       ${_forest_gnu_fatal_warning_options})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    try_compile(
        _declared_probe_compiles "${_probe_root}/declared-build"
        SOURCES "${_probe_root}/declared.cpp"
        COMPILE_DEFINITIONS ${_probe_options} CXX_STANDARD 20
                            CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE _declared_probe_output)
    if(NOT _declared_probe_compiles)
        message(
            FATAL_ERROR
                "declared-function warning-policy probe failed:\n${_declared_probe_output}"
        )
    endif()

    try_compile(
        _warning_probe_compiles "${_probe_root}/undeclared-warning-build"
        SOURCES "${_probe_root}/undeclared.cpp"
        COMPILE_DEFINITIONS ${_probe_diagnostic_options} CXX_STANDARD 20
                            CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE _warning_probe_output)
    if(NOT _warning_probe_compiles OR NOT _warning_probe_output MATCHES
                                      "${_expected_diagnostic}")
        message(
            FATAL_ERROR
                "undeclared-function warning-only probe did not emit the intended warning:\n${_warning_probe_output}"
        )
    endif()

    try_compile(
        _undeclared_probe_compiles "${_probe_root}/undeclared-build"
        SOURCES "${_probe_root}/undeclared.cpp"
        COMPILE_DEFINITIONS ${_probe_options} CXX_STANDARD 20
                            CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE _undeclared_probe_output)
    if(_undeclared_probe_compiles)
        message(
            FATAL_ERROR
                "undeclared-function warning-policy probe unexpectedly compiled"
        )
    endif()
    if(NOT _undeclared_probe_output MATCHES "${_expected_diagnostic}")
        message(
            FATAL_ERROR
                "undeclared-function probe failed for an unexpected reason:\n${_undeclared_probe_output}"
        )
    endif()
    forest_sorting_complete_warning_policy_probe("${_probe_root}")
endfunction()
