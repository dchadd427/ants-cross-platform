add_executable(test_sim_rules
    test_sim_rules.cpp
)

target_link_libraries(test_sim_rules PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_sim_rules PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_sim_rules PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_sim_rules COMMAND test_sim_rules)

add_executable(test_challenger_m2_2
    test_challenger_m2_2.cpp
)

target_link_libraries(test_challenger_m2_2 PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_challenger_m2_2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_challenger_m2_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m2_2 COMMAND test_challenger_m2_2)

add_executable(test_challenger_m2_1
    test_challenger_m2_1.cpp
)

target_link_libraries(test_challenger_m2_1 PRIVATE
    ants_sim
    ants_assets
)

target_include_directories(test_challenger_m2_1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(test_challenger_m2_1 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m2_1 COMMAND test_challenger_m2_1)
