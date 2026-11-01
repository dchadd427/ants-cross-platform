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
