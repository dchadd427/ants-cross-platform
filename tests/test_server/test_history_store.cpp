// Tests of the match history's store in the server's SQLite database (ants_server/history_store.hpp, docs/REPLAYS.md "The match history"): records kept and read back, the list's filters, sorts and search (a
// LIKE pattern whose wildcards in the text are literal, values that are never part of the SQL), the markers of the owner's deletes, the indexes the list uses, the feeder that counts the replay store's
// matches one at a time (real recordings played again on the shipped TINY map), a database that stops answering (the store degrades, the routes say 503, it comes back), and the routes of the control
// interface and of the public door in front of a room manager. The rule that decides who won is test_history_record.cpp; whole rooms are test_history_rooms.cpp.
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
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

// ---- records made by hand ------------------------------------------------------------------------------------------------------------

HistorySeat seat_of(uint8_t seat, const char* name, const char* bot, int32_t score, uint32_t killed, uint32_t lost, uint32_t hatched) {
    HistorySeat s;
    s.seat = seat;
    s.name = name;
    s.bot = bot;
    s.score = score;
    s.killed = killed;
    s.lost = lost;
    s.hatched = hatched;
    s.bombs_planted = seat + 1u;
    s.bombs_defused = seat + 2u;
    s.fires_lit = seat + 3u;
    return s;
}

// A record whose rows are one per seat, ranked by score (a tie: the lower seat); the first row wins unless the match is a draw
HistoryRecord record_of(int64_t t, const char* map, const char* format, uint32_t turns, bool finished, int quitter, bool draw, std::vector<HistorySeat> seats, const char* mode = "highest-score") {
    HistoryRecord r;
    r.id = id_at(t, ReplayStore::map_stem(map));
    r.ended_s = t;
    r.map = map;
    r.game = "v0.12.0-test";
    r.turns = turns;
    r.finished = finished;
    r.quitter = quitter;
    r.mode = mode;
    r.format = format;
    r.draw = draw;
    r.computers_only = true;
    for (const HistorySeat& s : seats) r.computers_only = r.computers_only && !s.bot.empty();
    std::vector<HistorySeat> ranked = seats;
    std::stable_sort(ranked.begin(), ranked.end(), [](const HistorySeat& a, const HistorySeat& b) { return a.score > b.score; });
    for (const HistorySeat& s : ranked) {
        HistoryRow row;
        row.seats.push_back(s.seat);
        row.score = s.score;
        row.killed = static_cast<int32_t>(s.killed);
        row.lost = static_cast<int32_t>(s.lost);
        row.hatched = static_cast<int32_t>(s.hatched);
        r.rows.push_back(row);
    }
    if (!draw && !r.rows.empty()) r.rows[0].winner = true;
    for (HistorySeat& s : seats) s.winner = !draw && !r.rows.empty() && s.seat == r.rows[0].seats[0];
    if (quitter >= 0) {
        for (HistorySeat& s : seats) s.left = s.seat == quitter;
    }
    r.seats = std::move(seats);
    return r;
}

// The records that the filter and sort tests use (index = age: the last is the newest). 0 - 5 highest-score, 6 and 7 mode 187
std::vector<HistoryRecord> eight_records() {
    std::vector<HistoryRecord> v;
    v.push_back(record_of(kT0, "TINY.LVL", "1v1", 1200, true, -1, false, {seat_of(0, "Ann", "", 900, 10, 2, 5), seat_of(1, "Bob", "", 400, 2, 10, 2)}));
    v.push_back(record_of(kT0 + 100, "TINY.LVL", "ffa4", 6000, true, -1, false,
                          {seat_of(0, "Cy", "", 300, 3, 3, 1), seat_of(1, "", "", 700, 8, 4, 6), seat_of(2, "", "Hard", 500, 5, 5, 4), seat_of(3, "", "Bot", 100, 1, 9, 0)}));
    v.push_back(record_of(kT0 + 200, "TREASURE.LVL", "2v2", 3000, true, -1, false,
                          {seat_of(0, "Dee", "", 600, 6, 6, 6), seat_of(1, "Eve", "", 550, 5, 7, 3), seat_of(2, "Fay", "", 300, 3, 9, 2), seat_of(3, "Gus", "", 200, 2, 10, 1)}));
    v.push_back(record_of(kT0 + 300, "TINY.LVL", "1v1", 2400, true, -1, true, {seat_of(0, "", "Medium", 500, 5, 5, 5), seat_of(1, "", "Medium", 500, 5, 5, 5)}));
    v.push_back(record_of(kT0 + 400, "TREASURE.LVL", "1v1", 800, true, 1, false, {seat_of(0, "Hal", "", 800, 9, 1, 4), seat_of(1, "Ian", "", 100, 1, 9, 1)}));
    v.push_back(record_of(kT0 + 500, "TINY.LVL", "ffa3", 36000, false, -1, false, {seat_of(0, "Jo", "", 250, 2, 2, 2), seat_of(1, "Kim", "", 250, 4, 4, 3), seat_of(2, "", "Easy", 150, 1, 3, 0)}));
    v.push_back(record_of(kT0 + 600, "ISLANDS.LVL", "1v1", 4000, true, -1, false, {seat_of(0, "Lee", "", 7, 7, 3, 0), seat_of(1, "May", "", 4, 4, 7, 0)}, "187"));
    v.push_back(record_of(kT0 + 700, "TINY.LVL", "2v1", 5200, true, -1, true, {seat_of(0, "Ned", "", 6, 6, 6, 0), seat_of(1, "Oz", "", 6, 6, 6, 0), seat_of(2, "Pia", "", 3, 3, 9, 0)}, "187"));
    return v;
}

bool same_seat(const HistorySeat& a, const HistorySeat& b) {
    return a.seat == b.seat && a.name == b.name && a.bot == b.bot && a.score == b.score && a.killed == b.killed && a.lost == b.lost && a.hatched == b.hatched && a.bombs_planted == b.bombs_planted &&
           a.bombs_defused == b.bombs_defused && a.fires_lit == b.fires_lit && a.left == b.left && a.winner == b.winner;
}

bool same_record(const HistoryRecord& a, const HistoryRecord& b) {
    if (a.id != b.id || a.ended_s != b.ended_s || a.map != b.map || a.game != b.game || a.turns != b.turns || a.finished != b.finished || a.quitter != b.quitter || a.mode != b.mode || a.format != b.format ||
        a.draw != b.draw || a.computers_only != b.computers_only || a.details != b.details || a.seats.size() != b.seats.size() || a.rows.size() != b.rows.size())
        return false;
    for (size_t i = 0; i < a.seats.size(); ++i) {
        if (!same_seat(a.seats[i], b.seats[i])) return false;
    }
    for (size_t i = 0; i < a.rows.size(); ++i) {
        const HistoryRow& x = a.rows[i];
        const HistoryRow& y = b.rows[i];
        if (x.seats != y.seats || x.score != y.score || x.killed != y.killed || x.lost != y.lost || x.hatched != y.hatched || x.winner != y.winner) return false;
    }
    return true;
}

std::vector<std::string> ids_of(const HistoryPage& page) {
    std::vector<std::string> out;
    for (const HistoryRecord& r : page.items) out.push_back(r.id);
    return out;
}

// the ids of the records `which` (indexes into `all`), in that order
std::vector<std::string> ids_at(const std::vector<HistoryRecord>& all, std::vector<size_t> which) {
    std::vector<std::string> out;
    for (const size_t i : which) out.push_back(all[i].id);
    return out;
}

const std::function<Recording(const std::string&)> kNoRecordings = [](const std::string&) { return Recording::Removed; };

// A thousand (or more) matches a minute apart, written by ONE statement; every third on TREASURE, every second a 1v1, every fifth mode 187, `best_*` and `turns` from k
bool fill_synthetic(db::Database& database, int n) {
    const std::string fill =
        "WITH RECURSIVE n(k) AS (SELECT 0 UNION ALL SELECT k + 1 FROM n WHERE k < " + std::to_string(n - 1) + ") "
        "INSERT INTO history_matches (id, ended, map, game, turns, finished, quitter, mode, format, result, computers_only, best_score, best_kills, best_hatched, search, rows_json, counted_at) "
        "SELECT 'ants-' || CASE WHEN k % 3 = 0 THEN 'TREASURE' ELSE 'TINY' END || '-' || strftime('%Y%m%d-%H%M%S', 1791469929 + k * 60, 'unixepoch') || 'Z', 1791469929 + k * 60, "
        "CASE WHEN k % 3 = 0 THEN 'TREASURE.LVL' ELSE 'TINY.LVL' END, 'v1', 600 + k, 1, NULL, CASE WHEN k % 5 = 0 THEN '187' ELSE 'highest-score' END, CASE WHEN k % 2 = 0 THEN '1v1' ELSE 'ffa3' END, "
        "'decided', 0, k, k, k, CASE WHEN k % 3 = 0 THEN 'treasure' ELSE 'tiny' END || char(10) || 'player ' || k, '[]', 1 FROM n";
    return database.exec(fill).ok();
}

// A clock in microseconds that moves by `step_us` at every look (the time box looks every few hundred steps of the database, so a query is as long as the number of its steps says: no wall clock in the test)
struct TickClock {
    uint64_t base{1000000};
    uint64_t step_us{0};
    uint64_t calls{0};
    uint64_t operator()() {
        ++calls;
        return base + calls * step_us;
    }
};

// ---- a database with the history store on it -------------------------------------------------------------------------------------------

struct Db {
    TempDir temp;
    int64_t now{kT0};
    db::Database database;
    bool ok{false};
    HistoryStore store;
    Db() : ok(open_server_db(database, (temp.path / "ants.db").string())), store(database, config()) {
        std::string why;
        ok = ok && store.prepare(why);
    }
    HistoryStore::Config config() {
        HistoryStore::Config c;
        c.clock_s = [this]() { return now; };
        return c;
    }
    std::string path() const { return (temp.path / "ants.db").string(); }
    bool fill(const std::vector<HistoryRecord>& records) {
        std::string why;
        for (const HistoryRecord& r : records) {
            if (!store.put(r, why)) return false;
        }
        return true;
    }
};

// The statement text of the list's shapes, for the plan checks: every one of them is written by HistoryStore::query from fixed pieces
std::string plan_of(db::Database& database, const std::string& select) {
    db::Statement st;
    if (!database.prepare(st, "EXPLAIN QUERY PLAN " + select).ok()) return "(no plan)";
    std::string out;
    for (;;) {
        const db::Statement::Step step = st.step();
        if (step != db::Statement::Step::Row) break;
        out += st.column_text(3) + "\n";
    }
    return out;
}

int64_t number_from(db::Database& database, const std::string& sql) {
    db::Statement st;
    if (!database.prepare(st, sql).ok() || st.step() != db::Statement::Step::Row) return -1;
    return st.column_int64(0);
}

// ---- a replay store and a history in a temporary folder, with the feeder between them ----------------------------------------------

struct Rig {
    TempDir temp;
    int64_t now{kT0};                                    // the replay store's clock (a file is named for the second it was kept in)
    fs::path replay_dir;
    db::Database database;
    bool db_ok;
    ReplayStore replays;
    HistoryStore history;
    HistoryFeeder feeder;
    uint32_t ms{1000};
    int idle_asked{0};
    uint64_t work_us{1000};                              // what the feeder's clock says that each look takes (the pace follows it)
    uint64_t work_clock{0};

    explicit Rig(const std::string& maps = maps_dir())
        : replay_dir(temp.path / "replays"), db_ok(open_server_db(database, (temp.path / "ants.db").string())), replays(replay_config()), history(database, history_config()), feeder(history, replays, maps, [this]() { return work_clock += work_us; }) {}

    ReplayConfig replay_config() {
        ReplayConfig c;
        c.dir = replay_dir.string();
        c.game_version = "v0.12.0-test";
        c.build_id = "test";
        c.clock_s = [this]() { return now; };
        c.min_free_bytes = 0;
        c.settle_s = 0;
        return c;
    }
    HistoryStore::Config history_config() {
        HistoryStore::Config c;
        c.clock_s = [this]() { return now; };
        return c;
    }
    bool open() {
        std::string why;
        return db_ok && replays.prepare(why) && history.prepare(why);
    }
    // keeps a recording, `after_s` seconds after the last one; the id it has
    std::string keep(const std::vector<uint8_t>& bytes, int64_t after_s = 10) {
        now += after_s;
        const ReplaySave saved = replays.save(bytes);
        return saved.kept ? saved.file.substr(0, saved.file.size() - std::string(ReplayStore::kExtension).size()) : std::string();
    }
    // the next piece of work, 300 ms after the last (the pace is 250)
    bool step(bool idle = true) {
        ms += 300;
        return feeder.step([&]() {
            ++idle_asked;
            return idle;
        }, ms);
    }
    // the feeder to the end (every file that is due, then the summary line)
    void drain(int max_steps = 40) {
        for (int i = 0; i < max_steps; ++i) step();
    }
    // the replay folder was changed from outside (the arena puts a file there): a look at it
    size_t touch_and_rescan() {
        static int touches = 0;
        fs::last_write_time(replay_dir, fs::file_time_type(std::chrono::hours(100000 + ++touches)));
        return replays.rescan();
    }
};

// ---- the control interface in front of a RoomManager ---------------------------------------------------------------------------------

struct Server {
    TempDir temp;
    int64_t now{kT0};
    db::Database database;
    bool db_ok;
    RoomManager mgr;
    uint32_t ms{1000};

    Server() : db_ok(open_server_db(database, (temp.path / "ants.db").string())), mgr(MapStore(maps_dir())) {}

    bool open(bool with_history = true, HistoryStore::Config history_config = HistoryStore::Config()) {
        ReplayConfig rc;
        rc.dir = (temp.path / "replays").string();
        rc.game_version = "v0.12.0-test";
        rc.build_id = "test";
        rc.clock_s = [this]() { return now; };
        rc.min_free_bytes = 0;
        rc.settle_s = 0;
        std::string why;
        if (!db_ok || !mgr.enable_replays(rc, false, why)) return false;
        return !with_history || mgr.enable_history(database, why, std::move(history_config));
    }
    ~Server() { mgr.disable_history(); }
    std::string keep(const std::vector<uint8_t>& bytes) {
        now += 10;
        const ReplaySave saved = mgr.replay_store()->save(bytes);
        return saved.kept ? saved.file.substr(0, saved.file.size() - std::string(ReplayStore::kExtension).size()) : std::string();
    }
    void run_updates(int n = 30) {
        for (int i = 0; i < n; ++i) {
            ms += 300;
            mgr.update(ms);
        }
    }
    ctl::HttpResponse control(const char* method, const std::string& path, const std::string& query = std::string()) {
        ctl::HttpRequest rq;
        rq.method = method;
        rq.path = path;
        rq.query = query;
        return handle_control(mgr, rq, ms);
    }
    ctl::HttpResponse door(const char* method, const std::string& path, const std::string& query = std::string()) const {
        ctl::HttpRequest rq;
        rq.method = method;
        rq.path = path;
        rq.query = query;
        return handle_public_replays(mgr, rq);
    }
};

}  // namespace

int main() {
    std::cout << "=======================================================\n MATCH HISTORY STORE SUITE (what a match came to, kept for good in the database)\n=======================================================\n";

    // ---- the store on a database ----------------------------------------------------------------------------------------------------

    TEST_CASE("HS1.1 A record is kept with its seats and rows and read back the same (a bot, a quitter, a draw, the mode, the reserved details); the counts follow; a record of the same id replaces the old one") {
        Db d;
        ASSERT_TRUE(d.ok && d.store.available() && d.store.count() == 0 && d.store.marker_count() == 0);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        ASSERT_EQ(d.store.count(), all.size());
        for (const HistoryRecord& want : all) {
            HistoryRecord got;
            ASSERT_TRUE(d.store.find(want.id, got));
            ASSERT_TRUE(same_record(want, got));
        }
        HistoryRecord none;
        ASSERT_FALSE(d.store.find(id_at(kT0 + 99999), none));
        ASSERT_FALSE(d.store.find("not an id", none));
        // the details that a later version keeps are stored as they are
        HistoryRecord with_details = all[0];
        with_details.details = "{\"events\":[{\"t\":1}]}";
        std::string why;
        ASSERT_TRUE(d.store.put(with_details, why));
        HistoryRecord got;
        ASSERT_TRUE(d.store.find(with_details.id, got) && got.details == with_details.details);
        ASSERT_TRUE(d.store.count() == all.size() && number_from(d.database, "SELECT count(*) FROM history_seats WHERE match_id = '" + with_details.id + "'") == 2);     // (replaced, not added)
        // the same id with fewer seats: the old seats are gone
        HistoryRecord fewer = all[1];
        fewer.seats.resize(2);
        fewer.rows.resize(2);
        ASSERT_TRUE(d.store.put(fewer, why) && d.store.find(fewer.id, got) && got.seats.size() == 2 && got.rows.size() == 2);
        ASSERT_EQ(number_from(d.database, "SELECT count(*) FROM history_seats"), int64_t{20});            // (22 seats in the eight records, two of the four seats of the second are gone)
        ASSERT_TRUE(d.store.take_notes().size() == 1);                                           // (only the line of prepare(): nothing went wrong)
    } TEST_END();

    TEST_CASE("HS1.2 What cannot be a record is refused with a reason and the store is untouched and stays available: an id that is no match's, no seat, five seats, a mode or a seat the database refuses") {
        Db d;
        ASSERT_TRUE(d.ok);
        const HistoryRecord good = eight_records()[0];
        std::string why;
        HistoryRecord bad = good;
        bad.id = "../../etc/passwd";
        ASSERT_TRUE(!d.store.put(bad, why) && !why.empty());
        bad = good;
        bad.seats.clear();
        ASSERT_TRUE(!d.store.put(bad, why) && !why.empty());
        bad = good;
        bad.seats.resize(5, good.seats[0]);
        ASSERT_FALSE(d.store.put(bad, why));
        bad = good;
        bad.mode = "chess";                                                                      // (the CHECK of the table says no to this one)
        ASSERT_FALSE(d.store.put(bad, why));
        ASSERT_TRUE(contains(why, "CHECK") || contains(why, "constraint"));
        bad = good;
        bad.seats[1].seat = 0;                                                                   // (two seats with one number: the key says no)
        ASSERT_FALSE(d.store.put(bad, why));
        ASSERT_TRUE(d.store.available() && d.store.count() == 0 && number_from(d.database, "SELECT count(*) FROM history_seats") == 0);       // (the failed transactions left nothing behind)
        ASSERT_TRUE(d.store.put(good, why) && d.store.count() == 1);                              // ... and the next one goes in
        const std::vector<std::string> notes = d.store.take_notes();
        ASSERT_EQ(lines_with(notes, "is refused by the database"), size_t{2});                    // (the two the database refused; the others never reached it)
    } TEST_END();

    TEST_CASE("HS1.3 The records are in the file: a new connection to the same database lists them, in the order the list gives; the file and its tables outlive the store") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        db::Database again;
        ASSERT_TRUE(again.open(d.path()).ok());
        HistoryStore other(again);
        std::string why;
        ASSERT_TRUE(other.prepare(why) && other.count() == all.size());
        const HistoryPage page = other.query(HistoryQuery(), kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == all.size() && page.items.size() == all.size());
        ASSERT_EQ(page.items.front().id, all.back().id);
        ASSERT_TRUE(same_record(page.items.back(), all.front()));
        // a database without the tables (an older program's) is not a history: the store says why and stays unavailable
        db::Database bare;
        ASSERT_TRUE(bare.open(":memory:").ok());
        HistoryStore none(bare);
        ASSERT_TRUE(!none.prepare(why) && !none.available() && contains(why, "history"));
    } TEST_END();

    // ---- the list ---------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS2.1 The list is filtered by format, result, who, colour, mode and text, alone and together, newest first, with the count of what fits") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        const auto list = [&](const std::function<void(HistoryQuery&)>& tweak) {
            HistoryQuery q;
            tweak(q);
            return ids_of(d.store.query(q, kNoRecordings));
        };
        ASSERT_EQ(list([](HistoryQuery&) {}), ids_at(all, {7, 6, 5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.format = HistoryQuery::Format::OneVsOne; }), ids_at(all, {6, 4, 3, 0}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.format = HistoryQuery::Format::Teams; }), ids_at(all, {7, 2}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.format = HistoryQuery::Format::Free; }), ids_at(all, {5, 1}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.result = HistoryQuery::Result::Draw; }), ids_at(all, {7, 3}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.result = HistoryQuery::Result::Cut; }), ids_at(all, {5, 4}));          // (a player left, or the time ran out; a draw is none of these)
        ASSERT_EQ(list([](HistoryQuery& q) { q.result = HistoryQuery::Result::Decided; }), ids_at(all, {6, 2, 1, 0}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.who = HistoryQuery::Who::Computers; }), ids_at(all, {3}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.who = HistoryQuery::Who::People; }), ids_at(all, {7, 6, 5, 4, 2, 1, 0}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.colour = 3; }), ids_at(all, {2, 1}));                                   // (a seat of that colour took part)
        ASSERT_EQ(list([](HistoryQuery& q) { q.colour = 2; }), ids_at(all, {7, 5, 2, 1}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.mode = HistoryQuery::Mode::Kills187; }), ids_at(all, {7, 6}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.mode = HistoryQuery::Mode::HighestScore; }), ids_at(all, {5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.mode = HistoryQuery::Mode::Kills187; q.format = HistoryQuery::Format::OneVsOne; }), ids_at(all, {6}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "tiny"; }), ids_at(all, {7, 5, 3, 1, 0}));                       // (part of the map's name, any case)
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "TREAS"; }), ids_at(all, {4, 2}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "kim"; }), ids_at(all, {5}));                                      // (a name that was typed)
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "bot (hard)"; }), ids_at(all, {1}));                              // (what the pages show for a computer)
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "bot"; }), ids_at(all, {5, 3, 1}));
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "red"; }), ids_at(all, {1}));                                     // (a seat with no name shows its colour: the second seat of record 1 is Red)
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "green"; }), std::vector<std::string>());                        // (and the first one is "Cy": nobody shows Green)
        ASSERT_EQ(list([](HistoryQuery& q) { q.text = "nobody-has-this"; }), std::vector<std::string>());
        ASSERT_EQ(list([](HistoryQuery& q) { q.format = HistoryQuery::Format::OneVsOne; q.result = HistoryQuery::Result::Decided; q.who = HistoryQuery::Who::People; q.colour = 1; q.text = "tiny"; }), ids_at(all, {0}));
        HistoryQuery q;
        q.format = HistoryQuery::Format::OneVsOne;
        const HistoryPage page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == 4 && page.items.size() == 4 && d.store.count() == 8);
        ASSERT_TRUE(page.items[0].seats.size() == 2 && page.items[0].seats[1].name == "May" && page.items[0].rows.size() == 2);       // (the page holds whole records)
    } TEST_END();

    TEST_CASE("HS2.2 The list is sorted by new, old, score, kills, hatched, long or short; a tie keeps the newer match first; the score of a match is its best seat's (mode 187: its kills)") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        const auto sorted = [&](HistoryQuery::Sort sort) {
            HistoryQuery q;
            q.sort = sort;
            return ids_of(d.store.query(q, kNoRecordings));
        };
        ASSERT_EQ(sorted(HistoryQuery::Sort::New), ids_at(all, {7, 6, 5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(sorted(HistoryQuery::Sort::Old), ids_at(all, {0, 1, 2, 3, 4, 5, 6, 7}));
        ASSERT_EQ(sorted(HistoryQuery::Sort::Score), ids_at(all, {0, 4, 1, 2, 3, 5, 6, 7}));        // 900, 800, 700, 600, 500, 250, 7, 6
        ASSERT_EQ(sorted(HistoryQuery::Sort::Kills), ids_at(all, {0, 4, 1, 6, 7, 2, 3, 5}));        // 10, 9, 8, 7, then 6 twice (the newer, 7, before 2), 5, 4
        ASSERT_EQ(sorted(HistoryQuery::Sort::Hatched), ids_at(all, {2, 1, 3, 0, 4, 5, 7, 6}));      // 6 twice (2 before 1), 5 twice (3 before 0), 4, 3, 0 twice (7 before 6)
        ASSERT_EQ(sorted(HistoryQuery::Sort::Long), ids_at(all, {5, 1, 7, 6, 2, 3, 0, 4}));
        ASSERT_EQ(sorted(HistoryQuery::Sort::Short), ids_at(all, {4, 0, 3, 2, 6, 7, 1, 5}));
    } TEST_END();

    TEST_CASE("HS2.3 Paging: offset and limit cut the list that the filters made; the limit is at most 100; a page past the end is empty; the count is of the whole list") {
        Db d;
        ASSERT_TRUE(d.ok);
        std::vector<HistoryRecord> many;
        for (int k = 0; k < 130; ++k) many.push_back(record_of(kT0 + k, "TINY.LVL", "1v1", 1000u + static_cast<uint32_t>(k), true, -1, false, {seat_of(0, "Ann", "", k, 1, 1, 1), seat_of(1, "Bob", "", 1, 1, 1, 1)}));
        ASSERT_TRUE(d.fill(many));
        HistoryQuery q;
        HistoryPage page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.count == 130 && page.items.size() == 50 && page.items.front().id == many.back().id);                 // (the default limit is 50)
        q.limit = 1000;
        page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.count == 130 && page.items.size() == 100);                                                               // (and the most is 100)
        q.limit = 10;
        q.offset = 125;
        page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.count == 130 && page.items.size() == 5 && page.items.back().id == many.front().id);
        q.offset = 5000;
        page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == 130 && page.items.empty());
        q.limit = 0;                                                                                                              // (a limit of 0 is one: the route refuses it, the store does not guess more)
        q.offset = 3;
        page = d.store.query(q, kNoRecordings);
        ASSERT_TRUE(page.items.size() == 1 && page.items[0].id == many[126].id);
    } TEST_END();

    TEST_CASE("HS2.4 The text search takes % _ \\ ' and \" in a name as themselves (the pattern's wildcards are escaped), and nothing in a request is ever part of the SQL: the tables are still there after the worst of them") {
        Db d;
        ASSERT_TRUE(d.ok);
        std::vector<HistoryRecord> v;
        const char* names[] = {"50% off", "a_b", "back\\slash", "O'Neil", "say \"hi\"", "plain", "x'; DROP TABLE history_matches; --", "ab"};
        for (int k = 0; k < 8; ++k) v.push_back(record_of(kT0 + k, "TINY.LVL", "1v1", 1000, true, -1, false, {seat_of(0, names[k], "", 5, 1, 1, 1), seat_of(1, "", "Hard", 4, 1, 1, 1)}));
        ASSERT_TRUE(d.fill(v));
        const auto find = [&](const std::string& text) {
            HistoryQuery q;
            q.text = text;
            return ids_of(d.store.query(q, kNoRecordings));
        };
        ASSERT_EQ(find("50%"), ids_at(v, {0}));
        ASSERT_EQ(find("%"), ids_at(v, {0}));                          // (a lone % is a percent sign, not "everything")
        ASSERT_EQ(find("a_b"), ids_at(v, {1}));                        // (and "_" is not "any character": "ab" and "a_b" are told apart)
        ASSERT_EQ(find("_"), ids_at(v, {6, 1}));                       // (the other name with an underscore is the one that tries to drop a table)
        ASSERT_EQ(find("\\"), ids_at(v, {2}));
        ASSERT_EQ(find("k\\s"), ids_at(v, {2}));
        ASSERT_EQ(find("o'neil"), ids_at(v, {3}));
        ASSERT_EQ(find("\"hi\""), ids_at(v, {4}));
        ASSERT_EQ(find("a%b"), std::vector<std::string>());            // (no wildcard in the text: "a_b" and "ab" do not match "a%b")
        ASSERT_EQ(find("x'; drop table history_matches; --"), ids_at(v, {6}));
        ASSERT_EQ(find("'; DROP TABLE history_matches; --"), ids_at(v, {6}));
        ASSERT_EQ(find("') OR 1=1 --"), std::vector<std::string>());
        ASSERT_EQ(find("%' ESCAPE '"), std::vector<std::string>());
        ASSERT_TRUE(d.store.available() && d.store.count() == 8 && number_from(d.database, "SELECT count(*) FROM history_matches") == 8);
        ASSERT_EQ(find("hard").size(), size_t{8});                      // (every record has a "Bot (Hard)")
        ASSERT_EQ(find("").size(), size_t{8});
        ASSERT_EQ(find(std::string(5000, 'a')), std::vector<std::string>());      // (a text far longer than any name: it matches nothing and costs nothing)
    } TEST_END();

    TEST_CASE("HS2.5 The recording's state is asked only when the list filters by it (once for each match that fits the other filters), and the page is cut from the matches that have that state") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        std::vector<std::string> asked;
        const auto state = [&](const std::string& id) {
            asked.push_back(id);
            // the three oldest are gone, 3 and 4 are kept but of other rules, the rest can be watched
            for (size_t k = 0; k < 3; ++k) {
                if (id == all[k].id) return Recording::Removed;
            }
            for (size_t k = 3; k < 5; ++k) {
                if (id == all[k].id) return Recording::Old;
            }
            return Recording::Watch;
        };
        HistoryQuery q;
        const HistoryPage plain = d.store.query(q, state);
        ASSERT_TRUE(plain.count == 8 && asked.empty());                                                       // (no filter by the recording: never asked)
        q.rec = HistoryQuery::Rec::Watch;
        ASSERT_EQ(ids_of(d.store.query(q, state)), ids_at(all, {7, 6, 5}));
        ASSERT_EQ(asked.size(), size_t{8});                                                                    // (each once)
        q.rec = HistoryQuery::Rec::Old;
        ASSERT_EQ(ids_of(d.store.query(q, state)), ids_at(all, {4, 3}));
        q.rec = HistoryQuery::Rec::Removed;
        q.limit = 2;
        q.offset = 1;
        HistoryPage page = d.store.query(q, state);
        ASSERT_TRUE(page.count == 3 && ids_of(page) == ids_at(all, {1, 0}));                                  // (the whole count, the window cut from it)
        q.mode = HistoryQuery::Mode::HighestScore;
        q.format = HistoryQuery::Format::OneVsOne;
        q.limit = 50;
        q.offset = 0;
        asked.clear();
        page = d.store.query(q, state);
        ASSERT_TRUE(ids_of(page) == ids_at(all, {0}) && asked.size() == 3);                                   // (asked only for the 1v1 matches of the original's rules: 0, 3 and 4)
        ASSERT_TRUE(page.items[0].seats.size() == 2);
    } TEST_END();

    TEST_CASE("HS2.6 The indexes carry the list: the plan of every sort and of each filter in the newest-first order uses an index and builds no temporary tree") {
        Db d;
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(d.fill(eight_records()));
        const std::string from = "SELECT m.id FROM history_matches m";
        const char* tail = " LIMIT 50";
        const std::vector<std::pair<std::string, std::string>> shapes = {
            {"new", from + " ORDER BY m.ended DESC, m.id DESC" + tail},
            {"old", from + " ORDER BY m.ended ASC, m.id ASC" + tail},
            {"score", from + " ORDER BY m.best_score DESC, m.ended DESC, m.id DESC" + tail},
            {"kills", from + " ORDER BY m.best_kills DESC, m.ended DESC, m.id DESC" + tail},
            {"hatched", from + " ORDER BY m.best_hatched DESC, m.ended DESC, m.id DESC" + tail},
            {"long", from + " ORDER BY m.turns DESC, m.ended DESC, m.id DESC" + tail},
            {"short", from + " ORDER BY m.turns ASC, m.ended DESC, m.id DESC" + tail},
            {"mode", from + " WHERE m.mode = '187' ORDER BY m.ended DESC, m.id DESC" + tail},
            {"format", from + " WHERE m.format = '1v1' ORDER BY m.ended DESC, m.id DESC" + tail},
            {"result", from + " WHERE m.result = 'draw' ORDER BY m.ended DESC, m.id DESC" + tail},
            {"who", from + " WHERE m.computers_only = 1 ORDER BY m.ended DESC, m.id DESC" + tail},
        };
        for (const auto& shape : shapes) {
            const std::string plan = plan_of(d.database, shape.second);
            ASSERT_TRUE(contains(plan, "INDEX history_matches_") || contains(plan, "SEARCH"));
            ASSERT_FALSE(contains(plan, "TEMP B-TREE"));
            ASSERT_FALSE(contains(plan, "SCAN history_matches m\n") && !contains(plan, "USING"));
        }
        // the page of a colour is a lookup of the seat's key
        const std::string colour = plan_of(d.database, from + " WHERE EXISTS (SELECT 1 FROM history_seats s WHERE s.match_id = m.id AND s.seat = 2) ORDER BY m.ended DESC, m.id DESC LIMIT 50");
        ASSERT_TRUE(contains(colour, "USING PRIMARY KEY (match_id=? AND seat=?)"));
    } TEST_END();

    TEST_CASE("HS2.7 A thousand records: a page from the middle of the list is the right one, with its filters, and the list answers at once") {
        Db d;
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(fill_synthetic(d.database, 1000));                                // (a thousand matches a minute apart; the fill is not what is measured)
        HistoryStore store(d.database);
        std::string why;
        ASSERT_TRUE(store.prepare(why) && store.count() == 1000);
        const auto id_of = [](int k) { return id_at(kT0 + k * 60, k % 3 == 0 ? "TREASURE" : "TINY"); };
        const auto start = std::chrono::steady_clock::now();
        HistoryQuery q;
        q.offset = 500;
        q.limit = 10;
        HistoryPage page = store.query(q, kNoRecordings, HistoryStore::Door::Owner);
        ASSERT_TRUE(page.ok && page.count == 1000 && page.items.size() == 10);
        for (int i = 0; i < 10; ++i) ASSERT_EQ(page.items[static_cast<size_t>(i)].id, id_of(999 - 500 - i));
        q = HistoryQuery();
        q.sort = HistoryQuery::Sort::Long;                                           // (longest first: the highest k)
        q.limit = 3;
        page = store.query(q, kNoRecordings, HistoryStore::Door::Owner);
        ASSERT_TRUE(page.items.size() == 3 && page.items[0].id == id_of(999) && page.items[2].id == id_of(997));
        q = HistoryQuery();
        q.mode = HistoryQuery::Mode::Kills187;
        q.format = HistoryQuery::Format::OneVsOne;
        q.text = "treasure";
        q.offset = 2;
        q.limit = 2;
        page = store.query(q, kNoRecordings, HistoryStore::Door::Owner);                                        // (k divisible by 5, by 2 and by 3: 30, 60, ...: 34 of them below 1000; the newest first, from the third)
        ASSERT_TRUE(page.ok && page.count == 34 && page.items.size() == 2 && page.items[0].id == id_of(930) && page.items[1].id == id_of(900));
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(took < 2000);
    } TEST_END();

    TEST_CASE("HS2.8 The time box: a query that runs longer than its box is stopped by the database, the answer says TooHeavy and the store stays available; the owner's box is larger than a visitor's") {
        Db d;
        ASSERT_TRUE(d.ok && fill_synthetic(d.database, 3000));
        TickClock clock;
        HistoryStore::Config config;
        config.clock_us = [&clock]() { return clock(); };
        HistoryStore store(d.database, config);
        std::string why;
        ASSERT_TRUE(store.prepare(why) && store.count() == 3000);
        HistoryQuery scan;
        scan.text = "zzzz";                                                     // matches nothing: the whole table is read to know it
        clock.step_us = 0;                                                      // (time stands still: nothing is too long)
        HistoryPage page = store.query(scan, kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == 0 && page.fail == HistoryPage::Fail::None);
        clock.step_us = 1000;                                                   // a millisecond at every look: the scan has hundreds of looks
        page = store.query(scan, kNoRecordings);
        ASSERT_FALSE(page.ok);
        ASSERT_TRUE(page.fail == HistoryPage::Fail::TooHeavy && page.items.empty());
        ASSERT_TRUE(store.available() && store.unavailable_reason().empty());   // (the database is fine: a heavy query is no failure of it)
        ASSERT_TRUE(store.take_notes().size() == 1);                           // (the line of prepare() and nothing else: no failure was logged)
        // 0.1 ms a look: the scan takes more than the visitor's 15 ms and less than the owner's 100 ms
        clock.step_us = 100;
        ASSERT_TRUE(store.query(scan, kNoRecordings, HistoryStore::Door::Public).fail == HistoryPage::Fail::TooHeavy);
        page = store.query(scan, kNoRecordings, HistoryStore::Door::Owner);
        ASSERT_TRUE(page.ok && page.count == 0);
        // the page is a cheap query and goes through the same box
        page = store.query(HistoryQuery(), kNoRecordings, HistoryStore::Door::Public);
        ASSERT_TRUE(page.ok || page.fail == HistoryPage::Fail::Busy);
        // the handler is taken off after a query: a statement of the database outside of it is not boxed
        clock.step_us = 1000000;
        ASSERT_EQ(number_from(d.database, "SELECT count(*) FROM history_matches WHERE search LIKE '%zzzz%'"), int64_t{0});
    } TEST_END();

    TEST_CASE("HS2.9 The budget of the public door: queries are paid for by the time they take; when it is used up the answer is Busy at once and the database is not touched; it fills again with time; the owner is not held by it") {
        Db d;
        ASSERT_TRUE(d.ok && fill_synthetic(d.database, 3000));
        TickClock clock;
        HistoryStore::Config config;
        config.clock_us = [&clock]() { return clock(); };
        HistoryStore store(d.database, config);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        HistoryQuery scan;
        scan.text = "zzzz";
        clock.step_us = 1000;
        int heavy = 0;
        HistoryPage page;
        for (; heavy < 40; ++heavy) {                                             // every one costs its box (15 ms): the 100 ms of the burst last about seven
            page = store.query(scan, kNoRecordings);
            if (page.fail == HistoryPage::Fail::Busy) break;
            ASSERT_TRUE(page.fail == HistoryPage::Fail::TooHeavy);
        }
        ASSERT_TRUE(heavy >= 5 && heavy <= 9);
        const uint64_t looks = clock.calls;
        page = store.query(scan, kNoRecordings);
        ASSERT_TRUE(!page.ok && page.fail == HistoryPage::Fail::Busy && page.items.empty());
        ASSERT_EQ(clock.calls, looks + 1);                                         // (Busy cost one look at the clock and no step of the database)
        ASSERT_TRUE(store.available());
        page = store.query(scan, kNoRecordings, HistoryStore::Door::Owner);       // the owner's door has no budget: its own box (100 ms) only
        ASSERT_TRUE(page.fail == HistoryPage::Fail::TooHeavy);
        clock.step_us = 0;
        clock.base += 100000;                                                      // 0.1 s: five milliseconds earned
        ASSERT_TRUE(store.query(scan, kNoRecordings).fail == HistoryPage::Fail::None || store.query(scan, kNoRecordings).fail == HistoryPage::Fail::Busy);
        clock.base += 3000000;                                                     // three seconds: the burst is full again
        page = store.query(scan, kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == 0);
        clock.step_us = 1000;                                                      // and it can be used up again
        int again = 0;
        while (again < 40 && store.query(scan, kNoRecordings).fail != HistoryPage::Fail::Busy) ++again;
        ASSERT_TRUE(again >= 5 && again <= 9);
    } TEST_END();

    TEST_CASE("HS2.10 Sixteen thousand matches: the page of a plain list reads two statements for its seats whatever its size, and the heavy lists of a visitor end inside their box (measured with the real clock, with generous margins)") {
        Db d;
        ASSERT_TRUE(d.ok && fill_synthetic(d.database, 16000));
        HistoryStore store(d.database);
        std::string why;
        ASSERT_TRUE(store.prepare(why) && store.count() == 16000);
        const auto since = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count(); };
        const auto removed = [](const std::string&) { return Recording::Removed; };
        // the three lists that were measured at 125 ms, 25 ms and 50 - 70 ms on the main thread before the box: a visitor's query now ends inside its box or is refused
        std::vector<HistoryQuery> heavy(3);
        heavy[0].rec = HistoryQuery::Rec::Removed;
        heavy[0].colour = 3;
        heavy[0].sort = HistoryQuery::Sort::Long;
        heavy[0].limit = 100;
        heavy[0].text = "e";
        heavy[1].text = "zzzz";
        heavy[2].sort = HistoryQuery::Sort::Score;
        heavy[2].text = "zzzzzzzzz";
        for (const HistoryQuery& q : heavy) {
            d.database.exec("SELECT 1");
            HistoryStore fresh(d.database);                                          // (a full budget for each)
            ASSERT_TRUE(fresh.prepare(why));
            const auto t = std::chrono::steady_clock::now();
            const HistoryPage page = fresh.query(q, removed, HistoryStore::Door::Public);
            ASSERT_TRUE(since(t) < 60);                                               // (the box is 15 ms; the margin is for a slow machine)
            ASSERT_TRUE(page.ok || page.fail == HistoryPage::Fail::TooHeavy);
            ASSERT_TRUE(fresh.available());
        }
        // a list without a filter is a few ms whatever the size of the table, and a page of 100 with seats is read by statements that do not grow with it
        for (int k = 0; k < 100; ++k) {
            HistoryRecord r = record_of(kT0 + 2000000 + k * 60, "TINY.LVL", "1v1", 1200, true, -1, false, {seat_of(0, "Ann", "", 9, 1, 1, 1), seat_of(1, "Bob", "", 4, 1, 1, 1)});
            ASSERT_TRUE(store.put(r, why));
        }
        HistoryQuery q;
        q.limit = 100;
        const auto t = std::chrono::steady_clock::now();
        const HistoryPage page = store.query(q, kNoRecordings, HistoryStore::Door::Owner);
        ASSERT_TRUE(since(t) < 500);
        ASSERT_TRUE(page.ok && page.items.size() == 100 && page.count == 16100);
        for (const HistoryRecord& r : page.items) ASSERT_EQ(r.seats.size(), size_t{2});              // (the seats of all hundred, from one statement)
        // the plan of the seats statement: one search of the key for a page, not one for each record
        const std::string plan = plan_of(d.database, "SELECT match_id, seat FROM history_seats WHERE match_id IN (?, ?, ?) ORDER BY match_id, seat");
        ASSERT_TRUE(contains(plan, "PRIMARY KEY") || contains(plan, "SEARCH"));
    } TEST_END();

    // ---- markers ----------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS3.1 The owner takes a match out: with its recording still there a marker stays (the match is known, not listed, not counted again); without it nothing stays; a match that is not there is Missing") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        std::string why;
        ASSERT_TRUE(d.store.knows(all[0].id) && d.store.knows(all[7].id) && !d.store.knows(id_at(kT0 + 5000)));
        ASSERT_TRUE(d.store.remove(all[0].id, true, why) == HistoryStore::Removal::Done);
        ASSERT_TRUE(d.store.count() == 7 && d.store.marker_count() == 1 && d.store.knows(all[0].id));
        HistoryRecord gone;
        ASSERT_FALSE(d.store.find(all[0].id, gone));
        ASSERT_EQ(number_from(d.database, "SELECT count(*) FROM history_seats WHERE match_id = '" + all[0].id + "'"), int64_t{0});          // (the seats went with it)
        ASSERT_TRUE(d.store.remove(all[1].id, false, why) == HistoryStore::Removal::Done);
        ASSERT_TRUE(d.store.count() == 6 && d.store.marker_count() == 1 && !d.store.knows(all[1].id));
        ASSERT_TRUE(d.store.remove(all[1].id, true, why) == HistoryStore::Removal::Missing);                                                  // (already gone: no marker made)
        ASSERT_TRUE(d.store.remove(id_at(kT0 + 5000), true, why) == HistoryStore::Removal::Missing && d.store.marker_count() == 1);
        ASSERT_TRUE(d.store.remove("junk", true, why) == HistoryStore::Removal::Missing);
        // keep_unknown drops what is a record or a marker, keeps the order of the rest
        std::vector<std::string> ids = {all[0].id, id_at(kT0 + 6000), all[2].id, id_at(kT0 + 6001), all[1].id};
        ASSERT_TRUE(d.store.keep_unknown(ids));
        ASSERT_EQ(ids, (std::vector<std::string>{id_at(kT0 + 6000), id_at(kT0 + 6001), all[1].id}));
        // a marker stays across a new connection; a record put again clears it
        db::Database again;
        ASSERT_TRUE(again.open(d.path()).ok());
        HistoryStore other(again);
        ASSERT_TRUE(other.prepare(why) && other.marker_count() == 1 && other.knows(all[0].id));
        ASSERT_TRUE(d.store.put(all[0], why) && d.store.marker_count() == 0 && d.store.count() == 7);
        // markers go when their recording does: pruned by what the caller still needs
        ASSERT_TRUE(d.store.remove(all[2].id, true, why) == HistoryStore::Removal::Done && d.store.remove(all[3].id, true, why) == HistoryStore::Removal::Done);
        ASSERT_EQ(d.store.prune_markers([&](const std::string& id) { return id == all[2].id; }), size_t{1});
        ASSERT_TRUE(d.store.marker_count() == 1 && d.store.knows(all[2].id) && !d.store.knows(all[3].id));
        ASSERT_EQ(d.store.prune_markers([](const std::string&) { return true; }), size_t{0});
    } TEST_END();

    // ---- a database that stops answering ------------------------------------------------------------------------------------------------

    TEST_CASE("HS4.1 A database that stops answering: the first error is logged once, the store is unavailable (a list says so, a put is refused, nothing throws), it tries again every minute and the way back is logged once") {
        Db d;
        ASSERT_TRUE(d.ok);
        const std::vector<HistoryRecord> all = eight_records();
        ASSERT_TRUE(d.fill(all));
        (void)d.store.take_notes();
        db::Database other;                                                         // (another connection takes a table away, as a broken file or a full disk would)
        ASSERT_TRUE(other.open(d.path()).ok());
        ASSERT_TRUE(other.exec("ALTER TABLE history_seats RENAME TO history_seats_away").ok());
        HistoryPage page = d.store.query(HistoryQuery(), kNoRecordings);
        ASSERT_FALSE(page.ok);
        ASSERT_TRUE(!d.store.available() && !d.store.unavailable_reason().empty());
        HistoryRecord got;
        ASSERT_FALSE(d.store.find(all[0].id, got));
        std::string why;
        ASSERT_FALSE(d.store.put(all[0], why));
        ASSERT_TRUE(d.store.remove(all[0].id, true, why) == HistoryStore::Removal::Missing);
        ASSERT_TRUE(d.store.knows("anything"));                                     // (down: nothing is counted)
        std::vector<std::string> ids = {id_at(kT0 + 6000)};
        ASSERT_TRUE(!d.store.keep_unknown(ids) && ids.empty());
        std::vector<std::string> notes = d.store.take_notes();
        ASSERT_TRUE(notes.size() == 1 && contains(notes[0], "unavailable") && contains(notes[0], "history_seats"));       // (one line for all of that)
        // a retry before the minute is over finds nothing to do; after it, with the table still away, the store stays down and says nothing more
        uint32_t ms = 1000;
        d.store.maintain(ms);                                                       // (arms the timer)
        d.store.maintain(ms + HistoryStore::kRetryMs - 1);
        ASSERT_FALSE(d.store.available());
        d.store.maintain(ms + HistoryStore::kRetryMs);
        ASSERT_TRUE(!d.store.available() && d.store.take_notes().empty());
        // the table is back: the next retry (a minute after the last) brings the store back, and says so once
        ASSERT_TRUE(other.exec("ALTER TABLE history_seats_away RENAME TO history_seats").ok());
        d.store.maintain(ms + HistoryStore::kRetryMs + 10);
        ASSERT_FALSE(d.store.available());
        d.store.maintain(ms + 2 * HistoryStore::kRetryMs);
        ASSERT_TRUE(d.store.available() && d.store.unavailable_reason().empty() && d.store.count() == all.size());
        notes = d.store.take_notes();
        ASSERT_TRUE(notes.size() == 1 && contains(notes[0], "available"));
        page = d.store.query(HistoryQuery(), kNoRecordings);
        ASSERT_TRUE(page.ok && page.count == all.size());
        d.store.maintain(ms + 3 * HistoryStore::kRetryMs);                          // (an available store has nothing to retry)
        ASSERT_TRUE(d.store.available() && d.store.take_notes().empty());
    } TEST_END();

    // ---- the feeder -------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS5.1 The matches that the replay store holds are played again one at a time, the newest first, never faster than the pace, only while the server is quiet; each becomes a record with the numbers of its own replay") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string id0 = rig.keep(match_file(0));
        const std::string id1 = rig.keep(match_file(1));
        const std::string id2 = rig.keep(match_file(2));
        ASSERT_TRUE(!id0.empty() && !id1.empty() && !id2.empty() && rig.history.count() == 0);
        ASSERT_FALSE(rig.step(false));                                               // (not quiet: not played; asked once)
        ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.pending() == 0 && rig.idle_asked == 1);
        ASSERT_TRUE(rig.step());                                                     // quiet: the newest first
        ASSERT_TRUE(rig.history.count() == 1 && rig.history.knows(id2) && !rig.history.knows(id1) && rig.feeder.pending() == 2);
        // faster than the pace: nothing happens
        const uint32_t before = rig.ms;
        ASSERT_FALSE(rig.feeder.step([] { return true; }, before + 100));
        ASSERT_EQ(rig.history.count(), size_t{1});
        ASSERT_TRUE(rig.step() && rig.step());
        ASSERT_TRUE(rig.history.count() == 3 && rig.feeder.pending() == 0 && rig.feeder.counted() == 3);
        rig.drain(5);
        const std::vector<std::string> notes = rig.history.take_notes();
        ASSERT_EQ(lines_with(notes, "3 match(es) counted, 0 left out; 3 kept in all"), size_t{1});                          // (one summary line for the run)
        // the record is the match: the same as a play of the stored recording
        HistoryRecord rec;
        ASSERT_TRUE(rig.history.find(id1, rec));
        const Played played = play_file(match_file(1));
        ASSERT_TRUE(played.ok);
        ASSERT_TRUE(same_record(rec, make_history_record(id1, rec.ended_s, played.rep, played.outcome)));
        ASSERT_TRUE(rec.map == "TINY.LVL" && rec.game == "v0.12.0-test" && rec.mode == "highest-score" && rec.format == "1v1" && rec.seats.size() == 2 && rec.rows.size() == 2);
        ASSERT_TRUE(rec.seats[0].name == "Cy" && rec.seats[1].name == "Di" && rec.finished == played.outcome.match_over && rec.turns == played.outcome.turns);
        ASSERT_TRUE(rec.draw || rec.rows[0].winner);
        // the numbers are the engine's own for each seat
        for (const HistorySeat& s : rec.seats) {
            ASSERT_EQ(s.killed, played.outcome.result.stats[s.seat].enemy_killed);
            ASSERT_EQ(s.lost, played.outcome.result.stats[s.seat].friendly_lost);
            ASSERT_EQ(s.score, played.outcome.result.stats[s.seat].score);
        }
        // the quitter's recording: his row is last and he is marked left; the free-for-all one has a bot and a person
        HistoryRecord quit;
        ASSERT_TRUE(rig.history.find(id2, quit));
        ASSERT_TRUE(quit.quitter == 1 && quit.seats[1].left && !quit.seats[0].left && quit.rows.back().seats[0] == 1 && std::string(quit.result()) == "cut");
        ASSERT_TRUE(quit.rows[0].winner && quit.seats[0].winner && !quit.seats[1].winner);
        HistoryRecord four;
        ASSERT_TRUE(rig.history.find(id0, four));
        ASSERT_TRUE(four.format == "ffa4" && four.seats[2].bot == "Hard" && four.seats[3].bot == "Bot" && four.seats[0].name == "Ann" && !four.computers_only);
    } TEST_END();

    TEST_CASE("HS5.2 A match that arrives later (a room's own, a file of the bot arena put in the folder) is counted when the replay store has seen it; one that is deleted before its turn is not played; nothing is played twice") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        rig.drain(4);
        ASSERT_TRUE(rig.history.count() == 1 && rig.history.knows(a));
        const size_t asked = static_cast<size_t>(rig.idle_asked);
        rig.drain(4);
        ASSERT_EQ(static_cast<size_t>(rig.idle_asked), asked);                         // (nothing to do: the quiet server is not even asked)
        const std::string b = rig.keep(match_file(2));
        const std::string c = rig.keep(match_file(3));
        ASSERT_TRUE(rig.replays.remove(history_recording_file(c)));                    // (the owner deleted it before its turn)
        rig.drain(6);
        ASSERT_TRUE(rig.history.count() == 2 && rig.history.knows(b) && !rig.history.knows(c) && rig.feeder.counted() == 2);
        // a file put into the folder from outside
        const std::string stamp = id_at(kT0 + 5000);
        {
            std::ofstream out(rig.replay_dir / (stamp + ".antsrep"), std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(match_file(3).data()), static_cast<std::streamsize>(match_file(3).size()));
        }
        ASSERT_EQ(rig.touch_and_rescan(), size_t{1});
        rig.drain(6);
        ASSERT_TRUE(rig.history.count() == 3 && rig.history.knows(stamp));
        rig.drain(6);
        ASSERT_EQ(rig.feeder.counted(), size_t{3});
    } TEST_END();

    TEST_CASE("HS5.3 A recording that cannot be counted is left out once with a line in the log (a map that is not here, a map of the same name that is another file, a recording of other rules, a damaged file); the good one next to them is counted") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        TempDir other_maps;                                                            // a folder with a TINY.LVL that is another file than the match was played on
        {
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            text.push_back('x');
            std::ofstream out(other_maps.path / "TINY.LVL", std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        Rig lost(other_maps.path.string());
        ASSERT_TRUE(lost.open());
        const std::string id = lost.keep(match_file(1));
        lost.drain(4);
        ASSERT_TRUE(lost.history.count() == 0 && lost.feeder.left_out() == 1);
        std::vector<std::string> notes = lost.history.take_notes();
        ASSERT_EQ(lines_with(notes, id + " is not counted"), size_t{1});
        lost.drain(10);
        ASSERT_TRUE(lines_with(lost.history.take_notes(), "is not counted") == 0 && lost.feeder.left_out() == 1);              // (not tried again, not told again)
        TempDir empty_maps;                                                            // no map at all
        Rig missing(empty_maps.path.string());
        ASSERT_TRUE(missing.open());
        missing.keep(match_file(1));
        missing.drain(4);
        ASSERT_TRUE(missing.history.count() == 0 && missing.feeder.left_out() == 1);
        // a recording of other rules (sim_rules 99) and a damaged file next to a good one
        Rig mixed;
        ASSERT_TRUE(mixed.open());
        MatchSpec odd;
        odd.seed = 21;
        odd.turns = 300;
        odd.roster = 0x03;
        odd.sim_rules = 99;
        const std::vector<uint8_t> foreign = record_match(odd);
        ASSERT_FALSE(foreign.empty());
        mixed.keep(foreign);
        {
            std::ofstream out(mixed.replay_dir / (id_at(kT0 + 3000) + ".antsrep"), std::ios::binary | std::ios::trunc);
            out << "this is not a recording";
        }
        const std::string good = mixed.keep(match_file(1));
        ASSERT_EQ(mixed.touch_and_rescan(), size_t{1});             // (the damaged file is found by the rescan)
        mixed.drain(8);
        ASSERT_TRUE(mixed.history.count() == 1 && mixed.history.knows(good));
        notes = mixed.history.take_notes();
        ASSERT_EQ(lines_with(notes, "1 match(es) counted, 0 left out"), size_t{1});
        ASSERT_EQ(lines_with(notes, "recording(s) of other simulation rules cannot be counted"), size_t{1});
    } TEST_END();

    TEST_CASE("HS5.4 A record that the database refuses is left out for this run with a line in the log; a database that is down stops the feeder (nothing is played, nothing is left out) and the work goes on when it answers again") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        const std::string b = rig.keep(match_file(2));
        const std::string c = rig.keep(match_file(3));
        db::Database other;
        ASSERT_TRUE(other.open((rig.temp.path / "ants.db").string()).ok());
        // the database refuses a record: a trigger that raises an error for the map of match b only... the CHECK of the table is the refusal: the newest (c) is played first
        ASSERT_TRUE(other.exec("CREATE TRIGGER no_c BEFORE INSERT ON history_matches WHEN NEW.id = '" + c + "' BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").ok());
        rig.drain(8);
        ASSERT_TRUE(rig.history.count() == 2 && rig.history.knows(a) && rig.history.knows(b) && !rig.history.knows(c) && rig.feeder.left_out() == 1 && rig.history.available());
        ASSERT_TRUE(lines_with(rig.history.take_notes(), "refused by the test") >= 1);
        // the database stops answering: no file is played while it is down
        Rig down;
        ASSERT_TRUE(down.open());
        const std::string d1 = down.keep(match_file(1));
        const std::string d2 = down.keep(match_file(2));
        db::Database away;
        ASSERT_TRUE(away.open((down.temp.path / "ants.db").string()).ok());
        ASSERT_TRUE(away.exec("ALTER TABLE history_matches RENAME TO history_matches_away").ok());
        down.drain(4);                                                                 // (the first look finds the database down; the feeder waits)
        ASSERT_TRUE(!down.history.available() && down.history.count() == 0 && down.feeder.counted() == 0);
        const int asked = down.idle_asked;
        down.drain(20);
        ASSERT_TRUE(down.feeder.counted() == 0 && down.feeder.left_out() == 0 && down.idle_asked == asked + 0);
        ASSERT_TRUE(away.exec("ALTER TABLE history_matches_away RENAME TO history_matches").ok());
        for (int minute = 0; minute < 4; ++minute) {                                    // (the store looks again every minute of the server's clock)
            down.ms += HistoryStore::kRetryMs;
            down.drain(2);
        }
        down.drain(6);
        ASSERT_TRUE(down.history.available() && down.history.count() == 2 && down.history.knows(d1) && down.history.knows(d2));
    } TEST_END();

    TEST_CASE("HS5.5 What the owner took out is not counted again while its recording is there, and the marker is deleted an hour after the recording is gone (the feeder looks once an hour)") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        const std::string b = rig.keep(match_file(2));
        rig.drain(6);
        ASSERT_EQ(rig.history.count(), size_t{2});
        std::string why;
        ASSERT_TRUE(rig.history.remove(a, true, why) == HistoryStore::Removal::Done);
        rig.drain(6);
        ASSERT_TRUE(rig.history.count() == 1 && rig.history.knows(b) && rig.history.marker_count() == 1 && rig.feeder.counted() == 2);       // (not counted again)
        ASSERT_TRUE(rig.replays.remove(history_recording_file(a)));                    // the recording goes (its 30 days): the marker is not needed
        rig.drain(6);
        ASSERT_EQ(rig.history.marker_count(), size_t{1});                              // (the look at the markers is once an hour: not yet)
        rig.ms += HistoryFeeder::kPruneEveryMs;
        rig.drain(2);
        ASSERT_TRUE(rig.history.marker_count() == 0 && rig.history.count() == 1);
    } TEST_END();

    TEST_CASE("HS5.6 The pace is counted from the END of a pass and follows what the pass took: at least 250 ms, and four times the time the recording took to count; a slow recording leaves the thread free for longer") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        for (const uint64_t work_us : {uint64_t{10}, uint64_t{400000}, uint64_t{100000}}) {
            Rig rig;
            rig.work_us = work_us;
            ASSERT_TRUE(rig.open());
            rig.keep(match_file(1));
            rig.keep(match_file(2));
            rig.keep(match_file(3));
            const auto idle = [] { return true; };
            uint32_t t = 5000;
            ASSERT_TRUE(rig.feeder.step(idle, t));                                   // the first pass: a recording is counted, the clock of the test says that it took `work_us`
            const uint64_t spent_ms = (work_us + 999) / 1000;
            const uint64_t wait = spent_ms + std::max<uint64_t>(HistoryFeeder::kPaceMs, spent_ms * HistoryFeeder::kPaceFactor);       // 251 ms, 2000 ms, 500 ms
            ASSERT_TRUE(rig.history.count() == 1);
            ASSERT_FALSE(rig.feeder.step(idle, t + 1));
            ASSERT_FALSE(rig.feeder.step(idle, t + static_cast<uint32_t>(wait) - 1));        // (a millisecond short: nothing is played)
            ASSERT_EQ(rig.history.count(), size_t{1});
            ASSERT_TRUE(rig.feeder.step(idle, t + static_cast<uint32_t>(wait)));             // (and on the dot it is)
            ASSERT_EQ(rig.history.count(), size_t{2});
            if (work_us == 400000) ASSERT_TRUE(wait == 2000);
            if (work_us == 10) ASSERT_TRUE(wait == 251);
        }
        // the old rule (the gap counted from the start of the pass, a fixed 250 ms) would have played the second recording 250 ms after the first whatever the first took: the slow case above says no to that
    } TEST_END();

    // ---- the routes -------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS6.1 The server counts its matches on its own loop, and GET /history lists them on both doors alike: newest first, the lean form, the recording's state and file, the counts") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Server s;
        ASSERT_TRUE(s.open());
        const std::string id0 = s.keep(match_file(0));
        const std::string id1 = s.keep(match_file(1));
        ASSERT_EQ(parse_text(s.control("GET", "/history").body).get("count").as_int_or(-1), int64_t{0});
        s.run_updates(30);
        ASSERT_TRUE(s.mgr.history_store() != nullptr && s.mgr.history_store()->count() == 2);
        const ctl::HttpResponse owner = s.control("GET", "/history");
        const ctl::HttpResponse door = s.door("GET", "/history");
        ASSERT_TRUE(owner.status == 200 && door.status == 200 && owner.body == door.body);
        const ctl::JsonValue list = parse_text(owner.body);
        ASSERT_TRUE(list.get("count").as_int_or(0) == 2 && list.get("total").as_int_or(0) == 2 && list.get("offset").as_int_or(-1) == 0 && list.get("limit").as_int_or(0) == 50);
        ASSERT_TRUE(list.get("keep_days").as_int_or(0) == 30 && list.get("sim_rules").as_int_or(0) == replay::kSimRules && list.get("history").size() == 2);
        const ctl::JsonValue first = list.get("history").at(0);
        ASSERT_TRUE(first.get("id").str() == id1 && first.get("recording").str() == "watch" && first.get("file").str() == history_recording_file(id1) && first.get("mode").str() == "highest-score");
        ASSERT_TRUE(first.get("seats").size() == 2 && first.get("seats").at(0).get("bombs_planted").is_null() && first.get("seats").at(0).get("colour").str() == "Green");        // (the lean form)
        ASSERT_TRUE(first.get("rows").size() == 2 && first.get("format").str() == "1v1" && first.get("result").str() == "cut" && first.get("v").is_null());      // (1000 turns: the rules did not end it)
        ASSERT_EQ(list.get("history").at(1).get("id").str(), id0);
        // filters through the query string
        ASSERT_EQ(parse_text(s.control("GET", "/history", "format=ffa").body).get("history").size(), size_t{1});
        ASSERT_EQ(parse_text(s.door("GET", "/history", "q=ann&sort=old&limit=1").body).get("history").at(0).get("id").str(), id0);
        ASSERT_EQ(parse_text(s.door("GET", "/history", "mode=187").body).get("count").as_int_or(-1), int64_t{0});
        ASSERT_EQ(parse_text(s.door("GET", "/history", "mode=highest-score").body).get("count").as_int_or(-1), int64_t{2});
        ASSERT_EQ(parse_text(s.door("GET", "/history", "colour=blue").body).get("count").as_int_or(-1), int64_t{1});
    } TEST_END();

    TEST_CASE("HS6.2 A query that is not understood is a 400 that names the key (a word that is not one, a key twice, a number out of range, an escape that is not one, a character no name has, a text too long, a key that does not exist); never a guess") {
        HistoryQuery q;
        std::string why;
        ASSERT_TRUE(parse_history_query("", q, why) && q.limit == 50 && q.offset == 0 && q.mode == HistoryQuery::Mode::All);
        ASSERT_TRUE(parse_history_query("limit=100&offset=1000000&sort=short&format=team&result=cut&who=computers&rec=old&colour=black&mode=187&q=a%20b%2Bc+d", q, why));
        ASSERT_TRUE(q.limit == 100 && q.offset == 1000000 && q.sort == HistoryQuery::Sort::Short && q.format == HistoryQuery::Format::Teams && q.result == HistoryQuery::Result::Cut);
        ASSERT_TRUE(q.who == HistoryQuery::Who::Computers && q.rec == HistoryQuery::Rec::Old && q.colour == 3 && q.mode == HistoryQuery::Mode::Kills187 && q.text == "a b+c d");
        ASSERT_TRUE(parse_history_query("mode=highest-score", q, why) && q.mode == HistoryQuery::Mode::HighestScore);
        const std::vector<std::pair<std::string, std::string>> bad = {
            {"limit=0", "limit"}, {"limit=101", "limit"}, {"limit=-1", "limit"}, {"limit=abc", "limit"}, {"limit=", "limit"}, {"offset=1000001", "offset"}, {"offset=1.5", "offset"},
            {"sort=best", "sort"}, {"format=3v3", "format"}, {"result=won", "result"}, {"who=robots", "who"}, {"rec=lost", "rec"}, {"colour=purple", "colour"}, {"colour=BLACK", "colour"}, {"colour=any", "colour"}, {"mode=chess", "mode"}, {"mode=1", "mode"},
            {"mode=187&mode=187", "mode"}, {"limit=5&limit=6", "limit"}, {"q=%zz", "q"}, {"q=%4", "q"}, {"q=%00", "q"}, {"q=" + std::string(41, 'a'), "q"}, {"q=%7F", "q"},
            {"colour", "colour"}, {"nonsense=1", "nonsense"}, {"Limit=5", "Limit"}, {"sort=NEW", "sort"}};
        for (const auto& b : bad) {
            ASSERT_FALSE(parse_history_query(b.first, q, why));
            ASSERT_TRUE(contains(why, b.second));
        }
        ASSERT_TRUE(parse_history_query("&&limit=3&", q, why) && q.limit == 3);        // (empty pieces are skipped)
        Server s;
        ASSERT_TRUE(s.open());
        for (const char* wrong : {"mode=chess", "limit=999", "q=%zz"}) {
            ASSERT_EQ(s.control("GET", "/history", wrong).status, 400);
            ASSERT_EQ(s.door("GET", "/history", wrong).status, 400);
        }
    } TEST_END();

    TEST_CASE("HS6.3 GET /history/<id> gives the whole record (the bombs and fires, the layout number, the mode, what a later version keeps in \"details\") and the state of the recording; an id that is not one, a match that is not there and a query are 404; both doors give the same") {
        Server s;
        ASSERT_TRUE(s.open());
        const std::vector<HistoryRecord> all = eight_records();
        std::string why;
        HistoryRecord rec = all[6];
        rec.details = "{\"events\":[{\"t\":3,\"what\":\"bomb\"}]}";
        ASSERT_TRUE(s.mgr.history_store()->put(rec, why) && s.mgr.history_store()->put(all[0], why));
        const ctl::HttpResponse owner = s.control("GET", "/history/" + rec.id);
        const ctl::HttpResponse door = s.door("GET", "/history/" + rec.id);
        ASSERT_TRUE(owner.status == 200 && door.status == 200 && owner.body == door.body);
        const ctl::JsonValue j = parse_text(owner.body);
        ASSERT_TRUE(j.get("v").as_int_or(0) == kHistoryVersion && j.get("id").str() == rec.id && j.get("mode").str() == "187" && j.get("recording").str() == "removed" && j.get("file").is_null());
        ASSERT_TRUE(j.get("seats").at(0).get("bombs_planted").as_int_or(0) == 1 && j.get("seats").at(1).get("fires_lit").as_int_or(0) == 4 && j.get("keep_days").as_int_or(0) == 30);
        ASSERT_TRUE(j.get("details").get("events").size() == 1 && j.get("details").get("events").at(0).get("what").str() == "bomb");
        ASSERT_TRUE(parse_text(s.door("GET", "/history/" + all[0].id).body).get("details").is_null());                 // (a record with none has none)
        ASSERT_EQ(s.control("GET", "/history/" + id_at(kT0 + 99999)).status, 404);
        ASSERT_EQ(s.door("GET", "/history/" + id_at(kT0 + 99999)).status, 404);
        for (const char* junk : {"/history/", "/history/nonsense", "/history/..%2f..", "/history/ants-TINY-20261008-143209Z.antsrep", "/history/ants-TINY-20261008-143209Z/x"}) {
            ASSERT_EQ(s.control("GET", junk).status, 404);
            ASSERT_EQ(s.door("GET", junk).status, 404);
        }
        ASSERT_EQ(s.control("GET", "/history/" + rec.id, "x=1").status, 404);
        ASSERT_EQ(s.door("GET", "/history/" + rec.id, "x=1").status, 404);
        ASSERT_EQ(s.door("POST", "/history").status, 404);                                       // (the handler alone; on the wire the public door answers 405 with Allow: GET before it is asked: test_ctl CTL2.22)
        ASSERT_EQ(s.control("POST", "/history").status, 405);
        ASSERT_EQ(s.control("PUT", "/history/" + rec.id).status, 405);
        ASSERT_EQ(s.door("PUT", "/history/" + rec.id).status, 404);
    } TEST_END();

    TEST_CASE("HS6.4 Only the owner takes a match out (DELETE /history/<id> on the control door): with the recording still there a restart does not count it again, without it nothing stays; the public door and every other method refuse") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        Server s;
        ASSERT_TRUE(s.open());
        const std::string a = s.keep(match_file(1));
        const std::string b = s.keep(match_file(2));
        s.run_updates(30);
        HistoryStore& history = *s.mgr.history_store();
        ASSERT_EQ(history.count(), size_t{2});
        ASSERT_EQ(s.door("DELETE", "/history/" + a).status, 404);                                 // (the public door cannot delete)
        ASSERT_TRUE(history.knows(a) && history.count() == 2);
        ASSERT_EQ(s.control("DELETE", "/history/" + id_at(kT0 + 99999)).status, 404);
        ASSERT_EQ(s.control("DELETE", "/history/nonsense").status, 404);
        ASSERT_EQ(s.control("DELETE", "/history/" + a, "x=1").status, 404);
        ASSERT_EQ(s.control("DELETE", "/history").status, 405);
        const ctl::HttpResponse deleted = s.control("DELETE", "/history/" + a);
        ASSERT_TRUE(deleted.status == 200 && parse_text(deleted.body).get("deleted").str() == a);
        ASSERT_TRUE(history.count() == 1 && history.marker_count() == 1 && s.mgr.replay_store()->count() == 2);
        s.run_updates(30);                                                                        // the recording is still there: it is not counted again
        ASSERT_TRUE(history.count() == 1 && history.knows(a));
        ASSERT_EQ(s.control("GET", "/history/" + a).status, 404);
        ASSERT_EQ(parse_text(s.door("GET", "/history").body).get("count").as_int_or(-1), int64_t{1});
        // the recording is gone first, then the record: no marker stays
        ASSERT_TRUE(s.mgr.replay_store()->remove(history_recording_file(b)));
        ASSERT_EQ(s.control("DELETE", "/history/" + b).status, 200);
        ASSERT_TRUE(history.count() == 0 && history.marker_count() == 1);
        ASSERT_EQ(s.control("DELETE", "/history/" + b).status, 404);
        ASSERT_EQ(parse_text(s.door("GET", "/history", "rec=removed").body).get("count").as_int_or(-1), int64_t{0});       // (nothing is left to say "removed")
    } TEST_END();

    TEST_CASE("HS6.5 A server with no history answers 404 on both doors; a history with no replay store lists what the database holds (every recording removed) and counts nothing; a history on a database without its tables is refused with the reason") {
        Server none;
        ASSERT_TRUE(none.open(false));
        ASSERT_TRUE(none.mgr.history_store() == nullptr);
        ASSERT_EQ(none.control("GET", "/history").status, 404);
        ASSERT_EQ(none.door("GET", "/history").status, 404);
        ASSERT_EQ(none.control("GET", "/history/" + id_at(kT0)).status, 404);
        ASSERT_EQ(none.control("DELETE", "/history/" + id_at(kT0)).status, 404);
        Server bare;                                                                              // no replay store: the history is read, nothing is added
        std::string why;
        ASSERT_TRUE(bare.db_ok && bare.mgr.enable_history(bare.database, why));
        ASSERT_TRUE(bare.mgr.replay_store() == nullptr && bare.mgr.history_store() != nullptr);
        ASSERT_TRUE(bare.mgr.history_store()->put(eight_records()[0], why));
        const ctl::JsonValue list = parse_text(bare.door("GET", "/history").body);
        ASSERT_TRUE(list.get("count").as_int_or(0) == 1 && list.get("keep_days").as_int_or(-1) == 0 && list.get("history").at(0).get("recording").str() == "removed");
        bare.run_updates(10);
        ASSERT_EQ(bare.mgr.history_store()->count(), size_t{1});
        db::Database plain;                                                                       // a database that has no history tables
        ASSERT_TRUE(plain.open(":memory:").ok());
        RoomManager mgr{MapStore(maps_dir())};
        ASSERT_FALSE(mgr.enable_history(plain, why));
        ASSERT_TRUE(contains(why, "history") && mgr.history_store() == nullptr);
    } TEST_END();

    TEST_CASE("HS6.6 A database that stops answering: both doors answer 503 with a clear body (the owner is told why, the public door only that it is so), the loop does not throw, and the routes work again when it does") {
        Server s;
        ASSERT_TRUE(s.open());
        std::string why;
        const std::vector<HistoryRecord> all = eight_records();
        for (const HistoryRecord& r : all) ASSERT_TRUE(s.mgr.history_store()->put(r, why));
        db::Database other;
        ASSERT_TRUE(other.open((s.temp.path / "ants.db").string()).ok());
        ASSERT_TRUE(other.exec("ALTER TABLE history_matches RENAME TO history_matches_away").ok());
        const ctl::HttpResponse owner = s.control("GET", "/history");
        ASSERT_EQ(owner.status, 503);
        ASSERT_TRUE(contains(parse_text(owner.body).get("error").str(), "unavailable") && contains(parse_text(owner.body).get("error").str(), "history_matches"));
        const ctl::HttpResponse door = s.door("GET", "/history");
        ASSERT_EQ(door.status, 503);
        ASSERT_EQ(parse_text(door.body).get("error").str(), std::string("the match history is unavailable"));
        ASSERT_FALSE(contains(door.body, "history_matches"));                                    // (nothing of the database on the public door)
        ASSERT_EQ(s.control("GET", "/history/" + all[0].id).status, 503);
        ASSERT_EQ(s.door("GET", "/history/" + all[0].id).status, 503);
        ASSERT_EQ(s.control("DELETE", "/history/" + all[0].id).status, 503);
        ASSERT_EQ(s.door("GET", "/history/nonsense").status, 404);                               // (an id that cannot exist is 404 whether the database answers or not)
        ASSERT_EQ(s.control("GET", "/stats").status, 200);                                       // (the rest of the interface is as it was)
        s.run_updates(10);                                                                       // (the loop goes on)
        ASSERT_TRUE(other.exec("ALTER TABLE history_matches_away RENAME TO history_matches").ok());
        for (int minute = 0; minute < 3; ++minute) {
            s.ms += HistoryStore::kRetryMs;
            s.run_updates(2);
        }
        ASSERT_EQ(s.control("GET", "/history").status, 200);
        ASSERT_EQ(parse_text(s.door("GET", "/history").body).get("count").as_int_or(-1), static_cast<int64_t>(all.size()));
        const std::vector<std::string> notes = s.mgr.take_notices();
        ASSERT_TRUE(lines_with(notes, "unavailable") == 1 && lines_with(notes, "available again") == 1);
    } TEST_END();

    TEST_CASE("HS6.7 A list that is too heavy, or asked while the budget is used up, is a 503 with a short body on both doors (the owner has the larger box and no budget); the other routes and the next calm request are as they were") {
        Server s;
        TickClock clock;
        HistoryStore::Config config;
        config.clock_us = [&clock]() { return clock(); };
        ASSERT_TRUE(s.open(true, config));
        ASSERT_TRUE(fill_synthetic(s.database, 3000));
        std::string why;
        ASSERT_TRUE(s.mgr.history_store()->prepare(why));                          // (the count of the rows made outside the store)
        clock.step_us = 1000;
        const ctl::HttpResponse door = s.door("GET", "/history", "q=zzzz");
        ASSERT_EQ(door.status, 503);
        ASSERT_EQ(parse_text(door.body).get("error").str(), std::string("that search is too heavy for the server: narrow it (a filter, a shorter text, a smaller offset)"));
        const ctl::HttpResponse owner = s.control("GET", "/history", "q=zzzz");
        ASSERT_EQ(owner.status, 503);                                              // (a millisecond a look is too much even for the owner's 100 ms)
        ASSERT_TRUE(contains(parse_text(owner.body).get("error").str(), "too heavy"));
        clock.step_us = 100;
        ASSERT_EQ(s.door("GET", "/history", "q=zzzz").status, 503);
        const ctl::HttpResponse ok = s.control("GET", "/history", "q=zzzz");
        ASSERT_TRUE(ok.status == 200 && parse_text(ok.body).get("count").as_int_or(-1) == 0);
        ASSERT_TRUE(s.mgr.history_store()->available());
        // the budget: the heavy searches of the door use it up; then "busy", at once, and the owner's door is not affected
        clock.step_us = 1000;
        int refused = 0;
        std::string last;
        for (int k = 0; k < 40 && refused == 0; ++k) {
            const ctl::HttpResponse r = s.door("GET", "/history", "q=zzzz");
            last = parse_text(r.body).get("error").str();
            if (contains(last, "busy")) refused = k + 1;
        }
        ASSERT_TRUE(refused > 0 && refused < 12);
        ASSERT_EQ(last, std::string("the match history is busy: try again in a moment"));
        ASSERT_EQ(s.door("GET", "/history").status, 503);                          // (any list: the budget is the door's, not the query's)
        clock.step_us = 0;
        ASSERT_EQ(s.control("GET", "/history").status, 200);
        ASSERT_EQ(s.control("GET", "/stats").status, 200);
        ASSERT_EQ(s.door("GET", "/history/nonsense").status, 404);                 // (a match by its id is no list: no budget)
        clock.base += 5000000;                                                     // five seconds later the door has its budget again
        const ctl::HttpResponse again = s.door("GET", "/history");
        ASSERT_TRUE(again.status == 200 && parse_text(again.body).get("count").as_int_or(-1) == 3000);
        ASSERT_TRUE(lines_with(s.mgr.take_notices(), "unavailable") == 0);          // (nothing of this was a failure of the database)
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
