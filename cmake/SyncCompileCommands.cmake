if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED BINARY_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT and BINARY_ROOT are required")
endif()

set(root_compile_commands "${SOURCE_ROOT}/compile_commands.json")
set(build_compile_commands "${BINARY_ROOT}/compile_commands.json")

if(EXISTS "${root_compile_commands}" AND NOT IS_SYMLINK
                                         "${root_compile_commands}")
    message(
        FATAL_ERROR "refusing to replace non-symlink ${root_compile_commands}")
endif()

file(RELATIVE_PATH link_target "${SOURCE_ROOT}" "${build_compile_commands}")
if(IS_SYMLINK "${root_compile_commands}")
    file(READ_SYMLINK "${root_compile_commands}" current_target)
    if(current_target STREQUAL link_target)
        return()
    endif()
    file(REMOVE "${root_compile_commands}")
endif()

file(CREATE_LINK "${link_target}" "${root_compile_commands}" SYMBOLIC
     RESULT link_result)
if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "failed to link compile_commands.json: ${link_result}")
endif()
