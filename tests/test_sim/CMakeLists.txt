add_executable(test_sim_rules
    test_sim_rules.cpp
)

target_link_libraries(test_sim_rules PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_sim_rules PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_sim_rules PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_sim_rules COMMAND test_sim_rules)

add_executable(test_challenger_m2_2
    test_challenger_m2_2.cpp
)

target_link_libraries(test_challenger_m2_2 PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_challenger_m2_2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_challenger_m2_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m2_2 COMMAND test_challenger_m2_2)

add_executable(test_challenger_m2_1
    test_challenger_m2_1.cpp
)

target_link_libraries(test_challenger_m2_1 PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_challenger_m2_1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_challenger_m2_1 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m2_1 COMMAND test_challenger_m2_1)

add_executable(test_path_planner
    test_path_planner.cpp
)

target_link_libraries(test_path_planner PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_path_planner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_path_planner COMMAND test_path_planner)

# Frame-exact original movement: golden timings from the Ants.exe reference model, blocking, orders
add_executable(test_movement_golden
    test_movement_golden.cpp
)

target_link_libraries(test_movement_golden PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_movement_golden PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_movement_golden PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_movement_golden COMMAND test_movement_golden)

add_executable(test_hill_actions
    test_hill_actions.cpp
)

target_link_libraries(test_hill_actions PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_hill_actions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_hill_actions COMMAND test_hill_actions)

add_executable(test_combat_actions
    test_combat_actions.cpp
)

target_link_libraries(test_combat_actions PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_combat_actions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_combat_actions COMMAND test_combat_actions)
