# SDL2 configuration
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_path(SDL2_INCLUDE_DIRS SDL.h PATH_SUFFIXES SDL2 /opt/homebrew/include/SDL2 /opt/homebrew/opt/sdl2/include/SDL2)
    find_library(SDL2_LIBRARIES NAMES SDL2 PATHS /opt/homebrew/lib /opt/homebrew/opt/sdl2/lib)
endif()

if(NOT SDL2_INCLUDE_DIRS)
    set(SDL2_INCLUDE_DIRS "/opt/homebrew/opt/sdl2/include/SDL2")
    set(SDL2_LIBRARIES "-L/opt/homebrew/opt/sdl2/lib -lSDL2")
endif()

add_executable(test_app_integration
    test_app_integration.cpp
)

target_include_directories(test_app_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_app_integration PRIVATE
    ants_app
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_app_integration PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

target_compile_definitions(test_app_integration PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

add_test(NAME test_app_integration COMMAND test_app_integration)
