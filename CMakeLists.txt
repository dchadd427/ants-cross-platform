cmake_minimum_required(VERSION 3.16)
project(AntsRemake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings and Platform Flags
# ANTS_WERROR turns every warning into an error (the project rule: -Wall -Wextra -Werror -Wsign-conversion, zero warnings). It is OFF by
# default so that a newer compiler (or the Emscripten image) never breaks a plain build; the test runners and release builds switch it on:
#   cmake -B build -DANTS_WERROR=ON
option(ANTS_WERROR "Treat compiler warnings as errors" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
    )
    if(ANTS_WERROR)
        add_compile_options(-Werror)
    endif()
elseif(MSVC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /permissive- /wd4127 /wd4244 /wd4100 /wd4189)
    if(ANTS_WERROR)
        add_compile_options(/WX)
    endif()
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
add_subdirectory(src/ants_net)
add_subdirectory(src/ants_ai)         # the computer players (virtual clients, docs/BOTS.md): no SDL, no sockets, no threads, so it builds for the web too
if(NOT EMSCRIPTEN)
    add_subdirectory(src/ants_ctl)    # control interface of the game server (strict JSON, authenticated HTTP on the loopback address): sockets, native only
    add_subdirectory(src/ants_server) # the dedicated game server (rooms, a referee per room): native only
endif()
# The game program needs SDL2 (window, audio, fonts); a build of the dedicated server alone (the server's Docker image) does not: -DANTS_BUILD_APP=OFF
option(ANTS_BUILD_APP "Build the game program (needs SDL2)" ON)
if(ANTS_BUILD_APP)
    add_subdirectory(src/ants_app)
endif()

# Table generator (not part of the normal build): writes src/ants_sim/food_footprints_data.inc from Original-Ants/ants.chd
#   cmake --build build --target gen_food_footprints && ./build/gen_food_footprints > src/ants_sim/food_footprints_data.inc
if(NOT EMSCRIPTEN)
    add_executable(gen_food_footprints EXCLUDE_FROM_ALL tools/gen_food_footprints.cpp)
    target_link_libraries(gen_food_footprints PRIVATE ants_sim ants_assets)
    target_compile_definitions(gen_food_footprints PRIVATE
        ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Original-Ants")

    # Map sweep (not part of the normal build): plays every map of a folder headless with the real engine (loads, determinism, speed, memory)
    #   cmake --build build --target map_sweep && ./build/map_sweep <maps-folder> --out report.json   (./build/map_sweep --selftest checks the tool itself)
    find_package(Threads REQUIRED)
    add_executable(map_sweep EXCLUDE_FROM_ALL tools/map_sweep.cpp)
    target_link_libraries(map_sweep PRIVATE ants_net ants_sim ants_assets Threads::Threads)
    target_compile_definitions(map_sweep PRIVATE
        ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Original-Ants")

    # Bot arena (not part of the normal build): plays matches of computer players headless with the real engine and the controller (docs/BOTS.md)
    #   cmake --build build --target bot_arena && ./build/bot_arena --map TINY --seeds 1..8 --seat 0=worker:hard --seat 1=worker:easy --rotate --out report.json
    #   (./build/bot_arena --selftest checks the tool itself)
    add_executable(bot_arena EXCLUDE_FROM_ALL tools/bot_arena.cpp)
    target_link_libraries(bot_arena PRIVATE ants_ai ants_sim ants_assets Threads::Threads)
    target_compile_definitions(bot_arena PRIVATE
        ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Original-Ants")
endif()

# Testing
if(EMSCRIPTEN)
    option(BUILD_TESTS "Build test executables" OFF)
else()
    option(BUILD_TESTS "Build test executables" ON)
endif()
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    # Guard (every platform, every configure): SDL's headers do `#define main SDL_main`, and on Windows SDL2main.lib supplies the real main,
    # which calls SDL_main(int, char**). A program that includes them (directly or through an ants_app header) and defines `int main()` or
    # `int main(void)` compiles everywhere and fails to LINK on Windows ("unresolved external symbol SDL_main", v0.0.96 shipped seven such
    # test programs). So no program under tests/test_app/ (all of them link ants_app) and no other test or tool that includes SDL or an ants_app
    # header may define a main without parameters: write `int main(int argc, char* argv[])` and `(void)argc; (void)argv;`.
    file(GLOB_RECURSE ANTS_MAIN_GUARD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp")
    set(ANTS_MAIN_GUARD_BAD "")
    foreach(guard_src IN LISTS ANTS_MAIN_GUARD_SOURCES)
        file(READ "${guard_src}" guard_text)
        string(REGEX MATCH "(^|\n)[ \t]*int[ \t]+main[ \t]*\\([ \t]*(void)?[ \t]*\\)" guard_main "${guard_text}")
        if(guard_main STREQUAL "")
            continue()
        endif()
        file(RELATIVE_PATH guard_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${guard_src}")
        set(guard_links_sdl FALSE)
        if(guard_rel MATCHES "^tests/test_app/")
            set(guard_links_sdl TRUE)
        elseif(guard_text MATCHES "#[ \t]*include[ \t]*[<\"](SDL|ants_app/)")
            set(guard_links_sdl TRUE)
        endif()
        if(guard_links_sdl)
            string(FIND "${guard_text}" "${guard_main}" guard_pos)
            string(SUBSTRING "${guard_text}" 0 ${guard_pos} guard_before)
            string(REGEX MATCHALL "\n" guard_newlines "${guard_before}")
            list(LENGTH guard_newlines guard_line)
            if(guard_main MATCHES "^\n")
                math(EXPR guard_line "${guard_line} + 2")
            else()
                math(EXPR guard_line "${guard_line} + 1")
            endif()
            string(APPEND ANTS_MAIN_GUARD_BAD "  ${guard_rel}:${guard_line}: int main() without arguments\n")
        endif()
    endforeach()
    if(NOT ANTS_MAIN_GUARD_BAD STREQUAL "")
        message(FATAL_ERROR "A program that links SDL must define `int main(int argc, char* argv[])` (SDL's headers rename main to SDL_main; the Windows build, SDL2main.lib, needs exactly that signature, or the link fails with \"unresolved external symbol SDL_main\"). Fix these, and add `(void)argc; (void)argv;` against unused-parameter warnings:\n${ANTS_MAIN_GUARD_BAD}")
    endif()
    enable_testing()
    add_subdirectory(tests/test_assets)
    add_subdirectory(tests/test_sim)
    add_subdirectory(tests/test_net)
    add_subdirectory(tests/test_ai)
    if(NOT EMSCRIPTEN)
        add_subdirectory(tests/test_ctl)
        add_subdirectory(tests/test_server)
    endif()
    if(ANTS_BUILD_APP)
        add_subdirectory(tests/test_app)
    endif()
endif()
