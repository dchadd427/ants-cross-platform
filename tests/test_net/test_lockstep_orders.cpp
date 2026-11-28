// What a seat may have sealed (docs/NETWORK_PORT.md, "The order budgets"): the sequencer's two budgets (Sequencer::Config: order_burst and orders_per_second for the commands that carry ants, control_burst and controls_per_second for the others), the question that takes out of an order the ants that
// the engine would not carry out (HostSession::vet_ants, the sequencer's vetter), and the host's counters and mark (heavy_order_seats). The sealed turn is what every machine runs, so what is dropped is never
// in it; an honest player's turns are what they were.
#include "lockstep_fixtures.hpp"

#include <array>

namespace {

// The rule of the budgets, written out a second time for the tests (not read from the code): the stores of one seat in thousandths of a command, 6,144 orders that earn 12 a second (600 a turn) and 512 control
// commands that earn 8 (400 a turn), a seat's first Quit taking nothing
struct Model {
    uint32_t orders{6144u * 1000u};
    uint32_t control{512u * 1000u};
    bool quit{false};
    bool takes(const Command& c) {
        if (c.type == CommandType::Quit && !quit) {
            quit = true;
            return true;
        }
        uint32_t& store = sim::has_ant_list(c.type) ? orders : control;
        if (store < 1000u) return false;
        store -= 1000u;
        return true;
    }
    void seal() {
        orders = std::min(6144u * 1000u, orders + 600u);
        control = std::min(512u * 1000u, control + 400u);
    }
};

// Gives a seat the commands that `plan` says for each of `turns` turns, one turn at a time, to a sequencer and to the model, and counts what the sequencer did against what the model says it must
struct Fate {
    uint32_t taken{0};
    uint32_t dropped{0};
    uint32_t disagreed{0};
};
Fate run_against_model(Sequencer& seq, Model& model, uint8_t seat, uint32_t turns, const std::function<std::vector<Command>(uint32_t)>& plan) {
    Fate fate;
    for (uint32_t turn = 0; turn < turns; ++turn) {
        for (const Command& c : plan(turn)) {
            const bool expect = model.takes(c);
            const Submit got = seq.submit_checked(seat, c);
            if (got == Submit::Queued) ++fate.taken;
            else ++fate.dropped;
            if (expect != (got == Submit::Queued) || (!expect && got != Submit::OverBudget)) ++fate.disagreed;
        }
        seq.seal();
        model.seal();
    }
    return fate;
}

Command group_move(uint8_t seat, int16_t x, int16_t y = 0, uint32_t ants = 1) {
    std::vector<uint32_t> list;
    for (uint32_t i = 0; i < ants; ++i) list.push_back(7 + i);
    return cmd(CommandType::GroupMove, seat, 255, x, y, list);
}

// The commands that a seat gives at the pace of `per_second` a second for `seconds` (one at a time: the HUD's clicks of several commands are give_clicks), turn by turn; returns what was dropped
uint32_t give_at_pace(Sequencer& seq, uint8_t seat, uint32_t per_second, uint32_t seconds) {
    uint32_t given = 0;
    for (uint32_t turn = 0; turn < seconds * kTurnsPerSecond; ++turn) {
        const uint32_t due = ((turn + 1) * per_second) / kTurnsPerSecond - (turn * per_second) / kTurnsPerSecond;
        for (uint32_t i = 0; i < due; ++i) seq.submit_checked(seat, group_move(seat, static_cast<int16_t>(given++ % 50)));
        seq.seal();
    }
    return seq.dropped_over_budget(seat);
}

// Clicks on a selection of `commands_per_click` chunks (the HUD sends one command for every 32 ants, all at once, to the same tile): `clicks_per_second` of them a second for `seconds`, the clicks spread over the second
uint32_t give_clicks(Sequencer& seq, uint8_t seat, uint32_t clicks_per_second, uint32_t commands_per_click, uint32_t seconds) {
    uint32_t sent = 0;
    for (uint32_t turn = 0; turn < seconds * kTurnsPerSecond; ++turn) {
        const uint32_t clicks = ((turn + 1) * clicks_per_second) / kTurnsPerSecond - (turn * clicks_per_second) / kTurnsPerSecond;
        for (uint32_t c = 0; c < clicks; ++c) {
            for (uint32_t i = 0; i < commands_per_click; ++i) seq.submit_checked(seat, group_move(seat, static_cast<int16_t>(sent % 50), 4, 32));
            ++sent;
        }
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
    TEST_CASE("S2.59 Sequencer, The Budgets: Orders (The Commands That Carry Ants) Have A Store Of 6,144 That Earns 12 A Second And Control Commands One Of 512 That Earns 8; A Command Over Its Store Is Dropped, Counted And Marks The Seat, Never Refused; The Other Seats' Stores Are Not Touched; The Commands Dropped Are In No Turn; Every Decision Is The Model's") {
        Sequencer seq;
        seq.set_active(0, true);
        seq.set_active(1, true);
        Model model;
        // 200 turns of 40 group orders and 30 Hatch commands at once (the control store of 512 goes first), 400 turns of rest, then 100 turns of 64 group orders again (the orders store is not full but never above its 6,144)
        const auto flood = [&](uint32_t turn) {
            std::vector<Command> out;
            if (turn < 200) {
                for (int i = 0; i < 40; ++i) out.push_back(group_move(1, static_cast<int16_t>(turn % 50)));
                for (int i = 0; i < 30; ++i) out.push_back(cmd(CommandType::Hatch, 1));
            } else if (turn >= 600) {
                for (int i = 0; i < 64; ++i) out.push_back(group_move(1, static_cast<int16_t>(turn % 50)));
            }
            return out;
        };
        const Fate fate = run_against_model(seq, model, 1, 700, flood);
        ASSERT_EQ(fate.disagreed, 0u);
        ASSERT_TRUE(fate.dropped > 1000 && fate.taken > 6000);
        ASSERT_EQ(seq.dropped_over_budget(1), fate.dropped);
        ASSERT_EQ(seq.heavy_seats(), 1u << 1);                                    // seat 1 is marked
        ASSERT_EQ(seq.dropped_over_budget(0), 0u);                                // seat 0 has stores of its own
        ASSERT_TRUE(seq.submit_checked(0, cmd(CommandType::Hatch, 0)) == Submit::Queued && seq.submit_checked(0, group_move(0, 1)) == Submit::Queued);
        // what is dropped is in no turn: the commands that the turns hold are the ones that were taken, in the order they came
        Sequencer count;
        count.set_active(1, true);
        Model again;
        std::vector<int16_t> expected;
        std::vector<int16_t> sealed;
        int16_t next = 0;
        for (uint32_t turn = 0; turn < 400; ++turn) {
            for (int i = 0; i < 40; ++i) {
                const Command c = group_move(1, next++);
                if (again.takes(c)) {
                    expected.push_back(c.tile_x);
                    ASSERT_TRUE(count.submit_checked(1, c) == Submit::Queued);
                } else {
                    ASSERT_TRUE(count.submit_checked(1, c) == Submit::OverBudget);
                }
            }
            again.seal();
            for (const sim::Command& c : count.seal().commands) sealed.push_back(c.tile_x);
        }
        ASSERT_TRUE(sealed == expected && expected.size() > 6000 && expected.size() < 16000);
        // a flood that is too fast for the turns: what cannot wait (64 in the turn and 1,024 that wait) is dropped like the rest of the flood, never an offence that throws a seat out
        Sequencer burst;
        burst.set_active(1, true);
        uint32_t queued = 0;
        uint32_t over = 0;
        uint32_t refused = 0;
        for (int i = 0; i < 2000; ++i) {
            const Submit r = burst.submit_checked(1, group_move(1, 1));
            queued += r == Submit::Queued ? 1u : 0u;
            over += r == Submit::OverBudget ? 1u : 0u;
            refused += r == Submit::Refused ? 1u : 0u;
        }
        ASSERT_EQ(queued, 64u + 1024u);
        ASSERT_EQ(over, 2000u - 64u - 1024u);
        ASSERT_EQ(refused, 0u);
        ASSERT_EQ(burst.heavy_seats(), 1u << 1);
        // a budget of 0 is no budget (the tests of the quotas of a turn want none): only the quotas of a turn and of the carry limit then, and a command beyond them is Refused
        Sequencer::Config none;
        none.orders_per_second = 0;
        none.controls_per_second = 0;
        Sequencer free_seq(none);
        free_seq.set_active(1, true);
        uint32_t all = 0;
        uint32_t refused_free = 0;
        for (int i = 0; i < 1200; ++i) {
            const Submit r = free_seq.submit_checked(1, i % 2 == 0 ? cmd(CommandType::Hatch, 1) : group_move(1, 1));
            all += r == Submit::Queued ? 1u : 0u;
            refused_free += r == Submit::Refused ? 1u : 0u;
        }
        ASSERT_EQ(all, 64u + 1024u);
        ASSERT_EQ(refused_free, 1200u - 64u - 1024u);
        ASSERT_EQ(free_seq.heavy_seats(), 0u);
        // a seat that is not at the table, or a command that clients do not send, is Refused, not counted as a seat that is too busy
        ASSERT_TRUE(seq.submit_checked(2, cmd(CommandType::Hatch, 2)) == Submit::Refused);
        ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Drop, 1)) == Submit::Refused);
        ASSERT_EQ(seq.dropped_over_budget(2), 0u);
        // a host that resumes starts every seat's stores again (and its first Quit is free again): the orders and the control commands are used up (56 group orders and 8 Hatch a turn) and a Quit is sent ...
        Sequencer moved;
        moved.set_active(1, true);
        Model before;
        Fate used = run_against_model(moved, before, 1, 300, [](uint32_t turn) {
            std::vector<Command> o;
            for (int i = 0; i < 56; ++i) o.push_back(group_move(1, 1));
            for (int i = 0; i < 8; ++i) o.push_back(cmd(CommandType::Hatch, 1));
            if (turn == 299) o.push_back(cmd(CommandType::Quit, 1));
            return o;
        });
        ASSERT_TRUE(used.dropped > 0 && used.disagreed == 0);
        moved.resume(500);
        moved.set_active(1, true);
        // ... and after the resume the control store holds its 512 again, and when it is used up again the seat's first Quit is free again
        Model fresh;
        used = run_against_model(moved, fresh, 1, 25, [](uint32_t turn) {
            std::vector<Command> o;
            if (turn < 24) {
                for (int i = 0; i < 64; ++i) o.push_back(cmd(CommandType::Hatch, 1));
            } else {
                o.push_back(cmd(CommandType::Quit, 1));
            }
            return o;
        });
        ASSERT_TRUE(used.disagreed == 0 && used.taken >= 512u + 1u);
        used = run_against_model(moved, fresh, 1, 120, [](uint32_t) { std::vector<Command> o; for (int i = 0; i < 64; ++i) o.push_back(group_move(1, 1)); return o; });
        ASSERT_TRUE(used.disagreed == 0 && used.taken >= 6144u);
    } TEST_END();

    TEST_CASE("S2.60 Sequencer, The Budgets Are Far Above Honest Play And Bound A Flood: An Army Of 200 Ants Clicked 4 Times A Second For 5 Minutes (7 Commands A Click), A Fight Of 100 Ants Clicked 12 Times A Second, Ten Clicks Of 48 Commands, A Stuck Uplink's 76 Orders And An Hour Of 7.5 A Second Lose Nothing; One Seat Cannot Fill The 16 MiB Turn Log In 2 Hours Nor Pass 1 MiB Of Recording In Under 100 Seconds") {
        {   // an army of 200 ants: a click is 7 commands (six of 32 ants and one of 8), 4 clicks a second, five minutes: 8,400 commands
            Sequencer seq;
            seq.set_active(1, true);
            ASSERT_EQ(give_clicks(seq, 1, 4, 7, 300), 0u);
            ASSERT_EQ(seq.heavy_seats(), 0u);
        }
        {   // a fight of 100 ants (4 commands a click) at 12 clicks a second: 48 commands a second, for a whole minute
            Sequencer seq;
            seq.set_active(1, true);
            ASSERT_EQ(give_clicks(seq, 1, 12, 4, 60), 0u);
        }
        {   // the same fight in bursts: 20 s at 12 clicks a second, then 2 clicks a second, for five minutes
            Sequencer seq;
            seq.set_active(1, true);
            for (int minute = 0; minute < 5; ++minute) {
                ASSERT_EQ(give_clicks(seq, 1, 12, 4, 20), 0u);
                ASSERT_EQ(give_clicks(seq, 1, 2, 4, 40), 0u);
            }
        }
        {   // an hour at the busiest second of the recordings measured, all the time, and an hour of 7.5 a second
            Sequencer seq;
            seq.set_active(1, true);
            ASSERT_EQ(give_at_pace(seq, 1, 6, 3600), 0u);
            ASSERT_EQ(give_at_pace(seq, 1, 12, 3600), 0u);                         // (12 a second is what the store earns: for ever)
        }
        {   // the HUD's biggest click is 48 commands (hud.hpp): ten of them in a second (the carry's own case), then the player goes on
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
            for (int i = 0; i < 76; ++i) ASSERT_TRUE(seq.submit(1, group_move(1, static_cast<int16_t>(i))));
            ASSERT_EQ(seq.dropped_over_budget(1), 0u);
        }
        {   // 40 commands a second for ten minutes is no play: it is dropped beyond what the store holds and earns, and the seat is marked
            Sequencer seq;
            seq.set_active(1, true);
            const uint32_t dropped = give_at_pace(seq, 1, 40, 600);
            ASSERT_EQ(seq.heavy_seats(), 1u << 1);
            const uint32_t taken = 40u * 600u - dropped;
            ASSERT_TRUE(taken >= 6144u + 12u * 600u - 12u && taken <= 6144u + 12u * 600u);       // (what the store held and earned)
        }
        {   // a seat that sends as fast as the server takes it (160 a second), for the longest match (2 hours), every command of 32 ants (137 bytes in the recording): 12 of the turn log's 16 MiB, and 1 MiB of recording not before 100 s
            Sequencer seq;
            seq.set_active(1, true);
            uint64_t bytes = 0;
            uint32_t second_of_a_mebibyte = 0;
            std::vector<uint32_t> ants;
            for (uint32_t i = 0; i < 32; ++i) ants.push_back(100 + i);
            for (uint32_t turn = 0; turn < 7200u * kTurnsPerSecond; ++turn) {
                for (int i = 0; i < 8; ++i) seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, 5, 5, ants));
                for (const sim::Command& c : seq.seal().commands) bytes += 1 + sim::encoded_size(c);
                if (second_of_a_mebibyte == 0 && bytes > (1u << 20)) second_of_a_mebibyte = turn / kTurnsPerSecond + 1;
            }
            ASSERT_TRUE(bytes < (uint64_t{16} << 20) * 4 / 5 && bytes > (uint64_t{8} << 20));
            ASSERT_TRUE(second_of_a_mebibyte >= 100 && second_of_a_mebibyte <= 200);
        }
    } TEST_END();

    TEST_CASE("S2.61 Sequencer, Quit And The Control Commands: A Seat's First Quit Takes Nothing From Any Store And Is Never Refused For A Full Carry; The Others Are Charged To The Control Store, A Second Quit Too; A Seat That Used Up Its Orders Can Still Say Them, And One That Says Them Without End Is Dropped Like The Rest") {
        {   // the carry is full (2,000 orders at once: 64 in the turn, 1,024 that wait, the rest dropped): the first Quit goes in all the same, behind the others; a second one finds the carry full
            Sequencer seq;
            seq.set_active(1, true);
            for (int i = 0; i < 2000; ++i) seq.submit_checked(1, group_move(1, 1));
            ASSERT_EQ(seq.heavy_seats(), 1u << 1);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Quit, 1)) == Submit::Queued);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Quit, 1)) == Submit::OverBudget);
            bool sealed_quit = false;
            for (int turn = 0; turn < 40 && !sealed_quit; ++turn) {
                for (const sim::Command& c : seq.seal().commands) sealed_quit = sealed_quit || (c.issuer == 1 && c.type == CommandType::Quit);
            }
            ASSERT_TRUE(sealed_quit);
        }
        {   // the orders are used up (64 a turn for 200 turns, every decision the model's): the next order is dropped, and Hatch, an answer to an invitation and the first Quit are not
            Sequencer seq;
            seq.set_active(1, true);
            Model model;
            const Fate fate = run_against_model(seq, model, 1, 200, [](uint32_t) { std::vector<Command> o; for (int i = 0; i < 64; ++i) o.push_back(group_move(1, 1)); return o; });
            ASSERT_TRUE(fate.disagreed == 0 && fate.dropped > 5000);
            seq.submit_checked(1, group_move(1, 2));                                                // (the last turn's earnings may be enough for one order, never for two)
            ASSERT_TRUE(seq.submit_checked(1, group_move(1, 3)) == Submit::OverBudget);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Hatch, 1)) == Submit::Queued);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::AllianceAccept, 1, 2)) == Submit::Queued);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::AllianceDeny, 1, 3)) == Submit::Queued);
            ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::Quit, 1)) == Submit::Queued);
        }
        {   // a seat that says Hatch as fast as it can (64 a turn): the control store holds 512 and earns 8 a second, the model decides each one, and the seat is marked
            Sequencer seq;
            seq.set_active(1, true);
            Model model;
            const Fate fate = run_against_model(seq, model, 1, 500, [](uint32_t turn) { std::vector<Command> o; if (turn >= 400) for (int i = 0; i < 64; ++i) o.push_back(cmd(CommandType::Hatch, 1)); return o; });     // (20 s of rest first: the store does not grow past its 512)
            ASSERT_EQ(fate.disagreed, 0u);
            ASSERT_TRUE(fate.taken >= 551 && fate.taken <= 552);                                  // 512 and the 99 turns that seal before the last of them, 8 a second
            ASSERT_EQ(seq.heavy_seats(), 1u << 1);
            // ... and while it is over, a group order of the seat is taken (the stores are apart)
            ASSERT_TRUE(seq.submit_checked(1, group_move(1, 1)) == Submit::Queued);
        }
        {   // Quit said again and again: the first is free and the others are charged like any control command (513 in all fit in the turn and the carry)
            Sequencer seq;
            seq.set_active(1, true);
            uint32_t quits = 0;
            for (int i = 0; i < 1000; ++i) quits += seq.submit_checked(1, cmd(CommandType::Quit, 1)) == Submit::Queued ? 1u : 0u;
            ASSERT_EQ(quits, 513u);
        }
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

    TEST_CASE("S2.63 Dedicated Server: A Seat That Sends Orders As Fast As It Can Gets 6,144 And 12 A Second Sealed, Is Marked, Is Not Dropped And Is Not A Violation; The Others' Orders Are All Run, And Every Machine Runs The Same Turns") {
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
        m.run(60000, false, [&](uint32_t) {
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
        ASSERT_TRUE(sent_by_1 >= 17900);
        ASSERT_EQ(m.host->orders_over_budget(1) + run_by_1, sent_by_1);            // every order was run or dropped for the budget: none was lost anywhere else
        ASSERT_TRUE(run_by_1 >= 6144 && run_by_1 <= 6144u + 12u * 61u);            // 6,144 at once and 12 a second for the 60 s of the flood of orders
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

    TEST_CASE("S2.66 Dedicated Server, Five Minutes Of Big-Army Play: An Army Of 200 Ants At 4 Clicks A Second (7 Commands A Click) And A Fight Of 100 Ants In Bursts Of 12 Clicks A Second Lose Not One Order And Are Not Marked, While A Seat That Floods Beside Them Is Dropped And Marked, Can Still Quit, And Every Machine Runs The Same Turns") {
        const uint32_t kSeconds = 300;
        const uint32_t kFloodSeconds = 150;
        ServerMatch m(7, 3, {30, 5}, {}, {}, false);
        std::array<uint32_t, 3> run{};
        uint32_t quits_by_2 = 0;
        m.host->runner().set_on_command([&](const sim::Command& c, const sim::CommandResult&) {
            if (c.issuer < 3 && c.type != CommandType::Quit) ++run[c.issuer];
            if (c.issuer == 2 && c.type == CommandType::Quit) ++quits_by_2;
        });
        m.run(8000, false);
        std::array<uint32_t, 3> sent{};
        uint32_t step = 0;
        const auto click = [&](uint8_t seat, uint32_t commands, int16_t x) {
            for (uint32_t i = 0; i < commands; ++i) m.clients[seat]->submit(cmd(CommandType::GroupMove, seat, 255, x, static_cast<int16_t>(step % 50), m.ids.ants[seat]));
            sent[seat] += commands;
        };
        const auto due = [](uint32_t per_hundred_steps, uint32_t at) { return (per_hundred_steps * (at + 1)) / 100 - (per_hundred_steps * at) / 100; };      // the clicks of one 10 ms step at a pace of so many a second
        m.run(kSeconds * 1000, false, [&](uint32_t) {
            const uint32_t second = step / 100;
            for (uint32_t k = 0; k < due(4, step); ++k) click(0, 7, static_cast<int16_t>((step / 25) % 60));                                      // seat 0: an army of 200 ants (7 commands a click), 4 clicks a second
            for (uint32_t k = 0; k < due(second % 60 < 20 ? 12u : 2u, step); ++k) click(1, 4, static_cast<int16_t>((step / 8) % 60));         // seat 1: a fight of 100 ants (4 commands a click), 20 s of every minute at 12 clicks a second, else 2
            if (second < kFloodSeconds) {                                                                                                       // seat 2: 300 a second, and then it quits
                for (int i = 0; i < 3; ++i) click(2, 1, static_cast<int16_t>(step % 60));
            } else if (second == kFloodSeconds && step % 100 == 0) {
                m.clients[2]->submit(cmd(CommandType::Quit, 2));
            }
            ++step;
        });
        m.run(5000, false);                                                            // (what the last clicks sent is run before the counts are read)
        m.settle();
        for (uint8_t seat = 0; seat < 2; ++seat) {
            ASSERT_EQ(run[seat], sent[seat]);                                          // every order of the two that play honestly was run
            ASSERT_EQ(m.host->orders_over_budget(seat), 0u);
            ASSERT_EQ(m.host->orders_without_ants(seat), 0u);
        }
        ASSERT_TRUE(sent[0] == 7u * 4u * kSeconds && sent[1] > 4u * 2u * kSeconds);
        ASSERT_EQ(m.host->heavy_order_seats(), 1u << 2);                               // only the seat that floods is marked
        ASSERT_TRUE(m.host->orders_over_budget(2) > 30000 && run[2] < sent[2] / 4);
        ASSERT_EQ(run[2] + m.host->orders_over_budget(2), sent[2]);                    // (every order of the flood was run or dropped for the budget: none was lost elsewhere)
        ASSERT_EQ(quits_by_2, 1u);                                                     // its Quit was sealed and run although it was over its budget
        ASSERT_TRUE(m.referee.is_player_dropped(2));
        ASSERT_EQ(m.host->violations(2), 0u);
        ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
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

    TEST_CASE("S2.67 Sequencer, The Vetter: An Order That Names No Ant That The Seat Can Order Is Not Sealed (NoAnts) And Costs No Budget; The Ants It Keeps Are The Only Ones In The Turn; A Seat That Sends Such Orders Is Not Marked For Its Rate") {
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
        // 1,000 orders of nothing do not use the budget: what follows is what a seat that had sent none has (the model's decisions), and the seat is not marked for them
        Model model;
        model.takes(cmd(CommandType::GroupMove, 1));                                            // (the one that was taken above)
        model.takes(cmd(CommandType::Hatch, 1));
        model.seal();
        for (int i = 0; i < 1000; ++i) ASSERT_TRUE(seq.submit_checked(1, cmd(CommandType::GroupMove, 1, 255, 3, 4, {150})) == Submit::NoAnts);
        ASSERT_EQ(seq.heavy_seats(), 0u);
        ASSERT_EQ(seq.dropped_no_ants(1), 1001u);
        const Fate fate = run_against_model(seq, model, 1, 150, [](uint32_t) { std::vector<Command> o; for (int i = 0; i < 64; ++i) o.push_back(cmd(CommandType::GroupMove, 1, 255, 1, 1, {1})); return o; });
        ASSERT_EQ(fate.disagreed, 0u);
        ASSERT_TRUE(fate.taken >= 6144u - 1u && fate.dropped > 0);                              // (the store had been used for one order only)
        ASSERT_EQ(seq.heavy_seats(), 1u << 1);                                                  // (marked now, for the commands that were over the budget and only for those)
        ASSERT_EQ(seq.dropped_over_budget(1), fate.dropped);
    } TEST_END();

    // ---- the tick of an order (protocol 30, docs/NETWORK_PORT.md "The tick of an order") ------------------------------------------------------------------------------------------

    TEST_CASE("S2.68 Sequencer, The Tick Of An Order: An Order Stamped With A Turn That Is Still Open Is Sealed In Exactly That Turn; One Stamped With The Next Turn Or With 0 Is Sealed In The Next; One That Is Late (A Turn That Is Sealed Already) Goes Into The First Open Turn, Never Into The Past; A Seat's Orders Keep The Order They Were Sent In, So An Order Never Overtakes An Earlier One; Seats Do Not Wait For Each Other") {
        Sequencer seq;
        for (uint8_t seat = 0; seat < 3; ++seat) seq.set_active(seat, true);
        for (int i = 0; i < 10; ++i) seq.seal();                                                 // the first open turn is 10
        ASSERT_EQ(seq.next_turn(), 10u);
        // seat 2: a late stamp (turn 3 is sealed since long), no preference, the next turn, and one turn beyond
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 1), 3) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 2), 0) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 3), 10) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 4), 11) == Submit::Queued);
        ASSERT_EQ(seq.held_ahead(2), size_t{1});                                                 // (only the last one waits for a turn that is not the next)
        // seat 1: an order for turn 13, and three that were sent after it with earlier stamps (they are sealed with it, behind it, in the order sent)
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 1), 13) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 2), 10) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 3), 0) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 4), 3) == Submit::Queued);
        ASSERT_EQ(seq.held_ahead(1), size_t{4});
        ASSERT_TRUE(seq.submit_checked(0, group_move(0, 5), 12) == Submit::Queued);              // seat 0's order does not wait for seat 1's
        std::vector<std::pair<uint32_t, int16_t>> by_seat[3];                                    // (turn, tile) of each seat's orders
        for (uint32_t t = 10; t < 16; ++t) {
            const TurnMsg turn = seq.seal();
            ASSERT_EQ(turn.turn, t);
            for (const Command& c : turn.commands) by_seat[c.issuer].emplace_back(t, c.tile_x);
        }
        ASSERT_EQ(by_seat[2].size(), size_t{4});
        ASSERT_TRUE(by_seat[2][0] == std::make_pair(10u, int16_t{1}) && by_seat[2][1] == std::make_pair(10u, int16_t{2}) && by_seat[2][2] == std::make_pair(10u, int16_t{3}) && by_seat[2][3] == std::make_pair(11u, int16_t{4}));
        ASSERT_EQ(by_seat[1].size(), size_t{4});
        for (size_t i = 0; i < 4; ++i) ASSERT_TRUE(by_seat[1][i] == std::make_pair(13u, static_cast<int16_t>(i + 1)));
        ASSERT_TRUE(by_seat[0].size() == 1 && by_seat[0][0] == std::make_pair(12u, int16_t{5}));
        ASSERT_EQ(seq.held_ahead(1) + seq.held_ahead(2) + seq.held_ahead(0), size_t{0});
        ASSERT_EQ(seq.order_ahead_clamped(1) + seq.order_ahead_clamped(2), 0u);
    } TEST_END();

    TEST_CASE("S2.69 Sequencer, The Rule Of The Turn, Written Down Again And Held To Random Play: Every Order Of Two Seats Is Sealed In The Turn max(its stamp, the first open turn when it came, the turn of the seat's last order), At Most 16 Beyond The First Open Turn, And In No Other; Nothing Is Left Waiting Afterwards") {
        Sequencer seq;
        seq.set_active(1, true);
        seq.set_active(2, true);
        std::map<int16_t, uint32_t> expected;                                                    // tile -> the turn the rule says
        uint32_t last[3] = {0, 0, 0};
        Lcg rng(69);
        int16_t tile = 0;
        std::map<int16_t, uint32_t> sealed;
        const auto seal_one = [&]() {
            const TurnMsg turn = seq.seal();
            for (const Command& c : turn.commands) sealed[c.tile_x] = turn.turn;
        };
        for (uint32_t t = 0; t < 60; ++t) {
            for (int i = 0; i < 3; ++i) {
                const uint8_t seat = static_cast<uint8_t>(1 + rng.below(2));
                const uint32_t stamp = rng.below(4) == 0 ? rng.below(t + 1) : seq.next_turn() + rng.below(24);      // (now and then a late one, mostly ahead, some beyond the cap)
                uint32_t turn = std::max(std::max(stamp, seq.next_turn()), last[seat]);
                turn = std::min(turn, seq.next_turn() + 16u);
                last[seat] = turn;
                expected[tile] = turn;
                ASSERT_TRUE(seq.submit_checked(seat, group_move(seat, tile), stamp) == Submit::Queued);
                ++tile;
            }
            seal_one();
        }
        for (int i = 0; i < 20; ++i) seal_one();
        ASSERT_EQ(sealed.size(), expected.size());
        for (const auto& e : expected) {
            ASSERT_TRUE(sealed.count(e.first) == 1);
            ASSERT_EQ(sealed[e.first], e.second);
        }
        ASSERT_EQ(seq.held_ahead(1) + seq.held_ahead(2), size_t{0});
        ASSERT_TRUE(seq.order_ahead_clamped(1) + seq.order_ahead_clamped(2) > 0u);               // (the cut was met: the test covered it)
    } TEST_END();

    TEST_CASE("S2.70 Sequencer, The Cap: An Order Is Held At Most Config::max_order_ahead_turns (16) Beyond The First Open Turn, A Stamp Beyond It Is Cut To It And Counted, Never An Offence; A Stamp Exactly At The Cap Is Not; An Honest Client's Stamp (Its Lead, 12 Ticks At The Most, Beyond The Turns It Holds) Never Touches It; The Cap Follows The Turns That Are Sealed") {
        Sequencer::Config cfg;
        ASSERT_EQ(cfg.max_order_ahead_turns, 16u);
        Sequencer seq(cfg);
        seq.set_active(1, true);
        for (int i = 0; i < 100; ++i) seq.seal();
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 1), 100 + 16) == Submit::Queued);        // at the cap: held, not cut
        ASSERT_EQ(seq.order_ahead_clamped(1), 0u);
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 2), 100 + 17) == Submit::Queued);        // one beyond: cut to the cap
        ASSERT_EQ(seq.order_ahead_clamped(1), 1u);
        ASSERT_TRUE(seq.submit_checked(1, group_move(1, 3), 0xFFFFFFFFu) == Submit::Queued);     // the largest number that a message can carry: cut to the cap, no overflow
        ASSERT_EQ(seq.order_ahead_clamped(1), 2u);
        ASSERT_EQ(seq.held_ahead(1), size_t{3});
        std::vector<uint32_t> turns_with_orders;
        for (uint32_t t = 100; t < 130; ++t) {
            if (!seq.seal().commands.empty()) turns_with_orders.push_back(t);
        }
        ASSERT_EQ(turns_with_orders.size(), size_t{1});
        ASSERT_EQ(turns_with_orders[0], 116u);                                                   // all three are in the turn at the cap, and in none beyond it
        // an honest stamp: the turns that the client holds are sealed already (they are before the first open turn), and its lead is at most 12 ticks beyond the confirmed tick
        for (uint32_t held = 0; held <= 12; ++held) {
            Sequencer honest(cfg);
            honest.set_active(1, true);
            for (int i = 0; i < 50; ++i) honest.seal();
            const uint32_t confirmed = 50 - held;                                                // the client has run `held` turns fewer than the host has sealed
            ASSERT_TRUE(honest.submit_checked(1, group_move(1, 1), confirmed + 12) == Submit::Queued);
            ASSERT_EQ(honest.order_ahead_clamped(1), 0u);
        }
        // the cap is counted from the first open turn: as the host seals, the same stamp is not cut any more
        Sequencer later(cfg);
        later.set_active(1, true);
        ASSERT_TRUE(later.submit_checked(1, group_move(1, 1), 40) == Submit::Queued);
        ASSERT_EQ(later.order_ahead_clamped(1), 1u);                                             // (turn 0 is open: 40 is 24 beyond)
        for (int i = 0; i < 30; ++i) later.seal();
        ASSERT_TRUE(later.submit_checked(1, group_move(1, 2), 40) == Submit::Queued);            // (turn 30 is open now: 40 is 10 beyond)
        ASSERT_EQ(later.order_ahead_clamped(1), 1u);
    } TEST_END();

    TEST_CASE("S2.71 Sequencer, What Is Held Ahead Is Bounded And Belongs To The Seat: It Counts With The Carried Commands Against max_carried_commands (A Flood Of Stamped Orders Is Dropped, Not Piled Up), Each Turn Takes At Most max_commands_per_turn Of A Seat And The Rest Waits For The Next, The Budget Is Charged When The Order Arrives, A Seat That Leaves Takes Its Held Orders With It, And A New Host Starts With None") {
        Sequencer::Config cfg;
        cfg.max_carried_commands = 20;
        cfg.max_commands_per_turn = 8;
        cfg.order_burst = 100;
        cfg.orders_per_second = 12;
        Sequencer seq(cfg);
        seq.set_active(1, true);
        seq.set_active(2, true);
        int taken = 0;
        int over = 0;
        for (int i = 0; i < 40; ++i) {                                                           // 40 orders for the turn 5 ahead, from a seat whose turn takes 8 and whose bound is 20
            const Submit s = seq.submit_checked(1, group_move(1, static_cast<int16_t>(i)), 5);
            if (s == Submit::Queued) ++taken;
            else if (s == Submit::OverBudget) ++over;
        }
        ASSERT_EQ(taken, 20);                                                                    // (20 are held)
        ASSERT_EQ(over, 20);                                                                     // (the rest is dropped, and counted: a flood is not an offence)
        ASSERT_EQ(seq.held_ahead(1), size_t{20});
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 1), 5) == Submit::Queued);              // another seat has its own bound
        std::vector<size_t> per_turn;
        for (uint32_t t = 0; t < 12; ++t) {
            size_t n = 0;
            for (const Command& c : seq.seal().commands) n += c.issuer == 1 ? 1u : 0u;
            per_turn.push_back(n);
        }
        ASSERT_TRUE(per_turn[5] == 8 && per_turn[6] == 8 && per_turn[7] == 4);                   // turn 5 takes 8 of the seat's 20, the next ones the rest, in the order sent
        for (size_t t = 0; t < 12; ++t) {
            if (t < 5 || t > 7) ASSERT_EQ(per_turn[t], size_t{0});
        }
        ASSERT_EQ(seq.held_ahead(1), size_t{0});
        // a seat that leaves takes what it held with it
        for (int i = 0; i < 5; ++i) ASSERT_TRUE(seq.submit_checked(1, group_move(1, static_cast<int16_t>(i)), 20) == Submit::Queued);
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 1), 20) == Submit::Queued);
        seq.set_active(1, false);
        ASSERT_EQ(seq.held_ahead(1), size_t{0});
        size_t seat1 = 0;
        size_t seat2 = 0;
        for (uint32_t t = 12; t < 30; ++t) {
            for (const Command& c : seq.seal().commands) (c.issuer == 1 ? seat1 : seat2) += 1u;
        }
        ASSERT_EQ(seat1, size_t{0});
        ASSERT_EQ(seat2, size_t{1});
        // a new host (host migration) starts with nothing held
        ASSERT_TRUE(seq.submit_checked(2, group_move(2, 2), 40) == Submit::Queued);
        seq.resume(100);
        seq.set_active(2, true);
        ASSERT_EQ(seq.held_ahead(2), size_t{0});
        for (uint32_t t = 100; t < 130; ++t) ASSERT_TRUE(seq.seal().commands.empty());
    } TEST_END();

    TEST_CASE("S2.72 Host Session, The Tick Of An Order On The Wire: A Client's Order That Is Stamped With A Turn That Is Open Is Sealed In That Turn And Run By Every Machine At That Tick, A Late One In The First Open Turn, One Far Ahead At The Cap; The Host Answers Every Order That Carries A Number With The First Turn That Was Open When It Arrived, And Answers None That Carries No Number; Every Machine Runs The Same Turns") {
        ServerMatch m(7, 3, {30, 0});
        std::vector<OrderAckMsg> acks;
        m.clients[1]->set_on_order_ack([&](const OrderAckMsg& a) { acks.push_back(a); });
        m.run(6000, false);                                                                      // (the match plays: the first turns are sealed)
        const uint32_t base = m.host->turns_sealed();
        ASSERT_TRUE(base > 20);
        const uint32_t ahead = base + 12;                                                        // (far enough ahead to be open when the order arrives, a link's few turns later)
        // sent in this order (a seat's orders keep it): a late one, one that asks for a turn that is open, one far beyond the cap, and one that asks for nothing
        ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 12, 4, {m.ids.ants[1][1]}), 2, 1));
        ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 11, 4, {m.ids.ants[1][0]}), ahead, 2));
        ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 13, 4, {m.ids.ants[1][2]}), base + 5000, 3));
        ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 14, 4, {m.ids.ants[1][3]}), 0, 0));
        m.run(3000, false);
        m.settle();
        ASSERT_EQ(acks.size(), size_t{3});                                                       // (the order without a number was not answered)
        ASSERT_TRUE(acks[0].seq == 1 && acks[1].seq == 2 && acks[2].seq == 3);
        const uint32_t open = acks[0].open_turn;                                                 // the first turn that was open as the orders arrived (together: one message after the other)
        ASSERT_TRUE(acks[1].open_turn == open && acks[2].open_turn == open);
        ASSERT_TRUE(open >= base && open < ahead);                                               // (the second order's stamp was still open)
        std::map<int16_t, uint32_t> sealed_at;
        for (uint32_t t = 0; t < m.host->turns_sealed(); ++t) {
            const TurnMsg* turn = m.host->runner().logged_turn(t);
            if (turn == nullptr) continue;
            for (const Command& c : turn->commands) {
                if (c.issuer == 1 && c.type == CommandType::GroupMove && c.tile_y == 4) sealed_at[c.tile_x] = t;
            }
        }
        ASSERT_EQ(sealed_at.size(), size_t{4});
        ASSERT_EQ(sealed_at[12], open);                                                          // late: the first open turn, not the past
        ASSERT_EQ(sealed_at[11], ahead);                                                         // honored: exactly the stamp
        ASSERT_EQ(sealed_at[13], open + 16u);                                                    // far ahead: at the cap from the first open turn
        ASSERT_EQ(sealed_at[14], open + 16u);                                                    // (the order that asked for nothing comes after the one that was held: a seat's orders keep their order)
        ASSERT_EQ(m.host->violations(1), 0u);
        ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());                                 // every machine ran the same turns
        // every machine ran the order of the stamp at that tick
        for (size_t seat = 0; seat < m.clients.size(); ++seat) {
            const TurnMsg* turn = m.clients[seat]->runner().logged_turn(ahead);
            ASSERT_TRUE(turn != nullptr);
            bool found = false;
            for (const Command& c : turn->commands) found = found || (c.issuer == 1 && c.tile_x == 11);
            ASSERT_TRUE(found);
        }
    } TEST_END();

    TEST_CASE("S2.73 Host Session, A Seat's Orders That Wait For A Later Turn Do Not Make Another Seat's Wait, Orders Over The Budget Or Naming No Ant Are Answered With The Open Turn Too (The Lag Is What The Sender Learns) And Reach No Turn, And A Client That Sends Stamps Beyond The Cap Over And Over Is Neither Dropped Nor Charged With An Offence") {
        ServerMatch m(8, 3, {20, 0});
        std::vector<OrderAckMsg> acks;
        m.clients[1]->set_on_order_ack([&](const OrderAckMsg& a) { acks.push_back(a); });
        m.run(6000, false);
        uint32_t seq = 0;
        for (int i = 0; i < 40; ++i) ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 20, static_cast<int16_t>(i % 50), {m.ids.ants[1][0]}), 0xFFFFFFFFu, ++seq));      // far beyond the cap, each time
        ASSERT_TRUE(m.clients[1]->submit(cmd(CommandType::GroupMove, 1, 255, 30, 30, {999999}), 0, ++seq));        // an ant that does not exist: no order, but an answer
        ASSERT_TRUE(m.clients[2]->submit(cmd(CommandType::GroupMove, 2, 255, 40, 5, {m.ids.ants[2][0]}), 0, 1));   // another seat: no wait for the first seat's orders
        m.run(3000, false);
        m.settle();
        ASSERT_EQ(acks.size(), size_t{41});
        for (size_t i = 1; i < acks.size(); ++i) ASSERT_TRUE(acks[i].open_turn >= acks[i - 1].open_turn);          // (the answers come in the order that the orders were sent, and the turns only go on)
        uint32_t seat2_at = 0;
        uint32_t seat1_last = 0;
        for (uint32_t t = 0; t < m.host->turns_sealed(); ++t) {
            const TurnMsg* turn = m.host->runner().logged_turn(t);
            if (turn == nullptr) continue;
            for (const Command& c : turn->commands) {
                if (c.issuer == 2 && c.tile_x == 40) seat2_at = t;
                if (c.issuer == 1 && c.tile_x == 20) seat1_last = t;
                ASSERT_FALSE(c.issuer == 1 && c.tile_x == 30);                                                    // (the order that names no ant of the seat is in no turn)
            }
        }
        ASSERT_TRUE(seat2_at != 0 && seat1_last != 0 && seat2_at < seat1_last);                                   // seat 2's order did not wait for the 16 turns that seat 1's were held
        ASSERT_TRUE(m.host->client_present(1));
        ASSERT_EQ(m.host->violations(1), 0u);
        ASSERT_TRUE(m.host->desyncs().empty() && m.all_equal());
    } TEST_END();
}
