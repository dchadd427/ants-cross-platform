// The match history in front of real rooms (docs/REPLAYS.md "The match history"): rooms of computer players play whole matches on the shipped TINY map, in both game modes ("highest-score" and "187"), on a
// room manager with a replay store and a database; the history counts each match from its recording when no room runs a match, the record is the match (mode, names, format, rows, the rule that picks the
// winner), both doors list it. The store's own rules are test_history_store.cpp, the rule of the winner test_history_record.cpp.
#include "ants_ai/bot_names.hpp"
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "ants_server/history_deaths.hpp"
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

// A room of computer players alone (the specification of the server's own matches): three seats, 45 s of play at the normal speed, then the rules or the clock end it
RoomSpec bots_room(const std::string& code, uint8_t mode) {
    RoomSpec spec;
    spec.code = code;
    spec.map = "TINY.LVL";
    spec.players = 3;
    spec.has_seed = true;
    spec.seed = 4242;
    spec.mode = mode;
    for (uint8_t seat = 0; seat < 3; ++seat) {
        ai::BotSpec bot;
        bot.seat = seat;
        bot.level = seat == 1 ? ai::Level::Hard : ai::Level::Medium;
        spec.bots.push_back(bot);
    }
    spec.bots_only = true;
    spec.early_start = false;
    spec.reconnect = false;
    spec.wait_ms = 5u * 1000u;
    spec.run_ms = 45u * 1000u;
    return spec;
}

struct Rooms {
    TempDir temp;
    int64_t clock{kT0};
    db::Database database;
    bool db_ok;
    RoomManager mgr;
    uint32_t now{1000};

    Rooms() : db_ok(open_server_db(database, (temp.path / "ants.db").string())), mgr(MapStore(maps_dir())) {}
    ~Rooms() { mgr.disable_history(); }

    bool open() {
        ReplayConfig rc;
        rc.dir = (temp.path / "replays").string();
        rc.game_version = "v0.12.0-test";
        rc.build_id = "test";
        rc.clock_s = [this]() { return clock; };
        rc.min_free_bytes = 0;
        std::string why;
        return db_ok && mgr.enable_replays(rc, false, why) && mgr.enable_history(database, why);
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            mgr.update(now);
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            run(10);
        }
        return cond();
    }
    RoomStatus status(const std::string& code) const {
        RoomStatus s;
        mgr.status(code, s, now);
        return s;
    }
    ctl::HttpResponse call(const char* method, const std::string& path, const std::string& query = std::string(), bool public_door = false) {
        ctl::HttpRequest rq;
        rq.method = method;
        rq.path = path;
        rq.query = query;
        return public_door ? handle_public_replays(mgr, rq) : handle_control(mgr, rq, now);
    }
};

std::string stem_of(const std::string& file) { return file.substr(0, file.size() - std::string(ReplayStore::kExtension).size()); }

// The rule of the owner, written out a second time from the stored rows (not by calling the code under test): the first row wins unless it is tied with the second
void check_rule(const HistoryRecord& rec, bool& ok) {
    ok = !rec.rows.empty() && !rec.seats.empty();
    if (!ok) return;
    const bool kills_only = rec.mode == "187";
    for (size_t k = 1; k < rec.rows.size(); ++k) {
        const HistoryRow& a = rec.rows[k - 1];
        const HistoryRow& b = rec.rows[k];
        if (kills_only) {
            if (!(k == 1 && a.killed < b.killed && rec.finished)) ok = ok && a.killed >= b.killed;         // (187: the side that is left stands first, whatever the kills)
        } else {
            ok = ok && (a.score > b.score || (a.score == b.score && a.killed >= b.killed));
        }
    }
    if (rec.rows.size() >= 2 && rec.quitter < 0) {
        const HistoryRow& a = rec.rows[0];
        const HistoryRow& b = rec.rows[1];
        const bool tied = kills_only ? a.killed == b.killed : (a.score == b.score && a.killed == b.killed);
        if (!kills_only) ok = ok && rec.draw == tied;
        if (kills_only && rec.draw) ok = ok && tied;
    }
    ok = ok && rec.rows[0].winner == !rec.draw;
    for (size_t k = 1; k < rec.rows.size(); ++k) ok = ok && !rec.rows[k].winner;
    for (const HistorySeat& s : rec.seats) {
        bool in_winner_row = false;
        for (const uint8_t member : rec.rows[0].seats) in_winner_row = in_winner_row || member == s.seat;
        ok = ok && s.winner == (!rec.draw && in_winner_row);
    }
}

}  // namespace

int main() {
    std::cout << "=======================================================\n MATCH HISTORY IN FRONT OF REAL ROOMS (both game modes)\n=======================================================\n";

    for (const uint8_t mode : {uint8_t{0}, uint8_t{1}}) {
        const std::string mode_name = mode == 0 ? "highest-score" : "187";
        TEST_CASE("HR1." + std::to_string(mode + 1) + " Rooms of computer players, game mode " + mode_name + ": a match is counted only while no room runs one, the record is the match (mode, bots, format, rows, the winner rule), both doors list it, and a second match is counted after it") {
            Rooms r;
            ASSERT_TRUE(r.db_ok && r.open());
            ASSERT_TRUE(r.mgr.create_room(bots_room("HIS-A", mode), r.now).ok);
            ASSERT_TRUE(r.until([&] { return r.status("HIS-A").state == RoomState::Running; }, 12000));
            r.run(5000);
            ASSERT_TRUE(r.status("HIS-A").state == RoomState::Running && r.status("HIS-A").mode == mode);
            ASSERT_EQ(r.mgr.history_store()->count(), size_t{0});                                          // (nothing is played again while a match runs)
            ASSERT_TRUE(r.until([&] { return r.status("HIS-A").state != RoomState::Running; }, 60000));
            const RoomStatus a = r.status("HIS-A");
            ASSERT_TRUE(a.replay_kept && ReplayStore::valid_file_name(a.replay_file));
            const std::string id = stem_of(a.replay_file);
            // a second room begins at once: while it waits and plays, the first match is counted, in the quiet of its five seconds of waiting... and the second only when it is over
            r.clock += 3600;
            ASSERT_TRUE(r.mgr.create_room(bots_room("HIS-B", mode), r.now).ok);
            ASSERT_TRUE(r.until([&] { return r.mgr.history_store()->count() == 1; }, 4000));            // (the room waits: no match runs, the feeder is let in)
            ASSERT_TRUE(r.status("HIS-B").state != RoomState::Running);
            ASSERT_TRUE(r.until([&] { return r.status("HIS-B").state == RoomState::Running; }, 12000));
            r.run(8000);
            ASSERT_TRUE(r.status("HIS-B").state == RoomState::Running && r.mgr.history_store()->count() == 1);
            // the record of the first match is the match
            HistoryRecord rec;
            ASSERT_TRUE(r.mgr.history_store()->find(id, rec));
            ASSERT_TRUE(rec.mode == mode_name && rec.map == "TINY.LVL" && rec.game == "v0.12.0-test" && rec.ended_s == kT0 && rec.format == "ffa3" && rec.computers_only);
            ASSERT_TRUE(rec.seats.size() == 3 && rec.rows.size() == 3 && rec.turns >= 600);
            ASSERT_TRUE(rec.seats[0].bot == "Medium" && rec.seats[1].bot == "Hard" && rec.seats[2].bot == "Medium");
            for (const HistorySeat& seat : rec.seats) ASSERT_EQ(seat.name, ai::bot_seat_name(ai::bot_name_key("HIS-A"), seat.seat));      // (protocol 20: the bot's own name, "Pip" of "Pip (bot, Hard)"; the level is the bot field)
            bool rule_holds = false;
            check_rule(rec, rule_holds);
            ASSERT_TRUE(rule_holds);
            if (mode == 1) {
                for (const HistorySeat& s : rec.seats) ASSERT_EQ(s.score, static_cast<int32_t>(s.killed));      // (187: the kills are the ranking value)
            }
            // the same as a play of the stored recording
            const ReplayEntry* entry = r.mgr.replay_store()->find(a.replay_file);
            ASSERT_TRUE(entry != nullptr && entry->mode == mode);
            std::vector<uint8_t> bytes;
            ASSERT_TRUE(r.mgr.replay_store()->read(a.replay_file, bytes));
            const Played played = play_file(bytes);
            ASSERT_TRUE(played.ok && played.rep.head.mode == mode);
            ASSERT_EQ(rec.finished, played.outcome.match_over);
            HistoryRecord expected = make_history_record(id, kT0, played.rep, played.outcome);
            ASSERT_TRUE(!rec.details.empty() && expected.details.empty());           // (the details of the same play: test_history_details.cpp checks what is in them)
            expected.details = rec.details;
            ASSERT_TRUE(rec.deaths.size() <= kDeathsMaxBytes);                       // (the map of deaths of the same play: test_history_deaths.cpp checks what is in it)
            expected.deaths = rec.deaths;
            ASSERT_EQ(ctl::to_json(history_record_to_json(rec, true)), ctl::to_json(history_record_to_json(expected, true)));
            // both doors list it
            const ctl::HttpResponse owner = r.call("GET", "/history");
            const ctl::HttpResponse door = r.call("GET", "/history", "", true);
            ASSERT_TRUE(owner.status == 200 && door.status == 200 && owner.body == door.body);
            const ctl::JsonValue list = parse_text(owner.body);
            ASSERT_TRUE(list.get("count").as_int_or(0) == 1 && list.get("history").at(0).get("id").str() == id && list.get("history").at(0).get("mode").str() == mode_name);
            ASSERT_TRUE(list.get("history").at(0).get("recording").str() == "watch" && list.get("history").at(0).get("file").str() == a.replay_file);
            ASSERT_EQ(parse_text(r.call("GET", "/history", "mode=" + std::string(mode == 0 ? "187" : "highest-score"), true).body).get("count").as_int_or(-1), int64_t{0});
            ASSERT_EQ(parse_text(r.call("GET", "/history", "mode=" + mode_name, true).body).get("count").as_int_or(-1), int64_t{1});
            const ctl::JsonValue item = parse_text(r.call("GET", "/history/" + id, "", true).body);
            ASSERT_TRUE(item.get("id").str() == id && item.get("seats").size() == 3 && item.get("seats").at(1).get("bot").str() == "Hard" && item.get("seats").at(0).get("bombs_planted").is_int());
            ASSERT_FALSE(contains(door.body, "HIS-A"));                                                // (no room code is anywhere)
            // the second match is counted after it is over
            ASSERT_TRUE(r.until([&] { return r.status("HIS-B").state != RoomState::Running; }, 60000));
            ASSERT_TRUE(r.until([&] { return r.mgr.history_store()->count() == 2; }, 4000));
            const ctl::JsonValue both = parse_text(r.call("GET", "/history", "", true).body);
            ASSERT_TRUE(both.get("count").as_int_or(0) == 2 && both.get("history").at(0).get("id").str() == stem_of(r.status("HIS-B").replay_file) && both.get("history").at(1).get("id").str() == id);
            // the owner takes the first out: its recording stays, it is not counted again
            ASSERT_EQ(r.call("DELETE", "/history/" + id).status, 200);
            r.run(3000);
            ASSERT_TRUE(r.mgr.history_store()->count() == 1 && r.mgr.history_store()->marker_count() == 1 && r.mgr.replay_store()->count() == 2);
        } TEST_END();
    }

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
