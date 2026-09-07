add_executable(test_assets
    test_assets.cpp
)

target_link_libraries(test_assets PRIVATE ants_assets)

target_compile_definitions(test_assets PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_assets COMMAND test_assets)

add_executable(test_challenger_m1_1
    test_adversarial.cpp
)

target_link_libraries(test_challenger_m1_1 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_1 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_1 COMMAND test_challenger_m1_1)

add_executable(test_challenger_m1_2
    test_challenger_m1_2.cpp
)

target_link_libraries(test_challenger_m1_2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_2 COMMAND test_challenger_m1_2)

add_executable(test_challenger_m1_it2
    test_challenger_m1_it2.cpp
)

target_link_libraries(test_challenger_m1_it2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_it2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_it2 COMMAND test_challenger_m1_it2)

add_executable(test_challenger_m1_it2_2
    test_challenger_m1_it2_2.cpp
)

target_link_libraries(test_challenger_m1_it2_2 PRIVATE ants_assets)

target_compile_definitions(test_challenger_m1_it2_2 PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_challenger_m1_it2_2 COMMAND test_challenger_m1_it2_2)

