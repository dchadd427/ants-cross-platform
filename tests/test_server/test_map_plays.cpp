// Tests of the play count of the maps (HistoryStore::put and map_plays, table map_plays of schema version 3, docs/SERVER.md "The database"): only matches with a person that ran 30 seconds count (29 s does not,
// 30 s does; a match of computer players only does not), a draw and a quit count as the history records them, one match is one play however often it is kept or the server restarts, the key is the map's file
// name and hash, the count survives a reopened database, and real recordings are counted by the feeder (and not again by a second feeder on the same database). A server with no database keeps no count.
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>

#include "../common/test_assert.hpp"
#include "big_map_fixtures.hpp"
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

// A record made by hand: `turns` long, with the given seats (a name for a person, "Bot" for a computer player), on `map` with `hash`
HistoryRecord play_of(int64_t t, const char* map, uint64_t hash, uint32_t turns, std::vector<std::string> who, bool draw = false, int quitter = -1) {
    HistoryRecord r;
    r.id = id_at(t, ReplayStore::map_stem(map));
    r.ended_s = t;
    r.map = map;
    r.map_hash = hash;
    r.game = "v0.12.0-test";
    r.turns = turns;
    r.finished = quitter < 0;
    r.quitter = quitter;
    r.mode = "highest-score";
    r.format = who.size() == 1 ? "solo" : "1v1";
    r.draw = draw;
    r.computers_only = true;
    for (size_t i = 0; i < who.size(); ++i) {
        HistorySeat s;
        s.seat = static_cast<uint8_t>(i);
        if (who[i] == "Bot") s.bot = "Hard";
        else s.name = who[i];
        r.computers_only = r.computers_only && !s.bot.empty();
        r.seats.push_back(s);
        HistoryRow row;
        row.seats.push_back(s.seat);
        row.winner = i == 0 && !draw;
        r.rows.push_back(row);
    }
    return r;
}

struct Db {
    TempDir temp;
    db::Database database;
    bool ok{false};
    HistoryStore store;
    Db() : ok(open_server_db(database, (temp.path / "ants.db").string())), store(database) {
        std::string why;
        ok = ok && store.prepare(why);
    }
    std::string path() const { return (temp.path / "ants.db").string(); }
    bool put(const HistoryRecord& r) {
        std::string why;
        return store.put(r, why);
    }
    // the plays of one map (0 when it has no row)
    int64_t plays(const char* map, uint64_t hash) {
        std::vector<MapPlays> all;
        if (!store.map_plays(all)) return -1;
        for (const MapPlays& m : all) {
            if (m.map == map && m.hash == map_hash_text(hash)) return m.plays;
        }
        return 0;
    }
};

constexpr uint64_t kHashA = 0x8683794a2c8cc99bULL;
constexpr uint64_t kHashB = 0x0000000000000abcULL;

}  // namespace

int main() {
    std::cout << "=======================================================\n MAP PLAY COUNTS (how many times each map was played)\n=======================================================\n";

    TEST_CASE("MP1.1 The hash is written as maps.json writes it: 16 lower-case hex digits, zero padded") {
        ASSERT_EQ(map_hash_text(0x8683794a2c8cc99bULL), std::string("8683794a2c8cc99b"));
        ASSERT_EQ(map_hash_text(0xabcULL), std::string("0000000000000abc"));
        ASSERT_EQ(map_hash_text(0), std::string("0000000000000000"));
        ASSERT_EQ(map_hash_text(~uint64_t{0}), std::string("ffffffffffffffff"));
    } TEST_END();

    TEST_CASE("MP1.2 Only a match with a person that ran 30 seconds is a play: 29 s (580 turns) and 599 turns are not, 600 turns are; a match of computer players only is not, whatever its length") {
        Db d;
        ASSERT_TRUE(d.ok);
        std::vector<MapPlays> none;
        ASSERT_TRUE(d.store.map_plays(none) && none.empty());
        ASSERT_TRUE(d.put(play_of(kT0, "TINY.LVL", kHashA, 580, {"Ann", "Bob"})));                    // 29 s
        ASSERT_TRUE(d.put(play_of(kT0 + 10, "TINY.LVL", kHashA, 599, {"Ann", "Bob"})));
        ASSERT_TRUE(d.put(play_of(kT0 + 20, "TINY.LVL", kHashA, 0, {"Ann"})));
        ASSERT_TRUE(d.put(play_of(kT0 + 30, "TINY.LVL", kHashA, 36000, {"Bot", "Bot"})));            // computers only: 30 minutes, no play
        ASSERT_TRUE(d.put(play_of(kT0 + 40, "TINY.LVL", kHashA, 6000, {"Bot"})));
        ASSERT_EQ(d.store.count(), size_t{5});                                                       // (all five are in the history)
        ASSERT_TRUE(d.store.map_plays(none) && none.empty());
        ASSERT_TRUE(d.put(play_of(kT0 + 50, "TINY.LVL", kHashA, 600, {"Ann", "Bob"})));              // 30 s
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{1});
        ASSERT_TRUE(d.put(play_of(kT0 + 60, "TINY.LVL", kHashA, 1200, {"Bot", "Cy"})));              // a person against a computer player
        ASSERT_TRUE(d.put(play_of(kT0 + 70, "TINY.LVL", kHashA, 1200, {"Bot", "Bot", "Dee"})));
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{3});
    } TEST_END();

    TEST_CASE("MP1.3 A draw and a quit count like the history keeps them (a record of a match that ended; a person took part and it ran 30 s); a quit before 30 s does not") {
        Db d;
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(d.put(play_of(kT0, "TREASURE.LVL", kHashB, 3000, {"Ann", "Bob"}, true)));
        ASSERT_TRUE(d.put(play_of(kT0 + 10, "TREASURE.LVL", kHashB, 800, {"Ann", "Bob"}, false, 1)));
        ASSERT_TRUE(d.put(play_of(kT0 + 20, "TREASURE.LVL", kHashB, 500, {"Ann", "Bob"}, false, 1)));
        ASSERT_EQ(d.plays("TREASURE.LVL", kHashB), int64_t{2});
        HistoryRecord cut = play_of(kT0 + 30, "TREASURE.LVL", kHashB, 36000, {"Ann", "Bob"});          // the room's time ran out
        cut.finished = false;
        ASSERT_TRUE(d.put(cut));
        ASSERT_EQ(d.plays("TREASURE.LVL", kHashB), int64_t{3});
    } TEST_END();

    TEST_CASE("MP1.4 One match is one play: a record kept again (same id) or put after the owner took the match out is not counted again; a match that was never kept is not counted") {
        Db d;
        ASSERT_TRUE(d.ok);
        const HistoryRecord r = play_of(kT0, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"});
        ASSERT_TRUE(d.put(r) && d.put(r) && d.put(r));
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{1});
        ASSERT_EQ(d.store.count(), size_t{1});
        std::string why;
        ASSERT_TRUE(d.store.remove(r.id, true, why) == HistoryStore::Removal::Done);                 // the owner takes it out, the recording is still there
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{1});                                          // (it was played: the count stays)
        ASSERT_TRUE(d.put(r));
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{1});
        ASSERT_TRUE(d.put(play_of(kT0 + 1, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"})));
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{2});
        HistoryRecord bad = play_of(kT0 + 2, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"});               // a record the database refuses (a mode that is none): nothing is counted
        bad.mode = "chess";
        ASSERT_FALSE(d.put(bad));
        ASSERT_TRUE(d.store.available());
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{2});
    } TEST_END();

    TEST_CASE("MP1.5 The key is the file name and the hash: another file under the same name is another map; the rows are listed by name and hash with the end time of the newest play") {
        Db d;
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(d.put(play_of(kT0 + 500, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"})));
        ASSERT_TRUE(d.put(play_of(kT0 + 100, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"})));            // (an older match counted later, as the backfill does: last_played stays the newest)
        ASSERT_TRUE(d.put(play_of(kT0 + 200, "TINY.LVL", kHashB, 1000, {"Ann", "Bob"})));
        ASSERT_TRUE(d.put(play_of(kT0 + 300, "!!!! A map.lvl", kHashB, 1000, {"Ann", "Bob"})));
        std::vector<MapPlays> all;
        ASSERT_TRUE(d.store.map_plays(all));
        ASSERT_EQ(all.size(), size_t{3});
        ASSERT_TRUE(all[0].map == "!!!! A map.lvl" && all[0].hash == "0000000000000abc" && all[0].plays == 1 && all[0].last_played == kT0 + 300);
        ASSERT_TRUE(all[1].map == "TINY.LVL" && all[1].hash == "0000000000000abc" && all[1].plays == 1 && all[1].last_played == kT0 + 200);
        ASSERT_TRUE(all[2].map == "TINY.LVL" && all[2].hash == "8683794a2c8cc99b" && all[2].plays == 2 && all[2].last_played == kT0 + 500);
    } TEST_END();

    TEST_CASE("MP1.6 The counts are in the file: a reopened database has them and goes on from them; a database that stops answering gives no counts and keeps nothing, and the store comes back") {
        Db d;
        ASSERT_TRUE(d.ok);
        ASSERT_TRUE(d.put(play_of(kT0, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"})));
        ASSERT_TRUE(d.put(play_of(kT0 + 10, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"})));
        db::Database again;                                                                           // (a new connection to the same file: a restarted server)
        ASSERT_TRUE(again.open(d.path()).ok());
        HistoryStore other(again);
        std::string why;
        ASSERT_TRUE(other.prepare(why));
        std::vector<MapPlays> all;
        ASSERT_TRUE(other.map_plays(all) && all.size() == 1 && all[0].plays == 2);
        ASSERT_TRUE(other.put(play_of(kT0 + 20, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"}), why));
        ASSERT_TRUE(other.map_plays(all) && all[0].plays == 3);
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{3});
        // a table that goes away: the play is not counted AND the match is not kept (one transaction), the store is down, and the play is counted once when it is back
        (void)d.store.take_notes();
        ASSERT_TRUE(again.exec("ALTER TABLE map_plays RENAME TO map_plays_away").ok());
        const HistoryRecord later = play_of(kT0 + 30, "TINY.LVL", kHashA, 1000, {"Ann", "Bob"});
        ASSERT_FALSE(d.put(later));
        ASSERT_FALSE(d.store.available());
        ASSERT_FALSE(d.store.map_plays(all));
        ASSERT_TRUE(all.empty());
        ASSERT_TRUE(again.exec("ALTER TABLE map_plays_away RENAME TO map_plays").ok());
        uint32_t ms = 1000;
        d.store.maintain(ms);
        d.store.maintain(ms + HistoryStore::kRetryMs);
        ASSERT_TRUE(d.store.available());
        ASSERT_EQ(d.store.count(), size_t{3});                                                       // (the failed match was not kept: the feeder would try it again)
        ASSERT_TRUE(d.put(later));
        ASSERT_EQ(d.plays("TINY.LVL", kHashA), int64_t{4});
    } TEST_END();

    TEST_CASE("MP2.1 Real recordings: the feeder counts a match of people (free for all with computer players, two people, one that quit) under the map's file name and the hash of the map file; a match of computer players only is in the history and is no play") {
        if (match_file(0).empty()) {
            std::cout << "SKIPPED (no map)\n";
            return;
        }
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "/TINY.LVL", hash));
        MatchSpec bots;
        bots.seed = 21;
        bots.turns = 1000;
        bots.roster = 0x03;
        bots.names = {"Bot (Hard)", "Bot (Hard)", "", ""};
        const std::vector<uint8_t> bot_file = record_match(bots);
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string id0 = rig.keep(match_file(0));
        const std::string id1 = rig.keep(match_file(1));
        const std::string id2 = rig.keep(match_file(2));
        const std::string idb = rig.keep(bot_file);
        ASSERT_TRUE(!id0.empty() && !id1.empty() && !id2.empty() && !idb.empty());
        rig.drain(12);
        ASSERT_EQ(rig.history.count(), size_t{4});
        HistoryRecord b;
        ASSERT_TRUE(rig.history.find(idb, b) && b.computers_only && b.turns >= kMapPlayMinTurns);
        std::vector<MapPlays> all;
        ASSERT_TRUE(rig.history.map_plays(all));
        ASSERT_EQ(all.size(), size_t{1});
        ASSERT_TRUE(all[0].map == "TINY.LVL" && all[0].hash == map_hash_text(hash) && all[0].plays == 3);
        HistoryRecord last;
        ASSERT_TRUE(rig.history.find(id2, last));
        ASSERT_EQ(all[0].last_played, last.ended_s);
        // a restart: another store and feeder on the same file find every match known and count nothing again
        HistoryStore again(rig.database);
        std::string why;
        ASSERT_TRUE(again.prepare(why));
        HistoryFeeder feeder(again, rig.replays, maps_dir());
        uint32_t ms = 100000;
        for (int i = 0; i < 6; ++i) feeder.step([] { return true; }, ms += 300);
        ASSERT_TRUE(feeder.counted() == 0 && feeder.pending() == 0);
        ASSERT_TRUE(again.map_plays(all) && all.size() == 1 && all[0].plays == 3);
        // the owner takes a match out and its recording is counted neither again (marker) nor taken from the plays
        ASSERT_TRUE(again.remove(id1, true, why) == HistoryStore::Removal::Done);
        for (int i = 0; i < 6; ++i) feeder.step([] { return true; }, ms += 300);
        ASSERT_TRUE(again.map_plays(all) && all.size() == 1 && all[0].plays == 3);
    } TEST_END();

    TEST_CASE("MP2.2 A match on an approved map of the players is counted: the feeder reads the map's file (a name of the submissions folder) by the name in the recording, checks its hash, and the plays are the map's under its own name; a name the server does not know, or a file that is not the match's map, leaves the match out") {
        MatchSpec spec;
        spec.seed = 31;
        spec.turns = 1000;
        spec.roster = 0x03;
        spec.names = {"Ann", "Bob", "", ""};
        const std::vector<uint8_t> played = record_match(spec);
        ASSERT_TRUE(!played.empty());
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(replay::decode(played.data(), played.size(), rep, why));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "/TINY.LVL", hash));
        ASSERT_EQ(rep.head.map_hash, hash);
        rep.head.map_name = "Approved One.lvl";                                                      // (TINY's bytes under the name of an approved map: its file is in no maps folder)
        std::string error;
        const std::vector<uint8_t> renamed = replay::encode(rep, error);
        ASSERT_TRUE(!renamed.empty());
        TempDir nowhere;                                                                             // (a maps folder with no map in it)
        {   // the server does not know the name: the match is left out, as ever
            Rig rig(nowhere.path.string());
            ASSERT_TRUE(rig.open());
            ASSERT_TRUE(!rig.keep(renamed).empty());
            rig.drain(8);
            ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.left_out() == 1);
        }
        {   // a name that is known: the match is counted, and the plays are the approved map's (name and hash)
            Rig rig(nowhere.path.string());
            rig.feeder.set_approved_map_path([](const std::string& name) { return name == "Approved One.lvl" ? maps_dir() + "/TINY.LVL" : std::string(); });
            ASSERT_TRUE(rig.open());
            ASSERT_TRUE(!rig.keep(renamed).empty());
            rig.drain(8);
            ASSERT_TRUE(rig.history.count() == 1 && rig.feeder.counted() == 1 && rig.feeder.left_out() == 0);
            std::vector<MapPlays> all;
            ASSERT_TRUE(rig.history.map_plays(all));
            ASSERT_TRUE(all.size() == 1 && all[0].map == "Approved One.lvl" && all[0].hash == map_hash_text(hash) && all[0].plays == 1);
        }
        {   // the server's own wiring: the history of a RoomManager counts a match on a map that its store has as approved, and not one on a map that was taken down
            Server srv(nowhere.path.string());
            ASSERT_TRUE(srv.open());
            ApprovedMap one;
            one.name = "Approved One.lvl";
            one.path = maps_dir() + "/TINY.LVL";
            one.hash = hash;
            one.size = 1;
            one.width = one.height = 31;
            one.kind = "classic";
            srv.mgr.mutable_store().set_approved({one});
            ASSERT_TRUE(!srv.keep(renamed).empty());
            srv.run_updates(40);
            ASSERT_EQ(srv.mgr.history_store()->count(), size_t{1});
            ApprovedMap big = one;                                                                   // (the same file, listed as a map that is over the online limit: a recording on it is not played here in one piece)
            big.width = big.height = 300;
            srv.mgr.mutable_store().set_approved({big});
            spec.seed = 33;
            replay::Replay second;
            const std::vector<uint8_t> played_again = record_match(spec);
            ASSERT_TRUE(replay::decode(played_again.data(), played_again.size(), second, why));
            second.head.map_name = "Approved One.lvl";
            ASSERT_TRUE(!srv.keep(replay::encode(second, error)).empty());
            srv.run_updates(40);
            ASSERT_EQ(srv.mgr.history_store()->count(), size_t{1});
            srv.mgr.mutable_store().set_approved({});                                                // (taken down: the map is nowhere)
            spec.seed = 34;
            replay::Replay third;
            const std::vector<uint8_t> played_third = record_match(spec);
            ASSERT_TRUE(replay::decode(played_third.data(), played_third.size(), third, why));
            third.head.map_name = "Approved One.lvl";
            ASSERT_TRUE(!srv.keep(replay::encode(third, error)).empty());
            srv.run_updates(40);
            ASSERT_EQ(srv.mgr.history_store()->count(), size_t{1});
        }
        {   // the name is known but its file is another map than the match's: left out (the hash is the match's)
            Rig rig(nowhere.path.string());
            rig.feeder.set_approved_map_path([](const std::string&) { return maps_dir() + "/SMALL.LVL"; });
            ASSERT_TRUE(rig.open());
            ASSERT_TRUE(!rig.keep(renamed).empty());
            rig.drain(8);
            ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.left_out() == 1);
        }
    } TEST_END();

    TEST_CASE("MP2.3 A recording on a map that starts more ants than a room plays (8,000, the four colors added up) is not played by the feeder (one piece on the one thread, seconds for 40,000 ants): it is left out; one at the cap is counted as ever") {
        TempDir maps;
        const auto keep_map = [&](const char* name, uint32_t per_color) {
            const std::string bytes = tiny_with_ants({per_color, per_color, per_color, per_color});
            std::ofstream out(maps.path / name, std::ios::binary);
            out << bytes;
            return !bytes.empty() && static_cast<bool>(out);
        };
        ASSERT_TRUE(keep_map("AT CAP.LVL", 2000) && keep_map("OVER CAP.LVL", 2001));                // (8,000 and 8,004)
        for (const bool over : {false, true}) {
            MatchSpec spec;
            spec.dir = maps.path.string();
            spec.file = over ? "OVER CAP.LVL" : "AT CAP.LVL";
            spec.seed = 41;
            spec.turns = 700;
            spec.roster = 0x03;
            spec.names = {"Ann", "Bob", "", ""};
            const std::vector<uint8_t> played = record_match(spec);
            ASSERT_TRUE(!played.empty());
            Rig rig(maps.path.string());
            ASSERT_TRUE(rig.open());
            ASSERT_TRUE(!rig.keep(played).empty());
            rig.drain(8);
            if (rig.history.count() != (over ? 0u : 1u) || rig.feeder.left_out() != (over ? 1u : 0u)) std::cout << "[" << (over ? "over the cap" : "at the cap") << ": " << rig.history.count() << " counted, " << rig.feeder.left_out() << " left out] " << std::flush;
            ASSERT_TRUE(rig.history.count() == (over ? 0u : 1u) && rig.feeder.left_out() == (over ? 1u : 0u));
        }
    } TEST_END();

    TEST_CASE("MP3.1 A server with no database or no history keeps no count and nothing fails: the match path and the routes are as before") {
        Server none;
        ASSERT_TRUE(none.open(false));                                                               // (a replay store, no history)
        ASSERT_TRUE(none.mgr.history_store() == nullptr);
        none.run_updates(10);
        ASSERT_EQ(none.control("GET", "/history").status, 404);
        ASSERT_EQ(none.control("GET", "/stats").status, 200);
        RoomManager bare{MapStore(maps_dir())};                                                      // (no replay store, no database at all)
        bare.update(1000);
        bare.update(1300);
        ASSERT_TRUE(bare.history_store() == nullptr && bare.take_notices().empty());
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
