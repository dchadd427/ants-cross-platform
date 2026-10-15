// Tests of the lock-step network core: failures of peers, protocol 11 and 12, team chat at the wire, protocol 17 and the restart records
#include "lockstep_fixtures.hpp"

void run_failure_tests() {
    TEST_CASE("S2.1 Dedicated Server: A Host Without A Seat Referees Four Clients Over 25 - 120 ms Links; Every Machine, The Referee Included, Stays Bit-Identical For 60 Seconds") {
        for (uint32_t seed : {1u, 2u}) {
            ServerMatch m(seed, 4, {60, 60});
            ASSERT_TRUE(m.host->seatless());
            m.run(60000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.host->turns_sealed() > 1150);
            ASSERT_TRUE(m.referee.current_tick() > 1100);
            for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
            ASSERT_TRUE(m.all_equal());                                              // the referee's own engine equals every client's
            sim::SimulationEngine fresh;
            build_world(fresh, seed);
            for (uint8_t p = 0; p < 4; ++p) {                                        // every client's commands took effect on the referee's engine
                int moved = 0;
                for (uint32_t id : m.ids.ants[p]) {
                    if (m.referee.get_unit(id).pos != fresh.get_unit(id).pos) ++moved;
                }
                ASSERT_TRUE(moved >= 2);
            }
        }
    } TEST_END();

    TEST_CASE("S2.2 Dedicated Server: The Referee Names A Diverging Client Within A Second, With The Subsystem; The Server Plays And Chats As Nobody") {
        ServerMatch m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        m.sims[3]->get_unit(m.ids.ants[3][1]).hp = 3;                                // a hit point that no command explains
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_FALSE(m.host->desyncs().empty());
        ASSERT_TRUE(m.now - before <= 2000);
        const DesyncMsg& d = m.host->desyncs()[0];
        ASSERT_EQ(d.player, 3);
        ASSERT_TRUE(d.peer.ants != d.host.ants);
        ASSERT_TRUE(d.peer.grid == d.host.grid && d.peer.players == d.host.players);
        for (auto& c : m.clients) ASSERT_TRUE(c->desynced());                        // everybody is told
        ASSERT_TRUE(m.host->frozen());
        for (const auto& x : m.host->desyncs()) ASSERT_EQ(x.player, 3);              // only client 3 is blamed (the referee agrees with the others)
        // a host without a seat has no commands and no chat of its own
        ServerMatch quiet(2, 2, {20, 5});
        quiet.run(500, false);
        Command c;
        c.type = CommandType::Stop;
        c.ants = {quiet.ids.ants[0][0]};
        quiet.host->submit_local(c);
        quiet.host->chat_local("nobody speaks", false);
        std::vector<uint8_t> msg;
        bool chat_seen = false;
        quiet.run(500, false);
        for (auto* end : quiet.client_ends) {
            while (end->poll(msg)) chat_seen = chat_seen || peek_type(msg) == MsgType::Chat;
        }
        ASSERT_FALSE(chat_seen);
    } TEST_END();

    TEST_CASE("S2.3 Dedicated Server: A Client Cannot Make The Report Maps Grow With Hashes For Turns That Were Never Sealed; A Report Of A Real Turn Is Compared Whichever Comes First") {
        // the sequencer alone: a host with and a host without a seat
        for (uint8_t host_seat : {uint8_t{0}, kNoSeat}) {
            Sequencer seq;
            seq.set_host_player(host_seat);
            seq.set_active(1, true);
            seq.set_active(2, true);
            if (host_seat < sim::MAX_PLAYERS) seq.set_active(host_seat, true);
            sim::StateHash bogus;
            for (uint32_t t = 100000; t < 100500; ++t) {
                ASSERT_TRUE(seq.on_hash(1, t, bogus).empty());
                ASSERT_TRUE(seq.on_referee_hash(t, bogus).empty());
            }
            ASSERT_EQ(seq.pending_reports(), size_t{0});                                  // nothing was sealed: nothing is kept
            for (int i = 0; i < 20; ++i) seq.seal();                                      // turns 0 .. 19 exist now
            sim::StateHash a;
            sim::StateHash b;
            b.total = 1;
            b.ants = 1;
            if (host_seat == kNoSeat) {
                ASSERT_TRUE(seq.on_hash(1, 10, a).empty());                               // the client is first: waits for the referee
                ASSERT_TRUE(seq.on_hash(2, 10, b).empty());
                const auto d = seq.on_referee_hash(10, a);                                // the referee agrees with client 1 and not with client 2
                ASSERT_EQ(d.size(), size_t{1});
                ASSERT_EQ(d[0].player, 2);
                ASSERT_TRUE(d[0].host == a && d[0].peer == b);
                const auto late = seq.on_hash(2, 10, b);                                  // a repeated report of the same turn is compared again
                ASSERT_EQ(late.size(), size_t{1});
                ASSERT_TRUE(seq.on_hash(1, 11, a).empty() && seq.on_referee_hash(11, a).empty());      // agreement: nothing
                ASSERT_EQ(seq.on_hash(2, 12, b).size() + seq.on_referee_hash(12, a).size(), size_t{1});   // the referee is first: the client is found as it reports
            }
        }
        // on the wire: a hostile client sends hashes for the far future; the match goes on unharmed
        ServerMatch m(1, 2, {10, 0});
        m.run(2000);
        HashMsg h;
        for (uint32_t t = 100000; t < 100200; ++t) {
            h.turn = t;
            m.client_ends[0]->send(encode(h));
        }
        m.run(1000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.4 Dedicated Server: A Client Whose Server Goes Away Does Not Elect Itself The Host (No Migration): The Match Is Lost Here After The Silence") {
        ServerMatch m(1, 3, {20, 5});
        m.run(3000);
        ASSERT_EQ(static_cast<int>(m.clients[0]->mode()), static_cast<int>(ClientSession::Mode::Normal));
        m.host_ends[0]->close();                                                      // client 0's link to the server dies
        m.run(1500, false);
        ASSERT_TRUE(m.clients[0]->lost());                                            // it does not become a host of a one-player game
        ASSERT_FALSE(m.clients[0]->promoted());
        ASSERT_FALSE(m.clients[0]->electing());
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Normal));    // the others are not affected
        m.net.set_time(m.now);
        // the server drops the vanished client at the same tick for everybody
        m.run(1000, false);
        ASSERT_TRUE(m.referee.is_player_dropped(0));
        for (uint8_t p = 1; p < 3; ++p) ASSERT_TRUE(m.sims[p]->is_player_dropped(0));
    } TEST_END();

    TEST_CASE("S2.5 Dedicated Server: A StartRequest That Reaches A Running Match (The Leader's Second Click Crossed The Start) Is Ignored And Costs Nothing (Up To The 16 That A Person Could Send, S2.6 Has The Flood); With A Payload Beyond The Fill Level It Is Garbage; A Host That Holds A Seat Has No Leader And Counts It As A Violation") {
        {
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            for (uint32_t i = 0; i < kIgnoredStartRequestsAllowed; ++i) m.client_ends[0]->send(encode(StartRequestMsg{}));          // sixteen late clicks: more than the eight violations that throw a client out
            m.run(500);
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            ASSERT_EQ(m.host->ignored_start_requests(), kIgnoredStartRequestsAllowed);
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::StartRequest), 0, 1});            // with a payload beyond the fill level: garbage like any other
            m.run(300);
            ASSERT_EQ(m.host->violations(1), 1u);
            ASSERT_TRUE(m.host->client_present(1));
            m.settle();
            ASSERT_TRUE(m.all_equal());                                                              // nothing changed in the match
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        {   // a host that plays a seat: nobody in its room leads, so the message is a violation like any other that a guest may not send
            Match m(1, 3, {20, 0});
            m.run(1000);
            m.client_ends[2]->send(encode(StartRequestMsg{}));
            m.run(300);
            ASSERT_EQ(m.host->violations(2), 1u);
            for (int i = 0; i < 10; ++i) m.client_ends[2]->send(encode(StartRequestMsg{}));
            m.run(300);
            ASSERT_FALSE(m.host->client_present(2));                                                 // thrown out after eight
            ASSERT_TRUE(m.host->client_present(1));
        }
    } TEST_END();

    TEST_CASE("S2.5b Dedicated Server: A SeatMove That Reaches A Running Match (The Leader's Press Crossed The Start, Protocol 14) Is Ignored And Costs Nothing (Up To The 16 That A Person Could Send, S2.6 Has The Flood); A SeatMove That Is Garbage Is A Violation; A Host That Holds A Seat Has No Leader And Counts It As One; The Match Does Not Change") {
        {
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            for (uint32_t i = 0; i < kIgnoredSeatMovesAllowed; ++i) m.client_ends[0]->send(encode(SeatMoveMsg{1, 3, 7}));           // sixteen late presses: more than the eight violations that throw a client out
            m.run(500);
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            ASSERT_EQ(m.host->ignored_seat_moves(), kIgnoredSeatMovesAllowed);
            ASSERT_EQ(m.host->ignored_start_requests(), 0u);                                         // (each message has its own count)
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::SeatMove), 2, 2, 7, 0, 0, 0});    // the same seat twice: garbage like any other
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::SeatMove), 0, 4, 7, 0, 0, 0});    // a seat that no room has
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::SeatMove), 0, 1, 0, 0, 0, 0});    // a guard of 0 (protocol 15: a number that was computed)
            m.client_ends[1]->send({static_cast<uint8_t>(MsgType::SeatMove), 0, 1});                // the three bytes of protocol 14
            m.run(300);
            ASSERT_EQ(m.host->violations(1), 4u);
            ASSERT_TRUE(m.host->client_present(1));
            ASSERT_EQ(m.host->ignored_seat_moves(), kIgnoredSeatMovesAllowed);                       // (garbage is no request: it is not counted as one)
            m.settle();
            ASSERT_TRUE(m.all_equal());                                                              // nothing changed in the match
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        {   // a host that plays a seat: nobody in its room leads, so the message is a violation like any other that a guest may not send
            Match m(1, 3, {20, 0});
            m.run(1000);
            m.client_ends[2]->send(encode(SeatMoveMsg{1, 2, 7}));
            m.run(300);
            ASSERT_EQ(m.host->violations(2), 1u);
            for (int i = 0; i < 10; ++i) m.client_ends[2]->send(encode(SeatMoveMsg{1, 2, 7}));
            m.run(300);
            ASSERT_FALSE(m.host->client_present(2));                                                 // thrown out after eight
            ASSERT_TRUE(m.host->client_present(1));
            ASSERT_EQ(m.host->ignored_seat_moves(), 0u);
        }
    } TEST_END();

    TEST_CASE("S2.6 Dedicated Server: Flood Control In The Running Match: A Client That Floods Valid Messages (Acknowledgements, Hashes Of Turns That Were Never Sealed, Pings, Chat Lines, Late StartRequests) Is Thrown Out After About A Second's Worth And Cannot Hurt The Others; At Most 256 Messages Of A Client Are Taken Per Update; Honest Rates Never Meet A Limit") {
        const auto message_of = [](const std::string& kind, uint32_t i) -> std::vector<uint8_t> {
            if (kind == "ack") return encode(AckMsg{0});
            if (kind == "hash") {
                HashMsg h;
                h.turn = 0xFFFFFFF0u;                                                  // a turn that was never sealed: valid, and nothing happens
                return encode(h);
            }
            if (kind == "ping") return encode_ping(PingMsg{i + 1, 0});
            if (kind == "chat") {
                ChatMsg c;
                c.text = "spam";
                return encode(c);
            }
            if (kind == "seatmove") return encode(SeatMoveMsg{1, 2, 7});                // (protocol 14: a leader's press that crossed the Start, repeated)
            return encode(StartRequestMsg{});                                           // "startreq"
        };
        for (const std::string kind : {"ack", "hash", "ping", "chat", "startreq", "seatmove"}) {
            ServerMatch m(1, 3, {20, 5});
            uint32_t chats = 0;
            m.clients[1]->set_on_chat([&](const ChatMsg&) { ++chats; });
            m.run(1000);
            for (uint32_t i = 0; i < 3000; ++i) m.client_ends[0]->send(message_of(kind, i));
            m.run(1000, false);
            ASSERT_FALSE(m.host->client_present(0));                                    // thrown out
            ASSERT_EQ(m.host->violations(0), 8u);
            ASSERT_FALSE(m.client_ends[0]->is_open());
            if (kind == "startreq") ASSERT_EQ(m.host->ignored_start_requests(), kIgnoredStartRequestsAllowed + 8u);      // the 24th is the eighth violation: the rest is never looked at
            if (kind == "seatmove") ASSERT_EQ(m.host->ignored_seat_moves(), kIgnoredSeatMovesAllowed + 8u);              // the same ladder: the 24th is the eighth violation
            if (kind == "chat") ASSERT_EQ(chats, kChatBurst);                                                              // the chat budget's burst was relayed (S2.10), not a second's worth of messages, not 3000 lines
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));         // nobody else notices
            m.settle();
            ASSERT_TRUE(m.sims[1]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.host->turns_sealed() > 20);
        }
        {   // at most 256 messages of a client are taken per update; the rest waits for the next (the budget is made too big to matter here)
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            build_world(referee, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.message_burst = 1000000;
            hc.messages_per_second = 1000000;
            HostSession host(referee, hc);
            auto ends = net.connect({0, 0});
            CountingConnection counted(ends.first);
            host.add_client(0, &counted);
            host.start(0);
            for (uint32_t i = 1; i <= 1000; ++i) ends.second->send(encode_ping(PingMsg{i, 0}));
            net.set_time(10);
            counted.taken = 0;
            host.update(10);
            ASSERT_EQ(counted.taken, 256u);
            host.update(11);
            ASSERT_EQ(counted.taken, 512u);
            host.update(12);
            ASSERT_EQ(counted.taken, 768u);
            host.update(13);
            ASSERT_EQ(counted.taken, 1000u);                                            // (what was left)
            ASSERT_TRUE(host.client_present(0));
        }
        {   // honest rates: 800 a second for twenty seconds, and a burst of 900 after five quiet seconds: the client stays; 1500 a second: it is out within a few seconds
            ServerMatch m(1, 3, {20, 5});
            m.run(1000);
            uint32_t n = 0;
            for (int step = 0; step < 2000; ++step) {
                for (int i = 0; i < 8; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
                m.run(10);
            }
            ASSERT_TRUE(m.host->client_present(0));
            m.run(5000);
            for (int i = 0; i < 900; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
            m.run(1000);
            ASSERT_TRUE(m.host->client_present(0) && m.host->violations(0) == 0u);
            const uint32_t before = m.now;
            int steps = 0;
            for (; steps < 1000 && m.host->client_present(0); ++steps) {
                for (int i = 0; i < 15; ++i) m.client_ends[0]->send(encode_ping(PingMsg{++n, 0}));
                m.run(10);
            }
            ASSERT_FALSE(m.host->client_present(0));
            ASSERT_TRUE(m.now - before >= 1500 && m.now - before <= 3500);
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));
        }
    } TEST_END();

    TEST_CASE("S2.7 Dedicated Server: A Client That Stops Executing For 5 s Does Not Slow The Others Down; They Are Told Who Lags; It Catches Up On Its Own At Up To 4x After It Is Back; Every State Hash Agrees") {
        ServerMatch m(1, 3, {30, 5});
        m.run(3000);
        ASSERT_EQ(m.host->lagging_mask(), 0);
        const uint64_t others_before = m.sims[0]->current_tick();
        const uint32_t sealed_before = m.host->turns_sealed();
        const uint32_t frozen_at = m.now;
        m.frozen_mask = 1u << 2;                                                      // seat 2's window stops: its process hangs, the link stays open
        bool noticed = false;
        uint32_t noticed_after_ms = 0;
        m.run(5000, true, [&](uint32_t now) {
            if (!noticed && m.clients[0]->lagging_seat() == 2) {
                noticed = true;
                noticed_after_ms = now - frozen_at;
            }
        });
        // the server never stopped: 5 s are 100 turns, and the others ran them (a tick of buffer behind the newest turn and the frame's rounding are all that is missing)
        ASSERT_TRUE(m.host->turns_sealed() - sealed_before >= 99);
        ASSERT_TRUE(m.sims[0]->current_tick() - others_before >= 95);
        ASSERT_TRUE(m.sims[1]->current_tick() - others_before >= 95);
        // ... and they were told, when it was 3 s behind (and not before), once a second, naming the seat and how far behind it is
        if (!noticed || noticed_after_ms < 2500 || noticed_after_ms > 4500) std::cout << "\n    noticed after " << noticed_after_ms << " ms\n";
        ASSERT_TRUE(noticed && noticed_after_ms >= 2500 && noticed_after_ms <= 4500);       // (3 s behind: the acks that had not come back yet when it froze count 0.2 s of that)
        ASSERT_EQ(m.clients[0]->lagging_seat(), 2);
        ASSERT_EQ(m.clients[1]->lagging_seat(), 2);
        ASSERT_TRUE(m.clients[0]->lagging_behind_ms() >= 3000 && m.clients[0]->lagging_behind_ms() <= 5500);
        ASSERT_EQ(m.host->lagging_mask(), 1u << 2);
        ASSERT_TRUE(m.host->client_present(2));                                       // 5 s: not dropped
        ASSERT_EQ(m.clients[2]->lagging_seat(), 255);                                 // (it is never told about itself)
        m.run(4000);                                                                  // it stays frozen for 4 s more (9 s in all: its first frame back runs a second at 4x, 80 ticks, and 100 turns are left)
        ASSERT_TRUE(m.host->client_present(2));
        // it is back: it finds 9 s of turns waiting, says so, runs them at up to 4 times normal speed (never faster) and is level again within a few seconds
        m.frozen_mask = 0;
        const uint32_t back_at = m.now;
        bool catching = false;
        uint32_t level_after_ms = 0;
        uint64_t last_tick = m.sims[2]->current_tick();
        uint32_t window_start = m.now;
        uint64_t window_ticks = 0;
        uint64_t fastest = 0;
        bool cleared = false;
        uint32_t cleared_after_ms = 0;
        m.run(8000, true, [&](uint32_t now) {
            catching = catching || m.clients[2]->catching_up();
            const uint64_t t = m.sims[2]->current_tick();
            window_ticks += t - last_tick;
            last_tick = t;
            if (now - window_start >= 50) {                                           // ticks per 50 ms of real time: 4x normal speed is 4
                if (window_start >= back_at + 100) fastest = std::max(fastest, window_ticks);     // (the first frame stood for a second and runs the second's ticks: a frame's time is owed)
                window_ticks = 0;
                window_start = now;
            }
            if (level_after_ms == 0 && m.clients[2]->runner().next_turn_to_execute() + 8 >= m.clients[0]->runner().next_turn_to_execute()) level_after_ms = now - back_at;
            if (!cleared && m.clients[0]->lagging_seat() == 255) {
                cleared = true;
                cleared_after_ms = now - back_at;
            }
        });
        ASSERT_TRUE(catching);                                                        // "Catching up..." showed on its screen
        ASSERT_FALSE(m.clients[2]->catching_up());                                    // ... and went
        ASSERT_TRUE(level_after_ms > 0 && level_after_ms <= 4000);                    // 100 turns behind, 3 gained per 50 ms: under 2 s of work, and the first frame
        ASSERT_TRUE(fastest <= 5);                                                    // four ticks in 50 ms (one more for the rounding of a frame that is not a divisor of 50)
        ASSERT_TRUE(fastest >= 3);                                                    // and it did run fast
        ASSERT_TRUE(cleared && cleared_after_ms <= level_after_ms + 1500);            // the notice of the others ended when it was within a second of the match
        ASSERT_EQ(m.host->lagging_mask(), 0);
        ASSERT_TRUE(m.host->client_present(2));
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
        ASSERT_TRUE(m.all_equal());                                                   // all three and the referee: the hash of every machine is the same
    } TEST_END();

    TEST_CASE("S2.8 Dedicated Server: The Lag Policy's Numbers: A Notice From 3 s Behind Once A Second, Cleared Within 1 s, A Drop At 60 s Behind Or After 30 s Without An Ack (Whichever Comes First), Never A Wait") {
        // The test plays the clients on raw links: client 0 acknowledges every turn at once; client 1 as the test decides.
        struct Rig {
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            std::unique_ptr<HostSession> host;
            Connection* ends[2]{};
            Connection* host_ends[2]{};
            uint32_t now{0};
            uint32_t last_turn[2]{0, 0};
            bool got_turn[2]{false, false};
            std::vector<std::pair<uint32_t, LagMsg>> notices;       // what client 0 was told (the time and the message)
            std::vector<LagMsg> told_client_1;
            Rig() {
                build_world(referee, 1);
                HostSession::Config hc;
                hc.host_player = kNoSeat;
                host = std::make_unique<HostSession>(referee, hc);
                for (int i = 0; i < 2; ++i) {
                    auto e = net.connect({5, 0});
                    host_ends[i] = e.first;
                    ends[i] = e.second;
                    host->add_client(static_cast<uint8_t>(i), e.first);
                }
                host->start(0);
            }
            // one step of 10 ms; `ack1` is the turn that client 1 acknowledges now (or -1 for none)
            void step(int64_t ack1) {
                now += 10;
                net.set_time(now);
                host->update(now);
                for (int i = 0; i < 2; ++i) {
                    std::vector<uint8_t> msg;
                    while (ends[i]->poll(msg)) {
                        if (peek_type(msg) == MsgType::Turn) {
                            TurnMsg t;
                            if (decode(msg, t)) {
                                last_turn[i] = t.turn;
                                got_turn[i] = true;
                            }
                        } else if (peek_type(msg) == MsgType::Lag) {
                            LagMsg l;
                            if (decode(msg, l)) {
                                if (i == 0) notices.emplace_back(now, l);
                                else told_client_1.push_back(l);
                            }
                        }
                    }
                }
                if (got_turn[0]) {
                    AckMsg a;
                    a.turn = last_turn[0];
                    ends[0]->send(encode(a));
                }
                if (ack1 >= 0) {
                    AckMsg a;
                    a.turn = static_cast<uint32_t>(ack1);
                    ends[1]->send(encode(a));
                }
            }
        };
        {   // 1. a client that never acknowledges: told from 3 s, once a second, dropped at 30 s (the idle rule), the match never waits
            Rig r;
            uint32_t dropped_at = 0;
            uint32_t sealed_at_10s = 0;
            while (r.now < 40000 && dropped_at == 0) {
                r.step(-1);
                if (r.now == 10000) sealed_at_10s = r.host->turns_sealed();
                if (!r.host->client_present(1)) dropped_at = r.now;
            }
            ASSERT_TRUE(dropped_at >= 30000 && dropped_at <= 30100);                      // 30 s without an ack
            ASSERT_TRUE(sealed_at_10s >= 199);                                            // the server sealed all along: 200 turns in 10 s
            ASSERT_TRUE(r.host->turns_sealed() >= 590);
            for (int i = 0; i < 40; ++i) r.step(-1);                                      // (the last notice, that of the drop, is on its way)
            ASSERT_EQ(r.told_client_1.size() + 1, r.notices.size());                      // the lagger is told too (its own link may be the slow one: its backlog is on the way, not in its queue) ...
            for (const LagMsg& l : r.told_client_1) ASSERT_EQ(l.seat, 1);                 // ... about itself, once a second like the others, only the last one (the drop ends the notice for the others, and the lagger is gone) is not
            ASSERT_TRUE(r.notices.size() >= 27 && r.notices.size() <= 29);               // one a second from 3 s to 30 s ...
            if (r.notices.empty() || r.notices.front().first < 2900 || r.notices.front().first > 3100) std::cout << "\n    first notice at " << (r.notices.empty() ? 0u : r.notices.front().first) << ", " << r.notices.size() << " notices\n";
            ASSERT_TRUE(r.notices.front().first >= 2900 && r.notices.front().first <= 3100);   // 3 s behind: sixty turns sealed and none acknowledged
            ASSERT_EQ(r.notices.front().second.seat, 1);
            for (size_t i = 1; i < r.notices.size(); ++i) {
                const uint32_t gap = r.notices[i].first - r.notices[i - 1].first;
                if (r.notices[i].second.behind_ms == 0) continue;
                ASSERT_TRUE(gap >= 990 && gap <= 1020);
                ASSERT_TRUE(r.notices[i].second.behind_ms > r.notices[i - 1].second.behind_ms || r.notices[i - 1].second.behind_ms == 0);   // the number grows while it lasts
            }
            ASSERT_EQ(r.notices.back().second.behind_ms, 0u);                             // ... and the drop ends the notice
            ASSERT_EQ(r.host->lagging_mask(), 0);
            ASSERT_TRUE(r.referee.is_player_dropped(1));                                  // the drop travelled in the turn stream
            ASSERT_FALSE(r.referee.is_player_dropped(0));
        }
        {   // 2. a client that makes progress but is 61 s behind: dropped at 60 s of it (the idle rule is never reached: it acknowledges a turn every 29 s)
            Rig r;
            uint32_t dropped_at = 0;
            uint32_t next_ack = 29000;
            uint32_t acked = 0;
            while (r.now < 80000 && dropped_at == 0) {
                int64_t ack = -1;
                if (r.now + 10 >= next_ack) {
                    ack = ++acked;                                                         // one more turn executed: progress, 29 s after the last
                    next_ack += 29000;
                }
                r.step(ack);
                if (!r.host->client_present(1)) dropped_at = r.now;
            }
            ASSERT_TRUE(dropped_at >= 59900 && dropped_at <= 60200);                       // 60 s behind
        }
        {   // 3. hysteresis and clearing: 5 s behind (told), 2 s behind (neither new nor cleared), within 1 s (cleared once); nothing is repeated after that
            Rig r;
            int64_t ack = -1;
            uint32_t phase_end = 5500;
            std::vector<LagMsg> seen;
            while (r.now < 12000) {
                // phase 1 (until 5.5 s): no acks. phase 2: ack up to 2 s behind. phase 3: ack up to 0.3 s behind
                if (r.now >= phase_end && r.now < 9000) ack = r.host->turns_sealed() > 40 ? static_cast<int64_t>(r.host->turns_sealed()) - 40 : 0;       // 40 turns: 2 s
                else if (r.now >= 9000) ack = static_cast<int64_t>(r.last_turn[1]);
                r.step(ack);
                ack = -1;
            }
            ASSERT_TRUE(r.host->client_present(1));
            // told while it lasted from 3 s; not cleared by the 2 s phase; cleared once in the last phase
            size_t zero_notices = 0;
            for (const auto& n : r.notices) zero_notices += n.second.behind_ms == 0 ? 1 : 0;
            ASSERT_EQ(zero_notices, size_t{1});
            ASSERT_TRUE(r.notices.back().second.behind_ms == 0);
            ASSERT_TRUE(r.notices.back().first >= 9000 && r.notices.back().first <= 9200);
            ASSERT_EQ(r.host->lagging_mask(), 0);
        }
        {   // 4. a client that is quiet because there is nothing to run (the match is frozen) is not dropped for it
            Rig r;
            for (int i = 0; i < 300; ++i) r.step(-1);
            r.host->freeze();
            for (int i = 0; i < 4500; ++i) r.step(static_cast<int64_t>(r.last_turn[1]));  // 45 s: acks are all there is: behind 0
            ASSERT_TRUE(r.host->client_present(1));
        }
    } TEST_END();

    TEST_CASE("S2.9 Dedicated Server: A Player With A Round Trip Of 200 ms (And One With 200 - 400 ms) Is Never Called A Lagger, Never Catches Up, And Nobody Is Dropped") {
        for (const LoopbackNetwork::Link link : {LoopbackNetwork::Link{100, 0}, LoopbackNetwork::Link{100, 100}}) {
            ServerMatch m(5, 4, link);
            bool anybody_told = false;
            m.run(60000, true, [&](uint32_t) {
                if (m.host->lagging_mask() != 0) anybody_told = true;
                for (auto& c : m.clients) {
                    if (c->lagging_seat() != 255 || c->catching_up()) anybody_told = true;
                }
            });
            ASSERT_FALSE(anybody_told);
            for (uint8_t p = 0; p < 4; ++p) ASSERT_TRUE(m.host->client_present(p));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("S2.10 Dedicated Server: The End Of A Match Over A Spiky Link: Every Guest Runs Every Turn The Referee Sealed And Reaches Its Final Tick (The Host Freezes When The Match Ends; A Guest That Stalled Near The End Waited For A Buffer That Could Not Come And Never Saw The End)") {
        // 2 % of the messages are held up by 250 ms (a resend), the ones behind them wait; sixty matches of two guests, each ends at another moment
        uint32_t guests = 0;
        uint32_t missed = 0;
        for (uint32_t match = 1; match <= 60; ++match) {
            ServerMatch m(match * 7919u + 3, 2, {30, 0, 250, 20});
            m.run(24000 + (match % 17) * 50);
            m.settle(6000);                                                           // the match ends: the host stops sealing, everything in flight arrives
            for (uint8_t p = 0; p < 2; ++p) {
                ++guests;
                if (m.clients[p]->runner().next_turn_to_execute() != m.host->turns_sealed() || m.sims[p]->current_tick() != m.referee.current_tick()) ++missed;
            }
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        if (missed != 0) std::cout << "\n    " << missed << " of " << guests << " guests did not reach the final tick\n";
        ASSERT_EQ(missed, 0u);
    } TEST_END();

    TEST_CASE("S2.11 Dedicated Server: A Player Whose Own Link Is The Slow One Is Told That It Lags (Its Backlog Is On The Way, Not In Its Queue: \"Catching up...\" Had Nothing To Say And Nobody Told It Until It Was Dropped At 60 s); Everybody Else Is Told Too, And The Notice Ends When It Is Level") {
        ServerMatch m(1, 3, {30, 5});
        SlowReader slow(m.client_ends[2], &m.now);
        m.clients[2]->set_connection(&slow);
        m.run(3000);
        ASSERT_EQ(m.clients[2]->self_lag_behind_ms(), 0u);
        slow.period_ms = 150;                                                       // seat 2's downlink hands the game 7 messages a second against the 21 that the room sends
        bool told_self = false;
        bool catching = false;
        bool others_told = false;
        uint32_t told_at = 0;
        const uint32_t from = m.now;
        m.run(40000, true, [&](uint32_t now) {
            if (!told_self && m.clients[2]->self_lag_behind_ms() > 0) {
                told_self = true;
                told_at = now - from;
            }
            catching = catching || m.clients[2]->catching_up();
            others_told = others_told || (m.clients[0]->lagging_seat() == 2 && m.clients[1]->lagging_seat() == 2);
            ASSERT_TRUE(m.clients[2]->lagging_seat() == 255);                       // (the OTHER players: nobody else lags)
        });
        if (!told_self) std::cout << "\n    the laggard was never told\n";
        ASSERT_TRUE(told_self);                                                    // it is told, although nothing is queued in its game
        ASSERT_TRUE(told_at >= 3000 && told_at <= 30000);                          // (the notice comes behind the turns that wait in front of it)
        ASSERT_FALSE(catching);                                                    // "Catching up..." never had anything to say: the turns it misses are still in the link
        ASSERT_TRUE(others_told);
        ASSERT_TRUE(m.clients[2]->self_lag_behind_ms() >= 3000);
        ASSERT_TRUE(m.host->client_present(2));
        // the link is back: it reads what waited, catches up, and the notice ends for everybody
        slow.period_ms = 0;
        m.run(20000);
        ASSERT_EQ(m.clients[2]->self_lag_behind_ms(), 0u);
        ASSERT_EQ(m.clients[0]->lagging_seat(), 255);
        ASSERT_FALSE(m.clients[2]->catching_up());
        ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() + 10 >= m.clients[0]->runner().next_turn_to_execute());
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.12 Dedicated Server: A Player Who Was Dropped For Being Away Is Told Why When It Is Back (Its Link Was Closed While Half A Minute Of The Match Waited Unplayed); A Link That Fails By Itself, Or A Server That Goes Silent, Is Only A Lost Connection") {
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::AwayTooLong), std::string("You were away too long and were dropped from the match."));
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::Connection), std::string("The connection to the other players was lost."));
        ASSERT_EQ(match_lost_text(ClientSession::LostReason::None), std::string("The connection to the other players was lost."));
        {   // seat 2's process stops for 35 s: the server drops it at 30 s without an acknowledgement and closes the link; when the process runs again it reads the turns that waited, then the close
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(35000);
            ASSERT_FALSE(m.host->client_present(2));
            ASSERT_TRUE(m.clients[0]->mode() == ClientSession::Mode::Normal && !m.clients[0]->lost());
            ASSERT_FALSE(m.clients[2]->lost());                                      // (it has not run: it does not know yet)
            m.frozen_mask = 0;
            m.run(1000);
            ASSERT_TRUE(m.clients[2]->lost());
            ASSERT_TRUE(m.clients[2]->lost_reason() == ClientSession::LostReason::AwayTooLong);
            ASSERT_TRUE(m.clients[2]->runner().backlog_ms() >= kAwayBacklogMs);
            ASSERT_FALSE(m.clients[0]->lost());                                      // the others play on
        }
        {   // a link that fails while the player plays: nothing waits in its queue
            ServerMatch m(1, 3, {30, 5});
            m.run(5000);
            m.net.cut(m.client_ends[1]);
            m.run(500);
            ASSERT_TRUE(m.clients[1]->lost());
            ASSERT_TRUE(m.clients[1]->lost_reason() == ClientSession::LostReason::Connection);
            ASSERT_TRUE(m.clients[1]->runner().backlog_ms() < kAwayBacklogMs);
        }
        {   // a server that says nothing for 10 s (the link stays open): lost, and not "dropped"
            ServerMatch m(1, 3, {30, 5});
            m.run(5000);
            uint32_t t = m.now;
            while (m.now < t + 12000) {
                m.now += 10;
                m.net.set_time(m.now);
                m.clients[1]->update(m.now);                                         // the host is not updated: no turns, no pongs
            }
            ASSERT_TRUE(m.clients[1]->lost());
            ASSERT_TRUE(m.clients[1]->lost_reason() == ClientSession::LostReason::Connection);
        }
        {   // a player that is behind by more than a minute is dropped by the other rule (60 s): the same message
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(26000);
            ASSERT_TRUE(m.host->client_present(2));                                  // 26 s: still a lagger
            m.frozen_mask = 0;
            ASSERT_FALSE(m.clients[2]->lost());
        }
    } TEST_END();

    TEST_CASE("S2.13 Dedicated Server: A Stuck Uplink That Is Let Go After 5 s Delivers The 76 Orders Of A Player Who Gave 15 A Second: Nobody Is Dropped For It, Every Order Is Run In The Order It Was Given, And Every Machine Runs The Same Turns (The 64 A Turn Of The Sequencer Made 12 Of Them Violations: Eight Throw The Player Out)") {
        ServerMatch m(1, 3, {30, 5}, {}, {}, true);
        std::vector<int16_t> run_order;                                             // the orders of seat 1 as the referee executes them
        m.host->runner().set_on_command([&](const sim::Command& c, const sim::CommandResult&) {
            if (c.issuer == 1 && c.type == CommandType::GroupMove) run_order.push_back(c.tile_x);
        });
        m.run(10000, false);
        const uint32_t stuck_from = m.now;
        m.arrivals[1]->hold = true;                                                 // seat 1's way to the host is stuck: what it sends waits in the pipe
        int16_t next_order = 0;
        uint32_t next_at = m.now;
        m.run(5000, false, [&](uint32_t now) {
            if (now >= next_at) {
                next_at += 1000 / 15;                                               // 15 orders a second
                m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, next_order++, 0, {m.ids.ants[1][0]}));
            }
        });
        ASSERT_TRUE(next_order >= 74 && next_order <= 76);
        ASSERT_TRUE(m.now - stuck_from == 5000);
        m.arrivals[1]->hold = false;                                                // let go: everything arrives in one poll
        m.run(15000, false);
        m.settle();
        ASSERT_TRUE(m.host->client_present(1));                                    // not dropped
        ASSERT_EQ(m.host->violations(1), 0u);                                      // not a single one counted
        ASSERT_EQ(run_order.size(), static_cast<size_t>(next_order));              // every order was run ...
        for (size_t i = 0; i < run_order.size(); ++i) ASSERT_EQ(run_order[i], static_cast<int16_t>(i));       // ... in the order that it was given
        // the turns that carry them are the same on every machine: the referee's log and every client's log hold the same bytes
        const uint32_t sealed = m.host->turns_sealed();
        ASSERT_TRUE(sealed > 400);
        const uint32_t first_logged = sealed > LockstepRunner::kTurnLogTurns - 10 ? sealed - (static_cast<uint32_t>(LockstepRunner::kTurnLogTurns) - 10) : 0u;      // (the runners keep the last 600 turns)
        for (uint32_t t = first_logged; t < sealed; ++t) {
            const TurnMsg* host_turn = m.host->runner().logged_turn(t);
            ASSERT_TRUE(host_turn != nullptr);
            const std::vector<uint8_t> bytes = encode(*host_turn);
            for (auto& c : m.clients) {
                const TurnMsg* turn = c->runner().logged_turn(t);
                ASSERT_TRUE(turn != nullptr && encode(*turn) == bytes);
            }
        }
        // the 64 a turn of the sequencer shows in the turns: the turn that came first carried 64 of seat 1, the next the other twelve (or fewer, when the orders came in two bunches)
        size_t most = 0;
        for (uint32_t t = first_logged; t < sealed; ++t) {
            size_t n = 0;
            for (const sim::Command& c : m.host->runner().logged_turn(t)->commands) n += (c.issuer == 1 && c.type == CommandType::GroupMove) ? 1 : 0;
            most = std::max(most, n);
        }
        ASSERT_EQ(most, size_t{64});
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("S2.14 Dedicated Server: The Small Rules Of The Lag Policy That No Other Test Pinned: The Ack Of A Frame That Ran Several Turns Is For The Last, A Notice That Is Not Renewed Goes Stale (3 s; 10 s For The Notice About Oneself), Notices About Oneself Are Kept Apart, A Late Pass Seals At Most Ten Turns, The Server's Host Never Waits, And \"Catching Up...\" Begins At Sixty Turns") {
        {   // the ack of a frame that ran many turns is for the LAST of them (the host sees a machine that is level, not one that is as far behind as the frame began)
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            const uint32_t last_clock = m.now;
            m.frozen_mask = 1u << 1;
            m.run(3000);                                                            // seat 1 stands still: 60 turns are waiting for it
            ASSERT_TRUE(m.host->behind_ms(1) >= 3000);
            m.frozen_mask = 0;
            m.now += 10;
            m.net.set_time(m.now);
            m.clients[1]->update(last_clock + 1000);                               // ONE frame: it runs the turns that it has (a second, at four times the speed: 80 ticks at the most) and acks once
            ASSERT_TRUE(m.clients[1]->runner().next_turn_to_execute() > 55);
            for (int i = 0; i < 20; ++i) {                                          // the host takes the ack (nobody else runs a frame of seat 1)
                m.now += 10;
                m.net.set_time(m.now);
                m.host->update(m.now);
            }
            if (m.host->behind_ms(1) > 500) std::cout << "\n    behind " << m.host->behind_ms(1) << " ms after one frame that ran " << m.clients[1]->runner().next_turn_to_execute() << " turns\n";
            ASSERT_TRUE(m.host->behind_ms(1) <= 500);
            // (and what the server's host says of such a player: it is the server's, so it never waits, and names nobody as the one that holds the game up)
            ASSERT_FALSE(m.host->waiting());
            ASSERT_EQ(m.host->laggard(), 255);
        }
        {   // the server's host: a player 5 s behind is announced, and the host neither waits nor names a laggard (HostSession::waiting / laggard are for a host with a seat)
            ServerMatch m(1, 3, {30, 5});
            m.run(3000);
            m.frozen_mask = 1u << 2;
            m.run(5000);
            ASSERT_EQ(m.host->lagging_mask(), 1u << 2);
            ASSERT_TRUE(m.host->behind_ms(2) >= 4000);
            ASSERT_FALSE(m.host->waiting());
            ASSERT_EQ(m.host->laggard(), 255);
        }
        {   // a notice that is not renewed goes stale after 3 s, one about the client itself after 10 s; a notice about itself never names the others' seat
            LoopbackNetwork net(1);
            sim::SimulationEngine sim;
            build_world(sim, 1);
            ClientSession::Config cc;
            cc.player = 0;
            cc.host = kNoSeat;
            cc.migration = false;
            ClientSession cs(sim, cc);
            auto ends = net.connect({1, 0});
            cs.set_connection(ends.second);
            cs.start(0);
            ASSERT_TRUE(ends.first->send(encode(LagMsg{2, 5000})));
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 8000})));                   // about this client itself
            uint32_t now = 0;
            const auto step_to = [&](uint32_t t) {
                while (now < t) {
                    ++now;
                    net.set_time(now);
                    if (now % 2000 == 0) ends.first->send(encode(TurnMsg{now / 50, {}}));         // (keeps the host's silence from being noticed: any message is a sign of life)
                    cs.update(now);
                }
            };
            step_to(20);
            ASSERT_EQ(cs.lagging_seat(), 2);
            ASSERT_EQ(cs.lagging_behind_ms(), 5000u);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);
            step_to(2900);
            ASSERT_EQ(cs.lagging_seat(), 2);                                          // 2.9 s after it came
            step_to(3100);
            ASSERT_EQ(cs.lagging_seat(), 255);                                        // gone: not renewed
            ASSERT_EQ(cs.lagging_behind_ms(), 0u);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);                                // the one about itself lasts longer
            step_to(9900);
            ASSERT_EQ(cs.self_lag_behind_ms(), 8000u);
            step_to(10100);
            ASSERT_EQ(cs.self_lag_behind_ms(), 0u);
            // renewed and ended
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 4000})));
            step_to(10200);
            ASSERT_EQ(cs.self_lag_behind_ms(), 4000u);
            ASSERT_TRUE(ends.first->send(encode(LagMsg{0, 0})));                       // "back within a second"
            step_to(10300);
            ASSERT_EQ(cs.self_lag_behind_ms(), 0u);
            ASSERT_EQ(cs.lagging_seat(), 255);
        }
        {   // a pass that comes late seals the turns it missed, ten at the most (half a second), and the schedule goes on in the passes that follow
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            sim::SimulationEngine sim;
            build_world(sim, 1);
            HostSession host(sim, hc);
            host.start(0);
            host.update(2000);                                                     // forty turns are due
            ASSERT_EQ(host.turns_sealed(), 10u);
            host.update(2000);
            ASSERT_EQ(host.turns_sealed(), 20u);
            host.update(2000);
            host.update(2000);
            host.update(2000);
            ASSERT_EQ(host.turns_sealed(), 41u);                                   // all the turns up to 2000 ms (0, 50, ... 2000), then the schedule is level
            host.update(2049);
            ASSERT_EQ(host.turns_sealed(), 41u);
            host.update(2050);
            ASSERT_EQ(host.turns_sealed(), 42u);
        }
        {   // "Catching up..." begins when 60 turns (3 s) of the match wait unplayed in the queue, not before
            for (const uint32_t queued : {60u, 61u}) {
                LoopbackNetwork net(1);
                sim::SimulationEngine sim;
                build_world(sim, 1);
                ClientSession::Config cc;
                cc.player = 0;
                cc.host = kNoSeat;
                cc.migration = false;
                ClientSession cs(sim, cc);
                auto ends = net.connect({1, 0});
                cs.set_connection(ends.second);
                cs.start(0);
                for (uint32_t t = 0; t < queued; ++t) ASSERT_TRUE(ends.first->send(encode(TurnMsg{t, {}})));
                net.set_time(10);
                cs.update(10);                                                      // the runner starts and runs one turn: queued - 1 stay
                ASSERT_EQ(cs.runner().backlog_ms(), (queued - 1) * 50);
                ASSERT_EQ(cs.catching_up(), queued - 1 >= 60);                      // 59 turns are 2950 ms, 60 are 3000
            }
        }
    } TEST_END();

    TEST_CASE("N2.38 Flood Control In A Match Whose Host Holds A Seat (A LAN Game): A Guest That Floods Valid Messages Is Thrown Out, The Host And The Other Guest Play On Bit-Identically") {
        for (const std::string kind : {"ack", "ping", "chat", "hash"}) {
            Match m(1, 3, {20, 0});
            m.run(1000);
            for (uint32_t i = 0; i < 3000; ++i) {
                if (kind == "ack") m.client_ends[2]->send(encode(AckMsg{0}));
                else if (kind == "ping") m.client_ends[2]->send(encode_ping(PingMsg{i + 1, 0}));
                else if (kind == "chat") {
                    ChatMsg c;
                    c.text = "spam";
                    m.client_ends[2]->send(encode(c));
                } else {
                    HashMsg h;
                    h.turn = 0xFFFFFFF0u;
                    m.client_ends[2]->send(encode(h));
                }
            }
            m.run(1000, false);
            ASSERT_FALSE(m.host->client_present(2));
            ASSERT_EQ(m.host->violations(2), 8u);
            ASSERT_TRUE(m.host->client_present(1));
            m.settle();
            ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.host->turns_sealed() > 20);
        }
    } TEST_END();

    TEST_CASE("N2.39 Flood Control With Turns Of 50 ms: What An Honest Client Sends Stays Far Below The Budget Of 1000 A Second (Steady, A Person Who Gives 20 Orders A Second, A Window That Catches Up At 4x After A Freeze, An Uplink That Delivers 25 s At Once)") {
        // The budget (flood.hpp) was set for turns of 100 ms. With turns of 50 ms a client acknowledges twice as often, so the claim "an honest client is far below it" is measured again:
        // every message that the host takes from a seat is noted with the time it was taken, and the most that any second holds is the client's peak. (The sequencer takes 64 orders
        // a turn from a seat, 1280 a second: a client at that rate is not a person and meets the budget first.)
        const size_t margin = kMessagesPerSecond / 5;                                  // the claim: five times below the budget at the least
        const auto report = [](const char* what, size_t peak) { std::cout << "\n      [flood budget] " << what << ": peak " << peak << " messages in one second (budget " << kMessagesPerSecond << ")"; };
        {   // 1. steady play over links of 60 ms with jitter: the order of 20 acknowledgements, a hash and a ping a second, and the scripted orders (2.5 a second)
            ServerMatch m(1, 4, {60, 30}, {}, {}, true);
            m.run(60000);
            size_t peak = 0;
            for (uint8_t p = 0; p < 4; ++p) {
                peak = std::max(peak, m.arrivals[p]->peak(1000));
                ASSERT_TRUE(m.host->client_present(p) && m.host->violations(p) == 0u);
            }
            report("steady play", peak);
            ASSERT_TRUE(peak >= 20 && peak <= margin);                                 // (20 turns a second are acknowledged: the test does measure something)
        }
        {   // 2. a person who gives 20 orders a second (more than a hand can) on top of that
            ServerMatch m(2, 4, {40, 20}, {}, {}, true);
            m.run(30000, true, [&](uint32_t t) {
                if (t % 50 != 0) return;
                for (uint8_t p = 0; p < 4; ++p) m.clients[p]->submit(cmd(CommandType::Stop, p, 255, 0, 0, {m.ids.ants[p][0]}));
            });
            size_t peak = 0;
            for (uint8_t p = 0; p < 4; ++p) {
                peak = std::max(peak, m.arrivals[p]->peak(1000));
                ASSERT_TRUE(m.host->client_present(p) && m.host->violations(p) == 0u);
            }
            report("20 orders a second", peak);
            ASSERT_TRUE(peak >= 40 && peak <= margin);
        }
        {   // 3. a window that did not run for 10 s and catches up at up to four times the speed: still one acknowledgement per turn that ran, at most
            ServerMatch m(3, 3, {30, 10}, {}, {}, true);
            m.run(5000);
            m.frozen_mask = 1u << 1;
            m.run(10000);
            m.frozen_mask = 0;
            const uint32_t back = m.now;
            m.run(15000);
            const size_t peak = m.arrivals[1]->peak(1000, back);
            report("catching up at 4x after a freeze of 10 s", peak);
            ASSERT_TRUE(peak >= 40 && peak <= margin);
            ASSERT_TRUE(m.host->client_present(1) && m.host->violations(1) == 0u);
            m.settle();
            ASSERT_TRUE(m.all_equal());                                                // it caught up: every machine ends at the same state
            ASSERT_TRUE(m.host->desyncs().empty());
        }
        {   // 4. an uplink that is stuck for 25 s (the longest that a server tolerates is 30 s without an acknowledgement) and delivers everything at once
            ServerMatch m(4, 3, {30, 10}, {}, {}, true);
            m.run(5000);
            m.arrivals[2]->hold = true;
            m.run(25000);
            ASSERT_TRUE(m.host->client_present(2));
            ASSERT_EQ(m.host->lagging_mask() & 4u, 4u);                                 // (the others were told, as the policy says)
            m.arrivals[2]->hold = false;
            const uint32_t release = m.now;
            m.run(5000);
            const size_t burst = m.arrivals[2]->peak(1000, release);
            report("an uplink that delivers 25 s at once", burst);
            ASSERT_TRUE(burst >= 400 && burst < kMessageBurst);                         // a real burst, and the burst of the budget holds it
            ASSERT_TRUE(m.host->client_present(2) && m.host->violations(2) == 0u);
            ASSERT_EQ(m.host->lagging_mask() & 4u, 0u);                                 // its acknowledgements are all there now: it is level again
            m.settle();
            ASSERT_TRUE(m.all_equal());
        }
        std::cout << "\n     ";                                                          // (the report lines above end the line: PASS gets one of its own)
    } TEST_END();

    TEST_CASE("N2.13 Desync: A Diverging Client Is Named Within A Second, With The Subsystem, And Sees It Itself") {
        Match m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        // corrupt client 2's ant state (a hit point that no command explains)
        m.sims[2]->get_unit(m.ids.ants[2][1]).hp = 3;
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_FALSE(m.host->desyncs().empty());
        ASSERT_TRUE(m.now - before <= 2000);
        const DesyncMsg& d = m.host->desyncs()[0];
        ASSERT_EQ(d.player, 2);
        ASSERT_TRUE(d.peer.ants != d.host.ants);
        ASSERT_TRUE(d.peer.grid == d.host.grid && d.peer.players == d.host.players);       // only the ants differ
        ASSERT_TRUE(m.clients[1]->desynced());
        ASSERT_EQ(m.clients[1]->desync().player, 2);
        ASSERT_TRUE(m.clients[0]->desynced());                                              // everybody is told
        ASSERT_TRUE(m.host->frozen());                                                      // and the match stops sealing turns
        for (const auto& x : m.host->desyncs()) ASSERT_EQ(x.player, 2);                     // only client 2 is blamed
    } TEST_END();

    TEST_CASE("N2.14 Flow Control: A Peer That Stops Executing Holds The Match Up After 3 s, Then The Game Continues When It Catches Up") {
        Match m(1, 3, {30, 0});
        m.run(2000);
        const uint32_t sealed_before_stall = m.host->turns_sealed();
        // client 2 freezes: it neither executes nor acknowledges (its updates stop)
        uint32_t now = m.now;
        for (uint32_t end = now + 6000; now < end; now += 10) {
            m.net.set_time(now);
            m.host->update(now);
            m.clients[0]->update(now);
        }
        m.now = now;
        ASSERT_TRUE(m.host->waiting());
        ASSERT_EQ(m.host->laggard(), 2);
        const uint32_t stalled_at = m.host->turns_sealed();
        ASSERT_TRUE(stalled_at > sealed_before_stall);
        ASSERT_TRUE(stalled_at <= sealed_before_stall + 60 + 90);                           // at most max lag (3 s) + what was in flight
        // the freeze ends: it catches up and the match goes on
        m.run(8000, false);
        ASSERT_FALSE(m.host->waiting());
        ASSERT_TRUE(m.host->turns_sealed() > stalled_at + 60);
        m.settle();
        ASSERT_TRUE(m.all_equal());
        ASSERT_TRUE(m.host->desyncs().empty());
    } TEST_END();

    TEST_CASE("N2.15 Drop-Out: A Client Whose Connection Dies Is Reported Once And The Others Play On, Still Identical") {
        Match m(1, 4, {30, 5});
        std::vector<uint8_t> left;
        m.host->set_on_player_left([&](uint8_t p) { left.push_back(p); });
        m.run(3000);
        m.net.cut(m.client_ends[3], true);
        m.run(5000);
        ASSERT_EQ(left.size(), 1u);
        ASSERT_EQ(left[0], 3);
        ASSERT_FALSE(m.host->client_present(3));
        m.settle();
        for (size_t i = 1; i < 3; ++i) ASSERT_TRUE(m.sims[i]->state_hash() == m.sims[0]->state_hash());
        ASSERT_TRUE(m.host->desyncs().empty() || m.host->desyncs()[0].player == 3);        // only the cut peer can differ
        ASSERT_TRUE(m.host->turns_sealed() > 140);                                          // the game did not stop for it
    } TEST_END();

    TEST_CASE("N2.16 Hostile Client: Garbage, Host-Only Messages, Oversized Messages And Floods Get It Thrown Out; Nobody Else Notices") {
        Match m(1, 3, {20, 0});
        m.run(1000);
        Connection* evil = m.client_ends[2];
        evil->send({1, 2, 3});                                          // a Hello that is too short
        evil->send({static_cast<uint8_t>(MsgType::Turn), 0, 0, 0, 0, 0, 0});
        evil->send({255, 255, 255});
        evil->send(std::vector<uint8_t>(kMaxMessageBytes + 10, 4));      // oversized
        m.run(500, false);
        ASSERT_TRUE(m.host->violations(2) >= 4 || !m.host->client_present(2));
        for (int i = 0; i < 400; ++i) evil->send(encode(CommandMsg{cmd(CommandType::Hatch, 2)}));
        m.run(1000, false);
        ASSERT_FALSE(m.host->client_present(2));                        // thrown out
        ASSERT_FALSE(evil->is_open());
        m.settle();
        ASSERT_TRUE(m.sims[1]->state_hash() == m.sims[0]->state_hash());
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_TRUE(m.host->turns_sealed() > 40);                       // the game went on for everybody else
    } TEST_END();

    TEST_CASE("N2.17 Chat: A Line For All Is Relayed To Everybody With The Sender Stamped By The Connection (A Line For The Team Is Another Rule Since The Release That Filters Team Chat: N2.90 - N2.92)") {
        Match m(1, 3, {20, 0});
        std::vector<ChatMsg> host_seen;
        std::vector<ChatMsg> c1_seen;
        std::vector<ChatMsg> c2_seen;
        m.host->set_on_chat([&](const ChatMsg& c) { host_seen.push_back(c); });
        m.clients[0]->set_on_chat([&](const ChatMsg& c) { c1_seen.push_back(c); });
        m.clients[1]->set_on_chat([&](const ChatMsg& c) { c2_seen.push_back(c); });
        m.run(500);
        ASSERT_TRUE(m.clients[0]->chat("hello all", false));
        m.host->chat_local("host here", false);                          // (this line used to be a team line: a team line goes to the ally only now, N2.92)
        m.run(500, false);
        ASSERT_EQ(host_seen.size(), 2u);
        ASSERT_EQ(c2_seen.size(), 2u);
        ASSERT_EQ(c1_seen.size(), 2u);                                  // the sender gets its own text back through the host
        auto find = [](const std::vector<ChatMsg>& v, const std::string& text) -> const ChatMsg* {
            for (const auto& c : v) {
                if (c.text == text) return &c;
            }
            return nullptr;
        };
        for (const auto* seen : {&host_seen, &c1_seen, &c2_seen}) {
            const ChatMsg* a = find(*seen, "hello all");
            const ChatMsg* b = find(*seen, "host here");
            ASSERT_TRUE(a != nullptr && b != nullptr);
            ASSERT_TRUE(a->sender == 1 && !a->team);
            ASSERT_TRUE(b->sender == 0 && !b->team);
        }
        m.host->chat_local("host team", true);                          // the host has no ally: nobody but itself hears a line for its team
        m.run(500, false);
        ASSERT_EQ(host_seen.size(), 3u);
        ASSERT_TRUE(c1_seen.size() == 2u && c2_seen.size() == 2u);
        // a client that forges the sender byte is stamped anyway
        ChatMsg forged;
        forged.sender = 0;
        forged.text = "I am the host";
        m.client_ends[2]->send(encode(forged));
        m.run(500, false);
        const ChatMsg* forged_seen = find(host_seen, "I am the host");
        ASSERT_TRUE(forged_seen != nullptr && forged_seen->sender == 2);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 11: the fill level of the leader's START, and chat in the waiting room
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol11_tests() {
    TEST_CASE("N2.89 Protocol 11: StartRequest Carries A Fill Level (Every Level Round-Trips, Every Truncation, Trailing Byte And Level Above 3 Is Refused, The Names And Parsing Of The Levels), The Room's Notices Are Chat From Sender 255 (Round Trip, Limits), And 300000 Mutated StartRequests And Chat Lines Only Give Messages That Encode Back To The Same Bytes") {
        ASSERT_EQ(kProtocolVersion, 21);                                 // (the Chat of protocol 11 is the Chat of protocol 17; the StartRequest grew in 13: a level for each seat and the teams, N2.100 has its rules, here the one level of protocol 11 is the same level in every seat)
        ASSERT_TRUE(kFillLevelLast == 3 && kRoomSender == 255 && static_cast<int>(MsgType::StartRequest) == 24 && static_cast<int>(MsgType::Chat) == 9);
        // every level
        const std::pair<FillLevel, const char*> levels[] = {{FillLevel::None, "none"}, {FillLevel::Easy, "easy"}, {FillLevel::Medium, "medium"}, {FillLevel::Hard, "hard"}};
        for (uint8_t i = 0; i < 4; ++i) {
            const StartRequestMsg m = StartRequestMsg::all(levels[i].first);
            const std::vector<uint8_t> bytes = encode(m);
            ASSERT_TRUE(bytes == (std::vector<uint8_t>{24, i, i, i, i, 255, 255}));
            StartRequestMsg back = StartRequestMsg::all(FillLevel::Hard);
            ASSERT_TRUE(decode(bytes, back) && back.fill == m.fill);
            ASSERT_EQ(std::string(fill_level_name(levels[i].first)), std::string(levels[i].second));
            FillLevel parsed = FillLevel::Hard;
            ASSERT_TRUE(parse_fill_level(levels[i].second, parsed) && parsed == levels[i].first);
            std::string upper = levels[i].second;
            for (char& c : upper) c = static_cast<char>(c - 'a' + 'A');
            ASSERT_TRUE(parse_fill_level(upper, parsed) && parsed == levels[i].first);
            // every strict prefix, and every trailing byte, is refused
            for (size_t cut = 0; cut < bytes.size(); ++cut) {
                ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
            }
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = bytes;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
        }
        ASSERT_TRUE(fill_bot_name(FillLevel::Easy) == "Bot (Easy)" && fill_bot_name(FillLevel::Medium) == "Bot (Medium)" && fill_bot_name(FillLevel::Hard) == "Bot (Hard)" && fill_bot_name(FillLevel::None).empty());
        ASSERT_TRUE(fill_level_title(FillLevel::Easy) == "Easy" && fill_level_title(FillLevel::Medium) == "Medium" && fill_level_title(FillLevel::Hard) == "Hard" && fill_level_title(FillLevel::None).empty());     // (what a person is told: "Empty seats will be Medium bots.")
        FillLevel keep = FillLevel::Easy;
        for (const char* bad : {"", "none ", " easy", "med", "mediumm", "hard1", "2", "harder", "NONE\\0"}) ASSERT_FALSE(parse_fill_level(bad, keep));
        ASSERT_EQ(keep, FillLevel::Easy);                                // a refusal leaves the value alone
        // the room's notice: a Chat message whose sender is 255; the limits of a Chat line are the match's
        ChatMsg notice;
        notice.sender = kRoomSender;
        notice.text = kNoticeFillFog;
        ChatMsg back;
        ASSERT_TRUE(decode(encode(notice), back) && back.sender == 255 && !back.team && back.text == kNoticeFillFog);
        ASSERT_TRUE(std::string(kNoticeFillFog).size() <= kMaxChatChars && std::string(kNoticeFillMap).size() <= kMaxChatChars);
        for (const char* text : {kNoticeFillFog, kNoticeFillMap}) {
            for (const char c : std::string(text)) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);       // printable ASCII, as a chat line must be
        }
        ChatMsg full;
        full.sender = 2;
        full.text = std::string(kMaxChatChars, 'q');
        ASSERT_TRUE(decode(encode(full), back) && back.text.size() == kMaxChatChars);
        std::vector<uint8_t> raw = {9, 2, 0, 101};
        raw.insert(raw.end(), 101, 'q');
        ASSERT_FALSE(decode(raw, back));                                 // one character too many
        ASSERT_FALSE(decode(std::vector<uint8_t>{9, 2, 2, 1, 'a'}, back) || decode(std::vector<uint8_t>{9, 2, 0, 1, 0x1F}, back) || decode(std::vector<uint8_t>{9, 2, 0, 1, 0x7F}, back));
        // the fuzz: bytes made from valid StartRequests and chat lines, mutated, cut and lengthened, give a message only when it encodes back to exactly those bytes
        const std::vector<std::vector<uint8_t>> seeds = {encode(StartRequestMsg::all(FillLevel::Easy)), encode(StartRequestMsg::all(FillLevel::Hard)), encode(notice), encode(full),
                                                         encode(ChatMsg{1, false, "hello"}), encode(ChatMsg{3, true, ""}), encode(StartRequestMsg{}),
                                                         encode(StartRequestMsg{{FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard}, 1, 2})};
        Lcg rng(11);
        size_t accepted_requests = 0;
        size_t accepted_chat = 0;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> buf = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
            for (uint32_t k = 1 + rng.below(3); k > 0; --k) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
            const uint32_t shape = rng.below(6);
            if (shape == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            else if (shape == 1) buf.push_back(static_cast<uint8_t>(rng.below(256)));
            StartRequestMsg sr;
            ChatMsg chat;
            const bool is_request = decode(buf, sr);
            const bool is_chat = decode(buf, chat);
            ASSERT_FALSE(is_request && is_chat);                          // (they have different type bytes)
            if (is_request) {
                ++accepted_requests;
                ASSERT_TRUE(encode(sr) == buf && buf.size() == 7 && buf[0] == 24);
                for (const FillLevel level : sr.fill) ASSERT_TRUE(static_cast<uint8_t>(level) <= kFillLevelLast);
            }
            if (is_chat) {
                ++accepted_chat;
                ASSERT_TRUE(encode(chat) == buf && chat.text.size() <= kMaxChatChars);
                for (const char c : chat.text) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
            }
            if (!buf.empty() && (buf[0] == 24 || buf[0] == 9)) ASSERT_EQ(any_decodes(buf), is_request || is_chat);     // no other decoder takes either type
        }
        ASSERT_TRUE(accepted_requests > 300 && accepted_chat > 3000);               // (a message of seven bytes survives a mutation rarely; the ones that do are all the right ones)
    } TEST_END();
}

namespace {

// ---------------------------------------------------------------------------------------------------------------------------------
// Team chat reaches only the allies (the relay of a host, by the host's own alliance table at the time of the relay)
// ---------------------------------------------------------------------------------------------------------------------------------

// A match of a host and `players` seats over a loopback network, built to see WHAT THE HOST PUTS ON EACH LINK: the host's end of every link is a HostTap (it counts the Chat messages that
// it sends), and a seat of `raw_mask` has no session at all: a raw client that never filters, never acknowledges and reads every message of its link (what a modified client would see).
// `lan` is a game on the local network (the host holds seat 0 and is a receiver like the others); without it the host is a dedicated server's referee and holds no seat.
struct TeamChatRig {
    LoopbackNetwork net;
    sim::SimulationEngine host_engine;                                  // the host's own engine: the table that the relay reads
    std::vector<std::unique_ptr<sim::SimulationEngine>> sims;           // by seat (null for the host's seat and for a raw seat)
    Ids ids;
    std::unique_ptr<HostSession> host;
    std::vector<std::unique_ptr<HostTap>> taps;                         // the host's end of the link of each seat (null for the host's own seat)
    std::vector<std::unique_ptr<ClientSession>> clients;                // by seat (null for the host's seat and for a raw seat)
    std::vector<Connection*> client_ends;
    std::vector<ChatMsg> heard[sim::MAX_PLAYERS];                       // what the session of a seat reported as chat
    std::vector<ChatMsg> raw[sim::MAX_PLAYERS];                         // every Chat message that arrived on the link of a raw seat (decoded from the bytes)
    std::vector<ChatMsg> host_heard;                                    // what the host's own callback was given
    uint32_t now{0};
    uint8_t players;
    bool lan;

    TeamChatRig(bool lan_, uint8_t players_, uint8_t raw_mask) : net(77), players(players_), lan(lan_) {
        ids = build_world(host_engine, 1);
        HostSession::Config hc;
        hc.host_player = lan ? uint8_t{0} : kNoSeat;
        host = std::make_unique<HostSession>(host_engine, hc);
        sims.resize(players);
        taps.resize(players);
        clients.resize(players);
        client_ends.assign(players, nullptr);
        for (uint8_t p = lan ? 1 : 0; p < players; ++p) {
            auto ends = net.connect({20, 0});
            taps[p] = std::make_unique<HostTap>(ends.first, &now);
            host->add_client(p, taps[p].get());
            client_ends[p] = ends.second;
            if ((raw_mask & (1u << p)) != 0) continue;
            sims[p] = std::make_unique<sim::SimulationEngine>();
            build_world(*sims[p], 1);
            ClientSession::Config c;
            c.player = p;
            c.host = lan ? uint8_t{0} : kNoSeat;
            c.migration = false;
            clients[p] = std::make_unique<ClientSession>(*sims[p], c);
            clients[p]->set_connection(ends.second);
            clients[p]->set_on_chat([this, p](const ChatMsg& m) { heard[p].push_back(m); });
        }
        host->set_on_chat([this](const ChatMsg& m) { host_heard.push_back(m); });
        host->start(0);
        for (auto& c : clients) {
            if (c) c->start(0);
        }
    }

    void step(uint32_t ms) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            net.set_time(now);
            host->update(now);
            for (uint8_t p = 0; p < players; ++p) {
                if (clients[p]) {
                    clients[p]->update(now);
                } else if (client_ends[p] != nullptr) {
                    std::vector<uint8_t> msg;
                    while (client_ends[p]->poll(msg)) {
                        ChatMsg c;
                        if (peek_type(msg) == MsgType::Chat && decode(msg, c)) raw[p].push_back(c);
                    }
                }
            }
        }
    }
    void order(uint8_t seat, const Command& c) {
        if (lan && seat == 0) host->submit_local(c);
        else clients[seat]->submit(c);
    }
    bool say(uint8_t seat, const std::string& text, bool team) {
        if (lan && seat == 0) {
            host->chat_local(text, team);
            return true;
        }
        return clients[seat]->chat(text, team);
    }
    // `a` invites `b`, `b` accepts: true when the host's table says that they are allies
    bool ally(uint8_t a, uint8_t b) {
        order(a, cmd(CommandType::AllianceInvite, a, b));
        step(500);
        order(b, cmd(CommandType::AllianceAccept, b, a));
        step(500);
        return host_engine.alliance_of(a) == b && host_engine.alliance_of(b) == a;
    }
    size_t on_wire(uint8_t seat) const { return taps[seat] ? taps[seat]->sent_of(MsgType::Chat) : 0; }     // the Chat messages that the host has sent to the seat
};

bool has_line(const std::vector<ChatMsg>& v, const std::string& text) {
    for (const ChatMsg& c : v) {
        if (c.text == text) return true;
    }
    return false;
}

}  // namespace

void run_team_chat_tests() {
    TEST_CASE("N2.90 Team Chat At The Wire (A Dedicated Server's Host): A Line For The Team Goes To The Sender And Its Ally Only, By The Referee's Alliance Table; A Raw Client Of A Non-Ally That Reads Every Byte Of Its Link Gets Nothing; A Line For All Goes To Everybody; A Player Without An Ally Hears Only Itself; The Referee Logs Every Line") {
        TeamChatRig m(false, 4, 0x08);                                   // seats 0 and 1 will be allies, seat 2 is not, seat 3 is a raw client without a session
        ASSERT_TRUE(m.host->seatless());
        ASSERT_EQ(m.host_engine.alliance_of(0), sim::ALLIANCE_NONE);
        ASSERT_TRUE(m.ally(0, 1));
        ASSERT_TRUE(m.host_engine.alliance_of(2) == sim::ALLIANCE_NONE && m.host_engine.alliance_of(3) == sim::ALLIANCE_NONE);
        // the rule, as a function of the table
        ASSERT_TRUE(m.host->hears_chat(0, true, 0) && m.host->hears_chat(0, true, 1) && !m.host->hears_chat(0, true, 2) && !m.host->hears_chat(0, true, 3));
        ASSERT_TRUE(m.host->hears_chat(0, false, 2) && m.host->hears_chat(0, false, 3) && m.host->hears_chat(2, true, 2) && !m.host->hears_chat(2, true, 0));

        const size_t before_2 = m.on_wire(2);
        const size_t before_3 = m.on_wire(3);
        ASSERT_TRUE(m.say(0, "team A", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "team A") && has_line(m.heard[1], "team A"));        // the sender (its own line comes back) and its ally
        ASSERT_FALSE(has_line(m.heard[2], "team A"));
        ASSERT_TRUE(m.raw[3].empty());                                                         // nothing at all arrived at the raw client
        ASSERT_EQ(m.on_wire(2), before_2);                                                     // and the host never sent it to either: counted on the host's end of the links
        ASSERT_EQ(m.on_wire(3), before_3);
        for (const ChatMsg& c : m.heard[1]) {
            if (c.text == "team A") ASSERT_TRUE(c.sender == 0 && c.team);                      // the line is what it was: the sender's seat (stamped by the connection) and the flag
        }

        ASSERT_TRUE(m.say(1, "team B", true));                                                 // the other way round
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "team B") && has_line(m.heard[1], "team B"));
        ASSERT_FALSE(has_line(m.heard[2], "team B"));
        ASSERT_TRUE(m.raw[3].empty());

        ASSERT_TRUE(m.say(2, "lonely", true));                                                 // a player without an ally: its own screen only
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[2], "lonely"));
        ASSERT_FALSE(has_line(m.heard[0], "lonely") || has_line(m.heard[1], "lonely"));
        ASSERT_TRUE(m.raw[3].empty());

        ASSERT_TRUE(m.say(2, "to all", false));                                                // a line for all: everybody, the raw client too
        m.step(300);
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(has_line(m.heard[p], "to all"));
        ASSERT_TRUE(m.raw[3].size() == 1 && m.raw[3][0].text == "to all" && m.raw[3][0].sender == 2 && !m.raw[3][0].team);
        // counted at the wire: seats 0 and 1 got three lines (team A, team B, to all), seat 2 two (lonely, to all), seat 3 one
        ASSERT_TRUE(m.on_wire(0) == 3 && m.on_wire(1) == 3 && m.on_wire(2) == before_2 + 2 && m.on_wire(3) == before_3 + 1);
        // the referee hears them all (its callback is its log, not a screen)
        ASSERT_EQ(m.host_heard.size(), size_t{4});
    } TEST_END();

    TEST_CASE("N2.91 Team Chat: An Alliance That Is Broken Stops The Delivery At Once (The Next Line After The Referee's Table Changed), A New Alliance Starts It At Once, A Line That Was Sent Before The Break Arrives") {
        TeamChatRig m(false, 4, 0x08);
        ASSERT_TRUE(m.ally(0, 1));
        ASSERT_TRUE(m.say(1, "before the break", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[0], "before the break") && !has_line(m.heard[2], "before the break"));
        const size_t at_0 = m.on_wire(0);
        // seat 0 leaves the team; the very step in which the referee's table changes is the last one in which the line could still go to it
        m.order(0, cmd(CommandType::AllianceBreak, 0));
        int steps = 0;
        while (m.host_engine.alliance_of(0) != sim::ALLIANCE_NONE && steps++ < 300) m.step(10);
        ASSERT_TRUE(steps < 300);
        ASSERT_TRUE(m.host_engine.alliance_of(1) == sim::ALLIANCE_NONE);                       // (an alliance ends for both)
        ASSERT_TRUE(m.say(1, "after the break", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "after the break"));                                  // seat 1 hears itself
        ASSERT_FALSE(has_line(m.heard[0], "after the break"));
        ASSERT_EQ(m.on_wire(0), at_0);                                                         // not sent: the line is not on the host's end of seat 0's link
        ASSERT_TRUE(m.raw[3].empty());
        ASSERT_TRUE(m.say(0, "from the former ally", true));                                   // and the other direction
        m.step(300);
        ASSERT_FALSE(has_line(m.heard[1], "from the former ally"));
        // a new alliance with another seat, and the next line goes there and nowhere else
        ASSERT_TRUE(m.ally(0, 2));
        ASSERT_TRUE(m.say(0, "to the new ally", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[2], "to the new ally") && has_line(m.heard[0], "to the new ally"));
        ASSERT_FALSE(has_line(m.heard[1], "to the new ally"));
        ASSERT_TRUE(m.raw[3].empty());
    } TEST_END();

    TEST_CASE("N2.92 Team Chat: A Host On The Local Network (It Holds A Seat) Relays The Same Way And Is A Receiver Like The Guests: It Hears A Team Line Only When It Is The Sender Or The Sender's Ally; Lines For All Reach Everybody, The Raw Client Included") {
        TeamChatRig m(true, 4, 0x08);                                    // the host is seat 0, seats 1 and 2 are guests with sessions, seat 3 is a raw client
        ASSERT_FALSE(m.host->seatless());
        ASSERT_TRUE(m.ally(1, 2));                                       // two guests are allies; the host is not one of them
        ASSERT_TRUE(m.say(1, "guest team", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "guest team") && has_line(m.heard[2], "guest team"));
        ASSERT_FALSE(has_line(m.host_heard, "guest team"));              // the host's own screen is not told either
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.say(0, "host team", true));                        // the host has no ally: the line is its own screen's and nobody else's
        m.step(300);
        ASSERT_TRUE(has_line(m.host_heard, "host team"));
        ASSERT_FALSE(has_line(m.heard[1], "host team") || has_line(m.heard[2], "host team"));
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.ally(0, 1));                                       // the host teams up with seat 1 (the pair of the guests ends)
        ASSERT_EQ(m.host_engine.alliance_of(2), sim::ALLIANCE_NONE);
        ASSERT_TRUE(m.say(0, "host team 2", true));
        ASSERT_TRUE(m.say(1, "guest team 2", true));
        m.step(300);
        ASSERT_TRUE(has_line(m.heard[1], "host team 2") && !has_line(m.heard[2], "host team 2"));
        ASSERT_TRUE(has_line(m.host_heard, "guest team 2") && has_line(m.heard[1], "guest team 2") && !has_line(m.heard[2], "guest team 2"));      // the host hears its ally
        ASSERT_TRUE(m.raw[3].empty() && m.on_wire(3) == 0);
        ASSERT_TRUE(m.say(2, "all of you", false));                      // a line for all: the host, the guests and the raw client
        ASSERT_TRUE(m.say(0, "host to all", false));
        m.step(300);
        ASSERT_TRUE(has_line(m.host_heard, "all of you") && has_line(m.heard[1], "all of you") && has_line(m.heard[2], "all of you"));
        ASSERT_TRUE(has_line(m.heard[1], "host to all") && has_line(m.heard[2], "host to all"));
        ASSERT_TRUE(m.raw[3].size() == 2 && m.on_wire(3) == 2);
    } TEST_END();

    TEST_CASE("N2.93 The Match's Chat Has A Budget Of Its Own (A Remake Protection: The Original Has No Limit On Chat): A Client Says A Burst Of 5 Lines And Then One A Second; A Line Beyond It Is Dropped (Nobody Hears It, Nothing Is Logged, No Offence At First); A Client That Goes On Beyond It For Long Is Thrown Out; Team Lines Count The Same; Each Client Has Its Own; The Host's Own Player Is Not A Connection And Has No Budget; An Honest Talker Is Never Touched") {
        {   // a burst of 8 from one client (seat 0): 5 reach the others (and itself), in order; the sender stays
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 8; ++i) ASSERT_TRUE(m.say(0, "burst " + std::to_string(i), false));
            m.step(400);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst});
            ASSERT_EQ(m.heard[0].size(), size_t{kChatBurst});
            for (size_t i = 0; i < m.heard[1].size(); ++i) ASSERT_EQ(m.heard[1][i].text, "burst " + std::to_string(i));
            ASSERT_EQ(m.host_heard.size(), size_t{kChatBurst});                          // (the referee's callback hears what was relayed, not what was dropped)
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_EQ(m.host->violations(0), 0u);
            // a second later one more, not two; five quiet seconds fill the burst again
            m.step(1000);
            ASSERT_TRUE(m.say(0, "after a second", false) && m.say(0, "right behind it", false));
            m.step(300);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst + 1});
            ASSERT_EQ(m.heard[1].back().text, std::string("after a second"));
            m.step(6000);
            for (int i = 0; i < 6; ++i) ASSERT_TRUE(m.say(0, "again " + std::to_string(i), false));
            m.step(300);
            ASSERT_EQ(m.heard[1].size(), size_t{kChatBurst + 1 + kChatBurst});
        }
        {   // a team line takes from the same budget; each client has its own; nothing else of the match is touched
            TeamChatRig m(false, 3, 0);
            ASSERT_TRUE(m.ally(0, 1));
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(m.say(0, "team " + std::to_string(i), true));
            for (int i = 0; i < 3; ++i) ASSERT_TRUE(m.say(0, "all " + std::to_string(i), false));
            for (int i = 0; i < 2; ++i) ASSERT_TRUE(m.say(2, "other " + std::to_string(i), false));
            m.step(400);
            size_t from_0 = 0, from_2 = 0;
            for (const ChatMsg& c : m.heard[1]) (c.sender == 0 ? from_0 : from_2) += 1;
            ASSERT_EQ(from_0, size_t{kChatBurst});                                       // 3 team + 3 all = 6 lines: the sixth is dropped
            ASSERT_EQ(from_2, size_t{2});                                                // seat 2 has its own burst
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        }
        {   // an honest talker: a line every 1.2 s for a minute and a busy one, a line a second for two minutes: all heard, no offence
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 50; ++i) {
                ASSERT_TRUE(m.say(0, "talk " + std::to_string(i), false));
                m.step(1200);
            }
            ASSERT_EQ(m.heard[1].size(), size_t{50});
            for (int i = 0; i < 120; ++i) {
                ASSERT_TRUE(m.say(2, "busy " + std::to_string(i), false));
                m.step(1000);
            }
            ASSERT_EQ(m.heard[1].size(), size_t{170});
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
            ASSERT_TRUE(m.host->violations(0) == 0u && m.host->violations(2) == 0u);
        }
        {   // two lines a second for a minute: half are dropped (the excess refills as fast as it is used), nobody is thrown out; four a second for long: the excess (20 lines) runs out after
            // about ten seconds, then each is a violation and eight throw the client out
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 120; ++i) {
                ASSERT_TRUE(m.say(0, "two a second " + std::to_string(i), false));
                m.step(500);
            }
            ASSERT_TRUE(m.host->client_present(0));
            ASSERT_TRUE(m.heard[1].size() >= 60 && m.heard[1].size() <= 70);
            m.step(6000);
            const size_t before = m.heard[1].size();
            size_t sent = 0;
            while (m.host->client_present(0) && sent < 400) {
                ASSERT_TRUE(m.say(0, "four a second " + std::to_string(sent), false));
                ++sent;
                m.step(250);
            }
            ASSERT_FALSE(m.host->client_present(0));
            ASSERT_TRUE(sent >= 30 && sent <= 90);
            ASSERT_TRUE(m.heard[1].size() - before >= 10 && m.heard[1].size() - before <= 30);
            ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));
        }
        {   // a flood is out within a pass; the other client's honest line is heard as ever
            TeamChatRig m(false, 3, 0);
            for (int i = 0; i < 900; ++i) m.client_ends[0]->send(encode(ChatMsg{0, false, "flood " + std::to_string(i)}));
            ASSERT_TRUE(m.say(1, "an honest line", false));
            m.step(300);
            ASSERT_FALSE(m.host->client_present(0));
            size_t honest = 0, spam = 0;
            for (const ChatMsg& c : m.heard[2]) (c.text == "an honest line" ? honest : spam) += 1;
            ASSERT_TRUE(honest == 1 && spam == kChatBurst);
        }
        {   // the host's own player (a game on the local network) is no connection: twenty lines at once all go out
            TeamChatRig m(true, 3, 0);
            for (int i = 0; i < 20; ++i) ASSERT_TRUE(m.say(0, "host line " + std::to_string(i), false));
            m.step(400);
            ASSERT_EQ(m.heard[1].size(), size_t{20});
            ASSERT_EQ(m.heard[2].size(), size_t{20});
            ASSERT_EQ(m.host_heard.size(), size_t{20});
        }
    } TEST_END();
}


// ---- protocol 12: the start of a match (the first turn is sealed kMatchStartDelayMs after the match began: the "Get ready to play!" dialog of every machine) -------------------------------

namespace {

// The turns of a host's own runner that carry a command of `issuer` (it keeps the last 30 s of turns: the first seconds of a test are in it)
std::vector<uint32_t> turns_with(HostSession& host, uint8_t issuer) {
    std::vector<uint32_t> out;
    for (uint32_t t = 0; t < host.turns_sealed(); ++t) {
        const TurnMsg* turn = host.runner().logged_turn(t);
        if (turn == nullptr) continue;
        for (const Command& c : turn->commands) {
            if (c.issuer == issuer) out.push_back(t);
        }
    }
    return out;
}

// The turn that a command is sealed into when it reaches the host at `arrive_ms` on a host that sealed its first turn at `first_seal_ms` (a turn every 50 ms; the host reads what has arrived before it seals, so
// a command that arrives on the very pass of a seal is in that turn)
uint32_t turn_for_arrival(uint32_t arrive_ms, uint32_t first_seal_ms) { return (arrive_ms - first_seal_ms + kTurnMs - 1) / kTurnMs; }

}  // namespace

void run_protocol12_tests() {
    TEST_CASE("N2.94 Protocol 12, The Start Of A Match (A Host With A Seat): The Host Seals The First Turn Exactly start_delay_ms After start() And Nothing Before It; The Seconds Before Are No Stall, No Lag, No Pause And No Growth Of The Jitter Buffer; The First Turn Runs On Every Machine A Link's Delay And One Turn Of Buffer Later; The Match Is Then Identical Everywhere; With The Default Of 0 The First Turn Is Sealed At Once (The Rigs)") {
        ASSERT_EQ(kMatchStartDelayMs, 5000u);                                       // the dialog's 5 s (the original's task KWFO: Ants.exe 0x10254b0)
        ASSERT_EQ(kMatchStartDelayMs, sim::kMatchStartDialogMs);
        {   // the default is a host that seals at once, as every rig of this suite wants it
            Match m(3, 3, {40, 10});
            m.run(20, false);
            ASSERT_TRUE(m.host->turns_sealed() >= 1);
        }
        HostSession::Config hc;
        hc.start_delay_ms = kMatchStartDelayMs;
        Match m(5, 4, {40, 10}, hc);
        uint32_t first_seal = 0;
        uint32_t first_tick = 0;
        uint32_t first_tick_client = 0;
        while (m.now < 30000 && (first_seal == 0 || first_tick == 0 || first_tick_client == 0)) {
            m.run(10, false);
            if (first_seal == 0 && m.host->turns_sealed() >= 1) first_seal = m.now;
            if (first_tick == 0 && m.sims[0]->current_tick() >= 1) first_tick = m.now;
            if (first_tick_client == 0 && m.sims[3]->current_tick() >= 1) first_tick_client = m.now;
            if (first_seal == 0) {                                                  // the seconds before the first turn: nothing is sealed, run, waited for or reported
                ASSERT_EQ(m.host->turns_sealed(), 0u);
                ASSERT_TRUE(!m.host->paused() && !m.host->waiting() && m.host->lagging_mask() == 0 && m.host->laggard() == 255 && m.host->desyncs().empty());
                ASSERT_EQ(m.host->runner().next_turn_to_execute(), 0u);
                for (auto& c : m.clients) {
                    ASSERT_TRUE(c->mode() == ClientSession::Mode::Normal && c->connected() && !c->paused() && !c->catching_up());
                    ASSERT_TRUE(c->lagging_seat() == 255 && c->self_lag_behind_ms() == 0);
                    ASSERT_TRUE(c->runner().queued() == 0 && c->runner().next_turn_to_execute() == 0 && c->runner().next_turn_expected() == 0);
                    ASSERT_TRUE(c->runner().stalled_ms() == 0 && !c->runner().stalled() && !c->runner().rebuilding() && c->runner().buffer_turns() == 1);   // not a stall of the link
                }
                for (auto& sm : m.sims) ASSERT_EQ(sm->current_tick(), 0u);
            }
        }
        ASSERT_EQ(first_seal, kMatchStartDelayMs);                                  // sealed on the very pass that is 5 s after start()
        ASSERT_TRUE(first_tick >= kMatchStartDelayMs && first_tick <= kMatchStartDelayMs + 2 * kTurnMs);                       // the host's own runner begins with two turns in hand
        ASSERT_TRUE(first_tick_client >= kMatchStartDelayMs + 30 && first_tick_client <= kMatchStartDelayMs + 400);             // a client: the link's delay, the second turn, a few steps of jitter
        m.run(10000, false);
        for (auto& c : m.clients) ASSERT_TRUE(c->runner().buffer_turns() == 1 && !c->runner().stalled() && c->lagging_seat() == 255);   // the wait before the start left the buffer as it was
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        ASSERT_TRUE(m.all_equal());
        ASSERT_TRUE(m.sims[0]->current_tick() > 150);
    } TEST_END();

    TEST_CASE("S2.15 Protocol 12, A Dedicated Server's Start: The Referee's First Turn Is Sealed Exactly start_delay_ms After start() And Nothing Before It, Nobody Is Behind, Announced As Lagging Or Paused In The Seconds Before It (A Room That Holds Seats Included); The Idle Rule Of The Lag Policy Does Not Count Those Seconds: A Seat That Never Acks Is Dropped Its Idle Time After The First Turn, Not After start()") {
        {   // the idle rule: seat 3's window never runs (no acks, no pings), the rule is 8 s (30 s by default: a shorter rule makes the difference visible)
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            hc.lag_drop_idle_ms = 8000;
            ServerMatch m(4, 4, {40, 10}, hc);
            m.frozen_mask = 1u << 3;
            uint32_t first_seal = 0;
            while (m.now < 5200 && first_seal == 0) {
                m.run(10, false);
                if (m.host->turns_sealed() >= 1) first_seal = m.now;
                if (first_seal == 0) {                                              // the quiet seconds: nothing is sealed, run or waited for
                    ASSERT_TRUE(m.host->turns_sealed() == 0 && m.referee.current_tick() == 0 && !m.host->paused() && m.host->lagging_mask() == 0);
                    ASSERT_TRUE(m.clients[0]->lagging_seat() == 255 && !m.clients[0]->catching_up() && !m.clients[0]->paused() && m.clients[0]->runner().stalled_ms() == 0);
                }
            }
            ASSERT_EQ(first_seal, kMatchStartDelayMs);
            m.run(7000, false);                                                     // 12 s after start(): the idle rule would have dropped the seat at 8 s if the quiet seconds counted
            ASSERT_TRUE(m.host->client_present(3));
            ASSERT_TRUE(m.clients[0]->lagging_seat() == 3);                         // (the others are told who lags once it is 3 s behind)
            m.run(2000, false);                                                     // 14 s: 9 s after the first turn, the idle time of 8 s is over
            ASSERT_FALSE(m.host->client_present(3));                                // dropped, the others play on
            for (auto& c : m.clients) ASSERT_FALSE(c->lost());
            m.run(1500, false);
            ASSERT_TRUE(m.sims[0]->is_player_dropped(3) && m.referee.is_player_dropped(3));
            ASSERT_TRUE(m.sims[0]->current_tick() > 100);
        }
        {   // a room that holds seats: the seconds before the first turn are no pause (nobody is missing, nothing is announced), and the first turn comes on time
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            hc.hold_seats = true;
            ServerMatch m(5, 3, {40, 10}, hc);
            uint32_t first_seal = 0;
            while (m.now < 5200 && first_seal == 0) {
                m.run(10, false);
                if (m.host->turns_sealed() >= 1) first_seal = m.now;
                if (first_seal == 0) {
                    ASSERT_TRUE(!m.host->paused() && !m.host->attendance().paused() && m.host->rejoiners() == 0);
                    for (auto& c : m.clients) ASSERT_TRUE(!c->paused() && c->presence().missing.empty() && c->presence().resume_s == 0 && c->mode() == ClientSession::Mode::Normal);
                }
            }
            ASSERT_EQ(first_seal, kMatchStartDelayMs);
            m.run(8000, false);
            ASSERT_TRUE(!m.host->paused() && m.referee.current_tick() > 100);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.95 Protocol 12, A Command That Reaches The Host Before Its First Turn Is Sealed Is Discarded, Not Counted As A Violation (A Host With A Seat): A Guest's Orders At 100 ms (Seventy At Once), 4,900 ms And 4,950 ms Of The 5 s Wait Are In No Turn, One That Arrives 10 ms After The First Seal Is In Turn 1, An Honest Guest's First Order Is Not Delayed By The Gate, And With start_delay_ms 0 A Command Is Accepted At Once") {
        const uint32_t link = 40;                                                   // one way, no jitter: what is sent at t arrives at t + 40
        HostSession::Config hc;
        hc.start_delay_ms = kMatchStartDelayMs;
        Match m(7, 3, {link, 0}, hc);                                               // the host plays seat 0; seat 1 is the guest that orders in the wait (a modified client, or a rig: a client has no gate of its own), seat 2 is honest
        const auto order = [&](uint8_t seat, int16_t x) { return cmd(CommandType::GroupMove, seat, 255, x, 12, m.ids.ants[seat]); };
        bool all_sent = true;
        uint32_t sealed_before_the_seal = 99;
        bool honest_sent = false;
        uint32_t honest_sent_at = 0;
        m.run(5400, false, [&](uint32_t now) {
            if (now == 100) {
                for (int i = 0; i < 70; ++i) all_sent = m.clients[0]->submit(order(1, static_cast<int16_t>(10 + i % 5))) && all_sent;       // seventy at once: a turn takes 64, the rest would be carried into the next ones
            }
            if (now == 4900) all_sent = m.clients[0]->submit(order(1, 30)) && all_sent;
            if (now == 4950) all_sent = m.clients[0]->submit(order(1, 11)) && all_sent;                                                    // arrives at 4990, 10 ms before the first seal
            if (now == 4970) all_sent = m.clients[0]->submit(order(1, 31)) && all_sent;                                                    // arrives at 5010, 10 ms after it
            if (now == 4990) sealed_before_the_seal = m.host->turns_sealed();
            if (!honest_sent && m.sims[2]->current_tick() >= 1) {                                                                          // seat 2's earliest order: the frame after its dialog is gone (its first turn has run)
                honest_sent = m.clients[1]->submit(order(2, 20));
                honest_sent_at = now;
            }
        });
        ASSERT_TRUE(all_sent && honest_sent);
        ASSERT_EQ(sealed_before_the_seal, 0u);                                      // nothing was sealed until 5000 ms
        ASSERT_TRUE(m.host->turns_sealed() >= 8);
        ASSERT_TRUE(turns_with(*m.host, 1) == std::vector<uint32_t>{1});            // of seat 1's seventy-three orders only the one that arrived at 5010 ms is in a turn: turn 1, sealed at 5050 ms; the others are in no turn (not turn 0, and none was kept for later)
        for (uint32_t t : {0u, 1u, 2u}) {
            const TurnMsg* turn = m.host->runner().logged_turn(t);
            ASSERT_TRUE(turn != nullptr);
            for (const Command& c : turn->commands) ASSERT_TRUE(t == 1 && c.issuer == 1 && c.tile_x == 31);        // the late order and nothing else in the first three turns
        }
        for (uint8_t seat : {uint8_t{1}, uint8_t{2}}) ASSERT_TRUE(m.host->client_present(seat) && m.host->violations(seat) == 0u);      // a discarded command is no offence: the seat is not on its way out
        const std::vector<uint32_t> honest = turns_with(*m.host, 2);                // seat 2 sent nothing in the wait, and its first order is not held back by the gate
        ASSERT_TRUE(honest.size() == 1);
        ASSERT_EQ(honest[0], turn_for_arrival(honest_sent_at + link, kMatchStartDelayMs));      // the first turn after its arrival (turn 3 on a link of 40 ms)
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        ASSERT_TRUE(m.all_equal());
        {   // the default delay of 0 (every rig of this suite): the first pass reads what has arrived before it seals turn 0, and the command is in turn 0 (the gate is not "before turn 0 was sealed" for a match that does not wait)
            Match z(8, 2, {0, 0});
            bool sent = false;
            z.run(30, false, [&](uint32_t now) {
                if (now == 10) sent = z.clients[0]->submit(cmd(CommandType::GroupMove, 1, 255, 10, 12, z.ids.ants[1]));
            });
            ASSERT_TRUE(sent);
            ASSERT_TRUE(turns_with(*z.host, 1) == std::vector<uint32_t>{0});
            ASSERT_EQ(z.host->violations(1), 0u);
        }
    } TEST_END();

    TEST_CASE("S2.16 Protocol 12, A Command That Reaches A Dedicated Server's Referee Before Its First Turn Is Sealed Is Discarded Without A Violation: A Raw Seat (A Modified Client) That Writes Seventy Orders At 100 ms And One That Arrives 100 ms Before The First Seal Has Them In No Turn, A Raw Order After The First Seal Is Accepted, And The Honest Seats' First Orders Are Sealed Into The First Turn After They Arrive (On Links Of 40, 120 And 300 ms): The Raw Seat Has No Opening Ahead Of Them") {
        for (const uint32_t link : {40u, 120u, 300u}) {
            HostSession::Config hc;
            hc.start_delay_ms = kMatchStartDelayMs;
            ServerMatch m(11, 3, {link, 0}, hc);                                    // seats 0 and 1 play honestly, seat 2 is a raw connection that writes CommandMsg itself
            const auto order = [&](uint8_t seat, int16_t x) { return cmd(CommandType::GroupMove, seat, 255, x, 12, m.ids.ants[seat]); };
            bool honest_sent[2] = {false, false};
            uint32_t honest_sent_at[2] = {0, 0};
            m.run(7000 + 2 * link, false, [&](uint32_t now) {
                if (now == 100) {
                    for (int i = 0; i < 70; ++i) m.client_ends[2]->send(encode(CommandMsg{order(2, static_cast<int16_t>(10 + i % 5))}));   // a scripted opening (seventy orders)
                }
                if (now == 4900 - link) m.client_ends[2]->send(encode(CommandMsg{order(2, 30)}));          // timed to arrive at 4,900 ms, 100 ms before the first seal (on any link)
                if (now == 5100) m.client_ends[2]->send(encode(CommandMsg{order(2, 31)}));       // after the first seal: an order like any other
                for (size_t p = 0; p < 2; ++p) {
                    if (!honest_sent[p] && m.sims[p]->current_tick() >= 1) {                 // an honest client's earliest order: the frame after its dialog is gone (its first turn has run)
                        honest_sent[p] = m.clients[p]->submit(order(static_cast<uint8_t>(p), 20));
                        honest_sent_at[p] = now;
                    }
                }
            });
            ASSERT_TRUE(honest_sent[0] && honest_sent[1]);
            ASSERT_TRUE(turns_with(*m.host, 2) == std::vector<uint32_t>{turn_for_arrival(5100 + link, kMatchStartDelayMs)});   // only the order after the first seal, in the first turn that follows its arrival
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                const std::vector<uint32_t> turns = turns_with(*m.host, seat);
                ASSERT_TRUE(turns.size() == 1);
                ASSERT_EQ(turns[0], turn_for_arrival(honest_sent_at[seat] + link, kMatchStartDelayMs));      // not held back: the turn after its arrival
            }
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{2}}) ASSERT_TRUE(m.host->client_present(seat) && m.host->violations(seat) == 0u);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 17: the game mode
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol17_tests() {
    TEST_CASE("N2.112 Protocol 17, The Game Mode: The Start, The Room And The Plan End With One Byte, 0 (The Original's Highest Score) Or 1 (187); Every Byte Above sim::kLastGameMode Is Refused In Each Of Them (Never Played As Another Mode), So Are The Layouts Of Protocol 16 (No Byte), A Byte More And Every Truncation; The Mode Is Not Part Of The Seating Hash; The Words Of The Modes Parse Exactly") {
        ASSERT_TRUE(kProtocolVersion == 21 && sim::kLastGameMode == 1 && sim::valid_game_mode(0) && sim::valid_game_mode(1) && !sim::valid_game_mode(2) && !sim::valid_game_mode(255));
        ASSERT_TRUE(static_cast<uint8_t>(sim::GameMode::HighestScore) == 0 && static_cast<uint8_t>(sim::GameMode::Kills187) == 1);
        // the Start: the last byte, after the platforms of the four seats
        {
            StartMsg start;
            start.seed = 77;
            start.map_name = "TINY.LVL";
            start.map_hash = 0x0102030405060708ull;
            start.roster = 0x07;
            start.names = {"Ann", "Bob", "Cy", ""};
            start.platforms = {static_cast<uint8_t>(kOsMacos), 0, 0, 0};
            ASSERT_EQ(start.mode, 0);
            const std::vector<uint8_t> original = encode(start);
            ASSERT_EQ(original.back(), 0);
            start.mode = 1;
            const std::vector<uint8_t> bytes = encode(start);
            ASSERT_TRUE(bytes.size() == original.size() && bytes.back() == 1 && std::equal(bytes.begin(), bytes.end() - 1, original.begin()));        // (the mode is the one byte that differs)
            StartMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.mode == 1 && back.game_mode() == sim::GameMode::Kills187 && back.seed == 77 && back.roster == 0x07 && back.platforms == start.platforms && encode(back) == bytes);
            ASSERT_TRUE(decode(original, back) && back.mode == 0 && back.game_mode() == sim::GameMode::HighestScore && encode(back) == original);
            for (unsigned value = 0; value < 256; ++value) {                                         // every byte in the place of the mode
                std::vector<uint8_t> one = bytes;
                one.back() = static_cast<uint8_t>(value);
                StartMsg got;
                ASSERT_EQ(decode(one, got), value <= sim::kLastGameMode);
                if (value <= sim::kLastGameMode) ASSERT_TRUE(got.mode == value && encode(got) == one);
            }
            StartMsg keep;
            keep.mode = 1;
            std::vector<uint8_t> two = bytes;
            two.back() = 2;
            ASSERT_FALSE(decode(two, keep));
            ASSERT_EQ(keep.mode, 1);                                                                 // (a refused message leaves its target alone)
            std::vector<uint8_t> v16(bytes.begin(), bytes.end() - 1);                               // protocol 16's Start: no mode byte
            ASSERT_FALSE(decode(v16, back));
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            for (size_t cut = 0; cut < bytes.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
        }
        // the Room: the last byte, after the games; in a room that is no lobby room too (a server's room made with the mode)
        {
            RoomMsg plain = server_room_of(0);
            ASSERT_EQ(plain.mode, 0);
            const std::vector<uint8_t> original = encode(plain);
            plain.mode = 1;
            const std::vector<uint8_t> bytes = encode(plain);
            ASSERT_TRUE(bytes.size() == original.size() && bytes.back() == 1 && original.back() == 0 && std::equal(bytes.begin(), bytes.end() - 1, original.begin()));
            RoomMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.mode == 1 && !back.lobby() && encode(back) == bytes);
            ASSERT_TRUE(decode(original, back) && back.mode == 0 && encode(back) == original);
            RoomMsg lobby = server_room_of(0);
            lobby.flags = static_cast<uint8_t>(kRoomLeaderStarts | kRoomLobby);
            lobby.plan = {PlanKind::Open, PlanKind::Hard, PlanKind::Open, PlanKind::Nobody};
            lobby.in_game = 0x03;
            lobby.mode = 1;
            ASSERT_TRUE(decode(encode(lobby), back) && back.mode == 1 && back.lobby() && back.plan == lobby.plan && back.in_game == 3);
            for (const std::vector<uint8_t>& base : {bytes, encode(lobby)}) {
                for (unsigned value = 0; value < 256; ++value) {
                    std::vector<uint8_t> one = base;
                    one.back() = static_cast<uint8_t>(value);
                    RoomMsg got;
                    ASSERT_EQ(decode(one, got), value <= sim::kLastGameMode);
                    if (value <= sim::kLastGameMode) ASSERT_TRUE(got.mode == value && encode(got) == one);
                }
                std::vector<uint8_t> v16(base.begin(), base.end() - 1);                              // protocol 16's Room: no mode byte
                ASSERT_FALSE(decode(v16, back));
                std::vector<uint8_t> longer = base;
                longer.push_back(0);
                ASSERT_FALSE(decode(longer, back));
                for (size_t cut = 0; cut < base.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(cut)), back));
            }
            RoomMsg other = plain;                                                                   // who sits where is the seating hash: the mode is the room's, not a seat's
            other.mode = 0;
            ASSERT_EQ(seating_hash(plain), seating_hash(other));
        }
        // the Plan: the last byte, after the two team bytes
        {
            PlanMsg plan;
            plan.map_name = "TINY.LVL";
            plan.plan = {PlanKind::Open, PlanKind::Easy, PlanKind::Open, PlanKind::Nobody};
            plan.set_teams(sim::StartTeams{true, 0, 1});
            ASSERT_EQ(plan.mode, 0);
            const std::vector<uint8_t> original = encode(plan);
            plan.mode = 1;
            const std::vector<uint8_t> bytes = encode(plan);
            ASSERT_TRUE(bytes.size() == original.size() && bytes.back() == 1 && original.back() == 0 && std::equal(bytes.begin(), bytes.end() - 1, original.begin()));
            PlanMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.mode == 1 && back.map_name == "TINY.LVL" && back.plan == plan.plan && back.team_a == 0 && back.team_b == 1 && encode(back) == bytes);
            ASSERT_TRUE(decode(original, back) && back.mode == 0 && encode(back) == original);
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> one = bytes;
                one.back() = static_cast<uint8_t>(value);
                PlanMsg got;
                ASSERT_EQ(decode(one, got), value <= sim::kLastGameMode);
                if (value <= sim::kLastGameMode) ASSERT_TRUE(got.mode == value && encode(got) == one);
            }
            PlanMsg keep;
            keep.map_name = "keep";
            keep.mode = 1;
            std::vector<uint8_t> two = bytes;
            two.back() = 2;
            ASSERT_FALSE(decode(two, keep));
            ASSERT_TRUE(keep.map_name == "keep" && keep.mode == 1);
            std::vector<uint8_t> v16(bytes.begin(), bytes.end() - 1);                               // protocol 16's Plan: no mode byte
            ASSERT_FALSE(decode(v16, back));
            std::vector<uint8_t> longer = bytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, back));
            for (size_t cut = 0; cut < bytes.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
        }
        // the words of the modes (the control interface, the replay list, the command line): exactly these, in this case, nothing around them
        {
            uint8_t mode = 9;
            ASSERT_TRUE(parse_game_mode_name("highest-score", mode) && mode == 0);
            ASSERT_TRUE(parse_game_mode_name("187", mode) && mode == 1);
            mode = 9;
            for (const char* bad : {"", "Highest-Score", "highest_score", "highest-score ", " 187", "0", "1", "188", "1870", "187.0", "kills", "187\n"}) {
                ASSERT_FALSE(parse_game_mode_name(bad, mode));
                ASSERT_EQ(mode, 9);                                                                  // (a refused word leaves the target alone)
            }
            for (uint8_t m = 0; m <= sim::kLastGameMode; ++m) {                                      // the names the engine gives are the names that parse
                uint8_t back = 9;
                ASSERT_TRUE(parse_game_mode_name(sim::game_mode_name(static_cast<sim::GameMode>(m)), back) && back == m);
            }
        }
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records, the session's part (docs/NETWORK_PORT.md "Restart records"): the attendance of a match that was brought back after a restart of the server, the hooks that the record
// is written from, and a host that is restored from the turns of its predecessor
// ---------------------------------------------------------------------------------------------------------------------------------

// The start message of a HoldMatch (what a machine that starts from nothing is sent first)
StartMsg start_of_hold(const HoldMatch& m) {
    StartMsg start;
    start.seed = m.seed;
    start.map_name = "TEST.LVL";
    start.roster = static_cast<uint8_t>((1u << m.seats) - 1u);
    for (uint8_t p = 0; p < m.seats; ++p) start.names[p] = "Seat " + std::to_string(p);
    return start;
}

void run_restart_tests() {
    TEST_CASE("N2.96 Attendance After A Restart Of The Server: Every Seat Of A Person Is Absent (Excused) From The Moment The Match Is Restored, The Seats That Were Dropped Stay Dropped; The Vote Opens After The Restart's Wait (90 s), Not A Lost Link's 30 s, And Nobody Can Win It While Nobody Is There; An Excused Absence Adds Nothing To The Away Time And Is No Loss For The Flapping Rule; The Pause Counts Toward The Cap And The Countdown Follows It; The Same Decisions From Four Clocks") {
        const uint32_t origins[] = {0u, 123456u, 0x7FFFF000u, 0xFFFFF000u};              // (the clock wraps after 49.7 days: a restart's clock starts at 0, a long-lived server's is anywhere)
        for (const uint32_t t0 : origins) {
            Attendance::Config c;
            c.vote_after_ms = 30000;
            c.restart_vote_after_ms = 90000;
            c.max_pause_ms = 400000;
            c.resume_countdown_ms = 10000;
            Attendance a(c);
            a.seat_restored(0x0F, 0x08, t0);                                              // four persons, seat 3 had been dropped before the restart
            ASSERT_TRUE(a.state(0) == Attendance::State::Absent && a.state(1) == Attendance::State::Absent && a.state(2) == Attendance::State::Absent);
            ASSERT_EQ(a.state(3), Attendance::State::Dropped);
            ASSERT_TRUE(a.paused() && !a.counting_down(t0));
            ASSERT_TRUE(a.drops_by_vote() == 0 && a.drops_by_cap() == 0 && a.rejoins() == 0 && a.rejoins_refused() == 0);          // (a seat dropped before the restart is no drop of this run)
            for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_FALSE(a.flapping(seat, t0 + 1000));                                       // (no seat has lost its link: the restart is nobody's loss)
            // the vote: a restart's wait, not a lost link's
            ASSERT_EQ(a.vote_subject(t0 + 30000), 255);
            ASSERT_EQ(a.vote_subject(t0 + 89999), 255);
            ASSERT_EQ(a.vote_subject(t0 + 90000), 0);                                     // (of a tie the lowest seat)
            ASSERT_TRUE(a.update(t0 + 95000).empty());                                    // nobody is there to vote: more than half of nobody is not a majority
            ASSERT_EQ(a.connected_humans(0), 0);
            // a control: a seat whose link is LOST is put to the vote at 30 s
            Attendance control(c);
            control.seat_humans(0x07, t0);
            ASSERT_TRUE(control.lost(0, t0 + 1000));
            ASSERT_EQ(control.vote_subject(t0 + 1000 + 29999), 255);
            ASSERT_EQ(control.vote_subject(t0 + 1000 + 30000), 0);
            // the pause counts as any pause does, and the presence tells it
            const PresenceMsg p = a.presence_for(255, t0 + 30000);
            ASSERT_EQ(p.missing.size(), size_t{3});
            ASSERT_TRUE(p.missing[0].seat == 0 && p.missing[1].seat == 1 && p.missing[2].seat == 2 && p.missing[0].waited_s == 30 && p.missing[0].state == PresenceMsg::State::Absent);
            ASSERT_TRUE(p.vote_seat == 255 && p.voters == 0 && p.cap_s == 370 && p.resume_s == 0);
            PresenceMsg decoded;
            ASSERT_TRUE(decode(encode(p), decoded) && decoded.missing.size() == 3 && decoded.cap_s == 370);                           // (the message is one that the decoder accepts)
            ASSERT_EQ(a.pause_ms(t0 + 30000), 30000u);
            // a seat comes back: its absence adds nothing to its away time, and a loss that follows is judged as the seat's first (30 s, no flapping)
            ASSERT_TRUE(a.returning(0, t0 + 20000));
            ASSERT_EQ(a.state(0), Attendance::State::CatchingUp);
            ASSERT_TRUE(a.caught_up(0, t0 + 22000));
            ASSERT_EQ(a.state(0), Attendance::State::Present);
            ASSERT_EQ(a.away_ms(0, t0 + 25000), 0u);                                      // (an absence of 22 s that the seat was not to blame for)
            ASSERT_TRUE(a.paused());                                                      // (seats 1 and 2 are still away)
            ASSERT_TRUE(a.lost(0, t0 + 40000));
            ASSERT_FALSE(a.flapping(0, t0 + 40000));
            ASSERT_EQ(a.vote_subject(t0 + 69999), 255);                                   // 30 s after ITS loss, not 30 s of total away time (and the others wait for their 90 s)
            ASSERT_EQ(a.vote_subject(t0 + 70000), 0);
            ASSERT_EQ(a.away_ms(0, t0 + 70000), 30000u);
            ASSERT_EQ(a.vote_subject(t0 + 90000), 1);                                     // (at 90 s the restart's seats are due too, and they have been away longer than seat 0's 50 s: the longest total is the subject, of a tie the lowest seat)
            // the flapping rule: the restart is no loss, so three real losses are needed
            Attendance f(c);
            f.seat_restored(0x03, 0, t0);
            ASSERT_TRUE(f.returning(0, t0 + 5000) && f.caught_up(0, t0 + 6000));
            ASSERT_TRUE(f.lost(0, t0 + 10000));
            ASSERT_TRUE(f.returning(0, t0 + 11000) && f.caught_up(0, t0 + 12000));
            ASSERT_TRUE(f.lost(0, t0 + 14000));
            ASSERT_FALSE(f.flapping(0, t0 + 14500));                                      // two losses: if the restart counted as one this would be the third
            ASSERT_TRUE(f.returning(0, t0 + 15000) && f.caught_up(0, t0 + 16000));
            ASSERT_TRUE(f.lost(0, t0 + 18000));
            ASSERT_TRUE(f.flapping(0, t0 + 18500));                                       // the third real loss within a minute
            // the cap: the restart's pause counts toward the match's total pause, and at the cap every seat that is not present is dropped
            Attendance k(c);
            k.seat_restored(0x07, 0, t0);
            ASSERT_TRUE(k.update(t0 + 399999).empty());
            ASSERT_EQ(k.cap_left_ms(t0 + 399999), 1u);
            const std::vector<uint8_t> dropped = k.update(t0 + 400000);
            ASSERT_EQ(dropped.size(), size_t{3});
            ASSERT_TRUE(k.drops_by_cap() == 3 && k.drops_by_vote() == 0 && !k.paused());
            ASSERT_TRUE(k.state(0) == Attendance::State::Dropped && k.state(1) == Attendance::State::Dropped && k.state(2) == Attendance::State::Dropped);
            // the countdown that follows a pause of 3 s or more: when the last seat is back
            Attendance r(c);
            r.seat_restored(0x03, 0, t0);
            ASSERT_TRUE(r.returning(0, t0 + 5000) && r.caught_up(0, t0 + 6000));
            ASSERT_TRUE(r.paused());
            ASSERT_TRUE(r.returning(1, t0 + 7000) && r.caught_up(1, t0 + 8000));
            ASSERT_FALSE(r.paused());
            ASSERT_TRUE(r.counting_down(t0 + 8000) && r.resume_s(t0 + 8000) == 10);
            ASSERT_FALSE(r.counting_down(t0 + 18000));
            // a seat that was dropped before the restart cannot come back: its key is told "dropped", nothing is counted against it
            ASSERT_FALSE(a.returning(3, t0 + 1000));
            ASSERT_EQ(a.rejoins_refused(), 0u);
            ASSERT_FALSE(a.dropped(3, t0 + 2000));
            // nothing to hold: no persons, or only seats that were dropped: nobody is waited for
            Attendance none(c);
            none.seat_restored(0, 0, t0);
            ASSERT_FALSE(none.paused());
            Attendance gone(c);
            gone.seat_restored(0x03, 0x03, t0);
            ASSERT_FALSE(gone.paused());
            ASSERT_TRUE(gone.state(0) == Attendance::State::Dropped && gone.state(1) == Attendance::State::Dropped && gone.update(t0 + 1000000).empty());
        }
        // the restart's wait is a setting like the others: shorter than a lost link's it is still what it says (the room never sets less than its own: Room::build_session)
        Attendance::Config quick;
        quick.vote_after_ms = 30000;
        quick.restart_vote_after_ms = 5000;
        Attendance q(quick);
        q.seat_restored(0x03, 0, 1000);
        ASSERT_TRUE(q.vote_subject(5999) == 255 && q.vote_subject(6000) == 0);
        ASSERT_EQ(kRestartVoteAfterMs, 90000u);                                            // the default: 90 s
    } TEST_END();

    TEST_CASE("N2.97 A Host Restored From The Sealed Turns Of Its Predecessor: The Restored Engine Has The State Hash The Uninterrupted One Had At The Restored Tick And At Every Later Tick When The Same Turns Follow; The Log Holds The Same Bytes; Every Seat Is Absent And The Match Is Paused; The Three Machines (Still In Memory, Their Links Gone) Come Back Through The Door With Their Keys, Are Compared With The Referee At The Restored Tick And The Match Goes On To The Same State Everywhere") {
        HoldMatch m(3);
        std::vector<TurnMsg> turns;
        std::vector<std::pair<uint32_t, uint64_t>> checks;                              // the referee's own hash after every 20th turn, as the record would hold it
        std::map<uint64_t, uint64_t> tick_hash;                                         // the referee's state after every tick of the uninterrupted match
        m.host->set_on_seal([&](const TurnMsg& t) { turns.push_back(t); });
        m.host->set_on_referee_hash([&](uint32_t turn, const sim::StateHash& h) { checks.emplace_back(turn, h.total); });
        m.host->runner().set_on_tick([&]() { tick_hash[m.referee.current_tick()] = m.referee.state_hash().total; });
        m.run(45000);                                                                   // 45 s of play: 900 turns, every kind of command (the script)
        ASSERT_TRUE(turns.size() >= 880);
        for (size_t i = 0; i < turns.size(); ++i) ASSERT_EQ(turns[i].turn, static_cast<uint32_t>(i));       // (the hook saw every turn once, in order)
        ASSERT_TRUE(checks.size() >= 43);
        for (size_t i = 0; i < checks.size(); ++i) ASSERT_EQ(checks[i].first, static_cast<uint32_t>(i * kHashEveryTurns + kHashEveryTurns - 1));     // (turn 19, 39, 59, ...)
        std::vector<uint8_t> original_log;
        for (uint32_t at = 0; at < m.host->log().turns();) {
            const uint32_t got = m.host->log().read(at, 4096, 1u << 30, original_log);
            ASSERT_TRUE(got > 0);
            at += got;
        }
        ASSERT_EQ(m.host->log().turns(), static_cast<uint32_t>(turns.size()));
        const uint64_t executed = m.referee.current_tick();                             // the referee runs a turn or two behind the sealing
        ASSERT_TRUE(executed >= turns.size() - 4 && executed <= turns.size());
        ASSERT_TRUE(tick_hash.count(executed) == 1);
        // ---- the restored engine: the first `executed` turns, then one at a time ----------------------------------------------------------------------------------
        {
            sim::SimulationEngine b;
            build_world(b, m.seed, m.match_ms);
            HostSession hb(b, m.host_cfg);
            hb.set_seat_keys(m.keys);
            hb.set_rejoin_start(start_of_hold(m));
            for (uint64_t i = 0; i < executed; ++i) ASSERT_TRUE(hb.restore_turn(turns[static_cast<size_t>(i)]));
            ASSERT_EQ(hb.restored_turns(), static_cast<uint32_t>(executed));
            ASSERT_EQ(hb.runner().fast_forward(static_cast<uint32_t>(hb.runner().queued())), static_cast<uint32_t>(executed));
            ASSERT_TRUE(hb.runner().at_boundary() && hb.runner().queued() == 0 && hb.runner().next_turn_to_execute() == static_cast<uint32_t>(executed));
            ASSERT_EQ(b.current_tick(), executed);
            ASSERT_EQ(b.state_hash().total, tick_hash[executed]);                       // THE restored tick: the same state, hash for hash
            for (uint64_t i = executed; i < turns.size(); ++i) {                        // and every tick after it, when the same turns follow
                ASSERT_TRUE(hb.restore_turn(turns[static_cast<size_t>(i)]));
                ASSERT_EQ(hb.runner().fast_forward(1), 1u);
                const uint64_t tick = b.current_tick();
                ASSERT_EQ(tick, i + 1);
                if (tick_hash.count(tick) == 1) ASSERT_EQ(b.state_hash().total, tick_hash[tick]);
            }
            // the log is the match: the same bytes in the same order
            std::vector<uint8_t> restored_log;
            for (uint32_t at = 0; at < hb.log().turns();) {
                const uint32_t got = hb.log().read(at, 4096, 1u << 30, restored_log);
                ASSERT_TRUE(got > 0);
                at += got;
            }
            ASSERT_EQ(hb.log().turns(), m.host->log().turns());
            ASSERT_TRUE(restored_log == original_log);
            // the replay of the first run's checkpoints agrees as well (the turns of 20 are a boundary of the replay: the room verifies exactly this)
            {
                sim::SimulationEngine c;
                build_world(c, m.seed, m.match_ms);
                HostSession hc(c, m.host_cfg);
                size_t next_check = 0;
                for (const TurnMsg& t : turns) {
                    ASSERT_TRUE(hc.restore_turn(t));
                    if ((t.turn + 1) % kHashEveryTurns != 0) continue;
                    hc.runner().fast_forward(static_cast<uint32_t>(hc.runner().queued()));
                    ASSERT_TRUE(next_check < checks.size() && checks[next_check].first == t.turn);
                    ASSERT_EQ(c.state_hash().total, checks[next_check].second);
                    ++next_check;
                }
            }
            // begins the match: nothing is sealed, every seat is held
            hb.start_restored(5000, 0x07, 0x00);
            ASSERT_TRUE(hb.paused());
            ASSERT_EQ(hb.turns_sealed(), static_cast<uint32_t>(turns.size()));
            for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(hb.attendance().state(seat) == Attendance::State::Absent && hb.seat_held(seat) && !hb.client_present(seat));
            hb.update(6000);
            hb.update(60000);
            ASSERT_EQ(hb.turns_sealed(), static_cast<uint32_t>(turns.size()));          // paused: nothing is sealed, however long it waits
            ASSERT_TRUE(hb.desyncs().empty());
        }
        // ---- the machines come back: the same rig, its host replaced by one that was restored ---------------------------------------------------------------------
        for (uint8_t seat = 0; seat < 3; ++seat) {
            m.auto_reconnect[seat] = false;
            m.cut(seat);                                                                // the old server is gone: every link is cut
        }
        m.host.reset();
        const StartMsg start = start_of_hold(m);
        build_world(m.referee, m.seed, m.match_ms);                                     // a fresh engine, the same object
        m.host = std::make_unique<HostSession>(m.referee, m.host_cfg);
        m.host->set_seat_keys(m.keys);
        m.host->set_rejoin_start(start);
        for (size_t i = 0; i < turns.size(); ++i) {
            ASSERT_TRUE(m.host->restore_turn(turns[i]));
            if ((i + 1) % kHashEveryTurns == 0) m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
        }
        m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
        m.host->start_restored(m.now, 0x07, 0x00);
        const uint32_t sealed_at_restore = m.sealed();
        ASSERT_EQ(sealed_at_restore, static_cast<uint32_t>(turns.size()));
        m.run(2000, false);
        ASSERT_TRUE(m.host->paused() && m.sealed() == sealed_at_restore);               // the machines have no link yet (they are told to wait)
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_EQ(static_cast<int>(m.clients[seat]->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
        for (uint8_t seat = 0; seat < 3; ++seat) m.auto_reconnect[seat] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));               // all three said Hello with their keys, were given what they lacked and agreed with the referee at the restored tick
        ASSERT_EQ(m.host->attendance().rejoins(), 3u);
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(m.host->client_present(seat) && m.host->attendance().state(seat) == Attendance::State::Present);
        m.run(15000);
        ASSERT_TRUE(m.sealed() > sealed_at_restore + 200);                              // the match goes on (the schedule did not burst)
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced() || c->lost());
        ASSERT_TRUE(m.all_equal());                                                     // the restored referee and the three machines: one state
        ASSERT_EQ(m.host->attendance().drops_by_cap() + m.host->attendance().drops_by_vote(), 0u);
    } TEST_END();

    TEST_CASE("N2.98 The Hooks That The Record Is Written From: set_on_seal Runs For Every Turn, In Order, Once, After The Turn Is In The Log And BEFORE Any Client Has Been Sent It (A Process That Dies At Any Moment Leaves No Client With A Turn That The Record Lacks); It Runs For A Host That Does Not Hold Seats Too; set_on_referee_hash Runs For Turns 19, 39, ... With The Referee's Own State After That Turn, And Only For A Host Without A Seat") {
        {   // a host that holds seats: the log has the turn, the clients have not been sent it yet
            HoldMatch m(3);
            std::vector<uint32_t> seen;
            uint32_t late_in_log = 0;
            uint32_t early_sends = 0;
            m.host->set_on_seal([&](const TurnMsg& t) {
                seen.push_back(t.turn);
                if (m.host->log().turns() != t.turn + 1) ++late_in_log;                  // the log has this turn already (the hook is called after the append) ...
                for (uint8_t p = 0; p < 3; ++p) {
                    if (m.taps[p] && m.taps[p]->sent_of(MsgType::Turn) > t.turn) ++early_sends;      // ... and the turn has not been sent to anybody (the taps count the Turn messages that they were given)
                }
            });
            std::map<uint64_t, uint64_t> tick_hash;
            std::vector<std::pair<uint32_t, uint64_t>> hashes;
            m.host->runner().set_on_tick([&]() { tick_hash[m.referee.current_tick()] = m.referee.state_hash().total; });
            m.host->set_on_referee_hash([&](uint32_t turn, const sim::StateHash& h) { hashes.emplace_back(turn, h.total); });
            m.run(30000);
            ASSERT_TRUE(seen.size() >= 580);
            for (size_t i = 0; i < seen.size(); ++i) ASSERT_EQ(seen[i], static_cast<uint32_t>(i));
            ASSERT_EQ(seen.size(), static_cast<size_t>(m.sealed()));                     // once for every sealed turn
            ASSERT_EQ(late_in_log, 0u);
            ASSERT_EQ(early_sends, 0u);
            ASSERT_TRUE(m.taps[0]->sent_of(MsgType::Turn) >= 580);                       // (the clients were sent the turns, of course: after the hook)
            ASSERT_TRUE(hashes.size() >= 28);
            for (size_t i = 0; i < hashes.size(); ++i) {
                ASSERT_EQ(hashes[i].first, static_cast<uint32_t>(i * kHashEveryTurns + kHashEveryTurns - 1));
                ASSERT_TRUE(tick_hash.count(hashes[i].first + 1) == 1);
                ASSERT_EQ(hashes[i].second, tick_hash[hashes[i].first + 1]);             // the state after that turn: the hash that the runner took when it ran it, not the engine's state at the time of the call
            }
        }
        {   // a host that does not hold seats: the seal hook still runs (the record is the room's business, not the log's), and it still has no log
            HoldOptions o;
            o.hold = false;
            HoldMatch m(3, o);
            uint32_t count = 0;
            m.host->set_on_seal([&](const TurnMsg& t) { count += t.turn == count ? 1u : 0u; });
            m.run(4000);
            ASSERT_TRUE(count >= 75 && count == m.sealed());
            ASSERT_EQ(m.host->log().turns(), 0u);
        }
        {   // a host with a seat reports its hashes through the sequencer like everybody and has no referee hook
            Match m(1, 3, {30, 10});
            uint32_t sealed = 0;
            uint32_t referee_calls = 0;
            m.host->set_on_seal([&](const TurnMsg& t) { sealed += t.turn == sealed ? 1u : 0u; });
            m.host->set_on_referee_hash([&](uint32_t, const sim::StateHash&) { ++referee_calls; });
            m.run(6000);
            ASSERT_TRUE(sealed >= 115 && sealed == m.host->turns_sealed());
            ASSERT_EQ(referee_calls, 0u);
        }
    } TEST_END();

    TEST_CASE("N2.99 What A Restored Host Refuses: A Session That Holds No Seats, A Turn That Is Not The Next, A Turn After start_restored, A Log That Cannot Hold The Match (Its Own Limit Or The Server's Budget); A Seat Without A Key Is Dropped At The Restart (Nobody Could Come Back To It) And Its Drop Is Sealed After The Pause; A Match Restored With No Turns (The Seconds Before Its First Turn) Waits For Its Players And Begins At Turn 0 Without Any Command Of The Pre-Start")  {
        {
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = false;
            HostSession h(e, hc);
            ASSERT_FALSE(h.restore_turn(TurnMsg{}));                                      // a room that holds no seats has no log to hold the match in
        }
        {
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            HostSession h(e, hc);
            std::array<SeatKey, sim::MAX_PLAYERS> keys{};
            keys[0] = key_with(1);
            keys[1] = key_with(2);
            h.set_seat_keys(keys);
            TurnMsg t0;
            t0.turn = 0;
            TurnMsg t1;
            t1.turn = 1;
            ASSERT_FALSE(h.restore_turn(t1));                                             // not the next one
            ASSERT_TRUE(h.restore_turn(t0));
            ASSERT_FALSE(h.restore_turn(t0));                                             // a repeat
            ASSERT_TRUE(h.restore_turn(t1));
            ASSERT_EQ(h.restored_turns(), 2u);
            h.runner().fast_forward(2);
            h.start_restored(1000, 0x03, 0x00);
            TurnMsg t2;
            t2.turn = 2;
            ASSERT_FALSE(h.restore_turn(t2));                                             // the match has begun
            h.start_restored(2000, 0x03, 0x00);                                           // (a second start changes nothing)
            ASSERT_TRUE(h.attendance().state(0) == Attendance::State::Absent && h.turns_sealed() == 2u);
        }
        {   // the log's own limit (its first allocation is 1.5 KB: a limit of 1 KB holds a few empty turns only) and the server's budget
            sim::SimulationEngine e;
            build_world(e, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            hc.max_log_bytes = 1024;
            HostSession h(e, hc);
            uint32_t taken = 0;
            for (uint32_t i = 0; i < 400; ++i) {
                TurnMsg t;
                t.turn = i;
                if (!h.restore_turn(t)) break;
                ++taken;
            }
            ASSERT_TRUE(taken > 100 && taken < 400);                                      // refused when the log is full: the match does not fit
            LogBudget budget(1000);                                                       // a budget of less than the first allocation keeps no log at all
            sim::SimulationEngine e2;
            build_world(e2, 1);
            hc.max_log_bytes = TurnLog::kDefaultMaxBytes;
            hc.log_budget = &budget;
            HostSession h2(e2, hc);
            ASSERT_FALSE(h2.restore_turn(TurnMsg{}));
            ASSERT_EQ(budget.used(), 0u);
        }
        {   // a seat without a key (the maker failed for it): nobody can come back to it, so it is dropped, at the restart, and its Drop is sealed in the first turn after the pause
            HoldMatch m(3);
            std::vector<TurnMsg> turns;
            m.host->set_on_seal([&](const TurnMsg& t) { turns.push_back(t); });
            m.run(8000);
            for (uint8_t seat = 0; seat < 3; ++seat) {
                m.auto_reconnect[seat] = false;
                m.cut(seat);
            }
            m.host.reset();
            build_world(m.referee, m.seed, m.match_ms);
            m.host = std::make_unique<HostSession>(m.referee, m.host_cfg);
            std::array<SeatKey, sim::MAX_PLAYERS> keys = m.keys;
            keys[1] = SeatKey{};                                                          // seat 1 never got a key
            m.host->set_seat_keys(keys);
            m.host->set_rejoin_start(start_of_hold(m));
            for (const TurnMsg& t : turns) ASSERT_TRUE(m.host->restore_turn(t));
            m.host->runner().fast_forward(static_cast<uint32_t>(m.host->runner().queued()));
            m.host->start_restored(m.now, 0x07, 0x00);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);         // (nobody could come back to it)
            ASSERT_TRUE(m.host->attendance().state(0) == Attendance::State::Absent && m.host->attendance().state(2) == Attendance::State::Absent);
            ASSERT_TRUE(m.host->paused());
            for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) m.auto_reconnect[seat] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.referee.is_player_dropped(1) && !m.referee.is_player_dropped(0) && !m.referee.is_player_dropped(2));       // its Drop was sealed after the pause, on every machine
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[2]->is_player_dropped(1));
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
        {   // no turns at all: the match had begun and the server stopped within the seconds before its first turn (a restored match waits for its players and begins at turn 0)
            HoldOptions o;
            o.host.start_delay_ms = kMatchStartDelayMs;
            HoldMatch m(3, o);
            m.run(2500, false);                                                           // 2.5 s into the 5 s of the dialog: nothing is sealed yet, nobody has given a command that counts
            ASSERT_EQ(m.sealed(), 0u);
            for (uint8_t seat = 0; seat < 3; ++seat) {
                m.auto_reconnect[seat] = false;
                m.cut(seat);
            }
            m.host.reset();
            build_world(m.referee, m.seed, m.match_ms);
            HostSession::Config hc = m.host_cfg;
            m.host = std::make_unique<HostSession>(m.referee, hc);
            m.host->set_seat_keys(m.keys);
            m.host->set_rejoin_start(start_of_hold(m));
            m.host->start_restored(m.now, 0x07, 0x00);                                    // (no restore_turn: the record held none)
            ASSERT_TRUE(m.host->paused() && m.host->turns_sealed() == 0u);
            m.run(3000, false);
            ASSERT_EQ(m.sealed(), 0u);
            for (uint8_t seat = 0; seat < 3; ++seat) m.auto_reconnect[seat] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 20000));              // every machine says Hello with its key (it has executed no turn: have_turns 0 is its count, it is given the match from nothing)
            m.run(4000, false);
            ASSERT_TRUE(m.sealed() > 0u);                                                 // the first turn is sealed when the pause ends, as it is after any pause
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();
}
