// Tests of the replay recording in the application (docs/REPLAYS.md): a headless Application plays a game on this computer and takes part in a match of the network (the other machine is a bare
// NetGame over loopback sockets), and the file that it keeps of each is played again by the replay player on the engine alone. A recording stands for the match exactly when that replay reaches the
// state in which the match ended: the same state hash, every order at its turn.
#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

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
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

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
std::unique_ptr<Duo> start_duo(Application& app, Peer& host, uint32_t seed) {
    if (!host.net.host(0, "Alice", true)) return nullptr;
    host.net.set_map("TINY.LVL");
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

Handmade make_handmade(uint32_t turns, bool quit_at_end, uint32_t seed = 21) {
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
    sim::SimulationEngine engine;
    engine.set_fog_of_war_enabled(true);
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

// An application that watches `bytes` (written to a file of its own folder)
struct Viewer {
    ScratchRoot root;
    Application app;
    bool ok{false};
    explicit Viewer(const std::vector<uint8_t>& bytes, const std::string& name = "match.antsrep") {
        const fs::path file = root.path() / name;
        {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.lan_port = 0;
        cfg.maps_dir = maps_dir();
        cfg.replay_path = file.string();
        ok = app.init(cfg);
    }
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
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = static_cast<uint16_t>(replay::kSimRules + 1); }), ReplayFailure::Newer));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = 0; r.head.engine_rules = 14; }), ReplayFailure::Older));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.sim_rules = 0; r.head.engine_rules = 200; }), ReplayFailure::Newer));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.map_hash ^= 1u; }), ReplayFailure::NoMap));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.map_name = "NOWHERE.LVL"; }), ReplayFailure::NoMap));
        ASSERT_TRUE(failure_of(edited([](replay::Replay& r) { r.head.seed += 1; }), ReplayFailure::Diverged));
        // an older file that the table of protocol numbers knows plays (the field is absent, protocol 15)
        {
            replay::Replay r = made.file;
            r.head.sim_rules = 0;
            r.head.engine_rules = 15;
            std::string error;
            Viewer v(replay::encode(r, error));
            ASSERT_TRUE(v.ok && v.state() == ReplayState::Playing && v.app.replay_failure() == ReplayFailure::None);
            v.app.shutdown();
        }
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

    std::cout << "\nreplay application tests: " << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
