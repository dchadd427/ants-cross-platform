// Tests of the framed non-blocking TCP transport on the loopback interface: message boundaries and order, size limits, hostile frames, orderly
// and abrupt closes, a peer that never reads, and a whole lock-step match of a host and three clients over real sockets.
#include "ants_net/lobby.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_net/tcp.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::net;
using ants::sim::AntType;
using ants::sim::Command;
using ants::sim::CommandType;
using ants::sim::TileCoord;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

// A connected pair over the loopback interface: (client side, server side), both Open
struct Pair {
    std::unique_ptr<TcpListener> listener;
    std::unique_ptr<TcpConnection> client;
    std::unique_ptr<TcpConnection> server;
};

bool make_pair(Pair& p) {
    p.listener = TcpListener::listen(0, true);
    if (!p.listener) return false;
    p.client = TcpConnection::connect("127.0.0.1", p.listener->port());
    if (!p.client) return false;
    for (int i = 0; i < 2000 && (!p.server || !p.client->is_open()); ++i) {
        if (!p.server) p.server = p.listener->accept();
        std::vector<uint8_t> dummy;
        p.client->poll(dummy);
        if (p.server) p.server->poll(dummy);
        if (!p.server || !p.client->is_open()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p.server && p.client->is_open() && p.server->is_open();
}

// Polls `conn` until a message arrives or ~2 s pass
bool receive(Connection& conn, std::vector<uint8_t>& out, int max_ms = 2000) {
    for (int i = 0; i < max_ms; ++i) {
        if (conn.poll(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::vector<uint8_t> blob(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 31u);
    return v;
}

#ifndef _WIN32
// A peer that is no game: a raw socket and a thread that writes frames as fast as the kernel takes them. Frame number n is a 4-byte message (its number), except that
// every 100000th is a message of kMaxMessageBytes (its number, then a pattern); `hostile_after` frames, when not 0, end with a length prefix that no frame may have;
// `payload`, when not 0, makes every frame that long (its number, then a pattern).
struct RawFlooder {
    int sock{-1};
    std::thread thread;
    std::atomic<bool> stop{false};

    RawFlooder(uint16_t port, uint32_t frames, uint32_t hostile_after = 0, uint32_t payload = 0) {
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
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));       // (a write to a connection that the receiver closed must be an error here, not a signal)
#endif
        thread = std::thread([this, frames, hostile_after, payload]() {
            const auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
                for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFFu));
            };
            std::vector<uint8_t> chunk;
            uint32_t seq = 0;
            while (!stop && (seq < frames || (hostile_after != 0 && seq == frames))) {
                chunk.clear();
                for (int i = 0; i < 20000 && (seq < frames || (hostile_after != 0 && seq == frames)); ++i, ++seq) {
                    if (seq == frames) {                                   // the hostile prefix: 4 GB
                        put32(chunk, 0xFFFFFFFFu);
                        put32(chunk, 0);
                        continue;
                    }
                    const bool big = payload != 0 || (hostile_after == 0 && seq % 100000u == 99999u);
                    const uint32_t len = payload != 0 ? payload : (big ? static_cast<uint32_t>(kMaxMessageBytes) : 4u);
                    put32(chunk, len);
                    put32(chunk, seq);
                    if (big) {
                        for (size_t k = 4; k < len; ++k) chunk.push_back(static_cast<uint8_t>(seq + k * 31u));
                    }
                }
                size_t off = 0;
                while (off < chunk.size() && !stop) {
#ifdef MSG_NOSIGNAL
                    const int flags = MSG_NOSIGNAL;
#else
                    const int flags = 0;
#endif
                    const auto n = ::send(sock, chunk.data() + off, chunk.size() - off, flags);
                    if (n <= 0) return;                                    // the receiver went away
                    off += static_cast<size_t>(n);
                }
            }
        });
    }
    ~RawFlooder() {
        stop = true;
        if (sock >= 0) ::shutdown(sock, SHUT_RDWR);                         // (a send that is blocked on a receiver that does not read ends)
        if (thread.joinable()) thread.join();
        if (sock >= 0) ::close(sock);
    }
};
#endif

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

Command cmd(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.other_player = other;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

struct Ids {
    std::vector<uint32_t> ants[sim::MAX_PLAYERS];
};

Ids build_world(sim::SimulationEngine& sim, uint32_t seed) {
    Ids ids;
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[sim::MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    sim.grid_mut().drop_lunchbox(20, 20, 25);
    const AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        for (int i = 0; i < 6; ++i) ids.ants[p].push_back(sim.spawn_unit(p, types[i], TileCoord{hills[p].x + 1 + i, hills[p].y + 6}));
    }
    return ids;
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: framed TCP transport (loopback)\n"
                 "=======================================================\n";

    TEST_CASE("N3.1 TCP: Messages Of Every Size Keep Their Boundaries And Their Order In Both Directions") {
        Pair p;
        ASSERT_TRUE(make_pair(p));
        std::vector<uint8_t> got;
        for (size_t n : {size_t{0}, size_t{1}, size_t{2}, size_t{100}, size_t{4095}, size_t{4096}, size_t{4097}, size_t{16384}, size_t{60000}, kMaxMessageBytes}) {
            const std::vector<uint8_t> m = blob(n, static_cast<uint8_t>(n));
            ASSERT_TRUE(p.client->send(m));
            ASSERT_TRUE(receive(*p.server, got));
            ASSERT_TRUE(got == m);
            ASSERT_TRUE(p.server->send(m));
            ASSERT_TRUE(receive(*p.client, got));
            ASSERT_TRUE(got == m);
        }
        ASSERT_FALSE(p.client->send(blob(kMaxMessageBytes + 1, 1)));                  // over the limit: refused, connection intact
        ASSERT_TRUE(p.client->is_open());
        // 20000 small messages, in bursts of 500 that the receiver drains, arrive whole and in order
        Lcg rng(3);
        for (int burst = 0; burst < 40; ++burst) {
            std::vector<std::vector<uint8_t>> sent;
            for (int i = 0; i < 500; ++i) {
                sent.push_back(blob(1 + rng.below(200), static_cast<uint8_t>(burst * 500 + i)));
                ASSERT_TRUE(p.client->send(sent.back()));
            }
            for (size_t i = 0; i < sent.size(); ++i) {
                ASSERT_TRUE(receive(*p.server, got));
                ASSERT_TRUE(got == sent[i]);
            }
        }
    } TEST_END();

    TEST_CASE("N3.2 TCP: An Orderly Close Is Seen As Closed, A Truncated Frame Is Never Delivered, Connecting To A Dead Port Fails") {
        {
            Pair p;
            ASSERT_TRUE(make_pair(p));
            ASSERT_TRUE(p.client->send({1, 2, 3}));
            p.client->close();
            std::vector<uint8_t> got;
            ASSERT_TRUE(receive(*p.server, got));                                        // what was sent before the close still arrives
            ASSERT_EQ(got.size(), 3u);
            for (int i = 0; i < 500 && p.server->is_open(); ++i) {
                p.server->poll(got);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_EQ(p.server->state(), Connection::State::Closed);
            ASSERT_FALSE(p.server->send({1}));
        }
        {
            // a port that nothing listens on
            uint16_t dead = 0;
            {
                auto l = TcpListener::listen(0, true);
                ASSERT_TRUE(l != nullptr);
                dead = l->port();
            }
            auto c = TcpConnection::connect("127.0.0.1", dead);
            ASSERT_TRUE(c != nullptr);
            std::vector<uint8_t> dummy;
            for (int i = 0; i < 2000 && c->state() == Connection::State::Connecting; ++i) {
                c->poll(dummy);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_EQ(c->state(), Connection::State::Failed);
        }
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("N3.3 TCP: A Hostile Length Prefix Fails The Connection Without Any Allocation; A Half Frame Then A Close Delivers Nothing") {
        auto listener = TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        auto raw_connect = [&]() {
            const int s = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a;
            std::memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_port = htons(listener->port());
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            return s;
        };
        auto accepted = [&]() {
            std::unique_ptr<TcpConnection> c;
            for (int i = 0; i < 2000 && !c; ++i) {
                c = listener->accept();
                if (!c) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return c;
        };
        // 4 GB frame announced
        {
            const int s = raw_connect();
            auto srv = accepted();
            ASSERT_TRUE(srv != nullptr);
            const uint8_t bad[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            ASSERT_TRUE(::send(s, bad, 4, 0) == 4);
            std::vector<uint8_t> got;
            for (int i = 0; i < 500 && srv->state() == Connection::State::Open; ++i) {
                srv->poll(got);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_EQ(srv->state(), Connection::State::Failed);
            ASSERT_TRUE(got.empty());
            ::close(s);
        }
        // one byte over the limit
        {
            const int s = raw_connect();
            auto srv = accepted();
            ASSERT_TRUE(srv != nullptr);
            const uint32_t len = static_cast<uint32_t>(kMaxMessageBytes + 1);
            const uint8_t hdr[4] = {static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>((len >> 8) & 0xFF), static_cast<uint8_t>((len >> 16) & 0xFF),
                                    static_cast<uint8_t>((len >> 24) & 0xFF)};
            ASSERT_TRUE(::send(s, hdr, 4, 0) == 4);
            std::vector<uint8_t> got;
            for (int i = 0; i < 500 && srv->state() == Connection::State::Open; ++i) {
                srv->poll(got);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_EQ(srv->state(), Connection::State::Failed);
            ::close(s);
        }
        // a frame of 100 bytes of which 10 arrive, then the sender closes
        {
            const int s = raw_connect();
            auto srv = accepted();
            ASSERT_TRUE(srv != nullptr);
            uint8_t frame[14] = {100, 0, 0, 0};
            ASSERT_TRUE(::send(s, frame, 14, 0) == 14);
            ::close(s);
            std::vector<uint8_t> got;
            for (int i = 0; i < 500 && srv->is_open(); ++i) {
                srv->poll(got);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(got.empty());
            ASSERT_EQ(srv->state(), Connection::State::Closed);
        }
    } TEST_END();
#endif

    TEST_CASE("N3.4 TCP: A Peer That Never Reads Cannot Make The Sender's Queue Grow Without Bound") {
        Pair p;
        ASSERT_TRUE(make_pair(p));
        const std::vector<uint8_t> big = blob(kMaxMessageBytes, 7);
        size_t sent = 0;
        bool refused = false;
        for (size_t i = 0; i < 4000 && !refused; ++i) {                                // up to 250 MB offered; the receiver polls never
            if (!p.client->send(big)) refused = true;
            else ++sent;
        }
        ASSERT_TRUE(refused);
        ASSERT_TRUE(sent < 3000);
        ASSERT_TRUE(p.client->backlog() <= 9 * kMaxMessageBytes + 4);
        ASSERT_EQ(p.client->state(), Connection::State::Failed);
    } TEST_END();

    TEST_CASE("N3.5 TCP: A Host And Three Clients Play 40 Seconds Over Real Sockets And Stay Bit-Identical") {
        auto listener = TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        std::vector<std::unique_ptr<TcpConnection>> client_ends;
        std::vector<std::unique_ptr<TcpConnection>> host_ends;
        for (int i = 0; i < 3; ++i) {
            client_ends.push_back(TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(client_ends.back() != nullptr);
        }
        for (int i = 0; i < 3000 && host_ends.size() < 3; ++i) {
            if (auto c = listener->accept()) host_ends.push_back(std::move(c));
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(host_ends.size(), 3u);
        std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
        Ids ids;
        for (int i = 0; i < 4; ++i) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            ids = build_world(*sims.back(), 5);
        }
        HostSession host(*sims[0], HostSession::Config{});
        std::vector<std::unique_ptr<ClientSession>> clients;
        for (uint8_t p = 1; p < 4; ++p) {
            // the k-th accepted connection is the k-th client's (they connected in order and the kernel queues them in order)
            host.add_client(p, host_ends[p - 1].get());
            ClientSession::Config cc;
            cc.player = p;
            clients.push_back(std::make_unique<ClientSession>(*sims[p], cc));
            clients.back()->set_connection(client_ends[p - 1].get());
        }
        // wait until the client sockets are Open
        std::vector<uint8_t> dummy;
        for (int i = 0; i < 2000; ++i) {
            bool all = true;
            for (auto& c : client_ends) {
                c->poll(dummy);
                all = all && c->is_open();
            }
            if (all) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        host.start(0);
        for (auto& c : clients) c->start(0);
        uint32_t now = 0;
        Lcg rng(9);
        for (; now < 40000; now += 10) {
            if (now % 300 == 0) {
                for (uint8_t p = 0; p < 4; ++p) {
                    std::vector<uint32_t> pick = {ids.ants[p][rng.below(6)], ids.ants[p][rng.below(6)]};
                    const Command c = cmd(rng.below(2) ? CommandType::GroupMove : CommandType::GroupAttack, p, 255, static_cast<int16_t>(rng.below(60)),
                                          static_cast<int16_t>(rng.below(60)), pick);
                    if (p == 0) host.submit_local(c);
                    else clients[p - 1]->submit(c);
                }
            }
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        host.freeze();
        for (uint32_t end = now + 3000; now < end; now += 10) {
            host.update(now);
            for (auto& cl : clients) cl->update(now);
        }
        ASSERT_TRUE(host.desyncs().empty());
        ASSERT_TRUE(host.turns_sealed() > 760);                              // (turns of 50 ms: the 380 of 100 ms that this stood for, twice)
        for (int i = 1; i < 4; ++i) {
            ASSERT_TRUE(sims[static_cast<size_t>(i)]->current_tick() == sims[0]->current_tick());
            ASSERT_TRUE(sims[static_cast<size_t>(i)]->state_hash() == sims[0]->state_hash());
        }
    } TEST_END();

    TEST_CASE("N3.6 TCP: The Room Works Over Real Sockets: Two Guests Join, The Host Starts, Everybody Loads And The Match Begins") {
        auto listener = TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        HostLobby::Config hc;
        hc.host_name = "Queen";
        HostLobby host(hc);
        host.set_map("SMALL.LVL");
        std::vector<std::unique_ptr<TcpConnection>> host_side;       // accepted connections (owned here)
        std::vector<std::unique_ptr<TcpConnection>> client_side;
        std::vector<std::unique_ptr<ClientLobby>> lobbies;
        for (const char* name : {"Bob", "Carl"}) {
            client_side.push_back(TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(client_side.back() != nullptr);
            ClientLobby::Config cc;
            cc.name = name;
            lobbies.push_back(std::make_unique<ClientLobby>(client_side.back().get(), cc));
        }
        uint32_t now = 0;
        auto step = [&]() {
            now += 10;
            if (auto c = listener->accept()) {
                host.add_connection(c.get(), now);
                host_side.push_back(std::move(c));
            }
            host.update(now);
            for (auto& l : lobbies) l->update(now);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        for (int i = 0; i < 3000 && host.players() < 3; ++i) step();
        ASSERT_EQ(host.players(), 3u);
        for (int i = 0; i < 200; ++i) step();
        for (auto& l : lobbies) {
            ASSERT_EQ(l->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(l->room().map_name, "SMALL.LVL");
            ASSERT_EQ(l->room().slots[0].name, "Queen");
        }
        ASSERT_TRUE(host.start(99, 0xFEEDull, now));
        for (int i = 0; i < 500; ++i) {
            step();
            bool all = true;
            for (auto& l : lobbies) all = all && l->phase() == ClientLobby::Phase::Loading;
            if (all) break;
        }
        for (auto& l : lobbies) {
            ASSERT_EQ(l->phase(), ClientLobby::Phase::Loading);
            ASSERT_EQ(l->start_info().seed, 99u);
            ASSERT_EQ(l->start_info().map_hash, 0xFEEDull);
            l->report_loaded(true);
        }
        host.host_loaded(true);
        for (int i = 0; i < 500 && host.phase() != HostLobby::Phase::Begun; ++i) step();
        ASSERT_EQ(host.phase(), HostLobby::Phase::Begun);
        for (int i = 0; i < 300; ++i) step();
        for (auto& l : lobbies) ASSERT_EQ(l->phase(), ClientLobby::Phase::Begun);
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("N3.7 TCP: A Flood Of Tiny Messages Is Held Back By TCP Instead Of Piling Up In The Receiver: The Inbox Never Holds More Than 4096 Messages However Little The Game Takes, Nothing Is Lost Or Reordered, A 64 KB Message In The Middle Arrives Whole, And A Hostile Length Behind The Flood Still Fails The Connection") {
        ASSERT_EQ(TcpConnection::kMaxInboxMessages, size_t{4096});
        ASSERT_EQ(TcpConnection::kMaxInboxBytes, size_t{1024 * 1024});
        {
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            const uint32_t kFrames = 1000000;
            RawFlooder flood(listener->port(), kFrames);
            ASSERT_TRUE(flood.sock >= 0);
            std::unique_ptr<TcpConnection> srv;
            for (int i = 0; i < 2000 && !srv; ++i) {
                srv = listener->accept();
                if (!srv) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(srv != nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));       // the sender has filled every buffer on the way: a receiver that polled at once would see a trickle
            uint32_t expected = 0;
            size_t peak = 0;
            size_t polls = 0;
            bool in_order = true;
            bool bigs_whole = true;
            int idle = 0;
            std::vector<uint8_t> got;
            while (expected < kFrames && idle < 5000) {
                int taken = 0;
                for (int i = 0; i < 64 && srv->poll(got); ++i) {              // one update of a host: it takes at most 64 messages of a connection
                    ++taken;
                    ++polls;
                    uint32_t n = 0;
                    if (got.size() >= 4) n = static_cast<uint32_t>(got[0]) | (static_cast<uint32_t>(got[1]) << 8) | (static_cast<uint32_t>(got[2]) << 16) | (static_cast<uint32_t>(got[3]) << 24);
                    if (n != expected) in_order = false;
                    const bool big = expected % 100000u == 99999u;
                    if (got.size() != (big ? kMaxMessageBytes : size_t{4})) in_order = false;
                    if (big && got.size() == kMaxMessageBytes) {
                        for (size_t k = 4; k < got.size(); ++k) {
                            if (got[k] != static_cast<uint8_t>(expected + k * 31u)) bigs_whole = false;
                        }
                    }
                    ++expected;
                }
                peak = std::max(peak, srv->inbox());
                if (taken == 0) {
                    ++idle;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    idle = 0;
                }
            }
            ASSERT_EQ(expected, kFrames);                                       // not one message lost
            ASSERT_TRUE(in_order);
            ASSERT_TRUE(bigs_whole);
            ASSERT_TRUE(peak <= 4096);                                          // (it used to be 30000 after the first poll, and growing)
            ASSERT_TRUE(peak >= 2048);                                          // the flood did queue up: the test measures something
            ASSERT_EQ(polls, size_t{kFrames});
            ASSERT_TRUE(srv->is_open());
        }
        {   // a hostile length behind a flood: the frames before it are all delivered, then the connection fails (the check is made when the parser gets there)
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            RawFlooder flood(listener->port(), 10000, 10000);                  // 10000 good frames, then 0xFFFFFFFF
            ASSERT_TRUE(flood.sock >= 0);
            std::unique_ptr<TcpConnection> srv;
            for (int i = 0; i < 2000 && !srv; ++i) {
                srv = listener->accept();
                if (!srv) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(srv != nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            uint32_t expected = 0;
            std::vector<uint8_t> got;
            for (int guard = 0; guard < 4000 && srv->state() != Connection::State::Failed; ++guard) {
                for (int i = 0; i < 64 && srv->poll(got); ++i) {
                    ASSERT_TRUE(got.size() == 4 && got[0] == static_cast<uint8_t>(expected & 0xFFu));
                    ++expected;
                }
                if (srv->state() != Connection::State::Failed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            while (srv->poll(got)) ++expected;                                  // what is left in the inbox
            ASSERT_EQ(srv->state(), Connection::State::Failed);
            ASSERT_EQ(expected, 10000u);
        }
        {   // a flood of BIG messages (60,000 bytes each, 6 MB): the bytes bound the inbox before the count does (about 1 MiB: 17 messages), and what was read and not parsed is
            // bounded too (a whole message and one read), however often the game polls
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            RawFlooder flood(listener->port(), 100, 0, 60000);
            ASSERT_TRUE(flood.sock >= 0);
            std::unique_ptr<TcpConnection> srv;
            for (int i = 0; i < 2000 && !srv; ++i) {
                srv = listener->accept();
                if (!srv) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(srv != nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));         // the kernel holds as much of the 6 MB as it can
            std::vector<uint8_t> got;
            size_t peak_messages = 0;
            size_t peak_buffered = 0;
            for (int i = 0; i < 40; ++i) {                                          // forty polls that take nothing (the game is busy), then the game takes everything
                srv->poll(got);                                                     // (a poll takes one message: put it back in the count)
                peak_messages = std::max(peak_messages, srv->inbox() + 1);
                peak_buffered = std::max(peak_buffered, srv->buffered());
            }
            ASSERT_TRUE(peak_messages <= 18);                                       // 1 MiB of 60,000-byte messages: 17, and the one just taken
            ASSERT_TRUE(peak_buffered <= kMaxMessageBytes + 4 + 16384);
            uint32_t n = 40;                                                        // (forty were taken above)
            for (int guard = 0; guard < 20000 && n < 100; ++guard) {
                if (srv->poll(got)) {
                    ASSERT_EQ(got.size(), size_t{60000});
                    ++n;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                peak_messages = std::max(peak_messages, srv->inbox() + 1);
                peak_buffered = std::max(peak_buffered, srv->buffered());
            }
            ASSERT_EQ(n, 100u);                                                     // all 100 arrived, whole
            ASSERT_TRUE(peak_messages <= 18 && peak_buffered <= kMaxMessageBytes + 4 + 16384);
        }
        {   // a server that drops a flooder closes the connection and keeps the object: what the flooder had sent and nobody read is let go of at once, not held until the room ends
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            RawFlooder flood(listener->port(), 1000000);
            ASSERT_TRUE(flood.sock >= 0);
            std::unique_ptr<TcpConnection> srv;
            for (int i = 0; i < 2000 && !srv; ++i) {
                srv = listener->accept();
                if (!srv) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(srv != nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::vector<uint8_t> got;
            ASSERT_TRUE(srv->poll(got));                                        // one poll: the inbox fills up to its bound
            ASSERT_TRUE(srv->inbox() >= 2048);
            srv->close();
            ASSERT_EQ(srv->inbox(), size_t{0});
            ASSERT_FALSE(srv->poll(got));
            ASSERT_EQ(srv->state(), Connection::State::Closed);
        }
        {   // what a PEER sent before it closed is still delivered (a close that was seen, not made): N3.2, here with a few thousand messages
            Pair p;
            ASSERT_TRUE(make_pair(p));
            for (uint32_t i = 0; i < 3000; ++i) ASSERT_TRUE(p.client->send({static_cast<uint8_t>(i & 0xFF), static_cast<uint8_t>((i >> 8) & 0xFF)}));
            for (int i = 0; i < 50 && p.client->backlog() > 0; ++i) {
                std::vector<uint8_t> dummy;
                p.client->poll(dummy);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            p.client->close();
            uint32_t n = 0;
            std::vector<uint8_t> got;
            for (int guard = 0; guard < 20000 && n < 3000; ++guard) {                 // (a pass that takes a message is not a pass that waits)
                if (p.server->poll(got)) {
                    ASSERT_TRUE(got.size() == 2 && got[0] == static_cast<uint8_t>(n & 0xFF) && got[1] == static_cast<uint8_t>((n >> 8) & 0xFF));
                    ++n;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            ASSERT_EQ(n, 3000u);
        }
    } TEST_END();
#endif

    TEST_CASE("N3.8 TCP: A Burst Of 48 Connections That Come While The Server Is Not Looking Are All Waiting For It When It Looks (The Listener's Backlog Was 8: On macOS 8 Of Them Were Accepted And The Rest Never Came)") {
        auto listener = TcpListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        constexpr size_t kBurst = 48;
        std::vector<std::unique_ptr<TcpConnection>> clients;
        for (size_t i = 0; i < kBurst; ++i) {
            clients.push_back(TcpConnection::connect("127.0.0.1", listener->port()));
            ASSERT_TRUE(clients.back() != nullptr);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));              // a slow pass of the server's loop: the operating system finishes the handshakes, nobody accepts
        std::vector<std::unique_ptr<TcpConnection>> servers;
        while (servers.size() < kBurst + 16) {
            std::unique_ptr<TcpConnection> c = listener->accept();
            if (!c) break;
            servers.push_back(std::move(c));
        }
        ASSERT_EQ(servers.size(), kBurst);                                         // all of them were waiting (with a backlog of 8 eight were, on macOS, and the rest never came)
        // and each one is a connection that works: a message of every client reaches the server, whichever connection it was accepted as
        for (size_t i = 0; i < kBurst; ++i) ASSERT_TRUE(clients[i]->send({static_cast<uint8_t>(i)}));
        std::set<uint8_t> got;
        for (int round = 0; round < 3000 && got.size() < kBurst; ++round) {
            std::vector<uint8_t> dummy;
            for (auto& c : clients) c->poll(dummy);
            for (auto& srv : servers) {
                std::vector<uint8_t> m;
                if (srv->poll(m) && m.size() == 1) got.insert(m[0]);
            }
            if (got.size() < kBurst) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(got.size(), kBurst);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
