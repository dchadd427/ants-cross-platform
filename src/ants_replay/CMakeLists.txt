# Replays (docs/REPLAYS.md): the .antsrep file, the recorder that every game feeds, and the player that checks a file and lists its orders. They depend on the simulation, the network
# protocol (the turn, the rules number, the start message) and the map reader: no SDL, no sockets, no threads, no clock, so the library builds for the web too.
add_library(ants_replay STATIC
    replay.cpp
    recorder.cpp
    player.cpp
)

target_include_directories(ants_replay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_replay PUBLIC
    ants_net
    ants_sim
    ants_assets
)

set_target_properties(ants_replay PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_replay"
)
