# SQLite, the official amalgamation (public domain), as the static library ants_sqlite. Native builds only (the game server's database; the web build has none).
# The root project declares only C++, so C is enabled here (the amalgamation is one C file).
#
# The compile definitions, and why:
#   SQLITE_THREADSAFE=0      no mutexes at all: the server uses its database from its one main thread (docs/SERVER.md "The database"). A Database object must never be used from two threads;
#                            a later worker thread gets its own connection and this becomes 2 (and the target links Threads::Threads) in the same change.
#   SQLITE_OMIT_LOAD_EXTENSION  no loadable extensions: nothing can load code into the server, and no -ldl
#   SQLITE_DEFAULT_FOREIGN_KEYS=1  foreign keys are enforced from the first statement (the default of SQLite is off, for history)
#   SQLITE_DQS=0             "text" is an identifier only, never a string: a typo in a column name is an error, not a silent literal
#   SQLITE_DEFAULT_WAL_SYNCHRONOUS=1  synchronous=NORMAL in WAL mode (the wrapper sets it too)
#   SQLITE_DEFAULT_MEMSTATUS=0, SQLITE_OMIT_SHARED_CACHE, SQLITE_OMIT_DEPRECATED: smaller and faster, nothing the wrapper uses is gone
# No shell: shell.c is not vendored. The amalgamation is third-party code: its warnings are not ours (-w / /W0 after the project's flags, so -Werror / /WX have nothing to turn into errors).
enable_language(C)

add_library(ants_sqlite STATIC sqlite3.c)
target_include_directories(ants_sqlite SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ants_sqlite PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_OMIT_DEPRECATED
)
if(MSVC)
    target_compile_options(ants_sqlite PRIVATE /W0)
else()
    target_compile_options(ants_sqlite PRIVATE -w)
endif()
set_target_properties(ants_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99 C_EXTENSIONS ON)
