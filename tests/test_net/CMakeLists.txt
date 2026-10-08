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
# COST (seconds of a CI run): ctest starts the dearest tests first, so that the longest one does not start last and run alone at the end
set_tests_properties(test_lockstep PROPERTIES COST 170)

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

# The client-side prediction of one's own orders (ants_net/prediction.hpp): a rig that plays the server, a real runner and engines, and an oracle
add_executable(test_prediction
    test_prediction.cpp
)

target_link_libraries(test_prediction PRIVATE
    ants_net
    ants_sim
    ants_assets
)

target_include_directories(test_prediction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_prediction COMMAND test_prediction)
set_tests_properties(test_prediction PROPERTIES COST 35)
