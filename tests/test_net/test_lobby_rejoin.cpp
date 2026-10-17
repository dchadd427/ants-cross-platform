// Tests of the room before a match: taking a seat back, the fill and its plan, the chat of the waiting room, the hook before the start and the color moves (N4.15 - N4.22)
#include "lobby_fixtures.hpp"

void run_lobby_rejoin_tests() {
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
        // the words (the color words of the seats: 0 green, 1 red, 2 blue, 3 black)
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

    TEST_CASE("N4.22 The Leader Moves The Colors (Protocol 14, The Guard Of 15): The Leader Of A Server's Room Puts A Player In An Empty Seat, With Key, Name, Thumb And Place In The Order; Everybody Is Sent The New Seats And The Moved Player Is Told By The Room; Only The Leader Is Heard, Only While The Room Is Open; What Cannot Be Done Is Ignored And Counted Like A Second Click On START, Garbage And A Flood Cost The Sender Its Seat (N4.23 Has The Swap And The Guard)") {
        ASSERT_TRUE(seat_color_word(0) == "Green" && seat_color_word(1) == "Red" && seat_color_word(2) == "Blue" && seat_color_word(3) == "Black" && seat_color_word(4).empty());
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
        {   // a color that a guest holds: the two guests change places (protocol 15), each with all that is its own, each told by the room; the leader's own color is a guest's too: the leader moves, and leads on
            Room room(keyed_server_config(22));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            room.run(300);
            for (const size_t g : {ann, bob, cat}) room.guests[g].lobby->take_chat();
            const SeatKey bob_key = room.guests[bob].lobby->key();
            const SeatKey cat_key = room.guests[cat].lobby->key();
            const auto bob_rtt = room.host.room().slots[1].rtt_ms;
            const auto cat_rtt = room.host.room().slots[2].rtt_ms;
            ASSERT_TRUE(bob_rtt != kRttUnknown && cat_rtt != kRttUnknown);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 1));                                        // Cat (Blue) onto Bob's seat (Red)
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Cat@1 Bob@2");
            for (const size_t g : {ann, bob, cat}) ASSERT_EQ(layout(room.guests[g].lobby->room()), "Ann@0 Cat@1 Bob@2");
            ASSERT_TRUE(room.guests[bob].lobby->my_seat() == 2 && room.guests[cat].lobby->my_seat() == 1 && room.guests[ann].lobby->my_seat() == 0);
            ASSERT_TRUE(key_matches(room.host.key_of(1), cat_key) && key_matches(room.host.key_of(2), bob_key));        // the key is the player's, wherever the player sits
            ASSERT_TRUE(room.host.room().slots[1].rtt_ms == cat_rtt && room.host.room().slots[2].rtt_ms == bob_rtt);   // ... and so is the thumb
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Blue."}));
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Red."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
            ASSERT_TRUE(room.host.seat_moves() == 1u && room.host.ignored_seat_moves() == 0u && room.host.players() == 3u);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 0));                                        // Cat (Red) onto the leader's own seat (Green): the leader goes to Red
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Cat@0 Ann@1 Bob@2");
            ASSERT_TRUE(room.guests[ann].lobby->my_seat() == 1 && room.guests[cat].lobby->my_seat() == 0 && room.guests[bob].lobby->my_seat() == 2);
            ASSERT_TRUE(room.host.leader() == 1 && room.guests[ann].lobby->is_leader() && !room.guests[cat].lobby->is_leader() && room.guests[cat].lobby->room().leader == 1);      // (the lead is the person's)
            ASSERT_EQ(notices_of(*room.guests[cat].lobby), (std::vector<std::string>{"Ann moved you to Green."}));
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[bob].lobby).empty());
            ASSERT_TRUE(room.host.seat_moves() == 2u && room.host.ignored_seat_moves() == 0u);
            // the color that is free is still a move, not a swap: three people in a room of four, Bob to Black
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(2, 3));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Cat@0 Ann@1 Bob@3");
            ASSERT_TRUE(room.host.seat_moves() == 3u && room.host.ignored_seat_moves() == 0u && !room.host.occupied(2));
            ASSERT_EQ(notices_of(*room.guests[bob].lobby), (std::vector<std::string>{"Ann moved you to Black."}));
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
            const size_t cat = room.join_seat("Cat");                                                           // a player who comes now takes the color that is free first (green), and is the newest: Ann leads still
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
            for (uint32_t i = 0; i < kIgnoredSeatMovesAllowed; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3, seating_hash(room.host.room())}));     // a client that is not the game's sends it anyway
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 16u);
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom);       // no offence
            for (int i = 0; i < 7; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3, seating_hash(room.host.room())}));                           // violations one to seven
            room.run(300);
            ASSERT_TRUE(room.host.occupied(bob_seat) && room.host.ignored_seat_moves() == 23u);
            for (int i = 0; i < 100; ++i) room.guests[bob].client_end->send(encode(SeatMoveMsg{2, 3, seating_hash(room.host.room())}));                        // the first of them is the eighth: out; the rest is never read
            room.run(300);
            ASSERT_FALSE(room.host.occupied(bob_seat));
            ASSERT_EQ(room.host.ignored_seat_moves(), 24u);
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::Rejected && room.guests[bob].lobby->reject_reason() == RejectReason::BadRequest);
            ASSERT_TRUE(room.host.leader() == 0 && room.host.seat_moves() == 0u && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
            ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty() && notices_of(*room.guests[cat].lobby).empty());
        }
        {   // what the leader asks for and cannot be had is ignored and counted, no offence: an empty seat, a bot's seat in either place, a player who left meanwhile; the rule's own refusals (move_seat) change nothing either
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
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), before);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 3u);
            ASSERT_TRUE(room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom && room.host.leader() == 0);
            room.guests[bob].lobby->leave();                                                                    // Bob goes; Ann's press that was meant for him is the next one
            room.run(300);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 2));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bot (Easy)@3");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 4u);
            // the rule itself: false and nothing changes for a seat that holds no guest, the same seat twice, a number that is no seat, a color that a bot holds (a guest's color is a swap: N4.23)
            const size_t cat = room.join_seat("Cat");
            ASSERT_EQ(room.guests[cat].lobby->my_seat(), 1);
            room.run(300);
            const std::string with_cat = layout(room.host.room());
            ASSERT_TRUE(!room.host.move_seat(2, 1) && !room.host.move_seat(1, 1) && !room.host.move_seat(1, 4) && !room.host.move_seat(4, 1) && !room.host.move_seat(255, 1) && !room.host.move_seat(1, 255));
            ASSERT_TRUE(!room.host.move_seat(1, 3) && !room.host.move_seat(3, 1) && !room.host.move_seat(3, 2) && !room.host.move_seat(0, 3));
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
            room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 3, seating_hash(room.host.room())}));        // a press that crossed the Start does arrive
            room.run(100);
            ASSERT_FALSE(room.host.move_seat(1, 3));
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && room.host.occupied(room.guests[ann].lobby->my_seat()));
            for (uint32_t i = 1; i < kIgnoredSeatMovesAllowed; ++i) room.guests[ann].client_end->send(encode(SeatMoveMsg{1, 3, seating_hash(room.host.room())}));     // ... as many as the sixteen that are free, in one tick: no budget of the done moves is spent, no offence
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
        {   // garbage (the same seat twice, a seat that no room has, a guard of 0, a missing or an extra byte, the three bytes of protocol 14) is a violation each, from the leader as from anybody; the eighth throws the sender out, and the next one leads
            Room room(keyed_server_config(28));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            room.run(300);
            const std::vector<std::vector<uint8_t>> garbage = {{31, 1, 1, 7, 0, 0, 0}, {31, 0, 4, 7, 0, 0, 0}, {31, 4, 0, 7, 0, 0, 0}, {31, 255, 255, 7, 0, 0, 0}, {31, 0, 1, 0, 0, 0, 0}, {31, 0, 1, 7, 0, 0}, {31, 0, 1}};
            for (const auto& bytes : garbage) room.guests[ann].client_end->send(bytes);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(0) && room.host.leader() == 0);                                      // seven violations
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 0u);                  // (garbage is no request: it is not counted as one)
            room.guests[ann].client_end->send(std::vector<uint8_t>{31, 0, 1, 7, 0, 0, 0, 0});                    // the eighth: an extra byte
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
            Screen screen{room.host.room()};
            for (int i = 0; i < presses; ++i) {                                                                 // the presses in the same millisecond: Bob bounces between seat 1 and 2 six times, and the rest press him on to seat 2 again
                const bool back = i < 6 && i % 2 == 1;                                                          // (every one of them can be done: the dropped ones leave Bob in seat 1)
                room.guests[ann].client_end->send(screen.press(back ? uint8_t{2} : uint8_t{1}, back ? uint8_t{1} : uint8_t{2}, i < 6));
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
            Screen screen{room.host.room()};
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));
            room.guests[ann].client_end->send(screen.press(0, 2));
            room.run(300);
            ASSERT_EQ(room.host.leader(), 2);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{2}));
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));                                       // the same on the way back, to a lower seat
            room.guests[ann].client_end->send(screen.press(2, 0));
            room.run(300);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1");
            ASSERT_EQ(room.host.leader(), 0);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{0}));
            room.guests[ann].client_end->send(screen.press(0, 3));                                              // and a START after a move is heard with the new seat
            room.guests[ann].client_end->send(encode(StartRequestMsg{}));
            room.run(300);
            ASSERT_EQ(room.host.leader(), 3);
            ASSERT_EQ(leader_starts(room.host), (std::vector<uint8_t>{3}));
            ASSERT_TRUE(room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom && room.host.ignored_start_requests() == 0u);
        }
        {   // the plan of bots of a START that was heard before a move is by color, so it goes with the colors: the bot that was for the color that the player takes is for the color that it leaves; every START that waits has it, whoever moves, and a START after the move is the client's own
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
            Screen screen{room.host.room()};
            const auto move = [&](uint8_t from, uint8_t to) { room.guests[ann].client_end->send(screen.press(from, to)); };
            ask(F::None, F::None, F::Hard, F::Easy);                                                            // a Hard bot for Blue, an Easy one for Black ...
            move(1, 2);                                                                                         // ... and Bob takes Blue: the Hard bot is for Red now, the Easy one is where it was
            room.run(300);
            ASSERT_EQ(leader_plans(room.host), (std::vector<std::string>{"0:0301"}));
            ask(F::Medium, F::None, F::Hard, F::Easy);                                                          // the leader moves itself (Green to Red): the levels of both colors trade places, and the START names the seat that the leader holds now
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
            Screen screen{room.host.room()};
            const auto send = [&](uint8_t from, uint8_t to, bool done) { room.guests[ann].client_end->send(screen.press(from, to, done)); };
            for (int i = 0; i < 10; ++i) send(3, 1, false);                                                     // nobody sits in Black (seat 3): ignored and counted, ten times
            send(1, 3, true);                                                                                   // Bob to Black: done, with a budget that the ten did not touch
            for (int i = 0; i < 5; ++i) send(i % 2 == 0 ? 3 : 1, i % 2 == 0 ? 1 : 3, true);                     // five more, Bob there and back: the six of the burst are spent, Bob is on Red again
            send(3, 0, false);                                                                                  // nobody sits in Black now: ignored and counted, not dropped
            send(1, 3, false);                                                                                  // a seventh move that could be done: dropped, counted by nobody
            room.run(300);
            ASSERT_EQ(room.host.seat_moves(), kSeatMoveBurst);
            ASSERT_EQ(room.host.ignored_seat_moves(), 11u);
            ASSERT_EQ(layout(room.host.room()), "Ann@0 Bob@1 Cat@2");
            ASSERT_TRUE(room.host.occupied(0) && room.host.leader() == 0 && room.guests[ann].lobby->phase() == ClientLobby::Phase::InRoom);
        }
        {   // a leader who gave no name is "Player 1" in the room's notice (the word for a player without a name that the lobby and the application use; the chat's own lines say "Seat 1")
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
            lan.guests[bob].client_end->send(encode(SeatMoveMsg{1, 2, seating_hash(lan.host.room())}));
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
            for (int i = 0; i < 100; ++i) lan.guests[bob].client_end->send(encode(SeatMoveMsg{1, 2, seating_hash(lan.host.room())}));
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
            room.guests[ann].client_end->send(encode(SeatMoveMsg{0, 2, seating_hash(room.host.room())}));
            room.run(300);
            ASSERT_TRUE(room.host.seat_moves() == 0u && room.host.ignored_seat_moves() == 1u && layout(room.host.room()) == "Ann@0 Bob@1");
        }
    } TEST_END();
}
