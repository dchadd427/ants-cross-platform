// Tests of the standard bot's foundations (B4-1, AI5.x), quick enough for suite 2.20: the new members of the view (the power-ups and who stands on them), the controller's new
// filters (an attack needs an enemy ant on its tile, a move onto a power-up tile is a planned pick-up or nothing), the ledger's ranks, and the first tasks of the standard bot in
// hand-made worlds (strike back, never an ant that stands on a power-up, the thief hole's fire walls, pick-ups, raids). The whole matches of the acceptance (A1 .. A5) are suite 2.25.
//
//   AI7.1   BotView::powerups() is the engine's grid at every look (also while a dropper drops and typed ants are made), in reading order, and a copy of a view keeps it
//   AI7.2   standing(): an ant on a power-up that is not walking (own and other teams, idle and on guard), not one that is only crossing the tile on its way to take it
//   AI7.3   the controller refuses an attack whose tile holds no ant of another team (nobody, an own ant, an ally, an ant on a hill tile, an ant that stands on a power-up)
//   AI7.4   the controller refuses a move onto a power-up tile unless it is a planned pick-up (one ant, a power-up there), and a special order or attack onto one
//   AI7.5   the ledger's ranks: a task takes an ant from a task of a lower rank and from no other; the economy forgets the ant that was taken
#include "ai_test.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

#include "ants_ai/arena.hpp"
#include "ants_ai/idle_bot.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tasks.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

Command command_of(CommandType type, uint8_t issuer, const std::vector<uint32_t>& ants, int32_t x, int32_t y) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = static_cast<int16_t>(x);
    c.tile_y = static_cast<int16_t>(y);
    c.ants = ants;
    return c;
}

void tick_all(sim::SimulationEngine& sim, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
    }
}

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
}
