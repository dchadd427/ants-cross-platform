cmake_minimum_required(VERSION 3.16)
project(AntsRemake VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
    )
endif()

# Sanitizer Option
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Global Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Subdirectories
add_subdirectory(src/ants_assets)
add_subdirectory(src/ants_sim)
add_subdirectory(src/ants_app)

# Testing
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/test_assets)
    add_subdirectory(tests/test_sim)
    add_subdirectory(tests/test_app)
endif()

