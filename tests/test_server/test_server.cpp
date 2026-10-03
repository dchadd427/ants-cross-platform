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
#include <process.h>
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <cstring>

using namespace ants;
using namespace ants::server;
namespace fs = std::filesystem;

// The first turn of a match is sealed this long after the match began (protocol 12: the "Get ready to play!" dialog of every machine, in which no simulation runs): a test that wants play to
// be under way waits this much longer than it did before
constexpr uint32_t kPre = net::kMatchStartDelayMs;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

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

struct RWorld;

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

    void start(net::Connection* client_end, uint32_t seed) {
        end = client_end;
        rng = seed;
        net::ClientLobby::Config cc;
        cc.name = name;
        cc.room = room;
        cc.want_seat = want_seat;
        cc.key = key;
        lobby = std::make_unique<net::ClientLobby>(end, cc);
    }
    uint32_t next_random() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    void update(uint32_t now_ms, const std::string& maps, RWorld& w);
};

struct RWorld {
    net::LoopbackNetwork net{5};
    RoomManager mgr;
    std::vector<std::unique_ptr<RClient>> clients;
    uint32_t now{1000};
    net::LoopbackNetwork::Link link{20, 10};

    explicit RWorld(ServerLimits limits = ServerLimits()) : mgr(MapStore(maps_dir()), limits) {}

    // a new link to the server: the door's end is the manager's, this end is the caller's
    net::Connection* open_link() {
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

void RClient::update(uint32_t now_ms, const std::string& maps, RWorld& w) {
    if (hung) return;
    if (lobby && !session) {
        lobby->update(now_ms);
        for (net::ChatLine& line : lobby->take_chat()) room_chat.push_back(std::move(line));
        for (const net::ClientLobby::Event& ev : lobby->take_events()) {
            if (ev.type == net::ClientLobby::Event::Type::StartRequested) {
                const net::StartMsg& s = lobby->start_info();
                assets::LevelData level;
                uint64_t hash = 0;
                const bool ok = level.load_from_file(maps + "/" + s.map_name) && net::hash_file(maps + "/" + s.map_name, hash) && hash == s.map_hash;
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
        ASSERT_TRUE(r.status == 200 && json_of(r).get("rooms").size() == 3);
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

    TEST_CASE("S3.71 Server CPU With Bots (Measured): Twelve Rooms Of One Person And Three Bots Each Cost The Server's Thread A Few Milliseconds A Second (Only mgr.update Is Timed: The Clients' Work Is Not The Server's), On TINY And On TREASURE, With Idle, Medium And Hard Bots; The Start Of Twelve Rooms Is Not A Stall (Each Room Analyses Its Map Once)") {
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
                const auto t0 = std::chrono::steady_clock::now();
                w.mgr.update(w.now);
                timed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
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

    TEST_CASE("S3.74 The Opening Of A Server's Room: The First Turn Is Sealed 5000 ms After The Match Began (Protocol 12: Nobody's Clock Runs Behind The Dialog, Nothing Is Sealed, Run Or Late Before It); A Room That The Leader's START Fills With Hard Bots Has No Command Of A Bot Seat In Turn 0 And Before A Bot's First Look (Tick 1 + Seat) And Its Reaction Time Are Over, And The Bots Do Play After It") {
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
    std::cout << "=======================================================\n";
    std::cout << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n";
    std::cout << "=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
