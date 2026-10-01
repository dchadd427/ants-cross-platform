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
