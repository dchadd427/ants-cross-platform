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
