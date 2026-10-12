// Tests of the dedicated game server (docs/NETWORK_PORT.md): the map store, the room manager and its door (Hello routing), a room from the first Hello to the result
// with a host that has no seat, the control interface's calls, and one run over real sockets.
// The fixtures that the parts of the server test share (test_server.cpp holds main(); the cases are in the test_server_*.cpp files and the headers of the same folder). Everything
// is inline or in an anonymous namespace, so every part is a self-contained translation unit.
#pragma once
#include "ants_assets/lvl_parser.hpp"
#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_net/tcp.hpp"
#include "ants_net/wire.hpp"
#include "ants_net/ws.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/control.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/secret.hpp"
#include "ants_server/site_stats.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
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
#include <windows.h>
#include <realtimeapiset.h>
#include <process.h>
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif
#include <cstring>
#include "ants_test_paths.hpp"
#include "../common/test_assert.hpp"
#ifdef ANTS_HAS_SERVER_BINARY
namespace ants_test_paths {
const char* server_dir();                                       // (a tiny source that CMake generates: tests/test_server/CMakeLists.txt)
}
// The program of the process tests: in the folder that CMake names (the Makefile and Ninja generators), or in the folder of a configuration inside it (Visual Studio, Xcode)
inline std::string server_binary_path() {
    const std::filesystem::path dir = ants_test_paths::server_dir();
    for (const char* sub : {"", "Release", "RelWithDebInfo", "MinSizeRel", "Debug"}) {
        const std::filesystem::path candidate = dir / sub / "ants_server";
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
    return (dir / "ants_server").string();
}
#define ANTS_SERVER_BINARY (server_binary_path())
#endif

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

// The first turn of a match is sealed this long after the match began (protocol 12: the "Get ready to play!" dialog of every machine, in which no simulation runs): a test that wants play to
// be under way waits this much longer than it did before
constexpr uint32_t kPre = net::kMatchStartDelayMs;

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

#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " << (msg) << " (" #cond ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

// A connection that forwards to one that somebody else owns (the loopback network owns its ends; the manager wants to own what it is given)
class Borrowed final : public net::Connection {
public:
    explicit Borrowed(net::Connection* c) : c_(c) {}
    bool send(const std::vector<uint8_t>& m) override { return c_->send(m); }
    bool poll(std::vector<uint8_t>& m) override { return c_->poll(m); }
    State state() const override { return c_->state(); }
    void close() override { c_->close(); }

private:
    net::Connection* c_;
};

// A connection that forwards to one that somebody else owns and tells what is sent through it, in order (what the server sends to a machine: a test looks at the world at that moment)
class TapSend final : public net::Connection {
public:
    TapSend(net::Connection* c, const std::function<void(const std::vector<uint8_t>&)>* tap) : c_(c), tap_(tap) {}
    bool send(const std::vector<uint8_t>& m) override {
        if (tap_ != nullptr && *tap_) (*tap_)(m);
        return c_->send(m);
    }
    bool poll(std::vector<uint8_t>& m) override { return c_->poll(m); }
    State state() const override { return c_->state(); }
    void close() override { c_->close(); }

private:
    net::Connection* c_;
    const std::function<void(const std::vector<uint8_t>&)>* tap_;
};

// A connection that hands the game one message every `period_ms` at the most (0: everything that is there): a slow downlink. What is not handed over waits in the link.
class Throttled final : public net::Connection {
public:
    Throttled(net::Connection* inner, const uint32_t* clock) : inner_(inner), clock_(clock) {}
    bool send(const std::vector<uint8_t>& m) override { return inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override {
        if (period_ms != 0 && *clock_ - last_ < period_ms) return false;
        if (!inner_->poll(m)) return false;
        last_ = *clock_;
        return true;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t period_ms{0};

private:
    net::Connection* inner_;
    const uint32_t* clock_;
    uint32_t last_{0};
};

// A player: a client lobby that asks for a room, loads the match, reports, and plays with a session of its own. It behaves like the application does.
struct Client {
    std::string name;
    std::string room;
    uint8_t want_seat{255};
    std::optional<net::CreateBlock> create;  // the create block of the Hello (protocol 15): what makes a public room on a server that offers them
    uint8_t platform{0};
    uint8_t kind{net::kClientGame};          // protocol 16: what the Hello says it is: a game, or the front page's lobby (a page cannot play a match)
    net::SeatKey key{};                      // the key that the Hello shows (a game that takes the seat of a page over, a page that was reloaded)
    net::Connection* end{nullptr};
    net::Connection* server_end{nullptr};    // the other end of the link (the server's): open until the server closes it, whatever this client has or has not read
    std::unique_ptr<net::ClientLobby> lobby;
    sim::SimulationEngine sim;
    std::unique_ptr<net::ClientSession> session;
    bool fail_load{false};
    bool asleep{false};                      // the machine is asleep: its link is open and it says nothing (nothing of it runs)
    bool record_hashes{false};               // the state hash after every tick that the session runs, by tick (compared with the referee's at the end)
    std::map<uint64_t, uint64_t> hash_at;
    std::vector<net::ChatLine> room_chat;    // every line that the room said to this player before the match (a guest's line, the room's own notices)
    std::vector<net::ChatMsg> chats;         // the chat of the match
    bool record_commands{false};             // every command that a turn applies on this machine, with the tick count at the time (the turn stream as this player's engine sees it)
    std::vector<std::pair<uint64_t, sim::Command>> saw;
    bool freeze{false};                      // the session is no longer run: no acks, no orders (a seat that stopped executing the turns)
    uint32_t clock_lag{0};                   // the application's network clock never advances more than a second per frame: what a window that stood still lost of the real time
    uint32_t last_frame_ms{0};               // and when the session's last frame ran
    bool lost{false};
    uint32_t next_order_ms{0};
    uint32_t rng{1};
    uint32_t map_w{40};
    uint32_t map_h{40};

    void start(net::Connection* client_end, uint32_t seed) {
        end = client_end;
        rng = seed;
        net::ClientLobby::Config cc;
        cc.name = name;
        cc.room = room;
        cc.want_seat = want_seat;
        cc.create = create;
        cc.platform = platform;
        cc.client_kind = kind;
        cc.key = key;
        lobby = std::make_unique<net::ClientLobby>(end, cc);
    }
    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    void update(uint32_t now_ms, const std::string& maps) {
        if (lobby == nullptr || asleep) return;
        lobby->update(now_ms);
        for (net::ChatLine& line : lobby->take_chat()) room_chat.push_back(std::move(line));
        for (const net::ClientLobby::Event& ev : lobby->take_events()) {
            if (ev.type == net::ClientLobby::Event::Type::StartRequested) {
                const net::StartMsg& s = lobby->start_info();
                assets::LevelData level;
                uint64_t hash = 0;
                const bool ok = !fail_load && level.load_from_file(maps + "/" + s.map_name) && net::hash_file(maps + "/" + s.map_name, hash) && hash == s.map_hash;
                if (ok) {
                    sim.set_fog_of_war_enabled(s.fog);
                    sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
                    sim.init(level, s.seed, s.roster);
                    sim::apply_start_teams(sim, s.teams());                   // (the teams of the Start, before the first tick: what the application does)
                    map_w = level.width();
                    map_h = level.height();
                }
                lobby->report_loaded(ok);
            } else if (ev.type == net::ClientLobby::Event::Type::Begun) {
                net::ClientSession::Config sc;
                sc.player = lobby->my_seat();
                sc.host = net::kNoSeat;
                sc.migration = false;
                session = std::make_unique<net::ClientSession>(sim, sc);
                session->set_connection(end);
                session->set_on_chat([this](const net::ChatMsg& m) { chats.push_back(m); });
                if (record_hashes) session->runner().set_on_tick([this]() { hash_at[sim.current_tick()] = sim.state_hash().total; });
                if (record_commands) session->runner().set_on_command([this](const sim::Command& c, const sim::CommandResult&) { saw.emplace_back(sim.current_tick(), c); });
                session->start(now_ms);
            }
        }
        if (session && !freeze) {
            if (last_frame_ms != 0 && now_ms - last_frame_ms > 1000) clock_lag += now_ms - last_frame_ms - 1000;      // a frame after a stop hands the network one second at the most
            last_frame_ms = now_ms;
            session->update(now_ms - clock_lag);
            if (sim.is_match_over()) session->finish();                                  // the application does this when its simulation says the match is over (check_match_over): a server that closes the link after it is no loss
            if (session->lost()) lost = true;
            if (now_ms >= next_order_ms && session->mode() == net::ClientSession::Mode::Normal) {
                next_order_ms = now_ms + 700;
                std::vector<uint32_t> mine;
                for (const auto& a : sim.get_world_state().ants) {
                    if (a.player_id == session->player()) mine.push_back(a.id);
                }
                if (!mine.empty()) {
                    sim::Command c;
                    c.type = (next_random() % 4 == 0) ? sim::CommandType::Hatch : sim::CommandType::GroupMove;
                    if (c.type == sim::CommandType::GroupMove) {
                        c.tile_x = static_cast<int16_t>(next_random() % map_w);
                        c.tile_y = static_cast<int16_t>(next_random() % map_h);
                        for (size_t i = 0; i < mine.size() && i < 6; ++i) c.ants.push_back(mine[(i + next_random()) % mine.size()]);
                    }
                    session->submit(c);
                }
            }
        }
    }
};

// A manager with a loopback network and some clients
struct World {
    net::LoopbackNetwork net{5};
    RoomManager mgr;
    std::vector<std::unique_ptr<Client>> clients;
    uint32_t now{1000};

    explicit World(ServerLimits limits = ServerLimits(), const std::string& dir = maps_dir()) : mgr(MapStore(dir), limits) {}

    std::vector<std::unique_ptr<Throttled>> throttles;                      // the slow downlinks of connect_throttled (index = the order they were made in)
    std::optional<net::CreateBlock> next_create;                            // the block that the next connect() puts in its Hello (connect_creating sets it for one call)
    uint8_t next_platform{0};                                               // the platform byte that the next connect() puts in its Hello (0: not told); it stays until it is set again
    uint8_t next_kind{net::kClientGame};                                    // the client kind of the next connect()'s Hello (protocol 16), and the key it shows: connect_page and connect_game set them for one call
    net::SeatKey next_key{};
    bool next_fail_load{false};                                             // the next connect()'s machine cannot load the map (connect_game sets it for one call)

    Client& connect(const std::string& name, const std::string& room, uint8_t seat = 255, net::LoopbackNetwork::Link link = {20, 10}, Throttled** throttle = nullptr) {
        auto ends = net.connect(link);
        mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
        clients.push_back(std::make_unique<Client>());
        Client& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.create = next_create;
        c.platform = next_platform;
        c.kind = next_kind;
        c.key = next_key;
        c.fail_load = next_fail_load;
        c.server_end = ends.first;
        net::Connection* end = ends.second;
        if (throttle != nullptr) {
            throttles.push_back(std::make_unique<Throttled>(ends.second, &now));
            *throttle = throttles.back().get();
            end = throttles.back().get();
        }
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    // A Hello that carries a create block: on a server that offers public rooms it makes the room when there is none. The room is made before the call returns: the Hellos of two links that are sent in the
    // same millisecond arrive in either order (the links jitter), and a guest whose Hello comes first has no room yet (a guest's Hello has no block: the front page's links carry it, these do not)
    Client& connect_creating(const std::string& name, const std::string& room, const net::CreateBlock& block, uint8_t seat = 255) {
        next_create = block;
        Client& c = connect(name, room, seat);
        next_create.reset();
        run(100);
        return c;
    }
    // The front page (protocol 16): a Hello that says "page" and carries the lobby block; it makes the lobby room when there is none (the server must offer lobbies), else it is seated in it. Runs 100 ms, as connect_creating.
    Client& connect_page(const std::string& name, const std::string& room, const std::optional<net::CreateBlock>& block, uint8_t seat = 255) {
        next_kind = net::kClientPage;
        next_create = block;
        Client& c = connect(name, room, seat);
        next_kind = net::kClientGame;
        next_create.reset();
        run(100);
        return c;
    }
    // A game that takes the seat of a page over (or of a game whose link was lost) with the seat's key, as the page's own game does
    Client& connect_game(const std::string& name, const std::string& room, const net::SeatKey& key, bool cannot_load = false) {
        next_key = key;
        next_fail_load = cannot_load;                                        // (a machine that cannot load the map: it must say so to the very first Start, which the 100 ms below can bring)
        Client& c = connect(name, room);
        next_key = net::SeatKey{};
        next_fail_load = false;
        run(100);
        return c;
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {            // (counted, not compared with an end time: the clock of a test may wrap)
            now += 10;
            net.set_time(now);
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
        }
    }
    // The same in steps of `step` ms, for what a server that has run for weeks went through (a day is 144 steps of ten minutes): the links must be able to be quiet for a step (the silence limit above it)
    void run_coarse(uint64_t ms, uint32_t step) {
        for (uint64_t elapsed = 0; elapsed < ms; elapsed += step) {
            now += static_cast<uint32_t>(std::min<uint64_t>(step, ms - elapsed));
            net.set_time(now);
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
        }
    }
    RoomStatus status(const std::string& code) {
        RoomStatus s;
        mgr.status(code, s, now);
        return s;
    }
};

// A create block for a room of `seats` on `map` (a file name as the server lists it, "" for the server's own choice), with the teams (255 = free for all) and the leader-starts flag.
inline net::CreateBlock block_of(const std::string& map = "", uint8_t seats = 2, uint8_t team_a = net::kNoTeam, uint8_t team_b = net::kNoTeam, bool leader_starts = false) {
    net::CreateBlock b;
    b.map_name = map;
    b.seats = seats;
    b.team_a = team_a;
    b.team_b = team_b;
    b.flags = leader_starts ? net::kCreateLeaderStarts : uint8_t{0};
    return b;
}

// The create block of the front page's lobby room (protocol 16): four seats, started by its leader, a lobby; the map when the page names one ("" for the server's own choice)
inline net::CreateBlock lobby_block_of(const std::string& map = "") {
    net::CreateBlock b = block_of(map, 4, net::kNoTeam, net::kNoTeam, true);
    b.flags = static_cast<uint8_t>(b.flags | net::kCreateLobby);
    return b;
}

inline RoomSpec spec_of(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
    RoomSpec s;
    s.code = code;
    s.map = map;
    s.players = players;
    s.has_seed = true;
    s.seed = 4242;
    return s;
}

#ifndef _WIN32
// A raw client that is no game: it says Hello for a room and then writes frames of one message as fast as the kernel takes them, until a write fails or it is stopped. It never
// reads, except in server_closed().
struct FloodPeer {
    int sock{-1};
    std::thread thread;
    std::atomic<bool> stop{false};

    FloodPeer(uint16_t port, const std::string& room, const std::vector<uint8_t>& message) {
        sock = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
            ::close(sock);
            sock = -1;
            return;
        }
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        const auto frame = [](const std::vector<uint8_t>& m) {
            std::vector<uint8_t> f;
            const uint32_t n = static_cast<uint32_t>(m.size());
            for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xFFu));
            for (const uint8_t b : m) f.push_back(b);                                   // (byte by byte: GCC 12 reads a range insert here as an overread and the project builds with -Werror)
            return f;
        };
        net::HelloMsg hello;
        hello.name = "Evil";
        hello.room = room;
        std::vector<uint8_t> first = frame(net::encode(hello));
        std::vector<uint8_t> blob;
        const std::vector<uint8_t> one_frame = frame(message);
        for (int i = 0; i < 20000; ++i) blob.insert(blob.end(), one_frame.begin(), one_frame.end());
        thread = std::thread([this, first, blob]() {
#ifdef MSG_NOSIGNAL
            const int flags = MSG_NOSIGNAL;
#else
            const int flags = 0;
#endif
            const auto write_all = [&](const std::vector<uint8_t>& data) {
                size_t off = 0;
                while (off < data.size() && !stop) {
                    const auto n = ::send(sock, data.data() + off, data.size() - off, flags);
                    if (n <= 0) return false;
                    off += static_cast<size_t>(n);
                }
                return !stop;
            };
            if (!write_all(first)) return;
            while (!stop) {
                if (!write_all(blob)) return;                        // (a write to a connection that the server closed need not fail at once: see server_closed())
            }
        });
    }
    // What the server has sent so far, counted: the Pongs (a flooder that reads nothing would stall its own connection). Nothing here depends on a close being seen: a connection
    // that the server closed while the peer kept writing can stay half open in the kernel for a long time (macOS shows it ESTABLISHED), the server has forgotten it.
    void read_server(uint32_t& pongs) {
        uint8_t buf[4096];
        for (int i = 0; i < 1000; ++i) {
            const auto n = ::recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
            if (n <= 0) break;
            received.insert(received.end(), buf, buf + n);
        }
        size_t pos = 0;
        while (received.size() - pos >= 4) {
            const uint32_t len = static_cast<uint32_t>(received[pos]) | (static_cast<uint32_t>(received[pos + 1]) << 8) | (static_cast<uint32_t>(received[pos + 2]) << 16) |
                                 (static_cast<uint32_t>(received[pos + 3]) << 24);
            if (received.size() - pos - 4 < len) break;
            if (len >= 1 && received[pos + 4] == static_cast<uint8_t>(net::MsgType::Pong)) ++pongs;
            pos += 4 + len;
        }
        received.erase(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    std::vector<uint8_t> received;
    ~FloodPeer() {
        stop = true;
        if (sock >= 0) ::shutdown(sock, SHUT_RDWR);
        if (thread.joinable()) thread.join();
        if (sock >= 0) ::close(sock);
    }
};

// CPU seconds (user and system) of this process so far, and the most memory it has held, in MB
inline double process_cpu_seconds() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1.0e6;
}
inline double peak_memory_mb() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);      // (bytes on macOS)
#else
    return static_cast<double>(ru.ru_maxrss) / 1024.0;                  // (kilobytes elsewhere)
#endif
}
#endif

#ifdef _WIN32
// How many cycles the cycle counter of a thread counts in a millisecond, measured once against the wall clock: the quickest of eight spins of 6 ms (a spin that another program interrupted reads
// too few cycles for its time). 0 when the system has no such counter.
inline double thread_cycles_per_ms() {
    static const double rate = []() {
        double best = 0.0;
        for (int i = 0; i < 8; ++i) {
            ULONG64 begin = 0;
            ULONG64 end = 0;
            const auto t0 = std::chrono::steady_clock::now();
            if (!QueryThreadCycleTime(GetCurrentThread(), &begin)) return 0.0;
            volatile uint64_t spin = 0;
            while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(6)) spin = spin + 1;
            if (!QueryThreadCycleTime(GetCurrentThread(), &end)) return 0.0;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            best = std::max(best, static_cast<double>(end - begin) / ms);
        }
        return best;
    }();
    return rate;
}
#endif

// The CPU time (user and system, milliseconds) that the calling thread has used so far; -1 when the system cannot say. It does not run while the thread waits for the machine or sleeps, so the
// difference of two readings is the thread's own work, however busy the machine of the test is (a wall clock around a call counts the time that other programs took, too). On Windows it is the
// thread's cycle counter, which counts what the thread ran: GetThreadTimes counts in clock ticks of 15.6 ms and, on some of the hosted runners (seen on Windows Server 2022 hosts with AMD EPYC 9V45 processors, October 2026),
// posts what a thread used in lumps of up to seconds to whichever pass is running at the time, so that a pass of a tenth of a millisecond read as 300 ms (the total over a second is right).
inline double thread_cpu_ms() {
#ifdef _WIN32
    const double cycles_per_ms = thread_cycles_per_ms();
    ULONG64 cycles = 0;
    if (cycles_per_ms > 0.0 && QueryThreadCycleTime(GetCurrentThread(), &cycles)) return static_cast<double>(cycles) / cycles_per_ms;
    FILETIME created, exited, kernel, user;                                // (no cycle counter: the clock ticks)
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1.0;
    const auto hundred_ns = [](const FILETIME& t) { return static_cast<double>((static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime); };
    return (hundred_ns(kernel) + hundred_ns(user)) / 10000.0;
#else
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return -1.0;
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
#endif
}

// A folder of this process alone, `ants_server_test_<pid>_<random hex>`, made new (create_directory says no when the name exists, and another is drawn) and removed with everything in it when
// the object goes. The folders of the suite used to be `ants_server_test_<tag>` and `ants_secret_test_<tag>` under the temp folder, the same for every run on the machine: two runs at the
// same time (two checkouts, a developer and a CI job) removed each other's files, and S3.1, S3.18 and S3.21 failed when they overlapped.
inline long process_id() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

class ScratchRoot {
public:
    ScratchRoot() {
        std::random_device entropy;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(entropy() & 0xFFFFFFu));
            const fs::path candidate = fs::temp_directory_path() / ("ants_server_test_" + std::to_string(process_id()) + "_" + suffix);
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

// The folder of this run (made at the first use, removed when the program ends)
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

inline void write_bytes(const fs::path& p, size_t n, char fill) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    const std::string chunk(4096, fill);
    for (size_t done = 0; done < n; done += chunk.size()) out.write(chunk.data(), static_cast<std::streamsize>(std::min(chunk.size(), n - done)));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Reconnect (protocol 10): the machines of players whose connections are lost, as the game will behave in release B, and a world that has them
// ---------------------------------------------------------------------------------------------------------------------------------

// Where a machine gets a new link to the server when its session asks for one (a world: the loopback network of the tests, or real sockets)
struct LinkSource {
    virtual ~LinkSource() = default;
    virtual net::Connection* open_link() = 0;
};

// A player: a client lobby that asks for a room, loads the match, reports, and plays with a session of its own, and, when the link to the server is lost, asks the world for a new one
// (the session says when) and says Hello with its key: what the application does
struct RClient {
    std::string name;
    std::string room;
    uint8_t want_seat{255};
    net::SeatKey key{};                      // a machine that starts from nothing (a page that was reloaded) is given the key that its page kept: its lobby says Hello with it
    net::Connection* end{nullptr};           // the link that the machine uses now
    std::unique_ptr<net::ClientLobby> lobby;
    sim::SimulationEngine sim;
    std::unique_ptr<net::ClientSession> session;
    bool reconnects{true};                   // its session may open a new link when it wants one
    bool hung{false};                        // the machine does not run (a frozen window): no frames, no acknowledgements, no pings
    uint32_t frame_every_ms{0};              // a slow machine: it runs a frame this often only (0: at every pass of the world)
    bool orders{true};                       // it gives orders (a person's clicks)
    bool lost{false};
    bool was_rejected{false};
    net::RejectReason rejected{net::RejectReason::BadRequest};
    uint32_t catch_up_ticks{200};
    uint32_t clock_lag{0};                   // the application's network clock never advances more than a second per frame: what a window that stood still lost of the real time
    uint32_t last_frame_ms{0};
    uint32_t next_order_ms{0};
    uint32_t rng{1};
    uint32_t map_w{40};
    uint32_t map_h{40};
    std::vector<net::ChatMsg> chats;
    std::vector<net::ChatLine> room_chat;    // what the room said before the match
    bool record_hashes{false};               // the state hash after every tick that the session runs, by tick
    std::map<uint64_t, uint64_t> hash_at;
    std::function<void(sim::SimulationEngine&)> tamper;      // runs on the engine when the machine has loaded the map (to make its state differ)
    bool fail_load{false};                   // the machine cannot load the map: it says so, and the start is cancelled
    uint16_t listen_port{0};                 // the port that the machine announces for the other guests (a game on the local network does; a server's room never uses it)
    std::optional<net::CreateBlock> create;  // the create block of its Hello (protocol 15): what makes a public room on a server that offers them
    uint8_t platform{0};

    void start(net::Connection* client_end, uint32_t seed) {
        end = client_end;
        rng = seed;
        net::ClientLobby::Config cc;
        cc.name = name;
        cc.room = room;
        cc.want_seat = want_seat;
        cc.key = key;
        cc.listen_port = listen_port;
        cc.create = create;
        cc.platform = platform;
        lobby = std::make_unique<net::ClientLobby>(end, cc);
    }
    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    void update(uint32_t now_ms, const std::string& maps, LinkSource& w);
};

struct RWorld : LinkSource {
    net::LoopbackNetwork net{5};
    RoomManager mgr;
    std::vector<std::unique_ptr<RClient>> clients;
    uint32_t now{1000};
    net::LoopbackNetwork::Link link{20, 10};

    explicit RWorld(ServerLimits limits = ServerLimits()) : mgr(MapStore(maps_dir()), limits) {}

    // a new link to the server: the door's end is the manager's, this end is the caller's
    net::Connection* open_link() override {
        auto ends = net.connect(link);
        mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
        return ends.second;
    }
    std::optional<net::CreateBlock> next_create;                             // the block that the next connect() puts in its Hello (connect_creating sets it for one call)
    RClient& connect(const std::string& name, const std::string& room, uint8_t seat = 255, const net::SeatKey& key = net::SeatKey{}) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.key = key;
        c.create = next_create;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    RClient& connect_creating(const std::string& name, const std::string& room, const net::CreateBlock& block, uint8_t seat = 255) {
        next_create = block;
        RClient& c = connect(name, room, seat);
        next_create.reset();
        return c;
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {            // (counted, not compared with an end time: the clock of a test may wrap)
            now += 10;
            net.set_time(now);
            mgr.update(now);
            for (size_t i = 0; i < clients.size(); ++i) clients[i]->update(now, maps_dir(), *this);
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            run(10);
        }
        return cond();
    }
    RoomStatus status(const std::string& code) {
        RoomStatus s;
        mgr.status(code, s, now);
        return s;
    }
    // the link of a machine is cut: both ends see it closed
    void cut(RClient& c) { net.cut(c.end); }
    // plays the match to its end (the clock of the map) and a little beyond it
    void play_to_the_end(const std::string& code) {
        for (int guard = 0; guard < 4000 && status(code).state == RoomState::Running; ++guard) run(250);
        run(Room::kGraceMs + 500);
    }
};

inline void RClient::update(uint32_t now_ms, const std::string& maps, LinkSource& w) {
    if (hung) return;
    if (lobby && !session) {
        lobby->update(now_ms);
        for (net::ChatLine& line : lobby->take_chat()) room_chat.push_back(std::move(line));
        for (const net::ClientLobby::Event& ev : lobby->take_events()) {
            if (ev.type == net::ClientLobby::Event::Type::StartRequested) {
                const net::StartMsg& s = lobby->start_info();
                assets::LevelData level;
                uint64_t hash = 0;
                const bool ok = !fail_load && level.load_from_file(maps + "/" + s.map_name) && net::hash_file(maps + "/" + s.map_name, hash) && hash == s.map_hash;
                if (ok) {
                    sim.set_fog_of_war_enabled(s.fog);
                    sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
                    sim.init(level, s.seed, s.roster);
                    sim::apply_start_teams(sim, s.teams());                   // (the teams of the Start, before the first tick: what the application does)
                    if (tamper) tamper(sim);
                    map_w = level.width();
                    map_h = level.height();
                }
                lobby->report_loaded(ok);
            } else if (ev.type == net::ClientLobby::Event::Type::Rejected) {
                rejected = lobby->reject_reason();
                was_rejected = true;
                lost = true;
            } else if (ev.type == net::ClientLobby::Event::Type::Begun) {
                net::ClientSession::Config sc;
                sc.player = lobby->my_seat();
                sc.host = net::kNoSeat;
                sc.migration = false;
                sc.reconnect = !net::key_is_zero(lobby->key());      // a room that holds seats gave it a key; a room that does not gave none: no way back
                sc.key = lobby->key();
                sc.hello.name = name;
                sc.hello.room = room;
                sc.rejoin = lobby->rejoined();
                sc.catch_up_ticks = catch_up_ticks;
                session = std::make_unique<net::ClientSession>(sim, sc);
                session->set_connection(end);
                session->set_on_chat([this](const net::ChatMsg& m) { chats.push_back(m); });
                if (record_hashes) session->runner().set_on_tick([this]() { hash_at[sim.current_tick()] = sim.state_hash().total; });
                session->start(now_ms);
            }
        }
    }
    if (!session) return;
    if (frame_every_ms != 0 && last_frame_ms != 0 && now_ms - last_frame_ms < frame_every_ms) return;       // a slow machine's frames are far apart
    if (last_frame_ms != 0 && now_ms - last_frame_ms > 1000) clock_lag += now_ms - last_frame_ms - 1000;      // a frame after a stop hands the network one second at the most
    last_frame_ms = now_ms;
    const uint32_t t = now_ms - clock_lag;
    // the application's side of reconnecting: a new link to the door when the session asks for one
    if (reconnects && session->wants_connection(t)) {
        net::Connection* link = w.open_link();
        end = link;
        session->attach(link, t);
    }
    if (sim.is_match_over()) session->finish();               // the application knows when the match is over: a server that closes its links after that is no loss
    session->update(t);
    if (session->lost()) {
        lost = true;
        was_rejected = session->rejected();
        rejected = session->reject_reason();
    }
    if (orders && now_ms >= next_order_ms && session->mode() == net::ClientSession::Mode::Normal && !session->paused()) {
        next_order_ms = now_ms + 700;
        std::vector<uint32_t> mine;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == session->player()) mine.push_back(a.id);
        }
        if (!mine.empty()) {
            sim::Command c;
            c.type = (next_random() % 4 == 0) ? sim::CommandType::Hatch : sim::CommandType::GroupMove;
            if (c.type == sim::CommandType::GroupMove) {
                c.tile_x = static_cast<int16_t>(next_random() % map_w);
                c.tile_y = static_cast<int16_t>(next_random() % map_h);
                for (size_t i = 0; i < mine.size() && i < 6; ++i) c.ants.push_back(mine[(i + next_random()) % mine.size()]);
            }
            session->submit(c);
        }
    }
}

// a room that holds the seats of players whose connections are lost
inline RoomSpec held_spec(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
    RoomSpec s;
    s.code = code;
    s.map = map;
    s.players = players;
    s.has_seed = true;
    s.seed = 4242;
    s.reconnect = true;
    s.resume_countdown_ms = 0;                              // (the countdown after a pause is off in the tests that are not about it: each would wait 10 s after every return; S3.52 is about it)
    return s;
}

// the Reject that a raw connection was sent (0: none yet)
inline int reject_on(net::Connection* c) {
    std::vector<uint8_t> msg;
    net::RejectMsg r;
    while (c->poll(msg)) {
        if (net::peek_type(msg) == net::MsgType::Reject && net::decode(msg, r)) return static_cast<int>(r.reason);
    }
    return 0;
}

inline std::string hex_of(const net::SeatKey& key) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const uint8_t b : key) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 15]);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records (restart_record.hpp, docs/NETWORK_PORT.md "Restart records"): a server that can be stopped and started again over the same folder of records, with machines that
// find it again by themselves, and the helpers of the record tests
// ---------------------------------------------------------------------------------------------------------------------------------

const char* const kTestVersion = "v0.0.0-test";

inline std::vector<uint8_t> read_all_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline net::SeatKey test_key(uint8_t salt) {
    net::SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 37u + i * 11u + 5u);
    return k;
}

// A head with everything in it: three seats, a bot of the leader's fill at seat 2, keys for the two persons
inline RestartHead sample_head(const std::string& code = "REC-1") {
    RestartHead h;
    h.identity.game_version = kTestVersion;
    h.identity.protocol = net::kProtocolVersion;
    h.identity.build_id = "abc1234";
    h.code = code;
    h.map = "TINY.LVL";
    h.map_hash = 0x1122334455667788ull;
    h.players = 3;
    h.fog = false;
    h.early_start = true;
    h.wait_ms = 120000;
    h.load_ms = 60000;
    h.keep_ms = 600000;
    h.run_ms = 7200000;
    h.vote_after_ms = 30000;
    h.max_pause_ms = 1800000;
    h.max_catch_up_ms = 300000;
    h.resume_countdown_ms = 10000;
    h.max_log_bytes = 16ull * 1024 * 1024;
    h.max_connections = 32;
    h.bots.push_back(ai::BotSpec{2, "standard", ai::Level::Hard});
    h.fill_mask = 0x04;
    h.start.seed = 4242;
    h.start.map_name = h.map;
    h.start.map_hash = h.map_hash;
    h.start.fog = false;
    h.start.roster = 0x07;
    h.start.names[0] = "Ann";
    h.start.names[1] = "Bob";
    h.start.names[2] = "Bot (Hard)";
    h.keys[0] = test_key(1);
    h.keys[1] = test_key(2);
    return h;
}

// Turn n of a made-up match: most are empty, some hold orders (a group move of three ants, a hatch, several commands at once, the Drop of a seat)
inline net::TurnMsg sample_turn(uint32_t n) {
    net::TurnMsg t;
    t.turn = n;
    if (n % 3 == 1) {
        sim::Command c;
        c.type = sim::CommandType::GroupMove;
        c.issuer = static_cast<uint8_t>(n % 3);
        c.tile_x = static_cast<int16_t>(n % 40);
        c.tile_y = static_cast<int16_t>((n * 7) % 40);
        c.ants = {n + 1, n + 2, n + 3};
        t.commands.push_back(c);
    }
    if (n % 7 == 2) {
        sim::Command c;
        c.type = sim::CommandType::Hatch;
        c.issuer = 1;
        t.commands.push_back(c);
    }
    if (n % 11 == 5) {
        sim::Command a;
        a.type = sim::CommandType::Stop;
        a.issuer = 0;
        a.ants = {7, 8};
        t.commands.push_back(a);
        sim::Command b;
        b.type = sim::CommandType::AllianceInvite;
        b.issuer = 1;
        b.other_player = 2;
        t.commands.push_back(b);
    }
    if (n == 40) {
        sim::Command d;
        d.type = sim::CommandType::Drop;
        d.issuer = 2;
        t.commands.push_back(d);
    }
    return t;
}

inline bool same_head(const RestartHead& a, const RestartHead& b) {
    return a.identity.game_version == b.identity.game_version && a.identity.protocol == b.identity.protocol && a.identity.build_id == b.identity.build_id && a.code == b.code && a.map == b.map &&
           a.map_hash == b.map_hash && a.players == b.players && a.fog == b.fog && a.early_start == b.early_start && a.public_room == b.public_room && a.wait_ms == b.wait_ms && a.load_ms == b.load_ms && a.keep_ms == b.keep_ms &&
           a.run_ms == b.run_ms && a.vote_after_ms == b.vote_after_ms && a.max_pause_ms == b.max_pause_ms && a.max_catch_up_ms == b.max_catch_up_ms && a.resume_countdown_ms == b.resume_countdown_ms &&
           a.max_log_bytes == b.max_log_bytes && a.max_connections == b.max_connections && a.fill_mask == b.fill_mask && a.keys == b.keys && net::encode(a.start) == net::encode(b.start) &&
           a.bots.size() == b.bots.size() &&
           std::equal(a.bots.begin(), a.bots.end(), b.bots.begin(), [](const ai::BotSpec& x, const ai::BotSpec& y) { return x.seat == y.seat && x.kind == y.kind && x.level == y.level; });
}

inline bool same_turns(const std::vector<net::TurnMsg>& a, const std::vector<net::TurnMsg>& b, size_t count) {
    if (a.size() < count || b.size() < count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (a[i].turn != b[i].turn || a[i].commands.size() != b[i].commands.size()) return false;
        for (size_t c = 0; c < a[i].commands.size(); ++c) {
            if (!(a[i].commands[c] == b[i].commands[c])) return false;
        }
    }
    return true;
}

// A record folder of its own for a test: made new, configured as a server's would be
inline RestartConfig test_restart_config(const char* tag) {
    RestartConfig c;
    c.dir = (fs::path(temp_dir_for(tag)) / "restart").string();
    c.identity.game_version = kTestVersion;
    c.identity.protocol = net::kProtocolVersion;
    c.identity.build_id = "test";
    return c;
}

// A server that can be stopped and started again over the same folder of restart records, and machines that find it again by themselves (RClient), on a loopback network. The server's clock
// is its own (a server that starts again has a new uptime: server_clock at start_server), the machines' clock and the network's are `now`.
struct PWorld : LinkSource {
    net::LoopbackNetwork net{5};
    std::unique_ptr<RoomManager> mgr;
    std::vector<std::unique_ptr<RClient>> clients;
    std::vector<net::Connection*> server_ends;              // the door's ends of every link made since the server started: a server that dies loses them all
    uint32_t now{1000};
    uint32_t server_offset{0};
    net::LoopbackNetwork::Link link{20, 10};
    ServerLimits limits;
    RestartConfig restart;
    std::string maps;
    RestoreReport report;                                   // what the last start of the server decided about the records (start_server waits for the replays: finish_restore)
    std::function<bool()> restore_should_stop;              // the server's stop flag while its records are judged (empty: none)
    std::function<void(RoomManager&)> configure;            // what a test sets up on every new manager before it reads the records (e.g. the replays), empty: nothing
    bool time_updates{false};                               // run() measures the CPU time of every update of the manager (the longest is kept)
    double longest_update_ms{0};
    uint64_t updates_timed{0};
    std::function<void(const std::vector<uint8_t>&)> on_server_send;     // told every message that the server sends on any link (empty: nothing)
    std::vector<std::string> notices;                       // every line that the server would have written to its log, in order
    size_t kept_at_stop{0};
    int starts{0};

    explicit PWorld(const char* tag, ServerLimits l = ServerLimits(), const std::string& maps_folder = maps_dir()) : limits(l), restart(test_restart_config(tag)), maps(maps_folder) {}

    uint32_t server_now() const { return now + server_offset; }
    void collect() {
        if (mgr == nullptr) return;
        for (std::string& line : mgr->take_notices()) notices.push_back(std::move(line));
    }
    // The server starts (its uptime is `server_clock` ms): it reads the folder and judges the records; the replays of the rooms that wait run in slices from update(), and this waits for them (the world's
    // clock stands still meanwhile: nothing else happens), so that what a test looks at is the server after its restore. `wait_for_restore` false: the queue is left as it is, for the tests of the restore itself.
    void start_server(uint32_t server_clock = 500, bool wait_for_restore = true) {
        server_offset = server_clock - now;
        server_ends.clear();
        mgr = std::make_unique<RoomManager>(MapStore(maps), limits);
        std::string why;
        if (!mgr->enable_restart_records(restart, why)) throw std::runtime_error("enable_restart_records: " + why);
        if (configure) configure(*mgr);
        report = mgr->restore_rooms(server_now(), restore_should_stop);
        collect();
        ++starts;
        if (wait_for_restore) finish_restore();
    }
    // Runs the manager (one pass after another, no time passes for the world) until every record has been replayed or refused; `report` then has every record's fate
    void finish_restore() {
        for (int guard = 0; mgr != nullptr && mgr->restoring_count() > 0; ++guard) {
            if (guard > 100000000) throw std::runtime_error("the restore does not end");
            mgr->update(server_now());
        }
        collect();
        if (mgr != nullptr) report = mgr->restore_report();
    }
    // The server stops: told to (SIGTERM: the records are made durable and kept) or not (SIGKILL, a crash: nothing is done); either way every link is gone and what was in flight is lost
    void stop_server(bool graceful) {
        if (graceful) kept_at_stop = mgr->shutdown(server_now());
        collect();
        for (net::Connection* e : server_ends) net.cut(e);
        mgr.reset();
    }
    net::Connection* open_link() override {
        auto ends = net.connect(link);
        if (mgr == nullptr) {                                // nobody listens: the connection fails
            net.cut(ends.first, true);
            return ends.second;
        }
        mgr->add_connection(std::make_unique<TapSend>(ends.first, &on_server_send), "127.0.0.1", server_now());
        server_ends.push_back(ends.first);
        return ends.second;
    }
    uint16_t announce_port{0};                              // the listen port that the machines made by connect() announce
    std::optional<net::CreateBlock> next_create;            // the block that the next connect() puts in its Hello (connect_creating sets it for one call)
    RClient& connect(const std::string& name, const std::string& room, uint8_t seat = 255, const net::SeatKey& key = net::SeatKey{}) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.key = key;
        c.listen_port = announce_port;
        c.create = next_create;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    RClient& connect_creating(const std::string& name, const std::string& room, const net::CreateBlock& block, uint8_t seat = 255) {
        next_create = block;
        RClient& c = connect(name, room, seat);
        next_create.reset();
        return c;
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            net.set_time(now);
            if (mgr != nullptr) {
                const double cpu_before = time_updates ? thread_cpu_ms() : 0.0;
                mgr->update(server_now());
                if (time_updates) {
                    longest_update_ms = std::max(longest_update_ms, thread_cpu_ms() - cpu_before);
                    ++updates_timed;
                }
                collect();
            }
            for (size_t i = 0; i < clients.size(); ++i) clients[i]->update(now, maps, *this);
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            run(10);
        }
        return cond();
    }
    RoomStatus status(const std::string& code) {
        RoomStatus s;
        if (mgr != nullptr) mgr->status(code, s, server_now());
        return s;
    }
    void play_to_the_end(const std::string& code) {
        for (int guard = 0; guard < 4000 && status(code).state == RoomState::Running; ++guard) run(250);
        run(Room::kGraceMs + 500);
    }
    std::vector<std::string> record_files() const { return RestartStore(restart).records(); }
    std::string record_path(const std::string& code) const { return RestartStore(restart).path_for(code); }
    RestartLoaded read_record(const std::string& code) const { return read_restart_record(record_path(code), 1ull << 30); }
};


inline size_t entries_in(const fs::path& dir) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++n;
    }
    return n;
}

// The entries of a restart folder besides its lock file (the file `.lock` is the folder's own since M5 of the review: a store that has prepared the folder holds it)
inline size_t entries_but_lock(const fs::path& dir) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) n += e.path().filename() == ".lock" ? 0u : 1u;
    return n;
}


inline uint32_t le32_at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

// A frame as the format says it (made here from the description, not with the writer): type, length, payload, the CRC of those three
inline std::vector<uint8_t> test_frame(uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back(type);
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>((payload.size() >> (8 * i)) & 0xFFu));
    f.insert(f.end(), payload.begin(), payload.end());
    const uint32_t crc = restart_crc32(f.data(), f.size());
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>((crc >> (8 * i)) & 0xFFu));
    return f;
}

inline std::vector<uint8_t> with_magic(const std::vector<uint8_t>& frames) {
    // (a vector::insert of the 8 bytes of the magic into an empty vector is a false -Warray-bounds / -Wstringop-overflow of GCC 12: the vector is made at its size and filled instead)
    std::vector<uint8_t> out(sizeof(kRestartMagic) + frames.size());
    for (size_t i = 0; i < sizeof(kRestartMagic); ++i) out[i] = static_cast<uint8_t>(kRestartMagic[i]);
    std::copy(frames.begin(), frames.end(), out.begin() + static_cast<std::ptrdiff_t>(sizeof(kRestartMagic)));
    return out;
}

inline std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// The payload of a turns frame, as the format says it
inline std::vector<uint8_t> turns_payload(uint32_t first, const std::vector<net::TurnMsg>& turns) {
    std::vector<uint8_t> p;
    net::ByteWriter w(p);
    w.u32(first);
    w.u16(static_cast<uint16_t>(turns.size()));
    for (const net::TurnMsg& t : turns) {
        w.u16(static_cast<uint16_t>(t.commands.size()));
        for (const sim::Command& c : t.commands) sim::encode(c, p);
    }
    return p;
}

inline std::vector<uint8_t> check_payload(uint32_t turn, uint64_t hash) {
    std::vector<uint8_t> p;
    net::ByteWriter w(p);
    w.u32(turn);
    w.u64(hash);
    return p;
}

// A record file's frames: where each starts and ends (the file is a good one)
inline std::vector<std::pair<size_t, size_t>> frames_of(const std::vector<uint8_t>& file) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t pos = sizeof(kRestartMagic);
    while (pos + 5 <= file.size()) {
        const size_t end = pos + 5 + le32_at(file, pos + 1) + 4;
        if (end > file.size()) break;
        out.emplace_back(pos, end);
        pos = end;
    }
    return out;
}

inline std::vector<uint8_t> head_frame_of(const RestartHead& h) { return encode_restart_head(h); }

inline bool same_checks(const std::vector<RestartCheck>& a, const std::vector<RestartCheck>& b, size_t count) {
    if (a.size() < count || b.size() < count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (a[i].turn != b[i].turn || a[i].hash != b[i].hash) return false;
    }
    return true;
}



// A room of `players` machines that has played `play_ms` of its match (after the five seconds of the start dialog): its record is on disk. The machines are returned in the order of their seats.
inline std::vector<RClient*> play_room(PWorld& w, const RoomSpec& spec, uint32_t play_ms) {
    if (!w.mgr->create_room(spec, w.server_now()).ok) throw std::runtime_error("create_room refused " + spec.code);
    std::vector<RClient*> machines;
    for (uint8_t p = 0; p < spec.players; ++p) {
        machines.push_back(&w.connect(std::string("P") + std::to_string(p), spec.code));
        machines.back()->record_hashes = true;                                              // (the state of every tick that the machine runs, by tick: what the record's checkpoints are compared with)
    }
    w.run(play_ms + kPre);
    if (w.status(spec.code).state != RoomState::Running) {
        const RoomStatus s = w.status(spec.code);
        throw std::runtime_error(std::string("the room is not running: ") + room_state_name(s.state) + ", " + s.reason + ", turns " + std::to_string(s.turns));
    }
    return machines;
}

// The record of `code` as bytes, and the same with one frame replaced (`frame`: its index) by new bytes
inline std::vector<uint8_t> record_bytes(const PWorld& w, const std::string& code) { return read_all_bytes(w.record_path(code)); }

// The independent replay of a record: an engine made the way a client makes one from the start message, run through the turns with the engine's own two calls
inline uint64_t replay_hash(const RestartLoaded& rec, size_t turns) {
    assets::LevelData level;
    if (!level.load_from_file(maps_dir() + "/" + rec.head.map)) throw std::runtime_error("map");
    sim::SimulationEngine sim;
    sim.set_fog_of_war_enabled(rec.head.start.fog);
    sim.set_game_mode(rec.head.start.game_mode());
    sim.init(level, rec.head.start.seed, rec.head.start.roster);
    sim::apply_start_teams(sim, rec.head.start.teams());
    for (size_t i = 0; i < turns && i < rec.turns.size(); ++i) {
        for (const sim::Command& c : rec.turns[i].commands) sim.apply_command(c);
        sim.tick();
    }
    return sim.state_hash().total;
}

inline std::string status_json_text(PWorld& w, const std::string& code) {
    ctl::HttpRequest rq;
    rq.method = "GET";
    rq.path = "/rooms/" + code;
    return handle_control(*w.mgr, rq, w.server_now()).body;
}

inline ctl::JsonValue status_json(PWorld& w, const std::string& code) {
    ctl::JsonValue j;
    std::string why;
    ctl::parse_json(status_json_text(w, code), j, &why);
    return j;
}



#ifndef _WIN32
// The process may not write a file of more than `bytes` (RLIMIT_FSIZE) and the signal that the kernel sends then is ignored: a write beyond it FAILS (EFBIG), like a full disk or an exhausted quota; the limits come back when the object goes
struct FileSizeLimit {
    rlimit old{};
    void (*old_handler)(int){nullptr};
    explicit FileSizeLimit(rlim_t bytes) {
        getrlimit(RLIMIT_FSIZE, &old);
        old_handler = ::signal(SIGXFSZ, SIG_IGN);
        rlimit now_limit = old;
        now_limit.rlim_cur = bytes;
        setrlimit(RLIMIT_FSIZE, &now_limit);
    }
    ~FileSizeLimit() {
        setrlimit(RLIMIT_FSIZE, &old);
        ::signal(SIGXFSZ, old_handler);
    }
};
#endif


inline void put_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
}

// Frames of EMPTY turns, as an honest writer or a hostile one makes them: `turns` of them from number `first` on, `per_frame` to a frame (1: what the server writes, 4096: the most that a frame may hold)
inline void append_empty_turns(std::vector<uint8_t>& out, uint32_t first, uint32_t turns, uint32_t per_frame) {
    uint8_t frame[5 + 6 + 2 * 4096 + 4];
    for (uint32_t done = 0; done < turns;) {
        const uint32_t n = std::min(per_frame, turns - done);
        const uint32_t length = 6 + 2 * n;
        std::memset(frame, 0, sizeof frame);
        frame[0] = 2;
        put_le32(frame + 1, length);
        put_le32(frame + 5, first + done);
        frame[9] = static_cast<uint8_t>(n & 0xFFu);
        frame[10] = static_cast<uint8_t>(n >> 8);
        const size_t body = 5 + length;
        put_le32(frame + body, restart_crc32(frame, body));
        const size_t at = out.size();
        out.resize(at + body + 4);
        std::memcpy(out.data() + at, frame, body + 4);
        done += n;
    }
}

// A record of sample_head() and `turns` empty turns
inline std::vector<uint8_t> empty_turns_record(uint32_t turns, uint32_t per_frame) {
    const std::vector<uint8_t> head = encode_restart_head(sample_head("REC-1"));
    std::vector<uint8_t> out = with_magic(head);
    append_empty_turns(out, 0, turns, per_frame);
    return out;
}

// A match played for `play_ms` and then lost with the server (a crash): the room's record is on disk, its machines do not come back
inline void crash_with_record_of(PWorld& w, const std::string& code, uint32_t play_ms, uint8_t players = 2) {
    std::vector<RClient*> m = play_room(w, held_spec(code, players), play_ms);
    for (RClient* p : m) p->reconnects = false;
    w.stop_server(false);
}

// A record whose head is made over by `edit` (the frames behind it are kept as they are)
inline void rewrite_head(PWorld& w, const std::string& code, const std::function<void(RestartHead&)>& edit) {
    const std::vector<uint8_t> bytes = read_all_bytes(w.record_path(code));
    const auto frames = frames_of(bytes);
    RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size());
    edit(rec.head);
    std::vector<uint8_t> out = with_magic(encode_restart_head(rec.head));
    const size_t at = out.size();
    out.resize(at + (bytes.size() - frames[0].second));
    std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    write_all_bytes(w.record_path(code), out);
}

// A clock that goes on by itself: every reading costs `step_ms`, so that a replay "takes" as long as the test says (a replay reads it twice every 20 turns); `real` switches it to the real one
struct FakeClock {
    uint32_t ms{0};
    uint32_t step_ms{100};
    bool real{false};
    std::function<uint32_t()> as_function() {
        return [this]() { return real ? restart_steady_ms() : (ms += step_ms); };
    }
};

// A copy of the record of `from_code` under another code (its head is made over; the keys, the seats and the match are the original's), given a last write `age` ago
inline void copy_record_as(PWorld& w, const std::string& from_code, const std::string& to_code, std::chrono::seconds age) {
    const std::vector<uint8_t> bytes = read_all_bytes(w.record_path(from_code));
    const auto frames = frames_of(bytes);
    RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size());
    rec.head.code = to_code;
    std::vector<uint8_t> out = with_magic(encode_restart_head(rec.head));
    const size_t at = out.size();
    out.resize(at + (bytes.size() - frames[0].second));
    std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    write_all_bytes(w.record_path(to_code), out);
    fs::last_write_time(w.record_path(to_code), fs::file_time_type::clock::now() - age);
}



// the result row of a seat
inline const RoomRow* row_of(const RoomStatus& s, uint8_t seat) {
    for (const RoomRow& r : s.rows) {
        if (r.first == seat) return &r;
    }
    return nullptr;
}

// the lines of a client's room chat as "seat|name|text" (a notice: 255 and no name)
inline std::vector<std::string> said(const std::vector<net::ChatLine>& lines) {
    std::vector<std::string> out;
    for (const net::ChatLine& l : lines) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
    return out;
}


// A record with `extra` turns that nobody played added after its last turn (empty turns: the engine just ticks; no checkpoint for them): a match as long as a test wants. A torn tail goes.
inline void lengthen_record(PWorld& w, const std::string& code, uint32_t extra) {
    const std::vector<uint8_t> bytes = read_all_bytes(w.record_path(code));
    const RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size(), false);
    if (!rec.ok()) throw std::runtime_error("lengthen_record: " + rec.why);
    std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(rec.good_bytes));
    append_empty_turns(out, rec.turn_count, extra, 1);
    write_all_bytes(w.record_path(code), out);
}

// A record's last write `seconds` ago (the restore takes the newest first)
inline void age_record(PWorld& w, const std::string& code, int seconds) { fs::last_write_time(w.record_path(code), fs::file_time_type::clock::now() - std::chrono::seconds(seconds)); }

}  // namespace

// The groups of cases, run by main() in test_server.cpp in this order
void run_site_stats_tests();
void run_replay_tests();
void run_bot_match_tests();
void run_store_tests();
void run_manager_tests();
void run_demo_tests();
void run_hardening_tests();
void run_map_tests();
void run_match_tests();
void run_leader_tests();
void run_control_tests();
void run_socket_tests();
void run_secret_tests();
void run_reconnect_tests();
void run_bot_tests();
void run_persist_tests();
void run_persist_server_tests();
void run_persist_server_tests_2();
void run_persist_server_tests_3();
void run_persist_server_tests_4();
void run_persist_server_tests_5();
void run_persist_server_tests_6();
void run_persist_review_tests();
void run_persist_process_tests();
void run_persist_review_process_tests();
void run_persist_review_process_tests_2();
void run_restore_tests();
void run_lobby_room_tests();
