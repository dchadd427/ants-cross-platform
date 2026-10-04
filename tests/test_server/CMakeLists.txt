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

# The tests of the restart records (S3.95, S3.96, S3.101) run the real program as a child process of the test (stopped and killed in the middle of a match, started again): the program is built
# with the test, and the folder that it is built into goes into ONE tiny source that CMake generates (ants_test_paths::server_dir(), as tests/common/ants_test_paths.hpp does for the folders of
# the checkout), never into a compile definition, so that the same test source compiles to the same object in every build folder. The Makefile and Ninja generators build it there, the
# multi-config ones (Visual Studio, Xcode) in a folder of the configuration inside it: the test looks in both. Not on Windows, where the test has no fork.
if(NOT WIN32)
    add_dependencies(test_server ants_server)
    set(ANTS_PATH_SERVER_DIR "${CMAKE_BINARY_DIR}/src/ants_server")
    string(REPLACE "\\" "\\\\" ANTS_PATH_SERVER_DIR "${ANTS_PATH_SERVER_DIR}")             # (a C++ string literal: a backslash or a quote in a folder name is escaped)
    string(REPLACE "\"" "\\\"" ANTS_PATH_SERVER_DIR "${ANTS_PATH_SERVER_DIR}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ants_server_binary.cpp.in" "${ANTS_GENERATED_DIR}/ants_server_binary.cpp" @ONLY)
    target_sources(test_server PRIVATE "${ANTS_GENERATED_DIR}/ants_server_binary.cpp")
    target_compile_definitions(test_server PRIVATE ANTS_HAS_SERVER_BINARY)
endif()

add_test(NAME test_server COMMAND test_server)
