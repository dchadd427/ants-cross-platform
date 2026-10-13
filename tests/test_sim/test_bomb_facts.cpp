// Engine-fact probes of the bomb throw, the hit knockback, fire walls, the defuse and the power-up pick-up (docs/reverse_engineering/bomb_gates.md).
// They change no rule: each case drives a SimulationEngine on a synthetic open world (or on POPcOrN) the way the combat and ability tests do and pins what the simulation does,
// so that a planner (a bot that bombs itself over a fire wall) can rely on it. Tile directions: 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW (a step is {drow, dcol} = movement::dir_delta).
// Own small check() that counts and goes on (the shared macros of tests/common/test_assert.hpp return from the case at the first failure, which the loops here do not want).
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace ants::sim;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_cases = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures > 40) return;
        std::cout << "    FAILED: " << what << " (line " << line << ")\n";
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

void run_case(const std::string& name, const std::function<void()>& fn) {
    ++g_cases;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    const int before = g_failures;
    fn();
    if (g_failures == before) std::cout << "PASS\n";
}

// ------------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------------

constexpr uint8_t kNeutral = 255;   // the owner of a bomb that a map file holds

TileCoord step(TileCoord t, int dir, int range) {
    const movement::TileDelta d = movement::dir_delta(static_cast<uint8_t>(dir));
    return TileCoord{t.x + d.dcol * range, t.y + d.drow * range};
}
int opposite(int dir) { return (dir + 4) % 8; }

void world(SimulationEngine& sim, uint32_t seed) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
}
void run(SimulationEngine& sim, int ticks) {
    for (int i = 0; i < ticks; ++i) sim.tick();
}
void bomb(SimulationEngine& sim, TileCoord t, uint8_t team) { sim.grid_mut().place_bomb(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), team); }
void fire(SimulationEngine& sim, TileCoord t, uint8_t team = 0) { sim.grid_mut().place_firewall(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), team); }
void rock(SimulationEngine& sim, TileCoord t) { sim.set_terrain(t.x, t.y, TERRAIN_OBSTACLE); }

AntOrder order(uint32_t ant, OrderType type, TileCoord target) {
    AntOrder o{};
    o.ant_id = ant;
    o.type = type;
    o.target_x = target.x;
    o.target_y = target.y;
    return o;
}

// The bomb is a dud when its draw of the match generator is below 20 of 100 (bomb_trigger: prng_.rand() % 100 < 20). The generator is seeded with the match seed; every spawn_unit draws once
// first (the random facing), then each bomb that goes off draws once. dud_seed answers for the draw after `before` earlier draws; seed_for finds a seed whose next draws, after `before`
// earlier ones, are the pattern: 'o' a bomb that throws, 'd' a dud.
bool dud_seed(uint32_t seed, int before = 1) {
    PRNG p(seed);
    for (int i = 0; i < before; ++i) p.rand();
    return p.rand() % 100u < 20u;
}
uint32_t seed_for(const std::string& pattern, int before = 1) {
    for (uint32_t s = 1;; ++s) {
        PRNG p(s);
        for (int i = 0; i < before; ++i) p.rand();
        bool ok = true;
        for (const char c : pattern) ok = ok && ((p.rand() % 100u < 20u) == (c == 'd'));
        if (ok) return s;
    }
}

// A worker of team 1 on `at` facing `facing`, a bomb of team 0 under it, set off through the hook; the flight is read at once (the landing tile is fixed when the bomb goes off).
struct Throw {
    TileCoord to;
    bool burn_on_spot;   // a dud, or no free landing tile: the ant does not fly, it burns where it stands
    uint16_t hp;
    int facing_after;
    bool frozen;
    bool bomb_left;
};
Throw throw_at(SimulationEngine& sim, uint32_t id, TileCoord at, int facing, uint8_t bomb_team = 0) {
    bomb(sim, at, bomb_team);
    sim.get_unit(id).facing = static_cast<Direction>(facing);
    sim.trigger_bomb_detonation(id, at);
    const AntUnit& a = sim.get_unit(id);
    return Throw{a.flight_tile, a.knock_flag, a.hp, static_cast<int>(a.facing), a.frozen, sim.has_bomb_at(at)};
}

// One throw in a fresh open world (a seed whose first draw is no dud), the ant at (30, 30) with the given facing.
Throw open_throw(int facing, const std::function<void(SimulationEngine&)>& setup = nullptr, AntType type = AntType::Worker) {
    int spawned = 0;
    for (int pass = 0; pass < 2; ++pass) {   // (the first pass counts the draws of the spawns, the second one throws with a seed whose bomb draw is no dud)
        SimulationEngine sim;
        world(sim, pass == 0 ? 1u : seed_for("o", spawned));
        if (setup) setup(sim);
        const uint32_t id = sim.spawn_unit(1, type, TileCoord{30, 30});
        if (pass == 0) {
            spawned = static_cast<int>(sim.get_world_state().ants.size());
            continue;
        }
        return throw_at(sim, id, TileCoord{30, 30}, facing);
    }
    return Throw{};
}

// Ticks until `done` or `limit` ticks; the number of ticks run.
int ticks_until(SimulationEngine& sim, int limit, const std::function<bool()>& done) {
    int t = 0;
    for (; t < limit && !done(); ++t) sim.tick();
    return t;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Bomb, knockback, fire wall, defuse and power-up facts\n"
              << "=======================================================\n";

    // --------------------------------------------------------------------------------------------
    // 1. The bomb throw
    // --------------------------------------------------------------------------------------------

    run_case("BG1.1 The Throw Starts In The Direction OPPOSITE The Ant's Facing And Flies Exactly 4 Tiles; The Ant Loses 2 Hit Points, The Bomb Is Gone, The Facing Is Kept", [] {
        for (int f = 0; f < 8; ++f) {
            const Throw t = open_throw(f);
            CHECK(!t.burn_on_spot);
            CHECK(t.to == step(TileCoord{30, 30}, opposite(f), 4));
            CHECK(t.hp == 8);
            CHECK(!t.bomb_left);
            CHECK(t.facing_after == f);      // (the flight clip faces the way the ant came from: facing again, which is why a chain flies on in the same direction)
        }
    });

    run_case("BG1.2 An Ant That WALKS Onto A Bomb Of Another Team Or A Neutral File Bomb, From Each Of The 8 Directions, Is Thrown 4 Tiles The Way It Came From", [] {
        for (const uint8_t owner : {uint8_t{0}, kNeutral}) {
            for (int f = 0; f < 8; ++f) {
                SimulationEngine sim;
                world(sim, seed_for("o"));
                const TileCoord b{30, 30};
                const TileCoord start = step(b, opposite(f), 3);          // 3 tiles behind the bomb: walking in direction f reaches it
                const uint32_t id = sim.spawn_unit(1, AntType::Worker, start);
                bomb(sim, b, owner);
                sim.issue_move_order(id, b);
                const int t = ticks_until(sim, 300, [&] { return !sim.has_bomb_at(b); });
                CHECK(t < 300);
                const AntUnit& a = sim.get_unit(id);
                CHECK(a.flight_tile == step(b, opposite(f), 4));
                CHECK(a.hp == 8);
            }
        }
    });

    run_case("BG1.3 No Free Tile In The Chosen Direction: The Directions Are Tried In Turn d, d+1, d+2 ... (Clockwise, All 8); With All 8 Landing Tiles Refused The Ant Does Not Fly: It Burns On The Spot (Frozen, -2 hp)", [] {
        const TileCoord at{30, 30};
        const int facing = 0;                                 // start direction 4 (S)
        for (int blocked = 0; blocked <= 8; ++blocked) {
            const Throw t = open_throw(facing, [&](SimulationEngine& sim) {
                for (int i = 0; i < blocked; ++i) rock(sim, step(at, (4 + i) % 8, 4));
            });
            if (blocked < 8) {
                CHECK(!t.burn_on_spot);
                CHECK(t.to == step(at, (4 + blocked) % 8, 4));
                CHECK(!t.frozen);
            } else {
                CHECK(t.burn_on_spot && t.to == at && t.frozen);
            }
            CHECK(t.hp == 8);
            CHECK(!t.bomb_left);
        }
    });

    run_case("BG1.4 Only The Landing Tile Counts: A Rock Or The Map Edge, A Hill (4x4) Or The Queue Tiles Of A Hill Refuse It; Water, An Ant, A Bomb Or A Fire Wall On It Do Not; Rocks On The Way Do Not", [] {
        const TileCoord at{30, 30};
        const TileCoord ahead = step(at, 4, 4);               // facing 0: landing (30, 34)
        const TileCoord next = step(at, 5, 4);                // the second try (26, 34)
        CHECK(open_throw(0).to == ahead);
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.set_terrain(ahead.x, ahead.y, TERRAIN_WATER); }).to == ahead);
        CHECK(open_throw(0, [&](SimulationEngine& s) { bomb(s, ahead, 1); }).to == ahead);
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.spawn_unit(0, AntType::Worker, ahead); }).to == ahead);
        CHECK(open_throw(0, [&](SimulationEngine& s) { fire(s, ahead); }).to == ahead);                        // (a fire wall is solid, yet counts as free ground)
        CHECK(open_throw(0, [&](SimulationEngine& s) { rock(s, step(at, 4, 1)); rock(s, step(at, 4, 2)); rock(s, step(at, 4, 3)); }).to == ahead);
        CHECK(open_throw(0, [&](SimulationEngine& s) { rock(s, ahead); }).to == next);
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.set_anthill(2, TileCoord{29, 33}); }).to == next);   // (the tile lies inside the 4x4 of the hill)
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.set_anthill(2, TileCoord{27, 33}); }).to == next);   // (its last column)
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.set_anthill(2, TileCoord{29, 35}); }).to == next);   // (its queue tiles are the row above it: (29..31, 34))
        CHECK(open_throw(0, [&](SimulationEngine& s) { s.set_anthill(2, TileCoord{26, 30}); }).to == ahead);  // (the tile next to a hill is free)
        // the map edge: facing south at (30, 2) throws north out of the map, then NE out of the map, then east: (34, 2)
        SimulationEngine sim;
        world(sim, seed_for("o"));
        const uint32_t id = sim.spawn_unit(1, AntType::Worker, TileCoord{30, 2});
        CHECK(throw_at(sim, id, TileCoord{30, 2}, 4).to == (TileCoord{34, 2}));
    });

    run_case("BG1.5 The Dud: One Draw Of The Match Generator Decides (Below 20 Of 100: Dud); A Dud Burns The Ant On The Spot (-2 hp, Frozen Under The Burn Overlay, Then Stunned) And The Bomb Is Gone; The Same Seed Gives The Same Result", [] {
        int duds = 0;
        for (uint32_t seed = 1; seed <= 1000; ++seed) {
            SimulationEngine sim;
            world(sim, seed);
            const uint32_t id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            const Throw t = throw_at(sim, id, TileCoord{20, 20}, 4);
            CHECK(t.burn_on_spot == dud_seed(seed, 1));
            CHECK(t.hp == 8 && !t.bomb_left);
            duds += t.burn_on_spot ? 1 : 0;
        }
        CHECK(duds == 201);                                   // (about one in five: the draws of seeds 1 .. 1000)
        SimulationEngine sim;
        world(sim, seed_for("d"));
        const uint32_t id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
        const Throw t = throw_at(sim, id, TileCoord{20, 20}, 4);
        CHECK(t.burn_on_spot && t.frozen && t.to == (TileCoord{20, 20}));
        run(sim, 400);
        CHECK(sim.get_unit(id).pos == (TileCoord{20, 20}) && !sim.get_unit(id).frozen && sim.get_unit(id).hp == 8);
        SimulationEngine again;
        world(again, seed_for("d"));
        const uint32_t id2 = again.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
        CHECK(throw_at(again, id2, TileCoord{20, 20}, 4).burn_on_spot);
    });

    run_case("BG1.6 A Thrown Ant That LANDS On A Bomb Sets It Off At Once And Is Thrown 4 More Tiles In The Same Direction (-2 hp Each Time); A Dud, A Landing Without A Bomb Or An Ant Without hp Ends The Chain", [] {
        // two bombs ahead of an ant that faces south (thrown north): (20, 20) -> (20, 16) -> (20, 12)
        SimulationEngine sim;
        world(sim, seed_for("ooo"));
        const uint32_t id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
        sim.get_unit(id).facing = Direction::South;
        bomb(sim, TileCoord{20, 20}, 0);
        bomb(sim, TileCoord{20, 16}, 1);                      // (the other team's bomb: a landing sets off any bomb)
        bomb(sim, TileCoord{20, 12}, kNeutral);
        sim.trigger_bomb_detonation(id, TileCoord{20, 20});
        run(sim, 40);
        CHECK(!sim.has_bomb_at(TileCoord{20, 16}));
        CHECK(sim.get_unit(id).pos.y <= 12);
        run(sim, 400);
        CHECK(!sim.has_bomb_at(TileCoord{20, 12}));
        CHECK(sim.get_unit(id).pos == (TileCoord{20, 8}));
        CHECK(sim.get_unit(id).hp == 4);                      // three bombs
        // a chain onto a dud: the first bomb throws, the second one (where the ant lands) is a dud, so the ant stays on the tile where it landed
        SimulationEngine s2;
        world(s2, seed_for("od"));
        const uint32_t i2 = s2.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
        s2.get_unit(i2).facing = Direction::South;
        bomb(s2, TileCoord{20, 20}, 0);
        bomb(s2, TileCoord{20, 16}, 0);
        s2.trigger_bomb_detonation(i2, TileCoord{20, 20});
        run(s2, 400);
        CHECK(s2.get_unit(i2).pos == (TileCoord{20, 16}) && s2.get_unit(i2).hp == 6 && !s2.has_bomb_at(TileCoord{20, 16}));
        // an ant with 2 hp dies of one bomb
        SimulationEngine s3;
        world(s3, seed_for("o"));
        const uint32_t i3 = s3.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
        s3.get_unit(i3).hp = 2;
        bomb(s3, TileCoord{20, 20}, 0);
        s3.trigger_bomb_detonation(i3, TileCoord{20, 20});
        CHECK(s3.get_unit(i3).hp == 0);
        run(s3, 400);
        CHECK(s3.get_unit(i3).removed);
    });

    run_case("BG1.7 Who Sets A Bomb Off: A Step Onto An Enemy Bomb Or A Neutral File Bomb; Own And Allied Bombs Are Routed Around (The Path Never Crosses Them) Unless The Ant Is Ordered ONTO The Bomb Tile; A Landing Sets Off Any Bomb", [] {
        // own bomb on the way: the ant walks round it
        for (int mode = 0; mode < 3; ++mode) {   // 0 own team, 1 allied team, 2 enemy team
            SimulationEngine sim;
            world(sim, seed_for("o"));
            if (mode == 1) {
                sim.propose_alliance(0, 2);
                sim.accept_alliance(2, 0);
            }
            const uint32_t id = sim.spawn_unit(mode == 0 ? 0 : (mode == 1 ? 2 : 1), AntType::Worker, TileCoord{20, 17});
            bomb(sim, TileCoord{20, 20}, 0);
            sim.issue_move_order(id, TileCoord{20, 23});
            ticks_until(sim, 400, [&] { return sim.get_unit(id).pos == (TileCoord{20, 23}) || !sim.has_bomb_at(TileCoord{20, 20}); });
            if (mode < 2) {
                CHECK(sim.has_bomb_at(TileCoord{20, 20}) && sim.get_unit(id).hp == 10);
            } else {
                CHECK(!sim.has_bomb_at(TileCoord{20, 20}) && sim.get_unit(id).hp == 8);
            }
        }
        // an order onto the own bomb tile sets it off
        SimulationEngine sim;
        world(sim, seed_for("o"));
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 17});
        bomb(sim, TileCoord{20, 20}, 0);
        sim.issue_move_order(id, TileCoord{20, 20});
        ticks_until(sim, 400, [&] { return !sim.has_bomb_at(TileCoord{20, 20}); });
        CHECK(!sim.has_bomb_at(TileCoord{20, 20}) && sim.get_unit(id).hp == 8 && sim.get_unit(id).flight_tile == (TileCoord{20, 16}));
        // the landing of a flight on the own team's bomb sets it off (BG1.6 used other teams' bombs)
        SimulationEngine s2;
        world(s2, seed_for("oo"));
        const uint32_t i2 = s2.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        s2.get_unit(i2).facing = Direction::South;
        bomb(s2, TileCoord{20, 20}, 1);
        bomb(s2, TileCoord{20, 16}, 0);
        s2.trigger_bomb_detonation(i2, TileCoord{20, 20});
        run(s2, 40);
        CHECK(!s2.has_bomb_at(TileCoord{20, 16}));
    });

    // --------------------------------------------------------------------------------------------
    // 2. The hit knockback
    // --------------------------------------------------------------------------------------------

    const auto knock = [](TileCoord attacker, TileCoord victim, AntType type, const std::function<void(SimulationEngine&)>& setup = nullptr) {
        SimulationEngine sim;
        world(sim, 3);
        if (setup) setup(sim);
        const uint32_t a = sim.spawn_unit(0, type, attacker);
        const uint32_t b = sim.spawn_unit(1, AntType::Worker, victim);
        sim.execute_melee_attack(a, b);
        run(sim, 60);
        return std::make_pair(sim.get_unit(b).pos, sim.get_unit(b).hp);
    };

    run_case("BG2.1 A Hit Throws The Victim 1 Tile (A Combat Ant 4 Tiles, Counted From The Victim's Tile) In The Direction From The Attacker Through The Victim; 1 hp (Combat: 2 hp)", [&] {
        const TileCoord v{30, 30};
        for (int d = 0; d < 8; ++d) {
            const TileCoord attacker = step(v, opposite(d), 1);
            const auto worker = knock(attacker, v, AntType::Worker);
            CHECK(worker.first == step(v, d, 1) && worker.second == 9);
            const auto combat = knock(attacker, v, AntType::Combat);
            CHECK(combat.first == step(v, d, 4) && combat.second == 8);
        }
    });

    run_case("BG2.2 A Blocked Landing Tries The Directions +1, -1, +2, -2 Of The Hit Direction (At The Same Range); With Those 5 Refused The Victim Does Not Fly: Hit Point Lost, Stunned In Place", [&] {
        const TileCoord v{30, 30}, attacker{30, 31};          // hit direction N
        const TileCoord landing[5] = {{30, 29}, {31, 29}, {29, 29}, {31, 30}, {29, 30}};   // N, NE, NW, E, W
        for (int blocked = 0; blocked <= 5; ++blocked) {
            const auto r = knock(attacker, v, AntType::Worker, [&](SimulationEngine& sim) {
                for (int i = 0; i < blocked; ++i) rock(sim, landing[i]);
            });
            CHECK(r.first == (blocked < 5 ? landing[blocked] : v));
            CHECK(r.second == 9);
        }
        // the south-west and south-east tiles are never tried: a victim cornered on all 5 stays although they are free
        const auto r = knock(attacker, v, AntType::Combat, [&](SimulationEngine& sim) {
            for (const int d : {0, 1, 7, 2, 6}) rock(sim, step(v, d, 4));
        });
        CHECK(r.first == v && r.second <= 8);                  // (the combat attacker's own reflex strikes the stunned victim again: 8 hp at the first hit, fewer later)
    });

    run_case("BG2.3 At A Map Edge The Landing Outside The Map Is Refused Like A Rock: A Worker Hit At Column 0 Goes North; A Combat Ant's Victim At Column 2 Goes North, 4 Tiles", [&] {
        CHECK(knock(TileCoord{1, 20}, TileCoord{0, 20}, AntType::Worker).first == (TileCoord{0, 19}));
        CHECK(knock(TileCoord{3, 20}, TileCoord{2, 20}, AntType::Combat).first == (TileCoord{2, 16}));
        CHECK(knock(TileCoord{20, 1}, TileCoord{20, 0}, AntType::Worker).first == (TileCoord{21, 0}));       // (the hit direction N: out; +1 NE: out; -1 NW: out; +2 E: free)
    });

    run_case("BG2.4 A Victim Knocked ONTO A Bomb Sets It Off (-2 More hp) And Is Thrown 4 Tiles On In The Direction Of The Knock; The Same For A Combat Ant's Victim", [&] {
        const auto worker = knock(TileCoord{30, 31}, TileCoord{30, 30}, AntType::Worker, [](SimulationEngine& s) { bomb(s, TileCoord{30, 29}, 1); });
        CHECK(worker.first == (TileCoord{30, 25}) && worker.second == 7);         // 1 (hit) + 2 (bomb)
        const auto combat = knock(TileCoord{30, 31}, TileCoord{30, 30}, AntType::Combat, [](SimulationEngine& s) { bomb(s, TileCoord{30, 26}, 0); });
        CHECK(combat.first == (TileCoord{30, 22}) && combat.second == 6);         // 2 (hit) + 2 (bomb)
    });

    // --------------------------------------------------------------------------------------------
    // 3. Fire walls
    // --------------------------------------------------------------------------------------------

    run_case("BG3.1 Only A Fire Ant Enters A Fire Wall Cell: Against A Wall Across The Whole Map A Worker And A Swimmer Cannot Cross (No Path), A Fire Ant Walks Through Unhurt", [] {
        for (int ty = 0; ty < 3; ++ty) {
            SimulationEngine sim;
            world(sim, 3);
            for (int x = 0; x < 60; ++x) fire(sim, TileCoord{x, 30});
            const AntType types[3] = {AntType::Worker, AntType::Swimmer, AntType::Fire};
            const uint32_t id = sim.spawn_unit(1, types[ty], TileCoord{30, 27});
            sim.issue_move_order(id, TileCoord{30, 33});
            ticks_until(sim, 400, [&] { return sim.get_unit(id).pos == (TileCoord{30, 33}); });
            CHECK((sim.get_unit(id).pos == (TileCoord{30, 33})) == (ty == 2));
            CHECK(sim.get_unit(id).hp == 10);
        }
    });

    run_case("BG3.2 Extinguish: Only A Fire Ant Does It (The Special Order On A Fire Wall); It Works From A Cardinal Neighbour Of The Wall (A Diagonal Start Walks To One) And Needs 26 Ticks From The North (1200 ms Clip), 28 From The Others", [] {
        const TileCoord w{30, 30};
        const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1}, dc[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        for (int n = 0; n < 8; ++n) {
            SimulationEngine sim;
            world(sim, 3);
            fire(sim, w, 1);
            const uint32_t id = sim.spawn_unit(0, AntType::Fire, TileCoord{w.x + dc[n], w.y + dr[n]});
            CHECK(sim.issue_order(order(id, OrderType::IgniteFire, w)));
            CHECK(sim.get_unit(id).orig_order == AntUnit::kOrderExtinguish);
            int t0 = -1;
            TileCoord at{};
            int t = 0;
            for (; t < 300; ++t) {
                if (t0 < 0 && sim.get_unit(id).loco_action == AntUnit::kActionExtinguish) { t0 = t; at = sim.get_unit(id).pos; }
                if (t0 >= 0 && !sim.has_fire_at(w)) break;
                sim.tick();
            }
            CHECK(t0 >= 0 && !sim.has_fire_at(w));
            CHECK(std::abs(at.x - w.x) + std::abs(at.y - w.y) == 1);                // a cardinal neighbour
            const bool from_north = (at == TileCoord{w.x, w.y - 1});
            CHECK(t - t0 == (from_north ? 26 : 28));
        }
        for (const AntType type : {AntType::Worker, AntType::Thief, AntType::Combat, AntType::Bomber, AntType::Swimmer}) {
            SimulationEngine sim;
            world(sim, 3);
            fire(sim, w, 1);
            const uint32_t id = sim.spawn_unit(0, type, TileCoord{30, 28});
            sim.issue_order(order(id, OrderType::IgniteFire, w));
            run(sim, 300);
            CHECK(sim.has_fire_at(w));                        // (a walk to the wall, a refusal: never an extinguish)
            CHECK(sim.get_unit(id).hp == 10);
        }
    });

    run_case("BG3.3 A Flight May Land ON A Fire Wall Cell: A Worker Loses 1 hp (After The Bomb's 2) And Is Thrown 1 Tile To A Neighbour Of The Wall, Which Keeps Burning; A Fire Ant Stays On The Cell, Stunned, Unhurt", [] {
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            if (dud_seed(seed, 1)) continue;
            for (int ty = 0; ty < 2; ++ty) {
                SimulationEngine sim;
                world(sim, seed);
                const TileCoord w{30, 26};
                fire(sim, w);
                const uint32_t id = sim.spawn_unit(1, ty ? AntType::Fire : AntType::Worker, TileCoord{30, 30});
                const Throw t = throw_at(sim, id, TileCoord{30, 30}, 4);
                CHECK(t.to == w);
                run(sim, 100);
                const AntUnit& a = sim.get_unit(id);
                CHECK(sim.has_fire_at(w));
                if (ty == 0) {
                    CHECK(a.hp == 7);
                    CHECK(a.pos != w && std::abs(a.pos.x - w.x) <= 1 && std::abs(a.pos.y - w.y) <= 1);
                } else {
                    CHECK(a.hp == 8 && a.pos == w);
                }
            }
        }
    });

    // --------------------------------------------------------------------------------------------
    // 4. Defuse
    // --------------------------------------------------------------------------------------------

    run_case("BG4.1 Defuse: Only A Bomber (The Special Order On A Bomb Tile); It Works From A Cardinal Neighbour (A Diagonal Start Walks To One), Takes 24 Ticks From Start To Bomb Gone, Costs Nothing; A Bomb Of Any Owner", [] {
        const TileCoord b{30, 30};
        const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1}, dc[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        for (const uint8_t owner : {uint8_t{0}, uint8_t{1}, kNeutral}) {
            for (int n = 0; n < 8; ++n) {
                SimulationEngine sim;
                world(sim, 3);
                bomb(sim, b, owner);
                const uint32_t id = sim.spawn_unit(0, AntType::Bomber, TileCoord{b.x + dc[n], b.y + dr[n]});
                CHECK(sim.issue_order(order(id, OrderType::PlantBomb, b)));
                CHECK(sim.get_unit(id).orig_order == AntUnit::kOrderDefuse);
                int t0 = -1, t = 0;
                TileCoord at{};
                for (; t < 300; ++t) {
                    if (t0 < 0 && sim.get_unit(id).loco_action == AntUnit::kActionDefuse) { t0 = t; at = sim.get_unit(id).pos; }
                    if (t0 >= 0 && !sim.has_bomb_at(b)) break;
                    sim.tick();
                }
                CHECK(t0 >= 0 && !sim.has_bomb_at(b));
                CHECK(std::abs(at.x - b.x) + std::abs(at.y - b.y) == 1);
                CHECK(t - t0 == 24);
                CHECK(sim.get_unit(id).hp == 10);
                CHECK(sim.get_player_stats(0).bombs_defused == 1u);
                CHECK(sim.get_unit(id).type == AntType::Bomber && sim.get_unit(id).pos == at);   // (nothing is picked up, the ant stays where it worked)
            }
        }
        // the other types: a worker, thief and combat ant just walk onto the bomb and set it off; fire ant and swimmer are refused
        for (const AntType type : {AntType::Worker, AntType::Thief, AntType::Combat, AntType::Fire, AntType::Swimmer}) {
            SimulationEngine sim;
            world(sim, seed_for("o"));
            bomb(sim, b, 1);
            const uint32_t id = sim.spawn_unit(0, type, TileCoord{30, 28});
            const bool walks = (type == AntType::Worker || type == AntType::Thief || type == AntType::Combat);
            CHECK(sim.issue_order(order(id, OrderType::PlantBomb, b)) == walks);
            run(sim, 200);
            CHECK(sim.get_player_stats(0).bombs_defused == 0u);
            CHECK(sim.has_bomb_at(b) == !walks);
            CHECK(sim.get_unit(id).hp == (walks ? 8 : 10));
        }
    });

    // --------------------------------------------------------------------------------------------
    // 5. Power-up pick-up, and POPcOrN
    // --------------------------------------------------------------------------------------------

    ants::assets::LevelData pop;
    CHECK(pop.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"));

    run_case("BG5.1 A Power-Up Is Taken Only When A Move ENDS On Its Tile: Walking Over A File Power-Up (Solid Bit Clear) Does Not Take It; Ending On It Does; A Flight That Lands On It Does Not Take It, A Move Order To The Ant's Own Tile Afterwards Does", [&] {
        const TileCoord p{15, 7};                             // POPcOrN: a power-up plug between two walls, free tiles north and south of it
        SimulationEngine sim;
        sim.init(pop, 7, 0x0F);
        CHECK(sim.grid().get_cell(p).has_powerup() && !sim.grid().is_solid_object(p));
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 6});
        sim.issue_move_order(id, TileCoord{15, 8});
        ticks_until(sim, 300, [&] { return sim.get_unit(id).pos == (TileCoord{15, 8}); });
        CHECK(sim.get_unit(id).pos == (TileCoord{15, 8}));
        CHECK(sim.grid().get_cell(p).has_powerup() && sim.get_unit(id).type == AntType::Worker);     // (it walked over the tile)
        const uint8_t ptype = sim.grid().get_powerup_type(p);
        sim.issue_move_order(id, p);
        run(sim, 100);
        CHECK(!sim.grid().get_cell(p).has_powerup() && static_cast<uint8_t>(sim.get_unit(id).type) == ptype && sim.get_unit(id).pos == p);
        // a landing on a power-up: a seed whose bomb is no dud (the map's own start-up draws shift the sequence)
        SimulationEngine s2;
        uint32_t i2 = 0;
        Throw t{};
        for (uint32_t seed = 1; seed < 100 && (t.burn_on_spot || i2 == 0); ++seed) {
            s2.init(pop, seed, 0x0F);
            i2 = s2.spawn_unit(0, AntType::Worker, TileCoord{15, 3});
            t = throw_at(s2, i2, TileCoord{15, 3}, 0);
        }
        CHECK(!t.burn_on_spot && t.to == p);
        run(s2, 400);
        CHECK(s2.get_unit(i2).pos == p && s2.grid().get_cell(p).has_powerup() && s2.get_unit(i2).type == AntType::Worker);
        s2.issue_move_order(i2, p);                           // a move order to the tile the ant stands on ends the move there: the power-up is taken
        run(s2, 100);
        CHECK(!s2.grid().get_cell(p).has_powerup() && static_cast<uint8_t>(s2.get_unit(i2).type) == ptype);
    });

    // The hops over the fire wall of POPcOrN. For every bomb tile T of a home half (valid bomb ground), every facing of the ant on it and the chain that follows: the landing tile of the
    // first throw and, when it holds a bomb of the map (owner 255), the landing of the next throw (the facing the flight leaves is the one it started with, unless a rotation happened).
    // The ant, the bombs and the draws: a dud is retried (another bomb) until the ant flies; the map's bombs that a hop sets off are put back.
    struct Hop {
        TileCoord from;
        int facing;
        std::vector<TileCoord> lands;                          // every landing of the chain
    };
    const auto popcorn_hops = [&](SimulationEngine& sim, int row_lo, int row_hi) {
        const Grid& g = sim.grid();
        const uint32_t id = sim.spawn_unit(1, AntType::Worker, TileCoord{0, 0});
        std::vector<Hop> out;
        for (int y = row_lo; y <= row_hi; ++y) {
            for (int x = 0; x < static_cast<int>(g.width()); ++x) {
                const TileCoord T{x, y};
                if (!sim.is_special_target_valid(AntType::Bomber, T, false, 0)) continue;
                for (int f = 0; f < 8; ++f) {
                    Hop h{T, f, {}};
                    TileCoord at = T;
                    int face = f;
                    std::vector<std::pair<TileCoord, TileCell>> saved;
                    for (int n = 0; n < 12; ++n) {
                        Throw t = throw_at(sim, id, at, face, 0);
                        for (int k = 0; k < 60 && t.burn_on_spot && t.to == at; ++k) t = throw_at(sim, id, at, face, 0);   // (a dud: bomb again)
                        h.lands.push_back(t.to);
                        if (t.to == at) break;                                         // no free landing tile at all
                        face = t.facing_after;
                        at = t.to;
                        if (!g.get_cell(at).has_bomb()) break;
                        saved.push_back({at, g.get_cell(at)});
                    }
                    for (const auto& s : saved) {
                        bomb(sim, s.first, static_cast<uint8_t>(s.second.interactive_id - BOMB_BLACK));
                        sim.grid_mut().get_cell_mut(static_cast<uint32_t>(s.first.x), static_cast<uint32_t>(s.first.y)).interactive_owner = s.second.interactive_owner;
                    }
                    out.push_back(h);
                }
            }
        }
        return out;
    };
    const auto kind = [](const SimulationEngine& sim, TileCoord t) -> char {
        const Grid& g = sim.grid();
        if (!g.in_bounds(t)) return 'X';
        const TileCell& c = g.get_cell(t);
        if (c.has_fire()) return 'F';
        if (c.has_bomb()) return 'B';
        if (c.has_powerup()) return 'P';
        if (g.terrain_class_at(t) == movement::kTerrainWater) return '~';
        return '.';
    };
    // Is the tile an ordinary open tile an ant can come from (so that the facing f on tile T is reachable by walking onto it)?
    const auto open_tile = [](const SimulationEngine& sim, TileCoord t) {
        const Grid& g = sim.grid();
        return g.in_bounds(t) && !g.is_solid_object(t) && g.terrain_class_at(t) != movement::kTerrainWater && !g.get_cell(t).has_fire();
    };
    // The crossing hops of one half as text: per bomb tile, per facing: the chain of landings. `home_row_max`: the half's last row (8 north, 22 south counted as a first row).
    const auto crossing = [](const Hop& h, bool north_half) { return north_half ? h.lands[0].y >= 9 : h.lands[0].y <= 21; };
    std::vector<Hop> north, south;
    SimulationEngine popsim;
    std::vector<std::string> two_hop_power_ups;

    run_case("BG6.1 POPcOrN: Every Hop From A Bomb Tile Of A Home Half Over (Or Onto) Its Fire Wall, Per Facing Of The Ant, With The Chain After It (The Table Is Printed Below); The 2-Hop Chains That End On A Power-Up", [&] {
        popsim.init(pop, 7, 0x0F);
        {   // (the start ants stand on tiles near the hills, and a tile with an ant takes no bomb: the table is the empty map's)
            std::vector<uint32_t> ids;
            for (const AntSnapshot& a : popsim.get_world_state().ants) ids.push_back(a.id);
            for (const uint32_t id : ids) popsim.kill_unit(id);
        }
        CHECK(popsim.grid().width() == 31 && popsim.grid().height() == 31);
        for (int x = 0; x < 31; ++x) {
            CHECK(popsim.grid().get_cell(static_cast<uint32_t>(x), 9).has_fire());      // the two fire walls: the full rows 9 and 21
            CHECK(popsim.grid().get_cell(static_cast<uint32_t>(x), 21).has_fire());
        }
        north = popcorn_hops(popsim, 0, 8);
        south = popcorn_hops(popsim, 22, 30);
        const auto text = [&](const Hop& h) {
            std::string out = "f" + std::to_string(h.facing) + (open_tile(popsim, step(h.from, h.facing, -1)) ? "*" : "");
            for (const TileCoord& l : h.lands) out += " (" + std::to_string(l.x) + "," + std::to_string(l.y) + ")" + kind(popsim, l);
            return out;
        };
        uint64_t table_hash = 1469598103934665603ull;   // (FNV-1a of every printed row of both halves: any change of a landing shows)
        for (int half = 0; half < 2; ++half) {
            const std::vector<Hop>& hops = half == 0 ? north : south;
            size_t over = 0, onto = 0, longer = 0, pu_end = 0, pu_two = 0;
            std::cout << "\n    " << (half == 0 ? "NORTH" : "SOUTH") << " HALF: bomb tile T(x,y), facing f of the ant on it (* = the ant can walk onto T in that facing: the tile behind it is open ground), then the landing of every hop:\n"
                      << "    (. plain, B bomb of the map: sets off again, P power-up, ~ water: a non-swimmer drowns, F fire wall)\n";
            TileCoord last{-1, -1};
            std::string line;
            const auto mix = [&](const std::string& t) { for (const char c : t) table_hash = (table_hash ^ static_cast<unsigned char>(c)) * 1099511628211ull; };
            for (const Hop& h : hops) {
                if (!crossing(h, half == 0)) continue;
                const TileCoord l = h.lands[0];
                const bool fire_cell = (kind(popsim, l) == 'F');
                over += fire_cell ? 0u : 1u;
                onto += fire_cell ? 1u : 0u;
                longer += (!fire_cell && h.lands.size() >= 2) ? 1u : 0u;
                if (!fire_cell && h.lands.size() >= 2 && kind(popsim, h.lands.back()) == 'P') {
                    ++pu_end;
                    if (h.lands.size() == 2) {
                        ++pu_two;
                        if (half == 0) two_hop_power_ups.push_back("T(" + std::to_string(h.from.x) + "," + std::to_string(h.from.y) + ") " + text(h));
                    }
                }
                if (h.from != last) {
                    if (!line.empty()) std::cout << line << "\n";
                    line = "    T(" + std::to_string(h.from.x) + "," + std::to_string(h.from.y) + "):";
                    last = h.from;
                }
                line += " | " + text(h);
                mix("T" + std::to_string(h.from.x) + "," + std::to_string(h.from.y) + " " + text(h) + "\n");
            }
            if (!line.empty()) std::cout << line << "\n";
            std::cout << "    " << (half == 0 ? "north" : "south") << " totals: " << over << " hops over the wall, " << onto << " onto the wall, " << longer << " over the wall with a chain of 2 or more hops, "
                      << pu_end << " chains end on a power-up (" << pu_two << " of them after exactly 2 hops)\n";
            CHECK(over > 0 && onto > 0 && longer > 0 && pu_end > 0 && pu_two > 0);
        }
        std::cout << "    table hash: " << table_hash << "\n    ... " << std::flush;
        CHECK(table_hash == 9950865441434428839ull);
        // the facts that the table rests on, pinned: row 5 reaches the wall itself, row 6 and below fly over it; a bomb tile in row 8 can throw a facing-north ant 4 tiles south to row 12
        bool row5_onto = false, row6_over = false;
        for (const Hop& h : north) {
            if (h.from.y == 5 && h.facing == 0 && h.lands[0] == TileCoord{h.from.x, 9}) row5_onto = true;
            if (h.from.y == 6 && h.facing == 0 && h.lands[0] == TileCoord{h.from.x, 10}) row6_over = true;
        }
        CHECK(row5_onto && row6_over);
        const std::vector<std::string> want = {
            "T(3,7) f0* (3,11)B (3,15)P",   "T(8,7) f6* (12,11)B (16,15)P",  "T(8,7) f7* (12,11)B (16,15)P", "T(9,7) f1* (5,11)B (1,15)P",   "T(21,7) f5* (25,11)B (29,15)P", "T(21,7) f6* (25,11)B (29,15)P",
            "T(21,7) f7* (25,11)B (29,15)P", "T(22,7) f1* (18,11)B (14,15)P", "T(27,7) f0* (27,11)B (27,15)P", "T(27,7) f5* (27,11)B (27,15)P", "T(27,7) f6* (27,11)B (27,15)P", "T(27,7) f7* (27,11)B (27,15)P",
            "T(29,7) f0* (29,11)B (29,15)P", "T(29,7) f5* (29,11)B (29,15)P", "T(29,7) f6* (29,11)B (29,15)P", "T(29,7) f7* (29,11)B (29,15)P",
        };
        CHECK(two_hop_power_ups == want);
    });

    run_case("BG6.2 POPcOrN, A Real Play: A Bomber Plants A Bomb At (3,7) Next To The Row-8 Lane, Steps Onto It Walking North And Is Thrown South Over The Row-9 Fire Wall Onto The Map Bomb At (3,11), Which Throws It On To The Power-Up At (3,15) (4 hp Lost, The Power-Up Not Taken Until A Move Order To Its Own Tile); A Dud On Either Bomb Stops The Chain Where It Happens", [&] {
        size_t landed = 0, stopped_first = 0, stopped_second = 0, seeds = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            ++seeds;
            SimulationEngine sim;
            sim.init(pop, seed, 0x0F);
            const TileCoord T{3, 7}, P{3, 15};
            CHECK(sim.grid().get_cell(P).has_powerup() && sim.grid().get_cell(TileCoord{3, 11}).has_bomb() && sim.grid().get_cell(TileCoord{3, 11}).interactive_owner == kNeutral);
            const uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{3, 8});
            CHECK(sim.issue_order(order(bomber, OrderType::PlantBomb, T)));
            ticks_until(sim, 200, [&] { return sim.has_bomb_at(T); });
            CHECK(sim.has_bomb_at(T) && sim.grid().get_cell(T).interactive_owner == 0);
            CHECK(sim.get_unit(bomber).pos == (TileCoord{3, 8}));
            sim.issue_move_order(bomber, T);                    // the own bomb: ordered onto its tile, the ant steps onto it
            ticks_until(sim, 200, [&] { return !sim.has_bomb_at(T); });
            CHECK(sim.get_unit(bomber).flight_tile == (TileCoord{3, 11}) || sim.get_unit(bomber).flight_tile == T);
            run(sim, 500);
            const AntUnit& a = sim.get_unit(bomber);
            if (a.pos == P) {
                ++landed;
                CHECK(a.hp == 6 && sim.grid().get_cell(P).has_powerup() && a.type == AntType::Bomber && !sim.has_bomb_at(TileCoord{3, 11}));
                sim.issue_move_order(bomber, P);
                run(sim, 100);
                CHECK(!sim.grid().get_cell(P).has_powerup() && a.pos == P);
            } else if (a.pos == T) {
                ++stopped_first;
                CHECK(a.hp == 8 && sim.has_bomb_at(TileCoord{3, 11}));
            } else if (a.pos == (TileCoord{3, 11})) {
                ++stopped_second;
                CHECK(a.hp == 6 && !sim.has_bomb_at(TileCoord{3, 11}));
            } else {
                CHECK(false);
            }
        }
        CHECK(landed + stopped_first + stopped_second == seeds);
        CHECK(landed > seeds / 2 && stopped_first > 0 && stopped_second > 0);      // (0.8 x 0.8 of the seeds fly both hops)
        std::cout << "[" << landed << " of " << seeds << " seeds fly both hops, " << stopped_first << " dud at the planted bomb, " << stopped_second << " dud at the map bomb] ... " << std::flush;
    });

    run_case("BG6.3 POPcOrN, The Lane The Other Way: A Worker Walking South From (9,7) Onto A Bomb At (9,8) Is Thrown North To (9,4); Walking West Along The Lane From (10,8) Onto It Throws It East To (13,8)", [&] {
        for (int mode = 0; mode < 2; ++mode) {
            SimulationEngine sim;
            sim.init(pop, 1, 0x08);                  // (only the south-west team: no start ant walks into the lane)
            const TileCoord b{9, 8};
            bomb(sim, b, 0);
            const uint32_t id = sim.spawn_unit(3, AntType::Worker, mode == 0 ? TileCoord{9, 7} : TileCoord{10, 8});
            sim.issue_move_order(id, b);
            const int t = ticks_until(sim, 300, [&] { return !sim.has_bomb_at(b); });
            CHECK(t < 300);
            const AntUnit& a = sim.get_unit(id);
            CHECK(a.hp == 8);
            CHECK(!a.knock_flag && a.flight_tile == (mode == 0 ? TileCoord{9, 4} : TileCoord{13, 8}));
        }
    });

    run_case("BG6.4 A Landing In Water Drowns Every Ant But A Swimmer (It Splashes And Is Stunned): POPcOrN's Row 14 Is Water", [&] {
        for (int ty = 0; ty < 2; ++ty) {
            SimulationEngine sim;
            world(sim, seed_for("o"));
            const TileCoord at{30, 30};
            sim.set_terrain(30, 34, TERRAIN_WATER);
            const uint32_t id = sim.spawn_unit(1, ty ? AntType::Swimmer : AntType::Worker, at);
            CHECK(throw_at(sim, id, at, 0).to == (TileCoord{30, 34}));
            run(sim, 600);
            CHECK(ty == 0 ? sim.get_unit(id).removed : (!sim.get_unit(id).removed && sim.get_unit(id).hp == 8));
        }
    });

    std::cout << "\nbomb facts: " << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
