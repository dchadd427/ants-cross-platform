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

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    net::NetGame::default_prediction_budget_ns() = UINT64_MAX;
    std::cout << "\n=======================================================\n [THE WAY BACK IN THE APPLICATION] the screens, the keys' storage, the start menu's Rejoin\n=======================================================\n";
    run_key_tests();
    run_use_tests();
    std::cout << "\n" << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "SOME TESTS FAILED\n";
    return 1;
}
