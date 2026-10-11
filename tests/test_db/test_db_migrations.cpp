// Tests of the schema migrations (include/ants_db/migrations.hpp) and of the server's own schema (include/ants_server/db_schema.hpp).
#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/db_schema.hpp"
#include "db_check.hpp"

#include <string>

using namespace ants::db;
using dbtest::TempDir;

namespace {

const Migration kTwo[] = {
    {1, "first", "CREATE TABLE a (x INTEGER)"},
    {2, "second", "CREATE TABLE b (y INTEGER); INSERT INTO b VALUES (7)"},
};
const Migration kThree[] = {
    {1, "first", "CREATE TABLE a (x INTEGER)"},
    {2, "second", "CREATE TABLE b (y INTEGER); INSERT INTO b VALUES (7)"},
    {3, "third", "ALTER TABLE a ADD COLUMN z TEXT"},
};

bool has_table(Database& db, const std::string& name) {
    Statement st;
    if (!db.prepare(st, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?").ok()) return false;
    st.bind_text(1, name);
    return st.step() == Statement::Step::Row;
}

int version_of(Database& db) {
    int v = -1;
    return current_version(db, v).ok() ? v : -1;
}

int64_t rows_in(Database& db, const std::string& table) {
    Statement st;
    if (!db.prepare(st, "SELECT count(*) FROM " + table).ok() || st.step() != Statement::Step::Row) return -1;
    return st.column_int64(0);
}

}  // namespace

void run_migration_tests() {
    DB_CASE("DB6.1 A new database is brought to the newest version, each step recorded in schema_version with its name and time") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(version_of(db) == 0);
        DB_CHECK(!has_table(db, "schema_version"));              // (reading the version writes nothing)
        int v = 0;
        DB_CHECK(migrate(db, kTwo, &v).ok());
        DB_CHECK(v == 2 && version_of(db) == 2);
        DB_CHECK(has_table(db, "a") && has_table(db, "b"));
        DB_CHECK(rows_in(db, "b") == 1);
        Statement st;
        DB_CHECK(db.prepare(st, "SELECT version, name, applied_at FROM schema_version ORDER BY version").ok());
        DB_CHECK(st.step() == Statement::Step::Row && st.column_int64(0) == 1 && st.column_text(1) == "first");
        DB_CHECK(st.column_text(2).size() == 20 && st.column_text(2).back() == 'Z');
        DB_CHECK(st.step() == Statement::Step::Row && st.column_int64(0) == 2 && st.column_text(1) == "second");
        DB_CHECK(st.step() == Statement::Step::Done);
    } DB_END;

    DB_CASE("DB6.2 Run again, it changes nothing (idempotent); a database that is up to date is not written") {
        TempDir dir;
        Database db;
        DB_CHECK(db.open(dir.file("m.db")).ok());
        DB_CHECK(migrate(db, kTwo).ok());
        const std::string before = [&] { db.close(); return dbtest::read_all(dir.file("m.db")); }();
        DB_CHECK(db.open(dir.file("m.db")).ok());
        int v = 0;
        DB_CHECK(migrate(db, kTwo, &v).ok() && v == 2);
        DB_CHECK(migrate(db, kTwo).ok());
        DB_CHECK(rows_in(db, "schema_version") == 2 && rows_in(db, "b") == 1);
        db.close();
        DB_CHECK(dbtest::read_all(dir.file("m.db")) == before);
    } DB_END;

    DB_CASE("DB6.3 A database at version 2 gets only the third (partial), and the file survives a reopen between the steps") {
        TempDir dir;
        {
            Database db;
            DB_CHECK(db.open(dir.file("p.db")).ok());
            DB_CHECK(migrate(db, kTwo).ok());
        }
        Database db;
        DB_CHECK(db.open(dir.file("p.db")).ok());
        int v = 0;
        DB_CHECK(migrate(db, kThree, &v).ok() && v == 3);
        DB_CHECK(rows_in(db, "schema_version") == 3 && rows_in(db, "b") == 1);
        DB_CHECK(db.exec("INSERT INTO a (x, z) VALUES (1, 'new column')").ok());
    } DB_END;

    DB_CASE("DB6.4 A database of a NEWER program is refused with a clear error, before any write; the file is byte for byte as it was") {
        TempDir dir;
        {
            Database db;
            DB_CHECK(db.open(dir.file("n.db")).ok());
            DB_CHECK(migrate(db, kThree).ok());
        }
        Database db;
        DB_CHECK(db.open(dir.file("n.db")).ok());
        db.close();
        const std::string reopened = dbtest::read_all(dir.file("n.db"));
        DB_CHECK(db.open(dir.file("n.db")).ok());
        int v = 0;
        const Error e = migrate(db, kTwo, &v);
        DB_CHECK(e.code == kErrNewerSchema);
        DB_CHECK(e.message.find("newer") != std::string::npos && e.message.find('3') != std::string::npos && e.message.find('2') != std::string::npos);
        DB_CHECK(v == 3);
        DB_CHECK(version_of(db) == 3 && !has_table(db, "extra"));
        DB_CHECK(rows_in(db, "schema_version") == 3);
        db.close();
        DB_CHECK(dbtest::read_all(dir.file("n.db")) == reopened);
    } DB_END;

    DB_CASE("DB6.5 A migration that fails rolls back WHOLE (its tables, its rows and its version row) and stops the later ones; the earlier ones stay") {
        const Migration bad[] = {
            {1, "good", "CREATE TABLE a (x INTEGER)"},
            {2, "half", "CREATE TABLE half (y INTEGER); INSERT INTO half VALUES (1); INSERT INTO no_such_table VALUES (1)"},
            {3, "never", "CREATE TABLE never (z INTEGER)"},
        };
        TempDir dir;
        Database db;
        DB_CHECK(db.open(dir.file("f.db")).ok());
        int v = 0;
        const Error e = migrate(db, bad, &v);
        DB_CHECK(!e.ok());
        DB_CHECK(e.message.find("migration 2 (half)") != std::string::npos && e.message.find("no_such_table") != std::string::npos && e.message.find("rolled back") != std::string::npos);
        DB_CHECK(v == 1 && version_of(db) == 1);
        DB_CHECK(has_table(db, "a") && !has_table(db, "half") && !has_table(db, "never"));
        DB_CHECK(!db.in_transaction());
        const Migration fixed[] = {bad[0], {2, "half", "CREATE TABLE half (y INTEGER)"}, bad[2]};
        DB_CHECK(migrate(db, fixed, &v).ok() && v == 3);                 // (a fixed list goes on from where it stopped)
        DB_CHECK(has_table(db, "half") && has_table(db, "never"));
    } DB_END;

    DB_CASE("DB6.6 A list that is not in order (a repeat, a smaller number, zero, no SQL) is refused and nothing is written") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        const Migration repeat[] = {{1, "a", "CREATE TABLE a (x)"}, {1, "b", "CREATE TABLE b (x)"}};
        const Migration backwards[] = {{2, "a", "CREATE TABLE a (x)"}, {1, "b", "CREATE TABLE b (x)"}};
        const Migration zero[] = {{0, "a", "CREATE TABLE a (x)"}};
        const Migration nosql[] = {{1, "a", nullptr}};
        DB_CHECK(migrate(db, repeat).code == kErrBadMigrations);
        DB_CHECK(migrate(db, backwards).code == kErrBadMigrations);
        DB_CHECK(migrate(db, zero).code == kErrBadMigrations);
        DB_CHECK(migrate(db, nosql).code == kErrBadMigrations);
        DB_CHECK(!has_table(db, "a") && !has_table(db, "schema_version"));
        DB_CHECK(migrate(db, nullptr, 0).ok());                           // an empty list is a list that knows nothing: fine on a new database
        DB_CHECK(!has_table(db, "schema_version"));
    } DB_END;

    DB_CASE("DB6.7 Gaps in the numbers are allowed; a vector works as well as an array") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        const std::vector<Migration> list = {{5, "five", "CREATE TABLE a (x)"}, {9, "nine", "CREATE TABLE b (x)"}};
        int v = 0;
        DB_CHECK(migrate(db, list, &v).ok() && v == 9);
        DB_CHECK(has_table(db, "a") && has_table(db, "b"));
    } DB_END;

    DB_CASE("SV1.1 The server's schema: version 1 makes the meta table with created_at, and nothing else of its own") {
        const auto& list = ants::server::db_migrations();
        DB_CHECK(!list.empty() && list.front().version == 1);
        DB_CHECK(ants::server::db_schema_version() == list.back().version);
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        int v = 0;
        DB_CHECK(migrate(db, list, &v).ok() && v == ants::server::db_schema_version());
        DB_CHECK(has_table(db, "meta") && has_table(db, "schema_version"));
        Statement st;
        DB_CHECK(db.prepare(st, "SELECT value FROM meta WHERE key='created_at'").ok());
        DB_CHECK(st.step() == Statement::Step::Row);
        const std::string created = st.column_text(0);
        DB_CHECK(created.size() == 20 && created.substr(0, 2) == "20" && created.back() == 'Z');
        DB_CHECK(db.prepare(st, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT IN ('meta', 'schema_version')").ok());
        DB_CHECK(st.step() == Statement::Step::Row && st.column_int64(0) == 0);       // (no data tables yet)
        DB_CHECK(migrate(db, list).ok());
        DB_CHECK(rows_in(db, "meta") == 1);
    } DB_END;
}
