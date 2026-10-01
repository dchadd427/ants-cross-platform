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
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

// A player: a client lobby that asks for a room, loads the match, reports, and plays with a session of its own. It behaves like the application does.
struct Client {
    std::string name;
    std::string room;
    uint8_t want_seat{255};
    net::Connection* end{nullptr};
    std::unique_ptr<net::ClientLobby> lobby;
    sim::SimulationEngine sim;
    std::unique_ptr<net::ClientSession> session;
    bool fail_load{false};
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
                    map_w = level.width;
                    map_h = level.height;
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
        if (session) {
            session->update(now_ms);
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

    explicit World(ServerLimits limits = ServerLimits()) : mgr(MapStore(maps_dir()), limits) {}

    Client& connect(const std::string& name, const std::string& room, uint8_t seat = 255, net::LoopbackNetwork::Link link = {20, 10}) {
        auto ends = net.connect(link);
        mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", now);
        clients.push_back(std::make_unique<Client>());
        Client& c = *clients.back();
        c.name = name;
        c.room = room;
        c.want_seat = seat;
        c.start(ends.second, static_cast<uint32_t>(clients.size()) * 7919u);
        return c;
    }
    void run(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
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

std::string temp_dir_for(const char* tag) {
    const fs::path p = fs::temp_directory_path() / (std::string("ants_server_test_") + tag);
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
        // an old client: its layout is not read, the answer is "version mismatch"
        {
            auto ends = w.net.connect({10, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "x", w.now);
            net::HelloMsg old;
            old.version = 4;
            old.name = "Old";
            old.room = "AAA-1";
            ends.second->send(net::encode(old));
            w.run(200);
            std::vector<uint8_t> reply;
            net::RejectMsg rj;
            ASSERT_TRUE(ends.second->poll(reply) && net::decode(reply, rj) && rj.reason == net::RejectReason::VersionMismatch);
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
        ASSERT_TRUE(s.ticks > 1000 && s.turns > 500);
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
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " Dedicated game server: map store, rooms, the door, control calls\n";
    std::cout << "=======================================================\n";
    run_store_tests();
    run_manager_tests();
    run_match_tests();
    run_control_tests();
    run_socket_tests();
    std::cout << "=======================================================\n";
    std::cout << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Failed:           " << g_test_failures << "\n";
    std::cout << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
