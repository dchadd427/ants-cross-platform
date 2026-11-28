# The application tests that run a real room manager (ants_server_core): a headless Application plays in a room of the game server's own RoomManager, over loopback sockets or a virtual-time
# link, with a bare second machine. Built by cmake/private.cmake (a tree without the game server does not have this folder). Everything else of the application is tests/test_app/.

# The network application (names, rooms, thumbs, start, matches): the real RoomManager of the dedicated server runs in the tests of the leader of a room
add_executable(test_network_app
    test_network_app.cpp
    test_network_app_basics.cpp
    test_network_app_leader.cpp
    test_network_app_rooms.cpp
    test_network_app_chat_box.cpp
    test_network_app_countdown.cpp
    test_network_app_rematch.cpp
)

target_include_directories(test_network_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# (ants_server_core brings ants_net, ants_ai, ants_sim and ants_assets with it: the real room manager of the dedicated server runs in the tests of the leader of a room; since protocol 11 the
# server runs the bots of a room, so it links ants_ai itself, and naming it here as well is a duplicate library for the linker)
target_link_libraries(test_network_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_network_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_network_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_network_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_network_app COMMAND test_network_app)
# COST (seconds of a CI run): ctest starts the dearest tests first, so that the longest one does not start last and run alone at the end
set_tests_properties(test_network_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" COST 200)

# The desktop start menu in the application: single player (with and without bots), join with a code, host an online match, every failure, the way back after a network game;
# a real RoomManager behind a real TCP listener on the loopback interface (ants_server_core), the screenshots of the panels (--shots DIR) and the gate's real-server mode (--real-server H:P)
add_executable(test_start_menu_app
    test_start_menu_app.cpp
    test_start_menu_app_play.cpp
    test_start_menu_app_options.cpp
)

target_include_directories(test_start_menu_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# (ants_server_core brings ants_ai with it since protocol 11: naming it here as well is a duplicate library for the linker)
target_link_libraries(test_start_menu_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_start_menu_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_start_menu_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_start_menu_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_start_menu_app COMMAND test_start_menu_app)
set_tests_properties(test_start_menu_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The screens of the way back and the keys' storage in the application (docs/NETWORK_PORT.md "What the clients do in release B"): a headless Application in a room of a real RoomManager
# (ants_server_core) over loopback sockets with a bare second machine: the overlay, the vote block, the catch-up screen, the start of a rejoin, leaving while held, the keys' file, the start menu's Rejoin
add_executable(test_rejoin_app
    test_rejoin_app.cpp
)

target_include_directories(test_rejoin_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# (ants_server_core brings ants_net, ants_ai, ants_sim and ants_assets with it)
target_link_libraries(test_rejoin_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_rejoin_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_rejoin_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_rejoin_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_rejoin_app COMMAND test_rejoin_app)
set_tests_properties(test_rejoin_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The prediction of one's own orders through the real input path over links with a delay and jitter (humping on mud, the attack click on a walking ant, reconciliation): a headless Application in a room
# of a real RoomManager (ants_server_core) over net::LoopbackNetwork links on a virtual clock, with a bare second machine
add_executable(test_prediction_link
    test_prediction_link.cpp
)

target_include_directories(test_prediction_link PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_prediction_link PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_prediction_link PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_prediction_link PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_prediction_link PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_prediction_link COMMAND test_prediction_link)
set_tests_properties(test_prediction_link PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The runtime DLLs of SDL on Windows go next to each program (tests/test_app/ copies them next to its own; these four are built into a folder of their own)
if(WIN32)
    foreach(app_server_test test_network_app test_start_menu_app test_rejoin_app test_prediction_link)
        if(DEFINED SDL2_DLL)
            add_custom_command(TARGET ${app_server_test} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL}" "$<TARGET_FILE_DIR:${app_server_test}>"
            )
        endif()
        if(DEFINED SDL2_TTF_DLL)
            add_custom_command(TARGET ${app_server_test} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_TTF_DLL}" "$<TARGET_FILE_DIR:${app_server_test}>"
            )
        endif()
    endforeach()
endif()
