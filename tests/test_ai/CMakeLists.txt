# The bot suite (run_tests.sh suite 2.20): the vocabulary of the bots, the controller, and the bot seat in a room (lobby, session, NetGame on real sockets)
add_executable(test_ai
    test_ai_main.cpp
    test_ai_setup.cpp
    test_ai_controller.cpp
    test_ai_net.cpp
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
