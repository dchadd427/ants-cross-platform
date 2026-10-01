# The control interface of the game server: a strict JSON library and a minimal authenticated HTTP server (native builds only, the root
# CMakeLists.txt does not add this directory for a WebAssembly build). It stands alone: no dependency on the simulation or on ants_net.
add_library(ants_ctl STATIC
    json.cpp
    http.cpp
)

if(WIN32)
    target_link_libraries(ants_ctl PUBLIC ws2_32)
endif()

target_include_directories(ants_ctl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(ants_ctl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_ctl"
)
