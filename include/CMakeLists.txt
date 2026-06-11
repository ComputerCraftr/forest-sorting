add_library(forest-sorting-lib INTERFACE)
add_library(forest-sorting::forest-sorting ALIAS forest-sorting-lib)

target_compile_features(forest-sorting-lib INTERFACE cxx_std_20)

target_include_directories(forest-sorting-lib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
