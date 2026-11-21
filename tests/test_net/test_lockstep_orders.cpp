// What a seat may have sealed (docs/NETWORK_PORT.md, "The order budget"): the sequencer's budget of orders (Sequencer::Config::order_burst, orders_per_second), the question that takes out of an order the ants that
// the engine would not carry out (HostSession::vet_ants, the sequencer's vetter), and the host's counters and mark (heavy_order_seats). The sealed turn is what every machine runs, so what is dropped is never
// in it; an honest player's turns are what they were.
#include "lockstep_fixtures.hpp"

namespace {

// The commands that a seat gives at the pace of `per_second` a second for `seconds`, one turn at a time (the clock of the budget is the sealed turns): returns the commands that were dropped for the budget
uint32_t give_at_pace(Sequencer& seq, uint8_t seat, uint32_t per_second, uint32_t seconds) {
    uint32_t given = 0;
    for (uint32_t turn = 0; turn < seconds * kTurnsPerSecond; ++turn) {
        const uint32_t due = ((turn + 1) * per_second) / kTurnsPerSecond - (turn * per_second) / kTurnsPerSecond;
        for (uint32_t i = 0; i < due; ++i) seq.submit_checked(seat, cmd(CommandType::GroupMove, seat, 255, static_cast<int16_t>(given++ % 50), 0, {7}));
        seq.seal();
    }
    return seq.dropped_over_budget(seat);
}

// The turns that the host of a ServerMatch sealed, as sent (the runner keeps the last 600)
std::vector<std::vector<uint8_t>> sealed_turns(HostSession& host) {
    std::vector<std::vector<uint8_t>> out;
    for (uint32_t t = 0; t < host.turns_sealed(); ++t) {
        const TurnMsg* turn = host.runner().logged_turn(t);
        if (turn != nullptr) out.push_back(encode(*turn));
    }
    return out;
}

}  // namespace

void run_order_tests() {
    TEST_CASE("S2.59 Sequencer, The Budget Of Orders: A Seat Can Have 512 Orders Sealed At Once And Earns 8 A Second; The Rest Is Dropped And Counted, The Seat Is Marked, Nobody Else's Budget Is Touched, And What Is Dropped Is In No Turn") {
        Sequencer seq;
        seq.set_active(0, true);
        seq.set_active(1, true);
        uint32_t queued = 0;
        uint32_t over = 0;
        for (int i = 0; i < 700; ++i) {
            const Submit r = seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, static_cast<int16_t>(i), 0, {7}));
            queued += r == Submit::Queued ? 1u : 0u;
            over += r == Submit::OverBudget ? 1u : 0u;
        }
        ASSERT_EQ(queued, 512u);
        ASSERT_EQ(over, 188u);
        ASSERT_EQ(seq.dropped_over_budget(1), 188u);
        const uint32_t emptied_at = seq.next_turn();                               // (the budget is empty from here: what it holds again is what the turns since earned)
        ASSERT_EQ(seq.heavy_seats(), 1u << 1);                                    // seat 1 is marked
        ASSERT_EQ(seq.dropped_over_budget(0), 0u);
        ASSERT_TRUE(seq.submit_checked(0, cmd(CommandType::Hatch, 0)) == Submit::Queued);      // seat 0 has its own budget
        size_t sealed = 0;
        int16_t expected_x = 0;
        while (seq.queued() > 0 || seq.carried(1) > 0) {
            for (const sim::Command& c : seq.seal().commands) {
                if (c.issuer != 1) continue;
                ASSERT_EQ(c.tile_x, expected_x++);                              // the first 512 in the order they came, the dropped 188 in no turn
                ++sealed;
            }
        }
        ASSERT_EQ(sealed, size_t{512});
        // the budget earns 8 a second, one turn's worth (0.4 of an order) at every seal: the orders that fit are the earnings of the turns since the budget was empty
        for (int i = 0; i < 20; ++i) seq.seal();
        uint32_t fit = 0;
        for (int i = 0; i < 20; ++i) fit += seq.submit_checked(1, cmd(CommandType::Hatch, 1)) == Submit::Queued ? 1u : 0u;
        ASSERT_EQ(fit, (seq.next_turn() - emptied_at) * 8u / kTurnsPerSecond);     // 28 turns (eight that sealed the orders, twenty after them): 11 orders, and the twelfth does not fit
        ASSERT_EQ(fit, 11u);
        // a seat that rests for 70 s has all 512 again, never more
        for (int i = 0; i < 70 * static_cast<int>(kTurnsPerSecond); ++i) seq.seal();
        fit = 0;
        for (int i = 0; i < 700; ++i) fit += seq.submit_checked(1, cmd(CommandType::Hatch, 1)) == Submit::Queued ? 1u : 0u;
        ASSERT_EQ(fit, 512u);
        // a budget of 0 is no budget (the tests of the quotas of a turn want none)
        Sequencer::Config none;
        none.orders_per_second = 0;
        Sequencer free_seq(none);
        free_seq.set_active(1, true);
        uint32_t all = 0;
        for (int i = 0; i < 1200; ++i) all += free_seq.submit_checked(1, cmd(CommandType::Hatch, 1)) == Submit::Queued ? 1u : 0u;
        ASSERT_EQ(all, 64u + 1024u);                                              // (the quotas of the turn and of the carry are the only limits then)
        ASSERT_EQ(free_seq.heavy_seats(), 0u);
        // a seat that is not at the table, or a command that clients do not send, is Refused, not counted as a seat that is too busy
        ASSERT_TRUE(seq.submit_checked(2, cmd(CommandType::Hatch, 2)) == Submit::Refused);
        ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Drop, 1)) == Submit::Refused);
        ASSERT_EQ(seq.dropped_over_budget(2), 0u);
        // a host that resumes starts every seat's budget again
        Sequencer moved;
        moved.set_active(1, true);
        for (int i = 0; i < 700; ++i) moved.submit(1, cmd(CommandType::Hatch, 1));
        moved.resume(500);
        moved.set_active(1, true);
        fit = 0;
        for (int i = 0; i < 700; ++i) fit += moved.submit(1, cmd(CommandType::Hatch, 1)) ? 1u : 0u;
        ASSERT_EQ(fit, 512u);
    } TEST_END();

    TEST_CASE("S2.60 Sequencer, The Budget Is Far Above Honest Play: A Seat That Gives 6 Orders A Second (The Busiest Second Of The Recordings Measured), Ten Clicks Of 48 Commands In A Second, And A Stuck Uplink's 76 Orders Is Never Dropped, Not In An Hour; 8 A Second Is Allowed For Ever; 12 A Second Is Marked") {
        {
            Sequencer seq;
            seq.set_active(1, true);
            ASSERT_EQ(give_at_pace(seq, 1, 6, 3600), 0u);                         // an hour at the busiest second of the recordings, all the time
            ASSERT_EQ(seq.heavy_seats(), 0u);
        }
        {
            Sequencer seq;
            seq.set_active(1, true);
            ASSERT_EQ(give_at_pace(seq, 1, 8, 3600), 0u);                         // the earnings: what a seat may do for ever
        }
        {
            Sequencer seq;
            seq.set_active(1, true);
            const uint32_t dropped = give_at_pace(seq, 1, 12, 600);
            ASSERT_TRUE(dropped > 0);                                             // 12 a second for ten minutes is no honest play
            ASSERT_EQ(seq.heavy_seats(), 1u << 1);
            // ... and what is sealed is what the budget earned: 512 and 8 a second, never more
            ASSERT_TRUE(12u * 600u - dropped <= 512u + 8u * 600u + 1u);
        }
        {   // the HUD's biggest click is 48 commands (hud.hpp): ten of them in a second (the carry's own case), none of them is over the budget
            Sequencer seq;
            seq.set_active(1, true);
            for (int click = 0; click < 10; ++click) {
                for (int i = 0; i < 48; ++i) ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, 20, 30, {1u + static_cast<uint32_t>(i)})));
                seq.seal();
            }
            ASSERT_EQ(seq.dropped_over_budget(1), 0u);
        }
        {   // a stuck uplink that is let go after 5 s delivers the 76 orders of a player who gave 15 a second (docs/NETWORK_PORT.md): every one is sealed
            Sequencer seq;
            seq.set_active(1, true);
            for (int i = 0; i < 76; ++i) ASSERT_TRUE(seq.submit(1, cmd(CommandType::GroupMove, 1, 255, static_cast<int16_t>(i), 0, {7})));
            ASSERT_EQ(seq.dropped_over_budget(1), 0u);
        }
    } TEST_END();

    TEST_CASE("S2.61 Sequencer, The Vetter: An Order That Names No Ant That The Seat Can Order Is Not Sealed (NoAnts) And Costs No Budget; The Ants It Keeps Are The Only Ones In The Turn; A Seat That Sends Such Orders Is Not Marked For Its Rate") {
        Sequencer seq;
        seq.set_vetter([](uint8_t, sim::Command& c) {
            if (!sim::has_ant_list(c.type)) return true;
            c.ants.erase(std::remove_if(c.ants.begin(), c.ants.end(), [](uint32_t id) { return id >= 100; }), c.ants.end());
            return !c.ants.empty();
        });
        seq.set_active(1, true);
        ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, 3, 4, {5, 150, 6})) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, 3, 4, {150, 151})) == Submit::NoAnts);
        ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Hatch, 1)) == Submit::Queued);      // (a command with no ant list is let through by this vetter)
        ASSERT_EQ(seq.dropped_no_ants(1), 1u);
        const TurnMsg turn = seq.seal();
        ASSERT_EQ(turn.commands.size(), size_t{2});
        ASSERT_TRUE(turn.commands[0].ants == (std::vector<uint32_t>{5, 6}));
        // 1,000 orders of nothing do not use the budget: what is left of the 512 is sealed, and the seat is marked for the orders that are over the budget only
        for (int i = 0; i < 1000; ++i) ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, 3, 4, {150})) == Submit::NoAnts);
        ASSERT_EQ(seq.heavy_seats(), 0u);
        uint32_t fit = 0;
        for (int i = 0; i < 700; ++i) fit += seq.submit(1, cmd(CommandType::GroupMove, 1, 255, 1, 1, {1})) ? 1u : 0u;
        ASSERT_EQ(fit, 510u);                                                      // (two of the 512 went to the orders above, and the turn that was sealed earned 0.4 of one)
        ASSERT_EQ(seq.dropped_no_ants(1), 1001u);
        ASSERT_EQ(seq.heavy_seats(), 1u << 1);                                      // (marked now for the 190 orders that were over the budget, and only for those)
        ASSERT_EQ(seq.dropped_over_budget(1), 190u);
    } TEST_END();

    TEST_CASE("S2.62 HostSession::vet_ants On A Real Engine: Ants Of Other Players, The Id 0, Ids That No Ant Has And None Is About To Have, And Repeats Are Taken Out, In The Order The Rest Were Given; An Ant Of The Seat Stays Whether It Is Alive Or Dead; A Command With No Ant List Is Let Through") {
        sim::SimulationEngine engine;
        const Ids ids = build_world(engine, 3);
        const uint32_t mine = ids.ants[1][0];
        const uint32_t also_mine = ids.ants[1][3];
        const uint32_t theirs = ids.ants[2][0];
        const uint32_t horizon = engine.next_ant_id();
        ASSERT_TRUE(engine.ant_claim(mine, 1) == sim::SimulationEngine::AntClaim::Own);
        ASSERT_TRUE(engine.ant_claim(theirs, 1) == sim::SimulationEngine::AntClaim::Foreign);
        ASSERT_TRUE(engine.ant_claim(horizon + 5, 1) == sim::SimulationEngine::AntClaim::Unborn);
        ASSERT_TRUE(engine.ant_claim(0, 1) == sim::SimulationEngine::AntClaim::Unborn);
        {   // a mixed order: the seat's own ants in the order given, once each
            Command c = cmd(CommandType::GroupAttack, 1, 255, 9, 9, {theirs, also_mine, 0, mine, also_mine, 4000000000u, horizon + 5000, ids.ants[3][2]});
            ASSERT_TRUE(HostSession::vet_ants(engine, 1, c));
            ASSERT_TRUE(c.ants == (std::vector<uint32_t>{also_mine, mine}));
            ASSERT_EQ(c.tile_x, 9);                                                // (nothing else of the command changes)
        }
        {   // only what is not the seat's: nothing is left, so the order is dropped
            Command c = cmd(CommandType::Stop, 1, 255, 0, 0, {theirs, ids.ants[0][0], 0});
            ASSERT_FALSE(HostSession::vet_ants(engine, 1, c));
        }
        {   // an id that the engine will hand out in a moment is the seat's honest order (a machine that is a turn ahead has seen the newborn ant): it stays
            Command c = cmd(CommandType::GroupMove, 1, 255, 1, 1, {mine, horizon + 3});
            ASSERT_TRUE(HostSession::vet_ants(engine, 1, c));
            ASSERT_EQ(c.ants.size(), size_t{2});
        }
        {   // an ant that died is still the seat's: the engine passes over it, and the order is sealed as it was sent
            engine.kill_unit(mine);
            Command c = cmd(CommandType::GroupMove, 1, 255, 1, 1, {mine, also_mine});
            ASSERT_TRUE(HostSession::vet_ants(engine, 1, c));
            ASSERT_TRUE(c.ants == (std::vector<uint32_t>{mine, also_mine}));
        }
        {   // the other kinds of command are none of the question's business
            Command hatch = cmd(CommandType::Hatch, 1);
            ASSERT_TRUE(HostSession::vet_ants(engine, 1, hatch));
            Command invite = cmd(CommandType::AllianceInvite, 1, 2);
            ASSERT_TRUE(HostSession::vet_ants(engine, 1, invite) && invite.other_player == 2);
        }
    } TEST_END();

    TEST_CASE("S2.63 Dedicated Server: A Seat That Sends Orders As Fast As It Can Gets 512 And 8 A Second Sealed, Is Marked, Is Not Dropped And Is Not A Violation; The Others' Orders Are All Run, And Every Machine Runs The Same Turns") {
        ServerMatch m(1, 3, {30, 5}, {}, {}, true);
        uint32_t run_by_1 = 0;
        uint32_t run_by_2 = 0;
        m.host->runner().set_on_command([&](const sim::Command& c, const sim::CommandResult&) {
            if (c.issuer == 1 && c.type == CommandType::GroupMove) ++run_by_1;
            if (c.issuer == 2 && c.type == CommandType::GroupMove) ++run_by_2;
        });
        m.run(10000, false);
        uint32_t sent_by_1 = 0;
        uint32_t sent_by_2 = 0;
        uint32_t step = 0;
        m.run(20000, false, [&](uint32_t) {
            for (size_t i = 0; i < 3; ++i) {                                       // 300 a second from seat 1 (a message budget of 1,000 a second is not met)
                m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, static_cast<int16_t>(sent_by_1 % 60), static_cast<int16_t>(step % 60), {m.ids.ants[1][i]}));
                ++sent_by_1;
            }
            if (++step % 25 == 0) {                                                // 4 a second from seat 2
                m.clients[2]->submit(cmd(CommandType::GroupMove, 2, 255, static_cast<int16_t>(sent_by_2 % 60), 1, {m.ids.ants[2][0]}));
                ++sent_by_2;
            }
        });
        m.run(5000, false);
        m.settle();
        ASSERT_TRUE(sent_by_1 >= 5900);
        ASSERT_EQ(m.host->orders_over_budget(1) + run_by_1, sent_by_1);            // every order was run or dropped for the budget: none was lost anywhere else
        ASSERT_TRUE(run_by_1 >= 512 && run_by_1 <= 512u + 8u * 21u);               // 512 at once and 8 a second for the 20 s of the flood of orders
        ASSERT_EQ(m.host->heavy_order_seats(), 1u << 1);
        ASSERT_EQ(run_by_2, sent_by_2);                                            // seat 2 is not touched
        ASSERT_EQ(m.host->orders_over_budget(2), 0u);
        ASSERT_TRUE(m.host->client_present(1) && m.host->client_present(2));       // nobody is dropped
        ASSERT_EQ(m.host->violations(1), 0u);
        ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
        // what was dropped is in no turn, so every machine's log is the host's
        const uint32_t sealed = m.host->turns_sealed();
        const uint32_t first = sealed > LockstepRunner::kTurnLogTurns - 10 ? sealed - (static_cast<uint32_t>(LockstepRunner::kTurnLogTurns) - 10) : 0u;
        for (uint32_t t = first; t < sealed; ++t) {
            const TurnMsg* host_turn = m.host->runner().logged_turn(t);
            ASSERT_TRUE(host_turn != nullptr);
            for (auto& c : m.clients) {
                const TurnMsg* turn = c->runner().logged_turn(t);
                ASSERT_TRUE(turn != nullptr && encode(*turn) == encode(*host_turn));
            }
        }
    } TEST_END();

    TEST_CASE("S2.64 Dedicated Server: An Order That Names Other Players' Ants Or Ants That Cannot Exist Is In No Turn, And The Ants Of One That Names Both Are Only The Seat's Own, Once Each; It Is Counted, It Is Not A Violation, And Nothing Desyncs") {
        ServerMatch m(1, 3, {30, 5}, {}, {}, true);
        std::vector<std::vector<uint32_t>> run_ants;                               // the ants of the orders of seat 1 as the referee executes them
        m.host->runner().set_on_command([&](const sim::Command& c, const sim::CommandResult&) {
            if (c.issuer == 1 && c.type == CommandType::GroupMove) run_ants.push_back(c.ants);
        });
        m.run(8000, false);
        const uint32_t mine = m.ids.ants[1][0];
        const uint32_t also_mine = m.ids.ants[1][1];
        m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 5, 5, {m.ids.ants[2][0], m.ids.ants[2][1]}));       // only the ants of seat 2
        m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 6, 6, {0, 4000000000u, 123456}));                   // only ids that no ant has
        m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 7, 7, {m.ids.ants[2][0], also_mine, mine, 0, mine, 99999999u}));
        m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 8, 8, {mine}));
        m.run(4000, false);
        m.settle();
        ASSERT_EQ(run_ants.size(), size_t{2});                                      // the third and the fourth order
        ASSERT_TRUE(run_ants[0] == (std::vector<uint32_t>{also_mine, mine}));
        ASSERT_TRUE(run_ants[1] == (std::vector<uint32_t>{mine}));
        ASSERT_EQ(m.host->orders_without_ants(1), 2u);
        ASSERT_EQ(m.host->orders_over_budget(1), 0u);
        ASSERT_EQ(m.host->heavy_order_seats(), 0u);                                 // (not a matter of rate)
        ASSERT_EQ(m.host->violations(1), 0u);
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
    } TEST_END();

    TEST_CASE("S2.65 Dedicated Server: Fast But Honest Play (Every Seat 5 Orders A Second, A Click Of 48 Commands Now And Then, Their Own Ants Only) Loses No Order And Marks Nobody, And Its Turns Are Byte For Byte The Turns Of A Server With No Budget") {
        struct Played {
            uint32_t dropped{99};
            uint8_t heavy{99};
            bool equal{false};
            std::vector<std::vector<uint8_t>> turns;
        };
        const auto play = [](bool budget) {
            HostSession::Config hc;
            if (!budget) hc.sequencer.orders_per_second = 0;
            ServerMatch m(4, 4, {30, 5}, hc);
            uint32_t step = 0;
            m.run(40000, false, [&](uint32_t) {
                ++step;
                for (uint8_t p = 0; p < 4; ++p) {
                    if (step % 20 == p) m.clients[p]->submit(cmd(CommandType::GroupMove, p, 255, static_cast<int16_t>(step % 60), static_cast<int16_t>((step / 7) % 60), {m.ids.ants[p][step % 6]}));       // 5 a second
                    if (step % 700 == 5u + p) {                                                                                                                   // a click on a big selection
                        for (uint32_t i = 0; i < 48; ++i) m.clients[p]->submit(cmd(CommandType::GroupAttack, p, 255, 3, 3, {m.ids.ants[p][i % 6]}));
                    }
                }
            });
            m.settle();
            Played out;
            out.dropped = 0;
            for (uint8_t p = 0; p < 4; ++p) out.dropped += m.host->orders_over_budget(p) + m.host->orders_without_ants(p);
            out.heavy = m.host->heavy_order_seats();
            out.equal = m.host->desyncs().empty() && m.all_equal();
            out.turns = sealed_turns(*m.host);
            return out;
        };
        const Played with = play(true);
        const Played without = play(false);
        ASSERT_TRUE(with.equal && without.equal);
        ASSERT_EQ(with.dropped, 0u);
        ASSERT_EQ(with.heavy, 0u);
        ASSERT_EQ(without.dropped, 0u);
        ASSERT_TRUE(with.turns.size() > 500 && with.turns == without.turns);
    } TEST_END();
}
