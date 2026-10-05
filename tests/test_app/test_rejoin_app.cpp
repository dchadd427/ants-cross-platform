// Tests of the screens of the way back and the keys' storage in the application (docs/NETWORK_PORT.md "What the clients do in release B"): a headless Application in a room of a real
// RoomManager (the door, the lobby, the room, the referee, the turn log) over loopback sockets, with a bare second machine, stepped by a clock of the test's own (10 ms a step).
// What the player SEES and DOES: the overlay of the way back, of the missing seats and of the countdown, the vote block (F2 / F3 and the mouse), the catch-up screen, the start of a rejoin
// (no dialog, no sound), the start dialog that gives way to a pause, leaving while the match is held, the HUD that a blip leaves as it was, the keys that the application keeps in its file
// and uses when a game is started again, and the start menu's "Rejoin your match". The library's own side (NetGame, PauseInfo) is test_rejoin; the file is test_rejoin_store.
//
// A KEY IS A SECRET: no test prints one, and the output of the program is searched for it.
#include <SDL.h>

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/page_layout.hpp"
#include "ants_app/rejoin_store.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::app;
namespace fs = std::filesystem;

// The first turn of a match is sealed this long after the match began (protocol 12: the "Get ready to play!" dialog of every machine)
constexpr uint32_t kPre = net::kMatchStartDelayMs;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(120) << name.substr(0, 120) << " ... " << std::flush;
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

using net::NetGame;

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// A folder of this process alone (made new, removed with everything in it when the program ends): the settings and the key file of every application of a test
class ScratchRoot {
public:
    ScratchRoot() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_rejoin_app_" + std::to_string(static_cast<long>(getpid())) + "_" + suffix);
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot make a scratch folder under " + fs::temp_directory_path().string());
    }
    ~ScratchRoot() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    ScratchRoot(const ScratchRoot&) = delete;
    ScratchRoot& operator=(const ScratchRoot&) = delete;
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

fs::path scratch_dir(const char* tag) {
    static ScratchRoot root;
    const fs::path p = root.path() / tag;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// What the program writes to stderr while a test runs (every line of the application's log): searched for keys at the end
class Captured {
public:
    Captured() : old_(std::cerr.rdbuf(buffer_.rdbuf())) {}
    ~Captured() { std::cerr.rdbuf(old_); }
    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;
    std::string text() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* old_;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// The server: a real RoomManager behind a real TCP listener on the loopback address, stepped by the test's clock
// ---------------------------------------------------------------------------------------------------------------------------------

// A TcpConnection that the manager owns and the test can still reach (to cut it)
class Wire final : public net::Connection {
public:
    explicit Wire(std::shared_ptr<net::TcpConnection> c) : c_(std::move(c)) {}
    bool send(const std::vector<uint8_t>& m) override { return c_->send(m); }
    bool poll(std::vector<uint8_t>& m) override { return c_->poll(m); }
    State state() const override { return c_->state(); }
    void close() override { c_->close(); }

private:
    std::shared_ptr<net::TcpConnection> c_;
};

class Server {
public:
    bool start(uint32_t world_now) {
        offset_ = 500u - world_now;
        listener_ = net::TcpListener::listen(0, true);
        if (!listener_) return false;
        port_ = listener_->port();
        mgr = std::make_unique<server::RoomManager>(server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"));
        return true;
    }
    uint16_t port() const noexcept { return port_; }
    uint32_t now(uint32_t world_now) const { return world_now + offset_; }
    // What the door does with the next link that the listener accepts: Open hands it to the manager, as on a real server; Scripted answers its Hello with `script_reason` and closes it, the manager
    // never sees it (the refusal of a server that lost the last second of its record, which no real moment of a test makes)
    enum class Door : uint8_t { Open, Scripted };
    Door door{Door::Open};
    net::RejectReason script_reason{net::RejectReason::BadRequest};
    uint32_t scripted{0};                                                                       // the links that were answered so
    void pump(uint32_t world_now) {
        if (!listener_) return;
        while (auto link = listener_->accept()) {
            ++accepted;
            std::shared_ptr<net::TcpConnection> shared(std::move(link));
            if (door == Door::Scripted) {
                pending_.push_back(shared);
                continue;
            }
            wires_.push_back(shared);
            mgr->add_connection(std::make_unique<Wire>(shared), "127.0.0.1", now(world_now));
        }
        for (size_t i = 0; i < pending_.size();) {
            std::vector<uint8_t> msg;
            if (pending_[i]->poll(msg)) {
                if (net::peek_type(msg) == net::MsgType::Hello) {
                    pending_[i]->send(net::encode(net::RejectMsg{script_reason}));
                    pending_[i]->close();
                    ++scripted;
                    pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            }
            ++i;
        }
        mgr->update(now(world_now));
    }
    // The cable of the machine whose link was the n-th that the door accepted (0: the first machine's first link), if it is still open: both ends see it closed
    bool cut_wire(size_t index) {
        if (index >= wires_.size()) return false;
        if (auto link = wires_[index].lock()) {
            if (link->is_open()) {
                link->close();
                return true;
            }
        }
        return false;
    }
    server::RoomStatus status(const std::string& code, uint32_t world_now) const {
        server::RoomStatus s;
        if (mgr != nullptr) mgr->status(code, s, now(world_now));
        return s;
    }

    std::unique_ptr<server::RoomManager> mgr;
    uint32_t accepted{0};

private:
    std::unique_ptr<net::TcpListener> listener_;
    uint16_t port_{0};
    uint32_t offset_{0};
    std::vector<std::weak_ptr<net::TcpConnection>> wires_;
    std::vector<std::shared_ptr<net::TcpConnection>> pending_;                                  // the links of a scripted door that have not said Hello yet
};

// A room that holds the seats of players whose connections are lost (the countdown after a pause is off unless a test is about it: each would wait ten seconds after every return)
server::RoomSpec held_spec(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
    server::RoomSpec s;
    s.code = code;
    s.map = map;
    s.players = players;
    s.has_seed = true;
    s.seed = 4242;
    s.reconnect = true;
    s.resume_countdown_ms = 0;
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The bare machine (a person who plays on another computer): an engine, its NetGame, and the little of an application that the room needs
// ---------------------------------------------------------------------------------------------------------------------------------

struct Machine {
    std::string name;
    sim::SimulationEngine sim;
    NetGame net{sim};
    std::vector<NetGame::Event> events;
    uint64_t ticks{0};
    bool orders{true};                            // it gives orders, as a person clicks
    uint32_t next_order_ms{0};
    uint32_t rng{1};
    uint32_t map_w{40};
    uint32_t map_h{40};
    std::vector<net::RejoinKey> keys_given;

    explicit Machine(std::string n, uint32_t seed = 1) : name(std::move(n)), rng(seed) {
        net.set_discovery(0);
        net.set_on_key([this](const net::RejoinKey& k) { keys_given.push_back(k); });
        net.set_on_tick([this]() { ++ticks; });
    }
    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    void handle(const NetGame::Event& ev) {
        events.push_back(ev);
        if (ev.type != NetGame::Event::Type::StartRequested) return;
        const net::StartMsg& s = net.start_info();
        assets::LevelData level;
        uint64_t hash = 0;
        const bool ok = level.load_lvl(maps_dir() + s.map_name) && net::hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
        if (ok) {
            sim.set_fog_of_war_enabled(s.fog);
            sim.init(level, s.seed, s.roster);
            for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
            map_w = level.width();
            map_h = level.height();
        }
        net.report_loaded(ok);
    }
    void frame(uint32_t now) {
        net.update(now);
        for (const NetGame::Event& ev : net.take_events()) handle(ev);
        if (net.phase() != NetGame::Phase::Playing || sim.is_match_over()) return;
        if (orders && now >= next_order_ms && sim.current_tick() > 0) {
            next_order_ms = now + 700;
            std::vector<uint32_t> mine;
            for (const auto& a : sim.get_world_state().ants) {
                if (a.player_id == net.my_seat()) mine.push_back(a.id);
            }
            if (!mine.empty()) {
                sim::Command c;
                c.type = sim::CommandType::GroupMove;
                c.tile_x = static_cast<int16_t>(next_random() % map_w);
                c.tile_y = static_cast<int16_t>(next_random() % map_h);
                for (size_t i = 0; i < mine.size() && i < 6; ++i) c.ants.push_back(mine[(i + next_random()) % mine.size()]);
                net.submit(c);
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// A server, an application and bare machines on one stepped clock
// ---------------------------------------------------------------------------------------------------------------------------------

struct World {
    Server server;
    std::unique_ptr<Application> app;
    std::vector<std::unique_ptr<Machine>> machines;
    uint32_t now{1000};
    uint32_t steps_{0};
    size_t app_wire{0};                           // the n-th link that the door accepted is the application's
    std::function<void(uint8_t)> seat_hook;       // given to every application that start_app makes (set_on_seat_known)

    uint32_t server_now() const { return server.now(now); }

    // The configuration of an application that joins the room `room` of the server: headless, with a settings file in `dir` (the key file lies beside it)
    ApplicationConfig config(const fs::path& dir, const std::string& room, const std::string& name) const {
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.lan_port = 0;
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        cfg.settings_path = (dir / "settings.ini").string();
        return cfg;
    }
    // The same with a window of the picture's own size (640 x 480 for the original's 4:3): the screenshot of a frame is the canvas pixel for pixel
    ApplicationConfig config_1to1(const fs::path& dir, const std::string& room, const std::string& name) const {
        ApplicationConfig cfg = config(dir, room, name);
        cfg.has_window_size = true;
        cfg.window_w = 640;
        cfg.window_h = 480;
        return cfg;
    }
    // The application joins (its link is the newest that the door accepted: a test can cut it)
    Application& start_app(const ApplicationConfig& cfg) {
        app = std::make_unique<Application>();
        if (seat_hook) app->set_on_seat_known(seat_hook);
        const uint32_t before = server.accepted;
        if (!app->init(cfg)) throw std::runtime_error("the application could not start");
        run_until([&]() { return server.accepted > before; }, 2000);
        app_wire = server.accepted > 0 ? server.accepted - 1 : 0;
        return *app;
    }
    // An application with the start menu (no join arguments: it connects when the player presses a button)
    Application& start_menu_app(const fs::path& dir) {
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.start_menu = true;
        cfg.lan_port = 0;
        cfg.settings_path = (dir / "settings.ini").string();
        app = std::make_unique<Application>();
        if (!app->init(cfg)) throw std::runtime_error("the application could not start");
        return *app;
    }
    // The game dies (a crash, a power cut, a closed tab): its link is cut, nothing is said to the server, and the file of its keys is as it was (the game's own goodbye, which lets go of the key,
    // is what a program that is closed on purpose does: here it does not run, so the file is put back as the death left it)
    void crash_app(const fs::path& dir) {
        const fs::path file = dir / "rejoin.txt";
        const std::string snapshot = read_file(file);
        app->net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });         // (a dead machine makes no new link)
        server.cut_wire(app_wire);
        run(100);
        app.reset();
        if (!snapshot.empty()) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << snapshot;
        }
    }
    Machine& join(const std::string& name, const std::string& room, uint8_t want_seat = 255) {
        machines.push_back(std::make_unique<Machine>(name, static_cast<uint32_t>(machines.size() + 1) * 7919u));
        Machine& m = *machines.back();
        if (!m.net.join("127.0.0.1", server.port(), name, want_seat, room)) throw std::runtime_error("join failed");
        const uint32_t before = server.accepted;
        run_until([&]() { return server.accepted > before; }, 2000);
        return m;
    }
    void pump(float dt) {
        server.pump(now);
        if (app) {
            app->pump_network(dt);
            app->update_simulation(dt);
        }
        for (auto& m : machines) m->frame(now);
    }
    // 10 ms of game time per step. The sockets are real and the clock is not: a loopback link delivers within the pass that wrote to it, but now and then the test gives the kernel a moment of real time
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            pump(0.010f);
            if ((++steps_ & 15u) == 0) std::this_thread::sleep_for(std::chrono::microseconds(300));
            else std::this_thread::yield();
        }
    }
    // The application alone runs for `ms` (the server and the machines stand still): a round trip that takes that long, however the system's loopback delivers
    void run_app_alone(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            if (app) {
                app->pump_network(0.010f);
                app->update_simulation(0.010f);
            }
            std::this_thread::yield();
        }
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
            if (cond()) return true;
            run(10);
        }
        // the game clock is virtual but the sockets are real: on a busy machine the kernel can be late with bytes that were sent long ago in game time. It gets up to two more seconds of real time
        // with the game clock standing still, so that nothing times out meanwhile; a wait that succeeds never gets here
        for (int i = 0; i < 2000 && !cond(); ++i) {
            pump(0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
    server::RoomStatus status(const std::string& code) const { return server.status(code, now); }
    // The match is under way on the application and on every machine given: it plays and has run ticks
    bool running(std::initializer_list<Machine*> ms) const {
        if (!app || app->state() != AppState::Playing || app->net() == nullptr || app->net()->phase() != NetGame::Phase::Playing || app->sim().current_tick() < 40) return false;
        for (const Machine* m : ms) {
            if (m->net.phase() != NetGame::Phase::Playing || m->ticks < 40) return false;
        }
        return true;
    }
};

// A server that only listens for the Hello of one machine (what a joining game says first): the tests look at the key and the seat that the game asks for
class HelloCatcher {
public:
    HelloCatcher() : listener_(net::TcpListener::listen(0, true)) {}
    uint16_t port() const { return listener_ ? listener_->port() : uint16_t{0}; }
    // Waits (real time) for a connection and its Hello; false when none came
    bool hear() {
        for (int i = 0; i < 3000; ++i) {
            if (!conn_) conn_ = listener_->accept();
            std::vector<uint8_t> msg;
            if (conn_ && conn_->poll(msg) && net::peek_type(msg) == net::MsgType::Hello && net::decode(msg, hello)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    net::HelloMsg hello;

private:
    std::unique_ptr<net::TcpListener> listener_;
    std::unique_ptr<net::TcpConnection> conn_;
};

// A line of the key file, as the store writes it
std::string key_line(const std::string& server, const std::string& room, unsigned seat, const net::SeatKey& key, int64_t written_ms) {
    return server + "\t" + room + "\t" + std::to_string(seat) + "\t" + rejoin_key_hex(key) + "\t" + std::to_string(written_ms / 1000) + "\n";
}

net::SeatKey key_of(uint8_t tag) {
    net::SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(tag + i * 7u + 1u);
    return k;
}

// A click on a control of the start menu: the pointer moves there, the left button goes down and up
void click_menu(Application& app, MenuId id) {
    MenuElement e;
    if (!app.start_menu().find_element(id, e)) throw std::runtime_error("no such control on the panel");
    const int32_t x = e.rect.x + e.rect.w / 2;
    const int32_t y = e.rect.y + e.rect.h / 2;
    app.start_menu().on_mouse_move(x, y);
    app.start_menu().on_mouse_down(x, y, SDL_BUTTON_LEFT);
    app.start_menu().on_mouse_up(x, y, SDL_BUTTON_LEFT);
}

bool menu_has(Application& app, MenuId id) {
    MenuElement e;
    return app.start_menu().find_element(id, e);
}

// The quit dialog's Yes, as the keys give it: Ctrl+Q opens the dialog, Y answers it
void quit_by_dialog(Application& app) {
    app.hud().open_quit_dialog();
    SDL_KeyboardEvent ke{};
    ke.type = SDL_KEYDOWN;
    ke.keysym.sym = SDLK_y;
    app.handle_key_down(ke);
}


SDL_KeyboardEvent key_event(SDL_Keycode sym, bool repeat = false, uint16_t mod = 0) {
    SDL_KeyboardEvent ke{};
    ke.type = SDL_KEYDOWN;
    ke.keysym.sym = sym;
    ke.keysym.mod = mod;
    ke.repeat = repeat ? 1 : 0;
    return ke;
}

SDL_MouseButtonEvent mouse_event(uint32_t type, int32_t x, int32_t y) {
    SDL_MouseButtonEvent be{};
    be.type = type;
    be.button = SDL_BUTTON_LEFT;
    be.x = x;
    be.y = y;
    be.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    be.clicks = 1;
    return be;
}

// A click on a rectangle of the picture: the pointer goes there, the left button goes down and up
void click(Application& app, const LayoutRect& r) {
    const int32_t x = r.x + r.w / 2;
    const int32_t y = r.y + r.h / 2;
    app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
    app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONUP, x, y));
}

// The machine is dead for good: it makes no new link (a person who closed the lid), whatever its NetGame tries
void make_dead(Machine& m) {
    m.net.set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
}

bool begins(const std::string& text, const std::string& start) { return text.compare(0, start.size(), start) == 0; }

std::string first_line(Application& app) {
    const NetOverlayLine l = app.net_overlay_now();
    return l.lines.empty() ? std::string() : l.lines[0];
}

// What the application shows and has done at one moment of a way back
struct Look {
    bool catching{false};                     // the catch-up screen is up
    int32_t percent{0};
    bool dialog{false};                       // the "Get ready to play!" dialog is up
    size_t channels{0};                       // the sounds that were started (nothing here plays them out: a channel stays until the mixer is told)
    AppState state{AppState::StartMenu};
    NetGame::Phase phase{NetGame::Phase::Off};
    uint64_t tick{0};
    std::string status;                       // the text of the status line
    bool overlay{false};                      // a line or the vote block of the overlay is there
};

Look look_at(Application& app) {
    Look l;
    l.catching = app.catch_up_screen_active();
    l.percent = app.catch_up_percent();
    l.dialog = app.hud().is_match_start_modal_active();
    l.channels = app.audio_mixer().active_channel_count();
    l.state = app.state();
    l.phase = app.net() != nullptr ? app.net()->phase() : NetGame::Phase::Off;
    l.tick = app.sim().current_tick();
    l.status = app.hud().status_line().text();
    const NetOverlayLine overlay = app.net_overlay_now();
    l.overlay = !overlay.lines.empty() || overlay.vote.open;
    return l;
}

std::vector<uint32_t> own_ants(Application& app) {
    std::vector<uint32_t> ids;
    for (const auto& a : app.sim().get_world_state().ants) {
        if (a.player_id == app.local_player_id() && a.hp > 0) ids.push_back(a.id);
    }
    return ids;
}

bool chat_has(Application& app, const std::string& text) {
    for (const std::string& line : app.hud().get_chat_log()) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// The picture of the next frame, read back: the renderer writes it as a BMP at the end of the frame
struct Picture {
    int32_t w{0};
    int32_t h{0};
    std::vector<uint8_t> rgba;
    bool ok() const { return w > 0 && h > 0; }
    bool is(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
        return rgba[i] == r && rgba[i + 1] == g && rgba[i + 2] == b;
    }
    int64_t differing(const Picture& o) const {
        if (w != o.w || h != o.h) return -1;
        int64_t n = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) n += (rgba[i] != o.rgba[i] || rgba[i + 1] != o.rgba[i + 1] || rgba[i + 2] != o.rgba[i + 2]) ? 1 : 0;
        return n;
    }
};

Picture grab(Application& app, const fs::path& file) {
    Picture p;
    app.renderer().request_screenshot(file.string());
    app.render_frame();
    SDL_Surface* raw = SDL_LoadBMP(file.string().c_str());
    if (raw == nullptr) return p;
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(raw);
    if (conv == nullptr) return p;
    p.w = conv->w;
    p.h = conv->h;
    p.rgba.resize(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 4u);
    for (int32_t y = 0; y < p.h; ++y) std::memcpy(p.rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(p.w) * 4u, static_cast<const uint8_t*>(conv->pixels) + static_cast<size_t>(y) * static_cast<size_t>(conv->pitch), static_cast<size_t>(p.w) * 4u);
    SDL_FreeSurface(conv);
    return p;
}

// The catch-up screen as the application draws the classic page: the loading screen's orange margin and the bar filled to the percent (what the match shows at those places is the HUD's frame and the map)
bool catch_up_picture(const Picture& pic, int32_t percent, std::string& why) {
    const LoadingLayout& l = LoadingLayout::classic();
    if (pic.w != 640 || pic.h != 480) {
        why = "the picture is " + std::to_string(pic.w) + " x " + std::to_string(pic.h);
        return false;
    }
    const int32_t fill = std::clamp(percent, 0, 100) * l.bar.w / 100;
    const int32_t y = l.bar.y + l.bar.h / 2;
    if (!pic.is(12, 12, 219, 75, 19)) why = "no orange at (12, 12)";
    else if (fill == 0 && pic.is(l.bar.x, y, 31, 23, 51)) why = "the bar has a fill at 0%";
    else if (fill > 0 && !(pic.is(l.bar.x, y, 31, 23, 51) && pic.is(l.bar.x + fill - 1, y, 31, 23, 51))) why = "the bar is not filled to " + std::to_string(fill) + " px";
    else if (fill < l.bar.w && pic.is(l.bar.x + fill, y, 31, 23, 51)) why = "the bar is filled beyond " + std::to_string(fill) + " px";
    return why.empty();
}

// An own ant (other than `except`) that stands in the map view: its screen place is where a click selects it (0: none does)
uint32_t own_ant_in_view(Application& app, uint32_t except = 0) {
    const ViewportCamera& cam = app.renderer().camera();
    const LayoutRect view = app.layout().view();
    for (const auto& a : app.sim().get_world_state().ants) {
        if (a.player_id != app.local_player_id() || a.hp == 0 || a.id == except) continue;
        const int32_t x = cam.view_x + (a.px - cam.world_x);
        const int32_t y = cam.view_y + (a.py - cam.world_y);
        if (x >= view.x + 20 && x < view.right() - 20 && y >= view.y + 20 && y < view.bottom() - 20) return a.id;
    }
    return 0;
}

// What the player does at once: the question whether the wheel may zoom the map, a click on an own ant, typed text (it goes to the chat box), a key of the match (Ctrl+O opens the options).
// What of it reached the match?
struct Reach {
    bool wheel{false};
    bool click{false};
    bool text{false};
    bool options{false};
};

Reach poke(Application& app, uint32_t ant_id) {
    Reach r;
    const LayoutRect view = app.layout().view();
    r.wheel = app.view_zoom_allowed(view.x + view.w / 2, view.y + view.h / 2);
    const uint32_t selected = app.hud().get_selected_ant_id();
    for (const auto& a : app.sim().get_world_state().ants) {
        if (a.id != ant_id) continue;
        const ViewportCamera& cam = app.renderer().camera();
        const int32_t x = cam.view_x + (a.px - cam.world_x);
        const int32_t y = cam.view_y + (a.py - cam.world_y);
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = x;
        motion.y = y;
        app.handle_mouse_motion(motion);
        app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
        app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONUP, x, y));
    }
    r.click = app.hud().get_selected_ant_id() != selected;
    SDL_Event typed{};                                                                           // text is an event of the loop: it is read there
    typed.type = SDL_TEXTINPUT;
    typed.text.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
    std::snprintf(typed.text.text, sizeof typed.text.text, "zz");
    SDL_PushEvent(&typed);
    app.run_frame_with_delta(0.001f);
    r.text = !app.hud().get_chat_input().empty();
    for (int i = 0; i < 8 && !app.hud().get_chat_input().empty(); ++i) app.hud().handle_key_down(SDLK_BACKSPACE, app.sim(), app.renderer().camera(), 0, false);      // (the HUD's own: the loop's key may be swallowed)
    app.handle_key_down(key_event(SDLK_o, false, KMOD_LCTRL));
    r.options = app.hud().is_modal_open();
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------------------------------------------------------------

void run_key_tests() {
    TEST_CASE("RA1.1 The Application Keeps The Key Of Its Seat In A File Beside Its Settings As Soon As The Match Begins (The Start; A Waiting Room Gives It None: Mode 0600, The Server, The Room And The Seat In Its Line); Bob's Key Is Not In It; Leaving Lets Go Of It, And Nothing That The Program Printed Holds A Key") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-1"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra11");
        Application& app = w.start_app(w.config(dir, "RA-1", "Ann"));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->phase() == NetGame::Phase::Room && app.net()->my_seat() == 0; }, 5000));
        w.run(500);
        ASSERT_TRUE(app.rejoin_store() != nullptr && app.rejoin_store()->entries().empty() && !fs::exists(dir / "rejoin.txt"));       // the room's Welcome gave a key, and a waiting room keeps none (the review: a visit alone leaves nothing to outlive it)
        Machine& bob = w.join("Bob", "RA-1");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        ASSERT_TRUE(w.run_until([&]() { return app.rejoin_store()->entries().size() == 1; }, 2000));        // the Start gave the key
        ASSERT_TRUE(!bob.keys_given.empty());
        const net::SeatKey bob_key = bob.keys_given[0].key;
        // the file: one line, its server, room and seat, the key of the application's seat (not Bob's)
        const fs::path file = dir / "rejoin.txt";
        ASSERT_TRUE(fs::exists(file));
#if !defined(_WIN32)
        struct stat st{};
        ASSERT_TRUE(::stat(file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
        const std::vector<RejoinEntry> kept = app.rejoin_store()->entries();
        ASSERT_EQ(kept.size(), size_t{1});
        ASSERT_TRUE(kept[0].server == "127.0.0.1:" + std::to_string(w.server.port()) && kept[0].room == "RA-1" && kept[0].seat == 0 && !net::key_is_zero(kept[0].key));
        ASSERT_FALSE(net::key_matches(kept[0].key, bob_key));
        const std::string text = read_file(file);
        ASSERT_TRUE(text.find(rejoin_key_hex(kept[0].key)) != std::string::npos && text.find(rejoin_key_hex(bob_key)) == std::string::npos);
        ASSERT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
        // a second store on the same file reads what the application wrote (the file is what a game that is started again finds)
        FileRejoinStore again(file.string());
        ASSERT_TRUE(again.find("127.0.0.1:" + std::to_string(w.server.port()), "RA-1") && net::key_matches(again.find("127.0.0.1:" + std::to_string(w.server.port()), "RA-1")->key, kept[0].key));
        // the player leaves for good: the key is let go of
        app.quit();
        ASSERT_TRUE(app.rejoin_store()->entries().empty());
        ASSERT_FALSE(fs::exists(file));
        // nothing that the program printed holds a key
        const std::string logged = output.text();
        ASSERT_TRUE(logged.find(rejoin_key_hex(kept[0].key)) == std::string::npos && logged.find(rejoin_key_hex(bob_key)) == std::string::npos);
    } TEST_END();
}

void run_use_tests() {
    TEST_CASE("RA4.1 A Game That Is Started Again With The Join Arguments Of Its Room (--join ADDR --room CODE, No Seat) Finds The Key In Its File And Takes Its Seat In The Running Match: The Room Counts A Rejoin, The Seat Is The Same, The Game Is Given The Match From The Server's Log (The Catch-Up Screen With A Percent That Grows, Nothing Of The Match Reached By Key, Click Or Wheel), The Match Begins For It Without The Start Of A Match (No Dialog, No Start Sound, No Start News), And It Ends In The Same State As Bob; The File Has The Same Key Again") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-4"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra41");
        const ApplicationConfig cfg = w.config_1to1(dir, "RA-4", "Ann");
        Application& first = w.start_app(cfg);
        Machine& bob = w.join("Bob", "RA-4");
        ASSERT_TRUE(w.run_until([&]() { return first.state() == AppState::Playing; }, 12000));
        ASSERT_TRUE(first.hud().is_match_start_modal_active() && first.audio_mixer().active_channel_count() >= 1 && chat_has(first, "Game started!") && !first.catch_up_screen_active());      // (a match that starts: the dialog, the start sound, the start news)
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(25000);                                                                            // some 500 turns: the catch-up of a game that starts from nothing is more than a frame's work
        const uint8_t seat = first.net()->my_seat();
        ASSERT_EQ(seat, uint8_t{0});
        ASSERT_TRUE(first.rejoin_store()->entries().size() == 1);
        const net::SeatKey key = first.rejoin_store()->entries()[0].key;
        w.crash_app(dir);                                                                        // the game dies
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-4").paused; }, 3000));
        ASSERT_TRUE(w.status("RA-4").absent.size() == 1 && w.status("RA-4").absent[0].seat == seat);
        const uint32_t sealed = w.status("RA-4").turns;
        ASSERT_TRUE(sealed > 400);
        FileRejoinStore file((dir / "rejoin.txt").string());                                     // (the file as the death left it: the key is there)
        ASSERT_TRUE(file.find("127.0.0.1:" + std::to_string(w.server.port()), "RA-4", seat) && net::key_matches(file.find("127.0.0.1:" + std::to_string(w.server.port()), "RA-4", seat)->key, key));
        // the game is started again, with the arguments of its room and no key. Every step of its way back is looked at
        Application& second = w.start_app(cfg);
        std::vector<Look> looks;
        bool began = false;
        bool poked = false;
        Reach blocked;
        bool picture_ok = false;
        std::string picture_problem;
        for (uint32_t elapsed = 0; elapsed < 30000; elapsed += 10) {
            w.run(10);
            const Look now = look_at(second);
            looks.push_back(now);
            if (!began && now.state == AppState::Playing) {                                      // the step in which the match begins for it
                began = true;
                ASSERT_TRUE(now.catching && !now.dialog && now.channels == 0);                  // the catch-up screen, no dialog, no start sound
                ASSERT_FALSE(chat_has(second, "Game started!"));                                // no start news, and no "Welcome to Ants!" on the status line
                ASSERT_TRUE(second.hud().status_line().text().empty() && second.hud().get_chat_log().empty());
            }
            if (!poked && now.catching && now.state == AppState::Playing) {                      // on the catch-up screen: the match takes no key, no click, no wheel
                poked = true;
                const uint32_t ant = own_ant_in_view(second);
                ASSERT_TRUE(ant != 0);
                blocked = poke(second, ant);
                picture_ok = catch_up_picture(grab(second, dir / "ra41.bmp"), second.catch_up_percent(), picture_problem);
            }
            if (began && !now.catching && !w.status("RA-4").paused) break;
        }
        ASSERT_TRUE(began && poked);
        ASSERT_FALSE(blocked.options || blocked.click || blocked.wheel || blocked.text);
        if (!picture_ok) std::cout << "\n    [the catch-up screen] " << picture_problem;
        ASSERT_TRUE(picture_ok);
        size_t catching_steps = 0;
        int32_t last_percent = -1;
        bool grew = false;
        uint64_t last_catching_tick = 0;
        for (const Look& l : looks) {
            if (l.state != AppState::Playing) continue;
            ASSERT_FALSE(l.dialog);                                                              // no dialog at any moment of the way back
            if (!l.catching) continue;
            ++catching_steps;
            ASSERT_TRUE(l.percent >= last_percent && l.percent <= 100);
            grew = grew || (last_percent >= 0 && l.percent > last_percent);
            last_percent = l.percent;
            last_catching_tick = l.tick;
        }
        ASSERT_TRUE(catching_steps >= 2 && grew);                                                // the percent grew over several frames (500 turns, 200 a frame)
        ASSERT_TRUE(second.state() == AppState::Playing && !second.catch_up_screen_active() && !w.status("RA-4").paused);
        const server::RoomStatus s = w.status("RA-4");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.names[seat] == "Ann");
        ASSERT_EQ(second.net()->my_seat(), seat);
        ASSERT_EQ(second.local_player_id(), seat);
        ASSERT_TRUE(second.net()->turns_executed() >= sealed);                                   // the whole match was given to it
        // the first tick after the catch-up does not play what the replay queued, and the match screen is back: the same key, click and wheel now reach it (what was refused above was the screen's doing)
        ASSERT_TRUE(w.run_until([&]() { return second.sim().current_tick() > last_catching_tick; }, 2000));
        ASSERT_EQ(second.audio_mixer().active_channel_count(), size_t{0});
        const uint32_t ant = own_ant_in_view(second);
        ASSERT_TRUE(ant != 0);
        const Reach reached = poke(second, ant);
        ASSERT_TRUE(reached.wheel && reached.click && reached.text && reached.options);
        second.handle_key_down(key_event(SDLK_ESCAPE));                                          // (closes the options again)
        ASSERT_FALSE(chat_has(second, "Game started!"));
        w.run(3000);
        ASSERT_TRUE(second.net()->turns_executed() > sealed + 40);                               // and it goes on
        // the Welcome of the rejoin said the key again: the file has it, the same one
        ASSERT_TRUE(second.rejoin_store()->entries().size() == 1 && net::key_matches(second.rejoin_store()->entries()[0].key, key) && second.rejoin_store()->entries()[0].seat == seat);
        // the same state as Bob's, at a tick that both stand at
        bool agree = false;
        for (int i = 0; i < 400 && !agree; ++i) {
            if (second.sim().current_tick() == bob.sim.current_tick()) agree = second.sim().state_hash() == bob.sim.state_hash();
            if (!agree) w.run(10);
        }
        ASSERT_TRUE(agree);
        ASSERT_FALSE(second.net()->desynced() || bob.net.desynced());
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
    } TEST_END();

    TEST_CASE("RA4.2 Which Key A Join Uses: The Newest Entry Of That Server And Room (With The Entry's Seat In The Hello, The Key Alone Never In The Address), Or Of The Seat That --seat Asks For; Another Room, Another Server, Another Seat, An Entry Of More Than A Day, And A Join Without A Room Use None") {
        struct Row {
            const char* what;
            std::vector<std::string> lines;          // the key file ({port} stands for the test server's port); times are minutes before now
            std::string room;
            uint8_t seat;                            // --seat (255: not given)
            int expect_key;                          // the tag of the key that the Hello must show, 0: none
            int expect_seat;                         // the seat that the Hello asks for
        };
        const int64_t now = wall_clock_ms();
        const auto line = [&](const std::string& server, const std::string& room, unsigned seat, uint8_t tag, int64_t minutes_ago) {
            return key_line(server, room, seat, key_of(tag), now - minutes_ago * 60 * 1000);
        };
        const std::string me = "127.0.0.1:{port}";
        const std::vector<Row> rows = {
            {"the entry of the room", {line(me, "ROOM-A", 1, 11, 5)}, "ROOM-A", 255, 11, 1},
            {"--seat that is the entry's seat", {line(me, "ROOM-A", 1, 11, 5)}, "ROOM-A", 1, 11, 1},
            {"--seat that no entry holds: a new player", {line(me, "ROOM-A", 1, 11, 5)}, "ROOM-A", 2, 0, 2},
            {"another room", {line(me, "ROOM-A", 1, 11, 5)}, "ROOM-B", 255, 0, -1},
            {"another server", {line("elsewhere.example:4001", "ROOM-A", 1, 11, 5), line("127.0.0.1:1", "ROOM-A", 1, 12, 4)}, "ROOM-A", 255, 0, -1},
            {"an entry of three hours and a minute", {line(me, "ROOM-A", 1, 11, 3 * 60 + 1)}, "ROOM-A", 255, 0, -1},
            {"an entry of three hours less a minute", {line(me, "ROOM-A", 1, 11, 3 * 60 - 1)}, "ROOM-A", 255, 11, 1},
            {"two seats of the room: the newest", {line(me, "ROOM-A", 0, 21, 60), line(me, "ROOM-A", 3, 22, 10)}, "ROOM-A", 255, 22, 3},
            {"two seats of the room, --seat 0: the older", {line(me, "ROOM-A", 0, 21, 60), line(me, "ROOM-A", 3, 22, 10)}, "ROOM-A", 0, 21, 0},
            {"no room (a direct join)", {line(me, "ROOM-A", 1, 11, 5)}, "", 255, 0, -1},
        };
        for (const Row& row : rows) {
            HelloCatcher catcher;
            ASSERT_TRUE(catcher.port() != 0);
            const fs::path dir = scratch_dir("ra42");
            std::string text;
            for (std::string l : row.lines) {
                const size_t at = l.find("{port}");
                if (at != std::string::npos) l.replace(at, 6, std::to_string(catcher.port()));
                text += l;
            }
            {
                std::ofstream out(dir / "rejoin.txt", std::ios::binary);
                out << text;
            }
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.lan_port = 0;
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = catcher.port();
            cfg.net_room = row.room;
            cfg.net_seat = row.seat;
            cfg.player_name = "Ann";
            cfg.settings_path = (dir / "settings.ini").string();
            Application app;
            ASSERT_TRUE(app.init(cfg));
            for (int i = 0; i < 100 && app.net() != nullptr; ++i) {                              // (the Hello goes out at the first update of the connection)
                app.pump_network(0.010f);
                if (i % 10 == 9) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool heard = catcher.hear();
            if (!heard) std::cout << "\n    [" << row.what << "] no Hello arrived";
            ASSERT_TRUE(heard);
            const net::HelloMsg& h = catcher.hello;
            const bool key_ok = row.expect_key == 0 ? net::key_is_zero(h.key) : net::key_matches(h.key, key_of(static_cast<uint8_t>(row.expect_key)));
            if (!key_ok) std::cout << "\n    [" << row.what << "] the Hello shows the wrong key";
            ASSERT_TRUE(key_ok);
            if (row.expect_seat >= 0 && h.want_seat != static_cast<uint8_t>(row.expect_seat)) std::cout << "\n    [" << row.what << "] the Hello asks for seat " << static_cast<unsigned>(h.want_seat);
            ASSERT_TRUE(row.expect_seat < 0 ? h.want_seat == 255 : h.want_seat == static_cast<uint8_t>(row.expect_seat));
            ASSERT_TRUE(h.room == row.room && h.have_turns == 0);                                // (a machine that starts from nothing says no turns)
        }
    } TEST_END();
}

void run_menu_tests() {
    TEST_CASE("RA7.1 The Start Menu Offers \"Rejoin your match (CODE)\" While A Fresh Key Is In The File And Not Without One; Pressing It Joins The Offer's Server, Room And Seat With The Key (Not The Menu's Own Server) And The Match Begins By Itself, Without The Quick Help Or The Room's Screens; Leaving The Match Lets Go Of The Key And The Button Is Gone") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-7"), w.server_now()).ok);
        {   // a game with no key in its file: the first panel is the one it always was
            Application plain;
            ApplicationConfig pc;
            pc.headless = true;
            pc.start_in_map_select = true;
            pc.start_menu = true;
            pc.lan_port = 0;
            pc.settings_path = (scratch_dir("ra71-plain") / "settings.ini").string();
            ASSERT_TRUE(plain.init(pc));
            ASSERT_EQ(plain.state(), AppState::StartMenu);
            ASSERT_FALSE(plain.start_menu().rejoin().has_value() || menu_has(plain, MenuId::Rejoin));
        }
        const fs::path dir = scratch_dir("ra71");
        Application& first = w.start_app(w.config(dir, "RA-7", "Ann"));
        Machine& bob = w.join("Bob", "RA-7");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(10000);
        const uint8_t seat = first.net()->my_seat();
        const net::SeatKey key = first.rejoin_store()->entries()[0].key;
        w.crash_app(dir);                                                                        // the game dies; its key stays in the file
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-7").paused; }, 3000));
        // the game is started again with the menu and nothing on its command line: the first panel offers the match
        Application& menu = w.start_menu_app(dir);
        ASSERT_EQ(menu.state(), AppState::StartMenu);
        ASSERT_TRUE(menu.start_menu().rejoin().has_value());
        ASSERT_TRUE(menu.start_menu().rejoin()->room == "RA-7" && menu.start_menu().rejoin()->seat == seat);
        ASSERT_TRUE(menu.start_menu().rejoin()->server.host == "127.0.0.1" && menu.start_menu().rejoin()->server.port == w.server.port());
        ASSERT_FALSE(menu.start_menu().server().host == "127.0.0.1");                           // (the menu's own server is the default one: the offer's is used)
        MenuElement button;
        ASSERT_TRUE(menu.start_menu().find_element(MenuId::Rejoin, button) && button.text == "Rejoin your match (RA-7)");
        click_menu(menu, MenuId::Rejoin);
        ASSERT_EQ(menu.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_TRUE(w.run_until([&]() { return menu.state() == AppState::Playing && !w.status("RA-7").paused; }, 30000));
        const server::RoomStatus s = w.status("RA-7");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.rejoins == 1 && s.absent.empty());
        ASSERT_TRUE(menu.net() != nullptr && menu.net()->my_seat() == seat && menu.local_player_id() == seat);
        ASSERT_TRUE(menu.window_title().find("room RA-7") != std::string::npos);
        ASSERT_TRUE(menu.rejoin_store()->entries().size() == 1 && net::key_matches(menu.rejoin_store()->entries()[0].key, key));        // the Welcome of the rejoin said it again
        w.run(2000);
        ASSERT_TRUE(menu.net()->turns_executed() > 200);
        // the player quits through the quit dialog (two sides: the quit ends the match for both): the match is over, the key is let go of; the results' Leave brings the menu back, and it has no button
        quit_by_dialog(menu);
        ASSERT_TRUE(w.run_until([&]() { return menu.scorecard().is_open(); }, 5000));
        ASSERT_TRUE(menu.rejoin_store()->entries().empty());
        ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
        SDL_KeyboardEvent leave{};
        leave.type = SDL_KEYDOWN;
        leave.keysym.sym = SDLK_c;
        menu.handle_key_down(leave);
        ASSERT_EQ(menu.state(), AppState::StartMenu);
        ASSERT_FALSE(menu.start_menu().rejoin().has_value() || menu_has(menu, MenuId::Rejoin));
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
    } TEST_END();

    TEST_CASE("RA7.2 The Offer Is Only A Fresh Key Of A Server That The Menu Can Reach (An Entry Of More Than A Day, And The Browser's Entry, A URL, Are Not Offered); A Key That The Server Refuses (The Match Is Over) Comes Back To The First Panel With The Reason And The Button Is Gone; A Key That Was Let Go Of While The Menu Stood Open Says So At The Press") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        const std::string server = "127.0.0.1:" + std::to_string(w.server.port());
        const int64_t now = wall_clock_ms();
        const auto write_file = [](const fs::path& path, const std::string& text) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        };
        // not offered: three hours and a minute old, a URL
        {
            const fs::path dir = scratch_dir("ra72a");
            write_file(dir / "rejoin.txt", key_line(server, "OLD-1", 0, key_of(1), now - 3 * 3600 * 1000 - 60 * 1000) + key_line("wss://play.example.org/game", "WEB-1", 1, key_of(2), now));
            Application& menu = w.start_menu_app(dir);
            ASSERT_FALSE(menu.start_menu().rejoin().has_value() || menu_has(menu, MenuId::Rejoin));
        }
        // offered: the newest fresh one of a server that can be reached, of several
        {
            const fs::path dir = scratch_dir("ra72b");
            write_file(dir / "rejoin.txt", key_line("wss://play.example.org/game", "WEB-1", 1, key_of(2), now) + key_line(server, "OLDER-1", 2, key_of(3), now - 120 * 1000) +
                                               key_line(server, "NEWEST-1", 3, key_of(4), now - 60 * 1000));
            Application& menu = w.start_menu_app(dir);
            ASSERT_TRUE(menu.start_menu().rejoin().has_value() && menu.start_menu().rejoin()->room == "NEWEST-1" && menu.start_menu().rejoin()->seat == 3);   // (the URL is newer, and is no offer for this menu)
        }
        // a key for a match that is over: the server has no such room
        {
            const fs::path dir = scratch_dir("ra72c");
            write_file(dir / "rejoin.txt", key_line(server, "GONE-1", 1, key_of(5), now - 60 * 1000));
            Application& menu = w.start_menu_app(dir);
            ASSERT_TRUE(menu.start_menu().rejoin().has_value());
            click_menu(menu, MenuId::Rejoin);
            ASSERT_TRUE(w.run_until([&]() { return menu.start_menu().panel() == MenuPanel::Main; }, 10000));
            ASSERT_EQ(menu.state(), AppState::StartMenu);
            ASSERT_EQ(menu.start_menu().message(), std::string("The match is over."));
            ASSERT_FALSE(menu.start_menu().rejoin().has_value() || menu_has(menu, MenuId::Rejoin));      // the refusal let go of the key
            ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
            ASSERT_TRUE(menu.net() == nullptr);                                                          // (nothing of the attempt stays)
        }
        // the key is let go of while the menu stands open (another window left the match): the press says so
        {
            const fs::path dir = scratch_dir("ra72d");
            write_file(dir / "rejoin.txt", key_line(server, "GONE-2", 0, key_of(6), now - 60 * 1000));
            Application& menu = w.start_menu_app(dir);
            ASSERT_TRUE(menu.start_menu().rejoin().has_value());
            std::filesystem::remove(dir / "rejoin.txt");
            click_menu(menu, MenuId::Rejoin);
            w.run(100);
            ASSERT_EQ(menu.start_menu().panel(), MenuPanel::Main);
            ASSERT_TRUE(menu.start_menu().message().find("no match to rejoin") != std::string::npos);
            ASSERT_FALSE(menu.start_menu().rejoin().has_value());
            ASSERT_TRUE(menu.net() == nullptr);
        }
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key_of(5))) != std::string::npos);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The overlay, the vote and the countdown
// ---------------------------------------------------------------------------------------------------------------------------------


void run_screen_tests() {
    TEST_CASE("RA1.2 The Application's Own Link Is Cut: The Overlay Says \"Connection lost. Reconnecting... 0:03\" With The Seconds Since The Loss And, From The Second Link On, The Attempt (And \"Esc leaves the match\" Under It); \"Waiting For The Other Players...\" Never Shows Meanwhile; The Words Are Gone When The Match Is Back") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-12"), w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra12"), "RA-12", "Ann"));
        Machine& bob = w.join("Bob", "RA-12");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(2000);
        ASSERT_TRUE(app.net_overlay_now().lines.empty());                                       // (a match that runs says nothing)
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });           // (the network is down: no new link can be made)
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        std::vector<std::string> seen;                                                           // every first line, in order, without repeats
        bool waiting_shown = false;
        bool second_line_ok = true;
        for (int step = 0; step < 520; ++step) {                                                 // 5.2 s
            w.run(10);
            const NetOverlayLine l = app.net_overlay_now();
            if (!l.lines.empty() && (seen.empty() || seen.back() != l.lines[0])) seen.push_back(l.lines[0]);
            for (const std::string& t : l.lines) waiting_shown = waiting_shown || begins(t, "Waiting for");
            if (!l.lines.empty()) second_line_ok = second_line_ok && l.lines.size() == 2 && l.lines[1] == "Esc leaves the match" && !l.alarm;
        }
        ASSERT_TRUE(app.net()->paused() && app.net()->pause_info().reconnecting && app.net()->phase() == NetGame::Phase::Playing);
        ASSERT_FALSE(waiting_shown);
        ASSERT_TRUE(second_line_ok);
        ASSERT_TRUE(seen.size() >= 6);                                                           // a new line at least every second
        ASSERT_EQ(seen.front(), std::string("Connection lost. Reconnecting... 0:00"));
        bool has_one = false;
        bool has_two = false;
        bool has_three = false;
        for (const std::string& t : seen) {
            has_one = has_one || t == "Connection lost. Reconnecting... 0:01";
            has_two = has_two || t == "Connection lost. Reconnecting... 0:02 (attempt 2)";
            has_three = has_three || t == "Connection lost. Reconnecting... 0:04 (attempt 3)";
        }
        ASSERT_TRUE(has_one && has_two && has_three);                                            // the first attempt is no "attempt"; the second link is (at 2 s), the third (at 4 s)
        for (const std::string& t : seen) ASSERT_TRUE(begins(t, "Connection lost. Reconnecting... 0:0"));
        // the network is back: the machine finds its way back by itself, and the words are gone
        app.net()->set_link_maker_for_test(nullptr);
        ASSERT_TRUE(w.run_until([&]() { return !app.net()->paused(); }, 15000));
        ASSERT_TRUE(app.net_overlay_now().lines.empty() || !begins(first_line(app), "Connection lost"));
        w.run(500);
        ASSERT_TRUE(app.net_overlay_now().lines.empty());
        ASSERT_TRUE(w.status("RA-12").rejoins == 1);
    } TEST_END();

    TEST_CASE("RA2.1 The Other Player's Link Is Cut (Three In The Room): No Banner For A Blip, After A Second \"Bob (Red) lost the connection, waiting 0:NN\" With The Seconds; When The Vote Opens The Block Shows \"0 of 2 voted to continue\" With F2 Keep waiting And F3 Continue without Bob; F3 Votes To Continue (The Count And The Pressed Button Change, The Server Counts It, The Seat Stays), F2 Takes It Back, F3 Again, And The Mouse Does The Same Without Touching The Map; A Dialog Over The Match Takes The Keys") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        server::RoomSpec spec = held_spec("RA-21", 3);
        spec.vote_after_ms = 5000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra21"), "RA-21", "Ann"));
        Machine& bob = w.join("Bob", "RA-21");
        Machine& cat = w.join("Cat", "RA-21");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob, &cat}); }, 12000 + kPre));
        w.run(1000);
        ASSERT_EQ(bob.net.my_seat(), uint8_t{1});
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->paused(); }, 3000));
        ASSERT_TRUE(app.net_overlay_now().lines.empty());                                        // the first moments of the pause: no banner
        w.run(500);
        ASSERT_TRUE(app.net_overlay_now().lines.empty() && !app.net_overlay_now().vote.open);
        ASSERT_TRUE(w.run_until([&]() { return !app.net_overlay_now().lines.empty(); }, 2000));  // a second after the pause began
        ASSERT_TRUE(begins(first_line(app), "Bob (Red) lost the connection, waiting 0:0"));
        ASSERT_FALSE(app.net_overlay_now().vote.open);
        ASSERT_FALSE(app.net_vote_buttons().open);
        // the seconds go up
        ASSERT_TRUE(w.run_until([&]() { return first_line(app) == "Bob (Red) lost the connection, waiting 0:03"; }, 4000));
        // the vote opens after 5 s of absence
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.open; }, 8000));
        NetOverlayLine l = app.net_overlay_now();
        ASSERT_TRUE(l.lines.size() == 1 && begins(l.lines[0], "Bob (Red) lost the connection, waiting 0:0") && l.vote.count == "0 of 2 voted to continue" && l.vote.keep == "F2 Keep waiting" &&
                    l.vote.go_on == "F3 Continue without Bob" && !l.vote.keep_pressed && !l.vote.go_on_pressed);
        Application::NetVoteButtons buttons = app.net_vote_buttons();
        ASSERT_TRUE(buttons.open && buttons.keep.w > 0 && buttons.go_on.w > 0 && buttons.keep.x + buttons.keep.w < buttons.go_on.x);
        ASSERT_TRUE(buttons.keep.contains(buttons.keep.x + 2, buttons.keep.y + 2) && !buttons.keep.contains(buttons.go_on.x + 2, buttons.go_on.y + 2));
        // the keys: F3 goes on without Bob
        app.handle_key_down(key_event(SDLK_F3));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.go_on_pressed; }, 3000));
        l = app.net_overlay_now();
        ASSERT_TRUE(l.vote.count == "1 of 2 voted to continue" && !l.vote.keep_pressed);
        ASSERT_TRUE(w.status("RA-21").votes_continue == 1 && w.status("RA-21").voters == 2 && w.status("RA-21").vote_seat == 1);                      // the server counted it
        ASSERT_TRUE(w.status("RA-21").drops_by_vote == 0 && w.status("RA-21").paused);                                                              // (one of two is not more than half)
        // F2 takes it back, a held F3 (a key repeat) does nothing, F3 again
        app.handle_key_down(key_event(SDLK_F2));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.keep_pressed; }, 3000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "0 of 2 voted to continue" && !app.net_overlay_now().vote.go_on_pressed && w.status("RA-21").votes_continue == 0);
        app.handle_key_down(key_event(SDLK_F3, true));
        w.run(300);
        ASSERT_TRUE(app.net_overlay_now().vote.keep_pressed && w.status("RA-21").votes_continue == 0);                                              // (a repeat is not a press)
        app.handle_key_down(key_event(SDLK_F3));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.go_on_pressed; }, 3000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "1 of 2 voted to continue");
        // a dialog over the match takes the keys: the quit dialog is open, F2 does nothing; closed, it works again
        app.hud().open_quit_dialog();
        app.handle_key_down(key_event(SDLK_F2));
        w.run(300);
        ASSERT_TRUE(app.net_overlay_now().vote.go_on_pressed && w.status("RA-21").votes_continue == 1);
        app.hud().close_quit_dialog();
        // the mouse: a press and a release on the button; a release elsewhere is nothing; the map under the block gets nothing (a selected ant stays selected)
        const std::vector<uint32_t> mine = [&]() {
            std::vector<uint32_t> ids;
            for (const auto& a : app.sim().get_world_state().ants) {
                if (a.player_id == app.local_player_id() && a.hp > 0) ids.push_back(a.id);
            }
            return ids;
        }();
        ASSERT_FALSE(mine.empty());
        app.hud().select_ant(mine[0]);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        buttons = app.net_vote_buttons();
        click(app, buttons.keep);
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.keep_pressed; }, 3000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "0 of 2 voted to continue" && w.status("RA-21").votes_continue == 0);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);                                     // the click did not reach the map
        app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONDOWN, buttons.go_on.x + 3, buttons.go_on.y + 3));
        ASSERT_FALSE(app.hud().is_input_captured());                                             // (the button took the press: the map has no rubber band)
        app.handle_mouse_button(mouse_event(SDL_MOUSEBUTTONUP, buttons.go_on.x - 40, buttons.go_on.y + 3));    // released beside it: nothing
        w.run(300);
        ASSERT_TRUE(app.net_overlay_now().vote.keep_pressed && w.status("RA-21").votes_continue == 0);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        click(app, buttons.go_on);
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.go_on_pressed; }, 3000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "1 of 2 voted to continue" && w.status("RA-21").votes_continue == 1);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        // Cat votes too: two of two go on without Bob; the room drops the seat and the match goes on (no countdown in this room)
        ASSERT_TRUE(cat.net.vote(false));
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-21").drops_by_vote == 1; }, 5000));
        ASSERT_TRUE(w.run_until([&]() { return !app.net()->paused() && app.net_overlay_now().lines.empty() && !app.net_overlay_now().vote.open; }, 5000));
        ASSERT_FALSE(app.net_vote_buttons().open);
        const uint32_t ticks = w.status("RA-21").ticks;
        w.run(2000);
        ASSERT_TRUE(w.status("RA-21").ticks > ticks + 20);                                       // the match goes on
    } TEST_END();

    TEST_CASE("RA3.1 The Countdown That Follows A Pause: When The Missing Player Is Back, \"Bob is back: the match goes on in 10\" Counts Down Once A Second To The Match's First Turn; After A Vote That Dropped The Seat Nobody Is Named (\"The match goes on in 3\"); A Blip Has None") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        server::RoomSpec spec = held_spec("RA-31");
        spec.resume_countdown_ms = 10000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra31"), "RA-31", "Ann"));
        Machine& bob = w.join("Bob", "RA-31");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(1000);
        // a blip: Bob's link is cut and he comes back at once: no pause of 3 s, no countdown, and nothing was ever on the screen
        std::vector<std::string> blip_lines;
        ASSERT_TRUE(w.server.cut_wire(1));
        for (int i = 0; i < 400; ++i) {
            w.run(10);
            const NetOverlayLine l = app.net_overlay_now();
            if (!l.lines.empty()) blip_lines.push_back(l.lines[0]);
        }
        ASSERT_TRUE(w.status("RA-31").rejoins == 1 && !w.status("RA-31").paused && w.status("RA-31").resume_s == 0);
        ASSERT_TRUE(blip_lines.empty());
        // a pause of more than 3 s: Bob is held away, then comes back
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(2));                                                       // (Bob's second link is the third that the door accepted)
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-31").paused; }, 3000));
        w.run(4000);
        ASSERT_TRUE(begins(first_line(app), "Bob (Red) lost the connection, waiting 0:0"));
        bob.net.set_link_maker_for_test(nullptr);                                                // the network is back
        ASSERT_TRUE(w.run_until([&]() { return first_line(app) == "Bob is back: the match goes on in 10"; }, 20000));
        std::vector<std::string> counted;
        for (int i = 0; i < 1300 && !app.net_overlay_now().lines.empty(); ++i) {
            const std::string t = first_line(app);
            if (counted.empty() || counted.back() != t) counted.push_back(t);
            w.run(10);
        }
        ASSERT_TRUE(counted.size() >= 8);                                                        // 10, 9, 8 ... (the last second or two may pass between two looks)
        ASSERT_EQ(counted.front(), std::string("Bob is back: the match goes on in 10"));
        for (const std::string& t : counted) ASSERT_TRUE(begins(t, "Bob is back: the match goes on in "));
        for (size_t i = 1; i < counted.size(); ++i) ASSERT_TRUE(counted[i] != counted[i - 1]);
        ASSERT_TRUE(app.net_overlay_now().lines.empty() && !app.net()->paused());                // then the line is gone and the match goes on
        const uint32_t ticks = w.status("RA-31").ticks;
        w.run(1500);
        ASSERT_TRUE(w.status("RA-31").ticks > ticks + 15);
    } TEST_END();

    TEST_CASE("RA3.2 After A Vote Dropped The Seat The Countdown Names Nobody: \"The match goes on in 3\", Counting Down To The First Turn") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        server::RoomSpec spec = held_spec("RA-32", 3);
        spec.vote_after_ms = 5000;
        spec.resume_countdown_ms = 3000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra32"), "RA-32", "Ann"));
        Machine& bob = w.join("Bob", "RA-32");
        Machine& cat = w.join("Cat", "RA-32");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob, &cat}); }, 12000 + kPre));
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.open; }, 12000));
        app.handle_key_down(key_event(SDLK_F3));
        ASSERT_TRUE(cat.net.vote(false));
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-32").drops_by_vote == 1; }, 5000));
        ASSERT_TRUE(w.run_until([&]() { return first_line(app) == "The match goes on in 3"; }, 5000));
        ASSERT_FALSE(app.net_overlay_now().vote.open);
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().lines.empty(); }, 6000));
        const uint32_t ticks = w.status("RA-32").ticks;
        w.run(1500);
        ASSERT_TRUE(w.status("RA-32").ticks > ticks + 15 && !app.net()->paused());
    } TEST_END();
}

void run_overlay_table_tests() {
    TEST_CASE("RA8.1 The Overlay's Priorities In A Real Match: Whatever Is Added To The State (A Desync, An Election, A Wait, Catching Up, A Slow Link, A Lagging Player, A Notice) Shows Only While Nothing Above It Does - The Way Back, The Vote And The Missing Seats, The Countdown - And Is Red Only For A Desync; With Nothing Above It Each Shows Its Own Words") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        server::RoomSpec spec = held_spec("RA-81", 3);
        spec.vote_after_ms = 5000;
        spec.resume_countdown_ms = 3000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra81"), "RA-81", "Ann"));
        Machine& bob = w.join("Bob", "RA-81");
        Machine& cat = w.join("Cat", "RA-81");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob, &cat}); }, 12000 + kPre));
        w.run(1000);
        struct Injected {
            const char* what;
            std::function<void(NetOverlayInput&)> add;
            std::string words;                               // what it says when nothing above it does
            bool alarm;
        };
        const std::vector<Injected> injected = {
            {"a desync", [](NetOverlayInput& in) { in.desynced = true; }, "Out of sync: the match has stopped.", true},
            {"an election", [](NetOverlayInput& in) { in.electing = true; }, "The host left. Choosing a new host...", false},
            {"a wait of 5 s", [](NetOverlayInput& in) { in.stalled_ms = 5000; }, "Waiting for the other players...", false},
            {"a wait for Cat", [](NetOverlayInput& in) { in.stalled_ms = 5000; in.waiting_for = "Cat"; }, "Waiting for Cat...", false},
            {"catching up", [](NetOverlayInput& in) { in.catching_up = true; }, "Catching up...", false},
            {"a slow link", [](NetOverlayInput& in) { in.self_lag_behind_ms = 9000; }, "You are lagging (9 s behind)", false},
            {"a lagging player", [](NetOverlayInput& in) { in.lag_seat = 2; in.lag_name = "Cat"; in.lag_behind_ms = 12000; }, "Cat is lagging (12 s behind)", false},
            {"a notice", [](NetOverlayInput& in) { in.notice = "Cat is the host now."; }, "Cat is the host now.", false},
        };
        // `own`: the first line of the state's own words (begins with it; "" for no line at all), `vote`: the block is open
        const auto table = [&](const char* state, const std::string& own, bool vote) -> bool {
            const NetOverlayInput real = app.net_overlay_input();
            const NetOverlayLine alone = net_overlay_line(real);
            const std::string first = alone.lines.empty() ? std::string() : alone.lines[0];
            if (own.empty() ? !first.empty() : !begins(first, own)) {
                std::cout << "\n    [" << state << "] the state's own line is \"" << first << "\"";
                return false;
            }
            if (alone.vote.open != vote) return false;
            for (const Injected& extra : injected) {
                NetOverlayInput in = real;
                extra.add(in);
                const NetOverlayLine l = net_overlay_line(in);
                const std::string got = l.lines.empty() ? std::string() : l.lines[0];
                const bool ok = own.empty() ? (got == extra.words && l.alarm == extra.alarm && !l.vote.open) : (got == first && !l.alarm && l.vote.open == vote && l.lines.size() == alone.lines.size());
                if (!ok) std::cout << "\n    [" << state << ", " << extra.what << "] the first line is \"" << got << "\"";
                if (!ok) return false;
            }
            return true;
        };
        ASSERT_TRUE(table("running", "", false));
        // Bob's link is cut for good: after a second the seat is named, after 5 s the vote is open
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return !app.net_overlay_now().lines.empty(); }, 4000));
        ASSERT_TRUE(table("a seat that is missing", "Bob (Red) lost the connection, waiting 0:", false));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.open; }, 8000));
        ASSERT_TRUE(table("a vote", "Bob (Red) lost the connection, waiting 0:", true));
        // Ann and Cat go on without Bob: the countdown names nobody
        app.handle_key_down(key_event(SDLK_F3));
        ASSERT_TRUE(cat.net.vote(false));
        ASSERT_TRUE(w.run_until([&]() { return first_line(app) == "The match goes on in 3"; }, 8000));
        ASSERT_TRUE(table("the countdown", "The match goes on in 3", false));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().lines.empty(); }, 6000));
        ASSERT_TRUE(table("running again", "", false));
        // this machine's own link is cut: the way back says so, whatever else is true
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->pause_info().reconnecting; }, 3000));
        w.run(1500);
        ASSERT_TRUE(table("the way back", "Connection lost. Reconnecting... 0:0", false));
        ASSERT_TRUE(app.net_overlay_now().lines.size() == 2 && !app.net_vote_buttons().open);
    } TEST_END();

    TEST_CASE("RA8.2 Every Text Fits The Overlay At Both Picture Shapes (The Original's 4:3 With Its 442 Px View, The 16:9 With Its 762): A Player With A Name Of 32 Of The Widest Letters Is Missing, Is Voted On, And Comes Back; Every Line, The Count And Both Buttons Lie Inside The Map View, The Buttons Do Not Overlap, And A Frame Is Drawn") {
        for (const Aspect aspect : {Aspect::Classic4x3, Aspect::Wide16x9}) {
            const Captured output;
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            server::RoomSpec spec = held_spec("RA-82", 3);
            spec.vote_after_ms = 5000;
            spec.resume_countdown_ms = 10000;
            ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
            ApplicationConfig cfg = w.config(scratch_dir(aspect == Aspect::Classic4x3 ? "ra82-classic" : "ra82-wide"), "RA-82", "Ann");
            cfg.aspect = aspect;
            cfg.aspect_given = true;
            Application& app = w.start_app(cfg);
            const std::string long_name(32, 'W');
            Machine& bob = w.join(long_name, "RA-82");
            Machine& cat = w.join("Cat", "RA-82");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&bob, &cat}); }, 12000 + kPre));
            ASSERT_EQ(app.aspect(), aspect);
            const LayoutRect view = app.layout().view();
            ASSERT_EQ(view.w, aspect == Aspect::Classic4x3 ? 442 : 762);
            const auto fit_check = [&](const char* state) -> bool {
                const NetOverlayLine l = app.net_overlay_now();
                const NetOverlayLayout where = net_overlay_layout(view, app.net_overlay_metrics_for_test(l), l.vote.open);
                const int32_t max = net_overlay_max_width(view);
                bool ok = !l.lines.empty();
                for (const std::string& t : l.lines) {
                    const int32_t width = app.renderer().get_text_width(t, FontSize::Px14);
                    if (width > max) std::cout << "\n    [" << state << ", view " << view.w << "] \"" << t << "\" is " << width << " px, the limit is " << max;
                    ok = ok && width <= max;
                }
                for (const NetOverlayBox& b : where.lines) ok = ok && b.box.x >= view.x && b.box.x + b.box.w <= view.x + view.w && b.box.y >= view.y && b.box.y + b.box.h <= view.y + view.h;
                if (l.vote.open) {
                    const bool count_fits = app.renderer().get_text_width(l.vote.count, FontSize::Px14) <= max;
                    const bool row_inside = where.keep.x >= view.x && where.go_on.x + where.go_on.w <= view.x + view.w && where.keep.x + where.keep.w < where.go_on.x && where.go_on.y + where.go_on.h <= view.y + view.h;
                    const bool count_inside = where.count.box.x >= view.x && where.count.box.x + where.count.box.w <= view.x + view.w;
                    const bool words = l.vote.go_on.compare(0, 20, "F3 Continue without ") == 0 && (l.vote.go_on.find("...") != std::string::npos) == (aspect == Aspect::Classic4x3);      // (cut at the 442 px view, whole in the 762 px one)
                    if (!(count_fits && row_inside && count_inside && words)) std::cout << "\n    [" << state << ", view " << view.w << "] count " << count_fits << " row " << row_inside << " count box " << count_inside << " words " << words << " (\"" << l.vote.go_on << "\")";
                    ok = ok && count_fits && row_inside && count_inside && words;
                }
                app.render_frame();
                return ok;
            };
            make_dead(bob);
            ASSERT_TRUE(w.server.cut_wire(1));
            ASSERT_TRUE(w.run_until([&]() { return !app.net_overlay_now().lines.empty(); }, 4000));
            ASSERT_TRUE(begins(first_line(app), "WWW") && first_line(app).find(" (Red) lost the connection, waiting 0:") != std::string::npos);
            ASSERT_EQ(first_line(app).find("...") != std::string::npos, aspect == Aspect::Classic4x3);      // the 32 letters do not fit the original's picture, and fit the 16:9 one
            ASSERT_TRUE(fit_check("missing"));
            ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.open; }, 8000));
            ASSERT_TRUE(fit_check("vote"));
            // the click on the wide-name button works where it is drawn
            click(app, app.net_vote_buttons().go_on);
            ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.go_on_pressed; }, 3000));
            ASSERT_TRUE(fit_check("vote, chosen"));
            // Bob comes back: the countdown names him, cut to fit
            bob.net.set_link_maker_for_test(nullptr);
            ASSERT_TRUE(w.run_until([&]() { return first_line(app).find(" is back: the match goes on in ") != std::string::npos; }, 20000));
            ASSERT_TRUE(begins(first_line(app), "WWW") && (first_line(app).find("...") != std::string::npos) == (aspect == Aspect::Classic4x3));
            const std::string line = first_line(app);
            ASSERT_TRUE(app.renderer().get_text_width(line, FontSize::Px14) <= net_overlay_max_width(view));
            app.render_frame();
        }
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The catch-up screen, the start of a rejoin, the dialog that gives way, leaving while held
// ---------------------------------------------------------------------------------------------------------------------------------

void run_way_back_tests() {
    TEST_CASE("RA1.3 The Way Back Of A Link That Was Cut, Its Second Half: While The Machine Is Reconnecting The Match Stays On The Screen With The Overlay; When The Server Gives It The Match The Screen Is The Loading Screen's Picture With The Bar Filled To The Percent (No Overlay, No Key, Click Or Wheel Reaches The Match; Esc Asks To Leave With The Dialog Over It And N Takes It Away); When It Has Caught Up The Match Is There With Everything As It Was: The Selection, The Camera, The Log Of The Chat (No Second \"Game started!\"), No Dialog, The Same Key In The File") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-13"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra13");
        Application& app = w.start_app(w.config_1to1(dir, "RA-13", "Ann"));
        Machine& bob = w.join("Bob", "RA-13");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(3000);
        // what a blip must leave as it is: the selection, the camera, the log of the chat
        const std::vector<uint32_t> mine = own_ants(app);
        ASSERT_TRUE(mine.size() >= 2);
        const ViewportCamera start_view = app.renderer().camera();
        app.renderer().camera().scroll_pixels(24, 16, bob.map_w, bob.map_h);
        const ViewportCamera camera = app.renderer().camera();
        ASSERT_TRUE(camera.x != start_view.x || camera.y != start_view.y);                         // (the view is where the player put it, not where a match starts)
        app.hud().select_ant(mine[0]);
        app.hud().receive_chat_message(1, "Bob", "hello Ann", false, app.sim().get_world_state());
        const std::deque<std::string> log = app.hud().get_chat_log();
        ASSERT_TRUE(chat_has(app, "Game started!") && chat_has(app, "hello Ann"));
        ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
        const net::SeatKey key = app.rejoin_store()->entries()[0].key;
        // the link is cut and the network stays down for three seconds: the match is still what the screen shows, with the words of the way back over it
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        w.run(3000);
        ASSERT_TRUE(app.net()->pause_info().reconnecting);
        ASSERT_FALSE(app.catch_up_screen_active());
        ASSERT_TRUE(begins(first_line(app), "Connection lost. Reconnecting..."));
        std::string why;
        ASSERT_FALSE(catch_up_picture(grab(app, dir / "ra13-match.bmp"), 0, why));                // (the check of the picture can tell the match from the catch-up screen)
        // the network is back: every step of the way is looked at
        app.net()->set_link_maker_for_test(nullptr);
        std::vector<Look> looks;
        bool poked = false;
        Reach blocked;
        bool picture_ok = false;
        std::string problem;
        for (uint32_t elapsed = 0; elapsed < 30000; elapsed += 10) {
            w.run(10);
            const Look now = look_at(app);
            looks.push_back(now);
            if (!poked && now.catching) {
                poked = true;
                const uint32_t ant = own_ant_in_view(app, mine[0]);
                ASSERT_TRUE(ant != 0);
                blocked = poke(app, ant);
                const auto pointer_at = [&app](int32_t x, int32_t y) {                           // (the game's own cursor is on the picture: the pictures below are compared with the pointer in one place)
                    SDL_MouseMotionEvent motion{};
                    motion.type = SDL_MOUSEMOTION;
                    motion.x = x;
                    motion.y = y;
                    app.handle_mouse_motion(motion);
                };
                pointer_at(560, 380);
                const Picture plain = grab(app, dir / "ra13-a.bmp");
                picture_ok = catch_up_picture(plain, app.catch_up_percent(), problem);
                app.handle_key_down(key_event(SDLK_ESCAPE));                                      // Esc asks: the dialog is drawn over the loading screen
                ASSERT_TRUE(app.hud().is_quit_dialog_open());
                ASSERT_TRUE(app.catch_up_screen_active());
                const int64_t over = plain.differing(grab(app, dir / "ra13-b.bmp"));
                ASSERT_TRUE(over > 2000);
                const LayoutPoint m = app.layout().modal_offset();                                // the mouse works on the dialog too: it lights No, and a click on No, where it is drawn
                const UIButton no = app.hud().quit_no_button();
                pointer_at(no.x + m.x + 10, no.y + m.y + 10);
                ASSERT_TRUE(app.hud().quit_no_button().is_active);                                 // (the button shows its hover picture)
                click(app, LayoutRect{no.x + m.x, no.y + m.y, no.w, no.h});
                ASSERT_FALSE(app.hud().is_quit_dialog_open());
                ASSERT_TRUE(app.catch_up_screen_active());
                pointer_at(560, 380);
                ASSERT_EQ(plain.differing(grab(app, dir / "ra13-c.bmp")), int64_t{0});            // (the screen is as it was)
                app.handle_key_down(key_event(SDLK_ESCAPE));
                ASSERT_TRUE(app.hud().is_quit_dialog_open());
                app.handle_key_down(key_event(SDLK_n));                                           // N is No as well
                ASSERT_FALSE(app.hud().is_quit_dialog_open());
            }
            if (poked && !now.catching && !app.net()->paused()) break;
        }
        ASSERT_TRUE(poked);
        ASSERT_FALSE(blocked.options || blocked.click || blocked.wheel || blocked.text);          // nothing of a key, a click, typed text or the wheel reached the match
        if (!picture_ok) std::cout << "\n    [the catch-up screen] " << problem;
        ASSERT_TRUE(picture_ok);
        int32_t last_percent = -1;
        for (const Look& l : looks) {
            ASSERT_FALSE(l.dialog);
            ASSERT_EQ(l.state, AppState::Playing);
            if (!l.catching) continue;
            ASSERT_TRUE(l.percent >= last_percent && l.percent <= 100);
            last_percent = l.percent;
            ASSERT_FALSE(l.overlay);                                                              // (the screen is the loading screen's: no overlay on it)
        }
        ASSERT_FALSE(app.net()->paused());
        ASSERT_FALSE(app.catch_up_screen_active());
        // the match is as it was
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        const ViewportCamera& after = app.renderer().camera();
        ASSERT_TRUE(after.x == camera.x && after.y == camera.y && after.world_x == camera.world_x && after.world_y == camera.world_y && after.zoom == camera.zoom);
        ASSERT_TRUE(app.hud().get_chat_log() == log);
        ASSERT_FALSE(app.hud().is_match_start_modal_active() || app.hud().is_quit_dialog_open() || app.hud().is_modal_open());
        ASSERT_TRUE(fs::exists(dir / "rejoin.txt") && app.rejoin_store()->entries().size() == 1 && net::key_matches(app.rejoin_store()->entries()[0].key, key));
        ASSERT_TRUE(w.status("RA-13").rejoins == 1);
        w.run(500);
        ASSERT_TRUE(app.net_overlay_now().lines.empty());
        // the match takes keys, clicks and the wheel again (what was refused on the catch-up screen was the screen's doing)
        const uint32_t ant = own_ant_in_view(app, mine[0]);
        ASSERT_TRUE(ant != 0);
        const Reach reached = poke(app, ant);
        if (!(reached.wheel && reached.click && reached.text && reached.options)) std::cout << "\n    [after the catch-up] the wheel " << reached.wheel << ", the click " << reached.click << ", the text " << reached.text << ", the key " << reached.options;
        ASSERT_TRUE(reached.wheel && reached.click && reached.text && reached.options);
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
    } TEST_END();

    TEST_CASE("RA1.4 What The Engine Queued While The Match Was Run Without A Picture Is Neither Heard Nor Said When The Machine Is Back: A Sound And A Line Of The Status That Wait In The Engine's Queues When The Catch-Up Ends Are Dropped At The First Tick After It (A Replay Of A Whole Match Would Play Its Sounds At Once); The Same Things Outside A Catch-Up Are Played And Said At The Next Tick") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-14"), w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra14"), "RA-14", "Ann"));
        Machine& bob = w.join("Bob", "RA-14");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(3000);
        const uint8_t seat = app.local_player_id();
        const std::string words = sim::strings::text(sim::strings::kNeed200Points);
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        w.run(3000);                                                                              // (the runner has run what it held: no tick comes until the match is back)
        ASSERT_TRUE(app.net()->pause_info().reconnecting);
        // the engine queues a sound and a line of the status, as the turns of a replay do: an order of the player's own that is refused (it changes nothing of the match)
        const uint64_t before = app.sim().state_hash().total;
        ASSERT_TRUE(app.sim().try_hatch(seat, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::NotEnoughPoints);
        ASSERT_TRUE(app.sim().state_hash().total == before);
        ASSERT_TRUE(app.sim().has_targeted_audio_event(seat, sim::SoundID::AntStop) && app.sim().has_news_event(seat, sim::strings::kNeed200Points));
        app.audio_mixer().stop_all();
        app.hud().clear_status();
        ASSERT_EQ(app.audio_mixer().active_channel_count(), size_t{0});
        app.net()->set_link_maker_for_test(nullptr);
        bool caught = false;
        bool first_live = false;
        uint64_t last_catching_tick = 0;
        for (uint32_t elapsed = 0; elapsed < 30000 && !first_live; elapsed += 10) {
            w.run(10);
            const Look now = look_at(app);
            ASSERT_TRUE(now.status != words);                                                     // never said
            if (now.catching) {
                caught = true;
                last_catching_tick = now.tick;
            } else if (caught && now.tick > last_catching_tick) {                                  // the first tick after the catch-up has run
                first_live = true;
                ASSERT_EQ(now.channels, size_t{0});                                               // and played nothing of what the engine queued
            }
        }
        ASSERT_TRUE(caught && first_live);
        // outside a catch-up the same two things are played and said at the next tick
        w.run(500);
        app.audio_mixer().stop_all();
        app.hud().clear_status();
        ASSERT_TRUE(app.sim().try_hatch(seat, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::NotEnoughPoints);
        ASSERT_TRUE(w.run_until([&]() { return app.audio_mixer().active_channel_count() >= 1 && app.hud().status_line().text() == words; }, 1000));
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("RA5.1 The \"Get ready to play!\" Dialog Gives Way To A Pause: A Player Whose Link Is Cut Two Seconds After The Match Began (Three In The Room) Holds The Match Before Its First Turn; The Dialog Of Another Machine Closes At Once (It Would Take The Keys And Hide The Overlay), The Seat Is Named, The Vote Block Opens And F3 Votes, The Dialog Does Not Come Back, And The Match Goes On After The Vote") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        server::RoomSpec spec = held_spec("RA-51", 3);
        spec.vote_after_ms = 5000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra51"), "RA-51", "Ann"));
        Machine& bob = w.join("Bob", "RA-51");
        Machine& cat = w.join("Cat", "RA-51");
        ASSERT_TRUE(w.run_until([&]() { return app.state() == AppState::Playing; }, 12000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0);        // the match began: the dialog is up
        w.run(2000);
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0 && !app.net()->paused());
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->paused(); }, 3000));
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                    // the dialog gave way in the moment of the pause
        ASSERT_EQ(app.sim().current_tick(), uint64_t{0});                                         // (the match has not begun to run: its first turn waits for the seat)
        ASSERT_TRUE(w.run_until([&]() { return !app.net_overlay_now().lines.empty(); }, 3000));
        ASSERT_TRUE(begins(first_line(app), "Bob (Red) lost the connection, waiting 0:0"));
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.open; }, 8000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "0 of 2 voted to continue");
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        app.handle_key_down(key_event(SDLK_F3));                                                  // the keys reach the vote
        ASSERT_TRUE(w.run_until([&]() { return app.net_overlay_now().vote.go_on_pressed; }, 3000));
        ASSERT_TRUE(app.net_overlay_now().vote.count == "1 of 2 voted to continue" && w.status("RA-51").votes_continue == 1);
        ASSERT_TRUE(cat.net.vote(false));
        ASSERT_TRUE(w.run_until([&]() { return w.status("RA-51").drops_by_vote == 1; }, 5000));
        ASSERT_TRUE(w.run_until([&]() { return !app.net()->paused() && app.net_overlay_now().lines.empty(); }, 5000));
        ASSERT_TRUE(w.run_until([&]() { return app.sim().current_tick() >= 20; }, 8000));         // the first turn came: the match runs
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                    // and the dialog did not come back
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("RA5.2 The Same For This Machine's Own Link: Cut In The Seconds Of The Dialog, The Dialog Gives Way To The Words Of The Way Back, And When The Machine Is Back (The Catch-Up Screen In Between) The Match Runs Without A Dialog") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-52"), w.server_now()).ok);
        Application& app = w.start_app(w.config(scratch_dir("ra52"), "RA-52", "Ann"));
        Machine& bob = w.join("Bob", "RA-52");
        ASSERT_TRUE(w.run_until([&]() { return app.state() == AppState::Playing; }, 12000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active());
        w.run(2000);
        ASSERT_TRUE(app.hud().is_match_start_modal_active());
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->pause_info().reconnecting; }, 3000));
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        w.run(1500);
        ASSERT_TRUE(begins(first_line(app), "Connection lost. Reconnecting..."));
        app.net()->set_link_maker_for_test(nullptr);
        bool caught = false;
        ASSERT_TRUE(w.run_until([&]() {
            const Look now = look_at(app);
            caught = caught || now.catching;
            return !app.net()->paused() && !now.catching;
        }, 30000));
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000));
        w.run(2000);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        ASSERT_TRUE(app.sim().current_tick() > 20 && !app.net()->desynced());
    } TEST_END();

    TEST_CASE("RA6.1 Quit While The Match Is Held (Two In The Room, The Other One Is Missing): The Quit Dialog's Yes Leaves For Good At Once - A Quit Command Would Be Discarded, No Turn Is Sealed - The Key Is Let Go Of, No Results Open, And When The Other One Is Back The Room Has Dropped The Seat (Bob Is Told)") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-61"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra61");
        Application& app = w.start_app(w.config(dir, "RA-61", "Ann"));
        Machine& bob = w.join("Bob", "RA-61");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(3000);
        const uint8_t seat = app.local_player_id();
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->paused() && !app.net_overlay_now().lines.empty(); }, 4000));
        ASSERT_TRUE(begins(first_line(app), "Bob (Red) lost the connection, waiting 0:"));
        ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
        const net::SeatKey key = app.rejoin_store()->entries()[0].key;
        quit_by_dialog(app);                                                                      // Ctrl+Q, Y
        ASSERT_FALSE(app.is_running());                                                           // the player left (a game that was joined by arguments ends)
        ASSERT_EQ(app.net()->phase(), NetGame::Phase::Off);
        ASSERT_FALSE(app.scorecard().is_open());                                                  // (no Quit command: the match did not end)
        ASSERT_TRUE(app.rejoin_store()->entries().empty());
        ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
        // Bob is back: the match goes on and the first turn after the pause drops the seat that left
        bob.net.set_link_maker_for_test(nullptr);
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RA-61").paused; }, 25000));
        w.run(1000);
        bool left = false;
        for (const NetGame::Event& e : bob.events) left = left || (e.type == NetGame::Event::Type::PlayerLeft && e.seat == seat);
        ASSERT_TRUE(left);
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
    } TEST_END();

    TEST_CASE("RA6.3 The Page Leaves A Match On Purpose (ants_leave_match: The Menu Button And Link, The Picture Selector's Yes; The Review's M2): Leave Is Sent, The Server Drops The Seat At Once (The Others Are Not Held Up: No Seat Is Missing, The Room Is Not Paused), The Key Is Let Go Of; A Closed Tab Is Not That: Its Seat Is Held And Its Key Kept") {
        const Captured output;
        {   // three in the room: the one who leaves is dropped, the other two play on
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-63", 3), w.server_now()).ok);
            const fs::path dir = scratch_dir("ra63");
            Application& app = w.start_app(w.config(dir, "RA-63", "Ann"));
            Machine& bob = w.join("Bob", "RA-63");
            Machine& cat = w.join("Cat", "RA-63");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&bob, &cat}); }, 14000 + kPre));
            w.run(3000);
            const uint8_t seat = app.local_player_id();
            ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
            const net::SeatKey key = app.rejoin_store()->entries()[0].key;
            app.leave_network_match();
            ASSERT_EQ(app.net()->phase(), NetGame::Phase::Off);
            ASSERT_TRUE(app.rejoin_store()->entries().empty());                                  // the key is let go of ...
            ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
            w.run(3000);
            const server::RoomStatus st = w.status("RA-63");
            ASSERT_TRUE(st.state == server::RoomState::Running && !st.paused && st.absent.empty());      // ... and the seat is dropped at once: nobody waits for it
            for (Machine* other : {&bob, &cat}) {
                bool left = false;
                for (const NetGame::Event& e : other->events) left = left || (e.type == NetGame::Event::Type::PlayerLeft && e.seat == seat);
                ASSERT_TRUE(left);
                ASSERT_TRUE(other->net.pause_info().missing.empty() && !other->net.pause_info().vote_open && !other->net.paused());
            }
            app.leave_network_match();                                                           // (again: nothing happens)
            ASSERT_TRUE(app.rejoin_store()->entries().empty());
            ASSERT_TRUE(output.text().find(rejoin_key_hex(key)) == std::string::npos);
        }
        {   // a closed tab says no goodbye: the seat is held, the key is kept
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-63B"), w.server_now()).ok);
            const fs::path dir = scratch_dir("ra63b");
            Application& app = w.start_app(w.config(dir, "RA-63B", "Ann"));
            Machine& bob = w.join("Bob", "RA-63B");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
            w.run(3000);
            ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
            w.crash_app(dir);
            ASSERT_TRUE(w.run_until([&]() { return w.status("RA-63B").paused; }, 4000));
            ASSERT_TRUE(w.status("RA-63B").absent.size() == 1);
            FileRejoinStore file((dir / "rejoin.txt").string());
            ASSERT_EQ(file.entries().size(), size_t{1});
        }
    } TEST_END();

    TEST_CASE("RA6.4 Quit While The Catch-Up Screen Is Up (L3 Of The Review): A Match That Starts Again After A BadRequest Has No Session That Plays In Its Lobby Phases (Connecting, Loading: Nothing Holds It, `paused()` Is False), So A Quit Command Would Go Nowhere; The Quit Dialog's Yes Leaves For Good There As It Did In The Catch-Up Of A Way Back (The Game Ends, The Key Is Let Go Of, No Results Open)") {
        const Captured output;
        for (const bool lobby_phases : {true, false}) {
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-64"), w.server_now()).ok);
            const fs::path dir = scratch_dir(lobby_phases ? "ra64a" : "ra64b");
            Application& app = w.start_app(w.config(dir, "RA-64", "Ann"));
            ASSERT_TRUE(w.run_until([&]() { return app.net()->phase() == NetGame::Phase::Room && app.net()->my_seat() == 0; }, 5000));
            Machine& bob = w.join("Bob", "RA-64");
            ASSERT_TRUE(w.run_until([&]() { return app.state() == AppState::Playing; }, 12000));
            ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
            w.run(lobby_phases ? 3000 : 20000);                                                   // (a longer match has more to catch up on)
            ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
            const net::SeatKey key = app.rejoin_store()->entries()[0].key;
            w.server.script_reason = net::RejectReason::BadRequest;                               // (the refusal that makes the machine start the match again from nothing)
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_wire(w.app_wire));
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 8000));
            w.server.door = Server::Door::Open;
            bool there = false;
            for (uint32_t elapsed = 0; elapsed < 30000 && !there; elapsed += 10) {
                w.run(10);
                const NetGame::Phase phase = app.net()->phase();
                there = lobby_phases ? (phase == NetGame::Phase::Connecting || phase == NetGame::Phase::Loading) : (phase == NetGame::Phase::Playing && app.net()->pause_info().catching_up);
            }
            ASSERT_TRUE(there && app.catch_up_screen_active());
            ASSERT_EQ(app.net()->paused(), !lobby_phases);                                        // (the catch-up of a way back holds the match, as it did: the quit already left; the lobby phases of a new start do not)
            ASSERT_EQ(app.sim().other_sides(app.local_player_id()), 1);                           // (the case that submits a Quit command)
            quit_by_dialog(app);
            ASSERT_FALSE(app.is_running());                                                       // the player left (a game that was joined by arguments ends)
            ASSERT_EQ(app.net()->phase(), NetGame::Phase::Off);
            ASSERT_FALSE(app.scorecard().is_open());
            ASSERT_TRUE(app.rejoin_store()->entries().empty());
            ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
            ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("RA10.1 The Page Is Told The Seat That The Room Gave The Machine (L2 Of The Review): In The Waiting Room Once The Room Has Said Where It Sits, Before Any Start, And Not Again At The Start Or After A Way Back; A Game That Is Started Again Without A Seat In Its Arguments (A Reloaded Page) Is Told The Seat Of Its Key When It Is Given Its Match; Nothing Is Told For A Game That Has No Room") {
        const Captured output;
        std::vector<unsigned> told;
        World w;
        w.seat_hook = [&](uint8_t seat) { told.push_back(seat); };
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-101"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra101");
        Application& app = w.start_app(w.config(dir, "RA-101", "Ann"));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->phase() == NetGame::Phase::Room && app.net()->my_seat() == 0; }, 5000));
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));                                         // (in the waiting room: the Start is not here yet)
        Machine& bob = w.join("Bob", "RA-101");
        ASSERT_TRUE(w.run_until([&]() { return app.state() == AppState::Playing; }, 12000));
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(3000);
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));                                         // (not again at the Start)
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));                                               // a way back: the same seat, not told again
        ASSERT_TRUE(w.run_until([&]() { return app.net()->pause_info().reconnecting; }, 4000));
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RA-101").paused && !app.net()->pause_info().reconnecting && !app.net()->pause_info().catching_up; }, 25000));
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));
        // the page is reloaded: a new game with no seat in its arguments takes the seat of its key and is told it when it is given its match
        w.app_wire = w.server.accepted - 1;                                                       // (the game's link is the newest that the door took: the way back made a new one)
        w.crash_app(dir);
        told.clear();
        Application& again = w.start_app(w.config(dir, "RA-101", "Ann"));
        ASSERT_TRUE(w.run_until([&]() { return again.net() != nullptr && again.net()->phase() == NetGame::Phase::Playing && !w.status("RA-101").paused; }, 25000));
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));
        w.run(2000);
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));
        // leaving ends the session: nothing more is told
        again.leave_network_match();
        w.run(500);
        ASSERT_TRUE(told == std::vector<unsigned>({0u}));
        // a second session of the same application (another room): its seat is told again, also when it is the same one
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-101B"), w.server_now()).ok);
        ASSERT_TRUE(again.net()->join("127.0.0.1", w.server.port(), "Ann", 255, "RA-101B"));
        ASSERT_TRUE(w.run_until([&]() { return told.size() == 2; }, 5000));
        ASSERT_TRUE(told == std::vector<unsigned>({0u, 0u}));
        again.net()->leave();
        // a game of one machine tells nothing at all
        {
            std::vector<unsigned> local;
            World v;
            v.seat_hook = [&](uint8_t seat) { local.push_back(seat); };
            const fs::path dir2 = scratch_dir("ra101b");
            Application& menu = v.start_menu_app(dir2);
            menu.set_on_seat_known([&](uint8_t seat) { local.push_back(seat); });
            v.run(1000);
            ASSERT_TRUE(local.empty());
        }
    } TEST_END();

    TEST_CASE("RA6.2 Esc: In A Match That Runs, And While Another Player's Seat Is Missing, It Is The Original's (Everything Is Deselected, No Dialog); On The Way Back, With This Machine's Link Lost, It Opens The Quit Dialog (A Held Key Does Not): N Or Esc Closes It, Y Leaves The Match For Good And The Key Is Let Go Of; The Selection Is Not Touched By The Esc That Asks") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-62"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra62");
        Application& app = w.start_app(w.config(dir, "RA-62", "Ann"));
        Machine& bob = w.join("Bob", "RA-62");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(3000);
        const std::vector<uint32_t> mine = own_ants(app);
        ASSERT_FALSE(mine.empty());
        // a match that runs
        app.hud().select_ant(mine[0]);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        app.handle_key_down(key_event(SDLK_ESCAPE));
        ASSERT_TRUE(app.hud().get_selected_ant_id() == 0 && !app.hud().is_quit_dialog_open());
        // another player's seat is missing: this machine's link is fine, and Esc is still the original's
        make_dead(bob);
        ASSERT_TRUE(w.server.cut_wire(1));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->paused(); }, 3000));
        ASSERT_FALSE(app.net()->pause_info().reconnecting);
        app.hud().select_ant(mine[0]);
        app.handle_key_down(key_event(SDLK_ESCAPE));
        ASSERT_TRUE(app.hud().get_selected_ant_id() == 0 && !app.hud().is_quit_dialog_open());
        // this machine's link is lost as well: the way back
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->pause_info().reconnecting; }, 3000));
        app.hud().select_ant(mine[0]);
        app.handle_key_down(key_event(SDLK_ESCAPE, true));                                       // a repeat of a held key (the press came before: it is not this test's) asks nothing and deselects nothing
        ASSERT_TRUE(!app.hud().is_quit_dialog_open() && app.hud().get_selected_ant_id() == mine[0]);
        app.handle_key_down(key_event(SDLK_ESCAPE));
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);                                      // (the Esc that asks deselects nothing)
        app.handle_key_down(key_event(SDLK_ESCAPE, true));                                        // the key is held: its repeats do not close the dialog that it opened
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        app.handle_key_down(key_event(SDLK_n));                                                   // No
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        app.handle_key_down(key_event(SDLK_ESCAPE));
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        app.handle_key_down(key_event(SDLK_ESCAPE));                                              // the dialog's own Esc closes it
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_TRUE(app.is_running() && app.net()->pause_info().reconnecting && app.rejoin_store()->entries().size() == 1);
        app.handle_key_down(key_event(SDLK_ESCAPE));
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        app.handle_key_down(key_event(SDLK_y));                                                   // Yes: the match is left for good
        ASSERT_FALSE(app.is_running());
        ASSERT_EQ(app.net()->phase(), NetGame::Phase::Off);
        ASSERT_TRUE(app.rejoin_store()->entries().empty());
        ASSERT_FALSE(fs::exists(dir / "rejoin.txt"));
    } TEST_END();

    TEST_CASE("RA9.1 The Match That Begins A Second Time In One Session (A Server That Lost The Last Second Of Its Record Answers BadRequest, The NetGame Starts The Match From Nothing With Its Key): The Screen Is The Catch-Up Screen From The Refusal On (Never The Match That Was Reset Behind It), No Dialog And No Start Sound Come, The HUD Starts Clean, The Key Is The Same In The File, And The Machine Ends In The Same State As Bob") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-9"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra9");
        Application& app = w.start_app(w.config(dir, "RA-9", "Ann"));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->phase() == NetGame::Phase::Room && app.net()->my_seat() == 0; }, 5000));
        ASSERT_TRUE(app.net()->chat("see you in the match"));                                     // (said in the waiting room: the log of the first begin has it)
        Machine& bob = w.join("Bob", "RA-9");
        ASSERT_TRUE(w.run_until([&]() { return app.state() == AppState::Playing; }, 12000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.audio_mixer().active_channel_count() >= 1);          // (the first begin: the dialog and the start sound)
        ASSERT_TRUE(chat_has(app, "see you in the match"));
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
        w.run(5000);
        const std::vector<uint32_t> mine = own_ants(app);
        ASSERT_FALSE(mine.empty());
        app.hud().select_ant(mine[0]);
        app.hud().receive_chat_message(1, "Bob", "hello Ann", false, app.sim().get_world_state());
        ASSERT_EQ(app.rejoin_store()->entries().size(), size_t{1});
        const net::SeatKey key = app.rejoin_store()->entries()[0].key;
        const uint32_t sealed = w.status("RA-9").turns;
        app.audio_mixer().stop_all();
        // the refusal that makes the machine start from nothing: its Hello says it has turns, the server answers BadRequest, the next link goes to the real server
        w.server.script_reason = net::RejectReason::BadRequest;
        w.server.door = Server::Door::Scripted;
        ASSERT_TRUE(w.server.cut_wire(w.app_wire));
        ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 8000));
        w.server.door = Server::Door::Open;
        std::vector<Look> looks;
        bool reloaded = false;
        bool slow_begin = false;
        uint64_t last_catching_tick = 0;
        for (uint32_t elapsed = 0; elapsed < 60000; elapsed += 10) {
            w.run(10);
            const Look now = look_at(app);
            looks.push_back(now);
            reloaded = reloaded || now.phase == NetGame::Phase::Connecting || now.phase == NetGame::Phase::Loading;
            if (now.catching) last_catching_tick = now.tick;
            if (reloaded && now.phase == NetGame::Phase::Playing && !now.catching && !w.status("RA-9").paused && now.tick > last_catching_tick) break;
            if (!slow_begin && now.phase == NetGame::Phase::Loading) {            // the map is loaded and the Begin is a round trip away (here: half a second, as a Mac under load or a far server make it)
                slow_begin = true;
                const uint64_t tick_at_load = now.tick;
                w.run_app_alone(500);
                looks.push_back(look_at(app));
                ASSERT_EQ(app.sim().current_tick(), tick_at_load);               // no tick of its own meanwhile: the match is the runner's (the first begin's screen was a local game's, ticking at 20 a second, and a tick here is a state that the replay does not reach: the server refused the machine)
            }
        }
        ASSERT_TRUE(reloaded && slow_begin);
        for (const Look& l : looks) {
            ASSERT_EQ(l.state, AppState::Playing);
            ASSERT_FALSE(l.dialog);                                                               // no dialog at any moment
            if (l.phase != NetGame::Phase::Playing) ASSERT_TRUE(l.catching);                      // the catch-up screen all the way: the match that was reset behind it is not shown
        }
        ASSERT_EQ(looks.back().channels, size_t{0});                                              // no start sound (the first live tick played nothing of the replay either)
        ASSERT_EQ(app.net()->phase(), NetGame::Phase::Playing);
        ASSERT_TRUE(w.status("RA-9").rejoins == 1 && w.status("RA-9").absent.empty());
        ASSERT_TRUE(app.net()->turns_executed() >= sealed);
        // the HUD starts clean: the selection and the log of the old session are gone (the waiting room's talk is not said again), and there is no start news
        ASSERT_TRUE(app.hud().get_selected_ant_id() == 0 && app.hud().get_chat_log().empty());
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        ASSERT_TRUE(app.rejoin_store()->entries().size() == 1 && net::key_matches(app.rejoin_store()->entries()[0].key, key));
        w.run(3000);
        ASSERT_TRUE(app.net()->turns_executed() > sealed + 40);
        bool agree = false;
        for (int i = 0; i < 400 && !agree; ++i) {
            if (app.sim().current_tick() == bob.sim.current_tick()) agree = app.sim().state_hash() == bob.sim.state_hash();
            if (!agree) w.run(10);
        }
        ASSERT_TRUE(agree);
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        ASSERT_FALSE(output.text().find(rejoin_key_hex(key)) != std::string::npos);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The screenshots (test_rejoin_app --shots DIR [--wide]): what the screens look like; nothing is compared
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

void shot(Application& app, const std::string& path) {
    SDL_WindowEvent we;
    std::memset(&we, 0, sizeof(we));
    we.event = SDL_WINDOWEVENT_LEAVE;
    app.handle_window_event(we);                                    // (the pointer is outside: the game's cursor is not drawn on the picture)
    app.renderer().request_screenshot(path);
    app.render_frame();
}

ApplicationConfig shots_config(World& w, const fs::path& dir, const std::string& room, const std::string& name, bool wide) {
    ApplicationConfig cfg = w.config(dir, room, name);
    if (wide) {
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
    }
    return cfg;
}

int make_shots(const std::string& out, bool wide) {
    std::error_code ec;
    fs::create_directories(out, ec);
    {   // the other player is missing: the seat, the vote, the countdown
        World w;
        if (!w.server.start(w.now)) return 1;
        server::RoomSpec spec = held_spec("SHOT-1", 3);
        spec.vote_after_ms = 5000;
        spec.resume_countdown_ms = 10000;
        w.server.mgr->create_room(spec, w.server_now());
        Application& app = w.start_app(shots_config(w, scratch_dir("shots-a"), "SHOT-1", "Ann", wide));
        Machine& bob = w.join("Bob", "SHOT-1");
        Machine& cat = w.join("Cat", "SHOT-1");
        if (!w.run_until([&]() { return w.running({&bob, &cat}); }, 12000 + kPre)) return 1;
        w.run(2000);
        make_dead(bob);
        w.server.cut_wire(1);
        w.run(2500);
        shot(app, out + "/01_a_seat_is_missing.bmp");
        if (!w.run_until([&]() { return app.net_overlay_now().vote.open; }, 8000)) return 1;
        shot(app, out + "/02_the_vote.bmp");
        app.handle_key_down(key_event(SDLK_F3));
        w.run(300);
        shot(app, out + "/03_the_vote_f3.bmp");
        app.handle_key_down(key_event(SDLK_F2));
        w.run(300);
        shot(app, out + "/04_the_vote_f2.bmp");
        bob.net.set_link_maker_for_test(nullptr);
        if (!w.run_until([&]() { return first_line(app).find(" is back: the match goes on in ") != std::string::npos; }, 20000)) return 1;
        shot(app, out + "/05_the_countdown.bmp");
    }
    {   // this machine's own link is cut
        World w;
        if (!w.server.start(w.now)) return 1;
        w.server.mgr->create_room(held_spec("SHOT-2"), w.server_now());
        Application& app = w.start_app(shots_config(w, scratch_dir("shots-b"), "SHOT-2", "Ann", wide));
        Machine& bob = w.join("Bob", "SHOT-2");
        if (!w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre)) return 1;
        w.run(2000);
        app.net()->set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(); });
        w.server.cut_wire(w.app_wire);
        w.run(3200);
        shot(app, out + "/06_the_way_back.bmp");
    }
    {   // a game that is started again is given the match from the server's log: the catch-up screen, and Esc over it
        World w;
        if (!w.server.start(w.now)) return 1;
        w.server.mgr->create_room(held_spec("SHOT-3"), w.server_now());
        const fs::path dir = scratch_dir("shots-c");
        const ApplicationConfig cfg = shots_config(w, dir, "SHOT-3", "Ann", wide);
        Application& first = w.start_app(cfg);
        Machine& bob = w.join("Bob", "SHOT-3");
        (void)first;
        if (!w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre)) return 1;
        w.run(40000);
        w.crash_app(dir);
        if (!w.run_until([&]() { return w.status("SHOT-3").paused; }, 3000)) return 1;
        Application& second = w.start_app(cfg);
        bool shown = false;
        for (uint32_t elapsed = 0; elapsed < 30000 && !shown; elapsed += 10) {
            w.run(10);
            if (second.catch_up_screen_active() && second.catch_up_percent() >= 20 && second.catch_up_percent() < 100) {
                shot(second, out + "/07_catching_up.bmp");
                second.handle_key_down(key_event(SDLK_ESCAPE));
                shot(second, out + "/08_catching_up_esc.bmp");
                second.handle_key_down(key_event(SDLK_n));
                shown = true;
            }
        }
        if (!shown) return 1;
    }
    std::cout << "screenshots written to " << out << "\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shots") == 0) {
            bool wide = false;
            for (int k = 1; k < argc; ++k) wide = wide || std::strcmp(argv[k], "--wide") == 0;
            return make_shots(argv[i + 1], wide);
        }
    }
    net::NetGame::default_prediction_budget_ns() = UINT64_MAX;
    std::cout << "\n=======================================================\n [THE WAY BACK IN THE APPLICATION] the screens, the keys' storage, the start menu's Rejoin\n=======================================================\n";
    run_key_tests();
    run_use_tests();
    run_menu_tests();
    run_screen_tests();
    run_overlay_table_tests();
    run_way_back_tests();
    std::cout << "\n" << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "SOME TESTS FAILED\n";
    return 1;
}
