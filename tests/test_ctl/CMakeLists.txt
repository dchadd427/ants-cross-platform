add_executable(test_ctl
    test_ctl.cpp
    test_ctl_json.cpp
    test_ctl_http.cpp
)

target_link_libraries(test_ctl PRIVATE
    ants_ctl
)

target_include_directories(test_ctl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_ctl COMMAND test_ctl)
