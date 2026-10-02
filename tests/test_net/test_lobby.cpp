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
        const std::vector<std::vector<uint8_t>> seeds = {encode(r), encode(s), encode(l), encode(c), encode(srv), request};
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
            if (decode(buf, ee)) { ++accepted; ASSERT_TRUE(buf == request); }
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
        // protocol 6 had this very Hello layout (but not this protocol's Room message): the version number alone makes it "version mismatch"
        HelloMsg six = h;
        six.version = 6;
        const std::vector<uint8_t> v6_hello = encode(six);
        ASSERT_TRUE(decode(v6_hello.data(), v6_hello.size(), old));
        ASSERT_EQ(old.version, 6);
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
        // a client of protocol 6 (the version before the room's leader): the same answer, from a LAN host and from a server's room alike
        for (const bool server : {false, true}) {
            HostLobby::Config hc;
            if (server) hc.host_seat = 255;
            Room r6(hc);
            auto e6 = r6.net.connect({10, 0});
            r6.host.add_connection(e6.first, 0);
            e6.second->send(v6_hello);
            r6.run(100);
            ASSERT_TRUE(e6.second->poll(reply) && decode(reply, rj) && rj.reason == RejectReason::VersionMismatch);
            ASSERT_EQ(r6.host.players(), server ? size_t{0} : size_t{1});          // nobody was seated, and no leader named
            ASSERT_EQ(r6.host.leader(), kNoLeader);
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

    TEST_CASE("N4.11 StartRequest In The Room: Only The Leader Is Heard, Only While The Room Can Start; Everything Else Is Ignored And Counted And Costs The Sender Nothing (Up To The 16 That A Person Could Send, N4.13 Has The Flood); A Payload Is Garbage; A Host With A Seat Ignores It") {
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
        {   // a payload is garbage, from the leader and from anybody: eight of them throw the sender out (a violation like any message that a guest may not send)
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send({static_cast<uint8_t>(MsgType::StartRequest), static_cast<uint8_t>(i)});
            room.run(200);
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));    // seven are not enough
            room.guests[ann].client_end->send({static_cast<uint8_t>(MsgType::StartRequest), 9});
            room.run(300);
            ASSERT_FALSE(room.host.occupied(room.guests[ann].lobby->my_seat()));   // the eighth
            ASSERT_EQ(room.host.leader(), room.guests[bob].lobby->my_seat());      // the leader that was thrown out is replaced
            ASSERT_EQ(room.host.ignored_start_requests(), 0u);                     // (garbage is no request)
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

    TEST_CASE("N4.12 The Leader's Side: request_start() Sends One Byte, And Only From The Leader's Open Room; A Guest, A Machine That Is Not In The Room Yet, One That Is Loading Or Gone Sends Nothing") {
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
        ASSERT_TRUE(one[0].size() == 1 && one[0][0] == static_cast<uint8_t>(MsgType::StartRequest));   // exactly the type byte
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

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
