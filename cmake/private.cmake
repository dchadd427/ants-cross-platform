# The part of the project that is not in every copy of this tree: the dedicated game server and the libraries under it (SQLite, the database layer, the control interface), the history
# service, the picture maker and their tests. CMakeLists.txt includes this file at its end when it is there (include(... OPTIONAL)); a tree without it configures and builds the game, its
# libraries, the tools and their tests, and nothing in the files that every copy has names what is here. The conditions are those that these lines had in CMakeLists.txt: none of it for
# the web build; the picture maker and the application's server tests need the game's renderer (ANTS_BUILD_APP); the tests when BUILD_TESTS is on (the tests/ folder and enable_testing() are CMakeLists.txt's).
if(NOT EMSCRIPTEN)
    add_subdirectory(third_party/sqlite)  # SQLite (the amalgamation, public domain): the static library ants_sqlite
    add_subdirectory(src/ants_db)         # the database layer of the game server (SQLite behind a thin wrapper, schema migrations): no SDL, no sockets, no threads
    add_subdirectory(src/ants_ctl)        # control interface of the game server (strict JSON, authenticated HTTP on the loopback address): sockets, native only
    add_subdirectory(src/ants_server)     # the dedicated game server (rooms, a referee per room): native only
    add_subdirectory(src/ants_history)    # the history service (the match history of the recordings, counted on a thread of its own, and its public pages): native only
    if(ANTS_BUILD_APP)
        add_subdirectory(src/ants_thumb)  # the picture maker of the game server (a map file as the setup screen draws it, no window): ants_app's renderer on SDL's dummy driver
    endif()
    if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_subdirectory(tests/test_db)
        add_subdirectory(tests/test_ctl)
        add_subdirectory(tests/test_server)
        if(ANTS_BUILD_APP)
            add_subdirectory(tests/test_app_server)  # the application's tests that run a real room manager (four programs that link ants_server_core)
        endif()
    endif()
endif()
