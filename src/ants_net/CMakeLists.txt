add_library(ants_net STATIC
    protocol.cpp
    name_rules.cpp
    sequencer.cpp
    lockstep.cpp
    prediction.cpp
    cue_router.cpp
    jitter.cpp
    turnlog.cpp
    attendance.cpp
    latency.cpp
    loopback.cpp
    session.cpp
    session_client.cpp
    session_hashes.cpp
    lobby.cpp
    netgame.cpp
    netgame_match.cpp
    p2p.cpp
    address.cpp
)

# Raw sockets (TCP, the UDP of the LAN discovery, and the WebSocket server for browsers behind a proxy) exist on native platforms only (a browser itself
# uses WebRTC / WebSocket as a client)
if(NOT EMSCRIPTEN)
    target_sources(ants_net PRIVATE tcp.cpp lan.cpp ws.cpp turn.cpp)
    if(WIN32)
        target_link_libraries(ants_net PUBLIC ws2_32)
    endif()
    # The desktop game's encrypted link to a game server: a WebSocket client (wss.cpp) over a TLS client (tls.cpp: Mbed TLS, third_party/mbedtls; the system's roots: tls_roots.cpp)
    target_sources(ants_net PRIVATE tls.cpp tls_roots.cpp wss.cpp)
    find_package(Threads REQUIRED)                          # (wss.cpp asks the system about a root on a thread of its own)
    target_link_libraries(ants_net PUBLIC ants_mbedtls Threads::Threads)
    if(WIN32)
        target_link_libraries(ants_net PUBLIC crypt32)       # the system's certificate stores
    endif()
    # (macOS: the Security framework is opened at run time by tls_roots.cpp, not linked: a program that links CoreFoundation gets a variable of its own in its environment, which the game server's tests look for)
endif()

# The browser's WebSocket client (the page's own WebSocket through Emscripten's library: -lwebsocket.js)
if(EMSCRIPTEN)
    target_sources(ants_net PRIVATE wasm_ws.cpp wasm_rtc.cpp)
    target_link_libraries(ants_net PUBLIC websocket.js)
endif()

target_include_directories(ants_net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_net PUBLIC
    ants_sim
    ants_ai          # the names of the bots and the reserved-name rule (ants_ai/bot_names.hpp): pure functions, ants_ai needs only the simulation
)

set_target_properties(ants_net PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_net"
)
