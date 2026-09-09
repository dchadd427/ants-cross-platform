# SDL2 configuration
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND AND NOT DEFINED SDL2_INCLUDE_DIRS)
    find_path(SDL2_INCLUDE_DIRS SDL.h PATH_SUFFIXES SDL2 /opt/homebrew/include/SDL2 /opt/homebrew/opt/sdl2/include/SDL2)
    find_library(SDL2_LIBRARIES NAMES SDL2 PATHS /opt/homebrew/lib /opt/homebrew/opt/sdl2/lib)
endif()

if(NOT SDL2_INCLUDE_DIRS)
    set(SDL2_INCLUDE_DIRS "/opt/homebrew/opt/sdl2/include/SDL2")
    set(SDL2_LIBRARIES "-L/opt/homebrew/opt/sdl2/lib -lSDL2")
endif()

# SDL2_ttf configuration
find_package(SDL2_ttf QUIET)
if(NOT SDL2_TTF_FOUND AND NOT DEFINED SDL2_TTF_INCLUDE_DIRS)
    find_path(SDL2_TTF_INCLUDE_DIRS SDL_ttf.h PATH_SUFFIXES SDL2 /opt/homebrew/include/SDL2 /opt/homebrew/opt/sdl2_ttf/include/SDL2)
    find_library(SDL2_TTF_LIBRARIES NAMES SDL2_ttf PATHS /opt/homebrew/lib /opt/homebrew/opt/sdl2_ttf/lib)
endif()

if(NOT SDL2_TTF_INCLUDE_DIRS AND EXISTS "/opt/homebrew/opt/sdl2_ttf/include/SDL2/SDL_ttf.h")
    set(SDL2_TTF_INCLUDE_DIRS "/opt/homebrew/opt/sdl2_ttf/include/SDL2")
    set(SDL2_TTF_LIBRARIES "-L/opt/homebrew/opt/sdl2_ttf/lib -lSDL2_ttf")
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
if(SDL2_TTF_INCLUDE_DIRS)
    target_include_directories(ants_app PUBLIC ${SDL2_TTF_INCLUDE_DIRS})
endif()

target_link_libraries(ants_app PUBLIC
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)
if(SDL2_TTF_LIBRARIES)
    target_compile_definitions(ants_app PUBLIC ANTS_ENABLE_SDL_TTF=1)
    target_link_libraries(ants_app PUBLIC ${SDL2_TTF_LIBRARIES})
endif()

if(APPLE)
    target_link_libraries(ants_app PUBLIC
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(ants_app PUBLIC ws2_32 winmm)
endif()

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

if(WIN32 AND DEFINED SDL2_DLL)
    add_custom_command(TARGET ants POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL}" "$<TARGET_FILE_DIR:ants>"
    )
endif()
