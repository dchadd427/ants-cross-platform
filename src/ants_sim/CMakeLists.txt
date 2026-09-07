add_library(ants_sim STATIC
    grid.cpp
    ant_unit.cpp
    combat_ai.cpp
    physics.cpp
    sim_engine.cpp
)

target_include_directories(ants_sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_sim PUBLIC
    ants_assets
)

set_target_properties(ants_sim PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_sim"
)
