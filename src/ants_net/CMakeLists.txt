add_library(ants_net STATIC
    protocol.cpp
    sequencer.cpp
    lockstep.cpp
    prediction.cpp
    jitter.cpp
    turnlog.cpp
    attendance.cpp
    latency.cpp
    loopback.cpp
    session.cpp
    lobby.cpp
    netgame.cpp
)

# Raw sockets (TCP, the UDP of the LAN discovery, and the WebSocket server for browsers behind a proxy) exist on native platforms only (a browser itself
# uses WebRTC / WebSocket as a client)
if(NOT EMSCRIPTEN)
    target_sources(ants_net PRIVATE tcp.cpp lan.cpp ws.cpp)
    if(WIN32)
        target_link_libraries(ants_net PUBLIC ws2_32)
    endif()
endif()

# The browser's WebSocket client (the page's own WebSocket through Emscripten's library: -lwebsocket.js)
if(EMSCRIPTEN)
    target_sources(ants_net PRIVATE wasm_ws.cpp)
    target_link_libraries(ants_net PUBLIC websocket.js)
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
