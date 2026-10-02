add_executable(test_assets
    test_assets.cpp
)

target_link_libraries(test_assets PRIVATE ants_assets)

target_compile_definitions(test_assets PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_assets COMMAND test_assets)

add_executable(test_challenger_m1_1
    test_adversarial.cpp
)

target_link_libraries(test_challenger_m1_1 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_1 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_1 COMMAND test_challenger_m1_1)

add_executable(test_challenger_m1_2
    test_challenger_m1_2.cpp
)

target_link_libraries(test_challenger_m1_2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_2 COMMAND test_challenger_m1_2)

add_executable(test_challenger_m1_it2
    test_challenger_m1_it2.cpp
)

target_link_libraries(test_challenger_m1_it2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_it2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_it2 COMMAND test_challenger_m1_it2)

add_executable(test_challenger_m1_it2_2
    test_challenger_m1_it2_2.cpp
)

target_link_libraries(test_challenger_m1_it2_2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_it2_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

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

target_compile_definitions(test_movement_tables PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_movement_tables COMMAND test_movement_tables)
