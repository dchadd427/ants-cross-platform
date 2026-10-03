cmake_minimum_required(VERSION 3.16)
project(AntsRemake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler cache. When ccache is found (on the PATH, or in ~/.local/bin, where an install without administrator rights puts it) it launches the C and
# C++ compilers, so that a second build of the same sources (a fresh clone, another worktree, a clean build folder) takes seconds instead of minutes. It is
# not used when the build already names a launcher (CI passes -DCMAKE_CXX_COMPILER_LAUNCHER=ccache, and sccache with MSVC under Ninja, itself), with MSVC (the Visual Studio
# generators do not support a launcher) and never for Emscripten (the web image). -DANTS_USE_CCACHE=OFF switches it off. The result of a build
# is the same with and without it (ccache replays the compiler's output, warnings included, so -DANTS_WERROR=ON behaves as before).
option(ANTS_USE_CCACHE "Use ccache as the compiler launcher when it is found" ON)
if(NOT EMSCRIPTEN)
    if(NOT ANTS_USE_CCACHE)
        message(STATUS "ccache: not used (ANTS_USE_CCACHE=OFF)")
    elseif(CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER)
        message(STATUS "ccache: not used (a compiler launcher is already set: '${CMAKE_CXX_COMPILER_LAUNCHER}')")
    elseif(MSVC)
        message(STATUS "ccache: not used (MSVC)")
    else()
        find_program(CCACHE_PROGRAM ccache HINTS "$ENV{HOME}/.local/bin")
        if(CCACHE_PROGRAM)
            set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
            message(STATUS "ccache: using ${CCACHE_PROGRAM}")
        else()
            message(STATUS "ccache: not used (not found)")
        endif()
    endif()
endif()

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
    # (GCC 13 and later: -Wdangling-reference has one known false positive in this project, in tests/test_ai/test_ai_worker.cpp: it is switched off for that one source file in
    # tests/test_ai/CMakeLists.txt, so that the product code and every other file keep the warning.)
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

# The version: ONE source, the single line of the file VERSION (MAJOR.MINOR.PATCH). The header ants_app/version.hpp does not exist in the repository: it is
# generated into <build folder>/generated from include/ants_app/version.hpp.in at configure time, and the library ants_version (below) puts that folder on
# the include path of every target that links it (ants_app, ants_server and, through them, the tests). The include path is a target property, never a
# global one, so every generator (Makefiles, Ninja, Xcode, the Visual Studio ones) finds the header the same way. The build id (which commit) is a second,
# tiny generated source that is stamped at every build; -DANTS_BUILD_ID=<text> sets it by hand (the Docker images do); see docs/WORKFLOW.md.
set(ANTS_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${ANTS_VERSION_FILE}")
    message(FATAL_ERROR "The file VERSION is missing at the root of the source tree (one line: MAJOR.MINOR.PATCH, for example 0.1.0).")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ANTS_VERSION_FILE}")   # a new VERSION configures again by itself
file(STRINGS "${ANTS_VERSION_FILE}" ANTS_VERSION_LINE LIMIT_COUNT 1)
string(STRIP "${ANTS_VERSION_LINE}" ANTS_VERSION_LINE)
if(NOT ANTS_VERSION_LINE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "The first line of the file VERSION must be MAJOR.MINOR.PATCH (digits only, for example 0.1.0), found: '${ANTS_VERSION_LINE}'")
endif()
set(ANTS_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(ANTS_VERSION_MINOR "${CMAKE_MATCH_2}")
set(ANTS_VERSION_PATCH "${CMAKE_MATCH_3}")
set(ANTS_VERSION_STRING "v${ANTS_VERSION_LINE}")
set(ANTS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/ants_app/version.hpp.in" "${ANTS_GENERATED_DIR}/ants_app/version.hpp" @ONLY)

set(ANTS_BUILD_ID "" CACHE STRING "Build id shown by --version and the web page (empty: the short git commit when this is a git checkout, else \"unknown\")")
find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
    set(GIT_EXECUTABLE "")
endif()
add_custom_target(ants_build_id
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUT_FILE=${ANTS_GENERATED_DIR}/build_id.cpp"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DANTS_BUILD_ID=${ANTS_BUILD_ID}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ants_stamp_build_id.cmake"
    BYPRODUCTS "${ANTS_GENERATED_DIR}/build_id.cpp"
    COMMENT "Stamping the build id"
    VERBATIM)
# The file must exist at the first configure (some generators check their sources then); the build rewrites it when the commit changed.
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT_FILE=${ANTS_GENERATED_DIR}/build_id.cpp"
    "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}" "-DANTS_BUILD_ID=${ANTS_BUILD_ID}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ants_stamp_build_id.cmake")
add_library(ants_version STATIC "${ANTS_GENERATED_DIR}/build_id.cpp")
add_dependencies(ants_version ants_build_id)
target_include_directories(ants_version PUBLIC "${ANTS_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(ants_version PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

# Where this checkout is, for the tests and tools that read Original-Ants/, tests/data or web/ (tests/common/ants_test_paths.hpp): the folders are written into ONE tiny
# generated source, never into a compile definition, so that the same source compiles to the same object in every folder and ccache serves another worktree (docs/WORKFLOW.md).
if(NOT EMSCRIPTEN)
    set(ANTS_PATH_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(ANTS_PATH_ORIGINAL_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Original-Ants")
    set(ANTS_PATH_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    foreach(ants_path_var ANTS_PATH_SOURCE_DIR ANTS_PATH_ORIGINAL_ASSETS_DIR ANTS_PATH_TEST_DATA_DIR)
        string(REPLACE "\\" "\\\\" ${ants_path_var} "${${ants_path_var}}")        # (a C++ string literal: a backslash or a quote in a folder name is escaped)
        string(REPLACE "\"" "\\\"" ${ants_path_var} "${${ants_path_var}}")
    endforeach()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ants_test_paths.cpp.in" "${ANTS_GENERATED_DIR}/ants_test_paths.cpp" @ONLY)
    add_library(ants_test_paths STATIC EXCLUDE_FROM_ALL "${ANTS_GENERATED_DIR}/ants_test_paths.cpp")
    target_include_directories(ants_test_paths PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tests/common")
endif()

# Table generator (not part of the normal build): writes src/ants_sim/food_footprints_data.inc from Original-Ants/ants.chd
#   cmake --build build --target gen_food_footprints && ./build/gen_food_footprints > src/ants_sim/food_footprints_data.inc
if(NOT EMSCRIPTEN)
    add_executable(gen_food_footprints EXCLUDE_FROM_ALL tools/gen_food_footprints.cpp)
    target_link_libraries(gen_food_footprints PRIVATE ants_sim ants_assets ants_test_paths)

    # Map sweep (not part of the normal build): plays every map of a folder headless with the real engine (loads, determinism, speed, memory)
    #   cmake --build build --target map_sweep && ./build/map_sweep <maps-folder> --out report.json   (./build/map_sweep --selftest checks the tool itself)
    find_package(Threads REQUIRED)
    add_executable(map_sweep EXCLUDE_FROM_ALL tools/map_sweep.cpp)
    target_link_libraries(map_sweep PRIVATE ants_net ants_sim ants_assets ants_test_paths Threads::Threads)

    # Bot arena (not part of the normal build): plays matches of computer players headless with the real engine and the controller (docs/BOTS.md)
    #   cmake --build build --target bot_arena && ./build/bot_arena --map TINY --seeds 1..8 --seat 0=worker:hard --seat 1=worker:easy --rotate --out report.json
    #   (./build/bot_arena --selftest checks the tool itself)
    add_executable(bot_arena EXCLUDE_FROM_ALL tools/bot_arena.cpp)
    target_link_libraries(bot_arena PRIVATE ants_ai ants_sim ants_assets ants_test_paths Threads::Threads)
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
