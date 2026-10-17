// Tests of the room before a match: the swap, the room's rules, the platforms and the lobby room of protocol 16 and 17 (N4.23 - N4.38, N4.40)
#include "lobby_fixtures.hpp"

#include <set>

#include "ants_ai/bot_names.hpp"

void run_lobby_rooms_tests() {
    TEST_CASE("N4.23 The Swap And The Guard (Protocol 15): Two Guests Change Places, In A Full Room As In One With A Free Color, Each With All That Is Its Own And Each Told By The Room; A Press Acts Only On The Seating That Its Leader Saw, So A Newcomer, A Replacement Or A Second Tap Of The Same Press Is Never Moved Or Swapped By Mistake; A Color That A Bot Or The Host Holds Stays Where It Is") {
        {   // a full room (no color is free) is arranged by swaps: the leader puts Bob and Dan in each other's colors; each goes with all that is its own and each is told by the room, nobody else is
            Room room(keyed_server_config(40));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const size_t dan = room.join_seat("Dan");
            room.run(300);
            for (const size_t g : {ann, bob, cat, dan}) room.guests[g].lobby->take_chat();
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2 Dan@3");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const SeatKey dan_key = room.guests[dan].lobby->key();
            const auto bob_rtt = room.host.room().slots[1].rtt_ms;
            const auto dan_rtt = room.host.room().slots[3].rtt_ms;
            ASSERT_TRUE(bob_rtt != kRttUnknown && dan_rtt != kRttUnknown);
            const uint32_t seated = seating_hash(room.host.room());
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Dan@1 Cat@2 Bob@3");
            for (const size_t g : {ann, bob, cat, dan}) ASSERT_EQ(layout(room.guests[g].lobby->room()), "Ann@0 Dan@1 Cat@2 Bob@3");
            ASSERT_TRUE(room.guests[ann].lobby->my_seat() == 0 && room.guests[dan].lobby->my_seat() == 1 && room.guests[cat].lobby->my_seat() == 2 && room.guests[bob].lobby->my_seat() == 3);
            ASSERT_TRUE(key_matches(room.host.key_of(3), bob_key) && key_matches(room.host.key_of(1), dan_key));          // the key is the player's, wherever the player sits
            ASSERT_TRUE(room.host.room().slots[3].rtt_ms == bob_rtt && room.host.room().slots[1].rtt_ms == dan_rtt);     // ... and so is the thumb
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Black."}));
            ASSERT_EQ(notices_of(*room.guests[dan].lobby), (std::vector<std::string>{"Ann moved you to Red."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[cat].lobby).empty());
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 0u && room.host.players() == 4u && room.host.leader() == 0);
            ASSERT_TRUE(seating_hash(room.host.room()) != seated);                                                       // (the seating that the next press has to know is the new one)
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(0, 2));                                                // the leader's own color is a guest's too: Ann and Cat change places and Ann still leads
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Cat@0 Dan@1 Ann@2 Bob@3");
            ASSERT_TRUE(room.host.leader() == 2 && room.guests[ann].lobby->is_leader() && room.guests[ann].lobby->my_seat() == 2 && room.guests[cat].lobby->room().leader == 2);
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Green."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[bob].lobby).empty() && notices_of(*room.guests[dan].lobby).empty());
            ASSERT_TRUE(room.host.seat_moves() == 2u && room.host.ignored_seat_moves() == 0u);
            room.guests[ann].lobby->leave();                                                                             // the order of the Welcomes is the player's, swapped with it: Bob came before Cat and Dan, so Bob leads (in Black), not the lowest seat (Cat, Green)
            room.run(300);
            ASSERT_EQ(room.host.leader(), 3);
            ASSERT_TRUE(room.guests[bob].lobby->is_leader() && !room.guests[cat].lobby->is_leader() && !room.guests[dan].lobby->is_leader());
        }
        {   // two guests of a room with a free color: the swap does not need it, and the free color is still a move (the guest goes there, nobody changes places)
            Room room(keyed_server_config(41));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 1));                                                // Cat onto Bob's color
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bob@2");
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                                                // Cat onto the free color
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@2 Cat@3");
            ASSERT_TRUE(!room.host.occupied(1) && room.host.seat_moves() == 2u && room.host.ignored_seat_moves() == 0u);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Blue."}));
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Red.", "Ann moved you to Black."}));
        }
        {   // the same press twice (a double tap that is made before the answer of the room is on the screen): the first swaps, the second was made for the seats as they were and is ignored and counted, so a double tap
            // never swaps the two back
            Room room(keyed_server_config(42));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(300);
            room.guests[bob].lobby->take_chat();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bob@2");
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 1u);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Blue."}));
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));                                                // a press that is made for the new seats is a new press: it swaps them back
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.seat_moves() == 2u && room.host.ignored_seat_moves() == 1u);
        }
        {   // a newcomer: the press was made for a room with a free color, and the newcomer took the color before it came; it would have swapped the newcomer out of the color that it took. It is ignored and counted,
            // nobody moves, nobody is told; the leader's screen has the newcomer a moment later and its next press is the one that swaps
            Room room(keyed_server_config(43));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(300);
            Screen screen{room.host.room()};                                                                             // (the seats that the leader saw when it pressed)
            const size_t dan = room.join_seat("Dan");                                                                    // ... and Dan takes Black before the press arrives
            ASSERT_EQ(room.guests[dan].lobby->my_seat(), 3);
            room.guests[bob].lobby->take_chat();
            room.guests[dan].lobby->take_chat();
            room.guests[ann].client_end->send(screen.press(1, 3, false));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2 Dan@3");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && room.host.players() == 4u);
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty() && notices_of(*room.guests[dan].lobby).empty());
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom && room.host.leader() == 0);       // (no offence: the leader's press was right when it was made)
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Dan@1 Cat@2 Bob@3");
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 1u);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Black."}));
            ASSERT_EQ(notices_of(*room.guests[dan].lobby), (std::vector<std::string>{"Ann moved you to Red."}));
        }
        {   // a replacement: the player that the press meant has left and another has taken the color (the same state, another name): the press is ignored, so it is not the replacement that is moved; and a player of
            // the same name that comes back to the same color makes the same seating, which is what the guard says (who sits where): the press is heard
            Room room(keyed_server_config(44));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(300);
            Screen screen{room.host.room()};
            room.guests[bob].lobby->leave();
            room.run(300);
            const size_t eve = room.join_seat("Eve");
            ASSERT_EQ(room.guests[eve].lobby->my_seat(), 1);
            room.run(300);
            room.guests[eve].lobby->take_chat();
            room.guests[ann].client_end->send(screen.press(1, 3, false));                                                // meant Bob, who is gone: Eve sits in Red now
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Eve@1 Cat@2");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && notices_of(*room.guests[eve].lobby).empty());
            room.guests[eve].lobby->leave();                                                                             // Eve goes and a Bob comes: the leader's screen of before shows the same seats again
            room.run(300);
            const size_t again = room.join_seat("Bob");
            ASSERT_EQ(room.guests[again].lobby->my_seat(), 1);
            room.run(300);
            room.guests[ann].client_end->send(screen.press(1, 3));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@2 Bob@3");
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 1u);
        }
        {   // a guard that is not the seating's number is ignored whatever the seats say, and counted like every request that is not done (no offence until the sixteenth); the seating of the room is what the number is of:
            // the names and the states of the seats, and nothing else of the Room message (the thumbs change all the time)
            Room room(keyed_server_config(45));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(300);
            const uint32_t right = seating_hash(room.host.room());
            ASSERT_TRUE(right != 0u && seating_hash(room.guests[ann].lobby->room()) == right);                           // (every machine of the room computes the number that the room does)
            const uint32_t wrong = right ^ 0x5A5A5A5Au;
            ASSERT_TRUE(wrong != 0u && wrong != right);
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 2, wrong}));
            room.run(300);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && layout(room.host.room()) == "Ann@0 Bob@1");
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 2, right}));
            room.run(300);
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 1u && layout(room.host.room()) == "Ann@0 Bob@2");
            room.run(5000);                                                                                              // (the round trips are measured again and again)
            const uint32_t later = seating_hash(room.host.room());
            ASSERT_TRUE(later != right);                                                                                 // ... the seating changed with the move
            room.guests[ann].client_end->send(encode(SeatMoveMsg{2, 1, later}));
            room.run(300);
            ASSERT_TRUE(room.host.seat_moves() == 2u && layout(room.host.room()) == "Ann@0 Bob@1");
            for (uint32_t i = 1; i < kIgnoredSeatMovesAllowed; ++i) room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 2, wrong}));       // the sixteen that are free ...
            room.run(300);
            ASSERT_TRUE(room.host.ignored_seat_moves() == kIgnoredSeatMovesAllowed && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 2, wrong}));                                         // ... and the seventeenth is the first offence
            room.run(300);
            ASSERT_EQ(room.host.ignored_seat_moves(), kIgnoredSeatMovesAllowed + 1u);
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom && room.host.leader() == 0);
        }
        {   // a START that was heard before a swap, in the same pass: the colors that are free are the same colors after it, so its plan of bots is left as it is, and its sender, the leader, is where it sits now
            // (whether it is the one that the press names or the one that the press puts it with)
            using F = FillLevel;
            Room room(keyed_server_config(46));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            leader_plans(room.host);
            const auto ask = [&](F green, F red, F blue, F black) {
                StartRequestMsg request;
                request.fill = {green, red, blue, black};
                room.guests[ann].client_end->send(encode(request));
            };
            Screen screen{room.host.room()};
            const auto swap = [&](uint8_t from, uint8_t to) { room.guests[ann].client_end->send(screen.press(from, to)); };
            ask(F::None, F::Easy, F::Hard, F::None);                                                                     // levels for two colors that guests hold ...
            swap(1, 2);                                                                                                  // ... which change places: nobody takes or leaves a free color, the plan stays
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"0:0130"}));
            ask(F::None, F::None, F::Easy, F::Hard);                                                                     // the leader presses its own color as the first of the two: it goes to Red, and the START names Red
            swap(0, 1);
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"1:0013"}));
            ask(F::Medium, F::None, F::None, F::Hard);                                                                   // a guest presses the leader's color as the second: the leader goes to Blue, and the START names Blue
            swap(2, 1);
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"2:2003"}));
            ASSERT_EQ(layout(room.host.room()), "Cat@0 Bob@1 Ann@2");
            ASSERT_TRUE(room.host.leader() == 2 && room.guests[ann].lobby->is_leader() && room.host.seat_moves() == 3u && room.host.ignored_seat_moves() == 0u && room.host.ignored_start_requests() == 0u);
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Blue.", "Ann moved you to Red."}));
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Red.", "Ann moved you to Green."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
        }
        {   // the owner of a lobby that has no leader (a game on the local network) swaps two guests too: the rule is the lobby's, and the room says so in its own name
            Room lan;
            lan.join_seat("Ann");
            lan.join_seat("Bob");
            lan.run(300);
            ASSERT_EQ(lan.host.leader(), kNoLeader);
            const uint8_t ann_seat = lan.guests[0].lobby->my_seat();
            const uint8_t bob_seat = lan.guests[1].lobby->my_seat();
            ASSERT_TRUE(ann_seat == 1 && bob_seat == 2);                                                                 // (the host holds Green)
            for (auto& g : lan.guests) g.lobby->take_chat();
            ASSERT_TRUE(lan.host.move_seat(ann_seat, bob_seat));
            lan.run(300);
            ASSERT_TRUE(lan.guests[0].lobby->my_seat() == bob_seat && lan.guests[1].lobby->my_seat() == ann_seat);
            ASSERT_EQ(notices_of(*lan.guests[0].lobby), (std::vector<std::string>{"The room moved you to Blue."}));
            ASSERT_EQ(notices_of(*lan.guests[1].lobby), (std::vector<std::string>{"The room moved you to Red."}));
            ASSERT_EQ(lan.host.seat_moves(), 1u);
            ASSERT_TRUE(!lan.host.move_seat(0, 1) && !lan.host.move_seat(1, 0) && !lan.host.move_seat(0, 2) && lan.host.seat_moves() == 1u);   // the host's own color never moves, and nobody swaps with it
        }
        {   // a color that a bot holds stays where it is, from either side, whatever the guard says
            Room room(keyed_server_config(47));
            ASSERT_TRUE(room.host.add_bot(2, "Bot (Hard)"));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            const std::string before = layout(room.host.room());
            ASSERT_EQ(before, "Ann@0 Bob@1 Bot (Hard)@2 Cat@3");
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));                                                // a guest onto the bot's color
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 3));                                                // the bot onto a guest's color
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(3, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), before);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 3u);
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty() && notices_of(*room.guests[cat].lobby).empty() && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                                                // the guests of the room still swap
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bot (Hard)@2 Bob@3");
        }
    } TEST_END();

    TEST_CASE("N4.24 The Room Tells Its Own Rules (Protocol 15): The Teams And The Leader-Starts Rule That A Room Was Made With Are In Every Room Message, For Every Guest From Its Welcome On And After Every Change Of The Seats (A Join, A Color Move, A Leave); A Room Made Without Them Says None; A Pair That No Honest Client Sends Is No Team") {
        const auto rules_of = [](const ClientLobby& lobby) {          // "<teams> <rule bits>": "0+3 1" ("none" for free for all)
            const RoomMsg& r = lobby.room();
            const std::string teams = r.team_a == kNoTeam && r.team_b == kNoTeam ? "none" : std::to_string(static_cast<unsigned>(r.team_a)) + "+" + std::to_string(static_cast<unsigned>(r.team_b));
            return teams + " " + std::to_string(static_cast<unsigned>(r.flags));
        };
        {
            HostLobby::Config config = keyed_server_config(47);
            config.room_teams = sim::StartTeams{true, 0, 3};
            config.leader_starts = true;
            Room room(config);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            for (const size_t g : {ann, bob}) {
                ASSERT_EQ(rules_of(*room.guests[g].lobby), std::string("0+3 1"));
                ASSERT_TRUE(room.guests[g].lobby->room().leader_starts() && room.guests[g].lobby->room().teams() == sim::StartTeams({true, 0, 3}));
            }
            const size_t cat = room.join_seat("Cat");                                    // a join ...
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));                // ... a color move ...
            room.run(300);
            room.guests[bob].client_end->close();                                        // ... and a leave: the rules are the room's, no seat's
            room.run(300);
            for (const size_t g : {ann, cat}) ASSERT_EQ(rules_of(*room.guests[g].lobby), std::string("0+3 1"));
            ASSERT_TRUE(room.host.room().team_a == 0 && room.host.room().team_b == 3 && room.host.room().leader_starts());
            const size_t dan = room.join_seat("Dan");                                    // a guest that comes late hears them in its first Room message
            room.run(300);
            ASSERT_EQ(rules_of(*room.guests[dan].lobby), std::string("0+3 1"));
        }
        {   // a room made without the rules says none; so does a room that was given a pair no honest client sends (the lower seat first, two seats, 0 - 3) or nothing at all
            const std::vector<sim::StartTeams> given = {sim::StartTeams{}, sim::StartTeams{true, 2, 1}, sim::StartTeams{true, 1, 1}, sim::StartTeams{true, 0, 4}, sim::StartTeams{true, 3, 9}};
            for (const sim::StartTeams& teams : given) {
                HostLobby::Config config = keyed_server_config(48);
                config.room_teams = teams;
                Room room(config);
                const size_t ann = room.join_seat("Ann");
                room.run(300);
                ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::InRoom);
                ASSERT_EQ(room.guests[ann].lobby->room().slots[0].name, std::string("Ann"));        // (a Room message did arrive: "none" is what it says, not what an empty screen shows)
                ASSERT_TRUE(room.host.room().team_a == kNoTeam && room.host.room().team_b == kNoTeam && !room.host.room().teams().set);      // the room itself, before any decoder sees it
                ASSERT_EQ(rules_of(*room.guests[ann].lobby), std::string("none 0"));
                ASSERT_TRUE(!room.guests[ann].lobby->room().leader_starts() && !room.guests[ann].lobby->room().teams().set);
            }
        }
    } TEST_END();

    TEST_CASE("N4.25 The Platforms (Protocol 15): What A Guest Tells In Its Hello Is In The Room Message At Its Seat For Everybody; It Is Taken Anew When The Guest Takes Its Seat Back From Another Machine; The Start Carries The Platform Of Every Seat That Plays; A Host With A Seat Tells Its Own; A Byte Or A Create Block That No Server Reads Never Leaves A Client") {
        const auto platforms_of = [](const RoomMsg& r) {                  // "<seat 0> <seat 1> <seat 2> <seat 3>": the byte of each seat
            std::string out;
            for (const RoomMsg::Slot& s : r.slots) out += (out.empty() ? "" : " ") + std::to_string(static_cast<unsigned>(s.platform));
            return out;
        };
        const auto start_platforms_of = [](const StartMsg& s) {
            std::string out;
            for (const uint8_t p : s.platforms) out += (out.empty() ? "" : " ") + std::to_string(static_cast<unsigned>(p));
            return out;
        };
        const uint8_t mac_page = static_cast<uint8_t>(kPlatformBrowser | kOsMacos);                 // 18
        const uint8_t android_page = static_cast<uint8_t>(kPlatformBrowser | kOsAndroid);           // 20
        const auto config_of = [](const std::string& name, uint8_t platform) {
            ClientLobby::Config cc;
            cc.name = name;
            cc.platform = platform;
            return cc;
        };
        {   // the Hello's byte reaches the seat, in the room and in what every guest is shown; a guest that says nothing is 0
            Room room(keyed_server_config(50));
            const size_t ann = join_with(room, config_of("Ann", kOsWindows));
            room.run(100);
            const size_t bob = join_with(room, config_of("Bob", mac_page));
            room.run(100);
            const size_t cat = join_with(room, config_of("Cat", kPlatformUnknown));
            room.run(300);
            ASSERT_EQ(platforms_of(room.host.room()), std::string("1 18 0 0"));
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(platforms_of(room.guests[g].lobby->room()), std::string("1 18 0 0"));
            // Bob's page is reloaded on another machine: a Hello with its key takes the seat over, and what it says now is the seat's platform (and everybody is told)
            ClientLobby::Config again = config_of("Bob", kOsLinux);
            again.key = room.guests[bob].lobby->key();
            const size_t bob2 = join_with(room, again);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(platforms_of(room.host.room()), std::string("1 3 0 0"));
            for (const size_t g : {ann, cat, bob2}) ASSERT_EQ(platforms_of(room.guests[g].lobby->room()), std::string("1 3 0 0"));
            ClientLobby::Config silent = config_of("Bob", kPlatformUnknown);                         // ... and a machine that says nothing leaves the seat with none
            silent.key = room.guests[bob].lobby->key();
            const size_t bob3 = join_with(room, silent);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 2u);
            ASSERT_EQ(platforms_of(room.host.room()), std::string("1 0 0 0"));
            ASSERT_EQ(platforms_of(room.guests[ann].lobby->room()), std::string("1 0 0 0"));
            ASSERT_EQ(platforms_of(room.guests[bob3].lobby->room()), std::string("1 0 0 0"));
        }
        {   // the Start: the platform of each seat that plays, 0 for a seat that does not, and every guest is handed the same
            Room room(keyed_server_config(51));
            const size_t ann = join_with(room, config_of("Ann", kOsWindows));
            room.run(100);
            const size_t bob = join_with(room, config_of("Bob", mac_page));
            room.run(100);
            const size_t cat = join_with(room, config_of("Cat", android_page));
            room.run(300);
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(300);
            ASSERT_EQ(start_platforms_of(room.host.start_info()), std::string("1 18 20 0"));
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(start_platforms_of(room.guests[g].lobby->start_info()), std::string("1 18 20 0"));
        }
        {   // a seat that holds a bot, and an empty seat, say 0 in the Start; the people keep theirs
            Room room(keyed_server_config(52));
            const size_t ann = join_with(room, config_of("Ann", kOsLinux));
            room.run(100);
            join_with(room, config_of("Bob", android_page));
            room.run(300);
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            room.run(100);
            ASSERT_EQ(platforms_of(room.host.room()), std::string("3 20 0 0"));
            ASSERT_TRUE(room.host.start(7, 8, room.now));
            room.run(300);
            ASSERT_EQ(start_platforms_of(room.host.start_info()), std::string("3 20 0 0"));
            ASSERT_EQ(start_platforms_of(room.guests[ann].lobby->start_info()), std::string("3 20 0 0"));
        }
        {   // a host that holds a seat (a LAN or direct host) tells its own, and so does a host that told a byte that no one reads: none
            HostLobby::Config hc;
            hc.host_seat = 0;
            hc.host_name = "Hal";
            hc.host_platform = static_cast<uint8_t>(kPlatformBrowser | kOsLinux);
            Room room(hc);
            ASSERT_EQ(platforms_of(room.host.room()), std::string("19 0 0 0"));
            const size_t ann = join_with(room, config_of("Ann", kOsWindows));
            room.run(300);
            ASSERT_EQ(room.guests[ann].lobby->my_seat(), 1);
            ASSERT_EQ(platforms_of(room.guests[ann].lobby->room()), std::string("19 1 0 0"));
            ASSERT_TRUE(room.host.start(9, 10, room.now));
            room.run(300);
            ASSERT_EQ(start_platforms_of(room.guests[ann].lobby->start_info()), std::string("19 1 0 0"));
            HostLobby::Config bad = hc;
            bad.host_platform = 0xFF;
            ASSERT_EQ(platforms_of(Room(bad).host.room()), std::string("0 0 0 0"));
            bad.host_platform = kPlatformUnknown;
            ASSERT_EQ(platforms_of(Room(bad).host.room()), std::string("0 0 0 0"));
        }
        {   // a client leaves out a platform byte and a create block that no server would read: the Hello would be refused as garbage, and the guest would never be seated
            Room room(keyed_server_config(53));
            ClientLobby::Config odd = config_of("Ann", 0xFF);
            const size_t ann = join_with(room, odd);
            room.run(300);
            ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(platforms_of(room.guests[ann].lobby->room()), std::string("0 0 0 0"));
            CreateBlock block;
            block.map_name = "TINY.LVL";
            block.seats = 9;                                                                          // no such room
            ASSERT_FALSE(valid_create_block(block));
            ClientLobby::Config odd_block = config_of("Bob", kOsWindows);
            odd_block.create = block;
            const size_t bob = join_with(room, odd_block);
            room.run(300);
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(platforms_of(room.guests[bob].lobby->room()), std::string("0 1 0 0"));
            block.seats = 3;                                                                          // an honest block is sent, and a lobby that is no room for the public ignores it
            ASSERT_TRUE(valid_create_block(block));
            ClientLobby::Config honest = config_of("Cat", kOsMacos);
            honest.create = block;
            const size_t cat = join_with(room, honest);
            room.run(300);
            ASSERT_EQ(room.guests[cat].lobby->phase(), ClientLobby::Phase::InRoom);
        }
    } TEST_END();

    TEST_CASE("N4.26 The Lobby Room (Protocol 16), The Joiners: A Newcomer Takes The Color It Asked For When Nobody Holds It And The Plan Calls It Open, Else The Lowest Such Color; A Color That The Plan Gives To A Bot Or To Nobody Is Not Taken, And No Such Color Is Full; A Room Is A Lobby Room Only With What It Needs") {
        using K = PlanKind;
        {   // the plan decides who may sit where
            Room room(lobby_room_config(60));
            ASSERT_TRUE(room.host.lobby_room() && room.host.room().lobby() && room.host.room().leader_starts());
            const size_t ann = room.join_seat("Ann");
            ASSERT_EQ(room.guests[ann].lobby->my_seat(), 0);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && room.guests[ann].lobby->room().lobby());
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Easy, K::Nobody, K::Open})));
            room.run(100);
            ASSERT_EQ(plan_text(room.guests[ann].lobby->room()), std::string("oeno -"));
            const size_t bob = room.join_seat("Bob", 1);                           // it asks for the color of a bot: it gets the lowest one that is open
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 3);
            const size_t cat = room.join_seat("Cat");                              // no color is open any more
            ASSERT_EQ(room.guests[cat].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[cat].lobby->reject_reason(), RejectReason::Full);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@3"));
            // the leader opens every color: a wish is kept when the color is open, and is the lowest open color otherwise
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Open})));
            room.run(100);
            const size_t dan = room.join_seat("Dan", 2);
            ASSERT_EQ(room.guests[dan].lobby->my_seat(), 2);
            const size_t eve = room.join_seat("Eve", 3);                           // (Bob holds it)
            ASSERT_EQ(room.guests[eve].lobby->my_seat(), 1);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Eve@1 Dan@2 Bob@3"));
            const size_t fay = room.join_seat("Fay");                              // the room is full
            ASSERT_EQ(room.guests[fay].lobby->reject_reason(), RejectReason::Full);
            ASSERT_FALSE(room.guests[bob].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Open})));       // (a guest knows it does not lead: nothing is sent)
        }
        {   // a color that a person leaves goes back to the plan's kind: a bot's color is not taken by the next newcomer, an open one is
            Room room(lobby_room_config(61));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Medium, K::Open, K::Open})));        // Bob sits in a color that is a bot's underneath
            room.run(100);
            room.guests[bob].lobby->leave();
            room.run(100);
            const size_t dan = room.join_seat("Dan");
            ASSERT_EQ(room.guests[dan].lobby->my_seat(), 3);                       // seat 1 is a bot's; seat 2 is Cat's; the lowest open color is 3
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Cat@2 Dan@3"));
        }
        {   // a room that is no lobby room seats as ever: the first free color, and it has no plan to read
            HostLobby::Config hc = lobby_room_config(62);
            hc.lobby_room = false;
            Room room(hc);
            ASSERT_FALSE(room.host.lobby_room() || room.host.room().lobby());
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob", 3);
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 3);
            ASSERT_FALSE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Easy, K::Open, K::Open})));       // (the leader of a room that is no lobby room has nothing to plan)
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooo -"));
        }
        {   // what a lobby room needs: keys (a seat is held for a key), a server's host with a leader that starts, and all four colors; without any of them the room is none
            const auto is_lobby = [](const std::function<void(HostLobby::Config&)>& spoil) {
                HostLobby::Config hc = lobby_room_config(63);
                spoil(hc);
                const HostLobby host(hc);
                return host.lobby_room() || host.room().lobby();
            };
            ASSERT_TRUE(is_lobby([](HostLobby::Config&) {}));
            ASSERT_FALSE(is_lobby([](HostLobby::Config& hc) { hc.make_key = nullptr; }));
            ASSERT_FALSE(is_lobby([](HostLobby::Config& hc) { hc.leader_starts = false; }));
            ASSERT_FALSE(is_lobby([](HostLobby::Config& hc) { hc.early_start = false; }));
            ASSERT_FALSE(is_lobby([](HostLobby::Config& hc) { hc.host_seat = 0; }));
            ASSERT_FALSE(is_lobby([](HostLobby::Config& hc) { hc.max_players = 3; }));
        }
    } TEST_END();

    TEST_CASE("N4.27 The Plan (Protocol 16): Only The Leader's Plan Of An Open Lobby Room Is Heard, It Sets The Map The Server Offers, What Each Color Is And The Teams, And The Room Is Shown To Everybody When It Changed; The Others Are Ignored And Counted (16 Free, Then Offences), The Leader's Clicks Have A Budget, A Room That Is No Lobby Room Has No Plan") {
        using K = PlanKind;
        {   // what a plan sets
            Room room(lobby_room_config(64));
            room.host.set_map_choice([](const std::string& name) { return name == "SMALL.LVL" || name == "TINY.LVL" ? name : std::string(); });
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_EQ(room.host.map_name(), std::string("TINY.LVL"));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Hard, K::Nobody}, 0, 2, "SMALL.LVL")));
            room.run(100);
            ASSERT_EQ(room.host.map_name(), std::string("SMALL.LVL"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("oohn 0+2"));
            for (const size_t g : {ann, bob}) {
                ASSERT_EQ(plan_text(room.guests[g].lobby->room()), std::string("oohn 0+2"));
                ASSERT_EQ(room.guests[g].lobby->room().map_name, std::string("SMALL.LVL"));
            }
            ASSERT_EQ(room.host.plan_changes(), 1u);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Hard, K::Nobody}, 0, 2, "SMALL.LVL")));        // the same plan: nothing changed, nothing is shown
            room.run(100);
            ASSERT_EQ(room.host.plan_changes(), 1u);
            // a map that the server does not offer keeps the map (the rest of the plan counts), and so does a plan that names none
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Easy, K::Nobody}, 1, 2, "ELSEWHERE.LVL")));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("ooen 1+2"));
            ASSERT_EQ(room.host.map_name(), std::string("SMALL.LVL"));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Easy, K::Nobody})));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("ooen -"));
            ASSERT_EQ(room.host.map_name(), std::string("SMALL.LVL"));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Easy, K::Nobody}, kNoTeam, kNoTeam, "TINY.LVL")));
            room.run(100);
            ASSERT_EQ(room.guests[bob].lobby->room().map_name, std::string("TINY.LVL"));
            ASSERT_EQ(room.host.ignored_plans(), 0u);
        }
        {   // who is heard: a guest that does not lead is ignored and counted without offence up to 16 times, and each plan after those is a violation (eight throw the guest out)
            Room room(lobby_room_config(65));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const std::vector<uint8_t> wish = encode(plan_of({K::Easy, K::Easy, K::Easy, K::Easy}));
            for (uint32_t i = 0; i < kIgnoredPlansAllowed; ++i) room.guests[bob].client_end->send(wish);
            room.run(300);
            ASSERT_EQ(room.host.ignored_plans(), kIgnoredPlansAllowed);
            ASSERT_EQ(room.host.plan_changes(), 0u);
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooo -"));
            ASSERT_TRUE(room.host.occupied(bob_seat));
            for (int i = 0; i < 7; ++i) room.guests[bob].client_end->send(wish);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(bob_seat));                             // seven violations are not enough
            room.guests[bob].client_end->send(wish);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(bob_seat));                            // the eighth
            ASSERT_EQ(room.host.plan_changes(), 0u);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader());
        }
        {   // the leader's clicks have a budget (a burst of 6, then 4 a second): a plan beyond it is dropped, 12 more are tolerated, every one after that is a violation
            Room room(lobby_room_config(66));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const auto click = [&](unsigned i) { room.guests[ann].client_end->send(encode(plan_of({K::Open, i % 2 == 0 ? K::Easy : K::Medium, K::Open, K::Open}))); };
            for (unsigned i = 0; i < kPlanBurst + kPlanExcessBurst; ++i) click(i);      // (every batch is read in the pass after it: the budget refills 4 a second, so no pause between them)
            room.run(10);
            ASSERT_EQ(room.host.plan_changes(), kPlanBurst);                       // the first six were done: the last of them was a Medium (the sixth, i = 5)
            ASSERT_EQ(plan_text(room.host.room()), std::string("omoo -"));
            ASSERT_TRUE(room.host.occupied(ann_seat));
            for (unsigned i = 0; i < 7; ++i) click(i);                             // the excess is used up: each one is an offence
            room.run(10);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            click(0);
            room.run(10);
            ASSERT_FALSE(room.host.occupied(ann_seat));                            // the eighth throws the leader out, as it does for any guest
            ASSERT_EQ(room.host.leader(), room.guests[bob].lobby->my_seat());
            ASSERT_EQ(room.host.plan_changes(), kPlanBurst);
        }
        {   // a room that is no lobby room has no plan: the message is garbage there (eight of them throw the sender out), and a client does not send it
            HostLobby::Config hc = lobby_room_config(67);
            hc.lobby_room = false;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(encode(plan_of({K::Open, K::Easy, K::Open, K::Open})));
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(encode(plan_of({K::Open, K::Easy, K::Open, K::Open})));
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_EQ(room.host.plan_changes(), 0u);
            ASSERT_EQ(room.host.ignored_plans(), 0u);                              // (garbage is no plan)
        }
    } TEST_END();

    TEST_CASE("N4.28 A Color Move In A Lobby Room (Protocol 16): The Colors Are Exchanged Completely, What The Plan Calls Each One And The Team Pair Go With The Person, A Person Whose Seat Is Held Can Be Moved; A Room That Is No Lobby Room Moves Only The People") {
        using K = PlanKind;
        {
            Room room(lobby_room_config(68));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Hard, K::Open, K::Easy, K::Nobody}, 0, 2)));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("hoen 0+2"));
            // a swap with Bob: Ann's color, with its kind and its place in the pair, is Bob's now, and Bob's is Ann's
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(0, 1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Bob@0 Ann@1"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("ohen 1+2"));
            ASSERT_EQ(plan_text(room.guests[bob].lobby->room()), std::string("ohen 1+2"));
            // a move to a color that nobody holds and that is Nobody's: Ann takes its kind's place and the color she left takes hers
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Bob@0 Ann@3"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("oneh 2+3"));
            ASSERT_EQ(room.host.seat_moves(), 2u);
            // the pair is written with the lower color first, whatever the move
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(3, 2));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("onhe 2+3"));
            // back to the first color: the swap with Bob is complete there as well
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 0));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@2"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("hnoe 0+3"));
        }
        {   // a person whose seat is held is moved with all that is its own: the hold, and the key that takes the seat back
            Room room(lobby_room_config(69));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(1));
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(100);
            ASSERT_TRUE(room.host.held(3) && !room.host.held(1) && !room.host.occupied(1));
            const size_t bob2 = join_keyed(room, "Bob", bob_key);
            room.run(300);
            ASSERT_EQ(room.guests[bob2].lobby->my_seat(), 3);
            ASSERT_TRUE(room.host.in_game(3) && !room.host.held(3));
            ASSERT_EQ(room.host.takeovers(), 1u);
        }
        {   // a room that is no lobby room: the people change places, and nothing else moves (the room's own teams are the owner's)
            HostLobby::Config hc = lobby_room_config(70);
            hc.lobby_room = false;
            hc.room_teams = sim::StartTeams{true, 0, 2};
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(0, 1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Bob@0 Ann@1"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooo 0+2"));
        }
    } TEST_END();

    TEST_CASE("N4.29 The Hold (Protocol 16): A Guest Whose Connection Ends Keeps Its Seat For A Minute (It Counts As Present, A Held Leader Still Leads, Nobody Takes The Color, Its Key Takes The Seat Back); Leave And A Kick Remove It At Once, And A Guest That Was Given No Key Cannot Be Held") {
        {
            Room room(lobby_room_config(71));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const SeatKey ann_key = room.guests[ann].lobby->key();
            ASSERT_FALSE(key_is_zero(ann_key));
            ASSERT_EQ(games_text(room.host.room()), std::string("1110"));
            room.guests[ann].client_end->close();                                  // the leader's link ends
            room.run(100);
            ASSERT_TRUE(room.host.held(0) && room.host.occupied(0) && !room.host.in_game(0));
            ASSERT_EQ(room.host.holds(), 1u);
            ASSERT_EQ(room.host.leader(), 0);                                      // a held leader leads still
            ASSERT_EQ(room.host.players(), 3u);
            ASSERT_EQ(games_text(room.host.room()), std::string("0110"));
            ASSERT_EQ(games_text(room.guests[bob].lobby->room()), std::string("0110"));
            ASSERT_EQ(room.guests[bob].lobby->room().leader, 0);
            const size_t dan = room.join_seat("Dan", 0);                           // the color is not free for a newcomer
            ASSERT_EQ(room.guests[dan].lobby->my_seat(), 3);
            const size_t eve = room.join_seat("Eve");
            ASSERT_EQ(room.guests[eve].lobby->reject_reason(), RejectReason::Full);
            // Ann comes back (a page that went to the game): the same seat, the lead, her key
            const size_t ann2 = join_keyed(room, "Ann", ann_key);
            room.run(300);
            ASSERT_EQ(room.guests[ann2].lobby->my_seat(), 0);
            ASSERT_TRUE(room.guests[ann2].lobby->is_leader() && !room.guests[ann2].lobby->rejoined());
            ASSERT_TRUE(!room.host.held(0) && room.host.in_game(0));
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(games_text(room.guests[cat].lobby->room()), std::string("1111"));
            // Leave is the guest's own word: the seat goes at once and is not held
            room.guests[cat].lobby->leave();
            room.run(100);
            ASSERT_FALSE(room.host.occupied(2));
            ASSERT_EQ(room.host.holds(), 1u);
            // a kick removes a held seat as well
            room.guests[dan].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(3));
            ASSERT_EQ(room.host.holds(), 2u);
            room.host.kick(3);
            room.run(100);
            ASSERT_FALSE(room.host.occupied(3));
            // the hold of a seat runs out after hold_ms: not before
            room.guests[bob].client_end->close();
            room.run(58000);
            ASSERT_TRUE(room.host.held(1));
            ASSERT_EQ(room.host.hold_expiries(), 0u);
            room.run(3000);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.hold_expiries(), 1u);
            ASSERT_EQ(room.host.players(), 1u);
            const size_t fay = room.join_seat("Fay");                              // the color is free again
            ASSERT_EQ(room.guests[fay].lobby->my_seat(), 1);
        }
        {   // the leader that stays away for the hold's time is replaced by the guest who was welcomed next
            Room room(lobby_room_config(72));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            room.guests[ann].client_end->close();
            room.run(61000);
            ASSERT_FALSE(room.host.occupied(0));
            ASSERT_EQ(room.host.leader(), room.guests[bob].lobby->my_seat());
            ASSERT_TRUE(room.guests[bob].lobby->is_leader());
            ASSERT_EQ(room.host.hold_expiries(), 1u);
        }
        {   // a guest that was given no key cannot come back: its seat goes at once and is not held
            HostLobby::Config hc = lobby_room_config(73);
            const auto calls = std::make_shared<int>(0);
            const std::function<bool(SeatKey&)> maker = hc.make_key;
            hc.make_key = [calls, maker](SeatKey& key) { return ++*calls <= 1 && maker(key); };       // (the first guest has a key, the others none)
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(key_is_zero(room.guests[bob].lobby->key()));
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.holds(), 0u);
        }
        {   // a match that is loading is cancelled for a guest that goes, as in every room: the seat is not held
            Room room(lobby_room_config(74));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.holds(), 0u);
        }
    } TEST_END();

    TEST_CASE("N4.30 The Leader's START In A Lobby Room (Protocol 16): It Stands Until Every Person Is A Game And Is Then Given To The Owner In Every Pass, Nobody New Is Taken And Nothing Moves While It Stands, It Ends With A Notice When A Game Does Not Come, And Silently When The Leader Goes Or The Room Cannot Start Any More") {
        using K = PlanKind;
        {   // the games are in: the owner is given the request in every pass, with the plan's levels and teams, until it starts the match
            Room room(lobby_room_config(75));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Easy, K::Hard}, 0, 2)));
            room.run(100);
            room.host.take_events();
            ASSERT_TRUE(room.guests[ann].lobby->request_start());                  // (the request carries no levels: the plan has them)
            room.run(100);
            ASSERT_TRUE(room.host.starting() && room.guests[ann].lobby->room().starting() && room.guests[bob].lobby->room().starting());
            const std::vector<HostLobby::Event> pending = room.host.take_events();
            ASSERT_FALSE(pending.empty());
            for (const HostLobby::Event& e : pending) {
                ASSERT_TRUE(e.type == HostLobby::Event::Type::LeaderStart && e.seat == 0);
                ASSERT_TRUE(e.fill[0] == FillLevel::None && e.fill[1] == FillLevel::None && e.fill[2] == FillLevel::Easy && e.fill[3] == FillLevel::Hard);
                ASSERT_TRUE(e.teams.set && e.teams.a == 0 && e.teams.b == 2);
            }
            room.run(10);
            ASSERT_EQ(leader_starts(room.host).size(), size_t{1});                 // again, in the next pass ...
            room.run(10);
            ASSERT_EQ(leader_starts(room.host).size(), size_t{1});                 // ... and in the one after it
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            ASSERT_FALSE(room.host.starting() || room.host.room().starting());
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_TRUE(leader_starts(room.host).empty());
        }
        {   // a page is no game: the request waits, the owner is not asked, and it cannot start the match without the page's game
            HostLobby::Config hc = lobby_room_config(76);
            hc.start_wait_ms = 5000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_EQ(room.guests[pia].lobby->my_seat(), 1);
            ASSERT_EQ(games_text(room.host.room()), std::string("1000"));
            const SeatKey pia_key = room.guests[pia].lobby->key();
            room.host.take_events();
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_FALSE(room.host.start(5, 6, room.now));
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Room);
            // while it stands: no newcomer, no color moves, no new plan; a second START is counted
            const size_t dan = room.join_seat("Dan");
            ASSERT_EQ(room.guests[dan].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[dan].lobby->reject_reason(), RejectReason::MatchRunning);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(0, 2));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Easy, K::Open})));
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Pia@1"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooo -"));
            ASSERT_EQ(room.host.ignored_seat_moves(), 1u);
            ASSERT_EQ(room.host.ignored_plans(), 1u);
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_TRUE(room.host.starting());
            // the page goes to the game page: its link ends (the seat is held, the wait goes on), and the game says Hello with the key
            room.guests[pia].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(1) && room.host.starting());
            ASSERT_TRUE(leader_starts(room.host).empty());
            const size_t pia_game = join_keyed(room, "Pia", pia_key);
            room.run(100);
            ASSERT_TRUE(room.host.in_game(1) && !room.host.held(1));
            ASSERT_FALSE(leader_starts(room.host).empty());
            ASSERT_EQ(games_text(room.guests[ann].lobby->room()), std::string("1100"));
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.guests[pia_game].lobby->phase(), ClientLobby::Phase::Loading);
        }
        {   // a game that does not come in time: the request ends, the leader alone is told which games are missing
            HostLobby::Config hc = lobby_room_config(77);
            hc.start_wait_ms = 5000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(4500);
            ASSERT_TRUE(room.host.starting());
            room.run(1000);
            ASSERT_FALSE(room.host.starting());
            ASSERT_FALSE(room.guests[ann].lobby->room().starting() || room.guests[pia].lobby->room().starting());
            std::vector<std::string> told = notices_of(*room.guests[ann].lobby);
            ASSERT_TRUE(told.size() == 1 && told[0] == "Pia's game did not come in time.");
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty() && notices_of(*room.guests[pia].lobby).empty());
            // two pages: one notice naming both; the leader may ask again, and the room takes newcomers again
            const size_t pam = join_with(room, page_config("Pam"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(5500);
            ASSERT_FALSE(room.host.starting());
            told = notices_of(*room.guests[ann].lobby);
            ASSERT_TRUE(told.size() == 1 && told[0] == "These games did not come in time: Pia and Pam.");
            (void)pam;
        }
        {   // a seat that is held does not run out while the request waits; its hold begins again when the request ends
            Room room(lobby_room_config(78));
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            room.guests[pia].client_end->close();
            room.run(80000);
            ASSERT_TRUE(room.host.starting() && room.host.held(1));                // (a minute and a half is the request's time, the hold's is a minute)
            room.run(11000);
            ASSERT_FALSE(room.host.starting());
            ASSERT_TRUE(room.host.held(1));
            const std::vector<std::string> told = notices_of(*room.guests[ann].lobby);
            ASSERT_TRUE(told.size() == 1 && told[0] == "Pia's game did not come in time.");
            room.run(50000);
            ASSERT_TRUE(room.host.held(1));
            room.run(11000);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.hold_expiries(), 1u);
        }
        {   // the request ends without a word when its leader does not lead any more, and when the room could not start with whom it has left
            Room room(lobby_room_config(79));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            room.guests[ann].lobby->leave();                                       // the leader goes: the request was hers
            room.run(100);
            ASSERT_FALSE(room.host.starting() || room.guests[bob].lobby->room().starting());
            ASSERT_TRUE(room.guests[bob].lobby->is_leader());
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty());
            ASSERT_TRUE(room.guests[bob].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            room.guests[pia].lobby->leave();                                       // two persons could start, one cannot
            room.run(100);
            ASSERT_EQ(room.host.players(), 1u);
            ASSERT_FALSE(room.host.starting() || room.guests[bob].lobby->room().starting());
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty());
        }
        {   // a request that the room cannot honour is ignored and counted (it does not start the wait); with a bot in the plan one person is enough
            Room room(lobby_room_config(80));
            const size_t ann = room.join_seat("Ann");
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_FALSE(room.host.starting());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Medium, K::Open, K::Open})));
            room.run(100);
            room.host.take_events();
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(10);
            room.run(10);
            ASSERT_TRUE(room.host.starting());
            ASSERT_EQ(leader_plans(room.host).back(), std::string("0:0200"));
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
        }
        {   // the owner drops the request (it cannot honour it): the leader is told, nobody else, and the request may be made again; a drop when nothing waits does nothing
            Room room(lobby_room_config(81));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.host.end_start("Nothing waits.");
            room.run(100);
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            room.host.end_start(kNoticeNoPlace);
            room.run(100);
            ASSERT_FALSE(room.host.starting() || room.guests[bob].lobby->room().starting());
            const std::vector<std::string> told = notices_of(*room.guests[ann].lobby);
            ASSERT_TRUE(told.size() == 1 && told[0] == kNoticeNoPlace);
            ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty());
            room.host.end_start("Nothing waits.");
            room.run(100);
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
        }
        {   // only the leader's START is heard: anybody else's (a client that is not the game's sends it anyway) waits for nothing, is ignored and counted, and no request is given to the owner
            Room room(lobby_room_config(190));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(100);
            room.host.take_events();
            room.guests[bob].client_end->send(encode(StartRequestMsg{}));
            room.run(100);
            ASSERT_FALSE(room.host.starting() || room.guests[ann].lobby->room().starting() || room.guests[bob].lobby->room().starting());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_TRUE(room.guests[ann].lobby->request_start());                  // (the leader's own works as ever)
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
        }
    } TEST_END();

    TEST_CASE("N4.31 What A Hello Is (Protocol 16): A Page Is Seated In A Lobby Room Only, And Is No Game There; The Game That Takes The Seat Back Is One; The Door Tells The Lobby That The Hello Made The Room, And The Welcome Of The New Seat Says So") {
        {   // a room that is no lobby room refuses a page; a lobby room seats it, and the room shows who holds a game
            HostLobby::Config plain = lobby_room_config(82);
            plain.lobby_room = false;
            Room room(plain);
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_EQ(room.guests[pia].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[pia].lobby->reject_reason(), RejectReason::BadRequest);
            ASSERT_EQ(room.host.players(), 0u);
        }
        {
            Room room(lobby_room_config(83));
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            const size_t bob = room.join_seat("Bob");
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Pia@1 Bob@2"));
            ASSERT_EQ(games_text(room.host.room()), std::string("1010"));
            ASSERT_EQ(games_text(room.guests[bob].lobby->room()), std::string("1010"));
            ASSERT_TRUE(room.guests[pia].lobby->room().seat_in_game(0) && !room.guests[pia].lobby->room().seat_in_game(1));
            // the page is reloaded as a game with the same key: the seat is the same and holds a game now; a game that goes back to a page is a page again
            const SeatKey pia_key = room.guests[pia].lobby->key();
            room.guests[pia].client_end->close();
            room.run(100);
            const size_t game = join_keyed(room, "Pia", pia_key);
            room.run(300);
            ASSERT_EQ(games_text(room.host.room()), std::string("1110"));
            room.guests[game].client_end->close();
            room.run(100);
            ClientLobby::Config again = page_config("Pia");
            again.key = pia_key;
            const size_t page = join_with(room, again);
            room.run(300);
            ASSERT_EQ(room.guests[page].lobby->my_seat(), 1);
            ASSERT_EQ(games_text(room.host.room()), std::string("1010"));
            ASSERT_TRUE(room.guests[ann].lobby->is_leader());
        }
        {   // the door: the Hello that made the room is handed over with the flag, and the Welcome of the seat has it (a seat has a key, or the flag is not set)
            Room room(lobby_room_config(84));
            const auto welcome_of = [](Connection* end) {
                WelcomeMsg w;
                for (const std::vector<uint8_t>& m : drain_messages(end)) {
                    if (peek_type(m) == MsgType::Welcome && decode(m, w)) return w;
                }
                w.player = 255;
                return w;
            };
            HelloMsg hello;
            hello.name = "Ann";
            auto first = room.net.connect({10, 0});
            room.host.add_connection(first.first, room.now, "", encode(hello), true);
            room.run(100);
            const WelcomeMsg created = welcome_of(first.second);
            ASSERT_EQ(created.player, 0);
            ASSERT_TRUE(created.flags == kWelcomeCreated && !key_is_zero(created.key));
            hello.name = "Bob";
            auto second = room.net.connect({10, 0});
            room.host.add_connection(second.first, room.now, "", encode(hello), false);        // a Hello that found the room
            room.run(100);
            const WelcomeMsg joined = welcome_of(second.second);
            ASSERT_EQ(joined.player, 1);
            ASSERT_EQ(joined.flags, 0);
            hello.name = "Ann";                                                                // a Hello that takes a seat over is no creator, whatever the door says
            hello.key = created.key;
            auto third = room.net.connect({10, 0});
            room.host.add_connection(third.first, room.now, "", encode(hello), true);
            room.run(100);
            const WelcomeMsg back = welcome_of(third.second);
            ASSERT_EQ(back.player, 0);
            ASSERT_EQ(back.flags, 0);
            ASSERT_EQ(room.host.takeovers(), 1u);
        }
        {   // a room that gives no key cannot say it: the flag needs a key
            HostLobby::Config hc = lobby_room_config(85);
            hc.make_key = [](SeatKey&) { return false; };
            Room room(hc);
            HelloMsg hello;
            hello.name = "Ann";
            auto ends = room.net.connect({10, 0});
            room.host.add_connection(ends.first, room.now, "", encode(hello), true);
            room.run(100);
            bool flagged = false;
            for (const std::vector<uint8_t>& m : drain_messages(ends.second)) {
                WelcomeMsg w;
                if (peek_type(m) == MsgType::Welcome && decode(m, w)) flagged = flagged || w.flags != 0;
            }
            ASSERT_FALSE(flagged);
        }
        {   // the client end: a lobby that was welcomed by a Hello that made the room knows it (created()), a lobby that joined does not
            Room room(lobby_room_config(86));
            auto ends = room.net.connect({10, 0});
            room.guests.emplace_back();
            Room::Guest& g = room.guests.back();
            g.host_end = ends.first;
            g.client_end = ends.second;
            ClientLobby::Config cc;
            cc.name = "Ann";
            g.lobby = std::make_unique<ClientLobby>(ends.second, cc);
            room.run(60);                                                                      // (the lobby says its Hello; the server's door reads it and hands it over)
            std::vector<uint8_t> hello;
            ASSERT_TRUE(ends.first->poll(hello));
            room.host.add_connection(ends.first, room.now, "", hello, true);
            room.run(100);
            ASSERT_EQ(room.guests[0].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_TRUE(room.guests[0].lobby->created());
            const size_t bob = room.join_seat("Bob");
            ASSERT_FALSE(room.guests[bob].lobby->created());
            ClientLobby::Config page = page_config("Pia");
            ASSERT_EQ(page.client_kind, kClientPage);
            ASSERT_EQ(ClientLobby::Config{}.client_kind, kClientGame);
        }
    } TEST_END();

    TEST_CASE("N4.32 The Silence (Protocol 16): A Link Of A Lobby Room From Which Nothing Has Come For silence_ms Is Closed And Its Seat Is Held Like Any Other Link That Ends, A Client That Answers The Room's Pings Is Never Closed, And A Room That Is No Lobby Room, Or Is Loading, Does Not Close A Quiet Link") {
        {   // a page whose machine went to sleep: its link is open and says nothing; the room closes it after 30 s and holds the seat for the key
            Room room(lobby_room_config(90));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            room.guests[bob].frozen = true;
            room.run(29000);
            ASSERT_TRUE(room.host.in_game(1));
            ASSERT_EQ(room.host.silences(), 0u);
            room.run(2000);
            ASSERT_EQ(room.host.silences(), 1u);
            ASSERT_TRUE(room.host.held(1) && room.host.occupied(1));
            ASSERT_EQ(room.host.holds(), 1u);
            ASSERT_FALSE(room.guests[bob].host_end->is_open());
            ASSERT_EQ(room.host.leader(), 0);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader());
            const size_t bob2 = join_keyed(room, "Bob", bob_key);                   // the machine wakes up and comes back with the key
            room.run(300);
            ASSERT_EQ(room.guests[bob2].lobby->my_seat(), 1);
            ASSERT_TRUE(room.host.in_game(1) && !room.host.held(1));
            ASSERT_EQ(room.host.takeovers(), 1u);
        }
        {   // a client that answers the pings is never quiet: five minutes in the room, nobody is closed or held
            Room room(lobby_room_config(91));
            room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(300000);
            ASSERT_EQ(room.host.silences(), 0u);
            ASSERT_EQ(room.host.holds(), 0u);
            ASSERT_EQ(room.host.players(), 2u);
        }
        {   // the time is the room's setting; a seat that was given no key has nothing to wait for and goes at once
            HostLobby::Config hc = lobby_room_config(92);
            hc.silence_ms = 5000;
            const auto calls = std::make_shared<int>(0);
            const std::function<bool(SeatKey&)> maker = hc.make_key;
            hc.make_key = [calls, maker](SeatKey& key) { return ++*calls <= 1 && maker(key); };       // (the first guest has a key, the others none)
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(key_is_zero(room.guests[bob].lobby->key()));
            room.guests[bob].frozen = true;
            room.run(4000);
            ASSERT_TRUE(room.host.occupied(1));
            room.run(2000);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.silences(), 1u);
            ASSERT_EQ(room.host.holds(), 0u);
        }
        {   // a setting below what a live client needs (it answers a ping a second) is lifted: a room is never configured to close every link
            HostLobby::Config hc = lobby_room_config(93);
            hc.silence_ms = 0;
            Room room(hc);
            room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(60000);
            ASSERT_EQ(room.host.silences(), 0u);
            ASSERT_EQ(room.host.players(), 2u);
        }
        {   // a room that is no lobby room never closes a quiet link
            Room room(keyed_server_config(94));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.guests[bob].frozen = true;
            room.run(120000);
            ASSERT_EQ(room.host.silences(), 0u);
            ASSERT_TRUE(room.host.occupied(1) && room.guests[bob].host_end->is_open());
        }
        {   // while a match loads, a quiet link is the load timeout's business
            Room room(lobby_room_config(95));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.guests[bob].frozen = true;
            room.run(40000);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_EQ(room.host.silences(), 0u);
        }
    } TEST_END();

    TEST_CASE("N4.33 The Name (Protocol 16): Any Person Of An Open Lobby Room Can Change The Name They Go By, The Room Is Shown To Everybody When It Changed, A Name That Looks Like A Bot's Is Not Taken, The Name Stays With The Seat When Its Key Takes It Back, And It Is Part Of What A Color Move's Guard Is Made Of; A Room Whose START Waits Ignores It (Counted, No Offence At First), It Has A Budget Of Its Own, And A Room That Is No Lobby Room Takes It For Garbage") {
        {   // what a rename sets: the person's own name, for the whole room; the leader and any other person alike
            Room room(lobby_room_config(80));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const uint8_t cat_seat = room.guests[cat].lobby->my_seat();
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Robert"));
            room.run(100);
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Robert"));
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(room.guests[g].lobby->room().slots[bob_seat].name, std::string("Robert"));
            ASSERT_EQ(room.host.renames(), 1u);
            ASSERT_TRUE(room.guests[ann].lobby->request_name("Annie Lee"));       // the leader too (a space inside a name is a name)
            room.run(100);
            ASSERT_EQ(room.guests[cat].lobby->room().slots[ann_seat].name, std::string("Annie Lee"));
            ASSERT_EQ(room.host.renames(), 2u);
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Robert"));          // the name they have already: nothing changed, nothing is shown, nothing is ignored
            room.run(100);
            ASSERT_EQ(room.host.renames(), 2u);
            ASSERT_EQ(room.host.ignored_names(), 0u);
            // a client cleans a name as the Hello's encoder does (the characters that travel, no space at an end) and sends nothing for one that is empty
            ASSERT_FALSE(room.guests[cat].lobby->request_name(""));
            ASSERT_FALSE(room.guests[cat].lobby->request_name("   "));
            ASSERT_FALSE(room.guests[cat].lobby->request_name("\t\n"));
            ASSERT_TRUE(room.guests[cat].lobby->request_name("  Cat\tWalks  "));
            room.run(100);
            ASSERT_EQ(room.host.room().slots[cat_seat].name, std::string("CatWalks"));
            ASSERT_TRUE(room.guests[cat].lobby->request_name(std::string(40, 'z')));                   // a longer name is cut to what a name may be
            room.run(100);
            ASSERT_EQ(room.host.room().slots[cat_seat].name, std::string(kMaxNameChars, 'z'));
            ASSERT_EQ(room.host.renames(), 4u);
            ASSERT_EQ(room.host.room().slots[ann_seat].name, std::string("Annie Lee"));              // (nobody else's name moved)
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Robert"));
            ASSERT_EQ(room.host.ignored_names(), 0u);
        }
        {   // a name that looks like a bot's is not taken (a person is never shown as a bot): the person keeps the name they have, and no offence is made of it
            Room room(lobby_room_config(81));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            for (const char* wish : {"Bot (Hard)", "bot(x)", "B o t (x)", "BOT(Easy)"}) {
                ASSERT_FALSE(room.guests[bob].lobby->request_name(wish));                         // (protocol 20: the client does not even send it; the room's own rule is the second line, N4.40 sends the raw Hello)
                room.run(1100);                                                                   // (a second apart: the refused ones took from the budget too, and it must not be the budget that refuses the later ones)
                ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Bob"));
            }
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_EQ(room.host.ignored_names(), 0u);
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.guests[ann].lobby->is_leader());
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Botanist"));                           // (only the mark of a bot is refused, not a name that begins with the same letters)
            room.run(100);
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Botanist"));
        }
        {   // the name stays with the seat: when the person's connection ends and their key takes the seat back (a page that goes to the game page), the room still calls them what they chose
            Room room(lobby_room_config(82));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const SeatKey bob_key = room.guests[bob].lobby->key();
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Robert"));
            room.run(100);
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(bob_seat));
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Robert"));
            const size_t bob2 = join_keyed(room, "Player", bob_key);                                 // (a game that knows no name: the seat has one)
            room.run(300);
            ASSERT_EQ(room.guests[bob2].lobby->my_seat(), bob_seat);
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Robert"));
            ASSERT_EQ(room.guests[ann].lobby->room().slots[bob_seat].name, std::string("Robert"));
            ASSERT_EQ(room.host.takeovers(), 1u);
        }
        {   // a rename changes the seating that a color move's guard is made of: a move that the leader made for the room as it was is ignored (counted), one made for the room as it shows now is done
            Room room(lobby_room_config(83));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint32_t stale = seating_hash(room.guests[ann].lobby->room());
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Robert"));
            room.run(100);
            ASSERT_TRUE(seating_hash(room.guests[ann].lobby->room()) != stale);
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 2, stale}));
            room.run(100);
            ASSERT_EQ(room.host.ignored_seat_moves(), 1u);
            ASSERT_EQ(room.host.seat_moves(), 0u);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(100);
            ASSERT_EQ(room.host.seat_moves(), 1u);
            ASSERT_EQ(room.host.room().slots[2].name, std::string("Robert"));                        // (the name went with the person)
        }
        {   // while the leader's START waits for the games, nobody renames: the request is ignored and counted, free up to kIgnoredNamesAllowed times, and each one after those is a violation (eight throw the person out)
            HostLobby::Config hc = lobby_room_config(84);
            hc.start_wait_ms = 20000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            const uint8_t pia_seat = room.guests[pia].lobby->my_seat();
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_TRUE(room.guests[pia].lobby->request_name("Pia Two"));                            // (the page does not know yet: it sends)
            room.run(100);
            ASSERT_EQ(room.host.ignored_names(), 1u);
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_EQ(room.host.room().slots[pia_seat].name, std::string("Pia"));
            const std::vector<uint8_t> wish = encode(NameMsg{"Pia Two"});
            for (uint32_t i = 1; i < kIgnoredNamesAllowed; ++i) room.guests[pia].client_end->send(wish);
            room.run(300);
            ASSERT_EQ(room.host.ignored_names(), kIgnoredNamesAllowed);
            ASSERT_TRUE(room.host.occupied(pia_seat));
            for (int i = 0; i < 7; ++i) room.guests[pia].client_end->send(wish);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(pia_seat));                             // seven violations are not enough
            room.guests[pia].client_end->send(wish);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(pia_seat));                            // the eighth
            ASSERT_EQ(room.host.renames(), 0u);
        }
        {   // a person's renames have a budget (a burst of 3, then 1 a second): one beyond it is dropped, 6 more are tolerated, every one after that is a violation
            Room room(lobby_room_config(85));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const auto type = [&](unsigned i) { room.guests[bob].client_end->send(encode(NameMsg{i % 2 == 0 ? "Bob A" : "Bob B"})); };
            for (unsigned i = 0; i < kNameBurst + kNameExcessBurst; ++i) type(i);      // (every batch is read in the pass after it: the budget refills a name a second, so no pause between them)
            room.run(10);
            ASSERT_EQ(room.host.renames(), kNameBurst);                            // the first three were done: the last of them was "Bob A" (the third, i = 2)
            ASSERT_EQ(room.host.room().slots[bob_seat].name, std::string("Bob A"));
            ASSERT_TRUE(room.host.occupied(bob_seat));
            for (unsigned i = 0; i < 7; ++i) type(i);                              // the excess is used up: each one is an offence
            room.run(10);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            type(0);
            room.run(10);
            ASSERT_FALSE(room.host.occupied(bob_seat));                            // the eighth throws the person out, as it does for any guest
            ASSERT_EQ(room.host.renames(), kNameBurst);
        }
        {   // a room that is no lobby room has no renames: the message is garbage there (eight of them throw the sender out), and a client does not send it
            HostLobby::Config hc = lobby_room_config(86);
            hc.lobby_room = false;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            ASSERT_FALSE(room.guests[ann].lobby->request_name("Annie"));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(encode(NameMsg{"Annie"}));
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(encode(NameMsg{"Annie"}));
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_EQ(room.host.ignored_names(), 0u);                              // (garbage is no rename)
        }
        {   // a match that loads or runs hears no rename either: the client does not send one, and a stray one is counted without offence
            Room room(lobby_room_config(87));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_FALSE(room.guests[bob].lobby->request_name("Robert"));
            room.guests[bob].client_end->send(encode(NameMsg{"Robert"}));
            room.run(100);
            ASSERT_EQ(room.host.ignored_names(), 1u);
            ASSERT_EQ(room.host.renames(), 0u);
            ASSERT_EQ(room.host.room().slots[room.guests[bob].lobby->my_seat()].name, std::string("Bob"));
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
        }
    } TEST_END();

    TEST_CASE("N4.34 Forgiveness (Protocol 16): A Lobby Room Lives As Long As Its People Stay, So Every Minute A Guest Is Forgiven One Violation And One Ignored Request Of Each Kind (START, Color Move, Plan, Rename, Removal); What Is Forgiven Makes Room In The Free Allowance Again, A Flood That Is Faster Than That Is Thrown Out As Ever, And A Room That Is No Lobby Room Forgives Nothing") {
        ASSERT_EQ(HostLobby::Config{}.forgive_ms, kLobbyForgiveMs);
        ASSERT_EQ(kLobbyForgiveMs, 60000u);
        // `wish` is a message that this guest may send but the room ignores (counted, free up to 16, then a violation each): the room has no use for it from this sender now
        const auto send = [](Room& room, size_t who, const std::vector<uint8_t>& wish, unsigned times) {
            for (unsigned i = 0; i < times; ++i) room.guests[who].client_end->send(wish);
            room.run(10);
        };
        const auto case_of = [&send](const char* what, Room& room, size_t who, const std::vector<uint8_t>& wish, bool forgives) {
            const uint8_t seat = room.guests[who].lobby->my_seat();
            send(room, who, wish, 16);                                                        // the free allowance, used up
            send(room, who, wish, 3);                                                         // three violations
            ASSERT_MSG(room.host.occupied(seat), what);
            room.run(3 * 60000);                                                              // three minutes: three of each count are forgiven (the fourth is 59.9 s away)
            send(room, who, wish, 5);                                                         // a lobby room: the counts are 16 and 0 again, so these are five violations. Another room: 19 and 3, and the fifth of these is the eighth.
            ASSERT_MSG(room.host.occupied(seat) == forgives, what);
            if (forgives) {
                send(room, who, wish, 2);                                                     // seven violations
                ASSERT_MSG(room.host.occupied(seat), what);
                send(room, who, wish, 1);                                                     // the eighth
                ASSERT_MSG(!room.host.occupied(seat), what);
            }
        };
        // The count of ignored requests of a kind comes down too, not only the violations: one past the free allowance is one violation, and two (or three) minutes later it is gone and the count stands at 15 (or 14), so the
        // first of eight more is free (and the second) and the guest is not thrown out. With the count left at 17 these would be eight violations. Eleven in all end it, whoever it is: forgiveness is not immunity.
        const auto allowance_of = [&send](const char* what, Room& room, size_t who, const std::vector<uint8_t>& wish) {
            const uint8_t seat = room.guests[who].lobby->my_seat();
            send(room, who, wish, 16);
            send(room, who, wish, 1);
            ASSERT_MSG(room.host.occupied(seat), what);
            room.run(130 * 1000);
            send(room, who, wish, 8);
            ASSERT_MSG(room.host.occupied(seat), what);
            send(room, who, wish, 3);
            ASSERT_MSG(!room.host.occupied(seat), what);
        };
        const std::vector<uint8_t> start_wish = encode(StartRequestMsg{});
        const std::vector<uint8_t> plan_wish = encode(PlanMsg{});
        {   // a guest that is not the leader: its START, its color move and its plan are ignored
            Room room(lobby_room_config(90));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            case_of("START", room, bob, start_wish, true);
            allowance_of("START", room, cat, start_wish);
        }
        {
            Room room(lobby_room_config(91));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const std::vector<uint8_t> move_wish = encode(SeatMoveMsg{2, 3, seating_hash(room.host.room())});
            case_of("color move", room, bob, move_wish, true);
            allowance_of("color move", room, cat, move_wish);
        }
        {
            Room room(lobby_room_config(92));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            case_of("plan", room, bob, plan_wish, true);
            allowance_of("plan", room, cat, plan_wish);
        }
        {   // a removal from a guest that is not the leader
            Room room(lobby_room_config(97));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const std::vector<uint8_t> remove_wish = encode(RemoveMsg{2, seating_hash(room.host.room())});
            case_of("removal", room, bob, remove_wish, true);
            allowance_of("removal", room, cat, remove_wish);
        }
        {   // a rename while the leader's START waits for a page's game (the wait is longer than the test)
            HostLobby::Config hc = lobby_room_config(93);
            hc.start_wait_ms = 3600u * 1000u;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            const size_t pat = join_with(room, page_config("Pat"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            case_of("rename", room, pia, encode(NameMsg{"Pia Two"}), true);
            allowance_of("rename", room, pat, encode(NameMsg{"Pat Two"}));
        }
        {   // a room that is no lobby room forgives nothing, whatever its setting says
            HostLobby::Config hc = lobby_room_config(94);
            hc.lobby_room = false;
            hc.forgive_ms = 1000;
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            case_of("a room that is no lobby room", room, bob, start_wish, false);
        }
        {   // 0 switches it off
            HostLobby::Config hc = lobby_room_config(95);
            hc.forgive_ms = 0;
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            case_of("forgive_ms 0", room, bob, start_wish, false);
        }
        {   // a server that has run for more than 24.8 days (its 32-bit clock is past the signed half): a guest that comes in now is forgiven a minute later, not at the end of the clock's round
            Room room(lobby_room_config(98));
            room.now = 0x80000100u;
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            case_of("a clock past the signed half", room, bob, start_wish, true);
        }
        {   // a flood that is faster than the forgiving is thrown out as ever: ten a second against one forgiven a second
            HostLobby::Config hc = lobby_room_config(96);
            hc.forgive_ms = 1000;
            Room room(hc);
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            for (int i = 0; i < 100 && room.host.occupied(bob_seat); ++i) {
                room.guests[bob].client_end->send(start_wish);
                room.run(100);
            }
            ASSERT_FALSE(room.host.occupied(bob_seat));
            // ... and one that is slower than it is never thrown out: two a second against one forgiven a second would be thrown out, one every two seconds is not
            Room slow(hc);
            slow.join_seat("Ann");
            const size_t cat = slow.join_seat("Cat");
            const uint8_t cat_seat = slow.guests[cat].lobby->my_seat();
            for (int i = 0; i < 400; ++i) {
                slow.guests[cat].client_end->send(start_wish);
                slow.run(2000);
            }
            ASSERT_TRUE(slow.host.occupied(cat_seat));
        }
    } TEST_END();

    TEST_CASE("N4.35 A Flood Of STARTs In A Lobby Room (Protocol 16): Those That Cannot Be Honoured Are Free Up To 16 Per Guest (The Leader's Second One While The First Stands, A Guest's That Does Not Lead), The 17th Is A Violation And The 24th Throws The Guest Out; Honoured Ones Count For Nothing") {
        {   // a guest that does not lead
            Room room(lobby_room_config(97));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const std::vector<uint8_t> wish = encode(StartRequestMsg{});
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) room.guests[bob].client_end->send(wish);
            room.run(10);
            ASSERT_EQ(room.host.ignored_start_requests(), kIgnoredStartRequestsAllowed);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            for (int i = 0; i < 7; ++i) room.guests[bob].client_end->send(wish);          // the 17th to the 23rd: violations one to seven
            room.run(10);
            ASSERT_EQ(room.host.ignored_start_requests(), kIgnoredStartRequestsAllowed + 7u);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            room.guests[bob].client_end->send(wish);                                     // the 24th: the eighth
            room.run(10);
            ASSERT_FALSE(room.host.occupied(bob_seat));
        }
        {   // the leader: one START stands (it is not ignored), every one while it stands is
            HostLobby::Config hc = lobby_room_config(98);
            hc.start_wait_ms = 3600u * 1000u;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            join_with(room, page_config("Pia"));
            room.run(100);
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const std::vector<uint8_t> wish = encode(StartRequestMsg{});
            room.guests[ann].client_end->send(wish);                                     // the one that stands (a page is no game: it waits)
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_EQ(room.host.ignored_start_requests(), 0u);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) room.guests[ann].client_end->send(wish);
            room.run(10);
            ASSERT_EQ(room.host.ignored_start_requests(), kIgnoredStartRequestsAllowed);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(wish);
            room.run(10);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(wish);
            room.run(10);
            ASSERT_FALSE(room.host.occupied(ann_seat));                                  // the leader is thrown out like anybody (the lead goes to the next)
        }
    } TEST_END();

    TEST_CASE("N4.36 The Activity Of A Lobby Room (Protocol 16): activity() Counts What The People Do With The Room (A Welcome, A Color Move, A Plan Or A Name That Changed, A Removal, A Line Of Chat, A START That Stands) And Nothing Else: Not A Ping, A Request That Was Ignored Or Changed Nothing, A Link That Ends, Comes Back Or Gives Up") {
        {
            HostLobby::Config hc = lobby_room_config(99);
            hc.hold_ms = 5000;
            hc.start_wait_ms = 3600u * 1000u;                                          // (the START below stands for as long as the test goes on)
            Room room(hc);
            ASSERT_EQ(room.host.activity(), 0u);
            const size_t ann = room.join_seat("Ann");
            ASSERT_EQ(room.host.activity(), 1u);                                       // a welcome
            const size_t bob = room.join_seat("Bob");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_EQ(room.host.activity(), 3u);
            const uint32_t seated = room.host.activity();
            room.run(30000);                                                           // half a minute of pings and pongs, and nobody does a thing
            ASSERT_EQ(room.host.activity(), seated);
            // a color move and a plan of a guest that does not lead are ignored, the leader's color move is not
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            ASSERT_EQ(bob_seat, 1);
            room.guests[bob].client_end->send(encode(SeatMoveMsg{bob_seat, 3, seating_hash(room.host.room())}));
            room.guests[bob].client_end->send(encode(plan_of({PlanKind::Easy, PlanKind::Easy, PlanKind::Easy, PlanKind::Easy})));
            room.run(100);
            ASSERT_EQ(room.host.ignored_seat_moves(), 1u);
            ASSERT_EQ(room.host.ignored_plans(), 1u);
            ASSERT_EQ(room.host.activity(), seated);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(bob_seat, 3));
            room.run(100);
            ASSERT_EQ(room.host.seat_moves(), 1u);
            ASSERT_EQ(room.host.room().slots[3].name, std::string("Bob"));
            ASSERT_EQ(room.host.activity(), seated + 1u);
            // a plan that changed something, and the same plan again
            const PlanMsg plan = plan_of({PlanKind::Open, PlanKind::Hard, PlanKind::Open, PlanKind::Open});
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan));
            room.run(100);
            ASSERT_EQ(room.host.plan_changes(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 2u);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan));
            room.run(100);
            ASSERT_EQ(room.host.plan_changes(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 2u);
            // a name that changed, the same name again, and one that looks like a bot's (nothing changes)
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Bobby"));
            room.run(1100);
            ASSERT_EQ(room.host.renames(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 3u);
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Bobby"));
            ASSERT_FALSE(room.guests[bob].lobby->request_name("Bot (Hard)"));                       // (protocol 20: not even sent)
            room.run(1100);
            ASSERT_EQ(room.host.renames(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 3u);
            // a line of chat (an empty one is not even sent)
            ASSERT_FALSE(room.guests[pia].lobby->chat(""));
            ASSERT_TRUE(room.guests[pia].lobby->chat("hello"));
            room.run(100);
            ASSERT_EQ(room.host.chat_total(), uint64_t{1});
            ASSERT_EQ(room.host.activity(), seated + 4u);
            // a link that ends is a held seat, its key takes the seat back, and another hold runs out: none of it is something that a person does with the room
            const SeatKey bob_key = room.guests[bob].lobby->key();
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(3));
            ASSERT_EQ(room.host.activity(), seated + 4u);
            const size_t bob2 = join_keyed(room, "Bobby", bob_key);
            room.run(300);
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(room.guests[bob2].lobby->my_seat(), 3);
            ASSERT_EQ(room.host.activity(), seated + 4u);
            room.guests[pia].client_end->close();
            room.run(hc.hold_ms + 1000);
            ASSERT_EQ(room.host.hold_expiries(), 1u);
            ASSERT_FALSE(room.host.occupied(2));
            ASSERT_EQ(room.host.activity(), seated + 4u);
            // a START that stands, and a second one while it stands (which is ignored)
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_EQ(room.host.activity(), seated + 5u);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_EQ(room.host.ignored_start_requests(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 5u);
        }
        {   // a person that the leader removes (protocol 16) is something that a person does with the room; a removal that was ignored (a guest that does not lead) is not
            Room room(lobby_room_config(110));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.join_seat("Cat");
            const uint32_t seated = room.host.activity();
            ASSERT_EQ(seated, 3u);
            room.guests[bob].client_end->send(encode(RemoveMsg{2, seating_hash(room.host.room())}));
            room.run(100);
            ASSERT_EQ(room.host.ignored_removes(), 1u);
            ASSERT_EQ(room.host.activity(), seated);
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(2));
            room.run(100);
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.activity(), seated + 1u);
        }
    } TEST_END();

    TEST_CASE("N4.37 The Removal (Protocol 16): The Leader Of An Open Lobby Room Takes Another Person Out, Held Or Not, Also While The START Waits For The Games; The Person Is Told (Kicked), The Color Is Free At Once, The Key Is Forgotten And Nobody Is Sent A Notice; It Acts Only On The Seating That The Leader Saw, Never On The Leader, A Bot Or An Empty Seat; Everything Else Is Ignored And Counted, And Removals Have A Budget Of Their Own") {
        using K = PlanKind;
        {   // what a removal does: the person is told and their link closed, the color is free for everybody at once, nothing else of the room changes (the plan, the teams, the lead), nobody is sent a notice
            Room room(lobby_room_config(100));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const SeatKey bob_key = room.guests[bob].lobby->key();
            ASSERT_FALSE(key_is_zero(bob_key));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Hard}, 0, 1)));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooh 0+1"));
            const uint32_t activity = room.host.activity();
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
            ASSERT_EQ(room.host.activity(), activity + 1u);
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[bob].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_FALSE(room.guests[bob].client_end->is_open());
            for (const size_t g : {ann, cat}) ASSERT_EQ(layout(room.guests[g].lobby->room()), std::string("Ann@0 Cat@2"));
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Cat@2"));
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooh 0+1"));                      // (the color keeps what the plan calls it, the teams stay as they were)
            ASSERT_EQ(room.host.leader(), 0);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && !room.guests[cat].lobby->is_leader());
            ASSERT_TRUE(room.guests[ann].lobby->chat_log().empty() && room.guests[cat].lobby->chat_log().empty());       // (the room says nothing to anybody)
            // the key of the person who was removed fits no seat: whoever shows it is a new player, in the lowest color that is free, and not a guest who takes a seat back
            const size_t bob2 = join_keyed(room, "Bob", bob_key);
            room.run(300);
            ASSERT_EQ(room.guests[bob2].lobby->my_seat(), 1);
            ASSERT_FALSE(room.guests[bob2].lobby->rejoined());
            ASSERT_EQ(room.host.takeovers(), 0u);
            ASSERT_TRUE(room.guests[bob2].lobby->key() != bob_key && !key_is_zero(room.guests[bob2].lobby->key()));
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1 Cat@2"));
            ASSERT_EQ(room.host.leader(), 0);
        }
        {   // a person whose link has ended and whose seat is held can be removed (the leader need not wait for the minute), and a page is removed as a game is
            Room room(lobby_room_config(101));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(1));
            ASSERT_EQ(layout(room.guests[ann].lobby->room()), std::string("Ann@0 Bob@1 Pia@2"));        // (the leader's screen still shows Bob)
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_FALSE(room.host.held(1));
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.holds(), 1u);
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(2));
            room.run(100);
            ASSERT_EQ(room.guests[pia].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[pia].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_EQ(room.host.removals(), 2u);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0"));
            room.run(61000);                                                                              // (a hold that was ended by a removal does not run out later)
            ASSERT_EQ(room.host.hold_expiries(), 0u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
        }
        {   // what is ignored and counted (free up to 16 per guest): a guest that does not lead, the leader on itself, an empty seat, a bot's seat, a screen that is not the seating any more
            Room room(lobby_room_config(102));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            const auto press = [&room](size_t who, uint8_t seat, uint32_t guard) { room.guests[who].client_end->send(encode(RemoveMsg{seat, guard})); };
            const auto expect = [&room](uint32_t ignored, const char* what) {
                room.run(100);
                ASSERT_MSG(room.host.ignored_removes() == ignored, what);
                ASSERT_MSG(room.host.removals() == 0 && room.host.players() >= 3, what);
            };
            press(cat, 1, seating_hash(room.host.room()));                                               // Cat does not lead
            expect(1, "a guest that does not lead");
            press(bob, 1, seating_hash(room.host.room()));                                               // not even for oneself
            expect(2, "a guest on itself");
            press(ann, 0, seating_hash(room.host.room()));                                               // the leader does not take itself out (it leaves)
            expect(3, "the leader on itself");
            press(ann, 3, seating_hash(room.host.room()));                                               // nobody in the color
            expect(4, "an empty seat");
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            room.run(100);
            press(ann, 3, seating_hash(room.host.room()));                                               // a computer player is no person
            expect(5, "a bot");
            const uint32_t stale = seating_hash(room.host.room());
            ASSERT_TRUE(room.guests[bob].lobby->request_name("Robert"));                                  // the seating is another one now (a name is part of it)
            room.run(100);
            ASSERT_TRUE(seating_hash(room.host.room()) != stale);
            press(ann, 1, stale);                                                                        // pressed on "Bob", who is "Robert" now
            expect(6, "a stale screen");
            press(ann, 1, seating_hash(room.host.room()) + 1u);                                          // a number that is no seating's
            expect(7, "a guard that is not the seating's");
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Robert@1 Cat@2 Bot (Easy)@3"));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));                                      // the right one is done
            room.run(100);
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.ignored_removes(), 7u);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Cat@2 Bot (Easy)@3"));
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()) && room.host.occupied(room.guests[cat].lobby->my_seat()));
        }
        {   // the guard keeps a press on the person it was made for: the screen showed Bob in the second color, Bob went, Dan took the color, and the press arrives: Dan stays
            Room room(lobby_room_config(103));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.guests[ann].frozen = true;                                                              // (the leader's page shows the room as it was)
            room.guests[bob].lobby->leave();
            room.run(100);
            const size_t dan = room.join_seat("Dan");
            ASSERT_EQ(room.guests[dan].lobby->my_seat(), 1);
            ASSERT_EQ(layout(room.guests[ann].lobby->room()), std::string("Ann@0 Bob@1"));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Dan@1"));
            ASSERT_EQ(room.guests[dan].lobby->phase(), ClientLobby::Phase::InRoom);
            ASSERT_EQ(room.host.ignored_removes(), 1u);
            ASSERT_EQ(room.host.removals(), 0u);
            room.guests[ann].frozen = false;                                                             // the page sees Dan now, and the press on Dan is done
            room.run(100);
            ASSERT_EQ(layout(room.guests[ann].lobby->room()), std::string("Ann@0 Dan@1"));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0"));
            ASSERT_EQ(room.guests[dan].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.ignored_removes(), 1u);
        }
        {   // a match that loads hears no removal: the client does not send one, and a stray one is counted without offence
            Room room(lobby_room_config(104));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(1));
            room.guests[ann].client_end->send(encode(RemoveMsg{1, seating_hash(room.host.room())}));
            room.run(100);
            ASSERT_EQ(room.host.ignored_removes(), 1u);
            ASSERT_EQ(room.host.removals(), 0u);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1"));
        }
        {   // while the leader's START waits for the games the leader can still take a person out: this is how a person whose game never opens goes, and the START goes on without them when the others are games
            HostLobby::Config hc = lobby_room_config(105);
            hc.start_wait_ms = 20000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            const size_t pat = join_with(room, page_config("Pat"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Easy})));      // (a bot in the last color: one person is enough for the match)
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));                                          // Pia's page
            room.run(100);
            ASSERT_EQ(room.guests[pia].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Pat@2"));
            ASSERT_TRUE(room.host.starting() && room.guests[ann].lobby->room().starting());                  // (Pat's game has not come: the wait goes on)
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(2));                                          // Pat's page
            room.run(100);
            ASSERT_EQ(room.guests[pat].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_TRUE(room.host.starting());
            ASSERT_FALSE(leader_starts(room.host).empty());                                                  // every person who is left is a game: the owner is given the START
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));                                                 // (the owner seats the plan's bot and starts)
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::Loading);
            ASSERT_EQ(room.host.removals(), 2u);
        }
        {   // ... and a START that cannot go on without the person who was removed (no bot in the plan, one person is not a match) ends quietly: nobody is told, and the leader may ask again when somebody has joined
            HostLobby::Config hc = lobby_room_config(111);
            hc.start_wait_ms = 20000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(room.guests[pia].lobby->reject_reason(), RejectReason::Kicked);
            ASSERT_FALSE(room.host.starting() || room.guests[ann].lobby->room().starting());
            ASSERT_TRUE(leader_starts(room.host).empty());
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
            const size_t bob = room.join_seat("Bob");                                                        // (the room takes newcomers again)
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 1);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_FALSE(leader_starts(room.host).empty());
        }
        {   // ... and a person whose link has ended, whose seat is held, is removed as well: the START waits for the person, not for the minute of the hold, so it goes on without them at once, and the hold does not run out later
            HostLobby::Config hc = lobby_room_config(112);
            hc.start_wait_ms = 20000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = join_with(room, page_config("Bob"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Easy})));      // (a bot in the last color: one person is enough for the match)
            room.run(100);
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(1));
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_TRUE(leader_starts(room.host).empty());                                                   // (Bob's seat is held: the START waits for him)
            ASSERT_TRUE(room.host.held(1));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));                                          // (the leader's screen still shows Bob)
            room.run(100);
            ASSERT_FALSE(room.host.occupied(1));
            ASSERT_FALSE(room.host.held(1));
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0"));
            ASSERT_TRUE(room.host.starting());
            ASSERT_FALSE(leader_starts(room.host).empty());                                                  // (every person who is left is a game: the owner is given the START)
            ASSERT_EQ(room.host.removals(), 1u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
            ASSERT_EQ(room.host.hold_expiries(), 0u);
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.guests[ann].lobby->phase(), ClientLobby::Phase::Loading);
            ASSERT_EQ(room.host.holds(), 1u);
            ASSERT_EQ(room.host.hold_expiries(), 0u);
        }
        {   // the budget: a burst of kRemoveBurst removals, then one a second. One beyond it is dropped (the person stays, nobody is told, nothing is counted as ignored), and a second after the burst was spent it is done
            Room room(lobby_room_config(106));
            const size_t ann = room.join_seat("Ann");
            for (const char* name : {"Bob", "Cat", "Dan"}) room.join_seat(name);
            Screen screen{room.host.room()};
            for (const uint8_t seat : {uint8_t{1}, uint8_t{2}, uint8_t{3}}) room.guests[ann].client_end->send(screen.remove(seat));        // (each one is made for the screen as the one before it left it)
            room.run(10);
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0"));
            for (const char* name : {"Eve", "Fay", "Gus"}) room.join_seat(name);                               // (three newcomers: 300 ms, no whole removal has been refilled)
            ASSERT_EQ(layout(room.guests[ann].lobby->room()), std::string("Ann@0 Eve@1 Fay@2 Gus@3"));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Eve@1 Fay@2 Gus@3"));
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
            room.run(400);                                                                                      // (0.7 seconds after the burst was spent: not a whole removal at one a second, nearly two at two a second)
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Eve@1 Fay@2 Gus@3"));
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
            room.run(1000);
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Fay@2 Gus@3"));
            ASSERT_EQ(room.host.removals(), kRemoveBurst + 1u);
            ASSERT_TRUE(room.host.occupied(0));
        }
        {   // ... and a leader that goes on beyond the budget is flooding: kRemoveExcessBurst (6) more are dropped, every one after that is a violation (the eighth throws the leader out)
            Room room(lobby_room_config(107));
            const size_t ann = room.join_seat("Ann");
            for (const char* name : {"Bob", "Cat", "Dan"}) room.join_seat(name);
            Screen screen{room.host.room()};
            for (const uint8_t seat : {uint8_t{1}, uint8_t{2}, uint8_t{3}}) room.guests[ann].client_end->send(screen.remove(seat));
            room.run(10);
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
            for (const char* name : {"Eve", "Fay", "Gus"}) room.join_seat(name);
            const std::vector<uint8_t> press = encode(RemoveMsg{1, seating_hash(room.host.room())});
            for (unsigned i = 0; i < 6; ++i) room.guests[ann].client_end->send(press);                         // (the excess that is tolerated: 6 more)
            room.run(10);
            ASSERT_TRUE(room.host.occupied(0) && room.host.occupied(1));
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
            ASSERT_EQ(room.host.ignored_removes(), 0u);                                                        // (a dropped press is counted by nobody)
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(press);                               // the excess is used up: each one is an offence
            room.run(10);
            ASSERT_TRUE(room.host.occupied(0));
            room.guests[ann].client_end->send(press);
            room.run(10);
            ASSERT_FALSE(room.host.occupied(0));                                                              // the eighth throws the leader out, as it does for any guest
            ASSERT_TRUE(room.host.occupied(1));
            ASSERT_EQ(room.host.removals(), kRemoveBurst);
        }
        {   // a room that is no lobby room has no removals: the message is garbage there (eight of them throw the sender out), and a client does not send it
            HostLobby::Config hc = lobby_room_config(108);
            hc.lobby_room = false;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(1));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(encode(RemoveMsg{1, seating_hash(room.host.room())}));
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(encode(RemoveMsg{1, seating_hash(room.host.room())}));
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_EQ(room.host.removals(), 0u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);                                                        // (garbage is no removal)
            ASSERT_TRUE(room.host.occupied(1));
        }
        {   // a message that is no RemoveMsg (a seat that no room has, a guard of 0, a missing or an extra byte) is garbage whoever sends it: a violation at once (eight throw the sender out), and no removal
            Room room(lobby_room_config(112));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const std::vector<std::vector<uint8_t>> garbage = {
                {34}, {34, 1}, {34, 1, 7, 0, 0}, {34, 9, 7, 0, 0, 0}, {34, 1, 0, 0, 0, 0}, {34, 1, 7, 0, 0, 0, 0}, {34, 255, 7, 0, 0, 0}, {34, 1, 7, 0, 0, 0, 0, 0},
            };
            for (size_t i = 0; i + 1 < garbage.size(); ++i) room.guests[ann].client_end->send(garbage[i]);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));                                                       // seven violations are not enough
            room.guests[ann].client_end->send(garbage.back());
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));                                                      // the eighth
            ASSERT_EQ(room.host.removals(), 0u);
            ASSERT_EQ(room.host.ignored_removes(), 0u);
            ASSERT_TRUE(room.host.occupied(1));
        }
        {   // the client: only the leader of an open lobby room asks, and only for a seat that shows another person
            Room room(lobby_room_config(109));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_FALSE(room.guests[bob].lobby->request_remove(0));                                          // a guest that does not lead
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(0));                                          // the leader's own color
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(2));                                          // a color that nobody holds
            ASSERT_TRUE(room.host.add_bot(3, "Bot (Easy)"));
            room.run(100);
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(3));                                          // a computer player
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(4));                                          // no color
            ASSERT_FALSE(room.guests[ann].lobby->request_remove(255));
            Room::Guest& fresh = room.join("Fay");                                                           // a machine that has not been welcomed yet
            ASSERT_FALSE(fresh.lobby->request_remove(1));
            room.run(100);
            ASSERT_EQ(room.host.ignored_removes(), 0u);                                                       // (nothing of it was sent)
            ASSERT_EQ(room.host.removals(), 0u);
            ASSERT_EQ(layout(room.host.room()), std::string("Ann@0 Bob@1 Fay@2 Bot (Easy)@3"));
            room.guests[bob].lobby->leave();                                                                  // a machine that has left sends nothing either
            room.run(100);
            ASSERT_FALSE(room.guests[bob].lobby->request_remove(2));
            ASSERT_TRUE(room.guests[ann].lobby->request_remove(2));                                           // (Fay is a person: the leader's screen shows her)
            room.run(100);
            ASSERT_EQ(room.host.removals(), 1u);
        }
    } TEST_END();

    TEST_CASE("N4.38 The Game Mode (Protocol 17): A Room Starts With The Original's Mode 0; The Owner Of A Room Sets It (Open Room, A Mode This Build Knows), Only The Leader's Plan Sets It In A Lobby Room, It Is Shown To Everybody, Also To A Late Joiner, And The Start That Every Machine Decodes Carries It; A Plan Of Another Player, A Mode Above The Last, And A Room That Loads Change Nothing") {
        using K = PlanKind;
        {   // the leader's plan sets the mode (alone or with the rest), it is shown to everybody and the Start carries it
            Room room(lobby_room_config(110));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_EQ(room.host.mode(), 0);
            for (const size_t g : {ann, bob}) ASSERT_EQ(room.guests[g].lobby->room().mode, 0);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Open})));
            room.run(100);
            ASSERT_EQ(room.host.plan_changes(), 0u);                                // (the plan of the mode 0 changes nothing)
            PlanMsg wish = plan_of({K::Open, K::Open, K::Open, K::Open});
            wish.mode = static_cast<uint8_t>(sim::GameMode::Kills187);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(wish));
            room.run(100);
            ASSERT_EQ(room.host.mode(), 1);
            ASSERT_EQ(room.host.room().mode, 1);
            ASSERT_EQ(room.host.plan_changes(), 1u);                                // (only the mode changed: it is a change of the plan, the room is shown anew)
            for (const size_t g : {ann, bob}) ASSERT_EQ(room.guests[g].lobby->room().mode, 1);
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooo -"));
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(wish));                // the same plan again: nothing changed, nothing is shown
            room.run(100);
            ASSERT_EQ(room.host.plan_changes(), 1u);
            const size_t cat = room.join_seat("Cat");                               // a late joiner hears it in its first Room message
            ASSERT_EQ(room.guests[cat].lobby->room().mode, 1);
            // a person who is not the leader is ignored and counted, the mode stays
            PlanMsg back = plan_of({K::Open, K::Open, K::Open, K::Open});
            ASSERT_FALSE(room.guests[bob].lobby->request_plan(back));               // (a client that does not lead sends nothing: a raw one does)
            room.guests[bob].client_end->send(encode(back));
            room.run(100);
            ASSERT_EQ(room.host.mode(), 1);
            ASSERT_EQ(room.host.ignored_plans(), 1u);
            ASSERT_EQ(room.host.plan_changes(), 1u);
            // the leader goes back to the original's mode
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(back));
            room.run(100);
            ASSERT_EQ(room.host.mode(), 0);
            ASSERT_EQ(room.host.plan_changes(), 2u);
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(room.guests[g].lobby->room().mode, 0);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(wish));
            room.run(100);
            ASSERT_EQ(room.host.mode(), 1);
            // the Start carries it: the owner's copy and the copy of every guest
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            ASSERT_EQ(room.host.start_info().mode, 1);
            ASSERT_TRUE(room.host.start_info().game_mode() == sim::GameMode::Kills187);
            room.run(100);
            for (const size_t g : {ann, bob, cat}) {
                ASSERT_EQ(room.guests[g].lobby->phase(), ClientLobby::Phase::Loading);
                ASSERT_EQ(room.guests[g].lobby->start_info().mode, 1);
                ASSERT_TRUE(room.guests[g].lobby->start_info().game_mode() == sim::GameMode::Kills187);
            }
            // a room that loads changes nothing: the owner's call is refused and so is a plan
            room.host.set_mode(0);
            ASSERT_EQ(room.host.mode(), 1);
            ASSERT_EQ(room.host.start_info().mode, 1);
        }
        {   // a mode that this build does not know is never taken: not by the owner, not by a plan on the wire (which is garbage, eight of them throw the sender out)
            Room room(lobby_room_config(111));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            room.host.set_mode(static_cast<uint8_t>(sim::kLastGameMode + 1));
            room.host.set_mode(255);
            ASSERT_EQ(room.host.mode(), 0);
            PlanMsg wish = plan_of({K::Open, K::Open, K::Open, K::Open});
            wish.mode = static_cast<uint8_t>(sim::kLastGameMode + 1);
            ASSERT_FALSE(room.guests[ann].lobby->request_plan(wish));                                    // (the client does not send it)
            std::vector<uint8_t> bytes = encode(plan_of({K::Open, K::Open, K::Open, K::Open}));
            ASSERT_EQ(bytes.back(), 0);
            bytes.back() = static_cast<uint8_t>(sim::kLastGameMode + 1);
            PlanMsg decoded;
            ASSERT_FALSE(decode(bytes.data(), bytes.size(), decoded));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(bytes);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(bytes);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_EQ(room.host.mode(), 0);
            ASSERT_EQ(room.host.plan_changes(), 0u);
            ASSERT_EQ(room.host.ignored_plans(), 0u);                                // (garbage is no plan)
        }
        {   // a room that is no lobby room (a LAN host, a server's game room) has the owner's mode only, and its Start carries it; a room is shown anew when the owner changes the mode
            HostLobby::Config hc = keyed_server_config(112);
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            ASSERT_EQ(room.guests[ann].lobby->room().mode, 0);
            room.host.set_mode(1);
            room.run(100);
            ASSERT_EQ(room.host.mode(), 1);
            ASSERT_EQ(room.guests[ann].lobby->room().mode, 1);
            room.guests[ann].lobby->take_events();
            room.host.set_mode(1);                                                    // (the same mode: nothing is sent again)
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->take_events().empty());
            room.host.set_mode(0);
            room.run(100);
            ASSERT_EQ(room.guests[ann].lobby->room().mode, 0);
            room.host.set_mode(1);
            room.run(100);
            ASSERT_TRUE(room.host.start(7, 8, room.now));
            room.run(100);
            ASSERT_EQ(room.host.start_info().mode, 1);
            ASSERT_EQ(room.guests[ann].lobby->start_info().mode, 1);
        }
        {   // a Start of protocol 16 (no mode byte) or with a mode above the last is no Start for a client: it stays in the room and loads nothing (a machine never plays a match as the original's when the room meant another);
            // a Room message of the same two kinds is not taken either; a Start with a mode that this build knows is
            LoopbackNetwork net(9);
            auto ends = net.connect({10, 0});
            Connection* server = ends.first;
            ClientLobby::Config cc;
            cc.name = "Bob";
            cc.want_seat = 1;
            ClientLobby lobby(ends.second, cc);
            uint32_t now = 0;
            const auto step = [&](uint32_t ms) {
                const uint32_t end = now + ms;
                while (now < end) {
                    now += 10;
                    net.set_time(now);
                    lobby.update(now);
                }
            };
            step(30);
            std::vector<uint8_t> m;
            ASSERT_TRUE(server->poll(m));
            WelcomeMsg w;
            w.player = 1;
            w.players = 4;
            server->send(encode(w));
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
            StartMsg start;
            start.seed = 31;
            start.map_name = "TINY.LVL";
            start.map_hash = 0x1234567890ABCDEFull;
            start.roster = 0x03;
            start.names[0] = "Ann";
            start.names[1] = "Bob";
            start.mode = 1;
            const std::vector<uint8_t> good = encode(start);
            std::vector<uint8_t> v16(good.begin(), good.end() - 1);                  // protocol 16's Start: the same bytes without the last
            std::vector<uint8_t> too_high = good;
            too_high.back() = static_cast<uint8_t>(sim::kLastGameMode + 1);
            server->send(v16);
            server->send(too_high);
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::InRoom);
            ASSERT_TRUE(lobby.take_events().empty());
            RoomMsg room_msg;
            room_msg.you = 1;
            room_msg.map_name = "TINY.LVL";
            room_msg.slots[0].state = SlotState::Client;
            room_msg.slots[0].name = "Ann";
            room_msg.slots[1].state = SlotState::Client;
            room_msg.slots[1].name = "Bob";
            std::vector<uint8_t> room_good = encode(room_msg);
            std::vector<uint8_t> room_v16(room_good.begin(), room_good.end() - 1);
            std::vector<uint8_t> room_high = room_good;
            room_high.back() = static_cast<uint8_t>(sim::kLastGameMode + 1);
            server->send(room_v16);
            server->send(room_high);
            step(30);
            ASSERT_TRUE(lobby.take_events().empty());
            server->send(room_good);                                                 // (the room of a mode that is known is taken)
            step(30);
            ASSERT_EQ(lobby.take_events().size(), size_t{1});
            ASSERT_EQ(lobby.room().mode, 0);
            server->send(good);
            step(30);
            ASSERT_EQ(lobby.phase(), ClientLobby::Phase::Loading);
            ASSERT_EQ(lobby.start_info().mode, 1);
        }
        {   // a mode of 0 leaves the Start as it was in every other field: the same room, the same seed and map, with and without the call that sets the original's mode
            Room plain(lobby_room_config(113));
            plain.join_seat("Ann");
            plain.join_seat("Bob");
            ASSERT_TRUE(plain.host.start(9, 10, plain.now));
            Room set(lobby_room_config(113));
            set.join_seat("Ann");
            set.join_seat("Bob");
            set.host.set_mode(0);
            ASSERT_TRUE(set.host.start(9, 10, set.now));
            ASSERT_TRUE(encode(plain.host.start_info()) == encode(set.host.start_info()));
            ASSERT_EQ(plain.host.start_info().mode, 0);
        }
    } TEST_END();
    TEST_CASE("N4.40 The Names Of The Bots (Protocol 20): With A Name Key A Lobby Room Shows The Bot That A Color's Plan Asks For, Under Its Own Name, In The Empty Seat Of Every Guest's Room Message (The Room's Own Copy Keeps The Seat Empty, So The Guard Of A Move Is The Same On Both Sides); Without A Key The Seat Is Empty As Before; A Bot That Is Seated Keeps The Name It Is Given; A Person Cannot Be Called Like A Bot; The Decoders Refuse A Name On An Empty Seat That Is No Planned Bot's And A New Name That Is A Bot's") {
        using K = PlanKind;
        const uint64_t key = ai::bot_name_key("N4.40");
        {   // the plan's bots are named in the guests' Room messages, seat by seat, and the name goes with the plan
            HostLobby::Config hc = lobby_room_config(80);
            hc.bot_name_key = key;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Hard, K::Nobody})));
            room.run(100);
            for (const size_t g : {ann, bob}) {
                const RoomMsg& shown = room.guests[g].lobby->room();
                ASSERT_EQ(shown.slots[2].name, bot_room_name(key, 2, FillLevel::Hard));
                ASSERT_EQ(shown.slots[2].name, ai::bot_label(ai::bot_seat_name(key, 2), "hard"));
                ASSERT_TRUE(shown.slots[2].state == SlotState::Empty && shown.slots[3].name.empty());
                ASSERT_EQ(seating_hash(shown), seating_hash(room.host.room()));                // (the guard of a move is the same on both sides)
            }
            ASSERT_TRUE(room.host.room().slots[2].name.empty());                           // (the owner's copy keeps the seat empty)
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Medium, K::Hard})));
            room.run(100);
            const RoomMsg& shown = room.guests[bob].lobby->room();
            std::set<std::string> distinct;
            for (uint8_t seat = 2; seat < 4; ++seat) {
                ai::BotLabel label;
                ASSERT_TRUE(ai::parse_bot_label(shown.slots[seat].name, label) && !label.legacy && label.name == ai::bot_seat_name(key, seat));
                distinct.insert(label.name);
            }
            ASSERT_EQ(distinct.size(), size_t{2});
            ASSERT_TRUE(shown.slots[2].name.find("(bot, Medium)") != std::string::npos && shown.slots[3].name.find("(bot, Hard)") != std::string::npos);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Nobody, K::Open})));      // the plan goes: the names go
            room.run(100);
            for (const auto& slot : room.guests[bob].lobby->room().slots) ASSERT_TRUE(slot.name.empty() || slot.state != SlotState::Empty);
            // a bot that is seated has the name it is given, as it is; a name that is no bot's is wrapped
            ASSERT_TRUE(room.host.add_bot(2, bot_room_name(key, 2, FillLevel::Easy)));
            ASSERT_EQ(room.host.room().slots[2].name, bot_room_name(key, 2, FillLevel::Easy));
            ASSERT_TRUE(room.host.add_bot(3, "Zed"));
            ASSERT_EQ(room.host.room().slots[3].name, std::string("Bot (Zed)"));
        }
        {   // no key: no names (the room is what protocol 19 showed)
            Room room(lobby_room_config(81));
            const size_t ann = room.join_seat("Ann");
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Medium, K::Hard})));
            room.run(100);
            for (const auto& slot : room.guests[ann].lobby->room().slots) ASSERT_TRUE(slot.name.empty() || slot.state != SlotState::Empty);
            ASSERT_EQ(bot_room_name(0, 1, FillLevel::Easy), std::string("Bot (Easy)"));
            ASSERT_EQ(bot_room_name(0, 1, FillLevel::None), std::string());
        }
        {   // a person is never called like a bot: the Hello's name is replaced, a rename is not taken
            Room room(lobby_room_config(82));
            const size_t ann = room.join_seat("Ann");
            const size_t pip = room.join_seat("Pip (bot, Hard)");
            ASSERT_EQ(room.host.room().slots[static_cast<size_t>(room.guests[pip].lobby->my_seat())].name, std::string("Player ") + std::to_string(room.guests[pip].lobby->my_seat() + 1));
            ASSERT_EQ(room.host.room().slots[static_cast<size_t>(room.guests[ann].lobby->my_seat())].name, std::string("Ann"));
            // a control byte or a byte above the table between the letters, in a Hello that no honest client writes (raw bytes): the room cuts such bytes out BEFORE it checks the name, so what is stored is the cleaned
            // name, and it is never a bot's
            for (const char* sneaky : {"Pip (bo\x01t)", "Pip (b\x7F" "ot)", "Pip (bot\x01, Hard)", "Bo\x01t (Hard)", "Pip (bo\xC3\xA9t, Hard)", "Pip (bot)\x09"}) {
                Room raw_room(lobby_room_config(84));
                auto ends = raw_room.net.connect({10, 0});
                raw_room.host.add_connection(ends.first, raw_room.now);
                HelloMsg hello;
                const std::string wish = sneaky;
                hello.name = std::string(wish.size(), 'a');                                      // (encode() cleans a name: write the Hello with a plain one and put the bytes in by hand)
                std::vector<uint8_t> bytes = encode(hello);
                ASSERT_TRUE(bytes.size() > 4 + wish.size() && bytes[3] == wish.size());           // (type, version, the length of the name, the name)
                for (size_t k = 0; k < wish.size(); ++k) bytes[4 + k] = static_cast<uint8_t>(wish[k]);
                ends.second->send(bytes);
                raw_room.run(100);
                uint8_t seat = 255;
                std::vector<uint8_t> m;
                while (ends.second->poll(m)) {
                    WelcomeMsg w;
                    if (peek_type(m) == MsgType::Welcome && decode(m, w)) seat = w.player;
                }
                // the room either refuses the Hello (no seat) or seats the person under the cleaned name: never under a name that is a bot's, never with a byte outside the table
                for (const RoomMsg::Slot& slot : raw_room.host.room().slots) {
                    ASSERT_FALSE(slot.state == SlotState::Client && ai::is_reserved_bot_name(slot.name));
                    for (const char c : slot.name) ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) <= 0x7E);
                }
                (void)seat;
            }
        }
        {   // the decoders
            RoomMsg m;
            m.flags = static_cast<uint8_t>(kRoomLobby | kRoomLeaderStarts);
            m.slots[0].state = SlotState::Client;
            m.slots[0].name = "Ann";
            m.leader = 0;
            m.plan[2] = K::Hard;
            m.slots[2].name = "Pip (bot, Hard)";
            RoomMsg back;
            ASSERT_TRUE(decode(encode(m), back) && back.slots[2].name == "Pip (bot, Hard)");
            m.slots[2].name = "Pip";                                      // (not a bot's label)
            ASSERT_FALSE(decode(encode(m), back));
            m.slots[2].name = "Pip (bot, Hard)";
            m.plan[2] = K::Open;                                          // (no bot is planned there)
            ASSERT_FALSE(decode(encode(m), back));
            m.plan[2] = K::Nobody;
            ASSERT_FALSE(decode(encode(m), back));
            m.plan[2] = K::Hard;
            m.slots[2].state = SlotState::Bot;                            // (a seated bot has its name as it always had)
            ASSERT_TRUE(decode(encode(m), back));
            {   // a client never sends such a name: the server would count a violation and, eight times, throw it out
                Room rename_room(lobby_room_config(83));
                const size_t sam = rename_room.join_seat("Sam");
                for (const char* bad : {"Pip (bot)", "Bot (Hard)", "x ( BOT , idle )"}) ASSERT_FALSE(rename_room.guests[sam].lobby->request_name(bad));
                ASSERT_TRUE(rename_room.guests[sam].lobby->request_name("Robot"));
                rename_room.run(100);
                ASSERT_EQ(rename_room.host.renames(), 1u);
            }
            NameMsg name;
            NameMsg got;
            name.name = "Pip";
            ASSERT_TRUE(decode(encode(name), got) && got.name == "Pip");
            for (const char* bad : {"Pip (bot)", "Pip (bot, Hard)", "Bot (Hard)", "x (BOT , idle )", "bot(x", "Pip (bo\x01t)", "Pip (bot\xC3\xA9)"}) {
                name.name = bad;
                ASSERT_FALSE(decode(encode(name), got));
            }
            for (const char* fine : {"Robot", "Pip (bots)", "Pip (bot) x", "Bot"}) {
                name.name = fine;
                ASSERT_TRUE(decode(encode(name), got));
            }
        }
    } TEST_END();
}
