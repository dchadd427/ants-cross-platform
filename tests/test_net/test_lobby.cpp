// Tests of the room before a match: the lobby messages, joining and refusing, leaving, the start barrier (Start, Loaded, Begin), cancelled
// starts, kicking, hostile guests, and a whole match started through the lobby.
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
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

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// A room: a host lobby and clients on links of a loopback network, driven by a virtual clock
struct Room {
    LoopbackNetwork net{7};
    HostLobby host;
    struct Guest {
        Connection* host_end{nullptr};
        Connection* client_end{nullptr};
        std::unique_ptr<ClientLobby> lobby;
    };
    std::vector<Guest> guests;
    uint32_t now{0};

    explicit Room(HostLobby::Config hc = {}, bool choose_map = true) : host(hc) { if (choose_map) host.set_map("TINY.LVL"); }      // a room has a map once its host has chosen one

    Guest& join(const std::string& name, LoopbackNetwork::Link link = {10, 0}, uint8_t want_seat = 255) {
        auto ends = net.connect(link);
        guests.emplace_back();
        Guest& g = guests.back();
        g.host_end = ends.first;
        g.client_end = ends.second;
        ClientLobby::Config cc;
        cc.name = name;
        cc.want_seat = want_seat;
        g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
        host.add_connection(ends.first, now);
        return g;
    }
    // The same, by index (the vector grows), and the Hello is answered before the next guest comes: the order of the Welcomes is the order of the calls
    size_t join_seat(const std::string& name, uint8_t want_seat = 255) {
        join(name, {10, 0}, want_seat);
        run(100);
        return guests.size() - 1;
    }
    void run(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            host.update(now);
            for (auto& g : guests) g.lobby->update(now);
        }
    }
};

// A server's room (the host holds no seat) that gives every guest a key. The keys come from a seeded generator, so that a test can tell them apart and repeat them.
HostLobby::Config keyed_server_config(uint32_t seed, const std::string& room_code = std::string()) {
    HostLobby::Config hc;
    hc.host_seat = 255;
    hc.min_players = 2;
    hc.room_code = room_code;
    auto rng = std::make_shared<Lcg>(seed);
    hc.make_key = [rng](SeatKey& key) {
        for (uint8_t& b : key) b = static_cast<uint8_t>(rng->below(256));
        return true;
    };
    return hc;
}

// A key that is not zero, and different for every `salt`
SeatKey key_with(uint8_t salt) {
    SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 31u + i * 7u + 1u);
    return k;
}

// What a Hello of protocols 6 to 9 looked like (they shared the layout: protocol 9 changed the rules of the match, not a message): no key, no turns. Protocol 10 added both.
std::vector<uint8_t> old_layout_hello(uint16_t version, const std::string& name, const std::string& room = std::string(), const std::string& token = std::string()) {
    std::vector<uint8_t> out = {static_cast<uint8_t>(MsgType::Hello), static_cast<uint8_t>(version & 0xFF), static_cast<uint8_t>(version >> 8), static_cast<uint8_t>(name.size())};
    const auto put = [&out](const std::string& s) {                  // (byte by byte: GCC 12 reads a range insert into a vector this small as an overread and the project builds with -Werror)
        for (const char c : s) out.push_back(static_cast<uint8_t>(c));
    };
    put(name);
    out.push_back(0x34);                                              // listen_port 0x1234
    out.push_back(0x12);
    out.push_back(255);                                               // want_seat
    out.push_back(static_cast<uint8_t>(room.size()));
    put(room);
    out.push_back(static_cast<uint8_t>(token.size()));
    put(token);
    return out;
}

// A guest that shows a key in its Hello (a page that was reloaded, a game that was started again), joined to the room like Room::join does; returns its index
size_t join_keyed(Room& r, const std::string& name, const SeatKey& key, uint8_t want_seat = 255, const std::string& room_code = std::string()) {
    auto ends = r.net.connect({10, 0});
    r.guests.emplace_back();
    Room::Guest& g = r.guests.back();
    g.host_end = ends.first;
    g.client_end = ends.second;
    ClientLobby::Config cc;
    cc.name = name;
    cc.want_seat = want_seat;
    cc.key = key;
    cc.room = room_code;
    g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
    r.host.add_connection(ends.first, r.now);
    return r.guests.size() - 1;
}

// A raw client (no lobby behind it) that has said Hello: what the host answers can be read from `end`
struct RawClient {
    Connection* host_end{nullptr};
    Connection* end{nullptr};
};
RawClient raw_hello(Room& r, const HelloMsg& hello) {
    auto ends = r.net.connect({10, 0});
    r.host.add_connection(ends.first, r.now);
    ends.second->send(encode(hello));
    return RawClient{ends.first, ends.second};
}
// Every message that waits on a connection
std::vector<std::vector<uint8_t>> drain_messages(Connection* c) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> m;
    while (c->poll(m)) out.push_back(m);
    return out;
}

// A connection that counts the messages that are taken from it: what a host polls in one update
class CountingConnection final : public Connection {
public:
    explicit CountingConnection(Connection* inner) : inner_(inner) {}
    bool send(const std::vector<uint8_t>& m) override { return inner_->send(m); }
    bool poll(std::vector<uint8_t>& m) override {
        if (!inner_->poll(m)) return false;
        ++taken;
        return true;
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t taken{0};

private:
    Connection* inner_;
};

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: room, joining and the start barrier\n"
                 "=======================================================\n";

    TEST_CASE("N4.1 Lobby Messages: Round Trip, Range Checks, Safe Map Names, 300000 Random And Mutated Messages") {
        RoomMsg r;
        r.slots[0] = {SlotState::Host, "Queen", 0};
        r.slots[1] = {SlotState::Client, "Bob", 250};
        r.slots[2] = {SlotState::Client, "Carl"};                     // not measured yet: kRttUnknown
        r.map_name = "TREASURE.LVL";
        r.fog = true;
        r.you = 1;
        RoomMsg r2;
        ASSERT_TRUE(decode(encode(r), r2) && r2.map_name == "TREASURE.LVL" && r2.fog && r2.you == 1 && r2.slots[1].name == "Bob" &&
                    r2.slots[1].state == SlotState::Client && r2.slots[3].state == SlotState::Empty);
        ASSERT_EQ(r2.slots[0].rtt_ms, 0);                                // the connection quality of every seat travels with the room
        ASSERT_EQ(r2.slots[1].rtt_ms, 250);
        ASSERT_EQ(r2.slots[2].rtt_ms, kRttUnknown);
        StartMsg s;
        s.seed = 4242;
        s.map_name = "SMALL.LVL";
        s.map_hash = 0x1122334455667788ull;
        s.fog = true;
        s.roster = 0x05;
        s.names[0] = "Queen";
        s.names[2] = "Carl";
        StartMsg s2;
        ASSERT_TRUE(decode(encode(s), s2) && s2.seed == 4242 && s2.map_hash == 0x1122334455667788ull && s2.roster == 5 && s2.names[2] == "Carl");
        LoadedMsg l;
        LoadedMsg l2;
        l.ok = false;
        ASSERT_TRUE(decode(encode(l), l2) && !l2.ok);
        CancelMsg c;
        c.reason = CancelMsg::Reason::LoadFailed;
        c.player = 2;
        CancelMsg c2;
        ASSERT_TRUE(decode(encode(c), c2) && c2.reason == CancelMsg::Reason::LoadFailed && c2.player == 2);
        ASSERT_EQ(peek_type(encode_begin()), MsgType::Begin);
        ASSERT_EQ(peek_type(encode_leave()), MsgType::Leave);
        // map names travel only as plain names of the maps folder
        const std::string too_long = std::string(kMaxMapNameChars - 3, 'M') + ".lvl";           // 65 characters: one too many
        for (const std::string& bad : {std::string("../secret.LVL"), std::string("a/b.LVL"), std::string("a\\b.LVL"), std::string(".hidden.LVL"), std::string("x.txt"), std::string("LVL"),
                                      std::string("a:b.LVL"), std::string("a*b.lvl"), std::string("a?b.lvl"), std::string("a\"b.lvl"), std::string("a<b.lvl"), std::string("a>b.lvl"), std::string("a|b.lvl"),
                                      std::string("tab\t.lvl"), std::string("del\x7f.lvl"), std::string("caf\xC3\xA9.lvl"), std::string("nul\0x.lvl", 10), std::string("x.lv"), std::string("x.lvlx"),
                                      std::string("..lvl"), too_long}) {
            ASSERT_FALSE(valid_map_name(bad));
            RoomMsg x = r;
            x.map_name = bad;
            RoomMsg y;
            ASSERT_FALSE(decode(encode(x), y));
            StartMsg sx = s;
            sx.map_name = bad;
            StartMsg sy;
            ASSERT_FALSE(decode(encode(sx), sy));
        }
        // the empty name is the room's "no map chosen yet": a Room may carry it, a Start may not
        ASSERT_FALSE(valid_map_name(""));
        {
            RoomMsg x = r;
            x.map_name = "";
            RoomMsg y;
            ASSERT_TRUE(decode(encode(x), y) && y.map_name.empty());
            StartMsg sx = s;
            sx.map_name = "";
            StartMsg sy;
            ASSERT_FALSE(decode(encode(sx), sy));
        }
        ASSERT_TRUE(valid_map_name("TREASURE.LVL") && valid_map_name("my-map_2.lvl"));
        // protocol 6: the community's names hold spaces, '!', '~', '#', '$', '&', quotes and even ".." (a name cannot hold a separator, so none of them names a path)
        for (const char* good : {"!!!! My Map ~v2~ (final).lvl", "ANTS WORLD....lvl", "Beach Day !...lvl", "OHH MY GOD..  TRAP!!.lvl", "Lero .lvl", "#1 Map $5 & It's.lvl", "a.LVL", "x y.lvl"}) {
            ASSERT_TRUE(valid_map_name(good));
            RoomMsg x = r;
            x.map_name = good;
            RoomMsg y;
            ASSERT_TRUE(decode(encode(x), y) && y.map_name == good);
            StartMsg sx = s;
            sx.map_name = good;
            StartMsg sy;
            ASSERT_TRUE(decode(encode(sx), sy) && sy.map_name == good);
        }
        ASSERT_TRUE(valid_map_name(std::string(kMaxMapNameChars - 4, 'M') + ".lvl"));            // exactly the longest
        ASSERT_FALSE(valid_map_name(std::string(kMaxMapNameChars - 3, 'M') + ".lvl"));           // one more is too long
        // room codes and tokens
        ASSERT_TRUE(valid_room_code("") && valid_room_code("ABCD-1234") && valid_room_code("room_7") && valid_room_code(std::string(kMaxRoomCodeChars, 'r')));
        ASSERT_FALSE(valid_room_code(std::string(kMaxRoomCodeChars + 1, 'r')));
        for (const char* bad_code : {"a b", "a/b", "..", "r\n", "room!", "caf\xC3\xA9"}) ASSERT_FALSE(valid_room_code(bad_code));
        {
            HelloMsg h;
            h.name = "Ann";
            h.room = "ROOM-42";
            h.token = "tok.en+/=_-123";
            HelloMsg back;
            ASSERT_TRUE(decode(encode(h), back) && back.room == "ROOM-42" && back.token == h.token && back.name == "Ann" && back.want_seat == 255);
            HelloMsg bad_room = h;
            bad_room.room = "no spaces";
            ASSERT_FALSE(decode(encode(bad_room), back));
            HelloMsg bad_token = h;
            bad_token.token = "a b";                                                              // a token has no spaces
            ASSERT_FALSE(decode(encode(bad_token), back));
            HelloMsg none;                                                                         // a LAN / direct Hello: no room, no token
            ASSERT_TRUE(decode(encode(none), back) && back.room.empty() && back.token.empty());
        }
        // out of range fields
        std::vector<uint8_t> b = encode(r);
        b[b.size() - 2] = 4;                                              // you = 4 (only 255 or 0..3); the last byte is the leader's since protocol 7
        ASSERT_FALSE(decode(b, r2));
        b = encode(r);
        b[b.size() - 1] = 4;                                              // leader = 4 (only 255 or the seat of a guest)
        ASSERT_FALSE(decode(b, r2));
        StartMsg one;
        one.map_name = "SMALL.LVL";
        one.roster = 0x01;                                                // one player is no match
        ASSERT_FALSE(decode(encode(one), s2));
        one.roster = 0x13;
        ASSERT_FALSE(decode(encode(one), s2));                            // a seat that does not exist
        CancelMsg bc;
        bc.reason = CancelMsg::Reason::PlayerLeft;
        std::vector<uint8_t> cb = encode(bc);
        cb[1] = 0;
        ASSERT_FALSE(decode(cb, c2));
        cb[1] = 4;
        ASSERT_FALSE(decode(cb, c2));
        cb[1] = 1;
        cb[2] = 4;
        ASSERT_FALSE(decode(cb, c2));
        // protocol 7: a server's room names its leader
        RoomMsg srv;
        srv.slots[0] = {SlotState::Client, "Ann", 20};
        srv.slots[1] = {SlotState::Client, "Bob", 40};
        srv.map_name = "TINY.LVL";
        srv.you = 1;
        srv.leader = 0;
        ASSERT_TRUE(decode(encode(srv), r2) && r2.leader == 0 && r2.you == 1 && r2.slots[0].name == "Ann");
        const std::vector<uint8_t> request = encode(StartRequestMsg{});
        StartRequestMsg ee0;
        // every strict prefix and one extra byte are rejected
        for (const auto& m : {encode(r), encode(s), encode(l), encode(c), encode(srv), request}) {
            for (size_t cut = 0; cut < m.size(); ++cut) {
                std::vector<uint8_t> sh(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(cut));
                RoomMsg a; StartMsg bb; LoadedMsg cc; CancelMsg dd;
                ASSERT_FALSE(decode(sh, a) || decode(sh, bb) || decode(sh, cc) || decode(sh, dd));
            }
            std::vector<uint8_t> lg = m;
            lg.push_back(0);
            RoomMsg a; StartMsg bb; LoadedMsg cc; CancelMsg dd;
            ASSERT_FALSE(decode(lg, a) || decode(lg, bb) || decode(lg, cc) || decode(lg, dd));
        }
        Lcg rng(5);
        StartRequestMsg leader_request;                                   // protocol 13: a level for each seat and a pair of seats as a team
        leader_request.fill = {FillLevel::None, FillLevel::Hard, FillLevel::Easy, FillLevel::Medium};
        leader_request.set_teams(sim::StartTeams{true, 0, 2});
        StartMsg with_teams = s;
        with_teams.roster = 0x0F;
        with_teams.set_teams(sim::StartTeams{true, 1, 3});
        ASSERT_TRUE(decode(encode(with_teams), s2) && s2.team_a == 1 && s2.team_b == 3 && s2.roster == 0x0F);
        ASSERT_TRUE(decode(encode(leader_request), ee0) && ee0.team_a == 0 && ee0.team_b == 2 && ee0.fill == leader_request.fill);
        const std::vector<std::vector<uint8_t>> seeds = {encode(r), encode(s), encode(l), encode(c), encode(srv), request, encode(leader_request), encode(with_teams)};
        size_t accepted = 0;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
            for (uint32_t m = 1 + rng.below(3); m > 0; --m) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
            if (rng.below(5) == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            RoomMsg a; StartMsg bb; LoadedMsg cc; CancelMsg dd; StartRequestMsg ee;
            if (decode(buf, a)) {
                ++accepted;
                ASSERT_TRUE(encode(a) == buf);
                ASSERT_TRUE(valid_map_name(a.map_name));
                ASSERT_TRUE(a.leader == kNoLeader || (a.leader < sim::MAX_PLAYERS && a.slots[a.leader].state == SlotState::Client));
            }
            if (decode(buf, bb)) { ++accepted; ASSERT_TRUE(encode(bb) == buf); ASSERT_TRUE(valid_map_name(bb.map_name)); }
            if (decode(buf, cc)) { ++accepted; ASSERT_TRUE(encode(cc) == buf); }
            if (decode(buf, dd)) { ++accepted; ASSERT_TRUE(encode(dd) == buf); }
            if (decode(buf, ee)) {                                          // (protocol 13: the type, a level 0 .. 3 for each seat, two team bytes: none, or two different seats)
                ++accepted;
                ASSERT_TRUE(encode(ee) == buf && buf.size() == 7);
                for (const FillLevel level : ee.fill) ASSERT_TRUE(static_cast<uint8_t>(level) <= kFillLevelLast);
                ASSERT_TRUE((ee.team_a == kNoTeam && ee.team_b == kNoTeam) || (ee.team_a < sim::MAX_PLAYERS && ee.team_b < sim::MAX_PLAYERS && ee.team_a != ee.team_b));
            }
        }
        ASSERT_TRUE(accepted > 3000);
    } TEST_END();

    TEST_CASE("N4.2c Room Codes (Protocol 6): A Server's Room Takes Only Hellos For Its Own Code; A LAN Host Takes Only Hellos Without One") {
        auto answer_to = [](Room& r, const HelloMsg& hello) {
            auto ends = r.net.connect({10, 0});
            r.host.add_connection(ends.first, 0);
            ends.second->send(encode(hello));
            r.run(100);
            std::vector<uint8_t> reply;
            RejectMsg rj;
            WelcomeMsg w;
            if (!ends.second->poll(reply)) return std::string("silent");
            if (decode(reply, rj)) return std::string("rejected ") + std::to_string(static_cast<int>(rj.reason));
            if (decode(reply, w)) return std::string("welcome");
            return std::string("other");
        };
        const std::string no_such_room = std::string("rejected ") + std::to_string(static_cast<int>(RejectReason::NoSuchRoom));
        {   // a room of a server
            HostLobby::Config hc;
            hc.room_code = "ROOM-7";
            Room server(hc);
            HelloMsg h;
            h.name = "Ann";
            h.room = "ROOM-7";
            h.token = "t0k3n";
            ASSERT_EQ(answer_to(server, h), std::string("welcome"));
            h.room = "ROOM-8";
            ASSERT_EQ(answer_to(server, h), no_such_room);                              // another room
            h.room = "";
            ASSERT_EQ(answer_to(server, h), no_such_room);                              // no room at all
            h.room = "room-7";
            ASSERT_EQ(answer_to(server, h), no_such_room);                              // the code is case sensitive
            ASSERT_EQ(server.host.players(), 2u);                                       // the host's seat and Ann: nobody else got in
        }
        {   // a LAN / direct host has no room code
            Room lan;
            HelloMsg h;
            h.name = "Bob";
            ASSERT_EQ(answer_to(lan, h), std::string("welcome"));
            h.room = "ROOM-7";
            ASSERT_EQ(answer_to(lan, h), no_such_room);
            ASSERT_EQ(lan.host.players(), 2u);
        }
    } TEST_END();

    TEST_CASE("N4.2 Joining: Seats Are Handed Out In Order, Everybody Sees The Roster; Full Rooms, Old Versions And Garbage Are Refused") {
        HostLobby::Config hc;
        hc.host_name = "Queen";
        Room room(hc);
        room.host.set_map("SMALL.LVL");
        room.host.set_fog(true);
        room.join("Bob");
        room.join("Carl");
        room.run(200);
        ASSERT_EQ(room.guests[0].lobby->phase(), ClientLobby::Phase::InRoom);
        ASSERT_EQ(room.guests[0].lobby->my_seat(), 1);
        ASSERT_EQ(room.guests[1].lobby->my_seat(), 2);
        ASSERT_EQ(room.host.players(), 3u);
        for (auto& g : room.guests) {
            const RoomMsg& r = g.lobby->room();
            ASSERT_EQ(r.slots[0].state, SlotState::Host);
            ASSERT_EQ(r.slots[0].name, "Queen");
            ASSERT_EQ(r.slots[1].name, "Bob");
            ASSERT_EQ(r.slots[2].name, "Carl");
            ASSERT_EQ(r.slots[3].state, SlotState::Empty);
            ASSERT_EQ(r.map_name, "SMALL.LVL");
            ASSERT_TRUE(r.fog);
            ASSERT_EQ(r.you, g.lobby->my_seat());
        }
        room.join("Dora");
        room.run(200);
        ASSERT_EQ(room.guests[2].lobby->my_seat(), 3);
        // the fifth guest is turned away
        Room::Guest& fifth = room.join("Eve");
        room.run(200);
        ASSERT_EQ(fifth.lobby->phase(), ClientLobby::Phase::Rejected);
        ASSERT_EQ(fifth.lobby->reject_reason(), RejectReason::Full);
        ASSERT_EQ(room.host.players(), 4u);
        // an old client
        {
            Room r2;
            auto ends = r2.net.connect({10, 0});
            r2.host.add_connection(ends.first, 0);
            HelloMsg old;
            old.version = 0;
            old.name = "Old";
            ends.second->send(encode(old));
            r2.run(100);
            std::vector<uint8_t> m;
            ASSERT_TRUE(ends.second->poll(m));
            RejectMsg rj;
            ASSERT_TRUE(decode(m, rj) && rj.reason == RejectReason::VersionMismatch);
            ASSERT_EQ(r2.host.players(), 1u);
            ASSERT_FALSE(ends.second->is_open());
        }
        // a connection that starts with garbage, one that says nothing, one that sends an oversized first message
        {
            Room r3;
            auto a = r3.net.connect({10, 0});
            auto b = r3.net.connect({10, 0});
            auto c = r3.net.connect({10, 0});
            r3.host.add_connection(a.first, 0);
            r3.host.add_connection(b.first, 0);
            r3.host.add_connection(c.first, 0);
            a.second->send({99, 1, 2, 3});
            c.second->send(std::vector<uint8_t>(kMaxMessageBytes + 5, 1));
            r3.run(100);
            ASSERT_FALSE(a.second->is_open());                                    // not a Hello: closed
            std::vector<uint8_t> reply;
            ASSERT_TRUE(c.second->poll(reply));                                   // the oversized one is answered (Reject: bad request) ...
            RejectMsg rj;
            ASSERT_TRUE(decode(reply, rj) && rj.reason == RejectReason::BadRequest);
            ASSERT_FALSE(c.second->is_open());                                    // ... and closed once the answer has been read
            ASSERT_TRUE(b.second->is_open());                                     // silent so far
            r3.run(11000);                                                        // the Hello timeout
            ASSERT_FALSE(b.second->is_open());
            ASSERT_EQ(r3.host.players(), 1u);
        }
    } TEST_END();

    TEST_CASE("N4.2b Seat Requests: A Guest Gets The Seat It Asks For When It Is Free, Otherwise The First Free One (Whatever The Order Of Arrival)") {
        // a guest that asks for a seat (the room owns the vector of guests, which grows: the tests keep indices, not references)
        const auto ask = [](Room& r, const std::string& name, uint8_t want) -> size_t {
            auto ends = r.net.connect({10, 0});
            r.guests.emplace_back();
            Room::Guest& g = r.guests.back();
            g.host_end = ends.first;
            g.client_end = ends.second;
            ClientLobby::Config cc;
            cc.name = name;
            cc.want_seat = want;
            g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
            r.host.add_connection(ends.first, r.now);
            return r.guests.size() - 1;
        };
        // the four ant colours sit where the guests asked: the last to arrive asked for the first guest seat
        Room room;
        const size_t blue = ask(room, "Blue", 2);
        const size_t black = ask(room, "Black", 3);
        const size_t red = ask(room, "Red", 1);
        room.run(300);
        ASSERT_EQ(room.guests[blue].lobby->my_seat(), 2);
        ASSERT_EQ(room.guests[black].lobby->my_seat(), 3);
        ASSERT_EQ(room.guests[red].lobby->my_seat(), 1);
        ASSERT_EQ(room.host.room().slots[1].name, "Red");
        ASSERT_EQ(room.host.room().slots[2].name, "Blue");
        ASSERT_EQ(room.host.room().slots[3].name, "Black");
        ASSERT_EQ(room.host.players(), 4u);
        // a seat that is taken, the host's own seat and no request at all give the first free seat
        Room room2;
        const size_t first = ask(room2, "First", 255);                              // no request: seat 1
        const size_t taken = ask(room2, "Taken", 1);                                // seat 1 is taken: seat 2
        const size_t host_seat = ask(room2, "HostSeat", 0);                         // the host's seat: seat 3
        room2.run(300);
        ASSERT_EQ(room2.guests[first].lobby->my_seat(), 1);
        ASSERT_EQ(room2.guests[taken].lobby->my_seat(), 2);
        ASSERT_EQ(room2.guests[host_seat].lobby->my_seat(), 3);
        // a seat that does not exist
        Room room3;
        const size_t absurd = ask(room3, "Absurd", 9);
        room3.run(200);
        ASSERT_EQ(room3.guests[absurd].lobby->my_seat(), 1);
        // a seat that a guest left is free for a request again
        room.guests[red].lobby->leave();                                            // Red (seat 1) leaves
        room.run(300);
        ASSERT_EQ(room.host.players(), 3u);
        const size_t again = ask(room, "Again", 1);
        room.run(300);
        ASSERT_EQ(room.guests[again].lobby->my_seat(), 1);
    } TEST_END();

    TEST_CASE("N4.2c Hello: The Seat Request Travels With It; A Hello Of The Previous Layout Is Still Answered With 'Version Mismatch'") {
        HelloMsg h;
        h.name = "Queen Ant";
        h.want_seat = 2;
        HelloMsg h2;
        ASSERT_TRUE(decode(encode(h), h2));
        ASSERT_EQ(h2.want_seat, 2);
        ASSERT_EQ(h2.version, kProtocolVersion);
        HelloMsg any;
        HelloMsg any2;
        ASSERT_TRUE(decode(encode(any), any2));
        ASSERT_EQ(any2.want_seat, 255);                                          // no request is 255
        // the layout of protocol 4 (no seat byte) and a longer one of a later version are not this protocol's Hello (the strict decoder refuses them, which keeps
        // decode -> encode exact), but their prefix (version, name) is read, so that the host can name the real problem
        std::vector<uint8_t> v4 = {static_cast<uint8_t>(MsgType::Hello), 4, 0, 3, 'O', 'l', 'd', 0xA1, 0x0F};
        HelloMsg old;
        ASSERT_FALSE(decode(v4.data(), v4.size(), old));
        ASSERT_TRUE(decode_hello_prefix(v4.data(), v4.size(), old));
        ASSERT_EQ(old.version, 4);
        ASSERT_EQ(old.name, "Old");
        std::vector<uint8_t> future = {static_cast<uint8_t>(MsgType::Hello), static_cast<uint8_t>(kProtocolVersion + 1), 0, 3, 'N', 'e', 'w', 0xA1, 0x0F, 1, 2, 3, 4};
        HelloMsg next;
        ASSERT_FALSE(decode(future.data(), future.size(), next));
        ASSERT_TRUE(decode_hello_prefix(future.data(), future.size(), next));
        ASSERT_EQ(next.version, kProtocolVersion + 1);
        std::vector<uint8_t> torn = {static_cast<uint8_t>(MsgType::Hello), 4, 0, 9, 'O', 'l'};      // a name that runs past the end is no Hello at all
        ASSERT_FALSE(decode_hello_prefix(torn.data(), torn.size(), old));
        std::vector<uint8_t> coded = {static_cast<uint8_t>(MsgType::Hello), 4, 0, 2, 'O', 0x07};    // nor is a name with a control character
        ASSERT_FALSE(decode_hello_prefix(coded.data(), coded.size(), old));
        // protocols 6 to 9 shared one Hello layout (the fields up to the token): it is not this protocol's Hello (which has the key and the turns after them), but its version and name
        // are read, so that the host answers "version mismatch" without reading what follows
        for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {
            const std::vector<uint8_t> raw = old_layout_hello(version, "Old", "ROOM-7", "tok");
            ASSERT_FALSE(decode(raw.data(), raw.size(), old));
            ASSERT_TRUE(decode_hello_prefix(raw.data(), raw.size(), old));
            ASSERT_EQ(old.version, version);
            ASSERT_EQ(old.name, "Old");
        }
        // the current layout must be exact
        std::vector<uint8_t> current = encode(h);
        current.push_back(0);
        ASSERT_FALSE(decode(current.data(), current.size(), h2));
        std::vector<uint8_t> shorter = encode(h);
        shorter.pop_back();
        ASSERT_FALSE(decode(shorter.data(), shorter.size(), h2));                // (the last byte of the turns is missing)
        // on the wire: an old client is refused with the reason, not with "bad request"
        Room r;
        auto ends = r.net.connect({10, 0});
        r.host.add_connection(ends.first, 0);
        ends.second->send(v4);
        r.run(100);
        std::vector<uint8_t> reply;
        ASSERT_TRUE(ends.second->poll(reply));
        RejectMsg rj;
        ASSERT_TRUE(decode(reply, rj) && rj.reason == RejectReason::VersionMismatch);
        // clients of protocols 6 to 9 (the versions before the keys: their Hello has the old layout; 7, 8 and 9 are the leader's, the latency release's and the community-map rules'): the same answer from a
        // LAN host and from a server's room alike, whatever is in the Hello (the key that this protocol's Hello has is not read from it), and nobody is seated
        for (const bool server : {false, true}) {
            for (const uint16_t version : {uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}}) {
                HostLobby::Config hc;
                if (server) hc.host_seat = 255;
                Room r6(hc);
                auto e6 = r6.net.connect({10, 0});
                r6.host.add_connection(e6.first, 0);
                e6.second->send(old_layout_hello(version, "Old", std::string(), "tok"));
                r6.run(100);
                ASSERT_TRUE(e6.second->poll(reply) && decode(reply, rj) && rj.reason == RejectReason::VersionMismatch);
                ASSERT_EQ(r6.host.players(), server ? size_t{0} : size_t{1});          // nobody was seated, and no leader named
                ASSERT_EQ(r6.host.leader(), kNoLeader);
            }
        }
        // a Hello of this layout but another version number is "version mismatch" as well (protocols 8 and 9 with the new fields: nobody sends them, a stranger may), key and all; before the
        // keys, protocol 8 (v0.0.94) was the release whose Hello had exactly the layout of protocol 9 (the community-map rules): nothing but the number told them apart, and the number refuses them.
        // The same is the whole of how protocol 11 (v0.1.0 and v0.1.1) is refused by a host of protocol 12 and later: its Hello is byte for byte a Hello of 12, and a client of 11 would count its "Get ready"
        // dialog in simulation ticks and be blocked for 100 ticks of the running match after the host's late first turn (the match clock waits for the dialog since 12). Protocol 12 (v0.2.0 to v0.4.0) is
        // refused by a host of 13 the same way: its leader's StartRequest is two bytes and its Start has no team bytes, and the Hello does not tell. Protocol 13 (v0.8.0 to v0.8.2) is refused by a host of 14 by
        // the number alone: a client of 13 cannot send a SeatMove, and a host of 14 must not take a client that cannot be told "the leader moved you" for one that can.
        ASSERT_EQ(kProtocolVersion, 14);
        for (const uint16_t version : {uint16_t{8}, uint16_t{9}, uint16_t{11}, uint16_t{12}, uint16_t{13}}) {
            Room r8;
            auto e8 = r8.net.connect({10, 0});
            r8.host.add_connection(e8.first, 0);
            HelloMsg other = h;
            other.version = version;
            other.key = key_with(1);
            other.have_turns = 5;
            const std::vector<uint8_t> other_hello = encode(other);
            ASSERT_TRUE(decode(other_hello.data(), other_hello.size(), old));
            ASSERT_EQ(old.version, version);
            ASSERT_TRUE(kProtocolVersion != version);
            e8.second->send(other_hello);
            r8.run(100);
            ASSERT_TRUE(e8.second->poll(reply) && decode(reply, rj) && rj.reason == RejectReason::VersionMismatch);
            ASSERT_EQ(r8.host.players(), size_t{1});
        }
    } TEST_END();

    TEST_CASE("N4.3 Leaving: A Guest Who Leaves Frees Its Seat For The Next One, Everybody Is Told") {
        Room room;
        room.join("Bob");
        room.join("Carl");
        room.run(200);
        room.guests[0].lobby->leave();
        room.run(200);
        ASSERT_EQ(room.host.players(), 2u);
        ASSERT_FALSE(room.host.occupied(1));
        ASSERT_EQ(room.guests[1].lobby->room().slots[1].state, SlotState::Empty);
        Room::Guest& dora = room.join("Dora");
        room.run(200);
        ASSERT_EQ(dora.lobby->my_seat(), 1);                                      // the freed seat
        ASSERT_EQ(room.guests[1].lobby->room().slots[1].name, "Dora");
        // a guest whose connection dies
        room.net.cut(room.guests[1].client_end, true);
        room.run(200);
        ASSERT_FALSE(room.host.occupied(2));
        bool left = false;
        for (const auto& e : room.host.take_events()) left = left || (e.type == HostLobby::Event::Type::Left && e.seat == 2);
        ASSERT_TRUE(left);
    } TEST_END();

    TEST_CASE("N4.4 The Start Barrier: Start, Every Machine Loads And Reports, Begin Only When All Are Loaded") {
        Room room;
        room.join("Bob");
        room.join("Carl");
        room.run(200);
        ASSERT_TRUE(room.host.can_start());
        ASSERT_TRUE(room.host.start(777, 0xABCDEF, room.now));
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
        ASSERT_FALSE(room.host.start(1, 1, room.now));                            // not twice
        room.run(200);
        for (auto& g : room.guests) {
            ASSERT_EQ(g.lobby->phase(), ClientLobby::Phase::Loading);
            ASSERT_EQ(g.lobby->start_info().seed, 777u);
            ASSERT_EQ(g.lobby->start_info().map_hash, 0xABCDEFull);
            ASSERT_EQ(g.lobby->start_info().roster, 0x07);
            ASSERT_EQ(g.lobby->start_info().names[2], "Carl");
        }
        room.guests[0].lobby->report_loaded(true);
        room.run(200);
        room.host.host_loaded(true);
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);                  // Carl has not reported
        room.guests[1].lobby->report_loaded(true);
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
        for (auto& g : room.guests) ASSERT_EQ(g.lobby->phase(), ClientLobby::Phase::Begun);
        ASSERT_TRUE(room.host.connection_of(1) != nullptr && room.host.connection_of(2) != nullptr);
        ASSERT_TRUE(room.host.connection_of(0) == nullptr && room.host.connection_of(3) == nullptr);
        // nobody can join a running match
        Room::Guest& late = room.join("Late");
        room.run(200);
        ASSERT_TRUE(late.lobby->phase() == ClientLobby::Phase::Joining || late.lobby->phase() == ClientLobby::Phase::Connecting);   // the lobby no longer listens
    } TEST_END();

    TEST_CASE("N4.5 A Failed Start Is Cancelled For Everybody (load failure, a leaver, host cancel, timeout) And Can Be Tried Again") {
        Room room;
        room.join("Bob");
        room.join("Carl");
        room.run(200);
        // Carl cannot load the map (a different file)
        room.host.start(1, 1, room.now);
        room.run(100);
        room.guests[0].lobby->report_loaded(true);
        room.guests[1].lobby->report_loaded(false);
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
        ASSERT_EQ(room.guests[0].lobby->phase(), ClientLobby::Phase::InRoom);
        ASSERT_EQ(room.guests[0].lobby->cancel_reason(), CancelMsg::Reason::LoadFailed);
        ASSERT_EQ(room.guests[1].lobby->phase(), ClientLobby::Phase::InRoom);
        ASSERT_EQ(room.host.players(), 3u);                                       // nobody was thrown out
        // a leaver during the load
        room.host.start(2, 1, room.now);
        room.run(100);
        room.guests[1].lobby->leave();
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
        ASSERT_EQ(room.guests[0].lobby->cancel_reason(), CancelMsg::Reason::PlayerLeft);
        ASSERT_EQ(room.host.players(), 2u);
        // the host cancels
        room.host.start(3, 1, room.now);
        room.run(100);
        room.host.cancel();
        room.run(100);
        ASSERT_EQ(room.guests[0].lobby->cancel_reason(), CancelMsg::Reason::HostCancelled);
        // the host itself cannot load
        room.host.start(4, 1, room.now);
        room.host.host_loaded(false);
        room.run(100);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
        // a guest that never answers: the load times out
        room.host.start(5, 1, room.now);
        room.host.host_loaded(true);
        room.run(61000);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
        ASSERT_EQ(room.guests[0].lobby->phase(), ClientLobby::Phase::InRoom);
        // and it can be tried again and then works
        room.host.start(6, 1, room.now);
        room.host.host_loaded(true);
        room.run(100);
        room.guests[0].lobby->report_loaded(true);
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
    } TEST_END();

    TEST_CASE("N4.5b A Dedicated Server's Room (A Host Without A Seat): Guests Take The Seats From 0, The Room Has No Host Slot, The Start Needs The Configured Number, The Barrier Is The Same") {
        HostLobby::Config hc;
        hc.host_seat = 255;                                                         // kNoSeat
        hc.min_players = 3;
        Room room(hc);
        ASSERT_EQ(room.host.players(), size_t{0});                                  // nobody sits there yet: the host holds no seat
        ASSERT_FALSE(room.host.can_start());
        room.join("Ann");
        room.join("Bob");
        room.run(200);
        ASSERT_EQ(room.guests[0].lobby->my_seat(), 0);                              // seat 0 is a guest's
        ASSERT_EQ(room.guests[1].lobby->my_seat(), 1);
        for (auto& g : room.guests) {
            const RoomMsg& r = g.lobby->room();
            for (const auto& slot : r.slots) ASSERT_TRUE(slot.state != SlotState::Host);      // no Host slot: the clients know it is a server's room
            ASSERT_EQ(r.slots[0].name, "Ann");
            ASSERT_EQ(r.slots[1].name, "Bob");
            ASSERT_EQ(r.slots[2].state, SlotState::Empty);
        }
        ASSERT_EQ(room.host.players(), size_t{2});
        ASSERT_FALSE(room.host.can_start());                                        // two of the three that the room wants
        ASSERT_FALSE(room.host.start(1, 1, room.now));
        room.join("Cara");
        room.run(200);
        ASSERT_TRUE(room.host.can_start());
        ASSERT_TRUE(room.host.start(7, 99, room.now));
        room.run(100);
        for (auto& g : room.guests) ASSERT_EQ(g.lobby->phase(), ClientLobby::Phase::Loading);
        ASSERT_EQ(room.guests[0].lobby->start_info().roster, 0x07);                 // seats 0, 1, 2
        room.host.host_loaded(true);                                                // the server loaded the map itself
        for (auto& g : room.guests) g.lobby->report_loaded(true);
        room.run(200);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
        for (auto& g : room.guests) ASSERT_EQ(g.lobby->phase(), ClientLobby::Phase::Begun);
        ASSERT_TRUE(room.host.connection_of(0) != nullptr && room.host.connection_of(1) != nullptr && room.host.connection_of(2) != nullptr);
        // a failed load by the server itself cancels like any other
        Room again(hc);
        again.join("A");
        again.join("B");
        again.join("C");
        again.run(200);
        ASSERT_TRUE(again.host.start(1, 1, again.now));
        again.host.host_loaded(false);
        again.run(100);
        ASSERT_EQ(again.host.phase(), HostLobby::Phase::Room);
        ASSERT_EQ(again.guests[0].lobby->cancel_reason(), CancelMsg::Reason::LoadFailed);
    } TEST_END();

    TEST_CASE("N4.6 Kicking And Hostile Guests: A Kicked Guest Is Told, Wrong Messages Get A Guest Thrown Out") {
        Room room;
        room.join("Bob");
        room.join("Evil");
        room.run(200);
        room.host.kick(1);
        room.run(200);
        ASSERT_EQ(room.guests[0].lobby->phase(), ClientLobby::Phase::Rejected);     // told why: Kicked
        ASSERT_EQ(room.guests[0].lobby->reject_reason(), RejectReason::Kicked);
        ASSERT_FALSE(room.host.occupied(1));
        room.host.kick(0);                                                          // the host cannot kick itself
        ASSERT_TRUE(room.host.occupied(0));
        // Evil sends what a guest must not: Start, Turn, Command, a second Hello, garbage
        Connection* evil = room.guests[1].client_end;
        for (int i = 0; i < 20; ++i) {
            StartMsg forged_start;
            forged_start.map_name = "SMALL.LVL";
            forged_start.map_hash = 1;
            forged_start.roster = 0x03;
            forged_start.names = {"a", "b", "", ""};
            evil->send(encode(forged_start));
            evil->send(encode(TurnMsg{}));
            evil->send(encode(HelloMsg{}));
            evil->send({250, 1, 2});
        }
        room.run(300);
        ASSERT_FALSE(room.host.occupied(2));                                        // out
        ASSERT_FALSE(evil->is_open());
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);                       // and the room did not start anything
    } TEST_END();

    TEST_CASE("N4.7 Room Settings: The Map And The Fog Reach Every Guest, Nothing Changes After The Start") {
        Room room;
        room.join("Bob");
        room.run(100);
        room.host.set_map("ISLANDS.LVL");
        room.host.set_fog(true);
        room.host.set_map("../evil.LVL");                                           // refused
        room.run(100);
        ASSERT_EQ(room.guests[0].lobby->room().map_name, "ISLANDS.LVL");
        ASSERT_TRUE(room.guests[0].lobby->room().fog);
        room.host.start(1, 1, room.now);
        room.host.set_map("TINY.LVL");
        room.host.set_fog(false);
        room.run(100);
        ASSERT_EQ(room.host.map_name(), "ISLANDS.LVL");
        ASSERT_TRUE(room.host.fog());
    } TEST_END();

    TEST_CASE("N4.9 Thumbs: The Host Measures Every Guest's Round Trip And Tells Everybody; The Tiers Are The Original's 1200 / 1800 ms") {
        Room room;
        room.join("Fast", {10, 0});                                      // 20 ms round trip
        room.join("Slow", {700, 0});                                     // 1400 ms: ok
        room.join("Awful", {1000, 0});                                   // 2000 ms: bad
        room.run(300);                                                   // the slow guests' Hello messages are still on their way
        ASSERT_TRUE(room.host.measured(0));                              // the host itself
        ASSERT_TRUE(room.host.measured(1));
        ASSERT_FALSE(room.host.occupied(2));
        ASSERT_TRUE(room.host.all_measured());
        ASSERT_EQ(link_quality(room.host.room().slots[1].rtt_ms), LinkQuality::Good);
        room.run(1200);                                                  // seated now, but no answer to the first ping yet: the question mark
        ASSERT_TRUE(room.host.occupied(2) && room.host.occupied(3));
        ASSERT_FALSE(room.host.measured(2));
        ASSERT_FALSE(room.host.measured(3));
        ASSERT_FALSE(room.host.all_measured());
        ASSERT_EQ(link_quality(room.host.room().slots[2].rtt_ms), LinkQuality::Unknown);
        room.run(2000);
        ASSERT_TRUE(room.host.all_measured());
        ASSERT_TRUE(room.host.rtt_ms(1) >= 20 && room.host.rtt_ms(1) <= 40);
        ASSERT_TRUE(room.host.rtt_ms(2) >= 1400 && room.host.rtt_ms(2) <= 1430);
        ASSERT_TRUE(room.host.rtt_ms(3) >= 2000 && room.host.rtt_ms(3) <= 2030);
        ASSERT_EQ(link_quality(room.host.room().slots[0].rtt_ms), LinkQuality::Good);
        ASSERT_EQ(link_quality(room.host.room().slots[1].rtt_ms), LinkQuality::Good);
        ASSERT_EQ(link_quality(room.host.room().slots[2].rtt_ms), LinkQuality::Ok);
        ASSERT_EQ(link_quality(room.host.room().slots[3].rtt_ms), LinkQuality::Bad);
        // every guest sees the same thumbs (the host tells them when a tier changes; the slowest link needs a second to deliver the news)
        room.run(1500);
        for (auto& g : room.guests) {
            ASSERT_EQ(link_quality(g.lobby->room().slots[0].rtt_ms), LinkQuality::Good);
            ASSERT_EQ(link_quality(g.lobby->room().slots[1].rtt_ms), LinkQuality::Good);
            ASSERT_EQ(link_quality(g.lobby->room().slots[2].rtt_ms), LinkQuality::Ok);
            ASSERT_EQ(link_quality(g.lobby->room().slots[3].rtt_ms), LinkQuality::Bad);
        }
        // a guest that leaves takes its thumb with it; the next one starts unmeasured
        room.guests[2].lobby->leave();
        room.run(1500);
        ASSERT_EQ(link_quality(room.host.room().slots[3].rtt_ms), LinkQuality::Unknown);
        ASSERT_TRUE(room.host.all_measured());
    } TEST_END();

    TEST_CASE("N4.8 A Whole Match Started Through The Lobby: Host And Three Guests Load, Begin And Play 30 Seconds Bit-Identical") {
        Room room;
        room.join("Bob", {40, 10});
        room.join("Carl", {80, 30});
        room.join("Dora", {30, 5});
        room.run(300);
        ASSERT_TRUE(room.host.start(31337, 1, room.now));
        room.run(200);
        // every machine builds the same world from the Start message
        std::vector<std::unique_ptr<sim::SimulationEngine>> sims;
        std::vector<std::vector<uint32_t>> ants(4);
        for (int i = 0; i < 4; ++i) {
            sims.push_back(std::make_unique<sim::SimulationEngine>());
            sims.back()->init_test_world(60, 60, 31337, 720000);
            const TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
            for (uint8_t p = 0; p < 4; ++p) sims.back()->grid_mut().set_anthill(p, hills[p]);
            for (uint8_t p = 0; p < 4; ++p) {
                ants[p].clear();
                for (int k = 0; k < 4; ++k) ants[p].push_back(sims.back()->spawn_unit(p, k == 0 ? AntType::Worker : AntType::Combat, TileCoord{hills[p].x + 1 + k, hills[p].y + 6}));
            }
        }
        for (auto& g : room.guests) g.lobby->report_loaded(true);
        room.host.host_loaded(true);
        room.run(300);
        ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
        // hand the connections to the match sessions (the seats follow the order in which the Hellos reached the host, not the order of joining)
        HostSession host(*sims[0], HostSession::Config{});
        std::vector<std::unique_ptr<ClientSession>> clients(4);
        for (auto& g : room.guests) {
            const uint8_t seat = g.lobby->my_seat();
            ASSERT_TRUE(seat >= 1 && seat <= 3 && clients[seat] == nullptr);
            ASSERT_TRUE(room.host.connection_of(seat) == g.host_end);
            host.add_client(seat, room.host.connection_of(seat));
            ClientSession::Config cc;
            cc.player = seat;
            clients[seat] = std::make_unique<ClientSession>(*sims[seat], cc);
            clients[seat]->set_connection(g.client_end);
        }
        uint32_t now = room.now;
        host.start(now);
        for (uint8_t p = 1; p < 4; ++p) clients[p]->start(now);
        Lcg rng(4);
        for (uint32_t end = now + 30000; now < end; now += 10) {
            room.net.set_time(now);
            if (now % 500 == 0) {
                for (uint8_t p = 0; p < 4; ++p) {
                    Command c;
                    c.type = CommandType::GroupMove;
                    c.issuer = p;
                    c.tile_x = static_cast<int16_t>(rng.below(60));
                    c.tile_y = static_cast<int16_t>(rng.below(60));
                    c.ants = {ants[p][rng.below(4)], ants[p][rng.below(4)]};
                    if (p == 0) host.submit_local(c);
                    else clients[p]->submit(c);
                }
            }
            host.update(now);
            for (uint8_t p = 1; p < 4; ++p) clients[p]->update(now);
        }
        host.freeze();
        for (uint32_t end = now + 3000; now < end; now += 10) {
            room.net.set_time(now);
            host.update(now);
            for (uint8_t p = 1; p < 4; ++p) clients[p]->update(now);
        }
        ASSERT_TRUE(host.desyncs().empty());
        ASSERT_TRUE(host.turns_sealed() > 500);                              // (turns of 50 ms: the 250 of 100 ms that this stood for, twice)
        for (int i = 1; i < 4; ++i) ASSERT_TRUE(sims[static_cast<size_t>(i)]->state_hash() == sims[0]->state_hash());
    } TEST_END();

    TEST_CASE("N4.10 The Leader Of A Server's Room: The First Guest To Be Welcomed Leads (Not The Lowest Seat), Everybody Is Told, The Earliest Of Those Left Leads When It Goes (Left, Kicked, Cut Off, Thrown Out), A Later Guest Never Takes The Lead; A Host With A Seat, A Room Without Early Start And A Bot Have None") {
        HostLobby::Config hc;
        hc.host_seat = 255;                                                         // kNoSeat: a dedicated server's room
        hc.min_players = 2;
        Room room(hc);
        ASSERT_EQ(room.host.leader(), kNoLeader);                                   // nobody is here yet
        // the host's view and the last Room message of every guest who is still in the room agree on the leader, and each says whether it is itself
        const auto leader_is = [&room](uint8_t expected) {
            room.run(300);
            if (room.host.leader() != expected) return false;
            for (auto& g : room.guests) {
                if (g.lobby->phase() != ClientLobby::Phase::InRoom) continue;       // (a guest who left, was cut off or thrown out hears nothing more)
                if (g.lobby->room().leader != expected || g.lobby->is_leader() != (g.lobby->my_seat() == expected)) return false;
            }
            return true;
        };
        room.join("Ann", {10, 0}, 3);                                               // the first to be welcomed, in the highest seat (not join_seat: the first Room message must be right
        const size_t ann = 0;                                                       // by itself, before the thumbs make the host send another one)
        for (int i = 0; i < 20 && room.guests[ann].lobby->room().slots[3].state == SlotState::Empty; ++i) room.run(10);
        ASSERT_EQ(room.guests[ann].lobby->room().slots[3].state, SlotState::Client);       // the first Room message that shows Ann in her seat ...
        ASSERT_EQ(room.guests[ann].lobby->room().leader, 3);                               // ... already names her
        ASSERT_TRUE(room.guests[ann].lobby->is_leader());                           // the Welcome and the first Room message are enough: it knows at once
        ASSERT_EQ(room.guests[ann].lobby->my_seat(), 3);
        ASSERT_TRUE(leader_is(3));
        const size_t bob = room.join_seat("Bob", 1);
        const size_t cat = room.join_seat("Cat", 0);                                // the lowest seat, the last to come
        ASSERT_EQ(room.guests[bob].lobby->my_seat(), 1);
        ASSERT_EQ(room.guests[cat].lobby->my_seat(), 0);
        ASSERT_TRUE(leader_is(3));                                                  // still Ann: the order of the Welcomes decides, not the number of the seat
        ASSERT_TRUE(room.guests[ann].lobby->is_leader() && !room.guests[bob].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
        room.guests[ann].lobby->leave();                                            // Ann goes: Bob is the earliest of those left (Cat has the lower seat, and does not lead)
        for (int i = 0; i < 20 && room.guests[bob].lobby->room().slots[3].state != SlotState::Empty; ++i) room.run(10);
        ASSERT_EQ(room.guests[bob].lobby->room().slots[3].state, SlotState::Empty);        // the first Room message that shows her seat empty ...
        ASSERT_EQ(room.guests[bob].lobby->room().leader, 1);                               // ... already names Bob
        ASSERT_TRUE(room.guests[bob].lobby->is_leader());
        ASSERT_TRUE(leader_is(1));
        const size_t dan = room.join_seat("Dan");
        ASSERT_EQ(room.guests[dan].lobby->my_seat(), 2);
        ASSERT_TRUE(leader_is(1));                                                  // a guest who comes now is the newest: Bob keeps the lead
        const size_t ann2 = room.join_seat("Ann again");                            // and Ann, back again, is the newest of all: no lead for her
        ASSERT_EQ(room.guests[ann2].lobby->my_seat(), 3);
        ASSERT_TRUE(leader_is(1));
        room.host.kick(1);                                                          // Bob is kicked: Cat is the earliest of Cat, Dan, Ann again
        ASSERT_TRUE(leader_is(0));
        ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Rejected);
        room.net.cut(room.guests[cat].client_end, true);                            // Cat's connection dies: Dan
        ASSERT_TRUE(leader_is(2));
        for (int i = 0; i < 10; ++i) room.guests[dan].client_end->send({250, 1, 2});    // Dan is thrown out for garbage: Ann, the one who is left
        ASSERT_TRUE(leader_is(3));
        ASSERT_FALSE(room.host.occupied(2));
        room.guests[ann2].lobby->leave();                                           // the last guest leaves: the room has no leader
        ASSERT_TRUE(leader_is(kNoLeader));
        ASSERT_EQ(room.host.players(), size_t{0});
        const size_t eve = room.join_seat("Eve");                                   // and the next one to come leads
        ASSERT_TRUE(leader_is(room.guests[eve].lobby->my_seat()) && room.guests[eve].lobby->is_leader());
        {   // the leader leaves while the map loads: the start is cancelled for everybody and the Room message that follows names the new leader
            Room r(hc);
            const size_t a = r.join_seat("A");
            const size_t b = r.join_seat("B");
            ASSERT_TRUE(r.host.start(1, 1, r.now));
            r.run(100);
            ASSERT_EQ(r.host.phase(), HostLobby::Phase::Loading);
            r.guests[a].lobby->leave();
            r.run(300);
            ASSERT_EQ(r.host.phase(), HostLobby::Phase::Room);
            ASSERT_EQ(r.host.leader(), r.guests[b].lobby->my_seat());
            ASSERT_TRUE(r.guests[b].lobby->is_leader() && r.guests[b].lobby->phase() == ClientLobby::Phase::InRoom);
            ASSERT_EQ(r.guests[b].lobby->cancel_reason(), CancelMsg::Reason::PlayerLeft);
        }
        {   // a host that holds a seat (a LAN / direct room) has no leader, whoever comes first
            Room lan;
            const size_t a = lan.join_seat("A");
            const size_t b = lan.join_seat("B");
            lan.run(300);
            ASSERT_EQ(lan.host.leader(), kNoLeader);
            ASSERT_TRUE(lan.guests[a].lobby->room().leader == kNoLeader && lan.guests[b].lobby->room().leader == kNoLeader);
            ASSERT_FALSE(lan.guests[a].lobby->is_leader() || lan.guests[b].lobby->is_leader());
            lan.host.kick(1);
            lan.run(200);
            ASSERT_EQ(lan.host.leader(), kNoLeader);
            ASSERT_EQ(lan.host.room().leader, kNoLeader);
        }
        {   // a room that does not allow an early start has no leader either (nobody would have a START that does anything)
            HostLobby::Config no = hc;
            no.early_start = false;
            Room r(no);
            const size_t a = r.join_seat("A");
            r.join_seat("B");
            r.run(200);
            ASSERT_EQ(r.host.leader(), kNoLeader);
            ASSERT_TRUE(r.guests[a].lobby->room().leader == kNoLeader && !r.guests[a].lobby->is_leader());
        }
        {   // a computer player is not a guest: it never leads
            Room r(hc);
            ASSERT_TRUE(r.host.add_bot(0, "Bot (Easy)"));
            ASSERT_EQ(r.host.leader(), kNoLeader);
            const size_t a = r.join_seat("Ann");
            ASSERT_EQ(r.guests[a].lobby->my_seat(), 1);
            ASSERT_EQ(r.host.leader(), 1);
            r.guests[a].lobby->leave();
            r.run(200);
            ASSERT_EQ(r.host.leader(), kNoLeader);                                  // the bot is still in seat 0, and leads nothing
            ASSERT_TRUE(r.host.occupied(0));
        }
    } TEST_END();

    TEST_CASE("N4.11 StartRequest In The Room: Only The Leader Is Heard, Only While The Room Can Start; Everything Else Is Ignored And Counted And Costs The Sender Nothing (Up To The 16 That A Person Could Send, N4.13 Has The Flood); A Fill Level Out Of Range Is Garbage (N4.17 Has The Fill); A Host With A Seat Ignores It") {
        const auto asked = [](HostLobby& host) {                                    // the LeaderStart events: the seats that asked
            std::vector<uint8_t> seats;
            for (const auto& e : host.take_events()) {
                if (e.type == HostLobby::Event::Type::LeaderStart) seats.push_back(e.seat);
            }
            return seats;
        };
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        {
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            asked(room.host);
            // one player: the leader's request is heard, and nothing happens
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            const size_t bob = room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            ASSERT_FALSE(room.guests[bob].lobby->request_start());                  // a guest knows it does not lead: nothing is sent
            room.run(100);
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) room.guests[bob].client_end->send(encode(StartRequestMsg{}));      // a client that is not the game's sends it anyway
            room.run(300);
            ASSERT_TRUE(asked(room.host).empty());                                  // never heard
            ASSERT_EQ(room.host.ignored_start_requests(), 17u);                     // but counted (Ann's one that found the room alone, and Bob's 16)
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom);       // and no offence: Bob is still here
            // two players: the leader's request is passed on, once, with the leader's seat; the lobby does not start by itself, its owner decides
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            const std::vector<uint8_t> heard = asked(room.host);
            ASSERT_TRUE(heard.size() == 1 && heard[0] == ann_seat);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_EQ(room.host.ignored_start_requests(), 17u);
            // the owner starts the match; the leader's second click arrives when the room is loading: ignored, counted, no offence
            ASSERT_TRUE(room.host.start(5, 5, room.now));
            room.run(100);
            for (int i = 0; i < 3; ++i) room.guests[ann].client_end->send(encode(StartRequestMsg{}));      // (a triple click)
            room.run(300);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 20u);
            ASSERT_TRUE(room.host.occupied(ann_seat) && room.host.phase() == HostLobby::Phase::Loading);
            // the cancelled start is back in the room, and the leader may ask again
            room.host.cancel();
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_EQ(asked(room.host).size(), size_t{1});
        }
        {   // a fill level out of range is garbage (a StartRequest is the type, a level 0 .. 3 for each of the four seats and the two team bytes: seven bytes), from the leader and from anybody: eight of them
            // throw the sender out (a violation like any message that a guest may not send); the bad level is in each of the four places in turn
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const auto bad_level = [](unsigned seat, unsigned level) {
                std::vector<uint8_t> m = {static_cast<uint8_t>(MsgType::StartRequest), 0, 0, 0, 0, 255, 255};
                m[1u + seat] = static_cast<uint8_t>(level);
                return m;
            };
            for (unsigned i = 0; i < 7; ++i) room.guests[ann].client_end->send(bad_level(i % 4, 4 + i));
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));    // seven are not enough
            room.guests[ann].client_end->send(bad_level(3, 9));
            room.run(300);
            ASSERT_FALSE(room.host.occupied(room.guests[ann].lobby->my_seat()));   // the eighth
            ASSERT_EQ(room.host.leader(), room.guests[bob].lobby->my_seat());      // the leader that was thrown out is replaced
            ASSERT_EQ(room.host.ignored_start_requests(), 0u);                     // (garbage is no request)
        }
        {   // teams that are no teams are garbage as well (protocol 13): one seat twice, one of the two bytes only, a seat that no room has; the old two-byte request too
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const std::vector<std::vector<uint8_t>> garbage = {
                {24, 0, 0, 0, 0, 1, 1}, {24, 0, 0, 0, 0, 255, 1}, {24, 0, 0, 0, 0, 2, 255}, {24, 0, 0, 0, 0, 4, 0}, {24, 0, 0, 0, 0, 0, 200}, {24, 0, 0, 0, 0}, {24, 1}};
            for (const auto& bad : garbage) room.guests[ann].client_end->send(bad);
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));    // seven garbage messages are a violation each, below the eight that throw a guest out
            room.guests[ann].client_end->send(garbage[0]);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(room.guests[ann].lobby->my_seat()));   // the eighth
            ASSERT_EQ(room.host.ignored_start_requests(), 0u);
        }
        {   // a room that wants more players than are in it: the leader's request is ignored; with a third player it is passed on
            HostLobby::Config three = hc;
            three.min_players = 3;
            Room room(three);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            asked(room.host);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            room.join_seat("Cat");
            asked(room.host);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_EQ(asked(room.host).size(), size_t{1});
        }
        {   // no map chosen yet: the room cannot start, the request is ignored; with a map it is passed on
            Room room(hc, false);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            asked(room.host);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            room.host.set_map("SMALL.LVL");
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_EQ(asked(room.host).size(), size_t{1});
        }
        {   // a room without early start: there is no leader, nobody is heard (the request goes through a modified client)
            HostLobby::Config no = hc;
            no.early_start = false;
            Room room(no);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            asked(room.host);
            ASSERT_FALSE(room.guests[ann].lobby->request_start());                  // nothing to send: Ann is no leader
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
        }
        {   // a host that holds a seat: a guest's StartRequest is ignored and counted, never an offence, and the room is not started
            Room lan;
            lan.join_seat("Ann");
            const size_t bob = lan.join_seat("Bob");
            asked(lan.host);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) lan.guests[bob].client_end->send(encode(StartRequestMsg{}));
            lan.run(300);
            ASSERT_TRUE(asked(lan.host).empty());
            ASSERT_EQ(lan.host.ignored_start_requests(), kIgnoredStartRequestsAllowed);
            ASSERT_TRUE(lan.host.occupied(lan.guests[bob].lobby->my_seat()) && lan.host.phase() == HostLobby::Phase::Room);
        }
    } TEST_END();

    TEST_CASE("N4.12 The Leader's Side: request_start() Sends The Type And The Fill Level (None Unless Asked For), And Only From The Leader's Open Room; A Guest, A Machine That Is Not In The Room Yet, One That Is Loading Or Gone Sends Nothing") {
        LoopbackNetwork net{3};
        auto ends = net.connect({5, 0});                                            // ends.first plays the server
        ClientLobby lobby(ends.second, ClientLobby::Config{});
        uint32_t now = 0;
        const auto step = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t += 5) {
                now += 5;
                net.set_time(now);
                lobby.update(now);
            }
        };
        const auto sent_requests = [&]() {                                          // what the server heard since the last call: the StartRequests
            std::vector<std::vector<uint8_t>> out;
            std::vector<uint8_t> m;
            while (ends.first->poll(m)) {
                if (peek_type(m) == MsgType::StartRequest) out.push_back(m);
            }
            return out;
        };
        ASSERT_FALSE(lobby.request_start());                                        // not connected yet
        step(20);                                                                   // the Hello goes out
        ASSERT_FALSE(lobby.request_start());                                        // not welcomed yet
        ends.first->send(encode(WelcomeMsg{2, 4}));
        RoomMsg r;
        r.slots[0] = {SlotState::Client, "Ann", 10};
        r.slots[2] = {SlotState::Client, "Me", 0};
        r.map_name = "TINY.LVL";
        r.you = 2;
        r.leader = 0;                                                               // somebody else leads
        ends.first->send(encode(r));
        step(40);
        ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
        ASSERT_EQ(lobby.my_seat(), 2);
        ASSERT_FALSE(lobby.is_leader());
        ASSERT_FALSE(lobby.request_start());
        step(20);
        ASSERT_TRUE(sent_requests().empty());                                       // nothing went out
        r.leader = 2;                                                               // the leader left: this machine leads now
        ends.first->send(encode(r));
        step(40);
        ASSERT_TRUE(lobby.is_leader());
        ASSERT_TRUE(lobby.request_start());
        step(20);
        const auto one = sent_requests();
        ASSERT_EQ(one.size(), size_t{1});
        ASSERT_TRUE(one[0] == (std::vector<uint8_t>{static_cast<uint8_t>(MsgType::StartRequest), 0, 0, 0, 0, 255, 255}));   // the type, no level in any seat, no teams (the START of protocol 7)
        r.leader = 0;                                                               // (leadership can move away only when this machine leaves, but the machine believes the room)
        ends.first->send(encode(r));
        step(40);
        ASSERT_FALSE(lobby.is_leader() || lobby.request_start());
        r.leader = 2;
        ends.first->send(encode(r));
        step(40);
        StartMsg st;
        st.map_name = "TINY.LVL";
        st.roster = 0x05;
        st.names[0] = "Ann";
        st.names[2] = "Me";
        ends.first->send(encode(st));
        step(40);
        ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Loading);                      // the room is loading: START is over
        ASSERT_FALSE(lobby.request_start());
        lobby.leave();
        ASSERT_FALSE(lobby.request_start());
        ASSERT_FALSE(lobby.is_leader());
    } TEST_END();

    TEST_CASE("N4.13 Flood Control In The Room: More Ignored StartRequests Than A Person Could Send (16) Cost A Guest Its Seat; Every Other Message Has A Budget Of 1000 A Second, So A Flood Of Pings Or Pongs Ends The Same Way; At Most 64 Messages Of A Guest Are Taken Per Update; Nobody Else Notices, And Honest Traffic Never Meets A Limit") {
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        const auto send_requests = [](Connection* end, uint32_t n) {
            for (uint32_t i = 0; i < n; ++i) end->send(encode(StartRequestMsg{}));
        };
        {   // a guest that does not lead: 16 ignored requests are free, each of the next seven is a violation, the 24th throws it out; the rest is never looked at
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            send_requests(room.guests[bob].client_end, kIgnoredStartRequestsAllowed);
            room.run(200);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            send_requests(room.guests[bob].client_end, 7);                          // violations one to seven
            room.run(200);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            ASSERT_EQ(room.host.ignored_start_requests(), 23u);
            send_requests(room.guests[bob].client_end, 100);                        // the first of them is the eighth violation: out; the other 99 are never read
            room.run(300);
            ASSERT_FALSE(room.host.occupied(bob_seat));
            ASSERT_EQ(room.host.ignored_start_requests(), 24u);
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob].lobby->reject_reason() == RejectReason::BadRequest);
            // nobody else notices: Ann still leads, the room is open and the seat is free for the next player
            ASSERT_TRUE(room.host.occupied(ann_seat) && room.host.leader() == ann_seat && room.host.phase() == HostLobby::Phase::Room);
            const size_t cat = room.join_seat("Cat");
            ASSERT_EQ(room.guests[cat].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.host.players(), 2u);
        }
        {   // the leader's own clicks that arrive after the Start: the same count; a leader who is thrown out cancels the loading like any leaver, and the next one leads
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.start(5, 5, room.now));
            room.run(100);
            send_requests(room.guests[ann].client_end, 200);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_EQ(room.host.ignored_start_requests(), 24u);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_EQ(room.host.leader(), bob_seat);
        }
        {   // a host that holds a seat (a LAN game): a guest's StartRequests are ignored, with the same allowance
            Room lan;
            lan.join_seat("Ann");
            const size_t bob = lan.join_seat("Bob");
            const uint8_t bob_seat = lan.guests[bob].lobby->my_seat();
            send_requests(lan.guests[bob].client_end, 200);
            lan.run(300);
            ASSERT_FALSE(lan.host.occupied(bob_seat));
            ASSERT_EQ(lan.host.ignored_start_requests(), 24u);
            ASSERT_TRUE(lan.host.occupied(lan.guests[0].lobby->my_seat()) && lan.host.phase() == HostLobby::Phase::Room);
        }
        {   // the allowance belongs to a connection, not to the room: two guests send 16 each (the room wants three players, so even the leader's request is ignored), both stay
            HostLobby::Config three = hc;
            three.min_players = 3;
            Room room(three);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            send_requests(room.guests[ann].client_end, kIgnoredStartRequestsAllowed);
            send_requests(room.guests[bob].client_end, kIgnoredStartRequestsAllowed);
            room.run(300);
            ASSERT_EQ(room.host.ignored_start_requests(), 2 * kIgnoredStartRequestsAllowed);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()) && room.host.occupied(room.guests[bob].lobby->my_seat()));
            send_requests(room.guests[ann].client_end, 1);                          // one more is a violation for Ann: one of eight, she stays
            room.run(100);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
        }
        {   // a flood of valid pings from a raw guest: a second's worth is answered, then it is out (the 3000 were not all answered)
            Room room(hc);
            room.join_seat("Ann");
            auto ends = room.net.connect({10, 0});
            room.host.add_connection(ends.first, room.now);
            HelloMsg hello;
            hello.name = "Raw";
            ends.second->send(encode(hello));
            room.run(100);
            std::vector<uint8_t> m;
            uint8_t raw_seat = 255;
            while (ends.second->poll(m)) {
                WelcomeMsg w;
                if (peek_type(m) == MsgType::Welcome && decode(m, w)) raw_seat = w.player;
            }
            ASSERT_TRUE(raw_seat < sim::MAX_PLAYERS && room.host.occupied(raw_seat));
            for (uint32_t i = 1; i <= 3000; ++i) ends.second->send(encode_ping(PingMsg{i, 0}));
            room.run(1000);
            ASSERT_FALSE(room.host.occupied(raw_seat));
            uint32_t pongs = 0;
            while (ends.second->poll(m)) pongs += peek_type(m) == MsgType::Pong ? 1u : 0u;
            ASSERT_TRUE(pongs >= kMessageBurst && pongs <= kMessageBurst + 300);   // the burst, and what the bucket gave back while the host took 64 messages per update
        }
        {   // a flood of valid pongs (the host believes none of them, so they are no offence by themselves): the budget ends it too
            Room room(hc);
            room.join_seat("Ann");
            auto ends = room.net.connect({10, 0});
            room.host.add_connection(ends.first, room.now);
            HelloMsg hello;
            hello.name = "Raw";
            ends.second->send(encode(hello));
            room.run(100);
            std::vector<uint8_t> m;
            uint8_t raw_seat = 255;
            while (ends.second->poll(m)) {
                WelcomeMsg w;
                if (peek_type(m) == MsgType::Welcome && decode(m, w)) raw_seat = w.player;
            }
            ASSERT_TRUE(raw_seat < sim::MAX_PLAYERS && room.host.occupied(raw_seat));
            for (int i = 0; i < 3000; ++i) ends.second->send(encode_pong(PingMsg{1, 0}));
            room.run(1000);
            ASSERT_FALSE(room.host.occupied(raw_seat));
        }
        {   // at most 64 messages of a guest are taken per update: the rest of a flood waits (and does not cost the host more in one update than in the next)
            LoopbackNetwork net{9};
            auto ends = net.connect({0, 0});
            CountingConnection counted(ends.first);
            HostLobby host(hc);
            host.set_map("TINY.LVL");
            host.add_connection(&counted, 0);
            HelloMsg hello;
            hello.name = "Raw";
            ends.second->send(encode(hello));
            net.set_time(10);
            host.update(10);
            ASSERT_EQ(host.players(), 1u);
            for (uint32_t i = 1; i <= 500; ++i) ends.second->send(encode_ping(PingMsg{i, 0}));
            net.set_time(20);
            counted.taken = 0;
            host.update(20);
            ASSERT_EQ(counted.taken, 64u);
            host.update(21);
            ASSERT_EQ(counted.taken, 128u);
            host.update(22);
            ASSERT_EQ(counted.taken, 192u);
        }
        {   // honest traffic never meets a limit: ten pings a second for a minute and a burst of 500 at once; 900 a second for twenty seconds (under the refill of 1000)
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            for (int second = 0; second < 60; ++second) {
                for (int i = 0; i < 10; ++i) room.guests[bob].client_end->send(encode_ping(PingMsg{static_cast<uint32_t>(second * 10 + i + 1), 0}));
                if (second == 30) {
                    for (uint32_t i = 0; i < 500; ++i) room.guests[bob].client_end->send(encode_ping(PingMsg{5000 + i, 0}));
                }
                room.run(1000);
            }
            ASSERT_TRUE(room.host.occupied(room.guests[bob].lobby->my_seat()) && room.host.occupied(room.guests[ann].lobby->my_seat()));
            for (int step = 0; step < 2000; ++step) {
                for (uint32_t i = 0; i < 9; ++i) room.guests[bob].client_end->send(encode_ping(PingMsg{100000u + static_cast<uint32_t>(step) * 9u + i, 0}));
                room.run(10);
            }
            ASSERT_TRUE(room.host.occupied(room.guests[bob].lobby->my_seat()));
        }
        {   // 1500 a second is above it: the bucket empties in about two seconds (it gives back 1000 and takes 1500) and the sender is out
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const uint32_t start = room.now;
            int steps = 0;
            for (; steps < 1000 && room.host.occupied(bob_seat); ++steps) {
                for (uint32_t i = 0; i < 15; ++i) room.guests[bob].client_end->send(encode_ping(PingMsg{static_cast<uint32_t>(steps) * 15u + i + 1u, 0}));
                room.run(10);
            }
            ASSERT_FALSE(room.host.occupied(bob_seat));
            ASSERT_TRUE(room.now - start >= 1500 && room.now - start <= 3500);
        }
    } TEST_END();

    TEST_CASE("N4.14 Keys (Protocol 10): A Host With A Key Maker Gives Every Guest Its Own Key, Never Zero, In Its Welcome; A Host Without One Gives Nobody A Key; A Maker That Fails Or Cannot Tell Its Keys Apart Gives No Key, And Two Seats Never Share One") {
        // ---- with a maker: four guests, four different keys; the Welcome carries the one that the host holds for the seat ----
        {
            Room room(keyed_server_config(1));
            std::vector<size_t> guests;
            for (const char* name : {"Ann", "Bob", "Cat", "Dan"}) guests.push_back(room.join_seat(name));
            room.run(200);
            std::vector<SeatKey> keys;
            for (const size_t i : guests) {
                const SeatKey& k = room.guests[i].lobby->key();
                ASSERT_FALSE(key_is_zero(k));
                ASSERT_TRUE(key_matches(k, room.host.key_of(room.guests[i].lobby->my_seat())));
                ASSERT_FALSE(room.guests[i].lobby->rejoined());                           // (a Welcome of the room: no rejoin flag)
                for (const SeatKey& other : keys) ASSERT_FALSE(key_matches(k, other));
                keys.push_back(k);
            }
            ASSERT_TRUE(key_is_zero(room.host.key_of(4)) && key_is_zero(room.host.key_of(255)));      // seats that are no seats have none
            ASSERT_EQ(room.host.players(), size_t{4});
            room.guests[guests[1]].lobby->leave();                                         // a guest that leaves takes its key with it: the seat has none until the next guest comes
            room.run(200);
            ASSERT_TRUE(key_is_zero(room.host.key_of(1)));
            const size_t eve = room.join_seat("Eve");
            ASSERT_EQ(room.guests[eve].lobby->my_seat(), 1);
            const SeatKey eve_key = room.guests[eve].lobby->key();
            ASSERT_FALSE(key_is_zero(eve_key));
            for (const SeatKey& other : keys) ASSERT_FALSE(key_matches(eve_key, other));  // a new key: not the one of the guest who left, nor of any other
            ASSERT_TRUE(key_matches(room.host.key_of(1), eve_key));
        }
        // ---- without a maker: a server's room and a LAN host alike give no key, and the Welcome says so ----
        for (const bool server : {true, false}) {
            HostLobby::Config hc;
            if (server) hc.host_seat = 255;
            Room room(hc);
            const size_t a = room.join_seat("A");
            const size_t b = room.join_seat("B");
            ASSERT_TRUE(key_is_zero(room.guests[a].lobby->key()) && key_is_zero(room.guests[b].lobby->key()));
            for (uint8_t s = 0; s < 4; ++s) ASSERT_TRUE(key_is_zero(room.host.key_of(s)));
            ASSERT_EQ(room.guests[a].lobby->phase(), ClientLobby::Phase::InRoom);
        }
        {   // a host that holds a seat and has a maker: its own seat has no key, the guests have
            HostLobby::Config hc = keyed_server_config(2);
            hc.host_seat = 0;
            Room room(hc);
            const size_t a = room.join_seat("A");
            ASSERT_TRUE(key_is_zero(room.host.key_of(0)));
            ASSERT_FALSE(key_is_zero(room.guests[a].lobby->key()));
        }
        // ---- a maker that fails: no key for the guest, but the seat; it is asked four times and no more ----
        {
            HostLobby::Config hc;
            hc.host_seat = 255;
            int calls = 0;
            hc.make_key = [&calls](SeatKey&) { ++calls; return false; };
            Room room(hc);
            const size_t a = room.join_seat("A");
            ASSERT_EQ(room.guests[a].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.guests[a].lobby->my_seat(), 0);
            ASSERT_TRUE(key_is_zero(room.guests[a].lobby->key()) && key_is_zero(room.host.key_of(0)));
            ASSERT_EQ(calls, 4);
        }
        {   // one that fails three times and then works gives a key (it is asked again); one that fails four times for a guest gives that guest none and the next guest a key
            HostLobby::Config hc;
            hc.host_seat = 255;
            int calls = 0;
            hc.make_key = [&calls](SeatKey& k) {
                ++calls;
                if (calls <= 3 || (calls >= 5 && calls <= 8)) return false;
                for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(calls * 16 + static_cast<int>(i) + 1);
                return true;
            };
            Room room(hc);
            const size_t a = room.join_seat("A");                                          // calls 1 - 3 fail, call 4 works
            const size_t b = room.join_seat("B");                                          // calls 5 - 8 fail: no key
            const size_t c = room.join_seat("C");                                          // call 9 works
            ASSERT_FALSE(key_is_zero(room.guests[a].lobby->key()));
            ASSERT_TRUE(key_is_zero(room.guests[b].lobby->key()));
            ASSERT_FALSE(key_is_zero(room.guests[c].lobby->key()));
            ASSERT_FALSE(key_matches(room.guests[a].lobby->key(), room.guests[c].lobby->key()));
            ASSERT_EQ(calls, 9);
        }
        // ---- a maker that cannot tell its keys apart: the same key every time, and the key zero ----
        {
            HostLobby::Config hc;
            hc.host_seat = 255;
            hc.make_key = [](SeatKey& k) {
                k.fill(0x42);
                return true;
            };
            Room room(hc);
            const size_t a = room.join_seat("A");
            const size_t b = room.join_seat("B");
            const size_t c = room.join_seat("C");
            ASSERT_FALSE(key_is_zero(room.guests[a].lobby->key()));                        // the first guest has it (nobody else had it then)
            ASSERT_TRUE(key_is_zero(room.guests[b].lobby->key()) && key_is_zero(room.guests[c].lobby->key()));      // the others get none: a key is never shared
            ASSERT_EQ(room.guests[a].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.guests[b].lobby->phase(), ClientLobby::Phase::InRoom);          // (and still have their seats)
            HostLobby::Config zero = hc;
            zero.make_key = [](SeatKey& k) {
                k.fill(0);
                return true;
            };
            Room room2(zero);
            const size_t z = room2.join_seat("Z");
            ASSERT_TRUE(key_is_zero(room2.guests[z].lobby->key()) && key_is_zero(room2.host.key_of(0)));
        }
        {   // one that gives the zero key first and a good key at its second try: the zero key is no key, so the guest is given the good one
            HostLobby::Config hc;
            hc.host_seat = 255;
            int calls = 0;
            hc.make_key = [&calls](SeatKey& k) {
                ++calls;
                k.fill(calls == 1 ? uint8_t{0} : uint8_t{9});
                return true;
            };
            Room room(hc);
            const size_t a = room.join_seat("A");
            ASSERT_TRUE(room.guests[a].lobby->key()[0] == 9 && calls == 2);
        }
        {   // one that repeats a key once and then gives a new one: the second guest gets the new one
            HostLobby::Config hc;
            hc.host_seat = 255;
            int calls = 0;
            hc.make_key = [&calls](SeatKey& k) {
                ++calls;
                k.fill(calls == 1 || calls == 2 ? uint8_t{7} : static_cast<uint8_t>(calls));
                return true;
            };
            Room room(hc);
            const size_t a = room.join_seat("A");                                          // call 1: 7
            const size_t b = room.join_seat("B");                                          // call 2: 7 again, refused; call 3: 3
            ASSERT_TRUE(room.guests[a].lobby->key()[0] == 7 && room.guests[b].lobby->key()[0] == 3);
            ASSERT_EQ(calls, 3);
        }
    } TEST_END();

    TEST_CASE("N4.15 Taking A Seat Back In The Waiting Room (Protocol 10): A Hello With The Key Of A Seated Guest Takes Over Its Seat (The Same Seat, Key, Name And Place In The Order Of The Welcomes, So The Leader Stays The Leader); The Old Connection Is Told Superseded And Closed; The Thumb Is Measured Again; A Wrong Key Is A New Player; In A Match That Loads Or Runs A Key Is 'Match Running'") {
        // ---- the takeover itself ----
        {
            Room room(keyed_server_config(3));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            ASSERT_TRUE(room.host.measured(0) && room.host.measured(1) && room.host.measured(2));
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 1);
            const SeatKey bob_key = room.guests[bob].lobby->key();
            room.host.take_events();                                                        // (the joins)
            // Bob's page is reloaded: a new connection says Hello with Bob's key, a new name, and asks for another seat
            const size_t again = join_keyed(room, "Bob again", bob_key, 3);
            room.run(20);                                                                   // two steps: the client says its Hello, the host handles it (the first answer to its ping is not back yet)
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_FALSE(room.host.measured(1));                                            // the thumb is measured again
            ASSERT_TRUE(key_matches(room.host.key_of(1), bob_key));                         // the same key
            ASSERT_EQ(room.host.players(), size_t{3});
            ASSERT_FALSE(room.host.occupied(3));                                            // (the seat it asked for was not given)
            ASSERT_EQ(room.host.room().slots[1].name, "Bob");                               // the seat keeps its name
            ASSERT_EQ(room.host.leader(), 0);
            ASSERT_TRUE(room.host.take_events().empty());                                   // nobody joined, nobody left
            room.run(400);
            ASSERT_TRUE(room.host.measured(1));
            ASSERT_EQ(room.guests[again].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.guests[again].lobby->my_seat(), 1);
            ASSERT_TRUE(key_matches(room.guests[again].lobby->key(), bob_key));
            ASSERT_FALSE(room.guests[again].lobby->rejoined());                             // (the room follows: this is no rejoin of a running match)
            ASSERT_EQ(room.guests[again].lobby->room().slots[1].name, "Bob");
            ASSERT_EQ(room.guests[again].lobby->room().you, 1);
            ASSERT_EQ(room.guests[ann].lobby->room().slots[1].state, SlotState::Client);    // the others see the same room
            ASSERT_EQ(room.guests[cat].lobby->room().slots[1].name, "Bob");
            ASSERT_EQ(room.guests[cat].lobby->room().slots[3].state, SlotState::Empty);
            // the old connection was told, and closed
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[bob].lobby->reject_reason(), RejectReason::Superseded);
            ASSERT_FALSE(room.guests[bob].client_end->is_open());
            ASSERT_TRUE(room.host.connection_of(1) == room.guests[again].host_end);
            ASSERT_TRUE(room.host.take_events().empty());
        }
        // ---- what the new connection is told: the Welcome of the seat, with its key and without the rejoin flag, then the room ----
        {
            Room room(keyed_server_config(4));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            HelloMsg h;
            h.name = "Whoever";
            h.key = bob_key;
            const RawClient raw = raw_hello(room, h);
            room.run(20);
            const std::vector<std::vector<uint8_t>> got = drain_messages(raw.end);
            ASSERT_TRUE(got.size() >= 2);
            WelcomeMsg w;
            ASSERT_TRUE(decode(got[0], w) && w.player == 1 && w.players == 4 && key_matches(w.key, bob_key) && w.flags == 0);
            RoomMsg r;
            ASSERT_TRUE(decode(got[1], r) && r.you == 1 && r.slots[1].name == "Bob" && r.slots[1].state == SlotState::Client && r.leader == 0);
            ASSERT_EQ(r.slots[1].rtt_ms, kRttUnknown);                                      // its thumb starts again
        }
        // ---- the leader keeps the lead, and every guest its place in the order of the Welcomes ----
        {
            Room room(keyed_server_config(5));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(200);
            ASSERT_EQ(room.host.leader(), 0);
            const SeatKey ann_key = room.guests[ann].lobby->key();
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const size_t ann2 = join_keyed(room, "Ann", ann_key);                          // the leader's page is reloaded
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(room.host.leader(), 0);                                               // still the leader
            ASSERT_TRUE(room.guests[ann2].lobby->is_leader() && room.guests[ann2].lobby->my_seat() == 0);
            ASSERT_TRUE(room.guests[bob].lobby->room().leader == 0 && room.guests[cat].lobby->room().leader == 0);
            const size_t bob2 = join_keyed(room, "Bob", bob_key);                          // Bob's too: it is not the newest guest for it
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 2u);
            const size_t dan = room.join_seat("Dan");                                       // a guest that comes now is the newest
            ASSERT_EQ(room.host.leader(), 0);
            room.guests[ann2].lobby->leave();                                               // the leader goes: Bob leads, not Cat (Bob's place in the order is what it was)
            room.run(300);
            ASSERT_EQ(room.host.leader(), 1);
            ASSERT_TRUE(room.guests[bob2].lobby->is_leader());
            ASSERT_FALSE(room.guests[cat].lobby->is_leader() || room.guests[dan].lobby->is_leader());
            // the contrast: a guest that leaves and comes back WITHOUT its key is the newest
            Room other(keyed_server_config(6));
            const size_t a = other.join_seat("Ann");
            const size_t b = other.join_seat("Bob");
            other.guests[a].lobby->leave();
            other.run(200);
            const size_t a2 = other.join_seat("Ann");
            ASSERT_EQ(other.host.leader(), other.guests[b].lobby->my_seat());
            ASSERT_FALSE(other.guests[a2].lobby->is_leader());
        }
        // ---- a key that fits no seat is a new player; a wrong key does not get past a full room ----
        {
            Room room(keyed_server_config(7));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const SeatKey wrong = key_with(99);
            const size_t newcomer = join_keyed(room, "New", wrong);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 0u);
            ASSERT_EQ(room.guests[newcomer].lobby->my_seat(), 2);                           // a seat of its own
            ASSERT_FALSE(key_is_zero(room.guests[newcomer].lobby->key()));
            ASSERT_FALSE(key_matches(room.guests[newcomer].lobby->key(), wrong));          // it is given a key of its own: the one that it showed is nothing
            ASSERT_EQ(room.host.players(), size_t{3});
            // a seat that was left: the key of its guest is dead
            room.guests[bob].lobby->leave();
            room.run(200);
            ASSERT_FALSE(room.host.occupied(1));
            const size_t late = join_keyed(room, "Bob late", bob_key);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 0u);
            ASSERT_EQ(room.guests[late].lobby->my_seat(), 1);                               // (the first free seat, as for anybody)
            ASSERT_FALSE(key_matches(room.guests[late].lobby->key(), bob_key));
            // a full room refuses a wrong key as it refuses anybody
            const size_t fourth = room.join_seat("Dan");
            ASSERT_EQ(room.host.players(), size_t{4});
            const size_t fifth = join_keyed(room, "Eve", key_with(98));
            room.run(300);
            ASSERT_EQ(room.guests[fifth].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[fifth].lobby->reject_reason(), RejectReason::Full);
            ASSERT_TRUE(room.guests[fourth].lobby->phase() == ClientLobby::Phase::InRoom && room.host.takeovers() == 0);
            // but the key of a seated guest gets in whatever the number of players: it is no new player
            const size_t again = join_keyed(room, "New again", room.guests[newcomer].lobby->key());
            room.run(300);
            ASSERT_EQ(room.guests[again].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.guests[again].lobby->my_seat(), 2);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(room.host.players(), size_t{4});
        }
        {   // a host without keys ignores the key of a Hello (a LAN host): the Hello is the Hello of a new player
            for (const bool server : {true, false}) {
                HostLobby::Config hc;
                if (server) hc.host_seat = 255;
                Room room(hc);
                room.join_seat("Ann");
                const size_t guest = join_keyed(room, "Guest", key_with(5));
                room.run(300);
                ASSERT_EQ(room.guests[guest].lobby->phase(), ClientLobby::Phase::InRoom);
                ASSERT_EQ(room.host.takeovers(), 0u);
                ASSERT_TRUE(key_is_zero(room.guests[guest].lobby->key()));
                ASSERT_EQ(room.host.players(), server ? size_t{2} : size_t{3});
            }
        }
        {   // the zero key takes no seat: a guest whose maker failed has the zero key, and a Hello with the zero key is a new player, not that guest
            HostLobby::Config hc;
            hc.host_seat = 255;
            int calls = 0;
            hc.make_key = [&calls](SeatKey&) { ++calls; return false; };
            Room room(hc);
            const size_t a = room.join_seat("A");
            const size_t b = join_keyed(room, "B", SeatKey{});
            room.run(300);
            ASSERT_TRUE(room.guests[a].lobby->my_seat() == 0 && room.guests[b].lobby->my_seat() == 1);
            ASSERT_EQ(room.host.takeovers(), 0u);
            ASSERT_EQ(room.host.players(), size_t{2});
        }
        // ---- two connections with one key in one pass: the second takes the seat from the first; nobody else is touched ----
        {
            Room room(keyed_server_config(8));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(300);
            HelloMsg h;
            h.name = "Twin";
            h.key = room.guests[bob].lobby->key();
            const RawClient first = raw_hello(room, h);
            const RawClient second = raw_hello(room, h);
            room.run(20);
            ASSERT_EQ(room.host.takeovers(), 2u);
            ASSERT_EQ(room.host.players(), size_t{3});
            ASSERT_TRUE(room.host.connection_of(1) == second.host_end);
            const std::vector<std::vector<uint8_t>> from_first = drain_messages(first.end);
            bool superseded = false;
            for (const auto& m : from_first) {
                RejectMsg rj;
                if (decode(m, rj) && rj.reason == RejectReason::Superseded) superseded = true;
            }
            ASSERT_TRUE(superseded);                                                        // the first twin was told that the second took the seat ...
            ASSERT_FALSE(first.end->is_open());                                             // ... and its connection is closed (once what it was told is read)
            ASSERT_TRUE(second.end->is_open());
            ASSERT_EQ(room.guests[bob].lobby->reject_reason(), RejectReason::Superseded);
            ASSERT_TRUE(key_matches(room.host.key_of(1), h.key));
        }
        // ---- the room code is checked before the key: a Hello for another room with the right key takes nothing ----
        {
            Room room(keyed_server_config(9, "ROOM-7"));
            const size_t ann = join_keyed(room, "Ann", SeatKey{}, 255, "ROOM-7");
            const size_t bob = join_keyed(room, "Bob", SeatKey{}, 255, "ROOM-7");
            room.run(300);
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::InRoom);
            HelloMsg h;
            h.name = "Other";
            h.room = "OTHER";
            h.key = room.guests[bob].lobby->key();
            const RawClient raw = raw_hello(room, h);
            room.run(50);
            RejectMsg rj;
            std::vector<uint8_t> m;
            ASSERT_TRUE(raw.end->poll(m) && decode(m, rj) && rj.reason == RejectReason::NoSuchRoom);
            ASSERT_EQ(room.host.takeovers(), 0u);
            ASSERT_TRUE(room.host.connection_of(1) == room.guests[bob].host_end && room.guests[bob].client_end->is_open());
            ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::InRoom);
        }
        // ---- the old link is dead in the very pass in which the new Hello comes: the Hello is read first, the seat is held ----
        {
            Room room(keyed_server_config(10));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            room.host.take_events();
            HelloMsg h;
            h.name = "Bob";
            h.key = room.guests[bob].lobby->key();
            const RawClient raw = raw_hello(room, h);
            room.net.cut(room.guests[bob].client_end, true);                                // the old link fails now; the Hello is read by the next update, with the old link closed
            room.run(10);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_TRUE(room.host.occupied(1) && room.host.take_events().empty());          // nobody left
            ASSERT_TRUE(room.host.connection_of(1) == raw.host_end && raw.end->is_open());
            room.run(300);
            ASSERT_TRUE(room.host.occupied(1) && room.host.takeovers() == 1u);
            // when the old link was seen to be closed BEFORE the Hello comes, the seat was freed and its key is gone with it: the Hello is the Hello of a new player (the key of a seat that
            // was left is dead: nothing is kept for it)
            const size_t carl = room.join_seat("Carl");
            room.net.cut(room.guests[carl].client_end, true);
            room.run(100);
            const SeatKey carl_key = room.guests[carl].lobby->key();
            ASSERT_FALSE(room.host.occupied(room.guests[carl].lobby->my_seat()));
            const size_t carl2 = join_keyed(room, "Carl", carl_key);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_FALSE(key_matches(room.guests[carl2].lobby->key(), carl_key));
        }
        // ---- what a guest has done follows its seat: seven violations and a takeover, and one more is the eighth ----
        {
            Room room(keyed_server_config(11));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            for (int i = 0; i < 7; ++i) room.guests[bob].client_end->send({250, 1, 2});
            room.run(100);
            ASSERT_TRUE(room.host.occupied(1));                                             // seven are not enough
            const size_t again = join_keyed(room, "Bob", room.guests[bob].lobby->key());
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_TRUE(room.host.occupied(1));
            room.guests[again].client_end->send({250, 1, 2});                               // one more, from the new connection
            room.run(100);
            ASSERT_FALSE(room.host.occupied(1));                                            // is the eighth: coming back does not start the count again
            ASSERT_EQ(room.guests[again].lobby->phase(), ClientLobby::Phase::Rejected);
        }
        // ---- in a match that loads or runs, a key is the session's business: the lobby says 'match running' (and the seat of the key is not touched) ----
        {
            Room room(keyed_server_config(12));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const SeatKey bob_key = room.guests[bob].lobby->key();
            ASSERT_TRUE(room.host.start(1, 1, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            auto hello_with_key = [&](const char* name) {
                auto ends = room.net.connect({10, 0});
                HelloMsg h;
                h.name = name;
                h.key = bob_key;
                room.host.add_connection(ends.first, room.now, std::string(), encode(h));     // (the door of a server: it read the Hello to find the room, then hands both over)
                room.run(30);
                std::vector<uint8_t> m;
                RejectMsg rj;
                if (!ends.second->poll(m)) return std::string("silent");
                if (decode(m, rj)) return std::string("rejected ") + std::to_string(static_cast<int>(rj.reason));
                return std::string("other");
            };
            const std::string running = std::string("rejected ") + std::to_string(static_cast<int>(RejectReason::MatchRunning));
            ASSERT_EQ(hello_with_key("Loading"), running);
            ASSERT_TRUE(room.host.connection_of(1) == room.guests[bob].host_end && room.guests[bob].client_end->is_open() && room.host.takeovers() == 0);
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::Loading && room.guests[bob].lobby->phase() == ClientLobby::Phase::Loading);
            for (auto& g : room.guests) g.lobby->report_loaded(true);
            room.host.host_loaded(true);
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            ASSERT_EQ(hello_with_key("Begun"), running);
            ASSERT_TRUE(room.host.connection_of(1) == room.guests[bob].host_end && room.host.takeovers() == 0);
            ASSERT_TRUE(key_matches(room.host.key_of(1), bob_key));                         // the keys stay with the seats: the session of the match is handed them from here
        }
    } TEST_END();

    TEST_CASE("N4.16 The Rejoin Flow Of A Machine That Starts From Nothing, Through ClientLobby (Protocol 10): The Hello Carries The Key And No Turns; Welcome With The Rejoin Flag, Start, Loaded, Begin, Begun; The New Rejections Are Told") {
        const SeatKey key = key_with(21);
        // ---- the flow, against a server that is played by the test ----
        {
            LoopbackNetwork net(5);
            auto ends = net.connect({10, 0});
            Connection* server = ends.first;
            ClientLobby::Config cc;
            cc.name = "Bob";
            cc.room = "ROOM-7";
            cc.token = "tok";
            cc.want_seat = 2;
            cc.key = key;
            ClientLobby lobby(ends.second, cc);
            uint32_t now = 0;
            auto step = [&](uint32_t ms) {
                const uint32_t end = now + ms;
                while (now < end) {
                    now += 10;
                    net.set_time(now);
                    lobby.update(now);
                }
            };
            step(30);
            std::vector<uint8_t> m;
            HelloMsg h;
            ASSERT_TRUE(server->poll(m) && decode(m, h));                                    // the Hello shows the key, and has no turns: this machine starts from nothing
            ASSERT_TRUE(h.version == kProtocolVersion && h.name == "Bob" && h.room == "ROOM-7" && h.token == "tok" && h.want_seat == 2);
            ASSERT_TRUE(key_matches(h.key, key) && h.have_turns == 0);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Joining);
            ASSERT_FALSE(lobby.rejoined());
            WelcomeMsg w;
            w.player = 2;
            w.players = 4;
            w.key = key;
            w.flags = kWelcomeRejoin;
            server->send(encode(w));
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(lobby.my_seat(), 2);
            ASSERT_TRUE(lobby.rejoined() && key_matches(lobby.key(), key));
            ASSERT_TRUE(lobby.take_events().empty());                                        // (no Room message comes for a rejoiner: nothing changed in a room)
            StartMsg s;
            s.seed = 777;
            s.map_name = "TINY.LVL";
            s.map_hash = 0x1234567890ABCDEFull;
            s.roster = 0x07;
            s.names[0] = "Ann";
            s.names[1] = "Cat";
            s.names[2] = "Bob";
            server->send(encode(s));
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Loading);
            const auto events = lobby.take_events();
            ASSERT_TRUE(events.size() == 1 && events[0].type == ClientLobby::Event::Type::StartRequested);
            ASSERT_TRUE(lobby.start_info().seed == 777 && lobby.start_info().map_name == "TINY.LVL" && lobby.start_info().roster == 0x07 && lobby.start_info().names[2] == "Bob");
            lobby.report_loaded(true);
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Loaded);
            bool loaded_told = false;
            while (server->poll(m)) {
                LoadedMsg l;
                if (decode(m, l) && l.ok) loaded_told = true;
            }
            ASSERT_TRUE(loaded_told);
            server->send(encode_begin());
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Begun);
            const auto begun = lobby.take_events();
            ASSERT_TRUE(begun.size() == 1 && begun[0].type == ClientLobby::Event::Type::Begun);
            ASSERT_TRUE(lobby.rejoined() && lobby.my_seat() == 2 && key_matches(lobby.key(), key));      // (it keeps what it was told, for the NetGame that goes on from here)
        }
        // ---- a client with no key says so; the Welcome of a room (no rejoin) and of a host without keys ----
        {
            LoopbackNetwork net(6);
            auto ends = net.connect({10, 0});
            ClientLobby::Config cc;
            cc.name = "New";
            ClientLobby lobby(ends.second, cc);
            net.set_time(10);
            lobby.update(10);
            net.set_time(30);
            std::vector<uint8_t> m;
            HelloMsg h;
            ASSERT_TRUE(ends.first->poll(m) && decode(m, h) && key_is_zero(h.key) && h.have_turns == 0);
            WelcomeMsg w;
            w.player = 1;
            w.players = 4;
            ends.first->send(encode(w));                                                     // a host without keys: the zero key, no flags
            net.set_time(60);
            lobby.update(60);
            ASSERT_TRUE(lobby.phase() == ClientLobby::Phase::InRoom && lobby.my_seat() == 1 && key_is_zero(lobby.key()) && !lobby.rejoined());
        }
        // ---- the rejections that protocol 10 adds end the join, with their reason ----
        for (const RejectReason reason : {RejectReason::Dropped, RejectReason::RejoinFailed, RejectReason::Superseded, RejectReason::MatchRunning, RejectReason::NoSuchRoom}) {
            LoopbackNetwork net(7);
            auto ends = net.connect({10, 0});
            ClientLobby::Config cc;
            cc.key = key;
            ClientLobby lobby(ends.second, cc);
            net.set_time(10);
            lobby.update(10);
            ends.first->send(encode(RejectMsg{reason}));
            net.set_time(40);
            lobby.update(40);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(lobby.reject_reason(), reason);
            const auto events = lobby.take_events();
            ASSERT_TRUE(events.size() == 1 && events[0].type == ClientLobby::Event::Type::Rejected);
        }
        // ---- through a real host lobby: a client that shows its key in the waiting room gets the seat back and the room follows ----
        {
            Room room(keyed_server_config(13));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const size_t again = join_keyed(room, "Bob", bob_key);
            room.run(300);
            ClientLobby& lobby = *room.guests[again].lobby;
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
            ASSERT_TRUE(lobby.my_seat() == 1 && key_matches(lobby.key(), bob_key) && !lobby.rejoined());
            ASSERT_EQ(lobby.room().slots[0].name, "Ann");                                    // the room follows: the roster is there
            ASSERT_EQ(lobby.room().you, 1);
            // and the match starts with it as with any guest
            ASSERT_TRUE(room.host.start(1, 1, room.now));
            room.run(100);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Loading);
            for (auto& g : room.guests) {
                if (g.lobby->phase() == ClientLobby::Phase::Loading) g.lobby->report_loaded(true);
            }
            room.host.host_loaded(true);
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Begun);
        }
    } TEST_END();

    TEST_CASE("N4.17 The Fill (Protocol 11): A Hello Of Protocol 10 Is Answered VersionMismatch; A Leader's StartRequest Carries A Fill Level (None, Easy, Medium, Hard); With One It Is Heard Although Only One Person Is In The Room (The Bots Make Up The Rest), With None It Is Ignored As Before; A Guest Who Is Not The Leader, A Room Without A Leader, A Room That Is Loading And A Host With A Seat Ignore It; A StartRequest Of Another Layout (Protocol 7's One Byte, A Level Above 3, Extra Bytes) Is Garbage") {
        const auto asked = [](HostLobby& host) {                                    // the LeaderStart events: (seat, the level of each seat; the fill is the same level in every seat here: N4.17b has the levels one by one)
            std::vector<std::pair<uint8_t, std::array<FillLevel, 4>>> out;
            for (const auto& e : host.take_events()) {
                if (e.type == HostLobby::Event::Type::LeaderStart) out.emplace_back(e.seat, e.fill);
            }
            return out;
        };
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        hc.max_players = 4;
        {   // one person alone: START without a fill is ignored (a room needs two), with a fill of any level it is heard, with its level
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            asked(room.host);
            ASSERT_FALSE(room.host.can_start());                                    // one person is no match ...
            ASSERT_TRUE(room.host.can_start_filled());                              // ... but is one with bots for the rest
            ASSERT_TRUE(room.guests[ann].lobby->request_start());                   // fill none
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            for (const FillLevel level : {FillLevel::Easy, FillLevel::Medium, FillLevel::Hard}) {
                ASSERT_TRUE(room.guests[ann].lobby->request_start(level));
                room.run(100);
                const auto heard = asked(room.host);
                ASSERT_TRUE(heard.size() == 1 && heard[0].first == room.guests[ann].lobby->my_seat() && heard[0].second == StartRequestMsg::all(level).fill);
            }
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);                      // (the three with a level were heard, not ignored)
            // the bytes on the wire: the type, the level of each seat (protocol 13) and the two team bytes
            ASSERT_TRUE(encode(StartRequestMsg::all(FillLevel::Hard)) == (std::vector<uint8_t>{static_cast<uint8_t>(MsgType::StartRequest), 3, 3, 3, 3, 255, 255}));
            // the owner seats the bots and starts: the lobby takes bots in the empty seats, names them, and the roster of the Start has them
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (!room.host.occupied(seat)) ASSERT_TRUE(room.host.add_bot(seat, fill_bot_name(FillLevel::Medium)));
            }
            ASSERT_EQ(room.host.players(), size_t{4});
            ASSERT_EQ(room.host.humans(), size_t{1});
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (room.host.room().slots[seat].state == SlotState::Bot) ASSERT_EQ(room.host.room().slots[seat].name, "Bot (Medium)");
            }
            ASSERT_TRUE(room.host.start(1, 1, room.now));                           // one person and three bots start
            ASSERT_EQ(room.host.start_info().roster, 0x0F);
        }
        {   // a guest who is not the leader, and a request while the room is loading: ignored and counted, whatever the level
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            asked(room.host);
            room.guests[bob].client_end->send(encode(StartRequestMsg::all(FillLevel::Hard)));      // Bob does not lead (a client that is not the game's)
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_TRUE(room.guests[ann].lobby->request_start(FillLevel::Easy));
            room.run(100);
            const auto heard = asked(room.host);
            ASSERT_TRUE(heard.size() == 1 && heard[0].second == StartRequestMsg::all(FillLevel::Easy).fill);
            ASSERT_TRUE(room.host.start(1, 1, room.now));
            room.run(100);
            room.guests[ann].client_end->send(encode(StartRequestMsg::all(FillLevel::Hard)));      // the click that crossed the Start
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 2u);
            ASSERT_FALSE(room.host.can_start_filled());                                       // (a room that is loading cannot be started again)
        }
        {   // a room without a leader (early start off) and a host that holds a seat: every request is ignored, fill or not
            HostLobby::Config no_early = hc;
            no_early.early_start = false;
            Room room(no_early);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.guests[ann].client_end->send(encode(StartRequestMsg::all(FillLevel::Medium)));
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            HostLobby::Config lan;
            lan.host_seat = 0;
            Room lan_room(lan);
            const size_t gus = lan_room.join_seat("Gus");
            lan_room.guests[gus].client_end->send(encode(StartRequestMsg::all(FillLevel::Medium)));
            lan_room.run(100);
            ASSERT_TRUE(asked(lan_room.host).empty());
            ASSERT_EQ(lan_room.host.ignored_start_requests(), 1u);
        }
        {   // a room with no map chosen cannot start with bots either
            Room room(hc, false);
            const size_t ann = room.join_seat("Ann");
            asked(room.host);
            ASSERT_FALSE(room.host.can_start_filled());
            ASSERT_TRUE(room.guests[ann].lobby->request_start(FillLevel::Medium));
            room.run(100);
            ASSERT_TRUE(asked(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
        }
        {   // garbage: protocol 7's single byte, protocol 11's two bytes, a level above 3 in any seat, teams that are none: each is a violation, eight throw the sender out and the lead moves on
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t type = static_cast<uint8_t>(MsgType::StartRequest);
            const std::vector<std::vector<uint8_t>> bad = {{type}, {type, 0}, {type, 4, 0, 0, 0, 255, 255}, {type, 0, 0, 0, 255, 255, 255}, {type, 1, 2}, {type, 0, 0, 0, 0, 1, 1}, {type, 7}};
            for (const auto& m : bad) room.guests[ann].client_end->send(m);
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));                // seven are not enough
            room.guests[ann].client_end->send({type, 99, 0, 0, 0, 255, 255});
            room.run(300);
            ASSERT_FALSE(room.host.occupied(room.guests[ann].lobby->my_seat()));               // the eighth
            ASSERT_EQ(room.host.leader(), room.guests[bob].lobby->my_seat());
            ASSERT_EQ(room.host.ignored_start_requests(), 0u);                                 // (garbage is no request)
            ASSERT_TRUE(asked(room.host).empty());
        }
        {   // a Hello of protocol 10 (the layout of the Hello did not change, the messages did): VersionMismatch from a server's room and from a host that holds a seat, before anything
            // else; a Hello of this protocol is welcomed
            for (const uint8_t host_seat : {uint8_t{255}, uint8_t{0}}) {
                HostLobby::Config c;
                c.host_seat = host_seat;
                c.min_players = 2;
                Room room(c);
                HelloMsg old_hello;
                old_hello.version = 10;
                old_hello.name = "Old";
                RawClient raw = raw_hello(room, old_hello);
                room.run(100);
                std::vector<uint8_t> m;
                RejectMsg rj;
                ASSERT_TRUE(raw.end->poll(m) && decode(m, rj) && rj.reason == RejectReason::VersionMismatch);
                ASSERT_EQ(room.host.players(), host_seat == 0 ? size_t{1} : size_t{0});            // nobody was seated
                HelloMsg now_hello;
                now_hello.name = "New";
                RawClient ok = raw_hello(room, now_hello);
                room.run(100);
                WelcomeMsg w;
                ASSERT_TRUE(ok.end->poll(m) && decode(m, w));
                ASSERT_EQ(room.host.players(), host_seat == 0 ? size_t{2} : size_t{1});
            }
        }
        {   // the fill names: what the room calls a bot, and what a person may not be called
            ASSERT_EQ(fill_bot_name(FillLevel::Easy), "Bot (Easy)");
            ASSERT_EQ(fill_bot_name(FillLevel::Medium), "Bot (Medium)");
            ASSERT_EQ(fill_bot_name(FillLevel::Hard), "Bot (Hard)");
            ASSERT_EQ(fill_bot_name(FillLevel::None), std::string());
            FillLevel level = FillLevel::None;
            ASSERT_TRUE(parse_fill_level("MEDIUM", level) && level == FillLevel::Medium);
            ASSERT_TRUE(parse_fill_level("none", level) && level == FillLevel::None);
            ASSERT_FALSE(parse_fill_level("harder", level) || parse_fill_level("", level) || parse_fill_level("easy ", level));
            ASSERT_EQ(level, FillLevel::None);                                                 // (a refusal leaves the value alone)
            Room room(hc);
            const size_t imposter = room.join_seat("Bot (Hard)");                              // a person who calls itself a bot is renamed: the marker belongs to bots
            ASSERT_TRUE(room.host.room().slots[room.guests[imposter].lobby->my_seat()].name.rfind("Bot (", 0) == std::string::npos);
        }
    } TEST_END();

    TEST_CASE("N4.17b The Fill And The Teams (Protocol 13): The Leader's StartRequest Reaches The Owner With A Level For Each Seat And The Teams; ClientLobby::request_start Sends Them; HostLobby::start Puts The Teams Into The Start That Every Guest Decodes, And Refuses Teams That The Seats Cannot Make") {
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        hc.max_players = 4;
        const auto leader_start = [](HostLobby& host, uint8_t& seat, std::array<FillLevel, 4>& fill, sim::StartTeams& teams) {
            size_t n = 0;
            for (const auto& e : host.take_events()) {
                if (e.type != HostLobby::Event::Type::LeaderStart) continue;
                seat = e.seat;
                fill = e.fill;
                teams = e.teams;
                ++n;
            }
            return n;
        };
        {   // levels and teams, as the leader chose them: every one arrives as sent (a person's seat holds a level too: it is the owner that skips it)
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            uint8_t seat = 255;
            std::array<FillLevel, 4> fill{};
            sim::StartTeams teams;
            leader_start(room.host, seat, fill, teams);
            const std::array<FillLevel, 4> asked = {FillLevel::None, FillLevel::Hard, FillLevel::None, FillLevel::Easy};
            ASSERT_TRUE(room.guests[ann].lobby->request_start(asked, sim::StartTeams{true, 0, 3}));
            room.run(100);
            ASSERT_EQ(leader_start(room.host, seat, fill, teams), size_t{1});
            ASSERT_TRUE(seat == room.guests[ann].lobby->my_seat() && fill == asked && teams == sim::StartTeams({true, 0, 3}));
            ASSERT_TRUE(room.guests[ann].lobby->request_start(StartRequestMsg::all(FillLevel::Medium).fill));     // no teams chosen: free for all
            room.run(100);
            ASSERT_EQ(leader_start(room.host, seat, fill, teams), size_t{1});
            ASSERT_TRUE(fill == StartRequestMsg::all(FillLevel::Medium).fill && !teams.set);
            ASSERT_TRUE(room.guests[ann].lobby->request_start(FillLevel::Easy));                                    // protocol 11's one level: the same level in every seat
            room.run(100);
            ASSERT_EQ(leader_start(room.host, seat, fill, teams), size_t{1});
            ASSERT_TRUE(fill == StartRequestMsg::all(FillLevel::Easy).fill && !teams.set);
            // a level in an empty seat only, with one person alone: heard (the bots make up the rest); levels that are all none: the START of protocol 7, ignored while one person is alone
            ASSERT_TRUE(room.guests[ann].lobby->request_start(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Hard}));
            room.run(100);
            ASSERT_EQ(leader_start(room.host, seat, fill, teams), size_t{1});
            ASSERT_TRUE(room.guests[ann].lobby->request_start(std::array<FillLevel, 4>{}, sim::StartTeams{true, 0, 1}));
            room.run(100);
            ASSERT_EQ(leader_start(room.host, seat, fill, teams), size_t{0});                                       // (teams alone are no bots: one person cannot start)
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
        }
        {   // the owner's start with teams: the Start every guest decodes has them, and so has the owner's own start_info; a start without teams has none
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(100);
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (!room.host.occupied(seat)) ASSERT_TRUE(room.host.add_bot(seat, fill_bot_name(FillLevel::Easy)));
            }
            ASSERT_FALSE(room.host.start(1, 1, room.now, sim::StartTeams{true, 0, 0}));                             // one seat is no team: refused, the room is as it was
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_TRUE(room.host.start(1, 1, room.now, sim::StartTeams{true, 1, 3}));
            ASSERT_TRUE(room.host.start_info().team_a == 1 && room.host.start_info().team_b == 3 && room.host.start_info().roster == 0x0F);
            room.run(100);
            for (const size_t g : {ann, bob}) {
                ASSERT_EQ(room.guests[g].lobby->phase(), ClientLobby::Phase::Loading);
                ASSERT_TRUE(room.guests[g].lobby->start_info().team_a == 1 && room.guests[g].lobby->start_info().team_b == 3);
                ASSERT_TRUE(room.guests[g].lobby->start_info().teams() == sim::StartTeams({true, 1, 3}));
            }
        }
        {   // teams that the seats cannot make: a seat of the pair that does not play, or a team that would be the whole match, is refused by the lobby itself
            Room room(hc);
            room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(100);
            ASSERT_FALSE(room.host.start(1, 1, room.now, sim::StartTeams{true, 0, 1}));                             // two seats play: the team would be everybody
            ASSERT_FALSE(room.host.start(1, 1, room.now, sim::StartTeams{true, 0, 3}));                             // seat 3 is empty
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_TRUE(room.host.start(1, 1, room.now));                                                           // free for all: as ever
            ASSERT_TRUE(room.host.start_info().team_a == kNoTeam && room.host.start_info().team_b == kNoTeam);
        }
    } TEST_END();

    TEST_CASE("N4.17c The Fill Plan (Protocol 13): One Level Or A Level For Each Seat (One Word Or Four Joined By Commas, In Any Case, Nothing Else), The Seats That A START Seats (Empty Seats With A Level, The Lowest First, Never More Than The Room's Players, A Taken Seat Skipped), The Words That Tell It") {
        using Seats = std::vector<std::pair<uint8_t, FillLevel>>;
        // one level converts to the plan that gives it to every seat; the plan knows when it asks for nothing and when it is one level
        const FillPlan hard = FillLevel::Hard;
        ASSERT_TRUE(hard.uniform() && hard.any() && hard.level[0] == FillLevel::Hard && hard.level[3] == FillLevel::Hard);
        ASSERT_TRUE(FillPlan().uniform() && !FillPlan().any() && FillPlan() == FillLevel::None);
        const FillPlan mixed(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Easy, FillLevel::None, FillLevel::Hard});
        ASSERT_TRUE(mixed.any() && !mixed.uniform() && mixed != hard && mixed == mixed && hard == FillLevel::Hard && hard != FillLevel::Easy);
        // parse: one word is every seat, four are the seats 0 to 3; any case; nothing else (and the target is left alone)
        FillPlan out = FillLevel::Medium;
        std::string why;
        for (const char* good : {"none", "easy", "Medium", "HARD", "none,none,easy,hard", "Easy,MEDIUM,hard,None", "hard,hard,hard,hard"}) {
            out = FillLevel::Medium;
            ASSERT_TRUE(parse_fill_plan(good, out, why));
        }
        ASSERT_TRUE(parse_fill_plan("Easy", out, why) && out == FillLevel::Easy);
        ASSERT_TRUE(parse_fill_plan("none,easy,none,hard", out, why) && out == mixed);
        ASSERT_TRUE(parse_fill_plan("NONE,Easy,NONE,Hard", out, why) && out == mixed);
        ASSERT_TRUE(parse_fill_plan("none,none,easy,hard", out, why) && out == FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard}));      // (the order is the seats')
        ASSERT_TRUE(parse_fill_plan("hard,hard,hard,hard", out, why) && out == hard && out.uniform());
        for (const char* bad : {"", "harder", "none,easy", "none,none,easy", "none,none,easy,hard,hard", "none,,easy,hard", ",,,", "easy medium", "none, easy, medium, hard", "1", "easy,", ",easy", "none,none,easy,hardd", "none;none;easy;hard"}) {
            out = FillLevel::Medium;
            why.clear();
            ASSERT_FALSE(parse_fill_plan(bad, out, why));
            ASSERT_TRUE(out == FillLevel::Medium && !why.empty());
        }
        // the text reads back: one word for one level in every seat, four words otherwise
        ASSERT_EQ(fill_plan_text(FillPlan()), std::string("none"));
        ASSERT_EQ(fill_plan_text(hard), std::string("hard"));
        ASSERT_EQ(fill_plan_text(mixed), std::string("none,easy,none,hard"));
        const FillPlan same_start(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Easy, FillLevel::Hard});          // (the first seats alike, the later ones not: four words, not one)
        const FillPlan same_three(std::array<FillLevel, 4>{FillLevel::Hard, FillLevel::Hard, FillLevel::Hard, FillLevel::Easy});
        ASSERT_EQ(fill_plan_text(same_start), std::string("none,none,easy,hard"));
        ASSERT_EQ(fill_plan_text(same_three), std::string("hard,hard,hard,easy"));
        for (const FillPlan& plan : {FillPlan(), hard, mixed, same_start, same_three, FillPlan(FillLevel::Easy), FillPlan(std::array<FillLevel, 4>{FillLevel::Hard, FillLevel::Medium, FillLevel::Easy, FillLevel::None})}) {
            FillPlan back = FillLevel::Medium;
            ASSERT_TRUE(parse_fill_plan(fill_plan_text(plan), back, why) && back == plan);
        }
        // the seats that a START seats: the lowest empty seats that have a level, up to the room's players
        RoomMsg room;
        room.slots[0] = {SlotState::Client, "Ann", 10};
        const auto seats_of = [&](const FillPlan& plan, uint8_t players) { return plan_fill_seats(plan, room, players); };
        const FillPlan ems(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard});
        ASSERT_TRUE(seats_of(ems, 4) == (Seats{{1, FillLevel::Easy}, {2, FillLevel::Medium}, {3, FillLevel::Hard}}));
        ASSERT_TRUE(seats_of(ems, 3) == (Seats{{1, FillLevel::Easy}, {2, FillLevel::Medium}}));              // (the room's players: one person and two bots)
        ASSERT_TRUE(seats_of(ems, 2) == (Seats{{1, FillLevel::Easy}}));
        ASSERT_TRUE(seats_of(ems, 1).empty());                                                               // (a room for one is full)
        ASSERT_TRUE(seats_of(FillPlan(), 4).empty());
        ASSERT_TRUE(seats_of(hard, 4) == (Seats{{1, FillLevel::Hard}, {2, FillLevel::Hard}, {3, FillLevel::Hard}}));   // (the person's own seat is never filled: it is taken)
        ASSERT_TRUE(seats_of(FillPlan(std::array<FillLevel, 4>{FillLevel::Hard, FillLevel::None, FillLevel::None, FillLevel::None}), 4).empty());          // a level for the seat of a person asks for nothing
        ASSERT_TRUE(seats_of(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Hard}), 2) == (Seats{{3, FillLevel::Hard}}));    // the lowest seat WITH a level
        room.slots[2] = {SlotState::Client, "Bob", 10};                                                      // a person took seat 2: its level is skipped, and it counts for the room's players
        ASSERT_TRUE(seats_of(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Easy, FillLevel::Hard, FillLevel::Medium}), 4) == (Seats{{1, FillLevel::Easy}, {3, FillLevel::Medium}}));
        ASSERT_TRUE(seats_of(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Easy, FillLevel::Hard, FillLevel::Medium}), 3) == (Seats{{1, FillLevel::Easy}}));
        room.slots[1] = {SlotState::Bot, "Bot (Easy)", 0};                                                   // a bot of the room's own specification holds a seat too
        ASSERT_TRUE(seats_of(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::Hard, FillLevel::Hard, FillLevel::Medium}), 4) == (Seats{{3, FillLevel::Medium}}));
        // the words (the colour words of the seats: 0 green, 1 red, 2 blue, 3 black)
        ASSERT_EQ(fill_seats_sentence(Seats{{2, FillLevel::Easy}, {3, FillLevel::Hard}}), std::string("Blue gets an Easy bot, Black a Hard bot"));
        ASSERT_EQ(fill_seats_sentence(Seats{{1, FillLevel::Medium}}), std::string("Red gets a Medium bot"));
        ASSERT_EQ(fill_seats_sentence(Seats{{1, FillLevel::Medium}, {2, FillLevel::Hard}, {3, FillLevel::Easy}}), std::string("Red gets a Medium bot, Blue a Hard bot, Black an Easy bot"));
        ASSERT_EQ(fill_seats_sentence(Seats{{0, FillLevel::Easy}}), std::string("Green gets an Easy bot"));
        ASSERT_EQ(fill_seats_sentence(Seats{}), std::string());
        ASSERT_EQ(fill_seats_short(Seats{{2, FillLevel::Easy}, {3, FillLevel::Hard}}), std::string("Blue Easy, Black Hard"));
        ASSERT_EQ(fill_seats_short(Seats{{1, FillLevel::Medium}, {2, FillLevel::Hard}, {3, FillLevel::Easy}}), std::string("Red Medium, Blue Hard, Black Easy"));
        ASSERT_EQ(fill_seats_short(Seats{}), std::string());
    } TEST_END();

    TEST_CASE("N4.18 Chat In The Waiting Room (Protocol 11): A Line Is Relayed To Everybody In The Room, The Sender Included, With The Seat Of The Connection (Not The Payload's) And The Name, No Team; A Late Joiner Hears Only What Is Said After It Came; Chat Works While The Map Loads; A Line That Does Not Decode (Too Long, Not Printable, A Wrong Flag) Is A Violation As In The Match; An Empty Line Is Ignored; The Lines Are Kept (The Last 200) And Handed On Once; The Room Can Speak To One Guest") {
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        const auto lines_of = [](ClientLobby& lobby) {
            std::vector<std::string> out;
            for (const ChatLine& l : lobby.take_chat()) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
            return out;
        };
        {   // the relay
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            ASSERT_TRUE(room.guests[ann].lobby->chat("hello Bob"));
            room.run(100);
            ASSERT_EQ(lines_of(*room.guests[bob].lobby), (std::vector<std::string>{"0|Ann|hello Bob"}));
            ASSERT_EQ(lines_of(*room.guests[ann].lobby), (std::vector<std::string>{"0|Ann|hello Bob"}));        // the sender hears its own line from the room
            // the seat is the connection's, whatever the payload says; the team flag is cleared (a team does not exist before the match): what a raw listener in the room is sent shows it
            HelloMsg rawhello;
            rawhello.name = "Raw";
            RawClient raw = raw_hello(room, rawhello);
            room.run(100);
            drain_messages(raw.end);
            ChatMsg forged;
            forged.sender = 0;
            forged.team = true;
            forged.text = "I am Ann";
            room.guests[bob].client_end->send(encode(forged));
            room.run(100);
            size_t relayed_to_raw = 0;
            for (const auto& m : drain_messages(raw.end)) {
                ChatMsg c;
                if (peek_type(m) != MsgType::Chat) continue;
                ASSERT_TRUE(decode(m, c));
                ASSERT_TRUE(c.sender == room.guests[bob].lobby->my_seat() && !c.team && c.text == "I am Ann");
                ++relayed_to_raw;
            }
            ASSERT_EQ(relayed_to_raw, size_t{1});
            const std::vector<ChatLine> heard = room.guests[ann].lobby->take_chat();
            ASSERT_TRUE(heard.size() == 1 && heard[0].seat == room.guests[bob].lobby->my_seat() && heard[0].name == "Bob" && heard[0].text == "I am Ann" && !heard[0].notice());
            ASSERT_TRUE(room.guests[ann].lobby->chat("x"));
            room.run(100);
            // the host lobby kept all of it, once for the owner
            ASSERT_EQ(room.host.chat_log().size(), size_t{3});
            ASSERT_EQ(room.host.take_chat().size(), size_t{3});
            ASSERT_TRUE(room.host.take_chat().empty());
            // the line on the wire: type 9, the sender's seat, no team, the text
            ASSERT_TRUE(room.guests[bob].lobby->chat("hi"));
            room.run(100);
            // an empty line and a line of only control characters say nothing: nothing is sent at all
            ASSERT_FALSE(room.guests[bob].lobby->chat(""));
            ASSERT_FALSE(room.guests[bob].lobby->chat(std::string("\x01\x02\x7f", 3)));
            // a longer line is cut to 100 characters (the match's rule)
            ASSERT_TRUE(room.guests[bob].lobby->chat(std::string(150, 'z')));
            room.run(100);
            const std::vector<ChatLine> all = room.guests[ann].lobby->take_chat();
            ASSERT_EQ(all.size(), size_t{3});                                       // "x", "hi", the cut line
            ASSERT_EQ(all[2].text.size(), kMaxChatChars);
        }
        {   // a late joiner hears only what is said after it came
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->chat("anybody here?"));
            room.run(100);
            const size_t late = room.join_seat("Late");
            room.run(100);
            ASSERT_TRUE(room.guests[late].lobby->take_chat().empty());              // nothing of what was said before
            ASSERT_TRUE(room.guests[ann].lobby->chat("welcome"));
            room.run(100);
            const std::vector<ChatLine> heard = room.guests[late].lobby->take_chat();
            ASSERT_TRUE(heard.size() == 1 && heard[0].text == "welcome");
            ASSERT_EQ(room.host.chat_log().size(), size_t{2});                      // (the room itself remembers both)
        }
        {   // chat while the map loads (the room's Start has gone out and the guests have not all reported); the Cancel that follows leaves the room as it was
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(100);
            ASSERT_TRUE(room.host.start(1, 1, room.now));
            room.run(100);
            ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::Loading);
            ASSERT_TRUE(room.guests[ann].lobby->chat("loading..."));
            room.guests[bob].lobby->report_loaded(true);                            // Bob is loaded and waits for the rest
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Loaded);
            ASSERT_TRUE(room.guests[bob].lobby->chat("done here"));
            room.run(100);
            ASSERT_EQ(lines_of(*room.guests[bob].lobby), (std::vector<std::string>{"0|Ann|loading...", "1|Bob|done here"}));
            ASSERT_EQ(lines_of(*room.guests[ann].lobby), (std::vector<std::string>{"0|Ann|loading...", "1|Bob|done here"}));
            room.host.cancel();                                                     // back in the room: chat still works
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->chat("again"));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{1});
            ASSERT_FALSE(room.guests[ann].lobby->chat_log().empty());               // the whole room's talk is kept
        }
        {   // a line that does not decode is a violation, as in the match: eight of them throw the guest out
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t type = static_cast<uint8_t>(MsgType::Chat);
            std::vector<uint8_t> too_long = {type, 0, 0, 101};
            too_long.insert(too_long.end(), 101, 'a');
            std::vector<std::vector<uint8_t>> bad = {
                too_long,
                {type, 0, 2, 2, 'h', 'i'},                                          // a team flag that is neither 0 nor 1
                {type, 0, 0, 2, 'h', 0x07},                                         // a control character
                {type, 0, 0, 2, 'h', 0xC3},                                         // not ASCII
                {type, 0, 0, 5, 'h', 'i'},                                          // the length says more than there is
                {type, 0, 0, 1, 'h', 'i'},                                          // and less
                {type},                                                             // nothing at all
            };
            for (const auto& m : bad) room.guests[bob].client_end->send(m);
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[bob].lobby->my_seat()));    // seven are not enough
            ASSERT_TRUE(room.host.chat_log().empty() && room.guests[ann].lobby->chat_log().empty());      // and nothing was relayed
            room.guests[bob].client_end->send({type, 0, 3});
            room.run(300);
            ASSERT_FALSE(room.host.occupied(room.guests[bob].lobby->my_seat()));   // the eighth
            ASSERT_TRUE(room.host.chat_log().empty());
            // an empty line (a valid message that says nothing) is ignored, costs a message of the budget and no offence
            ChatMsg blank;
            for (int i = 0; i < 20; ++i) room.guests[ann].client_end->send(encode(blank));
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()) && room.host.chat_log().empty());
        }
        {   // the flood: 1500 lines at once from one connection. The chat budget (N4.20) relays the first 5 and drops 20, then every line is a violation: the sender is out after eight of them
            // (33 lines in all), within one pass, and the other guest heard no more than the burst
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            ChatMsg line;
            line.text = "spam";
            const std::vector<uint8_t> bytes = encode(line);
            for (int i = 0; i < 1500; ++i) room.guests[ann].client_end->send(bytes);
            room.run(1000);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_TRUE(room.host.occupied(room.guests[bob].lobby->my_seat()));    // Bob is untouched
            ASSERT_EQ(room.host.chat_total(), uint64_t{kChatBurst});                // the burst of 5 and nothing else: not 1000, not 1500
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{kChatBurst});
        }
        {   // the log keeps the last 200 lines and hands every new line on once
            ChatLog log;
            for (int i = 0; i < 250; ++i) log.add(ChatLine{0, "A", "line " + std::to_string(i)});
            ASSERT_EQ(log.total(), uint64_t{250});
            ASSERT_EQ(log.lines().size(), ChatLog::kMaxLines);
            ASSERT_EQ(log.lines().front().text, "line 50");
            ASSERT_EQ(log.lines().back().text, "line 249");
            const std::vector<ChatLine> first = log.take();
            ASSERT_TRUE(first.size() == ChatLog::kMaxLines && first.front().text == "line 50");
            ASSERT_TRUE(log.take().empty());
            log.add(ChatLine{1, "B", "next"});
            log.add(ChatLine{2, "C", "and next"});
            const std::vector<ChatLine> two = log.take();
            ASSERT_TRUE(two.size() == 2 && two[0].text == "next" && two[1].seat == 2);
            const ChatLine from_room{255, "", "x"};
            const ChatLine from_dan{3, "D", "x"};
            ASSERT_TRUE(from_room.notice() && !from_dan.notice());
        }
        {   // the room speaks to ONE guest: a notice (sender 255, no name), kept out of everybody else's log and its own
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(100);
            ASSERT_TRUE(room.host.notify(room.guests[ann].lobby->my_seat(), kNoticeFillFog));
            room.run(100);
            const std::vector<ChatLine> heard = room.guests[ann].lobby->take_chat();
            ASSERT_TRUE(heard.size() == 1 && heard[0].notice() && heard[0].name.empty() && heard[0].text == kNoticeFillFog);
            ASSERT_TRUE(room.guests[bob].lobby->take_chat().empty());
            ASSERT_TRUE(room.host.chat_log().empty());
            ASSERT_FALSE(room.host.notify(3, "nobody sits here"));
            ASSERT_FALSE(room.host.notify(room.guests[ann].lobby->my_seat(), std::string()));
            ASSERT_TRUE(std::string(kNoticeFillFog).size() <= kMaxChatChars && std::string(kNoticeFillMap).size() <= kMaxChatChars);
        }
        {   // a host that holds a seat (a game on the local network): its own line goes to every guest and into its own log; a server's host has no seat and says nothing
            HostLobby::Config lan;
            lan.host_seat = 0;
            lan.host_name = "Queen";
            Room room(lan);
            const size_t gus = room.join_seat("Gus");
            ASSERT_TRUE(room.host.chat("welcome to my room"));
            room.run(100);
            ASSERT_EQ(lines_of(*room.guests[gus].lobby), (std::vector<std::string>{"0|Queen|welcome to my room"}));
            ASSERT_TRUE(room.guests[gus].lobby->chat("thanks"));
            room.run(100);
            const std::vector<ChatLine> own = room.host.take_chat();
            ASSERT_TRUE(own.size() == 2 && own[0].seat == 0 && own[0].name == "Queen" && own[1].name == "Gus");
            ASSERT_FALSE(room.host.chat(""));
            Room server(hc);
            ASSERT_FALSE(server.host.chat("the server has no seat"));
            // the match has begun: the lobby is not the room's any more
            room.host.start(1, 1, room.now);
            room.run(100);
            room.guests[gus].lobby->report_loaded(true);
            room.host.host_loaded(true);
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            ASSERT_FALSE(room.host.chat("late"));
            ASSERT_FALSE(room.guests[gus].lobby->chat("late too"));                 // (a lobby that has begun sends nothing: the session is the chat of the match)
        }
        {   // a client that is not in a room says nothing, and a line from the room that is no line is ignored (a sender that is no seat, a line that does not decode)
            LoopbackNetwork net(7);
            auto ends = net.connect({10, 0});
            ClientLobby lobby(ends.second, ClientLobby::Config{});
            ASSERT_FALSE(lobby.chat("hello"));                                      // not connected, not welcomed
            net.set_time(20);
            lobby.update(20);
            ends.first->send(encode(WelcomeMsg{1, 4}));
            RoomMsg r;
            r.slots[1] = {SlotState::Client, "Me", 0};
            r.slots[2] = {SlotState::Client, "Cat", 10};
            r.map_name = "TINY.LVL";
            r.you = 1;
            ends.first->send(encode(r));
            ChatMsg weird;
            weird.sender = 77;                                                      // no seat, and not the room
            weird.text = "who am I";
            ends.first->send(encode(weird));
            ChatMsg cat;
            cat.sender = 2;
            cat.text = "meow";
            ends.first->send(encode(cat));
            ends.first->send({static_cast<uint8_t>(MsgType::Chat), 2, 1, 200});     // cut off
            net.set_time(80);
            lobby.update(80);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
            ASSERT_TRUE(lobby.chat("hello"));
            const std::vector<ChatLine> got = lobby.take_chat();
            ASSERT_TRUE(got.size() == 1 && got[0].seat == 2 && got[0].name == "Cat" && got[0].text == "meow");
            const auto events = lobby.take_events();
            size_t chat_events = 0;
            for (const auto& e : events) chat_events += e.type == ClientLobby::Event::Type::Chat ? 1u : 0u;
            ASSERT_EQ(chat_events, size_t{1});
            ASSERT_TRUE(events.back().type == ClientLobby::Event::Type::Chat && events.back().seat == 2);
            lobby.leave();
            ASSERT_FALSE(lobby.chat("gone"));
        }
    } TEST_END();

    TEST_CASE("N4.19 The Waiting Room Has No Teams: A Line That Says It Is For The Team Reaches Everybody In The Room As A Line For All (The Flag Cleared On The Wire), Never Only An Ally; Nobody Is Left Out, A Raw Client Included") {
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        Room room(hc);
        const size_t ann = room.join_seat("Ann");
        const size_t bob = room.join_seat("Bob");
        const size_t cat = room.join_seat("Cat");
        HelloMsg rawhello;
        rawhello.name = "Raw";
        RawClient raw = raw_hello(room, rawhello);
        room.run(200);
        drain_messages(raw.end);
        for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
        // a client that marks its line for the team (the lobby's own chat() never does: a modified client may)
        ChatMsg team_line;
        team_line.sender = 0;
        team_line.team = true;
        team_line.text = "team only please";
        room.guests[ann].client_end->send(encode(team_line));
        room.run(200);
        for (const size_t g : {ann, bob, cat}) {
            const std::vector<ChatLine> heard = room.guests[g].lobby->take_chat();
            ASSERT_TRUE(heard.size() == 1 && heard[0].text == "team only please" && heard[0].seat == room.guests[ann].lobby->my_seat());
        }
        size_t on_wire = 0;
        for (const auto& m : drain_messages(raw.end)) {
            ChatMsg c;
            if (peek_type(m) != MsgType::Chat) continue;
            ASSERT_TRUE(decode(m, c));
            ASSERT_TRUE(!c.team && c.text == "team only please");                    // the flag is gone: the room is no team
            ++on_wire;
        }
        ASSERT_EQ(on_wire, size_t{1});
    } TEST_END();

    TEST_CASE("N4.20 The Waiting Room's Chat Has A Budget Of Its Own (A Remake Protection: The Original Has No Limit On Chat): A Burst Of 5 Lines, Then One A Second; A Line Beyond It Is Dropped (Not Relayed, Not Logged, No Offence At First); A Connection That Goes On Beyond It For Long Is Thrown Out; Each Connection Has Its Own; An Honest Talker Is Never Touched; Empty Lines Cost Nothing Of It; A Seat Taken Over With Its Key Keeps What It Used") {
        HostLobby::Config hc;
        hc.host_seat = 255;
        hc.min_players = 2;
        const auto say = [](Room& room, size_t guest, const std::string& text) {
            ChatMsg m;
            m.text = text;
            room.guests[guest].client_end->send(encode(m));
        };
        {   // a burst of 8 at once: 5 are relayed (in order), 3 are dropped, nobody is thrown out
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            for (int i = 0; i < 8; ++i) say(room, ann, "line " + std::to_string(i));
            room.run(100);
            const std::vector<ChatLine> heard = room.guests[bob].lobby->take_chat();
            ASSERT_EQ(heard.size(), size_t{5});
            for (size_t i = 0; i < heard.size(); ++i) ASSERT_EQ(heard[i].text, "line " + std::to_string(i));
            ASSERT_EQ(room.host.chat_total(), uint64_t{5});                         // (what was dropped is not in the room's log either)
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
            // one second later one more is allowed, not two
            room.run(1000);
            say(room, ann, "after one second");
            say(room, ann, "and a second one right behind it");
            room.run(100);
            const std::vector<ChatLine> later = room.guests[bob].lobby->take_chat();
            ASSERT_TRUE(later.size() == 1 && later[0].text == "after one second");
            // five quiet seconds fill the burst again
            room.run(6000);
            for (int i = 0; i < 6; ++i) say(room, ann, "again " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{5});
        }
        {   // an honest talker: a line every 1.2 s for a minute, all relayed, never an offence; and a busy one (a line a second for two minutes) is fine too
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            for (int i = 0; i < 50; ++i) {
                say(room, ann, "talk " + std::to_string(i));
                room.run(1200);
            }
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{50});
            room.guests[ann].lobby->take_chat();                                    // (Ann hears her own lines from the room too)
            for (int i = 0; i < 120; ++i) {
                say(room, bob, "busy " + std::to_string(i));
                room.run(1000);
            }
            ASSERT_EQ(room.guests[ann].lobby->take_chat().size(), size_t{120});
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()) && room.host.occupied(room.guests[bob].lobby->my_seat()));
        }
        {   // two lines a second for a minute: half of them are dropped (the excess refills as fast as it is used), nobody is thrown out; four a second for long: the excess runs out
            // (3 a second are dropped, 20 lines of excess last about 10 s), then it is a violation each and eight throw the sender out
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            size_t heard = 0;
            for (int i = 0; i < 120; ++i) {
                say(room, ann, "two a second " + std::to_string(i));
                room.run(500);
                heard += room.guests[bob].lobby->take_chat().size();
            }
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
            ASSERT_TRUE(heard >= 60 && heard <= 70);                                // about a line a second, the budget's rate (and the burst)
            room.run(6000);
            heard = 0;
            size_t sent = 0;
            bool out = false;
            while (!out && sent < 400) {
                say(room, ann, "four a second " + std::to_string(sent));
                ++sent;
                room.run(250);
                heard += room.guests[bob].lobby->take_chat().size();
                out = !room.host.occupied(room.guests[ann].lobby->my_seat());
            }
            ASSERT_TRUE(out);                                                       // a talker at four times the rate for ten seconds is flooding
            ASSERT_TRUE(sent >= 30 && sent <= 90);
            ASSERT_TRUE(heard >= 10 && heard <= 30);                                // (it was heard at the budget's rate until then)
            ASSERT_TRUE(room.host.occupied(room.guests[bob].lobby->my_seat()));
        }
        {   // a flood is thrown out within a pass, and a second connection says its lines as ever: each has its own budget
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(200);
            for (int i = 0; i < 900; ++i) say(room, ann, "flood " + std::to_string(i));
            say(room, bob, "an honest line");
            room.run(100);
            ASSERT_FALSE(room.host.occupied(room.guests[ann].lobby->my_seat()));
            const std::vector<ChatLine> heard = room.guests[cat].lobby->take_chat();
            size_t honest = 0, spam = 0;
            for (const ChatLine& l : heard) (l.text == "an honest line" ? honest : spam) += 1;
            ASSERT_TRUE(honest == 1 && spam == 5);                                  // Bob's line, and Ann's burst
            for (int i = 0; i < 5; ++i) say(room, bob, "bob " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[cat].lobby->take_chat().size(), size_t{4});       // (Bob had used one of his 5)
        }
        {   // each connection has a budget of its own: two guests who each say five lines at once are both heard in full (ten lines), a third guest's budget is untouched by either
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(200);
            for (int i = 0; i < 5; ++i) {
                say(room, ann, "ann " + std::to_string(i));
                say(room, bob, "bob " + std::to_string(i));
            }
            room.run(100);
            ASSERT_EQ(room.guests[cat].lobby->take_chat().size(), size_t{10});
            for (int i = 0; i < 5; ++i) say(room, cat, "cat " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[ann].lobby->take_chat().size(), size_t{15});                                     // (her own five, Bob's five and Cat's five: she never took them before)
        }
        {   // empty lines (valid, they say nothing) take nothing of the chat budget
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            ChatMsg blank;
            for (int i = 0; i < 100; ++i) room.guests[ann].client_end->send(encode(blank));
            room.run(100);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
            for (int i = 0; i < 5; ++i) say(room, ann, "real " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{5});
        }
        {   // the budget is the seat's, not the connection's: a seat taken over with its key (a reload) does not get a fresh burst
            HostLobby::Config kc = keyed_server_config(77);
            Room room(kc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(200);
            for (int i = 0; i < 5; ++i) say(room, ann, "used " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{5});
            const SeatKey key = room.host.key_of(room.guests[ann].lobby->my_seat());
            auto ends = room.net.connect({10, 0});
            room.guests.emplace_back();
            Room::Guest& again = room.guests.back();
            again.host_end = ends.first;
            again.client_end = ends.second;
            ClientLobby::Config cc;
            cc.name = "Ann";
            cc.key = key;
            again.lobby = std::make_unique<ClientLobby>(ends.second, cc);
            room.host.add_connection(ends.first, room.now);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            const size_t back = room.guests.size() - 1;
            for (int i = 0; i < 5; ++i) say(room, back, "after the reload " + std::to_string(i));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->take_chat().size(), size_t{0});       // the seat's burst was spent a moment ago
        }
    } TEST_END();

    TEST_CASE("N4.21 The Hook Before The Start (L3 Of The Review): HostLobby::start Calls set_before_start's Function Once, With The Start Message That Is About To Go Out And The Time Of The Start, After The Roster And The Keys Are Fixed And BEFORE The First Byte Of The Start Has Been Sent To Any Guest (The Order Of The Sends Is Recorded); A Start That Is Refused Does Not Call It; A Cancelled Start And The Next Start Call It Again; A Lobby Without The Function Starts As It Did") {
        // the sends of the host's side of every link, in order, with the hook's call among them
        std::vector<std::string> log;
        class OrderTap final : public Connection {
        public:
            OrderTap(Connection* inner, std::vector<std::string>* sends) : inner_(inner), log_(sends) {}
            bool send(const std::vector<uint8_t>& m) override {
                log_->push_back(peek_type(m) == MsgType::Start ? "Start" : "other");
                return inner_->send(m);
            }
            bool poll(std::vector<uint8_t>& m) override { return inner_->poll(m); }
            State state() const override { return inner_->state(); }
            void close() override { inner_->close(); }

        private:
            Connection* inner_;
            std::vector<std::string>* log_;
        };
        std::vector<std::unique_ptr<OrderTap>> taps;
        Room room(keyed_server_config(77));
        const auto join_tapped = [&](const std::string& name) {
            auto ends = room.net.connect({10, 0});
            room.guests.emplace_back();
            Room::Guest& g = room.guests.back();
            g.host_end = ends.first;
            g.client_end = ends.second;
            ClientLobby::Config cc;
            cc.name = name;
            g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
            taps.push_back(std::make_unique<OrderTap>(ends.first, &log));
            room.host.add_connection(taps.back().get(), room.now);
            room.run(100);
        };
        join_tapped("Ann");
        int calls = 0;
        room.host.set_before_start([&](const StartMsg&, uint32_t) { ++calls; });
        ASSERT_FALSE(room.host.start(1, 2, room.now));                                          // one player: the start is refused, and nothing is called
        ASSERT_EQ(calls, 0);
        join_tapped("Bob");
        StartMsg seen;
        uint32_t seen_at = 0;
        bool keys_known = false;
        bool info_is_the_message = false;
        bool phase_loading = false;
        room.host.set_before_start([&](const StartMsg& start, uint32_t now_ms) {
            ++calls;
            seen = start;
            seen_at = now_ms;
            log.push_back("hook");
            keys_known = !key_is_zero(room.host.key_of(0)) && !key_is_zero(room.host.key_of(1));
            info_is_the_message = encode(room.host.start_info()) == encode(start);
            phase_loading = room.host.phase() == HostLobby::Phase::Loading;
        });
        log.clear();
        ASSERT_TRUE(room.host.start(777, 0xABCDEFull, room.now));
        ASSERT_EQ(calls, 1);
        ASSERT_TRUE(seen.seed == 777 && seen.map_hash == 0xABCDEFull && seen.roster == 0x03 && seen.map_name == "TINY.LVL" && seen_at == room.now);
        ASSERT_TRUE(keys_known && info_is_the_message && phase_loading);
        const auto hook_at = std::find(log.begin(), log.end(), "hook");
        const auto first_start = std::find(log.begin(), log.end(), "Start");
        ASSERT_TRUE(hook_at != log.end() && first_start != log.end());
        ASSERT_TRUE(hook_at < first_start);                                                      // the hook was called before the first Start was sent
        ASSERT_EQ(std::count(log.begin(), log.end(), "Start"), 2);                               // ... and both guests were sent it
        room.run(100);
        ASSERT_TRUE(room.guests[0].lobby->phase() == ClientLobby::Phase::Loading && room.guests[1].lobby->phase() == ClientLobby::Phase::Loading);
        ASSERT_FALSE(room.host.start(5, 6, room.now));                                           // a start while one is under way is refused: not called again
        ASSERT_EQ(calls, 1);
        // a cancelled start, and the next one
        room.host.cancel();
        room.run(100);
        ASSERT_TRUE(room.host.phase() == HostLobby::Phase::Room);
        log.clear();
        ASSERT_TRUE(room.host.start(888, 0xABCDEFull, room.now));
        ASSERT_EQ(calls, 2);
        ASSERT_TRUE(seen.seed == 888 && std::find(log.begin(), log.end(), "hook") < std::find(log.begin(), log.end(), "Start"));
        // a lobby with no function starts as it did
        Room plain(keyed_server_config(78));
        plain.join_seat("Cy");
        plain.join_seat("Di");
        ASSERT_TRUE(plain.host.start(9, 9, plain.now));
        plain.run(100);
        ASSERT_TRUE(plain.guests[0].lobby->phase() == ClientLobby::Phase::Loading);
    } TEST_END();

    TEST_CASE("N4.22 The Leader Moves The Colours (Protocol 14): The Leader Of A Server's Room Puts A Player In An Empty Seat, With Key, Name, Thumb And Place In The Order; Everybody Is Sent The New Seats And The Moved Player Is Told By The Room; Only The Leader Is Heard, Only While The Room Is Open; What Cannot Be Done (A Colour That Anyone Holds Included) Is Ignored And Counted Like A Second Click On START, Garbage And A Flood Cost The Sender Its Seat") {
        const auto layout = [](const RoomMsg& r) {                       // who sits where, in the order of the seats: "Ann@0 Bob@2" (a seat that nobody holds is not listed)
            std::string out;
            for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
                if (r.slots[s].state == SlotState::Empty) continue;
                out += (out.empty() ? "" : " ") + r.slots[s].name + "@" + std::to_string(static_cast<unsigned>(s));
            }
            return out;
        };
        const auto notices_of = [](ClientLobby& lobby) {                 // what the room said to this player (a notice is a line of the room itself), and any line of a person
            std::vector<std::string> out;
            for (const ChatLine& l : lobby.take_chat()) out.push_back(l.notice() ? l.text : "(a line of " + l.name + ")");
            return out;
        };
        const auto leader_starts = [](HostLobby& host) {                 // the seats of the LeaderStart events
            std::vector<uint8_t> seats;
            for (const auto& e : host.take_events()) {
                if (e.type == HostLobby::Event::Type::LeaderStart) seats.push_back(e.seat);
            }
            return seats;
        };
        const auto leader_plans = [](HostLobby& host) {                  // the LeaderStart events as "seat:levels": the seat of the leader that asked, then the level of each seat ("2:0301": None, Hard, None, Easy ... as numbers)
            std::vector<std::string> plans;
            for (const auto& e : host.take_events()) {
                if (e.type != HostLobby::Event::Type::LeaderStart) continue;
                std::string plan = std::to_string(static_cast<unsigned>(e.seat)) + ":";
                for (const FillLevel level : e.fill) plan += std::to_string(static_cast<unsigned>(level));
                plans.push_back(plan);
            }
            return plans;
        };
        ASSERT_TRUE(seat_colour_word(0) == "Green" && seat_colour_word(1) == "Red" && seat_colour_word(2) == "Blue" && seat_colour_word(3) == "Black" && seat_colour_word(4).empty());
        {   // an empty seat: the leader puts Bob in the Black seat; he moves, nobody else does; he is told, the leader and Cat are not; every guest sees the same seats and knows its own; the leader stays the leader
            Room room(keyed_server_config(21));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const auto bob_rtt = room.host.room().slots[1].rtt_ms;
            ASSERT_TRUE(bob_rtt != kRttUnknown);                         // (the thumb was measured)
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(1, 1));                                       // a client sends nothing that is no message: the same seat twice, a number that is no seat
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(1, 4));
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(4, 1));
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(255, 255));
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@2 Bob@3");
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(layout(room.guests[g].lobby->room()), "Ann@0 Cat@2 Bob@3");
            ASSERT_TRUE(room.guests[ann].lobby->my_seat() == 0 && room.guests[bob].lobby->my_seat() == 3 && room.guests[cat].lobby->my_seat() == 2);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && room.host.leader() == 0 && room.guests[bob].lobby->room().leader == 0);
            ASSERT_TRUE(!room.guests[bob].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
            ASSERT_TRUE(key_matches(room.host.key_of(3), bob_key) && key_is_zero(room.host.key_of(1)));          // the key is the player's, wherever the player sits
            ASSERT_EQ(room.host.room().slots[3].rtt_ms, bob_rtt);                                                // ... and so is the thumb
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Black."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[cat].lobby).empty());
            ASSERT_EQ(room.host.seat_moves(), 1u);
            ASSERT_EQ(room.host.ignored_seat_moves(), 0u);
            ASSERT_TRUE(room.host.occupied(3) && !room.host.occupied(1) && room.host.players() == 3u);
        }
        {   // a colour that a player holds is not taken from the player: the move is ignored and counted, nobody is told; two players change places in three moves, through the colour that is free
            Room room(keyed_server_config(22));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const SeatKey cat_key = room.guests[cat].lobby->key();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 1));                                        // Cat (Blue) onto Bob's seat (Red)
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 0));                                        // Bob onto the leader's own seat
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 2u);
            for (const size_t g : {ann, bob, cat}) ASSERT_TRUE(notices_of(*room.guests[g].lobby).empty());
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                                        // Bob to black: red is free
            room.run(300);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 1));                                        // Cat to red
            room.run(300);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(3, 2));                                        // Bob to blue, which Cat left
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bob@2");
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(layout(room.guests[g].lobby->room()), "Ann@0 Cat@1 Bob@2");
            ASSERT_TRUE(room.guests[bob].lobby->my_seat() == 2 && room.guests[cat].lobby->my_seat() == 1 && room.guests[ann].lobby->my_seat() == 0);
            ASSERT_TRUE(key_matches(room.host.key_of(1), cat_key) && key_matches(room.host.key_of(2), bob_key) && key_is_zero(room.host.key_of(3)));
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Black.", "Ann moved you to Blue."}));
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Red."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
            ASSERT_TRUE(room.host.seat_moves() == 3u && room.host.ignored_seat_moves() == 2u);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && room.host.leader() == 0);
        }
        {   // the leader moves itself, and the lead goes with the person: everybody is told who leads, nobody is told of the move (the notice of the room is for the player that is moved)
            Room room(keyed_server_config(23));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            for (const size_t g : {ann, bob}) room.guests[g].lobby->take_chat();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(0, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Bob@1 Ann@2");
            ASSERT_TRUE(room.host.leader() == 2 && room.guests[ann].lobby->room().leader == 2 && room.guests[bob].lobby->room().leader == 2);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && !room.guests[bob].lobby->is_leader() && room.guests[ann].lobby->my_seat() == 2);
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[bob].lobby).empty());
            const size_t cat = room.join_seat("Cat");                                                           // a player who comes now takes the colour that is free first (green), and is the newest: Ann leads still
            ASSERT_EQ(room.guests[cat].lobby->my_seat(), 0);
            room.run(300);
            ASSERT_TRUE(room.host.leader() == 2 && room.guests[ann].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
            leader_starts(room.host);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());                                               // a START of the leader is heard with the seat that it holds now
            room.run(300);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{2}));
            room.guests[ann].lobby->leave();                                                                    // when the leader goes, the earliest of those left leads: Bob (seat 1), not the lowest seat (Cat, 0)
            room.run(300);
            ASSERT_EQ(room.host.leader(), 1);
            ASSERT_TRUE(room.guests[bob].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
        }
        {   // the order of the Welcomes is the player's, so a player that moves keeps its place in it: Bob moved past Cat still leads before her when Ann leaves
            Room room(keyed_server_config(24));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                                       // Bob to black: Cat (seat 2) is now between Ann and Bob
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@2 Bob@3");
            room.guests[ann].lobby->leave();
            room.run(300);
            ASSERT_EQ(room.host.leader(), 3);                                                                   // Bob came before Cat: seat 3 now
            ASSERT_TRUE(room.guests[bob].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
        }
        {   // a guest that does not lead is not heard: the first 16 of its messages are free, each of the next seven is a violation, the 24th throws it out; nothing moves, the leader notices nothing
            Room room(keyed_server_config(25));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            ASSERT_FALSE(room.guests[bob].lobby->request_seat_move(2, 3));                                      // a guest knows that it does not lead: nothing is sent
            room.run(100);
            ASSERT_EQ(room.host.ignored_seat_moves(), 0u);
            for (uint32_t i = 0; i < kIgnoredSeatMovesAllowed; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3}));     // a client that is not the game's sends it anyway
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 16u);
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom);       // no offence
            for (int i = 0; i < 7; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3}));                           // violations one to seven
            room.run(300);
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.host.ignored_seat_moves() == 23u);
            for (int i = 0; i < 100; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3}));                        // the first of them is the eighth: out; the rest is never read
            room.run(300);
            ASSERT_FALSE(room.host.occupied(bob_seat));
            ASSERT_EQ(room.host.ignored_seat_moves(), 24u);
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob].lobby->reject_reason() == RejectReason::BadRequest);
            ASSERT_TRUE(room.host.leader() == 0 && room.host.seat_moves() == 0u && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[cat].lobby).empty());
        }
        {   // what the leader asks for and cannot be had is ignored and counted, no offence: an empty seat, a bot's seat in either place, a colour that another player holds, a player who left meanwhile; the rule's own refusals (move_seat) change nothing either
            Room room(keyed_server_config(26));
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const std::string before = layout(room.host.room());
            ASSERT_EQ(before, "Ann@0 Bob@1 Bot (Easy)@3");
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 1));                                       // nobody sits in seat 2
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                                       // the bot's seat is not taken from it
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(3, 2));                                       // ... and a bot does not move
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 0));                                       // a player is not put onto the colour of another player (the leader's own)
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), before);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 4u);
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom && room.host.leader() == 0);
            room.guests[bob].lobby->leave();                                                                    // Bob goes; Ann's press that was meant for him is the next one
            room.run(300);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bot (Easy)@3");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 5u);
            // the rule itself: false and nothing changes for a seat that holds no guest, the same seat twice, a number that is no seat, a colour that is held (a bot's, a guest's)
            const size_t cat = room.join_seat("Cat");
            ASSERT_EQ(room.guests[cat].lobby->my_seat(), 1);
            room.run(300);
            const std::string with_cat = layout(room.host.room());
            ASSERT_TRUE(!room.host.move_seat(2, 1) && !room.host.move_seat(1, 1) && !room.host.move_seat(1, 4) && !room.host.move_seat(4, 1) && !room.host.move_seat(255, 1) && !room.host.move_seat(1, 255));
            ASSERT_TRUE(!room.host.move_seat(1, 3) && !room.host.move_seat(3, 1) && !room.host.move_seat(3, 2) && !room.host.move_seat(1, 0) && !room.host.move_seat(0, 1));
            ASSERT_EQ(layout(room.host.room()), with_cat);
            ASSERT_EQ(room.host.seat_moves(), 0u);
            ASSERT_TRUE(room.host.move_seat(1, 2));                                                             // (the owner of the lobby may move a guest too: the rule is the lobby's)
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@2 Bot (Easy)@3");
            ASSERT_EQ(room.guests[cat].lobby->my_seat(), 2);
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Blue."}));       // (the leader's name: it is the room's leader who is named)
            ASSERT_EQ(room.host.seat_moves(), 1u);
        }
        {   // the room is loading: a late press of the leader is ignored and counted, the rule refuses, the leader's screen does not send one; the room is open again after a cancel and the move works
            Room room(keyed_server_config(27));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            ASSERT_TRUE(room.host.start(5, 5, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(1, 3));                                      // the client knows: nothing is sent
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 3}));                                       // a press that crossed the Start does arrive
            room.run(100);
            ASSERT_FALSE(room.host.move_seat(1, 3));
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && room.host.occupied(room.guests[ann].lobby->my_seat()));
            for (uint32_t i = 1; i < kIgnoredSeatMovesAllowed; ++i) room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 3}));     // ... as many as the sixteen that are free, in one tick: no budget of the done moves is spent, no offence
            room.run(100);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == kIgnoredSeatMovesAllowed && room.host.occupied(room.guests[ann].lobby->my_seat()));
            room.host.cancel();
            room.run(200);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(200);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@3");
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 3);
        }
        {   // garbage (the same seat twice, a seat that no room has, a missing or an extra byte) is a violation each, from the leader as from anybody; the eighth throws the sender out, and the next one leads
            Room room(keyed_server_config(28));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const std::vector<std::vector<uint8_t>> garbage = {{31, 1, 1}, {31, 0, 4}, {31, 4, 0}, {31, 255, 255}, {31, 0}, {31}, {31, 0, 1, 2}};
            for (const auto& bytes : garbage) room.guests[ann].client_end->send(bytes);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(0) && room.host.leader() == 0);                                      // seven violations
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 0u);                  // (garbage is no request: it is not counted as one)
            room.guests[ann].client_end->send(std::vector<uint8_t>{31, 2, 2});                                  // the eighth
            room.run(300);
            ASSERT_FALSE(room.host.occupied(0));
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[ann].lobby->reject_reason() == RejectReason::BadRequest);
            ASSERT_TRUE(room.host.leader() == 1 && room.guests[bob].lobby->is_leader());
        }
        for (const int presses : {25, 26}) {   // the budget of a leader's presses is a person's: a burst of 6 is done, the next 12 are dropped (no answer, no count), every one after them is a violation, the eighth throws the leader out: of the presses of one millisecond the 25th is the seventh violation (she stays) and the 26th the eighth (she goes)
            Room room(keyed_server_config(presses == 25 ? 29 : 37));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            for (const size_t g : {ann, bob}) room.guests[g].lobby->take_chat();
            for (int i = 0; i < presses; ++i) {                                                                 // the presses in the same millisecond: Bob bounces between seat 1 and 2 six times, and the rest press him on to seat 2 again
                const bool back = i < 6 && i % 2 == 1;                                                          // (every one of them can be done: the dropped ones leave Bob in seat 1)
                room.guests[ann].client_end->send(encode(SeatMoveMsg{back ? uint8_t{2} : uint8_t{1}, back ? uint8_t{1} : uint8_t{2}}));
            }
            room.run(300);
            ASSERT_EQ(room.host.seat_moves(), kSeatMoveBurst);
            ASSERT_EQ(room.host.ignored_seat_moves(), 0u);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby).size(), size_t{6});                                  // each of the six moves told Bob (to seat 2 three times, back to seat 1 three times): a notice of the room each
            if (presses == 25) {
                ASSERT_TRUE(room.host.occupied(0) && room.host.leader() == 0 && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);       // 6 done + 12 dropped + 7 violations
                ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");
            } else {
                ASSERT_FALSE(room.host.occupied(0));                                                            // 6 done + 12 dropped + 8 violations: the 26th message throws her out
                ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[ann].lobby->reject_reason() == RejectReason::BadRequest);
                ASSERT_TRUE(room.host.leader() == 1 && room.guests[bob].lobby->is_leader());                    // (six moves: Bob is where he started)
                ASSERT_EQ(layout(room.host.room()), "Bob@1");
            }
        }
        {   // a person's presses never meet it: one every quarter of a second for as long as a finger keeps at it, and two taps at once
            Room room(keyed_server_config(30));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            for (int i = 0; i < 200; ++i) {
                const uint8_t from = i % 2 == 0 ? 1 : 2;
                const uint8_t to = i % 2 == 0 ? 2 : 1;
                ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(from, to));
                room.run(250);
                room.guests[bob].lobby->take_chat();
            }
            ASSERT_EQ(room.host.seat_moves(), 200u);
            ASSERT_TRUE(room.host.ignored_seat_moves() == 0u && room.host.occupied(0) && room.host.leader() == 0);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");                                                // (an even number of moves: where he started)
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));                                       // a double tap: the second asks for a move that is not there any more, and the room ignores it
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@2");
            ASSERT_TRUE(room.host.seat_moves() == 201u && room.host.ignored_seat_moves() == 1u && room.host.occupied(0));
        }
        {   // a START that was heard before a move, in the same pass, names the seat that its leader holds after it: the room's owner compares it with leader() and the request is not lost
            Room room(keyed_server_config(31));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            leader_starts(room.host);
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));
            room.guests[ann].client_end->send(encode(SeatMoveMsg{0, 2}));
            room.run(300);
            ASSERT_EQ(room.host.leader(), 2);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{2}));
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));                                       // the same on the way back, to a lower seat
            room.guests[ann].client_end->send(encode(SeatMoveMsg{2, 0}));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");
            ASSERT_EQ(room.host.leader(), 0);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{0}));
            room.guests[ann].client_end->send(encode(SeatMoveMsg{0, 3}));                                       // and a START after a move is heard with the new seat
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));
            room.run(300);
            ASSERT_EQ(room.host.leader(), 3);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{3}));
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom && room.host.ignored_start_requests() == 0u);
        }
        {   // the plan of bots of a START that was heard before a move is by colour, so it goes with the colours: the bot that was for the colour that the player takes is for the colour that it leaves; every START that waits has it, whoever moves, and a START after the move is the client's own
            using F = FillLevel;
            Room room(keyed_server_config(34));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(300);
            leader_plans(room.host);
            const auto ask = [&](F green, F red, F blue, F black) {
                StartRequestMsg request;
                request.fill = {green, red, blue, black};
                room.guests[ann].client_end->send(encode(request));
            };
            const auto move = [&](uint8_t from, uint8_t to) { room.guests[ann].client_end->send(encode(SeatMoveMsg{from, to})); };
            ask(F::None, F::None, F::Hard, F::Easy);                                                            // a Hard bot for Blue, an Easy one for Black ...
            move(1, 2);                                                                                         // ... and Bob takes Blue: the Hard bot is for Red now, the Easy one is where it was
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"0:0301"}));
            ask(F::Medium, F::None, F::Hard, F::Easy);                                                          // the leader moves itself (Green to Red): the levels of both colours trade places, and the START names the seat that the leader holds now
            move(0, 1);
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"1:0231"}));
            ask(F::Easy, F::None, F::Hard, F::None);                                                            // two STARTs wait for the same move of somebody else: both are made for the new seats, the leader is where it was
            ask(F::None, F::Medium, F::None, F::Hard);
            move(2, 3);                                                                                         // Bob goes on to Black
            ask(F::Hard, F::Easy, F::Medium, F::None);                                                          // ... and a START that comes after it is made for the seats as they are: it is left as it is
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"1:1003", "1:0230", "1:3120"}));
            ASSERT_TRUE(room.host.seat_moves() == 3u && room.host.ignored_seat_moves() == 0u && room.host.ignored_start_requests() == 0u);
            ASSERT_EQ(layout(room.host.room()), "Ann@1 Bob@3");
        }
        {   // what cannot be done costs no part of the budget of what can: ten requests that the room ignores, in the same tick, leave the leader its six moves, and one that is ignored after the six is counted, not dropped
            Room room(keyed_server_config(35));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(300);
            const auto send = [&](uint8_t from, uint8_t to) { room.guests[ann].client_end->send(encode(SeatMoveMsg{from, to})); };
            for (int i = 0; i < 10; ++i) send(1, 2);                                                            // Bob (Red) onto Cat's colour (Blue): ignored and counted, ten times
            send(1, 3);                                                                                         // Bob to Black: done, with a budget that the ten did not touch
            for (int i = 0; i < 5; ++i) send(i % 2 == 0 ? 3 : 1, i % 2 == 0 ? 1 : 3);                           // five more, Bob there and back: the six of the burst are spent, Bob is on Red again
            send(3, 0);                                                                                         // nobody sits in Black now: ignored and counted, not dropped
            send(1, 3);                                                                                         // a seventh move that could be done: dropped, counted by nobody
            room.run(300);
            ASSERT_EQ(room.host.seat_moves(), kSeatMoveBurst);
            ASSERT_EQ(room.host.ignored_seat_moves(), 11u);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.occupied(0) && room.host.leader() == 0 && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
        }
        {   // a leader who gave no name is "Player 1" in the room's notice, as everywhere in the lobby
            Room room(keyed_server_config(36));
            const size_t ann = room.join_seat("");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            ASSERT_TRUE(room.host.room().slots[0].name.empty());
            room.guests[bob].lobby->take_chat();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(300);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Player 1 moved you to Blue."}));
        }
        {   // the key is the player's: after a move a Hello with it takes over the NEW seat, whatever seat the Hello asks for (a page that was reloaded with the old seat in its address); the seat that was left is free for a new player
            Room room(keyed_server_config(32));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const SeatKey bob_key = room.guests[bob].lobby->key();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(300);
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 3);
            const size_t again = join_keyed(room, "Bob", bob_key, 1);                                           // asks for seat 1, shows Bob's key
            room.run(300);
            ASSERT_TRUE(room.guests[again].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[again].lobby->my_seat() == 3);
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob].lobby->reject_reason() == RejectReason::Superseded);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@3");
            ASSERT_TRUE(key_matches(room.host.key_of(3), bob_key) && room.host.players() == 2u);
            const size_t cat = room.join_seat("Cat", 1);                                                        // the seat that Bob left
            ASSERT_EQ(room.guests[cat].lobby->my_seat(), 1);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bob@3");
        }
        {   // a host that holds a seat (a game on the local network) has no leader: a guest's SeatMove is ignored and counted like a StartRequest, and the rule never moves the host's own seat
            Room lan;
            lan.join_seat("Ann");
            const size_t bob = lan.join_seat("Bob");
            lan.run(300);
            ASSERT_EQ(lan.host.leader(), kNoLeader);
            ASSERT_FALSE(lan.guests[bob].lobby->request_seat_move(1, 2));
            lan.guests[bob].client_end->send(encode(SeatMoveMsg{1, 2}));
            lan.run(300);
            ASSERT_TRUE(lan.host.seat_moves() == 0u && lan.host.ignored_seat_moves() == 1u && lan.host.occupied(lan.guests[bob].lobby->my_seat()));
            ASSERT_FALSE(lan.host.move_seat(0, 2));                                                             // (seat 0 is the host's own: it holds no guest)
            ASSERT_FALSE(lan.host.move_seat(1, 0));
            ASSERT_EQ(lan.host.seat_moves(), 0u);
            lan.guests[0].lobby->take_chat();
            ASSERT_TRUE(lan.host.move_seat(1, 3));                                                              // (the owner of a lobby that has no leader moves Ann: the room says so in its own name)
            lan.run(300);
            ASSERT_EQ(lan.guests[0].lobby->my_seat(), 3);
            ASSERT_EQ(notices_of(*lan.guests[0].lobby), (std::vector<std::string>{"The room moved you to Black."}));
            ASSERT_EQ(lan.host.seat_moves(), 1u);
            for (int i = 0; i < 100; ++i) lan.guests[bob].client_end->send(encode(SeatMoveMsg{1, 2}));
            lan.run(300);
            ASSERT_FALSE(lan.host.occupied(lan.guests[bob].lobby->my_seat()));                                 // the same ladder: out at the 24th
            ASSERT_EQ(lan.host.ignored_seat_moves(), 24u);
        }
        {   // what happened stays as it happened: a Joined that waits to be read keeps the seat that the guest joined at when the owner of the lobby moves the guest; only the START of a leader goes with the lead (the plans, above)
            Room lan;
            lan.join_seat("Ann");
            lan.run(300);
            lan.host.take_events();
            const size_t bob = lan.join_seat("Bob");
            const uint8_t joined_at = lan.guests[bob].lobby->my_seat();
            ASSERT_TRUE(lan.host.move_seat(joined_at, 3));
            size_t joins = 0;
            for (const auto& e : lan.host.take_events()) {
                if (e.type != HostLobby::Event::Type::Joined) continue;
                ++joins;
                ASSERT_EQ(e.seat, joined_at);
            }
            ASSERT_EQ(joins, size_t{1});
        }
        {   // a room that does not allow an early start has no leader: nobody's move is heard
            HostLobby::Config no = keyed_server_config(33);
            no.early_start = false;
            Room room(no);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(300);
            ASSERT_EQ(room.host.leader(), kNoLeader);
            ASSERT_FALSE(room.guests[ann].lobby->request_seat_move(0, 2));
            room.guests[ann].client_end->send(encode(SeatMoveMsg{0, 2}));
            room.run(300);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && layout(room.host.room()) == "Ann@0 Bob@1");
        }
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
