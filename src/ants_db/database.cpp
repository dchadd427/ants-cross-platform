// ants_db: Database (open, close, exec, prepare, backup). See include/ants_db/database.hpp.
#include "ants_db/database.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <utility>

namespace ants::db {

namespace {

Error make_error(int code, const std::string& context, const char* message) {
    Error e;
    e.code = code;
    e.message = context;
    if (message != nullptr && *message != '\0') {
        e.message += ": ";
        e.message += message;
    }
    return e;
}

// The text of a message that SQLite allocated for exec(), and frees it
std::string take_message(char* text) {
    std::string out = text != nullptr ? text : "";
    sqlite3_free(text);
    return out;
}

}  // namespace

Database::~Database() { close(); }

Database::Database(Database&& other) noexcept : db_(other.db_), path_(std::move(other.path_)) { other.db_ = nullptr; }

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        path_ = std::move(other.path_);
        other.db_ = nullptr;
    }
    return *this;
}

Error Database::error_from(const char* context, int code) const {
    return make_error(code, context, db_ != nullptr ? sqlite3_errmsg(db_) : nullptr);
}

Error Database::open(const std::string& path, const OpenOptions& options) {
    if (db_ != nullptr) return make_error(kErrNested, "open " + path, "this object has a database open already");
    if (path.empty()) return make_error(SQLITE_CANTOPEN, "open", "no file name");
    const bool memory = path == ":memory:";
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX;
    if (options.create) flags |= SQLITE_OPEN_CREATE;
    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        Error e = make_error(rc, "open " + path, handle != nullptr ? sqlite3_errmsg(handle) : sqlite3_errstr(rc));
        sqlite3_close_v2(handle);
        return e;
    }
    db_ = handle;
    path_ = path;
    // Every step below can find that the file is not a database or is damaged (the first read of page 1): then the object is closed again and the file is as it was
    auto fail = [&](const std::string& what, Error e) {
        e.message = "open " + path + ": " + what + ": " + e.message;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        path_.clear();
        return e;
    };
    sqlite3_busy_timeout(db_, options.busy_timeout_ms);
    Error e = exec("PRAGMA foreign_keys=ON");
    if (!e.ok()) return fail("foreign keys", e);
    e = exec("SELECT count(*) FROM sqlite_master");                 // reads the schema: a damaged file says so here (nothing has been written yet)
    if (!e.ok()) return fail("reading the file", e);
    if (options.wal && !memory) {
        std::string mode;
        e = use_journal(options.journal_request, mode);
        if (!e.ok()) return fail("journal mode", e);                 // (a mode other than the one asked for is no error: the file keeps its rollback journal)
    }
    return Error();
}

Error Database::use_journal(const std::string& request, std::string& mode) {
    mode.clear();
    if (db_ == nullptr) return make_error(kErrNotOpen, "journal mode", "the database is not open");
    Statement st;
    Error e = prepare(st, "PRAGMA journal_mode=" + request);
    if (!e.ok()) return e;
    if (st.step() != Statement::Step::Row) return st.error();
    mode = st.column_text(0);
    st = Statement();
    if (mode == "wal") return exec("PRAGMA synchronous=NORMAL");
    return Error();
}

Error Database::close() {
    if (db_ == nullptr) return Error();
    const int rc = sqlite3_close_v2(db_);          // (a statement that is still alive keeps the connection as a zombie until it is finalized)
    db_ = nullptr;
    path_.clear();
    return rc == SQLITE_OK ? Error() : make_error(rc, "close", sqlite3_errstr(rc));
}

Error Database::exec(const std::string& sql) {
    if (db_ == nullptr) return make_error(kErrNotOpen, "exec", "the database is not open");
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
    const std::string text = take_message(message);
    if (rc != SQLITE_OK) return make_error(rc, "exec", text.c_str());
    return Error();
}

Error Database::prepare(Statement& out, const std::string& sql) {
    out = Statement();
    if (db_ == nullptr) return make_error(kErrNotOpen, "prepare", "the database is not open");
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return error_from("prepare", rc);
    }
    if (stmt == nullptr) return make_error(SQLITE_MISUSE, "prepare", "the text holds no statement");
    out.stmt_ = stmt;
    return Error();
}

int64_t Database::last_insert_rowid() const { return db_ != nullptr ? sqlite3_last_insert_rowid(db_) : 0; }
int Database::changes() const { return db_ != nullptr ? sqlite3_changes(db_) : 0; }
bool Database::in_transaction() const { return db_ != nullptr && sqlite3_get_autocommit(db_) == 0; }

std::string Database::journal_mode() {
    Statement st;
    if (!prepare(st, "PRAGMA journal_mode").ok() || st.step() != Statement::Step::Row) return std::string();
    return st.column_text(0);
}

Error Database::backup_to(const std::string& path) {
    if (db_ == nullptr) return make_error(kErrNotOpen, "backup", "the database is not open");
    if (path.empty() || path == ":memory:") return make_error(SQLITE_CANTOPEN, "backup", "no file name");
    std::error_code ec;
    if (!path_.empty() && path_ != ":memory:") {
        const auto target = std::filesystem::absolute(std::filesystem::u8path(path), ec).lexically_normal();
        const auto own = std::filesystem::absolute(std::filesystem::u8path(path_), ec).lexically_normal();
        bool same = std::filesystem::equivalent(std::filesystem::u8path(path), std::filesystem::u8path(path_), ec);
        for (const char* suffix : {"", "-wal", "-shm", "-journal"}) {         // (the database's own side files too)
            if (target == std::filesystem::path(own.native() + std::filesystem::path(suffix).native())) same = true;
        }
        if (same) return make_error(SQLITE_MISUSE, "backup", "the target is the database itself or one of its side files");
    }
    const std::string temp = path + ".partial";           // (the backup is complete only when it is renamed)
    std::filesystem::remove(std::filesystem::u8path(temp), ec);        // (our own leftover of an earlier try)
    sqlite3* target = nullptr;
    int rc = sqlite3_open_v2(temp.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        Error e = make_error(rc, "backup: open " + temp, target != nullptr ? sqlite3_errmsg(target) : sqlite3_errstr(rc));
        sqlite3_close_v2(target);
        return e;
    }
    Error result;
    sqlite3_backup* job = sqlite3_backup_init(target, "main", db_, "main");
    if (job == nullptr) {
        result = make_error(sqlite3_errcode(target), "backup", sqlite3_errmsg(target));
    } else {
        rc = sqlite3_backup_step(job, -1);
        if (rc != SQLITE_DONE) result = make_error(rc, "backup", sqlite3_errstr(rc));
        rc = sqlite3_backup_finish(job);
        if (result.ok() && rc != SQLITE_OK) result = make_error(rc, "backup", sqlite3_errstr(rc));
    }
    sqlite3_exec(target, "PRAGMA journal_mode=DELETE", nullptr, nullptr, nullptr);       // (one self-contained file)
    // (the destination connection is closed completely before the rename: Windows cannot move a file that is open; std::filesystem::rename replaces an existing target on POSIX and on Windows)
    sqlite3_close_v2(target);
    if (result.ok()) {
        std::filesystem::rename(std::filesystem::u8path(temp), std::filesystem::u8path(path), ec);
        if (ec) result = make_error(SQLITE_IOERR, "backup: rename " + temp, ec.message().c_str());
    }
    if (!result.ok()) std::filesystem::remove(std::filesystem::u8path(temp), ec);
    return result;
}

// ---- Statement ----------------------------------------------------------------------------------------------------------------------------------------------------------------

Statement::~Statement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_), last_(std::move(other.last_)) { other.stmt_ = nullptr; }

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        last_ = std::move(other.last_);
        other.stmt_ = nullptr;
    }
    return *this;
}

Error Statement::fail(const char* context, int code) {
    last_ = make_error(code, context, stmt_ != nullptr ? sqlite3_errmsg(sqlite3_db_handle(stmt_)) : nullptr);
    return last_;
}

#define ANTS_DB_NEED_STATEMENT(context) \
    if (stmt_ == nullptr) return make_error(kErrNotOpen, context, "the statement is not prepared")

Error Statement::bind_int64(int index, int64_t value) {
    ANTS_DB_NEED_STATEMENT("bind");
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    return rc == SQLITE_OK ? Error() : fail("bind", rc);
}

Error Statement::bind_double(int index, double value) {
    ANTS_DB_NEED_STATEMENT("bind");
    const int rc = sqlite3_bind_double(stmt_, index, value);
    return rc == SQLITE_OK ? Error() : fail("bind", rc);
}

Error Statement::bind_text(int index, std::string_view value) {
    ANTS_DB_NEED_STATEMENT("bind");
    static const char kEmpty[] = "";                  // (an empty view with no data pointer is the empty string, never NULL: sqlite binds NULL for a null pointer)
    const int rc = sqlite3_bind_text64(stmt_, index, value.data() != nullptr ? value.data() : kEmpty, static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8);
    return rc == SQLITE_OK ? Error() : fail("bind", rc);
}

Error Statement::bind_blob(int index, const void* data, size_t size) {
    ANTS_DB_NEED_STATEMENT("bind");
    const int rc = size == 0 ? sqlite3_bind_zeroblob(stmt_, index, 0) : sqlite3_bind_blob64(stmt_, index, data, static_cast<sqlite3_uint64>(size), SQLITE_TRANSIENT);
    return rc == SQLITE_OK ? Error() : fail("bind", rc);
}

Error Statement::bind_null(int index) {
    ANTS_DB_NEED_STATEMENT("bind");
    const int rc = sqlite3_bind_null(stmt_, index);
    return rc == SQLITE_OK ? Error() : fail("bind", rc);
}

Statement::Step Statement::step() {
    if (stmt_ == nullptr) {
        last_ = make_error(kErrNotOpen, "step", "the statement is not prepared");
        return Step::Failed;
    }
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return Step::Row;
    if (rc == SQLITE_DONE) {
        last_ = Error();
        return Step::Done;
    }
    fail("step", rc);
    return Step::Failed;
}

Error Statement::reset() {
    ANTS_DB_NEED_STATEMENT("reset");
    const int rc = sqlite3_reset(stmt_);            // (returns the error of the last step again, if there was one)
    if (rc != SQLITE_OK) return fail("reset", rc);
    last_ = Error();
    return Error();
}

Error Statement::clear_bindings() {
    ANTS_DB_NEED_STATEMENT("clear_bindings");
    sqlite3_clear_bindings(stmt_);
    return Error();
}

int Statement::column_count() const { return stmt_ != nullptr ? sqlite3_column_count(stmt_) : 0; }

// (a column outside the result, or a statement without a row: the same answers as SQLite's own, NULL, 0 and "")
bool Statement::column_is_null(int column) const { return stmt_ == nullptr || sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
int64_t Statement::column_int64(int column) const { return stmt_ != nullptr ? sqlite3_column_int64(stmt_, column) : 0; }
double Statement::column_double(int column) const { return stmt_ != nullptr ? sqlite3_column_double(stmt_, column) : 0.0; }

std::string Statement::column_text(int column) const {
    if (stmt_ == nullptr) return std::string();
    const unsigned char* text = sqlite3_column_text(stmt_, column);
    const int size = sqlite3_column_bytes(stmt_, column);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(size)) : std::string();
}

std::vector<uint8_t> Statement::column_blob(int column) const {
    if (stmt_ == nullptr) return {};
    const void* data = sqlite3_column_blob(stmt_, column);
    const int size = sqlite3_column_bytes(stmt_, column);
    if (data == nullptr || size <= 0) return {};
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return std::vector<uint8_t>(bytes, bytes + size);
}

// ---- Transaction --------------------------------------------------------------------------------------------------------------------------------------------------------------

Transaction::Transaction(Database& db) : db_(&db) {
    if (db.in_transaction()) {
        begin_error_ = make_error(kErrNested, "begin", "a transaction is open already (they do not nest)");
        return;
    }
    begin_error_ = db.exec("BEGIN IMMEDIATE");
    active_ = begin_error_.ok();
}

Transaction::~Transaction() {
    if (active_) rollback();
}

Error Transaction::commit() {
    if (!active_) return make_error(kErrNotOpen, "commit", "no transaction is active");
    Error e = db_->exec("COMMIT");
    if (e.ok()) active_ = false;                     // (a failed COMMIT, for example a busy database, leaves the transaction open: the caller can retry or roll back)
    return e;
}

Error Transaction::rollback() {
    if (!active_) return make_error(kErrNotOpen, "rollback", "no transaction is active");
    if (!db_->in_transaction()) {                    // (SQLite rolled it back by itself after a full disk, for example)
        active_ = false;
        return Error();
    }
    Error e = db_->exec("ROLLBACK");
    if (e.ok()) active_ = false;                     // (a ROLLBACK that fails leaves the transaction active: the caller or the destructor tries again)
    return e;
}

}  // namespace ants::db
