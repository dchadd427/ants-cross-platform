// Tests of the room before a match: the lobby messages, joining and refusing, leaving, the start barrier (Start, Loaded, Begin), cancelled
// starts, kicking, hostile guests, and a whole match started through the lobby.
#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
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

    explicit Room(HostLobby::Config hc = {}) : host(hc) { host.set_map("TINY.LVL"); }      // a room has a map once its host has chosen one

    Guest& join(const std::string& name, LoopbackNetwork::Link link = {10, 0}) {
        auto ends = net.connect(link);
        guests.emplace_back();
        Guest& g = guests.back();
        g.host_end = ends.first;
        g.client_end = ends.second;
        ClientLobby::Config cc;
        cc.name = name;
        g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
        host.add_connection(ends.first, now);
        return g;
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
        b[b.size() - 1] = 4;                                              // you = 4 (only 255 or 0..3)
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
        // every strict prefix and one extra byte are rejected
        for (const auto& m : {encode(r), encode(s), encode(l), encode(c)}) {
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
        const std::vector<std::vector<uint8_t>> seeds = {encode(r), encode(s), encode(l), encode(c)};
        size_t accepted = 0;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
            for (uint32_t m = 1 + rng.below(3); m > 0; --m) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
            if (rng.below(5) == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            RoomMsg a; StartMsg bb; LoadedMsg cc; CancelMsg dd;
            if (decode(buf, a)) { ++accepted; ASSERT_TRUE(encode(a) == buf); ASSERT_TRUE(valid_map_name(a.map_name)); }
            if (decode(buf, bb)) { ++accepted; ASSERT_TRUE(encode(bb) == buf); ASSERT_TRUE(valid_map_name(bb.map_name)); }
            if (decode(buf, cc)) { ++accepted; ASSERT_TRUE(encode(cc) == buf); }
            if (decode(buf, dd)) { ++accepted; ASSERT_TRUE(encode(dd) == buf); }
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
        // the current layout must be exact
        std::vector<uint8_t> current = encode(h);
        current.push_back(0);
        ASSERT_FALSE(decode(current.data(), current.size(), h2));
        std::vector<uint8_t> shorter = encode(h);
        shorter.pop_back();
        ASSERT_FALSE(decode(shorter.data(), shorter.size(), h2));                // (the version is the current one, so the seat byte is required)
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
        ASSERT_TRUE(host.turns_sealed() > 250);
        for (int i = 1; i < 4; ++i) ASSERT_TRUE(sims[static_cast<size_t>(i)]->state_hash() == sims[0]->state_hash());
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
