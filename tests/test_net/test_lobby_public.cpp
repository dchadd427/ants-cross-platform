// Tests of the room before a match: public lobby rooms (network protocol 22, docs/NETWORK_PORT.md "Protocol 22"): the leader's Visibility and who is heard, its budget, the idle rule that makes an unused public room
// private, the colors that a listing is made of, and the number of the protocol (N4.50 - N4.54)
#include "lobby_fixtures.hpp"

namespace {

// The Room messages that this guest has heard since the last call (a ping, a pong and a line of chat are not)
size_t rooms_heard(Room& room, size_t who) {
    size_t n = 0;
    for (const ClientLobby::Event& e : room.guests[who].lobby->take_events()) n += e.type == ClientLobby::Event::Type::RoomChanged ? 1u : 0u;
    return n;
}

// Every guest that is in the room has heard, in its last Room message, that the room is public (or private)
bool everyone_sees(Room& room, bool is_public) {
    for (auto& g : room.guests) {
        if (g.lobby->phase() == ClientLobby::Phase::InRoom && g.lobby->room_is_public() != is_public) return false;
    }
    return true;
}

// Throws the events away that the guests have not taken yet (the Room messages of the joins and of the first thumbs)
void settle(Room& room) {
    room.run(3000);
    for (size_t i = 0; i < room.guests.size(); ++i) rooms_heard(room, i);
}

// A page's Hello (a raw one: its answer can be read from the client end), and the type of the first message that came back (the Welcome 2, the Reject 3), 0 when none
uint8_t answer_type(Room& room, const HelloMsg& hello) {
    const RawClient c = raw_hello(room, hello);
    room.run(100);
    const std::vector<std::vector<uint8_t>> got = drain_messages(c.end);
    return got.empty() ? uint8_t{0} : got[0][0];
}

}  // namespace

void run_lobby_public_tests() {
    TEST_CASE("N4.50 Public Rooms (Protocol 22): A New Lobby Room Is Private; The Leader's Visibility Makes It Public Or Private And Everybody Is Shown The Flag; Anybody Else's Is Ignored And Counted, Not Even Sent By A Client; Saying Public To A Public Room Is Use Of The Room And Shows Nothing; The Flag Is The Room's: A Color Move, A Held Seat And A Seat Taken Over Leave It; A Room That Is No Lobby Room Is Never Public") {
        {   // a new room is private, whoever is in it, and nobody has said anything about it
            Room room(lobby_room_config(201));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(500);
            ASSERT_FALSE(room.host.is_public());
            ASSERT_FALSE(room.host.room().is_public());
            ASSERT_TRUE(everyone_sees(room, false) && !room.guests[ann].lobby->room_is_public());
            ASSERT_EQ(room.host.listed_age_ms(room.now), 0u);
            ASSERT_TRUE(room.host.visibility_acts() == 0 && room.host.ignored_visibility() == 0 && room.host.unlist_count() == 0);
        }
        {   // only the leader is heard: Bob's message is ignored and counted (the client does not even send it), the leader's is done and shown to everybody
            Room room(lobby_room_config(202));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->is_leader() && !room.guests[bob].lobby->is_leader());
            ASSERT_FALSE(room.guests[bob].lobby->request_visibility(true));
            room.guests[bob].client_end->send(encode(VisibilityMsg{true}));
            room.run(300);
            ASSERT_EQ(room.host.ignored_visibility(), 1u);
            ASSERT_TRUE(!room.host.is_public() && room.host.visibility_acts() == 0 && everyone_sees(room, false));
            const uint32_t before = room.host.activity();
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(100);
            ASSERT_TRUE(room.host.is_public() && room.host.room().is_public() && everyone_sees(room, true));
            ASSERT_TRUE(room.guests[bob].lobby->room_is_public() && room.guests[cat].lobby->room_is_public());
            ASSERT_EQ(room.host.visibility_acts(), 1u);
            ASSERT_EQ(room.host.activity(), before + 1u);                                         // (the choice is use of the room)
            ASSERT_EQ(room.host.ignored_visibility(), 1u);
            const uint32_t age = room.host.listed_age_ms(room.now);
            ASSERT_TRUE(age > 0 && age <= 100);
            room.run(1000);
            ASSERT_EQ(room.host.listed_age_ms(room.now), age + 1000);                              // (listed since the leader's message; it grows with the clock)
            // Bob says private to a public room: ignored too, the room stays as the leader made it
            room.guests[bob].client_end->send(encode(VisibilityMsg{false}));
            room.run(100);
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            ASSERT_EQ(room.host.ignored_visibility(), 2u);
            // the leader says private: everybody is told, the listing time is gone
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(false));
            room.run(100);
            ASSERT_TRUE(!room.host.is_public() && everyone_sees(room, false) && room.host.listed_age_ms(room.now) == 0);
            ASSERT_EQ(room.host.visibility_acts(), 2u);
            ASSERT_EQ(room.host.activity(), before + 2u);
        }
        {   // "public" said to a public room shows nothing (no Room message) and is use of the room; "private" said to a private room is nothing at all
            Room room(lobby_room_config(203));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(false));                        // (private to a private room)
            room.run(200);
            ASSERT_TRUE(room.host.visibility_acts() == 0 && rooms_heard(room, bob) == 0);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_EQ(room.host.visibility_acts(), 1u);
            ASSERT_EQ(rooms_heard(room, bob), 1u);
            const uint32_t listed = room.host.listed_age_ms(room.now);
            const uint32_t activity = room.host.activity();
            room.run(1200);                                                                        // (a second passes: the budget has a whole message again)
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_EQ(room.host.visibility_acts(), 2u);
            ASSERT_EQ(room.host.activity(), activity + 1u);
            ASSERT_EQ(rooms_heard(room, bob), 0u);                                                 // (nobody is told anything: nothing changed)
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            ASSERT_EQ(room.host.listed_age_ms(room.now), listed + 1400);                           // (and the room was not listed again: it has been public since the first message)
        }
        {   // the leader who goes takes the choice with it: the next one leads a private room, is heard, and chooses for itself
            Room room(lobby_room_config(204));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const size_t cat = room.join_seat("Cat");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            room.guests[ann].lobby->leave();
            room.run(300);
            ASSERT_TRUE(room.guests[bob].lobby->is_leader());
            ASSERT_TRUE(!room.host.is_public() && !room.host.room().is_public() && everyone_sees(room, false));      // (Bob never chose it)
            ASSERT_TRUE(room.host.listed_age_ms(room.now) == 0 && room.host.unlist_count() == 0);
            room.guests[cat].client_end->send(encode(VisibilityMsg{true}));                        // (Cat does not lead)
            room.run(100);
            ASSERT_TRUE(!room.host.is_public() && room.host.ignored_visibility() == 1);
            ASSERT_TRUE(room.guests[bob].lobby->request_visibility(true));
            room.run(100);
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            ASSERT_TRUE(room.guests[bob].lobby->request_visibility(false));
            room.run(100);
            ASSERT_TRUE(!room.host.is_public() && everyone_sees(room, false));
        }
        {   // the flag is the room's: a color move, a seat that is held and a seat that its key takes over change nothing of it
            Room room(lobby_room_config(205));
            const size_t ann = room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
            room.run(200);
            ASSERT_EQ(room.host.seat_moves(), 1u);
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            const SeatKey bob_key = room.guests[bob].lobby->key();
            room.guests[bob].client_end->close();
            room.run(200);
            ASSERT_TRUE(room.host.held(3) && room.host.is_public() && room.guests[ann].lobby->room_is_public());
            const size_t bob2 = join_keyed(room, "Bob", bob_key);                                   // (the person comes back: the Room message that it is given says public)
            room.run(300);
            ASSERT_TRUE(room.guests[bob2].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[bob2].lobby->room_is_public());
            ASSERT_TRUE(room.host.is_public() && room.host.unlist_count() == 0);
        }
        {   // the leader's connection ends and the seat is held: the person still leads and the room is still public (for as long as the seat is held); a newcomer does not change it; the key brings the person back to it
            Room room(lobby_room_config(209));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            const SeatKey ann_key = room.guests[ann].lobby->key();
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            room.guests[ann].client_end->close();
            room.run(200);
            ASSERT_TRUE(room.host.held(ann_seat) && room.host.leader() == ann_seat && room.host.is_public());
            const size_t cat = room.join_seat("Cat");                                                // (a newcomer is welcomed: the earliest person still leads)
            room.run(300);
            ASSERT_TRUE(room.host.leader() == ann_seat && room.host.is_public() && room.guests[cat].lobby->room_is_public());
            const size_t ann2 = join_keyed(room, "Ann", ann_key);
            room.run(300);
            ASSERT_TRUE(room.guests[ann2].lobby->is_leader() && room.guests[ann2].lobby->room_is_public() && room.host.is_public());
            ASSERT_TRUE(room.host.visibility_acts() == 1 && room.host.unlist_count() == 0);
        }
        {   // the leader is thrown out (eight messages that are no Visibility at all): the next person leads a room that it never made public, and nobody is told that it is public
            Room room(lobby_room_config(210));
            const size_t ann = room.join_seat("Ann");
            const size_t bea = room.join_seat("Bea");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_TRUE(room.host.is_public() && everyone_sees(room, true));
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const std::vector<uint8_t> garbage = {static_cast<uint8_t>(MsgType::Visibility), 7};      // (the value is 0 or 1)
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(garbage);
            room.run(100);
            ASSERT_TRUE(room.host.occupied(ann_seat) && room.host.is_public());
            room.guests[ann].client_end->send(garbage);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_TRUE(room.guests[bea].lobby->is_leader());
            ASSERT_TRUE(!room.host.is_public() && !room.host.room().is_public() && room.host.listed_age_ms(room.now) == 0);
            ASSERT_TRUE(!room.guests[bea].lobby->room_is_public() && room.guests[bea].lobby->room().flags == (kRoomLeaderStarts | kRoomLobby));
        }
        {   // the only person leaves a public room: it is private, and the stranger who is welcomed next (the seat is free, the room lives on) leads a private room and sees it so
            Room room(lobby_room_config(214));
            const size_t ann = room.join_seat("Ann");
            settle(room);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_TRUE(room.host.is_public());
            room.guests[ann].lobby->leave();
            room.run(300);
            ASSERT_TRUE(room.host.leader() == kNoLeader && !room.host.is_public() && room.host.listed_age_ms(room.now) == 0);
            const size_t eve = room.join_seat("Eve");
            room.run(300);
            ASSERT_TRUE(room.guests[eve].lobby->is_leader());
            ASSERT_TRUE(!room.host.is_public() && !room.guests[eve].lobby->room_is_public() && room.guests[eve].lobby->room().flags == (kRoomLeaderStarts | kRoomLobby));
            ASSERT_TRUE(room.host.visibility_acts() == 1 && room.host.activity() >= 1u);
        }
        {   // a room that is no lobby room is never public: a client does not send the message, the room takes it for garbage (eight throw the sender out) and counts nothing
            HostLobby::Config hc = lobby_room_config(206);
            hc.lobby_room = false;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            ASSERT_FALSE(room.guests[ann].lobby->request_visibility(true));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(encode(VisibilityMsg{true}));
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            room.guests[ann].client_end->send(encode(VisibilityMsg{true}));
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));
            ASSERT_TRUE(!room.host.is_public() && room.host.visibility_acts() == 0 && room.host.ignored_visibility() == 0);
        }
        {   // while the leader's START waits for the games nobody is heard (the leader included): counted, free up to kIgnoredVisibilityAllowed, then a violation each (eight throw the sender out); the choice that was made before stands
            HostLobby::Config hc = lobby_room_config(207);
            hc.start_wait_ms = 3600u * 1000u;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(100);
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(200);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting() && room.host.is_public() && room.guests[pia].lobby->room().starting() && room.guests[pia].lobby->room_is_public());      // (public and starting: the room is not listed, the flag is still the leader's)
            ASSERT_FALSE(room.guests[ann].lobby->request_visibility(false));                       // (the client sends nothing that the room ignores)
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            const std::vector<uint8_t> wish = encode(VisibilityMsg{false});
            room.guests[ann].client_end->send(wish);
            room.run(100);
            ASSERT_TRUE(room.host.is_public() && room.host.ignored_visibility() == 1 && room.host.visibility_acts() == 1);
            for (uint32_t i = 1; i < kIgnoredVisibilityAllowed; ++i) room.guests[ann].client_end->send(wish);
            room.run(300);
            ASSERT_EQ(room.host.ignored_visibility(), kIgnoredVisibilityAllowed);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            for (int i = 0; i < 7; ++i) room.guests[ann].client_end->send(wish);
            room.run(300);
            ASSERT_TRUE(room.host.occupied(ann_seat));                                             // seven violations are not enough
            room.guests[ann].client_end->send(wish);
            room.run(300);
            ASSERT_FALSE(room.host.occupied(ann_seat));                                            // the eighth
            ASSERT_EQ(room.host.visibility_acts(), 1u);
            ASSERT_TRUE(!room.host.is_public() && !room.guests[pia].lobby->room_is_public());      // (Pia leads now and chose nothing: the room is private)
        }
        {   // a match that loads hears no Visibility either: a stray one is counted without offence
            Room room(lobby_room_config(208));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(100);
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.host.phase(), HostLobby::Phase::Loading);
            ASSERT_FALSE(room.guests[ann].lobby->request_visibility(true));
            room.guests[ann].client_end->send(encode(VisibilityMsg{true}));
            room.run(100);
            ASSERT_TRUE(room.host.ignored_visibility() == 1 && !room.host.is_public() && room.host.visibility_acts() == 0);
        }
    } TEST_END();

    TEST_CASE("N4.51 The Budget Of The Leader's Visibility (Protocol 22): A Burst Of 3, Then 1 A Second; One Beyond It Is Dropped, 6 More Are Tolerated And Each One After That Is A Violation (Eight Throw The Leader Out); The Budget Refills; A Guest Who Is Not The Leader Is Not Heard At All And Is Forgiven One Ignored Message A Minute Like The Other Kinds") {
        {   // the budget of the leader's choices: a burst of kVisibilityBurst, then kVisibilityPerSecond a second
            Room room(lobby_room_config(211));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            const uint8_t ann_seat = room.guests[ann].lobby->my_seat();
            ASSERT_TRUE(kVisibilityBurst == 3 && kVisibilityPerSecond == 1 && kVisibilityExcessBurst == 6);
            const auto say = [&](unsigned i) { room.guests[ann].client_end->send(encode(VisibilityMsg{i % 2 == 0})); };      // (public, private, public, ...)
            for (unsigned i = 0; i < kVisibilityBurst + kVisibilityExcessBurst; ++i) say(i);       // (every batch is read in the pass after it: the budget refills a message a second, so no pause between them)
            room.run(10);
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst);                              // the first three were done: public, private, public
            ASSERT_TRUE(room.host.is_public() && room.host.occupied(ann_seat));
            for (unsigned i = 0; i < 7; ++i) say(1);                                               // the excess is used up: each one is an offence
            room.run(10);
            ASSERT_TRUE(room.host.occupied(ann_seat));
            say(1);
            room.run(10);
            ASSERT_FALSE(room.host.occupied(ann_seat));                                            // the eighth throws the leader out, as it does for any guest
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst);
        }
        {   // a message beyond the burst is dropped, and a second later there is a whole one again: the choices at a pace of one a second are all heard, for as long as it goes on
            Room room(lobby_room_config(212));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(100);
            for (unsigned i = 0; i < kVisibilityBurst + 1; ++i) room.guests[ann].client_end->send(encode(VisibilityMsg{i % 2 == 0}));
            room.run(10);
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst);                              // (the fourth was dropped: the room is as the third left it, public)
            ASSERT_TRUE(room.host.is_public());
            room.run(500);                                                                          // half a second: only half a message is back, so a fifth is dropped too (the rate is one a second)
            room.guests[ann].client_end->send(encode(VisibilityMsg{false}));
            room.run(10);
            ASSERT_TRUE(room.host.visibility_acts() == kVisibilityBurst && room.host.is_public());
            room.run(500);                                                                          // a second since the burst: a message is back
            room.guests[ann].client_end->send(encode(VisibilityMsg{false}));
            room.run(10);
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst + 1);
            ASSERT_FALSE(room.host.is_public());
            for (int i = 0; i < 20; ++i) {                                                         // a leader whose page repeats it every second, for as long as it likes, is never an offence
                room.run(1010);
                room.guests[ann].client_end->send(encode(VisibilityMsg{i % 2 == 1}));
                room.run(10);
            }
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst + 1 + 19);                       // (the first of the twenty says private to a private room: nothing)
            ASSERT_TRUE(room.host.occupied(room.guests[ann].lobby->my_seat()));
        }
        {   // a guest who is not the leader: ignored and counted, free up to kIgnoredVisibilityAllowed, then a violation each; a minute forgives one of the counts, as for the other kinds (N4.34)
            Room room(lobby_room_config(213));
            room.join_seat("Ann");
            const size_t bob = room.join_seat("Bob");
            const uint8_t bob_seat = room.guests[bob].lobby->my_seat();
            const std::vector<uint8_t> wish = encode(VisibilityMsg{true});
            for (uint32_t i = 0; i < kIgnoredVisibilityAllowed; ++i) room.guests[bob].client_end->send(wish);
            room.run(300);
            ASSERT_EQ(room.host.ignored_visibility(), kIgnoredVisibilityAllowed);
            ASSERT_TRUE(room.host.occupied(bob_seat) && !room.host.is_public());
            room.run(65000);                                                                        // a minute: one count is forgiven (15 of the free 16 are used)
            for (int i = 0; i < 8; ++i) room.guests[bob].client_end->send(wish);                    // the first of these is the sixteenth, free; the next seven are violations, one short of the limit
            room.run(300);
            ASSERT_TRUE(room.host.occupied(bob_seat));
            room.guests[bob].client_end->send(wish);                                                // the eighth violation
            room.run(300);
            ASSERT_FALSE(room.host.occupied(bob_seat));
            ASSERT_TRUE(!room.host.is_public() && room.host.visibility_acts() == 0);
        }
        {   // the budget is the Visibility's own: the Plans that the leader has made use up the Plans' and none of it (six Plans, then a Visibility is still heard), and three Visibility messages use up theirs and none of the Plans'
            using K = PlanKind;
            Room room(lobby_room_config(215));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(100);
            ASSERT_TRUE(kPlanBurst == 6);
            for (unsigned i = 0; i < kPlanBurst; ++i) {                                              // (Easy, Open, Easy, ...: each one changes the room)
                ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, i % 2 == 0 ? K::Easy : K::Open})));
            }
            room.run(10);
            ASSERT_EQ(room.host.plan_changes(), kPlanBurst);                                         // (all six were heard: the Plans' burst)
            ASSERT_TRUE(room.guests[ann].lobby->request_visibility(true));
            room.run(10);
            ASSERT_TRUE(room.host.visibility_acts() == 1 && room.host.is_public());                  // (and the Visibility has a burst of its own, though the Plans have none left)
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Hard})));
            room.run(10);
            ASSERT_EQ(room.host.plan_changes(), kPlanBurst);                                         // (the seventh Plan has no budget left: dropped)
        }
        {
            using K = PlanKind;
            Room room(lobby_room_config(216));
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(100);
            for (unsigned i = 0; i < kVisibilityBurst; ++i) room.guests[ann].client_end->send(encode(VisibilityMsg{i % 2 == 0}));
            room.run(10);
            ASSERT_EQ(room.host.visibility_acts(), kVisibilityBurst);                                // (public, private, public: the whole burst)
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, K::Easy})));
            room.run(10);
            ASSERT_EQ(room.host.plan_changes(), 1u);                                                 // (the Plans' budget is whole)
            for (unsigned i = 1; i < kPlanBurst; ++i) ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Open, K::Open, i % 2 == 0 ? K::Easy : K::Hard})));
            room.run(10);
            ASSERT_EQ(room.host.plan_changes(), kPlanBurst);                                         // (... for all of its six)
            room.guests[ann].client_end->send(encode(VisibilityMsg{false}));
            room.run(10);
            ASSERT_TRUE(room.host.visibility_acts() == kVisibilityBurst && room.host.is_public());   // (a fourth Visibility is beyond the Visibility's burst: dropped)
        }
    } TEST_END();

    TEST_CASE("N4.52 The Idle Rule (Protocol 22): A Public Room In Which Nothing Has Been Done For 30 Minutes (activity() Has Not Moved: A Join, A Color Move, A Plan Or A Name, A Removal, A Line Of Chat, A START, A Visibility That Was Done) Becomes Private By Itself And Everybody Is Told; Pings, A Link That Comes Back And What Is Ignored Are Not Use; Public Said To A Public Room Is, And Keeps It; Across The 32-Bit Clock; A Listing's Age Stops Growing At 30 Days") {
        const uint32_t kStep = 5000;
        const uint64_t kHalfHour = 30u * 60u * 1000u;
        ASSERT_EQ(kLobbyPublicIdleMs, 30u * 60u * 1000u);
        ASSERT_EQ(HostLobby::Config{}.public_idle_ms, kLobbyPublicIdleMs);
        // a room of Ann (leads), Bob and Cat that Ann made public `lead` ms of coarse steps ago; the clock starts at `start`
        const auto public_room = [&](Room& room, uint32_t start) {
            room.now = start;
            room.join_seat("Ann");
            room.join_seat("Bob");
            room.join_seat("Cat");
            room.run(3000);
            ASSERT_TRUE(room.guests[0].lobby->request_visibility(true));
            run_coarse(room, kStep, kStep);                                                         // (the message is heard in this step: the clock of the room's idle time starts here)
            ASSERT_TRUE(room.host.is_public());
        };
        for (const uint32_t start : {0u, 0x7FFFF000u - 600000u, 0xFFFFFFFFu - 600000u}) {         // from a young clock, across the signed half of the 32-bit clock, and across the wrap
            {   // nothing is done: public for 30 minutes to the step, then private, and the room is told
                Room room(lobby_room_config(220));
                public_room(room, start);
                run_coarse(room, kHalfHour - kStep, kStep);                                         // 29 minutes 55 seconds after the room became public
                ASSERT_TRUE(room.host.is_public() && room.host.unlist_count() == 0);
                run_coarse(room, kStep, kStep);                                                     // the step in which the 30 minutes are full
                ASSERT_FALSE(room.host.is_public());
                ASSERT_EQ(room.host.unlist_count(), 1u);
                run_coarse(room, kStep, kStep);
                ASSERT_TRUE(everyone_sees(room, false) && !room.host.room().is_public());
                ASSERT_EQ(room.host.listed_age_ms(room.now), 0u);
                run_coarse(room, 5 * kHalfHour, kStep);                                             // (it does not flip again, and stays private)
                ASSERT_TRUE(room.host.unlist_count() == 1 && !room.host.is_public());
                // the leader makes it public again: a new listing, a new 30 minutes
                ASSERT_TRUE(room.guests[0].lobby->request_visibility(true));
                run_coarse(room, kStep, kStep);
                ASSERT_TRUE(room.host.is_public() && room.host.listed_age_ms(room.now) == 0);          // (listed again, from this step)
                run_coarse(room, kStep, kStep);
                ASSERT_TRUE(everyone_sees(room, true));
                run_coarse(room, kHalfHour - 2 * kStep, kStep);
                ASSERT_TRUE(room.host.is_public());
                run_coarse(room, kStep, kStep);
                ASSERT_TRUE(!room.host.is_public() && room.host.unlist_count() == 2);
            }
            {   // "public" said again to a public room is the page looking at it: it restarts the 30 minutes (and not the listing's age)
                Room room(lobby_room_config(221));
                public_room(room, start);
                run_coarse(room, 20 * 60000, kStep);
                const uint32_t age = room.host.listed_age_ms(room.now);
                ASSERT_TRUE(room.guests[0].lobby->request_visibility(true));
                run_coarse(room, kStep, kStep);
                ASSERT_EQ(room.host.visibility_acts(), 2u);
                ASSERT_EQ(room.host.listed_age_ms(room.now), age + kStep);
                run_coarse(room, kHalfHour - kStep, kStep);                                         // 29 minutes 55 seconds after the refresh (49 minutes 55 seconds after the first choice)
                ASSERT_TRUE(room.host.is_public());
                run_coarse(room, kStep, kStep);
                ASSERT_FALSE(room.host.is_public());
            }
            {   // a page that repeats it every ten minutes keeps the room public for hours
                Room room(lobby_room_config(222));
                public_room(room, start);
                for (int i = 0; i < 24; ++i) {                                                       // four hours
                    ASSERT_TRUE(room.guests[0].lobby->request_visibility(true));
                    run_coarse(room, 10 * 60000, kStep);
                    ASSERT_TRUE(room.host.is_public());
                }
                ASSERT_EQ(room.host.unlist_count(), 0u);
                ASSERT_EQ(room.host.listed_age_ms(room.now), 24u * 10u * 60000u);                    // (listed once, at the first choice)
            }
        }
        {   // what is use: each of these, done with a minute to go, keeps the room public for 30 minutes more, to the step
            struct Use {
                const char* what;
                std::function<void(Room&)> act;
            };
            const std::vector<Use> uses = {
                {"a line of chat", [](Room& r) { r.guests[1].lobby->chat("hello"); }},
                {"a rename", [](Room& r) { r.guests[1].lobby->request_name("Bobby"); }},
                {"a plan", [](Room& r) { r.guests[0].lobby->request_plan(plan_of({PlanKind::Open, PlanKind::Hard, PlanKind::Open, PlanKind::Open})); }},
                {"a color move", [](Room& r) { r.guests[0].lobby->request_seat_move(2, 3); }},
                {"a newcomer", [](Room& r) { r.join("Dan"); }},
                {"a removal", [](Room& r) { r.guests[0].lobby->request_remove(2); }},
                {"the leader's START", [](Room& r) { r.guests[0].lobby->request_start(); }},
            };
            for (const Use& use : uses) {
                HostLobby::Config hc = lobby_room_config(223);
                hc.start_wait_ms = 3600u * 1000u;
                Room room(hc);
                public_room(room, 1000);
                run_coarse(room, kHalfHour - 60000, kStep);                                         // 29 minutes after the room became public
                ASSERT_MSG(room.host.is_public(), use.what);
                const uint32_t before = room.host.activity();
                use.act(room);
                for (int i = 0; i < 6 && room.host.activity() == before; ++i) run_coarse(room, kStep, kStep);      // (the step in which the room heard it; a newcomer's Hello takes a few)
                ASSERT_MSG(room.host.activity() == before + 1, use.what);
                run_coarse(room, kHalfHour - kStep, kStep);                                         // just short of 30 minutes after the act
                ASSERT_MSG(room.host.is_public(), use.what);
                run_coarse(room, kStep, kStep);
                ASSERT_MSG(!room.host.is_public(), use.what);
            }
        }
        {   // what is not use: a ping and a pong, a message that the room ignores (a guest who does not lead says "public" every five minutes), a link that ends and comes back with its key; the room still goes private on time
            Room room(lobby_room_config(224));
            public_room(room, 5000);
            const SeatKey bob_key = room.guests[1].lobby->key();
            const uint32_t before = room.host.activity();
            for (int i = 0; i < 6; ++i) {
                room.guests[2].client_end->send(encode(VisibilityMsg{true}));                        // (Cat does not lead)
                run_coarse(room, 5 * 60000, kStep);
                if (i < 5) ASSERT_TRUE(room.host.is_public());
                if (i == 1) {                                                                        // Bob's link ends; the key brings the person back a little later
                    room.guests[1].client_end->close();
                    run_coarse(room, kStep, kStep);
                    join_keyed(room, "Bob", bob_key);
                }
            }
            ASSERT_EQ(room.host.takeovers(), 1u);
            ASSERT_EQ(room.host.ignored_visibility(), 6u);
            ASSERT_EQ(room.host.activity(), before);
            ASSERT_FALSE(room.host.is_public());                                                     // (more than 30 minutes since the leader's choice)
        }
        {   // a private room has nothing to flip, and public_idle_ms 0 never flips a public one
            Room quiet(lobby_room_config(225));
            quiet.join_seat("Ann");
            quiet.join_seat("Bob");
            run_coarse(quiet, 3 * kHalfHour, kStep);
            ASSERT_TRUE(!quiet.host.is_public() && quiet.host.unlist_count() == 0);
            HostLobby::Config hc = lobby_room_config(226);
            hc.public_idle_ms = 0;
            Room room(hc);
            public_room(room, 0);
            run_coarse(room, 10 * kHalfHour, kStep);
            ASSERT_TRUE(room.host.is_public() && room.host.unlist_count() == 0);
        }
        {   // the age of a listing stops growing at kLobbyListedAgeCapMs, so that the 32-bit clock cannot read a room that has been public for 49.7 days or more as a young one: 60 days of a page that repeats "public" every ten minutes
            Room room(lobby_room_config(227));
            public_room(room, 0xFFFFF000u);
            for (int i = 0; i < 60 * 144; ++i) {
                room.guests[0].lobby->request_visibility(true);
                run_coarse(room, 10 * 60000, 10 * 60000);
            }
            ASSERT_TRUE(room.host.is_public() && room.host.unlist_count() == 0);
            ASSERT_EQ(room.host.listed_age_ms(room.now), kLobbyListedAgeCapMs);
            ASSERT_EQ(kLobbyListedAgeCapMs, 30u * 24u * 3600u * 1000u);
        }
    } TEST_END();

    TEST_CASE("N4.53 The Colors Of A Listing (Protocol 22): people + bots + open colors Are The Colors Of The Match, Whatever The Plan Is And Whoever Sits Where; A Held Seat Is A Person Who Is Not Connected; An Open Color Is Exactly One That A Newcomer Takes, And A Room With None Says Full") {
        {   // by hand: one person, a plan with an Easy bot, a Nobody and an Open color
            Room room(lobby_room_config(231));
            const size_t ann = room.join_seat("Ann");
            room.run(300);
            ASSERT_TRUE(room.host.connected() == 1 && room.host.people_colors() == 1 && room.host.open_colors() == 3 && room.host.bot_colors() == 0 && room.host.match_colors() == 4);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({PlanKind::Open, PlanKind::Easy, PlanKind::Nobody, PlanKind::Open})));
            room.run(300);
            ASSERT_TRUE(room.host.people_colors() == 1 && room.host.bot_colors() == 1 && room.host.open_colors() == 1 && room.host.match_colors() == 3);
            const size_t bob = room.join_seat("Bob");                                                 // (Bob takes the lowest Open color that nobody holds: the plan's last, 3)
            ASSERT_EQ(room.guests[bob].lobby->my_seat(), 3);
            ASSERT_TRUE(room.host.connected() == 2 && room.host.people_colors() == 2 && room.host.bot_colors() == 1 && room.host.open_colors() == 0 && room.host.match_colors() == 3);
            // the leader gives the color that Bob holds to a bot and the empty bot color to Nobody: Bob plays on the color of the plan, which keeps its kind underneath, and is no bot color
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({PlanKind::Open, PlanKind::Nobody, PlanKind::Nobody, PlanKind::Hard})));
            room.run(300);
            ASSERT_TRUE(room.host.people_colors() == 2 && room.host.bot_colors() == 0 && room.host.open_colors() == 0 && room.host.match_colors() == 2);
            // a person on a color that the plan calls Nobody still plays: it is in the match
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({PlanKind::Nobody, PlanKind::Open, PlanKind::Open, PlanKind::Open})));
            room.run(300);
            ASSERT_TRUE(room.host.people_colors() == 2 && room.host.open_colors() == 2 && room.host.bot_colors() == 0 && room.host.match_colors() == 4);
            // Bob's link ends: the seat is held, the person counts, the connection does not (not even in the moment before the room has looked: a link that is closed is nobody to write to)
            room.guests[bob].frozen = true;
            room.run(100);
            room.guests[bob].client_end->close();
            ASSERT_TRUE(room.host.connected() == 1 && !room.host.held(3));
            room.run(300);
            ASSERT_TRUE(room.host.held(3));
            ASSERT_TRUE(room.host.connected() == 1 && room.host.people_colors() == 2 && room.host.open_colors() == 2 && room.host.match_colors() == 4);
            room.guests[ann].client_end->close();
            room.run(300);
            ASSERT_TRUE(room.host.connected() == 0 && room.host.people_colors() == 2);
        }
        {   // by chance: a random walk of joins, leaves, losses, plans and presses; the colors always add up, connections are people, and a newcomer is seated exactly when an Open color is free
            for (uint32_t seed = 1; seed <= 6; ++seed) {
                Room room(lobby_room_config(240 + seed));
                Lcg rng(seed * 7919u);
                std::vector<size_t> live;
                for (int step = 0; step < 120; ++step) {
                    switch (rng.below(5)) {
                        case 0:
                            if (live.size() < 4) live.push_back(join_with(room, page_config("P" + std::to_string(step))));
                            break;
                        case 1:
                            if (!live.empty()) {
                                const size_t who = live[rng.below(static_cast<uint32_t>(live.size()))];
                                if (rng.below(2) == 0) room.guests[who].lobby->leave();
                                else room.guests[who].client_end->close();                             // (a lost link: the seat is held)
                            }
                            break;
                        case 2:
                        case 3:
                            for (const size_t who : live) {
                                if (room.guests[who].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[who].lobby->is_leader()) {
                                    std::array<PlanKind, sim::MAX_PLAYERS> kinds{};
                                    for (PlanKind& k : kinds) k = static_cast<PlanKind>(rng.below(5));
                                    room.guests[who].lobby->request_plan(plan_of(kinds));
                                    break;
                                }
                            }
                            break;
                        default:
                            room.run(rng.below(2) == 0 ? 1100 : 100);
                            break;
                    }
                    room.run(1100);                                                                    // (a second between presses: the budgets of a plan are not what is tried here)
                    ASSERT_TRUE(room.host.people_colors() + room.host.bot_colors() + room.host.open_colors() == room.host.match_colors());
                    ASSERT_TRUE(room.host.connected() <= room.host.people_colors() && room.host.match_colors() <= sim::MAX_PLAYERS);
                    if (room.host.starting() || room.host.phase() != HostLobby::Phase::Room) continue;
                    HelloMsg probe;
                    probe.name = "Probe";
                    probe.client_kind = kClientPage;
                    const bool free_color = room.host.open_colors() > 0;
                    const uint8_t answer = answer_type(room, probe);
                    ASSERT_TRUE(answer == (free_color ? 2 : 3));                                         // a Welcome when a color is open, a Reject (Full) when none is
                    room.run(1100);
                    live.clear();                                                                       // (the walk goes on with the people that it makes after this: the probe is a person too, and stays)
                    for (size_t g = 0; g < room.guests.size(); ++g) {
                        if (room.guests[g].lobby->phase() == ClientLobby::Phase::InRoom && room.guests[g].client_end->is_open()) live.push_back(g);
                    }
                }
            }
        }
    } TEST_END();

    TEST_CASE("N4.54 The Number Of The Protocol Is 22: A Hello Of 21 Is Refused (VersionMismatch) And Seats Nobody, A Hello Of 22 Is Taken") {
        ASSERT_EQ(kProtocolVersion, uint16_t{22});
        Room room(lobby_room_config(251));
        HelloMsg old_hello;
        old_hello.version = 21;
        old_hello.name = "Old";
        old_hello.client_kind = kClientPage;
        const RawClient c = raw_hello(room, old_hello);
        room.run(100);
        const std::vector<std::vector<uint8_t>> got = drain_messages(c.end);
        RejectMsg reject;
        ASSERT_TRUE(!got.empty() && decode(got[0], reject) && reject.reason == RejectReason::VersionMismatch);
        ASSERT_EQ(room.host.players(), size_t{0});
        HelloMsg hello = old_hello;
        hello.version = kProtocolVersion;
        ASSERT_EQ(answer_type(room, hello), uint8_t{2});
        ASSERT_EQ(room.host.players(), size_t{1});
        HelloMsg newer = old_hello;
        newer.version = 23;
        ASSERT_EQ(answer_type(room, newer), uint8_t{3});
        ASSERT_EQ(room.host.players(), size_t{1});
    } TEST_END();
}
