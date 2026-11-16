# The history service (docs/HISTORY_SERVICE.md; native builds only: it needs sockets and a thread): a library with everything but main (so that the tests link it) and the program. It is made of the game server's own
# history code (ants_server_core: HistoryStore, HistoryFeeder, ReplayStore, the code of the public door's answers), so that the rows and the JSON are the same.
find_package(Threads REQUIRED)
add_library(ants_history_core STATIC
    history_service.cpp
    history_import.cpp
)
target_include_directories(ants_history_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ants_history_core PUBLIC
    ants_server_core
    ants_db
    ants_ctl
    Threads::Threads
)
set_target_properties(ants_history_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "ants_history_core"
)

add_executable(ants_history main.cpp)
target_link_libraries(ants_history PRIVATE ants_history_core ants_version)           # ants_version: ants::VERSION_STRING and ants::BUILD_ID (root CMakeLists.txt)
set_target_properties(ants_history PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
