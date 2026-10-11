# The ants_db suite (run_tests.sh suite 2.31): the SQLite wrapper (open, close, WAL, statements and values, transactions, a file that is no database, the backup), the schema migrations and the
# schema of the server (ants_server_core: its list of migrations). No network, no game: files in a temporary folder and memory databases.
add_executable(test_db
    test_db.cpp
    test_db_migrations.cpp
)

target_link_libraries(test_db PRIVATE
    ants_server_core
    ants_db
)

target_include_directories(test_db PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(NAME test_db COMMAND test_db)
set_tests_properties(test_db PROPERTIES COST 2)
