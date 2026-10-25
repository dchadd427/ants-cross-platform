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

target_link_libraries(test_sim_rules PRIVATE ants_test_paths)

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

target_link_libraries(test_movement_golden PRIVATE ants_test_paths)

add_test(NAME test_movement_golden COMMAND test_movement_golden)

# Independent models of the original walk and of PathRequest::Step against the remake on random scenarios (the models' tables
# come from a local copy of Ants.exe, or without it from the remake's tables checked against the pinned digests of
# tests/common/original_program_bytes.hpp, which includes the generated src/ants_sim/movement_tables_data.inc)
add_executable(test_movement_differential
    test_movement_differential.cpp
)

target_link_libraries(test_movement_differential PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_movement_differential PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ants_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(test_movement_differential PRIVATE ants_test_paths)

add_test(NAME test_movement_differential COMMAND test_movement_differential)

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

add_executable(test_ability_actions
    test_ability_actions.cpp
)

target_link_libraries(test_ability_actions PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_ability_actions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_ability_actions COMMAND test_ability_actions)

add_executable(test_powerup_actions
    test_powerup_actions.cpp
)

target_link_libraries(test_powerup_actions PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_powerup_actions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_powerup_actions PRIVATE ants_test_paths)

add_test(NAME test_powerup_actions COMMAND test_powerup_actions)

add_executable(test_food_actions
    test_food_actions.cpp
)

target_link_libraries(test_food_actions PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_food_actions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_food_actions PRIVATE ants_test_paths)

add_test(NAME test_food_actions COMMAND test_food_actions)

# What a level file says that the engine must obey as the original does, for maps that third-party editors made: the default ant type (block 3) and power-ups / flower
# droppers by tile id (a dictionary that calls them "." changes nothing); synthetic levels made from the bytes of TINY.LVL
add_executable(test_level_defaults
    test_level_defaults.cpp
)

target_link_libraries(test_level_defaults PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_level_defaults PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_level_defaults PRIVATE ants_test_paths)

add_test(NAME test_level_defaults COMMAND test_level_defaults)

# Bombs that a map file holds (layer-2 ids 129 .. 132, docs/reverse_engineering/map_file_bombs.md): they are the planted bomb of their color, walkable, set off by a step; the community map Bombz Away plays
add_executable(test_file_bombs
    test_file_bombs.cpp
)

target_link_libraries(test_file_bombs PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_file_bombs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_file_bombs PRIVATE ants_test_paths)

add_test(NAME test_file_bombs COMMAND test_file_bombs)

# Engine-fact probes of the bomb throw, the hit knockback, fire walls, defuse and power-up pick-up (docs/reverse_engineering/bomb_gates.md); no rule is changed by them
add_executable(test_bomb_facts
    test_bomb_facts.cpp
)

target_link_libraries(test_bomb_facts PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_bomb_facts PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_bomb_facts PRIVATE ants_test_paths)

add_test(NAME test_bomb_facts COMMAND test_bomb_facts)

# The map loader as the original's (docs/reverse_engineering/map_loader_fidelity.md): tile ids, tables and the file's solid bits instead of dictionary names and rules of the remake's own
add_executable(test_loader_fidelity
    test_loader_fidelity.cpp
)

target_link_libraries(test_loader_fidelity PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_loader_fidelity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_loader_fidelity PRIVATE ants_test_paths)

add_test(NAME test_loader_fidelity COMMAND test_loader_fidelity)

# The round-trip law of the map writer (docs/MAP_FORMAT.md): every map of the library is read, written and read again, and the file and its rewrite play the same
add_executable(test_map_roundtrip
    test_map_roundtrip.cpp
)

target_link_libraries(test_map_roundtrip PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_map_roundtrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_map_roundtrip PRIVATE ants_test_paths)

add_test(NAME test_map_roundtrip COMMAND test_map_roundtrip)

# The hash ledger (docs/TESTING.md): the 540 maps' state hashes at ticks 0, 200 and 2,400 in a file that two builds are compared with. A tool, run by hand (the whole library takes minutes)
add_executable(hash_ledger
    hash_ledger.cpp
)

target_link_libraries(hash_ledger PRIVATE
    ants_sim
    ants_assets
    ants_test_paths
)

target_include_directories(hash_ledger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_executable(test_commands
    test_commands.cpp
)

target_link_libraries(test_commands PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_commands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_commands PRIVATE ants_test_paths)

add_test(NAME test_commands COMMAND test_commands)

# The value semantics of SimulationEngine, the copy that the client-side prediction of one's own orders rests on: a copy equals its source in everything that can be observed, ticks
# identically (the state hash compared at every tick), and shares nothing. The second test is the same program on whole matches of the six shipped maps (about a minute).
add_executable(test_engine_copy
    test_engine_copy.cpp
)

target_link_libraries(test_engine_copy PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_engine_copy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_engine_copy PRIVATE ants_test_paths)

# (one ctest entry per test program, which tests/scripts/test_ci_workflow.py holds the CI to: the CI's run is the quick tests AND the whole matches; run_tests.sh has them as two suites, 2.25 and 2.26)
add_test(NAME test_engine_copy COMMAND test_engine_copy --all)
# COST (seconds of a CI run): ctest starts the dearest tests first, so that the longest one does not start last and run alone at the end
set_tests_properties(test_engine_copy PROPERTIES COST 50)

# The lookups that replaced scans (find_unit by id, has_request_for by flag) against the scans, in matches: the simulation again, with the shadow check compiled in
# (include/ants_sim/lookup_shadow.hpp: every call also runs the scan and stops the program on a difference, and a thread can be switched to the scans alone). Only this
# program links it; no product build has the check. The program is one ctest entry (the quick tests and the big ones: run_tests.sh has them as suites 2.33 and 2.34).
get_target_property(ANTS_SIM_SOURCE_NAMES ants_sim SOURCES)
list(TRANSFORM ANTS_SIM_SOURCE_NAMES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../../src/ants_sim/")
add_library(ants_sim_shadow STATIC ${ANTS_SIM_SOURCE_NAMES})

target_include_directories(ants_sim_shadow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(ants_sim_shadow PUBLIC ants_assets)

target_compile_definitions(ants_sim_shadow PUBLIC ANTS_SIM_LOOKUP_SHADOW=1)

add_executable(test_engine_lookups
    test_engine_lookups.cpp
)

target_link_libraries(test_engine_lookups PRIVATE
    ants_sim_shadow
    ants_assets
)

target_include_directories(test_engine_lookups PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(test_engine_lookups PRIVATE ants_test_paths)

add_test(NAME test_engine_lookups COMMAND test_engine_lookups --all)
set_tests_properties(test_engine_lookups PROPERTIES COST 60)

add_executable(test_game_mode
    test_game_mode.cpp
)

target_link_libraries(test_game_mode PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_game_mode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_game_mode PRIVATE ants_test_paths)

add_test(NAME test_game_mode COMMAND test_game_mode)

# The line "<Name> (<Color>) is out of ants!" and its cue (187), the engine copy and the state hash
add_executable(test_out_of_ants
    test_out_of_ants.cpp
)

target_link_libraries(test_out_of_ants PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_out_of_ants PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_out_of_ants PRIVATE ants_test_paths)

add_test(NAME test_out_of_ants COMMAND test_out_of_ants)
