// ants_db: a thin C++17 layer over SQLite (docs/SERVER.md "The database"). No exceptions: every call that can fail returns an Error that carries SQLite's own message.
//
//   Database db;
//   if (Error e = db.open("/results/ants.db"); !e.ok()) { ... e.message ... }
//   db.exec("CREATE TABLE t(a INTEGER)");
//   Statement st; db.prepare(st, "INSERT INTO t(a) VALUES (?)"); st.bind_int64(1, 7); st.step();
//   { Transaction tx(db); db.exec("..."); tx.commit(); }          // rolled back by the destructor unless committed
//
// One thread at a time: the library is built without mutexes (third_party/sqlite/CMakeLists.txt). Paths are UTF-8. A file that is not a database, or is damaged, makes open() fail and is
// never changed or deleted by this layer: the caller decides what to do with it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ants::db {

// Own error codes (negative, so that they never clash with SQLite's positive result codes)
constexpr int kErrNewerSchema = -1;         // the file was made by a newer program
constexpr int kErrBadMigrations = -2;       // the list of migrations is not in order (a bug of the program)
constexpr int kErrNested = -3;              // a transaction is open already
constexpr int kErrNotOpen = -4;             // the database (or statement) is not open
constexpr int kErrRange = -5;               // a column or parameter index out of range
constexpr int kErrForeign = -6;             // a database that this program did not make (it has tables and no schema_version)

struct Error {
    int code{0};                           // 0 = no error; SQLite's result code, or one of the kErr values
    std::string message;                   // SQLite's message, with the context in front of it
    bool ok() const { return code == 0; }
};

struct OpenOptions {
    bool create{true};                     // make the file when it does not exist (false: a missing file is an error)
    int busy_timeout_ms{5000};             // how long a statement waits for another connection's lock
    bool wal{true};                        // ask for journal_mode=WAL with synchronous=NORMAL right at open (not for ":memory:"). A file system that cannot do WAL (it needs a shared-memory file)
                                           // is NOT an error: the file keeps its rollback journal and journal_mode() says so. open_and_migrate() asks AFTER the version check instead
    std::string journal_request{"WAL"};    // what is asked for (a test asks for something a file cannot have, to see the fallback)
};

class Statement;

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Opens a file or ":memory:". On failure the object stays closed (and the file untouched when it is not a database).
    Error open(const std::string& path, const OpenOptions& options = OpenOptions());
    Error close();                          // idempotent; also done by the destructor (errors are lost there)
    bool is_open() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    Error exec(const std::string& sql);     // one or more statements, no result rows
    Error prepare(Statement& out, const std::string& sql);

    int64_t last_insert_rowid() const;
    int changes() const;                    // rows changed by the last INSERT, UPDATE or DELETE
    bool in_transaction() const;
    std::string journal_mode();             // "wal", "memory", ... ("" when closed)

    // A consistent copy of the live database into a new file (SQLite's online backup), to be made while the server runs. The target is written beside itself first and renamed when complete.
    Error backup_to(const std::string& path);

    // Asks for a journal mode ("WAL"): `mode` receives the one the file has afterwards, which differs from the request when the file system cannot give it (no error then). With "wal" the
    // synchronous level becomes NORMAL. This WRITES to the file header: for a file that is not yet known to be ours, call it only after the version check (open_and_migrate does).
    Error use_journal(const std::string& request, std::string& mode);

    sqlite3* handle() const { return db_; }

private:
    Error error_from(const char* context, int code) const;
    sqlite3* db_{nullptr};
    std::string path_;
};

class Statement {
public:
    enum class Step { Row, Done, Failed };

    Statement() = default;
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    bool valid() const { return stmt_ != nullptr; }

    // Parameters are numbered from 1, columns from 0
    Error bind_int64(int index, int64_t value);
    Error bind_double(int index, double value);
    Error bind_text(int index, std::string_view value);
    Error bind_blob(int index, const void* data, size_t size);
    Error bind_null(int index);

    Step step();                            // Failed: see error()
    Error reset();                          // runs the statement again from the start; the bindings stay
    Error clear_bindings();
    const Error& error() const { return last_; }

    int column_count() const;
    bool column_is_null(int column) const;
    int64_t column_int64(int column) const;
    double column_double(int column) const;
    std::string column_text(int column) const;
    std::vector<uint8_t> column_blob(int column) const;

private:
    friend class Database;
    Error fail(const char* context, int code);
    sqlite3_stmt* stmt_{nullptr};
    Error last_;
};

// BEGIN IMMEDIATE ... COMMIT. A Transaction that is not committed rolls back when it goes out of scope. A second one while one is open is refused (error() says so): transactions do not nest.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    const Error& error() const { return begin_error_; }      // the result of BEGIN: when it is not ok(), nothing is active
    bool active() const { return active_; }
    Error commit();
    Error rollback();                                       // a ROLLBACK that fails leaves the transaction active, so that the destructor tries again (its failure is ignored there)

private:
    Database* db_;
    bool active_{false};
    Error begin_error_;
};

}  // namespace ants::db
