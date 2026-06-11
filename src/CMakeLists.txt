add_executable(forest-sorting
    main.cpp
)

target_link_libraries(forest-sorting PRIVATE forest-sorting-lib)
forest_sorting_apply_common_options(forest-sorting)
