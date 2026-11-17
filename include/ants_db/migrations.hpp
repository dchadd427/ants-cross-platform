// Schema migrations (docs/SERVER.md "The database"): an ordered list of (version, SQL); the table schema_version holds the versions that were applied.
#pragma once

#include "ants_db/database.hpp"

#include <cstddef>
#include <vector>

namespace ants::db {

struct Migration {
    int version{0};                         // 1, 2, 3 ... strictly increasing, no gaps needed but each larger than the one before
    const char* name{""};                   // for the error message and the table
    const char* sql{""};                    // one or more statements; run inside one transaction with the row of schema_version
    bool vacuum_after{false};               // a migration that drops or blanks private data (accounts, hashes, names): once it is committed, VACUUM rewrites the file so that the dropped bytes are not left in free pages,
                                            // and a WAL file is checkpointed and truncated. A cleaning that fails is the error kErrCleaning (the migration stays applied, `version` says so, and the next start does not clean again:
                                            // the way back is the copy of the file that was made before the update)
    constexpr Migration() = default;
    constexpr Migration(int v, const char* n, const char* s, bool vacuum = false) : version(v), name(n), sql(s), vacuum_after(vacuum) {}
};

// The version the database has now (0: a new file, or no schema_version table yet). Does not write.
Error current_version(Database& db, int& version);

// A database that has tables and no schema_version was not made by this program (a foreign SQLite file): refused with kErrForeign, before any write.
// Applies every migration whose version is higher than the database's, each in a transaction of its own (a failing one is rolled back whole and the later ones are not tried).
// Refuses with kErrNewerSchema, before any write, a database whose version is higher than the last migration of the list; refuses with kErrBadMigrations a list that is not in order.
// Idempotent. `version` (optional) receives the version the database has afterwards (also after a failure). A migration with `vacuum_after` is followed, outside its transaction, by VACUUM and a checkpoint.
Error migrate(Database& db, const Migration* migrations, size_t count, int* version = nullptr);
Error migrate(Database& db, const std::vector<Migration>& migrations, int* version = nullptr);
template <size_t N>
Error migrate(Database& db, const Migration (&migrations)[N], int* version = nullptr) {
    return migrate(db, migrations, N, version);
}

// The way a program opens its file: open WITHOUT touching the journal mode, check and migrate (a junk file, a foreign SQLite file and a database of a newer program are refused with the file
// byte for byte as it was, and the object closed), and only then ask for WAL (options.wal; a file system without WAL keeps its rollback journal: `journal_mode` says which one is in use).
Error open_and_migrate(Database& db, const std::string& path, const std::vector<Migration>& migrations, const OpenOptions& options, int* version = nullptr, std::string* journal_mode = nullptr);

}  // namespace ants::db
