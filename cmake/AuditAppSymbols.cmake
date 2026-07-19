if(NOT DEFINED NM OR NOT EXISTS "${NM}")
    message(FATAL_ERROR "CMAKE_NM is required for the app symbol audit")
endif()
if(NOT DEFINED OBJECT_FILE OR NOT EXISTS "${OBJECT_FILE}")
    message(FATAL_ERROR "app object does not exist: ${OBJECT_FILE}")
endif()
if(NOT DEFINED ALLOWED_ENTRY)
    message(FATAL_ERROR "ALLOWED_ENTRY is required")
endif()

execute_process(
    COMMAND "${NM}" --defined-only --extern-only --demangle --format=posix
            "${OBJECT_FILE}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error)
set(symbol_format posix)

if(NOT nm_result EQUAL 0)
    if(NOT DEFINED CXXFILT OR NOT EXISTS "${CXXFILT}")
        message(
            FATAL_ERROR
                "nm requires a separate demangler, but c++filt was not found: ${nm_error}"
        )
    endif()
    execute_process(
        COMMAND "${NM}" -gU "${OBJECT_FILE}"
        COMMAND "${CXXFILT}"
        RESULT_VARIABLE nm_result
        OUTPUT_VARIABLE symbols
        ERROR_VARIABLE nm_error)
    set(symbol_format apple)
endif()

if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect app symbols: ${nm_error}")
endif()

function(parse_symbol line output_name output_type)
    if(symbol_format STREQUAL "posix")
        if(line MATCHES
           "^(.*)[ \t]+([A-Za-z?])[ \t]+[0-9A-Fa-f]+([ \t]+[0-9A-Fa-f]+)?$")
            set(${output_name}
                "${CMAKE_MATCH_1}"
                PARENT_SCOPE)
            set(${output_type}
                "${CMAKE_MATCH_2}"
                PARENT_SCOPE)
        else()
            set(${output_name}
                ""
                PARENT_SCOPE)
            set(${output_type}
                ""
                PARENT_SCOPE)
        endif()
    elseif(line MATCHES "^[0-9A-Fa-f]+[ \t]+([A-Za-z?])[ \t]+(.+)$")
        set(${output_name}
            "${CMAKE_MATCH_2}"
            PARENT_SCOPE)
        set(${output_type}
            "${CMAKE_MATCH_1}"
            PARENT_SCOPE)
    else()
        set(${output_name}
            ""
            PARENT_SCOPE)
        set(${output_type}
            ""
            PARENT_SCOPE)
    endif()
endfunction()

string(REPLACE "\n" ";" symbol_lines "${symbols}")
set(allowed_entry_count 0)
foreach(line IN LISTS symbol_lines)
    parse_symbol("${line}" symbol_name symbol_type)
    if(NOT symbol_name)
        continue()
    endif()

    # Uppercase text/data classes are defined, external, and strong. Weak and
    # COMDAT classes W/V are deliberately excluded before name comparison.
    if(NOT symbol_type MATCHES "^[ABCDGRST]$")
        continue()
    endif()
    string(REPLACE " " "" normalized_symbol "${symbol_name}")
    set(expected_symbol "${ALLOWED_ENTRY}(int,char**)")
    if(normalized_symbol STREQUAL expected_symbol)
        math(EXPR allowed_entry_count "${allowed_entry_count} + 1")
    endif()
endforeach()

if(NOT allowed_entry_count EQUAL 1)
    message(
        FATAL_ERROR
            "expected exactly one emitted ${ALLOWED_ENTRY}(int, char**) in ${OBJECT_FILE}, found ${allowed_entry_count}"
    )
endif()
