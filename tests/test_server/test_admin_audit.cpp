// Tests of the admin page's log and of the step that retired its accounts (admin_audit.hpp, db_schema.cpp version 8, docs/MAP_SUBMISSIONS.md "The admin page"): what a line holds and what it does not, the order, the
// caps, the migration of a copy of a version 7 database (the accounts and their counts are gone, the decisions on maps are signed "earlier admin", the rest is as it was), and the privacy of the database files.
// The pass and the routes are test_admin_pass.cpp. No sockets.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../common/test_assert.hpp"
#include "ants_server/admin_audit.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/private_files.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n    " << (msg) << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int64_t scalar(db::Database& db, const std::string& sql) {
    db::Statement st;
    if (!db.prepare(st, sql).ok()) return -1;
    return st.step() == db::Statement::Step::Row ? st.column_int64(0) : -1;
}

std::string text_of(db::Database& db, const std::string& sql) {
    db::Statement st;
    if (!db.prepare(st, sql).ok()) return "<error>";
    return st.step() == db::Statement::Step::Row ? st.column_text(0) : std::string("<none>");
}

}  // namespace

int main() {
    std::cout << "=======================================================\n THE ADMIN PAGE'S LOG AND THE END OF ITS ACCOUNTS\n=======================================================\n";

    TEST_CASE("AUD1.1 A line holds who (username and reference), what, which map and a detail, and nothing about an address or a password; the newest line is first; a refused write is told once and the request goes on") {
        db::Database database;
        open_server_db(database, "");
        AdminAudit log(database);
        std::string why;
        ASSERT_TRUE(log.open(why));
        const Actor dave{"0123456789abcdef", "dave"};
        const Actor mia{"fedcba9876543210", "mia"};
        log.record(dave, kT0, "map_approved", "Run One", "");
        log.record(mia, kT0 + 5, "map_declined", "Run Two", "too small");
        const std::vector<AuditEntry> lines = log.list(200);
        ASSERT_EQ(lines.size(), size_t{2});
        ASSERT_TRUE(lines[0].actor == "mia" && lines[0].ref == "fedcba9876543210" && lines[0].action == "map_declined" && lines[0].target == "Run Two" && lines[0].detail == "too small" && lines[0].at == kT0 + 5);
        ASSERT_TRUE(lines[1].actor == "dave" && lines[1].ref == "0123456789abcdef" && lines[1].action == "map_approved");
        ASSERT_EQ(log.list(1).size(), size_t{1});
        ASSERT_EQ(scalar(database, "SELECT COUNT(*) FROM pragma_table_info('admin_audit') WHERE name IN ('addr_key', 'actor_id', 'pw_hash')"), int64_t{0});          // (no address, no account number)
        ASSERT_TRUE(log.take_notices().empty());
        ASSERT_TRUE(database.exec("DROP TABLE admin_audit").ok());
        log.record(dave, kT0, "map_approved", "x", "");
        ASSERT_EQ(log.take_notices().size(), size_t{1});
        ASSERT_TRUE(log.take_notices().empty());
        AdminAudit shut(database);
        ASSERT_FALSE(shut.open(why));                                                              // (no table: not opened, and a log that is not open does nothing in maintenance)
        shut.maintenance(kT0);
    } TEST_END();

    TEST_CASE("AUD1.2 The log keeps 180 days and at most 20,000 lines (the newest): maintenance drops the rest") {
        db::Database database;
        open_server_db(database, "");
        AuditConfig config;
        config.max_lines = 5;
        AdminAudit log(database, config);
        std::string why;
        ASSERT_TRUE(log.open(why));
        const Actor dave{"0123456789abcdef", "dave"};
        for (int i = 0; i < 8; ++i) log.record(dave, kT0 + i, "map_approved", "map " + std::to_string(i), "");
        log.maintenance(kT0 + 10);
        const std::vector<AuditEntry> lines = log.list(200);
        ASSERT_EQ(lines.size(), size_t{5});
        ASSERT_TRUE(lines.front().target == "map 7" && lines.back().target == "map 3");           // (the newest five)
        log.maintenance(kT0 + 10 + 181 * 86400);
        ASSERT_EQ(log.list(200).size(), size_t{0});                                                  // (past the days)
        ASSERT_EQ(AuditConfig().days, int64_t{180});
        ASSERT_EQ(AuditConfig().max_lines, size_t{20000});
    } TEST_END();

    TEST_CASE("AUD2.1 The migration from a copy of a version 7 database: the accounts, their counts, their addresses and sessions are gone, the log keeps only decisions on maps and signs them 'earlier admin', every decided map and the review log say 'earlier admin', and the rest is as it was") {
        TempDir dir;
        const std::string live = (dir.path / "live.db").string();
        const std::string copy = (dir.path / "copy.db").string();
        const std::vector<db::Migration> every = db_migrations();
        ASSERT_TRUE(every.size() >= 8 && every[6].version == 7 && every[7].version == 8);
        const std::vector<db::Migration> all(every.begin(), every.begin() + 8);
        const std::vector<db::Migration> v7(every.begin(), every.begin() + 7);
        {
            db::Database db7;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db7, live, v7, db::OpenOptions(), &version).ok() && version == 7);
            const std::string id1(32, 'a'), id2(32, 'b'), id3(32, 'c'), id4(32, 'd');
            const auto row = [&](const std::string& id, const std::string& state, const std::string& final_name, const std::string& by) {
                return "INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description, size, hash, findings, file, "
                       "file_kept, created_at, decided_at, decided_by) VALUES ('" + id + "', '" + std::string(64, '1') + "', '" + state + "', 'classic', 'N" + id.substr(0, 1) + "', 'n" + id.substr(0, 1) + "', " +
                       (final_name.empty() ? std::string("NULL, NULL") : "'" + final_name + "', '" + final_name + "'") + ", 0, 'Mia', '', '', 31, 31, 5, 2, 'd', 100, '0123456789abcdef', '[]', 'f" + id.substr(0, 1) + "', 1, 1000, " +
                       (state == "waiting" ? std::string("NULL") : std::string("2000")) + ", '" + by + "')";
            };
            ASSERT_TRUE(db7.exec(row(id1, "approved", "na", "Old Owner")).ok());
            ASSERT_TRUE(db7.exec(row(id2, "declined", "", "Old Mod")).ok());
            ASSERT_TRUE(db7.exec(row(id3, "waiting", "", "")).ok());
            ASSERT_TRUE(db7.exec(row(id4, "taken_down", "nd", "")).ok());                           // (a decided map that no name was kept for)
            ASSERT_TRUE(db7.exec("INSERT INTO review_log (at, actor_id, actor_name, action, submission, detail) VALUES (1500, '1', 'Old Owner', 'approve', '" + id1 + "', ''), (1900, '2', 'Old Mod', 'decline', '" + id2 + "', 'x')").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO meta (key, value) VALUES ('submission_salt', 'keepme'), ('admin_salt', 'drop-me')").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO admin_users (name, name_key, role, pw_hash, created_at) VALUES ('Old Owner', 'old owner', 'admin', 'x', 1), ('Old Mod', 'old mod', 'moderator', 'x', 1)").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO admin_locks (scope, key, fails, first_at) VALUES ('pair', 'k', 1, 5)").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO admin_known_addr VALUES (1, 'a', 5)").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO admin_revoked VALUES ('nonce', 9)").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO admin_audit (at, actor, actor_id, action, target, detail, addr_key) VALUES (10, 'Old Owner', 1, 'sign_in', '', '', 'addrkey'), (11, 'Old Owner', 1, 'user_added', 'Old Mod', 'moderator', 'addrkey'),"
                                " (12, 'Old Owner', 1, 'map_approved', 'na', '', 'addrkey'), (13, 'Old Mod', 2, 'map_declined', 'N', 'small', 'addrkey'), (14, 'Old Mod', 2, 'password_changed', 'Old Mod', '', 'addrkey'),"
                                " (15, 'Old Owner', 1, 'map_taken_down', 'nd', '', ''), (16, 'Old Owner', 1, 'map_put_back', 'nd', '', '')").ok());
            ASSERT_TRUE(db7.exec("INSERT INTO site_stats VALUES ('online', 5)").ok());
            db7.close();
        }
        fs::copy_file(live, copy);                                                                  // (what the operator backs up before the first start)
        const std::string before_bytes = slurp(copy);
        {
            db::Database db8;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db8, live, all, db::OpenOptions(), &version).ok() && version == 8);
            for (const char* gone : {"admin_users", "admin_locks", "admin_known_addr", "admin_revoked"}) ASSERT_EQ(scalar(db8, std::string("SELECT COUNT(*) FROM sqlite_master WHERE name = '") + gone + "'"), int64_t{0});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM meta WHERE key = 'admin_salt'"), int64_t{0});                                     // (the salt of the address keys goes with them)
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM meta WHERE key = 'submission_salt' AND value = 'keepme'"), int64_t{1});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM site_stats"), int64_t{1});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM map_submissions"), int64_t{4});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM review_log"), int64_t{2});
            // the log: decisions only, signed "earlier admin", with no reference and no address
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM admin_audit"), int64_t{4});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM admin_audit WHERE actor = 'earlier admin' AND actor_ref = ''"), int64_t{4});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM admin_audit WHERE action NOT IN ('map_approved', 'map_declined', 'map_taken_down', 'map_put_back')"), int64_t{0});
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM pragma_table_info('admin_audit') WHERE name IN ('addr_key', 'actor_id')"), int64_t{0});
            ASSERT_EQ(text_of(db8, "SELECT target || '|' || detail FROM admin_audit WHERE action = 'map_declined'"), std::string("N|small"));                // (the rest of a line is as it was)
            ASSERT_EQ(text_of(db8, "SELECT group_concat(at, ',') FROM (SELECT at FROM admin_audit ORDER BY id)"), std::string("12,13,15,16"));            // (the order and the times)
            // every decided map is signed "earlier admin"; a waiting map has no decider
            for (const char* id : {"a", "b", "d"}) ASSERT_EQ(text_of(db8, "SELECT decided_by FROM map_submissions WHERE id = '" + std::string(32, id[0]) + "'"), std::string("earlier admin"));
            ASSERT_EQ(text_of(db8, "SELECT decided_by FROM map_submissions WHERE id = '" + std::string(32, 'c') + "'"), std::string(""));
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM review_log WHERE actor_name = 'earlier admin' AND actor_id = ''"), int64_t{2});
            // the new log works on the migrated file
            AdminAudit log(db8);
            std::string why;
            ASSERT_TRUE(log.open(why));
            log.record(Actor{"0123456789abcdef", "dave"}, 5000, "map_approved", "Run One", "");
            ASSERT_TRUE(log.list(1).front().actor == "dave" && log.list(1).front().ref == "0123456789abcdef");
            db8.close();
            db::Database again;
            version = 0;
            ASSERT_TRUE(db::open_and_migrate(again, live, all, db::OpenOptions(), &version).ok() && version == 8);          // (idempotent)
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM schema_version"), int64_t{8});
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM admin_audit"), int64_t{5});
            again.close();
        }
        {   // the copy that was made before: migrate it the same way, then an older program must refuse it and leave it byte for byte
            db::Database mig;
            ASSERT_TRUE(db::open_and_migrate(mig, copy, all, db::OpenOptions()).ok());
            mig.close();
            for (const char* suffix : {"-wal", "-shm"}) fs::remove(copy + suffix);
            const std::string migrated = slurp(copy);
            ASSERT_TRUE(migrated != before_bytes);
            db::Database old_program;
            const db::Error refused = db::open_and_migrate(old_program, copy, v7, db::OpenOptions());
            ASSERT_TRUE(!refused.ok() && refused.code == db::kErrNewerSchema);
            ASSERT_FALSE(old_program.is_open());
            for (const char* suffix : {"-wal", "-shm"}) fs::remove(copy + suffix);
            ASSERT_TRUE(slurp(copy) == migrated);
        }
        {   // a step that fails leaves version 7: a database whose log has a column that the step cannot copy is not changed halfway
            db::Database broken;
            const std::string bad = (dir.path / "bad.db").string();
            ASSERT_TRUE(db::open_and_migrate(broken, bad, v7, db::OpenOptions()).ok());
            ASSERT_TRUE(broken.exec("DROP TABLE admin_audit").ok() && broken.exec("CREATE TABLE admin_audit (x INTEGER)").ok());
            broken.close();
            db::Database third;
            ASSERT_FALSE(db::open_and_migrate(third, bad, all, db::OpenOptions()).ok());
            db::Database check;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(check, bad, v7, db::OpenOptions(), &version).ok() && version == 7);
            ASSERT_EQ(scalar(check, "SELECT COUNT(*) FROM sqlite_master WHERE name IN ('admin_users', 'admin_locks', 'admin_known_addr', 'admin_revoked')"), int64_t{4});          // (the accounts' tables were not dropped by a step that did not finish)
        }
    } TEST_END();

    TEST_CASE("AUD2.2 After the migration the old password hashes, names and addresses are in no byte of the file or of its WAL (the file is rewritten and the WAL cut, in WAL mode as the live file is and in the rollback journal); what is kept is kept; the copy made before still holds them") {
        const std::vector<std::string> markers = {"pbkdf2-sha256$600000$MARKERSALT7$HASHMARKER9f8e7d6c5b4a", "Marker Old Owner", "marker old owner", "MARKERADDRKEY0123456789", "MARKERSALTVALUE", "MARKERDROPPEDDETAIL"};
        const auto holds = [&](const std::string& bytes) {
            std::string found;
            for (const std::string& marker : markers) {
                if (bytes.find(marker) != std::string::npos) found += marker + "; ";
            }
            return found;
        };
        for (const bool wal : {true, false}) {
            const std::string what = wal ? "WAL" : "rollback journal";
            TempDir dir;
            const std::string live = (dir.path / "live.db").string();
            const std::string copy = (dir.path / "copy.db").string();
            const std::vector<db::Migration> every = db_migrations();
            const std::vector<db::Migration> v7(every.begin(), every.begin() + 7);
            db::OpenOptions options;
            options.wal = wal;
            {
                db::Database db7;
                ASSERT_TRUE(db::open_and_migrate(db7, live, v7, options).ok());
                const std::string id(32, 'a');
                ASSERT_TRUE(db7.exec("INSERT INTO map_submissions (id, token_hash, state, kind, name, name_key, final_name, final_key, renamed, maker, note, owner_message, width, height, minutes, players, description, size, hash, findings, file, "
                                     "file_kept, created_at, decided_at, decided_by) VALUES ('" + id + "', '" + std::string(64, '1') + "', 'approved', 'classic', 'N', 'n', 'na', 'na', 0, 'Mia', '', '', 31, 31, 5, 2, 'd', 100, '0123456789abcdef', '[]', 'fa', 1, 1000, 2000, 'Marker Old Owner')").ok());
                ASSERT_TRUE(db7.exec("INSERT INTO review_log (at, actor_id, actor_name, action, submission, detail) VALUES (1500, '1', 'Marker Old Owner', 'approve', '" + id + "', '')").ok());
                ASSERT_TRUE(db7.exec("INSERT INTO meta (key, value) VALUES ('submission_salt', 'keepme'), ('admin_salt', 'MARKERSALTVALUE')").ok());
                ASSERT_TRUE(db7.exec("INSERT INTO admin_users (name, name_key, role, pw_hash, created_at) VALUES ('Marker Old Owner', 'marker old owner', 'admin', 'pbkdf2-sha256$600000$MARKERSALT7$HASHMARKER9f8e7d6c5b4a', 1)").ok());
                ASSERT_TRUE(db7.exec("INSERT INTO admin_audit (at, actor, actor_id, action, target, detail, addr_key) VALUES (10, 'Marker Old Owner', 1, 'sign_in', '', 'MARKERDROPPEDDETAIL', 'MARKERADDRKEY0123456789'), (12, 'Marker Old Owner', 1, 'map_approved', 'na', '', 'MARKERADDRKEY0123456789')").ok());
                ASSERT_TRUE(db7.exec("INSERT INTO admin_known_addr VALUES (1, 'MARKERADDRKEY0123456789', 5)").ok());
                db7.close();
            }
            fs::copy_file(live, copy);                                                              // (what the operator backs up before the first start: it is a file of its own, which nothing here can clean)
            ASSERT_MSG(holds(slurp(live)).find("HASHMARKER") != std::string::npos, what);            // (the markers are really in the file before: the test can tell a clean file from a dirty one)
            ASSERT_MSG(holds(slurp(copy)) == holds(slurp(live)) && !holds(slurp(copy)).empty(), what);
            db::Database db8;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db8, live, db_migrations(), options, &version).ok() && version == db_schema_version());
            ASSERT_MSG(holds(slurp(live)).empty(), what + ", the file (still open, as the server keeps it) holds: " + holds(slurp(live)));
            for (const char* suffix : {"-wal", "-journal"}) {
                if (fs::exists(live + suffix)) ASSERT_MSG(holds(slurp(live + suffix)).empty(), what + ", " + suffix + " holds: " + holds(slurp(live + suffix)));
            }
            // what is kept is kept
            ASSERT_EQ(scalar(db8, "SELECT COUNT(*) FROM admin_audit"), int64_t{1});
            ASSERT_EQ(text_of(db8, "SELECT actor || '|' || action || '|' || target FROM admin_audit"), std::string("earlier admin|map_approved|na"));
            ASSERT_EQ(text_of(db8, "SELECT decided_by FROM map_submissions"), std::string("earlier admin"));
            ASSERT_EQ(text_of(db8, "SELECT value FROM meta WHERE key = 'submission_salt'"), std::string("keepme"));
            ASSERT_EQ(text_of(db8, "PRAGMA integrity_check"), std::string("ok"));
            db8.close();
            ASSERT_MSG(holds(slurp(live)).empty(), what + ", the closed file holds: " + holds(slurp(live)));
            ASSERT_MSG(!holds(slurp(copy)).empty(), what);                                          // (the copy is as it was)
        }
    } TEST_END();

    TEST_CASE("AUD3.1 The database and its side files are private to the user (0600), the results folder too (0700): made so even with a loose umask, and tightened with a line for the log when they were looser") {
#ifndef _WIN32
        const auto mode = [](const fs::path& p) {
            struct stat st;
            return ::stat(p.string().c_str(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : -1;
        };
        TempDir dir;
        const fs::path folder = dir.path / "results";
        const std::string db_path = (folder / "ants.db").string();
        const mode_t old = ::umask(022);                                                            // (a loose umask: the files would be 0644 and the folder 0755)
        {
            const PrivateCreation private_files;
            std::error_code made;
            fs::create_directories(folder, made);
            db::Database database;
            ASSERT_TRUE(db::open_and_migrate(database, db_path, db_migrations(), db::OpenOptions()).ok());
            ASSERT_TRUE(database.exec("INSERT INTO meta (key, value) VALUES ('k', 'v')").ok());
            ASSERT_EQ(mode(db_path), 0600);
            ASSERT_EQ(mode(folder), 0700);
            if (fs::exists(db_path + "-wal")) ASSERT_EQ(mode(db_path + "-wal"), 0600);              // (the side file that SQLite makes after the database is private too)
        }
        const mode_t after = ::umask(old);
        ASSERT_EQ(static_cast<int>(after), 022);                                                    // (the object put the umask back)
        ASSERT_TRUE(tighten_private_files(db_path, folder.string()).empty());                       // (nothing to tighten: nothing said)
        // looser ones are tightened, each with a line, and a second pass says nothing
        ASSERT_EQ(::chmod(db_path.c_str(), 0644), 0);
        { std::ofstream w(db_path + "-wal", std::ios::app); }
        ASSERT_EQ(::chmod((db_path + "-wal").c_str(), 0666), 0);
        ASSERT_EQ(::chmod(folder.string().c_str(), 0755), 0);
        const std::vector<std::string> lines = tighten_private_files(db_path, folder.string());
        ASSERT_EQ(lines.size(), size_t{3});
        ASSERT_TRUE(lines[0].find("made private") != std::string::npos && lines[0].find(db_path) != std::string::npos);
        ASSERT_TRUE(mode(db_path) == 0600 && mode(db_path + "-wal") == 0600 && mode(folder) == 0700);
        ASSERT_TRUE(tighten_private_files(db_path, folder.string()).empty());
        ASSERT_TRUE(tighten_private_files((dir.path / "none").string(), "").empty());              // (a file that is not there is no change)
#endif
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
