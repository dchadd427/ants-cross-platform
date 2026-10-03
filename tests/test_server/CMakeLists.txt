add_executable(test_server
    test_server.cpp
)

target_link_libraries(test_server PRIVATE
    ants_server_core
)

target_include_directories(test_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(test_server PRIVATE ants_test_paths)

# The tests of the restart records (S3.89, S3.90) run the real program as a child process of the test (stopped and killed in the middle of a match, started again): its path, and the program
# is built with the test. Not on Windows, where the test has no fork.
if(NOT WIN32)
    add_dependencies(test_server ants_server)
    target_compile_definitions(test_server PRIVATE ANTS_SERVER_BINARY="$<TARGET_FILE:ants_server>")
endif()

add_test(NAME test_server COMMAND test_server)
