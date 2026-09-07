cmake_minimum_required(VERSION 3.16)
project(ants_e2e_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warning and optimization flags
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

add_executable(e2e_runner
    e2e_main.cpp
    tier1_assets.cpp
    tier1_simulation.cpp
    tier1_app_hud.cpp
    tier2_boundaries.cpp
    tier3_pairwise.cpp
    tier4_scenarios.cpp
)

target_include_directories(e2e_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_test(NAME AllE2ETests COMMAND e2e_runner --all)
