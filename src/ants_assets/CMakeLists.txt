add_library(ants_assets STATIC
    mirroring.cpp
    chd_parser.cpp
    lvl_parser.cpp
    asset_archive.cpp
)

target_include_directories(ants_assets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(ants_assets PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_assets"
)
