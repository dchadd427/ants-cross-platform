# The replay suite (run_tests.sh suite 2.29): the .antsrep file and its strict reader, the recorder, the lock-step runner's tap that feeds it, the player that checks a file against its
# hashes and lists its orders. The matches are made by the engine and the bots of the arena on a shipped map.
add_executable(test_replay
    test_replay.cpp
)

target_link_libraries(test_replay PRIVATE
    ants_replay
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ants_test_paths
)

target_include_directories(test_replay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_replay COMMAND test_replay)

# The two rules numbers of game mode 187 (run_tests.sh suite 2.29.1): rules 6 (the hill works) still plays from a recording, rules 7 (the hill is closed) is what a match records now.
add_executable(test_rules_187
    test_rules_187.cpp
)

target_link_libraries(test_rules_187 PRIVATE
    ants_replay
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ants_test_paths
)

target_include_directories(test_rules_187 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_rules_187 COMMAND test_rules_187)

# The rules numbers of the original's game (run_tests.sh suite 2.29.2): 5 (the original's end rule alone) still plays from a recording, 8 (the last side standing) is what a match records now.
add_executable(test_rules_last_side
    test_rules_last_side.cpp
)

target_link_libraries(test_rules_last_side PRIVATE
    ants_replay
    ants_net
    ants_sim
    ants_assets
    ants_test_paths
)

target_include_directories(test_rules_last_side PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME test_rules_last_side COMMAND test_rules_last_side)

# A match on a map of rules 2 (run_tests.sh suite 2.29.3): the wide search in a recording, and a reader that does not know the word 2.
add_executable(test_rules_two
    test_rules_two.cpp
)

target_link_libraries(test_rules_two PRIVATE
    ants_replay
    ants_net
    ants_sim
    ants_assets
    ants_test_paths
)

target_include_directories(test_rules_two PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_rules_two COMMAND test_rules_two)
