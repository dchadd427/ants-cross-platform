add_executable(test_server
    test_server.cpp
)

target_link_libraries(test_server PRIVATE
    ants_server_core
)

target_include_directories(test_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_server PRIVATE ants_test_paths)

# The tests of the restart records (S3.95, S3.96, S3.101) run the real program as a child process of the test (stopped and killed in the middle of a match, started again): the program is built
# with the test, and the folder that it is built into goes into ONE tiny source that CMake generates (ants_test_paths::server_dir(), as tests/common/ants_test_paths.hpp does for the folders of
# the checkout), never into a compile definition, so that the same test source compiles to the same object in every build folder. The Makefile and Ninja generators build it there, the
# multi-config ones (Visual Studio, Xcode) in a folder of the configuration inside it: the test looks in both. Not on Windows, where the test has no fork.
if(NOT WIN32)
    add_dependencies(test_server ants_server)
    set(ANTS_PATH_SERVER_DIR "${CMAKE_BINARY_DIR}/src/ants_server")
    string(REPLACE "\\" "\\\\" ANTS_PATH_SERVER_DIR "${ANTS_PATH_SERVER_DIR}")             # (a C++ string literal: a backslash or a quote in a folder name is escaped)
    string(REPLACE "\"" "\\\"" ANTS_PATH_SERVER_DIR "${ANTS_PATH_SERVER_DIR}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ants_server_binary.cpp.in" "${ANTS_GENERATED_DIR}/ants_server_binary.cpp" @ONLY)
    target_sources(test_server PRIVATE "${ANTS_GENERATED_DIR}/ants_server_binary.cpp")
    target_compile_definitions(test_server PRIVATE ANTS_HAS_SERVER_BINARY)
endif()

add_test(NAME test_server COMMAND test_server)
# COST (seconds of a CI run): ctest starts the dearest tests first, so that the longest one does not start last and run alone at the end
set_tests_properties(test_server PROPERTIES COST 150)

# The way back of the game's NetGame (docs/NETWORK_PORT.md "Reconnect"): real NetGames over loopback sockets against a real RoomManager, so it needs the server's library as test_server does
add_executable(test_rejoin
    test_rejoin.cpp
)

target_link_libraries(test_rejoin PRIVATE
    ants_server_core
    ants_test_paths
)

target_include_directories(test_rejoin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_rejoin COMMAND test_rejoin)
set_tests_properties(test_rejoin PROPERTIES COST 25)

# The store of the matches that the server keeps (ants_server/replay_store.hpp): the file names, the age and size limits, the free space, a folder that holds other things. No network and no match
add_executable(test_replay_store
    test_replay_store.cpp
)

target_link_libraries(test_replay_store PRIVATE
    ants_server_core
)

target_include_directories(test_replay_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_replay_store COMMAND test_replay_store)
set_tests_properties(test_replay_store PROPERTIES COST 3)

# The board of the matches that run now (ants_server/live_board.hpp): the id of a match, the list, the snapshot's cache and the memory of the matches that ended. No network and no room: recorders that the test feeds
add_executable(test_live_board
    test_live_board.cpp
)

target_link_libraries(test_live_board PRIVATE
    ants_server_core
    ants_test_paths
)

target_include_directories(test_live_board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_live_board COMMAND test_live_board)
set_tests_properties(test_live_board PROPERTIES COST 3)

# What a record of the match history says of a match (ants_server/history_record.hpp): who won by the rule of the game mode, the ties, teams, a quitter, a seat that dropped out, the JSON. Pure: made-up engine results
add_executable(test_history_record
    test_history_record.cpp
)

target_link_libraries(test_history_record PRIVATE
    ants_server_core
)

target_include_directories(test_history_record PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_history_record COMMAND test_history_record)
set_tests_properties(test_history_record PROPERTIES COST 1)

# The match history's store in the server's database (ants_server/history_store.hpp): records kept and read back, the filters, sorts and search of the list, the markers, the indexes, a database that stops
# answering, the feeder that counts the replay store's matches (real recordings of the shipped TINY map) and the control interface's /history calls in front of a room manager. No network, no room that runs a match
add_executable(test_history_store
    test_history_store.cpp
)

target_link_libraries(test_history_store PRIVATE
    ants_server_core
    ants_test_paths
)

target_include_directories(test_history_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_history_store COMMAND test_history_store)
set_tests_properties(test_history_store PROPERTIES COST 6)

# The play counts of the maps (HistoryStore::put and map_plays, table map_plays): the 30-second and people-only rules, one play for each match however often it is kept, the key of file name and hash, the
# count in the file, a database that stops answering, and real recordings counted by the feeder. No network, no room that runs a match
add_executable(test_map_plays
    test_map_plays.cpp
)

target_link_libraries(test_map_plays PRIVATE
    ants_server_core
    ants_test_paths
)

target_include_directories(test_map_plays PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_map_plays COMMAND test_map_plays)
set_tests_properties(test_map_plays PROPERTIES COST 3)

# The match history in front of real rooms (ants_server/history_store.hpp): rooms of computer players play whole matches in both game modes on a room manager with a replay store and a database; the history
# counts them from the recordings when no room runs a match, and both doors list them. No network
add_executable(test_history_rooms
    test_history_rooms.cpp
)

target_link_libraries(test_history_rooms PRIVATE
    ants_server_core
    ants_test_paths
)

target_include_directories(test_history_rooms PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_history_rooms COMMAND test_history_rooms)
set_tests_properties(test_history_rooms PROPERTIES COST 15)
