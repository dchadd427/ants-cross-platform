add_executable(test_lockstep
    test_lockstep.cpp
)

target_link_libraries(test_lockstep PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_lockstep PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_lockstep COMMAND test_lockstep)

add_executable(test_tcp
    test_tcp.cpp
)

target_link_libraries(test_tcp PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_tcp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_tcp COMMAND test_tcp)

add_executable(test_lan
    test_lan.cpp
)

target_link_libraries(test_lan PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_lan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_lan COMMAND test_lan)

add_executable(test_lobby
    test_lobby.cpp
)

target_link_libraries(test_lobby PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_lobby PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_lobby COMMAND test_lobby)

add_executable(test_netgame
    test_netgame.cpp
)

target_link_libraries(test_netgame PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_netgame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_netgame PRIVATE ants_test_paths)

add_test(NAME test_netgame COMMAND test_netgame)

add_executable(test_ws
    test_ws.cpp
)

target_link_libraries(test_ws PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_ws PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_ws COMMAND test_ws)

add_executable(test_latency
    test_latency.cpp
)

target_link_libraries(test_latency PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_latency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_latency COMMAND test_latency)

add_executable(test_jitter
    test_jitter.cpp
)

target_link_libraries(test_jitter PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_jitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_jitter COMMAND test_jitter)
