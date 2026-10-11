// Tests of ants_db (include/ants_db/database.hpp): open and close, ":memory:", the WAL setup, exec, prepared statements with every kind of value, transactions, a file that is no database,
// and the online backup. The migrations are in test_db_migrations.cpp.
#include "ants_db/database.hpp"
#include "db_check.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace ants::db;
using dbtest::TempDir;

namespace {

int64_t count_rows(Database& db, const std::string& table) {
    Statement st;
    if (!db.prepare(st, "SELECT count(*) FROM " + table).ok() || st.step() != Statement::Step::Row) return -1;
    return st.column_int64(0);
}

void open_and_close_cases() {
    DB_CASE("DB1.1 A file is made on open, is a database, and closes cleanly (twice is fine)") {
        TempDir dir;
        Database db;
        DB_CHECK(!db.is_open());
        const Error e = db.open(dir.file("a.db"));
        DB_CHECK(e.ok());
        DB_CHECK(db.is_open());
        DB_CHECK(db.path() == dir.file("a.db"));
        DB_CHECK(std::filesystem::exists(dir.file("a.db")));
        DB_CHECK(db.close().ok());
        DB_CHECK(!db.is_open());
        DB_CHECK(db.close().ok());
        DB_CHECK(!db.exec("SELECT 1").ok());              // a closed database says so
        DB_CHECK(db.exec("SELECT 1").code == kErrNotOpen);
    } DB_END;

    DB_CASE("DB1.2 :memory: opens, holds data, and needs no file; open twice on one object is refused") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE t (a INTEGER); INSERT INTO t VALUES (1), (2)").ok());
        DB_CHECK(count_rows(db, "t") == 2);
        DB_CHECK(db.journal_mode() == "memory");
        DB_CHECK(!db.open(":memory:").ok());
        DB_CHECK(db.is_open());
    } DB_END;

    DB_CASE("DB1.3 A file database is in WAL mode with synchronous NORMAL, foreign keys are on, and the mode survives a reopen") {
        TempDir dir;
        {
            Database db;
            DB_CHECK(db.open(dir.file("w.db")).ok());
            DB_CHECK(db.journal_mode() == "wal");
            Statement st;
            DB_CHECK(db.prepare(st, "PRAGMA synchronous").ok());
            DB_CHECK(st.step() == Statement::Step::Row);
            DB_CHECK(st.column_int64(0) == 1);              // 1 = NORMAL
            DB_CHECK(db.prepare(st, "PRAGMA foreign_keys").ok());
            DB_CHECK(st.step() == Statement::Step::Row);
            DB_CHECK(st.column_int64(0) == 1);
        }
        Database again;
        DB_CHECK(again.open(dir.file("w.db")).ok());
        DB_CHECK(again.journal_mode() == "wal");
    } DB_END;

    DB_CASE("DB1.4 Without WAL the default journal stays; create=false refuses a missing file and makes none; an empty name is refused") {
        TempDir dir;
        Database db;
        OpenOptions plain;
        plain.wal = false;
        DB_CHECK(db.open(dir.file("p.db"), plain).ok());
        DB_CHECK(db.journal_mode() == "delete");
        db.close();
        OpenOptions existing;
        existing.create = false;
        const Error missing = db.open(dir.file("none.db"), existing);
        DB_CHECK(!missing.ok());
        DB_CHECK(missing.message.find("none.db") != std::string::npos);
        DB_CHECK(!std::filesystem::exists(dir.file("none.db")));
        DB_CHECK(!db.open("").ok());
    } DB_END;

    DB_CASE("DB1.5 A folder that does not exist gives an error with the path and SQLite's reason") {
        TempDir dir;
        Database db;
        const Error e = db.open(dir.file("no/such/folder/x.db"));
        DB_CHECK(!e.ok());
        DB_CHECK(e.code != 0);
        DB_CHECK(e.message.find("x.db") != std::string::npos);
        DB_CHECK(e.message.size() > 20);
        DB_CHECK(!db.is_open());
    } DB_END;

    DB_CASE("DB1.6 A database moves (the old object is closed) and a bad SQL text is an error with SQLite's message, not an exception") {
        Database a;
        DB_CHECK(a.open(":memory:").ok());
        Database b(std::move(a));
        DB_CHECK(!a.is_open() && b.is_open());
        const Error e = b.exec("CREATE TABEL oops (x)");
        DB_CHECK(!e.ok());
        DB_CHECK(e.message.find("syntax error") != std::string::npos);
        Statement st;
        const Error p = b.prepare(st, "SELECT * FROM missing_table");
        DB_CHECK(!p.ok() && p.message.find("missing_table") != std::string::npos);
        DB_CHECK(!st.valid());
    } DB_END;
}

void fallback_cases() {
    DB_CASE("DB1.7 A journal mode that the file cannot have (as a file system without WAL) is no error: the rollback journal stays and the mode is reported") {
        TempDir dir;
        OpenOptions odd;
        odd.journal_request = "NO_SUCH_MODE";                    // SQLite answers with the mode it keeps, as it does when WAL is impossible
        Database db;
        DB_CHECK(db.open(dir.file("f.db"), odd).ok());
        DB_CHECK(db.journal_mode() == "delete");
        std::string mode;
        DB_CHECK(db.use_journal("WAL", mode).ok() && mode == "wal");        // (and a real request works afterwards)
        DB_CHECK(db.use_journal("DELETE", mode).ok() && mode == "delete");
        db.close();
        DB_CHECK(!db.use_journal("WAL", mode).ok());
    } DB_END;
}

void statement_cases() {
    DB_CASE("DB2.1 Every kind of value goes in and comes out: int64 limits, double, text with a NUL and UTF-8, a blob with zero bytes, an empty blob, NULL") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE v (i INTEGER, d REAL, t TEXT, b BLOB, n TEXT)").ok());
        const std::string text("a\0b \xC3\xA4\xE2\x82\xAC", 9);
        const std::vector<uint8_t> blob = {0, 1, 0, 255, 0};
        Statement ins;
        DB_CHECK(db.prepare(ins, "INSERT INTO v VALUES (?1, ?2, ?3, ?4, ?5)").ok());
        DB_CHECK(ins.bind_int64(1, INT64_MIN).ok());
        DB_CHECK(ins.bind_double(2, 0.1).ok());
        DB_CHECK(ins.bind_text(3, text).ok());
        DB_CHECK(ins.bind_blob(4, blob.data(), blob.size()).ok());
        DB_CHECK(ins.bind_null(5).ok());
        DB_CHECK(ins.step() == Statement::Step::Done);
        DB_CHECK(db.changes() == 1 && db.last_insert_rowid() == 1);
        DB_CHECK(ins.reset().ok());
        DB_CHECK(ins.bind_int64(1, INT64_MAX).ok());
        DB_CHECK(ins.bind_double(2, -2.5e300).ok());
        DB_CHECK(ins.bind_text(3, "").ok());
        DB_CHECK(ins.bind_blob(4, nullptr, 0).ok());
        DB_CHECK(ins.bind_null(5).ok());
        DB_CHECK(ins.step() == Statement::Step::Done);

        Statement sel;
        DB_CHECK(db.prepare(sel, "SELECT i, d, t, b, n FROM v ORDER BY rowid").ok());
        DB_CHECK(sel.column_count() == 5);
        DB_CHECK(sel.step() == Statement::Step::Row);
        DB_CHECK(sel.column_int64(0) == INT64_MIN);
        DB_CHECK(sel.column_double(1) == 0.1);
        DB_CHECK(sel.column_text(2) == text);
        DB_CHECK(sel.column_text(2).size() == 9);
        DB_CHECK(sel.column_blob(3) == blob);
        DB_CHECK(sel.column_is_null(4));
        DB_CHECK(!sel.column_is_null(0));
        DB_CHECK(sel.step() == Statement::Step::Row);
        DB_CHECK(sel.column_int64(0) == INT64_MAX);
        DB_CHECK(sel.column_double(1) == -2.5e300);
        DB_CHECK(sel.column_text(2).empty() && !sel.column_is_null(2));
        DB_CHECK(sel.column_blob(3).empty());
        DB_CHECK(sel.step() == Statement::Step::Done);
    } DB_END;

    DB_CASE("DB2.2 A bad index, an unprepared statement and a constraint failure are errors; the statement can be reset and run again; the text is bound, never pasted (an injection stays text)") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE u (k TEXT PRIMARY KEY, n INTEGER NOT NULL)").ok());
        Statement st;
        DB_CHECK(db.prepare(st, "INSERT INTO u VALUES (?, ?)").ok());
        DB_CHECK(!st.bind_int64(3, 1).ok());                  // only two parameters
        DB_CHECK(st.bind_text(1, "x'); DROP TABLE u; --").ok());
        DB_CHECK(st.bind_null(2).ok());
        DB_CHECK(st.step() == Statement::Step::Failed);        // NOT NULL
        DB_CHECK(!st.error().ok() && st.error().message.find("NOT NULL") != std::string::npos);
        DB_CHECK(!st.reset().ok());                             // (reset repeats the error of the last step)
        DB_CHECK(st.bind_int64(2, 5).ok());
        DB_CHECK(st.step() == Statement::Step::Done);
        DB_CHECK(st.reset().ok());
        DB_CHECK(st.step() == Statement::Step::Failed);        // the same key again: UNIQUE
        Statement none;
        DB_CHECK(!none.valid());
        DB_CHECK(!none.bind_int64(1, 1).ok());
        DB_CHECK(none.step() == Statement::Step::Failed);
        DB_CHECK(count_rows(db, "u") == 1);
        Statement key;
        DB_CHECK(db.prepare(key, "SELECT k FROM u").ok() && key.step() == Statement::Step::Row);
        DB_CHECK(key.column_text(0) == "x'); DROP TABLE u; --");
    } DB_END;

    DB_CASE("DB2.5 An empty text is the empty string, never NULL, also from a string_view with no data pointer") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        Statement st;
        DB_CHECK(db.prepare(st, "SELECT typeof(?1), length(?1)").ok());
        DB_CHECK(st.bind_text(1, std::string_view()).ok());
        DB_CHECK(st.step() == Statement::Step::Row);
        DB_CHECK(st.column_text(0) == "text" && st.column_int64(1) == 0);
        DB_CHECK(st.reset().ok() && st.bind_text(1, std::string_view("", 0)).ok() && st.step() == Statement::Step::Row);
        DB_CHECK(st.column_text(0) == "text");
        DB_CHECK(st.reset().ok() && st.bind_null(1).ok() && st.step() == Statement::Step::Row);
        DB_CHECK(st.column_text(0) == "null");
    } DB_END;

    DB_CASE("DB2.3 Foreign keys are enforced from the start") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE p (id INTEGER PRIMARY KEY); CREATE TABLE c (p INTEGER REFERENCES p(id))").ok());
        const Error e = db.exec("INSERT INTO c VALUES (42)");
        DB_CHECK(!e.ok() && e.message.find("FOREIGN KEY") != std::string::npos);
    } DB_END;

    DB_CASE("DB2.4 A double-quoted word is no string (SQLITE_DQS=0): a typo in a column name is an error") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE q (a TEXT)").ok());
        DB_CHECK(!db.exec("INSERT INTO q VALUES (\"literal\")").ok());
        DB_CHECK(db.exec("INSERT INTO q VALUES ('literal')").ok());
    } DB_END;
}

void transaction_cases() {
    DB_CASE("DB3.1 A committed transaction keeps its rows; one that goes out of scope rolls back") {
        TempDir dir;
        Database db;
        DB_CHECK(db.open(dir.file("t.db")).ok());
        DB_CHECK(db.exec("CREATE TABLE t (a INTEGER)").ok());
        {
            Transaction tx(db);
            DB_CHECK(tx.error().ok() && tx.active() && db.in_transaction());
            DB_CHECK(db.exec("INSERT INTO t VALUES (1)").ok());
            DB_CHECK(tx.commit().ok());
            DB_CHECK(!tx.active() && !db.in_transaction());
        }
        {
            Transaction tx(db);
            DB_CHECK(db.exec("INSERT INTO t VALUES (2)").ok());
            DB_CHECK(count_rows(db, "t") == 2);
        }                                                       // no commit
        DB_CHECK(!db.in_transaction());
        DB_CHECK(count_rows(db, "t") == 1);
        {
            Transaction tx(db);
            DB_CHECK(db.exec("INSERT INTO t VALUES (3)").ok());
            DB_CHECK(tx.rollback().ok());
            DB_CHECK(!tx.commit().ok());                        // nothing active any more
        }
        DB_CHECK(count_rows(db, "t") == 1);
    } DB_END;

    DB_CASE("DB3.2 Transactions do not nest: the second is refused, the first goes on, and a raw BEGIN counts too") {
        Database db;
        DB_CHECK(db.open(":memory:").ok());
        DB_CHECK(db.exec("CREATE TABLE t (a INTEGER)").ok());
        Transaction outer(db);
        DB_CHECK(outer.active());
        {
            Transaction inner(db);
            DB_CHECK(!inner.active());
            DB_CHECK(inner.error().code == kErrNested);
            DB_CHECK(!inner.commit().ok());
        }                                                       // (the refused one rolls nothing back)
        DB_CHECK(db.in_transaction());
        DB_CHECK(db.exec("INSERT INTO t VALUES (1)").ok());
        DB_CHECK(outer.commit().ok());
        DB_CHECK(db.exec("BEGIN").ok());
        Transaction after_raw(db);
        DB_CHECK(after_raw.error().code == kErrNested);
        DB_CHECK(db.exec("ROLLBACK").ok());
        DB_CHECK(count_rows(db, "t") == 1);
    } DB_END;

    DB_CASE("DB3.3 A write by a second connection waits for the busy timeout and then fails with SQLITE_BUSY, and succeeds once the first commits") {
        TempDir dir;
        OpenOptions quick;
        quick.busy_timeout_ms = 50;
        Database a, b;
        DB_CHECK(a.open(dir.file("b.db"), quick).ok());
        DB_CHECK(b.open(dir.file("b.db"), quick).ok());
        DB_CHECK(a.exec("CREATE TABLE t (a INTEGER)").ok());
        Transaction first(a);
        DB_CHECK(first.active());
        Transaction second(b);
        DB_CHECK(!second.active());
        DB_CHECK(second.error().code == 5);                     // SQLITE_BUSY
        DB_CHECK(first.commit().ok());
        Transaction third(b);
        DB_CHECK(third.active());
    } DB_END;
}

void file_cases() {
    DB_CASE("DB4.1 A file that is no database fails to open with a clear error and is not changed (nor are other files made beside it)") {
        TempDir dir;
        const std::string path = dir.file("junk.db");
        const std::string junk = "this is certainly not a database file, it is a sentence of more than a hundred bytes so that the header is read as a header and refused as one.\n";
        dbtest::write_all(path, junk);
        Database db;
        const Error e = db.open(path);
        DB_CHECK(!e.ok());
        DB_CHECK(e.message.find("junk.db") != std::string::npos);
        DB_CHECK(e.message.find("not a database") != std::string::npos);
        DB_CHECK(!db.is_open());
        DB_CHECK(dbtest::read_all(path) == junk);
        int files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
            (void)entry;
            ++files;
        }
        DB_CHECK(files == 1);
    } DB_END;

    DB_CASE("DB4.2 A real database with a damaged page 1 or a cut file fails to open, and the bytes stay as they were") {
        TempDir dir;
        const std::string good = dir.file("good.db");
        {
            Database db;
            DB_CHECK(db.open(good).ok());
            DB_CHECK(db.exec("CREATE TABLE t (a TEXT); INSERT INTO t VALUES ('x')").ok());
        }
        std::string bytes = dbtest::read_all(good);
        DB_CHECK(bytes.size() >= 4096);
        std::string damaged = bytes;
        for (size_t i = 0; i < 100; ++i) damaged[16 + i] = static_cast<char>(0xFF - i);       // the header after the magic text
        const std::string path = dir.file("bad.db");
        dbtest::write_all(path, damaged);
        Database db;
        DB_CHECK(!db.open(path).ok());
        DB_CHECK(dbtest::read_all(path) == damaged);
        std::string cut = bytes.substr(0, 60);
        dbtest::write_all(path, cut);
        DB_CHECK(!db.open(path).ok());
        DB_CHECK(dbtest::read_all(path) == cut);
    } DB_END;

    DB_CASE("DB4.3 An empty file is an empty database (SQLite's rule)") {
        TempDir dir;
        dbtest::write_all(dir.file("empty.db"), "");
        Database db;
        DB_CHECK(db.open(dir.file("empty.db")).ok());
        DB_CHECK(db.exec("CREATE TABLE t (a)").ok());
    } DB_END;

    DB_CASE("DB5.1 backup_to writes a readable, complete copy of a live WAL database, as one file, and leaves the source usable") {
        TempDir dir;
        Database db;
        DB_CHECK(db.open(dir.file("live.db")).ok());
        DB_CHECK(db.exec("CREATE TABLE t (a INTEGER, s TEXT)").ok());
        {
            Transaction tx(db);
            Statement ins;
            DB_CHECK(db.prepare(ins, "INSERT INTO t VALUES (?, ?)").ok());
            for (int i = 0; i < 500; ++i) {
                DB_CHECK(ins.reset().ok());
                DB_CHECK(ins.bind_int64(1, i).ok());
                DB_CHECK(ins.bind_text(2, std::string(100, 'x')).ok());
                DB_CHECK(ins.step() == Statement::Step::Done);
            }
            DB_CHECK(tx.commit().ok());
        }
        const Error e = db.backup_to(dir.file("copy.db"));
        DB_CHECK(e.ok());
        DB_CHECK(!std::filesystem::exists(dir.file("copy.db.partial")));
        DB_CHECK(!std::filesystem::exists(dir.file("copy.db-wal")));
        DB_CHECK(db.exec("INSERT INTO t VALUES (1000, 'later')").ok());       // (the source goes on; the copy does not see it)
        Database copy;
        DB_CHECK(copy.open(dir.file("copy.db")).ok());
        DB_CHECK(count_rows(copy, "t") == 500);
        DB_CHECK(count_rows(db, "t") == 501);
        copy.close();                                                          // (Windows cannot replace a file that a connection still holds open)
        DB_CHECK(db.backup_to(dir.file("copy.db")).ok());                      // (a second backup replaces the first)
        DB_CHECK(copy.open(dir.file("copy.db")).ok());
        DB_CHECK(count_rows(copy, "t") == 501);
    } DB_END;

    DB_CASE("DB5.2 backup_to refuses the database's own file, a folder that is not there and a closed database; a memory database can be saved") {
        TempDir dir;
        Database db;
        DB_CHECK(db.open(dir.file("own.db")).ok());
        DB_CHECK(!db.backup_to(dir.file("own.db")).ok());
        DB_CHECK(!db.backup_to(dir.file("no/such/dir/x.db")).ok());
        DB_CHECK(!std::filesystem::exists(dir.file("no")));
        DB_CHECK(!db.backup_to("").ok());
        for (const char* side : {"-wal", "-shm", "-journal"}) DB_CHECK(!db.backup_to(dir.file("own.db") + side).ok());
        DB_CHECK(!db.backup_to((dir.path / "." / "own.db").string()).ok());
        db.close();
        DB_CHECK(db.backup_to(dir.file("x.db")).code == kErrNotOpen);
        Database mem;
        DB_CHECK(mem.open(":memory:").ok());
        DB_CHECK(mem.exec("CREATE TABLE t (a); INSERT INTO t VALUES (7)").ok());
        DB_CHECK(mem.backup_to(dir.file("mem.db")).ok());
        Database saved;
        DB_CHECK(saved.open(dir.file("mem.db")).ok());
        DB_CHECK(count_rows(saved, "t") == 1);
    } DB_END;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [SUITE] ants_db: the SQLite wrapper, the transactions, the migrations and the server's schema\n"
                 "=======================================================\n";
    open_and_close_cases();
    fallback_cases();
    statement_cases();
    transaction_cases();
    file_cases();
    run_migration_tests();
    std::cout << "\n  " << dbtest::g_cases << " cases, " << dbtest::g_checks << " checks, " << dbtest::g_failures << " failed\n";
    return dbtest::g_failures == 0 ? 0 : 1;
}
