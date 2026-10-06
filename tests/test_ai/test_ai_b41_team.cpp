// Team play of the standard bot (B4-1, AI8.x): the accept rule for an invitation to team up, the help for the ally, the economy with an ally. Hand-made worlds (b41_helpers.hpp) and
// TREASURE, quick enough for suite 2.20.
//
//   AI8.2   the contest-aware pile order on TREASURE: the first harvest orders of every seat go to the centre, at every level; the worker's order is unchanged
//   AI8.3   the classes of the order in a hand-made world (the centre, a side that one enemy shares, the safe piles, the ally's side last), recomputed when an alliance forms; within a
//           class by value (Medium, Hard) or by distance (Easy), the richer pile first
//   AI8.4   the opening: the ants that go to the contested centre of TREASURE at the start (Easy none, Medium one, Hard two), and none where nothing is contested
//   AI8.1   the accept rule: accepted with four or three live teams, denied when the alliance would unite all live teams, when the bot or the inviter already has an ally; the worker
//           bot (the yardstick) still denies; the standard bot never invites, withdraws or breaks
//   AI8.7   the accept rule is one pure function with a reason (team_up_answer): every case of AI8.1 with its reason, the order of the reasons, the inviter that is gone, the texts
//           that tell a player why a bot declined
#include "ai_test.hpp"
#include "b41_helpers.hpp"

#include "ants_ai/team_up.hpp"
#include "ants_ai/worker_bot.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

Command alliance_command(CommandType type, uint8_t issuer, uint8_t other) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.other_player = other;
    return c;
}

// Four hills, one worker for each of the teams in `alive_mask` (an ant in sight is what makes a team live for the bot)
void world_of(sim::SimulationEngine& sim, uint8_t alive_mask, uint32_t seed = 91) {
    empty_field(sim, seed);
    for (uint8_t t = 0; t < 4; ++t) {
        if ((alive_mask >> t) & 1u) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
    }
}

}  // namespace

void run_b41_team_tests() {
    TEST_CASE("AI8.1 The Accept Rule: An Invitation To Team Up Is Accepted When Three Or More Teams Are Live, Denied When The Alliance Would Unite All Live Teams (Two Live Teams), When The Bot Or The Inviter Already Has An Ally; Every Level Answers; The Worker Bot Still Denies; The Standard Bot Never Invites, Withdraws Or Breaks An Alliance") {
        struct Case {
            uint8_t alive_mask;
            uint8_t dropped_mask;
            bool bot_has_ally;
            bool inviter_has_ally;
            bool accept;
        };
        const Case cases[] = {
            {0x0F, 0x00, false, false, true},    // four live teams: accept
            {0x07, 0x08, false, false, true},    // team 3 dropped out: three live teams, still two sides
            {0x0B, 0x00, false, false, true},    // team 2 has no ant in sight but is not gone: the bot counts three live teams (0, 1, 3)
            {0x03, 0x0C, false, false, false},   // two live teams: the alliance would unite them all
            {0x03, 0x00, false, false, false},   // (teams 2 and 3 have no ants in sight: the same)
            {0x0F, 0x00, true, false, false},    // the bot has an ally: accepting would break it
            {0x0F, 0x00, false, true, false},    // the inviter has an ally
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const Case& k : cases) {
                sim::SimulationEngine sim;
                world_of(sim, k.alive_mask);
                if (k.dropped_mask & 8u) sim.drop_player(3);
                if (k.dropped_mask & 4u) sim.drop_player(2);
                if (k.bot_has_ally) sim.form_alliance(0, 2);
                if (k.inviter_has_ally) sim.form_alliance(1, 3);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(2);
                sim.apply_command(alliance_command(CommandType::AllianceInvite, 1, 0));
                rig.run(260);
                const StandardBot& bot = rig.as<StandardBot>();
                if (k.accept) {
                    ASSERT_EQ(sim.get_ally_id(0), 1);
                    ASSERT_EQ(sim.get_ally_id(1), 0);
                    ASSERT_EQ(bot.accepts(), 1u);
                    ASSERT_EQ(bot.denials(), 0u);
                } else {
                    ASSERT_TRUE(sim.get_ally_id(0) != 1);
                    ASSERT_EQ(bot.accepts(), 0u);
                    ASSERT_EQ(bot.denials(), 1u);
                }
                // one answer, no more (the invitation is gone), and the bot never invites, withdraws or breaks
                ASSERT_EQ(rig.proposed_count(CommandType::AllianceAccept) + rig.proposed_count(CommandType::AllianceDeny), 1u);
                ASSERT_EQ(rig.proposed_count(CommandType::AllianceInvite), 0u);
                ASSERT_EQ(rig.proposed_count(CommandType::AllianceWithdraw), 0u);
                ASSERT_EQ(rig.proposed_count(CommandType::AllianceBreak), 0u);
            }
        }
        // the worker bot (the frozen yardstick) denies as it always did
        {
            sim::SimulationEngine sim;
            world_of(sim, 0x0F);
            Rig rig(sim, 0, Level::Medium, std::make_unique<WorkerBot>(), 4, 4);
            rig.run(2);
            sim.apply_command(alliance_command(CommandType::AllianceInvite, 1, 0));
            rig.run(260);
            ASSERT_TRUE(sim.get_ally_id(0) != 1);
            ASSERT_EQ(rig.proposed_count(CommandType::AllianceDeny), 1u);
            ASSERT_EQ(rig.proposed_count(CommandType::AllianceAccept), 0u);
        }
        // a long allied match: the bot never breaks the alliance, never invites again
        {
            sim::SimulationEngine sim;
            world_of(sim, 0x0F);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(2);
            sim.apply_command(alliance_command(CommandType::AllianceInvite, 1, 0));
            rig.run(3000);
            ASSERT_EQ(sim.get_ally_id(0), 1);
            ASSERT_EQ(rig.proposed_count(CommandType::AllianceBreak) + rig.proposed_count(CommandType::AllianceInvite) + rig.proposed_count(CommandType::AllianceWithdraw), 0u);
        }
    } TEST_END();

    TEST_CASE("AI8.2 The Contest-Aware Pile Order On TREASURE (The Centre First, Then The Contested Sides): The First Harvest Orders Of Every Seat At Every Level Go To The Pile In The Centre (Contested By All Three Enemies); Without The Order They Go To The Near Piles; The Worker's Order Is Unchanged")
    {
        sim::SimulationEngine probe;
        start_match(probe, "TREASURE", 7, 0x0F);
        const MapInfo pmap(probe);
        size_t centre = 0;
        for (const PileInfo& p : pmap.piles()) {
            if (p.anchor.x == 30 && p.anchor.y == 29) centre = p.index;
        }
        const size_t piles = probe.grid().food_objects().size();
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (uint8_t seat = 0; seat < 4; ++seat) {
                for (const bool aware : {true, false}) {
                    sim::SimulationEngine sim;
                    start_match(sim, "TREASURE", 7, 0x0F);
                    LevelPlan plan = plan_for(level);
                    plan.contest_aware = aware;
                    plan.contest_opening_ants = 0;                                                            // (the opening has its own test, AI8.4)
                    Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(30);
                    const std::vector<int> targets = harvest_targets(rig, sim, seat, piles);
                    ASSERT_TRUE(!targets.empty());
                    if (aware) {
                        ASSERT_EQ(static_cast<size_t>(targets.front()), centre);                              // the centre first, at every seat
                        ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(centre)), static_cast<int>(PileClass::Multi));
                    } else {
                        if (seat != 2) ASSERT_TRUE(static_cast<size_t>(targets.front()) != centre);           // by value per trip it is a pile near the own hill (seat 2's nearest pile is the centre)
                        ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(centre)), -1);
                    }
                }
            }
        }
        // the worker bot (the frozen yardstick) never had the order: its first pile is a near one at every seat
        for (uint8_t seat = 0; seat < 4; ++seat) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 7, 0x0F);
            Rig rig(sim, seat, Level::Medium, std::make_unique<WorkerBot>(), 4, 4);
            rig.run(30);
            const std::vector<int> targets = harvest_targets(rig, sim, seat, piles);
            ASSERT_TRUE(!targets.empty());
            if (seat != 2) ASSERT_TRUE(static_cast<size_t>(targets.front()) != centre);
        }
    } TEST_END();

    TEST_CASE("AI8.3 The Classes Of The Order In A Hand-Made World: The Centre (All Enemies Reach It) Before The Side That One Enemy Shares, Before The Safe Piles Near The Own Hill, The Side Of An Ally Last (And Of An Enemy When Nobody Is Allied: One); Recomputed When An Alliance Forms; Within A Class By Value (Medium, Hard) Or By Distance (Easy)")
    {
        // four hills at the corners of a 60 x 60 field: the centre (27, 27) is as far from every hill, (27, 6) lies between hills 0 and 1, (6, 27) between hills 0 and 2, (9, 9) is next to hill 0
        const auto build = [&](sim::SimulationEngine& sim, bool allied_with_2) {
            empty_field(sim, 95);
            const int32_t centre = add_pile(sim, 27, 27, 30, 25);
            const int32_t side_one = add_pile(sim, 27, 6, 30, 25);        // shared with team 1
            const int32_t side_two = add_pile(sim, 6, 27, 30, 25);        // shared with team 2 (the ally in the second run)
            const int32_t safe = add_pile(sim, 12, 12, 30, 25);
            for (int i = 0; i < 16; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i % 8, 9 + i / 8});
            for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
            if (allied_with_2) sim.form_alliance(0, 2);
            return std::array<int32_t, 4>{centre, side_one, side_two, safe};
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const bool allied : {false, true}) {
                sim::SimulationEngine sim;
                const auto id = build(sim, allied);
                LevelPlan plan = plan_for(level);
                plan.contest_aware = true;
                plan.gate = false;                                                                           // (the gate guiding raises the ants per pile: this test is about the order of the piles)
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(30);
                const HarvestTask& h = rig.as<StandardBot>().harvest();
                ASSERT_EQ(h.tier_of(static_cast<uint32_t>(id[0])), static_cast<int>(PileClass::Multi));
                ASSERT_EQ(h.tier_of(static_cast<uint32_t>(id[1])), static_cast<int>(PileClass::One));
                ASSERT_EQ(h.tier_of(static_cast<uint32_t>(id[2])), allied ? static_cast<int>(PileClass::Shared) : static_cast<int>(PileClass::One));
                ASSERT_EQ(h.tier_of(static_cast<uint32_t>(id[3])), static_cast<int>(PileClass::Safe));
                // the orders go in the order of the classes: the centre, the side(s) of the enemies, the safe pile, the ally's side last
                const std::vector<int> targets = harvest_targets(rig, sim, 0, 4);
                ASSERT_TRUE(targets.size() >= 3);
                ASSERT_EQ(targets[0], id[0]);
                const auto position = [&](int pile) { return std::find(targets.begin(), targets.end(), pile) - targets.begin(); };
                ASSERT_TRUE(position(id[1]) < position(id[3]));                                              // the side that an enemy shares before the safe pile
                if (allied) ASSERT_TRUE(position(id[2]) > position(id[3]));                                  // the ally's side after it
                else ASSERT_TRUE(position(id[2]) < position(id[3]));
            }
        }
        {   // two teams on TREASURE: the piles on the other team's side, which it reaches far sooner than the own hill (below 70 percent of the own cost), are hopeless; the rest are not
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 7, 0x03);
            LevelPlan plan = plan_for(Level::Medium);
            plan.contest_aware = true;
            plan.gate = false;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            const HarvestTask& h = rig.as<StandardBot>().harvest();
            size_t hopeless = 0;
            for (const PileInfo& p : rig.map().piles()) {
                const int own = p.approach[0].cost;
                const int theirs = p.approach[1].cost;
                if (own < 0 || theirs < 0) continue;
                const bool far_sooner = static_cast<int64_t>(theirs) * 100 < static_cast<int64_t>(own) * 70;
                ASSERT_EQ(h.tier_of(p.index) == static_cast<int>(PileClass::Hopeless), far_sooner);
                hopeless += far_sooner ? 1u : 0u;
            }
            ASSERT_TRUE(hopeless >= 1u);
        }
        // an alliance that forms while the match runs moves the side of the new ally to the end (the classes are made again at every look)
        {
            sim::SimulationEngine sim;
            const auto id = build(sim, false);
            LevelPlan plan = plan_for(Level::Medium);
            plan.contest_aware = true;
                plan.gate = false;                                                                           // (the gate guiding raises the ants per pile: this test is about the order of the piles)
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(id[2])), static_cast<int>(PileClass::One));
            sim.form_alliance(0, 2);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 9});                                      // (the classes are made when ants wait for an order: a newborn does)
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(id[2])), static_cast<int>(PileClass::Shared));
            sim.break_alliance(0, 2);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 9});
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(id[2])), static_cast<int>(PileClass::One));
        }
        // within a class: two safe piles, the nearer worth 20 a unit, the farther 30 a unit: Medium and Hard (value per trip) take the dearer one, Easy (distance) the nearer;
        // with rank_by_remaining the richer pile (more units left) first
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 96);
            const int32_t near_cheap = add_pile(sim, 10, 10, 20, 20);
            const int32_t far_dear = add_pile(sim, 17, 17, 20, 50);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
            LevelPlan plan = plan_for(level);
            plan.contest_aware = true;
                plan.gate = false;                                                                           // (the gate guiding raises the ants per pile: this test is about the order of the piles)
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
            ASSERT_TRUE(!targets.empty());
            ASSERT_EQ(targets.front(), level == Level::Easy ? near_cheap : far_dear);
        }
        for (const bool rank_remaining : {false, true}) {
            sim::SimulationEngine sim;
            empty_field(sim, 96);
            const int32_t near_small = add_pile(sim, 10, 10, 8, 25);
            const int32_t far_rich = add_pile(sim, 16, 16, 60, 25);
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
            LevelPlan plan = plan_for(Level::Medium);
            plan.contest_aware = true;
                plan.gate = false;                                                                           // (the gate guiding raises the ants per pile: this test is about the order of the piles)
            plan.rank_by_remaining = rank_remaining;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
            ASSERT_TRUE(!targets.empty());
            ASSERT_EQ(targets.front(), rank_remaining ? far_rich : near_small);
        }
    } TEST_END();

    TEST_CASE("AI8.4 The Opening Contests The Centre: At The Start Medium Sends ONE Ant And Hard TWO To The Pile In The Centre Of TREASURE, The Rest Harvest By Value Per Trip, Easy None; After The Opening (1200 Ticks) A Pile Of That Class Gets The Ants By Value Per Trip Again; Where Nothing Is Contested The Opening Changes Nothing")
    {
        sim::SimulationEngine probe;
        start_match(probe, "TREASURE", 7, 0x0F);
        const MapInfo pmap(probe);
        size_t centre = 0;
        for (const PileInfo& p : pmap.piles()) {
            if (p.anchor.x == 30 && p.anchor.y == 29) centre = p.index;
        }
        const size_t piles = probe.grid().food_objects().size();
        const auto ants_at_centre = [&](const Rig& rig, const sim::SimulationEngine& sim, uint64_t until) {
            size_t n = 0;
            for (const auto& e : rig.proposed) {
                if (e.first > until || e.second.type != CommandType::GroupMove) continue;
                const sim::TileCoord t = tc(e.second.tile_x, e.second.tile_y);
                if (sim.grid().has_powerup_at(t)) continue;
                if (pile_of_tile(rig.map(), piles, t) == static_cast<int>(centre)) n += e.second.ants.size();
            }
            return n;
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const size_t want = level == Level::Easy ? 0u : level == Level::Medium ? 1u : 2u;
            ASSERT_EQ(plan_for(level).contest_opening_ants, want);
            for (const uint8_t seat : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {                                 // (at seat 2 the centre is the nearest pile: the plain order goes there anyway)
                sim::SimulationEngine sim;
                start_match(sim, "TREASURE", 7, 0x0F);
                Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(60);
                if (seat == 0) ASSERT_EQ(ants_at_centre(rig, sim, 60), want);                                  // (from the other hills the plain order sends ants to the centre too, once the nearer piles are full)
                else ASSERT_TRUE(ants_at_centre(rig, sim, 60) >= want);
                if (want > 0) {
                    const std::vector<int> targets = harvest_targets(rig, sim, seat, piles);
                    ASSERT_TRUE(!targets.empty());
                    ASSERT_EQ(static_cast<size_t>(targets.front()), centre);                                    // and the first order of the harvest is theirs
                }
            }
        }
        // after the opening the cap is gone: a pile of that class gets its ants by value per trip like any other (here: four newborn ants at tick 1300)
        for (const bool late : {false, true}) {
            sim::SimulationEngine sim;
            empty_field(sim, 97);
            const int32_t middle = add_pile(sim, 27, 27, 40, 25);
            add_pile(sim, 12, 12, 40, 10);                                                                    // a near pile of little value
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
            for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
            sim.get_unit(ants_of(sim, 0)[0]).hp = 10;
            LevelPlan hard = plan_for(Level::Hard);
            hard.contest_opening_min_ants = 0;                                                                // (a world of four ants: the minimum of six has its own test, AI8.5)
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(hard), 4, 4);
            if (late) {
                for (int i = 0; i < 1300; ++i) rig.tick();
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
            }
            const size_t before = rig.proposed.size();
            rig.run(60);
            size_t n = 0;
            for (size_t i = before; i < rig.proposed.size(); ++i) {
                const auto& e = rig.proposed[i];
                if (e.second.type != CommandType::GroupMove) continue;
                if (pile_of_tile(rig.map(), 2, tc(e.second.tile_x, e.second.tile_y)) == middle) n += e.second.ants.size();
            }
            if (late) ASSERT_TRUE(n >= 3);                                                                    // by value per trip the middle pile (25 a unit) beats the near one (10 a unit)
            else ASSERT_EQ(n, 2u);                                                                            // in the opening: two ants only, the other two take the near pile
        }
        // where nothing is contested (one pile next to the hill, the enemies far away) the opening changes nothing: the same orders with and without it
        {
            std::vector<std::vector<std::pair<uint64_t, Command>>> runs;
            for (const uint32_t k : {0u, 2u}) {
                sim::SimulationEngine sim;
                empty_field(sim, 98);
                add_pile(sim, 12, 12, 40, 25);
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
                for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
                LevelPlan plan = plan_for(Level::Hard);
                plan.contest_opening_ants = k;
                plan.contest_opening_min_ants = 0;                                                            // (four ants: the minimum is off, so that the opening itself is what differs)
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(120);
                runs.push_back(rig.proposed);
            }
            ASSERT_TRUE(runs[0] == runs[1]);
            ASSERT_TRUE(!runs[0].empty());
        }
    } TEST_END();

    TEST_CASE("AI8.5 The Opening's Contest Needs Six Ants (TINY Starts With 3, SMALL With 4): A Team That Cannot Spare An Ant Harvests First, At Every Level And Seat (The First Orders Are Those Of A Bot Without The Contest), While The Same Plan Without The Minimum Sends Ants Across TINY; TREASURE (6 Ants) Keeps The Contest; In A Hand-Made World Five Ants Spare None And Six Spare The Level's Number") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_EQ(plan_for(level).contest_opening_min_ants, 6u);
        const auto first_orders = [&](const char* map, uint8_t seat, Level level, uint32_t k, uint32_t min_ants) {
            sim::SimulationEngine sim;
            start_match(sim, map, 7, 0x0F);
            LevelPlan plan = plan_for(level);
            plan.contest_opening_ants = k;
            plan.contest_opening_min_ants = min_ants;
            Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            return rig.proposed;
        };
        const auto starting_ants = [&](const char* map, uint8_t seat) {
            sim::SimulationEngine sim;
            start_match(sim, map, 7, 0x0F);
            return ants_of(sim, seat).size();
        };
        ASSERT_EQ(starting_ants("TINY", 0), 3u);
        ASSERT_EQ(starting_ants("SMALL", 0), 4u);
        ASSERT_TRUE(starting_ants("TREASURE", 0) >= 6u);
        for (const char* map : {"TINY", "SMALL"}) {
            for (const Level level : {Level::Medium, Level::Hard}) {
                bool crossing = false;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    const auto shipped = first_orders(map, seat, level, plan_for(level).contest_opening_ants, 6);        // the plan as it is shipped
                    const auto without = first_orders(map, seat, level, 0, 6);                                           // no contest at all
                    ASSERT_TRUE(!shipped.empty() && shipped == without);
                    crossing = crossing || first_orders(map, seat, level, plan_for(level).contest_opening_ants, 0) != without;      // the contest of the plan with no minimum
                }
                if (std::string(map) == "TINY") ASSERT_TRUE(crossing);                                                   // (on TINY the contested pile is far: the unrestricted contest sends ants there)
            }
        }
        {   // TREASURE: six ants, the contest stays (AI8.4 pins who goes where)
            const auto shipped = first_orders("TREASURE", 0, Level::Hard, 2, 6);
            const auto without = first_orders("TREASURE", 0, Level::Hard, 0, 6);
            ASSERT_TRUE(shipped != without);
        }
        // a hand-made world: the middle pile (27, 27) is contested by every enemy, a near pile of little value; five ants spare none (the same orders as with no contest), six spare two (Hard)
        for (const size_t n : {size_t{5}, size_t{6}}) {
            std::array<size_t, 2> in_middle{};
            for (const int variant : {0, 1}) {                                                                   // 0: Hard's plan as it is, 1: Hard's plan without the contest
                sim::SimulationEngine sim;
                empty_field(sim, 97);
                const int32_t middle = add_pile(sim, 27, 27, 40, 25);
                add_pile(sim, 12, 12, 40, 10);
                for (size_t i = 0; i < n; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{static_cast<int32_t>(8 + i), 9});
                for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
                LevelPlan plan = plan_for(Level::Hard);
                if (variant == 1) plan.contest_opening_ants = 0;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                for (const auto& e : rig.proposed) {
                    if (e.second.type == CommandType::GroupMove && pile_of_tile(rig.map(), 2, tc(e.second.tile_x, e.second.tile_y)) == middle) in_middle[static_cast<size_t>(variant)] += e.second.ants.size();
                }
            }
            if (n == 5) ASSERT_EQ(in_middle[0], in_middle[1]);                                                   // too few ants: the contest is off
            else ASSERT_TRUE(in_middle[0] == 2u && in_middle[1] > 2u);                                           // six ants: two go first, the value order would send more
        }
    } TEST_END();

    TEST_CASE("AI8.6 Help For The Ally (The Defensive Style's Flag): A Blow On An Ant Of The Ally Is Answered By The Own Ants Within 10 Tiles Of It And By No Ant Further Away, Nothing Moves With The Flag Off, And The Ally's Ants Are Never The Target; An Enemy Bomb Near The Ally's Hill Is Defused By An Own Bomber With The Flag On And Left Alone With It Off")
    {
        const auto attack_ants = [](const Rig& rig) {
            std::set<uint32_t> out;
            for (const auto& e : rig.sent) {
                if (e.second.type == CommandType::GroupAttack) out.insert(e.second.ants.begin(), e.second.ants.end());
            }
            return out;
        };
        for (const Level level : {Level::Medium, Level::Hard}) {
            for (const bool help : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 93);
                sim.form_alliance(0, 1);
                const uint32_t victim = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 28});                 // an ant of the ally ...
                const uint32_t attacker = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{30, 29});               // ... that an enemy keeps hitting
                const std::vector<uint32_t> near = {sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 28}), sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 32})};
                const std::vector<uint32_t> far = {sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 8}), sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8})};
                LevelPlan plan = plan_for(level);
                plan.ally_help = help;
                plan.gate = false;
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                for (int t = 0; t < 200; ++t) {
                    keep_attacking(sim, 2, attacker, victim);
                    rig.tick();
                }
                const std::set<uint32_t> went = attack_ants(rig);
                if (!help) {
                    ASSERT_TRUE(went.empty());                                                                    // a blow on the ally is nothing to this plan
                } else {
                    ASSERT_FALSE(went.empty());
                    for (const uint32_t a : went) ASSERT_TRUE(a == near[0] || a == near[1]);                      // the ants within ten tiles, not the ones at the hill
                    for (const uint32_t a : far) ASSERT_TRUE(went.count(a) == 0);
                    ASSERT_TRUE(went.count(victim) == 0);
                    ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
                }
            }
        }
        {   // nobody near the blow (every own ant is more than ten tiles from it): no fight is started at all
            sim::SimulationEngine sim;
            empty_field(sim, 93);
            sim.form_alliance(0, 1);
            const uint32_t victim = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 28});
            const uint32_t attacker = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{30, 29});
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + i, 8});
            LevelPlan plan = plan_for(Level::Hard);
            plan.ally_help = true;
            plan.gate = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            for (int t = 0; t < 200; ++t) {
                keep_attacking(sim, 2, attacker, victim);
                rig.tick();
            }
            ASSERT_EQ(rig.as<StandardBot>().fight().fights_started(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
        {   // the counters reach the ally's hill: an enemy bomb 4 tiles from it is defused by an own Bomber (nothing is hurt), but only with the flag
            for (const bool help : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 94);
                sim.form_alliance(0, 1);
                sim.grid_mut().place_bomb(53, 8, 2);                                                               // hill 1 is at (50, 4)
                const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{44, 10});
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
                LevelPlan plan = plan_for(Level::Hard);
                plan.ally_help = help;
                plan.gate = false;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(60);
                bool ordered = false;
                for (const auto& e : rig.proposed) {
                    ordered = ordered || (e.second.type == CommandType::GroupSpecial && e.second.ants.size() == 1 && e.second.ants[0] == bomber && e.second.tile_x == 53 && e.second.tile_y == 8);
                }
                ASSERT_EQ(ordered, help);
            }
        }
    } TEST_END();

    TEST_CASE("AI8.7 The Accept Rule Is One Pure Function With A Reason (team_up_answer): Every Case Of AI8.1 Gives Its Answer, Where Two Reasons Apply The Bot's Own Team Comes First, Then The Inviter, Then The Number Of Live Teams; An Inviter That Is Not A Team Of The Match, Has Dropped Out Or Is The Bot Itself Is Gone; The Standard Bot Answers By It; The Texts That Tell A Player Why A Bot Declined Are Short And Say The Reason")
    {
        struct Case {
            uint8_t alive_mask;
            uint8_t dropped_mask;
            bool bot_has_ally;
            bool inviter_has_ally;
            TeamUpAnswer answer;
        };
        // the cases of AI8.1 (bot = seat 0, inviter = seat 1), then the order of the reasons
        const Case cases[] = {
            {0x0F, 0x00, false, false, TeamUpAnswer::Accept},                  // four live teams
            {0x07, 0x08, false, false, TeamUpAnswer::Accept},                  // team 3 dropped out: three live teams
            {0x0B, 0x00, false, false, TeamUpAnswer::Accept},                  // team 2 has no ant in sight but is not gone: three live teams (0, 1, 3)
            {0x03, 0x0C, false, false, TeamUpAnswer::TwoTeamsLeft},            // two live teams: the alliance would unite them all
            {0x03, 0x00, false, false, TeamUpAnswer::TwoTeamsLeft},            // (teams 2 and 3 have no ants in sight: the same)
            {0x0F, 0x00, true, false, TeamUpAnswer::BotHasTeammate},           // the bot has an ally: accepting would break it
            {0x0F, 0x00, false, true, TeamUpAnswer::InviterHasTeammate},       // the inviter has an ally
            {0x0F, 0x00, true, true, TeamUpAnswer::BotHasTeammate},            // both: the bot's own team is the reason
            {0x03, 0x00, true, false, TeamUpAnswer::BotHasTeammate},           // the bot has an ally and two teams are live: the bot's own team, not the count
            {0x03, 0x00, false, true, TeamUpAnswer::InviterHasTeammate},       // the inviter has an ally and two teams are live: the inviter, not the count
            {0x07, 0x04, false, false, TeamUpAnswer::TwoTeamsLeft},            // team 2 dropped out (its ants die at once, nobody sees them): two live teams (0 and 1)
            {0x06, 0x00, false, false, TeamUpAnswer::TwoTeamsLeft},            // the bot's own team has no ant in sight: it is not live either (1 and 2 are)
        };
        for (const Case& k : cases) {
            sim::SimulationEngine sim;
            world_of(sim, k.alive_mask);
            if (k.dropped_mask & 8u) sim.drop_player(3);
            if (k.dropped_mask & 4u) sim.drop_player(2);
            if (k.bot_has_ally) sim.form_alliance(0, 2);
            if (k.inviter_has_ally) sim.form_alliance(1, 3);
            const BotView view = BotView::build(sim, 0);
            ASSERT_TRUE(team_up_answer(view, 1) == k.answer);
            ASSERT_EQ(StandardBot::accepts_invitation(view, 1), k.answer == TeamUpAnswer::Accept);       // the bot answers by the function: the same decision
        }
        {   // an inviter that is gone: dropped out, not in the roster, no seat at all, or the bot itself (every one of them is no one to team up with)
            sim::SimulationEngine sim;
            world_of(sim, 0x0F);
            sim.drop_player(1);
            const BotView view = BotView::build(sim, 0);
            ASSERT_TRUE(team_up_answer(view, 1) == TeamUpAnswer::InviterGone);
            ASSERT_TRUE(team_up_answer(view, 0) == TeamUpAnswer::InviterGone);                              // the bot itself
            ASSERT_TRUE(team_up_answer(view, 4) == TeamUpAnswer::InviterGone);
            ASSERT_TRUE(team_up_answer(view, 255) == TeamUpAnswer::InviterGone);
            ASSERT_TRUE(!StandardBot::accepts_invitation(view, 1) && !StandardBot::accepts_invitation(view, 0) && !StandardBot::accepts_invitation(view, 255));
        }
        {   // a team that is not in the match (a three-seat match on TREASURE: seat 3 has no player)
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x07);
            const BotView view = BotView::build(sim, 0);
            ASSERT_TRUE(team_up_answer(view, 3) == TeamUpAnswer::InviterGone);
            ASSERT_TRUE(team_up_answer(view, 1) == TeamUpAnswer::Accept);                                    // three teams play, seat 1 is one of them
        }
        {   // the same match with only two seats: the other seat's invitation is the one that would end the match
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x03);
            const BotView view = BotView::build(sim, 0);
            ASSERT_TRUE(team_up_answer(view, 1) == TeamUpAnswer::TwoTeamsLeft);
        }
        {   // the texts: the reason in a few words (the chat log's body holds 100 characters), nothing for Accept and for a gone inviter
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::TwoTeamsLeft, "Bot (Easy)"), std::string("Bots team up only while three or more teams play."));
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::InviterHasTeammate, "Bot (Easy)"), std::string("You already have a teammate."));
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::BotHasTeammate, "Bot (Hard)"), std::string("Bot (Hard) already has a teammate."));
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::BotHasTeammate, "Maple"), std::string("Maple already has a teammate."));
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::BotHasTeammate, ""), std::string("This bot already has a teammate."));
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::Accept, "Bot (Easy)"), std::string());
            ASSERT_EQ(team_up_decline_text(TeamUpAnswer::InviterGone, "Bot (Easy)"), std::string());
            ASSERT_EQ(std::string(kWorkerNeverTeamsUpText), std::string("This bot never teams up."));
            const std::string cut = team_up_decline_text(TeamUpAnswer::BotHasTeammate, std::string(80, 'x'));
            ASSERT_EQ(cut, std::string(32, 'x') + " already has a teammate.");                              // a long name is cut at 32 characters
            for (const TeamUpAnswer a : {TeamUpAnswer::TwoTeamsLeft, TeamUpAnswer::InviterHasTeammate, TeamUpAnswer::BotHasTeammate}) {
                ASSERT_TRUE(team_up_decline_text(a, std::string(80, 'y')).size() <= 100);
            }
        }
    } TEST_END();
}
