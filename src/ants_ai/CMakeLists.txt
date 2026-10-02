# The computer players (docs/BOTS.md): virtual clients that read the world through a read-only view and send ants::sim::Commands through the door a person
# uses. They depend on the simulation only: no SDL, no sockets, no threads, no clock, so the library builds for the web too.
add_library(ants_ai STATIC
    bot.cpp
    bot_view.cpp
    map_info.cpp
    bot_controller.cpp
    tasks.cpp
    worker_bot.cpp
    baselines.cpp
    arena.cpp
)

target_include_directories(ants_ai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_ai PUBLIC
    ants_sim
)

set_target_properties(ants_ai PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_ai"
)
