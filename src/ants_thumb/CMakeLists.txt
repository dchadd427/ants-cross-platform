# The picture maker of the game server (docs/MAP_SUBMISSIONS.md, "Pictures"): a program that draws a map file as the game's setup screen does, with no window, and writes a PNG. It is the game's own
# renderer (ants_app) on SDL's dummy video driver and software renderer, so the server's image needs SDL2 and SDL2_ttf and nothing else of the game. Native builds only.
add_executable(ants_thumb main.cpp)
target_link_libraries(ants_thumb PRIVATE ants_app)
set_target_properties(ants_thumb PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
if(WIN32 AND DEFINED SDL2_DLL)
    add_custom_command(TARGET ants_thumb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL}" "$<TARGET_FILE_DIR:ants_thumb>"
    )
endif()
if(WIN32 AND DEFINED SDL2_TTF_DLL)
    add_custom_command(TARGET ants_thumb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_TTF_DLL}" "$<TARGET_FILE_DIR:ants_thumb>"
    )
endif()
