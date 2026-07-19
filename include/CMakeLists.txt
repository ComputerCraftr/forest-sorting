add_library(forest-sorting-lib INTERFACE)
add_library(forest-sorting::forest-sorting ALIAS forest-sorting-lib)

target_compile_features(forest-sorting-lib INTERFACE cxx_std_20)

set(_forest_sorting_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/forest_sorting/algorithms.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/forest_sorting/traits.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/forest_sorting/uint128.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/forest_sorting/uint128_forest.hpp")
set_property(GLOBAL PROPERTY FOREST_SORTING_PUBLIC_HEADERS
                             "${_forest_sorting_public_headers}")
target_sources(
    forest-sorting-lib
    INTERFACE FILE_SET
              public_headers
              TYPE
              HEADERS
              BASE_DIRS
              "${CMAKE_CURRENT_SOURCE_DIR}"
              FILES
              ${_forest_sorting_public_headers})

target_include_directories(
    forest-sorting-lib INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                 $<INSTALL_INTERFACE:include>)
