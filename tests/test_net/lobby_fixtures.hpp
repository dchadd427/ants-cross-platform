// The fixtures of the room tests (test_lobby.cpp holds main(); the cases are in test_lobby_basics.cpp, test_lobby_rejoin.cpp, test_lobby_rooms.cpp and test_lobby_rematch.cpp).
// Inline or in an anonymous namespace.
#pragma once
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
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::net;
using ants::sim::AntType;
using ants::sim::Command;
using ants::sim::CommandType;
using ants::sim::TileCoord;

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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

#define ASSERT_MSG(cond, msg) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

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
        bool frozen{false};                      // the machine is asleep: its link is open and it says nothing (its lobby is not run)
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
            for (auto& g : guests) {
                if (!g.frozen) g.lobby->update(now);
            }
        }
    }
};

// A server's room (the host holds no seat) that gives every guest a key. The keys come from a seeded generator, so that a test can tell them apart and repeat them.
inline HostLobby::Config keyed_server_config(uint32_t seed, const std::string& room_code = std::string()) {
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
inline SeatKey key_with(uint8_t salt) {
    SeatKey k{};
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(salt * 31u + i * 7u + 1u);
    return k;
}

// What a Hello of protocols 6 to 9 looked like (they shared the layout: protocol 9 changed the rules of the match, not a message): no key, no turns. Protocol 10 added both.
inline std::vector<uint8_t> old_layout_hello(uint16_t version, const std::string& name, const std::string& room = std::string(), const std::string& token = std::string()) {
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
inline size_t join_keyed(Room& r, const std::string& name, const SeatKey& key, uint8_t want_seat = 255, const std::string& room_code = std::string()) {
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

// A guest with a configuration of its own (a platform, a create block ...), joined to the room like Room::join does; returns its index
inline size_t join_with(Room& r, const ClientLobby::Config& cc) {
    auto ends = r.net.connect({10, 0});
    r.guests.emplace_back();
    Room::Guest& g = r.guests.back();
    g.host_end = ends.first;
    g.client_end = ends.second;
    g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
    r.host.add_connection(ends.first, r.now);
    return r.guests.size() - 1;
}

// A raw client (no lobby behind it) that has said Hello: what the host answers can be read from `end`
struct RawClient {
    Connection* host_end{nullptr};
    Connection* end{nullptr};
};
inline RawClient raw_hello(Room& r, const HelloMsg& hello) {
    auto ends = r.net.connect({10, 0});
    r.host.add_connection(ends.first, r.now);
    ends.second->send(encode(hello));
    return RawClient{ends.first, ends.second};
}
// Every message that waits on a connection
inline std::vector<std::vector<uint8_t>> drain_messages(Connection* c) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> m;
    while (c->poll(m)) out.push_back(m);
    return out;
}

// Who sits where, in the order of the seats: "Ann@0 Bob@2" (a seat that nobody holds is not listed)
inline std::string layout(const RoomMsg& r) {
    std::string out;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (r.slots[s].state == SlotState::Empty) continue;
        out += (out.empty() ? "" : " ") + r.slots[s].name + "@" + std::to_string(static_cast<unsigned>(s));
    }
    return out;
}
// What the room said to this player (a notice is a line of the room itself), and any line of a person
inline std::vector<std::string> notices_of(ClientLobby& lobby) {
    std::vector<std::string> out;
    for (const ChatLine& l : lobby.take_chat()) out.push_back(l.notice() ? l.text : "(a line of " + l.name + ")");
    return out;
}

// The seats of the LeaderStart events that wait on a host (they are taken)
inline std::vector<uint8_t> leader_starts(HostLobby& host) {
    std::vector<uint8_t> seats;
    for (const auto& e : host.take_events()) {
        if (e.type == HostLobby::Event::Type::LeaderStart) seats.push_back(e.seat);
    }
    return seats;
}
// The LeaderStart events that wait on a host as "seat:levels" (they are taken): the seat of the leader that asked, then the level of each seat ("2:0301": None, Hard, None, Easy ... as numbers)
inline std::vector<std::string> leader_plans(HostLobby& host) {
    std::vector<std::string> plans;
    for (const auto& e : host.take_events()) {
        if (e.type != HostLobby::Event::Type::LeaderStart) continue;
        std::string plan = std::to_string(static_cast<unsigned>(e.seat)) + ":";
        for (const FillLevel level : e.fill) plan += std::to_string(static_cast<unsigned>(level));
        plans.push_back(plan);
    }
    return plans;
}

// The leader's screen as a script, for the presses that a test sends as raw bytes: every press is made for the seats as the screen shows them (its guard is their seating_hash), and a press that the room
// will do changes what the screen shows (one that it will not do, or drops, leaves it as it is)
struct Screen {
    RoomMsg shown;
    std::vector<uint8_t> press(uint8_t from, uint8_t to, bool done = true) {
        const std::vector<uint8_t> bytes = encode(SeatMoveMsg{from, to, seating_hash(shown)});
        if (done) std::swap(shown.slots[from], shown.slots[to]);
        return bytes;
    }
    // The same for the leader's press on a person's Remove (protocol 16): the seat is empty on the screen once the room does it
    std::vector<uint8_t> remove(uint8_t seat, bool done = true) {
        const std::vector<uint8_t> bytes = encode(RemoveMsg{seat, seating_hash(shown)});
        if (done) shown.slots[seat] = RoomMsg::Slot{};
        return bytes;
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


// A lobby room (protocol 16): a server's room that gives keys (a seat is held for a key), has a leader and waits for it to start
inline HostLobby::Config lobby_room_config(uint32_t seed) {
    HostLobby::Config hc = keyed_server_config(seed);
    hc.early_start = true;
    hc.leader_starts = true;
    hc.lobby_room = true;
    return hc;
}

// What the front page is in a Hello: a lobby page, which cannot play a match
inline ClientLobby::Config page_config(const std::string& name) {
    ClientLobby::Config cc;
    cc.name = name;
    cc.client_kind = kClientPage;
    return cc;
}

// The plan of a room as a short text: the kind of each colour as a letter (o open, e easy, m medium, h hard, n nobody), then the team pair ("-": none): "oehn 0+3"
inline std::string plan_text(const RoomMsg& r) {
    static const char kLetters[] = {'o', 'e', 'm', 'h', 'n'};
    std::string out;
    for (const PlanKind kind : r.plan) out += kLetters[static_cast<size_t>(kind)];
    const bool none = r.team_a == kNoTeam && r.team_b == kNoTeam;
    return out + " " + (none ? std::string("-") : std::to_string(static_cast<unsigned>(r.team_a)) + "+" + std::to_string(static_cast<unsigned>(r.team_b)));
}

// Which colours hold a game, as the room shows it: "1011" (the colour of a page, of a guest whose seat is held and of nobody is 0)
inline std::string games_text(const RoomMsg& r) {
    std::string out;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out += r.seat_in_game(seat) ? '1' : '0';
    return out;
}

// A leader's plan: what each colour is, the team pair and the map
inline PlanMsg plan_of(const std::array<PlanKind, sim::MAX_PLAYERS>& kinds, uint8_t team_a = kNoTeam, uint8_t team_b = kNoTeam, const std::string& map = std::string()) {
    PlanMsg m;
    m.map_name = map;
    m.plan = kinds;
    m.team_a = team_a;
    m.team_b = team_b;
    return m;
}

}  // namespace

// The groups of cases, run by main() in test_lobby.cpp in this order
void run_lobby_basics_tests();
void run_lobby_rejoin_tests();
void run_lobby_rooms_tests();
void run_lobby_rematch_tests();
