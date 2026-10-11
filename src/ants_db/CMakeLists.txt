# The database layer of the game server (native builds only): a thin C++17 wrapper over SQLite (third_party/sqlite) and the schema migrations. No SDL, no sockets, no threads.
add_library(ants_db STATIC
    database.cpp
    migrations.cpp
)
target_include_directories(ants_db PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ants_db PRIVATE ants_sqlite)           # (sqlite3.h is used by the sources only: the headers of ants_db do not include it)
set_target_properties(ants_db PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
