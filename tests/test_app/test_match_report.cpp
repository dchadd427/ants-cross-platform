// Tests of the numbers of the results screen besides the original's four (match_report.hpp, live_match_stats.hpp, docs/REPLAYS.md "The match pages"):
//   MR1  the honors and the Play of the game, on made-up counts of the stats pass (the MVP rule, First blood, Top Thief, Last stand; the play: its points, its bonuses, its moment and its ties)
//   MR2  the live pass against the recording's pass: a match that is counted while it is played says what the replay of its own recording says, field by field (four computer players on three maps, a game on one machine,
//        a match of the network, a catch-up through the lock-step runner in slices of every size, the lists of bombs and fires on a world made by hand)
//   MR3  a tick or a turn that the pass did not see, a match that was not played from its first turn: the report is not ok; and counting changes nothing (the state hash, the file, the engine's queues of cues)
#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/live_match_stats.hpp"
#include "ants_app/match_report.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_net/lockstep.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/match_stats.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::app;
using ants::replay::MatchStats;
using ants::sim::Command;
using ants::sim::CommandType;
namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

// A match of the network shows the predicted engine (net::Prediction), whose work has a budget of the thread's CPU time: a test process on a busy machine that goes over it loses the prediction for ten
// seconds, and the screen's engine changes under the test. That is no part of what these tests are about: no budget here.
static const bool g_prediction_has_no_budget_here = (ants::net::NetGame::default_prediction_budget_ns() = UINT64_MAX, true);

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name.substr(0, 100) << " ... " << std::flush;
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

constexpr size_t kSeats = MatchReport::kSeats;
using Action = MatchStats::Action;
constexpr size_t kDeath = static_cast<size_t>(Action::Death);
constexpr size_t kTheft = static_cast<size_t>(Action::Theft);
constexpr size_t kBombAction = static_cast<size_t>(Action::Bomb);
constexpr size_t kFire = static_cast<size_t>(Action::Fire);
constexpr size_t kHit = static_cast<size_t>(Action::Hit);
constexpr uint8_t kNone = MatchStats::kNoKiller;

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }
const std::vector<std::string>& map_dirs() {
    static const std::vector<std::string> dirs = {maps_dir(), std::string(ANTS_SOURCE_DIR) + "/Community-Maps"};
    return dirs;
}
// The file of a map of the six originals or of the players' library
std::string map_file(const std::string& name) {
    for (const std::string& dir : map_dirs()) {
        const std::string path = dir + "/" + name;
        if (std::ifstream(path, std::ios::binary).good()) return path;
    }
    return std::string();
}

// ---- made-up counts ---------------------------------------------------------------------------------------------------------------------------------

// The counts of a match of `roster` in which the pass found nothing
MatchStats empty_stats(uint8_t roster = 0x0F) {
    MatchStats st;
    st.ok = true;
    st.roster = roster;
    st.turns = 6000;
    return st;
}

// The results of a match in which seat s has the score `scores[s]`; `ally` is each seat's ally field (a pair that names each other is a team: one row)
sim::MatchResult result_of(std::array<int32_t, 4> scores, uint8_t roster = 0x0F, std::array<uint8_t, 4> ally = {sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE}) {
    sim::MatchResult r;
    r.is_over = true;
    r.present_mask = roster;
    r.ally = ally;
    for (size_t s = 0; s < 4; ++s) r.stats[s].score = scores[s];
    return r;
}
sim::MatchResult seat_wins(uint8_t seat) {
    std::array<int32_t, 4> scores{};
    scores[seat] = 1000;
    return result_of(scores);
}

MatchStats::Kill kill_of(uint32_t turn, uint8_t killer, uint8_t victim, int32_t x = 10, int32_t y = 10, bool drowned = false) {
    MatchStats::Kill k;
    k.turn = turn;
    k.killer = killer;
    k.victim = victim;
    k.tile_x = x;
    k.tile_y = y;
    k.drowned = drowned;
    k.mates_near = 1;                                                              // (an ant of the victim's seat was left near it: not the last of a group, unless a test says so)
    return k;
}
// A kill with `mates` ants of the victim's own seat left within 4 tiles of it (0: it was the last of its group)
MatchStats::Kill kill_with(uint32_t turn, uint8_t killer, uint8_t victim, int32_t x, int32_t y, uint8_t mates) {
    MatchStats::Kill k = kill_of(turn, killer, victim, x, y);
    k.mates_near = mates;
    return k;
}
// The kill of a Thief (the type 3): it was raiding the hill of seat `raid_on` (kNone: none) and carried loot or not
MatchStats::Kill thief_kill(uint32_t turn, uint8_t killer, uint8_t victim, int32_t x, int32_t y, uint8_t raid_on, bool loot) {
    MatchStats::Kill k = kill_of(turn, killer, victim, x, y);
    k.victim_type = 3;
    k.raid_on = raid_on;
    k.carried_loot = loot;
    return k;
}

MatchStats::Raid raid_of(uint32_t turn, uint8_t thief, uint8_t victim, int32_t points, int32_t x = 5, int32_t y = 5) {
    MatchStats::Raid r;
    r.turn = turn;
    r.thief = thief;
    r.victim = victim;
    r.points = points;
    r.tile_x = x;
    r.tile_y = y;
    return r;
}

void count_in_bucket(MatchStats& st, size_t kind, uint32_t turn, uint32_t n = 1) {
    std::vector<uint32_t>& series = st.action[kind];
    const size_t bucket = turn / MatchStats::kSampleTurns;
    if (series.size() <= bucket) series.resize(bucket + 1);
    series[bucket] += n;
}
// The way the pass adds an event: to its list and to the count of the bucket of its turn
void add_kill(MatchStats& st, const MatchStats::Kill& k) {
    st.kills.push_back(k);
    count_in_bucket(st, kDeath, k.turn);
}
void add_raid(MatchStats& st, const MatchStats::Raid& r) {
    st.raids.push_back(r);
    count_in_bucket(st, kTheft, r.turn);
}
MatchReport report_of(const MatchStats& st, const sim::MatchResult& result, sim::GameMode mode = sim::GameMode::HighestScore, uint8_t local = 0) {
    return make_match_report(st, result, mode, local);
}

// ---- comparing two counts ---------------------------------------------------------------------------------------------------------------------------

// The name of the first field in which two counts of the same match differ, "" when none does. The orders are not compared: the live pass does not count them (live_match_stats.hpp), and says so by leaving them empty.
std::string differs(const MatchStats& a, const MatchStats& b) {
#define SAME(field) \
    if (!(a.field == b.field)) return #field
    SAME(ok);
    SAME(turns);
    SAME(roster);
    SAME(map_width);
    SAME(map_height);
    if (a.samples.size() != b.samples.size()) return "samples (how many)";
    for (size_t i = 0; i < a.samples.size(); ++i) {
        if (a.samples[i].turn != b.samples[i].turn || a.samples[i].box != b.samples[i].box || a.samples[i].score != b.samples[i].score || a.samples[i].killed != b.samples[i].killed) return "samples";
    }
    if (a.kills.size() != b.kills.size()) return "kills (how many)";
    for (size_t i = 0; i < a.kills.size(); ++i) {
        const MatchStats::Kill &x = a.kills[i], &y = b.kills[i];
        if (x.turn != y.turn || x.killer != y.killer || x.victim != y.victim || x.victim_type != y.victim_type || x.drowned != y.drowned || x.tile_x != y.tile_x || x.tile_y != y.tile_y || x.raid_on != y.raid_on ||
            x.carried_loot != y.carried_loot || x.mates_near != y.mates_near) {
            return "kills";
        }
    }
    if (a.raids.size() != b.raids.size()) return "raids (how many)";
    for (size_t i = 0; i < a.raids.size(); ++i) {
        const MatchStats::Raid &x = a.raids[i], &y = b.raids[i];
        if (x.turn != y.turn || x.thief != y.thief || x.victim != y.victim || x.points != y.points || x.tile_x != y.tile_x || x.tile_y != y.tile_y) return "raids";
    }
    SAME(damage_dealt);
    SAME(damage_taken);
    SAME(damage_from);
    SAME(action);
    SAME(powerups);
    SAME(food_trips);
    SAME(food_points);
    SAME(banked);
    SAME(hatch_cost);
    SAME(bombs_off);
    SAME(wipe_turn);
    SAME(wipes);
    SAME(out_turn);
    SAME(raids_home);
    SAME(loot_home);
    SAME(bombs_planted);
    SAME(fires_lit);
    SAME(hill_x);
    SAME(hill_y);
#undef SAME
    return std::string();
}

bool same_honor(const MatchReport::Honor& a, const MatchReport::Honor& b) { return a.present == b.present && a.seat == b.seat && a.value == b.value && a.turn == b.turn; }

std::string differs(const MatchReport& a, const MatchReport& b) {
#define SAME(field) \
    if (!(a.field == b.field)) return #field
    SAME(ok);
    SAME(turns);
    SAME(roster);
    SAME(winners);
    SAME(damage_done);
    SAME(damage_taken);
    SAME(food_trips);
    SAME(food_points);
    SAME(bombs_set);
    SAME(fires_lit);
    SAME(raids);
    SAME(loot);
    SAME(raids_home);
    SAME(loot_home);
#undef SAME
    if (!same_honor(a.mvp, b.mvp)) return "mvp";
    if (!same_honor(a.first_blood, b.first_blood)) return "first_blood";
    if (!same_honor(a.top_thief, b.top_thief)) return "top_thief";
    if (!same_honor(a.last_stand, b.last_stand)) return "last_stand";
    const MatchReport::Play &x = a.play, &y = b.play;
    if (x.present != y.present || x.seat != y.seat || x.kills != y.kills || x.points != y.points || x.seconds != y.seconds || x.bonuses != y.bonuses || x.knocked_out != y.knocked_out || x.turn != y.turn || x.tile_x != y.tile_x ||
        x.tile_y != y.tile_y) {
        return "play";
    }
    return std::string();
}

// ---- a match that is played ---------------------------------------------------------------------------------------------------------------------------

class RecordingSink final : public sim::CommandSink {
public:
    RecordingSink(sim::SimulationEngine& sim, replay::Recorder& rec) : sim_(sim), rec_(rec) {}
    sim::CommandResult submit(const Command& c) override {
        rec_.on_command(c);
        return sim_.apply_command(c);
    }

private:
    sim::SimulationEngine& sim_;
    replay::Recorder& rec_;
};

// What a match of four computer players came to, counted while it was played (the way the application does it: after each tick, before the queues of the engine are drained) and written as a recording
struct Played {
    std::vector<uint8_t> bytes;
    replay::Replay file;
    assets::LevelData level;
    replay::MatchStats live;
    MatchReport live_report;
    uint64_t hash{0};
    bool over{false};
    std::vector<sim::AudioEvent> cues;           // every cue that the engine made, drained after each tick
    std::vector<uint64_t> hashes;                // the state hash after every tick
};

Played play_bots(const char* map, uint32_t seed, uint32_t turns, bool counted) {
    Played out;
    const std::string path = map_file(map);
    uint64_t map_hash = 0;
    if (path.empty() || !out.level.load_from_file(path) || !net::hash_file(path, map_hash)) return out;
    replay::Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = map;
    head.map_hash = map_hash;
    head.seed = seed;
    head.roster = 0x0F;
    head.fog = false;
    for (uint8_t seat = 0; seat < 4; ++seat) head.names[seat] = "Bot";
    sim::SimulationEngine engine;
    engine.init(out.level, seed, 0x0F);
    replay::Recorder rec(head);
    RecordingSink sink(engine, rec);
    ai::BotController controller(engine, seed);
    for (uint8_t seat = 0; seat < 4; ++seat) {
        ai::BotSpec spec;
        spec.seat = seat;
        spec.level = ai::Level::Hard;
        std::string why;
        if (!controller.add(spec, sink, why)) return out;
    }
    LiveMatchStats live;
    if (counted) live.start(engine);
    for (uint32_t turn = 0; turn < turns && !engine.is_match_over(); ++turn) {
        engine.tick();
        rec.on_tick(engine);
        live.on_tick(engine);
        controller.on_tick(engine);
        for (const sim::AudioEvent& cue : engine.poll_audio_events()) out.cues.push_back(cue);
        engine.clear_news_events();
        out.hashes.push_back(engine.state_hash().total);
    }
    if (counted) {
        live.finish(engine, 0);
        out.live = live.stats();
        out.live_report = live.report();
    }
    out.hash = engine.state_hash().total;
    out.over = engine.is_match_over();
    std::string error;
    out.bytes = rec.finish(engine, error);
    if (!out.bytes.empty() && !replay::decode(out.bytes.data(), out.bytes.size(), out.file, error)) out.bytes.clear();
    return out;
}

// The pass over the recording of a match: the stats, and the report that the results give
struct Replayed {
    bool ok{false};
    replay::MatchStats stats;
    MatchReport report;
};
Replayed replayed(const replay::Replay& file, uint8_t local_seat) {
    Replayed out;
    assets::LevelData level;
    std::string why;
    if (!replay::load_map(file.head, map_dirs(), level, why)) return out;
    replay::StatsPlay sp = replay::play_with_stats(file, level);
    out.ok = sp.outcome.ok && sp.stats.ok;
    out.report = make_match_report(sp.stats, sp.outcome.result, static_cast<sim::GameMode>(file.head.mode), local_seat);
    out.stats = std::move(sp.stats);
    return out;
}

// The same match as a machine of the network runs it: its turns go through a lock-step runner, the first `catch_up_turns` as the catch-up of a machine that came back (executed at once, `slice` at a time, the engine's
// queues never drained) and the rest as the live stream, and the pass is told by the runner's tap, as the application is. Nothing drains the queues in between: the cues of the whole match pile up in the engine.
struct Run {
    bool ok{false};
    replay::MatchStats stats;
    MatchReport report;
    uint64_t hash{0};
};
Run through_runner(const Played& played, uint32_t catch_up_turns, uint32_t slice) {
    Run out;
    sim::SimulationEngine engine;
    replay::begin_match(engine, played.file, played.level, played.file.head.fog);
    net::LockstepRunner runner(engine);
    LiveMatchStats live;
    live.start(engine);
    runner.set_on_executed([&](const net::TurnMsg& turn) { live.on_turn(turn, engine); });
    const uint32_t total = played.file.total_turns;
    size_t next = 0;
    for (uint32_t t = 0; t < total; ++t) {
        net::TurnMsg msg;
        msg.turn = t;
        for (; next < played.file.commands.size() && played.file.commands[next].turn == t; ++next) msg.commands.push_back(played.file.commands[next].command);
        if (!(t < catch_up_turns ? runner.on_catch_up_turn(msg) : runner.on_turn(msg))) return out;
    }
    while (runner.next_turn_to_execute() < catch_up_turns) {
        if (runner.fast_forward(std::min(slice, catch_up_turns - runner.next_turn_to_execute())) == 0) return out;
    }
    for (int frames = 0; frames < 400000 && runner.next_turn_to_execute() < total; ++frames) runner.update(50);
    if (runner.next_turn_to_execute() != total) return out;
    live.finish(engine, 0);
    out.stats = live.stats();
    out.report = live.report();
    out.hash = engine.state_hash().total;
    out.ok = true;
    return out;
}

// What was in the matches that the tests play: some of everything, so that "equal" is not "equally empty"
struct Content {
    size_t kills{0}, raids{0}, bombs{0}, fires{0}, hits{0}, near_mates{0}, last_of_group{0};
    uint64_t damage{0}, food_trips{0};
};
Content content_of(const MatchStats& s) {
    Content c;
    c.kills = s.kills.size();
    c.raids = s.raids.size();
    for (size_t seat = 0; seat < kSeats; ++seat) {
        c.bombs += s.bombs_off[seat];
        c.fires += s.fires_lit[seat];
    }
    for (const uint32_t n : s.action[kHit]) c.hits += n;
    for (const MatchStats::Kill& k : s.kills) {
        c.near_mates += k.mates_near > 0 ? 1u : 0u;
        c.last_of_group += k.mates_near == 0 ? 1u : 0u;
    }
    for (size_t seat = 0; seat < kSeats; ++seat) {
        c.damage += s.damage_dealt[seat];
        c.food_trips += s.food_trips[seat];
    }
    return c;
}

// ---- the application ----------------------------------------------------------------------------------------------------------------------------------

// A folder of this process alone (made new, removed with everything in it when the program ends)
class ScratchRoot {
public:
    ScratchRoot() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_match_report_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) {
                path_ = candidate;
                return;
            }
        }
    }
    ~ScratchRoot() {
        std::error_code ec;
        if (!path_.empty()) fs::remove_all(path_, ec);
    }
    ScratchRoot(const ScratchRoot&) = delete;
    ScratchRoot& operator=(const ScratchRoot&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

ApplicationConfig local_config(uint8_t bots_mask) {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.default_map_path = maps_dir() + "/TINY.LVL";
    cfg.play_at_once = true;                                                // (the setup screen's own START, with the "Get ready" dialog)
    cfg.lan_port = 0;
    for (uint8_t seat = 1; seat < 4; ++seat) {
        if (((bots_mask >> seat) & 1u) == 0) continue;
        ants::ai::BotSpec bot;
        bot.seat = seat;
        bot.level = ants::ai::Level::Medium;
        cfg.bots.push_back(bot);
    }
    return cfg;
}

void press(Application& app, SDL_Keycode key, uint16_t mod = 0) {
    SDL_KeyboardEvent ev{};
    ev.type = SDL_KEYDOWN;
    ev.keysym.sym = key;
    ev.keysym.mod = mod;
    app.handle_key_down(ev);
}

// Ctrl+Q and Yes: the quit that ends a match with one other side left
void quit_match(Application& app) {
    press(app, SDLK_q, KMOD_LCTRL);
    press(app, SDLK_y);
}

bool read_back(const Application& app, replay::Replay& out, std::string& error) {
    return !app.last_replay().empty() && replay::decode(app.last_replay().data(), app.last_replay().size(), out, error);
}

// The other machine of a match of the network: a simulation and a NetGame, with the little that the application does for the room (load the map, report)
struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    uint32_t now{1000};
    Peer() { net.set_discovery(0); }
    void update() {
        net.update(now);
        for (const auto& ev : net.take_events()) {
            if (ev.type != net::NetGame::Event::Type::StartRequested) continue;
            const net::StartMsg& s = net.start_info();
            assets::LevelData level;
            uint64_t hash = 0;
            const bool ok = level.load_lvl(maps_dir() + "/" + s.map_name) && net::hash_file(maps_dir() + "/" + s.map_name, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
                sim.init(level, s.seed, s.roster);
                sim::apply_start_teams(sim, s.teams());
            }
            net.report_loaded(ok);
        }
    }
};

// Steps the application and the peer together in 10 ms of game time
struct Duo {
    Application& app;
    Peer& peer;
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            peer.now += 10;
            peer.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {       // the game clock is virtual but the sockets are real: a late kernel gets real time, with the game clock standing still
            app.pump_network(0.0f);
            peer.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

// A match of two on TINY: the bare machine hosts as Alice (seat 0), the application joins as Bob (seat 1). Returns the stepper once the match is begun on both machines, nullptr when it could not be started.
std::unique_ptr<Duo> start_duo(Application& app, Peer& host, uint32_t seed, uint8_t mode = 0) {
    if (!host.net.host(0, "Alice", true)) return nullptr;
    host.net.set_map("TINY.LVL");
    host.net.set_mode(mode);
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;
    cfg.net_role = ApplicationConfig::NetRole::Join;
    cfg.net_address = "127.0.0.1";
    cfg.net_port = host.net.listen_port();
    cfg.player_name = "Bob";
    if (!app.init(cfg)) return nullptr;
    auto duo = std::make_unique<Duo>(Duo{app, host});
    if (!duo->until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000)) return nullptr;
    host.net.set_map("TINY.LVL");
    duo->step(300);
    uint64_t hash = 0;
    if (!net::hash_file(maps_dir() + "/TINY.LVL", hash) || !host.net.start_match(seed, hash)) return nullptr;
    if (!duo->until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000)) return nullptr;
    return duo;
}

// An application that watches `bytes` (written to a file of its own folder)
struct Viewer {
    ScratchRoot root;
    Application app;
    bool ok{false};
    explicit Viewer(const std::vector<uint8_t>& bytes) {
        const fs::path path = root.path() / "match.antsrep";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.lan_port = 0;
        cfg.maps_dir = maps_dir();
        cfg.replay_path = path.string();
        ok = app.init(cfg);
    }
};

// A world made by hand: an engine with no map and a live pass that is told each tick, as the application tells it
struct Bench {
    sim::SimulationEngine engine;
    LiveMatchStats live;
    uint32_t ticks{0};
    explicit Bench(uint32_t match_time_ms = 720000) {
        engine.init_test_world(60, 60, 1, match_time_ms);
        live.start(engine);
    }
    void step(uint32_t n = 1) {
        for (uint32_t i = 0; i < n; ++i) {
            engine.tick();
            ++ticks;
            live.on_tick(engine);
            engine.clear_news_events();
            engine.clear_audio_events();
        }
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=== The numbers of the results screen ===\n";

    // ---- MR1: what the counts say -----------------------------------------------------------------------------------------------------------------------

    TEST_CASE("MR1.1 The MVP share is the average of the seat's shares of the three things (damage done, kills, points banked) in thousandths, each share rounded down, a thing that nobody did left out of the average") {
        MatchStats st = empty_stats();
        ASSERT_EQ(mvp_share(st, 0), 0u);                                            // (nothing was done at all)
        st.damage_dealt = {300, 100, 0, 0};                                        // 750 and 250 thousandths
        for (uint32_t i = 0; i < 3; ++i) st.kills.push_back(kill_of(100 + i, 0, 1));
        st.kills.push_back(kill_of(200, 1, 0));                                    // kills 3 and 1: 750 and 250
        st.kills.push_back(kill_of(210, 2, 0, 10, 10, true));                      // (a death by drowning is nobody's kill)
        st.kills.push_back(kill_of(220, kNone, 0));                                // (and nor is one that no team did)
        st.banked = {100, 300, 0, 0};                                              // 250 and 750
        ASSERT_EQ(mvp_share(st, 0), (750u + 750u + 250u) / 3u);                    // (a thing that nobody did is left out, not counted as 0: 583, not 437 of four things)
        ASSERT_EQ(mvp_share(st, 1), (250u + 250u + 750u) / 3u);                    // (416: the sum is divided, never each share rounded up)
        ASSERT_EQ(mvp_share(st, 2), 0u);
        ASSERT_EQ(mvp_share(st, 3), 0u);
        st.raids.push_back(raid_of(300, 0, 1, 30));
        st.raids.push_back(raid_of(310, 1, 0, 10));                                // loot 30 of 40 and 10 of 40
        ASSERT_EQ(mvp_share(st, 0), (750u + 750u + 250u) / 3u);                    // (loot is no thing of its own: the points banked have it already, and it would count twice; the shares are what they were)
        ASSERT_EQ(mvp_share(st, 1), (250u + 250u + 750u) / 3u);
        ASSERT_EQ(mvp_share(st, 9), 0u);                                           // (no such seat)
    } TEST_END();

    TEST_CASE("MR1.2 The share rounds down at each step, a seat that does not play has none, banked points below zero are none, and a share of kills is of the kills of the roster") {
        MatchStats st = empty_stats();
        st.damage_dealt = {1, 1, 1, 0};                                            // a third each: 333, never 334
        ASSERT_EQ(mvp_share(st, 0), 333u);
        ASSERT_EQ(mvp_share(st, 3), 0u);
        MatchStats three = empty_stats(0x07);                                      // seat 3 does not play: its damage (a made-up number that cannot be) is not in the total
        three.damage_dealt = {1, 1, 0, 50};
        ASSERT_EQ(mvp_share(three, 0), 500u);
        ASSERT_EQ(mvp_share(three, 3), 0u);
        MatchStats food = empty_stats();
        food.banked = {-50, 100, 0, 0};                                            // (a seat that gained less than nothing has no share, and the total is the other's 100)
        ASSERT_EQ(mvp_share(food, 0), 0u);
        ASSERT_EQ(mvp_share(food, 1), 1000u);
        MatchStats kills = empty_stats();
        kills.kills = {kill_of(10, 0, 1), kill_of(20, 1, 0), kill_of(30, 1, 2)};
        ASSERT_EQ(mvp_share(kills, 0), 333u);
        ASSERT_EQ(mvp_share(kills, 1), 666u);
    } TEST_END();

    TEST_CASE("MR1.3 The MVP is the seat of the winning side with the highest share, the lower seat on a tie, none when the winner did none of the three things; an alliance wins as two seats; the local seat breaks a tie of scores") {
        MatchStats st = empty_stats();
        st.damage_dealt = {100, 900, 300, 0};
        st.banked = {50, 0, 150, 0};
        // seat 1 did the most, but seat 0 won: the MVP is the best of the winning side
        MatchReport r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.ok && r.winners == 0x01);
        ASSERT_TRUE(r.mvp.present && r.mvp.seat == 0 && r.mvp.value == static_cast<int64_t>(mvp_share(st, 0)) && r.mvp.turn == 0);
        // an alliance of seats 0 and 2 is the first row (their scores are added): the better of the two, with the damage that it did
        const sim::MatchResult team = result_of({500, 100, 400, 0}, 0x0F, {2, sim::ALLIANCE_NONE, 0, sim::ALLIANCE_NONE});
        r = report_of(st, team);
        ASSERT_TRUE(r.winners == 0x05);
        ASSERT_TRUE(r.mvp.present && r.mvp.seat == 2 && r.mvp.value == static_cast<int64_t>(mvp_share(st, 2)) && mvp_share(st, 2) > mvp_share(st, 0));        // (seat 2: 230 and 750 of the two things, seat 0: 76 and 250; the value is the share)
        // an equal share: the lower seat
        MatchStats tie = empty_stats();
        tie.damage_dealt = {200, 0, 200, 0};
        r = report_of(tie, team);
        ASSERT_TRUE(r.mvp.present && r.mvp.seat == 0 && r.mvp.value == 500);                  // (a half each: 500 thousandths)
        // nothing done by the winning side: no MVP, though another seat did things; and nothing done at all
        MatchStats other = empty_stats();
        other.damage_dealt = {0, 80, 0, 0};
        r = report_of(other, seat_wins(3));
        ASSERT_TRUE(r.ok && r.winners == 0x08 && !r.mvp.present);
        r = report_of(empty_stats(), seat_wins(1));
        ASSERT_TRUE(r.ok && !r.mvp.present && !r.first_blood.present && !r.top_thief.present && !r.last_stand.present && !r.play.present);
        // two rows with the same score: the local seat's goes first (the results screen's own rule), so the winners are the local seat's
        const sim::MatchResult level = result_of({100, 100, 0, 0}, 0x03);
        ASSERT_EQ(report_of(st, level, sim::GameMode::HighestScore, 1).winners, 0x02);
        ASSERT_EQ(report_of(st, level, sim::GameMode::HighestScore, 0).winners, 0x01);
    } TEST_END();

    TEST_CASE("MR1.4 First blood is the first kill by a seat that is not its victim's own: a death by drowning, a death that no team did and one of the team's own ant are nobody's; the killer and the turn are its") {
        MatchStats st = empty_stats();
        st.kills.push_back(kill_of(50, 1, 2, 10, 10, true));                       // drowned
        st.kills.push_back(kill_of(60, kNone, 2));                                 // no team
        st.kills.push_back(kill_of(70, 2, 2));                                     // (a team's own: the pass never makes this, the rule says it anyway)
        st.kills.push_back(kill_of(100, 3, 0));
        st.kills.push_back(kill_of(100, 1, 0));                                    // (the same turn, counted after: the first stays)
        MatchReport r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.first_blood.present && r.first_blood.seat == 3 && r.first_blood.turn == 100 && r.first_blood.value == 0);
        st.kills.push_back(kill_of(90, 2, 0));                                     // (an earlier turn, whatever the order: it is the first blood)
        r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.first_blood.present && r.first_blood.seat == 2 && r.first_blood.turn == 90);
        MatchStats none = empty_stats();
        none.kills.push_back(kill_of(50, 1, 2, 10, 10, true));
        none.kills.push_back(kill_of(60, kNone, 2));
        r = report_of(none, seat_wins(0));
        ASSERT_FALSE(r.first_blood.present);
    } TEST_END();

    TEST_CASE("MR1.5 Top Thief is the seat with the most raids whose loot got home (more than none, the lower seat on a tie); in 187 there is Last stand instead: of the seats that did not win and ran out of ants for good, the last") {
        MatchStats st = empty_stats();
        st.raids_home = {1, 3, 3, 2};
        st.loot_home = {50, 150, 90, 100};
        st.out_turn = {-1, 3000, 3500, -1};
        st.wipe_turn = {-1, 100, 2000, 5000};                                      // (a seat that was wiped out and came back from an egg did not run out: only out_turn counts)
        MatchReport r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.top_thief.present && r.top_thief.seat == 1 && r.top_thief.value == 3 && r.top_thief.turn == 0);       // (seats 1 and 2 have three raids home: the lower seat)
        ASSERT_TRUE(r.raids_home == st.raids_home && r.loot_home == st.loot_home);
        ASSERT_FALSE(r.last_stand.present);                                        // (the original's game has no Last stand, whatever ran out)
        st.raids_home = {1, 3, 4, 2};
        r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.top_thief.present && r.top_thief.seat == 2 && r.top_thief.value == 4);
        st.raids_home = {1, 0, 0, 5};                                              // (the winner of the match is not the one: the honor is about every seat)
        r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.top_thief.present && r.top_thief.seat == 3 && r.top_thief.value == 5);
        r = report_of(st, seat_wins(0), sim::GameMode::Kills187);
        ASSERT_FALSE(r.top_thief.present);                                         // (187 has no Top Thief)
        ASSERT_TRUE(r.last_stand.present && r.last_stand.seat == 2 && r.last_stand.turn == 3500);
        st.out_turn = {-1, 3000, 3000, -1};                                        // a tie: the lower seat
        r = report_of(st, seat_wins(0), sim::GameMode::Kills187);
        ASSERT_TRUE(r.last_stand.present && r.last_stand.seat == 1);
        st.out_turn = {4000, 3000, 3500, -1};                                      // the winner ran out later than all and is not a candidate (it is on the winning side)
        r = report_of(st, seat_wins(0), sim::GameMode::Kills187);
        ASSERT_TRUE(r.last_stand.present && r.last_stand.seat == 2);
        st.out_turn = {-1, -1, -1, -1};
        r = report_of(st, seat_wins(0), sim::GameMode::Kills187);
        ASSERT_FALSE(r.last_stand.present);
        // a raid that was begun is not a raid that got home: five begun and one home loses to two home
        MatchStats begun = empty_stats();
        for (uint32_t i = 0; i < 5; ++i) add_raid(begun, raid_of(100 + 10 * i, 3, 0, 50));
        add_raid(begun, raid_of(300, 1, 0, 50));
        add_raid(begun, raid_of(310, 1, 0, 50));
        begun.raids_home = {0, 2, 0, 1};
        begun.loot_home = {0, 100, 0, 50};
        r = report_of(begun, seat_wins(0));
        ASSERT_TRUE(r.top_thief.present && r.top_thief.seat == 1 && r.top_thief.value == 2 && r.loot_home[1] == 100);
        ASSERT_TRUE(r.raids[3] == 5 && r.raids_home[3] == 1);
        // none got home (a raid on a hill with nothing, a Thief killed on the way): no Top Thief
        MatchStats nothing = empty_stats();
        add_raid(nothing, raid_of(100, 0, 1, 0));
        add_raid(nothing, raid_of(200, 2, 3, 50));
        r = report_of(nothing, seat_wins(0));
        ASSERT_FALSE(r.top_thief.present);
        ASSERT_EQ(r.raids[0], 1u);
        // the debt comes home too: a raid on a hill below zero is a raid home, whatever its points
        MatchStats debt = empty_stats();
        debt.raids_home = {0, 1, 0, 0};
        debt.loot_home = {0, -30, 0, 0};
        r = report_of(debt, seat_wins(0));
        ASSERT_TRUE(r.top_thief.present && r.top_thief.seat == 1 && r.top_thief.value == 1 && r.loot_home[1] == -30);
    } TEST_END();

    TEST_CASE("MR1.6 The Play of the game is a seat's best 10 seconds: 10 points for every kill of another seat's ant, a moment runs from one kill to the last that is less than 200 turns after it, and the card's facts (kills, seconds, the first kill's time and place) are read off its kills") {
        MatchStats one = empty_stats();                                            // one kill is a play when it is all there is: there is no minimum
        add_kill(one, kill_of(330, 1, 2, 11, 12));
        MatchReport r = report_of(one, seat_wins(0));
        ASSERT_TRUE(r.play.present && r.play.seat == 1 && r.play.kills == 1 && r.play.points == 10 && r.play.seconds == 0 && r.play.bonuses == 0);
        ASSERT_TRUE(r.play.turn == 330 && r.play.tile_x == 11 && r.play.tile_y == 12 && r.play.watch_turn() == 230);       // (Watch opens 5 seconds before the first kill)
        // a kill that is nobody's is none: a drowned ant, a death that no team did, an ant of a team's own
        MatchStats nobody = empty_stats();
        add_kill(nobody, kill_of(100, 1, 2, 10, 10, true));
        add_kill(nobody, kill_of(110, kNone, 2));
        add_kill(nobody, kill_of(120, 2, 2));
        ASSERT_FALSE(report_of(nobody, seat_wins(0)).play.present);
        add_kill(nobody, kill_of(500, 3, 0, 4, 5));
        r = report_of(nobody, seat_wins(0));
        ASSERT_TRUE(r.play.present && r.play.seat == 3 && r.play.kills == 1 && r.play.points == 10 && r.play.turn == 500);      // (a seat that did not win can have it: the play is about every seat)
        // 10 seconds: a kill 199 turns after the first is in its moment, one 200 turns after is not (and is a moment of its own)
        MatchStats edge = empty_stats();
        add_kill(edge, kill_of(1000, 0, 1));
        add_kill(edge, kill_of(1199, 0, 1));
        r = report_of(edge, seat_wins(1));
        ASSERT_TRUE(r.play.kills == 2 && r.play.points == 20 && r.play.turn == 1000 && r.play.seconds == 10);
        MatchStats apart = empty_stats();
        add_kill(apart, kill_of(1000, 0, 1));
        add_kill(apart, kill_of(1200, 0, 1));
        r = report_of(apart, seat_wins(1));
        ASSERT_TRUE(r.play.kills == 1 && r.play.points == 10 && r.play.turn == 1000 && r.play.seconds == 0);          // (two moments of 10 points: the earlier)
        // the seconds from the first kill to the last, rounded up, at least 1 for two kills or more, 0 for one
        const struct { uint32_t span, seconds; } spans[] = {{0, 1}, {1, 1}, {20, 1}, {21, 2}, {40, 2}, {41, 3}, {100, 5}, {179, 9}, {180, 9}, {181, 10}, {199, 10}};
        for (const auto& s : spans) {
            MatchStats st = empty_stats();
            add_kill(st, kill_of(2000, 2, 0));
            add_kill(st, kill_of(2000 + s.span, 2, 0));
            ASSERT_EQ(report_of(st, seat_wins(0)).play.seconds, s.seconds);
        }
        // the seconds are those of the moment's own kills: a third kill that is out of it does not add to them
        MatchStats three = empty_stats();
        add_kill(three, kill_of(3000, 2, 0));
        add_kill(three, kill_of(3030, 2, 0));
        add_kill(three, kill_of(3400, 2, 0));
        r = report_of(three, seat_wins(0));
        ASSERT_TRUE(r.play.kills == 2 && r.play.seconds == 2 && r.play.turn == 3000);
        // a moment is the best that a seat has, not the first: the moment begins at a kill, and the kills of other seats are none of it
        MatchStats later = empty_stats();
        add_kill(later, kill_of(100, 1, 0, 3, 3));
        add_kill(later, kill_of(105, 0, 1));                                       // (another seat's kill in the same 10 seconds adds to nobody else)
        add_kill(later, kill_of(1000, 1, 0, 7, 8));
        add_kill(later, kill_of(1010, 1, 0));
        add_kill(later, kill_of(1020, 1, 0));
        r = report_of(later, seat_wins(0));
        ASSERT_TRUE(r.play.seat == 1 && r.play.kills == 3 && r.play.points == 30 && r.play.turn == 1000 && r.play.tile_x == 7 && r.play.tile_y == 8 && r.play.seconds == 1);
        MatchStats slide = empty_stats();                                          // 0, 150, 300 and 310: the moment that begins at 150 has three kills, the one that begins at 0 two
        for (const uint32_t turn : {0u, 150u, 300u, 310u}) add_kill(slide, kill_of(turn, 3, 0));
        r = report_of(slide, seat_wins(0));
        ASSERT_TRUE(r.play.seat == 3 && r.play.kills == 3 && r.play.turn == 150 && r.play.seconds == 8);
        // kills in turn order or not, and several in one turn: the moment is the same
        MatchStats shuffled = empty_stats();
        for (const uint32_t turn : {310u, 0u, 300u, 150u}) add_kill(shuffled, kill_of(turn, 3, 0));
        r = report_of(shuffled, seat_wins(0));
        ASSERT_TRUE(r.play.kills == 3 && r.play.turn == 150);
    } TEST_END();

    TEST_CASE("MR1.7 A kill that knocked a seat out is worth 20 more: the last ant that the seat lost before it was out of the match, when a seat killed it; a seat that came back from an egg was not out then, and a drowned last ant is nobody's") {
        const auto play_of = [](const MatchStats& st) { return report_of(st, seat_wins(0)).play; };
        MatchStats ko = empty_stats();
        ko.out_turn = {-1, 500, -1, -1};
        add_kill(ko, kill_of(300, 0, 1));                                          // (seat 1's first ant to go: 10 points, as any kill)
        add_kill(ko, kill_of(500, 2, 1, 7, 8));                                    // (its last)
        MatchReport::Play p = play_of(ko);
        ASSERT_TRUE(p.present && p.seat == 2 && p.kills == 1 && p.points == 30 && p.bonuses == MatchReport::kKnockedOut && p.knocked_out == 1 && p.turn == 500);
        ko.out_turn = {-1, 520, -1, -1};                                           // out a little later, when its last egg was gone: the last ant that it lost is still the one
        p = play_of(ko);
        ASSERT_TRUE(p.seat == 2 && p.points == 30 && p.bonuses == MatchReport::kKnockedOut);
        ko.out_turn = {-1, -1, -1, -1};                                            // not out at the end: no knockout
        p = play_of(ko);
        ASSERT_TRUE(p.seat == 0 && p.points == 10 && p.bonuses == 0);              // (two kills of 10: the earlier)
        MatchStats back = empty_stats();                                           // wiped out at 300, an egg gave it an ant, lost at 700 and out: only the last is the knockout
        back.out_turn = {-1, 700, -1, -1};
        add_kill(back, kill_of(300, 0, 1));
        add_kill(back, kill_of(700, 3, 1));
        p = play_of(back);
        ASSERT_TRUE(p.seat == 3 && p.points == 30 && p.knocked_out == 1);
        MatchStats same = empty_stats();                                           // the same seat made both kills, 400 turns apart: two moments, and the knockout's is the best
        same.out_turn = {-1, 700, -1, -1};
        add_kill(same, kill_of(300, 0, 1));
        add_kill(same, kill_of(700, 0, 1));
        p = play_of(same);
        ASSERT_TRUE(p.seat == 0 && p.kills == 1 && p.points == 30 && p.turn == 700);
        MatchStats drowned = empty_stats();                                        // the last ant fell in the water: nobody knocked the seat out
        drowned.out_turn = {-1, 500, -1, -1};
        add_kill(drowned, kill_of(300, 0, 1));
        add_kill(drowned, kill_of(500, 2, 1, 10, 10, true));
        p = play_of(drowned);
        ASSERT_TRUE(p.seat == 0 && p.points == 10 && p.bonuses == 0);
        MatchStats nobodys = empty_stats();                                        // ... or died of something that no team did
        nobodys.out_turn = {-1, 500, -1, -1};
        add_kill(nobodys, kill_of(300, 0, 1));
        add_kill(nobodys, kill_of(500, kNone, 1));
        p = play_of(nobodys);
        ASSERT_TRUE(p.seat == 0 && p.points == 10 && p.bonuses == 0);
        MatchStats two = empty_stats();                                            // two seats knocked out in one moment: 60 points, and the first is the one that the card names
        two.out_turn = {-1, 100, 150, -1};
        add_kill(two, kill_of(100, 0, 1));
        add_kill(two, kill_of(150, 0, 2));
        p = play_of(two);
        ASSERT_TRUE(p.seat == 0 && p.kills == 2 && p.points == 60 && p.bonuses == MatchReport::kKnockedOut && p.knocked_out == 1);
        MatchStats other = empty_stats();                                          // a seat that does not play has no knockout, whatever its turn says
        other.roster = 0x07;
        other.out_turn = {-1, -1, -1, 400};
        add_kill(other, kill_of(400, 0, 3));
        ASSERT_TRUE(play_of(other).points == 10);
    } TEST_END();

    TEST_CASE("MR1.8 A Thief that was raiding the killer's own hill and was killed within 4 tiles of its mound is 15 more (the hill is saved); a Thief that carried loot is 10 more, wherever it was killed") {
        MatchStats base = empty_stats();
        base.hill_x = {20, 40, -1, 5};                                             // the mounds: 20 to 23 across and down, 40 to 43, none, 5 to 8
        base.hill_y = {20, 40, -1, 5};
        const auto saved = [&](int32_t x, int32_t y, uint8_t raid_on, uint8_t killer) {
            MatchStats st = base;
            add_kill(st, thief_kill(100, killer, killer == 2 ? 3 : 2, x, y, raid_on, false));
            return report_of(st, seat_wins(0)).play;
        };
        MatchReport::Play p = saved(21, 21, 0, 0);                                 // on the mound
        ASSERT_TRUE(p.present && p.points == 25 && p.bonuses == MatchReport::kSavedHill && p.kills == 1);
        // 4 tiles away is near, 5 is not: on each side, and across a corner (the larger of the two differences)
        const int32_t inside[][2] = {{27, 20}, {27, 23}, {16, 21}, {20, 27}, {21, 16}, {27, 27}, {16, 16}, {27, 16}, {16, 27}};
        const int32_t outside[][2] = {{28, 20}, {15, 21}, {20, 28}, {21, 15}, {28, 28}, {15, 15}, {28, 15}, {15, 28}};
        for (const auto& t : inside) ASSERT_TRUE(saved(t[0], t[1], 0, 0).points == 25);
        for (const auto& t : outside) ASSERT_TRUE(saved(t[0], t[1], 0, 0).points == 10);
        ASSERT_TRUE(saved(21, 21, 1, 0).points == 10);                             // raiding another seat's hill: nothing to save
        ASSERT_TRUE(saved(21, 21, kNone, 0).points == 10);                         // not raiding
        ASSERT_TRUE(saved(41, 41, 0, 1).points == 10);                             // killed by another seat, near that seat's own hill: it was raiding seat 0's
        ASSERT_TRUE(saved(41, 41, 1, 1).points == 25);                             // ... and the hill that it was raiding is the killer's own
        ASSERT_TRUE(saved(1, 1, 2, 2).points == 10);                               // a seat with no hill saves none
        ASSERT_TRUE(saved(7, 7, 3, 3).points == 25 && saved(7, 7, 3, 3).seat == 3);
        // loot: a Thief that carried it, wherever it was and whatever it was raiding
        MatchStats loot = empty_stats();
        add_kill(loot, thief_kill(100, 1, 0, 3, 3, kNone, true));
        p = report_of(loot, seat_wins(0)).play;
        ASSERT_TRUE(p.present && p.seat == 1 && p.points == 20 && p.bonuses == MatchReport::kStoppedLoot);
        MatchStats empty_handed = empty_stats();
        add_kill(empty_handed, thief_kill(100, 1, 0, 3, 3, kNone, false));
        ASSERT_TRUE(report_of(empty_handed, seat_wins(0)).play.points == 10);
        // every one of them on one kill: the last ant of seat 2, a Thief that raided the killer's hill, and had loot: 10 + 20 + 15 + 10
        MatchStats all = base;
        all.out_turn = {-1, -1, 100, -1};
        add_kill(all, thief_kill(100, 0, 2, 22, 22, 0, true));
        p = report_of(all, seat_wins(0)).play;
        ASSERT_TRUE(p.points == 55 && p.bonuses == (MatchReport::kKnockedOut | MatchReport::kSavedHill | MatchReport::kStoppedLoot) && p.knocked_out == 2);
        // the bonuses are what a kill is worth, and they make the best moment: a knockout alone (30) beats two plain kills (20); a Thief with loot alone (20) ties them, and the earlier moment is the play
        MatchStats beats = empty_stats();
        beats.out_turn = {-1, -1, 5000, -1};
        add_kill(beats, kill_of(100, 0, 1));
        add_kill(beats, kill_of(120, 0, 1));
        add_kill(beats, kill_of(5000, 3, 2));
        p = report_of(beats, seat_wins(0)).play;
        ASSERT_TRUE(p.seat == 3 && p.points == 30 && p.bonuses == MatchReport::kKnockedOut);
        MatchStats tied = empty_stats();
        add_kill(tied, kill_of(100, 0, 1));
        add_kill(tied, kill_of(120, 0, 1));
        add_kill(tied, thief_kill(50, 3, 2, 3, 3, kNone, true));
        p = report_of(tied, seat_wins(0)).play;
        ASSERT_TRUE(p.seat == 3 && p.points == 20 && p.turn == 50);
    } TEST_END();

    TEST_CASE("MR1.9 Wiping out a group is 15 more: the kill after which no ant of its victim's seat is left within 4 tiles of the victim, when 3 or more ants of that seat that the same seat killed in the moment, up to and with that kill, lay within 4 tiles of it; once for a place") {
        const auto group = [](std::initializer_list<MatchStats::Kill> kills) {
            MatchStats st = empty_stats();
            for (const MatchStats::Kill& k : kills) add_kill(st, k);
            return report_of(st, seat_wins(0)).play;
        };
        // three ants of seat 1 by seat 0 within 4 tiles of each other, the last leaving none of its group: 3 kills and the bonus
        MatchReport::Play p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 11, 1), kill_with(120, 0, 1, 14, 10, 0)});
        ASSERT_TRUE(p.present && p.kills == 3 && p.points == 45 && p.bonuses == MatchReport::kWipedGroup);
        // ... but not when an ant of its seat is left near (the last has company): a fight that goes on is not a group wiped out
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 11, 1), kill_with(120, 0, 1, 14, 10, 1)});
        ASSERT_TRUE(p.points == 30 && p.bonuses == 0);
        // the ants that count are those up to and with the kill: a kill that leaves none near is the wipe of the ants before it, not of those after
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 11, 0), kill_with(120, 0, 1, 14, 10, 1)});
        ASSERT_TRUE(p.points == 30 && p.bonuses == 0);
        // two ants are not a group
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 11, 0)});
        ASSERT_TRUE(p.points == 20 && p.bonuses == 0);
        // the ants of the group lie within 4 tiles of the last one: 4 is near, 5 is not
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 10, 1), kill_with(120, 0, 1, 14, 14, 0)});
        ASSERT_TRUE(p.points == 45);                                               // (both are within 4 of (14, 14)? (10, 10) is 4 away, (12, 10) is 4 away)
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 12, 10, 1), kill_with(120, 0, 1, 15, 14, 0)});
        ASSERT_TRUE(p.points == 30 && p.bonuses == 0);                             // ((10, 10) is 5 from (15, 14))
        // the ants are of one seat, and the same seat killed them
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 0, 1, 11, 10, 1), kill_with(120, 0, 2, 12, 10, 0)});
        ASSERT_TRUE(p.points == 30 && p.bonuses == 0);
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(110, 2, 1, 11, 10, 1), kill_with(120, 0, 1, 12, 10, 0)});
        ASSERT_TRUE(p.points == 20 && p.seat == 0 && p.bonuses == 0);              // (seat 2's kill is its own moment)
        // the ants of the moment: a group whose first ants fell before the moment began is not wiped out in it (the moment of the first two kills is 20, so is the one of the last two: the earlier)
        p = group({kill_with(0, 0, 1, 10, 10, 1), kill_with(100, 0, 1, 11, 10, 1), kill_with(250, 0, 1, 12, 10, 0)});
        ASSERT_TRUE(p.points == 20 && p.turn == 0 && p.bonuses == 0);
        // once for a place: five ants die where they stand, the last three leave none near; the bonus is one
        p = group({kill_with(100, 0, 1, 10, 10, 2), kill_with(100, 0, 1, 11, 10, 2), kill_with(100, 0, 1, 12, 10, 0), kill_with(100, 0, 1, 11, 11, 0), kill_with(100, 0, 1, 12, 11, 0)});
        ASSERT_TRUE(p.kills == 5 && p.points == 65 && p.bonuses == MatchReport::kWipedGroup);
        // two places, two groups
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(105, 0, 1, 11, 10, 1), kill_with(110, 0, 1, 12, 10, 0),
                   kill_with(120, 0, 1, 30, 30, 1), kill_with(125, 0, 1, 31, 30, 1), kill_with(130, 0, 1, 32, 30, 0)});
        ASSERT_TRUE(p.kills == 6 && p.points == 90);
        // two groups of two seats
        p = group({kill_with(100, 0, 1, 10, 10, 1), kill_with(105, 0, 1, 11, 10, 1), kill_with(110, 0, 1, 12, 10, 0),
                   kill_with(120, 0, 2, 12, 12, 1), kill_with(125, 0, 2, 13, 12, 1), kill_with(130, 0, 2, 14, 12, 0)});
        ASSERT_TRUE(p.points == 90 && p.bonuses == MatchReport::kWipedGroup);
    } TEST_END();

    TEST_CASE("MR1.10 The Play of the game is the best moment of any seat: the most points (three kills in a few seconds beat a brawl in which everybody did a little), the earlier moment on a tie, then the lower seat; none when no seat killed anything") {
        MatchStats brawl = empty_stats();
        for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {                                     // seats 0, 1 and 3 each make two kills in a brawl and seat 2 three in a fight of its own
            add_kill(brawl, kill_of(200u + seat, seat, 2, 20, 20));
            add_kill(brawl, kill_of(260u + seat, seat, 2, 20, 20));
        }
        for (const uint32_t turn : {4000u, 4040u, 4090u}) add_kill(brawl, kill_of(turn, 2, 0, 9, 9));
        MatchReport r = report_of(brawl, seat_wins(0));
        ASSERT_TRUE(r.play.present && r.play.seat == 2 && r.play.kills == 3 && r.play.points == 30 && r.play.turn == 4000 && r.play.seconds == 5);
        // a tie goes to the earlier moment, whatever the seat, and then to the lower seat
        MatchStats tie = empty_stats();
        add_kill(tie, kill_of(800, 0, 2, 4, 4));
        add_kill(tie, kill_of(500, 3, 2, 5, 5));
        add_kill(tie, kill_of(500, 1, 2, 6, 6));
        r = report_of(tie, seat_wins(0));
        ASSERT_TRUE(r.play.seat == 1 && r.play.turn == 500 && r.play.tile_x == 6);
        // within one seat too: two moments of 20 points, the earlier is the play
        MatchStats own = empty_stats();
        for (const uint32_t turn : {100u, 150u, 400u, 450u}) add_kill(own, kill_of(turn, 2, 0));
        r = report_of(own, seat_wins(0));
        ASSERT_TRUE(r.play.seat == 2 && r.play.kills == 2 && r.play.turn == 100 && r.play.points == 20);
        // nobody killed anything: no play, whatever else was counted
        MatchStats quiet = empty_stats();
        quiet.damage_dealt = {40, 30, 0, 0};
        add_raid(quiet, raid_of(100, 0, 1, 20));
        quiet.bombs_off = {1, 0, 0, 0};
        ASSERT_FALSE(report_of(quiet, seat_wins(0)).play.present);
        // the report of a count that is not ok has no play
        MatchStats failed = brawl;
        failed.ok = false;
        ASSERT_FALSE(report_of(failed, seat_wins(0)).play.present);
    } TEST_END();

    TEST_CASE("MR1.11 The totals come from the counts, by seat; a count that is not ok gives a report that is not ok and holds nothing but its length") {
        MatchStats st = empty_stats();
        st.damage_dealt = {5, 6, 7, 8};
        st.damage_taken = {1, 2, 3, 4};
        st.food_trips = {9, 8, 7, 6};
        st.food_points = {900, 800, -70, 60};
        st.bombs_planted = {3, 2, 1, 0};
        st.fires_lit = {0, 1, 2, 3};
        st.raids.push_back(raid_of(10, 1, 0, 12));
        st.raids.push_back(raid_of(20, 1, 2, 30));
        st.raids.push_back(raid_of(30, 3, 0, 0));
        st.raids_home = {0, 1, 0, 0};
        st.loot_home = {0, 12, 0, 0};
        const MatchReport r = report_of(st, seat_wins(0));
        ASSERT_TRUE(r.ok && r.turns == 6000 && r.roster == 0x0F);
        for (size_t s = 0; s < kSeats; ++s) {
            ASSERT_TRUE(r.damage_done[s] == st.damage_dealt[s] && r.damage_taken[s] == st.damage_taken[s] && r.food_trips[s] == st.food_trips[s] && r.food_points[s] == st.food_points[s]);
            ASSERT_TRUE(r.bombs_set[s] == st.bombs_planted[s] && r.fires_lit[s] == st.fires_lit[s]);
        }
        ASSERT_TRUE(r.raids[0] == 0 && r.raids[1] == 2 && r.raids[2] == 0 && r.raids[3] == 1);
        ASSERT_TRUE(r.loot[1] == 42 && r.loot[3] == 0 && r.loot[0] == 0);
        ASSERT_TRUE(r.raids_home == st.raids_home && r.loot_home == st.loot_home);                 // (of the two raids of seat 1, one got home)
        MatchStats failed = st;
        failed.ok = false;
        const MatchReport bad = report_of(failed, seat_wins(0));
        ASSERT_FALSE(bad.ok);
        ASSERT_TRUE(bad.turns == 6000 && bad.damage_done[0] == 0 && bad.food_trips[0] == 0 && bad.loot[1] == 0 && !bad.mvp.present && !bad.first_blood.present && !bad.play.present);
    } TEST_END();

    // ---- MR2: the live pass says what the recording says ------------------------------------------------------------------------------------------------------

    // The matches that the next cases share: four Hard computer players on a gate map where bombs are planted and set off, on a map with fires and thieves, and on a third that has both (made when the first case that needs
    // them runs)
    struct Matches {
        Played hellraiser, treasure, popcorn;
    };
    const auto matches = []() -> const Matches& {
        static const Matches m = {play_bots("HELLRAISER.lvl", 1, 6000, true), play_bots("TREASURE.LVL", 8, 4000, true), play_bots("POPcOrN.lvl", 12, 6000, true)};
        return m;
    };

    TEST_CASE("MR2.1 A match counted while it is played says what the replay of its own recording says, field by field: damage, kills (with the Thief that raided and the ants left near each), raids with their tiles, the hills, the bombs and fires, the curve of the activity, the food, the seats that ran out; the same report; and the matches had all of it") {
        const Matches& m = matches();
        Content all;
        size_t top_thieves = 0;
        for (const Played* p : {&m.hellraiser, &m.treasure, &m.popcorn}) {
            ASSERT_FALSE(p->bytes.empty());
            ASSERT_TRUE(p->live.ok && p->live_report.ok);
            const MatchReport& thief = p->live_report;                              // (the Top Thief of a real match has the most raids home, and no seat got more home than it began)
            for (size_t s = 0; s < kSeats; ++s) ASSERT_TRUE(thief.raids_home[s] <= thief.raids[s] && (!thief.top_thief.present || thief.raids_home[s] <= static_cast<uint32_t>(thief.top_thief.value)));
            ASSERT_TRUE(!thief.top_thief.present || thief.top_thief.value == static_cast<int64_t>(thief.raids_home[thief.top_thief.seat]));
            top_thieves += thief.top_thief.present ? 1u : 0u;
            const Replayed again = replayed(p->file, 0);
            ASSERT_TRUE(again.ok);
            ASSERT_EQ(differs(p->live, again.stats), std::string());
            ASSERT_EQ(differs(p->live_report, again.report), std::string());
            ASSERT_EQ(p->live.turns, p->file.total_turns);
            ASSERT_EQ(p->live_report.turns, p->file.total_turns);
            for (size_t k = 0; k < MatchStats::kOrderKinds; ++k) ASSERT_TRUE(p->live.orders[k].empty());      // (the orders are not counted live)
            for (const MatchStats::Raid& raid : p->live.raids) {                    // (a raid begins at the hill that it raids: the thief's tile is beside it)
                sim::SimulationEngine hills;
                replay::begin_match(hills, p->file, p->level, false);
                const auto* hill = hills.grid().find_anthill(raid.victim);
                ASSERT_TRUE(hill != nullptr);
                ASSERT_TRUE(std::abs(raid.tile_x - static_cast<int32_t>(hill->x)) <= 6 && std::abs(raid.tile_y - static_cast<int32_t>(hill->y)) <= 6);
            }
            const Content c = content_of(p->live);
            all.kills += c.kills;
            all.raids += c.raids;
            all.bombs += c.bombs;
            all.fires += c.fires;
            all.hits += c.hits;
            all.near_mates += c.near_mates;
            all.last_of_group += c.last_of_group;
            all.damage += c.damage;
            all.food_trips += c.food_trips;
            // what the engine itself counted agrees with what the pass found
            sim::SimulationEngine engine;
            replay::begin_match(engine, p->file, p->level, false);
            size_t next = 0;
            for (uint32_t t = 0; t < p->file.total_turns; ++t) {
                for (; next < p->file.commands.size() && p->file.commands[next].turn == t; ++next) engine.apply_command(p->file.commands[next].command);
                engine.tick();
                engine.clear_audio_events();
                engine.clear_news_events();
            }
            for (size_t s = 0; s < kSeats; ++s) {
                const sim::PlayerMatchStats& e = engine.stats_manager().get_player_stats(static_cast<uint8_t>(s));
                ASSERT_EQ(p->live_report.bombs_set[s], e.bombs_planted);
                ASSERT_EQ(p->live_report.fires_lit[s], e.fires_lit);
            }
            for (size_t seat = 0; seat < kSeats; ++seat) {                         // (every seat of a match has its hill: the tile of its mound; a kill's Thief raided another seat's hill, and an ant that was killed with loot was a Thief that raided)
                const auto* hill = engine.grid().find_anthill(static_cast<uint8_t>(seat));
                ASSERT_TRUE(hill != nullptr && p->live.hill_x[seat] == hill->x && p->live.hill_y[seat] == hill->y);
            }
            for (const MatchStats::Kill& kill : p->live.kills) ASSERT_TRUE(kill.raid_on == MatchStats::kNoKiller || (kill.raid_on < kSeats && kill.raid_on != kill.victim));
        }
        ASSERT_TRUE(top_thieves > 0);                                              // (some match had a raid that got home)
        ASSERT_TRUE(all.kills > 0 && all.raids > 0 && all.bombs > 0 && all.fires > 0 && all.hits > 0 && all.damage > 0 && all.food_trips > 0);
        ASSERT_TRUE(all.near_mates > 0 && all.last_of_group > 0);                  // (the ants left near a kill: with some there were, with some there were none)
        ASSERT_TRUE(m.hellraiser.live_report.play.present && m.treasure.live_report.play.present);         // (a real match in which a seat killed an ant has a play, and a play always has a kill and the points of it)
        for (const Played* p : {&m.hellraiser, &m.treasure, &m.popcorn}) {
            const MatchReport::Play& play = p->live_report.play;
            ASSERT_TRUE(!play.present || (play.kills > 0 && play.points >= 10 * play.kills && play.seat < kSeats && play.seconds <= 10));
        }
        std::cout << "(" << all.kills << " kills, " << all.raids << " raids, " << all.bombs << " bombs, " << all.fires << " fires, " << all.hits << " hits, " << all.near_mates << " kills with company) ";
    } TEST_END();

    TEST_CASE("MR2.2 A machine of the network says the same however its turns came: live, or as the catch-up of a machine that came back (in slices of one turn, of 777, of all the match, with the live turns after it), the engine's queues never drained") {
        const Matches& m = matches();
        const Replayed again = replayed(m.hellraiser.file, 0);
        ASSERT_TRUE(again.ok);
        const uint32_t total = m.hellraiser.file.total_turns;
        for (const auto& [catch_up, slice] : std::vector<std::pair<uint32_t, uint32_t>>{{0u, 1u}, {1u, 1u}, {2500u, 1u}, {2500u, 777u}, {total, 5000u}, {total - 30, 64u}}) {
            const Run run = through_runner(m.hellraiser, catch_up, slice);
            ASSERT_TRUE(run.ok);
            ASSERT_EQ(run.hash, m.hellraiser.hash);
            ASSERT_EQ(differs(run.stats, again.stats), std::string());
            ASSERT_EQ(differs(run.report, again.report), std::string());
        }
        const Replayed other = replayed(m.popcorn.file, 0);
        const Run run = through_runner(m.popcorn, 3000, 100);
        ASSERT_TRUE(run.ok && other.ok);
        ASSERT_EQ(differs(run.stats, other.stats), std::string());
        ASSERT_EQ(differs(run.report, other.report), std::string());
    } TEST_END();

    TEST_CASE("MR2.3 A game on this computer counts its numbers in the application: made when the match is over, before the results open, equal to the replay of the file that the application kept; a team's seats win together") {
        Application app;
        ApplicationConfig cfg = local_config(0x0E);                                    // seats 1, 2 and 3 are bots
        cfg.teams.set = true;                                                          // the player and the first bot are a team
        cfg.teams.a = 0;
        cfg.teams.b = 1;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_FALSE(app.match_report().ok);                                           // (no match has ended)
        ASSERT_TRUE(app.live_stats().counting());
        app.hud().dismiss_match_start_modal();
        int steps = 0;
        while (!app.sim().is_match_over() && steps < 40000) {
            app.update_simulation(0.05f);
            ++steps;
        }
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_FALSE(app.live_stats().counting());
        const MatchReport& report = app.match_report();
        ASSERT_TRUE(report.ok);
        ASSERT_EQ(report.turns, static_cast<uint32_t>(steps));
        ASSERT_EQ(report.roster, 0x0F);
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        const Replayed again = replayed(rep, 0);
        ASSERT_TRUE(again.ok);
        ASSERT_EQ(differs(app.live_stats().stats(), again.stats), std::string());
        ASSERT_EQ(differs(report, again.report), std::string());
        ASSERT_TRUE(content_of(again.stats).food_trips > 0);
        // the winners are the seats of the first row of the results, an alliance both of its seats
        const std::vector<sim::ResultRow> rows = app.sim().get_world_state().match_result.rows(0);
        ASSERT_FALSE(rows.empty());
        uint8_t first = static_cast<uint8_t>(1u << rows[0].first);
        if (rows[0].has_second()) first = static_cast<uint8_t>(first | (1u << rows[0].second));
        ASSERT_EQ(report.winners, first);
        // leaving the results does not take the report of a match that ended: it is valid until the next one begins
        app.return_to_map_select();
        ASSERT_TRUE(app.match_report().ok);
        app.shutdown();
    } TEST_END();

    TEST_CASE("MR2.4 A match of the network counts its numbers on every turn that the lock-step runner executes: a quit ends it, and the report equals the replay of the machine's file, in the original's game and in 187") {
        for (const uint8_t mode : {uint8_t{0}, uint8_t{1}}) {
            Peer host;
            Application app;
            const std::unique_ptr<Duo> duo = start_duo(app, host, mode == 0 ? 31337u : 4711u, mode);
            ASSERT_TRUE(duo != nullptr);
            ASSERT_TRUE(app.live_stats().counting());
            duo->step(8000);
            ASSERT_TRUE(app.sim().current_tick() > 20);
            ASSERT_EQ(app.live_stats().turns_seen(), static_cast<uint32_t>(app.sim().current_tick()));
            std::vector<uint32_t> own;
            for (const auto& a : app.sim().get_world_state().ants) {
                if (a.player_id == 1) own.push_back(a.id);
            }
            ASSERT_FALSE(own.empty());
            app.hud().select_ant(own[0]);
            app.hud().stop_selected(app.sim());
            duo->step(400);
            quit_match(app);
            ASSERT_TRUE(duo->until([&]() { return app.sim().is_match_over() && host.sim.is_match_over(); }, 4000));
            ASSERT_FALSE(app.live_stats().counting());
            const MatchReport& report = app.match_report();
            ASSERT_TRUE(report.ok);
            ASSERT_EQ(report.roster, 0x03);
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_EQ(rep.head.mode, mode);
            const Replayed again = replayed(rep, 1);
            ASSERT_TRUE(again.ok);
            ASSERT_EQ(differs(app.live_stats().stats(), again.stats), std::string());
            ASSERT_EQ(differs(report, again.report), std::string());
            ASSERT_EQ(report.turns, rep.total_turns);
            ASSERT_FALSE(mode == 1 && report.top_thief.present);                       // (187 has none)
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("MR2.5 A viewer of a recording counts nothing and has no report: the match is played from the file, and the results that it opens show the original's numbers only") {
        const Matches& m = matches();
        Viewer v(m.treasure.bytes);
        ASSERT_TRUE(v.ok);
        ASSERT_TRUE(v.app.replay_mode());
        ASSERT_FALSE(v.app.live_stats().counting());
        for (int i = 0; i < 6000; ++i) {
            const ReplayState state = v.app.replay_state();
            if (state == ReplayState::Ended || state == ReplayState::CutShort || state == ReplayState::Failed) break;
            v.app.update_simulation(0.05f);
        }
        ASSERT_TRUE(v.app.replay_state() == ReplayState::Ended || v.app.replay_state() == ReplayState::CutShort);
        ASSERT_TRUE(v.app.sim().current_tick() > 3000);
        ASSERT_FALSE(v.app.live_stats().counting());
        ASSERT_FALSE(v.app.match_report().ok);
        ASSERT_EQ(v.app.match_report().turns, 0u);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("MR2.6 What a world made by hand shows that the matches played above do not: a bomb that went off in the tick in which another was defused (which was which is read off the ants) and the seat that planted it, the hits and damage of the team that did them, fires counted for their seats") {
        {   // a bomb of seat 0 goes off under an ant of seat 1 ...
            Bench b;
            const uint32_t bomber0 = b.engine.spawn_unit(0, sim::AntType::Bomber, {10, 10});
            const uint32_t bomber1 = b.engine.spawn_unit(1, sim::AntType::Bomber, {30, 30});
            const uint32_t walker = b.engine.spawn_unit(1, sim::AntType::Worker, {12, 10});
            b.step(2);
            ASSERT_TRUE(b.engine.plant_bomb(bomber0, {11, 10}));
            ASSERT_TRUE(b.engine.plant_bomb(bomber1, {31, 30}));
            b.step(2);
            ASSERT_TRUE(b.engine.has_bomb_at({11, 10}) && b.engine.has_bomb_at({31, 30}));
            b.engine.trigger_bomb_detonation(walker, {11, 10});
            b.step(1);
            ASSERT_FALSE(b.engine.has_bomb_at({11, 10}));
            ASSERT_TRUE(b.engine.has_bomb_at({31, 30}));
            b.live.finish(b.engine, 0);
            const MatchStats& st = b.live.stats();
            ASSERT_TRUE(st.ok);
            ASSERT_TRUE(st.bombs_off[0] == 1 && st.bombs_off[1] == 0);             // (the planter's, not the ant's seat: seat 0's)
            ASSERT_TRUE(st.bombs_planted[0] == 1 && st.bombs_planted[1] == 1);
            ASSERT_EQ(st.action[kBombAction].size(), size_t{1});
            ASSERT_EQ(st.action[kBombAction][0], 1u);
            // the ant that set it off took the hits from seat 0's team: they are seat 0's damage and a hit
            ASSERT_TRUE(st.damage_dealt[0] > 0 && st.damage_dealt[1] == 0 && st.damage_taken[1] == st.damage_dealt[0] && st.damage_from[0][1] == st.damage_dealt[0]);
            ASSERT_EQ(st.action[kHit][0], 1u);
        }
        {   // a bomb of the seat's own set off by its own ant hurts it, and nobody did that: a hit, no damage dealt
            Bench b;
            const uint32_t bomber = b.engine.spawn_unit(0, sim::AntType::Bomber, {10, 10});
            const uint32_t walker = b.engine.spawn_unit(0, sim::AntType::Worker, {12, 10});
            b.engine.spawn_unit(1, sim::AntType::Worker, {40, 40});
            b.step(2);
            ASSERT_TRUE(b.engine.plant_bomb(bomber, {11, 10}));
            b.step(1);
            b.engine.trigger_bomb_detonation(walker, {11, 10});
            b.step(1);
            b.live.finish(b.engine, 0);
            const MatchStats& st = b.live.stats();
            ASSERT_TRUE(st.ok);
            ASSERT_TRUE(st.damage_taken[0] > 0 && st.damage_dealt[0] == 0 && st.bombs_off[0] == 1);
            ASSERT_EQ(st.action[kHit][0], 1u);
        }
        {   // ... and in the same tick an ant takes the other bomb away: two bombs are gone and one was defused, the ant on the tile says which went off
            Bench b;
            const uint32_t bomber0 = b.engine.spawn_unit(0, sim::AntType::Bomber, {10, 10});
            const uint32_t bomber1 = b.engine.spawn_unit(1, sim::AntType::Bomber, {30, 30});
            const uint32_t clearer = b.engine.spawn_unit(0, sim::AntType::Bomber, {32, 30});
            const uint32_t walker = b.engine.spawn_unit(1, sim::AntType::Worker, {12, 10});
            b.step(2);
            ASSERT_TRUE(b.engine.plant_bomb(bomber0, {11, 10}));
            ASSERT_TRUE(b.engine.plant_bomb(bomber1, {31, 30}));
            b.step(2);
            b.engine.trigger_bomb_detonation(walker, {11, 10});
            ASSERT_TRUE(b.engine.defuse_bomb(clearer, {31, 30}));
            b.step(1);
            ASSERT_FALSE(b.engine.has_bomb_at({11, 10}) || b.engine.has_bomb_at({31, 30}));
            b.live.finish(b.engine, 0);
            const MatchStats& st = b.live.stats();
            ASSERT_TRUE(st.ok);
            ASSERT_TRUE(st.bombs_off[0] == 1 && st.bombs_off[1] == 0);             // (the one that went off, not the one that was taken away, which seat 1 had planted)
        }
        {   // fires: the count of the engine, seat by seat, and the activity of the tick that they were lit in
            Bench b;
            const uint32_t fire0 = b.engine.spawn_unit(0, sim::AntType::Fire, {10, 10});
            const uint32_t fire1 = b.engine.spawn_unit(1, sim::AntType::Fire, {30, 30});
            b.step(2);
            ASSERT_TRUE(b.engine.ignite_fire(fire0, {11, 10}));
            ASSERT_TRUE(b.engine.ignite_fire(fire1, {30, 31}));
            b.step(4);
            ASSERT_TRUE(b.engine.ignite_fire(fire0, {9, 10}));
            b.step(1);
            b.live.finish(b.engine, 0);
            const MatchStats& st = b.live.stats();
            ASSERT_TRUE(st.ok);
            ASSERT_TRUE(st.fires_lit[0] == 2 && st.fires_lit[1] == 1);
            ASSERT_EQ(st.action[kFire].size(), size_t{1});
            ASSERT_EQ(st.action[kFire][0], 3u);
        }
    } TEST_END();

    // ---- MR3: nothing is believed that was not seen whole, and counting changes nothing --------------------------------------------------------------------

    TEST_CASE("MR3.1 A tick that the pass did not see makes the report not ok: one skipped in the middle, one at the end, one before the first, a turn that is not the next, a match that was not played from its first turn") {
        {   // seen whole: ok
            Bench b;
            b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            b.engine.spawn_unit(1, sim::AntType::Worker, {40, 40});
            b.step(12);
            const MatchReport& r = b.live.finish(b.engine, 0);
            ASSERT_TRUE(r.ok && r.turns == 12 && b.live.stats().ok && b.live.stats().turns == 12);
            ASSERT_FALSE(b.live.counting());
            ASSERT_TRUE(b.live.report().ok);                                           // (the report stays: it is valid until the next match starts)
            b.live.start(b.engine);                                                    // (and goes with the next match's start)
            ASSERT_FALSE(b.live.report().ok);
            ASSERT_TRUE(b.live.counting());
        }
        {   // a tick in the middle that the pass was not told of
            Bench b;
            b.step(4);
            b.engine.tick();
            b.step(4);
            const MatchReport& r = b.live.finish(b.engine, 0);
            ASSERT_FALSE(r.ok);
            ASSERT_FALSE(b.live.stats().ok);
            ASSERT_FALSE(b.live.stats().why.empty());
        }
        {   // the same tick told twice (the engine's count did not move, and the match is not over)
            Bench b;
            b.step(4);
            b.live.on_tick(b.engine);
            ASSERT_FALSE(b.live.finish(b.engine, 0).ok);
        }
        {   // the last tick, which ran after the last that the pass saw
            Bench b;
            b.step(6);
            b.engine.tick();
            ASSERT_FALSE(b.live.finish(b.engine, 0).ok);
        }
        {   // the engine had run before the count began
            sim::SimulationEngine engine;
            engine.init_test_world(60, 60, 1);
            engine.tick();
            engine.tick();
            LiveMatchStats live;
            live.start(engine);
            for (int i = 0; i < 5; ++i) {
                engine.tick();
                live.on_tick(engine);
            }
            ASSERT_FALSE(live.finish(engine, 0).ok);
        }
        {   // the lock-step runner's turns: from the first, each the next; a turn skipped or counted twice is no whole match
            for (const std::vector<uint32_t>& turns : std::vector<std::vector<uint32_t>>{{0, 1, 2, 3}, {0, 1, 3, 4}, {1, 2, 3, 4}, {0, 1, 1, 2}}) {
                sim::SimulationEngine engine;
                engine.init_test_world(60, 60, 1);
                LiveMatchStats live;
                live.start(engine);
                for (const uint32_t t : turns) {
                    engine.tick();
                    net::TurnMsg msg;
                    msg.turn = t;
                    live.on_turn(msg, engine);
                }
                const bool whole = turns == std::vector<uint32_t>({0, 1, 2, 3});
                ASSERT_EQ(live.finish(engine, 0).ok, whole);
            }
        }
        {   // a match that ends on its clock: the tick that ends it, and any after it, do not count themselves in the engine's counter, and the pass has seen them all
            Bench b(3000);
            b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
            b.engine.spawn_unit(1, sim::AntType::Worker, {40, 40});
            for (int i = 0; i < 200 && !b.engine.is_match_over(); ++i) b.step();
            ASSERT_TRUE(b.engine.is_match_over());
            b.step(3);                                                                 // (ticks that the runner may still run after the end)
            const MatchReport& r = b.live.finish(b.engine, 0);
            ASSERT_TRUE(r.ok);
            ASSERT_TRUE(r.turns > b.engine.current_tick());
        }
        {   // a finish that no start came before is no report: not the one of the match that ended before, which report() holds until the next match starts
            Bench b;
            b.step(6);
            ASSERT_TRUE(b.live.finish(b.engine, 0).ok);
            ASSERT_FALSE(b.live.finish(b.engine, 0).ok);
            ASSERT_TRUE(b.live.report().ok && b.live.report().turns == 6);
            LiveMatchStats never;
            ASSERT_FALSE(never.finish(b.engine, 0).ok);
            ASSERT_FALSE(never.report().ok);
        }
        {   // a match that is dropped has no report
            Bench b;
            b.step(3);
            b.live.abandon();
            ASSERT_FALSE(b.live.counting());
            ASSERT_FALSE(b.live.finish(b.engine, 0).ok);
            ASSERT_FALSE(b.live.report().ok);
        }
    } TEST_END();

    TEST_CASE("MR3.2 A tick that the application did not pass through its own step makes the report not ok, in a game on this computer; the same match without it is ok") {
        for (const bool skip : {false, true}) {
            Application app;
            ASSERT_TRUE(app.init(local_config(0x02)));
            app.hud().dismiss_match_start_modal();
            for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
            if (skip) app.sim().tick();                                                // (a tick of the engine that nobody told the pass of)
            for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
            quit_match(app);
            ASSERT_TRUE(app.sim().is_match_over());
            ASSERT_EQ(app.match_report().ok, !skip);
            if (skip) ASSERT_FALSE(app.live_stats().stats().why.empty());
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("MR3.4 The pass reads the ants without the map: get_ant_snapshots is get_world_state().ants at every moment, the world state stale or not, and asking for it leaves the world state as right as before") {
        Bench b;
        b.engine.spawn_unit(0, sim::AntType::Worker, {10, 10});
        b.engine.spawn_unit(0, sim::AntType::Worker, {12, 10});
        b.engine.spawn_unit(1, sim::AntType::Bomber, {40, 40});
        const auto same = [](const sim::AntSnapshot& x, const sim::AntSnapshot& y) {
            return x.id == y.id && x.player_id == y.player_id && x.type == y.type && x.raw_type == y.raw_type && x.hp == y.hp && x.tile_x == y.tile_x && x.tile_y == y.tile_y && x.px == y.px && x.py == y.py &&
                   x.is_holding == y.is_holding && x.carried_points == y.carried_points && x.action == y.action && x.target_team_id == y.target_team_id;
        };
        for (int tick = 0; tick < 40; ++tick) {
            b.step();                                                                  // (the tick left the world state stale)
            const std::vector<sim::AntSnapshot> alone = b.engine.get_ant_snapshots();  // (a copy: the next call may make them again)
            const sim::WorldState& world = b.engine.get_world_state();
            ASSERT_EQ(alone.size(), world.ants.size());
            for (size_t i = 0; i < alone.size(); ++i) ASSERT_TRUE(same(alone[i], world.ants[i]));
            ASSERT_EQ(world.cells.size(), static_cast<size_t>(world.width) * world.height);       // (the world state was made whole, whatever was asked before it)
            const std::vector<sim::AntSnapshot>& again = b.engine.get_ant_snapshots();            // (now it is the world state's own)
            ASSERT_EQ(again.size(), world.ants.size());
            for (size_t i = 0; i < again.size(); ++i) ASSERT_TRUE(same(again[i], world.ants[i]));
        }
        b.engine.spawn_unit(1, sim::AntType::Worker, {41, 40});                         // (an ant added after the last tick: stale again, and the ants alone show it)
        ASSERT_EQ(b.engine.get_ant_snapshots().size(), b.engine.get_world_state().ants.size());
        ASSERT_EQ(b.engine.get_ant_snapshots().size(), size_t{4});
    } TEST_END();

    TEST_CASE("MR3.3 Counting changes nothing: the same match with and without it has the same state hash after every tick, the same file, the same cues in the same order (the pass takes nothing out of the engine's queues)") {
        const Matches& m = matches();
        const Played plain = play_bots("HELLRAISER.lvl", 1, 6000, false);
        ASSERT_FALSE(plain.bytes.empty());
        ASSERT_TRUE(plain.hashes == m.hellraiser.hashes);
        ASSERT_TRUE(plain.bytes == m.hellraiser.bytes);
        ASSERT_EQ(plain.hash, m.hellraiser.hash);
        ASSERT_EQ(plain.cues.size(), m.hellraiser.cues.size());
        bool same_cues = plain.cues.size() == m.hellraiser.cues.size();
        for (size_t i = 0; same_cues && i < plain.cues.size(); ++i) {
            const sim::AudioEvent &a = plain.cues[i], &b = m.hellraiser.cues[i];
            same_cues = a.sound_id == b.sound_id && a.world_x == b.world_x && a.world_y == b.world_y && a.priority == b.priority && a.target_player == b.target_player && a.owner == b.owner && a.stop == b.stop;
        }
        ASSERT_TRUE(same_cues);
        size_t booms = 0;
        for (const sim::AudioEvent& cue : plain.cues) booms += cue.sound_id == sim::SoundID::BombDetonate && !cue.stop ? 1u : 0u;
        ASSERT_TRUE(booms > 0);
        // the bomb's cue is still in the engine's queue after the pass has looked at the tick that made it
        Bench b;
        const uint32_t bomber = b.engine.spawn_unit(0, sim::AntType::Bomber, {10, 10});
        const uint32_t walker = b.engine.spawn_unit(1, sim::AntType::Worker, {12, 10});
        b.step(2);
        ASSERT_TRUE(b.engine.plant_bomb(bomber, {11, 10}));
        b.engine.trigger_bomb_detonation(walker, {11, 10});
        ASSERT_TRUE(b.engine.has_audio_event(sim::SoundID::BombDetonate));
        b.engine.tick();
        b.live.on_tick(b.engine);
        ASSERT_TRUE(b.engine.has_audio_event(sim::SoundID::BombDetonate));
        size_t left = 0;
        for (const sim::AudioEvent& cue : b.engine.poll_audio_events()) left += cue.sound_id == sim::SoundID::BombDetonate && !cue.stop ? 1u : 0u;
        ASSERT_EQ(left, size_t{1});
    } TEST_END();

    std::cout << "\nmatch report tests: " << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
