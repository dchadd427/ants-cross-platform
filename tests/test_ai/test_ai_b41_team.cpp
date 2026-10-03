// Team play of the standard bot (B4-1, AI8.x): the accept rule for an invitation to team up, the help for the ally, the economy with an ally. Hand-made worlds (b41_helpers.hpp) and
// TREASURE, quick enough for suite 2.20.
//
//   AI8.2   the contest-aware pile order on TREASURE: the first harvest orders of every seat go to the centre, at every level; the worker's order is unchanged
//   AI8.3   the classes of the order in a hand-made world (the centre, a side that one enemy shares, the safe piles, the ally's side last), recomputed when an alliance forms; within a
//           class by value (Medium, Hard) or by distance (Easy), the richer pile first
//   AI8.1   the accept rule: accepted with four or three live teams, denied when the alliance would unite all live teams, when the bot or the inviter already has an ally; the worker
//           bot (the yardstick) still denies; the standard bot never invites, withdraws or breaks
#include "ai_test.hpp"
#include "b41_helpers.hpp"

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

// A pile of `units` units of `value` points with the five stages of a cracker box
int32_t add_pile(sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value) {
    const uint16_t q = static_cast<uint16_t>(std::max<int>(1, units / 4));
    return place_pile(sim, col, row, units, value, {{units, 369}, {static_cast<uint16_t>(q * 3), 370}, {static_cast<uint16_t>(q * 2), 371}, {q, 372}, {0, kPileGone}});
}

// The pile (by the table's index) that a tile belongs to, as the analysis of the map sees the cells; -1 when none
int pile_of_tile(const MapInfo& map, size_t piles, sim::TileCoord tile) {
    for (size_t i = 0; i < piles; ++i) {
        const PileInfo* info = map.pile(static_cast<uint32_t>(i));
        if (info == nullptr) continue;
        for (const sim::TileCoord& c : info->cells) {
            if (c == tile) return static_cast<int>(i);
        }
    }
    return -1;
}

// The piles that the harvest orders of a rig name, in the order they were proposed (a move onto a power-up tile is a pick-up, not a harvest order)
std::vector<int> harvest_targets(const Rig& rig, const sim::SimulationEngine& sim, uint8_t seat, size_t piles) {
    std::vector<int> out;
    for (const auto& e : rig.proposed) {
        if (e.second.type != CommandType::GroupMove || e.second.issuer != seat) {
            if (e.second.type != CommandType::GroupMove) continue;
        }
        const int pile = pile_of_tile(rig.map(), piles, tc(e.second.tile_x, e.second.tile_y));
        if (pile < 0 || sim.grid().has_powerup_at(tc(e.second.tile_x, e.second.tile_y))) continue;
        if (std::find(out.begin(), out.end(), pile) == out.end()) out.push_back(pile);
    }
    return out;
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
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(level), 4, 4);
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
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(Level::Hard), 4, 4);
            rig.run(2);
            sim.apply_command(alliance_command(CommandType::AllianceInvite, 1, 0));
            rig.run(3000);
            ASSERT_EQ(sim.get_ally_id(0), 1);
            ASSERT_EQ(rig.proposed_count(CommandType::AllianceBreak) + rig.proposed_count(CommandType::AllianceInvite) + rig.proposed_count(CommandType::AllianceWithdraw), 0u);
        }
    } TEST_END();

    TEST_CASE("AI8.2 The Contest-Aware Pile Order On TREASURE (The Owner's Playbook: The Centre First, Then The Contested Sides): The First Harvest Orders Of Every Seat At Every Level Go To The Pile In The Centre (Contested By All Three Enemies); Without The Order They Go To The Near Piles; The Worker's Order Is Unchanged")
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
        // an alliance that forms while the match runs moves the side of the new ally to the end (the classes are made again at every look)
        {
            sim::SimulationEngine sim;
            const auto id = build(sim, false);
            LevelPlan plan = plan_for(Level::Medium);
            plan.contest_aware = true;
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
            plan.rank_by_remaining = rank_remaining;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);
            const std::vector<int> targets = harvest_targets(rig, sim, 0, 2);
            ASSERT_TRUE(!targets.empty());
            ASSERT_EQ(targets.front(), rank_remaining ? far_rich : near_small);
        }
    } TEST_END();
}
