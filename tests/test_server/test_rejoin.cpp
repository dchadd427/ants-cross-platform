// Tests of the way back of the game's own NetGame (docs/NETWORK_PORT.md "Reconnect"): real NetGames over loopback TCP sockets against a real RoomManager (the door, the lobby, the room, the
// referee, the turn log and the restart records), driven by a stepped clock (10 ms a step, a moment of real time for the kernel to deliver the bytes). A link that is cut, a server that
// is stopped and started again over its records, a machine that starts from nothing with the key it was given, the refusals of the server and the vote of the others: what the player's machine
// does by itself, and what it reports to its screens (PauseInfo, the events) and to the place that keeps the key (set_on_key, set_on_forget_key).
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "../common/ants_test_pause.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

// The first turn of a match is sealed this long after the match began (protocol 12: the "Get ready to play!" dialog of every machine)
constexpr uint32_t kPre = net::kMatchStartDelayMs;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
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

using net::NetGame;

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// The create block (protocol 15) of a room for `seats` on `map` (a file name as the server lists it, "" for the server's own choice): what makes a public room
net::CreateBlock block_of(const std::string& map = "", uint8_t seats = 2) {
    net::CreateBlock b;
    b.map_name = map;
    b.seats = seats;
    return b;
}

long process_id() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

// A folder of this process alone (made new, removed with everything in it when the program ends): the restart records of the tests that stop a server and start it again
class ScratchRoot {
public:
    ScratchRoot() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_rejoin_test_" + std::to_string(process_id()) + "_" + suffix);
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

const fs::path& scratch_root() {
    static ScratchRoot root;
    return root.path();
}

std::string temp_dir_for(const char* tag) {
    const fs::path p = scratch_root() / tag;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The server: a real RoomManager behind a real TCP listener on the loopback address, stepped by the test's clock
// ---------------------------------------------------------------------------------------------------------------------------------

// A TcpConnection that the manager owns and the test can still reach (to cut it): the test holds a weak pointer, the manager's room frees the connection when it is done with it
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
    // What the door does with the next connection that the listener accepts
    enum class Door : uint8_t {
        Open,        // it goes to the manager, as it does on a real server
        Refusing,    // it is closed at once: the machine cannot reach the server (its network is down, the server is overloaded)
        Scripted     // its Hello is answered with `script_reason` and it is closed, the manager never sees it: a refusal that no real situation of the test makes at the right moment
    };

    explicit Server(const char* tag = nullptr, ServerLimits limits = ServerLimits()) : limits_(std::move(limits)) {
        if (tag != nullptr) {
            restart_.dir = (fs::path(temp_dir_for(tag)) / "restart").string();
            restart_.identity.game_version = "v0.0.0-test";
            restart_.identity.protocol = net::kProtocolVersion;
            restart_.identity.build_id = "test";
        }
    }

    // The server starts (its uptime is `clock` ms, so a server that starts again has a new clock); it reads the records of its folder and brings the rooms back. `port` 0: any free port,
    // else the port that a server that was stopped had (the machines look for it there)
    bool start(uint32_t world_now, uint32_t clock = 500, uint16_t port = 0) {
        offset_ = clock - world_now;
        listener_ = net::TcpListener::listen(port != 0 ? port : port_, true);
        if (!listener_) return false;
        port_ = listener_->port();
        mgr = std::make_unique<RoomManager>(MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), limits_);
        if (!restart_.dir.empty()) {
            std::string why;
            if (!mgr->enable_restart_records(restart_, why)) throw std::runtime_error("enable_restart_records: " + why);
            report = mgr->restore_rooms(now(world_now));
        }
        ++starts;
        return true;
    }
    // The server stops: told to (SIGTERM: the records are made durable and kept) or not (SIGKILL, a crash); either way every link is gone and the port is free
    void stop(uint32_t world_now, bool graceful) {
        if (graceful && mgr != nullptr) kept_at_stop = mgr->shutdown(now(world_now));
        for (auto& w : wires_) {
            if (auto link = w.lock()) link->close();
        }
        wires_.clear();
        scripted_links_.clear();
        mgr.reset();
        listener_.reset();
    }
    bool running() const noexcept { return mgr != nullptr; }
    uint16_t port() const noexcept { return port_; }
    uint32_t now(uint32_t world_now) const { return world_now + offset_; }

    void pump(uint32_t world_now) {
        if (!listener_) return;
        while (auto link = listener_->accept()) {
            ++accepted;
            std::shared_ptr<net::TcpConnection> shared(std::move(link));
            switch (door) {
                case Door::Open:
                    wires_.push_back(shared);
                    mgr->add_connection(std::make_unique<Wire>(shared), "127.0.0.1", now(world_now));
                    break;
                case Door::Refusing:
                    ++refused;
                    shared->close();
                    break;
                case Door::Scripted:
                    scripted_pending_.push_back(shared);
                    break;
            }
        }
        for (size_t i = 0; i < scripted_pending_.size();) {          // a scripted connection: its Hello is read and answered with the Reject
            std::vector<uint8_t> msg;
            if (scripted_pending_[i]->poll(msg)) {
                if (net::peek_type(msg) == net::MsgType::Hello) {
                    net::HelloMsg hello;
                    if (net::decode(msg, hello)) scripted_hellos.push_back(hello);
                }
                if (script_messages.empty()) {
                    scripted_pending_[i]->send(net::encode(net::RejectMsg{script_reason}));
                    scripted_pending_[i]->close();
                } else {
                    for (const std::vector<uint8_t>& m : script_messages) scripted_pending_[i]->send(m);           // (the link stays open: the machine's side goes on from the answer)
                }
                scripted_links_.push_back(scripted_pending_[i]);
                ++scripted;
                scripted_pending_.erase(scripted_pending_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        for (const std::shared_ptr<net::TcpConnection>& link : scripted_links_) {        // what the machines say after their Hello (a scripted conversation reads it)
            std::vector<uint8_t> msg;
            while (link->poll(msg)) scripted_inbox.push_back(msg);
        }
        if (mgr != nullptr) mgr->update(now(world_now));
    }
    // The link that was accepted last and is still open: the machine that said Hello last (or came back last) is on it. Closing it is the cut of a cable: both ends see it closed
    bool cut_newest() {
        for (size_t i = wires_.size(); i > 0; --i) {
            if (auto link = wires_[i - 1].lock()) {
                if (link->is_open()) {
                    link->close();
                    return true;
                }
            }
        }
        return false;
    }
    // A scripted conversation (script_messages answered the Hello and the link stayed open): a message to the newest of those links that is open, and the cut of it
    bool scripted_send(const std::vector<uint8_t>& message) {
        for (size_t i = scripted_links_.size(); i > 0; --i) {
            if (scripted_links_[i - 1]->is_open()) return scripted_links_[i - 1]->send(message);
        }
        return false;
    }
    bool scripted_cut() {
        for (size_t i = scripted_links_.size(); i > 0; --i) {
            if (scripted_links_[i - 1]->is_open()) {
                scripted_links_[i - 1]->close();
                return true;
            }
        }
        return false;
    }
    bool scripted_heard(net::MsgType type) const {
        for (const std::vector<uint8_t>& m : scripted_inbox) {
            if (net::peek_type(m) == type) return true;
        }
        return false;
    }
    // The link that the door handed to the manager as its n-th (0 is the first machine's first link), if it is still open
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
    RoomStatus status(const std::string& code, uint32_t world_now) const {
        RoomStatus s;
        if (mgr != nullptr) mgr->status(code, s, now(world_now));
        return s;
    }
    // The record of a room (the keys of its seats are in its head: the place that keeps a machine's key gets the same bytes from the NetGame's callback)
    std::string record_path(const std::string& code) const { return RestartStore(restart_).path_for(code); }
    RestartLoaded read_record(const std::string& code) const { return read_restart_record(record_path(code), 1ull << 30); }
    // The record of a room cut after its first `turns` turns, at the end of a frame: what a server finds when the death of its machine took the last second of a file that was not synced (the frames that
    // remain are whole). A server that was stopped (the file is closed) is started again on it, and the machines that ran more turns than it holds are AHEAD of it.
    bool shorten_record(const std::string& code, uint32_t turns) const {
        const std::string path = record_path(code);
        std::vector<uint8_t> bytes;
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const auto u32 = [&bytes](size_t at) {
            return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) | (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
        };
        size_t pos = sizeof(kRestartMagic);
        size_t cut = bytes.size();
        while (pos + kRestartFrameOverhead <= bytes.size()) {
            const uint32_t length = u32(pos + 1);
            const size_t end = pos + 1 + 4 + size_t{length} + 4;
            if (end > bytes.size()) break;
            if (bytes[pos] == static_cast<uint8_t>(RestartFrame::Turns) && length >= 6) {
                const uint32_t first = u32(pos + 5);
                const uint32_t count = static_cast<uint32_t>(bytes[pos + 9]) | (static_cast<uint32_t>(bytes[pos + 10]) << 8);
                if (first + count > turns) {
                    cut = pos;
                    break;
                }
            }
            pos = end;
        }
        if (cut == bytes.size()) return false;                      // (nothing to cut: the record holds no more turns than that)
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(cut));
        return static_cast<bool>(out);
    }

    std::unique_ptr<RoomManager> mgr;
    Door door{Door::Open};
    net::RejectReason script_reason{net::RejectReason::RejoinFailed};
    std::vector<std::vector<uint8_t>> script_messages;      // Scripted: when it is not empty these messages answer the Hello, in order, instead of a Reject (and the link is left open)
    uint32_t accepted{0};                       // the connections that the listener accepted, whatever the door did with them
    uint32_t refused{0};
    uint32_t scripted{0};
    std::vector<net::HelloMsg> scripted_hellos; // what the scripted connections said (the tests look at the Hello: the key, the turns)
    std::vector<std::vector<uint8_t>> scripted_inbox;       // ... and what they said after it
    size_t kept_at_stop{0};
    RestoreReport report;                       // what the last start brought back
    int starts{0};

private:
    ServerLimits limits_;
    RestartConfig restart_;
    std::unique_ptr<net::TcpListener> listener_;
    uint16_t port_{0};
    uint32_t offset_{0};
    std::vector<std::weak_ptr<net::TcpConnection>> wires_;
    std::vector<std::shared_ptr<net::TcpConnection>> scripted_pending_;
    std::vector<std::shared_ptr<net::TcpConnection>> scripted_links_;
};

// A room that holds the seats of players whose connections are lost (the countdown after a pause is off unless a test is about it: each would wait ten seconds after every return)
RoomSpec held_spec(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
    RoomSpec s;
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
// A machine: an engine, its NetGame, and the little bit of application that the room needs (load the map, report, give orders, freeze at the end of the match)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Machine {
    std::string name;
    sim::SimulationEngine sim;
    NetGame net{sim};
    std::vector<NetGame::Event> events;
    std::map<uint64_t, uint64_t> hash_at;         // the state hash after every tick that the runner ran live (a tick of a catch-up has no hook: none is recorded)
    uint64_t ticks{0};
    uint32_t loads{0};                            // how many times the machine loaded the map (the reload path loads it again)
    bool orders{true};                            // it gives orders, as a person clicks
    uint32_t next_order_ms{0};
    uint32_t rng{1};
    uint32_t map_w{40};
    uint32_t map_h{40};
    uint32_t last_frame_ms{0};
    uint32_t clock_lag{0};                        // the application's network clock never advances more than a second per frame: what a window that stood still lost of the real time
    bool hung{false};                             // the window does not run: no frames at all
    std::vector<net::RejoinKey> keys_given;       // what set_on_key was told, in order (the place that keeps the key of the seat)
    std::vector<net::RejoinKey> keys_forgotten;   // what set_on_forget_key was told
    size_t leave_on_key{0};                       // the n-th time that the machine is given its key it leaves, from inside that very call (0: never)
    bool leave_on_forget{false};                  // the first time that it is told to let go of the key it leaves, from inside that very call
    bool fail_load{false};                        // the machine cannot load the map of a Start (the file is missing on its computer)
    std::string status_at_start;                  // the status line when the last Start arrived (a room that fills up starts at once: the waiting room is a few frames long)

    explicit Machine(std::string n, uint32_t seed = 1) : name(std::move(n)), rng(seed) {
        net.set_discovery(0);
        net.set_on_key([this](const net::RejoinKey& k) {
            keys_given.push_back(k);
            if (leave_on_key != 0 && keys_given.size() == leave_on_key) net.leave();
        });
        net.set_on_forget_key([this](const net::RejoinKey& k) {
            keys_forgotten.push_back(k);
            if (leave_on_forget) net.leave();
        });
        net.set_on_tick([this]() {
            ++ticks;
            hash_at[sim.current_tick()] = sim.state_hash().total;
        });
    }

    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    bool my_seat_is(uint8_t seat) const { return net.my_seat() == seat; }
    bool saw(NetGame::Event::Type t) const {
        for (const auto& e : events) {
            if (e.type == t) return true;
        }
        return false;
    }
    size_t count(NetGame::Event::Type t) const {
        size_t n = 0;
        for (const auto& e : events) n += e.type == t ? 1u : 0u;
        return n;
    }

    void handle(const NetGame::Event& ev) {
        events.push_back(ev);
        if (ev.type == NetGame::Event::Type::StartRequested) {
            status_at_start = net.status_text();
            const net::StartMsg& s = net.start_info();
            assets::LevelData level;
            uint64_t hash = 0;
            const bool ok = !fail_load && level.load_lvl(maps_dir() + s.map_name) && net::hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());
                sim.init(level, s.seed, s.roster);
                for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
                sim::apply_start_teams(sim, s.teams());                   // (after the names, as the application does: the News Flash names the players)
                map_w = level.width();
                map_h = level.height();
                ++loads;
            }
            net.report_loaded(ok);
        }
    }

    // One frame of the application: the network, its events, the player's orders; the match's end freezes the network (what check_match_over does)
    void frame(uint32_t now) {
        if (hung) return;
        if (last_frame_ms != 0 && now - last_frame_ms > 1000) clock_lag += now - last_frame_ms - 1000;      // a frame after a stop hands the network one second at the most
        const uint32_t gap = last_frame_ms != 0 && now - last_frame_ms > 1000 ? now - last_frame_ms - 1000 : 0u;
        last_frame_ms = now;
        net.update(now - clock_lag);
        if (gap > 0) net.note_gap(gap);                                       // a host that said nothing has been silent for the gap too
        for (const NetGame::Event& ev : net.take_events()) handle(ev);
        if (net.phase() != NetGame::Phase::Playing) return;
        if (sim.is_match_over()) {
            net.freeze();
            return;
        }
        if (orders && now >= next_order_ms && sim.current_tick() > 0) {
            next_order_ms = now + 700;
            std::vector<uint32_t> mine;
            for (const auto& a : sim.get_world_state().ants) {
                if (a.player_id == net.my_seat()) mine.push_back(a.id);
            }
            if (!mine.empty()) {
                sim::Command c;
                c.type = (next_random() % 4 == 0) ? sim::CommandType::Hatch : sim::CommandType::GroupMove;
                if (c.type == sim::CommandType::GroupMove) {
                    c.tile_x = static_cast<int16_t>(next_random() % map_w);
                    c.tile_y = static_cast<int16_t>(next_random() % map_h);
                    for (size_t i = 0; i < mine.size() && i < 6; ++i) c.ants.push_back(mine[(i + next_random()) % mine.size()]);
                }
                net.submit(c);
            }
        }
    }
    // The player quits the match with two sides left: the end of the match for everybody (the application's confirm_quit)
    void quit() {
        sim::Command q;
        q.type = sim::CommandType::Quit;
        q.issuer = net.my_seat();
        net.submit(q);
    }
};

// A server and machines on one stepped clock
struct World {
    Server server;
    std::vector<std::unique_ptr<Machine>> machines;
    uint32_t now{1000};
    uint32_t steps_{0};
    std::function<void()> between;                // called in every step after the server's pass and before the machines' frames: a test holds a machine back at the moment that the server has done something

    explicit World(const char* tag = nullptr, ServerLimits limits = ServerLimits()) : server(tag, std::move(limits)) {}

    uint32_t server_now() const { return server.now(now); }

    Machine& add_machine(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name, static_cast<uint32_t>(machines.size() + 1) * 7919u));
        return *machines.back();
    }
    // A new machine joins the room (the TCP link is made at once; the room answers as the clock runs); `create`: the block of its Hello (protocol 15), which makes the room on a server that offers public ones
    Machine& join(const std::string& name, const std::string& room, uint8_t want_seat = 255, const std::string& token = std::string(), const net::CreateBlock* create = nullptr, uint8_t platform = 0) {
        Machine& m = add_machine(name);
        if (create != nullptr) m.net.set_create(*create);
        if (platform != 0) m.net.set_platform(platform);                      // (what the machine says it runs on; 0: the build's own)
        if (!m.net.join("127.0.0.1", server.port(), name, want_seat, room, token)) throw std::runtime_error("join failed");
        const uint32_t before = server.accepted;
        run_until([&]() { return server.accepted > before; }, 2000);          // (the machine's link is the newest one that the door accepted: a test can cut it)
        if (server.door == Server::Door::Open) {                              // the next one is told after this one: the room gives the seats in the order in which the Hellos are read, and on a loopback that does not deliver within the pass that need not be the order of the joins
            run_until([&]() { return m.net.my_seat() < sim::MAX_PLAYERS || m.net.phase() == NetGame::Phase::Failed || m.net.phase() == NetGame::Phase::Over; }, 3000);
        }
        return m;
    }
    Machine& join_creating(const std::string& name, const std::string& room, const net::CreateBlock& block, uint8_t want_seat = 255) { return join(name, room, want_seat, std::string(), &block); }
    void pump() {
        server.pump(now);
        if (between) between();
        for (auto& m : machines) m->frame(now);
    }
    // 10 ms of game time per step. The sockets are real and the clock is not: a loopback link delivers within the pass that wrote to it on Linux, but not on every system (a Mac's loopback is handled by a kernel thread: a message can be read a pass or
    // more later, and two messages that were sent in different passes can be read together), so a test that has to see a state between two messages holds the sender back (World::between); now and then (every 16th step) the test gives the kernel a moment
    // of real time, and the others yield (a test of many minutes of game time must not be paid for in pauses: they add up)
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {            // (counted, not compared with an end time: the clock of a test may wrap)
            now += 10;
            pump();
            if ((++steps_ & 15u) == 0) ants_test::short_pause();
            else std::this_thread::yield();
        }
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
            if (cond()) return true;
            run(10);
        }
        // The game clock is virtual but the sockets are real: on a busy machine the kernel can be late with bytes that were sent long ago in game time. It gets up to two more seconds of real
        // time with the game clock standing still, so that nothing times out meanwhile; a wait that succeeds never gets here.
        for (int i = 0; i < 2000 && !cond(); ++i) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
    RoomStatus status(const std::string& code) const { return server.status(code, now); }
    // The match is under way on every machine given: it plays and has run ticks
    bool running(std::initializer_list<Machine*> ms) const {
        for (const Machine* m : ms) {
            if (m->net.phase() != NetGame::Phase::Playing || m->ticks < 40) return false;
        }
        return true;
    }
    // The room's match is over: the machines have the end of it and the referee has finished
    bool finished(const std::string& code) const { return status(code).state == RoomState::Finished; }
};

bool all_equal(const std::vector<Machine*>& ms) {
    for (size_t i = 1; i < ms.size(); ++i) {
        if (ms[i]->sim.state_hash() != ms[0]->sim.state_hash()) return false;
    }
    return true;
}

// The states agree: every tick that two machines both ran live has the same hash on both
bool hashes_agree(const Machine& a, const Machine& b) {
    size_t compared = 0;
    for (const auto& kv : a.hash_at) {
        const auto other = b.hash_at.find(kv.first);
        if (other == b.hash_at.end()) continue;
        if (other->second != kv.second) return false;
        ++compared;
    }
    return compared > 0;
}

// The front page's lobby (protocol 16) on a real link: a ClientLobby that says "page" (it cannot play a match), stepped by the test's clock (World::between)
struct Page {
    std::unique_ptr<net::TcpConnection> link;
    std::unique_ptr<net::ClientLobby> lobby;
    Page(uint16_t port, const std::string& name, const std::string& room, const std::optional<net::CreateBlock>& create) : link(net::TcpConnection::connect("127.0.0.1", port)) {
        net::ClientLobby::Config cc;
        cc.name = name;
        cc.room = room;
        cc.client_kind = net::kClientPage;
        cc.create = create;
        lobby = std::make_unique<net::ClientLobby>(link.get(), cc);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------------------------------------------------------------

void run_way_back_tests() {
    TEST_CASE("RJ1.1 A Link Cut In The Middle Of A Match: The NetGame Comes Back By Itself (A New Link, The Hello With Its Key, The Turns It Lacks), The Match Goes On, And Both Machines And The Referee End In The Same State") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-1"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-1");
        Machine& b = w.join("Bob", "RJ-1");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        w.run(3000);
        ASSERT_TRUE(w.status("RJ-1").state == RoomState::Running && !w.status("RJ-1").paused);
        ASSERT_EQ(w.server.accepted, 2u);
        const uint8_t bob = b.net.my_seat();
        const uint32_t b_turns = b.net.turns_executed();
        const uint32_t cut_tick = w.status("RJ-1").ticks;
        ASSERT_TRUE(!a.net.paused() && !b.net.paused() && !b.net.pause_info().reconnecting && a.net.pause_info().missing.empty());
        ASSERT_TRUE(a.keys_given.size() == 1 && b.keys_given.size() == 1 && a.keys_forgotten.empty() && b.keys_forgotten.empty());      // (each was given its key at its Welcome)
        ASSERT_TRUE(a.keys_given[0].room == "RJ-1" && a.keys_given[0].seat == a.net.my_seat() && b.keys_given[0].seat == bob && !net::key_is_zero(b.keys_given[0].key));
        ASSERT_TRUE(a.keys_given[0].server == "127.0.0.1:" + std::to_string(w.server.port()) && !net::key_matches(a.keys_given[0].key, b.keys_given[0].key));
        ASSERT_TRUE(w.server.cut_newest());                                              // Bob's cable
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-1").paused; }, 2000));        // the room sees it and pauses for everybody
        RoomStatus s = w.status("RJ-1");
        ASSERT_TRUE(s.absent.size() == 1 && s.absent[0].seat == bob);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);                               // the machine does not leave the match
        bool saw_catching_up = false;
        bool b_linking = false;                                                          // what each machine's screens are told while it happens
        bool b_catching = false;
        bool a_missing = false;
        bool a_missing_catching = false;
        bool a_paused_all_along = true;
        uint8_t b_percent = 0;
        bool consistent = true;                                                          // what the screens are told never contradicts itself
        w.between = [&]() {                                                              // Bob's machine is held from the moment that the server has him catching up until Ann has been shown that: the two Presence messages (catching up, back) must not reach one of her frames together, as they can on a loopback that does not deliver within the pass
            bool catching = false;
            for (const RoomStatus::Absent& e : w.status("RJ-1").absent) catching = catching || (e.catching_up && e.seat == bob);
            b.hung = catching && !a_missing_catching;
        };
        ASSERT_TRUE(w.run_until([&]() {
            const RoomStatus st = w.status("RJ-1");
            for (const RoomStatus::Absent& e : st.absent) saw_catching_up = saw_catching_up || (e.catching_up && e.seat == bob);
            const net::PauseInfo bi = b.net.pause_info();
            const net::PauseInfo ai = a.net.pause_info();
            if (bi.reconnecting) {
                b_linking = b_linking || (bi.attempts >= 1 && bi.give_up_s > 0 && !bi.catching_up && bi.missing.empty());
                consistent = consistent && b.net.paused();
            }
            if (bi.catching_up) {
                b_catching = true;
                consistent = consistent && bi.catch_up_percent <= 100 && bi.catch_up_percent >= b_percent && !bi.reconnecting && b.net.paused();       // (it only grows)
                b_percent = bi.catch_up_percent;
            }
            if (!ai.missing.empty()) {
                consistent = consistent && ai.missing.size() == 1 && ai.missing[0].seat == bob && ai.missing[0].name == "Bob";
                a_missing = true;
                a_missing_catching = a_missing_catching || ai.missing[0].catching_up;
                a_paused_all_along = a_paused_all_along && a.net.paused();
            }
            return !st.paused;
        }, 15000));
        w.between = nullptr;
        b.hung = false;
        ASSERT_TRUE(saw_catching_up);                                                    // it was given the match again, not just let back in
        ASSERT_TRUE(consistent && b_linking && b_catching && a_missing && a_paused_all_along);        // Bob's screen showed the way back and the catch-up, Ann's the seat that was missing
        ASSERT_EQ(static_cast<unsigned>(b_percent), 100u);                               // the catch-up ended at its 100 percent, waiting for the server's word
        ASSERT_TRUE(a_missing_catching);                                                 // ... and that it was back and catching up
        s = w.status("RJ-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.drops_by_vote == 0 && s.drops_by_cap == 0);
        ASSERT_EQ(w.server.accepted, 3u);                                                // one new link, made by the NetGame itself
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        ASSERT_FALSE(b.saw(NetGame::Event::Type::HostLeft) || b.saw(NetGame::Event::Type::Failed) || b.saw(NetGame::Event::Type::Desync));
        ASSERT_TRUE(b.net.turns_executed() >= b_turns);
        w.run(300);
        ASSERT_TRUE(!a.net.paused() && !b.net.paused() && a.net.pause_info().missing.empty() && !b.net.pause_info().reconnecting && !b.net.pause_info().catching_up);
        ASSERT_EQ(b.count(NetGame::Event::Type::Rejoined), size_t{1});                   // one event: this machine is back in the match (the screen leaves its catch-up view)
        ASSERT_EQ(a.count(NetGame::Event::Type::Rejoined), size_t{0});
        ASSERT_TRUE(b.keys_given.size() == 2 && net::key_matches(b.keys_given[0].key, b.keys_given[1].key) && b.keys_given[1].seat == bob);        // a rejoin's Welcome says the key again
        ASSERT_TRUE(a.keys_given.size() == 1 && a.keys_forgotten.empty() && b.keys_forgotten.empty());
        w.run(3700);
        ASSERT_TRUE(w.status("RJ-1").ticks > cut_tick + 40);                             // the match goes on
        ASSERT_TRUE(b.net.turns_executed() > b_turns + 40);
        a.quit();                                                                        // two sides: the match ends for everybody
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-1") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-1");
        ASSERT_TRUE(end.state == RoomState::Finished && end.referee_hash != 0 && end.rejoins == 1);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);            // the referee's state and both machines': one
        ASSERT_TRUE(a.hash_at.count(end.ticks) == 1 && a.hash_at[end.ticks] == end.referee_hash && b.hash_at.count(end.ticks) == 1 && b.hash_at[end.ticks] == end.referee_hash);
        ASSERT_TRUE(hashes_agree(a, b));
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
        ASSERT_TRUE(a.keys_forgotten.size() == 1 && b.keys_forgotten.size() == 1);        // the match is over: the keys can be let go of, once each
        ASSERT_TRUE(net::key_matches(a.keys_forgotten[0].key, a.keys_given[0].key) && net::key_matches(b.keys_forgotten[0].key, b.keys_given[0].key));
        ASSERT_TRUE(b.keys_forgotten[0].room == "RJ-1" && b.keys_forgotten[0].seat == bob);
        a.net.leave();                                                                   // the player's Leave on the scorecard: the key was let go of at the end of the match, and is not again
        ASSERT_EQ(a.keys_forgotten.size(), size_t{1});
    } TEST_END();

    TEST_CASE("RJ1.2 The Server Restarts With Its Records (Stopped After Some Play, Started Again On The Same Records Folder And Port): Both NetGames Look For It By Themselves, Come Back With Their Turns, The Match Goes On And Ends In The Same State On Both And The Restored Referee") {
        World w("rejoin-restart");
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-2"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-2");
        Machine& b = w.join("Bob", "RJ-2");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        w.run(6000);
        RoomStatus s = w.status("RJ-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && !s.paused && s.record_kept);
        const uint32_t sealed = s.turns;
        ASSERT_TRUE(sealed > 100);
        const uint16_t port = w.server.port();
        w.server.stop(w.now, true);                                                      // SIGTERM: the records are made durable and stay
        ASSERT_EQ(w.server.kept_at_stop, size_t{1});
        w.run(5000);                                                                     // the machines look for the server (they try every 2 s; nobody listens)
        ASSERT_EQ(a.net.phase(), NetGame::Phase::Playing);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        ASSERT_FALSE(a.saw(NetGame::Event::Type::HostLeft) || b.saw(NetGame::Event::Type::HostLeft));
        ASSERT_TRUE(w.server.start(w.now, 500, port));                                   // a new uptime, the same port: the machines have no other address
        ASSERT_EQ(w.server.port(), port);
        ASSERT_TRUE(w.server.report.items.empty() && w.server.report.queued == 1);      // the record was judged good: its replay runs in the server's passes, in slices
        ASSERT_TRUE(w.run_until([&]() { return w.server.mgr->restore_report().items.size() == 1; }, 2000));
        const RestoreItem& restored = w.server.mgr->restore_report().items[0];
        ASSERT_TRUE(restored.outcome == RestoreItem::Outcome::Restored && restored.code == "RJ-2" && restored.turns == sealed);
        s = w.status("RJ-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && s.absent.size() == 2 && s.turns == sealed);       // the room is back, every seat held
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-2").paused; }, 60000));
        s = w.status("RJ-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 2 && s.absent.empty() && s.drops_by_cap == 0 && s.drops_by_vote == 0);
        ASSERT_EQ(a.net.phase(), NetGame::Phase::Playing);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        w.run(300);
        ASSERT_TRUE(a.count(NetGame::Event::Type::Rejoined) == 1 && b.count(NetGame::Event::Type::Rejoined) == 1);     // both came back: one event each
        ASSERT_TRUE(a.keys_given.size() == 2 && b.keys_given.size() == 2 && a.keys_forgotten.empty() && b.keys_forgotten.empty());      // (the keys of the rejoins are the same: the server's restored room has them in its record)
        ASSERT_TRUE(net::key_matches(a.keys_given[0].key, a.keys_given[1].key) && net::key_matches(b.keys_given[0].key, b.keys_given[1].key));
        w.run(4700);
        ASSERT_TRUE(w.status("RJ-2").turns > sealed + 60);                               // the match goes on: new turns are sealed
        ASSERT_TRUE(a.net.turns_executed() > sealed + 60 && b.net.turns_executed() > sealed + 60);
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-2") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-2");
        ASSERT_TRUE(end.state == RoomState::Finished && end.restored && end.referee_hash != 0);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);        // the machines that lived through the restart and the restored referee: one state at the end
        ASSERT_TRUE(a.hash_at.count(end.ticks) == 1 && a.hash_at[end.ticks] == end.referee_hash && b.hash_at.count(end.ticks) == 1 && b.hash_at[end.ticks] == end.referee_hash);
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
    } TEST_END();

    TEST_CASE("RJ1.3 A Machine That Starts From Nothing (A Reloaded Page, A Game Started Again) Joins With Its Key: The Room Gives It Its Seat And The Match From The Server's Log (Start, The Stream, CaughtUp), It Is Back In The Match With The Events Marked `rejoin`, A Key That Fits No Seat Is Refused, And Both Machines End In The Same State As The Referee") {
        World w("rejoin-reload");
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-3"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-3");
        Machine& b = w.join("Bob", "RJ-3");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        w.run(25000);                                                                    // some 500 turns
        const uint8_t seat = b.net.my_seat();
        const net::SeatKey key = w.server.read_record("RJ-3").head.keys[seat];            // the key of Bob's seat, as the record has it
        ASSERT_FALSE(net::key_is_zero(key));
        ASSERT_TRUE(b.keys_given.size() == 1 && net::key_matches(b.keys_given[0].key, key) && b.keys_given[0].seat == seat && b.keys_given[0].room == "RJ-3");        // the NetGame gave the place that keeps it the same bytes
        ASSERT_FALSE(a.events.empty());
        for (const NetGame::Event& e : a.events) ASSERT_FALSE(e.rejoin);                  // (the first start of a match is no rejoin)
        w.machines.erase(w.machines.begin() + 1);                                        // the page of Bob is gone: its game, its engine and its links with it (no goodbye)
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-3").paused; }, 3000));
        ASSERT_TRUE(w.status("RJ-3").absent.size() == 1 && w.status("RJ-3").absent[0].seat == seat);
        const uint32_t sealed = w.status("RJ-3").turns;                                  // the match so far: all of it is to be given to the new machine
        ASSERT_TRUE(sealed > 300);
        Machine& wrong = w.add_machine("Mallory");                                       // a key that fits no seat reveals nothing: the room answers as it answers any Hello for a running match
        net::SeatKey made_up = key;
        made_up[0] = static_cast<uint8_t>(made_up[0] ^ 0xFFu);
        ASSERT_TRUE(wrong.net.join("127.0.0.1", w.server.port(), "Mallory", seat, "RJ-3", "", made_up));
        ASSERT_TRUE(w.run_until([&]() { return wrong.net.phase() == NetGame::Phase::Failed; }, 5000));
        ASSERT_TRUE(wrong.net.fail_reason() == NetGame::FailReason::Rejected && wrong.net.reject_reason() == net::RejectReason::MatchRunning);
        ASSERT_EQ(wrong.net.status_text(), std::string("The server does not hold your seat."));        // (a first join would be told "The match has already started.")
        ASSERT_TRUE(wrong.keys_forgotten.size() == 1 && net::key_matches(wrong.keys_forgotten[0].key, made_up) && wrong.keys_given.empty());     // the key is no good: the place that kept it is told
        ASSERT_TRUE(w.status("RJ-3").paused && w.status("RJ-3").rejoins == 0);
        Machine& b2 = w.add_machine("Bob");                                              // a new machine with nothing but the key
        ASSERT_TRUE(b2.net.join("127.0.0.1", w.server.port(), "Bob", seat, "RJ-3", "", key));
        ASSERT_EQ(b2.net.phase(), NetGame::Phase::Connecting);
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Playing && !w.status("RJ-3").paused; }, 30000));
        ASSERT_EQ(b2.net.my_seat(), seat);
        ASSERT_EQ(b2.loads, 1u);                                                         // it loaded the map, as for any start
        bool start_flag = false;
        bool begun_flag = false;
        for (const NetGame::Event& e : b2.events) {
            start_flag = start_flag || (e.type == NetGame::Event::Type::StartRequested && e.rejoin);
            begun_flag = begun_flag || (e.type == NetGame::Event::Type::Begun && e.rejoin);
        }
        ASSERT_TRUE(start_flag && begun_flag);                                           // the screen skips the start dialog and the start sound
        ASSERT_TRUE(b2.keys_given.size() == 1 && net::key_matches(b2.keys_given[0].key, key) && b2.keys_given[0].seat == seat && b2.keys_forgotten.empty());        // the Welcome of the rejoin: the key again
        ASSERT_EQ(b2.count(NetGame::Event::Type::StartRequested), size_t{1});
        ASSERT_EQ(b2.count(NetGame::Event::Type::Begun), size_t{1});
        RoomStatus s = w.status("RJ-3");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.names[seat] == "Bob");
        ASSERT_TRUE(b2.net.turns_executed() >= sealed);                                  // the whole match was given to it
        w.run(5000);
        ASSERT_TRUE(b2.net.turns_executed() > sealed + 60);                              // and the match goes on
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-3") && a.sim.is_match_over() && b2.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-3");
        ASSERT_TRUE(end.state == RoomState::Finished && end.rejoins == 1 && end.referee_hash != 0);
        ASSERT_TRUE(all_equal({&a, &b2}) && a.sim.state_hash().total == end.referee_hash);
        ASSERT_TRUE(b2.hash_at.count(end.ticks) == 1 && b2.hash_at[end.ticks] == end.referee_hash);
        ASSERT_FALSE(a.net.desynced() || b2.net.desynced());
        ASSERT_TRUE(b2.count(NetGame::Event::Type::Rejoined) == 1 && b2.keys_forgotten.size() == 1);     // it caught up: the screen leaves the loading view; the match is over: the key is let go of
    } TEST_END();

    TEST_CASE("RJ1.3b Protocol 13: A Machine That Starts From Nothing Is Given The Start With The Leader's Teams: Its Engine Has The Alliances Before Its First Tick (Made From The Start Message, After Init And The Names), The Catch-Up Runs On Top Of Them, And It Stands At The State Of The Machine That Never Left, Tick For Tick; The Room's Own Engine Agrees (Its Status Says The Teams)") {
        using net::FillLevel;
        const auto allies_of = [](const sim::SimulationEngine& e) {
            std::string out;
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
            return out;                                                                  // by seat: the ally's seat, 4 for none
        };
        World w("rejoin-teams");
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-T", 4), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-T");
        Machine& b = w.join("Bob", "RJ-T");
        ASSERT_TRUE(w.run_until([&]() { return a.net.is_leader() && b.net.phase() == NetGame::Phase::Room && a.net.room().slots[1].state == net::SlotState::Client; }, 8000));
        a.net.set_fill_bots(net::FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard}));        // Blue (seat 2) an Easy bot, Black (3) a Hard one
        a.net.set_start_teams(sim::StartTeams{true, 1, 2});                              // Red + Blue (Bob and the Easy bot) against Green + Black (Ann and the Hard bot)
        ASSERT_TRUE(a.net.request_start());
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        ASSERT_EQ(allies_of(a.sim), std::string("3210"));
        ASSERT_EQ(allies_of(b.sim), std::string("3210"));
        ASSERT_TRUE(a.net.start_info().team_a == 1 && a.net.start_info().team_b == 2);
        RoomStatus s = w.status("RJ-T");
        ASSERT_TRUE(s.state == RoomState::Running && s.teams == "1+2" && s.allies[0] == 3 && s.allies[1] == 2 && s.bots.size() == 2 && s.bots[0].level == "easy" && s.bots[1].level == "hard");
        w.run(15000);                                                                    // some 300 turns
        const uint8_t seat = b.net.my_seat();
        const net::SeatKey key = w.server.read_record("RJ-T").head.keys[seat];
        ASSERT_FALSE(net::key_is_zero(key));
        w.machines.erase(w.machines.begin() + 1);                                        // Bob's page is gone: its game, its engine and its links with it
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-T").paused; }, 3000));
        const uint32_t sealed = w.status("RJ-T").turns;
        ASSERT_TRUE(sealed > 200);
        Machine& b2 = w.add_machine("Bob");                                              // a new machine with nothing but the key
        ASSERT_TRUE(b2.net.join("127.0.0.1", w.server.port(), "Bob", seat, "RJ-T", "", key));
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Playing && !w.status("RJ-T").paused; }, 30000));
        ASSERT_EQ(b2.loads, 1u);                                                         // it loaded the map and made its engine from the Start
        bool from_nothing = false;
        for (const NetGame::Event& e : b2.events) from_nothing = from_nothing || (e.type == NetGame::Event::Type::StartRequested && e.rejoin);
        ASSERT_TRUE(from_nothing);
        ASSERT_TRUE(b2.net.start_info().team_a == 1 && b2.net.start_info().team_b == 2);   // the Start that a machine from nothing is sent has the teams
        ASSERT_EQ(allies_of(b2.sim), std::string("3210"));                               // (after the catch-up too: nobody broke a team)
        ASSERT_TRUE(b2.net.turns_executed() >= sealed);
        w.run(6000);
        size_t common = 0;                                                               // the ticks that both machines ran live: the same hash at every one of them
        for (const auto& tick_hash : b2.hash_at) {
            const auto other = a.hash_at.find(tick_hash.first);
            if (other == a.hash_at.end()) continue;
            ASSERT_EQ(other->second, tick_hash.second);
            ++common;
        }
        ASSERT_TRUE(common > 30);
        s = w.status("RJ-T");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.teams == "1+2" && s.allies[1] == 2);
        ASSERT_FALSE(a.net.desynced() || b2.net.desynced());
    } TEST_END();

    TEST_CASE("RJ1.4 A Server That Lost The Last Second Of Its Record Answers The Hello Of A Machine That Is Ahead Of It With BadRequest: The NetGame Starts The Match From Nothing With Its Key (A New Lobby, Start, The Stream), Once; A Second BadRequest Ends It As Any Refusal Does And Nothing Tries Again") {
        {   // the real thing: a server restarted on a record that lacks the last 40 turns, two machines that ran them
            World w("rejoin-ahead");
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-4"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-4");
            Machine& b = w.join("Bob", "RJ-4");
            int a_dropped = 0;                                                           // the prediction of one's own orders is on: the machine's engine is made again, so the prediction that stood
            int b_dropped = 0;                                                           // on the old session's runner is destroyed (and whoever points at its engine is told)
            a.net.set_prediction_enabled(true);
            b.net.set_prediction_enabled(true);
            a.net.set_on_prediction_dropped([&a_dropped]() { ++a_dropped; });
            b.net.set_on_prediction_dropped([&b_dropped]() { ++b_dropped; });
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}) && a.net.predicting() && b.net.predicting(); }, 12000 + kPre));
            w.run(20000);
            const uint32_t sealed = w.status("RJ-4").turns;
            ASSERT_TRUE(sealed > 300);
            const uint16_t port = w.server.port();
            w.server.stop(w.now, true);
            ASSERT_TRUE(w.server.shorten_record("RJ-4", sealed - 40));                   // two seconds of play never reached the disk
            ASSERT_TRUE(a.net.turns_executed() + 4 >= sealed && b.net.turns_executed() + 4 >= sealed);        // (the machines saw them)
            w.run(3000);
            ASSERT_TRUE(w.server.start(w.now, 500, port));
            ASSERT_TRUE(w.server.report.items.empty() && w.server.report.queued == 1);  // (replayed in the server's passes, in slices)
            ASSERT_TRUE(w.run_until([&]() { return w.server.mgr->restore_report().items.size() == 1; }, 2000));
            ASSERT_TRUE(w.server.mgr->restore_report().items[0].outcome == RestoreItem::Outcome::Restored && w.server.mgr->restore_report().items[0].turns == sealed - 40);
            ASSERT_TRUE(w.run_until([&]() { return a.count(NetGame::Event::Type::StartRequested) == 2 && b.count(NetGame::Event::Type::StartRequested) == 2; }, 60000));      // each starts again from nothing
            ASSERT_TRUE(w.run_until([&]() { return a.count(NetGame::Event::Type::Begun) == 2 && b.count(NetGame::Event::Type::Begun) == 2 && !w.status("RJ-4").paused; }, 60000));
            for (Machine* m : {&a, &b}) {
                ASSERT_EQ(m->net.phase(), NetGame::Phase::Playing);
                ASSERT_EQ(m->loads, 2u);
                size_t flagged = 0;
                for (const NetGame::Event& e : m->events) flagged += (e.rejoin ? 1u : 0u);
                ASSERT_EQ(flagged, size_t{2});                                           // the second start and the second begin only
                ASSERT_FALSE(m->saw(NetGame::Event::Type::HostLeft) || m->saw(NetGame::Event::Type::Failed));
            }
            RoomStatus s = w.status("RJ-4");
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.rejoins == 2 && s.absent.empty() && s.turns >= sealed - 40);
            ASSERT_TRUE(a_dropped == 1 && b_dropped == 1);                               // told once, when the old session went
            ASSERT_TRUE(a.keys_given.size() == 2 && b.keys_given.size() == 2);           // the Start of the match, and the Welcome of the new start: the same key, said again
            ASSERT_TRUE(net::key_matches(a.keys_given[0].key, a.keys_given[1].key) && net::key_matches(b.keys_given[0].key, b.keys_given[1].key));
            ASSERT_TRUE(a.keys_given[1].seat == a.net.my_seat() && b.keys_given[1].seat == b.net.my_seat() && a.keys_forgotten.empty() && b.keys_forgotten.empty());
            ASSERT_TRUE(w.run_until([&]() { return a.net.predicting() && b.net.predicting(); }, 5000));
            ASSERT_TRUE(a.net.prediction()->stats().starts == 1 && b.net.prediction()->stats().starts == 1);      // a new prediction on the new session, not the old one resumed
            w.run(4000);
            ASSERT_TRUE(w.status("RJ-4").turns > sealed - 40 + 60);
            a.quit();
            ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-4") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
            w.run(2000);
            const RoomStatus end = w.status("RJ-4");
            ASSERT_TRUE(end.state == RoomState::Finished && end.referee_hash != 0);
            ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);
            ASSERT_TRUE(a.hash_at.count(end.ticks) == 1 && a.hash_at[end.ticks] == end.referee_hash && b.hash_at.count(end.ticks) == 1 && b.hash_at[end.ticks] == end.referee_hash);
            ASSERT_FALSE(a.net.desynced() || b.net.desynced());
        }
        {   // the fallback is taken once: scripted refusals (the door answers the Hello with BadRequest and the manager never sees it)
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-4B"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-4B");
            Machine& b = w.join("Bob", "RJ-4B");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            w.run(2000);
            w.server.script_reason = net::RejectReason::BadRequest;
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 5000));
            w.server.door = Server::Door::Open;                                          // the machine's next link goes to the real server
            ASSERT_EQ(w.server.scripted_hellos.size(), size_t{1});
            ASSERT_TRUE(!net::key_is_zero(w.server.scripted_hellos[0].key) && w.server.scripted_hellos[0].have_turns > 0);        // the Hello that was refused carried turns
            ASSERT_TRUE(w.run_until([&]() { return b.count(NetGame::Event::Type::Begun) == 2 && !w.status("RJ-4B").paused; }, 60000));
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
            ASSERT_TRUE(w.status("RJ-4B").rejoins == 1 && b.loads == 2 && b.count(NetGame::Event::Type::StartRequested) == 2);
            ASSERT_FALSE(b.saw(NetGame::Event::Type::HostLeft));
            w.run(3000);
            w.server.door = Server::Door::Scripted;                                      // the second time
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 2; }, 5000));
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 3000));          // it ends as a refusal does, it does not start again from nothing a second time
            w.run(10);
            ASSERT_TRUE(b.saw(NetGame::Event::Type::HostLeft));
            ASSERT_EQ(b.net.status_text(), std::string("The host refused the connection."));
            ASSERT_TRUE(b.keys_forgotten.empty());                                       // (the key is good: the player may use Rejoin later)
            ASSERT_EQ(b.count(NetGame::Event::Type::StartRequested), size_t{2});
            w.server.door = Server::Door::Open;
            const uint32_t seen = w.server.accepted;
            w.run(10000);
            ASSERT_EQ(w.server.accepted, seen);                                          // nothing tries again
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Over);
        }
        {   // the new lobby is refused in its turn (the seat was dropped meanwhile): the words and the rule of the way back, because the machine still holds its key
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-4D"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-4D");
            Machine& b = w.join("Bob", "RJ-4D");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            w.run(2000);
            w.server.script_reason = net::RejectReason::BadRequest;                      // the session's Hello is answered BadRequest ...
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 5000));
            w.server.script_reason = net::RejectReason::Dropped;                         // ... and the Hello of the new lobby Dropped
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Failed; }, 5000));
            ASSERT_EQ(w.server.scripted, 2u);
            ASSERT_TRUE(b.net.fail_reason() == NetGame::FailReason::Rejected && b.net.reject_reason() == net::RejectReason::Dropped);
            ASSERT_EQ(b.net.status_text(), std::string("You were dropped from the match."));
            ASSERT_TRUE(b.keys_forgotten.size() == 1 && net::key_matches(b.keys_forgotten[0].key, b.keys_given[0].key));
            ASSERT_TRUE(b.saw(NetGame::Event::Type::Failed) && b.count(NetGame::Event::Type::StartRequested) == 1);
            ASSERT_TRUE(w.server.scripted_hellos.size() == 2 && w.server.scripted_hellos[1].have_turns == 0 && net::key_matches(w.server.scripted_hellos[1].key, b.keys_given[0].key));      // (the new lobby starts from nothing, with the key)
            const uint32_t seen = w.server.accepted;
            w.run(5000);
            ASSERT_EQ(w.server.accepted, seen);                                          // nothing tries again
        }
        {   // a machine that has run no turn (its link is cut in the seconds of the start dialog) says Hello with no turns: BadRequest to that is not "ahead of the record", it is a refusal
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-4C"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-4C");
            Machine& b = w.join("Bob", "RJ-4C");
            ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Playing && b.net.phase() == NetGame::Phase::Playing; }, 12000));
            ASSERT_EQ(b.ticks, uint64_t{0});
            w.server.script_reason = net::RejectReason::BadRequest;
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 5000));
            ASSERT_TRUE(w.server.scripted_hellos.size() == 1 && !net::key_is_zero(w.server.scripted_hellos[0].key) && w.server.scripted_hellos[0].have_turns == 0);
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 3000));
            ASSERT_EQ(b.count(NetGame::Event::Type::StartRequested), size_t{1});         // (no new match was asked for)
        }
    } TEST_END();

    TEST_CASE("RJ1.5 Every Refusal On The Way Back Ends It With Its Own Words And Its Own Rule For The Key (The Seat Was Dropped, The Match Is Over, The Server Does Not Hold The Seat: The Key Goes; Taken Over By Another Window, The Server Would Not Take It Back Now: The Key Stays), Nothing Tries Again After It, Whether The Machine Came Back In Memory Or Started From Nothing With The Key") {
        struct Refusal {
            net::RejectReason reason;
            const char* text;
            bool forgets;
        };
        const std::vector<Refusal> refusals = {
            {net::RejectReason::Dropped, "You were dropped from the match.", true},
            {net::RejectReason::NoSuchRoom, "The match is over.", true},
            {net::RejectReason::MatchRunning, "The server does not hold your seat.", true},
            {net::RejectReason::Superseded, "This game was taken over by another window.", false},
            {net::RejectReason::RejoinFailed, "The server would not take you back now. Try Rejoin in a minute.", false},
            {net::RejectReason::Kicked, "You were dropped from the match.", true},
            {net::RejectReason::Full, "The room is full.", false},
            {net::RejectReason::VersionMismatch, "This version cannot play with the host's version.", false},
        };
        for (const Refusal& r : refusals) {
            ASSERT_EQ(NetGame::way_back_text(r.reason), std::string(r.text));
            ASSERT_TRUE(std::string(r.text).size() <= NetGame::kStatusNoticeChars);      // (the setup screen's status box holds two lines: the bound that a notice keeps)
            {   // in memory: the machine's link is cut and the door answers the Hello of its session with the reason
                World w;
                ASSERT_TRUE(w.server.start(w.now));
                ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-5"), w.server_now()).ok);
                Machine& a = w.join("Ann", "RJ-5");
                Machine& b = w.join("Bob", "RJ-5");
                ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
                ASSERT_TRUE(b.keys_given.size() == 1 && b.keys_forgotten.empty());
                w.server.script_reason = r.reason;
                w.server.door = Server::Door::Scripted;
                ASSERT_TRUE(w.server.cut_newest());
                ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 5000));
                w.run(10);
                ASSERT_EQ(b.net.status_text(), std::string(r.text));
                ASSERT_TRUE(b.saw(NetGame::Event::Type::HostLeft) && !b.saw(NetGame::Event::Type::Rejoined));
                ASSERT_EQ(b.keys_forgotten.size(), r.forgets ? size_t{1} : size_t{0});
                if (r.forgets) ASSERT_TRUE(net::key_matches(b.keys_forgotten[0].key, b.keys_given[0].key) && b.keys_forgotten[0].seat == b.keys_given[0].seat);
                ASSERT_EQ(w.server.scripted, 1u);
                ASSERT_EQ(b.count(NetGame::Event::Type::StartRequested), size_t{1});       // (no new match: only BadRequest gets one)
                w.server.door = Server::Door::Open;
                const uint32_t seen = w.server.accepted;
                w.run(5000);
                ASSERT_EQ(w.server.accepted, seen);                                      // nothing tries again (an attempt would come every two seconds)
                ASSERT_EQ(b.net.phase(), NetGame::Phase::Over);
                ASSERT_EQ(a.net.phase(), NetGame::Phase::Playing);                       // (the others wait for it: the real server was not told)
                b.net.leave();                                                           // the application's way back to its menu: the end of the way back decided about the key, this lets go of nothing more
                b.net.freeze();
                ASSERT_EQ(b.keys_forgotten.size(), r.forgets ? size_t{1} : size_t{0});       // (a key that was kept stays: the other window has the seat, or the player tries Rejoin in a minute)
                ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
            }
            {   // from nothing: a machine joins with the key and the server's door answers its Hello with the reason
                World w;
                ASSERT_TRUE(w.server.start(w.now));
                w.server.script_reason = r.reason;
                w.server.door = Server::Door::Scripted;
                net::SeatKey key{};
                for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7u + 3u);
                Machine& m = w.add_machine("Bob");
                ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 2, "RJ-5", "", key));
                ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Failed; }, 5000));
                ASSERT_TRUE(m.net.fail_reason() == NetGame::FailReason::Rejected && m.net.reject_reason() == r.reason);
                ASSERT_EQ(m.net.status_text(), std::string(r.text));
                ASSERT_TRUE(w.server.scripted_hellos.size() == 1 && net::key_matches(w.server.scripted_hellos[0].key, key) && w.server.scripted_hellos[0].have_turns == 0);        // (it starts from nothing)
                ASSERT_EQ(m.keys_forgotten.size(), r.forgets ? size_t{1} : size_t{0});
                if (r.forgets) ASSERT_TRUE(net::key_matches(m.keys_forgotten[0].key, key) && m.keys_forgotten[0].seat == 2 && m.keys_forgotten[0].room == "RJ-5");
                ASSERT_TRUE(m.keys_given.empty() && m.saw(NetGame::Event::Type::Failed));
                const uint32_t seen = w.server.accepted;
                w.run(5000);
                ASSERT_EQ(w.server.accepted, seen);
                m.net.leave();
                m.net.freeze();
                ASSERT_EQ(m.keys_forgotten.size(), r.forgets ? size_t{1} : size_t{0});
            }
        }
        {   // a machine that joins with its key and cannot reach the server: the key is good, and the application's way back to its menu keeps it
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            w.server.door = Server::Door::Refusing;
            net::SeatKey key{};
            for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7u + 3u);
            Machine& m = w.add_machine("Bob");
            ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 2, "RJ-5", "", key));
            ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Failed; }, 5000));
            ASSERT_TRUE(m.keys_forgotten.empty() && m.keys_given.empty());
            m.net.leave();
            ASSERT_TRUE(m.keys_forgotten.empty());                                       // (the player may use Rejoin when the server is back)
            ASSERT_EQ(m.net.phase(), NetGame::Phase::Off);
        }
        {   // a first join is told what it always was (the key makes the difference): the words of a join that has no seat to come back to
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            w.server.script_reason = net::RejectReason::MatchRunning;
            w.server.door = Server::Door::Scripted;
            Machine& m = w.add_machine("Bob");
            ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 255, "RJ-5"));
            ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Failed; }, 5000));
            ASSERT_EQ(m.net.status_text(), std::string("The match has already started."));
            ASSERT_TRUE(m.keys_forgotten.empty() && m.keys_given.empty());
        }
        {   // the real server: another window takes the seat with the key, the older one is told, keeps its key (the other window has it) and ends; the new window plays on
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-5S"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-5S");
            Machine& b = w.join("Bob", "RJ-5S");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            w.run(3000);
            const net::SeatKey key = b.keys_given[0].key;
            Machine& b2 = w.add_machine("Bob");
            ASSERT_TRUE(b2.net.join("127.0.0.1", w.server.port(), "Bob", b.net.my_seat(), "RJ-5S", "", key));
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 10000));
            ASSERT_EQ(b.net.status_text(), std::string("This game was taken over by another window."));
            ASSERT_TRUE(b.keys_forgotten.empty());
            ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Playing && !w.status("RJ-5S").paused && b2.count(NetGame::Event::Type::Rejoined) == 1; }, 30000));
            ASSERT_TRUE(b2.keys_forgotten.empty() && b2.keys_given.size() == 1);
            w.run(3000);
            a.quit();
            ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-5S") && a.sim.is_match_over() && b2.sim.is_match_over(); }, 20000));
            w.run(2000);
            ASSERT_TRUE(all_equal({&a, &b2}) && a.sim.state_hash().total == w.status("RJ-5S").referee_hash);
            ASSERT_TRUE(b2.keys_forgotten.size() == 1 && b.keys_forgotten.empty());        // the match is over: the new window lets go of the key
        }
    } TEST_END();

    TEST_CASE("RJ1.6 A Keyed Hello To A Public Room's Code Whose Match Is Gone (The Server Was Replaced By One With No Record), Block And All, Is Refused With NoSuchRoom And Makes No Room (The Review's M3): The Machine Says \"The match is over.\" And Lets Go Of The Key; The Same Hello Without A Key Makes The Room Again, And Its New Player Is Given No Key Until The Match Starts; What A Welcome Says Decides, By The Flag And The Key Alone") {
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.reconnect = true;
        limits.resume_countdown_ms = 0;
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        const std::string code = "rj6aaaaa";
        Machine& a = w.join_creating("Ann", code, block_of("", 2));
        Machine& b = w.join("Bob", code);
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        ASSERT_TRUE(b.keys_given.size() == 1 && a.keys_given.size() == 1);              // (each at the Start of the match: the room's Welcome left nothing to keep)
        const net::SeatKey old_key = b.keys_given[0].key;
        const uint16_t port = w.server.port();
        w.machines.clear();                                                              // the players' games are gone
        w.server.stop(w.now, false);                                                     // the server too, with no record of the match
        ASSERT_TRUE(w.server.start(w.now, 500, port));
        ASSERT_TRUE(w.server.report.items.empty() && w.server.report.queued == 0);
        ASSERT_EQ(w.server.mgr->room_count(), size_t{0});
        Machine& b2 = w.add_machine("Bob");
        b2.net.set_create(block_of("", 2));                                                // (a page's link carries the block and the key: the key wins)
        ASSERT_TRUE(b2.net.join("127.0.0.1", port, "Bob", 1, code, "", old_key));          // the Hello that shows the key of a seat of a match that is gone
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(b2.net.reject_reason() == net::RejectReason::NoSuchRoom);
        ASSERT_EQ(b2.net.status_text(), std::string("The match is over."));
        ASSERT_TRUE(b2.keys_forgotten.size() == 1 && net::key_matches(b2.keys_forgotten[0].key, old_key) && b2.keys_forgotten[0].room == code);
        ASSERT_TRUE(b2.keys_given.empty() && b2.saw(NetGame::Event::Type::Failed) && !b2.saw(NetGame::Event::Type::StartRequested));
        ASSERT_TRUE(w.server.mgr->room_count() == 0 && w.status(code).code.empty() && w.server.mgr->rooms_created() == 0);       // no room was made for a Hello that shows a key
        // the same Hello without the key is a new player's: with its block it makes the room, as it always did, and a waiting room gives it no key to keep
        Machine& b3 = w.join_creating("Bob", code, block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return b3.net.phase() == NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(w.status(code).state == RoomState::Waiting && w.status(code).joined == 1 && w.server.mgr->rooms_created() == 1);
        w.run(500);
        ASSERT_TRUE(b3.keys_given.empty() && b3.keys_forgotten.empty());               // (a visit to a waiting room leaves nothing behind: a closed tab would not have told anybody)
        // a second player comes: the match starts, and the key is kept now (the Start)
        Machine& c = w.join("Cat", code);
        ASSERT_TRUE(w.run_until([&]() { return w.running({&b3, &c}); }, 12000 + kPre));
        ASSERT_TRUE(b3.keys_given.size() == 1 && c.keys_given.size() == 1 && !net::key_matches(b3.keys_given[0].key, old_key) && !net::key_is_zero(b3.keys_given[0].key));
        ASSERT_TRUE(b3.count(NetGame::Event::Type::StartRequested) == 1 && b3.count(NetGame::Event::Type::Begun) == 1);
        for (const NetGame::Event& e : b3.events) ASSERT_FALSE(e.rejoin);               // it is no rejoin
        // what the Welcome says decides, by the flag and the key alone (scripted answers of a server that has the room as the case needs)
        struct Answer {
            const char* what;
            bool flag;
            int key_kind;                       // 0 no key, 1 the key that the Hello showed, 2 another key
            bool forgets;
            bool announces_at_welcome;          // a rejoin's key is announced at its Welcome ...
            bool announces_at_start;            // ... a new player's when the Start comes
            bool notice;
        };
        const std::vector<Answer> answers = {
            {"a rejoin: the flag and the same key", true, 1, false, true, false, false},
            {"the flag decides: a rejoin is the machine's own match whatever the key", true, 2, false, true, false, false},
            {"the seat taken back in a waiting room: no flag, the same key", false, 1, false, false, true, false},
            {"a new player of the room that took the code: no flag, another key", false, 2, true, false, true, true},
            {"a new player of a room that holds no seats: no flag, no key", false, 0, true, false, false, true},
        };
        uint64_t map_hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", map_hash));
        for (const Answer& answer : answers) {
            World s2;
            ASSERT_TRUE(s2.server.start(s2.now));
            net::SeatKey shown{};
            net::SeatKey other{};
            for (size_t i = 0; i < shown.size(); ++i) {
                shown[i] = static_cast<uint8_t>(i * 5u + 1u);
                other[i] = static_cast<uint8_t>(i * 11u + 9u);
            }
            net::WelcomeMsg welcome;
            welcome.player = 1;
            welcome.players = 4;
            welcome.key = answer.key_kind == 1 ? shown : (answer.key_kind == 2 ? other : net::SeatKey{});
            welcome.flags = answer.flag ? net::kWelcomeRejoin : uint8_t{0};
            s2.server.door = Server::Door::Scripted;
            s2.server.script_messages.push_back(net::encode(welcome));
            if (!answer.flag) {                                                          // (a waiting room goes on with the room; a running match with Start)
                net::RoomMsg room;
                room.you = 1;
                room.slots[0].state = net::SlotState::Client;
                room.slots[0].name = "Ann";
                room.slots[1].state = net::SlotState::Client;
                room.slots[1].name = "Bob";
                room.leader = 0;
                s2.server.script_messages.push_back(net::encode(room));
            }
            Machine& m = s2.add_machine("Bob");
            ASSERT_TRUE(m.net.join("127.0.0.1", s2.server.port(), "Bob", 1, "RJ-6", "", shown));
            ASSERT_TRUE(s2.run_until([&]() { return s2.server.scripted == 1; }, 5000));
            ASSERT_TRUE(s2.run_until([&]() { return answer.flag ? m.net.phase() == NetGame::Phase::Connecting && (m.keys_given.size() + m.keys_forgotten.size()) > 0 : m.net.phase() == NetGame::Phase::Room; }, 3000));
            ASSERT_EQ(m.keys_forgotten.size(), answer.forgets ? size_t{1} : size_t{0});
            ASSERT_EQ(m.keys_given.size(), answer.announces_at_welcome ? size_t{1} : size_t{0});
            if (answer.forgets) ASSERT_TRUE(net::key_matches(m.keys_forgotten[0].key, shown));
            if (answer.announces_at_welcome) ASSERT_TRUE(net::key_matches(m.keys_given[0].key, welcome.key) && m.keys_given[0].seat == 1);
            ASSERT_EQ(m.net.status_text() == "Your match has ended. This is a new room.", answer.notice);
            if (answer.flag) continue;
            net::StartMsg start;                                                         // the Start comes: the match has begun, the key of a new player is kept now
            start.seed = 8;
            start.map_name = "TINY.LVL";
            start.map_hash = map_hash;
            start.roster = 0x03;
            start.names[0] = "Ann";
            start.names[1] = "Bob";
            ASSERT_TRUE(s2.server.scripted_send(net::encode(start)));
            ASSERT_TRUE(s2.run_until([&]() { return s2.server.scripted_heard(net::MsgType::Loaded); }, 5000));
            ASSERT_EQ(m.keys_given.size(), answer.announces_at_start ? size_t{1} : size_t{0});
            if (answer.announces_at_start) ASSERT_TRUE(net::key_matches(m.keys_given[0].key, welcome.key) && m.keys_given[0].seat == 1 && m.keys_given[0].room == "RJ-6");
        }
    } TEST_END();

    TEST_CASE("RJ1.7 The Time To Wait Runs Out (A Room With A Short Cap: The Cap That The Server Named And A Minute After The Loss): The Machine Whose Network Stays Down Gives Up, Says So, Lets Go Of Its Key, And Nothing Tries Again; Until Then It Keeps Trying And Says How Long It Will") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        RoomSpec spec = held_spec("RJ-7");
        spec.max_pause_ms = 60000;                                                       // the shortest cap
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-7");
        Machine& b = w.join("Bob", "RJ-7");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        ASSERT_TRUE(w.server.cut_wire(1));                                               // a short outage of Bob's link: Ann is told what the cap is (Presence names it)
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-7").rejoins == 1 && !w.status("RJ-7").paused; }, 15000));
        w.run(1500);
        ASSERT_TRUE(a.net.pause_info().missing.empty() && a.net.phase() == NetGame::Phase::Playing);
        // now Ann's network is down for good
        w.server.door = Server::Door::Refusing;
        ASSERT_TRUE(w.server.cut_wire(0));
        const uint32_t cut_at = w.now;
        w.run(1000);
        net::PauseInfo pi = a.net.pause_info();
        ASSERT_TRUE(pi.reconnecting && pi.attempts >= 1 && pi.give_up_s > 90 && pi.give_up_s <= 120);        // the cap that remained (55 s) and a minute
        uint32_t last_left = pi.give_up_s;
        bool counts_down = true;
        for (uint32_t t = 1000; t < 60000; t += 1000) {
            w.run(1000);
            pi = a.net.pause_info();
            ASSERT_TRUE(pi.reconnecting && a.net.phase() == NetGame::Phase::Playing);   // it keeps trying all that time
            counts_down = counts_down && pi.give_up_s <= last_left && pi.away_s >= t / 1000 - 1;
            last_left = pi.give_up_s;
        }
        ASSERT_TRUE(counts_down && last_left < 70);
        ASSERT_EQ(w.status("RJ-7").drops_by_cap, 1u);                                    // the room dropped the seat at the cap, and Ann's machine knows nothing of it
        ASSERT_TRUE(w.server.refused >= 20);                                             // (an attempt every two seconds)
        ASSERT_TRUE(a.keys_forgotten.empty());
        ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Over; }, 90000));
        const uint32_t waited_ms = w.now - cut_at;
        ASSERT_TRUE(waited_ms >= 110000 && waited_ms <= 125000);                         // 55 s of cap that were left, and a minute
        w.run(10);
        ASSERT_EQ(a.net.status_text(), std::string("The match could not wait any longer."));
        ASSERT_TRUE(a.saw(NetGame::Event::Type::HostLeft) && !a.saw(NetGame::Event::Type::Rejoined));
        ASSERT_TRUE(a.keys_forgotten.size() == 1 && net::key_matches(a.keys_forgotten[0].key, a.keys_given[0].key));
        ASSERT_FALSE(a.net.pause_info().reconnecting);
        w.server.door = Server::Door::Open;
        const uint32_t seen_accepted = w.server.accepted;
        const uint32_t seen_refused = w.server.refused;
        w.run(10000);
        ASSERT_TRUE(w.server.accepted == seen_accepted && w.server.refused == seen_refused);        // nothing tries again
        a.net.leave();                                                                   // the application's way back to its menu: the key was let go of when the time ran out, and is not again
        ASSERT_EQ(a.keys_forgotten.size(), size_t{1});
    } TEST_END();

    TEST_CASE("RJ1.8 The Vote Of The Others, Through The NetGame: After Five Seconds Away The Screens Show It (Who, How Many Vote, This Machine's Own Choice), One Of Two Voters Is Not Enough, The Last Choice Of A Player Counts, Two Of Two Drop The Seat, The Machine That Was Away Is Told That It Was Dropped, And The Others Go On Without It And End In The Same State") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        RoomSpec spec = held_spec("RJ-8", 3);
        spec.vote_after_ms = 5000;
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-8");
        Machine& b = w.join("Bob", "RJ-8");
        Machine& c = w.join("Cat", "RJ-8");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b, &c}); }, 12000 + kPre));
        ASSERT_TRUE(!a.net.pause_info().vote_open && !a.net.vote(true) && !a.net.vote(false));      // no vote is open: nothing is sent
        const uint8_t cat = c.net.my_seat();
        w.server.door = Server::Door::Refusing;                                           // Cat's network goes down
        ASSERT_TRUE(w.server.cut_newest());
        ASSERT_TRUE(w.run_until([&]() { return a.net.pause_info().vote_open && b.net.pause_info().vote_open; }, 15000));
        net::PauseInfo ai = a.net.pause_info();
        ASSERT_TRUE(ai.vote_seat == cat && ai.vote_name == "Cat" && ai.voters == 2 && ai.votes_continue == 0 && ai.my_vote == net::PauseInfo::Choice::None);
        ASSERT_TRUE(ai.missing.size() == 1 && ai.missing[0].seat == cat && ai.missing[0].away_s >= 5 && !ai.missing[0].catching_up);
        ASSERT_FALSE(c.net.vote(false));                                                 // the machine that is away follows no match: it cannot vote
        ASSERT_TRUE(b.net.vote(false));                                                  // Bob: go on without Cat. One of two voters is not more than half
        ASSERT_TRUE(w.run_until([&]() { return a.net.pause_info().votes_continue == 1 && b.net.pause_info().my_vote == net::PauseInfo::Choice::Continue; }, 2000));          // (what each machine is told reaches it on its own link)
        ASSERT_TRUE(b.net.pause_info().my_vote == net::PauseInfo::Choice::Continue && a.net.pause_info().my_vote == net::PauseInfo::Choice::None);
        w.run(500);
        ASSERT_TRUE(w.status("RJ-8").paused && w.status("RJ-8").drops_by_vote == 0 && a.net.paused());
        ASSERT_TRUE(a.net.vote(true));                                                   // Ann: keep waiting (the last choice of a player counts)
        w.run(300);
        ai = a.net.pause_info();
        ASSERT_TRUE(ai.my_vote == net::PauseInfo::Choice::KeepWaiting && ai.votes_continue == 1 && w.status("RJ-8").drops_by_vote == 0);
        ASSERT_TRUE(a.net.vote(false));                                                  // ... and she changes her mind
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-8").drops_by_vote == 1; }, 3000));
        // Cat's network is back: its next attempt is told that the seat was dropped
        w.server.door = Server::Door::Open;
        ASSERT_TRUE(w.run_until([&]() { return c.net.phase() == NetGame::Phase::Over; }, 15000));
        w.run(10);
        ASSERT_EQ(c.net.status_text(), std::string("You were dropped from the match."));
        ASSERT_TRUE(c.keys_forgotten.size() == 1 && net::key_matches(c.keys_forgotten[0].key, c.keys_given[0].key));
        ASSERT_TRUE(c.saw(NetGame::Event::Type::HostLeft) && !c.saw(NetGame::Event::Type::Rejoined));
        // the others go on without it
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-8").paused; }, 8000));
        w.run(1000);
        ASSERT_TRUE(!a.net.paused() && !b.net.paused() && !a.net.pause_info().vote_open && a.net.pause_info().missing.empty());
        for (Machine* m : {&a, &b}) {
            bool left = false;
            for (const NetGame::Event& e : m->events) left = left || (e.type == NetGame::Event::Type::PlayerLeft && e.seat == cat);
            ASSERT_TRUE(left);                                                           // Cat's Drop was applied at the same tick on both
        }
        const uint32_t sealed = w.status("RJ-8").turns;
        w.run(3000);
        ASSERT_TRUE(w.status("RJ-8").turns > sealed + 40);
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-8") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-8");
        ASSERT_TRUE(end.state == RoomState::Finished && end.drops_by_vote == 1 && end.referee_hash != 0);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
    } TEST_END();

    TEST_CASE("RJ1.9 The Prediction Of One's Own Orders Is Not Run While The Way Back Runs (Reconnecting, The Hello, The Catch-Up) Or While The Match Is Held (A Seat Missing, The Countdown After A Pause), And Comes Back After It: Both Machines Predict Again From The Confirmed Engine And End In The Same State") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        RoomSpec spec = held_spec("RJ-9");
        spec.resume_countdown_ms = 3000;                                                 // a pause of 3 s or more is followed by a countdown, in which nothing is sealed
        ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-9");
        Machine& b = w.join("Bob", "RJ-9");
        a.net.set_prediction_enabled(true);
        b.net.set_prediction_enabled(true);
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}) && a.net.predicting() && b.net.predicting(); }, 15000 + kPre));
        w.run(1500);
        ASSERT_TRUE(a.net.predicting() && b.net.predicting() && &a.net.view_engine() != &a.sim && &b.net.view_engine() != &b.sim);
        ASSERT_TRUE(a.net.prediction() != nullptr && b.net.prediction() != nullptr);
        const uint64_t a_starts = a.net.prediction()->stats().starts;
        const uint64_t b_starts = b.net.prediction()->stats().starts;
        const uint32_t before_ticks = w.status("RJ-9").ticks;
        w.server.door = Server::Door::Refusing;                                           // Bob's network is down for four seconds
        ASSERT_TRUE(w.server.cut_newest());
        uint32_t paused_ms = 0;                                                          // how long the server has said that the match is held (a Presence takes a pass or two to arrive)
        uint32_t away_ms = 0;
        for (; away_ms < 4000; away_ms += 10) {
            w.run(10);
            if (away_ms >= 50) {
                ASSERT_FALSE(b.net.predicting());                                        // the machine knows that its link is gone: the confirmed engine is shown
                ASSERT_TRUE(&b.net.view_engine() == &b.sim);
            }
            paused_ms = w.status("RJ-9").paused ? paused_ms + 10 : 0;
            if (paused_ms >= 200) {
                ASSERT_FALSE(a.net.predicting());                                        // the others are told that a seat is missing: their runner is held
                ASSERT_TRUE(&a.net.view_engine() == &a.sim);
            }
        }
        ASSERT_TRUE(w.status("RJ-9").paused && a.net.prediction()->suspended() && b.net.prediction()->suspended());
        w.server.door = Server::Door::Open;                                               // the network is back: Bob's next attempt is let in
        bool seen_countdown = false;
        bool countdown_on_screens = false;
        for (uint32_t waited = 0; waited < 30000; waited += 10) {
            w.run(10);
            const RoomStatus st = w.status("RJ-9");
            seen_countdown = seen_countdown || st.resume_s > 0;
            countdown_on_screens = countdown_on_screens || (st.resume_s > 0 && a.net.pause_info().resume_seconds_left > 0 && a.net.paused() && b.net.pause_info().resume_seconds_left > 0 && b.net.paused());
            paused_ms = st.paused ? paused_ms + 10 : 0;
            if (st.paused && st.rejoins == 0) ASSERT_FALSE(b.net.predicting());          // the way back
            if (paused_ms >= 200) ASSERT_FALSE(a.net.predicting() || b.net.predicting());   // the match is held, whoever is back already
            if (!st.paused) break;
        }
        ASSERT_TRUE(seen_countdown && countdown_on_screens);                             // (the countdown after the pause was part of it, and both machines' screens were told: the match goes on in N seconds)
        ASSERT_FALSE(w.status("RJ-9").paused);
        ASSERT_TRUE(w.run_until([&]() { return a.net.predicting() && b.net.predicting(); }, 3000));      // the match runs again: both predict again
        ASSERT_EQ(a.net.prediction()->stats().starts, a_starts + 1);                     // one new beginning each, from the confirmed engine
        ASSERT_EQ(b.net.prediction()->stats().starts, b_starts + 1);
        ASSERT_TRUE(w.status("RJ-9").ticks > before_ticks);
        w.run(3000);
        ASSERT_TRUE(a.net.predicting() && b.net.predicting() && a.net.view_engine().current_tick() > a.sim.current_tick());
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-9") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-9");
        ASSERT_TRUE(end.state == RoomState::Finished && end.rejoins == 1);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
    } TEST_END();

    TEST_CASE("RJ1.10 A Match That Cannot Come Back Is What It Was: A Room On The Local Network (Host Migration: The Host Leaves, The Guest Takes Over), And A Server's Room That Does Not Hold Seats (No Key, A Cut Link Ends The Match For That Machine At Once, And Nothing Tries To Come Back)") {
        {   // a room on the local network
            World w;
            Machine& host = w.add_machine("Alice");
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            Machine& guest = w.add_machine("Bob");
            ASSERT_TRUE(guest.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
            ASSERT_TRUE(w.run_until([&]() { return host.net.can_start(); }, 8000));
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(8, hash));
            ASSERT_TRUE(w.run_until([&]() { return w.running({&host, &guest}); }, 12000 + kPre));
            w.run(2000);
            host.net.leave();                                                            // the host's machine is gone
            ASSERT_TRUE(w.run_until([&]() { return guest.saw(NetGame::Event::Type::HostChanged); }, 8000));
            ASSERT_TRUE(guest.net.is_host());                                            // the guest took over: the way of a game on the local network
            ASSERT_EQ(guest.net.phase(), NetGame::Phase::Playing);
            w.run(3000);
            ASSERT_EQ(guest.net.phase(), NetGame::Phase::Playing);
            ASSERT_FALSE(guest.saw(NetGame::Event::Type::HostLeft));
        }
        {   // a room that has a Host slot, the way of a game on the local network, and hands out a key all the same: the host is a player there, so a lost link is host migration, and no way back is made
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            net::SeatKey key{};
            for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 13u + 5u);
            net::WelcomeMsg welcome;
            welcome.player = 1;
            welcome.players = sim::MAX_PLAYERS;
            welcome.key = key;
            net::RoomMsg room;
            room.you = 1;
            room.map_name = "TINY.LVL";
            room.slots[0].state = net::SlotState::Host;
            room.slots[0].name = "Ann";
            room.slots[1].state = net::SlotState::Client;
            room.slots[1].name = "Bob";
            w.server.door = Server::Door::Scripted;
            w.server.script_messages = {net::encode(welcome), net::encode(room)};
            Machine& m = w.add_machine("Bob");
            m.orders = false;
            ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 1, "RJ-10L"));
            ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Room; }, 5000));
            ASSERT_TRUE(m.keys_given.empty());                                           // (the room's Welcome handed out a key: it is kept when the Start comes)
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            net::StartMsg start;
            start.seed = 8;
            start.map_name = "TINY.LVL";
            start.map_hash = hash;
            start.roster = 0x03;
            start.names[0] = "Ann";
            start.names[1] = "Bob";
            ASSERT_TRUE(w.server.scripted_send(net::encode(start)));
            ASSERT_TRUE(w.run_until([&]() { return w.server.scripted_heard(net::MsgType::Loaded); }, 5000));        // the machine loaded the map
            ASSERT_TRUE(m.keys_given.size() == 1 && net::key_matches(m.keys_given[0].key, key));        // (the key was handed out at the Start)
            ASSERT_TRUE(w.server.scripted_send(net::encode_begin()));
            ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Playing; }, 5000));
            ASSERT_TRUE(w.server.scripted_send(net::encode(net::RejectMsg{net::RejectReason::Superseded})));      // a host's Reject is not a refusal of a way back here: a guest of a game on the local network ignores it
            w.run(300);
            ASSERT_TRUE(m.net.phase() == NetGame::Phase::Playing && !m.saw(NetGame::Event::Type::HostLeft));
            ASSERT_TRUE(w.server.scripted_cut());                                        // the host's link is gone
            ASSERT_TRUE(w.run_until([&]() { return m.saw(NetGame::Event::Type::HostChanged); }, 15000));
            ASSERT_TRUE(m.net.is_host() && !m.net.pause_info().reconnecting && m.net.phase() == NetGame::Phase::Playing);       // the guest took over: the way of a game on the local network
            ASSERT_FALSE(m.saw(NetGame::Event::Type::HostLeft) || m.saw(NetGame::Event::Type::Rejoined));
            ASSERT_EQ(w.server.accepted, 1u);                                            // nothing tried to come back
        }
        {   // a server's room that does not hold seats: no key, no way back
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            RoomSpec spec = held_spec("RJ-10");
            spec.reconnect = false;
            ASSERT_TRUE(w.server.mgr->create_room(spec, w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-10");
            Machine& b = w.join("Bob", "RJ-10");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            ASSERT_EQ(w.server.accepted, 2u);
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 3000));      // the match is over for this machine, as it always was
            ASSERT_EQ(b.net.status_text(), net::match_lost_text(net::ClientSession::LostReason::Connection));
            ASSERT_TRUE(b.saw(NetGame::Event::Type::HostLeft));
            w.run(8000);
            ASSERT_EQ(w.server.accepted, 2u);                                            // nothing tried to come back
            const RoomStatus s = w.status("RJ-10");
            ASSERT_TRUE(!s.paused && s.absent.empty() && s.rejoins == 0);                // the room dropped the seat at once
        }
    } TEST_END();

    TEST_CASE("RJ1.11 A Link That Cannot Be Made (The Name Does Not Resolve, The Network Is Down) Is Told To The Session: The Next Attempt Is Two Seconds Later And Not At The Next Frame (A Lookup Every Frame Would Freeze The Game), And The First Link That Can Be Made Brings The Machine Back; The Same For The Link Of A New Start From Nothing") {
        {   // the link of the new start cannot be made (a server that answered BadRequest, a network that went down at that moment): the machine is where a join that cannot connect is, and keeps its key
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-11B"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-11B");
            Machine& b = w.join("Bob", "RJ-11B");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            int calls = 0;
            const uint16_t port = w.server.port();
            b.net.set_link_maker_for_test([&]() -> std::unique_ptr<net::Connection> {
                ++calls;
                if (calls > 1) return nullptr;                                           // the way back's own link reaches the server, the one for the new lobby cannot be made
                return net::TcpConnection::connect("127.0.0.1", port);
            });
            w.server.script_reason = net::RejectReason::BadRequest;
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Failed; }, 5000));
            ASSERT_EQ(calls, 2);
            ASSERT_TRUE(b.net.fail_reason() == NetGame::FailReason::Unreachable && b.saw(NetGame::Event::Type::Failed));
            ASSERT_EQ(b.net.status_text(), std::string(sim::strings::text(sim::strings::kUnableToConnect)));
            ASSERT_TRUE(b.keys_forgotten.empty());                                       // (the key is good: the player may use Rejoin when the network is back)
            ASSERT_FALSE(b.net.pause_info().reconnecting);
            w.run(6000);
            ASSERT_EQ(calls, 2);                                                         // nothing tries again
            b.net.leave();                                                               // the application's way back to its menu keeps the key too: the session ended by itself, the key is good
            ASSERT_TRUE(b.keys_forgotten.empty());
        }
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-11"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-11");
        Machine& b = w.join("Bob", "RJ-11");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        int calls = 0;
        bool can_connect = false;
        const uint16_t port = w.server.port();
        b.net.set_link_maker_for_test([&]() -> std::unique_ptr<net::Connection> {
            ++calls;
            if (!can_connect) return nullptr;
            return net::TcpConnection::connect("127.0.0.1", port);
        });
        ASSERT_TRUE(w.server.cut_newest());
        w.run(1000);
        ASSERT_EQ(calls, 1);                                                             // the first attempt is at once
        w.run(1500);
        ASSERT_EQ(calls, 2);                                                             // the next one two seconds after it began
        w.run(2000);
        ASSERT_EQ(calls, 3);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        ASSERT_EQ(w.server.accepted, 2u);                                                // (nothing reached the server)
        can_connect = true;
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-11").paused; }, 15000));
        ASSERT_TRUE(calls >= 4 && calls <= 5);
        ASSERT_TRUE(w.status("RJ-11").rejoins == 1 && b.net.phase() == NetGame::Phase::Playing);
        w.run(2000);
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-11") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == w.status("RJ-11").referee_hash);
    } TEST_END();

    TEST_CASE("RJ1.12 A Page That The Browser Did Not Wake Hands Its Gap To The Session In Every Phase Of The Way Back (The Time Of The Way Back Is Real Time: The Attempts, And The Give-Up After The Cap And A Minute)") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-12"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-12");
        Machine& b = w.join("Bob", "RJ-12");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        w.server.door = Server::Door::Refusing;                                           // Bob cannot reach the server
        ASSERT_TRUE(w.server.cut_newest());
        w.run(3000);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        const uint32_t tries = w.server.refused;
        ASSERT_TRUE(tries >= 1);                                                         // it is trying
        b.net.note_gap(10000);                                                           // ten seconds that its clock did not count: an attempt is due at once, nothing is given up
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        w.run(100);
        ASSERT_TRUE(w.server.refused > tries);
        b.net.note_gap(32u * 60u * 1000u);                                               // half an hour that its clock did not count: the way back is over (the cap and a minute)
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Over);
        w.run(10);                                                                       // (the next frame takes its events)
        ASSERT_TRUE(b.saw(NetGame::Event::Type::HostLeft));
        ASSERT_EQ(b.net.status_text(), std::string("The match could not wait any longer."));
        ASSERT_TRUE(b.keys_forgotten.size() == 1);
        const uint32_t seen = w.server.accepted;
        w.run(5000);
        ASSERT_EQ(w.server.accepted, seen);                                              // and nothing tries again
    } TEST_END();

    TEST_CASE("RJ1.13 leave() Works In Every State Of The Way Back: While The Match Is Held For A Seat That Is Away (No Quit Command Would Be Sealed: The Leave Message Drops The Seat, The Others Are Told At The Same Tick), While The Machine Has No Link (It Ends The Attempts), And While It Catches Up (The Server Is Told And Drops The Seat); The Key Is Let Go Of Each Time") {
        {   // the match is held for Cat, and Bob quits
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-13A", 3), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-13A");
            Machine& b = w.join("Bob", "RJ-13A");
            Machine& c = w.join("Cat", "RJ-13A");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b, &c}); }, 12000 + kPre));
            const uint8_t bob = b.net.my_seat();
            w.server.door = Server::Door::Refusing;
            ASSERT_TRUE(w.server.cut_newest());                                          // Cat's network is down
            ASSERT_TRUE(w.run_until([&]() { return b.net.paused() && a.net.paused(); }, 5000));
            const uint32_t ticks_held = w.status("RJ-13A").ticks;
            b.net.leave();                                                               // Bob quits while nothing can be sealed
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
            ASSERT_TRUE(b.keys_forgotten.size() == 1 && net::key_matches(b.keys_forgotten[0].key, b.keys_given[0].key));
            w.run(1000);
            ASSERT_TRUE(w.status("RJ-13A").paused && w.status("RJ-13A").ticks <= ticks_held + 2);        // (still held for Cat)
            w.server.door = Server::Door::Open;                                          // Cat is back
            ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-13A").paused; }, 20000));
            w.run(500);
            for (Machine* m : {&a, &c}) {
                bool left = false;
                for (const NetGame::Event& e : m->events) left = left || (e.type == NetGame::Event::Type::PlayerLeft && e.seat == bob);
                ASSERT_TRUE(left);                                                       // the Leave reached the room: Bob's Drop came with the first turn after the pause
            }
            ASSERT_TRUE(w.status("RJ-13A").state == RoomState::Running && w.status("RJ-13A").rejoins == 1);
        }
        {   // the machine has no link and leaves: the attempts end
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-13B"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-13B");
            Machine& b = w.join("Bob", "RJ-13B");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            w.server.door = Server::Door::Refusing;
            ASSERT_TRUE(w.server.cut_newest());
            w.run(5000);
            ASSERT_TRUE(b.net.pause_info().reconnecting && w.server.refused >= 2);
            b.net.leave();
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
            ASSERT_FALSE(b.net.pause_info().reconnecting);
            ASSERT_TRUE(b.keys_forgotten.size() == 1);
            const uint32_t seen = w.server.refused + w.server.accepted;
            w.run(10000);
            ASSERT_EQ(w.server.refused + w.server.accepted, seen);                       // nothing tries any more
            ASSERT_TRUE(w.status("RJ-13B").paused && a.net.phase() == NetGame::Phase::Playing);       // (the server cannot be told: the seat is held until the others vote or the cap)
        }
        {   // the machine catches up and leaves
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-13C", 3), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-13C");
            Machine& b = w.join("Bob", "RJ-13C");
            Machine& c = w.join("Cat", "RJ-13C");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b, &c}); }, 12000 + kPre));
            w.run(10000);
            const uint8_t cat = c.net.my_seat();
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return c.net.pause_info().catching_up; }, 8000));
            ASSERT_TRUE(c.net.paused());
            c.net.leave();                                                               // in the middle of the catch-up: the server is told
            ASSERT_EQ(c.net.phase(), NetGame::Phase::Off);
            ASSERT_TRUE(c.keys_forgotten.size() == 1 && c.count(NetGame::Event::Type::Rejoined) == 0);
            // Cat's CaughtUp is usually out already when the test sees it catch up (the stream is short) and the Leave follows it: the server must read both in one pass, so that the Leave ends the attempt before the states are compared.
            // A Mac's loopback can hand it the CaughtUp a pass earlier: the seat is given back, then a send to the closed link holds it as lost (the wait below fails) or the Leave drops it with a rejoin counted (the last check fails).
            std::this_thread::sleep_for(std::chrono::milliseconds(50));                  // the kernel delivers both before the server's next pass
            ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-13C").paused; }, 20000));      // the seat is dropped, so nobody waits for it
            w.run(500);
            for (Machine* m : {&a, &b}) {
                bool left = false;
                for (const NetGame::Event& e : m->events) left = left || (e.type == NetGame::Event::Type::PlayerLeft && e.seat == cat);
                ASSERT_TRUE(left);
            }
            ASSERT_TRUE(w.status("RJ-13C").absent.empty() && w.status("RJ-13C").rejoins == 0);
        }
    } TEST_END();

    TEST_CASE("RJ1.14 What The Server Is Told By A Machine That Comes Back: The Hello Of The Way Back Has The Name As The Lobby Says It (Printable, At Most 32 Characters: A Name That Is Not Would Not Decode At The Server), The Room, The Token, The Machine's Own Seat (Not The One It Asked For), Its Key And The Turns It Has") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-14"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-14", 1);                                          // Ann asks for seat 1 and has it
        const std::string odd = std::string("B\xC3\xA9") + "b\x01 " + std::string(40, 'x');      // not printable ASCII, and longer than a name may be
        const uint8_t bobs_platform = static_cast<uint8_t>(net::kPlatformBrowser | net::kOsLinux);
        const net::CreateBlock bobs_block = block_of("", 2);                             // (Bob's link carries a block: the room is there already, so the server ignores it; the way back must not carry it)
        Machine& b = w.join(odd, "RJ-14", 1, "tok-14", &bobs_block, bobs_platform);      // Bob asks for the same seat: the first free one is his (seat 0)
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        ASSERT_TRUE(a.net.my_seat() == 1 && b.net.my_seat() == 0);
        const net::JoinTarget& target = b.net.join_target();                             // how the first link was made: what the way back makes its links from
        ASSERT_TRUE(target.address == "127.0.0.1" && target.port == w.server.port() && target.url.empty() && target.name == odd && target.want_seat == 1);
        ASSERT_TRUE(target.room == "RJ-14" && target.token == "tok-14");
        w.server.script_reason = net::RejectReason::RejoinFailed;
        w.server.door = Server::Door::Scripted;                                          // the door shows what Bob's new link says
        ASSERT_TRUE(w.server.cut_newest());
        ASSERT_TRUE(w.run_until([&]() { return w.server.scripted == 1; }, 5000));
        ASSERT_EQ(w.server.scripted_hellos.size(), size_t{1});                           // (a Hello that does not decode is not in the list: a real server would say BadRequest to it)
        const net::HelloMsg hello = w.server.scripted_hellos[0];
        ASSERT_EQ(hello.name, "Bb " + std::string(29, 'x'));                             // what the lobby's Hello said: printable, 32 characters
        ASSERT_EQ(hello.room, std::string("RJ-14"));
        ASSERT_EQ(hello.token, std::string("tok-14"));
        ASSERT_EQ(hello.platform, bobs_platform);                                        // (protocol 15: what the machine runs on goes with it on the way back, as it went the first time)
        ASSERT_FALSE(hello.create.has_value());                                          // (and a Hello with a key never makes a room: no create block, though the first link of this machine had one)
        ASSERT_EQ(hello.want_seat, uint8_t{0});                                          // its own seat: the key decides, but this is what anybody's Hello says
        ASSERT_TRUE(net::key_matches(hello.key, b.keys_given[0].key) && hello.have_turns > 0 && hello.version == net::kProtocolVersion);
        ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 3000));
        ASSERT_EQ(b.count(NetGame::Event::Type::StartRequested), size_t{1});             // (it was refused for what it is: nothing made it start again from nothing)
        b.net.leave();
        ASSERT_TRUE(b.net.join_target().address.empty() && b.net.join_target().token.empty() && b.net.join_target().room.empty());      // (nothing of the old link, the token least of all, is kept after the player left)
        {   // the place that keeps a key is told where the room is as a person writes it: an IPv6 address is in brackets (a machine without an IPv6 loopback skips this)
            Machine m("Bob");
            net::SeatKey key{};
            for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 5u + 9u);
            if (m.net.join("::1", 9, "Bob", 2, "RJ-14", "", key)) {
                m.net.leave();
                ASSERT_TRUE(m.keys_forgotten.size() == 1 && m.keys_forgotten[0].server == "[::1]:9" && m.keys_forgotten[0].room == "RJ-14" && m.keys_forgotten[0].seat == 2);
            }
        }
    } TEST_END();

    TEST_CASE("RJ1.15 The Functions That Are Told About The Key May End The Session From Inside The Call (leave()): At The Start Of The Match, At The Welcome Of A Rejoin, When The Key Is Let Go Of At The End Of The Match, And When A Refusal Lets Go Of It In Memory And From Nothing; Nothing That Is Gone Is Touched After It") {
        {   // at the Start of the match (the room's Welcome gave a key and kept none)
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-15"), w.server_now()).ok);
            Machine& a = w.add_machine("Ann");
            a.leave_on_key = 1;
            ASSERT_TRUE(a.net.join("127.0.0.1", w.server.port(), "Ann", 255, "RJ-15"));
            ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Room; }, 5000));
            w.run(300);
            ASSERT_TRUE(a.keys_given.empty() && a.keys_forgotten.empty());
            Machine& other = w.join("Bob", "RJ-15");
            (void)other;
            ASSERT_TRUE(w.run_until([&]() { return !a.keys_given.empty(); }, 8000));
            ASSERT_EQ(a.net.phase(), NetGame::Phase::Off);
            ASSERT_EQ(a.keys_forgotten.size(), size_t{1});                               // (leaving lets go of the key)
            w.run(500);
            ASSERT_EQ(a.net.phase(), NetGame::Phase::Off);
        }
        {   // at the Welcome of a rejoin: the machine is back and leaves at that very moment
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-15B"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-15B");
            Machine& b = w.join("Bob", "RJ-15B");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            b.leave_on_key = 2;                                                          // (the first call was the Start of the match)
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return b.keys_given.size() == 2; }, 10000));
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
            ASSERT_EQ(b.keys_forgotten.size(), size_t{1});
            w.run(1000);
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
        }
        {   // when the key is let go of at the end of the match
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-15C"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-15C");
            Machine& b = w.join("Bob", "RJ-15C");
            a.leave_on_forget = true;
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            w.run(1000);
            b.quit();
            ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Off; }, 20000));
            ASSERT_EQ(a.keys_forgotten.size(), size_t{1});
            w.run(500);
            ASSERT_EQ(a.net.phase(), NetGame::Phase::Off);
        }
        {   // when a refusal lets go of it, in memory
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-15D"), w.server_now()).ok);
            Machine& a = w.join("Ann", "RJ-15D");
            Machine& b = w.join("Bob", "RJ-15D");
            ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
            b.leave_on_forget = true;
            w.server.script_reason = net::RejectReason::Dropped;
            w.server.door = Server::Door::Scripted;
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return !b.keys_forgotten.empty(); }, 5000));
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
            ASSERT_EQ(b.keys_forgotten.size(), size_t{1});
            w.run(500);
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Off);
        }
        {   // ... and from nothing (the machine joined with the key)
            World w;
            ASSERT_TRUE(w.server.start(w.now));
            w.server.script_reason = net::RejectReason::NoSuchRoom;
            w.server.door = Server::Door::Scripted;
            net::SeatKey key{};
            for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7u + 3u);
            Machine& m = w.add_machine("Bob");
            m.leave_on_forget = true;
            ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 2, "RJ-15E", "", key));
            ASSERT_TRUE(w.run_until([&]() { return !m.keys_forgotten.empty(); }, 5000));
            ASSERT_EQ(m.net.phase(), NetGame::Phase::Off);
            ASSERT_EQ(m.keys_forgotten.size(), size_t{1});
            w.run(500);
            ASSERT_EQ(m.net.phase(), NetGame::Phase::Off);
        }
    } TEST_END();

    TEST_CASE("RJ1.16 What The Screens Are Told Is What The Server Said, Field By Field (A Scripted Server's Presence Messages): The Seats That Are Missing With Their Names, Seconds And Progress, The Vote With Its Counts And This Machine's Own Choice, The Countdown; paused() Follows The Server's Hold; vote() Sends The Choice For The Seat The Vote Is About, And Nothing When No Vote Is Open") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        net::SeatKey key{};
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 11u + 2u);
        net::WelcomeMsg welcome;                                                         // a dedicated server's room: no seat is the host's, and the Welcome gives a key
        welcome.player = 1;
        welcome.players = sim::MAX_PLAYERS;
        welcome.key = key;
        net::RoomMsg room;
        room.you = 1;
        room.leader = 0;
        room.map_name = "TINY.LVL";
        const char* const names[3] = {"Ann", "Bob", "Cat"};
        for (size_t s = 0; s < 3; ++s) {
            room.slots[s].state = net::SlotState::Client;
            room.slots[s].name = names[s];
        }
        w.server.door = Server::Door::Scripted;
        w.server.script_messages = {net::encode(welcome), net::encode(room)};
        Machine& m = w.add_machine("Bob");
        m.orders = false;
        ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 1, "RJ-16"));
        ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Room; }, 5000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        net::StartMsg start;
        start.seed = 8;
        start.map_name = "TINY.LVL";
        start.map_hash = hash;
        start.roster = 0x07;
        for (size_t s = 0; s < 3; ++s) start.names[s] = names[s];
        ASSERT_TRUE(w.server.scripted_send(net::encode(start)));
        ASSERT_TRUE(w.run_until([&]() { return w.server.scripted_heard(net::MsgType::Loaded); }, 5000));
        ASSERT_TRUE(w.server.scripted_send(net::encode_begin()));
        ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Playing; }, 5000));
        ASSERT_TRUE(!m.net.paused() && m.net.my_seat() == 1);
        net::PauseInfo pi = m.net.pause_info();
        ASSERT_TRUE(!pi.reconnecting && !pi.catching_up && pi.missing.empty() && !pi.vote_open && pi.resume_seconds_left == 0);
        const auto votes_sent = [&]() {                                                  // the votes that this machine has said, in order (true: continue without the seat)
            std::vector<std::pair<uint8_t, bool>> out;
            for (const std::vector<uint8_t>& message : w.server.scripted_inbox) {
                net::VoteMsg v;
                if (net::peek_type(message) == net::MsgType::Vote && net::decode(message.data(), message.size(), v)) out.emplace_back(v.seat, v.continue_without);
            }
            return out;
        };
        const auto say = [&](const net::PresenceMsg& presence, const std::function<bool()>& read) {      // the server's word, and the moment when the machine has read it
            return w.server.scripted_send(net::encode(presence)) && w.run_until(read, 3000);
        };
        ASSERT_TRUE(!m.net.vote(true) && !m.net.vote(false) && votes_sent().empty());      // no vote is open: nothing is sent

        net::PresenceMsg p;                                                              // Cat has been away for 40 s; Ann is back and is being given the match (60 percent); a vote about Cat
        p.missing.push_back(net::PresenceMsg::Entry{2, net::PresenceMsg::State::Absent, 40, 0});
        p.missing.push_back(net::PresenceMsg::Entry{0, net::PresenceMsg::State::CatchingUp, 12, 60});
        p.vote_seat = 2;
        p.votes_continue = 1;
        p.voters = 2;
        p.your_vote = 2;
        p.cap_s = 1500;
        ASSERT_TRUE(say(p, [&]() { return m.net.pause_info().missing.size() == 2; }));
        pi = m.net.pause_info();
        ASSERT_TRUE(m.net.paused() && !pi.reconnecting && !pi.catching_up && pi.away_s == 0 && pi.attempts == 0 && pi.give_up_s == 0 && pi.catch_up_percent == 0);
        ASSERT_EQ(pi.missing.size(), size_t{2});
        ASSERT_TRUE(pi.missing[0].seat == 2 && pi.missing[0].name == "Cat" && pi.missing[0].away_s == 40 && !pi.missing[0].catching_up && pi.missing[0].progress == 0);
        ASSERT_TRUE(pi.missing[1].seat == 0 && pi.missing[1].name == "Ann" && pi.missing[1].away_s == 12 && pi.missing[1].catching_up && pi.missing[1].progress == 60);
        ASSERT_TRUE(pi.vote_open && pi.vote_seat == 2 && pi.vote_name == "Cat" && pi.votes_continue == 1 && pi.voters == 2 && pi.my_vote == net::PauseInfo::Choice::Continue);
        ASSERT_EQ(static_cast<unsigned>(pi.resume_seconds_left), 0u);
        ASSERT_TRUE(m.net.vote(true));                                                   // keep waiting for Cat: the choice goes out for the seat that the vote is about
        ASSERT_TRUE(m.net.vote(false));                                                  // ... and continue without it
        ASSERT_TRUE(w.run_until([&]() { return votes_sent().size() == 2; }, 3000));
        ASSERT_TRUE((votes_sent() == std::vector<std::pair<uint8_t, bool>>{{2, false}, {2, true}}));

        p.votes_continue = 0;                                                            // the server counted again: this machine's last choice is to keep waiting
        p.your_vote = 1;
        ASSERT_TRUE(say(p, [&]() { return m.net.pause_info().my_vote == net::PauseInfo::Choice::KeepWaiting; }));
        pi = m.net.pause_info();
        ASSERT_TRUE(pi.vote_open && pi.votes_continue == 0 && pi.my_vote == net::PauseInfo::Choice::KeepWaiting);

        p.vote_seat = 255;                                                               // the vote is closed and this machine has no choice
        p.votes_continue = 0;
        p.voters = 0;
        p.your_vote = 0;
        ASSERT_TRUE(say(p, [&]() { return !m.net.pause_info().vote_open; }));
        pi = m.net.pause_info();
        ASSERT_TRUE(m.net.paused() && !pi.vote_open && pi.vote_seat == 255 && pi.vote_name.empty() && pi.votes_continue == 0 && pi.voters == 0 && pi.my_vote == net::PauseInfo::Choice::None);
        ASSERT_EQ(pi.missing.size(), size_t{2});
        ASSERT_TRUE(!m.net.vote(true) && votes_sent().size() == 2);                      // (nothing is voted when no vote is open)

        p = net::PresenceMsg{};                                                          // everybody is back: the match goes on after a countdown of 7 s
        p.resume_s = 7;
        ASSERT_TRUE(say(p, [&]() { return m.net.pause_info().resume_seconds_left == 7; }));
        pi = m.net.pause_info();
        ASSERT_TRUE(m.net.paused() && pi.missing.empty() && !pi.vote_open && pi.resume_seconds_left == 7);

        p.resume_s = 0;                                                                  // the match runs
        ASSERT_TRUE(say(p, [&]() { return !m.net.paused(); }));
        pi = m.net.pause_info();
        ASSERT_TRUE(!m.net.paused() && pi.missing.empty() && pi.resume_seconds_left == 0);
    } TEST_END();

    TEST_CASE("RJ1.17 The Server's Budget Of Hellos Refuses A Machine That Comes Back Too Often (Twelve A Minute, The Thirteenth Is RejoinFailed): The Way Back Ends With \"Try Rejoin In A Minute\", The Key Stays, Nothing Tries Again By Itself, A Rejoin Within The Minute Is Refused In The Same Words, And The Rejoin After The Minute Takes The Machine Back Into The Match") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-17"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-17");
        Machine& b = w.join("Bob", "RJ-17");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        const net::SeatKey key = b.keys_given[0].key;
        const uint8_t seat = b.net.my_seat();
        const std::string refused_text = "The server would not take you back now. Try Rejoin in a minute.";
        for (uint32_t i = 1; i <= 12; ++i) {                                             // twelve cuts within a minute: each is taken back
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-17").rejoins == i && !w.status("RJ-17").paused; }, 15000));
            w.run(300);
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        }
        ASSERT_TRUE(w.server.cut_newest());                                              // the thirteenth Hello of the minute
        ASSERT_TRUE(w.run_until([&]() { return b.net.phase() == NetGame::Phase::Over; }, 15000));
        w.run(10);
        ASSERT_EQ(b.net.status_text(), refused_text);
        ASSERT_TRUE(b.keys_forgotten.empty() && b.saw(NetGame::Event::Type::HostLeft));
        const uint32_t seen = w.server.accepted;
        w.run(6000);
        ASSERT_EQ(w.server.accepted, seen);                                              // nothing tries again by itself
        ASSERT_TRUE(w.status("RJ-17").paused && w.status("RJ-17").absent.size() == 1 && w.status("RJ-17").absent[0].seat == seat && w.status("RJ-17").rejoins == 12);      // (the seat is still held)
        b.net.leave();                                                                   // the application's way back to its menu keeps the key
        ASSERT_TRUE(b.keys_forgotten.empty());
        Machine& early = w.add_machine("Bob");                                           // the player tries Rejoin at once: the same words, the key stays
        ASSERT_TRUE(early.net.join("127.0.0.1", w.server.port(), "Bob", seat, "RJ-17", "", key));
        ASSERT_TRUE(w.run_until([&]() { return early.net.phase() == NetGame::Phase::Failed; }, 5000));
        ASSERT_TRUE(early.net.fail_reason() == NetGame::FailReason::Rejected && early.net.reject_reason() == net::RejectReason::RejoinFailed);
        ASSERT_EQ(early.net.status_text(), refused_text);
        ASSERT_TRUE(early.keys_forgotten.empty() && early.keys_given.empty());
        early.net.leave();
        ASSERT_TRUE(early.keys_forgotten.empty());
        w.run(61000);                                                                    // the minute is over
        Machine& again = w.add_machine("Bob");
        ASSERT_TRUE(again.net.join("127.0.0.1", w.server.port(), "Bob", seat, "RJ-17", "", key));
        ASSERT_TRUE(w.run_until([&]() { return again.net.phase() == NetGame::Phase::Playing && again.count(NetGame::Event::Type::Rejoined) >= 1 && w.status("RJ-17").rejoins == 13 && !w.status("RJ-17").paused; }, 30000));       // (the machine has the server's word when the room says so only where a loopback delivers within the pass)
        ASSERT_TRUE(again.my_seat_is(seat) && again.count(NetGame::Event::Type::Rejoined) == 1);
        w.run(3000);
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-17") && a.sim.is_match_over() && again.sim.is_match_over(); }, 20000));
        w.run(2000);
        ASSERT_TRUE(all_equal({&a, &again}) && a.sim.state_hash().total == w.status("RJ-17").referee_hash);
        ASSERT_TRUE(again.keys_forgotten.size() == 1);
    } TEST_END();

    TEST_CASE("RJ1.18 A Link Cut In The Seconds Of The Start Dialog (No Turn Was Sealed Yet, The Machine Has Run None): The Machine Comes Back In Memory With No Turns, The Room Gives It The Match From The Start, The Seconds Of The Dialog Slide, And Both Machines And The Referee End In The Same State") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-18"), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-18");
        Machine& b = w.join("Bob", "RJ-18");
        ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Playing && b.net.phase() == NetGame::Phase::Playing; }, 12000));
        ASSERT_TRUE(a.ticks == 0 && b.ticks == 0 && b.net.turns_executed() == 0);        // the dialog of the start: no turn was sealed
        ASSERT_TRUE(w.server.cut_newest());
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-18").rejoins == 1 && !w.status("RJ-18").paused && b.count(NetGame::Event::Type::Rejoined) >= 1; }, 30000));
        ASSERT_TRUE(b.net.phase() == NetGame::Phase::Playing && b.count(NetGame::Event::Type::Rejoined) == 1);
        ASSERT_TRUE(b.count(NetGame::Event::Type::StartRequested) == 1 && !b.saw(NetGame::Event::Type::HostLeft) && !b.saw(NetGame::Event::Type::Failed));      // (it did not start from nothing: its engine was loaded)
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 20000 + kPre));
        w.run(3000);
        ASSERT_TRUE(b.net.turns_executed() > 40 && a.net.turns_executed() > 40);
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("RJ-18") && a.sim.is_match_over() && b.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("RJ-18");
        ASSERT_TRUE(end.state == RoomState::Finished && end.rejoins == 1 && end.referee_hash != 0);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);
        ASSERT_TRUE(a.hash_at.count(end.ticks) == 1 && a.hash_at[end.ticks] == end.referee_hash && b.hash_at.count(end.ticks) == 1 && b.hash_at[end.ticks] == end.referee_hash);
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
    } TEST_END();

    TEST_CASE("RJ1.19 A Public Match That Nobody Comes Back To Does Not Hold Its Place For Long (The Review's M1): A Public Room's Pause Cap Is 10 Minutes, Not The Server's 30; A Hello With A Block That Needs A Public Place When None Is Free Ends The Room That Has Been Abandoned The Longest (Once It Has Been For A Minute); A Room With A Person At It Is Never Ended For That; The Rooms Of The Control Interface Keep The Server's Cap; /busy Counts As It Did") {
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.reconnect = true;
        limits.resume_countdown_ms = 0;
        const auto close_tabs = [](World& w, std::initializer_list<Machine*> ms) {         // a tab that is closed says no goodbye: its link is just gone
            for (Machine* m : ms) w.machines.erase(std::remove_if(w.machines.begin(), w.machines.end(), [m](const std::unique_ptr<Machine>& p) { return p.get() == m; }), w.machines.end());
        };
        {   // the caps, as the status says them (the rooms of the control interface start from default_spec: the server's own)
            ServerLimits small = limits;
            small.max_pause_ms = 120000;                                                 // a server that was told a cap below the public rooms' cap keeps it
            World w(nullptr, small);
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_EQ(kDemoMaxPauseMs, 10u * 60u * 1000u);
            Machine& a = w.join_creating("Ann", "capm0001", block_of("", 2));
            ASSERT_TRUE(w.run_until([&]() { return a.net.phase() == NetGame::Phase::Room; }, 4000));
            ASSERT_EQ(w.status("capm0001").max_pause_ms, 120000u);
            RoomSpec control = w.server.mgr->default_spec();
            control.code = "CTRL-1";
            control.map = "TINY.LVL";
            ASSERT_TRUE(w.server.mgr->create_room(control, w.server_now()).ok);
            ASSERT_EQ(w.status("CTRL-1").max_pause_ms, 120000u);
        }
        {   // a room that somebody made (the control interface starts from default_spec) keeps the half hour of the server's default
            World w(nullptr, limits);
            ASSERT_TRUE(w.server.start(w.now));
            ASSERT_EQ(w.server.mgr->default_spec().max_pause_ms, 30u * 60u * 1000u);
            RoomSpec control = w.server.mgr->default_spec();
            control.code = "CTRL-1";
            control.map = "TINY.LVL";
            ASSERT_TRUE(w.server.mgr->create_room(control, w.server_now()).ok);
            ASSERT_EQ(w.status("CTRL-1").max_pause_ms, 30u * 60u * 1000u);
        }
        World w("rj19", limits);                                                         // (with restart records: an ended room's record must go)
        ASSERT_TRUE(w.server.start(w.now));
        Machine& a1 = w.join_creating("Ann", "scrm0001", block_of("", 2));
        Machine& b1 = w.join("Bob", "scrm0001");
        Machine& a2 = w.join_creating("Cat", "scrm0002", block_of("", 2));
        Machine& b2 = w.join("Dan", "scrm0002");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a1, &b1, &a2, &b2}); }, 14000 + kPre));
        ASSERT_EQ(w.status("scrm0001").max_pause_ms, 600000u);                          // a public room's cap
        ASSERT_EQ(w.status("scrm0002").max_pause_ms, 600000u);
        ASSERT_TRUE(w.server.mgr->busy(w.server_now()).matches == 2 && w.server.mgr->busy(w.server_now()).players == 4);
        ASSERT_TRUE(std::filesystem::exists(w.server.record_path("scrm0001")) && std::filesystem::exists(w.server.record_path("scrm0002")));
        // the people of sc1 close their tabs; 40 s later so do the people of sc2
        close_tabs(w, {&a1, &b1});
        w.run(40000);
        close_tabs(w, {&a2, &b2});
        w.run(10000);
        ASSERT_TRUE(w.status("scrm0001").state == RoomState::Running && w.status("scrm0001").paused && w.status("scrm0001").absent.size() == 2);
        ASSERT_TRUE(w.status("scrm0002").state == RoomState::Running && w.status("scrm0002").paused);
        ASSERT_TRUE(w.server.mgr->busy(w.server_now()).matches == 0 && w.server.mgr->busy(w.server_now()).players == 0);     // (nobody is there: /busy counts as it always did)
        ASSERT_TRUE(w.server.mgr->rooms_created() == 2);
        // sc1 has been abandoned for 50 s: less than the floor, and no place is free: the answer is today's
        Machine& c0 = w.join_creating("Eve", "scrm0003", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return c0.net.phase() != NetGame::Phase::Connecting; }, 4000));
        ASSERT_TRUE(c0.net.phase() == NetGame::Phase::Failed && c0.net.reject_reason() == net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.server.mgr->room_count() == 2 && w.status("scrm0003").code.empty() && w.status("scrm0001").state == RoomState::Running);
        close_tabs(w, {&c0});
        // five minutes after the people of sc1 went: the newcomer is given a place, and it is sc1's (the room that has been abandoned the longest); sc2 is not touched
        w.run(4 * 60 * 1000 - 3000);
        (void)w.server.mgr->take_ended(w.server_now());
        const uint64_t log_before = w.server.mgr->log_bytes();
        Machine& c = w.join_creating("Eve", "scrm0003", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return c.net.phase() == NetGame::Phase::Room; }, 4000));
        ASSERT_TRUE(w.status("scrm0001").code.empty());                                 // gone: its place was needed
        ASSERT_TRUE(w.status("scrm0002").state == RoomState::Running && w.status("scrm0003").state == RoomState::Waiting && w.server.mgr->room_count() == 2);
        const std::vector<RoomStatus> ended = w.server.mgr->take_ended(w.server_now());
        ASSERT_TRUE(ended.size() == 1 && ended[0].code == "scrm0001" && ended[0].state == RoomState::Failed && ended[0].reason.find("abandoned") != std::string::npos);      // (its end is reported once, like any room's)
        ASSERT_TRUE(w.server.mgr->take_ended(w.server_now()).empty());
        ASSERT_FALSE(std::filesystem::exists(w.server.record_path("scrm0001")));          // its record is deleted (a restart would not bring it back) ...
        ASSERT_TRUE(std::filesystem::exists(w.server.record_path("scrm0002")));
        ASSERT_TRUE(log_before > 0 && w.server.mgr->log_bytes() < log_before);           // ... and its turn log is freed
        // the cap of a public room has ended sc2 on its own when 10 minutes of pause have gone by (it has been abandoned since 40 s after sc1)
        w.run(11 * 60 * 1000 - 5 * 60 * 1000);
        ASSERT_TRUE(w.status("scrm0002").state == RoomState::Finished && w.status("scrm0002").reason == "everybody left");
    } TEST_END();

    TEST_CASE("RJ1.19b A Room Of The Control Interface That Nobody Comes Back To Is No Public Place To Take (Protocol 15): Only A Room That A Visitor's Block Made Is Ended For Another Visitor's, However Long The Other Room Has Been Abandoned") {
        ServerLimits limits;
        limits.demo_rooms = 1;
        limits.demo_map = "TINY.LVL";
        limits.reconnect = true;
        limits.resume_countdown_ms = 0;
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("CTRL-AB", 2), w.server_now()).ok);
        Machine& c1 = w.join("Cat", "CTRL-AB");
        Machine& c2 = w.join("Dan", "CTRL-AB");
        Machine& a = w.join_creating("Ann", "pubab001", block_of("", 2));                  // the one public place, with two people at it
        Machine& b = w.join("Bob", "pubab001");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&c1, &c2, &a, &b}); }, 14000 + kPre));
        for (Machine* gone : {&c1, &c2}) w.machines.erase(std::remove_if(w.machines.begin(), w.machines.end(), [gone](const std::unique_ptr<Machine>& p) { return p.get() == gone; }), w.machines.end());
        w.run(90000);                                                                      // a minute and a half: the control interface's room has been abandoned for longer than the floor
        ASSERT_TRUE(w.status("CTRL-AB").state == RoomState::Running && w.status("CTRL-AB").paused && w.status("CTRL-AB").absent.size() == 2);
        (void)w.server.mgr->take_ended(w.server_now());
        Machine& e = w.join_creating("Eve", "pubab002", block_of("", 2));                 // a visitor wants a public place: none is free, and the only room that qualifies is a public one
        ASSERT_TRUE(w.run_until([&]() { return e.net.phase() != NetGame::Phase::Connecting; }, 4000));
        ASSERT_TRUE(e.net.phase() == NetGame::Phase::Failed && e.net.reject_reason() == net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.status("CTRL-AB").state == RoomState::Running && w.status("pubab001").state == RoomState::Running && w.status("pubab002").code.empty());
        ASSERT_TRUE(w.server.mgr->room_count() == 2 && w.server.mgr->take_ended(w.server_now()).empty());                // nobody was ended for it
    } TEST_END();

    TEST_CASE("RJ1.20 A Room With A Person At It Is Never Ended For A Public Place (One Person Is Enough: Its Partner's Seat Is Held, The Match Is Paused, And The Room Stays); A Room That Waits Is Not Ended Either; When No Room Qualifies The Hello Is Answered As Before, And The Room That Is Abandoned Later Is Ended Then") {
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.reconnect = true;
        limits.resume_countdown_ms = 0;
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        Machine& a1 = w.join_creating("Ann", "prrm0001", block_of("", 2));
        Machine& b1 = w.join("Bob", "prrm0001");
        Machine& a2 = w.join_creating("Cat", "prrm0002", block_of("", 2));
        Machine& b2 = w.join("Dan", "prrm0002");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a1, &b1, &a2, &b2}); }, 14000 + kPre));
        const auto close_tabs = [&w](std::initializer_list<Machine*> ms) {
            for (Machine* m : ms) w.machines.erase(std::remove_if(w.machines.begin(), w.machines.end(), [m](const std::unique_ptr<Machine>& p) { return p.get() == m; }), w.machines.end());
        };
        close_tabs({&b1, &a2, &b2});                                                     // pr1 keeps Ann; pr2's people are gone
        w.run(2 * 60 * 1000);                                                            // two minutes: pr1's match is paused for Bob's seat, pr2 has been abandoned
        ASSERT_TRUE(w.status("prrm0001").paused && w.status("prrm0001").absent.size() == 1 && w.status("prrm0002").absent.size() == 2);
        Machine& c = w.join_creating("Eve", "prrm0003", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return c.net.phase() == NetGame::Phase::Room; }, 4000));
        ASSERT_TRUE(w.status("prrm0002").code.empty() && w.status("prrm0001").state == RoomState::Running);       // pr2 went, pr1 (a person at it) stayed
        // no room qualifies now: pr1 has Ann at it, pr3 waits (and has Eve): the next Hello is told NoSuchRoom, and nobody is ended for it, however long it takes
        w.run(5 * 60 * 1000);
        Machine& d = w.join_creating("Fay", "prrm0004", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return d.net.phase() != NetGame::Phase::Connecting; }, 4000));
        ASSERT_TRUE(d.net.phase() == NetGame::Phase::Failed && d.net.reject_reason() == net::RejectReason::NoSuchRoom);
        ASSERT_EQ(d.net.status_text(), std::string("The server cannot make a room now: it is busy, or hosts no online matches. Try again in a few minutes."));      // (a Hello with a block: it is the place that is missing, not a room that does not exist)
        ASSERT_TRUE(w.status("prrm0001").state == RoomState::Running && w.status("prrm0003").state == RoomState::Waiting && w.server.mgr->room_count() == 2);
        ASSERT_TRUE(a1.net.phase() == NetGame::Phase::Playing && !w.server.mgr->take_ended(w.server_now()).empty());      // (Ann is still in her match: pr2's end was the only one)
        // Ann closes her tab: pr1 is abandoned from now on; for half a minute it is not ended, after a minute it is
        close_tabs({&a1});
        w.run(30000);
        Machine& e = w.join_creating("Fay", "prrm0004", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return e.net.phase() != NetGame::Phase::Connecting; }, 4000));
        ASSERT_TRUE(e.net.phase() == NetGame::Phase::Failed && w.status("prrm0001").state == RoomState::Running);
        close_tabs({&e});
        w.run(40000);
        Machine& f = w.join_creating("Fay", "prrm0004", block_of("", 2));
        ASSERT_TRUE(w.run_until([&]() { return f.net.phase() == NetGame::Phase::Room; }, 4000));
        ASSERT_TRUE(w.status("prrm0001").code.empty() && w.status("prrm0003").state == RoomState::Waiting && w.status("prrm0004").state == RoomState::Waiting);
        {   // a stall of the server's loop (a frozen machine, a swap storm; the re-check's N2): the first pass after it reads a Hello for a third code before the rooms have looked at their people
            // again, and by the clock alone both rooms have been abandoned for two minutes. Their people are at them: Dan at one (the seat of his partner Cat is held), Ann at the other (she came
            // back after her tab was closed, and is catching up: a person too, and Bob's seat is held). A person on the first seat and one on the second: each seat counts. Nobody is ended for a place.
            World s(nullptr, limits);
            ASSERT_TRUE(s.server.start(s.now));
            Machine& ann = s.join_creating("Ann", "strm0001", block_of("", 2));
            Machine& bob = s.join("Bob", "strm0001");
            Machine& cat = s.join_creating("Cat", "strm0002", block_of("", 2));
            Machine& dan = s.join("Dan", "strm0002");
            ASSERT_TRUE(s.run_until([&]() { return s.running({&ann, &bob, &cat, &dan}); }, 14000 + kPre));
            s.run(3000);
            ASSERT_TRUE(s.status("strm0001").state == RoomState::Running && s.status("strm0002").state == RoomState::Running);
            ASSERT_TRUE(ann.keys_given.size() == 1 && ann.net.my_seat() == 0 && dan.net.my_seat() == 1);
            const net::RejoinKey ann_key = ann.keys_given[0];
            for (Machine* gone : {&ann, &bob, &cat}) s.machines.erase(std::remove_if(s.machines.begin(), s.machines.end(), [gone](const std::unique_ptr<Machine>& p) { return p.get() == gone; }), s.machines.end());
            s.run(70000);                                                                // (those tabs are closed: a minute and ten seconds on, st1 is abandoned by the floor, st2 has Dan)
            ASSERT_TRUE(s.status("strm0001").paused && s.status("strm0001").absent.size() == 2 && s.status("strm0002").paused && s.status("strm0002").absent.size() == 1);
            Machine& back = s.add_machine("Ann");
            ASSERT_TRUE(back.net.join("127.0.0.1", s.server.port(), "Ann", ann_key.seat, "strm0001", "", ann_key.key));
            bool catching_up = false;
            for (int i = 0; i < 2000 && !catching_up; ++i) {                             // (her machine and the server move, no other, until the server has her catching up: her machine does not read the match)
                back.frame(s.now);
                s.server.pump(s.now);
                for (const RoomStatus::Absent& held : s.status("strm0001").absent) catching_up = catching_up || (held.catching_up && held.seat == ann_key.seat);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(catching_up);
            Machine& eve = s.add_machine("Eve");
            eve.net.set_create(block_of("", 2));
            ASSERT_TRUE(eve.net.join("127.0.0.1", s.server.port(), "Eve", 255, "strm0003"));
            for (int i = 0; i < 400; ++i) {                                              // (her Hello is on its way to a server that does not look: the loop is frozen)
                eve.frame(s.now);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            s.now += 120000;                                                             // the loop wakes up two minutes later: one pass, with the Hello in it
            s.server.pump(s.now);
            ASSERT_TRUE(s.status("strm0001").state == RoomState::Running && s.status("strm0002").state == RoomState::Running);
            ASSERT_TRUE(s.status("strm0003").code.empty() && s.server.mgr->room_count() == 2 && s.server.mgr->rooms_created() == 2 && s.server.mgr->take_ended(s.server_now()).empty());
            ASSERT_TRUE(s.run_until([&]() { return eve.net.phase() == NetGame::Phase::Failed; }, 4000));
            ASSERT_TRUE(eve.net.reject_reason() == net::RejectReason::NoSuchRoom);       // the answer that it always was when no place is free
            ASSERT_EQ(eve.net.status_text(), std::string("The server cannot make a room now: it is busy, or hosts no online matches. Try again in a few minutes."));
        }
    } TEST_END();

    TEST_CASE("RJ1.21 A Keyed Hello To A Public Room That Ended Does Not Replace It (The Review's M3), Block Or Not: A Match Ended By The Cap While Its Players Were Away; Their Key Is Told NoSuchRoom (\"The match is over.\"), The Key Is Let Go Of, And The Room Stays As It Ended; A Hello Without A Key But With The Block Makes A New Room, As It Always Did (A Late Friend, A Rematch With The Same Link); A Stale Key That Meets Another Player's New Waiting Room Of The Same Code Is A New Player's (The Re-Check's N5)") {
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.reconnect = true;
        limits.resume_countdown_ms = 0;
        limits.max_pause_ms = 60000;                                                     // (this server's cap: a public room takes the smaller of it and 10 minutes)
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        const std::string code = "rj21aaaa";
        Machine& a = w.join_creating("Ann", code, block_of("", 2));
        Machine& b = w.join("Bob", code);
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 12000 + kPre));
        ASSERT_TRUE(b.keys_given.size() == 1);
        const net::RejoinKey bob_key = b.keys_given[0];
        w.machines.clear();                                                              // both tabs are closed
        ASSERT_TRUE(w.run_until([&]() { return w.finished(code); }, 150000));
        ASSERT_EQ(w.status(code).reason, std::string("everybody left"));                 // the cap dropped the seats of people who never came back
        Machine& b2 = w.add_machine("Bob");
        b2.net.set_create(block_of("", 2));                                              // (the link carries the block and the key: the key wins, the ended room is not replaced)
        ASSERT_TRUE(b2.net.join("127.0.0.1", w.server.port(), "Bob", bob_key.seat, code, "", bob_key.key));
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Failed; }, 5000));
        ASSERT_TRUE(b2.net.reject_reason() == net::RejectReason::NoSuchRoom && b2.net.status_text() == "The match is over.");
        ASSERT_TRUE(b2.keys_forgotten.size() == 1 && net::key_matches(b2.keys_forgotten[0].key, bob_key.key) && b2.keys_given.empty());
        ASSERT_TRUE(w.status(code).state == RoomState::Finished && w.server.mgr->rooms_created() == 1);        // the room that ended is still that room: nothing was made for the key
        Machine& c = w.join_creating("Cat", code, block_of("", 2));                       // the same code without a key, with the block: a new room, as it always did
        ASSERT_TRUE(w.run_until([&]() { return c.net.phase() == NetGame::Phase::Room; }, 5000));
        ASSERT_TRUE(w.status(code).state == RoomState::Waiting && w.status(code).joined == 1 && w.server.mgr->rooms_created() == 2);
        w.run(500);
        ASSERT_TRUE(c.keys_given.empty());                                               // (a waiting room keeps no key)
        // the stale key meets that NEW waiting room of the same code, which another player's Hello made (the re-check's N5: what the first RJ1.6 reached with the real server): the lobby takes the
        // Hello for a new player's, the machine says so and lets the old key go, and its new key is kept when the match starts (the room is full with him: that is at once)
        Machine& b3 = w.add_machine("Bob");
        b3.net.set_create(block_of("", 2));
        ASSERT_TRUE(b3.net.join("127.0.0.1", w.server.port(), "Bob", bob_key.seat, code, "", bob_key.key));
        ASSERT_TRUE(w.run_until([&]() { return b3.saw(NetGame::Event::Type::StartRequested); }, 14000 + kPre));
        ASSERT_EQ(b3.status_at_start, std::string("Your match has ended. This is a new room."));
        ASSERT_TRUE(b3.status_at_start.size() <= NetGame::kStatusNoticeChars);           // (the notice fits the line that shows it)
        ASSERT_TRUE(b3.keys_forgotten.size() == 1 && net::key_matches(b3.keys_forgotten[0].key, bob_key.key) && b3.keys_forgotten[0].room == code);
        ASSERT_FALSE(b3.saw(NetGame::Event::Type::Failed) || b3.saw(NetGame::Event::Type::HostLeft));
        ASSERT_TRUE(b3.net.my_seat() < sim::MAX_PLAYERS && b3.net.room().slots[b3.net.my_seat()].name == "Bob");
        ASSERT_TRUE(w.status(code).joined == 2 && w.server.mgr->rooms_created() == 2);
        ASSERT_TRUE(w.run_until([&]() { return w.running({&c, &b3}); }, 14000 + kPre));
        ASSERT_TRUE(b3.keys_given.size() == 1 && !net::key_matches(b3.keys_given[0].key, bob_key.key) && !net::key_is_zero(b3.keys_given[0].key) && b3.keys_forgotten.size() == 1);
        for (const NetGame::Event& e : b3.events) ASSERT_FALSE(e.rejoin);               // it is no rejoin
    } TEST_END();

    TEST_CASE("RJ1.22 A Start That Is Cancelled Takes The Key Back (The Review's M3): The Key Of A New Player Is Kept When The Start Arrives, Let Go Of When The Start Is Cancelled (The Room Waits Again And Keeps None), Kept Again At The Next Start; The Function That Is Told May End The Session From Inside The Call") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        net::SeatKey key{};
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 9u + 4u);
        net::WelcomeMsg welcome;
        welcome.player = 1;
        welcome.players = 4;
        welcome.key = key;
        net::RoomMsg room;
        room.you = 1;
        room.slots[0].state = net::SlotState::Client;
        room.slots[0].name = "Ann";
        room.slots[1].state = net::SlotState::Client;
        room.slots[1].name = "Bob";
        room.leader = 0;
        w.server.door = Server::Door::Scripted;
        w.server.script_messages = {net::encode(welcome), net::encode(room)};
        uint64_t map_hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", map_hash));
        net::StartMsg start;
        start.seed = 8;
        start.map_name = "TINY.LVL";
        start.map_hash = map_hash;
        start.roster = 0x03;
        start.names[0] = "Ann";
        start.names[1] = "Bob";
        const auto loaded = [&]() {
            size_t n = 0;
            for (const std::vector<uint8_t>& m : w.server.scripted_inbox) n += net::peek_type(m) == net::MsgType::Loaded ? 1u : 0u;
            return n;
        };
        Machine& m = w.add_machine("Bob");
        m.orders = false;
        ASSERT_TRUE(m.net.join("127.0.0.1", w.server.port(), "Bob", 1, "RJ-22"));
        ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Room; }, 5000));
        ASSERT_TRUE(m.keys_given.empty() && m.keys_forgotten.empty());                  // the Welcome gave a key; the waiting room keeps none
        ASSERT_TRUE(w.server.scripted_send(net::encode(start)));
        ASSERT_TRUE(w.run_until([&]() { return loaded() == 1; }, 5000));
        ASSERT_TRUE(m.keys_given.size() == 1 && net::key_matches(m.keys_given[0].key, key) && m.keys_given[0].seat == 1 && m.keys_given[0].room == "RJ-22" && m.keys_forgotten.empty());   // the Start: kept
        ASSERT_TRUE(w.server.scripted_send(net::encode(start)));                         // a second Start without a cancel (a server that says it twice): the key is not given twice
        w.run(500);
        ASSERT_TRUE(m.keys_given.size() == 1 && m.keys_forgotten.empty());
        net::CancelMsg cancel;
        cancel.reason = net::CancelMsg::Reason::LoadFailed;
        cancel.player = 0;
        ASSERT_TRUE(w.server.scripted_send(net::encode(cancel)));
        ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Room && m.saw(NetGame::Event::Type::Cancelled); }, 5000));
        ASSERT_TRUE(m.keys_forgotten.size() == 1 && net::key_matches(m.keys_forgotten[0].key, key));        // the start was cancelled: the key goes
        ASSERT_TRUE(w.server.scripted_send(net::encode(start)));                         // the next Start: kept again
        ASSERT_TRUE(w.run_until([&]() { return loaded() == 2; }, 5000));
        ASSERT_TRUE(m.keys_given.size() == 2 && net::key_matches(m.keys_given[1].key, key) && m.keys_forgotten.size() == 1);
        // the player leaves at the Start's cancel, from inside the call that lets go of the key
        World v;
        ASSERT_TRUE(v.server.start(v.now));
        v.server.door = Server::Door::Scripted;
        v.server.script_messages = {net::encode(welcome), net::encode(room)};
        Machine& n = v.add_machine("Bob");
        n.orders = false;
        n.leave_on_forget = true;
        ASSERT_TRUE(n.net.join("127.0.0.1", v.server.port(), "Bob", 1, "RJ-22"));
        ASSERT_TRUE(v.run_until([&]() { return n.net.phase() == NetGame::Phase::Room; }, 5000));
        ASSERT_TRUE(v.server.scripted_send(net::encode(start)));
        ASSERT_TRUE(v.run_until([&]() { return n.keys_given.size() == 1; }, 5000));
        ASSERT_TRUE(v.server.scripted_send(net::encode(cancel)));
        ASSERT_TRUE(v.run_until([&]() { return !n.keys_forgotten.empty(); }, 5000));
        ASSERT_EQ(n.net.phase(), NetGame::Phase::Off);
        v.run(500);
        ASSERT_TRUE(n.net.phase() == NetGame::Phase::Off && n.keys_forgotten.size() == 1);
        // a guest whose OWN load of the map fails (the re-check's N1) is back in the room at once and the lobby takes no Cancel of the server's after that: it lets go of the key that the Start
        // made it keep, then and there, and not at a Cancel that never counts
        {
            World f;
            ASSERT_TRUE(f.server.start(f.now));
            f.server.door = Server::Door::Scripted;
            f.server.script_messages = {net::encode(welcome), net::encode(room)};
            Machine& g = f.add_machine("Bob");
            g.orders = false;
            g.fail_load = true;
            ASSERT_TRUE(g.net.join("127.0.0.1", f.server.port(), "Bob", 1, "RJ-22"));
            ASSERT_TRUE(f.run_until([&]() { return g.net.phase() == NetGame::Phase::Room; }, 5000));
            ASSERT_TRUE(f.server.scripted_send(net::encode(start)));
            ASSERT_TRUE(f.run_until([&]() { return g.saw(NetGame::Event::Type::Cancelled); }, 5000));
            ASSERT_TRUE(g.net.phase() == NetGame::Phase::Room && g.loads == 0);
            ASSERT_TRUE(g.keys_given.size() == 1 && g.keys_forgotten.size() == 1 && net::key_matches(g.keys_forgotten[0].key, key) && g.keys_forgotten[0].room == "RJ-22");
            ASSERT_TRUE(f.server.scripted_send(net::encode(cancel)));                    // the server's own Cancel (it was told Loaded{false}) changes nothing: the key goes once
            f.run(500);
            ASSERT_TRUE(g.keys_given.size() == 1 && g.keys_forgotten.size() == 1);
            ASSERT_TRUE(f.server.scripted_send(net::encode(start)));                     // the room tries again: kept again, and let go of again
            ASSERT_TRUE(f.run_until([&]() { return g.keys_forgotten.size() == 2; }, 5000));
            ASSERT_TRUE(g.keys_given.size() == 2 && net::key_matches(g.keys_given[1].key, key));
        }
        {   // the same, with the real server: Ann loads, Bob cannot; every Start gives a key and every cancel takes it back, for the one who could load and for the one who could not
            ServerLimits limits;
            World r(nullptr, limits);
            ASSERT_TRUE(r.server.start(r.now));
            ASSERT_TRUE(r.server.mgr->create_room(held_spec("RJ-22B", 2), r.server_now()).ok);
            Machine& ann = r.join("Ann", "RJ-22B");
            Machine& bob = r.add_machine("Bob");
            bob.fail_load = true;
            ASSERT_TRUE(bob.net.join("127.0.0.1", r.server.port(), "Bob", 255, "RJ-22B"));
            ASSERT_TRUE(r.run_until([&]() { return bob.count(NetGame::Event::Type::Cancelled) >= 1 && bob.net.phase() == NetGame::Phase::Room; }, 15000));
            ASSERT_TRUE(!bob.keys_given.empty() && bob.keys_given.size() == bob.keys_forgotten.size());     // (not one key left in the store of a match that did not begin)
            ASSERT_TRUE(r.run_until([&]() { return ann.count(NetGame::Event::Type::Cancelled) >= 1 && ann.net.phase() == NetGame::Phase::Room; }, 15000));
            ASSERT_TRUE(!ann.keys_given.empty() && ann.keys_given.size() == ann.keys_forgotten.size());
            ASSERT_TRUE(r.status("RJ-22B").state == RoomState::Waiting);
        }
        {   // the function that is told to let go of the key leaves from inside that call: the session is over, and nothing touches it afterwards
            World f;
            ASSERT_TRUE(f.server.start(f.now));
            f.server.door = Server::Door::Scripted;
            f.server.script_messages = {net::encode(welcome), net::encode(room)};
            Machine& g = f.add_machine("Bob");
            g.orders = false;
            g.fail_load = true;
            g.leave_on_forget = true;
            ASSERT_TRUE(g.net.join("127.0.0.1", f.server.port(), "Bob", 1, "RJ-22"));
            ASSERT_TRUE(f.run_until([&]() { return g.net.phase() == NetGame::Phase::Room; }, 5000));
            ASSERT_TRUE(f.server.scripted_send(net::encode(start)));
            ASSERT_TRUE(f.run_until([&]() { return !g.keys_forgotten.empty(); }, 5000));
            ASSERT_EQ(g.net.phase(), NetGame::Phase::Off);
            f.run(500);
            ASSERT_TRUE(g.net.phase() == NetGame::Phase::Off && g.keys_given.size() == 1 && g.keys_forgotten.size() == 1);
        }
    } TEST_END();

    TEST_CASE("RJ1.23 A Vote About A Machine's Own Seat Is Not Shown To It And Cannot Be Cast By It (L5 Of The Review): A Seat That Flaps Is Put To The Vote At Once; When Its Machine Is Back The Others Have The Block With The Vote About It, And Its Own Screen Has None (pause_info Has No Vote, vote() Is False For Both Choices)") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-23", 3), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-23");
        Machine& b = w.join("Bob", "RJ-23");
        Machine& c = w.join("Cat", "RJ-23");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b, &c}); }, 14000 + kPre));
        w.run(2000);
        for (int cut = 0; cut < 3; ++cut) {                                              // (Cat's link is the newest each time: it flaps)
            ASSERT_TRUE(w.server.cut_newest());
            ASSERT_TRUE(w.run_until([&]() { return c.net.pause_info().reconnecting; }, 4000));
            ASSERT_TRUE(w.run_until([&]() { return !c.net.pause_info().reconnecting && !c.net.pause_info().catching_up && c.net.phase() == NetGame::Phase::Playing; }, 20000));
            w.run(1500);
        }
        const uint8_t cat = c.net.my_seat();
        ASSERT_TRUE(w.status("RJ-23").vote_seat == cat);                                 // the server holds a vote about Cat's seat, now that Cat is back
        for (Machine* other : {&a, &b}) {
            const net::PauseInfo p = other->net.pause_info();
            ASSERT_TRUE(p.vote_open && p.vote_seat == cat && p.vote_name == "Cat" && p.voters == 2);
        }
        const net::PauseInfo own = c.net.pause_info();
        ASSERT_TRUE(!own.vote_open && own.vote_seat == 255 && own.vote_name.empty() && own.voters == 0 && own.my_vote == net::PauseInfo::Choice::None);
        ASSERT_FALSE(c.net.vote(false));                                                 // (the server would ignore it: a vote is for the others)
        ASSERT_FALSE(c.net.vote(true));
        ASSERT_TRUE(a.net.vote(false));                                                  // the others may
    } TEST_END();

    TEST_CASE("RJ1.24 A Join By A Host Name Looks The Name Up Once (L7 Of The Review): The Lookup Of join() Is The Only One, The Way Back Of Every Later Loss Of The Link Goes To The Address That It Gave (No Lookup, So No Freeze Of A Frame, And A Name That Points Elsewhere By Then Gets No Key), Also When The Name Does Not Resolve Any More; A Name That Does Not Resolve At All Is A Join That Fails; The Address Is Kept As The Player Gave It For The Key's Entry") {
        World w;
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-24"), w.server_now()).ok);
        int lookups = 0;
        bool resolves = true;
        Machine& a = w.add_machine("Ann");
        a.net.set_resolver_for_test([&](const std::string& host) -> std::string {
            ++lookups;
            return resolves && host == "ants.example.test" ? std::string("127.0.0.1") : std::string();
        });
        const uint32_t before = w.server.accepted;
        ASSERT_TRUE(a.net.join("ants.example.test", w.server.port(), "Ann", 255, "RJ-24"));
        ASSERT_TRUE(w.run_until([&]() { return w.server.accepted > before; }, 2000));
        ASSERT_EQ(lookups, 1);
        ASSERT_TRUE(a.net.join_target().address == "ants.example.test" && a.net.join_target().resolved == "127.0.0.1");
        Machine& b = w.join("Bob", "RJ-24");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 14000 + kPre));
        ASSERT_EQ(lookups, 1);
        resolves = false;                                                                // (the name stops resolving: its DNS is down, or points elsewhere)
        size_t ann_wire = before;
        for (int loss = 1; loss <= 3; ++loss) {                                          // three losses of the link: every way back goes to the address of the join
            const uint32_t accepted = w.server.accepted;
            ASSERT_TRUE(w.server.cut_wire(ann_wire));
            ASSERT_TRUE(w.run_until([&]() { return w.server.accepted > accepted && w.status("RJ-24").rejoins == static_cast<uint32_t>(loss) && !w.status("RJ-24").paused && !a.net.pause_info().reconnecting && !a.net.pause_info().catching_up; }, 25000));
            ann_wire = w.server.accepted - 1;
            w.run(500);
        }
        ASSERT_EQ(lookups, 1);                                                           // (a lookup at every attempt would be 1 + the attempts: and here they would all have failed)
        ASSERT_TRUE(a.net.phase() == NetGame::Phase::Playing && a.keys_forgotten.empty());
        ASSERT_EQ(a.keys_given.back().server, std::string("ants.example.test:") + std::to_string(w.server.port()));      // (the entry of the key says the name that the player gave)
        Machine& c = w.add_machine("Cat");
        c.net.set_resolver_for_test([&](const std::string&) -> std::string { return std::string(); });
        ASSERT_FALSE(c.net.join("nowhere.example.test", w.server.port(), "Cat", 255, "RJ-24"));          // a name that does not resolve: no join
        ASSERT_EQ(c.net.phase(), NetGame::Phase::Off);
        // an address that is a number is its own answer
        ASSERT_EQ(net::resolve_host("127.0.0.1"), std::string("127.0.0.1"));
        ASSERT_EQ(net::resolve_host(""), std::string());
    } TEST_END();

    TEST_CASE("RJ1.25 Protocol 14, A Player That The Leader Moved Is The Same Player: Its Key Is The Key Of Its New Colour In The Server's Record (Nothing Is Kept For The Old One), The Place That Keeps The Key Is Given The Seat The Player Has Then, And A Machine That Starts From Nothing With That Key Is Given The New Colour And The Match") {
        World w("rejoin-moved");
        ASSERT_TRUE(w.server.start(w.now));
        ASSERT_TRUE(w.server.mgr->create_room(held_spec("RJ-25", 4), w.server_now()).ok);
        Machine& a = w.join("Ann", "RJ-25");
        Machine& b = w.join("Bob", "RJ-25");
        Machine& c = w.join("Cat", "RJ-25");
        ASSERT_TRUE(w.run_until([&]() { return a.net.is_leader() && a.net.room().slots[2].state == net::SlotState::Client && c.net.my_seat() == 2; }, 8000));
        ASSERT_EQ(b.net.my_seat(), 1);
        ASSERT_FALSE(b.net.request_move_seat(1));                                         // (a guest sends nothing: the room has one leader)
        ASSERT_TRUE(a.net.request_move_seat(1));                                          // Bob (red) goes to the next free colour: black
        ASSERT_FALSE(a.net.request_move_seat(1));                                         // the next press waits for the room's answer
        ASSERT_TRUE(w.run_until([&]() { return b.net.my_seat() == 3 && a.net.room().slots[3].state == net::SlotState::Client; }, 5000));
        w.run(500);
        RoomStatus s = w.status("RJ-25");
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1].empty() && s.names[2] == "Cat" && s.names[3] == "Bob" && s.seat_moves == 1 && s.ignored_seat_moves == 0);
        ASSERT_TRUE(b.keys_given.empty());                                                // (a new player's key is announced with the Start, and with the seat that it has then)
        ASSERT_TRUE(a.net.request_start());                                               // three of four: START
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b, &c}); }, 12000 + kPre));
        ASSERT_EQ(b.net.my_seat(), 3);
        const auto record = w.server.read_record("RJ-25");
        const net::SeatKey key = record.head.keys[3];                                     // the key of Bob's seat, as the record has it
        ASSERT_FALSE(net::key_is_zero(key));
        ASSERT_TRUE(net::key_is_zero(record.head.keys[1]));                               // nothing is kept for the colour that he left
        ASSERT_TRUE(b.keys_given.size() == 1 && net::key_matches(b.keys_given[0].key, key) && b.keys_given[0].seat == 3 && b.keys_given[0].room == "RJ-25");      // the place that keeps it has the seat he has now
        w.run(8000);
        w.machines.erase(w.machines.begin() + 1);                                         // the page of Bob is gone: its game, its engine and its links with it (no goodbye)
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-25").paused; }, 3000));
        ASSERT_TRUE(w.status("RJ-25").absent.size() == 1 && w.status("RJ-25").absent[0].seat == 3);       // the seat that is held is the new one
        Machine& b2 = w.add_machine("Bob");                                               // a new machine with nothing but the key, and the colour that Bob's page had in its address (the old one)
        ASSERT_TRUE(b2.net.join("127.0.0.1", w.server.port(), "Bob", 1, "RJ-25", "", key));
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Playing && !w.status("RJ-25").paused; }, 30000));
        ASSERT_EQ(b2.net.my_seat(), 3);                                                   // the key says who it is: the colour it has now
        s = w.status("RJ-25");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.names[3] == "Bob" && s.names[1].empty());
        w.run(5000);
        ASSERT_TRUE(b2.net.turns_executed() > 60);
        ASSERT_FALSE(a.net.desynced() || b2.net.desynced() || c.net.desynced());
        ASSERT_TRUE(hashes_agree(a, b2) && hashes_agree(c, b2));
    } TEST_END();

    TEST_CASE("RJ1.26 Protocol 15, A Client Of A Room That Has Teams Of Its Own (Its Create Block Named Them, The Room Message Says So) Shows And Asks For The Room's Teams And Not Its Own, And So Does A Guest Whatever Block Its Own Hello Carries; A Room Whose Block Named None Keeps The Leader's Choice; The Leader's Prompt Says That The Teams Are The Room's") {
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        net::CreateBlock with_teams = block_of("TINY.LVL", 4);
        with_teams.team_a = 0;
        with_teams.team_b = 1;
        Machine& named = w.join_creating("Zed", "teams001", with_teams);
        ASSERT_TRUE(w.run_until([&]() { return named.net.phase() == NetGame::Phase::Room; }, 4000));
        ASSERT_TRUE(named.net.room_teams() == sim::StartTeams({true, 0, 1}));
        ASSERT_TRUE(named.net.effective_teams() == sim::StartTeams({true, 0, 1}));            // no choice of its own
        named.net.set_start_teams(sim::StartTeams{true, 1, 2});
        ASSERT_TRUE(named.net.start_teams() == sim::StartTeams({true, 1, 2}));                // (its own choice is kept, and does not count in this room)
        ASSERT_TRUE(named.net.effective_teams() == sim::StartTeams({true, 0, 1}));
        Machine& guest = w.join_creating("Yan", "teams001", block_of("TINY.LVL", 2));          // (a block for a room that exists is ignored: the room is still for four, with the first Hello's teams)
        ASSERT_TRUE(w.run_until([&]() { return guest.net.phase() == NetGame::Phase::Room && guest.net.my_seat() == 1 && named.net.room().slots[1].state == net::SlotState::Client; }, 4000));
        ASSERT_TRUE(guest.net.room_teams() == sim::StartTeams({true, 0, 1}));
        ASSERT_TRUE(w.status("teams001").expected == 4 && w.status("teams001").room_teams == "0+1");
        ASSERT_TRUE(named.net.is_leader() && !named.net.prompt_texts().empty());
        ASSERT_TRUE(named.net.prompt_texts().front().find("the room's teams") != std::string::npos);
        Machine& plain = w.join_creating("Xi", "teams002", block_of("TINY.LVL", 4));
        ASSERT_TRUE(w.run_until([&]() { return plain.net.phase() == NetGame::Phase::Room; }, 4000));
        plain.net.set_start_teams(sim::StartTeams{true, 1, 2});
        ASSERT_FALSE(plain.net.room_teams().set);
        ASSERT_TRUE(plain.net.effective_teams() == sim::StartTeams({true, 1, 2}));            // a room without teams of its own: the leader's choice
        ASSERT_TRUE(w.status("teams002").room_teams.empty());
    } TEST_END();

    TEST_CASE("RJ1.27 Protocol 16, The Pages Of A Lobby Room Are Taken Over By Real Games (A NetGame With The Key Of The Page's Seat): The Leader's START Waits For The Last Page To Be A Game, The Pages Are Told Superseded, The Plan (A Bot, Teams) Is The Match That Both Games Play, And Both Machines And The Referee End In The Same State") {
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_lobbies = 3;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL"};
        World w(nullptr, limits);
        ASSERT_TRUE(w.server.start(w.now));
        std::vector<Page*> pages;
        w.between = [&]() { for (Page* p : pages) p->lobby->update(w.now); };
        net::CreateBlock lobby_block = block_of("", 4);
        lobby_block.flags = static_cast<uint8_t>(net::kCreateLeaderStarts | net::kCreateLobby);
        Page ann(w.server.port(), "Ann", "lob27001", lobby_block);                          // the first page makes the room with its Hello
        pages.push_back(&ann);
        ASSERT_TRUE(w.run_until([&]() { return ann.lobby->phase() == net::ClientLobby::Phase::InRoom && ann.lobby->is_leader() && ann.lobby->room().lobby(); }, 4000));   // (the Welcome sets InRoom; the leader and the lobby flag come with the next Room message, which a slow machine can deliver one pass later)
        ASSERT_TRUE(ann.lobby->created() && ann.lobby->is_leader() && ann.lobby->room().lobby() && ann.lobby->my_seat() == 0);
        Page bob(w.server.port(), "Bob", "lob27001", std::nullopt);                         // the second is seated in it
        pages.push_back(&bob);
        ASSERT_TRUE(w.run_until([&]() { return bob.lobby->phase() == net::ClientLobby::Phase::InRoom && ann.lobby->room().slots[1].state == net::SlotState::Client; }, 4000));
        ASSERT_TRUE(!bob.lobby->created() && bob.lobby->my_seat() == 1 && !net::key_is_zero(bob.lobby->key()) && !net::key_matches(ann.lobby->key(), bob.lobby->key()));
        net::PlanMsg plan;                                                                   // the leader's plan: the two of them allied, an easy bot on the black seat, nobody on blue
        plan.map_name = "TINY.LVL";
        plan.plan = {net::PlanKind::Open, net::PlanKind::Open, net::PlanKind::Nobody, net::PlanKind::Easy};
        plan.team_a = 0;
        plan.team_b = 1;
        ASSERT_TRUE(ann.lobby->request_plan(plan));
        ASSERT_TRUE(w.run_until([&]() { return bob.lobby->room().plan[3] == net::PlanKind::Easy && bob.lobby->room().team_a == 0 && bob.lobby->room().team_b == 1; }, 3000));
        ASSERT_TRUE(ann.lobby->request_start());                                             // the START of a page: it waits until both are games
        ASSERT_TRUE(w.run_until([&]() { return bob.lobby->room().starting(); }, 3000));
        RoomStatus s = w.status("lob27001");
        ASSERT_TRUE(s.lobby && s.starting && s.state == RoomState::Waiting && s.games == 0u);
        Machine& ga = w.add_machine("Ann");                                                  // Ann's game takes her seat over with the key of her page
        ASSERT_TRUE(ga.net.join("127.0.0.1", w.server.port(), "Ann", 0, "lob27001", "", ann.lobby->key()));
        ASSERT_TRUE(w.run_until([&]() { return ga.net.my_seat() == 0 && ann.lobby->phase() == net::ClientLobby::Phase::Rejected && bob.lobby->room().seat_in_game(0); }, 4000));
        ASSERT_TRUE(ann.lobby->reject_reason() == net::RejectReason::Superseded);            // the page is told that the game replaced it (it must not come back)
        ASSERT_TRUE(ga.net.is_leader() && ga.net.room().starting() && !bob.lobby->room().seat_in_game(1));
        w.run(1500);
        s = w.status("lob27001");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.starting && s.games == 1u && s.joined == 2);          // one page is left: the START waits
        ASSERT_EQ(ga.loads, 0u);
        Machine& gb = w.add_machine("Bob");
        ASSERT_TRUE(gb.net.join("127.0.0.1", w.server.port(), "Bob", 1, "lob27001", "", bob.lobby->key()));
        ASSERT_TRUE(w.run_until([&]() { return w.running({&ga, &gb}); }, 12000 + kPre));
        ASSERT_TRUE(bob.lobby->phase() == net::ClientLobby::Phase::Rejected && bob.lobby->reject_reason() == net::RejectReason::Superseded);
        ASSERT_TRUE(ga.net.my_seat() == 0 && gb.net.my_seat() == 1 && ga.loads == 1 && gb.loads == 1);
        ASSERT_TRUE(ga.net.start_info().teams() == sim::StartTeams({true, 0, 1}) && gb.net.start_info().teams() == sim::StartTeams({true, 0, 1}));      // the plan's teams are the match's
        ASSERT_TRUE(ga.net.start_info().names[0] == "Ann" && ga.net.start_info().names[1] == "Bob" && ga.net.start_info().names[2].empty() && ga.net.start_info().names[3] == "Bot (Easy)");
        s = w.status("lob27001");
        ASSERT_TRUE(s.lobby && s.state == RoomState::Running && !s.starting && s.joined == 3 && s.plan == "oone 0+1");         // (the seat of the bot counts in the room that runs)
        ASSERT_TRUE(ga.keys_given.size() == 1 && gb.keys_given.size() == 1 && net::key_matches(ga.keys_given[0].key, ann.lobby->key()) && net::key_matches(gb.keys_given[0].key, bob.lobby->key()));      // the games keep the keys the pages had
        w.run(6000);
        ASSERT_FALSE(ga.net.desynced() || gb.net.desynced());
        ASSERT_TRUE(w.status("lob27001").ticks > 100 && hashes_agree(ga, gb));
        ga.quit();                                                                           // both leave the match: the bot's side is the only one left
        gb.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("lob27001") && ga.sim.is_match_over() && gb.sim.is_match_over(); }, 20000));
        w.run(2000);
        const RoomStatus end = w.status("lob27001");
        ASSERT_TRUE(end.state == RoomState::Finished && end.referee_hash != 0 && end.lobby);
        ASSERT_TRUE(all_equal({&ga, &gb}) && ga.sim.state_hash().total == end.referee_hash);              // the referee's state and both machines': one
        ASSERT_TRUE(ga.hash_at.count(end.ticks) == 1 && ga.hash_at[end.ticks] == end.referee_hash && gb.hash_at.count(end.ticks) == 1 && gb.hash_at[end.ticks] == end.referee_hash);
        ASSERT_TRUE(hashes_agree(ga, gb));
        ASSERT_FALSE(ga.net.desynced() || gb.net.desynced());
        w.between = nullptr;
    } TEST_END();
}

int main() {
    NetGame::default_prediction_budget_ns() = UINT64_MAX;      // (a busy machine stalls the test process now and then: that is not the prediction's cost, and no test is to lose its prediction to it)
    std::cout << "\n=======================================================\n [SUITE] Network port: the way back of a NetGame (real sockets, a real server)\n"
                 "=======================================================\n";
    run_way_back_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
