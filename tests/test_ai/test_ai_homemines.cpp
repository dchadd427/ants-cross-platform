// The home-mine batch (docs/BOT_PEOPLE.md, "Step 5: home mines on the lanes"; LevelPlan::home_on_route, home_route_lane, home_first; MineTask in src/ants_ai/standard_tasks_build.cpp). The rule that keeps the mines
// round the own hill off the way of the own carriers to a pile leaves no tile on a map whose food lies between the hills, because that way is the enemy's lane to the hill too. The batch lets a mine lie on that way
// where the enemy's shortest way to the hill runs (within home_route_lane cost units of it), and lets a home mine come before a pile mine in turn. The scenes run the real path: a real BotView, a real MineTask, a
// Bomber of the bot on the 60 by 60 field of four hills, the hill of team 0 at (4, 4), a pile of food fourteen tiles east of it, and an enemy worker that walks (so that the bot sees an enemy play).
//
//   AI39.1  the plans: the batch is built and off at every level, Hard's and Medium's numbers, a plan made by hand has none, without_home_mines takes it out again
//   AI39.2  the turn: a home mine comes before a pile mine while fewer home mines were laid than min(mine_home, the other mines laid / 2 + 2), and never without the flag or without mine_home
//   AI39.3  the lane: the shipped rule lays no mine on the way of the carriers; with the batch on a mine lies there, and a narrower lane (Medium's) lays fewer
//   AI39.4  the order: with a pile mine wanted too the bot lays home mines first and alternates; without the flag it lays pile mines until the pile has its share
//   AI39.5  the ring: with the batch on every home mine lies five to nine tiles from the hill, never on the doorstep or the hill
//   AI39.6  the other levels: Easy lays no mine, a Medium of the shipped plan lays none on the way of the carriers
//   AI39.7  the game type 187 plays the brawl: the batch changes nothing there (the same orders with it on and off)
//   AI39.8  two piles: the way to any pile counts whatever another pile says, and a pile's edge is never laid on, even beside an open way
//   AI39.9  the turn rule with no home tile left: the pile mines are still laid
// AI39.1 and AI39.2 are pins (the plans and the turn rule as a function); the others play scenes.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The plan of the scenes: Hard's, with the fights and the fire-in out of the way and mine_home mines at home from the first tick on
LevelPlan scene_plan(uint32_t mine_home = 6) {
    LevelPlan plan = plan_for(Level::Hard);
    plan.assault = plan.raider_hunt = plan.sabotage = plan.rush = false;
    plan.mine_per_pile = plan.mine_gate = plan.behind_mine_gate = 0;
    plan.mine_home = mine_home;
    plan.mine_home_apart = 2;
    plan.mine_home_after = 0;
    return plan;
}

// The world: a Bomber and three workers of team 0, an enemy worker of team 1 that walks, and (`pile`) a pile of 60 units of 25 points on the row of the hill: at column 26, fourteen tiles east of the hill
// (the carriers' way), or at column 38, near the hill of team 1 (the enemy walks to it much sooner than the bot: its mines score high)
void build_world(sim::SimulationEngine& sim, bool pile, int32_t pile_col = 26, const std::vector<std::pair<int32_t, int32_t>>& more = {}) {
    empty_field(sim, 96);
    const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{kFightHills[1].x - 3, kFightHills[1].y + 6});
    sim.apply_command(command_of(sim::CommandType::GroupMove, 1, {foe}, kFightHills[1].x - 3, kFightHills[1].y + 12));
    sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 10});
    for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
    if (pile) add_pile(sim, pile_col, 10, 60, 25);
    for (const auto& at : more) add_pile(sim, at.first, at.second, 60, 25);                          // (the piles after the first: the table keeps their order)
}

// What a scene laid: every tile that held a mine of team 0 at some tick (a mine that the enemy walked onto is gone, the tile was laid), and the task's counters at the end
struct Laid {
    std::set<std::pair<int32_t, int32_t>> tiles;                  // the mines round the hill (within 14 tiles of it)
    std::vector<bool> order;                                      // every mine in the order it was first seen: true for one round the hill, false for one at the pile
    uint32_t planted{0};
    uint32_t home{0};
    TileCoord origin{};
    std::vector<std::vector<TileCoord>> piles;                    // the cells of every pile, in the order of the table
    // the mines on the way of the carriers to the pile: the rows of the hill's gate, between the hill and the pile
    size_t on_way() const {
        size_t n = 0;
        for (const auto& t : tiles) n += std::abs(t.second - origin.y) <= 2 && t.first > origin.x && t.first < 26 ? 1u : 0u;
        return n;
    }
    int32_t nearest() const {
        int32_t best = 1 << 20;
        for (const auto& t : tiles) best = std::min(best, TileCoord{t.first, t.second}.chebyshev_dist(origin));
        return tiles.empty() ? -1 : best;
    }
    int32_t farthest() const {
        int32_t far = -1;
        for (const auto& t : tiles) far = std::max(far, TileCoord{t.first, t.second}.chebyshev_dist(origin));
        return far;
    }
};

Laid play(const LevelPlan& plan, bool pile, uint64_t ticks, Level level = Level::Hard, int32_t pile_col = 26, const std::vector<std::pair<int32_t, int32_t>>& more = {}) {
    sim::SimulationEngine sim;
    build_world(sim, pile, pile_col, more);
    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
    Laid out;
    std::set<std::pair<int32_t, int32_t>> seen;
    rig.after_sim_tick = [&] {
        const BotView view = BotView::build(sim, 0, &rig.map());
        for (const BombView& b : view.bombs()) {
            if (b.owner != 0 || !seen.insert({b.tile.x, b.tile.y}).second) continue;
            const bool home = b.tile.chebyshev_dist(rig.map().hill(0).origin) <= 14;                  // (the pile's mines and the gate's lie further out)
            out.order.push_back(home);
            if (home) out.tiles.insert({b.tile.x, b.tile.y});
        }
    };
    rig.run(ticks);
    out.origin = rig.map().hill(0).origin;
    for (uint32_t i = 0; rig.map().pile(i) != nullptr; ++i) out.piles.push_back(rig.map().pile(i)->cells);
    out.planted = rig.as<StandardBot>().mines().planted();
    out.home = rig.as<StandardBot>().mines().home_planted();
    return out;
}

LevelPlan with_batch(LevelPlan plan, bool on_route, bool first, int32_t lane = 60) {
    plan.home_on_route = on_route;
    plan.home_first = first;
    plan.home_route_lane = lane;
    return plan;
}

}  // namespace

void run_homemines_tests() {
    TEST_CASE("AI39.1 The Plans: The Home-Mine Batch Is Built And Off At Every Level (Hard's Lane Is The Rule's Own 60 Cost Units, Medium's 20); A Plan Made By Hand Has None; without_home_mines Puts A Plan Back")
    {
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.home_on_route);
        ASSERT_FALSE(by_hand.home_first);
        ASSERT_EQ(by_hand.home_route_lane, 60);
        ASSERT_EQ(plan_for(Level::Hard).home_route_lane, MineTask::kHomeLaneSlack);                          // (Hard's lane is the widest a candidate has: it filters nothing; the two numbers move together)
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_FALSE(p.home_on_route);
            ASSERT_FALSE(p.home_first);
            ASSERT_EQ(p.home_route_lane, level == Level::Medium ? 20 : 60);
            ASSERT_TRUE(p.mine_home_off_route);                                                              // (the rule that the batch opens stays on in the plans)
            for (uint32_t seed = 1; seed <= 12; ++seed) {                                                    // whatever the style and the variations of the bot's own
                BotRng seat_rng(seat_seed(seed, static_cast<uint8_t>(seed % 4), "standard"));
                StandardBot bot(level, Style::Random);
                bot.start(BotContext{static_cast<uint8_t>(seed % 4), profile_for(level), seat_rng.next(), nullptr});
                const LevelPlan& q = bot.tactics().plan;
                ASSERT_FALSE(q.home_on_route || q.home_first);
                ASSERT_EQ(q.home_route_lane, level == Level::Medium ? 20 : 60);
            }
        }
        ASSERT_EQ(plan_for(Level::Easy).mine_home, 0u);                                                      // (Easy has no mines at home to open)
        LevelPlan on = plan_for(Level::Hard);
        on.home_on_route = on.home_first = true;
        on.home_route_lane = 20;
        without_home_mines(on);
        ASSERT_FALSE(on.home_on_route);
        ASSERT_FALSE(on.home_first);
        ASSERT_EQ(on.home_route_lane, 20);                                                                   // (a number is not a switch: only the two flags go)
        ASSERT_EQ(on.mine_home, plan_for(Level::Hard).mine_home);                                            // (and the rest of the mines is as it was)
    } TEST_END();

    TEST_CASE("AI39.2 The Turn: A Home Mine Comes Before A Pile Mine While Fewer Home Mines Were Laid Than min(mine_home, The Other Mines Laid / 2 + 2); Never Without The Flag Or Without mine_home")
    {
        LevelPlan p = plan_for(Level::Hard);
        p.mine_home = 12;
        ASSERT_FALSE(home_mine_turn(p, 0, 0));                                                               // the flag is off: the pile mines come first, as they did
        ASSERT_FALSE(home_mine_turn(p, 0, 40));
        p.home_first = true;
        ASSERT_TRUE(home_mine_turn(p, 0, 0));                                                                // the first two come at once
        ASSERT_TRUE(home_mine_turn(p, 1, 0));
        ASSERT_FALSE(home_mine_turn(p, 2, 0));                                                               // ... and then the pile mines earn the next
        ASSERT_FALSE(home_mine_turn(p, 2, 1));
        ASSERT_TRUE(home_mine_turn(p, 2, 2));                                                                // two pile mines laid: three home mines
        ASSERT_FALSE(home_mine_turn(p, 3, 2));
        ASSERT_TRUE(home_mine_turn(p, 3, 4));
        ASSERT_TRUE(home_mine_turn(p, 11, 100));                                                             // never more than mine_home in all
        ASSERT_FALSE(home_mine_turn(p, 12, 100));
        ASSERT_FALSE(home_mine_turn(p, 20, 100));
        p.mine_home = 1;
        ASSERT_TRUE(home_mine_turn(p, 0, 0));
        ASSERT_FALSE(home_mine_turn(p, 1, 50));
        p.mine_home = 0;                                                                                     // no home mines in the plan: nothing to put first
        ASSERT_FALSE(home_mine_turn(p, 0, 0));
    } TEST_END();

    TEST_CASE("AI39.3 The Lane: The Shipped Rule Lays No Mine On The Way Of The Carriers To A Pile; With The Batch On A Mine Lies There, And A Narrower Lane Lays Fewer")
    {
        const Laid shipped = play(with_batch(scene_plan(), false, false), true, 4000);
        ASSERT_EQ(shipped.on_way(), 0u);                                                                     // (the rule: the carriers' way is no place for a mine)
        const Laid wide = play(with_batch(scene_plan(), true, false, 60), true, 4000);
        ASSERT_TRUE(wide.on_way() >= 3u);
        ASSERT_TRUE(wide.home >= 3u);
        const Laid narrow = play(with_batch(scene_plan(), true, false, 0), true, 4000);
        ASSERT_TRUE(narrow.on_way() < wide.on_way());                                                        // (Medium's 20 is between: only the tiles on the enemy's own line)
        const Laid medium = play(with_batch(scene_plan(), true, false, 20), true, 4000);
        ASSERT_TRUE(medium.on_way() <= wide.on_way());
        ASSERT_TRUE(medium.on_way() >= narrow.on_way());
        const Laid no_pile = play(with_batch(scene_plan(), true, false), false, 4000);                      // with no pile there is no way to keep clear: the batch changes nothing
        const Laid no_pile_off = play(with_batch(scene_plan(), false, false), false, 4000);
        ASSERT_TRUE(no_pile.tiles == no_pile_off.tiles);
        ASSERT_TRUE(no_pile.planted >= 1u);
    } TEST_END();

    TEST_CASE("AI39.4 The Order: With A Pile Mine Wanted Too The Bot Lays Home Mines First And Alternates; Without The Flag It Lays Pile Mines Until The Pile Has Its Share")
    {
        LevelPlan plan = scene_plan(6);
        plan.mine_per_pile = 10;
        plan.war_free_only = false;
        const auto first_n = [](const Laid& laid, size_t n) {                                                // how many of the first n mines were laid round the hill
            size_t home = 0;
            for (size_t i = 0; i < n && i < laid.order.size(); ++i) home += laid.order[i] ? 1u : 0u;
            return home;
        };
        const Laid off = play(with_batch(plan, true, false), true, 3000, Level::Hard, 38);
        ASSERT_TRUE(off.order.size() >= 6u);
        ASSERT_EQ(first_n(off, 6), 0u);                                                                      // flag off: the pile mines come first, the home mines when the pile has none to ask for
        const Laid on = play(with_batch(plan, true, true), true, 3000, Level::Hard, 38);
        ASSERT_TRUE(on.order.size() >= 6u);
        ASSERT_TRUE(first_n(on, 4) >= 2u);                                                                   // flag on: the home mines come at once (the first mine is laid before the bot has seen the enemy walk) ...
        ASSERT_TRUE(first_n(on, 6) >= 3u && first_n(on, 6) <= 4u);                                           // ... and the pile mines alternate with them
        ASSERT_TRUE(on.home <= 6u);
    } TEST_END();

    TEST_CASE("AI39.5 The Ring: With The Batch On Every Home Mine Lies Five To Nine Tiles From The Hill, Never On The Doorstep Or The Hill, Whatever The Number Of Mines Or The Lane")
    {
        for (const int32_t lane : {0, 60}) {
            LevelPlan plan = scene_plan(40);
            plan.mine_home_apart = 1;
            const Laid laid = play(with_batch(plan, true, false, lane), true, 6000);
            ASSERT_TRUE(laid.tiles.size() >= 4u);
            ASSERT_TRUE(laid.nearest() >= 5);
            ASSERT_TRUE(laid.farthest() <= 9);
        }
    } TEST_END();

    TEST_CASE("AI39.6 The Other Levels: Easy Lays No Mine; A Medium Of The Shipped Plan Lays None On The Way Of The Carriers, And With The Batch On Only On The Narrow Lane")
    {
        const Laid easy = play(plan_for(Level::Easy), true, 4000, Level::Easy);
        ASSERT_EQ(easy.planted, 0u);
        LevelPlan medium = plan_for(Level::Medium);
        medium.mine_home_after = 0;
        medium.war_free_only = false;
        const Laid shipped = play(medium, true, 4000, Level::Medium);
        ASSERT_EQ(shipped.on_way(), 0u);
        LevelPlan on = medium;
        on.home_on_route = true;                                                                             // (Medium's own lane, 20)
        const Laid milder = play(on, true, 4000, Level::Medium);
        LevelPlan hard_lane = medium;
        hard_lane.home_on_route = true;
        hard_lane.home_route_lane = 60;
        const Laid wider = play(hard_lane, true, 4000, Level::Medium);
        ASSERT_TRUE(milder.on_way() <= wider.on_way());
        ASSERT_TRUE(milder.farthest() <= 9);
    } TEST_END();

    TEST_CASE("AI39.7 The Game Type 187: The Bot Plays The Brawl And Lays No Mine; The Batch On Or Off Gives The Same Orders")
    {
        std::vector<std::pair<uint64_t, sim::Command>> orders[2];
        uint32_t planted[2] = {0, 0};
        for (int on = 0; on < 2; ++on) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            build_world(sim, false);
            LevelPlan plan = with_batch(scene_plan(), on == 1, on == 1);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3000);
            orders[on] = rig.proposed;
            planted[on] = rig.as<StandardBot>().mines().planted();
        }
        ASSERT_EQ(planted[0], 0u);
        ASSERT_EQ(planted[1], 0u);
        ASSERT_EQ(orders[0].size(), orders[1].size());
        for (size_t i = 0; i < orders[0].size() && i < orders[1].size(); ++i) {
            ASSERT_EQ(orders[0][i].first, orders[1][i].first);
            ASSERT_EQ(static_cast<int>(orders[0][i].second.type), static_cast<int>(orders[1][i].second.type));
            ASSERT_EQ(orders[0][i].second.tile_x, orders[1][i].second.tile_x);
            ASSERT_EQ(orders[0][i].second.tile_y, orders[1][i].second.tile_y);
        }
    } TEST_END();

    TEST_CASE("AI39.8 Two Piles: A Tile Is On The Way Of The Carriers If It Is On The Way To Any Pile, Whatever Another Pile Says; A Tile At A Pile's Edge Is Never Laid, Even When The Way To An Earlier Pile Is Open")
    {
        {   // (a) the lane of 20: the way to pile A (east) outside the lane stays shut, with a second pile far south that the tiles are not on the way to
            const LevelPlan narrow = with_batch(scene_plan(), true, false, 20);
            const Laid one = play(narrow, true, 4000);
            const Laid two = play(narrow, true, 4000, Level::Hard, 26, {{4, 40}});
            ASSERT_TRUE(one.on_way() >= 1u);
            ASSERT_EQ(two.on_way(), one.on_way());
        }
        {   // (b) the lane of 60 opens the way to pile A (the first of the table); pile B lies beside the same way near the hill: no mine within two tiles of a cell of either
            LevelPlan wide = with_batch(scene_plan(40), true, false, 60);
            wide.mine_home_apart = 1;
            const Laid laid = play(wide, true, 5000, Level::Hard, 26, {{12, 6}});
            ASSERT_EQ(laid.piles.size(), 2u);
            ASSERT_TRUE(laid.tiles.size() >= 4u);
            for (const auto& t : laid.tiles) {
                const TileCoord at{t.first, t.second};
                for (const auto& cells : laid.piles) {
                    for (const TileCoord& c : cells) ASSERT_TRUE(at.chebyshev_dist(c) > 2);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI39.9 A Full Home Area On The Home Turn: With The Turn Rule On And No Home Tile Left (The Mines Keep Apart), The Pile Mines Are Still Laid")
    {
        LevelPlan plan = scene_plan(6);
        plan.mine_per_pile = 10;
        plan.war_free_only = false;
        plan.mine_home_apart = 40;                                                                           // every tile is too near a mine that stands: the home area has no tile left (and the home mines laid stay under the turn's share)
        const Laid laid = play(with_batch(plan, true, true), true, 3000, Level::Hard, 38);
        ASSERT_TRUE(laid.home < 2u);
        ASSERT_TRUE(laid.planted >= laid.home + 3u);                                                         // (the turn never starves the pile: three or more pile mines)
    } TEST_END();
}
