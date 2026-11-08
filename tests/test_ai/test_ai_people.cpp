// The yardstick for computer players against people (docs/BOT_PEOPLE.md): the "people" numbers of bot_watch on hand-made streams of snapshots, five scenes in which the standard bot at Hard is
// judged against an opponent that the test scripts (synthetic: no recording of anybody's game), and the bench bot `gang`. Nothing here changes how a bot plays.
//
//   AI34.1  a gang of three hits one of four ants that are far from home: the bot must hit back (at least six blows in 600 ticks) and lose at most one ant   (known failure: Step 1)
//   AI34.2  an enemy Fire Ant camps four tiles from the hill of a bot with two ants: it is not left to kill them alone                                          (known failure: Step 3)
//   AI34.3  a lone ant on its way to a Fire power-up does not walk up to an enemy Combat Ant alone                                                              (known failure: Step 3)
//   AI34.4  a hurt Bomber that an enemy worker hits strikes back within 40 ticks or walks home                                                                  (known failure: Step 1)
//   AI34.5  an enemy Thief raids a hill that has no Fire Ant three times: before the fourth raid the hole is shut or the Thief is struck                       (known failure: Step 5)
//   AI34.6  the blows, the fights, hitting back and helping, the crowd at a fight and the focus on a victim, from a stream of snapshots
//   AI34.7  heals and Fire Ants (the walls lit at an enemy gate)
//   AI34.8  deaths that could have walked home (the speed of the match's walkers, the window, the closed hill of 187)
//   AI34.9  the mines laid round the own hill by tick 3,000, 6,000 and 12,000, the mines laid elsewhere, the enemy ants that a mine hit
//   AI34.10 the observer reads the engine and changes nothing: a match with it ends on the same state hash as without it
//   AI34.11 the bench bot gang plays only orders that a person could give, is not in the registry, and plays the same match twice alike
//
// A known failure is a scene that the shipped bot fails today and that a later step of the bot work has to make pass: it runs, prints "KNOWN FAIL AI34.n (Step k)" with what it measured, and is green.
// The day it passes the case turns red ("NOW PASSES"): the step that fixed it makes the case required.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bench_aggressor.hpp"
#include "bot_watch_people.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

using watch::AntSnap;
using watch::PlaceSnap;
using watch::SeatPeople;
using watch::Snapshot;

// ---- hand-made snapshots ---------------------------------------------------------------------------------------------------------------------------------------

AntSnap ant(uint32_t id, uint8_t seat, int32_t x, int32_t y, unsigned hp = 10, sim::UnitState state = sim::UnitState::Idle, sim::AntType type = sim::AntType::Worker) {
    AntSnap a;
    a.id = id;
    a.seat = seat;
    a.tile = TileCoord{x, y};
    a.hp = static_cast<uint16_t>(hp);
    a.state = state;
    a.type = type;
    return a;
}

PlaceSnap place(int32_t x, int32_t y, uint8_t owner) {
    PlaceSnap p;
    p.tile = TileCoord{x, y};
    p.owner = owner;
    return p;
}

Snapshot snap(uint64_t tick, const std::vector<AntSnap>& ants, const std::vector<PlaceSnap>& bombs = {}, const std::vector<PlaceSnap>& fires = {}) {
    Snapshot s;
    s.tick = tick;
    s.ants = ants;
    s.bombs = bombs;
    s.fires = fires;
    return s;
}

// Seats 0 and 1 play; their hills' entrances are at (5, 5) and (50, 5)
watch::Setup two_seats(bool closed = false) {
    watch::Setup s;
    s.present[0] = s.present[1] = true;
    s.entrance[0] = TileCoord{5, 5};
    s.entrance[1] = TileCoord{50, 5};
    s.hill_closed = closed;
    return s;
}

constexpr sim::UnitState kIdle = sim::UnitState::Idle;
constexpr sim::UnitState kHit = sim::UnitState::Attacking;

// ---- the scenes ------------------------------------------------------------------------------------------------------------------------------------------------

// What a scene found: whether the rule held, and what was measured (for the line of a known failure)
struct Verdict {
    bool pass{true};
    std::string note;
};

// A Hard bot at seat 0 through the real controller (its budget, reaction time and filter), a scripted opponent, and the people numbers read after every tick
class Scene {
public:
    sim::SimulationEngine sim;
    watch::PeopleObserver seen;

    // The world must be built before start(): the controller analyses the map when it is made
    void start(uint32_t seed, Style style, Level level = Level::Hard) {
        sink_ = std::make_unique<RecordingSink>(sim, true);
        controller_ = std::make_unique<BotController>(sim, seed);
        BotSpec spec;
        spec.seat = 0;
        spec.kind = "standard";
        spec.level = level;
        spec.style = style;
        std::string why;
        if (!controller_->add(spec, *sink_, why)) throw std::runtime_error("the bot was not seated: " + why);
    }
    // One tick: the engine, the bot's looks and the orders that leave, the observer, then the script's orders (they take effect in the next tick)
    void step(const std::function<void(uint64_t)>& script = {}) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
        controller_->on_tick(sim);
        seen.tick(sim);
        if (script) script(sim.current_tick());
    }
    SeatPeople bot() const { return seen.people().seat(0); }
    size_t blows_by(uint32_t striker, uint32_t victim, uint64_t from = 0, uint64_t to = ~uint64_t{0}) const {
        size_t n = 0;
        for (const watch::Blow& b : seen.people().blows()) n += b.striker == striker && b.victim == victim && b.tick >= from && b.tick <= to ? 1u : 0u;
        return n;
    }

private:
    std::unique_ptr<RecordingSink> sink_;
    std::unique_ptr<BotController> controller_;
};

constexpr uint64_t kWarmUp = 40;               // the bot's Memory starts at the first tick of the scene: nobody strikes before it has looked a few times

// The enemy ant `enemy` strikes `victim` again the moment it is idle after a blow
void strike_again(Scene& s, uint32_t enemy, uint32_t victim) {
    if (victim == 0 || !alive(s.sim, enemy)) return;
    const sim::AntSnapshot* e = snapshot_of(s.sim, enemy);
    const sim::AntSnapshot* v = snapshot_of(s.sim, victim);
    if (e == nullptr || v == nullptr || (e->state != sim::UnitState::Idle && e->state != sim::UnitState::GuardIdle)) return;
    s.sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, v->tile_x, v->tile_y));
}

uint32_t first_alive(const sim::SimulationEngine& sim, const std::vector<uint32_t>& ids) {
    for (const uint32_t id : ids) {
        if (alive(sim, id)) return id;
    }
    return 0;
}

std::string styled(Style style, const std::string& what) { return std::string(style_name(style)) + ": " + what; }

// AI34.1: four ants of the bot (two Thieves, a Fire Ant and a worker, all at full health and empty) stand together 12 tiles from their hill; three enemy workers strike a Thief again after every blow
Verdict scene_gang_on_four(Style style) {
    Scene s;
    empty_field(s.sim, 341);
    const std::vector<uint32_t> mine = {s.sim.spawn_unit(0, sim::AntType::Thief, TileCoord{16, 16}), s.sim.spawn_unit(0, sim::AntType::Thief, TileCoord{18, 16}),
                                        s.sim.spawn_unit(0, sim::AntType::Fire, TileCoord{17, 18}), s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{16, 18})};
    const std::vector<uint32_t> enemies = {s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 17}), s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 19}),
                                           s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{22, 18})};
    s.start(341, style);
    for (uint64_t t = 0; t < kWarmUp + 600; ++t) {
        s.step([&](uint64_t now) {
            if (now < kWarmUp) return;
            const uint32_t victim = first_alive(s.sim, mine);
            for (const uint32_t e : enemies) strike_again(s, e, victim);
        });
    }
    const SeatPeople p = s.bot();
    Verdict v;
    v.pass = p.blows_struck >= 6 && p.deaths <= 1;
    v.note = styled(style, "blows " + std::to_string(p.blows_struck) + " (at least 6), ants lost " + std::to_string(p.deaths) + " (at most 1)");
    return v;
}

// AI34.2: the bot has two ants (one at home, one hatched); an enemy Fire Ant stands four tiles from its hill entrance and strikes any ant of the bot that comes within three tiles
Verdict scene_camper(Style style) {
    Scene s;
    empty_field(s.sim, 342);
    add_pile(s.sim, 30, 10, 40, 25);
    const uint32_t home_ant = s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 3});
    const uint32_t hatched = s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 3});
    s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{50, 10});                                    // (the enemy hill has an ant: a team that plays)
    const uint32_t camper = s.sim.spawn_unit(1, sim::AntType::Fire, TileCoord{9, 5});                // four tiles from the entrance (5, 5)
    s.start(342, style);
    for (uint64_t t = 0; t < kWarmUp + 1200; ++t) {
        s.step([&](uint64_t now) {
            if (now < kWarmUp || !alive(s.sim, camper)) return;
            const sim::AntSnapshot* c = snapshot_of(s.sim, camper);
            if (c == nullptr || (c->state != sim::UnitState::Idle && c->state != sim::UnitState::GuardIdle)) return;
            const sim::AntSnapshot* best = nullptr;
            for (const uint32_t id : {home_ant, hatched}) {
                const sim::AntSnapshot* m = snapshot_of(s.sim, id);
                if (m == nullptr || !alive(s.sim, id) || std::max(std::abs(m->tile_x - c->tile_x), std::abs(m->tile_y - c->tile_y)) > 3) continue;
                if (best == nullptr) best = m;
            }
            if (best != nullptr) s.sim.apply_command(command_of(CommandType::GroupAttack, 1, {camper}, best->tile_x, best->tile_y));
        });
    }
    std::set<uint32_t> strikers;
    for (const watch::Blow& blow : s.seen.people().blows()) {
        if (blow.victim == camper && blow.striker_seat == 0) strikers.insert(blow.striker);
    }
    const SeatPeople p = s.bot();
    Verdict v;
    v.pass = p.deaths == 0 || strikers.size() >= 2;
    v.note = styled(style, "ants lost " + std::to_string(p.deaths) + ", ants that struck the camper " + std::to_string(strikers.size()) + " (no loss, or at least 2)");
    return v;
}

// AI34.3: the bot's only ant is sent for a Fire power-up 10 tiles away (a worker: only a worker is sent for a power-up). An enemy Combat Ant stands 2 tiles from the power-up: from the start
// (the bot then does not send the ant at all: it would lose the race) or from tick 30 (it came while the ant was on its way). The ant must not stand within a tile of it with no second ant of the bot
// within two tiles.
Verdict scene_lone_ant(Style style, bool combat_from_start) {
    Scene s;
    empty_field(s.sim, 343);
    s.sim.grid_mut().place_powerup(20, 10, 2);
    const uint32_t mine = s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 10});
    s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{50, 10});
    uint32_t combat = combat_from_start ? s.sim.spawn_unit(1, sim::AntType::Combat, TileCoord{22, 10}) : 0;
    s.start(343, style);
    bool close = false;
    for (uint64_t t = 0; t < 640; ++t) {
        s.step([&](uint64_t now) {
            if (now == 30 && combat == 0) combat = s.sim.spawn_unit(1, sim::AntType::Combat, TileCoord{22, 10});
            const sim::AntSnapshot* m = snapshot_of(s.sim, mine);
            const sim::AntSnapshot* c = combat != 0 ? snapshot_of(s.sim, combat) : nullptr;
            if (m == nullptr || c == nullptr || !alive(s.sim, mine)) return;
            close = close || std::max(std::abs(m->tile_x - c->tile_x), std::abs(m->tile_y - c->tile_y)) <= 1;         // (the bot has no second ant)
        });
    }
    Verdict v;
    v.pass = !close;
    v.note = styled(style, std::string("the Combat Ant is there ") + (combat_from_start ? "from the start" : "from tick 30") + ", the ant " + (close ? "stood next to it alone" : "kept away"));
    return v;
}

// AI34.4: the bot's Bomber (empty, 6 hit points) is struck by an enemy worker that strikes again after every blow. It walks towards its hill when, after the first blow, it gains three tiles on the hill's
// entrance while it is in its walking clip (a blow throws it a tile away from the striker, which is no walk)
Verdict scene_hurt_bomber(Style style) {
    Scene s;
    empty_field(s.sim, 344);
    add_pile(s.sim, 30, 12, 40, 25);
    const uint32_t bomber = s.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{16, 10});
    s.sim.get_unit(bomber).hp = 6;
    const uint32_t enemy = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 10});
    s.start(344, style);
    const TileCoord home = TileCoord{5, 5};
    uint64_t first_blow = 0;
    int32_t before = -1;
    bool was_walking = false;
    int32_t gained = 0;
    for (uint64_t t = 0; t < kWarmUp + 400; ++t) {
        s.step([&](uint64_t now) {
            if (now >= kWarmUp) strike_again(s, enemy, bomber);
            for (const watch::Blow& b : s.seen.people().blows()) {
                if (first_blow == 0 && b.victim == bomber) first_blow = b.tick;
            }
            const sim::AntSnapshot* m = snapshot_of(s.sim, bomber);
            if (first_blow == 0 || m == nullptr || !alive(s.sim, bomber) || now > first_blow + 300) return;
            const int32_t d = std::max(std::abs(m->tile_x - home.x), std::abs(m->tile_y - home.y));
            const bool walking = m->state == sim::UnitState::Walking;
            if (before >= 0 && was_walking && walking && d < before) gained += before - d;
            before = d;
            was_walking = walking;
        });
    }
    const size_t back = first_blow == 0 ? 0 : s.blows_by(bomber, enemy, first_blow, first_blow + 40);
    Verdict v;
    v.pass = first_blow != 0 && (back > 0 || gained >= 3);
    v.note = styled(style, std::string("first blow at tick ") + std::to_string(first_blow) + ", blows back within 40 ticks " + std::to_string(back) + ", tiles walked towards its hill " + std::to_string(gained));
    return v;
}

// AI34.5: an enemy Thief raids the bot's hill again and again (its own hill is 46 tiles away: a round trip of about 800 ticks, so three raids in 2,400 ticks). The bot has four workers and no Fire Ant.
Verdict scene_thief_hole(Style style) {
    Scene s;
    empty_field(s.sim, 345);
    add_pile(s.sim, 14, 14, 40, 25);
    for (int i = 0; i < 4; ++i) s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
    s.sim.set_player_score(0, 500);
    const uint32_t thief = s.sim.spawn_unit(1, sim::AntType::Thief, TileCoord{50, 10});
    s.start(345, style);
    const MapInfo map(s.sim);
    const HillInfo& hill = map.hill(0);
    int raids = 0;
    bool in_raid = false;
    bool decided = false;
    bool shut = false;
    for (uint64_t t = 0; t < 6000 && !decided; ++t) {
        s.step([&](uint64_t now) {
            const sim::AntSnapshot* th = snapshot_of(s.sim, thief);
            if (th == nullptr || now < kWarmUp) return;
            const bool raiding = th->state == sim::UnitState::Infiltrating;
            if (raiding && !in_raid) ++raids;
            in_raid = raiding;
            const bool free_thief = alive(s.sim, thief) && !th->is_holding && (th->state == sim::UnitState::Idle || th->state == sim::UnitState::GuardIdle);
            if (free_thief && raids >= 3) {                                                          // the fourth raid is about to be ordered: is the hole shut, or was the Thief struck?
                shut = east_state(s.sim.grid(), hill).shut();
                decided = true;
                return;
            }
            if (free_thief) s.sim.apply_command(command_of(CommandType::GroupSpecial, 1, {thief}, hill.entrance.x, hill.entrance.y));
        });
    }
    if (!decided) shut = east_state(s.sim.grid(), hill).shut();
    size_t struck = 0;
    for (const watch::Blow& b : s.seen.people().blows()) struck += b.victim == thief && b.striker_seat == 0 ? 1u : 0u;
    Verdict v;
    v.pass = shut || struck > 0;
    v.note = styled(style, "raids " + std::to_string(raids) + ", hole " + (shut ? "shut" : "open") + ", blows on the Thief " + std::to_string(struck));
    return v;
}

// Runs a scene for every style that a Hard bot plays: the rule holds only when it holds for both
Verdict both_styles(const std::function<Verdict(Style)>& scene) {
    Verdict all;
    for (const Style style : {Style::Aggressive, Style::Raider}) {
        const Verdict one = scene(style);
        all.pass = all.pass && one.pass;
        all.note += (all.note.empty() ? "" : "; ") + one.note;
    }
    return all;
}

// A case that the shipped bot fails today and a later step has to fix: green while it fails (the measure is printed), red the day it passes. `required`: the step has made it a rule.
void settle(const char* id, int step, bool required, const Verdict& v) {
    if (required) {
        ASSERT_TRUE(v.pass);
        return;
    }
    if (v.pass) {
        std::cout << "NOW PASSES " << id << " (Step " << step << "): make it required (" << v.note << ")\n";
        ASSERT_TRUE(!v.pass);
        return;
    }
    ASSERT_TRUE(!v.pass);
    std::cout << "KNOWN FAIL " << id << " (Step " << step << "): " << v.note << "\n";
}

// A match on TREASURE of the standard bot at Hard (seat 0) against the bench gang at Hard (seat 1): with or without the observer of the people numbers
ArenaResult play_gang_match(watch::PeopleObserver* observer, uint64_t ticks, uint32_t seed = 3) {
    ArenaSpec spec;
    spec.level = &level_of("TREASURE");
    spec.seed = seed;
    spec.max_ticks = ticks;
    BotSpec hard;
    hard.seat = 0;
    hard.kind = "standard";
    hard.level = Level::Hard;
    BotSpec gang;
    gang.seat = 1;
    gang.kind = "gang";
    gang.level = Level::Hard;
    spec.bots = {hard, gang};
    spec.extra_kinds = {"gang"};
    spec.factory = [](const BotSpec& b) -> std::unique_ptr<Bot> {
        std::unique_ptr<Bot> bench = bench::make_bench_bot(b.kind, b.level);
        return bench != nullptr ? std::move(bench) : make_bot(b);
    };
    if (observer != nullptr) spec.on_tick = [observer](const sim::SimulationEngine& e) { observer->tick(e); };
    return play_match(spec);
}

}  // namespace

void run_people_tests() {
    TEST_CASE("AI34.6 Blows And Fights: A Hit Ant With An Enemy In Its Strike Clip Within Two Tiles Was Struck By The Nearest One; Blows Of Two Sides Less Than 300 Ticks And 10 Tiles Apart Are One Fight; Hitting Back, Helping, The Crowd At The Fight, Ants Lost And Killed, And The Focus On A Victim Are Counted For The Seat That Was Struck First") {
        watch::People people(two_seats());
        const AntSnap a0 = ant(1, 0, 20, 20), a1 = ant(2, 0, 23, 20), e1 = ant(11, 1, 21, 21), e2 = ant(12, 1, 23, 23);
        people.tick(snap(100, {a0, a1, e1, e2}));
        people.tick(snap(110, {a0, a1, ant(11, 1, 21, 21, 10, kHit), e2}));
        people.tick(snap(111, {ant(1, 0, 20, 20, 9), a1, ant(11, 1, 21, 21, 10, kHit), e2}));                    // blow 1: e1 strikes a0 (a fight begins, struck first: seat 0)
        people.tick(snap(140, {ant(1, 0, 20, 20, 8), a1, ant(11, 1, 21, 21, 10, kHit), e2}));                    // blow 2
        people.tick(snap(150, {ant(1, 0, 20, 20, 8), ant(2, 0, 23, 20, 10, kHit), ant(11, 1, 21, 21, 9), e2}));  // blow 3: a1, an ant other than the first victim, strikes e1, two tiles away
        people.tick(snap(160, {ant(1, 0, 20, 20, 8), a1, ant(11, 1, 21, 21, 9), e2}));                           // (a sample of the crowd: 2 of seat 0 and 2 of seat 1 within 4 tiles of the last blow)
        people.tick(snap(170, {ant(1, 0, 20, 20, 7), a1, ant(11, 1, 21, 21, 9), ant(12, 1, 23, 23, 10, kHit)}));  // a0 loses a hit point, the only ant in its strike clip is three tiles away: no blow
        people.tick(snap(400, {ant(1, 0, 20, 20, 6), a1, ant(11, 1, 21, 21, 9, kHit), e2}));                     // blow 4: 250 ticks after the last one, the same place: the same fight
        people.tick(snap(1000, {ant(1, 0, 20, 20, 6), ant(2, 0, 23, 20, 9), ant(11, 1, 21, 21, 9, kHit), e2}));   // blow 5: 600 ticks later: a new fight, a1 is struck first
        people.tick(snap(1030, {ant(1, 0, 20, 20, 6), ant(2, 0, 23, 20, 0), ant(11, 1, 21, 21, 9, kHit), e2}));   // blow 6 kills a1
        people.tick(snap(1070, {ant(1, 0, 40, 40, 6), ant(11, 1, 21, 21, 9), ant(12, 1, 41, 41, 10, kHit), ant(13, 1, 43, 43)}));
        people.tick(snap(1080, {ant(1, 0, 40, 40, 5), ant(11, 1, 21, 21, 9), ant(12, 1, 41, 41, 10, kHit), ant(13, 1, 43, 43)}));   // blow 7: 50 ticks later but 20 tiles away: a third fight
        people.tick(snap(1100, {ant(1, 0, 40, 40, 5, kHit), ant(11, 1, 21, 21, 9), ant(12, 1, 41, 41, 9), ant(13, 1, 43, 43)}));     // blow 8: the first victim hits back itself
        ASSERT_EQ(people.blows().size(), 8u);
        ASSERT_EQ(people.blows()[0].striker, 11u);
        ASSERT_EQ(people.blows()[0].victim, 1u);
        ASSERT_TRUE(people.blows()[5].killed);
        const SeatPeople s0 = people.seat(0);
        const SeatPeople s1 = people.seat(1);
        ASSERT_EQ(s0.struck_first, 3u);
        ASSERT_EQ(s0.hit_back, 2u);
        ASSERT_EQ(s0.helped, 1u);
        ASSERT_EQ(s0.help_ticks.size(), 1u);
        ASSERT_EQ(s0.help_ticks[0], 39u);                                                                         // from the first blow (111) to the helper's (150)
        ASSERT_EQ(s0.strikers_own, 2u);                                                                           // a1 in the first fight, a0 in the third
        ASSERT_EQ(s0.strikers_enemy, 3u);
        ASSERT_EQ(s0.lost_in_fights, 1u);
        ASSERT_EQ(s0.killed_in_fights, 0u);
        ASSERT_EQ(s0.local_own, 2u + 2u + 1u);                                                                    // the most ants within 4 tiles of the last blow, sampled at ticks that 20 divides: fights 1, 2 and 3
        ASSERT_EQ(s0.local_enemy, 2u + 2u + 2u);
        ASSERT_EQ(s0.blows_struck, 2u);
        ASSERT_EQ(s1.struck_first, 0u);
        ASSERT_EQ(s1.blows_struck, 6u);
        ASSERT_EQ(s1.focus_pairs, 3u);                                                                            // blows 1-2, 5-6 (the same victim) and 6-7 (not): the others are 200 ticks or more apart
        ASSERT_EQ(s1.focus_same, 2u);
        ASSERT_EQ(s0.focus_pairs, 0u);
        const std::string json = s0.json();
        ASSERT_TRUE(json.find("\"struck_first\":3") != std::string::npos);
        ASSERT_TRUE(json.find("\"help_ticks_median\":39") != std::string::npos);
        ASSERT_TRUE(json.find("\"focus_pct\":null") != std::string::npos);
        ASSERT_TRUE(s1.json().find("\"focus_pct\":66.67") != std::string::npos);
        // the nearest ant in its strike clip struck, and of two that are equally near the one with the lower number
        {
            watch::People near(two_seats());
            near.tick(snap(10, {ant(1, 0, 10, 10), ant(14, 1, 11, 10), ant(15, 1, 12, 10), ant(16, 1, 9, 10)}));
            near.tick(snap(11, {ant(1, 0, 10, 10, 9), ant(14, 1, 11, 10, 10, kHit), ant(15, 1, 12, 10, 10, kHit), ant(16, 1, 9, 10, 10, kHit)}));
            ASSERT_EQ(near.blows().size(), 1u);
            ASSERT_EQ(near.blows()[0].striker, 14u);
            near.tick(snap(12, {ant(1, 0, 10, 10, 8), ant(14, 1, 11, 10, 10, kIdle), ant(15, 1, 12, 10, 10, kHit), ant(16, 1, 9, 10, 10, kHit)}));
            ASSERT_EQ(near.blows().size(), 2u);
            ASSERT_EQ(near.blows()[1].striker, 16u);                                                              // (one tile away beats two)
            near.tick(snap(13, {ant(1, 0, 10, 10, 7), ant(14, 1, 11, 10, 10, kIdle), ant(15, 1, 12, 10, 10, kHit), ant(16, 1, 9, 10, 10, kIdle)}));
            ASSERT_EQ(near.blows()[2].striker, 15u);
            near.tick(snap(14, {ant(1, 0, 10, 10, 7), ant(14, 1, 11, 10, 10, kHit), ant(15, 1, 12, 10, 10, kHit), ant(16, 1, 9, 10, 10, kHit)}));          // (a hit point that did not fall is no blow)
            ASSERT_EQ(near.blows().size(), 3u);
        }
        // allies are one side: a blow of an ally's ant is no fight with the ally, and the fight is the side's
        {
            watch::Setup three = two_seats();
            three.present[2] = true;
            three.entrance[2] = TileCoord{5, 50};
            watch::People team(three);
            Snapshot base = snap(10, {ant(1, 0, 10, 10), ant(2, 1, 11, 10), ant(21, 2, 12, 11)});
            base.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
            team.tick(base);
            Snapshot hit = snap(11, {ant(1, 0, 10, 10, 9), ant(2, 1, 11, 10, 10, kHit), ant(21, 2, 12, 11, 10, kHit)});
            hit.ally = base.ally;
            team.tick(hit);
            ASSERT_EQ(team.blows().size(), 1u);
            ASSERT_EQ(team.blows()[0].striker, 21u);                                                              // the ally (seat 1) is never the striker
            Snapshot help = snap(12, {ant(1, 0, 10, 10, 9), ant(2, 1, 11, 10, 10, kHit), ant(21, 2, 12, 11, 9)});
            help.ally = base.ally;
            team.tick(help);
            ASSERT_EQ(team.blows().size(), 2u);
            ASSERT_EQ(team.blows()[1].victim, 21u);
            ASSERT_EQ(team.seat(0).struck_first, 1u);
            ASSERT_EQ(team.seat(0).helped, 1u);                                                                   // the ally's ant struck back, and it is not the first victim
            ASSERT_EQ(team.seat(1).struck_first, 0u);
        }
    } TEST_END();

    TEST_CASE("AI34.7 Heals And Fire Ants: Every Rise Of An Own Ant's Hit Points Is A Heal (An Ant That Is First Seen Is Not), An Ant That Becomes A Fire Ant And One That Dies As One Are Counted, And Walls That Are Lit Within Four Tiles Of An Enemy Hill's Entrance, Once Each") {
        watch::People people(two_seats());
        const sim::AntType fire = sim::AntType::Fire;
        people.tick(snap(100, {ant(1, 0, 20, 20, 4), ant(2, 1, 30, 30, 5), ant(7, 0, 22, 20), ant(8, 0, 24, 20, 10, kIdle, fire)}));
        people.tick(snap(120, {ant(1, 0, 20, 20, 10), ant(2, 1, 30, 30, 8), ant(7, 0, 22, 20, 10, kIdle, fire), ant(8, 0, 24, 20, 10, kIdle, fire), ant(9, 0, 26, 20)}));    // two heals (and a new ant that is not one); a Fire Ant is made
        people.tick(snap(140, {ant(1, 0, 20, 20, 6), ant(2, 1, 30, 30, 8), ant(7, 0, 22, 20, 10, kIdle, fire), ant(8, 0, 24, 20, 10, kIdle, fire), ant(9, 0, 26, 20)}));     // a fall is no heal
        people.tick(snap(160, {ant(1, 0, 20, 20, 10), ant(2, 1, 30, 30, 8), ant(7, 0, 22, 20, 10, kIdle, fire), ant(8, 0, 24, 20, 10, kIdle, fire), ant(9, 0, 26, 20)}));    // the third heal
        const SeatPeople mid = people.seat(0);
        ASSERT_EQ(mid.heals, 2u);
        ASSERT_EQ(people.seat(1).heals, 1u);
        ASSERT_EQ(mid.fire_ants, 1u);                                                                             // ant 7 became one; ant 8 was one when it was first seen
        // walls: (49, 6) and (46, 5) are within 4 tiles of the entrance (50, 5), (45, 5) is not, (6, 6) is at the own hill, a wall of nobody's does not count
        const std::vector<PlaceSnap> walls = {place(49, 6, 0), place(46, 5, 0), place(45, 5, 0), place(6, 6, 0), place(49, 4, 255), place(51, 5, 1)};
        people.tick(snap(300, {ant(1, 0, 20, 20), ant(7, 0, 22, 20, 10, kIdle, fire)}, {}, walls));
        people.tick(snap(320, {ant(1, 0, 20, 20), ant(7, 0, 22, 20, 10, kIdle, fire)}, {}, walls));                // (the same walls again: not lit again)
        std::vector<PlaceSnap> fewer = walls;
        fewer.erase(fewer.begin());                                                                               // (49, 6) burns out ...
        people.tick(snap(340, {ant(1, 0, 20, 20), ant(7, 0, 22, 20, 10, kIdle, fire)}, {}, fewer));
        people.tick(snap(360, {ant(1, 0, 20, 20), ant(7, 0, 22, 20, 10, kIdle, fire)}, {}, walls));                // ... and is lit again: a wall again
        people.tick(snap(500, {ant(1, 0, 20, 20), ant(7, 0, 22, 20, 0, sim::UnitState::Dead, fire), ant(8, 0, 24, 20, 0, sim::UnitState::Dead, fire)}, {}, walls));
        const SeatPeople s0 = people.seat(0);
        ASSERT_EQ(s0.walls_at_enemy_gates, 3u);
        ASSERT_EQ(s0.fire_ants_died, 1u);                                                                         // ant 7 became one and died; ant 8 was born one
        ASSERT_EQ(s0.deaths, 2u);
        ASSERT_TRUE(s0.json().find("\"fire_ants_lost_per_wall\":0.33") != std::string::npos);
        ASSERT_EQ(people.seat(1).walls_at_enemy_gates, 0u);                                                       // (the wall of seat 1 stands at its own hill)
    } TEST_END();

    TEST_CASE("AI34.8 Deaths That Could Have Walked Home: From The First Look At 4 Hit Points Or Fewer (At Most 1,200 Ticks Back, And Not Before A Heal) The Distance To The Hill's Entrance Divided By The Speed Of The Match's Walkers Against The Ticks The Ant Still Lived; Without A Warning Is Counted Apart; A Closed Hill (187) Counts None Of Them") {
        const auto play = [](bool closed) {
            auto people = std::make_unique<watch::People>(two_seats(closed));
            std::vector<AntSnap> field;
            // the walkers of the match: one ant that goes 2 tiles every 20 ticks (0.1 tiles a tick)
            for (uint64_t t = 20; t <= 140; t += 20) {
                std::vector<AntSnap> ants = {ant(50, 1, static_cast<int32_t>(20 + t / 10), 30, 10, sim::UnitState::Walking)};
                if (t == 100) {
                    ants.push_back(ant(1, 0, 15, 5, 4));                                                          // (a) 10 tiles from the entrance, hurt at 100, dies at 300: 100 ticks to walk, 200 to live
                    ants.push_back(ant(2, 0, 45, 5, 3));                                                          // (b) 40 tiles: 400 ticks to walk
                    ants.push_back(ant(4, 0, 7, 5, 4));                                                           // (d) hurt, then healed
                    ants.push_back(ant(5, 0, 155, 5, 4));                                                         // (e) 150 tiles: 1,500 ticks to walk, and it lived 4,900
                    ants.push_back(ant(3, 0, 20, 5, 10));
                } else if (t > 100) {
                    ants.push_back(ant(1, 0, 15, 5, 4));
                    ants.push_back(ant(2, 0, 45, 5, 3));
                    ants.push_back(ant(4, 0, 7, 5, t >= 120 ? 10 : 4));
                    ants.push_back(ant(5, 0, 155, 5, 4));
                    ants.push_back(ant(3, 0, 20, 5, 10));
                }
                people->tick(snap(t, ants));
            }
            const auto rest = [&](uint64_t t, const std::vector<AntSnap>& ants) { people->tick(snap(t, ants)); };
            rest(300, {ant(1, 0, 15, 5, 0, sim::UnitState::Dead), ant(2, 0, 45, 5, 0, sim::UnitState::Dead), ant(4, 0, 7, 5, 10), ant(5, 0, 155, 5, 4), ant(3, 0, 20, 5, 10)});   // (a) and (b) die
            rest(400, {ant(4, 0, 7, 5, 10), ant(5, 0, 155, 5, 4), ant(3, 0, 20, 5, 0, sim::UnitState::Dead)});                                                          // (c) dies at full health: no warning
            rest(600, {ant(4, 0, 7, 5, 10), ant(5, 0, 155, 5, 4), ant(6, 0, 30, 5, 10)});
            rest(620, {ant(4, 0, 7, 5, 10), ant(5, 0, 155, 5, 4), ant(6, 0, 30, 5, 10, sim::UnitState::Drowning)});                                                       // (f) drowns at full health
            rest(2000, {ant(4, 0, 45, 5, 2), ant(5, 0, 155, 5, 4)});                                                                                                      // (d) hurt again, 40 tiles away
            rest(2100, {ant(4, 0, 45, 5, 0, sim::UnitState::Dead), ant(5, 0, 155, 5, 4)});                                                                                // (d) dies: 100 ticks to live
            rest(5000, {ant(5, 0, 155, 5, 0, sim::UnitState::Dead)});                                                                                                     // (e) dies: the last 1,200 ticks count, not 4,900
            return people;
        };
        const auto open = play(false);
        ASSERT_TRUE(open->walking_speed() > 0.0999 && open->walking_speed() < 0.1001);
        const SeatPeople s = open->seat(0);
        ASSERT_EQ(s.deaths, 6u);
        ASSERT_EQ(s.could_walk_home, 1u);                                                                         // (a)
        ASSERT_EQ(s.died_far, 3u);                                                                                // (b), (d) and (e)
        ASSERT_EQ(s.died_without_warning, 2u);                                                                    // (c) and (f)
        ASSERT_TRUE(s.walk_applicable);
        ASSERT_TRUE(s.json().find("\"could_walk_home\":1,\"died_far\":3,\"died_without_warning\":2") != std::string::npos);
        const auto shut = play(true);
        const SeatPeople c = shut->seat(0);
        ASSERT_FALSE(c.walk_applicable);                                                                          // the hill is closed: not applicable
        ASSERT_EQ(c.deaths, 6u);
        ASSERT_EQ(c.died_without_warning, 2u);
        ASSERT_EQ(c.could_walk_home, 0u);
        ASSERT_TRUE(c.json().find("\"could_walk_home\":null,\"died_far\":null,\"died_without_warning\":2") != std::string::npos);
        ASSERT_TRUE(c.line().find("hill closed") != std::string::npos);
        // an ant that never walked and a stream with no walkers: nothing could have walked home
        watch::People still(two_seats());
        still.tick(snap(100, {ant(1, 0, 6, 5, 3)}));
        still.tick(snap(200, {ant(1, 0, 6, 5, 0, sim::UnitState::Dead)}));
        ASSERT_EQ(still.seat(0).could_walk_home, 0u);
        ASSERT_EQ(still.seat(0).died_far, 1u);
    } TEST_END();

    TEST_CASE("AI34.9 Mines: A Seat's Bombs That Appear Within 10 Tiles Of Its Own Hill's Entrance Are Home Mines By Tick 3,000, 6,000 And 12,000, The Others Away Mines; An Enemy Ant That Loses Hit Points Within A Tile Of A Bomb That Is Gone In The Same Tick Or The Next Two Is A Hit") {
        watch::People people(two_seats());
        std::vector<PlaceSnap> bombs;
        const auto at = [&](uint64_t tick, const std::vector<AntSnap>& ants) { people.tick(snap(tick, ants, bombs)); };
        at(10, {});
        bombs.push_back(place(8, 8, 0));
        at(1000, {});                                                                                             // 3 tiles from the entrance (5, 5): a home mine
        bombs.push_back(place(14, 5, 0));
        at(2500, {});                                                                                             // 9 tiles
        bombs.push_back(place(15, 5, 0));
        at(2900, {});                                                                                             // 10 tiles: still home
        bombs.push_back(place(16, 5, 0));
        at(3500, {});                                                                                             // 11 tiles: away
        bombs.push_back(place(9, 9, 0));
        at(5000, {});                                                                                             // home, after tick 3,000
        bombs.push_back(place(6, 6, 0));
        at(7000, {});                                                                                             // home, after tick 6,000
        bombs.push_back(place(7, 7, 0));
        at(13000, {});                                                                                            // home, after tick 12,000: no count by any of the ticks
        bombs.push_back(place(7, 8, 255));
        bombs.push_back(place(49, 5, 0));
        at(13100, {});                                                                                            // a bomb of nobody's does not count; (49, 5) is at the enemy's hill: away
        bombs.push_back(place(8, 3, 1));
        at(13200, {});                                                                                            // seat 1's bomb is 42 tiles from its own hill's entrance: away, for seat 1
        const SeatPeople s0 = people.seat(0);
        ASSERT_EQ(s0.home_mines[0], 3u);
        ASSERT_EQ(s0.home_mines[1], 4u);
        ASSERT_EQ(s0.home_mines[2], 5u);
        ASSERT_EQ(s0.away_mines, 2u);
        ASSERT_EQ(people.seat(1).away_mines, 1u);
        ASSERT_EQ(people.seat(1).home_mines[2], 0u);
        ASSERT_TRUE(s0.json().find("\"home_mines_3000\":3,\"home_mines_6000\":4,\"home_mines_12000\":5,\"away_mines\":2") != std::string::npos);
        // the hits: a bomb of seat 0 goes off next to an enemy ant, and next to ants that do not count
        watch::People hits(two_seats());
        bombs = {place(30, 30, 0), place(40, 40, 0), place(20, 20, 0), place(10, 40, 0), place(30, 10, 0)};
        const auto ants = [&](unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
            return std::vector<AntSnap>{ant(21, 1, 31, 30, a), ant(22, 1, 41, 41, b), ant(23, 1, 22, 20, c), ant(24, 0, 11, 40, d), ant(25, 1, 31, 11, e)};
        };
        hits.tick(snap(4000, ants(10, 10, 10, 10, 10), bombs));
        bombs.erase(std::remove_if(bombs.begin(), bombs.end(), [](const PlaceSnap& p) { return p.tile.x == 30 && p.tile.y == 30; }), bombs.end());
        hits.tick(snap(4400, ants(10, 10, 10, 10, 10), bombs));                                                   // the first bomb is gone
        hits.tick(snap(4401, ants(8, 10, 10, 10, 10), bombs));                                                    // the enemy ant next to it loses hit points: a hit
        bombs.erase(bombs.begin(), bombs.begin() + 1);                                                            // (40, 40) is gone
        hits.tick(snap(4500, ants(8, 10, 10, 10, 10), bombs));
        hits.tick(snap(4503, ants(8, 8, 10, 10, 10), bombs));                                                     // three ticks later: no hit
        bombs.erase(bombs.begin(), bombs.begin() + 1);                                                            // (20, 20) is gone
        hits.tick(snap(4600, ants(8, 8, 10, 10, 10), bombs));
        hits.tick(snap(4601, ants(8, 8, 7, 10, 10), bombs));                                                      // the enemy ant is two tiles from it: no hit
        bombs.erase(bombs.begin(), bombs.begin() + 1);                                                            // (10, 40) is gone
        hits.tick(snap(4700, ants(8, 8, 7, 10, 10), bombs));
        hits.tick(snap(4701, ants(8, 8, 7, 6, 10), bombs));                                                       // the ant next to it is the owner's own: no hit
        bombs.clear();                                                                                            // (30, 10) is gone
        hits.tick(snap(4800, ants(8, 8, 7, 6, 10), bombs));
        hits.tick(snap(4802, ants(8, 8, 7, 6, 9), bombs));                                                        // two ticks later, an enemy ant a tile away: a hit
        ASSERT_EQ(hits.seat(0).mine_hits_enemy, 2u);
        ASSERT_EQ(hits.seat(1).mine_hits_enemy, 0u);
    } TEST_END();

    TEST_CASE("AI34.1 A Gang Of Three Hits One Of Four Ants That Are Far From Home (Two Thieves, A Fire Ant, A Worker): Within 600 Ticks The Hard Bot Strikes At Least Six Blows And Loses At Most One Ant") {
        settle("AI34.1", 1, false, both_styles([](Style st) { return scene_gang_on_four(st); }));
    } TEST_END();

    TEST_CASE("AI34.2 An Enemy Fire Ant Camps Four Tiles From The Hill Of A Hard Bot With Two Ants And Strikes Whatever Comes Within Three Tiles: Within 1,200 Ticks No Ant Of The Bot Dies Alone To It, Or Two Of Them Strike It") {
        settle("AI34.2", 3, false, both_styles([](Style st) { return scene_camper(st); }));
    } TEST_END();

    TEST_CASE("AI34.3 The Only Ant Of A Hard Bot Is Sent For A Fire Power-Up 10 Tiles Away, An Enemy Combat Ant Stands Two Tiles From It: The Ant Does Not Stand Next To The Combat Ant Without A Second Ant Of The Bot Within Two Tiles") {
        settle("AI34.3", 3, false, both_styles([](Style st) {
            Verdict a = scene_lone_ant(st, true);
            const Verdict b = scene_lone_ant(st, false);
            a.pass = a.pass && b.pass;
            a.note += "; " + b.note;
            return a;
        }));
    } TEST_END();

    TEST_CASE("AI34.4 An Enemy Worker Strikes The Empty Bomber Of A Hard Bot (6 Hit Points) Again After Every Blow: The Bomber Strikes Back Within 40 Ticks Of The First Blow Or Walks Towards Its Hill") {
        settle("AI34.4", 1, false, both_styles([](Style st) { return scene_hurt_bomber(st); }));
    } TEST_END();

    TEST_CASE("AI34.5 An Enemy Thief Raids The Hill Of A Hard Bot With No Fire Ant Three Times In About 2,400 Ticks: Before The Fourth Raid The Three Tiles East Of The Raid Tile Are Shut, Or The Thief Has Been Struck") {
        settle("AI34.5", 5, false, both_styles([](Style st) { return scene_thief_hole(st); }));
    } TEST_END();

    TEST_CASE("AI34.10 The Observer Of The People Numbers Reads The Engine And Changes Nothing: A Match Of 6,000 Ticks With It Ends On The Same State Hash, And At Every 20th Tick The Same One, As Without It, And It Saw The Fights") {
        const ArenaResult plain = play_gang_match(nullptr, 6000);
        watch::PeopleObserver seen;
        const ArenaResult watched = play_gang_match(&seen, 6000);
        ASSERT_TRUE(plain.error.empty() && watched.error.empty());
        ASSERT_EQ(plain.ticks, 6000u);
        ASSERT_EQ(plain.hash, watched.hash);
        ASSERT_TRUE(plain.checkpoints == watched.checkpoints);
        ASSERT_TRUE(seen.started());
        seen.finish();
        const SeatPeople hard = seen.people().seat(0);
        const SeatPeople gang = seen.people().seat(1);
        ASSERT_TRUE(hard.blows_struck + gang.blows_struck > 0);                                                  // (a quiet match would prove nothing)
        ASSERT_TRUE(hard.struck_first + gang.struck_first > 0);
        ASSERT_TRUE(seen.people().walking_speed() > 0.05 && seen.people().walking_speed() < 0.13);               // (about 0.08 tiles a tick on this map, 0.125 at the most: a tile every 8 ticks)
    } TEST_END();

    TEST_CASE("AI34.11 The Bench Bot gang Plays Only What A Person Could Click (No Order Filtered Or Rejected), Is Not In The Registry, Harvests, Raids And Strikes, And Plays The Same Match Twice Alike") {
        ASSERT_FALSE(known_bot_kind("gang"));
        BotSpec spec;
        spec.kind = "gang";
        ASSERT_TRUE(make_bot(spec) == nullptr);
        ASSERT_EQ(bench::bench_kind_of("GANG"), std::string("gang"));
        ASSERT_EQ(bench::bench_kind_of("Aggr7"), std::string("aggr7"));
        ASSERT_EQ(bench::bench_kind_of("standard"), std::string());
        ASSERT_EQ(bench::bench_kind_of("aggr10"), std::string());
        ASSERT_TRUE(bench::make_bench_bot("worker", Level::Hard) == nullptr);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_EQ(std::string(bench::make_bench_bot("gang", level)->kind()), std::string("gang"));
        watch::PeopleObserver seen;
        const ArenaResult first = play_gang_match(&seen, 6000);
        ASSERT_TRUE(first.error.empty());
        ASSERT_EQ(first.seats[1].runs, std::string("gang"));
        ASSERT_EQ(first.seats[1].stats.filtered, 0u);
        ASSERT_EQ(first.seats[1].stats.rejected, 0u);
        ASSERT_TRUE(first.seats[1].banked > 0);                                                                  // it harvests
        ASSERT_TRUE(first.seats[1].attack_orders > 0);                                                           // it strikes
        ASSERT_TRUE(seen.people().seat(1).blows_struck > 0);
        const ArenaResult second = play_gang_match(nullptr, 6000);
        ASSERT_EQ(first.hash, second.hash);
    } TEST_END();
}
