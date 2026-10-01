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

add_executable(test_app_integration
    test_app_integration.cpp
)

target_include_directories(test_app_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_app_integration PRIVATE
    ants_app
    ants_ai
    ants_net
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

if(WIN32)
    target_link_libraries(test_app_integration PRIVATE ws2_32 winmm)
    if(DEFINED SDL2_DLL)
        add_custom_command(TARGET test_app_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL}" "$<TARGET_FILE_DIR:test_app_integration>"
        )
    endif()
    if(DEFINED SDL2_TTF_DLL)
        add_custom_command(TARGET test_app_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_TTF_DLL}" "$<TARGET_FILE_DIR:test_app_integration>"
        )
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/Original-Ants/Maps")
        add_custom_command(TARGET test_app_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Original-Ants/Maps" "$<TARGET_FILE_DIR:test_app_integration>/Maps"
        )
    endif()
endif()

target_compile_definitions(test_app_integration PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

add_test(NAME test_app_integration COMMAND test_app_integration)
set_tests_properties(test_app_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Render parity: real Renderer (software, dummy video driver) vs an independent CPU model of the original's draw rules
add_executable(test_render_parity
    test_render_parity.cpp
)

target_include_directories(test_render_parity PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_render_parity PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_render_parity PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_render_parity PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(test_render_parity PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

add_test(NAME test_render_parity COMMAND test_render_parity)
set_tests_properties(test_render_parity PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# HUD layout: the real HUD drives a recording IRenderer; draw calls are checked against the original's coordinates
add_executable(test_hud_layout
    test_hud_layout.cpp
)

target_include_directories(test_hud_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_hud_layout PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_hud_layout PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_hud_layout PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(test_hud_layout PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

add_test(NAME test_hud_layout COMMAND test_hud_layout)
set_tests_properties(test_hud_layout PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Status messages: the one-slot status line, selection / order texts, flash flags and the drawing of the status box
add_executable(test_status_messages
    test_status_messages.cpp
)

target_include_directories(test_status_messages PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_status_messages PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_status_messages PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_status_messages PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(test_status_messages PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

add_test(NAME test_status_messages COMMAND test_status_messages)
set_tests_properties(test_status_messages PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Input model: edge scrolling, minimap drag, pointer rules (pure logic plus the HUD's input tick; golden data from the original's code)
add_executable(test_input_model
    test_input_model.cpp
)

target_include_directories(test_input_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_input_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_input_model PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_input_model PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(test_input_model PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
)

add_test(NAME test_input_model COMMAND test_input_model)
set_tests_properties(test_input_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Pointer model: hit boxes, cursor decision table, clicks by cursor mode, rubber band, right button, pedestals (Ants.exe INPUT task)
add_executable(test_pointer_model
    test_pointer_model.cpp
)

target_include_directories(test_pointer_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_pointer_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_pointer_model PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_pointer_model PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_pointer_model COMMAND test_pointer_model)
set_tests_properties(test_pointer_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Options model: the slider, the latching button and the edit field of the original, the settings store and the options screen built from them (pure logic)
add_executable(test_options
    test_options.cpp
)

target_include_directories(test_options PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_options PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(test_options PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_options PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_options COMMAND test_options)
set_tests_properties(test_options PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(test_network_app
    test_network_app.cpp
)

target_include_directories(test_network_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(test_network_app PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
    ${SDL2_LIBRARIES}
)

target_compile_definitions(test_network_app PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Original-Ants"
)

if(APPLE)
    target_link_libraries(test_network_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_network_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_network_app COMMAND test_network_app)
set_tests_properties(test_network_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
