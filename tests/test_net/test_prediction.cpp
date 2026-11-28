// Tests of the client-side prediction of one's own orders (include/ants_net/prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders").
//
// The rig plays the SERVER: the test seals the turns and chooses when each one reaches the client and in which turn an order of the player is sealed, so that every timing that matters
// is a number in the test. The client is the real thing: a confirmed engine, a LockstepRunner with a fixed jitter buffer (one turn is one tick, one step of the rig is 50 ms), the
// prediction, and, next to them, an ORACLE: a second engine that executes every sealed turn the moment it is sealed, so that it always knows the truth that the client will reach later.
//
// What is checked:
//   - exactness: with the right lead and no other player's commands the predicted engine equals the oracle at every frame and is never rebuilt; with a jitter buffer deeper than the lead
//     it equals the oracle whatever the other players do (the turns in hand are run as certain input, there is no guess);
//   - the corrections: another player's command, an order of one's own sealed one tick later or earlier than it was applied, an order the server never sealed: how many rebuilds each
//     costs, and that the prediction is right again after them;
//   - the property that makes it safe: the predicted engine that was advanced tick by tick equals the engine that a rebuild would make now (derived_hash), at every frame, under random
//     jitter, stalls, bursts, commands of every kind and a lead that changes;
//   - the prediction never touches the confirmed engine, the turns or the hashes.
#include "prediction_rig.hpp"

namespace {

// ---------------------------------------------------------------------------------------------------------------------------------

void run_basic_tests() {
    TEST_CASE("RP1.1 Which Commands Are Predicted: Group Moves, Special Orders, Attacks And Stop; Not Hatch, The Alliance Commands, Quit Or Drop") {
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupMove));
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupSpecial));
        ASSERT_TRUE(Prediction::predicts(CommandType::GroupAttack));
        ASSERT_TRUE(Prediction::predicts(CommandType::Stop));
        for (CommandType t : {CommandType::None, CommandType::Hatch, CommandType::AllianceInvite, CommandType::AllianceAccept, CommandType::AllianceDeny, CommandType::AllianceWithdraw,
                              CommandType::AllianceBreak, CommandType::Quit, CommandType::Drop}) {
            ASSERT_FALSE(Prediction::predicts(t));
        }
    } TEST_END();

    TEST_CASE("RP1.4 Only The Player's Own Orders Are Predicted: A Command Of Another Seat Is Left To The Caller (Nothing Applied, The Answer Untouched), Even Of A Kind That Is Predicted; An Own Order Is Applied To The Predicted Engine And Never To The Confirmed One") {
        Scenario sc;
        Rig rig(sc);
        rig.run(10);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active());
        const uint32_t ant = rig.ids.ants[1][0];                                            // (seat 1's ant: this player is seat 0)
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::RejectedMalformed;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 1, 255, 20, 40, {ant}), r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::RejectedMalformed);
        ASSERT_EQ(p.stats().commands_predicted, 0u);
        ASSERT_TRUE(p.engine().get_unit(ant).orig_order != sim::AntUnit::kOrderMove);       // (the other seat's ant is not ordered by this player's prediction)
        const uint32_t own = rig.ids.ants[0][0];
        ASSERT_TRUE(p.submit(make_command(CommandType::GroupMove, 0, 255, 20, 40, {own}), r));
        ASSERT_EQ(p.stats().commands_predicted, 1u);
        ASSERT_EQ(p.engine().get_unit(own).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_TRUE(rig.confirmed().get_unit(own).orig_order != sim::AntUnit::kOrderMove);   // (the confirmed engine waits for the turn)
    } TEST_END();

    TEST_CASE("RP1.2 Nothing Is Predicted Before The First Tick Has Run (the \"Get ready\" dialog), And Every Order Is Left To The Caller") {
        Scenario sc;
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        ASSERT_FALSE(p.active());
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::RejectedMalformed;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 0, 255, 10, 10, {rig.ids.ants[0][0]}), r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::RejectedMalformed);          // untouched
        rig.run(1);                                                                         // turn 0 sealed and in flight: the runner has no turn yet
        ASSERT_FALSE(p.active());
        ASSERT_EQ(rig.confirmed().current_tick(), 0u);
        rig.run(4);                                                                         // the buffer is full, the first tick has run
        ASSERT_TRUE(rig.confirmed().current_tick() >= 1u);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.stats().starts, 1u);
        ASSERT_TRUE(p.display_tick() > rig.confirmed().current_tick());
    } TEST_END();

    TEST_CASE("RP1.3 The Display Tick Is The Confirmed Tick Plus The Lead, Tick By Tick, And The Lead Follows The Delay: It Falls One Tick At A Time (The First At Once, The Next Ones Four Ticks Apart: The Picture Stands Still For One Tick Each Time, Never Longer), Rises At Once By Two Ticks And More, By One Tick Only After 40 Ticks, And Is Bounded") {
        Scenario sc;
        sc.lead_ms = 150;                                                                   // 3 ticks
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        rig.run(20);
        ASSERT_EQ(p.lead_ticks(), 3u);
        for (int i = 0; i < 30; ++i) {
            rig.step();
            ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + p.lead_ticks());
        }
        p.set_expected_delay_ms(300);                                                       // slower by three ticks: the lead rises at the next tick
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 6u);
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 6);
        p.set_expected_delay_ms(350);                                                       // slower by one tick: nothing changes for 40 ticks (the lag of an order goes up and down by one with the phase of the host's seals), then it rises
        for (int i = 0; i < 39; ++i) rig.step();
        ASSERT_EQ(p.lead_ticks(), 6u);
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 6);
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 7u);
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 7);
        // faster by three ticks: the lead falls one tick at the next tick (the engine is held for that tick: the confirmed tick moves on, the display tick does not), and one more every fourth
        // tick after it; the display tick never stands for two ticks in a row
        p.set_expected_delay_ms(200);
        uint64_t shown = p.display_tick();
        uint32_t held_in_a_row = 0;
        uint32_t longest_hold = 0;
        std::vector<uint32_t> leads;
        for (int i = 0; i < 12; ++i) {
            rig.step();
            held_in_a_row = p.display_tick() == shown ? held_in_a_row + 1 : 0;
            longest_hold = std::max(longest_hold, held_in_a_row);
            shown = p.display_tick();
            leads.push_back(p.lead_ticks());
            ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + p.lead_ticks());
        }
        ASSERT_TRUE(leads == (std::vector<uint32_t>{6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4}));
        ASSERT_EQ(longest_hold, 1u);
        p.set_expected_delay_ms(60000);
        rig.step();
        ASSERT_EQ(p.lead_ticks(), 12u);                                                     // bounded above
        p.set_expected_delay_ms(0);
        for (int i = 0; i < 11 * 4 + 5; ++i) rig.step();
        ASSERT_EQ(p.lead_ticks(), 1u);                                                      // and below
        ASSERT_EQ(p.display_tick(), rig.confirmed().current_tick() + 1);
    } TEST_END();

    TEST_CASE("RP1.4 submit(): The Engine's Own Verdict Comes Back (the ant that answers, the ants that needed the order), A Foreign Seat Or An Unpredicted Kind Is Refused, The Order Is Applied At The Display Tick") {
        Scenario sc;
        Rig rig(sc);
        rig.run(30);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active());
        const uint32_t ant = rig.ids.ants[0][0];
        sim::CommandResult r;
        const Command move = make_command(CommandType::GroupMove, 0, 255, 40, 40, {ant});
        ASSERT_TRUE(p.submit(move, r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, ant);
        ASSERT_EQ(r.needing_order, 1u);
        ASSERT_EQ(p.pending_orders(), 1u);
        ASSERT_TRUE(p.applied_at_display().size() == 1 && p.applied_at_display()[0] == move);
        ASSERT_EQ(p.stats().commands_predicted, 1u);
        sim::CommandResult untouched;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 1, 255, 5, 5, {rig.ids.ants[1][0]}), untouched));      // another seat's: not its to predict
        ASSERT_FALSE(p.submit(make_command(CommandType::Hatch, 0), untouched));
        ASSERT_FALSE(p.submit(make_command(CommandType::AllianceInvite, 0, 1), untouched));
        ASSERT_FALSE(p.submit(make_command(CommandType::Quit, 0), untouched));
        ASSERT_EQ(p.pending_orders(), 1u);
        // an order that the engine refuses (a stranger's ant) is answered as the engine answers it, and still tracked (the server will seal it and the engine will refuse it there too)
        const Command stranger = make_command(CommandType::GroupMove, 0, 255, 8, 8, {rig.ids.ants[2][0]});
        ASSERT_TRUE(p.submit(stranger, r));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Ignored);
        ASSERT_EQ(r.ack_ant, 0u);
        ASSERT_EQ(p.pending_orders(), 2u);
    } TEST_END();
}

void run_exactness_tests() {
    TEST_CASE("RP2.1 Exact Timing, No Other Player: The Predicted Engine Equals The Oracle At Every Frame, Is Never Rebuilt, And The Order Stands At The Tick The Turn Gives It (3 buffers, 3 lags, 2 uplinks each, 300 steps)") {
        for (uint32_t buffer : {1u, 2u, 3u}) {
            for (uint32_t down : {0u, 1u, 2u}) {
                for (uint32_t up : {static_cast<uint32_t>(down), static_cast<uint32_t>((down + 2) % 3)}) {          // (six combinations of lag and uplink)
                    Scenario sc;
                    sc.seed = 10 + buffer * 7 + down * 3 + up;
                    sc.buffer_turns = buffer;
                    sc.down_delay = down;
                    sc.uplink = up;
                    Rig rig(sc);
                    Lcg rng(sc.seed);
                    rig.run(buffer + down + 8);
                    for (uint32_t s = 0; s < 180; ++s) {
                        if (s % 7 == 0) rig.issue(random_order(rng, rig.ids, 0));
                        rig.step();
                    }
                    rig.run_quiet(40);
                    ASSERT_EQ(rig.prediction()->stats().rebuilds, 0u);
                    ASSERT_EQ(rig.prediction()->stats().commands_predicted, rig.own_orders);
                    ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
                    ASSERT_OK(verify_min(rig, 0, 140));
                }
            }
        }
    } TEST_END();

    TEST_CASE("RP2.2 A Jitter Buffer Deeper Than The Lead Is Run As Certain Input: Whatever The Other Players Do (hatching and alliances too) The Predicted Engine Is The Oracle, With No Rebuild And No Guess") {
        for (uint32_t seed : {3u, 4u}) {
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 4;
            sc.down_delay = 1;
            sc.lead_ms = 50;                                                                 // one tick: the display tick stays inside the turns that are in hand
            Rig rig(sc);
            Lcg rng(seed * 31u);
            rig.run(12);
            for (uint32_t s = 0; s < 300; ++s) {
                if (s % 3 == 0) {
                    const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                    rig.schedule(rig.step_no() + 2, rng.below(3) == 0 ? random_unpredicted(rng, seat) : random_order(rng, rig.ids, seat));
                }
                if (s % 11 == 0) rig.schedule(rig.step_no() + 3, random_unpredicted(rng, 0));      // (the player's own hatch and alliance commands: not predicted)
                rig.step();
            }
            rig.run_quiet(30);
            ASSERT_EQ(rig.prediction()->stats().rebuilds, 0u);
            ASSERT_TRUE(rig.prediction()->stats().turns_checked > 250);
            ASSERT_OK(verify_min(rig, 0, 250));
        }
    } TEST_END();

    TEST_CASE("RP2.5 Fog Of War: The Explored Ground That The Predicted Engine Shows Is The Confirmed Engine's At Every Frame And Never More Than The Oracle's At The Tick That Is Shown (The Hash Does Not Hold It), With A Buffer Deeper Than The Lead Whatever The Other Seats Order And With The Right Lead For The Player's Own Orders: The Prediction Explores Nothing That The Engine Would Not At That Tick") {
        for (const int kind : {0, 1, 2}) {                                                   // 0: a buffer deeper than the lead; 1: the right lead, own orders; 2: other seats' orders that the prediction does not know, so that it is rebuilt again and again
            const bool deep = kind == 0;
            Scenario sc;
            sc.seed = 51u + static_cast<uint32_t>(kind);
            sc.fog = true;
            if (deep) {
                sc.buffer_turns = 4;
                sc.down_delay = 1;
                sc.lead_ms = 50;
            }
            Rig rig(sc);
            Lcg rng(sc.seed);
            rig.run(12);
            for (uint32_t s = 0; s < 260; ++s) {
                if (deep) {
                    if (s % 5 == 0) rig.schedule(rig.step_no() + 2, random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                    if (s % 9 == 0) rig.schedule(rig.step_no() + 2, random_order(rng, rig.ids, 0));
                } else if (s % 7 == 0) {
                    rig.issue(random_order(rng, rig.ids, 0));
                }
                if (kind == 2 && s % 6 == 3) rig.schedule(rig.step_no() + 2, random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                rig.step();
            }
            rig.run_quiet(30);
            ASSERT_TRUE(rig.prediction()->stats().turns_checked > 200);
            if (kind == 2) {
                ASSERT_TRUE(rig.prediction()->stats().rebuilds > 20u);
                size_t views = 0;
                const std::string why = rig.verify_views(0, &views, false);                      // (the pictures differ from the oracle's here; the explored ground must not)
                ASSERT_TRUE(why.empty() && views > 150);
            } else {
                ASSERT_OK(verify_min(rig, 0, 200));
            }
            size_t explored = 0;
            for (uint8_t cell : rig.confirmed().get_world_state().fog_revealed) explored += cell != 0 ? 1u : 0u;
            ASSERT_TRUE(explored > 200);                                                      // (the player's ants walked and revealed ground: the comparison is not about an empty map)
        }
    } TEST_END();

    TEST_CASE("RP2.3 The Orders Of A Match Of Four Seats (every seat gives orders, the lead is exact for the player's own): the Player's Orders Never Make A Correction By Themselves, Another Seat's Do, One Rebuild For Each Turn That Has A Command") {
        Scenario sc;
        sc.seed = 21;
        sc.buffer_turns = 1;
        sc.down_delay = 1;
        sc.uplink = 1;
        Rig rig(sc);
        Lcg rng(21);
        rig.run(12);
        std::set<uint32_t> foreign_turns;
        for (uint32_t s = 0; s < 300; ++s) {
            if (s % 9 == 0) rig.issue(random_order(rng, rig.ids, 0));
            if (s % 13 == 4) {
                const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                const uint32_t at = rig.step_no() + 2;
                rig.schedule(at, random_order(rng, rig.ids, seat));
                foreign_turns.insert(at);
            }
            rig.step();
        }
        rig.run_quiet(40);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_TRUE(st.rebuilds_foreign >= 1);
        ASSERT_EQ(st.rebuilds_own_timing, 0u);
        ASSERT_TRUE(st.rebuilds <= foreign_turns.size());                                    // at most one rebuild for each turn that had another player's command (turns that come together share one)
        ASSERT_EQ(st.rebuilds, st.rebuilds_foreign + st.rebuilds_other + st.rebuilds_own_timing);
    } TEST_END();

    TEST_CASE("RP2.4 A Lead Shorter Than The Delay (set too low, or a buffer that grew after a burst): An Order Is Put Behind The Turns In Hand And Chased Turn By Turn Until Its Own (one rebuild for each turn that passes it, no more), Is Never Run Twice, And The Prediction Stays Derived State At Every Frame") {
        for (uint32_t seed : {81u, 82u}) {
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 3;
            sc.down_delay = 1;
            sc.uplink = 2;
            sc.lead_ms = 50;                                                                 // one tick: shorter than the three turns in hand, and than the way of the order
            Rig rig(sc);
            Lcg rng(seed);
            rig.check_derived(1);
            rig.run(12);
            uint32_t orders = 0;
            for (uint32_t s = 0; s < 150; ++s) {
                if (s % 12 == 0) {
                    rig.issue(random_order(rng, rig.ids, 0));
                    ++orders;
                }
                rig.step();
            }
            rig.run_quiet(30);
            const Prediction::Stats& st = rig.prediction()->stats();
            ASSERT_EQ(rig.derived_mismatches, 0u);                                           // at every frame: what was advanced tick by tick is what a rebuild would make
            ASSERT_TRUE(rig.derived_checks > 150);
            ASSERT_EQ(st.commands_predicted, orders);
            ASSERT_EQ(st.commands_lost, 0u);
            // The order is put at the first tick whose turn has not arrived, which the server sealed (without it) before the order was given; the turns up to its own (uplink 2 and the lag 1
            // later) pass it one by one, each costing one rebuild that moves it a tick on: the price of a lead that is too low, bounded by the turns that pass the order.
            ASSERT_EQ(st.rebuilds, 3ull * orders);
            ASSERT_EQ(st.rebuilds_foreign, 0u);
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_OK(verify_min(rig, rig.step_no() - 20, 10));                              // and when the last of them has been sealed and run, the prediction is the oracle
        }
    } TEST_END();

    TEST_CASE("RP10.1 A Big Selection (One Click Of 100 Ants, Four Commands Given In One Step) Is Predicted Command By Command And Sealed In One Turn In The Order It Was Given: No Rebuild, Nothing Lost, Every Frame Is The Oracle's") {
        Scenario sc;
        sc.seed = 19;
        sc.extra_workers = 100;
        Rig rig(sc);
        rig.run(12);
        const std::vector<uint32_t>& all = rig.ids.ants[0];
        ASSERT_EQ(all.size(), size_t{106});
        const uint32_t given = rig.step_no();
        for (size_t from = 0; from < all.size(); from += sim::kMaxCommandAnts) {
            const size_t to = std::min(all.size(), from + sim::kMaxCommandAnts);
            rig.issue(make_command(CommandType::GroupMove, 0, 255, 44, 12, std::vector<uint32_t>(all.begin() + static_cast<std::ptrdiff_t>(from), all.begin() + static_cast<std::ptrdiff_t>(to))));
        }
        rig.run(50);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.commands_predicted, 4u);
        ASSERT_EQ(st.commands_lost, 0u);
        ASSERT_EQ(st.rebuilds, 0u);
        ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
        size_t turns_with_them = 0;
        std::vector<uint32_t> in_turn;
        for (const TurnMsg& t : rig.sealed) {
            size_t here = 0;
            for (const Command& c : t.commands) {
                if (c.issuer != 0 || c.type != CommandType::GroupMove) continue;
                ++here;
                in_turn.insert(in_turn.end(), c.ants.begin(), c.ants.end());
            }
            if (here != 0) {
                ++turns_with_them;
                ASSERT_EQ(here, size_t{4});
            }
        }
        ASSERT_EQ(turns_with_them, size_t{1});                                              // one turn carries the four commands
        ASSERT_TRUE(in_turn == all);                                                        // in the order of the selection
        ASSERT_OK(verify_min(rig, given + 10, 20));
    } TEST_END();
}

void run_correction_tests() {
    TEST_CASE("RP3.1 A Command Of Another Player Costs One Rebuild When Its Turn Arrives, The Picture Is Right From That Frame On, And The Others' Ants Are The Ones That Move In The Correction") {
        Scenario sc;
        sc.seed = 31;
        sc.lead_ms = 400;                                                                   // the display stands 8 ticks ahead: the order's ants have walked for a few ticks when its turn comes
        sc.uplink = 6;                                                                      // (and the player's own order is exact for that lead: it is no correction)
        Rig rig(sc);
        Lcg rng(31);
        rig.prediction()->set_measure_corrections(true);
        rig.run(12);
        // the player's own ants walk all the time; seat 1 gives one order, sealed at step t
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 50, 50, rig.ids.ants[0]));
        rig.run(6);
        const uint32_t t = rig.step_no() + 3;
        rig.schedule(t, make_command(CommandType::GroupMove, 1, 255, 20, 40, rig.ids.ants[1]));
        rig.run(20);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.rebuilds, 1u);
        ASSERT_EQ(st.rebuilds_foreign, 1u);
        ASSERT_EQ(st.corrections, 1u);
        ASSERT_TRUE(st.corrections_visible == 1u && st.ants_moved >= 1u);                    // the foreign ants stood elsewhere after the correction
        ASSERT_TRUE(st.max_move_px > 0 && st.max_move_px <= 64);                             // by a few ticks of walking, never more
        ASSERT_TRUE(st.move_px_total >= st.max_move_px && st.move_px_total <= st.ants_moved * st.max_move_px);      // (the sum of the moves lies between the largest one and the count times it)
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 1, 20));                                  // from the frame of the turn's arrival on, every frame is the oracle's
    } TEST_END();

    TEST_CASE("RP3.5 The Ants That A Rebuild Moves Are Told To The Picture With How Far: Only The Ants Of The Other Player's Order (Never The Player's Own, Exact Ones), By A Few Ticks Of Walking At Most, One Entry For Each Ant, What Two Rebuilds Moved It By Added Up (The Same As Taking It After Each)") {
        // the same match twice: the shifts taken after each of two rebuilds, and taken once after both
        using Sums = std::map<uint32_t, std::pair<int32_t, int32_t>>;
        const auto play = [](bool take_between, Sums& sums, size_t& entries) {
            Scenario sc;
            sc.seed = 31;
            sc.lead_ms = 400;
            sc.uplink = 6;
            Rig rig(sc);
            rig.run(12);
            rig.issue(make_command(CommandType::GroupMove, 0, 255, 50, 50, rig.ids.ants[0]));
            rig.run(6);
            const uint32_t t = rig.step_no() + 3;
            rig.schedule(t, make_command(CommandType::GroupMove, 1, 255, 20, 40, rig.ids.ants[1]));
            rig.schedule(t + 4, make_command(CommandType::GroupMove, 1, 255, 40, 20, rig.ids.ants[1]));         // (a second order for the same ants, a little later: a second rebuild)
            const auto add = [&](const std::vector<Prediction::Shift>& list) {
                entries += list.size();
                for (const Prediction::Shift& sh : list) {
                    sums[sh.ant].first += sh.dx;
                    sums[sh.ant].second += sh.dy;
                }
            };
            while (rig.prediction()->stats().rebuilds < 1u && rig.step_no() < t + 20) rig.step();
            if (take_between) add(rig.prediction()->take_shifts());
            rig.run(12);
            add(rig.prediction()->take_shifts());
            for (auto it = sums.begin(); it != sums.end();) it = (it->second.first == 0 && it->second.second == 0) ? sums.erase(it) : std::next(it);      // (two that cancel are no entry)
            return rig.prediction()->stats().rebuilds;
        };
        Sums once;
        Sums twice;
        size_t once_entries = 0;
        size_t twice_entries = 0;
        ASSERT_TRUE(play(false, once, once_entries) >= 2u);
        ASSERT_TRUE(play(true, twice, twice_entries) >= 2u);
        ASSERT_FALSE(once.empty());
        ASSERT_TRUE(once == twice);                                                                  // (what two rebuilds moved an ant by, added up, is what each moved it by)
        ASSERT_TRUE(once_entries <= twice_entries);                                                  // (one entry for each ant when taken once)
        sim::SimulationEngine probe;
        const Ids probe_ids = build_world(probe, 31);
        const std::vector<uint32_t>& seat1 = probe_ids.ants[1];
        for (const auto& entry : once) {
            ASSERT_TRUE(std::find(seat1.begin(), seat1.end(), entry.first) != seat1.end());   // (the other player's ants, none of the player's own)
            ASSERT_TRUE(std::max(std::abs(entry.second.first), std::abs(entry.second.second)) <= 128);        // (a tile of walking at most for each of the two rebuilds, in the one direction or the other)
        }
    } TEST_END();

    TEST_CASE("RP3.6 When The Prediction Begins Or Stops The Screen Changes Engine, And Every Ant That Stands Elsewhere In The Other One Is Told To The Picture With How Far (It Slides, It Does Not Jump): At A Stop From Where The Predicted Engine Had It To Where The Confirmed One Has It, At A Start The Other Way; What A Stop Left Is Forgotten When No Frame Took It") {
        Scenario sc;
        sc.seed = 33;
        sc.lead_ms = 300;                                                                   // six ticks: an ant that walks stands 24 px further in the predicted engine
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        rig.run(12);
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 30, 30, rig.ids.ants[0]));
        rig.run(40);
        p.take_shifts();
        const auto places = [](const sim::SimulationEngine& e) {
            std::map<uint32_t, std::pair<int32_t, int32_t>> out;
            for (const auto& a : e.get_world_state().ants) out[a.id] = {a.px, a.py};
            return out;
        };
        const auto shown = places(p.engine());                                              // what the screen shows: the predicted engine
        const auto confirmed = places(rig.confirmed());
        p.set_suspended(true);                                                              // a pause, a hidden page, a link that is away: the screen shows the confirmed engine from here
        const std::vector<Prediction::Shift> back = p.take_shifts();
        size_t moved = 0;
        for (const auto& entry : shown) moved += entry.second != confirmed.at(entry.first) ? 1u : 0u;
        ASSERT_TRUE(moved >= 3);                                                            // (the player's ants walk: the two engines have them at different places)
        ASSERT_EQ(back.size(), moved);
        for (const Prediction::Shift& sh : back) {
            ASSERT_EQ(sh.dx, shown.at(sh.ant).first - confirmed.at(sh.ant).first);          // drawn where it was shown: the confirmed place plus this
            ASSERT_EQ(sh.dy, shown.at(sh.ant).second - confirmed.at(sh.ant).second);
            ASSERT_TRUE(std::max(std::abs(sh.dx), std::abs(sh.dy)) <= 64);
        }
        ASSERT_TRUE(p.take_shifts().empty());
        p.set_suspended(false);
        const auto before_start = places(rig.confirmed());
        rig.step();                                                                         // it begins with the next tick
        ASSERT_TRUE(p.active());
        const auto after_confirmed = places(rig.confirmed());
        const auto after_shown = places(p.engine());
        const std::vector<Prediction::Shift> forth = p.take_shifts();
        ASSERT_FALSE(forth.empty());
        for (const Prediction::Shift& sh : forth) {
            ASSERT_EQ(sh.dx, after_confirmed.at(sh.ant).first - after_shown.at(sh.ant).first);       // from where the confirmed engine has it to where the predicted one does
            ASSERT_EQ(sh.dy, after_confirmed.at(sh.ant).second - after_shown.at(sh.ant).second);
        }
        (void)before_start;
        // a stop that no frame saw (a hidden page): its shifts are about a picture that is gone
        rig.run(10);
        p.take_shifts();
        p.set_suspended(true);
        rig.run(3);
        ASSERT_TRUE(p.take_shifts().empty());
        p.set_suspended(true);
        rig.run(3);
        p.set_suspended(false);
        rig.step();
        for (const Prediction::Shift& sh : p.take_shifts()) {                               // (and a start after it tells only the start)
            const auto now_confirmed = places(rig.confirmed());
            const auto now_shown = places(p.engine());
            ASSERT_EQ(sh.dx, now_confirmed.at(sh.ant).first - now_shown.at(sh.ant).first);
        }
    } TEST_END();

    TEST_CASE("RP3.2 An Order Sealed At Another Tick Than The One Where It Was Applied: Earlier Costs One Rebuild; One Tick Later Costs One Too (Its Tick Passes Without It, And The Next Turn, Where It Is Expected Then, Carries It); Later Than That Costs Three (The Next Turn Passes It Too, Then Its Own Comes) Unless It Lands Where The Second Miss Put It; Afterwards The Prediction Is Right") {
        // The lead is 5 ticks, the buffer 1 and the lag 1, so the order is applied 5 ticks ahead of the confirmed engine; an uplink of 3 steps seals it exactly there. The first miss (its
        // turn arrives without it) puts it at the next tick; a second miss puts it at the display tick of that moment, which is lead - buffer = 4 ticks after the tick that it missed then.
        struct Case {
            int shift;                       // the tick of the turn minus the tick at which the prediction applied the order
            uint64_t rebuilds;
            uint64_t own_timing;             // ... of which the order's own turn found at another tick than the one it was applied at (the others: a turn that passed it without it)
        };
        for (const Case c : {Case{-3, 1, 1}, Case{-2, 1, 1}, Case{-1, 1, 1}, Case{0, 0, 0}, Case{1, 1, 0}, Case{2, 3, 1}, Case{3, 3, 1}, Case{4, 3, 1}, Case{5, 2, 0}}) {
            Scenario sc;
            sc.seed = 40 + static_cast<uint32_t>(c.shift + 5);
            sc.lead_ms = 250;
            sc.uplink = static_cast<uint32_t>(3 + c.shift);
            Rig rig(sc);
            rig.run(12);
            const uint32_t given = rig.step_no();
            rig.issue(make_command(CommandType::GroupMove, 0, 255, 44, 12, rig.ids.ants[0]));
            rig.run(50);
            rig.run_quiet(30);
            const Prediction::Stats& st = rig.prediction()->stats();
            ASSERT_EQ(st.rebuilds, c.rebuilds);
            ASSERT_EQ(st.rebuilds_own_timing, c.own_timing);
            ASSERT_EQ(st.rebuilds_other, c.rebuilds - c.own_timing);
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_OK(verify_min(rig, given + sc.uplink + sc.down_delay + 8, 20));           // after its turn has arrived every frame is the oracle's
        }
    } TEST_END();

    TEST_CASE("RP3.3 An Order That The Server Never Seals Is Dropped After pending_timeout_ticks And The Picture Is Rebuilt Without It; Until Then Every Turn That Passes It Costs At Most One Rebuild") {
        Scenario sc;
        sc.seed = 51;
        sc.prediction.pending_timeout_ticks = 25;
        Rig rig(sc);
        rig.check_derived(1);                                                               // (at every frame: the frame in which the order is dropped is already rebuilt without it)
        rig.run(12);
        const sim::CommandResult r = rig.issue_lost(make_command(CommandType::GroupMove, 0, 255, 50, 50, {rig.ids.ants[0][0], rig.ids.ants[0][1]}));
        ASSERT_TRUE(r.status == sim::CommandResult::Status::Applied);
        const uint32_t given = rig.step_no();
        rig.run(10);
        ASSERT_EQ(rig.prediction()->pending_orders(), 1u);                                  // still waiting: nothing has sealed it and the timeout has not run out
        const uint64_t rebuilds_waiting = rig.prediction()->stats().rebuilds;
        ASSERT_TRUE(rebuilds_waiting >= 1);                                                 // (each turn that passed it without it was a miss: the prediction put it at the display tick again)
        ASSERT_TRUE(rebuilds_waiting <= 10);                                                // ... and never more than one rebuild for a turn
        rig.run(40);                                                                        // past the timeout
        ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
        ASSERT_EQ(rig.prediction()->stats().commands_lost, 1u);
        const uint64_t rebuilds_after = rig.prediction()->stats().rebuilds;
        rig.run_quiet(30);
        ASSERT_EQ(rig.prediction()->stats().rebuilds, rebuilds_after);                       // nothing more to correct
        ASSERT_OK(verify_min(rig, given + 25 + 8, 10));                                      // rebuilt without the order: the oracle's again
        ASSERT_EQ(rig.derived_mismatches, 0u);
        ASSERT_TRUE(rig.derived_checks > 60);
    } TEST_END();

    TEST_CASE("RP3.4 Two Orders Of The Player, The First Of Which The Server Never Seals: The Second Matches Its Turn, The First Is Counted Lost At Once, And The Picture Is Right Afterwards") {
        Scenario sc;
        sc.seed = 53;
        Rig rig(sc);
        rig.run(12);
        rig.issue_lost(make_command(CommandType::GroupMove, 0, 255, 50, 50, {rig.ids.ants[0][0]}));
        rig.run(1);
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 12, 44, {rig.ids.ants[0][1]}));
        rig.run(30);
        rig.run_quiet(30);
        ASSERT_EQ(rig.prediction()->stats().commands_lost, 1u);
        ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
        ASSERT_OK(verify_min(rig, rig.step_no() - 40, 20));
    } TEST_END();
}

void run_property_tests() {
    TEST_CASE("RP4.1 The Predicted Engine Is Derived State: Advanced Tick By Tick It Equals The Engine That A Rebuild Would Make At Every Frame (24 random matches: jitter, bursts, every kind of command, a lead that changes)") {
        uint32_t checks = 0;
        for (uint32_t seed = 1; seed <= 24; ++seed) {
            Lcg rng(seed * 7919u);
            Scenario sc;
            sc.oracle_hashes = false;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(3);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(4);
            sc.uplink = rng.below(4);
            sc.lead_ms = 50 * (1 + rng.below(6));
            Rig rig(sc);
            rig.check_derived(3);
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 120; ++s) {
                const uint32_t roll = rng.below(100);
                if (roll < 14) rig.issue(random_order(rng, rig.ids, 0));
                else if (roll < 18) rig.schedule(rig.step_no() + rng.below(5), random_unpredicted(rng, 0));      // own, not predicted
                else if (roll < 30) rig.schedule(rig.step_no() + rng.below(5), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                else if (roll < 34) rig.schedule(rig.step_no() + rng.below(5), random_unpredicted(rng, static_cast<uint8_t>(1 + rng.below(3))));
                if (s % 40 == 20) rig.prediction()->set_expected_delay_ms(50 * (1 + rng.below(8)));
                if (s % 50 == 30) rig.hold_link_until(rig.step_no() + 1 + rng.below(6));                 // a stall: the turns that are held arrive in a burst
                rig.step();
            }
            rig.run_quiet(20);
            checks += rig.derived_checks;
            ASSERT_EQ(rig.derived_mismatches, 0u);
            ASSERT_TRUE(rig.derived_checks > 30);
            // and the confirmed engine reached every turn: nothing was lost on the way
            ASSERT_TRUE(rig.confirmed().current_tick() > 100);
        }
        ASSERT_TRUE(checks > 700);
    } TEST_END();

    TEST_CASE("RP4.2 After Everything Has Settled The Prediction Is The Oracle: A Burst Of Random Commands From All Seats, Then Quiet Turns (corrections converge)") {
        for (uint32_t seed = 100; seed < 108; ++seed) {
            Lcg rng(seed);
            Scenario sc;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(2);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(3);
            sc.uplink = rng.below(3);
            Rig rig(sc);
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 120; ++s) {
                if (s < 80 && rng.below(5) == 0) rig.issue(random_order(rng, rig.ids, 0));
                if (s < 80 && rng.below(6) == 0) rig.schedule(rig.step_no() + rng.below(4), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                rig.step();
            }
            rig.run_quiet(60);                                                                   // (quiet: empty turns; every order has been sealed and has arrived)
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_FALSE(rig.prediction()->stale());
            ASSERT_OK(verify_min(rig, rig.step_no() - 20, 10));                                     // the last twenty frames: the oracle's, whatever happened before
        }
    } TEST_END();

    TEST_CASE("RP4.3 The Prediction Never Touches The Confirmed Engine, The Turns Or The Hashes: The Same Match With And Without It Has The Same Confirmed State At Every Tick And The Same Cues") {
        for (uint32_t seed : {61u, 62u}) {
            Scenario with;
            with.seed = seed;
            with.down_jitter = 2;
            Scenario without = with;
            without.predict = false;
            Rig a(with);
            Rig b(without);
            Lcg ra(seed);
            Lcg rb(seed);
            a.run(10);
            b.run(10);
            for (uint32_t s = 0; s < 200; ++s) {
                if (s % 6 == 0) {
                    const Command ca = random_order(ra, a.ids, 0);
                    const Command cb = random_order(rb, b.ids, 0);
                    a.issue(ca);
                    b.issue(cb);
                }
                if (s % 10 == 3) {
                    const Command fa = random_order(ra, a.ids, 2);
                    const Command fb = random_order(rb, b.ids, 2);
                    a.schedule(a.step_no() + 1, fa);
                    b.schedule(b.step_no() + 1, fb);
                }
                a.step();
                b.step();
                ASSERT_EQ(a.confirmed().current_tick(), b.confirmed().current_tick());
                ASSERT_TRUE(a.confirmed().state_hash() == b.confirmed().state_hash());
                ASSERT_EQ(a.runner().next_turn_to_execute(), b.runner().next_turn_to_execute());
                const auto audio_a = a.confirmed().poll_audio_events();
                const auto audio_b = b.confirmed().poll_audio_events();
                ASSERT_EQ(audio_a.size(), audio_b.size());
                ASSERT_EQ(a.confirmed().poll_news_events().size(), b.confirmed().poll_news_events().size());
            }
            ASSERT_TRUE(a.sealed.size() == b.sealed.size());
            for (size_t i = 0; i < a.sealed.size(); ++i) ASSERT_TRUE(a.sealed[i].commands == b.sealed[i].commands);
        }
    } TEST_END();
}

void run_state_tests() {
    TEST_CASE("RP5.1 Suspension: Suspended The Prediction Is Gone At Once (the confirmed engine is what is shown), Resumed It Begins At The Next Tick, And An Order In Flight Is Corrected When Its Turn Comes") {
        Scenario sc;
        sc.seed = 71;
        Rig rig(sc);
        rig.run(20);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active());
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 30, 30, {rig.ids.ants[0][0]}));
        ASSERT_EQ(p.pending_orders(), 1u);
        p.set_suspended(true);
        ASSERT_FALSE(p.active());
        ASSERT_EQ(p.pending_orders(), 0u);
        sim::CommandResult r;
        ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 0, 255, 31, 31, {rig.ids.ants[0][1]}), r));
        const uint64_t starts = p.stats().starts;
        rig.run(10);
        ASSERT_FALSE(p.active());
        ASSERT_EQ(p.stats().starts, starts);
        p.set_suspended(false);
        ASSERT_FALSE(p.active());                                                           // (not before a tick has run)
        rig.run(1);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.stats().starts, starts + 1);
        rig.run(40);
        rig.run_quiet(30);
        ASSERT_OK(verify_min(rig, rig.step_no() - 30, 10));
    } TEST_END();

    TEST_CASE("RP5.2 The End Of The Match: The Prediction Reaches It Before The Confirmed Engine, Nothing Hangs, And Both End Alike") {
        Scenario sc;
        sc.seed = 72;
        sc.match_ms = 8000;                                                                  // 160 ticks
        Rig rig(sc);
        Lcg rng(72);
        rig.run(10);
        for (uint32_t s = 0; s < 200; ++s) {
            if (s % 20 == 0) rig.issue(random_order(rng, rig.ids, 0));
            rig.step();
        }
        rig.run_quiet(20);
        ASSERT_TRUE(rig.confirmed().is_match_over());
        ASSERT_TRUE(rig.oracle().is_match_over());
        ASSERT_TRUE(rig.confirmed().state_hash() == rig.oracle().state_hash());
        ASSERT_TRUE(rig.prediction()->engine().is_match_over());
        ASSERT_TRUE(rig.prediction()->engine().get_world_state().match_result.is_over);
    } TEST_END();

    TEST_CASE("RP5.3 The Cues And News Of The Predicted Ticks Are Kept Once, With The Tick That Made Them, Bounded; Taking Them Empties The Buffer; A Rebuild Starts A New Generation") {
        Scenario sc;
        sc.seed = 73;
        sc.prediction.max_events = 40;
        Rig rig(sc);
        Lcg rng(73);
        rig.run(10);
        for (uint32_t s = 0; s < 120; ++s) {
            if (s % 5 == 0) rig.issue(random_order(rng, rig.ids, 0));
            rig.step();
        }
        const std::vector<Prediction::PredictedAudio> audio = rig.prediction()->take_audio();
        ASSERT_TRUE(!audio.empty());
        ASSERT_TRUE(audio.size() <= 40);                                                     // bounded
        for (size_t i = 1; i < audio.size(); ++i) ASSERT_TRUE(audio[i].tick >= audio[i - 1].tick || audio[i].generation != audio[i - 1].generation);
        ASSERT_TRUE(rig.prediction()->take_audio().empty());                                 // taking empties it
        const uint32_t generation = audio.back().generation;
        rig.schedule(rig.step_no() + 1, make_command(CommandType::GroupMove, 1, 255, 10, 40, rig.ids.ants[1]));
        rig.run(10);
        const std::vector<Prediction::PredictedAudio> later = rig.prediction()->take_audio();
        bool newer = false;
        for (const auto& e : later) newer = newer || e.generation > generation;
        ASSERT_TRUE(newer || later.empty());                                                 // (the rebuild's replay is a later generation)
        ASSERT_TRUE(rig.prediction()->stats().rebuilds >= 1);
    } TEST_END();

    TEST_CASE("RP5.4 A Rebuild Does Not Turn What The Confirmed Engine Has Queued Into Predicted Cues And News (the one-minute warning passed in both engines: the replay does not make it again, the copy must not carry it over)") {
        Scenario sc;
        sc.seed = 74;
        sc.match_ms = 62000;                                                                 // the one-minute warning is made by the tick at which the clock falls below 61 s: tick 21
        sc.lead_ms = 200;
        Rig rig(sc);
        rig.run(60);                                                                         // the confirmed engine is past tick 21, the predicted one too; nobody has taken the confirmed engine's cues
        Prediction& p = *rig.prediction();
        {
            sim::SimulationEngine probe(rig.confirmed());                                    // (a look at what the confirmed engine has queued: a copy is polled, not the engine)
            bool queued = false;
            for (const sim::AudioEvent& e : probe.poll_audio_events()) queued = queued || e.sound_id == sim::SoundID::OneMinute;
            ASSERT_TRUE(queued);
        }
        const auto count_audio = [](const std::vector<Prediction::PredictedAudio>& v) {
            size_t n = 0;
            for (const auto& e : v) n += e.event.sound_id == sim::SoundID::OneMinute ? 1u : 0u;
            return n;
        };
        const auto count_news = [](const std::vector<Prediction::PredictedNews>& v) {
            size_t n = 0;
            for (const auto& e : v) n += e.event.string_id == sim::strings::kOneMinute ? 1u : 0u;
            return n;
        };
        ASSERT_EQ(count_audio(p.take_audio()), 1u);                                          // the predicted engine made the warning once, when it ran tick 21 (ahead of the confirmed one) ...
        ASSERT_EQ(count_news(p.take_news()), 1u);
        const uint64_t rebuilds = p.stats().rebuilds;
        rig.schedule(rig.step_no() + 1, make_command(CommandType::GroupMove, 1, 255, 10, 40, rig.ids.ants[1]));       // another player's order: a rebuild, with the warning still queued in the confirmed engine
        rig.run(8);
        ASSERT_TRUE(p.stats().rebuilds > rebuilds);
        ASSERT_EQ(count_audio(p.take_audio()), 0u);                                          // ... and the rebuild's replay starts after it: no second warning, from the copy or anywhere
        ASSERT_EQ(count_news(p.take_news()), 0u);
    } TEST_END();

    TEST_CASE("RP5.5 A Tick That The Prediction Was Not Told Of (a catch-up that went round the hooks, a runner that was restarted) Makes It Stale: The Next Frame Is Rebuilt From The Confirmed Engine, Which Knows What Happened") {
        Scenario sc;
        sc.seed = 75;
        Rig rig(sc);
        rig.check_derived(1);
        Lcg rng(75);
        rig.run(20);
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(10);
        ASSERT_EQ(rig.derived_mismatches, 0u);
        const uint64_t rebuilds = rig.prediction()->stats().rebuilds;
        // two ticks of the confirmed engine that no hook announced, with a command of another player in them: the prediction knows nothing of either
        rig.confirmed().apply_command(make_command(CommandType::GroupMove, 1, 255, 12, 44, rig.ids.ants[1]));
        rig.confirmed().tick();
        rig.confirmed().tick();
        rig.step();
        ASSERT_EQ(rig.derived_mismatches, 0u);                                                // the frame after it is the confirmed engine's truth plus the lead, not the old picture
        ASSERT_TRUE(rig.prediction()->stats().rebuilds > rebuilds);
        rig.run(10);
        ASSERT_EQ(rig.derived_mismatches, 0u);
    } TEST_END();
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The cues: who plays what, once (cue_router.hpp)
// ---------------------------------------------------------------------------------------------------------------------------------

sim::AudioEvent cue(uint32_t sound, uint32_t owner, bool stop = false) {
    sim::AudioEvent e;
    e.sound_id = sound;
    e.owner = owner;
    e.stop = stop;
    e.world_x = 100;
    e.world_y = 100;
    return e;
}

Prediction::PredictedAudio predicted_cue(sim::AudioEvent e, uint64_t tick, uint32_t generation = 1) {
    Prediction::PredictedAudio p;
    p.event = std::move(e);
    p.tick = tick;
    p.generation = generation;
    return p;
}

// "sound/owner" for a cue, "stop/owner" for a stop, in order
std::string describe(const std::vector<sim::AudioEvent>& v) {
    std::string out;
    for (const auto& e : v) out += (e.stop ? std::string("stop") : std::to_string(e.sound_id)) + "/" + std::to_string(e.owner) + " ";
    return out;
}

void run_cue_tests() {
    const CueRouter::OwnsAnt owns = [](uint32_t id) { return id >= 1 && id <= 6; };             // the player's ants are 1 .. 6, another player's 11 ..

    TEST_CASE("RP6.1 Which Cues Are Played From The Predicted Engine: An Own Ant's Own Actions And Its Stops (can't go, harvest, the chimes, fire, bomb, bridge and raid cues, the bump), Never A Blow, A Hit, An Explosion, A Score, An Alliance Cue Or Anything Of Another Player's Ants") {
        namespace S = sim::SoundID;
        for (uint32_t sound : {S::PowerUpHeal, S::PowerUpChime, S::Bump, S::CantGo, S::FoodHarvest, S::FoodGrab, S::FireBeam, S::FireErupt, S::FireExtinguish, S::BombDefuseGrab, S::BombBodySquash, S::BombPick,
                               S::ShovelGravel, S::ShovelWater, S::ThiefDive, S::ThiefRummage, S::ThiefEmerge}) {
            ASSERT_TRUE(CueRouter::is_own_action_sound(sound));
        }
        for (uint32_t sound : {S::ButtonClick, S::CombatNetFairy, S::BombDetonate, S::FireBurnout, S::PlayerDropOut, S::ExitHill, S::Countdown, S::Anthill, S::AllianceBreak, S::AllianceYes, S::ThirtySeconds,
                               S::OneMinute, S::MeleeAttack, S::BaseAlarmSiren, S::CombatAttack1, S::AntStop, S::PowerUpDrop, S::FlingThumpA, S::FlingThumpB, S::Stun, S::WaterSplash, S::AntDrown,
                               S::AttackAlt, S::HeavyPunch, S::WaterAttack, S::ThiefWhip, S::BaseScoreUp, S::BaseScoreDn}) {
            ASSERT_FALSE(CueRouter::is_own_action_sound(sound));
        }
        ASSERT_TRUE(CueRouter::in_class(cue(S::CantGo, 3), owns));                          // an own ant's own action ...
        ASSERT_FALSE(CueRouter::in_class(cue(S::CantGo, 13), owns));                        // ... another player's ant's is not ours to play
        ASSERT_FALSE(CueRouter::in_class(cue(S::CantGo, 0), owns));                         // ... a cue that nobody owns is not an ant's
        ASSERT_FALSE(CueRouter::in_class(cue(S::CantGo, 0x40000005u), owns));               // ... an effect sprite's owner is no ant
        ASSERT_FALSE(CueRouter::in_class(cue(S::FlingThumpA, 3), owns));                    // a blow on an own ant is another player's doing
        ASSERT_FALSE(CueRouter::in_class(cue(S::MeleeAttack, 3), owns));
        ASSERT_TRUE(CueRouter::in_class(cue(S::Bump, 0), owns));                            // the bump has no owner: the engine makes it for the viewer's own ants alone
        ASSERT_TRUE(CueRouter::in_class(cue(0, 3, true), owns));                            // the stop of an own ant (whatever it cuts) ...
        ASSERT_FALSE(CueRouter::in_class(cue(0, 13, true), owns));                          // ... not another's
        ASSERT_FALSE(CueRouter::in_class(cue(0, 0x40000005u, true), owns));                 // ... nor an effect sprite's
        ASSERT_FALSE(CueRouter::in_class(cue(0, 0, true), owns));
    } TEST_END();

    TEST_CASE("RP6.2 Each Occurrence Is Played Once: From The Predicted Engine When It Runs On, Never From A Replay Of A Rebuild, The Confirmed Engine's Copy Dropped Once For Each (The Same Tick Or A Few Ticks Off); What Nobody Played Is Played, What Is Not Of The Class Is The Confirmed Engine's") {
        namespace S = sim::SoundID;
        CueRouter r;
        // tick 10: the predicted engine's own ant 3 is refused (stop, then the cue), an enemy's ant 13 is hit (not ours to play) and the own ant 4 harvests twice
        std::vector<Prediction::PredictedAudio> batch = {predicted_cue(cue(0, 3, true), 10), predicted_cue(cue(S::CantGo, 3), 10), predicted_cue(cue(S::FlingThumpA, 13), 10),
                                                         predicted_cue(cue(S::FoodHarvest, 4), 10), predicted_cue(cue(S::FoodHarvest, 4), 10)};
        ASSERT_EQ(describe(r.from_predicted(batch, owns)), std::string("stop/3 63/3 66/4 66/4 "));                      // in order, and only the class
        ASSERT_EQ(r.stats().played_predicted, 4u);
        ASSERT_EQ(r.outstanding(), size_t{4});
        // a rebuild replays ticks 10 and 11 (a later generation, flagged): the same four, a third harvest that the corrected timeline makes, and a cue of the next tick: nothing is played from a replay
        std::vector<Prediction::PredictedAudio> replay = {predicted_cue(cue(0, 3, true), 10, 2), predicted_cue(cue(S::CantGo, 3), 10, 2), predicted_cue(cue(S::FoodHarvest, 4), 10, 2),
                                                          predicted_cue(cue(S::FoodHarvest, 4), 10, 2), predicted_cue(cue(S::FoodHarvest, 4), 10, 2), predicted_cue(cue(S::CantGo, 5), 11, 2)};
        for (auto& e : replay) e.replay = true;
        ASSERT_EQ(describe(r.from_predicted(replay, owns)), std::string());
        ASSERT_EQ(r.stats().replays_dropped, 6u);
        // the prediction is suspended and begins again within its lead: the straight run makes tick 10 again, and what was played at that very tick is not played again
        ASSERT_EQ(describe(r.from_predicted({predicted_cue(cue(0, 3, true), 10, 3), predicted_cue(cue(S::CantGo, 3), 10, 3)}, owns)), std::string());
        ASSERT_EQ(r.stats().replays_dropped, 6u);
        ASSERT_EQ(r.stats().repeats_dropped, 2u);
        ASSERT_EQ(r.stats().played_predicted, 4u);
        // the confirmed engine reaches tick 10: it makes the same things, the enemy's hit and a cue of the clock. It plays the hit and the clock, drops the four that were heard, and plays the
        // third harvest, which only the replay made (nobody heard it)
        std::vector<sim::AudioEvent> c10 = {cue(0, 3, true), cue(S::CantGo, 3), cue(S::FlingThumpA, 13), cue(S::FoodHarvest, 4), cue(S::FoodHarvest, 4), cue(S::FoodHarvest, 4), cue(S::OneMinute, 0)};
        ASSERT_EQ(describe(r.from_confirmed(c10, 10, owns)), std::string("64/13 66/4 55/0 "));
        ASSERT_EQ(r.stats().duplicates_dropped, 4u);
        ASSERT_EQ(r.stats().played_confirmed_own, 1u);
        ASSERT_EQ(r.outstanding(), size_t{0});
        // a copy a few ticks off (an order that the server sealed 3 ticks later than the prediction assumed): the same cue, dropped; one later than the window is another cue, played
        ASSERT_EQ(describe(r.from_predicted({predicted_cue(cue(S::CantGo, 2), 20)}, owns)), std::string("63/2 "));
        ASSERT_EQ(describe(r.from_confirmed({cue(S::CantGo, 2)}, 23, owns)), std::string());                           // (20 played, 23 made: 3 ticks off)
        ASSERT_EQ(r.outstanding(), size_t{0});
        ASSERT_EQ(describe(r.from_predicted({predicted_cue(cue(S::CantGo, 2), 30)}, owns)), std::string("63/2 "));
        ASSERT_EQ(describe(r.from_confirmed({cue(S::CantGo, 2)}, 30 + CueRouter::kMatchWindowTicks + 1, owns)), std::string("63/2 "));      // (beyond the window: another occurrence)
        ASSERT_EQ(r.stats().phantoms, 1u);                                                                             // ... and the one that was played for tick 30 was never met
        ASSERT_EQ(r.outstanding(), size_t{0});
        // the nearest is the one that is met: two occurrences waited for, the confirmed engine's cue at 41 meets the one at 40, not the one at 35
        ASSERT_EQ(describe(r.from_predicted({predicted_cue(cue(S::FoodHarvest, 5), 35), predicted_cue(cue(S::FoodHarvest, 5), 40)}, owns)), std::string("66/5 66/5 "));
        ASSERT_EQ(describe(r.from_confirmed({cue(S::FoodHarvest, 5)}, 41, owns)), std::string());
        ASSERT_EQ(r.outstanding(), size_t{1});
        ASSERT_EQ(describe(r.from_confirmed({cue(S::FoodHarvest, 5)}, 36, owns)), std::string());                      // (the one at 35 is met by a cue at 36)
        ASSERT_EQ(r.outstanding(), size_t{0});
        // a cue that was played for something that did not happen: the confirmed engine passes the window without making it
        ASSERT_EQ(describe(r.from_predicted({predicted_cue(cue(S::FoodHarvest, 6), 50)}, owns)), std::string("66/6 "));
        const uint64_t phantoms = r.stats().phantoms;
        ASSERT_EQ(describe(r.from_confirmed({}, 50 + CueRouter::kMatchWindowTicks - 1, owns)), std::string());
        ASSERT_EQ(r.stats().phantoms, phantoms);                                                                       // (still within the window: it may yet be met)
        ASSERT_EQ(describe(r.from_confirmed({}, 50 + CueRouter::kMatchWindowTicks, owns)), std::string());
        ASSERT_EQ(r.stats().phantoms, phantoms + 1);
        ASSERT_EQ(r.outstanding(), size_t{0});
        // the kinds are kept apart: an owner's stop is not met by another owner's stop, a cue by another sound's cue
        r.from_predicted({predicted_cue(cue(0, 3, true), 60), predicted_cue(cue(S::CantGo, 3), 60)}, owns);
        ASSERT_EQ(describe(r.from_confirmed({cue(0, 4, true), cue(S::FoodHarvest, 3)}, 60, owns)), std::string("stop/4 66/3 "));
        ASSERT_EQ(r.outstanding(), size_t{2});
        // reset forgets the book and the counts
        r.reset();
        ASSERT_EQ(r.outstanding(), size_t{0});
        ASSERT_EQ(r.stats().played_predicted, 0u);
    } TEST_END();

    TEST_CASE("RP6.3 In A Match: The Cues Of An Own Worker That Walks To The Lunch Box And Harvests Are Heard Once Each, From The Predicted Engine, Before The Confirmed Engine Makes Them; None Is Lost, None Is Doubled, None Is A Phantom") {
        Scenario sc;
        sc.seed = 91;
        sc.lead_ms = 150;                                                    // the exact lead of the scenario (buffer 1, lag 1, uplink 1): no order is ever re-timed
        sc.oracle_hashes = false;
        Rig rig(sc);
        CueRouter router;
        const std::set<uint32_t> own_ids(rig.ids.ants[0].begin(), rig.ids.ants[0].end());
        const CueRouter::OwnsAnt owns_ant = [&](uint32_t id) { return own_ids.count(id) != 0; };
        struct Heard {
            uint32_t step;
            sim::AudioEvent event;
            bool from_predicted;
        };
        std::vector<Heard> heard;                                            // what the loudspeaker would play, in order
        std::vector<std::pair<uint32_t, sim::AudioEvent>> made;              // every cue of the class that the confirmed engine made, and the step at which it did
        rig.set_on_tick_extra([&]() {                                        // (the application's post_tick: the predicted engine's cues first, then the confirmed engine's)
            for (sim::AudioEvent& e : router.from_predicted(rig.prediction()->take_audio(), owns_ant)) heard.push_back(Heard{rig.step_no(), std::move(e), true});
            const uint64_t tick = rig.confirmed().current_tick() - 1;
            std::vector<sim::AudioEvent> events = rig.confirmed().poll_audio_events();
            for (const sim::AudioEvent& e : events) {
                if (CueRouter::in_class(e, owns_ant)) made.emplace_back(rig.step_no(), e);
            }
            for (sim::AudioEvent& e : router.from_confirmed(std::move(events), tick, owns_ant)) heard.push_back(Heard{rig.step_no(), std::move(e), false});
            rig.prediction()->take_news();
        });
        rig.run(12);
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 20, 20, {rig.ids.ants[0][0]}));          // the worker to the lunch box
        rig.issue(make_command(CommandType::GroupSpecial, 0, 255, 25, 25, {rig.ids.ants[0][1]}));        // the bomber to the bomb of the other team
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 30, 30, {rig.ids.ants[0][2]}));           // the fire ant onto the bridge
        rig.issue(make_command(CommandType::GroupMove, 0, 255, 29, 30, {rig.ids.ants[0][5]}));           // the swimmer next to it, in the river
        rig.run(260);
        rig.run_quiet(40);
        ASSERT_TRUE(made.size() >= 5);                                       // (the worker harvests, the bomber defuses, the fire ant bumps: the scenario makes cues of the class)
        std::vector<sim::AudioEvent> made_events;
        for (const auto& m : made) made_events.push_back(m.second);
        std::vector<sim::AudioEvent> heard_events;
        for (const Heard& h : heard) heard_events.push_back(h.event);
        ASSERT_EQ(describe(heard_events), describe(made_events));            // every cue that the confirmed engine made was heard exactly once, in its order
        ASSERT_EQ(heard.size(), made.size());
        for (size_t i = 0; i < heard.size(); ++i) {
            ASSERT_TRUE(heard[i].from_predicted);                            // ... from the predicted engine ...
            ASSERT_TRUE(heard[i].step + 3 <= made[i].first);                 // ... three ticks before the confirmed engine made it (the lead)
        }
        const CueRouter::Stats& st = router.stats();
        ASSERT_EQ(st.played_predicted, made.size());
        ASSERT_EQ(st.duplicates_dropped, made.size());                       // ... and its copies were dropped, one for each
        ASSERT_EQ(st.played_confirmed_own, 0u);
        ASSERT_EQ(st.phantoms, 0u);
        ASSERT_EQ(st.replays_dropped, 0u);                                   // (no rebuild: nothing was replayed)
        ASSERT_EQ(rig.prediction()->stats().rebuilds, 0u);
        ASSERT_EQ(router.outstanding(), size_t{0});
    } TEST_END();

    TEST_CASE("RP6.4 Under Random Jitter, Bursts, Orders Of Every Kind From Every Seat And Rebuilds, The Books Balance: Every Cue Of The Class That The Confirmed Engine Makes Is Dropped (it was heard from the predicted engine) Or Played (nobody had), Every Cue That Was Played From The Predicted Engine Was Met Or Is A Phantom, And Nothing Is Played Twice By A Replay") {
        uint64_t total_made = 0;
        uint64_t total_replays = 0;
        uint64_t total_phantoms = 0;
        uint64_t total_dup = 0;
        for (uint32_t seed = 200; seed < 212; ++seed) {
            Lcg rng(seed * 104729u);
            Scenario sc;
            sc.oracle_hashes = false;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(3);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(3);
            sc.uplink = rng.below(3);
            sc.lead_ms = 50 * (2 + rng.below(4));
            Rig rig(sc);
            CueRouter router;
            const std::set<uint32_t> own_ids(rig.ids.ants[0].begin(), rig.ids.ants[0].end());
            const CueRouter::OwnsAnt owns_ant = [&](uint32_t id) { return own_ids.count(id) != 0; };
            uint64_t made = 0;
            uint64_t played_from_confirmed_class = 0;
            rig.set_on_tick_extra([&]() {
                router.from_predicted(rig.prediction()->take_audio(), owns_ant);
                const uint64_t tick = rig.confirmed().current_tick() - 1;
                std::vector<sim::AudioEvent> events = rig.confirmed().poll_audio_events();
                for (const sim::AudioEvent& e : events) made += CueRouter::in_class(e, owns_ant) ? 1u : 0u;
                for (const sim::AudioEvent& e : router.from_confirmed(std::move(events), tick, owns_ant)) played_from_confirmed_class += CueRouter::in_class(e, owns_ant) ? 1u : 0u;
                rig.prediction()->take_news();
            });
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 220; ++s) {
                const uint32_t roll = rng.below(100);
                if (roll < 12) rig.issue(random_order(rng, rig.ids, 0));
                else if (roll < 24) rig.schedule(rig.step_no() + rng.below(4), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                if (s % 40 == 25) rig.hold_link_until(rig.step_no() + 1 + rng.below(6));
                rig.step();
            }
            rig.run_quiet(60);
            const CueRouter::Stats& st = router.stats();
            ASSERT_EQ(made, st.duplicates_dropped + st.played_confirmed_own);                // every cue the confirmed engine made: heard before, or heard now
            ASSERT_EQ(played_from_confirmed_class, st.played_confirmed_own);
            ASSERT_EQ(router.outstanding(), size_t{0});
            ASSERT_EQ(st.played_predicted, st.duplicates_dropped + st.phantoms);            // every cue played from the predicted engine was met by the confirmed engine's copy, or is a phantom
            total_made += made;
            total_replays += st.replays_dropped;
            total_phantoms += st.phantoms;
            total_dup += st.duplicates_dropped;
        }
        ASSERT_TRUE(total_made >= 20);                                                       // (the matches make cues of the class)
        ASSERT_TRUE(total_dup >= 10);                                                        // ... most of them are heard before the confirmed engine makes them
        ASSERT_TRUE(total_replays >= 1);                                                     // ... rebuilds replayed ticks that had been heard (flagged by the prediction as replays), and they were not heard again
        if (std::getenv("RP_DEBUG")) std::cout << "\n  made " << total_made << " dup " << total_dup << " replays " << total_replays << " phantoms " << total_phantoms << "\n";
    } TEST_END();

    TEST_CASE("RP6.5 The Refusal Of An Order Is Heard Once: Right After The Click When The Lead Is Right, And Still Once When The Lead Is Too Low And Chases The Order Through Three Rebuilds (Not Once For Every Place It Was Put At), The Confirmed Engine's Copy Is The One That Is Dropped") {
        struct Case {
            const char* name;
            uint32_t lead_ms;                // 0: the exact lead of the scenario
            uint64_t rebuilds;
            uint32_t earliest_ticks;         // the cue is heard at least this many ticks before the confirmed engine makes it
        };
        for (const Case c : {Case{"exact", 0, 0, 3}, Case{"too low", 50, 3, 1}, Case{"too high", 600, 1, 5}}) {
            Scenario sc;
            sc.seed = 95;
            sc.buffer_turns = 3;
            sc.down_delay = 1;
            sc.uplink = 2;
            sc.lead_ms = c.lead_ms;                                           // (exact: 3 + 1 + 2 = 6 ticks; too low: one tick, the chase of RP2.4)
            sc.oracle_hashes = false;
            Rig rig(sc);
            CueRouter router;
            const std::set<uint32_t> own_ids(rig.ids.ants[0].begin(), rig.ids.ants[0].end());
            const CueRouter::OwnsAnt owns_ant = [&](uint32_t id) { return own_ids.count(id) != 0; };
            std::vector<std::pair<uint32_t, sim::AudioEvent>> heard;
            std::vector<std::pair<uint32_t, sim::AudioEvent>> made;
            rig.set_on_tick_extra([&]() {
                for (sim::AudioEvent& e : router.from_predicted(rig.prediction()->take_audio(), owns_ant)) heard.emplace_back(rig.step_no(), std::move(e));
                const uint64_t tick = rig.confirmed().current_tick() - 1;
                std::vector<sim::AudioEvent> events = rig.confirmed().poll_audio_events();
                for (const sim::AudioEvent& e : events) {
                    if (CueRouter::in_class(e, owns_ant)) made.emplace_back(rig.step_no(), e);
                }
                for (sim::AudioEvent& e : router.from_confirmed(std::move(events), tick, owns_ant)) heard.emplace_back(rig.step_no(), std::move(e));
                rig.prediction()->take_news();
            });
            rig.run(14);
            rig.issue(make_command(CommandType::GroupSpecial, 0, 255, 30, 10, {rig.ids.ants[0][1]}));       // the bomber is told to plant a bomb in the river: refused
            rig.run(40);
            rig.run_quiet(40);
            ASSERT_EQ(rig.prediction()->stats().rebuilds, c.rebuilds);
            ASSERT_EQ(made.size(), size_t{2});                                                              // the refusal and the stop that cuts it when the clip ends
            ASSERT_EQ(heard.size(), made.size());                                                           // each heard once
            for (size_t i = 0; i < heard.size(); ++i) {
                ASSERT_EQ(heard[i].second.stop, made[i].second.stop);
                ASSERT_EQ(heard[i].second.sound_id, made[i].second.sound_id);
                ASSERT_TRUE(heard[i].first + c.earliest_ticks <= made[i].first);
            }
            ASSERT_EQ(router.stats().played_confirmed_own, 0u);                                             // (the predicted engine played both: the confirmed engine's copies were dropped)
            ASSERT_EQ(router.stats().duplicates_dropped, 2u);
            ASSERT_EQ(router.stats().phantoms, 0u);
            ASSERT_EQ(router.outstanding(), size_t{0});
        }
    } TEST_END();

    TEST_CASE("RP7.1 The Budget: A Prediction Whose Work Costs More Than Its Budget Four Times Within The Window Switches Itself Off For A Cool-Down, At Once: The Confirmed Engine Is Shown, Orders Are Left To The Caller, A Suspension Does Not End It, The Match Is Not Touched; Exactly cooldown_ticks Confirmed Ticks Later It Begins Again From The Confirmed Engine, And Is Derived State Again") {
        const Prediction::Config defaults;
        ASSERT_TRUE(defaults.budget_ns == 12ull * 1000ull * 1000ull && defaults.budget_strikes == 4 && defaults.budget_window_ticks == 200);
        ASSERT_TRUE(defaults.cooldown_ticks == 200 && defaults.cooldown_max_ticks == 3200);
        ManualClock clock;                                                                    // (the clocks of the budget, moved by hand: a timed block costs what the hook says)
        bool slow = true;                                                                     // the machine is busy: every timed block costs 25 ms of CPU against a budget of 20 ms
        Scenario with;
        with.seed = 97;
        with.down_jitter = 1;
        with.prediction.budget_ns = 20 * kMs;
        with.prediction.budget_strikes = 4;
        with.prediction.cooldown_ticks = 30;
        clock.install(with.prediction);
        with.prediction.work_hook = [&slow, &clock]() {
            if (slow) clock.work(25 * kMs);
        };
        Scenario without = with;
        without.predict = false;
        Rig a(with);
        Rig b(without);
        Lcg ra(97);
        Lcg rb(97);
        Prediction& p = *a.prediction();
        a.check_derived(1);
        Prediction::Stats at_start;
        uint64_t began = 0;
        uint64_t ended = 0;
        bool was_cooling = false;
        a.set_on_tick_extra([&]() {                                                           // (after the prediction's own tick: what it did at this tick)
            if (p.cooling_down() == was_cooling) return;
            if (!was_cooling) {
                began = a.confirmed().current_tick();
                at_start = p.stats();
                slow = false;                                                                 // (the load is gone: the next blocks cost nothing)
            } else {
                ended = a.confirmed().current_tick();
            }
            was_cooling = p.cooling_down();
        });
        bool refused = false;
        bool suspended_meanwhile = false;
        for (uint32_t s = 0; s < 160 && ended == 0; ++s) {
            if (s % 9 == 0) {
                a.issue(random_order(ra, a.ids, 0));
                b.issue(random_order(rb, b.ids, 0));
            }
            if (s % 20 == 5) {
                const Command fa = random_order(ra, a.ids, 2);
                const Command fb = random_order(rb, b.ids, 2);
                a.schedule(a.step_no() + 2, fa);
                b.schedule(b.step_no() + 2, fb);
            }
            a.step();
            b.step();
            ASSERT_TRUE(a.confirmed().state_hash() == b.confirmed().state_hash());            // (it never touches the confirmed engine: before, during and after the cool-down)
            if (began != 0 && ended == 0) {
                ASSERT_TRUE(p.cooling_down() && !p.active());                                  // the confirmed engine is shown for as long as it lasts
                if (!refused) {
                    refused = true;
                    sim::CommandResult r;
                    ASSERT_FALSE(p.submit(make_command(CommandType::GroupMove, 0, 255, 10, 10, {a.ids.ants[0][0]}), r));      // an order is left to the caller
                    ASSERT_EQ(p.pending_orders(), 0u);
                }
                ASSERT_EQ(p.cooldown_ticks_left(), began + 30 - a.confirmed().current_tick());
                if (!suspended_meanwhile && a.confirmed().current_tick() >= began + 5) {
                    suspended_meanwhile = true;
                    p.set_suspended(true);                                                     // a suspension and a resume do not end the cool-down early
                    p.set_suspended(false);
                }
            }
        }
        ASSERT_TRUE(began != 0 && ended != 0 && refused && suspended_meanwhile);
        ASSERT_EQ(at_start.over_budget, 4u);                                                   // four strikes: the copy that began it and the three ticks that followed it
        ASSERT_EQ(at_start.ticks_advanced, 3u);
        ASSERT_EQ(at_start.starts, 1u);
        ASSERT_EQ(at_start.cooldowns, 1u);
        ASSERT_EQ(ended - began, 30u);                                                         // cooldown_ticks confirmed ticks
        ASSERT_TRUE(p.active() && !p.cooling_down());                                          // it begins again, at the tick that ends the cool-down ...
        ASSERT_EQ(p.cooldown_ticks_left(), 0u);
        ASSERT_EQ(p.stats().starts, 2u);
        ASSERT_EQ(p.stats().cooldowns, 1u);
        ASSERT_EQ(p.stats().over_budget, 4u);                                                  // ... and the machine is not slow any more
        a.run(60);
        ASSERT_TRUE(p.active());
        sim::CommandResult r;
        ASSERT_TRUE(p.submit(make_command(CommandType::GroupMove, 0, 255, 12, 14, {a.ids.ants[0][1]}), r));        // orders are predicted again
        ASSERT_EQ(a.derived_mismatches, 0u);                                                   // (the engine that begins again is the derivation, at every frame)
        ASSERT_TRUE(a.derived_checks > 40);
    } TEST_END();

    TEST_CASE("RP7.2 The Budget Runs Out In The Middle Of An Order (The Rebuild That The Order Asks For Is The Strike That Starts The Cool-Down): The Order Is Left To The Caller, Nothing Is Touched That The Prediction Has Dropped, The Match Goes On") {
        ManualClock clock;
        Scenario sc;
        sc.seed = 98;
        sc.down_delay = 0;                                                                     // (a turn arrives in the step that seals it: the step of the test says when)
        sc.prediction.budget_ns = 1;
        sc.prediction.budget_strikes = 2;                                                      // the first tick's copy and replay is one strike; the rebuild that the order asks for is the second
        clock.install(sc.prediction);
        sc.prediction.work_hook = [&clock]() { clock.work(kMs); };                             // (every timed block costs a millisecond on the clocks of the rig, more than the nanosecond of the budget)
        sc.oracle_hashes = false;
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        for (uint32_t i = 0; i < 30 && !p.active(); ++i) rig.step();                           // (the first tick: the prediction begins, one strike)
        ASSERT_TRUE(p.active() && !p.cooling_down() && p.stats().over_budget == 1);
        rig.schedule(rig.step_no(), make_command(CommandType::GroupMove, 1, 255, 20, 40, rig.ids.ants[1]));
        rig.step(false);                                                                       // (no frame: nobody asks for the engine; the turn of another player has marked it stale)
        ASSERT_TRUE(p.stale() && p.active() && p.stats().over_budget == 1);
        const sim::CommandResult r = rig.issue(make_command(CommandType::GroupMove, 0, 255, 12, 44, {rig.ids.ants[0][0]}));
        ASSERT_TRUE(p.cooling_down());                                                         // the rebuild that the order asked for was the second strike, and it started the cool-down
        ASSERT_FALSE(p.active());
        ASSERT_TRUE(r.status != sim::CommandResult::Status::Applied);                          // (the order is the caller's: the answer is the default one, nothing was predicted)
        ASSERT_EQ(p.stats().commands_predicted, 0u);
        ASSERT_EQ(p.pending_orders(), 0u);
        rig.run(40);
        ASSERT_FALSE(p.active());                                                              // (the cool-down is 200 ticks)
        ASSERT_TRUE(p.cooling_down());
        ASSERT_TRUE(rig.confirmed().current_tick() > 30);                                      // (the match went on)
    } TEST_END();

    TEST_CASE("RP7.3 Strikes That Are Spread Out Do Not Add Up: With A Window Of Nothing Each Strike Is Forgotten At The Next Tick, So That A Prediction Whose Every Tick Is Over The Budget But Whose Ticks Come Alone Never Cools Down (The Window Counts)") {
        ManualClock clock;
        Scenario sc;
        sc.seed = 99;
        sc.prediction.budget_ns = 1;
        sc.prediction.budget_strikes = 2;
        sc.prediction.budget_window_ticks = 0;
        clock.install(sc.prediction);
        sc.prediction.work_hook = [&clock]() { clock.work(kMs); };
        sc.oracle_hashes = false;
        Rig rig(sc);
        rig.run(60);                                                                           // (no foreign command, no order: nothing is rebuilt, one run of ticks for every tick)
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.active() && !p.cooling_down());
        ASSERT_TRUE(p.stats().over_budget >= 50);                                              // every tick was a strike ...
        ASSERT_EQ(p.stats().rebuilds, 0u);
        ASSERT_EQ(p.stats().cooldowns, 0u);
    } TEST_END();

    TEST_CASE("RP7.4 Every Further Cool-Down Of The Match Is Twice As Long As The One Before (cooldown_ticks, twice, four times ...) Up To cooldown_max_ticks, And The Strikes Of One Do Not Count In The Next (Between Two It Is On For The One Tick That It Takes To Collect The Strikes Again)") {
        ManualClock clock;
        Scenario sc;
        sc.seed = 100;
        sc.oracle_hashes = false;
        sc.prediction.budget_ns = 1;
        sc.prediction.budget_strikes = 2;                                                      // the copy that begins it, then the first tick: a cool-down every other tick of life
        sc.prediction.cooldown_ticks = 10;
        sc.prediction.cooldown_max_ticks = 40;
        clock.install(sc.prediction);
        sc.prediction.work_hook = [&clock]() { clock.work(kMs); };
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        std::vector<uint64_t> began;                                                           // the confirmed tick at which each cool-down began and ended
        std::vector<uint64_t> ended;
        bool was_cooling = false;
        rig.set_on_tick_extra([&]() {
            if (p.cooling_down() == was_cooling) return;
            (was_cooling ? ended : began).push_back(rig.confirmed().current_tick());
            was_cooling = p.cooling_down();
        });
        rig.run(260);
        ASSERT_TRUE(began.size() >= 6 && ended.size() >= 5);
        const uint64_t lengths[5] = {10, 20, 40, 40, 40};                                      // doubled, doubled, the cap, the cap ...
        for (size_t i = 0; i < 5; ++i) {
            ASSERT_EQ(ended[i] - began[i], lengths[i]);
            ASSERT_EQ(began[i + 1] - ended[i], 1u);
        }
        ASSERT_EQ(p.stats().cooldowns, began.size());
        ASSERT_EQ(p.stats().starts, ended.size() + 1);                                         // (it began again after every cool-down that is over)
        // the cap rules the first cool-down too: a cool-down that is asked to be longer than the longest is the longest
        Scenario capped = sc;
        capped.prediction.cooldown_ticks = 50;
        capped.prediction.cooldown_max_ticks = 20;
        Rig second(capped);
        Prediction& q = *second.prediction();
        uint64_t first_began = 0;
        uint64_t first_ended = 0;
        bool cooling = false;
        second.set_on_tick_extra([&]() {
            if (q.cooling_down() == cooling) return;
            if (!cooling && first_began == 0) first_began = second.confirmed().current_tick();
            if (cooling && first_ended == 0) first_ended = second.confirmed().current_tick();
            cooling = q.cooling_down();
        });
        second.run(60);
        ASSERT_TRUE(first_began != 0 && first_ended != 0);
        ASSERT_EQ(first_ended - first_began, 20u);
    } TEST_END();

    TEST_CASE("RP7.5 The Budget Counts The CPU Time Of The Thread, Not The Time That Went By: A Timed Block In Which The Thread Sleeps 30 ms (A Preempted Or Stalled Process Looks The Same) Is No Strike Against A Budget Of 12 ms, One In Which It Computes For 15 ms Is; The Statistics Keep The Wall Time; A Block Is Never Charged More Than Its Wall Time; A Block That Costs Exactly The Budget Is No Strike, One Nanosecond More Is") {
        ASSERT_EQ(work_cost_ns(100, 15625000), 100u);                                          // a clock that counts in ticks may charge a short block a whole tick: the wall time is the bound
        ASSERT_EQ(work_cost_ns(30 * kMs, 0), 0u);
        ASSERT_EQ(work_cost_ns(7, 7), 7u);
        {                                                                                      // the thread sleeps 30 ms in every block (or the process is stalled): the time goes by, the CPU time does not
            ManualClock clock;
            Scenario sc;
            sc.seed = 101;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            clock.install(sc.prediction);
            sc.prediction.work_hook = [&clock]() { clock.wait(30 * kMs); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(14);
            ASSERT_TRUE(p.active() && !p.cooling_down());
            ASSERT_TRUE(p.stats().ticks_advanced >= 8);
            ASSERT_EQ(p.stats().over_budget, 0u);                                              // every block took 30 ms and cost nothing
            ASSERT_EQ(p.stats().advance_ns_max, 30 * kMs);                                     // (the statistics are about the time that went by)
        }
        {                                                                                      // the thread computes for 15 ms in every block
            ManualClock clock;
            Scenario sc;
            sc.seed = 102;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            clock.install(sc.prediction);
            sc.prediction.work_hook = [&clock]() { clock.work(15 * kMs); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(8);
            ASSERT_TRUE(p.stats().over_budget >= 4);                                           // every block cost 15 ms
            ASSERT_TRUE(p.stats().cooldowns >= 1 && !p.active());
        }
        {                                                                                      // a thread clock that counts in ticks reads a whole tick (15.625 ms) after 2 ms of work
            ManualClock clock;
            Scenario sc;
            sc.seed = 103;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            clock.install(sc.prediction);
            sc.prediction.work_hook = [&clock]() { clock.move(2 * kMs, 15625000); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(14);
            ASSERT_TRUE(p.active() && !p.cooling_down());
            ASSERT_TRUE(p.stats().ticks_advanced >= 8);
            ASSERT_EQ(p.stats().over_budget, 0u);                                              // the block is charged its 2 ms, never the tick
            ASSERT_EQ(p.stats().advance_ns_max, 2 * kMs);
        }
        for (const uint64_t cost : {12 * kMs, 12 * kMs + 1}) {                                 // the edge: a block that costs exactly the budget is no strike, one that costs a nanosecond more is
            ManualClock clock;
            Scenario sc;
            sc.seed = 104;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            clock.install(sc.prediction);
            sc.prediction.work_hook = [&clock, cost]() { clock.work(cost); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(14);
            if (cost == 12 * kMs) {
                ASSERT_EQ(p.stats().over_budget, 0u);
                ASSERT_TRUE(p.active() && !p.cooling_down());
            } else {
                ASSERT_TRUE(p.stats().over_budget >= 1);
            }
        }
    } TEST_END();

    TEST_CASE("RP7.6 The Real Clocks (None Installed): The Budget Reads The Thread's CPU Clock And The Wall Clock; A Timed Block In Which The Thread Computes For 15 ms Is A Strike Against A Budget Of 12 ms On Every Platform, One In Which It Sleeps 30 ms Is None Where The Thread Clock Is Exact (Not Windows, Not The Web Build)") {
        const uint64_t t0 = thread_cpu_ns();
        burn_cpu(3 * kMs);
        const uint64_t t1 = thread_cpu_ns();
        ASSERT_TRUE(t1 >= t0 + 3 * kMs);                                                       // the clock counts computing ...
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const uint64_t t2 = thread_cpu_ns();
        ASSERT_TRUE(t2 >= t1 && t2 - t1 < 10 * kMs);                                           // ... and not sleeping
        {
            Scenario sc;
            sc.seed = 101;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            sc.prediction.work_hook = []() { std::this_thread::sleep_for(std::chrono::milliseconds(30)); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(14);
            ASSERT_TRUE(p.active() && !p.cooling_down());
            ASSERT_TRUE(p.stats().ticks_advanced >= 8);
            ASSERT_EQ(p.stats().over_budget, 0u);                                              // every block took 30 ms and cost nothing
            ASSERT_TRUE(p.stats().advance_ns_max >= 30 * kMs);                                 // (the statistics are about the time that went by)
        }
#endif
        {
            Scenario sc;
            sc.seed = 102;
            sc.oracle_hashes = false;
            sc.prediction.budget_ns = 12 * kMs;
            sc.prediction.work_hook = []() { burn_cpu(15 * kMs); };
            Rig rig(sc);
            Prediction& p = *rig.prediction();
            rig.run(8);
            ASSERT_TRUE(p.stats().over_budget >= 4);                                           // every block cost 15 ms
            ASSERT_TRUE(p.stats().cooldowns >= 1 && !p.active());
        }
    } TEST_END();

    TEST_CASE("RP8.1 The Lead Learns The Lag Of The Player's Own Orders: A Lead That Starts Too Low (One Tick Where The Orders Need Six) Costs The First Order A Chase And Then Is The Lag, With No Rebuild For The Orders That Follow; With A Bias Of One The Orders Are Stamped A Tick Beyond The Lag, The Host Seals Them There, And They Cost No Rebuild Either") {
        for (uint32_t bias : {0u, 1u}) {
            Scenario sc;
            sc.seed = 110 + bias;
            sc.buffer_turns = 3;
            sc.down_delay = 1;
            sc.uplink = 2;                                                                       // the lag of an order is 3 + 1 + 2 = 6 ticks
            sc.lead_ms = 50;                                                                     // the estimate: one tick
            sc.prediction.learn_lead = true;
            sc.honor_stamps = true;
            sc.prediction.lead_bias_ticks = bias;
            Rig rig(sc);
            Lcg rng(110 + bias);
            Prediction& p = *rig.prediction();
            rig.check_derived(1);
            rig.run(16);
            ASSERT_EQ(p.lead_ticks(), 1u + bias);                                                // (no order yet: the estimate and the bias)
            ASSERT_EQ(p.learned_lag_ticks(), 0u);
            rig.issue(random_order(rng, rig.ids, 0));
            rig.run(30);
            ASSERT_EQ(p.learned_lag_ticks(), 6u);                                                // the first order taught it
            ASSERT_EQ(p.lead_ticks(), 6u + bias);                                                // (it rises at once)
            const uint64_t after_first = p.stats().rebuilds;
            ASSERT_TRUE(after_first >= 1);                                                       // (the first order was chased: its turns passed it)
            for (uint32_t i = 0; i < 10; ++i) {
                rig.issue(random_order(rng, rig.ids, 0));
                rig.run(18);
            }
            rig.run_quiet(30);
            const uint64_t later = p.stats().rebuilds - after_first;
            ASSERT_EQ(later, 0u);                                                                // the lead is the lag (or beyond it with a bias): the host seals every order at its stamp, so every order lands where it is run
            ASSERT_EQ(rig.derived_mismatches, 0u);
            ASSERT_EQ(p.pending_orders(), 0u);
        }
    } TEST_END();

    TEST_CASE("RP8.2 The Lag Is The Longest Of The Last Twelve Orders, No More Than Two Ticks Above Their Middle: One Order That A Stall Held Up Raises The Lead By The Two Ticks, Not To Its Own Lag; A Lag That Has Grown For Good Raises It At Once (The Host's Answer Says That The Order Was Late); It Falls When The Longer Lags Leave The Twelve") {
        Scenario sc;
        sc.seed = 120;
        sc.buffer_turns = 1;
        sc.down_delay = 1;
        sc.uplink = 1;                                                                           // the lag is 1 + 1 + the uplink: 3
        sc.lead_ms = 150;
        sc.prediction.learn_lead = true;
        sc.honor_stamps = true;
        sc.prediction.lead_bias_ticks = 0;
        sc.prediction.lag_fresh_ticks = 5000;
        Rig rig(sc);
        Lcg rng(120);
        Prediction& p = *rig.prediction();
        rig.run(16);
        std::vector<uint32_t> lags;                                                              // (the lags that the orders had, as the rule sees them: at most the longest lead)
        // the rule, written down again here: the longest of the last 12, no more than 2 above the middle (the one at index n / 2 of the sorted ones)
        const auto rule = [&]() {
            std::vector<uint32_t> last(lags.end() - static_cast<std::ptrdiff_t>(std::min<size_t>(12, lags.size())), lags.end());
            std::sort(last.begin(), last.end());
            return std::min(last.back(), last[last.size() / 2] + 2u);
        };
        const auto order_with_uplink = [&](uint32_t uplink) {
            rig.set_uplink(uplink);
            rig.issue(random_order(rng, rig.ids, 0));
            lags.push_back(std::min<uint32_t>(2u + uplink, 12u));
            rig.run(uplink + 25);                                                                // (sealed, back, run)
            return p.learned_lag_ticks();
        };
        // lags 3 3 3, then a stall (32: held to 12), 3 3, then the link is slower for good: 5 5 5 5 5 5, then one order that is quick again: 3, and then quick ones until the slower lags leave the twelve
        std::vector<uint32_t> uplinks = {1, 1, 1, 30, 1, 1, 3, 3, 3, 3, 3, 3, 1};
        for (int i = 0; i < 12; ++i) uplinks.push_back(1);
        for (size_t i = 0; i < uplinks.size(); ++i) {
            const uint32_t learned = order_with_uplink(uplinks[i]);
            ASSERT_EQ(learned, rule());
            if (i == 2) ASSERT_EQ(p.lead_ticks(), 3u);
            if (i == 3) {
                ASSERT_EQ(learned, 5u);                                                          // the stall raised it by the two ticks that a single lag may stand above the middle, not to its own 12
                ASSERT_EQ(p.lead_ticks(), 5u);                                                   // (at once: the order was late)
            }
            if (i == 6) ASSERT_EQ(p.lead_ticks(), 5u);                                           // the slower link: its lags of 5 are within the lead already
            if (i == 11) ASSERT_TRUE(p.lead_ticks() >= 5u);                                      // the link of 5 is covered
        }
        ASSERT_EQ(p.learned_lag_ticks(), 3u);                                                    // the slower lags and the stall have left the twelve: the lag of the link now
        rig.run_quiet(20);
        ASSERT_EQ(p.lead_ticks(), 3u);
    } TEST_END();

    TEST_CASE("RP8.3 A Lag That Is Not Fresh Is Forgotten (lag_fresh_ticks): The Lead Goes Back To The Owner's Estimate Once The Orders Are Old, And The Orders That Come After A Quiet Spell Teach It Again") {
        Scenario sc;
        sc.seed = 121;
        sc.buffer_turns = 1;
        sc.down_delay = 1;
        sc.uplink = 3;                                                                           // the lag is 5
        sc.lead_ms = 100;                                                                        // the estimate: 2
        sc.prediction.learn_lead = true;
        sc.honor_stamps = true;
        sc.prediction.lead_bias_ticks = 0;
        sc.prediction.lag_fresh_ticks = 60;
        Rig rig(sc);
        Lcg rng(121);
        Prediction& p = *rig.prediction();
        rig.run(16);
        ASSERT_EQ(p.lead_ticks(), 2u);
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(30);
        ASSERT_EQ(p.learned_lag_ticks(), 5u);
        ASSERT_EQ(p.lead_ticks(), 5u);
        rig.run_quiet(61);                                                                       // the lag is older than 60 ticks now
        ASSERT_EQ(p.learned_lag_ticks(), 0u);
        rig.run_quiet(12);                                                                       // (the lead falls to the estimate one tick at a time, four ticks apart)
        ASSERT_EQ(p.lead_ticks(), 2u);
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(30);
        ASSERT_EQ(p.learned_lag_ticks(), 5u);                                                    // it learns again
        ASSERT_EQ(p.lead_ticks(), 5u);
    } TEST_END();

    TEST_CASE("RP8.7 What The Orders Taught Is Forgotten When The Prediction Stops (A Pause, A Host Change, A Way Back: The Link And The Buffer May Be Others Afterwards): It Begins Again With The Owner's Estimate, And The Next Orders Teach It Again") {
        Scenario sc;
        sc.seed = 123;
        sc.buffer_turns = 1;
        sc.down_delay = 1;
        sc.uplink = 3;                                                                           // the lag is 5
        sc.lead_ms = 100;                                                                        // the estimate: 2
        sc.prediction.learn_lead = true;
        sc.honor_stamps = true;
        sc.prediction.lead_bias_ticks = 0;
        Rig rig(sc);
        Lcg rng(123);
        Prediction& p = *rig.prediction();
        rig.run(16);
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(30);
        ASSERT_EQ(p.learned_lag_ticks(), 5u);
        ASSERT_EQ(p.lead_ticks(), 5u);
        p.set_suspended(true);
        ASSERT_EQ(p.learned_lag_ticks(), 0u);                                                    // forgotten
        rig.run(5);
        p.set_suspended(false);
        rig.run(2);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.lead_ticks(), 2u);                                                           // the estimate, at once (a new beginning, not a fall)
        rig.issue(random_order(rng, rig.ids, 0));
        rig.run(30);
        ASSERT_EQ(p.learned_lag_ticks(), 5u);                                                    // taught again
        ASSERT_EQ(p.lead_ticks(), 5u);
    } TEST_END();

    TEST_CASE("RP8.5 The Lead's Edges: A Prediction Told Nothing Starts At Two Ticks Of Lag Plus The Bias (Never Outside Its Bounds); Two Orders That Disagree Give The Longer One (The Host Seals An Order At Its Stamp Only When The Stamp Is Still Open For It); An Order Held Up For Ever Is Counted As The Longest Lead; The Bias On A Long Lag Stops At The Longest Lead") {
        Rig rig(Scenario{});
        Prediction::Config c = Scenario::rig_config();
        {
            Prediction fresh(rig.confirmed(), rig.runner(), c);                                  // no bias, nothing told: the estimate that stands in for a delay that is not known yet
            ASSERT_EQ(fresh.lead_ticks(), 2u);
            ASSERT_EQ(fresh.learned_lag_ticks(), 0u);
        }
        c.lead_bias_ticks = 3;
        {
            Prediction fresh(rig.confirmed(), rig.runner(), c);
            ASSERT_EQ(fresh.lead_ticks(), 5u);
        }
        c.lead_bias_ticks = 9;
        c.max_lead_ticks = 4;
        {
            Prediction fresh(rig.confirmed(), rig.runner(), c);
            ASSERT_EQ(fresh.lead_ticks(), 4u);                                                   // 2 + 9 is out of bounds: the longest lead
        }
        c.max_lead_ticks = 12;
        c.min_lead_ticks = 7;
        c.lead_bias_ticks = 0;
        {
            Prediction fresh(rig.confirmed(), rig.runner(), c);
            ASSERT_EQ(fresh.lead_ticks(), 7u);                                                   // ... and 2 is below the shortest
        }
        // two orders that disagree (lags 3 and 5): the longer one (the host seals an order at its stamp only when the stamp is open for it)
        {
            Scenario sc;
            sc.seed = 125;
            sc.buffer_turns = 1;
            sc.down_delay = 1;
            sc.lead_ms = 150;
            sc.prediction.learn_lead = true;
            sc.honor_stamps = true;
            sc.prediction.lead_bias_ticks = 0;
            Rig two(sc);
            Lcg rng(125);
            two.run(16);
            two.set_uplink(1);
            two.issue(random_order(rng, two.ids, 0));
            two.run(30);
            ASSERT_EQ(two.prediction()->learned_lag_ticks(), 3u);
            two.set_uplink(3);
            two.issue(random_order(rng, two.ids, 0));
            two.run(30);
            ASSERT_EQ(two.prediction()->learned_lag_ticks(), 5u);
            ASSERT_EQ(two.prediction()->lead_ticks(), 5u);
        }
        // orders that a stall holds up for ever (an uplink of 30 steps) are lags of the longest lead, however long they really took
        {
            Scenario sc;
            sc.seed = 126;
            sc.buffer_turns = 1;
            sc.down_delay = 1;
            sc.uplink = 30;
            sc.lead_ms = 150;
            sc.prediction.learn_lead = true;
            sc.honor_stamps = true;
            sc.prediction.lead_bias_ticks = 0;
            Rig stalled(sc);
            Lcg rng(126);
            stalled.run(16);
            for (int i = 0; i < 3; ++i) {
                stalled.issue(random_order(rng, stalled.ids, 0));
                stalled.run(60);
            }
            ASSERT_EQ(stalled.prediction()->learned_lag_ticks(), 12u);
            ASSERT_EQ(stalled.prediction()->lead_ticks(), 12u);
        }
        // a lag of 11 and a bias of 3: the lead stops at the longest one
        {
            Scenario sc;
            sc.seed = 127;
            sc.buffer_turns = 1;
            sc.down_delay = 1;
            sc.uplink = 9;
            sc.lead_ms = 150;
            sc.prediction.learn_lead = true;
            sc.honor_stamps = true;
            sc.prediction.lead_bias_ticks = 3;
            Rig edge(sc);
            Lcg rng(127);
            edge.run(16);
            edge.issue(random_order(rng, edge.ids, 0));
            edge.run(40);
            ASSERT_EQ(edge.prediction()->learned_lag_ticks(), 11u);
            ASSERT_EQ(edge.prediction()->lead_ticks(), 12u);
        }
    } TEST_END();

    TEST_CASE("RP8.6 The Product's Defaults Are The Documented Numbers (docs/NETWORK_PORT.md, README): The Lead Learns From The Longest Lag Of The Last Twelve Orders (No More Than Two Ticks Above Their Middle) No Older Than 400 Ticks With No Bias (An Order Is Put Where The Host Seals It), Between 1 And 12 Ticks, Rises By One Tick After 40 Ticks; A Lost Order Is Dropped After 100; The Budget Is 12 ms, Four Strikes In 200 Ticks, A Cool-Down Of 200 Ticks That Doubles Up To 3200") {
        const Prediction::Config c;
        ASSERT_TRUE(c.learn_lead);
        ASSERT_EQ(c.lag_samples, 12u);
        ASSERT_EQ(c.lag_spike_cap_ticks, 2u);
        ASSERT_EQ(c.lag_fresh_ticks, 400u);
        ASSERT_EQ(c.lead_bias_ticks, 0u);                                                        // (a project decision: the lead is the lag; a bias of 1 stamps a tick beyond it)
        ASSERT_EQ(c.min_lead_ticks, 1u);
        ASSERT_EQ(c.max_lead_ticks, 12u);
        ASSERT_EQ(c.lead_rise_after_ticks, 40u);
        ASSERT_EQ(c.pending_timeout_ticks, 100u);
        ASSERT_EQ(c.budget_ns, 12ull * 1000ull * 1000ull);
        ASSERT_EQ(c.budget_strikes, 4u);
        ASSERT_EQ(c.budget_window_ticks, 200u);
        ASSERT_EQ(c.cooldown_ticks, 200u);
        ASSERT_EQ(c.cooldown_max_ticks, 3200u);
        ASSERT_TRUE(!c.work_hook);
        ASSERT_TRUE(!c.wall_clock && !c.cpu_clock);                                              // (the product measures a block with the real clocks: only the tests move them)
    } TEST_END();

    TEST_CASE("RP8.4 The Lead That Learns And Is Biased Keeps The Prediction Derived State: 16 Random Matches (a lag that changes, bursts, orders of every kind from every seat), The Predicted Engine Equals The One A Rebuild Would Make At Every Third Frame, And It Converges") {
        uint32_t checks = 0;
        for (uint32_t seed = 300; seed < 316; ++seed) {
            Lcg rng(seed * 7919u);
            Scenario sc;
            sc.oracle_hashes = false;
            sc.seed = seed;
            sc.buffer_turns = 1 + rng.below(3);
            sc.down_delay = rng.below(3);
            sc.down_jitter = rng.below(3);
            sc.uplink = rng.below(4);
            sc.lead_ms = 50 * (1 + rng.below(5));
            sc.prediction.learn_lead = true;
            sc.honor_stamps = true;
            sc.prediction.lead_bias_ticks = rng.below(3);
            Rig rig(sc);
            rig.check_derived(3);
            rig.run(sc.buffer_turns + sc.down_delay + 6);
            for (uint32_t s = 0; s < 160; ++s) {
                const uint32_t roll = rng.below(100);
                if (roll < 12) rig.issue(random_order(rng, rig.ids, 0));
                else if (roll < 24) rig.schedule(rig.step_no() + rng.below(5), random_order(rng, rig.ids, static_cast<uint8_t>(1 + rng.below(3))));
                else if (roll < 27) rig.schedule(rig.step_no() + rng.below(5), random_unpredicted(rng, static_cast<uint8_t>(1 + rng.below(3))));
                if (s % 50 == 25) rig.set_uplink(rng.below(6));
                if (s % 60 == 30) rig.hold_link_until(rig.step_no() + 1 + rng.below(6));
                rig.step();
            }
            rig.run_quiet(60);
            checks += rig.derived_checks;
            ASSERT_EQ(rig.derived_mismatches, 0u);
            ASSERT_EQ(rig.prediction()->pending_orders(), 0u);
            ASSERT_FALSE(rig.prediction()->stale());
        }
        ASSERT_TRUE(checks > 600);
    } TEST_END();
}

// --measure: how often and how much the prediction corrects the picture (not a test: it prints tables and checks nothing). Seat 0 is the player whose screen it is; the other seats up to the
// number of players give random orders at a steady rate (a person gives one every second or two: the table has 0.5 and 2 a second for each of them), and the player's own orders come at the
// rate in the table. The link has a round trip of about 60 - 100 ms (a jitter buffer of one turn, a turn that takes one step to arrive and an order that takes one step to be sealed, a step
// of jitter unless the line says none), the prediction is the product's (it learns its lead, with no bias unless the line says another), and a ten minute match is played (12,000 ticks) for
// every line. What a correction is: a rebuild after which at least one ant stands elsewhere at the display tick than it did before (Prediction::set_measure_corrections).
struct MeasureLine {
    double own_rate;
    uint32_t players;
    double foreign_rate;
    uint32_t bias;
    uint32_t jitter;
    bool stops{true};       // false: no Stop orders (the orders of a match are mostly moves and attacks; a Stop is the one order whose tick decides on which tile an ant halts)
};

void measure_line(const MeasureLine& line, uint32_t seed) {
    Scenario sc;
    sc.seed = seed;
    sc.buffer_turns = 1;
    sc.down_delay = 1;
    sc.down_jitter = line.jitter;
    sc.uplink = 1;
    sc.oracle_hashes = false;
    sc.prediction.learn_lead = true;
    sc.honor_stamps = true;
    sc.prediction.lead_bias_ticks = line.bias;
    sc.prediction.lag_fresh_ticks = 400;
    Rig rig(sc);
    rig.prediction()->set_measure_corrections(true);
    Lcg rng(sc.seed * 31u + 7u);
    const uint32_t steps = 12000;
    uint32_t foreign_orders = 0;
    rig.run(16);
    for (uint32_t s = 0; s < steps; ++s) {
        if (rng.below(100000) < static_cast<uint32_t>(line.own_rate * 5000.0)) {                                                   // a rate a second, 20 ticks a second: probability rate / 20 per tick
            Command order = random_order(rng, rig.ids, 0);
            while (!line.stops && order.type == CommandType::Stop) order = random_order(rng, rig.ids, 0);
            rig.issue(order);
        }
        for (uint32_t seat = 1; seat < line.players; ++seat) {
            if (rng.below(100000) < static_cast<uint32_t>(line.foreign_rate * 5000.0)) {
                Command order = random_order(rng, rig.ids, static_cast<uint8_t>(seat));
                while (!line.stops && order.type == CommandType::Stop) order = random_order(rng, rig.ids, static_cast<uint8_t>(seat));
                rig.schedule(rig.step_no() + 1 + rng.below(2), order);
                ++foreign_orders;
            }
        }
        rig.step();
    }
    const Prediction::Stats& st = rig.prediction()->stats();
    const double minutes = static_cast<double>(steps) * 0.05 / 60.0;
    std::cout << std::fixed << std::setprecision(1) << " " << std::setw(5) << line.own_rate << " | " << std::setw(7) << line.players << " x " << std::setw(4) << line.foreign_rate << " | " << std::setw(4) << line.bias
              << " | " << std::setw(6) << line.jitter << (line.stops ? "" : " (no Stop)") << " | " << std::setw(10) << rig.own_orders << " | " << std::setw(14) << foreign_orders << " | " << std::setw(8) << st.rebuilds_own_timing << " / "
              << std::setw(4) << st.rebuilds_foreign << " / " << st.rebuilds_other << " | " << std::setw(10) << static_cast<double>(st.corrections_visible) / minutes << " | " << std::setw(8)
              << (st.corrections_visible != 0 ? static_cast<double>(st.ants_moved) / static_cast<double>(st.corrections_visible) : 0.0) << " | " << std::setw(6)
              << (st.ants_moved != 0 ? static_cast<double>(st.move_px_total) / static_cast<double>(st.ants_moved) : 0.0) << " / " << std::setw(2) << st.max_move_px << " | " << std::setprecision(3)
              << (st.rebuilds != 0 ? static_cast<double>(st.rebuild_ns_total) / 1e6 / static_cast<double>(st.rebuilds) : 0.0) << " / " << static_cast<double>(st.rebuild_ns_max) / 1e6 << " | "
              << (st.ticks_advanced != 0 ? static_cast<double>(st.advance_ns_total) / 1e6 / static_cast<double>(st.ticks_advanced) : 0.0) << " / " << static_cast<double>(st.advance_ns_max) / 1e6 << "\n";
}

void run_measure() {
    const char* head =
        "\n own/s | players x foreign/s | bias | jitter | own orders | foreign orders | rebuilds: own timing / foreign / other | visible/min | ants/corr | px mean / max | rebuild ms mean / max | tick ms mean / max\n"
        " ------|---------------------|------|--------|------------|----------------|----------------------------------------|-------------|-----------|---------------|-----------------------|-------------------\n";
    std::cout << head;
    const MeasureLine foreign_lines[] = {{1.0, 1, 0.0, 0, 1}, {0.0, 2, 0.5, 0, 1}, {0.0, 2, 2.0, 0, 1}, {0.0, 4, 0.5, 0, 1}, {0.0, 4, 2.0, 0, 1},
                                         {1.0, 2, 0.5, 0, 1}, {1.0, 2, 2.0, 0, 1}, {1.0, 3, 0.5, 0, 1}, {1.0, 3, 2.0, 0, 1}, {1.0, 4, 0.5, 0, 1}, {1.0, 4, 2.0, 0, 1}};
    uint32_t n = 0;
    for (const MeasureLine& line : foreign_lines) measure_line(line, 1000u + 17u * n++);
    std::cout << "\n the bias, own orders only (the same orders, the same link; a jitter of one step is the lag changing by a tick from order to order):\n" << head;
    const MeasureLine bias_lines[] = {{1.0, 1, 0.0, 0, 0}, {1.0, 1, 0.0, 1, 0}, {1.0, 1, 0.0, 0, 1}, {1.0, 1, 0.0, 1, 1}, {1.0, 1, 0.0, 0, 2}, {1.0, 1, 0.0, 1, 2},
                                      {1.0, 1, 0.0, 0, 0, false}, {1.0, 1, 0.0, 1, 0, false}, {1.0, 1, 0.0, 0, 2, false}, {1.0, 1, 0.0, 1, 2, false}};
    for (const MeasureLine& line : bias_lines) measure_line(line, 2000u + 17u * n++);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--measure") {      // (not a test: prints how often and how much the prediction corrects the picture; takes a minute)
        run_measure();
        return 0;
    }
    std::cout << "\n=======================================================\n [PREDICTION SUITE] client-side prediction of one's own orders\n=======================================================\n";
    run_basic_tests();
    run_exactness_tests();
    run_correction_tests();
    run_property_tests();
    run_state_tests();
    run_cue_tests();
    std::cout << "\n=======================================================\n"
              << " PREDICTION TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
