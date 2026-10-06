add_executable(test_app_integration
    test_app_integration.cpp
)

target_include_directories(test_app_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_app_integration PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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
endif()

target_link_libraries(test_app_integration PRIVATE ants_test_paths)

add_test(NAME test_app_integration COMMAND test_app_integration)
set_tests_properties(test_app_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Render parity: real Renderer (software, dummy video driver) vs an independent CPU model of the original's draw rules
add_executable(test_render_parity
    test_render_parity.cpp
)

target_include_directories(test_render_parity PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_render_parity PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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

target_link_libraries(test_render_parity PRIVATE ants_test_paths)

add_test(NAME test_render_parity COMMAND test_render_parity)
set_tests_properties(test_render_parity PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# HUD layout: the real HUD drives a recording IRenderer; draw calls are checked against the original's coordinates
add_executable(test_hud_layout
    test_hud_layout.cpp
)

target_include_directories(test_hud_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_hud_layout PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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

target_link_libraries(test_hud_layout PRIVATE ants_test_paths)

add_test(NAME test_hud_layout COMMAND test_hud_layout)
set_tests_properties(test_hud_layout PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Status messages: the one-slot status line, selection / order texts, flash flags and the drawing of the status box
add_executable(test_status_messages
    test_status_messages.cpp
)

target_include_directories(test_status_messages PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_status_messages PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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

target_link_libraries(test_status_messages PRIVATE ants_test_paths)

add_test(NAME test_status_messages COMMAND test_status_messages)
set_tests_properties(test_status_messages PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Input model: edge scrolling, minimap drag, pointer rules (pure logic plus the HUD's input tick; golden data from the original's code)
add_executable(test_input_model
    test_input_model.cpp
)

target_include_directories(test_input_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_input_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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

target_link_libraries(test_input_model PRIVATE ants_test_paths)

add_test(NAME test_input_model COMMAND test_input_model)
set_tests_properties(test_input_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Pointer model: hit boxes, cursor decision table, clicks by cursor mode, rubber band, right button, pedestals (Ants.exe INPUT task)
add_executable(test_pointer_model
    test_pointer_model.cpp
)

target_include_directories(test_pointer_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_pointer_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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
)

target_link_libraries(test_options PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
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
)

# (ants_server_core brings ants_net, ants_ai, ants_sim and ants_assets with it: the real room manager of the dedicated server runs in the tests of the leader of a room; since protocol 11 the
# server runs the bots of a room, so it links ants_ai itself, and naming it here as well is a duplicate library for the linker)
target_link_libraries(test_network_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_network_app PRIVATE ants_test_paths)

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

# The prediction of one's own orders in the application (R3 of the rollback): what the match screen reads. A headless Application plays a real match against a bare machine over loopback
add_executable(test_prediction_app
    test_prediction_app.cpp
)

target_include_directories(test_prediction_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_prediction_app PRIVATE
    ants_app
)

target_link_libraries(test_prediction_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_prediction_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_prediction_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_prediction_app COMMAND test_prediction_app)
set_tests_properties(test_prediction_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# View fingerprint (M0 of the widescreen / zoom / touch work): what the game draws (draw-call and pixel hashes) and what the pointer does (every pixel of the 640 x 480 screen), pinned at the classic picture
add_executable(test_view_fingerprint
    test_view_fingerprint.cpp
)

target_include_directories(test_view_fingerprint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_view_fingerprint PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_view_fingerprint PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_view_fingerprint PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_view_fingerprint PRIVATE ants_test_paths)

add_test(NAME test_view_fingerprint COMMAND test_view_fingerprint)
set_tests_properties(test_view_fingerprint PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The desktop start menu in the application: single player (with and without bots), join with a code, host an online match, every failure, the way back after a network game;
# a real RoomManager behind a real TCP listener on the loopback interface (ants_server_core), the screenshots of the panels (--shots DIR) and the gate's real-server mode (--real-server H:P)
add_executable(test_start_menu_app
    test_start_menu_app.cpp
)

target_include_directories(test_start_menu_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# (ants_server_core brings ants_ai with it since protocol 11: naming it here as well is a duplicate library for the linker)
target_link_libraries(test_start_menu_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_start_menu_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_start_menu_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_start_menu_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_start_menu_app COMMAND test_start_menu_app)
set_tests_properties(test_start_menu_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The desktop start menu as a model: navigation, text fields, the single-player seats and the rule-8 line, the join and host panels, the room's code and the clipboard, servers, room codes,
# names, the remembered settings, the layout of every panel, what is drawn and how long texts are cut, and which command-line options show or skip the menu (no window, no sockets)
add_executable(test_start_menu
    test_start_menu.cpp
)

target_include_directories(test_start_menu PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_start_menu PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

target_link_libraries(test_start_menu PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_start_menu PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_start_menu PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_start_menu COMMAND test_start_menu)
set_tests_properties(test_start_menu PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Screen layout (M1 of the widescreen work): the geometry of the picture as numbers (screen_layout.hpp) and every consumer of it (HUD rectangles and drawing, edge scroll, minimap, start view, pointer limits, the plate's corner, camera, renderer, application)
add_executable(test_screen_layout
    test_screen_layout.cpp
)

target_include_directories(test_screen_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_screen_layout PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_screen_layout PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_screen_layout PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_screen_layout PRIVATE ants_test_paths)

add_test(NAME test_screen_layout COMMAND test_screen_layout)
set_tests_properties(test_screen_layout PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Canvas layout (M2 of the widescreen work): the logical canvas and how SDL fits it into the window (CanvasLayout), --aspect and the settings key, the window that opens for a 16:9 canvas, the classic picture centred in it, Alt+Enter, the screenshot
add_executable(test_canvas_layout
    test_canvas_layout.cpp
)

target_include_directories(test_canvas_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_canvas_layout PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_canvas_layout PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_canvas_layout PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_canvas_layout PRIVATE ants_test_paths)

add_test(NAME test_canvas_layout COMMAND test_canvas_layout)
set_tests_properties(test_canvas_layout PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Wide match screen (M3 of the widescreen work): the anchoring model of the frame's 14 pieces, the cuts in the art, the composed picture, the HUD at 960 x 540, the view, the pages and dialogs, the picture per screen, the 16:9 default
add_executable(test_wide_hud
    test_wide_hud.cpp
)

target_include_directories(test_wide_hud PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_wide_hud PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_wide_hud PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_wide_hud PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_wide_hud PRIVATE ants_test_paths)

add_test(NAME test_wide_hud COMMAND test_wide_hud)
set_tests_properties(test_wide_hud PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Wide setup screen (the setup / map selection screen at 960 x 540 with a map preview): the layout against the mock-ups, the seams against the art, the composed screens pixel for pixel,
# the preview, the screens' state, the pointer through the application, the picture per screen, fingerprints
add_executable(test_wide_setup
    test_wide_setup.cpp
)

target_include_directories(test_wide_setup PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_wide_setup PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_wide_setup PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_wide_setup PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_wide_setup PRIVATE ants_test_paths)

add_test(NAME test_wide_setup COMMAND test_wide_setup)
set_tests_properties(test_wide_setup PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Wide pages (the loading screen, the quick help at the start, the results and the desktop start menu at 960 x 540): the layouts against the mock-ups, the seams of the results boxes against the art, the boxes
# at the original's size, the loading screen's order, the composed screens pixel for pixel, the results' numbers, the pointer, the picture per screen, fingerprints
add_executable(test_wide_pages
    test_wide_pages.cpp
)

target_include_directories(test_wide_pages PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_wide_pages PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_wide_pages PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_wide_pages PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_wide_pages PRIVATE ants_test_paths)

add_test(NAME test_wide_pages COMMAND test_wide_pages)
set_tests_properties(test_wide_pages PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The map preview as the game's own picture of the map (the world drawn offscreen by the real renderer and reduced by an exact area filter): the filter against an independent oracle, the fit
# of a non-square map, the render (the match screen's own pixels, the live renderer left as it was, tiles equal the whole), the engine, the cache, the fallback, the six shipped maps pinned
add_executable(test_map_preview
    test_map_preview.cpp
)

target_include_directories(test_map_preview PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_map_preview PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_map_preview PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_map_preview PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_map_preview PRIVATE ants_test_paths)

add_test(NAME test_map_preview COMMAND test_map_preview)
set_tests_properties(test_map_preview PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Zoom model (M4 of the widescreen work): the levels, which of them a map and a kind of match offers, the camera after a zoom that keeps the point under the pointer, the clamps, the camera's
# conversions, the edge scroll in screen pixels, the minimap, the start view, the wheel's accumulation and the settings key (pure numbers: no window)
add_executable(test_zoom_model
    test_zoom_model.cpp
)

target_include_directories(test_zoom_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_zoom_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_zoom_model PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_zoom_model PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_zoom_model PRIVATE ants_test_paths)

add_test(NAME test_zoom_model COMMAND test_zoom_model)
set_tests_properties(test_zoom_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Zoom view (M4): the world pass at a zoom against the direct pass (pixel for pixel), the HUD's cursor, clicks, rubber band, minimap and edge scroll at a zoom, and the application's wheel, middle
# button, anchoring, fairness per kind of match, settings key, start view, listener and a network match with two different zooms
add_executable(test_zoom_view
    test_zoom_view.cpp
)

target_include_directories(test_zoom_view PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_zoom_view PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_zoom_view PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_zoom_view PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_zoom_view PRIVATE ants_test_paths)

add_test(NAME test_zoom_view COMMAND test_zoom_view)
set_tests_properties(test_zoom_view PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Zoom fingerprint (M4): the pictures of the world pass and of whole frames, the minimap's frame, the pointer's edge scroll, cursor and clicks, and the camera's conversions at the zoom levels 0.5 and 2,
# classic and wide, pinned as golden numbers
add_executable(test_zoom_fingerprint
    test_zoom_fingerprint.cpp
)

target_include_directories(test_zoom_fingerprint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_zoom_fingerprint PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_zoom_fingerprint PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_zoom_fingerprint PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_zoom_fingerprint PRIVATE ants_test_paths)

add_test(NAME test_zoom_fingerprint COMMAND test_zoom_fingerprint)
set_tests_properties(test_zoom_fingerprint PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Prestart view (a fix of v0.2.0): the ants behind the "Get ready to play!" dialog are drawn (every team's, local and network, classic and wide), the first tick leaves their pixels as they are, and
# nothing moves before it
add_executable(test_prestart_view
    test_prestart_view.cpp
)

target_include_directories(test_prestart_view PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_prestart_view PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_prestart_view PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_prestart_view PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_prestart_view PRIVATE ants_test_paths)

add_test(NAME test_prestart_view COMMAND test_prestart_view)
set_tests_properties(test_prestart_view PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The keys of the seats of a server's room (rejoin_store.hpp): the desktop's file (format, broken lines, 24 hours, 8 entries, replace, forget, mode 0600, the atomic write) and the browser's
# local storage backend over a map. No window, no sockets
add_executable(test_rejoin_store
    test_rejoin_store.cpp
)

target_include_directories(test_rejoin_store PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_rejoin_store PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_rejoin_store PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_rejoin_store PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_rejoin_store PRIVATE ants_test_paths)

add_test(NAME test_rejoin_store COMMAND test_rejoin_store)
set_tests_properties(test_rejoin_store PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The screens of the way back and the keys' storage in the application (docs/NETWORK_PORT.md "What the clients do in release B"): a headless Application in a room of a real RoomManager
# (ants_server_core) over loopback sockets with a bare second machine: the overlay, the vote block, the catch-up screen, the start of a rejoin, leaving while held, the keys' file, the start menu's Rejoin
add_executable(test_rejoin_app
    test_rejoin_app.cpp
)

target_include_directories(test_rejoin_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# (ants_server_core brings ants_net, ants_ai, ants_sim and ants_assets with it)
target_link_libraries(test_rejoin_app PRIVATE
    ants_app
    ants_server_core
)

target_link_libraries(test_rejoin_app PRIVATE ants_test_paths)

if(APPLE)
    target_link_libraries(test_rejoin_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_rejoin_app PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_rejoin_app COMMAND test_rejoin_app)
set_tests_properties(test_rejoin_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Touch model: what one finger or two do on the match screen (tap, drag, hold, the minimap, the pan and the pinch, the second and the third finger, cancels, the clock and the slop's size):
# finger events in, actions out, with an injected clock (pure: no SDL, no window)
add_executable(test_touch_model
    test_touch_model.cpp
)

target_include_directories(test_touch_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_touch_model PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_touch_model PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_touch_model PRIVATE ws2_32 winmm)
endif()

add_test(NAME test_touch_model COMMAND test_touch_model)
set_tests_properties(test_touch_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Touch controls in the application: synthetic finger events through the real event loop of a headless application (tap, drag and hold against the mouse's click, band and right click, the pan and the
# pinch, the places where two fingers do nothing, cancels, the late press, the other screens, the window's letterbox, the slop's size)
add_executable(test_touch_app
    test_touch_app.cpp
)

target_include_directories(test_touch_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_touch_app PRIVATE
    ants_app
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_touch_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_touch_app PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_touch_app PRIVATE ants_test_paths)

add_test(NAME test_touch_app COMMAND test_touch_app)
set_tests_properties(test_touch_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Replays in the application: a headless application plays a game on this computer and a match of the network (the other machine is a bare NetGame over loopback sockets); the file that it keeps
# of each is played again by the replay player on the engine alone and must reach the state in which the match ended
add_executable(test_replay_app
    test_replay_app.cpp
)

target_include_directories(test_replay_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_replay_app PRIVATE
    ants_app
    ants_replay
    ants_ai
    ants_net
    ants_sim
    ants_assets
)

if(APPLE)
    target_link_libraries(test_replay_app PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(test_replay_app PRIVATE ws2_32 winmm)
endif()

target_link_libraries(test_replay_app PRIVATE ants_test_paths)

add_test(NAME test_replay_app COMMAND test_replay_app)
set_tests_properties(test_replay_app PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
