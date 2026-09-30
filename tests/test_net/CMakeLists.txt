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
