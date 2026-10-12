// Tests of the dedicated game server: the held seats (reconnect) and the restart records (their format, S3.82 and on)
#include "server_fixtures.hpp"

// Reconnect, release A: the server (protocol 10). A room that holds the seat of a player whose connection is lost, through the real manager, door and lobby. S3.36 - S3.59 (S3.34 and S3.35 are the end of a match and a laggard, v0.0.94: the ids S3.58 and S3.59 are this release's random_bytes and keys).
void run_reconnect_tests() {
    TEST_CASE("S3.59 Keys: A Room That Holds Seats Gives Every Player A Different Key (None Is Zero) In Its Welcome, And The Session Knows It; A Room That Does Not Gives None, And Its Players Have No Way Back") {
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("K-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "K-1");
            RClient& b = w.connect("Bob", "K-1");
            RClient& c = w.connect("Cat", "K-1");
            w.run(3000 + kPre);
            const RoomStatus s = w.status("K-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.reconnect && !s.paused && s.absent.empty() && s.rejoins == 0);
            ASSERT_TRUE(!net::key_is_zero(a.lobby->key()) && !net::key_is_zero(b.lobby->key()) && !net::key_is_zero(c.lobby->key()));
            ASSERT_TRUE(a.lobby->key() != b.lobby->key() && b.lobby->key() != c.lobby->key() && a.lobby->key() != c.lobby->key());
            ASSERT_TRUE(a.session->key() == a.lobby->key() && b.session->key() == b.lobby->key());
            ASSERT_FALSE(a.lobby->rejoined());                                           // a first Welcome is no rejoin
            ASSERT_TRUE(s.log_usable && s.log_turns > 0 && s.log_bytes > 0);              // the log of the match is being kept
        }
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(spec_of("K-2", 2), w.now).ok);                // (the default of a room is not to hold seats in this release)
            RClient& a = w.connect("Ann", "K-2");
            RClient& b = w.connect("Bob", "K-2");
            w.run(3000);
            const RoomStatus s = w.status("K-2");
            ASSERT_TRUE(s.state == RoomState::Running && !s.reconnect);
            ASSERT_TRUE(net::key_is_zero(a.lobby->key()) && net::key_is_zero(b.lobby->key()));
            ASSERT_FALSE(a.session->reconnecting());
            ASSERT_TRUE(s.log_turns == 0 && s.log_bytes == 0);                           // nothing is logged for a room that cannot use a log
            // a Hello with a made-up key for it is what any Hello for a running match is
            net::Connection* c = w.open_link();
            net::HelloMsg h;
            h.room = "K-2";
            for (uint8_t& v : h.key) v = 9;
            c->send(net::encode(h));
            w.run(300);
            ASSERT_EQ(reject_on(c), static_cast<int>(net::RejectReason::MatchRunning));
        }
    } TEST_END();

    TEST_CASE("S3.36 A Lost Link Pauses The Room (The Status Says Who Is Missing And Since When, Nothing Is Sealed For 10 s), The Player Comes Back Through The Door With Its Key And The Match Goes On To Its End: Three Machines And The Referee End Identical, The Counters Say What Happened") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("R-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "R-1");
        RClient& b = w.connect("Bob", "R-1");
        RClient& c = w.connect("Cat", "R-1");
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("R-1").state == RoomState::Running);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        w.run(1000);
        RoomStatus s = w.status("R-1");
        ASSERT_TRUE(s.paused && s.absent.size() == 1);
        ASSERT_TRUE(s.absent[0].seat == bob && s.absent[0].name == "Bob" && !s.absent[0].catching_up && s.absent[0].away_s <= 2 && s.absent[0].progress == 0);
        ASSERT_TRUE(s.vote_seat == 255);
        const uint32_t ticks_at_pause = s.ticks;
        w.run(10000);
        s = w.status("R-1");
        ASSERT_TRUE(s.ticks <= ticks_at_pause + 2);                                      // nothing runs while Bob is away
        ASSERT_TRUE(s.absent[0].away_s >= 10 && s.absent[0].away_s <= 12 && s.paused_s >= 10);
        ASSERT_TRUE(a.session->paused() && c.session->paused());
        b.reconnects = true;                                                             // its session finds a new link: the door takes it to the running room
        ASSERT_TRUE(w.until([&]() { return !w.status("R-1").paused; }, 8000));
        w.run(5000);
        s = w.status("R-1");
        ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.absent.empty());
        ASSERT_TRUE(s.rejoins == 1 && s.drops_by_vote == 0 && s.drops_by_cap == 0);
        ASSERT_TRUE(s.paused_s >= 11 && s.paused_s <= 13);
        ASSERT_TRUE(s.ticks > ticks_at_pause + 60);
        w.play_to_the_end("R-1");
        ASSERT_TRUE(w.status("R-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(a.sim.is_match_over() && b.sim.is_match_over() && c.sim.is_match_over());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        const RoomStatus end = w.status("R-1");
        ASSERT_TRUE(end.rejoins == 1 && end.log_usable && end.log_turns > 1000 && end.paused_s >= 11 && !end.paused && end.absent.empty());     // the log was freed at the end and says what it held
    } TEST_END();

    TEST_CASE("S3.37 A Machine That Starts From Nothing (A Reloaded Page: Its Lobby Says Hello With The Key) Is Given The Match, More Than 300 Turns Of It, Through The Door: The Seat Is Back With Its Name, The Hash Agreed, The Match Ends Identical On Three Machines") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("L-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "L-1");
        RClient& b = w.connect("Bob", "L-1");
        RClient& c = w.connect("Cat", "L-1");
        w.run(40000 + kPre);                                                                    // more than the 600 turns that every machine keeps
        ASSERT_TRUE(w.status("L-1").state == RoomState::Running && w.status("L-1").turns > 700);
        const net::SeatKey key = b.lobby->key();
        const uint8_t seat = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);                                                                        // the page of Bob is reloaded: its game is gone
        w.run(2000);
        ASSERT_TRUE(w.status("L-1").paused);
        RClient& b2 = w.connect("Bob", "L-1", 255, key);                                 // a new machine with nothing but the key
        bool saw_catching_up = false;
        bool saw_catching_up_in_json = false;                                            // the control interface says so too: state "catching_up" and a progress of 0 - 100
        ASSERT_TRUE(w.until([&]() {
            const RoomStatus st = w.status("L-1");
            for (const RoomStatus::Absent& e : st.absent) saw_catching_up = saw_catching_up || (e.catching_up && e.seat == seat);
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/rooms/L-1";
            ctl::JsonValue j;
            std::string why;
            ctl::parse_json(handle_control(w.mgr, rq, w.now).body, j, &why);
            if (j.get("absent").size() == 1) {
                const ctl::JsonValue& row = j.get("absent").at(0);
                saw_catching_up_in_json = saw_catching_up_in_json || (row.get("state").str() == "catching_up" && row.get("seat").as_int_or(9) == seat && row.get("name").str() == "Bob" &&
                                                                      row.get("progress").as_int_or(-1) >= 0 && row.get("progress").as_int_or(101) <= 100);
            }
            return !st.paused && b2.session != nullptr && b2.session->mode() == net::ClientSession::Mode::Normal;
        }, 20000));
        ASSERT_TRUE(saw_catching_up && saw_catching_up_in_json);
        ASSERT_EQ(b2.lobby->my_seat(), seat);
        ASSERT_TRUE(b2.lobby->key() == key && b2.lobby->rejoined());
        ASSERT_EQ(w.status("L-1").names[seat], std::string("Bob"));
        w.run(4000);
        w.play_to_the_end("L-1");
        ASSERT_TRUE(w.status("L-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b2.sim.state_hash() && b2.sim.state_hash() == c.sim.state_hash());
        ASSERT_TRUE(w.status("L-1").rejoins == 1);
        ASSERT_FALSE(a.session->desynced() || b2.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.38 Two Players: The One That Stays Votes After 30 s (The Status Shows The Vote), The Absent Seat Is Dropped, The Room Finishes With The Survivor The Winner In The Rows") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("V-1", 2), w.now).ok);
        RClient& a = w.connect("Ann", "V-1");
        RClient& b = w.connect("Bob", "V-1");
        w.run(6000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        w.run(25000);
        RoomStatus s = w.status("V-1");
        ASSERT_TRUE(s.paused && s.vote_seat == 255);                                     // 25 s: the vote is not open
        w.run(6500);
        s = w.status("V-1");
        ASSERT_TRUE(s.paused && s.vote_seat == bob && s.voters == 1 && s.votes_continue == 0);
        ASSERT_TRUE(a.session->presence().vote_seat == bob);
        ASSERT_TRUE(a.session->vote(bob, true));                                         // the one player that is left: more than half of one
        w.run(300);
        s = w.status("V-1");
        w.run(3000);
        s = w.status("V-1");
        ASSERT_EQ(s.drops_by_vote, 1u);
        ASSERT_TRUE(s.state == RoomState::Finished);                                     // Ann has won: the drop leaves her alone
        ASSERT_TRUE(a.sim.is_match_over());
        ASSERT_TRUE(!s.rows.empty() && s.rows[0].name == "Ann" && s.rows[0].winner);
        ASSERT_EQ(s.reason, std::string("the match ended"));
        {   // a room that says 8 s: the vote opens after 8 s of absence, not 30
            RWorld v;
            RoomSpec quick = held_spec("V-2", 3);
            quick.vote_after_ms = 8000;
            ASSERT_TRUE(v.mgr.create_room(quick, v.now).ok);
            RClient& x = v.connect("Xan", "V-2");
            RClient& y = v.connect("Yan", "V-2");
            RClient& z = v.connect("Zed", "V-2");
            v.run(4000 + kPre);
            z.reconnects = false;
            v.cut(z);
            v.run(6500);
            ASSERT_TRUE(v.status("V-2").paused && v.status("V-2").vote_seat == 255 && x.session->presence().vote_seat == 255);
            v.run(3000);
            ASSERT_TRUE(v.status("V-2").vote_seat == z.lobby->my_seat() && v.status("V-2").voters == 2);
            ASSERT_TRUE(x.session->presence().vote_seat == z.lobby->my_seat() && y.session->presence().vote_seat == z.lobby->my_seat());
        }
    } TEST_END();

    TEST_CASE("S3.39 The Room's Wall-Clock Limit Counts Play, Not Waiting: A Minute Of Waiting For A Player Inside A Limit Of 30 s Does Not Fail The Room, 30 s Of Play Do") {
        RWorld w;
        RoomSpec spec = held_spec("T-1", 2);
        spec.run_ms = 30000;
        spec.max_pause_ms = 600000;                                                      // (nobody is dropped by the cap in this test)
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        w.connect("Ann", "T-1");
        RClient& b = w.connect("Bob", "T-1");
        w.run(5000 + kPre);
        b.reconnects = false;
        w.cut(b);
        w.run(60000);                                                                    // a minute of waiting: more than the limit
        ASSERT_TRUE(w.status("T-1").state == RoomState::Running);
        b.reconnects = true;
        ASSERT_TRUE(w.until([&]() { return !w.status("T-1").paused; }, 8000));
        w.run(10000);
        ASSERT_TRUE(w.status("T-1").state == RoomState::Running);
        w.run(25000);                                                                    // now 30 s of play have passed
        ASSERT_TRUE(w.status("T-1").state == RoomState::Failed && w.status("T-1").reason.find("longer") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.40 Everybody Lost: Each Seat Is Held (The Room Waits For Them As Long As The Cap Allows), Then The Room Ends \"Everybody Left\" With All Seats Dropped By The Cap; A Short Cap In The Spec Is Obeyed To The Second") {
        RWorld w;
        RoomSpec spec = held_spec("E-1", 3);
        spec.max_pause_ms = 60000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        RClient& a = w.connect("Ann", "E-1");
        RClient& b = w.connect("Bob", "E-1");
        RClient& c = w.connect("Cat", "E-1");
        w.run(4000 + kPre);
        for (RClient* p : {&a, &b, &c}) {
            p->reconnects = false;
            w.cut(*p);
        }
        const uint32_t cut_at = w.now;
        w.run(30000);
        RoomStatus s = w.status("E-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.absent.size() == 3);   // nobody is there, but each seat is held
        ASSERT_TRUE(s.absent[0].away_s >= 29 && s.absent[0].away_s <= 31);
        uint32_t ended_after_ms = 0;
        for (int i = 0; i < 4000 && ended_after_ms == 0; ++i) {
            w.run(10);
            if (w.status("E-1").state != RoomState::Running) ended_after_ms = w.now - cut_at;
        }
        s = w.status("E-1");
        ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "everybody left");
        ASSERT_TRUE(ended_after_ms >= 60000 && ended_after_ms <= 60100);
        ASSERT_EQ(s.drops_by_cap, 3u);
        ASSERT_EQ(s.drops_by_vote, 0u);
        ASSERT_TRUE(s.paused_s >= 59 && s.paused_s <= 61);
    } TEST_END();

    TEST_CASE("S3.41 The Cap On A Match's Pauses Through The Door: With Three Players One Of Them Is Away For The Whole Cap (60 s): The Seat Is Dropped At The Same Tick For The Others, The Match Goes On To Its End, And The Key Of The Dropped Seat Is Told \"Dropped\" When It Comes Back") {
        RWorld w;
        RoomSpec spec = held_spec("C-1", 3);
        spec.max_pause_ms = 60000;
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        RClient& a = w.connect("Ann", "C-1");
        RClient& b = w.connect("Bob", "C-1");
        RClient& c = w.connect("Cat", "C-1");
        w.run(6000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.reconnects = false;
        w.cut(b);
        const uint32_t cut_at = w.now;
        w.run(55000);
        RoomStatus s = w.status("C-1");
        ASSERT_TRUE(s.paused && s.vote_seat == bob);                                      // (the vote is open since 30 s; nobody votes)
        uint32_t resumed_after_ms = 0;
        for (int i = 0; i < 1000 && resumed_after_ms == 0; ++i) {
            w.run(10);
            if (!w.status("C-1").paused) resumed_after_ms = w.now - cut_at;
        }
        ASSERT_TRUE(resumed_after_ms >= 60000 && resumed_after_ms <= 60100);
        s = w.status("C-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.drops_by_cap == 1 && s.drops_by_vote == 0 && s.rejoins == 0);
        w.run(3000);
        ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
        b.reconnects = true;                                                              // Bob's machine finds a link: its seat is gone
        w.run(4000);
        ASSERT_TRUE(b.lost && b.was_rejected && b.rejected == net::RejectReason::Dropped);
        w.play_to_the_end("C-1");
        ASSERT_TRUE(w.status("C-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.42 The Prune Window (Run It Under AddressSanitizer): A Returning Player's Link Closes While The Session Is Keeping Its Catch-Up, In The Very Pass In Which The Door Reads Another Seat's Hello And A Room That Keeps 32 Connections Prunes The Closed Ones: The Connection That The Session Still Points At Is Not Freed") {
        RWorld w;
        w.link = {20, 0};                                                                // no jitter: the Hello of the second link arrives exactly when this test needs it
        ASSERT_TRUE(w.mgr.create_room(held_spec("P-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "P-1");
        RClient& b = w.connect("Bob", "P-1");
        RClient& c = w.connect("Cat", "P-1");
        w.run(4000 + kPre);
        RClient* const seats[3] = {&b, &c, &a};
        // (a seat may be taken back twelve times a minute: the three of them take turns, one cut every 7 s, so that each is cut every 21 s)
        for (int i = 0; i < 80 && w.status("P-1").connections != Room::kMaxConnections - 1; ++i) {        // until the room keeps 31 connections: the next one fills it
            w.cut(*seats[i % 3]);
            ASSERT_TRUE(w.until([&]() { return !w.status("P-1").paused && w.status("P-1").rejoins == static_cast<uint32_t>(i + 1); }, 6000));
            w.run(7000);
        }
        ASSERT_EQ(w.status("P-1").connections, static_cast<uint32_t>(Room::kMaxConnections - 1));
        b.reconnects = false;
        c.reconnects = false;
        const net::SeatKey key_b = b.session->key();
        const net::SeatKey key_c = c.session->key();
        const uint32_t have_b = b.session->runner().next_turn_expected();
        const uint32_t have_c = c.session->runner().next_turn_expected();
        w.run(15000);                                                                    // (the last returns of the loop were 21 s apart: the oldest of the three leaves the minute)
        w.cut(b);
        w.cut(c);
        w.run(200);
        ASSERT_TRUE(w.status("P-1").paused);
        const auto make_link = [&](const net::SeatKey& key, uint32_t have) {
            net::Connection* end = w.open_link();
            net::HelloMsg h;
            h.name = "x";
            h.room = "P-1";
            h.key = key;
            h.have_turns = have;
            end->send(net::encode(h));
            return end;
        };
        net::Connection* link_b = make_link(key_b, have_b);                              // Bob's Hello reaches the door 20 ms later
        w.run(30);                                                                       // accepted: the room keeps the connection, the session has a returning player for it
        ASSERT_EQ(w.status("P-1").connections, static_cast<uint32_t>(Room::kMaxConnections));
        make_link(key_c, have_c);                                                        // Cat's Hello arrives 20 ms from now ...
        w.run(10);
        w.net.cut(link_b);                                                               // ... and 10 ms from now Bob's link is closed (the session has not looked at it yet)
        w.run(10);                                                                       // the pass in which the door reads Cat's Hello: the room prunes, the session pumps its returning players
        w.run(2000);
        ASSERT_TRUE(w.status("P-1").state == RoomState::Running);
        ASSERT_TRUE(w.status("P-1").connections < static_cast<uint32_t>(Room::kMaxConnections));    // the closed ones were pruned (the table is not full any more)
    } TEST_END();

    TEST_CASE("S3.43 Connections That Are Cut 40 Times (Three Players Take Turns, Each Is Cut Every 21 s: A Seat May Be Taken Back Twelve Times A Minute): Every Return Is Verified, The Closed Connections Are Pruned And The Live Ones Never Are, The Match Ends Identical (Each Pause Counts 5 s Toward The Cap At Least, The Cap Is Far Away)") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("F-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "F-1");
        RClient& b = w.connect("Bob", "F-1");
        RClient& c = w.connect("Cat", "F-1");
        w.run(4000 + kPre);
        RClient* const seats[3] = {&b, &c, &a};
        for (int i = 0; i < 40; ++i) {
            w.cut(*seats[i % 3]);
            ASSERT_TRUE(w.until([&]() { return !w.status("F-1").paused && w.status("F-1").rejoins == static_cast<uint32_t>(i + 1); }, 6000));
            w.run(7000);                                                                 // (the next seat's turn: this one is cut again 21 s from now, which is within twelve a minute)
        }
        const RoomStatus s = w.status("F-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 40 && s.drops_by_vote == 0 && s.drops_by_cap == 0 && s.rejoins_refused == 0);
        ASSERT_TRUE(s.connections <= Room::kMaxConnections + 1u);                        // (the table did not grow with every return)
        ASSERT_FALSE(a.lost || b.lost || c.lost);
        ASSERT_TRUE(s.paused_s >= 40 * 5 && s.paused_s < 40 * 5 + 40);                   // (each pause counts at least 5 s toward the cap: 40 of them are 200 s, and they were a second each)
        w.play_to_the_end("F-1");
        ASSERT_TRUE(w.status("F-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.44 The Door And A Hello With A Key: A Wrong Key, No Key And A Made-Up Key Are Told \"The Match Has Started\" By A Running Room (Nobody Is Disturbed, Nothing Is Revealed), So Is A Key For A Room That Does Not Hold Seats Or Is Loading; A Room That Is Over Says \"No Such Room\"; A Refusal Is Counted") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("X-1", 2), w.now).ok);
        RClient& a = w.connect("Ann", "X-1");
        RClient& b = w.connect("Bob", "X-1");
        w.run(4000 + kPre);
        ASSERT_TRUE(w.status("X-1").state == RoomState::Running);
        const auto hello_for = [&](const std::string& room, const net::SeatKey& key, uint32_t have) {
            net::Connection* end = w.open_link();
            net::HelloMsg h;
            h.name = "Mallory";
            h.room = room;
            h.key = key;
            h.have_turns = have;
            end->send(net::encode(h));
            return end;
        };
        net::SeatKey bad;
        for (uint8_t& v : bad) v = 7;
        const uint64_t refused_before = w.mgr.connections_refused();
        net::Connection* wrong = hello_for("X-1", bad, 0);
        net::Connection* none = hello_for("X-1", net::SeatKey{}, 0);
        net::SeatKey almost = a.lobby->key();
        almost[15] = static_cast<uint8_t>(almost[15] ^ 0x80);                             // the key of Ann with one bit changed
        net::Connection* near_miss = hello_for("X-1", almost, 0);
        w.run(400);
        ASSERT_EQ(reject_on(wrong), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(reject_on(none), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(reject_on(near_miss), static_cast<int>(net::RejectReason::MatchRunning));
        ASSERT_EQ(w.mgr.connections_refused() - refused_before, uint64_t{3});
        RoomStatus s = w.status("X-1");
        ASSERT_TRUE(!s.paused && s.absent.empty() && s.rejoins == 0);                    // nobody was disturbed
        ASSERT_TRUE(a.session->mode() == net::ClientSession::Mode::Normal && b.session->mode() == net::ClientSession::Mode::Normal);
        // a key that is Ann's, with a count of turns that was never sealed: refused (the session says BadRequest: the key was right, the claim is not) and nobody is held for it
        net::Connection* liar = hello_for("X-1", a.lobby->key(), 999999999);
        w.run(400);
        ASSERT_EQ(reject_on(liar), static_cast<int>(net::RejectReason::BadRequest));
        ASSERT_TRUE(!w.status("X-1").paused && a.session->mode() == net::ClientSession::Mode::Normal);
        // a room that does not hold seats: a key is a key to nothing
        ASSERT_TRUE(w.mgr.create_room(spec_of("X-2", 2), w.now).ok);
        w.connect("Cat", "X-2");
        w.connect("Dan", "X-2");
        w.run(3000);
        ASSERT_TRUE(w.status("X-2").state == RoomState::Running);
        net::Connection* off = hello_for("X-2", a.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(off), static_cast<int>(net::RejectReason::MatchRunning));
        // a room that is loading (one player never says that it has loaded the map): MatchRunning too
        ASSERT_TRUE(w.mgr.create_room(held_spec("X-3", 2), w.now).ok);
        RClient& e = w.connect("Eve", "X-3");
        net::Connection* raw = w.open_link();                                            // a raw player that says Hello and then nothing
        net::HelloMsg h;
        h.name = "Slow";
        h.room = "X-3";
        raw->send(net::encode(h));
        w.run(2500);
        ASSERT_TRUE(w.status("X-3").state == RoomState::Loading);
        net::Connection* during = hello_for("X-3", e.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(during), static_cast<int>(net::RejectReason::MatchRunning));
        // a room that is over: NoSuchRoom, whatever is in the Hello
        w.play_to_the_end("X-1");
        ASSERT_TRUE(w.status("X-1").state == RoomState::Finished);
        net::Connection* late = hello_for("X-1", a.lobby->key(), 0);
        net::Connection* late2 = hello_for("X-1", bad, 0);
        w.run(300);
        ASSERT_EQ(reject_on(late), static_cast<int>(net::RejectReason::NoSuchRoom));
        ASSERT_EQ(reject_on(late2), static_cast<int>(net::RejectReason::NoSuchRoom));
        ASSERT_TRUE(w.mgr.close_room("X-3", w.now));
        w.run(100);
        ASSERT_TRUE(w.status("X-3").state == RoomState::Failed);
        net::Connection* failed = hello_for("X-3", e.lobby->key(), 0);
        w.run(300);
        ASSERT_EQ(reject_on(failed), static_cast<int>(net::RejectReason::NoSuchRoom));
    } TEST_END();

    TEST_CASE("S3.45 A Page That Is Reloaded In The Waiting Room Takes Its Seat Back Through The Door (Same Seat, Same Key, The Room Still Counts One Player, The Leader Stays The Leader), The Old Connection Is Told It Was Superseded; The Room Then Fills And Plays") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("W-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "W-1");
        w.run(1000);
        ASSERT_TRUE(w.status("W-1").joined == 1 && w.status("W-1").leader == a.lobby->my_seat());
        const net::SeatKey key = a.lobby->key();
        const uint8_t seat = a.lobby->my_seat();
        RClient& a2 = w.connect("Ann", "W-1", 255, key);                                  // a reload: the new page says Hello with the key before the room has noticed that the old link is dead
        w.run(1000);
        RoomStatus s = w.status("W-1");
        ASSERT_TRUE(s.joined == 1 && s.state == RoomState::Waiting && s.leader == seat);
        ASSERT_EQ(a2.lobby->my_seat(), seat);
        ASSERT_TRUE(a2.lobby->key() == key);
        ASSERT_FALSE(a2.lobby->rejoined());                                               // (still the waiting room: no catch-up)
        ASSERT_TRUE(a.lobby->phase() == net::ClientLobby::Phase::Rejected && a.lobby->reject_reason() == net::RejectReason::Superseded);
        w.connect("Bob", "W-1");
        w.connect("Cat", "W-1");
        w.run(3000);
        ASSERT_TRUE(w.status("W-1").state == RoomState::Running);
        ASSERT_TRUE(a2.session != nullptr && a2.session->key() == key);
    } TEST_END();

    TEST_CASE("S3.46 The Control Interface's Status Says What The Reconnect Does: Paused, Who Is Missing And Since When, The Vote, The Total Pause, The Counters, The Log, The Rules; A Seat's Key Is In None Of It: Not In The Status Of A Running Or Finished Room, The List, A Result File's Text, The Log Output (Nothing Is Written To Stderr)") {
        std::ostringstream captured;                                                     // whatever the server code writes to stderr in this test
        std::streambuf* old_cerr = std::cerr.rdbuf(captured.rdbuf());
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("J-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "J-1");
            RClient& b = w.connect("Bob", "J-1");
            RClient& c = w.connect("Cat", "J-1");
            w.run(5000 + kPre);
            const std::vector<net::SeatKey> keys = {a.lobby->key(), b.lobby->key(), c.lobby->key()};
            const auto status_json = [&](const std::string& code) {
                ctl::HttpRequest rq;
                rq.method = "GET";
                rq.path = "/rooms/" + code;
                return handle_control(w.mgr, rq, w.now);
            };
            const auto parse = [](const ctl::HttpResponse& r) {
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(r.body, v, &why);
                return v;
            };
            ctl::HttpResponse r = status_json("J-1");
            ASSERT_EQ(r.status, 200);
            ctl::JsonValue v = parse(r);
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && !v.get("paused").as_bool_or(true) && v.get("absent").size() == 0 && v.get("vote").is_null());
            ASSERT_TRUE(v.get("hold_vote_seconds").as_int_or(0) == 30 && v.get("max_pause_seconds").as_int_or(0) == 1800);
            ASSERT_TRUE(v.get("rejoins").as_int_or(9) == 0 && v.get("drops_by_vote").as_int_or(9) == 0 && v.get("drops_by_cap").as_int_or(9) == 0 && v.get("paused_seconds").as_int_or(9) == 0);
            ASSERT_TRUE(v.get("log").get("usable").as_bool_or(false) && v.get("log").get("turns").as_int_or(0) > 50 && v.get("log").get("bytes").as_int_or(0) > 100);
            b.reconnects = false;
            w.cut(b);
            w.run(31500);
            a.session->vote(b.lobby->my_seat(), true);                                   // one of two connected players: not enough
            w.run(2500);
            r = status_json("J-1");
            v = parse(r);
            ASSERT_TRUE(v.get("paused").as_bool_or(false) && v.get("absent").size() == 1);
            const ctl::JsonValue& row = v.get("absent").at(0);
            ASSERT_TRUE(row.get("seat").as_int_or(9) == b.lobby->my_seat() && row.get("name").str() == "Bob" && row.get("state").str() == "absent");
            ASSERT_TRUE(row.get("away_seconds").as_int_or(0) >= 33 && row.get("away_seconds").as_int_or(0) <= 35 && row.get("progress").as_int_or(9) == 0);
            ASSERT_TRUE(v.get("vote").get("seat").as_int_or(9) == b.lobby->my_seat() && v.get("vote").get("continue").as_int_or(9) == 1 && v.get("vote").get("voters").as_int_or(9) == 2);
            ASSERT_TRUE(v.get("paused_seconds").as_int_or(0) >= 33 && v.get("paused_seconds").as_int_or(0) <= 35);
            ASSERT_TRUE(v.get("rejoins").as_int_or(9) == 0);
            std::string everything = r.body;
            ctl::HttpRequest list;
            list.method = "GET";
            list.path = "/rooms";
            everything += handle_control(w.mgr, list, w.now).body;
            ctl::HttpRequest stats;
            stats.method = "GET";
            stats.path = "/stats";
            const ctl::HttpResponse stat = handle_control(w.mgr, stats, w.now);
            ASSERT_TRUE(parse(stat).get("log_bytes").as_int_or(0) > 100 && parse(stat).get("log_budget_bytes").as_int_or(0) == 256 * 1024 * 1024);
            everything += stat.body;
            // Bob comes back: the counters
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("J-1").paused; }, 8000));
            w.run(1000);
            v = parse(status_json("J-1"));
            ASSERT_TRUE(!v.get("paused").as_bool_or(true) && v.get("absent").size() == 0 && v.get("vote").is_null() && v.get("rejoins").as_int_or(0) == 1);
            ASSERT_TRUE(v.get("paused_seconds").as_int_or(0) >= 33);
            everything += status_json("J-1").body;
            w.play_to_the_end("J-1");
            // the room that ended: what main() writes to <code>.json is exactly this text
            std::string result_file;
            for (const RoomStatus& ended : w.mgr.take_ended(w.now)) result_file += ctl::to_json(status_to_json(ended)) + "\n";
            ASSERT_FALSE(result_file.empty());
            v = parse(status_json("J-1"));
            ASSERT_TRUE(v.get("state").str() == "finished" && v.get("rejoins").as_int_or(0) == 1 && v.get("log").get("turns").as_int_or(0) > 1000 && v.get("log").get("usable").as_bool_or(false));
            ASSERT_TRUE(v.get("drops_by_vote").as_int_or(9) == 0 && !v.get("paused").as_bool_or(true) && v.get("absent").size() == 0);
            everything += status_json("J-1").body + result_file;
            ASSERT_TRUE(result_file.find("\"rejoins\":1") != std::string::npos && result_file.find("\"log\":{") != std::string::npos);
            for (const net::SeatKey& key : keys) {
                const std::string hex = hex_of(key);
                ASSERT_TRUE(everything.find(hex) == std::string::npos);                   // no key anywhere in what the control interface says
                ASSERT_TRUE(captured.str().find(hex) == std::string::npos);               // nor in what the server code logged
            }
            ASSERT_TRUE(everything.find("key") == std::string::npos);                     // (the word does not occur either: nothing is there to leak)
        }
        std::cerr.rdbuf(old_cerr);
        ASSERT_TRUE(captured.str().empty());                                              // the server code writes nothing to stderr
    } TEST_END();

    TEST_CASE("S3.47 The Reconnect Settings Of A Room And Of The Server: The Control Interface Takes \"reconnect\", \"hold_vote_seconds\" (5 - 3600) And \"max_pause_seconds\" (60 - 86400) And Refuses What Is Outside Or Of Another Kind; A Body Without Them Gets The Server's Defaults (Hold Seats Unless --no-reconnect, --hold-vote-seconds, --max-pause-seconds); create_room Checks The Bounds Too; Public Rooms Follow The Server's Setting") {
        {
            RoomManager mgr{MapStore(maps_dir())};
            const auto call = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                return handle_control(mgr, rq, 5000);
            };
            const auto parse = [](const ctl::HttpResponse& r) {
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(r.body, v, &why);
                return v;
            };
            // the built-in defaults (a deliberate change of release B's switch: they were "do not hold seats"): hold seats, 30 s, 1800 s
            ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"O-1"})");
            ASSERT_EQ(r.status, 201);
            ctl::JsonValue v = parse(r);
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 30 && v.get("max_pause_seconds").as_int_or(0) == 1800);
            r = call(R"({"map":"TINY.LVL","code":"O-2","reconnect":true,"hold_vote_seconds":5,"max_pause_seconds":86400})");     // the edges are good
            ASSERT_EQ(r.status, 201);
            v = parse(r);
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 5 && v.get("max_pause_seconds").as_int_or(0) == 86400);
            r = call(R"({"map":"TINY.LVL","code":"O-3","hold_vote_seconds":3600,"max_pause_seconds":60})");
            ASSERT_EQ(r.status, 201);
            v = parse(r);
            ASSERT_TRUE(v.get("hold_vote_seconds").as_int_or(0) == 3600 && v.get("max_pause_seconds").as_int_or(0) == 60 && v.get("reconnect").as_bool_or(false));       // (it said nothing about reconnect: the default holds seats)
            r = call(R"({"map":"TINY.LVL","code":"O-4","reconnect":false})");                  // the off state, said out loud, stays covered: this room holds no seats
            ASSERT_EQ(r.status, 201);
            ASSERT_TRUE(!parse(r).get("reconnect").as_bool_or(true));
            for (const char* bad : {R"("hold_vote_seconds":4)", R"("hold_vote_seconds":3601)", R"("hold_vote_seconds":0)", R"("hold_vote_seconds":-5)", R"("hold_vote_seconds":"30")", R"("hold_vote_seconds":30.5)",
                                    R"("max_pause_seconds":59)", R"("max_pause_seconds":86401)", R"("max_pause_seconds":0)", R"("max_pause_seconds":"1800")", R"("reconnect":"yes")", R"("reconnect":1)", R"("reconnect":null)"}) {
                r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
                ASSERT_MSG(r.status == 400, bad);
                const std::string why = parse(r).get("error").str();
                ASSERT_TRUE(why.size() > 10);
                // the body is refused by the control interface itself, with the bounds in its words (the room manager would refuse the numbers too, with a message of its own)
                const std::string key = std::string(bad).find("hold_vote") != std::string::npos ? "\"hold_vote_seconds\" must be an integer from 5 to 3600"
                                        : std::string(bad).find("max_pause") != std::string::npos ? "\"max_pause_seconds\" must be an integer from 60 to 86400" : "\"reconnect\" must be true or false";
                ASSERT_MSG(why == key, why);
            }
            ASSERT_EQ(mgr.room_count(), size_t{4});                                       // none of the refused ones made a room
        }
        {   // the server's own defaults reach a room that says nothing; what a room says wins
            ServerLimits limits;
            limits.reconnect = true;
            limits.hold_vote_ms = 45000;
            limits.max_pause_ms = 900000;
            RoomManager mgr{MapStore(maps_dir()), limits};
            const auto call = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(handle_control(mgr, rq, 5000).body, v, &why);
                return v;
            };
            ctl::JsonValue v = call(R"({"map":"TINY.LVL","code":"D-1"})");
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 45 && v.get("max_pause_seconds").as_int_or(0) == 900);
            v = call(R"({"map":"TINY.LVL","code":"D-2","reconnect":false,"max_pause_seconds":120})");
            ASSERT_TRUE(!v.get("reconnect").as_bool_or(true) && v.get("hold_vote_seconds").as_int_or(0) == 45 && v.get("max_pause_seconds").as_int_or(0) == 120);
            const RoomSpec defaults = mgr.default_spec();
            ASSERT_TRUE(defaults.reconnect && defaults.vote_after_ms == 45000 && defaults.max_pause_ms == 900000 && defaults.max_log_bytes == net::TurnLog::kDefaultMaxBytes);
        }
        {   // a server that was started with --no-reconnect (ServerLimits::reconnect false): a room that says nothing holds no seats, and a room that says "reconnect": true still does
            ServerLimits limits;
            limits.reconnect = false;
            limits.hold_vote_ms = 45000;
            RoomManager mgr{MapStore(maps_dir()), limits};
            const auto call = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                ctl::JsonValue v;
                std::string why;
                ctl::parse_json(handle_control(mgr, rq, 5000).body, v, &why);
                return v;
            };
            ctl::JsonValue v = call(R"({"map":"TINY.LVL","code":"N-1"})");
            ASSERT_TRUE(!v.get("reconnect").as_bool_or(true) && v.get("hold_vote_seconds").as_int_or(0) == 45);
            v = call(R"({"map":"TINY.LVL","code":"N-2","reconnect":true})");
            ASSERT_TRUE(v.get("reconnect").as_bool_or(false) && v.get("hold_vote_seconds").as_int_or(0) == 45);
            ASSERT_FALSE(mgr.default_spec().reconnect);
        }
        {   // create_room checks the bounds for everybody who does not come through the JSON
            RoomManager mgr{MapStore(maps_dir())};
            const auto made = [&](const std::string& code, const std::function<void(RoomSpec&)>& change) {
                RoomSpec spec = spec_of(code, 2);
                change(spec);
                return mgr.create_room(spec, 5000);
            };
            ASSERT_EQ(made("B-1", [](RoomSpec& s) { s.vote_after_ms = 4999; }).http_status, 400);
            ASSERT_EQ(made("B-2", [](RoomSpec& s) { s.vote_after_ms = 3600001; }).http_status, 400);
            ASSERT_EQ(made("B-3", [](RoomSpec& s) { s.max_pause_ms = 59999; }).http_status, 400);
            ASSERT_EQ(made("B-4", [](RoomSpec& s) { s.max_pause_ms = 86400001; }).http_status, 400);
            ASSERT_EQ(made("B-5", [](RoomSpec& s) { s.max_log_bytes = 100; }).http_status, 400);
            ASSERT_EQ(made("B-6", [](RoomSpec& s) { s.max_log_bytes = size_t{2} << 30; }).http_status, 400);
            ASSERT_TRUE(made("B-7", [](RoomSpec& s) { s.vote_after_ms = 5000; s.max_pause_ms = 60000; s.max_log_bytes = 1024; }).ok);
            ASSERT_TRUE(made("B-8", [](RoomSpec& s) { s.vote_after_ms = 3600000; s.max_pause_ms = 86400000; s.max_log_bytes = size_t{1} << 30; }).ok);
        }
        {   // public rooms follow the server's setting: keys in the Welcome (or none), and the status says so
            for (const bool hold : {false, true}) {
                ServerLimits limits;
                limits.demo_rooms = 2;
                limits.demo_map = "TINY.LVL";
                limits.reconnect = hold;
                RWorld w(limits);
                RClient& a = w.connect_creating("Ann", "held0001", block_of("", 2));
                RClient& b = w.connect("Bob", "held0001");
                w.run(4000 + kPre);
                const RoomStatus s = w.status("held0001");
                ASSERT_TRUE(s.state == RoomState::Running && s.reconnect == hold);
                ASSERT_EQ(!net::key_is_zero(a.lobby->key()) && !net::key_is_zero(b.lobby->key()), hold);
                ASSERT_EQ(net::key_is_zero(a.lobby->key()) && net::key_is_zero(b.lobby->key()), !hold);
            }
        }
    } TEST_END();

    TEST_CASE("S3.48 The Server's Budget For The Logs Of All Its Rooms: The Logs Together Never Hold More Than The Budget (The Statistics Say How Much); A Log That Cannot Grow Is Not Kept And The Room Falls Back To Dropping At Once (Its Status Says Unusable); What A Match Held Is Given Back When It Ends, And A New Room Can Log Again") {
        ServerLimits limits;
        limits.log_budget_bytes = 9 * 1024;                                              // 9 KB for all the logs of the server (a quiet match adds about 120 bytes a second)
        RWorld w(limits);
        ASSERT_EQ(w.mgr.log_budget_bytes(), uint64_t{9 * 1024});
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-1", 3), w.now).ok);
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-2", 2), w.now).ok);
        RClient& a1 = w.connect("A1", "G-1");
        RClient& b1 = w.connect("B1", "G-1");
        w.connect("C1", "G-1");
        RClient& a2 = w.connect("A2", "G-2");
        RClient& b2 = w.connect("B2", "G-2");
        (void)a2;
        (void)b2;
        uint64_t most = 0;
        bool first_dead = false;
        bool both = false;
        for (int i = 0; i < 40000 && !both; ++i) {
            w.run(250);
            most = std::max(most, w.mgr.log_bytes());
            ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());                   // never more than the budget, however busy the rooms are
            const RoomStatus r1 = w.status("G-1");
            const RoomStatus r2 = w.status("G-2");
            if (!first_dead && (!r1.log_usable || !r2.log_usable)) {
                first_dead = true;
                const RoomStatus& dead = r1.log_usable ? r2 : r1;
                ASSERT_TRUE(dead.log_bytes == 0 && dead.log_turns == 0);                  // a log that cannot grow is DEAD: it freed what it held at once (and gave it back to the budget)
            }
            both = !r1.log_usable && !r2.log_usable;                                      // (the one that is left may grow in what the dead one gave back, until it is full too)
        }
        ASSERT_TRUE(first_dead && both);
        ASSERT_TRUE(most > 6 * 1024);                                                    // it filled up (the budget counts what the logs have allocated, which grows by doubling)
        w.run(2000);
        const RoomStatus s1 = w.status("G-1");
        const RoomStatus s2 = w.status("G-2");
        ASSERT_FALSE(s1.log_usable);
        ASSERT_FALSE(s2.log_usable);                                                     // the server has no memory for either: neither log grows any more, and both are dead (they hold nothing)
        ASSERT_TRUE(w.mgr.log_bytes() == 0);
        ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());
        // a room with a log that is not usable drops a lost seat at once: no pause, no way back (its key is told "dropped")
        const uint8_t bob = b1.lobby->my_seat();
        b1.reconnects = true;
        w.cut(b1);
        w.run(2000);
        const RoomStatus after = w.status("G-1");
        ASSERT_TRUE(!after.paused && after.absent.empty() && after.state == RoomState::Running);          // the two that are left play on
        ASSERT_TRUE(a1.sim.is_player_dropped(bob));
        ASSERT_TRUE(b1.lost && b1.was_rejected && b1.rejected == net::RejectReason::Dropped);
        // two dead logs hold nothing: the budget is free, and the next room can keep a log again
        ASSERT_TRUE(w.mgr.close_room("G-2", w.now));
        w.run(100);
        ASSERT_TRUE(w.mgr.log_bytes() == 0);
        ASSERT_TRUE(w.mgr.create_room(held_spec("G-3", 2), w.now).ok);
        w.connect("A3", "G-3");
        w.connect("B3", "G-3");
        w.run(8000);
        const RoomStatus s3 = w.status("G-3");
        ASSERT_TRUE(s3.state == RoomState::Running && s3.log_usable && s3.log_bytes > 0);
        ASSERT_TRUE(w.mgr.log_bytes() > 0 && w.mgr.log_bytes() <= w.mgr.log_budget_bytes());
        // the end of a match gives its log back at once, and the status keeps what it held
        ASSERT_TRUE(w.mgr.close_room("G-3", w.now));
        w.run(100);
        const RoomStatus ended = w.status("G-3");
        ASSERT_TRUE(ended.log_turns > 0 && ended.log_bytes > 0 && ended.log_usable);
        ASSERT_TRUE(w.mgr.log_bytes() == 0);                                             // what the room held is not held any more
    } TEST_END();

    TEST_CASE("S3.49 Lag Is Not A Loss In A Room That Holds Seats: A Machine That Draws A Frame Every 4 s Falls Behind (The Others Are Told It Lags, Nobody Is Paused, Nobody Waits), Is Not Held And Not Dropped, And Is Level Again When Its Frames Come Back; The States Agree At The End") {
        RWorld w;
        ASSERT_TRUE(w.mgr.create_room(held_spec("S-1", 3), w.now).ok);
        RClient& a = w.connect("Ann", "S-1");
        RClient& b = w.connect("Bob", "S-1");
        RClient& c = w.connect("Cat", "S-1");
        w.run(4000 + kPre);
        const uint8_t bob = b.lobby->my_seat();
        b.frame_every_ms = 4000;
        bool paused_ever = false;
        bool told = false;
        const uint32_t ticks_before = w.status("S-1").ticks;
        for (int i = 0; i < 3000; ++i) {                                                  // 30 s
            w.run(10);
            const RoomStatus st = w.status("S-1");
            paused_ever = paused_ever || st.paused || !st.absent.empty();
            told = told || a.session->lagging_seat() == bob;
        }
        ASSERT_FALSE(paused_ever);
        ASSERT_TRUE(told);
        ASSERT_TRUE(w.status("S-1").ticks - ticks_before >= 590);                         // the room played on at its own pace (20 ticks a second)
        ASSERT_FALSE(a.sim.is_player_dropped(bob) || c.sim.is_player_dropped(bob));
        ASSERT_TRUE(w.status("S-1").rejoins == 0 && w.status("S-1").drops_by_cap == 0 && w.status("S-1").drops_by_vote == 0);
        b.frame_every_ms = 0;                                                             // its frames are back: it runs the backlog at up to 4x
        w.run(40000);
        ASSERT_TRUE(b.session->mode() == net::ClientSession::Mode::Normal && !b.session->catching_up());
        ASSERT_TRUE(a.session->lagging_seat() == 255);
        w.play_to_the_end("S-1");
        ASSERT_TRUE(w.status("S-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
        ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
    } TEST_END();

    TEST_CASE("S3.50 A Room That Does Not Hold Seats (reconnect: false, Said Out Loud) Behaves Exactly As In v0.0.94: A Cut Link Is A Drop For Everybody At The Same Tick Within 3 s (And The Machine Is Lost At Once), When Everybody Has Left The Room Finishes; A Seat That Stops Executing Is Dropped After 30 s And The Others Play On, With Two Players The Room Finishes; No Presence Is Ever Sent") {
        {
            RWorld w;
            RoomSpec spec = spec_of("DROP-1", 3);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& a = w.connect("A", "DROP-1");
            RClient& b = w.connect("B", "DROP-1");
            RClient& c = w.connect("C", "DROP-1");
            w.run(4000 + kPre);
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
            w.cut(b);
            bool paused_ever = false;
            for (int i = 0; i < 300; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("DROP-1").paused;
            }
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(a.sim.is_player_dropped(1) && c.sim.is_player_dropped(1));
            ASSERT_TRUE(b.lost && !b.was_rejected);                                       // its match is over at once: no key, no way back
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Running);
            for (RClient* p : {&a, &c}) ASSERT_TRUE(p->session->presence().missing.empty() && !p->session->paused());
            w.cut(a);
            w.cut(c);
            w.run(3000);
            ASSERT_TRUE(w.status("DROP-1").state == RoomState::Finished);
            ASSERT_TRUE(w.status("DROP-1").reason == "everybody left" || w.status("DROP-1").reason == "the match ended");
        }
        {
            RWorld w;
            RoomSpec spec = spec_of("LAG-1", 3);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& a = w.connect("Ann", "LAG-1");
            RClient& b = w.connect("Bob", "LAG-1");
            RClient& c = w.connect("Cat", "LAG-1");
            w.run(3000 + kPre);
            const uint8_t bob = b.lobby->my_seat();
            const uint32_t ticks_before = w.status("LAG-1").ticks;
            b.hung = true;                                                               // Bob's program hangs: it neither acks nor answers
            w.run(8000);
            ASSERT_TRUE(w.status("LAG-1").ticks - ticks_before >= 150);                  // the room did not wait for Bob
            ASSERT_TRUE(a.session->lagging_seat() == bob && c.session->lagging_seat() == bob);
            w.run(18000);                                                                // 26 s: a lagger, not yet dropped
            ASSERT_FALSE(a.sim.is_player_dropped(bob));
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running && !w.status("LAG-1").paused);       // (a room without a way back never pauses, however long a seat is silent)
            w.run(7000);                                                                 // 33 s: dropped, at one tick for both of the others
            ASSERT_TRUE(a.sim.is_player_dropped(bob) && c.sim.is_player_dropped(bob));
            ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
            w.run(3000);
            ASSERT_TRUE(w.status("LAG-1").state == RoomState::Running);
            ASSERT_FALSE(a.lost || c.lost);
            ASSERT_EQ(a.session->lagging_seat(), 255);
        }
        {
            RWorld w;
            RoomSpec spec = spec_of("LAG-2", 2);
            spec.reconnect = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "LAG-2");
            RClient& b = w.connect("Bob", "LAG-2");
            w.run(3000 + kPre);
            b.hung = true;
            w.run(40000);
            ASSERT_TRUE(w.status("LAG-2").state == RoomState::Finished);                 // two players: the one that is left has won, the room ends
        }
    } TEST_END();

    TEST_CASE("S3.51 Silence Is A Loss After 10 s In A Room That Holds Seats (A Hung Window): The Room Pauses 10 s After The Last Word And Says So In Its Status (\"absent\", Not \"lagging\"), 9 s Of Silence Is No Loss; The Window That Wakes Up Finds Its Link Closed, Comes Back Through The Door And The Match Goes On To Its End, Identical") {
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("Z-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "Z-1");
            RClient& b = w.connect("Bob", "Z-1");
            RClient& c = w.connect("Cat", "Z-1");
            w.run(4000 + kPre);
            const uint8_t bob = b.lobby->my_seat();
            b.hung = true;
            const uint32_t at = w.now;
            uint32_t paused_after_ms = 0;
            bool noticed = false;
            for (int i = 0; i < 1100 && paused_after_ms == 0; ++i) {
                w.run(10);
                noticed = noticed || a.session->lagging_seat() == bob;
                if (w.status("Z-1").paused) paused_after_ms = w.now - at;
            }
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);          // 10 s after its last word
            ASSERT_TRUE(noticed);                                                        // (for 7 s of it the others were told that it lags)
            w.run(300);
            const RoomStatus s = w.status("Z-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.absent[0].seat == bob && !s.absent[0].catching_up);
            ASSERT_EQ(a.session->lagging_seat(), 255);                                   // the notice ended with the loss: it is missing now, not lagging
            ASSERT_TRUE(a.session->paused() && c.session->paused());
            w.run(5000);
            b.hung = false;                                                              // the window wakes up
            ASSERT_TRUE(w.until([&]() { return !w.status("Z-1").paused && b.session->mode() == net::ClientSession::Mode::Normal; }, 10000));
            ASSERT_EQ(w.status("Z-1").rejoins, 1u);
            w.play_to_the_end("Z-1");
            ASSERT_TRUE(w.status("Z-1").state == RoomState::Finished);
            ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash() && b.sim.state_hash() == c.sim.state_hash());
            ASSERT_FALSE(a.session->desynced() || b.session->desynced() || c.session->desynced());
        }
        {   // 9 s of silence is no loss: nobody is paused, the window catches up as a lagger
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("Z-2", 2), w.now).ok);
            w.connect("Ann", "Z-2");
            RClient& b = w.connect("Bob", "Z-2");
            w.run(4000);
            b.hung = true;
            bool paused_ever = false;
            for (int i = 0; i < 900; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("Z-2").paused;
            }
            b.hung = false;
            for (int i = 0; i < 2000; ++i) {
                w.run(10);
                paused_ever = paused_ever || w.status("Z-2").paused;
            }
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(w.status("Z-2").rejoins == 0 && b.session->mode() == net::ClientSession::Mode::Normal);
        }
    } TEST_END();

    // ---- the fixes of the review of release A, through the real manager and door (S3.52 - S3.57) -------------------------------------------------------------------------------

    TEST_CASE("S3.52 The Resume Countdown Through The Manager: After A Pause Of 6 s And A Return The Room Is Held For 10 s More (The Status Says \"paused\" And The Seconds Left, The Ticks Stand Still, The Pause Total Does Not Grow); The Wall-Clock Limit Counts Play And Not The Waiting Nor The Countdown; A Room With The Countdown Off Resumes At Once; A Blip Is No Countdown") {
        {
            RWorld w;
            RoomSpec s = held_spec("N-1", 3);
            s.resume_countdown_ms = 10000;
            s.run_ms = 30000;                                                            // the limit counts the match's play: 30 s of it
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            RClient& a = w.connect("Ann", "N-1");
            RClient& b = w.connect("Bob", "N-1");
            RClient& c = w.connect("Cat", "N-1");
            w.run(12000 + kPre);                                                                // 12 s of play (the match began about a second after the clients came)
            ASSERT_TRUE(w.status("N-1").state == RoomState::Running);
            b.reconnects = false;
            w.cut(b);
            w.run(12000);                                                                // 12 s of waiting
            RoomStatus st = w.status("N-1");
            ASSERT_TRUE(st.paused && st.absent.size() == 1 && st.resume_s == 0);
            const uint32_t ticks_at_pause = st.ticks;
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return w.status("N-1").absent.empty(); }, 8000));                // verified: nobody is missing now ...
            const uint32_t back_at = w.now;
            st = w.status("N-1");
            ASSERT_TRUE(st.paused && st.resume_s >= 9 && st.resume_s <= 10 && st.rejoins == 1);       // ... and the room is held: ten seconds to go
            const uint32_t pause_total = st.paused_s;
            ASSERT_TRUE(pause_total >= 11 && pause_total <= 14);
            w.run(150);                                                                  // (the Presence of the countdown reaches the players)
            ASSERT_TRUE(a.session->paused() && c.session->paused() && a.session->resume_seconds_left() >= 8 && a.session->resume_seconds_left() <= 10);
            std::vector<unsigned> seen;
            while (w.now - back_at < 9500) {
                w.run(100);
                st = w.status("N-1");
                ASSERT_TRUE(st.paused && st.absent.empty() && st.ticks <= ticks_at_pause + 2);   // the ticks stand still
                ASSERT_EQ(st.paused_s, pause_total);                                     // the pause total does not grow in the countdown: the cap is not spent by it
                if (seen.empty() || seen.back() != st.resume_s) seen.push_back(st.resume_s);
            }
            ASSERT_TRUE(seen.size() >= 9 && seen.front() >= 9 && seen.back() <= 1);
            for (size_t i = 1; i < seen.size(); ++i) ASSERT_TRUE(seen[i] < seen[i - 1]);       // counting down
            w.run(1500);
            st = w.status("N-1");
            ASSERT_TRUE(!st.paused && st.resume_s == 0 && st.ticks > ticks_at_pause + 20 && st.state == RoomState::Running);
            ASSERT_FALSE(a.session->paused() || b.session->paused() || c.session->paused());
            // 12 s of play + 12 s of waiting + 10 s of countdown + play: the room has played about 14 s, the limit is 30 s of play: it runs on (it would have failed at 22 s of "play" if the waiting and
            // the countdown counted: 12 + 12 + 10 = 34 s of wall clock already)
            w.run(10000);
            ASSERT_TRUE(w.status("N-1").state == RoomState::Running);
            w.run(20000);
            st = w.status("N-1");
            ASSERT_TRUE(st.state == RoomState::Failed && st.reason.find("took longer") != std::string::npos);      // and the limit does come, after about 30 s of play
        }
        {   // the countdown off: the return resumes the room at once (a room's setting of 0)
            RWorld w;
            RoomSpec s = held_spec("N-2", 2);
            s.resume_countdown_ms = 0;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            w.connect("Ann", "N-2");
            RClient& b = w.connect("Bob", "N-2");
            w.run(5000);
            b.reconnects = false;
            w.cut(b);
            w.run(6000);
            b.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("N-2").paused; }, 8000));
            ASSERT_EQ(w.status("N-2").resume_s, 0u);
        }
        {   // a blip (the link cut and back within a second or two): no countdown
            RWorld w;
            RoomSpec s = held_spec("N-3", 2);
            s.resume_countdown_ms = 10000;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            w.connect("Ann", "N-3");
            RClient& b = w.connect("Bob", "N-3");
            w.run(5000);
            w.cut(b);
            bool counted_down = false;
            ASSERT_TRUE(w.until([&]() {
                counted_down = counted_down || w.status("N-3").resume_s != 0;
                return w.status("N-3").rejoins == 1 && !w.status("N-3").paused;
            }, 5000));
            ASSERT_FALSE(counted_down);
        }
    } TEST_END();

    TEST_CASE("S3.53 The New Settings: \"max_catch_up_seconds\" (10 - 3600, Default 300) And \"resume_countdown_seconds\" (0 - 60, Default 10) In The Control Interface, The Server's Defaults (--max-catch-up-seconds, --resume-countdown-seconds) And The Status Keys (The Counters Of The Budgets, The Countdown); The Bounds Are Checked For Everybody; A Room Cannot Keep Fewer Connections Than It Has Players") {
        RoomManager mgr{MapStore(maps_dir())};
        const auto call = [&](const std::string& body) {
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = body;
            return handle_control(mgr, rq, 5000);
        };
        const auto parse = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ASSERT_TRUE(ServerLimits().max_catch_up_ms == 300000u && ServerLimits().resume_countdown_ms == 10000u);
        ASSERT_TRUE(RoomSpec().max_catch_up_ms == 300000u && RoomSpec().resume_countdown_ms == 10000u && RoomSpec().max_connections == Room::kMaxConnections);
        ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"Q-1"})");
        ASSERT_EQ(r.status, 201);
        ctl::JsonValue v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 300 && v.get("resume_countdown_seconds").as_int_or(99) == 10);
        ASSERT_TRUE(v.get("resume_seconds").as_int_or(99) == 0 && v.get("rejoins_refused").as_int_or(99) == 0 && v.get("catch_up_expired").as_int_or(99) == 0 && v.get("streamed_bytes").as_int_or(99) == 0);
        r = call(R"({"map":"TINY.LVL","code":"Q-2","max_catch_up_seconds":10,"resume_countdown_seconds":0})");           // the edges are good
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 10 && v.get("resume_countdown_seconds").as_int_or(99) == 0);
        r = call(R"({"map":"TINY.LVL","code":"Q-3","max_catch_up_seconds":3600,"resume_countdown_seconds":60})");
        ASSERT_EQ(r.status, 201);
        v = parse(r);
        ASSERT_TRUE(v.get("max_catch_up_seconds").as_int_or(0) == 3600 && v.get("resume_countdown_seconds").as_int_or(0) == 60);
        for (const char* bad : {R"("max_catch_up_seconds":9)", R"("max_catch_up_seconds":3601)", R"("max_catch_up_seconds":0)", R"("max_catch_up_seconds":-1)", R"("max_catch_up_seconds":"300")", R"("max_catch_up_seconds":300.5)",
                                R"("resume_countdown_seconds":61)", R"("resume_countdown_seconds":-1)", R"("resume_countdown_seconds":"10")", R"("resume_countdown_seconds":10.5)", R"("resume_countdown_seconds":true)"}) {
            r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            const std::string key = std::string(bad).find("max_catch_up") != std::string::npos ? "\"max_catch_up_seconds\" must be an integer from 10 to 3600" : "\"resume_countdown_seconds\" must be an integer from 0 to 60";
            ASSERT_MSG(parse(r).get("error").str() == key, parse(r).get("error").str());
        }
        ASSERT_EQ(mgr.room_count(), size_t{3});
        {   // the server's defaults reach a room that says nothing; what a room says wins
            ServerLimits limits;
            limits.max_catch_up_ms = 120000;
            limits.resume_countdown_ms = 5000;
            RoomManager other{MapStore(maps_dir()), limits};
            const auto call2 = [&](const std::string& body) {
                ctl::HttpRequest rq;
                rq.method = "POST";
                rq.path = "/rooms";
                rq.body = body;
                return parse(handle_control(other, rq, 5000));
            };
            ctl::JsonValue d = call2(R"({"map":"TINY.LVL","code":"D-1"})");
            ASSERT_TRUE(d.get("max_catch_up_seconds").as_int_or(0) == 120 && d.get("resume_countdown_seconds").as_int_or(0) == 5);
            d = call2(R"({"map":"TINY.LVL","code":"D-2","resume_countdown_seconds":0,"max_catch_up_seconds":30})");
            ASSERT_TRUE(d.get("max_catch_up_seconds").as_int_or(0) == 30 && d.get("resume_countdown_seconds").as_int_or(9) == 0);
            ASSERT_TRUE(other.default_spec().max_catch_up_ms == 120000 && other.default_spec().resume_countdown_ms == 5000);
        }
        {   // create_room checks the bounds for everybody who does not come through the JSON
            RoomManager direct{MapStore(maps_dir())};
            const auto made = [&](const std::string& code, const std::function<void(RoomSpec&)>& change) {
                RoomSpec spec = spec_of(code, 2);
                change(spec);
                return direct.create_room(spec, 5000);
            };
            ASSERT_EQ(made("C-1", [](RoomSpec& s) { s.max_catch_up_ms = 9999; }).http_status, 400);
            ASSERT_EQ(made("C-2", [](RoomSpec& s) { s.max_catch_up_ms = 3600001; }).http_status, 400);
            ASSERT_EQ(made("C-3", [](RoomSpec& s) { s.resume_countdown_ms = 60001; }).http_status, 400);
            ASSERT_EQ(made("C-4", [](RoomSpec& s) { s.max_connections = 1; }).http_status, 400);                    // fewer than the players
            ASSERT_EQ(made("C-5", [](RoomSpec& s) { s.max_connections = 5000; }).http_status, 400);
            ASSERT_TRUE(made("C-6", [](RoomSpec& s) { s.max_catch_up_ms = 10000; s.resume_countdown_ms = 0; s.max_connections = 2; }).ok);
            ASSERT_TRUE(made("C-7", [](RoomSpec& s) { s.max_catch_up_ms = 3600000; s.resume_countdown_ms = 60000; s.max_connections = 4096; }).ok);
        }
    } TEST_END();

    TEST_CASE("S3.54 A Key Holder Cannot Freeze A Room Through The Door (The Real Server): A Hello Every 12 s, Each With A Little Progress, Is A Catch-Up Of 30 s (The Room's Budget) And Then Refused; The Status Says It (Expired, Refused, Absent Again), The Others May Vote, The Cap (90 s) Takes The Seat And The Match Goes On To Its End") {
        RWorld w;
        RoomSpec s = held_spec("W-1", 3);
        s.max_catch_up_ms = 30000;
        s.max_pause_ms = 90000;
        ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
        RClient& a = w.connect("Ann", "W-1");
        RClient& b = w.connect("Bob", "W-1");
        RClient& c = w.connect("Cat", "W-1");
        w.run(6000 + kPre);
        ASSERT_TRUE(w.status("W-1").state == RoomState::Running);
        const net::SeatKey key = c.lobby->key();
        const uint8_t cat = c.lobby->my_seat();
        c.reconnects = false;
        w.cut(c);
        const uint32_t cut_at = w.now;
        w.run(200);
        ASSERT_TRUE(w.status("W-1").paused);
        const uint32_t ticks_at_pause = w.status("W-1").ticks;
        std::vector<net::Connection*> links;
        uint32_t next_hello = w.now;
        uint32_t next_ack = w.now + 3000;
        uint32_t total = 0;
        uint32_t creeping = 0;
        const auto step = [&]() {
            w.run(10);
            if (static_cast<int32_t>(w.now - next_hello) >= 0) {
                net::Connection* link = w.open_link();
                net::HelloMsg h;
                h.name = "Eve";
                h.room = "W-1";
                h.key = key;
                h.have_turns = 1;
                link->send(net::encode(h));
                links.push_back(link);
                next_hello = w.now + 12000;
            }
            for (net::Connection* l : links) {
                std::vector<uint8_t> msg;
                while (l->poll(msg)) {
                    net::CatchUpMsg cu;
                    if (net::peek_type(msg) == net::MsgType::CatchUp && net::decode(msg, cu)) total = cu.total_turns;
                }
            }
            if (static_cast<int32_t>(w.now - next_ack) >= 0 && total > 100 && !links.empty() && links.back()->is_open()) {
                creeping = std::min(total, creeping + std::max<uint32_t>(total / 40, 2));
                net::AckMsg ack;
                ack.turn = creeping - 1;
                links.back()->send(net::encode(ack));
                next_ack = w.now + 3000;
            }
        };
        while (w.now - cut_at < 25000) step();
        RoomStatus st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.absent.size() == 1 && st.absent[0].seat == cat && st.absent[0].catching_up && st.absent[0].progress > 0);       // 25 s into a catch-up that shows progress
        ASSERT_EQ(st.catch_up_expired, 0u);
        while (w.now - cut_at < 33000) step();
        st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.absent.size() == 1 && !st.absent[0].catching_up);                // 33 s: the budget of 30 s is spent: it is absent again
        ASSERT_EQ(st.catch_up_expired, 1u);
        ASSERT_TRUE(st.rejoins == 0);
        ASSERT_TRUE(st.vote_seat == cat);                                                            // and the others are asked about it
        while (w.now - cut_at < 40000) step();
        ASSERT_TRUE(w.status("W-1").rejoins_refused >= 1);                                           // (the Hello of 36 s: a seat whose catch-up budget is spent)
        while (w.now - cut_at < 80000) step();
        st = w.status("W-1");
        ASSERT_TRUE(st.paused && st.ticks <= ticks_at_pause + 2);
        ASSERT_TRUE(st.rejoins_refused >= 3);
        const uint64_t refused_before = w.mgr.connections_refused();
        while (w.now - cut_at < 95000) step();
        st = w.status("W-1");
        ASSERT_TRUE(!st.paused && st.state == RoomState::Running && st.drops_by_cap == 1 && st.drops_by_vote == 0);       // the cap: the seat is dropped and the match goes on
        ASSERT_TRUE(st.rejoins == 0 && st.absent.empty());
        (void)refused_before;
        w.run(3000);
        ASSERT_TRUE(w.status("W-1").ticks > ticks_at_pause + 40);
        w.play_to_the_end("W-1");
        ASSERT_TRUE(w.status("W-1").state == RoomState::Finished);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        ASSERT_TRUE(a.sim.is_player_dropped(cat) && b.sim.is_player_dropped(cat));
        ASSERT_FALSE(a.session->desynced() || b.session->desynced());
    } TEST_END();

    TEST_CASE("S3.55 A Key Holder Cannot Make The Server Stream Its Log Again And Again (The Real Server): With A Log Of More Than A Megabyte, 100 Hellos In 10 s Make Three Streams At Most; The Status Counts What Was Refused And What Was Streamed (3 x The Log At The Most), A Key Is In None Of It; The Others Are Not Disturbed And The Seat Is Held: The Vote And The Cap Apply") {
        std::ostringstream captured;
        std::streambuf* old_cerr = std::cerr.rdbuf(captured.rdbuf());
        {
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("M-1", 3), w.now).ok);
            RClient& a = w.connect("Ann", "M-1");
            RClient& b = w.connect("Bob", "M-1");
            RClient& c = w.connect("Cat", "M-1");
            w.run(6000 + kPre);
            ASSERT_TRUE(w.status("M-1").state == RoomState::Running);
            a.orders = false;
            b.orders = false;
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            for (int i = 0; i < 800 && w.status("M-1").log_bytes <= 1200u * 1024u; ++i) {         // two players put commands of 32 ants into every turn: a log of more than a megabyte
                w.run(10);
                for (net::Connection* end : {a.end, b.end}) {
                    for (int k = 0; k < 8; ++k) {
                        net::CommandMsg msg;
                        msg.command.type = sim::CommandType::GroupMove;
                        msg.command.issuer = 0;
                        msg.command.tile_x = 5;
                        msg.command.tile_y = 5;
                        for (const uint32_t id : many) msg.command.ants.push_back(id);          // (a loop: GCC 12 reads the copy of a vector as a null memmove)
                        end->send(net::encode(msg));
                    }
                }
            }
            const uint32_t log_bytes = w.status("M-1").log_bytes;
            ASSERT_TRUE(log_bytes > 1024u * 1024u && w.status("M-1").log_usable);
            const net::SeatKey key = c.lobby->key();
            c.reconnects = false;
            w.cut(c);
            w.run(300);
            ASSERT_TRUE(w.status("M-1").paused);
            std::vector<net::Connection*> links;
            uint32_t sent = 0;
            for (int i = 0; i < 1400; ++i) {                                                     // 100 Hellos in 10 s, and 4 s more
                w.run(10);
                if (sent < 100 && i % 10 == 0) {
                    net::Connection* link = w.open_link();
                    net::HelloMsg h;
                    h.name = "Eve";
                    h.room = "M-1";
                    h.key = key;
                    h.have_turns = 1;
                    link->send(net::encode(h));
                    links.push_back(link);
                    ++sent;
                }
                for (net::Connection* l : links) {
                    std::vector<uint8_t> msg;
                    while (l->poll(msg)) {
                        net::TurnBatchMsg batch;
                        if (net::peek_type(msg) == net::MsgType::TurnBatch && net::decode(msg, batch) && !batch.turns.empty()) {
                            net::AckMsg ack;
                            ack.turn = batch.first_turn + static_cast<uint32_t>(batch.turns.size()) - 1;
                            l->send(net::encode(ack));
                        }
                    }
                }
            }
            ASSERT_EQ(sent, 100u);
            const RoomStatus st = w.status("M-1");
            ASSERT_TRUE(st.streamed_bytes <= 3 * uint64_t{st.log_bytes} && st.streamed_bytes >= uint64_t{st.log_bytes} / 2);        // at most three streams of the log (a hundred before)
            ASSERT_TRUE(st.rejoins_refused >= 90);                                                // every refusal counted
            ASSERT_EQ(st.rejoins, 0u);
            ASSERT_TRUE(st.state == RoomState::Running && !a.lost && !b.lost);
            ASSERT_TRUE(st.absent.size() == 1 && st.absent[0].seat == c.lobby->my_seat());          // the seat is held (catching up or absent): the vote and the cap apply
            // the status says it in JSON, and a key is nowhere in it
            ctl::HttpRequest rq;
            rq.method = "GET";
            rq.path = "/rooms/M-1";
            const ctl::HttpResponse r = handle_control(w.mgr, rq, w.now);
            ctl::JsonValue v;
            std::string why;
            ASSERT_TRUE(ctl::parse_json(r.body, v, &why));
            ASSERT_TRUE(v.get("rejoins_refused").as_int_or(0) >= 90 && v.get("streamed_bytes").as_int_or(0) > 0 && v.get("streamed_bytes").as_int_or(0) <= 3 * int64_t{log_bytes});
            ASSERT_TRUE(r.body.find(hex_of(key)) == std::string::npos && r.body.find("key") == std::string::npos);
            ASSERT_TRUE(captured.str().find(hex_of(key)) == std::string::npos);
        }
        std::cerr.rdbuf(old_cerr);
        ASSERT_TRUE(captured.str().empty());
    } TEST_END();

    TEST_CASE("S3.56 Hostile Rooms Cannot Exhaust The Way Back For Everybody (The Real Server): 16 Rooms Whose Players Fill Their Logs Are Dead After A Few Seconds (Each Log Frees What It Held The Moment It Dies, The Server's Budget Is Whole Again), And A Room That Is Made Afterwards Keeps Its Log, Holds A Lost Seat And Takes It Back") {
        ServerLimits limits;
        limits.log_budget_bytes = 3u * 1024u * 1024u;
        RWorld w(limits);
        for (int i = 0; i < 16; ++i) {
            RoomSpec s = held_spec("H" + std::to_string(i), 2);
            s.max_log_bytes = 256 * 1024;
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
        }
        std::vector<RClient*> hostile;
        for (int i = 0; i < 16; ++i) {
            hostile.push_back(&w.connect("A" + std::to_string(i), "H" + std::to_string(i)));
            hostile.push_back(&w.connect("B" + std::to_string(i), "H" + std::to_string(i)));
        }
        w.run(4000 + kPre);
        for (int i = 0; i < 16; ++i) ASSERT_TRUE(w.status("H" + std::to_string(i)).state == RoomState::Running);
        for (RClient* c : hostile) c->orders = false;
        std::vector<uint32_t> many;
        for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
        net::CommandMsg flood;
        flood.command.type = sim::CommandType::GroupMove;
        flood.command.tile_x = 5;
        flood.command.tile_y = 5;
        for (const uint32_t id : many) flood.command.ants.push_back(id);            // (a loop: GCC 12 reads the copy of a vector as a null memmove)
        const std::vector<uint8_t> flood_bytes = net::encode(flood);
        uint64_t most = 0;
        for (int i = 0; i < 700; ++i) {                                                           // 7 s: every player fills its room's log
            w.run(10);
            for (RClient* c : hostile) {
                if (c->end != nullptr && c->end->is_open()) {
                    for (int k = 0; k < 4; ++k) c->end->send(flood_bytes);
                }
            }
            most = std::max(most, w.mgr.log_bytes());
            ASSERT_TRUE(w.mgr.log_bytes() <= w.mgr.log_budget_bytes());                           // never more than the budget
        }
        ASSERT_TRUE(most > 1024 * 1024);                                                          // (they did fill it)
        int dead = 0;
        for (int i = 0; i < 16; ++i) {
            const RoomStatus s = w.status("H" + std::to_string(i));
            dead += !s.log_usable ? 1 : 0;
            if (!s.log_usable) ASSERT_TRUE(s.log_bytes == 0 && s.log_turns == 0);                  // a dead log holds nothing
        }
        ASSERT_EQ(dead, 16);                                                                      // every log of a room that was made to fill it is dead
        ASSERT_EQ(w.mgr.log_bytes(), uint64_t{0});                                                // ... and the budget is whole again (the dead logs used to keep it for ever)
        for (RClient* c : hostile) c->orders = false;
        // the good room: made after the hostile ones died, it logs, holds a lost seat and gives it back
        ASSERT_TRUE(w.mgr.create_room(held_spec("GOOD", 2), w.now).ok);
        w.connect("Ann", "GOOD");
        RClient& bob = w.connect("Bob", "GOOD");
        w.run(5000 + kPre);
        RoomStatus g = w.status("GOOD");
        ASSERT_TRUE(g.state == RoomState::Running && g.log_usable && g.log_bytes > 0 && w.mgr.log_bytes() > 0);
        bob.reconnects = false;
        w.cut(bob);
        w.run(1000);
        g = w.status("GOOD");
        ASSERT_TRUE(g.paused && g.absent.size() == 1);                                            // held, not dropped at once
        bob.reconnects = true;
        ASSERT_TRUE(w.until([&]() { return !w.status("GOOD").paused; }, 8000));
        ASSERT_EQ(w.status("GOOD").rejoins, 1u);
        ASSERT_FALSE(bob.lost);
    } TEST_END();

    TEST_CASE("S3.57 A Room Keeps At Most As Many Connections As Its Setting Says, Whatever Comes Through The Door With A Key: With Room For Only The Three That Play, A Returning Player Is Told \"Full\" (The Room Is Not Grown, Nothing Leaks, The Seat Stays Held); With Room To Spare The Same Player Comes Back")  {
        {
            RWorld w;
            RoomSpec s = held_spec("L-1", 3);
            s.max_connections = 3;                                                                // exactly the connections of the lobby: nothing can be pruned
            ASSERT_TRUE(w.mgr.create_room(s, w.now).ok);
            RClient& a = w.connect("Ann", "L-1");
            RClient& b = w.connect("Bob", "L-1");
            RClient& c = w.connect("Cat", "L-1");
            w.run(5000 + kPre);
            ASSERT_TRUE(w.status("L-1").state == RoomState::Running);
            ASSERT_EQ(w.status("L-1").connections, 3u);
            b.reconnects = true;
            w.cut(b);
            ASSERT_TRUE(w.until([&]() { return b.lost; }, 8000));
            ASSERT_TRUE(b.was_rejected && b.rejected == net::RejectReason::Full);                 // the answer, in words that say what it is
            const RoomStatus st = w.status("L-1");
            ASSERT_TRUE(st.state == RoomState::Running && st.paused && st.absent.size() == 1 && st.absent[0].seat == b.lobby->my_seat());      // the seat is held
            ASSERT_EQ(st.connections, 3u);                                                        // the room did not grow
            ASSERT_EQ(st.rejoins, 0u);
            ASSERT_FALSE(a.lost || c.lost);
        }
        {   // with room to spare (the default) the same cut is a return
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(held_spec("L-2", 3), w.now).ok);
            w.connect("Ann", "L-2");
            RClient& b = w.connect("Bob", "L-2");
            w.connect("Cat", "L-2");
            w.run(5000);
            w.cut(b);
            ASSERT_TRUE(w.until([&]() { return w.status("L-2").rejoins == 1 && !w.status("L-2").paused; }, 8000));
            ASSERT_FALSE(b.lost);
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records (restart_record.hpp, docs/NETWORK_PORT.md "Restart records"): S3.82 and on
// ---------------------------------------------------------------------------------------------------------------------------------


void run_persist_tests() {
    TEST_CASE("S3.82 The Record: A Head And Its Turns And Checkpoints Read Back Exactly (Every Field, Every Command, The Keys); The File Is For Its Owner Only (Mode 600 In A Folder Of Mode 700), Has Its Own Name (Two Codes That Differ In Case Differ), Is Made All At Once (A Process That Dies At Its First Write Leaves No Record Under Its Name), A Torn Tail Is Cut Off When It Is Opened Again; The Budget Follows Every Byte; The CRC Is CRC-32") {
        {   // CRC-32: the check value of the standard, and in pieces
            const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
            ASSERT_EQ(restart_crc32(digits, 9), 0xCBF43926u);
            ASSERT_EQ(restart_crc32(digits + 4, 5, restart_crc32(digits, 4)), 0xCBF43926u);
            ASSERT_EQ(restart_crc32(digits, 0), 0u);
        }
        RestartConfig cfg = test_restart_config("rec-format");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        ASSERT_TRUE(fs::is_directory(cfg.dir));
#ifndef _WIN32
        {
            struct stat st;
            ASSERT_EQ(::stat(cfg.dir.c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0700);                       // the folder is the server's own
        }
#endif
        // the name: the code, a hash of the code (so that "ABC" and "abc" are two files even where the file system ignores case), an extension
        {
            const auto lower = [](std::string text) {
                for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return text;
            };
            ASSERT_TRUE(lower(store.path_for("ABC")) != lower(store.path_for("abc")));      // two files even where the file system ignores case (the names differ without it)
        }
        {
            const std::string name = fs::path(store.path_for("ROOM-1")).filename().string();
            const std::string ext = kRestartExtension;
            ASSERT_TRUE(name.rfind("room-ROOM-1-", 0) == 0 && name.size() == std::string("room-ROOM-1-").size() + 8 + ext.size());
            ASSERT_TRUE(name.compare(name.size() - ext.size(), ext.size(), ext) == 0);
        }
        const RestartHead head = sample_head();
        auto w = store.create(head, why);
        ASSERT_TRUE(w != nullptr);
        ASSERT_EQ(w->path(), store.path_for("REC-1"));
        ASSERT_EQ(store.open_records(), size_t{1});
        ASSERT_TRUE(store.records().size() == 1 && store.records()[0] == w->path());
        ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});                                // (no temporary file is left behind)
        ASSERT_EQ(w->turns(), 0u);
        {
            const RestartLoaded only_head = read_restart_record(w->path(), cfg.max_record_bytes);       // a record with a head and nothing else is a record
            ASSERT_TRUE(only_head.ok() && only_head.turns.empty() && only_head.checks.empty() && !only_head.torn && same_head(only_head.head, head));
        }
        std::vector<net::TurnMsg> sent;
        std::vector<RestartCheck> checks;
        for (uint32_t n = 0; n < 100; ++n) {
            sent.push_back(sample_turn(n));
            ASSERT_TRUE(w->append_turn(sent.back()));
            if ((n + 1) % net::kHashEveryTurns == 0) {
                checks.push_back(RestartCheck{n, 0x0102030405060708ull + n});
                ASSERT_TRUE(w->append_check(n, checks.back().hash));
            }
        }
        ASSERT_EQ(w->turns(), 100u);
        ASSERT_TRUE(w->dirty());
        ASSERT_TRUE(w->sync());
        ASSERT_FALSE(w->dirty());
        ASSERT_EQ(fs::file_size(w->path()), w->bytes());
        ASSERT_EQ(store.used_bytes(), w->bytes());                                      // the budget follows every byte of the file
        {
            const RestartLoaded r = read_restart_record(w->path(), cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.why.empty());
            ASSERT_TRUE(same_head(r.head, head));                                       // every field, the start message, the bots, the fill, the two keys
            ASSERT_EQ(r.turns.size(), size_t{100});
            ASSERT_TRUE(same_turns(r.turns, sent, 100));                                // every command of every turn, with its ants
            ASSERT_EQ(r.checks.size(), size_t{5});
            ASSERT_TRUE(same_checks(r.checks, checks, 5));
            ASSERT_TRUE(r.good_bytes == r.file_bytes && r.file_bytes == fs::file_size(w->path()) && !r.torn && r.path == w->path());
        }
#ifndef _WIN32
        {
            struct stat st;
            ASSERT_EQ(::stat(w->path().c_str(), &st), 0);
            ASSERT_EQ(static_cast<int>(st.st_mode & 0777), 0600);                       // it holds the keys of the seats: for its owner only
        }
#endif
        {   // a writer that is closed leaves the file where it is, and gives its bytes back
            const std::string path = w->path();
            const uint64_t size = w->bytes();
            w.reset();
            ASSERT_TRUE(fs::exists(path) && fs::file_size(path) == size);
            ASSERT_EQ(store.used_bytes(), 0u);
            ASSERT_EQ(store.open_records(), size_t{0});
            // opened again to go on: the budget takes the file, the next turn is number 100
            auto again = store.reopen(path, size, 100, why);
            ASSERT_TRUE(again != nullptr);
            ASSERT_EQ(store.used_bytes(), size);
            ASSERT_EQ(again->turns(), 100u);
            ASSERT_TRUE(again->append_turn(sample_turn(100)));
            ASSERT_TRUE(again->sync());
            sent.push_back(sample_turn(100));
            const RestartLoaded r = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.turns.size() == 101 && same_turns(r.turns, sent, 101));
            // a write that was cut short (a torn tail): what follows the last whole frame is cut off when the record is opened again, and the next frame follows the last good one
            again.reset();
            std::vector<uint8_t> bytes = read_all_bytes(path);
            const std::vector<uint8_t> whole = bytes;
            const std::vector<uint8_t> next = test_frame(2, turns_payload(101, {sample_turn(101)}));
            bytes.insert(bytes.end(), next.begin(), next.begin() + 9);                  // 9 bytes of the next frame: a header and a few bytes of its payload
            write_all_bytes(path, bytes);
            const RestartLoaded torn = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(torn.ok() && torn.torn && torn.turns.size() == 101 && torn.good_bytes == whole.size() && torn.file_bytes == whole.size() + 9);
            auto cut = store.reopen(path, torn.good_bytes, static_cast<uint32_t>(torn.turns.size()), why);
            ASSERT_TRUE(cut != nullptr);
            ASSERT_EQ(fs::file_size(path), whole.size());                               // the torn bytes are gone
            ASSERT_TRUE(cut->append_turn(sample_turn(101)));
            cut.reset();
            sent.push_back(sample_turn(101));
            const RestartLoaded clean = read_restart_record(path, cfg.max_record_bytes);
            ASSERT_TRUE(clean.ok() && !clean.torn && clean.turns.size() == 102 && same_turns(clean.turns, sent, 102));
            // the file must not be shorter than what the caller says is good (the caller read another file): refused, nothing changes
            ASSERT_TRUE(store.reopen(path, fs::file_size(path) + 1, 102, why) == nullptr && !why.empty());
            ASSERT_EQ(store.used_bytes(), 0u);
        }
        {   // a second head for the same code replaces the first, all at once (a room that is made again with a code that an old record still has)
            auto first = store.create(head, why);
            ASSERT_TRUE(first != nullptr && first->append_turn(sample_turn(0)));
            RestartHead other = head;
            other.start.seed = 99;
#ifdef _WIN32
            // (Windows cannot replace a file that is open: while the first writer holds it the second head is refused and the first record stays whole; a room's
            // writer is always closed before its code can be made again, and then the second head replaces the record as everywhere)
            ASSERT_TRUE(store.create(other, why) == nullptr && !why.empty());
            const RestartLoaded kept = read_restart_record(first->path(), cfg.max_record_bytes);
            ASSERT_TRUE(kept.ok() && kept.turns.size() == 1 && kept.head.start.seed == head.start.seed);
            ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});
            first.reset();
#endif
            auto second = store.create(other, why);
            ASSERT_TRUE(second != nullptr);
            const RestartLoaded r = read_restart_record(second->path(), cfg.max_record_bytes);
            ASSERT_TRUE(r.ok() && r.turns.empty() && r.head.start.seed == 99);
            ASSERT_EQ(entries_but_lock(cfg.dir), size_t{1});
        }
        {   // a writer that is told to discard its record deletes the file and gives its bytes back, and writes no more
            store.remove_file(store.path_for("REC-1"));
            auto d = store.create(head, why);
            ASSERT_TRUE(d != nullptr && d->append_turn(sample_turn(0)));
            const std::string path = d->path();
            d->discard();
            ASSERT_FALSE(fs::exists(path));
            ASSERT_TRUE(d->failed());
            ASSERT_FALSE(d->append_turn(sample_turn(1)));
            ASSERT_FALSE(fs::exists(path));
            d.reset();
            ASSERT_EQ(store.used_bytes(), 0u);
            ASSERT_EQ(store.open_records(), size_t{0});
        }
        {   // a turn that is not the next one fails the writer: a record with a hole is no record; and it stays failed
            auto f = store.create(head, why);
            ASSERT_TRUE(f != nullptr && f->append_turn(sample_turn(0)));
            ASSERT_FALSE(f->append_turn(sample_turn(5)));
            ASSERT_TRUE(f->failed() && f->error().find("order") != std::string::npos);
            ASSERT_FALSE(f->append_turn(sample_turn(1)));
            ASSERT_FALSE(f->sync());
            f->discard();
        }
        {   // the limits: a head that is bigger than a record may be, a budget that does not hold a record, a folder that is gone; nothing is left behind and the budget is whole
            RestartConfig tiny = cfg;
            tiny.max_record_bytes = 100;
            RestartStore small_store(tiny);
            ASSERT_TRUE(small_store.create(head, why) == nullptr && why.find("bigger") != std::string::npos);
            RestartConfig poor = cfg;
            poor.budget_bytes = 50;
            RestartStore poor_store(poor);
            ASSERT_TRUE(poor_store.create(head, why) == nullptr && why.find("budget") != std::string::npos);
            ASSERT_EQ(poor_store.used_bytes(), 0u);
            RestartConfig gone = test_restart_config("rec-gone");
            RestartStore gone_store(gone);                                              // (prepare() was never called: the folder does not exist)
            ASSERT_TRUE(gone_store.create(head, why) == nullptr && !why.empty());
            ASSERT_EQ(gone_store.used_bytes(), 0u);
            ASSERT_EQ(gone_store.open_records(), size_t{0});
            RestartStore off{RestartConfig{}};                                          // a server that keeps no records
            ASSERT_FALSE(off.enabled());
            ASSERT_TRUE(off.create(head, why) == nullptr && off.records().empty());
        }
        {   // stale temporary files of a start that died are removed when the folder is prepared; records are not
            const fs::path stale = fs::path(cfg.dir) / "room-OLD-0000abcd.restart.0123456789abcdef.tmp";
            write_all_bytes(stale, {1, 2, 3});
            const fs::path other = fs::path(cfg.dir) / "notes.txt";
            write_all_bytes(other, {'x'});
            auto keep = store.create(head, why);
            ASSERT_TRUE(keep != nullptr);
            ASSERT_TRUE(store.prepare(why));
            ASSERT_FALSE(fs::exists(stale));
            ASSERT_TRUE(fs::exists(other) && fs::exists(store.path_for("REC-1")));      // (what is not a temporary file of a record is left alone)
        }
#ifndef _WIN32
        {   // made all at once: a process whose first write kills it (RLIMIT_FSIZE: SIGXFSZ) leaves no record under the record's name, and the next start of the folder is clean
            RestartConfig c = test_restart_config("rec-atomic");
            RestartStore prepared(c);
            ASSERT_TRUE(prepared.prepare(why));
            const pid_t pid = ::fork();
            ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                struct rlimit none = {0, 0};
                ::setrlimit(RLIMIT_CORE, &none);
                ::setrlimit(RLIMIT_FSIZE, &none);
                ::signal(SIGXFSZ, SIG_DFL);
                RestartStore child(c);
                std::string child_why;
                (void)child.create(head, child_why);
                ::_exit(0);                                                             // not reached: the write kills the process
            }
            int wait_status = 0;
            ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
            ASSERT_TRUE(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGXFSZ);
            ASSERT_TRUE(prepared.records().empty());                                    // no record under any name that a start would read
            ASSERT_TRUE(prepared.prepare(why));                                         // the temporary file of the dead process is removed
            ASSERT_EQ(entries_but_lock(c.dir), size_t{0});
            auto after = prepared.create(head, why);
            ASSERT_TRUE(after != nullptr);
        }
#endif
    } TEST_END();

    TEST_CASE("S3.82b The Flags Of A Head (Protocol 15): Fog, The Early Start And A Public Room Are Bits 0 - 2 Of One Byte And Read Back In Every Combination; Any Other Bit Is A Flag That This Build Does Not Know, And The Head Is Refused") {
        const RestartHead sample = sample_head();
        const std::vector<uint8_t> whole = encode_restart_head(sample);
        const std::vector<uint8_t> payload(whole.begin() + 5, whole.end() - 4);                  // (type and length in front, the CRC behind)
        // the flags byte comes after the format, the version, the protocol, the build, the code, the map, the map's hash and the players
        const size_t at = 2 + 1 + sample.identity.game_version.size() + 2 + 1 + sample.identity.build_id.size() + 1 + sample.code.size() + 1 + sample.map.size() + 8 + 1;
        ASSERT_TRUE(at < payload.size() && payload[at] == 2);                                    // (the sample has the early start only: the offset is right)
        for (unsigned mask = 0; mask < 8; ++mask) {
            RestartHead h = sample_head();
            h.fog = (mask & 1u) != 0;
            h.start.fog = h.fog;                                                                    // (the head's parts must agree: the Start in it is the room's, fog included)
            h.early_start = (mask & 2u) != 0;
            h.public_room = (mask & 4u) != 0;
            const std::vector<uint8_t> frame = encode_restart_head(h);
            ASSERT_TRUE(frame.size() == whole.size() && frame[5 + at] == mask);                  // the three flags are the three low bits
            RestartHead read;
            std::string why;
            ASSERT_TRUE(decode_restart_head(frame.data() + 5, frame.size() - 9, read, why) && why.empty());
            ASSERT_TRUE(same_head(read, h) && read.fog == h.fog && read.early_start == h.early_start && read.public_room == h.public_room);
        }
        for (const unsigned bit : {8u, 16u, 32u, 64u, 128u}) {
            std::vector<uint8_t> changed = payload;
            changed[at] = static_cast<uint8_t>(changed[at] | bit);
            RestartHead read;
            std::string why;
            ASSERT_FALSE(decode_restart_head(changed.data(), changed.size(), read, why));
            ASSERT_TRUE(why.find("flags that this build does not know") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.83 The Reader Is Safe With Anything: Every Prefix Of A Record Is Read As Far As It Is Whole (A Cut Inside The Head Is Refused, A Cut After It Is A Torn Tail); Every Flipped Bit In A Frame Is Either Refused Or Stops The Reading Early (Never Altered Turns); Garbage, Hostile Lengths, Repeated, Reordered And Misplaced Frames, A Second Head, A Checkpoint Of A Turn That Is Not There, Heads That Are Wrong In Each Field, A File That Is Too Big Or No File: All Refused With A Reason, None Crashes Or Allocates Without Bound (Runs Under AddressSanitizer)") {
        RestartConfig cfg = test_restart_config("rec-fuzz");
        RestartStore store(cfg);
        std::string why;
        ASSERT_TRUE(store.prepare(why));
        const RestartHead head = sample_head();
        auto w = store.create(head, why);
        ASSERT_TRUE(w != nullptr);
        for (uint32_t n = 0; n < 61; ++n) {
            ASSERT_TRUE(w->append_turn(sample_turn(n)));
            if ((n + 1) % net::kHashEveryTurns == 0) ASSERT_TRUE(w->append_check(n, 0xAA00u + n));
        }
        ASSERT_TRUE(w->sync());
        const std::string path = w->path();
        w.reset();
        const std::vector<uint8_t> good = read_all_bytes(path);
        const RestartLoaded ref = parse_restart_record(good.data(), good.size());
        ASSERT_TRUE(ref.ok() && ref.turns.size() == 61 && ref.checks.size() == 3 && !ref.torn);
        const std::vector<std::pair<size_t, size_t>> frames = frames_of(good);
        ASSERT_EQ(frames.size(), size_t{1 + 61 + 3});
        ASSERT_EQ(frames.back().second, good.size());
        const size_t head_end = frames[0].second;
        // what a reader may hand out for a damaged file: the head of the record and a PREFIX of its turns and checkpoints, never anything else
        const auto prefix_of_reference = [&](const RestartLoaded& r) {
            return same_head(r.head, ref.head) && r.turns.size() <= ref.turns.size() && same_turns(r.turns, ref.turns, r.turns.size()) && r.checks.size() <= ref.checks.size() &&
                   same_checks(r.checks, ref.checks, r.checks.size());
        };
        {   // every prefix: refused inside the head, read as far as it is whole after it
            std::vector<size_t> boundaries;
            for (const auto& f : frames) boundaries.push_back(f.second);
            size_t last_turns = 0;
            for (size_t n = 0; n <= good.size(); ++n) {
                const RestartLoaded r = parse_restart_record(good.data(), n);
                if (n < head_end) {
                    ASSERT_FALSE(r.ok());
                    ASSERT_TRUE(r.status == (n < sizeof(kRestartMagic) ? RestartLoaded::Status::NotARecord : RestartLoaded::Status::Corrupt) && !r.why.empty());
                    continue;
                }
                ASSERT_TRUE(r.ok() && prefix_of_reference(r));
                ASSERT_TRUE(r.good_bytes <= n && r.torn == (r.good_bytes < n) && std::find(boundaries.begin(), boundaries.end(), r.good_bytes) != boundaries.end());
                ASSERT_TRUE(r.turns.size() >= last_turns);                              // (a longer prefix never holds fewer turns)
                last_turns = r.turns.size();
            }
            ASSERT_EQ(last_turns, size_t{61});
        }
        {   // every flipped bit
            const uint8_t masks[] = {0x01, 0x10, 0x80, 0xFF};
            for (size_t p = 0; p < good.size(); ++p) {
                for (const uint8_t mask : masks) {
                    std::vector<uint8_t> bad = good;
                    bad[p] = static_cast<uint8_t>(bad[p] ^ mask);
                    const RestartLoaded r = parse_restart_record(bad.data(), bad.size());
                    if (r.ok()) ASSERT_TRUE(prefix_of_reference(r));                    // whatever is accepted is the record itself, cut short: never different turns
                    if (p < sizeof(kRestartMagic)) {                                    // the magic: not a record of this format
                        ASSERT_FALSE(r.ok());
                        continue;
                    }
                    size_t fi = 0;
                    while (fi + 1 < frames.size() && p >= frames[fi].second) ++fi;
                    const size_t offset = p - frames[fi].first;
                    if (fi + 1 < frames.size() && (offset == 0 || offset >= 5)) {
                        ASSERT_FALSE(r.ok());                                           // a flipped type, payload or checksum before the last frame: refused
                        ASSERT_TRUE(r.status == RestartLoaded::Status::Corrupt && !r.why.empty());
                    }
                }
            }
        }
        {   // the last frame with a wrong checksum is a write that was interrupted: a torn tail (the record is the frames before it); the same fault with a frame behind it is corruption
            std::vector<uint8_t> bad = good;
            bad[frames.back().second - 6] = static_cast<uint8_t>(bad[frames.back().second - 6] ^ 0x55);
            const RestartLoaded r = parse_restart_record(bad.data(), bad.size());
            ASSERT_TRUE(r.ok() && r.torn && r.good_bytes == frames.back().first && r.turns.size() == 60 && r.checks.size() == 3 && prefix_of_reference(r));
            std::vector<uint8_t> mid = good;
            mid[frames[frames.size() - 2].second - 6] = static_cast<uint8_t>(mid[frames[frames.size() - 2].second - 6] ^ 0x55);
            const RestartLoaded m = parse_restart_record(mid.data(), mid.size());
            ASSERT_TRUE(!m.ok() && m.status == RestartLoaded::Status::Corrupt);
        }
        {   // garbage: nothing is a record, nothing crashes (a third of them start with the magic, as a damaged record would)
            uint32_t rng = 12345;
            const auto next = [&rng]() {
                rng = rng * 1664525u + 1013904223u;
                return rng >> 8;
            };
            for (int i = 0; i < 3000; ++i) {
                std::vector<uint8_t> junk;
                if (i % 3 == 0) junk.assign(kRestartMagic, kRestartMagic + sizeof(kRestartMagic));
                const size_t len = next() % 400;
                for (size_t k = 0; k < len; ++k) junk.push_back(static_cast<uint8_t>(next()));
                const RestartLoaded r = parse_restart_record(junk.data(), junk.size());
                ASSERT_FALSE(r.ok());
                ASSERT_FALSE(r.why.empty());
            }
            ASSERT_FALSE(parse_restart_record(nullptr, 0).ok());
            ASSERT_FALSE(parse_restart_record(nullptr, 100).ok());
        }
        {   // hostile fields
            const std::vector<uint8_t> huge_type2 = with_magic({2, 0xFF, 0xFF, 0xFF, 0xFF});
            const RestartLoaded a = parse_restart_record(huge_type2.data(), huge_type2.size());
            ASSERT_TRUE(!a.ok() && a.status == RestartLoaded::Status::Corrupt);         // a length of 4 GB: refused before anything is allocated
            const std::vector<uint8_t> cut_head = with_magic({1, 0x00, 0x00, 0x10, 0x00});   // a head of 1 MiB that the file does not hold
            const RestartLoaded b = parse_restart_record(cut_head.data(), cut_head.size());
            ASSERT_TRUE(!b.ok() && b.status == RestartLoaded::Status::Corrupt);
            const std::vector<uint8_t> zeros = with_magic(std::vector<uint8_t>(5000, 0));
            const RestartLoaded c = parse_restart_record(zeros.data(), zeros.size());
            ASSERT_TRUE(!c.ok());                                                       // (nothing but a zero fill after the magic: there is no head)
            std::vector<uint8_t> other_format(kRestartMagic, kRestartMagic + sizeof(kRestartMagic));
            other_format.back() = '2';
            const RestartLoaded d = parse_restart_record(other_format.data(), other_format.size());
            ASSERT_TRUE(!d.ok() && d.status == RestartLoaded::Status::UnknownFormat && d.why.find("format") != std::string::npos);
            std::vector<uint8_t> not_ours = good;
            not_ours[0] = 'X';
            const RestartLoaded e = parse_restart_record(not_ours.data(), not_ours.size());
            ASSERT_TRUE(!e.ok() && e.status == RestartLoaded::Status::NotARecord);
            // a zero fill after a good record is a torn tail (the file grew and its data did not reach the disk), not corruption
            std::vector<uint8_t> padded = good;
            padded.insert(padded.end(), 3000, 0);
            const RestartLoaded f = parse_restart_record(padded.data(), padded.size());
            ASSERT_TRUE(f.ok() && f.torn && f.good_bytes == good.size() && f.turns.size() == 61 && prefix_of_reference(f));
        }
        {   // frames in the wrong order, repeated, misplaced: all refused, whatever their checksums say
            const std::vector<uint8_t> head_f = test_frame(1, std::vector<uint8_t>(good.begin() + static_cast<std::ptrdiff_t>(frames[0].first + 5), good.begin() + static_cast<std::ptrdiff_t>(frames[0].second - 4)));
            const std::vector<uint8_t> t0 = test_frame(2, turns_payload(0, {sample_turn(0)}));
            const std::vector<uint8_t> t1 = test_frame(2, turns_payload(1, {sample_turn(1)}));
            const std::vector<uint8_t> t5 = test_frame(2, turns_payload(5, {sample_turn(5)}));
            ASSERT_TRUE(head_f == std::vector<uint8_t>(good.begin() + static_cast<std::ptrdiff_t>(frames[0].first), good.begin() + static_cast<std::ptrdiff_t>(frames[0].second)));    // (the test's own frame is the writer's)
            const auto refused = [](const std::vector<uint8_t>& frames_bytes) {
                const std::vector<uint8_t> file = with_magic(frames_bytes);
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                return !r.ok() && r.status == RestartLoaded::Status::Corrupt && !r.why.empty();
            };
            const auto accepted_turns = [](const std::vector<uint8_t>& frames_bytes) {
                const std::vector<uint8_t> file = with_magic(frames_bytes);
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                return r.ok() ? r.turns.size() : size_t{9999};
            };
            ASSERT_EQ(accepted_turns(concat({head_f, t0, t1})), size_t{2});             // (the same frames in the right order are fine)
            ASSERT_TRUE(refused(concat({head_f, t1})));                                 // the first turn is not 0
            ASSERT_TRUE(refused(concat({head_f, t0, t0})));                             // a repeat
            ASSERT_TRUE(refused(concat({head_f, t0, t1, t0})));
            ASSERT_TRUE(refused(concat({head_f, t1, t0})));                             // swapped
            ASSERT_TRUE(refused(concat({head_f, t0, t5})));                             // a hole
            ASSERT_TRUE(refused(concat({t0, head_f})));                                 // turns before the head
            ASSERT_TRUE(refused(concat({t0, t1})));                                     // no head at all
            ASSERT_TRUE(refused(concat({head_f, head_f})));                             // a second head
            ASSERT_TRUE(refused(concat({head_f, t0, head_f})));
            ASSERT_TRUE(refused(concat({head_f, test_frame(3, check_payload(19, 1))})));          // a checkpoint of a turn that the file does not hold
            ASSERT_TRUE(refused(concat({head_f, t0, test_frame(3, check_payload(0, 1))})));       // of a turn that is not a multiple of 20 minus 1
            std::vector<net::TurnMsg> twenty;
            for (uint32_t n = 0; n < 20; ++n) twenty.push_back(sample_turn(n));
            const std::vector<uint8_t> batch = test_frame(2, turns_payload(0, twenty));            // (twenty turns in one frame are fine: the reader takes any number up to 4096)
            ASSERT_EQ(accepted_turns(concat({head_f, batch, test_frame(3, check_payload(19, 77))})), size_t{20});
            ASSERT_TRUE(refused(concat({head_f, batch, test_frame(3, check_payload(19, 77)), test_frame(3, check_payload(19, 78))})));        // a checkpoint twice
            ASSERT_TRUE(refused(concat({head_f, batch, test_frame(3, check_payload(39, 77))})));  // a turn that is not there yet
            ASSERT_TRUE(refused(concat({head_f, test_frame(4, {1, 2, 3})})));                      // a frame type that nobody writes
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, {})})));                             // an empty turns frame
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, turns_payload(0, {}))})));           // count 0
            std::vector<uint8_t> trailing = turns_payload(0, {sample_turn(0)});
            trailing.push_back(0);
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, trailing)})));                       // bytes after the last turn of a frame
            std::vector<uint8_t> short_commands = turns_payload(0, {sample_turn(1)});
            short_commands.pop_back();
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, short_commands)})));                 // a command that is cut short
            net::TurnMsg bad_issuer = sample_turn(1);
            bad_issuer.commands[0].issuer = 9;
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, turns_payload(0, {bad_issuer}))})));  // a command of a seat that does not exist: the sequencer stamps seats 0 - 3 only
            std::vector<uint8_t> too_many = turns_payload(0, {net::TurnMsg{}});
            too_many[6] = 0xFF;                                                                     // 0x01FF commands: more than a turn may hold (512 is the most) ...
            too_many[7] = 0x03;                                                                     // ... 0x03FF = 1023
            ASSERT_TRUE(refused(concat({head_f, test_frame(2, too_many)})));
            ASSERT_TRUE(refused(concat({head_f, test_frame(3, std::vector<uint8_t>(11, 0))})));    // a checkpoint of the wrong size
        }
        {   // heads that are wrong in one field (their frames are whole and their checksums right: only the head's own checks can refuse them)
            struct Case {
                const char* what;
                std::function<void(RestartHead&)> mutate;
            };
            const std::vector<Case> cases = {
                {"one player", [](RestartHead& h) { h.players = 1; }},
                {"five players", [](RestartHead& h) { h.players = 5; }},
                {"a code with a space", [](RestartHead& h) { h.code = "bad code"; }},
                {"an empty code", [](RestartHead& h) { h.code.clear(); }},
                {"a map that is a path", [](RestartHead& h) { h.map = "../x.lvl"; h.start.map_name = h.map; }},
                {"a start message of another map", [](RestartHead& h) { h.start.map_name = "SMALL.LVL"; }},
                {"a start message of another map file", [](RestartHead& h) { h.start.map_hash = 5; }},
                {"a start message of another fog", [](RestartHead& h) { h.start.fog = true; }},
                {"a roster of more seats than players", [](RestartHead& h) { h.start.roster = 0x0F; h.start.names[3] = "Dee"; }},
                {"a bot outside the roster", [](RestartHead& h) { h.bots.push_back(ai::BotSpec{3, "standard", ai::Level::Easy}); }},
                {"two bots on a seat", [](RestartHead& h) { h.bots.push_back(ai::BotSpec{2, "standard", ai::Level::Easy}); }},
                {"a bot that does not exist", [](RestartHead& h) { h.bots[0].kind = "boss"; }},
                {"a level that does not exist", [](RestartHead& h) { h.bots[0].level = static_cast<ai::Level>(7); }},
                {"a key for a bot", [](RestartHead& h) { h.keys[2] = test_key(9); }},
                {"a key for a seat outside the roster", [](RestartHead& h) { h.keys[3] = test_key(9); }},
                {"one key for two seats", [](RestartHead& h) { h.keys[1] = h.keys[0]; }},
                {"no game version", [](RestartHead& h) { h.identity.game_version.clear(); }},
                {"a game version of 40 characters", [](RestartHead& h) { h.identity.game_version = std::string(40, 'v'); }},
                {"a build id with a control character", [](RestartHead& h) { h.identity.build_id = "a\nb"; }},
            };
            for (const Case& c : cases) {
                RestartHead bad = head;
                c.mutate(bad);
                const std::vector<uint8_t> file = with_magic(head_frame_of(bad));
                const RestartLoaded r = parse_restart_record(file.data(), file.size());
                ASSERT_MSG(!r.ok() && r.status == RestartLoaded::Status::Corrupt && !r.why.empty(), c.what);
            }
            const std::vector<uint8_t> fine = with_magic(head_frame_of(head));
            ASSERT_TRUE(parse_restart_record(fine.data(), fine.size()).ok());           // (the unmutated head passes the same road)
        }
        {   // files: one that is too big is refused without being read, a folder, a link and a file that is not there are no records
            const fs::path big = fs::path(cfg.dir) / "room-BIG-00000000.restart";
            {
                std::ofstream out(big, std::ios::binary);
                out.put('x');
            }
            fs::resize_file(big, 100ull * 1024 * 1024);                                 // (a sparse file where the file system has them: nothing is read)
            const RestartLoaded r = read_restart_record(big.string(), 48ull * 1024 * 1024);
            ASSERT_TRUE(!r.ok() && r.status == RestartLoaded::Status::TooBig);
            fs::remove(big);
            const fs::path dir = fs::path(cfg.dir) / "room-DIR-00000000.restart";
            fs::create_directories(dir);
            ASSERT_TRUE(read_restart_record(dir.string(), 1 << 20).status == RestartLoaded::Status::Unreadable);
            ASSERT_TRUE(read_restart_record((fs::path(cfg.dir) / "nothing.restart").string(), 1 << 20).status == RestartLoaded::Status::Unreadable);
            ASSERT_EQ(store.records().size(), size_t{1});                               // (the folder named like a record is not listed as one)
            for (const std::string& rec : store.records()) ASSERT_TRUE(fs::is_regular_file(rec));
#ifndef _WIN32
            const fs::path link = fs::path(cfg.dir) / "room-LINK-00000000.restart";
            fs::create_symlink(path, link);
            ASSERT_TRUE(read_restart_record(link.string(), 1 << 20).status == RestartLoaded::Status::Unreadable);       // a link is not followed: a record is a file of the folder
            fs::remove(link);
#endif
            fs::remove_all(dir);
        }
    } TEST_END();
}
