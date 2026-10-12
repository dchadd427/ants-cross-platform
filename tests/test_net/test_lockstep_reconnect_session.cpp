// Tests of the lock-step network core: coming back through whole sessions (protocol 10, release A and its review)
#include "lockstep_fixtures.hpp"


// ---------------------------------------------------------------------------------------------------------------------------------
// Reconnect, release A: the sessions (protocol 10). A dedicated server's match that holds the seat of a player whose connection is lost. N2.47 - N2.7x.
// ---------------------------------------------------------------------------------------------------------------------------------

void run_reconnect_session_tests() {
    TEST_CASE("N2.47 Hold: A Lost Link Pauses The Match For Everybody Within One Pass (Nothing Is Sealed For 20 s, The Others Stall And Are Told Who Is Missing And Since When, The Held Seat Gates Nothing), The Player Comes Back With The Turns It Has, The Match Resumes And All Three Machines End Identical") {
        HoldMatch m(3);
        m.run(6000);
        ASSERT_FALSE(m.host->paused());
        for (auto& c : m.clients) ASSERT_TRUE(c->presence().missing.empty() && !c->paused());
        ASSERT_TRUE(m.sealed() >= 115);
        m.auto_reconnect[1] = false;                                            // seat 1's machine does not look for a new link yet
        m.cut(1);
        const uint32_t cut_at = m.now;
        uint32_t paused_after_ms = 0;
        uint32_t told_after_ms = 0;
        m.run(500, true, [&](uint32_t now) {
            if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - cut_at;
            if (told_after_ms == 0 && m.clients[0]->paused() && m.clients[2]->paused()) told_after_ms = now - cut_at;
        });
        ASSERT_TRUE(paused_after_ms > 0 && paused_after_ms <= 20);               // one pass of the host (10 ms steps)
        ASSERT_TRUE(told_after_ms > 0 && told_after_ms <= 120);                  // ... and the one-way trip of the Presence (30 - 40 ms)
        ASSERT_TRUE(m.host->seat_held(1) && !m.host->client_present(1));
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Absent);
        ASSERT_EQ(m.host->behind_ms(1), 0u);                                     // inactive in the sequencer: it gates nothing and its acknowledgements no longer count
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        const uint32_t sealed_at_pause = m.sealed();
        const uint64_t ticks_at_pause = m.sims[0]->current_tick();
        for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{1});
            ASSERT_TRUE(p.missing[0].seat == 1 && p.missing[0].state == PresenceMsg::State::Absent && p.missing[0].progress == 0);
            ASSERT_TRUE(p.vote_seat == 255 && p.voters == 2 && p.cap_s <= 1800 && p.cap_s >= 1795);
        }
        m.run(20000);
        ASSERT_EQ(m.sealed(), sealed_at_pause);                                  // nothing is sealed for 20 s ...
        ASSERT_TRUE(m.sims[0]->current_tick() <= ticks_at_pause + 3 && m.sims[2]->current_tick() <= ticks_at_pause + 3);       // ... so nobody plays: the clients wait at a turn boundary
        ASSERT_TRUE(m.clients[0]->runner().held() && m.clients[2]->runner().held());          // REWRITTEN: the others' runners used to be STALLED here (which told their jitter buffers about a pause); they know the match is held (Presence): they wait, with no stall
        ASSERT_FALSE(m.clients[0]->runner().stalled() || m.clients[2]->runner().stalled());
        const PresenceMsg& later = m.clients[0]->presence();
        ASSERT_TRUE(later.missing[0].waited_s >= 19 && later.missing[0].waited_s <= 22);        // since when: the seat has been away for 20 s and more
        ASSERT_TRUE(later.cap_s < 1795);                                                         // the cap counts down
        ASSERT_EQ(m.host->violations(0), 0u);                                    // the clicks that they went on giving (the script) cost nothing: refused here, discarded there
        ASSERT_EQ(m.host->violations(2), 0u);
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        ASSERT_FALSE(m.clients[1]->lost());                                      // the machine of seat 1 waits for its owner to give it a link
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
        // the machine comes back in memory: it says Hello with the key and the turns it has, is given the rest, and the match goes on
        m.auto_reconnect[1] = true;
        const uint32_t back_at = m.now;
        uint32_t resumed_after_ms = 0;
        m.run(3000, true, [&](uint32_t now) {
            if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - back_at;
        });
        ASSERT_TRUE(resumed_after_ms > 0 && resumed_after_ms <= 600);            // a round trip for the Hello and the Welcome, the stream, the hash: about 0.3 s over 30 - 40 ms links
        std::cout << "\n      [reconnect] a link cut: the host paused " << paused_after_ms << " ms later, the others knew " << told_after_ms << " ms later; the machine came back in memory and the match resumed "
                  << resumed_after_ms << " ms after its new link was made (links of 30 - 40 ms one way)" << std::flush;
        const uint32_t sealed_at_resume = m.sealed();
        m.run(2000);
        ASSERT_TRUE(m.sealed() - sealed_at_resume >= 38 && m.sealed() - sealed_at_resume <= 42);      // the schedule slid during the pause: no burst of 400 turns, 20 a second
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.host->client_present(1) && !m.host->seat_held(1));
        ASSERT_EQ(static_cast<int>(m.clients[1]->mode()), static_cast<int>(ClientSession::Mode::Normal));
        ASSERT_TRUE(m.sealed() > sealed_at_pause + 40);                          // sealing went on
        for (auto& c : m.clients) ASSERT_TRUE(c->presence().missing.empty() && !c->paused());      // everybody was told that the match runs
        m.run(10000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        for (auto& s : m.sims) ASSERT_EQ(s->current_tick(), m.referee.current_tick());
        ASSERT_TRUE(m.all_equal());                                              // the three machines and the referee: one state
        ASSERT_TRUE(m.clients[0]->runner().buffer_turns() == 1 && m.clients[2]->runner().buffer_turns() == 1);      // a pause is no lateness of the link: the others' jitter buffers did not grow
        ASSERT_TRUE(m.clients[1]->runner().buffer_turns() == 1);                 // and the returning machine's did not (the catch-up feeds it nothing)
    } TEST_END();

    TEST_CASE("N2.48 Hold: A Machine That Starts From Nothing (A Reloaded Page: A New Engine, The Lobby, Then The Match) Is Given More Than 300 Turns And Verified, Twice, The Second Time With A Slowed Catch-Up That Lasts Past 30 s Of Away Time: The Seat Is Catching Up, So No Vote Is Opened For It And It Is Not Dropped While Its Acknowledgements Move") {
        {
            HoldMatch m(3);
            m.run(40000);                                                        // 800 turns: more than the 600 that every machine keeps
            ASSERT_TRUE(m.sealed() >= 790);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(2000);
            ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
            const uint32_t total = m.sealed();
            const uint32_t reload_at = m.now;
            m.reload(2);
            bool saw_catching_up = false;
            ASSERT_TRUE(m.until([&]() {
                saw_catching_up = saw_catching_up || m.host->attendance().state(2) == Attendance::State::CatchingUp;
                return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal;       // (the machine goes on when the server's word reaches it)
            }, 20000));
            ASSERT_TRUE(saw_catching_up);
            std::cout << "\n      [reconnect] a machine that starts from nothing was given " << total << " turns (" << m.host->log().bytes() << " bytes of log) and was back " << m.now - reload_at
                      << " ms after its page opened (links of 30 - 40 ms one way)" << std::flush;
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
            ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() >= total);
            m.run(5000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
            ASSERT_TRUE(m.all_equal());
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
        }
        {
            HoldOptions o;
            o.client.catch_up_ticks = 1;                                         // one turn per update (10 ms): 100 turns a second
            HoldMatch m(3, o);
            m.run(170000);                                                       // 3400 turns: 34 s of catch-up at that speed
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(1000);
            m.reload(1);
            bool opened_a_vote = false;
            bool dropped = false;
            uint8_t last_percent = 0;
            bool percent_grew = false;
            uint32_t catching_for_ms = 0;
            m.run(60000, true, [&](uint32_t) {
                if (m.host->attendance().state(1) == Attendance::State::CatchingUp) catching_for_ms += 10;
                for (uint8_t seat : {uint8_t{0}, uint8_t{2}}) opened_a_vote = opened_a_vote || m.clients[seat]->presence().vote_seat != 255;
                dropped = dropped || m.host->attendance().state(1) == Attendance::State::Dropped;
                const uint8_t pc = m.host->attendance().percent(1);
                if (pc > last_percent) percent_grew = true;
                last_percent = std::max(last_percent, pc);
            });
            ASSERT_TRUE(catching_for_ms >= 30000);                               // it was catching up for longer than the vote's 30 s: away for 35 s in all
            ASSERT_FALSE(opened_a_vote);                                         // a seat that is back is never put to the vote
            ASSERT_FALSE(dropped);
            ASSERT_TRUE(percent_grew && last_percent >= 90);
            ASSERT_FALSE(m.host->paused());                                      // it got through
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.49 Hold: The Vote Opens After 30 s Away And Not Before; More Than Half Of The Connected Players Win It (Two Of Two Here: One Of Two Is Not Enough, A Changed Choice Counts); The Drop Is Sealed In One Turn And Executes At The Same Tick On Every Machine, The Match Goes On, The Key Of The Dropped Seat Is Told \"Dropped\"") {
        HoldMatch m(3);
        m.run(5000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        const uint32_t cut_at = m.now;
        m.run(28500);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(m.clients[seat]->paused() && m.clients[seat]->presence().vote_seat == 255);       // 28.5 s: no vote yet
        uint32_t vote_opened_after_ms = 0;
        m.run(3500, true, [&](uint32_t now) {
            if (vote_opened_after_ms == 0 && m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2) vote_opened_after_ms = now - cut_at;
        });
        ASSERT_TRUE(vote_opened_after_ms >= 30000 && vote_opened_after_ms <= 31600);        // 30 s of away time, and the second for the Presence to arrive
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.vote_seat == 2 && p.voters == 2 && p.votes_continue == 0 && p.your_vote == 0);
        }
        ASSERT_TRUE(m.clients[0]->vote(2, true));                                    // seat 0: continue without seat 2. One of two connected players is not more than half
        m.run(1500);
        ASSERT_FALSE(m.sims[0]->is_player_dropped(2));
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[0]->presence().votes_continue == 1 && m.clients[0]->presence().your_vote == 2);        // everybody is told the count, each its own choice
        ASSERT_TRUE(m.clients[1]->presence().votes_continue == 1 && m.clients[1]->presence().your_vote == 0);
        ASSERT_TRUE(m.clients[1]->vote(2, false));                                   // seat 1: keep waiting
        m.run(1500);
        ASSERT_TRUE(m.host->paused() && m.clients[1]->presence().your_vote == 1 && m.clients[1]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[0]->vote(1, true));                                    // (a vote about a seat that is not the subject: ignored by the host, no offence)
        m.run(300);
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->paused());
        // seat 1 changes its mind: two of two connected players want to continue
        const uint32_t sealed_before = m.sealed();
        ASSERT_TRUE(m.clients[1]->vote(2, true));
        // the tick at which each machine executes the Drop: the same
        uint64_t drop_tick[3] = {0, 0, 0};
        m.run(3000, true, [&](uint32_t) {
            if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
            if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
            if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
        });
        ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[1] != 0 && drop_tick[2] != 0);
        ASSERT_TRUE(drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);           // the same tick on every machine, the referee included
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 1u);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_FALSE(m.host->paused());                                              // the pause is over: the match goes on without seat 2
        ASSERT_TRUE(m.sealed() > sealed_before);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) ASSERT_TRUE(!m.clients[seat]->paused() && m.clients[seat]->presence().missing.empty());
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            ASSERT_TRUE(m.sims[seat]->is_player_dropped(2));
            ASSERT_EQ(m.sims[seat]->current_tick(), m.referee.current_tick());
        }
        ASSERT_TRUE(m.all_equal());                                                  // the Drop was applied at the same tick: the states are one
        // the machine of the dropped seat comes by with its key: the seat is gone ("Sorry, you have been dropped": its session ends with that reason)
        m.auto_reconnect[2] = true;
        m.run(5000);
        ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
        ASSERT_FALSE(m.clients[2]->wants_connection(m.now));                         // and it makes no more attempts
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
    } TEST_END();

    TEST_CASE("N2.50 Hold: The Match's Total Pause Is Capped (Here 20 s) And The Cap Is Absolute: At The Cap Every Seat That Is Not Present Is Dropped At The Same Tick Everywhere, A Seat That Is Catching Up And Progressing Too, And The Match Goes On, cap_s Counts Down For The Players; One That Stops Is Let Go After 20 s And Then Dropped At Once; A Seat That Was Dropped Is Told So By Its Key") {
        {
            HoldOptions o;
            o.host.attendance.max_pause_ms = 20000;
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            m.run(5000);
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.clients[0]->presence().cap_s >= 14 && m.clients[0]->presence().cap_s <= 16);      // about 15 s are left (Presence is a second old at the most): it says so
            ASSERT_TRUE(m.clients[1]->presence().cap_s >= 14 && m.clients[1]->presence().cap_s <= 16);
            m.run(14500);                                                                                  // 19.5 s into the pause
            ASSERT_TRUE(m.host->paused() && m.host->attendance().state(2) == Attendance::State::Absent);
            ASSERT_TRUE(m.clients[0]->presence().cap_s <= 1);
            uint32_t resumed_after_ms = 0;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t now) {
                if (resumed_after_ms == 0 && !m.host->paused()) resumed_after_ms = now - cut_at;
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(resumed_after_ms >= 20000 && resumed_after_ms <= 20100);                           // at the cap, to the pass
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 1u);
            ASSERT_EQ(m.host->attendance().drops_by_vote(), 0u);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            m.run(3000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            m.auto_reconnect[2] = true;                                                                    // its machine comes by: the seat is gone
            m.run(4000);
            ASSERT_TRUE(m.clients[2]->lost() && m.clients[2]->rejected() && m.clients[2]->reject_reason() == RejectReason::Dropped);
        }
        {   // the cap is ABSOLUTE: a seat that is catching up and progresses is dropped at the cap with the other seat that is not present (REWRITTEN: it used to keep its chance while it progressed)
            HoldOptions o;
            o.host.attendance.max_pause_ms = 20000;
            o.client.catch_up_ticks = 1;                                                                   // 100 turns a second
            HoldMatch m(4, o);
            m.run(150000);                                                                                 // 3000 turns: a catch-up of 30 s
            m.auto_reconnect[1] = false;
            m.auto_reconnect[2] = false;
            m.cut(1);
            m.cut(2);
            m.run(500);
            m.reload(1);                                                                                   // seat 1 comes back (slowly), seat 2 does not
            m.run(15000);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::CatchingUp);                       // on its way, and getting on: 15 s into a catch-up of 30 s
            ASSERT_TRUE(m.host->attendance().percent(1) > 20 && m.host->attendance().percent(1) < 80);
            uint64_t drop_tick[3] = {0, 0, 0};
            const auto watch = [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[3]->is_player_dropped(1)) drop_tick[1] = m.sims[3]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
            };
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 8000));                              // the cap: 20 s of pause in all
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);                          // the absent seat went at the cap ...
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);                          // ... and so did the one that was on its way, whatever progress it showed
            ASSERT_EQ(m.host->attendance().drops_by_cap(), 2u);
            ASSERT_EQ(m.host->attendance().rejoins(), 0u);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            m.run(3000, true, watch);                                                                      // (the Drops are sealed in the first turn after the pause)
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[0]->is_player_dropped(2) && m.sims[3]->is_player_dropped(1) && m.sims[3]->is_player_dropped(2));
            ASSERT_TRUE(m.referee.is_player_dropped(1) && m.referee.is_player_dropped(2));
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);   // the same tick everywhere
            ASSERT_TRUE(m.all_equal());
            ASSERT_TRUE(m.clients[1] == nullptr || m.clients[1]->lost() || m.clients[1]->mode() != ClientSession::Mode::Normal);      // (the machine that was coming back is told that it is out)
        }
        {   // a catch-up that stops: let go after 20 s without progress, and then the cap takes the seat (the match has no pause left)
            HoldOptions o;
            o.host.attendance.max_pause_ms = 30000;
            o.client.catch_up_ticks = 1;
            HoldMatch m(3, o);
            m.run(150000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            m.run(2000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
            m.frozen_mask = 1u << 2;                                                                       // the machine hangs in the middle of its catch-up
            m.run(19000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);                       // 19 s without progress: still its chance
            m.run(2500);
            ASSERT_NE(static_cast<int>(m.host->attendance().state(2)), static_cast<int>(Attendance::State::CatchingUp));   // 20 s: let go
            m.run(10000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);                          // the cap (30 s of pause) has passed meanwhile: dropped
            ASSERT_FALSE(m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.51 Hold: Two Seats Gone: The Vote Is About The One That Has Been Away Longest, The Other Keeps The Match Waiting (Its Own Vote Starts Empty When It Is The Subject); The Seats Are Dropped One After The Other And The Match Goes On With The Two That Are Left") {
        HoldMatch m(4);
        m.run(4000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        const uint32_t cut1 = m.now;
        m.run(8000);
        m.cut(2);                                                                       // seat 2 follows 8 s later
        m.run(cut1 + 31500 - m.now);                                                    // 31.5 s after seat 1 was lost: it is away 30 s and more, seat 2 only 23.5 s
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{2});
            ASSERT_TRUE(p.missing[0].seat == 1 && p.missing[1].seat == 2);               // longest away first
            ASSERT_TRUE(p.missing[0].waited_s >= 30 && p.missing[1].waited_s >= 22 && p.missing[1].waited_s <= 24);
            ASSERT_TRUE(p.vote_seat == 1 && p.voters == 2);
        }
        m.run(8000);                                                                    // seat 2 has been away 30 s too: the subject is still seat 1
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 1 && m.clients[0]->presence().missing[1].waited_s >= 30);
        ASSERT_TRUE(m.clients[0]->vote(1, true));
        ASSERT_TRUE(m.clients[3]->vote(1, true));                                       // two of two connected players: seat 1 is dropped
        m.run(1500);
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
        ASSERT_TRUE(m.host->paused());                                                  // seat 2 is still away: the match waits on
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_EQ(p.missing.size(), size_t{1});
            ASSERT_TRUE(p.missing[0].seat == 2 && p.vote_seat == 2);                     // the next subject, at once (it has been away more than 30 s) ...
            ASSERT_TRUE(p.votes_continue == 0 && p.your_vote == 0);                      // ... and its vote starts empty
        }
        ASSERT_FALSE(m.sims[0]->is_player_dropped(1));                                  // (the Drop of seat 1 waits for the end of the pause: nothing is sealed)
        ASSERT_TRUE(m.clients[0]->vote(2, true));
        ASSERT_TRUE(m.clients[3]->vote(2, true));
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 3000));
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().drops_by_vote(), 2u);
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (uint8_t seat : {uint8_t{0}, uint8_t{3}}) {                                 // both Drops travelled in the first turn after the pause: the same tick on both machines
            ASSERT_TRUE(m.sims[seat]->is_player_dropped(1) && m.sims[seat]->is_player_dropped(2));
            ASSERT_EQ(m.sims[seat]->current_tick(), m.referee.current_tick());
        }
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.52 Hold: A Wrong Key, No Key And A Key With Too Many Turns Are Refused (The First Two With \"The Match Has Started\": Nothing Is Revealed); A Second Window With The Key Of A Seat Whose Link Is Alive Takes It Over And The First Is Told It Was Superseded, A Newer Attempt Of A Seat Closes The Earlier One; Nobody Else Is Disturbed") {
        HoldMatch m(3);
        m.run(5000);
        const auto raw = [&](const SeatKey& key, uint32_t have) {
            auto ends = m.open_link();
            HelloMsg h;
            h.name = "Mallory";
            h.key = key;
            h.have_turns = have;
            ends.second->send(encode(h));
            return ends.second;
        };
        const auto answer = [&](Connection* c) {                                         // the Reject that the machine got, MsgType::None when nothing yet
            std::vector<uint8_t> msg;
            RejectMsg r;
            while (c->poll(msg)) {
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) return static_cast<int>(r.reason);
            }
            return 0;
        };
        Connection* wrong = raw(key_with(99), 0);
        Connection* none = raw(SeatKey{}, 0);
        Connection* too_many = raw(m.keys[1], m.sealed() + 500);
        m.run(300);
        ASSERT_EQ(answer(wrong), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_EQ(answer(none), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_EQ(answer(too_many), static_cast<int>(RejectReason::BadRequest));
        ASSERT_EQ(m.refusals, 3u);
        ASSERT_FALSE(m.host->paused());                                                  // nobody was disturbed: no seat is held, nobody is coming back
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(m.host->client_present(p) && m.host->attendance().state(p) == Attendance::State::Present);
        // a key that fits two seats' worth of nothing: the key of seat 1 with its first byte changed
        SeatKey almost = m.keys[1];
        almost[0] = static_cast<uint8_t>(almost[0] ^ 1);
        Connection* close_call = raw(almost, 0);
        m.run(300);
        ASSERT_EQ(answer(close_call), static_cast<int>(RejectReason::MatchRunning));
        ASSERT_FALSE(m.host->paused());
        // a second window with the key of seat 1, whose own link is alive (a duplicated tab, a Wi-Fi switch that the host has not noticed)
        Connection* second = raw(m.keys[1], m.sealed());
        m.run(400);
        ASSERT_TRUE(m.host->paused());                                                   // the seat is away from now: the match waits for the new window
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::CatchingUp);
        ASSERT_EQ(m.host->rejoiners(), size_t{1});
        ASSERT_FALSE(m.host->client_present(1));
        ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->rejected() && m.clients[1]->reject_reason() == RejectReason::Superseded);      // the first window was told, and stops trying
        ASSERT_FALSE(m.clients[1]->wants_connection(m.now));
        std::vector<uint8_t> msg;
        bool welcome = false, catch_up = false;
        while (second->poll(msg)) {
            WelcomeMsg w;
            CatchUpMsg c;
            if (peek_type(msg) == MsgType::Welcome && decode(msg, w)) welcome = (w.flags & kWelcomeRejoin) != 0 && w.player == 1 && w.key == m.keys[1];
            if (peek_type(msg) == MsgType::CatchUp && decode(msg, c)) catch_up = c.first_turn == c.total_turns;
        }
        ASSERT_TRUE(welcome && catch_up);                                                // the new window got its seat, its key and the (empty) stream
        // a newer attempt of the same seat closes the earlier one, and tells it
        Connection* third = raw(m.keys[1], m.sealed());
        m.run(300);
        ASSERT_EQ(answer(second), static_cast<int>(RejectReason::Superseded));
        ASSERT_FALSE(second->is_open());
        ASSERT_EQ(m.host->rejoiners(), size_t{1});
        ASSERT_TRUE(third->is_open());
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
        {   // a session that has no Start message to give cannot take in a machine that has nothing (it could not tell it how to load the match): RejoinFailed, and the seat stays away; a
            // machine that has its turns needs no Start, and comes back as ever
            HoldOptions no_start;
            no_start.rejoin_start = false;
            HoldMatch n(3, no_start);
            n.run(4000);
            n.auto_reconnect[2] = false;
            n.cut(2);
            n.run(500);
            ASSERT_TRUE(n.host->paused());
            n.reload(2);
            n.run(1000);
            ASSERT_EQ(static_cast<int>(n.rejected_lobby[2]), static_cast<int>(RejectReason::RejoinFailed));
            ASSERT_EQ(n.host->attendance().state(2), Attendance::State::Absent);
            ASSERT_EQ(n.host->rejoiners(), size_t{0});
            ASSERT_TRUE(n.host->paused());
            HoldMatch k(3, no_start);
            k.run(4000);
            k.cut(1);
            ASSERT_TRUE(k.until([&]() { return !k.host->paused() && k.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            ASSERT_EQ(k.host->attendance().state(1), Attendance::State::Present);
        }
    } TEST_END();

    TEST_CASE("N2.53 Hold: A Machine Whose State Differs After The Catch-Up Does Not Get Its Seat Back: It Alone Is Told (Desync), The Room Does Not Fail, The Others Are Not Disturbed And The Seat Stays Away; A Good Machine With The Same Key Gets It Afterwards") {
        HoldMatch m(3);
        m.run(8000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        m.tamper_reloaded = [&m](sim::SimulationEngine& s) { s.get_unit(m.ids.ants[2][1]).hp = 3; };      // a hit point that no turn explains
        m.reload(2);
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());                                          // the referee's record of the match has no desync: the room does not fail
        ASSERT_FALSE(m.host->frozen());
        ASSERT_TRUE(m.host->paused());
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);             // the seat stays away
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.clients[2] != nullptr && m.clients[2]->desynced() && m.clients[2]->lost() && m.clients[2]->reject_reason() == RejectReason::RejoinFailed);
        ASSERT_EQ(m.clients[2]->desync().player, 2);
        ASSERT_TRUE(m.clients[2]->desync().host != m.clients[2]->desync().peer);
        ASSERT_FALSE(m.clients[0]->desynced() || m.clients[1]->desynced());              // nobody else was told
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));
        m.tamper_reloaded = nullptr;
        m.reload(2);                                                                     // a good machine, the same key
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 10000));
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.54 Hold: A Log That Is Full Falls Back To Dropping At Once (No Pause, The Key Is Told \"Dropped\"); Until It Is Full A Lost Seat Is Held") {
        HoldOptions o;
        o.host.max_log_bytes = 3000;
        HoldMatch m(3, o);
        m.run(2000);
        ASSERT_TRUE(m.host->log().usable());
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused() && m.host->seat_held(2));                           // the log has room: held
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 6000));
        m.run(40000);
        ASSERT_FALSE(m.host->log().usable());                                            // the 3000 bytes were used up (the index alone is 80 bytes a second)
        ASSERT_TRUE(m.host->log().bytes() == 0 && m.host->log().turns() == 0 && m.host->log().capacity_bytes() == 0);     // (REWRITTEN: a log that is full used to keep its 3000 bytes for the rest of the match; it is DEAD, it freed them the moment it died)
        m.auto_reconnect[1] = false;
        m.cut(1);
        uint64_t drop_tick[3] = {0, 0, 0};
        bool paused_ever = false;
        m.run(3000, true, [&](uint32_t) {
            paused_ever = paused_ever || m.host->paused();
            if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
            if (drop_tick[1] == 0 && m.sims[2]->is_player_dropped(1)) drop_tick[1] = m.sims[2]->current_tick();
            if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
        });
        ASSERT_FALSE(paused_ever);                                                       // dropped at once, as before there was a way back
        ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
        ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
        m.auto_reconnect[1] = true;
        m.run(4000);
        ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->reject_reason() == RejectReason::Dropped);       // its key (which survived the drop) is told so
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
        {   // a log that is not usable cannot give anybody the match: a second window with the key of a seat whose link is alive is refused (the first window is NOT replaced)
            auto ends = m.open_link();
            HelloMsg h;
            h.name = "Second";
            h.key = m.keys[2];
            h.have_turns = m.sealed();
            ends.second->send(encode(h));
            m.run(300);
            std::vector<uint8_t> msg;
            RejectMsg r;
            int reason = 0;
            while (ends.second->poll(msg)) {
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) reason = static_cast<int>(r.reason);
            }
            ASSERT_EQ(reason, static_cast<int>(RejectReason::RejoinFailed));
            ASSERT_TRUE(m.host->client_present(2) && m.host->rejoiners() == 0 && !m.host->paused());
            ASSERT_EQ(static_cast<int>(m.clients[2]->mode()), static_cast<int>(ClientSession::Mode::Normal));     // the window that plays was not told that it was replaced
        }
        {   // the log is the match, so a turn is appended BEFORE it is sent: a link that fails in the very send of the turn that does not fit the log is dropped at once (a seat that was
            // held with a log that had just become useless could never be given the match: it would wait for the cap)
            const uint32_t turn = 120;
            size_t before_bytes = 0;
            {
                HoldMatch probe(3);
                probe.run(10000, true, [&](uint32_t) {
                    if (before_bytes == 0 && probe.host->log().turns() == turn) before_bytes = probe.host->log().bytes();
                });
            }
            ASSERT_TRUE(before_bytes > 0);
            HoldOptions tight;
            tight.host.max_log_bytes = before_bytes + 1;                                 // turn 120 (at least 6 bytes) does not fit
            HoldMatch t(3, tight);
            t.taps[2]->fail_send_of_turn = turn;
            bool paused_at_all = false;
            t.run(8000, true, [&](uint32_t) { paused_at_all = paused_at_all || t.host->paused(); });
            ASSERT_FALSE(t.host->log().usable());
            ASSERT_TRUE(t.host->log().turns() == 0 && t.host->log().bytes() == 0);          // (dead: nothing is kept)
            ASSERT_FALSE(paused_at_all);
            ASSERT_EQ(t.host->attendance().state(2), Attendance::State::Dropped);
        }
        {   // a seat that has no key cannot be held: its lost link is a drop at once
            HoldOptions keyless;
            keyless.keys = false;
            HoldMatch k(3, keyless);
            k.run(3000);
            k.cut(1);
            bool keyless_paused = false;
            k.run(2000, true, [&](uint32_t) { keyless_paused = keyless_paused || k.host->paused(); });
            ASSERT_FALSE(keyless_paused);
            ASSERT_EQ(k.host->attendance().state(1), Attendance::State::Dropped);
            ASSERT_TRUE(k.clients[1]->lost() && !k.clients[1]->reconnecting());
        }
    } TEST_END();

    TEST_CASE("N2.55 Hold: Commands During A Pause Are Discarded Without A Violation (400 Of Them Do Not Cost A Player Its Seat), A Command That Cannot Be Decoded Still Does; ClientSession::submit Refuses While A Seat Is Missing And Works Again When The Match Runs") {
        HoldMatch m(3);
        m.run(4000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        Command c = cmd(CommandType::GroupMove, 0, 255, 10, 10, {m.ids.ants[0][0], m.ids.ants[0][1]});
        ASSERT_FALSE(m.clients[0]->submit(c));                                           // refused at the client: a seat is missing
        for (int i = 0; i < 400; ++i) {                                                  // ... and a client that sends them all the same (a page that did not know yet) is not punished
            CommandMsg msg;
            msg.command = c;
            m.client_ends[0]->send(encode(msg));
            if (i % 40 == 39) m.run(10);
        }
        m.run(500);
        ASSERT_EQ(m.host->violations(0), 0u);
        ASSERT_TRUE(m.host->client_present(0));
        const uint32_t sealed_in_pause = m.sealed();
        m.client_ends[0]->send(std::vector<uint8_t>{static_cast<uint8_t>(MsgType::Command), 1, 2, 3});     // not a command
        m.run(100);
        ASSERT_EQ(m.host->violations(0), 1u);                                            // a malformed message is an offence in a pause as at any time
        // the match runs again: a command is carried out, and is counted by the sequencer's limit like any
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal && !m.clients[0]->paused(); }, 8000));
        ASSERT_TRUE(m.clients[0]->submit(c));
        m.run(3000);
        ASSERT_TRUE(m.sealed() > sealed_in_pause);
        ASSERT_EQ(m.host->violations(0), 1u);
        m.settle();
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.56 Hold: Leave Is Final: A Player That Quits While Another Seat Is Away Is Dropped At Once (Its Drop Waits For The End Of The Pause, The Same Tick Everywhere); A Player That Quits While It Is Coming Back Is Dropped And Its Key Is Told So") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(1000);
            ASSERT_TRUE(m.host->paused());
            m.clients[2]->leave();                                                       // seat 2 quits during the pause
            m.run(500);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);        // final at once: not held, though its key is valid
            ASSERT_FALSE(m.host->client_present(2) || m.host->seat_held(2));
            ASSERT_TRUE(m.host->paused());                                               // seat 1 is still away
            ASSERT_FALSE(m.sims[0]->is_player_dropped(2));                               // nothing is sealed: the Drop waits
            m.auto_reconnect[1] = true;
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(2)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[1]->is_player_dropped(2)) drop_tick[1] = m.sims[1]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(2)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[1]->state_hash() == m.referee.state_hash());
        }
        {
            HoldOptions o;
            o.client.catch_up_ticks = 1;
            HoldMatch m(3, o);
            m.run(80000);                                                                // 1600 turns: a catch-up of 16 s
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(500);
            m.reload(2);
            m.run(3000);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
            m.clients[2]->leave();                                                       // it quits on its way back
            m.run(500);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            ASSERT_FALSE(m.host->paused());                                              // nobody else is away: the match goes on, the Drop is sealed
            m.run(3000);
            ASSERT_TRUE(m.sims[0]->is_player_dropped(2) && m.sims[1]->is_player_dropped(2));
            const SeatKey key = m.keys[2];
            m.reload(2, key);                                                            // a new page with the same key is told that the seat is gone
            m.run(2000);
            ASSERT_EQ(m.rejected_lobby[2], RejectReason::Dropped);
            m.settle();
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[1]->state_hash() == m.referee.state_hash());
        }
    } TEST_END();

    TEST_CASE("N2.57 Hold: Absences Add Up: A Seat That Was Away For 20.2 s, Came Back And Is Lost Again Is Put To The Vote 9.4 s Into The Second Absence (30 s In All), Not 30 s; Every Loss Counts At Least 5 s, And A Connection That Is Lost Three Times In A Minute Is Put To The Vote At Its Third Loss") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(20200);
            ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);
            m.auto_reconnect[2] = true;                                                  // it comes back after 20.2 s
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 5000));
            const uint32_t first_absence = m.host->attendance().away_ms(2, m.now);
            ASSERT_TRUE(first_absence >= 20200 && first_absence <= 21500);              // the 20.2 s and the way back
            m.run(3000);
            m.auto_reconnect[2] = false;
            m.cut(2);                                                                    // lost again
            const uint32_t second_cut = m.now;
            const uint32_t missing = 30000 - m.host->attendance().away_ms(2, m.now);     // what the seat still needs to be away to be put to the vote
            m.run(missing - 1500);
            ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);                          // not yet (the second absence alone is 8 s)
            m.run(3500);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);
            ASSERT_TRUE(m.now - second_cut < 12000);                                     // about 9 s into the second absence: 30 s in all
        }
        {   // REWRITTEN (it was: six short absences of a second, each counting 5 s, put to the vote at the seventh loss): a connection that is lost three times within a minute FLAPS, and the vote
            // about it opens at its third loss, though it is back each time (the 5 s of every loss are still counted: N2.42, N2.43)
            HoldMatch m(3);
            m.run(4000);
            for (int i = 1; i <= 3; ++i) {
                m.cut(2);
                m.run(100);
                ASSERT_TRUE(m.host->paused());
                ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 5000));
                ASSERT_TRUE(m.host->attendance().away_ms(2, m.now) >= static_cast<uint32_t>(5000 * i));      // however short, a loss counts 5 s
                if (i < 3) ASSERT_EQ(m.clients[0]->presence().vote_seat, 255);                              // (two losses are no flapping)
                m.run(300);
            }
            ASSERT_TRUE(m.host->attendance().flapping(2, m.now));
            m.run(1500);
            ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[1]->presence().vote_seat == 2);   // the vote about a seat that is back
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.58 Hold: Two Machines That Start From Nothing At Once Are Both Given The Match; It Resumes When The Last Of Them Is Back (And Not Before), Every Machine Ends Identical") {
        HoldMatch m(4);
        m.run(30000);
        m.auto_reconnect[1] = false;
        m.auto_reconnect[2] = false;
        m.cut(1);
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused() && m.host->attendance().state(1) == Attendance::State::Absent && m.host->attendance().state(2) == Attendance::State::Absent);
        m.reload(1);
        m.reload(2);
        bool both_catching_up = false;
        bool inconsistent = false;
        const auto back = [&](uint8_t seat) { return m.host->attendance().state(seat) == Attendance::State::Present; };
        ASSERT_TRUE(m.until([&]() {
            both_catching_up = both_catching_up || (m.host->rejoiners() == 2 && m.host->attendance().state(1) == Attendance::State::CatchingUp && m.host->attendance().state(2) == Attendance::State::CatchingUp);
            if (!m.host->paused() && !(back(1) && back(2))) inconsistent = true;           // the match runs while a seat is still on its way
            return !m.host->paused() && m.clients[1] && m.clients[2] && m.clients[1]->mode() == ClientSession::Mode::Normal && m.clients[2]->mode() == ClientSession::Mode::Normal;
        }, 20000));
        ASSERT_TRUE(both_catching_up);
        ASSERT_FALSE(inconsistent);
        ASSERT_EQ(m.host->attendance().rejoins(), 2u);
        m.run(5000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.59 Hold: A Catch-Up That Stops Is Let Go After 20 s Without Progress (The Seat Is Absent Again, Its Time Kept Running, The Others May Vote), And The Machine Gets Through On Its Second Try, From The Turns It Already Has") {
        HoldOptions o;
        o.client.catch_up_ticks = 1;
        HoldMatch m(3, o);
        m.run(150000);                                                                   // 3000 turns: a catch-up of 30 s at one turn per 10 ms
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        m.reload(2);
        m.run(8000);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::CatchingUp);
        const uint32_t executed_before = m.clients[2]->runner().next_turn_to_execute();
        ASSERT_TRUE(executed_before > 500 && executed_before < 2000);
        m.frozen_mask = 1u << 2;                                                         // it hangs
        const uint32_t frozen_at = m.now;
        uint32_t let_go_after_ms = 0;
        m.run(25000, true, [&](uint32_t now) {
            if (let_go_after_ms == 0 && m.host->attendance().state(2) == Attendance::State::Absent) let_go_after_ms = now - frozen_at;
        });
        ASSERT_TRUE(let_go_after_ms >= 19900 && let_go_after_ms <= 21500);              // 20 s without progress
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.host->attendance().away_ms(2, m.now) >= 30000);                    // its time kept running: the others may vote now ...
        ASSERT_TRUE(m.clients[0]->presence().vote_seat == 2 && m.clients[0]->presence().missing[0].state == PresenceMsg::State::Absent);
        m.frozen_mask = 0;                                                               // ... but it is back: its session sees the closed link, asks again, and is given what it lacks
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 40000));
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        ASSERT_TRUE(m.clients[2]->runner().next_turn_to_execute() >= executed_before);
        m.run(3000);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.60 Hold: What Presence Says To Each Player: Who Is Missing And For How Long (Longest Away First), The Vote's Count For Everybody And Each Player's Own Choice For That Player Only, The Voters, The Seconds Left Until The Cap (Counting Down), A Seat That Is Catching Up With Its Progress And Never As The Subject") {
        HoldMatch m(4);
        m.run(4000);
        m.auto_reconnect[3] = false;
        m.cut(3);
        m.run(31500);
        for (uint8_t seat = 0; seat < 3; ++seat) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 1 && p.missing[0].seat == 3 && p.vote_seat == 3 && p.voters == 3 && p.votes_continue == 0 && p.your_vote == 0);
            ASSERT_TRUE(p.missing[0].waited_s >= 30 && p.missing[0].waited_s <= 32);
        }
        const uint16_t cap_before = m.clients[0]->presence().cap_s;
        ASSERT_TRUE(m.clients[0]->vote(3, true));                                        // seat 0: continue
        m.run(200);                                                                      // (the periodic Presence is half a second away): a vote is answered at once, not at the next second
        ASSERT_TRUE(m.clients[0]->presence().your_vote == 2 && m.clients[0]->presence().votes_continue == 1 && m.clients[2]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[1]->vote(3, false));                                       // seat 1: keep waiting; seat 2 says nothing
        m.run(3300);
        ASSERT_TRUE(m.clients[0]->presence().your_vote == 2 && m.clients[0]->presence().votes_continue == 1);       // each is told its own choice, everybody the count
        ASSERT_TRUE(m.clients[1]->presence().your_vote == 1 && m.clients[1]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[2]->presence().your_vote == 0 && m.clients[2]->presence().votes_continue == 1);
        ASSERT_TRUE(m.clients[0]->presence().voters == 3 && m.clients[2]->presence().voters == 3);
        ASSERT_TRUE(m.clients[0]->presence().cap_s + 2 <= cap_before);                   // the cap counts down: 3.5 s later, at least 2 s less
        ASSERT_TRUE(m.clients[0]->presence().missing[0].waited_s > 31);                  // and the time away goes on
        ASSERT_TRUE(m.host->paused());                                                   // one of three is not more than half
        // a second seat is lost: the list is longest away first, and the vote is still about seat 3
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1500);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 2 && p.missing[0].seat == 3 && p.missing[1].seat == 2 && p.missing[0].waited_s > p.missing[1].waited_s);
            ASSERT_TRUE(p.vote_seat == 3 && p.voters == 2);                              // seat 2 is lost: it is no voter and its choice (it made none) is gone
        }
        ASSERT_EQ(m.clients[0]->presence().votes_continue, 1);                           // seat 0's choice stays: one of two connected players
        // a seat that comes back is catching up: shown with its progress, never the subject
        m.reload(2);
        bool catching = false;
        m.run(3000, true, [&](uint32_t) {
            for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
                for (const PresenceMsg::Entry& e : m.clients[seat]->presence().missing) {
                    if (e.seat == 2 && e.state == PresenceMsg::State::CatchingUp) {
                        catching = true;
                        ASSERT_TRUE(m.clients[seat]->presence().vote_seat != 2);
                    }
                }
            }
        });
        ASSERT_TRUE(catching);
    } TEST_END();

    TEST_CASE("N2.61 Hold: Chat Works During A Pause Between The Players Who Are There (Relayed By The Host, The Sender Stamped); The Absent Player's Machine Cannot Send, And What Was Said While It Was Away Is Not Replayed To It") {
        HoldMatch m(3);
        m.run(4000);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[0]->chat("anybody home?", false));
        m.run(300);
        ASSERT_TRUE(m.clients[1]->chat("only us", false));
        m.run(300);
        ASSERT_FALSE(m.clients[2]->chat("(from the dark)", false));                      // no link: nothing can be said
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            ASSERT_EQ(m.chats[seat].size(), size_t{2});
            ASSERT_TRUE(m.chats[seat][0].sender == 0 && m.chats[seat][0].text == "anybody home?" && !m.chats[seat][0].team);
            ASSERT_TRUE(m.chats[seat][1].sender == 1 && m.chats[seat][1].text == "only us" && !m.chats[seat][1].team);
        }
        ASSERT_TRUE(m.chats[2].empty());
        m.auto_reconnect[2] = true;
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));
        m.run(500);
        ASSERT_TRUE(m.chats[2].empty());                                                 // the chat of the pause is not replayed
        ASSERT_TRUE(m.clients[2]->chat("back", false));                                  // it can talk again
        m.run(500);
        ASSERT_TRUE(m.chats[0].size() == 3 && m.chats[0][2].sender == 2 && m.chats[1].size() == 3 && m.chats[2].size() == 1);
        ASSERT_EQ(m.host->violations(0), 0u);
    } TEST_END();

    TEST_CASE("N2.62 Hold: A Bot's Seat Is Never Absent, Never A Voter And Not Counted As Connected (The Vote Is Won By More Than Half Of The People); Its Orders Wait While The Match Is Paused; Nothing Is Held For It") {
        HoldOptions o;
        o.bot_mask = 1u << 3;
        HoldMatch m(4, o);
        m.run(4000);
        ASSERT_EQ(m.host->attendance().state(3), Attendance::State::Empty);              // no state at all: it is not at the table
        ASSERT_EQ(m.host->attendance().connected_humans(), 3);
        Command c = cmd(CommandType::GroupMove, 3, 255, 12, 12, {m.ids.ants[3][0]});
        ASSERT_TRUE(m.host->submit_bot(3, c));                                           // while the match runs a bot gives its orders
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(1000);
        ASSERT_TRUE(m.host->paused());
        ASSERT_FALSE(m.host->submit_bot(3, c));                                          // ... and waits like everybody while it is paused
        ASSERT_EQ(m.host->attendance().connected_humans(), 2);
        m.run(30000);
        for (uint8_t seat : {uint8_t{0}, uint8_t{1}}) {
            const PresenceMsg& p = m.clients[seat]->presence();
            ASSERT_TRUE(p.missing.size() == 1 && p.missing[0].seat == 2);                // the bot is not in the list
            ASSERT_TRUE(p.vote_seat == 2 && p.voters == 2);                              // two people vote, the bot does not count: one of them is not more than half
        }
        ASSERT_TRUE(m.clients[0]->vote(2, true));
        m.run(1500);
        ASSERT_TRUE(m.host->paused());
        ASSERT_TRUE(m.clients[1]->vote(2, true));                                        // two of two people
        ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 3000));
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_EQ(m.host->attendance().state(3), Attendance::State::Empty);              // still nothing for the bot
        ASSERT_TRUE(m.host->submit_bot(3, c));                                           // the match runs again: the bot plays on
        m.run(5000);
        ASSERT_FALSE(m.referee.is_player_dropped(3));
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.63 Hold: A Hostile Log Of The Biggest Turns (Two Players Put 40 Commands Of 32 Ants Into Every Turn: 5 MB In 25 s) Is Streamed To A Machine That Starts From Nothing Without Failing Its Link (A Link That Fails At 1 MB Of Backlog, As A WebSocket Does): The Stream Is Paced By What The Machine Has Executed, In Bytes") {
        HoldOptions o;
        o.host.max_log_bytes = 8u * 1024u * 1024u;
        HoldMatch m(3, o);
        m.audit_new_links = true;
        std::vector<uint32_t> many;                                                      // 32 ants: the most that a command names
        for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
        m.run(25000, false, [&](uint32_t) {
            for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                for (int i = 0; i < 8; ++i) {                                            // 8 a step of 10 ms: 40 a turn, 800 a second: under the budget of a connection, under the 64 of a turn
                    CommandMsg msg;
                    msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                    m.client_ends[p]->send(encode(msg));
                }
            }
        });
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));            // (the flood budget was not met)
        ASSERT_TRUE(m.host->log().usable());
        const size_t log_bytes = m.host->log().bytes();
        ASSERT_TRUE(log_bytes > 3u * 1024u * 1024u && log_bytes < 8u * 1024u * 1024u);
        m.auto_reconnect[2] = false;
        m.cut(2);
        m.run(500);
        ASSERT_TRUE(m.host->paused());
        m.reload(2);
        ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2] != nullptr && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 60000));
        ASSERT_EQ(m.audits.size(), size_t{1});
        ASSERT_FALSE(m.audits[0]->failed);                                               // the link never failed
        std::cout << "\n      [reconnect] a hostile log of " << log_bytes << " bytes (" << m.host->log().turns() << " turns) was streamed over a link that fails at 1 MB: " << m.audits[0]->sent
                  << " bytes sent, at most " << m.audits[0]->peak << " in flight at any moment" << std::flush;
        ASSERT_TRUE(m.audits[0]->sent > 3u * 1024u * 1024u);                             // the whole log went over it ...
        ASSERT_TRUE(m.audits[0]->peak < 400u * 1024u);                                   // ... never more than the window (256 KB) and a batch (48 KB) in flight, a third of what fails a WebSocket
        ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
    } TEST_END();

    TEST_CASE("N2.64 Client: A Reject Ends The Session For Good In Every Mode (Normal, Normal With The Close Behind It, Rejoining, CatchingUp), Whatever The Reason (1 - 9), With The Reason And No More Attempts; A Link That Only Closes Is Not The End; A Session That Does Not Reconnect Ignores A Reject As Before") {
        for (int r = 1; r <= 9; ++r) {
            const RejectReason reason = static_cast<RejectReason>(r);
            const auto ended = [&](LoneSession& L) {
                return L.s->lost() && L.s->rejected() && L.s->reject_reason() == reason && !L.s->wants_connection(L.now + 100000) && !L.s->submit(cmd(CommandType::Hatch, 1)) &&
                       !L.s->vote(0, true) && !L.s->chat("x", false) && L.s->mode() == ClientSession::Mode::Lost;
            };
            {   // Normal: the server says no on the live link
                LoneSession L;
                L.step(100);
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
                L.first().srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
                ASSERT_FALSE(L.first().cli->is_open());                                 // its link was closed by the session
            }
            {   // Normal, the Reject and the close in the same breath (the host says "superseded" just before it closes the old link): the word is read before the close is judged
                LoneSession L;
                L.step(100);
                L.first().srv->send(encode(RejectMsg{reason}));
                L.first().srv->close();
                L.step(100);
                ASSERT_TRUE(ended(L));
            }
            {   // Rejoining: the link was lost, a new one says Hello, the answer is a Reject
                LoneSession L;
                L.step(100);
                L.lose_link();
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
                ASSERT_TRUE(L.s->wants_connection(L.now));
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                ASSERT_EQ(w.count(MsgType::Hello), size_t{1});
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
                w.srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
                ASSERT_FALSE(w.cli->is_open());
            }
            {   // CatchingUp: the Welcome and the stream's announcement came, then a Reject
                LoneSession L;
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100}));
                L.step(100);
                ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
                w.srv->send(encode(RejectMsg{reason}));
                L.step(100);
                ASSERT_TRUE(ended(L));
            }
        }
        {   // a link that only closes (no word from the server) is not the end: the machine is on its way back, and it was not "rejected"
            LoneSession L;
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(!L.s->lost() && !L.s->rejected() && L.s->reconnecting() && L.s->wants_connection(L.now));
        }
        {   // a session that does not reconnect (a LAN guest, a room that holds no seats) ignores a Reject on its live link, as it always did
            LoneSession L(ClientSession::Config{}, false);
            L.step(100);
            L.first().srv->send(encode(RejectMsg{RejectReason::Superseded}));
            L.step(100);
            ASSERT_TRUE(!L.s->lost() && !L.s->rejected() && L.s->mode() == ClientSession::Mode::Normal);
            L.first().srv->close();                                                      // ... and a closed link is the end of its match as before
            L.step(100);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
        }
        {   // a session that has no key has no way back: a lost link is the end, at once, without an attempt (reconnect is on, the key is zero)
            LoneSession L;
            L.step(100);
            sim::SimulationEngine other;
            build_world(other, 1);
            ClientSession::Config nk;
            nk.player = 1;
            nk.host = kNoSeat;
            nk.migration = false;
            nk.reconnect = true;
            ClientSession keyless(other, nk);
            auto ends = L.net.connect({10, 0});
            keyless.set_connection(ends.second);
            keyless.start(L.now);
            ends.first->close();
            L.net.set_time(L.now + 20);
            keyless.update(L.now + 20);
            ASSERT_TRUE(keyless.lost() && !keyless.reconnecting() && !keyless.wants_connection(L.now + 100000));
        }
    } TEST_END();

    TEST_CASE("N2.65 Client: The Way Back: A New Link Is Asked For At Once And Again Every 2 s (attach(nullptr): No Link Could Be Made), The Hello Goes Out When The Link OPENS (Not Before: A TCP Link Is Connecting For A While), Once, With The Key And The Turns The Machine Has; The Give-Up Time Counts From The First Loss Through Every Attempt; An Attempt With No Welcome Is Abandoned; Leaving Or Finishing Ends The Attempts") {
        {   // the Hello of a machine that comes back, and when it goes out
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 10);
            L.step(600);
            ASSERT_EQ(L.s->runner().next_turn_expected(), 10u);
            L.lose_link();
            ASSERT_TRUE(L.s->wants_connection(L.now));
            auto ends = L.net.connect({10, 0});
            OpensLater later(ends.second, &L.now, L.now + 300);                         // the link opens in 300 ms
            L.wires.push_back(std::make_unique<LoneSession::Wire>());
            L.wires.back()->srv = ends.first;
            L.wires.back()->cli = &later;
            L.s->attach(&later, L.now);
            L.step(250);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{0});                        // nothing was written to a link that was not open
            L.step(150);
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{1});                        // it went out when the link opened ...
            L.step(300);
            ASSERT_EQ(L.last().count(MsgType::Hello), size_t{1});                        // ... once
            HelloMsg h;
            ASSERT_TRUE(decode(L.last().heard[0], h));
            ASSERT_TRUE(h.version == kProtocolVersion && h.key == L.key && h.have_turns == 10 && h.name == "Lone" && h.room == "R-1" && h.token == "tok");
        }
        {   // the Hello says how many turns the machine has RECEIVED (run or not), not how many it has run: the host's stream starts after the last one that the machine holds
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 40);
            L.step(60);                                                                  // they arrive; the runner has run only some of them
            ASSERT_EQ(L.s->runner().next_turn_expected(), 40u);
            ASSERT_TRUE(L.s->runner().next_turn_to_execute() < 40u);
            L.lose_link();
            ASSERT_TRUE(L.s->runner().next_turn_to_execute() < 40u);
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_TRUE(!w.heard.empty() && decode(w.heard[0], h));
            ASSERT_EQ(h.have_turns, 40u);
        }
        {   // attempts: at once, then 2 s after the last one began; attach(nullptr) counts as an attempt that could not start
            LoneSession L;
            L.step(100);
            L.lose_link();
            const uint32_t lost_at = L.now;
            ASSERT_TRUE(L.s->wants_connection(L.now));
            ASSERT_EQ(L.s->lost_since_ms(), L.now - 10);                                 // (it noticed in its update of the step before the last)
            std::vector<uint32_t> times;
            for (int attempt = 0; attempt < 3; ++attempt) {
                while (!L.s->wants_connection(L.now)) L.step(10);
                times.push_back(L.now);
                if (attempt == 1) {
                    L.s->attach(nullptr, L.now);                                         // no link could be made
                } else {
                    auto ends = L.net.connect({10, 0});
                    L.net.cut(ends.second);                                              // a link that is refused at once
                    L.s->attach(ends.second, L.now);
                }
                ASSERT_FALSE(L.s->wants_connection(L.now));
                L.step(20);
            }
            ASSERT_TRUE(times[0] - lost_at <= 20);
            ASSERT_TRUE(times[1] - times[0] >= 1990 && times[1] - times[0] <= 2030);
            ASSERT_TRUE(times[2] - times[1] >= 1990 && times[2] - times[1] <= 2030);
            ASSERT_EQ(L.s->reconnect_attempts(), 3u);
            ASSERT_EQ(L.s->lost_since_ms(), lost_at - 10);                               // the first loss: it did not move
        }
        {   // the give-up time (here 7 s) counts from the first loss, through every attempt
            ClientSession::Config c;
            c.reconnect_give_up_ms = 7000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            const uint32_t lost_at = L.now;
            while (!L.s->lost() && L.now - lost_at < 20000) {
                if (L.s->wants_connection(L.now)) {
                    auto ends = L.net.connect({10, 0});
                    L.net.cut(ends.second);
                    L.s->attach(ends.second, L.now);
                }
                L.step(10);
            }
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());                                // no word from the server: just no way back
            ASSERT_TRUE(L.now - lost_at >= 7000 && L.now - lost_at <= 7100);
            ASSERT_TRUE(L.s->reconnect_attempts() >= 3 && L.s->reconnect_attempts() <= 5);
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
        }
        {   // an attempt that gets no Welcome (here within 3 s) is abandoned: the link is closed, the next attempt is due at once (the attempt lasted more than 2 s)
            ClientSession::Config c;
            c.rejoin_timeout_ms = 3000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();                                       // the server never answers
            L.step(2900);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Rejoining));
            L.step(200);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
            ASSERT_FALSE(w.cli->is_open());
            ASSERT_TRUE(L.s->wants_connection(L.now));
        }
        {   // a link lost in the middle of the catch-up: back to Reconnecting, the give-up clock keeps the first loss, the next Hello says how many turns the machine has by now
            LoneSession L;
            L.step(100);
            L.lose_link();
            const uint32_t first_loss = L.s->lost_since_ms();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 300}));
            TurnBatchMsg b;
            b.first_turn = 0;
            for (uint32_t i = 0; i < 120; ++i) {
                TurnMsg t;
                t.turn = i;
                b.turns.push_back(t);
            }
            w.srv->send(encode(b));
            L.step(30);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            L.net.cut(w.cli);                                                            // the link dies with 120 of the 300 turns delivered
            L.step(50);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));
            ASSERT_EQ(L.s->lost_since_ms(), first_loss);
            L.step(2100);
            ASSERT_TRUE(L.s->wants_connection(L.now));
            LoneSession::Wire& w2 = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_EQ(w2.count(MsgType::Hello), size_t{1});
            ASSERT_TRUE(decode(w2.heard[0], h) && h.have_turns == 120 && h.key == L.key);
        }
        {   // leave() in each mode of the way back: no more attempts; the Leave goes out when there is a link
            for (int stage = 0; stage < 3; ++stage) {
                LoneSession L;
                L.step(100);
                L.lose_link();
                LoneSession::Wire* w = nullptr;
                if (stage >= 1) {
                    w = &L.attach_new();
                    L.step(100);
                }
                if (stage == 2) {
                    w->srv->send(encode(L.rejoin_welcome()));
                    w->srv->send(encode(CatchUpMsg{0, 50}));
                    L.step(50);
                    ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
                }
                L.s->leave();
                L.step(100);
                ASSERT_TRUE(L.s->lost() && !L.s->wants_connection(L.now + 100000));
                if (w != nullptr) ASSERT_EQ(w->count(MsgType::Leave), size_t{1});
            }
        }
        {   // finish(): the match is over, the server closing its links is no reason to come back
            LoneSession L;
            L.step(100);
            L.s->finish();
            L.first().srv->close();
            L.step(100);
            ASSERT_FALSE(L.s->lost() || L.s->reconnecting());
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
        }
        {   // a machine that was on its way back when the match ended (the owner finished the session): no link that is handed over after that is taken, no Hello goes out
            LoneSession L;
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(L.s->reconnecting());
            L.s->finish();
            ASSERT_FALSE(L.s->wants_connection(L.now + 100000));
            LoneSession::Wire& w = L.attach_new();
            L.step(200);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Reconnecting));      // the link was not taken
            ASSERT_EQ(w.count(MsgType::Hello), size_t{0});
            ASSERT_EQ(L.s->reconnect_attempts(), 0u);
        }
    } TEST_END();

    TEST_CASE("N2.66 Client: The Catch-Up Through A Scripted Server: The Stream Is Run At Once And Silently, Acknowledged At Every Higher Percent And When Its Queue Is Empty, CaughtUp Carries The State After Exactly The Announced Turns, The Machine Waits For The Server's Presence (Or A Live Turn) Before It Plays; Every Protocol Failure Ends It With \"Rejoin Failed\"; The Gating Of submit, chat And vote") {
        {   // the whole flow: 50 turns before the loss, 350 given now
            ClientSession::Config c;
            c.catch_up_ticks = 100;
            LoneSession L(c);
            L.step(100);
            L.give_turns(L.first(), 50);
            L.step(300);
            L.lose_link();
            ASSERT_EQ(L.s->runner().next_turn_expected(), 50u);
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            HelloMsg h;
            ASSERT_TRUE(decode(w.heard[0], h) && h.have_turns == 50);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{50, 400}));
            for (uint32_t first = 50; first < 400; first += 100) {                       // four batches of at most 100 empty turns
                TurnBatchMsg b;
                b.first_turn = first;
                for (uint32_t t = first; t < std::min<uint32_t>(first + 100, 400); ++t) {
                    TurnMsg tm;
                    tm.turn = t;
                    b.turns.push_back(tm);
                }
                w.srv->send(encode(b));
            }
            L.step(40);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));                       // nothing is given while it catches up
            ASSERT_FALSE(L.s->chat("x", false));
            ASSERT_FALSE(L.s->vote(0, true));
            L.step(200);
            std::vector<uint32_t> acks;
            bool caught_up = false;
            CaughtUpMsg claim;
            for (const auto& m : w.heard) {
                AckMsg a;
                if (peek_type(m) == MsgType::TurnAck && decode(m, a)) acks.push_back(a.turn);
                if (peek_type(m) == MsgType::CaughtUp) caught_up = decode(m, claim);
            }
            ASSERT_FALSE(acks.empty());
            for (size_t i = 1; i < acks.size(); ++i) ASSERT_TRUE(acks[i] > acks[i - 1]);   // every acknowledgement is further than the one before
            ASSERT_EQ(acks.back(), 399u);
            ASSERT_TRUE(caught_up && claim.turns == 400 && claim.hash == L.hash_after(400));        // the state after exactly those turns, and nothing was drawn on the way
            ASSERT_EQ(L.sim.current_tick(), 400u);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));      // it waits for the server's word
            ASSERT_EQ(L.s->catch_up_percent(), 100);
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));
            PresenceMsg here;                                                            // the server compared the states: the match is paused for somebody else
            here.missing.push_back(PresenceMsg::Entry{2, PresenceMsg::State::Absent, 40, 0});
            here.voters = 1;
            w.srv->send(encode(here));
            L.step(60);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
            ASSERT_TRUE(L.s->paused() && L.s->presence().missing.size() == 1);
            ASSERT_FALSE(L.s->submit(cmd(CommandType::Hatch, 1)));                       // a seat is missing: nothing can be given, the host would discard it
            ASSERT_TRUE(L.s->chat("hello", false));                                      // chat goes on
            ASSERT_TRUE(L.s->vote(2, true));
            w.srv->send(encode(PresenceMsg{}));                                          // the match runs
            L.step(60);
            ASSERT_FALSE(L.s->paused());
            ASSERT_TRUE(L.s->submit(cmd(CommandType::Hatch, 1)));
        }
        {   // a live turn instead of a Presence confirms too
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 0}));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            TurnMsg t;
            t.turn = 0;
            w.srv->send(encode(t));
            L.step(60);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::Normal));
            ASSERT_EQ(L.s->runner().next_turn_expected(), 1u);
        }
        {   // a server that thinks the machine has nothing (no turns when it said Hello) sends Start: the map is loaded, it says so once, and the stream follows
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            StartMsg start;
            start.map_name = "TEST.LVL";
            start.roster = 7;
            w.srv->send(encode(start));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::Loaded), size_t{1});
            w.srv->send(encode_begin());
            w.srv->send(encode(CatchUpMsg{0, 0}));
            L.step(60);
            ASSERT_EQ(w.count(MsgType::Loaded), size_t{1});
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{1});
        }
        {   // pings go on while it catches up (a link that is quiet for 10 s is a link that is gone)
            ClientSession::Config c;
            c.catch_up_ticks = 1;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            w.srv->send(encode(L.rejoin_welcome()));
            w.srv->send(encode(CatchUpMsg{0, 1000}));
            TurnBatchMsg b;
            for (uint32_t t = 0; t < 1000; ++t) {
                TurnMsg tm;
                tm.turn = t;
                b.turns.push_back(tm);
            }
            w.srv->send(encode(b));
            L.step(3500);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_TRUE(w.count(MsgType::Ping) >= 3);
            ASSERT_TRUE(L.s->catch_up_percent() >= 30 && L.s->catch_up_percent() <= 40);       // 350 of 1000 turns at 100 a second
            ASSERT_EQ(w.count(MsgType::CaughtUp), size_t{0});
            w.srv->send(encode(PresenceMsg{}));                                          // "the match runs", said to a machine that has not said CaughtUp: it is not back yet ...
            TurnMsg live;
            live.turn = 1000;
            w.srv->send(encode(live));                                                   // ... and a live turn does not bring it back either
            L.step(100);
            ASSERT_EQ(static_cast<int>(L.s->mode()), static_cast<int>(ClientSession::Mode::CatchingUp));
            ASSERT_TRUE(L.s->catch_up_percent() < 100);
        }
        {   // the protocol failures: each ends it, with "rejoin failed" (an honest server does none of them)
            enum Fail { NoRejoinFlag, CatchUpFromTheWrongTurn, BatchFromTheWrongTurn, BatchBeyondTheTotal, SecondCatchUp, BatchBeforeCatchUp, DesyncVerdict };
            for (int f = 0; f <= DesyncVerdict; ++f) {
                LoneSession L;
                L.step(100);
                L.give_turns(L.first(), 5);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                L.step(100);
                WelcomeMsg welcome = L.rejoin_welcome();
                if (f == NoRejoinFlag) welcome.flags = 0;
                w.srv->send(encode(welcome));
                const auto batch = [&](uint32_t first, uint32_t n) {
                    TurnBatchMsg b;
                    b.first_turn = first;
                    for (uint32_t t = first; t < first + n; ++t) {
                        TurnMsg tm;
                        tm.turn = t;
                        b.turns.push_back(tm);
                    }
                    return encode(b);
                };
                if (f == CatchUpFromTheWrongTurn) w.srv->send(encode(CatchUpMsg{3, 100}));
                else if (f == BatchBeforeCatchUp) w.srv->send(batch(5, 5));
                else w.srv->send(encode(CatchUpMsg{5, 100}));
                if (f == BatchFromTheWrongTurn) w.srv->send(batch(6, 5));
                if (f == BatchBeyondTheTotal) w.srv->send(batch(5, 200));
                if (f == SecondCatchUp) w.srv->send(encode(CatchUpMsg{5, 100}));
                if (f == DesyncVerdict) {
                    DesyncMsg d;
                    d.turn = 100;
                    d.player = 1;
                    w.srv->send(encode(d));
                }
                L.step(100);
                ASSERT_TRUE(L.s->lost() && L.s->rejected() && L.s->reject_reason() == RejectReason::RejoinFailed);
                ASSERT_EQ(L.s->desynced(), f == DesyncVerdict);
            }
        }
    } TEST_END();

    TEST_CASE("N2.67 Hold: Lag Is Not A Loss In A Room That Holds Seats: A Window That Stops For 5 s, And One That Stops For 9 s, Do Not Pause Anybody (The Others Are Told \"Bob Is Lagging\", It Catches Up At 4x); A Client That Keeps Talking But Whose Acknowledgements Never Arrive Is Told To The Others As Lagging And Dropped After 30 s Without Progress (Dropped For Good, No Pause); One That Is 60 s Behind Is Dropped Too: The Rules Of v0.0.94") {
        for (const uint32_t frozen_ms : {5000u, 9000u}) {
            HoldMatch m(3);
            m.run(3000);
            const uint32_t sealed_before = m.sealed();
            m.frozen_mask = 1u << 2;
            bool paused_ever = false;
            bool noticed = false;
            bool presence_ever = false;
            m.run(frozen_ms, true, [&](uint32_t) {
                paused_ever = paused_ever || m.host->paused();
                noticed = noticed || m.clients[0]->lagging_seat() == 2;
                presence_ever = presence_ever || m.clients[0]->paused() || m.clients[1]->paused();
            });
            ASSERT_FALSE(paused_ever || presence_ever);                                  // nobody is paused for a window that stopped
            ASSERT_TRUE(noticed);                                                        // they are told who lags (from 3 s behind)
            ASSERT_TRUE(m.sealed() - sealed_before >= frozen_ms / 50 - 2);               // the server kept sealing, every 50 ms
            ASSERT_TRUE(m.host->client_present(2) && !m.host->seat_held(2));
            m.frozen_mask = 0;
            m.run(10000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(m.clients[2]->mode() == ClientSession::Mode::Normal && m.host->client_present(2));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // it talks (pings, a second apart) but its acknowledgements and hashes never reach the host: the host cannot see it run
            HoldMatch m(3);
            m.run(3000);
            m.taps[1]->swallow_acks = true;
            const uint32_t swallowed_at = m.now;
            bool paused_ever = false;
            bool noticed = false;
            uint32_t dropped_after_ms = 0;
            m.run(40000, true, [&](uint32_t now) {
                paused_ever = paused_ever || m.host->paused();
                noticed = noticed || m.clients[0]->lagging_seat() == 1;
                if (dropped_after_ms == 0 && m.host->attendance().state(1) == Attendance::State::Dropped) dropped_after_ms = now - swallowed_at;
            });
            ASSERT_FALSE(paused_ever);                                                   // it is connected: that is lag, never a pause
            ASSERT_TRUE(noticed);
            ASSERT_TRUE(dropped_after_ms >= 29900 && dropped_after_ms <= 31000);         // 30 s without progress, then a drop (final: the seat is not held)
            ASSERT_FALSE(m.host->seat_held(1));
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
            ASSERT_TRUE(m.sims[0]->is_player_dropped(1) && m.sims[2]->is_player_dropped(1));
            ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->reject_reason() == RejectReason::Dropped);        // its machine came back with the key and was told
        }
        {   // one acknowledgement every 25 s: a laggard that makes progress (never 30 s without one, and each one says how far it really is). Told to the others, never dropped, never a pause
            HoldMatch m(3);
            m.run(3000);
            m.taps[1]->swallow_acks = true;
            m.taps[1]->let_one_ack_through_every_ms = 25000;
            bool paused_ever = false;
            m.run(100000, true, [&](uint32_t) { paused_ever = paused_ever || m.host->paused(); });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(m.host->client_present(1) && !m.host->seat_held(1));
        }
        {   // 60 s behind with progress (an acknowledgement of one more turn every 29 s, and a ping every second): dropped at 60 s of it, in a room that holds seats, and never a pause
            LoopbackNetwork net{3};
            sim::SimulationEngine referee;
            build_world(referee, 1);
            HostSession::Config hc;
            hc.host_player = kNoSeat;
            hc.hold_seats = true;
            HostSession host(referee, hc);
            std::array<SeatKey, sim::MAX_PLAYERS> keys{};
            keys[0] = key_with(1);
            keys[1] = key_with(2);
            host.set_seat_keys(keys);
            StartMsg st;
            st.map_name = "TEST.LVL";
            st.roster = 3;
            host.set_rejoin_start(st);
            auto e0 = net.connect({5, 0});
            auto e1 = net.connect({5, 0});
            host.add_client(0, e0.first);
            host.add_client(1, e1.first);
            host.start(0);
            uint32_t now = 0;
            uint32_t last_turn = 0;
            bool got_turn = false;
            uint32_t next_ack = 29000;
            uint32_t acked = 0;
            uint32_t dropped_at = 0;
            bool paused_ever = false;
            while (now < 80000 && dropped_at == 0) {
                now += 10;
                net.set_time(now);
                host.update(now);
                std::vector<uint8_t> msg;
                while (e0.second->poll(msg)) {
                    TurnMsg t;
                    if (peek_type(msg) == MsgType::Turn && decode(msg, t)) {
                        last_turn = t.turn;
                        got_turn = true;
                    }
                }
                while (e1.second->poll(msg)) {}
                if (got_turn) {
                    AckMsg a;
                    a.turn = last_turn;
                    e0.second->send(encode(a));
                }
                if (now + 10 >= next_ack) {
                    AckMsg a;
                    a.turn = ++acked;
                    e1.second->send(encode(a));
                    next_ack += 29000;
                }
                if (now % 1000 == 0) e1.second->send(encode_ping(PingMsg{now / 1000 + 1, now}));
                paused_ever = paused_ever || host.paused();
                if (!host.client_present(1)) dropped_at = now;
            }
            ASSERT_TRUE(dropped_at >= 59900 && dropped_at <= 60300);
            ASSERT_FALSE(paused_ever);
            ASSERT_EQ(host.attendance().state(1), Attendance::State::Dropped);
            ASSERT_TRUE(host.client_present(0));
        }
    } TEST_END();

    TEST_CASE("N2.68 Hold: Silence Is A Loss After 10 s (A Window That Hangs, A Link That Is Half Open And Delivers Nothing): The Match Pauses 10 s After The Last Word, The Others Read \"missing\" And Not \"lagging\"; 9 s Of Silence Is No Loss; The Machine That Comes Back Finds Its Link Closed And Gets Its Seat Back") {
        {
            HoldMatch m(3);
            m.run(4000);
            m.frozen_mask = 1u << 1;                                                     // the process hangs: it neither answers nor pings, its link stays open
            const uint32_t frozen_at = m.now;
            uint32_t paused_after_ms = 0;
            bool lag_notice = false;
            m.run(9500, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - frozen_at;
                lag_notice = lag_notice || m.clients[0]->lagging_seat() == 1;
            });
            ASSERT_EQ(paused_after_ms, 0u);                                              // 9.5 s: not yet
            ASSERT_TRUE(lag_notice);                                                     // (for 6 s of it the others were told that it lags: it has stopped running turns)
            m.run(1000, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - frozen_at;
            });
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);          // 10 s after the last thing it said (the last ack or ping before it hung)
            m.run(300);
            ASSERT_TRUE(m.host->seat_held(1) && m.host->attendance().state(1) == Attendance::State::Absent);
            ASSERT_TRUE(m.clients[0]->presence().missing.size() == 1 && m.clients[0]->presence().missing[0].seat == 1);      // "missing", not "lagging":
            ASSERT_EQ(m.clients[0]->lagging_seat(), 255);                                 // the notice of lag ended with the loss
            m.run(5000);
            m.frozen_mask = 0;                                                           // the machine wakes up: its link was closed by the host, it asks for a new one
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 8000));
            m.run(3000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            ASSERT_EQ(m.host->attendance().rejoins(), 1u);
        }
        {   // a half-open link: the host hears nothing, the machine hears everything and thinks that all is well
            HoldMatch m(3);
            m.run(4000);
            m.taps[2]->silent = true;
            const uint32_t at = m.now;
            uint32_t paused_after_ms = 0;
            m.run(9500, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - at;
            });
            ASSERT_EQ(paused_after_ms, 0u);
            m.run(1000, true, [&](uint32_t now) {
                if (paused_after_ms == 0 && m.host->paused()) paused_after_ms = now - at;
            });
            ASSERT_TRUE(paused_after_ms >= 10000 && paused_after_ms <= 10100);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 8000));      // the host closed the dead link: the machine found out and came back
            m.run(2000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.69 Hold Off: A Host That Does Not Hold Seats Behaves Exactly As Before There Was A Way Back: A Lost Link Is A Drop At Once (No Pause, No Presence Ever, The Same Tick Everywhere); A Machine With A Key That Tries To Come Back Is Told \"The Match Has Started\", One Without A Key Is Lost At Once; A Hanging Window Is Lag, Not A Loss") {
        {
            HoldOptions o;
            o.hold = false;
            o.keys = false;
            HoldMatch m(3, o);
            m.run(5000);
            m.cut(1);
            bool paused_ever = false;
            uint64_t drop_tick[3] = {0, 0, 0};
            m.run(3000, true, [&](uint32_t) {
                paused_ever = paused_ever || m.host->paused();
                if (drop_tick[0] == 0 && m.sims[0]->is_player_dropped(1)) drop_tick[0] = m.sims[0]->current_tick();
                if (drop_tick[1] == 0 && m.sims[2]->is_player_dropped(1)) drop_tick[1] = m.sims[2]->current_tick();
                if (drop_tick[2] == 0 && m.referee.is_player_dropped(1)) drop_tick[2] = m.referee.current_tick();
            });
            ASSERT_FALSE(paused_ever);
            ASSERT_TRUE(drop_tick[0] != 0 && drop_tick[0] == drop_tick[1] && drop_tick[1] == drop_tick[2]);
            ASSERT_TRUE(m.clients[1]->lost() && !m.clients[1]->rejected() && !m.clients[1]->reconnecting());       // no key, no way back: the match is over for it at once
            ASSERT_EQ(m.refusals, 0u);
            const uint32_t before_vote = m.host->violations(0);
            m.client_ends[0]->send(encode(VoteMsg{1, true}));                            // a Vote belongs to a room that holds seats: for this host it is a message that hosts do not receive
            m.run(100);
            ASSERT_EQ(m.host->violations(0), before_vote + 1);
            m.frozen_mask = 1u << 2;                                                     // a hanging window is lag: no loss, no drop for 30 s
            m.run(15000);
            ASSERT_TRUE(m.host->client_present(2) && !m.host->paused());
            m.frozen_mask = 0;
            m.run(5000);
            m.settle();
            for (uint8_t p = 0; p < 3; ++p) ASSERT_EQ(m.taps[p]->sent_of(MsgType::Presence), size_t{0});         // nothing of the reconnect protocol was ever said
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
        {   // the machine has a key (the room that it joined held seats; this host does not): it asks, and is told that the match has started
            HoldOptions o;
            o.hold = false;
            HoldMatch m(3, o);
            m.run(4000);
            m.cut(1);
            m.run(3000);
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Dropped);
            ASSERT_FALSE(m.host->paused());
            ASSERT_EQ(m.refusals, 1u);
            ASSERT_TRUE(m.clients[1]->lost() && m.clients[1]->rejected() && m.clients[1]->reject_reason() == RejectReason::MatchRunning);
            m.settle();
            ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[2]->state_hash() == m.referee.state_hash());
        }
    } TEST_END();

    TEST_CASE("N2.70 Hold: The Referee's Runner Can Hold Turns That Nobody Will Send (Fewer Than Its Buffer Asks For: At The Start Of A Match, And Right After A Resume): A Seat Lost In The First 50 ms, And One Lost Right After A Resume, Still Get Their Seats Back (The Referee Runs Its Queue For The Comparison)") {
        {   // lost before the referee's runner has started: one turn is sealed, the buffer asks for two
            HoldMatch m(3);
            m.run(30);
            ASSERT_TRUE(m.sealed() <= 1);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(100);
            ASSERT_TRUE(m.host->paused());
            ASSERT_TRUE(m.referee.current_tick() == 0 && m.host->runner().queued() <= 1);
            m.reload(1);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1] != nullptr && m.clients[1]->mode() == ClientSession::Mode::Normal; }, 10000));
            m.run(8000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
        }
        {   // lost again right after a resume: the referee's runner was restarted by the comparison, a turn or two are sealed and then it is paused again
            HoldMatch m(3);
            m.run(6000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(500);
            m.reload(1);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused(); }, 10000));
            m.auto_reconnect[2] = false;
            m.cut(2);                                                                    // about a tick after the match resumed
            m.run(200);
            ASSERT_TRUE(m.host->paused());
            m.reload(2);
            ASSERT_TRUE(m.until([&]() { return !m.host->paused() && m.clients[1] != nullptr && m.clients[2] != nullptr && m.clients[1]->mode() == ClientSession::Mode::Normal && m.clients[2]->mode() == ClientSession::Mode::Normal; }, 10000));
            m.run(6000);
            m.settle();
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_EQ(m.host->attendance().rejoins(), 2u);
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("N2.71 Hold: A Returning Connection Is Held To The Rules Of A Client: Flooding (3000 Pings At Once), Garbage And A Wrong Number Of Turns End The Attempt, The Others Are Not Disturbed; Commands It Sends Before The Host Has Compared Its State Are No Offence; A Wrong Hash Is Answered With A Desync To It Alone; A Right Hash Gives It The Seat; A Map That Cannot Be Loaded Is Told \"Rejoin Failed\"; The Stream Is Paced In Bytes By What It Acknowledges, And An Acknowledgement Of What Was Never Sent Opens Nothing") {
        const auto drain = [](Connection* c) {
            std::vector<std::vector<uint8_t>> out;
            std::vector<uint8_t> msg;
            while (c->poll(msg)) out.push_back(msg);
            return out;
        };
        const auto has = [](const std::vector<std::vector<uint8_t>>& got, MsgType t) {
            for (const auto& m : got) {
                if (peek_type(m) == t) return true;
            }
            return false;
        };
        {
            HoldOptions many;                                                               // (a seat is taken back twelve times a minute: this block says Hello five times in a few seconds)
            many.host.attendance.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            HoldMatch m(3, many);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(300);
            ASSERT_TRUE(m.host->paused());
            const auto rejoiner = [&](uint32_t have) {
                auto ends = m.open_link();
                HelloMsg h;
                h.name = "x";
                h.key = m.keys[2];
                h.have_turns = have;
                ends.second->send(encode(h));
                return ends.second;
            };
            {   // flooding: the budget of a connection is the budget of its seat (1000 a second)
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                ASSERT_EQ(m.host->rejoiners(), size_t{1});
                for (uint32_t i = 0; i < 3000; ++i) c->send(encode_ping(PingMsg{i + 1, 0}));
                m.run(600);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);        // the attempt is over, the seat is held
                drain(c);                                                                   // (what the host said before it closed is still to be read: only then the machine sees the close)
                ASSERT_FALSE(c->is_open());
                ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1) && m.host->violations(0) == 0 && m.host->violations(1) == 0);
            }
            {   // commands before the comparison are no offence (a client that has said CaughtUp may believe that it plays)
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                for (int i = 0; i < 100; ++i) {
                    CommandMsg msg;
                    msg.command = cmd(CommandType::Hatch, 2);
                    c->send(encode(msg));
                    c->send(encode_ping(PingMsg{static_cast<uint32_t>(i) + 1, 0}));
                }
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{1});
                ASSERT_TRUE(c->is_open());
                ASSERT_TRUE(has(drain(c), MsgType::Pong));                                  // and its pings are answered
                for (int i = 0; i < 8; ++i) c->send(std::vector<uint8_t>{200, 1, 2});         // garbage: eight of them throw it out
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
            }
            {   // CaughtUp with another number of turns than were announced
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                CaughtUpMsg bad;
                bad.turns = m.sealed() + 1;
                bad.hash = m.referee.state_hash();
                c->send(encode(bad));
                m.run(200);
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
            }
            {   // CaughtUp with a hash that is not the referee's: a Desync to it alone, the room does not fail, the seat stays away
                Connection* c = rejoiner(m.sealed());
                m.run(100);
                drain(c);
                CaughtUpMsg bad;
                bad.turns = m.sealed();
                bad.hash = sim::StateHash{1, 2, 3, 4, 5, 6, 7, 8};
                c->send(encode(bad));
                m.run(200);
                const auto got = drain(c);
                ASSERT_TRUE(has(got, MsgType::Desync));
                for (const auto& msg : got) {
                    DesyncMsg d;
                    if (peek_type(msg) == MsgType::Desync && decode(msg, d)) ASSERT_TRUE(d.player == 2 && d.host == m.referee.state_hash() && d.peer == bad.hash);
                }
                ASSERT_EQ(m.host->rejoiners(), size_t{0});
                ASSERT_TRUE(m.host->desyncs().empty() && !m.host->frozen());
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Absent);
                ASSERT_FALSE(m.clients[0]->desynced() || m.clients[1]->desynced());
            }
            {   // the right hash (a client that executed everything): the connection is the seat's, the others hear that the match runs
                Connection* c = rejoiner(m.sealed());
                m.run(300);
                drain(c);
                CaughtUpMsg good;
                good.turns = m.sealed();
                good.hash = m.referee.state_hash();
                c->send(encode(good));
                m.run(300);
                ASSERT_FALSE(m.host->paused());
                ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
                ASSERT_TRUE(m.host->client_present(2));
                ASSERT_TRUE(has(drain(c), MsgType::Presence));
                ASSERT_TRUE(!m.clients[0]->paused() && !m.clients[1]->paused());
            }
        }
        {   // a machine whose map cannot be loaded (Loaded not ok) is told that there is no way back for it
            HoldMatch m(3);
            m.run(4000);
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(300);
            auto ends = m.open_link();
            HelloMsg h;
            h.key = m.keys[1];
            h.have_turns = 0;
            ends.second->send(encode(h));
            m.run(300);
            ASSERT_TRUE(has(drain(ends.second), MsgType::Start));
            ends.second->send(encode(LoadedMsg{false}));
            m.run(300);
            bool told = false;
            for (const auto& msg : drain(ends.second)) {
                RejectMsg r;
                if (peek_type(msg) == MsgType::Reject && decode(msg, r)) told = r.reason == RejectReason::RejoinFailed;
            }
            ASSERT_TRUE(told);
            ASSERT_EQ(m.host->rejoiners(), size_t{0});
            ASSERT_EQ(m.host->attendance().state(1), Attendance::State::Absent);
        }
        {   // the stream is paced in bytes: a window of 64 KB, a batch of 48 KB at the most beyond it
            HoldOptions o;
            o.host.stream_window_bytes = 64 * 1024;
            HoldMatch m(3, o);
            std::vector<uint32_t> many;
            for (uint32_t i = 0; i < 32; ++i) many.push_back(1000 + i);
            m.run(8000, false, [&](uint32_t) {
                for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
                    for (int i = 0; i < 8; ++i) {
                        CommandMsg msg;
                        msg.command = cmd(CommandType::GroupMove, p, 255, 5, 5, many);
                        m.client_ends[p]->send(encode(msg));
                    }
                }
            });
            ASSERT_TRUE(m.host->log().bytes() > 1024u * 1024u);
            m.auto_reconnect[2] = false;
            m.cut(2);
            m.run(300);
            const uint32_t total = m.sealed();
            auto ends = m.open_link();
            Connection* c = ends.second;
            HelloMsg h;
            h.key = m.keys[2];
            h.have_turns = 0;
            c->send(encode(h));
            m.run(300);
            ASSERT_TRUE(has(drain(c), MsgType::Start));
            c->send(encode(LoadedMsg{true}));
            struct Got {
                size_t bytes{0};
                uint32_t turns{0};
                uint32_t last_turn{0};
                bool begin{false};
                bool catch_up{false};
            } got;
            const auto take = [&]() {
                for (const auto& msg : drain(c)) {
                    TurnBatchMsg b;
                    CatchUpMsg cu;
                    if (peek_type(msg) == MsgType::TurnBatch && decode(msg, b)) {
                        got.bytes += msg.size();
                        got.turns += static_cast<uint32_t>(b.turns.size());
                        got.last_turn = b.first_turn + static_cast<uint32_t>(b.turns.size()) - 1;
                    } else if (peek_type(msg) == MsgType::Begin) {
                        got.begin = true;
                    } else if (peek_type(msg) == MsgType::CatchUp && decode(msg, cu)) {
                        got.catch_up = cu.first_turn == 0 && cu.total_turns == total;
                    }
                }
            };
            m.run(400);
            take();
            ASSERT_TRUE(got.begin && got.catch_up);
            ASSERT_TRUE(got.bytes >= 64u * 1024u && got.bytes <= 64u * 1024u + kBatchBytes + 64u);      // the window, and the batch that crossed it
            ASSERT_TRUE(got.turns < total);
            const size_t before = got.bytes;
            c->send(encode(AckMsg{0xFFFFFFFEu}));                                           // it acknowledges turns that were never sent: the window opens to what was sent, not to the log
            m.run(400);
            take();
            ASSERT_TRUE(got.bytes - before <= 64u * 1024u + kBatchBytes + 64u);
            ASSERT_TRUE(got.turns < total);
            for (int guard = 0; guard < 2000 && got.turns < total; ++guard) {                // an honest client: acknowledges what it has run
                c->send(encode(AckMsg{got.last_turn}));
                m.run(20);
                take();
            }
            ASSERT_EQ(got.turns, total);
            ASSERT_TRUE(m.host->rejoiners() == 1 && m.host->attendance().state(2) == Attendance::State::CatchingUp);
            CaughtUpMsg good;
            good.turns = total;
            good.hash = m.referee.state_hash();
            c->send(encode(good));
            m.run(300);
            ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Present);
            ASSERT_FALSE(m.host->paused());
        }
    } TEST_END();

    TEST_CASE("N2.72 Hold: The Whole Timeline Is The Same From Every Clock: A Match That Starts 8 s Before The Wrap Of The 32-Bit Clock, At Its Signed Half, And At 0 Pauses, Opens The Vote, Votes And Drops At Cap At The Same Moments (To 10 ms), Across The Wrap") {
        struct Timeline {
            uint32_t paused_after{0};
            uint32_t told_after{0};
            uint32_t vote_after{0};
            uint32_t dropped_after{0};
            uint32_t resumed_after{0};
            uint32_t sealed_at_cap{0};
            uint32_t waited_at_20s{0};
            uint16_t cap_at_20s{0};
        };
        const auto scenario = [&](uint32_t origin) {
            HoldOptions o;
            o.origin = origin;
            o.host.attendance.max_pause_ms = 45000;                                      // the cap (45 s) is after the vote (30 s)
            HoldMatch m(3, o);
            m.run(5000);
            m.auto_reconnect[2] = false;
            m.cut(2);
            const uint32_t cut_at = m.now;
            Timeline t;
            m.run(20000, true, [&](uint32_t now) {
                if (t.paused_after == 0 && m.host->paused()) t.paused_after = now - cut_at;
                if (t.told_after == 0 && m.clients[0]->paused()) t.told_after = now - cut_at;
                if (t.vote_after == 0 && m.clients[0]->presence().vote_seat == 2) t.vote_after = now - cut_at;
            });
            t.waited_at_20s = m.clients[0]->presence().missing.empty() ? 0u : m.clients[0]->presence().missing[0].waited_s;
            t.cap_at_20s = m.clients[0]->presence().cap_s;
            m.run(30000, true, [&](uint32_t now) {
                if (t.vote_after == 0 && m.clients[0]->presence().vote_seat == 2) t.vote_after = now - cut_at;
                if (t.dropped_after == 0 && m.host->attendance().state(2) == Attendance::State::Dropped) {
                    t.dropped_after = now - cut_at;
                    t.sealed_at_cap = m.sealed();
                }
                if (t.dropped_after != 0 && t.resumed_after == 0 && !m.host->paused()) t.resumed_after = now - cut_at;
            });
            return t;
        };
        const Timeline base = scenario(0);
        ASSERT_TRUE(base.paused_after > 0 && base.paused_after <= 20 && base.told_after > 0 && base.told_after <= 120);
        ASSERT_TRUE(base.vote_after >= 30000 && base.vote_after <= 31200);
        ASSERT_TRUE(base.dropped_after >= 45000 && base.dropped_after <= 45100);
        ASSERT_TRUE(base.waited_at_20s >= 19 && base.waited_at_20s <= 21 && base.cap_at_20s >= 24 && base.cap_at_20s <= 26);
        for (const uint32_t origin : {0xFFFFE000u, 0x7FFFFE00u, 0x80000000u, 0xFFFFFC18u - 5000u}) {
            const Timeline t = scenario(origin);
            ASSERT_EQ(t.paused_after, base.paused_after);
            ASSERT_EQ(t.told_after, base.told_after);
            ASSERT_EQ(t.vote_after, base.vote_after);
            ASSERT_EQ(t.dropped_after, base.dropped_after);
            ASSERT_EQ(t.resumed_after, base.resumed_after);
            ASSERT_EQ(t.sealed_at_cap, base.sealed_at_cap);
            ASSERT_EQ(t.waited_at_20s, base.waited_at_20s);
            ASSERT_EQ(t.cap_at_20s, base.cap_at_20s);
        }
    } TEST_END();

    TEST_CASE("N2.73 Turn Log And The Server's Budget: Every Log Takes What It Has ALLOCATED (Its Capacity, Which Grows By Doubling And Is At Most Twice What It Stores) From A Budget That Many Logs Share; A Log That The Budget Cannot Grow Is DEAD: It Frees What It Held And Gives It Back At Once, And Serves Nothing; Destroying Or Releasing A Log Gives Everything Back Once") {
        const std::vector<TurnMsg> turns = real_looking_turns(2000, 11, 60);
        std::vector<size_t> cost(turns.size());                                          // what each turn takes: its packed bytes and its 4 bytes of index
        for (size_t i = 0; i < turns.size(); ++i) cost[i] = pack_turn(turns[i]).size() + sizeof(uint32_t);
        {   // accounting: the budget holds exactly what the logs have allocated, whoever appends, and that is never less than what they hold nor more than twice that and the first allocation
            LogBudget budget(1u << 30);
            TurnLog one(1u << 30, &budget);
            TurnLog two(1u << 30, &budget);
            for (size_t i = 0; i < 700; ++i) {
                ASSERT_TRUE(one.append(turns[i]));
                if (i % 3 == 0) ASSERT_TRUE(two.append(turns[i / 3]) || two.turns() != i / 3);        // (two takes a turn every third step, in order, from the front)
                ASSERT_EQ(budget.used(), uint64_t{one.capacity_bytes()} + uint64_t{two.capacity_bytes()});
                ASSERT_TRUE(one.capacity_bytes() >= one.bytes() && one.capacity_bytes() <= 2 * one.bytes() + TurnLog::kFirstBlobBytes + TurnLog::kFirstOffsets * sizeof(uint32_t));
                ASSERT_TRUE(two.capacity_bytes() >= two.bytes());
            }
            ASSERT_TRUE(budget.used() > 0 && budget.limit() == (1u << 30));
            ASSERT_TRUE(budget.used() > uint64_t{one.bytes()} + uint64_t{two.bytes()});          // (the capacity is what is charged: more than what the logs hold)
        }
        {   // the first allocation is 1.5 KiB: a budget of less keeps no log, a budget of exactly that keeps the 128 turns that its index holds (a turn without a command costs 6 bytes: 2 and 4)
            const size_t first = TurnLog::kFirstBlobBytes + TurnLog::kFirstOffsets * sizeof(uint32_t);
            ASSERT_EQ(first, size_t{1536});
            for (const size_t room : {first - 1, first, first + 1}) {
                LogBudget budget(room);
                TurnLog log(1u << 30, &budget);
                TurnMsg empty;
                uint32_t made = 0;
                while (made < 1000) {
                    empty.turn = made;
                    ASSERT_TRUE(budget.used() <= budget.limit());
                    if (!log.append(empty)) break;
                    ASSERT_EQ(budget.used(), uint64_t{log.capacity_bytes()});
                    ++made;
                }
                ASSERT_EQ(made, room < first ? 0u : 128u);                                // exactly the turns that the index holds; the next one needs a growth that the budget cannot give
                ASSERT_FALSE(log.usable());
                ASSERT_TRUE(log.turns() == 0 && log.bytes() == 0 && log.capacity_bytes() == 0);      // DEAD: it held made turns, and freed them
                ASSERT_EQ(budget.used(), uint64_t{0});                                    // ... and gave every byte back at the moment that it died
                ASSERT_FALSE(log.append(empty));
                std::vector<uint8_t> packed;
                ASSERT_EQ(log.read(0, 10, 1000, packed), 0u);
            }
        }
        {   // a budget that cannot double a log's vectors but can add a quarter keeps the log going a while longer, and then the log is dead and holds nothing
            LogBudget budget(2047);
            TurnLog log(1u << 30, &budget);
            TurnMsg empty;
            uint32_t made = 0;
            while (made < 1000) {
                empty.turn = made;
                if (!log.append(empty)) break;
                ASSERT_TRUE(budget.used() <= 2047);
                ++made;
            }
            ASSERT_TRUE(made > 128 && made < 256);                                        // past the first allocation (the index grew by a quarter), short of the doubling that 2048 would have held
            ASSERT_TRUE(!log.usable() && budget.used() == 0 && log.capacity_bytes() == 0);
        }
        {   // the edge of the budget for real turns: it never passes the budget, the log dies at the first turn that it cannot hold, and its bytes come back
            for (const uint64_t limit : {uint64_t{4000}, uint64_t{9000}, uint64_t{31000}}) {
                LogBudget budget(limit);
                TurnLog log(1u << 30, &budget);
                size_t made = 0;
                while (made < turns.size() && log.append(turns[made])) {
                    ASSERT_TRUE(budget.used() <= limit && budget.used() == log.capacity_bytes());
                    ++made;
                }
                ASSERT_TRUE(made > 0 && made < turns.size());
                size_t held = 0;
                for (size_t i = 0; i < made; ++i) held += cost[i];
                ASSERT_TRUE(held <= limit);                                               // what it held fitted the budget ...
                ASSERT_FALSE(log.usable());
                ASSERT_EQ(budget.used(), uint64_t{0});                                    // ... and it gave all of it back
                ASSERT_TRUE(log.turns() == 0 && log.bytes() == 0);
                ASSERT_FALSE(log.append(turns[made]));                                    // it stays dead
            }
        }
        {   // two logs share one budget: the one that comes later finds less, and the one that dies frees the room that the other may grow into
            uint64_t half = 0;
            for (size_t i = 0; i < 200; ++i) half += cost[i];
            LogBudget budget(half * 3);
            TurnLog first(1u << 30, &budget);
            TurnLog second(1u << 30, &budget);
            for (size_t i = 0; i < 200; ++i) ASSERT_TRUE(first.append(turns[i]));
            size_t got = 0;
            while (got < 2000 && second.append(turns[got])) ++got;
            ASSERT_TRUE(got > 50 && got < 2000);                                          // the second log got what was left
            ASSERT_FALSE(second.usable());
            ASSERT_TRUE(first.usable());
            ASSERT_TRUE(budget.used() <= budget.limit());
            ASSERT_EQ(budget.used(), uint64_t{first.capacity_bytes()});                   // (the dead one holds nothing)
            for (size_t i = 200; i < 400; ++i) ASSERT_TRUE(first.append(turns[i]));       // the first goes on in the room that the second gave back
        }
        {   // destruction and release give everything back, once
            LogBudget budget(1u << 30);
            {
                TurnLog a(1u << 30, &budget);
                TurnLog b(1u << 30, &budget);
                for (size_t i = 0; i < 300; ++i) {
                    ASSERT_TRUE(a.append(turns[i]));
                    ASSERT_TRUE(b.append(turns[i]));
                }
                const uint64_t both = budget.used();
                ASSERT_EQ(both, uint64_t{a.capacity_bytes()} + uint64_t{b.capacity_bytes()});
                a.release();
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});                   // a gave back all that it held
                ASSERT_TRUE(a.turns() == 0 && a.bytes() == 0 && a.capacity_bytes() == 0 && !a.usable());
                ASSERT_FALSE(a.append(turns[300]));                                       // a released log takes nothing
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});
                std::vector<uint8_t> packed;
                ASSERT_EQ(a.read(0, 10, 1000, packed), 0u);
                ASSERT_EQ(a.bytes_between(0, 10), size_t{0});
                a.release();                                                              // twice is harmless
                ASSERT_EQ(budget.used(), uint64_t{b.capacity_bytes()});
            }                                                                             // the destructors: a gives nothing more, b gives its bytes
            ASSERT_EQ(budget.used(), uint64_t{0});
        }
        {   // a log without a budget is what it was
            TurnLog plain(1u << 20);
            for (size_t i = 0; i < 100; ++i) ASSERT_TRUE(plain.append(turns[i]));
            ASSERT_TRUE(plain.capacity_bytes() >= plain.bytes());
            plain.release();
            ASSERT_TRUE(plain.turns() == 0 && plain.bytes() == 0 && !plain.usable());
        }
        {   // the log's own limit counts what it holds (its size), the budget what it has allocated: the log dies at its limit and gives its capacity back
            LogBudget budget(1u << 30);
            TurnLog log(5000, &budget);
            size_t made = 0;
            while (made < turns.size() && log.append(turns[made])) ++made;
            ASSERT_TRUE(made > 10 && made < turns.size());
            ASSERT_FALSE(log.usable());
            ASSERT_EQ(budget.used(), uint64_t{0});
        }
        LogBudget over(10);                                                               // a budget never gives more than it was given
        over.give(1000);
        ASSERT_EQ(over.used(), uint64_t{0});
        ASSERT_TRUE(over.take(10) && !over.take(1) && over.used() == 10);
        over.give(4);
        ASSERT_TRUE(over.take(4) && !over.take(1));
    } TEST_END();

    TEST_CASE("N2.74 Client: A Silent Server Is Silent In Real Time, And Only When It Was Asked (note_gap, The Rule Of A Page That The Browser Does Not Wake): The Gap That A Wake-Up Hands Over Counts Against A Server That Was Asked And Did Not Answer; One That Was Not Asked Yet Gets The Gap Only Up To 3 s Short Of The 10 s (2.9 s After The Question No, 3.1 s Yes); A Live Server That Answers Its Pings, Or Has Data Waiting, Is Never Condemned; The Way Back Counts Its Waits In Real Time Too: The Catch-Up, A Hello Without A Welcome, The Attempts, The Give-Up") {
        using Mode = ClientSession::Mode;
        const auto is = [](const LoneSession& L, Mode m) { return L.s->mode() == m; };
        {   // the control: a wake-up that hands over no gap (the clock moves by the second that the application allows) is no reason to say anything of the server
            LoneSession L;
            L.step(100);
            L.wake(1000);
            ASSERT_TRUE(is(L, Mode::Normal));
            ASSERT_FALSE(L.s->note_gap(0));                                              // nothing to count
        }
        {   // a server that is dead (it was asked: a ping went out before the page fell asleep) and a page that sleeps for a minute: the whole gap counts, at once, as on a machine whose clock kept time
            LoneSession L;
            L.step(100);
            ASSERT_EQ(L.first().count(MsgType::Ping), size_t{1});
            L.wake(60000);
            ASSERT_TRUE(is(L, Mode::Reconnecting));
            ASSERT_TRUE(L.s->reconnecting() && !L.s->lost() && !L.s->rejected());
            ASSERT_TRUE(L.s->wants_connection(L.now));                                    // and the way back begins
        }
        {   // a server that was heard just before the sleep and dies meanwhile has not been asked: the wake-up asks it (the ping goes out) and counts the gap only up to 3 s short of the 10 s
            const auto sleep_after_an_answer = [](LoneSession& L) {
                L.first().pong = true;
                L.step(100);                                                              // the first ping was answered and the answer heard
                L.first().pong = false;                                                   // the server dies
                L.wake(60000);                                                            // the page wakes after a minute: the question is asked now
            };
            {
                LoneSession L;
                sleep_after_an_answer(L);
                ASSERT_TRUE(is(L, Mode::Normal));                                         // not condemned for a silence that it had no chance to end
            }
            for (const uint32_t after_ms : {2900u, 3000u, 3001u, 3100u}) {                // the next wake-up, that long after the question: the answer is overdue, the whole gap counts
                LoneSession L;
                sleep_after_an_answer(L);
                L.wake(after_ms);
                ASSERT_EQ(is(L, Mode::Reconnecting), after_ms > 3000);                    // 7 s were counted at the first wake-up: 10 s of silence are more than 10 s from 3001 ms on
                ASSERT_EQ(is(L, Mode::Normal), after_ms <= 3000);
            }
            {   // wake-ups every 1.5 s (a page that is awake): the same moment, 3 s after the question
                LoneSession L;
                sleep_after_an_answer(L);
                int wakes = 0;
                while (is(L, Mode::Normal) && wakes < 20) {
                    L.wake(1500);
                    ++wakes;
                }
                ASSERT_TRUE(is(L, Mode::Reconnecting));
                ASSERT_EQ(wakes, 3);                                                      // 3 s after the question the silence is 10 s, 4.5 s after it is more
            }
        }
        {   // a live server that holds its turns says nothing but answers its pings (the answer is a message, which wakes the page): a wake-up a minute for three minutes never makes it silent
            LoneSession L;
            L.first().pong = true;
            L.step(100);
            for (int minute = 0; minute < 3; ++minute) {
                L.wake(60000);
                L.step(30);                                                               // the ping went out at the wake-up, the answer comes back
                ASSERT_TRUE(is(L, Mode::Normal));
            }
            ASSERT_TRUE(L.first().count(MsgType::Ping) >= 4);                             // it was asked every time
            L.first().pong = false;                                                       // ... until it dies: asked and not answered, the next wake-up has it
            L.wake(60000);
            L.step(30);
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(60000);
            ASSERT_TRUE(is(L, Mode::Reconnecting));
        }
        {   // a live server with data waiting in the link: what waited was heard (it is read before the gap is counted), so nothing counts, and the silence after it starts from zero
            LoneSession L;
            L.step(100);
            L.give_turns(L.first(), 3);
            L.now += 1000;
            L.net.set_time(L.now);
            L.s->update(L.now);
            ASSERT_EQ(L.s->runner().next_turn_expected(), 3u);
            ASSERT_FALSE(L.s->note_gap(59000));                                           // heard in this very update
            L.step(10);
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(3100);                                                                 // 3.1 s of real silence after the data: no reason to say anything
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(6000);                                                                 // 9.1 s
            ASSERT_TRUE(is(L, Mode::Normal));
            L.wake(1500);                                                                 // 10.6 s: the question that went out after the data was never answered
            ASSERT_TRUE(is(L, Mode::Reconnecting));
        }
        {   // the machine that catches up is told the same way: its pings are the question, the stream's messages and the pongs are the answer
            ClientSession::Config c;
            c.catch_up_ticks = 1;                                                         // a slow machine: the stream below is never done
            for (const bool alive : {true, false}) {
                LoneSession L(c);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                w.pong = alive;
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100000}));
                L.step(100);
                ASSERT_TRUE(is(L, Mode::CatchingUp));
                for (int minute = 0; minute < 3 && is(L, Mode::CatchingUp); ++minute) {
                    L.wake(60000);
                    L.step(30);
                }
                ASSERT_EQ(is(L, Mode::CatchingUp), alive);                                // the one that is answered is never condemned
                ASSERT_EQ(is(L, Mode::Reconnecting), !alive);                             // the one that is not is condemned by the first wake-up: the attempt is over
                ASSERT_FALSE(L.s->lost());
            }
            {   // heard just before the sleep: the first wake-up asks, the second condemns
                LoneSession L(c);
                L.step(100);
                L.lose_link();
                LoneSession::Wire& w = L.attach_new();
                w.pong = true;
                L.step(100);
                w.srv->send(encode(L.rejoin_welcome()));
                w.srv->send(encode(CatchUpMsg{0, 100000}));
                L.step(100);
                w.pong = false;
                L.wake(60000);
                ASSERT_TRUE(is(L, Mode::CatchingUp));
                L.wake(5000);
                ASSERT_TRUE(is(L, Mode::Reconnecting));
            }
        }
        {   // a Hello that no Welcome has answered: the server was asked with it, the wait counts in full (an attempt is abandoned after 10 s, not after 10 wake-ups)
            LoneSession L;
            L.step(100);
            L.lose_link();
            LoneSession::Wire& w = L.attach_new();
            L.step(100);
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(8800);                                                                 // 8.9 s since the Hello
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(1300);                                                                 // 10.2 s
            ASSERT_TRUE(is(L, Mode::Reconnecting));
            ASSERT_FALSE(w.cli->is_open());
        }
        {   // no link: the attempts are 2 s apart in real time, and the give-up (here 2 minutes) counts from the first loss in real time
            ClientSession::Config c;
            c.reconnect_give_up_ms = 120000;
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            ASSERT_TRUE(L.s->wants_connection(L.now));
            L.s->attach(nullptr, L.now);                                                  // no link could be made: the next attempt is due in 2 s
            ASSERT_FALSE(L.s->wants_connection(L.now));
            L.wake(60000);                                                                // the page was not woken for a minute
            ASSERT_TRUE(L.s->wants_connection(L.now));
            ASSERT_FALSE(L.s->lost());
            L.s->attach(nullptr, L.now);
            L.wake(40000);                                                                // 100 s since the loss
            ASSERT_TRUE(L.s->reconnecting() && !L.s->lost());
            L.wake(30000);                                                                // 130 s
            ASSERT_TRUE(L.s->lost() && !L.s->rejected() && !L.s->reconnecting());         // no word from the server, no way back
        }
        {   // the same in the middle of an attempt: a Hello that is waiting counts toward the give-up
            ClientSession::Config c;
            c.reconnect_give_up_ms = 120000;
            c.rejoin_timeout_ms = 1000000;                                                // (the attempt itself is not abandoned)
            LoneSession L(c);
            L.step(100);
            L.lose_link();
            L.attach_new();
            L.step(100);
            L.wake(100000);
            ASSERT_TRUE(is(L, Mode::Rejoining));
            L.wake(30000);
            ASSERT_TRUE(L.s->lost() && !L.s->rejected());
        }
        {   // the guards: a session that was not started, and one that is not waiting for a server any more, have nothing to count
            sim::SimulationEngine sim0;
            build_world(sim0, 1);
            ClientSession::Config c0;
            c0.player = 1;
            c0.host = kNoSeat;
            c0.migration = false;
            ClientSession fresh(sim0, c0);
            ASSERT_FALSE(fresh.note_gap(5000));
            LoneSession L;
            L.step(100);
            L.s->leave();
            L.step(100);
            ASSERT_TRUE(L.s->lost());
            ASSERT_FALSE(L.s->note_gap(60000));
        }
    } TEST_END();
}
