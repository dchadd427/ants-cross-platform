add_library(ants_net STATIC
    protocol.cpp
    sequencer.cpp
    lockstep.cpp
    loopback.cpp
    session.cpp
    lobby.cpp
)

# Raw sockets exist on native platforms only (a browser uses WebRTC / WebSocket)
if(NOT EMSCRIPTEN)
    target_sources(ants_net PRIVATE tcp.cpp)
    if(WIN32)
        target_link_libraries(ants_net PUBLIC ws2_32)
    endif()
endif()

target_include_directories(ants_net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_net PUBLIC
    ants_sim
)

set_target_properties(ants_net PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_net"
)
