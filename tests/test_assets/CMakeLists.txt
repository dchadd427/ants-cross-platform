add_executable(test_assets
    test_assets.cpp
)

target_link_libraries(test_assets PRIVATE ants_assets)

target_link_libraries(test_assets PRIVATE ants_test_paths)

add_test(NAME test_assets COMMAND test_assets)

add_executable(test_challenger_m1_1
    test_adversarial.cpp
)

target_link_libraries(test_challenger_m1_1 PRIVATE ants_assets)

target_link_libraries(test_challenger_m1_1 PRIVATE ants_test_paths)

add_test(NAME test_challenger_m1_1 COMMAND test_challenger_m1_1)

add_executable(test_challenger_m1_2
    test_challenger_m1_2.cpp
)

target_link_libraries(test_challenger_m1_2 PRIVATE ants_assets)

target_link_libraries(test_challenger_m1_2 PRIVATE ants_test_paths)

add_test(NAME test_challenger_m1_2 COMMAND test_challenger_m1_2)

add_executable(test_challenger_m1_it2
    test_challenger_m1_it2.cpp
)

target_link_libraries(test_challenger_m1_it2 PRIVATE ants_assets)

target_link_libraries(test_challenger_m1_it2 PRIVATE ants_test_paths)

add_test(NAME test_challenger_m1_it2 COMMAND test_challenger_m1_it2)

add_executable(test_challenger_m1_it2_2
    test_challenger_m1_it2_2.cpp
)

target_link_libraries(test_challenger_m1_it2_2 PRIVATE ants_assets)

target_link_libraries(test_challenger_m1_it2_2 PRIVATE ants_test_paths)

add_test(NAME test_challenger_m1_it2_2 COMMAND test_challenger_m1_it2_2)

# Movement ground-truth tables (generated src/ants_sim/movement_tables_data.inc) vs ants.chd / Ants.exe (a local copy of the
# program, or without it the remake's tables checked against the pinned digests of tests/common/original_program_bytes.hpp)
add_executable(test_movement_tables
    test_movement_tables.cpp
)

target_link_libraries(test_movement_tables PRIVATE ants_sim ants_assets)

target_include_directories(test_movement_tables PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ants_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(test_movement_tables PRIVATE ants_test_paths)

add_test(NAME test_movement_tables COMMAND test_movement_tables)

# The writer of the .lvl format (docs/MAP_FORMAT.md), and the reader and the writer on hostile input (the round-trip proof over the library is tests/test_sim/test_map_roundtrip)
add_executable(test_lvl_writer
    test_lvl_writer.cpp
)

target_link_libraries(test_lvl_writer PRIVATE ants_assets ants_test_paths)

add_test(NAME test_lvl_writer COMMAND test_lvl_writer)

# The original-safe profile of the map checker (docs/MAP_FORMAT.md): every rule, files, stricter than the remake, the 540 maps of the library
add_executable(test_original_safe
    test_original_safe.cpp
)

target_link_libraries(test_original_safe PRIVATE ants_assets ants_test_paths)

add_test(NAME test_original_safe COMMAND test_original_safe)

# The map core behind plain data (include/ants_assets/lvl_core.hpp) and its C interface, the code of the browser module lvl_core.wasm; it also keeps the expectation
# in tests/data/lvl_core/ that tests/scripts/lvl_core_check.js holds the module to
add_executable(test_lvl_core
    test_lvl_core.cpp
    ../../src/ants_assets/lvl_core_c.cpp
)

target_link_libraries(test_lvl_core PRIVATE ants_assets ants_test_paths)

add_test(NAME test_lvl_core COMMAND test_lvl_core)
