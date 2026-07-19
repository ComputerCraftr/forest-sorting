add_executable(
    forest-sorting-tests
    forest_tests.cpp
    benchmark_support_tests.cpp
    generic_api_tests.cpp
    generic_id_parent_tests.cpp
    generic_id_radix_tests.cpp
    parent_index_tests.cpp
    radix_ladder_tests.cpp
    radix_pathological_tests.cpp
    tail_microbenchmark_tests.cpp)

target_link_libraries(forest-sorting-tests
                      PRIVATE forest-sorting-lib forest-sorting-benchmark-full)
target_include_directories(forest-sorting-tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
forest_sorting_apply_common_options(forest-sorting-tests)

add_executable(forest-sorting-portable-include-tests portable_include_tests.cpp)

target_link_libraries(forest-sorting-portable-include-tests
                      PRIVATE forest-sorting-lib)
target_include_directories(forest-sorting-portable-include-tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
forest_sorting_apply_common_options(forest-sorting-portable-include-tests)

add_executable(forest-sorting-hash-support-tests hash_support_tests.cpp)
target_link_libraries(forest-sorting-hash-support-tests
                      PRIVATE forest-sorting-lib forest-sorting-benchmark-full)
target_include_directories(forest-sorting-hash-support-tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
forest_sorting_apply_common_options(forest-sorting-hash-support-tests)

add_executable(forest-sorting-options-header-link-tests
               cmake/options_header_tu_a.cpp cmake/options_header_tu_b.cpp)
target_link_libraries(forest-sorting-options-header-link-tests
                      PRIVATE forest-sorting-benchmark-full)
forest_sorting_apply_common_options(forest-sorting-options-header-link-tests)

if(PROJECT_IS_TOP_LEVEL)
    get_property(_forest_public_headers GLOBAL
                 PROPERTY FOREST_SORTING_PUBLIC_HEADERS)
    set(_forest_generic_probe_source)
    set(_forest_uint128_probe_source)
    foreach(header IN LISTS _forest_public_headers)
        file(RELATIVE_PATH relative_header "${CMAKE_SOURCE_DIR}/include"
             "${header}")
        string(
            APPEND _forest_uint128_probe_source
            "#include <${relative_header}> // NOLINT(misc-include-cleaner)\n")
        if(NOT relative_header MATCHES "^forest_sorting/uint128")
            string(
                APPEND
                _forest_generic_probe_source
                "#include <${relative_header}> // NOLINT(misc-include-cleaner)\n"
            )
        endif()
    endforeach()
    set(_forest_portable_generic_probe
        "${CMAKE_CURRENT_BINARY_DIR}/public_api_portable_generic_probe.cpp")
    set(_forest_uint128_enabled_probe
        "${CMAKE_CURRENT_BINARY_DIR}/public_api_uint128_enabled_probe.cpp")
    file(WRITE "${_forest_portable_generic_probe}"
         "${_forest_generic_probe_source}")
    file(WRITE "${_forest_uint128_enabled_probe}"
         "${_forest_uint128_probe_source}")

    add_library(forest-sorting-public-api-portable-generic-probe OBJECT
                "${_forest_portable_generic_probe}")
    target_link_libraries(forest-sorting-public-api-portable-generic-probe
                          PRIVATE forest-sorting-lib)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(forest-sorting-public-api-portable-generic-probe
                               PRIVATE /U__SIZEOF_INT128__)
    else()
        target_compile_options(forest-sorting-public-api-portable-generic-probe
                               PRIVATE -U__SIZEOF_INT128__)
    endif()

    add_library(forest-sorting-public-api-uint128-enabled-probe OBJECT
                "${_forest_uint128_enabled_probe}")
    target_link_libraries(forest-sorting-public-api-uint128-enabled-probe
                          PRIVATE forest-sorting-lib)
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

function(forest_expect_source_compiles check_name expected source)
    cmake_parse_arguments(PARSE_ARGV 3 CHECK "" "" "INCLUDES")
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_INCLUDES ${CHECK_INCLUDES})
    unset(${check_name} CACHE)
    check_cxx_source_compiles("${source}" ${check_name})
    cmake_pop_check_state()
    if(expected AND NOT ${check_name})
        message(FATAL_ERROR "${check_name} should compile")
    elseif(NOT expected AND ${check_name})
        message(FATAL_ERROR "${check_name} should not compile")
    endif()
endfunction()

set(_forest_production_include "${CMAKE_SOURCE_DIR}/include")
set(_forest_benchmark_common_include
    "${CMAKE_SOURCE_DIR}/benchmarks/support/common/include")
set(_forest_benchmark_tail_include
    "${CMAKE_SOURCE_DIR}/benchmarks/support/tail/include")
set(_forest_benchmark_full_include
    "${CMAKE_SOURCE_DIR}/benchmarks/support/full/include")

forest_expect_source_compiles(
    FOREST_COMMON_HEADER_COMPILES
    TRUE
    [[
#include <forest_sorting/benchmark_support/common/benchmark_cli.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}")
forest_expect_source_compiles(
    FOREST_COMMON_SEES_TAIL
    FALSE
    [[
#include <forest_sorting/benchmark_support/tail/tail_sort_variants.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}")
forest_expect_source_compiles(
    FOREST_COMMON_SEES_FULL
    FALSE
    [[
#include <forest_sorting/benchmark_support/full/hash_variants.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}")
forest_expect_source_compiles(
    FOREST_TAIL_SEES_FULL
    FALSE
    [[
#include <forest_sorting/benchmark_support/full/hash_variants.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}"
    "${_forest_benchmark_tail_include}")
forest_expect_source_compiles(
    FOREST_FULL_SEES_COMMON_AND_TAIL
    TRUE
    [[
#include <forest_sorting/benchmark_support/common/benchmark_cli.hpp>
#include <forest_sorting/benchmark_support/tail/tail_sort_variants.hpp>
#include <forest_sorting/benchmark_support/full/hash_variants.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}"
    "${_forest_benchmark_tail_include}"
    "${_forest_benchmark_full_include}")
forest_expect_source_compiles(
    FOREST_LIGHTWEIGHT_FULL_HEADERS_COMPILE
    TRUE
    [[
#include <forest_sorting/benchmark_support/full/forest_benchmark_options.hpp>
#include <forest_sorting/benchmark_support/full/parent_registry.hpp>
#include <forest_sorting/benchmark_support/full/sort_registry.hpp>
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}"
    "${_forest_benchmark_common_include}"
    "${_forest_benchmark_full_include}")

forest_expect_source_compiles(
    FOREST_PUBLIC_VALID_ID_BYTE_COUNT_LEAKS
    FALSE
    [[
#include <forest_sorting/traits.hpp>
template <typename T>
concept Leaked = forest_sorting::ValidForestIdByteCount<T>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_CHUNK_VALUE_LEAKS
    FALSE
    [[
#include <forest_sorting/traits.hpp>
using Leaked = forest_sorting::ForestTraitsChunkValue<4>;
int main() { return sizeof(Leaked) == 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_SORTING_NODE_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
#include <vector>
using Leaked = forest_sorting::ForestSortingNode<std::vector<int>>;
int main() { return sizeof(Leaked) == 0; }
]]
    INCLUDES
    "${_forest_production_include}")

forest_expect_source_compiles(
    FOREST_PUBLIC_INDEXED_INPUT_LEAKS
    FALSE
    [[
#include <forest_sorting/traits.hpp>
template <typename T>
concept Leaked = forest_sorting::IndexedInput<T>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_INDEXED_VALUE_LEAKS
    FALSE
    [[
#include <forest_sorting/traits.hpp>
using Leaked = forest_sorting::IndexedValue<int>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_ID_COUNT_POLICY_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
using Leaked = forest_sorting::IdCountPolicy<int>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_DEPTH_COUNT_POLICY_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
using Leaked = forest_sorting::DepthCountPolicy<int>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_ID_POLICY_CONCEPT_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
template <typename T>
concept Leaked = forest_sorting::IdRadixCountPolicy<T>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_DEPTH_POLICY_CONCEPT_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
template <typename T>
concept Leaked = forest_sorting::DepthRadixCountPolicy<T>;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_PRODUCTION_ID_POLICY_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
using Leaked = forest_sorting::ProductionIdCountPolicy;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")
forest_expect_source_compiles(
    FOREST_PUBLIC_PRODUCTION_DEPTH_POLICY_LEAKS
    FALSE
    [[
#include <forest_sorting/algorithms.hpp>
using Leaked = forest_sorting::ProductionDepthCountPolicy;
int main() { return 0; }
]]
    INCLUDES
    "${_forest_production_include}")

forest_expect_source_compiles(
    FOREST_SUPPORTED_PUBLIC_SURFACE_COMPILES
    TRUE
    [[
#include <forest_sorting/algorithms.hpp>
#include <forest_sorting/uint128_forest.hpp>
#include <vector>
using Nodes = std::vector<forest_sorting::Node>;
using Depths = std::vector<unsigned int>;
static_assert(forest_sorting::IndexedNodeInput<Nodes>);
static_assert(forest_sorting::CopyableNodeInput<Nodes>);
static_assert(forest_sorting::MutableNodeInput<Nodes>);
static_assert(forest_sorting::IndexedDepthInput<Depths>);
static_assert(forest_sorting::ForestTraits<
              forest_sorting::UInt128NodeTraits, forest_sorting::Node>);
static_assert(forest_sorting::ForestTraitsLess<
              forest_sorting::UInt128NodeTraits>);
static_assert(forest_sorting::ForestTraitsEqual<
              forest_sorting::UInt128NodeTraits>);
static_assert(forest_sorting::ForestTraitsParentSentinel<
              forest_sorting::UInt128NodeTraits>);
static_assert(forest_sorting::ForestTraitsChunkAccess<
              4, forest_sorting::UInt128NodeTraits>);
int main() {
    Nodes nodes;
    Depths depths;
    forest_sorting::UInt128NodeTraits traits;
    (void)forest_sorting::sortedOrderByDepthAndId(nodes, traits);
    (void)forest_sorting::sortedOrderByDepthAndIdWithDepths(nodes, traits,
                                                            depths);
    (void)forest_sorting::sortedCopyByDepthAndId(nodes, traits);
    forest_sorting::sortInPlaceByDepthAndId(nodes, traits);
    (void)forest_sorting::verifySortedByDepthAndId(nodes, traits);
}
]]
    INCLUDES
    "${_forest_production_include}")

try_compile(
    FOREST_REMOVED_API_COMPILES "${CMAKE_CURRENT_BINARY_DIR}/removed-api-check"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/removed_api_compile_fail.cpp"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/include"
                "-DCMAKE_CXX_STANDARD=20" "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
    OUTPUT_VARIABLE FOREST_REMOVED_API_OUTPUT)
if(FOREST_REMOVED_API_COMPILES)
    message(
        FATAL_ERROR
            "Removed API sortForestByDepthAndId unexpectedly compiled:\n${FOREST_REMOVED_API_OUTPUT}"
    )
endif()

add_test(NAME forest-sorting-tests COMMAND forest-sorting-tests)
add_test(NAME forest-sorting-portable-include-tests
         COMMAND forest-sorting-portable-include-tests)
add_test(NAME forest-sorting-hash-support-tests
         COMMAND forest-sorting-hash-support-tests)
add_test(NAME forest-sorting-options-header-link-tests
         COMMAND forest-sorting-options-header-link-tests)

add_test(
    NAME source.file-line-limit
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DMAX_SOURCE_LINES=1000 -P
        "${CMAKE_SOURCE_DIR}/cmake/CheckSourceLineLimit.cmake")
add_test(
    NAME source.file-line-limit-self-test
    COMMAND
        "${CMAKE_COMMAND}"
        -DCHECKER=${CMAKE_SOURCE_DIR}/cmake/CheckSourceLineLimit.cmake
        -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/line-limit-test -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/source_line_limit_test.cmake")
add_test(
    NAME configure.warning-probe-only-isolation
    COMMAND
        "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/warning-probe-only-test
        -DCXX_COMPILER=${CMAKE_CXX_COMPILER} "-DGENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/warning_probe_only_test.cmake")
add_test(
    NAME build.compile-commands-sync
    COMMAND
        "${CMAKE_COMMAND}"
        -DSYNC_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/SyncCompileCommands.cmake
        -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/compile-commands-sync-test
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_commands_sync_test.cmake")
add_test(
    NAME source.support-boundaries
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/support_boundary_test.cmake")

if(PROJECT_IS_TOP_LEVEL)
    if(NOT FOREST_API_MANIFEST_CLANG)
        message(
            FATAL_ERROR
                "A Clang C++ frontend is required for API manifest tests")
    endif()
    set(_forest_public_header_arguments)
    foreach(header IN LISTS _forest_public_headers)
        list(APPEND _forest_public_header_arguments --public-header "${header}")
    endforeach()
    set(_forest_api_manifest_script
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/public_api_manifest.py")
    set(_forest_api_registry
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/public_api_configurations.json")
    add_test(
        NAME source.public-api.portable-generic
        COMMAND
            "${Python3_EXECUTABLE}" "${_forest_api_manifest_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${_forest_portable_generic_probe}" --configuration
            portable-generic --include-root "${CMAKE_SOURCE_DIR}/include"
            --registry "${_forest_api_registry}" --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/public_api_portable_generic_manifest.json"
            ${_forest_public_header_arguments})
    add_test(
        NAME source.public-api.uint128-enabled
        COMMAND
            "${Python3_EXECUTABLE}" "${_forest_api_manifest_script}" --clang
            "${FOREST_API_MANIFEST_CLANG}" --compile-commands
            "${CMAKE_BINARY_DIR}/compile_commands.json" --source
            "${_forest_uint128_enabled_probe}" --configuration uint128-enabled
            --include-root "${CMAKE_SOURCE_DIR}/include" --registry
            "${_forest_api_registry}" --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/public_api_uint128_enabled_manifest.json"
            ${_forest_public_header_arguments})
    add_test(
        NAME source.public-api-self-test
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/public_api_manifest_self_test.py"
            --clang "${FOREST_API_MANIFEST_CLANG}")
    add_test(NAME source.clang-ast-command-self-test
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/clang_ast_self_test.py")
    add_test(
        NAME source.app-linkage-audit-self-test
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/app_linkage_audit_self_test.py")
endif()
