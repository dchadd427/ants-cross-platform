// Tests of the site statistics in the server's database (SiteStats::open_db, tables site_stats and site_stats_hours of schema version 4, docs/SERVER.md "The site statistics" and "The database"): the migration
// from a database of version 3, a new database, the numbers kept and read back (the window of 24 hours, the totals, `since`), the import of the site-stats.json of an older server (once, the file left as it
// was, a file that is no statistics put aside), the same answer as the file gives, a write that fails and is rolled back whole, a stop without a save (what was saved is there), the cap of single-player
// reports, and a server without a database (the file as before). The counting rules and the file itself are tested in test_server (S3.140 - S3.149).
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ANTS_SITE_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_SITE_GETPID getpid
#endif

#include "../common/test_assert.hpp"
#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/site_stats.hpp"

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (run_tests.sh clears it)
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

namespace {

constexpr int64_t kT0 = 1791117296;                       // 2026-10-04 12:34:56 UTC, the hour number 497532
constexpr int64_t kH = 3600;

struct Clock {
    int64_t now{kT0};
    SiteStats::Clock fn() { return [this]() { return now; }; }
};

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path() / ("ants_test_sitedb_" + std::to_string(ANTS_SITE_GETPID()) + "_" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

bool open_db_file(db::Database& database, const std::string& path, size_t migrations = 0) {
    std::vector<db::Migration> list = db_migrations();
    if (migrations != 0) list.resize(migrations);
    return db::open_and_migrate(database, path, list, db::OpenOptions()).ok();
}

int64_t scalar(db::Database& database, const std::string& sql) {
    db::Statement st;
    if (!database.prepare(st, sql).ok() || st.step() != db::Statement::Step::Row) return -1;
    return st.column_int64(0);
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// A statistics file as an older server wrote it
std::string old_file(const std::string& since = "2026-09-01", const std::string& online = "{\"total\":50,\"hours\":[[497532,3],[497531,2]]}", const std::string& local = "{\"total\":120,\"hours\":[[497520,7]]}") {
    return "{\"format\":1,\"since\":\"" + since + "\",\"online\":" + online + ",\"local\":" + local + "}\n";
}

bool has(const std::vector<std::string>& notices, const std::string& text) {
    for (const std::string& n : notices) {
        if (n.find(text) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> names_in(const fs::path& dir, const std::string& prefix) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().filename().string().rfind(prefix, 0) == 0) out.push_back(e.path().filename().string());
    }
    return out;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n SITE STATISTICS IN THE DATABASE (schema version 4)\n=======================================================\n";

    TEST_CASE("SD1.1 Schema version 4 is brought to a database of version 3 with its data kept: two empty tables, the checks that keep a bad row out") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        {
            db::Database v3;
            ASSERT_TRUE(open_db_file(v3, path, 3));
            ASSERT_FALSE(scalar(v3, "SELECT count(*) FROM sqlite_master WHERE name = 'site_stats'") == 1);
            ASSERT_TRUE(v3.exec("INSERT INTO map_plays (map, hash, plays, last_played) VALUES ('TINY.LVL', '0123456789abcdef', 4, 99)").ok());
        }
        db::Database database;
        ASSERT_TRUE(open_db_file(database, path));
        ASSERT_EQ(scalar(database, "SELECT max(version) FROM schema_version"), db_schema_version());       // (the newest version: the later migrations ran too)
        ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats"), 0);
        ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats_hours"), 0);
        ASSERT_EQ(scalar(database, "SELECT plays FROM map_plays"), 4);                                  // (the older data is as it was)
        ASSERT_FALSE(database.exec("INSERT INTO site_stats (kind, total) VALUES ('other', 1)").ok());      // (only the two counters)
        ASSERT_FALSE(database.exec("INSERT INTO site_stats (kind, total) VALUES ('online', -1)").ok());
        ASSERT_FALSE(database.exec("INSERT INTO site_stats_hours (kind, hour, games) VALUES ('online', 5, 1)").ok());     // (a bucket needs its counter)
        ASSERT_TRUE(database.exec("INSERT INTO site_stats (kind, total) VALUES ('online', 1)").ok());
        ASSERT_FALSE(database.exec("INSERT INTO site_stats_hours (kind, hour, games) VALUES ('online', 5, 0)").ok());
        ASSERT_TRUE(database.exec("INSERT INTO site_stats_hours (kind, hour, games) VALUES ('online', 5, 1)").ok());
        ASSERT_FALSE(database.exec("INSERT INTO site_stats_hours (kind, hour, games) VALUES ('online', 5, 1)").ok());     // (an hour once)
    } TEST_END();

    TEST_CASE("SD1.2 A new database: the counters start at 0 from today, the first save makes the rows, and the next start reads every number back with the day read at the new time") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        Clock clock;
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats stats(clock.fn());
            stats.open_db(database, "");
            ASSERT_TRUE(has(stats.take_notices(), "the database has none yet, counting from 2026-10-04"));
            ASSERT_TRUE(stats.dirty());
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats"), 0);
            ASSERT_TRUE(stats.save_if_due());                                                              // (a new counter is written at the first look: `since` outlives a restart before any game)
            ASSERT_FALSE(stats.dirty());
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats"), 2);
            ASSERT_FALSE(stats.save_if_due());                                                             // (nothing changed: nothing is written)
            stats.count_online();
            ASSERT_FALSE(stats.save_if_due());                                                             // (the same throttle as the file: ten seconds)
            clock.now += 10;
            ASSERT_TRUE(stats.save_if_due());
            stats.count_online();
            ASSERT_TRUE(stats.count_local());
            ASSERT_TRUE(stats.save());
            ASSERT_TRUE(stats.save());                                                                     // (and again: nothing to do)
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), 2);
            ASSERT_EQ(scalar(database, "SELECT games FROM site_stats_hours WHERE kind = 'online' AND hour = 497532"), 2);
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'local'"), 1);
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM meta WHERE key = 'site_stats_since' AND value = '2026-10-04'"), 1);
        }
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            clock.now = kT0 + 20 * kH;
            SiteStats stats(clock.fn());
            stats.open_db(database, "");
            ASSERT_TRUE(has(stats.take_notices(), "read 2 online and 1 single-player games since 2026-10-04 from the database"));
            ASSERT_FALSE(stats.dirty());
            ASSERT_TRUE(stats.online().total == 2 && stats.online().day == 2 && stats.local().total == 1 && stats.local().day == 1 && stats.since() == "2026-10-04");
            clock.now = kT0 + 24 * kH;                                                                    // the hours of the old day leave the window, the totals stay
            ASSERT_TRUE(stats.online().total == 2 && stats.online().day == 0 && stats.local().day == 0);
            stats.count_online();
            ASSERT_TRUE(stats.save());
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats_hours WHERE kind = 'online'"), 1);   // (only the buckets of the window are kept)
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), 3);
        }
        {   // `since` is the day of the first start and stays
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            clock.now = kT0 + 90 * 24 * kH;
            SiteStats later(clock.fn());
            later.open_db(database, "");
            ASSERT_EQ(later.since(), std::string("2026-10-04"));
        }
    } TEST_END();

    TEST_CASE("SD1.3 The file of an older server is read into the database once: every number, the date, the file left byte for byte as it was, and a later start (even over a changed file) reads the database") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        const std::string file = dir.file("site-stats.json");
        spit(file, old_file());
        Clock clock;
        {
            db::Database v3;
            ASSERT_TRUE(open_db_file(v3, path, 3));                                                          // (the deploy: a database of version 3 and the file next to it)
        }
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats stats(clock.fn());
            stats.open_db(database, file);
            const std::vector<std::string> notices = stats.take_notices();
            ASSERT_TRUE(has(notices, "imported 50 online and 120 single-player games since 2026-09-01") && has(notices, "the file is left as it was"));
            ASSERT_FALSE(stats.dirty());                                                                  // (written at once)
            ASSERT_TRUE(stats.online().total == 50 && stats.online().day == 5 && stats.local().total == 120 && stats.local().day == 7 && stats.since() == "2026-09-01");
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), 50);
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'local'"), 120);
            ASSERT_EQ(scalar(database, "SELECT sum(games) FROM site_stats_hours WHERE kind = 'online'"), 5);
            ASSERT_EQ(scalar(database, "SELECT games FROM site_stats_hours WHERE kind = 'local' AND hour = 497520"), 7);
            ASSERT_EQ(slurp(file), old_file());                                                           // (untouched)
            ASSERT_EQ(names_in(dir.path, "site-stats").size(), size_t{1});                                // (not renamed, no .tmp, no .broken)
            stats.count_online();                                                                          // the games after the import go on from there
            ASSERT_TRUE(stats.save());
        }
        spit(file, old_file("2026-01-01", "{\"total\":9,\"hours\":[]}", "{\"total\":9,\"hours\":[]}"));    // (another file now: it is not looked at)
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats stats(clock.fn());
            stats.open_db(database, file);
            ASSERT_TRUE(has(stats.take_notices(), "read 51 online and 120 single-player games since 2026-09-01 from the database"));
            ASSERT_TRUE(stats.online().total == 51 && stats.local().total == 120 && stats.since() == "2026-09-01");
        }
        {   // the same file twice in one start-up (a crash between the import and the end of start-up): the second import is the database
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats again(clock.fn());
            again.open_db(database, file);
            ASSERT_EQ(again.online().total, uint64_t{51});
        }
    } TEST_END();

    TEST_CASE("SD1.4 A file that is no statistics is put aside whole (as without a database), the counters start at 0, and the database holds the new ones; a missing file is a new start") {
        TempDir dir;
        const std::string file = dir.file("site-stats.json");
        Clock clock;
        {
            spit(file, "this is no statistics");
            db::Database database;
            ASSERT_TRUE(open_db_file(database, dir.file("a.db")));
            SiteStats stats(clock.fn());
            stats.open_db(database, file);
            ASSERT_TRUE(has(stats.take_notices(), "cannot be used (it is not JSON"));
            ASSERT_TRUE(stats.online().total == 0 && stats.local().total == 0 && stats.since() == "2026-10-04");
            ASSERT_FALSE(fs::exists(file));
            const std::vector<std::string> aside = names_in(dir.path, "site-stats.json.broken-");
            ASSERT_EQ(aside.size(), size_t{1});
            ASSERT_EQ(slurp(dir.path / aside[0]), std::string("this is no statistics"));
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats"), 2);
        }
        {
            spit(file, old_file("2026-09-01", "{\"total\":1,\"hours\":[[497532,2]]}"));                    // (buckets that hold more than the total: wrong)
            db::Database database;
            ASSERT_TRUE(open_db_file(database, dir.file("b.db")));
            SiteStats stats(clock.fn());
            stats.open_db(database, file);
            ASSERT_TRUE(has(stats.take_notices(), "hold more than its total"));
            ASSERT_TRUE(stats.online().total == 0);
        }
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, dir.file("c.db")));
            SiteStats stats(clock.fn());
            stats.open_db(database, dir.file("no-such.json"));
            ASSERT_TRUE(has(stats.take_notices(), "the database has none yet, counting from") && stats.online().total == 0);
            ASSERT_EQ(scalar(database, "SELECT count(*) FROM site_stats"), 2);
        }
    } TEST_END();

    TEST_CASE("SD1.5 The answer of GET /stats is the same from the database as from the file, for the same games") {
        TempDir dir;
        Clock a_clock;
        Clock b_clock;
        SiteStats from_file(a_clock.fn());
        from_file.open(dir.file("site-stats.json"));
        db::Database database;
        ASSERT_TRUE(open_db_file(database, dir.file("ants.db")));
        SiteStats from_db(b_clock.fn());
        from_db.open_db(database, "");
        for (SiteStats* s : {&from_file, &from_db}) {
            s->count_online();
            s->count_online();
            for (int i = 0; i < 5; ++i) ASSERT_TRUE(s->count_local());
            ASSERT_TRUE(s->save());
        }
        BusyCounts now;
        now.matches = 2;
        now.players = 5;
        ASSERT_EQ(from_db.json(now), from_file.json(now));
        ASSERT_EQ(from_db.json(now), std::string("{\"now\":{\"matches\":2,\"players\":5},\"online\":{\"day\":2,\"total\":2},\"local\":{\"day\":5,\"total\":5},\"since\":\"2026-10-04\"}"));
        b_clock.now += 30 * kH;                                                                           // (a restart a day and more later: the same numbers again)
        db::Database reopened;
        ASSERT_TRUE(open_db_file(reopened, dir.file("ants.db")));
        SiteStats later(b_clock.fn());
        later.open_db(reopened, "");
        ASSERT_EQ(later.json(now), std::string("{\"now\":{\"matches\":2,\"players\":5},\"online\":{\"day\":0,\"total\":2},\"local\":{\"day\":0,\"total\":5},\"since\":\"2026-10-04\"}"));
    } TEST_END();

    TEST_CASE("SD1.6 A write that fails is rolled back whole (the numbers of the last good save stay), said once in the log, and the way back is said once") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        Clock clock;
        db::Database database;
        ASSERT_TRUE(open_db_file(database, path));
        SiteStats stats(clock.fn());
        stats.open_db(database, "");
        stats.count_online();
        ASSERT_TRUE(stats.save());
        stats.take_notices();
        // the second bucket of the next save is refused by the database: the totals were written before it, and must not stay
        ASSERT_TRUE(database.exec("CREATE TRIGGER refuse BEFORE INSERT ON site_stats_hours WHEN NEW.games >= 2 BEGIN SELECT RAISE(ABORT, 'refused'); END").ok());
        stats.count_online();
        stats.count_online();
        ASSERT_FALSE(stats.save());
        ASSERT_TRUE(stats.dirty());
        std::vector<std::string> notices = stats.take_notices();
        ASSERT_TRUE(notices.size() == 1 && has(notices, "site statistics could not be saved") && has(notices, "refused"));
        ASSERT_FALSE(database.in_transaction());
        ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), 1);              // (the old numbers, whole)
        ASSERT_EQ(scalar(database, "SELECT games FROM site_stats_hours WHERE kind = 'online'"), 1);
        clock.now += 10;
        ASSERT_FALSE(stats.save_if_due());                                                                 // (tried again, failed again: not said again)
        ASSERT_TRUE(stats.take_notices().empty());
        ASSERT_TRUE(database.exec("DROP TRIGGER refuse").ok());
        clock.now += 10;
        ASSERT_TRUE(stats.save_if_due());
        notices = stats.take_notices();
        ASSERT_TRUE(notices.size() == 1 && has(notices, "site statistics are saved again"));
        ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), 3);
        ASSERT_EQ(scalar(database, "SELECT games FROM site_stats_hours WHERE kind = 'online'"), 3);
        // a database that lost its tables: a save fails and says why, the server goes on counting in memory
        ASSERT_TRUE(database.exec("DROP TABLE site_stats_hours").ok());
        stats.count_local();
        clock.now += 10;
        ASSERT_FALSE(stats.save_if_due());
        ASSERT_TRUE(has(stats.take_notices(), "could not be saved"));
        ASSERT_EQ(stats.local().total, uint64_t{1});
    } TEST_END();

    TEST_CASE("SD1.7 A stop without a save keeps what the last save wrote (a second connection sees it at once, a start after the crash reads it), and nothing half-written") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        Clock clock;
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats stats(clock.fn());
            stats.open_db(database, "");
            stats.count_online();
            ASSERT_TRUE(stats.save());
            stats.count_online();                                                                          // (not saved: the last ten seconds)
            ASSERT_TRUE(stats.count_local());
            db::Database reader;
            ASSERT_TRUE(reader.open(path).ok());
            ASSERT_EQ(scalar(reader, "SELECT total FROM site_stats WHERE kind = 'online'"), 1);
            ASSERT_EQ(scalar(reader, "SELECT total FROM site_stats WHERE kind = 'local'"), 0);
        }                                                                                                  // (the object goes without save())
        db::Database database;
        ASSERT_TRUE(open_db_file(database, path));
        SiteStats stats(clock.fn());
        stats.open_db(database, "");
        ASSERT_TRUE(stats.online().total == 1 && stats.local().total == 0);
    } TEST_END();

    TEST_CASE("SD1.8 The largest numbers: a total that fills a signed number is kept, shown as the largest and never negative; the cap of 120 single-player reports in 60 seconds holds with a database") {
        TempDir dir;
        const std::string path = dir.file("ants.db");
        const std::string file = dir.file("site-stats.json");
        spit(file, old_file("9999-12-31", "{\"total\":9223372036854775807,\"hours\":[]}", "{\"total\":9223372036854775807,\"hours\":[]}"));
        Clock clock;
        {
            db::Database database;
            ASSERT_TRUE(open_db_file(database, path));
            SiteStats big(clock.fn());
            big.open_db(database, file);
            big.count_online();                                                                            // one more than a signed number holds
            ASSERT_TRUE(big.save());
            ASSERT_TRUE(big.json(BusyCounts()).find("\"online\":{\"day\":1,\"total\":9223372036854775807}") != std::string::npos);
            ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'online'"), INT64_MAX);
        }
        TempDir other;
        db::Database database;
        ASSERT_TRUE(open_db_file(database, other.file("ants.db")));
        SiteStats stats(clock.fn());
        stats.open_db(database, "");
        for (size_t i = 0; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(stats.count_local());
        ASSERT_FALSE(stats.count_local());
        clock.now += 60;
        ASSERT_TRUE(stats.count_local());
        ASSERT_TRUE(stats.save());
        ASSERT_EQ(scalar(database, "SELECT total FROM site_stats WHERE kind = 'local'"), 121);
    } TEST_END();

    TEST_CASE("SD1.9 Without a database the file is what it was (written whole, read back, put aside when it is no statistics), and without either the counters live in memory") {
        TempDir dir;
        const std::string file = dir.file("site-stats.json");
        Clock clock;
        {
            SiteStats stats(clock.fn());
            stats.open(file);
            ASSERT_TRUE(has(stats.take_notices(), "is new, counting from 2026-10-04"));
            stats.count_online();
            ASSERT_TRUE(stats.save());
            ASSERT_EQ(slurp(file), std::string("{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":1,\"hours\":[[497532,1]]},\"local\":{\"total\":0,\"hours\":[]}}\n"));
        }
        {
            SiteStats stats(clock.fn());
            stats.open(file);
            ASSERT_TRUE(has(stats.take_notices(), "read 1 online and 0 single-player games since 2026-10-04"));
        }
        {
            SiteStats memory(clock.fn());
            memory.count_online();
            ASSERT_TRUE(memory.dirty() && memory.save() && !memory.save_if_due());
            ASSERT_EQ(memory.online().total, uint64_t{1});
        }
        ASSERT_EQ(names_in(dir.path, "").size(), size_t{1});                                               // (no database was made)
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
