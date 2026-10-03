// Tests of what a bot sees (AI1.1 .. AI1.7): the ants of the seat and of the others, what is hidden (hit points, carried points, eggs, orders), the food piles, copies against
// borrows, the engine's own prediction of an order's acknowledgement, and what a look costs. The analysis of the map has its own file (test_ai_map.cpp).
#include "ai_test.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <type_traits>

#include "ants_ai/arena.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/rng.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

// ---- API-level negative checks: a view has no way to ask for what a player cannot know (these are detection idioms: they compile to "false" because the call does not exist)

template <class V, class = void>
struct CanAskEggsOf : std::false_type {};
template <class V>
struct CanAskEggsOf<V, std::void_t<decltype(std::declval<const V&>().eggs(uint8_t{}))>> : std::true_type {};

template <class V, class = void>
struct CanAskHatchingOf : std::false_type {};
template <class V>
struct CanAskHatchingOf<V, std::void_t<decltype(std::declval<const V&>().hatching(uint8_t{}))>> : std::true_type {};

template <class A, class = void>
struct AntHasOrder : std::false_type {};
template <class A>
struct AntHasOrder<A, std::void_t<decltype(std::declval<const A&>().order)>> : std::true_type {};

template <class A, class = void>
struct AntHasTarget : std::false_type {};
template <class A>
struct AntHasTarget<A, std::void_t<decltype(std::declval<const A&>().target)>> : std::true_type {};

template <class A, class = void>
struct AntHasHarvestOrigin : std::false_type {};
template <class A>
struct AntHasHarvestOrigin<A, std::void_t<decltype(std::declval<const A&>().harvest_origin)>> : std::true_type {};

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) {
    for (const AntView& a : ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

Command move_of(uint8_t issuer, std::vector<uint32_t> ants, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = issuer;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

}  // namespace

void run_view_tests() {
    TEST_CASE("AI1.1 Ants In The View: Own And Others Apart And By Id, Gone Ants Not Listed, Hit Points And Carried Points Only Of Own Ants, The Crumb Is Visible, No Order Or Target Anywhere") {
        sim::SimulationEngine sim;
        build_world(sim, 21);
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        const std::vector<uint32_t> foes = ants_of(sim, 1);
        ASSERT_EQ(mine.size(), 12u);
        // the own ant that has taken damage and carries food, an enemy ant that does too, an enemy with an order, and three ants that are gone for a player's eyes
        sim::AntUnit& own = sim.get_unit(mine[0]);
        own.hp = 6;
        own.holding = 1;
        own.carried_points = 30;
        sim::AntUnit& foe = sim.get_unit(foes[0]);
        foe.hp = 3;
        foe.holding = 1;
        foe.carried_points = 50;
        sim.get_unit(mine[5]).state = sim::UnitState::Drowning;
        sim.get_unit(mine[6]).state = sim::UnitState::Dead;
        sim.get_unit(foes[7]).state = sim::UnitState::Dead;
        ASSERT_TRUE(sim.apply_command(move_of(1, {foes[1]}, 30, 30)).accepted());
        sim.tick();
        const sim::WorldState& ws = sim.get_world_state();
        size_t listed_by_engine = 0;
        for (const sim::AntSnapshot& a : ws.ants) listed_by_engine += (a.id == mine[5] || a.id == mine[6] || a.id == foes[7]) ? 1u : 0u;
        ASSERT_EQ(listed_by_engine, 3u);                                               // (so their absence below is the view's doing: the engine still lists them)
        const BotView v = BotView::build(sim, 0);
        ASSERT_EQ(v.mine().size(), 10u);
        ASSERT_EQ(v.others().size(), 35u);
        ASSERT_TRUE(std::is_sorted(v.mine().begin(), v.mine().end(), [](const AntView& a, const AntView& b) { return a.id < b.id; }));
        ASSERT_TRUE(std::is_sorted(v.others().begin(), v.others().end(), [](const AntView& a, const AntView& b) { return a.id < b.id; }));
        ASSERT_TRUE(find_ant(v.mine(), mine[5]) == nullptr && find_ant(v.mine(), mine[6]) == nullptr && find_ant(v.others(), foes[7]) == nullptr);
        const AntView* me = find_ant(v.mine(), mine[0]);
        ASSERT_TRUE(me != nullptr);
        ASSERT_EQ(me->hp, 6);                                                          // the own ant is exact
        ASSERT_EQ(me->carried_points, 30);
        ASSERT_TRUE(me->holding);
        const AntView* them = find_ant(v.others(), foes[0]);
        ASSERT_TRUE(them != nullptr);
        ASSERT_TRUE(them->holding);                                                    // the sprite shows the crumb
        ASSERT_EQ(them->hp, 0);                                                        // but not the health bar
        ASSERT_EQ(them->carried_points, 0);                                            // nor what the crumb is worth
        for (const AntView& a : v.mine()) ASSERT_TRUE(a.team == 0 && a.hp > 0);
        for (const AntView& a : v.others()) ASSERT_TRUE(a.team != 0 && a.hp == 0 && a.carried_points == 0);
        // what the view says of an ant that another team has ordered is only what is on the screen: it walks
        const AntView* walker = find_ant(v.others(), foes[1]);
        ASSERT_TRUE(walker != nullptr && (walker->state == sim::UnitState::Walking || walker->state == sim::UnitState::Idle));
        // an ant of the other team seen from its own side is exact
        const BotView w = BotView::build(sim, 1);
        const AntView* foe_self = find_ant(w.mine(), foes[0]);
        ASSERT_TRUE(foe_self != nullptr && foe_self->hp == 3 && foe_self->carried_points == 50);
        ASSERT_TRUE(find_ant(w.others(), mine[0]) != nullptr && find_ant(w.others(), mine[0])->hp == 0);
        // the type is visible, and so are positions
        sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{20, 20});
        sim.tick();
        const BotView typed = BotView::build(sim, 1);
        bool saw_bomber = false;
        for (const AntView& a : typed.others()) saw_bomber = saw_bomber || (a.type == sim::AntType::Bomber && a.tile == TileCoord{20, 20});
        ASSERT_TRUE(saw_bomber);
        // nothing in the types of the view can say what an ant was ordered, or where it is going (detection idioms: false means "there is no such thing")
        ASSERT_FALSE(AntHasOrder<AntView>::value);
        ASSERT_FALSE(AntHasTarget<AntView>::value);
        ASSERT_FALSE(AntHasHarvestOrigin<AntView>::value);
    } TEST_END();

    TEST_CASE("AI1.2 Eggs And The Incubator: Only The Seat's Own, And The View Has No Way To Ask For Another Team's") {
        sim::SimulationEngine sim;
        build_world(sim, 22);
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) sim.set_player_eggs(t, 3u + t);
        sim.set_player_score(1, 300);
        ASSERT_TRUE(sim.try_hatch(1) == sim::SimulationEngine::HatchResult::Started);
        sim.tick();
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const BotView v = BotView::build(sim, t);
            ASSERT_EQ(v.eggs(), t == 1 ? 3u : 3u + t);                                  // team 1 used an egg
            ASSERT_EQ(v.hatching(), t == 1);                                            // and is the only one that has one incubating
        }
        // the interface: eggs() and hatching() take no seat, so the view can only answer for its own
        ASSERT_FALSE((CanAskEggsOf<BotView>::value));
        ASSERT_FALSE((CanAskHatchingOf<BotView>::value));
        // the egg count is what the engine says for the seat, and it moves when the engine moves (a new view)
        sim.set_player_eggs(2, 9);
        ASSERT_EQ(BotView::build(sim, 2).eggs(), 9u);
        ASSERT_EQ(BotView::build(sim, 0).eggs(), 3u);
        // an egg that has hatched is no longer incubating
        for (int i = 0; i < 200; ++i) sim.tick();
        ASSERT_FALSE(BotView::build(sim, 1).hatching());
        ASSERT_EQ(BotView::build(sim, 1).mine().size(), 13u);                           // the newborn
    } TEST_END();

    TEST_CASE("AI1.3 Food Piles: Units Left, Points Per Unit, The Anchor And The Table's Index; An Empty Pile Leaves The List, A Lunchbox Is One Unit, Two Objects On One Anchor Keep Their Own Indices") {
        sim::SimulationEngine sim;
        build_world(sim, 23);
        const int32_t crackers = place_crackers(sim, 20, 20, 25);
        const int32_t pretzels = place_pile(sim, 30, 20, 30, 40, {{30, 369}, {20, 370}, {10, 371}, {0, kPileGone}});
        const int32_t empty = place_pile(sim, 10, 30, 0, 10, {{0, kPileGone}});
        const int32_t twin_a = place_crackers(sim, 40, 40, 20);
        const int32_t twin_b = place_crackers(sim, 40, 40, 50);                          // the same anchor again: a second object of the table
        sim.grid_mut().drop_lunchbox(25, 35, 40);
        sim.tick();
        const BotView v = BotView::build(sim, 0);
        std::set<uint32_t> indices;
        for (const PileView& p : v.piles()) indices.insert(p.index);
        ASSERT_EQ(indices.size(), v.piles().size());                                    // every index once
        ASSERT_TRUE(std::is_sorted(v.piles().begin(), v.piles().end(), [](const PileView& a, const PileView& b) { return a.index < b.index; }));
        ASSERT_EQ(v.piles().size(), 5u);                                                // crackers, pretzels, both twins, the lunchbox: not the empty one
        const auto pile = [&](int32_t index) -> const PileView* {
            for (const PileView& p : v.piles()) {
                if (static_cast<int32_t>(p.index) == index) return &p;
            }
            return nullptr;
        };
        ASSERT_TRUE(pile(empty) == nullptr);
        const PileView* c = pile(crackers);
        ASSERT_TRUE(c != nullptr && c->remaining == 4 && c->value == 25 && c->anchor == tc(20, 20) && !c->lunchbox);
        const PileView* p = pile(pretzels);
        ASSERT_TRUE(p != nullptr && p->remaining == 30 && p->value == 40 && p->anchor == tc(30, 20));
        const PileView* a = pile(twin_a);
        const PileView* b = pile(twin_b);
        ASSERT_TRUE(a != nullptr && b != nullptr && a->index != b->index && a->anchor == b->anchor && a->value == 20 && b->value == 50);
        ASSERT_EQ(a->index, static_cast<uint32_t>(twin_a));                              // the index is the engine's table position
        size_t lunchboxes = 0;
        for (const PileView& q : v.piles()) {
            if (!q.lunchbox) continue;
            ++lunchboxes;
            ASSERT_TRUE(q.remaining == 1 && q.value == kLunchboxNominalValue && q.anchor == tc(25, 35));      // not the 40 points that the dead ant carried: the picture does not tell
        }
        ASSERT_EQ(lunchboxes, 1u);
        // the pile is the engine's table, read now: a bite shows in the next view, an eaten pile is gone, and indices of the others never move
        bool changed = false;
        sim.grid_mut().take_food(crackers, 1, changed);
        const BotView after = BotView::build(sim, 0);
        const PileView* c2 = nullptr;
        for (const PileView& q : after.piles()) c2 = q.index == static_cast<uint32_t>(crackers) ? &q : c2;
        ASSERT_TRUE(c2 != nullptr && c2->remaining == 3);
        sim.grid_mut().take_food(crackers, 3, changed);
        const BotView eaten = BotView::build(sim, 0);
        ASSERT_EQ(eaten.piles().size(), 4u);
        for (const PileView& q : eaten.piles()) ASSERT_TRUE(q.index != static_cast<uint32_t>(crackers));
        // every seat sees the same piles (the map is public)
        for (uint8_t t = 1; t < sim::MAX_PLAYERS; ++t) ASSERT_EQ(BotView::build(sim, t).piles().size(), 4u);
        // a shipped map: the piles of the table, with the units of the file
        sim::SimulationEngine real;
        start_match(real, "TREASURE", 1, 0x0F);
        const BotView tv = BotView::build(real, 0);
        ASSERT_EQ(tv.piles().size(), 18u);
        ASSERT_EQ(real.grid().food_objects().size(), 18u);
        for (const PileView& q : tv.piles()) {
            const sim::FoodObject& o = real.grid().food_objects()[q.index];
            ASSERT_TRUE(q.remaining == o.remaining && q.value == o.value && q.anchor == tc(o.col, o.row));
        }
    } TEST_END();

    TEST_CASE("AI1.4 Copies And Borrows: A View Stays What It Was When The Engine Moves On (The Engine's World State Is A Cache That Is Rewritten); Only The Grid And The Helpers Are Borrowed, And A Copy Of A View Has None") {
        sim::SimulationEngine sim;
        build_world(sim, 24);
        place_crackers(sim, 20, 20, 25);
        sim.tick();
        const sim::WorldState& ws = sim.get_world_state();                              // the cache: this very reference is rewritten by the next tick or command
        const sim::WorldState* cache = &ws;
        const int32_t tile_before = ws.ants[0].tile_x;
        const uint32_t first_id = ws.ants[0].id;
        BotView v = BotView::build(sim, ws.ants[0].player_id);
        const uint64_t tick_before = v.tick();
        const BotView frozen = v;                                                       // a copy: the data as it is now, nothing borrowed
        ASSERT_FALSE(frozen.has_grid());
        ASSERT_TRUE(v.has_grid());
        // move the world on: orders, ticks, a score, a bite, a changed tile
        const std::vector<uint32_t> own = ants_of(sim, v.seat());
        ASSERT_TRUE(sim.apply_command(move_of(v.seat(), own, 30, 30)).accepted());
        for (int i = 0; i < 120; ++i) sim.tick();
        sim.set_player_score(v.seat(), 777);
        bool changed = false;
        sim.grid_mut().take_food(0, 2, changed);
        sim.set_terrain(5, 5, sim::TERRAIN_WATER);
        ASSERT_TRUE(&sim.get_world_state() == cache);                                   // the same object ...
        ASSERT_TRUE(sim.get_world_state().ants[0].id == first_id && sim.get_world_state().ants[0].tile_x != tile_before);   // ... with other contents: the premise of the rule
        // the view did not follow
        ASSERT_EQ(v.tick(), tick_before);
        ASSERT_EQ(v.score(), 0);
        ASSERT_EQ(v.piles().front().remaining, 4);
        const AntView* a0 = find_ant(v.mine(), first_id);
        ASSERT_TRUE(a0 != nullptr && a0->tile.x == tile_before && a0->state == sim::UnitState::Idle);
        // the copy is the same data as the original view
        ASSERT_EQ(frozen.tick(), v.tick());
        ASSERT_EQ(frozen.mine().size(), v.mine().size());
        ASSERT_EQ(frozen.others().size(), v.others().size());
        ASSERT_EQ(frozen.piles().size(), v.piles().size());
        ASSERT_EQ(frozen.score(), v.score());
        // a fresh view is the new world
        const BotView fresh = BotView::build(sim, v.seat());
        ASSERT_TRUE(fresh.tick() > v.tick() && fresh.score() == 777 && fresh.piles().front().remaining == 2);
        // the borrow reads the engine itself, now: the grid is the engine's grid and shows the new tile; the copy has an empty grid
        ASSERT_TRUE(&v.grid() == &sim.grid());
        ASSERT_EQ(v.grid().get_cell(tc(5, 5)).terrain_type, sim::TERRAIN_WATER);
        ASSERT_EQ(frozen.grid().width(), 0u);
        ASSERT_EQ(frozen.grid().height(), 0u);
        ASSERT_EQ(frozen.predict_ack(move_of(v.seat(), {first_id}, 40, 40)), 0u);        // a copy cannot ask the engine
        ASSERT_FALSE(frozen.has_pending_path(first_id));
        // copy assignment drops the borrow as well; a move keeps it (the view that think() gets is built and handed over, never copied)
        BotView assigned;
        assigned = v;
        ASSERT_FALSE(assigned.has_grid());
        ASSERT_EQ(assigned.mine().size(), v.mine().size());
        BotView moved = std::move(v);
        ASSERT_TRUE(moved.has_grid() && &moved.grid() == &sim.grid());
        // a view built for a seat that is not in range falls back to seat 0 rather than reading outside its arrays
        ASSERT_EQ(BotView::build(sim, 9).seat(), 0);
    } TEST_END();

    TEST_CASE("AI1.5 The Engine's Own Prediction: predict_ack Is What apply_command Answers (On 600 Random Orders Over A Match), Only The Seat's Own Ants Count, And has_pending_path Knows Only Its Own Ants") {
        sim::SimulationEngine sim;
        build_world(sim, 25);
        place_crackers(sim, 12, 12, 25);
        sim.set_anthill(0, TileCoord{4, 4});
        BotRng rng(0xA11CE);
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        const std::vector<uint32_t> foes = ants_of(sim, 1);
        // the scripted case first: three ants, one command
        {
            const BotView v = BotView::build(sim, 0);
            uint32_t needed = 99;
            const Command c = move_of(0, {mine[0], mine[1], mine[2]}, 30, 30);
            const uint32_t predicted = v.predict_ack(c, &needed);
            ASSERT_TRUE(predicted != 0 && needed == 3);
            const sim::CommandResult r = sim.apply_command(c);
            ASSERT_TRUE(r.accepted() && r.ack_ant == predicted && r.needing_order == needed);
            ASSERT_TRUE(sim.has_pending_path(mine[0]) || sim.has_pending_path(mine[1]) || sim.has_pending_path(mine[2]));
            const BotView after = BotView::build(sim, 0);
            ASSERT_TRUE(after.has_pending_path(mine[0]) == sim.has_pending_path(mine[0]) && after.has_pending_path(mine[2]) == sim.has_pending_path(mine[2]));
            // the same click again: an ant that carries this very order out is left alone, the others are not, and the prediction tells which
            needed = 99;
            const uint32_t again = after.predict_ack(c, &needed);
            const sim::CommandResult r2 = sim.apply_command(c);
            ASSERT_TRUE(again == r2.ack_ant && needed == r2.needing_order && needed <= 3);
        }
        // another team's ants: an order given as team 1 has a pending path in the engine, which team 0 cannot know; and a command of team 0 that names them gets nothing
        ASSERT_TRUE(sim.apply_command(move_of(1, {foes[0], foes[1], foes[2], foes[3]}, 30, 30)).accepted());
        {
            const BotView v = BotView::build(sim, 0);
            bool engine_knows = false;
            for (uint32_t f : foes) engine_knows = engine_knows || sim.has_pending_path(f);
            ASSERT_TRUE(engine_knows);
            for (uint32_t f : foes) ASSERT_FALSE(v.has_pending_path(f));
            uint32_t needed = 99;
            ASSERT_EQ(v.predict_ack(move_of(0, {foes[0], foes[1]}, 40, 40), &needed), 0u);
            ASSERT_EQ(needed, 0u);
            ASSERT_EQ(v.predict_ack(move_of(2, {foes[0]}, 40, 40), &needed), 0u);        // the issuer of the command is overwritten with the seat's own
            ASSERT_FALSE(v.has_pending_path(0xFFFFFF));                                  // an ant that does not exist
            // a mix: only the own ant counts
            ASSERT_TRUE(v.predict_ack(move_of(0, {foes[0], mine[5]}, 40, 40), &needed) == mine[5] && needed == 1);
            // the helpers answer nothing but "no" for an ant that is not the seat's, whatever the engine says about it
            const BotView foe_view = BotView::build(sim, 1);
            ASSERT_TRUE(foe_view.has_pending_path(foes[0]) == sim.has_pending_path(foes[0]));
        }
        // 600 random commands of seat 0 (group moves and attacks and specials at random tiles, the pile, the hill, enemy hills; one to five ants)
        size_t probes = 0;
        size_t acknowledged = 0;
        for (int step = 0; step < 600; ++step) {
            const BotView v = BotView::build(sim, 0);
            Command c;
            const uint32_t pick = rng.below(10);
            c.type = pick < 6 ? CommandType::GroupMove : pick < 8 ? CommandType::GroupAttack : CommandType::GroupSpecial;
            c.issuer = 0;
            const uint32_t n = 1 + rng.below(5);
            for (uint32_t i = 0; i < n; ++i) c.ants.push_back(mine[rng.below(static_cast<uint32_t>(mine.size()))]);
            const uint32_t where = rng.below(8);
            if (where == 0) { c.tile_x = 12; c.tile_y = 12; }                              // the pile
            else if (where == 1) { c.tile_x = 5; c.tile_y = 5; }                            // the own hill
            else if (where == 2) { c.tile_x = 51; c.tile_y = 5; }                           // another team's hill
            else { c.tile_x = static_cast<int16_t>(rng.below(60)); c.tile_y = static_cast<int16_t>(rng.below(60)); }
            uint32_t needed = 0;
            const uint32_t predicted = v.predict_ack(c, &needed);
            const sim::CommandResult r = sim.apply_command(c);
            ASSERT_TRUE(r.status == sim::CommandResult::Status::Applied || r.status == sim::CommandResult::Status::Ignored);
            ASSERT_EQ(predicted, r.ack_ant);
            ASSERT_EQ(needed, r.needing_order);
            ++probes;
            acknowledged += predicted != 0 ? 1u : 0u;
            // a one-ant order: the hint takes_orders() is what the engine would accept
            const AntView& a = v.mine()[rng.below(static_cast<uint32_t>(v.mine().size()))];
            const uint32_t one = v.predict_ack(move_of(0, {a.id}, 57, 57));
            if (!a.takes_orders()) ASSERT_EQ(one, 0u);                                    // an ant that is attacking, entering the hill, harvesting, placing ...: no order
            if (one != 0) ASSERT_EQ(one, a.id);
            for (int t = 0; t < 1 + static_cast<int>(rng.below(4)); ++t) sim.tick();
        }
        ASSERT_EQ(probes, 600u);
        ASSERT_TRUE(acknowledged > 150);                                                  // the probes did give orders that were acknowledged (not 600 refusals)
    } TEST_END();

    TEST_CASE("AI1.6 The Hint takes_orders() Agrees With The Engine Over A Scripted Scenario: Every State An Ant Passes Through On Its Way To A Pile, Its Bite And Its Way Home") {
        sim::SimulationEngine sim;
        build_world(sim, 26, 6);
        sim.set_anthill(0, TileCoord{4, 4});
        place_crackers(sim, 12, 10, 25);
        const std::vector<uint32_t> mine = ants_of(sim, 0);
        ASSERT_TRUE(sim.apply_command(move_of(0, mine, 12, 10)).accepted());              // onto the pile: a harvest loop that passes through walking, the bite, the hill entrance, ...
        std::set<sim::UnitState> seen;
        size_t refused = 0;
        size_t accepted = 0;
        for (int t = 0; t < 900; ++t) {
            sim.tick();
            const BotView v = BotView::build(sim, 0);
            for (const AntView& a : v.mine()) {
                seen.insert(a.state);
                const uint32_t ack = v.predict_ack(move_of(0, {a.id}, 57, 57));
                if (a.takes_orders()) {
                    // every state that takes orders is one the engine accepts, unless the ant is mid-bite or struck (engaged / frozen): never the other way round
                    ack == a.id ? ++accepted : ++refused;
                } else {
                    ASSERT_EQ(ack, 0u);
                }
                ASSERT_EQ(a.idle(), a.state == sim::UnitState::Idle || a.state == sim::UnitState::GuardIdle || a.state == sim::UnitState::Swimming);
            }
        }
        ASSERT_TRUE(seen.count(sim::UnitState::Idle) && seen.count(sim::UnitState::Walking));
        ASSERT_TRUE(seen.count(sim::UnitState::HarvestingFood) || seen.count(sim::UnitState::EnteringBase));      // states that do not take orders were passed through, too
        ASSERT_TRUE(accepted > 500);
        ASSERT_TRUE(refused * 20 < accepted);                                             // takes_orders() with a refusal is rare (an ant that is engaged or frozen)
    } TEST_END();

    TEST_CASE("AI1.7 The Map Analysis Reaches The Bot: The Controller Owns One MapInfo, The Bot Gets It At Start And In Every View; A View Built By Hand Has None; A Look Costs In Proportion To The Ants") {
        sim::SimulationEngine sim;
        build_world(sim, 27);
        RecordingSink sink(sim);
        BotController c(sim, 5);
        c.set_start_hold(0);   // the view and the map at the first look, 30 ticks into the match (the start hold: AI2.17 - AI2.22)
        const MapInfo* seen_in_view = nullptr;
        bool grid_is_engine = false;
        ScriptBot* bot = nullptr;
        {
            auto b = std::make_unique<ScriptBot>([&](const BotView& v, Orders&) {
                seen_in_view = v.map();
                grid_is_engine = v.has_grid() && &v.grid() == &sim.grid();
            });
            bot = b.get();
            BotSpec spec;
            spec.seat = 0;
            spec.kind = "idle";
            std::string why;
            ASSERT_TRUE(c.add(spec, std::move(b), sink, why));
        }
        ASSERT_TRUE(bot->map == &c.map());                                                 // BotContext::map
        for (int i = 0; i < 30; ++i) {
            sim.tick();
            c.on_tick(sim);
        }
        ASSERT_TRUE(!bot->thought.empty());
        ASSERT_TRUE(seen_in_view == &c.map());                                              // BotView::map()
        ASSERT_TRUE(grid_is_engine);
        ASSERT_TRUE(c.map().hill(0).present && c.map().hill(0).origin == tc(4, 4) && c.map().hill(1).origin == tc(50, 4));
        ASSERT_TRUE(BotView::build(sim, 0).map() == nullptr);
        ASSERT_TRUE(BotView::build(sim, 0, &c.map()).map() == &c.map());
        // the cost of a look grows with the ants and no faster: a world of 500 ants against one of 48 (10 times as many); the design measured 3 - 27 microseconds on a release build, and an
        // absolute limit means nothing in a build with sanitizers, so the test compares the two (linear: about 10; an accidental quadratic loop: about 100). The build includes the
        // rebuild of the engine's world state that every first look after a tick makes. The figure is the FASTEST of 300 looks, not their mean: a busy machine (a Docker build next to
        // the test run) can only make a look slower, and one preemption of 30 ms inside the mean of the big world used to fail a correct build; the minimum ignores it.
        const auto cost_of_a_look = [&](uint32_t ants_per_team) {
            sim::SimulationEngine world;
            build_world(world, 28, ants_per_team);
            const MapInfo map(world);
            double fastest = 1e300;
            size_t sum = 0;
            const int rounds = 300;
            for (int i = 0; i < rounds; ++i) {
                world.tick();
                const auto t0 = std::chrono::steady_clock::now();
                const BotView v = BotView::build(world, static_cast<uint8_t>(i % 4), &map);
                fastest = std::min(fastest, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
                sum += v.mine().size() + v.others().size();
            }
            return std::make_pair(fastest, sum / static_cast<size_t>(rounds));
        };
        const auto small = cost_of_a_look(12);
        const auto big = cost_of_a_look(125);
        ASSERT_EQ(small.second, 48u);                                                                   // a view lists every ant: its own and the three other teams'
        ASSERT_EQ(big.second, 500u);
        ASSERT_TRUE(big.first <= small.first * 25.0 + 20.0);
    } TEST_END();
    TEST_CASE("AI1.21 A Level With A Default Ant Type (LVL Block 3: Some Community Maps): Every Ant Of The View Shows That Type, The View Says Which, A Copy Keeps It; The Worker Bot Still Harvests With Them") {
        const uint16_t tiles[5] = {62, 63, 64, 65, 66};
        const sim::AntType types[5] = {sim::AntType::Combat, sim::AntType::Thief, sim::AntType::Bomber, sim::AntType::Swimmer, sim::AntType::Fire};
        for (int k = 0; k < 5; ++k) {
            assets::LevelData level = level_of("TINY");
            level.ambient_tile_or_sound = tiles[k];                                          // the entry's name does not matter (this dictionary calls entry 62 "." or not)
            sim::SimulationEngine sim;
            sim.init(level, 3, 0x0F);
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const BotView v = BotView::build(sim, seat);
                ASSERT_EQ(v.default_ant_type(), types[k]);
                ASSERT_EQ(v.mine().size() + v.others().size(), 12u);
                for (const AntView& a : v.mine()) ASSERT_EQ(a.type, types[k]);              // what the sprite shows
                for (const AntView& a : v.others()) ASSERT_EQ(a.type, types[k]);
                const BotView copy = v;
                ASSERT_EQ(copy.default_ant_type(), types[k]);
            }
        }
        sim::SimulationEngine plain;                                                         // a shipped map: workers
        start_match(plain, "TINY", 3, 0x0F);
        ASSERT_EQ(BotView::build(plain, 0).default_ant_type(), sim::AntType::Worker);
        // an ant that took a power-up shows the type of its own, among the workers of a default level
        assets::LevelData combat = level_of("TINY");
        combat.ambient_tile_or_sound = 62;
        sim::SimulationEngine mixed;
        mixed.init(combat, 3, 0x0F);
        const uint32_t thief = mixed.spawn_unit(0, sim::AntType::Thief, sim::TileCoord{12, 10});
        const BotView mixed_view = BotView::build(mixed, 0);
        const AntView* seen = find_ant(mixed_view.mine(), thief);
        ASSERT_TRUE(seen != nullptr);
        ASSERT_EQ(seen->type, sim::AntType::Thief);
        // the worker bot of a seat on such a level: its pool is the ants that show the default type, so it harvests as it always did (the idle seats do nothing)
        for (const uint16_t tile : {uint16_t{62}, uint16_t{65}}) {
            assets::LevelData lvl = level_of("TINY");
            lvl.ambient_tile_or_sound = tile;
            ArenaSpec spec;
            spec.level = &lvl;
            spec.seed = 1;
            spec.max_ticks = 2400;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = seat == 0 ? "worker" : "idle";
                b.level = Level::Medium;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_EQ(r.error, std::string());
            ASSERT_TRUE(r.seats[0].score >= 450);                                            // two minutes of harvesting on TINY (the same 600 as on the shipped map); an economy that finds no worker in the pool scores 0
        }
    } TEST_END();
}
