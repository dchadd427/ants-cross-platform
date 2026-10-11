# The bot suite (run_tests.sh suite 2.20): the vocabulary of the bots, the controller, the bot seat in a room (lobby, session, NetGame on real sockets), what a bot sees (the view),
# the analysis of the map (MapInfo against the engine's path manager) and the match runner of the arena
add_executable(test_ai
    test_ai_main.cpp
    test_ai_setup.cpp
    test_ai_controller.cpp
    test_ai_net.cpp
    test_ai_view.cpp
    test_ai_map.cpp
    test_ai_arena.cpp
    test_ai_b2fix.cpp
    test_ai_b41.cpp
    test_ai_b41_team.cpp
    test_ai_b41_fight.cpp
    test_ai_b41_gate.cpp
    test_ai_b41_style.cpp
    test_ai_b41_offence.cpp
    test_ai_b41_cost.cpp
    test_ai_race.cpp
    test_ai_contest.cpp
    test_ai_islands.cpp
    test_ai_island_task.cpp
    test_ai_island_expedition.cpp
    test_ai_island_ferry.cpp
    test_ai_island_small.cpp
    test_ai_merge.cpp
    test_ai_cantgo.cpp
    test_ai_flowers.cpp
    test_ai_war.cpp
    test_ai_stand.cpp
)

target_link_libraries(test_ai PRIVATE
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_ai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools
)

target_link_libraries(test_ai PRIVATE ants_test_paths)

add_test(NAME test_ai COMMAND test_ai)
# COST (seconds of a CI run): ctest starts the dearest tests first, so that the longest one does not start last and run alone at the end
set_tests_properties(test_ai PROPERTIES COST 120)

# The island matches (run_tests.sh suite 2.28): whole matches of four standard bots on ISLANDS and SMALL at every level (the expedition, the bridges and the ferry). Kept apart from test_ai: it plays
# about a minute of matches.
add_executable(test_ai_islands
    test_ai_islands_main.cpp
    test_ai_island_matches.cpp
)

target_link_libraries(test_ai_islands PRIVATE
    ants_ai
    ants_sim
    ants_assets
)

target_include_directories(test_ai_islands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools
)

target_link_libraries(test_ai_islands PRIVATE ants_test_paths)

add_test(NAME test_ai_islands COMMAND test_ai_islands)

# The worker bot suite (run_tests.sh suite 2.22): the economy on every shipped map (matches of the whole length), the pinned baselines (baselines.inc, written by
# `bot_arena --write-baselines`), the task model. Kept apart from test_ai: it plays about a minute of matches, and the pinned procedure runs on a few threads.
find_package(Threads REQUIRED)
add_executable(test_ai_worker
    test_ai_worker_main.cpp
    test_ai_worker.cpp
)

# GCC 13 and later: -Wdangling-reference (part of -Wall) calls `const ArenaResult& r = alone_full(map, level);` dangling because the std::string argument is a temporary, although the
# reference is into a static cache that outlives the statement: a known false positive of that warning (GCC 14 narrowed it), found by the Linux job of .github/workflows/ci.yml; the other
# compilers have no such warning. It is switched off for this one source file only (a source file property is read by the targets of this directory), never for the product code.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    set_source_files_properties(test_ai_worker.cpp PROPERTIES COMPILE_OPTIONS "-Wno-dangling-reference")
endif()

target_link_libraries(test_ai_worker PRIVATE
    ants_ai
    ants_sim
    ants_assets
    Threads::Threads
)

target_include_directories(test_ai_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_ai_worker PRIVATE ants_test_paths)

add_test(NAME test_ai_worker COMMAND test_ai_worker)
set_tests_properties(test_ai_worker PROPERTIES COST 60)
