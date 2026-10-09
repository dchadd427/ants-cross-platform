// Tests of the board of the matches that run now (ants_server/live_board.hpp): the id of a match (made of the map and the second it began in, and of nothing else), the list (30 seconds, no failed
// recording, the newest first, at most 50, the players in the strings of the replay list), the snapshot (the recorder's incomplete file, cached for the clock's second), and the memory of the
// matches that ended (the file they were kept as, 15 minutes, 256 of them). No network and no room: the matches are recorders that the test feeds with ticks, and the clock is the test's own.
// The board in front of real rooms and the public door's answers are in test_server (replay_tests.hpp, S3.176 and on).
#include "ants_assets/lvl_parser.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/live_board.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ANTS_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_GETPID getpid
#endif

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

constexpr int64_t kT0 = 1791469929;       // 2026-10-08 14:32:09 UTC
const std::string kId0 = "TINY-20261008-143209Z";

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_live_board_" + std::to_string(ANTS_GETPID()) + "_" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// The test's clock, and a board that reads it
struct Clock {
    int64_t now{kT0};
};

// One engine, never ticked: a recorder reads its state hash every 100th turn, which is all a board test needs of a match
struct Fixture {
    assets::LevelData level;
    sim::SimulationEngine engine;
    bool ok{false};
    Fixture() {
        ok = level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL");
        if (ok) engine.init(level, 1, 0x0F);
    }
};

replay::Header head_of(const std::string& map = "TINY.LVL", uint8_t roster = 0x03, std::array<std::string, sim::MAX_PLAYERS> names = {{"Ann", "", "", ""}}) {
    replay::Header h;
    h.game_version = "v0.0.0-test";
    h.build_id = "abc123";
    h.venue = "game server";
    h.map_name = map;
    h.map_hash = 0x1122334455667788ull;
    h.seed = 7;
    h.roster = roster;
    h.names = names;
    return h;
}

// A recording that has run `turns`
struct Rec {
    std::unique_ptr<replay::Recorder> rec;
    Rec(Fixture& f, uint32_t turns, replay::Header head = head_of()) : rec(std::make_unique<replay::Recorder>(std::move(head))) { run(f, turns); }
    void run(Fixture& f, uint32_t turns) {
        for (uint32_t i = 0; i < turns; ++i) rec->on_tick(f.engine);
    }
};

struct Decoded {
    bool ok{false};
    replay::Replay replay;
    std::string error;
};

Decoded decoded(const std::vector<uint8_t>& bytes) {
    Decoded d;
    d.ok = replay::decode(bytes.data(), bytes.size(), d.replay, d.error);
    return d;
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Live Board: the matches that run now, for the public door\n"
                 "=======================================================\n";
    Fixture f;
    if (!f.ok) {
        std::cout << "  The shipped map TINY is missing: nothing can be tested\n";
        return 1;
    }

    TEST_CASE("LB1.1 The Id Of A Match Is The Map's Name And The Second It Began In (Letters, Digits, '_', At Most 24), With -2, -3 ... For Matches Of One Map That Began In One Second, Unique Among The Matches That Run And The Ones That Ended Lately; The Shape Is Checked By valid_id") {
        for (const char* good : {"TINY-20261008-143209Z", "TINY-20261008-143209Z-2", "TINY-20261008-143209Z-9999", "a_B9-20261008-143209Z-0", "ABCDEFGHIJKLMNOPQRSTUVWX-20261008-143209Z", "_-00000000-000000Z"}) ASSERT_TRUE(LiveBoard::valid_id(good));
        for (const char* bad : {"", "-", "TINY", "TINY-20261008-143209", "TINY-20261008-143209z", "TINY-2026100-143209Z", "TINY-20261008-14320Z", "TINY-20261008-1432099Z", "-20261008-143209Z", "TINY--20261008-143209Z",
                                "ABCDEFGHIJKLMNOPQRSTUVWXY-20261008-143209Z",              // (a stem of 25)
                                "TINY-20261008-143209Z-", "TINY-20261008-143209Z-12345", "TINY-20261008-143209Z-x", "TINY-20261008-143209Z-2-3", "TINY-20261008-143209Z-2 ", "TINY-20261008-143209Z\n",
                                " TINY-20261008-143209Z", "TI NY-20261008-143209Z", "TI-NY-20261008-143209Z", "TINY-2026100a-143209Z", "../TINY-20261008-143209Z", "TINY/../x-20261008-143209Z", "%54INY-20261008-143209Z",
                                "TINY-20261008-143209Z.antsrep", "ants-TINY-20261008-143209Z.antsrep", "TINY\xC3\xA9-20261008-143209Z"}) {
            ASSERT_FALSE(LiveBoard::valid_id(bad));
        }
        ASSERT_FALSE(LiveBoard::valid_id(std::string("TINY-20261008-143209Z\0", 22)));
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        Rec a(f, 0), b(f, 0), c(f, 0);
        ASSERT_EQ(board.begin(*a.rec), kId0);
        ASSERT_EQ(board.begin(*b.rec), kId0 + "-2");                               // (a second match of the map in the same second)
        ASSERT_EQ(board.begin(*c.rec), kId0 + "-3");
        clock.now += 1;
        Rec d(f, 0);
        ASSERT_EQ(board.begin(*d.rec), "TINY-20261008-143210Z");
        // a map of another name is another id; the characters that a name must not carry are '_' and the stem is cut at 24
        Rec e(f, 0, head_of("DRY LAND.lvl")), g(f, 0, head_of("A-B.LVL")), h(f, 0, head_of("Mäp.LVL")), i(f, 0, head_of("THISMAPHASAVERYLONGNAMEINDEED.LVL")), j(f, 0, head_of("NODOT"));
        ASSERT_EQ(board.begin(*e.rec), "DRY_LAND-20261008-143210Z");
        ASSERT_EQ(board.begin(*g.rec), "A_B-20261008-143210Z");
        ASSERT_EQ(board.begin(*h.rec), "M__p-20261008-143210Z");
        ASSERT_EQ(board.begin(*i.rec), "THISMAPHASAVERYLONGNAMEI-20261008-143210Z");
        ASSERT_EQ(board.begin(*j.rec), "NODOT-20261008-143210Z");
        for (const std::string& id : {kId0, kId0 + "-2", std::string("DRY_LAND-20261008-143210Z"), std::string("M__p-20261008-143210Z"), std::string("THISMAPHASAVERYLONGNAMEI-20261008-143210Z")}) ASSERT_TRUE(LiveBoard::valid_id(id));
        // the id is made of the map and the second and of nothing else: not of the names, the seed, the seats, the map's hash (the board never sees a room's code)
        {
            LiveBoard other([&clock]() { return clock.now; });
            replay::Header other_head = head_of("TINY.LVL", 0x0F, {{"Zed", "Yan", "Xi", "Wu"}});
            other_head.seed = 99;
            other_head.map_hash = 5;
            Rec k(f, 0, other_head);
            ASSERT_EQ(other.begin(*k.rec), "TINY-20261008-143210Z");
        }
        // the id of a match that ended lately is not given again (a page that watched the old one would be shown the new one) while the board remembers it
        {
            Clock same;
            LiveBoard memory([&same]() { return same.now; });
            Rec old(f, 700);
            const std::string first = memory.begin(*old.rec);
            ASSERT_EQ(first, kId0);
            memory.end(first, "");
            ASSERT_EQ(memory.remembered(), size_t{1});
            Rec again(f, 0), third(f, 0);
            ASSERT_EQ(memory.begin(*again.rec), kId0 + "-2");
            ASSERT_EQ(memory.begin(*third.rec), kId0 + "-3");
            same.now += LiveBoard::kRememberS;                                     // (a new second, a new id)
            Rec later(f, 0);
            ASSERT_EQ(memory.begin(*later.rec), "TINY-20261008-144709Z");
        }
        // 9999 ids in one second and then none: the match cannot be named and is not on the board
        Clock at_once;
        LiveBoard crowd([&at_once]() { return at_once.now; });
        std::vector<std::unique_ptr<replay::Recorder>> recorders;
        std::set<std::string> ids;
        for (unsigned n = 0; n < 9999; ++n) {
            recorders.push_back(std::make_unique<replay::Recorder>(head_of()));
            const std::string id = crowd.begin(*recorders.back());
            ASSERT_TRUE(LiveBoard::valid_id(id));
            ids.insert(id);
        }
        ASSERT_EQ(ids.size(), size_t{9999});
        ASSERT_TRUE(ids.count(kId0) == 1 && ids.count(kId0 + "-9999") == 1);
        recorders.push_back(std::make_unique<replay::Recorder>(head_of()));
        ASSERT_EQ(crowd.begin(*recorders.back()), std::string());
        ASSERT_EQ(crowd.running(), size_t{9999});
    } TEST_END();

    TEST_CASE("LB1.2 The List Holds The Matches That Ran 30 Seconds (600 Turns) And Whose Recording Has Not Failed, The Newest Start First, At Most 50; An Entry Says The Id, The Map, When It Began, The Turns So Far, The Seconds And The Players In The Strings Of The Replay List") {
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        ASSERT_TRUE(board.list().empty());
        Rec young(f, 599);
        const std::string young_id = board.begin(*young.rec);
        ASSERT_TRUE(board.list().empty());                                         // (599 turns: not yet)
        young.run(f, 1);
        std::vector<LiveMatch> list = board.list();
        ASSERT_EQ(list.size(), size_t{1});
        ASSERT_TRUE(list[0].id == young_id && list[0].map == "TINY.LVL" && list[0].started == kT0 && list[0].turns == 600);
        ASSERT_EQ(list[0].players, (std::vector<std::string>{"Green (Ann)", "Red"}));
        young.run(f, 40);
        ASSERT_EQ(board.list()[0].turns, 640u);                                    // (the turns so far, read when the list is made)
        // a recording that failed is not listed (a turn that was not seen: the recording is lost)
        Rec lost(f, 650, head_of("TREASURE.LVL"));
        const std::string lost_id = board.begin(*lost.rec);
        ASSERT_EQ(board.list().size(), size_t{2});
        net::TurnMsg late;
        late.turn = 9;
        lost.rec->on_turn(late, f.engine);
        ASSERT_FALSE(lost.rec->failure().empty());
        list = board.list();
        ASSERT_TRUE(list.size() == 1 && list[0].id == young_id);
        ASSERT_EQ(board.running(), size_t{2});                                     // (it is still on the board: its room has not ended it)
        board.end(lost_id, "");
        // newest start first: by the second the match began in, and on a tie the one that began last
        clock.now += 10;
        Rec second(f, 700, head_of("ISLANDS.LVL", 0x0D, {{"Cat", "", "Bot (Medium)", "Dan"}}));
        const std::string second_id = board.begin(*second.rec);
        Rec third(f, 800, head_of("ISLANDS.LVL"));
        const std::string third_id = board.begin(*third.rec);
        list = board.list();
        ASSERT_EQ(list.size(), size_t{3});
        ASSERT_TRUE(list[0].id == third_id && list[1].id == second_id && list[2].id == young_id);
        ASSERT_EQ(second_id, "ISLANDS-20261008-143219Z");
        ASSERT_EQ(third_id, second_id + "-2");
        ASSERT_TRUE(list[0].started == kT0 + 10 && list[2].started == kT0);
        ASSERT_EQ(list[1].players, (std::vector<std::string>{"Green (Cat)", "Blue (Bot (Medium))", "Black (Dan)"}));       // seats 0, 2 and 3 of the roster 0x0D
        // at most 50
        std::vector<std::unique_ptr<Rec>> many;
        for (unsigned n = 0; n < 60; ++n) {
            clock.now += 1;
            many.push_back(std::make_unique<Rec>(f, 600));
            board.begin(*many.back()->rec);
        }
        list = board.list();
        ASSERT_EQ(list.size(), LiveBoard::kMaxListed);
        ASSERT_EQ(LiveBoard::kMaxListed, size_t{50});
        for (size_t i = 1; i < list.size(); ++i) ASSERT_TRUE(list[i - 1].started > list[i].started);        // (the newest 50 are the last 50 that began)
        ASSERT_EQ(list.front().started, kT0 + 70);
        ASSERT_EQ(list.back().started, kT0 + 21);
        ASSERT_EQ(board.running(), size_t{3 + 60});
    } TEST_END();

    TEST_CASE("LB1.3 The Players Are Written Exactly As The Replay List Writes Them: The Strings Of A Match That Runs Are The Ones Of Its File Once It Is Kept (A Name, The Colour Alone, \"Bot (Medium)\")") {
        TempDir dir;
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        ReplayConfig config;
        config.dir = (dir.path / "replays").string();
        config.game_version = "v0.0.0-test";
        config.build_id = "abc123";
        config.min_free_bytes = 0;
        config.clock_s = [&clock]() { return clock.now; };
        ReplayStore store(config);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        Rec r(f, 650, head_of("TREASURE.LVL", 0x0F, {{"Ann", "", "Bot (Hard)", "O'Neil \"Q\" <b>"}}));
        board.begin(*r.rec);
        const std::vector<LiveMatch> list = board.list();
        ASSERT_EQ(list.size(), size_t{1});
        std::string error;
        const std::vector<uint8_t> bytes = r.rec->finish(f.engine, error);
        ASSERT_FALSE(bytes.empty());
        const ReplaySave saved = store.save(bytes);
        ASSERT_TRUE(saved.kept);
        const std::vector<ReplayEntry> kept = store.list();
        ASSERT_EQ(kept.size(), size_t{1});
        ASSERT_EQ(list[0].players, kept[0].players);
        ASSERT_EQ(list[0].players, (std::vector<std::string>{"Green (Ann)", "Red", "Blue (Bot (Hard))", "Black (O'Neil \"Q\" <b>)"}));
        ASSERT_EQ(list[0].map, kept[0].map);
        ASSERT_EQ(list[0].turns, kept[0].turns);
    } TEST_END();

    TEST_CASE("LB1.4 The Snapshot Is The Recorder's Incomplete File With The Turns Run When It Was Made, Handed Out Again Until The Clock Is In A Later Second, Then Made Again; A Match That Is Not Listed, Failed, Ended Or Unknown Has None; Asking Changes Nothing In The Recording") {
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        Rec a(f, 600, head_of("TREASURE.LVL"));
        const std::string id = board.begin(*a.rec);
        const std::vector<uint8_t>* first = board.snapshot(id);
        ASSERT_TRUE(first != nullptr);
        const std::vector<uint8_t> first_bytes = *first;
        Decoded d = decoded(first_bytes);
        ASSERT_TRUE(d.ok && !d.replay.complete && d.replay.total_turns == 600 && d.replay.head.map_name == "TREASURE.LVL" && d.replay.hashes.size() == 6);
        // the match runs on: in the same second the same bytes go to everybody, in the next one a new snapshot
        a.run(f, 50);
        ASSERT_EQ(*board.snapshot(id), first_bytes);
        ASSERT_EQ(*board.snapshot(id), first_bytes);
        clock.now += 1;
        const std::vector<uint8_t>* second = board.snapshot(id);
        ASSERT_TRUE(second != nullptr && *second != first_bytes);
        d = decoded(*second);
        ASSERT_TRUE(d.ok && d.replay.total_turns == 650);
        const std::vector<uint8_t> second_bytes = *second;
        a.run(f, 50);
        clock.now += 5;
        ASSERT_EQ(decoded(*board.snapshot(id)).replay.total_turns, 700u);
        a.run(f, 10);
        clock.now -= 3;                                                            // (a clock that is set back: the snapshot is made again, never held for ever)
        ASSERT_EQ(decoded(*board.snapshot(id)).replay.total_turns, 710u);
        ASSERT_EQ(LiveBoard::kSnapshotMaxAgeS, int64_t{1});
        // each match has its own
        Rec b(f, 650);
        const std::string other = board.begin(*b.rec);
        ASSERT_EQ(decoded(*board.snapshot(other)).replay.total_turns, 650u);
        ASSERT_EQ(decoded(*board.snapshot(id)).replay.total_turns, 710u);
        // asking changed nothing in the recording: it ends in the file that a recorder nobody asked about writes
        Rec plain(f, 710, head_of("TREASURE.LVL"));
        std::string error;
        ASSERT_EQ(a.rec->finish(f.engine, error), plain.rec->finish(f.engine, error));
        // nothing for an id that is unknown, of a match that is not 30 seconds old, whose recording failed, or that ended
        ASSERT_TRUE(board.snapshot("TINY-20200101-000000Z") == nullptr && board.snapshot("") == nullptr && board.snapshot("../x") == nullptr);
        Rec young(f, 599);
        const std::string young_id = board.begin(*young.rec);
        ASSERT_TRUE(board.snapshot(young_id) == nullptr);
        young.run(f, 1);
        ASSERT_TRUE(board.snapshot(young_id) != nullptr);
        net::TurnMsg late;
        late.turn = 9;
        young.rec->on_turn(late, f.engine);
        ASSERT_TRUE(board.snapshot(young_id) == nullptr);
        board.end(young_id, "");
        ASSERT_TRUE(board.snapshot(young_id) == nullptr);
        board.end(other, "ants-TINY-20261008-143300Z.antsrep");
        ASSERT_TRUE(board.snapshot(other) == nullptr);
    } TEST_END();

    TEST_CASE("LB1.5 A Match That Is Taken Off The Board Disappears From The List And Has No Snapshot; If It Had Run 30 Seconds Its Outcome Is Remembered (The File It Was Kept As, Empty When It Was Not Kept) For 15 Minutes, 256 Matches At Most; A Shorter Match Leaves Nothing, An Unknown Id Is Ignored") {
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        Rec kept(f, 700), dropped(f, 700), brief(f, 599);
        const std::string kept_id = board.begin(*kept.rec);
        const std::string dropped_id = board.begin(*dropped.rec);
        const std::string brief_id = board.begin(*brief.rec);
        ASSERT_EQ(board.list().size(), size_t{2});
        std::string file = "unchanged";
        ASSERT_FALSE(board.outcome(kept_id, file));                                // (it runs: it has not ended)
        ASSERT_EQ(file, "unchanged");
        board.end(kept_id, "ants-TINY-20261008-143300Z.antsrep");
        ASSERT_TRUE(board.list().size() == 1 && board.list()[0].id == dropped_id);
        ASSERT_TRUE(board.snapshot(kept_id) == nullptr);
        ASSERT_TRUE(board.outcome(kept_id, file));
        ASSERT_EQ(file, "ants-TINY-20261008-143300Z.antsrep");
        board.end(dropped_id, "");
        ASSERT_TRUE(board.outcome(dropped_id, file));
        ASSERT_EQ(file, std::string());
        board.end(brief_id, "");
        ASSERT_FALSE(board.outcome(brief_id, file));                               // (it never was in the list: nothing is remembered of it)
        ASSERT_EQ(board.running(), size_t{0});
        ASSERT_EQ(board.remembered(), size_t{2});
        board.end("TINY-20200101-000000Z", "x.antsrep");                           // (not on the board: ignored)
        board.end(kept_id, "ants-TINY-20261008-143399Z.antsrep");                  // (ended already: ignored, the first outcome stands)
        ASSERT_TRUE(board.outcome(kept_id, file) && file == "ants-TINY-20261008-143300Z.antsrep");
        ASSERT_FALSE(board.outcome("TINY-20200101-000000Z", file));
        ASSERT_FALSE(board.outcome("", file));
        // 15 minutes, counted from the end
        clock.now += LiveBoard::kRememberS - 1;
        ASSERT_TRUE(board.outcome(kept_id, file) && board.outcome(dropped_id, file));
        ASSERT_EQ(board.remembered(), size_t{2});
        Rec next(f, 700);
        const std::string next_id = board.begin(*next.rec);
        clock.now += 1;                                                            // (900 s after the end)
        ASSERT_FALSE(board.outcome(kept_id, file));
        ASSERT_FALSE(board.outcome(dropped_id, file));
        ASSERT_EQ(board.remembered(), size_t{0});
        ASSERT_EQ(LiveBoard::kRememberS, int64_t{900});
        board.end(next_id, "ants-TINY-20261008-144000Z.antsrep");
        ASSERT_TRUE(board.outcome(next_id, file) && file == "ants-TINY-20261008-144000Z.antsrep");
        // the board forgets without being asked: once the clock has moved on, the old ones are gone from the next change
        clock.now += LiveBoard::kRememberS;
        Rec last(f, 700);
        board.end(board.begin(*last.rec), "");
        ASSERT_FALSE(board.outcome(next_id, file));
        ASSERT_EQ(board.remembered(), size_t{1});
        // at most 256: the oldest are forgotten first
        LiveBoard flood([&clock]() { return clock.now; });
        std::vector<std::string> ids;
        std::vector<std::unique_ptr<replay::Recorder>> recorders;
        for (unsigned n = 0; n < 300; ++n) {
            clock.now += 1;
            recorders.push_back(std::make_unique<replay::Recorder>(head_of()));
            for (uint32_t t = 0; t < 600; ++t) recorders.back()->on_tick(f.engine);
            ids.push_back(flood.begin(*recorders.back()));
            flood.end(ids.back(), "ants-TINY-" + std::to_string(n) + ".antsrep");
        }
        ASSERT_EQ(LiveBoard::kMaxRemembered, size_t{256});
        ASSERT_EQ(flood.remembered(), size_t{256});
        ASSERT_FALSE(flood.outcome(ids[0], file));
        ASSERT_FALSE(flood.outcome(ids[43], file));
        ASSERT_TRUE(flood.outcome(ids[44], file) && file == "ants-TINY-44.antsrep");
        ASSERT_TRUE(flood.outcome(ids[299], file) && file == "ants-TINY-299.antsrep");
    } TEST_END();

    TEST_CASE("LB1.6 A Clock Set Back Does Not Keep An Ended Match For Longer Than The Memory Is: Its Age Counts From The Earlier Time") {
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        Rec r(f, 700);
        const std::string id = board.begin(*r.rec);
        clock.now += 100000;                                                       // (a clock that is ahead ...)
        board.end(id, "");
        clock.now -= 100000 + 100;                                                 // ... and is set back by more than a day, to before the match ended
        Rec other(f, 700);
        board.end(board.begin(*other.rec), "");                                    // (the board looks at its memory when it changes)
        std::string file;
        ASSERT_TRUE(board.outcome(id, file));
        clock.now += LiveBoard::kRememberS;
        ASSERT_FALSE(board.outcome(id, file));
    } TEST_END();

    TEST_CASE("LB1.7 A Match With Fog Of War Is Not On The Board (A Live View Would Show What The Fog Hides From Its Players), And What Becomes Of It Changes Nothing For The Others") {
        Clock clock;
        LiveBoard board([&clock]() { return clock.now; });
        replay::Header fogged = head_of("TINY.LVL", 0x03);
        fogged.fog = true;
        Rec hidden(f, 700, fogged), visible(f, 700);
        ASSERT_EQ(board.begin(*hidden.rec), std::string());                        // (no id: the room keeps none, and end() of "" is a no-op)
        ASSERT_EQ(board.running(), size_t{0});
        const std::string id = board.begin(*visible.rec);
        ASSERT_EQ(id, kId0);                                                       // (the fog match took no id of the second)
        ASSERT_EQ(board.running(), size_t{1});
        ASSERT_EQ(board.list().size(), size_t{1});
        ASSERT_TRUE(board.snapshot(id) != nullptr);
        board.end(std::string(), "ants-TINY-20261008-143209Z.antsrep");
        ASSERT_EQ(board.remembered(), size_t{0});
        ASSERT_EQ(board.running(), size_t{1});
        board.end(id, "");
        ASSERT_EQ(board.remembered(), size_t{1});
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures
              << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
