# The dedicated game server (native builds only: it needs sockets): a library with everything but main (so that the tests link it) and the program.
add_library(ants_server_core STATIC
    map_store.cpp
    room.cpp
    room_manager.cpp
    control.cpp
    secret.cpp
    restart_record.cpp
    site_stats.cpp
    replay_store.cpp
    history_store.cpp
    live_board.cpp
)
target_include_directories(ants_server_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ants_server_core PUBLIC
    ants_net
    ants_ai          # the bots of a room (docs/BOTS.md B6): virtual clients that the referee runs
    ants_sim
    ants_assets
    ants_ctl
    ants_replay      # the recorder of a room's match and the files that the server keeps (docs/REPLAYS.md "On the game server")
)
if(WIN32)
    target_link_libraries(ants_server_core PRIVATE bcrypt)      # the random number generator that makes the control secret
endif()
set_target_properties(ants_server_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "ants_server_core"
)

add_executable(ants_server main.cpp)
target_link_libraries(ants_server PRIVATE ants_server_core ants_version)     # ants_version: the generated ants_app/version.hpp and ants::BUILD_ID (root CMakeLists.txt)
set_target_properties(ants_server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
