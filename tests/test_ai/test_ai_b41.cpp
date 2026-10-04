// Tests of the standard bot (B4-1, AI7.x), quick enough for suite 2.20: the new members of the view (the power-ups and who stands on them, the bombs and the fire walls), the
// controller's new filters (an attack needs an enemy ant on its tile, a move onto a power-up tile is a planned pick-up or nothing), the ledger's ranks, and the tasks of the standard
// bot in hand-made worlds and on TREASURE (strike back, never an ant that stands on a power-up, the thief hole's fire walls, pick-ups, raids, the guard, the sides of the map, the
// counters to the enemy's walls and bombs). The whole matches of the acceptance (A1 .. A5) are suite 2.25.
//
//   AI7.1   BotView::powerups() is the engine's grid at every look (also while a dropper drops and typed ants are made), in reading order, and a copy of a view keeps it
//   AI7.2   standing(): an ant on a power-up that is not walking (own and other teams, idle and on guard), not one that is only crossing the tile on its way to take it
//   AI7.3   the controller refuses an attack whose tile holds no ant of another team (nobody, an own ant, an ally, an ant on a hill tile, an ant that stands on a power-up)
//   AI7.4   the controller refuses a move onto a power-up tile unless it is a planned pick-up (one ant, a power-up there), and a special order or attack onto one
//   AI7.5   the ledger's ranks: a task takes an ant from a task of a lower rank and from no other; the economy forgets the ant that was taken
//   AI7.6   strike back: who is sent (the level's number, Combat Ants first, no carrier, no hurt ant), the enemy is hurt, the ants go back to the pool
//   AI7.7   never an ant that stands on a power-up (no order, no fight)
//   AI7.8   an attack that shows "can't go" means that no order reaches the target: it is left alone for 900 ticks
//   AI7.9   interception of a thief (the plan flag, off in every level): a standing thief is hit, a walking one is never chased
//   AI7.10  a hit carrier is sent home at once, once per try
//   AI7.11  the fire walls of the thief hole by level (the triggers), the Fire Ant, the economy gets it back; mud, the last 400 ticks
//   AI7.12  the three tiles in front of the hole stop a raid (the engine's own order), the state of the tiles, the deposits go on with the walls up
//   AI7.13  the walls are renewed before they burn out (never two down at once), Easy lights them again; no wall on a power-up tile
//   AI7.14  pick-ups: the shortest walk, one ant by a plain click, the longest trip, a contest, an ant that stands on the power-up, a failed trip, the dropper that changes the kind
//   AI7.15  raids: the leading team with points to take that is open, not an ally, reachable, in time, and (Hard) not guarded; a whole raid in the engine
//   AI7.16  the guard: the three posts, ordered once, a power-up tile is no post, no guard at Easy
//   AI7.17  the sides of the map (TREASURE, a match of fewer teams, ISLANDS, a tie, a shut-in power-up)
//   AI7.18  securing the own side on TREASURE (Medium and Hard: Fire and Bomber; Easy: the Fire for the walls only; nobody steals)
//   AI7.19  the counter to enemy bombs: a Bomber defuses, a healthy idle worker sets off, never a carrier or a hurt ant, never own, allied or far bombs, a bomb nothing reaches
//   AI7.20  the counter to enemy fire walls: put out near the hill or a pile, never the own three, waiting without a Fire Ant, a pile that a ring cuts off
//   AI7.21  BotView::bombs() and fire_walls() are the engine's grid at every look
//   AI7.22  Thief and Combat pick-ups follow the enemy (an enemy that plays, an enemy Combat Ant in sight), the raids, the guard post
//   AI7.25  a Thief that harvests is taken for a raid as soon as a hill has loot and a hole that is open: its loop (walk to the pile, bite, walk home) is the engine's and it is never idle
//   AI7.24  the double-thief opening at Hard: with idle neighbours the Hard bot has two Thief ants (its own side's and an unguarded one), Medium one, Easy none
//   AI7.23  the defence against the double-thief opening: the own side's Thief power-up is taken before the neighbour's second thief gets there (Medium, Hard), not at Easy
#include "ai_test.hpp"
#include "b41_helpers.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "ants_ai/arena.hpp"
#include "ants_ai/idle_bot.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tasks.hpp"
#include "bench_aggressor.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The oracle for the view's power-ups: a scan of the engine's own grid, tile by tile, written down independently of BotView::build (the cells' own predicate, the kind from the
// tile's number)
struct Oracle {
    TileCoord tile;
    sim::AntType kind;
};
std::vector<Oracle> scan_powerups(const sim::SimulationEngine& sim) {
    std::vector<Oracle> out;
    const sim::Grid& g = sim.grid();
    for (int32_t y = 0; y < static_cast<int32_t>(g.height()); ++y) {
        for (int32_t x = 0; x < static_cast<int32_t>(g.width()); ++x) {
            if (g.has_powerup_at(TileCoord{x, y}) && g.get_powerup_type(TileCoord{x, y}) >= 1 && g.get_powerup_type(TileCoord{x, y}) <= 5) {
                out.push_back(Oracle{TileCoord{x, y}, static_cast<sim::AntType>(g.get_powerup_type(TileCoord{x, y}))});
            }
        }
    }
    return out;
}

const AntView* find_in(const std::vector<AntView>& ants, uint32_t id) {
    for (const AntView& a : ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// A controller with a recording sink that FORWARDS to the engine (the commands that pass the filter are applied): `bot` sits at seat 0 with `level`
struct Seat0 {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    ScriptBot* script{nullptr};
    explicit Seat0(const std::function<void(sim::SimulationEngine&)>& build, ScriptBot::Think think, Level level = Level::Hard) {
        build(sim);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        auto bot = std::make_unique<ScriptBot>(std::move(think));
        script = bot.get();
        BotSpec spec;
        spec.seat = 0;
        spec.level = level;
        std::string why;
        if (!ctl->add(spec, std::move(bot), *sink, why)) std::cout << "    add failed: " << why << "\n";
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
        }
    }
    size_t released(CommandType t) const {
        size_t n = 0;
        for (const auto& e : sink->log) n += e.second.type == t ? 1u : 0u;
        return n;
    }
};


}  // namespace

void run_b41_tests() {
    TEST_CASE("AI7.1 The View's Power-Ups Are The Engine's Grid At Every Look: Tile, Kind, Reading Order; While A Dropper Drops (MEDIUM, SMALL, GAUNTLET, ISLANDS) And On TREASURE; Who Stands On One Is An Ant Of The View On That Tile; A Copy Keeps The List And Its Helpers Work Without The Engine") {
        for (const char* map : {"MEDIUM", "SMALL", "GAUNTLET", "ISLANDS", "TREASURE"}) {
            sim::SimulationEngine sim;
            start_match(sim, map, 5, 0x0F);
            size_t looks = 0;
            size_t most = 0;
            std::set<std::string> seen_lists;
            for (int t = 0; t < 1900; ++t) {
                sim.tick();
                sim.clear_news_events();
                sim.clear_audio_events();
                if (t % 7 != 0) continue;
                const std::vector<Oracle> want = scan_powerups(sim);
                for (uint8_t seat = 0; seat < 4; seat += 3) {
                    const BotView v = BotView::build(sim, seat);
                    ASSERT_EQ(v.powerups().size(), want.size());
                    std::string text;
                    for (size_t i = 0; i < want.size(); ++i) {
                        ASSERT_TRUE(v.powerups()[i].tile == want[i].tile && v.powerups()[i].kind == want[i].kind);
                        if (i > 0) ASSERT_TRUE(v.powerups()[i - 1].tile.y < v.powerups()[i].tile.y || (v.powerups()[i - 1].tile.y == v.powerups()[i].tile.y && v.powerups()[i - 1].tile.x < v.powerups()[i].tile.x));
                        ASSERT_TRUE(v.powerup_at(want[i].tile) == &v.powerups()[i]);
                        text += std::to_string(want[i].tile.x) + "," + std::to_string(want[i].tile.y) + "," + std::to_string(static_cast<int>(want[i].kind)) + ";";
                    }
                    ASSERT_TRUE(v.powerup_at(TileCoord{0, 0}) == nullptr || want.empty() == false);
                    const BotView copy = v;                                                  // a copy has no engine: the list and the helpers stay
                    ASSERT_EQ(copy.powerups().size(), want.size());
                    for (size_t i = 0; i < want.size(); ++i) ASSERT_TRUE(copy.powerup_at(want[i].tile) != nullptr && copy.powerup_at(want[i].tile)->kind == want[i].kind);
                    if (seat == 0) {
                        ++looks;
                        most = std::max(most, want.size());
                        seen_lists.insert(text);
                    }
                }
            }
            ASSERT_TRUE(looks > 100);
            if (std::string(map) == "TREASURE") ASSERT_EQ(most, 20u);                         // 4 of each kind, no dropper
            if (std::string(map) == "ISLANDS") ASSERT_TRUE(most >= 40u && most <= 42u);       // 40 placed, two droppers that hold one each at most
            if (std::string(map) == "GAUNTLET") ASSERT_TRUE(most >= 10u && most <= 11u);      // 10 placed, one dropper
            if (std::string(map) == "SMALL") ASSERT_TRUE(seen_lists.size() >= 2);             // the droppers drop: the list changed during the match
        }
        // MEDIUM's dropper (tile 29, 29: a drop every 9 s) replaces what lies there: the view followed it through several kinds, and a drop that lands under an ant that stands there is
        // under that ant
        {
            sim::SimulationEngine sim;
            start_match(sim, "MEDIUM", 9, 0x0F);
            std::set<int> kinds;
            for (int t = 0; t < 4000; ++t) {
                sim.tick();
                sim.clear_news_events();
                sim.clear_audio_events();
                if (t % 5 != 0) continue;
                const BotView v = BotView::build(sim, 0);
                const PowerUpView* p = v.powerup_at(TileCoord{29, 29});
                if (p != nullptr) kinds.insert(static_cast<int>(p->kind));
            }
            ASSERT_TRUE(kinds.size() >= 3);
        }
        // a power-up that a typed ant leaves when it dies appears in the view: a Combat Ant is killed, its power-up lies on a free neighbour tile
        {
            sim::SimulationEngine sim;
            build_world(sim, 3, 1);
            const uint32_t combat = sim.spawn_unit(1, sim::AntType::Combat, TileCoord{30, 30});
            ASSERT_TRUE(BotView::build(sim, 0).powerups().empty());
            sim.kill_unit(combat);
            tick_all(sim, 120);
            const BotView v = BotView::build(sim, 0);
            ASSERT_EQ(v.powerups().size(), 1u);
            ASSERT_TRUE(v.powerups()[0].kind == sim::AntType::Combat && v.powerups()[0].tile.chebyshev_dist(TileCoord{30, 30}) == 1);
        }
    } TEST_END();

    TEST_CASE("AI7.2 Standing: An Ant On A Power-Up That Is Not Walking Stands (Own, Another Team's, A Combat Ant On Guard); One That Is Only Crossing The Tile On Its Way To Take The Power-Up Does Not, Nor One Next To It; The View Lists Who Stands On Each") {
        sim::SimulationEngine sim;
        build_world(sim, 4, 3);
        sim.grid_mut().place_powerup(20, 20, 3);                                              // a Thief power-up with an own ant standing on it
        sim.grid_mut().place_powerup(22, 20, 4);                                              // a Combat power-up with an enemy worker standing on it
        sim.grid_mut().place_powerup(24, 20, 2);                                              // a Fire power-up with an enemy Combat Ant on it (the guard state)
        sim.grid_mut().place_powerup(26, 20, 1);                                              // a Bomber power-up: free
        const uint32_t own = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 20});
        const uint32_t guard = sim.spawn_unit(1, sim::AntType::Combat, TileCoord{24, 20});
        const uint32_t beside = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 20});   // next to the free one
        tick_all(sim, 3);
        BotView v = BotView::build(sim, 0);
        ASSERT_EQ(v.powerups().size(), 4u);
        const AntView* a_own = find_in(v.mine(), own);
        const AntView* a_foe = find_in(v.others(), foe);
        const AntView* a_guard = find_in(v.others(), guard);
        const AntView* a_beside = find_in(v.mine(), beside);
        ASSERT_TRUE(a_own != nullptr && a_foe != nullptr && a_guard != nullptr && a_beside != nullptr);
        ASSERT_TRUE(v.standing(*a_own) && v.standing(*a_foe) && v.standing(*a_guard));
        ASSERT_FALSE(v.standing(*a_beside));
        ASSERT_TRUE(a_guard->state == sim::UnitState::GuardIdle);                              // a Combat Ant at rest: guard
        ASSERT_TRUE(v.powerup_at(TileCoord{20, 20})->standing_ant == own && v.powerup_at(TileCoord{20, 20})->standing_team == 0);
        ASSERT_TRUE(v.powerup_at(TileCoord{22, 20})->standing_ant == foe && v.powerup_at(TileCoord{22, 20})->standing_team == 1);
        ASSERT_TRUE(v.powerup_at(TileCoord{24, 20})->standing_ant == guard && v.powerup_at(TileCoord{24, 20})->standing_team == 1);
        ASSERT_TRUE(v.powerup_at(TileCoord{26, 20})->standing_ant == 0 && v.powerup_at(TileCoord{26, 20})->standing_team == 255);
        const BotView copy = v;                                                                // the helper works on a copy (no grid)
        ASSERT_FALSE(copy.has_grid());
        ASSERT_TRUE(copy.standing(*find_in(copy.mine(), own)) && !copy.standing(*find_in(copy.mine(), beside)));
        // an ant that walks onto the free power-up: the tick it crosses into the tile (its tile is the power-up's, the power-up is still there) it is walking, not standing; then it takes
        // the power-up, which is gone, and the ant is no standing ant either
        ASSERT_TRUE(sim.apply_command(command_of(CommandType::GroupMove, 0, {beside}, 26, 20)).accepted());
        bool crossed = false;
        bool taken = false;
        for (int t = 0; t < 60; ++t) {
            tick_all(sim, 1);
            const BotView w = BotView::build(sim, 0);
            const AntView* a = find_in(w.mine(), beside);
            ASSERT_TRUE(a != nullptr);
            if (a->tile == TileCoord{26, 20} && w.powerup_at(TileCoord{26, 20}) != nullptr) {
                crossed = true;
                ASSERT_FALSE(w.standing(*a));                                                  // walking over the tile it will take
                ASSERT_TRUE(w.powerup_at(TileCoord{26, 20})->standing_ant == 0);
            }
            if (w.powerup_at(TileCoord{26, 20}) == nullptr) {
                taken = true;
                ASSERT_FALSE(w.standing(*a));
                break;
            }
        }
        ASSERT_TRUE(crossed && taken);
        ASSERT_TRUE(BotView::build(sim, 0).powerups().size() == 3 || BotView::build(sim, 0).powerups().size() == 4);   // (the old kind of a typed ant is dropped: a Worker drops none)
    } TEST_END();

    TEST_CASE("AI7.3 The Controller's Attack Filter: An Attack Is Released Only When An Ant Of Another Team Stands On Its Tile (Nobody There, An Own Ant, An Ally, An Enemy On A Hill Tile And An Enemy That Stands On A Power-Up Are Refused); What The HUD Sends For A Click On An Enemy Is A GroupAttack On The Ant's Tile") {
        const auto build = [](sim::SimulationEngine& sim) {
            build_world(sim, 6, 6);
            sim.grid_mut().place_powerup(30, 30, 3);
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 30});            // an enemy that stands on a power-up (the trick)
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 20});            // an enemy on open ground
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{6, 6});              // an enemy on a tile of team 0's own mound (4 .. 7 x 4 .. 7)
            sim.spawn_unit(2, sim::AntType::Worker, TileCoord{25, 25});            // a third team's ant (an ally in the second run)
        };
        for (const bool allied : {false, true}) {
            const std::vector<std::pair<TileCoord, bool>> expect = {
                {tc(20, 20), true},       // an enemy ant on open ground
                {tc(21, 20), false},      // nobody
                {tc(30, 30), false},      // an enemy standing on a power-up: a person's click is possible (and ends in "Can't go there."), the bot never sends it
                {tc(6, 6), false},        // an enemy on a tile of a hill: the cursor over it is the plain move cursor
                {tc(25, 25), !allied}     // an ant of another team; an ally's needs the break of the alliance first
            };
            size_t index = 0;
            Seat0 w(build, [&](const BotView& v, Orders& o) {
                if ((v.tick() - 1) % 60 != 0 || index >= expect.size()) return;
                o.attack({v.mine()[0].id}, expect[index++].first, Priority::Urgent);
            }, Level::Hard);
            if (allied) {
                w.sim.form_alliance(0, 2);
                ASSERT_EQ(w.sim.get_ally_id(0), 2);
            }
            w.run(expect.size() * 60 + 40);
            ASSERT_EQ(index, expect.size());
            size_t want_released = 0;
            for (const auto& e : expect) want_released += e.second ? 1u : 0u;
            const BotController::SeatStats& st = w.ctl->stats(0);
            ASSERT_EQ(st.released, want_released);
            ASSERT_EQ(st.filtered, expect.size() - want_released);
            ASSERT_EQ(st.rejected, 0u);
            ASSERT_EQ(w.released(CommandType::GroupAttack), want_released);
            for (const auto& e : w.sink->log) ASSERT_TRUE(e.second.tile_x == 20 || (!allied && e.second.tile_x == 25));
        }
        // an attack on the tile of the bot's own ant is refused as well (there is no ant of another team on it)
        {
            Seat0 w(build, [&](const BotView& v, Orders& o) {
                if (v.tick() != 1) return;
                o.attack({v.mine()[1].id}, v.mine()[0].tile, Priority::Urgent);
            });
            w.run(60);
            ASSERT_EQ(w.ctl->stats(0).filtered, 1u);
            ASSERT_EQ(w.ctl->stats(0).released, 0u);
        }
    } TEST_END();

    TEST_CASE("AI7.4 The Controller's Power-Up Guard: A Move Onto A Power-Up Tile Passes Only As A Planned Pick-Up Of ONE Ant With A Power-Up On The Tile; A Group, A Special Order, An Attack Or A Pick-Up Of Nothing Is Refused; A Move Next To One Passes; A Planned Pick-Up Takes It") {
        const auto build = [](sim::SimulationEngine& sim) {
            build_world(sim, 7, 6);
            sim.grid_mut().place_powerup(20, 20, 3);
        };
        size_t step = 0;
        uint32_t taker = 0;
        Seat0 w(build, [&](const BotView& v, Orders& o) {
            if ((v.tick() - 1) % 60 != 0 || step > 8) return;
            const uint32_t a0 = v.mine()[0].id;
            const uint32_t a1 = v.mine()[1].id;
            const uint32_t a2 = v.mine()[2].id;
            switch (step++) {
                case 0: o.move({a0}, tc(20, 20)); break;                                                      // refused: a click on a power-up that is no planned pick-up
                case 1: o.move({a0, a1}, tc(20, 20)); break;                                                  // refused: a group
                case 2: o.special(a0, tc(20, 20)); break;                                                     // refused: a special order onto a power-up tile
                case 3: o.pick_up(a0, tc(21, 20)); break;                                                     // refused: nothing to take there
                case 4: o.move({a0}, tc(21, 20)); break;                                                      // passes: next to the power-up
                case 5: o.push_unchecked(command_of(CommandType::GroupMove, 0, {a0, a1}, 20, 20), Priority::Normal, true); break;   // refused: a pick-up that names two ants
                case 6: o.push_unchecked(command_of(CommandType::GroupAttack, 0, {a0}, 20, 20), Priority::Normal, true); break;     // refused: a pick-up that is an attack
                case 7:                                                                                       // passes: ONE ant, a power-up on the tile
                    taker = a2;
                    o.pick_up(a2, tc(20, 20));
                    break;
                default: o.attack({a0}, tc(20, 20), Priority::Urgent); break;                                 // refused: no ant stands there
            }
        }, Level::Hard);
        w.run(8 * 60 + 400);
        ASSERT_EQ(step, 9u);
        const BotController::SeatStats& st = w.ctl->stats(0);
        ASSERT_EQ(st.released, 2u);
        ASSERT_EQ(st.filtered, 7u);
        ASSERT_EQ(st.rejected, 0u);
        ASSERT_TRUE(taker != 0);
        const BotView v = BotView::build(w.sim, 0);
        ASSERT_TRUE(find_in(v.mine(), taker) != nullptr && find_in(v.mine(), taker)->type == sim::AntType::Thief);     // the planned pick-up took it
        ASSERT_TRUE(v.powerups().empty());
    } TEST_END();

    TEST_CASE("AI7.5 The Ledger's Ranks: A Task Takes An Ant From A Task Of A Lower Rank And From No Other; The Economy Forgets An Ant That Was Taken (And Orders It Again When It Is Given Back); Without A Rank Nothing Is Taken") {
        AntLedger ledger;
        ledger.set_rank(1, 1);
        ledger.set_rank(2, 5);
        ledger.set_rank(3, 5);
        ASSERT_TRUE(ledger.claim(10, 1));
        ASSERT_FALSE(ledger.claim(10, 2));                                                    // claim never takes
        ASSERT_TRUE(ledger.take(10, 2));                                                      // 5 > 1: taken
        ASSERT_EQ(ledger.owner(10), 2u);
        ASSERT_FALSE(ledger.take(10, 3));                                                     // equal rank: no
        ASSERT_FALSE(ledger.take(10, 1));                                                     // lower rank: no
        ASSERT_EQ(ledger.owner(10), 2u);
        ASSERT_TRUE(ledger.take(10, 2));                                                      // its own ant: yes
        ASSERT_TRUE(ledger.take(11, 3));                                                      // a free ant is simply taken
        ASSERT_FALSE(ledger.take(11, kNoTask));
        AntLedger plain;                                                                      // no ranks: every task has rank 0 and takes nothing
        ASSERT_TRUE(plain.claim(1, 7));
        ASSERT_FALSE(plain.take(1, 8));
        ASSERT_EQ(plain.owner(1), 7u);
        // the economy: an ant that a task of a higher rank took is not its own any more
        sim::SimulationEngine sim;
        build_world(sim, 2, 3);
        const int32_t pile = place_crackers(sim, 14, 12, 25);
        (void)pile;
        const MapInfo map(sim);
        const Profile profile = profile_for(Level::Hard);
        AntLedger l2;
        l2.set_rank(1, 1);
        l2.set_rank(2, 5);
        HarvestTask harvest(1);
        Orders orders;
        BotView view = BotView::build(sim, 0, &map);
        TaskContext ctx{view, orders, l2, profile, map, 0};
        harvest.step(ctx);
        ASSERT_EQ(harvest.working(), 3u);
        const uint32_t victim = ants_of(sim, 0)[0];
        ASSERT_TRUE(l2.take(victim, 2));
        view = BotView::build(sim, 0, &map);
        TaskContext ctx2{view, orders, l2, profile, map, 0};
        harvest.step(ctx2);
        ASSERT_EQ(harvest.working(), 2u);                                                     // the order of the ant that was taken is forgotten
        ASSERT_EQ(l2.owner(victim), 2u);                                                      // and the ant stays with its new task (not ordered again, not released)
        l2.release(victim, 2);
        harvest.step(ctx2);
        ASSERT_EQ(harvest.working(), 3u);                                                     // given back, it is idle in the pool again
    } TEST_END();

    TEST_CASE("AI7.6 Strike Back: An Enemy Hits An Own Ant, The Nearest Healthy Ants That Carry Nothing (A Combat Ant First) Are Ordered To Attack It, As Many As The Level Says (Easy 1, Medium 2, Hard 3); Carriers And Hurt Ants Are Never Sent; The Enemy Is Hurt; When It Is Gone The Ants Go Back To The Pool") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
            const uint32_t near1 = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 19});
            const uint32_t near2 = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 20});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{29, 20});                          // far away: never among the first
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 22});
            const uint32_t weak = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{21, 22});
            const uint32_t combat = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 24});
            sim.get_unit(carrier).pick_up_food(1, 25);
            sim.get_unit(weak).hp = 3;
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 27});
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            const size_t want = level == Level::Easy ? 1u : level == Level::Medium ? 2u : 3u;
            bool hit = false;
            for (int t = 0; t < 700; ++t) {
                keep_attacking(sim, 1, enemy, victim);
                rig.tick();
                if (!hit && sim.get_unit(victim).hp < 10) hit = true;
            }
            ASSERT_TRUE(hit);                                                                 // the enemy did hit
            const std::pair<uint64_t, Command>* first = nullptr;
            std::set<uint32_t> ever;
            for (const auto& e : rig.sent) {
                if (e.second.type != CommandType::GroupAttack) continue;
                if (first == nullptr) first = &e;
                ever.insert(e.second.ants.begin(), e.second.ants.end());
            }
            ASSERT_TRUE(first != nullptr);
            ASSERT_EQ(first->second.ants.size(), want);                                       // the level's number, in one command
            ASSERT_TRUE(std::find(first->second.ants.begin(), first->second.ants.end(), combat) != first->second.ants.end());      // the Combat Ant first
            for (const uint32_t id : first->second.ants) ASSERT_TRUE(id == combat || id == victim || id == near1 || id == near2);   // the nearest, never the far one
            ASSERT_TRUE(ever.count(carrier) == 0 && ever.count(weak) == 0);                   // a carrier and a hurt ant are never sent
            ASSERT_TRUE(!alive(sim, enemy) || sim.get_unit(enemy).hp < 10);                   // the attackers hit back
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_TRUE(bot.fight().fights_started() >= 1);
            if (!alive(sim, enemy)) {                                                         // the enemy is dead: the fight is over and every ant is back in the pool
                ASSERT_EQ(bot.fight().fights(), 0u);
                ASSERT_EQ(bot.ledger().count(StandardBot::kFight), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI7.7 Never An Ant That Stands On A Power-Up: An Enemy That Hit From Its Power-Up Tile Is Not Attacked At Any Level (No Order, No Fight), However Often Its Victim Is Hit; The Same Enemy Next To The Power-Up Is") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const bool on_powerup : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 5);
                sim.grid_mut().place_powerup(22, 21, 3);
                const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 18});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 18});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 18});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, on_powerup ? TileCoord{22, 21} : TileCoord{23, 21});
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
                rig.run(3);
                sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 22, 20));           // one blow from where it stands (a standing ant can hit what is next to it)
                rig.run(60);
                ASSERT_TRUE(sim.get_unit(victim).hp < 10);                                               // it did hit
                for (int blow = 0; blow < 4; ++blow) {                                                  // and again and again (the test makes the blows: the victim loses a hit point)
                    sim.get_unit(victim).hp = static_cast<uint16_t>(std::max<int>(3, sim.get_unit(victim).hp - 1));
                    rig.run(60);
                }
                const StandardBot& bot = rig.as<StandardBot>();
                ASSERT_TRUE(alive(sim, enemy));
                if (on_powerup) {
                    ASSERT_EQ(sim.get_unit(enemy).pos.x, 22);                                            // (it never left its tile)
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);                         // no order on an ant that stands on a power-up
                    ASSERT_EQ(bot.fight().fights_started(), 0u);
                } else {
                    ASSERT_TRUE(rig.proposed_count(CommandType::GroupAttack) > 0);
                    ASSERT_TRUE(bot.fight().fights_started() >= 1);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI7.8 An Attack That Shows 'Can't Go' Soon After It Left Means That No Order Reaches The Target: It Is Left Alone For 900 Ticks (An Enemy Shut In By Rocks Is Hit, Its Attackers Are Refused By The Path Finder, The Fight Ends And Is Not Started Again)") {
        sim::SimulationEngine sim;
        empty_field(sim, 6);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
            }
        }
        const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28, 30});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 28});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 32});
        const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 30});            // shut in: no path leads to its tile
        Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
        rig.run(10);
        sim.get_unit(victim).hp = 9;                                                                 // "hit" (the enemy cannot reach anybody: the test makes the blow)
        rig.run(300);
        const StandardBot& bot = rig.as<StandardBot>();
        ASSERT_TRUE(bot.fight().fights_started() >= 1);
        ASSERT_EQ(bot.fight().shunned_count(), 1u);
        ASSERT_TRUE(bot.fight().shunned(enemy, sim.current_tick()));
        ASSERT_EQ(bot.fight().fights(), 0u);
        const size_t attacks = rig.proposed_count(CommandType::GroupAttack);
        ASSERT_TRUE(attacks >= 1 && attacks <= 3);                                                   // one round of orders, not a stream
        ASSERT_EQ(bot.ledger().count(StandardBot::kFight), 0u);                                       // and the ants are free again
        sim.get_unit(victim).hp = 8;                                                                 // another blow while the enemy is left alone: no new fight
        rig.run(200);
        ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), attacks);
    } TEST_END();

    TEST_CASE("AI7.9 Interception Of A Thief (Off In Every Level's Plan: Measured, It Costs More Than It Takes; The Plan Flag Turns It On): A Thief That Stands Near The Hill Is Attacked And Hit, A Thief That WALKS To The Hill Is Never Chased (It Cannot Be Caught From Behind: The Walls And The Guard Are For It)") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            ASSERT_FALSE(plan_for(level).intercepts);                                             // the plans as shipped: no level intercepts
            for (const bool enabled : {false, true}) {
                LevelPlan plan = plan_for(level);
                plan.intercepts = enabled;
                // (a) a thief that stands 4 tiles from the raid tile
                {
                    sim::SimulationEngine sim;
                    empty_field(sim, 7);
                    sim.set_player_score(0, 120);
                    for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 9});
                    const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{12, 6});
                    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(300);
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack) > 0, enabled);
                    ASSERT_EQ(!alive(sim, thief) || sim.get_unit(thief).hp < 10, enabled);           // it was hit
                }
                // (b) a thief that walks to the hill
                {
                    sim::SimulationEngine sim;
                    empty_field(sim, 7);
                    sim.set_player_score(0, 120);
                    for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 9});
                    const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{32, 6});
                    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(3);
                    sim.apply_command(command_of(CommandType::GroupSpecial, 1, {thief}, 5, 5));      // the thief raids team 0
                    int raid_started = -1;
                    for (int t = 0; t < 300 && raid_started < 0; ++t) {
                        rig.tick();
                        if (sim.get_unit(thief).state == sim::UnitState::Infiltrating) raid_started = t;
                    }
                    ASSERT_TRUE(raid_started > 0);                                                   // it walked all the way and began the raid
                    ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);                     // nobody ran after it
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI7.10 A Hit Carrier Is Sent Home At Once: The Blow Clears Its Walk And It Stands With Its Food; The Standard Bot Orders It To The Hill Entrance (The Worker's Rescue Waits 900 Ticks Near The Gate), Once Per Try; Without The Aid (A Plan With It Off) It Stays") {
        for (const bool aid : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 8);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 9});          // three tiles from the mound: the rescue of the economy waits 900 ticks for it
            sim.get_unit(carrier).pick_up_food(1, 25);
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{9, 11});
            LevelPlan plan = plan_for(Level::Medium);
            plan.carrier_aid = aid;
            plan.defenders = 0;                                                                         // the carrier is the only thing the test looks at
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 9, 9));
            const int32_t score_before = sim.get_player_score(0);
            bool hit = false;
            for (int t = 0; t < 500; ++t) {
                rig.tick();
                hit = hit || sim.get_unit(carrier).hp < 10;                                              // (a carrier that gets home heals)
            }
            ASSERT_TRUE(hit);                                                                            // it was hit
            const StandardBot& bot = rig.as<StandardBot>();
            if (aid) {
                ASSERT_TRUE(bot.aid().sent_home() >= 1);
                ASSERT_TRUE(sim.get_player_score(0) > score_before);                                      // it banked its food
            } else {
                ASSERT_EQ(bot.aid().sent_home(), 0u);
                ASSERT_EQ(sim.get_player_score(0), score_before);                                         // nothing else sends it: it still stands with its food
                ASSERT_TRUE(sim.get_unit(carrier).holding != 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI7.11 The Fire Walls Of The Thief Hole By Level: Easy Builds Them When An Enemy Thief Ant Is Seen And Never Otherwise; Medium When One Is Seen Or An Enemy That Plays Can Reach A Thief Power-Up; Hard Pre-Emptively (A Thief Power-Up On The Map Is Enough) And Not When No Thief Can Be; A Fire Power-Up Is Taken For It (One Ant), And Given Back To The Economy When The Walls Stand; No Walls For A Hole With A Mud Tile, Nor In The Last 400 Ticks") {
        struct Case {
            Level level;
            bool thief_ant;
            int thief_powerup;      // 0 none, 1 an enemy reaches it, 2 shut in by rocks
            bool plays;
            bool builds;
        };
        const Case cases[] = {
            {Level::Easy, true, 0, true, true},      // a thief in sight: walls
            {Level::Easy, false, 1, true, false},    // a Thief power-up that the enemy can reach, but no thief seen: nothing
            {Level::Easy, false, 0, true, false},
            {Level::Medium, true, 0, true, true},
            {Level::Medium, false, 1, true, true},   // an enemy that plays can reach a Thief power-up
            {Level::Medium, false, 1, false, false}, // ... but not when it does not play (the idle bot's ants never move)
            {Level::Medium, false, 0, true, false},
            {Level::Medium, false, 2, true, false},  // a Thief power-up that nobody reaches is no threat
            {Level::Hard, true, 0, false, true},
            {Level::Hard, false, 1, false, true},    // pre-emptive: a Thief power-up that an enemy can reach, whether or not it has shown that it plays
            {Level::Hard, false, 0, true, false},    // nothing can make a thief on this map: no ant is given up for walls
            {Level::Hard, false, 2, true, false},
        };
        for (const Case& k : cases) {
            WallWorld w;
            w.build(k.thief_ant, k.thief_powerup);
            LevelPlan plan = plan_for(k.level);
            plan.secure_side = false;                                                                  // the walls only: the own-side Fire power-up is the next test's subject
            Rig rig(w.sim, 0, k.level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            if (k.plays) w.let_enemy_play();
            rig.run(900);
            const size_t walls = walls_east(w.sim, kFightHills[0]);
            ASSERT_EQ(walls, k.builds ? 3u : 0u);
            ASSERT_EQ(count_type(w.sim, 0, sim::AntType::Fire), k.builds ? 1u : 0u);                  // one Fire Ant for it, none otherwise
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.powerups().taken(), k.builds ? 1u : 0u);
            if (k.builds) {
                ASSERT_EQ(bot.walls().walls_ordered(), 3u);
                ASSERT_TRUE(east_state(w.sim.grid(), rig.map().hill(0)).shut());
                ASSERT_EQ(bot.ledger().count(StandardBot::kWalls), 0u);                                  // nothing left to do: the Fire Ant is back in the economy (it harvests like a worker)
            }
        }
        // a mud tile among the three: no wall can shut the hole, so none is built and no Fire Ant is fetched, whatever threatens
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            WallWorld w;
            w.build(true, 1);
            w.sim.grid_mut().set_terrain_class(8, 6, 3);
            LevelPlan plan = plan_for(level);
            plan.secure_side = false;
            Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            w.let_enemy_play();
            rig.run(700);
            ASSERT_EQ(walls_east(w.sim, kFightHills[0]), 0u);
            ASSERT_EQ(rig.as<StandardBot>().powerups().started(), 0u);
        }
        // the match ends in 350 ticks: a wall that would stand for the last few seconds is not worth an ant
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            WallWorld w;
            w.build(true, 1, 350);
            LevelPlan plan = plan_for(level);
            plan.secure_side = false;
            Rig rig(w.sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(300);
            ASSERT_EQ(walls_east(w.sim, kFightHills[0]), 0u);
            ASSERT_EQ(rig.as<StandardBot>().powerups().started(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI7.12 The Three Tiles In Front Of The Thief Hole Stop A Raid (The Engine's Own Order Of A Real Enemy Thief): With A Fire Wall On Each Of (bx + 4, by + 1 .. by + 3) The Order Ends Without A Raid And The Victim Keeps Its Points; With Two Of Them, Or With Three One Column Further East, The Raid Goes Through; East State Says Which; The Own Deposits Go On With The Walls Up") {
        const auto raid = [&](const std::vector<TileCoord>& walls, bool* started, const std::function<void(sim::SimulationEngine&)>& extra = {}) {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            sim.set_player_score(0, 100);
            for (const TileCoord& t : walls) sim.set_fire_at(t, 3500);
            if (extra) extra(sim);
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{25, 12});
            tick_all(sim, 2);
            sim.apply_command(command_of(CommandType::GroupSpecial, 1, {thief}, 5, 5));
            *started = false;
            for (int t = 0; t < 400; ++t) {
                tick_all(sim, 1);
                *started = *started || sim.get_unit(thief).state == sim::UnitState::Infiltrating;
            }
            return sim.get_player_score(0);
        };
        sim::SimulationEngine probe;
        empty_field(probe, 12);
        const MapInfo map(probe);
        const std::array<TileCoord, 3> east = east_tiles(map.hill(0));
        ASSERT_TRUE(east[0] == tc(8, 5) && east[1] == tc(8, 6) && east[2] == tc(8, 7));
        bool started = false;
        ASSERT_EQ(raid({}, &started), 50);                                                                 // no walls: the raid takes 50
        ASSERT_TRUE(started);
        ASSERT_EQ(raid({east[0], east[1], east[2]}, &started), 100);                                        // the three tiles: nothing is taken, and the thief never began
        ASSERT_FALSE(started);
        ASSERT_EQ(raid({east[0], east[1]}, &started), 50);                                                  // two are not enough
        ASSERT_TRUE(started);
        ASSERT_EQ(raid({east[1], east[2]}, &started), 50);
        ASSERT_EQ(raid({east[0], east[2]}, &started), 50);
        ASSERT_EQ(raid({tc(9, 5), tc(9, 6), tc(9, 7)}, &started), 50);                                      // three walls one column further east: the thief steps in between
        ASSERT_TRUE(started);
        ASSERT_EQ(raid({tc(8, 4), tc(8, 5), tc(8, 6)}, &started), 50);                                      // the wrong three (one row too high)
        // the other things that stand on a tile make it as shut as a wall does (checked against the engine too): a rock and a power-up are solid, a bomb throws the thief away
        ASSERT_EQ(raid({east[1], east[2]}, &started, [&](sim::SimulationEngine& e) { e.set_terrain(8, 5, sim::TERRAIN_OBSTACLE); }), 100);
        ASSERT_FALSE(started);
        ASSERT_EQ(raid({east[1], east[2]}, &started, [&](sim::SimulationEngine& e) { e.grid_mut().place_powerup(8, 5, 4); }), 100);
        ASSERT_FALSE(started);
        ASSERT_EQ(raid({east[1], east[2]}, &started, [&](sim::SimulationEngine& e) { e.grid_mut().place_bomb(8, 5, 0); }), 100);                // (the blast is that one raid's end)
        ASSERT_FALSE(started);
        // east_state agrees with the engine: shut exactly when all three tiles hold a wall
        for (int mask = 0; mask < 8; ++mask) {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            for (int i = 0; i < 3; ++i) {
                if ((mask >> i) & 1) sim.set_fire_at(east[static_cast<size_t>(i)], 3500);
            }
            const EastState st = east_state(sim.grid(), map.hill(0));
            ASSERT_EQ(st.shut(), mask == 7);
            ASSERT_EQ(st.walls(), static_cast<size_t>((mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1)));
        }
        // a bomb in front of the hole counts as shut for a raid (a thief that steps on it is thrown away), a rock too, mud (where nothing can stand) does not
        {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            sim.grid_mut().place_bomb(8, 5, 0);
            sim.set_terrain(8, 6, sim::TERRAIN_OBSTACLE);
            sim.set_fire_at(east[2], 3500);
            ASSERT_TRUE(east_state(sim.grid(), map.hill(0)).shut());
            sim.grid_mut().clear_bomb(8, 5);
            sim.grid_mut().set_terrain_class(8, 5, 3);                                                        // mud
            const EastState st = east_state(sim.grid(), map.hill(0));
            ASSERT_FALSE(st.shut());
            ASSERT_FALSE(st.shuttable());
        }
        // a power-up is solid for every walker: it shuts its tile like a rock (and an enemy's bomb next to it, a wall on the third tile: the hole is shut)
        {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            sim.grid_mut().place_powerup(8, 6, 4);
            sim.grid_mut().place_bomb(8, 5, 1);
            sim.set_fire_at(east[2], 3500);
            const EastState st = east_state(sim.grid(), map.hill(0));
            ASSERT_TRUE(st.tile[1] == EastTile::Blocked && st.tile[0] == EastTile::Bomb && st.tile[2] == EastTile::Wall);
            ASSERT_TRUE(st.shut());
            ASSERT_EQ(st.walls(), 1u);
        }
        // the own deposits go on with the walls up: four workers on a pile east of the walls bank as much as without them
        const auto bank = [&](bool walls) {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            place_pile(sim, 14, 6, 60, 25, {{60, 369}, {45, 370}, {30, 371}, {15, 372}, {0, kPileGone}});
            place_pile(sim, 14, 9, 60, 25, {{60, 369}, {45, 370}, {30, 371}, {15, 372}, {0, kPileGone}});
            if (walls) {
                for (const TileCoord& t : east) sim.set_fire_at(t, 14000);
            }
            std::vector<uint32_t> ants;
            for (int i = 0; i < 4; ++i) ants.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{16 + i, 8}));
            tick_all(sim, 2);
            sim.apply_command(command_of(CommandType::GroupMove, 0, ants, 14, 6));
            tick_all(sim, 2500);
            return sim.get_player_score(0);
        };
        const int32_t open_bank = bank(false);
        const int32_t walled_bank = bank(true);
        ASSERT_TRUE(open_bank >= 300);
        ASSERT_TRUE(walled_bank * 100 >= open_bank * 95);
    } TEST_END();

    TEST_CASE("AI7.13 The Walls Are Renewed Before They Burn Out While The Threat Lasts (Hard 130 Ticks Early, Medium 200, At A Calm Moment And One Wall At A Time) And The Hole Is Never Open Long; Easy Lights A Wall Again After It Burned Out; A Wall Is Never Lit On A Tile That Holds A Power-Up") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 13);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
            const uint32_t keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{12, 7});
            sim.spawn_unit(1, sim::AntType::Thief, TileCoord{55, 55});                                  // a thief in sight, far away: the threat lasts
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            size_t open_after_first = 0;
            size_t fewest = 3;
            bool built = false;
            for (int t = 0; t < 4300; ++t) {
                rig.tick();
                const size_t w = walls_east(sim, kFightHills[0]);
                if (w == 3) built = true;
                if (built && w < 3) ++open_after_first;
                if (built) fewest = std::min(fewest, w);
            }
            ASSERT_TRUE(built);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_TRUE(alive(sim, keeper));
            if (level == Level::Easy) {
                ASSERT_EQ(bot.walls().renewals(), 0u);                                                  // no lead: it lights the wall again when it has burned out
                ASSERT_TRUE(bot.walls().walls_ordered() >= 6);                                           // the first three and the three after the burn-out
                ASSERT_TRUE(open_after_first >= 40);
            } else {
                ASSERT_TRUE(bot.walls().renewals() >= 3);                                                // each wall was put out and lit again before its time
                ASSERT_TRUE(open_after_first <= 400);                                                    // the hole was open for a short while only (a wall at a time)
                ASSERT_TRUE(fewest >= 2);                                                                // and never with two walls down at once: the renewal is ahead of the burn-out
            }
        }
        // a power-up on one of the three tiles: no wall is ordered there (nothing can be lit on it) and nothing else is ordered onto it
        {
            sim::SimulationEngine sim;
            empty_field(sim, 13);
            sim.grid_mut().place_powerup(8, 6, 4);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{12, 7});
            sim.spawn_unit(1, sim::AntType::Thief, TileCoord{55, 55});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
            rig.run(600);
            for (const auto& e : rig.proposed) ASSERT_FALSE(e.second.tile_x == 8 && e.second.tile_y == 6);
            ASSERT_EQ(walls_east(sim, kFightHills[0]), 2u);                                              // the other two
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(8, 6)));
        }
    } TEST_END();

    TEST_CASE("AI7.14 Pick-Ups: The Ant That Goes Is The One With The Shortest WALK, Not The Nearest As The Crow Flies (A Rock Wall Between), Alone And By A Plain Click On The Tile; No Trip When Every Walk Is Too Long; A Power-Up That An Enemy Would Reach First Is Not Contested For; One That Somebody Stands On Is No Goal; A Trip That Fails Blacklists The Ant And The Tile And The Next Ant Is Sent") {
        LevelPlan plan = plan_for(Level::Medium);
        plan.secure_side = false;                                                                        // the Fire Ant is wanted for the walls (an enemy Thief is in sight), nothing else
        plan.steals = true;                                                                              // (the rock wall puts the power-up on another hill's side by the walking cost: this test is about the trips, AI7.24 about the sides)
        const auto build = [&](sim::SimulationEngine& sim, bool with_wall) {
            empty_field(sim, 21);
            sim.grid_mut().place_powerup(16, 12, 2);
            if (with_wall) {
                for (int y = 0; y <= 55; ++y) sim.set_terrain(14, y, sim::TERRAIN_OBSTACLE);               // a wall of rocks with a gap far south
            }
            sim.spawn_unit(1, sim::AntType::Thief, TileCoord{52, 30});                                  // in sight: the walls are wanted at every level
        };
        // (a) the walking cost decides: A is 4 tiles away as the crow flies but behind the wall (a walk of about 90 tiles), D is 6 tiles away on the open side
        {
            sim::SimulationEngine sim;
            build(sim, true);
            const uint32_t ant_a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
            const uint32_t ant_d = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 12});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(40);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.powerups().started(), 1u);
            size_t clicks = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupMove && e.second.tile_x == 16 && e.second.tile_y == 12) {
                    ++clicks;
                    ASSERT_EQ(e.second.ants.size(), 1u);                                                  // one ant, by a plain click
                    ASSERT_EQ(e.second.ants[0], ant_d);                                                   // the shorter walk, not the nearer ant
                }
            }
            ASSERT_EQ(clicks, 1u);
            rig.run(160);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Fire), 1u);                                       // it took the power-up
            ASSERT_TRUE(sim.get_unit(ant_d).type == sim::AntType::Fire);
            ASSERT_TRUE(sim.get_unit(ant_a).type == sim::AntType::Worker);
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(16, 12)));
            ASSERT_EQ(bot.powerups().taken(), 1u);
        }
        // (b) no trip when every walk is too long: the only Fire power-up of the map is at the far corner, a walk of 600 ticks for the one ant that there is (the limit is 420); with the
        //     limit out of the way the ant would go
        {
            sim::SimulationEngine sim;
            empty_field(sim, 21);
            sim.grid_mut().place_powerup(58, 22, 2);
            sim.spawn_unit(1, sim::AntType::Thief, TileCoord{3, 59});                                   // in sight, and further from the power-up than the ant (no contest)
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 10});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(200);
            ASSERT_EQ(rig.as<StandardBot>().powerups().started(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().powerups().active(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().powerups().contested(), 0u);
            for (const auto& e : rig.proposed) ASSERT_FALSE(e.second.tile_x == 58 && e.second.tile_y == 22);
        }
        // (c) a contest: an enemy worker 1 tile from the power-up gets there before any ant of ours (6 tiles), so the trip is not started and the tile is left alone for a while;
        //     the same worker 25 tiles away does not stop it
        for (const bool near : {true, false}) {
            sim::SimulationEngine sim;
            build(sim, false);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 12});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 13});
            sim.spawn_unit(1, sim::AntType::Worker, near ? TileCoord{17, 12} : TileCoord{40, 36});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(8);
            const StandardBot& bot = rig.as<StandardBot>();
            if (near) {
                ASSERT_EQ(bot.powerups().started(), 0u);
                ASSERT_TRUE(bot.powerups().contested() >= 1);
                ASSERT_TRUE(bot.powerups().tile_blacklisted(tc(16, 12), sim.current_tick()));
                ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);
            } else {
                ASSERT_EQ(bot.powerups().started(), 1u);
                ASSERT_EQ(bot.powerups().contested(), 0u);
            }
        }
        // (d) an ant that stands on the power-up: nobody else can take it, so it is no goal (and no contest is counted: the view says who stands there)
        {
            sim::SimulationEngine sim;
            build(sim, false);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 12});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{16, 12});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(40);
            ASSERT_EQ(rig.as<StandardBot>().powerups().started(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);
        }
        // (e) a trip that fails: rocks close in on the power-up after the order was given (the ant shows "can't go"): the (ant, tile) pair is left alone for 900 ticks and the tile for
        //     300; once the way is open again the NEXT ant is sent, not the one that failed
        {
            sim::SimulationEngine sim;
            build(sim, false);
            const uint32_t first = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 12});
            const uint32_t second = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 12});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            while (rig.proposed_count(CommandType::GroupMove) == 0 && sim.current_tick() < 60) rig.tick();
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
            ASSERT_EQ(rig.proposed[0].second.ants[0], first);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(16 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);   // the order leaves 4 ticks after the look: the engine finds the power-up shut in
                }
            }
            rig.run(120);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.powerups().failed(), 1u);
            ASSERT_TRUE(bot.powerups().blacklisted(first, tc(16, 12), sim.current_tick()));
            ASSERT_TRUE(bot.powerups().tile_blacklisted(tc(16, 12), sim.current_tick()));
            ASSERT_EQ(bot.powerups().active(), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);                                   // and nobody is sent while the tile is on the list
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(16 + dx, 12 + dy, sim::TERRAIN_WALKABLE);
                }
            }
            rig.run(400);                                                                                // the 300 ticks of the tile are over
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 2u);
            ASSERT_EQ(rig.proposed.back().second.ants[0], second);                                       // the failed ant is skipped for 900 ticks even though it is nearer
            ASSERT_TRUE(sim.get_unit(second).type == sim::AntType::Fire);
        }
        // (f) a flower dropper replaces what lies on the tile while the ant walks: far from it (3 tiles or more) the ant is called back by a stop order and stays a worker; close to it the ant
        //     goes on and takes what is there when it arrives
        for (const bool near : {false, true}) {
            sim::SimulationEngine sim;
            build(sim, false);
            const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 12});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            while (rig.proposed_count(CommandType::GroupMove) == 0 && sim.current_tick() < 60) rig.tick();
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
            while (TileCoord{sim.get_unit(ant).pos.x, sim.get_unit(ant).pos.y}.chebyshev_dist(tc(16, 12)) > (near ? 2 : 8)) rig.tick();
            sim.grid_mut().place_powerup(16, 12, 3);                                                     // the dropper puts a Thief where the Fire was
            rig.run(near ? 120 : 40);
            const StandardBot& bot = rig.as<StandardBot>();
            if (near) {
                ASSERT_EQ(bot.powerups().called_back(), 0u);
                ASSERT_TRUE(sim.get_unit(ant).type == sim::AntType::Thief);                              // it took what was there
            } else {
                ASSERT_EQ(bot.powerups().called_back(), 1u);
                ASSERT_EQ(rig.proposed_count(CommandType::Stop), 1u);
                ASSERT_TRUE(sim.get_unit(ant).type == sim::AntType::Worker);
                ASSERT_TRUE(sim.grid().has_powerup_at(tc(16, 12)));
            }
        }
    } TEST_END();

    TEST_CASE("AI7.15 Raids: The Thief Goes To The Hill Of The LEADING Team (By The Score Boxes) That Has Points To Take (At Least 30), Is Not Shut By Three Fire Walls, Is Not An Ally, Can Be Reached On Foot, Leaves Time For The Round Trip And, At Hard, Has No Enemy Combat Ant Near Its Raid Tile; Medium And Hard Raid, Easy Does Not; A Whole Raid In The Engine Banks The Loot") {
        const auto build = [&](sim::SimulationEngine& sim, uint32_t ticks, int32_t s1, int32_t s2, int32_t s3) {
            empty_field(sim, 31, ticks);
            sim.set_player_score(1, s1);
            sim.set_player_score(2, s2);
            sim.set_player_score(3, s3);
            return sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
        };
        const auto first_raid_target = [&](const Rig& rig) -> int {                                       // the team whose entrance the first special order of the bot names
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupSpecial) continue;
                for (int t = 1; t < 4; ++t) {
                    if (e.second.tile_x == rig.map().hill(static_cast<uint8_t>(t)).entrance.x && e.second.tile_y == rig.map().hill(static_cast<uint8_t>(t)).entrance.y) return t;
                }
            }
            return -1;
        };
        // (a) the leader (team 2, 500 points) at Medium and Hard; Easy has no raids at all
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            const uint32_t thief = build(sim, 14400, 300, 500, 100);
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            rig.run(30);
            if (level == Level::Easy) {
                ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);
                ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
                continue;
            }
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 1u);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 2);
            ASSERT_EQ(first_raid_target(rig), 2);
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupSpecial) ASSERT_TRUE(e.second.ants.size() == 1 && e.second.ants[0] == thief);
            }
        }
        // (b) the leader's hole is shut by three fire walls: the next team in the ranking (team 1), nobody when they all are shut
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 500, 100);
            sim::SimulationEngine probe;
            empty_field(probe, 31);
            const MapInfo map(probe);
            for (const TileCoord& t : east_tiles(map.hill(2))) sim.set_fire_at(t, 3500);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 1);
            ASSERT_EQ(first_raid_target(rig), 1);
        }
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 500, 100);
            sim::SimulationEngine probe;
            empty_field(probe, 31);
            const MapInfo map(probe);
            for (uint8_t t = 1; t < 4; ++t) {
                for (const TileCoord& e : east_tiles(map.hill(t))) sim.set_fire_at(e, 3500);
            }
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);                                 // nothing to take through three walls: the thief stays
            ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
        }
        // (b2) the leader's three tiles hold bombs (an enemy's bombs do not stop the walk of a thief: it would step on one and be thrown away): the hill counts as shut, the next team is raided
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 500, 100);
            sim::SimulationEngine probe;
            empty_field(probe, 31);
            const MapInfo map(probe);
            for (const TileCoord& t : east_tiles(map.hill(2))) sim.grid_mut().place_bomb(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), 2);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 1);
        }
        // (c) an ally's hill is no target even when its score leads
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 500, 100);
            sim.form_alliance(0, 2);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 1);
        }
        // (d) a box that shows less than 30 points is not worth the trip: the 20-point teams are left alone, the 40-point one is not
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 20, 20, 20);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);
            sim::SimulationEngine sim2;
            build(sim2, 14400, 20, 40, 20);
            Rig rig2(sim2, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig2.run(30);
            ASSERT_EQ(rig2.as<StandardBot>().raids().last_target(), 2);
        }
        // (e) no time for the round trip: the match ends in 700 ticks and the nearest hole is a walk of about 800 away and back
        {
            sim::SimulationEngine sim;
            build(sim, 700, 300, 500, 100);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(30);
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);
        }
        // (f) a thief that cannot walk anywhere (shut in by rocks) is not ordered: the raid would end in "can't go"
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 500, 100);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(20 + dx, 20 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);
        }
        // (g) a Combat Ant of the leader next to its raid tile: the plan that avoids guarded hills goes to the next hill (no shipped plan does: the Combat Ant of the enemy is a worker that fights, not
        //     a guard, and the tournaments measured 94.1 against 95.8 percent for avoiding it, docs/BOTS.md "Aggression")
        for (const Level level : {Level::Medium, Level::Hard}) {
            for (const bool avoids : {false, true}) {
                sim::SimulationEngine sim;
                build(sim, 14400, 300, 500, 100);
                sim::SimulationEngine probe;
                empty_field(probe, 31);
                const MapInfo map(probe);
                const TileCoord raid = map.hill(2).raid;
                sim.spawn_unit(2, sim::AntType::Combat, TileCoord{raid.x + 2, raid.y});
                LevelPlan plan = plan_for(level);
                plan.avoids_guarded_hills = avoids;
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(30);
                ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), avoids ? 1 : 2);
            }
            ASSERT_FALSE(plan_for(level).avoids_guarded_hills);
        }
        // (h) a whole raid in the engine: only team 1 has points; the thief walks there, raids, walks home and banks the 50 that it took
        {
            sim::SimulationEngine sim;
            build(sim, 14400, 300, 0, 0);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(1300);
            ASSERT_TRUE(sim.get_player_score(0) >= 50);
            ASSERT_TRUE(sim.get_player_score(1) <= 250);
            ASSERT_TRUE(rig.as<StandardBot>().raids().raids_ordered() >= 1);
            ASSERT_EQ(rig.as<StandardBot>().raids().failures(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI7.16 The Guard: A Combat Ant Is Parked Once On A Tile Whose 7 x 7 Square Holds The Three Tiles In Front Of The Thief Hole (East Of The Walls), The Next On The Queue Row, The Third South East; It Is Ordered ONCE (An Order Restarts The Two Seconds Of Its Reflex) And Left Alone On Its Post; A Power-Up Tile Is Never A Post; Easy Has No Guard") {
        sim::SimulationEngine probe;
        empty_field(probe, 41);
        const MapInfo pmap(probe);
        const HillInfo& hill = pmap.hill(0);
        // the geometry itself: the reflex rings reach three tiles, so the square of the first post holds the three tiles in front of the hole, the second post's the queue row
        const TileCoord post0 = GuardTask::post_of(hill, 0);
        const TileCoord post1 = GuardTask::post_of(hill, 1);
        const TileCoord post2 = GuardTask::post_of(hill, 2);
        for (const TileCoord& e : east_tiles(hill)) ASSERT_TRUE(post0.chebyshev_dist(e) <= 3);
        for (const TileCoord& q : hill.starts) ASSERT_TRUE(post1.chebyshev_dist(q) <= 3);
        ASSERT_TRUE(post0 != post1 && post1 != post2 && post0 != post2);
        // (the shipped plans do not park Combat Ants at home any more: a Combat Ant that harvests and fights what comes near is worth more, docs/BOTS.md "Aggression"; the guard is tested as the
        // feature that it is, with a plan that switches it on)
        const auto guard_plan = [](Level level) {
            LevelPlan p = plan_for(level);
            p.guards = level != Level::Easy;
            p.combat_harvests = false;
            return p;
        };
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 41);
            const uint32_t a = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            const uint32_t b = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{34, 30});
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(guard_plan(level)), 4, 4);
            rig.run(900);
            if (level == Level::Easy) {
                ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);                                 // no guard at Easy: the Combat Ants stand where they were
                ASSERT_EQ(sim.get_unit(a).pos.x, 30);
                continue;
            }
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 2u);                                     // one order each, never another
            const auto at = [&](uint32_t id) { return TileCoord{sim.get_unit(id).pos.x, sim.get_unit(id).pos.y}; };
            ASSERT_TRUE(at(a).chebyshev_dist(post0) <= 2);
            ASSERT_TRUE(at(b).chebyshev_dist(post1) <= 2);
            ASSERT_EQ(rig.as<StandardBot>().guard().guards(), 2u);
            ASSERT_EQ(rig.as<StandardBot>().guard().posted(), 2u);
            rig.run(900);                                                                                  // and they stay: no order restarts the reflex
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 2u);
            ASSERT_TRUE(at(a).chebyshev_dist(post0) <= 2);
        }
        // a power-up on the first post: the post moves to the next tile east, and nothing is ever ordered onto the power-up
        {
            sim::SimulationEngine sim;
            empty_field(sim, 41);
            sim.grid_mut().place_powerup(post0.x, post0.y, 4);
            const uint32_t a = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{30, 30});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(guard_plan(Level::Medium)), 4, 4);
            rig.run(900);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
            for (const auto& e : rig.proposed) ASSERT_FALSE(e.second.tile_x == post0.x && e.second.tile_y == post0.y);
            ASSERT_TRUE(sim.grid().has_powerup_at(post0));
            const TileCoord where{sim.get_unit(a).pos.x, sim.get_unit(a).pos.y};
            ASSERT_TRUE(where.chebyshev_dist(TileCoord{post0.x + 1, post0.y}) <= 2);
            ASSERT_TRUE(sim.get_unit(a).type == sim::AntType::Combat);                                      // it took nothing on the way (it is still a Combat Ant)
        }
    } TEST_END();

    TEST_CASE("AI7.17 The Sides Of The Map: On TREASURE Every Base Has One Power-Up Of Each Kind On Its Side (The Hill That Reaches It At The Least Walking Cost, Strictly Less Than Every Other); A Match Without Some Of The Teams Gives Their Power-Ups To The Teams That Play; A Tie, A Power-Up No Hill Reaches And A Tile Without A Power-Up Have No Side") {
        sim::SimulationEngine sim;
        start_match(sim, "TREASURE", 5, 0x0F);
        const MapInfo map(sim);
        // the pinned layout of the shipped map (tile -> side with four teams): the owner's words, "each base has one of each type of power-up on its side", checked against the analysis
        struct Pin {
            int32_t x;
            int32_t y;
            sim::AntType kind;
            int side;
        };
        const Pin pins[] = {
            {57, 24, sim::AntType::Fire, 0}, {2, 35, sim::AntType::Fire, 1}, {57, 34, sim::AntType::Fire, 2}, {2, 23, sim::AntType::Fire, 3},
            {48, 25, sim::AntType::Bomber, 0}, {9, 33, sim::AntType::Bomber, 1}, {48, 33, sim::AntType::Bomber, 2}, {9, 25, sim::AntType::Bomber, 3},
            {35, 14, sim::AntType::Thief, 0}, {26, 47, sim::AntType::Thief, 1}, {34, 47, sim::AntType::Thief, 2}, {25, 14, sim::AntType::Thief, 3},
            {37, 2, sim::AntType::Combat, 0}, {24, 55, sim::AntType::Combat, 1}, {36, 55, sim::AntType::Combat, 2}, {24, 2, sim::AntType::Combat, 3},
        };
        for (const Pin& p : pins) {
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(p.x, p.y)));
            ASSERT_EQ(static_cast<int>(sim.grid().get_powerup_type(tc(p.x, p.y))), static_cast<int>(p.kind));
            ASSERT_EQ(power_up_side(map, tc(p.x, p.y), 0x0F), p.side);
        }
        // by the table of the analysis: every kind (Fire, Bomber, Thief, Combat, Swimmer) has exactly one power-up on each of the four sides
        for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer}) {
            std::array<int, 4> per_side{};
            for (const PowerUpInfo& pu : map.powerups()) {
                if (pu.type != kind) continue;
                const int side = power_up_side(map, pu.tile, 0x0F);
                ASSERT_TRUE(side >= 0 && side < 4);
                ++per_side[static_cast<size_t>(side)];
            }
            for (const int n : per_side) ASSERT_EQ(n, 1);
        }
        // only teams 0 and 1 play: the power-ups of the absent hills go to the nearer of the two players, never to an absent team
        for (const PowerUpInfo& pu : map.powerups()) {
            const int side = power_up_side(map, pu.tile, 0x03);
            ASSERT_TRUE(side == 0 || side == 1);
            const int32_t c0 = pu.approach[0].cost;
            const int32_t c1 = pu.approach[1].cost;
            ASSERT_EQ(side, c0 < c1 ? 0 : 1);
        }
        ASSERT_EQ(power_up_side(map, tc(2, 23), 0x03), 1);                                              // (the Fire of team 3's side: team 1 is nearer than team 0)
        ASSERT_EQ(power_up_side(map, tc(57, 34), 0x05), 2);                                             // teams 0 and 2 only: team 2's own Fire stays its own
        ASSERT_EQ(power_up_side(map, tc(30, 30), 0x0F), -1);                                            // a tile without a power-up has no side
        // ISLANDS: only the four Bombers on the islands are reachable on foot (each by one team alone), the other 36 power-ups lie out of every hill's reach and have no side
        {
            sim::SimulationEngine islands;
            start_match(islands, "ISLANDS", 5, 0x0F);
            const MapInfo imap(islands);
            ASSERT_EQ(power_up_side(imap, tc(20, 21), 0x0F), 0);
            ASSERT_EQ(power_up_side(imap, tc(40, 39), 0x0F), 1);
            ASSERT_EQ(power_up_side(imap, tc(20, 39), 0x0F), 2);
            ASSERT_EQ(power_up_side(imap, tc(40, 21), 0x0F), 3);
            size_t with_side = 0;
            for (const PowerUpInfo& pu : imap.powerups()) with_side += power_up_side(imap, pu.tile, 0x0F) >= 0 ? 1u : 0u;
            ASSERT_EQ(with_side, 4u);
        }
        // a tie has no side, a power-up that no hill reaches has none either; just left of the middle, just right of it (the hills at (4, 4) and (50, 4) start at x = 5 and 51)
        {
            sim::SimulationEngine field;
            empty_field(field, 51);
            field.grid_mut().place_powerup(27, 6, 3);
            field.grid_mut().place_powerup(28, 6, 3);
            field.grid_mut().place_powerup(29, 6, 3);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) field.set_terrain(30 + dx, 40 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            field.grid_mut().place_powerup(30, 40, 2);                                                    // shut in by rocks: no hill reaches it
            const MapInfo fmap(field);
            ASSERT_EQ(power_up_side(fmap, tc(27, 6), 0x0F), 0);
            ASSERT_EQ(power_up_side(fmap, tc(28, 6), 0x0F), -1);
            ASSERT_EQ(power_up_side(fmap, tc(29, 6), 0x0F), 1);
            ASSERT_EQ(power_up_side(fmap, tc(30, 40), 0x0F), -1);
        }
    } TEST_END();

    TEST_CASE("AI7.18 The Opening, Securing The Own Side (The First Moves Go To Power-Ups, Not To Food: Medium And Hard Take The Fire, Bomber And Thief Power-Ups On Their Side Of TREASURE In That Order Of Value, Nobody Else's, Nothing That They Do Not Need: Combat Waits For An Enemy That Fights; Easy Takes The Fire Only When An Enemy Thief Is In Sight, Never A Bomber Or A Thief): Typed Ants Go On Harvesting") {
        struct Case {
            Level level;
            bool thief_in_sight;
            bool fire;
            bool bomber;
            bool thief;
        };
        const Case cases[] = {
            {Level::Easy, false, false, false, false},    // nothing is wanted: no walls, no denial
            {Level::Easy, true, true, false, false},      // an enemy Thief is in sight: the Fire for the walls, and no Bomber, no Thief
            {Level::Medium, false, true, true, true},     // denial: the Fire, the Bomber and the Thief of the own side
            {Level::Hard, false, true, true, true},
        };
        for (const Case& k : cases) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x0F);
            Rig rig(sim, 0, k.level, std::make_unique<StandardBot>(plan_for(k.level)), 4, 4);
            if (k.thief_in_sight) sim.spawn_unit(1, sim::AntType::Thief, rig.map().hill(1).queue);
            rig.run(1100);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Fire), k.fire ? 1u : 0u);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Bomber), k.bomber ? 1u : 0u);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Thief), k.thief ? 1u : 0u);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), 0u);                                       // Combat waits for an enemy that fights (the others stand idle here)
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Swimmer), 0u);
            // the power-ups of the own side are gone when taken; every other power-up of the map is where it was
            ASSERT_EQ(sim.grid().has_powerup_at(tc(57, 24)), !k.fire);
            ASSERT_EQ(sim.grid().has_powerup_at(tc(48, 25)), !k.bomber);
            ASSERT_EQ(sim.grid().has_powerup_at(tc(35, 14)), !k.thief);
            for (const TileCoord other : {tc(2, 23), tc(57, 34), tc(2, 35), tc(9, 25), tc(9, 33), tc(48, 33), tc(25, 14), tc(26, 47), tc(34, 47)}) ASSERT_TRUE(sim.grid().has_powerup_at(other));
            ASSERT_EQ(rig.as<StandardBot>().powerups().taken(), static_cast<uint32_t>((k.fire ? 1 : 0) + (k.bomber ? 1 : 0) + (k.thief ? 1 : 0)));
            ASSERT_EQ(rig.as<StandardBot>().powerups().failed(), 0u);
            if (k.level != Level::Easy) {                                                                  // the trips of the opening leave at the first looks, in the order of value
                std::vector<TileCoord> order;
                for (const auto& e : rig.proposed) {
                    if (e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && sim.grid().in_bounds(tc(e.second.tile_x, e.second.tile_y)) &&
                        (e.second.tile_x == 57 || e.second.tile_x == 48 || e.second.tile_x == 35) && (e.second.tile_y == 24 || e.second.tile_y == 25 || e.second.tile_y == 14)) {
                        order.push_back(tc(e.second.tile_x, e.second.tile_y));
                    }
                }
                ASSERT_TRUE(order.size() >= 3);
                ASSERT_TRUE(order[0] == tc(57, 24) && order[1] == tc(48, 25) && order[2] == tc(35, 14));    // Fire, Bomber, Thief
                ASSERT_TRUE(rig.proposed.front().first <= 10);                                              // and the first of them at the first look
            }
        }
        // the own side's Fire, Bomber and Thief are gone (an enemy took them): nobody goes for those of the other sides (the levels do not steal)
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x0F);
            sim.grid_mut().clear_powerup(57, 24);
            sim.grid_mut().clear_powerup(48, 25);
            sim.grid_mut().clear_powerup(35, 14);
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            if (level == Level::Easy) sim.spawn_unit(1, sim::AntType::Thief, rig.map().hill(1).queue);       // (Easy would take a Fire for the walls: still not a stolen one)
            rig.run(1100);
            ASSERT_EQ(rig.as<StandardBot>().powerups().started(), 0u);
            if (level == Level::Medium) {                                                                   // (nothing threatens: no wish for a Fire, a Bomber or a Thief at all, however many lie on other sides)
                ASSERT_EQ(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Fire)], 0);
                ASSERT_EQ(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Bomber)], 0);
                ASSERT_EQ(rig.as<StandardBot>().tactics().wants[static_cast<size_t>(sim::AntType::Thief)], 0);
            }
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Fire), 0u);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Bomber), 0u);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Thief), 0u);
            for (const TileCoord other : {tc(2, 23), tc(57, 34), tc(2, 35), tc(9, 25), tc(9, 33), tc(48, 33), tc(25, 14), tc(26, 47), tc(34, 47)}) ASSERT_TRUE(sim.grid().has_powerup_at(other));
        }
    } TEST_END();

    TEST_CASE("AI7.19 Enemy Bombs Near The Base (Counter): An Own Bomber Defuses The Nearest Harmful One (A Special Order Onto Its Tile; Nobody Is Hurt); Without A Bomber A Healthy Idle Worker Sets It Off On Purpose (A Plain Click: It Loses Hit Points, The Bomb Is Gone); Never With A Carrier, A Hurt Ant Or When The Plan Says No; Never For An Own Or An Allied Bomb, Or One Far From The Hill And The Piles; A Bomb Nothing Can Reach Is Left Alone For A While") {
        const TileCoord bomb_tile{9, 12};                                                                  // 8 tiles from the hill's origin (4, 4): within the radius of 10
        const auto build = [&](sim::SimulationEngine& sim, uint8_t owner, TileCoord at, bool with_bomber, int32_t worker_hp) {
            empty_field(sim, 61);
            sim.grid_mut().place_bomb(static_cast<uint32_t>(at.x), static_cast<uint32_t>(at.y), owner);
            struct Ids {
                uint32_t worker;
                uint32_t bomber;
            } ids{0, 0};
            ids.worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{15, 12});
            sim.get_unit(ids.worker).hp = static_cast<uint16_t>(worker_hp);
            if (with_bomber) ids.bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{16, 12});
            return ids;
        };
        // (a) a Bomber defuses it: the special order, the bomb is gone, nobody lost a hit point
        {
            sim::SimulationEngine sim;
            const auto ids = build(sim, 1, bomb_tile, true, 10);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(500);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_FALSE(sim.grid().has_bomb_at(bomb_tile));
            ASSERT_EQ(bot.bombs().defused(), 1u);
            ASSERT_EQ(bot.bombs().set_off(), 0u);
            size_t specials = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupSpecial && e.second.tile_x == bomb_tile.x && e.second.tile_y == bomb_tile.y) {
                    ++specials;
                    ASSERT_TRUE(e.second.ants.size() == 1 && e.second.ants[0] == ids.bomber);
                }
            }
            ASSERT_EQ(specials, 1u);
            ASSERT_EQ(sim.get_unit(ids.worker).hp, 10);                                                    // nothing was hurt
            ASSERT_EQ(sim.get_unit(ids.bomber).hp, 10);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);                                      // (and no worker was sent over it)
        }
        // (b) without a Bomber a healthy idle worker is clicked onto the bomb and sets it off: the bomb is gone and the worker is hurt
        {
            sim::SimulationEngine sim;
            const auto ids = build(sim, 1, bomb_tile, false, 10);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(500);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_FALSE(sim.grid().has_bomb_at(bomb_tile));
            ASSERT_EQ(bot.bombs().set_off(), 1u);
            ASSERT_EQ(bot.bombs().defused(), 0u);
            bool clicked = false;
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupMove && e.second.tile_x == bomb_tile.x && e.second.tile_y == bomb_tile.y) {
                    clicked = true;
                    ASSERT_TRUE(e.second.ants.size() == 1 && e.second.ants[0] == ids.worker);
                }
            }
            ASSERT_TRUE(clicked);
            ASSERT_TRUE(sim.get_unit(ids.worker).hp <= 10);
        }
        // (c) no ant that may be sent: a hurt worker (5 hit points) or a plan without the hit: the bomb stays and nothing is ordered onto it; a carrier is never sent while it holds
        //     its food (the economy takes it home first, and it is a free worker afterwards)
        for (const int variant : {0, 1, 2, 3}) {
            sim::SimulationEngine sim;
            const auto ids = build(sim, 1, bomb_tile, variant == 3, variant == 0 ? 5 : 10);
            if (variant == 1) sim.get_unit(ids.worker).pick_up_food(1, 25);
            LevelPlan plan = plan_for(Level::Medium);
            plan.bomb_hit = variant != 2;
            plan.counters = variant != 3;                                                                 // (variant 3: a Bomber stands by, but the plan has no counters)
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            uint64_t emptied = 0;
            for (int t = 0; t < 300; ++t) {
                rig.tick();
                if (emptied == 0 && variant == 1 && sim.get_unit(ids.worker).holding == 0) emptied = sim.current_tick();
            }
            for (const auto& e : rig.proposed) {
                if (e.second.tile_x != bomb_tile.x || e.second.tile_y != bomb_tile.y) continue;
                ASSERT_EQ(variant, 1);                                                                    // only the carrier that has delivered may be sent
                ASSERT_TRUE(emptied != 0 && e.first >= emptied);
            }
            if (variant != 1) ASSERT_TRUE(sim.grid().has_bomb_at(bomb_tile));
            if (variant == 3) ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
        }
        // (d) a bomb that is not an enemy's (own, or an ally's) or that is far from the hill and from every pile is no business of the counter
        {
            sim::SimulationEngine own;
            build(own, 0, bomb_tile, true, 10);
            Rig r1(own, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            r1.run(200);
            ASSERT_TRUE(own.grid().has_bomb_at(bomb_tile));
            ASSERT_EQ(r1.proposed_count(CommandType::GroupSpecial), 0u);
            sim::SimulationEngine allied;
            build(allied, 1, bomb_tile, true, 10);
            allied.form_alliance(0, 1);
            Rig r2(allied, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            r2.run(200);
            ASSERT_TRUE(allied.grid().has_bomb_at(bomb_tile));
            ASSERT_EQ(r2.proposed_count(CommandType::GroupSpecial), 0u);
            sim::SimulationEngine far;
            build(far, 1, TileCoord{40, 40}, true, 10);
            Rig r3(far, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            r3.run(200);
            ASSERT_TRUE(far.grid().has_bomb_at(TileCoord{40, 40}));
            ASSERT_EQ(r3.proposed_count(CommandType::GroupSpecial), 0u);
            ASSERT_EQ(r3.proposed_count(CommandType::GroupMove), 0u);
        }
        // (e) a bomb that nothing reaches (shut in by rocks): the order is given once, the ant stands idle again, the bomb is left alone for 300 ticks and tried again after
        {
            sim::SimulationEngine sim;
            build(sim, 1, bomb_tile, false, 10);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(bomb_tile.x + dx, bomb_tile.y + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(250);
            const StandardBot& bot = rig.as<StandardBot>();
            ASSERT_EQ(bot.bombs().failures(), 1u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 1u);
            rig.run(250);                                                                                  // 500 ticks in: the 300 of the blacklist are over (it started at about tick 170)
            ASSERT_TRUE(rig.proposed_count(CommandType::GroupMove) >= 2);
            ASSERT_TRUE(sim.grid().has_bomb_at(bomb_tile));
        }
    } TEST_END();

    TEST_CASE("AI7.20 Enemy Fire Walls Near The Base (Counter): An Own Fire Ant Puts Out Every Wall That Lies Near The Hill Or A Pile (One At A Time, The Nearest First); Without A Fire Ant Nothing Is Ordered Onto Fire, The Walls Burn Out By Themselves And The Economy Waits For The Pile That They Cut Off; The Own Three Walls In Front Of The Thief Hole Are Never Put Out; Walls Far From Everything Are Left Alone") {
        const std::array<TileCoord, 3> theirs = {tc(10, 6), tc(10, 7), tc(10, 8)};                      // 6 tiles from the origin of the hill at (4, 4)
        const auto build = [&](sim::SimulationEngine& sim, bool fire_ant) {
            empty_field(sim, 71);
            uint32_t keeper = 0;
            if (fire_ant) keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{15, 9});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{16, 20});
            return keeper;
        };
        // (a) the Fire Ant puts out all three, by special orders of the Fire Ant onto the wall tiles
        {
            sim::SimulationEngine sim;
            const uint32_t keeper = build(sim, true);
            for (const TileCoord& t : theirs) sim.set_fire_at(t, 3500);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(1500);
            const StandardBot& bot = rig.as<StandardBot>();
            for (const TileCoord& t : theirs) ASSERT_FALSE(sim.grid().has_fire_at(t));
            ASSERT_EQ(bot.walls().extinguished(), 3u);
            size_t orders = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupSpecial) continue;
                ++orders;
                ASSERT_TRUE(e.second.ants.size() == 1 && e.second.ants[0] == keeper);
                bool on_wall = false;
                for (const TileCoord& t : theirs) on_wall = on_wall || (e.second.tile_x == t.x && e.second.tile_y == t.y);
                ASSERT_TRUE(on_wall);
            }
            ASSERT_EQ(orders, 3u);
        }
        // (b) no Fire Ant: no order onto fire, the walls stand (their 3,500 ticks have not run out)
        {
            sim::SimulationEngine sim;
            build(sim, false);
            for (const TileCoord& t : theirs) sim.set_fire_at(t, 3500);
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);
            rig.run(800);
            for (const TileCoord& t : theirs) ASSERT_TRUE(sim.grid().has_fire_at(t));
            ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
            for (const auto& e : rig.proposed) {
                for (const TileCoord& t : theirs) ASSERT_FALSE(e.second.tile_x == t.x && e.second.tile_y == t.y);                // and nobody is clicked onto a wall either
            }
        }
        // (c) the own walls in front of the thief hole are never put out (no threat here: the walls are not wanted, the Fire Ant stands by); a wall far from everything is left alone
        {
            sim::SimulationEngine sim;
            build(sim, true);
            sim::SimulationEngine probe;
            empty_field(probe, 71);
            const MapInfo map(probe);
            const std::array<TileCoord, 3> own = east_tiles(map.hill(0));
            for (const TileCoord& t : own) sim.set_fire_at(t, 3500);
            sim.set_fire_at(tc(40, 40), 3500);
            Rig rig(sim, 0, Level::Easy, std::make_unique<StandardBot>(plan_for(Level::Easy)), 4, 4);
            rig.run(900);
            for (const TileCoord& t : own) ASSERT_TRUE(sim.grid().has_fire_at(t));
            ASSERT_TRUE(sim.grid().has_fire_at(tc(40, 40)));
            ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
            ASSERT_EQ(rig.as<StandardBot>().walls().extinguished(), 0u);
        }
        // (d) with the plan's counters off the enemy's walls stand even though a Fire Ant is there
        {
            sim::SimulationEngine sim;
            build(sim, true);
            for (const TileCoord& t : theirs) sim.set_fire_at(t, 3500);
            LevelPlan plan = plan_for(Level::Medium);
            plan.counters = false;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(600);
            for (const TileCoord& t : theirs) ASSERT_TRUE(sim.grid().has_fire_at(t));
            ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
        }
        // (e) a pile that a ring of walls has cut off: the economy waits (no order onto the pile) and works when the walls have burned out; with a Fire Ant the ring is opened early
        for (const bool fire_ant : {false, true}) {
            sim::SimulationEngine sim;
            empty_field(sim, 72);
            const int32_t pile = place_pile(sim, 20, 12, 40, 25, {{40, 369}, {30, 370}, {20, 371}, {10, 372}, {0, kPileGone}});
            const MapInfo map(sim);
            const PileInfo* info = map.pile(static_cast<uint32_t>(pile));
            ASSERT_TRUE(info != nullptr && !info->cells.empty());
            std::set<std::pair<int32_t, int32_t>> cells;
            std::set<std::pair<int32_t, int32_t>> ring;
            for (const TileCoord& c : info->cells) cells.insert({c.x, c.y});
            for (const TileCoord& c : info->cells) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (cells.count({c.x + dx, c.y + dy}) == 0) ring.insert({c.x + dx, c.y + dy});
                    }
                }
            }
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12 + i, 10});
            if (fire_ant) sim.spawn_unit(0, sim::AntType::Fire, TileCoord{14, 14});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_for(Level::Medium)), 4, 4);                // (the analysis of the start sees the pile open, as in a match)
            for (const auto& r : ring) sim.set_fire_at(tc(r.first, r.second), fire_ant ? 3500u : 500u);
            rig.run(450);
            uint64_t first_special = ~uint64_t{0};
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupSpecial) first_special = std::min(first_special, e.first);
            }
            for (const auto& e : rig.proposed) {                                                           // nobody is sent into the ring (the pile's cells are inside) before a wall of it is out
                if (e.second.type == CommandType::GroupMove && cells.count({e.second.tile_x, e.second.tile_y}) != 0) ASSERT_TRUE(fire_ant && e.first > first_special);
            }
            if (!fire_ant) {
                ASSERT_EQ(sim.get_player_score(0), 0);                                                      // nothing was banked: the economy waited for the walls to burn out
                ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);
            }
            rig.run(2400);
            ASSERT_TRUE(sim.get_player_score(0) > 100);                                                     // and the pile is harvested afterwards, with or without the Fire Ant
            if (fire_ant) ASSERT_TRUE(rig.as<StandardBot>().walls().extinguished() >= 1);
            else ASSERT_EQ(rig.as<StandardBot>().walls().extinguished(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI7.21 The View's Bombs And Fire Walls Are The Engine's Grid At Every Look: Every Bomb With The Team Whose Colour It Is Drawn In (An Enemy's Too: A Person Sees It), Every Fire Wall Without Owner Or Timer, In Reading Order; A Bomb That Went Off Or Was Defused And A Wall That Burned Out Are Gone At The Next Look; A Copy Of The View Keeps Them; Power-Ups, Bombs And Walls Do Not Mix") {
        sim::SimulationEngine sim;
        empty_field(sim, 81);
        struct B {
            int32_t x;
            int32_t y;
            uint8_t owner;
        };
        const B bombs[] = {{30, 10, 1}, {12, 11, 0}, {13, 11, 2}, {44, 30, 3}, {20, 52, 1}};
        const TileCoord walls[] = {{31, 10}, {30, 11}, {8, 6}, {8, 7}, {50, 50}};
        for (const B& b : bombs) sim.grid_mut().place_bomb(static_cast<uint32_t>(b.x), static_cast<uint32_t>(b.y), b.owner);
        for (const TileCoord& w : walls) sim.set_fire_at(w, 400);
        sim.grid_mut().place_powerup(33, 33, 2);
        const auto expected_bombs = [&](const sim::SimulationEngine& e) {                                // the oracle: a scan of the engine's own grid, tile by tile
            std::vector<BombView> out;
            for (int32_t y = 0; y < static_cast<int32_t>(e.grid().height()); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(e.grid().width()); ++x) {
                    if (e.grid().has_bomb_at(tc(x, y))) out.push_back(BombView{tc(x, y), e.grid().get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).interactive_owner});
                }
            }
            return out;
        };
        const auto expected_walls = [&](const sim::SimulationEngine& e) {
            std::vector<TileCoord> out;
            for (int32_t y = 0; y < static_cast<int32_t>(e.grid().height()); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(e.grid().width()); ++x) {
                    if (e.grid().has_fire_at(tc(x, y))) out.push_back(tc(x, y));
                }
            }
            return out;
        };
        const auto same = [&](const BotView& v, const sim::SimulationEngine& e) {
            const std::vector<BombView> b = expected_bombs(e);
            const std::vector<TileCoord> w = expected_walls(e);
            if (v.bombs().size() != b.size() || v.fire_walls().size() != w.size()) return false;
            for (size_t i = 0; i < b.size(); ++i) {
                if (!(v.bombs()[i].tile == b[i].tile) || v.bombs()[i].owner != b[i].owner) return false;
            }
            for (size_t i = 0; i < w.size(); ++i) {
                if (!(v.fire_walls()[i].tile == w[i])) return false;
            }
            return true;
        };
        for (uint8_t seat = 0; seat < 4; ++seat) {                                                         // the same list for every seat: nothing of it is a team's secret
            const BotView v = BotView::build(sim, seat);
            ASSERT_EQ(v.bombs().size(), 5u);
            ASSERT_EQ(v.fire_walls().size(), 5u);
            ASSERT_EQ(v.powerups().size(), 1u);
            ASSERT_TRUE(same(v, sim));
            const BotView copy(v);
            ASSERT_TRUE(same(copy, sim));
            ASSERT_EQ(v.bombs()[0].owner, 1);                                                              // reading order: the bomb at (30, 10) first
            ASSERT_EQ(v.bombs()[1].owner, 0);
            ASSERT_EQ(v.bombs()[2].owner, 2);
        }
        // the walls burn out (400 ticks), a bomb is defused, another goes off under an ant: the next look knows
        for (int t = 0; t < 12; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
        }
        ASSERT_TRUE(same(BotView::build(sim, 0), sim));
        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
        sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, 12, 11));                             // onto the team's own bomb: a deliberate detonation
        for (int t = 0; t < 200; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
            ASSERT_TRUE(same(BotView::build(sim, 0), sim));
        }
        ASSERT_FALSE(sim.grid().has_bomb_at(tc(12, 11)));
        ASSERT_EQ(BotView::build(sim, 0).bombs().size(), 4u);
        for (int t = 0; t < 400; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
        }
        const BotView late = BotView::build(sim, 0);
        ASSERT_TRUE(same(late, sim));
        ASSERT_EQ(late.fire_walls().size(), 0u);                                                           // all five burned out
        ASSERT_EQ(late.bombs().size(), 4u);
        ASSERT_EQ(late.powerups().size(), 1u);
    } TEST_END();

    TEST_CASE("AI7.22 Thief And Combat: Medium And Hard Take The Thief Power-Up Of Their Side In The Opening (Easy Never) And Their Thief Goes Raiding The Team That Has Points; They Take The Combat Power-Up Of Their Side In The Opening As Soon As An Enemy Plays (One Combat Ant): It Is A Worker That Fights, It Harvests Like One And Is Not Parked At The Hill")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x0F);
            sim.set_player_score(1, 100);                                                               // something to raid (30 at least)
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            const std::vector<uint32_t> theirs = ants_of(sim, 1);
            sim.apply_command(command_of(CommandType::GroupMove, 1, {theirs[0]}, 30, 27));                 // an enemy that moves: an enemy plays
            rig.run(2200);
            const StandardBot& bot = rig.as<StandardBot>();
            const size_t thieves = count_type(sim, 0, sim::AntType::Thief);
            const size_t fighters = count_type(sim, 0, sim::AntType::Combat);
            ASSERT_EQ(bot.guard().guards(), 0u);                                                         // no level parks a Combat Ant at home
            if (level == Level::Easy) {
                ASSERT_EQ(thieves, 0u);
                ASSERT_EQ(fighters, 0u);
                ASSERT_EQ(bot.raids().raids_ordered(), 0u);
                ASSERT_TRUE(sim.grid().has_powerup_at(tc(37, 2)));
                continue;
            }
            ASSERT_EQ(thieves, 1u);                                                                      // the Thief of the own side
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(35, 14)));
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(25, 14)));                                          // (another side's is left)
            ASSERT_TRUE(bot.raids().raids_ordered() >= 1);                                               // it raids the team that has points
            ASSERT_EQ(bot.raids().last_target(), 1);
            ASSERT_EQ(fighters, 1u);                                                                     // the Combat power-up of the own side, in the opening, and no other
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(37, 2)));
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(24, 2)));
            const uint32_t fighter = first_of_type(sim, 0, sim::AntType::Combat);
            size_t harvest_orders = 0;                                                                   // it was sent to piles after it became a Combat Ant
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupMove || (e.second.tile_x == 37 && e.second.tile_y == 2)) continue;
                for (const uint32_t id : e.second.ants) harvest_orders += id == fighter ? 1u : 0u;
            }
            ASSERT_TRUE(harvest_orders >= 1);
        }
    } TEST_END();

    TEST_CASE("AI7.23 The Defence Against The Double-Thief Opening (The Owner's Playbook: A Player Steals Your Thief Power-Up With His Second Ant And Raids Twice From The Start): The Bench Aggressor That Takes TWO Thief Power-Ups (Its Own Side's And The Nearest Other) Finds The Thief Of A Neighbour's Side Gone When The Neighbour Is A Medium Or Hard Bot (Taken In The Opening) And Takes It At Easy")
    {
        assets::LevelData treasure;
        ASSERT_TRUE(treasure.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL"));
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            ArenaSpec spec;
            spec.level = &treasure;
            spec.seed = 7;
            spec.max_ticks = 300;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = seat == 0 ? "standard" : seat == 3 ? "aggressor2" : "idle";
                b.level = seat == 3 ? Level::Hard : seat == 0 ? level : Level::Medium;
                spec.bots.push_back(b);
            }
            spec.extra_kinds = {"aggressor2"};
            spec.factory = [](const BotSpec& b) -> std::unique_ptr<Bot> {
                if (b.kind == "aggressor2") return std::make_unique<bench::AggressorBot>(2);
                return make_bot(b);
            };
            size_t thieves_of_ours = 0;
            size_t thieves_of_theirs = 0;
            bool power_up_there = true;
            spec.inspect = [&](const sim::SimulationEngine& e) {
                for (const sim::AntSnapshot& a : e.get_world_state().ants) {
                    if (a.raw_type != sim::AntType::Thief) continue;
                    if (a.player_id == 0) ++thieves_of_ours;
                    if (a.player_id == 3) ++thieves_of_theirs;
                }
                power_up_there = e.grid().has_powerup_at(tc(35, 14));
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_FALSE(power_up_there);                                                                     // somebody took it
            if (level == Level::Easy) {
                ASSERT_EQ(thieves_of_ours, 0u);                                                              // the aggressor has it: two thieves from the start
                ASSERT_EQ(thieves_of_theirs, 2u);
            } else {
                ASSERT_EQ(thieves_of_ours, 1u);                                                              // the bot took its own side's Thief in the opening
                ASSERT_EQ(thieves_of_theirs, 1u);                                                            // and the aggressor's second trip found it gone
            }
        }
    } TEST_END();

    TEST_CASE("AI7.24 The Double-Thief Opening At Hard (The Owner's Playbook; Stealing Is Hard's Only): With Three Idle Neighbours The Hard Bot Has Two Thief Ants After 900 Ticks (The Thief Of Its Own Side And The Unguarded Thief Power-Up Of Another), Medium One, Easy None; A Neighbour That Takes Its Own Thief Power-Up At The Start (Medium Or Hard) Is Not Robbed")
    {
        for (const Level level : {Level::Hard, Level::Medium, Level::Easy}) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", 5, 0x0F);
            for (uint8_t t = 1; t < 4; ++t) {
                for (const uint32_t id : ants_of(sim, t)) sim.get_unit(id).hp = 0;                           // the neighbours' ants are away from home (out of the view): their power-ups are unguarded
            }
            const uint32_t walker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{58, 58});
            sim.apply_command(command_of(CommandType::GroupMove, 1, {walker}, 50, 58));                     // and an enemy plays (the idle bot's ants never move), far from every power-up
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            rig.run(900);
            const size_t thieves = count_type(sim, 0, sim::AntType::Thief);
            const size_t others_gone = (sim.grid().has_powerup_at(tc(25, 14)) ? 0u : 1u) + (sim.grid().has_powerup_at(tc(26, 47)) ? 0u : 1u) + (sim.grid().has_powerup_at(tc(34, 47)) ? 0u : 1u);
            ASSERT_EQ(thieves, level == Level::Hard ? 2u : level == Level::Medium ? 1u : 0u);
            ASSERT_EQ(others_gone, level == Level::Hard ? 1u : 0u);
        }
        // one Thief wanted at Hard (a plan that does not want two): the own side's, never the nearer one of a neighbour (the own side comes first, theft is for the second one only)
        for (const uint32_t seed : {5u, 6u, 9u}) {
            sim::SimulationEngine sim;
            start_match(sim, "TREASURE", seed, 0x0F);
            for (uint8_t t = 1; t < 4; ++t) {
                for (const uint32_t id : ants_of(sim, t)) sim.get_unit(id).hp = 0;
            }
            const uint32_t walker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{58, 58});
            sim.apply_command(command_of(CommandType::GroupMove, 1, {walker}, 50, 58));
            LevelPlan plan = plan_for(Level::Hard);
            plan.max_thief = 1;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(900);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Thief), 1u);
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(35, 14)));                                           // the Thief of the own side is gone ...
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(25, 14)));                                            // ... and the neighbour's, which lies nearer to some of the ants, is not
        }
        {   // a hand-made world where the neighbour's Thief power-up lies NEARER to the ants than the own side's: Hard (one Thief wanted) still walks to the own side's
            sim::SimulationEngine sim;
            empty_field(sim, 83);
            sim.grid_mut().place_powerup(33, 10, 3);                                                       // between the hills of teams 0 and 1, nearer to team 1 by the walk
            sim.grid_mut().place_powerup(14, 12, 3);                                                       // on the own side
            for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28 + i % 2, 8 + i / 2});
            const uint32_t walker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{58, 58});
            sim.apply_command(command_of(CommandType::GroupMove, 1, {walker}, 50, 58));
            LevelPlan plan = plan_for(Level::Hard);
            plan.max_thief = 1;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            ASSERT_EQ(power_up_side(rig.map(), tc(33, 10), 0x0F), 1);
            ASSERT_EQ(power_up_side(rig.map(), tc(14, 12), 0x0F), 0);
            rig.run(600);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Thief), 1u);
            ASSERT_FALSE(sim.grid().has_powerup_at(tc(14, 12)));                                           // the own side's, though it is the farther
            ASSERT_TRUE(sim.grid().has_powerup_at(tc(33, 10)));
        }
        {   // the Thief power-up that lies on the ally's side is never stolen (an enemy's is): Hard wants two Thieves, the own side's is taken in both worlds
            for (const bool allied : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 83);
                sim.grid_mut().place_powerup(33, 10, 3);                                                   // on the side of team 1, away from its ants
                sim.grid_mut().place_powerup(14, 12, 3);                                                   // on the own side
                if (allied) sim.form_alliance(0, 1);
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{28 + i % 2, 8 + i / 2});
                const uint32_t walker = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{4, 58});         // an enemy (not the ally) plays, far from every power-up
                sim.apply_command(command_of(CommandType::GroupMove, 2, {walker}, 10, 58));
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_for(Level::Hard)), 4, 4);
                rig.run(800);
                ASSERT_FALSE(sim.grid().has_powerup_at(tc(14, 12)));
                ASSERT_EQ(sim.grid().has_powerup_at(tc(33, 10)), allied);                                  // the ally's stays, the enemy's goes
                ASSERT_EQ(count_type(sim, 0, sim::AntType::Thief), allied ? 1u : 2u);
            }
        }
        // two Medium neighbours: each takes its own side's Thief power-up at the start, so there is nothing left to steal (the Hard bot keeps its one)
        {
            ArenaSpec spec;
            assets::LevelData treasure;
            ASSERT_TRUE(treasure.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL"));
            spec.level = &treasure;
            spec.seed = 5;
            spec.max_ticks = 900;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = seat == 0 ? Level::Hard : Level::Medium;
                spec.bots.push_back(b);
            }
            size_t ours = 0;
            spec.inspect = [&](const sim::SimulationEngine& e) {
                for (const sim::AntSnapshot& a : e.get_world_state().ants) ours += a.player_id == 0 && a.raw_type == sim::AntType::Thief ? 1u : 0u;
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_EQ(ours, 1u);
        }
    } TEST_END();
    TEST_CASE("AI7.25 A Thief That Harvests Is Taken For A Raid: Its Loop (Walk To The Pile, Bite, Walk Home, Deliver, Walk Back) Is The Engine's, So It Is Never Idle, And A Raid That Waited For An Idle Thief Would Hardly Ever Start; As Soon As A Hill Has Loot And An Open Hole The Thief That Walks Empty-Handed Is Sent (One That Carries Food Delivers It First)")
    {
        for (const Level level : {Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 51);
            place_pile(sim, 12, 10, 600, 25, {{600, 369}, {450, 370}, {300, 371}, {150, 372}, {0, kPileGone}});         // a pile near the hill: the thief's loop is a few hundred ticks
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{8, 8});
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
            rig.run(260);                                                                              // nothing to raid yet (every box shows 0): the thief harvests
            ASSERT_EQ(rig.as<StandardBot>().raids().raids_ordered(), 0u);
            bool walked = false;
            bool carried = false;
            for (int t = 0; t < 120; ++t) {                                                            // its loop: it walks and it carries, and it is never idle
                rig.tick();
                const sim::AntSnapshot* a = snapshot_of(sim, thief);
                ASSERT_TRUE(a != nullptr);
                walked = walked || a->state == sim::UnitState::Walking;
                carried = carried || a->is_holding;
            }
            ASSERT_TRUE(walked && carried);
            sim.set_player_score(1, 300);                                                              // a hill with loot and an open hole
            const size_t before = rig.proposed.size();
            rig.run(260);
            ASSERT_TRUE(rig.as<StandardBot>().raids().raids_ordered() >= 1);
            ASSERT_EQ(rig.as<StandardBot>().raids().last_target(), 1);
            bool ordered = false;
            for (size_t i = before; i < rig.proposed.size(); ++i) {
                const Command& c = rig.proposed[i].second;
                if (c.type == CommandType::GroupSpecial && c.ants.size() == 1 && c.ants[0] == thief && c.tile_x == rig.map().hill(1).entrance.x && c.tile_y == rig.map().hill(1).entrance.y) ordered = true;
            }
            ASSERT_TRUE(ordered);
        }
    } TEST_END();
}
