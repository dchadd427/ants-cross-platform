// Tests of the bot seat in a room: the lobby (a computer player takes a seat, is shown as one, is never waited for, can never be faked by a name), the session (the host
// acknowledges its turns, so the sequencer never stalls; its commands carry its seat), a match with a bot seat on two machines (identical states), a host that leaves
// (the bot leaves with it), and a room on real sockets (AI5.1 .. AI5.6), and the fill of a room's empty seats at the host's START (AI5.7, protocol 11). SlotState::Bot has been part of protocol 6 from the start.
#include "ai_test.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include "ants_net/lobby.hpp"
#include "ants_net/loopback.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/session.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace ants::net;

namespace {

// ---- a room of a host lobby and guests on a loopback network ---------------------------------------------------------------------------------------

struct Room {
    LoopbackNetwork net{7};
    HostLobby host;
    struct Guest {
        Connection* host_end{nullptr};
        std::unique_ptr<ClientLobby> lobby;
    };
    std::vector<std::unique_ptr<Guest>> guests;
    uint32_t now{0};

    explicit Room(HostLobby::Config hc = {}) : host(std::move(hc)) { host.set_map("TINY.LVL"); }

    Guest& join(const std::string& name, uint8_t want_seat = 255) {
        auto ends = net.connect(LoopbackNetwork::Link{10, 0});
        guests.push_back(std::make_unique<Guest>());
        Guest& g = *guests.back();
        g.host_end = ends.first;
        ClientLobby::Config cc;
        cc.name = name;
        cc.want_seat = want_seat;
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
            for (auto& g : guests) g->lobby->update(now);
        }
    }
};

// ---- sessions ---------------------------------------------------------------------------------------------------------------------------------------

// The sink of the bot of a seat in a session: the sequencer of the host
class SessionSink final : public sim::CommandSink {
public:
    SessionSink(HostSession& host, uint8_t seat) : host_(host), seat_(seat) {}
    sim::CommandResult submit(const sim::Command& c) override {
        sim::CommandResult r;
        if (host_.submit_bot(seat_, c)) r.status = sim::CommandResult::Status::Applied;
        return r;
    }

private:
    HostSession& host_;
    uint8_t seat_;
};

sim::Command hatch_of(uint8_t issuer) {
    sim::Command c;
    c.type = CommandType::Hatch;
    c.issuer = issuer;
    return c;
}

// The time in which a bot of a room waits for the match's "Get ready to play!" dialog (the start hold): no command of a bot before it
constexpr uint32_t kStartHoldMs = sim::kMatchStartHoldTicks * sim::TICK_MS;

// A bot that sends one move per look with one of its ants, to a tile that changes every time
ScriptBot::Think wanderer(const std::vector<uint32_t>& ants, size_t& counter) {
    return [&ants, &counter](const BotView&, Orders& o) {
        const size_t n = counter++;
        o.move({ants[n % ants.size()]}, TileCoord{static_cast<int32_t>(8 + (n * 7) % 24), static_cast<int32_t>(8 + (n * 11) % 24)});
    };
}

// A host (seat 0), a guest (seat 1) and a computer player (seat 2, run by the host), each with an engine of its own
struct BotMatch {
    LoopbackNetwork net{5};
    sim::SimulationEngine host_sim;
    sim::SimulationEngine client_sim;
    std::unique_ptr<HostSession> host;
    std::unique_ptr<ClientSession> client;
    std::unique_ptr<SessionSink> sink;
    std::unique_ptr<BotController> bots;
    ScriptBot* bot{nullptr};
    std::vector<uint32_t> bot_ants;
    std::vector<std::pair<uint32_t, TileCoord>> bot_start;   // where the bot's ants stood at the start
    size_t counter{0};
    std::vector<std::pair<uint64_t, Command>> host_saw;      // what each machine's engine applied (tick, command)
    std::vector<std::pair<uint64_t, Command>> client_saw;
    uint32_t now{0};

    explicit BotMatch(uint32_t seed) {
        build_world(host_sim, seed);
        build_world(client_sim, seed);
        host = std::make_unique<HostSession>(host_sim, HostSession::Config{});
        auto ends = net.connect(LoopbackNetwork::Link{30, 5});
        host->add_client(1, ends.first);
        host->add_bot_seat(2);
        ClientSession::Config cc;
        cc.player = 1;
        client = std::make_unique<ClientSession>(client_sim, cc);
        client->set_connection(ends.second);
        bot_ants = ants_of(host_sim, 2);
        for (const auto& a : host_sim.get_world_state().ants) {
            if (a.player_id == 2) bot_start.emplace_back(a.id, TileCoord{a.tile_x, a.tile_y});
        }
        sink = std::make_unique<SessionSink>(*host, 2);
        bots = std::make_unique<BotController>(host_sim, 11);
        auto script = std::make_unique<ScriptBot>(wanderer(bot_ants, counter));
        bot = script.get();
        std::string why;
        BotSpec spec;
        spec.seat = 2;
        spec.level = Level::Medium;
        if (!bots->add(spec, std::move(script), *sink, why)) bot = nullptr;
        host->runner().set_on_tick([this]() { bots->on_tick(host_sim); });
        host->runner().set_on_command([this](const Command& c, const sim::CommandResult&) { host_saw.emplace_back(host_sim.current_tick(), c); });
        client->runner().set_on_command([this](const Command& c, const sim::CommandResult&) { client_saw.emplace_back(client_sim.current_tick(), c); });
        host->start(0);
        client->start(0);
    }
    void run(uint32_t ms, bool humans = true) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            if (humans && now % 500 == 0) {
                const std::vector<uint32_t> a0 = ants_of(host_sim, 0);
                const std::vector<uint32_t> a1 = ants_of(client_sim, 1);
                Command c;
                c.type = CommandType::GroupMove;
                c.tile_x = static_cast<int16_t>(10 + (now / 500) % 30);
                c.tile_y = static_cast<int16_t>(12 + (now / 250) % 30);
                c.ants = {a0[(now / 500) % a0.size()]};
                host->submit_local(c);
                c.ants = {a1[(now / 500) % a1.size()]};
                client->submit(c);
            }
            host->update(now);
            client->update(now);
        }
    }
};

std::vector<std::pair<uint64_t, Command>> of_issuer(const std::vector<std::pair<uint64_t, Command>>& all, uint8_t issuer) {
    std::vector<std::pair<uint64_t, Command>> out;
    for (const auto& e : all) {
        if (e.second.issuer == issuer && e.second.type != CommandType::Drop) out.push_back(e);
    }
    return out;
}

// ---- a three-machine mesh for host migration: seat 0 hosts and runs the bot at seat 3, seats 1 and 2 are guests linked to each other -----------------------

struct BotMesh {
    LoopbackNetwork net{17};
    sim::SimulationEngine sims[3];
    std::unique_ptr<HostSession> hosts[3];
    std::unique_ptr<ClientSession> clients[3];
    Connection* link[3][3] = {};
    std::unique_ptr<SessionSink> sink;
    std::unique_ptr<BotController> bots;
    std::vector<uint32_t> bot_ants;
    size_t counter{0};
    std::vector<std::pair<uint8_t, uint8_t>> left;           // (host seat, player) of every drop-out a host announced
    std::vector<std::pair<uint64_t, Command>> saw1;          // what the engine of seat 1 applied
    bool host_alive{true};
    uint32_t now{0};
    int promotions{0};

    BotMesh() {
        for (auto& s : sims) build_world(s, 21);
        hosts[0] = std::make_unique<HostSession>(sims[0], HostSession::Config{});
        hosts[0]->set_on_player_left([this](uint8_t p) { left.emplace_back(0, p); });
        for (uint8_t p = 1; p < 3; ++p) {
            ClientSession::Config cc;
            cc.player = p;
            cc.host = 0;
            clients[p] = std::make_unique<ClientSession>(sims[p], cc);
            auto ends = net.connect(LoopbackNetwork::Link{30, 5});
            link[0][p] = ends.first;
            link[p][0] = ends.second;
            hosts[0]->add_client(p, ends.first);
            clients[p]->set_connection(ends.second);
        }
        auto between = net.connect(LoopbackNetwork::Link{30, 5});
        link[1][2] = between.first;
        link[2][1] = between.second;
        clients[1]->set_peer(2, between.first);
        clients[2]->set_peer(1, between.second);
        hosts[0]->add_bot_seat(3);
        bot_ants = ants_of(sims[0], 3);
        sink = std::make_unique<SessionSink>(*hosts[0], 3);
        bots = std::make_unique<BotController>(sims[0], 4);
        BotSpec spec;
        spec.seat = 3;
        spec.level = Level::Hard;
        std::string why;
        bots->add(spec, std::make_unique<ScriptBot>(wanderer(bot_ants, counter)), *sink, why);
        hosts[0]->runner().set_on_tick([this]() { bots->on_tick(sims[0]); });
        clients[1]->runner().set_on_command([this](const Command& c, const sim::CommandResult&) { saw1.emplace_back(sims[1].current_tick(), c); });
        hosts[0]->start(0);
        clients[1]->start(0);
        clients[2]->start(0);
    }
    void kill_host() {
        host_alive = false;
        net.cut(link[0][1]);
        net.cut(link[0][2]);
    }
    void run(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            if (host_alive) hosts[0]->update(now);
            for (uint8_t s = 1; s < 3; ++s) {
                if (hosts[s]) hosts[s]->update(now);
                else if (clients[s]) clients[s]->update(now);
            }
            for (uint8_t s = 1; s < 3; ++s) {
                if (!clients[s] || !clients[s]->promoted()) continue;
                hosts[s] = promote_to_host(*clients[s], sims[s], HostSession::Config{}, now, [this, s](HostSession& h) { h.set_on_player_left([this, s](uint8_t p) { left.emplace_back(s, p); }); });
                clients[s].reset();
                ++promotions;
            }
        }
    }
    void settle(uint32_t ms = 3000) {
        for (uint8_t s = 0; s < 3; ++s) {
            if (hosts[s] && (s != 0 || host_alive)) hosts[s]->freeze();
        }
        run(ms);
    }
};

// ---- a room on real sockets ---------------------------------------------------------------------------------------------------------------------------

struct Machine {
    sim::SimulationEngine sim;
    NetGame net{sim};
    std::string name;
    std::vector<NetGame::Event> events;
    std::vector<std::pair<uint64_t, Command>> saw;
    std::function<void()> on_tick;
    uint64_t ticks{0};

    explicit Machine(std::string n) : name(std::move(n)) {
        net.set_discovery(0);                                                   // the tests do not announce their rooms on the real network
        net.set_on_tick([this]() {
            ++ticks;
            if (on_tick) on_tick();
        });
        net.set_on_command([this](const Command& c, const sim::CommandResult&) { saw.emplace_back(sim.current_tick(), c); });
    }
    void handle(const NetGame::Event& ev) {
        events.push_back(ev);
        if (ev.type != NetGame::Event::Type::StartRequested) return;
        const StartMsg& s = net.start_info();
        ants::assets::LevelData level;
        uint64_t hash = 0;
        const bool ok = level.load_lvl(maps_dir() + s.map_name) && hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
        if (ok) {
            sim.set_fog_of_war_enabled(s.fog);
            sim.init(level, s.seed, s.roster);
            for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
        }
        net.report_loaded(ok);
    }
};

struct Table {
    std::vector<std::unique_ptr<Machine>> machines;
    uint32_t now{1000};
    Machine& add(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name));
        return *machines.back();
    }
    void pump() {
        for (auto& m : machines) {
            m->net.update(now);
            for (const auto& ev : m->net.take_events()) m->handle(ev);
        }
    }
    void run(uint32_t ms, const std::function<void(uint32_t)>& each = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            pump();
            if (each) each(now);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        const uint32_t end = now + max_ms;
        while (now < end) {
            if (cond()) return true;
            run(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {                       // the game clock is virtual, the sockets are real: a late kernel gets real time (see test_netgame)
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

class NetSink final : public sim::CommandSink {
public:
    NetSink(NetGame& net, uint8_t seat) : net_(net), seat_(seat) {}
    sim::CommandResult submit(const sim::Command& c) override {
        sim::CommandResult r;
        if (net_.submit_bot(seat_, c)) r.status = sim::CommandResult::Status::Applied;
        return r;
    }

private:
    NetGame& net_;
    uint8_t seat_;
};

}  // namespace

void run_net_tests() {
    TEST_CASE("AI5.1 Room: A Bot Takes A Seat And Counts As A Player, Its Thumb Is Good And Nobody Waits For It; A Person Cannot Take A Bot's Seat Or Name; Fog And A Bot Exclude Each Other") {
        {   // a host alone with a bot can start: one person and one bot
            HostLobby lobby;
            lobby.set_map("TINY.LVL");
            ASSERT_FALSE(lobby.can_start());
            ASSERT_FALSE(lobby.has_bot());
            ASSERT_TRUE(lobby.add_bot(1, "Bot (Medium)"));
            ASSERT_TRUE(lobby.has_bot());
            ASSERT_EQ(lobby.players(), 2u);
            ASSERT_TRUE(lobby.can_start() && lobby.occupied(1));
            ASSERT_TRUE(lobby.room().slots[1].state == SlotState::Bot && lobby.room().slots[1].name == "Bot (Medium)");
            ASSERT_EQ(lobby.room().slots[1].rtt_ms, 0);
            ASSERT_TRUE(link_quality(lobby.room().slots[1].rtt_ms) == LinkQuality::Good);
            ASSERT_TRUE(lobby.measured(1) && lobby.rtt_ms(1) == 0 && lobby.all_measured());               // nothing to measure
            ASSERT_TRUE(lobby.connection_of(1) == nullptr);                                                // no connection behind the seat
            const auto events = lobby.take_events();
            ASSERT_TRUE(events.size() == 1 && events[0].type == HostLobby::Event::Type::Joined && events[0].seat == 1);
            // refused: the host's seat, a seat that is taken, seats that do not exist
            ASSERT_FALSE(lobby.add_bot(0, "Bot (Hard)"));
            ASSERT_FALSE(lobby.add_bot(1, "Bot (Hard)"));
            ASSERT_FALSE(lobby.add_bot(4, "Bot (Hard)"));
            ASSERT_FALSE(lobby.add_bot(255, "Bot (Hard)"));
            ASSERT_EQ(lobby.players(), 2u);
            lobby.kick(1);                                                                                 // a bot is not a guest: nothing to throw out
            ASSERT_TRUE(lobby.occupied(1));
            // START: the roster and the names include the bot; the host's own report is the whole barrier (a bot has nothing to load)
            ASSERT_TRUE(lobby.start(42, 7, 0));
            ASSERT_EQ(lobby.start_info().roster, 0x03);
            ASSERT_TRUE(lobby.start_info().names[0] == "Host" && lobby.start_info().names[1] == "Bot (Medium)");
            ASSERT_TRUE(lobby.start_info().endpoints[1].address.empty() && lobby.start_info().endpoints[1].port == 0);   // it can never be elected: nobody connects to it
            ASSERT_TRUE(lobby.phase() == HostLobby::Phase::Loading);
            lobby.host_loaded(true);
            ASSERT_TRUE(lobby.phase() == HostLobby::Phase::Begun);
            lobby.remove_bot(1);                                                                           // once the match runs the seat stays
            ASSERT_TRUE(lobby.room().slots[1].state == SlotState::Bot);
        }
        {   // a room with a bot and a guest: the guest gets the next free seat, sees the bot as a bot, and the match begins without the bot taking part
            Room room;
            ASSERT_TRUE(room.host.add_bot(1, "Bot (Hard)"));
            Room::Guest& g = room.join("Bob", 1);                                                          // it asks for the seat of the bot ...
            room.run(300);
            ASSERT_EQ(g.lobby->my_seat(), 2);                                                              // ... and gets the first free one
            ASSERT_TRUE(g.lobby->room().slots[1].state == SlotState::Bot && g.lobby->room().slots[1].name == "Bot (Hard)" && g.lobby->room().slots[1].rtt_ms == 0);
            ASSERT_TRUE(g.lobby->room().slots[2].state == SlotState::Client && g.lobby->room().slots[2].name == "Bob");
            ASSERT_EQ(room.host.players(), 3u);
            ASSERT_TRUE(room.host.measured(1) && room.host.measured(2));                                  // (the guest answered the ping)
            ASSERT_TRUE(room.host.start(5, 9, room.now));
            room.run(100);
            ASSERT_TRUE(g.lobby->phase() == ClientLobby::Phase::Loading);
            ASSERT_EQ(g.lobby->start_info().roster, 0x07);
            ASSERT_TRUE(g.lobby->start_info().names[1] == "Bot (Hard)" && g.lobby->start_info().names[2] == "Bob");
            g.lobby->report_loaded(true);
            room.host.host_loaded(true);
            room.run(100);
            ASSERT_TRUE(room.host.phase() == HostLobby::Phase::Begun && g.lobby->phase() == ClientLobby::Phase::Begun);
        }
        {   // a bot leaves the room; a cancelled start; a full room
            Room room;
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            Room::Guest& g = room.join("Bob");
            room.run(300);
            ASSERT_EQ(g.lobby->my_seat(), 1);
            room.host.remove_bot(1);                                                                       // a guest's seat is not a bot's: nothing happens
            ASSERT_TRUE(room.host.room().slots[1].state == SlotState::Client);
            room.host.remove_bot(3);
            room.run(100);
            ASSERT_TRUE(room.host.room().slots[3].state == SlotState::Empty && !room.host.has_bot());
            ASSERT_TRUE(g.lobby->room().slots[3].state == SlotState::Empty);
            ASSERT_EQ(room.host.players(), 2u);
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            ASSERT_TRUE(room.host.start(5, 9, room.now));
            room.run(50);
            room.host.remove_bot(3);                                                                       // while the map loads: the start is cancelled for everybody
            room.run(100);
            ASSERT_TRUE(room.host.phase() == HostLobby::Phase::Room);
            ASSERT_TRUE(g.lobby->phase() == ClientLobby::Phase::InRoom && g.lobby->cancel_reason() == CancelMsg::Reason::PlayerLeft && g.lobby->cancel_player() == 3);
        }
        {   // a bot's name always carries the marker (a name that does not is wrapped, an empty one is "Bot"), and no person's name looks like a bot's: with spaces left out
            // and in either case, "bot(" at the start is a bot's, so " Bot (x)", "bot(x)" and "B o t (x)" are all refused to a person
            HostLobby lobby;
            lobby.set_map("TINY.LVL");
            ASSERT_TRUE(lobby.add_bot(1, "Zed"));
            ASSERT_EQ(lobby.room().slots[1].name, std::string("Bot (Zed)"));
            ASSERT_TRUE(lobby.add_bot(2, ""));
            ASSERT_EQ(lobby.room().slots[2].name, std::string("Bot"));
            ASSERT_TRUE(lobby.add_bot(3, "  bot (hard)  "));
            ASSERT_EQ(lobby.room().slots[3].name, std::string("bot (hard)"));                              // already a bot's name: kept (trimmed)
            Room room;
            for (const char* sneaky : {"Bot (Hard)", "bot(x)", " Bot (x)", "B o t (x)", "BOT   (Easy)", "bOt(1)"}) {
                Room::Guest& g = room.join(sneaky);
                room.run(300);
                ASSERT_TRUE(g.lobby->phase() == ClientLobby::Phase::InRoom);
                const std::string shown = room.host.room().slots[g.lobby->my_seat()].name;
                ASSERT_TRUE(shown != sneaky && shown.find("(") == std::string::npos && shown.size() >= 1);          // renamed to something that is not a bot's
                room.host.kick(g.lobby->my_seat());
                room.run(100);
            }
            Room::Guest& honest = room.join("Botany");                                                        // "Bot" without the bracket is a name like any other
            room.run(300);
            ASSERT_EQ(room.host.room().slots[honest.lobby->my_seat()].name, std::string("Botany"));
        }
        {   // a match needs a person: a room of bots alone (a dedicated server's, with no seat for the host) cannot start, with a person in it it can
            HostLobby::Config hc;
            hc.host_seat = 255;
            hc.min_players = 2;
            hc.max_players = 4;
            HostLobby lobby(hc);
            lobby.set_map("TINY.LVL");
            ASSERT_TRUE(lobby.add_bot(0, "Bot (Easy)") && lobby.add_bot(1, "Bot (Hard)"));
            ASSERT_EQ(lobby.players(), 2u);
            ASSERT_EQ(lobby.humans(), 0u);
            ASSERT_FALSE(lobby.can_start());
            ASSERT_FALSE(lobby.start(1, 2, 0));
        }
        {   // a room that holds two players: host and bot fill it
            HostLobby::Config hc;
            hc.max_players = 2;
            Room room(hc);
            ASSERT_TRUE(room.host.add_bot(1, "Bot (Medium)"));
            ASSERT_FALSE(room.host.add_bot(2, "Bot (Medium)"));
            Room::Guest& g = room.join("Bob");
            room.run(300);
            ASSERT_TRUE(g.lobby->phase() == ClientLobby::Phase::Rejected && g.lobby->reject_reason() == RejectReason::Full);
        }
        {   // a person can never be shown as a bot: a name that starts with "Bot (" (either case) is replaced; other names are not touched
            Room room;
            Room::Guest& a = room.join("Bot (Hard)");
            Room::Guest& b = room.join("bot (easy)");
            Room::Guest& c = room.join("Bot (");
            Room::Guest& d = room.join("Bots R Us");
            room.run(300);
            ASSERT_TRUE(a.lobby->room().slots[1].state == SlotState::Client && a.lobby->room().slots[1].name == "Player 2");
            ASSERT_TRUE(b.lobby->room().slots[2].name == "Player 3");
            ASSERT_TRUE(c.lobby->room().slots[3].name == "Player 4");
            ASSERT_EQ(room.host.room().slots[1].name, "Player 2");
            (void)d;
            HostLobby::Config hc;
            hc.host_name = "BOT (Medium)";
            HostLobby lobby(hc);
            ASSERT_EQ(lobby.room().slots[0].name, "Host");                                                  // not even the host's own name may pose as a bot
            HostLobby::Config plain;
            plain.host_name = "Bot";
            ASSERT_EQ(HostLobby(plain).room().slots[0].name, "Bot");
            plain.host_name = "Botany (x)";
            ASSERT_EQ(HostLobby(plain).room().slots[0].name, "Botany (x)");
            Room room2;
            room2.join("Bots R Us");
            room2.run(300);
            ASSERT_EQ(room2.host.room().slots[1].name, "Bots R Us");
        }
        {   // fog and a bot exclude each other, whichever comes first
            HostLobby lobby;
            lobby.set_map("TINY.LVL");
            ASSERT_TRUE(lobby.add_bot(1, "Bot (Medium)"));
            lobby.set_fog(true);
            ASSERT_FALSE(lobby.fog());                                                                      // refused: it stays off
            lobby.remove_bot(1);
            lobby.set_fog(true);
            ASSERT_TRUE(lobby.fog());
            ASSERT_FALSE(lobby.add_bot(1, "Bot (Medium)"));                                                 // refused: the room has fog
            ASSERT_FALSE(lobby.has_bot());
            lobby.set_fog(false);
            ASSERT_TRUE(lobby.add_bot(1, "Bot (Medium)"));
        }
        {   // the wire: a room with a bot slot round-trips (protocol 6 has the slot state), an unknown state is refused
            RoomMsg r;
            r.slots[0] = {SlotState::Host, "Queen", 0};
            r.slots[1] = {SlotState::Bot, "Bot (Medium)", 0};
            r.slots[2] = {SlotState::Client, "Bob", 40};
            r.map_name = "TINY.LVL";
            r.you = 2;
            RoomMsg back;
            ASSERT_TRUE(decode(encode(r), back));
            ASSERT_TRUE(back.slots[1].state == SlotState::Bot && back.slots[1].name == "Bot (Medium)" && back.slots[1].rtt_ms == 0 && back.slots[3].state == SlotState::Empty);
            // the byte of slot 1's state: the one place where the encodings with a Bot and with a Client in slot 1 differ
            RoomMsg other = r;
            other.slots[1].state = SlotState::Client;
            const std::vector<uint8_t> bytes = encode(r);
            const std::vector<uint8_t> bytes_other = encode(other);
            ASSERT_EQ(bytes.size(), bytes_other.size());
            size_t state_byte = bytes.size();
            for (size_t i = 0; i < bytes.size(); ++i) {
                if (bytes[i] != bytes_other[i]) {
                    ASSERT_EQ(state_byte, bytes.size());                                                    // exactly one byte differs
                    state_byte = i;
                }
            }
            ASSERT_TRUE(state_byte < bytes.size());
            for (const uint8_t bad : {uint8_t{4}, uint8_t{5}, uint8_t{100}, uint8_t{255}}) {               // the states are 0 .. 3: anything above is refused
                std::vector<uint8_t> broken = bytes;
                broken[state_byte] = bad;
                RoomMsg x;
                ASSERT_FALSE(decode(broken, x));
            }
            std::vector<uint8_t> as_bot = bytes;
            as_bot[state_byte] = static_cast<uint8_t>(SlotState::Bot);
            RoomMsg y;
            ASSERT_TRUE(decode(as_bot, y) && y.slots[1].state == SlotState::Bot);
        }
    } TEST_END();

    TEST_CASE("AI5.2 Session: A Bot Seat Is Acknowledged By The Host, So The Sequencer Never Stalls (1000+ Turns, With And Without A Seat For The Host); Without The Ack It Stalls After 61 Turns") {
        {   // the reason: a seat that is active and never acknowledges holds the sequencer up (max_lag_turns = 60: 3 s of 50 ms turns)
            Sequencer s;
            s.set_active(0, true);
            s.set_active(1, true);
            uint32_t sealed = 0;
            while (s.can_seal() && sealed < 500) {
                const TurnMsg t = s.seal();
                s.on_ack(0, t.turn);
                ++sealed;
            }
            ASSERT_EQ(sealed, 61u);
            ASSERT_EQ(s.laggard(), 1);
        }
        for (const bool seatless : {false, true}) {
            sim::SimulationEngine sim;
            build_world(sim, 31);
            HostSession::Config hc;
            hc.host_player = seatless ? kNoSeat : uint8_t{0};
            HostSession host(sim, hc);
            host.add_bot_seat(1);
            host.add_bot_seat(2);
            host.add_bot_seat(static_cast<uint8_t>(seatless ? 3 : 0));                                                       // the host's own seat is never a bot's (ignored); a seatless host has seat 3 free
            host.add_bot_seat(9);                                                                           // not a seat
            ASSERT_TRUE(host.is_bot_seat(1) && host.is_bot_seat(2) && !host.is_bot_seat(9) && !host.is_bot_seat(255));
            ASSERT_EQ(host.is_bot_seat(0), false);
            ASSERT_EQ(host.is_bot_seat(3), seatless);
            host.start(0);
            host.add_bot_seat(3);                                                                           // too late: the roster is fixed once the match runs
            uint32_t now = 0;
            while (host.turns_sealed() < 2400 && now < 300000) {
                now += 10;
                host.update(now);
                ASSERT_FALSE(host.waiting());                                                                 // never held up
            }
            ASSERT_TRUE(host.turns_sealed() >= 2400);
            ASSERT_EQ(host.laggard(), 255);
            ASSERT_TRUE(host.desyncs().empty());
            ASSERT_TRUE(sim.current_tick() > 2000);                                                         // the engine ran along: 2400 turns are 2400 ticks (a turn is one tick)
            ASSERT_EQ(host.is_bot_seat(3), seatless);
        }
    } TEST_END();

    TEST_CASE("AI5.3 Session: submit_bot Puts A Bot's Command Into The Next Turn With The Seat As Issuer; It Refuses A Seat That Is Not A Bot's, Drop, None, A Flood, And Anything Before The Start") {
        for (const bool seatless : {false, true}) {
            sim::SimulationEngine sim;
            build_world(sim, 32);
            HostSession::Config hc;
            hc.host_player = seatless ? kNoSeat : uint8_t{0};
            HostSession host(sim, hc);
            host.add_bot_seat(2);
            const std::vector<uint32_t> ants = ants_of(sim, 2);
            sim::Command move;
            move.type = CommandType::GroupMove;
            move.issuer = 3;                                                                                // not the bot's to say
            move.tile_x = 20;
            move.tile_y = 20;
            move.ants = {ants[0]};
            ASSERT_FALSE(host.submit_bot(2, move));                                                         // before the start
            std::vector<std::pair<uint64_t, Command>> saw;
            host.runner().set_on_command([&](const Command& c, const sim::CommandResult&) { saw.emplace_back(sim.current_tick(), c); });
            host.start(0);
            ASSERT_TRUE(host.submit_bot(2, move));
            ASSERT_FALSE(host.submit_bot(1, move));                                                         // seat 1 is nobody's bot
            ASSERT_FALSE(host.submit_bot(0, move));                                                         // nor the host's own seat
            ASSERT_FALSE(host.submit_bot(7, move));
            sim::Command drop = hatch_of(2);
            drop.type = CommandType::Drop;
            ASSERT_FALSE(host.submit_bot(2, drop));                                                         // the system command is the sequencer's alone
            drop.type = CommandType::None;
            ASSERT_FALSE(host.submit_bot(2, drop));
            int accepted = 0;
            for (int i = 0; i < 400; ++i) accepted += host.submit_bot(2, hatch_of(2)) ? 1 : 0;
            ASSERT_EQ(accepted, 63 + 256);                                                                  // 64 per seat and turn (one is queued already), and 256 more wait for the turns after it; the rest is a flood
            uint32_t now = 0;
            while (now < 1500) {
                now += 10;
                host.update(now);
            }
            ASSERT_TRUE(saw.size() >= 64);
            ASSERT_EQ(saw[0].second.issuer, 2);                                                             // the move went first, stamped with the bot's seat
            ASSERT_TRUE(saw[0].second.type == CommandType::GroupMove && saw[0].second.tile_x == 20);
            for (const auto& e : saw) ASSERT_EQ(e.second.issuer, 2);
            ASSERT_FALSE(sim.is_player_dropped(2));
        }
    } TEST_END();

    TEST_CASE("AI5.4 Match: A Host, A Guest And A Bot Seat Run By The Host Play 60 Seconds; Both Engines Stay Identical, The Bot's Commands Are In The Turn Stream With Its Seat On Both, Nothing Stalls") {
        BotMatch m(41);
        ASSERT_TRUE(m.bot != nullptr);
        m.run(60000);
        ASSERT_TRUE(m.host->turns_sealed() > 1150);
        ASSERT_FALSE(m.host->waiting());
        m.host->freeze();
        m.run(3000, false);                                                                               // what is in flight arrives and executes
        ASSERT_TRUE(m.host_sim.state_hash() == m.client_sim.state_hash());
        ASSERT_TRUE(m.host->desyncs().empty() && !m.client->desynced());
        ASSERT_EQ(m.host_sim.current_tick(), m.client_sim.current_tick());
        const auto from_bot_host = of_issuer(m.host_saw, 2);
        const auto from_bot_client = of_issuer(m.client_saw, 2);
        ASSERT_TRUE(from_bot_host.size() > 40);                                                           // about one command per second (a look every 20 ticks)
        ASSERT_TRUE(from_bot_host.size() == from_bot_client.size());
        for (size_t i = 0; i < from_bot_host.size(); ++i) {
            ASSERT_TRUE(from_bot_host[i].first == from_bot_client[i].first && from_bot_host[i].second == from_bot_client[i].second);          // the same command at the same tick
            ASSERT_TRUE(from_bot_host[i].second.type == CommandType::GroupMove && from_bot_host[i].second.ants.size() == 1);
        }
        ASSERT_EQ(m.bots->stats(2).rejected, 0u);
        ASSERT_TRUE(m.bots->stats(2).released > 40 && m.bots->stats(2).released <= static_cast<uint32_t>(from_bot_host.size()) + 3);
        ASSERT_FALSE(m.host_sim.is_player_dropped(2));                                                    // a bot that is never heard from through a socket is never dropped for silence
        // the people play too: their commands arrive with their seats, and the bot's team really moved (the commands were not just logged)
        ASSERT_TRUE(of_issuer(m.host_saw, 0).size() > 50 && of_issuer(m.host_saw, 1).size() > 50);
        size_t moved = 0;
        for (const auto& a : m.host_sim.get_world_state().ants) {
            if (a.player_id != 2) continue;
            for (const auto& start : m.bot_start) {
                if (start.first == a.id && !(start.second == TileCoord{a.tile_x, a.tile_y})) ++moved;
            }
        }
        ASSERT_TRUE(moved >= 3);
    } TEST_END();

    TEST_CASE("AI5.5 Host Migration: When The Host Leaves, Its Bot Leaves With It (Dropped By The New Host's First Turn); The Survivors Stay Identical And Play On") {
        BotMesh m;
        m.run(5000 + kStartHoldMs);                                                                        // (the bot's first order comes after the start hold: five seconds of play, then the host goes)
        ASSERT_TRUE(m.hosts[0]->is_bot_seat(3));
        ASSERT_TRUE(m.hosts[0]->turns_sealed() > 80);
        ASSERT_TRUE(of_issuer(m.saw1, 3).size() > 5);                                                      // the bot played, and the guest saw it
        const size_t before = of_issuer(m.saw1, 3).size();
        m.kill_host();
        m.run(9000);
        ASSERT_EQ(m.promotions, 1);
        ASSERT_TRUE(m.hosts[1] != nullptr && m.hosts[2] == nullptr);
        ASSERT_FALSE(m.hosts[1]->is_bot_seat(3));                                                           // the new host has no bot: it lived in the old host's process
        ASSERT_EQ(m.clients[2]->host_seat(), 1);
        m.settle();
        for (uint8_t s = 1; s < 3; ++s) {
            ASSERT_TRUE(m.sims[s].is_player_dropped(0) && m.sims[s].is_player_dropped(3));                  // the old host and its bot are out of the match
            ASSERT_FALSE(m.sims[s].is_player_dropped(1) || m.sims[s].is_player_dropped(2));
        }
        ASSERT_TRUE(m.sims[1].state_hash() == m.sims[2].state_hash());
        ASSERT_TRUE(m.hosts[1]->desyncs().empty() && !m.clients[2]->desynced());
        ASSERT_FALSE(m.sims[1].is_match_over());                                                            // two teams are left: the match goes on
        bool dropped0 = false;
        bool dropped3 = false;
        for (const auto& l : m.left) {
            dropped0 = dropped0 || (l.first == 1 && l.second == 0);
            dropped3 = dropped3 || (l.first == 1 && l.second == 3);
        }
        ASSERT_TRUE(dropped0 && dropped3);
        // after the drop nothing more comes from the bot: its commands stop with the old host (the turn that dropped it is the last that names it)
        const auto from_bot = of_issuer(m.saw1, 3);
        ASSERT_TRUE(from_bot.size() >= before);
        const uint64_t last_bot_tick = from_bot.back().first;
        ASSERT_TRUE(m.sims[1].current_tick() > last_bot_tick + 100);
    } TEST_END();

    TEST_CASE("AI5.6 LAN Room: A Host With A Bot And A Guest Start A Match Through NetGame; The Room Shows The Bot, The Guest Cannot Command It, Both Engines Stay Bit-Identical For 30 Seconds") {
        Table t;
        Machine& host = t.add("Alice");
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ASSERT_FALSE(host.net.add_bot(0, "Bot (Medium)"));                                                  // the host's own seat
        ASSERT_TRUE(host.net.add_bot(2, "Bot (Medium)"));
        ASSERT_FALSE(host.net.add_bot(2, "Bot (Hard)"));
        host.net.set_fog(true);                                                                             // refused while a bot sits in the room: the option stays off, the line says why
        ASSERT_FALSE(host.net.room().fog);
        t.pump();
        ASSERT_TRUE(host.net.status_text().find("Fog of War") != std::string::npos);
        Machine& bob = t.add("Bob");
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob", 2));                           // it asks for the bot's seat and gets the first free one
        ASSERT_TRUE(t.run_until([&]() {
            return bob.net.phase() == NetGame::Phase::Room && bob.net.room().slots[1].state == SlotState::Client && host.net.can_start() &&
                   bob.net.room().slots[2].state == SlotState::Bot;
        }, 8000));
        ASSERT_EQ(bob.net.my_seat(), 1);
        for (Machine* m : {&host, &bob}) {
            ASSERT_TRUE(m->net.room().slots[2].state == SlotState::Bot && m->net.room().slots[2].name == "Bot (Medium)");
            ASSERT_TRUE(m->net.seat_quality(2) == LinkQuality::Good);                                       // the thumb of a bot
            ASSERT_FALSE(m->net.room().fog);
        }
        ASSERT_FALSE(bob.net.add_bot(3, "Bot (Easy)"));                                                     // only the host seats bots
        ASSERT_FALSE(bob.net.submit_bot(2, hatch_of(2)));
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4242, hash));
        ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Playing && bob.net.phase() == NetGame::Phase::Playing; }, 8000));
        for (Machine* m : {&host, &bob}) {
            ASSERT_EQ(m->net.start_info().roster, 0x07);
            ASSERT_EQ(m->sim.roster_mask(), 0x07);
            ASSERT_EQ(m->sim.get_player_name(2), "Bot (Medium)");
            ASSERT_EQ(m->sim.grid().anthills().size(), 3u);                                                 // the bot's team is a team: it has its hill and its ants
        }
        ASSERT_TRUE(host.sim.state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(host.net.submit_bot(1, hatch_of(1)));                                                  // seat 1 is a person's
        ASSERT_FALSE(host.net.submit_bot(0, hatch_of(0)));
        // the bot's mind runs on the host: its look every second, its commands through the host's sequencer
        BotController bots(host.sim, 4242);
        NetSink sink(host.net, 2);
        const std::vector<uint32_t> bot_ants = ants_of(host.sim, 2);
        ASSERT_TRUE(bot_ants.size() >= 3);
        size_t counter = 0;
        std::string why;
        BotSpec spec;
        spec.seat = 2;
        spec.level = Level::Medium;
        ASSERT_TRUE(bots.add(spec, std::make_unique<ScriptBot>(wanderer(bot_ants, counter)), sink, why));
        host.on_tick = [&]() { bots.on_tick(host.sim); };
        const std::vector<uint32_t> mine = ants_of(host.sim, 0);
        const std::vector<uint32_t> theirs = ants_of(bob.sim, 1);
        t.run(30000, [&](uint32_t now) {
            if (now % 700 != 0) return;
            Command c;
            c.type = CommandType::GroupMove;
            c.tile_x = static_cast<int16_t>(5 + (now / 700) % 30);
            c.tile_y = static_cast<int16_t>(5 + (now / 300) % 30);
            c.ants = {mine[(now / 700) % mine.size()]};
            host.net.submit(c);
            c.ants = {theirs[(now / 700) % theirs.size()]};
            bob.net.submit(c);
        });
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(host.sim.state_hash() == bob.sim.state_hash());
        ASSERT_EQ(host.sim.current_tick(), bob.sim.current_tick());
        ASSERT_FALSE(host.net.desynced() || bob.net.desynced());
        ASSERT_EQ(host.net.laggard(), 255);
        const auto host_bot = of_issuer(host.saw, 2);
        const auto bob_bot = of_issuer(bob.saw, 2);
        ASSERT_TRUE(host_bot.size() > 20);
        ASSERT_TRUE(host_bot.size() == bob_bot.size());
        for (size_t i = 0; i < host_bot.size(); ++i) ASSERT_TRUE(host_bot[i].first == bob_bot[i].first && host_bot[i].second == bob_bot[i].second);
        ASSERT_EQ(bots.stats(2).rejected, 0u);
        ASSERT_FALSE(host.sim.is_player_dropped(2) || bob.sim.is_player_dropped(2));
        // leaving ends it for everybody; the host's room and the bot go with it
        host.net.leave();
        ASSERT_FALSE(host.net.active());
    } TEST_END();

    TEST_CASE("AI5.7 The Fill Of A Room On The Local Network (Protocol 11): The Names Of The Fill And Of ants_ai Agree For Every Level; The Host's Own START Seats Bots Of A Level In The Empty Seats (Easy / Medium / Hard), Their Real Bots Run On The Host's Machine Through The Sequencer For 40 Seconds, Both Engines Stay Bit-Identical, The Bots Played (Commands In The Turn Stream With Their Seats, Nobody Refused, Nobody Dropped); The Bots Are Not Needed To Host Or Join A Room") {
        for (const FillLevel fill : {FillLevel::Easy, FillLevel::Medium, FillLevel::Hard}) {            // the room's name for a fill bot is the name of the standard bot of that level
            BotSpec spec;
            spec.seat = 1;
            spec.level = fill == FillLevel::Easy ? Level::Easy : fill == FillLevel::Medium ? Level::Medium : Level::Hard;
            ASSERT_EQ(fill_bot_name(fill), bot_display_name(spec));
            ASSERT_EQ(std::string(fill_level_name(fill)), std::string(level_name(spec.level)));
        }
        {   // no bot code to host or join: a room without a fill never builds a controller, and the room is the one of every earlier version
            Table t;
            Machine& host = t.add("Alice");
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            Machine& bob = t.add("Bob");
            ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
            ASSERT_TRUE(t.run_until([&]() { return bob.net.phase() == NetGame::Phase::Room && host.net.can_start(); }, 8000));
            ASSERT_EQ(host.net.fill_bots(), FillLevel::None);
            ASSERT_FALSE(host.net.room().slots[2].state == SlotState::Bot || host.net.room().slots[3].state == SlotState::Bot);
        }
        Table t;
        Machine& host = t.add("Alice");
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        Machine& bob = t.add("Bob");
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob", 1));
        ASSERT_TRUE(t.run_until([&]() { return bob.net.phase() == NetGame::Phase::Room && bob.net.room().slots[1].state == SlotState::Client && host.net.can_start(); }, 8000));
        std::vector<uint8_t> seats;
        ASSERT_EQ(host.net.fill_bots(FillLevel::Hard, &seats), size_t{2});                                // the empty seats: 2 and 3
        ASSERT_EQ(seats, (std::vector<uint8_t>{2, 3}));
        ASSERT_TRUE(t.run_until([&]() { return bob.net.room().slots[2].state == SlotState::Bot && bob.net.room().slots[3].state == SlotState::Bot; }, 3000));
        ASSERT_EQ(bob.net.room().slots[3].name, "Bot (Hard)");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4243, hash));
        ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Playing && bob.net.phase() == NetGame::Phase::Playing; }, 8000));
        for (Machine* m : {&host, &bob}) {
            ASSERT_EQ(m->sim.roster_mask(), 0x0F);
            ASSERT_TRUE(m->sim.get_player_name(2) == "Bot (Hard)" && m->sim.get_player_name(3) == "Bot (Hard)" && m->sim.get_player_name(1) == "Bob");
        }
        // the bots of the filled seats, as the application builds them: the controller over the host's engine, one sink per seat, called after every tick
        BotController bots(host.sim, 4243);
        NetSink sink2(host.net, 2);
        NetSink sink3(host.net, 3);
        std::string why;
        ASSERT_TRUE(bots.add(BotSpec{2, "standard", Level::Hard}, sink2, why));
        ASSERT_TRUE(bots.add(BotSpec{3, "standard", Level::Hard}, sink3, why));
        host.on_tick = [&]() { bots.on_tick(host.sim); };
        const std::vector<uint32_t> mine = ants_of(host.sim, 0);
        const std::vector<uint32_t> theirs = ants_of(bob.sim, 1);
        t.run(40000, [&](uint32_t now) {
            if (now % 900 != 0) return;
            Command c;
            c.type = CommandType::GroupMove;
            c.tile_x = static_cast<int16_t>(6 + (now / 900) % 28);
            c.tile_y = static_cast<int16_t>(6 + (now / 450) % 28);
            c.ants = {mine[(now / 900) % mine.size()]};
            host.net.submit(c);
            c.ants = {theirs[(now / 900) % theirs.size()]};
            bob.net.submit(c);
        });
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(host.sim.state_hash() == bob.sim.state_hash());
        ASSERT_EQ(host.sim.current_tick(), bob.sim.current_tick());
        ASSERT_FALSE(host.net.desynced() || bob.net.desynced());
        for (const uint8_t seat : {uint8_t{2}, uint8_t{3}}) {
            const auto host_saw = of_issuer(host.saw, seat);
            const auto bob_saw = of_issuer(bob.saw, seat);
            ASSERT_TRUE(host_saw.size() >= 1 && host_saw.size() == bob_saw.size());                      // the bots acted (a worker needs few commands: one order starts a harvest loop that runs by itself), and both machines saw the same commands at the same ticks
            ASSERT_TRUE(host.sim.get_display_score(seat) > 0 && host.sim.get_display_score(seat) == bob.sim.get_display_score(seat));       // and what they did is in the match: they banked food, the same on both machines
            for (size_t i = 0; i < host_saw.size(); ++i) ASSERT_TRUE(host_saw[i].first == bob_saw[i].first && host_saw[i].second == bob_saw[i].second);
            ASSERT_EQ(bots.stats(seat).rejected, 0u);
            ASSERT_TRUE(bots.stats(seat).released >= 1 && bots.stats(seat).released <= static_cast<uint32_t>(host_saw.size()) + 3);
            ASSERT_FALSE(host.sim.is_player_dropped(seat) || bob.sim.is_player_dropped(seat));
        }
        // a person may not take a bot's name, and a bot's seat is never absent: the room that this machine keeps has no connection behind the bots
        ASSERT_TRUE(host.net.room().slots[2].state == SlotState::Bot && host.net.room().slots[3].state == SlotState::Bot);
        host.net.leave();
        ASSERT_FALSE(host.net.active());
    } TEST_END();
}
