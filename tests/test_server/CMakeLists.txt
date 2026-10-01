add_executable(test_server
    test_server.cpp
)

target_link_libraries(test_server PRIVATE
    ants_server_core
)

target_include_directories(test_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_server COMMAND test_server)
