// Tests of the lock-step network core: the fixes of the review of release A, protocols 13, 15 and 16
#include "lockstep_fixtures.hpp"

void run_reconnect_session_tests_2() {
    // ---- the fixes of the review of release A, through whole matches (N2.80 - N2.87) --------------------------------------------------------------------------------------------

    TEST_CASE("N2.80 Hold: A Key Holder Cannot Freeze A Room: A Hello Every 12 s, Each Time With A Little Progress So That No Stall Is Ever Seen, Holds The Match For The Catch-Up Budget Of The Absence (Here 40 s) And No Longer; Then The Seat Is Absent, Its Hellos Are Refused And Counted, The Vote Opens, And The Cap (Here 120 s) Drops It; The Others Play On To The End Identical And The Key Says \"Dropped\"") {
        HoldOptions o;
        o.host.attendance.max_catch_up_ms = 40000;
        o.host.attendance.max_pause_ms = 120000;
        HoldMatch m(3, o);
        m.run(6000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        const uint32_t cut_at = m.now;
        m.run(200);
        ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
        const uint32_t sealed_at_pause = m.sealed();
        // the key holder: a raw client that says Hello for the seat on a new link every 12 s, and acknowledges a few more turns every 5 s
        std::vector<Connection*> links;
        uint32_t next_hello = m.now;
        uint32_t next_ack = m.now + 4000;
        uint32_t total = 0;
        uint32_t creeping = 0;
        const auto key_holder = [&](uint32_t now) {
            if (static_cast<int32_t>(now - next_hello) >= 0) {
                auto ends = m.open_link();
                HelloMsg h;
                h.name = "Eve";
                h.key = m.keys[2];
                h.have_turns = 1;
                ends.second->send(encode(h));
                links.push_back(ends.second);
                next_hello = now + 12000;
            }
            for (Connection* c : links) {
                std::vector<uint8_t> msg;
                while (c->poll(msg)) {
                    CatchUpMsg cu;
                    if (peek_type(msg) == MsgType::CatchUp && decode(msg, cu)) total = cu.total_turns;
                }
            }
            if (static_cast<int32_t>(now - next_ack) >= 0 && total > 100 && !links.empty() && links.back()->is_open()) {
                creeping = std::min(total, creeping + std::max<uint32_t>(total / 40, 2));
                AckMsg a;
                a.turn = creeping - 1;
                links.back()->send(encode(a));
                next_ack = now + 4000;
            }
        };
        m.run(cut_at + 39000 - m.now, true, key_holder);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);       // 39 s: it has been catching up for 39 s of the 40, and has shown progress all along
        ASSERT_TRUE(m.host->paused() && m.host->attendance().percent(2) > 5);
        ASSERT_EQ(m.host->attendance().catch_up_expired(), 0u);
        m.run(3000, true, key_holder);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);            // 42 s: the budget is used up, whatever progress it made: let go
        ASSERT_EQ(m.host->attendance().catch_up_expired(), 1u);
        ASSERT_EQ(m.host->attendance().rejoins(), 0u);
        ASSERT_EQ(m.host->attendance().rejoins_refused(), 0u);                          // (the four Hellos so far, at 0, 12, 24 and 36 s, were taken: they are within the twelve of a minute; the next one, at 48 s, finds the budget spent)
        ASSERT_TRUE(m.host->paused());
        m.run(2000, true, key_holder);
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);      // the others may vote on it: it is away (nothing keeps it catching up)
        ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].state == PresenceMsg::State::Absent);
        uint32_t resumed_after_ms = 0;
        m.run(100000, true, [&](uint32_t now) {
            key_holder(now);
            if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - cut_at;
            if (resumed_after_ms == 0) {
                ASSERT_TRUE(m.sealed() == sealed_at_pause);                             // nothing is sealed for all that time
            }
        });
        ASSERT_TRUE(resumed_after_ms >= 120000 && resumed_after_ms <= 120400);           // the cap, to the pass (a key holder held the room for ever before)
        ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 0u);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().rejoins(), 0u);
        ASSERT_TRUE(m.host->attendance().rejoins_refused() >= 6 && m.host->attendance().rejoins_refused() <= 8);       // the Hellos of 48 s to 108 s
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        m.run(3000);
        ASSERT_TRUE(m.sealed() > sealed_at_pause + 40);                                 // the others play on
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.sims[0]->is_player_dropped(2) && m.sims[1]->is_player_dropped(2) && m.referee.is_player_dropped(2));
        ASSERT_TRUE(m.all_equal());
        m.auto_reconnect[2] = true;                                                     // the seat's own machine comes by: its key says "dropped"
        m.run(5000);
        ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
    } TEST_END();

    TEST_CASE("N2.81 Hold: A Key Holder Cannot Make The Server Stream Its Log Again And Again: With A Log Of More Than A Megabyte, 100 Hellos In 10 s Make Three Streams At Most (The Fourth Hello Of A Minute Is Refused), And With The Hellos Allowed The Bytes Stop It At Three Times The Log's Size; Every Refusal Is Counted And Streams Nothing") {
        const auto big_match = [](const HoldOptions& o) {
            auto m = std::make_unique<HoldMatch>(3, o);
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            m->run(8000, false, [&](uint32_t) {                                       // two players put 16 commands of 32 ants into every 10 ms: a log that is bigger than a megabyte
                for (const uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                    for (int i = 0; i < 8; ++i) {
                        CommandMsg msg;
                        msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                        m->client_ends[p]->send(encode(msg));
                    }
                }
            });
            return m;
        };
        // the key holder: Hello on a new link every `every_ms`, `hellos` times; whatever it is sent it acknowledges at once, so that the window of the stream never closes
        const auto hammer = [](HoldMatch& m, uint32_t hellos, uint32_t every_ms) {
            std::vector<Connection*> links;
            uint32_t sent = 0;
            uint32_t next = m.now;
            m.run(hellos * every_ms + 4000, false, [&](uint32_t now) {
                if (sent < hellos && static_cast<int32_t>(now - next) >= 0) {
                    auto ends = m.open_link();
                    HelloMsg h;
                    h.name = "Eve";
                    h.key = m.keys[2];
                    h.have_turns = 1;
                    ends.second->send(encode(h));
                    links.push_back(ends.second);
                    ++sent;
                    next = now + every_ms;
                }
                for (Connection* c : links) {
                    std::vector<uint8_t> msg;
                    while (c->poll(msg)) {
                        TurnBatchMsg b;
                        if (peek_type(msg) == MsgType::TurnBatch && decode(msg, b) && !b.turns.empty()) {
                            AckMsg a;
                            a.turn = b.first_turn + static_cast<uint32_t>(b.turns.size()) - 1;
                            c->send(encode(a));
                        }
                    }
                }
            });
            uint64_t streamed = 0;
            for (const auto& audit : m.audits) streamed += audit->sent;
            return streamed;
        };
        {   // 100 Hellos in 10 s (a Hello every 100 ms): three are accepted, the rest are refused
            HoldOptions o;
            auto mp = big_match(o);
            HoldMatch& m = *mp;
            const size_t log_bytes = m.host->log().bytes();
            ASSERT_TRUE(log_bytes > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.audit_new_links = true;
            m.audit_limit = SIZE_MAX / 2;
            m.cut(2);
            m.run(300);
            ASSERT_TRUE(m.host->paused());
            const uint64_t streamed = hammer(m, 100, 100);
            ASSERT_TRUE(streamed <= 3 * uint64_t{log_bytes} + 65536);                  // at most three streams of the log (a hundred before: 150 MB in 10 s)
            ASSERT_TRUE(streamed >= uint64_t{log_bytes} * 9 / 10);                     // (the third was let to finish: its player did not say Hello again that was accepted)
            ASSERT_TRUE(m.host->attendance().streamed_bytes() <= 3 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().rejoins_refused() >= 90);                 // 97 Hellos refused, every one counted
            ASSERT_TRUE(m.refusals >= 90);                                             // (the door saw the refusals too)
            ASSERT_EQ(m.host->attendance().rejoins(), 0u);
            ASSERT_TRUE(m.host->rejoiners() <= 1);
            ASSERT_EQ(m.host->attendance().state(2) == Attendance::State::CatchingUp || m.host->attendance().state(2) == Attendance::State::Absent, true);
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));       // nobody else was disturbed
        }
        {   // the attempts raised to eight a minute and no floor: the bytes alone stop it, at the fourth stream (3 x the log's size streamed in ten minutes)
            HoldOptions o;
            o.host.attendance.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            o.host.attendance.stream_floor_bytes = 0;
            o.host.attendance.max_catch_up_ms = 3600000;
            o.host.attendance.max_pause_ms = 3600000;
            auto mp = big_match(o);
            HoldMatch& m = *mp;
            const size_t log_bytes = m.host->log().bytes();
            ASSERT_TRUE(log_bytes > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.audit_new_links = true;
            m.audit_limit = SIZE_MAX / 2;
            m.cut(2);
            m.run(300);
            const uint64_t streamed = hammer(m, 10, 2000);                              // ten Hellos, two seconds apart: each stream is finished before the next Hello comes
            ASSERT_TRUE(streamed <= 3 * uint64_t{log_bytes} + 65536);
            ASSERT_TRUE(streamed >= 2 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().streamed_bytes() <= 3 * uint64_t{log_bytes});
            ASSERT_TRUE(m.host->attendance().streamed_bytes() > 2 * uint64_t{log_bytes});
            ASSERT_EQ(m.host->attendance().rejoins_refused(), 7u);                      // the fourth to the tenth: the bytes (and not the Hellos a minute: ten in 20 s are within twelve)
        }
    } TEST_END();

    TEST_CASE("N2.82 Hold: A Short Pause Does Not Inflate The Others' Jitter Buffers: Three Cuts Of A Seat's Link (Each A Pause Of Under A Second, Links Of 5 ms) Leave The Buffers Of The Two Other Machines, And Of The One That Came Back, At One Turn For The Next 12 s; Nobody Is Told \"Waiting For The Other Players\"") {
        HoldOptions o;
        o.link.latency_ms = 5;
        o.link.jitter_ms = 2;
        HoldMatch m(3, o);
        m.run(8000);
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1 && m.clients[1]->runner().buffer_turns() == 1);
        for (int i = 0; i < 3; ++i) {
            const uint32_t sealed_before = m.sealed();
            m.cut(1);
            uint32_t paused_ms = 0;
            bool waiting = false;
            m.run(2000, true, [&](uint32_t) {
                if (m.host->paused()) paused_ms += 10;
                waiting = waiting || m.clients[0]->runner().stalled_ms() >= 1000 || m.clients[2]->runner().stalled_ms() >= 1000;
            });
            ASSERT_TRUE(paused_ms >= 30 && paused_ms <= 600);                           // a short pause (the review measured 130 - 210 ms over its links)
            ASSERT_TRUE(m.sealed() - sealed_before >= 25);                              // the match went on
            ASSERT_FALSE(waiting);
            ASSERT_EQ(m.clients[0]->runner().buffer_turns(), 1u);                       // (they used to be 2, then 4, and 150 ms of input delay for half a minute)
            ASSERT_EQ(m.clients[2]->runner().buffer_turns(), 1u);
            ASSERT_EQ(m.clients[1]->runner().buffer_turns(), 1u);
        }
        ASSERT_EQ(m.host->attendance().rejoins(), 3u);
        m.run(12000);
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1 && m.clients[1]->runner().buffer_turns() == 1);
        ASSERT_TRUE(m.clients[0]->runner().jitter().lateness_ms() <= 20 && m.clients[2]->runner().jitter().lateness_ms() <= 20);
        ASSERT_FALSE(m.clients[0]->runner().held() || m.clients[1]->runner().held() || m.clients[2]->runner().held());      // the Presence that ended the pause let go of the runners
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.83 Hold: A Connection That Flaps Cannot Slow The Match Down For Ever (Cut Every 300 ms): Three Losses In A Minute Put The Seat To The Vote At Once, Whatever Its State; Back And Present It Is Voted Out By The Two Others (Its Link Is Closed, The Drop Is The Same Tick Everywhere, The Machine Is Told \"Dropped\"); A Seat That Is Cut Again And Again Is Refused At Its Thirteenth Hello Of A Minute And The Vote And The Cap Take It; The Match Is At Full Speed Afterwards") {
        {   // three cuts, then the seat stays: it is back and Present, and the vote about it is open
            HoldMatch m(3);
            m.run(6000);
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 3000 && cuts < 3; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_EQ(cuts, 3u);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2); }, 6000));
            m.run(1500);
            ASSERT_EQ(m.host->attendance().rejoins(), 3u);
            ASSERT_TRUE(m.host->attendance().flapping(2, m.now));
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);       // the seat is back and plays
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                const PresenceMsg& p = m.clients[seat]->presence();
                ASSERT_TRUE(p.missing.empty() && p.vote_seat == 2 && p.voters == 2 && p.votes_continue == 0);      // the vote about a seat that is not missing: the others are asked
            }
            ASSERT_TRUE(m.clients[2]->presence().vote_seat == 2 && m.clients[2]->presence().voters == 2);           // (it is told too: the vote is about it, the two others vote)
            ASSERT_FALSE(m.host->paused());
            ASSERT_TRUE(m.clients[0]->vote(2, true));                                    // one of two is not more than half ...
            m.run(1500);
            ASSERT_TRUE(m.host->client_present(2) && m.clients[0]->presence().votes_continue == 1 && m.clients[0]->presence().your_vote == 2);
            ASSERT_TRUE(m.clients[2]->vote(2, true));                                    // ... and the seat does not vote on its own case: its message is ignored (it crossed nothing: no offence)
            m.run(1500);
            ASSERT_EQ(m.host->violations(2), 0u);
            ASSERT_EQ(m.clients[0]->presence().votes_continue, 1);
            const uint32_t sealed_before = m.sealed();
            ASSERT_TRUE(m.clients[1]->vote(2, true));                                    // two of two: the seat is dropped though it is connected
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);      // the same tick everywhere
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->client_present(2));                                     // its connection was closed by the host ...
            ASSERT_EQ(m.host->behind_ms(2), 0u);                                         // ... its seat gates nothing
            ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);        // ... and its machine knows why
            ASSERT_TRUE(m.sealed() > sealed_before + 40);
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(m.clients[seat]->presence().vote_seat == 255 && m.clients[seat]->presence().missing.empty());
            // the match is at full speed: 20 turns a second
            const uint32_t at = m.sealed();
            m.run(5000);
            ASSERT_TRUE(m.sealed() - at >= 98 && m.sealed() - at <= 102);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // nobody votes: the vote about a seat that flapped is open for a minute after its last loss, then the players are told that it is closed (and the seat plays on)
            HoldMatch m(3);
            m.run(6000);
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 3000 && cuts < 3; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_EQ(cuts, 3u);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 6000));
            const uint32_t last_loss = last_cut;
            m.run(2000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            m.run(last_loss + 55000 - m.now);                                            // 55 s after the last loss: still open
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            m.run(8000);                                                                 // more than a minute: closed, and the others were told
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 255 && m.clients[1]->presence().vote_seat == 255);
            ASSERT_FALSE(m.host->attendance().flapping(2, m.now));
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused() && m.host->attendance().state(2) == Attendance::State::Present);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // a seat that is cut again and again: the thirteenth Hello of a minute is refused, and its machine is told that there is no way back; the vote and the cap take the seat
            HoldOptions o;
            o.host.attendance.max_pause_ms = 120000;                                      // (twelve losses count at least 5 s each: 60 s; the cap is above them, so that the Hello budget is what ends it)
            HoldMatch m(3, o);
            m.run(6000);
            const uint32_t sealed_before_cuts = m.sealed();
            const uint32_t t_start = m.now;
            uint32_t cuts = 0;
            uint32_t last_cut = 0;
            for (int i = 0; i < 6000 && !m.clients[2]->lost() && cuts < 20; ++i) {
                m.run(10);
                if (m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2) && !m.host->paused() && m.now - last_cut >= 300) {
                    m.cut(2);
                    last_cut = m.now;
                    ++cuts;
                }
            }
            ASSERT_TRUE(m.until([&]() { return m.clients[2]->lost(); }, 10000));
            ASSERT_EQ(cuts, 13u);                                                         // the thirteenth cut is the last one: its Hello is the thirteenth of a minute
            ASSERT_TRUE(m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::RejoinFailed);
            ASSERT_EQ(m.host->attendance().rejoins(), 12u);
            ASSERT_EQ(m.host->attendance().rejoins_refused(), 1u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);          // the seat is held: the vote and the cap apply
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.now - t_start < 20000);
            const uint32_t held_in_flapping = m.host->attendance().held_ms(m.now);
            ASSERT_TRUE(held_in_flapping < 12000);                                        // (the whole flapping cost the others a few seconds: it used to go on for as long as the machine kept it up)
            ASSERT_TRUE(m.sealed() > sealed_before_cuts + 10);                            // (the match ran between the cuts)
            m.run(2000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);      // the vote is open about it
            ASSERT_TRUE(m.clients[0]->vote(2, true) && m.clients[1]->vote(2, true));
            m.run(2000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.84 Hold: The Resume Countdown (As Requested): After A Pause Of 3 s Or More The Match Stays Held For 10 s More Before The Next Turn Is Sealed (After A Return Or After A Vote Or The Cap Dropped The Seat): Presence Says The Seconds Left, Once A Second (10, 9, ... 1, 0); Chat Goes On, Commands Are Discarded Without A Violation; A Loss Cancels It And A Countdown Follows That Pause; A Blip Under 3 s And The Setting 0 Resume At Once; The Countdown Is Not The Cap's Time Nor A Catch-Up's; The Drops And The Resume Are The Same Tick On Every Machine") {
        HoldOptions o;
        o.host.attendance.resume_countdown_ms = 10000;
        {   // after a return: a pause of 6 s
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(6000);
            ASSERT_TRUE(m.host->paused());
            const uint32_t sealed_at_pause = m.sealed();
            const uint32_t cap_s_before = m.host->attendance().cap_s(m.now);
            m.auto_reconnect[1] = true;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            uint32_t pause_at_back = 0;
            std::vector<unsigned> seen;
            bool mid_checked = false;
            bool mid_submit = true;
            bool mid_paused = false;
            unsigned mid_left = 0;
            bool mid_held = false;
            m.run(18000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present) {
                    back_at = now;
                    pause_at_back = m.host->attendance().pause_ms(now);
                }
                if (back_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
                if (back_at != 0 && resumed_at == 0 && now - back_at < 9900) {            // the countdown: nothing is sealed, the room is held for no seat's sake
                    ASSERT_EQ(m.sealed(), sealed_at_pause);
                    ASSERT_TRUE(m.host->paused() && !m.host->attendance().paused() && m.host->attendance().counting_down(now));
                }
                if (back_at != 0 && !mid_checked && now - back_at >= 4000) {              // 4 s into it
                    mid_checked = true;
                    mid_paused = m.clients[0]->paused() && m.clients[2]->paused();
                    mid_left = m.clients[0]->resume_seconds_left();
                    mid_held = m.clients[0]->runner().held() && m.clients[2]->runner().held() && m.clients[1]->runner().held();
                    mid_submit = m.clients[0]->submit(cmd(CommandType::Hatch, 0));       // the client refuses what the server would discard
                    ASSERT_TRUE(m.clients[0]->chat("anybody there", false));              // chat goes on
                    for (int i = 0; i < 20; ++i) {                                        // a client that does not know: the server discards, no offence
                        CommandMsg msg;
                        msg.command = cmd(CommandType::Hatch, 0);
                        m.client_ends[0]->send(encode(msg));
                    }
                }
                const unsigned s = m.clients[0]->presence().resume_s;
                if (seen.empty() ? s != 0 : s != seen.back()) seen.push_back(s);
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0);
            ASSERT_TRUE(resumed_at - back_at >= 10000 && resumed_at - back_at <= 10040);   // ten seconds from the verified return to the first turn
            ASSERT_TRUE(seen == (std::vector<unsigned>{10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));  // the seconds, once each, and the end
            ASSERT_TRUE(mid_paused && mid_left >= 5 && mid_left <= 7 && !mid_submit && mid_held);
            ASSERT_EQ(m.host->violations(0), 0u);                                         // the commands of the countdown cost nothing ...
            std::vector<uint8_t> packed;
            ASSERT_EQ(m.host->log().read(sealed_at_pause, 1, 100000, packed), 1u);
            TurnBatchMsg first;
            ASSERT_TRUE(decode(encode_turn_batch_packed(sealed_at_pause, 1, packed.data(), packed.size()), first) && first.turns[0].commands.size() < 5);     // ... and were not kept for the first turn after it
            ASSERT_TRUE(m.chats[2].size() >= 1 && m.chats[2].back().text == "anybody there" && m.chats[2].back().sender == 0);     // chat was relayed
            // it is no pause: the cap and the away times are what the pause left, the match's held time has the countdown in it
            ASSERT_EQ(m.host->attendance().pause_ms(m.now), pause_at_back);
            ASSERT_TRUE(m.host->attendance().cap_s(m.now) <= cap_s_before && m.host->attendance().cap_s(m.now) >= cap_s_before - 3);   // (the cap is where the pause left it: 1 - 2 s of the return)
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) >= pause_at_back + 9990);
            ASSERT_TRUE(m.host->attendance().away_ms(1, m.now) <= pause_at_back);
            ASSERT_FALSE(m.clients[0]->paused() || m.clients[1]->paused() || m.clients[2]->paused());
            ASSERT_FALSE(m.clients[0]->runner().held() || m.clients[1]->runner().held() || m.clients[2]->runner().held());
            ASSERT_TRUE(m.clients[0]->submit(cmd(CommandType::Hatch, 0)));                // the match runs: the client may send again
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            for (auto& c : m.clients) ASSERT_EQ(c->runner().buffer_turns(), 1u);           // (a countdown is no stall either)
        }
        {   // a blip: the match resumes at once, no countdown is ever told
            HoldMatch m(3, o);
            m.run(5000);
            m.cut(1);
            std::vector<unsigned> seen;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            const uint32_t sealed_before = m.sealed();
            m.run(3000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present && m.host->attendance().rejoins() == 1) back_at = now;
                if (back_at != 0 && resumed_at == 0 && !m.host->paused()) resumed_at = now;
                if (m.clients[0]->presence().resume_s != 0) seen.push_back(m.clients[0]->presence().resume_s);
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0 && resumed_at - back_at <= 10);
            ASSERT_TRUE(seen.empty());
            ASSERT_FALSE(m.host->attendance().counting_down(m.now));
            ASSERT_TRUE(m.sealed() > sealed_before + 30);
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) < 2000);
        }
        {   // a loss during the countdown cancels it; a countdown follows that pause, however short it is
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.auto_reconnect[2] = false;
            m.cut(1);
            m.run(6000);
            m.auto_reconnect[1] = true;
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(1) == Attendance::State::Present; }, 6000));
            const uint32_t sealed_before = m.sealed();
            m.run(4000);                                                                  // 4 s into the countdown
            ASSERT_TRUE(m.host->paused() && m.host->attendance().counting_down(m.now));
            ASSERT_TRUE(m.clients[0]->resume_seconds_left() > 0);
            m.cut(2);
            m.run(250);
            ASSERT_TRUE(m.host->attendance().paused() && !m.host->attendance().counting_down(m.now));        // cancelled: seat 2 is missing
            ASSERT_EQ(m.sealed(), sealed_before);
            ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].seat == 2 && m.clients[0]->presence().resume_s == 0);
            ASSERT_TRUE(m.clients[0]->resume_seconds_left() == 0 && m.clients[0]->paused());
            m.auto_reconnect[2] = true;
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(2) == Attendance::State::Present; }, 6000));        // it is back within a second or two: a short pause ...
            const uint32_t back2 = m.now;
            ASSERT_TRUE(m.host->attendance().counting_down(m.now));                       // ... and the countdown comes: 10 s from this return
            m.run(9500);
            ASSERT_EQ(m.sealed(), sealed_before);
            m.run(800);
            ASSERT_TRUE(m.sealed() > sealed_before);
            ASSERT_TRUE(m.now - back2 >= 10000);
            ASSERT_FALSE(m.host->paused());
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // the countdown is a setting: 0 resumes at once after a long pause (a room with it off), and so does the library's default
            HoldOptions off;
            ASSERT_EQ(off.host.attendance.resume_countdown_ms, 0u);
            off.host.attendance.resume_countdown_ms = 0;
            HoldMatch m(3, off);
            m.run(5000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(6000);
            m.auto_reconnect[1] = true;
            uint32_t back_at = 0;
            uint32_t resumed_at = 0;
            bool any_countdown = false;
            m.run(3000, true, [&](uint32_t now) {
                if (back_at == 0 && m.host->attendance().state(1) == Attendance::State::Present) back_at = now;
                if (back_at != 0 && resumed_at == 0 && !m.host->paused()) resumed_at = now;
                any_countdown = any_countdown || m.clients[0]->presence().resume_s != 0;
            });
            ASSERT_TRUE(back_at != 0 && resumed_at != 0 && resumed_at - back_at <= 10 && !any_countdown);
        }
        {   // after a vote that dropped the last seat that was missing: ten seconds more, then the Drop is sealed and executes at the same tick everywhere
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(31000);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            const uint32_t sealed_at_pause = m.sealed();
            ASSERT_TRUE(m.clients[0]->vote(2, true) && m.clients[1]->vote(2, true));
            uint32_t dropped_at = 0;
            uint32_t resumed_at = 0;
            std::vector<unsigned> seen;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(16000, true, [&](uint32_t now) {
                if (dropped_at == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) dropped_at = now;
                if (dropped_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
                if (dropped_at != 0 && resumed_at == 0) ASSERT_EQ(m.sealed(), sealed_at_pause);
                const unsigned s = m.clients[0]->presence().resume_s;
                if (seen.empty() ? s != 0 : s != seen.back()) seen.push_back(s);
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(dropped_at != 0 && resumed_at != 0);
            ASSERT_TRUE(resumed_at - dropped_at >= 10000 && resumed_at - dropped_at <= 10040);
            ASSERT_TRUE(seen.size() == 11 && seen.front() == 10 && seen.back() == 0);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);      // the same tick on every machine, after the countdown
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
            m.run(4000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // after the cap dropped the seat: ten seconds more; the cap's account is the pause only
            HoldOptions capped = o;
            capped.host.attendance.max_pause_ms = 20000;
            HoldMatch m(3, capped);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            const uint32_t sealed_at_pause = m.sealed();
            uint32_t dropped_at = 0;
            uint32_t resumed_at = 0;
            m.run(34000, true, [&](uint32_t now) {
                if (dropped_at == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) dropped_at = now;
                if (dropped_at != 0 && resumed_at == 0 && m.sealed() > sealed_at_pause) resumed_at = now;
            });
            ASSERT_TRUE(dropped_at - cut_at >= 20000 && dropped_at - cut_at <= 20300);    // the cap: 20 s of pause
            ASSERT_TRUE(resumed_at - dropped_at >= 10000 && resumed_at - dropped_at <= 10040);
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
            ASSERT_TRUE(m.host->attendance().pause_ms(m.now) >= 20000 && m.host->attendance().pause_ms(m.now) <= 20300);        // the 10 s of countdown are not in it
            ASSERT_TRUE(m.host->attendance().held_ms(m.now) >= 30000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // the countdown is not the catch-up's time: a seat that catches up for 25 s of a budget of 30 s is not let go in the countdown that a slow stream's end begins
            HoldOptions slow = o;
            slow.host.attendance.max_catch_up_ms = 30000;
            slow.client.catch_up_ticks = 1;                                               // 100 turns a second
            HoldMatch m(3, slow);
            m.run(100000);                                                                // 2000 turns: a catch-up of 20 s
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            ASSERT_TRUE(m.until([&]() { return m.host->attendance().state(2) == Attendance::State::Present; }, 28000));
            ASSERT_TRUE(m.host->attendance().catch_up_expired() == 0 && m.host->attendance().counting_down(m.now));
            m.run(11000);                                                                 // the countdown passes (it would have run the 30 s out, were it a catch-up)
            ASSERT_EQ(m.host->attendance().catch_up_expired(), 0u);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.85 Hold: A Log That Is Dead Frees Its Memory At Once (Nobody Can Be Streaming From It: A Log Only Grows While The Match Runs, And A Seat Is Only Brought Back While It Is Paused): The Server's Budget Is Whole Again The Moment It Dies, And The Room Falls Back To Dropping A Lost Seat At Once; A Pause Never Changes What The Log Holds") {
        LogBudget budget(6000);
        HoldOptions o;
        o.host.log_budget = &budget;
        HoldMatch m(3, o);
        m.run(1000);
        ASSERT_TRUE(m.host->log().usable() && budget.used() > 0 && budget.used() == m.host->log().capacity_bytes());
        // a pause: nothing is appended, nothing is allocated, whatever the seat does meanwhile
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        const uint64_t held = budget.used();
        const uint32_t turns = m.host->log().turns();
        m.run(5000);
        ASSERT_TRUE(budget.used() == held && m.host->log().turns() == turns && m.host->log().usable());
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));
        // the match runs on until the budget is spent: the log is dead from that moment and has given everything back
        uint64_t most = 0;
        ASSERT_TRUE(m.until([&]() {
            most = std::max(most, budget.used());
            return !m.host->log().usable();
        }, 120000));
        ASSERT_TRUE(most > 3000 && most <= 6000);
        ASSERT_EQ(budget.used(), uint64_t{0});                                            // at once: not when the match ends
        ASSERT_TRUE(m.host->log().turns() == 0 && m.host->log().bytes() == 0 && m.host->log().capacity_bytes() == 0);
        m.run(2000);
        ASSERT_EQ(budget.used(), uint64_t{0});
        // a lost seat is dropped at once now (no pause, no way back)
        m.auto_reconnect[1] = false;
        m.cut(1);
        bool paused_ever = false;
        m.run(3000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
        ASSERT_FALSE(paused_ever);
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.86 Client: How Long A Machine Tries To Come Back Follows The Room's Cap: The Last Cap That A Presence Named (From The Loss, Or From That Presence When A Seat Was Missing Then) And A Minute; 31 Minutes Before Any Presence; A Cap Of \"That Or More\" Is A Day; The Last Presence Wins") {
        const auto lose_and_wait = [](LoneSession& L) {
            L.lose_link();
            const uint32_t since = L.s->lost_since_ms();
            uint32_t waited = 0;
            while (!L.s->lost() && waited < 3 * 3600u * 1000u) {
                L.step(1000);
                waited += 1000;
            }
            return L.now - since;
        };
        const auto presence = [](LoneSession& L, uint16_t cap_s, bool missing) {
            PresenceMsg p;
            p.cap_s = cap_s;
            p.voters = 2;
            if (missing) p.missing = {{2, PresenceMsg::State::Absent, 20, 0}};
            L.first().srv->send(encode(p));
            L.step(100);
        };
        ASSERT_TRUE(kReconnectGiveUpMs == 31u * 60u * 1000u && kReconnectMarginMs == 60000u && kMaxCapSeconds == 86400u);
        {   // before any Presence: the default, 31 minutes (what a room's default cap and a minute make)
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), kReconnectGiveUpMs);
            ClientSession::Config c;
            c.reconnect_give_up_ms = 7000;
            LoneSession configured(c);
            configured.step(100);
            configured.lose_link();
            ASSERT_EQ(configured.s->give_up_ms(), 7000u);                                // (the configured time is the one that holds until a Presence names a cap)
        }
        {   // a Presence that names a cap of 100 s while nobody was missing: the cap runs from the loss: 100 s and a minute
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, false);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), 160000u);
            const uint32_t waited = lose_and_wait(L);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
            ASSERT_TRUE(waited >= 160000 && waited <= 161100);
        }
        {   // a Presence of a pause that was on already, 30 s before the loss: the cap was running since: 100 s from that Presence and a minute, so 130 s from the loss
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, true);
            L.step(30000);
            L.lose_link();
            const uint32_t gone = L.s->lost_since_ms();
            ASSERT_TRUE(L.s->give_up_ms() >= 129000 && L.s->give_up_ms() <= 130000);
            const uint32_t waited = lose_and_wait(L);
            ASSERT_TRUE(waited >= 129000 && waited <= 131100);
            (void)gone;
        }
        {   // the last Presence wins (a cap that ran down, a pause that ended); a cap that is spent leaves the margin
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            presence(L, 100, false);
            presence(L, 50, false);
            L.lose_link();
            ASSERT_EQ(L.s->give_up_ms(), 110000u);
            LoneSession spent;
            spent.first().pong = true;
            spent.step(100);
            presence(spent, 0, false);
            spent.lose_link();
            ASSERT_EQ(spent.s->give_up_ms(), kReconnectMarginMs);
            LoneSession huge;
            huge.first().pong = true;
            huge.step(100);
            presence(huge, kCapSecondsMore, false);
            huge.lose_link();
            ASSERT_EQ(huge.s->give_up_ms(), 86400u * 1000u + 60000u);                    // 0xFFFF seconds says "that or more": a room's cap is at most a day
            LoneSession exact;
            exact.first().pong = true;
            exact.step(100);
            presence(exact, 1800, false);
            exact.lose_link();
            ASSERT_EQ(exact.s->give_up_ms(), 1860000u);                                  // the default cap of 30 minutes is the default give-up time
        }
    } TEST_END();

    TEST_CASE("N2.87 Hold: Presence Goes Out At Once For A Second Loss While The Match Is Paused (Not Up To A Second Later), And Once A Second And No Faster While Nothing Changes; A Vote That Opens Or Closes While The Match Runs Is Told Too") {
        HoldMatch m(3);
        m.run(3000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        m.run(300);                                                                       // paused; the last Presence went out at the loss, the next is due in 0.7 s
        ASSERT_TRUE(m.host->paused());
        const size_t before = m.taps[0]->sent_of(MsgType::Presence);
        ASSERT_TRUE(before >= 1);
        m.cut(2);
        m.run(30);                                                                        // the next pass or two
        ASSERT_TRUE(m.taps[0]->sent_of(MsgType::Presence) > before);                       // told in the same pass that saw the loss
        m.run(120);                                                                       // ... and it reaches the player
        ASSERT_EQ(m.clients[0]->presence().missing.size(), size_t{2});
        // nothing changes for ten seconds: a Presence a second, to every player that is there
        const size_t quiet_from = m.taps[0]->sent_of(MsgType::Presence);
        m.run(10000);
        const size_t quiet = m.taps[0]->sent_of(MsgType::Presence) - quiet_from;
        ASSERT_TRUE(quiet >= 9 && quiet <= 11);                                           // (a thousand when it went out on every pass)
        // a vote that opens by itself: told when the seat's away time reaches the vote's 30 s (within the second), and nothing more often than that
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 255);
        m.run(20000);
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 1);
    } TEST_END();
    TEST_CASE("N2.88 Hold (the rig): A Machine That Is Reloaded Over A Slow Link (More Than A Second In The Lobby) Has A Sane Clock: Its Session Is Started With The Machine's Own Clock And Its First Frame Does Not Take Back The Time That The Lobby Took, So It Is Not \"Silent\" And Does Not Give Up At Once; The Rig Of Every Other Test Reloads The Same Way") {
        HoldOptions o;
        o.link.latency_ms = 700;                                                        // four trips for the lobby alone: more than 2.8 s before the session exists
        o.link.jitter_ms = 0;
        HoldMatch m(3, o);
        m.run(8000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(3000);
        ASSERT_TRUE(m.host->paused());
        const uint32_t reload_at = m.now;
        m.clock_lag[2] = 4000;                                                          // this machine's own clock is behind (a window that was frozen earlier): the new session is started with that clock, not the real one
        m.reload(2);
        ASSERT_TRUE(m.until([&]() { return m.clients[2] != nullptr; }, 8000));
        ASSERT_TRUE(m.now - reload_at > 2000);                                          // the lobby took longer than a second: the old rig put the session's clock back by that much at its first frame
        ASSERT_EQ(m.clock_lag[2], 4000u);                                               // ... and the time in the lobby is no stop of the session's frames: the lag has not grown by it
        ASSERT_TRUE(m.until([&]() { return m.clients[2]->mode() == ClientSession::Mode::Normal && !m.host->paused(); }, 30000));
        ASSERT_FALSE(m.clients[2]->lost());
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        m.run(8000);
        m.settle(6000);
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 13: a level for each seat of the leader's fill, and teams in the start data
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol13_tests() {
    TEST_CASE("N2.100 Protocol 13, The Messages: StartRequest Is The Type, A Level For Each Seat (0 .. 3) And Two Team Bytes (None Both, Or Two Different Seats 0 .. 3), Seven Bytes And Nothing Else; Start Ends With The Same Two Bytes And Its Decoder Takes Only Teams That Its Own Roster Can Make; The Notice Fits A Line Of Chat") {
        ASSERT_EQ(kProtocolVersion, 20);                                                       // (the messages of protocol 13 are the messages of protocols 14 to 17 too: 14 added the SeatMove, N2.101; 15 the platform and the create block, N2.102 - N2.104; 16 the lobby room, N2.105 - N2.107; 17 the game mode, N2.112)
        ASSERT_TRUE(kNoTeam == 255 && kFillLevelLast == 3);
        StartRequestMsg out;
        // the layout, byte by byte: the type, the levels of seats 0 to 3, team_a, team_b
        StartRequestMsg m;
        m.fill = {FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard};
        m.team_a = 2;
        m.team_b = 0;
        ASSERT_TRUE(encode(m) == (std::vector<uint8_t>{24, 0, 1, 2, 3, 2, 0}));
        ASSERT_TRUE(decode(encode(m), out) && out.fill == m.fill && out.team_a == 2 && out.team_b == 0);
        ASSERT_TRUE(encode(StartRequestMsg{}) == (std::vector<uint8_t>{24, 0, 0, 0, 0, 255, 255}));
        ASSERT_TRUE(StartRequestMsg::all(FillLevel::Medium).fill == (std::array<FillLevel, 4>{FillLevel::Medium, FillLevel::Medium, FillLevel::Medium, FillLevel::Medium}));
        // each seat's own level: every level at every seat, the others none
        for (uint8_t seat = 0; seat < 4; ++seat) {
            for (uint8_t level = 0; level <= kFillLevelLast; ++level) {
                StartRequestMsg one;
                one.fill[seat] = static_cast<FillLevel>(level);
                std::vector<uint8_t> want = {24, 0, 0, 0, 0, 255, 255};
                want[1u + seat] = level;
                ASSERT_TRUE(encode(one) == want);
                StartRequestMsg back = StartRequestMsg::all(FillLevel::Hard);
                ASSERT_TRUE(decode(want, back) && back.fill == one.fill && back.team_a == kNoTeam && back.team_b == kNoTeam);
            }
        }
        // a level above 3 in any seat, whatever the other bytes: no message (and the target is left alone)
        for (size_t at = 1; at <= 4; ++at) {
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> bytes = {24, 1, 2, 3, 0, 255, 255};
                bytes[at] = static_cast<uint8_t>(value);
                StartRequestMsg keep = StartRequestMsg::all(FillLevel::Hard);
                const bool accepted = decode(bytes, keep);
                ASSERT_EQ(accepted, value <= kFillLevelLast);
                if (!accepted) ASSERT_TRUE(keep.fill == StartRequestMsg::all(FillLevel::Hard).fill && keep.team_a == kNoTeam);
            }
        }
        // the team bytes: every pair of byte values. Free for all is 255 and 255; a team is two DIFFERENT seats of 0 .. 3; nothing else is a message
        size_t pairs = 0;
        for (unsigned a = 0; a < 256; ++a) {
            for (unsigned b = 0; b < 256; ++b) {
                const std::vector<uint8_t> bytes = {24, 0, 0, 0, 0, static_cast<uint8_t>(a), static_cast<uint8_t>(b)};
                StartRequestMsg keep;
                keep.team_a = 7;
                const bool should = (a == 255 && b == 255) || (a < 4 && b < 4 && a != b);
                const bool accepted = decode(bytes, keep);
                ASSERT_EQ(accepted, should);
                if (accepted) {
                    ASSERT_TRUE(keep.team_a == a && keep.team_b == b && encode(keep) == bytes);
                    ASSERT_EQ(keep.teams() == sim::StartTeams{}, a == 255);
                    pairs += a == 255 ? 0u : 1u;
                } else {
                    ASSERT_EQ(keep.team_a, 7);
                }
            }
        }
        ASSERT_EQ(pairs, size_t{12});                                                          // 4 x 3 ordered pairs (the first seat invites)
        // the teams of a message as the model's value, and back
        for (uint8_t a = 0; a < 4; ++a) {
            for (uint8_t b = 0; b < 4; ++b) {
                if (a == b) continue;
                StartRequestMsg req;
                req.set_teams(sim::StartTeams{true, a, b});
                ASSERT_TRUE(req.teams() == sim::StartTeams({true, a, b}) && req.team_a == a && req.team_b == b);
                StartMsg st;
                st.set_teams(sim::StartTeams{true, a, b});
                ASSERT_TRUE(st.teams() == sim::StartTeams({true, a, b}) && st.team_a == a && st.team_b == b);
            }
        }
        {
            StartRequestMsg req;
            req.set_teams(sim::StartTeams{true, 1, 2});
            req.set_teams(sim::StartTeams{});                                                  // free for all again: both bytes
            ASSERT_TRUE(req.team_a == kNoTeam && req.team_b == kNoTeam && !req.teams().set);
        }
        // every strict prefix and every extra byte of a message with teams and levels is refused; no other type byte takes it
        const std::vector<uint8_t> full = encode(m);
        for (size_t cut = 0; cut < full.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut)), out));
        for (unsigned extra = 0; extra < 256; ++extra) {
            std::vector<uint8_t> longer = full;
            longer.push_back(static_cast<uint8_t>(extra));
            ASSERT_FALSE(decode(longer, out));
        }
        for (unsigned type = 0; type < 256; ++type) {
            if (type == 24) continue;
            std::vector<uint8_t> other = full;
            other[0] = static_cast<uint8_t>(type);
            ASSERT_FALSE(decode(other, out));
        }
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 2}, out));                                // protocol 11's two bytes: one level for all
        ASSERT_FALSE(decode(std::vector<uint8_t>{24, 0, 0, 0, 0}, out));                       // the levels without the teams

        // ---- the Start message: the two team bytes, then the four platform bytes of protocol 15 (N2.103), and the decoder's rule ----
        const auto start_with = [](uint8_t roster, uint8_t a, uint8_t b) {
            StartMsg st;
            st.seed = 77;
            st.map_name = "TREASURE.LVL";
            st.map_hash = 0x0102030405060708ull;
            st.roster = roster;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if ((roster >> seat) & 1u) st.names[seat] = std::string("P") + static_cast<char>('0' + seat);
            }
            st.team_a = a;
            st.team_b = b;
            return st;
        };
        {
            const StartMsg st = start_with(0x0F, 0, 3);
            const std::vector<uint8_t> bytes = encode(st);
            ASSERT_TRUE(bytes.size() >= 8 && bytes[bytes.size() - 7] == 0 && bytes[bytes.size() - 6] == 3);       // appended to the layout: the two bytes before the four platform bytes (and, since protocol 17, the mode)
            StartMsg back;
            ASSERT_TRUE(decode(bytes, back) && back.team_a == 0 && back.team_b == 3 && back.roster == 0x0F && back.seed == 77 && back.names[2] == "P2" && encode(back) == bytes);
            const std::vector<uint8_t> ffa = encode(start_with(0x0F, kNoTeam, kNoTeam));
            ASSERT_EQ(ffa.size(), bytes.size());
            ASSERT_TRUE(ffa[ffa.size() - 7] == 255 && ffa[ffa.size() - 6] == 255);
            ASSERT_TRUE(decode(ffa, back) && back.team_a == kNoTeam && back.team_b == kNoTeam && !back.teams().set);
            std::vector<uint8_t> old_layout = ffa;                                                                // protocol 12's Start has no team bytes and no platform bytes: it is no Start
            old_layout.resize(old_layout.size() - 7);
            ASSERT_FALSE(decode(old_layout, back));
            std::vector<uint8_t> v14 = ffa;                                                                       // protocols 13 and 14's Start has no platform bytes: it is no Start of 15 (nor of 17: no mode byte either)
            v14.resize(v14.size() - 5);
            ASSERT_FALSE(decode(v14, back));
            std::vector<uint8_t> v16 = ffa;                                                                       // protocols 15 and 16's Start has no mode byte: it is no Start of 17
            v16.resize(v16.size() - 1);
            ASSERT_FALSE(decode(v16, back));
            std::vector<uint8_t> one_short = ffa;
            one_short.pop_back();
            ASSERT_FALSE(decode(one_short, back));
            std::vector<uint8_t> longer = ffa;
            longer.push_back(255);
            ASSERT_FALSE(decode(longer, back));
        }
        // every pair of bytes against every roster: the decoder takes it exactly when it is free for all, or the plan of the model can make it for that roster
        size_t accepted_teams = 0;
        size_t refused_teams = 0;
        for (unsigned roster = 0; roster < 16; ++roster) {
            if (std::bitset<8>(roster).count() < 2) continue;                                                           // (a Start of fewer than two players is refused for that reason)
            for (unsigned a = 0; a < 256; a += (a < 6 ? 1 : (a == 254 ? 1 : 249))) {                               // 0 .. 5, 255 (the values between are no different)
                for (unsigned b = 0; b < 256; b += (b < 6 ? 1 : (b == 254 ? 1 : 249))) {
                    const StartMsg st = start_with(static_cast<uint8_t>(roster), static_cast<uint8_t>(a), static_cast<uint8_t>(b));
                    StartMsg back;
                    const bool plain = a == 255 && b == 255;
                    const bool can = !plain && sim::plan_start_teams(sim::StartTeams{true, static_cast<uint8_t>(a), static_cast<uint8_t>(b)}, static_cast<uint8_t>(roster)).why.empty();
                    const bool accepted = decode(encode(st), back);
                    ASSERT_EQ(accepted, plain || can);
                    if (accepted) {
                        ASSERT_TRUE(back.team_a == a && back.team_b == b && encode(back) == encode(st));
                        accepted_teams += plain ? 0u : 1u;
                    } else {
                        ++refused_teams;
                    }
                }
            }
        }
        ASSERT_TRUE(accepted_teams > 30 && refused_teams > 100);
        // the cases that matter by name
        {
            StartMsg back;
            ASSERT_TRUE(decode(encode(start_with(0x0F, 0, 1)), back));                                              // four seats: the pair (and the other two, as a team)
            ASSERT_TRUE(decode(encode(start_with(0x07, 1, 2)), back));                                              // three seats: the pair, the third alone
            ASSERT_TRUE(decode(encode(start_with(0x0D, 3, 0)), back));                                              // seats 0, 2, 3 play: the pair 3 + 0, seat 2 alone
            ASSERT_FALSE(decode(encode(start_with(0x03, 0, 1)), back));                                             // two seats: the team would be the whole match (the original ends it at once)
            ASSERT_FALSE(decode(encode(start_with(0x07, 0, 3)), back));                                             // seat 3 does not play
            ASSERT_FALSE(decode(encode(start_with(0x0F, 2, 2)), back));                                             // one seat is no team
            ASSERT_FALSE(decode(encode(start_with(0x0F, 255, 1)), back));                                           // one of the bytes only
            ASSERT_FALSE(decode(encode(start_with(0x0F, 1, 255)), back));
            ASSERT_FALSE(decode(encode(start_with(0x0F, 4, 5)), back));                                             // seats that no match has
        }
        // ---- the notice: "No teams: " and the reason is one line of chat, whatever the teams and the seats ----
        ASSERT_EQ(std::string(kNoticeNoTeams), std::string("No teams: "));
        for (unsigned roster = 0; roster < 16; ++roster) {
            for (uint8_t a = 0; a < 4; ++a) {
                for (uint8_t b = 0; b < 4; ++b) {
                    const sim::StartTeamsPlan plan = sim::plan_start_teams(sim::StartTeams{true, a, b}, static_cast<uint8_t>(roster));
                    if (plan.why.empty()) continue;
                    const std::string text = std::string(kNoticeNoTeams) + plan.short_why;
                    ASSERT_TRUE(text.size() <= kMaxChatChars);
                    ChatMsg notice;
                    notice.sender = kRoomSender;
                    notice.text = text;
                    ChatMsg back;
                    ASSERT_TRUE(decode(encode(notice), back) && back.text == text);
                }
            }
        }
    } TEST_END();

    TEST_CASE("N2.101 Protocol 14 And 15, The Message: SeatMove Is The Type 31, Two Different Seats 0 .. 3 And The Guard (A 32-Bit Number That Is Never 0, Little Endian), Seven Bytes And Nothing Else; Every Other Pair Of Seats, A Guard Of 0, Every Truncation, Every Extra Byte And Every Other Type Is Refused, A Refused Message Leaves Its Target Alone, The Layout Of Protocol 14 Is Refused, And No Other Message Is Taken For It; The Guard Is The Seating Hash: The State And The Name Of Each Seat, Nothing Else Of The Room Message") {
        ASSERT_EQ(kProtocolVersion, 20);
        ASSERT_TRUE(static_cast<int>(MsgType::SeatMove) == 31 && static_cast<int>(MsgType::Last) == 34);
        // the layout, byte by byte: the type, the seat of the player that moves, the seat that it takes, the guard
        ASSERT_TRUE(encode(SeatMoveMsg{0, 1, 0x04030201u}) == (std::vector<uint8_t>{31, 0, 1, 1, 2, 3, 4}));
        ASSERT_TRUE(encode(SeatMoveMsg{3, 2, 0xFFFFFFFFu}) == (std::vector<uint8_t>{31, 3, 2, 255, 255, 255, 255}));
        ASSERT_EQ(peek_type(encode(SeatMoveMsg{2, 0, 7})), MsgType::SeatMove);
        SeatMoveMsg out;
        ASSERT_TRUE(decode(std::vector<uint8_t>{31, 2, 0, 9, 0, 0, 0}, out) && out.from == 2 && out.to == 0 && out.guard == 9);
        // every pair of byte values: the 12 ordered pairs of two different seats of the room are messages, nothing else
        size_t accepted = 0;
        for (unsigned from = 0; from < 256; ++from) {
            for (unsigned to = 0; to < 256; ++to) {
                const std::vector<uint8_t> bytes = {31, static_cast<uint8_t>(from), static_cast<uint8_t>(to), 0x78, 0x56, 0x34, 0x12};
                SeatMoveMsg keep{7, 9, 5};
                const bool should = from < 4 && to < 4 && from != to;
                const bool ok = decode(bytes, keep);
                ASSERT_EQ(ok, should);
                if (ok) {
                    ASSERT_TRUE(keep.from == from && keep.to == to && keep.guard == 0x12345678u && encode(keep) == bytes);
                    ++accepted;
                } else {
                    ASSERT_TRUE(keep.from == 7 && keep.to == 9 && keep.guard == 5);                  // (the target is left alone)
                }
            }
        }
        ASSERT_EQ(accepted, size_t{12});
        // the guard: any number but 0 (an honest client sends the seating hash, which is never 0)
        for (const uint32_t guard : {1u, 2u, 255u, 256u, 0x10000u, 0x01000000u, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu}) {
            SeatMoveMsg got;
            ASSERT_TRUE(decode(encode(SeatMoveMsg{1, 3, guard}), got) && got.guard == guard);
        }
        {
            SeatMoveMsg keep{7, 9, 5};
            ASSERT_FALSE(decode(std::vector<uint8_t>{31, 1, 2, 0, 0, 0, 0}, keep));
            ASSERT_TRUE(keep.from == 7 && keep.to == 9 && keep.guard == 5);
        }
        // every strict prefix and every extra byte of a message is refused; no other type byte takes it, and no pointer without bytes
        const std::vector<uint8_t> full = encode(SeatMoveMsg{1, 3, 0xA1B2C3D4u});
        ASSERT_EQ(full.size(), size_t{7});
        for (size_t cut = 0; cut < full.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut)), out));
        for (unsigned extra = 0; extra < 256; ++extra) {
            std::vector<uint8_t> longer = full;
            longer.push_back(static_cast<uint8_t>(extra));
            ASSERT_FALSE(decode(longer, out));
        }
        for (unsigned type = 0; type < 256; ++type) {
            if (type == 31) continue;
            std::vector<uint8_t> other = full;
            other[0] = static_cast<uint8_t>(type);
            ASSERT_FALSE(decode(other, out));
        }
        ASSERT_FALSE(decode(static_cast<const uint8_t*>(nullptr), 7, out));
        ASSERT_FALSE(decode(std::vector<uint8_t>{31, 1, 2}, out));                                  // protocol 14's three bytes: no guard, no message
        // no other message is a SeatMove, and a SeatMove is no other message
        {
            VoteMsg vote;
            ChatMsg chat;
            StartRequestMsg request;
            LagMsg lag;
            RequestMsg ask;
            ASSERT_FALSE(decode(encode(VoteMsg{2, true}), out));
            ASSERT_FALSE(decode(encode(StartRequestMsg{}), out));
            ASSERT_FALSE(decode(encode(ChatMsg{1, true, "hi"}), out));
            ASSERT_FALSE(decode(encode(LagMsg{2, 7000}), out));
            const std::vector<uint8_t> move = encode(SeatMoveMsg{1, 2, 77});
            ASSERT_FALSE(decode(move, vote) || decode(move, chat) || decode(move, request) || decode(move, lag) || decode(move, ask));
        }
        // the guard is the seating hash: never 0, the same for the same seats (the leader's screen and the room compute it from the same Room message, each from its own copy), and different when a
        // seat's state or name is different; the round trip times, the platforms, the map, the fog, the teams and the rules are not who sits where and do not count
        {
            RoomMsg room;
            room.slots[0] = {SlotState::Client, "Ann", 30, 0};
            room.slots[1] = {SlotState::Client, "Bob", 40, 0};
            room.slots[2] = {SlotState::Bot, "Bot (Easy)", 0, 0};
            room.leader = 0;
            room.map_name = "TINY.LVL";
            const uint32_t base = seating_hash(room);
            ASSERT_TRUE(base != 0);
            // the number itself is protocol 15's: FNV-1a (offset basis 2166136261, prime 16777619) over [state, the name's bytes, 0] of each of the four seats, worked out apart from the code (a machine of
            // another build has to agree on it, or every press of its leader would be ignored)
            ASSERT_EQ(base, 0xAD2C9B12u);
            ASSERT_EQ(seating_hash(RoomMsg{}), 0x9BE17165u);                                        // four seats of (Empty, "")
            {
                RoomMsg hosted;
                hosted.slots[0] = {SlotState::Host, "Hal", 0, 0};
                hosted.slots[1] = {SlotState::Client, "Ann", 30, 0};
                hosted.slots[2] = {SlotState::Client, "Bob", 40, 0};
                hosted.slots[3] = {SlotState::Client, "Cat", 50, 0};
                ASSERT_EQ(seating_hash(hosted), 0xB5063C85u);
            }
            RoomMsg copy;
            ASSERT_TRUE(decode(encode(room), copy));
            ASSERT_EQ(seating_hash(copy), base);                                                    // (what travelled is what was hashed)
            RoomMsg other = room;
            other.slots[0].rtt_ms = 900;
            other.slots[1].platform = static_cast<uint8_t>(kOsLinux | kPlatformBrowser);
            other.map_name = "SMALL.LVL";
            other.fog = true;
            other.you = 1;
            other.team_a = 0;
            other.team_b = 2;
            other.flags = kRoomLeaderStarts;
            ASSERT_EQ(seating_hash(other), base);
            other = room;
            std::swap(other.slots[0], other.slots[1]);                                              // two guests that changed places
            ASSERT_TRUE(seating_hash(other) != base);
            other = room;
            other.slots[0].name = "Anna";
            ASSERT_TRUE(seating_hash(other) != base);
            other = room;
            other.slots[3] = {SlotState::Client, "Cat", 20, 0};                                     // somebody sits in a free colour
            ASSERT_TRUE(seating_hash(other) != base);
            other = room;
            other.slots[1].state = SlotState::Host;                                                 // the state counts as well as the name
            ASSERT_TRUE(seating_hash(other) != base);
            other = room;
            other.slots[2].name = "Bot (Hard)";
            ASSERT_TRUE(seating_hash(other) != base);
            ASSERT_TRUE(seating_hash(RoomMsg{}) != 0);
            // the names cannot slide from one seat into the next: "Ann" + "" and "An" + "n" differ (a 0 ends each name)
            RoomMsg a;
            a.slots[0] = {SlotState::Client, "Ann", 1, 0};
            a.slots[1] = {SlotState::Client, "", 1, 0};
            RoomMsg b;
            b.slots[0] = {SlotState::Client, "An", 1, 0};
            b.slots[1] = {SlotState::Client, "n", 1, 0};
            ASSERT_TRUE(seating_hash(a) != seating_hash(b));
            // every state in every seat gives a different number than the others (4^4 seatings with one name): no two of the 256 agree
            std::set<uint32_t> seen;
            for (unsigned code = 0; code < 256; ++code) {
                RoomMsg r;
                for (unsigned seat = 0; seat < 4; ++seat) r.slots[seat].state = static_cast<SlotState>((code >> (2 * seat)) & 3u);
                seen.insert(seating_hash(r));
            }
            ASSERT_EQ(seen.size(), size_t{256});
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 15: a room is made by its first Hello's create block, and the platform of each person is told
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol15_tests() {
    // the bytes of a Hello as protocol 16 writes it: the fields of protocol 10, then the platform byte, the client kind (protocol 16), then (when the Hello makes a room) the create block
    const auto key_bytes = []() {
        SeatKey key{};
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(0xA0 + i);
        return key;
    };
    const auto hello_prefix = [&](const std::string& room) {
        std::vector<uint8_t> bytes = {1, 20, 0, 3, 'A', 'n', 'n', 0x34, 0x12, 2, static_cast<uint8_t>(room.size())};
        bytes.insert(bytes.end(), room.begin(), room.end());
        const std::vector<uint8_t> token = {3, 't', 'o', 'k'};
        bytes.insert(bytes.end(), token.begin(), token.end());
        const SeatKey key = key_bytes();
        bytes.insert(bytes.end(), key.begin(), key.end());
        const std::vector<uint8_t> turns = {5, 0, 0, 0};                                      // (little endian)
        bytes.insert(bytes.end(), turns.begin(), turns.end());
        return bytes;
    };
    const auto hello_of = [&](const std::string& room, uint8_t platform) {
        HelloMsg h;
        h.name = "Ann";
        h.listen_port = 0x1234;
        h.want_seat = 2;
        h.room = room;
        h.token = "tok";
        h.key = key_bytes();
        h.have_turns = 5;
        h.platform = platform;
        return h;
    };

    TEST_CASE("N2.102 Protocol 15 And 16, The Hello: The Platform Byte Follows The Turns, A Create Block (A Map Name, The Seats, The Two Team Bytes And The Rule Flags) Ends The Message When The Hello Makes A Room; The Decoder Reads The Block Strictly, Refuses Every Value An Honest Client Does Not Send, Every Truncation Of The Block And Every Extra Byte; The Layout Of Protocol 14 Is No Hello Of This Protocol But Its Version And Name Are Read") {
        ASSERT_EQ(kProtocolVersion, 20);
        ASSERT_TRUE(kCreateLeaderStarts == 1 && kRoomLeaderStarts == kCreateLeaderStarts && kNoTeam == 255);
        // the layout, byte by byte, without a block: the platform byte and the client kind are the last two (Linux in a browser: 3 + 0x10; a game)
        {
            HelloMsg h = hello_of("k7m2xq", static_cast<uint8_t>(kOsLinux | kPlatformBrowser));
            std::vector<uint8_t> want = hello_prefix("k7m2xq");
            want.push_back(0x13);
            want.push_back(0);
            ASSERT_TRUE(encode(h) == want);
            HelloMsg back;
            ASSERT_TRUE(decode(want, back) && back.platform == 0x13 && !back.create && back.room == "k7m2xq" && back.name == "Ann" && back.have_turns == 5 && back.version == 20 && back.client_kind == kClientGame);
            ASSERT_TRUE(encode(back) == want);
        }
        // with a block: the map's name, the seats, team_a, team_b, the flags
        {
            HelloMsg h = hello_of("k7m2xq", kOsWindows);
            CreateBlock block;
            block.map_name = "TREASURE.LVL";
            block.seats = 3;
            block.team_a = 0;
            block.team_b = 2;
            block.flags = kCreateLeaderStarts;
            h.create = block;
            std::vector<uint8_t> want = hello_prefix("k7m2xq");
            want.push_back(1);
            want.push_back(0);
            want.push_back(12);
            for (const char c : std::string("TREASURE.LVL")) want.push_back(static_cast<uint8_t>(c));
            for (const uint8_t b : {uint8_t{3}, uint8_t{0}, uint8_t{2}, uint8_t{1}}) want.push_back(b);
            ASSERT_TRUE(encode(h) == want);
            HelloMsg back;
            ASSERT_TRUE(decode(want, back) && back.create && *back.create == block && back.platform == kOsWindows);
            ASSERT_TRUE(encode(back) == want);
            ASSERT_TRUE(back.create->leader_starts() && back.create->teams() == sim::StartTeams({true, 0, 2}));
            // every strict prefix of the whole message is refused except the one that ends after the client kind (a Hello without a block); one byte more is refused
            const size_t without_block = hello_prefix("k7m2xq").size() + 2;
            for (size_t cut = 0; cut < want.size(); ++cut) {
                HelloMsg keep;
                keep.name = "keep";
                const bool ok = decode(std::vector<uint8_t>(want.begin(), want.begin() + static_cast<std::ptrdiff_t>(cut)), keep);
                ASSERT_EQ(ok, cut == without_block);
                if (!ok) ASSERT_TRUE(keep.name == "keep" && !keep.create);                                        // (the target is left alone)
            }
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = want;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
        }
        // the platform byte: of the 256 values, the 14 that an honest client tells (an operating system 0 .. 6, with the browser bit or without) are a Hello
        {
            size_t accepted = 0;
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> bytes = hello_prefix("ROOM-1");
                bytes.push_back(static_cast<uint8_t>(value));
                bytes.push_back(0);
                HelloMsg back;
                const bool ok = decode(bytes, back);
                ASSERT_EQ(ok, valid_platform(static_cast<uint8_t>(value)));
                if (ok) {
                    ASSERT_TRUE(back.platform == value && encode(back) == bytes);
                    ++accepted;
                }
            }
            ASSERT_EQ(accepted, size_t{14});
            ASSERT_TRUE(valid_platform(0) && valid_platform(kPlatformBrowser) && valid_platform(kOsOther) && valid_platform(static_cast<uint8_t>(kOsOther | kPlatformBrowser)));
            ASSERT_FALSE(valid_platform(7) || valid_platform(0x0F) || valid_platform(0x20) || valid_platform(0x80) || valid_platform(0xFF) || valid_platform(0x17));
        }
        // the block's values, one at a time: what is refused and what is taken (the block is `valid_create_block`'s: the decoder reads it with the same rule)
        {
            const auto with = [&](const std::string& map, uint8_t seats, uint8_t a, uint8_t b, uint8_t flags) {
                std::vector<uint8_t> bytes = hello_prefix("ROOM-1");
                bytes.push_back(0);
                bytes.push_back(0);
                bytes.push_back(static_cast<uint8_t>(map.size()));
                bytes.insert(bytes.end(), map.begin(), map.end());
                for (const uint8_t v : {seats, a, b, flags}) bytes.push_back(v);
                return bytes;
            };
            HelloMsg back;
            for (unsigned seats = 0; seats < 256; ++seats) {
                const bool ok = decode(with("", static_cast<uint8_t>(seats), 255, 255, 0), back);
                ASSERT_EQ(ok, seats >= 2 && seats <= 4);
            }
            size_t pairs = 0;
            for (unsigned a = 0; a < 256; ++a) {
                for (unsigned b = 0; b < 256; ++b) {
                    const bool ok = decode(with("", 4, static_cast<uint8_t>(a), static_cast<uint8_t>(b), 0), back);
                    const bool should = (a == 255 && b == 255) || (a < b && b < 4);                              // free for all, or two seats and the lower one first
                    ASSERT_EQ(ok, should);
                    if (ok) {
                        ASSERT_TRUE(back.create->team_a == a && back.create->team_b == b);
                        pairs += a == 255 ? 0u : 1u;
                    }
                }
            }
            ASSERT_EQ(pairs, size_t{6});                                                                         // 4 seats: six pairs, each in the one spelling that the decoder takes
            for (unsigned flags = 0; flags < 256; ++flags) ASSERT_EQ(decode(with("", 4, 255, 255, static_cast<uint8_t>(flags)), back), flags == 0 || flags == 1 || flags == 3);        // (2 is a lobby room: the leader starts it and it has all four colours, so 3 is the one)
            // the map's name: "" (the server's own choice), a map's file name, the longest one, and not a name that could name a path
            ASSERT_TRUE(decode(with("", 2, 255, 255, 0), back) && back.create->map_name.empty());
            ASSERT_TRUE(decode(with("TINY.LVL", 2, 255, 255, 0), back) && back.create->map_name == "TINY.LVL");
            ASSERT_TRUE(decode(with(std::string(kMaxMapNameChars - 4, 'x') + ".LVL", 2, 255, 255, 0), back));
            for (const std::string& bad : {std::string("tiny.txt"), std::string("a/b.LVL"), std::string("..\\x.LVL"), std::string(".hidden.LVL"), std::string(kMaxMapNameChars - 3, 'x') + ".LVL", std::string("x.LVL\x01"), std::string("c:x.LVL")}) {
                ASSERT_FALSE(decode(with(bad, 2, 255, 255, 0), back));
            }
        }
        // the room's code in a Hello: 1 .. 32 letters, digits, '_' and '-', as before; the 6 random characters of the game's own codes are such a code
        {
            HelloMsg back;
            for (const std::string& ok : {std::string("k7m2xq"), std::string("ROOM-1"), std::string("a"), std::string(32, 'z')}) {
                std::vector<uint8_t> bytes = hello_prefix(ok);
                bytes.push_back(0);
                bytes.push_back(0);
                ASSERT_TRUE(decode(bytes, back) && back.room == ok);
            }
            for (const std::string& bad : {std::string("k7m 2xq"), std::string("k7m2.xq"), std::string(33, 'z'), std::string("k7m2/xq")}) {
                std::vector<uint8_t> bytes = hello_prefix(bad);
                bytes.push_back(0);
                bytes.push_back(0);
                ASSERT_FALSE(decode(bytes, back));
            }
        }
        // the layout of protocol 14 (no platform byte) is no Hello of this protocol, whatever the version byte says, but its version and its name are read, so that a server can tell the player
        {
            std::vector<uint8_t> old_layout = hello_prefix("k7m2xq");
            old_layout[1] = 14;
            HelloMsg back;
            ASSERT_FALSE(decode(old_layout, back));
            HelloMsg prefix;
            ASSERT_TRUE(decode_hello_prefix(old_layout.data(), old_layout.size(), prefix) && prefix.version == 14 && prefix.name == "Ann");
            std::vector<uint8_t> as_17 = old_layout;
            as_17[1] = 17;
            ASSERT_FALSE(decode(as_17, back));                                                                   // (the number of the protocol does not make it a Hello of 17: the platform byte and the client kind are missing)
            as_17.push_back(0);
            ASSERT_FALSE(decode(as_17, back));                                                                   // (the layout of protocol 15, with its platform byte and no client kind: not a Hello of 17 either)
            as_17.push_back(0);
            ASSERT_TRUE(decode(as_17, back) && back.version == 17 && back.client_kind == kClientGame);
        }
        // no other message is a Hello of this protocol and the Hello is no other message
        {
            HelloMsg h = hello_of("ROOM-1", 0);
            const std::vector<uint8_t> bytes = encode(h);
            WelcomeMsg welcome;
            RejectMsg reject;
            ASSERT_FALSE(decode(bytes, welcome) || decode(bytes, reject));
            for (unsigned type = 0; type < 256; ++type) {
                if (type == 1) continue;
                std::vector<uint8_t> other = bytes;
                other[0] = static_cast<uint8_t>(type);
                HelloMsg back;
                ASSERT_FALSE(decode(other, back));
            }
        }
    } TEST_END();

    TEST_CASE("N2.103 Protocol 15 (And 16, Whose Five Bytes Follow The Flags), The Room And The Start: Each Seat Of The Room Message Ends With A Platform Byte, The Message Ends With The Room's Two Team Bytes And Its Rule Flags; The Start Ends With The Platform Of Each Seat; The Decoders Refuse A Platform That No Honest Server Writes (One For An Empty Seat Or A Bot, A Value Outside The Table), Teams That Are Not A Pair Of Seats, A Rule Bit That Nobody Knows, And The Layouts Of Protocol 14") {
        RoomMsg room;
        room.slots[0] = {SlotState::Client, "Ann", 30, static_cast<uint8_t>(kOsWindows)};
        room.slots[1] = {SlotState::Client, "Bob", 40, static_cast<uint8_t>(kOsAndroid | kPlatformBrowser)};
        room.slots[2] = {SlotState::Bot, "Bot (Easy)", 0, 0};
        room.map_name = "TINY.LVL";
        room.fog = true;
        room.you = 1;
        room.leader = 0;
        room.team_a = 0;
        room.team_b = 1;
        room.flags = kRoomLeaderStarts;
        const std::vector<uint8_t> bytes = encode(room);
        // the layout of the first seat, and the end of the message: the platform byte after the round trip time, then ... map, fog, you, leader, team_a, team_b, flags
        ASSERT_TRUE(bytes.size() > 20 && bytes[0] == 12);
        ASSERT_TRUE(bytes[1] == 2 && bytes[2] == 3 && bytes[3] == 'A' && bytes[4] == 'n' && bytes[5] == 'n' && bytes[6] == 30 && bytes[7] == 0 && bytes[8] == kOsWindows);
        const size_t end = bytes.size() - 6;                                                             // (protocol 16 ends the message with five more bytes: the plan of each colour and the seats that hold a game; N2.107 reads them; protocol 17 one more, the game mode, N2.112)
        ASSERT_TRUE(bytes[end] == 0 && bytes[end + 1] == 0 && bytes[end + 2] == 0 && bytes[end + 3] == 0 && bytes[end + 4] == 0 && bytes[end + 5] == 0);       // (a room that is no lobby room, and has the original's game mode, writes zeros)
        ASSERT_TRUE(bytes[end - 6] == 1 && bytes[end - 5] == 1 && bytes[end - 4] == 0 && bytes[end - 3] == 0 && bytes[end - 2] == 1 && bytes[end - 1] == 1);        // fog, you, leader, team_a, team_b, flags
        RoomMsg back;
        ASSERT_TRUE(decode(bytes, back));
        ASSERT_TRUE(back.slots[0].platform == kOsWindows && back.slots[1].platform == (kOsAndroid | kPlatformBrowser) && back.slots[2].platform == 0 && back.slots[3].platform == 0);
        ASSERT_TRUE(back.team_a == 0 && back.team_b == 1 && back.flags == kRoomLeaderStarts && back.leader_starts() && back.teams() == sim::StartTeams({true, 0, 1}));
        ASSERT_TRUE(encode(back) == bytes);
        ASSERT_FALSE(RoomMsg{}.leader_starts() || RoomMsg{}.teams().set);
        // the teams: every pair of byte values (free for all, or two seats and the lower one first), every flag byte (0, 1; protocol 16: 3, 7)
        {
            size_t pairs = 0;
            for (unsigned a = 0; a < 256; ++a) {
                for (unsigned b = 0; b < 256; ++b) {
                    std::vector<uint8_t> one = bytes;
                    one[end - 3] = static_cast<uint8_t>(a);
                    one[end - 2] = static_cast<uint8_t>(b);
                    RoomMsg got;
                    const bool ok = decode(one, got);
                    ASSERT_EQ(ok, (a == 255 && b == 255) || (a < b && b < 4));
                    if (ok) {
                        ASSERT_TRUE(got.team_a == a && got.team_b == b && encode(got) == one);
                        pairs += a == 255 ? 0u : 1u;
                    }
                }
            }
            ASSERT_EQ(pairs, size_t{6});
            for (unsigned flags = 0; flags < 256; ++flags) {
                std::vector<uint8_t> one = bytes;
                one[end - 1] = static_cast<uint8_t>(flags);
                RoomMsg got;
                ASSERT_EQ(decode(one, got), flags == 0 || flags == 1 || flags == 3 || flags == 7);          // (protocol 16: bit 1 = a lobby room, which the leader starts; bit 2 = it is starting, only in a lobby room)
            }
        }
        // the platform of a seat: a person (the host or a guest) tells an operating system or nothing; an empty seat and a bot tell nothing; a byte outside the table is no message
        {
            for (const SlotState state : {SlotState::Empty, SlotState::Host, SlotState::Client, SlotState::Bot}) {
                for (unsigned value = 0; value < 256; ++value) {
                    RoomMsg one;
                    one.slots[0] = {SlotState::Client, "Ann", 30, 0};
                    one.slots[1] = {state, state == SlotState::Empty ? "" : "Seat", 30, static_cast<uint8_t>(value)};
                    one.map_name = "TINY.LVL";
                    std::vector<uint8_t> raw = encode(one);                                              // (the encoder writes what it is given: the decoder is the rule)
                    RoomMsg got;
                    const bool person = state == SlotState::Host || state == SlotState::Client;
                    ASSERT_EQ(decode(raw, got), valid_platform(static_cast<uint8_t>(value)) && (person || value == 0));
                }
            }
        }
        // every strict prefix and every extra byte is refused; the layout of protocol 14 (no platform bytes, no teams and flags) is no Room message
        for (size_t cut = 0; cut < bytes.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
        for (unsigned extra = 0; extra < 256; ++extra) {
            std::vector<uint8_t> longer = bytes;
            longer.push_back(static_cast<uint8_t>(extra));
            ASSERT_FALSE(decode(longer, back));
        }
        {
            std::vector<uint8_t> old_layout = {12};
            for (const RoomMsg::Slot& slot : room.slots) {
                old_layout.push_back(static_cast<uint8_t>(slot.state));
                old_layout.push_back(static_cast<uint8_t>(slot.name.size()));
                for (const char c : slot.name) old_layout.push_back(static_cast<uint8_t>(c));
                old_layout.push_back(static_cast<uint8_t>(slot.rtt_ms & 0xFF));
                old_layout.push_back(static_cast<uint8_t>(slot.rtt_ms >> 8));
            }
            old_layout.push_back(8);
            for (const char c : std::string("TINY.LVL")) old_layout.push_back(static_cast<uint8_t>(c));
            for (const uint8_t b : {uint8_t{1}, uint8_t{1}, uint8_t{0}}) old_layout.push_back(b);
            ASSERT_FALSE(decode(old_layout, back));
        }
        // the leader of the room is still a person that holds a seat as a guest, whatever the new fields say
        {
            std::vector<uint8_t> one = bytes;
            one[end - 4] = 2;                                                                            // the leader's seat 2 is a bot
            ASSERT_FALSE(decode(one, back));
        }

        // ---- the Start: the platforms of the seats are its last four bytes before the mode (protocol 17: the very last byte) ----
        StartMsg start;
        start.seed = 77;
        start.map_name = "TINY.LVL";
        start.map_hash = 0x0102030405060708ull;
        start.roster = 0x07;
        start.names = {"Ann", "Bob", "Bot (Easy)", ""};
        start.platforms = {static_cast<uint8_t>(kOsMacos), static_cast<uint8_t>(kOsIos | kPlatformBrowser), 0, 0};
        start.set_teams(sim::StartTeams{true, 0, 1});
        const std::vector<uint8_t> sbytes = encode(start);
        const size_t send = sbytes.size() - 1;                                                           // (the end of the platforms: the mode byte follows, N2.112 reads it)
        ASSERT_TRUE(sbytes[0] == 13 && sbytes[send - 6] == 0 && sbytes[send - 5] == 1 && sbytes[send - 4] == kOsMacos && sbytes[send - 3] == (kOsIos | kPlatformBrowser) && sbytes[send - 2] == 0 && sbytes[send - 1] == 0 && sbytes[send] == 0);
        StartMsg sback;
        ASSERT_TRUE(decode(sbytes, sback) && sback.platforms == start.platforms && sback.teams() == sim::StartTeams({true, 0, 1}) && encode(sback) == sbytes);
        for (unsigned seat = 0; seat < 4; ++seat) {
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> one = sbytes;
                one[send - 4 + seat] = static_cast<uint8_t>(value);
                StartMsg got;
                const bool plays = (start.roster >> seat) & 1;
                ASSERT_EQ(decode(one, got), valid_platform(static_cast<uint8_t>(value)) && (plays || value == 0));       // (a seat that does not play says nothing; one that plays says an operating system or nothing)
            }
        }
        for (size_t cut = 0; cut < sbytes.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(sbytes.begin(), sbytes.begin() + static_cast<std::ptrdiff_t>(cut)), sback));
        {
            std::vector<uint8_t> old_layout(sbytes.begin(), sbytes.end() - 5);                           // protocol 14's Start: the same message without the four platform bytes (and the mode byte)
            ASSERT_FALSE(decode(old_layout, sback));
            old_layout.assign(sbytes.begin(), sbytes.end() - 1);                                         // protocol 15's and 16's Start: without the mode byte
            ASSERT_FALSE(decode(old_layout, sback));
            std::vector<uint8_t> longer = sbytes;
            longer.push_back(0);
            ASSERT_FALSE(decode(longer, sback));
        }
    } TEST_END();

    TEST_CASE("N2.104 Protocol 15, The Platform Byte And The Create Block As Values: The Words Of --platform And Of The Page's Address (windows, macos, linux, android, ios, other, And browser- In Front), What A Person Is Told (Windows, macOS, Linux, Android, iOS, Another System, And (Browser)), The Build's Own Platform, And The Block's Teams As A Pair In One Spelling") {
        struct Word {
            const char* text;
            uint8_t value;
        };
        const Word words[] = {{"windows", kOsWindows}, {"macos", kOsMacos}, {"linux", kOsLinux}, {"android", kOsAndroid}, {"ios", kOsIos}, {"other", kOsOther}};
        for (const Word& w : words) {
            uint8_t out = 0xEE;
            ASSERT_TRUE(parse_platform(w.text, out) && out == w.value);
            std::string upper = w.text;
            for (char& c : upper) c = static_cast<char>(c - 'a' + 'A');
            ASSERT_TRUE(parse_platform(upper, out) && out == w.value);                                  // (in any case)
            ASSERT_TRUE(parse_platform(std::string("browser-") + w.text, out) && out == (w.value | kPlatformBrowser));
            ASSERT_TRUE(parse_platform(std::string("Browser-") + upper, out) && out == (w.value | kPlatformBrowser));
            ASSERT_TRUE(valid_platform(out));
        }
        for (const std::string& bad : {std::string(), std::string("browser"), std::string("browser-"), std::string("browser-browser-linux"), std::string("win"), std::string("windows "), std::string(" windows"),
                                       std::string("windows,linux"), std::string("unknown"), std::string("0"), std::string("browser_linux"), std::string("browser linux"), std::string("windowsx"),
                                       std::string("--platform"), std::string("linux\n")}) {
            uint8_t out = 0xEE;
            ASSERT_FALSE(parse_platform(bad, out));
            ASSERT_EQ(out, 0xEE);                                                                         // (untouched)
        }
        // what a person reads: the system, and "(browser)" for a game in a page; nothing when the system was not told or the byte is not one
        ASSERT_EQ(platform_text(kOsWindows), std::string("Windows"));
        ASSERT_EQ(platform_text(kOsMacos), std::string("macOS"));
        ASSERT_EQ(platform_text(kOsLinux), std::string("Linux"));
        ASSERT_EQ(platform_text(kOsAndroid), std::string("Android"));
        ASSERT_EQ(platform_text(kOsIos), std::string("iOS"));
        ASSERT_EQ(platform_text(kOsOther), std::string("another system"));
        ASSERT_EQ(platform_text(static_cast<uint8_t>(kOsMacos | kPlatformBrowser)), std::string("macOS (browser)"));
        ASSERT_EQ(platform_text(static_cast<uint8_t>(kOsOther | kPlatformBrowser)), std::string("another system (browser)"));
        ASSERT_TRUE(platform_text(kPlatformUnknown).empty() && platform_text(kPlatformBrowser).empty());
        for (unsigned value = 0; value < 256; ++value) {
            if (!valid_platform(static_cast<uint8_t>(value))) ASSERT_TRUE(platform_text(static_cast<uint8_t>(value)).empty());
        }
        // every word that parses reads back as the text of its platform
        for (const Word& w : words) {
            uint8_t out = 0;
            ASSERT_TRUE(parse_platform(w.text, out));
            ASSERT_FALSE(platform_text(out).empty());
        }
        // this build says what it was built for (a browser build leaves the operating system to the page: kOsOther here)
        {
            const uint8_t own = native_platform();
            ASSERT_TRUE(valid_platform(own) && (own & 0x0Fu) != kPlatformUnknown);
#if defined(__EMSCRIPTEN__)
            ASSERT_EQ(own, static_cast<uint8_t>(kPlatformBrowser | kOsOther));
#else
            ASSERT_TRUE((own & kPlatformBrowser) == 0);
#endif
        }
        // the block as values: the pair is told in one spelling (the lower seat first), free for all is both bytes kNoTeam
        {
            CreateBlock block;
            ASSERT_TRUE(block.seats == 4 && block.team_a == kNoTeam && block.team_b == kNoTeam && block.flags == 0 && block.map_name.empty() && !block.teams().set && !block.leader_starts());
            ASSERT_TRUE(valid_create_block(block));
            for (uint8_t a = 0; a < 4; ++a) {
                for (uint8_t b = 0; b < 4; ++b) {
                    if (a == b) continue;
                    CreateBlock pair;
                    pair.set_teams(sim::StartTeams{true, a, b});
                    ASSERT_TRUE(pair.team_a == (a < b ? a : b) && pair.team_b == (a < b ? b : a) && pair.teams() == sim::StartTeams({true, pair.team_a, pair.team_b}));
                    ASSERT_TRUE(valid_create_block(pair));
                }
            }
            block.set_teams(sim::StartTeams{true, 3, 1});
            block.set_teams(sim::StartTeams{});
            ASSERT_TRUE(block.team_a == kNoTeam && block.team_b == kNoTeam && !block.teams().set);
            CreateBlock other = block;
            ASSERT_TRUE(other == block);
            other.flags = kCreateLeaderStarts;
            ASSERT_TRUE(other != block && other.leader_starts() && valid_create_block(other));
            for (const uint8_t seats : {uint8_t{0}, uint8_t{1}, uint8_t{5}, uint8_t{255}}) {
                CreateBlock bad;
                bad.seats = seats;
                ASSERT_FALSE(valid_create_block(bad));
            }
            for (const uint8_t flags : {uint8_t{2}, uint8_t{0x80}, uint8_t{0xFF}}) {
                CreateBlock bad;
                bad.flags = flags;
                ASSERT_FALSE(valid_create_block(bad));
            }
            CreateBlock lopsided;
            lopsided.team_a = 1;                                                                          // one team byte only
            ASSERT_FALSE(valid_create_block(lopsided));
            lopsided.team_b = 0;                                                                          // the higher seat first
            ASSERT_FALSE(valid_create_block(lopsided));
            lopsided.team_a = 0;
            lopsided.team_b = 4;                                                                          // a seat that the room cannot have
            ASSERT_FALSE(valid_create_block(lopsided));
            CreateBlock named;
            named.map_name = "TREASURE.LVL";
            ASSERT_TRUE(valid_create_block(named));
            named.map_name = "../x.LVL";
            ASSERT_FALSE(valid_create_block(named));
        }
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// Protocol 16: the lobby room (the client kind, the lobby block, the created flag, the plan of each colour)
// ---------------------------------------------------------------------------------------------------------------------------------

void run_protocol16_tests() {
    TEST_CASE("N2.105 Protocol 16, The Hello And The Welcome: The Client Kind (A Game Or A Lobby Page) Follows The Platform Byte And Leads The Create Block; The Lobby Flag Of The Block Needs A Leader Who Starts And All Four Colours; The Welcome Says When Its Hello Made The Room, And Only For A Seat That Has A Key; Every Other Value, Truncation And Extra Byte Is Refused, And The Layout Of Protocol 15 Is No Hello Of This Protocol") {
        ASSERT_EQ(kProtocolVersion, 20);
        ASSERT_TRUE(kClientGame == 0 && kClientPage == 1 && kCreateLeaderStarts == 1 && kCreateLobby == 2 && kWelcomeRejoin == 1 && kWelcomeCreated == 2);
        // a lobby page's Hello that makes a lobby room, byte by byte: the platform byte, the client kind, then the block (map, seats, team_a, team_b, flags)
        HelloMsg h;
        h.name = "Ann";
        h.room = "k7m2xq";
        h.platform = static_cast<uint8_t>(kOsMacos | kPlatformBrowser);
        h.client_kind = kClientPage;
        CreateBlock block;
        block.map_name = "TREASURE.LVL";
        block.flags = static_cast<uint8_t>(kCreateLeaderStarts | kCreateLobby);
        h.create = block;
        std::vector<uint8_t> want = {1, 20, 0, 3, 'A', 'n', 'n', 0, 0, 255, 6, 'k', '7', 'm', '2', 'x', 'q', 0};
        want.insert(want.end(), size_t{16 + 4}, uint8_t{0});                                           // the key and the turns: a new player has neither
        want.push_back(static_cast<uint8_t>(kOsMacos | kPlatformBrowser));
        want.push_back(1);                                                                              // the client kind: a lobby page
        want.push_back(12);
        for (const char c : std::string("TREASURE.LVL")) want.push_back(static_cast<uint8_t>(c));
        for (const uint8_t b : {uint8_t{4}, uint8_t{255}, uint8_t{255}, uint8_t{3}}) want.push_back(b);
        ASSERT_TRUE(encode(h) == want);
        HelloMsg back;
        ASSERT_TRUE(decode(want, back) && back.client_kind == kClientPage && back.create && *back.create == block && back.create->lobby() && back.create->leader_starts() && back.room == "k7m2xq");
        ASSERT_TRUE(encode(back) == want);
        // a Hello of a game that makes a room that is not a lobby room, and one that joins: the kind 0
        {
            HelloMsg game;
            game.name = "Bob";
            ASSERT_TRUE(game.client_kind == kClientGame);
            ASSERT_EQ(encode(game).back(), 0);
            ASSERT_TRUE(decode(encode(game), back) && back.client_kind == kClientGame && !back.create);
            game.client_kind = kClientPage;                                                             // a page that joins a room (it finds out there whether the room is a lobby room)
            ASSERT_TRUE(decode(encode(game), back) && back.client_kind == kClientPage && !back.create);
        }
        // the kind, of 256 values: a game and a page; every other byte is no Hello, with a block and without one
        for (unsigned kind = 0; kind < 256; ++kind) {
            std::vector<uint8_t> with_block = want;
            with_block[want.size() - 18] = static_cast<uint8_t>(kind);
            ASSERT_EQ(decode(with_block, back), kind <= 1);
            std::vector<uint8_t> without_block = want;
            without_block.resize(want.size() - 18 + 1);                                                 // (the kind is the last byte)
            ASSERT_EQ(without_block.back(), 1);
            without_block.back() = static_cast<uint8_t>(kind);
            ASSERT_EQ(decode(without_block, back), kind <= 1);
            if (kind <= 1) ASSERT_TRUE(static_cast<unsigned>(back.client_kind) == kind);
        }
        // the layout of protocol 15 (no client kind) is no Hello of this protocol: without a block the byte is missing, with one the block's map length is read as the kind
        {
            std::vector<uint8_t> v15(want.begin(), want.end());
            v15.erase(v15.begin() + static_cast<std::ptrdiff_t>(want.size() - 18));                     // the kind goes
            ASSERT_FALSE(decode(v15, back));
            v15.resize(v15.size() - 17);                                                                // ... and so does the block: protocol 15's Hello without one
            ASSERT_FALSE(decode(v15, back));
            ASSERT_TRUE(decode_hello_prefix(v15.data(), v15.size(), back) && back.name == "Ann");
        }
        // every strict prefix is refused except the two Hellos without a block: a prefix that ends after the client kind (the block is optional)
        {
            const size_t without_block = want.size() - 18 + 1;
            for (size_t cut = 0; cut < want.size(); ++cut) {
                HelloMsg keep;
                keep.name = "keep";
                const bool ok = decode(std::vector<uint8_t>(want.begin(), want.begin() + static_cast<std::ptrdiff_t>(cut)), keep);
                ASSERT_EQ(ok, cut == without_block);
                if (!ok) ASSERT_TRUE(keep.name == "keep" && !keep.client_kind && !keep.create);
            }
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = want;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
        }
        // the block: of every (seats, flags), a lobby block is one with the leader starting it and four seats; no other bit is used. The decoder reads the block with the same rule
        {
            size_t lobby_blocks = 0;
            for (unsigned seats = 0; seats < 256; ++seats) {
                for (unsigned flags = 0; flags < 256; ++flags) {
                    CreateBlock b;
                    b.seats = static_cast<uint8_t>(seats);
                    b.flags = static_cast<uint8_t>(flags);
                    const bool should = seats >= 2 && seats <= 4 && (flags <= 1 || (flags == 3 && seats == 4));
                    ASSERT_EQ(valid_create_block(b), should);
                    HelloMsg one = h;
                    one.create = b;
                    ASSERT_EQ(decode(encode(one), back), should);
                    if (should && b.lobby()) {
                        ++lobby_blocks;
                        ASSERT_TRUE(back.create->lobby() && back.create->leader_starts() && back.create->seats == 4);
                    }
                }
            }
            ASSERT_EQ(lobby_blocks, size_t{1});                                                         // 4 seats and flags 3: the one
            CreateBlock plain;
            ASSERT_FALSE(plain.lobby());
            plain.flags = kCreateLeaderStarts;
            ASSERT_FALSE(plain.lobby());
        }
        // the Welcome: bit 1 says that this Hello made the room. 0, 1 and 2 only, and 1 and 2 need a key
        {
            WelcomeMsg w;
            w.player = 0;
            w.players = 4;
            w.key = key_with(5);
            w.flags = kWelcomeCreated;
            const std::vector<uint8_t> bytes = encode(w);
            ASSERT_EQ(bytes.size(), size_t{20});
            ASSERT_TRUE(bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 4 && bytes[19] == 2);
            WelcomeMsg wb;
            ASSERT_TRUE(decode(bytes, wb) && wb.flags == kWelcomeCreated && wb.key == key_with(5) && encode(wb) == bytes);
            for (unsigned flags = 0; flags < 256; ++flags) {
                std::vector<uint8_t> one = bytes;
                one[19] = static_cast<uint8_t>(flags);
                ASSERT_EQ(decode(one, wb), flags <= 2);
            }
            for (const uint8_t flags : {uint8_t{1}, uint8_t{2}}) {                                      // no key: a LAN or direct host cannot make a room by a Hello, and cannot rejoin
                WelcomeMsg lan;
                lan.player = 1;
                lan.players = 4;
                lan.flags = flags;
                ASSERT_FALSE(decode(encode(lan), wb));
            }
            WelcomeMsg both = w;
            both.flags = static_cast<uint8_t>(kWelcomeRejoin | kWelcomeCreated);                        // a rejoin does not make a room
            ASSERT_FALSE(decode(encode(both), wb));
        }
    } TEST_END();

    TEST_CASE("N2.106 Protocol 16, The Plan: PlanMsg Is The Type 32, A Map (A Length And Its Name; Empty Keeps The Room's), A Kind 0 .. 4 For Each Colour And The Two Team Bytes (None Both, Or Two Seats With The Lower First); Every Kind, Every Pair Of Team Bytes, Every Truncation, Every Extra Byte And Every Other Type Is Decided By The Rule; A Refused Message Leaves Its Target Alone; The Kinds Of A Bot Are The Fill Levels Of START") {
        ASSERT_TRUE(static_cast<int>(MsgType::Plan) == 32 && kPlanKindLast == 4);
        ASSERT_TRUE(static_cast<int>(PlanKind::Open) == 0 && static_cast<int>(PlanKind::Easy) == 1 && static_cast<int>(PlanKind::Medium) == 2 && static_cast<int>(PlanKind::Hard) == 3 && static_cast<int>(PlanKind::Nobody) == 4);
        // the layout, byte by byte
        {
            PlanMsg m;
            m.map_name = "TREASURE.LVL";
            m.plan = {PlanKind::Open, PlanKind::Easy, PlanKind::Hard, PlanKind::Nobody};
            m.set_teams(sim::StartTeams{true, 3, 1});                                                   // (the lower seat first: a pair has one spelling)
            ASSERT_TRUE(m.team_a == 1 && m.team_b == 3 && m.teams() == sim::StartTeams({true, 1, 3}));
            std::vector<uint8_t> want = {32, 12};
            for (const char c : std::string("TREASURE.LVL")) want.push_back(static_cast<uint8_t>(c));
            for (const uint8_t b : {uint8_t{0}, uint8_t{1}, uint8_t{3}, uint8_t{4}, uint8_t{1}, uint8_t{3}, uint8_t{0}}) want.push_back(b);        // (protocol 17: the game mode is the last byte, N2.112)
            ASSERT_TRUE(encode(m) == want);
            ASSERT_EQ(peek_type(want), MsgType::Plan);
            PlanMsg back;
            ASSERT_TRUE(decode(want, back) && back.map_name == "TREASURE.LVL" && back.plan == m.plan && back.team_a == 1 && back.team_b == 3);
            ASSERT_TRUE(encode(back) == want);
            PlanMsg none;                                                                               // the map stays the room's, every colour is open, free for all
            ASSERT_TRUE(encode(none) == (std::vector<uint8_t>{32, 0, 0, 0, 0, 0, 255, 255, 0}));
            ASSERT_TRUE(decode(encode(none), back) && back.map_name.empty() && !back.teams().set);
            m.set_teams(sim::StartTeams{});
            ASSERT_TRUE(m.team_a == kNoTeam && m.team_b == kNoTeam && !m.teams().set);
            // every strict prefix and every extra byte of the whole message is refused, and the target is left alone
            for (size_t cut = 0; cut < want.size(); ++cut) {
                PlanMsg keep;
                keep.map_name = "keep";
                ASSERT_FALSE(decode(std::vector<uint8_t>(want.begin(), want.begin() + static_cast<std::ptrdiff_t>(cut)), keep));
                ASSERT_TRUE(keep.map_name == "keep" && keep.plan == PlanMsg{}.plan);
            }
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = want;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
            // no other type byte is a PlanMsg, and the PlanMsg is no other message
            for (unsigned type = 0; type < 256; ++type) {
                if (type == 32) continue;
                std::vector<uint8_t> other = want;
                other[0] = static_cast<uint8_t>(type);
                ASSERT_FALSE(decode(other, back));
            }
            ASSERT_TRUE(any_decodes(want));
            SeatMoveMsg move;
            StartRequestMsg request;
            RoomMsg room;
            HelloMsg hello;
            ASSERT_FALSE(decode(want, move) || decode(want, request) || decode(want, room) || decode(want, hello));
        }
        // the kinds: 625 plans, each one round-trips; of 256 values in each of the four places, 0 .. 4 are a plan
        {
            for (unsigned code = 0; code < 625; ++code) {
                PlanMsg m;
                unsigned rest = code;
                for (PlanKind& kind : m.plan) {
                    kind = static_cast<PlanKind>(rest % 5u);
                    rest /= 5u;
                }
                PlanMsg back;
                const std::vector<uint8_t> bytes = encode(m);
                ASSERT_TRUE(decode(bytes, back) && back.plan == m.plan && encode(back) == bytes);
            }
            const std::vector<uint8_t> base = encode(PlanMsg{});
            for (size_t seat = 0; seat < 4; ++seat) {
                for (unsigned value = 0; value < 256; ++value) {
                    std::vector<uint8_t> one = base;
                    one[2 + seat] = static_cast<uint8_t>(value);
                    PlanMsg got;
                    ASSERT_EQ(decode(one, got), value <= kPlanKindLast);
                    if (value <= kPlanKindLast) ASSERT_TRUE(static_cast<unsigned>(got.plan[seat]) == value && encode(got) == one);
                }
            }
        }
        // the teams: of the 65536 pairs of bytes, free for all and the six pairs of two seats with the lower first
        {
            const std::vector<uint8_t> base = encode(PlanMsg{});
            size_t pairs = 0;
            for (unsigned a = 0; a < 256; ++a) {
                for (unsigned b = 0; b < 256; ++b) {
                    std::vector<uint8_t> one = base;
                    one[6] = static_cast<uint8_t>(a);
                    one[7] = static_cast<uint8_t>(b);
                    PlanMsg got;
                    const bool ok = decode(one, got);
                    ASSERT_EQ(ok, (a == 255 && b == 255) || (a < b && b < 4));
                    if (ok) {
                        ASSERT_TRUE(got.team_a == a && got.team_b == b && encode(got) == one);
                        pairs += a == 255 ? 0u : 1u;
                    }
                }
            }
            ASSERT_EQ(pairs, size_t{6});
        }
        // the map: "" (the room keeps its own), a map's file name, the longest one; not a name that could name a path
        {
            const auto with_map = [](const std::string& map) {
                std::vector<uint8_t> bytes = {32, static_cast<uint8_t>(map.size())};
                bytes.insert(bytes.end(), map.begin(), map.end());
                for (const uint8_t b : {uint8_t{0}, uint8_t{0}, uint8_t{0}, uint8_t{0}, uint8_t{255}, uint8_t{255}, uint8_t{0}}) bytes.push_back(b);
                return bytes;
            };
            PlanMsg got;
            ASSERT_TRUE(decode(with_map(""), got) && got.map_name.empty());
            ASSERT_TRUE(decode(with_map("TINY.LVL"), got) && got.map_name == "TINY.LVL");
            ASSERT_TRUE(decode(with_map(std::string(kMaxMapNameChars - 4, 'x') + ".LVL"), got));
            for (const std::string& bad : {std::string("tiny.txt"), std::string("a/b.LVL"), std::string("..\\x.LVL"), std::string(".hidden.LVL"), std::string(kMaxMapNameChars - 3, 'x') + ".LVL", std::string("x.LVL\x01"), std::string("c:x.LVL")}) {
                PlanMsg keep;
                keep.map_name = "keep";
                ASSERT_FALSE(decode(with_map(bad), keep));
                ASSERT_EQ(keep.map_name, std::string("keep"));
            }
        }
        // a bot of the plan is a bot of the fill: Easy, Medium and Hard are the levels of START, Open and Nobody seat none
        ASSERT_TRUE(plan_fill_level(PlanKind::Open) == FillLevel::None && plan_fill_level(PlanKind::Easy) == FillLevel::Easy && plan_fill_level(PlanKind::Medium) == FillLevel::Medium &&
                    plan_fill_level(PlanKind::Hard) == FillLevel::Hard && plan_fill_level(PlanKind::Nobody) == FillLevel::None);
    } TEST_END();

    TEST_CASE("N2.107 Protocol 16, The Room: The Message Ends With The Plan Of Each Colour (0 .. 4) And The Seats That Hold A Game, Its Rule Bits Say A Lobby Room (Which The Leader Starts) That Is Starting; A Plan, A Game Or A Starting Room That No Honest Server Writes Is Refused, Every Truncation And Extra Byte Too, And The Layout Of Protocol 15 Is No Room Message") {
        RoomMsg room = server_room_of(0);                                                               // seats 0, 1 and 3 hold guests, seat 2 is empty
        room.flags = static_cast<uint8_t>(kRoomLeaderStarts | kRoomLobby);
        room.plan = {PlanKind::Open, PlanKind::Hard, PlanKind::Easy, PlanKind::Nobody};
        room.in_game = 0x03;                                                                            // the persons of seats 0 and 1 run a game, the one of seat 3 a page
        const std::vector<uint8_t> bytes = encode(room);
        const size_t end = bytes.size() - 1;                                                            // (protocol 17 ends the message with the game mode, N2.112; the lobby bytes are the five before it)
        ASSERT_TRUE(kRoomLobby == 2 && kRoomStarting == 4 && kRoomLeaderStarts == 1 && bytes.back() == 0);
        ASSERT_TRUE(bytes[end - 6] == 3 && bytes[end - 5] == 0 && bytes[end - 4] == 3 && bytes[end - 3] == 1 && bytes[end - 2] == 4 && bytes[end - 1] == 3);       // the flags, the four kinds, the games
        RoomMsg back;
        ASSERT_TRUE(decode(bytes, back));
        ASSERT_TRUE(back.lobby() && back.leader_starts() && !back.starting() && back.plan == room.plan && back.in_game == 3);
        ASSERT_TRUE(back.seat_in_game(0) && back.seat_in_game(1) && !back.seat_in_game(2) && !back.seat_in_game(3) && !back.seat_in_game(4) && !back.seat_in_game(255));
        ASSERT_TRUE(encode(back) == bytes);
        ASSERT_FALSE(RoomMsg{}.lobby() || RoomMsg{}.starting() || RoomMsg{}.seat_in_game(0));
        room.flags = static_cast<uint8_t>(kRoomLeaderStarts | kRoomLobby | kRoomStarting);
        ASSERT_TRUE(decode(encode(room), back) && back.starting() && back.lobby());
        // the kinds, in each place: 0 .. 4 in a lobby room; a plan in a room that is not a lobby room is no message
        for (size_t seat = 0; seat < 4; ++seat) {
            for (unsigned value = 0; value < 256; ++value) {
                std::vector<uint8_t> one = bytes;
                one[end - 5 + seat] = static_cast<uint8_t>(value);
                ASSERT_EQ(decode(one, back), value <= kPlanKindLast);
                std::vector<uint8_t> plain = bytes;                                                     // the same message, but of a room that is no lobby room
                plain[end - 6] = kRoomLeaderStarts;
                plain[end - 5 + seat] = static_cast<uint8_t>(value);
                for (size_t other = 0; other < 4; ++other) {
                    if (other != seat) plain[end - 5 + other] = 0;
                }
                plain[end - 1] = 0;
                ASSERT_EQ(decode(plain, back), value == 0);
            }
        }
        // the games: only a seat that a person holds as a guest (the seats 0, 1 and 3 here), and no bit above the fourth
        for (unsigned value = 0; value < 256; ++value) {
            std::vector<uint8_t> one = bytes;
            one[end - 1] = static_cast<uint8_t>(value);
            ASSERT_EQ(decode(one, back), (value & ~0x0Bu) == 0);
            if ((value & ~0x0Bu) == 0) ASSERT_TRUE(static_cast<unsigned>(back.in_game) == value && encode(back) == one);
        }
        for (const SlotState state : {SlotState::Host, SlotState::Bot, SlotState::Empty}) {            // the games of seat 2 when it holds no guest: nobody runs a game in it
            RoomMsg odd = room;
            odd.flags = static_cast<uint8_t>(kRoomLeaderStarts | kRoomLobby);
            odd.slots[2] = {state, state == SlotState::Empty ? "" : "Seat", 0};
            odd.in_game = 0x04;
            ASSERT_FALSE(decode(encode(odd), back));
            odd.in_game = 0x01;
            ASSERT_TRUE(decode(encode(odd), back));
        }
        // the rule bits: of the eight values, the room's own (0, 1), a lobby room that the leader starts (3) and one that is starting (7)
        for (unsigned flags = 0; flags < 256; ++flags) {
            std::vector<uint8_t> one = bytes;
            one[end - 6] = static_cast<uint8_t>(flags);
            const bool plan_free = flags == 0 || flags == 1;                                            // (a room that is no lobby room has no plan and no games: this message has both)
            const bool ok = flags == 3 || flags == 7;
            ASSERT_EQ(decode(one, back), ok);
            one[end - 5] = one[end - 4] = one[end - 3] = one[end - 2] = one[end - 1] = 0;
            ASSERT_EQ(decode(one, back), plan_free || ok);
        }
        // every strict prefix and every extra byte is refused; the layout of protocol 15 (the same message without its last five bytes) is no Room message of this protocol
        for (size_t cut = 0; cut < bytes.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), back));
        for (unsigned extra = 0; extra < 256; ++extra) {
            std::vector<uint8_t> longer = bytes;
            longer.push_back(static_cast<uint8_t>(extra));
            ASSERT_FALSE(decode(longer, back));
        }
        // the seating hash is the state and the name of each seat and nothing else: a plan and a game do not change it (SeatMove's guard is the seating of the room)
        {
            RoomMsg a = room;
            RoomMsg b = room;
            b.plan = {PlanKind::Nobody, PlanKind::Open, PlanKind::Open, PlanKind::Open};
            b.in_game = 0;
            b.flags = kRoomLeaderStarts;
            ASSERT_EQ(seating_hash(a), seating_hash(b));
        }
    } TEST_END();

    TEST_CASE("N2.108 Protocol 16, The Name: NameMsg Is The Type 33, A Length Byte And A Name Of 1 .. 32 Printable Characters (0x20 .. 0x7E) That Neither Starts Nor Ends With A Space; Every Other Name, Every Truncation, Every Extra Byte And Every Other Type Is Refused And Leaves Its Target Alone; The Encoder Cuts A Longer Name To 32 Characters And Drops The Characters That Do Not Travel") {
        ASSERT_TRUE(static_cast<int>(MsgType::Name) == 33 && static_cast<int>(MsgType::Last) == static_cast<int>(MsgType::Remove) && kMaxNameChars == 32);
        // the layout, byte by byte
        const std::vector<uint8_t> want = {33, 3, 'A', 'n', 'n'};
        {
            NameMsg m;
            m.name = "Ann";
            ASSERT_TRUE(encode(m) == want);
            ASSERT_EQ(peek_type(want), MsgType::Name);
            NameMsg back;
            ASSERT_TRUE(decode(want, back) && back.name == "Ann");
            ASSERT_TRUE(encode(back) == want);
        }
        // the names that travel: one character, spaces inside, the longest, and every printable character in the middle of a name
        {
            NameMsg back;
            for (const std::string& name : {std::string("A"), std::string("Ann Lee"), std::string("a  b"), std::string(kMaxNameChars, 'x'), std::string("~!#&"), std::string("Robot (Easy)")}) {
                NameMsg m;
                m.name = name;
                const std::vector<uint8_t> bytes = encode(m);
                ASSERT_TRUE(bytes.size() == 2 + name.size() && bytes[0] == 33 && bytes[1] == name.size());
                ASSERT_TRUE(decode(bytes, back) && back.name == name);
            }
            for (unsigned c = 0x20; c <= 0x7E; ++c) {
                const std::vector<uint8_t> bytes = {33, 3, 'a', static_cast<uint8_t>(c), 'b'};
                ASSERT_TRUE(decode(bytes, back));
                ASSERT_EQ(back.name.size(), 3u);
            }
            for (unsigned c = 0; c < 256; ++c) {                                                        // a character outside the table is refused wherever it stands
                if (c >= 0x20 && c <= 0x7E) continue;
                ASSERT_FALSE(decode(std::vector<uint8_t>{33, 3, 'a', static_cast<uint8_t>(c), 'b'}, back));
                ASSERT_FALSE(decode(std::vector<uint8_t>{33, 1, static_cast<uint8_t>(c)}, back));
            }
        }
        // the names that are refused: none, a space at an end (a name has one spelling), spaces only, one character too many; each leaves its target alone
        {
            NameMsg keep;
            keep.name = "keep";
            const std::vector<std::vector<uint8_t>> refused = {
                {33, 0},                                                                                // nothing
                {33, 4, ' ', 'A', 'n', 'n'}, {33, 4, 'A', 'n', 'n', ' '}, {33, 1, ' '}, {33, 3, ' ', ' ', ' '},
                {33, 2, ' ', 'a'}, {33, 2, 'a', ' '},
            };
            for (const std::vector<uint8_t>& bytes : refused) {
                ASSERT_FALSE(decode(bytes, keep));
                ASSERT_TRUE(keep.name == "keep");
            }
            std::vector<uint8_t> too_long = {33, static_cast<uint8_t>(kMaxNameChars + 1)};
            too_long.insert(too_long.end(), kMaxNameChars + 1, 'x');
            ASSERT_FALSE(decode(too_long, keep));
            ASSERT_TRUE(keep.name == "keep");
            for (unsigned len = 0; len < 256; ++len) {                                                  // a length that is not the name's: the message is refused whatever it says
                std::vector<uint8_t> bytes = want;
                bytes[1] = static_cast<uint8_t>(len);
                ASSERT_EQ(decode(bytes, keep), len == 3);
            }
            ASSERT_TRUE(keep.name == "Ann");                                                            // (the one that was right was taken)
        }
        // the encoder: a longer name is cut to kMaxNameChars, what does not travel is dropped (a tab, a character outside the table), and nothing is sent that the decoder would refuse
        {
            NameMsg m;
            m.name = std::string(40, 'y');
            const std::vector<uint8_t> cut = encode(m);
            ASSERT_TRUE(cut.size() == 2 + kMaxNameChars && cut[1] == kMaxNameChars);
            m.name = std::string("A\tn\xC3\xA9n");
            ASSERT_TRUE(encode(m) == want);
            NameMsg back;
            ASSERT_TRUE(decode(encode(m), back) && back.name == "Ann");
        }
        // every strict prefix and every extra byte is refused; no other type byte is a NameMsg, and the NameMsg is no other message
        {
            NameMsg back;
            for (size_t cut = 0; cut < want.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(want.begin(), want.begin() + static_cast<std::ptrdiff_t>(cut)), back));
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = want;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, back));
            }
            for (unsigned type = 0; type < 256; ++type) {
                if (type == 33) continue;
                std::vector<uint8_t> other = want;
                other[0] = static_cast<uint8_t>(type);
                ASSERT_FALSE(decode(other, back));
            }
            PlanMsg plan;
            ChatMsg chat;
            HelloMsg hello;
            SeatMoveMsg move;
            ASSERT_FALSE(decode(want, plan) || decode(want, chat) || decode(want, hello) || decode(want, move));
            ASSERT_FALSE(decode(encode(PlanMsg{}), back) || decode(encode(ChatMsg{1, false, "Ann"}), back) || decode(encode(SeatMoveMsg{0, 1, 7}), back));
        }
    } TEST_END();

    TEST_CASE("N2.109 The Server's Sentences (Protocol 16): The Words That The Room Says To A Player Are Printed As They Are By The Front Page (Its Picture Of Every Message Shows Them), So They Are Pinned Word For Word; Each Whole Sentence Fits In A Line Of Chat (100 Printable Characters) And Survives The Wire") {
        const std::vector<std::pair<std::string, std::string>> words = {
            {kNoticeFillFog, "Bots cannot play with Fog of War."},
            {kNoticeFillMap, "This map cannot be played by every seat: no bots in the empty seats."},
            {kNoticeNoTeams, "No teams: "},                                                    // (the start of a line: the reason follows)
            {kNoticeGameLate, "'s game did not come in time."},                                  // (the end of a line: a name comes before it)
            {kNoticeGamesLate, "These games did not come in time: "},                            // (the start of a line: the names follow)
            {kNoticeMapLost, "This map could not be loaded: choose another map."},
            {kNoticeMapColours, "This map cannot be played with these colours."},
            {kNoticeNoPlace, "The server has no place for another match right now: try again in a minute."},
            {kNoticeStartsFailed, "The match could not start a few times in a row: try again in a minute."},
        };
        for (const auto& w : words) {
            ASSERT_TRUE(w.first == w.second);
            ASSERT_TRUE(w.first.size() <= kMaxChatChars);
            ASSERT_TRUE(w.first.find_first_not_of(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~") == std::string::npos);
            if (w.first.back() != '.') continue;                                                // (a whole sentence: one that the room says as it is)
            ChatMsg notice;
            notice.sender = kRoomSender;
            notice.text = w.first;
            ChatMsg back;
            ASSERT_TRUE(decode(encode(notice), back) && back.sender == kRoomSender && !back.team && back.text == w.first);
        }
    } TEST_END();

    TEST_CASE("N2.110 Protocol 16, The Removal: RemoveMsg Is The Type 34, A Seat 0 .. 3 And The Guard (A 32-Bit Number That Is Never 0, Little Endian), Six Bytes And Nothing Else; Every Other Seat, A Guard Of 0, Every Truncation, Every Extra Byte And Every Other Type Is Refused And Leaves Its Target Alone") {
        ASSERT_TRUE(static_cast<int>(MsgType::Remove) == 34 && static_cast<int>(MsgType::Last) == static_cast<int>(MsgType::Remove) && static_cast<int>(MsgType::Name) == 33);
        // the layout, byte by byte: the type, the seat of the person who goes, the guard (little endian)
        ASSERT_TRUE(encode(RemoveMsg{2, 0x04030201u}) == (std::vector<uint8_t>{34, 2, 1, 2, 3, 4}));
        ASSERT_TRUE(encode(RemoveMsg{0, 0xFFFFFFFFu}) == (std::vector<uint8_t>{34, 0, 255, 255, 255, 255}));
        ASSERT_TRUE(encode(RemoveMsg{3, 1}) == (std::vector<uint8_t>{34, 3, 1, 0, 0, 0}));
        ASSERT_EQ(peek_type(encode(RemoveMsg{1, 7})), MsgType::Remove);
        // every seat of the room, and a guard of every size, make the trip
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            for (const uint32_t guard : {1u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u, 0x01000000u, 0xA1B2C3D4u, 0xFFFFFFFFu}) {
                RemoveMsg got;
                const std::vector<uint8_t> bytes = encode(RemoveMsg{seat, guard});
                ASSERT_TRUE(bytes.size() == 6 && decode(bytes, got) && got.seat == seat && got.guard == guard);
                ASSERT_TRUE(encode(got) == bytes);
            }
        }
        {   // the refusals: a seat that the room does not have (every byte value from 4 up) and a guard of 0, which is not a number that was computed; each leaves its target alone
            RemoveMsg keep{7, 9};
            for (unsigned seat = sim::MAX_PLAYERS; seat < 256; ++seat) {
                ASSERT_FALSE(decode(std::vector<uint8_t>{34, static_cast<uint8_t>(seat), 1, 0, 0, 0}, keep));
                ASSERT_FALSE(decode(std::vector<uint8_t>{34, static_cast<uint8_t>(seat), 0xD4, 0xC3, 0xB2, 0xA1}, keep));
            }
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) ASSERT_FALSE(decode(std::vector<uint8_t>{34, seat, 0, 0, 0, 0}, keep));
            ASSERT_TRUE(keep.seat == 7 && keep.guard == 9);
        }
        {   // every strict prefix and every extra byte is refused; no other type byte is a RemoveMsg, and the RemoveMsg is no other message
            const std::vector<uint8_t> full = encode(RemoveMsg{1, 0xA1B2C3D4u});
            RemoveMsg keep{7, 9};
            for (size_t cut = 0; cut < full.size(); ++cut) ASSERT_FALSE(decode(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut)), keep));
            for (unsigned extra = 0; extra < 256; ++extra) {
                std::vector<uint8_t> longer = full;
                longer.push_back(static_cast<uint8_t>(extra));
                ASSERT_FALSE(decode(longer, keep));
            }
            for (unsigned type = 0; type < 256; ++type) {
                if (type == 34) continue;
                std::vector<uint8_t> other = full;
                other[0] = static_cast<uint8_t>(type);
                ASSERT_FALSE(decode(other, keep));
            }
            ASSERT_TRUE(keep.seat == 7 && keep.guard == 9);
            SeatMoveMsg move;
            NameMsg name;
            PlanMsg plan;
            ASSERT_FALSE(decode(full, move) || decode(full, name) || decode(full, plan));
            ASSERT_FALSE(decode(encode(SeatMoveMsg{0, 1, 7}), keep) || decode(encode(NameMsg{"Ann"}), keep) || decode(encode(PlanMsg{}), keep) || decode(encode(StartRequestMsg{}), keep));
            ASSERT_TRUE(keep.seat == 7 && keep.guard == 9);
        }
        {   // the guard of a removal is the guard of a colour move: the number that seating_hash gives for the seats as the leader's screen shows them (N2.107 pins what it is made of)
            RoomMsg room = server_room_of(0);
            RemoveMsg got;
            ASSERT_TRUE(decode(encode(RemoveMsg{1, seating_hash(room)}), got) && got.guard == seating_hash(room) && got.guard != 0);
            RoomMsg other = room;
            other.slots[1].name += "x";
            ASSERT_TRUE(seating_hash(other) != seating_hash(room));
        }
    } TEST_END();

    TEST_CASE("N2.111 Protocol 16 And 17, The Lobby Page's Messages: tests/data/lobby_messages.txt Holds Byte For Byte What The Encoders Write For What A Lobby Page Sends (The Hello, The Colour Move, The Plan, The Name, The Removal, START) And Hears (Welcome, Reject, Room, Chat, Ping); Each Line Is Made From Its Fields, Decodes And Encodes Again To The Same Bytes, And A Room's Guard Is Its seating_hash (The Page's Codec Is Held To The Same File By tests/scripts/web_lobby_net_check.js)") {
        struct Sample {
            std::string kind;
            std::string name;
            std::vector<uint8_t> bytes;
        };
        std::vector<Sample> samples;
        const auto add = [&samples](const char* kind, const char* name, std::vector<uint8_t> bytes) { samples.push_back(Sample{kind, name, std::move(bytes)}); };
        const auto key_of = [](uint8_t first) {
            SeatKey k{};
            for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(first + i);
            return k;
        };
        const auto seat = [](SlotState st, const char* name, uint16_t rtt, uint8_t platform) {
            RoomMsg::Slot s;
            s.state = st;
            s.name = name;
            s.rtt_ms = rtt;
            s.platform = platform;
            return s;
        };
        const uint8_t lobby_block = kCreateLeaderStarts | kCreateLobby;
        {   // what a page says first: its Hello (client kind 1) with the lobby block, with and without a key, with a platform, and without any block
            HelloMsg h;
            h.name = "Priya"; h.room = "k7m2xq"; h.client_kind = kClientPage; h.create = CreateBlock{"TREASURE.LVL", 4, kNoTeam, kNoTeam, lobby_block};
            add("hello", "page-new-room", encode(h));
            HelloMsg k;
            k.name = "Sam"; k.room = "abc234"; k.key = key_of(1); k.client_kind = kClientPage; k.platform = kPlatformBrowser | kOsLinux; k.create = CreateBlock{"", 4, kNoTeam, kNoTeam, lobby_block};
            add("hello", "page-with-key", encode(k));
            HelloMsg n;
            n.room = "xx"; n.client_kind = kClientPage;
            add("hello", "page-no-block", encode(n));
            HelloMsg g;
            g.name = "Bob"; g.room = "p_q-9"; g.client_kind = kClientGame;
            add("hello", "game-joins", encode(g));
        }
        add("leave", "leave", encode_leave());
        add("pong", "pong", encode_pong(PingMsg{0x01020304u, 0x0A0B0C0Du}));
        add("ping", "ping", encode_ping(PingMsg{0xFFFFFFFFu, 7u}));
        add("seatmove", "move-1-to-3", encode(SeatMoveMsg{1, 3, 0xDEADBEEFu}));
        add("seatmove", "move-3-to-0", encode(SeatMoveMsg{3, 0, 1u}));
        {
            PlanMsg a;
            a.map_name = "ISLANDS.LVL"; a.plan = {PlanKind::Open, PlanKind::Easy, PlanKind::Hard, PlanKind::Nobody};
            add("plan", "plan-ffa", encode(a));
            PlanMsg b;
            b.plan = {PlanKind::Open, PlanKind::Medium, PlanKind::Open, PlanKind::Medium}; b.team_a = 0; b.team_b = 1;
            add("plan", "plan-teams-keep-map", encode(b));
            PlanMsg c;
            c.map_name = "TREASURE.LVL"; c.plan = {PlanKind::Nobody, PlanKind::Nobody, PlanKind::Open, PlanKind::Open}; c.team_a = 2; c.team_b = 3;
            add("plan", "plan-teams-2-3", encode(c));
            PlanMsg d;
            d.map_name = "TINY.LVL"; d.plan = {PlanKind::Open, PlanKind::Hard, PlanKind::Open, PlanKind::Nobody}; d.mode = 1;           // (protocol 17: the game mode "187")
            add("plan", "plan-187", encode(d));
        }
        add("name", "name-plain", encode(NameMsg{"Priya"}));
        add("name", "name-32-chars-with-spaces", encode(NameMsg{"Sam the Great, 3rd of his name!!"}));
        add("name", "name-one-char", encode(NameMsg{"Z"}));
        add("remove", "remove-2", encode(RemoveMsg{2, 1u}));
        add("remove", "remove-0", encode(RemoveMsg{0, 0xFFFFFFFFu}));
        {
            StartRequestMsg a;
            a.fill = {FillLevel::None, FillLevel::Easy, FillLevel::Medium, FillLevel::Hard}; a.team_a = 0; a.team_b = 1;
            add("startrequest", "start-teams", encode(a));
            add("startrequest", "start-ffa-nobody", encode(StartRequestMsg{}));
        }
        add("chat", "chat-plain", encode(ChatMsg{0, false, "hello there"}));
        {   // what a page hears
            WelcomeMsg created;
            created.player = 0; created.players = 4; created.key = key_of(0x11); created.flags = kWelcomeCreated;
            add("welcome", "welcome-created", encode(created));
            WelcomeMsg joined;
            joined.player = 2; joined.players = 4; joined.key = key_of(0xA0);
            add("welcome", "welcome-joined", encode(joined));
            WelcomeMsg back;
            back.player = 3; back.players = 4; back.key = key_of(0x40); back.flags = kWelcomeRejoin;
            add("welcome", "welcome-rejoin", encode(back));
        }
        {
            const char* const reasons[] = {"reject-full", "reject-version", "reject-match-running", "reject-kicked", "reject-bad-request", "reject-no-such-room", "reject-dropped", "reject-rejoin-failed", "reject-superseded"};
            for (int r = 1; r <= 9; ++r) add("reject", reasons[r - 1], encode(RejectMsg{static_cast<RejectReason>(r)}));
        }
        std::vector<std::pair<std::string, RoomMsg>> rooms;
        {
            RoomMsg m;
            m.slots[0] = seat(SlotState::Client, "Priya", 12, 0); m.slots[1] = seat(SlotState::Client, "Sam", 40, kPlatformBrowser | kOsMacos); m.slots[2] = seat(SlotState::Bot, "Bot (Medium)", 0, 0); m.slots[3] = seat(SlotState::Empty, "", kRttUnknown, 0);
            m.map_name = "TREASURE.LVL"; m.you = 1; m.leader = 0; m.flags = kRoomLeaderStarts | kRoomLobby; m.plan = {PlanKind::Open, PlanKind::Open, PlanKind::Medium, PlanKind::Open};
            rooms.emplace_back("room-lobby-waiting", m);
            RoomMsg s;
            s.slots[0] = seat(SlotState::Client, "Priya", 12, 0); s.slots[1] = seat(SlotState::Client, "Sam", 40, 0); s.slots[2] = seat(SlotState::Client, "Juniper", kRttUnknown, 0); s.slots[3] = seat(SlotState::Bot, "Bot (Hard)", 0, 0);
            s.map_name = "ISLANDS.LVL"; s.you = 2; s.leader = 1; s.team_a = 0; s.team_b = 2; s.flags = kRoomLeaderStarts | kRoomLobby | kRoomStarting; s.plan = {PlanKind::Open, PlanKind::Open, PlanKind::Open, PlanKind::Hard}; s.in_game = 0x05;
            rooms.emplace_back("room-lobby-starting", s);
            RoomMsg a;
            a.slots[0] = seat(SlotState::Client, "Priya", 12, 0); a.slots[1] = seat(SlotState::Empty, "", kRttUnknown, 0); a.slots[2] = seat(SlotState::Empty, "", kRttUnknown, 0); a.slots[3] = seat(SlotState::Empty, "", kRttUnknown, 0);
            a.you = 0; a.leader = 0; a.flags = kRoomLeaderStarts | kRoomLobby; a.plan = {PlanKind::Open, PlanKind::Nobody, PlanKind::Easy, PlanKind::Open};
            rooms.emplace_back("room-lobby-alone-no-map", a);
            RoomMsg p;
            p.slots[0] = seat(SlotState::Client, "A", kRttUnknown, 0); p.slots[1] = seat(SlotState::Client, "B", 5, 0); p.slots[2] = seat(SlotState::Client, "C", 6, 0); p.slots[3] = seat(SlotState::Client, "D", 7, 0);
            p.map_name = "SMALL.LVL"; p.you = 255; p.leader = kNoLeader; p.team_a = 0; p.team_b = 1;
            rooms.emplace_back("room-plain-not-a-lobby", p);
            RoomMsg g;
            g.slots[0] = seat(SlotState::Client, "Priya", 12, 0); g.slots[1] = seat(SlotState::Client, "Sam", 40, 0); g.slots[2] = seat(SlotState::Empty, "", kRttUnknown, 0); g.slots[3] = seat(SlotState::Empty, "", kRttUnknown, 0);
            g.map_name = "TINY.LVL"; g.you = 0; g.leader = 0; g.flags = kRoomLeaderStarts | kRoomLobby; g.plan = {PlanKind::Open, PlanKind::Open, PlanKind::Open, PlanKind::Open}; g.mode = 1;       // (protocol 17: the game mode "187")
            rooms.emplace_back("room-lobby-187", g);
        }
        for (const auto& r : rooms) add("room", r.first.c_str(), encode(r.second));
        add("chat", "chat-notice-from-room", encode(ChatMsg{kRoomSender, false, "Priya's game did not come in time."}));
        add("chat", "chat-notice-fits", encode(ChatMsg{kRoomSender, false, std::string(kNoticeStartsFailed)}));
        add("other", "begin", encode_begin());
        add("ping", "ping-from-server", encode_ping(PingMsg{12345u, 99999u}));

        const auto hex = [](const std::vector<uint8_t>& bytes) {
            static const char* const digits = "0123456789abcdef";
            std::string s;
            for (const uint8_t b : bytes) {
                s.push_back(digits[b >> 4]);
                s.push_back(digits[b & 15]);
            }
            return s;
        };
        if (std::getenv("ANTS_WRITE_LOBBY_MESSAGES") != nullptr) {      // the file again after a layout changed: redirect this output to tests/data/lobby_messages.txt (the header comment is kept by hand)
            std::cout << "\n";
            for (const Sample& s : samples) std::cout << s.kind << " " << s.name << " " << hex(s.bytes) << "\n";
            for (const auto& r : rooms) {
                char buf[16];
                std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(seating_hash(r.second)));
                std::cout << "hash " << r.first << " " << buf << "\n";
            }
            return;
        }
        // the file, line by line: <kind> <name> <hex>, and `hash <room> <8 hex digits>`
        std::map<std::string, std::pair<std::string, std::string>> file;     // name -> kind, hex
        std::map<std::string, std::string> hashes;
        std::ifstream in(std::string(TEST_DATA_DIR) + "/lobby_messages.txt");
        ASSERT_TRUE(in.good());
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') continue;
            const size_t a = line.find(' ');
            const size_t b = line.find(' ', a + 1);
            ASSERT_TRUE(a != std::string::npos && b != std::string::npos && line.find(' ', b + 1) == std::string::npos);
            const std::string first = line.substr(0, a);
            const std::string second = line.substr(a + 1, b - a - 1);
            const std::string third = line.substr(b + 1);
            if (first == "hash") {
                ASSERT_TRUE(hashes.emplace(second, third).second);
            } else {
                ASSERT_TRUE(file.emplace(second, std::make_pair(first, third)).second);         // (a name once)
            }
        }
        std::vector<std::string> problems;
        for (const Sample& s : samples) {
            const auto it = file.find(s.name);
            if (it == file.end()) problems.push_back(s.name + ": not in the file");
            else if (it->second.first != s.kind) problems.push_back(s.name + ": the kind is " + it->second.first + ", not " + s.kind);
            else if (it->second.second != hex(s.bytes)) problems.push_back(s.name + ": the encoder writes " + hex(s.bytes) + ", the file has " + it->second.second);
        }
        for (const auto& line : file) {
            bool known = false;
            for (const Sample& s : samples) known = known || s.name == line.first;
            if (!known) problems.push_back(line.first + ": in the file, made by no sample");
        }
        for (const auto& r : rooms) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(seating_hash(r.second)));
            const auto it = hashes.find(r.first);
            if (it == hashes.end() || it->second != buf) problems.push_back(r.first + ": the seating hash is " + buf + ", the file has " + (it == hashes.end() ? std::string("none") : it->second));
        }
        ASSERT_EQ(hashes.size(), rooms.size());
        for (const std::string& problem : problems) std::cout << "\n    " << problem;
        ASSERT_TRUE(problems.empty());
        // every line decodes with the decoder of its message and encodes again to the same bytes (what a page makes is what the server reads, what the server makes is what a page reads)
        for (const Sample& s : samples) {
            bool ok = false;
            if (s.kind == "hello") { HelloMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes && (s.name == "game-joins" ? m.client_kind == kClientGame : m.client_kind == kClientPage); }
            else if (s.kind == "leave") ok = s.bytes == std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Leave)};
            else if (s.kind == "ping" || s.kind == "pong") { PingMsg m; ok = decode_ping(s.bytes.data(), s.bytes.size(), m) && (s.kind == "ping" ? encode_ping(m) : encode_pong(m)) == s.bytes; }
            else if (s.kind == "seatmove") { SeatMoveMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "plan") { PlanMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "name") { NameMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "remove") { RemoveMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "startrequest") { StartRequestMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "chat") { ChatMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "welcome") { WelcomeMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "reject") { RejectMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "room") { RoomMsg m; ok = decode(s.bytes, m) && encode(m) == s.bytes; }
            else if (s.kind == "other") ok = s.bytes == encode_begin() && peek_type(s.bytes) == MsgType::Begin;
            if (!ok) std::cout << "\n    " << s.name << ": the decoder or the encoder of its kind does not take these bytes";
            ASSERT_TRUE(ok);
        }
        // the page's Hello is the page's alone: a lobby block with a game's client kind is no Hello, and a page's Hello is no game's
        {
            HelloMsg m;
            ASSERT_TRUE(decode(samples[0].bytes, m) && m.client_kind == kClientPage && m.create.has_value() && m.create->lobby() && m.create->leader_starts() && m.create->seats == 4);
        }
    } TEST_END();
}
