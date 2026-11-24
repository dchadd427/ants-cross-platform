// Tests of the lists of the rotation (bot_match_spec.hpp bot_rotation_defaults, bot_rotation.hpp, db_schema.cpp version 10; docs/SERVER_REPLAYS.md "The lists"): the lists that a server starts with are the ones that the code always had, an
// edited list is kept (in the database across a restart, in memory without one) and "back to the defaults" is no row, the shape of a list is checked when it is read back, and a copy of a version 9 database migrates to 10 and keeps what
// it had. What the routes accept and what the draws do with a list are in the server suite (bot_rotation_tests.hpp). No sockets, no rooms.
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_server/bot_match_spec.hpp"
#include "ants_server/bot_rotation.hpp"
#include "ants_server/bot_schedules.hpp"
#include "ants_server/db_schema.hpp"
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

namespace {

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

std::vector<std::string> files_of(const std::vector<BotRotationEntry>& list) {
    std::vector<std::string> out;
    for (const BotRotationEntry& e : list) out.push_back(e.file);
    return out;
}

const int64_t kNow = 1800000000;                          // 2027-01-15 08:00 UTC

}  // namespace

int main() {
    std::cout << "=======================================================\n THE LISTS OF THE ROTATION OF THE SERVER'S OWN MATCHES OF COMPUTER PLAYERS\n=======================================================\n";

    TEST_CASE("BRO1.1 The lists that a server starts with are the ones that it always had, in the same order: the original's game has the six maps and three of the library, 187 has five of the six and an approved map of the players") {
        ASSERT_EQ(files_of(bot_rotation_defaults(false)), (std::vector<std::string>{"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL", "POPcOrN.lvl", "HELLRAISER.lvl", "Bombz Away.lvl"}));
        ASSERT_EQ(files_of(bot_rotation_defaults(true)), (std::vector<std::string>{"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "Fight Island.lvl"}));
        for (const bool kills_187 : {false, true}) {
            for (const BotRotationEntry& e : bot_rotation_defaults(kills_187)) ASSERT_EQ(e.approved, e.file == "Fight Island.lvl");          // (only the map of the players is found in the store of the approved maps)
        }
        ASSERT_TRUE(std::string(bot_rotation_word(false)) == "highest-score" && std::string(bot_rotation_word(true)) == "187");
        bool k = true;
        ASSERT_TRUE(bot_rotation_rules_of("highest-score", k) && !k && bot_rotation_rules_of("187", k) && k);
        ASSERT_FALSE(bot_rotation_rules_of("random", k) || bot_rotation_rules_of("", k) || bot_rotation_rules_of("Highest score", k));
    } TEST_END();

    TEST_CASE("BRO1.2 A list is read from the text that the table keeps, and only when it is a list: a name of the shape of a map's file, once, a bool for the approved flag, at least one map and at most a hundred; anything else is refused whole") {
        std::vector<BotRotationEntry> list;
        ASSERT_TRUE(bot_rotation_parse("[{\"file\":\"TINY.LVL\",\"approved\":false},{\"file\":\"Fight Island.lvl\",\"approved\":true}]", list) && list.size() == 2 && list[1].file == "Fight Island.lvl" && list[1].approved);
        std::vector<BotRotationEntry> again;
        ASSERT_TRUE(bot_rotation_parse(bot_rotation_json(list), again) && bot_rotation_same(list, again));                          // (what is written is what is read)
        for (const char* bad : {"", "not json", "{}", "[]", "[1]", "[\"TINY.LVL\"]", "[{\"file\":\"TINY.LVL\"}]", "[{\"approved\":false}]", "[{\"file\":3,\"approved\":false}]", "[{\"file\":\"TINY.LVL\",\"approved\":0}]",
                                "[{\"file\":\"TINY.LVL\",\"approved\":false},{\"file\":\"TINY.LVL\",\"approved\":false}]", "[{\"file\":\"../TINY.LVL\",\"approved\":false}]", "[{\"file\":\"a/b.lvl\",\"approved\":false}]",
                                "[{\"file\":\"TINY.txt\",\"approved\":false}]", "[{\"file\":\".lvl\",\"approved\":false}]"}) {
            ASSERT_FALSE(bot_rotation_parse(bad, list));
        }
        std::string many = "[";
        for (size_t i = 0; i < kBotRotationMax + 1; ++i) many += std::string(i ? "," : "") + "{\"file\":\"Map " + std::to_string(i) + ".lvl\",\"approved\":false}";
        ASSERT_FALSE(bot_rotation_parse(many + "]", list));                                                                          // (a hundred and one)
        ASSERT_TRUE(bot_rotation_parse(many.substr(0, many.rfind(",{")) + "]", list) && list.size() == kBotRotationMax);              // (a hundred)
    } TEST_END();

    TEST_CASE("BRO2.1 With no row a rules has its defaults; a saved list is kept and listed in its order; a list that is the defaults is no row; back to the defaults deletes the row; the other rules is not touched") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        BotRotationStore store(&database);
        std::string why, error;
        ASSERT_TRUE(store.open(why));
        ASSERT_TRUE(bot_rotation_same(store.list(false), bot_rotation_defaults(false)) && bot_rotation_same(store.list(true), bot_rotation_defaults(true)) && !store.edited(false) && !store.edited(true));
        ASSERT_EQ(scalar(database, "SELECT COUNT(*) FROM bot_rotation"), int64_t{0});                                                // (nothing is written at the first start)
        const std::vector<BotRotationEntry> mine = {{"TINY.LVL", false}, {"PoPcOrN 3.lvl", false}, {"SMALL.LVL", false}};
        ASSERT_TRUE(store.set(false, mine, kNow, error));
        ASSERT_TRUE(store.edited(false) && !store.edited(true) && bot_rotation_same(store.list(false), mine) && bot_rotation_same(store.list(true), bot_rotation_defaults(true)));
        ASSERT_EQ(text_of(database, "SELECT rules || '|' || updated_at FROM bot_rotation"), std::string("highest-score|") + std::to_string(kNow));
        ASSERT_TRUE(store.set(false, bot_rotation_defaults(false), kNow + 5, error));                                                // (the same as the defaults: no row)
        ASSERT_TRUE(!store.edited(false) && scalar(database, "SELECT COUNT(*) FROM bot_rotation") == 0);
        ASSERT_TRUE(store.set(true, mine, kNow, error) && store.set(false, mine, kNow, error) && scalar(database, "SELECT COUNT(*) FROM bot_rotation") == 2);
        ASSERT_TRUE(store.reset(true, error) && !store.edited(true) && store.edited(false) && bot_rotation_same(store.list(true), bot_rotation_defaults(true)));
        ASSERT_EQ(text_of(database, "SELECT rules FROM bot_rotation"), std::string("highest-score"));
        ASSERT_TRUE(store.reset(true, error));                                                                                       // (twice is the same)
    } TEST_END();

    TEST_CASE("BRO2.2 A restart reads the lists back: the same maps in the same order; a row that is no list is left out (its rules have the defaults) and told in the notices; a table that is not there is told") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        std::string why, error;
        const std::vector<BotRotationEntry> mine = {{"MEDIUM.LVL", false}, {"Fight Island.lvl", true}, {"TINY.LVL", false}};
        {
            BotRotationStore store(&database);
            ASSERT_TRUE(store.open(why) && store.set(true, mine, kNow, error));
        }
        BotRotationStore reopened(&database);
        ASSERT_TRUE(reopened.open(why) && reopened.edited(true) && bot_rotation_same(reopened.list(true), mine) && !reopened.edited(false));
        ASSERT_TRUE(database.exec("INSERT INTO bot_rotation (rules, maps, updated_at) VALUES ('highest-score', '[{\"file\":\"x\"}]', 1)").ok());          // (a damaged row)
        BotRotationStore damaged(&database);
        ASSERT_TRUE(damaged.open(why) && !damaged.edited(false) && bot_rotation_same(damaged.list(false), bot_rotation_defaults(false)) && damaged.edited(true));
        ASSERT_EQ(damaged.take_notices().size(), size_t{1});
        ASSERT_TRUE(database.exec("DROP TABLE bot_rotation").ok());                                                                    // (no table: not the file of this program's version)
        BotRotationStore none(&database);
        ASSERT_FALSE(none.open(why));
        ASSERT_FALSE(why.empty());
    } TEST_END();

    TEST_CASE("BRO2.3 Without a database the lists live in memory: the same calls, the same answers, and a new store has the defaults again") {
        BotRotationStore memory(nullptr);
        std::string why, error;
        ASSERT_TRUE(memory.open(why));
        const std::vector<BotRotationEntry> mine = {{"GAUNTLET.LVL", false}};
        ASSERT_TRUE(memory.set(false, mine, kNow, error) && memory.edited(false) && bot_rotation_same(memory.list(false), mine));
        ASSERT_TRUE(memory.reset(false, error) && !memory.edited(false) && bot_rotation_same(memory.list(false), bot_rotation_defaults(false)));
        ASSERT_TRUE(memory.set(true, mine, kNow, error));
        BotRotationStore next(nullptr);
        ASSERT_TRUE(next.open(why) && !next.edited(true));
    } TEST_END();

    TEST_CASE("BRO2.4 The database refuses a list that no list is, and a database that refuses a change leaves the list as it was") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        ASSERT_TRUE(database.exec("INSERT INTO bot_rotation (rules, maps, updated_at) VALUES ('187', '[]', 5)").ok());
        ASSERT_FALSE(database.exec("INSERT INTO bot_rotation (rules, maps, updated_at) VALUES ('random', '[]', 5)").ok());                                // (a rules that is not one)
        ASSERT_FALSE(database.exec("INSERT INTO bot_rotation (rules, maps, updated_at) VALUES ('highest-score', '" + std::string(16001, 'x') + "', 5)").ok());          // (a text too long to be a hundred maps)
        ASSERT_FALSE(database.exec("INSERT INTO bot_rotation (rules, maps, updated_at) VALUES ('187', '[]', 5)").ok());                                    // (one row for each rules)
        BotRotationStore store(&database);
        std::string why, error;
        ASSERT_TRUE(store.open(why));
        ASSERT_TRUE(database.exec("DROP TABLE bot_rotation").ok());                                                                    // (the file goes bad under the store)
        ASSERT_FALSE(store.set(false, {{"TINY.LVL", false}}, kNow, error));
        ASSERT_TRUE(error == "db" && !store.edited(false) && bot_rotation_same(store.list(false), bot_rotation_defaults(false)));
        ASSERT_FALSE(store.reset(true, error));
        ASSERT_EQ(error, std::string("db"));
    } TEST_END();

    TEST_CASE("BRO3.1 The migration to version 10 from a copy of a version 9 database: the table is there and empty, the schedules and every other row are as they were, a second start changes nothing, and an older program that does not know version 10 refuses the file") {
        TempDir dir;
        const std::string live = (dir.path / "live.db").string();
        const std::vector<db::Migration> every = db_migrations();
        ASSERT_TRUE(every.size() >= 10 && every[8].version == 9 && every[9].version == 10 && db_schema_version() == every.back().version);
        const std::vector<db::Migration> v9(every.begin(), every.begin() + 9);
        const std::vector<db::Migration> v10(every.begin(), every.begin() + 10);               // (the version this case is about: a later migration is not its business)
        {
            db::Database old;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(old, live, v9, db::OpenOptions(), &version).ok() && version == 9);
            ASSERT_EQ(scalar(old, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'bot_rotation'"), int64_t{0});
            BotScheduleStore schedules(&old);
            std::string why;
            ASSERT_TRUE(schedules.open(kNow, why) && schedules.seed_default(60, kNow) && schedules.list().size() == 1);
            ASSERT_TRUE(old.exec("INSERT INTO admin_audit (at, actor, actor_ref, action, target, detail) VALUES (10, 'dave', '0123456789abcdef', 'bot_schedule_added', 'Hourly', '')").ok());
            old.close();
        }
        {
            db::Database db10;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db10, live, v10, db::OpenOptions(), &version).ok() && version == 10);
            ASSERT_EQ(scalar(db10, "SELECT COUNT(*) FROM bot_rotation"), int64_t{0});
            ASSERT_EQ(scalar(db10, "SELECT COUNT(*) FROM bot_schedules"), int64_t{1});
            ASSERT_EQ(text_of(db10, "SELECT name FROM bot_schedules"), std::string("Hourly rotation"));
            ASSERT_EQ(scalar(db10, "SELECT COUNT(*) FROM admin_audit"), int64_t{1});
            ASSERT_EQ(text_of(db10, "PRAGMA integrity_check"), std::string("ok"));
            BotScheduleStore schedules(&db10);
            BotRotationStore rotation(&db10);
            std::string why, error;
            ASSERT_TRUE(schedules.open(kNow, why) && schedules.list().size() == 1 && rotation.open(why) && !rotation.edited(false) && !rotation.edited(true));          // (nothing is made by the migration or the first start: no row, the defaults)
            ASSERT_TRUE(rotation.set(false, {{"TINY.LVL", false}}, kNow, error));
            db10.close();
            db::Database again;
            version = 0;
            ASSERT_TRUE(db::open_and_migrate(again, live, v10, db::OpenOptions(), &version).ok() && version == 10);                    // (idempotent)
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM schema_version"), int64_t{10});
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM bot_rotation"), int64_t{1});
            again.close();
            db::Database old_program;
            const db::Error refused = db::open_and_migrate(old_program, live, v9, db::OpenOptions());
            ASSERT_TRUE(!refused.ok() && refused.code == db::kErrNewerSchema);
        }
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
