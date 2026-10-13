// AI26: the gate analysis (docs/reverse_engineering/bomb_gates.md, "Gate analysis"): GateInfo says, for every pile and power-up of a map, what stands between a team's hill and it (fire walls, bombs,
// both, or nothing) and which means get past it (a Fire ant, a Bomber that defuses, a Bomber hop chain). It duplicates the engine's throw rule, so the conformance test holds it against the
// simulation tile by tile; the rest is checked on hand-made worlds and on the two maps that are built on gates, POPcOrN (fire walls and a field of bombs) and Bombz Away (bombs everywhere).
//
//   AI26.1   the throw rule against the engine: every free tile of POPcOrN, the 8 facings, the whole chain (landings, hit points, how it ends)
//   AI26.2   the throw rule by hand: opposite the facing, the clockwise turn, a stuck ant, water, fire, the cost and the chance of a chain
//   AI26.3   hand-made worlds: no gate, a fire wall, a bomb line, both, a gap of each, a rock wall that only a hop passes
//   AI26.4   POPcOrN: the components, the barriers, the targets of each team, the hops and the 2-hop chains that end on a power-up (the table of the document)
//   AI26.5   Bombz Away: a bomb under every tile of the way in, the pile behind bombs, a tunnel of defused bombs
//   AI26.6   the shipped maps and the whole library: a gate is only found where a fire wall or a bomb lies; a free target is one the walking analysis reaches; the same map gives the same analysis
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

#include "ai_test.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_ai/island_info.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

constexpr const char* kShipped[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};

assets::LevelData load_level(const std::string& path) {
    assets::LevelData level;
    level.load_from_file(path);
    return level;
}

assets::LevelData popcorn() { return load_level(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"); }

void clear_ants(sim::SimulationEngine& sim) {
    std::vector<uint32_t> ids;
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) ids.push_back(a.id);
    for (const uint32_t id : ids) sim.kill_unit(id);
}

TileCoord step_in(TileCoord t, int dir, int range) {
    const sim::movement::TileDelta d = sim::movement::dir_delta(static_cast<uint8_t>(dir));
    return TileCoord{t.x + d.dcol * range, t.y + d.drow * range};
}

// What the engine does with a bomb on `bomb` and an ant that steps onto it with `facing`: the landings of the chain, read from the simulation itself (the hook sets the bomb off at once). A dud
// burns the ant where it stands and removes the bomb: another bomb is planted until the ant flies (the throw is the same for every draw, only the dud differs). The bombs of the map that the
// chain lands on are put back afterwards.
struct EngineHop {
    std::vector<TileCoord> lands;
    int32_t hp_lost{0};
    bool stuck{false};
};
EngineHop engine_hop(sim::SimulationEngine& sim, uint32_t ant, TileCoord bomb, int facing) {
    EngineHop out;
    TileCoord at = bomb;
    int face = facing;
    std::vector<std::pair<TileCoord, sim::TileCell>> saved;
    for (int n = 0; n < GateInfo::kMaxChain; ++n) {
        TileCoord to = at;
        for (int tries = 0; tries < 80 && to == at; ++tries) {
            sim.get_unit(ant).hp = 10;
            sim.grid_mut().place_bomb(static_cast<uint32_t>(at.x), static_cast<uint32_t>(at.y), 0);
            sim.get_unit(ant).facing = static_cast<sim::Direction>(face);
            sim.trigger_bomb_detonation(ant, at);
            to = sim.get_unit(ant).flight_tile;
        }
        out.hp_lost += 10 - static_cast<int32_t>(sim.get_unit(ant).hp);
        out.lands.push_back(to);
        if (to == at) {
            out.stuck = true;
            break;
        }
        face = static_cast<int>(sim.get_unit(ant).facing);
        at = to;
        if (!sim.grid().get_cell(at).has_bomb()) break;
        saved.push_back({at, sim.grid().get_cell(at)});
    }
    for (const auto& s : saved) {
        sim.grid_mut().place_bomb(static_cast<uint32_t>(s.first.x), static_cast<uint32_t>(s.first.y), static_cast<uint8_t>(s.second.interactive_id - sim::BOMB_BLACK));
        sim.grid_mut().get_cell_mut(static_cast<uint32_t>(s.first.x), static_cast<uint32_t>(s.first.y)).interactive_owner = s.second.interactive_owner;
    }
    return out;
}

// A hand-made world of 40 x 40: the hill of team 0 at (2, 2) and one pile at (12, 30)
struct World {
    sim::SimulationEngine sim;
    int32_t pile{-1};
    World() {
        sim.init_test_world(40, 40, 11, 720000);
        sim.set_anthill(0, TileCoord{2, 2});
        pile = place_crackers(sim, 12, 30, 25);
    }
    void fire_row(int y, int from = 0, int to = 39) { for (int x = from; x <= to; ++x) sim.grid_mut().place_firewall(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1); }
    void bomb_row(int y, int from = 0, int to = 39) { for (int x = from; x <= to; ++x) sim.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1); }
    void rock_row(int y, int from = 0, int to = 39) { for (int x = from; x <= to; ++x) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE); }
    GateInfo gates() {
        const MapInfo map(sim);
        return GateInfo::analyse(map, sim.grid(), 0);
    }
};

}  // namespace

void run_gate_tests() {
    TEST_CASE("AI26.1 The Throw Rule Against The Engine: Every Free Tile Of POPcOrN, All 8 Facings, The Whole Chain (Landings, Hit Points, How It Ends) Is What The Simulation Does") {
        sim::SimulationEngine sim;
        sim.init(popcorn(), 7, 0x0F);
        clear_ants(sim);
        const uint32_t ant = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{0, 0});
        const sim::Grid& grid = sim.grid();
        int compared = 0, chains = 0, stuck = 0;
        for (int y = 0; y < static_cast<int>(grid.height()); ++y) {
            for (int x = 0; x < static_cast<int>(grid.width()); ++x) {
                const TileCoord t{x, y};
                if (!bomb_ground(grid, t)) continue;
                for (int f = 0; f < 8; ++f) {
                    const BombHop mine = GateInfo::simulate(grid, t, static_cast<uint8_t>(f));
                    const EngineHop theirs = engine_hop(sim, ant, t, f);
                    ASSERT_TRUE(mine.lands == theirs.lands);
                    ASSERT_EQ(mine.end == HopEnd::Stuck, theirs.stuck);
                    ASSERT_EQ(mine.bombs * GateInfo::kBombHp, theirs.hp_lost);      // (a landing on fire costs one more once the ant has landed: the flight itself is 2 per bomb)
                    ++compared;
                    chains += mine.bombs > 1 ? 1 : 0;
                    stuck += theirs.stuck ? 1 : 0;
                }
            }
        }
        ASSERT_TRUE(compared > 1000);
        ASSERT_TRUE(chains > 100);
        ASSERT_EQ(stuck, 0);
    } TEST_END();

    TEST_CASE("AI26.2 The Throw Rule By Hand: Opposite The Facing, 4 Tiles, The Clockwise Turn, A Stuck Ant, Water And Fire, The Cost And The Chance Of A Chain") {
        World w;
        const sim::Grid& grid = w.sim.grid();
        const TileCoord at{20, 20};
        for (int f = 0; f < 8; ++f) {
            const BombHop h = GateInfo::simulate(grid, at, static_cast<uint8_t>(f));
            ASSERT_TRUE((h.lands == std::vector<TileCoord>{step_in(at, (f + 4) % 8, 4)}));
            ASSERT_EQ(h.end, HopEnd::Ground);
            ASSERT_EQ(h.bombs, 1);
            ASSERT_EQ(h.hp, 2);
            ASSERT_EQ(h.fly_percent, 80);
        }
        for (int blocked = 0; blocked < 8; ++blocked) {                           // a rock on the landing tile of the first blocked directions: the next one clockwise
            World r;
            for (int i = 0; i < blocked; ++i) r.sim.set_terrain(step_in(at, (4 + i) % 8, 4).x, step_in(at, (4 + i) % 8, 4).y, sim::TERRAIN_OBSTACLE);
            const BombHop h = GateInfo::simulate(r.sim.grid(), at, 0);
            ASSERT_TRUE((h.lands == std::vector<TileCoord>{step_in(at, (4 + blocked) % 8, 4)}));
        }
        {
            World r;
            for (int d = 0; d < 8; ++d) r.sim.set_terrain(step_in(at, d, 4).x, step_in(at, d, 4).y, sim::TERRAIN_OBSTACLE);
            const BombHop h = GateInfo::simulate(r.sim.grid(), at, 0);
            ASSERT_EQ(h.end, HopEnd::Stuck);
            ASSERT_TRUE((h.lands == std::vector<TileCoord>{at}));
            ASSERT_EQ(h.hp, 2);
        }
        {
            World r;                                                               // water and fire on the landing tile: allowed landings, and the end says what happens
            r.sim.set_terrain(20, 24, sim::TERRAIN_WATER);
            ASSERT_EQ(GateInfo::simulate(r.sim.grid(), at, 0).end, HopEnd::Water);
            World r2;
            r2.sim.grid_mut().place_firewall(20, 24, 1);
            const BombHop f = GateInfo::simulate(r2.sim.grid(), at, 0);
            ASSERT_EQ(f.end, HopEnd::Fire);
            ASSERT_EQ(f.hp, 3);
        }
        {
            World r;                                                               // a chain of three bombs, all in line: the same direction on, 2 hit points each, the chance 80 * 80 * 80
            r.sim.grid_mut().place_bomb(20, 24, 1);
            r.sim.grid_mut().place_bomb(20, 28, 1);
            const BombHop h = GateInfo::simulate(r.sim.grid(), at, 0);
            ASSERT_TRUE((h.lands == std::vector<TileCoord>{TileCoord{20, 24}, TileCoord{20, 28}, TileCoord{20, 32}}));
            ASSERT_EQ(h.bombs, 3);
            ASSERT_EQ(h.hp, 6);
            ASSERT_EQ(h.fly_percent, 51);
            ASSERT_EQ(GateInfo::kDudPercent, 20);
            ASSERT_EQ(GateInfo::kFlightRange, 4);
        }
        {
            World r;                                                               // the map edge refuses the landing: facing south at (30, 2) goes north out of the map, then NE out, then east
            const BombHop h = GateInfo::simulate(r.sim.grid(), TileCoord{30, 2}, 4);
            ASSERT_TRUE((h.lands == std::vector<TileCoord>{TileCoord{34, 2}}));
        }
    } TEST_END();

    TEST_CASE("AI26.3 Hand-Made Worlds: No Gate, A Fire Wall, A Bomb Line, Both, A Gap Of Each, A Rock Wall That Only A Hop Passes") {
        {
            World w;
            const GateInfo g = w.gates();
            ASSERT_TRUE(!g.empty() && g.home() >= 0);
            ASSERT_EQ(g.piles().size(), 1u);
            ASSERT_EQ(g.piles()[0].gate, GateKind::None);
            ASSERT_TRUE(g.piles()[0].free() && g.piles()[0].fire_cells == 0 && g.piles()[0].defuse_bombs == 0 && g.piles()[0].passes == 0);
            ASSERT_EQ(g.free_piles(), 1);
            ASSERT_TRUE(g.barriers().empty());
        }
        {
            World w;                                                               // a fire wall across the world: a Fire ant walks through it (one cell), a Bomber hops over it
            w.fire_row(20);
            const GateInfo g = w.gates();
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Fire);
            ASSERT_EQ(t.fire_cells, 1);
            ASSERT_EQ(t.defuse_bombs, -1);
            ASSERT_TRUE((t.passes & kPassFireAnt) != 0 && (t.passes & kPassDefuse) == 0 && (t.passes & kPassHop) != 0);
            ASSERT_EQ(g.fire_cell_count(), 40);
            ASSERT_EQ(g.barriers().size(), 1u);
            ASSERT_TRUE(g.barriers()[0].kind == GateKind::Fire && g.barriers()[0].cells == 40);
        }
        {
            World w;                                                               // a bomb line: a Bomber defuses one bomb; a hop lands behind it
            w.bomb_row(20);
            const GateInfo g = w.gates();
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Bombs);
            ASSERT_EQ(t.defuse_bombs, 1);
            ASSERT_EQ(t.fire_cells, -1);
            ASSERT_TRUE((t.passes & kPassDefuse) != 0 && (t.passes & kPassFireAnt) == 0 && (t.passes & kPassHop) != 0);
            ASSERT_EQ(g.bomb_cell_count(), 40);
        }
        {
            World w;                                                               // a fire wall and a bomb line behind it: no walk passes both with one means
            w.fire_row(20);
            w.bomb_row(21);
            const GateInfo g = w.gates();
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Mixed);
            ASSERT_TRUE(t.fire_cells == -1 && t.defuse_bombs == -1);
            ASSERT_TRUE((t.passes & kPassHop) != 0);                                // the hop is 4 tiles: from row 19 or 18 it lands on row 23 or 22, behind both
            ASSERT_TRUE(g.hops()[static_cast<size_t>(t.hop)].bombs == 1);
        }
        {
            World w;                                                               // a rock wall with a gap of fire at x = 5 and a gap of one bomb at x = 30: either way
            w.rock_row(20);
            w.sim.set_terrain(5, 20, sim::TERRAIN_WALKABLE);
            w.sim.set_terrain(30, 20, sim::TERRAIN_WALKABLE);
            w.sim.grid_mut().place_firewall(5, 20, 1);
            w.sim.grid_mut().place_bomb(30, 20, 1);
            const GateInfo g = w.gates();
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Either);
            ASSERT_EQ(t.fire_cells, 1);
            ASSERT_EQ(t.defuse_bombs, 1);
            ASSERT_TRUE((t.passes & kPassFireAnt) != 0 && (t.passes & kPassDefuse) != 0);
        }
        {
            World w;                                                               // a rock wall, one tile thick, across the world: sealed for every walker; a hop passes it
            w.rock_row(20);
            const GateInfo g = w.gates();
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Sealed);
            ASSERT_EQ(t.passes, kPassHop);
            const BombHop& h = g.hops()[static_cast<size_t>(t.hop)];
            ASSERT_EQ(h.bombs, 1);
            ASSERT_EQ(h.end, HopEnd::Ground);
            ASSERT_TRUE(h.bomb.y < 20 && h.land().y > 20 && h.land().y == h.bomb.y + 4);
            ASSERT_EQ(g.component(h.from), g.home());
            ASSERT_EQ(g.component(h.land()), h.to_comp);
            ASSERT_TRUE(h.to_comp != g.home());
            ASSERT_TRUE(step_in(h.from, h.facing, 1) == h.bomb);
            ASSERT_FALSE(g.hops_to(h.to_comp).empty());
            ASSERT_FALSE(g.hops_onto(h.land()).empty());
        }
        {
            World w;                                                               // a rock wall two tiles thick: the 4-tile flight still lands behind it, from the tile right in front
            w.rock_row(20);
            w.rock_row(21);
            const GateInfo g = w.gates();
            ASSERT_EQ(g.piles()[0].gate, GateKind::Sealed);
            ASSERT_TRUE((g.piles()[0].passes & kPassHop) != 0);
            ASSERT_EQ(g.hops()[static_cast<size_t>(g.piles()[0].hop)].land().y - g.hops()[static_cast<size_t>(g.piles()[0].hop)].bomb.y, 4);
        }
        {
            World w;                                                               // a wall of rocks 5 thick: no flight clears it, and nothing else does
            for (int y = 18; y <= 22; ++y) w.rock_row(y);
            const GateInfo g = w.gates();
            ASSERT_EQ(g.piles()[0].gate, GateKind::Sealed);
            ASSERT_EQ(g.piles()[0].passes, 0);
            ASSERT_EQ(g.piles()[0].hop, -1);
            ASSERT_EQ(g.passable_piles(), 0);
        }
        {
            World w;                                                               // a bomb tile whose four cardinal neighbours are rocks: no Bomber can stand where it works from, so no hop starts there
            w.rock_row(20);
            for (const TileCoord r : {TileCoord{20, 16}, TileCoord{19, 17}, TileCoord{21, 17}, TileCoord{20, 18}}) w.sim.set_terrain(r.x, r.y, sim::TERRAIN_OBSTACLE);
            const GateInfo g = w.gates();
            int from_walled = 0;
            for (const BombHop& h : g.hops()) from_walled += (h.bomb == TileCoord{20, 17}) ? 1 : 0;
            ASSERT_EQ(from_walled, 0);
            World open;                                                            // the same world without the rocks: the tile has hops
            open.rock_row(20);
            int from_open = 0;
            const GateInfo g2 = open.gates();
            for (const BombHop& h : g2.hops()) from_open += (h.bomb == TileCoord{20, 17}) ? 1 : 0;
            ASSERT_TRUE(from_open > 0);
        }
        {
            const GateInfo g;                                                      // an analysis that was never made is empty, and so is one for a team without a hill
            ASSERT_TRUE(g.empty());
            World w;
            const MapInfo map(w.sim);
            ASSERT_TRUE(GateInfo::analyse(map, w.sim.grid(), 3).empty());
            ASSERT_TRUE(GateInfo::analyse(map, w.sim.grid(), 9).empty());
        }
    } TEST_END();

    TEST_CASE("AI26.4 POPcOrN: Two Fire Walls And A Field Of Bombs; The Home Half Is Free, The Field Is Behind Fire AND Bombs, The Lake's Pile Is Sealed; The Hops Over The Wall And The 2-Hop Chains To A Power-Up") {
        sim::SimulationEngine sim;
        sim.init(popcorn(), 7, 0x0F);
        const MapInfo map(sim);
        const sim::Grid& grid = sim.grid();
        std::vector<GateInfo> gates;
        for (uint8_t team = 0; team < 4; ++team) gates.push_back(GateInfo::analyse(map, grid, team));
        for (const GateInfo& g : gates) {
            ASSERT_TRUE(!g.empty() && g.home() >= 0);
            ASSERT_EQ(g.fire_cell_count(), 62);
            ASSERT_EQ(g.bomb_cell_count(), 249);
            ASSERT_EQ(g.piles().size(), 1u);
            ASSERT_EQ(g.piles()[0].gate, GateKind::Sealed);                       // the pile lies in the lake
            ASSERT_EQ(g.powerups().size(), 32u);
            int none = 0, mixed = 0, sealed = 0;
            for (const GateTarget& t : g.powerups()) {
                none += t.gate == GateKind::None ? 1 : 0;
                mixed += t.gate == GateKind::Mixed ? 1 : 0;
                sealed += t.gate == GateKind::Sealed ? 1 : 0;
                ASSERT_TRUE(t.gate != GateKind::Fire && t.gate != GateKind::Bombs && t.gate != GateKind::Either);   // nothing is behind fire alone or bombs alone
            }
            ASSERT_EQ(none, 11);                                                  // the power-ups of the two home halves... of THIS half and the rows it can walk to
            ASSERT_EQ(mixed, 11);
            ASSERT_EQ(sealed, 10);
        }
        ASSERT_EQ(gates[0].home(), gates[2].home());                              // the two north hills share the north half
        ASSERT_EQ(gates[1].home(), gates[3].home());
        ASSERT_TRUE(gates[0].home() != gates[1].home());
        // the barriers: row 9 and row 21 are one fire wall each (31 cells), the field of bombs is a few groups
        int fire_walls = 0;
        for (const GateBarrier& b : gates[0].barriers()) {
            if (b.kind == GateKind::Fire && b.cells == 31 && b.min.x == 0 && b.max.x == 30 && b.min.y == b.max.y && (b.min.y == 9 || b.min.y == 21)) ++fire_walls;
        }
        ASSERT_EQ(fire_walls, 2);
        // the hops: only from the half's own tiles, only onto ground of another component, none onto water or fire
        ASSERT_EQ(gates[0].hops().size(), 42u);
        ASSERT_EQ(gates[2].hops().size(), 42u);
        ASSERT_EQ(gates[1].hops().size(), 44u);
        ASSERT_EQ(gates[3].hops().size(), 45u);
        for (const GateInfo& g : gates) {
            for (const BombHop& h : g.hops()) {
                ASSERT_EQ(h.end, HopEnd::Ground);
                ASSERT_EQ(h.from_comp, g.home());
                ASSERT_TRUE(h.to_comp >= 0 && h.to_comp != g.home());
                ASSERT_EQ(g.component(h.land()), h.to_comp);
                ASSERT_EQ(g.component(h.from), g.home());
                ASSERT_TRUE(step_in(h.from, h.facing, 1) == h.bomb);
                ASSERT_EQ(h.hp, h.bombs * 2);
            }
        }
        // the 2-hop chains of the north half that end ON a power-up (the document's list, complete now: the table of the document leaves out the tiles where a start ant stood when it was made)
        std::set<std::string> two;
        for (const BombHop& h : gates[0].hops()) {
            if (h.lands.size() != 2) continue;
            for (const PowerUpInfo& p : map.powerups()) {
                if (p.tile == h.land()) two.insert("T(" + std::to_string(h.bomb.x) + "," + std::to_string(h.bomb.y) + ") f" + std::to_string(h.facing) + " " + std::to_string(h.lands[0].x) + "," + std::to_string(h.lands[0].y) + " " + std::to_string(h.lands[1].x) + "," + std::to_string(h.lands[1].y));
            }
        }
        const std::set<std::string> want = {
            "T(3,7) f0 3,11 3,15",   "T(8,7) f6 12,11 16,15",  "T(8,7) f7 12,11 16,15",  "T(9,7) f1 5,11 1,15",    "T(21,7) f5 25,11 29,15", "T(21,7) f6 25,11 29,15", "T(21,7) f7 25,11 29,15",
            "T(22,7) f1 18,11 14,15", "T(27,7) f0 27,11 27,15", "T(27,7) f5 27,11 27,15", "T(27,7) f6 27,11 27,15", "T(27,7) f7 27,11 27,15", "T(29,7) f0 29,11 29,15", "T(29,7) f5 29,11 29,15",
            "T(29,7) f6 29,11 29,15", "T(29,7) f7 29,11 29,15",
        };
        ASSERT_TRUE(two == want);
        // the hop that the target names is the best one: fewest bombs, and it lands in reach
        int with_hop = 0;
        for (const GateTarget& t : gates[0].powerups()) {
            if (t.hop < 0) continue;
            ++with_hop;
            const BombHop& h = gates[0].hops()[static_cast<size_t>(t.hop)];
            ASSERT_TRUE(std::find(t.comps.begin(), t.comps.end(), h.to_comp) != t.comps.end());
            ASSERT_TRUE((t.passes & kPassHop) != 0);
            for (const BombHop* other : gates[0].hops_to(h.to_comp)) ASSERT_TRUE(other->bombs >= h.bombs);
        }
        ASSERT_EQ(with_hop, 17);
    } TEST_END();

    TEST_CASE("AI26.5 Bombz Away: A Bomb Lies On Every Tile In Front Of Every Ramp, So No Ant Starts On Safe Ground; The Pile Is Behind Bombs Only, Reached By A Tunnel Of Defused Bombs") {
        sim::SimulationEngine sim;
        sim.init(load_level(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl"), 7, 0x0F);
        const MapInfo map(sim);
        for (uint8_t team = 0; team < 4; ++team) {
            const GateInfo g = GateInfo::analyse(map, sim.grid(), team);
            ASSERT_FALSE(g.empty());
            ASSERT_EQ(g.home(), -1);
            ASSERT_EQ(g.bomb_cell_count(), 895);
            ASSERT_EQ(g.fire_cell_count(), 0);
            ASSERT_TRUE(g.hops().empty());
            ASSERT_EQ(g.piles().size(), 1u);
            const GateTarget& t = g.piles()[0];
            ASSERT_EQ(t.gate, GateKind::Bombs);
            ASSERT_EQ(t.fire_cells, -1);
            const int32_t tunnel[4] = {13, 8, 12, 9};                              // the bombs of the shortest walk, the one in front of the ramp included
            ASSERT_EQ(t.defuse_bombs, tunnel[team]);
            ASSERT_EQ(t.passes, kPassDefuse);
            ASSERT_EQ(g.passable_piles(), 1);
            ASSERT_EQ(g.free_piles(), 0);
        }
    } TEST_END();

    TEST_CASE("AI26.6 The Shipped Maps And The Whole Library: A Gate Is Found Only Where A Fire Wall Or A Bomb Lies, A Free Target Is One The Walking Analysis Reaches, The Same Map Gives The Same Analysis") {
        std::vector<std::string> files;
        for (const char* m : kShipped) files.push_back(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + m + ".LVL");
        for (const auto& e : std::filesystem::directory_iterator(std::string(ANTS_SOURCE_DIR) + "/Community-Maps")) {
            if (e.path().extension() == ".lvl") files.push_back(e.path().string());
        }
        std::sort(files.begin(), files.end());
        size_t no_home = 0, with_home = 0, analysed = 0, gated = 0, with_bombs = 0, with_fire = 0;
        for (const std::string& file : files) {
            sim::SimulationEngine sim;
            assets::LevelData level = load_level(file);
            sim.init(level, 5, 0x0F);
            const MapInfo map(sim);
            const bool any_gate_cells = [&] {
                for (uint32_t y = 0; y < sim.grid().height(); ++y) {
                    for (uint32_t x = 0; x < sim.grid().width(); ++x) {
                        const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                        if (sim.grid().has_fire_at(t) || sim.grid().has_bomb_at(t)) return true;
                    }
                }
                return false;
            }();
            for (uint8_t team = 0; team < 4; ++team) {
                if (!map.hill(team).present) continue;
                const GateInfo g = GateInfo::analyse(map, sim.grid(), team);
                const GateInfo again = GateInfo::analyse(map, sim.grid(), team);
                ASSERT_FALSE(g.empty());
                ++analysed;
                ASSERT_EQ(g.hops().size(), again.hops().size());
                if (g.home() < 0) ASSERT_TRUE(g.hops().empty());
                for (const BombHop& h : g.hops()) ASSERT_TRUE(g.home() >= 0 && h.from_comp == g.home() && h.to_comp >= 0 && h.to_comp != g.home());
                ++(g.home() < 0 ? no_home : with_home);
                ASSERT_EQ(g.barriers().size(), again.barriers().size());
                for (size_t i = 0; i < g.piles().size(); ++i) {
                    ASSERT_TRUE(g.piles()[i].gate == again.piles()[i].gate && g.piles()[i].hop == again.piles()[i].hop && g.piles()[i].defuse_bombs == again.piles()[i].defuse_bombs);
                    const bool walk_reaches = map.can_reach_pile(team, map.hill(team).starts.empty() ? map.hill(team).entrance : map.hill(team).starts[0], static_cast<uint32_t>(i));
                    if (g.piles()[i].gate == GateKind::None) ASSERT_TRUE(walk_reaches);                       // free here means reachable there (that analysis lets an ant step on bombs: the converse needs no bombs)
                    if (!any_gate_cells && !map.piles()[i].cells.empty()) ASSERT_EQ(g.piles()[i].gate == GateKind::None, walk_reaches);
                    if (g.piles()[i].gate != GateKind::None) ++gated;
                    ASSERT_TRUE(g.piles()[i].gate != GateKind::Fire || g.fire_cell_count() > 0);
                    ASSERT_TRUE(g.piles()[i].gate != GateKind::Bombs || g.bomb_cell_count() > 0);
                }
                with_bombs += g.bomb_cell_count() > 0 ? 1u : 0u;
                with_fire += g.fire_cell_count() > 0 ? 1u : 0u;
            }
        }
        ASSERT_TRUE(no_home > 50 && with_home > 300);                                     // (the library has both kinds: a bomb under every ramp, and a safe way out)
        ASSERT_TRUE(analysed > 400);
        ASSERT_TRUE(with_bombs > 100 && with_fire > 0);
        ASSERT_TRUE(gated > 0);
    } TEST_END();
}
