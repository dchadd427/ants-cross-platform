// Tests of the match history of the game server (ants_server/history_store.hpp, docs/REPLAYS.md "The match history"): what a record says of a match that was played again (the winner and the draw, who left, the
// format, the computers), its JSON file and the reader that skips what it does not know, the store on a real folder (written whole, read again at the start, a crashed write, a damaged file, a rename that
// fails, the marker of a record that the owner took out, thousands of records), the filters and the sorts of the list, the feeder that counts the matches of the replay store one at a time (the newest first, paced,
// only while the server is quiet, real recordings played again on the shipped TINY map), and the two doors of the control interface (GET /history, GET /history/<id>, DELETE /history/<id>).
//
// No network and no room that runs a match: the recordings are made here with the engine and the recorder, the clock and the folders are the test's own. The whole match through a room's door is
// test_server (S3.18x).
#include "ants_ai/arena.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/control.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
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

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_history_store_" + std::to_string(ANTS_GETPID()) + "_" + std::to_string(counter++));
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

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

size_t lines_with(const std::vector<std::string>& lines, const std::string& part) {
    size_t n = 0;
    for (const std::string& l : lines) n += contains(l, part) ? 1u : 0u;
    return n;
}

std::string slurp_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::string> names_in(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

ctl::JsonValue parse(const std::string& text) {
    ctl::JsonValue v;
    std::string why;
    if (!ctl::parse_json(text, v, &why, history_file_limits())) return ctl::JsonValue::make_null();
    return v;
}

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

// "ants-TINY-20261008-143209Z" (and "-2" for a second match of that map that ended in the same second)
std::string id_at(int64_t t, const std::string& map = "TINY", int n = 1) {
    return "ants-" + map + "-" + ReplayStore::time_stamp(t) + (n > 1 ? "-" + std::to_string(n) : std::string());
}

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
HistoryRecord record_of(int64_t t, const char* map, const char* format, uint32_t turns, bool finished, int quitter, bool draw, std::vector<HistorySeat> seats) {
    HistoryRecord r;
    r.id = id_at(t, ReplayStore::map_stem(map));
    r.ended_s = t;
    r.map = map;
    r.game = "v0.12.0-test";
    r.turns = turns;
    r.finished = finished;
    r.quitter = quitter;
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

// The six records that the filter and sort tests use (index = age: the last is the newest)
std::vector<HistoryRecord> six_records() {
    std::vector<HistoryRecord> v;
    v.push_back(record_of(kT0, "TINY.LVL", "1v1", 1200, true, -1, false, {seat_of(0, "Ann", "", 900, 10, 2, 5), seat_of(1, "Bob", "", 400, 2, 10, 2)}));
    v.push_back(record_of(kT0 + 100, "TINY.LVL", "ffa4", 6000, true, -1, false,
                          {seat_of(0, "Cy", "", 300, 3, 3, 1), seat_of(1, "", "", 700, 8, 4, 6), seat_of(2, "", "Hard", 500, 5, 5, 4), seat_of(3, "", "Bot", 100, 1, 9, 0)}));
    v.push_back(record_of(kT0 + 200, "TREASURE.LVL", "2v2", 3000, true, -1, false,
                          {seat_of(0, "Dee", "", 600, 6, 6, 6), seat_of(1, "Eve", "", 550, 5, 7, 3), seat_of(2, "Fay", "", 300, 3, 9, 2), seat_of(3, "Gus", "", 200, 2, 10, 1)}));
    v.push_back(record_of(kT0 + 300, "TINY.LVL", "1v1", 2400, true, -1, true, {seat_of(0, "", "Medium", 500, 5, 5, 5), seat_of(1, "", "Medium", 500, 5, 5, 5)}));
    v.push_back(record_of(kT0 + 400, "TREASURE.LVL", "1v1", 800, true, 1, false, {seat_of(0, "Hal", "", 800, 9, 1, 4), seat_of(1, "Ian", "", 100, 1, 9, 1)}));
    v.push_back(record_of(kT0 + 500, "TINY.LVL", "ffa3", 36000, false, -1, false, {seat_of(0, "Jo", "", 250, 2, 2, 2), seat_of(1, "Kim", "", 250, 4, 4, 3), seat_of(2, "", "Easy", 150, 1, 3, 0)}));
    return v;
}

bool same_seat(const HistorySeat& a, const HistorySeat& b) {
    return a.seat == b.seat && a.name == b.name && a.bot == b.bot && a.score == b.score && a.killed == b.killed && a.lost == b.lost && a.hatched == b.hatched && a.bombs_planted == b.bombs_planted &&
           a.bombs_defused == b.bombs_defused && a.fires_lit == b.fires_lit && a.left == b.left && a.winner == b.winner;
}

bool same_record(const HistoryRecord& a, const HistoryRecord& b) {
    if (a.id != b.id || a.ended_s != b.ended_s || a.map != b.map || a.game != b.game || a.turns != b.turns || a.finished != b.finished || a.quitter != b.quitter || a.format != b.format || a.draw != b.draw ||
        a.computers_only != b.computers_only || a.seats.size() != b.seats.size() || a.rows.size() != b.rows.size())
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
    for (const HistoryRecord* r : page.items) out.push_back(r->id);
    return out;
}

// the ids of the records `which` (indexes into `all`), in that order
std::vector<std::string> ids_at(const std::vector<HistoryRecord>& all, std::vector<size_t> which) {
    std::vector<std::string> out;
    for (const size_t i : which) out.push_back(all[i].id);
    return out;
}

// ---- matches played with the engine (real recordings) ------------------------------------------------------------------------------

struct MatchSpec {
    uint32_t seed{7};
    uint32_t turns{1200};
    uint8_t roster{0x0F};
    sim::StartTeams teams;
    int quit_seat{-1};                                  // that seat quits after the last tick (a Quit that ends a match of two seats)
    std::array<std::string, sim::MAX_PLAYERS> names;
    uint16_t sim_rules{replay::kSimRules};
};

// A match on the shipped TINY map: the standard computer players gather food (the scores move), and on top of what they do every seat hatches an ant every 100 ticks and, from tick 1000, sends all its ants
// against an ant of the next seat every 200 ticks (so that ants are hatched, killed and lost), recorded as the server's rooms record one. Empty when the map is not there.
std::vector<uint8_t> record_match(const MatchSpec& spec) {
    const std::string path = maps_dir() + "/TINY.LVL";
    assets::LevelData level;
    uint64_t hash = 0;
    if (!level.load_from_file(path) || !net::hash_file(path, hash)) return std::vector<uint8_t>();
    ai::ArenaSpec arena;
    arena.level = &level;
    arena.seed = spec.seed;
    arena.max_ticks = spec.turns;
    arena.record = true;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((spec.roster >> seat) & 1u) != 0) arena.bots.push_back(ai::BotSpec{seat, "standard", ai::Level::Hard, ai::Style::Random});
    }
    const ai::ArenaResult played = ai::play_match(arena);
    if (!played.error.empty()) return std::vector<uint8_t>();
    replay::Replay shell;
    shell.head.game_version = "v0.12.0-test";
    shell.head.build_id = "test";
    shell.head.venue = "game server";
    shell.head.map_name = "TINY.LVL";
    shell.head.map_hash = hash;
    shell.head.seed = spec.seed;
    shell.head.roster = spec.roster;
    shell.head.names = spec.names;
    shell.head.teams = spec.teams;
    shell.head.sim_rules = spec.sim_rules;
    sim::SimulationEngine engine;
    replay::begin_match(engine, shell, level, false);                // (as the player sets a match up)
    replay::Recorder rec(shell.head);
    size_t next = 0;
    for (uint64_t step = 0; step < played.steps; ++step) {
        std::vector<sim::Command> extra;
        if (step >= 200 && step % 100 == 50) {
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                if (((spec.roster >> seat) & 1u) == 0) continue;
                sim::Command hatch;
                hatch.type = sim::CommandType::Hatch;
                hatch.issuer = seat;
                extra.push_back(hatch);
            }
        }
        if (step >= 1000 && step % 200 == 100) {
            const sim::WorldState& world = engine.get_world_state();
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                if (((spec.roster >> seat) & 1u) == 0) continue;
                sim::Command attack;
                attack.type = sim::CommandType::GroupAttack;
                attack.issuer = seat;
                for (const sim::AntSnapshot& a : world.ants) {
                    if (a.player_id == seat && a.hp > 0 && attack.ants.size() < sim::kMaxCommandAnts) attack.ants.push_back(a.id);
                    if (a.player_id != seat && a.hp > 0 && ((spec.roster >> a.player_id) & 1u) != 0 && attack.tile_x == 0) {
                        attack.tile_x = static_cast<int16_t>(a.tile_x);
                        attack.tile_y = static_cast<int16_t>(a.tile_y);
                    }
                }
                if (!attack.ants.empty() && attack.tile_x != 0) extra.push_back(attack);
            }
        }
        for (const sim::Command& c : extra) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        for (; next < played.log.size() && played.log[next].step == step; ++next) {
            engine.apply_command(played.log[next].command);
            rec.on_command(played.log[next].command);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    for (; next < played.log.size(); ++next) {                       // (orders that the arena gave after its last tick: the recording holds them at the turn that the match ended in)
        engine.apply_command(played.log[next].command);
        rec.on_command(played.log[next].command);
    }
    if (spec.quit_seat >= 0) {
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        quit.issuer = static_cast<uint8_t>(spec.quit_seat);
        engine.apply_command(quit);
        rec.on_command(quit);
    }
    std::string error;
    return rec.finish(engine, error);
}

// Four recordings that the tests share (made once): 0 four people free for all, 1 two people, 2 two people where the second one quits, 3 four people in two teams
const std::vector<uint8_t>& match_file(int k) {
    static std::map<int, std::vector<uint8_t>> cache;
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    MatchSpec spec;
    switch (k) {
        case 0:
            spec.seed = 11;
            spec.turns = 0;                                         // (until the rules end it)
            spec.names = {"Ann", "", "Bot (Hard)", "Bot"};
            break;
        case 1:
            spec.seed = 12;
            spec.turns = 1000;
            spec.roster = 0x03;
            spec.names = {"Cy", "Di", "", ""};
            break;
        case 2:
            spec.seed = 13;
            spec.turns = 800;
            spec.roster = 0x03;
            spec.quit_seat = 1;
            spec.names = {"Ed", "Flo", "", ""};
            break;
        default:
            spec.seed = 14;
            spec.turns = 900;
            spec.teams = sim::StartTeams{true, 0, 1};
            spec.names = {"Gil", "Hob", "Ivy", "Jon"};
            break;
    }
    return cache.emplace(k, record_match(spec)).first->second;
}

// A recording as the feeder reads it: decoded and played once
struct Played {
    bool ok{false};
    replay::Replay rep;
    replay::Outcome outcome;
};

Played play_file(const std::vector<uint8_t>& bytes) {
    Played p;
    std::string why;
    if (!replay::decode(bytes.data(), bytes.size(), p.rep, why)) return p;
    assets::LevelData level;
    if (!replay::load_map(p.rep.head, maps_dir(), level, why)) return p;
    p.outcome = replay::play(p.rep, level);
    p.ok = p.outcome.ran && p.outcome.ok;
    return p;
}

// ---- a replay store and a history in folders of the test, with the feeder between them -------------------------------------------------

struct Rig {
    TempDir temp;
    int64_t now{kT0};                                    // the replay store's clock (a file is named for the second it was kept in)
    bool rename_fails{false};                            // the history's rename (a record that the disk refuses)
    fs::path replay_dir;
    fs::path history_dir;
    ReplayStore replays;
    HistoryStore history;
    HistoryFeeder feeder;
    uint32_t ms{1000};
    int idle_asked{0};

    explicit Rig(const std::string& maps = maps_dir())
        : replay_dir(temp.path / "replays"), history_dir(temp.path / "history"), replays(replay_config()), history(history_config()), feeder(history, replays, maps) {}

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
    HistoryConfig history_config() {
        HistoryConfig c;
        c.dir = history_dir.string();
        c.rename_file = [this](const std::string& from, const std::string& to, std::error_code& ec) {
            if (rename_fails) {
                ec = std::make_error_code(std::errc::permission_denied);
                return;
            }
            fs::rename(from, to, ec);
        };
        return c;
    }
    bool open() {
        std::string why;
        return replays.prepare(why) && history.prepare(why);
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
    RoomManager mgr;
    uint32_t ms{1000};
    fs::path replay_dir;
    fs::path history_dir;

    Server() : mgr(MapStore(maps_dir())), replay_dir(temp.path / "replays"), history_dir(temp.path / "history") {}

    bool open(bool with_history = true) {
        ReplayConfig rc;
        rc.dir = replay_dir.string();
        rc.game_version = "v0.12.0-test";
        rc.build_id = "test";
        rc.clock_s = [this]() { return now; };
        rc.min_free_bytes = 0;
        rc.settle_s = 0;
        std::string why;
        if (!mgr.enable_replays(rc, false, why)) return false;
        if (!with_history) return true;
        HistoryConfig hc;
        hc.dir = history_dir.string();
        return mgr.enable_history(hc, why);
    }
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
    std::cout << "=======================================================\n MATCH HISTORY SUITE (what a match came to, kept for good)\n=======================================================\n";

    // ---- the record of a match ------------------------------------------------------------------------------------------------------

    TEST_CASE("HS1.1 An Id Is The Name Of A Recording Without Its Extension, And Only A Name That The Replay Store Could Have Made Is One") {
        for (const char* good : {"ants-TINY-20261008-143209Z", "ants-TREASURE-20261008-143209Z-2", "ants-a_B9-19700101-000000Z"}) {
            ASSERT_TRUE(valid_history_id(good));
            ASSERT_EQ(history_recording_file(good), std::string(good) + ".antsrep");
        }
        for (const char* bad : {"", "ants-TINY-20261008-143209Z.antsrep", "ants-TINY-20261008-143209Z.json", "../ants-TINY-20261008-143209Z", "ants-TINY-20261008-143209", "ants-TINY-20260230-143209Z",
                                "ants-TINY-20261008-143209Z/x", "ants-TINY-20261008-143209Z\\x", "ants-TINY-20261008-143209Z ", "x"}) {
            ASSERT_FALSE(valid_history_id(bad));
        }
        ASSERT_EQ(std::string(recording_name(Recording::Watch)), std::string("watch"));
        ASSERT_EQ(std::string(recording_name(Recording::Old)), std::string("old"));
        ASSERT_EQ(std::string(recording_name(Recording::Removed)), std::string("removed"));
    } TEST_END();

    TEST_CASE("HS1.2 A Record Is Made From The Engine's Own Result: The Winner Is The First Row, Equal In Score Kills Losses And Hatched Is A Draw, A Tie In Score Alone Goes To The Lower Seat, A Seat That Quit Is Marked Left And Is Last") {
        replay::Replay rep;
        rep.head.roster = 0x03;
        rep.head.names[0] = "Ann";
        rep.head.names[1] = "Bob";
        replay::Outcome out;
        out.ran = out.ok = true;
        out.turns = 1200;
        out.match_over = true;
        out.result.is_over = true;
        out.result.present_mask = 0x03;
        const auto stats = [&](uint8_t seat, int32_t score, uint32_t killed, uint32_t lost, uint32_t hatched) {
            sim::PlayerMatchStats& s = out.result.stats[seat];
            s.score = score;
            s.enemy_killed = killed;
            s.friendly_lost = lost;
            s.new_hatched = hatched;
            s.bombs_planted = seat + 4u;
            s.bombs_defused = seat + 5u;
            s.fires_lit = seat + 6u;
        };
        // a decided match: Bob has the higher score, so his row is first whatever the seat
        stats(0, 400, 2, 10, 2);
        stats(1, 900, 10, 2, 5);
        HistoryRecord r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_EQ(r.id, id_at(kT0));
        ASSERT_TRUE(r.ended_s == kT0 && r.turns == 1200 && r.seconds() == 60 && r.finished && r.quitter == -1 && r.format == "1v1" && !r.draw && !r.computers_only);
        ASSERT_TRUE(r.seats.size() == 2 && r.rows.size() == 2);
        ASSERT_TRUE(r.rows[0].seats == std::vector<uint8_t>{1} && r.rows[0].winner && r.rows[1].seats == std::vector<uint8_t>{0} && !r.rows[1].winner);
        ASSERT_TRUE(r.rows[0].score == 900 && r.rows[0].killed == 10 && r.rows[0].lost == 2 && r.rows[0].hatched == 5);
        ASSERT_TRUE(r.seats[0].name == "Ann" && r.seats[0].bot.empty() && !r.seats[0].winner && !r.seats[0].left);
        ASSERT_TRUE(r.seats[1].name == "Bob" && r.seats[1].winner && r.seats[1].score == 900 && r.seats[1].killed == 10 && r.seats[1].lost == 2 && r.seats[1].hatched == 5);
        ASSERT_TRUE(r.seats[1].bombs_planted == 5 && r.seats[1].bombs_defused == 6 && r.seats[1].fires_lit == 7);
        // all four numbers equal: a draw, nobody wins
        stats(0, 500, 5, 5, 5);
        stats(1, 500, 5, 5, 5);
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(r.draw && !r.rows[0].winner && !r.rows[1].winner && !r.seats[0].winner && !r.seats[1].winner);
        // the same score but one number apart: not a draw (the game ranks by score and breaks a tie for the one who looks at the screen: here nobody does, so the lower seat is first)
        stats(1, 500, 6, 5, 5);
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(!r.draw && r.rows[0].seats == std::vector<uint8_t>{0} && r.rows[0].winner && r.seats[0].winner && !r.seats[1].winner);
        // Bob quits: his row is last whatever he scored, he is marked left, and the match is no draw even when the numbers are equal
        out.result.quitter = 1;
        stats(0, 100, 1, 9, 1);
        stats(1, 900, 9, 1, 4);
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(r.quitter == 1 && !r.draw && r.rows[0].seats == std::vector<uint8_t>{0} && r.rows[1].seats == std::vector<uint8_t>{1});
        ASSERT_TRUE(r.seats[0].winner && !r.seats[0].left && r.seats[1].left && !r.seats[1].winner);
        stats(0, 500, 5, 5, 5);
        stats(1, 500, 5, 5, 5);
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(r.quitter == 1 && !r.draw && r.seats[0].winner);
        // a match that the time ended (the rules did not): not finished
        out.result.quitter = sim::NO_QUITTER;
        out.match_over = false;
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(!r.finished && r.quitter == -1);
    } TEST_END();

    TEST_CASE("HS1.3 The Format Is How The Match Began: Two Seats 1v1, Three Or Four Free For All, A Pair And A Pair 2v2, A Pair And A Seat 2v1; Teams That The Roster Cannot Make Are Free For All; A Seat That Dropped Is Marked Left And Has No Row") {
        replay::Replay rep;
        replay::Outcome out;
        out.ran = out.ok = true;
        out.turns = 700;
        out.match_over = true;
        out.result.is_over = true;
        const auto make = [&](uint8_t roster, sim::StartTeams teams, uint8_t dropped) {
            rep.head.roster = roster;
            rep.head.teams = teams;
            out.dropped_mask = dropped;
            out.result.present_mask = static_cast<uint8_t>(roster & ~dropped & 0x0Fu);
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                out.result.stats[seat] = sim::PlayerMatchStats{};
                out.result.stats[seat].score = 100 * (4 - seat);
                out.result.ally[seat] = sim::ALLIANCE_NONE;
            }
            if (teams.set) {                                               // (the engine's alliances after the start's invitations: a pair, and the other two)
                out.result.ally[teams.a] = teams.b;
                out.result.ally[teams.b] = teams.a;
                uint8_t others[2];
                size_t n = 0;
                for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                    if (((roster >> seat) & 1u) != 0 && seat != teams.a && seat != teams.b && n < 2) others[n++] = seat;
                }
                if (n == 2) {
                    out.result.ally[others[0]] = others[1];
                    out.result.ally[others[1]] = others[0];
                }
            }
            return make_history_record(id_at(kT0), kT0, rep, out);
        };
        ASSERT_EQ(make(0x03, sim::StartTeams{}, 0).format, std::string("1v1"));
        ASSERT_EQ(make(0x05, sim::StartTeams{}, 0).format, std::string("1v1"));
        ASSERT_EQ(make(0x07, sim::StartTeams{}, 0).format, std::string("ffa3"));
        ASSERT_EQ(make(0x0F, sim::StartTeams{}, 0).format, std::string("ffa4"));
        HistoryRecord pair = make(0x0F, sim::StartTeams{true, 0, 1}, 0);
        ASSERT_EQ(pair.format, std::string("2v2"));
        ASSERT_TRUE(pair.rows.size() == 2 && pair.rows[0].seats.size() == 2 && pair.rows[1].seats.size() == 2);
        ASSERT_TRUE(pair.rows[0].seats == (std::vector<uint8_t>{0, 1}) && pair.rows[0].winner && pair.rows[0].score == 700 && pair.rows[1].seats == (std::vector<uint8_t>{2, 3}));
        ASSERT_TRUE(pair.seats[0].winner && pair.seats[1].winner && !pair.seats[2].winner && !pair.seats[3].winner);          // (a team wins together)
        ASSERT_EQ(make(0x07, sim::StartTeams{true, 0, 1}, 0).format, std::string("2v1"));
        ASSERT_EQ(make(0x03, sim::StartTeams{true, 0, 1}, 0).format, std::string("1v1"));                                      // (the pair would be the whole match: the room makes no team)
        ASSERT_EQ(make(0x0F, sim::StartTeams{true, 0, 7}, 0).format.empty(), false);                                           // (a pair that is not a pair: still a format, never empty)
        // seat 2 dropped out before the end: it has no row, it is marked left, and the others are ranked without it
        HistoryRecord dropped = make(0x0F, sim::StartTeams{}, 0x04);
        ASSERT_TRUE(dropped.rows.size() == 3 && dropped.seats.size() == 4 && dropped.seats[2].left && !dropped.seats[0].left && !dropped.seats[1].left && !dropped.seats[3].left);
        for (const HistoryRow& row : dropped.rows) ASSERT_TRUE(row.seats[0] != 2);
        ASSERT_EQ(dropped.format, std::string("ffa4"));
    } TEST_END();

    TEST_CASE("HS1.4 Computers: \"Bot (Hard)\" Is A Bot Of Level Hard, \"Bot\" A Bot With No Level, Anything Else Is A Person's Name; A Match With No Person Is computers_only; An Empty Name Stays Empty (The Page Shows The Colour)") {
        replay::Replay rep;
        rep.head.roster = 0x0F;
        replay::Outcome out;
        out.ran = out.ok = true;
        out.turns = 700;
        out.match_over = true;
        out.result.is_over = true;
        rep.head.names = {"Bot (Hard)", "Bot", "  Bot (Easy) ", "Bot (Medium)"};
        HistoryRecord r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(r.computers_only && r.seats[0].bot == "Hard" && r.seats[1].bot == "Bot" && r.seats[2].bot == "Easy" && r.seats[3].bot == "Medium");
        for (const HistorySeat& s : r.seats) ASSERT_TRUE(s.name.empty());
        rep.head.names = {"Bot (Hard)", "Ann", "", "Robot"};
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(!r.computers_only && r.seats[0].bot == "Hard" && r.seats[0].name.empty());
        ASSERT_TRUE(r.seats[1].name == "Ann" && r.seats[1].bot.empty() && r.seats[2].name.empty() && r.seats[2].bot.empty() && r.seats[3].name == "Robot" && r.seats[3].bot.empty());
        rep.head.names = {"Bot (", "Bot ()", "Bot(Hard)", "bot"};
        r = make_history_record(id_at(kT0), kT0, rep, out);
        ASSERT_TRUE(!r.computers_only && r.seats[0].name == "Bot (" && r.seats[1].name == "Bot ()" && r.seats[2].name == "Bot(Hard)" && r.seats[3].name == "bot");          // (no level, no bot: only \"Bot (X)\" and \"Bot\" are)
    } TEST_END();

    TEST_CASE("HS1.5 A Match That Is Played Again With The Engine Gives The Numbers Of The Engine's Own Result Seat By Seat, The Rows Add Up Teams, And Exactly One Row Wins Unless It Is A Draw") {
        uint32_t total_hatched = 0;
        uint32_t total_killed = 0;
        uint32_t total_lost = 0;
        for (int k = 0; k < 4; ++k) {
            const std::vector<uint8_t>& bytes = match_file(k);
            ASSERT_FALSE(bytes.empty());
            const Played p = play_file(bytes);
            ASSERT_TRUE(p.ok);
            const HistoryRecord r = make_history_record(id_at(kT0 + k), kT0 + k, p.rep, p.outcome);
            {
                // an independent count: the Hatch commands that the engine started an egg for, seat by seat, are the ants that the history says were hatched
                std::array<uint32_t, sim::MAX_PLAYERS> started{};
                replay::Hooks hooks;
                hooks.after_command = [&](uint32_t, const sim::Command& c, const sim::CommandResult& res, sim::SimulationEngine&) {
                    if (c.type == sim::CommandType::Hatch && res.accepted() && res.hatch_result == static_cast<uint8_t>(sim::SimulationEngine::HatchResult::Started) && c.issuer < sim::MAX_PLAYERS) ++started[c.issuer];
                };
                assets::LevelData level;
                std::string why;
                ASSERT_TRUE(replay::load_map(p.rep.head, maps_dir(), level, why));
                ASSERT_TRUE(replay::play(p.rep, level, hooks).ok);
                for (const HistorySeat& st : r.seats) ASSERT_EQ(st.hatched, started[st.seat]);
            }
            for (const HistorySeat& st : r.seats) {
                total_hatched += st.hatched;
                total_killed += st.killed;
                total_lost += st.lost;
            }
            ASSERT_TRUE(r.turns == p.outcome.turns && r.finished == p.outcome.match_over && r.game == "v0.12.0-test" && r.map == "TINY.LVL");
            size_t playing = 0;
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) playing += ((p.rep.head.roster >> seat) & 1u) != 0 ? 1u : 0u;
            ASSERT_EQ(r.seats.size(), playing);
            for (const HistorySeat& s : r.seats) {
                const sim::PlayerMatchStats& st = p.outcome.result.stats[s.seat];
                ASSERT_TRUE(s.score == st.score && s.killed == st.enemy_killed && s.lost == st.friendly_lost && s.hatched == st.new_hatched);
                ASSERT_TRUE(s.bombs_planted == st.bombs_planted && s.bombs_defused == st.bombs_defused && s.fires_lit == st.fires_lit);
                if (k != 3) ASSERT_EQ(s.score, p.outcome.scores[s.seat]);                  // (in a free for all the seat's own score is the score box that the screen shows)
            }
            size_t winners_rows = 0;
            for (const HistoryRow& row : r.rows) winners_rows += row.winner ? 1u : 0u;
            ASSERT_EQ(winners_rows, r.draw ? size_t{0} : size_t{1});
            if (!r.draw) {
                for (const HistorySeat& s : r.seats) {
                    bool in_first = false;
                    for (const uint8_t member : r.rows[0].seats) in_first = in_first || member == s.seat;
                    ASSERT_EQ(s.winner, in_first);
                }
            }
            if (k == 0) {
                ASSERT_TRUE(r.format == "ffa4" && r.rows.size() == 4 && !r.computers_only && r.seats[0].name == "Ann" && r.seats[2].bot == "Hard" && r.seats[3].bot == "Bot");
            }
            if (k == 3) {
                ASSERT_TRUE(r.format == "2v2" && r.rows.size() == 2 && r.rows[0].seats.size() == 2);
                int32_t row_score = 0;
                for (const uint8_t member : r.rows[0].seats) row_score += p.outcome.result.stats[member].score;
                ASSERT_EQ(r.rows[0].score, row_score);
            }
        }
        ASSERT_TRUE(total_hatched > 0 && total_killed > 0 && total_lost > 0);            // (the matches have something to count: else every number above is 0 == 0)
        // the match where the second seat quits: his quit ended it, he is left and last
        const Played quit = play_file(match_file(2));
        ASSERT_TRUE(quit.ok && quit.outcome.match_over);
        const HistoryRecord r = make_history_record(id_at(kT0), kT0, quit.rep, quit.outcome);
        ASSERT_TRUE(r.finished && r.quitter == 1 && !r.draw && r.format == "1v1");
        ASSERT_TRUE(r.seats[1].left && !r.seats[0].left && r.seats[0].winner && !r.seats[1].winner && r.rows.back().seats == std::vector<uint8_t>{1});
    } TEST_END();

    // ---- the JSON of a record ------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS2.1 A Record Written As JSON Is Read Back The Same (Teams In One Row, A Quitter, A Draw), The List's Form Leaves Out The Numbers Of The Page And Keeps The Rest") {
        std::vector<HistoryRecord> all = six_records();
        HistoryRecord team = record_of(kT0 + 600, "TINY.LVL", "2v2", 3000, true, -1, false, {seat_of(0, "A", "", 600, 6, 6, 6), seat_of(1, "B", "", 500, 5, 7, 3), seat_of(2, "", "Hard", 300, 3, 9, 2), seat_of(3, "D", "", 200, 2, 10, 1)});
        team.rows.clear();                                                 // (two rows of a pair each, as the game builds them)
        team.rows.push_back(HistoryRow{{0, 1}, 1100, 11, 13, 9, true});
        team.rows.push_back(HistoryRow{{2, 3}, 500, 5, 19, 3, false});
        team.seats[0].winner = team.seats[1].winner = true;
        all.push_back(team);
        for (const HistoryRecord& r : all) {
            const std::string text = ctl::to_json(history_record_to_json(r, true));
            HistoryRecord back;
            std::string why;
            ASSERT_TRUE(history_record_from_json(parse(text), back, why));
            ASSERT_TRUE(same_record(r, back));
        }
        const HistoryRecord& q = all[4];                                   // the one with a quitter
        const ctl::JsonValue full = history_record_to_json(q, true);
        ASSERT_TRUE(full.get("v").as_int_or(0) == kHistoryVersion && full.get("quitter").as_int_or(-1) == 1 && full.get("seats").at(0).has("bombs_planted") && full.get("seconds").as_int_or(0) == 40);
        ASSERT_TRUE(full.get("seats").at(1).get("left").as_bool_or(false) && full.get("seats").at(0).get("winner").as_bool_or(false) && full.get("seats").at(0).get("colour").as_string_or() == "Green");
        const ctl::JsonValue lean = history_record_to_json(q, false);
        ASSERT_FALSE(lean.has("v"));
        ASSERT_TRUE(lean.has("id") && lean.has("seconds") && lean.has("rows") && lean.get("seats").at(0).has("killed") && lean.get("seats").at(0).has("hatched"));
        ASSERT_FALSE(lean.get("seats").at(0).has("bombs_planted") || lean.get("seats").at(0).has("fires_lit"));
        const ctl::JsonValue none = history_record_to_json(all[0], false);
        ASSERT_TRUE(none.has("quitter") && none.get("quitter").is_null());      // (no quitter: null, never a seat number)
        ASSERT_EQ(history_record_to_json(all[0], true).get("seats").at(1).get("colour").as_string_or(), std::string("Red"));
    } TEST_END();

    TEST_CASE("HS2.2 The Reader Skips A Key It Does Not Know (A Later Version's \"details\" Is Safe To Read), And Refuses What Is Not A Record With A Reason: Not An Object, No Layout Number, An Id That Is Not A Match, A Key Of The Wrong Kind, No Seat, Five Seats, A Row Without A Seat") {
        const HistoryRecord r = six_records()[0];
        ctl::JsonValue j = history_record_to_json(r, true);
        j.set("details", ctl::JsonValue::make_object());
        j.set("future", ctl::JsonValue::make_int(5));
        HistoryRecord back;
        std::string why;
        ASSERT_TRUE(history_record_from_json(j, back, why) && same_record(r, back));
        const auto refused = [&](const std::function<void(ctl::JsonValue&)>& spoil, const std::string& reason_part) {
            ctl::JsonValue k = history_record_to_json(r, true);
            spoil(k);
            HistoryRecord out;
            std::string reason;
            return !history_record_from_json(k, out, reason) && contains(reason, reason_part);
        };
        ASSERT_FALSE(history_record_from_json(ctl::JsonValue::make_array(), back, why));
        ASSERT_TRUE(contains(why, "object"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) { k.set("v", ctl::JsonValue::make_int(0)); }, "layout"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) { k.set("id", ctl::JsonValue::make_string("../x")); }, "id"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) { k.set("turns", ctl::JsonValue::make_string("many")); }, "missing"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) { k.set("seats", ctl::JsonValue::make_array()); }, "seats"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) {
            ctl::JsonValue seats = ctl::JsonValue::make_array();
            for (int i = 0; i < 5; ++i) seats.push_back(ctl::JsonValue::make_object());
            k.set("seats", std::move(seats));
        }, "seats"));
        ASSERT_TRUE(refused([](ctl::JsonValue& k) {
            ctl::JsonValue rows = ctl::JsonValue::make_array();
            ctl::JsonValue row = ctl::JsonValue::make_object();
            row.set("seats", ctl::JsonValue::make_array());
            rows.push_back(std::move(row));
            k.set("rows", std::move(rows));
        }, "row"));
        // a number outside what a count can be is clamped, not wrapped
        ctl::JsonValue wild = history_record_to_json(r, true);
        wild.set("turns", ctl::JsonValue::make_int(-5));
        ASSERT_TRUE(history_record_from_json(wild, back, why) && back.turns == 0);
        wild.set("turns", ctl::JsonValue::make_int(int64_t{1} << 40));
        ASSERT_TRUE(history_record_from_json(wild, back, why) && back.turns == 0xFFFFFFFFu);
    } TEST_END();

    // ---- the store on a folder -----------------------------------------------------------------------------------------------------------

    TEST_CASE("HS3.1 A Record Is Written Whole Under Its Id, Nothing Is Left Behind, The Records Are Kept Oldest First Whatever The Order They Come In, A Record Of The Same Id Is Replaced, And A New Store Reads Them All Again") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        HistoryStore store(cfg);
        std::string why;
        ASSERT_FALSE(store.enabled());
        ASSERT_FALSE(store.put(six_records()[0], why));
        ASSERT_TRUE(contains(why, "keeps no match history"));
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(store.enabled() && fs::is_directory(cfg.dir) && store.count() == 0 && names_in(cfg.dir).empty());
        ASSERT_EQ(lines_with(store.take_notes(), "0 match(es) kept for good"), size_t{1});
        const std::vector<HistoryRecord> all = six_records();
        for (const size_t i : {size_t{2}, size_t{0}, size_t{1}}) ASSERT_TRUE(store.put(all[i], why));
        ASSERT_EQ(store.count(), size_t{3});
        ASSERT_EQ(names_in(cfg.dir), (std::vector<std::string>{all[0].id + ".json", all[1].id + ".json", all[2].id + ".json"}));          // (no ".tmp")
        ASSERT_TRUE(store.records()[0].id == all[0].id && store.records()[1].id == all[1].id && store.records()[2].id == all[2].id);
        const std::string text = slurp_text(fs::path(cfg.dir) / (all[0].id + ".json"));
        ASSERT_TRUE(!text.empty() && text.back() == '\n' && parse(text).get("id").as_string_or() == all[0].id && parse(text).get("v").as_int_or(0) == 1);
        // the same id again: replaced, not added
        HistoryRecord changed = all[0];
        changed.seats[0].score = 1234;
        ASSERT_TRUE(store.put(changed, why));
        ASSERT_TRUE(store.count() == 3 && store.find(all[0].id)->seats[0].score == 1234);
        ASSERT_TRUE(store.find("ants-TINY-20200101-000000Z") == nullptr && store.find("../x") == nullptr && store.find("") == nullptr);
        // a record whose id is not a match is refused, nothing is written
        HistoryRecord odd = all[0];
        odd.id = "not a match";
        ASSERT_FALSE(store.put(odd, why));
        ASSERT_TRUE(contains(why, "id") && names_in(cfg.dir).size() == 3);
        // the text of a record's file
        std::string file_text;
        ASSERT_TRUE(store.read_file(all[1].id, file_text) && file_text == slurp_text(fs::path(cfg.dir) / (all[1].id + ".json")));
        ASSERT_FALSE(store.read_file("ants-TINY-20200101-000000Z", file_text));
        ASSERT_TRUE(file_text.empty());
        // a new store (the server starts again) has them all, in the same order, with the same numbers
        HistoryStore again(cfg);
        ASSERT_TRUE(again.prepare(why));
        ASSERT_TRUE(again.count() == 3 && again.marker_count() == 0);
        ASSERT_TRUE(same_record(again.records()[0], changed) && same_record(again.records()[1], all[1]) && same_record(again.records()[2], all[2]));
        ASSERT_EQ(lines_with(again.take_notes(), "3 match(es) kept for good"), size_t{1});
    } TEST_END();

    TEST_CASE("HS3.2 The Start Deletes The Half-Written Files Of A Crash And Nothing Else; A File That Is No Record Is Left Where It Is, Counted, And The First Few Are Named In The Log; Other Files, Folders And Names That Are No Id Are Not Touched") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        fs::create_directories(cfg.dir);
        const fs::path dir = cfg.dir;
        const std::vector<HistoryRecord> all = six_records();
        write_text(dir / (all[0].id + ".json"), ctl::to_json(history_record_to_json(all[0], true)));          // a good record (no newline at its end: fine)
        write_text(dir / (all[1].id + ".json.tmp"), "{\"v\":1,");                                          // the write of a crash
        write_text(dir / "notes.txt", "private notes");
        write_text(dir / "README.json.tmp", "x");                                                         // (not an id: not ours to delete)
        write_text(dir / "ants-TINY-20200101-000000Z.txt", "x");
        fs::create_directories(dir / (all[2].id + ".json"));                                              // (a folder of that name is no file of the store)
        write_text(dir / (all[3].id + ".json"), "{\"v\":1,\"id\":");                                      // cut short: a power cut
        write_text(dir / (all[4].id + ".json"), ctl::to_json(history_record_to_json(all[5], true)));       // a record of another id
        for (int i = 0; i < 8; ++i) write_text(dir / (id_at(kT0 - 1000 - i * 10) + ".json"), "not json");      // (eight more that are not records)
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_EQ(store.count(), size_t{1});
        ASSERT_TRUE(store.find(all[0].id) != nullptr);
        ASSERT_FALSE(fs::exists(dir / (all[1].id + ".json.tmp")));
        ASSERT_TRUE(fs::exists(dir / "notes.txt") && fs::exists(dir / "README.json.tmp") && fs::exists(dir / "ants-TINY-20200101-000000Z.txt") && fs::is_directory(dir / (all[2].id + ".json")));
        ASSERT_TRUE(fs::exists(dir / (all[3].id + ".json")) && fs::exists(dir / (all[4].id + ".json")));         // (never deleted: the owner looks at them)
        const std::vector<std::string> notes = store.take_notes();
        ASSERT_EQ(lines_with(notes, " is not read: ") + lines_with(notes, " is not a record: "), size_t{5});      // (the first five are named)
        ASSERT_EQ(lines_with(notes, "10 file(s) that are no records were left where they are"), size_t{1});
        ASSERT_EQ(lines_with(notes, "1 half-written file(s) of an interrupted write deleted"), size_t{1});
        // the folder cannot be made (a file is in the way), or no folder was given
        write_text(temp.path / "plain", "x");
        HistoryConfig in_the_way;
        in_the_way.dir = (temp.path / "plain").string();
        HistoryStore blocked(in_the_way);
        ASSERT_FALSE(blocked.prepare(why));
        ASSERT_TRUE(contains(why, "history folder") && !blocked.enabled());
        HistoryStore none(HistoryConfig{});
        ASSERT_FALSE(none.prepare(why));
        ASSERT_FALSE(none.enabled());
    } TEST_END();

    TEST_CASE("HS3.3 A Rename That Fails Leaves The Store As It Was: No File, No Temporary File, The Reason Is Told And Logged, And The Next Record Is Written When The Disk Works Again") {
        TempDir temp;
        bool fail = true;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        cfg.rename_file = [&](const std::string& from, const std::string& to, std::error_code& ec) {
            if (fail) {
                ec = std::make_error_code(std::errc::permission_denied);
                return;
            }
            fs::rename(from, to, ec);
        };
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        store.take_notes();
        const std::vector<HistoryRecord> all = six_records();
        ASSERT_FALSE(store.put(all[0], why));
        ASSERT_TRUE(contains(why, "could not be given its name"));
        ASSERT_TRUE(store.count() == 0 && names_in(cfg.dir).empty());
        ASSERT_EQ(lines_with(store.take_notes(), all[0].id + " could not be kept"), size_t{1});
        fail = false;
        ASSERT_TRUE(store.put(all[0], why));
        ASSERT_TRUE(store.count() == 1 && names_in(cfg.dir) == std::vector<std::string>{all[0].id + ".json"});
        // a record that is replaced and whose rename fails keeps the old file and the old numbers
        fail = true;
        HistoryRecord changed = all[0];
        changed.seats[0].score = 4321;
        ASSERT_FALSE(store.put(changed, why));
        ASSERT_TRUE(store.find(all[0].id)->seats[0].score == 900 && parse(slurp_text(fs::path(cfg.dir) / (all[0].id + ".json"))).get("seats").at(0).get("score").as_int_or(0) == 900);
        ASSERT_EQ(names_in(cfg.dir).size(), size_t{1});
    } TEST_END();

    TEST_CASE("HS3.4 The Owner Takes A Match Out: With Its Recording Still There The File Becomes A Marker (The Match Is Not Counted Again), Without It The File Is Deleted; A Marker Survives A Restart, Goes When The Match Is Put Again Or Its Recording Is Gone, And A Failed Rename Takes Nothing Out") {
        TempDir temp;
        bool fail = false;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        cfg.rename_file = [&](const std::string& from, const std::string& to, std::error_code& ec) {
            if (fail) {
                ec = std::make_error_code(std::errc::permission_denied);
                return;
            }
            fs::rename(from, to, ec);
        };
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<HistoryRecord> all = six_records();
        for (size_t i = 0; i < 3; ++i) ASSERT_TRUE(store.put(all[i], why));
        ASSERT_FALSE(store.remove("ants-TINY-20200101-000000Z", true, why));
        ASSERT_FALSE(store.remove("../x", false, why));
        fail = true;
        ASSERT_FALSE(store.remove(all[0].id, true, why));                 // (the marker cannot be written: the record stays)
        ASSERT_TRUE(store.find(all[0].id) != nullptr && store.marker_count() == 0 && parse(slurp_text(fs::path(cfg.dir) / (all[0].id + ".json"))).get("seats").size() == 2);
        fail = false;
        ASSERT_TRUE(store.remove(all[0].id, true, why));
        ASSERT_TRUE(store.find(all[0].id) == nullptr && store.knows(all[0].id) && store.marker_count() == 1 && store.count() == 2);
        const ctl::JsonValue marker = parse(slurp_text(fs::path(cfg.dir) / (all[0].id + ".json")));
        ASSERT_TRUE(marker.get("deleted").as_bool_or(false) && marker.get("id").as_string_or() == all[0].id && !marker.has("seats"));
        ASSERT_TRUE(store.remove(all[1].id, false, why));
        ASSERT_TRUE(!store.knows(all[1].id) && !fs::exists(fs::path(cfg.dir) / (all[1].id + ".json")) && store.count() == 1);
        // a restart: the marker is read as a marker
        HistoryStore again(cfg);
        ASSERT_TRUE(again.prepare(why));
        ASSERT_TRUE(again.count() == 1 && again.marker_count() == 1 && again.knows(all[0].id) && again.find(all[0].id) == nullptr);
        ASSERT_EQ(lines_with(again.take_notes(), "1 taken out by the owner"), size_t{1});
        // the marker goes when the recording does (the caller says which ids it still needs), and not before
        ASSERT_EQ(again.prune_markers([&](const std::string& id) { return id == all[0].id; }), size_t{0});
        ASSERT_TRUE(again.marker_count() == 1 && fs::exists(fs::path(cfg.dir) / (all[0].id + ".json")));
        ASSERT_EQ(again.prune_markers([](const std::string&) { return false; }), size_t{1});
        ASSERT_TRUE(again.marker_count() == 0 && !again.knows(all[0].id) && !fs::exists(fs::path(cfg.dir) / (all[0].id + ".json")));
        // a match that is put again after the owner took it out is a record again, not a marker
        ASSERT_TRUE(again.remove(all[2].id, true, why));
        ASSERT_TRUE(again.put(all[2], why));
        ASSERT_TRUE(again.marker_count() == 0 && again.find(all[2].id) != nullptr && parse(slurp_text(fs::path(cfg.dir) / (all[2].id + ".json"))).has("seats"));
    } TEST_END();

    TEST_CASE("HS3.5 A Year Of Matches Is Read At The Start And Kept Sorted: Two Thousand Records, The Ones That Ended In One Second Told Apart By Their \"-n\", A Page From The Middle Of The List") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        {
            HistoryStore store(cfg);
            std::string why;
            ASSERT_TRUE(store.prepare(why));
            const std::vector<HistoryRecord> all = six_records();
            for (int i = 0; i < 2000; ++i) {
                HistoryRecord r = all[static_cast<size_t>(i) % all.size()];
                const int64_t t = kT0 + (1999 - i) * 20 / 2;               // (newest = smallest i: they are written newest first; every second time two of them end together)
                r.ended_s = t;
                r.id = id_at(t, "TINY", i % 2 == 0 ? 1 : 2);
                r.seats[0].score = i;
                ASSERT_TRUE(store.put(r, why));
            }
            ASSERT_EQ(store.count(), size_t{2000});
        }
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_EQ(store.count(), size_t{2000});
        const std::vector<HistoryRecord>& records = store.records();
        for (size_t i = 1; i < records.size(); ++i) {
            ASSERT_TRUE(records[i - 1].ended_s < records[i].ended_s || (records[i - 1].ended_s == records[i].ended_s && records[i - 1].id < records[i].id));
        }
        HistoryQuery q;
        q.offset = 1950;
        q.limit = 100;
        const HistoryPage page = store.query(q, nullptr);
        ASSERT_TRUE(page.count == 2000 && page.items.size() == 50);
        ASSERT_TRUE(page.items.front() == &records[49] && page.items.back() == &records[0]);                 // (newest first: the end of the page is the oldest record)
    } TEST_END();

    // ---- the list: filters, sorts, pages --------------------------------------------------------------------------------------------------

    TEST_CASE("HS4.1 The List Is Filtered By Format Result Who Colour And Text, All Together, Newest First: 1v1 Team And Free For All, Decided Draw And Cut (A Player Left Or The Time Ran Out), People And Computers, A Seat That Took Part, Part Of A Map Or Of A Name In Any Case") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<HistoryRecord> all = six_records();
        for (const HistoryRecord& r : all) ASSERT_TRUE(store.put(r, why));
        const auto ask = [&](const std::function<void(HistoryQuery&)>& set) {
            HistoryQuery q;
            set(q);
            return ids_of(store.query(q, nullptr));
        };
        ASSERT_EQ(ask([](HistoryQuery&) {}), ids_at(all, {5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.format = HistoryQuery::Format::OneVsOne; }), ids_at(all, {4, 3, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.format = HistoryQuery::Format::Teams; }), ids_at(all, {2}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.format = HistoryQuery::Format::Free; }), ids_at(all, {5, 1}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.result = HistoryQuery::Result::Decided; }), ids_at(all, {2, 1, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.result = HistoryQuery::Result::Draw; }), ids_at(all, {3}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.result = HistoryQuery::Result::Cut; }), ids_at(all, {5, 4}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.who = HistoryQuery::Who::People; }), ids_at(all, {5, 4, 2, 1, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.who = HistoryQuery::Who::Computers; }), ids_at(all, {3}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.colour = 0; }), ids_at(all, {5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.colour = 2; }), ids_at(all, {5, 2, 1}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.colour = 3; }), ids_at(all, {2, 1}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "ann"; }), ids_at(all, {0}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "EVE"; }), ids_at(all, {2}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "treasure"; }), ids_at(all, {4, 2}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "bot (hard)"; }), ids_at(all, {1}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "Bot (Medium)"; }), ids_at(all, {3}));
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "red"; }), ids_at(all, {1}));                         // (a seat with no typed name is its colour)
        ASSERT_EQ(ask([](HistoryQuery& q) { q.text = "nobody called this"; }), std::vector<std::string>());
        ASSERT_EQ(ask([](HistoryQuery& q) {
            q.format = HistoryQuery::Format::OneVsOne;
            q.who = HistoryQuery::Who::People;
        }), ids_at(all, {4, 0}));
        ASSERT_EQ(ask([](HistoryQuery& q) {
            q.format = HistoryQuery::Format::OneVsOne;
            q.result = HistoryQuery::Result::Cut;
            q.text = "hal";
        }), ids_at(all, {4}));
        // the count is what fits, the page is a part of it
        HistoryQuery q;
        q.format = HistoryQuery::Format::OneVsOne;
        q.limit = 2;
        q.offset = 1;
        HistoryPage page = store.query(q, nullptr);
        ASSERT_TRUE(page.count == 3 && ids_of(page) == ids_at(all, {3, 0}));
        q.offset = 3;
        page = store.query(q, nullptr);
        ASSERT_TRUE(page.count == 3 && page.items.empty());
        q.offset = 99999;
        ASSERT_TRUE(store.query(q, nullptr).items.empty());
        q.offset = 0;
        q.limit = 0;
        ASSERT_TRUE(store.query(q, nullptr).items.empty());
    } TEST_END();

    TEST_CASE("HS4.2 The List Is Sorted By New Old Score Kills Hatched Long Or Short; A Tie Keeps The Newer Match First; The Score Of A Match Is Its Best Seat's") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<HistoryRecord> all = six_records();
        for (const HistoryRecord& r : all) ASSERT_TRUE(store.put(r, why));
        const auto sorted = [&](HistoryQuery::Sort sort) {
            HistoryQuery q;
            q.sort = sort;
            return ids_of(store.query(q, nullptr));
        };
        ASSERT_EQ(sorted(HistoryQuery::Sort::New), ids_at(all, {5, 4, 3, 2, 1, 0}));
        ASSERT_EQ(sorted(HistoryQuery::Sort::Old), ids_at(all, {0, 1, 2, 3, 4, 5}));
        ASSERT_EQ(sorted(HistoryQuery::Sort::Score), ids_at(all, {0, 4, 1, 2, 3, 5}));           // 900 800 700 600 500 250
        ASSERT_EQ(sorted(HistoryQuery::Sort::Kills), ids_at(all, {0, 4, 1, 2, 3, 5}));           // 10 9 8 6 5 4
        ASSERT_EQ(sorted(HistoryQuery::Sort::Hatched), ids_at(all, {2, 1, 3, 0, 4, 5}));         // 6 6 5 5 4 3: the newer of two that are equal first
        ASSERT_EQ(sorted(HistoryQuery::Sort::Long), ids_at(all, {5, 1, 2, 3, 0, 4}));            // 36000 6000 3000 2400 1200 800 turns
        ASSERT_EQ(sorted(HistoryQuery::Sort::Short), ids_at(all, {4, 0, 3, 2, 1, 5}));
        // a page of a sorted list
        HistoryQuery q;
        q.sort = HistoryQuery::Sort::Score;
        q.limit = 2;
        q.offset = 2;
        ASSERT_EQ(ids_of(store.query(q, nullptr)), ids_at(all, {1, 2}));
    } TEST_END();

    TEST_CASE("HS4.3 The Recording's State Is Asked Only When The List Filters By It (Never For A Record Twice): Watch, Old (Kept But Of Other Rules) And Removed") {
        TempDir temp;
        HistoryConfig cfg;
        cfg.dir = (temp.path / "history").string();
        HistoryStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const std::vector<HistoryRecord> all = six_records();
        for (const HistoryRecord& r : all) ASSERT_TRUE(store.put(r, why));
        std::map<std::string, int> asked;
        const auto state = [&](const std::string& id) {
            ++asked[id];
            if (id == all[0].id || id == all[2].id || id == all[4].id) return Recording::Watch;
            return id == all[1].id ? Recording::Old : Recording::Removed;
        };
        HistoryQuery q;
        ASSERT_EQ(ids_of(store.query(q, state)), ids_at(all, {5, 4, 3, 2, 1, 0}));
        ASSERT_TRUE(asked.empty());
        q.rec = HistoryQuery::Rec::Watch;
        ASSERT_EQ(ids_of(store.query(q, state)), ids_at(all, {4, 2, 0}));
        q.rec = HistoryQuery::Rec::Old;
        ASSERT_EQ(ids_of(store.query(q, state)), ids_at(all, {1}));
        q.rec = HistoryQuery::Rec::Removed;
        ASSERT_EQ(ids_of(store.query(q, state)), ids_at(all, {5, 3}));
        for (const auto& kv : asked) ASSERT_EQ(kv.second, 3);               // (once for each of the three lists)
        q.rec = HistoryQuery::Rec::Watch;
        q.format = HistoryQuery::Format::Teams;                              // (a record that another filter turned down is not asked about)
        asked.clear();
        ASSERT_EQ(ids_of(store.query(q, state)), ids_at(all, {2}));
        ASSERT_EQ(asked.size(), size_t{1});
        ASSERT_TRUE(store.query(q, nullptr).items.empty());                  // (no way to ask: the recording is gone)
    } TEST_END();

    // ---- the feeder: the matches of the replay store, counted one at a time --------------------------------------------------------------

    TEST_CASE("HS5.1 The Matches That The Replay Store Holds Are Played Again One At A Time, The Newest First, Never Faster Than The Pace, Only While The Server Is Quiet; Each Becomes A Record With The Numbers Of Its Own Replay; The Log Says What Came Of It Once") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        std::vector<std::string> ids;
        for (int k = 0; k < 4; ++k) ids.push_back(rig.keep(match_file(k)));
        for (const std::string& id : ids) ASSERT_FALSE(id.empty());
        ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.pending() == 0);
        // somebody plays: nothing is done, and nothing is lost
        for (int i = 0; i < 5; ++i) ASSERT_FALSE(rig.step(false));
        ASSERT_TRUE(rig.history.count() == 0 && rig.idle_asked == 5);
        // quiet: the newest first, one at a time
        ASSERT_TRUE(rig.step());
        ASSERT_TRUE(rig.history.count() == 1 && rig.history.find(ids[3]) != nullptr && rig.feeder.counted() == 1 && rig.feeder.pending() == 3);
        const int asked = rig.idle_asked;
        rig.ms += 100;                                                      // (too soon: the pace is 250 ms; the question about the server is not even asked)
        ASSERT_FALSE(rig.feeder.step([&]() {
            ++rig.idle_asked;
            return true;
        }, rig.ms));
        ASSERT_TRUE(rig.history.count() == 1 && rig.idle_asked == asked);
        ASSERT_TRUE(rig.step() && rig.history.count() == 2 && rig.history.find(ids[2]) != nullptr);
        ASSERT_TRUE(rig.step() && rig.history.count() == 3 && rig.step() && rig.history.count() == 4);
        ASSERT_TRUE(rig.history.find(ids[0]) != nullptr && rig.feeder.pending() == 0);
        // the numbers are those of the replay played again
        for (int k = 0; k < 4; ++k) {
            const Played p = play_file(match_file(k));
            ASSERT_TRUE(p.ok);
            const ReplayEntry* entry = rig.replays.find(ids[static_cast<size_t>(k)] + ".antsrep");
            ASSERT_TRUE(entry != nullptr);
            ASSERT_TRUE(same_record(*rig.history.find(ids[static_cast<size_t>(k)]), make_history_record(ids[static_cast<size_t>(k)], entry->ended_s, p.rep, p.outcome)));
        }
        ASSERT_EQ(rig.history.find(ids[0])->ended_s, kT0 + 10);
        // when the work is done one line says so; after that a quiet server is not asked anything
        const int before = rig.idle_asked;
        ASSERT_FALSE(rig.step());
        ASSERT_EQ(lines_with(rig.history.take_notes(), "4 match(es) counted, 0 left out; 4 kept in all"), size_t{1});
        const int after_summary = rig.idle_asked;
        ASSERT_TRUE(after_summary == before + 1);
        for (int i = 0; i < 10; ++i) ASSERT_FALSE(rig.step());
        ASSERT_EQ(rig.idle_asked, after_summary);
        ASSERT_EQ(names_in(rig.history_dir).size(), size_t{4});
        // the server starts again: what is in the history is not counted again
        HistoryStore history2(rig.history.config());
        std::string why;
        ASSERT_TRUE(history2.prepare(why) && history2.count() == 4);
        HistoryFeeder feeder2(history2, rig.replays, maps_dir());
        uint32_t ms = 5000;
        for (int i = 0; i < 6; ++i) {
            ms += 300;
            feeder2.step([]() { return true; }, ms);
        }
        ASSERT_TRUE(feeder2.counted() == 0 && feeder2.pending() == 0 && history2.count() == 4);
    } TEST_END();

    TEST_CASE("HS5.2 A Match That Arrives Later (A Room's Own, A File Of The Bot Arena Put In The Folder) Is Counted When The Replay Store Has Seen It; One That Is Deleted Before Its Turn Is Not Played; Nothing Is Played Twice") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string first = rig.keep(match_file(1));
        rig.drain(6);
        ASSERT_TRUE(rig.history.count() == 1 && rig.feeder.counted() == 1);
        // a room keeps a match: the store's index moves, the feeder looks again
        const std::string second = rig.keep(match_file(2));
        ASSERT_FALSE(second.empty());
        rig.drain(6);
        ASSERT_TRUE(rig.history.count() == 2 && rig.history.find(second) != nullptr && rig.feeder.counted() == 2);
        // the arena puts a file in the folder: nothing happens before the store has looked at the folder
        const std::string arena = id_at(kT0 + 500, "TINY");
        write_bytes(rig.replay_dir / (arena + ".antsrep"), match_file(3));
        rig.drain(6);
        ASSERT_TRUE(rig.history.find(arena) == nullptr && rig.feeder.counted() == 2);
        ASSERT_EQ(rig.touch_and_rescan(), size_t{1});
        rig.drain(6);
        ASSERT_TRUE(rig.history.find(arena) != nullptr && rig.feeder.counted() == 3 && rig.history.find(arena)->format == "2v2");
        // two keep before the feeder works; the one that is to be played next is deleted in the meantime
        const std::string a = rig.keep(match_file(0));
        const std::string b = rig.keep(match_file(1));
        ASSERT_TRUE(rig.replays.remove(b + ".antsrep"));
        rig.drain(6);
        ASSERT_TRUE(rig.history.find(a) != nullptr && rig.history.find(b) == nullptr && rig.feeder.counted() == 4 && rig.history.count() == 4);
        (void)first;
    } TEST_END();

    TEST_CASE("HS5.3 A Recording That Cannot Be Counted Is Left Out, Once, With A Line In The Log (The First Ten Are Named, The Rest Are Counted In The Summary): A Map That Is Not Here, A Map Of The Same Name That Is Another File; The Server Does Not Try Again") {
        TempDir nowhere;
        Rig rig(nowhere.path.string());
        ASSERT_TRUE(rig.open());
        for (int i = 0; i < 12; ++i) write_bytes(rig.replay_dir / (id_at(kT0 + 100 + i) + ".antsrep"), match_file(1));
        ASSERT_EQ(rig.touch_and_rescan(), size_t{12});
        rig.drain(20);
        ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.counted() == 0 && rig.feeder.left_out() == 12 && rig.feeder.pending() == 0);
        std::vector<std::string> notes = rig.history.take_notes();
        ASSERT_EQ(lines_with(notes, " is not counted: "), size_t{10});
        ASSERT_TRUE(lines_with(notes, "TINY.LVL is not in") == 10);
        ASSERT_EQ(lines_with(notes, "0 match(es) counted, 12 left out"), size_t{1});
        const int asked = rig.idle_asked;
        rig.drain(10);                                                         // (once left out, a file is not played again until the server starts again)
        ASSERT_TRUE(rig.idle_asked == asked && rig.history.take_notes().empty());
        ASSERT_TRUE(names_in(rig.history_dir).empty());
        // a map of that name that is another file
        TempDir other_map;
        fs::copy_file(fs::path(maps_dir()) / "TINY.LVL", other_map.path / "TINY.LVL");
        {
            std::ofstream more(other_map.path / "TINY.LVL", std::ios::binary | std::ios::app);
            more.put('x');
        }
        Rig changed(other_map.path.string());
        ASSERT_TRUE(changed.open());
        ASSERT_FALSE(changed.keep(match_file(1)).empty());
        changed.drain(6);
        notes = changed.history.take_notes();
        ASSERT_TRUE(changed.history.count() == 0 && changed.feeder.left_out() == 1);
        ASSERT_EQ(lines_with(notes, "is not the file that the match was played on"), size_t{1});
    } TEST_END();

    TEST_CASE("HS5.4 Files That Are Not Played At All: A Recording Of Other Simulation Rules (Counted In The Summary, Never Queued, Never Left Out), A Damaged File (Not Readable, Not Queued); The Good One Next To Them Is Counted") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        MatchSpec other;
        other.seed = 21;
        other.turns = 150;
        other.roster = 0x03;
        other.sim_rules = 99;
        const std::vector<uint8_t> other_bytes = record_match(other);
        ASSERT_FALSE(other_bytes.empty());
        const std::string other_id = rig.keep(other_bytes);
        ASSERT_FALSE(other_id.empty());
        ASSERT_EQ(rig.replays.find(other_id + ".antsrep")->sim_rules, uint16_t{99});
        const std::string good = rig.keep(match_file(1));
        write_text(rig.replay_dir / (id_at(kT0 + 700) + ".antsrep"), "this is not a replay");
        ASSERT_EQ(rig.touch_and_rescan(), size_t{1});
        ASSERT_FALSE(rig.replays.find(id_at(kT0 + 700) + ".antsrep")->readable);
        rig.drain(8);
        ASSERT_TRUE(rig.history.count() == 1 && rig.history.find(good) != nullptr && rig.history.find(other_id) == nullptr);
        ASSERT_TRUE(rig.feeder.counted() == 1 && rig.feeder.left_out() == 0 && rig.feeder.pending() == 0);
        const std::vector<std::string> notes = rig.history.take_notes();
        ASSERT_EQ(lines_with(notes, "1 match(es) counted, 0 left out; 1 kept in all; 1 recording(s) of other simulation rules cannot be counted"), size_t{1});
        ASSERT_EQ(lines_with(notes, " is not counted: "), size_t{0});
    } TEST_END();

    TEST_CASE("HS5.5 A Record That The Disk Refuses Is Told In The Log And Left Out For This Run (The Server Does Not Try Again And Again); When The Server Starts Again It Is Counted") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        const std::string b = rig.keep(match_file(2));
        rig.rename_fails = true;
        rig.drain(8);
        ASSERT_TRUE(rig.history.count() == 0 && rig.feeder.counted() == 0 && rig.feeder.left_out() == 2 && rig.feeder.pending() == 0);
        ASSERT_TRUE(names_in(rig.history_dir).empty());
        std::vector<std::string> notes = rig.history.take_notes();
        ASSERT_TRUE(lines_with(notes, a + " could not be kept") == 1 && lines_with(notes, b + " could not be kept") == 1);
        rig.rename_fails = false;
        rig.drain(8);
        ASSERT_EQ(rig.history.count(), size_t{0});
        HistoryStore again(rig.history.config());
        std::string why;
        ASSERT_TRUE(again.prepare(why));
        HistoryFeeder feeder(again, rig.replays, maps_dir());
        uint32_t ms = 9000;
        for (int i = 0; i < 6; ++i) {
            ms += 300;
            feeder.step([]() { return true; }, ms);
        }
        ASSERT_TRUE(again.count() == 2 && again.find(a) != nullptr && again.find(b) != nullptr && feeder.counted() == 2);
        // a history that was never prepared (the folder could not be used) is no work for anybody
        HistoryStore off(HistoryConfig{});
        HistoryFeeder idle_feeder(off, rig.replays, maps_dir());
        int asked = 0;
        ASSERT_FALSE(idle_feeder.step([&]() {
            ++asked;
            return true;
        }, 20000));
        ASSERT_TRUE(asked == 0 && off.count() == 0);
    } TEST_END();

    TEST_CASE("HS5.6 What The Owner Took Out Is Not Counted Again While Its Recording Is There, And The Marker Is Deleted An Hour After The Recording Is Gone (The Feeder Looks Once An Hour)") {
        Rig rig;
        ASSERT_TRUE(rig.open());
        const std::string a = rig.keep(match_file(1));
        const std::string b = rig.keep(match_file(2));
        rig.drain(8);
        ASSERT_EQ(rig.history.count(), size_t{2});
        std::string why;
        ASSERT_TRUE(rig.history.remove(a, true, why));
        ASSERT_TRUE(rig.history.knows(a) && rig.history.count() == 1);
        // the server starts again: the folder says a is taken out, so only nothing is counted
        {
            HistoryStore again(rig.history.config());
            ASSERT_TRUE(again.prepare(why));
            ASSERT_TRUE(again.count() == 1 && again.marker_count() == 1);
            HistoryFeeder feeder(again, rig.replays, maps_dir());
            uint32_t ms = 9000;
            for (int i = 0; i < 8; ++i) {
                ms += 300;
                feeder.step([]() { return true; }, ms);
            }
            ASSERT_TRUE(feeder.counted() == 0 && again.count() == 1 && again.find(a) == nullptr);
        }
        // an hour later, with the recording still there: the marker stays; after the recording went, the next hourly look deletes it
        rig.ms += HistoryFeeder::kPruneEveryMs + 1000;
        rig.step();
        ASSERT_TRUE(rig.history.marker_count() == 1 && fs::exists(rig.history_dir / (a + ".json")));
        ASSERT_TRUE(rig.replays.remove(a + ".antsrep"));
        rig.step();
        ASSERT_EQ(rig.history.marker_count(), size_t{1});                       // (not yet: it looks once an hour)
        rig.ms += HistoryFeeder::kPruneEveryMs + 1000;
        rig.step();
        ASSERT_TRUE(rig.history.marker_count() == 0 && !rig.history.knows(a) && !fs::exists(rig.history_dir / (a + ".json")));
        ASSERT_TRUE(rig.history.find(b) != nullptr && fs::exists(rig.history_dir / (b + ".json")));
    } TEST_END();

    // ---- the doors ------------------------------------------------------------------------------------------------------------------------

    TEST_CASE("HS6.1 The Server Counts Its Matches On Its Own Loop, And GET /history Lists Them On Both Doors Alike: Newest First, The Lean Form, The Recording's State And File, The Counts, The Days The Recording Is Kept") {
        Server s;
        ASSERT_TRUE(s.open());
        std::vector<std::string> ids;
        for (int k = 0; k < 4; ++k) ids.push_back(s.keep(match_file(k)));
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{0});
        s.run_updates(30);
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{4});
        const ctl::HttpResponse r = s.control("GET", "/history");
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(contains(r.content_type, "json"));
        const ctl::JsonValue j = parse(r.body);
        ASSERT_TRUE(j.get("count").as_int_or(-1) == 4 && j.get("total").as_int_or(-1) == 4 && j.get("offset").as_int_or(-1) == 0 && j.get("limit").as_int_or(-1) == 50);
        ASSERT_TRUE(j.get("keep_days").as_int_or(-1) == 30 && j.get("sim_rules").as_int_or(-1) == replay::kSimRules);
        ASSERT_EQ(j.get("history").size(), size_t{4});
        for (size_t i = 0; i < 4; ++i) {
            const ctl::JsonValue& e = j.get("history").at(i);
            const std::string& id = ids[3 - i];
            ASSERT_TRUE(e.get("id").as_string_or() == id && e.get("recording").as_string_or() == "watch" && e.get("file").as_string_or() == id + ".antsrep");
            ASSERT_TRUE(e.has("seconds") && e.has("format") && e.has("rows") && e.has("draw") && e.has("computers_only") && e.get("seats").at(0).has("killed") && e.get("seats").at(0).has("colour"));
            ASSERT_FALSE(e.has("v") || e.get("seats").at(0).has("bombs_planted"));
        }
        ASSERT_EQ(j.get("history").at(0).get("format").as_string_or(), std::string("2v2"));
        ASSERT_EQ(j.get("history").at(3).get("seats").at(0).get("name").as_string_or(), std::string("Ann"));
        ASSERT_EQ(j.get("history").at(3).get("seats").at(2).get("bot").as_string_or(), std::string("Hard"));
        const ctl::HttpResponse pub = s.door("GET", "/history");
        ASSERT_TRUE(pub.status == 200 && pub.body == r.body);
        // the filters, by their words
        const auto ask = [&](const std::string& query) {
            const ctl::HttpResponse a = s.control("GET", "/history", query);
            std::vector<std::string> out;
            if (a.status != 200) return out;
            const ctl::JsonValue k = parse(a.body);
            for (const ctl::JsonValue& e : k.get("history").items()) out.push_back(e.get("id").as_string_or());
            return out;
        };
        ASSERT_EQ(ask("limit=2&offset=1&sort=old"), (std::vector<std::string>{ids[1], ids[2]}));
        ASSERT_EQ(ask("format=ffa"), (std::vector<std::string>{ids[0]}));
        ASSERT_EQ(ask("format=team"), (std::vector<std::string>{ids[3]}));
        ASSERT_EQ(ask("format=1v1"), (std::vector<std::string>{ids[2], ids[1]}));
        const auto matching = [&](const std::function<bool(const HistoryRecord&)>& fits) {         // (newest first, by what the records themselves say)
            std::vector<std::string> out;
            for (size_t i = 0; i < 4; ++i) {
                if (fits(*s.mgr.history_store()->find(ids[3 - i]))) out.push_back(ids[3 - i]);
            }
            return out;
        };
        const auto cut = [](const HistoryRecord& rec) { return !rec.finished || rec.quitter >= 0; };
        ASSERT_EQ(ask("result=draw"), matching([](const HistoryRecord& rec) { return rec.draw; }));
        ASSERT_EQ(ask("result=cut"), matching([&](const HistoryRecord& rec) { return !rec.draw && cut(rec); }));
        ASSERT_EQ(ask("result=decided"), matching([&](const HistoryRecord& rec) { return !rec.draw && !cut(rec); }));
        ASSERT_EQ(ask("result=decided"), (std::vector<std::string>{ids[0]}));                  // (only the long match was ended by the rules, and nobody ties)
        ASSERT_TRUE(!ask("result=cut").empty() && ask("result=cut").front() != ids[0] && ask("result=draw").empty());
        ASSERT_EQ(ask("who=computers"), std::vector<std::string>());
        ASSERT_EQ(ask("who=people&format=1v1&sort=short").size(), size_t{2});
        ASSERT_EQ(ask("q=ann"), (std::vector<std::string>{ids[0]}));
        ASSERT_EQ(ask("q=Bot+%28Hard%29"), (std::vector<std::string>{ids[0]}));
        ASSERT_EQ(ask("colour=black"), (std::vector<std::string>{ids[3], ids[0]}));
        ASSERT_EQ(ask("colour=Blue&rec=watch"), (std::vector<std::string>{ids[3], ids[0]}));
        ASSERT_EQ(ask("colour=all&rec=all&result=all&who=all&format=all&sort=new").size(), size_t{4});
        ASSERT_EQ(ask("rec=removed"), std::vector<std::string>());
        ASSERT_EQ(ask("rec=old"), std::vector<std::string>());
        ASSERT_EQ(ask("rec=watch").size(), size_t{4});
        ASSERT_EQ(ask("&&limit=1&").size(), size_t{1});
        const ctl::JsonValue paged = parse(s.control("GET", "/history", "limit=1&offset=2").body);
        ASSERT_TRUE(paged.get("count").as_int_or(-1) == 4 && paged.get("total").as_int_or(-1) == 4 && paged.get("offset").as_int_or(-1) == 2 && paged.get("limit").as_int_or(-1) == 1);
    } TEST_END();

    TEST_CASE("HS6.2 A Query That Is Not Understood Is A 400 That Names The Key (A Word That Is Not One, A Key Twice, A Number Out Of Range, An Escape That Is Not One, A Character No Name Has, A Text Too Long, A Key That Does Not Exist); Never A Guess") {
        Server s;
        ASSERT_TRUE(s.open());
        for (const std::string& query : std::vector<std::string>{
                 "sort=bogus", "format=2v2", "result=won", "who=bots", "rec=gone", "colour=purple", "limit=0", "limit=101", "limit=abc", "limit=", "limit=-1", "limit=00000", "limit=1e2", "offset=-1",
                 "offset=1000001", "offset=x", "limit=1&limit=2", "sort=new&sort=old", "foo=1", "sort", "=1", "q=%zz", "q=%4", "q=%", "q=%01", "q=%7F", "q=%C3%A9", "q=" + std::string(41, 'a'), "q=a&q=b"}) {
            const ctl::HttpResponse r = s.control("GET", "/history", query);
            ASSERT_EQ(r.status, 400);
            ASSERT_TRUE(parse(r.body).get("error").is_string() && !parse(r.body).get("error").str().empty());
            ASSERT_EQ(s.door("GET", "/history", query).status, 400);
        }
        ASSERT_TRUE(contains(parse(s.control("GET", "/history", "sort=bogus").body).get("error").str(), "sort"));
        ASSERT_TRUE(contains(parse(s.control("GET", "/history", "limit=1&limit=2").body).get("error").str(), "limit"));
        ASSERT_TRUE(contains(parse(s.control("GET", "/history", "foo=1").body).get("error").str(), "foo"));
        // the edges that are fine
        for (const std::string& query : std::vector<std::string>{"limit=1", "limit=100", "offset=0", "offset=1000000", "q=" + std::string(40, 'a'), "q=a+b", "q=%41%62", "q=", "colour=any", "colour=GREEN"}) {
            ASSERT_EQ(s.control("GET", "/history", query).status, 200);
        }
    } TEST_END();

    TEST_CASE("HS6.3 GET /history/<id> Gives The Whole Record File With What A Later Version Keeps In It (The Page Reads The Keys It Knows), And The State Of The Recording; An Id That Is Not One, A Match That Is Not There And A Query Are 404; Both Doors Give The Same") {
        Server s;
        ASSERT_TRUE(s.open());
        const std::string id = s.keep(match_file(1));
        s.run_updates(10);
        ASSERT_TRUE(s.mgr.history_store()->find(id) != nullptr);
        ctl::HttpResponse r = s.control("GET", "/history/" + id);
        ASSERT_EQ(r.status, 200);
        ctl::JsonValue j = parse(r.body);
        ASSERT_TRUE(j.get("id").as_string_or() == id && j.get("v").as_int_or(0) == 1 && j.get("recording").as_string_or() == "watch" && j.get("file").as_string_or() == id + ".antsrep");
        ASSERT_TRUE(j.get("keep_days").as_int_or(0) == 30 && j.get("seats").at(0).has("bombs_planted") && j.get("seats").at(0).has("fires_lit") && j.get("seats").at(0).get("name").as_string_or() == "Cy");
        ASSERT_FALSE(j.has("details"));
        ASSERT_TRUE(s.door("GET", "/history/" + id).body == r.body);
        // a later version adds its details to the file; the answer carries them, the list does not need them
        const fs::path file = s.history_dir / (id + ".json");
        ctl::JsonValue on_disk = parse(slurp_text(file));
        ctl::JsonValue details = ctl::JsonValue::make_object();
        details.set("graph", ctl::JsonValue::make_array());
        on_disk.set("details", std::move(details));
        write_text(file, ctl::to_json(on_disk) + "\n");
        r = s.control("GET", "/history/" + id);
        j = parse(r.body);
        ASSERT_TRUE(r.status == 200 && j.get("details").is_object() && j.get("details").has("graph") && j.get("recording").as_string_or() == "watch");
        ASSERT_FALSE(parse(s.control("GET", "/history").body).get("history").at(0).has("details"));
        // the refusals
        for (const std::string& path : std::vector<std::string>{"/history/ants-TINY-20990101-000000Z", "/history/x", "/history/", "/history/../x", "/history/" + id + ".json", "/history/" + id + "/", "/history/" + id + ".antsrep",
                                                                   "/history/ANTS-TINY-20261008-143209Z"}) {
            ASSERT_EQ(s.control("GET", path).status, 404);
            ASSERT_EQ(s.door("GET", path).status, 404);
        }
        ASSERT_EQ(s.control("GET", "/history/" + id, "x=1").status, 404);
        ASSERT_EQ(s.door("GET", "/history/" + id, "x=1").status, 404);
        // a file on disk that the answer cannot parse is no reason to fail: the record in memory answers
        write_text(file, "{ broken");
        r = s.control("GET", "/history/" + id);
        ASSERT_TRUE(r.status == 200 && parse(r.body).get("id").as_string_or() == id && parse(r.body).get("seats").size() == 2);
    } TEST_END();

    TEST_CASE("HS6.4 A Recording That Is Gone Leaves Its History: The Record Says \"removed\" With No File, A Recording Of Other Rules Says \"old\" With Its File, And Both Are Found By The rec Filter") {
        Server s;
        ASSERT_TRUE(s.open());
        const std::string kept = s.keep(match_file(1));
        const std::string gone = s.keep(match_file(2));
        MatchSpec other;
        other.seed = 31;
        other.turns = 150;
        other.roster = 0x03;
        other.sim_rules = 99;
        const std::string old = s.keep(record_match(other));
        ASSERT_FALSE(old.empty());
        s.run_updates(30);
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{2});                  // (the one of other rules cannot be counted: a record of it exists only when somebody made it)
        HistoryRecord made_for_old = *s.mgr.history_store()->find(kept);
        made_for_old.id = old;
        ASSERT_TRUE(ReplayStore::time_of_name(history_recording_file(old), made_for_old.ended_s));
        std::string why;
        ASSERT_TRUE(s.mgr.history_store()->put(made_for_old, why));
        HistoryRecord orphan = *s.mgr.history_store()->find(kept);
        orphan.id = id_at(kT0 - 86400 * 40);
        orphan.ended_s = kT0 - 86400 * 40;
        ASSERT_TRUE(s.mgr.history_store()->put(orphan, why));
        ASSERT_TRUE(s.mgr.replay_store()->remove(gone + ".antsrep"));
        const ctl::JsonValue j = parse(s.control("GET", "/history").body);
        ASSERT_TRUE(j.get("total").as_int_or(0) == 4 && j.get("history").size() == 4);
        std::map<std::string, std::string> state;
        std::map<std::string, bool> has_file;
        for (const ctl::JsonValue& e : j.get("history").items()) {
            state[e.get("id").as_string_or()] = e.get("recording").as_string_or();
            has_file[e.get("id").as_string_or()] = e.has("file");
        }
        ASSERT_TRUE(state[kept] == "watch" && has_file[kept] && state[gone] == "removed" && !has_file[gone] && state[old] == "old" && has_file[old] && state[orphan.id] == "removed" && !has_file[orphan.id]);
        const auto ids = [&](const std::string& query) {
            std::set<std::string> out;
            const ctl::JsonValue answer = parse(s.control("GET", "/history", query).body);
            for (const ctl::JsonValue& e : answer.get("history").items()) out.insert(e.get("id").as_string_or());
            return out;
        };
        ASSERT_EQ(ids("rec=watch"), (std::set<std::string>{kept}));
        ASSERT_EQ(ids("rec=old"), (std::set<std::string>{old}));
        ASSERT_EQ(ids("rec=removed"), (std::set<std::string>{gone, orphan.id}));
        const ctl::JsonValue item = parse(s.control("GET", "/history/" + gone).body);
        ASSERT_TRUE(item.get("recording").as_string_or() == "removed" && !item.has("file") && item.get("keep_days").as_int_or(0) == 30 && item.get("seats").size() == 2);
    } TEST_END();

    TEST_CASE("HS6.5 Only The Owner Takes A Match Out (DELETE /history/<id> on the control door): With The Recording Still There A Restart Does Not Count It Again, Without It The File Is Gone; The Public Door And Every Other Method Refuse") {
        Server s;
        ASSERT_TRUE(s.open());
        const std::string a = s.keep(match_file(1));
        const std::string b = s.keep(match_file(2));
        s.run_updates(30);
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{2});
        // not the public door, not another method
        ASSERT_EQ(s.door("DELETE", "/history/" + a).status, 404);
        ASSERT_EQ(s.door("POST", "/history").status, 404);
        ASSERT_EQ(s.door("POST", "/history/" + a).status, 404);
        ASSERT_EQ(s.control("POST", "/history").status, 405);
        ASSERT_EQ(s.control("POST", "/history/" + a).status, 405);
        ASSERT_EQ(s.control("DELETE", "/history").status, 405);
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{2});
        // not a match that is not there, not an id that is not one, not with a query
        ASSERT_EQ(s.control("DELETE", "/history/ants-TINY-20990101-000000Z").status, 404);
        ASSERT_EQ(s.control("DELETE", "/history/../x").status, 404);
        ASSERT_EQ(s.control("DELETE", "/history/" + a, "x=1").status, 404);
        ASSERT_EQ(s.mgr.history_store()->count(), size_t{2});
        // the recording is still there: the file becomes a marker
        ctl::HttpResponse r = s.control("DELETE", "/history/" + a);
        ASSERT_EQ(r.status, 200);
        ASSERT_EQ(parse(r.body).get("deleted").as_string_or(), a);
        ASSERT_TRUE(s.mgr.history_store()->count() == 1 && s.mgr.history_store()->knows(a));
        ASSERT_TRUE(parse(slurp_text(s.history_dir / (a + ".json"))).get("deleted").as_bool_or(false));
        ASSERT_EQ(s.control("GET", "/history/" + a).status, 404);
        ASSERT_EQ(s.control("DELETE", "/history/" + a).status, 404);
        ASSERT_EQ(parse(s.control("GET", "/history").body).get("total").as_int_or(0), 1);
        // the server starts again over the same folders: the match is not counted again
        {
            RoomManager again{MapStore(maps_dir())};
            ReplayConfig rc;
            rc.dir = s.replay_dir.string();
            rc.game_version = "v0.12.0-test";
            rc.min_free_bytes = 0;
            rc.settle_s = 0;
            rc.clock_s = [&]() { return s.now; };
            std::string why;
            ASSERT_TRUE(again.enable_replays(rc, false, why));
            HistoryConfig hc;
            hc.dir = s.history_dir.string();
            ASSERT_TRUE(again.enable_history(hc, why));
            for (uint32_t i = 1; i <= 30; ++i) again.update(1000 + i * 300);
            ASSERT_TRUE(again.history_store()->count() == 1 && again.history_store()->find(a) == nullptr && again.history_store()->knows(a));
            ASSERT_TRUE(parse(slurp_text(s.history_dir / (a + ".json"))).get("deleted").as_bool_or(false));
        }
        // the recording of b went already: the file is deleted, no marker
        ASSERT_TRUE(s.mgr.replay_store()->remove(b + ".antsrep"));
        r = s.control("DELETE", "/history/" + b);
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(s.mgr.history_store()->count() == 0 && !s.mgr.history_store()->knows(b) && !fs::exists(s.history_dir / (b + ".json")));
        // the marker of a goes an hour after the recording of a did
        ASSERT_TRUE(s.mgr.replay_store()->remove(a + ".antsrep"));
        s.ms += HistoryFeeder::kPruneEveryMs + 5000;
        s.run_updates(3);
        s.ms += HistoryFeeder::kPruneEveryMs + 5000;
        s.run_updates(3);
        ASSERT_TRUE(!s.mgr.history_store()->knows(a) && !fs::exists(s.history_dir / (a + ".json")) && names_in(s.history_dir).empty());
    } TEST_END();

    TEST_CASE("HS6.6 A Server With No History Answers 404 On Both Doors; A History With No Replay Store Lists What The Folder Holds (Every Recording Removed) And Counts Nothing; A Folder That Cannot Be Used Is Refused With The Reason; The Log Hears Of The History") {
        {
            Server s;
            ASSERT_TRUE(s.open(false));
            ASSERT_TRUE(s.mgr.history_store() == nullptr);
            for (const char* method : {"GET", "DELETE"}) {
                ASSERT_EQ(s.control(method, "/history").status, 404);
                ASSERT_EQ(s.control(method, "/history/ants-TINY-20261008-143209Z").status, 404);
            }
            ASSERT_EQ(s.door("GET", "/history").status, 404);
            ASSERT_EQ(s.door("GET", "/history/ants-TINY-20261008-143209Z").status, 404);
            ASSERT_TRUE(contains(parse(s.control("GET", "/history").body).get("error").str(), "no match history"));
        }
        {
            TempDir temp;
            HistoryConfig hc;
            hc.dir = (temp.path / "history").string();
            {
                HistoryStore writer(hc);
                std::string why;
                ASSERT_TRUE(writer.prepare(why));
                for (const HistoryRecord& r : six_records()) ASSERT_TRUE(writer.put(r, why));
            }
            RoomManager mgr{MapStore(maps_dir())};
            std::string why;
            ASSERT_TRUE(mgr.enable_history(hc, why));
            ASSERT_TRUE(mgr.history_store() != nullptr && mgr.history_store()->count() == 6);
            for (int i = 1; i <= 10; ++i) mgr.update(1000 + static_cast<uint32_t>(i) * 300);             // (no replay store: nothing to count, nothing breaks)
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/history";
            rq.query = "rec=removed&limit=100";
            const ctl::HttpResponse r = handle_control(mgr, rq, 5000);
            ASSERT_EQ(r.status, 200);
            const ctl::JsonValue j = parse(r.body);
            ASSERT_TRUE(j.get("count").as_int_or(0) == 6 && j.get("keep_days").as_int_or(-1) == 0);
            for (const ctl::JsonValue& e : j.get("history").items()) ASSERT_TRUE(e.get("recording").as_string_or() == "removed" && !e.has("file"));
            ASSERT_EQ(lines_with(mgr.take_notices(), "6 match(es) kept for good"), size_t{1});
            // an empty folder name keeps none and is no error; a folder that is a file is refused
            ASSERT_TRUE(mgr.enable_history(HistoryConfig{}, why) && mgr.history_store() == nullptr);
            write_text(temp.path / "plain", "x");
            HistoryConfig bad;
            bad.dir = (temp.path / "plain").string();
            ASSERT_FALSE(mgr.enable_history(bad, why));
            ASSERT_TRUE(contains(why, "history folder") && mgr.history_store() == nullptr);
        }
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures
              << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
