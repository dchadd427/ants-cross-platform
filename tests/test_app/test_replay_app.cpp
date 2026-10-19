// Tests of the replay recording in the application (docs/REPLAYS.md): a headless Application plays a game on this computer and takes part in a match of the network (the other machine is a bare
// NetGame over loopback sockets), and the file that it keeps of each is played again by the replay player on the engine alone. A recording stands for the match exactly when that replay reaches the
// state in which the match ended: the same state hash, every order at its turn.
#include "ants_app/application.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_app/hud.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "crowd_level.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <cmath>
#include <map>
#include <set>
#include <memory>
#include <random>
#include <string>
#include <thread>
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
using ants::sim::Command;
using ants::sim::CommandType;
namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

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

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

// A folder of this process alone (made new, removed with everything in it when the program ends): the settings of the applications that save their replays
class ScratchRoot {
public:
    ScratchRoot() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_replay_app_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
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

std::vector<uint32_t> ants_of(const sim::SimulationEngine& s, uint8_t player) {
    std::vector<uint32_t> out;
    for (const auto& a : s.get_world_state().ants) {
        if (a.player_id == player) out.push_back(a.id);
    }
    return out;
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

// What the application kept, read back
bool read_back(const Application& app, replay::Replay& out, std::string& error) {
    return !app.last_replay().empty() && replay::decode(app.last_replay().data(), app.last_replay().size(), out, error);
}

// The replay played on the engine alone reaches the state in which the match ended, or in which it was left (the application's own engine says what that is, and whether the rules had ended the match)
bool plays_to(const replay::Replay& rep, const sim::SimulationEngine& ended, std::string& why) {
    assets::LevelData level;
    if (!replay::load_map(rep.head, maps_dir(), level, why)) return false;
    const replay::Outcome outcome = replay::play(rep, level);
    if (!outcome.ok) {
        why = outcome.error;
        return false;
    }
    if (outcome.hash != ended.state_hash().total || outcome.match_over != ended.is_match_over()) {
        why = "it ends in another state than the match did";
        return false;
    }
    return true;
}

// (the button's own rectangle: the setup screen of the 16:9 picture has the buttons elsewhere than the original's 640 x 480 page)
void click_fog(Application& app, bool on) {
    const ButtonRect& rect = (on ? app.map_select().fog_on_button() : app.map_select().fog_off_button()).up_rect();
    const int32_t x = rect.x + 2;
    const int32_t y = rect.y + 2;
    app.map_select().handle_mouse_motion(x, y);
    app.map_select().handle_mouse_down(x, y, 1);
    app.map_select().handle_mouse_up(x, y, 1);
}

size_t count_files(const fs::path& folder) {
    std::error_code ec;
    size_t n = 0;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}

std::vector<uint8_t> slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The name that the application gives the file of a match on TINY that ends at `when` (the map, the date and time of this computer), and `tail` ("" or "-2") before the extension
std::string stamped_name(std::time_t when, const std::string& tail) {
    char stamp[32] = {0};
    if (const std::tm* local = std::localtime(&when)) std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
    return std::string("ants-TINY-") + stamp + tail + ".antsrep";
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

// A match of two on TINY: the bare machine hosts as Alice (seat 0), the application joins as Bob (seat 1). Returns the stepper once the match is begun on both machines (the "Get ready" dialog may
// still be up), nullptr when it could not be started.
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


// ---- watching a replay (ApplicationConfig::replay_path) ------------------------------------------------------------------------------------------------------------------

// A match of two on TINY made by hand: seat 0 (Ann) moves a worker now and then, seat 1 is the computer player "Bot (Medium)"; the Fog of War is ON in the file (a viewer shows the whole map).
// `quit_at_end`: seat 1 quits after the last tick, which ends the match by the rules (the file's end says so); otherwise the match is left standing (a recording that ends before its match did).
struct Handmade {
    std::vector<uint8_t> bytes;
    replay::Replay file;
    std::vector<uint64_t> hash_at;        // the state hash after turn t, for t = 0 .. turns (index 0: the start)
};

Handmade make_handmade(uint32_t turns, bool quit_at_end, uint32_t seed = 21, uint8_t mode = 0) {
    Handmade out;
    assets::LevelData level;
    const std::string path = maps_dir() + "/TINY.LVL";
    uint64_t map_hash = 0;
    if (!level.load_from_file(path) || !net::hash_file(path, map_hash)) return out;
    const uint8_t roster = 0x03;
    level = level.for_roster(roster);
    replay::Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "TINY.LVL";
    head.map_hash = map_hash;
    head.seed = seed;
    head.roster = roster;
    head.fog = true;
    head.names[0] = "Ann";
    head.names[1] = "Bot (Medium)";
    head.set_mode(mode);
    sim::SimulationEngine engine;
    engine.set_fog_of_war_enabled(true);
    engine.set_game_mode(static_cast<sim::GameMode>(mode));
    engine.init(level, seed, roster);
    replay::Recorder rec(head);
    out.hash_at.push_back(engine.state_hash().total);
    for (uint32_t turn = 0; turn < turns; ++turn) {
        if (turn % 60 == 5) {
            const std::vector<uint32_t> own = ants_of(engine, static_cast<uint8_t>(turn % 120 == 5 ? 0 : 1));
            if (!own.empty()) {
                Command c;
                c.type = CommandType::GroupMove;
                c.issuer = static_cast<uint8_t>(turn % 120 == 5 ? 0 : 1);
                c.tile_x = static_cast<int16_t>(8 + static_cast<int>(turn % 17));
                c.tile_y = static_cast<int16_t>(9 + static_cast<int>(turn % 13));
                c.ants = {own[0]};
                rec.on_command(c);
                engine.apply_command(c);
            }
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
        out.hash_at.push_back(engine.state_hash().total);
    }
    if (quit_at_end) {
        Command quit;
        quit.type = CommandType::Quit;
        quit.issuer = 1;
        rec.on_command(quit);
        engine.apply_command(quit);
    }
    std::string error;
    out.bytes = rec.finish(engine, error);
    if (!out.bytes.empty() && !replay::decode(out.bytes.data(), out.bytes.size(), out.file, error)) out.bytes.clear();
    return out;
}

// A match of four computer players on TREASURE (they fight, steal and light fires), made as the application records one: the bots' orders go to the engine and the recorder. `cues` gets every cue that
// the engine made, as the replay's camera sees them (the queue of the engine holds every seat's cues, whoever they are for).
struct BotMatch {
    std::vector<uint8_t> bytes;
    replay::Replay file;
    std::vector<sim::AudioEvent> cues;
};

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

// (seed 6: the Hard bots eat contested food first since v0.12.54, and in the 4,000 turns of seed 5 nobody died any more; seed 6 has the deaths, blows, thieves, fires and alarms
// that the cases ask for)
BotMatch make_bot_match(uint32_t turns, const char* map = "TREASURE.LVL", uint32_t seed = 6) {
    BotMatch out;
    assets::LevelData level;
    const std::string path = maps_dir() + "/" + map;
    uint64_t map_hash = 0;
    if (!level.load_from_file(path) || !net::hash_file(path, map_hash)) return out;
    replay::Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = map;
    head.map_hash = map_hash;
    head.seed = seed;
    head.roster = 0x0F;
    head.fog = false;
    for (uint8_t seat = 0; seat < 4; ++seat) head.names[seat] = "Bot (Hard)";
    sim::SimulationEngine engine;
    engine.init(level, seed, head.roster);
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
    for (uint32_t turn = 0; turn < turns && !engine.is_match_over(); ++turn) {
        engine.tick();
        rec.on_tick(engine);
        controller.on_tick(engine);
        for (const sim::AudioEvent& cue : engine.poll_audio_events()) out.cues.push_back(cue);
        engine.clear_news_events();
    }
    std::string error;
    out.bytes = rec.finish(engine, error);
    if (!out.bytes.empty() && !replay::decode(out.bytes.data(), out.bytes.size(), out.file, error)) out.bytes.clear();
    return out;
}

// An application that watches `bytes` (written to a file of its own folder)
struct Viewer {
    ScratchRoot root;
    Application app;
    bool ok{false};
    fs::path path;
    explicit Viewer(const std::vector<uint8_t>& bytes, const std::string& name = "match.antsrep", bool live = false) {
        path = root.path() / name;
        put(bytes);
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.lan_port = 0;
        cfg.maps_dir = maps_dir();
        cfg.replay_path = path.string();
        cfg.replay_live = live;
        ok = app.init(cfg);
    }
    // The file as the page leaves it: a newer copy of the match (the page writes it, then asks for Extend)
    void put(const std::vector<uint8_t>& bytes) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    int value(ReplayValue what) const { return app.replay_value(what); }
    void frames(int n, float dt = 0.05f) {
        for (int i = 0; i < n; ++i) app.update_simulation(dt);
    }
    uint32_t turn() const { return static_cast<uint32_t>(app.replay_value(ReplayValue::Turn)); }
    ReplayState state() const { return app.replay_state(); }
};

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=== Replays in the application ===\n";

    TEST_CASE("RA1.1 A game on this computer is recorded from its first tick: the HUD's order, the computer player's orders and the quit stand at their turns, and the file plays out to the state in which the match ended") {
        Application app;
        const ApplicationConfig cfg = local_config(0x02);
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.recorder() != nullptr);                                        // (the match began: the recording with it)
        ASSERT_TRUE(app.last_replay().empty());
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 600; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.recorder()->turns(), app.sim().current_tick());                  // (every tick that ran was seen)
        const std::vector<uint32_t> own = ants_of(app.sim(), 0);
        ASSERT_FALSE(own.empty());
        app.hud().select_ant(own[0]);
        app.hud().stop_selected(app.sim());                                            // the HUD's order: it reaches the engine through the sink that the recording sees
        const uint32_t stop_turn = static_cast<uint32_t>(app.sim().current_tick());
        for (int i = 0; i < 20; ++i) app.update_simulation(0.05f);
        ASSERT_FALSE(app.sim().is_match_over());
        quit_match(app);                                                               // the quit is the last command, given after the last tick
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_TRUE(app.recorder() == nullptr);                                        // (the match ended: the file was made)

        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.complete);
        ASSERT_TRUE(rep.match_over);
        ASSERT_EQ(rep.total_turns, app.sim().current_tick());
        ASSERT_TRUE(rep.final_hash == app.sim().state_hash().total);
        ASSERT_EQ(rep.head.venue, "local game");
        ASSERT_EQ(rep.head.map_name, "TINY.LVL");
        ASSERT_EQ(rep.head.roster, 0x03);                                              // the player and the bot
        ASSERT_EQ(rep.head.seed, cfg.random_seed);
        ASSERT_EQ(rep.head.recorder_seat, 0);
        ASSERT_FALSE(rep.head.fog);
        ASSERT_FALSE(rep.head.teams.set);
        ASSERT_TRUE(rep.head.mode == 0 && rep.head.sim_rules == replay::kSimRules && app.sim().game_mode() == sim::GameMode::HighestScore);       // (no --game-mode: the original's game, as every file before the modes)
        ASSERT_EQ(rep.head.engine_rules, net::kProtocolVersion);
        ASSERT_EQ(rep.head.names[1], "Bot (Medium)");
        bool saw_stop = false;
        bool saw_bot = false;
        for (const replay::TimedCommand& tc : rep.commands) {
            if (tc.command.type == CommandType::Stop && tc.command.issuer == 0) {
                saw_stop = true;
                ASSERT_EQ(tc.turn, stop_turn);                                         // (given after that many ticks, before the next one)
            }
            if (tc.command.issuer == 1) saw_bot = true;
        }
        ASSERT_TRUE(saw_stop);
        ASSERT_TRUE(saw_bot);                                                          // the bot gave orders in 30 s, and each was recorded
        ASSERT_TRUE(rep.commands.back().command.type == CommandType::Quit);
        ASSERT_EQ(rep.commands.back().turn, rep.total_turns);
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        ASSERT_FALSE(app.last_replay_name().empty());
        ASSERT_TRUE(app.last_replay_name().rfind("ants-TINY-", 0) == 0);
        ASSERT_TRUE(app.last_replay_name().size() > 8 && app.last_replay_name().compare(app.last_replay_name().size() - 8, 8, ".antsrep") == 0);
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.2 A match that runs out its clock, with teams made at the start: the file ends at the tick that ended it and plays out to the same state, teams included") {
        Application app;
        ApplicationConfig cfg = local_config(0x06);                                    // seats 1 and 2 are bots
        cfg.teams.set = true;                                                          // the player and the first bot are a team, the second bot plays alone
        cfg.teams.a = 0;
        cfg.teams.b = 1;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.recorder() != nullptr);
        ASSERT_TRUE(app.recorder()->replay().head.teams.set);
        app.hud().dismiss_match_start_modal();
        int steps = 0;
        while (!app.sim().is_match_over() && steps < 40000) {
            app.update_simulation(0.05f);
            ++steps;
        }
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_TRUE(app.recorder() == nullptr);
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.complete);
        ASSERT_TRUE(rep.head.teams.set && rep.head.teams.a == 0 && rep.head.teams.b == 1);
        ASSERT_EQ(rep.head.roster, 0x07);
        ASSERT_TRUE(rep.total_turns == app.sim().current_tick() || rep.total_turns == app.sim().current_tick() + 1);     // (the tick that ends a match on the clock does not count itself in the engine's counter)
        ASSERT_EQ(rep.total_turns, static_cast<uint32_t>(steps));                      // (one tick for each step)
        ASSERT_TRUE(rep.commands.empty() || rep.commands.back().command.type != CommandType::Quit);       // (nobody quit: the clock ended it)
        ASSERT_TRUE(rep.commands.size() > 5);                                          // (the bots played the whole match)
        ASSERT_FALSE(rep.hashes.empty());
        ASSERT_EQ(rep.hashes.size(), rep.total_turns / rep.head.hash_period);
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.3 The names in the file are the ones that were typed or given: --name, a team's name, a bot's; what the program makes of the system user stays out") {
        {
            Application app;
            ApplicationConfig cfg = local_config(0x02);
            ASSERT_TRUE(app.init(cfg));                                                // no --name: the name is the system user's ("user@machine")
            ASSERT_TRUE(app.recorder() != nullptr);
            ASSERT_TRUE(app.recorder()->replay().head.names[0].empty());
            ASSERT_EQ(app.recorder()->replay().head.names[1], "Bot (Medium)");
            app.shutdown();
        }
        {
            Application app;
            ApplicationConfig cfg = local_config(0x02);
            cfg.player_name = "Dave";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.recorder()->replay().head.names[0], "Dave");
            app.shutdown();
        }
        {
            Application app;
            ApplicationConfig cfg = local_config(0x02);
            cfg.team_names[0] = "Alice";
            cfg.team_names[1] = "Robot One";                                           // an explicit name beats the bot's
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.recorder()->replay().head.names[0], "Alice");
            ASSERT_EQ(app.recorder()->replay().head.names[1], "Robot One");
            ASSERT_TRUE(app.recorder()->replay().head.names[2].empty());               // (a seat that does not play has none)
            app.shutdown();
        }
        {                                                                              // a name that holds a letter outside ASCII, a delete and a tab: the file keeps printable ASCII only (every other byte becomes a '?')
            Application app;
            ApplicationConfig cfg = local_config(0x02);
            cfg.player_name = "Zo\xC3\xAB\x7F\tx";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.recorder()->replay().head.names[0], "Zo????x");
            app.hud().dismiss_match_start_modal();
            for (int i = 0; i < 60; ++i) app.update_simulation(0.05f);
            quit_match(app);
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));                                     // (and the file reads back)
            ASSERT_EQ(rep.head.names[0], "Zo????x");
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("RA1.4 The desktop game keeps the file beside its settings: a folder `replays`, the name of the match, the bytes that the program holds; the next match takes the offer back; a file that is there is never replaced (a match that ends in the same second as another gets -2, then -3) and the name that the application reports is the one that was written") {
        ScratchRoot scratch;
        ASSERT_FALSE(scratch.path().empty());
        Application app;
        ApplicationConfig cfg = local_config(0x02);
        cfg.settings_path = (scratch.path() / "settings.ini").string();
        ASSERT_TRUE(app.init(cfg));
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 120; ++i) app.update_simulation(0.05f);
        const fs::path folder = scratch.path() / "replays";
        ASSERT_FALSE(fs::exists(folder));                                              // (nothing is written before the match ends)
        quit_match(app);
        ASSERT_TRUE(fs::is_directory(folder));
        ASSERT_EQ(count_files(folder), 1u);
        ASSERT_TRUE(fs::is_regular_file(folder / app.last_replay_name()));
        ASSERT_TRUE(slurp(folder / app.last_replay_name()) == app.last_replay());
        const std::string first_name = app.last_replay_name();
        const std::vector<uint8_t> first_bytes = app.last_replay();

        // a file with a name that the next match would get is there already, for every second that the match can end in: the match must take the next free name, and say which it took
        const auto occupy = [&](const std::string& tail) {
            const std::time_t now = std::time(nullptr);
            for (int ahead = -1; ahead <= 8; ++ahead) {
                const std::string name = stamped_name(now + ahead, tail);
                if (!fs::exists(folder / name)) std::ofstream(folder / name, std::ios::binary) << "decoy " << name;
            }
        };
        const auto intact = [&](const std::vector<std::string>& except) {              // every file that is not one of the matches' own is as it was made
            std::error_code ec;
            bool all = true;
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (std::find(except.begin(), except.end(), name) != except.end()) continue;
                const std::vector<uint8_t> bytes = slurp(it->path());
                const std::string want = "decoy " + name;
                all = all && std::string(bytes.begin(), bytes.end()) == want;
            }
            return all;
        };
        const auto next_match = [&](int steps) {
            app.return_to_map_select();
            ASSERT_TRUE(app.start_game(cfg.default_map_path));
            ASSERT_TRUE(app.last_replay().empty());                                    // (the old file is not offered any more, and a new recording is on)
            ASSERT_TRUE(app.last_replay_name().empty());
            ASSERT_TRUE(app.recorder() != nullptr);
            app.hud().dismiss_match_start_modal();
            for (int i = 0; i < steps; ++i) app.update_simulation(0.05f);
            quit_match(app);
        };
        occupy("");
        const size_t before_second = count_files(folder);
        next_match(140);                                                               // (a longer match: its file is not the first one's)
        const std::string second_name = app.last_replay_name();
        ASSERT_TRUE(second_name.size() > 10 && second_name.compare(second_name.size() - 10, 10, "-2.antsrep") == 0);
        ASSERT_EQ(count_files(folder), before_second + 1);                             // (one more file: nothing was replaced)
        ASSERT_TRUE(app.last_replay() != first_bytes);
        ASSERT_TRUE(slurp(folder / second_name) == app.last_replay());                 // (the name that is reported is the file that holds this match)
        ASSERT_TRUE(slurp(folder / first_name) == first_bytes);                        // (the first is still there, whatever the second was called)
        ASSERT_TRUE(intact({first_name, second_name}));
        const std::vector<uint8_t> second_bytes = app.last_replay();
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(plays_to(rep, app.sim(), why));

        occupy("-2");                                                                  // the "-2" of every second is taken as well: the third match gets "-3"
        const size_t before_third = count_files(folder);
        next_match(160);
        const std::string third_name = app.last_replay_name();
        ASSERT_TRUE(third_name.size() > 10 && third_name.compare(third_name.size() - 10, 10, "-3.antsrep") == 0);
        ASSERT_EQ(count_files(folder), before_third + 1);
        ASSERT_TRUE(slurp(folder / third_name) == app.last_replay());
        ASSERT_TRUE(slurp(folder / second_name) == second_bytes && slurp(folder / first_name) == first_bytes);
        ASSERT_TRUE(intact({first_name, second_name, third_name}));
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.5 A run with no settings file (a headless run) writes nothing to disk, not even a folder of the current directory, and still keeps the file in memory") {
        const bool had_folder = fs::exists("replays");
        const size_t had_files = had_folder ? count_files("replays") : 0;
        Application app;
        ASSERT_TRUE(app.init(local_config(0x02)));
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 120; ++i) app.update_simulation(0.05f);
        quit_match(app);
        ASSERT_FALSE(app.last_replay().empty());                                       // (there is no folder to write to: kept in memory only)
        ASSERT_EQ(fs::exists("replays"), had_folder);
        ASSERT_EQ(had_folder ? count_files("replays") : size_t{0}, had_files);
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.6 A match that ends before its first tick leaves no file and no complaint; the direct start (--map, no setup screen) is recorded like the others") {
        {
            Application app;
            ASSERT_TRUE(app.init(local_config(0x02)));
            app.hud().dismiss_match_start_modal();                                     // (the dialog takes every key while it is up)
            ASSERT_EQ(app.sim().current_tick(), 0u);                                   // no tick has run
            quit_match(app);                                                           // the quit ends the match before its first tick
            ASSERT_TRUE(app.sim().is_match_over());
            ASSERT_TRUE(app.last_replay().empty());                                    // (no turn ran: nothing to keep)
            ASSERT_TRUE(app.recorder() == nullptr);
            app.shutdown();
        }
        {
            Application app;
            ApplicationConfig cfg = local_config(0x02);
            cfg.start_in_map_select = false;                                           // --map without the setup screen: the match is running at once
            cfg.play_at_once = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.recorder() != nullptr);
            for (int i = 0; i < 200; ++i) app.update_simulation(0.05f);
            quit_match(app);
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_EQ(rep.head.venue, "local game");
            ASSERT_TRUE(plays_to(rep, app.sim(), why));
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("RA1.11 A game on this computer on an Extended map (100 x 100, rules 1) is recorded like the others and plays out to the state in which it was left; the same file is watched in the application when the map is in the maps folder, and not by a reading of the network's (Classic maps only)") {
        ScratchRoot root;
        const std::string big = (root.path() / "Big.lvl").string();
        ASSERT_TRUE(ants_test::write_crowd_file(big, ants_test::crowd_level()));
        std::error_code copied;
        fs::copy_file(maps_dir() + "/TINY.LVL", root.path() / "TINY.LVL", copied);        // (a maps folder of the game holds Classic maps beside it; the setup screen's list is theirs)
        ASSERT_FALSE(copied);
        replay::Replay rep;
        std::vector<uint8_t> bytes;
        uint64_t ended_hash = 0;
        {
            Application app;
            ApplicationConfig cfg = local_config(0x02);                                // one bot
            cfg.start_in_map_select = false;                                           // --map: the match is running at once
            cfg.play_at_once = false;
            cfg.maps_dir = root.path().string();
            cfg.default_map_path = big;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.recorder() != nullptr);
            for (int i = 0; i < 400; ++i) app.update_simulation(0.05f);
            quit_match(app);
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_EQ(rep.head.venue, "local game");
            ASSERT_EQ(rep.head.sim_rules, replay::kSimRules);                          // (the rules number is the build's: an older build says only that the map cannot be loaded)
            bytes = app.last_replay();
            ended_hash = app.sim().state_hash().total;
            assets::LevelData level;
            ASSERT_FALSE(replay::load_map(rep.head, root.path().string(), level, why));     // (a reading of the network: Classic maps only)
            ASSERT_TRUE(why.find("Extended maps can't be played online yet") != std::string::npos);
            ASSERT_TRUE(replay::load_map(rep.head, root.path().string(), level, why, assets::LevelLimits::for_game(true)));
            const replay::Outcome outcome = replay::play(rep, level);
            ASSERT_TRUE(outcome.ok && outcome.hash == ended_hash);
            app.shutdown();
        }
        {
            const fs::path file = root.path() / "match.antsrep";
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out.close();
            Application viewer;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.lan_port = 0;
            cfg.maps_dir = root.path().string();
            cfg.replay_path = file.string();
            ASSERT_TRUE(viewer.init(cfg));
            ASSERT_EQ(viewer.sim().grid().width(), 100u);
            ASSERT_EQ(viewer.sim().grid().height(), 100u);
            for (int i = 0; i < 100; ++i) viewer.update_simulation(0.05f);
            ASSERT_TRUE(viewer.replay_value(ReplayValue::Turn) > 0);
            viewer.shutdown();
        }
    } TEST_END();

    TEST_CASE("RA1.7 A match that is left before its end keeps its file when the player gave an order or a minute went by (the computer players' orders do not count): it says that the match was not over and plays out to the state at which it was left; one that is left at once leaves nothing") {
        const auto run = [](Application& app, int steps) {
            app.hud().dismiss_match_start_modal();
            for (int i = 0; i < steps; ++i) app.update_simulation(0.05f);
        };
        {                                                                              // alone, no order, just under a minute: nothing
            Application app;
            ASSERT_TRUE(app.init(local_config(0x00)));
            run(app, 1190);
            ASSERT_TRUE(app.recorder() != nullptr);
            ASSERT_EQ(app.recorder()->commands(), 0u);
            ASSERT_EQ(app.recorder()->turns(), 1190u);
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            ASSERT_TRUE(app.last_replay().empty());
            app.shutdown();
        }
        {                                                                              // a computer player gives orders, the player none, just under a minute: nothing (its orders are not the player's)
            Application app;
            ASSERT_TRUE(app.init(local_config(0x02)));
            run(app, 600);
            ASSERT_FALSE(app.sim().is_match_over());
            ASSERT_TRUE(app.recorder() != nullptr);
            ASSERT_TRUE(app.recorder()->commands() > 0);                               // (the bot ordered, and each order is in the recording)
            ASSERT_EQ(app.recorder()->own_commands(), 0u);
            ASSERT_EQ(app.recorder()->turns(), 600u);
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            ASSERT_TRUE(app.last_replay().empty());
            app.shutdown();
        }
        {                                                                              // the same, a minute: kept, with the bot's orders in it
            Application app;
            ASSERT_TRUE(app.init(local_config(0x02)));
            run(app, 1200);
            ASSERT_FALSE(app.sim().is_match_over());
            ASSERT_EQ(app.recorder()->own_commands(), 0u);
            app.return_to_map_select();
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_TRUE(rep.complete && !rep.match_over && rep.total_turns == 1200u && !rep.commands.empty());
            ASSERT_TRUE(plays_to(rep, app.sim(), why));
            app.shutdown();
        }
        {                                                                              // alone, no order, a minute: kept
            Application app;
            ASSERT_TRUE(app.init(local_config(0x00)));
            run(app, 1200);
            ASSERT_FALSE(app.sim().is_match_over());
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_TRUE(rep.complete);
            ASSERT_FALSE(rep.match_over);
            ASSERT_TRUE(rep.commands.empty());
            ASSERT_EQ(rep.total_turns, 1200u);
            ASSERT_EQ(rep.hashes.size(), 12u);
            ASSERT_TRUE(rep.final_hash == app.sim().state_hash().total);
            ASSERT_TRUE(plays_to(rep, app.sim(), why));
            app.shutdown();
        }
        {                                                                              // an order, a few seconds, a bot: kept, with the order
            Application app;
            ASSERT_TRUE(app.init(local_config(0x02)));
            run(app, 40);
            const std::vector<uint32_t> own = ants_of(app.sim(), 0);
            ASSERT_FALSE(own.empty());
            app.hud().select_ant(own[0]);
            app.hud().stop_selected(app.sim());
            run(app, 20);
            app.return_to_map_select();
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_TRUE(rep.complete && !rep.match_over);
            ASSERT_EQ(rep.total_turns, 60u);
            bool saw_stop = false;
            for (const replay::TimedCommand& tc : rep.commands) saw_stop = saw_stop || (tc.command.type == CommandType::Stop && tc.command.issuer == 0 && tc.turn == 40);
            ASSERT_TRUE(saw_stop);
            ASSERT_TRUE(plays_to(rep, app.sim(), why));
            app.shutdown();
        }
        {                                                                              // the program ends in the middle of a match: the file is written beside the settings
            ScratchRoot scratch;
            ASSERT_FALSE(scratch.path().empty());
            Application app;
            ApplicationConfig cfg = local_config(0x00);
            cfg.settings_path = (scratch.path() / "settings.ini").string();
            ASSERT_TRUE(app.init(cfg));
            run(app, 1210);
            ASSERT_FALSE(fs::exists(scratch.path() / "replays"));
            app.shutdown();
            ASSERT_EQ(count_files(scratch.path() / "replays"), 1u);
            ASSERT_TRUE(slurp(scratch.path() / "replays" / app.last_replay_name()) == app.last_replay());
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_TRUE(rep.complete && !rep.match_over && rep.total_turns == 1210u);
        }
    } TEST_END();

    TEST_CASE("RA1.8 Fog of War switched on at the setup screen is in the file, and the match plays out to the same state with it") {
        Application app;
        ApplicationConfig cfg = local_config(0x00);                                    // (a computer player and fog do not go together)
        cfg.play_at_once = false;
        ASSERT_TRUE(app.init(cfg));
        click_fog(app, true);
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_TRUE(app.start_game(cfg.default_map_path));
        ASSERT_TRUE(app.recorder() != nullptr);
        ASSERT_TRUE(app.recorder()->replay().head.fog);
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 60; ++i) app.update_simulation(0.05f);
        const std::vector<uint32_t> own = ants_of(app.sim(), 0);
        ASSERT_FALSE(own.empty());
        app.hud().select_ant(own[0]);
        app.hud().stop_selected(app.sim());
        for (int i = 0; i < 40; ++i) app.update_simulation(0.05f);
        app.return_to_map_select();
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.head.fog);
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.9 The file's name is safe whatever the map is called: a map file with spaces, brackets and a mark in its name gives letters, digits, '_' and '-' and the date and time of the computer, while the file's header keeps the map's own name") {
        ScratchRoot scratch;
        ASSERT_FALSE(scratch.path().empty());
        const fs::path odd = scratch.path() / "Zwei Wege [v2]!.LVL";                    // (a name that the protocol takes: printable, no separator)
        std::error_code ec;
        fs::copy_file(maps_dir() + "/TINY.LVL", odd, ec);
        ASSERT_FALSE(ec);
        Application app;
        ApplicationConfig cfg = local_config(0x02);
        cfg.default_map_path = odd.string();
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.recorder() != nullptr);
        ASSERT_EQ(app.recorder()->replay().head.map_name, "Zwei Wege [v2]!.LVL");      // (the map is found by this name)
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 120; ++i) app.update_simulation(0.05f);
        quit_match(app);
        ASSERT_FALSE(app.last_replay().empty());
        const std::string name = app.last_replay_name();
        const std::string head = "ants-Zwei_Wege__v2__-";
        const std::string tail = ".antsrep";
        ASSERT_EQ(name.size(), head.size() + 15 + tail.size());                        // "YYYYMMDD-HHMMSS" between them
        ASSERT_EQ(name.compare(0, head.size(), head), 0);
        ASSERT_EQ(name.compare(name.size() - tail.size(), tail.size(), tail), 0);
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            ASSERT_TRUE(safe);
        }
        for (size_t i = 0; i < 15; ++i) {
            const char c = name[head.size() + i];
            ASSERT_TRUE(i == 8 ? c == '-' : (c >= '0' && c <= '9'));
        }
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA1.10 --game-mode 187 on this computer: the game plays by it from the first tick (no food on the map), the file carries game mode 1 and the rules number 2 and plays out to the state in which the match ended, and the next game of the same program plays by it too; a game without the option stays the original's") {
        Application app;
        ApplicationConfig cfg = local_config(0x02);
        cfg.game_mode = 1;
        cfg.game_mode_given = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.recorder() != nullptr);
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187);
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        ASSERT_TRUE(app.recorder()->replay().head.mode == 1 && app.recorder()->replay().head.sim_rules == replay::kSimRulesMode187);
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 400; ++i) app.update_simulation(0.05f);
        const std::vector<uint32_t> own = ants_of(app.sim(), 0);
        ASSERT_FALSE(own.empty());
        app.hud().select_ant(own[0]);
        app.hud().stop_selected(app.sim());
        for (int i = 0; i < 20; ++i) app.update_simulation(0.05f);
        quit_match(app);
        ASSERT_TRUE(app.sim().is_match_over());
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.complete && rep.match_over);
        ASSERT_TRUE(rep.head.mode == 1 && rep.head.sim_rules == replay::kSimRulesMode187 && replay::plays_here(rep.head));
        ASSERT_TRUE(rep.final_hash == app.sim().state_hash().total);
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        // the next game of the program: back to the setup screen and START again (a "Play again" keeps the engine's mode, a start from the setup screen takes the option's)
        app.return_to_map_select();
        ASSERT_TRUE(app.start_game(cfg.default_map_path));
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187);
        ASSERT_TRUE(app.recorder() != nullptr && app.recorder()->replay().head.mode == 1);
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        app.shutdown();
        // no option: the original's game, with food on the map
        Application plain;
        ASSERT_TRUE(plain.init(local_config(0x02)));
        ASSERT_TRUE(plain.sim().game_mode() == sim::GameMode::HighestScore);
        bool food = false;
        for (const auto& cell : plain.sim().get_world_state().cells) food = food || cell.is_food;
        ASSERT_TRUE(food);
        ASSERT_TRUE(plain.recorder() != nullptr && plain.recorder()->replay().head.mode == 0 && plain.recorder()->replay().head.sim_rules == replay::kSimRules);
        plain.shutdown();
    } TEST_END();

    TEST_CASE("RA1.10b --game-mode 187 with a map and bots (the game starts at once, with no setup screen): the engine is made with the mode before its first init, so the first tick has no food and the file says game mode 1 and rules 2; no option: the original's game") {
        Application app;
        ApplicationConfig cfg = local_config(0x02);
        cfg.start_in_map_select = false;                                               // (init makes the engine itself: the setup screen's START, which sets the mode in load_match, never comes)
        cfg.game_mode = 1;
        cfg.game_mode_given = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.state() == AppState::Playing);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187);
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        ASSERT_TRUE(app.sim().get_world_state().flower_droppers.empty());
        ASSERT_TRUE(app.recorder() != nullptr && app.recorder()->replay().head.mode == 1 && app.recorder()->replay().head.sim_rules == replay::kSimRulesMode187);
        app.shutdown();
        Application plain;
        ApplicationConfig cfg0 = local_config(0x02);
        cfg0.start_in_map_select = false;
        ASSERT_TRUE(plain.init(cfg0));
        ASSERT_TRUE(plain.sim().game_mode() == sim::GameMode::HighestScore);
        bool food = false;
        for (const auto& cell : plain.sim().get_world_state().cells) food = food || cell.is_food;
        ASSERT_TRUE(food);
        ASSERT_TRUE(plain.recorder() != nullptr && plain.recorder()->replay().head.mode == 0 && plain.recorder()->replay().head.sim_rules == replay::kSimRules);
        plain.shutdown();
    } TEST_END();

    TEST_CASE("RA2.1 A match of the network: the machine's file holds every machine's orders and the quit, names and seats as the Start gave them, and plays out to the state of the match") {
        Peer host;
        Application app;
        const std::unique_ptr<Duo> duo = start_duo(app, host, 31337);
        ASSERT_TRUE(duo != nullptr);
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_TRUE(app.recorder() != nullptr);                                        // (the Start was loaded: the recording began)
        duo->step(8000);                                                               // the "get ready" dialog takes every key for its five seconds
        ASSERT_FALSE(app.hud().is_modal_open());
        ASSERT_TRUE(app.sim().current_tick() > 20);
        ASSERT_EQ(app.recorder()->turns(), app.sim().current_tick());
        // the host's order, and the player's own (the HUD's sink is the NetGame here)
        const std::vector<uint32_t> hosts = ants_of(host.sim, 0);
        ASSERT_FALSE(hosts.empty());
        Command order;
        order.type = CommandType::GroupMove;
        order.issuer = 0;
        order.ants = {hosts[0]};
        const auto* hill = host.sim.grid().find_anthill(0);
        ASSERT_TRUE(hill != nullptr);
        order.tile_x = static_cast<int16_t>(hill->x + 6);
        order.tile_y = static_cast<int16_t>(hill->y + 6);
        host.net.submit(order);
        duo->step(400);
        const std::vector<uint32_t> own = ants_of(app.sim(), 1);
        ASSERT_FALSE(own.empty());
        app.hud().select_ant(own[0]);
        app.hud().stop_selected(app.sim());
        duo->step(400);
        quit_match(app);
        ASSERT_TRUE(duo->until([&]() { return app.sim().is_match_over() && host.sim.is_match_over(); }, 4000));
        ASSERT_TRUE(app.recorder() == nullptr);

        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.complete && rep.match_over);
        ASSERT_EQ(rep.head.venue, "network game");
        ASSERT_EQ(rep.head.recorder_seat, 1);
        ASSERT_EQ(rep.head.roster, 0x03);
        ASSERT_EQ(rep.head.seed, 31337u);
        ASSERT_EQ(rep.head.names[0], "Alice");
        ASSERT_EQ(rep.head.names[1], "Bob");
        ASSERT_EQ(rep.head.map_name, "TINY.LVL");
        bool saw_host_order = false;
        bool saw_stop = false;
        for (const replay::TimedCommand& tc : rep.commands) {
            if (tc.command.type == CommandType::GroupMove && tc.command.issuer == 0) saw_host_order = true;
            if (tc.command.type == CommandType::Stop && tc.command.issuer == 1) saw_stop = true;
        }
        ASSERT_TRUE(saw_host_order);
        ASSERT_TRUE(saw_stop);
        ASSERT_TRUE(rep.commands.back().command.type == CommandType::Quit && rep.commands.back().command.issuer == 1);
        ASSERT_TRUE(rep.commands.back().turn < rep.total_turns);                        // (the quit rides a turn of the network: it is applied before that turn's tick)
        ASSERT_TRUE(rep.total_turns == app.sim().current_tick() || rep.total_turns == app.sim().current_tick() + 1);     // (the tick of the turn that ended the match does not count itself in the engine's counter)
        ASSERT_TRUE(rep.final_hash == app.sim().state_hash().total);
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA2.4 A match of the network in a room of game mode 187: the Start carries the mode, the machine that joined makes its engine with it before the first init (no food, the same state as the host's), its file has game mode 1 and the rules number 2 and plays out to the state of the match") {
        Peer host;
        Application app;
        const std::unique_ptr<Duo> duo = start_duo(app, host, 4711, 1);
        ASSERT_TRUE(duo != nullptr);
        ASSERT_EQ(app.net()->start_info().mode, 1);
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187 && host.sim.game_mode() == sim::GameMode::Kills187);
        ASSERT_TRUE(app.recorder() != nullptr && app.recorder()->replay().head.mode == 1 && app.recorder()->replay().head.sim_rules == replay::kSimRulesMode187);
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        duo->step(8000);
        ASSERT_TRUE(app.sim().current_tick() > 20);
        const std::vector<uint32_t> own = ants_of(app.sim(), 1);
        ASSERT_FALSE(own.empty());
        app.hud().select_ant(own[0]);
        app.hud().stop_selected(app.sim());
        duo->step(400);
        quit_match(app);
        ASSERT_TRUE(duo->until([&]() { return app.sim().is_match_over() && host.sim.is_match_over(); }, 4000));
        ASSERT_TRUE(app.recorder() == nullptr);
        replay::Replay rep;
        std::string why;
        ASSERT_TRUE(read_back(app, rep, why));
        ASSERT_TRUE(rep.complete && rep.match_over && rep.head.venue == "network game");
        ASSERT_TRUE(rep.head.mode == 1 && rep.head.sim_rules == replay::kSimRulesMode187 && replay::plays_here(rep.head));
        ASSERT_TRUE(rep.final_hash == app.sim().state_hash().total && app.sim().state_hash() == host.sim.state_hash());
        ASSERT_TRUE(plays_to(rep, app.sim(), why));
        app.shutdown();
    } TEST_END();

    TEST_CASE("RA2.2 A match of the network that the player leaves before its end keeps the machine's file when the player gave an order (it says that the match was not over and plays out to the state at which the player left); one left with nothing done by the player leaves none, whatever the others did; the recording does not outlive the session") {
        {
            Peer host;
            Application app;
            const std::unique_ptr<Duo> duo = start_duo(app, host, 4242);
            ASSERT_TRUE(duo != nullptr);
            ASSERT_TRUE(app.recorder() != nullptr);
            duo->step(6200);
            ASSERT_TRUE(app.recorder()->turns() > 10);
            ASSERT_EQ(app.recorder()->commands(), 0u);
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            ASSERT_TRUE(app.last_replay().empty());                                    // (a few seconds, no order: nothing to keep)
            app.shutdown();
        }
        {                                                                              // the other machine gives an order, the player none: nothing (the file is the player's)
            Peer host;
            Application app;
            const std::unique_ptr<Duo> duo = start_duo(app, host, 4244);
            ASSERT_TRUE(duo != nullptr);
            duo->step(6200);
            const std::vector<uint32_t> hosts = ants_of(host.sim, 0);
            ASSERT_FALSE(hosts.empty());
            const auto* hill = host.sim.grid().find_anthill(0);
            ASSERT_TRUE(hill != nullptr);
            Command order;
            order.type = CommandType::GroupMove;
            order.issuer = 0;
            order.ants = {hosts[0]};
            order.tile_x = static_cast<int16_t>(hill->x + 6);
            order.tile_y = static_cast<int16_t>(hill->y + 6);
            host.net.submit(order);
            duo->step(600);
            ASSERT_TRUE(app.recorder() != nullptr);
            ASSERT_TRUE(app.recorder()->commands() > 0);                               // (the host's order came through the turns and is in the recording)
            ASSERT_EQ(app.recorder()->own_commands(), 0u);
            ASSERT_TRUE(app.recorder()->turns() < 1200u);
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            ASSERT_TRUE(app.last_replay().empty());
            app.shutdown();
        }
        {
            Peer host;
            Application app;
            const std::unique_ptr<Duo> duo = start_duo(app, host, 4243);
            ASSERT_TRUE(duo != nullptr);
            duo->step(6200);
            const std::vector<uint32_t> own = ants_of(app.sim(), 1);
            ASSERT_FALSE(own.empty());
            app.hud().select_ant(own[0]);
            app.hud().stop_selected(app.sim());
            duo->step(600);
            ASSERT_FALSE(app.sim().is_match_over());
            app.return_to_map_select();
            ASSERT_TRUE(app.recorder() == nullptr);
            replay::Replay rep;
            std::string why;
            ASSERT_TRUE(read_back(app, rep, why));
            ASSERT_TRUE(rep.complete && !rep.match_over);
            ASSERT_EQ(rep.head.venue, "network game");
            ASSERT_EQ(rep.head.recorder_seat, 1);
            ASSERT_EQ(rep.head.seed, 4243u);
            ASSERT_EQ(rep.total_turns, app.sim().current_tick());
            bool saw_stop = false;
            for (const replay::TimedCommand& tc : rep.commands) saw_stop = saw_stop || (tc.command.type == CommandType::Stop && tc.command.issuer == 1);
            ASSERT_TRUE(saw_stop);
            ASSERT_TRUE(plays_to(rep, app.sim(), why));
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("RA2.3 The match of the network that begins takes back the offer of the file of the match that ended before it") {
        Application app;
        ASSERT_TRUE(app.init(local_config(0x02)));
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 120; ++i) app.update_simulation(0.05f);
        quit_match(app);
        ASSERT_FALSE(app.last_replay().empty());
        Peer host;
        const std::unique_ptr<Duo> duo = start_duo(app, host, 99);                    // (the same application joins a room)
        ASSERT_TRUE(duo != nullptr);
        ASSERT_TRUE(app.recorder() != nullptr);
        ASSERT_TRUE(app.last_replay().empty());
        ASSERT_TRUE(app.last_replay_name().empty());
        app.shutdown();
    } TEST_END();


    // ---- watching a replay ----

    TEST_CASE("RA7.1 A recording is watched: the match of the file plays turn by turn on the engine, whole map shown, names kept; it ends in the recorded state and the results open (a match that the rules ended)") {
        const Handmade made = make_handmade(700, true);
        ASSERT_FALSE(made.bytes.empty());
        ASSERT_TRUE(made.file.complete && made.file.match_over);
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok);
        ASSERT_TRUE(v.app.replay_mode() && v.app.replay_failure() == ReplayFailure::None);
        ASSERT_TRUE(v.state() == ReplayState::Playing && v.turn() == 0);
        ASSERT_TRUE(v.app.state() == AppState::Playing && v.app.hud().chat_takes_text() && !v.app.web_chat_available() && !v.app.web_chat_do(1, 'a'));      // (a phone's keyboard: a recording has no chat line to type in)
        ASSERT_EQ(v.app.replay_value(ReplayValue::Total), 700);
        ASSERT_EQ(v.app.replay_value(ReplayValue::Speed), 100);
        ASSERT_FALSE(v.app.sim().is_fog_of_war_enabled());                              // (the file has the fog on: a viewer sees everything, and the fog is no part of any hash)
        ASSERT_EQ(v.app.sim().get_player_name(0), std::string("Ann"));
        ASSERT_EQ(v.app.sim().get_player_name(1), std::string("Bot (Medium)"));
        ASSERT_TRUE(v.app.recorder() == nullptr && v.app.last_replay().empty());        // (a viewer records nothing)
        v.frames(1);
        ASSERT_EQ(v.turn(), 1u);                                                        // (no "Get ready" dialog: the first turn runs at once)
        v.frames(99);
        ASSERT_EQ(v.turn(), 100u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[100]);                   // (the same state as the recorded match had at that turn)
        v.frames(2000);
        ASSERT_TRUE(v.state() == ReplayState::Ended);
        ASSERT_EQ(v.turn(), 700u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.file.final_hash);
        ASSERT_TRUE(v.app.sim().is_match_over());
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA7.8 A recording of game mode 187 is watched like any other: the viewer plays it by the file's mode (no food on the map from the first picture), every turn lands in the recorded state, and it ends in the recorded final state with the results; the same match watched as the original's game is not this one") {
        const Handmade made = make_handmade(700, true, 21, 1);
        ASSERT_FALSE(made.bytes.empty());
        ASSERT_TRUE(made.file.complete && made.file.match_over && made.file.head.mode == 1 && made.file.head.sim_rules == replay::kSimRulesMode187);
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok && v.app.replay_mode() && v.app.replay_failure() == ReplayFailure::None);
        ASSERT_TRUE(v.app.sim().game_mode() == sim::GameMode::Kills187);
        for (const auto& cell : v.app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[0]);
        v.frames(100);
        ASSERT_EQ(v.turn(), 100u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[100]);
        v.app.replay_control(ReplayControl::Seek, 50);
        v.frames(1);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[v.turn()]);             // (a jump back starts the engine again: with the file's mode)
        ASSERT_TRUE(v.app.sim().game_mode() == sim::GameMode::Kills187);
        v.frames(2000);
        ASSERT_TRUE(v.state() == ReplayState::Ended);
        ASSERT_EQ(v.app.sim().state_hash().total, made.file.final_hash);
        ASSERT_TRUE(v.app.sim().is_match_over());
        v.app.shutdown();
        const Handmade original = make_handmade(700, true, 21, 0);                      // (the same seed, orders and turns as the original's game: another state from the first hash on)
        ASSERT_FALSE(original.bytes.empty());
        ASSERT_TRUE(original.hash_at[0] != made.hash_at[0] && original.file.final_hash != made.file.final_hash);
    } TEST_END();

    TEST_CASE("RA7.2 The HUD's orders do nothing while a recording is watched: an ant that is ordered to stop and moved on every turn changes nothing of the match") {
        const Handmade made = make_handmade(400, false);
        ASSERT_FALSE(made.bytes.empty());
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok);
        v.frames(150);
        const std::vector<uint32_t> own = ants_of(v.app.sim(), 0);
        ASSERT_FALSE(own.empty());
        v.app.hud().select_ant(own[0]);
        v.app.hud().stop_selected(v.app.sim());                                          // (what a click on Stop does: it goes to the HUD's sink, which is nowhere)
        v.app.hud().stop_selected(v.app.sim());
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[150]);
        v.frames(400);
        ASSERT_TRUE(v.state() == ReplayState::CutShort);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[400]);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA7.3 Pause, speed, jump, jump back, watch again: each lands in the state that the recorded match had at that turn") {
        const Handmade made = make_handmade(700, true);
        ASSERT_FALSE(made.bytes.empty());
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok);
        v.frames(40);
        v.app.replay_control(ReplayControl::TogglePause, 0);
        ASSERT_TRUE(v.state() == ReplayState::Paused);
        v.frames(100);
        ASSERT_EQ(v.turn(), 40u);                                                       // (nothing runs while it is paused)
        v.app.replay_control(ReplayControl::SetPaused, 0);
        v.app.replay_control(ReplayControl::SetSpeed, 800);
        ASSERT_EQ(v.app.replay_value(ReplayValue::Speed), 800);
        v.frames(10);
        ASSERT_TRUE(v.turn() >= 118u && v.turn() <= 120u);                              // (8 turns for each 50 ms of the clock; a turn that the float clock has not quite reached comes with the next frame)
        const uint32_t before_slow = v.turn();
        v.app.replay_control(ReplayControl::SetSpeed, 50);
        v.frames(10);
        ASSERT_TRUE(v.turn() >= before_slow + 4u && v.turn() <= before_slow + 5u);       // (half speed: 5 turns in half a second)
        v.app.replay_control(ReplayControl::SetSpeed, 100);
        // a jump ahead plays on, without a picture, to the turn: the frames of the jump are visible as the state Jumping, and it ends playing
        v.app.replay_control(ReplayControl::Seek, 500);
        ASSERT_TRUE(v.state() == ReplayState::Jumping);
        ASSERT_EQ(v.app.replay_value(ReplayValue::JumpTarget), 500);
        v.frames(1);
        ASSERT_TRUE(v.state() == ReplayState::Playing);
        ASSERT_EQ(v.turn(), 500u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[500]);
        // back: the match is played again from its first turn, to the same state as the way forward
        v.app.replay_control(ReplayControl::Seek, 200);
        v.frames(1);
        ASSERT_EQ(v.turn(), 200u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.hash_at[200]);
        v.frames(1);
        ASSERT_EQ(v.turn(), 201u);
        // to the end by a jump: the results are there, as when it was played
        v.app.replay_control(ReplayControl::Seek, 100000);
        v.frames(2);
        ASSERT_TRUE(v.state() == ReplayState::Ended);
        ASSERT_EQ(v.turn(), 700u);
        ASSERT_EQ(v.app.sim().state_hash().total, made.file.final_hash);
        // watch again
        v.app.replay_control(ReplayControl::Restart, 0);
        ASSERT_TRUE(v.state() == ReplayState::Playing && v.turn() == 0);
        v.frames(30);
        ASSERT_EQ(v.turn(), 30u);
        // a pause survives a jump
        v.app.replay_control(ReplayControl::SetPaused, 1);
        v.app.replay_control(ReplayControl::Seek, 90);
        v.frames(1);
        ASSERT_TRUE(v.state() == ReplayState::Paused && v.turn() == 90u);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA7.4 A recording that ends before its match did stops on its last picture (CutShort) and says so; a jump to its end ends the same way") {
        const Handmade made = make_handmade(300, false);
        ASSERT_FALSE(made.bytes.empty());
        ASSERT_TRUE(made.file.complete && !made.file.match_over);
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok);
        v.frames(1000);
        ASSERT_TRUE(v.state() == ReplayState::CutShort);
        ASSERT_EQ(v.turn(), 300u);
        ASSERT_FALSE(v.app.sim().is_match_over());
        v.app.replay_control(ReplayControl::Seek, 10);                                  // (back from the end: played again from the start)
        v.frames(1);
        ASSERT_TRUE(v.state() == ReplayState::Playing && v.turn() == 10u);
        v.app.replay_control(ReplayControl::Seek, 300);
        v.frames(1);
        ASSERT_TRUE(v.state() == ReplayState::CutShort && v.turn() == 300u);
        // a file that has no end (a cut copy) plays as far as it goes
        std::vector<uint8_t> cut = made.bytes;
        cut.resize(cut.size() - 33);                                                    // (the ENDS chunk: 8 + 13 + 4 + ... bytes; whatever is cut, the file stops early)
        replay::Replay probe;
        std::string why;
        if (replay::decode(cut.data(), cut.size(), probe, why)) {
            ASSERT_FALSE(probe.complete);
            Viewer w(cut, "cut.antsrep");
            ASSERT_TRUE(w.ok);
            w.frames(2000);
            ASSERT_TRUE(w.state() == ReplayState::CutShort);
            ASSERT_EQ(w.turn(), probe.total_turns);
        }
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA7.5 A file that cannot be shown leaves the game idle with the reason: not a replay, older or newer rules, a map that is not here, a match that does not play out the same") {
        const Handmade made = make_handmade(400, true);
        ASSERT_FALSE(made.bytes.empty());
        const auto failure_of = [&](const std::vector<uint8_t>& bytes, ReplayFailure expected) {
            Viewer v(bytes, "bad.antsrep");
            if (!v.ok || (v.app.replay_failure() != expected && expected != ReplayFailure::Diverged)) return false;
            if (expected == ReplayFailure::Diverged) v.frames(500);
            const bool failed = v.state() == ReplayState::Failed && v.app.replay_failure() == expected && !v.app.replay_failure_text().empty();
            const uint32_t before = v.turn();
            v.app.replay_control(ReplayControl::Seek, 100);                          // (the bar does nothing to a replay that cannot be shown)
            v.app.replay_control(ReplayControl::Restart, 0);
            v.frames(20);
            const bool idle = v.turn() == before;
            v.app.shutdown();
            return failed && idle;
        };
        const auto edited = [&](const std::function<void(replay::Replay&)>& edit) {
            replay::Replay r = made.file;
            edit(r);
            std::string error;
            return replay::encode(r, error);
        };
        ASSERT_TRUE(failure_of(std::vector<uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'r', 'e', 'p', 'l', 'a', 'y'}, ReplayFailure::Unreadable));
        ASSERT_TRUE(failure_of(std::vector<uint8_t>{}, ReplayFailure::Unreadable));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = static_cast<uint16_t>(replay::kSimRulesMode187 + 1); }), ReplayFailure::Newer));      // (rules 2 are the ones of game mode 187: the first number that no build plays)
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = 0; r.head.engine_rules = 14; }), ReplayFailure::Older));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = 0; r.head.engine_rules = 200; }), ReplayFailure::Newer));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.map_hash ^= 1u; }), ReplayFailure::NoMap));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.map_name = "NOWHERE.LVL"; }), ReplayFailure::NoMap));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.seed += 1; }), ReplayFailure::Diverged));
        // an older file that the table of protocol numbers knows (the field is absent, protocol 15: rules 1) is an earlier version's since the bombs of a map file (rules 3)
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = 0; r.head.engine_rules = 15; }), ReplayFailure::Older));
        // the page's own questions on an application that shows no replay
        Application plain;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.lan_port = 0;
        ASSERT_TRUE(plain.init(cfg));
        ASSERT_TRUE(!plain.replay_mode() && plain.replay_state() == ReplayState::None);
        plain.replay_control(ReplayControl::Seek, 5);
        plain.shutdown();
    } TEST_END();

    TEST_CASE("RA7.6 Quit and Leave Game leave the viewer (back to the page's list): the dialog's Yes does not quit a match, it ends the viewing") {
        const Handmade made = make_handmade(300, false);
        ASSERT_FALSE(made.bytes.empty());
        Viewer v(made.bytes);
        ASSERT_TRUE(v.ok);
        v.frames(50);
        ASSERT_TRUE(v.app.is_running());
        quit_match(v.app);                                                               // (Ctrl+Q and Yes: a match would end by the quit command, a viewer is left)
        ASSERT_FALSE(v.app.is_running());
        ASSERT_FALSE(v.app.sim().is_match_over());                                      // (no Quit command reached the engine)
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA7.7 A match that is still going on is followed: the game plays up to two seconds short of the newest turn and waits there, an Extend takes a longer copy of the same match and refuses any other, a whole copy ends the following") {
        const Handmade made = make_handmade(600, true);
        ASSERT_FALSE(made.bytes.empty());
        // the copies of the match as a server makes them: the start data, the orders and the hashes up to some turn, no end
        const auto copy_until = [&](uint32_t turns) {
            replay::Replay r = made.file;
            r.complete = false;
            r.match_over = false;
            r.final_hash = 0;
            r.commands.erase(std::remove_if(r.commands.begin(), r.commands.end(), [&](const replay::TimedCommand& c) { return c.turn >= turns; }), r.commands.end());
            r.hashes.resize(turns / r.head.hash_period);
            r.total_turns = turns;
            std::string error;
            return replay::encode(r, error);
        };
        const std::vector<uint8_t> at300 = copy_until(300);
        const std::vector<uint8_t> at500 = copy_until(500);
        ASSERT_FALSE(at300.empty());
        ASSERT_FALSE(at500.empty());
        Viewer v(at300, "live.antsrep", true);
        ASSERT_TRUE(v.ok && v.app.replay_failure() == ReplayFailure::None);
        ASSERT_EQ(v.value(ReplayValue::Live), 1);
        ASSERT_EQ(v.value(ReplayValue::Total), 300);
        ASSERT_EQ(v.value(ReplayValue::Limit), 300 - static_cast<int>(kLiveHoldTurns));
        ASSERT_EQ(v.value(ReplayValue::Complete), 0);
        v.frames(2000);                                                                  // (it waits where it may play to: not ended, not cut short)
        ASSERT_TRUE(v.state() == ReplayState::Playing);
        ASSERT_EQ(v.turn(), 300u - kLiveHoldTurns);
        v.app.replay_control(ReplayControl::Seek, 100000);                               // (a jump past what it may play goes to that turn)
        v.frames(2);
        ASSERT_EQ(v.turn(), 300u - kLiveHoldTurns);
        // a copy that is longer: taken; the time that passed while it waited is not owed
        v.put(at500);
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Total), 500);
        ASSERT_EQ(v.value(ReplayValue::Limit), 500 - static_cast<int>(kLiveHoldTurns));
        v.frames(3);
        ASSERT_EQ(v.turn(), 300u - kLiveHoldTurns + 3u);
        v.frames(2000);
        ASSERT_EQ(v.turn(), 500u - kLiveHoldTurns);
        // a copy that is shorter, one of another match, a damaged one and a missing one change nothing
        v.put(at300);
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Total), 500);
        const Handmade other = make_handmade(600, true, 22);
        ASSERT_FALSE(other.bytes.empty());
        v.put(other.bytes);
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Total), 500);                          // (longer, but another match: another seed)
        std::vector<uint8_t> damaged = at500;
        damaged[damaged.size() / 2] ^= 0xFFu;
        v.put(damaged);
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Total), 500);
        v.put({});
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Total), 500);
        ASSERT_TRUE(v.app.replay_failure() == ReplayFailure::None && v.state() == ReplayState::Playing);
        // going back and watching again are played from the start, and the playhead still stops at the limit
        v.app.replay_control(ReplayControl::Seek, 100);
        v.frames(1);
        ASSERT_EQ(v.turn(), 100u);
        v.app.replay_control(ReplayControl::Restart, 0);
        v.frames(3000);
        ASSERT_EQ(v.turn(), 500u - kLiveHoldTurns);
        // the whole copy (the match is over): the following ends, the game plays on and ends on the results
        v.put(made.bytes);
        v.app.replay_control(ReplayControl::Extend, 0);
        ASSERT_EQ(v.value(ReplayValue::Live), 0);
        ASSERT_EQ(v.value(ReplayValue::Complete), 1);
        ASSERT_EQ(v.value(ReplayValue::Limit), static_cast<int>(made.file.total_turns));
        v.frames(3000);
        ASSERT_TRUE(v.state() == ReplayState::Ended);
        ASSERT_EQ(v.turn(), made.file.total_turns);
        ASSERT_EQ(v.app.sim().state_hash().total, made.file.final_hash);                  // (the state after the last tick and the Quit that ends the match)
        v.app.shutdown();
        // no more will come (the match was not kept): the game plays to the end of what it holds and stops like a cut recording
        Viewer w(at300, "live2.antsrep", true);
        ASSERT_TRUE(w.ok);
        w.frames(1000);
        ASSERT_EQ(w.turn(), 300u - kLiveHoldTurns);
        w.app.replay_control(ReplayControl::LiveOver, 0);
        ASSERT_EQ(w.value(ReplayValue::Live), 0);
        w.frames(1000);
        ASSERT_TRUE(w.state() == ReplayState::CutShort);
        ASSERT_EQ(w.turn(), 300u);
        w.app.shutdown();
        // a file that is whole is no match that goes on, whatever the option says
        Viewer x(made.bytes, "whole.antsrep", true);
        ASSERT_TRUE(x.ok);
        ASSERT_EQ(x.value(ReplayValue::Live), 0);
        ASSERT_EQ(x.value(ReplayValue::Limit), x.value(ReplayValue::Total));
        x.app.shutdown();
    } TEST_END();

    // ---- the camera of a replay (auto_camera.hpp, docs/REPLAYS.md "The camera"): its model is tested alone in test_auto_camera.cpp; here it watches a real recording --------------------------------------

    // The match of four computer players on TREASURE that the next cases watch (made once)
    const auto bot_match = []() -> const BotMatch& {                                // (made when the first case that needs it runs; ANTS_BOT_MATCH_TURNS makes a longer one for the browser check)
        static const BotMatch m = make_bot_match(std::getenv("ANTS_BOT_MATCH_TURNS") != nullptr ? static_cast<uint32_t>(std::atoi(std::getenv("ANTS_BOT_MATCH_TURNS"))) : 4000u);
        return m;
    };

    TEST_CASE("RA8.8 The engine's cues carry what the camera needs in a real match: the blows of the ants (broadcast, with a place), the thieves, fires, bombs and base alarms, for every seat, not one player's") {
        ASSERT_FALSE(bot_match().bytes.empty());
        namespace S = sim::SoundID;
        size_t blows = 0;
        size_t placed_blows = 0;
        size_t named_blows = 0;
        size_t thieves = 0;
        size_t fires = 0;
        std::set<uint8_t> alarmed;
        for (const sim::AudioEvent& e : bot_match().cues) {
            if (e.stop) continue;
            if (e.sound_id == S::MeleeAttack || e.sound_id == S::AttackAlt || e.sound_id == S::HeavyPunch || e.sound_id == S::WaterAttack || e.sound_id == S::ThiefWhip) {
                ++blows;
                if (e.target_player == 255 && (e.world_x != 0 || e.world_y != 0)) ++placed_blows;
                if (e.owner != 0 && e.owner < 0x40000000u) ++named_blows;
            }
            if (e.sound_id == S::ThiefDive) ++thieves;
            if (e.sound_id == S::FireBeam || e.sound_id == S::FireErupt) ++fires;
            if (e.sound_id == S::BaseAlarmSiren || e.sound_id == S::Anthill) alarmed.insert(e.target_player);
        }
        ASSERT_GT(blows, 10u);
        ASSERT_EQ(blows, placed_blows);                                             // (every blow is a cue for everybody, at the ant that struck)
        ASSERT_EQ(blows, named_blows);                                              // (and names the ant: the camera follows it)
        ASSERT_GT(thieves, 0u);
        ASSERT_GT(fires, 0u);
        ASSERT_TRUE(alarmed.count(0) && alarmed.count(1) && alarmed.count(2) && alarmed.count(3));      // (the engine makes the cue of each seat: the replay's engine holds them all, none is filtered by whose screen it is)
        if (const char* out = std::getenv("ANTS_BOT_MATCH_OUT")) {                  // (the file of this match, for the browser check of the camera: tests/scripts/web_camera_check.py)
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bot_match().bytes.data()), static_cast<std::streamsize>(bot_match().bytes.size()));
        }
    } TEST_END();

    TEST_CASE("RA9.1 Zooming does not touch the sounds: a recording watched while the wheel zooms in and out starts the same sounds, in the same order, at the same places, as one watched at a constant zoom (the Auto camera on and off, at 1x and 4x)") {
        ASSERT_FALSE(bot_match().bytes.empty());
        for (const bool auto_camera : {true, false}) {
            for (const int speed : {100, 400}) {
                std::vector<AudioMixer::SoundStart> plain;
                std::vector<AudioMixer::SoundStart> zoomed;
                for (const bool zooming : {false, true}) {
                    Viewer v(bot_match().bytes);
                    ASSERT_TRUE(v.ok);
                    std::vector<AudioMixer::SoundStart>& log = zooming ? zoomed : plain;
                    v.app.audio_mixer().set_start_log(&log);
                    v.app.replay_control(ReplayControl::SetCamera, auto_camera ? 1 : 0);
                    v.app.replay_control(ReplayControl::SetSpeed, speed);
                    const LayoutRect view = v.app.layout().view();
                    size_t steps = 0;
                    for (int i = 0; i < 2400; ++i) {
                        if (zooming && i % 5 == 0) {                                                   // a notch of the wheel every 5 frames, in and out, now at the pointer, now at a corner
                            const int32_t ax = view.x + (i % 3 == 0 ? 3 : view.w / 2);
                            const int32_t ay = view.y + (i % 3 == 0 ? view.h - 4 : view.h / 3);
                            if (v.app.step_zoom(((i / 5) / 4) % 2 == 0 ? -1 : +1, ax, ay)) ++steps;
                            if (i % 60 == 0) v.app.set_zoom(i % 120 == 0 ? 2.0f : 0.5f, ax, ay);
                        }
                        v.app.update_simulation(0.016f);
                        v.app.audio_mixer().render_frames(704);                                        // (the audio device's share of the frame: channels end, so the voice limit acts alike in both runs)
                    }
                    if (zooming) ASSERT_GT(steps, 100u);                                               // (it did zoom)
                }
                ASSERT_GT(plain.size(), 80u);
                ASSERT_EQ(zoomed.size(), plain.size());
                bool alike = true;
                for (size_t n = 0; n < plain.size() && alike; ++n) {
                    alike = plain[n].sound_id == zoomed[n].sound_id && plain[n].owner == zoomed[n].owner && plain[n].world_x == zoomed[n].world_x && plain[n].world_y == zoomed[n].world_y &&
                            plain[n].spatial == zoomed[n].spatial && plain[n].priority == zoomed[n].priority;
                }
                ASSERT_TRUE(alike);
            }
        }
    } TEST_END();

    TEST_CASE("RA9.2 A move of the view changes the gain of a playing sound in a ramp, not in a step: no click, and the law at the end is the original's") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        AudioMixer& mixer = v.app.audio_mixer();
        const assets::AssetArchive& archive = v.app.assets();
        uint32_t longest = 0;
        for (uint32_t id = 0; id < archive.sound_count(); ++id) {
            if (archive.get_sound(id).pcm_data.size() > archive.get_sound(longest).pcm_data.size()) longest = id;
        }
        constexpr size_t kFrames = 4096;
        ASSERT_GT(archive.get_sound(longest).pcm_data.size(), 2 * kFrames);
        // The sound at (1000, 0) is heard from (0, 0) and from (900, 0), and from the first place until the view has moved to the second after `moved` frames (a zoom at the pointer moves the middle of the view)
        const auto render = [&](int32_t from_x, int32_t to_x, size_t moved) {
            mixer.stop_all();
            mixer.set_listener_position(from_x, 0);
            mixer.play_spatial(longest, 1000, 0, 128);
            std::vector<int16_t> out = mixer.render_frames(moved);
            mixer.set_listener_position(to_x, 0);
            const std::vector<int16_t> rest = mixer.render_frames(kFrames - moved);
            out.insert(out.end(), rest.begin(), rest.end());
            return out;
        };
        const std::vector<int16_t> before = render(0, 0, kFrames);
        const std::vector<int16_t> after = render(900, 900, kFrames);
        size_t moved = 0;                                                                              // the frame where the two gains differ the most: the move is heard there
        for (size_t n = 500; n + 1500 < kFrames; ++n) {
            if (std::abs(after[2 * n] - before[2 * n]) > std::abs(after[2 * moved] - before[2 * moved])) moved = n;
        }
        ASSERT_GT(std::abs(after[2 * moved] - before[2 * moved]), 1000);                               // (a loud moment of a sound whose gain changes by more than a factor of 2)
        const std::vector<int16_t> run = render(0, 900, moved);
        for (size_t n = 0; n < moved * 2; ++n) ASSERT_EQ(run[n], before[n]);                           // the sound goes on as it was until the move
        const int step_in_wave = std::abs(run[2 * moved] - run[2 * moved - 2]);
        const int step_of_step = std::abs(after[2 * moved] - before[2 * moved]);
        ASSERT_LT(step_in_wave * 10, step_of_step);                                                    // not a step of the whole change in one sample (without the ramp: 2895 of 2844; with it: the wave's own 59)
        size_t between = 0;
        for (size_t n = moved; n < kFrames; ++n) {
            if (std::abs(run[2 * n] - after[2 * n]) > 4) between = n;                                  // the last frame that has not yet got there
        }
        const size_t ramp = static_cast<size_t>(AUDIO_GAIN_RAMP_MS * 44.1f);
        ASSERT_GT(between, moved + 20);                                                                // it takes more than a few samples ...
        ASSERT_LT(between, moved + ramp + 8);                                                          // ... and no more than the ramp of the longest change
        for (size_t n = moved + ramp + 8; n < kFrames; ++n) ASSERT_TRUE(std::abs(run[2 * n] - after[2 * n]) <= 4 && std::abs(run[2 * n + 1] - after[2 * n + 1]) <= 4);       // then it is the original's law
        float left = 0.0f;
        float right = 0.0f;
        mixer.stop_all();
        mixer.set_listener_position(0, 0);
        const int channel = mixer.play_spatial(longest, 1000, 0, 128);
        mixer.set_listener_position(900, 0);
        ASSERT_TRUE(mixer.channel_volumes(channel, left, right));
        float want_left = 0.0f;
        float want_right = 0.0f;
        mixer.calculate_spatial_pan(1000, 0, 1.0f, want_left, want_right);
        ASSERT_TRUE(left == want_left && right == want_right);                                         // (the target of the ramp is the law, as ever)
        mixer.stop_all();
    } TEST_END();

    TEST_CASE("RA9.3 The same in a game on this computer: the sounds a match makes are the same ones with the wheel zooming as without") {
        std::vector<AudioMixer::SoundStart> plain;
        std::vector<AudioMixer::SoundStart> zoomed;
        for (const bool zooming : {false, true}) {
            Application app;
            const ApplicationConfig cfg = local_config(0x0E);
            ASSERT_TRUE(app.init(cfg));
            std::vector<AudioMixer::SoundStart>& log = zooming ? zoomed : plain;
            app.audio_mixer().set_start_log(&log);
            app.hud().dismiss_match_start_modal();
            const LayoutRect view = app.layout().view();
            for (int i = 0; i < 2400; ++i) {
                if (zooming && i % 4 == 0) app.step_zoom(((i / 4) / 5) % 2 == 0 ? -1 : +1, view.x + (i % 3) * view.w / 3, view.y + view.h / 2);
                app.update_simulation(0.05f);
                app.audio_mixer().render_frames(2205);
            }
        }
        ASSERT_GT(plain.size(), 50u);
        ASSERT_EQ(zoomed.size(), plain.size());
        bool alike = true;
        for (size_t n = 0; n < plain.size() && alike; ++n) {
            alike = plain[n].sound_id == zoomed[n].sound_id && plain[n].owner == zoomed[n].owner && plain[n].world_x == zoomed[n].world_x && plain[n].world_y == zoomed[n].world_y && plain[n].spatial == zoomed[n].spatial;
        }
        ASSERT_TRUE(alike);
    } TEST_END();

    TEST_CASE("RA8.9 A recording is watched with the Auto camera by default: the camera locks on fights and follows them, stays at least the dwell on each, never moves faster than its limit, the minimap's ring is whole, and the match is the same one (every hash of the file and the end)") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok && v.app.replay_failure() == ReplayFailure::None);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 0);
        ASSERT_EQ(v.app.auto_camera().hills().size(), 4u);                           // (the hills of the match: where a base alarm is)
        for (const assets::AnthillSpawn& hill : v.app.sim().get_world_state().anthills) {
            ASSERT_TRUE(hill.team_id < 4);
            ASSERT_TRUE(v.app.auto_camera().hills()[hill.team_id].x == (hill.x + 2.0) * 32.0 && v.app.auto_camera().hills()[hill.team_id].y == (hill.y + 2.0) * 32.0);
        }
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        const ViewportCamera& cam = v.app.renderer().camera();
        const float start_x = cam.x;
        const float start_y = cam.y;
        std::set<std::pair<int, int>> places;
        bool ring = false;
        bool inside = true;
        int arrived = 0;
        uint32_t locks = 0;
        double last_lock_ms = -1.0;
        double shortest_stay_ms = 1e9;
        double biggest_step = 0.0;
        float seen_x = cam.x;
        float seen_y = cam.y;
        for (int i = 0; i < 2100 && v.state() == ReplayState::Playing; ++i) {
            v.frames(1);
            biggest_step = std::max(biggest_step, static_cast<double>(std::hypot(cam.x - seen_x, cam.y - seen_y)));
            seen_x = cam.x;
            seen_y = cam.y;
            if (v.app.auto_camera().locks() != locks) {                              // (a lock: the camera went to a place; not sooner than the dwell after the one before)
                locks = v.app.auto_camera().locks();
                if (last_lock_ms >= 0.0) shortest_stay_ms = std::min(shortest_stay_ms, v.app.auto_camera().now_ms() - last_lock_ms);
                last_lock_ms = v.app.auto_camera().now_ms();
            }
            if (v.app.hud().camera_ring_shown() && v.app.hud().camera_ring_solid()) ring = true;
            AutoCamera::Point target;
            if (v.app.auto_camera().target(target) && !v.app.auto_camera().gliding()) {                   // (arrived: the place is in the picture)
                const double vw = zoom::visible_exact(cam.viewport_w, cam.zoom);
                const double vh = zoom::visible_exact(cam.viewport_h, cam.zoom);
                if (target.x >= cam.x && target.x <= cam.x + vw && target.y >= cam.y && target.y <= cam.y + vh) ++arrived;
                else inside = false;
                places.insert({static_cast<int>(target.x) / 200, static_cast<int>(target.y) / 200});
            }
        }
        ASSERT_TRUE(v.app.replay_failure() == ReplayFailure::None);                  // (no hash of the file differed: the camera touches nothing of the match)
        ASSERT_TRUE(cam.x != start_x || cam.y != start_y);
        ASSERT_GE(places.size(), 2u);                                                // (it went to more than one place)
        ASSERT_GE(locks, 2u);
        ASSERT_LE(locks, 2100u * 50u / static_cast<uint32_t>(AutoCamera::kDwellMs) + 1u);        // (and never more often than one in a dwell)
        ASSERT_GE(shortest_stay_ms, AutoCamera::kDwellMs - 1.0);
        ASSERT_LE(biggest_step, AutoCamera::kMaxSpeed * 0.050 + 2.0);                // (no frame moves the picture faster than the limit: no snap; a pixel or two for the rounding)
        ASSERT_TRUE(ring);
        ASSERT_GT(arrived, 20);
        ASSERT_TRUE(inside);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 0);
        v.frames(3000);
        ASSERT_TRUE(v.state() == ReplayState::CutShort);
        ASSERT_GT(v.app.auto_camera().deaths(), 0u);                                 // (the camera reads the ants that died: a match of four Hard bots has deaths)
        ASSERT_EQ(v.app.sim().state_hash().total, bot_match().file.final_hash);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.10 Manual camera: the view stays where it is for the whole match, the ring is only a hint (dashed), and the match is the same one") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetCamera, 0);
        ASSERT_EQ(v.value(ReplayValue::Camera), 0);
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        const ViewportCamera& cam = v.app.renderer().camera();
        const float start_x = cam.x;
        const float start_y = cam.y;
        bool hint = false;
        bool whole = false;
        for (int i = 0; i < 2100 && v.state() == ReplayState::Playing; ++i) {
            v.frames(1);
            ASSERT_TRUE(cam.x == start_x && cam.y == start_y);
            if (v.app.hud().camera_ring_shown()) (v.app.hud().camera_ring_solid() ? whole : hint) = true;
        }
        ASSERT_TRUE(hint);
        ASSERT_FALSE(whole);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 0);
        v.frames(3000);
        ASSERT_EQ(v.app.sim().state_hash().total, bot_match().file.final_hash);
        v.app.shutdown();
    } TEST_END();

    // The pointer at one edge of the picture (the edge strips scroll the view, as in the original), then back in the middle. True when the view moved.
    const auto scroll_by_edge = [](Application& app) {
        const int32_t width = app.layout().width;                                   // (the strips run along the edges of the picture)
        const int32_t height = app.layout().height;
        const int32_t mid_x = width / 2;
        const int32_t mid_y = height / 2;
        const int32_t spots[4][2] = {{1, mid_y}, {width - 2, mid_y}, {mid_x, 1}, {mid_x, height - 2}};
        for (const auto& spot : spots) {
            const float x = app.renderer().camera().x;
            const float y = app.renderer().camera().y;
            SDL_MouseMotionEvent m{};
            m.type = SDL_MOUSEMOTION;
            m.x = spot[0];
            m.y = spot[1];
            app.handle_mouse_motion(m);
            for (int i = 0; i < 4; ++i) app.handle_camera_panning(0.060f);
            m.x = mid_x;
            m.y = mid_y;
            app.handle_mouse_motion(m);
            if (app.renderer().camera().x != x || app.renderer().camera().y != y) return true;
        }
        return false;
    };

    TEST_CASE("RA8.11 Taking the camera: the pointer at the screen edge moves the view and makes it Manual (told once to the page), an edge that cannot scroll takes nothing, and the button's Auto glides back to the last action") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        v.frames(500);                                                              // (the view has been to a place or two)
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        {   // an edge that cannot scroll takes nothing: a Viewer of its own, its view put at the map's top left corner (a test's own doing, not the viewer's), the pointer at the left edge
            Viewer w(bot_match().bytes);
            ASSERT_TRUE(w.ok);
            ViewportCamera& c = w.app.renderer().camera();
            c.set_origin(0.0, 0.0, w.app.sim().get_world_state().width, w.app.sim().get_world_state().height);
            ASSERT_TRUE(c.world_x == 0 && c.world_y == 0);
            const int32_t width = w.app.layout().width;
            const int32_t height = w.app.layout().height;
            SDL_MouseMotionEvent m{};
            m.type = SDL_MOUSEMOTION;
            m.x = 1;
            m.y = height / 2;
            w.app.handle_mouse_motion(m);
            for (int i = 0; i < 10; ++i) w.app.handle_camera_panning(0.060f);
            ASSERT_TRUE(c.world_x == 0 && c.world_y == 0);
            ASSERT_EQ(w.value(ReplayValue::Camera), 1);
            ASSERT_EQ(w.value(ReplayValue::CameraTaken), 0);
            m.y = height - 2;                                                       // (the bottom edge can scroll: that one takes it)
            m.x = width / 2;
            w.app.handle_mouse_motion(m);
            for (int i = 0; i < 10; ++i) w.app.handle_camera_panning(0.060f);
            ASSERT_TRUE(c.world_y > 0);
            ASSERT_EQ(w.value(ReplayValue::Camera), 0);
            w.app.shutdown();
        }
        ASSERT_TRUE(scroll_by_edge(v.app));
        ASSERT_EQ(v.value(ReplayValue::Camera), 0);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 1);
        const ViewportCamera& cam = v.app.renderer().camera();
        const float held_x = cam.x;
        const float held_y = cam.y;
        v.frames(400);                                                              // (more happens, and the view stays where the viewer put it)
        ASSERT_TRUE(cam.x == held_x && cam.y == held_y);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 1);
        v.app.replay_control(ReplayControl::SetCamera, 1);                          // the button
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        v.frames(40);
        ASSERT_TRUE(cam.x != held_x || cam.y != held_y);                            // (it glides back ...)
        v.frames(60);
        AutoCamera::Point target;
        ASSERT_TRUE(v.app.auto_camera().target(target));
        const double vw = zoom::visible_exact(cam.viewport_w, cam.zoom);
        const double vh = zoom::visible_exact(cam.viewport_h, cam.zoom);
        ASSERT_TRUE(target.x >= cam.x && target.x <= cam.x + vw && target.y >= cam.y && target.y <= cam.y + vh);        // (... to the place it was, and it is in the picture)
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.12 The wheel's zoom keeps the Auto camera, at the viewer's zoom (the place stays in the picture); two fingers that pan take it, the pixel or two that a pinch drifts does not, and a minimap drag takes it") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        v.frames(500);
        v.app.replay_control(ReplayControl::SetPaused, 1);                          // (nothing more happens: the events fade, the camera lets go, the view arrives and stays)
        AutoCamera::Point target;
        for (int i = 0; i < 400 && (v.app.auto_camera().gliding() || v.app.auto_camera().events() != 0 || v.app.auto_camera().locked()); ++i) v.frames(1);
        ASSERT_TRUE(v.app.auto_camera().target(target) && !v.app.auto_camera().gliding());
        const LayoutRect view = v.app.layout().view();
        SDL_MouseMotionEvent m{};
        m.type = SDL_MOUSEMOTION;
        m.x = view.x + view.w / 2;
        m.y = view.y + view.h / 2;
        v.app.handle_mouse_motion(m);
        const float zoom_before = v.app.renderer().camera().zoom;
        SDL_MouseWheelEvent wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.y = 1;
        wheel.preciseY = 1.0f;
        wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        wheel.timestamp = 1000;
        v.app.handle_mouse_wheel(wheel);
        v.frames(2);
        const ViewportCamera& cam = v.app.renderer().camera();
        ASSERT_TRUE(cam.zoom != zoom_before);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);                                 // (zoom keeps Auto)
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 0);
        {
            const double vw = zoom::visible_exact(cam.viewport_w, cam.zoom);
            const double vh = zoom::visible_exact(cam.viewport_h, cam.zoom);
            ASSERT_TRUE(v.app.auto_camera().target(target));
            ASSERT_TRUE(target.x >= cam.x && target.x <= cam.x + vw && target.y >= cam.y && target.y <= cam.y + vh);
        }
        v.app.pan_view(2, 1);                                                       // (a pinch's drift)
        v.app.pan_view(-1, 2);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        v.frames(10);                                                               // (a while later: the sum starts again)
        v.app.pan_view(1, 1);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        v.app.pan_view(30, 0);                                                      // two fingers that really pan
        ASSERT_EQ(v.value(ReplayValue::Camera), 0);
        ASSERT_EQ(v.value(ReplayValue::CameraTaken), 1);
        v.app.replay_control(ReplayControl::SetCamera, 1);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        // the minimap: a press and the pointer held off the middle of the minimap steer the view
        const LayoutRect mini = v.app.layout().minimap();
        SDL_MouseButtonEvent press{};
        press.type = SDL_MOUSEBUTTONDOWN;
        press.button = SDL_BUTTON_LEFT;
        press.x = mini.x + mini.w / 2;
        press.y = mini.y + mini.h / 2;
        v.app.handle_mouse_motion(m);
        m.x = mini.x + 1;
        m.y = mini.y + mini.h / 2;
        v.app.handle_mouse_motion(m);
        press.x = m.x;
        press.y = m.y;
        v.app.handle_mouse_button(press);
        const float x = cam.x;
        const float y = cam.y;
        for (int i = 0; i < 6; ++i) v.app.handle_camera_panning(0.060f);
        ASSERT_TRUE(cam.x != x || cam.y != y);
        ASSERT_EQ(v.value(ReplayValue::Camera), 0);
        SDL_MouseButtonEvent release = press;
        release.type = SDL_MOUSEBUTTONUP;
        v.app.handle_mouse_button(release);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.13 A jump forgets what happened before it: the camera has no event, no lock and no place after it, and does not rush to a place of the past; the mode stays") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        v.frames(400);
        AutoCamera::Point place;
        ASSERT_TRUE(v.app.auto_camera().target(place));
        ASSERT_GT(v.app.auto_camera().events(), 0u);
        v.app.replay_control(ReplayControl::SetCamera, 0);                           // (Manual, to see that the mode stays)
        v.app.replay_control(ReplayControl::SetPaused, 1);
        v.app.replay_control(ReplayControl::Seek, static_cast<int>(v.turn()) + 300);        // (forward: the jump plays turns that nobody sees)
        for (int i = 0; i < 40 && v.state() == ReplayState::Jumping; ++i) v.frames(1);
        ASSERT_TRUE(v.state() == ReplayState::Paused);
        ASSERT_EQ(v.app.auto_camera().events(), 0u);
        ASSERT_FALSE(v.app.auto_camera().locked());
        ASSERT_FALSE(v.app.auto_camera().target(place));
        ASSERT_FALSE(v.app.hud().camera_ring_shown());
        ASSERT_EQ(v.value(ReplayValue::Camera), 0);
        v.app.replay_control(ReplayControl::SetCamera, 1);                           // (the button after a jump: no last action to glide to)
        const float x = v.app.renderer().camera().x;
        const float y = v.app.renderer().camera().y;
        v.frames(40);
        ASSERT_TRUE(v.app.renderer().camera().x == x && v.app.renderer().camera().y == y);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.15 A wheel zoom does not snap the Auto camera back: the zoom moves the middle of the view (it keeps the world under the pointer), and the camera glides back to its place from there") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetSpeed, 200);
        v.frames(500);
        v.app.replay_control(ReplayControl::SetPaused, 1);                          // (nothing more happens: the camera lets go, and the view arrives)
        for (int i = 0; i < 400 && (v.app.auto_camera().gliding() || v.app.auto_camera().events() != 0 || v.app.auto_camera().locked()); ++i) v.frames(1);
        AutoCamera::Point target;
        ASSERT_TRUE(v.app.auto_camera().target(target) && !v.app.auto_camera().gliding());
        const ViewportCamera& cam = v.app.renderer().camera();
        const auto middle = [&cam]() {
            return AutoCamera::Point{static_cast<double>(cam.x) + zoom::visible_exact(cam.viewport_w, cam.zoom) / 2.0, static_cast<double>(cam.y) + zoom::visible_exact(cam.viewport_h, cam.zoom) / 2.0};
        };
        const AutoCamera::Point before = middle();
        const double map_x = static_cast<double>(v.app.sim().get_world_state().width) * 16.0;      // (the middle of the map, in world pixels: the pointer goes to the side of the view that is nearer to it, so the zoom has room to move the middle)
        const double map_y = static_cast<double>(v.app.sim().get_world_state().height) * 16.0;
        const LayoutRect view = v.app.layout().view();
        SDL_MouseMotionEvent m{};
        m.type = SDL_MOUSEMOTION;
        m.x = view.x + (map_x > before.x ? view.w * 9 / 10 : view.w / 10);
        m.y = view.y + (map_y > before.y ? view.h * 9 / 10 : view.h / 10);
        v.app.handle_mouse_motion(m);
        SDL_MouseWheelEvent wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.y = 1;
        wheel.preciseY = 1.0f;
        wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        wheel.timestamp = 1000;
        const float zoom_before = cam.zoom;
        v.app.handle_mouse_wheel(wheel);
        ASSERT_TRUE(cam.zoom != zoom_before);
        const AutoCamera::Point shifted = middle();
        const double shift = std::hypot(shifted.x - before.x, shifted.y - before.y);
        ASSERT_GT(shift, 25.0);                                                      // (the zoom moved the middle of the view)
        v.frames(1);
        const AutoCamera::Point next = middle();
        ASSERT_LT(std::hypot(next.x - shifted.x, next.y - shifted.y), 12.0);         // (the frame after it: a step of the glide, not a jump back to the place)
        ASSERT_GT(std::hypot(next.x - before.x, next.y - before.y), shift - 12.0);
        v.frames(80);                                                                // (and later it is back at the place, at the new zoom)
        const AutoCamera::Point later = middle();
        ASSERT_LT(std::hypot(later.x - before.x, later.y - before.y), 6.0);
        ASSERT_EQ(v.value(ReplayValue::Camera), 1);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.16 The camera is told the speed of the replay (from 4x up only the big events count there): the model's speed is the replay's") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        for (const int speed100 : {100, 200, 400, 800, 50}) {
            v.app.replay_control(ReplayControl::SetSpeed, speed100);
            v.frames(30);                                                            // (a turn is run: the camera is told as it reads it)
            ASSERT_EQ(v.app.auto_camera().speed(), static_cast<float>(speed100) / 100.0f);
        }
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.17 A long frame (a stall, a hidden page) moves the camera's clock by 0.25 s at the most, so that it does not rush") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.frames(10);
        const double before = v.app.auto_camera().now_ms();
        v.app.update_simulation(2.0f);
        ASSERT_NEAR(v.app.auto_camera().now_ms() - before, 250.0, 0.001);
        v.app.update_simulation(0.05f);
        ASSERT_NEAR(v.app.auto_camera().now_ms() - before, 300.0, 0.001);
        v.app.shutdown();
    } TEST_END();

    TEST_CASE("RA8.14 Nothing can be picked in a replay: no click, no drag box, no corner marks, no Move and Stop buttons, no order marker, no Ctrl+A / H / N / P, no inspect panel; the same keys and drag pick in a match that is played") {
        ASSERT_FALSE(bot_match().bytes.empty());
        Viewer v(bot_match().bytes);
        ASSERT_TRUE(v.ok);
        v.app.replay_control(ReplayControl::SetCamera, 0);
        v.frames(160);
        HUD& hud = v.app.hud();
        ASSERT_TRUE(hud.view_only());
        int markers = 0;
        hud.set_on_spawn_click_marker([&](int32_t, int32_t) { ++markers; });
        const ViewportCamera& cam = v.app.renderer().camera();
        const LayoutRect view = v.app.layout().view();
        // the first player's ants that the picture shows, and where on the screen they are
        std::vector<std::pair<int32_t, int32_t>> mine;
        std::vector<std::pair<int32_t, int32_t>> theirs;
        for (const auto& a : v.app.sim().get_world_state().ants) {
            int32_t sx = 0;
            int32_t sy = 0;
            if (!cam.world_to_screen(a.px, a.py - 8, sx, sy) || sx < view.x + 4 || sx > view.right() - 4 || sy < view.y + 4 || sy > view.bottom() - 4) continue;
            (a.player_id == 0 ? mine : theirs).push_back({sx, sy});
        }
        ASSERT_FALSE(mine.empty());
        const auto press_at = [&](int32_t x, int32_t y, uint8_t button, bool down) {
            SDL_MouseMotionEvent m{};
            m.type = SDL_MOUSEMOTION;
            m.x = x;
            m.y = y;
            v.app.handle_mouse_motion(m);
            SDL_MouseButtonEvent b{};
            b.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            b.button = button;
            b.x = x;
            b.y = y;
            v.app.handle_mouse_button(b);
        };
        const auto drag_box = [&]() {                                               // a drag box round the ant that is there, the corner of the picture the near one
            press_at(view.x + 10, view.y + 30, SDL_BUTTON_LEFT, true);
            press_at(mine[0].first + 24, mine[0].second + 24, SDL_BUTTON_LEFT, false);
        };
        const auto picked = [&]() { return !hud.get_selected_ant_ids().empty() || hud.get_selected_ant_id() != 0 || hud.get_selected_base_team_id() >= 0; };
        // (the control: with the switch off, the same hands pick the ants and the HUD shows its Move and Stop panel)
        hud.set_view_only(false);
        drag_box();
        ASSERT_TRUE(picked());
        hud.clear_selection();
        press(v.app, SDLK_a, KMOD_LCTRL);
        ASSERT_FALSE(hud.get_selected_ant_ids().empty());
        hud.clear_selection();
        hud.set_view_only(true);
        // a click on an ant (the first player's, and another's: the inspect panel), a drag box, the right button on the map and on the minimap
        press_at(mine[0].first, mine[0].second, SDL_BUTTON_LEFT, true);
        ASSERT_FALSE(hud.is_dragging());
        press_at(mine[0].first, mine[0].second, SDL_BUTTON_LEFT, false);
        ASSERT_FALSE(picked());
        if (!theirs.empty()) {
            press_at(theirs[0].first, theirs[0].second, SDL_BUTTON_LEFT, true);
            press_at(theirs[0].first, theirs[0].second, SDL_BUTTON_LEFT, false);
            ASSERT_FALSE(picked());
        }
        drag_box();
        ASSERT_FALSE(picked());
        press_at(view.x + view.w / 2, view.y + view.h / 2, SDL_BUTTON_RIGHT, true);
        press_at(view.x + view.w / 2, view.y + view.h / 2, SDL_BUTTON_RIGHT, false);
        const LayoutRect mini = v.app.layout().minimap();
        press_at(mini.x + mini.w / 2, mini.y + mini.h / 2, SDL_BUTTON_RIGHT, true);
        press_at(mini.x + mini.w / 2, mini.y + mini.h / 2, SDL_BUTTON_RIGHT, false);
        ASSERT_EQ(markers, 0);                                                      // (no order marker, white on the ground or red on an ant)
        ASSERT_FALSE(picked());
        // the keys
        for (const SDL_Keycode key : {SDLK_a, SDLK_h, SDLK_n, SDLK_p, SDLK_s}) {
            press(v.app, key, KMOD_LCTRL);
            ASSERT_FALSE(picked());
        }
        // the pointer over an ant is the plain arrow, not the pick cursor
        press_at(mine[0].first, mine[0].second, SDL_BUTTON_LEFT, false);
        ASSERT_TRUE(hud.evaluate_cursor(mine[0].first, mine[0].second, v.app.sim().get_world_state(), v.app.sim().grid(), v.app.renderer().camera()) == CursorType::Normal);
        // what a replay keeps: the minimap steers the view, the Picture and the bar are the page's; the match is untouched
        v.frames(10);
        ASSERT_TRUE(v.app.replay_failure() == ReplayFailure::None);
        v.app.shutdown();
    } TEST_END();

    std::cout << "\nreplay application tests: " << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
