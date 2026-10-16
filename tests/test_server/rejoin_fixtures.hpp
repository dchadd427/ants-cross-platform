// The fixtures of the way-back test (test_rejoin.cpp holds main() and the cases): real NetGames over loopback sockets against a real RoomManager. Inline or in an anonymous namespace.
#pragma once
#include "ants_assets/lvl_parser.hpp"
#include "ants_ai/bot_names.hpp"
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
#include "../common/test_assert.hpp"

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

// The name that the room with this code gives the bot of a seat (protocol 20): "Pip (bot, Hard)"
inline std::string bn(const std::string& code, unsigned seat, const char* level) {
    return ai::bot_label(ai::bot_seat_name(ai::bot_name_key(code), static_cast<uint8_t>(seat)), level);
}

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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


namespace {

using net::NetGame;

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// The create block (protocol 15) of a room for `seats` on `map` (a file name as the server lists it, "" for the server's own choice): what makes a public room
inline net::CreateBlock block_of(const std::string& map = "", uint8_t seats = 2) {
    net::CreateBlock b;
    b.map_name = map;
    b.seats = seats;
    return b;
}

inline long process_id() {
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

inline const fs::path& scratch_root() {
    static ScratchRoot root;
    return root.path();
}

inline std::string temp_dir_for(const char* tag) {
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
inline RoomSpec held_spec(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
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
    // The counters of the door (Server::accepted, refused, scripted) are counted when the listener's accept finds a link, and a busy machine (a Mac's loopback is handled by a kernel thread, and a CI runner has
    // other jobs) can be late with that by far more than the 10 ms of real time in which a test runs 3 s of game time. So a test that asserts a count the game clock has made certain (an attempt of the way back
    // was made, so the door must have seen it) WAITS for it: in real time, with the game clock standing still, so that nothing times out meanwhile. `real_ms` is a deadline on the steady clock.
    bool wait_real(const std::function<bool()>& cond, uint32_t real_ms = 20000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(real_ms);
        while (!cond()) {
            if (std::chrono::steady_clock::now() >= end) return false;
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    // ... and a count that is taken as "what there was", to show that nothing comes after it, is taken once the door is quiet: no counter has moved for `quiet_ms` of real time (a link that was made before is
    // counted before it, not after)
    void settle(uint32_t quiet_ms = 200) {
        const auto total = [this]() { return server.accepted + server.refused + server.scripted; };
        uint32_t seen = total();
        auto since = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - since < std::chrono::milliseconds(quiet_ms)) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (total() != seen) {
                seen = total();
                since = std::chrono::steady_clock::now();
            }
        }
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

inline bool all_equal(const std::vector<Machine*>& ms) {
    for (size_t i = 1; i < ms.size(); ++i) {
        if (ms[i]->sim.state_hash() != ms[0]->sim.state_hash()) return false;
    }
    return true;
}

// The states agree: every tick that two machines both ran live has the same hash on both
inline bool hashes_agree(const Machine& a, const Machine& b) {
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


void run_way_back_tests();
