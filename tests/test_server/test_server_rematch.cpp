// Tests of the dedicated game server: the rematch of a lobby room (docs/SERVER.md "Lobby rooms"). When the rules end the match that a lobby room began, the server opens a lobby room with the same code at once,
// with the match's plan and a seat reserved for the key of each of its people; the room of the match is retired (it answers its last connections and keeps its status for its keep time).
#include "server_fixtures.hpp"

#include <regex>

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
// quit ends the match by its rules (every machine shows the results). The keys of the two seats, and the two games. Bob's game tells the room that it runs on `bob_platform` (protocol 15; 0: it does not say).
struct Played {
    net::SeatKey pia{};
    net::SeatKey bob{};
    Client* pia_game{nullptr};
    Client* bob_game{nullptr};
};
void play_and_quit(World& w, const std::string& code, const net::PlanMsg& plan, Played& out, bool quit = true, uint8_t bob_platform = 0) {
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
    w.next_platform = bob_platform;
    out.bob_game = &w.connect_game("Bob", code, out.bob);
    w.next_platform = 0;
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

    TEST_CASE("S3.194 The Rematch, The Round Trip: The Rules End A Lobby Room's Match And The Server Opens A Lobby With The Same Code At Once (The Match's Map, Colours, Bots, Teams; A Seat Reserved For Each Person's Key; Nobody Leads Yet); The Code's Status Is The Lobby's And The Match Is Listed Beside It For Its Keep Time, Its End Is Reported Once; A Results Screen Holds Its Seat, A Stranger Is Turned Away, The Host's Page Comes Back And Leads, START Waits For The One Who Is Not Back (The Host Is Told), The Host Removes Them And START Is Taken") {
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

    TEST_CASE("S3.198 Whose Seat Is Kept, And When There Is No Rematch: A Game Of The Desktop Has No Seat Kept (Its Colour Is Open; It Joins By The Code Like Anybody Once Somebody Is Back), A Key Of Another Room's Match Is A Stranger's, Two Tabs With One Key Are The Take-Over Of Any Lobby; A Server With No Room For Another Room (max_rooms) Opens No Lobby; With The Default Limits A Kept Seat Waits Three Hours On The Server's Clock") {
        {   // Bob played on the desktop (Linux): only Pia's seat is kept; Bob's game joins the lobby by the code like anybody, once Pia is back
            World w(rematch_limits());
            const std::string code = "rmt00011";
            Played p;
            play_and_quit(w, code, plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p, true, net::kOsLinux);
            std::optional<RoomStatus> lobby = lobby_of(w, code);
            ASSERT_TRUE(lobby.has_value() && lobby->reopened && lobby->reserved == 0x1);
            ASSERT_TRUE(lobby->names[0] == "Pia" && lobby->names[1].empty() && lobby->plan == "oonn -");
            w.next_platform = net::kOsLinux;
            Client& bob_early = w.connect("Bob", code);                                                // (a game's Hello with no key: the desktop's join by the code)
            w.next_platform = 0;
            ASSERT_EQ(refusal_of(w, bob_early), net::RejectReason::MatchRunning);
            w.next_key = p.pia;
            Client& pia_back = w.connect_page("Pia", code, lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(pia_back.lobby->is_leader() && lobby_of(w, code)->reserved == 0);
            w.next_platform = net::kOsLinux;
            Client& bob = w.connect("Bob", code);
            w.next_platform = 0;
            w.run(300);
            ASSERT_TRUE(bob.lobby->phase() == net::ClientLobby::Phase::InRoom && bob.lobby->my_seat() == 1 && !net::key_matches(bob.lobby->key(), p.bob));
            ASSERT_EQ(games_of(pia_back.lobby->room()), std::string("0100"));                          // (Bob is a game in the room, and nobody is waited for)
            ASSERT_TRUE(pia_back.lobby->request_start());
            w.run(300);
            ASSERT_TRUE(lobby_of(w, code).has_value() && lobby_of(w, code)->starting);
        }
        {   // a key of another room's match is a stranger's; two tabs with one key: the later takes the seat over
            World w(rematch_limits());
            Played a;
            Played b;
            play_and_quit(w, "rmt00012", plan_of({K::Open, K::Open, K::Open, K::Nobody}), a);
            play_and_quit(w, "rmt00013", plan_of({K::Open, K::Open, K::Open, K::Nobody}), b);
            ASSERT_TRUE(lobby_of(w, "rmt00012")->reserved == 0x3 && lobby_of(w, "rmt00013")->reserved == 0x3);
            w.next_key = a.pia;                                                                         // Pia of the first match at the second one: nobody of it is back
            Client& lost = w.connect_page("Pia", "rmt00013", lobby_block_of());
            w.next_key = net::SeatKey{};
            ASSERT_EQ(refusal_of(w, lost), net::RejectReason::MatchRunning);
            w.next_key = b.bob;                                                                         // Bob of the second match is back there
            Client& bob_back = w.connect_page("Bob", "rmt00013", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(bob_back.lobby->is_leader());
            w.next_key = a.pia;                                                                         // now she is a newcomer there: the open colour 2, a key of her own; her seat at the first stays kept
            Client& stranger = w.connect_page("Pia", "rmt00013", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(stranger.lobby->phase() == net::ClientLobby::Phase::InRoom && stranger.lobby->my_seat() == 2 && !net::key_matches(stranger.lobby->key(), a.pia));
            ASSERT_TRUE(lobby_of(w, "rmt00013")->reserved == 0x1 && lobby_of(w, "rmt00012")->reserved == 0x3);
            w.next_key = b.bob;                                                                         // Bob's second tab: it takes his seat, the first is told so
            Client& bob_tab2 = w.connect_page("Bob", "rmt00013", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(bob_back.lobby->phase() == net::ClientLobby::Phase::Rejected && bob_back.lobby->reject_reason() == net::RejectReason::Superseded);
            ASSERT_TRUE(bob_tab2.lobby->my_seat() == 1 && bob_tab2.lobby->is_leader() && lobby_of(w, "rmt00013")->reserved == 0x1);
        }
        {   // the server has no room for another room: no lobby, the code keeps the match that is over
            ServerLimits l = rematch_limits();
            l.max_rooms = 2;
            World w(l);
            ASSERT_TRUE(w.mgr.create_room(spec_of("RMT-FULL", 2), w.now).ok);                       // (a room of the control interface: no lobby, nothing to give up)
            Played p;
            play_and_quit(w, "rmt00014", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            w.run(300);
            ASSERT_TRUE(w.status("rmt00014").state == RoomState::Finished && w.mgr.rooms_reopened() == 0u && w.mgr.retired_count() == 0u && !lobby_of(w, "rmt00014").has_value());
            Client& back = w.connect_game("Pia", "rmt00014", p.pia);
            ASSERT_EQ(refusal_of(w, back), net::RejectReason::NoSuchRoom);
        }
        {   // the default limits: a kept seat waits three hours on the server's clock (Pia is back and keeps the room; Bob never comes)
            ASSERT_EQ(ServerLimits{}.lobby_reserve_ms, net::kLobbyReserveMs);
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00015", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            const uint32_t since = w.now;                                                               // (the lobby is opened in the pass that ends the match)
            ASSERT_TRUE(lobby_of(w, "rmt00015").has_value() && lobby_of(w, "rmt00015")->reserved == 0x3);
            w.next_key = p.pia;
            Client& pia_back = w.connect_page("Pia", "rmt00015", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            w.run_coarse(net::kLobbyReserveMs - (w.now - since) - 1000u, 1000);
            ASSERT_TRUE(lobby_of(w, "rmt00015").has_value() && lobby_of(w, "rmt00015")->reserved == 0x2 && pia_back.lobby->phase() == net::ClientLobby::Phase::InRoom);
            w.run(990);
            ASSERT_TRUE(w.now - since == net::kLobbyReserveMs - 10u && lobby_of(w, "rmt00015")->reserved == 0x2);
            w.run(10);
            ASSERT_TRUE(lobby_of(w, "rmt00015")->reserved == 0 && lobby_of(w, "rmt00015")->names[1].empty() && pia_back.lobby->is_leader());
        }
    } TEST_END();

    TEST_CASE("S3.200 Who Left The Match Has No Seat Kept: Cid Leaves In The Middle (Leave Game: The Match Drops His Seat) And Bob's Quit Ends It (One Other Side Left: Bob Stays In The Match, So His Seat Is Kept); The Lobby Keeps Pia's And Bob's Seats, Cid's Colour Is Open And START Waits For Bob Only; Cid's Key Is Then A Newcomer's; A Game Page That Asks For Its Colour With The Key (The Front Page's Rejoin) Is The Person Back, As A Game") {
        World w(rematch_limits());
        const std::string code = "rmt00016";
        Client& pia = w.connect_page("Pia", code, lobby_block_of());
        Client& bob = w.connect_page("Bob", code, lobby_block_of());
        Client& cid = w.connect_page("Cid", code, lobby_block_of());
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader() && bob.lobby->my_seat() == 1 && cid.lobby->my_seat() == 2);
        ASSERT_TRUE(pia.lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Nobody})));
        w.run(300);
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(300);
        const net::SeatKey kp = pia.lobby->key(), kb = bob.lobby->key(), kc = cid.lobby->key();
        for (Client* page : {&pia, &bob, &cid}) page->end->close();
        Client& pia_game = w.connect_game("Pia", code, kp);
        Client& bob_game = w.connect_game("Bob", code, kb);
        Client& cid_game = w.connect_game("Cid", code, kc);
        ASSERT_TRUE(until_running(w, code, 8000));
        w.run(kPre + 3000);
        ASSERT_TRUE(cid_game.session != nullptr);
        cid_game.session->leave();                                                                  // Cid leaves the match: his seat is dropped
        w.run(1000);
        ASSERT_TRUE(pia_game.sim.is_player_dropped(2) && !pia_game.sim.is_player_dropped(1));
        sim::Command q;                                                                             // Bob quits with one other side left: the quit ends the match by its rules
        q.type = sim::CommandType::Quit;
        q.issuer = 1;
        ASSERT_TRUE(bob_game.session->submit(q));
        for (int i = 0; i < 400 && w.status(code).state == RoomState::Running; ++i) w.run(10);
        ASSERT_TRUE(w.ended(code).state == RoomState::Finished && w.ended(code).reason == "the match ended" && !pia_game.sim.is_player_dropped(1));
        std::optional<RoomStatus> lobby = lobby_of(w, code);
        ASSERT_TRUE(lobby.has_value() && lobby->reopened && lobby->reserved == 0x3);
        ASSERT_TRUE(lobby->names[0] == "Pia" && lobby->names[1] == "Bob" && lobby->names[2].empty() && lobby->plan == "ooon -");      // (Cid's colour is an open colour of the plan)
        // Pia's game page, opened again by the front page's Rejoin with her key (it asks for her colour): she is back, as a game, and leads; START waits for Bob only
        w.next_key = kp;
        Client& pia_rejoin = w.connect("Pia", code, 0);
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(pia_rejoin.lobby->phase() == net::ClientLobby::Phase::InRoom && pia_rejoin.lobby->my_seat() == 0 && pia_rejoin.lobby->is_leader());
        ASSERT_TRUE(lobby_of(w, code)->reserved == 0x2 && lobby_of(w, code)->games == 0x1);
        ASSERT_TRUE(pia_rejoin.lobby->request_start());
        w.run(300);
        ASSERT_FALSE(lobby_of(w, code)->starting);
        ASSERT_EQ(said(pia_rejoin.room_chat), (std::vector<std::string>{std::string("255||Bob") + net::kNoticeNotBack}));
        // Cid's key fits no kept seat: his page is a newcomer's, and takes the open colour like anybody
        w.next_key = kc;
        Client& cid_back = w.connect_page("Cid", code, lobby_block_of());
        w.next_key = net::SeatKey{};
        w.run(300);
        ASSERT_TRUE(cid_back.lobby->phase() == net::ClientLobby::Phase::InRoom && cid_back.lobby->my_seat() == 2 && !net::key_matches(cid_back.lobby->key(), kc));
        ASSERT_TRUE(lobby_of(w, code)->reserved == 0x2 && pia_rejoin.lobby->is_leader());
    } TEST_END();

    TEST_CASE("S3.201 The Room Of The Match That Gave Its Code To Its Rematch (Retired): It Counts Against --max-rooms (The Control Interface Makes No Room Until Its Keep Time Is Over) And In The Control Interface's GET /stats; It Keeps Its Public Place (The Rematch's START Finds None While It Holds The Only One); It Is Still Served (Its Games' Links Close After The Grace Period, Before Its Keep Time Is Over); A Stop Of The Server Closes Its Links") {
        const auto call = [](World& w, const char* method, const std::string& path, const std::string& body = std::string()) {
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
        {   // --max-rooms counts it, and so does GET /stats
            ServerLimits l = rematch_limits();
            l.max_rooms = 2;
            World w(l);
            Played p;
            play_and_quit(w, "rmt00017", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            ASSERT_TRUE(w.mgr.rooms_reopened() == 1u && w.mgr.room_count() == 1u && w.mgr.retired_count() == 1u);
            ASSERT_EQ(json_of(call(w, "GET", "/stats")).get("rooms").as_int_or(-1), 2);
            ASSERT_EQ(call(w, "POST", "/rooms", R"({"map":"TINY.LVL","code":"RMT-CTL2"})").status, 503);
            w.run(31000);                                                                               // the match's keep time is over: it is forgotten
            ASSERT_EQ(w.mgr.retired_count(), 0u);
            ASSERT_EQ(json_of(call(w, "GET", "/stats")).get("rooms").as_int_or(-1), 1);
            ASSERT_EQ(call(w, "POST", "/rooms", R"({"map":"TINY.LVL","code":"RMT-CTL2"})").status, 201);
        }
        {   // the only public place: the rematch's START waits for it (the leader is told), and is taken once the match is forgotten
            ServerLimits l = rematch_limits();
            l.demo_rooms = 1;
            World w(l);
            const std::string code = "rmt00018";
            Played p;
            play_and_quit(w, code, plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            ASSERT_EQ(w.mgr.retired_count(), 1u);
            w.next_key = p.pia;
            Client& pia = w.connect_page("Pia", code, lobby_block_of());
            w.next_key = p.bob;
            Client& bob = w.connect_page("Bob", code, lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(pia.lobby->is_leader() && lobby_of(w, code)->reserved == 0);
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            pia.end->close();
            bob.end->close();
            Client& pia_game = w.connect_game("Pia", code, p.pia);
            w.connect_game("Bob", code, p.bob);
            w.run(1000);
            ASSERT_TRUE(lobby_of(w, code).has_value() && !lobby_of(w, code)->starting);               // no place: the START ends, the lobby waits
            ASSERT_EQ(said(pia_game.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeNoPlace}));
            w.run(30000);                                                                                // the match is forgotten: its place is free
            ASSERT_EQ(w.mgr.retired_count(), 0u);
            ASSERT_TRUE(pia_game.lobby->request_start());
            ASSERT_TRUE(until_running(w, code, 8000));
        }
        {   // it is served: its games' links close at the end of the grace period (everybody is level), while it is still listed
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00019", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            const uint32_t ended = w.now;
            ASSERT_TRUE(p.pia_game->server_end->is_open() && p.bob_game->server_end->is_open() && w.mgr.retired_count() == 1u);
            w.run(Room::kGraceMs - 1500 - (w.now - ended));
            ASSERT_TRUE(p.pia_game->server_end->is_open());
            w.run(3000);
            ASSERT_TRUE(!p.pia_game->server_end->is_open() && !p.bob_game->server_end->is_open() && w.mgr.retired_count() == 1u);
        }
        {   // a stop of the server closes its links at once
            World w(rematch_limits());
            Played p;
            play_and_quit(w, "rmt00020", plan_of({K::Open, K::Open, K::Nobody, K::Nobody}), p);
            ASSERT_TRUE(p.pia_game->server_end->is_open() && w.mgr.retired_count() == 1u);
            w.mgr.shutdown(w.now);
            ASSERT_TRUE(!p.pia_game->server_end->is_open() && !p.bob_game->server_end->is_open());
        }
    } TEST_END();

    TEST_CASE("S3.202 The Link To The Match's Stats: A Lobby Room's Match That Runs 30 Seconds Is Recorded, And The Public List Of Replays Names Its Players As tests/data/replay_players.txt Says (The Colour, And The Name In Brackets; The Front Page Finds The Match By Them: web_rematch_check.js); Every Line Of That File Is What The Server's Own Functions Make Of Its Names; The File's Name Is An Id That The Page Takes") {
        std::vector<std::pair<std::array<std::string, 4>, std::string>> golden;                    // (the four names, the labels joined by '|')
        {
            std::ifstream in(std::string(TEST_DATA_DIR) + "/replay_players.txt");
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                const size_t arrow = line.find(" => ");
                ASSERT_TRUE(arrow != std::string::npos);
                std::array<std::string, 4> names;
                std::string rest = line.substr(0, arrow);
                for (size_t s = 0; s < 4; ++s) {
                    const size_t bar = rest.find('|');
                    names[s] = s < 3 ? rest.substr(0, bar) : rest;
                    ASSERT_TRUE(s == 3 || bar != std::string::npos);
                    if (s < 3) rest = rest.substr(bar + 1);
                }
                golden.emplace_back(names, line.substr(arrow + 4));
            }
        }
        ASSERT_TRUE(golden.size() >= 3u);
        // every line: the labels that a recording of these names gets (a person's name as replay_person_name keeps it, a seat with none is its colour; a seat that did not play has none)
        for (const auto& g : golden) {
            replay::Header head;
            for (uint8_t s = 0; s < 4; ++s) {
                if (g.first[s].empty()) continue;
                head.roster = static_cast<uint8_t>(head.roster | (1u << s));
                head.names[s] = replay_person_name(g.first[s]);
            }
            std::string labels;
            for (uint8_t s = 0; s < 4; ++s) {
                if ((head.roster & (1u << s)) != 0) labels += (labels.empty() ? "" : "|") + replay::seat_label(head, s);
            }
            ASSERT_EQ(labels, g.second);
        }
        // the first line is a real match: Pia, Bob and the Medium bot of the plan, in a lobby room of the code rmt00021, recorded and listed by the server
        World w(rematch_limits());
        const std::string code = "rmt00021";
        ASSERT_TRUE(golden[0].first[0] == "Pia" && golden[0].first[1] == "Bob" && golden[0].first[3].empty());
        ASSERT_EQ(golden[0].first[2], bn(code, 2, "Medium"));                                             // (the bot's name in this room)
        int64_t clock_s = 1760600000;
        ReplayConfig rc;
        rc.dir = (fs::path(temp_dir_for("rematch-replays")) / "replays").string();
        rc.game_version = "v0.0.0-test";
        rc.build_id = "abc123";
        rc.min_free_bytes = 0;
        rc.clock_s = [&clock_s]() { return clock_s; };
        std::string why;
        ASSERT_MSG(w.mgr.enable_replays(rc, true, why), why);
        Played p;
        play_and_quit(w, code, plan_of({K::Open, K::Open, K::Medium, K::Nobody}, 0, 2), p, false);    // (Pia and the bot against Bob: his quit ends the match)
        w.run(28000);                                                                                  // (a match is kept when it ran 30 seconds)
        sim::Command q;
        q.type = sim::CommandType::Quit;
        q.issuer = 1;
        ASSERT_TRUE(p.bob_game->session->submit(q));
        for (int i = 0; i < 400 && w.status(code).state == RoomState::Running; ++i) w.run(10);
        ASSERT_TRUE(w.ended(code).state == RoomState::Finished && w.ended(code).replay_kept && lobby_of(w, code).has_value());
        ctl::HttpRequest rq;
        rq.method = "GET";
        rq.path = "/replays";
        const ctl::HttpResponse r = handle_public_replays(w.mgr, rq);
        ctl::JsonValue j;
        ASSERT_TRUE(r.status == 200 && ctl::parse_json(r.body, j, &why));
        ASSERT_EQ(j.get("replays").size(), size_t{1});
        const ctl::JsonValue& e = j.get("replays").at(0);
        std::string players;
        for (size_t i = 0; i < e.get("players").size(); ++i) players += (i == 0 ? "" : "|") + e.get("players").at(i).str();
        ASSERT_EQ(players, golden[0].second);
        ASSERT_TRUE(e.get("finished").as_bool_or(false) && e.get("map").str() == "TINY.LVL" && e.get("ended").as_int_or(0) == clock_s);
        const std::string file = e.get("file").str();
        ASSERT_TRUE(file.size() > 8 && file.compare(file.size() - 8, 8, ".antsrep") == 0);
        ASSERT_TRUE(std::regex_match(file.substr(0, file.size() - 8), std::regex("ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?")));      // (web/front/rematch.js ID_RE)
    } TEST_END();

    TEST_CASE("S3.203 A Restart Record Of A Code Whose Earlier Record Could Not Be Deleted (The Rematch's Match Has The Code Of The Match Before It, And The Record's Name Is Made Of The Code): The New Record Replaces The Old File And Is Taken Off The List Of Files To Delete Again, So The Retry Every 10 s Does Not Delete It; A File That Nothing Replaced Is Still Deleted When It Can Be") {
#ifndef _WIN32
        RestartConfig cfg = test_restart_config("rematch-stale");
        std::string why;
        RestartStore store(cfg);
        ASSERT_MSG(store.prepare(why), why);
        const auto make_undeletable = [](const std::string& path) {                                   // (a folder that is not empty takes the file's name: the delete fails)
            fs::rename(path, path + ".aside");
            fs::create_directory(path);
            write_all_bytes(fs::path(path) / "x", {1});
        };
        const std::string path = store.path_for("rmt00022");
        auto first = store.create(sample_head("rmt00022"), why);
        ASSERT_TRUE(first != nullptr && first->append_turn(sample_turn(0)));
        make_undeletable(path);
        first->discard();                                                                              // the match is over: its record is to go, and cannot
        ASSERT_EQ(store.stale_count(), size_t{1});
        fs::remove_all(path);                                                                          // the obstacle goes before the next try
        auto second = store.create(sample_head("rmt00022"), why);                                     // the rematch's match: a record under the same name
        ASSERT_TRUE(second != nullptr && second->append_turn(sample_turn(0)) && second->sync());
        ASSERT_EQ(store.stale_count(), size_t{0});
        ASSERT_EQ(store.retry_stale(), size_t{0});
        ASSERT_TRUE(fs::is_regular_file(path));                                                        // the record of the match that runs is there
        // a file that nothing replaced: deleted at the next try once it can be
        const std::string other = store.path_for("rmt00023");
        auto third = store.create(sample_head("rmt00023"), why);
        ASSERT_TRUE(third != nullptr);
        make_undeletable(other);
        third->discard();
        ASSERT_EQ(store.stale_count(), size_t{1});
        fs::remove(fs::path(other) / "x");
        ASSERT_EQ(store.retry_stale(), size_t{0});
        ASSERT_FALSE(fs::exists(other));
        ASSERT_TRUE(fs::is_regular_file(path));
#endif
    } TEST_END();
}
