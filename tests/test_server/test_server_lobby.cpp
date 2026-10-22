// Tests of the dedicated game server: the lobby rooms that the front page waits in (protocol 16)
#include "server_fixtures.hpp"

// ---------------------------------------------------------------------------------------------------------------------------------
// Lobby rooms (protocol 16): the room that the front page waits in, on the server
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

// The options of a server that offers the front page's lobbies: the public rooms, the maps of the page, and --demo-lobbies
ServerLimits lobby_limits() {
    ServerLimits l;
    l.demo_rooms = 4;
    l.demo_lobbies = 3;
    l.demo_map = "TINY.LVL";
    l.demo_maps = {"TINY.LVL", "SMALL.LVL", "GAUNTLET.LVL"};
    return l;
}

// A leader's plan: what each color is, the teams (kNoTeam: none) and the map ("": the room keeps its own)
net::PlanMsg plan_msg(const std::array<net::PlanKind, 4>& kinds, uint8_t team_a = net::kNoTeam, uint8_t team_b = net::kNoTeam, const std::string& map = std::string()) {
    net::PlanMsg m;
    m.map_name = map;
    m.plan = kinds;
    m.team_a = team_a;
    m.team_b = team_b;
    return m;
}

// The answer that a Hello got (0 when it was seated): the reason of its Reject
net::RejectReason answer_of(World& w, Client& c) {
    w.run(300);
    return c.lobby->phase() == net::ClientLobby::Phase::Rejected ? c.lobby->reject_reason() : static_cast<net::RejectReason>(0);
}

// What the room says to its leader for each START that one game cannot load the map of (a Classic map here): whose game it is, and how to start without them - `times` times over
std::vector<std::string> stale_lines(const std::string& who, size_t times) {
    std::vector<std::string> lines;
    for (size_t i = 0; i < times; ++i) {
        lines.push_back("255||" + who + "'s game could not load this map.");
        lines.push_back("255||To start without " + who + ", press Remove on their color.");
    }
    return lines;
}

// How many lobby rooms wait on the server
size_t waiting_lobbies(World& w) {
    size_t n = 0;
    for (const RoomStatus& s : w.mgr.list(w.now)) n += s.lobby && s.state == RoomState::Waiting ? 1u : 0u;
    return n;
}

// Two pages whose Hellos reach the door in the same pass (links with no jitter), the first for `first_room` and the second for `second_room`, both with the lobby block; runs 300 ms
std::pair<Client*, Client*> two_pages_at_once(World& w, const char* first_room, const char* second_room) {
    w.next_kind = net::kClientPage;
    w.next_create = lobby_block_of();
    Client& first = w.connect("First", first_room, 255, {20, 0});
    Client& second = w.connect("Second", second_room, 255, {20, 0});
    w.next_kind = net::kClientGame;
    w.next_create.reset();
    w.run(300);
    return {&first, &second};
}

}  // namespace

void run_lobby_room_tests() {
    using K = net::PlanKind;
    const std::array<K, 4> all_open = {K::Open, K::Open, K::Open, K::Open};

    TEST_CASE("S3.132 Lobby Rooms (Protocol 16): Off Unless The Server Offers Them (--demo-lobbies, With The Public Rooms, A Map And Reconnect); A Page's Hello With The Lobby Block Makes The Room When There Is None And The Welcome Of Its Seat Says So, A Second Page Or A Game Finds It; A Page Makes Only A Lobby Room And A Game Only A Plain One; A Lobby That Waits Holds No Place Of The Public Rooms") {
        for (int missing = 0; missing < 4; ++missing) {                                   // what a lobby stands on: lobbies asked for, the public rooms, a map, reconnect (the keys)
            ServerLimits l = lobby_limits();
            if (missing == 0) l.demo_lobbies = 0;
            if (missing == 1) l.demo_rooms = 0;
            if (missing == 2) l.demo_map.clear();
            if (missing == 3) l.reconnect = false;
            World w(l);
            Client& pia = w.connect_page("Pia", "k7m2xq", lobby_block_of());
            ASSERT_MSG(answer_of(w, pia) == net::RejectReason::NoSuchRoom, std::to_string(missing));
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "k7m2xq", lobby_block_of());
        w.run(300);
        ASSERT_EQ(pia.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(pia.lobby->created() && pia.lobby->is_leader() && !net::key_is_zero(pia.lobby->key()));
        ASSERT_TRUE(pia.lobby->room().lobby() && !pia.lobby->room().starting());
        {
            const RoomStatus s = w.status("k7m2xq");
            ASSERT_TRUE(s.lobby && s.public_room && s.leader_starts && s.early_start && s.state == RoomState::Waiting && !s.starting);
            ASSERT_EQ(static_cast<int>(s.expected), 4);
            ASSERT_EQ(s.map, std::string("TINY.LVL"));
            ASSERT_EQ(s.plan, std::string("oooo -"));
            ASSERT_EQ(static_cast<int>(s.games), 0);                                      // a page is no game
            ASSERT_EQ(static_cast<int>(s.joined), 1);
            ASSERT_EQ(static_cast<int>(s.leader), 0);
        }
        Client& bob = w.connect_page("Bob", "k7m2xq", lobby_block_of());                  // a second page finds the room: it did not make it
        w.run(300);
        ASSERT_EQ(bob.lobby->my_seat(), 1);
        ASSERT_FALSE(bob.lobby->created());
        Client& cat = w.connect("Cat", "k7m2xq");                                         // a game finds it too, and is the only one of the three that holds a game
        w.run(300);
        ASSERT_EQ(cat.lobby->my_seat(), 2);
        ASSERT_FALSE(cat.lobby->created());
        ASSERT_EQ(static_cast<int>(w.status("k7m2xq").games), 4);
        ASSERT_EQ(static_cast<int>(pia.lobby->room().in_game), 4);
        ASSERT_EQ(w.mgr.room_count(), size_t{1});
        {   // a page makes only a lobby room, and a game only a plain one; the other way round is the Hello of no honest client
            Client& plain_page = w.connect_page("Pat", "zzzz0001", block_of("", 4, net::kNoTeam, net::kNoTeam, true));
            ASSERT_EQ(answer_of(w, plain_page), net::RejectReason::BadRequest);
            w.next_create = lobby_block_of();
            Client& lobby_game = w.connect("Gil", "zzzz0002");
            w.next_create.reset();
            ASSERT_EQ(answer_of(w, lobby_game), net::RejectReason::BadRequest);
            Client& bare_page = w.connect_page("Pam", "zzzz0003", std::nullopt);          // no block: nothing to make the room from
            ASSERT_EQ(answer_of(w, bare_page), net::RejectReason::NoSuchRoom);
            Client& loud = w.connect_page("Lou", "ROOM-9", lobby_block_of());             // a code with a capital is the control interface's: a visitor's block never takes it
            ASSERT_EQ(answer_of(w, loud), net::RejectReason::NoSuchRoom);
            w.next_key = pia.lobby->key();                                                // a Hello with a key is a person who comes back, never a newcomer: it makes no room (its page makes the room again without the key)
            Client& stale = w.connect_page("Pia", "zzzz0004", lobby_block_of());
            w.next_key = net::SeatKey{};
            ASSERT_EQ(answer_of(w, stale), net::RejectReason::NoSuchRoom);
            Client& gus = w.connect_creating("Gus", "gus00001", block_of("TINY.LVL", 4)); // a plain public room, and a page that comes to it
            w.run(300);
            ASSERT_EQ(gus.lobby->phase(), net::ClientLobby::Phase::InRoom);
            Client& wrong = w.connect_page("Pem", "gus00001", lobby_block_of());
            ASSERT_EQ(answer_of(w, wrong), net::RejectReason::BadRequest);
            ASSERT_FALSE(w.status("gus00001").lobby);
            ASSERT_EQ(w.mgr.room_count(), size_t{2});
        }
        {   // the lobbies that wait hold no place of the public rooms: three lobbies (the pool), and four public rooms (the places) fit together; a fifth public room and a fourth lobby do not
            w.connect_page("Dee", "dddd0001", lobby_block_of());
            w.connect_page("Eli", "eeee0001", lobby_block_of());
            ASSERT_EQ(waiting_lobbies(w), size_t{3});
            for (const char* code : {"gus00002", "gus00003", "gus00004"}) w.connect_creating("G", code, block_of("TINY.LVL", 4));
            w.run(300);
            ASSERT_EQ(w.mgr.room_count(), size_t{7});
            Client& fifth = w.connect_creating("G5", "gus00005", block_of("TINY.LVL", 4));
            ASSERT_EQ(answer_of(w, fifth), net::RejectReason::NoSuchRoom);
            Client& fourth = w.connect_page("Fay", "ffff0001", lobby_block_of());       // every lobby has a person in it: none gives its place up
            ASSERT_EQ(answer_of(w, fourth), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(w.mgr.room_count(), size_t{7});
        }
    } TEST_END();

    TEST_CASE("S3.133 The Plan Through A Real Lobby Room (Protocol 16): The Leader's Map Is Taken When The Server Lists It (In Any Case, As The List Spells It) And Kept When It Does Not, The Colors And The Teams Are In The Status And In Every Page's Room Message; Another Player's Plan Is Not Heard") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "plan0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "plan0001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader() && !bob.lobby->is_leader());
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Open, K::Medium, K::Nobody}, 0, 1, "small.lvl")));
        w.run(300);
        RoomStatus s = w.status("plan0001");
        ASSERT_EQ(s.map, std::string("SMALL.LVL"));
        ASSERT_EQ(s.plan, std::string("oomn 0+1"));
        for (const Client* c : {&pia, &bob}) {
            ASSERT_EQ(c->lobby->room().map_name, std::string("SMALL.LVL"));
            ASSERT_TRUE(c->lobby->room().plan[2] == K::Medium && c->lobby->room().plan[3] == K::Nobody && c->lobby->room().team_a == 0 && c->lobby->room().team_b == 1);
        }
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg(all_open, net::kNoTeam, net::kNoTeam, "NOWHERE.LVL")));      // a map that the server does not have: the map stays, the rest of the plan is taken
        w.run(300);
        s = w.status("plan0001");
        ASSERT_EQ(s.map, std::string("SMALL.LVL"));
        ASSERT_EQ(s.plan, std::string("oooo -"));
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg(all_open, net::kNoTeam, net::kNoTeam, "MEDIUM.LVL")));      // a map of the store that --demo-maps does not list: taken too (the lobby may choose any map the server has; tests/test_server/test_map_list.cpp)
        w.run(300);
        ASSERT_EQ(w.status("plan0001").map, std::string("MEDIUM.LVL"));
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg(all_open, net::kNoTeam, net::kNoTeam, "gauntlet.lvl")));
        w.run(300);
        ASSERT_EQ(w.status("plan0001").map, std::string("GAUNTLET.LVL"));
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Open, K::Hard, K::Open})));                    // no map: the room keeps its own
        w.run(300);
        ASSERT_EQ(w.status("plan0001").map, std::string("GAUNTLET.LVL"));
        ASSERT_EQ(w.status("plan0001").plan, std::string("ooho -"));
        // Bob does not lead: his client does not send a plan, and one that is sent by hand is heard and ignored
        ASSERT_FALSE(bob.lobby->request_plan(plan_msg(all_open)));
        bob.end->send(net::encode(plan_msg({K::Easy, K::Easy, K::Easy, K::Easy}, net::kNoTeam, net::kNoTeam, "TINY.LVL")));
        w.run(300);
        s = w.status("plan0001");
        ASSERT_EQ(s.map, std::string("GAUNTLET.LVL"));
        ASSERT_EQ(s.plan, std::string("ooho -"));
    } TEST_END();

    TEST_CASE("S3.134 The Leader's START Through A Real Lobby Room (Protocol 16): It Waits For The Game Of Every Person (The Page's Own, Which Takes Its Seat With The Key), Nobody New Is Taken Meanwhile, A Game That Does Not Come Ends It With A Notice, And The Match Begins When The Last Game Is In; The Lobby Is Not A Match For /busy Until It Does") {
        ServerLimits l = lobby_limits();
        l.lobby_start_wait_ms = 6000;                                                     // (90 s by default)
        {
            World w(l);
            Client& pia = w.connect_page("Pia", "strt0001", lobby_block_of());
            Client& bob = w.connect_page("Bob", "strt0001", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            const net::SeatKey bob_key = bob.lobby->key();
            ASSERT_TRUE(!net::key_is_zero(pia_key) && !net::key_is_zero(bob_key) && pia_key != bob_key);
            ASSERT_TRUE(w.mgr.busy(w.now).matches == 0 && w.mgr.busy(w.now).players == 2);        // two people wait in a lobby: no match
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            RoomStatus s = w.status("strt0001");
            ASSERT_TRUE(s.starting && s.state == RoomState::Waiting && s.games == 0);
            ASSERT_TRUE(pia.lobby->room().starting() && bob.lobby->room().starting());
            Client& lou = w.connect_page("Lou", "strt0001", lobby_block_of());               // a newcomer is turned away while the request stands
            ASSERT_EQ(answer_of(w, lou), net::RejectReason::MatchRunning);
            Client& pia_game = w.connect_game("Pia", "strt0001", pia_key);                   // Pia's game takes her seat over: the page is told that it was replaced
            w.run(300);
            ASSERT_EQ(pia.lobby->phase(), net::ClientLobby::Phase::Rejected);
            ASSERT_EQ(pia.lobby->reject_reason(), net::RejectReason::Superseded);
            ASSERT_EQ(pia_game.lobby->my_seat(), 0);
            ASSERT_TRUE(pia_game.lobby->is_leader() && pia_game.lobby->rejoined() == false);
            s = w.status("strt0001");
            ASSERT_TRUE(s.starting && s.state == RoomState::Waiting && s.games == 1);        // one game of two
            Client& bob_game = w.connect_game("Bob", "strt0001", bob_key);                   // the last game: the match is started
            w.run(300);
            s = w.status("strt0001");
            ASSERT_TRUE(s.state == RoomState::Loading || s.state == RoomState::Running);
            ASSERT_FALSE(s.starting);
            w.run(3000);
            s = w.status("strt0001");
            ASSERT_TRUE(s.state == RoomState::Running);
            ASSERT_EQ(static_cast<int>(s.joined), 2);
            ASSERT_TRUE(s.names[0] == "Pia" && s.names[1] == "Bob" && s.names[2].empty());
            ASSERT_EQ(s.map, std::string("TINY.LVL"));
            ASSERT_EQ(pia_game.lobby->start_info().roster, 3);
            ASSERT_EQ(bob_game.lobby->start_info().map_name, std::string("TINY.LVL"));
            ASSERT_TRUE(w.mgr.busy(w.now).matches == 1 && w.mgr.busy(w.now).players == 2);        // now it is a match
            ASSERT_EQ(waiting_lobbies(w), size_t{0});                                         // (and the place of the pool is free again)
        }
        {   // a game that does not come: the request ends after the wait, the leader is told whose game it was, and the room waits on
            World w(l);
            Client& pia = w.connect_page("Pia", "strt0002", lobby_block_of());
            w.connect_page("Bob", "strt0002", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            ASSERT_TRUE(pia.lobby->request_start());
            Client& pia_game = w.connect_game("Pia", "strt0002", pia_key);                   // Pia's game comes, Bob's never does
            w.run(5000);
            ASSERT_TRUE(w.status("strt0002").starting);
            w.run(2000);
            const RoomStatus s = w.status("strt0002");
            ASSERT_TRUE(!s.starting && s.state == RoomState::Waiting && s.joined == 2 && s.games == 1);
            ASSERT_EQ(said(pia_game.room_chat), (std::vector<std::string>{std::string("255||") + "Bob" + net::kNoticeGameLate}));
        }
    } TEST_END();

    TEST_CASE("S3.135 The Plan's Bots And Teams Are The Match's: One Person And The Plan's Medium And Easy Bots Start A Match Of Three With The Plan's Teams, The Bots Are Named For Their Levels And Seated In The Colors Of The Plan, Nobody's Color Stays Empty") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "bots0001", lobby_block_of());
        const net::SeatKey key = pia.lobby->key();
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Easy}, 0, 1)));
        w.run(300);
        ASSERT_EQ(w.status("bots0001").plan, std::string("omne 0+1"));
        // protocol 20: before START the page already reads the names that the plan's bots will have, and START seats them under the same names
        ASSERT_EQ(pia.lobby->room().slots[1].name, bn("bots0001", 1, "Medium"));
        ASSERT_EQ(pia.lobby->room().slots[3].name, bn("bots0001", 3, "Easy"));
        ASSERT_TRUE(pia.lobby->room().slots[0].state == net::SlotState::Client && pia.lobby->room().slots[2].name.empty());
        ASSERT_TRUE(pia.lobby->request_start());                                          // one person is enough, with the plan's bots to make up the match
        w.run(300);
        ASSERT_TRUE(w.status("bots0001").starting);
        Client& game = w.connect_game("Pia", "bots0001", key);
        w.run(3500);
        const RoomStatus s = w.status("bots0001");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(static_cast<int>(s.joined), 3);
        ASSERT_EQ(s.bots.size(), size_t{2});
        ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].level == "medium" && s.bots[0].name == bn("bots0001", 1, "Medium") && s.bots[0].fill);
        ASSERT_TRUE(s.bots[1].seat == 3 && s.bots[1].level == "easy" && s.bots[1].name == bn("bots0001", 3, "Easy") && s.bots[1].fill);
        ASSERT_EQ(s.teams, std::string("0+1"));
        ASSERT_EQ(game.lobby->start_info().roster, 11);
        ASSERT_TRUE(s.bot_controller);
    } TEST_END();

    TEST_CASE("S3.186 The Plan's Game Mode Is The Match's (Protocol 17): The Leader's Plan Of A Lobby Room Sets The Mode (The Status, The Control Interface And Every Page's Room Message Say It), The START Puts It Into The Start Of The Match, The Leader's Game Makes Its Engine With It, And A Room Of A Page That Never Chose One Plays The Original's Game") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "mode0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "mode0001", lobby_block_of());
        const net::SeatKey key = pia.lobby->key();
        w.run(300);
        ASSERT_TRUE(pia.lobby->is_leader() && !bob.lobby->is_leader());
        ASSERT_EQ(static_cast<int>(w.status("mode0001").mode), 0);
        ASSERT_EQ(status_to_json(w.status("mode0001")).get("mode").str(), std::string("highest-score"));
        net::PlanMsg plan = plan_msg({K::Open, K::Open, K::Nobody, K::Nobody});
        plan.mode = static_cast<uint8_t>(sim::GameMode::Kills187);
        ASSERT_TRUE(pia.lobby->request_plan(plan));
        w.run(300);
        ASSERT_EQ(static_cast<int>(w.status("mode0001").mode), 1);
        ASSERT_EQ(status_to_json(w.status("mode0001")).get("mode").str(), std::string("187"));
        ASSERT_TRUE(pia.lobby->room().mode == 1 && bob.lobby->room().mode == 1);
        net::PlanMsg unknown = plan;                                                      // a plan that names a mode above the last never leaves a client (and is garbage on the wire)
        unknown.mode = static_cast<uint8_t>(sim::kLastGameMode + 1);
        ASSERT_FALSE(pia.lobby->request_plan(unknown));
        ASSERT_EQ(static_cast<int>(w.status("mode0001").mode), 1);
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(300);
        ASSERT_TRUE(w.status("mode0001").starting);
        Client& pia_game = w.connect_game("Pia", "mode0001", key);
        Client& bob_game = w.connect_game("Bob", "mode0001", bob.lobby->key());
        w.run(3500);
        const RoomStatus s = w.status("mode0001");
        ASSERT_TRUE(s.state == RoomState::Running && static_cast<int>(s.mode) == 1);
        for (Client* game : {&pia_game, &bob_game}) {
            ASSERT_EQ(game->lobby->start_info().mode, 1);
            ASSERT_TRUE(game->sim.game_mode() == sim::GameMode::Kills187);
        }
        {   // a page that never chose a mode starts the original's game
            World plain(lobby_limits());
            Client& ann = plain.connect_page("Ann", "mode0003", lobby_block_of());
            const net::SeatKey ann_key = ann.lobby->key();
            ASSERT_TRUE(ann.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Nobody})));
            plain.run(300);
            ASSERT_TRUE(ann.lobby->request_start());
            plain.run(300);
            Client& ann_game = plain.connect_game("Ann", "mode0003", ann_key);
            plain.run(3500);
            ASSERT_TRUE(plain.status("mode0003").state == RoomState::Running && static_cast<int>(plain.status("mode0003").mode) == 0);
            ASSERT_TRUE(ann_game.lobby->start_info().mode == 0 && ann_game.sim.game_mode() == sim::GameMode::HighestScore);
        }
    } TEST_END();

    TEST_CASE("S3.188 A Match Of Game Mode 187 That Starts With Teams Works From The Start To The End On Every Engine (Protocols 13 And 17): The Leader's Teams And The Room's Own Teams Stand On The Referee And On Every Machine Before The First Tick, The Mode Is On All Of Them, The Machines Fight, And Every State Hash Agrees With The Referee's To The Last Tick") {
        using net::FillLevel;
        const auto allies_text = [](const std::array<uint8_t, 4>& a) {
            std::string out;
            for (const uint8_t v : a) out += std::to_string(static_cast<unsigned>(v));
            return out;
        };
        const auto engine_allies = [](const sim::SimulationEngine& e) {
            std::string out;
            for (uint8_t seat = 0; seat < 4; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
            return out;
        };
        const auto no_food = [](const sim::SimulationEngine& e) {
            for (const auto& cell : e.get_world_state().cells) {
                if (cell.is_food || cell.is_powerup) return false;
            }
            return true;
        };
        // a machine sends its three first ants at the first ant of `foe` that it sees (a fight: kills are the score in 187)
        const auto attack = [](Client& from, uint8_t foe) {
            const sim::WorldState& ws = from.sim.get_world_state();
            const uint8_t seat = from.lobby->my_seat();
            sim::Command c;
            c.type = sim::CommandType::GroupAttack;
            c.issuer = seat;
            bool target = false;
            for (const sim::AntSnapshot& a : ws.ants) {
                if (a.player_id == seat && c.ants.size() < 3) c.ants.push_back(a.id);
                if (a.player_id == foe && !target) {
                    c.tile_x = static_cast<int16_t>(a.tile_x);
                    c.tile_y = static_cast<int16_t>(a.tile_y);
                    target = true;
                }
            }
            if (c.ants.empty() || !target) return;
            (void)from.session->submit(c);
        };
        {   // a room that was made with the mode, three people and a bot: the leader asks for Green + Black, so Red + Blue are the other team (seats 1 and 2 of two people); the alliances stand at tick 0 everywhere, the two teams fight, the match is played to its end
            World w;
            RoomSpec spec = spec_of("T187-1", 4);
            spec.mode = 1;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "T187-1");
            Client& bob = w.connect("Bob", "T187-1");
            Client& cat = w.connect("Cat", "T187-1");
            for (Client* c : {&ann, &bob, &cat}) c->record_hashes = true;
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::None, FillLevel::Medium}, sim::StartTeams{true, 0, 3}));
            w.run(1500);
            RoomStatus s = w.status("T187-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.ticks == 0 && s.turns == 0 && s.joined == 4 && s.bots.size() == 1);
            ASSERT_TRUE(static_cast<int>(s.mode) == 1 && s.teams == "0+3");
            ASSERT_EQ(allies_text(s.allies), std::string("3210"));                          // the referee: Green and Black are a team, Red and Blue are the other
            for (Client* c : {&ann, &bob, &cat}) {
                ASSERT_EQ(c->lobby->start_info().mode, 1);
                ASSERT_TRUE(c->lobby->start_info().teams() == sim::StartTeams({true, 0, 3}));
                ASSERT_TRUE(c->sim.game_mode() == sim::GameMode::Kills187 && no_food(c->sim));
                ASSERT_EQ(engine_allies(c->sim), std::string("3210"));
                ASSERT_EQ(c->sim.current_tick(), uint64_t{0});
                ASSERT_TRUE(said(c->room_chat).empty());                                   // nothing was refused: the teams are made in 187 as in the original's game
            }
            for (int guard = 0; guard < 4000 && w.status("T187-1").state == RoomState::Running; ++guard) {
                w.run(250);
                if (guard % 8 == 0) {
                    attack(ann, 1);                                                        // Green (with the bot of Black) against Red and Blue
                    attack(bob, 0);
                    attack(cat, 0);
                }
            }
            s = w.status("T187-1");
            ASSERT_TRUE(s.state == RoomState::Finished);
            for (Client* c : {&ann, &bob, &cat}) ASSERT_FALSE(c->session->desynced());
            w.run(Room::kGraceMs + 500);
            ASSERT_TRUE(s.referee_hash != 0 && s.ticks > 1000);
            for (Client* c : {&ann, &bob, &cat}) ASSERT_TRUE(c->hash_at.count(s.ticks) == 1 && c->hash_at[s.ticks] == s.referee_hash);
            for (const auto& at : ann.hash_at) {                                           // every tick of the match, not only the last
                ASSERT_TRUE(bob.hash_at.count(at.first) == 1 && bob.hash_at[at.first] == at.second && cat.hash_at[at.first] == at.second);
            }
            ASSERT_FALSE(s.rows.empty());
            ASSERT_EQ(s.rows.size(), size_t{2});                                           // two teams, two rows (an alliance is one row)
            uint32_t kills = 0;
            for (uint8_t seat = 0; seat < 4; ++seat) kills += ann.sim.get_world_state().player_stats[seat].enemy_killed;
            ASSERT_TRUE(kills > 0);                                                        // the machines really fought: the points of this match are kills
        }
        {   // a room whose own teams are 1 + 2 (the room's specification, not a leader's request; Green and Black are the other team): the same, with every seat a person
            World w;
            RoomSpec spec = spec_of("T187-2", 4);
            spec.mode = 1;
            spec.teams = sim::StartTeams{true, 1, 2};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& ann = w.connect("Ann", "T187-2");
            Client& bob = w.connect("Bob", "T187-2");
            Client& cat = w.connect("Cat", "T187-2");
            Client& dee = w.connect("Dee", "T187-2");
            for (Client* c : {&ann, &bob, &cat, &dee}) c->record_hashes = true;
            w.run(2500);
            RoomStatus s = w.status("T187-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 4 && static_cast<int>(s.mode) == 1 && s.teams == "1+2" && s.room_teams == "1+2");
            for (Client* c : {&ann, &bob, &cat, &dee}) {
                ASSERT_TRUE(c->sim.game_mode() == sim::GameMode::Kills187 && no_food(c->sim));
                ASSERT_EQ(engine_allies(c->sim), std::string("3210"));
                ASSERT_TRUE(said(c->room_chat).empty());
            }
            ASSERT_EQ(allies_text(s.allies), std::string("3210"));
            for (int guard = 0; guard < 4000 && w.status("T187-2").state == RoomState::Running; ++guard) {
                w.run(250);
                if (guard % 8 == 0) {
                    attack(ann, 1);                                                        // Green and Black against Red and Blue
                    attack(dee, 2);
                }
            }
            s = w.status("T187-2");
            ASSERT_TRUE(s.state == RoomState::Finished);
            w.run(Room::kGraceMs + 500);
            for (Client* c : {&ann, &bob, &cat, &dee}) {
                ASSERT_FALSE(c->session->desynced());
                ASSERT_TRUE(c->hash_at.count(s.ticks) == 1 && c->hash_at[s.ticks] == s.referee_hash);
            }
        }
        {   // the front page's lobby room: the leader's plan carries the mode and the teams together (Green + Blue, so Red + Black are the other team; two bots), the START puts both in the Start, both machines and the referee stand the same
            World w(lobby_limits());
            Client& pia = w.connect_page("Pia", "t187l001", lobby_block_of());
            Client& bob = w.connect_page("Bob", "t187l001", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            const net::SeatKey bob_key = bob.lobby->key();
            w.run(300);
            net::PlanMsg plan = plan_msg({K::Open, K::Open, K::Medium, K::Easy}, 0, 2);
            plan.mode = static_cast<uint8_t>(sim::GameMode::Kills187);
            ASSERT_TRUE(pia.lobby->request_plan(plan));
            w.run(300);
            ASSERT_TRUE(static_cast<int>(w.status("t187l001").mode) == 1);
            ASSERT_TRUE(pia.lobby->room().mode == 1 && bob.lobby->room().mode == 1 && pia.lobby->room().teams() == sim::StartTeams({true, 0, 2}));
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            Client& pia_game = w.connect_game("Pia", "t187l001", pia_key);
            Client& bob_game = w.connect_game("Bob", "t187l001", bob_key);
            pia_game.record_hashes = true;
            bob_game.record_hashes = true;
            w.run(3500);
            RoomStatus s = w.status("t187l001");
            ASSERT_TRUE(s.state == RoomState::Running && static_cast<int>(s.mode) == 1 && s.teams == "0+2" && s.joined == 4 && s.bots.size() == 2);
            ASSERT_EQ(allies_text(s.allies), std::string("2301"));
            for (Client* c : {&pia_game, &bob_game}) {
                ASSERT_EQ(c->lobby->start_info().mode, 1);
                ASSERT_TRUE(c->lobby->start_info().teams() == sim::StartTeams({true, 0, 2}));
                ASSERT_TRUE(c->sim.game_mode() == sim::GameMode::Kills187 && no_food(c->sim));
                ASSERT_EQ(engine_allies(c->sim), std::string("2301"));
            }
            for (int guard = 0; guard < 4000 && w.status("t187l001").state == RoomState::Running; ++guard) {
                w.run(250);
                if (guard % 8 == 0) attack(bob_game, 0);                                   // Red's person sends ants at Green
            }
            s = w.ended("t187l001");                                                       // (the match is over: its code is the rematch's lobby now, the match's room is listed beside it)
            ASSERT_TRUE(s.state == RoomState::Finished);
            w.run(Room::kGraceMs + 500);
            for (Client* c : {&pia_game, &bob_game}) {
                ASSERT_FALSE(c->session->desynced());
                ASSERT_TRUE(c->hash_at.count(s.ticks) == 1 && c->hash_at[s.ticks] == s.referee_hash);
            }
        }
    } TEST_END();

    TEST_CASE("S3.189 A Match Of Game Mode 187 With Teams Survives Two Restarts Of The Server (Protocols 13 And 17): The Record's Start Has The Mode And The Pair, The Restored Referee Makes The Mode And The Teams As Every Machine Did (Its State Is That Of An Independent Replay Of The Record), A Machine That Comes Back And A Page That Starts From Nothing Stand At The Referee's Final State With The Teams Kept") {
        using net::FillLevel;
        const auto allies_text = [](const std::array<uint8_t, 4>& a) {
            std::string out;
            for (const uint8_t v : a) out += std::to_string(static_cast<unsigned>(v));
            return out;
        };
        const auto engine_allies = [](const sim::SimulationEngine& e) {
            std::string out;
            for (uint8_t seat = 0; seat < 4; ++seat) out += std::to_string(static_cast<unsigned>(e.alliance_of(seat)));
            return out;
        };
        PWorld w("persist-189");
        w.start_server(500);
        RoomSpec spec = held_spec("T-187", 4);
        spec.mode = 1;
        ASSERT_TRUE(w.mgr->create_room(spec, w.server_now()).ok);
        RClient& leader = w.connect("Lea", "T-187");
        w.run(1500);
        net::StartRequestMsg request;                                                          // Medium at seat 1, Hard at seat 2, Easy at seat 3; Red + Blue (1 + 2) are a team, so are Green + Black
        request.fill = {FillLevel::None, FillLevel::Medium, FillLevel::Hard, FillLevel::Easy};
        request.set_teams(sim::StartTeams{true, 1, 2});
        ASSERT_TRUE(leader.end->send(net::encode(request)));
        ASSERT_TRUE(w.until([&]() { return w.status("T-187").state == RoomState::Running; }, 20000));
        w.run(9000 + kPre);
        RoomStatus s = w.status("T-187");
        ASSERT_TRUE(s.teams == "1+2" && allies_text(s.allies) == "3210" && s.bots.size() == 3 && s.record_kept && static_cast<int>(s.mode) == 1);
        ASSERT_EQ(engine_allies(leader.sim), std::string("3210"));
        ASSERT_TRUE(leader.sim.game_mode() == sim::GameMode::Kills187);
        const RestartLoaded before = w.read_record("T-187");
        ASSERT_TRUE(before.ok() && before.head.start.mode == 1 && before.head.start.team_a == 1 && before.head.start.team_b == 2 && before.head.fill_mask == 0x0E);
        ASSERT_TRUE(before.checks.size() >= 1);
        const net::SeatKey key = leader.lobby->key();
        const uint8_t seat = leader.lobby->my_seat();
        w.stop_server(true);
        w.start_server(500);
        ASSERT_TRUE(w.report.count(RestoreItem::Outcome::Restored) == 1);                      // (a referee that had not made the mode or the teams would have been refused at turn 19: "the replay does not agree")
        const RestartLoaded rec = w.read_record("T-187");
        s = w.status("T-187");
        ASSERT_TRUE(s.restored && s.teams == "1+2" && allies_text(s.allies) == "3210" && s.bots.size() == 3 && static_cast<int>(s.mode) == 1);
        ASSERT_EQ(s.restored_hash, replay_hash(rec, rec.turns.size()));                        // the state that the room stands at is that of an independent replay of the record
        ASSERT_TRUE(w.until([&]() { return !w.status("T-187").paused; }, 60000));
        w.run(5000);
        s = w.status("T-187");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && allies_text(s.allies) == "3210" && static_cast<int>(s.mode) == 1);
        ASSERT_TRUE(leader.session->mode() == net::ClientSession::Mode::Normal && !leader.session->desynced() && !leader.lost);
        ASSERT_EQ(engine_allies(leader.sim), std::string("3210"));
        // a second restart; this time the leader's machine is a page that was reloaded: it starts from nothing, is sent the Start of the match with the mode and the teams and catches up to the referee
        w.stop_server(true);
        leader.reconnects = false;
        w.start_server(500);
        RClient& reloaded = w.connect("Lea", "T-187", seat, key);
        ASSERT_TRUE(w.until([&]() { return reloaded.lobby != nullptr && reloaded.lobby->rejoined() && reloaded.session != nullptr; }, 30000));
        ASSERT_TRUE(reloaded.lobby->start_info().team_a == 1 && reloaded.lobby->start_info().team_b == 2 && reloaded.lobby->start_info().mode == 1);
        ASSERT_TRUE(w.until([&]() { return !w.status("T-187").paused && reloaded.session->mode() == net::ClientSession::Mode::Normal; }, 60000));
        w.run(3000);
        s = w.status("T-187");
        ASSERT_TRUE(s.state == RoomState::Running && s.teams == "1+2" && allies_text(s.allies) == "3210" && static_cast<int>(s.mode) == 1);
        ASSERT_EQ(engine_allies(reloaded.sim), std::string("3210"));
        ASSERT_TRUE(reloaded.sim.game_mode() == sim::GameMode::Kills187);
        ASSERT_TRUE(!reloaded.session->desynced() && !reloaded.lost);
        w.play_to_the_end("T-187");                                                            // the match is played to its end: the machine from nothing stands at the referee's final state
        s = w.status("T-187");
        ASSERT_TRUE(s.state == RoomState::Finished && s.referee_hash != 0);
        ASSERT_TRUE(reloaded.sim.state_hash().total == s.referee_hash && !reloaded.session->desynced() && !reloaded.lost);
    } TEST_END();

    TEST_CASE("S3.136 A START The Server Cannot Carry Out Ends With A Notice For The Leader And The Room Goes On Waiting, Fails Nothing: The Plan's Map Is Gone, The Colors Cannot Play The Map, There Is No Place For Another Match (And Is When An Abandoned Match Gives Its Place Up); The Same Leader Starts Again When The Plan Is Fixed") {
        const std::string dir = temp_dir_for("lobby_refusals");
        fs::copy_file(maps_dir() + "/TINY.LVL", dir + "/TINY.LVL");
        {   // BAD.LVL: TINY with the green start marker moved outside the grid (S3.64): playable for red and blue, not for a roster with green
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
        ServerLimits l = lobby_limits();
        l.demo_maps = {"TINY.LVL", "BAD.LVL", "GONE.LVL"};                                // (GONE.LVL is listed and not in the folder)
        {   // the map of the plan is gone; then the colors cannot play the map; then a plan that can be played starts
            World w(l, dir);
            Client& pia = w.connect_page("Pia", "refu0001", lobby_block_of());
            const net::SeatKey key = pia.lobby->key();
            ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Nobody}, net::kNoTeam, net::kNoTeam, "GONE.LVL")));
            w.run(300);
            ASSERT_EQ(w.status("refu0001").map, std::string("GONE.LVL"));
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(200);
            Client& game = w.connect_game("Pia", "refu0001", key);
            w.run(500);
            RoomStatus s = w.status("refu0001");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.starting && s.bots.empty());
            ASSERT_EQ(said(game.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeMapLost}));
            ASSERT_TRUE(game.lobby->is_leader() && game.lobby->room().plan[1] == K::Medium);   // (the plan stands: the leader changes what is wrong with it)
            ASSERT_TRUE(game.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Nobody}, net::kNoTeam, net::kNoTeam, "BAD.LVL")));
            w.run(300);
            ASSERT_TRUE(game.lobby->request_start());
            w.run(500);
            s = w.status("refu0001");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.starting && s.bots.empty() && s.map == "BAD.LVL");
            ASSERT_EQ(said(game.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeMapLost, std::string("255||") + net::kNoticeMapColors}));
            ASSERT_TRUE(game.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Nobody}, net::kNoTeam, net::kNoTeam, "TINY.LVL")));
            w.run(300);
            ASSERT_TRUE(game.lobby->request_start());
            w.run(3000);
            s = w.status("refu0001");
            ASSERT_TRUE(s.state == RoomState::Running && s.map == "TINY.LVL" && s.joined == 2 && s.bots.size() == 1);
        }
        {   // no place: the one public place is taken by a match with people at it; an abandoned one gives its place up
            ServerLimits one = l;
            one.demo_rooms = 1;
            World w(one, dir);
            Client& ann = w.connect_creating("Ann", "occu0001", block_of("TINY.LVL", 2));
            Client& cat = w.connect("Cat", "occu0001");
            w.run(3000);
            ASSERT_TRUE(w.status("occu0001").state == RoomState::Running);
            Client& pia = w.connect_page("Pia", "refu0002", lobby_block_of());
            const net::SeatKey key = pia.lobby->key();
            ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Nobody})));
            w.run(300);
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(200);
            Client& game = w.connect_game("Pia", "refu0002", key);
            w.run(500);
            RoomStatus s = w.status("refu0002");
            ASSERT_TRUE(s.state == RoomState::Waiting && !s.starting);
            ASSERT_EQ(said(game.room_chat), (std::vector<std::string>{std::string("255||") + net::kNoticeNoPlace}));
            ASSERT_TRUE(w.status("occu0001").state == RoomState::Running);                   // (the match with people at it is not touched)
            ann.end->close();                                                                // Ann and Cat close their tabs; a minute later nobody has come back to the match
            cat.end->close();
            w.run(30000);
            ASSERT_TRUE(game.lobby->request_start());                                        // too early: the match has been abandoned for half a minute only
            w.run(500);
            ASSERT_TRUE(w.status("refu0002").state == RoomState::Waiting);
            ASSERT_EQ(said(game.room_chat).size(), size_t{2});
            w.run(32000);
            (void)w.mgr.take_ended(w.now);
            ASSERT_TRUE(game.lobby->request_start());
            w.run(3000);
            s = w.status("refu0002");
            ASSERT_TRUE(s.state == RoomState::Running && s.joined == 2);
            ASSERT_TRUE(w.status("occu0001").code.empty());                                  // the abandoned match is gone: its place was needed
            ASSERT_EQ(w.mgr.room_count(), size_t{1});
        }
        std::error_code ignore;
        fs::remove_all(dir, ignore);
    } TEST_END();

    TEST_CASE("S3.137 The Lobby Closes When Nobody Is In It: A Minute After The Last Person Has Gone (Five Seconds Here), Silently (No Line For The Log, No Result File); A Person Who Comes Meanwhile Starts The Wait Over; A Seat Whose Link Ended Is Held And Counts As A Person; A Link That Says Nothing Is Closed And Its Seat Is Held; The Code Can Be Made Again") {
        ServerLimits l = lobby_limits();
        l.lobby_empty_close_ms = 5000;
        l.lobby_hold_ms = 3000;
        l.lobby_silence_ms = 4000;
        {   // the last person says Leave
            World w(l);
            Client& pia = w.connect_page("Pia", "emp00001", lobby_block_of());
            w.run(500);
            pia.lobby->leave();
            w.run(4500);
            ASSERT_EQ(w.mgr.room_count(), size_t{1});                                     // (the wait is five seconds)
            ASSERT_EQ(waiting_lobbies(w), size_t{1});
            w.run(1500);
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
            ASSERT_TRUE(w.mgr.take_ended(w.now).empty());                                 // no word: nothing ended, nobody played
            Client& again = w.connect_page("Pia", "emp00001", lobby_block_of());          // the code is free: a new room, made by this Hello
            w.run(300);
            ASSERT_TRUE(again.lobby->created() && again.lobby->phase() == net::ClientLobby::Phase::InRoom);
        }
        {   // somebody comes while the room waits: the wait starts over when it is empty again
            World w(l);
            Client& pia = w.connect_page("Pia", "emp00002", lobby_block_of());
            w.run(500);
            pia.lobby->leave();
            w.run(3000);
            Client& bob = w.connect_page("Bob", "emp00002", lobby_block_of());
            w.run(3000);                                                                  // (six seconds since Pia left: the room would be gone)
            ASSERT_EQ(w.mgr.room_count(), size_t{1});
            ASSERT_FALSE(bob.lobby->created());
            bob.lobby->leave();
            w.run(4500);
            ASSERT_EQ(w.mgr.room_count(), size_t{1});
            w.run(1500);
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        {   // a link that ended: the seat is held for 3 s and the room is not empty meanwhile; then the wait of 5 s begins
            World w(l);
            Client& pia = w.connect_page("Pia", "emp00003", lobby_block_of());
            w.run(500);
            pia.end->close();
            w.run(2500);
            ASSERT_TRUE(w.status("emp00003").joined == 1 && w.status("emp00003").leader == 0);          // (held: a person still, and the leader)
            w.run(1000);
            ASSERT_TRUE(w.status("emp00003").joined == 0);
            w.run(4000);
            ASSERT_EQ(w.mgr.room_count(), size_t{1});
            w.run(1500);
            ASSERT_EQ(w.mgr.room_count(), size_t{0});
        }
        {   // a machine that went to sleep: its link is open and says nothing; the room closes it after 4 s of silence (counted from its last word, up to a second before it fell asleep), holds the seat, and the key takes it back
            ServerLimits slow = l;
            slow.lobby_hold_ms = 10000;
            World w(slow);
            Client& pia = w.connect_page("Pia", "emp00004", lobby_block_of());
            Client& bob = w.connect_page("Bob", "emp00004", lobby_block_of());
            const net::SeatKey bob_key = bob.lobby->key();
            w.run(500);
            bob.asleep = true;
            w.run(2500);
            ASSERT_TRUE(bob.server_end->is_open());
            w.run(1600);
            ASSERT_FALSE(bob.server_end->is_open());                                      // the room closed the link
            ASSERT_TRUE(w.status("emp00004").joined == 2 && w.status("emp00004").names[1] == "Bob");      // ... and holds the seat
            Client& bob2 = w.connect_page("Bob", "emp00004", lobby_block_of());           // (a new Hello from the same machine, without its key, is a newcomer: the color is held, so it takes the next)
            w.run(300);
            ASSERT_EQ(bob2.lobby->my_seat(), 2);
            Client& bob3 = w.connect_game("Bob", "emp00004", bob_key);                    // the key takes the seat back
            w.run(300);
            ASSERT_EQ(bob3.lobby->my_seat(), 1);
            ASSERT_TRUE(pia.lobby->is_leader());
        }
    } TEST_END();

    TEST_CASE("S3.138 The Pool Of Lobbies: At Most --demo-lobbies Wait At A Time; A Lobby With A Person In It Never Gives Its Place Up, So A Page Is Told NoSuchRoom When They All Have One; An Empty Lobby Does, The One That Has Been Empty The Longest First, And Is Forgotten Without A Word") {
        ServerLimits l = lobby_limits();
        l.demo_lobbies = 2;
        l.lobby_empty_close_ms = 600000;                                                  // (the minute is not what this test is about)
        World w(l);
        Client& a = w.connect_page("A", "pool0001", lobby_block_of());
        Client& b = w.connect_page("B", "pool0002", lobby_block_of());
        Client& c = w.connect_page("C", "pool0003", lobby_block_of());
        ASSERT_EQ(answer_of(w, c), net::RejectReason::NoSuchRoom);
        ASSERT_EQ(waiting_lobbies(w), size_t{2});
        a.lobby->leave();
        w.run(2000);                                                                      // pool0001 has been empty for two seconds
        Client& d = w.connect_page("D", "pool0003", lobby_block_of());                    // its place goes to the newcomer
        w.run(300);
        ASSERT_TRUE(d.lobby->created() && d.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(w.status("pool0001").code.empty() && w.status("pool0002").state == RoomState::Waiting && w.status("pool0003").state == RoomState::Waiting);
        ASSERT_EQ(waiting_lobbies(w), size_t{2});
        ASSERT_TRUE(w.mgr.take_ended(w.now).empty());                                     // (no word of it: it never ended)
        b.lobby->leave();
        w.run(3000);                                                                      // pool0002 has been empty for three seconds
        d.lobby->leave();
        w.run(1000);                                                                      // pool0003 for one
        Client& e = w.connect_page("E", "pool0004", lobby_block_of());                    // the one that has been empty the longest goes: pool0002
        w.run(300);
        ASSERT_TRUE(e.lobby->created());
        ASSERT_TRUE(w.status("pool0002").code.empty() && w.status("pool0003").state == RoomState::Waiting && w.status("pool0004").state == RoomState::Waiting);
        // a lobby whose seat is held counts as a person: it is not given up
        World h(l);
        Client& p = h.connect_page("P", "pool0011", lobby_block_of());
        h.connect_page("Q", "pool0012", lobby_block_of());
        p.end->close();
        h.run(2000);                                                                      // (held for a minute by default)
        Client& r = h.connect_page("R", "pool0013", lobby_block_of());
        ASSERT_EQ(answer_of(h, r), net::RejectReason::NoSuchRoom);
        ASSERT_EQ(waiting_lobbies(h), size_t{2});
    } TEST_END();

    TEST_CASE("S3.139 A Page Never Takes A Seat Of A Match: Its Hello With A Key Is MatchRunning, The Game Of The Seat Is Not Disturbed; The Game With The Key Comes Back As Ever; When The Match Is Over A Page With The Lobby Block Makes A New Lobby Of The Same Code (The Old Room's End Is Reported)") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "page0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "page0001", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(200);
        w.connect_game("Pia", "page0001", pia_key);
        Client& bob_game = w.connect_game("Bob", "page0001", bob_key);
        w.run(3500);
        ASSERT_TRUE(w.status("page0001").state == RoomState::Running);
        // a page (a second tab of Bob's, with his key in its storage) is told that a match runs; the game keeps the seat
        w.next_key = bob_key;
        Client& tab = w.connect_page("Bob", "page0001", lobby_block_of());
        w.next_key = net::SeatKey{};
        ASSERT_EQ(answer_of(w, tab), net::RejectReason::MatchRunning);
        ASSERT_TRUE(bob_game.lobby->phase() != net::ClientLobby::Phase::Rejected && bob_game.server_end->is_open());
        ASSERT_TRUE(w.status("page0001").state == RoomState::Running && w.status("page0001").absent.empty());
        // the match is over: a page with the lobby block makes the room again (the link of the lobby carries the block), a Hello without a key and a block
        ASSERT_TRUE(w.mgr.close_room("page0001", w.now));
        w.run(200);
        ASSERT_TRUE(w.status("page0001").state == RoomState::Failed);
        Client& next = w.connect_page("Pia", "page0001", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(next.lobby->created() && next.lobby->phase() == net::ClientLobby::Phase::InRoom);
        const RoomStatus s = w.status("page0001");
        ASSERT_TRUE(s.lobby && s.state == RoomState::Waiting && s.joined == 1);
        bool reported = false;
        for (const RoomStatus& st : w.mgr.take_ended(w.now)) reported = reported || (st.code == "page0001" && st.state == RoomState::Failed);
        ASSERT_TRUE(reported);
    } TEST_END();

    TEST_CASE("S3.152 A Lobby Room From The Control Interface's Door (create_room): Only With What A Lobby Needs, And Its Numbers In Range; It Is Served Like Any Other: A Page Is Seated In It And Its Plan Chooses The Server's Maps") {
        World w(lobby_limits());
        const auto lobby_spec = [](const std::string& code) {
            RoomSpec s = spec_of(code, 4);
            s.lobby = true;
            s.early_start = true;
            s.leader_starts = true;
            s.reconnect = true;
            return s;
        };
        const auto refused = [&](RoomSpec s) {
            const CreateResult r = w.mgr.create_room(std::move(s), w.now);
            return !r.ok && r.http_status == 400;
        };
        { RoomSpec s = lobby_spec("ctl-l001"); s.players = 3; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l002"); s.early_start = false; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l003"); s.leader_starts = false; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l004"); s.reconnect = false; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l005"); s.fog = true; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l006"); s.teams = sim::StartTeams{true, 0, 1}; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l007"); s.bots.push_back(ai::BotSpec{1, "standard", ai::Level::Easy}); ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l008"); s.hold_ms = 10; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l009"); s.start_wait_ms = 0; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l010"); s.silence_ms = 4000000; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l011"); s.empty_close_ms = 999; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l0m1"); s.mode = 1; ASSERT_TRUE(refused(s)); }                                        // (protocol 17: the leader's plan decides the game mode)
        { RoomSpec s = lobby_spec("ctl-l0m2"); s.mode = 2; ASSERT_TRUE(refused(s)); }
        { RoomSpec s = lobby_spec("ctl-l013"); s.silence_ms = 999; ASSERT_TRUE(refused(s)); }
        ASSERT_EQ(w.mgr.room_count(), size_t{0});
        ASSERT_TRUE(w.mgr.create_room(lobby_spec("ctl-l012"), w.now).ok);
        Client& pia = w.connect_page("Pia", "ctl-l012", lobby_block_of());
        w.run(300);
        ASSERT_EQ(pia.lobby->phase(), net::ClientLobby::Phase::InRoom);
        ASSERT_FALSE(pia.lobby->created());                                               // (the Hello did not make this room)
        ASSERT_TRUE(pia.lobby->request_plan(plan_msg(all_open, net::kNoTeam, net::kNoTeam, "gauntlet.lvl")));
        w.run(300);
        ASSERT_EQ(w.status("ctl-l012").map, std::string("GAUNTLET.LVL"));                 // (the server's maps: its services are installed)
        ASSERT_TRUE(w.status("ctl-l012").lobby && !w.status("ctl-l012").public_room);
        {   // the control interface shows a lobby room as one, with its plan; the status of any other room has no such key
            ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Medium, K::Nobody, K::Easy}, 0, 1)));
            w.run(300);
            const std::string json = ctl::to_json(status_to_json(w.status("ctl-l012")));
            ASSERT_TRUE(json.find("\"lobby\":true") != std::string::npos && json.find("\"starting\":false") != std::string::npos);
            ASSERT_TRUE(json.find("\"plan\":\"omne 0+1\"") != std::string::npos);
            ASSERT_TRUE(w.mgr.create_room(spec_of("ctl-plain", 2), w.now).ok);
            const std::string plain = ctl::to_json(status_to_json(w.status("ctl-plain")));
            ASSERT_TRUE(plain.find("\"lobby\"") == std::string::npos && plain.find("\"starting\"") == std::string::npos && plain.find("\"plan\"") == std::string::npos);
        }
        {   // visitors' lobbies are off (--demo-lobbies 0): a page's block makes nothing, and the lobby that the operator made keeps its place (no room is pushed out for a visitor's)
            ServerLimits off = lobby_limits();
            off.demo_lobbies = 0;
            World wo(off);
            ASSERT_TRUE(wo.mgr.create_room(lobby_spec("ctl-l020"), wo.now).ok);
            Client& pam = wo.connect_page("Pam", "pool0001", lobby_block_of());
            ASSERT_EQ(answer_of(wo, pam), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(wo.mgr.room_count(), size_t{1});
            ASSERT_TRUE(wo.status("ctl-l020").lobby);
        }
    } TEST_END();

    TEST_CASE("S3.153 A START That Is Cancelled (A Game That Cannot Load The Map): The Request Is Done, The Lobby Waits Again With Everybody In It, And It Does Not Fail For Any Number Of Them As A Room Of Another Kind Does After Five; The First Three Cancels In A Row Cost The Pause Of Two Seconds (A START Asked For In It Waits It Out), The Fourth And Every One After It A Pause Of A Minute, And A START Asked For In That Gets A Notice At Once And Does Not Stand; The Count Is Forgotten After Fifteen Minutes Without A Cancel; The Leader's Next START Works When The Game Can Load") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "retr0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "retr0001", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        ASSERT_TRUE(pia.lobby->request_start());
        Client& pia_game = w.connect_game("Pia", "retr0001", pia_key);
        Client& bob_game = w.connect_game("Bob", "retr0001", bob_key, true);              // Bob's game cannot load the map: every start is cancelled
        w.run(500);                                                                       // (the first start was cancelled at once; the pause of two seconds runs)
        const auto waits_open = [&]() {                                                   // the room waits again with everybody in it, and nothing was asked of it
            const RoomStatus s = w.status("retr0001");
            return s.state == RoomState::Waiting && !s.starting && s.joined == 2 && s.games == 3 && s.reason.empty();
        };
        ASSERT_MSG(waits_open(), "after the first cancel");
        ASSERT_TRUE(pia_game.lobby->request_start());                                     // asked for in the pause: it stands until the pause is over, and everybody is shown that the room waits
        w.run(500);
        ASSERT_TRUE(w.status("retr0001").starting && pia_game.lobby->room().starting());
        w.run(2500);                                                                      // the pause is over, the start is tried and cancelled again (the second): the request is done
        ASSERT_MSG(waits_open(), "after the second cancel");
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(2500);                                                                      // the third (the last that costs two seconds)
        ASSERT_MSG(waits_open(), "after the third cancel");
        ASSERT_EQ(said(pia_game.room_chat), stale_lines("Bob", 3));                       // (each cancel names the game that could not load the map, and how to start without it: the players also see the Cancel message)
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(2500);                                                                      // the fourth: the pause is a minute now
        ASSERT_MSG(waits_open(), "after the fourth cancel");
        ASSERT_EQ(said(pia_game.room_chat), stale_lines("Bob", 4));
        ASSERT_TRUE(pia_game.lobby->request_start());                                     // asked for in the long pause: the leader is told at once and the request does not stand
        w.run(300);
        ASSERT_MSG(waits_open(), "a START in the long pause");
        ASSERT_TRUE(!pia_game.lobby->room().starting());
        ASSERT_EQ(said(pia_game.room_chat), stale_lines("Bob", 5));                       // (whose game it is, not only that the match could not start a few times in a row: nobody is blocked without a word)
        w.run(30000);
        ASSERT_TRUE(pia_game.lobby->request_start());                                     // 30 s into the pause: still told, still not standing
        w.run(300);
        ASSERT_MSG(waits_open(), "a START 30 s into the long pause");
        ASSERT_EQ(said(pia_game.room_chat), stale_lines("Bob", 6));
        w.run(31000);                                                                     // the minute is over
        bob_game.fail_load = false;                                                       // the game can load now
        ASSERT_TRUE(pia_game.lobby->request_start());
        w.run(4000);
        ASSERT_TRUE(w.status("retr0001").state == RoomState::Running);
        for (const bool forgets : {false, true}) {   // the count of cancels in a row is forgotten when fifteen minutes pass without one (not before: after fourteen it counts still)
            World f(lobby_limits());
            Client& ann = f.connect_page("Ann", "retr0002", lobby_block_of());
            Client& cat = f.connect_page("Cat", "retr0002", lobby_block_of());
            const net::SeatKey ann_key = ann.lobby->key();
            const net::SeatKey cat_key = cat.lobby->key();
            ASSERT_TRUE(ann.lobby->request_start());
            Client& ann_game = f.connect_game("Ann", "retr0002", ann_key);
            f.connect_game("Cat", "retr0002", cat_key, true);
            f.run(500);
            for (int round = 0; round < 3; ++round) {                                     // (the first cancel is behind us; three more are the fourth ... )
                ASSERT_TRUE(ann_game.lobby->request_start());
                f.run(2500);
            }
            ASSERT_TRUE(ann_game.lobby->request_start());
            f.run(300);
            ASSERT_EQ(said(ann_game.room_chat), stale_lines("Cat", 5));                    // (the minute is on: it was answered, with the name)
            f.run((forgets ? 15u : 14u) * 60u * 1000u);                                   // fifteen minutes without a cancel (or fourteen)
            ASSERT_TRUE(ann_game.lobby->request_start());
            f.run(400);                                                                   // the start is tried and cancelled: the first of a new count when forgotten (the pause is two seconds), else one more of the count
            ASSERT_TRUE(ann_game.lobby->request_start());
            f.run(300);
            if (forgets) {
                ASSERT_TRUE(f.status("retr0002").starting && ann_game.lobby->room().starting());      // it stands: the pause is short
                ASSERT_EQ(said(ann_game.room_chat), stale_lines("Cat", 6));                // (the cancel of the new count said whose game it was: nothing else new)
            } else {
                ASSERT_TRUE(!f.status("retr0002").starting && !ann_game.lobby->room().starting());     // the pause is a minute: told at once, and it does not stand
                ASSERT_EQ(said(ann_game.room_chat), stale_lines("Cat", 7));
            }
        }
    } TEST_END();

    TEST_CASE("S3.154 A Rename Through A Real Lobby Room (Protocol 16): A Person's New Name Is In The Status And In Every Page's Room Message, A Name That Looks Like A Bot's Is Not Taken, And The Name Goes With The Seat When The Pages' Games Take Their Seats With The Keys: It Is The Name Of The Match") {
        World w(lobby_limits());
        Client& pia = w.connect_page("Pia", "name0001", lobby_block_of());
        Client& bob = w.connect_page("Bob", "name0001", lobby_block_of());
        const net::SeatKey pia_key = pia.lobby->key();
        const net::SeatKey bob_key = bob.lobby->key();
        w.run(300);
        ASSERT_TRUE(bob.lobby->request_name("Robert"));
        w.run(300);
        RoomStatus s = w.status("name0001");
        ASSERT_TRUE(s.names[0] == "Pia" && s.names[1] == "Robert" && s.names[2].empty());
        for (const Client* c : {&pia, &bob}) ASSERT_TRUE(c->lobby->room().slots[0].name == "Pia" && c->lobby->room().slots[1].name == "Robert");
        ASSERT_TRUE(pia.lobby->request_name("Pia Lee"));                                  // the leader's too
        w.run(300);
        ASSERT_EQ(w.status("name0001").names[0], std::string("Pia Lee"));
        ASSERT_FALSE(bob.lobby->request_name("Bot (Hard)"));                              // a person is never shown as a bot: Robert stays Robert
        w.run(300);
        ASSERT_EQ(w.status("name0001").names[1], std::string("Robert"));
        // START: the pages go to the game page and their games take the seats with the keys, saying the name that a game knows by default; the seats keep the names that the room shows
        ASSERT_TRUE(pia.lobby->request_start());
        w.run(300);
        Client& pia_game = w.connect_game("Player", "name0001", pia_key);
        Client& bob_game = w.connect_game("Player", "name0001", bob_key);
        w.run(3000);
        s = w.status("name0001");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_TRUE(s.names[0] == "Pia Lee" && s.names[1] == "Robert");
        ASSERT_TRUE(pia_game.lobby->start_info().names[0] == "Pia Lee" && bob_game.lobby->start_info().names[1] == "Robert");
    } TEST_END();

    TEST_CASE("S3.162 A Removal Through A Real Lobby Room (Protocol 16): The Leader's Press Takes A Person Out: Their Page Is Told (Kicked) And Its Link Is Closed, The Status Shows The Color Free, A Newcomer Takes It, And The Removed Person's Key Fits No Seat Any More; A START That Waits For A Game That Never Opens Goes On Without That Person When The Leader Removes Them") {
        {   // before a START
            World w(lobby_limits());
            Client& pia = w.connect_page("Pia", "rem00001", lobby_block_of());
            Client& bob = w.connect_page("Bob", "rem00001", lobby_block_of());
            Client& cat = w.connect_page("Cat", "rem00001", lobby_block_of());
            const net::SeatKey bob_key = bob.lobby->key();
            w.run(300);
            ASSERT_TRUE(pia.lobby->request_remove(1));
            ASSERT_FALSE(bob.lobby->request_remove(2));                                       // (only the leader asks)
            w.run(300);
            ASSERT_EQ(bob.lobby->phase(), net::ClientLobby::Phase::Rejected);
            ASSERT_EQ(bob.lobby->reject_reason(), net::RejectReason::Kicked);
            ASSERT_FALSE(bob.server_end->is_open());
            RoomStatus s = w.status("rem00001");
            ASSERT_TRUE(s.names[0] == "Pia" && s.names[1].empty() && s.names[2] == "Cat");
            ASSERT_EQ(s.joined, size_t{2});
            for (const Client* c : {&pia, &cat}) ASSERT_TRUE(c->lobby->room().slots[1].state == net::SlotState::Empty && c->lobby->room().slots[0].name == "Pia");
            ASSERT_TRUE(pia.lobby->is_leader() && pia.lobby->phase() == net::ClientLobby::Phase::InRoom && cat.lobby->phase() == net::ClientLobby::Phase::InRoom);
            ASSERT_TRUE(pia.lobby->chat_log().empty() && cat.lobby->chat_log().empty());     // (nobody is told by the room)
            Client& dan = w.connect_page("Dan", "rem00001", lobby_block_of());               // the color is free for a newcomer at once
            w.run(300);
            ASSERT_EQ(dan.lobby->my_seat(), 1);
            w.next_key = bob_key;                                                             // the removed person opens the link again, with the key they had
            Client& bob2 = w.connect_page("Bob", "rem00001", lobby_block_of());
            w.next_key = net::SeatKey{};
            w.run(300);
            ASSERT_TRUE(bob2.lobby->phase() == net::ClientLobby::Phase::InRoom && bob2.lobby->my_seat() == 3 && !bob2.lobby->rejoined());       // a new player in the color that was left
            ASSERT_TRUE(bob2.lobby->key() != bob_key && !net::key_is_zero(bob2.lobby->key()));
            s = w.status("rem00001");
            ASSERT_TRUE(s.names[0] == "Pia" && s.names[1] == "Dan" && s.names[2] == "Cat" && s.names[3] == "Bob");
            ASSERT_EQ(s.leader, 0);
        }
        {   // the leader's START waits for Bob's game, which never opens: the leader's game removes Bob, and the match starts with the games that are there
            World w(lobby_limits());
            Client& pia = w.connect_page("Pia", "rem00002", lobby_block_of());
            Client& bob = w.connect_page("Bob", "rem00002", lobby_block_of());
            Client& cat = w.connect_page("Cat", "rem00002", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            const net::SeatKey cat_key = cat.lobby->key();
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            Client& pia_game = w.connect_game("Pia", "rem00002", pia_key);
            w.connect_game("Cat", "rem00002", cat_key);
            w.run(2000);
            RoomStatus s = w.status("rem00002");
            ASSERT_TRUE(s.state == RoomState::Waiting && s.starting);                         // (Bob's page has not gone to the game page)
            ASSERT_TRUE(pia_game.lobby->room().starting() && pia_game.lobby->is_leader());
            ASSERT_TRUE(pia_game.lobby->request_remove(1));
            w.run(4000);
            ASSERT_EQ(bob.lobby->reject_reason(), net::RejectReason::Kicked);
            s = w.status("rem00002");
            ASSERT_TRUE(s.state == RoomState::Running);                                       // the match went on without the person who was removed
            ASSERT_TRUE(s.names[0] == "Pia" && s.names[1].empty() && s.names[2] == "Cat");
            ASSERT_EQ(s.joined, size_t{2});
        }
    } TEST_END();

    TEST_CASE("S3.155 Two Hellos Reach The Door In One Pass (Links With No Jitter): A Visitor Who Joins The Lobby That Nobody Was In Is Not Dropped When The Next Hello Makes A Lobby In A Full Pool, Because The Lobby Is Not Empty Any More Although It Has Not Read The Hello Yet; A Lobby That No Hello Reached Still Gives Its Place Up") {
        ServerLimits l = lobby_limits();
        l.demo_lobbies = 2;
        l.lobby_empty_close_ms = 600000;
        const auto two_hellos = two_pages_at_once;                                                     // (the second one asks for a lobby of its own)
        {   // the first Hello is for the lobby that nobody is in (the room saw it empty in its last pass)
            World w(l);
            Client& a = w.connect_page("A", "same0001", lobby_block_of());
            w.connect_page("X", "same0002", lobby_block_of());                            // (the pool of two is full now)
            a.lobby->leave();
            w.run(1000);
            ASSERT_EQ(w.mgr.list(w.now).size(), size_t{2});
            const auto pair = two_hellos(w, "same0001", "same0003");
            ASSERT_TRUE(pair.first->lobby->phase() == net::ClientLobby::Phase::InRoom && !pair.first->lobby->created());
            ASSERT_EQ(pair.second->lobby->phase(), net::ClientLobby::Phase::Rejected);
            ASSERT_EQ(pair.second->lobby->reject_reason(), net::RejectReason::NoSuchRoom);          // (the pool is full of lobbies with a person in them)
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
            ASSERT_EQ(w.status("same0001").joined, size_t{1});
        }
        {   // no Hello reached the empty lobby: it gives its place up to the Hello that makes a new one, as ever
            World w(l);
            Client& a = w.connect_page("A", "same0011", lobby_block_of());
            w.connect_page("X", "same0012", lobby_block_of());
            a.lobby->leave();
            w.run(1000);
            const auto pair = two_hellos(w, "same0012", "same0013");
            ASSERT_TRUE(pair.second->lobby->phase() == net::ClientLobby::Phase::InRoom && pair.second->lobby->created());
            ASSERT_TRUE(w.status("same0011").code.empty() && w.status("same0013").state == RoomState::Waiting);
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
        }
    } TEST_END();

    TEST_CASE("S3.156 A Lobby Room Does Not Care Where The Server's 32-Bit Clock Is: Just Before The Signed Flip, Just After It, Across The Wrap; Names, Plan, START, The Back-Off Of Cancelled Starts, The Close Of An Empty Lobby; And A Lobby That Was Made At Time 0 And Is Read For The First Time Past The Signed Half Starts Its Match") {
        for (const uint32_t origin : {0x7FFFFE00u, 0x80000100u, 0xFFFFFC18u}) {
            const std::string at = " at origin " + std::to_string(origin);
            {   // names, plan (a bot in the last color), START, the match
                World w(lobby_limits());
                w.now = origin;
                Client& pia = w.connect_page("Pia", "clk00001", lobby_block_of());
                Client& bob = w.connect_page("Bob", "clk00001", lobby_block_of());
                const net::SeatKey pia_key = pia.lobby->key();
                const net::SeatKey bob_key = bob.lobby->key();
                ASSERT_TRUE(bob.lobby->request_name("Robert"));
                ASSERT_TRUE(pia.lobby->request_plan(plan_msg({K::Open, K::Open, K::Open, K::Easy})));
                w.run(300);
                ASSERT_MSG(w.status("clk00001").names[1] == "Robert", "the rename" + at);
                ASSERT_TRUE(pia.lobby->request_start());
                w.run(300);
                w.connect_game("Pia", "clk00001", pia_key);
                w.connect_game("Bob", "clk00001", bob_key);
                w.run(4000);
                ASSERT_MSG(w.status("clk00001").state == RoomState::Running, "the match starts" + at);
            }
            {   // a game that cannot load: four cancels in a row, the pause of a minute, then it can
                World w(lobby_limits());
                w.now = origin;
                Client& pia = w.connect_page("Pia", "clk00002", lobby_block_of());
                Client& bob = w.connect_page("Bob", "clk00002", lobby_block_of());
                const net::SeatKey pia_key = pia.lobby->key();
                const net::SeatKey bob_key = bob.lobby->key();
                ASSERT_TRUE(pia.lobby->request_start());
                Client& pia_game = w.connect_game("Pia", "clk00002", pia_key);
                Client& bob_game = w.connect_game("Bob", "clk00002", bob_key, true);
                w.run(500);
                for (int round = 0; round < 3; ++round) {
                    ASSERT_TRUE(pia_game.lobby->request_start());
                    w.run(500);
                    ASSERT_MSG(w.status("clk00002").starting, "a START asked for in the pause waits for it (the end of the pause is a time to come, whichever side of the wrap it falls on)" + at);
                    w.run(2000);
                }
                ASSERT_TRUE(pia_game.lobby->request_start());
                w.run(300);
                ASSERT_MSG(said(pia_game.room_chat) == stale_lines("Bob", 5), "the notice of the long pause" + at);
                w.run(62000);
                bob_game.fail_load = false;
                ASSERT_TRUE(pia_game.lobby->request_start());
                w.run(4000);
                ASSERT_MSG(w.status("clk00002").state == RoomState::Running, "the match starts after the long pause" + at);
            }
            {   // the last person leaves: the lobby closes a minute later (five seconds here)
                ServerLimits l = lobby_limits();
                l.lobby_empty_close_ms = 5000;
                World w(l);
                w.now = origin;
                Client& pia = w.connect_page("Pia", "clk00003", lobby_block_of());
                w.run(500);
                pia.lobby->leave();
                w.run(4500);
                ASSERT_MSG(w.mgr.room_count() == 1, "the lobby waits" + at);
                w.run(1500);
                ASSERT_MSG(w.mgr.room_count() == 0, "the lobby closes" + at);
            }
        }
        {   // a lobby that the operator made at time 0 and that is read for the first time past the signed half of the clock (the pause after a cancel is not an old time that reads as a time to come)
            World w(lobby_limits());
            w.now = 0;
            RoomSpec spec = spec_of("clk00010", 4);
            spec.lobby = true;
            spec.early_start = true;
            spec.leader_starts = true;
            spec.reconnect = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.now = 0x80000100u;
            Client& pia = w.connect_page("Pia", "clk00010", lobby_block_of());
            Client& bob = w.connect_page("Bob", "clk00010", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            const net::SeatKey bob_key = bob.lobby->key();
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            w.connect_game("Pia", "clk00010", pia_key);
            w.connect_game("Bob", "clk00010", bob_key);
            w.run(4000);
            ASSERT_TRUE(w.status("clk00010").state == RoomState::Running);
        }
    } TEST_END();

    TEST_CASE("S3.157 A Full Pool Of Lobbies Gives The Place Of An Idle One Up: When No Lobby Is Empty, The One In Which Nothing Has Been Done For Ten Minutes (One Minute Here), The Longest Idle First And On A Tie The First Code; Pings Are Not Use, A Line Of Chat Is; A Lobby Whose Only Person Is A Held Seat Is Idle Too; An Idle Lobby That A Hello Has Just Reached Is Not; The Same When The Server Has No Place For Another Room; And A Lobby That Is Used Is Never Given Up") {
        ServerLimits l = lobby_limits();
        l.demo_lobbies = 2;
        l.lobby_empty_close_ms = 600000;
        l.lobby_idle_evict_ms = 60000;
        {   // the longest idle goes; a line of chat is use, and a lobby that was used a minute ago is not idle for a minute yet
            World w(l);
            Client& a = w.connect_page("A", "idle0001", lobby_block_of());
            Client& b = w.connect_page("B", "idle0002", lobby_block_of());
            w.run(20000);
            ASSERT_TRUE(a.lobby->chat("anybody here?"));
            w.run(30000);                                                                 // idle0002 has done nothing for 50 s, idle0001 for 30
            Client& early = w.connect_page("Early", "idle0003", lobby_block_of());
            ASSERT_EQ(answer_of(w, early), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
            ASSERT_TRUE(a.server_end->is_open() && b.server_end->is_open());
            w.run(20000);                                                                 // 70 s and 50 s
            Client& c = w.connect_page("C", "idle0003", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(c.lobby->created() && c.lobby->phase() == net::ClientLobby::Phase::InRoom);
            ASSERT_TRUE(w.status("idle0002").code.empty());                               // the idle lobby is gone, its person is dropped
            ASSERT_FALSE(b.server_end->is_open());
            ASSERT_TRUE(w.status("idle0001").state == RoomState::Waiting && a.server_end->is_open());
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
            ASSERT_TRUE(w.mgr.take_ended(w.now).empty());                                 // (no word of it: it never ended)
        }
        {   // both idle: the one that has been idle the longest
            World w(l);
            Client& a = w.connect_page("A", "idle0011", lobby_block_of());
            w.run(5000);
            w.connect_page("B", "idle0012", lobby_block_of());
            w.run(70000);
            Client& c = w.connect_page("C", "idle0013", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(c.lobby->created());
            ASSERT_TRUE(w.status("idle0011").code.empty() && w.status("idle0012").state == RoomState::Waiting);
            ASSERT_FALSE(a.server_end->is_open());
        }
        {   // an empty lobby goes before an idle one with a person in it, however long that one has waited: nobody is dropped while a lobby is free
            World w(l);
            Client& a = w.connect_page("A", "idle0071", lobby_block_of());
            w.run(20000);
            Client& b = w.connect_page("B", "idle0072", lobby_block_of());
            w.run(50000);                                                                 // idle0071 has been idle for 70 s, idle0072 for 50
            b.lobby->leave();                                                             // ... and nobody is in it any more
            w.run(1000);
            Client& c = w.connect_page("C", "idle0073", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(c.lobby->created() && c.lobby->phase() == net::ClientLobby::Phase::InRoom);
            ASSERT_TRUE(w.status("idle0072").code.empty());
            ASSERT_TRUE(w.status("idle0071").state == RoomState::Waiting && a.server_end->is_open());
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
        }
        {   // two lobbies that have been idle for the same time (made in one pass): the first code goes, whichever was made first
            World w(l);
            w.next_kind = net::kClientPage;
            w.next_create = lobby_block_of();
            w.connect("A", "idle0022", 255, {20, 0});
            w.connect("B", "idle0021", 255, {20, 0});
            w.next_kind = net::kClientGame;
            w.next_create.reset();
            w.run(70000);
            Client& c = w.connect_page("C", "idle0023", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(c.lobby->created());
            ASSERT_TRUE(w.status("idle0021").code.empty() && w.status("idle0022").state == RoomState::Waiting);
        }
        {   // a lobby whose only person is a held seat (the link ended, the seat waits for its key for up to an hour) holds its place no longer than any other idle one
            ServerLimits held = l;
            held.lobby_hold_ms = 3600u * 1000u;
            World w(held);
            Client& a = w.connect_page("A", "idle0031", lobby_block_of());
            Client& b = w.connect_page("B", "idle0032", lobby_block_of());
            a.end->close();
            w.run(40000);
            ASSERT_TRUE(b.lobby->chat("still here"));
            w.run(30000);
            ASSERT_TRUE(w.status("idle0031").joined == 1);                                // (held: a person still)
            Client& c = w.connect_page("C", "idle0033", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(c.lobby->created());
            ASSERT_TRUE(w.status("idle0031").code.empty() && w.status("idle0032").state == RoomState::Waiting);
        }
        {   // a server that has no place for another room (--max-rooms) while its pool is not full: the idle lobby gives its place up, the lobby that is used does not
            ServerLimits full = l;
            full.max_rooms = 3;
            full.demo_lobbies = 3;
            for (const bool used : {false, true}) {
                World w(full);
                ASSERT_TRUE(w.mgr.create_room(spec_of("plain-1", 2), w.now).ok);
                ASSERT_TRUE(w.mgr.create_room(spec_of("plain-2", 2), w.now).ok);
                Client& a = w.connect_page("A", "idle0041", lobby_block_of());
                ASSERT_EQ(w.mgr.room_count(), size_t{3});
                w.run(used ? 40000 : 70000);
                if (used) {
                    ASSERT_TRUE(a.lobby->chat("we are here"));
                    w.run(30000);
                }
                Client& b = w.connect_page("B", "idle0042", lobby_block_of());
                w.run(300);
                if (used) {
                    ASSERT_EQ(b.lobby->phase(), net::ClientLobby::Phase::Rejected);
                    ASSERT_TRUE(w.status("idle0041").state == RoomState::Waiting && w.mgr.room_count() == size_t{3});
                } else {
                    ASSERT_TRUE(b.lobby->created());
                    ASSERT_TRUE(w.status("idle0041").code.empty() && w.status("idle0042").state == RoomState::Waiting && w.mgr.room_count() == size_t{3});
                }
            }
        }
        {   // an idle lobby that a Hello has just reached (the room reads the Hello in its next pass) is not given up to the Hello that makes a lobby in the same pass: nobody is dropped, and the one that wanted a lobby is
            // told NoSuchRoom, because the other lobby is used
            World w(l);
            w.connect_page("A", "idle0061", lobby_block_of());
            Client& b = w.connect_page("B", "idle0062", lobby_block_of());
            w.run(65000);
            ASSERT_TRUE(b.lobby->chat("busy"));
            w.run(5000);                                                                  // idle0061 has been idle for 70 s, idle0062 for 5
            const auto pair = two_pages_at_once(w, "idle0061", "idle0063");
            ASSERT_TRUE(pair.first->lobby->phase() == net::ClientLobby::Phase::InRoom && !pair.first->lobby->created());
            ASSERT_EQ(pair.second->lobby->phase(), net::ClientLobby::Phase::Rejected);
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
            ASSERT_EQ(w.status("idle0061").joined, size_t{2});
        }
        {   // idle lobbies are kept when the server is told so (0)
            ServerLimits keep = l;
            keep.lobby_idle_evict_ms = 0;
            World w(keep);
            w.connect_page("A", "idle0051", lobby_block_of());
            w.connect_page("B", "idle0052", lobby_block_of());
            w.run(3600u * 1000u);
            Client& c = w.connect_page("C", "idle0053", lobby_block_of());
            ASSERT_EQ(answer_of(w, c), net::RejectReason::NoSuchRoom);
            ASSERT_EQ(waiting_lobbies(w), size_t{2});
        }
    } TEST_END();

    TEST_CASE("S3.158 Two Empty Lobbies That Have Been Empty For The Same Time (Their Last People Left In One Pass): A Full Pool Gives The First Code Up, Whichever Was Made First") {
        ServerLimits l = lobby_limits();
        l.demo_lobbies = 2;
        l.lobby_empty_close_ms = 600000;
        World w(l);
        w.next_kind = net::kClientPage;
        w.next_create = lobby_block_of();
        Client& a = w.connect("A", "tie00002", 255, {20, 0});
        Client& b = w.connect("B", "tie00001", 255, {20, 0});
        w.next_kind = net::kClientGame;
        w.next_create.reset();
        w.run(500);
        a.lobby->leave();
        b.lobby->leave();
        w.run(3000);
        ASSERT_EQ(waiting_lobbies(w), size_t{2});
        Client& c = w.connect_page("C", "tie00003", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(c.lobby->created());
        ASSERT_TRUE(w.status("tie00001").code.empty() && w.status("tie00002").state == RoomState::Waiting);
    } TEST_END();

    TEST_CASE("S3.159 A Lobby In Which Nothing Was Done For Longer Than The 32-Bit Clock Counts (49.7 Days) Is Still Idle: Its Age Stops Growing At A Day (Room::kMaxIdleMs), So The Clock Cannot Read It As A Young One And A Full Pool Gives Its Place Up") {
        ServerLimits l = lobby_limits();
        l.demo_lobbies = 1;
        l.lobby_idle_evict_ms = 60000;
        l.lobby_empty_close_ms = 3600u * 1000u;
        l.lobby_silence_ms = 3600u * 1000u;                                               // (the link of the lobby's person is quiet between two steps of ten minutes)
        World w(l);
        Client& a = w.connect_page("A", "age00001", lobby_block_of());
        w.run_coarse((uint64_t{1} << 32) + 20000, 10u * 60u * 1000u);                    // the clock has been round once: the age of the lobby, read without the stop, is 20 s
        ASSERT_TRUE(a.server_end->is_open() && w.status("age00001").joined == 1);          // (its person never left: the lobby is idle, not empty)
        w.run(100);
        Client& b = w.connect_page("B", "age00002", lobby_block_of());
        w.run(300);
        ASSERT_TRUE(b.lobby->created() && b.lobby->phase() == net::ClientLobby::Phase::InRoom);
        ASSERT_TRUE(w.status("age00001").code.empty());
        ASSERT_FALSE(a.server_end->is_open());
        ASSERT_EQ(waiting_lobbies(w), size_t{1});
    } TEST_END();

    TEST_CASE("S3.160 A Lobby Room's Idle Time, Read From The Room Itself: It Counts From The Making Of The Room, Stops Growing At A Day However Often The Room Is Looked At (The 32-Bit Clock Goes Round In 49.7 Days), And Is 0 For A Room That Is Not A Lobby That Waits; A Match That A Lobby Began Is Never Given Up As An Idle Lobby, However Long It Is Since The Lobby Was Used") {
        const std::string maps = maps_dir();
        MapEntry entry;
        std::string problem;
        ASSERT_MSG(MapStore(maps).find("TINY.LVL", entry, &problem), problem);
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(entry.path));
        const uint64_t hour = 3600u * 1000u;
        const uint64_t day = 24u * hour;
        RoomSpec spec = spec_of("idle-r01", 4);
        spec.lobby = true;
        spec.early_start = true;
        spec.leader_starts = true;
        spec.reconnect = true;
        spec.empty_close_ms = 0xFFFFFFFFu;                                               // (nobody comes into these rooms, and they are not forgotten for that)
        {   // the age counts from the making of the room, and from the last thing that was done in it
            Room room(spec, entry, level, 1u, 5000u);
            ASSERT_TRUE(room.lobby_waiting());
            ASSERT_EQ(room.idle_ms(5000), 0u);
            room.update(5010);
            ASSERT_EQ(room.idle_ms(5010), 10u);
            room.update(65000);
            ASSERT_EQ(room.idle_ms(65000), 60000u);
        }
        {   // looked at every hour for fifty days: the age is what it is until a day, and a day after that, whatever the 32-bit clock reads
            Room room(spec, entry, level, 1u, 5000u);
            for (uint64_t t = 65000; t <= (uint64_t{1} << 32) + 30 * hour; t += hour) {
                const uint32_t now = static_cast<uint32_t>(t);
                room.update(now);
                ASSERT_EQ(uint64_t{room.idle_ms(now)}, std::min<uint64_t>(t - 5000u, day));
            }
        }
        {   // a room that is not a lobby that waits has no age: one that was forgotten, and one that is no lobby
            Room room(spec, entry, level, 1u, 5000u);
            room.update(65000);
            ASSERT_EQ(room.idle_ms(65000), 60000u);
            room.forget(65000);
            ASSERT_TRUE(!room.lobby_waiting());
            ASSERT_EQ(room.idle_ms(165000), 0u);
            Room plain(spec_of("idle-r02", 2), entry, level, 1u, 5000u);
            plain.update(65000);
            ASSERT_TRUE(!plain.lobby_waiting());
            ASSERT_EQ(plain.idle_ms(65000), 0u);
        }
        {   // a match that a lobby began holds its room however long it is since the lobby was used: it is no idle lobby, and a server that is full refuses a new lobby rather than end it
            ServerLimits l = lobby_limits();
            l.max_rooms = 2;
            l.lobby_idle_evict_ms = 3000;
            l.lobby_empty_close_ms = 600000;
            World w(l);
            Client& pia = w.connect_page("Pia", "keep0001", lobby_block_of());
            Client& bob = w.connect_page("Bob", "keep0001", lobby_block_of());
            const net::SeatKey pia_key = pia.lobby->key();
            const net::SeatKey bob_key = bob.lobby->key();
            ASSERT_TRUE(pia.lobby->request_start());
            w.run(300);
            w.connect_game("Pia", "keep0001", pia_key);
            w.connect_game("Bob", "keep0001", bob_key);
            w.run(4000);
            ASSERT_TRUE(w.status("keep0001").state == RoomState::Running);
            Client& dan = w.connect_page("Dan", "keep0002", lobby_block_of());
            w.run(300);
            ASSERT_TRUE(dan.lobby->created() && w.mgr.room_count() == size_t{2});
            w.run(10000);                                                                 // the match has been going on for fifteen seconds; nothing has been done in the first lobby since its START
            ASSERT_TRUE(dan.lobby->chat("anybody here?"));                                // (the second lobby is used a moment before the third is asked for)
            w.run(1000);
            Client& eve = w.connect_page("Eve", "keep0003", lobby_block_of());
            ASSERT_EQ(answer_of(w, eve), net::RejectReason::NoSuchRoom);
            ASSERT_TRUE(w.status("keep0001").state == RoomState::Running && w.status("keep0002").state == RoomState::Waiting);
            ASSERT_EQ(w.mgr.room_count(), size_t{2});
        }
    } TEST_END();

    TEST_CASE("S3.161 A Server That Is Still Replaying Its Records Counts Them Among Its Rooms When A New Lobby Needs A Place: With Two Records In The Queue And Three Places, The Lobby That Nobody Is In Gives Its Place Up To The Next One") {
        ServerLimits limits = lobby_limits();
        limits.max_rooms = 3;
        limits.lobby_empty_close_ms = 600000;
        PWorld w("restore-161", limits);
        w.start_server(500);
        crash_with_record_of(w, "RQ-1", 6000, 2);
        lengthen_record(w, "RQ-1", 40000);                                                // (a long replay: the queue stands for the length of this test)
        copy_record_as(w, "RQ-1", "RQ-2", std::chrono::seconds(0));
        age_record(w, "RQ-1", 10);
        age_record(w, "RQ-2", 20);
        w.restart.restore_slice_ms = 0;
        w.start_server(500, false);
        ASSERT_EQ(w.mgr->restoring_count(), size_t{2});
        const auto page_hello = [&w](const std::string& code) {                         // what a front page says: Hello with the lobby block
            net::Connection* link = w.open_link();
            net::HelloMsg hello;
            hello.name = "Raw";
            hello.room = code;
            hello.client_kind = net::kClientPage;
            hello.create = lobby_block_of();
            link->send(net::encode(hello));
            return link;
        };
        net::Connection* first = page_hello("lobq0001");                                   // two records and one room: the three places
        w.run(100);
        ASSERT_TRUE(w.status("lobq0001").lobby && w.mgr->room_count() == size_t{1} && w.mgr->restoring_count() == size_t{2});
        first->send(net::encode_leave());                                                 // nobody is in it now
        w.run(100);
        page_hello("lobq0002");
        w.run(100);
        ASSERT_TRUE(w.status("lobq0002").lobby && w.status("lobq0002").state == RoomState::Waiting);
        ASSERT_TRUE(w.status("lobq0001").code.empty());
        ASSERT_TRUE(w.mgr->room_count() == size_t{1} && w.mgr->restoring_count() == size_t{2});
    } TEST_END();
}
