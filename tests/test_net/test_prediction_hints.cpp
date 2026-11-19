// Tests of the hints of the prediction (Prediction::hint, docs/P2P_HINTS.md): another player's order, or a computer player's, that reaches a machine ahead of its sealed turn.
//
// The rig is test_prediction's (prediction_rig.hpp): a server that the test plays, a real runner and engines, and an oracle that executes every turn the moment it is sealed. A hint is delivered to the
// prediction at a step the test names, with the tick that the sender's screen applied it at.
//
// What is checked:
//   - the benefit: with the hint the picture is the oracle's from the frame in which the hint arrives, where without it the picture waits for the sealed turn;
//   - the matching rule: a hint at the tick that is sealed costs nothing when the turn comes; at the next tick it is moved once and confirmed; one that is not sealed, or sealed as another order, is dropped and
//     the sealed stream wins; a hint that comes after its turn, a copy of a hint, a refused kind and a flood are ignored;
//   - the property that makes it safe: the predicted engine that was advanced tick by tick equals the engine that a rebuild would make now, under random orders, hints that are lost, doubled, late, at wrong
//     ticks and out of order, and when the hints have settled the picture is the oracle's;
//   - what a hint can never do: change the confirmed engine, a turn or a hash, and show an ant where the confirmed engine has not explored (Fog of War).
#include "prediction_rig.hpp"

namespace {

using Verdict = Prediction::HintVerdict;

// seat `seat`'s whole colony walks to (x, y)
Command walk(const Rig& rig, uint8_t seat, int16_t x, int16_t y) { return make_command(CommandType::GroupMove, seat, 255, x, y, rig.ids.ants[seat]); }

Scenario hint_scenario(uint32_t seed) {
    Scenario sc;
    sc.seed = seed;
    sc.lead_ms = 400;                                                                       // the display stands 8 ticks ahead of the confirmed engine: an order that is not known walks for a while
    sc.uplink = 6;
    return sc;
}

void run_benefit_tests() {
    TEST_CASE("RH1.1 A Hint Puts Another Player's Order Into The Picture In The Frame In Which It Arrives, And Its Turn, At The Tick The Hint Stood At, Costs Nothing") {
        Scenario sc = hint_scenario(41);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;                                               // the turn that will carry seat 1's order
        const Command order = walk(rig, 1, 20, 40);
        rig.schedule(t, order);
        rig.schedule_hint(t - 2, order, 1, t);                                              // ... and the hint comes two steps before the seal
        rig.run(20);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_accepted, 1u);
        ASSERT_EQ(st.hints_confirmed, 1u);
        ASSERT_EQ(st.hints_confirmed_moved + st.hints_dropped + st.hints_late + st.hints_duplicate + st.hints_refused, 0u);
        ASSERT_EQ(st.rebuilds, 1u);                                                         // the hint's own rebuild; the turn, which carries what was applied, needs none
        ASSERT_EQ(st.rebuilds_hint, 1u);
        ASSERT_EQ(st.rebuilds_foreign, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_TRUE(rig.hint_verdicts.size() == 1 && rig.hint_verdicts[0] == Verdict::Accepted);
        ASSERT_OK(verify_min(rig, t - 2, 25));                                              // from the frame in which the hint arrived, every frame is the oracle's
    } TEST_END();

    TEST_CASE("RH1.2 Without The Hint The Same Order Is Wrong On The Screen Until Its Turn Arrives: One Foreign Rebuild, And The Frames Before It Are Not The Oracle's") {
        Scenario sc = hint_scenario(41);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;
        rig.schedule(t, walk(rig, 1, 20, 40));
        rig.run(20);
        rig.run_quiet(30);
        ASSERT_EQ(rig.prediction()->stats().rebuilds_foreign, 1u);
        ASSERT_FALSE(verify_min(rig, t - 2, 25).empty());                                   // (the frames between the hint's arrival and the turn's show the ants where they were)
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 1, 20));
    } TEST_END();

    TEST_CASE("RH1.3 A Hint That Stands Ahead Of The Display Tick Waits For It And Is Applied Without A Rebuild") {
        Scenario sc = hint_scenario(43);
        Rig rig(sc);
        rig.run(12);
        Prediction& p = *rig.prediction();
        const uint64_t display = p.display_tick();
        const Command order = walk(rig, 1, 20, 40);
        const uint32_t t = rig.step_no() + 14;
        rig.schedule(t, order);
        rig.schedule_hint(rig.step_no() + 1, order, 1, display + 3);                        // the sender is three ticks ahead of this screen
        rig.run(2);
        ASSERT_EQ(p.stats().rebuilds, 0u);                                                  // not past its tick: nothing to rebuild
        ASSERT_EQ(p.pending_hints(), 1u);
        rig.run(25);
        rig.run_quiet(20);
        ASSERT_EQ(p.stats().hints_accepted, 1u);
        ASSERT_EQ(p.pending_hints(), 0u);                                                   // (sealed at t, a different tick from where it stood: it is one correction, and the sealed stream decides)
        ASSERT_TRUE(p.stats().hints_confirmed + p.stats().hints_confirmed_moved + p.stats().hints_dropped == 1u);
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 2, 20));
    } TEST_END();
}

void run_rule_tests() {
    TEST_CASE("RH2.1 A Hint Whose Order Is Sealed One Tick Later Moves Once To The Next Tick And Is Confirmed There; The Picture Is Right Again From The Turn On") {
        Scenario sc = hint_scenario(51);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;
        const Command order = walk(rig, 1, 20, 40);
        rig.schedule(t + 1, order);                                                         // sealed one turn after the tick that the sender's screen had
        rig.schedule_hint(t - 2, order, 1, t);
        rig.run(20);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_accepted, 1u);
        ASSERT_EQ(st.hints_confirmed, 1u);                                                  // (moved by the miss of turn t, then standing where the sealed order is)
        ASSERT_EQ(st.hints_dropped, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_OK(verify_min(rig, t + 1 + sc.down_delay + 1, 20));
    } TEST_END();

    TEST_CASE("RH2.2 A Hint That The Server Never Seals Is Dropped After Two Turns Without It, And The Picture Is The Oracle's Again: The Sealed Stream Wins") {
        Scenario sc = hint_scenario(52);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;
        rig.schedule_hint(t - 2, walk(rig, 1, 20, 40), 1, t);                               // nothing is ever sealed for it
        rig.run(25);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_accepted, 1u);
        ASSERT_EQ(st.hints_dropped, 1u);
        ASSERT_EQ(st.hints_confirmed + st.hints_confirmed_moved, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_OK(verify_min(rig, t + 8, 25));                                              // (the oracle never moved those ants)
    } TEST_END();

    TEST_CASE("RH2.3 The Sealed Order Is Another From The Hint: The Hint Is Dropped, The Sealed One Is Applied, And The Picture Is The Oracle's") {
        Scenario sc = hint_scenario(53);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;
        rig.schedule(t, walk(rig, 1, 40, 20));                                              // the sender's order, as it was sealed
        rig.schedule_hint(t - 2, walk(rig, 1, 20, 40), 1, t);                               // ... which is not what the hint said
        rig.run(25);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_dropped, 1u);
        ASSERT_EQ(st.hints_confirmed, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 2, 20));
    } TEST_END();

    TEST_CASE("RH2.4 Of Two Hints Of One Seat The One That The Server Seals Takes Out The Older One It Did Not Seal; The Numbers Are The Sender's, Not The Order Of Arrival") {
        Scenario sc = hint_scenario(54);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 5;
        const Command first = walk(rig, 1, 20, 40);
        const Command second = walk(rig, 1, 44, 12);
        rig.schedule(t, second);                                                            // only the second one is sealed (the first was refused by the server's flood limit)
        rig.schedule_hint(t - 3, second, 2, t);                                             // the later order's hint comes first (an unordered channel)
        rig.schedule_hint(t - 2, first, 1, t - 1);
        rig.run(25);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_accepted, 2u);
        ASSERT_EQ(st.hints_confirmed + st.hints_confirmed_moved, 1u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);                                   // (the first never stays: the second's turn, or two turns without it, took it out)
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 3, 20));
    } TEST_END();

    TEST_CASE("RH3.1 A Copy Of A Hint That Was Seen Is Ignored (A Channel Sends Each Hint Three Times), Also From The Far Side Of A Window Of Numbers; A Number Behind The Window Is Ignored") {
        Scenario sc = hint_scenario(55);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 5;
        const Command order = walk(rig, 1, 20, 40);
        rig.schedule(t, order);
        rig.schedule_hint(t - 3, order, 7, t);
        rig.schedule_hint(t - 2, order, 7, t);
        rig.schedule_hint(t - 1, order, 7, t);
        rig.run(25);
        const std::vector<Verdict>& v = rig.hint_verdicts;
        ASSERT_TRUE(v.size() == 3 && v[0] == Verdict::Accepted && v[1] == Verdict::Duplicate && v[2] == Verdict::Duplicate);
        ASSERT_EQ(rig.prediction()->stats().hints_duplicate, 2u);
        Prediction& p = *rig.prediction();
        const Command other = walk(rig, 2, 12, 12);
        ASSERT_TRUE(p.hint(other, 100, p.display_tick() + 2) == Verdict::Accepted);
        ASSERT_TRUE(p.hint(other, 36, p.display_tick() + 2) == Verdict::Duplicate);         // 64 numbers behind the highest one: out of the window
        ASSERT_TRUE(p.hint(other, 99, p.display_tick() + 2) == Verdict::Accepted);          // inside the window and not seen: a copy that arrived out of order
        ASSERT_TRUE(p.hint(other, 99, p.display_tick() + 2) == Verdict::Duplicate);
    } TEST_END();

    TEST_CASE("RH3.2 A Hint That Comes After Its Sealed Turn Is Ignored: The Order Is Not Applied Twice, And The Picture Stays The Oracle's") {
        Scenario sc = hint_scenario(56);
        Rig rig(sc);
        rig.run(12);
        const uint32_t t = rig.step_no() + 4;
        const Command order = walk(rig, 1, 20, 40);
        rig.schedule(t, order);
        rig.schedule_hint(t + sc.down_delay + 2, order, 1, t, true);                        // the direct path was slower than the server's: the hint comes after the turn that is in hand
        rig.run(25);
        rig.run_quiet(30);
        const Prediction::Stats& st = rig.prediction()->stats();
        ASSERT_EQ(st.hints_late, 1u);
        ASSERT_EQ(st.hints_accepted, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_EQ(st.rebuilds, 1u);                                                         // the turn's own foreign rebuild, nothing more
        ASSERT_OK(verify_min(rig, t + sc.down_delay + 1, 20));
    } TEST_END();

    TEST_CASE("RH3.3 A Hint Of A Tick Far Behind The Turns That Are In Hand Is Late Whatever Else Is Known, And A Hint Is Refused When It Is An Own Seat's, Of A Kind That Is Not Predicted, Or Too Many Wait") {
        Scenario sc = hint_scenario(57);
        Rig rig(sc);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.hint(walk(rig, 1, 20, 40), 1, 3) == Verdict::Refused);                // (not running yet: the "Get ready" dialog)
        rig.run(30);
        ASSERT_TRUE(p.active());
        ASSERT_TRUE(p.hint(walk(rig, 0, 20, 40), 1, p.display_tick()) == Verdict::Refused);         // this player's own seat is never hinted
        ASSERT_TRUE(p.hint(make_command(CommandType::Hatch, 1), 1, p.display_tick()) == Verdict::Refused);
        ASSERT_TRUE(p.hint(make_command(CommandType::AllianceInvite, 1, 2), 1, p.display_tick()) == Verdict::Refused);
        ASSERT_TRUE(p.hint(make_command(CommandType::Quit, 1), 1, p.display_tick()) == Verdict::Refused);
        ASSERT_TRUE(p.hint(make_command(CommandType::GroupMove, sim::MAX_PLAYERS, 255, 4, 4, {1}), 1, p.display_tick()) == Verdict::Refused);
        ASSERT_TRUE(p.hint(walk(rig, 1, 20, 40), 1, 2) == Verdict::Late);                   // tick 2 of a match that is 30 ticks on
        const Prediction::Config cfg;
        for (size_t i = 0; i < cfg.max_hints; ++i) ASSERT_TRUE(p.hint(walk(rig, 1, static_cast<int16_t>(10 + i % 40), 20), static_cast<uint32_t>(10 + i), p.display_tick() + 2) == Verdict::Accepted);
        ASSERT_EQ(p.pending_hints(), cfg.max_hints);
        ASSERT_TRUE(p.hint(walk(rig, 1, 5, 5), 5000, p.display_tick() + 2) == Verdict::Refused);       // the limit of hints that wait
        ASSERT_EQ(p.stats().hints_refused, 7u);
    } TEST_END();

    TEST_CASE("RH3.4 A Hint Of A Tick Far Ahead Is Put At The Most A Lead Beyond The Display Tick: No Number That A Sender Can Make Holds The Prediction For Long") {
        Scenario sc = hint_scenario(58);
        Rig rig(sc);
        rig.run(30);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.hint(walk(rig, 1, 20, 40), 1, 4000000000ull) == Verdict::Accepted);
        rig.run(60);                                                                        // no turn carries it: after the two turns that pass its (clamped) tick it is dropped, or by the timeout
        rig.run_quiet(120);
        ASSERT_EQ(p.pending_hints(), 0u);
        ASSERT_EQ(p.stats().hints_dropped, 1u);
    } TEST_END();

    TEST_CASE("RH3.5 Hints Are Forgotten When The Prediction Stops: A Hint Does Not Reach Into A Prediction That Begins Again") {
        Scenario sc = hint_scenario(59);
        Rig rig(sc);
        rig.run(20);
        Prediction& p = *rig.prediction();
        ASSERT_TRUE(p.hint(walk(rig, 1, 20, 40), 1, p.display_tick() + 3) == Verdict::Accepted);
        ASSERT_EQ(p.pending_hints(), 1u);
        p.set_suspended(true);
        ASSERT_EQ(p.pending_hints(), 0u);
        ASSERT_TRUE(p.hint(walk(rig, 1, 22, 40), 2, p.display_tick() + 3) == Verdict::Refused);         // (off: the confirmed engine is shown, hints have no place)
        p.set_suspended(false);
        rig.run(10);
        ASSERT_TRUE(p.active());
        ASSERT_EQ(p.pending_hints(), 0u);
    } TEST_END();
}

void run_order_tests() {
    TEST_CASE("RH4.1 Two Seats Order In The Same Tick And Their Hints Come In The Opposite Order: The Picture Has Them In The Order Of A Turn, As The Engine That A Rebuild Makes") {
        Scenario sc = hint_scenario(61);
        Rig rig(sc);
        rig.check_derived(1);
        rig.run(12);
        const uint32_t t = rig.step_no() + 5;
        const Command a = walk(rig, 1, 20, 40);
        const Command b = walk(rig, 2, 40, 20);
        rig.schedule(t, b);
        rig.schedule(t, a);
        rig.schedule_hint(t - 3, b, 1, t);
        rig.schedule_hint(t - 2, a, 1, t);
        rig.run(25);
        rig.run_quiet(30);
        ASSERT_EQ(rig.derived_mismatches, 0u);
        ASSERT_TRUE(rig.derived_checks > 20u);
        ASSERT_EQ(rig.prediction()->stats().hints_confirmed, 2u);
        ASSERT_OK(verify_min(rig, t - 2, 25));
    } TEST_END();

    TEST_CASE("RH4.2 Hints Of The Other Seats Together With The Player's Own Orders, A Lead That Changes And A Jittery Link: The Picture Is Right After Them And Equal To A Rebuild At Every Frame") {
        Scenario sc = hint_scenario(62);
        sc.down_jitter = 1;
        Rig rig(sc);
        rig.check_derived(1);
        Lcg rng(62);
        rig.run(14);
        uint32_t seq[sim::MAX_PLAYERS] = {0, 0, 0, 0};
        for (uint32_t s = 0; s < 120; ++s) {
            if (s % 3 == 0) {
                const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                const Command c = random_order(rng, rig.ids, seat);
                const uint32_t t = rig.step_no() + 4;
                rig.schedule(t + rng.below(2), c);                                          // sealed at the tick of the hint or a tick later
                rig.schedule_hint(t - rng.below(3), c, ++seq[seat], t);
            }
            if (s % 7 == 0) rig.issue(random_order(rng, rig.ids, 0));
            if (s == 60) rig.prediction()->set_expected_delay_ms(250);
            rig.step();
        }
        rig.run_quiet(60);
        ASSERT_EQ(rig.derived_mismatches, 0u);
        ASSERT_EQ(rig.prediction()->pending_hints(), 0u);
        ASSERT_TRUE(rig.prediction()->stats().hints_confirmed + rig.prediction()->stats().hints_confirmed_moved > 20u);
        ASSERT_OK(verify_min(rig, 14 + 120 + 16, 40));                                    // (after the last own order has been sealed too: it is applied by the prediction at the tick it was given, not by a hint)
    } TEST_END();
}

// A match with the same orders, played twice: with the other seats' hints and without them
struct Played {
    uint64_t confirmed_hash{0};
    std::vector<uint64_t> post;
    std::vector<TurnMsg> sealed;
    uint64_t rebuilds{0};
    uint64_t visible{0};           // corrections in which an ant stood elsewhere afterwards
    uint64_t pixels{0};            // the sum of how far the ants were moved
    uint64_t moved{0};             // ants moved
    uint64_t hints{0};
};

Played play_with_hints(uint32_t seed, bool hints, bool fog, bool damage, uint32_t early = 0) {
    Scenario sc = hint_scenario(seed);
    sc.fog = fog;
    Rig rig(sc);
    rig.check_derived(1);
    rig.prediction()->set_measure_corrections(true);
    Lcg rng(seed);
    Lcg luck(seed * 31u + 7u);                                                              // (the server's: sealed late, or never; the same in the match without hints)
    Lcg hdice(seed * 17u + 3u);                                                             // (the hints' own dice: the orders and the turns of the two matches must be the same)
    uint32_t seq[sim::MAX_PLAYERS] = {0, 0, 0, 0};
    rig.run(14);
    for (uint32_t s = 0; s < 160; ++s) {
        if (s % 4 == 0) {
            const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
            const Command c = random_order(rng, rig.ids, seat);
            const uint32_t t = rig.step_no() + 4 + early;                                       // (the turn that will carry it: `early` steps ahead of the hint's arrival window)
            const uint32_t sealed_at = t + (damage ? luck.below(3) : 0u);                       // (damage: sealed a tick or two after the hint's tick)
            const bool lost = damage && luck.below(5) == 0;                                    // ... or never sealed
            if (!lost) rig.schedule(sealed_at, c);
            if (hints) {
                ++seq[seat];
                rig.schedule_hint(t - early - hdice.below(3), c, seq[seat], t);
                if (damage && hdice.below(2) == 0) rig.schedule_hint(t - early - hdice.below(3), c, seq[seat], t);      // a copy
            }
        }
        if (s % 9 == 0) rig.issue(random_order(rng, rig.ids, 0));
        rig.step();
    }
    rig.run_quiet(60);
    Played out;
    out.confirmed_hash = rig.confirmed().state_hash().total;
    out.post = rig.post;
    out.sealed = rig.sealed;
    out.rebuilds = rig.prediction()->stats().rebuilds;
    out.visible = rig.prediction()->stats().corrections_visible;
    out.pixels = rig.prediction()->stats().move_px_total;
    out.moved = rig.prediction()->stats().ants_moved;
    out.hints = rig.prediction()->stats().hints_accepted;
    if (rig.derived_mismatches != 0) out.confirmed_hash = 0;
    return out;
}

void run_property_tests() {
    TEST_CASE("RH5.1 Hints That Are Right, Lost, Doubled, At Wrong Ticks Or Out Of Order Never Change The Confirmed Engine, A Turn Or A Hash: The Same Match With And Without Them Ends In The Same State, Turn By Turn") {
        for (uint32_t seed : {71u, 72u, 73u}) {
            for (bool damage : {false, true}) {
                const Played with = play_with_hints(seed, true, false, damage);
                const Played without = play_with_hints(seed, false, false, damage);
                ASSERT_TRUE(with.hints > 10u);
                ASSERT_NE(with.confirmed_hash, 0u);                                          // (0: the predicted engine differed from a rebuild at some frame)
                ASSERT_EQ(with.confirmed_hash, without.confirmed_hash);
                ASSERT_TRUE(with.post == without.post);                                      // the oracle's hash after every turn
                ASSERT_TRUE(with.sealed.size() == without.sealed.size());
                for (size_t i = 0; i < with.sealed.size(); ++i) ASSERT_TRUE(with.sealed[i].commands == without.sealed[i].commands);   // the turns are the server's alone
            }
        }
    } TEST_END();

    TEST_CASE("RH5.2 With Hints The Picture Is Corrected By Less: The Same Orders Move The Ants Of The Other Seats By A Fraction Of The Distance When The Hints Come Well Ahead Of The Seal (The Gain Is The Time That The Hint Is Ahead Of The Turn)") {
        uint64_t with_px = 0;
        uint64_t without_px = 0;
        uint64_t with_moved = 0;
        uint64_t without_moved = 0;
        for (uint32_t seed : {81u, 82u, 83u, 84u}) {
            const Played with = play_with_hints(seed, true, false, false, 4);              // the hints come 4 to 6 steps before the seal
            const Played without = play_with_hints(seed, false, false, false, 4);
            with_px += with.pixels;
            without_px += without.pixels;
            with_moved += with.moved;
            without_moved += without.moved;
        }
        std::cout << "[with " << with_px << " px over " << with_moved << " ants, without " << without_px << " px over " << without_moved << " ants] ";
        ASSERT_TRUE(without_moved > 20u && without_px > 200u);
        ASSERT_TRUE(with_px * 2 < without_px);
    } TEST_END();

    TEST_CASE("RH5.3 Fog Of War: A Hint Never Shows An Ant Where The Confirmed Engine Has Not Explored, Nor Explores Ground: The Predicted Engine's Explored Ground Is The Confirmed Engine's At Every Frame") {
        Scenario sc = hint_scenario(91);
        sc.fog = true;
        Rig rig(sc);
        Lcg rng(91);
        uint32_t seq[sim::MAX_PLAYERS] = {0, 0, 0, 0};
        rig.run(14);
        for (uint32_t s = 0; s < 100; ++s) {
            if (s % 3 == 0) {
                const uint8_t seat = static_cast<uint8_t>(1 + rng.below(3));
                const Command c = random_order(rng, rig.ids, seat);
                const uint32_t t = rig.step_no() + 4;
                rig.schedule(t, c);
                rig.schedule_hint(t - rng.below(3), c, ++seq[seat], t);
            }
            rig.step();
        }
        rig.run_quiet(40);
        ASSERT_TRUE(rig.prediction()->stats().hints_accepted > 20u);
        size_t checked = 0;
        ASSERT_OK(rig.verify_views(0, &checked, false));                                    // (only the explored ground: what the picture hides is decided by it)
        ASSERT_TRUE(checked > 60u);
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [PREDICTION HINTS SUITE] orders that arrive ahead of their turns\n=======================================================\n";
    run_benefit_tests();
    run_rule_tests();
    run_order_tests();
    run_property_tests();
    std::cout << "\n=======================================================\n"
              << " PREDICTION HINTS TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
