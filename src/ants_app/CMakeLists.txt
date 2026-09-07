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

add_library(ants_app STATIC
    audio_mixer.cpp
    midi_player.cpp
    renderer.cpp
    hud.cpp
    scorecard.cpp
    map_select.cpp
    application.cpp
)

target_include_directories(ants_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(ants_app PUBLIC
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework CoreAudio"
)

set_target_properties(ants_app PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_app"
)

add_executable(ants
    main.cpp
)

target_link_libraries(ants PRIVATE
    ants_app
)
