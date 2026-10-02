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
#include <memory>
#include <random>
#include <set>
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

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
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
        for (const uint16_t old_version : {uint16_t{4}, uint16_t{8}}) {
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
            w.run(20000);
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
            w.run(3000);
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
            w.run(3000);
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
        w.run(4000);
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
            w.run(4000);
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
        w.run(3000);
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
        w.run(5000);
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
    std::cout << "=======================================================\n";
    std::cout << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n";
    std::cout << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
