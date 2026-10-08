// AI23: the flowers (docs/BOTS.md, "The flowers"): what the analysis and the view show of the flower droppers, whose side a drop is on, what the arena counts of them, and what a Hard bot
// does with a drop that lands. The maps are the shipped ones: a flower cannot be made in a hand-made world. Medium and Easy play none of it.
//
//   AI23.1 - 2b             the analysis and the view: the flowers of every map, the droplet's kind and age, every kind that lands
//   AI23.3, 12, 13          whose drop it is, which drops a bot wants, a power-up of the start on another side
//   AI23.4, 4b, 14          what the arena counts: the landings, the kinds, the power-ups taken and where
//   AI23.5, 6, 23           the Fire at home and the contest; a Fire that no hill walks to is wanted by nobody
//   AI23.7                  the plans: Hard has the rules, Medium and Easy none
//   AI23.8                  the memory of a flower: the cycle, the next landing, the chance of a kind
//   AI23.9, 15              the recall and the recovery of a pick-up trip
//   AI23.10, 11, 16 - 21, 24 - 31   the watcher: when it is sent, its place, its choice, its orders, when it goes home and when it stays, the step off the drop tile, a blow, a ring of fire,
//                           the trips under way, a landing that does not come, an ant that another task holds
//   AI23.22                 where no flower is within a hill's reach the rules change nothing
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// Medium plays none of the flower rules (they lost 20 points on SMALL there: docs/BOTS.md, "The flowers"); the tests that ask what the rules do play a Medium bot with them switched on
LevelPlan medium_with_rules() {
    LevelPlan p = plan_for(Level::Medium);
    p.flower_sides = p.flower_fire = p.flower_recover = p.flower_recall = true;
    return p;
}

// The plan that a bot of (level, style) plays at `seat` in the match of `match_seed` (what the controller hands it)
LevelPlan plan_of(Level level, Style style, uint32_t match_seed, uint8_t seat) {
    BotRng seat_rng(seat_seed(match_seed, seat, "standard"));
    StandardBot bot(level, style);
    bot.start(BotContext{seat, profile_for(level), seat_rng.next(), nullptr});
    return bot.tactics().plan;
}

const PowerUpView* powerup_on(const BotView& view, TileCoord tile) {
    for (const PowerUpView& p : view.powerups()) {
        if (p.tile == tile) return &p;
    }
    return nullptr;
}

// The landings of a run: the tick at which a droplet stopped falling and the kind that lies on its tile then
struct Landing {
    uint64_t tick;
    size_t flower;
    sim::AntType kind;
};

class LandingWatch {
public:
    void scan(const sim::SimulationEngine& sim, const MapInfo& map) {
        const BotView view = BotView::build(sim, 0, &map);
        falling_.resize(view.flowers().size(), false);
        for (size_t k = 0; k < view.flowers().size(); ++k) {
            const FlowerView& f = view.flowers()[k];
            if (!f.falling && falling_[k]) {
                const PowerUpView* p = powerup_on(view, f.drop);
                if (p != nullptr) landings.push_back(Landing{sim.current_tick(), k, p->kind});
            }
            falling_[k] = f.falling;
        }
    }
    std::vector<Landing> landings;

private:
    std::vector<bool> falling_;
};

struct MapFlowers {
    const char* name;
    size_t count;
};

// A match on SMALL (seed 2, 1,000 ticks at most) with Hard Raider bots at the seats of `bot_seats` and idle bots elsewhere
ArenaResult small_match(uint8_t bot_seats, uint32_t max_ticks) {
    ArenaSpec spec;
    spec.level = &level_of("SMALL");
    spec.seed = 2;
    spec.max_ticks = max_ticks;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        BotSpec b;
        b.seat = seat;
        b.level = Level::Hard;
        if (((bot_seats >> seat) & 1u) != 0) {
            b.kind = "standard";
            b.style = Style::Raider;
        } else {
            b.kind = "idle";
        }
        spec.bots.push_back(b);
    }
    return play_match(spec);
}

constexpr size_t kFire = static_cast<size_t>(sim::AntType::Fire);

// A Hard Raider (seat 1: the west hill, whose own flower is the west one, flower 1) plays SMALL for `ticks` ticks: `prepare` changes the world first, `edit` the plan, and `each` is called
// after every tick with the rig and the engine
template <class Prepare, class Edit, class Each>
void raider_run(uint32_t seed, uint64_t ticks, Prepare prepare, Edit edit, Each each) {
    sim::SimulationEngine sim;
    start_match(sim, "SMALL", seed, 0x0F);
    prepare(sim);
    LevelPlan plan = plan_of(Level::Hard, Style::Raider, seed, 1);
    edit(plan);
    Rig rig(sim, 1, Level::Hard, std::make_unique<StandardBot>(plan));
    for (uint64_t t = 0; t < ticks; ++t) {
        rig.tick();
        each(rig, sim);
    }
}

void keep_world(sim::SimulationEngine&) {}
void keep_plan(LevelPlan&) {}

const PowerUpTask& powerups_of(Rig& rig) { return rig.as<StandardBot>().powerups(); }

int32_t distance_to(const sim::AntSnapshot* a, TileCoord t) { return a == nullptr ? -1 : std::max(std::abs(a->tile_x - t.x), std::abs(a->tile_y - t.y)); }

// The moves of the single ant `ant` that the bot proposed at a look from `since` on: (tick of the look, index in rig.proposed, tile)
struct Move {
    uint64_t tick;
    size_t index;
    TileCoord tile;
};
std::vector<Move> moves_of(const Rig& rig, uint32_t ant, uint64_t since) {
    std::vector<Move> out;
    for (size_t i = 0; i < rig.proposed.size(); ++i) {
        const auto& e = rig.proposed[i];
        if (e.first >= since && e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && e.second.ants[0] == ant) out.push_back(Move{e.first, i, tc(e.second.tile_x, e.second.tile_y)});
    }
    return out;
}

}  // namespace

void run_flower_tests() {
    TEST_CASE("AI23.1 The Analysis And The View List The Flowers: SMALL 2, MEDIUM 1, GAUNTLET 1, ISLANDS 2, TINY And TREASURE None; The Drop Tile Is The Plant's Tile One Row Down; An Analysis Of A Grid Alone Has None")
    {
        for (const MapFlowers& m : {MapFlowers{"TINY", 0}, MapFlowers{"SMALL", 2}, MapFlowers{"MEDIUM", 1}, MapFlowers{"GAUNTLET", 1}, MapFlowers{"TREASURE", 0}, MapFlowers{"ISLANDS", 2}}) {
            sim::SimulationEngine sim;
            start_match(sim, m.name, 1, 0x0F);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            ASSERT_EQ(map.flowers().size(), m.count);
            ASSERT_EQ(view.flowers().size(), m.count);
            for (size_t k = 0; k < m.count; ++k) {
                const FlowerInfo& f = map.flowers()[k];
                ASSERT_TRUE(f.drop == tc(f.plant.x, f.plant.y + 1));
                ASSERT_TRUE(view.flowers()[k].plant == f.plant && view.flowers()[k].drop == f.drop);
                ASSERT_TRUE(map.flower_at(f.drop) == &f);
                ASSERT_TRUE(map.flower_at(f.plant) == nullptr);
                ASSERT_TRUE(view.flower_at(f.drop) != nullptr && view.flower_at(f.drop)->plant == f.plant);
                ASSERT_FALSE(view.flowers()[k].falling);
            }
            const BotView copy = view;                                                  // (a copy of the view keeps the flowers)
            ASSERT_EQ(copy.flowers().size(), m.count);
            for (size_t k = 0; k < m.count; ++k) ASSERT_TRUE(copy.flower_at(view.flowers()[k].drop) != nullptr);
            const MapInfo grid_only(sim.grid());
            ASSERT_TRUE(grid_only.flowers().empty());
        }
        {   // the tiles of the maps
            sim::SimulationEngine small;
            start_match(small, "SMALL", 1, 0x0F);
            const MapInfo small_map(small);
            ASSERT_TRUE(small_map.flower_at(tc(37, 20)) != nullptr && small_map.flower_at(tc(2, 20)) != nullptr);
            sim::SimulationEngine medium;
            start_match(medium, "MEDIUM", 1, 0x0F);
            ASSERT_TRUE(MapInfo(medium).flower_at(tc(29, 29)) != nullptr);
            sim::SimulationEngine gauntlet;
            start_match(gauntlet, "GAUNTLET", 1, 0x0F);
            ASSERT_TRUE(MapInfo(gauntlet).flower_at(tc(3, 6)) != nullptr);
        }
    } TEST_END();

    TEST_CASE("AI23.2 The View Shows The Droplet As The Screen Draws It: About 16 Ticks With Its Kind, No Power-Up On The Tile Before The First Landing, The Power-Up Of That Kind When It Stops; SMALL's Cycle Is 300 Ticks")
    {
        sim::SimulationEngine sim;
        start_match(sim, "SMALL", 1, 0x0F);
        const MapInfo map(sim);
        std::array<uint64_t, 2> began{0, 0};
        std::array<uint32_t, 2> ticks_falling{0, 0};
        std::array<bool, 2> was_falling{false, false};
        std::array<sim::AntType, 2> kind_seen{sim::AntType::Bomber, sim::AntType::Bomber};
        std::vector<std::pair<uint64_t, uint64_t>> cycle;                            // (tick the droplet began to fall, tick it landed) of flower 0
        bool first_landing_seen = false;
        for (uint64_t i = 0; i < 640; ++i) {
            tick_all(sim, 1);
            const BotView view = BotView::build(sim, 0, &map);
            ASSERT_EQ(view.flowers().size(), 2u);
            for (size_t k = 0; k < 2; ++k) {
                const FlowerView& f = view.flowers()[k];
                const PowerUpView* p = powerup_on(view, f.drop);
                if (f.falling) {
                    if (!was_falling[k]) {
                        began[k] = sim.current_tick();
                        ticks_falling[k] = 0;
                        kind_seen[k] = f.kind;
                    }
                    ASSERT_EQ(f.age, ticks_falling[k]);                                // age 0, 1, ..., 15
                    ASSERT_TRUE(f.kind == kind_seen[k]);                               // the picture does not change while it falls
                    ++ticks_falling[k];
                    if (!first_landing_seen) ASSERT_TRUE(p == nullptr);                // nothing lies there before the first landing
                } else {
                    ASSERT_EQ(f.age, 0u);
                    if (was_falling[k]) {                                              // the tick it stopped: the power-up lies there
                        ASSERT_EQ(ticks_falling[k], 16u);
                        ASSERT_TRUE(p != nullptr && p->kind == kind_seen[k]);
                        first_landing_seen = true;
                        if (k == 0) cycle.emplace_back(began[k], sim.current_tick());
                    }
                }
                was_falling[k] = f.falling;
            }
        }
        ASSERT_EQ(cycle.size(), 2u);                                                   // 317 and 617 in 640 ticks
        ASSERT_EQ(cycle[0].first, 301u);
        ASSERT_EQ(cycle[0].second, 317u);
        ASSERT_EQ(cycle[1].second - cycle[0].second, 300u);
    } TEST_END();

    TEST_CASE("AI23.2b The Droplet In The View Is The Power-Up That Lands, For Every Kind That The Maps Drop (SMALL: Bomber, Fire And Swimmer; MEDIUM: Bomber, Fire, Thief And Combat; GAUNTLET: Bomber, Fire, Combat And Swimmer; ISLANDS: Bomber, Fire, Thief And Swimmer): 3,700 Ticks Of An Idle Match Each, Every Kind Lands Somewhere")
    {
        std::array<uint32_t, 6> landed{};
        for (const char* name : {"SMALL", "MEDIUM", "GAUNTLET", "ISLANDS"}) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const MapInfo map(sim);
            std::vector<bool> was_falling(map.flowers().size(), false);
            std::vector<sim::AntType> drawn(map.flowers().size(), sim::AntType::Worker);
            for (uint64_t i = 0; i < 3700; ++i) {
                tick_all(sim, 1);
                const BotView view = BotView::build(sim, 0, &map);
                for (size_t k = 0; k < map.flowers().size(); ++k) {
                    const FlowerView& f = view.flowers()[k];
                    if (f.falling) {
                        if (!was_falling[k]) drawn[k] = f.kind;
                        ASSERT_TRUE(f.kind == drawn[k]);                                // (the picture does not change while it falls)
                    } else if (was_falling[k]) {
                        const PowerUpView* p = powerup_on(view, f.drop);
                        ASSERT_TRUE(p != nullptr && p->kind == drawn[k]);               // the kind that was drawn is the kind that lies there
                        ++landed[static_cast<size_t>(p->kind)];
                    }
                    was_falling[k] = f.falling;
                }
            }
        }
        for (size_t kind = 1; kind < landed.size(); ++kind) ASSERT_TRUE(landed[kind] > 0);
    } TEST_END();

    TEST_CASE("AI23.3 A Drop Has A Side: The Hill That Walks To The Drop Tile At The Least Cost (SMALL West 1 And East 2, MEDIUM 3, GAUNTLET 3, ISLANDS None, No Hill Reaches It); A Team Out Of The Match Is Left Out; A Tile That Is No Drop Tile Has None")
    {
        const auto side = [](const char* name, TileCoord tile, uint8_t present = 0x0F) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const MapInfo map(sim);
            return drop_side(map, tile, present);
        };
        ASSERT_EQ(side("SMALL", tc(2, 20)), 1);
        ASSERT_EQ(side("SMALL", tc(37, 20)), 2);
        ASSERT_EQ(side("MEDIUM", tc(29, 29)), 3);
        ASSERT_EQ(side("GAUNTLET", tc(3, 6)), 3);
        ASSERT_EQ(side("SMALL", tc(2, 20), 0x0D), 3);                                   // seat 1 is not in the match: the next hill is the nearest
        ASSERT_EQ(side("SMALL", tc(2, 20), 0x02), 1);
        ASSERT_EQ(side("SMALL", tc(2, 20), 0x00), -1);
        ASSERT_EQ(side("SMALL", tc(2, 19)), -1);                                        // the plant's own tile is no drop tile
        ASSERT_EQ(side("TINY", tc(2, 20)), -1);
        {   // the two flowers of ISLANDS are out of every hill's reach
            sim::SimulationEngine sim;
            start_match(sim, "ISLANDS", 1, 0x0F);
            const MapInfo map(sim);
            ASSERT_EQ(map.flowers().size(), 2u);
            for (const FlowerInfo& f : map.flowers()) {
                ASSERT_EQ(drop_side(map, f.drop, 0x0F), -1);
                for (uint8_t t = 0; t < 4; ++t) ASSERT_FALSE(f.approach[t].reachable());
            }
        }
        {   // a drop is on no list of the power-ups of the start
            sim::SimulationEngine sim;
            start_match(sim, "SMALL", 1, 0x0F);
            const MapInfo map(sim);
            ASSERT_EQ(power_up_side(map, tc(2, 20), 0x0F), -1);
        }
    } TEST_END();

    TEST_CASE("AI23.4 The Arena Counts The Landings (Idle Matches: SMALL 62, MEDIUM 66, GAUNTLET 19, ISLANDS 22, TINY And TREASURE 0; The Kinds Add Up) And What A Seat's Ants Take: The Fire Drop A Hard Raider Takes At SMALL Is One Taken At A Flower")
    {
        struct Expect {
            const char* map;
            uint32_t landings;
        };
        for (const Expect& e : {Expect{"TINY", 0}, Expect{"SMALL", 62}, Expect{"MEDIUM", 66}, Expect{"GAUNTLET", 19}, Expect{"TREASURE", 0}, Expect{"ISLANDS", 22}}) {
            ArenaSpec spec;
            spec.level = &level_of(e.map);
            spec.seed = 1;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "idle";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_EQ(r.landings, e.landings);
            uint32_t sum = 0;
            for (size_t k = 1; k < r.landed.size(); ++k) sum += r.landed[k];
            ASSERT_EQ(sum, e.landings);
            ASSERT_EQ(r.landed[0], 0u);
            for (const ArenaSeatResult& s : r.seats) {
                ASSERT_EQ(s.took_at_flowers, 0u);
                for (const uint32_t n : s.took) ASSERT_EQ(n, 0u);
            }
        }
        {   // seed 2 on SMALL: the Raider at seat 1 (the nearest hill to the west flower) takes the Fire of 617, and nothing else, in 1,000 ticks
            const ArenaResult r = small_match(0x02, 1000);
            ASSERT_EQ(r.landings, 6u);                                                  // 317, 617 and 917 at both flowers
            ASSERT_EQ(r.seats[1].took[kFire], 1u);
            ASSERT_EQ(r.seats[1].took_at_flowers, 1u);
            for (const size_t seat : {0u, 2u, 3u}) ASSERT_EQ(r.seats[seat].took_at_flowers, 0u);
        }
        for (const uint32_t ticks : {316u, 317u}) ASSERT_EQ(small_match(0x02, ticks).landings, ticks == 317u ? 2u : 0u);          // (the tally looks every 4 ticks and once more when the match ends: 317 is not a look)
    } TEST_END();

    TEST_CASE("AI23.4b The Arena's Count Of The Kinds That Land Is The View's, Kind By Kind (3,600 Ticks Of An Idle Match On Every Map With Flowers)")
    {
        for (const char* name : {"SMALL", "MEDIUM", "GAUNTLET", "ISLANDS"}) {
            ArenaSpec spec;
            spec.level = &level_of(name);
            spec.seed = 1;
            spec.max_ticks = 3600;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "idle";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_EQ(r.ticks, 3600u);
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const MapInfo map(sim);
            LandingWatch watch;
            for (uint64_t i = 0; i < r.ticks; ++i) {
                tick_all(sim, 1);
                watch.scan(sim, map);
            }
            std::array<uint32_t, 6> seen{};
            for (const Landing& l : watch.landings) ++seen[static_cast<size_t>(l.kind)];
            ASSERT_EQ(r.landings, static_cast<uint32_t>(watch.landings.size()));
            for (size_t kind = 1; kind < seen.size(); ++kind) ASSERT_EQ(r.landed[kind], seen[kind]);
        }
    } TEST_END();

    TEST_CASE("AI23.5 Sides And The Fire At Home: A Hard Raider At SMALL (Seed 2, Seat 1) Wants The Fire That Lands (617) And Takes It Within 150 Ticks, Wants No Bomber (317 And 917: Its Style Wants None); Without The Rules It Wants And Takes Nothing; Without The Recovery The Ant That Took A Bite On The Way Keeps The Trip, And The Fire Is Not Taken In Time")
    {
        struct Variant {
            bool sides_and_fire;
            bool recover;
            uint32_t fire_at_767;                                                       // the Fire ants of seat 1 150 ticks after the landing of 617
        };
        for (const Variant& v : {Variant{true, true, 1u}, Variant{true, false, 0u}, Variant{false, false, 0u}}) {
            sim::SimulationEngine sim;
            start_match(sim, "SMALL", 2, 0x0F);
            LevelPlan plan = plan_of(Level::Hard, Style::Raider, 2, 1);
            ASSERT_TRUE(plan.flower_sides && plan.flower_fire && plan.flower_recover);
            plan.flower_sides = plan.flower_fire = v.sides_and_fire;
            plan.flower_recover = v.recover;
            Rig rig(sim, 1, Level::Hard, std::make_unique<StandardBot>(plan));
            LandingWatch watch;
            const auto wants = [&](sim::AntType kind) { return static_cast<unsigned>(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(kind)]); };
            for (uint64_t t = 0; t < 1000; ++t) {
                rig.tick();
                watch.scan(sim, rig.map());
                if (t + 1 == 330) {                                                      // the Bomber at the west flower and the Fire at the east one (317): only the Fire is wanted
                    ASSERT_EQ(wants(sim::AntType::Bomber), 0u);
                    ASSERT_EQ(wants(sim::AntType::Fire), v.sides_and_fire ? 1u : 0u);
                }
                if (t + 1 == 600) {                                                      // the Bomber of 317 lay untouched for 280 ticks
                    ASSERT_EQ(count_type(sim, 1, sim::AntType::Bomber), 0u);
                    ASSERT_EQ(count_type(sim, 1, sim::AntType::Fire), 0u);
                }
                if (t + 1 == 640) ASSERT_EQ(wants(sim::AntType::Fire), v.sides_and_fire ? 1u : 0u);       // the Fire of 617 at the west flower
                if (t + 1 == 767) ASSERT_EQ(count_type(sim, 1, sim::AntType::Fire), v.fire_at_767);       // 150 ticks after the landing of 617
            }
            // the schedule that this seed draws at the west flower (flower 1): a Bomber at 317, a Fire at 617, a Bomber at 917
            std::vector<Landing> west;
            for (const Landing& l : watch.landings) {
                if (l.flower == 1) west.push_back(l);
            }
            ASSERT_EQ(west.size(), 3u);
            ASSERT_TRUE(west[0].tick == 317 && west[0].kind == sim::AntType::Bomber);
            ASSERT_TRUE(west[1].tick == 617 && west[1].kind == sim::AntType::Fire);
            ASSERT_TRUE(west[2].tick == 917 && west[2].kind == sim::AntType::Bomber);
            ASSERT_EQ(count_type(sim, 1, sim::AntType::Bomber), 0u);                      // never
            ASSERT_EQ(wants(sim::AntType::Bomber), 0u);
        }
    } TEST_END();

    TEST_CASE("AI23.6 The Contest: Two Hard Bots (Seats 0 And 2) And The Fire Drop Of 317 At The East Flower Of SMALL (Seed 2): The Hill That Is Nearest To It, Seat 2, Takes It; Seat 0 Has No Fire Ant By Tick 600")
    {
        const ArenaResult r = small_match(0x05, 600);
        ASSERT_EQ(r.seats[2].took[kFire], 1u);
        ASSERT_EQ(r.seats[0].took[kFire], 0u);
        ASSERT_EQ(r.seats[2].took_at_flowers, 1u);
    } TEST_END();

    TEST_CASE("AI23.7 The Plans: Hard Carries The Flower Rules (A Side For A Drop, The Fire Ant From Any Flower, The Recovery, The Recall, The Watcher); Medium And Easy Have None")
    {
        for (const Level level : {Level::Easy, Level::Medium}) {
            const LevelPlan none = plan_for(level);
            ASSERT_FALSE(none.flower_sides || none.flower_fire || none.flower_recover || none.flower_recall || none.flower_watch);
            for (const Style s : {Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive}) {
                if (!style_allowed(level, s)) continue;
                const LevelPlan p = plan_of(level, s, 7, 2);
                ASSERT_FALSE(p.flower_sides || p.flower_fire || p.flower_recover || p.flower_recall || p.flower_watch);
            }
        }
        ASSERT_TRUE(plan_for(Level::Hard).flower_sides && plan_for(Level::Hard).flower_fire && plan_for(Level::Hard).flower_recover && plan_for(Level::Hard).flower_recall && plan_for(Level::Hard).flower_watch);
        for (const Style s : {Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive}) {
            if (!style_allowed(Level::Hard, s)) continue;
            const LevelPlan p = plan_of(Level::Hard, s, 7, 2);
            ASSERT_TRUE(p.flower_sides && p.flower_fire && p.flower_recover && p.flower_recall && p.flower_watch);         // a style moves other values
        }
    } TEST_END();

    TEST_CASE("AI23.8 The Cadence: The Memory Counts The Droplets It Sees, Once Each (At A Look Every Tick Or Every 4): After Two Landings The Cycle Is Known (SMALL 300 Ticks, Both Flowers), The Chance Of A Kind Is Flat Until Three Landings And Then What Has Landed, TREASURE Shows Nothing")
    {
        for (const uint32_t every : {1u, 4u}) {
            sim::SimulationEngine sim;
            start_match(sim, "SMALL", 4, 0x0F);
            const MapInfo map(sim);
            Memory memory;
            LandingWatch watch;
            bool flat_prior_checked = false;
            for (uint64_t t = 0; t < 1500; ++t) {
                tick_all(sim, 1);
                watch.scan(sim, map);
                if (sim.current_tick() % every == 0) memory.update(BotView::build(sim, 0, &map), map);
                if (sim.current_tick() == 640) {                                         // two landings seen at each flower (317 and 617): the cycle is known, the mix is not
                    for (const FlowerInfo& f : map.flowers()) {
                        const FlowerLog* log = memory.flower(f.drop);
                        ASSERT_TRUE(log != nullptr);
                        ASSERT_EQ(log->landings, 2u);
                        ASSERT_EQ(log->cycle(), 300u);
                        ASSERT_EQ(log->next(), log->last + 300u);
                        ASSERT_EQ(log->chance(1u << static_cast<unsigned>(sim::AntType::Fire)), 200u);                                                  // five kinds alike
                        ASSERT_EQ(log->chance((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Thief))), 400u);
                        ASSERT_EQ(log->chance(0u), 0u);
                        flat_prior_checked = true;
                    }
                }
            }
            ASSERT_TRUE(flat_prior_checked);
            ASSERT_EQ(watch.landings.size(), 8u);                                        // 317, 617, 917 and 1217 at both flowers (the next droplet begins to fall at 1501)
            for (size_t k = 0; k < map.flowers().size(); ++k) {
                std::vector<Landing> own;
                std::array<uint32_t, 6> counts{};
                for (const Landing& l : watch.landings) {
                    if (l.flower != k) continue;
                    own.push_back(l);
                    ++counts[static_cast<size_t>(l.kind)];
                }
                const FlowerLog* log = memory.flower(map.flowers()[k].drop);
                ASSERT_TRUE(log != nullptr);
                const uint64_t expected_last = own.back().tick;                          // (the last landing of the run: nothing falls at the end of tick 1500)
                ASSERT_EQ(log->landings, static_cast<uint32_t>(own.size()));
                ASSERT_EQ(log->last, expected_last);
                ASSERT_EQ(log->before_last, own[own.size() - 2].tick);
                ASSERT_EQ(log->cycle(), 300u);
                for (size_t kind = 1; kind < counts.size(); ++kind) ASSERT_EQ(log->by_kind[kind], counts[kind]);
                // what has landed, in permille of the landings (three or more seen)
                for (size_t kind = 1; kind < counts.size(); ++kind) {
                    ASSERT_EQ(log->chance(1u << kind), counts[kind] * 1000u / static_cast<uint32_t>(own.size()));
                }
                ASSERT_EQ(log->chance(0x3Eu), 1000u);                                    // any of the five
            }
        }
        {   // TREASURE has no flower that drops: the memory never hears of one
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 1, 0x0F);
            const MapInfo map(sim);
            Memory memory;
            for (uint64_t t = 0; t < 700; ++t) {
                tick_all(sim, 1);
                memory.update(BotView::build(sim, 0, &map), map);
            }
            ASSERT_TRUE(memory.flower(tc(10, 10)) == nullptr);
            ASSERT_TRUE(map.flowers().empty());
        }
    } TEST_END();

    TEST_CASE("AI23.9 The Recall: A Power-Up That Changes Kind Under An Ant On Its Way Sends The Ant Back From Any Distance; Without The Rule An Ant Two Tiles Away Takes What Is There; Far Away (8 Tiles) Both Call It Back")
    {
        const auto build = [&](sim::SimulationEngine& sim) {
            empty_field(sim, 21);
            sim.grid_mut().place_powerup(16, 12, 2);                                    // a Fire power-up on the side of seat 0
            sim.spawn_unit(1, sim::AntType::Thief, TileCoord{52, 30});                  // in sight: the walls are wanted at every level, so is the Fire Ant
        };
        for (const bool recall : {false, true}) {
            for (const int stop_at : {2, 8}) {
                sim::SimulationEngine sim;
                build(sim);
                const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 12});
                LevelPlan plan = medium_with_rules();
                plan.flower_recall = recall;
                Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                while (rig.proposed_count(CommandType::GroupMove) == 0 && sim.current_tick() < 60) rig.tick();
                ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
                const auto distance = [&]() { return TileCoord{sim.get_unit(ant).pos.x, sim.get_unit(ant).pos.y}.chebyshev_dist(tc(16, 12)); };
                while (distance() > stop_at && sim.current_tick() < 400) rig.tick();
                ASSERT_TRUE(distance() <= stop_at);
                sim.grid_mut().place_powerup(16, 12, 3);                                // the dropper puts a Thief where the Fire was
                rig.run(stop_at == 2 ? 120 : 40);
                const StandardBot& bot = rig.as<StandardBot>();
                if (stop_at == 2 && !recall) {
                    ASSERT_EQ(bot.powerups().called_back(), 0u);
                    ASSERT_TRUE(sim.get_unit(ant).type == sim::AntType::Thief);         // it took what was there
                } else {
                    ASSERT_EQ(bot.powerups().called_back(), 1u);
                    ASSERT_EQ(rig.proposed_count(CommandType::Stop), 1u);
                    ASSERT_TRUE(sim.get_unit(ant).type == sim::AntType::Worker);
                    ASSERT_TRUE(sim.grid().has_powerup_at(tc(16, 12)));
                }
            }
        }
    } TEST_END();
    TEST_CASE("AI23.10 The Watcher: A Hard Raider At SMALL (Seed 4, Seat 1) Stands Beside The West Flower 40 Ticks Before The Third Landing (917, A Fire After Two Bombers), Takes The Fire Within 45 Ticks Of The Landing And Steps Off The Tile; Without The Watcher The Fire Is Not Taken In Time; The Fourth Landing Is On Schedule")
    {
        uint64_t taken_with = 0;
        uint64_t taken_without = 0;
        for (const bool with_watcher : {true, false}) {
            sim::SimulationEngine sim;
            start_match(sim, "SMALL", 4, 0x0F);
            LevelPlan plan = plan_of(Level::Hard, Style::Raider, 4, 1);
            ASSERT_TRUE(plan.flower_watch);
            plan.flower_watch = with_watcher;
            Rig rig(sim, 1, Level::Hard, std::make_unique<StandardBot>(plan));
            LandingWatch watch;
            const TileCoord drop = rig.map().flowers()[1].drop;
            uint32_t fire_ant = 0;
            uint64_t fire_taken_at = 0;
            for (uint64_t t = 0; t < 1300; ++t) {
                rig.tick();
                watch.scan(sim, rig.map());
                const uint64_t now = sim.current_tick();
                const PowerUpTask& powerups = rig.as<StandardBot>().powerups();
                if (now == 877) {                                                       // 40 ticks before the third landing
                    if (with_watcher) {
                        const sim::AntSnapshot* w = snapshot_of(sim, powerups.watcher());
                        ASSERT_TRUE(w != nullptr);
                        ASSERT_EQ(std::max(std::abs(w->tile_x - drop.x), std::abs(w->tile_y - drop.y)), 1);            // beside the drop tile, not on it
                        ASSERT_EQ(powerups.watchers_sent(), 1u);
                    } else {
                        ASSERT_EQ(powerups.watcher(), 0u);
                        ASSERT_EQ(powerups.watchers_sent(), 0u);
                    }
                }
                if (fire_taken_at == 0 && count_type(sim, 1, sim::AntType::Fire) != 0) {
                    fire_taken_at = now;
                    fire_ant = first_of_type(sim, 1, sim::AntType::Fire);
                }
                if (now == 1037 && with_watcher) {                                      // 120 ticks after the landing: the Fire Ant does not stand on the drop tile
                    const sim::AntSnapshot* f = snapshot_of(sim, fire_ant);
                    ASSERT_TRUE(f != nullptr);
                    ASSERT_TRUE(!(f->tile_x == drop.x && f->tile_y == drop.y));
                }
            }
            std::vector<Landing> west;
            for (const Landing& l : watch.landings) {
                if (l.flower == 1) west.push_back(l);
            }
            ASSERT_EQ(west.size(), 4u);                                                 // the schedule of this seed at the west flower: Bomber, Bomber, Fire, Bomber
            ASSERT_TRUE(west[0].kind == sim::AntType::Bomber && west[1].kind == sim::AntType::Bomber && west[2].kind == sim::AntType::Fire && west[3].kind == sim::AntType::Bomber);
            ASSERT_TRUE(west[2].tick == 917 && west[3].tick == 1217);                   // the Fire ant did not hold the flower
            (with_watcher ? taken_with : taken_without) = fire_taken_at;
        }
        ASSERT_TRUE(taken_with > 917 && taken_with <= 917 + 30);                        // the watcher is beside the tile: a move onto it, the pick-up 12 ticks after the order
        ASSERT_TRUE(taken_without >= 917 + 60);                                         // an ant that harvests 7 tiles from the flower is sent and walks
    } TEST_END();


    TEST_CASE("AI23.11 The Watcher And The Cadence: Two Bombers Seen At The West Flower Of SMALL (Seed 5) Do Not Tell The Raider That Nothing It Lacks Lands There (Flat Prior): It Stands Beside The Flower; The Third Bomber Does, And The Watcher Goes Home")
    {
        sim::SimulationEngine sim;
        start_match(sim, "SMALL", 5, 0x0F);
        LevelPlan plan = plan_of(Level::Hard, Style::Raider, 5, 1);
        Rig rig(sim, 1, Level::Hard, std::make_unique<StandardBot>(plan));
        LandingWatch watch;
        const TileCoord drop = rig.map().flowers()[1].drop;
        uint32_t watcher_at_877 = 0;
        for (uint64_t t = 0; t < 1000; ++t) {
            rig.tick();
            watch.scan(sim, rig.map());
            const PowerUpTask& powerups = rig.as<StandardBot>().powerups();
            if (sim.current_tick() == 877) {
                watcher_at_877 = powerups.watcher();
                ASSERT_TRUE(watcher_at_877 != 0);
            }
            if (sim.current_tick() == 960) {                                            // the third Bomber (917) was seen: no Fire or Thief has ever landed here
                const FlowerLog* log = rig.as<StandardBot>().tactics().memory.flower(drop);
                ASSERT_TRUE(log != nullptr);
                ASSERT_EQ(log->landings, 3u);
                ASSERT_EQ(log->chance((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Thief))), 0u);
                ASSERT_EQ(powerups.watcher(), 0u);
                ASSERT_EQ(powerups.watchers_sent(), 1u);
            }
        }
        std::vector<Landing> west;
        for (const Landing& l : watch.landings) {
            if (l.flower == 1) west.push_back(l);
        }
        ASSERT_EQ(west.size(), 3u);
        for (const Landing& l : west) ASSERT_TRUE(l.kind == sim::AntType::Bomber);
    } TEST_END();

    TEST_CASE("AI23.12 Which Drops A Bot Wants (A Hard Economic Bot On SMALL, Seed 5: A Bomber Lands At Both Flowers At 317): The Bomber At The West Flower Is Wanted By Seat 1 Alone Of The Seats That It Is Not The Nearest Hill's, The One At The East Flower By Seat 2; Seats 0 And 3 Want Neither")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            sim::SimulationEngine sim;
            start_match(sim, "SMALL", 5, 0x0F);
            LevelPlan plan = plan_of(Level::Hard, Style::Economic, 5, seat);
            ASSERT_TRUE(plan.flower_sides && plan.secure_side && ((plan.secure_kinds >> static_cast<unsigned>(sim::AntType::Bomber)) & 1u) != 0);
            Rig rig(sim, seat, Level::Hard, std::make_unique<StandardBot>(plan));
            const auto wants_bomber = [&]() { return static_cast<unsigned>(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Bomber)]); };
            rig.run(300);
            ASSERT_EQ(wants_bomber(), 0u);                                                // (before the first landing nothing lies there: the premise)
            rig.run(30);
            ASSERT_EQ(wants_bomber(), (seat == 1 || seat == 2) ? 1u : 0u);                // 330: the Bomber of 317 lies at each flower, on the side of seat 1 (west) and of seat 2 (east)
        }
    } TEST_END();

    TEST_CASE("AI23.13 A Fire Power-Up Of The Start On Another Side Is Not A Flower's Fire: A Medium Bot Wants The One On Its Own Side And Not The One On The Side Of Seat 3 (No Flower In This World)")
    {
        for (const bool own : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 21);
            sim.grid_mut().place_powerup(own ? 16 : 40, own ? 12 : 40, 2);               // a Fire power-up: on the side of seat 0, or on the side of seat 3
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 8});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(medium_with_rules()), 4, 4);
            ASSERT_TRUE(rig.map().flowers().empty());
            rig.run(8);
            ASSERT_EQ(static_cast<unsigned>(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Fire)]), own ? 1u : 0u);
        }
    } TEST_END();

    TEST_CASE("AI23.14 What The Arena Counts Of The Pick-Ups, By Hand (SMALL, Before The First Landing): Four Ants Take A Bomber, A Fire, A Thief And A Combat Power-Up At 0, 1, 2 And 3 Tiles From The West Flower's Drop Tile, Each Is Counted By The Kind It Became, Those At 0 And 1 Tiles Are Counted At A Flower; A Typed Ant That Stands There From The Start Took Nothing")
    {
        sim::SimulationEngine sim;
        start_match(sim, "SMALL", 1, 0x0F);
        const MapInfo map(sim);
        const TileCoord drop = map.flowers()[1].drop;
        const std::array<sim::AntType, 4> kinds = {sim::AntType::Bomber, sim::AntType::Fire, sim::AntType::Thief, sim::AntType::Combat};
        std::array<uint32_t, 4> ants{};
        for (int d = 0; d < 4; ++d) {
            ants[static_cast<size_t>(d)] = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{drop.x + d, drop.y + 1});
            sim.grid_mut().place_powerup(drop.x + d, drop.y, static_cast<uint8_t>(kinds[static_cast<size_t>(d)]));
        }
        sim.spawn_unit(1, sim::AntType::Swimmer, TileCoord{drop.x + 6, drop.y + 3});          // a Swimmer that nobody made on the way
        FlowerTally tally;
        tick_all(sim, 1);
        tally.scan(sim);
        for (int d = 0; d < 4; ++d) sim.apply_command(command_of(CommandType::GroupMove, 1, {ants[static_cast<size_t>(d)]}, drop.x + d, drop.y));
        for (int i = 0; i < 80; ++i) {
            tick_all(sim, 1);
            tally.scan(sim);
        }
        for (int d = 0; d < 4; ++d) ASSERT_TRUE(snapshot_of(sim, ants[static_cast<size_t>(d)]) != nullptr && snapshot_of(sim, ants[static_cast<size_t>(d)])->raw_type == kinds[static_cast<size_t>(d)]);     // (the premise: all four took theirs)
        const FlowerTally::Seat& seat = tally.seat(1);
        for (const sim::AntType kind : kinds) ASSERT_EQ(seat.took[static_cast<size_t>(kind)], 1u);
        ASSERT_EQ(seat.took[static_cast<size_t>(sim::AntType::Swimmer)], 0u);
        ASSERT_EQ(seat.at_flowers, 2u);
        for (const int other : {0, 2, 3}) ASSERT_EQ(tally.seat(static_cast<uint8_t>(other)).at_flowers, 0u);
        ASSERT_EQ(tally.landings(), 0u);                                                        // (nothing has landed yet: the first droplet falls at 301)
    } TEST_END();

    TEST_CASE("AI23.15 A Pick-Up Trip Whose Ant Took Food On The Way Is Given Up At Once At A Flower's Drop (Recover): A Crumb In Its Mouth Or Points In Its Hands Is Enough, Each Alone; Without The Rule The Trip Goes On; To A Power-Up Of The Start (No Flower's) It Goes On With The Rule Too, As Before")
    {
        for (const bool recover : {false, true}) {
            for (const bool crumb : {false, true}) {
                {   // (a) a power-up of the start, no flower's: the trip goes on, with the rule and without it (the rule is the flowers')
                    sim::SimulationEngine sim;
                    empty_field(sim, 21);
                    sim.grid_mut().place_powerup(16, 12, 2);                                   // a Fire power-up on the side of seat 0
                    sim.spawn_unit(1, sim::AntType::Thief, TileCoord{52, 30});                  // in sight: the Fire Ant is wanted at every level
                    const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 12});
                    LevelPlan plan = medium_with_rules();
                    plan.flower_recover = recover;
                    Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                    while (rig.proposed_count(CommandType::GroupMove) == 0 && sim.current_tick() < 60) rig.tick();
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
                    rig.run(10);                                                                // on its way
                    if (crumb) sim.get_unit(ant).holding = 1;
                    else sim.get_unit(ant).carried_points = 7;
                    rig.run(30);
                    const PowerUpTask& powerups = rig.as<StandardBot>().powerups();
                    ASSERT_EQ(powerups.failed(), 0u);
                    ASSERT_EQ(powerups.active(), 1u);
                }
                {   // (b) the Fire of 617 at the west flower of SMALL (seed 2, the Hard Raider of seat 1): the ant ordered to it takes food at once
                    sim::SimulationEngine sim;
                    start_match(sim, "SMALL", 2, 0x0F);
                    LevelPlan plan = plan_of(Level::Hard, Style::Raider, 2, 1);
                    plan.flower_recover = recover;
                    plan.flower_watch = false;                                                  // (the watcher stands there before the landing and takes it at once: this is the trip of an ant that walks)
                    Rig rig(sim, 1, Level::Hard, std::make_unique<StandardBot>(plan));
                    rig.run(617);                                                               // (the landing is seen at 617 and the nearest ant is ordered to the drop tile at that look)
                    const std::vector<std::pair<uint32_t, sim::AntType>> on_the_way = powerups_of(rig).on_their_way();
                    ASSERT_EQ(on_the_way.size(), 1u);
                    ASSERT_TRUE(on_the_way[0].second == sim::AntType::Fire);
                    const uint32_t ant = on_the_way[0].first;
                    if (crumb) sim.get_unit(ant).holding = 1;
                    else sim.get_unit(ant).carried_points = 7;
                    for (int i = 0; i < 30 && (!recover || powerups_of(rig).failed() == 0); ++i) rig.tick();
                    ASSERT_EQ(powerups_of(rig).failed(), recover ? 1u : 0u);
                    ASSERT_EQ(powerups_of(rig).active(), recover ? 0u : 1u);
                    if (recover && !crumb) ASSERT_EQ(sim.get_unit(ant).holding, 0);              // (the points alone ended the trip: the engine's own bite, which sets the crumb, comes 12 ticks later)
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI23.16 The Watcher: The Watcher Is Sent Only When The Plan Lacks A Kind And There Is Time: A Raider That Owns A Fire Ant And A Thief Sends None (Even At A Chance Of 0, Which Never Says Not Likely Enough), With 850 Ticks Left At 700 None Is Sent, Otherwise One Is (SMALL, Seed 4)")
    {
        for (const bool owns : {false, true}) {
            uint32_t sent = 99;
            raider_run(4, 1300, [&](sim::SimulationEngine& sim) {
                if (!owns) return;
                sim.spawn_unit(1, sim::AntType::Fire, TileCoord{5, 31});
                sim.spawn_unit(1, sim::AntType::Thief, TileCoord{6, 30});
            }, [](LevelPlan& plan) { plan.flower_watch_chance = 0; }, [&](Rig& rig, sim::SimulationEngine& sim) {
                if (sim.current_tick() == 1000) sent = powerups_of(rig).watchers_sent();
            });
            ASSERT_EQ(sent, owns ? 0u : 1u);
        }
        for (const bool late : {false, true}) {
            uint32_t sent = 99;
            raider_run(4, 1000, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                if (late && sim.current_tick() == 700) sim.set_match_time_remaining_ms(850u * sim::TICK_MS);
                if (sim.current_tick() == 1000) sent = powerups_of(rig).watchers_sent();
            });
            ASSERT_EQ(sent, late ? 0u : 1u);
        }
    } TEST_END();

    TEST_CASE("AI23.17 The Watcher: The Watcher's Place Is A Free Tile Beside The Drop Tile, Never One That A Power-Up Lies On (SMALL, Seed 4: The Place Is (1, 21); With A Swimmer Power-Up Laid There At 790, Three Ticks Before The Watcher Is Sent, It Is Another Tile Beside The Drop)")
    {
        const TileCoord usual{1, 21};
        for (const bool laid : {false, true}) {
            TileCoord place{-1, -1};
            int32_t from_drop = -1;
            raider_run(4, 800, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                const uint32_t watcher = powerups_of(rig).watcher();
                if (laid && sim.current_tick() == 790) sim.grid_mut().place_powerup(usual.x, usual.y, 5);
                if (watcher != 0 && place.x < 0) {
                    const std::vector<Move> sent = moves_of(rig, watcher, sim.current_tick());
                    if (!sent.empty()) place = sent.front().tile;
                }
                if (sim.current_tick() == 800) from_drop = place.chebyshev_dist(rig.map().flowers()[1].drop);
            });
            ASSERT_EQ(from_drop, 1);
            ASSERT_TRUE(laid ? !(place == usual) : place == usual);
        }
    } TEST_END();

    TEST_CASE("AI23.18 The Watcher: The Watcher Is The Ant That Walks Least To Its Place (SMALL, Seed 4: The Raider's Own Ant Is Sent At 793; A Worker Put Down At (3, 25) At 780, Four Tiles From The Place, Is Sent Instead, Later, For It Needs Less Lead)")
    {
        for (const bool neighbour : {false, true}) {
            uint32_t spawned = 0;
            uint32_t watcher = 0;
            uint64_t sent_at = 0;
            raider_run(4, 900, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                if (neighbour && sim.current_tick() == 780) spawned = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{3, 25});
                if (watcher == 0 && powerups_of(rig).watcher() != 0) {
                    watcher = powerups_of(rig).watcher();
                    sent_at = sim.current_tick();
                }
            });
            ASSERT_TRUE(watcher != 0);
            if (neighbour) {
                ASSERT_EQ(watcher, spawned);
                ASSERT_TRUE(sent_at > 810);
            } else {
                ASSERT_TRUE(sent_at < 800);
            }
        }
    } TEST_END();

    TEST_CASE("AI23.19 The Watcher: The Watcher Is Ordered To Its Place Once, And Again Only When It Stands Idle 80 Ticks After The Order (SMALL, Seed 5, A Chance Of 0 So That It Stays: A Stop At 820 Leaves It Idle, The Second Order Comes At The First Look From 873 On, It Stands Beside The Drop From 940 On; Without The Stop The One Order Is Enough)")
    {
        for (const bool stopped : {false, true}) {
            uint32_t watcher = 0;
            uint64_t sent_at = 0;
            std::vector<Move> orders;
            uint32_t still = 0;
            int32_t at_1000 = -1;
            raider_run(5, 1000, keep_world, [](LevelPlan& plan) { plan.flower_watch_chance = 0; }, [&](Rig& rig, sim::SimulationEngine& sim) {
                if (watcher == 0 && powerups_of(rig).watcher() != 0) {
                    watcher = powerups_of(rig).watcher();
                    sent_at = sim.current_tick();
                }
                if (stopped && sim.current_tick() == 820) sim.apply_command(command_of(CommandType::Stop, 1, {watcher}, 0, 0));
                if (sim.current_tick() != 1000) return;
                orders = moves_of(rig, watcher, sent_at);
                still = powerups_of(rig).watcher();
                at_1000 = distance_to(snapshot_of(sim, watcher), rig.map().flowers()[1].drop);
            });
            ASSERT_TRUE(watcher != 0);
            ASSERT_EQ(orders.size(), stopped ? 2u : 1u);
            if (stopped) ASSERT_TRUE(orders[1].tick >= orders[0].tick + 80 && orders[1].tick < orders[0].tick + 80 + 4);       // (the look of the Hard level comes every 4 ticks)
            ASSERT_EQ(still, watcher);
            ASSERT_EQ(at_1000, 1);
        }
    } TEST_END();

    TEST_CASE("AI23.20 The Watcher: The Watcher Goes Home When The Wait For The Next Landing Is Longer Than A Trip There And Back, Not Before The Droplet It Waited For Has Been Taken Or Left (SMALL, Seed 5, A Lead Of 10 Ticks And A Chance Of 0: The Bomber Of 917 Is Left; It Stands There At 960 And Is Gone At 1,000)")
    {
        uint32_t at_960 = 0;
        int32_t beside_960 = -1;
        uint32_t at_1000 = 99;
        uint32_t sent = 0;
        raider_run(5, 1000, keep_world, [](LevelPlan& plan) {
            plan.flower_watch_chance = 0;
            plan.flower_watch_early = 10;
        }, [&](Rig& rig, sim::SimulationEngine& sim) {
            if (sim.current_tick() == 960) {
                at_960 = powerups_of(rig).watcher();
                beside_960 = distance_to(snapshot_of(sim, at_960), rig.map().flowers()[1].drop);
            }
            if (sim.current_tick() == 1000) {
                at_1000 = powerups_of(rig).watcher();
                sent = powerups_of(rig).watchers_sent();
            }
        });
        ASSERT_TRUE(at_960 != 0);
        ASSERT_EQ(beside_960, 1);
        ASSERT_EQ(at_1000, 0u);
        ASSERT_EQ(sent, 1u);
    } TEST_END();

    TEST_CASE("AI23.21 The Watcher: The Ant That Took The Drop From The Watcher's Place Is Ordered Off The Drop Tile, To The Place, With An Urgent Order At Its First Look After The Power-Up Animation (SMALL, Seed 4: The Fire Of 917 Is Taken By The Watcher, It Can Take Orders Again At 955)")
    {
        TileCoord place{-1, -1};
        uint32_t fire = 0;
        uint64_t orderable = 0;
        std::vector<Move> off;
        Priority priority = Priority::Background;
        uint32_t interval = 0;
        raider_run(4, 1000, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
            const uint64_t now = sim.current_tick();
            const uint32_t watcher = powerups_of(rig).watcher();
            if (watcher != 0 && place.x < 0) {
                const std::vector<Move> sent = moves_of(rig, watcher, sim.current_tick());
                if (!sent.empty()) place = sent.front().tile;
            }
            if (fire == 0) fire = first_of_type(sim, 1, sim::AntType::Fire);
            const sim::AntSnapshot* f = snapshot_of(sim, fire);
            if (fire != 0 && orderable == 0 && f != nullptr && f->state != sim::UnitState::PoweringUp) orderable = now;
            if (now != 1000) return;
            for (const Move& m : moves_of(rig, fire, 918)) {
                if (m.tile == place) off.push_back(m);
            }
            interval = rig.profile().decision_interval;
            if (!off.empty()) priority = rig.proposed_priority[off[0].index];
        });
        ASSERT_TRUE(fire != 0 && orderable > 917);
        ASSERT_EQ(off.size(), 1u);
        ASSERT_TRUE(off[0].tick >= orderable && off[0].tick < orderable + interval);
        ASSERT_TRUE(priority == Priority::Urgent);
    } TEST_END();

    TEST_CASE("AI23.24 The Watcher Is Hit: The Ant That Waits Beside The West Flower Of SMALL (Seed 4, 40 Ticks Before The Landing Of 917) Loses A Hit Point; At The Next Look It Is Released And Nobody Takes Its Place, Also Not At The Looks Before The Landing (The Flower Is Left For 600 Ticks), And The Fire Is Taken By An Ordinary Trip; Without The Blow The Watcher Stays And Takes It Within 30 Ticks")
    {
        for (const bool blow : {true, false}) {
            uint32_t watcher = 0;
            uint64_t fire_at = 0;
            uint32_t sent_after = 0;
            raider_run(4, 1100, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                const uint64_t now = sim.current_tick();
                const PowerUpTask& powerups = powerups_of(rig);
                if (now == 877) {
                    watcher = powerups.watcher();
                    ASSERT_TRUE(watcher != 0);
                    if (blow) sim.get_unit(watcher).hp = static_cast<uint16_t>(sim.get_unit(watcher).hp - 1);
                }
                if (blow && now >= 885 && now <= 1000 && now % 5 == 0) {
                    ASSERT_EQ(powerups.watcher(), 0u);                              // released at the next look (every 4 ticks), and nobody is sent in its place
                    sent_after = powerups.watchers_sent();
                }
                if (fire_at == 0 && count_type(sim, 1, sim::AntType::Fire) != 0) fire_at = now;
            });
            if (blow) {
                ASSERT_EQ(sent_after, 1u);
                ASSERT_TRUE(fire_at > 917);                                         // the trip of the ordinary kind takes it (an ant that harvests nearby)
            } else {
                ASSERT_TRUE(fire_at > 917 && fire_at <= 917 + 30);
            }
        }
    } TEST_END();

    TEST_CASE("AI23.25 The Watcher's Place Is Shut In: Fire Walls On Every Tile Round The Place Beside The West Flower Of SMALL (Seed 4, Lit 40 Ticks Before The Landing Of 917) Leave No Walk To The Hill; The Watcher Is Released When The Walking Field Of The Place Is Made Again (Within 100 Ticks); Without The Fire It Stays")
    {
        const TileCoord place{1, 21};                                               // (AI23.17: the place of this watcher)
        for (const bool fire : {true, false}) {
            uint32_t watcher = 0;
            raider_run(4, 912, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                const uint64_t now = sim.current_tick();
                const PowerUpTask& powerups = powerups_of(rig);
                if (now == 877) {
                    watcher = powerups.watcher();
                    ASSERT_TRUE(watcher != 0);
                    if (fire) {
                        for (int32_t dy = -1; dy <= 1; ++dy) {
                            for (int32_t dx = -1; dx <= 1; ++dx) {
                                if (dx != 0 || dy != 0) sim.set_fire_at(TileCoord{place.x + dx, place.y + dy}, 3600);
                            }
                        }
                    }
                }
                if (now >= 878 && now <= 892) ASSERT_EQ(powerups.watcher(), watcher);       // (the field of the place is up to 100 ticks old: the bot has not seen the fire yet)
                if (now >= 906) ASSERT_TRUE(fire ? powerups.watcher() != watcher : powerups.watcher() == watcher);        // (released at 898 to 905 here)
            });
        }
    } TEST_END();

    TEST_CASE("AI23.26 The Watcher Counts The Trips That Are Under Way As Kinds It Has: The Raider At SMALL (Seed 4, Seat 1), With The Bar Of Its Chance Raised To 300 In 1,000 (Before Three Landings Are Seen Each Kind Has 200: A Fire And A Thief Lacking Pass, A Thief Alone Does Not), Sends The Watcher At 793; With A Fire Power-Up Laid On Its Side At 760, On Its Way At 793, It Sends None")
    {
        for (const bool laid : {false, true}) {
            size_t on_the_way = 99;
            bool fire = false;
            uint32_t sent = 99;
            raider_run(4, 900, keep_world, [](LevelPlan& plan) { plan.flower_watch_chance = 300; }, [&](Rig& rig, sim::SimulationEngine& sim) {
                if (laid && sim.current_tick() == 760) sim.grid_mut().place_powerup(13, 20, 2);        // (a Fire power-up on the Raider's side, 38 ticks from its nearest ant)
                if (sim.current_tick() == 800) {
                    const std::vector<std::pair<uint32_t, sim::AntType>> trips = powerups_of(rig).on_their_way();
                    on_the_way = trips.size();
                    fire = !trips.empty() && trips[0].second == sim::AntType::Fire;
                }
                if (sim.current_tick() == 900) sent = powerups_of(rig).watchers_sent();
            });
            ASSERT_EQ(on_the_way, laid ? 1u : 0u);
            ASSERT_TRUE(fire == laid);
            ASSERT_EQ(sent, laid ? 0u : 1u);
        }
    } TEST_END();

    TEST_CASE("AI23.27 The Watcher Is Not Sent From Afar: On GAUNTLET (Seed 4) The Hard Raider At Seat 3, Whose Flower Is The One At (3, 6), Sends The Watcher At 1,481; With All Its Ants Put Down In The Far Corner At 1,250, More Than The 420 Ticks Of A Trip From The Flower, It Sends None By 1,500")
    {
        for (const bool afar : {false, true}) {
            sim::SimulationEngine sim;
            start_match(sim, "GAUNTLET", 4, 0x0F);
            Rig rig(sim, 3, Level::Hard, std::make_unique<StandardBot>(plan_of(Level::Hard, Style::Raider, 4, 3)));
            uint32_t at_1400 = 99;
            int32_t nearest = -1;
            for (uint64_t t = 0; t < 1500; ++t) {
                rig.tick();
                if (afar && sim.current_tick() == 1250) {
                    const BotView before = BotView::build(sim, 3, &rig.map());
                    const size_t count = before.mine().size();
                    for (const AntView& a : before.mine()) sim.kill_unit(a.id);
                    for (size_t i = 0; i < count; ++i) ASSERT_TRUE(sim.spawn_unit(3, sim::AntType::Worker, tc(58 - static_cast<int32_t>(i), 57)) != 0);
                    const BotView view = BotView::build(sim, 3, &rig.map());                    // (how far the ants are now: the walking cost to the flower's drop tile)
                    const std::vector<uint8_t> mask = MapInfo::walkable_mask(view.grid(), 3, view.walk_context());
                    const std::vector<int32_t> field = MapInfo::cost_field_onto(view.grid(), mask, 3, rig.map().flowers()[0].drop, view.walk_context());
                    nearest = 1 << 20;
                    for (const AntView& a : view.mine()) {
                        const int32_t cost = field[static_cast<size_t>(a.tile.y) * view.grid().width() + static_cast<size_t>(a.tile.x)];
                        if (cost >= 0) nearest = std::min(nearest, MapInfo::walking_ticks(cost));
                    }
                }
                if (sim.current_tick() == 1400) at_1400 = powerups_of(rig).watchers_sent();
            }
            if (afar) ASSERT_TRUE(nearest > 480);                                              // (the watcher's place is a tile beside the drop tile: a little nearer, never 60 ticks)
            ASSERT_EQ(at_1400, 0u);
            ASSERT_EQ(powerups_of(rig).watchers_sent(), afar ? 0u : 1u);
        }
    } TEST_END();

    TEST_CASE("AI23.28 A Landing That Does Not Come Moves The Expected Landing One Cycle On, After 60 Ticks Of Grace: A Fire Wall On The Drop Tile Of The West Flower Of SMALL (Seed 5, A Chance Of 0, Lit At 1,100 For 600 Ticks) Holds Back The Landing Of 1,217; The Watcher Stands Beside The Tile From 1,241 On And Goes Home At The First Look After 1,277, Not Before And Not Never, And Is Not Sent Again By 1,400")
    {
        uint64_t gone_at = 0;
        uint32_t sent = 0;
        int32_t beside_1270 = -1;
        size_t landed = 0;
        LandingWatch watch;
        raider_run(5, 1400, keep_world, [](LevelPlan& plan) {
            plan.flower_watch_chance = 0;
            plan.flower_watch_early = 10;
        }, [&](Rig& rig, sim::SimulationEngine& sim) {
            const uint64_t now = sim.current_tick();
            const TileCoord drop = rig.map().flowers()[1].drop;
            if (now == 1100) {                                                          // (the Bomber of 917 is taken away first: a tile that still holds a power-up takes a landing whatever else lies on it)
                sim.grid_mut().clear_powerup(drop.x, drop.y);
                sim.set_fire_at(drop, 600);
            }
            watch.scan(sim, rig.map());
            const uint32_t watcher = powerups_of(rig).watcher();
            if (now == 1270) beside_1270 = distance_to(snapshot_of(sim, watcher), drop);
            if (now >= 1250 && gone_at == 0 && watcher == 0) gone_at = now;
            if (now == 1400) sent = powerups_of(rig).watchers_sent();
        });
        for (const Landing& l : watch.landings) landed += (l.flower == 1 && l.tick > 1100) ? 1u : 0u;
        ASSERT_EQ(landed, 0u);                                                          // the wall held the landing back
        ASSERT_EQ(beside_1270, 1);
        ASSERT_TRUE(gone_at > 1277 && gone_at <= 1300);
        ASSERT_EQ(sent, 2u);
    } TEST_END();

    TEST_CASE("AI23.29 A Landing That Is Held Back For Several Cycles Moves The Expected Landing On By Whole Cycles: A Fire Wall On The Drop Tile Of The West Flower Of SMALL (Seed 5, A Chance Of 0, Lit At 1,100 For 1,500 Ticks) Holds Back The Landings Of 1,217 And 1,517; At 1,700 The Watcher That Was Sent For 1,517 Has Gone Home And The Next One Is Not Sent Yet")
    {
        uint32_t watcher = 99;
        uint32_t sent = 0;
        raider_run(5, 1700, keep_world, [](LevelPlan& plan) {
            plan.flower_watch_chance = 0;
            plan.flower_watch_early = 10;
        }, [&](Rig& rig, sim::SimulationEngine& sim) {
            const TileCoord drop = rig.map().flowers()[1].drop;
            if (sim.current_tick() == 1100) {                                           // (the Bomber of 917 is taken away first: a tile that still holds a power-up takes a landing whatever else lies on it)
                sim.grid_mut().clear_powerup(drop.x, drop.y);
                sim.set_fire_at(drop, 1500);
            }
            if (sim.current_tick() == 1700) {
                watcher = powerups_of(rig).watcher();
                sent = powerups_of(rig).watchers_sent();
            }
        });
        ASSERT_EQ(watcher, 0u);                                                         // (a landing moved on once only would leave the watcher standing from 1,578 on)
        ASSERT_EQ(sent, 3u);                                                            // (880, 1,140 and 1,470: the next, for 1,817, goes at about 1,770)
    } TEST_END();

    TEST_CASE("AI23.30 The Watcher Stays While The Wait For The Next Landing Is No Longer Than A Trip There And Back With Its Lead And 30 Ticks: SMALL, Seed 5, A Chance Of 0 And A Lead Of 60 Ticks, The Bomber Of 917 Is Left, And At 1,100 It Stands Beside The Drop Tile Still, Sent Once (With A Lead Of 10 It Goes Home At 977: AI23.20)")
    {
        uint32_t watcher = 0;
        int32_t beside = -1;
        uint32_t sent = 0;
        raider_run(5, 1100, keep_world, [](LevelPlan& plan) {
            plan.flower_watch_chance = 0;
            plan.flower_watch_early = 60;
        }, [&](Rig& rig, sim::SimulationEngine& sim) {
            if (sim.current_tick() != 1100) return;
            watcher = powerups_of(rig).watcher();
            beside = distance_to(snapshot_of(sim, watcher), rig.map().flowers()[1].drop);
            sent = powerups_of(rig).watchers_sent();
        });
        ASSERT_TRUE(watcher != 0);
        ASSERT_EQ(beside, 1);
        ASSERT_EQ(sent, 1u);
    } TEST_END();

    TEST_CASE("AI23.31 An Ant That A Task Of A Higher Rank Holds Is Not Taken For The Watcher: On SMALL (Seed 4) A Worker Put Down At (3, 25) At 780, Four Tiles From The Place, Would Be The Watcher (AI23.18); In The Hands Of The Fight It Is Passed Over, And The Raider's Own Ant Is Sent As At 793")
    {
        for (const bool held : {false, true}) {
            uint32_t spawned = 0;
            uint32_t watcher = 0;
            uint32_t sent = 99;
            TaskId owner = 99;
            raider_run(4, 900, keep_world, keep_plan, [&](Rig& rig, sim::SimulationEngine& sim) {
                StandardBot& bot = rig.as<StandardBot>();
                if (sim.current_tick() == 780) {
                    spawned = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{3, 25});
                    if (held) ASSERT_TRUE(bot.ledger_for_labs().take(spawned, StandardBot::kFight));
                }
                if (watcher == 0) watcher = bot.powerups().watcher();
                if (sim.current_tick() == 900) {
                    sent = bot.powerups().watchers_sent();
                    owner = bot.ledger().owner(spawned);
                }
            });
            ASSERT_TRUE(watcher != 0);
            ASSERT_EQ(sent, 1u);
            ASSERT_EQ(watcher == spawned, !held);
            ASSERT_TRUE(owner == (held ? StandardBot::kFight : StandardBot::kPowerUps));
        }
    } TEST_END();

    TEST_CASE("AI23.22 Where No Flower Is Within A Hill's Reach (TINY, TREASURE, ISLANDS) The Flower Rules Change Nothing: A Whole Match Of Four Standard Bots, Medium (With The Rules Switched On) And Hard, Has The Same State Hash And The Same Scores With And Without Them (The Same Bots, Only The Flags Differ)")
    {
        for (const char* map : {"TINY", "TREASURE", "ISLANDS"}) {
            for (const Level level : {Level::Medium, Level::Hard}) {
                ArenaResult results[2];
                for (int off = 0; off < 2; ++off) {
                    ArenaSpec spec;
                    spec.level = &level_of(map);
                    spec.seed = 3;
                    spec.max_ticks = 2400;
                    for (uint8_t seat = 0; seat < 4; ++seat) {
                        BotSpec b;
                        b.seat = seat;
                        b.level = level;
                        b.kind = "standard";
                        b.style = seat % 2 == 0 ? Style::Raider : Style::Aggressive;
                        spec.bots.push_back(b);
                    }
                    spec.factory = [off](const BotSpec& b) -> std::unique_ptr<Bot> {
                        return std::make_unique<StandardBot>(b.level, b.style, [off, level = b.level](LevelPlan& p) {
                            p.flower_sides = p.flower_fire = p.flower_recover = p.flower_recall = off == 0;                // (Medium plays none as shipped: here it plays them, or not)
                            p.flower_watch = off == 0 && level == Level::Hard;                                             // (Hard's alone)
                        });
                    };
                    results[off] = play_match(spec);
                }
                ASSERT_TRUE(results[0].error.empty() && results[1].error.empty() && results[0].ticks > 2000);
                ASSERT_EQ(results[0].hash, results[1].hash);
                for (size_t seat = 0; seat < results[0].seats.size(); ++seat) ASSERT_EQ(results[0].seats[seat].score, results[1].seats[seat].score);
            }
        }
    } TEST_END();

    TEST_CASE("AI23.23 A Fire Drop That No Hill Walks To Is Wanted By Nobody (The Flowers Of ISLANDS Lie On The Strips Of The Ring): A Fire Power-Up On The Drop Tile Of The First Flower Is Wanted On SMALL, Where A Hill Reaches It, And Not On ISLANDS (A Medium Bot With The Rule Switched On, And Hard)")
    {
        for (const bool islands : {false, true}) {
            for (const Level level : {Level::Medium, Level::Hard}) {
                sim::SimulationEngine sim;
                start_match(sim, islands ? "ISLANDS" : "SMALL", 1, 0x0F);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(level, Style::Economic, [](LevelPlan& p) { p.flower_fire = true; }));
                ASSERT_TRUE(!rig.map().flowers().empty());
                const FlowerInfo& flower = rig.map().flowers()[0];
                ASSERT_EQ(flower.approach[0].reachable(), !islands);
                rig.run(40);
                sim.grid_mut().place_powerup(flower.drop.x, flower.drop.y, static_cast<uint8_t>(sim::AntType::Fire));
                rig.run(20);
                ASSERT_EQ(static_cast<unsigned>(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Fire)]), islands ? 0u : 1u);
            }
        }
    } TEST_END();
}
