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
#include "ants_app/rejoin_store.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/command.hpp"
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
    void pump(uint32_t world_now) {
        if (!listener_) return;
        while (auto link = listener_->accept()) {
            ++accepted;
            std::shared_ptr<net::TcpConnection> shared(std::move(link));
            wires_.push_back(shared);
            mgr->add_connection(std::make_unique<Wire>(shared), "127.0.0.1", now(world_now));
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
    // The application joins (its link is the newest that the door accepted: a test can cut it)
    Application& start_app(const ApplicationConfig& cfg) {
        app = std::make_unique<Application>();
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

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------------------------------------------------------------

void run_key_tests() {
    TEST_CASE("RA1.1 The Application Keeps The Key Of Its Seat In A File Beside Its Settings As Soon As The Room Gives It (Mode 0600, The Server, The Room And The Seat In Its Line); Bob's Key Is Not In It; Leaving Lets Go Of It, And Nothing That The Program Printed Holds A Key") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-1"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra11");
        Application& app = w.start_app(w.config(dir, "RA-1", "Ann"));
        ASSERT_TRUE(w.run_until([&]() { return app.net()->phase() == NetGame::Phase::Room && app.net()->my_seat() == 0; }, 5000));
        ASSERT_TRUE(w.run_until([&]() { return app.rejoin_store() != nullptr && app.rejoin_store()->entries().size() == 1; }, 2000));        // the Welcome gave the key
        Machine& bob = w.join("Bob", "RA-1");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&bob}); }, 12000 + kPre));
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
    TEST_CASE("RA4.1 A Game That Is Started Again With The Join Arguments Of Its Room (--join ADDR --room CODE, No Seat) Finds The Key In Its File And Takes Its Seat In The Running Match: The Room Counts A Rejoin, The Seat Is The Same, The Game Is Given The Match From The Server's Log, And It Ends In The Same State As Bob; The File Has The Same Key Again") {
        const Captured output;
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RA-4"), w.server_now()).ok);
        const fs::path dir = scratch_dir("ra41");
        const ApplicationConfig cfg = w.config(dir, "RA-4", "Ann");
        Application& first = w.start_app(cfg);
        Machine& bob = w.join("Bob", "RA-4");
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
        // the game is started again, with the arguments of its room and no key
        Application& second = w.start_app(cfg);
        ASSERT_TRUE(w.run_until([&]() { return second.state() == AppState::Playing && !w.status("RA-4").paused; }, 30000));
        const server::RoomStatus s = w.status("RA-4");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.names[seat] == "Ann");
        ASSERT_EQ(second.net()->my_seat(), seat);
        ASSERT_EQ(second.local_player_id(), seat);
        ASSERT_TRUE(second.net()->turns_executed() >= sealed);                                   // the whole match was given to it
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
            {"an entry of a day and a minute", {line(me, "ROOM-A", 1, 11, 24 * 60 + 1)}, "ROOM-A", 255, 0, -1},
            {"an entry of a day less a minute", {line(me, "ROOM-A", 1, 11, 24 * 60 - 1)}, "ROOM-A", 255, 11, 1},
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
        // not offered: a day and a minute old, a URL
        {
            const fs::path dir = scratch_dir("ra72a");
            write_file(dir / "rejoin.txt", key_line(server, "OLD-1", 0, key_of(1), now - 24 * 3600 * 1000 - 60 * 1000) + key_line("wss://play.example.org/game", "WEB-1", 1, key_of(2), now));
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

namespace {

SDL_KeyboardEvent key_event(SDL_Keycode sym, bool repeat = false) {
    SDL_KeyboardEvent ke{};
    ke.type = SDL_KEYDOWN;
    ke.keysym.sym = sym;
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

}  // namespace

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
    std::cout << "\n" << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "SOME TESTS FAILED\n";
    return 1;
}
