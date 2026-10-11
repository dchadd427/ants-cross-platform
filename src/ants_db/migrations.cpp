// ants_db: schema migrations. See include/ants_db/migrations.hpp.
#include "ants_db/migrations.hpp"

#include <string>

namespace ants::db {

namespace {

Error fail(int code, const std::string& message) {
    Error e;
    e.code = code;
    e.message = message;
    return e;
}

}  // namespace

Error current_version(Database& db, int& version) {
    version = 0;
    Statement st;
    Error e = db.prepare(st, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_version'");
    if (!e.ok()) return e;
    const Statement::Step has_table = st.step();
    if (has_table == Statement::Step::Failed) return st.error();
    if (has_table == Statement::Step::Done) return Error();            // no table yet: a new file, version 0
    e = db.prepare(st, "SELECT COALESCE(MAX(version), 0) FROM schema_version");
    if (!e.ok()) return e;
    if (st.step() != Statement::Step::Row) return st.error();
    version = static_cast<int>(st.column_int64(0));
    return Error();
}

Error migrate(Database& db, const Migration* migrations, size_t count, int* version) {
    int have = 0;
    if (version != nullptr) *version = 0;
    int previous = 0;
    for (size_t i = 0; i < count; ++i) {
        if (migrations[i].version <= previous || migrations[i].sql == nullptr) {
            return fail(kErrBadMigrations, "the list of migrations is not in order: version " + std::to_string(migrations[i].version) + " comes after " + std::to_string(previous));
        }
        previous = migrations[i].version;
    }
    const int newest = previous;
    Error e = current_version(db, have);
    if (!e.ok()) return fail(e.code, "reading the schema version: " + e.message);
    if (version != nullptr) *version = have;
    if (have == 0) {                                          // no schema_version: only an empty file may be made ours
        Statement objects;
        e = db.prepare(objects, "SELECT count(*) FROM sqlite_master");
        if (!e.ok()) return fail(e.code, "reading the schema: " + e.message);
        if (objects.step() != Statement::Step::Row) return fail(objects.error().code, "reading the schema: " + objects.error().message);
        if (objects.column_int64(0) > 0 && count > 0) {
            return fail(kErrForeign, "the database was not made by this program (it has tables and no schema_version); nothing was changed");
        }
    }
    if (have > newest) {
        return fail(kErrNewerSchema, "the database has schema version " + std::to_string(have) + ", newer than the " + std::to_string(newest) +
                                         " that this program knows: it was made by a newer program; nothing was changed");
    }
    for (size_t i = 0; i < count; ++i) {
        const Migration& m = migrations[i];
        if (m.version <= have) continue;
        const std::string label = "migration " + std::to_string(m.version) + " (" + (m.name != nullptr ? m.name : "") + ")";
        Transaction tx(db);
        if (!tx.error().ok()) return fail(tx.error().code, label + ": " + tx.error().message);
        int inside = 0;
        e = current_version(db, inside);                      // (under the write lock: another process may have applied it meanwhile)
        if (!e.ok()) return fail(e.code, label + ": " + e.message);
        if (inside >= m.version) {
            have = inside;
            continue;
        }
        e = db.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)");
        if (e.ok()) e = db.exec(m.sql);
        if (e.ok()) {
            Statement st;
            e = db.prepare(st, "INSERT INTO schema_version (version, name, applied_at) VALUES (?, ?, strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))");
            if (e.ok()) e = st.bind_int64(1, m.version);
            if (e.ok()) e = st.bind_text(2, m.name != nullptr ? m.name : "");
            if (e.ok() && st.step() == Statement::Step::Failed) e = st.error();
        }
        if (e.ok()) e = tx.commit();
        if (!e.ok()) return fail(e.code, label + " failed and was rolled back: " + e.message);       // (the destructor of tx rolls back)
        have = m.version;
        if (version != nullptr) *version = have;
    }
    return Error();
}

Error migrate(Database& db, const std::vector<Migration>& migrations, int* version) { return migrate(db, migrations.data(), migrations.size(), version); }

Error open_and_migrate(Database& db, const std::string& path, const std::vector<Migration>& migrations, const OpenOptions& options, int* version, std::string* journal_mode) {
    OpenOptions plain = options;
    plain.wal = false;                                       // (the journal mode is a write to the file header: not before the file is known to be ours)
    Error e = db.open(path, plain);
    if (!e.ok()) return e;
    e = migrate(db, migrations, version);
    if (!e.ok()) {
        db.close();
        return e;
    }
    std::string mode = db.journal_mode();
    if (options.wal && path != ":memory:") {
        e = db.use_journal(options.journal_request, mode);
        if (!e.ok()) {
            db.close();
            return e;
        }
    }
    if (journal_mode != nullptr) *journal_mode = mode;
    return Error();
}

}  // namespace ants::db
