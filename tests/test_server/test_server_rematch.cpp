// Tests of the dedicated game server: the rematch of a lobby room (docs/SERVER.md "Lobby rooms"). When the rules end the match that a lobby room began, the server opens a lobby room with the same code at once,
// with the match's plan and a seat reserved for the key of each of its people; the room of the match is retired (it answers its last connections and keeps its status for its keep time).
#include "server_fixtures.hpp"

namespace {

// The options of a server that offers the front page's lobbies (as test_server_lobby.cpp's)
ServerLimits rematch_limits() {
    ServerLimits l;
    l.demo_rooms = 4;
    l.demo_lobbies = 3;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL"};
    return l;
}

net::PlanMsg plan_of(const std::array<net::PlanKind, 4>& kinds, uint8_t team_a = net::kNoTeam, uint8_t team_b = net::kNoTeam, const std::string& map = std::string()) {
    net::PlanMsg m;
    m.map_name = map;
    m.plan = kinds;
    m.team_a = team_a;
    m.team_b = team_b;
    return m;
}

// The status of the waiting lobby of a code: none when there is no lobby
std::optional<RoomStatus> lobby_of(World& w, const std::string& code) {
    RoomStatus s;
    if (w.mgr.status(code, s, w.now) && s.lobby && s.state == RoomState::Waiting) return s;
    return std::nullopt;
}

bool until_running(World& w, const std::string& code, uint32_t ms) {
    for (uint32_t t = 0; t < ms && w.status(code).state != RoomState::Running; t += 10) w.run(10);
    return w.status(code).state == RoomState::Running;
}

net::RejectReason refusal_of(World& w, Client& c) {
    w.run(300);
    return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
}

// Which colours hold a game in a Room message ("1100"): the reserved seats of a rematch count as games
std::string games_of(const net::RoomMsg& r) {
    std::string out;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out += r.seat_in_game(seat) ? '1' : '0';
    return out;
}

// Pia's and Bob's pages make the lobby room `code` (Pia leads), Pia's plan, START, the pages go and their games take the seats over, the match runs a little, and Bob's game quits: with one other side left the
// quit ends the match by its rules (every machine shows the results). The keys of the two seats, and the two games.
struct Played {
    net::SeatKey pia{};
    net::SeatKey bob{};
    Client* pia_game{nullptr};
    Client* bob_game{nullptr};
};
void play_and_quit(World& w, const std::string& code, const net::PlanMsg& plan, Played& out, bool quit = true) {
    Client& pia = w.connect_page("Pia", code, lobby_block_of());
    Client& bob = w.connect_page("Bob", code, lobby_block_of());
    w.run(300);
    ASSERT_TRUE(pia.lobby->is_leader() && bob.lobby->my_seat() == 1);
    ASSERT_TRUE(pia.lobby->request_plan(plan));
    w.run(300);
    ASSERT_TRUE(pia.lobby->request_start());
    w.run(300);
    ASSERT_TRUE(w.status(code).starting);
    out.pia = pia.lobby->key();
    out.bob = bob.lobby->key();
    pia.end->close();                                                                // (the pages go to the game page)
    bob.end->close();
    out.pia_game = &w.connect_game("Pia", code, out.pia);
    out.bob_game = &w.connect_game("Bob", code, out.bob);
    ASSERT_TRUE(until_running(w, code, 8000));
    w.run(kPre + 3000);
    ASSERT_TRUE(out.pia_game->session != nullptr && out.bob_game->session != nullptr && w.status(code).ticks > 20);
    if (!quit) return;
    sim::Command q;
    q.type = sim::CommandType::Quit;
    q.issuer = out.bob_game->lobby->my_seat();
    ASSERT_TRUE(out.bob_game->session->submit(q));
    for (int i = 0; i < 400 && w.status(code).state == RoomState::Running; ++i) w.run(10);
    ASSERT_TRUE(w.ended(code).state == RoomState::Finished && w.ended(code).reason == "the match ended");
}

}  // namespace

void run_rematch_tests() {
    using K = net::PlanKind;

    TEST_CASE("S3.194 The Rematch, The Round Trip: The Rules End A Lobby Room's Match And The Server Opens A Lobby With The Same Code At Once (The Match's Map, Colours, Bots, Teams; A Seat Reserved For Each Person's Key; Nobody Leads Yet); The Code's Status Shows The Match For Its Keep Time, Its End Is Reported Once; A Results Screen Holds Its Seat, A Stranger Is Turned Away, The Host's Page Comes Back And Leads, START Waits For The One Who Is Not Back (The Host Is Told), The Host Removes Them And START Is Taken") {
        World w(rematch_limits());
        const std::string code = "rmt00001";
        Played p;
        play_and_quit(w, code, plan_of({K::Open, K::Open, K::Medium, K::Nobody}, 0, 2, "SMALL.LVL"), p);
        // the match is over: the code is a lobby's now, and the match's room is listed beside it for its keep time (with its result)
        RoomStatus end = w.ended(code);
        ASSERT_TRUE(end.state == RoomState::Finished && !end.rows.empty() && end.map == "SMALL.LVL");
        ASSERT_TRUE(w.mgr.rooms_reopened() == 1u && w.mgr.retired_count() == 1u && w.mgr.room_count() == 1u);
        std::optional<RoomStatus> lobby = lobby_of(w, code);
        ASSERT_TRUE(lobby.has_value());
        ASSERT_TRUE(lobby->reopened && lobby->reserved == 0x3 && lobby->games == 0 && lobby->leader == 255);
        ASSERT_EQ(lobby->map, std::string("SMALL.LVL"));                                           // the match's map, colours, bot and teams
        ASSERT_EQ(lobby->plan, std::string("oomn 0+2"));
        ASSERT_TRUE(lobby->names[0] == "Pia" && lobby->names[1] == "Bob" && lobby->names[2].empty() && lobby->names[3].empty());
        std::vector<RoomStatus> ended = w.mgr.take_ended(w.now);
        ASSERT_TRUE(ended.size() == 1 && ended[0].code == code && ended[0].state == RoomState::Finished);
        ASSERT_TRUE(w.mgr.take_ended(w.now).empty());
        ASSERT_EQ(w.mgr.busy(w.now).players, 0u);                                                  // (nobody is in the lobby yet: the reserved seats are nobody online)
        // a results screen: a game with Bob's key holds his seat, which stays reserved; the Room message shows both as games and no leader
        Client& bob_results = w.connect_game("Bob", code, p.bob);
        w.run(300);
        ASSERT_TRUE(bob_results.lobby->phase() == net::ClientLobby::Phase::InRoom && bob_results.lobby->my_seat() == 1 && net::key_matches(bob_results.lobby->key(), p.bob));
        ASSERT_EQ(games_of(bob_results.lobby->room()), std::string("1100"));
        ASSERT_EQ(bob_results.lobby->room().leader, net::kNoLeader);
        ASSERT_EQ(bob_results.lobby->room().slots[2].name, bn(code, 2, "Medium"));                  // the planned bot, under its name of this code (the match's)
        ASSERT_EQ(w.mgr.busy(w.now).players, 1u);
        // a stranger is turned away while nobody of the match is back
        Client& stranger = w.connect_page("Sid", code, lobby_block_of());
        ASSERT_EQ(refusal_of(w, stranger), net::RejectReason::MatchRunning);
        // Pia's page comes back with her key (the front page's Hello carries the lobby block too): she is back and leads
        w.next_key = p.pia;
        Client& pia_back = w.connect_page("Pia", code, lobby_block_of());
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(pia_back.lobby->phase() == net::ClientLobby::Phase::InRoom && pia_back.lobby->my_seat() == 0 && pia_back.lobby->is_leader());
        ASSERT_EQ(games_of(pia_back.lobby->room()), std::string("0100"));
        lobby = lobby_of(w, code);
        ASSERT_TRUE(lobby.has_value() && lobby->reserved == 0x2 && lobby->leader == 0);
        // START waits for Bob: not taken, the host is told who is missing
        ASSERT_TRUE(pia_back.lobby->request_start());
        w.run(300);
        lobby = lobby_of(w, code);
        ASSERT_TRUE(lobby.has_value() && !lobby->starting && lobby->ignored_start_requests == 1u);
        ASSERT_EQ(said(pia_back.room_chat), (std::vector<std::string>{std::string("255||Bob") + net::kNoticeNotBack}));
        // the host removes Bob: his results screen is told, and the START is taken (and waits for Pia's game, as any lobby's)
        ASSERT_TRUE(pia_back.lobby->request_remove(1));
        w.run(300);
        ASSERT_TRUE(bob_results.lobby->phase() == net::ClientLobby::Phase::Rejected && bob_results.lobby->reject_reason() == net::RejectReason::Kicked);
        ASSERT_TRUE(pia_back.lobby->request_start());
        w.run(300);
        lobby = lobby_of(w, code);
        ASSERT_TRUE(lobby.has_value() && lobby->starting && lobby->reserved == 0);
        // the match's room is forgotten when its keep time is over
        w.run(30000);
        ASSERT_TRUE(w.mgr.retired_count() == 0u && w.ended(code).code.empty());
        ASSERT_TRUE(w.status(code).lobby && w.status(code).reopened);
        ASSERT_TRUE(w.mgr.take_ended(w.now).empty());                                               // (the match's end was reported once)
    } TEST_END();

    TEST_CASE("S3.195 The Rematch Brings Everybody Back And Starts Again: Both Results Screens Hold Their Seats, Both Pages Come Back (The Host First In The Order Leads Again After The Other Led), START Is Taken When Nobody Is Missing, Both Games Take Their Seats Over With The Same Keys And The Second Match Runs On The Same Map With The Same Teams; A Game That Comes Back To The Match That Is Over Finds None") {
        World w(rematch_limits());
        const std::string code = "rmt00002";
        Played p;
        play_and_quit(w, code, plan_of({K::Open, K::Open, K::Easy, K::Nobody}, 0, 2, "SMALL.LVL"), p);
        // a game of the match that comes back to it (a key and the turns that it ran: its way back) finds no match: the code is the lobby's
        {
            net::HelloMsg hello;
            hello.name = "Bob";
            hello.room = code;
            hello.key = p.bob;
            hello.have_turns = 50;
            auto ends = w.net.connect({20, 0});
            w.mgr.add_connection(std::make_unique<Borrowed>(ends.first), "127.0.0.1", w.now);
            ends.second->send(net::encode(hello));
            w.run(300);
            bool told = false;
            std::vector<uint8_t> m;
            while (ends.second->poll(m)) {
                net::RejectMsg r;
                if (net::peek_type(m) == net::MsgType::Reject && net::decode(m, r)) told = r.reason == net::RejectReason::NoSuchRoom;
            }
            ASSERT_TRUE(told);
            ASSERT_TRUE(lobby_of(w, code).has_value() && lobby_of(w, code)->reserved == 0x3);
        }
        Client& pia_results = w.connect_game("Pia", code, p.pia);
        Client& bob_results = w.connect_game("Bob", code, p.bob);
        w.run(300);
        ASSERT_TRUE(pia_results.lobby->my_seat() == 0 && bob_results.lobby->my_seat() == 1);
        w.next_key = p.bob;                                                                         // Bob is back first: he leads while Pia is not back
        Client& bob_back = w.connect_page("Bob", code, lobby_block_of());
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(bob_back.lobby->is_leader() && bob_results.lobby->reject_reason() == net::RejectReason::Superseded);
        w.next_key = p.pia;                                                                         // Pia is back: first in the order, she leads again
        Client& pia_back = w.connect_page("Pia", code, lobby_block_of());
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(pia_back.lobby->is_leader() && !bob_back.lobby->is_leader());
        ASSERT_EQ(games_of(pia_back.lobby->room()), std::string("0000"));
        ASSERT_TRUE(pia_back.lobby->request_start());                                              // nobody is missing: START is taken, the pages go to the games
        w.run(300);
        ASSERT_TRUE(lobby_of(w, code).has_value() && lobby_of(w, code)->starting);
        pia_back.end->close();
        bob_back.end->close();
        Client& pia_game = w.connect_game("Pia", code, p.pia);
        Client& bob_game = w.connect_game("Bob", code, p.bob);
        ASSERT_TRUE(until_running(w, code, 8000));
        w.run(kPre + 2000);
        const RoomStatus again = w.status(code);
        ASSERT_TRUE(again.state == RoomState::Running && again.map == "SMALL.LVL" && again.teams == "0+2" && again.bots.size() == 1u && again.bots[0].seat == 2);
        ASSERT_TRUE(pia_game.lobby->my_seat() == 0 && bob_game.lobby->my_seat() == 1 && pia_game.session != nullptr && bob_game.session != nullptr);
        ASSERT_FALSE(pia_game.session->desynced() || bob_game.session->desynced());
    } TEST_END();

    TEST_CASE("S3.196 No Rematch Without The Rules' End Or Without A Lobby: Everybody Leaving The Match, The Owner Closing It, A Room Of The Control Interface And A Public Room Of A Game's Block Are Over As Before (The Code Keeps The Ended Room, A Key Is Told NoSuchRoom); And None When The Pool Has No Place (Every Lobby In Use)") {
        {   // everybody leaves: the match ends "everybody left", no lobby is opened
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00003", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p, false);
            p.pia_game->session->leave();                                                           // (both leave the match on purpose: their seats go at once)
            p.bob_game->session->leave();
            for (int i = 0; i < 400 && w.status("rmt00003").state == RoomState::Running; ++i) w.run(10);
            ASSERT_TRUE(w.status("rmt00003").state == RoomState::Finished && w.status("rmt00003").reason == "everybody left");
            w.run(300);
            ASSERT_TRUE(w.mgr.rooms_reopened() == 0u && w.mgr.retired_count() == 0u && !lobby_of(w, "rmt00003").has_value());
            Client& back = w.connect_game("Pia", "rmt00003", p.pia);
            ASSERT_EQ(refusal_of(w, back), net::RejectReason::NoSuchRoom);
        }
        {   // the owner closes the match: failed, no lobby
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00004", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p, false);
            ASSERT_TRUE(w.mgr.close_room("rmt00004", w.now));
            w.run(300);
            ASSERT_TRUE(w.status("rmt00004").state == RoomState::Failed && w.mgr.rooms_reopened() == 0u && !lobby_of(w, "rmt00004").has_value());
        }
        {   // a room of the control interface, and a public room that a game's block made, whose matches end by the rules: no lobby
            World w(rematch_limits());
            ASSERT_TRUE(w.mgr.create_room(spec_of("RMT-CTL", 2), w.now).ok);
            Client& ann = w.connect("Ann", "RMT-CTL");
            Client& bob = w.connect("Bob", "RMT-CTL");
            Client& gus = w.connect_creating("Gus", "rmt00005", block_of("TINY.LVL", 2));
            Client& gil = w.connect("Gil", "rmt00005");
            w.run(kPre + 4000);
            for (Client* quitter : {&bob, &gil}) {
                ASSERT_TRUE(quitter->session != nullptr);
                sim::Command q;
                q.type = sim::CommandType::Quit;
                q.issuer = quitter->lobby->my_seat();
                ASSERT_TRUE(quitter->session->submit(q));
            }
            w.run(3000);
            ASSERT_TRUE(w.status("RMT-CTL").state == RoomState::Finished && w.status("rmt00005").state == RoomState::Finished);
            ASSERT_TRUE(w.mgr.rooms_reopened() == 0u && w.mgr.retired_count() == 0u);
            ASSERT_TRUE(ann.sim.is_match_over() && gus.sim.is_match_over());
        }
        {   // the pool has no place: three lobbies with a person in each, and nothing idle long enough to give up: the match's room keeps its code as before
            ServerLimits l = rematch_limits();
            l.demo_lobbies = 3;
            l.lobby_idle_evict_ms = 0;
            World w(l);
            Played p;
            play_and_quit(w, "rmt00006", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p, false);
            for (const char* other : {"busy0001", "busy0002", "busy0003"}) w.connect_page("Lou", other, lobby_block_of());
            w.run(300);
            sim::Command q;
            q.type = sim::CommandType::Quit;
            q.issuer = p.bob_game->lobby->my_seat();
            ASSERT_TRUE(p.bob_game->session->submit(q));
            for (int i = 0; i < 400 && w.status("rmt00006").state == RoomState::Running; ++i) w.run(10);
            w.run(300);
            ASSERT_TRUE(w.status("rmt00006").state == RoomState::Finished && w.mgr.rooms_reopened() == 0u && !lobby_of(w, "rmt00006").has_value());
            Client& back = w.connect_game("Pia", "rmt00006", p.pia);
            ASSERT_EQ(refusal_of(w, back), net::RejectReason::NoSuchRoom);
        }
    } TEST_END();

    TEST_CASE("S3.197 The Life Of A Rematch Lobby: It Goes A Minute After Nobody Is In It (Nobody Came: Its Reservations Go With It, A Key Is Then Told NoSuchRoom And A Page's Block Makes A New Lobby); A Results Screen Keeps It, A Results Screen That Goes Leaves The Seat Reserved; An Empty One Gives Its Place Up To A New Page's Lobby When The Pool Is Full; A Reservation Runs Out After lobby_reserve_ms") {
        {   // nobody comes: forgotten a minute after the match ended, without a word (no result file for a lobby)
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00007", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            p.pia_game->end->close();                                                               // (the games leave their results screens without a word)
            p.bob_game->end->close();
            w.run(31000);                                                                           // (the match's room is forgotten after its keep time)
            ASSERT_TRUE(lobby_of(w, "rmt00007").has_value() && w.mgr.retired_count() == 0u);
            w.mgr.take_ended(w.now);
            w.run(31000);                                                                           // a minute after the reopening
            ASSERT_FALSE(lobby_of(w, "rmt00007").has_value());
            ASSERT_TRUE(w.mgr.room_count() == 0u && w.mgr.take_ended(w.now).empty());
            Client& late = w.connect_game("Pia", "rmt00007", p.pia);
            ASSERT_EQ(refusal_of(w, late), net::RejectReason::NoSuchRoom);
            Client& fresh = w.connect_page("Pia", "rmt00007", lobby_block_of());                   // (the page that is told so makes the room again without the key: a new lobby, nobody reserved)
            w.run(300);
            ASSERT_TRUE(fresh.lobby->created() && fresh.lobby->is_leader() && lobby_of(w, "rmt00007")->reserved == 0 && !lobby_of(w, "rmt00007")->reopened);
        }
        {   // a results screen keeps it alive; when it goes, the seat stays reserved and the room goes a minute later
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00008", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            Client& pia_results = w.connect_game("Pia", "rmt00008", p.pia);
            w.run(120000);
            ASSERT_TRUE(lobby_of(w, "rmt00008").has_value() && lobby_of(w, "rmt00008")->reserved == 0x3);
            ASSERT_TRUE(pia_results.lobby->phase() == net::ClientLobby::Phase::InRoom);
            pia_results.end->close();
            w.run(300);
            ASSERT_TRUE(lobby_of(w, "rmt00008").has_value() && lobby_of(w, "rmt00008")->reserved == 0x3);
            w.run(61000);
            ASSERT_FALSE(lobby_of(w, "rmt00008").has_value());
        }
        {   // an empty rematch lobby gives its place up when the pool is full and a new page needs one
            ServerLimits l = rematch_limits();
            l.demo_lobbies = 2;
            World w(l);
            Played p;
            play_and_quit(w, "rmt00009", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            ASSERT_TRUE(lobby_of(w, "rmt00009").has_value());
            w.connect_page("Lou", "full0001", lobby_block_of());
            w.run(300);
            Client& newcomer = w.connect_page("Ned", "full0002", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(newcomer.lobby->phase() == net::ClientLobby::Phase::InRoom && newcomer.lobby->created());
            ASSERT_FALSE(lobby_of(w, "rmt00009").has_value());
        }
        {   // a reservation runs out (here after 90 s): the seat is free, the results screen that held it is told
            ServerLimits l = rematch_limits();
            l.lobby_reserve_ms = 90000;
            World w(l);
            Played p;
            play_and_quit(w, "rmt00010", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            Client& bob_results = w.connect_game("Bob", "rmt00010", p.bob);
            w.next_key = p.pia;
            Client& pia_back = w.connect_page("Pia", "rmt00010", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(60000);
            ASSERT_TRUE(lobby_of(w, "rmt00010")->reserved == 0x2);
            w.run(31000);
            ASSERT_TRUE(lobby_of(w, "rmt00010")->reserved == 0 && lobby_of(w, "rmt00010")->names[1].empty());
            ASSERT_TRUE(bob_results.lobby->phase() == net::ClientLobby::Phase::Rejected && bob_results.lobby->reject_reason() == net::RejectReason::Kicked);
            ASSERT_TRUE(pia_back.lobby->is_leader());
        }
    } TEST_END();
}
