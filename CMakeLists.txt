cmake_minimum_required(VERSION 3.16)
project(AntsRemake VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings and Platform Flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
    )
elseif(MSVC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /permissive- /wd4127 /wd4244 /wd4100 /wd4189)
endif()

# Automated SDL2 resolution for Windows (PC)
if(WIN32)
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        include(FetchContent)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        FetchContent_Declare(
            SDL2_prebuilt
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-devel-2.30.12-VC.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_GetProperties(SDL2_prebuilt)
        if(NOT sdl2_prebuilt_POPULATED)
            FetchContent_Populate(SDL2_prebuilt)
            set(SDL2_INCLUDE_DIRS "${sdl2_prebuilt_SOURCE_DIR}/include" CACHE PATH "SDL2 include directory" FORCE)
            set(SDL2_LIBRARIES "${sdl2_prebuilt_SOURCE_DIR}/lib/x64/SDL2.lib;${sdl2_prebuilt_SOURCE_DIR}/lib/x64/SDL2main.lib" CACHE STRING "SDL2 libraries" FORCE)
            set(SDL2_DLL "${sdl2_prebuilt_SOURCE_DIR}/lib/x64/SDL2.dll" CACHE FILEPATH "SDL2 runtime DLL" FORCE)
        endif()
    endif()

    find_package(SDL2_ttf QUIET)
    if(NOT SDL2_TTF_FOUND)
        FetchContent_Declare(
            SDL2_ttf_prebuilt
            URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.22.0/SDL2_ttf-devel-2.22.0-VC.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_GetProperties(SDL2_ttf_prebuilt)
        if(NOT sdl2_ttf_prebuilt_POPULATED)
            FetchContent_Populate(SDL2_ttf_prebuilt)
            set(SDL2_TTF_INCLUDE_DIRS "${sdl2_ttf_prebuilt_SOURCE_DIR}/include" CACHE PATH "SDL2_ttf include directory" FORCE)
            set(SDL2_TTF_LIBRARIES "${sdl2_ttf_prebuilt_SOURCE_DIR}/lib/x64/SDL2_ttf.lib" CACHE STRING "SDL2_ttf libraries" FORCE)
            set(SDL2_TTF_DLL "${sdl2_ttf_prebuilt_SOURCE_DIR}/lib/x64/SDL2_ttf.dll" CACHE FILEPATH "SDL2_ttf runtime DLL" FORCE)
        endif()
    endif()
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
if(EMSCRIPTEN)
    option(BUILD_TESTS "Build test executables" OFF)
else()
    option(BUILD_TESTS "Build test executables" ON)
endif()
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests/test_assets)
    add_subdirectory(tests/test_sim)
    add_subdirectory(tests/test_app)
endif()

