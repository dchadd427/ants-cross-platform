add_executable(test_server
    test_server.cpp
)

target_link_libraries(test_server PRIVATE
    ants_server_core
)

target_include_directories(test_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_server PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_server COMMAND test_server)
