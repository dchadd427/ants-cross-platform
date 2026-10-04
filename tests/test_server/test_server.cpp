// Tests of the dedicated game server (docs/NETWORK_PORT.md): the map store, the room manager and its door (Hello routing), a room from the first Hello to the result
// with a host that has no seat, the control interface's calls, and one run over real sockets.
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
#include "ants_server/control.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/secret.hpp"
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

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

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
    net::Connection* end{nullptr};
    net::Connection* server_end{nullptr};    // the other end of the link (the server's): open until the server closes it, whatever this client has or has not read
    std::unique_ptr<net::ClientLobby> lobby;
    sim::SimulationEngine sim;
    std::unique_ptr<net::ClientSession> session;
    bool fail_load{false};
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
        lobby = std::make_unique<net::ClientLobby>(end, cc);
    }
    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    void update(uint32_t now_ms, const std::string& maps) {
        if (lobby == nullptr) return;
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
                    sim.init(level, s.seed, s.roster);
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

    Client& connect(const std::string& name, const std::string& room, uint8_t seat = 255, net::LoopbackNetwork::Link link = {20, 10}, Throttled** throttle = nullptr) {
        auto ends = net.connect(link);
        mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
        clients.push_back(std::make_unique<Client>());
        Client& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
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
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {            // (counted, not compared with an end time: the clock of a test may wrap)
            now += 10;
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

RoomSpec spec_of(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
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
double process_cpu_seconds() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1.0e6;
}
double peak_memory_mb() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);      // (bytes on macOS)
#else
    return static_cast<double>(ru.ru_maxrss) / 1024.0;                  // (kilobytes elsewhere)
#endif
}
#endif

// The CPU time (user and system, milliseconds) that the calling thread has used so far; -1 when the system cannot say. It does not run while the thread waits for the machine or sleeps, so the
// difference of two readings is the thread's own work, however busy the machine of the test is (a wall clock around a call counts the time that other programs took, too).
double thread_cpu_ms() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
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
long process_id() {
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

void write_bytes(const fs::path& p, size_t n, char fill) {
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

    void start(net::Connection* client_end, uint32_t seed) {
        end = client_end;
        rng = seed;
        net::ClientLobby::Config cc;
        cc.name = name;
        cc.room = room;
        cc.want_seat = want_seat;
        cc.key = key;
        cc.listen_port = listen_port;
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
    RClient& connect(const std::string& name, const std::string& room, uint8_t seat = 255, const net::SeatKey& key = net::SeatKey{}) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.key = key;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
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

void RClient::update(uint32_t now_ms, const std::string& maps, LinkSource& w) {
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
                    sim.init(level, s.seed, s.roster);
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
RoomSpec held_spec(const std::string& code, uint8_t players = 2, const char* map = "TINY.LVL") {
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
int reject_on(net::Connection* c) {
    std::vector<uint8_t> msg;
    net::RejectMsg r;
    while (c->poll(msg)) {
        if (net::peek_type(msg) == net::MsgType::Reject && net::decode(msg, r)) return static_cast<int>(r.reason);
    }
    return 0;
}

std::string hex_of(const net::SeatKey& key) {
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

std::vector<uint8_t> read_all_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

net::SeatKey test_key(uint8_t salt) {
    net::SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 37u + i * 11u + 5u);
    return k;
}

// A head with everything in it: three seats, a bot of the leader's fill at seat 2, keys for the two persons
RestartHead sample_head(const std::string& code = "REC-1") {
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
net::TurnMsg sample_turn(uint32_t n) {
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

bool same_head(const RestartHead& a, const RestartHead& b) {
    return a.identity.game_version == b.identity.game_version && a.identity.protocol == b.identity.protocol && a.identity.build_id == b.identity.build_id && a.code == b.code && a.map == b.map &&
           a.map_hash == b.map_hash && a.players == b.players && a.fog == b.fog && a.early_start == b.early_start && a.wait_ms == b.wait_ms && a.load_ms == b.load_ms && a.keep_ms == b.keep_ms &&
           a.run_ms == b.run_ms && a.vote_after_ms == b.vote_after_ms && a.max_pause_ms == b.max_pause_ms && a.max_catch_up_ms == b.max_catch_up_ms && a.resume_countdown_ms == b.resume_countdown_ms &&
           a.max_log_bytes == b.max_log_bytes && a.max_connections == b.max_connections && a.fill_mask == b.fill_mask && a.keys == b.keys && net::encode(a.start) == net::encode(b.start) &&
           a.bots.size() == b.bots.size() &&
           std::equal(a.bots.begin(), a.bots.end(), b.bots.begin(), [](const ai::BotSpec& x, const ai::BotSpec& y) { return x.seat == y.seat && x.kind == y.kind && x.level == y.level; });
}

bool same_turns(const std::vector<net::TurnMsg>& a, const std::vector<net::TurnMsg>& b, size_t count) {
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
RestartConfig test_restart_config(const char* tag) {
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
    RestoreReport report;                                   // what the last start of the server brought back
    std::function<bool()> restore_should_stop;              // the server's stop flag while it restores (empty: none)
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
    // The server starts (its uptime is `server_clock` ms): it reads the folder and brings the rooms back
    void start_server(uint32_t server_clock = 500) {
        server_offset = server_clock - now;
        server_ends.clear();
        mgr = std::make_unique<RoomManager>(MapStore(maps), limits);
        std::string why;
        if (!mgr->enable_restart_records(restart, why)) throw std::runtime_error("enable_restart_records: " + why);
        report = mgr->restore_rooms(server_now(), restore_should_stop);
        collect();
        ++starts;
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
    RClient& connect(const std::string& name, const std::string& room, uint8_t seat = 255, const net::SeatKey& key = net::SeatKey{}) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.key = key;
        c.listen_port = announce_port;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            net.set_time(now);
            if (mgr != nullptr) {
                mgr->update(server_now());
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

}  // namespace

void run_store_tests() {
    TEST_CASE("S3.0 The Scratch Folders Of A Run Are Its Own: Named With The Process And A Random Part, Never The Name That Every Run Of The Suite Shared, And Removed With Their Contents") {
        const std::string pid = std::to_string(process_id());
        fs::path a_path;
        fs::path b_path;
        {
            ScratchRoot a;
            ScratchRoot b;                                                          // two roots in one process (and so two processes) never get the same folder
            a_path = a.path();
            b_path = b.path();
            ASSERT_TRUE(a_path != b_path);
            ASSERT_TRUE(fs::is_directory(a_path) && fs::is_directory(b_path));
            ASSERT_TRUE(fs::equivalent(a_path.parent_path(), fs::temp_directory_path()) && fs::equivalent(b_path.parent_path(), fs::temp_directory_path()));   // (the temp path may end in a slash)
            ASSERT_TRUE(a_path.filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);        // the process's own: its id in the name
            ASSERT_TRUE(b_path.filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);
            ASSERT_TRUE(a_path.filename().string().size() > std::string("ants_server_test_" + pid + "_").size());   // and a random part after it
            fs::create_directories(a_path / "deep" / "er");
            write_bytes(a_path / "deep" / "er" / "file", 10, 'x');
            ASSERT_TRUE(fs::exists(a_path / "deep" / "er" / "file"));
        }
        ASSERT_FALSE(fs::exists(a_path) || fs::exists(b_path));                     // removed with everything in them
        // the folders that the suite asks for are made fresh under the root of this run, and never at the old shared names
        const std::string dir = temp_dir_for("s30");
        ASSERT_TRUE(fs::path(dir).parent_path() == scratch_root());
        ASSERT_TRUE(fs::path(dir) != fs::temp_directory_path() / "ants_server_test_s30");
        ASSERT_TRUE(fs::path(dir).string().find("ants_server_test_" + pid + "_") != std::string::npos);
        write_bytes(fs::path(dir) / "left_behind", 10, 'x');
        const std::string again = temp_dir_for("s30");                              // a second request for the same name starts empty
        ASSERT_TRUE(again == dir && !fs::exists(fs::path(again) / "left_behind"));
        ASSERT_TRUE(scratch_root().filename().string().rfind("ants_server_test_" + pid + "_", 0) == 0);
    } TEST_END();

    TEST_CASE("S3.1 Map Store: A Map Of The Folder Is Found With Its Hash; Names That Are No Map Of The Folder Are Refused") {
        MapStore store(maps_dir());
        MapEntry e;
        std::string why;
        ASSERT_TRUE(store.find("TINY.LVL", e, &why));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "/TINY.LVL", hash));
        ASSERT_TRUE(e.hash == hash && e.size > 1000 && e.name == "TINY.LVL");
        for (const char* bad : {"../Original-Ants/Maps/TINY.LVL", "..", "a/b.lvl", "nosuch.lvl", "TINY.LVL ", "TINY", "", "TINY.txt", "x:y.lvl"}) {
            ASSERT_FALSE(store.find(bad, e, &why));
            ASSERT_FALSE(why.empty());
        }
        // odd names of a folder: spaces, '!', '..' inside, a 64 character name; empty and huge files are no maps
        const std::string dir = temp_dir_for("store");
        fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(dir) / "!!! My Map ~v2~ ....lvl");
        write_bytes(fs::path(dir) / "empty.lvl", 0, 'x');
        write_bytes(fs::path(dir) / "huge.lvl", MapStore::kMaxMapBytes + 1, 'x');
        fs::create_directories(fs::path(dir) / "folder.lvl");
        MapStore odd(dir);
        ASSERT_TRUE(odd.find("!!! My Map ~v2~ ....lvl", e, &why) && e.hash == hash);
        ASSERT_FALSE(odd.find("empty.lvl", e, &why));
        ASSERT_FALSE(odd.find("huge.lvl", e, &why));
        ASSERT_FALSE(odd.find("folder.lvl", e, &why));
        fs::remove_all(dir);
    } TEST_END();
}

void run_manager_tests() {
    TEST_CASE("S3.2 Making Rooms: A Good Spec Makes A Waiting Room; Bad Specs Are Refused With The Right Status; Codes Are Unique And Generated When Missing; The Number Of Rooms Is Limited") {
        ServerLimits limits;
        limits.max_rooms = 4;
        RoomManager mgr{MapStore(maps_dir()), limits};
        CreateResult r = mgr.create_room(spec_of("ROOM-1"), 0);
        ASSERT_TRUE(r.ok && r.http_status == 201 && r.code == "ROOM-1");
        RoomStatus s;
        ASSERT_TRUE(mgr.status("ROOM-1", s, 0));
        ASSERT_TRUE(s.state == RoomState::Waiting && s.expected == 2 && s.joined == 0 && s.map == "TINY.LVL" && !s.fog);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-1"), 0).http_status, 409);              // the code exists
        ASSERT_EQ(mgr.create_room(spec_of("bad code"), 0).http_status, 400);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 1), 0).http_status, 400);           // players 1
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 5), 0).http_status, 400);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 2, "nosuch.lvl"), 0).http_status, 404);
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-2", 2, "../x.lvl"), 0).http_status, 404);
        RoomSpec short_wait = spec_of("ROOM-2");
        short_wait.wait_ms = 10;
        ASSERT_EQ(mgr.create_room(short_wait, 0).http_status, 400);
        // a map the engine cannot read
        const std::string dir = temp_dir_for("manager");
        write_bytes(fs::path(dir) / "junk.lvl", 5000, '\xAB');
        RoomManager junk{MapStore(dir)};
        ASSERT_EQ(junk.create_room(spec_of("J-1", 2, "junk.lvl"), 0).http_status, 422);
        fs::remove_all(dir);
        // generated codes: eight characters, valid, different
        RoomSpec anon = spec_of("");
        const CreateResult a = mgr.create_room(anon, 0);
        const CreateResult b = mgr.create_room(anon, 0);
        ASSERT_TRUE(a.ok && b.ok && a.code.size() == 8 && b.code.size() == 8 && a.code != b.code && net::valid_room_code(a.code));
        ASSERT_TRUE(mgr.create_room(spec_of("ROOM-3"), 0).ok);                          // the fourth room
        ASSERT_EQ(mgr.create_room(spec_of("ROOM-4"), 0).http_status, 503);              // no room for a fifth
        ASSERT_EQ(mgr.room_count(), size_t{4});
        ASSERT_TRUE(mgr.close_room("ROOM-3", 0));
        ASSERT_FALSE(mgr.close_room("NOPE", 0));
    } TEST_END();

    TEST_CASE("S3.3 The Door: A Hello Finds Its Room By Its Code; Wrong Codes, Old Versions, Garbage, Silence, A Full Room And A Running Match Are Refused") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("AAA-1", 2), w.now).ok);
        auto reject_of = [&](Client& c) {
            w.run(300);
            return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
        };
        Client& wrong = w.connect("Wrong", "BBB-9");
        ASSERT_EQ(reject_of(wrong), net::RejectReason::NoSuchRoom);
        Client& none = w.connect("None", "");
        ASSERT_EQ(reject_of(none), net::RejectReason::NoSuchRoom);                       // a Hello without a room code goes nowhere on a server
        Client& ann = w.connect("Ann", "AAA-1");
        w.run(300);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(ann.lobby->my_seat(), 0);                                              // seat 0 is a guest's: the server has none
        // an old client: its layout is not read, the answer is "version mismatch"; so is the answer to a game of protocol 8 (v0.0.94), whose Hello has exactly this protocol's layout:
        // the engine's rules changed in 9 (the community-map rules), and the number is all that the door has to tell the two games apart
        // (the door decides the version before it looks for the room, so an old game that asks for a room that does not exist is refused for its version, not told "no such room")
        ASSERT_TRUE(net::kProtocolVersion != 8);
        // (and so is a game of protocol 11, v0.1.0 or v0.1.1: its Hello is a Hello of 12 byte for byte, the number alone refuses it; it would count its dialog in simulation ticks and be blocked for
        // 100 ticks of the match after the server's late first turn)
        for (const uint16_t old_version : {uint16_t{4}, uint16_t{8}, uint16_t{11}}) {
            for (const char* room : {"AAA-1", "NOPE-9"}) {
                auto ends = w.net.connect({10, 0});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
                net::HelloMsg old;
                old.version = old_version;
                old.name = "Old";
                old.room = room;
                ends.second->send(net::encode(old));
                w.run(200);
                std::vector<uint8_t> reply;
                net::RejectMsg rj;
                ASSERT_TRUE(ends.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::VersionMismatch);
            }
        }
        // the Hello of protocols 6 to 9 has the layout before the keys (no key, no turns after the token): it is told "version mismatch" without its layout being read, and nobody is seated
        for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {
            std::vector<uint8_t> raw = {static_cast<uint8_t>(net::MsgType::Hello), static_cast<uint8_t>(version), 0, 3, 'O', 'l', 'd', 0x34, 0x12, 255, 5, 'A', 'A', 'A', '-', '1', 0};
            auto ends = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
            ends.second->send(raw);
            w.run(200);
            std::vector<uint8_t> reply;
            net::RejectMsg rj;
            ASSERT_TRUE(ends.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::VersionMismatch);
            ASSERT_EQ(w.status("AAA-1").joined, 1u);                                          // (only Ann)
        }
        // garbage and a message that is no Hello
        {
            auto a = w.net.connect({10, 0});
            auto b = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(a.first), "x", w.now);
            w.mgr.add_connection(std::make_unique<Borrowed>(b.first), "x", w.now);
            a.second->send({99, 1, 2, 3});
            b.second->send(net::encode_leave());
            w.run(200);
            std::vector<uint8_t> reply;
            net::RejectMsg rj;
            ASSERT_TRUE(a.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::BadRequest);
            ASSERT_TRUE(b.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::BadRequest);
        }
        // a connection that never says Hello is closed after the timeout and costs nothing afterwards
        {
            auto s = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(s.first), "x", w.now);
            ASSERT_EQ(w.mgr.pending_count(), size_t{1});
            w.run(10500);
            ASSERT_EQ(w.mgr.pending_count(), size_t{0});
            ASSERT_FALSE(s.second->is_open());
        }
        // the second player starts the match; a third Hello (the room takes two) and a later one are refused
        Client& bob = w.connect("Bob", "AAA-1");
        Client& cat = w.connect("Cat", "AAA-1");
        w.run(500);
        ASSERT_EQ(bob.lobby->my_seat(), 1);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_TRUE(cat.lobby->reject_reason() == net::RejectReason::Full || cat.lobby->reject_reason() == net::RejectReason::MatchRunning);
        Client& late = w.connect("Late", "AAA-1");
        w.run(1500);
        ASSERT_TRUE(late.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_TRUE(w.status("AAA-1").state == RoomState::Running);
        ASSERT_EQ(late.lobby->reject_reason(), net::RejectReason::MatchRunning);
    } TEST_END();
}

// (placed before the match tests: the door's tests)
void run_demo_tests() {
    TEST_CASE("S3.10 Demo Rooms: Off Unless Asked For; A Hello For \"demo-...\" Makes The Room, Only With The Prefix, Only Up To The Limit, And An Unfilled One Fails After Its Wait (One Minute Here, Ten By Default)") {
        auto reject_of = [](World& w, Client& c) {
            w.run(300);
            return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
        };
        {
            World w;                                                                      // the default: no demo rooms
            Client& a = w.connect("Ann", "demo-a");
            ASSERT_EQ(reject_of(w, a), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.demo_players = 2;
        limits.demo_wait_ms = 60000;                                                     // (the default is ten minutes: S3.25)
        World w(limits);
        Client& other = w.connect("Other", "other-1");                                    // no prefix: no room is made
        ASSERT_EQ(reject_of(w, other), net::RejectReason::NoSuchRoom);
        Client& bare = w.connect("Bare", "demo-");                                        // the prefix alone is no code
        ASSERT_EQ(reject_of(w, bare), net::RejectReason::NoSuchRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{0});
        Client& ann = w.connect("Ann", "demo-a");
        w.run(300);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        ASSERT_TRUE(w.status("demo-a").state == RoomState::Waiting);
        ASSERT_EQ(w.status("demo-a").map, std::string("TINY.LVL"));
        ASSERT_EQ(w.status("demo-a").expected, 2);
        Client& bob = w.connect("Bob", "demo-a");                                         // the second Hello finds the room that the first one made
        w.run(800);
        ASSERT_EQ(bob.lobby->my_seat(), 1);
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        ASSERT_TRUE(w.status("demo-a").state == RoomState::Loading || w.status("demo-a").state == RoomState::Running);
        Client& cat = w.connect("Cat", "demo-b");                                         // a second demo room
        w.run(300);
        ASSERT_EQ(cat.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
        Client& dan = w.connect("Dan", "demo-c");                                         // the limit is two
        ASSERT_EQ(reject_of(w, dan), net::RejectReason::NoSuchRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
        w.run(62000);                                                                     // demo-b never filled: it fails after its minute (the match of demo-a goes on)
        ASSERT_TRUE(w.status("demo-b").state == RoomState::Failed);
        ASSERT_EQ(ServerLimits().demo_players, 4);                                        // the default is a room of four (this test uses two)
        ASSERT_EQ(ServerLimits().demo_rooms, size_t{0});
        w.run(31000);                                                                     // a failed demo room is forgotten after 30 s, and its place is free again
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        Client& eve = w.connect("Eve", "demo-d");
        w.run(300);
        ASSERT_EQ(eve.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(w.mgr.room_count(), size_t{2});
    } TEST_END();

    TEST_CASE("S3.23 A Demo Room Code Can Choose Its Map: demo-<map>-... Makes The Room On That Map When It Is In The List, In Any Case; Every Other Code Keeps The Default Map; Without A List Nothing Changes") {
        ServerLimits limits;
        limits.demo_rooms = 13;                                                   // room for the twelve codes below and one more
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
        limits.demo_players = 2;
        World w(limits);
        auto map_of = [&](const std::string& code) {
            w.connect("P", code);
            w.run(300);
            return w.status(code).map;
        };
        ASSERT_EQ(map_of("demo-small-x7k2"), std::string("SMALL.LVL"));              // the choice
        ASSERT_EQ(map_of("demo-SMALL-upper"), std::string("SMALL.LVL"));             // any case in the code
        ASSERT_EQ(map_of("demo-tiny-ab12"), std::string("TINY.LVL"));
        ASSERT_EQ(map_of("demo-GaUnTlEt-q"), std::string("GAUNTLET.LVL"));           // the name as the list spells it, whatever the case of the code
        ASSERT_EQ(map_of("demo-small-a-b-c"), std::string("SMALL.LVL"));             // only the first word counts
        ASSERT_EQ(map_of("demo-small-"), std::string("SMALL.LVL"));                  // an empty tail is a valid code
        ASSERT_EQ(map_of("demo-medium-x1"), std::string("TINY.LVL"));                // a real map that is not in the list: the default
        ASSERT_EQ(map_of("demo-smallish-x1"), std::string("TINY.LVL"));              // a longer word is not the map
        ASSERT_EQ(map_of("demo-smal-x1"), std::string("TINY.LVL"));                  // nor a shorter one
        ASSERT_EQ(map_of("demo-small"), std::string("TINY.LVL"));                    // no dash after the word: that is just a code
        ASSERT_EQ(map_of("demo--x1"), std::string("TINY.LVL"));                      // an empty word
        ASSERT_EQ(map_of("demo-x7k2"), std::string("TINY.LVL"));                     // the page's old codes
        ASSERT_EQ(map_of("demo-small.lvl-x"), std::string());                        // '.' is no character of a room code: refused, no room
        ASSERT_EQ(w.mgr.room_count(), size_t{12});
        // a second Hello for the same code finds the room that the first one made, on the same map
        Client& second = w.connect("Q", "demo-small-x7k2");
        w.run(800);
        ASSERT_EQ(second.lobby->my_seat(), 1);
        ASSERT_EQ(w.mgr.room_count(), size_t{12});
        ASSERT_EQ(w.status("demo-small-x7k2").map, std::string("SMALL.LVL"));
        // no list: the choice is ignored, every demo room is on the default map (what the server did before the list existed)
        ServerLimits plain;
        plain.demo_rooms = 4;
        plain.demo_map = "TINY.LVL";
        plain.demo_players = 2;
        ASSERT_TRUE(plain.demo_maps.empty());
        World p(plain);
        p.connect("P", "demo-small-x7k2");
        p.run(300);
        ASSERT_EQ(p.status("demo-small-x7k2").map, std::string("TINY.LVL"));
        // a listed map that does not load makes the Hello fail like any room with a bad map: the room is not made
        ServerLimits broken = limits;
        broken.demo_maps = {"NO-SUCH-MAP.LVL"};
        World b(broken);
        Client& lost = b.connect("P", "demo-no-such-map-x");
        b.run(300);
        ASSERT_TRUE(lost.lobby->phase() != net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(b.mgr.room_count(), size_t{0});
        // of two names that fit, the longest wins (a name may contain dashes): a maps folder with BIG.LVL and BIG-ISLAND.LVL (copies of TINY.LVL)
        const std::string big_dir = temp_dir_for("demo_longest");
        fs::copy_file(maps_dir() + "/TINY.LVL", big_dir + "/BIG.LVL");
        fs::copy_file(maps_dir() + "/TINY.LVL", big_dir + "/BIG-ISLAND.LVL");
        ServerLimits two;
        two.demo_rooms = 4;
        two.demo_map = "BIG.LVL";
        two.demo_maps = {"BIG.LVL", "BIG-ISLAND.LVL"};
        two.demo_players = 2;
        World g(two, big_dir);
        g.connect("P", "demo-big-island-2p-x");
        g.connect("Q", "demo-big-y");
        g.connect("R", "demo-BIG-ISLAND-z");
        g.run(300);
        ASSERT_EQ(g.status("demo-big-island-2p-x").map, std::string("BIG-ISLAND.LVL"));
        ASSERT_EQ(g.status("demo-big-y").map, std::string("BIG.LVL"));
        ASSERT_EQ(g.status("demo-BIG-ISLAND-z").map, std::string("BIG-ISLAND.LVL"));
        std::error_code ignore;
        fs::remove_all(big_dir, ignore);
    } TEST_END();

    TEST_CASE("S3.24 A Demo Room Code Can Choose Its Number Of Players: demo-[<map>-]<n>p-... Makes A Room For 2, 3 Or 4; Anything Else Keeps The Default; A Room For Two Starts With Two") {
        ServerLimits limits;
        limits.demo_rooms = 20;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
        ASSERT_EQ(limits.demo_players, 4);                                        // the default: four
        World w(limits);
        auto made = [&](const std::string& code) {
            w.connect("P", code);
            w.run(300);
            return w.status(code);
        };
        struct Case { const char* code; const char* map; int players; };
        const Case cases[] = {
            {"demo-small-2p-a", "SMALL.LVL", 2}, {"demo-small-3P-b", "SMALL.LVL", 3}, {"demo-small-4p-c", "SMALL.LVL", 4},
            {"demo-2p-d", "TINY.LVL", 2},                                          // a player count without a map: the default map
            {"demo-gauntlet-3p-e", "GAUNTLET.LVL", 3},
            {"demo-small-5p-f", "SMALL.LVL", 4}, {"demo-small-1p-g", "SMALL.LVL", 4}, {"demo-small-0p-h", "SMALL.LVL", 4},   // not 2 to 4: the default
            {"demo-small-2px-i", "SMALL.LVL", 4},                                  // no dash after the word: part of the code
            {"demo-small-2p", "SMALL.LVL", 4},                                     // the word without a dash after it
            {"demo-small-22p-j", "SMALL.LVL", 4}, {"demo-medium-x-2p-m", "TINY.LVL", 4},   // the players word is the first or second word only
            {"demo-medium-2p-k", "TINY.LVL", 2},                                   // a map that is not allowed: the default map, but the players are read (the page offers six maps)
            {"demo-2p-small-l", "TINY.LVL", 2},                                    // the order is map, then players: here the map word comes too late
            {"demo-x7k2", "TINY.LVL", 4},                                          // the page's old codes
        };
        for (const Case& c : cases) {
            const RoomStatus st = made(c.code);
            ASSERT_MSG(st.map == c.map, c.code);
            ASSERT_MSG(static_cast<int>(st.expected) == c.players, c.code);
        }
        // without a list of maps the player count is still chosen
        ServerLimits plain;
        plain.demo_rooms = 4;
        plain.demo_map = "TINY.LVL";
        World p(plain);
        p.connect("P", "demo-2p-x");
        p.connect("Q", "demo-small-2p-y");
        p.run(300);
        ASSERT_EQ(static_cast<int>(p.status("demo-2p-x").expected), 2);
        ASSERT_EQ(p.status("demo-2p-x").map, std::string("TINY.LVL"));
        ASSERT_EQ(static_cast<int>(p.status("demo-small-2p-y").expected), 2);                     // no list: "small" is no map here, the players are still read
        // a room for two starts as soon as two have joined, and a third player cannot get in
        World s2(limits);
        Client& ann = s2.connect("Ann", "demo-small-2p-duel");
        Client& bob = s2.connect("Bob", "demo-small-2p-duel");
        s2.run(800);
        ASSERT_EQ(ann.lobby->my_seat() != bob.lobby->my_seat(), true);
        ASSERT_TRUE(s2.status("demo-small-2p-duel").state == RoomState::Loading || s2.status("demo-small-2p-duel").state == RoomState::Running);
        ASSERT_EQ(static_cast<int>(s2.status("demo-small-2p-duel").joined), 2);
        Client& cat = s2.connect("Cat", "demo-small-2p-duel");
        s2.run(800);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::Rejected || cat.lobby->phase() == net::ClientLobby::Phase::Closed);
        ASSERT_EQ(static_cast<int>(s2.status("demo-small-2p-duel").joined), 2);
    } TEST_END();

    TEST_CASE("S3.74 The Public Stack's Demo Rooms (docker-compose.stack.yml: --demo-map TREASURE.LVL and the six maps of the page): A Code That Names No Map Is Made On TREASURE.LVL For Four Players; A Code That Names One Of The Six Is Made On It, Whatever Its Case; The Players Word Is Read With And Without A Map") {
        ServerLimits limits;                                                      // the options of the stack file: --demo-rooms 12 --demo-map TREASURE.LVL --demo-maps <the six>
        limits.demo_rooms = 12;
        limits.demo_map = "TREASURE.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};
        World w(limits);
        auto made = [&](const std::string& code) {
            w.connect("P", code);
            w.run(300);
            return w.status(code);
        };
        struct Case { const char* code; const char* map; int players; };
        const Case cases[] = {
            {"demo-x7k2", "TREASURE.LVL", 4}, {"demo-abc", "TREASURE.LVL", 4},    // the page's old codes and every other code that names no map: the default map
            {"demo-2p-n", "TREASURE.LVL", 2},                                      // a players word without a map: the default map
            {"demo-tiny-a", "TINY.LVL", 4}, {"demo-small-b", "SMALL.LVL", 4}, {"demo-medium-c", "MEDIUM.LVL", 4},      // a map of the page's six: that map, four players
            {"demo-gauntlet-d", "GAUNTLET.LVL", 4}, {"demo-treasure-e", "TREASURE.LVL", 4}, {"demo-islands-f", "ISLANDS.LVL", 4},
            {"demo-ISLANDS-3p-g", "ISLANDS.LVL", 3}, {"demo-tiny-2p-h", "TINY.LVL", 2},                                // whatever the case, and with the players word
        };
        for (const Case& c : cases) {
            const RoomStatus st = made(c.code);
            ASSERT_MSG(st.map == c.map, c.code);
            ASSERT_MSG(static_cast<int>(st.expected) == c.players, c.code);
        }
        ASSERT_EQ(w.mgr.room_count(), sizeof(cases) / sizeof(cases[0]));
    } TEST_END();

    TEST_CASE("S3.25 Friends Who Come Late: A Demo Room Waits Ten Minutes By Default; A Code Whose Demo Room Is Over Makes A New One (Its End Is Still Reported); A Room Of The Control Interface That Is Over Answers \"No Such Room\", Not \"Match Running\"") {
        ASSERT_EQ(ServerLimits().demo_wait_ms, 600000u);
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL", "SMALL.LVL"};
        {
            World w(limits);                                                          // the default wait: a friend can come after a minute
            w.connect("Host", "demo-small-2p-slow");
            w.run(61000);
            ASSERT_TRUE(w.status("demo-small-2p-slow").state == RoomState::Waiting);
            Client& friend_ = w.connect("Friend", "demo-small-2p-slow");
            w.run(800);
            ASSERT_EQ(friend_.lobby->my_seat(), 1);
            ASSERT_TRUE(w.status("demo-small-2p-slow").state == RoomState::Loading || w.status("demo-small-2p-slow").state == RoomState::Running);
        }
        ServerLimits short_wait = limits;
        short_wait.demo_wait_ms = 60000;
        World w(short_wait);
        Client& host = w.connect("Host", "demo-small-2p-late");
        w.run(61000);                                                                 // nobody came in time: the room failed
        ASSERT_TRUE(w.status("demo-small-2p-late").state == RoomState::Failed);
        ASSERT_TRUE(host.lobby->phase() != net::ClientLobby::Phase::InRoom);
        Client& late = w.connect("Late", "demo-small-2p-late");                       // within the 30 s of keep time: a new room at once, not "match running"
        w.run(300);
        ASSERT_EQ(late.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(w.status("demo-small-2p-late").state == RoomState::Waiting);
        ASSERT_EQ(static_cast<int>(w.status("demo-small-2p-late").joined), 1);
        ASSERT_EQ(w.status("demo-small-2p-late").map, std::string("SMALL.LVL"));
        bool reported = false;
        for (const RoomStatus& st : w.mgr.take_ended(w.now)) reported = reported || (st.code == "demo-small-2p-late" && st.state == RoomState::Failed);
        ASSERT_TRUE(reported);                                                        // the old room's end is not lost
        // a room of the control interface that is over is not replaced: "no such room"
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-1", 2), w.now).ok);
        ASSERT_TRUE(w.mgr.close_room("CTL-1", w.now));
        ASSERT_TRUE(w.status("CTL-1").state == RoomState::Failed);
        Client& too_late = w.connect("TooLate", "CTL-1");
        w.run(300);
        ASSERT_TRUE(too_late.lobby->phase() == net::ClientLobby::Phase::Rejected);
        ASSERT_EQ(too_late.lobby->reject_reason(), net::RejectReason::NoSuchRoom);
        ASSERT_TRUE(w.status("CTL-1").state == RoomState::Failed);
    } TEST_END();
}

void run_hardening_tests() {
    TEST_CASE("S3.11 The Server's Clock Is Its Uptime: A Room Starts And Plays When The 32-Bit Clock Is Past Its Signed Half, And Across The Wrap") {
        for (const uint32_t origin : {0x7FFFFE00u, 0x80000100u, 0xFFFFFC18u}) {          // just before the signed flip, just after it, a second before the wrap
            World w;
            w.now = origin;
            ASSERT_TRUE(w.mgr.create_room(spec_of("CLOCK-1", 2), w.now).ok);
            w.connect("Ann", "CLOCK-1");
            w.connect("Bob", "CLOCK-1");
            w.run(2500);
            ASSERT_MSG(w.status("CLOCK-1").state == RoomState::Running, "the room started at origin " + std::to_string(origin));
            w.run(20000 + kPre);
            const RoomStatus s = w.status("CLOCK-1");
            ASSERT_MSG(s.state == RoomState::Running && s.ticks > 300, "the match runs at origin " + std::to_string(origin));
            ASSERT_FALSE(w.clients[0]->session->desynced());
            ASSERT_TRUE(w.clients[0]->sim.state_hash() == w.clients[1]->sim.state_hash() || w.clients[0]->sim.current_tick() != w.clients[1]->sim.current_tick());
        }
    } TEST_END();

    TEST_CASE("S3.12 A Seat That Stops Executing Turns Does Not Hold A Room Even For A Moment: The Others Play On At Their Pace And Are Told, It Is Dropped After 30 s Without An Ack; A Running Room Has A Wall-Clock Limit") {
        {
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("LAG-1", 3), w.now).ok);
            Client& a = w.connect("Ann", "LAG-1");
            Client& b = w.connect("Bob", "LAG-1");
            Client& c = w.connect("Cat", "LAG-1");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            const uint8_t bob = b.lobby->my_seat();
            const uint32_t ticks_before = w.status("LAG-1").ticks;
            b.freeze = true;                                                              // Bob's program hangs: it neither acks nor answers (a hostile client would still ping)
            w.run(8000);
            // the room did not wait for Bob (it used to, 3 s after he stopped, until the 20 s were up): 8 s are 160 ticks, and Ann and Cat played them
            const uint32_t ticks_while_stuck = w.status("LAG-1").ticks;
            ASSERT_TRUE(ticks_while_stuck - ticks_before >= 150);
            ASSERT_TRUE(a.session->lagging_seat() == bob && c.session->lagging_seat() == bob);      // ... and they are told who lags
            ASSERT_TRUE(a.session->lagging_behind_ms() >= 3000);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            w.run(4000);
            ASSERT_TRUE(w.status("LAG-1").ticks - ticks_while_stuck >= 75);               // and on: 4 s, 80 ticks
            w.run(14000);                                                                 // 26 s since Bob stopped: still a lagger, not yet dropped (30 s without an ack)
            ASSERT_FALSE(a.sim.is_player_dropped(bob));
            w.run(7000);                                                                  // 33 s: dropped, at one tick for both of the others
            ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
            ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
            const uint32_t after_drop = w.status("LAG-1").ticks;
            w.run(5000);
            ASSERT_TRUE(w.status("LAG-1").ticks >= after_drop + 95);                      // Ann and Cat play on
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            ASSERT_FALSE(a.lost || c.lost);
            ASSERT_EQ(a.session->lagging_seat(), 255);                                    // the notice ended with the drop
        }
        {                                                                                 // two players: the one that is left has won, the room ends (it is not held for ever)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("LAG-2", 2), w.now).ok);
            w.connect("Ann", "LAG-2");
            Client& b = w.connect("Bob", "LAG-2");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Running);
            b.freeze = true;
            w.run(40000);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Finished);
        }
        {
            World w;
            RoomSpec spec = spec_of("RUN-1", 2);
            spec.run_ms = 5000;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "RUN-1");
            w.connect("Bob", "RUN-1");
            w.run(2500);
            ASSERT_TRUE(w.status("RUN-1").state == RoomState::Running);
            w.run(6000);
            const RoomStatus s = w.status("RUN-1");
            ASSERT_TRUE(s.state == RoomState::Failed);
            ASSERT_TRUE(s.reason.find("longer") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.34 The End Of A Match Does Not Cut Off A Laggard: A Player Who Is Far Behind When The Match Ends (Its Slow Link Brought It 55 s Behind) Is Still Answered And Served Until It Has Run The Last Turn, Then The Room Closes (It Closed 15 s After The End: The Player Was Cut Off 10 s Into Its Catch-Up, Never Saw The Results)") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("END-1", 3), w.now).ok);
        Client& a = w.connect("Ann", "END-1");
        Client& b = w.connect("Bob", "END-1");
        Throttled* slow = nullptr;
        Client& c = w.connect("Cat", "END-1", 255, {20, 10}, &slow);
        w.run(4000 + kPre);
        ASSERT_TRUE(w.status("END-1").state == RoomState::Running);
        slow->period_ms = 150;                                                       // Cat's downlink hands the game 7 of the 21 messages a second that the room sends
        w.run(82000);                                                                // it falls behind by two thirds of a second each second: 55 s behind (not 60: not dropped), and it still makes progress
        RoomStatus s = w.status("END-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(a.sim.current_tick() > 1500 && c.sim.current_tick() + 900 < a.sim.current_tick());
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        // the match ends for everybody else: Ann and Bob go, Cat is the last team and has won; its link is healthy again
        w.net.cut(a.end);
        w.net.cut(b.end);
        slow->period_ms = 0;
        for (int i = 0; i < 100 && w.status("END-1").state == RoomState::Running; ++i) w.run(100);
        ASSERT_TRUE(w.status("END-1").state == RoomState::Finished);
        const uint32_t ended_at = w.now;
        const uint32_t final_turns = w.status("END-1").turns;
        ASSERT_TRUE(final_turns > 1600);
        ASSERT_TRUE(c.sim.current_tick() + 800 < final_turns);                        // it is more than 40 s short of the end: its catch-up takes more than 10 s (the first frame back 4 s, then 4 s a second)
        // ... it is answered all the while (pings), and runs every turn to the last one
        uint32_t level_after_ms = 0;
        while (w.now - ended_at < 40000 && c.end->is_open()) {
            w.run(100);
            if (level_after_ms == 0 && c.sim.is_match_over()) level_after_ms = w.now - ended_at;
        }
        if (level_after_ms == 0 || c.lost) std::cout << "\n    Cat: lost " << c.lost << ", at turn " << c.session->runner().next_turn_to_execute() << " of " << final_turns << " after " << (w.now - ended_at) << " ms\n";
        ASSERT_FALSE(c.lost);
        ASSERT_TRUE(level_after_ms > 10000 && level_after_ms < 30000);                // it needed more than the 10 s of silence that cut it off, and the room waited
        ASSERT_EQ(c.session->runner().next_turn_to_execute(), final_turns);           // every turn of the match
        ASSERT_TRUE(c.sim.is_match_over());
        // the room closes: not before the grace period (a client that is level finds the results in peace), and not long after the last acknowledgement
        ASSERT_TRUE(w.now - ended_at >= Room::kGraceMs);
        ASSERT_FALSE(c.end->is_open());
        ASSERT_TRUE(w.now - ended_at <= level_after_ms + Room::kGraceMs + 1000 || w.now - ended_at <= Room::kEndWaitMs + 1000);
    } TEST_END();

    TEST_CASE("S3.35 A Finished Room Closes Its Connections At The Latest After kEndWaitMs, Whoever Is Still Behind, And At The Earliest After kGraceMs; Everybody Level Closes It At kGraceMs") {
        ASSERT_EQ(Room::kGraceMs, 15000u);
        ASSERT_EQ(Room::kEndWaitMs, 30000u);
        {   // everybody level: the connections stay open for the grace period and then close
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("END-2", 2), w.now).ok);
            Client& a = w.connect("Ann", "END-2");
            Client& b = w.connect("Bob", "END-2");
            w.run(4000 + kPre);
            w.net.cut(b.end);
            for (int i = 0; i < 100 && w.status("END-2").state == RoomState::Running; ++i) w.run(100);
            ASSERT_TRUE(w.status("END-2").state == RoomState::Finished);
            w.run(Room::kGraceMs - 1500);
            ASSERT_TRUE(a.server_end->is_open());
            w.run(3000);
            ASSERT_FALSE(a.server_end->is_open());
        }
        {   // a client that never acknowledges (a process that was stopped): the room does not wait for it for ever, and it is not dropped by the idle rule after the end
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("END-3", 3), w.now).ok);
            Client& a = w.connect("Ann", "END-3");
            Client& b = w.connect("Bob", "END-3");
            Client& c = w.connect("Cat", "END-3");
            w.run(4000);
            c.freeze = true;
            w.net.cut(a.end);
            w.net.cut(b.end);
            for (int i = 0; i < 100 && w.status("END-3").state == RoomState::Running; ++i) w.run(100);
            ASSERT_TRUE(w.status("END-3").state == RoomState::Finished);
            w.run(Room::kEndWaitMs - 2000);
            ASSERT_TRUE(c.server_end->is_open());                                    // 28 s: the room still waits for it
            w.run(4000);
            ASSERT_FALSE(c.server_end->is_open());                                   // 32 s: it does not wait any longer
        }
    } TEST_END();

    TEST_CASE("S3.13 The Door Cannot Be Locked By Silent Connections, And The End Of A Room That Is Forgotten At Once Is Still Reported") {
        {
            ServerLimits limits;
            limits.max_pending = 4;
            World w(limits);
            ASSERT_TRUE(w.mgr.create_room(spec_of("DOOR-1", 2), w.now).ok);
            std::vector<std::pair<net::Connection*, net::Connection*>> silent;
            for (int i = 0; i < 4; ++i) {
                auto ends = w.net.connect({5, 0});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
                silent.push_back(ends);
            }
            ASSERT_EQ(w.mgr.pending_count(), size_t{4});
            Client& ann = w.connect("Ann", "DOOR-1");                                    // the fifth connection: it takes the place of the oldest silent one
            w.run(400);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            ASSERT_FALSE(silent[0].second->is_open());                                    // (the evicted one was closed)
            ASSERT_TRUE(silent[3].second->is_open());
            ASSERT_TRUE(w.mgr.connections_refused() >= 1);
        }
        {
            World w;
            RoomSpec spec = spec_of("GONE-1", 2);
            spec.wait_ms = 1000;
            spec.keep_ms = 0;                                                             // forgotten in the pass in which it fails
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            std::vector<RoomStatus> ended;
            for (int i = 0; i < 300; ++i) {
                w.run(10);
                for (const RoomStatus& s : w.mgr.take_ended(w.now)) ended.push_back(s);
            }
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
            ASSERT_EQ(ended.size(), size_t{1});
            ASSERT_EQ(ended[0].code, std::string("GONE-1"));
            ASSERT_TRUE(ended[0].state == RoomState::Failed);
        }
    } TEST_END();
}

void run_map_tests() {
    TEST_CASE("S3.14 A Map That Is Playable For Some Seats And Not For Others: A Start Marker Outside The Grid Fails The Room When That Seat Plays, And Does Not When It Does Not") {
        // TINY with the green start marker (tile 154) moved to row 200, outside the 40 x 40 grid: the engine loads it, the original would crash when green plays
        const std::string dir = temp_dir_for("badmarker");
        {
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;                                                          // the row: outside the grid
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_room = [&](uint8_t first_seat, uint8_t second_seat, RoomStatus& s) {
            net::LoopbackNetwork net{5};
            RoomManager mgr{MapStore(dir)};
            uint32_t now = 1000;
            RoomSpec spec = spec_of("BAD-1", 2, "BAD.LVL");
            ASSERT_TRUE(mgr.create_room(spec, now).ok);                                   // it loads: the room is made
            std::vector<std::unique_ptr<Client>> clients;
            const uint8_t seats[2] = {first_seat, second_seat};
            for (int i = 0; i < 2; ++i) {
                auto ends = net.connect({20, 10});
                mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
                clients.push_back(std::make_unique<Client>());
                clients.back()->name = i == 0 ? "Ann" : "Bob";
                clients.back()->room = "BAD-1";
                clients.back()->want_seat = seats[i];
                clients.back()->start(ends.second, 11u + static_cast<uint32_t>(i));
            }
            for (int step = 0; step < 300; ++step) {                                      // 3 s
                now += 10;
                net.set_time(now);
                mgr.update(now);
                for (auto& c : clients) c->update(now, dir);
            }
            ASSERT_TRUE(mgr.status("BAD-1", s, now));
        };
        RoomStatus with_green;
        run_room(0, 1, with_green);                                                       // green (seat 0) plays: refused, and the room says why
        ASSERT_TRUE(with_green.state == RoomState::Failed);
        ASSERT_TRUE(with_green.reason.find("outside") != std::string::npos && with_green.reason.find("green") != std::string::npos);
        RoomStatus without_green;
        run_room(1, 2, without_green);                                                    // red and blue play: the marker is never used, the match starts
        ASSERT_TRUE(without_green.state == RoomState::Running || without_green.state == RoomState::Loading);
    } TEST_END();
}

void run_match_tests() {
    TEST_CASE("S3.4 A Whole Match: Three Clients Join By Code, The Match Starts By Itself, The Referee Plays Along Bit-Identically To The End, The Result Is Kept") {
        World w;
        RoomSpec spec = spec_of("MATCH-1", 3);
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& a = w.connect("Ann", "MATCH-1", 2);                                     // asks for seat 2
        Client& b = w.connect("Bob", "MATCH-1");                                        // any: the first free one, seat 0
        w.run(300);
        ASSERT_TRUE(w.status("MATCH-1").state == RoomState::Waiting && w.status("MATCH-1").joined == 2);
        Client& c = w.connect("Cat", "MATCH-1", 3, {40, 20});                           // asks for seat 3: the roster is 0, 2, 3
        w.run(1500);
        RoomStatus s = w.status("MATCH-1");
        ASSERT_TRUE(s.state == RoomState::Running);                                     // nobody pressed START: the room started itself
        ASSERT_EQ(a.lobby->my_seat(), 2);
        ASSERT_EQ(c.lobby->my_seat(), 3);
        ASSERT_EQ(b.lobby->my_seat(), 0);
        ASSERT_EQ(s.names[2], std::string("Ann"));
        ASSERT_EQ(s.names[0], std::string("Bob"));
        ASSERT_TRUE(a.session != nullptr && b.session != nullptr && c.session != nullptr);
        // play until the match is over (the clock of the map)
        for (int guard = 0; guard < 4000 && w.status("MATCH-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("MATCH-1");
        ASSERT_TRUE(s.state == RoomState::Finished);
        ASSERT_TRUE(s.ticks > 1000 && s.turns > 1000);                                   // (a turn is a tick: turns of 50 ms)
        ASSERT_EQ(s.rows.size(), size_t{3});                                            // three teams, no alliances: three rows
        int winners = 0;
        for (const RoomRow& r : s.rows) winners += r.winner ? 1 : 0;
        ASSERT_TRUE(winners >= 1);
        ASSERT_TRUE(s.rows[0].score >= s.rows[1].score && s.rows[1].score >= s.rows[2].score);     // best first
        // the end is reported once
        ASSERT_EQ(w.mgr.take_ended(w.now).size(), size_t{1});
        ASSERT_EQ(w.mgr.take_ended(w.now).size(), size_t{0});
        // after the grace period the connections are closed (every client has executed the last turn by then); after the keep time the room is forgotten
        w.run(Room::kGraceMs + 500);
        ASSERT_FALSE(a.end->is_open());
        for (Client* p : {&a, &b, &c}) ASSERT_FALSE(p->session->desynced());              // the referee saw no desync
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && c.sim.is_match_over());
        w.run(11 * 60 * 1000);
        ASSERT_EQ(w.mgr.room_count(), size_t{0});
    } TEST_END();

    TEST_CASE("S3.81 The Busy Count (The Public /busy Answer): Rooms Whose Match Loads Or Runs, And The People In Rooms That Wait, Load Or Run; A Bot Is No Person; A Room That Is Closed Or Over Counts Nothing") {
        World w;
        const auto busy = [&w]() { return w.mgr.busy(w.now); };
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // a server that holds nothing
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-A", 2), w.now).ok);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // a room that waits, nobody in it
        w.connect("Ann", "BUSY-A");
        w.run(300);
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Waiting);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 1);                          // a person who waits in a lobby: no match yet
        w.connect("Bob", "BUSY-A");
        bool saw_loading = false;
        for (int guard = 0; guard < 400 && w.status("BUSY-A").state != RoomState::Running; ++guard) {
            w.run(10);
            if (w.status("BUSY-A").state == RoomState::Loading) {
                saw_loading = true;
                ASSERT_TRUE(busy().matches == 1 && busy().players == 2);                  // the match is loading: a restart now would cancel the start, it counts
            }
        }
        ASSERT_TRUE(saw_loading);
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Running);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 2);                          // the match runs
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-B", 4), w.now).ok);
        w.connect("Cy", "BUSY-B");
        w.run(300);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 3);                          // a second room only waits
        RoomSpec with_bot = spec_of("BUSY-C", 3);
        with_bot.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
        ASSERT_TRUE(w.mgr.create_room(with_bot, w.now).ok);
        w.connect("Di", "BUSY-C");
        w.run(300);
        ASSERT_TRUE(w.status("BUSY-C").joined == 2 && w.status("BUSY-C").bots.size() == 1);
        ASSERT_TRUE(busy().matches == 1 && busy().players == 4);                          // the bot of room C is not counted: Ann, Bob, Cy and Di
        ASSERT_TRUE(w.mgr.close_room("BUSY-A", w.now));                                   // the owner closes the running match
        ASSERT_TRUE(w.status("BUSY-A").state == RoomState::Failed);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 2);                          // it counts nothing now: Cy and Di still wait
        ASSERT_TRUE(w.mgr.close_room("BUSY-B", w.now) && w.mgr.close_room("BUSY-C", w.now));
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);
        // a match that ends by itself stops counting
        ASSERT_TRUE(w.mgr.create_room(spec_of("BUSY-D", 2), w.now).ok);
        w.connect("Ed", "BUSY-D");
        w.connect("Flo", "BUSY-D");
        w.run(1500);
        ASSERT_TRUE(w.status("BUSY-D").state == RoomState::Running && busy().matches == 1 && busy().players == 2);
        for (int guard = 0; guard < 4000 && w.status("BUSY-D").state == RoomState::Running; ++guard) w.run(250);
        ASSERT_TRUE(w.status("BUSY-D").state == RoomState::Finished);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);                          // over: nobody plays, a restart would cut nothing
    } TEST_END();

    TEST_CASE("S3.5 A Failed Start Is Cancelled And Tried Again: A Client That Cannot Load The Map Does Not Spoil The Room; Too Many Failed Starts End It") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("RETRY-1", 2), w.now).ok);
        Client& a = w.connect("Ann", "RETRY-1");
        Client& b = w.connect("Bob", "RETRY-1");
        b.fail_load = true;
        w.run(1500);
        ASSERT_TRUE(w.status("RETRY-1").state == RoomState::Waiting);                   // cancelled: back to waiting (and a pause before the next try)
        b.fail_load = false;
        w.run(4000);                                                                     // the room tries again by itself
        ASSERT_TRUE(w.status("RETRY-1").state == RoomState::Running);
        (void)a;
        // a client that always fails: the room gives up after a few starts
        World v;
        ASSERT_TRUE(v.mgr.create_room(spec_of("RETRY-2", 2), v.now).ok);
        v.connect("Ann", "RETRY-2");
        Client& bad = v.connect("Bad", "RETRY-2");
        bad.fail_load = true;
        v.run(25000);                                                                    // five starts, two seconds apart
        const RoomStatus s = v.status("RETRY-2");
        ASSERT_TRUE(s.state == RoomState::Failed);
        ASSERT_TRUE(s.reason.find("start failed") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.6 Nobody Came, And The Owner Closes: A Room That Does Not Fill Fails After Its Wait; Closing A Running Room Drops Everybody") {
        World w;
        RoomSpec spec = spec_of("WAIT-1", 2);
        spec.wait_ms = 3000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& a = w.connect("Ann", "WAIT-1");
        w.run(2000);
        ASSERT_TRUE(w.status("WAIT-1").state == RoomState::Waiting);
        w.run(2000);
        RoomStatus s = w.status("WAIT-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("1 of 2") != std::string::npos);
        ASSERT_FALSE(a.end->is_open());                                                 // the waiting client is let go
        // closing a running match
        World x;
        ASSERT_TRUE(x.mgr.create_room(spec_of("CLOSE-1", 2), x.now).ok);
        Client& p = x.connect("P", "CLOSE-1");
        Client& q = x.connect("Q", "CLOSE-1");
        x.run(3000);
        ASSERT_TRUE(x.status("CLOSE-1").state == RoomState::Running);
        ASSERT_TRUE(x.mgr.close_room("CLOSE-1", x.now));
        x.run(200);
        ASSERT_TRUE(x.status("CLOSE-1").state == RoomState::Failed && x.status("CLOSE-1").reason == "closed by the owner");
        ASSERT_FALSE(p.end->is_open());
        ASSERT_FALSE(q.end->is_open());
    } TEST_END();

    TEST_CASE("S3.7 A Player Who Leaves During The Match Is Dropped For Everybody At The Same Tick; When Everybody Has Left The Room Finishes") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("DROP-1", 3), w.now).ok);
        Client& a = w.connect("A", "DROP-1");
        Client& b = w.connect("B", "DROP-1");
        Client& c = w.connect("C", "DROP-1");
        w.run(4000);
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
        w.net.cut(b.end);                                                                // B's link dies
        w.run(3000);
        ASSERT_TRUE(a.sim.is_player_dropped(1) && c.sim.is_player_dropped(1));
        ASSERT_TRUE(b.lost);                                                             // B has no host to elect: its match is over
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);                     // the others play on
        w.net.cut(a.end);
        w.net.cut(c.end);
        w.run(3000);
        ASSERT_TRUE(w.status("DROP-1").state == RoomState::Finished);
        ASSERT_TRUE(w.status("DROP-1").reason == "everybody left" || w.status("DROP-1").reason == "the match ended");
    } TEST_END();
}

// The room's leader (protocol 7): the first player who joined may start the match before every seat is taken
void run_leader_tests() {
    TEST_CASE("S3.26 The Leader Starts A Room For Four With Two Players: The Match Runs With Their Two Seats To The End, The Referee And Both Clients Agree, The Status Keeps What Was Asked For And Shows Who Joined; A Player Who Is Not The Leader Is Ignored") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEAD-1", 4), w.now).ok);
        ASSERT_TRUE(w.status("LEAD-1").early_start);                                     // on by default
        ASSERT_EQ(w.status("LEAD-1").leader, 255);                                       // nobody has joined yet
        Client& ann = w.connect("Ann", "LEAD-1");
        Client& bob = w.connect("Bob", "LEAD-1");
        w.run(500);
        RoomStatus s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.expected == 4);
        ASSERT_EQ(s.leader, 0);                                                          // Ann was welcomed first: seat 0
        ASSERT_TRUE(ann.lobby->room().leader == 0 && bob.lobby->room().leader == 0);     // everybody is told
        ASSERT_TRUE(ann.lobby->is_leader() && !bob.lobby->is_leader());
        // Bob does not lead: a request of his (from a client that is not the game's) is ignored and counted, and does not cost him his seat
        bob.end->send(net::encode(net::StartRequestMsg{}));
        w.run(1500);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 1);
        ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::InRoom);
        // Ann's request starts the match at once with the two of them
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(1500);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(s.expected, 4);                                                         // as asked
        ASSERT_EQ(s.joined, 2);                                                           // who is there
        ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Bob" && s.names[2].empty() && s.names[3].empty());
        ASSERT_EQ(s.ignored_start_requests, 1u);                                          // (Ann's was honoured, not ignored)
        for (int i = 0; i < 3; ++i) ann.end->send(net::encode(net::StartRequestMsg{}));   // the leader's second, third and fourth click cross the Start: the running match counts them too
        w.run(500);
        ASSERT_EQ(w.status("LEAD-1").ignored_start_requests, 4u);
        ASSERT_TRUE(w.status("LEAD-1").state == RoomState::Running && ann.session != nullptr && !ann.lost);
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x03 && bob.sim.roster_mask() == 0x03);      // the roster is the seats that are taken
        Client& late = w.connect("Late", "LEAD-1");                                      // a running match takes nobody new
        w.run(500);
        ASSERT_TRUE(late.lobby->phase() == net::ClientLobby::Phase::Rejected && late.lobby->reject_reason() == net::RejectReason::MatchRunning);
        // played to the end: the room finished (it would have failed with a desync) and both clients stand where the referee stands
        for (int guard = 0; guard < 4000 && w.status("LEAD-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("LEAD-1");
        ASSERT_TRUE(s.state == RoomState::Finished);
        ASSERT_TRUE(s.ticks > 1000 && s.turns > 1000);                                    // (a turn is a tick since protocol 8: this said 500 for turns of two ticks)
        ASSERT_EQ(s.rows.size(), size_t{2});                                              // two teams: two rows
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_TRUE(ann.sim.state_hash() == bob.sim.state_hash());
        ASSERT_TRUE(ann.sim.is_match_over() && bob.sim.is_match_over());
        ASSERT_EQ(ann.sim.current_tick(), bob.sim.current_tick());
    } TEST_END();

    TEST_CASE("S3.33 The Leader And The Lag Policy: A Waiting Room Has No Lag And No Notice; A Leader Whose Window Stops After The Start Does Not Hold The Room Up, The Other Player Is Told That It Lags, Its Late Clicks On START Are Counted And Cost It Nothing, It Catches Up (\"Catching Up\") And Is Not Dropped; Both End Bit-Identical") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEAD-LAG", 4), w.now).ok);
        Client& ann = w.connect("Ann", "LEAD-LAG");
        Client& bob = w.connect("Bob", "LEAD-LAG");
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader() && !bob.lobby->is_leader());
        w.run(20000);                                                                       // twenty seconds in the waiting room: nothing is sealed, so nobody is behind, nobody is told, nobody is dropped
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Waiting && w.status("LEAD-LAG").joined == 2);
        ASSERT_TRUE(ann.session == nullptr && bob.session == nullptr);
        ASSERT_TRUE(ann.lobby->is_leader() && ann.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(3000 + kPre);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr);
        ASSERT_EQ(w.status("LEAD-LAG").leader, 255);                                        // once the match runs nobody leads: the lobby is over
        ASSERT_EQ(bob.session->lagging_seat(), 255);
        // the leader's window stops for 8 s; three clicks on START that it made in its last moment reach the server late
        const uint32_t ticks_before = w.status("LEAD-LAG").ticks;
        ann.freeze = true;
        for (int i = 0; i < 3; ++i) ann.end->send(net::encode(net::StartRequestMsg{}));
        w.run(8000);
        ASSERT_TRUE(w.status("LEAD-LAG").ticks - ticks_before >= 150);                      // the room did not wait for it: 8 s are 160 ticks, and Bob played them
        ASSERT_EQ(bob.session->lagging_seat(), 0);                                          // Bob is told who lags: the seat of the first player
        ASSERT_TRUE(bob.session->lagging_behind_ms() >= 3000);
        ASSERT_EQ(w.status("LEAD-LAG").ignored_start_requests, 3u);                         // the three clicks are counted, they were free (16 are), nothing else came of them
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ann.freeze = false;
        w.run(300);
        ASSERT_TRUE(ann.session->catching_up());                                            // "Catching up..." on the leader's screen, until it is within a second
        w.run(7000);
        ASSERT_FALSE(ann.session->catching_up());
        ASSERT_EQ(bob.session->lagging_seat(), 255);                                        // and the notice of the other player ended
        ASSERT_FALSE(ann.lost || bob.lost);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Running);
        ASSERT_TRUE(ann.sim.current_tick() + 40 >= bob.sim.current_tick());                 // level (within the turns that are on the way)
        // played to the end: both clients stand where the referee stands, and the room did not fail with a desync
        for (int guard = 0; guard < 4000 && w.status("LEAD-LAG").state == RoomState::Running; ++guard) w.run(250);
        ASSERT_TRUE(w.status("LEAD-LAG").state == RoomState::Finished);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_TRUE(ann.sim.state_hash() == bob.sim.state_hash());
        ASSERT_EQ(ann.sim.current_tick(), bob.sim.current_tick());
    } TEST_END();

    TEST_CASE("S3.27 A Request That Cannot Be Honoured Does Nothing: One Player Alone, A Room That Does Not Allow An Early Start (No Leader); A Room Whose Seats Are All Taken Still Starts By Itself") {
        {   // one player alone: nothing happens however often the leader asks, and the leader's screen stays (it is not thrown out)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("ONE-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "ONE-1");
            w.run(300);
            for (int i = 0; i < 5; ++i) {
                ASSERT_TRUE(ann.lobby->request_start());
                w.run(100);
            }
            w.run(1500);
            RoomStatus s = w.status("ONE-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 1 && s.ignored_start_requests == 5);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Bob", "ONE-1");                                                    // a second player: the next request works
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(1500);
            ASSERT_TRUE(w.status("ONE-1").state == RoomState::Running);
        }
        {   // early_start off: the room has no leader, the request of a modified client is ignored and counted, and the room starts when every seat is taken
            World w;
            RoomSpec spec = spec_of("NOEARLY-1", 3);
            spec.early_start = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "NOEARLY-1");
            Client& bob = w.connect("Bob", "NOEARLY-1");
            w.run(500);
            RoomStatus s = w.status("NOEARLY-1");
            ASSERT_TRUE(!s.early_start && s.leader == 255);
            ASSERT_TRUE(ann.lobby->room().leader == 255 && !ann.lobby->is_leader());     // nobody is told that they lead
            ASSERT_FALSE(ann.lobby->request_start());                                     // so the client sends nothing
            ann.end->send(net::encode(net::StartRequestMsg{}));
            bob.end->send(net::encode(net::StartRequestMsg{}));
            w.run(2000);
            s = w.status("NOEARLY-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 2);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Cat", "NOEARLY-1");                                                // the third seat: it starts by itself, as ever
            w.run(1500);
            ASSERT_TRUE(w.status("NOEARLY-1").state == RoomState::Running);
        }
        {   // every seat taken: the room starts by itself, nobody has to ask (and nothing is counted)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FULL-1", 3), w.now).ok);
            for (const char* name : {"Ann", "Bob", "Cat"}) w.connect(name, "FULL-1");
            w.run(2000);
            const RoomStatus s = w.status("FULL-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.ignored_start_requests == 0);
        }
        {   // two of three with no request: the room waits (min_players is for the leader's request, not for the room)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("TWO-1", 3), w.now).ok);
            w.connect("Ann", "TWO-1");
            w.connect("Bob", "TWO-1");
            w.run(5000);
            ASSERT_TRUE(w.status("TWO-1").state == RoomState::Waiting);
        }
    } TEST_END();

    TEST_CASE("S3.28 The Leader Leaves: The Earliest Player Who Is Left Leads And Can Start; The Seat That Was Left Is Free And Is Not In The Roster") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("LEFT-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "LEFT-1");
        Client& bob = w.connect("Bob", "LEFT-1");
        Client& cat = w.connect("Cat", "LEFT-1");
        w.run(500);
        ASSERT_EQ(w.status("LEFT-1").leader, 0);
        ann.lobby->leave();                                                               // the leader goes
        w.run(500);
        RoomStatus s = w.status("LEFT-1");
        ASSERT_TRUE(s.joined == 2 && s.leader == 1 && s.state == RoomState::Waiting);     // Bob, the earlier of Bob and Cat
        ASSERT_TRUE(bob.lobby->is_leader() && !cat.lobby->is_leader());
        ASSERT_TRUE(bob.lobby->room().leader == 1 && cat.lobby->room().leader == 1);      // both were told with the next Room message
        cat.end->send(net::encode(net::StartRequestMsg{}));                               // Cat does not lead: ignored
        w.run(1500);
        s = w.status("LEFT-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.ignored_start_requests == 1);
        ASSERT_TRUE(bob.lobby->request_start());                                          // Bob starts with himself and Cat: seats 1 and 2
        w.run(1500);
        s = w.status("LEFT-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(s.names[0].empty() && s.names[1] == "Bob" && s.names[2] == "Cat" && s.names[3].empty());
        ASSERT_TRUE(bob.sim.roster_mask() == 0x06 && cat.sim.roster_mask() == 0x06);
        w.run(5000 + kPre);
        ASSERT_FALSE(bob.session->desynced() || cat.session->desynced());
        ASSERT_TRUE(w.status("LEFT-1").state == RoomState::Running && w.status("LEFT-1").ticks > 60);
        {   // a leader who asks and leaves in the same breath (both reach the room in one pass) asked for nothing: the room does not start with the two who are left
            World v;
            ASSERT_TRUE(v.mgr.create_room(spec_of("LEFT-2", 4), v.now).ok);
            Client& a = v.connect("Ann", "LEFT-2", 255, {20, 0});                          // (no jitter: the two messages arrive together)
            Client& b = v.connect("Bob", "LEFT-2");
            v.connect("Cat", "LEFT-2");
            v.run(500);
            a.end->send(net::encode(net::StartRequestMsg{}));
            a.end->send(net::encode_leave());
            v.run(2500);
            const RoomStatus t = v.status("LEFT-2");
            ASSERT_TRUE(t.state == RoomState::Waiting && t.joined == 2 && t.leader == 1);
            ASSERT_TRUE(b.lobby->is_leader());                                              // Bob leads now, and did not ask for anything
            ASSERT_TRUE(b.lobby->request_start());                                          // when he does, the room starts
            v.run(1500);
            ASSERT_TRUE(v.status("LEFT-2").state == RoomState::Running);
        }
    } TEST_END();

    TEST_CASE("S3.29 The Early Start Meets The Room's Other Rules: A Client That Cannot Load Cancels It And The Leader Asks Again After The Pause (A Request In The Pause Is Lost); Seats The Map Cannot Be Played By Do Not Start Early And Do Not Fail The Room") {
        {
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("PAUSE-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "PAUSE-1");
            Client& bob = w.connect("Bob", "PAUSE-1");
            bob.fail_load = true;                                                          // Bob's copy of the map is no good
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(1000);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);                  // the start was cancelled, the room waits a moment (two seconds) before another try
            bob.fail_load = false;                                                         // Bob's map is fine now: a start would work, if the room tried
            ASSERT_TRUE(ann.lobby->request_start());                                       // a request in the pause is lost (it does not start the match, and it is not kept for later)
            w.run(500);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            w.run(2500);                                                                   // the pause is over, and nobody asked again: two of four players do not start by themselves
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            ASSERT_TRUE(ann.lobby->request_start());
            w.run(2000);
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Running);
        }
        // TINY with the green start marker moved outside the grid (as in S3.14): the engine loads it, but green cannot play it
        const std::string dir = temp_dir_for("early_marker");
        {
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_in = [&dir](World& w, uint32_t ms) {                                // (the clients load the map from this folder)
            for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                w.now += 10;
                w.net.set_time(w.now);
                w.mgr.update(w.now);
                for (auto& c : w.clients) c->update(w.now, dir);
            }
        };
        {   // green (seat 0) and red in a room for four: the leader's request is not honoured (green cannot play this map), the room does not fail, nobody is thrown out
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MARK-1", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MARK-1", 0);
            w.connect("Bob", "MARK-1", 1);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start());
            run_in(w, 2000);
            RoomStatus s = w.status("MARK-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2);
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            w.connect("Cat", "MARK-1", 2);                                                // with more players still waiting, the room goes on waiting
            w.connect("Dan", "MARK-1", 3);                                                // when every seat is taken (green among them) the room fails, as it always did
            run_in(w, 2000);
            s = w.status("MARK-1");
            ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("outside") != std::string::npos);
        }
        {   // red and blue: the roster does not contain green, the same map starts early
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MARK-2", 4, "BAD.LVL"), w.now).ok);
            Client& bob = w.connect("Bob", "MARK-2", 1);
            w.connect("Cat", "MARK-2", 2);
            run_in(w, 500);
            ASSERT_TRUE(bob.lobby->request_start());
            run_in(w, 2000);
            ASSERT_TRUE(w.status("MARK-2").state == RoomState::Running);
        }
        std::error_code ignore;
        fs::remove_all(dir, ignore);
    } TEST_END();

    TEST_CASE("S3.30 The Control Interface Knows The Early Start: early_start Goes In And Comes Out (On By Default; A Wrong Type Is A 400), The Status Names The Leader And Counts The Requests That Were Ignored; Demo Rooms Have It On And Their Leader Starts Them") {
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        World w(limits);
        const auto call = [&w](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(w.mgr, rq, w.now);
        };
        const auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-1"})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("early_start").is_bool() && v.get("early_start").as_bool_or(false));   // the default: on
        ASSERT_TRUE(v.get("leader").is_null() && v.get("ignored_start_requests").as_int_or(9) == 0);
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-2","early_start":false})");
        ASSERT_EQ(r.status, 201);
        v = json_of(r);
        ASSERT_TRUE(v.get("early_start").is_bool() && !v.get("early_start").as_bool_or(true));
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"J-3","early_start":true})");
        ASSERT_EQ(r.status, 201);
        ASSERT_TRUE(json_of(r).get("early_start").as_bool_or(false));
        for (const char* bad : {R"("yes")", "1", "0", "null", "[true]", "{}", R"("")"}) {          // only true or false
            r = call("POST", "/rooms", std::string(R"({"map":"TINY.LVL","early_start":)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            ASSERT_TRUE(json_of(r).get("error").str().find("early_start") != std::string::npos);
        }
        ASSERT_EQ(w.mgr.room_count(), size_t{3});                                         // (none of the bad ones made a room)
        r = call("GET", "/rooms/J-2");
        ASSERT_TRUE(r.status == 200 && !json_of(r).get("early_start").as_bool_or(true));
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 3);
        int on = 0;
        const ctl::JsonValue all = json_of(r);
        for (size_t i = 0; i < all.get("rooms").size(); ++i) on += all.get("rooms").at(i).get("early_start").as_bool_or(false) ? 1 : 0;
        ASSERT_EQ(on, 2);
        // the leader and the ignored requests, seen through the interface
        Client& ann = w.connect("Ann", "J-1");
        Client& bob = w.connect("Bob", "J-1");
        w.run(500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("leader").as_int_or(9) == 0 && v.get("joined").as_int_or(0) == 2 && v.get("players").size() == 2);
        bob.end->send(net::encode(net::StartRequestMsg{}));
        w.run(500);
        ASSERT_EQ(json_of(call("GET", "/rooms/J-1")).get("ignored_start_requests").as_int_or(0), 1);
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(1500);
        v = json_of(call("GET", "/rooms/J-1"));
        ASSERT_TRUE(v.get("state").str() == "running" && v.get("expected").as_int_or(0) == 4 && v.get("joined").as_int_or(0) == 2);
        ASSERT_TRUE(v.get("early_start").as_bool_or(false));
        ASSERT_TRUE(v.get("leader").is_null());                                           // the lead means something until the match runs
        // a room with early_start off names no leader, and says so
        Client& cat = w.connect("Cat", "J-2");
        w.connect("Dan", "J-2");
        w.run(500);
        v = json_of(call("GET", "/rooms/J-2"));
        ASSERT_TRUE(v.get("leader").is_null() && !v.get("early_start").as_bool_or(true) && v.get("joined").as_int_or(0) == 2);
        ASSERT_FALSE(cat.lobby->is_leader());
        // a demo room has the early start on, and its leader can start it
        Client& eve = w.connect("Eve", "demo-small-4p-x1");
        w.connect("Fay", "demo-small-4p-x1");
        w.run(500);
        v = json_of(call("GET", "/rooms/demo-small-4p-x1"));
        ASSERT_TRUE(v.get("early_start").as_bool_or(false) && v.get("leader").as_int_or(9) == 0 && v.get("state").str() == "waiting");
        ASSERT_TRUE(eve.lobby->is_leader());
        ASSERT_TRUE(eve.lobby->request_start());
        w.run(1500);
        ASSERT_TRUE(json_of(call("GET", "/rooms/demo-small-4p-x1")).get("state").str() == "running");
    } TEST_END();

    TEST_CASE("S3.31 Over Real Sockets: The Leader Of A Room For Four Starts It With Two Players, Both Play Bit-Identically") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-4", 4), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-4";
            clients.back()->start(links.back().get(), 55u);
        }
        const auto pump = [&]() {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        };
        for (int i = 0; i < 3000; ++i) {                                                  // both are in the room (the room waits for four)
            pump();
            RoomStatus s;
            mgr.status("SOCK-4", s, now);
            if (s.joined == 2 && clients[0]->lobby->room().slots[1].state == net::SlotState::Client && clients[0]->lobby->is_leader()) break;
        }
        RoomStatus s;
        ASSERT_TRUE(mgr.status("SOCK-4", s, now));
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.leader < 4);
        ASSERT_TRUE(clients[0]->lobby->is_leader() && !clients[1]->lobby->is_leader());
        for (int i = 0; i < 100; ++i) pump();
        ASSERT_TRUE(clients[0]->lobby->request_start());
        for (int i = 0; i < 6000; ++i) {
            pump();
            mgr.status("SOCK-4", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 400 && s.joined == 2 && s.expected == 4);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        ASSERT_TRUE(clients[0]->sim.roster_mask() == 0x03 && clients[1]->sim.roster_mask() == 0x03);
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            pump();
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
    } TEST_END();
}

void run_control_tests() {
    TEST_CASE("S3.8 Control Interface: Make, Look At, List And Close Rooms With JSON; Every Mistake Gets An Error Object And The Right Status") {
        RoomManager mgr{MapStore(maps_dir())};
        auto call = [&](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(mgr, rq, 5000);
        };
        auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);                                            // (a body that does not parse gives null: the asserts below then fail)
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":3,"fog":true,"code":"CTL-1","seed":7,"wait_seconds":30})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("code").str() == "CTL-1" && v.get("state").str() == "waiting" && v.get("expected").as_int_or(0) == 3 && v.get("fog").as_bool_or(false));
        ASSERT_TRUE(v.get("map").str() == "TINY.LVL" && v.get("joined").as_int_or(9) == 0 && v.get("players").size() == 0);
        r = call("GET", "/rooms/CTL-1");
        ASSERT_EQ(r.status, 200);
        ASSERT_TRUE(json_of(r).get("code").str() == "CTL-1");
        r = call("POST", "/rooms", R"({"map":"SMALL.LVL"})");                           // the code is drawn
        ASSERT_EQ(r.status, 201);
        const std::string drawn = json_of(r).get("code").str();
        ASSERT_TRUE(drawn.size() == 8 && net::valid_room_code(drawn));
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 2);
        r = call("GET", "/stats");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").as_int_or(0) == 2 && json_of(r).get("created").as_int_or(0) == 2);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","code":"CTL-1"})").status, 409);
        ASSERT_EQ(call("POST", "/rooms", "").status, 400);
        ASSERT_EQ(call("POST", "/rooms", "{not json").status, 400);
        ASSERT_EQ(call("POST", "/rooms", "[1,2]").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"players":2})").status, 400);               // no map
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","players":"two"})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","players":9})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","fog":"yes"})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","wait_seconds":0})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","max_run_seconds":59})").status, 400);          // a match lasts at least a minute
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","max_run_seconds":86401})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"TINY.LVL","seed":-1})").status, 400);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"../../etc/passwd"})").status, 404);
        ASSERT_EQ(call("POST", "/rooms", R"({"map":"NOSUCH.LVL"})").status, 404);
        ASSERT_EQ(call("GET", "/rooms/NOPE").status, 404);
        ASSERT_EQ(call("GET", "/rooms/").status, 404);
        ASSERT_EQ(call("GET", "/rooms/../stats").status, 404);
        ASSERT_EQ(call("GET", "/rooms/a%2Fb").status, 404);
        ASSERT_EQ(call("PUT", "/rooms/CTL-1").status, 405);
        ASSERT_EQ(call("DELETE", "/rooms").status, 405);
        ASSERT_EQ(call("GET", "/nothing").status, 404);
        r = call("DELETE", "/rooms/CTL-1");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("state").str() == "failed" && json_of(r).get("reason").str() == "closed by the owner");
        ASSERT_EQ(call("DELETE", "/rooms/NOPE").status, 404);
        // every error body is a JSON object with an "error" text
        r = call("POST", "/rooms", "{not json");
        ASSERT_TRUE(json_of(r).get("error").is_string());
    } TEST_END();
}

void run_socket_tests() {
    TEST_CASE("S3.9 Over Real Sockets: Two Clients Connect To The Server's TCP Port, Join A Room By Its Code And Play Bit-Identically") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-1", 2), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-1";
            clients.back()->start(links.back().get(), 77u);
        }
        for (int i = 0; i < 6000; ++i) {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            RoomStatus s;
            mgr.status("SOCK-1", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        RoomStatus s;
        ASSERT_TRUE(mgr.status("SOCK-1", s, now));
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(s.ticks > 400);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        // the two machines are bit-identical at any moment when they stand at the same tick
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            now += 10;
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("S3.32 Over Real Sockets: A Raw Client That Floods The Server With Valid Messages (StartRequests, Pings) Is Dropped Within Two Seconds; The Server Does Not Grow, Never Stalls, Falls Back To Idle, And A Match In Another Room Keeps Its Clock Of 20 Ticks A Second") {
        std::signal(SIGPIPE, SIG_IGN);
        for (const std::string kind : {"startreq", "ping"}) {
            RoomManager mgr{MapStore(maps_dir())};
            auto listener = net::TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            const auto t0 = std::chrono::steady_clock::now();
            const auto clock_ms = [&]() { return 1000u + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
            uint32_t now = clock_ms();
            ASSERT_TRUE(mgr.create_room(spec_of("VICTIM", 2), now).ok);
            ASSERT_TRUE(mgr.create_room(spec_of("FLOOD", 4), now).ok);
            std::vector<std::unique_ptr<Client>> clients;
            std::vector<std::unique_ptr<net::TcpConnection>> links;
            for (const char* name : {"Ann", "Bob"}) {
                links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
                ASSERT_TRUE(links.back() != nullptr);
                clients.push_back(std::make_unique<Client>());
                clients.back()->name = name;
                clients.back()->room = "VICTIM";
                clients.back()->start(links.back().get(), 91u);
            }
            double max_iteration_ms = 0;
            const auto pump = [&]() {                                                   // the server's main loop: one pass, then the 2 ms nap of ants_server
                const auto begin = std::chrono::steady_clock::now();
                now = clock_ms();
                for (int k = 0; k < 4; ++k) {
                    auto c = listener->accept();
                    if (!c) break;
                    mgr.add_connection(std::move(c), "127.0.0.1", now);
                }
                mgr.update(now);
                for (auto& c : clients) c->update(now, maps_dir());
                max_iteration_ms = std::max(max_iteration_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            };
            const auto ticks_of_victim = [&]() {
                RoomStatus v;
                mgr.status("VICTIM", v, now);
                return v.ticks;
            };
            RoomStatus s;
            for (int i = 0; i < 4000; ++i) {
                pump();
                mgr.status("VICTIM", s, now);
                if (s.state == RoomState::Running && s.ticks > 60) break;
            }
            ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 60);
            // the clock of the victim's referee without a flood: 20 ticks a second
            uint32_t ticks_a = ticks_of_victim();
            const auto timer_a = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - timer_a < std::chrono::milliseconds(1500)) pump();
            const double quiet_rate = (ticks_of_victim() - ticks_a) / std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_a).count();
            ASSERT_TRUE(quiet_rate > 17.0 && quiet_rate < 23.0);
            // the flood: a raw client in the other room
            max_iteration_ms = 0;
            const double memory_before = peak_memory_mb();
            ticks_a = ticks_of_victim();
            const auto timer_b = std::chrono::steady_clock::now();
            const std::vector<uint8_t> message = kind == "startreq" ? net::encode(net::StartRequestMsg{}) : net::encode_ping(net::PingMsg{1, 0});
            FloodPeer flood(listener->port(), "FLOOD", message);
            ASSERT_TRUE(flood.sock >= 0);
            double dropped_after = -1.0;                                                // when the room lost the flooder it had seated, seconds after the flood began
            bool seated = false;
            uint32_t pongs = 0;
            while (std::chrono::steady_clock::now() - timer_b < std::chrono::milliseconds(2500)) {       // (a fixed window: the flooder is gone long before it ends)
                pump();
                flood.read_server(pongs);
                RoomStatus r;
                mgr.status("FLOOD", r, now);
                seated = seated || r.joined > 0;
                if (seated && r.joined == 0 && dropped_after < 0.0) dropped_after = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_b).count();
            }
            const double flood_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_b).count();
            ASSERT_EQ(mgr.connections_refused(), uint64_t{0});                          // (the flooder's Hello was taken: it had a seat)
            if (kind == "ping") {
                // the pings need at least 16 updates (64 of a connection per update) to use up the budget, so the seat is seen, and then the drop: within two seconds
                ASSERT_TRUE(seated);
                ASSERT_TRUE(dropped_after >= 0.0 && dropped_after < 2.0);
                ASSERT_TRUE(pongs >= net::kMessageBurst && pongs <= net::kMessageBurst + 300);        // a second's worth was answered, not the millions that were sent
            }
            const double flood_rate = (ticks_of_victim() - ticks_a) / flood_seconds;
            // after the drop the loop has nothing to digest: it is idle again, the victim keeps its clock, and nothing grew
            const double cpu_before = process_cpu_seconds();
            const uint32_t ticks_c = ticks_of_victim();
            const auto timer_c = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - timer_c < std::chrono::milliseconds(1500)) pump();
            const double idle_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_c).count();
            const double idle_cpu = (process_cpu_seconds() - cpu_before) / idle_seconds;
            const double after_rate = (ticks_of_victim() - ticks_c) / idle_seconds;
            const double memory_growth = peak_memory_mb() - memory_before;
            RoomStatus f;
            ASSERT_TRUE(mgr.status("FLOOD", f, now));
            ASSERT_TRUE(f.state == RoomState::Waiting && f.joined == 0);                // it left the room (a flooder's seat is free again)
            ASSERT_EQ(f.ignored_start_requests, kind == "startreq" ? net::kIgnoredStartRequestsAllowed + 8u : 0u);   // it had a seat, and was out at its 24th request: the rest was not even read
            ASSERT_TRUE(flood_rate > 16.0);                                             // the other room kept its clock while the flood lasted (the loop never stalled)
            ASSERT_TRUE(after_rate > 17.0 && after_rate < 23.0);
            ASSERT_TRUE(max_iteration_ms < 400.0);                                      // no pass of the main loop took long, with the flood on
            ASSERT_TRUE(idle_cpu < 0.5);                                                // the process does not stay busy digesting a backlog (it had one of gigabytes)
            ASSERT_TRUE(memory_growth < 200.0);                                         // and did not grow by the flood (gigabytes, before the inbox was bounded)
            ASSERT_TRUE(mgr.status("VICTIM", s, now) && s.state == RoomState::Running);
            ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        }
    } TEST_END();
#endif
}

namespace {

#ifndef _WIN32
// umask 0 for the life of the object: a mode that the code asks for is then the mode that the file gets. A umask can only take bits away, so under a strict umask
// (077, as hardened hosts have) a wrong mode would hide behind it and the check on the mode could not fail.
struct ZeroUmask {
    mode_t previous;
    ZeroUmask() : previous(::umask(0)) {}
    ~ZeroUmask() { ::umask(previous); }
    ZeroUmask(const ZeroUmask&) = delete;
    ZeroUmask& operator=(const ZeroUmask&) = delete;
};

// The highest resident memory of this process so far (macOS counts bytes, Linux kilobytes)
size_t peak_rss_bytes() {
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss);
#else
    return static_cast<size_t>(usage.ru_maxrss) * 1024;
#endif
}
#endif

size_t entries_in(const fs::path& dir) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++n;
    }
    return n;
}

// The entries of a restart folder besides its lock file (the file `.lock` is the folder's own since M5 of the review: a store that has prepared the folder holds it)
size_t entries_but_lock(const fs::path& dir) {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) n += e.path().filename() == ".lock" ? 0u : 1u;
    return n;
}

}  // namespace

// The control secret: from the environment, or made once and kept in a file (secret.hpp)
void run_secret_tests() {
    auto fresh_dir = [](const char* tag) {
        const fs::path d = scratch_root() / (std::string("secret_") + tag);
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d, ec);
        return d;
    };
    auto slurp = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    auto put = [](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    auto is_hex64 = [](const std::string& t) {
        if (t.size() != 64) return false;
        for (const char c : t) if (!std::isxdigit(static_cast<unsigned char>(c)) || std::isupper(static_cast<unsigned char>(c))) return false;
        return true;
    };
    // the control interface's own verdict: it refuses to start with a secret that it does not take
    auto http_takes = [](const std::string& s) { return ctl::HttpServer::listen(0, s) != nullptr; };

    TEST_CASE("S3.15 The Control Secret: The Environment Wins And Never Touches The File; Without Both The Server Refuses; Without The Environment It Makes A Random Secret Once, Keeps It Owner-Only (Under Any umask), And Reads The Same One Next Time") {
#ifndef _WIN32
        const ZeroUmask zero_umask;
#endif
        const fs::path dir = fresh_dir("make");
        const std::string file = (dir / "state" / "control-secret").string();      // the folder does not exist yet: it is made
        // the environment wins; no file is made, an existing one is not read
        auto env = server::resolve_secret("from-the-environment-0123456789", file);
        ASSERT_TRUE(env.ok);
        ASSERT_EQ(env.secret, std::string("from-the-environment-0123456789"));
        ASSERT_TRUE(env.source == server::SecretSource::Environment);
        ASSERT_FALSE(fs::exists(file));
        // an environment variable that is set but empty counts as not set
        ASSERT_FALSE(server::resolve_secret("", "").ok);
        auto empty_env = server::resolve_secret("", file);
        ASSERT_TRUE(empty_env.ok);
        ASSERT_TRUE(empty_env.source == server::SecretSource::Generated);
#ifndef _WIN32
        struct stat made;
        ASSERT_EQ(::stat(file.c_str(), &made), 0);
        ASSERT_EQ(static_cast<unsigned>(made.st_mode & 0777), 0600u);
#endif
        ASSERT_TRUE(fs::remove(file));
        // no environment and no place for a file: refused, with a sentence that says what to do
        auto none = server::resolve_secret(nullptr, "");
        ASSERT_FALSE(none.ok);
        ASSERT_TRUE(none.error.find("ANTS_SERVER_SECRET") != std::string::npos);
        ASSERT_TRUE(none.error.find("--secret-file") != std::string::npos);
        // the first start makes it: 64 hex digits, in the file, one line
        auto first = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(first.ok);
        ASSERT_TRUE(first.source == server::SecretSource::Generated);
        ASSERT_EQ(first.path, file);
        ASSERT_TRUE(is_hex64(first.secret));
        ASSERT_EQ(slurp(file), first.secret + "\n");
        ASSERT_TRUE(server::usable_secret_text(first.secret));
        ASSERT_EQ(entries_in(dir / "state"), size_t{1});                            // the temporary file of the making is gone: nothing but the secret file
#ifndef _WIN32
        struct stat st;
        ASSERT_EQ(::stat(file.c_str(), &st), 0);
        ASSERT_EQ(static_cast<unsigned>(st.st_mode & 0777), 0600u);                 // nobody but its owner can read it, and the umask did not have to help
        ASSERT_EQ(static_cast<unsigned>(st.st_nlink), 1u);                          // no second name (the temporary one) is left behind
#endif
        // every later start reads the same secret and does not write the file again
        const auto before = fs::last_write_time(file);
        for (int i = 0; i < 3; ++i) {
            auto again = server::resolve_secret(nullptr, file);
            ASSERT_TRUE(again.ok);
            ASSERT_TRUE(again.source == server::SecretSource::File);
            ASSERT_EQ(again.secret, first.secret);
        }
        ASSERT_TRUE(fs::last_write_time(file) == before);
        ASSERT_EQ(entries_in(dir / "state"), size_t{1});
        // a secret in the environment still wins over a file that exists
        auto over = server::resolve_secret("the-environment-is-stronger-0123456789", file);
        ASSERT_EQ(over.secret, std::string("the-environment-is-stronger-0123456789"));
        ASSERT_EQ(slurp(file), first.secret + "\n");
        // two servers do not share a secret: a second file gets a different one
        auto other = server::resolve_secret(nullptr, (dir / "second").string());
        ASSERT_TRUE(other.ok);
        ASSERT_TRUE(other.secret != first.secret);
        // the HTTP server takes the generated secret
        ASSERT_TRUE(http_takes(first.secret));
    } TEST_END();

    TEST_CASE("S3.16 A Secret File That Is Not A Usable Secret Is Never Overwritten: An Empty, Short, Long, Spaced, Two-Line, Padded Or Binary File, A Directory, A Device, A FIFO And A Folder That Cannot Be Made All Stop The Server With A Sentence") {
        const fs::path dir = fresh_dir("refuse");
        const std::string hex = server::generate_secret_text();
        struct Case {
            const char* name;
            std::string content;
            const char* says = "does not hold a usable secret";
        };
        const std::vector<Case> cases = {
            {"empty", ""},
            {"only a line end", "\n"},
            {"31 characters", std::string(31, 'a') + "\n"},
            {"257 characters", std::string(257, 'a') + "\n"},
            {"a space inside", hex.substr(0, 20) + " " + hex.substr(20) + "\n"},
            {"a space in front", " " + hex + "\n"},
            {"a tab in front", "\t" + hex + "\n"},
            {"a line end in front", "\n" + hex + "\n"},
            {"two lines", hex + "\n" + hex + "\n"},
            {"a control character", hex.substr(0, 40) + std::string(1, '\x01') + hex.substr(40) + "\n"},
            {"a DEL", hex.substr(0, 40) + std::string(1, '\x7f') + hex.substr(40) + "\n"},
            {"a byte above 127", hex.substr(0, 40) + std::string(1, static_cast<char>(0xC3)) + hex.substr(40) + "\n"},
            {"a tab inside", hex.substr(0, 30) + "\t" + hex.substr(30) + "\n"},
            {"1024 bytes that are no secret", std::string(1024, 'x')},                                 // the biggest file that is looked at: still too long a secret
            {"1025 bytes", std::string(1025, 'x'), "is too big to be a secret"},
            {"5000 bytes", std::string(5000, 'x'), "is too big to be a secret"},
            {"a secret and 1100 blanks", hex + std::string(1100, ' ') + "\n", "is too big to be a secret"},       // only the size branch can refuse this one
            {"a secret and 1100 line ends", hex + std::string(1100, '\n'), "is too big to be a secret"},
        };
        for (const Case& c : cases) {
            const fs::path f = dir / "control-secret";
            put(f, c.content);
            auto r = server::resolve_secret(nullptr, f.string());
            ASSERT_MSG(!r.ok, c.name);
            ASSERT_MSG(r.error.find("nothing was changed") != std::string::npos, c.name);
            ASSERT_MSG(r.error.find(c.says) != std::string::npos, std::string(c.name) + ": " + r.error);
            ASSERT_MSG(r.secret.empty(), c.name);
            ASSERT_MSG(slurp(f) == c.content, c.name);                                // not repaired, not replaced
            // the environment is still all it takes to start, and it does not look at the file at all
            ASSERT_MSG(server::resolve_secret("a-good-secret-from-the-environment-0123", f.string()).ok, c.name);
        }
        // a directory where the file should be
        const fs::path as_dir = dir / "is-a-directory";
        fs::create_directories(as_dir);
        auto d = server::resolve_secret(nullptr, as_dir.string());
        ASSERT_FALSE(d.ok);
        ASSERT_TRUE(d.error.find("not a regular file") != std::string::npos);
        // a "folder" that is a file: nothing can be made below it
        const fs::path blocker = dir / "blocker";
        put(blocker, "x");
        auto b = server::resolve_secret(nullptr, (blocker / "control-secret").string());
        ASSERT_FALSE(b.ok);
        ASSERT_TRUE(b.error.find("ANTS_SERVER_SECRET") != std::string::npos);
        ASSERT_EQ(slurp(blocker), std::string("x"));
#ifndef _WIN32
        // a device and a FIFO: refused as "not a regular file" before anything is read (a FIFO that nobody writes to must not make the server wait: the alarm ends a hang)
        ::alarm(30);
        auto z = server::resolve_secret(nullptr, "/dev/zero");
        ASSERT_FALSE(z.ok);
        ASSERT_TRUE(z.error.find("not a regular file") != std::string::npos);
        const fs::path fifo = dir / "a-fifo";
        ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        auto q = server::resolve_secret(nullptr, fifo.string());
        ASSERT_FALSE(q.ok);
        ASSERT_TRUE(q.error.find("not a regular file") != std::string::npos);
        ::alarm(0);
        // a folder that may not be written (not for root, which may write anywhere)
        if (::geteuid() != 0) {
            const fs::path ro = dir / "readonly";
            fs::create_directories(ro);
            ASSERT_EQ(::chmod(ro.c_str(), 0500), 0);
            auto w = server::resolve_secret(nullptr, (ro / "control-secret").string());
            ASSERT_FALSE(w.ok);
            ASSERT_TRUE(w.error.find("cannot be created") != std::string::npos);
            ASSERT_EQ(entries_in(ro), size_t{0});                                     // no file, no temporary file
            ASSERT_EQ(::chmod(ro.c_str(), 0700), 0);
        }
#endif
    } TEST_END();

    TEST_CASE("S3.17 A Secret File That Somebody Wrote By Hand Is Used As It Is: Its Line End, A Carriage Return Or Trailing Blanks Do Not Belong To The Secret; 32 And 256 Characters Are Both Fine") {
        const fs::path dir = fresh_dir("hand");
        const std::string secret = "My-own-secret_with.punctuation/and+symbols=0123456789";
        const std::vector<std::string> shapes = {secret, secret + "\n", secret + "\r\n", secret + " \t\n\n", secret + "\r\n\r\n"};
        for (const std::string& shape : shapes) {
            put(dir / "control-secret", shape);
            auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
            ASSERT_TRUE(r.ok);
            ASSERT_TRUE(r.source == server::SecretSource::File);
            ASSERT_EQ(r.secret, secret);
        }
        for (const size_t n : {size_t{32}, size_t{256}}) {
            put(dir / "control-secret", std::string(n, 'k') + "\n");
            auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
            ASSERT_TRUE(r.ok);
            ASSERT_EQ(r.secret.size(), n);
        }
        // the largest file that is looked at: 1024 bytes, a secret and blanks that are not part of it
        put(dir / "control-secret", std::string(256, 'k') + std::string(768, ' '));
        auto padded = server::resolve_secret(nullptr, (dir / "control-secret").string());
        ASSERT_TRUE(padded.ok);
        ASSERT_EQ(padded.secret, std::string(256, 'k'));
        ASSERT_TRUE(server::usable_secret_text(std::string(32, '!')));
        ASSERT_TRUE(server::usable_secret_text(std::string(256, '~')));
        ASSERT_FALSE(server::usable_secret_text(std::string(32, ' ')));
        ASSERT_FALSE(server::usable_secret_text(std::string(31, 'a')));
        ASSERT_FALSE(server::usable_secret_text(std::string(257, 'a')));
    } TEST_END();

    TEST_CASE("S3.18 The Generated Secret Has All Its Strength, And The Rule For A Secret In A File Is The Control Interface's Own: 512 Secrets Show Every Digit At Every Position, No Position Copies Another, No Half Is Zero; All 256 Byte Values At The Start, In The Middle And At The End Agree With The HTTP Server") {
        std::vector<std::string> many;
        for (int i = 0; i < 512; ++i) many.push_back(server::generate_secret_text());
        for (const std::string& t : many) ASSERT_TRUE(is_hex64(t));
        std::vector<std::string> sorted = many;
        std::sort(sorted.begin(), sorted.end());
        ASSERT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());       // never the same twice
        // every one of the 64 positions shows at least 10 of the 16 digits (a position that is fixed or mostly zero means missing random bytes)
        for (size_t p = 0; p < 64; ++p) {
            std::set<char> seen;
            for (const std::string& t : many) seen.insert(t[p]);
            ASSERT_MSG(seen.size() >= 10, "position " + std::to_string(p) + " shows only " + std::to_string(seen.size()) + " digits");
        }
        // no position is a copy of another one (a byte that is written twice, or random bytes that are used again further on)
        for (size_t a = 0; a < 64; ++a) {
            for (size_t b = a + 1; b < 64; ++b) {
                bool differ = false;
                for (const std::string& t : many) {
                    if (t[a] != t[b]) {
                        differ = true;
                        break;
                    }
                }
                ASSERT_MSG(differ, "positions " + std::to_string(a) + " and " + std::to_string(b) + " are always equal");
            }
        }
        // no secret has a half of zeros
        const std::string zeros(32, '0');
        for (const std::string& t : many) ASSERT_MSG(t.substr(0, 32) != zeros && t.substr(32) != zeros, t);

        // The rule for a file: what the reader gives for every byte value in front of, inside and behind a secret, written out again here
        const fs::path dir = fresh_dir("bytes");
        const std::string core(40, 'k');
        for (int b = 0; b < 256; ++b) {
            const std::string c(1, static_cast<char>(b));
            const std::string shapes[3] = {c + core, core.substr(0, 20) + c + core.substr(20), core + c};
            for (const std::string& text : shapes) {
                const std::string tag = "byte " + std::to_string(b) + " in " + std::to_string(text.size()) + " characters";
                // the file reader's own rule and the HTTP server's agree: a usable text is always accepted by the control interface, and a refused one is not
                ASSERT_MSG(server::usable_secret_text(text) == http_takes(text), tag + ": usable_secret_text and the HTTP server disagree");
                // through a file: trailing line ends and blanks do not belong to the secret; the rest must be usable
                std::string trimmed = text;
                while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
                put(dir / "control-secret", text);
                auto r = server::resolve_secret(nullptr, (dir / "control-secret").string());
                ASSERT_MSG(r.ok == server::usable_secret_text(trimmed), tag + ": the file reader decided otherwise than the rule");
                if (r.ok) {
                    ASSERT_MSG(r.secret == trimmed, tag);
                    ASSERT_MSG(http_takes(r.secret), tag + ": the control interface refuses a secret that the file reader gave");
                } else {
                    ASSERT_MSG(slurp(dir / "control-secret") == text, tag + ": a refused file was changed");
                }
            }
        }
    } TEST_END();

    TEST_CASE("S3.58 random_bytes, The Generator Of The Control Secret, Is There For The Server To Make The Keys Of The Seats With (Protocol 10): Every Length Is Filled Exactly And Nothing Beyond It Is Touched; 512 Keys Of 16 Bytes Show Many Values At Every Position, None Repeats, None Is Zero, No Position Copies Another; Zero Bytes Is Nothing To Do") {
        for (const size_t n : {size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{16}, size_t{31}, size_t{32}, size_t{33}, size_t{255}, size_t{4096}, size_t{100000}}) {
            std::vector<uint8_t> buf(n + 16, 0xA5);
            ASSERT_TRUE(server::random_bytes(buf.data() + 8, n));
            for (size_t i = 0; i < 8; ++i) ASSERT_TRUE(buf[i] == 0xA5 && buf[n + 8 + i] == 0xA5);        // the bytes before and after the buffer are as they were
            if (n >= 32) {                                                                                 // a run of 32 bytes or more is not constant, and not left as it was
                std::set<uint8_t> values(buf.begin() + 8, buf.begin() + 8 + static_cast<std::ptrdiff_t>(n));
                ASSERT_TRUE(values.size() >= 16);
            }
        }
        ASSERT_TRUE(server::random_bytes(nullptr, 0));                                                    // nothing to do is done
        ASSERT_FALSE(server::random_bytes(nullptr, 1));                                                   // nowhere to write it is not
        // the keys of seats as the server makes them: 512 of 16 bytes
        std::vector<net::SeatKey> keys(512);
        for (net::SeatKey& k : keys) {
            ASSERT_TRUE(server::random_bytes(k.data(), k.size()));
            ASSERT_FALSE(net::key_is_zero(k));
        }
        std::vector<net::SeatKey> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        ASSERT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());                     // never the same key twice
        for (size_t p = 0; p < net::kKeyBytes; ++p) {                                                      // every position shows many of the 256 values (a position that is fixed means missing random bytes)
            std::set<uint8_t> seen;
            for (const net::SeatKey& k : keys) seen.insert(k[p]);
            ASSERT_MSG(seen.size() >= 150, "position " + std::to_string(p) + " shows only " + std::to_string(seen.size()) + " values");
        }
        for (size_t a = 0; a < net::kKeyBytes; ++a) {                                                      // no position is a copy of another
            for (size_t b = a + 1; b < net::kKeyBytes; ++b) {
                bool differ = false;
                for (const net::SeatKey& k : keys) differ = differ || k[a] != k[b];
                ASSERT_MSG(differ, "positions " + std::to_string(a) + " and " + std::to_string(b) + " are always equal");
            }
        }
    } TEST_END();

    TEST_CASE("S3.19 A File Bigger Than A Secret Is Refused Without Being Read: A Sparse File Of 256 MiB Is Called Too Big At Once (The Whole File Used To Be Read Into Memory First)") {
#ifndef _WIN32
        const fs::path dir = fresh_dir("big");
        const fs::path big = dir / "control-secret";
        put(big, "");
        std::error_code ec;
        fs::resize_file(big, std::uintmax_t{256} << 20, ec);                        // sparse: it costs no disk, and reading it whole costs a second and half a gigabyte
        ASSERT_FALSE(ec);
        const size_t peak_before = peak_rss_bytes();
        double best_ms = 1e9;
        for (int i = 0; i < 3; ++i) {                                               // the best of three: a stall of the machine does not fail the test
            const auto t0 = std::chrono::steady_clock::now();
            auto r = server::resolve_secret(nullptr, big.string());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ASSERT_FALSE(r.ok);
            ASSERT_TRUE(r.error.find("is too big to be a secret") != std::string::npos);
            best_ms = std::min(best_ms, ms);
        }
        ASSERT_MSG(best_ms < 250.0, "the refusal took " + std::to_string(best_ms) + " ms: the file was read");
        // and the memory never held the file (a read of all of it raises the highest resident size of this process by at least its size)
        const size_t peak_after = peak_rss_bytes();
        ASSERT_MSG(peak_after - peak_before < (std::size_t{32} << 20), "the refusal raised the peak memory by " + std::to_string((peak_after - peak_before) >> 20) + " MiB");
        ASSERT_EQ(fs::file_size(big), std::uintmax_t{256} << 20);                    // and left alone
        fs::remove(big, ec);
#endif
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("S3.20 Symbolic Links: A Link To A Good File (A Mounted Secret) Is Followed, A Link To A Bad File Or A Directory Is Refused, A Link To Nothing Is Called That (And Nothing Is Made Behind It), A Loop Is Called A Loop") {
        const fs::path dir = fresh_dir("links");
        const std::string secret = server::generate_secret_text();
        put(dir / "real", secret + "\n");
        fs::create_symlink("real", dir / "link");                                   // relative, as the mounts of a secrets store make them
        auto ok = server::resolve_secret(nullptr, (dir / "link").string());
        ASSERT_TRUE(ok.ok);
        ASSERT_TRUE(ok.source == server::SecretSource::File);
        ASSERT_EQ(ok.secret, secret);
        ASSERT_EQ(ok.path, (dir / "link").string());
        ASSERT_TRUE(fs::is_symlink(dir / "link"));                                  // still a link
        // a chain of links through a folder that is a link too (a Kubernetes secret volume: key -> data/key, data -> a folder with a timestamp)
        const std::string secret2 = "A-mounted-secret-0123456789-abcdefghijklmnop";
        fs::create_directories(dir / "mount" / "ts-1");
        put(dir / "mount" / "ts-1" / "key", secret2);
        fs::create_directory_symlink("ts-1", dir / "mount" / "data");
        fs::create_symlink("data/key", dir / "mount" / "key");
        auto chain = server::resolve_secret(nullptr, (dir / "mount" / "key").string());
        ASSERT_TRUE(chain.ok);
        ASSERT_EQ(chain.secret, secret2);
        // a link to a file that is no secret: refused, the file is left as it was
        put(dir / "badreal", "short");
        fs::create_symlink("badreal", dir / "badlink");
        auto bad = server::resolve_secret(nullptr, (dir / "badlink").string());
        ASSERT_FALSE(bad.ok);
        ASSERT_TRUE(bad.error.find("nothing was changed") != std::string::npos);
        ASSERT_EQ(slurp(dir / "badreal"), std::string("short"));
        // a link to a directory
        fs::create_directories(dir / "somedir");
        fs::create_directory_symlink("somedir", dir / "dirlink");
        auto dl = server::resolve_secret(nullptr, (dir / "dirlink").string());
        ASSERT_FALSE(dl.ok);
        ASSERT_TRUE(dl.error.find("not a regular file") != std::string::npos);
        // a link to nothing, in the same folder and into a folder that exists: the truth, and nothing made (not the target, not a file instead of the link)
        fs::create_symlink("nowhere", dir / "dangling");
        fs::create_symlink("somedir/missing", dir / "dangling2");
        for (const char* name : {"dangling", "dangling2"}) {
            auto r = server::resolve_secret(nullptr, (dir / name).string());
            ASSERT_MSG(!r.ok, name);
            ASSERT_MSG(r.error.find("is a symbolic link to nothing") != std::string::npos, std::string(name) + ": " + r.error);
            ASSERT_MSG(r.error.find("keeps changing") == std::string::npos, name);
            ASSERT_MSG(fs::is_symlink(dir / name), name);
            ASSERT_MSG(!fs::exists(dir / "nowhere") && !fs::exists(dir / "somedir" / "missing"), name);
        }
        ASSERT_EQ(fs::read_symlink(dir / "dangling").string(), std::string("nowhere"));
        // the environment is all it takes to start, whatever is at the path
        ASSERT_TRUE(server::resolve_secret("a-good-secret-from-the-environment-0123", (dir / "dangling").string()).ok);
        // a loop
        fs::create_symlink("loop-b", dir / "loop-a");
        fs::create_symlink("loop-a", dir / "loop-b");
        auto lp = server::resolve_secret(nullptr, (dir / "loop-a").string());
        ASSERT_FALSE(lp.ok);
        ASSERT_TRUE(lp.error.find("cannot be looked at") != std::string::npos);
    } TEST_END();
#endif

    TEST_CASE("S3.21 Starts At The Same Moment: 8 Threads On One Fresh Folder, 200 Rounds (Half With A Folder That Does Not Exist Yet): Every Start Works, All Get The Same Secret, Exactly One Made It, The File Holds It Complete, Nothing Else Is Left In The Folder") {
        constexpr int kThreads = 8;
        constexpr int kRounds = 200;
        const fs::path base = fresh_dir("race");
        for (int round = 0; round < kRounds; ++round) {
            const fs::path folder = base / ("round-" + std::to_string(round));
            if (round % 2 == 0) fs::create_directories(folder);
            const std::string file = (folder / "control-secret").string();
            std::atomic<int> ready{0};
            std::atomic<bool> go{false};
            std::vector<server::SecretResult> results(kThreads);
            std::vector<std::thread> threads;
            for (int i = 0; i < kThreads; ++i) {
                threads.emplace_back([&, i] {
                    ready.fetch_add(1);
                    while (!go.load()) std::this_thread::yield();                  // released together
                    results[static_cast<size_t>(i)] = server::resolve_secret(nullptr, file);
                });
            }
            while (ready.load() < kThreads) std::this_thread::yield();
            go.store(true);
            for (std::thread& t : threads) t.join();
            int generated = 0;
            for (const server::SecretResult& r : results) {
                ASSERT_MSG(r.ok, "round " + std::to_string(round) + ": a start failed: " + r.error);
                ASSERT_MSG(r.secret == results[0].secret, "round " + std::to_string(round) + ": the starts did not get the same secret");
                if (r.source == server::SecretSource::Generated) ++generated;
            }
            ASSERT_MSG(generated == 1, "round " + std::to_string(round) + ": " + std::to_string(generated) + " starts made the secret");
            ASSERT_MSG(slurp(file) == results[0].secret + "\n", "round " + std::to_string(round) + ": the file does not hold the secret that the starts use");
            ASSERT_MSG(entries_in(folder) == 1, "round " + std::to_string(round) + ": something besides the secret file is left in the folder");
        }
    } TEST_END();

    TEST_CASE("S3.22 A Start That Dies While It Makes The File Leaves No Half Secret: Stale Temporary Files (Of Any Name A Simple Scheme Would Use) Never Block The Next Start, And A Process That Is Killed At Its First Write Leaves Nothing That Blocks It") {
        const fs::path dir = fresh_dir("crash");
        const std::string file = (dir / "control-secret").string();
        // what a crashed start can leave: empty or half written temporary files, with the names that a fixed or process-number scheme would pick
        const std::vector<std::string> stale = {"control-secret.tmp", "control-secret.tmp.1", "control-secret.1.tmp", ".control-secret.tmp", "control-secret.0123456789abcdef.tmp", "control-secret~"};
        for (const std::string& name : stale) put(dir / name, name == "control-secret.tmp.1" ? "0123456789abcdef0123" : "");
        auto r = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(r.ok);
        ASSERT_TRUE(r.source == server::SecretSource::Generated);
        ASSERT_EQ(slurp(file), r.secret + "\n");
        for (const std::string& name : stale) ASSERT_MSG(fs::exists(dir / name), name);          // the leftovers of others are not the server's to remove
        ASSERT_EQ(entries_in(dir), stale.size() + 1);
        auto next = server::resolve_secret(nullptr, file);
        ASSERT_TRUE(next.ok);
        ASSERT_TRUE(next.source == server::SecretSource::File);
        ASSERT_EQ(next.secret, r.secret);
#ifndef _WIN32
        // a real death: a child whose files may not grow is killed (SIGXFSZ) by its first write
        const fs::path dir2 = fresh_dir("crash2");
        const std::string file2 = (dir2 / "control-secret").string();
        const pid_t pid = ::fork();
        ASSERT_TRUE(pid >= 0);
        if (pid == 0) {
            struct rlimit none = {0, 0};
            ::setrlimit(RLIMIT_CORE, &none);                                         // no core file for this death
            ::setrlimit(RLIMIT_FSIZE, &none);
            ::signal(SIGXFSZ, SIG_DFL);
            (void)server::resolve_secret(nullptr, file2);
            ::_exit(0);                                                              // not reached: the write kills the process
        }
        int wait_status = 0;
        ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
        ASSERT_TRUE(WIFSIGNALED(wait_status));
        ASSERT_TRUE(WTERMSIG(wait_status) == SIGXFSZ);
        ASSERT_FALSE(fs::exists(file2));                                             // nothing under the name that the next start would find and refuse
        auto after = server::resolve_secret(nullptr, file2);
        ASSERT_TRUE(after.ok);
        ASSERT_TRUE(after.source == server::SecretSource::Generated);
        ASSERT_EQ(slurp(file2), after.secret + "\n");
        // what the dead start left is a temporary file and nothing else
        for (const auto& e : fs::directory_iterator(dir2)) {
            const std::string name = e.path().filename().string();
            ASSERT_MSG(name == "control-secret" || name.size() > 4, name);
            ASSERT_MSG(name == "control-secret" || name.compare(name.size() - 4, 4, ".tmp") == 0, name);
        }
#endif
    } TEST_END();
}

// Reconnect, release A: the server (protocol 10). A room that holds the seat of a player whose connection is lost, through the real manager, door and lobby. S3.36 - S3.59 (S3.34 and S3.35 are the end of a match and a laggard, v0.0.94: the ids S3.58 and S3.59 are this release's random_bytes and keys).
void run_reconnect_tests() {
    TEST_CASE("S3.59 Keys: A Room That Holds Seats Gives Every Player A Different Key (None Is Zero) In Its Welcome, And The Session Knows It; A Room That Does Not Gives None, And Its Players Have No Way Back") {
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("K-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "K-1");
            RClient& b = w.connect("Bob", "K-1");
            RClient& c = w.connect("Cat", "K-1");
            w.run(3000 + kPre);
            const RoomStatus s = w.status("K-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && !s.paused && s.absent.empty() && s.rejoins == 0);
            ASSERT_TRUE(!net::key_is_zero(a.lobby->key()) && !net::key_is_zero(b.lobby->key()) && !net::key_is_zero(c.lobby->key()));
            ASSERT_TRUE(a.lobby->key() != b.lobby->key() && b.lobby->key() != c.lobby->key() && a.lobby->key() != c.lobby->key());
            ASSERT_TRUE(a.session->key() == a.lobby->key() && b.session->key() == b.lobby->key());
            ASSERT_FALSE(a.lobby->rejoined());                                           // a first Welcome is no rejoin
            ASSERT_TRUE(s.log_usable && s.log_turns > 0 && s.log_bytes > 0);              // the log of the match is being kept
        }
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("K-2", 2), w.now).ok);                // (the default of a room is not to hold seats in this release)
            RClient& a = w.connect("Ann", "K-2");
            RClient& b = w.connect("Bob", "K-2");
            w.run(3000);
            const RoomStatus s = w.status("K-2");
            ASSERT_TRUE(s.state == RoomState::Running && !s.reconnect);
            ASSERT_TRUE(net::key_is_zero(a.lobby->key()) && net::key_is_zero(b.lobby->key()));
            ASSERT_FALSE(a.session->reconnecting());
            ASSERT_TRUE(s.log_turns == 0 && s.log_bytes == 0);                           // nothing is logged for a room that cannot use a log
            // a Hello with a made-up key for it is what any Hello for a running match is
            net::Connection* c = w.open_link();
            net::HelloMsg h;
            h.room = "K-2";
            for (uint8_t& v : h.key) v = 9;
            c->send(net::encode(h));
            w.run(300);
            ASSERT_EQ(reject_on(c), static_cast<int>(net::RejectReason::MatchRunning));
        }
    } TEST_END();

    TEST_CASE("S3.36 A Lost Link Pauses The Room (The Status Says Who Is Missing And Since When, Nothing Is Sealed For 10 s), The Player Comes Back Through The Door With Its Key And The Match Goes On To Its End: Three Machines And The Referee End Identical, The Counters Say What Happened") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("R-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "R-1");
        RClient& b = w.connect("Bob", "R-1");
        RClient& c = w.connect("Cat", "R-1");
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("R-1").state == RoomState::Running);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        w.run(1000);
        RoomStatus s = w.status("R-1");
        ASSERT_TRUE(s.paused && s.absent.size() == 1);
        ASSERT_TRUE(s.absent[0].seat == bob && s.absent[0].name == "Bob" && !s.absent[0].catching_up && s.absent[0].away_s <= 2 && s.absent[0].progress == 0);
        ASSERT_TRUE(s.vote_seat == 255);
        const uint32_t ticks_at_pause = s.ticks;
        w.run(10000);
        s = w.status("R-1");
        ASSERT_TRUE(s.ticks <= ticks_at_pause + 2);                                      // nothing runs while Bob is away
        ASSERT_TRUE(s.absent[0].away_s >= 10 && s.absent[0].away_s <= 12 && s.paused_s >= 10);
        ASSERT_TRUE(a.session->paused() && c.session->paused());
        b.reconnects = true;                                                             // its session finds a new link: the door takes it to the running room
        ASSERT_TRUE(w.until([&]() { return !w.status("R-1").paused; }, 8000));
        w.run(5000);
        s = w.status("R-1");
        ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.absent.empty());
        ASSERT_TRUE(s.rejoins == 1 && s.drops_by_vote == 0 && s.drops_by_cap == 0);
        ASSERT_TRUE(s.paused_s >= 11 && s.paused_s <= 13);
        ASSERT_TRUE(s.ticks > ticks_at_pause + 60);
        w.play_to_the_end("R-1");
        ASSERT_TRUE(w.status("R-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && c.sim.is_match_over());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        const RoomStatus end = w.status("R-1");
        ASSERT_TRUE(end.rejoins == 1 && end.log_usable && end.log_turns > 1000 && end.paused_s >= 11 && !end.paused && end.absent.empty());     // the log was freed at the end and says what it held
    } TEST_END();

    TEST_CASE("S3.37 A Machine That Starts From Nothing (A Reloaded Page: Its Lobby Says Hello With The Key) Is Given The Match, More Than 300 Turns Of It, Through The Door: The Seat Is Back With Its Name, The Hash Agreed, The Match Ends Identical On Three Machines") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("L-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "L-1");
        RClient& b = w.connect("Bob", "L-1");
        RClient& c = w.connect("Cat", "L-1");
        w.run(40000 + kPre);                                                                    // more than the 600 turns that every machine keeps
        ASSERT_TRUE(w.status("L-1").state == RoomState::Running && w.status("L-1").turns > 700);
        const net::SeatKey key = b.lobby->key();
        const uint8_t seat = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);                                                                        // the page of Bob is reloaded: its game is gone
        w.run(2000);
        ASSERT_TRUE(w.status("L-1").paused);
        RClient& b2 = w.connect("Bob", "L-1", 255, key);                                 // a new machine with nothing but the key
        bool saw_catching_up = false;
        bool saw_catching_up_in_json = false;                                            // the control interface says so too: state "catching_up" and a progress of 0 - 100
        ASSERT_TRUE(w.until([&]() {
            const RoomStatus st = w.status("L-1");
            for (const RoomStatus::Absent& e : st.absent) saw_catching_up = saw_catching_up || (e.catching_up && e.seat == seat);
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/rooms/L-1";
            ctl::JsonValue j;
            std::string why;
            ctl::parse_json(handle_control(w.mgr, rq, w.now).body, j, &why);
            if (j.get("absent").size() == 1) {
                const ctl::JsonValue& row = j.get("absent").at(0);
                saw_catching_up_in_json = saw_catching_up_in_json || (row.get("state").str() == "catching_up" && row.get("seat").as_int_or(9) == seat && row.get("name").str() == "Bob" &&
                                                                      row.get("progress").as_int_or(-1) >= 0 && row.get("progress").as_int_or(101) <= 100);
            }
            return !st.paused && b2.session != nullptr && b2.session->mode() == net::ClientSession::Mode::Normal;
        }, 20000));
        ASSERT_TRUE(saw_catching_up && saw_catching_up_in_json);
        ASSERT_EQ(b2.lobby->my_seat(), seat);
        ASSERT_TRUE(b2.lobby->key() == key && b2.lobby->rejoined());
        ASSERT_EQ(w.status("L-1").names[seat], std::string("Bob"));
        w.run(4000);
        w.play_to_the_end("L-1");
        ASSERT_TRUE(w.status("L-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b2.sim.state_hash() && b2.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(w.status("L-1").rejoins == 1);
        ASSERT_FALSE(a.session->desynced() || b2.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.38 Two Players: The One That Stays Votes After 30 s (The Status Shows The Vote), The Absent Seat Is Dropped, The Room Finishes With The Survivor The Winner In The Rows") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("V-1", 2), w.now).ok);
        RClient& a = w.connect("Ann", "V-1");
        RClient& b = w.connect("Bob", "V-1");
        w.run(6000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        w.run(25000);
        RoomStatus s = w.status("V-1");
        ASSERT_TRUE(s.paused && s.vote_seat == 255);                                     // 25 s: the vote is not open
        w.run(6500);
        s = w.status("V-1");
        ASSERT_TRUE(s.paused && s.vote_seat == bob && s.voters == 1 && s.votes_continue == 0);
        ASSERT_TRUE(a.session->presence().vote_seat == bob);
        ASSERT_TRUE(a.session->vote(bob, true));                                         // the one player that is left: more than half of one
        w.run(300);
        s = w.status("V-1");
        w.run(3000);
        s = w.status("V-1");
        ASSERT_EQ(s.drops_by_vote, 1u);
        ASSERT_TRUE(s.state == RoomState::Finished);                                     // Ann has won: the drop leaves her alone
        ASSERT_TRUE(a.sim.is_match_over());
        ASSERT_TRUE(!s.rows.empty() && s.rows[0].name == "Ann" && s.rows[0].winner);
        ASSERT_EQ(s.reason, std::string("the match ended"));
        {   // a room that says 8 s: the vote opens after 8 s of absence, not 30
            RWorld v;
            RoomSpec quick = held_spec("V-2", 3);
            quick.vote_after_ms = 8000;
            ASSERT_TRUE(v.mgr.create_room(quick, v.now).ok);
            RClient& x = v.connect("Xan", "V-2");
            RClient& y = v.connect("Yan", "V-2");
            RClient& z = v.connect("Zed", "V-2");
            v.run(4000 + kPre);
            z.reconnects = false;
            v.cut(z);
            v.run(6500);
            ASSERT_TRUE(v.status("V-2").paused && v.status("V-2").vote_seat == 255 && x.session->presence().vote_seat == 255);
            v.run(3000);
            ASSERT_TRUE(v.status("V-2").vote_seat == z.lobby->my_seat() && v.status("V-2").voters == 2);
            ASSERT_TRUE(x.session->presence().vote_seat == z.lobby->my_seat() && y.session->presence().vote_seat == z.lobby->my_seat());
        }
    } TEST_END();

    TEST_CASE("S3.39 The Room's Wall-Clock Limit Counts Play, Not Waiting: A Minute Of Waiting For A Player Inside A Limit Of 30 s Does Not Fail The Room, 30 s Of Play Do") {
        RWorld w;
        RoomSpec spec = held_spec("T-1", 2);
        spec.run_ms = 30000;
        spec.max_pause_ms = 600000;                                                      // (nobody is dropped by the cap in this test)
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        w.connect("Ann", "T-1");
        RClient& b = w.connect("Bob", "T-1");
        w.run(5000 + kPre);
        b.reconnects = false;
        w.cut(b);
        w.run(60000);                                                                    // a minute of waiting: more than the limit
        ASSERT_TRUE(w.status("T-1").state == RoomState::Running);
        b.reconnects = true;
        ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused; }, 8000));
        w.run(10000);
        ASSERT_TRUE(w.status("T-1").state == RoomState::Running);
        w.run(25000);                                                                    // now 30 s of play have passed
        ASSERT_TRUE(w.status("T-1").state == RoomState::Failed && w.status("T-1").reason.find("longer") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.40 Everybody Lost: Each Seat Is Held (The Room Waits For Them As Long As The Cap Allows), Then The Room Ends \"Everybody Left\" With All Seats Dropped By The Cap; A Short Cap In The Spec Is Obeyed To The Second") {
        RWorld w;
        RoomSpec spec = held_spec("E-1", 3);
        spec.max_pause_ms = 60000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        RClient& a = w.connect("Ann", "E-1");
        RClient& b = w.connect("Bob", "E-1");
        RClient& c = w.connect("Cat", "E-1");
        w.run(4000 + kPre);
        for (RClient* p : {&a, &b, &c}) {
            p->reconnects = false;
            w.cut(*p);
        }
        const uint32_t cut_at = w.now;
        w.run(30000);
        RoomStatus s = w.status("E-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.absent.size() == 3);   // nobody is there, but each seat is held
        ASSERT_TRUE(s.absent[0].away_s >= 29 && s.absent[0].away_s <= 31);
        uint32_t ended_after_ms = 0;
        for (int i = 0; i < 4000 && ended_after_ms == 0; ++i) {
            w.run(10);
            if (w.status("E-1").state != RoomState::Running) ended_after_ms = w.now - cut_at;
        }
        s = w.status("E-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "everybody left");
        ASSERT_TRUE(ended_after_ms >= 60000 && ended_after_ms <= 60100);
        ASSERT_EQ(s.drops_by_cap, 3u);
        ASSERT_EQ(s.drops_by_vote, 0u);
        ASSERT_TRUE(s.paused_s >= 59 && s.paused_s <= 61);
    } TEST_END();

    TEST_CASE("S3.41 The Cap On A Match's Pauses Through The Door: With Three Players One Of Them Is Away For The Whole Cap (60 s): The Seat Is Dropped At The Same Tick For The Others, The Match Goes On To Its End, And The Key Of The Dropped Seat Is Told \"Dropped\" When It Comes Back") {
        RWorld w;
        RoomSpec spec = held_spec("C-1", 3);
        spec.max_pause_ms = 60000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        RClient& a = w.connect("Ann", "C-1");
        RClient& b = w.connect("Bob", "C-1");
        RClient& c = w.connect("Cat", "C-1");
        w.run(6000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        const uint32_t cut_at = w.now;
        w.run(55000);
        RoomStatus s = w.status("C-1");
        ASSERT_TRUE(s.paused && s.vote_seat == bob);                                      // (the vote is open since 30 s; nobody votes)
        uint32_t resumed_after_ms = 0;
        for (int i = 0; i < 1000 && resumed_after_ms == 0; ++i) {
            w.run(10);
            if (!w.status("C-1").paused) resumed_after_ms = w.now - cut_at;
        }
        ASSERT_TRUE(resumed_after_ms >= 60000 && resumed_after_ms <= 60100);
        s = w.status("C-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_cap == 1 && s.drops_by_vote == 0 && s.rejoins == 0);
        w.run(3000);
        ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
        b.reconnects = true;                                                              // Bob's machine finds a link: its seat is gone
        w.run(4000);
        ASSERT_TRUE(b.lost && b.was_rejected && b.rejected == net::RejectReason::Dropped);
        w.play_to_the_end("C-1");
        ASSERT_TRUE(w.status("C-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.42 The Prune Window (Run It Under AddressSanitizer): A Returning Player's Link Closes While The Session Is Keeping Its Catch-Up, In The Very Pass In Which The Door Reads Another Seat's Hello And A Room That Keeps 32 Connections Prunes The Closed Ones: The Connection That The Session Still Points At Is Not Freed") {
        RWorld w;
        w.link = {20, 0};                                                                // no jitter: the Hello of the second link arrives exactly when this test needs it
        ASSERT_TRUE(w.mgr.create_room(held_spec("P-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "P-1");
        RClient& b = w.connect("Bob", "P-1");
        RClient& c = w.connect("Cat", "P-1");
        w.run(4000 + kPre);
        RClient* const seats[3] = {&b, &c, &a};
        // (a seat may be taken back three times a minute: the three of them take turns, one cut every 7 s, so that each is cut every 21 s)
        for (int i = 0; i < 80 && w.status("P-1").connections != Room::kMaxConnections - 1; ++i) {        // until the room keeps 31 connections: the next one fills it
            w.cut(*seats[i % 3]);
            ASSERT_TRUE(w.until([&]() { return !w.status("P-1").paused && w.status("P-1").rejoins == static_cast<uint32_t>(i + 1); }, 6000));
            w.run(7000);
        }
        ASSERT_EQ(w.status("P-1").connections, static_cast<uint32_t>(Room::kMaxConnections - 1));
        b.reconnects = false;
        c.reconnects = false;
        const net::SeatKey key_b = b.session->key();
        const net::SeatKey key_c = c.session->key();
        const uint32_t have_b = b.session->runner().next_turn_expected();
        const uint32_t have_c = c.session->runner().next_turn_expected();
        w.run(15000);                                                                    // (the last returns of the loop were 21 s apart: the oldest of the three leaves the minute)
        w.cut(b);
        w.cut(c);
        w.run(200);
        ASSERT_TRUE(w.status("P-1").paused);
        const auto make_link = [&](const net::SeatKey& key, uint32_t have) {
            net::Connection* end = w.open_link();
            net::HelloMsg h;
            h.name = "x";
            h.room = "P-1";
            h.key = key;
            h.have_turns = have;
            end->send(net::encode(h));
            return end;
        };
        net::Connection* link_b = make_link(key_b, have_b);                              // Bob's Hello reaches the door 20 ms later
        w.run(30);                                                                       // accepted: the room keeps the connection, the session has a returning player for it
        ASSERT_EQ(w.status("P-1").connections, static_cast<uint32_t>(Room::kMaxConnections));
        make_link(key_c, have_c);                                                        // Cat's Hello arrives 20 ms from now ...
        w.run(10);
        w.net.cut(link_b);                                                               // ... and 10 ms from now Bob's link is closed (the session has not looked at it yet)
        w.run(10);                                                                       // the pass in which the door reads Cat's Hello: the room prunes, the session pumps its returning players
        w.run(2000);
        ASSERT_TRUE(w.status("P-1").state == RoomState::Running);
        ASSERT_TRUE(w.status("P-1").connections < static_cast<uint32_t>(Room::kMaxConnections));    // the closed ones were pruned (the table is not full any more)
    } TEST_END();

    TEST_CASE("S3.43 Connections That Are Cut 40 Times (Three Players Take Turns, Each Is Cut Every 21 s: A Seat May Be Taken Back Three Times A Minute): Every Return Is Verified, The Closed Connections Are Pruned And The Live Ones Never Are, The Match Ends Identical (Each Pause Counts 5 s Toward The Cap At Least, The Cap Is Far Away)") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("F-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "F-1");
        RClient& b = w.connect("Bob", "F-1");
        RClient& c = w.connect("Cat", "F-1");
        w.run(4000 + kPre);
        RClient* const seats[3] = {&b, &c, &a};
        for (int i = 0; i < 40; ++i) {
            w.cut(*seats[i % 3]);
            ASSERT_TRUE(w.until([&]() { return !w.status("F-1").paused && w.status("F-1").rejoins == static_cast<uint32_t>(i + 1); }, 6000));
            w.run(7000);                                                                 // (the next seat's turn: this one is cut again 21 s from now, which is within three a minute)
        }
        const RoomStatus s = w.status("F-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 40 && s.drops_by_vote == 0 && s.drops_by_cap == 0 && s.rejoins_refused == 0);
        ASSERT_TRUE(s.connections <= Room::kMaxConnections + 1u);                        // (the table did not grow with every return)
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        ASSERT_TRUE(s.paused_s >= 40 * 5 && s.paused_s < 40 * 5 + 40);                   // (each pause counts at least 5 s toward the cap: 40 of them are 200 s, and they were a second each)
        w.play_to_the_end("F-1");
        ASSERT_TRUE(w.status("F-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.44 The Door And A Hello With A Key: A Wrong Key, No Key And A Made-Up Key Are Told \"The Match Has Started\" By A Running Room (Nobody Is Disturbed, Nothing Is Revealed), So Is A Key For A Room That Does Not Hold Seats Or Is Loading; A Room That Is Over Says \"No Such Room\"; A Refusal Is Counted") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("X-1", 2), w.now).ok);
        RClient& a = w.connect("Ann", "X-1");
        RClient& b = w.connect("Bob", "X-1");
        w.run(4000 + kPre);
        ASSERT_TRUE(w.status("X-1").state == RoomState::Running);
        const auto hello_for = [&](const std::string& room, const net::SeatKey& key, uint32_t have) {
            net::Connection* end = w.open_link();
            net::HelloMsg h;
            h.name = "Mallory";
            h.room = room;
            h.key = key;
            h.have_turns = have;
            end->send(net::encode(h));
            return end;
        };
        net::SeatKey bad;
        for (uint8_t& v : bad) v = 7;
        const uint64_t refused_before = w.mgr.connections_refused();
        net::Connection* wrong = hello_for("X-1", bad, 0);
        net::Connection* none = hello_for("X-1", net::SeatKey{}, 0);
        net::SeatKey almost = a.lobby->key();
        almost[15] = static_cast<uint8_t>(almost[15] ^ 0x80);                             // the key of Ann with one bit changed
        net::Connection* near_miss = hello_for("X-1", almost, 0);
        w.run(400);
        ASSERT_EQ(reject_on(wrong), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(reject_on(none), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(reject_on(near_miss), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(w.mgr.connections_refused() - refused_before, uint64_t{3});
        RoomStatus s = w.status("X-1");
        ASSERT_TRUE(!s.paused && s.absent.empty() && s.rejoins == 0);                    // nobody was disturbed
        ASSERT_TRUE(a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal);
        // a key that is Ann's, with a count of turns that was never sealed: refused (the session says BadRequest: the key was right, the claim is not) and nobody is held for it
        net::Connection* liar = hello_for("X-1", a.lobby->key(), 999999999);
        w.run(400);
        ASSERT_EQ(reject_on(liar), static_cast<int>(net::RejectReason::BadRequest));
        ASSERT_TRUE(!w.status("X-1").paused && a.session->mode() == net::ClientSession::Mode::Normal);
        // a room that does not hold seats: a key is a key to nothing
        ASSERT_TRUE(w.mgr.create_room(spec_of("X-2", 2), w.now).ok);
        w.connect("Cat", "X-2");
        w.connect("Dan", "X-2");
        w.run(3000);
        ASSERT_TRUE(w.status("X-2").state == RoomState::Running);
        net::Connection* off = hello_for("X-2", a.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(off), static_cast<int>(net::RejectReason::MatchRunning));
        // a room that is loading (one player never says that it has loaded the map): MatchRunning too
        ASSERT_TRUE(w.mgr.create_room(held_spec("X-3", 2), w.now).ok);
        RClient& e = w.connect("Eve", "X-3");
        net::Connection* raw = w.open_link();                                            // a raw player that says Hello and then nothing
        net::HelloMsg h;
        h.name = "Slow";
        h.room = "X-3";
        raw->send(net::encode(h));
        w.run(2500);
        ASSERT_TRUE(w.status("X-3").state == RoomState::Loading);
        net::Connection* during = hello_for("X-3", e.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(during), static_cast<int>(net::RejectReason::MatchRunning));
        // a room that is over: NoSuchRoom, whatever is in the Hello
        w.play_to_the_end("X-1");
        ASSERT_TRUE(w.status("X-1").state == RoomState::Finished);
        net::Connection* late = hello_for("X-1", a.lobby->key(), 0);
        net::Connection* late2 = hello_for("X-1", bad, 0);
        w.run(300);
        ASSERT_EQ(reject_on(late), static_cast<int>(net::RejectReason::NoSuchRoom));
        ASSERT_EQ(reject_on(late2), static_cast<int>(net::RejectReason::NoSuchRoom));
        ASSERT_TRUE(w.mgr.close_room("X-3", w.now));
        w.run(100);
        ASSERT_TRUE(w.status("X-3").state == RoomState::Failed);
        net::Connection* failed = hello_for("X-3", e.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(failed), static_cast<int>(net::RejectReason::NoSuchRoom));
    } TEST_END();

    TEST_CASE("S3.45 A Page That Is Reloaded In The Waiting Room Takes Its Seat Back Through The Door (Same Seat, Same Key, The Room Still Counts One Player, The Leader Stays The Leader), The Old Connection Is Told It Was Superseded; The Room Then Fills And Plays") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("W-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "W-1");
        w.run(1000);
        ASSERT_TRUE(w.status("W-1").joined == 1 && w.status("W-1").leader == a.lobby->my_seat());
        const net::SeatKey key = a.lobby->key();
        const uint8_t seat = a.lobby->my_seat();
        RClient& a2 = w.connect("Ann", "W-1", 255, key);                                  // a reload: the new page says Hello with the key before the room has noticed that the old link is dead
        w.run(1000);
        RoomStatus s = w.status("W-1");
        ASSERT_TRUE(s.joined == 1 && s.state == RoomState::Waiting && s.leader == seat);
        ASSERT_EQ(a2.lobby->my_seat(), seat);
        ASSERT_TRUE(a2.lobby->key() == key);
        ASSERT_FALSE(a2.lobby->rejoined());                                               // (still the waiting room: no catch-up)
        ASSERT_TRUE(a.lobby->phase() == net::ClientLobby::Phase::Rejected && a.lobby->reject_reason() == net::RejectReason::Superseded);
        w.connect("Bob", "W-1");
        w.connect("Cat", "W-1");
        w.run(3000);
        ASSERT_TRUE(w.status("W-1").state == RoomState::Running);
        ASSERT_TRUE(a2.session != nullptr && a2.session->key() == key);
    } TEST_END();

    TEST_CASE("S3.46 The Control Interface's Status Says What The Reconnect Does: Paused, Who Is Missing And Since When, The Vote, The Total Pause, The Counters, The Log, The Rules; A Seat's Key Is In None Of It: Not In The Status Of A Running Or Finished Room, The List, A Result File's Text, The Log Output (Nothing Is Written To Stderr)") {
        std::ostringstream captured;                                                     // whatever the server code writes to stderr in this test
        std::streambuf* old_cerr = std::cerr.rdbuf(captured.rdbuf());
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("J-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "J-1");
            RClient& b = w.connect("Bob", "J-1");
            RClient& c = w.connect("Cat", "J-1");
            w.run(5000 + kPre);
            const std::vector<net::SeatKey> keys = {a.lobby->key(), b.lobby->key(), c.lobby->key()};
            const auto status_json = [&](const std::string& code) {
                ctl::HttpRequest rq;
                rq.method = "GET";
                rq.path = "/rooms/" + code;
                return handle_control(w.mgr, rq, w.now);
            };
            const auto parse = [](const ctl::HttpResponse& r) {
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(r.body, v, &why);
                return v;
            };
            ctl::HttpResponse r = status_json("J-1");
            ASSERT_EQ(r.status, 200);
            ctl::JsonValue v = parse(r);
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && !v.get("paused").as_bool_or(true) && v.get("absent").size() == 0 && v.get("vote").is_null());
            ASSERT_TRUE(v.get("hold_vote_seconds").as_int_or(0) == 30 && v.get("max_pause_seconds").as_int_or(0) == 1800);
            ASSERT_TRUE(v.get("rejoins").as_int_or(9) == 0 && v.get("drops_by_vote").as_int_or(9) == 0 && v.get("drops_by_cap").as_int_or(9) == 0 && v.get("paused_seconds").as_int_or(9) == 0);
            ASSERT_TRUE(v.get("log").get("usable").as_bool_or(false) && v.get("log").get("turns").as_int_or(0) > 50 && v.get("log").get("bytes").as_int_or(0) > 100);
            b.reconnects = false;
            w.cut(b);
            w.run(31500);
            a.session->vote(b.lobby->my_seat(), true);                                   // one of two connected players: not enough
            w.run(2500);
            r = status_json("J-1");
            v = parse(r);
            ASSERT_TRUE(v.get("paused").as_bool_or(false) && v.get("absent").size() == 1);
            const ctl::JsonValue& row = v.get("absent").at(0);
            ASSERT_TRUE(row.get("seat").as_int_or(9) == b.lobby->my_seat() && row.get("name").str() == "Bob" && row.get("state").str() == "absent");
            ASSERT_TRUE(row.get("away_seconds").as_int_or(0) >= 33 && row.get("away_seconds").as_int_or(0) <= 35 && row.get("progress").as_int_or(9) == 0);
            ASSERT_TRUE(v.get("vote").get("seat").as_int_or(9) == b.lobby->my_seat() && v.get("vote").get("continue").as_int_or(9) == 1 && v.get("vote").get("voters").as_int_or(9) == 2);
            ASSERT_TRUE(v.get("paused_seconds").as_int_or(0) >= 33 && v.get("paused_seconds").as_int_or(0) <= 35);
            ASSERT_TRUE(v.get("rejoins").as_int_or(9) == 0);
            std::string everything = r.body;
            ctl::HttpRequest list;
            list.method = "GET";
            list.path = "/rooms";
            everything += handle_control(w.mgr, list, w.now).body;
            ctl::HttpRequest stats;
            stats.method = "GET";
            stats.path = "/stats";
            const ctl::HttpResponse stat = handle_control(w.mgr, stats, w.now);
            ASSERT_TRUE(parse(stat).get("log_bytes").as_int_or(0) > 100 && parse(stat).get("log_budget_bytes").as_int_or(0) == 256 * 1024 * 1024);
            everything += stat.body;
            // Bob comes back: the counters
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("J-1").paused; }, 8000));
            w.run(1000);
            v = parse(status_json("J-1"));
            ASSERT_TRUE(!v.get("paused").as_bool_or(true) && v.get("absent").size() == 0 && v.get("vote").is_null() && v.get("rejoins").as_int_or(0) == 1);
            ASSERT_TRUE(v.get("paused_seconds").as_int_or(0) >= 33);
            everything += status_json("J-1").body;
            w.play_to_the_end("J-1");
            // the room that ended: what main() writes to <code>.json is exactly this text
            std::string result_file;
            for (const RoomStatus& ended : w.mgr.take_ended(w.now)) result_file += ctl::to_json(status_to_json(ended)) + "\n";
            ASSERT_FALSE(result_file.empty());
            v = parse(status_json("J-1"));
            ASSERT_TRUE(v.get("state").str() == "finished" && v.get("rejoins").as_int_or(0) == 1 && v.get("log").get("turns").as_int_or(0) > 1000 && v.get("log").get("usable").as_bool_or(false));
            ASSERT_TRUE(v.get("drops_by_vote").as_int_or(9) == 0 && !v.get("paused").as_bool_or(true) && v.get("absent").size() == 0);
            everything += status_json("J-1").body + result_file;
            ASSERT_TRUE(result_file.find("\"rejoins\":1") != std::string::npos && result_file.find("\"log\":{") != std::string::npos);
            for (const net::SeatKey& key : keys) {
                const std::string hex = hex_of(key);
                ASSERT_TRUE(everything.find(hex) == std::string::npos);                   // no key anywhere in what the control interface says
                ASSERT_TRUE(captured.str().find(hex) == std::string::npos);               // nor in what the server code logged
            }
            ASSERT_TRUE(everything.find("key") == std::string::npos);                     // (the word does not occur either: nothing is there to leak)
        }
        std::cerr.rdbuf(old_cerr);
        ASSERT_TRUE(captured.str().empty());                                              // the server code writes nothing to stderr
    } TEST_END();

    TEST_CASE("S3.47 The Reconnect Settings Of A Room And Of The Server: The Control Interface Takes \"reconnect\", \"hold_vote_seconds\" (5 - 3600) And \"max_pause_seconds\" (60 - 86400) And Refuses What Is Outside Or Of Another Kind; A Body Without Them Gets The Server's Defaults (--reconnect, --hold-vote-seconds, --max-pause-seconds); create_room Checks The Bounds Too; Demo Rooms Follow The Server's Setting") {
        {
            RoomManager mgr{MapStore(maps_dir())};
            const auto call = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                return handle_control(mgr, rq, 5000);
            };
            const auto parse = [](const ctl::HttpResponse& r) {
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(r.body, v, &why);
                return v;
            };
            // the built-in defaults: do not hold seats, 30 s, 1800 s
            ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"O-1"})");
            ASSERT_EQ(r.status, 201);
            ctl::JsonValue v = parse(r);
            ASSERT_TRUE(!v.get("reconnect").as_bool_or(true) && v.get("hold_vote_seconds").as_int_or(0) == 30 && v.get("max_pause_seconds").as_int_or(0) == 1800);
            r = call(R"({"map":"TINY.LVL","code":"O-2","reconnect":true,"hold_vote_seconds":5,"max_pause_seconds":86400})");     // the edges are good
            ASSERT_EQ(r.status, 201);
            v = parse(r);
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 5 && v.get("max_pause_seconds").as_int_or(0) == 86400);
            r = call(R"({"map":"TINY.LVL","code":"O-3","hold_vote_seconds":3600,"max_pause_seconds":60})");
            ASSERT_EQ(r.status, 201);
            v = parse(r);
            ASSERT_TRUE(v.get("hold_vote_seconds").as_int_or(0) == 3600 && v.get("max_pause_seconds").as_int_or(0) == 60 && !v.get("reconnect").as_bool_or(true));
            for (const char* bad : {R"("hold_vote_seconds":4)", R"("hold_vote_seconds":3601)", R"("hold_vote_seconds":0)", R"("hold_vote_seconds":-5)", R"("hold_vote_seconds":"30")", R"("hold_vote_seconds":30.5)",
                                    R"("max_pause_seconds":59)", R"("max_pause_seconds":86401)", R"("max_pause_seconds":0)", R"("max_pause_seconds":"1800")", R"("reconnect":"yes")", R"("reconnect":1)", R"("reconnect":null)"}) {
                r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
                ASSERT_MSG(r.status == 400, bad);
                const std::string why = parse(r).get("error").str();
                ASSERT_TRUE(why.size() > 10);
                // the body is refused by the control interface itself, with the bounds in its words (the room manager would refuse the numbers too, with a message of its own)
                const std::string key = std::string(bad).find("hold_vote") != std::string::npos ? "\"hold_vote_seconds\" must be an integer from 5 to 3600"
                                        : std::string(bad).find("max_pause") != std::string::npos ? "\"max_pause_seconds\" must be an integer from 60 to 86400" : "\"reconnect\" must be true or false";
                ASSERT_MSG(why == key, why);
            }
            ASSERT_EQ(mgr.room_count(), size_t{3});                                       // none of the refused ones made a room
        }
        {   // the server's own defaults reach a room that says nothing; what a room says wins
            ServerLimits limits;
            limits.reconnect = true;
            limits.hold_vote_ms = 45000;
            limits.max_pause_ms = 900000;
            RoomManager mgr{MapStore(maps_dir()), limits};
            const auto call = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(handle_control(mgr, rq, 5000).body, v, &why);
                return v;
            };
            ctl::JsonValue v = call(R"({"map":"TINY.LVL","code":"D-1"})");
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 45 && v.get("max_pause_seconds").as_int_or(0) == 900);
            v = call(R"({"map":"TINY.LVL","code":"D-2","reconnect":false,"max_pause_seconds":120})");
            ASSERT_TRUE(!v.get("reconnect").as_bool_or(true) && v.get("hold_vote_seconds").as_int_or(0) == 45 && v.get("max_pause_seconds").as_int_or(0) == 120);
            const RoomSpec defaults = mgr.default_spec();
            ASSERT_TRUE(defaults.reconnect && defaults.vote_after_ms == 45000 && defaults.max_pause_ms == 900000 && defaults.max_log_bytes == net::TurnLog::kDefaultMaxBytes);
        }
        {   // create_room checks the bounds for everybody who does not come through the JSON
            RoomManager mgr{MapStore(maps_dir())};
            const auto made = [&](const std::string& code, const std::function<void(RoomSpec&)>& change) {
                RoomSpec spec = spec_of(code, 2);
                change(spec);
                return mgr.create_room(spec, 5000);
            };
            ASSERT_EQ(made("B-1", [](RoomSpec& s) { s.vote_after_ms = 4999; }).http_status, 400);
            ASSERT_EQ(made("B-2", [](RoomSpec& s) { s.vote_after_ms = 3600001; }).http_status, 400);
            ASSERT_EQ(made("B-3", [](RoomSpec& s) { s.max_pause_ms = 59999; }).http_status, 400);
            ASSERT_EQ(made("B-4", [](RoomSpec& s) { s.max_pause_ms = 86400001; }).http_status, 400);
            ASSERT_EQ(made("B-5", [](RoomSpec& s) { s.max_log_bytes = 100; }).http_status, 400);
            ASSERT_EQ(made("B-6", [](RoomSpec& s) { s.max_log_bytes = size_t{2} << 30; }).http_status, 400);
            ASSERT_TRUE(made("B-7", [](RoomSpec& s) { s.vote_after_ms = 5000; s.max_pause_ms = 60000; s.max_log_bytes = 1024; }).ok);
            ASSERT_TRUE(made("B-8", [](RoomSpec& s) { s.vote_after_ms = 3600000; s.max_pause_ms = 86400000; s.max_log_bytes = size_t{1} << 30; }).ok);
        }
        {   // demo rooms follow the server's setting: keys in the Welcome (or none), and the status says so
            for (const bool hold : {false, true}) {
                ServerLimits limits;
                limits.demo_rooms = 2;
                limits.demo_map = "TINY.LVL";
                limits.reconnect = hold;
                RWorld w(limits);
                RClient& a = w.connect("Ann", "demo-tiny-2p-held");
                RClient& b = w.connect("Bob", "demo-tiny-2p-held");
                w.run(4000 + kPre);
                const RoomStatus s = w.status("demo-tiny-2p-held");
                ASSERT_TRUE(s.state == RoomState::Running && s.reconnect == hold);
                ASSERT_EQ(!net::key_is_zero(a.lobby->key()) && !net::key_is_zero(b.lobby->key()), hold);
                ASSERT_EQ(net::key_is_zero(a.lobby->key()) && net::key_is_zero(b.lobby->key()), !hold);
            }
        }
    } TEST_END();

    TEST_CASE("S3.48 The Server's Budget For The Logs Of All Its Rooms: The Logs Together Never Hold More Than The Budget (The Statistics Say How Much); A Log That Cannot Grow Is Not Kept And The Room Falls Back To Dropping At Once (Its Status Says Unusable); What A Match Held Is Given Back When It Ends, And A New Room Can Log Again") {
        ServerLimits limits;
        limits.log_budget_bytes = 9 * 1024;                                              // 9 KB for all the logs of the server (a quiet match adds about 120 bytes a second)
        RWorld w(limits);
        ASSERT_EQ(w.mgr.log_budget_bytes(), uint64_t{9 * 1024});
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-1", 3), w.now).ok);
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-2", 2), w.now).ok);
        RClient& a1 = w.connect("A1", "G-1");
        RClient& b1 = w.connect("B1", "G-1");
        w.connect("C1", "G-1");
        RClient& a2 = w.connect("A2", "G-2");
        RClient& b2 = w.connect("B2", "G-2");
        (void)a2;
        (void)b2;
        uint64_t most = 0;
        bool first_dead = false;
        bool both = false;
        for (int i = 0; i < 40000 && !both; ++i) {
            w.run(250);
            most = std::max(most, w.mgr.log_bytes());
            ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());                   // never more than the budget, however busy the rooms are
            const RoomStatus r1 = w.status("G-1");
            const RoomStatus r2 = w.status("G-2");
            if (!first_dead && (!r1.log_usable || !r2.log_usable)) {
                first_dead = true;
                const RoomStatus& dead = r1.log_usable ? r2 : r1;
                ASSERT_TRUE(dead.log_bytes == 0 && dead.log_turns == 0);                  // a log that cannot grow is DEAD: it freed what it held at once (and gave it back to the budget)
            }
            both = !r1.log_usable && !r2.log_usable;                                      // (the one that is left may grow in what the dead one gave back, until it is full too)
        }
        ASSERT_TRUE(first_dead && both);
        ASSERT_TRUE(most > 6 * 1024);                                                    // it filled up (the budget counts what the logs have allocated, which grows by doubling)
        w.run(2000);
        const RoomStatus s1 = w.status("G-1");
        const RoomStatus s2 = w.status("G-2");
        ASSERT_FALSE(s1.log_usable);
        ASSERT_FALSE(s2.log_usable);                                                     // the server has no memory for either: neither log grows any more, and both are dead (they hold nothing)
        ASSERT_TRUE(w.mgr.log_bytes() == 0);
        ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());
        // a room with a log that is not usable drops a lost seat at once: no pause, no way back (its key is told "dropped")
        const uint8_t bob = b1.lobby->my_seat();
        b1.reconnects = true;
        w.cut(b1);
        w.run(2000);
        const RoomStatus after = w.status("G-1");
        ASSERT_TRUE(!after.paused && after.absent.empty() && after.state == RoomState::Running);          // the two that are left play on
        ASSERT_TRUE(a1.sim.is_player_dropped(bob));
        ASSERT_TRUE(b1.lost && b1.was_rejected && b1.rejected == net::RejectReason::Dropped);
        // two dead logs hold nothing: the budget is free, and the next room can keep a log again
        ASSERT_TRUE(w.mgr.close_room("G-2", w.now));
        w.run(100);
        ASSERT_TRUE(w.mgr.log_bytes() == 0);
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-3", 2), w.now).ok);
        w.connect("A3", "G-3");
        w.connect("B3", "G-3");
        w.run(8000);
        const RoomStatus s3 = w.status("G-3");
        ASSERT_TRUE(s3.state == RoomState::Running && s3.log_usable && s3.log_bytes > 0);
        ASSERT_TRUE(w.mgr.log_bytes() > 0 && w.mgr.log_bytes() <= w.mgr.log_budget_bytes());
        // the end of a match gives its log back at once, and the status keeps what it held
        ASSERT_TRUE(w.mgr.close_room("G-3", w.now));
        w.run(100);
        const RoomStatus ended = w.status("G-3");
        ASSERT_TRUE(ended.log_turns > 0 && ended.log_bytes > 0 && ended.log_usable);
        ASSERT_TRUE(w.mgr.log_bytes() == 0);                                             // what the room held is not held any more
    } TEST_END();

    TEST_CASE("S3.49 Lag Is Not A Loss In A Room That Holds Seats: A Machine That Draws A Frame Every 4 s Falls Behind (The Others Are Told It Lags, Nobody Is Paused, Nobody Waits), Is Not Held And Not Dropped, And Is Level Again When Its Frames Come Back; The States Agree At The End") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("S-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "S-1");
        RClient& b = w.connect("Bob", "S-1");
        RClient& c = w.connect("Cat", "S-1");
        w.run(4000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.frame_every_ms = 4000;
        bool paused_ever = false;
        bool told = false;
        const uint32_t ticks_before = w.status("S-1").ticks;
        for (int i = 0; i < 3000; ++i) {                                                  // 30 s
            w.run(10);
            const RoomStatus st = w.status("S-1");
            paused_ever = paused_ever || st.paused || !st.absent.empty();
            told = told || a.session->lagging_seat() == bob;
        }
        ASSERT_FALSE(paused_ever);
        ASSERT_TRUE(told);
        ASSERT_TRUE(w.status("S-1").ticks - ticks_before >= 590);                         // the room played on at its own pace (20 ticks a second)
        ASSERT_FALSE(a.sim.is_player_dropped(bob) || c.sim.is_player_dropped(bob));
        ASSERT_TRUE(w.status("S-1").rejoins == 0 && w.status("S-1").drops_by_cap == 0 && w.status("S-1").drops_by_vote == 0);
        b.frame_every_ms = 0;                                                             // its frames are back: it runs the backlog at up to 4x
        w.run(40000);
        ASSERT_TRUE(b.session->mode() == net::ClientSession::Mode::Normal && !b.session->catching_up());
        ASSERT_TRUE(a.session->lagging_seat() == 255);
        w.play_to_the_end("S-1");
        ASSERT_TRUE(w.status("S-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.50 A Room That Does Not Hold Seats (reconnect: false, Said Out Loud) Behaves Exactly As In v0.0.94: A Cut Link Is A Drop For Everybody At The Same Tick Within 3 s (And The Machine Is Lost At Once), When Everybody Has Left The Room Finishes; A Seat That Stops Executing Is Dropped After 30 s And The Others Play On, With Two Players The Room Finishes; No Presence Is Ever Sent") {
        {
            RWorld w;
            RoomSpec spec = spec_of("DROP-1", 3);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& a = w.connect("A", "DROP-1");
            RClient& b = w.connect("B", "DROP-1");
            RClient& c = w.connect("C", "DROP-1");
            w.run(4000 + kPre);
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
            w.cut(b);
            bool paused_ever = false;
            for (int i = 0; i < 300; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("DROP-1").paused;
            }
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(a.sim.is_player_dropped(1) && c.sim.is_player_dropped(1));
            ASSERT_TRUE(b.lost && !b.was_rejected);                                       // its match is over at once: no key, no way back
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
            for (RClient* p : {&a, &c}) ASSERT_TRUE(p->session->presence().missing.empty() && !p->session->paused());
            w.cut(a);
            w.cut(c);
            w.run(3000);
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Finished);
            ASSERT_TRUE(w.status("DROP-1").reason == "everybody left" || w.status("DROP-1").reason == "the match ended");
        }
        {
            RWorld w;
            RoomSpec spec = spec_of("LAG-1", 3);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& a = w.connect("Ann", "LAG-1");
            RClient& b = w.connect("Bob", "LAG-1");
            RClient& c = w.connect("Cat", "LAG-1");
            w.run(3000 + kPre);
            const uint8_t bob = b.lobby->my_seat();
            const uint32_t ticks_before = w.status("LAG-1").ticks;
            b.hung = true;                                                               // Bob's program hangs: it neither acks nor answers
            w.run(8000);
            ASSERT_TRUE(w.status("LAG-1").ticks - ticks_before >= 150);                  // the room did not wait for Bob
            ASSERT_TRUE(a.session->lagging_seat() == bob && c.session->lagging_seat() == bob);
            w.run(18000);                                                                // 26 s: a lagger, not yet dropped
            ASSERT_FALSE(a.sim.is_player_dropped(bob));
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running && !w.status("LAG-1").paused);       // (a room without a way back never pauses, however long a seat is silent)
            w.run(7000);                                                                 // 33 s: dropped, at one tick for both of the others
            ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
            ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
            w.run(3000);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            ASSERT_FALSE(a.lost || c.lost);
            ASSERT_EQ(a.session->lagging_seat(), 255);
        }
        {
            RWorld w;
            RoomSpec spec = spec_of("LAG-2", 2);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "LAG-2");
            RClient& b = w.connect("Bob", "LAG-2");
            w.run(3000 + kPre);
            b.hung = true;
            w.run(40000);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Finished);                 // two players: the one that is left has won, the room ends
        }
    } TEST_END();

    TEST_CASE("S3.51 Silence Is A Loss After 10 s In A Room That Holds Seats (A Hung Window): The Room Pauses 10 s After The Last Word And Says So In Its Status (\"absent\", Not \"lagging\"), 9 s Of Silence Is No Loss; The Window That Wakes Up Finds Its Link Closed, Comes Back Through The Door And The Match Goes On To Its End, Identical") {
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("Z-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "Z-1");
            RClient& b = w.connect("Bob", "Z-1");
            RClient& c = w.connect("Cat", "Z-1");
            w.run(4000 + kPre);
            const uint8_t bob = b.lobby->my_seat();
            b.hung = true;
            const uint32_t at = w.now;
            uint32_t paused_after_ms = 0;
            bool noticed = false;
            for (int i = 0; i < 1100 && paused_after_ms == 0; ++i) {
                w.run(10);
                noticed = noticed || a.session->lagging_seat() == bob;
                if (w.status("Z-1").paused) paused_after_ms = w.now - at;
            }
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);          // 10 s after its last word
            ASSERT_TRUE(noticed);                                                        // (for 7 s of it the others were told that it lags)
            w.run(300);
            const RoomStatus s = w.status("Z-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == bob && !s.absent[0].catching_up);
            ASSERT_EQ(a.session->lagging_seat(), 255);                                   // the notice ended with the loss: it is missing now, not lagging
            ASSERT_TRUE(a.session->paused() && c.session->paused());
            w.run(5000);
            b.hung = false;                                                              // the window wakes up
            ASSERT_TRUE(w.until([&]() { return !w.status("Z-1").paused && b.session->mode() == net::ClientSession::Mode::Normal; }, 10000));
            ASSERT_EQ(w.status("Z-1").rejoins, 1u);
            w.play_to_the_end("Z-1");
            ASSERT_TRUE(w.status("Z-1").state == RoomState::Finished);
            ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
            ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
        }
        {   // 9 s of silence is no loss: nobody is paused, the window catches up as a lagger
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("Z-2", 2), w.now).ok);
            w.connect("Ann", "Z-2");
            RClient& b = w.connect("Bob", "Z-2");
            w.run(4000);
            b.hung = true;
            bool paused_ever = false;
            for (int i = 0; i < 900; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("Z-2").paused;
            }
            b.hung = false;
            for (int i = 0; i < 2000; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("Z-2").paused;
            }
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(w.status("Z-2").rejoins == 0 && b.session->mode() == net::ClientSession::Mode::Normal);
        }
    } TEST_END();

    // ---- the fixes of the review of release A, through the real manager and door (S3.52 - S3.57) -------------------------------------------------------------------------------

    TEST_CASE("S3.52 The Resume Countdown Through The Manager: After A Pause Of 6 s And A Return The Room Is Held For 10 s More (The Status Says \"paused\" And The Seconds Left, The Ticks Stand Still, The Pause Total Does Not Grow); The Wall-Clock Limit Counts Play And Not The Waiting Nor The Countdown; A Room With The Countdown Off Resumes At Once; A Blip Is No Countdown") {
        {
            RWorld w;
            RoomSpec s = held_spec("N-1", 3);
            s.resume_countdown_ms = 10000;
            s.run_ms = 30000;                                                            // the limit counts the match's play: 30 s of it
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            RClient& a = w.connect("Ann", "N-1");
            RClient& b = w.connect("Bob", "N-1");
            RClient& c = w.connect("Cat", "N-1");
            w.run(12000 + kPre);                                                                // 12 s of play (the match began about a second after the clients came)
            ASSERT_TRUE(w.status("N-1").state == RoomState::Running);
            b.reconnects = false;
            w.cut(b);
            w.run(12000);                                                                // 12 s of waiting
            RoomStatus st = w.status("N-1");
            ASSERT_TRUE(st.paused && st.absent.size() == 1 && st.resume_s == 0);
            const uint32_t ticks_at_pause = st.ticks;
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return w.status("N-1").absent.empty(); }, 8000));                // verified: nobody is missing now ...
            const uint32_t back_at = w.now;
            st = w.status("N-1");
            ASSERT_TRUE(st.paused && st.resume_s >= 9 && st.resume_s <= 10 && st.rejoins == 1);       // ... and the room is held: ten seconds to go
            const uint32_t pause_total = st.paused_s;
            ASSERT_TRUE(pause_total >= 11 && pause_total <= 14);
            w.run(150);                                                                  // (the Presence of the countdown reaches the players)
            ASSERT_TRUE(a.session->paused() && c.session->paused() && a.session->resume_seconds_left() >= 8 && a.session->resume_seconds_left() <= 10);
            std::vector<unsigned> seen;
            while (w.now - back_at < 9500) {
                w.run(100);
                st = w.status("N-1");
                ASSERT_TRUE(st.paused && st.absent.empty() && st.ticks <= ticks_at_pause + 2);   // the ticks stand still
                ASSERT_EQ(st.paused_s, pause_total);                                     // the pause total does not grow in the countdown: the cap is not spent by it
                if (seen.empty() || seen.back() != st.resume_s) seen.push_back(st.resume_s);
            }
            ASSERT_TRUE(seen.size() >= 9 && seen.front() >= 9 && seen.back() <= 1);
            for (size_t i = 1; i < seen.size(); ++i) ASSERT_TRUE(seen[i] < seen[i - 1]);       // counting down
            w.run(1500);
            st = w.status("N-1");
            ASSERT_TRUE(!st.paused && st.resume_s == 0 && st.ticks > ticks_at_pause + 20 && st.state == RoomState::Running);
            ASSERT_FALSE(a.session->paused() || b.session->paused() || c.session->paused());
            // 12 s of play + 12 s of waiting + 10 s of countdown + play: the room has played about 14 s, the limit is 30 s of play: it runs on (it would have failed at 22 s of "play" if the waiting and
            // the countdown counted: 12 + 12 + 10 = 34 s of wall clock already)
            w.run(10000);
            ASSERT_TRUE(w.status("N-1").state == RoomState::Running);
            w.run(20000);
            st = w.status("N-1");
            ASSERT_TRUE(st.state == RoomState::Failed && st.reason.find("took longer") != std::string::npos);      // and the limit does come, after about 30 s of play
        }
        {   // the countdown off: the return resumes the room at once (a room's setting of 0)
            RWorld w;
            RoomSpec s = held_spec("N-2", 2);
            s.resume_countdown_ms = 0;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            w.connect("Ann", "N-2");
            RClient& b = w.connect("Bob", "N-2");
            w.run(5000);
            b.reconnects = false;
            w.cut(b);
            w.run(6000);
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("N-2").paused; }, 8000));
            ASSERT_EQ(w.status("N-2").resume_s, 0u);
        }
        {   // a blip (the link cut and back within a second or two): no countdown
            RWorld w;
            RoomSpec s = held_spec("N-3", 2);
            s.resume_countdown_ms = 10000;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            w.connect("Ann", "N-3");
            RClient& b = w.connect("Bob", "N-3");
            w.run(5000);
            w.cut(b);
            bool counted_down = false;
            ASSERT_TRUE(w.until([&]() {
                counted_down = counted_down || w.status("N-3").resume_s != 0;
                return w.status("N-3").rejoins == 1 && !w.status("N-3").paused;
            }, 5000));
            ASSERT_FALSE(counted_down);
        }
    } TEST_END();

    TEST_CASE("S3.53 The New Settings: \"max_catch_up_seconds\" (10 - 3600, Default 300) And \"resume_countdown_seconds\" (0 - 60, Default 10) In The Control Interface, The Server's Defaults (--max-catch-up-seconds, --resume-countdown-seconds) And The Status Keys (The Counters Of The Budgets, The Countdown); The Bounds Are Checked For Everybody; A Room Cannot Keep Fewer Connections Than It Has Players") {
        RoomManager mgr{MapStore(maps_dir())};
        const auto call = [&](const std::string& body) {
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = body;
            return handle_control(mgr, rq, 5000);
        };
        const auto parse = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ASSERT_TRUE(ServerLimits().max_catch_up_ms == 300000u && ServerLimits().resume_countdown_ms == 10000u);
        ASSERT_TRUE(RoomSpec().max_catch_up_ms == 300000u && RoomSpec().resume_countdown_ms == 10000u && RoomSpec().max_connections == Room::kMaxConnections);
        ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"Q-1"})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 300 && v.get("resume_countdown_seconds").as_int_or(99) == 10);
        ASSERT_TRUE(v.get("resume_seconds").as_int_or(99) == 0 && v.get("rejoins_refused").as_int_or(99) == 0 && v.get("catch_up_expired").as_int_or(99) == 0 && v.get("streamed_bytes").as_int_or(99) == 0);
        r = call(R"({"map":"TINY.LVL","code":"Q-2","max_catch_up_seconds":10,"resume_countdown_seconds":0})");           // the edges are good
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 10 && v.get("resume_countdown_seconds").as_int_or(99) == 0);
        r = call(R"({"map":"TINY.LVL","code":"Q-3","max_catch_up_seconds":3600,"resume_countdown_seconds":60})");
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 3600 && v.get("resume_countdown_seconds").as_int_or(0) == 60);
        for (const char* bad : {R"("max_catch_up_seconds":9)", R"("max_catch_up_seconds":3601)", R"("max_catch_up_seconds":0)", R"("max_catch_up_seconds":-1)", R"("max_catch_up_seconds":"300")", R"("max_catch_up_seconds":300.5)",
                                R"("resume_countdown_seconds":61)", R"("resume_countdown_seconds":-1)", R"("resume_countdown_seconds":"10")", R"("resume_countdown_seconds":10.5)", R"("resume_countdown_seconds":true)"}) {
            r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            const std::string key = std::string(bad).find("max_catch_up") != std::string::npos ? "\"max_catch_up_seconds\" must be an integer from 10 to 3600" : "\"resume_countdown_seconds\" must be an integer from 0 to 60";
            ASSERT_MSG(parse(r).get("error").str() == key, parse(r).get("error").str());
        }
        ASSERT_EQ(mgr.room_count(), size_t{3});
        {   // the server's defaults reach a room that says nothing; what a room says wins
            ServerLimits limits;
            limits.max_catch_up_ms = 120000;
            limits.resume_countdown_ms = 5000;
            RoomManager other{MapStore(maps_dir()), limits};
            const auto call2 = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                return parse(handle_control(other, rq, 5000));
            };
            ctl::JsonValue d = call2(R"({"map":"TINY.LVL","code":"D-1"})");
            ASSERT_TRUE(d.get("max_catch_up_seconds").as_int_or(0) == 120 && d.get("resume_countdown_seconds").as_int_or(0) == 5);
            d = call2(R"({"map":"TINY.LVL","code":"D-2","resume_countdown_seconds":0,"max_catch_up_seconds":30})");
            ASSERT_TRUE(d.get("max_catch_up_seconds").as_int_or(0) == 30 && d.get("resume_countdown_seconds").as_int_or(9) == 0);
            ASSERT_TRUE(other.default_spec().max_catch_up_ms == 120000 && other.default_spec().resume_countdown_ms == 5000);
        }
        {   // create_room checks the bounds for everybody who does not come through the JSON
            RoomManager direct{MapStore(maps_dir())};
            const auto made = [&](const std::string& code, const std::function<void(RoomSpec&)>& change) {
                RoomSpec spec = spec_of(code, 2);
                change(spec);
                return direct.create_room(spec, 5000);
            };
            ASSERT_EQ(made("C-1", [](RoomSpec& s) { s.max_catch_up_ms = 9999; }).http_status, 400);
            ASSERT_EQ(made("C-2", [](RoomSpec& s) { s.max_catch_up_ms = 3600001; }).http_status, 400);
            ASSERT_EQ(made("C-3", [](RoomSpec& s) { s.resume_countdown_ms = 60001; }).http_status, 400);
            ASSERT_EQ(made("C-4", [](RoomSpec& s) { s.max_connections = 1; }).http_status, 400);                    // fewer than the players
            ASSERT_EQ(made("C-5", [](RoomSpec& s) { s.max_connections = 5000; }).http_status, 400);
            ASSERT_TRUE(made("C-6", [](RoomSpec& s) { s.max_catch_up_ms = 10000; s.resume_countdown_ms = 0; s.max_connections = 2; }).ok);
            ASSERT_TRUE(made("C-7", [](RoomSpec& s) { s.max_catch_up_ms = 3600000; s.resume_countdown_ms = 60000; s.max_connections = 4096; }).ok);
        }
    } TEST_END();

    TEST_CASE("S3.54 A Key Holder Cannot Freeze A Room Through The Door (The Real Server): A Hello Every 12 s, Each With A Little Progress, Is A Catch-Up Of 30 s (The Room's Budget) And Then Refused; The Status Says It (Expired, Refused, Absent Again), The Others May Vote, The Cap (90 s) Takes The Seat And The Match Goes On To Its End") {
        RWorld w;
        RoomSpec s = held_spec("W-1", 3);
        s.max_catch_up_ms = 30000;
        s.max_pause_ms = 90000;
        ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
        RClient& a = w.connect("Ann", "W-1");
        RClient& b = w.connect("Bob", "W-1");
        RClient& c = w.connect("Cat", "W-1");
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("W-1").state == RoomState::Running);
        const net::SeatKey key = c.lobby->key();
        const uint8_t cat = c.lobby->my_seat();
        c.reconnects = false;
        w.cut(c);
        const uint32_t cut_at = w.now;
        w.run(200);
        ASSERT_TRUE(w.status("W-1").paused);
        const uint32_t ticks_at_pause = w.status("W-1").ticks;
        std::vector<net::Connection*> links;
        uint32_t next_hello = w.now;
        uint32_t next_ack = w.now + 3000;
        uint32_t total = 0;
        uint32_t creeping = 0;
        const auto step = [&]() {
            w.run(10);
            if (static_cast<int32_t>(w.now - next_hello) >= 0) {
                net::Connection* link = w.open_link();
                net::HelloMsg h;
                h.name = "Eve";
                h.room = "W-1";
                h.key = key;
                h.have_turns = 1;
                link->send(net::encode(h));
                links.push_back(link);
                next_hello = w.now + 12000;
            }
            for (net::Connection* l : links) {
                std::vector<uint8_t> msg;
                while (l->poll(msg)) {
                    net::CatchUpMsg cu;
                    if (net::peek_type(msg) == net::MsgType::CatchUp && net::decode(msg, cu)) total = cu.total_turns;
                }
            }
            if (static_cast<int32_t>(w.now - next_ack) >= 0 && total > 100 && !links.empty() && links.back()->is_open()) {
                creeping = std::min(total, creeping + std::max<uint32_t>(total / 40, 2));
                net::AckMsg ack;
                ack.turn = creeping - 1;
                links.back()->send(net::encode(ack));
                next_ack = w.now + 3000;
            }
        };
        while (w.now - cut_at < 25000) step();
        RoomStatus st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.absent.size() == 1 && st.absent[0].seat == cat && st.absent[0].catching_up && st.absent[0].progress > 0);       // 25 s into a catch-up that shows progress
        ASSERT_EQ(st.catch_up_expired, 0u);
        while (w.now - cut_at < 33000) step();
        st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.absent.size() == 1 && !st.absent[0].catching_up);                // 33 s: the budget of 30 s is spent: it is absent again
        ASSERT_EQ(st.catch_up_expired, 1u);
        ASSERT_TRUE(st.rejoins == 0);
        ASSERT_TRUE(st.vote_seat == cat);                                                            // and the others are asked about it
        while (w.now - cut_at < 40000) step();
        ASSERT_TRUE(w.status("W-1").rejoins_refused >= 1);                                           // (the Hello of 36 s: the fourth in a minute, and a seat whose budget is spent)
        while (w.now - cut_at < 80000) step();
        st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.ticks <= ticks_at_pause + 2);
        ASSERT_TRUE(st.rejoins_refused >= 3);
        const uint64_t refused_before = w.mgr.connections_refused();
        while (w.now - cut_at < 95000) step();
        st = w.status("W-1");
        ASSERT_TRUE(!st.paused && st.state == RoomState::Running && st.drops_by_cap == 1 && st.drops_by_vote == 0);       // the cap: the seat is dropped and the match goes on
        ASSERT_TRUE(st.rejoins == 0 && st.absent.empty());
        (void)refused_before;
        w.run(3000);
        ASSERT_TRUE(w.status("W-1").ticks > ticks_at_pause + 40);
        w.play_to_the_end("W-1");
        ASSERT_TRUE(w.status("W-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        ASSERT_TRUE(a.sim.is_player_dropped(cat) && b.sim.is_player_dropped(cat));
        ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    } TEST_END();

    TEST_CASE("S3.55 A Key Holder Cannot Make The Server Stream Its Log Again And Again (The Real Server): With A Log Of More Than A Megabyte, 100 Hellos In 10 s Make Three Streams At Most; The Status Counts What Was Refused And What Was Streamed (3 x The Log At The Most), A Key Is In None Of It; The Others Are Not Disturbed And The Seat Is Held: The Vote And The Cap Apply") {
        std::ostringstream captured;
        std::streambuf* old_cerr = std::cerr.rdbuf(captured.rdbuf());
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("M-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "M-1");
            RClient& b = w.connect("Bob", "M-1");
            RClient& c = w.connect("Cat", "M-1");
            w.run(6000 + kPre);
            ASSERT_TRUE(w.status("M-1").state == RoomState::Running);
            a.orders = false;
            b.orders = false;
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            for (int i = 0; i < 800 && w.status("M-1").log_bytes <= 1200u * 1024u; ++i) {         // two players put commands of 32 ants into every turn: a log of more than a megabyte
                w.run(10);
                for (net::Connection* end : {a.end, b.end}) {
                    for (int k = 0; k < 8; ++k) {
                        net::CommandMsg msg;
                        msg.command.type = sim::CommandType::GroupMove;
                        msg.command.issuer = 0;
                        msg.command.tile_x = 5;
                        msg.command.tile_y = 5;
                        for (const uint32_t id : many) msg.command.ants.push_back(id);          // (a loop: GCC 12 reads the copy of a vector as a null memmove)
                        end->send(net::encode(msg));
                    }
                }
            }
            const uint32_t log_bytes = w.status("M-1").log_bytes;
            ASSERT_TRUE(log_bytes > 1024u * 1024u && w.status("M-1").log_usable);
            const net::SeatKey key = c.lobby->key();
            c.reconnects = false;
            w.cut(c);
            w.run(300);
            ASSERT_TRUE(w.status("M-1").paused);
            std::vector<net::Connection*> links;
            uint32_t sent = 0;
            for (int i = 0; i < 1400; ++i) {                                                     // 100 Hellos in 10 s, and 4 s more
                w.run(10);
                if (sent < 100 && i % 10 == 0) {
                    net::Connection* link = w.open_link();
                    net::HelloMsg h;
                    h.name = "Eve";
                    h.room = "M-1";
                    h.key = key;
                    h.have_turns = 1;
                    link->send(net::encode(h));
                    links.push_back(link);
                    ++sent;
                }
                for (net::Connection* l : links) {
                    std::vector<uint8_t> msg;
                    while (l->poll(msg)) {
                        net::TurnBatchMsg batch;
                        if (net::peek_type(msg) == net::MsgType::TurnBatch && net::decode(msg, batch) && !batch.turns.empty()) {
                            net::AckMsg ack;
                            ack.turn = batch.first_turn + static_cast<uint32_t>(batch.turns.size()) - 1;
                            l->send(net::encode(ack));
                        }
                    }
                }
            }
            ASSERT_EQ(sent, 100u);
            const RoomStatus st = w.status("M-1");
            ASSERT_TRUE(st.streamed_bytes <= 3 * uint64_t{st.log_bytes} && st.streamed_bytes >= uint64_t{st.log_bytes} / 2);        // at most three streams of the log (a hundred before)
            ASSERT_TRUE(st.rejoins_refused >= 90);                                                // every refusal counted
            ASSERT_EQ(st.rejoins, 0u);
            ASSERT_TRUE(st.state == RoomState::Running && !a.lost && !b.lost);
            ASSERT_TRUE(st.absent.size() == 1 && st.absent[0].seat == c.lobby->my_seat());          // the seat is held (catching up or absent): the vote and the cap apply
            // the status says it in JSON, and a key is nowhere in it
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/rooms/M-1";
            const ctl::HttpResponse r = handle_control(w.mgr, rq, w.now);
            ctl::JsonValue v;
            std::string why;
            ASSERT_TRUE(ctl::parse_json(r.body, v, &why));
            ASSERT_TRUE(v.get("rejoins_refused").as_int_or(0) >= 90 && v.get("streamed_bytes").as_int_or(0) > 0 && v.get("streamed_bytes").as_int_or(0) <= 3 * int64_t{log_bytes});
            ASSERT_TRUE(r.body.find(hex_of(key)) == std::string::npos && r.body.find("key") == std::string::npos);
            ASSERT_TRUE(captured.str().find(hex_of(key)) == std::string::npos);
        }
        std::cerr.rdbuf(old_cerr);
        ASSERT_TRUE(captured.str().empty());
    } TEST_END();

    TEST_CASE("S3.56 Hostile Rooms Cannot Exhaust The Way Back For Everybody (The Real Server): 16 Rooms Whose Players Fill Their Logs Are Dead After A Few Seconds (Each Log Frees What It Held The Moment It Dies, The Server's Budget Is Whole Again), And A Room That Is Made Afterwards Keeps Its Log, Holds A Lost Seat And Takes It Back") {
        ServerLimits limits;
        limits.log_budget_bytes = 3u * 1024u * 1024u;
        RWorld w(limits);
        for (int i = 0; i < 16; ++i) {
            RoomSpec s = held_spec("H" + std::to_string(i), 2);
            s.max_log_bytes = 256 * 1024;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
        }
        std::vector<RClient*> hostile;
        for (int i = 0; i < 16; ++i) {
            hostile.push_back(&w.connect("A" + std::to_string(i), "H" + std::to_string(i)));
            hostile.push_back(&w.connect("B" + std::to_string(i), "H" + std::to_string(i)));
        }
        w.run(4000 + kPre);
        for (int i = 0; i < 16; ++i) ASSERT_TRUE(w.status("H" + std::to_string(i)).state == RoomState::Running);
        for (RClient* c : hostile) c->orders = false;
        std::vector<uint32_t> many;
        for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
        net::CommandMsg flood;
        flood.command.type = sim::CommandType::GroupMove;
        flood.command.tile_x = 5;
        flood.command.tile_y = 5;
        for (const uint32_t id : many) flood.command.ants.push_back(id);            // (a loop: GCC 12 reads the copy of a vector as a null memmove)
        const std::vector<uint8_t> flood_bytes = net::encode(flood);
        uint64_t most = 0;
        for (int i = 0; i < 700; ++i) {                                                           // 7 s: every player fills its room's log
            w.run(10);
            for (RClient* c : hostile) {
                if (c->end != nullptr && c->end->is_open()) {
                    for (int k = 0; k < 4; ++k) c->end->send(flood_bytes);
                }
            }
            most = std::max(most, w.mgr.log_bytes());
            ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());                           // never more than the budget
        }
        ASSERT_TRUE(most > 1024 * 1024);                                                          // (they did fill it)
        int dead = 0;
        for (int i = 0; i < 16; ++i) {
            const RoomStatus s = w.status("H" + std::to_string(i));
            dead += !s.log_usable ? 1 : 0;
            if (!s.log_usable) ASSERT_TRUE(s.log_bytes == 0 && s.log_turns == 0);                  // a dead log holds nothing
        }
        ASSERT_EQ(dead, 16);                                                                      // every log of a room that was made to fill it is dead
        ASSERT_EQ(w.mgr.log_bytes(), uint64_t{0});                                                // ... and the budget is whole again (the dead logs used to keep it for ever)
        for (RClient* c : hostile) c->orders = false;
        // the good room: made after the hostile ones died, it logs, holds a lost seat and gives it back
        ASSERT_TRUE(w.mgr.create_room(held_spec("GOOD", 2), w.now).ok);
        w.connect("Ann", "GOOD");
        RClient& bob = w.connect("Bob", "GOOD");
        w.run(5000 + kPre);
        RoomStatus g = w.status("GOOD");
        ASSERT_TRUE(g.state == RoomState::Running && g.log_usable && g.log_bytes > 0 && w.mgr.log_bytes() > 0);
        bob.reconnects = false;
        w.cut(bob);
        w.run(1000);
        g = w.status("GOOD");
        ASSERT_TRUE(g.paused && g.absent.size() == 1);                                            // held, not dropped at once
        bob.reconnects = true;
        ASSERT_TRUE(w.until([&]() { return !w.status("GOOD").paused; }, 8000));
        ASSERT_EQ(w.status("GOOD").rejoins, 1u);
        ASSERT_FALSE(bob.lost);
    } TEST_END();

    TEST_CASE("S3.57 A Room Keeps At Most As Many Connections As Its Setting Says, Whatever Comes Through The Door With A Key: With Room For Only The Three That Play, A Returning Player Is Told \"Full\" (The Room Is Not Grown, Nothing Leaks, The Seat Stays Held); With Room To Spare The Same Player Comes Back")  {
        {
            RWorld w;
            RoomSpec s = held_spec("L-1", 3);
            s.max_connections = 3;                                                                // exactly the connections of the lobby: nothing can be pruned
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            RClient& a = w.connect("Ann", "L-1");
            RClient& b = w.connect("Bob", "L-1");
            RClient& c = w.connect("Cat", "L-1");
            w.run(5000 + kPre);
            ASSERT_TRUE(w.status("L-1").state == RoomState::Running);
            ASSERT_EQ(w.status("L-1").connections, 3u);
            b.reconnects = true;
            w.cut(b);
            ASSERT_TRUE(w.until([&]() { return b.lost; }, 8000));
            ASSERT_TRUE(b.was_rejected && b.rejected == net::RejectReason::Full);                 // the answer, in words that say what it is
            const RoomStatus st = w.status("L-1");
            ASSERT_TRUE(st.state == RoomState::Running && st.paused && st.absent.size() == 1 && st.absent[0].seat == b.lobby->my_seat());      // the seat is held
            ASSERT_EQ(st.connections, 3u);                                                        // the room did not grow
            ASSERT_EQ(st.rejoins, 0u);
            ASSERT_FALSE(a.lost || c.lost);
        }
        {   // with room to spare (the default) the same cut is a return
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("L-2", 3), w.now).ok);
            w.connect("Ann", "L-2");
            RClient& b = w.connect("Bob", "L-2");
            w.connect("Cat", "L-2");
            w.run(5000);
            w.cut(b);
            ASSERT_TRUE(w.until([&]() { return w.status("L-2").rejoins == 1 && !w.status("L-2").paused; }, 8000));
            ASSERT_FALSE(b.lost);
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Bots fill the empty seats at the leader's START, bots in the room's specification, and chat in the waiting room (protocol 11, docs/NETWORK_PORT.md "Protocol 11")
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

// the result row of a seat
const RoomRow* row_of(const RoomStatus& s, uint8_t seat) {
    for (const RoomRow& r : s.rows) {
        if (r.first == seat) return &r;
    }
    return nullptr;
}

// the lines of a client's room chat as "seat|name|text" (a notice: 255 and no name)
std::vector<std::string> said(const std::vector<net::ChatLine>& lines) {
    std::vector<std::string> out;
    for (const net::ChatLine& l : lines) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
    return out;
}

}  // namespace

void run_bot_tests() {
    TEST_CASE("S3.60 The Leader's START With A Fill: One Person In A Room For Four Starts It With Bots In Exactly The Three Empty Seats, Named \"Bot (Medium)\" (The Person's Own Seat Is Left Alone), The Match Runs To Its End With The Bots Playing (Their Scores In The Rows), The Referee And The Client Stand At The Same State At The Same Tick, The Status Lists The Bots") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("FILL-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "FILL-1", 2);                                     // the person sits in seat 2: the bots take 0, 1 and 3
        ann.record_hashes = true;
        w.run(500);
        RoomStatus s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 1 && s.bots.empty() && ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(2000);
        s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(s.joined, 4);                                                           // a bot is a player
        ASSERT_EQ(s.expected, 4);
        ASSERT_TRUE(s.names[0] == "Bot (Medium)" && s.names[1] == "Bot (Medium)" && s.names[2] == "Ann" && s.names[3] == "Bot (Medium)");
        ASSERT_EQ(s.bots.size(), size_t{3});
        for (size_t i = 0; i < 3; ++i) {
            const uint8_t seat = i < 2 ? static_cast<uint8_t>(i) : uint8_t{3};
            ASSERT_TRUE(s.bots[i].seat == seat && s.bots[i].kind == "standard" && s.bots[i].level == "medium" && s.bots[i].name == "Bot (Medium)" && s.bots[i].fill);
        }
        ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);                                       // everybody plays: the roster of the Start has the bots' seats
        {
            const net::StartMsg& start = ann.lobby->start_info();                            // what every machine was told: the names of the seats, the bots' among them
            ASSERT_TRUE(start.names[0] == "Bot (Medium)" && start.names[1] == "Bot (Medium)" && start.names[2] == "Ann" && start.names[3] == "Bot (Medium)" && start.roster == 0x0F);
        }
        for (int guard = 0; guard < 4000 && w.status("FILL-1").state == RoomState::Running; ++guard) w.run(250);
        s = w.status("FILL-1");
        ASSERT_TRUE(s.state == RoomState::Finished);                                      // (a client that diverged from the referee would have failed the room)
        ASSERT_FALSE(ann.session->desynced());
        w.run(Room::kGraceMs + 500);
        ASSERT_EQ(s.rows.size(), size_t{4});
        for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
            const RoomRow* row = row_of(s, seat);
            ASSERT_TRUE(row != nullptr && row->name == "Bot (Medium)" && row->score > 300);       // the bots harvested: they played
        }
        ASSERT_TRUE(s.referee_hash != 0 && s.ticks > 1000);
        ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1);
        ASSERT_TRUE(ann.hash_at[s.ticks] == s.referee_hash);                              // the referee and the person's game, the same state at the same tick
        ASSERT_TRUE(ann.sim.is_match_over());
        const RoomStatus end = w.status("FILL-1");
        ASSERT_EQ(end.bots.size(), size_t{3});                                            // the seats stay listed after the match
    } TEST_END();

    TEST_CASE("S3.61 The Fill Only Happens On The Leader's Request: A Player Who Is Not The Leader Is Ignored, Fill None Is The START Of Protocol 7 (One Person Alone Does Nothing, Two Start Without Bots), A Room That Fills Up Starts By Itself With No Bots, A Room For Three Gets Two Bots (Never More Than The Room's Players), Easy And Hard Name Their Bots, A Room Without A Leader Ignores The Fill") {
        {   // not the leader
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "FR-1");
            Client& bob = w.connect("Bob", "FR-1");
            w.run(500);
            bob.end->send(net::encode(net::StartRequestMsg{net::FillLevel::Hard}));
            w.run(1500);
            RoomStatus s = w.status("FR-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 2 && s.ignored_start_requests == 1);
            ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::InRoom);
            // fill none, two people: today's early start, no bots
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            w.run(1500);
            s = w.status("FR-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2 && ann.sim.roster_mask() == 0x03);
        }
        {   // one person alone, fill none: nothing happens (the room needs two)
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-2", 4), w.now).ok);
            Client& ann = w.connect("Ann", "FR-2");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            w.run(1500);
            RoomStatus s = w.status("FR-2");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1 && s.ignored_start_requests == 1);
        }
        {   // a room that fills up starts by itself: no bots, whatever the leader once asked for
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-3", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-3");
            w.connect("Bob", "FR-3");
            w.run(2500);
            RoomStatus s = w.status("FR-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.empty());
            ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
        }
        {   // a full room that the leader's fill request finds already loading adds nothing: the request crossed the start
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-4", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-4");
            w.connect("Bob", "FR-4");
            w.run(500);
            ann.end->send(net::encode(net::StartRequestMsg{net::FillLevel::Medium}));
            w.run(2500);
            RoomStatus s = w.status("FR-4");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2);
        }
        {   // a room for three: one person and two bots, the empty seat beyond the room's players stays empty; the level names the bots
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-5", 3), w.now).ok);
            Client& ann = w.connect("Ann", "FR-5");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Easy));
            w.run(2000);
            RoomStatus s = w.status("FR-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 3 && s.expected == 3 && s.bots.size() == 2);
            ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Bot (Easy)" && s.names[2] == "Bot (Easy)" && s.names[3].empty());
            ASSERT_TRUE(s.bots[0].level == "easy" && s.bots[1].level == "easy" && ann.sim.roster_mask() == 0x07);
        }
        {   // hard
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("FR-6", 2), w.now).ok);
            Client& ann = w.connect("Ann", "FR-6");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
            w.run(2000);
            RoomStatus s = w.status("FR-6");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.bots.size() == 1 && s.bots[0].level == "hard" && s.names[1] == "Bot (Hard)");
        }
        {   // a room without a leader (early_start false) ignores every request, fill or not
            World w;
            RoomSpec spec = spec_of("FR-7", 4);
            spec.early_start = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "FR-7");
            w.run(500);
            ann.end->send(net::encode(net::StartRequestMsg{net::FillLevel::Medium}));
            w.run(1500);
            RoomStatus s = w.status("FR-7");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1 && s.ignored_start_requests == 1);
        }
    } TEST_END();

    TEST_CASE("S3.62 Bots And Fog Of War Never Mix: A Fill Request In A Room With Fog Is Refused And The Leader Is Told Why (A Notice From The Room, To The Leader Only); One Person Alone Stays In The Waiting Room, Two Start Without Bots; A Room Specification With Bots And Fog Is A 400") {
        {
            World w;
            RoomSpec spec = spec_of("FOG-1", 4);
            spec.fog = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "FOG-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
            w.run(1500);
            RoomStatus s = w.status("FOG-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillFog}));
            ASSERT_TRUE(ann.room_chat[0].notice());
            Client& bob = w.connect("Bob", "FOG-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                  // two people: the match goes on without bots, and the leader is told again
            w.run(2000);
            s = w.status("FOG-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.empty() && s.joined == 2 && ann.sim.is_fog_of_war_enabled());
            ASSERT_EQ(ann.room_chat.size(), size_t{2});
            ASSERT_TRUE(bob.room_chat.empty());                                              // the notice is the leader's alone
        }
        {
            World w;
            RoomSpec spec = spec_of("FOG-2", 4);
            spec.fog = true;
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            const CreateResult made = w.mgr.create_room(spec, w.now);
            ASSERT_TRUE(!made.ok && made.http_status == 400 && made.error.find("Fog") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.63 A Cancelled Start Takes The Fill Away Again: A Player Who Cannot Load The Map Cancels It, The Bots Go (The Room Is As It Was: The Status Lists None, Their Seats Are Free), The Next Player Who Comes Gets A Seat, And The Leader's New Request Seats Bots Again Round The Three People; The Pause After A Cancel Loses A Request") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("CAN-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "CAN-1");
        Client& bob = w.connect("Bob", "CAN-1");
        bob.fail_load = true;                                                              // Bob's copy of the map is no good
        w.run(500);
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(1000);
        RoomStatus s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Waiting);                                        // cancelled
        ASSERT_TRUE(s.bots.empty() && s.joined == 2 && s.names[2].empty() && s.names[3].empty());
        ASSERT_TRUE(ann.lobby->room().slots[2].state == net::SlotState::Empty && ann.lobby->room().slots[3].state == net::SlotState::Empty);      // the room that Ann sees has no bots
        Client& cat = w.connect("Cat", "CAN-1");                                           // a seat for the next player
        w.run(300);
        ASSERT_TRUE(cat.lobby->phase() == net::ClientLobby::Phase::InRoom && w.status("CAN-1").joined == 3);
        bob.fail_load = false;
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                    // inside the pause of two seconds: lost
        w.run(600);
        s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty());
        w.run(2500);
        ASSERT_TRUE(w.status("CAN-1").state == RoomState::Waiting);                       // (nobody asked again: three of four do not start by themselves)
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
        w.run(2000);
        s = w.status("CAN-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 1);
        ASSERT_TRUE(s.bots[0].seat == 3 && s.bots[0].fill && s.names[3] == "Bot (Medium)" && s.names[2] == "Cat");
        ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);
    } TEST_END();

    TEST_CASE("S3.64 A Map The Filled Roster Cannot Play: The Request Does Nothing But Tell The Leader (A Notice), The Room Does Not Fail And Seats No Bot; The Same Request Where The Bots Take Playable Seats Starts") {
        const std::string dir = temp_dir_for("fill_marker");
        {   // TINY with the green start marker moved outside the grid (as in S3.14 and S3.29)
            std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assets::LevelData tiny;
            ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
            const assets::AnthillSpawn* green = nullptr;
            for (const auto& sp : tiny.anthill_spawns) {
                if (sp.tile_id == 154) green = &sp;
            }
            ASSERT_TRUE(green != nullptr);
            const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
            size_t at = bytes.size();
            for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
            }
            ASSERT_TRUE(at < bytes.size());
            bytes[at + 2] = 200;
            bytes[at + 3] = 0;
            std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const auto run_in = [&dir](World& w, uint32_t ms) {
            for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                w.now += 10;
                w.net.set_time(w.now);
                w.mgr.update(w.now);
                for (auto& c : w.clients) c->update(w.now, dir);
            }
        };
        {   // Ann in seat 1 (red), a room for three: the lowest empty seats are 0 (green) and 2: the filled roster has green, which this map cannot play
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MAP-1", 3, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MAP-1", 1);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
            run_in(w, 2000);
            RoomStatus s = w.status("MAP-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.bots.empty() && s.joined == 1);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillMap}));
            ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);
            ASSERT_TRUE(ann.lobby->room().slots[0].state == net::SlotState::Empty);        // (no bot appeared and went away: the room never had one)
        }
        {   // Ann in seat 0 would be the green that cannot play: nothing to do with the fill, the room is for 3 with a bot at red and blue: that roster has green: also refused
            // Ann in seat 1 (red) and Bob in seat 2 (blue) start with no bots: the map is playable for them (S3.29's second case); a fill for them changes nothing
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("MAP-2", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "MAP-2", 1);
            w.connect("Bob", "MAP-2", 2);
            run_in(w, 500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));                 // the empty seats are 0 and 3: green again
            run_in(w, 1500);
            ASSERT_TRUE(w.status("MAP-2").state == RoomState::Waiting && w.status("MAP-2").bots.empty());
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));                   // without the fill the same two start
            run_in(w, 2000);
            ASSERT_TRUE(w.status("MAP-2").state == RoomState::Running && w.status("MAP-2").bots.empty());
        }
        std::error_code ignore;
        fs::remove_all(dir, ignore);
    } TEST_END();

    TEST_CASE("S3.65 The Control Interface Seats Bots In A Room's Specification (\"bots\": [{\"seat\": 2, \"bot\": \"medium\"}]): The Status JSON Lists Them Next To The Players, A Mistake Is A 400 That Names The Key, A Person Who Joins Gets Another Seat, The Room Starts By Itself When The Person Has Come (The Bots Count As Players) And Plays To Its End; The Referee's Final Hash Is In The JSON") {
        World w;
        const auto call = [&w](const char* method, const std::string& path, const std::string& body = std::string()) {
            ctl::HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.body = body;
            return handle_control(w.mgr, rq, w.now);
        };
        const auto json_of = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ctl::HttpResponse r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":2,"code":"CB-1","seed":9,"bots":[{"seat":0,"bot":"medium"}]})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = json_of(r);
        ASSERT_TRUE(v.get("bots").is_array() && v.get("bots").size() == 1);
        const ctl::JsonValue& b = v.get("bots").at(0);
        ASSERT_TRUE(b.get("seat").as_int_or(9) == 0 && b.get("bot").str() == "medium" && b.get("kind").str() == "standard" && b.get("level").str() == "medium" &&
                    b.get("name").str() == "Bot (Medium)" && !b.get("fill").as_bool_or(true));
        ASSERT_TRUE(v.get("joined").as_int_or(0) == 1 && v.get("expected").as_int_or(0) == 2);
        ASSERT_TRUE(v.get("players").size() == 1 && v.get("players").at(0).get("name").str() == "Bot (Medium)" && v.get("players").at(0).get("bot").as_bool_or(false));      // (a bot is a player of the room, and the entry says it is a bot)
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","code":"CB-2"})");                  // no bots: an empty list in the JSON
        ASSERT_TRUE(r.status == 201 && json_of(r).get("bots").is_array() && json_of(r).get("bots").size() == 0);
        // the mistakes: each one a 400 that names the key
        const char* bad[] = {R"({"map":"TINY.LVL","bots":"medium"})", R"({"map":"TINY.LVL","bots":[1]})", R"({"map":"TINY.LVL","bots":[{"seat":2}]})", R"({"map":"TINY.LVL","bots":[{"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":4,"bot":"easy"}]})", R"({"map":"TINY.LVL","bots":[{"seat":-1,"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":"1","bot":"easy"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":7}]})",
                             R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"expert"}]})", R"({"map":"TINY.LVL","players":4,"bots":[{"seat":1,"bot":"easy"},{"seat":1,"bot":"hard"}]})",           // a seat twice (with room for a person, so that only this is wrong)
                             R"({"map":"TINY.LVL","players":2,"bots":[{"seat":1,"bot":"easy"},{"seat":2,"bot":"easy"}]})",      // no seat left for a person
                             R"({"map":"TINY.LVL","players":3,"bots":[{"seat":0,"bot":"easy"},{"seat":1,"bot":"easy"},{"seat":2,"bot":"easy"}]})",
                             R"({"map":"TINY.LVL","fog":true,"bots":[{"seat":1,"bot":"easy"}]})"};
        for (const char* body : bad) {
            r = call("POST", "/rooms", body);
            ASSERT_EQ(r.status, 400);
            ASSERT_TRUE(json_of(r).get("error").str().find("bot") != std::string::npos || json_of(r).get("error").str().find("Bot") != std::string::npos);
        }
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"CB-3","bots":[{"seat":3,"bot":"hard"},{"seat":1,"bot":"worker:easy"},{"seat":2,"bot":"idle"}]})");
        ASSERT_EQ(r.status, 201);                                                          // three bots and one person; kinds and levels as --bot takes them
        v = json_of(r);
        ASSERT_TRUE(v.get("bots").size() == 3 && v.get("bots").at(0).get("seat").as_int_or(9) == 1 && v.get("bots").at(0).get("bot").str() == "worker:easy" &&
                    v.get("bots").at(1).get("bot").str() == "idle:medium" && v.get("bots").at(2).get("bot").str() == "hard");     // (listed by seat)
        ASSERT_TRUE(v.get("bots").at(2).get("style").str() == "random");                   // (a standard bot without a pinned style draws its own at the start of the match)
        ASSERT_TRUE(!v.get("bots").at(0).has("style") && !v.get("bots").at(1).has("style"));       // (the worker and the idle bot have no style: the key is absent, it does not say "random")
        // a pinned style (docs/BOTS.md, "Styles"): the text is what --bot takes, the style is listed, the name of the seat is still "Bot (Level)"; a style that the level may not play is refused
        r = call("POST", "/rooms", R"({"map":"TINY.LVL","players":4,"code":"CB-3S","bots":[{"seat":3,"bot":"hard:raider"},{"seat":1,"bot":"standard:medium:defensive"}]})");
        ASSERT_EQ(r.status, 201);
        v = json_of(r);
        ASSERT_TRUE(v.get("bots").size() == 2 && v.get("bots").at(0).get("seat").as_int_or(9) == 1 && v.get("bots").at(0).get("bot").str() == "medium:defensive" && v.get("bots").at(0).get("style").str() == "defensive" &&
                    v.get("bots").at(0).get("level").str() == "medium" && v.get("bots").at(0).get("name").str() == "Bot (Medium)" && v.get("bots").at(1).get("bot").str() == "hard:raider" &&
                    v.get("bots").at(1).get("style").str() == "raider" && v.get("bots").at(1).get("name").str() == "Bot (Hard)");
        for (const char* body : {R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"hard:economic"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"worker:easy:raider"}]})", R"({"map":"TINY.LVL","bots":[{"seat":1,"bot":"medium:wild"}]})"}) {
            r = call("POST", "/rooms", body);
            ASSERT_EQ(r.status, 400);
            ASSERT_TRUE(json_of(r).get("error").str().find("style") != std::string::npos);
        }
        // a person joins CB-1 (a room for two with a bot at seat 0): it gets seat 1, and the room is full: it starts by itself
        Client& ann = w.connect("Ann", "CB-1", 0);                                         // (it asks for seat 0, which the bot has: the first free seat)
        ann.record_hashes = true;
        w.run(2500);
        RoomStatus s = w.status("CB-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && ann.lobby->my_seat() == 1);
        ASSERT_TRUE(s.names[0] == "Bot (Medium)" && s.names[1] == "Ann" && s.bots.size() == 1 && !s.bots[0].fill);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
        for (int guard = 0; guard < 4000 && w.status("CB-1").state == RoomState::Running; ++guard) w.run(250);
        w.run(Room::kGraceMs + 500);
        r = call("GET", "/rooms/CB-1");
        v = json_of(r);
        ASSERT_EQ(v.get("state").str(), std::string("finished"));
        ASSERT_TRUE(v.get("bots").size() == 1 && v.get("result").get("rows").size() == 2);
        ASSERT_TRUE(v.get("players").size() == 2 && v.get("players").at(0).get("bot").as_bool_or(false) && !v.get("players").at(1).get("bot").as_bool_or(true) && v.get("players").at(1).get("name").str() == "Ann");
        const std::string hex = v.get("state_hash").str();
        ASSERT_EQ(hex.size(), size_t{16});
        s = w.status("CB-1");
        char expected[17];
        std::snprintf(expected, sizeof(expected), "%016llx", static_cast<unsigned long long>(ann.hash_at[s.ticks]));
        ASSERT_EQ(hex, std::string(expected));                                             // the JSON says what the person's game stands at, at the end
        ASSERT_TRUE(row_of(s, 0) != nullptr && row_of(s, 0)->score > 300);
        // the list shows bots too
        r = call("GET", "/rooms");
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 4);               // (CB-1, CB-2, CB-3 and the room with the pinned styles)
    } TEST_END();

    TEST_CASE("S3.66 A Room's Own Bots: The Early Start With Them (One Person And The Bot Of A Room For Three Start At The Leader's Request, No Fill Needed), And A Fill On Top Of Them Seats Only The Seats That Are Still Empty") {
        World w;
        RoomSpec spec = spec_of("CB-4", 3);
        spec.bots = {ai::BotSpec{2, "standard", ai::Level::Hard}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& ann = w.connect("Ann", "CB-4");
        w.run(500);
        RoomStatus s = w.status("CB-4");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.bots.size() == 1 && ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));                      // one person and the room's bot are two players
        w.run(2000);
        s = w.status("CB-4");
        ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2 && s.names[2] == "Bot (Hard)" && s.bots.size() == 1 && !s.bots[0].fill);
        ASSERT_TRUE(ann.sim.roster_mask() == 0x05);
        {   // a room for four with a bot at seat 1: the fill takes seats 2 and 3 (Ann has 0), not seat 1
            World v;
            RoomSpec four = spec_of("CB-5", 4);
            four.bots = {ai::BotSpec{1, "standard", ai::Level::Easy}};
            ASSERT_TRUE(v.mgr.create_room(four, v.now).ok);
            Client& bob = v.connect("Bob", "CB-5", 0);
            v.run(500);
            ASSERT_TRUE(bob.lobby->request_start(net::FillLevel::Hard));
            v.run(2000);
            s = v.status("CB-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && s.bots.size() == 3);
            ASSERT_TRUE(s.names[1] == "Bot (Easy)" && s.names[2] == "Bot (Hard)" && s.names[3] == "Bot (Hard)");
            ASSERT_TRUE(!s.bots[0].fill && s.bots[1].fill && s.bots[2].fill);
        }
    } TEST_END();

    TEST_CASE("S3.67 Bot Seats In A Room That Holds Seats: A Bot Is Never Absent And Never Votes (Two People And Two Bots: The Person Who Is Cut Is The Only One Missing, The Vote Has ONE Voter, The Survivor's Vote Drops The Absent Person And The Bots Play On To The End, The Referee And The Survivor Agree); A Person Who Comes Back Through The Door Finds The Bots Where They Were; The Bots' Commands Wait While The Match Is Paused") {
        {
            RWorld w;
            RoomSpec spec = held_spec("BH-1", 4);
            spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}, ai::BotSpec{3, "standard", ai::Level::Easy}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BH-1", 0);
            RClient& bob = w.connect("Bob", "BH-1", 1);
            ann.record_hashes = true;
            w.run(6000 + kPre);                                                                     // the room is full (two people and two bots): it started by itself
            ASSERT_TRUE(w.status("BH-1").state == RoomState::Running);
            ASSERT_TRUE(ann.sim.roster_mask() == 0x0F);
            const uint8_t bob_seat = bob.lobby->my_seat();
            bob.reconnects = false;
            w.cut(bob);
            w.run(1500);
            RoomStatus s = w.status("BH-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == bob_seat && s.absent[0].name == "Bob");        // only the person is missing: a bot has no connection to lose
            ASSERT_EQ(s.bots.size(), size_t{2});
            const uint32_t ticks_at_pause = s.ticks;
            w.run(10000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.ticks <= ticks_at_pause + 2);                                      // nothing runs, the bots included
            ASSERT_TRUE(ann.session->presence().missing.size() == 1 && ann.session->presence().missing[0].seat == bob_seat);
            w.run(21000);                                                                    // 30 s away in all: the vote opens
            s = w.status("BH-1");
            ASSERT_TRUE(s.paused && s.vote_seat == bob_seat);
            ASSERT_EQ(s.voters, 1);                                                          // Ann alone: the bots do not vote, and are not counted as connected
            ASSERT_EQ(ann.session->presence().voters, 1);
            ASSERT_TRUE(ann.session->vote(bob_seat, true));                                  // more than half of ONE
            w.run(3000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.drops_by_vote == 1);
            const uint32_t after_vote = s.ticks;
            w.run(10000);
            s = w.status("BH-1");
            ASSERT_TRUE(s.ticks > after_vote + 150);                                         // the match runs again, with the bots
            for (int guard = 0; guard < 4000 && w.status("BH-1").state == RoomState::Running; ++guard) w.run(250);
            w.run(Room::kGraceMs + 500);
            s = w.status("BH-1");
            ASSERT_TRUE(s.state == RoomState::Finished);
            ASSERT_TRUE(row_of(s, 2) != nullptr && row_of(s, 2)->score > 100 && row_of(s, 3) != nullptr && row_of(s, 3)->score > 100);       // the bots kept harvesting
            ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1 && ann.hash_at[s.ticks] == s.referee_hash);
            ASSERT_FALSE(ann.session->desynced());
        }
        {   // the person comes back with its key: the bots are where they were, and everybody ends identical
            RWorld w;
            RoomSpec spec = held_spec("BH-2", 3);
            spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BH-2", 0);
            RClient& bob = w.connect("Bob", "BH-2", 1);
            ann.record_hashes = true;
            bob.record_hashes = true;
            w.run(6000);
            ASSERT_TRUE(w.status("BH-2").state == RoomState::Running);
            bob.reconnects = false;
            w.cut(bob);
            w.run(4000);
            ASSERT_TRUE(w.status("BH-2").paused);
            const uint32_t ticks_at_pause = w.status("BH-2").ticks;
            bob.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("BH-2").paused; }, 8000));
            w.run(3000);
            RoomStatus s = w.status("BH-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty() && s.ticks > ticks_at_pause + 40);
            for (int guard = 0; guard < 4000 && w.status("BH-2").state == RoomState::Running; ++guard) w.run(250);
            w.run(Room::kGraceMs + 500);
            s = w.status("BH-2");
            ASSERT_TRUE(s.state == RoomState::Finished && s.rejoins == 1);
            ASSERT_TRUE(ann.hash_at.count(s.ticks) == 1 && ann.hash_at[s.ticks] == s.referee_hash);
            ASSERT_TRUE(bob.hash_at.count(s.ticks) == 1 && bob.hash_at[s.ticks] == s.referee_hash);       // the one that was away, too
            ASSERT_TRUE(row_of(s, 2) != nullptr && row_of(s, 2)->score > 100);
            ASSERT_FALSE(ann.lost || bob.lost);
        }
    } TEST_END();

    TEST_CASE("S3.68 Chat In A Server's Waiting Room: Everybody Hears A Line With The Sender's Seat And Name (The Sender Too), A Player Who Comes Later Hears Only What Is Said After It Came, Chat Works While The Map Loads (A Slow Link Keeps The Room In Loading), A Flood Of Lines Costs The Sender Its Seat And Nobody Else Notices, And The Lines Are Kept For The Match's Log After The Start") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("CHAT-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "CHAT-1");
        Client& bob = w.connect("Bob", "CHAT-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->chat("hello Bob"));
        w.run(200);
        ASSERT_TRUE(bob.lobby->chat("hello Ann"));
        w.run(200);
        ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{"0|Ann|hello Bob", "1|Bob|hello Ann"}));
        ASSERT_EQ(said(bob.room_chat), said(ann.room_chat));
        Client& cat = w.connect("Cat", "CHAT-1");
        w.run(500);
        ASSERT_TRUE(cat.room_chat.empty());                                                  // what was said before Cat came is not for Cat
        ASSERT_TRUE(ann.lobby->chat("welcome Cat"));
        w.run(200);
        ASSERT_EQ(said(cat.room_chat), (std::vector<std::string>{"0|Ann|welcome Cat"}));
        ASSERT_EQ(said(bob.room_chat).size(), size_t{3});
        // a flood: 1500 lines at once from Bob; the chat budget (5 lines, then one a second) lets 5 through and then it is a violation each: he is out (BadRequest) at once
        net::ChatMsg spam;
        spam.text = "spam spam spam";
        const std::vector<uint8_t> bytes = net::encode(spam);
        for (int i = 0; i < 1500; ++i) bob.end->send(bytes);
        w.run(1500);
        ASSERT_TRUE(bob.lobby->phase() == net::ClientLobby::Phase::Rejected && bob.lobby->reject_reason() == net::RejectReason::BadRequest);
        RoomStatus s = w.status("CHAT-1");
        ASSERT_TRUE(s.state == RoomState::Waiting && s.joined == 2 && s.names[1].empty());
        ASSERT_TRUE(ann.lobby->phase() == net::ClientLobby::Phase::InRoom && cat.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_EQ(ann.room_chat.size(), size_t{3 + net::kChatBurst - 1});                    // what was said before, and what was left of Bob's burst of 5 (he had said one line); not 1000, not 1500
        // chat while loading: Dan is on a slow link (400 ms each way), so the room waits for his Loaded for a second after the Start
        Client& dan = w.connect("Dan", "CHAT-1", 255, {400, 0});
        w.run(1500);
        ASSERT_TRUE(ann.lobby->is_leader());
        const size_t before = ann.room_chat.size();
        ASSERT_TRUE(ann.lobby->request_start());
        w.run(100);                                                                          // the Start has gone out, Dan has not got it yet
        ASSERT_TRUE(w.status("CHAT-1").state == RoomState::Loading);
        ASSERT_TRUE(cat.lobby->chat("loading, loading"));
        w.run(200);
        ASSERT_EQ(ann.room_chat.size(), before + 1);
        ASSERT_EQ(ann.room_chat.back().text, std::string("loading, loading"));
        ASSERT_EQ(ann.room_chat.back().name, std::string("Cat"));
        w.run(3000);
        ASSERT_TRUE(w.status("CHAT-1").state == RoomState::Running);
        ASSERT_TRUE(!dan.room_chat.empty() && dan.room_chat.back().text == "loading, loading" && dan.room_chat.back().name == "Cat");      // Dan, on his slow link, heard it too (it came behind his Start)
        // the lines are still there when the match has begun (the application starts the match's chat log with them)
        ASSERT_TRUE(ann.lobby->chat_log().back().text == "loading, loading");
        ASSERT_EQ(ann.lobby->chat_log().size(), size_t{3 + net::kChatBurst - 1 + 1});        // (every line of the room is kept: the three, what the flood got through, the one of the loading room)
        // and the chat of the match is the match's: it works as ever
        ASSERT_TRUE(ann.session != nullptr && cat.session != nullptr);
        ASSERT_TRUE(ann.session->chat("in the match", false));
        w.run(500);
        bool heard = false;
        for (const net::ChatMsg& m : cat.chats) heard = heard || (m.text == "in the match" && m.sender == ann.lobby->my_seat());
        ASSERT_TRUE(heard);
    } TEST_END();

    TEST_CASE("S3.69 The Door Of Protocol 11: A Hello Of Protocol 10 Is Answered VersionMismatch (The Layout Is The Same, The Messages Are Not: A One-Byte StartRequest Of Protocol 10 Is Garbage Now); A Leader's Old One-Byte Request Costs A Violation Each Time, Eight Throw It Out") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("V11-1", 4), w.now).ok);
        {
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.version = 10;
            hello.name = "Old";
            hello.room = "V11-1";
            ends.second->send(net::encode(hello));
            w.run(300);
            ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
        }
        ASSERT_EQ(w.status("V11-1").joined, 0);
        Client& ann = w.connect("Ann", "V11-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader());
        for (int i = 0; i < 7; ++i) ann.end->send({static_cast<uint8_t>(net::MsgType::StartRequest)});       // protocol 10's request: one byte
        w.run(500);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::InRoom);                                       // seven are not enough
        ann.end->send({static_cast<uint8_t>(net::MsgType::StartRequest)});
        w.run(500);
        ASSERT_EQ(ann.lobby->phase(), net::ClientLobby::Phase::Rejected);                                     // the eighth
        ASSERT_EQ(w.status("V11-1").ignored_start_requests, 0u);                                              // (garbage is no request)
    } TEST_END();

    TEST_CASE("S3.70 Over Real Sockets: A Person Alone Starts A Room For Four With A Fill (Bots In The Three Other Seats), Two Players Chat In The Waiting Room First; The Match Is Played, Both Clients Stand At The Referee's State, The Status Lists The Bots") {
        RoomManager mgr{MapStore(maps_dir())};
        auto listener = net::TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        uint32_t now = 1000;
        ASSERT_TRUE(mgr.create_room(spec_of("SOCK-5", 4), now).ok);
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<net::TcpConnection>> links;
        for (const char* name : {"Ann", "Bob"}) {
            links.push_back(net::TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(links.back() != nullptr);
            clients.push_back(std::make_unique<Client>());
            clients.back()->name = name;
            clients.back()->room = "SOCK-5";
            clients.back()->record_hashes = true;
            clients.back()->start(links.back().get(), 77u);
        }
        const auto pump = [&]() {
            now += 10;
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
            mgr.update(now);
            for (auto& c : clients) c->update(now, maps_dir());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        };
        for (int i = 0; i < 3000; ++i) {
            pump();
            RoomStatus s;
            mgr.status("SOCK-5", s, now);
            if (s.joined == 2 && clients[0]->lobby->room().slots[1].state == net::SlotState::Client && clients[1]->lobby->room().slots[0].state == net::SlotState::Client) break;
        }
        ASSERT_TRUE(clients[0]->lobby->chat("hello over TCP"));
        for (int i = 0; i < 3000 && clients[1]->room_chat.empty(); ++i) pump();                // (Bob answers after it has heard Ann: two lines sent at the same moment over two sockets arrive in either order)
        ASSERT_TRUE(clients[1]->lobby->chat("and back"));
        for (int i = 0; i < 400; ++i) pump();
        ASSERT_EQ(said(clients[0]->room_chat), (std::vector<std::string>{"0|Ann|hello over TCP", "1|Bob|and back"}));
        ASSERT_EQ(said(clients[1]->room_chat), said(clients[0]->room_chat));
        ASSERT_TRUE(clients[0]->lobby->request_start(net::FillLevel::Hard));
        RoomStatus s;
        for (int i = 0; i < 6000; ++i) {
            pump();
            mgr.status("SOCK-5", s, now);
            if (s.state == RoomState::Running && s.ticks > 400) break;
        }
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 400 && s.joined == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[1].seat == 3 && s.bots[0].level == "hard" && s.names[2] == "Bot (Hard)" && s.names[3] == "Bot (Hard)");
        ASSERT_TRUE(clients[0]->sim.roster_mask() == 0x0F && clients[1]->sim.roster_mask() == 0x0F);
        bool compared = false;
        for (int i = 0; i < 400 && !compared; ++i) {
            pump();
            if (clients[0]->sim.current_tick() == clients[1]->sim.current_tick()) {
                ASSERT_TRUE(clients[0]->sim.state_hash() == clients[1]->sim.state_hash());
                compared = true;
            }
        }
        ASSERT_TRUE(compared);
        ASSERT_FALSE(clients[0]->session->desynced() || clients[1]->session->desynced());
        ASSERT_TRUE(clients[0]->lobby->chat_log().size() == 2);                              // the waiting room's lines are still there
    } TEST_END();

    TEST_CASE("S3.71 Server CPU With Bots (Measured): Twelve Rooms Of One Person And Three Bots Each Cost The Server's Thread A Few Milliseconds A Second (Only mgr.update Is Timed, As The Thread's CPU Time: The Clients' Work Is Not The Server's, A Busy Machine Is Not Either), On TINY And On TREASURE, With Idle, Medium And Hard Bots; The Start Of Twelve Rooms Is Not A Stall (Each Room Analyses Its Map Once)") {
        // the clock of the measurement: it works, it does not run while the thread sleeps (a wall clock would read the whole 60 ms) and it runs while the thread works
        const double cpu_start = thread_cpu_ms();
        ASSERT_TRUE(cpu_start >= 0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        ASSERT_TRUE(thread_cpu_ms() - cpu_start < 30.0);
        volatile uint64_t spin = 0;
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (thread_cpu_ms() - cpu_start < 30.0 && std::chrono::steady_clock::now() < give_up) spin = spin + 1;
        static_cast<void>(spin);
        ASSERT_TRUE(thread_cpu_ms() - cpu_start >= 30.0);
        struct Result {
            double ms_per_second{0};
            double worst_pass_ms{0};
            double start_pass_ms{0};
        };
        // twelve rooms, each with one person (a client that asks for its seat) and bots in the other three seats: through the leader's fill (`fill` set) or the room's specification
        const auto measure = [&](const char* map, const char* kind, ai::Level level, bool via_fill, Result& out) {
            World w;
            std::vector<Client*> leaders;
            std::vector<std::string> codes;
            for (int r = 0; r < 12; ++r) {
                codes.push_back("CPU-" + std::to_string(r));
                RoomSpec spec = spec_of(codes.back(), 4, map);
                if (!via_fill) {
                    for (uint8_t seat = 1; seat < 4; ++seat) spec.bots.push_back(ai::BotSpec{seat, kind, level});
                }
                if (!w.mgr.create_room(spec, w.now).ok) return false;
                leaders.push_back(&w.connect("P" + std::to_string(r), codes.back(), 0));
            }
            const auto pass = [&](double& timed_ms) {
                w.now += 10;
                w.net.set_time(w.now);
                const double cpu0 = thread_cpu_ms();                                          // the thread's CPU time, not the wall clock: a loaded machine delays the thread, it does not make the pass cost more
                w.mgr.update(w.now);
                timed_ms = thread_cpu_ms() - cpu0;
                for (auto& c : w.clients) c->update(w.now, maps_dir());
            };
            double ms = 0;
            for (int i = 0; i < 100; ++i) pass(ms);
            if (via_fill) {
                for (Client* c : leaders) c->lobby->request_start(net::FillLevel::Medium);
            }
            out.start_pass_ms = 0;
            for (int i = 0; i < 600; ++i) {                                                  // six seconds: every room starts, loads, begins
                pass(ms);
                out.start_pass_ms = std::max(out.start_pass_ms, ms);
            }
            for (const std::string& code : codes) {
                if (w.status(code).state != RoomState::Running) return false;
            }
            double total = 0;
            const int passes = 3000;                                                         // 30 s of match in all twelve rooms
            for (int i = 0; i < passes; ++i) {
                pass(ms);
                total += ms;
                out.worst_pass_ms = std::max(out.worst_pass_ms, ms);
            }
            out.ms_per_second = total / 30.0;
            return true;
        };
        // A build with AddressSanitizer or UBSan runs the server 10 to 30 times slower than the program: its figures say nothing about the server, so such a build plays the two TINY rows (twelve
        // rooms with bots, memory and undefined behaviour are what it checks) and puts no bound on the time
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
        constexpr bool kSanitized = true;
#else
        constexpr bool kSanitized = false;
#endif
#else
        constexpr bool kSanitized = false;
#endif
        Result tiny_fill, tiny_idle, tiny_medium, tiny_hard, treasure_idle, treasure_hard;
        ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Medium, true, tiny_fill));
        ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Medium, false, tiny_medium));
        if (!kSanitized) {
            ASSERT_TRUE(measure("TINY.LVL", "idle", ai::Level::Medium, false, tiny_idle));
            ASSERT_TRUE(measure("TINY.LVL", "standard", ai::Level::Hard, false, tiny_hard));
            ASSERT_TRUE(measure("TREASURE.LVL", "idle", ai::Level::Medium, false, treasure_idle));
            ASSERT_TRUE(measure("TREASURE.LVL", "standard", ai::Level::Hard, false, treasure_hard));
        }
        const auto row = [](const char* what, const Result& r) {
            std::cout << "\n    " << std::left << std::setw(34) << what << std::right << std::fixed << std::setprecision(2) << std::setw(7) << r.ms_per_second << " ms per second of play for 12 rooms ("
                      << std::setw(5) << r.ms_per_second / 12.0 << " per room), worst pass " << std::setw(6) << r.worst_pass_ms << " ms, worst pass while starting " << std::setw(6) << r.start_pass_ms << " ms";
        };
        std::cout << "\n    [12 rooms, one person and three bots in each; only the server's own thread is timed" << (kSanitized ? "; a sanitizer build: no figure here is the server's, nothing is bounded" : "") << "]";
        row("TINY, the leader's fill, medium", tiny_fill);
        row("TINY, medium bots", tiny_medium);
        if (!kSanitized) {
            row("TINY, idle bots (the controller)", tiny_idle);
            row("TINY, hard bots", tiny_hard);
            row("TREASURE, idle bots", treasure_idle);
            row("TREASURE, hard bots", treasure_hard);
        }
        std::cout << "\n    ";
        if (kSanitized) return;
        for (const Result* r : {&tiny_fill, &tiny_idle, &tiny_medium, &tiny_hard, &treasure_idle, &treasure_hard}) {
            ASSERT_TRUE(r->ms_per_second < 100.0);                                           // a tenth of the thread's time (1000 ms of it pass every second): the rooms are far from it
            ASSERT_TRUE(r->worst_pass_ms < 250.0);                                           // no pass is a stall (the machine of a test run is shared and busy: the number is for the record, the bound is wide)
            ASSERT_TRUE(r->start_pass_ms < 250.0);                                           // twelve rooms that start in a few passes: a hitch, not a stall
        }
    } TEST_END();

    TEST_CASE("S3.72 Team Chat On A Real Server (The Referee's Relay): In A Match Of Three, A Line For The Team Reaches The Sender And Its Ally Only, Measured On The Link Of A Raw Client That Reads Every Byte Of It (It Gets No Team Line, Not Even One That Its Own Screen Would Have Dropped); A Line For All Reaches It; A Broken Alliance Stops The Delivery At Once; The Waiting Room Before The Match Has No Teams And Tells Everybody") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("TEAM-1", 4), w.now).ok);                      // (a room for four, started by its leader with the three who are there)
        Client& ann = w.connect("Ann", "TEAM-1");
        Client& bob = w.connect("Bob", "TEAM-1");
        Client& cat = w.connect("Cat", "TEAM-1");
        w.run(500);
        ASSERT_TRUE(ann.lobby->chat("before the match"));                                    // the waiting room: everybody, whatever the clients would call their teams
        w.run(300);
        for (Client* c : {&ann, &bob, &cat}) ASSERT_TRUE(!c->room_chat.empty() && c->room_chat.back().text == "before the match");
        ASSERT_TRUE(ann.lobby->is_leader() && ann.lobby->request_start());
        w.run(3000 + kPre);
        ASSERT_TRUE(w.status("TEAM-1").state == RoomState::Running);
        ASSERT_TRUE(ann.session && bob.session && cat.session);
        cat.freeze = true;                                                                   // Cat's session stops: nothing filters, nothing acknowledges; the test reads its link
        std::vector<net::ChatMsg> raw;
        const auto pump = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t += 10) {
                w.run(10);
                std::vector<uint8_t> msg;
                while (cat.end->poll(msg)) {
                    net::ChatMsg c;
                    if (net::peek_type(msg) == net::MsgType::Chat && net::decode(msg, c)) raw.push_back(c);
                }
            }
        };
        const uint8_t a = ann.lobby->my_seat();
        const uint8_t b = bob.lobby->my_seat();
        const auto command = [](sim::CommandType type, uint8_t issuer, uint8_t other) {
            sim::Command c;
            c.type = type;
            c.issuer = issuer;
            c.other_player = other;
            return c;
        };
        const auto has = [](const std::vector<net::ChatMsg>& v, const char* text) {
            for (const net::ChatMsg& c : v) {
                if (c.text == text) return true;
            }
            return false;
        };
        pump(500);
        ASSERT_TRUE(ann.session->submit(command(sim::CommandType::AllianceInvite, a, b)));
        pump(800);
        ASSERT_TRUE(bob.session->submit(command(sim::CommandType::AllianceAccept, b, a)));
        pump(800);
        ASSERT_TRUE(ann.sim.alliance_of(a) == b && bob.sim.alliance_of(b) == a);              // allies, by the clients' own engines (the referee's is the same state)
        ann.next_order_ms = bob.next_order_ms = UINT32_MAX;                                  // (no more random orders: the test's own commands only)
        raw.clear();
        ASSERT_TRUE(ann.session->chat("team talk", true));
        pump(500);
        ASSERT_TRUE(has(ann.chats, "team talk") && has(bob.chats, "team talk"));
        ASSERT_TRUE(raw.empty());                                                            // not one team line on the raw client's link
        ASSERT_TRUE(bob.session->chat("team reply", true));
        pump(500);
        ASSERT_TRUE(has(ann.chats, "team reply") && has(bob.chats, "team reply"));
        ASSERT_TRUE(raw.empty());
        ASSERT_TRUE(bob.session->chat("to everybody", false));                               // a line for all reaches the raw client
        pump(500);
        ASSERT_TRUE(has(ann.chats, "to everybody") && has(bob.chats, "to everybody"));
        ASSERT_TRUE(raw.size() == 1 && raw[0].text == "to everybody" && raw[0].sender == b && !raw[0].team);
        // a broken alliance: the next team line of the former ally does not reach the one who left the team
        ASSERT_TRUE(ann.session->submit(command(sim::CommandType::AllianceBreak, a, 255)));
        for (int i = 0; i < 300 && (ann.sim.alliance_of(a) != sim::ALLIANCE_NONE || bob.sim.alliance_of(b) != sim::ALLIANCE_NONE); ++i) pump(10);
        ASSERT_TRUE(ann.sim.alliance_of(a) == sim::ALLIANCE_NONE && bob.sim.alliance_of(b) == sim::ALLIANCE_NONE);
        const size_t ann_heard = ann.chats.size();
        ASSERT_TRUE(bob.session->chat("late team line", true));
        pump(500);
        ASSERT_TRUE(has(bob.chats, "late team line"));                                       // Bob hears itself
        ASSERT_EQ(ann.chats.size(), ann_heard);                                              // Ann nothing
        ASSERT_EQ(raw.size(), size_t{1});                                                    // and the raw client still only the line for all
    } TEST_END();

    TEST_CASE("S3.73 The Door Tells A Hello Of Another Protocol Before It Looks For The Room (A Hello Of Protocol 10, 11 (The Release Before The Match Clock Waited For The Start Dialog) Or 13 For A Room That Does Not Exist Is VersionMismatch, Not NoSuchRoom; The Right Protocol Is NoSuchRoom); A Room Without Bots Builds No Bot Controller (The \"No Bot Code\" Rule), A Room With A Bot Seat Or A Fill Does; The Map Notice Of A Fill Waits For The Pause After A Cancelled Start To End; A Vote That No Person Can Cast (Everybody Who Is Left Is A Bot) Is No Vote In The Status JSON") {
        {   // the door's own check of the protocol, for a code that no room has
            World w;
            ASSERT_EQ(net::kProtocolVersion, uint16_t{12});                  // (11 was the protocol of v0.1.0 and v0.1.1: a client of it counts its dialog in simulation ticks, which a host that seals its first turn 5 s late would block for 100 ticks of the running match)
            for (const uint16_t version : {uint16_t{10}, uint16_t{11}, uint16_t{13}, uint16_t{1}, uint16_t{0}}) {
                auto ends = w.net.connect({20, 10});
                w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
                net::HelloMsg hello;
                hello.version = version;
                hello.name = "Old";
                hello.room = "NO-SUCH-ROOM";
                ends.second->send(net::encode(hello));
                w.run(300);
                ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::VersionMismatch));
            }
            auto ends = w.net.connect({20, 10});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            net::HelloMsg hello;
            hello.name = "Right";
            hello.room = "NO-SUCH-ROOM";
            ends.second->send(net::encode(hello));
            w.run(300);
            ASSERT_EQ(reject_on(ends.second), static_cast<int>(net::RejectReason::NoSuchRoom));       // (the protocol is right: the room is what is wrong)
        }
        {   // no bot code in a room without bots
            World w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("NOBOT-1", 2), w.now).ok);
            Client& ann = w.connect("Ann", "NOBOT-1");
            w.connect("Bob", "NOBOT-1");
            w.run(500);
            ASSERT_FALSE(w.status("NOBOT-1").bot_controller);                                           // (waiting: nothing yet)
            w.run(5000);
            ASSERT_TRUE(w.status("NOBOT-1").state == RoomState::Running);
            ASSERT_FALSE(w.status("NOBOT-1").bot_controller);                                           // a room that runs with two people has no controller
            ASSERT_TRUE(ann.sim.roster_mask() == 0x03);
            // a leader's START without a fill: the same
            ASSERT_TRUE(w.mgr.create_room(spec_of("NOBOT-2", 4), w.now).ok);
            Client& cat = w.connect("Cat", "NOBOT-2");
            w.run(300);                                                                                // (Cat first: the Hellos of two links that are made together arrive in either order, whatever the link's jitter says, and the first to arrive leads)
            w.connect("Dan", "NOBOT-2");
            w.run(500);
            ASSERT_TRUE(cat.lobby->request_start(net::FillLevel::None));
            w.run(3000);
            ASSERT_TRUE(w.status("NOBOT-2").state == RoomState::Running && w.status("NOBOT-2").bots.empty());
            ASSERT_FALSE(w.status("NOBOT-2").bot_controller);
            // the fill builds one (and the room's own bot spec does too)
            ASSERT_TRUE(w.mgr.create_room(spec_of("BOT-F", 3), w.now).ok);
            Client& eve = w.connect("Eve", "BOT-F");
            w.run(500);
            ASSERT_TRUE(eve.lobby->request_start(net::FillLevel::Easy));
            w.run(3000);
            ASSERT_TRUE(w.status("BOT-F").state == RoomState::Running && w.status("BOT-F").bots.size() == 2);
            ASSERT_TRUE(w.status("BOT-F").bot_controller);
            RoomSpec own = spec_of("BOT-S", 2);
            own.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(own, w.now).ok);
            w.connect("Fay", "BOT-S");
            w.run(6000);
            ASSERT_TRUE(w.status("BOT-S").state == RoomState::Running && w.status("BOT-S").bot_controller);
        }
        {   // the map notice waits for the pause after a cancelled start (the request that falls into the pause is lost, and the room says nothing about it)
            const std::string dir = temp_dir_for("fill_pause");
            {
                std::ifstream in(maps_dir() + "/TINY.LVL", std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                assets::LevelData tiny;
                ASSERT_TRUE(tiny.load_from_memory(bytes.data(), bytes.size()));
                const assets::AnthillSpawn* green = nullptr;
                for (const auto& sp : tiny.anthill_spawns) {
                    if (sp.tile_id == 154) green = &sp;
                }
                ASSERT_TRUE(green != nullptr);
                const uint8_t pattern[6] = {154, 0, static_cast<uint8_t>(green->y & 0xFF), static_cast<uint8_t>(green->y >> 8), static_cast<uint8_t>(green->x & 0xFF), static_cast<uint8_t>(green->x >> 8)};
                size_t at = bytes.size();
                for (size_t i = 0; i + 6 <= bytes.size() && at == bytes.size(); ++i) {
                    if (std::equal(pattern, pattern + 6, bytes.begin() + static_cast<std::ptrdiff_t>(i))) at = i;
                }
                ASSERT_TRUE(at < bytes.size());
                bytes[at + 2] = 200;
                bytes[at + 3] = 0;
                std::ofstream out(fs::path(dir) / "BAD.LVL", std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            World w(ServerLimits(), dir);
            ASSERT_TRUE(w.mgr.create_room(spec_of("PAUSE-1", 4, "BAD.LVL"), w.now).ok);
            Client& ann = w.connect("Ann", "PAUSE-1", 1);              // red and blue are playable together (S3.64): the room starts with no fill
            Client& bob = w.connect("Bob", "PAUSE-1", 2);
            bob.fail_load = true;                                       // Bob's copy is no good: the start is cancelled and the room waits two seconds
            const auto run_in = [&](uint32_t ms) {
                for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
                    w.now += 10;
                    w.net.set_time(w.now);
                    w.mgr.update(w.now);
                    for (auto& c : w.clients) c->update(w.now, dir);
                }
            };
            run_in(500);
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::None));
            run_in(300);                                                // the cancel has come: the pause (2 s) has begun
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting);
            bob.fail_load = false;
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));      // the filled roster has green, which this map cannot play: but we are in the pause
            run_in(600);
            ASSERT_TRUE(ann.room_chat.empty());                         // the room says nothing during the pause (the request is lost, START again)
            ASSERT_TRUE(w.status("PAUSE-1").state == RoomState::Waiting && w.status("PAUSE-1").bots.empty());
            run_in(2500);                                               // the pause is over
            ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Medium));
            run_in(500);
            ASSERT_EQ(said(ann.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeFillMap}));       // now it is answered
            std::error_code ignore;
            fs::remove_all(dir, ignore);
        }
        {   // a vote that nobody can cast: Ann, the only person, is cut; the two bots are not voters; the status says "vote: null" (and the C++ status has the seat and no voter)
            RWorld w;
            RoomSpec spec = held_spec("BV-9", 3);
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Easy}, ai::BotSpec{2, "standard", ai::Level::Easy}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "BV-9", 0);
            w.run(6000);
            ASSERT_TRUE(w.status("BV-9").state == RoomState::Running);
            const uint8_t ann_seat = ann.lobby->my_seat();
            ann.reconnects = false;
            w.cut(ann);
            w.run(1500);
            ASSERT_TRUE(w.status("BV-9").paused);
            w.run(32000);                                               // 30 s away in all: the vote is open for the people who are connected: there are none
            RoomStatus s = w.status("BV-9");
            ASSERT_TRUE(s.paused && s.vote_seat == ann_seat && s.voters == 0);
            ctl::JsonValue v = status_to_json(s);
            ASSERT_TRUE(v.get("paused").as_bool_or(false) && v.get("absent").size() == 1);
            ASSERT_TRUE(v.get("vote").is_null());                       // nobody can vote: no vote to show (the status of a vote with voters 0 used to be {"seat": 0, "continue": 0, "voters": 0})
        }
    } TEST_END();

    TEST_CASE("S3.76 The Opening Of A Server's Room: The First Turn Is Sealed 5000 ms After The Match Began (Protocol 12: Nobody's Clock Runs Behind The Dialog, Nothing Is Sealed, Run Or Late Before It); A Room That The Leader's START Fills With Hard Bots Has No Command Of A Bot Seat In Turn 0 And Before A Bot's First Look (Tick 1 + Seat) And Its Reaction Time Are Over, And The Bots Do Play After It") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("HOLD-1", 4), w.now).ok);
        Client& ann = w.connect("Ann", "HOLD-1", 2);                      // the person sits in seat 2: the bots take 0, 1 and 3
        ann.record_commands = true;
        w.run(500);
        ASSERT_TRUE(ann.lobby->is_leader());
        ASSERT_TRUE(ann.lobby->request_start(net::FillLevel::Hard));
        uint32_t begin = 0;                                                  // the time of the room's begin_match: the first pass after which the room runs (one pass is one 10 ms step here)
        for (int i = 0; i < 400 && begin == 0; ++i) {
            w.run(10);
            if (w.status("HOLD-1").state == RoomState::Running) begin = w.now;
        }
        ASSERT_TRUE(begin != 0);
        RoomStatus s = w.status("HOLD-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.bots.size() == 3 && s.bots[0].level == "hard" && s.bot_controller);
        ASSERT_TRUE(s.turns == 0 && s.ticks == 0);
        // the pre-start: for 5 s nothing is sealed and nothing runs (the referee's engine, the person's engine), nobody is announced as lagging, no pause, no command of anybody
        uint32_t first_turn_ms = 0;
        while (w.now - begin < 20000 && first_turn_ms == 0) {
            w.run(10);
            s = w.status("HOLD-1");
            if (s.turns > 0) {
                first_turn_ms = w.now - begin;
            } else {
                ASSERT_TRUE(s.ticks == 0 && !s.paused && s.state == RoomState::Running);
                ASSERT_TRUE(ann.sim.current_tick() == 0 && ann.saw.empty());
                if (ann.session == nullptr) continue;                         // (the Begin is on its way to the client: its session starts a link delay after the room's)
                ASSERT_TRUE(!ann.session->catching_up() && ann.session->lagging_seat() == 255 && ann.session->self_lag_behind_ms() == 0);
                ASSERT_TRUE(ann.session->runner().stalled_ms() == 0 && ann.session->runner().buffer_turns() == 1 && !ann.session->runner().stalled());   // not a stall of the link, no growth of the jitter buffer
            }
        }
        ASSERT_TRUE(first_turn_ms >= net::kMatchStartDelayMs && first_turn_ms <= net::kMatchStartDelayMs + net::kTurnMs);   // the first turn: 5000 ms after the match began (one turn, 50 ms, at the most more)
        w.run(25000);                                                       // some 500 ticks
        ASSERT_TRUE(ann.sim.current_tick() > 400);
        ASSERT_TRUE(ann.session->runner().buffer_turns() == 1 && !ann.session->catching_up());     // (the pre-start left the buffer as it was)
        size_t per_seat[4] = {0, 0, 0, 0};
        uint64_t first = ~0ull;
        for (const auto& e : ann.saw) {
            if (e.second.issuer == 2) continue;                              // the person's own (this test client orders from the first turn on: it knows no dialog)
            ASSERT_TRUE(e.second.issuer == 0 || e.second.issuer == 1 || e.second.issuer == 3);
            ASSERT_TRUE(e.first >= 1u);                                      // no turn 0 carries a command of a bot (the first tick that a person's engine runs is 1)
            ++per_seat[e.second.issuer];
            first = std::min(first, e.first);
        }
        ASSERT_TRUE(per_seat[0] >= 1 && per_seat[1] >= 1 && per_seat[3] >= 1);        // every bot played (a harvesting bot needs well under a command per second)
        ASSERT_TRUE(first >= 1u + 6u);                                       // a Hard bot looks on tick 1 + seat and its first order leaves 6 to 10 ticks later (then it is sealed into a turn)
        ASSERT_FALSE(ann.session->desynced());
    } TEST_END();
    TEST_CASE("S3.75 What The Room's Limit And Status Say About The Start (Protocol 12): run_ms Counts From The Moment The Match Began, The 5 s Before The First Turn Included (A Room With A Limit Of 8 s Fails 8 s After It Began, Not 13); A Room With A Bot Of Its Specification Opens Its Bots Like The Product Does (The Status Says The Controller's Hold Is kStartHoldTicks), A Room Without Bots Has No Controller") {
        {
            World w;
            RoomSpec spec = spec_of("RUNLIM-1", 2);
            spec.run_ms = 8000;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "RUNLIM-1");
            w.connect("Bob", "RUNLIM-1");
            uint32_t begin = 0;
            for (int i = 0; i < 400 && begin == 0; ++i) {
                w.run(10);
                if (w.status("RUNLIM-1").state == RoomState::Running) begin = w.now;
            }
            ASSERT_TRUE(begin != 0);
            uint32_t failed_after = 0;
            while (w.now - begin < 20000 && failed_after == 0) {
                w.run(10);
                const RoomStatus s = w.status("RUNLIM-1");
                if (s.state == RoomState::Failed) {
                    failed_after = w.now - begin;
                    ASSERT_TRUE(s.reason.find("longer") != std::string::npos);
                    ASSERT_TRUE(s.turns > 0 && s.turns < 100);                                       // (3 s of turns: the first one was sealed 5 s into the 8)
                }
            }
            ASSERT_TRUE(failed_after >= 8000 && failed_after <= 8000 + net::kTurnMs);             // the limit counts from the begin of the match, the 5 s of the dialog in it
        }
        {
            World w;
            RoomSpec own = spec_of("HOLDSPEC-1", 2);
            own.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(own, w.now).ok);
            w.connect("Fay", "HOLDSPEC-1");
            ASSERT_TRUE(w.mgr.create_room(spec_of("PLAIN-1", 2), w.now).ok);
            w.connect("Gus", "PLAIN-1");
            w.connect("Hal", "PLAIN-1");
            w.run(1500);
            const RoomStatus with_bot = w.status("HOLDSPEC-1");
            const RoomStatus plain = w.status("PLAIN-1");
            ASSERT_TRUE(with_bot.state == RoomState::Running && with_bot.bot_controller && with_bot.bot_start_hold == ai::kStartHoldTicks);
            ASSERT_TRUE(plain.state == RoomState::Running && !plain.bot_controller && plain.bot_start_hold == 0u);
        }
    } TEST_END();
    TEST_CASE("S3.77 Protocol 12, A Command That Reaches A Server's Room Before Its First Turn Is Sealed Is Discarded (The Product Path: Room::begin_match Sets The Start Delay): A Raw Connection That Writes Seventy Orders At 100 ms And One At 4,500 ms Of The Dialog Has Them In No Turn On Any Machine And Stays In The Room; Its Order After The First Seal And An Honest Client's Order After Its First Tick Are Applied On Both Machines At The Same Tick") {
        World w;
        ASSERT_TRUE(w.mgr.create_room(spec_of("EARLY-1", 2), w.now).ok);
        Client& ann = w.connect("Ann", "EARLY-1");                       // honest: it orders after its first tick
        Client& bob = w.connect("Bob", "EARLY-1");                       // a raw connection in the dialog (a modified client): it writes CommandMsg itself
        ann.record_commands = true;
        bob.record_commands = true;
        ann.next_order_ms = bob.next_order_ms = 0xFFFFFFFFu;             // (the rig's own orders, one every 700 ms from its first moment because it knows no dialog, are off: this test sends its own, at chosen times)
        uint32_t begin = 0;
        for (int i = 0; i < 400 && begin == 0; ++i) {
            w.run(10);
            if (w.status("EARLY-1").state == RoomState::Running) begin = w.now;
        }
        ASSERT_TRUE(begin != 0);
        ASSERT_TRUE(ann.sim.get_world_state().ants.size() > 0);
        const auto order_of = [&](Client& who, int16_t x) {
            sim::Command c;
            c.type = sim::CommandType::GroupMove;
            c.issuer = who.lobby->my_seat();
            c.tile_x = x;
            c.tile_y = 12;
            for (const auto& a : who.sim.get_world_state().ants) {
                if (a.player_id == c.issuer && c.ants.size() < 3) c.ants.push_back(a.id);
            }
            return c;
        };
        const auto step_to = [&](uint32_t ms_after_begin) {
            while (w.now - begin < ms_after_begin) w.run(10);
        };
        step_to(100);
        ASSERT_TRUE(bob.session != nullptr && !order_of(bob, 10).ants.empty());
        for (int i = 0; i < 70; ++i) bob.end->send(net::encode(net::CommandMsg{order_of(bob, static_cast<int16_t>(10 + i % 5))}));   // a scripted opening, seventy orders
        step_to(4500);
        bob.end->send(net::encode(net::CommandMsg{order_of(bob, 30)}));                                                              // one more, half a second before the first turn
        step_to(4700);
        RoomStatus s = w.status("EARLY-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns == 0 && s.ticks == 0);      // the dialog is still up: nothing sealed
        uint32_t sealed_at = 0;
        for (int i = 0; i < 100 && sealed_at == 0; ++i) {
            w.run(10);
            if (w.status("EARLY-1").turns > 0) sealed_at = w.now - begin;
        }
        ASSERT_TRUE(sealed_at >= net::kMatchStartDelayMs && sealed_at <= net::kMatchStartDelayMs + net::kTurnMs);   // the first turn is 5000 ms after the match began
        // after the first seal: the raw connection's order is an order like any other, and so is an honest client's, sent after its own first tick
        step_to(sealed_at + 200);
        bob.end->send(net::encode(net::CommandMsg{order_of(bob, 31)}));
        bool ann_sent = false;
        for (int i = 0; i < 600 && !ann_sent; ++i) {
            if (ann.sim.current_tick() >= 1) ann_sent = ann.session->submit(order_of(ann, 20));
            if (!ann_sent) w.run(10);
        }
        ASSERT_TRUE(ann_sent);
        w.run(3000);
        ASSERT_TRUE(ann.saw.size() == 2 && bob.saw.size() == 2);                         // Bob's late order and Ann's, and none of Bob's seventy-one orders of the dialog (their tiles are 10 - 14 and 30)
        for (size_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(ann.saw[i].second.tile_x == 31 || ann.saw[i].second.tile_x == 20);
            ASSERT_TRUE(ann.saw[i].first >= 1u);                                         // not turn 0 (no tick had run)
            ASSERT_TRUE(ann.saw[i].first == bob.saw[i].first && ann.saw[i].second.issuer == bob.saw[i].second.issuer && ann.saw[i].second.tile_x == bob.saw[i].second.tile_x);   // the same tick on both machines
        }
        ASSERT_TRUE(!ann.lost && !bob.lost && !ann.session->lost() && !bob.session->lost());      // a discarded command is no violation: Bob stays in the room
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        s = w.status("EARLY-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks > 50);                      // (some 3 s of play after the first turn)
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records (restart_record.hpp, docs/NETWORK_PORT.md "Restart records"): S3.82 and on
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

uint32_t le32_at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

// A frame as the format says it (made here from the description, not with the writer): type, length, payload, the CRC of those three
std::vector<uint8_t> test_frame(uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back(type);
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>((payload.size() >> (8 * i)) & 0xFFu));
    f.insert(f.end(), payload.begin(), payload.end());
    const uint32_t crc = restart_crc32(f.data(), f.size());
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>((crc >> (8 * i)) & 0xFFu));
    return f;
}

std::vector<uint8_t> with_magic(const std::vector<uint8_t>& frames) {
    // (a vector::insert of the 8 bytes of the magic into an empty vector is a false -Warray-bounds / -Wstringop-overflow of GCC 12: the vector is made at its size and filled instead)
    std::vector<uint8_t> out(sizeof(kRestartMagic) + frames.size());
    for (size_t i = 0; i < sizeof(kRestartMagic); ++i) out[i] = static_cast<uint8_t>(kRestartMagic[i]);
    std::copy(frames.begin(), frames.end(), out.begin() + static_cast<std::ptrdiff_t>(sizeof(kRestartMagic)));
    return out;
}

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// The payload of a turns frame, as the format says it
std::vector<uint8_t> turns_payload(uint32_t first, const std::vector<net::TurnMsg>& turns) {
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

std::vector<uint8_t> check_payload(uint32_t turn, uint64_t hash) {
    std::vector<uint8_t> p;
    net::ByteWriter w(p);
    w.u32(turn);
    w.u64(hash);
    return p;
}

// A record file's frames: where each starts and ends (the file is a good one)
std::vector<std::pair<size_t, size_t>> frames_of(const std::vector<uint8_t>& file) {
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

std::vector<uint8_t> head_frame_of(const RestartHead& h) { return encode_restart_head(h); }

bool same_checks(const std::vector<RestartCheck>& a, const std::vector<RestartCheck>& b, size_t count) {
    if (a.size() < count || b.size() < count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (a[i].turn != b[i].turn || a[i].hash != b[i].hash) return false;
    }
    return true;
}

}  // namespace

void run_persist_tests() {
    TEST_CASE("S3.82 The Record: A Head And Its Turns And Checkpoints Read Back Exactly (Every Field, Every Command, The Keys); The File Is For Its Owner Only (Mode 600 In A Folder Of Mode 700), Has Its Own Name (Two Codes That Differ In Case Differ), Is Made All At Once (A Process That Dies At Its First Write Leaves No Record Under Its Name), A Torn Tail Is Cut Off When It Is Opened Again; The Budget Follows Every Byte; The CRC Is CRC-32") {
        {   // CRC-32: the check value of the standard, and in pieces
            const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
            ASSERT_EQ(restart_crc32(digits, 9), 0xCBF43926u);
            ASSERT_EQ(restart_crc32(digits + 4, 5, restart_crc32(digits, 4)), 0xCBF43926u);
            ASSERT_EQ(restart_crc32(digits, 0), 0u);
        }
        RestartConfig cfg = test_restart_config("rec-format");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(fs::is_directory(cfg.dir));
#ifndef _WIN32
        {
            struct stat st;
            ASSERT_EQ(::stat(cfg.dir.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);                       // the folder is the server's own
        }
#endif
        // the name: the code, a hash of the code (so that "ABC" and "abc" are two files even where the file system ignores case), an extension
        {
            const auto lower = [](std::string text) {
                for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return text;
            };
            ASSERT_TRUE(lower(store.path_for("ABC")) != lower(store.path_for("abc")));      // two files even where the file system ignores case (the names differ without it)
        }
        {
            const std::string name = fs::path(store.path_for("ROOM-1")).filename().string();
            const std::string ext = kRestartExtension;
            ASSERT_TRUE(name.rfind("room-ROOM-1-", 0) == 0 && name.size() == std::string("room-ROOM-1-").size() + 8 + ext.size());
            ASSERT_TRUE(name.compare(name.size() - ext.size(), ext.size(), ext) == 0);
        }
        const RestartHead head = sample_head();
        auto w = store.create(head, why);
        ASSERT_TRUE(w != nullptr);
        ASSERT_EQ(w->path(), store.path_for("REC-1"));
        ASSERT_EQ(store.open_records(), size_t{1});
        ASSERT_TRUE(store.records().size() == 1 && store.records()[0] == w->path());
        ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});                                // (no temporary file is left behind)
        ASSERT_EQ(w->turns(), 0u);
        {
            const RestartLoaded only_head = read_restart_record(w->path(), cfg.max_record_bytes);       // a record with a head and nothing else is a record
            ASSERT_TRUE(only_head.ok() && only_head.turns.empty() && only_head.checks.empty() && !only_head.torn && same_head(only_head.head, head));
        }
        std::vector<net::TurnMsg> sent;
        std::vector<RestartCheck> checks;
        for (uint32_t n = 0; n < 100; ++n) {
            sent.push_back(sample_turn(n));
            ASSERT_TRUE(w->append_turn(sent.back()));
            if ((n + 1) % net::kHashEveryTurns == 0) {
                checks.push_back(RestartCheck{n, 0x0102030405060708ull + n});
                ASSERT_TRUE(w->append_check(n, checks.back().hash));
            }
        }
        ASSERT_EQ(w->turns(), 100u);
        ASSERT_TRUE(w->dirty());
        ASSERT_TRUE(w->sync());
        ASSERT_FALSE(w->dirty());
        ASSERT_EQ(fs::file_size(w->path()), w->bytes());
        ASSERT_EQ(store.used_bytes(), w->bytes());                                      // the budget follows every byte of the file
        {
            const RestartLoaded r = read_restart_record(w->path(), cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.why.empty());
            ASSERT_TRUE(same_head(r.head, head));                                       // every field, the start message, the bots, the fill, the two keys
            ASSERT_EQ(r.turns.size(), size_t{100});
            ASSERT_TRUE(same_turns(r.turns, sent, 100));                                // every command of every turn, with its ants
            ASSERT_EQ(r.checks.size(), size_t{5});
            ASSERT_TRUE(same_checks(r.checks, checks, 5));
            ASSERT_TRUE(r.good_bytes == r.file_bytes && r.file_bytes == fs::file_size(w->path()) && !r.torn && r.path == w->path());
        }
#ifndef _WIN32
        {
            struct stat st;
            ASSERT_EQ(::stat(w->path().c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);                       // it holds the keys of the seats: for its owner only
        }
#endif
        {   // a writer that is closed leaves the file where it is, and gives its bytes back
            const std::string path = w->path();
            const uint64_t size = w->bytes();
            w.reset();
            ASSERT_TRUE(fs::exists(path) && fs::file_size(path) == size);
            ASSERT_EQ(store.used_bytes(), 0u);
            ASSERT_EQ(store.open_records(), size_t{0});
            // opened again to go on: the budget takes the file, the next turn is number 100
            auto again = store.reopen(path, size, 100, why);
            ASSERT_TRUE(again != nullptr);
            ASSERT_EQ(store.used_bytes(), size);
            ASSERT_EQ(again->turns(), 100u);
            ASSERT_TRUE(again->append_turn(sample_turn(100)));
            ASSERT_TRUE(again->sync());
            sent.push_back(sample_turn(100));
            const RestartLoaded r = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.turns.size() == 101 && same_turns(r.turns, sent, 101));
            // a write that was cut short (a torn tail): what follows the last whole frame is cut off when the record is opened again, and the next frame follows the last good one
            again.reset();
            std::vector<uint8_t> bytes = read_all_bytes(path);
            const std::vector<uint8_t> whole = bytes;
            const std::vector<uint8_t> next = test_frame(2, turns_payload(101, {sample_turn(101)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);                  // 9 bytes of the next frame: a header and a few bytes of its payload
            write_all_bytes(path, bytes);
            const RestartLoaded torn = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(torn.ok() && torn.torn && torn.turns.size() == 101 && torn.good_bytes == whole.size() && torn.file_bytes == whole.size() + 9);
            auto cut = store.reopen(path, torn.good_bytes, static_cast<uint32_t>(torn.turns.size()), why);
            ASSERT_TRUE(cut != nullptr);
            ASSERT_EQ(fs::file_size(path), whole.size());                               // the torn bytes are gone
            ASSERT_TRUE(cut->append_turn(sample_turn(101)));
            cut.reset();
            sent.push_back(sample_turn(101));
            const RestartLoaded clean = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(clean.ok() && !clean.torn && clean.turns.size() == 102 && same_turns(clean.turns, sent, 102));
            // the file must not be shorter than what the caller says is good (the caller read another file): refused, nothing changes
            ASSERT_TRUE(store.reopen(path, fs::file_size(path) + 1, 102, why) == nullptr && !why.empty());
            ASSERT_EQ(store.used_bytes(), 0u);
        }
        {   // a second head for the same code replaces the first, all at once (a room that is made again with a code that an old record still has)
            auto first = store.create(head, why);
            ASSERT_TRUE(first != nullptr && first->append_turn(sample_turn(0)));
            RestartHead other = head;
            other.start.seed = 99;
#ifdef _WIN32
            // (Windows cannot replace a file that is open: while the first writer holds it the second head is refused and the first record stays whole; a room's
            // writer is always closed before its code can be made again, and then the second head replaces the record as everywhere)
            ASSERT_TRUE(store.create(other, why) == nullptr && !why.empty());
            const RestartLoaded kept = read_restart_record(first->path(), cfg.max_record_bytes);
            ASSERT_TRUE(kept.ok() && kept.turns.size() == 1 && kept.head.start.seed == head.start.seed);
            ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});
            first.reset();
#endif
            auto second = store.create(other, why);
            ASSERT_TRUE(second != nullptr);
            const RestartLoaded r = read_restart_record(second->path(), cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.turns.empty() && r.head.start.seed == 99);
            ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});
        }
        {   // a writer that is told to discard its record deletes the file and gives its bytes back, and writes no more
            store.remove_file(store.path_for("REC-1"));
            auto d = store.create(head, why);
            ASSERT_TRUE(d != nullptr && d->append_turn(sample_turn(0)));
            const std::string path = d->path();
            d->discard();
            ASSERT_FALSE(fs::exists(path));
            ASSERT_TRUE(d->failed());
            ASSERT_FALSE(d->append_turn(sample_turn(1)));
            ASSERT_FALSE(fs::exists(path));
            d.reset();
            ASSERT_EQ(store.used_bytes(), 0u);
            ASSERT_EQ(store.open_records(), size_t{0});
        }
        {   // a turn that is not the next one fails the writer: a record with a hole is no record; and it stays failed
            auto f = store.create(head, why);
            ASSERT_TRUE(f != nullptr && f->append_turn(sample_turn(0)));
            ASSERT_FALSE(f->append_turn(sample_turn(5)));
            ASSERT_TRUE(f->failed() && f->error().find("order") != std::string::npos);
            ASSERT_FALSE(f->append_turn(sample_turn(1)));
            ASSERT_FALSE(f->sync());
            f->discard();
        }
        {   // the limits: a head that is bigger than a record may be, a budget that does not hold a record, a folder that is gone; nothing is left behind and the budget is whole
            RestartConfig tiny = cfg;
            tiny.max_record_bytes = 100;
            RestartStore small_store(tiny);
            ASSERT_TRUE(small_store.create(head, why) == nullptr && why.find("bigger") != std::string::npos);
            RestartConfig poor = cfg;
            poor.budget_bytes = 50;
            RestartStore poor_store(poor);
            ASSERT_TRUE(poor_store.create(head, why) == nullptr && why.find("budget") != std::string::npos);
            ASSERT_EQ(poor_store.used_bytes(), 0u);
            RestartConfig gone = test_restart_config("rec-gone");
            RestartStore gone_store(gone);                                              // (prepare() was never called: the folder does not exist)
            ASSERT_TRUE(gone_store.create(head, why) == nullptr && !why.empty());
            ASSERT_EQ(gone_store.used_bytes(), 0u);
            ASSERT_EQ(gone_store.open_records(), size_t{0});
            RestartStore off{RestartConfig{}};                                          // a server that keeps no records
            ASSERT_FALSE(off.enabled());
            ASSERT_TRUE(off.create(head, why) == nullptr && off.records().empty());
        }
        {   // stale temporary files of a start that died are removed when the folder is prepared; records are not
            const fs::path stale = fs::path(cfg.dir) / "room-OLD-0000abcd.restart.0123456789abcdef.tmp";
            write_all_bytes(stale, {1, 2, 3});
            const fs::path other = fs::path(cfg.dir) / "notes.txt";
            write_all_bytes(other, {'x'});
            auto keep = store.create(head, why);
            ASSERT_TRUE(keep != nullptr);
            ASSERT_TRUE(store.prepare(why));
            ASSERT_FALSE(fs::exists(stale));
            ASSERT_TRUE(fs::exists(other) && fs::exists(store.path_for("REC-1")));      // (what is not a temporary file of a record is left alone)
        }
#ifndef _WIN32
        {   // made all at once: a process whose first write kills it (RLIMIT_FSIZE: SIGXFSZ) leaves no record under the record's name, and the next start of the folder is clean
            RestartConfig c = test_restart_config("rec-atomic");
            RestartStore prepared(c);
            ASSERT_TRUE(prepared.prepare(why));
            const pid_t pid = ::fork();
            ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                struct rlimit none = {0, 0};
                ::setrlimit(RLIMIT_CORE, &none);
                ::setrlimit(RLIMIT_FSIZE, &none);
                ::signal(SIGXFSZ, SIG_DFL);
                RestartStore child(c);
                std::string child_why;
                (void)child.create(head, child_why);
                ::_exit(0);                                                             // not reached: the write kills the process
            }
            int wait_status = 0;
            ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
            ASSERT_TRUE(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGXFSZ);
            ASSERT_TRUE(prepared.records().empty());                                    // no record under any name that a start would read
            ASSERT_TRUE(prepared.prepare(why));                                         // the temporary file of the dead process is removed
            ASSERT_EQ(entries_but_lock(c.dir), size_t{0});
            auto after = prepared.create(head, why);
            ASSERT_TRUE(after != nullptr);
        }
#endif
    } TEST_END();

    TEST_CASE("S3.83 The Reader Is Safe With Anything: Every Prefix Of A Record Is Read As Far As It Is Whole (A Cut Inside The Head Is Refused, A Cut After It Is A Torn Tail); Every Flipped Bit In A Frame Is Either Refused Or Stops The Reading Early (Never Altered Turns); Garbage, Hostile Lengths, Repeated, Reordered And Misplaced Frames, A Second Head, A Checkpoint Of A Turn That Is Not There, Heads That Are Wrong In Each Field, A File That Is Too Big Or No File: All Refused With A Reason, None Crashes Or Allocates Without Bound (Runs Under AddressSanitizer)") {
        RestartConfig cfg = test_restart_config("rec-fuzz");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const RestartHead head = sample_head();
        auto w = store.create(head, why);
        ASSERT_TRUE(w != nullptr);
        for (uint32_t n = 0; n < 61; ++n) {
            ASSERT_TRUE(w->append_turn(sample_turn(n)));
            if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(w->append_check(n, 0xAA00u + n));
        }
        ASSERT_TRUE(w->sync());
        const std::string path = w->path();
        w.reset();
        const std::vector<uint8_t> good = read_all_bytes(path);
        const RestartLoaded ref = parse_restart_record(good.data(), good.size());
        ASSERT_TRUE(ref.ok() && ref.turns.size() == 61 && ref.checks.size() == 3 && !ref.torn);
        const std::vector<std::pair<size_t, size_t>> frames = frames_of(good);
        ASSERT_EQ(frames.size(), size_t{1 + 61 + 3});
        ASSERT_EQ(frames.back().second, good.size());
        const size_t head_end = frames[0].second;
        // what a reader may hand out for a damaged file: the head of the record and a PREFIX of its turns and checkpoints, never anything else
        const auto prefix_of_reference = [&](const RestartLoaded& r) {
            return same_head(r.head, ref.head) && r.turns.size() <= ref.turns.size() && same_turns(r.turns, ref.turns, r.turns.size()) && r.checks.size() <= ref.checks.size() &&
                   same_checks(r.checks, ref.checks, r.checks.size());
        };
        {   // every prefix: refused inside the head, read as far as it is whole after it
            std::vector<size_t> boundaries;
            for (const auto& f : frames) boundaries.push_back(f.second);
            size_t last_turns = 0;
            for (size_t n = 0; n <= good.size(); ++n) {
                const RestartLoaded r = parse_restart_record(good.data(), n);
                if (n < head_end) {
                    ASSERT_FALSE(r.ok());
                    ASSERT_TRUE(r.status == (n < sizeof(kRestartMagic) ? RestartLoaded::Status::NotARecord : RestartLoaded::Status::Corrupt) && !r.why.empty());
                    continue;
                }
                ASSERT_TRUE(r.ok() && prefix_of_reference(r));
                ASSERT_TRUE(r.good_bytes <= n && r.torn == (r.good_bytes < n) && std::find(boundaries.begin(), boundaries.end(), r.good_bytes) != boundaries.end());
                ASSERT_TRUE(r.turns.size() >= last_turns);                              // (a longer prefix never holds fewer turns)
                last_turns = r.turns.size();
            }
            ASSERT_EQ(last_turns, size_t{61});
        }
        {   // every flipped bit
            const uint8_t masks[] = {0x01, 0x10, 0x80, 0xFF};
            for (size_t p = 0; p < good.size(); ++p) {
                for (const uint8_t mask : masks) {
                    std::vector<uint8_t> bad = good;
                    bad[p] = static_cast<uint8_t>(bad[p] ^ mask);
                    const RestartLoaded r = parse_restart_record(bad.data(), bad.size());
                    if (r.ok()) ASSERT_TRUE(prefix_of_reference(r));                    // whatever is accepted is the record itself, cut short: never different turns
                    if (p < sizeof(kRestartMagic)) {                                    // the magic: not a record of this format
                        ASSERT_FALSE(r.ok());
                        continue;
                    }
                    size_t fi = 0;
                    while (fi + 1 < frames.size() && p >= frames[fi].second) ++fi;
                    const size_t offset = p - frames[fi].first;
                    if (fi + 1 < frames.size() && (offset == 0 || offset >= 5)) {
                        ASSERT_FALSE(r.ok());                                           // a flipped type, payload or checksum before the last frame: refused
                        ASSERT_TRUE(r.status == RestartLoaded::Status::Corrupt && !r.why.empty());
                    }
                }
            }
        }
        {   // the last frame with a wrong checksum is a write that was interrupted: a torn tail (the record is the frames before it); the same fault with a frame behind it is corruption
            std::vector<uint8_t> bad = good;
            bad[frames.back().second - 6] = static_cast<uint8_t>(bad[frames.back().second - 6] ^ 0x55);
            const RestartLoaded r = parse_restart_record(bad.data(), bad.size());
            ASSERT_TRUE(r.ok() && r.torn && r.good_bytes == frames.back().first && r.turns.size() == 60 && r.checks.size() == 3 && prefix_of_reference(r));
            std::vector<uint8_t> mid = good;
            mid[frames[frames.size() - 2].second - 6] = static_cast<uint8_t>(mid[frames[frames.size() - 2].second - 6] ^ 0x55);
            const RestartLoaded m = parse_restart_record(mid.data(), mid.size());
            ASSERT_TRUE(!m.ok() && m.status == RestartLoaded::Status::Corrupt);
        }
        {   // garbage: nothing is a record, nothing crashes (a third of them start with the magic, as a damaged record would)
            uint32_t rng = 12345;
            const auto next = [&rng]() {
                rng = rng * 1664525u + 1013904223u;
                return rng >> 8;
            };
            for (int i = 0; i < 3000; ++i) {
                std::vector<uint8_t> junk;
                if (i % 3 == 0) junk.assign(kRestartMagic, kRestartMagic + sizeof(kRestartMagic));
                const size_t len = next() % 400;
                for (size_t k = 0; k < len; ++k) junk.push_back(static_cast<uint8_t>(next()));
                const RestartLoaded r = parse_restart_record(junk.data(), junk.size());
                ASSERT_FALSE(r.ok());
                ASSERT_FALSE(r.why.empty());
            }
            ASSERT_FALSE(parse_restart_record(nullptr, 0).ok());
            ASSERT_FALSE(parse_restart_record(nullptr, 100).ok());
        }
        {   // hostile fields
            const std::vector<uint8_t> huge_type2 = with_magic({2, 0xFF, 0xFF, 0xFF, 0xFF});
            const RestartLoaded a = parse_restart_record(huge_type2.data(), huge_type2.size());
            ASSERT_TRUE(!a.ok() && a.status == RestartLoaded::Status::Corrupt);         // a length of 4 GB: refused before anything is allocated
            const std::vector<uint8_t> cut_head = with_magic({1, 0x00, 0x00, 0x10, 0x00});   // a head of 1 MiB that the file does not hold
            const RestartLoaded b = parse_restart_record(cut_head.data(), cut_head.size());
            ASSERT_TRUE(!b.ok() && b.status == RestartLoaded::Status::Corrupt);
            const std::vector<uint8_t> zeros = with_magic(std::vector<uint8_t>(5000, 0));
            const RestartLoaded c = parse_restart_record(zeros.data(), zeros.size());
            ASSERT_TRUE(!c.ok());                                                       // (nothing but a zero fill after the magic: there is no head)
            std::vector<uint8_t> other_format(kRestartMagic, kRestartMagic + sizeof(kRestartMagic));
            other_format.back() = '2';
            const RestartLoaded d = parse_restart_record(other_format.data(), other_format.size());
            ASSERT_TRUE(!d.ok() && d.status == RestartLoaded::Status::UnknownFormat && d.why.find("format") != std::string::npos);
            std::vector<uint8_t> not_ours = good;
            not_ours[0] = 'X';
            const RestartLoaded e = parse_restart_record(not_ours.data(), not_ours.size());
            ASSERT_TRUE(!e.ok() && e.status == RestartLoaded::Status::NotARecord);
            // a zero fill after a good record is a torn tail (the file grew and its data did not reach the disk), not corruption
            std::vector<uint8_t> padded = good;
            padded.insert(padded.end(), 3000, 0);
            const RestartLoaded f = parse_restart_record(padded.data(), padded.size());
            ASSERT_TRUE(f.ok() && f.torn && f.good_bytes == good.size() && f.turns.size() == 61 && prefix_of_reference(f));
        }
        {   // frames in the wrong order, repeated, misplaced: all refused, whatever their checksums say
            const std::vector<uint8_t> head_f = test_frame(1, std::vector<uint8_t>(good.begin() + static_cast<std::ptrdiff_t>(frames[0].first + 5), good.begin() + static_cast<std::ptrdiff_t>(frames[0].second - 4)));
            const std::vector<uint8_t> t0 = test_frame(2, turns_payload(0, {sample_turn(0)}));
            const std::vector<uint8_t> t1 = test_frame(2, turns_payload(1, {sample_turn(1)}));
            const std::vector<uint8_t> t5 = test_frame(2, turns_payload(5, {sample_turn(5)}));
            ASSERT_TRUE(head_f == std::vector<uint8_t>(good.begin() + static_cast<std::ptrdiff_t>(frames[0].first), good.begin() + static_cast<std::ptrdiff_t>(frames[0].second)));    // (the test's own frame is the writer's)
            const auto refused = [](const std::vector<uint8_t>& frames_bytes) {
                const std::vector<uint8_t> file = with_magic(frames_bytes);
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                return !r.ok() && r.status == RestartLoaded::Status::Corrupt && !r.why.empty();
            };
            const auto accepted_turns = [](const std::vector<uint8_t>& frames_bytes) {
                const std::vector<uint8_t> file = with_magic(frames_bytes);
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                return r.ok() ? r.turns.size() : size_t{9999};
            };
            ASSERT_EQ(accepted_turns(concat({head_f, t0, t1})), size_t{2});             // (the same frames in the right order are fine)
            ASSERT_TRUE(refused(concat({head_f, t1})));                                 // the first turn is not 0
            ASSERT_TRUE(refused(concat({head_f, t0, t0})));                             // a repeat
            ASSERT_TRUE(refused(concat({head_f, t0, t1, t0})));
            ASSERT_TRUE(refused(concat({head_f, t1, t0})));                             // swapped
            ASSERT_TRUE(refused(concat({head_f, t0, t5})));                             // a hole
            ASSERT_TRUE(refused(concat({t0, head_f})));                                 // turns before the head
            ASSERT_TRUE(refused(concat({t0, t1})));                                     // no head at all
            ASSERT_TRUE(refused(concat({head_f, head_f})));                             // a second head
            ASSERT_TRUE(refused(concat({head_f, t0, head_f})));
            ASSERT_TRUE(refused(concat({head_f, test_frame(3, check_payload(19, 1))})));          // a checkpoint of a turn that the file does not hold
            ASSERT_TRUE(refused(concat({head_f, t0, test_frame(3, check_payload(0, 1))})));       // of a turn that is not a multiple of 20 minus 1
            std::vector<net::TurnMsg> twenty;
            for (uint32_t n = 0; n < 20; ++n) twenty.push_back(sample_turn(n));
            const std::vector<uint8_t> batch = test_frame(2, turns_payload(0, twenty));            // (twenty turns in one frame are fine: the reader takes any number up to 4096)
            ASSERT_EQ(accepted_turns(concat({head_f, batch, test_frame(3, check_payload(19, 77))})), size_t{20});
            ASSERT_TRUE(refused(concat({head_f, batch, test_frame(3, check_payload(19, 77)), test_frame(3, check_payload(19, 78))})));        // a checkpoint twice
            ASSERT_TRUE(refused(concat({head_f, batch, test_frame(3, check_payload(39, 77))})));  // a turn that is not there yet
            ASSERT_TRUE(refused(concat({head_f, test_frame(4, {1, 2, 3})})));                      // a frame type that nobody writes
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, {})})));                             // an empty turns frame
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, turns_payload(0, {}))})));           // count 0
            std::vector<uint8_t> trailing = turns_payload(0, {sample_turn(0)});
            trailing.push_back(0);
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, trailing)})));                       // bytes after the last turn of a frame
            std::vector<uint8_t> short_commands = turns_payload(0, {sample_turn(1)});
            short_commands.pop_back();
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, short_commands)})));                 // a command that is cut short
            net::TurnMsg bad_issuer = sample_turn(1);
            bad_issuer.commands[0].issuer = 9;
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, turns_payload(0, {bad_issuer}))})));  // a command of a seat that does not exist: the sequencer stamps seats 0 - 3 only
            std::vector<uint8_t> too_many = turns_payload(0, {net::TurnMsg{}});
            too_many[6] = 0xFF;                                                                     // 0x01FF commands: more than a turn may hold (512 is the most) ...
            too_many[7] = 0x03;                                                                     // ... 0x03FF = 1023
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, too_many)})));
            ASSERT_TRUE(refused(concat({head_f, test_frame(3, std::vector<uint8_t>(11, 0))})));    // a checkpoint of the wrong size
        }
        {   // heads that are wrong in one field (their frames are whole and their checksums right: only the head's own checks can refuse them)
            struct Case {
                const char* what;
                std::function<void(RestartHead&)> mutate;
            };
            const std::vector<Case> cases = {
                {"one player", [](RestartHead& h) { h.players = 1; }},
                {"five players", [](RestartHead& h) { h.players = 5; }},
                {"a code with a space", [](RestartHead& h) { h.code = "bad code"; }},
                {"an empty code", [](RestartHead& h) { h.code.clear(); }},
                {"a map that is a path", [](RestartHead& h) { h.map = "../x.lvl"; h.start.map_name = h.map; }},
                {"a start message of another map", [](RestartHead& h) { h.start.map_name = "SMALL.LVL"; }},
                {"a start message of another map file", [](RestartHead& h) { h.start.map_hash = 5; }},
                {"a start message of another fog", [](RestartHead& h) { h.start.fog = true; }},
                {"a roster of more seats than players", [](RestartHead& h) { h.start.roster = 0x0F; h.start.names[3] = "Dee"; }},
                {"a bot outside the roster", [](RestartHead& h) { h.bots.push_back(ai::BotSpec{3, "standard", ai::Level::Easy}); }},
                {"two bots on a seat", [](RestartHead& h) { h.bots.push_back(ai::BotSpec{2, "standard", ai::Level::Easy}); }},
                {"a bot that does not exist", [](RestartHead& h) { h.bots[0].kind = "boss"; }},
                {"a level that does not exist", [](RestartHead& h) { h.bots[0].level = static_cast<ai::Level>(7); }},
                {"a key for a bot", [](RestartHead& h) { h.keys[2] = test_key(9); }},
                {"a key for a seat outside the roster", [](RestartHead& h) { h.keys[3] = test_key(9); }},
                {"one key for two seats", [](RestartHead& h) { h.keys[1] = h.keys[0]; }},
                {"no game version", [](RestartHead& h) { h.identity.game_version.clear(); }},
                {"a game version of 40 characters", [](RestartHead& h) { h.identity.game_version = std::string(40, 'v'); }},
                {"a build id with a control character", [](RestartHead& h) { h.identity.build_id = "a\nb"; }},
            };
            for (const Case& c : cases) {
                RestartHead bad = head;
                c.mutate(bad);
                const std::vector<uint8_t> file = with_magic(head_frame_of(bad));
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                ASSERT_MSG(!r.ok() && r.status == RestartLoaded::Status::Corrupt && !r.why.empty(), c.what);
            }
            const std::vector<uint8_t> fine = with_magic(head_frame_of(head));
            ASSERT_TRUE(parse_restart_record(fine.data(), fine.size()).ok());           // (the unmutated head passes the same road)
        }
        {   // files: one that is too big is refused without being read, a folder, a link and a file that is not there are no records
            const fs::path big = fs::path(cfg.dir) / "room-BIG-00000000.restart";
            {
                std::ofstream out(big, std::ios::binary);
                out.put('x');
            }
            fs::resize_file(big, 100ull * 1024 * 1024);                                 // (a sparse file where the file system has them: nothing is read)
            const RestartLoaded r = read_restart_record(big.string(), 48ull * 1024 * 1024);
            ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::TooBig);
            fs::remove(big);
            const fs::path dir = fs::path(cfg.dir) / "room-DIR-00000000.restart";
            fs::create_directories(dir);
            ASSERT_TRUE(read_restart_record(dir.string(), 1 << 20).status == RestartLoaded::Status::Unreadable);
            ASSERT_TRUE(read_restart_record((fs::path(cfg.dir) / "nothing.restart").string(), 1 << 20).status == RestartLoaded::Status::Unreadable);
            ASSERT_EQ(store.records().size(), size_t{1});                               // (the folder named like a record is not listed as one)
            for (const std::string& rec : store.records()) ASSERT_TRUE(fs::is_regular_file(rec));
#ifndef _WIN32
            const fs::path link = fs::path(cfg.dir) / "room-LINK-00000000.restart";
            fs::create_symlink(path, link);
            ASSERT_TRUE(read_restart_record(link.string(), 1 << 20).status == RestartLoaded::Status::Unreadable);       // a link is not followed: a record is a file of the folder
            fs::remove(link);
#endif
            fs::remove_all(dir);
        }
    } TEST_END();
}


namespace {

// A room of `players` machines that has played `play_ms` of its match (after the five seconds of the start dialog): its record is on disk. The machines are returned in the order of their seats.
std::vector<RClient*> play_room(PWorld& w, const RoomSpec& spec, uint32_t play_ms) {
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
std::vector<uint8_t> record_bytes(const PWorld& w, const std::string& code) { return read_all_bytes(w.record_path(code)); }

// The independent replay of a record: an engine made the way a client makes one from the start message, run through the turns with the engine's own two calls
uint64_t replay_hash(const RestartLoaded& rec, size_t turns) {
    assets::LevelData level;
    if (!level.load_from_file(maps_dir() + "/" + rec.head.map)) throw std::runtime_error("map");
    sim::SimulationEngine sim;
    sim.set_fog_of_war_enabled(rec.head.start.fog);
    sim.init(level, rec.head.start.seed, rec.head.start.roster);
    for (size_t i = 0; i < turns && i < rec.turns.size(); ++i) {
        for (const sim::Command& c : rec.turns[i].commands) sim.apply_command(c);
        sim.tick();
    }
    return sim.state_hash().total;
}

std::string status_json_text(PWorld& w, const std::string& code) {
    ctl::HttpRequest rq;
    rq.method = "GET";
    rq.path = "/rooms/" + code;
    return handle_control(*w.mgr, rq, w.server_now()).body;
}

ctl::JsonValue status_json(PWorld& w, const std::string& code) {
    ctl::JsonValue j;
    std::string why;
    ctl::parse_json(status_json_text(w, code), j, &why);
    return j;
}

}  // namespace

void run_persist_server_tests() {
    TEST_CASE("S3.84 A Match Survives A Restart Of The Server (SIGTERM): The Record Holds Every Turn That Was Sealed And Has The Keys Of The Seats; After The Restart The Room Is Back With Its Code, Its Turns Replayed (The State Hash Is That Of An Independent Replay And The Stored Checkpoints Are The Machines' Own Hashes), Every Seat Held And The Match Paused; The Three Machines Come Back By Themselves, The Match Goes On And Ends Identical On Them And On The Referee, And The Record Is Gone") {
        PWorld w("persist-78");
        w.start_server(500);
        const RoomSpec spec = held_spec("P-1", 3);
        std::vector<RClient*> m = play_room(w, spec, 30000);
        RoomStatus s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && !s.paused && s.record_kept && s.record_bytes > 0 && s.record_note.empty() && !s.restored);
        ASSERT_EQ(w.record_files().size(), size_t{1});
        RestartLoaded rec = w.read_record("P-1");
        ASSERT_TRUE(rec.ok() && !rec.torn && rec.head.code == "P-1" && rec.head.map == "TINY.LVL" && rec.head.players == 3);
        ASSERT_EQ(rec.turns.size(), static_cast<size_t>(s.turns));                           // every turn that the room has sealed is in the record: written before it was sent
        ASSERT_TRUE(rec.turns.size() > 590 && rec.checks.size() >= 29);
        ASSERT_EQ(s.record_bytes, rec.file_bytes);
        for (RClient* p : m) {                                                               // the keys that the players hold are the ones in the record, and nobody else's
            const uint8_t seat = p->lobby->my_seat();
            ASSERT_TRUE(rec.head.keys[seat] == p->lobby->key() && !net::key_is_zero(rec.head.keys[seat]));
            ASSERT_TRUE(rec.head.start.names[seat] == p->name);
        }
        for (const RestartCheck& c : rec.checks) {                                           // the checkpoints are the machines' own states: the referee's hash and the three machines' agree
            for (RClient* p : m) {
                const auto at = p->hash_at.find(uint64_t{c.turn} + 1);
                if (at != p->hash_at.end()) ASSERT_EQ(at->second, c.hash);
            }
        }
        size_t compared = 0;
        for (const RestartCheck& c : rec.checks) compared += m[0]->hash_at.count(uint64_t{c.turn} + 1);
        ASSERT_TRUE(compared >= 10);
        // ---- the server is told to stop (SIGTERM): the record is made durable and stays; the machines lose their links and look for the server ------------------------------------------
        const uint32_t sealed_at_stop = s.turns;
        w.stop_server(true);
        ASSERT_EQ(w.kept_at_stop, size_t{1});
        ASSERT_TRUE(fs::exists(w.record_path("P-1")));
        w.run(3000);
        for (RClient* p : m) ASSERT_TRUE(p->session->reconnecting() && !p->lost);            // (they retry every 2 s: the server is not there)
        rec = w.read_record("P-1");
        ASSERT_TRUE(rec.ok() && rec.turns.size() == sealed_at_stop);                         // nothing was lost, nothing was added
        // ---- the server starts again (its uptime is 0.5 s): the room is back ------------------------------------------------------------------------------------------------------
        w.start_server(500);
        ASSERT_EQ(w.report.items.size(), size_t{1});
        ASSERT_TRUE(w.report.items[0].outcome == RestoreItem::Outcome::Restored && w.report.items[0].code == "P-1" && w.report.items[0].turns == sealed_at_stop);
        ASSERT_TRUE(w.report.items[0].note.find("restored") != std::string::npos && w.report.items[0].note.find("3 seat(s) waiting") != std::string::npos);
        s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.restored_turns == sealed_at_stop && s.turns == sealed_at_stop && s.ticks == sealed_at_stop);
        ASSERT_TRUE(s.paused && s.absent.size() == 3 && s.joined == 3 && s.reconnect && s.record_kept && s.record_note.empty());
        ASSERT_TRUE(s.names[0] == "P0" && s.names[1] == "P1" && s.names[2] == "P2");
        ASSERT_EQ(s.restored_hash, replay_hash(rec, rec.turns.size()));                      // the state that the room stands at is that of an independent replay of the record
        ASSERT_TRUE(s.restore_ms < 5000);
        ASSERT_EQ(w.record_files().size(), size_t{1});                                       // the same record: the room goes on writing it
        {
            const ctl::JsonValue j = status_json(w, "P-1");                                   // the control interface says so
            ASSERT_TRUE(j.get("state").str() == "running" && j.get("paused").as_bool_or(false) && j.get("absent").size() == 3);
            ASSERT_TRUE(j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) == sealed_at_stop && j.get("restored").get("state_hash").str().size() == 16);
            ASSERT_TRUE(j.get("record").get("kept").as_bool_or(false) && j.get("record").get("bytes").as_int_or(0) > 0);
        }
        // ---- the three machines find the server again, with their keys, and the match goes on ----------------------------------------------------------------------------------
        ASSERT_TRUE(w.until([&]() { return !w.status("P-1").paused; }, 90000));
        s = w.status("P-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 3 && s.absent.empty() && s.drops_by_cap == 0 && s.drops_by_vote == 0);
        ASSERT_TRUE(w.until([&]() { return m[0]->session->mode() == net::ClientSession::Mode::Normal && m[1]->session->mode() == net::ClientSession::Mode::Normal && m[2]->session->mode() == net::ClientSession::Mode::Normal; }, 3000));
        for (RClient* p : m) ASSERT_FALSE(p->lost);
        w.run(8000);
        ASSERT_TRUE(w.status("P-1").turns > sealed_at_stop + 100);                           // the match goes on: new turns are sealed (and written: the same record grows)
        ASSERT_TRUE(w.read_record("P-1").turns.size() > sealed_at_stop + 100);
        w.play_to_the_end("P-1");
        const RoomStatus end = w.status("P-1");
        ASSERT_TRUE(end.state == RoomState::Finished && end.restored);
        ASSERT_TRUE(m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[1]->sim.state_hash() == m[2]->sim.state_hash());
        ASSERT_EQ(m[0]->sim.state_hash().total, end.referee_hash);                           // the machines that lived through the restart and the restored referee: one state at the end
        ASSERT_TRUE(m[0]->sim.is_match_over());
        for (RClient* p : m) ASSERT_FALSE(p->session->desynced() || p->lost);
        ASSERT_TRUE(w.record_files().empty());                                               // the match is over: its record is gone
        ASSERT_TRUE(!end.record_kept && end.record_note == "the room is over");
        ASSERT_TRUE(w.mgr->take_ended(w.server_now()).size() == 1);
    } TEST_END();

    TEST_CASE("S3.85 What Cannot Be Restored Is Said, And The Room Is A Failed Room With The Reason (Status, JSON, The Log's Report): Another Game Version, Another Protocol, A Map That Changed Or Is Gone, A Record That Is Older Than An Hour, A Replay That Does Not Agree With The Stored Hash (A Tampered Checkpoint, A Tampered Turn), A Room That This Server Would Not Make, A Replay That Takes Too Long, A Turn Log That Cannot Hold The Match, No Room For It; Each Record Is Deleted, A Match That Is Within Its Hour Is Restored, The Failed Room Keeps Its Code Until Its Keep Time Is Over And Says NoSuchRoom To A Hello")  {
        struct Refusal {
            const char* what;
            std::function<void(PWorld&)> before_restart;           // what changes between the crash and the new server
            const char* in_reason;                                  // a word that the reason must hold
        };
        const auto crash_with_record = [](PWorld& w, const char* code, uint32_t play_ms) {
            RoomSpec spec = held_spec(code, 2);
            spec.keep_ms = 8000;
            std::vector<RClient*> m = play_room(w, spec, play_ms);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);                                               // a crash
        };
        // rewrites a record: the head replaced by a changed one (the frames after it are kept as they are)
        const auto change_head = [](PWorld& w, const char* code, const std::function<void(RestartHead&)>& edit) {
            std::vector<uint8_t> bytes = record_bytes(w, code);
            const auto frames = frames_of(bytes);
            RestartLoaded rec = parse_restart_record(bytes.data(), bytes.size());
            edit(rec.head);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(kRestartMagic)));
            const std::vector<uint8_t> head = encode_restart_head(rec.head);
            out.insert(out.end(), head.begin(), head.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
        };
        // rewrites one frame's payload (and its checksum): a record that is whole as far as the format goes and wrong in what it says
        const auto change_frame = [](PWorld& w, const char* code, size_t index, const std::function<void(std::vector<uint8_t>&)>& edit) {
            std::vector<uint8_t> bytes = record_bytes(w, code);
            const auto frames = frames_of(bytes);
            std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].second - 4));
            edit(payload);
            const std::vector<uint8_t> frame = test_frame(bytes[frames[index].first], payload);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].first));
            out.insert(out.end(), frame.begin(), frame.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[index].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
        };
        const std::vector<Refusal> cases = {
            {"another protocol", [](PWorld& w) { w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1); }, "protocol"},
            {"the map file changed", [](PWorld& w) { std::ofstream(fs::path(w.maps) / "TINY.LVL", std::ios::binary | std::ios::app).put('x'); }, "not the one"},
            {"the map is gone", [](PWorld& w) { fs::remove(fs::path(w.maps) / "TINY.LVL"); }, "not on this server"},
            {"older than an hour", [](PWorld& w) { fs::last_write_time(w.record_path("R-1"), fs::file_time_type::clock::now() - std::chrono::hours(2)); }, "given up"},
            {"a replay that takes too long", [](PWorld& w) { w.restart.replay_budget_ms = 0; }, "longer than"},
        };
        for (const Refusal& c : cases) {
            // a maps folder of this test's own, so that a map can change
            const std::string maps_copy = (fs::path(temp_dir_for("persist-79-maps")) / "Maps").string();
            fs::create_directories(maps_copy);
            fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(maps_copy) / "TINY.LVL", fs::copy_options::overwrite_existing);
            PWorld w("persist-79", ServerLimits(), maps_copy);
            // (the restore's clock is this world's own, 1 ms a read: the real one stamped the failed room ahead of the world's time on a slow machine, and the keep
            // time read that as long over; a replay still outlasts a budget of 0)
            w.restart.clock_ms = [&w]() { w.now += 1; return w.server_now(); };
            w.start_server(500);
            crash_with_record(w, "R-1", 8000);
            ASSERT_MSG(w.record_files().size() == 1, c.what);
            c.before_restart(w);
            w.start_server(500);
            ASSERT_MSG(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended, c.what);
            ASSERT_MSG(w.report.items[0].note.find(c.in_reason) != std::string::npos, std::string(c.what) + ": " + w.report.items[0].note);
            ASSERT_MSG(w.record_files().empty(), c.what);                              // the record is deleted
            const RoomStatus s = w.status("R-1");
            ASSERT_MSG(s.state == RoomState::Failed && s.reason == w.report.items[0].note && s.ticks > 100 && !s.record_kept && !s.restored && s.joined == 2, c.what);
            ASSERT_MSG(s.names[0] == "P0" && s.names[1] == "P1" && s.expected == 2 && s.map == "TINY.LVL", c.what);
            const ctl::JsonValue j = status_json(w, "R-1");
            ASSERT_MSG(j.get("state").str() == "failed" && j.get("reason").str().find(c.in_reason) != std::string::npos && j.get("restored").is_null(), c.what);
            bool saw_notice = false;
            for (const std::string& n : w.notices) saw_notice = saw_notice || (n.find("R-1") != std::string::npos && n.find("not restored") != std::string::npos);
            ASSERT_MSG(saw_notice, c.what);
            const std::vector<RoomStatus> ended = w.mgr->take_ended(w.server_now());     // the end is reported once, like any room's: the log line and the result file
            ASSERT_MSG(ended.size() == 1 && ended[0].code == "R-1" && ended[0].state == RoomState::Failed, c.what);
            ASSERT_MSG(w.mgr->take_ended(w.server_now()).empty(), c.what);
            // its code is taken until its keep time is over (8 s), a Hello for it says that there is no such room, and then a new room can have the code
            net::Connection* hello_link = w.open_link();
            net::HelloMsg h;
            h.room = "R-1";
            hello_link->send(net::encode(h));
            w.run(500);
            ASSERT_MSG(reject_on(hello_link) == static_cast<int>(net::RejectReason::NoSuchRoom), c.what);
            ASSERT_MSG(!w.mgr->create_room(held_spec("R-1", 2), w.server_now()).ok, c.what);
            w.run(9000);
            RoomStatus gone;
            ASSERT_MSG(!w.mgr->status("R-1", gone, w.server_now()), c.what);
            fs::copy_file(maps_dir() + "/TINY.LVL", fs::path(w.maps) / "TINY.LVL", fs::copy_options::overwrite_existing);          // (the map is as it was: a room needs it)
            ASSERT_MSG(w.mgr->create_room(held_spec("R-1", 2), w.server_now()).ok, c.what);
        }
        {   // M4 of the review: the game version moves with every release, the protocol with every change of the rules: a record of another game version and build and the same protocol IS restored, with the
            // checkpoint-verified replay as the safety net (and the log says who wrote it); another protocol is refused (the cases above)
            PWorld w("persist-79v");
            w.start_server(500);
            crash_with_record(w, "R-6", 8000);
            w.restart.identity.game_version = "v9.9.9";
            w.restart.identity.build_id = "other-build";
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("R-6").restored && w.status("R-6").state == RoomState::Running);
            ASSERT_TRUE(w.report.items[0].note.find("written by " + std::string(kTestVersion)) != std::string::npos);
        }
        {   // a record that is within its hour is restored (59 minutes old)
            PWorld w("persist-79b");
            w.start_server(500);
            crash_with_record(w, "R-2", 8000);
            fs::last_write_time(w.record_path("R-2"), fs::file_time_type::clock::now() - std::chrono::minutes(59));
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("R-2").restored);
        }
        {   // a tampered checkpoint, and a tampered turn: the replay does not agree with what the old server saw
            PWorld w("persist-79c");
            w.start_server(500);
            crash_with_record(w, "R-3", 12000);
            change_frame(w, "R-3", [&]() -> size_t {                                    // (the frame of the first checkpoint)
                const auto frames = frames_of(record_bytes(w, "R-3"));
                for (size_t i = 1; i < frames.size(); ++i) {
                    if (record_bytes(w, "R-3")[frames[i].first] == 3) return i;
                }
                return 0;
            }(), [](std::vector<uint8_t>& p) { p[8] = static_cast<uint8_t>(p[8] ^ 1); });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended);
            ASSERT_TRUE(w.report.items[0].note.find("does not agree with the state hash") != std::string::npos && w.report.items[0].note.find("turn 19") != std::string::npos);
            ASSERT_TRUE(w.status("R-3").state == RoomState::Failed && w.record_files().empty());
        }
        {
            PWorld w("persist-79d");
            w.start_server(500);
            crash_with_record(w, "R-4", 12000);
            const RestartLoaded rec = w.read_record("R-4");
            // the first turn that holds a group move (a machine gives an order every 0.7 s from the end of the start dialog on, and the room discards what it is sent before its first turn: protocol 12)
            size_t move_turn = 0;
            while (move_turn < rec.turns.size()) {
                bool has_move = false;
                for (const sim::Command& c : rec.turns[move_turn].commands) has_move = has_move || c.type == sim::CommandType::GroupMove;
                if (has_move) break;
                ++move_turn;
            }
            ASSERT_TRUE(move_turn < 60);
            size_t frame_of_turn = 0;                                                   // (the record's frames: the head, one frame a turn, a checkpoint after every 20th turn)
            {
                const std::vector<uint8_t> bytes = record_bytes(w, "R-4");
                const auto frames = frames_of(bytes);
                size_t turns_seen = 0;
                for (size_t i = 1; i < frames.size(); ++i) {
                    if (bytes[frames[i].first] != 2) continue;
                    if (turns_seen++ == move_turn) {
                        frame_of_turn = i;
                        break;
                    }
                }
            }
            ASSERT_TRUE(frame_of_turn != 0);
            change_frame(w, "R-4", frame_of_turn, [](std::vector<uint8_t>& p) { p.resize(8); p[6] = 0; p[7] = 0; });   // that turn without its commands (first turn, count 1, then a command count of 0)
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended);
            ASSERT_TRUE(w.report.items[0].note.find("does not agree") != std::string::npos);
        }
        {   // a head that this server would not make (a wait of 0 s), a log that cannot hold the match, a budget for logs that cannot, and no room for the room
            PWorld w("persist-79e");
            w.start_server(500);
            crash_with_record(w, "R-5", 20000);
            change_head(w, "R-5", [](RestartHead& h) { h.wait_ms = 0; });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("would not make") != std::string::npos);
        }
        {
            PWorld w("persist-79f");
            w.start_server(500);
            crash_with_record(w, "R-6", 20000);
            change_head(w, "R-6", [](RestartHead& h) { h.max_log_bytes = 1024; });
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("turn log cannot hold") != std::string::npos);
            ASSERT_EQ(w.mgr->log_bytes(), uint64_t{0});                                 // (the half-built room gave its log back)
        }
        {
            PWorld w("persist-79g");
            w.start_server(500);
            crash_with_record(w, "R-7", 20000);
            w.limits.log_budget_bytes = 1000;                                            // the server's memory for logs cannot hold even the first allocation of one
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.report.items[0].note.find("turn log cannot hold") != std::string::npos);
        }
        {   // two records and room for one room: one is restored, the other is ended with the reason (and has no room to show it in: it is only in the log)
            PWorld w("persist-79h");
            w.start_server(500);
            RoomSpec one = held_spec("R-8", 2);
            RoomSpec two = held_spec("R-9", 2);
            ASSERT_TRUE(w.mgr->create_room(one, w.server_now()).ok && w.mgr->create_room(two, w.server_now()).ok);
            for (const char* code : {"R-8", "R-9"}) {
                for (int p = 0; p < 2; ++p) w.connect(std::string("Q") + std::to_string(p), code).reconnects = false;
            }
            w.run(8000 + kPre);
            ASSERT_TRUE(w.status("R-8").state == RoomState::Running && w.status("R-9").state == RoomState::Running);
            w.stop_server(false);
            w.limits.max_rooms = 1;
            w.start_server(500);
            ASSERT_EQ(w.report.items.size(), size_t{2});
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Ended) == 1);
            ASSERT_EQ(w.mgr->room_count(), size_t{1});
            ASSERT_TRUE(w.record_files().size() == 1);                                   // the restored room's record stays, the other is deleted
        }
    } TEST_END();
}


void run_persist_server_tests_2() {
    TEST_CASE("S3.86 Files That Are No Records Are Refused Safely At The Start (An Empty File, Garbage, A Head That Is Cut Short, A Record Of Another Format, A Record With A Flipped Bit In The Middle, A Text, A File Bigger Than A Record May Be, A Link; A Folder With A Record's Name Is Not Looked At): Each Is Named In The Log With A Reason And Deleted, Nothing Crashes Or Waits, The Good Record Next To Them Is Restored, The Files That Are Not Records' Names Are Left Alone, And The Server Then Makes Rooms As Usual") {
        PWorld w("persist-80");
        w.start_server(500);
        {
            RoomSpec spec = held_spec("G-1", 2);
            std::vector<RClient*> m = play_room(w, spec, 8000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
        }
        const fs::path dir = w.restart.dir;
        const std::vector<uint8_t> good = record_bytes(w, "G-1");
        const auto frames = frames_of(good);
        const auto plant = [&](const std::string& name, const std::vector<uint8_t>& bytes) { write_all_bytes(dir / name, bytes); };
        plant("room-EMPTY-00000001.restart", {});
        {
            std::vector<uint8_t> junk;
            uint32_t rng = 777;
            for (int i = 0; i < 500; ++i) {
                rng = rng * 1664525u + 1013904223u;
                junk.push_back(static_cast<uint8_t>(rng >> 16));
            }
            plant("room-JUNK-00000002.restart", junk);
        }
        plant("room-CUT-00000003.restart", std::vector<uint8_t>(good.begin(), good.begin() + 60));                            // the head is cut short
        {
            std::vector<uint8_t> v2 = good;
            v2[sizeof(kRestartMagic) - 1] = '2';
            plant("room-FORMAT2-00000004.restart", v2);
        }
        {
            std::vector<uint8_t> rot = good;
            rot[(frames[3].first + frames[3].second) / 2] ^= 0x40;                                                              // a bit flipped inside the third turn's frame
            plant("room-BITROT-00000005.restart", rot);
        }
        plant("room-TEXT-00000006.restart", std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'});
        fs::create_directories(dir / "room-DIR-00000007.restart");
        {
            std::ofstream out(dir / "room-HUGE-00000008.restart", std::ios::binary);
            out.put('x');
        }
        fs::resize_file(dir / "room-HUGE-00000008.restart", 60ull * 1024 * 1024);                                               // more than the 48 MiB of a record
#ifndef _WIN32
        fs::create_symlink(w.record_path("G-1"), dir / "room-LINK-00000009.restart");
#endif
        plant("notes.txt", {'n'});
        plant("other.restart", good);                                                                                           // (a name that is not a record's: no "room-" in front)
        w.start_server(500);
#ifndef _WIN32
        const size_t bad = 8;
#else
        const size_t bad = 7;
#endif
        ASSERT_EQ(w.report.items.size(), 1 + bad);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Unreadable) == bad && w.report.count(RestoreItem::Outcome::Ended) == 0);
        for (const RestoreItem& i : w.report.items) {
            if (i.outcome == RestoreItem::Outcome::Restored) continue;
            ASSERT_MSG(!i.note.empty() && i.code.empty() && i.file.rfind("room-", 0) == 0, i.file);
            bool logged = false;
            for (const std::string& n : w.notices) logged = logged || (n.find(i.file) != std::string::npos && n.find(i.note) != std::string::npos);
            ASSERT_MSG(logged, i.file);                                                                                         // named in the log, with the reason
            ASSERT_MSG(!fs::exists(dir / i.file) && !fs::is_symlink(dir / i.file), i.file);                                     // and deleted
        }
        ASSERT_TRUE(fs::exists(dir / "room-DIR-00000007.restart") && fs::is_directory(dir / "room-DIR-00000007.restart"));      // a folder is no record: left alone
        ASSERT_TRUE(fs::exists(dir / "notes.txt") && fs::exists(dir / "other.restart"));
        ASSERT_TRUE(w.status("G-1").restored && w.status("G-1").state == RoomState::Running);
        ASSERT_TRUE(fs::exists(w.record_path("G-1")));                                                                          // (the link pointed at the good record: the link went, the record stayed)
        for (const RestoreItem& i : w.report.items) {                                                                           // the reasons are of the kinds that they are
            if (i.file.find("EMPTY") != std::string::npos || i.file.find("TEXT") != std::string::npos) ASSERT_TRUE(i.note.find("record") != std::string::npos);
            if (i.file.find("FORMAT2") != std::string::npos) ASSERT_TRUE(i.note.find("another format") != std::string::npos);
            if (i.file.find("BITROT") != std::string::npos) ASSERT_TRUE(i.note.find("corrupt") != std::string::npos && i.note.find("checksum") != std::string::npos);
            if (i.file.find("HUGE") != std::string::npos) ASSERT_TRUE(i.note.find("too big") != std::string::npos);
            if (i.file.find("CUT") != std::string::npos) ASSERT_TRUE(i.note.find("corrupt") != std::string::npos);
        }
        // the server goes on as usual
        ASSERT_TRUE(w.mgr->create_room(held_spec("N-1", 2), w.server_now()).ok);
        std::vector<RClient*> fresh;
        fresh.push_back(&w.connect("Ann", "N-1"));
        fresh.push_back(&w.connect("Bob", "N-1"));
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("N-1").state == RoomState::Running && w.status("N-1").record_kept);
    } TEST_END();

    TEST_CASE("S3.87 A Record Whose Last Frame Was Cut Short (A Write That The Death Of The Machine Interrupted) Is Read As Far As It Is Whole, The Torn Bytes Are Cut Off And The Room Goes On Writing After The Last Good Frame; A Record That Lacks The Last Seconds (The Machine Died And The Disk Never Had Them) Is Restored To That Tick, And The Machines That Were Ahead Of It Are Told BadRequest (They Must Start The Match From Nothing, Which They Can: Their Keys Are Good) And Do, And The Match Goes On To The End Identical") {
        {
            PWorld w("persist-81a");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("T-1", 2), 8000);
            const uint32_t sealed = w.status("T-1").turns;
            w.stop_server(false);
            std::vector<uint8_t> bytes = record_bytes(w, "T-1");
            const size_t whole = bytes.size();
            const std::vector<uint8_t> next = test_frame(2, turns_payload(sealed, {sample_turn(sealed)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);                                     // the first nine bytes of the next frame
            write_all_bytes(w.record_path("T-1"), bytes);
            ASSERT_TRUE(read_restart_record(w.record_path("T-1"), 1ull << 30).torn);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("T-1").restored_turns == sealed);
            ASSERT_EQ(fs::file_size(w.record_path("T-1")), whole);                                          // the torn bytes are cut off: the next frame follows the last whole one
            ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused; }, 60000));
            w.run(6000);
            const RoomStatus s = w.status("T-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.turns > sealed + 80 && s.rejoins == 2);
            const RestartLoaded rec = w.read_record("T-1");
            ASSERT_TRUE(rec.ok() && !rec.torn && rec.turns.size() == s.turns);                              // the record is whole again and has grown
            w.play_to_the_end("T-1");
            ASSERT_TRUE(w.status("T-1").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == w.status("T-1").referee_hash);
        }
        {
            PWorld w("persist-81b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("T-2", 2), 8000);
            const uint32_t sealed = w.status("T-2").turns;
            const net::SeatKey key0 = m[0]->lobby->key();
            const net::SeatKey key1 = m[1]->lobby->key();
            const uint8_t seat0 = m[0]->lobby->my_seat();
            const uint8_t seat1 = m[1]->lobby->my_seat();
            ASSERT_TRUE(m[0]->session->runner().next_turn_expected() + 3 >= sealed);                       // the machines have (nearly) every turn of the room
            w.stop_server(false);
            // the disk never had the last ten turns: the record is cut back to a frame boundary that holds `sealed - 10` turns
            const std::vector<uint8_t> bytes = record_bytes(w, "T-2");
            const auto frames = frames_of(bytes);
            size_t turns_seen = 0;
            size_t cut = frames[0].second;
            for (size_t i = 1; i < frames.size(); ++i) {
                if (bytes[frames[i].first] == 2) {
                    if (turns_seen == sealed - 10) break;
                    ++turns_seen;
                }
                cut = frames[i].second;
            }
            write_all_bytes(w.record_path("T-2"), std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)));
            const RestartLoaded rolled_back = w.read_record("T-2");
            ASSERT_TRUE(rolled_back.ok() && rolled_back.turns.size() == sealed - 10);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("T-2").restored_turns == sealed - 10);
            w.run(8000);                                                                                   // the machines try: they are ahead of the record
            for (RClient* p : m) ASSERT_TRUE(p->lost && p->was_rejected && p->rejected == net::RejectReason::BadRequest);     // told so (more turns than were ever sealed)
            ASSERT_TRUE(w.status("T-2").paused && w.status("T-2").absent.size() == 2 && w.status("T-2").rejoins == 0 && w.status("T-2").rejoins_refused == 0);        // (the seats are held, the refusal cost them nothing)
            // they start the match from nothing, with their keys (what a client that finds itself ahead of a restarted server has to do)
            RClient& a2 = w.connect("P0", "T-2", seat0, key0);
            RClient& b2 = w.connect("P1", "T-2", seat1, key1);
            ASSERT_TRUE(w.until([&]() { return !w.status("T-2").paused && a2.session != nullptr && b2.session != nullptr && a2.session->mode() == net::ClientSession::Mode::Normal && b2.session->mode() == net::ClientSession::Mode::Normal; }, 60000));
            w.run(5000);
            w.play_to_the_end("T-2");
            const RoomStatus end = w.status("T-2");
            ASSERT_TRUE(end.state == RoomState::Finished && end.rejoins == 2);
            ASSERT_TRUE(a2.sim.state_hash() == b2.sim.state_hash() && a2.sim.state_hash().total == end.referee_hash && a2.sim.is_match_over());
            ASSERT_FALSE(a2.session->desynced() || b2.session->desynced() || a2.lost || b2.lost);
        }
    } TEST_END();

    TEST_CASE("S3.88 The Record Always Holds What Any Machine Has Run (Written Before The Turn Is Sent): In A Match Of Three Players, At Every 10 ms Step Of 30 s Of Play The File Holds Exactly The Turns That The Room Has Sealed, And No Machine Has Received A Turn That The File Lacks (So A Process That Is Killed At Any Moment Loses Nothing That A Player Has Seen)") {
        PWorld w("persist-82");
        w.start_server(500);
        ASSERT_TRUE(w.mgr->create_room(held_spec("W-1", 3), w.server_now()).ok);
        std::vector<RClient*> m;
        for (int p = 0; p < 3; ++p) m.push_back(&w.connect(std::string("P") + std::to_string(p), "W-1"));
        uint32_t checks = 0;
        uint32_t most_turns = 0;
        for (int step = 0; step < 3500; ++step) {
            w.run(10);
            const RoomStatus s = w.status("W-1");
            if (s.state != RoomState::Running) continue;
            const RestartLoaded rec = w.read_record("W-1");                                                // (read with no flush and no stop: whatever the process has handed to the operating system)
            ASSERT_TRUE(rec.ok() && !rec.torn);
            ASSERT_EQ(rec.turns.size(), static_cast<size_t>(s.turns));                                    // everything that was sealed is in the file ...
            for (RClient* p : m) {
                if (p->session == nullptr) continue;
                ASSERT_TRUE(p->session->runner().next_turn_expected() <= rec.turns.size());                // ... and no machine has a turn that the file lacks
            }
            most_turns = s.turns;
            ++checks;
        }
        ASSERT_TRUE(checks > 2500 && most_turns > 400);
    } TEST_END();
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

void run_persist_server_tests_3() {
    TEST_CASE("S3.89 The Disk Has Limits And The Match Does Not Suffer From Them: A Record That Passes The Size Of A Record, One That The Server's Budget Cannot Hold (At Its Head, And As It Grows), A Write That The Disk Refuses (EFBIG), A Turn Log That Passed Its Limit: Each Ends The Record (The File Is Deleted: A Record That Stops In The Middle Of A Match Would Bring It Back At The Wrong Tick), The Room's Status Says Why, The Log Gets A Line, The Budget Is Whole Again And The Match Goes On To Its End On The Same State Everywhere")  {
        {   // the size of one record
            PWorld w("persist-83a");
            w.restart.max_record_bytes = 3000;
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("D-1", 2), 14000);                // 280 turns: a record of 4 KB
            const RoomStatus s = w.status("D-1");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_bytes == 0 && s.record_note.find("size that a record may have") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-1") != std::string::npos && n.find("restart record is gone") != std::string::npos && n.find("size that a record may have") != std::string::npos);
            ASSERT_TRUE(noted);
            ASSERT_EQ(w.mgr->restart_store()->records_failed(), 1u);
            ASSERT_TRUE(w.mgr->restart_store()->used_bytes() == 0 && w.mgr->restart_store()->open_records() == 0);
            ASSERT_TRUE(status_json(w, "D-1").get("record").get("kept").as_bool_or(true) == false && status_json(w, "D-1").get("record").get("note").str() == s.record_note);
            w.play_to_the_end("D-1");                                                          // the match is not touched by it
            ASSERT_TRUE(w.status("D-1").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == w.status("D-1").referee_hash);
        }
        {   // the server's budget: a second room cannot even make its head, the first one's record ends when it outgrows what is left, and the budget is whole afterwards
            uint64_t head_bytes = 0;
            {
                PWorld probe("persist-83b-probe");
                probe.start_server(500);
                ASSERT_TRUE(probe.mgr->create_room(held_spec("B-0", 2), probe.server_now()).ok);
                probe.connect("A", "B-0");
                probe.connect("B", "B-0");
                ASSERT_TRUE(probe.until([&]() { return probe.status("B-0").state == RoomState::Running; }, 10000));
                head_bytes = probe.status("B-0").record_bytes;                                 // (a record that holds a head and nothing else)
                ASSERT_TRUE(head_bytes > 100 && head_bytes < 1000);
            }
            PWorld w("persist-83b");
            w.restart.budget_bytes = 2 * head_bytes - 20;
            w.start_server(500);
            for (const char* code : {"B-1", "B-2"}) {
                ASSERT_TRUE(w.mgr->create_room(held_spec(code, 2), w.server_now()).ok);
                w.connect("A", code);
                w.connect("B", code);
            }
            ASSERT_TRUE(w.until([&]() { return w.status("B-1").state == RoomState::Running && w.status("B-2").state == RoomState::Running; }, 10000));
            w.run(1500);                                                                       // (before the first turn of either: the 5 s of the dialog)
            const RoomStatus first = w.status("B-1");
            const RoomStatus second = w.status("B-2");
            ASSERT_TRUE(first.record_kept && first.record_bytes == head_bytes);
            ASSERT_TRUE(!second.record_kept && second.record_note.find("no restart record could be made") != std::string::npos && second.record_note.find("budget") != std::string::npos);
            ASSERT_EQ(w.record_files().size(), size_t{1});
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("B-2") != std::string::npos && n.find("budget") != std::string::npos);
            ASSERT_TRUE(noted);
            w.run(8000);                                                                       // the first room's record outgrows the budget: it ends (and its bytes come back)
            const RoomStatus later = w.status("B-1");
            ASSERT_TRUE(later.state == RoomState::Running && !later.record_kept && later.record_note.find("budget") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty() && w.mgr->restart_store()->used_bytes() == 0);
            ASSERT_TRUE(w.mgr->create_room(held_spec("B-3", 2), w.server_now()).ok);          // and a room that is made now has the budget again
            w.connect("A", "B-3");
            w.connect("B", "B-3");
            ASSERT_TRUE(w.until([&]() { return w.status("B-3").state == RoomState::Running; }, 10000));
            ASSERT_TRUE(w.status("B-3").record_kept);
        }
#ifndef _WIN32
        {   // a write that the disk refuses (the process may not grow a file beyond 2500 bytes: EFBIG, as for a full disk)
            PWorld w("persist-83c");
            w.start_server(500);
            std::vector<RClient*> m;
            {
                const FileSizeLimit limit(2500);
                m = play_room(w, held_spec("D-3", 2), 14000);
            }
            const RoomStatus s = w.status("D-3");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_note.find("the disk refused a write") != std::string::npos && s.record_note.find("File too large") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());                                             // the file that stopped in the middle of the match is deleted
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-3") != std::string::npos && n.find("the disk refused a write") != std::string::npos);
            ASSERT_TRUE(noted && w.mgr->restart_store()->records_failed() == 1 && w.mgr->restart_store()->used_bytes() == 0);
            w.play_to_the_end("D-3");
            ASSERT_TRUE(w.status("D-3").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash());
        }
#endif
        {   // the turn log of the room passed its limit: the room can no longer give the match to a player who comes back, so a restart could not hold its seats: the record ends
            PWorld w("persist-83d");
            w.start_server(500);
            RoomSpec spec = held_spec("D-4", 2);
            spec.max_log_bytes = 1536;
            std::vector<RClient*> m = play_room(w, spec, 40000);
            const RoomStatus s = w.status("D-4");
            ASSERT_TRUE(s.state == RoomState::Running && !s.log_usable && !s.record_kept && s.record_note.find("turn log") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("D-4") != std::string::npos && n.find("turn log") != std::string::npos);
            ASSERT_TRUE(noted);
            w.play_to_the_end("D-4");
            ASSERT_TRUE(w.status("D-4").state == RoomState::Finished && m[0]->sim.state_hash() == m[1]->sim.state_hash());
        }
    } TEST_END();

    TEST_CASE("S3.90 A Record Lives As Long As Its Room Has A Match To Bring Back: A Waiting Room Has None; The Start Makes It And A Cancelled Start Deletes It (A Machine That Cannot Load The Map); A Running Room Has One Until The Owner Closes It (DELETE /rooms/<code>) Or The Match Ends; A Room That Holds No Seats Has None (The Status Says Why); A Server That Keeps No Records Says So; The Server's Stop Keeps The Records Of The Rooms That Have One And Closes The Others")  {
        {
            PWorld w("persist-84");
            w.start_server(500);
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-1", 2), w.server_now()).ok);
            RoomStatus s = w.status("C-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.record_kept && s.record_note.find("has not started") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            w.connect("Ann", "C-1");
            w.run(500);
            ASSERT_TRUE(w.record_files().empty());                                             // (one player: still waiting)
            RClient& bad = w.connect("Bob", "C-1");
            bad.fail_load = true;                                                              // Bob cannot load the map: the start is cancelled
            bool seen_while_loading = false;
            for (int i = 0; i < 400; ++i) {
                w.run(10);
                const RoomStatus now_status = w.status("C-1");
                if (now_status.state == RoomState::Loading) {
                    seen_while_loading = seen_while_loading || (now_status.record_kept && w.record_files().size() == 1);
                }
                if (now_status.state == RoomState::Waiting && seen_while_loading) break;
            }
            ASSERT_TRUE(seen_while_loading);                                                   // the start made a record (the match is fixed: the start message, the keys) ...
            s = w.status("C-1");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.record_kept);
            ASSERT_TRUE(w.record_files().empty());                                             // ... and its cancel took it away again
            ASSERT_TRUE(s.record_note.find("has not started") != std::string::npos);
        }
        {   // a running room: the record until the owner closes the room
            PWorld w("persist-84b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("C-2", 2), 3000);
            ASSERT_TRUE(w.status("C-2").record_kept && w.record_files().size() == 1);
            ctl::HttpRequest rq;
            rq.method = "DELETE";
            rq.path = "/rooms/C-2";
            const ctl::HttpResponse r = handle_control(*w.mgr, rq, w.server_now());
            ASSERT_EQ(r.status, 200);
            ASSERT_TRUE(w.record_files().empty());                                             // closed by its owner: nothing to bring back
            const RoomStatus s = w.status("C-2");
            ASSERT_TRUE(s.state == RoomState::Failed && !s.record_kept && s.record_note == "the room is over");
            ASSERT_EQ(w.mgr->restart_store()->used_bytes(), uint64_t{0});
        }
        {   // a room that holds no seats: no record, and the status says why; the stop closes it
            PWorld w("persist-84c");
            w.start_server(500);
            RoomSpec spec = spec_of("C-3", 2);                                                  // (reconnect off)
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            w.connect("Ann", "C-3");
            w.connect("Bob", "C-3");
            w.run(3000 + kPre);
            RoomStatus s = w.status("C-3");
            ASSERT_TRUE(s.state == RoomState::Running && !s.reconnect && !s.record_kept && s.record_note.find("holds no seats") != std::string::npos);
            ASSERT_TRUE(w.record_files().empty());
            // and the stop: one room with a record and this one without
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-4", 2), w.server_now()).ok);
            w.connect("Cat", "C-4");
            w.connect("Dan", "C-4");
            w.run(3000 + kPre);
            ASSERT_TRUE(w.status("C-4").record_kept);
            w.stop_server(true);
            ASSERT_EQ(w.kept_at_stop, size_t{1});                                              // the record of the room that holds seats is kept ...
            ASSERT_TRUE(w.record_files().size() == 1 && fs::exists(w.record_path("C-4")));
            w.start_server(500);                                                               // ... and only that room is back
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].code == "C-4" && w.report.items[0].outcome == RestoreItem::Outcome::Restored);
            RoomStatus gone;
            ASSERT_FALSE(w.mgr->status("C-3", gone, w.server_now()));
        }
        {   // a server that keeps no records says so in every room's status
            PWorld w("persist-84d");
            w.restart.dir.clear();
            w.start_server(500);
            ASSERT_TRUE(w.mgr->restart_store() == nullptr);
            ASSERT_TRUE(w.mgr->create_room(held_spec("C-5", 2), w.server_now()).ok);
            RoomStatus s = w.status("C-5");
            ASSERT_TRUE(!s.record_kept && s.record_note == "this server keeps no restart records");
            w.connect("Ann", "C-5");
            w.connect("Bob", "C-5");
            w.run(3000 + kPre);
            s = w.status("C-5");
            ASSERT_TRUE(s.state == RoomState::Running && !s.record_kept && s.record_note == "this server keeps no restart records");
            ASSERT_EQ(w.mgr->shutdown(w.server_now()), size_t{0});                              // (and the stop closes its rooms, as it always did)
            ASSERT_TRUE(w.status("C-5").state == RoomState::Failed);
        }
    } TEST_END();

    TEST_CASE("S3.91 No Key Is In Any Text The Server Makes: The Record Holds The Keys (As Bytes: The Test Looks For Them There First), And They Are In No Notice For The Log, No Status (Running, Restored, Refused, Finished), No JSON Of The Control Interface, No List, No Result Of An Ended Room, Through A Restart And To The Match's End") {
        PWorld w("persist-85");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("K-1", 3), 6000);
        std::vector<net::SeatKey> keys;
        for (RClient* p : m) keys.push_back(p->lobby->key());
        const std::vector<uint8_t> bytes = record_bytes(w, "K-1");
        for (const net::SeatKey& k : keys) {                                                   // the keys ARE in the record: the search is not a search for nothing
            ASSERT_TRUE(!net::key_is_zero(k) && std::search(bytes.begin(), bytes.end(), k.begin(), k.end()) != bytes.end());
        }
        std::vector<std::string> texts;
        const auto collect = [&]() {
            for (const std::string& n : w.notices) texts.push_back(n);
            for (const char* code : {"K-1", "K-2"}) {
                RoomStatus s;
                if (w.mgr != nullptr && w.mgr->status(code, s, w.server_now())) {
                    texts.push_back(status_json_text(w, code));
                    texts.push_back(ctl::to_json(status_to_json(s)));
                }
            }
            if (w.mgr != nullptr) {
                ctl::HttpRequest rq;
                rq.method = "GET";
                rq.path = "/rooms";
                texts.push_back(handle_control(*w.mgr, rq, w.server_now()).body);
                rq.path = "/stats";
                texts.push_back(handle_control(*w.mgr, rq, w.server_now()).body);
            }
        };
        collect();
        w.stop_server(true);
        collect();
        w.start_server(500);                                                                   // (the restart: the room comes back; the log's lines about it are in w.notices)
        collect();
        ASSERT_TRUE(w.until([&]() { return !w.status("K-1").paused; }, 90000));
        w.run(3000);
        collect();
        // a second room whose record cannot be restored (another network protocol): the failed room and its report
        w.stop_server(false);
        w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
        w.start_server(500);
        collect();
        for (const RoomStatus& ended : w.mgr->take_ended(w.server_now())) texts.push_back(ctl::to_json(status_to_json(ended)));
        w.restart.identity.protocol = net::kProtocolVersion;
        size_t looked_at = 0;
        for (const std::string& t : texts) {
            for (const net::SeatKey& k : keys) ASSERT_TRUE(t.find(hex_of(k)) == std::string::npos);
            looked_at += t.size();
        }
        ASSERT_TRUE(texts.size() >= 14 && looked_at > 5000);
        // the record of a room is for its owner only
#ifndef _WIN32
        {
            PWorld p("persist-85b");
            p.start_server(500);
            std::vector<RClient*> q = play_room(p, held_spec("K-3", 2), 3000);
            struct stat st;
            ASSERT_EQ(::stat(p.record_path("K-3").c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
            ASSERT_EQ(::stat(p.restart.dir.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
        }
#endif
    } TEST_END();
}


void run_persist_server_tests_4() {
    TEST_CASE("S3.92 After A Restart The Room Waits For Its Players As For A Lost Link, With The Restart's Longer Wait: Of Three Players Two Come Back And One Never Does: No Vote Is Open At 30 s, None At 85 s, The Vote Opens At 90 s About The Seat That Is Missing (Two Voters), One Vote Of Two Does Not Win It And The Second Does, The Seat Is Dropped And The Match Goes On (A Quit Ends It), Whatever The New Server's Clock Is (0.5 s, A Second Before Its 32-Bit Wrap, Half Way); A Seat That Had Been Dropped Before The Restart Is Told So By Its Key; A Room Whose Players Never Come Back Ends 'Everybody Left' At The Cap")  {
        const uint32_t clocks[] = {500u, 0xFFFFFC18u, 0x7FFFFFF0u};
        for (const uint32_t clock : clocks) {
            PWorld w("persist-86");
            w.start_server(7000);
            const RoomSpec spec = held_spec("V-1", 3);
            std::vector<RClient*> m = play_room(w, spec, 6000);
            const uint8_t seat_a = m[0]->lobby->my_seat();
            const uint8_t seat_c = m[2]->lobby->my_seat();
            w.stop_server(false);
            m[2]->reconnects = false;                                                          // Cat's machine does not come back
            w.start_server(clock);
            const uint32_t restarted = w.now;
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1);
            ASSERT_TRUE(w.until([&]() { return w.status("V-1").rejoins == 2; }, 30000));
            const auto since_restart = [&]() { return w.now - restarted; };
            while (since_restart() < 40000) w.run(500);
            RoomStatus s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == seat_c && s.absent[0].away_s >= 38 && s.absent[0].away_s <= 42 && s.vote_seat == 255);       // 40 s: a lost link's 30 s are over, the restart's wait is not
            while (since_restart() < 85000) w.run(500);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.vote_seat == 255);
            while (since_restart() < 92000) w.run(500);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.vote_seat == seat_c && s.voters == 2 && s.votes_continue == 0);
            ASSERT_TRUE(m[0]->session->vote(seat_c, true));                                    // one vote of two: not more than half
            w.run(600);
            s = w.status("V-1");
            ASSERT_TRUE(s.paused && s.votes_continue == 1 && s.vote_seat == seat_c);
            ASSERT_TRUE(m[1]->session->vote(seat_c, true));                                    // two of two
            ASSERT_TRUE(w.until([&]() { return !w.status("V-1").paused; }, 5000));
            s = w.status("V-1");
            ASSERT_TRUE(s.drops_by_vote == 1 && s.drops_by_cap == 0 && s.absent.empty() && s.state == RoomState::Running);
            const uint32_t turns_then = s.turns;
            w.run(3000);
            ASSERT_TRUE(w.status("V-1").turns > turns_then + 40);                              // the match goes on without Cat
            sim::Command quit;
            quit.type = sim::CommandType::Quit;
            quit.issuer = seat_a;
            ASSERT_TRUE(m[0]->session->submit(quit));
            ASSERT_TRUE(w.until([&]() { return w.status("V-1").state == RoomState::Finished; }, 20000));
            w.run(3000);
            const RoomStatus end = w.status("V-1");
            ASSERT_TRUE(end.state == RoomState::Finished && end.quitter == seat_a && end.drops_by_vote == 1);
            ASSERT_TRUE(m[0]->sim.state_hash() == m[1]->sim.state_hash() && m[0]->sim.state_hash().total == end.referee_hash);
            ASSERT_TRUE(m[0]->sim.is_player_dropped(seat_c) && w.record_files().empty());
        }
        {   // a seat that had left before the restart is Dropped after it: its key is told so, and nobody waits for it
            PWorld w("persist-86b");
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("V-2", 3), 6000);
            const net::SeatKey key_b = m[1]->lobby->key();
            const uint8_t seat_b = m[1]->lobby->my_seat();
            m[1]->session->leave();                                                            // Bob quits: his Drop is sealed
            w.run(1500);
            ASSERT_TRUE(w.status("V-2").state == RoomState::Running && !w.status("V-2").paused);
            w.stop_server(false);
            w.start_server(500);
            RoomStatus s = w.status("V-2");
            ASSERT_TRUE(s.restored && s.paused && s.absent.size() == 2);                       // (Ann and Cat: Bob is gone for good)
            for (const RoomStatus::Absent& a : s.absent) ASSERT_TRUE(a.seat != seat_b);
            net::Connection* link = w.open_link();
            net::HelloMsg hello;
            hello.room = "V-2";
            hello.key = key_b;
            hello.have_turns = 0;
            link->send(net::encode(hello));
            w.run(600);
            ASSERT_EQ(reject_on(link), static_cast<int>(net::RejectReason::Dropped));          // "Sorry, you have been dropped from the game"
            ASSERT_TRUE(w.until([&]() { return !w.status("V-2").paused; }, 60000));            // the others come back: nobody waits for Bob
            ASSERT_TRUE(w.status("V-2").rejoins == 2 && w.status("V-2").state == RoomState::Running);
        }
        {   // nobody comes back: the room ends by the existing rules (the pause's cap: here 60 s), "everybody left"
            PWorld w("persist-86c");
            w.start_server(500);
            RoomSpec spec = held_spec("V-3", 2);
            spec.max_pause_ms = 60000;
            std::vector<RClient*> m = play_room(w, spec, 6000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
            w.start_server(500);
            w.run(58000);
            RoomStatus s = w.status("V-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.absent.size() == 2 && s.drops_by_cap == 0);
            w.run(4000);
            s = w.status("V-3");
            ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "everybody left" && s.drops_by_cap == 2 && s.drops_by_vote == 0);
            ASSERT_TRUE(w.record_files().empty() && w.mgr->take_ended(w.server_now()).size() == 1);
        }
    } TEST_END();

    TEST_CASE("S3.93 The Bots Of A Room Sit Down Again At The Restored Tick And Play On: A Room Of One Person And A Bot Of Its Specification, And A Room Whose Leader's START Filled The Empty Seats; After The Restart The Status Lists The Bots (Fill Marked As Fill), The Controller Is There, The Bot Is Never Absent (Only The Person Is), And When The Person Is Back The Bots Give Orders Again (Commands Of Their Seats In Turns After The Restored One); A Quit Ends The Match With The Same State On The Person's Machine And The Referee") {
        const auto commands_of_seat = [](const RestartLoaded& rec, size_t from, uint8_t seat) {
            size_t n = 0;
            for (size_t i = from; i < rec.turns.size(); ++i) {
                for (const sim::Command& c : rec.turns[i].commands) n += c.issuer == seat ? 1u : 0u;
            }
            return n;
        };
        {   // the room's own bot
            PWorld w("persist-87");
            w.start_server(500);
            RoomSpec spec = held_spec("B-1", 2);
            spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Medium});
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            RClient& person = w.connect("Pat", "B-1");
            person.record_hashes = true;
            w.run(14000 + kPre);
            RoomStatus s = w.status("B-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.bot_controller && s.bots.size() == 1 && s.bots[0].seat == 1 && s.bots[0].name == "Bot (Medium)" && !s.bots[0].fill && s.joined == 2 && s.record_kept);
            const uint8_t seat_person = person.lobby->my_seat();
            ASSERT_EQ(seat_person, 0);
            RestartLoaded before = w.read_record("B-1");
            ASSERT_TRUE(before.ok() && commands_of_seat(before, 0, 1) > 0 && s.bot_decisions >= 10);          // the bot gives orders (its harvest order, once: the ants go on by themselves)
            ASSERT_TRUE(before.head.bots.size() == 1 && before.head.fill_mask == 0 && before.head.keys[1] == net::SeatKey{});     // (no key for a bot's seat)
            const size_t sealed = before.turns.size();
            w.stop_server(false);
            w.start_server(500);
            s = w.status("B-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && s.bot_controller && s.bots.size() == 1 && s.bots[0].name == "Bot (Medium)" && s.joined == 2);
            ASSERT_TRUE(s.absent.size() == 1 && s.absent[0].seat == seat_person);              // only the person is missing: a bot is never absent
            ASSERT_TRUE(w.until([&]() { return !w.status("B-1").paused; }, 60000));
            w.run(14000);
            const RestartLoaded after = w.read_record("B-1");
            ASSERT_TRUE(after.ok() && after.turns.size() > sealed + 200);
            s = w.status("B-1");
            ASSERT_TRUE(s.bot_controller && s.bot_decisions >= 10);                            // the bot looks at the match again, from the restored tick on (a new controller: it counts from 0; a Medium bot looks every 20 ticks)
            sim::Command quit;
            quit.type = sim::CommandType::Quit;
            quit.issuer = seat_person;
            ASSERT_TRUE(person.session->submit(quit));
            ASSERT_TRUE(w.until([&]() { return w.status("B-1").state == RoomState::Finished; }, 20000));
            w.run(3000);
            ASSERT_TRUE(person.sim.state_hash().total == w.status("B-1").referee_hash && !person.session->desynced());
        }
        {   // a restart in the seconds before the first turn: the room comes back at turn 0, the bot acts as in any match that begins (its first look is on tick 1 + its seat, its order follows its reaction time),
            // and nothing of the pre-start is needed to bring the match back (a command that a machine gave in the dialog is in turn 0, or is not: the room restores sealed turns, nothing else)
            PWorld w("persist-87c");
            w.start_server(500);
            RoomSpec spec = held_spec("B-3", 2);
            spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Hard});
            ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
            RClient& person = w.connect("Pat", "B-3");
            ASSERT_TRUE(w.until([&]() { return w.status("B-3").state == RoomState::Running; }, 10000));
            w.run(2500);
            ASSERT_EQ(w.status("B-3").turns, 0u);                                              // (in the dialog: nothing is sealed yet)
            ASSERT_TRUE(w.status("B-3").record_kept && w.read_record("B-3").turns.empty());
            w.stop_server(false);
            w.start_server(500);
            RoomStatus s = w.status("B-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.restored_turns == 0 && s.turns == 0 && s.paused && s.bot_controller && s.absent.size() == 1);
            ASSERT_TRUE(w.until([&]() { return !w.status("B-3").paused; }, 60000));
            w.run(12000);
            const RestartLoaded after = w.read_record("B-3");
            ASSERT_TRUE(after.ok() && after.turns.size() > 200);
            ASSERT_TRUE(commands_of_seat(after, 0, 1) > 0);                                    // the bot's first order, as in any match
            ASSERT_TRUE(w.status("B-3").bot_decisions >= 10);
            ASSERT_TRUE(person.session->mode() == net::ClientSession::Mode::Normal && !person.lost);
        }
        {   // the leader's START with a fill: two bots that the room seated
            PWorld w("persist-87b");
            w.start_server(500);
            ASSERT_TRUE(w.mgr->create_room(held_spec("B-2", 3), w.server_now()).ok);
            RClient& leader = w.connect("Lea", "B-2");
            w.run(1500);
            ASSERT_TRUE(leader.end->send(net::encode(net::StartRequestMsg{net::FillLevel::Medium})));
            ASSERT_TRUE(w.until([&]() { return w.status("B-2").state == RoomState::Running; }, 20000));
            w.run(12000 + kPre);
            RoomStatus s = w.status("B-2");
            ASSERT_TRUE(s.bots.size() == 2 && s.bots[0].fill && s.bots[1].fill && s.joined == 3 && s.record_kept);
            const RestartLoaded before = w.read_record("B-2");
            ASSERT_TRUE(before.ok() && before.head.fill_mask == 0x06 && before.head.bots.size() == 2);
            const size_t sealed = before.turns.size();
            w.stop_server(true);
            w.start_server(500);
            s = w.status("B-2");
            ASSERT_TRUE(s.restored && s.bot_controller && s.bots.size() == 2 && s.bots[0].fill && s.bots[1].fill && s.bots[0].name == "Bot (Medium)" && s.joined == 3);
            ASSERT_TRUE(s.absent.size() == 1 && s.paused);
            ASSERT_TRUE(w.until([&]() { return !w.status("B-2").paused; }, 60000));
            w.run(12000);
            const RestartLoaded after = w.read_record("B-2");
            ASSERT_TRUE(after.ok() && after.turns.size() > sealed + 200);
            ASSERT_TRUE(w.status("B-2").bot_controller && w.status("B-2").bot_decisions >= 20);          // two bots look again, each every 20 ticks
        }
    } TEST_END();

    TEST_CASE("S3.94 A Match That Had Ended When The Server Stopped (Its Last Turn Was In The Record, The Room Had Not Finished Yet) Is Finished At The Restart With The Same Final State As The Match That Was Never Interrupted: The Result Rows, The Referee's Hash And The Report Of Its End Are There, Its Record Is Deleted, Nobody Is Waited For")  {
        PWorld w("persist-88");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("O-1", 2), 6000);
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        quit.issuer = m[1]->lobby->my_seat();
        ASSERT_TRUE(m[1]->session->submit(quit));
        std::vector<uint8_t> last_copy;                                                        // the record as it was at the last step before the room finished: it holds the turn of the Quit
        bool finished = false;
        for (int step = 0; step < 3000 && !finished; ++step) {
            w.run(10);
            if (w.status("O-1").state == RoomState::Finished) {
                finished = true;
            } else if (fs::exists(w.record_path("O-1"))) {
                last_copy = record_bytes(w, "O-1");
            }
        }
        ASSERT_TRUE(finished);
        const RoomStatus first_end = w.status("O-1");
        ASSERT_TRUE(first_end.referee_hash != 0 && first_end.quitter == quit.issuer && !first_end.rows.empty());
        ASSERT_TRUE(w.record_files().empty());
        ASSERT_FALSE(last_copy.empty());
        const RestartLoaded copy = parse_restart_record(last_copy.data(), last_copy.size());
        ASSERT_TRUE(copy.ok());
        bool quit_in_record = false;
        for (const net::TurnMsg& t : copy.turns) {
            for (const sim::Command& c : t.commands) quit_in_record = quit_in_record || c.type == sim::CommandType::Quit;
        }
        ASSERT_TRUE(quit_in_record);                                                           // (the copy was taken in the window between the Quit's turn and the end of the room)
        w.stop_server(false);
        write_all_bytes(w.record_path("O-1"), last_copy);                                      // the old server died in that window
        w.start_server(500);
        ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Restored && w.report.items[0].note.find("the match had ended") != std::string::npos);
        const RoomStatus s = w.status("O-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "the match had ended when the server stopped" && s.restored);
        ASSERT_EQ(s.referee_hash, first_end.referee_hash);                                     // the final state of the match that was never interrupted, hash for hash
        ASSERT_TRUE(s.quitter == first_end.quitter && s.rows.size() == first_end.rows.size() && s.rows[0].winner == first_end.rows[0].winner && s.rows[0].score == first_end.rows[0].score);
        ASSERT_TRUE(w.record_files().empty());                                                 // its record is deleted
        const std::vector<RoomStatus> ended = w.mgr->take_ended(w.server_now());               // and its end is reported, with the result file's content
        ASSERT_TRUE(ended.size() == 1 && ended[0].code == "O-1" && ended[0].state == RoomState::Finished);
        ASSERT_TRUE(status_to_json(ended[0]).get("state_hash").str().size() == 16);
    } TEST_END();

    TEST_CASE("S3.97 The Switch That Phase 2 Flips: Rooms Hold Seats By Default Exactly When kReconnectByDefault Says So (ServerLimits, A Room Specification Made By The Manager, A Room Made By The Control Interface Without A \"reconnect\" Key, A Demo Room), And Whatever The Default Is A Room's Own Key Wins; A Restored Room Holds Seats Whatever The Default Is Now (It Held Them When It Was Written)")  {
        ASSERT_EQ(ServerLimits().reconnect, kReconnectByDefault);
        World dflt;
        ASSERT_EQ(dflt.mgr.default_spec().reconnect, kReconnectByDefault);
        {   // the control interface
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-1\"}";
            ctl::HttpResponse r = handle_control(dflt.mgr, rq, dflt.now);
            ASSERT_EQ(r.status, 201);
            ASSERT_EQ(dflt.status("SW-1").reconnect, kReconnectByDefault);
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-2\", \"reconnect\": true}";
            ASSERT_EQ(handle_control(dflt.mgr, rq, dflt.now).status, 201);
            ASSERT_TRUE(dflt.status("SW-2").reconnect);
            rq.body = "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"SW-3\", \"reconnect\": false}";
            ASSERT_EQ(handle_control(dflt.mgr, rq, dflt.now).status, 201);
            ASSERT_FALSE(dflt.status("SW-3").reconnect);
        }
        {   // a demo room follows the default
            ServerLimits l;
            l.demo_rooms = 2;
            l.demo_map = "TINY.LVL";
            World w(l);
            w.connect("Ann", "demo-tiny-2p-abc");
            w.run(500);
            ASSERT_EQ(w.status("demo-tiny-2p-abc").reconnect, kReconnectByDefault);
        }
        {   // a server that holds seats by default (whatever the constant is): the same default for every room that does not say
            ServerLimits l;
            l.reconnect = true;
            World w(l);
            ASSERT_TRUE(w.mgr.default_spec().reconnect);
            RoomSpec spec = w.mgr.default_spec();
            spec.code = "SW-4";
            spec.map = "TINY.LVL";
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok && w.status("SW-4").reconnect);
        }
        {   // a server that does not (a restart's record is of a room that held seats: it holds them now, whatever the server's default is)
            ServerLimits l;
            l.reconnect = false;
            PWorld w("persist-91", l);
            w.start_server(500);
            std::vector<RClient*> m = play_room(w, held_spec("SW-5", 2), 4000);
            ASSERT_TRUE(w.status("SW-5").reconnect && w.status("SW-5").record_kept);
            w.stop_server(true);
            w.start_server(500);
            ASSERT_TRUE(w.status("SW-5").reconnect && w.status("SW-5").restored && w.status("SW-5").record_kept);
        }
    } TEST_END();
}


void run_persist_server_tests_5() {
    TEST_CASE("S3.98 The Room's Wall-Clock Limit Counts The Play That Came Before The Restart (Not The Time The Match Waited For Its Players): A Room With A Limit Of 70 s Whose Match Had Been Played For 35 s When The Server Stopped Fails 35 s Of Play After The Players Are Back, Not 70") {
        PWorld w("persist-92");
        w.start_server(500);
        RoomSpec spec = held_spec("L-1", 2);
        spec.run_ms = 70000;
        std::vector<RClient*> m = play_room(w, spec, 30000);                                   // 5 s of dialog and 30 s of play: 35 s of the match
        ASSERT_EQ(w.status("L-1").state, RoomState::Running);
        w.stop_server(true);
        w.run(20000);                                                                          // the server is away for 20 s: that is no play
        w.start_server(500);
        ASSERT_TRUE(w.until([&]() { return !w.status("L-1").paused; }, 60000));
        w.run(28000);
        ASSERT_EQ(w.status("L-1").state, RoomState::Running);                                  // 28 s of play after the return: 63 s of the match, the limit is 70
        w.run(12000);
        const RoomStatus s = w.status("L-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.reason.find("longer than the room's limit") != std::string::npos);
        ASSERT_TRUE(w.record_files().empty());
    } TEST_END();

    TEST_CASE("S3.99 A Record Keeps No Address Of A Client: The Start Message That The Room Sent To Machines That Announce A Port Names Their Addresses (Host Migration's Business, A Game On The Local Network's); The Record's Start Message Has None, And Neither Has The Start That A Machine From Nothing Is Sent After A Restart")  {
        PWorld w("persist-93");
        w.announce_port = 4321;
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("E-1", 2), 3000);
        const net::StartMsg& live = m[0]->lobby->start_info();
        size_t named = 0;
        for (const net::Endpoint& e : live.endpoints) named += e.port == 4321 && !e.address.empty() ? 1u : 0u;
        ASSERT_EQ(named, size_t{2});                                                           // (the live Start names the two machines: the premise of the test)
        const RestartLoaded rec = w.read_record("E-1");
        ASSERT_TRUE(rec.ok());
        for (const net::Endpoint& e : rec.head.start.endpoints) ASSERT_TRUE(e.address.empty() && e.port == 0);
        const net::SeatKey key = m[1]->lobby->key();
        const uint8_t seat = m[1]->lobby->my_seat();
        w.stop_server(false);
        m[1]->reconnects = false;
        w.start_server(500);
        RClient& reloaded = w.connect("P1", "E-1", seat, key);                                 // a machine from nothing: it is sent the room's Start message
        ASSERT_TRUE(w.until([&]() { return reloaded.lobby != nullptr && reloaded.lobby->phase() != net::ClientLobby::Phase::Connecting && reloaded.lobby->phase() != net::ClientLobby::Phase::Joining; }, 20000));
        w.run(500);
        for (const net::Endpoint& e : reloaded.lobby->start_info().endpoints) ASSERT_TRUE(e.address.empty() && e.port == 0);
        ASSERT_EQ(reloaded.lobby->start_info().seed, live.seed);
    } TEST_END();
}


void run_persist_server_tests_6() {
    TEST_CASE("S3.100 What A Record Costs (Measured): Its Size For A Minute Of Play (An Idle Match, A Busy One Of Three Players And Of Four), The Cost Of Writing A Turn (One write() Of A Few Dozen Bytes) And Of The Flush Once A Second, And The Time It Takes To Bring A Match Back (Three Minutes Of Play Here; ANTS_PERSIST_MEASURE_LONG=1 Measures The Longest Plays Of TINY And Of TREASURE): The Numbers Are Printed, The Bounds Are Generous (A Record Of A Minute Under 120 KB, A Turn Under 1 ms, A Flush Under 250 ms, A Restore Of A Minute Of Play Under 5 s)")  {
        const auto seconds_since = [](const std::chrono::steady_clock::time_point& t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        // the size of a minute of play
        {
            double idle_per_minute = 0;
            double busy3_per_minute = 0;
            double busy4_per_minute = 0;
            for (int variant = 0; variant < 3; ++variant) {
                PWorld w("persist-94a");
                w.start_server(500);
                const uint8_t players = variant == 2 ? 4 : 3;
                std::vector<RClient*> m = play_room(w, held_spec("Z-1", players), 10000);
                if (variant == 0) {
                    for (RClient* p : m) p->orders = false;                                    // nobody gives an order: the idle match
                }
                const uint64_t at_start = w.status("Z-1").record_bytes;
                const uint32_t turns_start = w.status("Z-1").turns;
                w.run(60000);
                const RoomStatus s = w.status("Z-1");
                const double per_minute = static_cast<double>(s.record_bytes - at_start) / (static_cast<double>(s.turns - turns_start) / 1200.0);
                (variant == 0 ? idle_per_minute : variant == 1 ? busy3_per_minute : busy4_per_minute) = per_minute;
            }
            std::cout << "\n      [measured] a minute of play adds " << static_cast<int>(idle_per_minute) << " bytes to a record when nobody gives an order, " << static_cast<int>(busy3_per_minute)
                      << " for three players who each give an order every 0.7 s, " << static_cast<int>(busy4_per_minute) << " for four" << std::flush;
            ASSERT_TRUE(idle_per_minute > 15000 && idle_per_minute < 40000);              // 1,200 turns at 17 bytes and 60 checkpoints: about 21 KB
            ASSERT_TRUE(busy3_per_minute < 120000 && busy4_per_minute < 120000 && busy3_per_minute > idle_per_minute);
        }
        // the cost of writing a turn, and of the flush (the folder is the test's own: on a real disk)
        {
            RestartConfig cfg = test_restart_config("persist-94b");
            RestartStore store(cfg);
            std::string why;
            ASSERT_TRUE(store.prepare(why));
            auto writer = store.create(sample_head("M-1"), why);
            ASSERT_TRUE(writer != nullptr);
            const uint32_t turns = 100000;
            std::vector<net::TurnMsg> prepared;
            for (uint32_t n = 0; n < 1000; ++n) {
                prepared.push_back(sample_turn(n % 11 == 4 ? n : n + 1));
                prepared.back().commands.erase(std::remove_if(prepared.back().commands.begin(), prepared.back().commands.end(), [](const sim::Command& c) { return c.type == sim::CommandType::Drop; }), prepared.back().commands.end());
            }
            const auto t0 = std::chrono::steady_clock::now();
            for (uint32_t n = 0; n < turns; ++n) {
                net::TurnMsg t = prepared[n % prepared.size()];
                t.turn = n;
                if (!writer->append_turn(t)) break;
            }
            const double write_seconds = seconds_since(t0);
            ASSERT_EQ(writer->turns(), turns);
            const double write_us = write_seconds * 1e6 / turns;
            double worst_sync_ms = 0;
            double total_sync_ms = 0;
            const int syncs = 30;
            for (int i = 0; i < syncs; ++i) {
                for (uint32_t n = 0; n < 20; ++n) {
                    net::TurnMsg t = prepared[n];
                    t.turn = writer->turns();
                    ASSERT_TRUE(writer->append_turn(t));
                }
                const auto s0 = std::chrono::steady_clock::now();
                ASSERT_TRUE(writer->sync());
                const double ms = seconds_since(s0) * 1000.0;
                worst_sync_ms = std::max(worst_sync_ms, ms);
                total_sync_ms += ms;
            }
            std::cout << "\n      [measured] writing a turn costs " << write_us << " microseconds (100,000 turns in " << write_seconds << " s, " << writer->bytes() / 1024 << " KiB); a flush of a second's turns costs "
                      << total_sync_ms / syncs << " ms on average, " << worst_sync_ms << " ms at the worst of " << syncs << std::flush;
            ASSERT_TRUE(write_us < 1000.0);
            ASSERT_TRUE(worst_sync_ms < 250.0);
        }
        // the time to bring a match back
        {
            struct Run {
                const char* map;
                uint8_t players;
                uint32_t minutes;
            };
            std::vector<Run> runs = {{"TINY.LVL", 3, 3}};
            if (std::getenv("ANTS_PERSIST_MEASURE_LONG") != nullptr) {
                runs.push_back(Run{"TINY.LVL", 3, 5});                                         // (a minute short of its 6 minutes)
                runs.push_back(Run{"TREASURE.LVL", 4, 12});                                    // (its whole match is 12 minutes: 14,400 ticks; this is 20 s short of the end)
            }
            for (const Run& r : runs) {
                PWorld w("persist-94c");
                w.start_server(500);
                RoomSpec spec = held_spec("Z-2", r.players, r.map);
                spec.run_ms = 24u * 3600u * 1000u;
                std::vector<RClient*> m = play_room(w, spec, r.minutes * 60000u - 20000u);                  // (the match ends at its minutes of play: a little before)
                for (RClient* p : m) p->reconnects = false;
                const uint32_t sealed = w.status("Z-2").turns;
                const uint64_t bytes = w.status("Z-2").record_bytes;
                w.stop_server(false);
                const auto t0 = std::chrono::steady_clock::now();
                w.start_server(500);
                const double total_seconds = seconds_since(t0);
                ASSERT_EQ(w.report.count(RestoreItem::Outcome::Restored), size_t{1});
                const RoomStatus s = w.status("Z-2");
                std::cout << "\n      [measured] a match of " << r.minutes << " minutes (less 20 s) of play on " << r.map << " with " << static_cast<int>(r.players) << " players: " << sealed << " turns, a record of " << bytes / 1024
                          << " KiB, brought back in " << total_seconds << " s (the replay " << s.restore_ms << " ms: " << static_cast<double>(sealed) / std::max(1.0, static_cast<double>(s.restore_ms)) * 1000.0 << " turns a second)" << std::flush;
                ASSERT_TRUE(s.restored && s.restored_turns == sealed);
                ASSERT_TRUE(total_seconds < 5.0 * r.minutes);
            }
        }
    } TEST_END();

    TEST_CASE("S3.102 A Restored Room Is A Match That Runs, As Far As /busy Counts (The Public Answer That A Deploy Waits On), For Five Minutes (M3 Of The Review): After A Restart Of The Server Two Rooms Whose Players Have Not Come Back Count Two Matches And Their People (The Seats Are Held, A Restart Now Would Interrupt Them Again; A Bot Is No Person) up to 299.999 s After The Restore And Nothing From 300 s On (A Room That Nobody Comes Back To Must Not Hold A Deploy For The Pause Cap); A Player Who Is Back Makes Its Room Count Again, As Itself (The Held Seats Of The Others Count Only Inside The Window); In A Live Room A Seat Whose Link Is Lost Is Held And Is No Person Who Is There; After A Restart That Cannot Bring Them Back (Another Network Protocol) They Count Nothing") {
        PWorld w("persist-102");
        w.restart.clock_ms = []() { return 0u; };                                              // (a clock that stands still: the restore takes no time by it, so the rooms begin at the very moment of the restart and the window's edge is exact)
        w.start_server(500);
        const auto busy_after = [&w](uint32_t ms) { return w.mgr->busy(w.server_now() + ms); };
        const auto busy = [&]() { return busy_after(0); };
        RoomSpec one = held_spec("BZ-1", 2);
        one.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy});
        ASSERT_TRUE(w.mgr->create_room(one, w.server_now()).ok);
        RClient& pat = w.connect("Pat", "BZ-1");
        std::vector<RClient*> two = play_room(w, held_spec("BZ-2", 2), 4000);
        ASSERT_TRUE(w.until([&]() { return w.status("BZ-1").state == RoomState::Running; }, 20000));
        ASSERT_TRUE(busy().matches == 2 && busy().players == 3);                              // Pat and two more people: the bot of BZ-1 is no person
        const uint8_t seat_back = two[0]->lobby->my_seat();
        const net::SeatKey key_back = two[0]->lobby->key();
        pat.reconnects = false;
        for (RClient* p : two) p->reconnects = false;
        {   // a live room: a seat whose link is lost is held (the room pauses, the seat may come back) and is no person who is there; the room counts while anybody is
            w.net.cut(two[1]->end);
            w.run(1500);
            ASSERT_TRUE(w.status("BZ-2").paused && w.status("BZ-2").absent.size() == 1);
            ASSERT_TRUE(busy().matches == 2 && busy().players == 2);                          // Pat, and the one person who is left in BZ-2
            w.net.cut(two[0]->end);
            w.run(1500);
            ASSERT_TRUE(w.status("BZ-2").paused && w.status("BZ-2").absent.size() == 2);
            ASSERT_TRUE(busy().matches == 1 && busy().players == 1);                          // BZ-2 has nobody in it now: a restart would interrupt Pat's match only (its record is kept anyway)
        }
        // ---- a restart: both rooms are back, paused, every seat of a person held and absent; they are matches that run, with their people ---------------------------------------------------------------
        w.stop_server(false);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 2);
        for (const char* code : {"BZ-1", "BZ-2"}) {
            const RoomStatus s = w.status(code);
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.paused && !s.absent.empty());
        }
        ASSERT_EQ(busy().matches, 2u);
        ASSERT_EQ(busy().players, 3u);                                                         // (nobody is connected: the held seats are the people that a restart now would interrupt again)
        // ---- the window: five minutes from the restore, to the millisecond -------------------------------------------------------------------------------------------------------------------------
        ASSERT_EQ(kRestoredBusyWindowMs, 300000u);
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs - 1).matches == 2 && busy_after(kRestoredBusyWindowMs - 1).players == 3);
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 0 && busy_after(kRestoredBusyWindowMs).players == 0);       // nobody came back: the rooms hold no deploy (the pause cap would hold it for 30 minutes)
        ASSERT_TRUE(busy_after(10u * 60u * 1000u).matches == 0 && busy_after(24u * 3600u * 1000u).players == 0);
        // ---- a player comes back to BZ-2 (a machine from nothing, with its key): its room counts again, as itself --------------------------------------------------------------------------------------
        RClient& back = w.connect("P0", "BZ-2", seat_back, key_back);
        bool saw_catching_up = false;
        for (int step = 0; step < 6000 && w.status("BZ-2").rejoins < 1; ++step) {
            w.run(10);
            for (const RoomStatus::Absent& a : w.status("BZ-2").absent) {
                if (!a.catching_up) continue;
                saw_catching_up = true;                                                        // a player who is being given the match is there: the room counts, beyond the window too
                ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 1 && busy_after(kRestoredBusyWindowMs).players == 1);
            }
        }
        ASSERT_TRUE(saw_catching_up && w.status("BZ-2").rejoins == 1);
        ASSERT_TRUE(back.session != nullptr && !back.lost);
        ASSERT_TRUE(busy().matches == 2 && busy().players == 3);                               // still inside the window: BZ-2 has a person and a held seat, BZ-1 two held seats' worth of one person
        ASSERT_TRUE(busy_after(kRestoredBusyWindowMs).matches == 1 && busy_after(kRestoredBusyWindowMs).players == 1);      // beyond it: the person who is there, and no held seat
        ASSERT_TRUE(busy_after(24u * 3600u * 1000u).matches == 1 && busy_after(24u * 3600u * 1000u).players == 1);
        // ---- a restart that cannot bring them back (the network protocol is another one): they are failed rooms, and a failed room counts nothing ---------------------------------------------------
        w.stop_server(false);
        w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 2);
        ASSERT_TRUE(w.status("BZ-1").state == RoomState::Failed && w.status("BZ-2").state == RoomState::Failed);
        ASSERT_TRUE(busy().matches == 0 && busy().players == 0);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The review of the restart records (docs/audit/persist_notes.md, "State at handoff"): S3.103 and on
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

void put_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
}

// Frames of EMPTY turns, as an honest writer or a hostile one makes them: `turns` of them from number `first` on, `per_frame` to a frame (1: what the server writes, 4096: the most that a frame may hold)
void append_empty_turns(std::vector<uint8_t>& out, uint32_t first, uint32_t turns, uint32_t per_frame) {
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
std::vector<uint8_t> empty_turns_record(uint32_t turns, uint32_t per_frame) {
    const std::vector<uint8_t> head = encode_restart_head(sample_head("REC-1"));
    std::vector<uint8_t> out = with_magic(head);
    append_empty_turns(out, 0, turns, per_frame);
    return out;
}

// A match played for `play_ms` and then lost with the server (a crash): the room's record is on disk, its machines do not come back
void crash_with_record_of(PWorld& w, const std::string& code, uint32_t play_ms, uint8_t players = 2) {
    std::vector<RClient*> m = play_room(w, held_spec(code, players), play_ms);
    for (RClient* p : m) p->reconnects = false;
    w.stop_server(false);
}

// A record whose head is made over by `edit` (the frames behind it are kept as they are)
void rewrite_head(PWorld& w, const std::string& code, const std::function<void(RestartHead&)>& edit) {
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
void copy_record_as(PWorld& w, const std::string& from_code, const std::string& to_code, std::chrono::seconds age) {
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

}  // namespace

void run_persist_review_tests() {
    TEST_CASE("S3.103 A Hostile Record Costs What Its Bytes Cost (M1 Of The Review): A Record May Hold 1,800,000 Turns (25 Hours Of Play) And Not One More, In Both Modes; The Limit Is Judged From The Header Of A Frame, Before Any Of Its Turns Is Decoded; A Record Read As Streaming Keeps No Turns (Its Count, Checkpoints, Head And Good Bytes Are The Whole Record's) And Gives Its Turns Again One At A Time; The Biggest Hostile File (48 MiB Of Valid Empty Turns, One To A Frame) Is Refused As Corrupt At The Start And Deleted; A Head Whose Keep Time Or Run Limit Is Beyond 24 Hours Is No Room That This Server Would Make") {
        {   // the limit: exactly kRestartMaxTurns empty turns are read (4096 to a frame), one more is corrupt; the same in both modes
            const std::vector<uint8_t> at_limit = empty_turns_record(kRestartMaxTurns, 4096);
            const RestartLoaded whole = parse_restart_record(at_limit.data(), at_limit.size());
            ASSERT_TRUE(whole.ok() && whole.turn_count == kRestartMaxTurns && whole.turns.size() == kRestartMaxTurns && !whole.torn && whole.bytes.empty());
            const RestartLoaded streamed = parse_restart_record(at_limit.data(), at_limit.size(), false);
            ASSERT_TRUE(streamed.ok() && streamed.turn_count == kRestartMaxTurns && streamed.turns.empty() && streamed.good_bytes == whole.good_bytes && streamed.file_bytes == whole.file_bytes);
            ASSERT_TRUE(same_head(streamed.head, whole.head));
            for (const bool keep : {true, false}) {
                const std::vector<uint8_t> over = empty_turns_record(kRestartMaxTurns + 1, 4096);
                const RestartLoaded r = parse_restart_record(over.data(), over.size(), keep);
                ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.why.find("more turns than a match can") != std::string::npos);
                ASSERT_TRUE(r.turns.empty() && r.turn_count == 0 && r.checks.empty() && r.good_bytes == 0);       // (a refused record hands out nothing)
            }
        }
        {   // the header decides: a frame that would take the record past the limit is refused for its count, whatever its body holds (here: nothing that decodes)
            std::vector<uint8_t> bytes = empty_turns_record(kRestartMaxTurns - 1, 4096);
            std::vector<uint8_t> payload(6, 0);
            put_le32(payload.data(), kRestartMaxTurns - 1);
            payload[4] = 2;                                                                                // two turns: one more than the limit allows; the body that would hold them is not there
            const std::vector<uint8_t> frame = test_frame(2, payload);
            bytes.insert(bytes.end(), frame.begin(), frame.end());
            const RestartLoaded r = parse_restart_record(bytes.data(), bytes.size(), false);
            ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.why.find("more turns than a match can") != std::string::npos);
            payload[4] = 1;                                                                                // ... and one turn that is within the limit but has no body is "cut short", the old message
            std::vector<uint8_t> within = empty_turns_record(kRestartMaxTurns - 1, 4096);
            const std::vector<uint8_t> frame_one = test_frame(2, payload);
            within.insert(within.end(), frame_one.begin(), frame_one.end());
            const RestartLoaded cut = parse_restart_record(within.data(), within.size(), false);
            ASSERT_TRUE(!cut.ok() && cut.why.find("cut short") != std::string::npos);
        }
        {   // a record read as Streaming keeps no turns and gives them again, one at a time, the same as the kept ones; an early stop stops
            RestartConfig cfg = test_restart_config("persist-103a");
            RestartStore store(cfg);
            std::string why;
            ASSERT_TRUE(store.prepare(why));
            auto writer = store.create(sample_head("S-1"), why);
            ASSERT_TRUE(writer != nullptr);
            for (uint32_t n = 0; n < 100; ++n) {
                ASSERT_TRUE(writer->append_turn(sample_turn(n)));
                if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(writer->append_check(n, 0x1000u + n));
            }
            ASSERT_TRUE(writer->sync());
            const std::string path = writer->path();
            writer.reset();
            {   // (a torn tail after the good frames: the walk stops at the last good byte)
                std::vector<uint8_t> bytes = read_all_bytes(path);
                const std::vector<uint8_t> next = test_frame(2, turns_payload(100, {sample_turn(100)}));
                bytes.insert(bytes.end(), next.begin(), next.begin() + 9);
                write_all_bytes(path, bytes);
            }
            const RestartLoaded kept = read_restart_record(path, cfg.max_record_bytes);
            const RestartLoaded streamed = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(kept.ok() && streamed.ok() && kept.torn && streamed.torn);
            ASSERT_TRUE(kept.turns.size() == 100 && kept.turn_count == 100 && kept.bytes.empty());
            ASSERT_TRUE(streamed.turns.empty() && streamed.turn_count == 100 && streamed.bytes.size() == streamed.file_bytes);
            ASSERT_TRUE(streamed.good_bytes == kept.good_bytes && streamed.checks.size() == 5 && same_checks(streamed.checks, kept.checks, 5) && same_head(streamed.head, kept.head) && streamed.path == path);
            std::vector<net::TurnMsg> a;
            std::vector<net::TurnMsg> b;
            ASSERT_TRUE(for_each_restart_turn(kept, [&](const net::TurnMsg& t) { a.push_back(t); return true; }));
            ASSERT_TRUE(for_each_restart_turn(streamed, [&](const net::TurnMsg& t) { b.push_back(t); return true; }));
            ASSERT_TRUE(a.size() == 100 && b.size() == 100 && same_turns(a, b, 100) && same_turns(b, kept.turns, 100));
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(streamed, [&](const net::TurnMsg&) { return ++seen < 7; }));        // the callback said stop at the seventh turn
            ASSERT_EQ(seen, size_t{7});
            ASSERT_FALSE(for_each_restart_turn(kept, [&](const net::TurnMsg&) { return false; }));
            // a streamed record that is refused hands out no bytes
            std::vector<uint8_t> broken = read_all_bytes(path);
            broken[sizeof(kRestartMagic) + 7] ^= 0x20;                                                     // inside the head
            write_all_bytes(path, broken);
            const RestartLoaded bad = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(!bad.ok() && bad.bytes.empty() && bad.turns.empty() && bad.turn_count == 0);
        }
        {   // the biggest hostile file that a record may be (48 MiB: valid empty turns, one to a frame) is refused at the start: corrupt, named in the log, deleted, nothing left of it
            PWorld w("persist-103b");
            w.start_server(500);
            w.stop_server(false);
            const uint32_t turns = 2'900'000;                                                              // 17 bytes each: 47 MiB
            const std::vector<uint8_t> hostile = empty_turns_record(turns, 1);
            ASSERT_TRUE(hostile.size() < w.restart.max_record_bytes && hostile.size() > 40ull * 1024 * 1024);
            const fs::path planted = fs::path(w.restart.dir) / "room-HOSTILE-00000001.restart";
            write_all_bytes(planted, hostile);
            const auto began = std::chrono::steady_clock::now();
            w.start_server(500);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            ASSERT_EQ(w.report.items.size(), size_t{1});
            ASSERT_TRUE(w.report.items[0].outcome == RestoreItem::Outcome::Unreadable && w.report.items[0].note.find("corrupt") != std::string::npos && w.report.items[0].note.find("more turns than a match can") != std::string::npos);
            ASSERT_FALSE(fs::exists(planted));
            bool named = false;
            for (const std::string& n : w.notices) named = named || (n.find("room-HOSTILE-00000001.restart") != std::string::npos && n.find("more turns than a match can") != std::string::npos);
            ASSERT_TRUE(named);
            ASSERT_EQ(w.mgr->room_count(), size_t{0});
            ASSERT_TRUE(seconds < 20.0);                                                                   // (it is refused when the 1,800,001st turn is read: a second at the most; the bound is loose, a loaded machine)
            // the same file read as a file in both modes
            write_all_bytes(planted, hostile);
            for (const RestartRead mode : {RestartRead::Whole, RestartRead::Streaming}) {
                const RestartLoaded r = read_restart_record(planted.string(), w.restart.max_record_bytes, mode);
                ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::Corrupt && r.turns.empty() && r.bytes.empty());
            }
        }
        {   // a head whose keep time or run limit is beyond what the control interface allows (24 hours) is no room that this server would make, in create_room and in a restore
            ASSERT_EQ(kRestartMaxTurns, 1'800'000u);
            World w;
            RoomSpec ok_spec = spec_of("RG-1");
            ok_spec.keep_ms = 24u * 3600u * 1000u;
            ok_spec.run_ms = 24u * 3600u * 1000u;
            ASSERT_TRUE(w.mgr.create_room(ok_spec, w.now).ok);                                              // (24 h exactly is fine)
            RoomSpec long_keep = spec_of("RG-2");
            long_keep.keep_ms = 24u * 3600u * 1000u + 1;
            CreateResult r = w.mgr.create_room(long_keep, w.now);
            ASSERT_TRUE(!r.ok && r.http_status == 400 && r.error.find("keep_seconds") != std::string::npos);
            RoomSpec long_run = spec_of("RG-3");
            long_run.run_ms = 24u * 3600u * 1000u + 1;
            r = w.mgr.create_room(long_run, w.now);
            ASSERT_TRUE(!r.ok && r.http_status == 400 && r.error.find("max_run_seconds") != std::string::npos);
            long_run.run_ms = 0xFFFFFFFFu;
            ASSERT_EQ(w.mgr.create_room(long_run, w.now).http_status, 400);
            for (const bool keep_side : {true, false}) {
                PWorld p("persist-103c");
                p.start_server(500);
                crash_with_record_of(p, "RH-1", 6000);
                rewrite_head(p, "RH-1", [keep_side](RestartHead& h) { (keep_side ? h.keep_ms : h.run_ms) = 24u * 3600u * 1000u + 1; });
                p.start_server(500);
                ASSERT_TRUE(p.report.items.size() == 1 && p.report.items[0].outcome == RestoreItem::Outcome::Ended && p.report.items[0].note.find("would not make") != std::string::npos);
                ASSERT_TRUE(p.status("RH-1").state == RoomState::Failed && p.record_files().empty());
            }
        }
    } TEST_END();

    TEST_CASE("S3.104 One Server To A Folder (M5 Of The Review): The Folder Is Locked For The Life Of The Store That Prepared It (The File .lock, flock): A Second Store On The Same Folder, In This Process Or In Another, Cannot Prepare It And Says Another Server Has It; The Temporary File Of The First One's Record Is Not Removed By The Second (The Cleanup Comes After The Lock); The Same Store May Prepare Again; The Lock Goes When The Store Does (Or The Process Dies); A Second Room Manager Keeps No Records And Says Why") {
        RestartConfig cfg = test_restart_config("persist-104");
        std::string why;
        const fs::path tmp = fs::path(cfg.dir) / "room-LIVE-0000abcd.restart.0123456789abcdef.tmp";
        {
            RestartStore first(cfg);
            ASSERT_TRUE(first.prepare(why) && why.empty());
            ASSERT_TRUE(first.prepare(why));                                                    // (the same store again: it holds the lock already)
            ASSERT_TRUE(fs::exists(fs::path(cfg.dir) / ".lock"));
            ASSERT_TRUE(first.records().empty());                                               // (the lock file is no record)
            write_all_bytes(tmp, {1, 2, 3});                                                    // the first server is making a record: its temporary file
            RestartStore second(cfg);
            ASSERT_FALSE(second.prepare(why));
            ASSERT_TRUE(why.find("another server") != std::string::npos && why.find("lock") != std::string::npos);
            ASSERT_TRUE(fs::exists(tmp));                                                       // the refused store did not touch what is the first one's
            ASSERT_FALSE(second.prepare(why));                                                  // (and it stays refused)
            auto w1 = first.create(sample_head("LK-1"), why);                                    // the first one goes on as it did
            ASSERT_TRUE(w1 != nullptr && w1->append_turn(sample_turn(0)));
            ASSERT_TRUE(fs::exists(tmp));
            RoomManager keeper{MapStore(maps_dir())};                                           // a second room manager over the same folder keeps no records, and says why
            ASSERT_FALSE(keeper.enable_restart_records(cfg, why));
            ASSERT_TRUE(keeper.restart_store() == nullptr && why.find("another server") != std::string::npos);
            ASSERT_TRUE(keeper.create_room(held_spec("LK-2", 2), 1000).ok);
            RoomStatus s;
            ASSERT_TRUE(keeper.status("LK-2", s, 1000) && !s.record_kept && s.record_note == "this server keeps no restart records");
        }
        {   // the first store is gone: the folder is free, and the stale temporary file is cleaned up now that the lock is held
            RestartStore later(cfg);
            ASSERT_TRUE(later.prepare(why));
            ASSERT_FALSE(fs::exists(tmp));
            RestartStore blocked(cfg);
            ASSERT_FALSE(blocked.prepare(why));                                                 // ... and is taken again
        }
        {   // another folder is another lock; a folder with a lock file that nobody holds (a server that died) is free
            RestartConfig other = test_restart_config("persist-104b");
            RestartStore x(other);
            ASSERT_TRUE(x.prepare(why));
            RestartStore y(cfg);
            ASSERT_TRUE(y.prepare(why));
            ASSERT_TRUE(fs::exists(fs::path(cfg.dir) / ".lock") && fs::exists(fs::path(other.dir) / ".lock"));
        }
#ifndef _WIN32
        {   // a lock that another PROCESS holds: the same refusal, and the lock goes when that process dies (SIGKILL: no destructor runs)
            RestartConfig shared = test_restart_config("persist-104c");
            int up[2];
            ASSERT_EQ(::pipe(up), 0);
            const pid_t pid = ::fork();
            ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                ::close(up[0]);
                RestartStore holder(shared);
                std::string child_why;
                const char ok = holder.prepare(child_why) ? 'y' : 'n';
                (void)!::write(up[1], &ok, 1);
                ::pause();
                ::_exit(0);
            }
            ::close(up[1]);
            char told = 0;
            ASSERT_TRUE(::read(up[0], &told, 1) == 1 && told == 'y');
            RestartStore rival(shared);
            const bool refused = !rival.prepare(why) && why.find("another server") != std::string::npos;
            ::kill(pid, SIGKILL);
            int wait_status = 0;
            ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
            ::close(up[0]);
            ASSERT_TRUE(refused);
            ASSERT_TRUE(rival.prepare(why));                                                    // the process is dead: its lock is gone, the folder is free
        }
#endif
    } TEST_END();

    TEST_CASE("S3.112 A Record Is Opened Without A Race (L8 Of The Review): The File That Is Looked At Is The File That Is Read (One Open, Then The Descriptor Is Asked What It Is And How Big): A Link, A Folder And A Pipe Named Like A Record Are Refused As Not Regular Files (The Pipe Without Waiting On It: The Read Happens In A Child With An Alarm), A Missing Name Is Unreadable, A File Over The Limit Is Refused From The Size Of The Open File, An Empty File Is No Record, A Good One Reads In Both Modes") {
        RestartConfig cfg = test_restart_config("persist-112");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        auto w = store.create(sample_head("OP-1"), why);
        ASSERT_TRUE(w != nullptr);
        for (uint32_t n = 0; n < 30; ++n) ASSERT_TRUE(w->append_turn(sample_turn(n)));
        ASSERT_TRUE(w->sync());
        const std::string path = w->path();
        w.reset();
        const RestartLoaded good = read_restart_record(path, cfg.max_record_bytes);
        ASSERT_TRUE(good.ok() && good.turn_count == 30);
        const fs::path dir = cfg.dir;
        const auto read_at = [&](const char* name) { return read_restart_record((dir / name).string(), uint64_t{1} << 20); };
        // a folder, a name that is not there
        fs::create_directories(dir / "room-DIR-00000002.restart");
        const RestartLoaded folder = read_at("room-DIR-00000002.restart");
        ASSERT_TRUE(folder.status == RestartLoaded::Status::Unreadable);
        ASSERT_TRUE(read_at("room-NONE-00000003.restart").status == RestartLoaded::Status::Unreadable);
        // the limit comes from the size of the file that was opened, to the byte
        ASSERT_TRUE(read_restart_record(path, good.file_bytes - 1).status == RestartLoaded::Status::TooBig);
        ASSERT_TRUE(read_restart_record(path, good.file_bytes).ok());
        // an empty file is no record; a good one reads in both modes, and the bytes are those of the file
        write_all_bytes(dir / "room-EMPTY-00000005.restart", {});
        ASSERT_TRUE(read_at("room-EMPTY-00000005.restart").status == RestartLoaded::Status::NotARecord);
        const RestartLoaded streamed = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);
        ASSERT_TRUE(streamed.ok() && streamed.turn_count == 30 && streamed.bytes.size() == good.file_bytes && streamed.turns.empty());
#ifndef _WIN32
        ASSERT_TRUE(folder.why.find("not a regular file") != std::string::npos && folder.why.find("a folder") != std::string::npos);
        // a link to a good record is not followed (a followed link would read as that record)
        fs::create_symlink(path, dir / "room-LINK-00000001.restart");
        const RestartLoaded linked = read_at("room-LINK-00000001.restart");
        ASSERT_TRUE(linked.status == RestartLoaded::Status::Unreadable && linked.why.find("not a regular file") != std::string::npos && linked.why.find("a link") != std::string::npos);
        fs::create_symlink((dir / "room-NONE-00000003.restart").string(), dir / "room-DANGLING-00000006.restart");      // (and a link to nothing is the same refusal)
        ASSERT_TRUE(read_at("room-DANGLING-00000006.restart").why.find("a link") != std::string::npos);
        // a pipe that nobody writes to: opening it for reading would wait for ever; the child has five seconds (the alarm ends a hang)
        const std::string fifo = (dir / "room-FIFO-00000004.restart").string();
        ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        const pid_t pid = ::fork();
        ASSERT_TRUE(pid >= 0);
        if (pid == 0) {
            ::alarm(5);
            const RestartLoaded r = read_restart_record(fifo, uint64_t{1} << 20);
            ::_exit(r.status == RestartLoaded::Status::Unreadable && r.why.find("not a regular file") != std::string::npos ? 0 : 3);
        }
        int wait_status = 0;
        ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
        ASSERT_TRUE(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0);
#endif
    } TEST_END();
}


#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
// ---------------------------------------------------------------------------------------------------------------------------------
// The real program: ants_server as a child process of the test, stopped with SIGTERM or killed with SIGKILL in the middle of a match and started again over the same folder, and two
// machines of the test (RClient: the real lobby and the real session) that find it again by themselves over real TCP sockets
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

uint32_t wall_ms() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()) + 1000u;
}

uint16_t free_tcp_port() {
    auto listener = net::TcpListener::listen(0, true);
    return listener != nullptr ? listener->port() : uint16_t{0};
}

// The real ants_server program. Only the process that this object started is ever signalled (never by name).
struct ServerProcess {
    pid_t pid{-1};
    int started{0};
    bool start(const std::vector<std::string>& args, const std::string& secret, const std::string& log_file) {
        std::vector<std::string> storage;
        storage.push_back(ANTS_SERVER_BINARY);
        for (const std::string& a : args) storage.push_back(a);
        std::vector<char*> argv;
        for (std::string& a : storage) argv.push_back(&a[0]);
        argv.push_back(nullptr);
        pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            const int fd = ::open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (fd >= 0) {
                ::dup2(fd, 1);
                ::dup2(fd, 2);
                ::close(fd);
            }
            ::setenv("ANTS_SERVER_SECRET", secret.c_str(), 1);
            ::execv(argv[0], argv.data());
            ::_exit(127);
        }
        ++started;
        return true;
    }
    // Waits for the process to end (and reaps it): how long it took in ms, or -1 when it did not end within `timeout_ms` (then it is still there)
    int64_t wait_exit(uint32_t timeout_ms, int& status) {
        const auto began = std::chrono::steady_clock::now();
        for (;;) {
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            const int64_t took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
            if (r == pid) {
                pid = -1;
                return took;
            }
            if (took > static_cast<int64_t>(timeout_ms)) return -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void signal_it(int sig) {
        if (pid > 0) ::kill(pid, sig);
    }
    ~ServerProcess() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
    }
};

// A blocking request to the control interface (the answer carries Connection: close): the JSON body, null when there is none
ctl::JsonValue ctl_call(uint16_t port, const std::string& method, const std::string& path, const std::string& secret, const std::string& body = std::string(), int* status = nullptr) {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return ctl::JsonValue::make_null();
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return ctl::JsonValue::make_null();
    }
    std::string request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + secret + "\r\nConnection: close\r\n";
    if (!body.empty()) request += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n" + body;
    ::send(s, request.data(), request.size(), 0);
    std::string response;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(s);
    if (status != nullptr) *status = response.compare(0, 9, "HTTP/1.1 ") == 0 ? std::atoi(response.c_str() + 9) : 0;
    const size_t split = response.find("\r\n\r\n");
    ctl::JsonValue j;
    std::string why;
    if (split == std::string::npos || !ctl::parse_json(response.substr(split + 4), j, &why)) return ctl::JsonValue::make_null();
    return j;
}

// Machines of the test over real TCP links (the clock is the wall clock)
struct RealWorld : LinkSource {
    uint16_t port{0};
    std::string maps;
    std::vector<std::unique_ptr<net::TcpConnection>> links;
    std::vector<std::unique_ptr<RClient>> clients;

    net::Connection* open_link() override {
        links.push_back(net::TcpConnection::connect("127.0.0.1", port));
        return links.back().get();
    }
    RClient& connect(const std::string& name, const std::string& room) {
        net::Connection* end = open_link();
        clients.push_back(std::make_unique<RClient>());
        RClient& c = *clients.back();
        c.name = name;
        c.room = room;
        c.record_hashes = true;
        c.start(end, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    void step() {
        for (size_t i = 0; i < clients.size(); ++i) clients[i]->update(wall_ms(), maps, *this);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool until(const std::function<bool()>& cond, uint32_t timeout_ms) {
        const uint32_t end = wall_ms() + timeout_ms;
        while (static_cast<int32_t>(wall_ms() - end) < 0) {
            if (cond()) return true;
            step();
        }
        return cond();
    }
    void run_for(uint32_t ms) {
        const uint32_t end = wall_ms() + ms;
        while (static_cast<int32_t>(wall_ms() - end) < 0) step();
    }
};

// The port accepts connections (the kernel completes them before the program reads them: a restore that is still running does not show here)
bool port_accepts(uint16_t port, uint32_t timeout_ms) {
    const uint32_t end = wall_ms() + timeout_ms;
    while (static_cast<int32_t>(wall_ms() - end) < 0) {
        auto c = net::TcpConnection::connect("127.0.0.1", port);
        for (int i = 0; i < 100 && c != nullptr && c->state() == net::Connection::State::Connecting; ++i) {
            std::vector<uint8_t> nothing;
            c->poll(nothing);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (c != nullptr && c->is_open()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::string text_of_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string hex_u64(uint64_t v) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<size_t>(15 - i)] = kHex[(v >> (4 * i)) & 0xFu];
    return out;
}

// One scenario: a match of two machines on a real server, the server stopped with `stop_signal` in the middle of it, started again, the machines find it by themselves, the match goes on to its end
void real_process_scenario(const char* tag, int stop_signal, bool control_room) {
    const fs::path root = temp_dir_for(tag);
    const fs::path results = root / "results";
    const uint16_t game_port = free_tcp_port();
    const uint16_t ctl_port = free_tcp_port();
    ASSERT_TRUE(game_port != 0 && ctl_port != 0 && game_port != ctl_port);
    const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
    const std::string log_file = (root / "server.log").string();
    const std::vector<std::string> args = {"--maps", maps_dir(), "--port", std::to_string(game_port), "--ctl-port", std::to_string(ctl_port), "--results-dir", results.string(), "--reconnect",
                                           "--demo-rooms", "4", "--demo-map", "TINY.LVL", "--resume-countdown-seconds", "0", "--max-pause-seconds", "300"};
    const fs::path restart_dir = results / "restart";
    ServerProcess server;
    ASSERT_TRUE(server.start(args, secret, log_file));
    ASSERT_TRUE(port_accepts(game_port, 15000));
    ASSERT_TRUE(port_accepts(ctl_port, 5000));
    const std::string code = control_room ? "RP-1" : "demo-tiny-2p-r1";
    if (control_room) {
        int http = 0;
        const ctl::JsonValue made = ctl_call(ctl_port, "POST", "/rooms", secret, "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"RP-1\", \"reconnect\": true, \"resume_countdown_seconds\": 0, \"max_pause_seconds\": 300}", &http);
        ASSERT_TRUE(http == 201 && made.get("state").str() == "waiting");
    }
    RealWorld w;
    w.port = game_port;
    w.maps = maps_dir();
    RClient& a = w.connect("Ann", code);
    RClient& b = w.connect("Bob", code);
    ASSERT_TRUE(w.until([&]() { return a.session != nullptr && b.session != nullptr && a.session->runner().next_turn_expected() >= 130 && b.session->runner().next_turn_expected() >= 130; }, 40000));
    // the record is there, for its owner only, and the control interface says so
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(restart_dir)) {
        if (e.path().extension() == kRestartExtension) files.push_back(e.path());              // (the folder also holds the lock file of the server that runs)
    }
    ASSERT_EQ(files.size(), size_t{1});
    {
        struct stat st;
        ASSERT_EQ(::stat(files[0].c_str(), &st), 0);
        ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
        ASSERT_EQ(::stat(restart_dir.c_str(), &st), 0);
        ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
    }
    ctl::JsonValue j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("record").get("kept").as_bool_or(false) && j.get("restored").is_null());
    const net::SeatKey key_a = a.lobby->key();
    const net::SeatKey key_b = b.lobby->key();
    // ---- the server is stopped (SIGTERM: it makes the records durable and exits at once; SIGKILL: it is gone) -------------------------------------------------------------------
    w.run_for(1234);                                                                    // (a moment that is no turn boundary)
    const uint32_t seen = std::max(a.session->runner().next_turn_expected(), b.session->runner().next_turn_expected());       // the most turns that any machine has been sent
    const auto began_stop = std::chrono::steady_clock::now();
    server.signal_it(stop_signal);
    int status = 0;
    const int64_t took = server.wait_exit(10000, status);
    const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
    ASSERT_TRUE(took >= 0);
    if (stop_signal == SIGTERM) {
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ASSERT_TRUE(stop_ms < 3000);                                                    // within the grace (the stack files give docker 15 s): the flush is a moment's work
    } else {
        ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    }
    files.clear();
    for (const auto& e : fs::directory_iterator(restart_dir)) {
        if (e.path().extension() == kRestartExtension) files.push_back(e.path());              // (the folder also holds the lock file of the server that runs)
    }
    ASSERT_EQ(files.size(), size_t{1});                                                 // the record is still there
    const RestartLoaded rec = read_restart_record(files[0].string(), 1ull << 30);
    ASSERT_TRUE(rec.ok());
    ASSERT_TRUE(rec.turns.size() >= seen);                                              // it holds every turn that any machine has been sent: a restart loses nothing that a player has run
    ASSERT_TRUE(rec.head.code == code && rec.head.keys[a.lobby->my_seat()] == key_a && rec.head.keys[b.lobby->my_seat()] == key_b);
    const size_t record_turns = rec.turns.size();
    const std::string log_before = text_of_file(log_file);
    if (stop_signal == SIGTERM) ASSERT_TRUE(log_before.find("stopped: 1 restart record(s) made durable and kept") != std::string::npos);
    // ---- the machines notice and look for the server; it starts again over the same folder ----------------------------------------------------------------------------------------
    w.run_for(2500);
    ASSERT_TRUE(a.session->reconnecting() && b.session->reconnecting() && !a.lost && !b.lost);
    ASSERT_TRUE(server.start(args, secret, log_file));
    ASSERT_TRUE(port_accepts(game_port, 20000));
    // the machines find the room by its code and their keys: no help from the test
    ASSERT_TRUE(w.until([&]() { return a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal && !a.session->paused(); }, 60000));
    ASSERT_FALSE(a.lost || b.lost || a.was_rejected || b.was_rejected);                 // (none was told that it is ahead of the record)
    j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) == static_cast<int64_t>(record_turns));
    ASSERT_TRUE(j.get("rejoins").as_int_or(0) == 2 && !j.get("paused").as_bool_or(true) && j.get("record").get("kept").as_bool_or(false));
    w.run_for(4000);
    j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
    ASSERT_TRUE(j.get("turns").as_int_or(0) > static_cast<int64_t>(record_turns) + 60);  // the match goes on
    // ---- Bob quits: the match ends, on the machines and on the referee, in one state -------------------------------------------------------------------------------------------------
    sim::Command quit;
    quit.type = sim::CommandType::Quit;
    quit.issuer = b.lobby->my_seat();
    ASSERT_TRUE(b.session->submit(quit));
    bool finished = false;
    for (int i = 0; i < 100 && !finished; ++i) {
        w.run_for(200);
        j = ctl_call(ctl_port, "GET", "/rooms/" + code, secret);
        finished = j.get("state").str() == "finished";
    }
    ASSERT_TRUE(finished);
    w.run_for(1500);
    ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && a.sim.state_hash() == b.sim.state_hash());
    ASSERT_EQ(j.get("state_hash").str(), hex_u64(a.sim.state_hash().total));              // the referee that was restored and the two machines that lived through the restart
    ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    ASSERT_EQ(entries_but_lock(restart_dir), size_t{0});                                // the match is over: its record is gone
    // ---- the log: what happened, and no key anywhere in it ---------------------------------------------------------------------------------------------------------------------------
    const std::string log_text = text_of_file(log_file);
    ASSERT_TRUE(log_text.find("room " + code + " restored: " + std::to_string(record_turns) + " turns") != std::string::npos);
    ASSERT_TRUE(log_text.find(hex_of(key_a)) == std::string::npos && log_text.find(hex_of(key_b)) == std::string::npos);
    ASSERT_TRUE(log_text.find("restart records in ") != std::string::npos);
    // the server that has nothing to keep stops at once too
    server.signal_it(SIGTERM);
    ASSERT_TRUE(server.wait_exit(5000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::cout << "\n      [restart] " << (stop_signal == SIGTERM ? "SIGTERM" : "SIGKILL") << " in the middle of a match (" << record_turns << " turns in the record, the machines had been sent " << seen
              << "): the server " << (stop_signal == SIGTERM ? "exited " + std::to_string(stop_ms) + " ms after the signal" : std::string("was gone")) << ", started again, both machines found the room by themselves, the match ended in one state" << std::flush;
}


// The output (stdout and stderr) of a shell command and its exit status
std::string shell_output(const std::string& command, int& exit_status) {
    std::string out;
    FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        exit_status = -1;
        return out;
    }
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
    const int st = ::pclose(pipe);
    exit_status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return out;
}

// The scenario of S3.101: a server that runs in a docker container that the operator made (a volume at /results, the game and control ports published on this machine, --reconnect), stopped with
// `docker stop` and started again. The test never makes or removes a container: it only starts, stops and asks the one whose name it is given.
void container_scenario(const std::string& name, uint16_t game_port, uint16_t ctl_port, const std::string& secret) {
    int rc = 0;
    const auto docker = [&rc](const std::string& args) { return shell_output("docker " + args, rc); };
    ASSERT_TRUE(port_accepts(game_port, 20000));
    ASSERT_TRUE(port_accepts(ctl_port, 5000));
    int http = 0;
    const ctl::JsonValue made = ctl_call(ctl_port, "POST", "/rooms", secret, "{\"map\": \"TINY.LVL\", \"players\": 2, \"code\": \"CT-1\", \"reconnect\": true, \"resume_countdown_seconds\": 0, \"max_pause_seconds\": 300}", &http);
    ASSERT_TRUE(http == 201 && made.get("state").str() == "waiting");
    RealWorld w;
    w.port = game_port;
    w.maps = maps_dir();
    RClient& a = w.connect("Ann", "CT-1");
    RClient& b = w.connect("Bob", "CT-1");
    ASSERT_TRUE(w.until([&]() { return a.session != nullptr && b.session != nullptr && a.session->runner().next_turn_expected() >= 130 && b.session->runner().next_turn_expected() >= 130; }, 60000));
    // the record is on the volume, for its owner only (the folder 700, the file 600), and the control interface says so
    const std::string modes = docker("exec " + name + " sh -c 'stat -c %a /results/restart /results/restart/*.restart'");
    ASSERT_TRUE(rc == 0 && modes == "700\n600\n");
    ctl::JsonValue j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("record").get("kept").as_bool_or(false) && j.get("restored").is_null());
    const net::SeatKey key_a = a.lobby->key();
    const net::SeatKey key_b = b.lobby->key();
    // ---- docker stop: SIGTERM, then SIGKILL after the grace period that the stack gives (15 s) -------------------------------------------------------------------------------------------
    w.run_for(1234);
    const uint32_t seen = std::max(a.session->runner().next_turn_expected(), b.session->runner().next_turn_expected());
    const auto began_stop = std::chrono::steady_clock::now();
    docker("stop -t 15 " + name);
    const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
    ASSERT_EQ(rc, 0);
    const std::string exit_code = docker("inspect -f '{{.State.ExitCode}}' " + name);
    ASSERT_TRUE(rc == 0 && exit_code == "0\n");                                         // it stopped by itself (137 would be docker's SIGKILL)
    ASSERT_TRUE(stop_ms < 6000);
    w.run_for(2500);
    ASSERT_TRUE(a.session->reconnecting() && b.session->reconnecting() && !a.lost && !b.lost);
    docker("start " + name);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(port_accepts(game_port, 30000));
    // the machines find the room by its code and their keys, and the match goes on
    ASSERT_TRUE(w.until([&]() { return a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal && !a.session->paused(); }, 60000));
    ASSERT_FALSE(a.lost || b.lost || a.was_rejected || b.was_rejected);
    j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("state").str() == "running" && j.get("restored").is_object() && j.get("restored").get("turns").as_int_or(0) >= static_cast<int64_t>(seen));
    ASSERT_TRUE(j.get("rejoins").as_int_or(0) == 2 && !j.get("paused").as_bool_or(true));
    const int64_t restored_turns = j.get("restored").get("turns").as_int_or(0);
    w.run_for(4000);
    j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
    ASSERT_TRUE(j.get("turns").as_int_or(0) > restored_turns + 60);
    // ---- Bob quits: the match ends in one state on the machines and on the referee ----------------------------------------------------------------------------------------------------
    sim::Command quit;
    quit.type = sim::CommandType::Quit;
    quit.issuer = b.lobby->my_seat();
    ASSERT_TRUE(b.session->submit(quit));
    bool finished = false;
    for (int i = 0; i < 100 && !finished; ++i) {
        w.run_for(200);
        j = ctl_call(ctl_port, "GET", "/rooms/CT-1", secret);
        finished = j.get("state").str() == "finished";
    }
    ASSERT_TRUE(finished);
    w.run_for(1500);
    ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && a.sim.state_hash() == b.sim.state_hash());
    ASSERT_EQ(j.get("state_hash").str(), hex_u64(a.sim.state_hash().total));
    ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    const std::string left = docker("exec " + name + " sh -c 'ls /results/restart'");    // the match is over: its record is gone
    ASSERT_TRUE(rc == 0 && left.empty());
    // the container's log: what happened, and no key anywhere in it
    const std::string log_text = docker("logs " + name);
    ASSERT_TRUE(log_text.find("stopped: 1 restart record(s) made durable and kept") != std::string::npos);
    ASSERT_TRUE(log_text.find("room CT-1 restored: ") != std::string::npos);
    ASSERT_TRUE(log_text.find(hex_of(key_a)) == std::string::npos && log_text.find(hex_of(key_b)) == std::string::npos);
    std::cout << "\n      [container] docker stop took " << stop_ms << " ms (exit code 0), the room came back with " << restored_turns << " turns (the machines had been sent " << seen
              << "), both machines found it by themselves, the match ended in one state" << std::flush;
}

}  // namespace

void run_persist_process_tests() {
    TEST_CASE("S3.95 The Real Program, Told To Stop (SIGTERM) In The Middle Of A Match Of A Demo Room: It Exits At Once With Status 0 (Well Within The 15 s That The Stack Gives Docker), The Record Holds Every Turn That A Machine Has Been Sent, Is For Its Owner Only And Is Still There; The Program Started Again Over The Same Folder Logs The Restored Room, Both Machines (The Real Lobby And Session Over Real TCP) Find The Room By Its Code And Their Keys By Themselves, Nobody Is Told That It Is Ahead, The Match Goes On And Ends: The Referee's State Hash (From The Control Interface) Is The Machines'; The Record Is Gone, No Key Is In The Log") {
        real_process_scenario("persist-89", SIGTERM, false);
    } TEST_END();

    TEST_CASE("S3.96 The Real Program, Killed (SIGKILL: A Crash, The Container's Death) In The Middle Of A Match Of A Room That The Control Interface Made: The Record Holds Every Turn That A Machine Has Been Sent (A Turn Is Written Before It Is Sent: No Turn Is Lost That A Player Has Seen), The Program Started Again Restores The Room, Both Machines Find It By Themselves And Are Not Told That They Are Ahead, The Match Goes On And Ends In One State On The Machines And On The Referee")  {
        real_process_scenario("persist-90", SIGKILL, true);
    } TEST_END();

    TEST_CASE("S3.101 The Real Container (Opt-In: ANTS_PERSIST_CONTAINER Names A Container That The Operator Made From The Server's Image With A Volume At /results, --reconnect And The Ports ANTS_PERSIST_GAME_PORT / ANTS_PERSIST_CTL_PORT / ANTS_PERSIST_SECRET): A Match Of Two Machines, docker stop (SIGTERM, Exit Code 0 Within The Grace), docker start, The Record On The Volume Is For Its Owner Only, The Machines Find The Room By Themselves, The Match Ends In One State On The Machines And The Referee, The Record Is Gone, No Key Is In The Container's Log") {
        const char* name = std::getenv("ANTS_PERSIST_CONTAINER");
        const char* game = std::getenv("ANTS_PERSIST_GAME_PORT");
        const char* ctl_port = std::getenv("ANTS_PERSIST_CTL_PORT");
        const char* secret = std::getenv("ANTS_PERSIST_SECRET");
        if (name == nullptr || game == nullptr || ctl_port == nullptr || secret == nullptr) {
            std::cout << "\n      [container] skipped: ANTS_PERSIST_CONTAINER, _GAME_PORT, _CTL_PORT and _SECRET are not all set (the opt-in check of a real container: tests/test_server)" << std::flush;
        } else {
            const std::string container = name;
            ASSERT_FALSE(container.empty());
            for (const char c : container) ASSERT_TRUE(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-');      // (it goes into a shell command)
            container_scenario(container, static_cast<uint16_t>(std::atoi(game)), static_cast<uint16_t>(std::atoi(ctl_port)), secret);
        }
    } TEST_END();
}

namespace {

// Waits until the log of a server process holds `text` (the server logs the state of its restart folder after its listeners are open: a port that accepts says nothing about it)
bool log_has(const std::string& log_file, const std::string& text, uint32_t timeout_ms) {
    const uint32_t end = wall_ms() + timeout_ms;
    while (static_cast<int32_t>(wall_ms() - end) < 0) {
        if (text_of_file(log_file).find(text) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return text_of_file(log_file).find(text) != std::string::npos;
}

}  // namespace

void run_persist_review_process_tests() {
    TEST_CASE("S3.105 The Real Program Over A Folder That Another Server Holds (M5 Of The Review): The Second Server With The Same --restart-dir Exits With Status 1 And Says Why; The Second Server With The Same Results Folder (The Default Restart Folder) Runs, Logs That Restart Records Are Off, And Keeps None; Neither Touches The Temporary File Of The First One's Record; When The First Is Killed (SIGKILL) The Lock Is Gone With It And A Third Server Takes The Folder") {
        const fs::path root = temp_dir_for("persist-105");
        const fs::path results = root / "results";
        const fs::path restart_dir = results / "restart";
        const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
        const auto args = [&](uint16_t port, const std::vector<std::string>& more) {
            std::vector<std::string> a = {"--maps", maps_dir(), "--port", std::to_string(port)};
            a.insert(a.end(), more.begin(), more.end());
            return a;
        };
        // ---- the first server has the folder (the default one of its results folder) ---------------------------------------------------------------------------------------------------------------
        const std::string log_a = (root / "a.log").string();
        ServerProcess a;
        ASSERT_TRUE(a.start(args(free_tcp_port(), {"--results-dir", results.string(), "--reconnect"}), secret, log_a));
        ASSERT_TRUE(log_has(log_a, "restart records in ", 15000));
        ASSERT_TRUE(fs::exists(restart_dir / ".lock"));
        const fs::path temp_of_a = restart_dir / "room-A-0000abcd.restart.0123456789abcdef.tmp";       // a record that the first server is making, as far as anybody else can tell
        write_all_bytes(temp_of_a, {1, 2, 3});
        // ---- a second server that is told to keep its records in that folder: it cannot, and the operator asked for it: status 1 ---------------------------------------------------------------------------
        {
            const std::string log_b = (root / "b.log").string();
            ServerProcess b;
            ASSERT_TRUE(b.start(args(free_tcp_port(), {"--restart-dir", restart_dir.string()}), secret, log_b));
            int status = 0;
            ASSERT_TRUE(b.wait_exit(15000, status) >= 0);
            ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 1);
            ASSERT_TRUE(text_of_file(log_b).find("another server is using the restart folder") != std::string::npos);
        }
        // ---- a second server over the same results folder: its default restart folder is taken: it runs without records and says so ------------------------------------------------------------
        {
            const std::string log_c = (root / "c.log").string();
            ServerProcess c;
            const uint16_t port_c = free_tcp_port();
            ASSERT_TRUE(c.start(args(port_c, {"--results-dir", results.string(), "--reconnect"}), secret, log_c));
            ASSERT_TRUE(log_has(log_c, "restart records are off: another server is using the restart folder", 15000));
            ASSERT_TRUE(port_accepts(port_c, 15000));                                                  // (it runs: the game goes on without records)
            ASSERT_TRUE(text_of_file(log_c).find("restart records in ") == std::string::npos);
            c.signal_it(SIGTERM);
            int status = 0;
            ASSERT_TRUE(c.wait_exit(10000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        ASSERT_TRUE(fs::exists(temp_of_a));                                                           // neither of them removed what is the first server's
        // ---- the first server dies without a word (SIGKILL): the lock goes with the process, and a third server has the folder -----------------------------------------------------------------------
        a.signal_it(SIGKILL);
        int killed = 0;
        ASSERT_TRUE(a.wait_exit(10000, killed) >= 0 && WIFSIGNALED(killed));
        {
            const std::string log_d = (root / "d.log").string();
            ServerProcess d;
            ASSERT_TRUE(d.start(args(free_tcp_port(), {"--restart-dir", restart_dir.string(), "--reconnect"}), secret, log_d));
            ASSERT_TRUE(log_has(log_d, "restart records in ", 15000));
            ASSERT_TRUE(text_of_file(log_d).find("another server") == std::string::npos);
            ASSERT_FALSE(fs::exists(temp_of_a));                                                      // (the lock is held now: the leftover of the dead server is cleaned up)
            d.signal_it(SIGTERM);
            int status = 0;
            ASSERT_TRUE(d.wait_exit(10000, status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    } TEST_END();

    TEST_CASE("S3.106 The Restore Is Bounded (M2 Of The Review): Seven Copies Of One Record (Rooms Of The Demo Kind) Are Restored Newest First Within A Budget Of A Few Seconds (Measured By A Clock That The Test Makes Go): The Ones That The Budget Reaches Are Back, Paused, Every Seat Held; The Rest Are Deferred (Their Records Stay On Disk Untouched, Their Codes Are Taken: create_room Says 409, They Are Not Counted By /busy) And A Hello With A Key For A Deferred Code Restores That Room (Not A New Demo Room) And Rejoins The Player; One Room's Replay May Not Pass Its Own Cap (Refused As Too Slow, A Failed Room); The Rooms Begin After The Replays With A Clock That Starts Then (The 90 s Wait For The Vote Is Not Eaten By The Replays Of The Others), A Room That Is Restored By A Hello Too; A Stop Flag (Before The First Room, In The Middle Of A Replay, Between Two Rooms, Between Two Rooms That Are Too Short To Be Asked In) Ends The Restore At Once And Leaves Every Record As It Was") {
        {
            const RestartConfig defaults;                                                                                   // the budgets: a minute of a deploy's time is a minute of nobody's game
            ASSERT_TRUE(defaults.replay_budget_ms == 20000 && defaults.restore_budget_ms == 30000 && !defaults.clock_ms);
        }
        {   // seven copies, a budget for about two of them: the newest are restored, the rest wait for their first player
            ServerLimits limits;
            limits.demo_rooms = 16;
            limits.demo_map = "TINY.LVL";
            PWorld w("persist-106a", limits);
            w.start_server(500);
            const int rooms = 7;
            const auto code_of = [](int i) { return "demo-tiny-2p-cp" + std::to_string(i); };
            crash_with_record_of(w, code_of(0), 20000, 3);
            const RestartLoaded original = w.read_record(code_of(0));
            ASSERT_TRUE(original.ok() && original.turn_count > 300);
            for (int i = 1; i < rooms; ++i) copy_record_as(w, code_of(0), code_of(i), std::chrono::seconds(0));
            for (int i = 0; i < rooms; ++i) fs::last_write_time(w.record_path(code_of(i)), fs::file_time_type::clock::now() - std::chrono::seconds(10 * (rooms - i)));   // the last copy is the newest: not the order of the names
            std::map<std::string, std::vector<uint8_t>> on_disk;
            for (int i = 0; i < rooms; ++i) on_disk[code_of(i)] = read_all_bytes(w.record_path(code_of(i)));
            FakeClock clock;                                                                                                // every reading is 100 ms: a replay reads it twice every 20 turns (4 s a room)
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_budget_ms = 12000;
            w.restart.replay_budget_ms = 20000;
            const uint32_t before = clock.ms;
            w.start_server(500);
            w.now += clock.ms - before;                                                                                     // (the world's clock goes on by what the replays took, as a server's does)
            clock.real = true;
            const size_t restored = w.report.count(RestoreItem::Outcome::Restored);
            const size_t deferred = w.report.count(RestoreItem::Outcome::Deferred);
            ASSERT_TRUE(restored >= 2 && deferred >= 2 && restored + deferred == static_cast<size_t>(rooms) && w.report.count(RestoreItem::Outcome::Ended) == 0 && !w.report.stopped);
            ASSERT_EQ(w.mgr->room_count(), restored);
            ASSERT_EQ(w.mgr->deferred_count(), deferred);
            for (int i = 0; i < rooms; ++i) {                                                                               // newest first: the restored are exactly the newest records
                const std::string code = code_of(i);
                RoomStatus s;
                const bool alive = w.mgr->status(code, s, w.server_now());
                ASSERT_MSG(alive == (static_cast<size_t>(rooms - 1 - i) < restored), code);
                if (alive) ASSERT_MSG(s.state == RoomState::Running && s.restored && s.paused && s.absent.size() == 3 && s.record_kept, code);
                ASSERT_MSG(fs::exists(w.record_path(code)), code);                                                          // every record is still on disk
                if (!alive) ASSERT_MSG(read_all_bytes(w.record_path(code)) == on_disk[code], code);                         // and a deferred one is as it was, byte for byte
            }
            const std::string late = code_of(0);                                                                            // the oldest: deferred
            const std::string next_late = code_of(rooms - 1 - static_cast<int>(restored));                                  // the newest of the deferred
            ASSERT_EQ(w.mgr->create_room(held_spec(late, 2), w.server_now()).http_status, 409);                             // its code is taken
            ASSERT_TRUE(w.mgr->busy(w.server_now()).matches == restored && w.mgr->busy(w.server_now()).players == 3 * restored);       // a deferred room is nothing that a restart would interrupt
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find(late) != std::string::npos && n.find("deferred") != std::string::npos);
            ASSERT_TRUE(noted);
            // a Hello with a key for a deferred code (a demo code: the room that it names exists, so no new demo room is made): that room is restored now, and the player is back in it
            RClient& back = w.connect(original.head.start.names[0], late, 0, original.head.keys[0]);
            ASSERT_TRUE(w.until([&]() { return w.status(late).rejoins == 1; }, 60000));
            const RoomStatus s = w.status(late);
            ASSERT_TRUE(s.state == RoomState::Running && s.restored && s.restored_turns == original.turn_count && s.record_kept && !back.lost && back.session != nullptr);
            ASSERT_EQ(w.mgr->deferred_count(), deferred - 1);
            RoomStatus other;
            ASSERT_FALSE(w.mgr->status(next_late, other, w.server_now()));                                                  // (the others still wait: nobody asked for them)
            // the server stops and starts again with time enough: everything that was on disk is restored (the deferred records were never touched)
            w.stop_server(true);
            ASSERT_EQ(w.record_files().size(), static_cast<size_t>(rooms));
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == static_cast<size_t>(rooms) && w.report.count(RestoreItem::Outcome::Deferred) == 0 && w.mgr->deferred_count() == 0);
        }
        {   // the room's own cap: a replay that takes longer than its budget is refused as too slow (a failed room, its record deleted)
            PWorld w("persist-106b");
            w.start_server(500);
            crash_with_record_of(w, "CAP-1", 20000, 2);
            copy_record_as(w, "CAP-1", "CAP-2", std::chrono::seconds(10));
            FakeClock clock;
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_budget_ms = 600000;
            w.restart.replay_budget_ms = 2000;                                                                              // 4 s a room by this clock
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 2 && w.report.count(RestoreItem::Outcome::Restored) == 0 && w.report.count(RestoreItem::Outcome::Deferred) == 0);
            for (const RestoreItem& i : w.report.items) ASSERT_TRUE(i.note.find("longer than the 2 s") != std::string::npos);
            ASSERT_TRUE(w.status("CAP-1").state == RoomState::Failed && w.status("CAP-1").reason.find("longer than the 2 s") != std::string::npos && w.record_files().empty());
        }
        {   // the rooms begin together, after the replays, with a clock that starts then: the wait of 90 s for a vote counts from there
            PWorld w("persist-106c");
            w.start_server(500);
            crash_with_record_of(w, "FC-1", 20000, 2);
            copy_record_as(w, "FC-1", "FC-2", std::chrono::seconds(10));
            FakeClock clock;
            clock.step_ms = 1000;                                                                                           // a second a reading: a room takes more than half a minute by this clock
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_budget_ms = 1000000;
            w.restart.replay_budget_ms = 100000;
            const uint32_t before = clock.ms;
            w.start_server(500);
            const uint32_t took = clock.ms - before;
            ASSERT_TRUE(took > 40000 && w.report.count(RestoreItem::Outcome::Restored) == 2);
            w.now += took;                                                                                                  // the world has gone on by what the replays took
            const auto absent_for = [&](const std::string& code) {
                const RoomStatus s = w.status(code);
                uint32_t most = 0;
                for (const RoomStatus::Absent& a : s.absent) most = std::max(most, a.away_s);
                return most;
            };
            for (const char* code : {"FC-1", "FC-2"}) {
                const RoomStatus s = w.status(code);
                ASSERT_MSG(s.restored && s.paused && s.absent.size() == 2 && s.vote_seat == 255, code);
                ASSERT_MSG(absent_for(code) <= 3, code);                                                                    // the pause began when the room began, not `took` ago
            }
            w.run(60000);
            for (const char* code : {"FC-1", "FC-2"}) {
                ASSERT_MSG(w.status(code).vote_seat == 255 && absent_for(code) >= 58 && absent_for(code) <= 63, code);       // 60 s on: no vote yet (it opens at 90 s)
            }
            w.run(32000);
            for (const char* code : {"FC-1", "FC-2"}) ASSERT_MSG(w.status(code).vote_seat != 255, code);                     // 92 s on: the vote is open
        }
        {   // a room that a Hello restores begins when its replay is over, not when the Hello came: the wait for the vote counts from there too
            PWorld w("persist-106e");
            w.start_server(500);
            crash_with_record_of(w, "LZ-1", 20000, 2);
            copy_record_as(w, "LZ-1", "LZ-2", std::chrono::seconds(10));
            FakeClock clock;                                                                                                // 4 s a room by this clock
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_budget_ms = 6000;                                                                             // the first room is replayed (4 s), the second is put off at 6 s
            w.restart.replay_budget_ms = 20000;
            const uint32_t at_start = clock.ms;
            w.start_server(500);
            w.now += clock.ms - at_start;
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Deferred) == 1 && w.mgr->deferred_count() == 1);
            const uint32_t before = clock.ms;
            net::Connection* hello_link = w.open_link();                                                                    // a stranger asks for the deferred room: the room is restored, the stranger is told MatchRunning
            net::HelloMsg h;
            h.room = "LZ-2";
            hello_link->send(net::encode(h));
            w.run(100);
            const uint32_t took = clock.ms - before;
            ASSERT_TRUE(took >= 1500 && took <= 4000 && w.mgr->deferred_count() == 0);                                      // (about 2 s by that clock: the replay of the second room, read once every 20 turns)
            ASSERT_TRUE(w.status("LZ-2").restored && w.status("LZ-2").paused);
            ASSERT_EQ(reject_on(hello_link), static_cast<int>(net::RejectReason::MatchRunning));
            w.now += took;                                                                                                  // the world has gone on by what that replay took
            w.run(30000);
            uint32_t most = 0;
            for (const RoomStatus::Absent& a : w.status("LZ-2").absent) most = std::max(most, a.away_s);
            ASSERT_TRUE(most >= 29 && most <= 31);                                                                          // about 30 s since the room began (the 2 s of the Hello's own replay are not part of it: a room that began at the Hello would be at 32)
        }
        {   // the server is told to stop: before the first room, in the middle of a replay, between two rooms; every record stays as it was and nothing is restored; then the restart restores all three
            PWorld w("persist-106d");
            w.start_server(500);
            crash_with_record_of(w, "SP-0", 20000, 2);
            const RestartLoaded original = w.read_record("SP-0");
            for (int i = 1; i < 3; ++i) copy_record_as(w, "SP-0", "SP-" + std::to_string(i), std::chrono::seconds(10 * i));
            fs::last_write_time(w.record_path("SP-0"), fs::file_time_type::clock::now() - std::chrono::seconds(1));
            std::map<std::string, std::vector<uint8_t>> on_disk;
            for (int i = 0; i < 3; ++i) on_disk["SP-" + std::to_string(i)] = read_all_bytes(w.record_path("SP-" + std::to_string(i)));
            const size_t checks_in_a_room = original.turn_count / net::kHashEveryTurns;
            ASSERT_TRUE(checks_in_a_room >= 5);
            const auto untouched = [&]() {
                for (const auto& kv : on_disk) {
                    if (!fs::exists(w.record_path(kv.first)) || read_all_bytes(w.record_path(kv.first)) != kv.second) return false;
                }
                return w.record_files().size() == on_disk.size();
            };
            int polls = 0;
            const auto stop_at = [&polls](int n) { return [&polls, n]() { return ++polls >= n; }; };
            const struct { const char* what; int at; } cases[] = {
                {"before the first room", 1},
                {"in the middle of the first replay", 4},
                {"between the first room and the second", static_cast<int>(1 + checks_in_a_room + 1)},
            };
            for (const auto& c : cases) {
                polls = 0;
                w.restore_should_stop = stop_at(c.at);
                w.start_server(500);
                ASSERT_MSG(w.report.stopped && w.report.left_on_disk == 3 && w.report.items.empty(), c.what);
                ASSERT_MSG(w.mgr->room_count() == 0 && w.mgr->deferred_count() == 0, c.what);                               // nothing was restored, nothing is a failed room
                ASSERT_MSG(untouched(), c.what);                                                                            // every record is as it was, byte for byte
                ASSERT_MSG(polls == c.at, c.what);                                                                          // and the restore asked no more once it was told
                bool noted = false;
                for (const std::string& n : w.notices) noted = noted || n.find("restore was stopped") != std::string::npos;
                ASSERT_MSG(noted, c.what);
                ASSERT_MSG(w.mgr->shutdown(w.server_now()) == 0, c.what);
                w.stop_server(false);
            }
            w.restore_should_stop = nullptr;
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 3 && !w.report.stopped);
        }
        {   // two records that are too short to be asked in the middle of their replays (fewer than 20 turns): only the question between the rooms can stop the restore
            PWorld w("persist-106f");
            w.start_server(500);
            crash_with_record_of(w, "SH-0", 500, 2);
            ASSERT_TRUE(w.read_record("SH-0").turn_count < net::kHashEveryTurns);
            copy_record_as(w, "SH-0", "SH-1", std::chrono::seconds(10));
            fs::last_write_time(w.record_path("SH-0"), fs::file_time_type::clock::now() - std::chrono::seconds(1));
            int polls = 0;
            w.restore_should_stop = [&polls]() { return ++polls >= 2; };                                                    // yes at the second question: the one before the second room
            w.start_server(500);
            ASSERT_TRUE(w.report.stopped && w.report.left_on_disk == 2 && w.mgr->room_count() == 0 && polls == 2 && w.record_files().size() == 2);
        }
        {   // the same two short records and the budget: the question between the rooms is the only one that they allow (no 20 turns to ask in), and it puts the second room off
            PWorld w("persist-106g");
            w.start_server(500);
            crash_with_record_of(w, "SB-0", 500, 2);
            copy_record_as(w, "SB-0", "SB-1", std::chrono::seconds(10));
            fs::last_write_time(w.record_path("SB-0"), fs::file_time_type::clock::now() - std::chrono::seconds(1));
            FakeClock clock;                                                                                                // 100 ms a reading
            w.restart.clock_ms = clock.as_function();
            w.restart.restore_budget_ms = 250;
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.report.count(RestoreItem::Outcome::Deferred) == 1 && w.mgr->deferred_count() == 1 && !w.report.stopped);
            ASSERT_TRUE(w.status("SB-0").restored && w.record_files().size() == 2);
        }
    } TEST_END();
}

void run_persist_review_process_tests_2() {
    TEST_CASE("S3.107 The Real Program Told To Stop While It Restores (M2 Of The Review): Eight Records Of A Busy Match On TREASURE (Four Players, Three Minutes Of Play) Take Seconds To Replay; SIGTERM Comes While The First Are Being Replayed And The Program Leaves At Once (The Restore Looks At The Stop Flag Every 20 Turns: Not After The Replays Are Over), With Status 0, Every Record Untouched, Nothing Deleted, Nothing Restored") {
        const fs::path root = temp_dir_for("persist-107");
        const fs::path results = root / "results";
        const fs::path restart_dir = results / "restart";
        fs::create_directories(restart_dir);
        const RestartStore names{[&]() {
            RestartConfig c;
            c.dir = restart_dir.string();
            return c;
        }()};
        std::vector<std::string> paths;
        std::vector<std::vector<uint8_t>> contents;
        uint32_t turns = 0;
        {   // a match of four machines on the busiest map, played for three minutes (they give orders: the replay does what the match did), and copies of its record under other codes
            PWorld w("persist-107w");
            w.start_server(500);
            RoomSpec spec = held_spec("ST-1", 4, "TREASURE.LVL");
            spec.run_ms = 24u * 3600u * 1000u;
            std::vector<RClient*> m = play_room(w, spec, 180000);
            for (RClient* p : m) p->reconnects = false;
            w.stop_server(false);
            const std::vector<uint8_t> bytes = read_all_bytes(w.record_path("ST-1"));
            const RestartLoaded base = w.read_record("ST-1");
            ASSERT_TRUE(base.ok() && !base.torn);
            turns = base.turn_count;
            const auto frames = frames_of(bytes);
            for (int i = 1; i <= 8; ++i) {
                RestartHead head = base.head;
                head.code = "ST-" + std::to_string(i);
                std::vector<uint8_t> copy = with_magic(encode_restart_head(head));
                const size_t at = copy.size();
                copy.resize(at + (bytes.size() - frames[0].second));
                std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(frames[0].second), bytes.end(), copy.begin() + static_cast<std::ptrdiff_t>(at));
                paths.push_back(names.path_for(head.code));
                write_all_bytes(paths.back(), copy);
                contents.push_back(copy);
            }
        }
        const std::string secret = "test-secret-0123456789abcdef0123456789abcdef";
        const std::vector<std::string> args = {"--maps", maps_dir(), "--port", std::to_string(free_tcp_port()), "--results-dir", results.string(), "--reconnect"};
        const std::string log_a = (root / "a.log").string();
        ServerProcess server;
        ASSERT_TRUE(server.start(args, secret, log_a));
        ASSERT_TRUE(log_has(log_a, "restart records in ", 15000));                                // (the restore starts right after this line: the replays take seconds)
        const auto began_stop = std::chrono::steady_clock::now();
        server.signal_it(SIGTERM);
        int status = 0;
        const int64_t took = server.wait_exit(60000, status);
        const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began_stop).count();
        ASSERT_TRUE(took >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ASSERT_TRUE(stop_ms < 1500);                                                              // (not the seconds that the replays would still take)
        for (size_t i = 0; i < paths.size(); ++i) ASSERT_TRUE(fs::exists(paths[i]) && read_all_bytes(paths[i]) == contents[i]);           // every record is as it was: nothing deleted, nothing written
        const std::string log_text = text_of_file(log_a);
        ASSERT_TRUE(log_text.find("the restore was stopped by the server's stop: 8 restart record(s) are left on disk as they were") != std::string::npos);
        ASSERT_TRUE(log_text.find(" restored: ") == std::string::npos && log_text.find("was not restored") == std::string::npos);
        std::cout << "\n      [stop in a restore] the program left " << stop_ms << " ms after SIGTERM, in the middle of the replays of eight matches of " << turns << " turns, with every record untouched" << std::flush;
    } TEST_END();

    TEST_CASE("S3.108 The Record Exists Before The Start Is Sent (L3 Of The Review): A Room Of Two Machines Is Started; At The Moment The Server Sends The Start Message To Each Of Them (A Tap On The Server's End Of The Links Looks At The Folder) The Record Of The Room Is On Disk, Whole, With Both Keys And The Start Message That Is Being Sent; A Start That Is Cancelled (A Machine That Cannot Load The Map) Takes Its Record Away And The Next Start Makes It Again Before Its Start Is Sent") {
        PWorld w("persist-108");
        w.start_server(500);
        int starts_sent = 0;
        int with_record = 0;
        int whole_and_the_same = 0;
        w.on_server_send = [&](const std::vector<uint8_t>& m) {
            if (net::peek_type(m) != net::MsgType::Start) return;
            ++starts_sent;
            const std::string path = w.record_path("L3-1");
            if (!fs::exists(path)) return;
            ++with_record;
            const RestartLoaded rec = read_restart_record(path, 1ull << 30);
            net::StartMsg sent;
            if (rec.ok() && rec.head.code == "L3-1" && rec.turns.empty() && !net::key_is_zero(rec.head.keys[0]) && !net::key_is_zero(rec.head.keys[1]) && net::decode(m, sent) && sent.seed == rec.head.start.seed && sent.roster == rec.head.start.roster &&
                sent.map_hash == rec.head.start.map_hash) {
                ++whole_and_the_same;                                                           // (the addresses of the clients are the one thing that the record's copy does not have: it has no business with them)
            }
        };
        ASSERT_TRUE(w.mgr->create_room(held_spec("L3-1", 2), w.server_now()).ok);
        w.connect("Ann", "L3-1");
        RClient& bad = w.connect("Bob", "L3-1");
        bad.fail_load = true;                                                                   // Bob cannot load the map: the first start is cancelled
        ASSERT_TRUE(w.until([&]() { return starts_sent >= 2 && w.status("L3-1").state == RoomState::Waiting; }, 20000));
        ASSERT_TRUE(w.record_files().empty());                                                  // the cancel took the record away
        ASSERT_EQ(starts_sent, 2);
        ASSERT_TRUE(with_record == 2 && whole_and_the_same == 2);                               // both Starts of the first attempt were sent with the record on disk
        bad.fail_load = false;                                                                  // (Bob stays in the room: the retry comes two seconds after the cancel, and now he can load the map)
        ASSERT_TRUE(w.until([&]() { return w.status("L3-1").state == RoomState::Running; }, 30000));
        ASSERT_TRUE(starts_sent >= 4 && with_record == starts_sent && whole_and_the_same == starts_sent);       // every Start of every attempt was sent with its record on disk
        ASSERT_TRUE(w.status("L3-1").record_kept && w.record_files().size() == 1);
    } TEST_END();

    TEST_CASE("S3.109 A Record That Cannot Be Deleted Is Not Forgotten (L1 Of The Review): The Delete That Fails (Here: A Folder With Something In It Takes The File's Name) Leaves The File Of A Room That Is Over; The Room's Status Says So (record.stale, A Note) And Says Kept While The File Is There (Never Kept: False), The Log Gets A Line; The Server Tries Again Every 10 s (Not Sooner) And When It Can The Status Is That Of A Room That Is Over And The Log Says So; The Server's Stop Tries Once More") {
#ifndef _WIN32
        const auto make_undeletable = [](PWorld& w, const std::string& code) {                  // the room's descriptor goes on writing the file that was moved aside; the name is now a folder that is not empty
            const std::string path = w.record_path(code);
            fs::rename(path, path + ".aside");
            fs::create_directory(path);
            write_all_bytes(fs::path(path) / "x", {1});
        };
        PWorld w("persist-109");
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("DL-1", 2), 6000);
        make_undeletable(w, "DL-1");
        RoomStatus s = w.status("DL-1");
        ASSERT_TRUE(s.record_kept && !s.record_stale);
        ctl::HttpRequest rq;
        rq.method = "DELETE";
        rq.path = "/rooms/DL-1";
        ASSERT_EQ(handle_control(*w.mgr, rq, w.server_now()).status, 200);                      // the owner closes the room: its record is to go
        const std::string path = w.record_path("DL-1");
        ASSERT_TRUE(fs::exists(path));                                                           // ... and cannot
        s = w.status("DL-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.record_kept && s.record_stale && s.record_note.find("could not be deleted") != std::string::npos);      // kept: true while the file is there
        {
            const ctl::JsonValue j = status_json(w, "DL-1");
            ASSERT_TRUE(j.get("record").get("kept").as_bool_or(false) && j.get("record").get("stale").as_bool_or(false) && j.get("record").get("note").str() == s.record_note);
        }
        w.collect();                                                                             // (the lines that the server would have logged)
        bool noted = false;
        for (const std::string& n : w.notices) noted = noted || (n.find("could not be deleted") != std::string::npos && n.find("DL-1") != std::string::npos);
        ASSERT_TRUE(noted);
        for (const std::string& n : w.notices) ASSERT_TRUE(n.find(w.restart.dir) == std::string::npos);        // (a line names the file, not the folder of the server)
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{1});
        // the obstacle goes at once; the server tries again every 10 s: not before 5 s, and by 11 s the file is gone
        fs::remove(fs::path(path) / "x");
        w.run(5000);
        ASSERT_TRUE(fs::exists(path) && w.status("DL-1").record_stale);
        w.run(6000);
        ASSERT_FALSE(fs::exists(path));
        s = w.status("DL-1");
        ASSERT_TRUE(!s.record_kept && !s.record_stale && s.record_note == "the room is over");
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{0});
        noted = false;
        for (const std::string& n : w.notices) noted = noted || (n.find("was deleted") != std::string::npos && n.find("DL-1") != std::string::npos);
        ASSERT_TRUE(noted);
        // the retries go on while the obstacle stays: a second room, 25 s of tries, the file is still there and still stale
        std::vector<RClient*> m2 = play_room(w, held_spec("DL-2", 2), 6000);
        make_undeletable(w, "DL-2");
        ASSERT_TRUE(w.mgr->close_room("DL-2", w.server_now()));
        w.run(25000);
        ASSERT_TRUE(fs::exists(w.record_path("DL-2")) && w.status("DL-2").record_stale && w.mgr->restart_store()->stale_count() == 1);
        // the stop tries once more, at once: the obstacle goes just before it
        fs::remove(fs::path(w.record_path("DL-2")) / "x");
        w.mgr->shutdown(w.server_now());
        ASSERT_FALSE(fs::exists(w.record_path("DL-2")));
        ASSERT_EQ(w.mgr->restart_store()->stale_count(), size_t{0});
#endif
    } TEST_END();

    TEST_CASE("S3.110 A Record That Was Read And Refused Is Kept For A Day (L2 Of The Review): The Record Of A Match That Another Network Protocol Ended Goes To restart/refused/ Whole (Mode 600 In A Folder Of 700), The Log Says So, And The Owner Who Moves It Back When The Cause Is Gone Has The Match Restored; A Record That Is Corrupt Is Deleted At Once; At Start The Folder Is Purged Of What Is Older Than 24 Hours (Not Of Younger Files); The Folder Is Held Under The Budget (The Oldest Go First) And A Record That Alone Passes It Is Deleted") {
        const auto refused_dir = [](PWorld& w) { return fs::path(w.restart.dir) / "refused"; };
        const auto name_of = [](PWorld& w, const std::string& code) { return fs::path(w.record_path(code)).filename(); };
        {   // refused: kept, whole, private; moved back by the owner it is restored
            PWorld w("persist-110a");
            w.start_server(500);
            crash_with_record_of(w, "RF-1", 6000);
            const std::vector<uint8_t> bytes = read_all_bytes(w.record_path("RF-1"));
            w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            w.start_server(500);
            ASSERT_TRUE(w.report.items.size() == 1 && w.report.items[0].outcome == RestoreItem::Outcome::Ended && w.status("RF-1").state == RoomState::Failed);
            ASSERT_TRUE(w.record_files().empty());                                               // (not in the folder that a start reads any more)
            const fs::path kept = refused_dir(w) / name_of(w, "RF-1");
            ASSERT_TRUE(fs::exists(kept) && read_all_bytes(kept) == bytes);
#ifndef _WIN32
            struct stat st;
            ASSERT_EQ(::stat(kept.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);
            ASSERT_EQ(::stat(refused_dir(w).c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);
#endif
            bool noted = false;
            for (const std::string& n : w.notices) noted = noted || (n.find("RF-1") != std::string::npos && n.find("refused") != std::string::npos && n.find("24 hours") != std::string::npos);
            ASSERT_TRUE(noted);
            w.stop_server(false);
            w.restart.identity.protocol = net::kProtocolVersion;                                 // the cause is gone (the server is what it was) and the owner moves the file back
            fs::rename(kept, w.record_path("RF-1"));
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1 && w.status("RF-1").restored && w.status("RF-1").state == RoomState::Running);
        }
        {   // corrupt: deleted at once, nothing kept (a bit flipped in the middle, and a file that is no record at all)
            PWorld w("persist-110b");
            w.start_server(500);
            crash_with_record_of(w, "RF-2", 6000);
            std::vector<uint8_t> bytes = read_all_bytes(w.record_path("RF-2"));
            bytes[bytes.size() / 2] ^= 0x10;
            write_all_bytes(w.record_path("RF-2"), bytes);
            write_all_bytes(fs::path(w.restart.dir) / "room-JUNK-00000001.restart", {'j', 'u', 'n', 'k'});
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Unreadable) == 2 && w.record_files().empty());
            ASSERT_TRUE(!fs::exists(refused_dir(w)) || fs::is_empty(refused_dir(w)));
        }
        {   // the purge at start: what is older than 24 hours goes, what is younger stays
            PWorld w("persist-110c");
            w.start_server(500);
            w.stop_server(false);
            fs::create_directories(refused_dir(w));
            const auto plant = [&](const char* name, std::chrono::hours age) {
                write_all_bytes(refused_dir(w) / name, {1, 2, 3});
                fs::last_write_time(refused_dir(w) / name, fs::file_time_type::clock::now() - age);
            };
            plant("old-1.restart", std::chrono::hours(25));
            plant("old-2.restart", std::chrono::hours(72));
            plant("young-1.restart", std::chrono::hours(23));
            plant("young-2.restart", std::chrono::hours(1));
            w.start_server(500);
            ASSERT_TRUE(!fs::exists(refused_dir(w) / "old-1.restart") && !fs::exists(refused_dir(w) / "old-2.restart"));
            ASSERT_TRUE(fs::exists(refused_dir(w) / "young-1.restart") && fs::exists(refused_dir(w) / "young-2.restart"));
        }
        {   // the folder is held under the budget: the oldest go first; a record that alone passes the budget is deleted
            PWorld w("persist-110d");
            w.start_server(500);
            crash_with_record_of(w, "RF-3", 6000);
            const uint64_t size = fs::file_size(w.record_path("RF-3"));
            fs::create_directories(refused_dir(w));
            const auto plant = [&](const char* name, std::chrono::hours age) {
                write_all_bytes(refused_dir(w) / name, std::vector<uint8_t>(static_cast<size_t>(size), 7));
                fs::last_write_time(refused_dir(w) / name, fs::file_time_type::clock::now() - age);
            };
            plant("a.restart", std::chrono::hours(5));                                           // the oldest
            plant("b.restart", std::chrono::hours(4));
            plant("c.restart", std::chrono::hours(3));
            w.restart.budget_bytes = 3 * size + 10;                                              // room for three records of this size, not four
            w.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            w.start_server(500);
            ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Ended) == 1);
            ASSERT_TRUE(!fs::exists(refused_dir(w) / "a.restart") && fs::exists(refused_dir(w) / "b.restart") && fs::exists(refused_dir(w) / "c.restart") && fs::exists(refused_dir(w) / name_of(w, "RF-3")));
            // a record that alone is more than the budget is deleted, not kept
            w.stop_server(false);
            PWorld small("persist-110e");
            small.start_server(500);
            crash_with_record_of(small, "RF-4", 6000);
            const uint64_t big = fs::file_size(small.record_path("RF-4"));
            small.restart.budget_bytes = big - 1;
            small.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            small.start_server(500);
            ASSERT_TRUE(small.report.count(RestoreItem::Outcome::Ended) == 1 && small.record_files().empty());
            ASSERT_TRUE(!fs::exists(refused_dir(small) / name_of(small, "RF-4")));
        }
    } TEST_END();

    TEST_CASE("S3.111 A Folder That Cannot Be Written Is Found Out At The Start (L5 Of The Review): prepare() Makes A Probe File And Deletes It; A Folder Where A Write Fails (The Process May Not Grow A File: EFBIG, As A Full Disk Or A Quota Would Answer) Is Refused With The Reason And Holds No Lock; A Folder That Can Be Written Leaves No Probe Behind; A Room Manager Over Such A Folder Keeps No Records And Says Why") {
#ifndef _WIN32
        {
            RestartConfig cfg = test_restart_config("persist-111a");
            std::string why;
            {
                RestartStore ok(cfg);
                ASSERT_TRUE(ok.prepare(why));
                ASSERT_EQ(entries_but_lock(cfg.dir), size_t{0});                                   // no probe is left behind
            }
            {
                const FileSizeLimit none(0);                                                       // every write fails (the limit is 0 bytes) and no signal comes
                RestartStore refused(cfg);
                ASSERT_FALSE(refused.prepare(why));
                ASSERT_TRUE(why.find("cannot be written") != std::string::npos && why.find("File too large") != std::string::npos);
                ASSERT_EQ(entries_but_lock(cfg.dir), size_t{0});                                   // (and the probe that was made is deleted)
                RoomManager keeper{MapStore(maps_dir())};
                ASSERT_FALSE(keeper.enable_restart_records(cfg, why));
                ASSERT_TRUE(keeper.restart_store() == nullptr && why.find("cannot be written") != std::string::npos);
            }
            RestartStore again(cfg);                                                               // the refused store holds no lock: the folder is free once writes work
            ASSERT_TRUE(again.prepare(why));
        }
#endif
    } TEST_END();
}
#endif


// ---------------------------------------------------------------------------------------------------------------------------------
// The restore that never blocks the server (docs/NETWORK_PORT.md "Restart records", "Restoring"): S3.113 and on
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

struct WalkedTurns {
    std::vector<net::TurnMsg> turns;
    bool failed{false};
    std::string why;
    uint32_t read{0};
    bool stays_ended{true};
};

// Every turn that a RestartTurnReader gives for a record, and how it ended
WalkedTurns walk_turns(const RestartLoaded& rec) {
    WalkedTurns out;
    RestartTurnReader reader(rec);
    net::TurnMsg t;
    while (reader.next(t)) out.turns.push_back(t);
    out.failed = reader.failed();
    out.why = reader.why();
    out.read = reader.turns_read();
    out.stays_ended = !reader.next(t) && !reader.next(t);                                      // (a reader that has ended, or failed, says no again)
    return out;
}

}  // namespace

void run_restore_tests() {
    TEST_CASE("S3.113 The Turn Reader (RestartTurnReader, The Pull Reader That A Replay In Slices Needs): It Gives Exactly The Turns Of A Record, One At A Time And In Order, Of Every Kind That The Record Tests Make (A Writer's Record With A Turn To A Frame And Checkpoints Between, Frames Of 20 And Of 4096 Turns, A Match That Was Really Played, A Record With No Turn), Read Whole Or Streaming; It Stops At A Torn Tail Where The Streaming Read Stops; It Fails (failed(), why()) For A Record That Was Not Judged Good And For Bytes That Are Not What Was Judged (Cut Inside A Frame Or At The End Of One, Renumbered, A Frame With Bytes After Its Last Turn, A Turn With Too Many Commands); Two Readers Of One Record Do Not Disturb Each Other; for_each_restart_turn Is The Same Walk") {
        RestartConfig cfg = test_restart_config("restore-113");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const auto read_both = [&](const std::string& path) { return std::make_pair(read_restart_record(path, cfg.max_record_bytes), read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming)); };
        // the reader gives exactly the turns that the whole read keeps, from the whole record and from the streamed one, and ends without a fault
        const auto expect_all = [&](const std::string& path, uint32_t expected, const char* what) {
            const auto both = read_both(path);
            ASSERT_MSG(both.first.ok() && both.second.ok() && both.first.turn_count == expected && both.second.turn_count == expected && both.first.turns.size() == expected, what);
            for (const RestartLoaded* rec : {&both.first, &both.second}) {
                const WalkedTurns w = walk_turns(*rec);
                ASSERT_MSG(!w.failed && w.why.empty() && w.turns.size() == expected && w.read == expected && w.stays_ended, what);
                ASSERT_MSG(same_turns(w.turns, both.first.turns, expected), what);
                for (uint32_t i = 0; i < expected; ++i) ASSERT_MSG(w.turns[i].turn == i, what);       // (numbered from 0 without a hole)
                uint32_t seen = 0;                                                                     // for_each_restart_turn is the same walk
                ASSERT_MSG(for_each_restart_turn(*rec, [&](const net::TurnMsg& t) { return t.turn == seen++; }) && seen == expected, what);
            }
        };
        // ---- a record that the writer made: one turn to a frame, a checkpoint after every 20th turn, commands of every kind that sample_turn makes (a Drop among them) -------------------------------
        std::string path_a;
        {
            auto writer = store.create(sample_head("RD-A"), why);
            ASSERT_TRUE(writer != nullptr);
            for (uint32_t n = 0; n < 200; ++n) {
                ASSERT_TRUE(writer->append_turn(sample_turn(n)));
                if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(writer->append_check(n, 0x1000u + n));
            }
            ASSERT_TRUE(writer->sync());
            path_a = writer->path();
        }
        expect_all(path_a, 200, "the writer's record");
        // ---- frames of many turns (20, then 4096 empty ones) and no turn at all ------------------------------------------------------------------------------------------------------------------
        const std::string path_b = store.path_for("RD-B");
        {
            std::vector<net::TurnMsg> twenty;
            std::vector<net::TurnMsg> next_ten;
            for (uint32_t n = 0; n < 20; ++n) twenty.push_back(sample_turn(n));
            for (uint32_t n = 20; n < 30; ++n) next_ten.push_back(sample_turn(n));
            const std::vector<uint8_t> head = encode_restart_head(sample_head("RD-B"));
            write_all_bytes(path_b, with_magic(concat({head, test_frame(2, turns_payload(0, twenty)), test_frame(3, check_payload(19, 77)), test_frame(2, turns_payload(20, next_ten))})));
        }
        expect_all(path_b, 30, "frames of 20 and of 10 turns with a checkpoint between");
        const std::string path_big = store.path_for("REC-1");
        write_all_bytes(path_big, empty_turns_record(10000, 4096));                                    // (4096 + 4096 + 1808)
        expect_all(path_big, 10000, "frames of 4096 turns");
        const std::string path_none = store.path_for("RD-E");
        write_all_bytes(path_none, with_magic(encode_restart_head(sample_head("RD-E"))));
        expect_all(path_none, 0, "a record that holds no turn");
        // ---- a match that was really played (the turns of a real room: orders of every kind that the machines give) -----------------------------------------------------------------------------------
        {
            PWorld w("restore-113w");
            w.start_server(500);
            crash_with_record_of(w, "RD-C", 16000);
            const RestartLoaded real = w.read_record("RD-C");
            ASSERT_TRUE(real.ok() && real.turn_count > 250 && !real.checks.empty());
            expect_all(w.record_path("RD-C"), real.turn_count, "a match that was played");
        }
        // ---- a torn tail: the reader stops where the streaming read stops, without a fault -------------------------------------------------------------------------------------------------------------
        {
            std::vector<uint8_t> bytes = read_all_bytes(path_a);
            const std::vector<uint8_t> next = test_frame(2, turns_payload(200, {sample_turn(200)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);
            const std::string torn_path = store.path_for("RD-T");
            write_all_bytes(torn_path, bytes);
            const auto both = read_both(torn_path);
            ASSERT_TRUE(both.first.ok() && both.second.ok() && both.first.torn && both.second.torn && both.second.good_bytes < both.second.file_bytes);
            for (const RestartLoaded* rec : {&both.first, &both.second}) {
                const WalkedTurns w = walk_turns(*rec);
                ASSERT_TRUE(!w.failed && w.turns.size() == 200 && w.read == 200 && w.stays_ended);        // (the bytes of the torn frame are not read as a turn)
                ASSERT_TRUE(same_turns(w.turns, both.first.turns, 200));
            }
        }
        // ---- bytes that are not what was judged ------------------------------------------------------------------------------------------------------------------------------------------------------
        const RestartLoaded base = read_restart_record(path_a, cfg.max_record_bytes, RestartRead::Streaming);
        ASSERT_TRUE(base.ok() && !base.bytes.empty());
        const std::vector<std::pair<size_t, size_t>> frames = frames_of(base.bytes);
        std::vector<size_t> turn_frames;
        for (size_t i = 0; i < frames.size(); ++i) {
            if (base.bytes[frames[i].first] == 2) turn_frames.push_back(i);
        }
        ASSERT_EQ(turn_frames.size(), size_t{200});
        {   // cut inside a frame: the turns before it are given, then the reader fails (and says so again)
            RestartLoaded cut = base;
            cut.bytes.resize(frames[turn_frames[50]].second - 3);
            const WalkedTurns w = walk_turns(cut);
            ASSERT_TRUE(w.failed && w.why.find("end inside a frame") != std::string::npos && w.turns.size() == 50 && w.read == 50 && w.stays_ended);
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(cut, [&](const net::TurnMsg&) { ++seen; return true; }));       // the walk that fails is no complete walk
            ASSERT_EQ(seen, size_t{50});
        }
        {   // cut at the end of a frame: every frame that is left is whole, but the record held more turns than that
            RestartLoaded cut = base;
            cut.bytes.resize(frames[turn_frames[50]].second);
            const WalkedTurns w = walk_turns(cut);
            ASSERT_TRUE(w.failed && w.why.find("fewer turns") != std::string::npos && w.turns.size() == 51 && w.read == 51 && w.stays_ended);
            size_t seen = 0;
            ASSERT_FALSE(for_each_restart_turn(cut, [&](const net::TurnMsg&) { ++seen; return true; }));
            ASSERT_EQ(seen, size_t{51});
        }
        {   // a whole record whose turn list is shorter than the count that it was judged to hold
            RestartLoaded shorter = read_restart_record(path_a, cfg.max_record_bytes);
            shorter.turns.resize(120);
            const WalkedTurns w = walk_turns(shorter);
            ASSERT_TRUE(w.failed && w.why.find("fewer turns") != std::string::npos && w.turns.size() == 120);
        }
        {   // renumbered: the first turn of a frame is not the next turn
            RestartLoaded renumbered = base;
            renumbered.bytes[frames[turn_frames[30]].first + 5] = static_cast<uint8_t>(renumbered.bytes[frames[turn_frames[30]].first + 5] + 1);
            const WalkedTurns w = walk_turns(renumbered);
            ASSERT_TRUE(w.failed && w.why.find("not numbered") != std::string::npos && w.turns.size() == 30 && w.stays_ended);
        }
        {   // a frame of 20 turns that says 19: the bytes of the twentieth are after its last turn
            RestartLoaded shortened = read_restart_record(path_b, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(shortened.ok());
            const auto b_frames = frames_of(shortened.bytes);
            ASSERT_TRUE(shortened.bytes[b_frames[1].first] == 2 && shortened.bytes[b_frames[1].first + 9] == 20);
            shortened.bytes[b_frames[1].first + 9] = 19;
            const WalkedTurns w = walk_turns(shortened);
            ASSERT_TRUE(w.failed && w.why.find("bytes after its last turn") != std::string::npos && w.turns.size() == 19 && w.stays_ended);
        }
        {   // a turn that says it holds more commands than a turn may
            RestartLoaded greedy = read_restart_record(path_big, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_TRUE(greedy.ok());
            const auto g_frames = frames_of(greedy.bytes);
            const size_t at = g_frames[1].first + 5 + 6 + 2 * 5;                                       // the command count of the sixth turn of the first frame
            ASSERT_TRUE(greedy.bytes[at] == 0 && greedy.bytes[at + 1] == 0);
            greedy.bytes[at] = 0xFF;
            greedy.bytes[at + 1] = 0xFF;
            const WalkedTurns w = walk_turns(greedy);
            ASSERT_TRUE(w.failed && w.why.find("more commands than a turn may") != std::string::npos && w.turns.size() == 5 && w.stays_ended);
        }
        {   // a record that was not judged good gives nothing and says why (a flipped bit in its head)
            std::vector<uint8_t> bytes = read_all_bytes(path_a);
            bytes[sizeof(kRestartMagic) + 9] ^= 0x10;
            const std::string rotten_path = store.path_for("RD-R");
            write_all_bytes(rotten_path, bytes);
            const RestartLoaded rotten = read_restart_record(rotten_path, cfg.max_record_bytes, RestartRead::Streaming);
            ASSERT_FALSE(rotten.ok());
            const WalkedTurns w = walk_turns(rotten);
            ASSERT_TRUE(w.failed && w.turns.empty() && w.why.find("not read as a good one") != std::string::npos && w.stays_ended);
            ASSERT_FALSE(for_each_restart_turn(rotten, [](const net::TurnMsg&) { return true; }));
        }
        {   // two readers of one record, taken turn by turn in turns: each gives the whole record (the state of a reader is its own: the manager holds several jobs)
            RestartTurnReader first(base);
            RestartTurnReader second(base);
            std::vector<net::TurnMsg> a;
            std::vector<net::TurnMsg> b;
            net::TurnMsg t;
            for (int round = 0; round < 400; ++round) {
                RestartTurnReader& r = (round % 3 == 0) ? second : first;
                std::vector<net::TurnMsg>& into = (round % 3 == 0) ? b : a;
                if (r.next(t)) into.push_back(t);
            }
            while (first.next(t)) a.push_back(t);
            while (second.next(t)) b.push_back(t);
            ASSERT_TRUE(!first.failed() && !second.failed() && a.size() == 200 && b.size() == 200);
            const RestartLoaded kept = read_restart_record(path_a, cfg.max_record_bytes);
            ASSERT_TRUE(same_turns(a, kept.turns, 200) && same_turns(b, kept.turns, 200));
        }
    } TEST_END();

    TEST_CASE("S3.114 The Replay In Slices (Room::begin_replay, Room::replay_step): A Match That Was Played (Two Persons And A Bot, One Person Gone For Good) Is Replayed By Rooms Of Their Own In One Call And In Slices Of 1, 7 And 100 ms (By A Clock That Runs On The Room's Own Work): Every One Is The Same Room (The State Hash Is That Of An Independent Replay, The Seats Are Held And Dropped The Same, The Bot Sits Down, /busy Counts The Same); A Slice Ends Within 20 Turns' Work Of Its Time And Not Before It, The Time Between Slices Is Not The Room's Work (The Cap Of A Room Is On The Sum Of Its Own Slices, To The Millisecond), A Disagreeing Checkpoint Refuses The Room In The Slice That Meets It; What Is Not A Replay Under Way Is Refused; A Half-Built Room Gives Its Log Back; The Record May Go When The Replay Is Over") {
        PWorld w("restore-114");
        w.start_server(500);
        RoomSpec spec = held_spec("RS-0", 3);
        spec.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy});
        ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
        RClient& ann = w.connect("Ann", "RS-0");
        RClient& bob = w.connect("Bob", "RS-0");
        w.run(15000 + kPre);
        ASSERT_TRUE(w.status("RS-0").state == RoomState::Running && w.status("RS-0").bot_controller);
        const uint8_t seat_bob = bob.lobby->my_seat();
        bob.session->leave();                                                              // Bob quits: his Drop is in the record, his seat is gone for good
        w.run(1500);
        ASSERT_TRUE(w.status("RS-0").state == RoomState::Running && !w.status("RS-0").paused);
        ann.reconnects = false;
        bob.reconnects = false;
        w.stop_server(false);
        const RestartLoaded whole = w.read_record("RS-0");
        ASSERT_TRUE(whole.ok() && whole.turn_count > 300 && whole.checks.size() >= 15);
        const uint32_t n = whole.turn_count;
        const uint32_t last_boundary = (n / net::kHashEveryTurns) * net::kHashEveryTurns;      // the clock is looked at after turns 19, 39, ...: the last look is after this many turns
        const uint64_t independent = replay_hash(whole, n);                                // the state that an engine run through the turns by itself stands at
        const std::string maps = maps_dir();
        // a room as the manager makes one for a record: from its head, with the server's store and memory for logs
        RestartStore store(w.restart);
        std::string why;
        ASSERT_TRUE(store.prepare(why));                                                   // (the server that made the records is gone: the folder is free)
        net::LogBudget budget(256ull * 1024 * 1024);
        const auto make_room = [&](const RestartLoaded& rec, RestartStore& with_store) {
            MapEntry entry;
            std::string problem;
            if (!MapStore(maps).find(rec.head.map, entry, &problem)) throw std::runtime_error("map: " + problem);
            assets::LevelData level;
            if (!level.load_from_file(entry.path)) throw std::runtime_error("level");
            auto room = std::make_unique<Room>(room_spec_of(rec.head), std::move(entry), std::move(level), rec.head.start.seed, 1000u, &budget);
            room->set_restart_store(&with_store);
            return room;
        };
        const auto picture = [](Room& room, uint32_t now) {                                // everything that makes a restored room what it is
            const RoomStatus s = room.status(now);
            const RoomBusy b = room.busy(now);
            std::ostringstream out;
            out << room.code().substr(0, 2) << " " << room_state_name(s.state) << " hash " << s.restored_hash << " ticks " << s.ticks << " turns " << s.turns << " restored " << s.restored << "/" << s.restored_turns
                << " paused " << s.paused << " joined " << static_cast<int>(s.joined) << " bots " << s.bots.size() << "/" << s.bot_controller << " busy " << b.match << "/" << b.players << " absent";
            for (const RoomStatus::Absent& a : s.absent) out << " " << static_cast<int>(a.seat) << (a.catching_up ? "c" : "a");
            out << " vote " << static_cast<int>(s.vote_seat);
            return out.str();
        };
        // the clock of these tests runs on the room's own work: a millisecond for every turn that its replay has read (times `per_turn`), and on whatever time other rooms took between its slices
        Room* under_test = nullptr;
        uint32_t per_turn = 1;
        uint32_t foreign = 0;
        const std::function<uint32_t()> work_clock = [&]() { return (under_test != nullptr ? under_test->replay_progress() * per_turn : 0u) + foreign; };
        int next_code = 1;
        const auto copy_for_room = [&]() {                                                 // the same record under a code of its own: a room that begins writes its record again
            const std::string code = "RS-" + std::to_string(next_code++);
            copy_record_as(w, "RS-0", code, std::chrono::seconds(0));
            return code;
        };
        // ---- one call: the room that every slice size is compared with --------------------------------------------------------------------------------------------------------------------------------
        std::string reference;
        {
            const std::string code = copy_for_room();
            auto rec = std::make_unique<RestartLoaded>(read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming));
            ASSERT_TRUE(rec->ok() && rec->turn_count == n);
            auto room = make_room(*rec, store);
            Room::ReplayLimits limits;
            limits.clock = work_clock;
            under_test = room.get();
            ASSERT_TRUE(room->replay(*rec, 90000, limits, why) == Room::ReplayResult::Replayed);
            under_test = nullptr;
            rec.reset();                                                                   // (the record may go when the replay is over: the room needs no byte of it any more)
            ASSERT_TRUE(room->begin_restored(5000, why));
            ASSERT_EQ(room->status(5000).restored_hash, independent);                      // the replay is the match: an engine that was run through the turns by itself stands at the same state
            ASSERT_TRUE(room->status(5000).paused && room->status(5000).absent.size() == 1 && room->status(5000).bot_controller);        // (one person is held: Bob is gone for good, and the bot has sat down)
            for (const RoomStatus::Absent& a : room->status(5000).absent) ASSERT_TRUE(a.seat != seat_bob);
            reference = picture(*room, 5000);
            ASSERT_TRUE(reference.find("RS running") != std::string::npos && reference.find("restored 1/" + std::to_string(n)) != std::string::npos);
            room->update(5100);                                                            // (the room goes on from what the replay made)
            ASSERT_TRUE(room->state() == RoomState::Running);
        }
        // ---- the same record in slices: every one is the same room, a slice ends within 20 turns' work of its time and not before it -------------------------------------------------------------------
        for (const uint32_t slice : {1u, 7u, 100u, 20000u}) {
            const std::string code = copy_for_room();
            auto rec = std::make_unique<RestartLoaded>(read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming));
            auto room = make_room(*rec, store);
            under_test = room.get();
            foreign = 0;
            ASSERT_TRUE(room->begin_replay(*rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_EQ(room->replay_progress(), 0u);
            int steps = 0;
            uint32_t before = 0;
            Room::ReplayStep step = Room::ReplayStep::More;
            while (step == Room::ReplayStep::More) {
                step = room->replay_step(slice, work_clock, why);
                ++steps;
                ASSERT_MSG(step != Room::ReplayStep::Refused, why);
                const uint32_t done = room->replay_progress() - before;
                before = room->replay_progress();
                ASSERT_MSG(done <= slice + net::kHashEveryTurns - 1, "a slice ran more than 20 turns past its time (slice " + std::to_string(slice) + ", " + std::to_string(done) + " turns)");
                if (step == Room::ReplayStep::More) ASSERT_MSG(done >= slice, "a slice stopped before its time (slice " + std::to_string(slice) + ", " + std::to_string(done) + " turns)");
                foreign += 1000;                                                           // (other rooms' work between the slices)
                ASSERT_TRUE(steps < 100000);
            }
            under_test = nullptr;
            ASSERT_MSG(room->replay_progress() == n, "every turn was read");
            ASSERT_MSG(room->replay_work_ms() == n, "the room's work is the sum of its slices, not the time between them (slice " + std::to_string(slice) + ")");
            if (slice == 1) ASSERT_TRUE(steps >= static_cast<int>(n / net::kHashEveryTurns));         // (a slice of 1 ms is 20 turns: as many steps as looks at the clock)
            if (slice == 20000) ASSERT_EQ(steps, 1);                                                  // (a slice longer than the replay is the replay in one go)
            rec.reset();
            ASSERT_TRUE(room->begin_restored(5000, why));
            ASSERT_EQ(room->status(5000).restore_ms, n);                                  // (the status says how long the room's own work was)
            ASSERT_MSG(picture(*room, 5000) == reference, picture(*room, 5000) + " != " + reference);
            room->update(5100);
            ASSERT_TRUE(room->state() == RoomState::Running);
        }
        // ---- a clock that stands still: a slice of 5 ms is never used up, the replay is done in one step -----------------------------------------------------------------------------------------------
        {
            const std::string code = copy_for_room();
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            auto room = make_room(rec, store);
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->replay_step(5, []() { return 77u; }, why) == Room::ReplayStep::Replayed);
            ASSERT_EQ(room->replay_work_ms(), 0u);
        }
        // ---- the cap is on the sum of the room's own slices, to the millisecond (RestartConfig::replay_budget_ms; 10 ms for every turn here) -----------------------------------------------------
        for (const uint32_t slice : {1u, 100u, 5000u}) {
            const uint32_t total = last_boundary * 10;                                     // what the clock reads at the last look
            ASSERT_TRUE(total > 3000);
            for (const bool refuse : {false, true}) {
                RestartConfig capped = w.restart;
                capped.replay_budget_ms = refuse ? total - 1 : total;                      // (the cap is passed when the sum is MORE than it)
                RestartStore capped_store(capped);                                         // (not prepared: the first store holds the folder's lock, and this one only carries the cap)
                const std::string code = copy_for_room();
                RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
                auto room = make_room(rec, capped_store);
                under_test = room.get();
                per_turn = 10;
                foreign = 0;
                ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
                Room::ReplayStep step = Room::ReplayStep::More;
                int steps = 0;
                while (step == Room::ReplayStep::More && steps < 100000) {
                    step = room->replay_step(slice, work_clock, why);
                    foreign += 100000;                                                     // (an hour of other rooms' work in all: not the room's)
                    ++steps;
                }
                under_test = nullptr;
                per_turn = 1;
                ASSERT_MSG(step == (refuse ? Room::ReplayStep::Refused : Room::ReplayStep::Replayed), std::string("slice ") + std::to_string(slice) + (refuse ? " refused" : " not refused") + ": " + why);
                if (refuse) {
                    ASSERT_TRUE(why.find("longer than the " + std::to_string((total - 1) / 1000) + " s that a restore may") != std::string::npos);
                    ASSERT_MSG(room->replay_progress() == last_boundary, "the room is refused at the look that passes the cap, not later");
                }
            }
        }
        // ---- a checkpoint that the replay disagrees with refuses the room in the slice that meets it: the third checkpoint, turn 59, in the third slice of 1 ms -----------------------------------
        {
            const std::string code = copy_for_room();
            std::vector<uint8_t> bytes = read_all_bytes(w.record_path(code));
            const auto frames = frames_of(bytes);
            size_t checks_seen = 0;
            size_t tampered = 0;
            for (size_t i = 1; i < frames.size() && tampered == 0; ++i) {
                if (bytes[frames[i].first] == 3 && ++checks_seen == 3) tampered = i;
            }
            ASSERT_TRUE(tampered != 0);
            std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].first + 5), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].second - 4));
            payload[8] = static_cast<uint8_t>(payload[8] ^ 1);
            const std::vector<uint8_t> frame = test_frame(3, payload);
            std::vector<uint8_t> out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].first));
            out.insert(out.end(), frame.begin(), frame.end());
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(frames[tampered].second), bytes.end());
            write_all_bytes(w.record_path(code), out);
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            ASSERT_TRUE(rec.ok());
            auto room = make_room(rec, store);
            under_test = room.get();
            foreign = 0;
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More && room->replay_progress() == 20);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More && room->replay_progress() == 40);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused);
            ASSERT_TRUE(why.find("does not agree with the state hash") != std::string::npos && why.find("turn 59") != std::string::npos);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused && why.find("no replay is under way") != std::string::npos);     // (a refused replay is over)
            under_test = nullptr;
            ASSERT_FALSE(room->begin_restored(5000, why));                                  // the room has not been replayed
        }
        // ---- what is not a replay under way is refused; a room that is thrown away half built gives its log back ----------------------------------------------------------------------------
        {
            const std::string code = copy_for_room();
            RestartLoaded rec = read_restart_record(w.record_path(code), 1ull << 30, RestartRead::Streaming);
            auto room = make_room(rec, store);
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::Refused && why.find("no replay is under way") != std::string::npos);        // (nothing was begun)
            RestartLoaded wrong = rec;
            wrong.head.code = "OTHER-ROOM";
            ASSERT_TRUE(room->begin_replay(wrong, 90000, why) == Room::ReplayBegin::Refused && why.find("not one that a record can be restored into") != std::string::npos);      // (another room's record)
            RestartLoaded refused_record = read_restart_record(w.record_path("RS-0"), 1ull << 30);
            refused_record.status = RestartLoaded::Status::Corrupt;
            ASSERT_TRUE(room->begin_replay(refused_record, 90000, why) == Room::ReplayBegin::Refused);                                                                // (a record that was not judged good)
            const uint64_t used_before = budget.used();
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Ready);
            ASSERT_TRUE(room->begin_replay(rec, 90000, why) == Room::ReplayBegin::Refused);                                                                           // (once)
            under_test = room.get();
            ASSERT_TRUE(room->replay_step(1, work_clock, why) == Room::ReplayStep::More);
            under_test = nullptr;
            ASSERT_TRUE(budget.used() > used_before);                                       // the turns of the half-replayed match are in the room's log
            room.reset();                                                                   // thrown away in the middle
            ASSERT_EQ(budget.used(), used_before);                                          // ... and the memory is the server's again
        }
    } TEST_END();
}


int main() {
    std::cout << "=======================================================\n";
    std::cout << " Dedicated game server: map store, rooms, the door, control calls\n";
    std::cout << "=======================================================\n";
    run_store_tests();
    run_manager_tests();
    run_demo_tests();
    run_hardening_tests();
    run_map_tests();
    run_match_tests();
    run_leader_tests();
    run_control_tests();
    run_socket_tests();
    run_secret_tests();
    run_reconnect_tests();
    run_bot_tests();
    run_persist_tests();
    run_persist_server_tests();
    run_persist_server_tests_2();
    run_persist_server_tests_3();
    run_persist_server_tests_4();
    run_persist_server_tests_5();
    run_persist_server_tests_6();
    run_persist_review_tests();
    run_restore_tests();
#if !defined(_WIN32) && defined(ANTS_SERVER_BINARY)
    run_persist_process_tests();
    run_persist_review_process_tests();
    run_persist_review_process_tests_2();
#endif
    std::cout << "=======================================================\n";
    std::cout << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n";
    std::cout << "=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
