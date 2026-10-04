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
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
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
                scripted_pending_[i]->send(net::encode(net::RejectMsg{script_reason}));
                scripted_pending_[i]->close();
                scripted_links_.push_back(scripted_pending_[i]);
                ++scripted;
                scripted_pending_.erase(scripted_pending_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
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
    uint32_t accepted{0};                       // the connections that the listener accepted, whatever the door did with them
    uint32_t refused{0};
    uint32_t scripted{0};
    std::vector<net::HelloMsg> scripted_hellos; // what the scripted connections said (the tests look at the Hello: the key, the turns)
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

    explicit Machine(std::string n, uint32_t seed = 1) : name(std::move(n)), rng(seed) {
        net.set_discovery(0);
        net.set_on_tick([this]() {
            ++ticks;
            hash_at[sim.current_tick()] = sim.state_hash().total;
        });
    }

    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
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

    explicit World(const char* tag = nullptr, ServerLimits limits = ServerLimits()) : server(tag, std::move(limits)) {}

    uint32_t server_now() const { return server.now(now); }

    Machine& add_machine(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name, static_cast<uint32_t>(machines.size() + 1) * 7919u));
        return *machines.back();
    }
    // A new machine joins the room (the TCP link is made at once; the room answers as the clock runs)
    Machine& join(const std::string& name, const std::string& room, uint8_t want_seat = 255) {
        Machine& m = add_machine(name);
        if (!m.net.join("127.0.0.1", server.port(), name, want_seat, room)) throw std::runtime_error("join failed");
        const uint32_t before = server.accepted;
        run_until([&]() { return server.accepted > before; }, 2000);          // (the machine's link is the newest one that the door accepted: a test can cut it)
        return m;
    }
    void pump() {
        server.pump(now);
        for (auto& m : machines) m->frame(now);
    }
    // 10 ms of game time per step with a moment of real time so that the kernel can deliver the loopback bytes
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {            // (counted, not compared with an end time: the clock of a test may wrap)
            now += 10;
            pump();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
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
        ASSERT_TRUE(w.server.cut_newest());                                              // Bob's cable
        ASSERT_TRUE(w.run_until([&]() { return w.status("RJ-1").paused; }, 2000));        // the room sees it and pauses for everybody
        RoomStatus s = w.status("RJ-1");
        ASSERT_TRUE(s.absent.size() == 1 && s.absent[0].seat == bob);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);                               // the machine does not leave the match
        bool saw_catching_up = false;
        ASSERT_TRUE(w.run_until([&]() {
            const RoomStatus st = w.status("RJ-1");
            for (const RoomStatus::Absent& e : st.absent) saw_catching_up = saw_catching_up || (e.catching_up && e.seat == bob);
            return !st.paused;
        }, 15000));
        ASSERT_TRUE(saw_catching_up);                                                    // it was given the match again, not just let back in
        s = w.status("RJ-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.drops_by_vote == 0 && s.drops_by_cap == 0);
        ASSERT_EQ(w.server.accepted, 3u);                                                // one new link, made by the NetGame itself
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        ASSERT_FALSE(b.saw(NetGame::Event::Type::HostLeft) || b.saw(NetGame::Event::Type::Failed) || b.saw(NetGame::Event::Type::Desync));
        ASSERT_TRUE(b.net.turns_executed() >= b_turns);
        w.run(4000);
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
        ASSERT_EQ(w.server.report.items.size(), size_t{1});
        ASSERT_TRUE(w.server.report.items[0].outcome == RestoreItem::Outcome::Restored && w.server.report.items[0].code == "RJ-2" && w.server.report.items[0].turns == sealed);
        s = w.status("RJ-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && s.absent.size() == 2 && s.turns == sealed);       // the room is back, every seat held
        ASSERT_TRUE(w.run_until([&]() { return !w.status("RJ-2").paused; }, 60000));
        s = w.status("RJ-2");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 2 && s.absent.empty() && s.drops_by_cap == 0 && s.drops_by_vote == 0);
        ASSERT_EQ(a.net.phase(), NetGame::Phase::Playing);
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        w.run(5000);
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
            ASSERT_TRUE(w.server.report.items.size() == 1 && w.server.report.items[0].outcome == RestoreItem::Outcome::Restored && w.server.report.items[0].turns == sealed - 40);
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
            ASSERT_EQ(b.count(NetGame::Event::Type::StartRequested), size_t{2});
            w.server.door = Server::Door::Open;
            const uint32_t seen = w.server.accepted;
            w.run(10000);
            ASSERT_EQ(w.server.accepted, seen);                                          // nothing tries again
            ASSERT_EQ(b.net.phase(), NetGame::Phase::Over);
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
        for (uint32_t waited = 0; waited < 30000; waited += 10) {
            w.run(10);
            const RoomStatus st = w.status("RJ-9");
            seen_countdown = seen_countdown || st.resume_s > 0;
            paused_ms = st.paused ? paused_ms + 10 : 0;
            if (st.paused && st.rejoins == 0) ASSERT_FALSE(b.net.predicting());          // the way back
            if (paused_ms >= 200) ASSERT_FALSE(a.net.predicting() || b.net.predicting());   // the match is held, whoever is back already
            if (!st.paused) break;
        }
        ASSERT_TRUE(seen_countdown);                                                     // (the countdown after the pause was part of it)
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

    TEST_CASE("RJ1.11 A Link That Cannot Be Made (The Name Does Not Resolve, The Network Is Down) Is Told To The Session: The Next Attempt Is Two Seconds Later And Not At The Next Frame (A Lookup Every Frame Would Freeze The Game), And The First Link That Can Be Made Brings The Machine Back") {
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
        const uint32_t seen = w.server.accepted;
        w.run(5000);
        ASSERT_EQ(w.server.accepted, seen);                                              // and nothing tries again
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
