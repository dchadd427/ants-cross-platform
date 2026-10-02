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
)

target_link_libraries(test_ai PRIVATE
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_ai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_ai PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_ai COMMAND test_ai)

# The worker bot suite (run_tests.sh suite 2.22): the economy on every shipped map (matches of the whole length), the pinned baselines (baselines.inc, written by
# `bot_arena --write-baselines`), the task model. Kept apart from test_ai: it plays about a minute of matches, and the pinned procedure runs on a few threads.
find_package(Threads REQUIRED)
add_executable(test_ai_worker
    test_ai_worker_main.cpp
    test_ai_worker.cpp
)

target_link_libraries(test_ai_worker PRIVATE
    ants_ai
    ants_sim
    ants_assets
    Threads::Threads
)

target_include_directories(test_ai_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_ai_worker PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_ai_worker COMMAND test_ai_worker)
