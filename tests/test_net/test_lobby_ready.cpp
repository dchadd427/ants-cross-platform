// Tests of the room before a match: ready up (network protocol 27, docs/READY_UP.md): who says Ready and who is heard, what the Room message shows, the leader's START that waits for it, the marks that stay when the
// leader changes the plan and the ones that go, the computer players and the games that need no Ready, and the budget (N4.60 - N4.64)
#include "lobby_fixtures.hpp"

namespace {

// A lobby room whose guests are all pages (the front page), the first one the leader; returns the guests' indices
std::vector<size_t> pages(Room& room, const std::vector<std::string>& names) {
    std::vector<size_t> out;
    for (const std::string& name : names) {
        out.push_back(join_with(room, page_config(name)));
        room.run(100);
    }
    return out;
}

// The seats that the room shows as ready, as the digits of the seats ("13"), and what a guest was shown
std::string ready_text(const RoomMsg& r) {
    std::string out;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) out += r.seat_ready(seat) ? std::to_string(static_cast<unsigned>(seat)) : std::string();
    return out;
}

// Every guest of the room is shown the same marks as the room holds
bool all_show(Room& room, const std::string& marks) {
    if (ready_text(room.host.room()) != marks) return false;
    for (auto& g : room.guests) {
        if (g.lobby->phase() == ClientLobby::Phase::InRoom && ready_text(g.lobby->room()) != marks) return false;
    }
    return true;
}

}  // namespace

void run_lobby_ready_tests() {
    TEST_CASE("N4.60 Ready Up (Protocol 27): A Person Who Is A Page And Does Not Lead Says Ready And The Room Shows The Mark To Everybody; The Leader's START Is Not Taken Until Every Such Person Is Ready, The Leader Is Told Whose Ready It Waits For, And Saying The Same Again Changes Nothing") {
        Room room(lobby_room_config(401));
        const std::vector<size_t> g = pages(room, {"Ann", "Bob", "Cat"});
        const size_t ann = g[0], bob = g[1], cat = g[2];
        room.run(500);
        notices_of(*room.guests[ann].lobby);
        ASSERT_TRUE(room.guests[ann].lobby->is_leader() && !room.guests[bob].lobby->is_leader());
        ASSERT_TRUE(all_show(room, "") && !room.host.everyone_ready() && room.host.ready_changes() == 0);
        // nobody is ready: the START is not taken, and the leader is told who it waits for (both of them, by name)
        ASSERT_FALSE(room.guests[ann].lobby->request_ready(true));                       // (the leader does not ready: the client does not even send it)
        ASSERT_TRUE(room.guests[ann].lobby->request_start());
        room.run(100);
        ASSERT_TRUE(!room.host.starting() && !room.guests[ann].lobby->room().starting());
        std::vector<std::string> told = notices_of(*room.guests[ann].lobby);
        ASSERT_TRUE(told.size() == 1 && told[0] == "These players are not ready yet: Bob and Cat.");
        ASSERT_TRUE(notices_of(*room.guests[bob].lobby).empty() && notices_of(*room.guests[cat].lobby).empty());      // (the leader alone is told)
        // Bob says Ready: everybody is shown the mark; the START still waits for Cat, and says so
        ASSERT_TRUE(room.guests[bob].lobby->request_ready(true));
        room.run(100);
        ASSERT_TRUE(all_show(room, "1") && room.host.ready(1) && !room.host.ready(2) && room.host.ready_changes() == 1u);
        ASSERT_TRUE(room.guests[ann].lobby->request_start());
        room.run(100);
        ASSERT_FALSE(room.host.starting());
        told = notices_of(*room.guests[ann].lobby);
        ASSERT_TRUE(told.size() == 1 && told[0] == "Cat is not ready yet.");
        // saying Ready again is nothing: no Room message, no change counted
        room.guests[bob].lobby->take_events();
        room.run(1000);
        ASSERT_TRUE(room.guests[bob].lobby->request_ready(true));
        room.run(100);
        size_t rooms_heard = 0;
        for (const ClientLobby::Event& e : room.guests[bob].lobby->take_events()) rooms_heard += e.type == ClientLobby::Event::Type::RoomChanged ? 1u : 0u;
        ASSERT_TRUE(rooms_heard == 0 && room.host.ready_changes() == 1u && room.host.ignored_ready() == 0u);
        // Cat says Ready: the START is taken, and everybody is shown that it waits for the games
        ASSERT_TRUE(room.guests[cat].lobby->request_ready(true));
        room.run(100);
        ASSERT_TRUE(all_show(room, "12") && room.host.everyone_ready());
        ASSERT_TRUE(room.guests[ann].lobby->request_start());
        room.run(100);
        ASSERT_TRUE(room.host.starting() && room.guests[bob].lobby->room().starting() && room.guests[cat].lobby->room().starting());
        ASSERT_TRUE(notices_of(*room.guests[ann].lobby).empty());
        // a person can take the mark back, and the START then waits again (while no START waits)
        Room again(lobby_room_config(402));
        const std::vector<size_t> h = pages(again, {"Ann", "Bob"});
        ASSERT_TRUE(again.guests[h[1]].lobby->request_ready(true));
        again.run(100);
        ASSERT_TRUE(all_show(again, "1"));
        again.run(1000);
        ASSERT_TRUE(again.guests[h[1]].lobby->request_ready(false));
        again.run(100);
        ASSERT_TRUE(all_show(again, "") && !again.host.everyone_ready());
        ASSERT_TRUE(again.guests[h[0]].lobby->request_start());
        again.run(100);
        ASSERT_FALSE(again.host.starting());
        // the room's own rule, not the page's: a START sent as raw bytes is refused the same way
        again.guests[h[0]].client_end->send(encode(StartRequestMsg{}));
        again.run(100);
        ASSERT_FALSE(again.host.starting());
    } TEST_END();

    TEST_CASE("N4.61 Ready Up (Protocol 27): The Leader Does Not Ready (A Ready From The Leader Is Ignored And Counted, Never Shown, And The START Does Not Wait For Them); A Person Who Becomes The Leader Starts With No Mark; A Mark Goes With Its Person When The Colors Change Places") {
        Room room(lobby_room_config(403));
        const std::vector<size_t> g = pages(room, {"Ann", "Bob", "Cat"});
        const size_t ann = g[0], bob = g[1], cat = g[2];
        room.run(500);
        // the leader's Ready as raw bytes: ignored and counted, no mark, and the START does not wait for it
        room.guests[ann].client_end->send(encode(ReadyMsg{true}));
        room.run(100);
        ASSERT_TRUE(room.host.ignored_ready() == 1u && all_show(room, "") && !room.host.ready(0) && room.host.ready_changes() == 0u);
        ASSERT_TRUE(room.guests[bob].lobby->request_ready(true) && room.guests[cat].lobby->request_ready(true));
        room.run(100);
        ASSERT_TRUE(all_show(room, "12") && room.host.everyone_ready());                  // (everybody but the leader is ready: that is all)
        // a mark goes with its person: the leader puts Bob (ready) in the free color 3, and then swaps Bob with Cat
        ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(1, 3));
        room.run(100);
        ASSERT_TRUE(layout(room.host.room()) == "Ann@0 Cat@2 Bob@3" && all_show(room, "23"));
        room.run(1000);
        ASSERT_TRUE(room.guests[ann].lobby->request_seat_move(3, 2));
        room.run(100);
        ASSERT_TRUE(layout(room.host.room()) == "Ann@0 Bob@2 Cat@3" && all_show(room, "23"));
        // the leader leaves: Bob (ready) leads now and starts with no mark, Cat's mark stays, and the START waits for nobody else
        room.guests[ann].lobby->leave();
        room.run(300);
        ASSERT_TRUE(room.guests[bob].lobby->is_leader() && all_show(room, "3") && room.host.everyone_ready() && !room.host.ready(2));
        // Bob (the leader) leaves too: Cat (ready) leads without a mark
        room.guests[bob].lobby->leave();
        room.run(300);
        ASSERT_TRUE(room.guests[cat].lobby->is_leader() && all_show(room, "") && room.host.everyone_ready());
        ASSERT_FALSE(room.host.ready(3));
    } TEST_END();

    TEST_CASE("N4.62 Ready Up (Protocol 27): A Map, A Plan, The Teams And The Mode Chosen By The Leader Leave Every Mark As It Was; A Person Whose Link Ends, Or Who Takes The Seat Over, Is Not Ready; A START That Stands Keeps The Marks") {
        using K = PlanKind;
        {   // the leader changes everything there is to change: the marks stay
            Room room(lobby_room_config(404));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob", "Cat"});
            const size_t ann = g[0], bob = g[1], cat = g[2];
            room.run(500);
            ASSERT_TRUE(room.guests[bob].lobby->request_ready(true) && room.guests[cat].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(all_show(room, "12"));
            PlanMsg plan = plan_of({K::Open, K::Open, K::Open, K::Hard}, 0, 1, "ISLANDS.LVL");
            plan.mode = 1;
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan));
            room.run(100);
            ASSERT_EQ(plan_text(room.host.room()), std::string("oooh 0+1"));
            ASSERT_TRUE(room.host.room().map_name == "ISLANDS.LVL" && room.host.room().mode == 1);
            ASSERT_TRUE(all_show(room, "12") && room.host.everyone_ready() && room.host.ready_changes() == 2u);
            room.run(1000);
            ASSERT_TRUE(room.guests[ann].lobby->request_plan(plan_of({K::Open, K::Nobody, K::Open, K::Easy})));      // (a color that a person holds keeps its kind underneath; the marks stay)
            room.run(100);
            ASSERT_TRUE(all_show(room, "12"));
        }
        {   // a person whose link ends is not ready any more, and says Ready again after coming back with the key; the others keep their marks
            Room room(lobby_room_config(405));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob", "Cat"});
            const size_t ann = g[0], bob = g[1], cat = g[2];
            room.run(500);
            const SeatKey bob_key = room.guests[bob].lobby->key();
            ASSERT_TRUE(room.guests[bob].lobby->request_ready(true) && room.guests[cat].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(all_show(room, "12"));
            room.guests[bob].client_end->close();
            room.run(100);
            ASSERT_TRUE(room.host.held(1) && all_show(room, "2") && !room.host.everyone_ready());
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_FALSE(room.host.starting());
            ASSERT_EQ(notices_of(*room.guests[ann].lobby).size(), size_t{1});
            ClientLobby::Config back = page_config("Bob");
            back.key = bob_key;
            const size_t bob2 = join_with(room, back);
            room.run(300);
            ASSERT_TRUE(!room.host.held(1) && all_show(room, "2"));                          // (back in the seat, not ready)
            ASSERT_TRUE(room.guests[bob2].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(all_show(room, "12") && room.host.everyone_ready());
        }
        {   // a START that stands keeps the marks: the pages go to the game page, their links end, and the marks are not lost
            HostLobby::Config hc = lobby_room_config(406);
            hc.start_wait_ms = 5000;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");                                           // (the leader's own game is open: only Pia's page is left to come)
            const size_t pia = join_with(room, page_config("Pia"));
            room.run(300);
            const SeatKey pia_key = room.guests[pia].lobby->key();
            ASSERT_TRUE(room.guests[pia].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(all_show(room, "1"));
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting() && all_show(room, "1"));
            room.guests[pia].client_end->close();                                              // (the page goes to the game page)
            room.run(100);
            ASSERT_TRUE(room.host.held(1) && room.host.starting() && room.host.everyone_ready() && all_show(room, "1"));       // (a seat that is held while the START waits keeps its mark)
            const size_t pia_game = join_keyed(room, "Pia", pia_key);
            room.run(100);
            ASSERT_TRUE(room.host.in_game(1) && !leader_starts(room.host).empty() && all_show(room, ""));                     // (a game has no mark to show: it needs none)
            ASSERT_TRUE(room.host.start(5, 6, room.now));
            room.run(100);
            ASSERT_EQ(room.guests[pia_game].lobby->phase(), ClientLobby::Phase::Loading);
        }
    } TEST_END();

    TEST_CASE("N4.63 Ready Up (Protocol 27): A Computer Player Is Always Ready (No Mark, Nothing To Wait For); A Person Whose Game Is Open Needs No Ready And Their Ready Is Ignored And Counted; A Person Who Has To Say It Is Never A Leader's Bot, Hold Or Empty Seat") {
        using K = PlanKind;
        {   // the plan's bots are never asked: the leader and a bot start with no Ready at all, and no mark is shown for the bot's color
            Room room(lobby_room_config(407));
            const std::vector<size_t> g = pages(room, {"Ann"});
            ASSERT_TRUE(room.guests[g[0]].lobby->request_plan(plan_of({K::Open, K::Open, K::Hard, K::Medium})));
            room.run(100);
            ASSERT_TRUE(room.host.everyone_ready() && all_show(room, ""));
            ASSERT_TRUE(room.guests[g[0]].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting() && notices_of(*room.guests[g[0]].lobby).empty());
        }
        {   // a person with a bot beside them: only the person is asked
            Room room(lobby_room_config(408));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob"});
            ASSERT_TRUE(room.guests[g[0]].lobby->request_plan(plan_of({K::Open, K::Open, K::Hard, K::Open})));
            room.run(100);
            ASSERT_TRUE(room.guests[g[0]].lobby->request_start());
            room.run(100);
            ASSERT_FALSE(room.host.starting());
            ASSERT_TRUE(notices_of(*room.guests[g[0]].lobby) == std::vector<std::string>{"Bob is not ready yet."});
            ASSERT_TRUE(room.guests[g[1]].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(all_show(room, "1") && ready_text(room.host.room()) == "1");        // (no bit for the bot's color 2)
            ASSERT_TRUE(room.guests[g[0]].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
        }
        {   // a game in the room (a desktop player) needs no Ready: the START is not held back for it, and its own Ready is ignored and counted
            Room room(lobby_room_config(409));
            const size_t ann = join_with(room, page_config("Ann"));
            room.run(100);
            const size_t gus = room.join_seat("Gus");                                           // (a game: Room::join_seat makes a client of kClientGame)
            room.run(300);
            ASSERT_TRUE(room.host.in_game(1) && room.host.everyone_ready());
            ASSERT_FALSE(room.guests[gus].lobby->request_ready(true));                          // (the client does not send it)
            room.guests[gus].client_end->send(encode(ReadyMsg{true}));
            room.run(100);
            ASSERT_TRUE(room.host.ignored_ready() == 1u && all_show(room, "") && room.host.ready_changes() == 0u);
            ASSERT_TRUE(room.guests[ann].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
        }
        {   // a room that is no lobby room has no Ready: the message is garbage there
            HostLobby::Config hc = keyed_server_config(410);
            hc.early_start = true;
            Room room(hc);
            const size_t ann = room.join_seat("Ann");
            room.join_seat("Bob");
            room.run(300);
            room.guests[ann].client_end->send(encode(ReadyMsg{true}));
            room.run(100);
            ASSERT_TRUE(room.host.ready_changes() == 0u && ready_text(room.host.room()).empty());
            ASSERT_FALSE(room.guests[ann].lobby->request_ready(true));
        }
    } TEST_END();

    TEST_CASE("N4.64 Ready Up (Protocol 27): The Message Is Heard While The Room Is Open And Its START Does Not Wait; Garbage Is A Violation, An Ignored One Is No Offence At First, A Flood Is Dropped And Then A Violation; Ready Counts As Use Of A Public Room") {
        {   // while a START waits nobody is heard: the mark stays as it was
            HostLobby::Config hc = lobby_room_config(411);
            hc.start_wait_ms = 5000;
            Room room(hc);
            const std::vector<size_t> g = pages(room, {"Ann", "Bob"});
            ASSERT_TRUE(room.guests[g[1]].lobby->request_ready(true));
            room.run(100);
            ASSERT_TRUE(room.guests[g[0]].lobby->request_start());
            room.run(100);
            ASSERT_TRUE(room.host.starting());
            ASSERT_FALSE(room.guests[g[1]].lobby->request_ready(false));                        // (the client does not send it)
            room.guests[g[1]].client_end->send(encode(ReadyMsg{false}));
            room.run(100);
            ASSERT_TRUE(all_show(room, "1") && room.host.ignored_ready() == 1u);
        }
        {   // garbage: a value above 1, an extra byte and a missing byte are violations; the guest that goes on is thrown out
            Room room(lobby_room_config(412));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob"});
            const size_t bob = g[1];
            room.guests[bob].client_end->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Ready), 7});
            room.guests[bob].client_end->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Ready), 1, 0});
            room.guests[bob].client_end->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Ready)});
            room.run(100);
            ASSERT_TRUE(all_show(room, "") && room.host.ready_changes() == 0u && room.host.ignored_ready() == 0u);
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::InRoom);
            for (int i = 0; i < 20 && room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom; ++i) {
                room.guests[bob].client_end->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Ready), 9});
                room.run(10);
            }
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Rejected);
            ASSERT_EQ(room.guests[bob].lobby->reject_reason(), RejectReason::BadRequest);
        }
        {   // a flood: past the burst a message is dropped (the mark does not move for it), and a connection that goes on beyond the excess allowance is thrown out
            Room room(lobby_room_config(413));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob"});
            const size_t bob = g[1];
            room.run(500);
            for (uint32_t i = 0; i < kReadyBurst; ++i) room.guests[bob].client_end->send(encode(ReadyMsg{i % 2 == 0}));      // (true, false, true: the budget's whole burst, ending Ready)
            room.run(100);
            ASSERT_TRUE(all_show(room, "1") && room.host.ready_changes() == kReadyBurst);
            room.guests[bob].client_end->send(encode(ReadyMsg{false}));                      // (one beyond the burst, in the same instant: dropped, the mark stays)
            room.run(100);
            ASSERT_TRUE(all_show(room, "1") && room.host.ready_changes() == kReadyBurst);
            for (int i = 0; i < 200 && room.guests[bob].lobby->phase() == ClientLobby::Phase::InRoom; ++i) {
                room.guests[bob].client_end->send(encode(ReadyMsg{true}));
                room.run(10);
            }
            ASSERT_EQ(room.guests[bob].lobby->phase(), ClientLobby::Phase::Rejected);
        }
        {   // a person's Ready is use of the room (it keeps a public room public)
            Room room(lobby_room_config(414));
            const std::vector<size_t> g = pages(room, {"Ann", "Bob"});
            room.run(300);
            const uint32_t before = room.host.activity();
            ASSERT_TRUE(room.guests[g[1]].lobby->request_ready(true));
            room.run(100);
            ASSERT_EQ(room.host.activity(), before + 1u);
        }
    } TEST_END();
}
