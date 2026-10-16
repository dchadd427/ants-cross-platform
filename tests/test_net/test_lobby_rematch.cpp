// Tests of the room before a match: the rematch of a lobby room (docs/SERVER.md "Lobby rooms", net::HostLobby (7)): what a lobby room's match gives for the lobby that follows it, the reserved seats of
// that lobby and the way back into them (N4.41 - N4.45)
#include "lobby_fixtures.hpp"

namespace {

// A guest that is a page (the front page) and shows a key: a person whose page comes back to the room
ClientLobby::Config page_with_key(const std::string& name, const SeatKey& key) {
    ClientLobby::Config cc = page_config(name);
    cc.key = key;
    return cc;
}

// A lobby room goes as far as its match: Ann's and Bob's pages join (Ann leads), Ann's plan and START, both pages go and their games take their seats over with the keys, the owner starts, the games
// load and the match begins. The keys of Ann and Bob (zero when a step did not happen: the test looks at the phase).
std::pair<SeatKey, SeatKey> play_to_begun(Room& room, const PlanMsg& plan) {
    const size_t ann = join_with(room, page_config("Ann"));
    room.run(100);
    const size_t bob = join_with(room, page_config("Bob"));
    room.run(100);
    room.guests[ann].lobby->request_plan(plan);
    room.run(100);
    room.guests[ann].lobby->request_start();
    room.run(100);
    const SeatKey a = room.guests[ann].lobby->key();
    const SeatKey b = room.guests[bob].lobby->key();
    room.guests[ann].client_end->close();
    room.guests[bob].client_end->close();
    room.run(100);
    const size_t ga = join_keyed(room, "Ann", a);
    const size_t gb = join_keyed(room, "Bob", b);
    room.run(200);
    room.host.take_events();
    room.host.start(5, 6, room.now);                       // (the owner starts: a server's room seats the plan's bots first, which is no business of these tests)
    room.run(100);
    room.guests[ga].lobby->report_loaded(true);
    room.guests[gb].lobby->report_loaded(true);
    room.host.host_loaded(true);
    room.run(100);
    return {a, b};
}

// The same with three persons, Ann, Bob and Cat (seats 0, 1 and 2), whose games tell what they run on (`platforms`, in that order: protocol 15): the keys of the three
std::array<SeatKey, 3> play_three_to_begun(Room& room, const PlanMsg& plan, const std::array<uint8_t, 3>& platforms) {
    static const char* const kNames[3] = {"Ann", "Bob", "Cat"};
    std::array<size_t, 3> pages{};
    for (size_t i = 0; i < 3; ++i) {
        pages[i] = join_with(room, page_config(kNames[i]));
        room.run(100);
    }
    room.guests[pages[0]].lobby->request_plan(plan);
    room.run(100);
    room.guests[pages[0]].lobby->request_start();
    room.run(100);
    std::array<SeatKey, 3> keys{};
    for (size_t i = 0; i < 3; ++i) keys[i] = room.guests[pages[i]].lobby->key();
    for (size_t i = 0; i < 3; ++i) room.guests[pages[i]].client_end->close();
    room.run(100);
    std::array<size_t, 3> games{};
    for (size_t i = 0; i < 3; ++i) {
        ClientLobby::Config cc;
        cc.name = kNames[i];
        cc.key = keys[i];
        cc.platform = platforms[i];
        games[i] = join_with(room, cc);
    }
    room.run(200);
    room.host.take_events();
    room.host.start(5, 6, room.now);
    room.run(100);
    for (size_t i = 0; i < 3; ++i) room.guests[games[i]].lobby->report_loaded(true);
    room.host.host_loaded(true);
    room.run(100);
    return keys;
}

// The room's clock in steps of `step` ms, for the hours of a reservation (every link speaks within a step: the silence limit is far above it)
void run_coarse(Room& room, uint64_t ms, uint32_t step) {
    for (uint64_t elapsed = 0; elapsed < ms; elapsed += step) {
        room.now += static_cast<uint32_t>(std::min<uint64_t>(step, ms - elapsed));
        room.net.set_time(room.now);
        room.host.update(room.now);
        for (auto& g : room.guests) {
            if (!g.frozen) g.lobby->update(room.now);
        }
    }
}

// A lobby room that is opened again from `r` (what the server does when the match is over), at the time `now`
bool reopened(Room& room, const Rematch& r) { return room.host.reopen(r, room.now); }

// A rematch of Ann (seat 0, first in the order) and Bob (seat 1, second) on TINY.LVL, with the plan `plan`
Rematch rematch_of(const std::array<PlanKind, sim::MAX_PLAYERS>& plan, uint8_t team_a = kNoTeam, uint8_t team_b = kNoTeam) {
    Rematch r;
    r.map_name = "TINY.LVL";
    r.plan = plan;
    r.team_a = team_a;
    r.team_b = team_b;
    r.people.push_back(Rematch::Person{0, "Ann", key_with(1), 1, static_cast<uint8_t>(kPlatformBrowser | kOsMacos)});
    r.people.push_back(Rematch::Person{1, "Bob", key_with(2), 2, static_cast<uint8_t>(kPlatformBrowser | kOsWindows)});
    return r;
}

}  // namespace

void run_lobby_rematch_tests() {
    using K = PlanKind;

    TEST_CASE("N4.41 The Rematch (A Lobby Room Opened Again After Its Match): A Lobby Room Whose Match Has Begun Gives Its Plan (The Map, What Each Colour Is, The Teams, The Mode) And Every Person Of Its Start (Seat, Name, Key, Place In The Order, Platform); A Fresh Lobby Room Takes It: The Same Colours And Names, Each Seat Reserved (A Game In The Room Message, Nobody Present, Nobody Leads); What Does Not Fit Changes Nothing") {
        {   // what a match gives
            Room room(lobby_room_config(140));
            ASSERT_TRUE(room.host.rematch().people.empty());                                         // (no match yet)
            const auto keys = play_to_begun(room, plan_of({K::Open, K::Open, K::Easy, K::Nobody}, 0, 2));
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            const Rematch r = room.host.rematch();
            ASSERT_EQ(r.map_name, std::string("TINY.LVL"));
            ASSERT_TRUE(r.plan == (std::array<K, 4>{K::Open, K::Open, K::Easy, K::Nobody}) && r.team_a == 0 && r.team_b == 2 && r.mode == 0);
            ASSERT_EQ(r.people.size(), size_t{2});
            ASSERT_TRUE(r.people[0].seat == 0 && r.people[0].name == "Ann" && key_matches(r.people[0].key, keys.first) && r.people[0].order == 1u);
            ASSERT_TRUE(r.people[1].seat == 1 && r.people[1].name == "Bob" && key_matches(r.people[1].key, keys.second) && r.people[1].order == 2u);
            Room plain(keyed_server_config(141));                                                     // a room that is no lobby room gives nothing
            plain.join_seat("Ann");
            plain.join_seat("Bob");
            ASSERT_TRUE(plain.host.start(5, 6, plain.now));
            plain.run(100);
            ASSERT_TRUE(plain.host.rematch().people.empty());
        }
        {   // a fresh lobby room takes it
            Room room(lobby_room_config(142), false);
            Rematch r = rematch_of({K::Open, K::Open, K::Easy, K::Nobody}, 0, 2);
            r.map_name = "SMALL.LVL";
            r.mode = 1;
            ASSERT_TRUE(reopened(room, r));
            const RoomMsg& m = room.host.room();
            ASSERT_EQ(layout(m), std::string("Ann@0 Bob@1"));
            ASSERT_TRUE(m.slots[0].state == SlotState::Client && m.slots[0].rtt_ms == kRttUnknown && m.slots[0].platform == (kPlatformBrowser | kOsMacos) && m.slots[1].platform == (kPlatformBrowser | kOsWindows));
            ASSERT_EQ(plan_text(m), std::string("ooen 0+2"));
            ASSERT_TRUE(m.map_name == "SMALL.LVL" && m.mode == 1 && m.lobby() && !m.starting());
            ASSERT_EQ(m.leader, kNoLeader);                                                             // nobody leads before somebody is back
            ASSERT_EQ(games_text(m), std::string("1100"));                                             // a reserved seat is a game in the Room message: its person is on the results screen
            ASSERT_TRUE(room.host.reserved(0) && room.host.reserved(1) && !room.host.reserved(2) && room.host.any_reserved());
            ASSERT_EQ(room.host.present(), size_t{0});                                                  // ... and nobody is in the room
            ASSERT_EQ(room.host.humans(), size_t{2});
            ASSERT_FALSE(reopened(room, r));                                                            // once
        }
        {   // what does not fit changes nothing
            const auto refused = [](const Rematch& r, HostLobby::Config hc) {
                Room room(hc, false);
                return !room.host.reopen(r, room.now) && layout(room.host.room()).empty() && !room.host.any_reserved();
            };
            Rematch twice = rematch_of({K::Open, K::Open, K::Open, K::Open});
            twice.people[1].seat = 0;
            ASSERT_TRUE(refused(twice, lobby_room_config(143)));                                        // a seat twice
            Rematch same_key = rematch_of({K::Open, K::Open, K::Open, K::Open});
            same_key.people[1].key = same_key.people[0].key;
            ASSERT_TRUE(refused(same_key, lobby_room_config(144)));                                     // a key twice
            Rematch no_key = rematch_of({K::Open, K::Open, K::Open, K::Open});
            no_key.people[1].key = SeatKey{};
            ASSERT_TRUE(refused(no_key, lobby_room_config(145)));                                       // a person without a key
            Rematch bad_teams = rematch_of({K::Open, K::Open, K::Open, K::Open}, 2, 1);
            ASSERT_TRUE(refused(bad_teams, lobby_room_config(146)));
            Rematch nobody = rematch_of({K::Open, K::Open, K::Open, K::Open});
            nobody.people.clear();
            ASSERT_TRUE(refused(nobody, lobby_room_config(147)));
            ASSERT_TRUE(refused(rematch_of({K::Open, K::Open, K::Open, K::Open}), keyed_server_config(148)));        // a room that is no lobby room
            Room busy(lobby_room_config(149));                                                          // a room that somebody is in
            busy.join_seat("Cat");
            ASSERT_FALSE(busy.host.reopen(rematch_of({K::Open, K::Open, K::Open, K::Open}), busy.now));
            ASSERT_EQ(layout(busy.host.room()), std::string("Cat@0"));
        }
    } TEST_END();

    TEST_CASE("N4.42 The Way Back Into A Reserved Seat: Nobody New Is Taken While Nobody Of The Match Is Back (A Stranger, A Key That Fits No Seat); A Game With The Key Holds The Seat From The Results Screen And Leaves It Reserved; A Page With The Key Brings The Person Back, Who Leads When Earliest In The Order (The Host Leads Again On Return); A Newcomer Comes After Every Person Of The Match") {
        Room room(lobby_room_config(150), false);
        ASSERT_TRUE(reopened(room, rematch_of({K::Open, K::Open, K::Easy, K::Open})));
        // nobody of the match is back: a newcomer is turned away, and so is a key that fits no seat (it is a newcomer's Hello)
        const size_t stranger = join_with(room, page_config("Sid"));
        const size_t stale = join_with(room, page_with_key("Sam", key_with(9)));
        room.run(200);
        for (const size_t g : {stranger, stale}) {
            ASSERT_EQ(room.guests[g].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[g].lobby->reject_reason(), RejectReason::MatchRunning);
        }
        // Bob's results screen: a game with his key holds his seat (his own key, his seat), and the seat stays reserved
        const size_t bob_results = join_keyed(room, "Bob", key_with(2));
        room.run(200);
        ClientLobby& br = *room.guests[bob_results].lobby;
        ASSERT_TRUE(br.phase() == ClientLobby::Phase::InRoom && br.my_seat() == 1 && key_matches(br.key(), key_with(2)));
        ASSERT_TRUE(room.host.reserved(1) && room.host.present() == 1u && room.host.leader() == kNoLeader);
        ASSERT_EQ(games_text(br.room()), std::string("1100"));
        ASSERT_EQ(br.room().leader, kNoLeader);
        ASSERT_EQ(room.host.returns(), 0u);
        // Bob's page comes back (his results screen is told that another window has the seat): he is back, and leads (the only one back)
        const size_t bob_page = join_with(room, page_with_key("Bob", key_with(2)));
        room.run(200);
        ClientLobby& bp = *room.guests[bob_page].lobby;
        ASSERT_TRUE(bp.phase() == ClientLobby::Phase::InRoom && bp.my_seat() == 1);
        ASSERT_TRUE(br.phase() == ClientLobby::Phase::Rejected && br.reject_reason() == RejectReason::Superseded);
        ASSERT_TRUE(!room.host.reserved(1) && room.host.reserved(0) && room.host.returns() == 1u);
        ASSERT_EQ(room.host.leader(), 1);
        ASSERT_TRUE(bp.is_leader());
        ASSERT_EQ(games_text(bp.room()), std::string("1000"));                                        // Ann is still on the results screen
        // Ann's page comes back: she was first in the order, so she leads again
        const size_t ann_page = join_with(room, page_with_key("Ann", key_with(1)));
        room.run(200);
        ClientLobby& ap = *room.guests[ann_page].lobby;
        ASSERT_TRUE(ap.phase() == ClientLobby::Phase::InRoom && ap.my_seat() == 0 && ap.is_leader() && !bp.is_leader());
        ASSERT_TRUE(!room.host.any_reserved() && room.host.returns() == 2u && room.host.present() == 2u);
        ASSERT_EQ(games_text(ap.room()), std::string("0000"));
        // somebody of the match is back: a newcomer is let in (the open colour 3; the Easy colour 2 is never a joiner's), after every person of the match in the order
        const size_t cat = join_with(room, page_config("Cat"));
        room.run(200);
        ASSERT_TRUE(room.guests[cat].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[cat].lobby->my_seat() == 3);
        ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1 Cat@3"));
        room.guests[ann_page].lobby->leave();                                                          // Ann goes: Bob leads (before the newcomer)
        room.run(200);
        ASSERT_EQ(room.host.leader(), 1);
    } TEST_END();

    TEST_CASE("N4.43 The START Of A Rematch Waits For Everybody Who Is Not Back: The Leader's START Is Not Taken While A Seat Is Reserved, The Leader Is Told Who Is Missing, The Owner Is Not Asked And Cannot Start; When The Person Comes Back Or The Leader Removes Them (Their Results Screen Is Told), The START Is Taken As In Any Lobby Room") {
        {   // one person not back: the notice names them; the leader removes them and the START is taken
            Room room(lobby_room_config(151), false);
            ASSERT_TRUE(reopened(room, rematch_of({K::Open, K::Open, K::Easy, K::Nobody})));
            const size_t ann = join_with(room, page_with_key("Ann", key_with(1)));
            const size_t bob_results = join_keyed(room, "Bob", key_with(2));
            room.run(200);
            ClientLobby& ap = *room.guests[ann].lobby;
            ASSERT_TRUE(ap.is_leader() && room.host.reserved(1));
            room.host.take_events();
            ASSERT_TRUE(ap.request_start());
            room.run(200);
            ASSERT_FALSE(room.host.starting() || ap.room().starting());
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_EQ(notices_of(ap), (std::vector<std::string>{"Bob is not back from the match yet."}));
            ASSERT_TRUE(notices_of(*room.guests[bob_results].lobby).empty());
            ASSERT_FALSE(room.host.start(5, 6, room.now));                                              // (the owner cannot start it either)
            ASSERT_TRUE(ap.request_remove(1));
            room.run(200);
            ASSERT_TRUE(room.guests[bob_results].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob_results].lobby->reject_reason() == RejectReason::Kicked);
            ASSERT_TRUE(!room.host.occupied(1) && !room.host.any_reserved() && room.host.removals() == 1u);
            ASSERT_TRUE(ap.request_start());                                                           // Ann and the Easy bot of the plan: the START stands (it waits for Ann's game)
            room.run(200);
            ASSERT_TRUE(room.host.starting());
        }
        {   // two not back: the notice names both; one comes back, the START still waits for the other; when the other is back too, it is taken
            Room room(lobby_room_config(152), false);
            Rematch r = rematch_of({K::Open, K::Open, K::Open, K::Open});
            r.people.push_back(Rematch::Person{2, "Cat", key_with(3), 3, 0});
            ASSERT_TRUE(reopened(room, r));
            const size_t ann = join_with(room, page_with_key("Ann", key_with(1)));
            room.run(200);
            ClientLobby& ap = *room.guests[ann].lobby;
            ASSERT_TRUE(ap.request_start());
            room.run(200);
            ASSERT_EQ(notices_of(ap), (std::vector<std::string>{"These players are not back from the match yet: Bob and Cat."}));
            join_with(room, page_with_key("Bob", key_with(2)));
            room.run(200);
            ASSERT_TRUE(ap.request_start());
            room.run(200);
            ASSERT_FALSE(room.host.starting());
            ASSERT_EQ(notices_of(ap), (std::vector<std::string>{"Cat is not back from the match yet."}));
            join_with(room, page_with_key("Cat", key_with(3)));
            room.run(200);
            ASSERT_TRUE(ap.request_start());
            room.run(200);
            ASSERT_TRUE(room.host.starting() && ap.room().starting());
            ASSERT_TRUE(notices_of(ap).empty());
        }
    } TEST_END();

    TEST_CASE("N4.44 The Life Of A Reservation: A Results Screen That Goes Leaves The Seat Reserved (No Hold, No Minute: It Waits For Its Person), A Quiet One Is Closed And Leaves It Reserved Too; The Reservation Runs Out After reserve_ms (A Results Screen That Holds It Is Told); A Person Who Is Back Is An Ordinary Guest (Held For A Minute When The Link Ends, Then Gone); The Leader Can Move A Reserved Person, Whose Key Finds The New Colour") {
        HostLobby::Config hc = lobby_room_config(153);
        hc.reserve_ms = 600000;
        hc.hold_ms = 5000;
        hc.silence_ms = 3000;
        Room room(hc, false);
        ASSERT_TRUE(reopened(room, rematch_of({K::Open, K::Open, K::Open, K::Open})));
        const size_t ann = join_with(room, page_with_key("Ann", key_with(1)));
        const size_t bob_results = join_keyed(room, "Bob", key_with(2));
        room.run(200);
        ASSERT_TRUE(room.host.present() == 2u && room.host.reserved(1));
        // the results screen goes: the seat stays reserved, with no hold that would run out
        room.guests[bob_results].client_end->close();
        room.run(200);
        ASSERT_TRUE(room.host.reserved(1) && !room.host.held(1) && room.host.occupied(1) && room.host.present() == 1u);
        room.run(20000);                                                                               // (four times the hold)
        ASSERT_TRUE(room.host.reserved(1) && room.host.occupied(1) && room.host.hold_expiries() == 0u);
        // a quiet results screen is closed by the silence, and the seat stays reserved
        const size_t bob_asleep = join_keyed(room, "Bob", key_with(2));
        room.run(200);
        ASSERT_TRUE(room.host.reserved(1) && room.host.present() == 2u);
        room.guests[bob_asleep].frozen = true;
        room.run(4000);
        ASSERT_FALSE(room.guests[bob_asleep].host_end->is_open());
        ASSERT_TRUE(room.host.reserved(1) && room.host.present() == 1u && room.host.silences() >= 1u);
        // the leader moves Bob, who is not back, to the open colour 3: his key finds him there
        ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
        room.run(200);
        ASSERT_TRUE(room.host.reserved(3) && !room.host.occupied(1));
        const size_t bob_again = join_keyed(room, "Bob", key_with(2));
        room.run(200);
        ASSERT_TRUE(room.guests[bob_again].lobby->my_seat() == 3 && room.host.reserved(3));
        // the reservation runs out: the results screen that holds it is told (Kicked), the colour is free
        room.run(600000);
        ASSERT_TRUE(!room.host.occupied(3) && !room.host.any_reserved() && room.host.reserve_expiries() == 1u);
        ASSERT_TRUE(room.guests[bob_again].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob_again].lobby->reject_reason() == RejectReason::Kicked);
        // a person who is back is an ordinary guest: held for the minute (here 5 s) when the link ends, then gone (not reserved again)
        room.guests[ann].client_end->close();
        room.run(200);
        ASSERT_TRUE(room.host.held(0) && !room.host.reserved(0));
        room.run(6000);
        ASSERT_TRUE(!room.host.occupied(0) && room.host.hold_expiries() == 1u && !room.host.any_reserved());
    } TEST_END();

    TEST_CASE("N4.45 Whose Seat Is Kept: A Game In A Browser And A Game That Did Not Say What It Runs On Keep Their Seats, A Game Of The Desktop Or A Phone's App (No \"Back To The Lobby\" There) Keeps None: Its Colour Is Open In The Lobby That Follows And It Joins By The Code Like Anybody Once Somebody Is Back; A Stranger Never Takes A Kept Colour (Asked For, Or The Last Open One: Full), A Key Of Another Room Is A Stranger's; Two Tabs With One Key: The Later Takes The Seat (The Other Is Superseded), The Person Stays Back And Leads; A Reservation Lasts Three Hours On The Room's Clock") {
        {   // the match of Ann (a game in a browser), Bob (a game of the desktop) and Cat (a game that did not say)
            Room room(lobby_room_config(154));
            const auto keys = play_three_to_begun(room, plan_of({K::Open, K::Open, K::Open, K::Hard}), {static_cast<uint8_t>(kPlatformBrowser | kOsOther), kOsWindows, kPlatformUnknown});
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            ASSERT_TRUE(room.host.room().slots[0].platform == (kPlatformBrowser | kOsOther) && room.host.room().slots[1].platform == kOsWindows && room.host.room().slots[2].platform == kPlatformUnknown);
            const Rematch r = room.host.rematch();
            ASSERT_EQ(r.people.size(), size_t{2});
            ASSERT_TRUE(r.people[0].seat == 0 && key_matches(r.people[0].key, keys[0]) && r.people[1].seat == 2 && key_matches(r.people[1].key, keys[2]));
            ASSERT_TRUE(r.plan == (std::array<K, 4>{K::Open, K::Open, K::Open, K::Hard}));                // (Bob's colour is an open colour of the plan)
            // the lobby that follows: Ann's and Cat's colours are kept, Bob's is open
            Room next(lobby_room_config(155), false);
            ASSERT_TRUE(reopened(next, r));
            ASSERT_EQ(layout(next.host.room()), std::string("Ann@0 Cat@2"));
            ASSERT_EQ(games_text(next.host.room()), std::string("1010"));
            ASSERT_TRUE(next.host.reserved(0) && !next.host.occupied(1) && next.host.reserved(2));
            // Bob's game of the desktop joins by the code (no key: the match's key is over): turned away while nobody of the match is back, like anybody
            ClientLobby::Config desktop;
            desktop.name = "Bob";
            desktop.platform = kOsWindows;
            const size_t early = join_with(next, desktop);
            next.run(200);
            ASSERT_TRUE(next.guests[early].lobby->phase() == ClientLobby::Phase::Rejected && next.guests[early].lobby->reject_reason() == RejectReason::MatchRunning);
            // Ann is back: Bob comes in like anybody, at the lowest open colour (his own)
            const size_t ann = join_with(next, page_with_key("Ann", keys[0]));
            next.run(200);
            const size_t bob = join_with(next, desktop);
            next.run(200);
            ASSERT_TRUE(next.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom && next.guests[bob].lobby->my_seat() == 1);
            ASSERT_EQ(layout(next.host.room()), std::string("Ann@0 Bob@1 Cat@2"));
            ASSERT_EQ(games_text(next.host.room()), std::string("0110"));                              // (a game of the desktop is a game in the room; Cat's colour is kept)
            ASSERT_TRUE(next.guests[ann].lobby->is_leader() && !next.host.reserved(1) && next.host.reserved(2));
            // START waits for Cat only
            ASSERT_TRUE(next.guests[ann].lobby->request_start());
            next.run(200);
            ASSERT_FALSE(next.host.starting());
            ASSERT_EQ(notices_of(*next.guests[ann].lobby), (std::vector<std::string>{"Cat is not back from the match yet."}));
        }
        {   // the games of a phone's apps and of the desktop: nobody to keep a seat for (the server opens no lobby then)
            Room room(lobby_room_config(156));
            play_three_to_begun(room, plan_of({K::Open, K::Open, K::Open, K::Nobody}), {kOsAndroid, kOsIos, kOsMacos});
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Begun);
            ASSERT_TRUE(room.host.rematch().people.empty());
        }
        {   // a stranger never takes a kept colour; a key of another room is a stranger's
            Room room(lobby_room_config(157), false);
            ASSERT_TRUE(reopened(room, rematch_of({K::Open, K::Open, K::Open, K::Nobody})));
            const size_t ann = join_with(room, page_with_key("Ann", key_with(1)));
            room.run(200);
            ClientLobby::Config sid = page_config("Sid");
            sid.want_seat = 1;                                                                              // (Bob's colour, which is kept)
            const size_t s1 = join_with(room, sid);
            room.run(200);
            ASSERT_TRUE(room.guests[s1].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[s1].lobby->my_seat() == 2 && room.host.reserved(1));
            const size_t other = join_with(room, page_with_key("Ola", key_with(77)));                      // (no open colour is left: the kept one is not one)
            room.run(200);
            ASSERT_TRUE(room.guests[other].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[other].lobby->reject_reason() == RejectReason::Full);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1 Sid@2"));
            ASSERT_TRUE(room.host.reserved(1) && room.guests[ann].lobby->is_leader());
            // two tabs with Ann's key: the later takes her seat over (the earlier is told Superseded); she is still back, still first in the order, and leads
            const size_t ann_tab2 = join_with(room, page_with_key("Ann", key_with(1)));
            room.run(200);
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[ann].lobby->reject_reason() == RejectReason::Superseded);
            ASSERT_TRUE(room.guests[ann_tab2].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[ann_tab2].lobby->my_seat() == 0 && room.guests[ann_tab2].lobby->is_leader());
            ASSERT_TRUE(!room.host.reserved(0) && room.host.reserved(1) && room.host.returns() == 1u && room.host.takeovers() == 2u);
            // Bob's two tabs: the results screen holds his seat, his page takes it over, and a second page with his key takes it from the first; he stays back
            const size_t bob_results = join_keyed(room, "Bob", key_with(2));
            room.run(200);
            const size_t bob_tab1 = join_with(room, page_with_key("Bob", key_with(2)));
            room.run(200);
            const size_t bob_tab2 = join_with(room, page_with_key("Bob", key_with(2)));
            room.run(200);
            ASSERT_TRUE(room.guests[bob_results].lobby->reject_reason() == RejectReason::Superseded && room.guests[bob_tab1].lobby->reject_reason() == RejectReason::Superseded);
            ASSERT_TRUE(room.guests[bob_tab2].lobby->my_seat() == 1 && !room.host.any_reserved() && room.host.returns() == 2u);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1 Sid@2"));
        }
        {   // three hours on the room's clock (the default reservation): Ann is back and keeps the room, Bob's colour waits for him to the millisecond
            ASSERT_EQ(kLobbyReserveMs, 3u * 3600u * 1000u);
            ASSERT_EQ(lobby_room_config(158).reserve_ms, kLobbyReserveMs);
            Room room(lobby_room_config(158), false);
            const uint32_t since = room.now;
            ASSERT_TRUE(reopened(room, rematch_of({K::Open, K::Open, K::Open, K::Open})));
            const size_t ann = join_with(room, page_with_key("Ann", key_with(1)));
            room.run(200);
            run_coarse(room, kLobbyReserveMs - (room.now - since) - 1000u, 1000);
            ASSERT_TRUE(room.host.reserved(1) && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom && room.host.reserve_expiries() == 0u);
            run_coarse(room, 990, 10);
            ASSERT_TRUE(room.now - since == kLobbyReserveMs - 10u && room.host.reserved(1));
            run_coarse(room, 10, 10);
            ASSERT_TRUE(!room.host.occupied(1) && room.host.reserve_expiries() == 1u && room.guests[ann].lobby->is_leader());
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0"));
        }
    } TEST_END();
}
