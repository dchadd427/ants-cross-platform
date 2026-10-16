// The denial order of the standard bot (AI28.x; docs/BOT_FOOD.md, "Medium and Hard: the denial order"): food is finite, and what lies where other teams reach it as soon
// is eaten by whoever gets there, so a Medium or Hard bot eats contested food before its own reserve, unless the contested food is too far to be worth the walk or an enemy
// comes for the reserve. Hand-made worlds only (ReserveWorld, contest_helpers.hpp), each drawn in its eight orientations and played from every seat: the rule is about who
// reaches a pile how soon, never about one map.
// (A hill is not symmetric itself: its entrance is on one side of the mound in every drawing, so the trips, and the ants that a pile has room for, differ a little between the
// drawings; what must not differ is the decision.)
//
//   AI28.1  a pile that every enemy reaches as soon as the seat goes before the seat's reserve of the same value with up to 2.3 times its trip (Medium, Hard); a pile that ONE
//           enemy reaches (25 points a unit) goes before a reserve of 20 points with up to 1.7 times its trip, not before one of 25 points; Easy keeps its order
//   AI28.2  a contested pile with 3.7 times the trip of the reserve does not go first: the denial value weighs the points per trip, it does not send the ants across the field
//   AI28.3  a reserve pile with an enemy ant beside it goes before everything else (it is being taken); an ally's ant there is no threat
//   AI28.4  a contested pile that an enemy army holds counts as it is (Hopeless)
#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "contest_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using namespace contest;

namespace {

constexpr size_t kPiles = 5;          // the middle pile and the four reserves of a ReserveWorld

// What one standard bot ordered in its first 60 ticks: the pile of its first harvest order (0 the middle, 1 its own reserve, 2 another pile, -1 none), the ants sent to the
// middle and to its own reserve, and the class of the middle pile for it (PileClass as a number, -1 when no order reads classes)
struct Outcome {
    int first{-1};
    size_t middle{0};
    size_t reserve{0};
    int middle_class{-1};
};

struct Game {
    int orient{0};
    int32_t reserve_at{19};                           // the reserve's anchor is (reserve_at, reserve_at) at team 0 in the first drawing
    uint16_t reserve_value{25};                       // its points per unit (the middle's are 25)
    std::array<size_t, 4> enemies{{1, 1, 1, 1}};      // the workers of every team but the seat (an enemy needs one ant to be a live team)
    bool threat{false};                               // a worker of the next team stands 4 tiles beside the seat's reserve ...
    bool ally{false};                                 // ... and that team is the seat's ally
    bool army{false};                                 // a Combat Ant of the team across stands 2 tiles beside the middle pile
};

Outcome play(Level level, uint8_t seat, const Game& g) {
    sim::SimulationEngine sim;
    ReserveWorld w;
    w.orient = g.orient;
    std::array<size_t, 4> workers = g.enemies;
    workers[seat] = 6;
    w.build(sim, g.reserve_at, workers, 25, g.reserve_value);
    for (uint8_t t = 0; t < 4; ++t) {
        if (workers[t] == 0) sim.drop_player(t);                    // a team that is not live competes for nothing
    }
    if (g.threat) sim.spawn_unit(static_cast<uint8_t>((seat + 1) % 4), sim::AntType::Worker, w.tile_of(seat, TileCoord{g.reserve_at - 4, g.reserve_at}));
    if (g.ally) sim.form_alliance(seat, static_cast<uint8_t>((seat + 1) % 4));
    if (g.army) sim.spawn_unit(static_cast<uint8_t>((seat + 2) % 4), sim::AntType::Combat, w.tile_of(seat, TileCoord{27, 30}));
    Rig rig(sim, seat, level, std::make_unique<StandardBot>(plan_for(level)), 4, 4);
    rig.run(60);
    const std::vector<int> targets = harvest_targets(rig, sim, seat, kPiles);
    Outcome out;
    if (!targets.empty()) out.first = targets.front() == w.middle ? 0 : targets.front() == w.reserve[seat] ? 1 : 2;
    out.middle = ants_to_pile(rig, sim, kPiles, w.middle, 60);
    out.reserve = ants_to_pile(rig, sim, kPiles, w.reserve[seat], 60);
    out.middle_class = rig.as<StandardBot>().harvest().tier_of(static_cast<uint32_t>(w.middle));
    return out;
}

// The seat's trip to the middle pile in percent of its trip to its reserve (the start analysis' costs; the points per trip that the order weighs): the premise of a case
uint32_t far_percent(const Game& g, uint8_t seat) {
    sim::SimulationEngine sim;
    ReserveWorld w;
    w.orient = g.orient;
    w.build(sim, g.reserve_at, g.enemies, 25, g.reserve_value);
    const MapInfo map(sim);
    const int32_t to_middle = MapInfo::trip_ticks_for_cost(map.pile(static_cast<uint32_t>(w.middle))->approach[seat].cost);
    const int32_t to_reserve = MapInfo::trip_ticks_for_cost(map.pile(static_cast<uint32_t>(w.reserve[seat]))->approach[seat].cost);
    return to_reserve > 0 ? static_cast<uint32_t>(to_middle * 100 / to_reserve) : 0u;
}

// The shortest and the longest of those percentages over the eight drawings and the seats of the live teams (a hill's entrance is on one side of its mound, so they differ)
std::pair<uint32_t, uint32_t> far_range(Game g) {
    std::pair<uint32_t, uint32_t> out{~0u, 0u};
    for (int orient = 0; orient < 8; ++orient) {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            if (g.enemies[seat] == 0) continue;
            g.orient = orient;
            const uint32_t f = far_percent(g, seat);
            out.first = std::min(out.first, f);
            out.second = std::max(out.second, f);
        }
    }
    return out;
}

// The case decides alike in all eight drawings and from every seat of a live team: the same first pile, and `holds` is true of what was sent
template <class Holds>
void alike_everywhere(Level level, Game g, int first, Holds holds) {
    for (int orient = 0; orient < 8; ++orient) {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            if (g.enemies[seat] == 0) continue;
            g.orient = orient;
            const Outcome o = play(level, seat, g);
            ASSERT_EQ(o.first, first);
            ASSERT_TRUE(holds(o));
        }
    }
}

}  // namespace

void run_deny_tests() {
    TEST_CASE("AI28.1 The Denial Order (Reported After Play: On A Four-Player Map The Bots Eat The Cheerios Next To Their Base First Instead Of The Contested Middle Food): A Pile That Every Enemy Reaches As Soon As The Seat Goes Before The Own Reserve Of The Same Value With Up To 2.3 Times Its Trip (Medium, Hard), One That One Enemy Reaches Before A Reserve Of 20 Points With 1.7 Times Its Trip, Not Before One Of 25 Points; In All Eight Drawings And From Every Seat; Easy Keeps Its Order")
    {
        // four live teams: the middle is contested by three enemies; its trip is 1.6 to 1.8 times the reserve's (the reserve 19 tiles from the corner) or 2.0 to 2.3 times (16)
        Game g;
        const auto far = far_range(g);
        ASSERT_TRUE(far.first >= 150 && far.second <= 190);
        Game farther;
        farther.reserve_at = 16;
        const auto farther_far = far_range(farther);
        ASSERT_TRUE(farther_far.first >= 190 && farther_far.second <= 240);
        for (const Level level : {Level::Medium, Level::Hard}) {
            // the middle first, all six ants there and nobody at the reserve (the plain order sends the first ants to the reserve: it has the better points per trip)
            alike_everywhere(level, g, 0, [](const Outcome& o) { return o.middle == 6u && o.reserve == 0u && o.middle_class == static_cast<int>(PileClass::Multi); });
            alike_everywhere(level, farther, 0, [](const Outcome& o) { return o.middle > o.reserve; });
        }
        // ONE enemy (teams 2 and 3 are out of the match): the middle is contested by one team and counts less. Against a reserve of 20 points a unit with 1.5 to 1.7 times
        // its trip (20) the middle of 25 goes first (the plain order: the reserve); against a reserve of 25 with 1.7 to 2.0 times its trip (18) it does not
        Game one;
        one.enemies = {{1, 1, 0, 0}};
        one.reserve_at = 20;
        one.reserve_value = 20;
        const auto one_far = far_range(one);
        ASSERT_TRUE(one_far.first >= 150 && one_far.second <= 172);
        Game one_farther;
        one_farther.enemies = one.enemies;
        one_farther.reserve_at = 18;
        const auto one_farther_far = far_range(one_farther);
        ASSERT_TRUE(one_farther_far.first >= 165 && one_farther_far.second <= 200);
        for (const Level level : {Level::Medium, Level::Hard}) {
            alike_everywhere(level, one, 0, [](const Outcome& o) { return o.reserve == 0u && o.middle_class == static_cast<int>(PileClass::One); });
            alike_everywhere(level, one_farther, 1, [](const Outcome& o) { return o.reserve >= 3u && o.middle_class == static_cast<int>(PileClass::One); });
        }
        // Easy keeps its order: its first ants work the nearest pile, the reserve
        ASSERT_TRUE(play(Level::Easy, 0, g).reserve > 0u);
    } TEST_END();

    TEST_CASE("AI28.2 The Denial Value Is A Weight, Not A Rank: A Pile That Every Enemy Reaches As Soon As The Seat, With 3.7 Times The Trip Of The Own Reserve Or More, Does Not Go First (No Walk Across The Field While The Reserve Is Near); In All Eight Drawings And From Every Seat")
    {
        Game g;
        g.reserve_at = 9;
        const auto far = far_range(g);
        ASSERT_TRUE(far.first >= 360);
        // (the first ants go to the reserve, as many as it has room for: two or three at Medium, whose gate takes a deposit per 90 ticks, more at Hard)
        for (const Level level : {Level::Medium, Level::Hard}) alike_everywhere(level, g, 1, [](const Outcome& o) { return o.reserve >= 2u && o.middle_class == static_cast<int>(PileClass::Multi); });
    } TEST_END();

    TEST_CASE("AI28.3 A Reserve That An Enemy Comes For Is Eaten Now: With An Enemy Ant Four Tiles From The Own Reserve, The Reserve Goes Before The Contested Middle And Gets Most Ants (An Ally's Ant There Changes Nothing); In All Eight Drawings And From Every Seat")
    {
        Game g;
        g.threat = true;
        for (const Level level : {Level::Medium, Level::Hard}) alike_everywhere(level, g, 1, [](const Outcome& o) { return o.reserve >= 5u && o.reserve > o.middle; });
        g.ally = true;
        for (const Level level : {Level::Medium, Level::Hard}) alike_everywhere(level, g, 0, [](const Outcome& o) { return o.reserve == 0u; });
    } TEST_END();

    TEST_CASE("AI28.4 A Contested Pile That An Enemy Army Holds Counts As It Is: With An Enemy Combat Ant Beside The Middle Pile (And None Of The Seat's Own There) The Middle Reads Hopeless And The Nearer Reserve Goes First; In All Eight Drawings And From Every Seat")
    {
        Game g;
        g.army = true;
        for (const Level level : {Level::Medium, Level::Hard}) alike_everywhere(level, g, 1, [](const Outcome& o) { return o.reserve >= 3u && o.middle_class == static_cast<int>(PileClass::Hopeless); });
    } TEST_END();
}
