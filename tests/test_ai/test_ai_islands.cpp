// AI14: the water analysis of the bots (island_info.hpp, docs/BOTS.md "Islands"): ISLANDS and SMALL against the facts that were measured, and every flight, bridge and row of power-ups the
// analysis lists against the engine itself (a bomb is planted and an ant walks onto it, a swimmer digs a chain of tiles and a worker crosses it, a worker walks a row of power-ups).
#include "ai_test.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

#include "ants_ai/island_info.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

TileCoord tile_of(const sim::AntSnapshot& a) { return TileCoord{a.tile_x, a.tile_y}; }

const sim::AntSnapshot* snap_of(sim::SimulationEngine& sim, uint32_t id) {
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

void run_ticks(sim::SimulationEngine& sim, int n) {
    for (int i = 0; i < n; ++i) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
    }
}

Command order(CommandType type, uint8_t issuer, uint32_t ant, TileCoord tile) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = static_cast<int16_t>(tile.x);
    c.tile_y = static_cast<int16_t>(tile.y);
    c.ants = {ant};
    return c;
}

// the match as the arena starts it, without any ant (each trial puts its own)
void empty_match(sim::SimulationEngine& sim, const std::string& map, uint32_t seed) {
    start_match(sim, map, seed, 0x0F);
    for (uint8_t t = 0; t < 4; ++t) {
        for (const uint32_t id : ants_of(sim, t)) sim.kill_unit(id);
    }
}

constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// the members of a token group, as "FFSSSFF" (the kinds in the order of a walk)
std::string kinds_of(const MapInfo& map, const TokenGroup& g) {
    std::string s;
    for (const uint32_t m : g.members) s += "?BFTCS"[static_cast<int>(map.powerups()[m].type)];
    return s;
}

// the least number of water tiles on a chain from the walker component `a` to the walker component `b`, found the other way round (a 0-1 search over every tile of the map: the tiles of the two
// components are free, a bridgeable water tile costs 1, nothing else may be crossed): the analysis' own breadth-first search from the shore must agree
int least_water_tiles(const MapInfo& map, const sim::Grid& grid, uint8_t team, int32_t a, int32_t b, int limit) {
    const int w = map.width();
    const int h = map.height();
    std::vector<int> cost(static_cast<size_t>(w) * static_cast<size_t>(h), 1 << 20);
    std::deque<int> q;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (map.component(team, TileCoord{x, y}) == a) {
                cost[static_cast<size_t>(y * w + x)] = 0;
                q.push_back(y * w + x);
            }
        }
    }
    while (!q.empty()) {
        const int idx = q.front();
        q.pop_front();
        const int x = idx % w;
        const int y = idx / w;
        const int c = cost[static_cast<size_t>(idx)];
        for (int k = 0; k < 8; ++k) {
            const TileCoord n{x + kDx[k], y + kDy[k]};
            if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h) continue;
            const int32_t comp = map.component(team, n);
            int step;
            if (comp == b) return c;                       // the shore of b is next to the last water tile (c tiles so far)
            if (comp >= 0) continue;                        // another island or this one: not a way
            if (!bridge_water(grid, n)) continue;
            step = c + 1;
            if (step > limit) continue;
            if (step < cost[static_cast<size_t>(n.y * w + n.x)]) {
                cost[static_cast<size_t>(n.y * w + n.x)] = step;
                q.push_back(n.y * w + n.x);               // (a unit-cost search in layers: the first time b is met is the shortest)
            }
        }
    }
    return -1;
}

// ---- one flight against the engine -----------------------------------------------------------------------------------------------------------------------

struct Trial {
    bool skipped{false};     // no free tile for the bomber next to the bomb tile
    bool planted{false};
    bool dud{false};         // the bomb went off and the ant stayed where the bomb was
    bool landed{false};
    TileCoord at{-1, -1};
    bool alive{true};
};

Trial fly(const Flight& f, uint32_t seed) {
    Trial r;
    sim::SimulationEngine sim;
    empty_match(sim, "ISLANDS", seed);
    MapInfo map(sim);
    const sim::Grid& grid = sim.grid();
    // the bomber: a free walker tile of the same island next to the bomb tile, not S
    TileCoord spot{-1, -1};
    for (int k = 0; k < 8 && spot.x < 0; ++k) {
        const TileCoord t{f.bomb.x + kDx[k], f.bomb.y + kDy[k]};
        if (t == f.from || !grid.in_bounds(t) || !MapInfo::walkable(grid, 0, t) || map.component(0, t) != f.from_comp) continue;
        spot = t;
    }
    if (spot.x < 0) {
        r.skipped = true;
        return r;
    }
    const uint32_t jumper = sim.spawn_unit(0, sim::AntType::Worker, f.from);
    const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, spot);
    run_ticks(sim, 2);
    sim.apply_command(order(CommandType::GroupSpecial, 0, bomber, f.bomb));
    for (int i = 0; i < 400 && !grid.get_cell(f.bomb).has_bomb(); ++i) run_ticks(sim, 1);
    r.planted = grid.get_cell(f.bomb).has_bomb();
    if (!r.planted) return r;
    sim.apply_command(order(CommandType::GroupMove, 0, jumper, f.bomb));
    run_ticks(sim, 150);
    const sim::AntSnapshot* a = snap_of(sim, jumper);
    r.alive = a != nullptr && a->hp > 0 && !a->is_drowning && a->state != sim::UnitState::Drowning && a->state != sim::UnitState::Dead;
    if (a != nullptr) r.at = tile_of(*a);
    r.dud = r.alive && r.at == f.bomb && !grid.get_cell(f.bomb).has_bomb();
    r.landed = r.alive && r.at == f.land;
    return r;
}

// ---- one channel against the engine ----------------------------------------------------------------------------------------------------------------------

// a swimmer digs the chain tile by tile from the shore of a, then a worker walks from a tile of a to a tile of b; true when it arrives
bool cross(const Channel& ch, bool& built) {
    built = false;
    sim::SimulationEngine sim;
    empty_match(sim, "ISLANDS", 1);
    MapInfo map(sim);
    const sim::Grid& grid = sim.grid();
    TileCoord shore_a{-1, -1};
    TileCoord shore_b{-1, -1};
    for (int k = 0; k < 8; ++k) {
        const TileCoord first{ch.tiles.front().x + kDx[k], ch.tiles.front().y + kDy[k]};
        const TileCoord last{ch.tiles.back().x + kDx[k], ch.tiles.back().y + kDy[k]};
        if (shore_a.x < 0 && grid.in_bounds(first) && map.component(0, first) == ch.a) shore_a = first;
        if (shore_b.x < 0 && grid.in_bounds(last) && map.component(0, last) == ch.b) shore_b = last;
    }
    if (shore_a.x < 0 || shore_b.x < 0) return false;
    const uint32_t swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, shore_a);
    run_ticks(sim, 2);
    for (const TileCoord& t : ch.tiles) {
        sim.apply_command(order(CommandType::GroupSpecial, 0, swimmer, t));
        int waited = 0;
        while (grid.get_cell(t).interactive_id != sim::TILE_BRIDGE4 && waited < 300) {
            run_ticks(sim, 1);
            ++waited;
        }
        if (grid.get_cell(t).interactive_id != sim::TILE_BRIDGE4) return false;
    }
    built = true;
    // the swimmer parks in the water next to the last tile (an idle ant on a one-tile-wide bridge or on the tile that the worker is going to shuts the way: a stationary team-mate costs 8000)
    TileCoord park{-1, -1};
    for (int r = 1; r <= 3 && park.x < 0; ++r) {
        for (int dy = -r; dy <= r && park.x < 0; ++dy) {
            for (int dx = -r; dx <= r && park.x < 0; ++dx) {
                const TileCoord t{ch.tiles.back().x + dx, ch.tiles.back().y + dy};
                if (!grid.in_bounds(t) || grid.terrain_class_at(t) != sim::movement::kTerrainWater) continue;
                if (std::find(ch.tiles.begin(), ch.tiles.end(), t) != ch.tiles.end()) continue;
                park = t;
            }
        }
    }
    if (park.x < 0) return false;
    sim.apply_command(order(CommandType::GroupMove, 0, swimmer, park));
    for (int i = 0; i < 400; ++i) {
        const sim::AntSnapshot* s = snap_of(sim, swimmer);
        if (s != nullptr && tile_of(*s) == park) break;
        run_ticks(sim, 1);
    }
    const uint32_t worker = sim.spawn_unit(0, sim::AntType::Worker, shore_a);
    sim.apply_command(order(CommandType::GroupMove, 0, worker, shore_b));
    for (int i = 0; i < 900; ++i) {
        run_ticks(sim, 1);
        const sim::AntSnapshot* a = snap_of(sim, worker);
        if (a == nullptr) return false;
        if (tile_of(*a) == shore_b) return true;
    }
    return false;
}

}  // namespace

void run_island_tests() {
    TEST_CASE("AI14.1 ISLANDS Has 21 Islands For Every Team (The Four Hills' Islands, Eight Satellites With Food Or A Bomber, The Four Pieces Of The Ring, The Two Strips Of The Flowers): Where The Food, The Power-Ups And The Flowers Are, And That Each Hill Reaches Exactly One Power-Up On Foot, Its Own Bomber")
    {
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x0F);
        MapInfo map(sim);
        const int32_t hill_comp[4] = {8, 13, 12, 9};                                                        // team 0 green (NW), 1 red (SE), 2 blue (SW), 3 black (NE)
        for (uint8_t t = 0; t < 4; ++t) {
            const IslandInfo info = IslandInfo::analyse(map, sim.grid(), t);
            ASSERT_EQ(info.components().size(), 21u);
            ASSERT_EQ(map.hill_component(t), hill_comp[t]);
            for (const IslandComponent& c : info.components()) ASSERT_EQ(c.hill, c.id == hill_comp[t]);
            // the food: 600 points on each of four satellites (two piles of ten units of 30), 1,600 on each side of the ring (one pile of 80 units of 20), nothing on a hill's island
            std::map<int32_t, uint64_t> points;
            for (const IslandComponent& c : info.components()) {
                for (const uint32_t p : c.piles) points[c.id] += static_cast<uint64_t>(map.piles()[p].units) * map.piles()[p].value;
            }
            ASSERT_EQ(points.size(), 6u);
            ASSERT_EQ(points[0], 1600u);
            ASSERT_EQ(points[3], 1600u);
            for (const int32_t c : {4, 11, 14, 19}) {
                ASSERT_EQ(points[c], 600u);
                ASSERT_EQ(info.components()[static_cast<size_t>(c)].piles.size(), 2u);
            }
            for (uint8_t other = 0; other < 4; ++other) ASSERT_TRUE(info.components()[static_cast<size_t>(hill_comp[other])].piles.empty());
            // the flowers: the daisies at (30, 4) and (30, 55), their drops fall on (30, 5) and (30, 56): the strips at the top and the bottom of the ring
            ASSERT_EQ(info.components()[1].flowers.size(), 1u);
            ASSERT_EQ(info.components()[20].flowers.size(), 1u);
            ASSERT_TRUE(info.components()[1].flowers[0] == tc(30, 4));
            ASSERT_TRUE(info.components()[20].flowers[0] == tc(30, 55));
            ASSERT_TRUE(flower_drop_tile(info.components()[1].flowers[0]) == tc(30, 5));
            ASSERT_TRUE(flower_drop_tile(info.components()[20].flowers[0]) == tc(30, 56));
            size_t flowers = 0;
            for (const IslandComponent& c : info.components()) flowers += c.flowers.size();
            ASSERT_EQ(flowers, 2u);                                                                         // (the six other plants are clovers and flowers of other kinds)
            // what a hill reaches on foot: one power-up, the Bomber on its own island
            size_t reach = 0;
            for (const PowerUpInfo& p : map.powerups()) {
                if (!p.approach[t].reachable()) continue;
                ++reach;
                ASSERT_TRUE(p.type == sim::AntType::Bomber);
            }
            ASSERT_EQ(reach, 1u);
        }
        ASSERT_EQ(map.powerups().size(), 40u);                                                              // 12 Bombers, 16 Fire and 12 Swimmers
        size_t kinds[6] = {0, 0, 0, 0, 0, 0};
        for (const PowerUpInfo& p : map.powerups()) ++kinds[static_cast<int>(p.type)];
        ASSERT_EQ(kinds[1], 12u);
        ASSERT_EQ(kinds[2], 16u);
        ASSERT_EQ(kinds[5], 12u);
        ASSERT_EQ(kinds[3] + kinds[4], 0u);
    } TEST_END();

    TEST_CASE("AI14.2 The Power-Ups Of ISLANDS Stand In Rows: Each Corner Is A Row Of Seven (Fire, Fire, Swimmer, Swimmer, Swimmer, Fire, Fire) That No Ant Reaches Except Through The Next One, With An Entrance At Each End; The Twelve Bombers Stand Alone, One On Every Island Of A Hill And On Every Satellite That Has No Food")
    {
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x0F);
        MapInfo map(sim);
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        ASSERT_EQ(info.token_groups().size(), 16u);
        size_t rows = 0;
        size_t singles = 0;
        for (const TokenGroup& g : info.token_groups()) {
            if (g.members.size() == 7) {
                ++rows;
                ASSERT_TRUE(kinds_of(map, g) == "FFSSSFF");
                ASSERT_EQ(g.entrances.size(), 2u);
                ASSERT_EQ(g.entrances[0].member, 0u);                                                       // one end ...
                ASSERT_EQ(g.entrances[1].member, 6u);                                                       // ... and the other
                ASSERT_TRUE(g.entrances[0].comp == 0 || g.entrances[0].comp == 3);
                for (size_t i = 1; i < g.members.size(); ++i) {                                             // every member is next to an earlier one: a row that is walked from its first entrance (the corner of the bottom right is a triangle)
                    bool next_to_earlier = false;
                    for (size_t j = 0; j < i; ++j) next_to_earlier = next_to_earlier || map.powerups()[g.members[j]].tile.chebyshev_dist(map.powerups()[g.members[i]].tile) == 1;
                    ASSERT_TRUE(next_to_earlier);
                }
            } else {
                ASSERT_EQ(g.members.size(), 1u);
                ASSERT_TRUE(map.powerups()[g.members[0]].type == sim::AntType::Bomber);
                ASSERT_EQ(g.entrances.size(), 1u);
                ++singles;
            }
        }
        ASSERT_EQ(rows, 4u);
        ASSERT_EQ(singles, 12u);
        // the top left corner, as measured: the row (3, 1) (2, 1) (1, 1) (0, 1) (0, 2) (0, 3) (0, 4), entered at (4, 1) or at (0, 5)
        bool found = false;
        for (const TokenGroup& g : info.token_groups()) {
            if (g.members.size() != 7 || !(map.powerups()[g.members[0]].tile == tc(3, 1))) continue;
            found = true;
            ASSERT_TRUE(g.entrances[0].tile == tc(4, 1));
            ASSERT_TRUE(g.entrances[1].tile == tc(0, 5));
            ASSERT_TRUE(map.powerups()[g.members[6]].tile == tc(0, 4));
        }
        ASSERT_TRUE(found);
        // no hill reaches a Swimmer on foot, and no component that a hill stands on holds one
        for (uint8_t t = 0; t < 4; ++t) {
            for (const PowerUpInfo& p : map.powerups()) ASSERT_FALSE(p.type == sim::AntType::Swimmer && p.approach[t].reachable());
        }
    } TEST_END();

    TEST_CASE("AI14.3 The Channels Of ISLANDS Are The Shortest Chains Of Water Tiles Between Two Islands (Checked By A Second, Independent Search): 2 Tiles To The West Island, 3 To The Next Hill And To The Island Of The Centre, 2 To Every Satellite With Food Of The Other Hills, 1 Or 2 To The Strips Of The Flowers")
    {
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x0F);
        MapInfo map(sim);
        for (uint8_t t = 0; t < 4; ++t) {
            const IslandInfo info = IslandInfo::analyse(map, sim.grid(), t);
            ASSERT_EQ(info.channels().size(), 68u);
            for (const Channel& ch : info.channels()) {
                ASSERT_TRUE(ch.a >= 0 && ch.a < ch.b);
                ASSERT_TRUE(ch.length() >= 1 && ch.length() <= IslandInfo::kMaxChannel);
                for (size_t i = 0; i < ch.tiles.size(); ++i) {
                    ASSERT_TRUE(bridge_water(sim.grid(), ch.tiles[i]));
                    if (i > 0) ASSERT_EQ(ch.tiles[i - 1].chebyshev_dist(ch.tiles[i]), 1);                   // the tiles are neighbours (a bridge piece is walkable like mud: no corner rule)
                }
                bool touches_a = false;
                bool touches_b = false;
                for (int k = 0; k < 8; ++k) {
                    touches_a = touches_a || map.component(t, TileCoord{ch.tiles.front().x + kDx[k], ch.tiles.front().y + kDy[k]}) == ch.a;
                    touches_b = touches_b || map.component(t, TileCoord{ch.tiles.back().x + kDx[k], ch.tiles.back().y + kDy[k]}) == ch.b;
                }
                ASSERT_TRUE(touches_a);
                ASSERT_TRUE(touches_b);
                ASSERT_EQ(ch.length(), least_water_tiles(map, sim.grid(), t, ch.a, ch.b, IslandInfo::kMaxChannel));        // nobody digs a longer bridge than the shortest
            }
            // no channel is missing: any two islands that a chain of at most kMaxChannel tiles joins have one
            for (int32_t a = 0; a < 21; ++a) {
                for (int32_t b = a + 1; b < 21; ++b) {
                    const bool listed = info.channel(a, b) != nullptr;
                    ASSERT_EQ(listed, least_water_tiles(map, sim.grid(), t, a, b, IslandInfo::kMaxChannel) >= 0);
                }
            }
        }
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        const auto len = [&](int32_t a, int32_t b) { return info.channel(a, b) != nullptr ? info.channel(a, b)->length() : -1; };
        ASSERT_EQ(len(8, 10), 2);                                                                           // the hill 0's island to the island west of it
        ASSERT_EQ(len(10, 8), 2);                                                                           // (either order)
        ASSERT_EQ(len(0, 10), 2);                                                                           // that island to the western part of the ring
        ASSERT_EQ(len(8, 4), 3);                                                                            // to the island of the centre (the two piles)
        ASSERT_EQ(len(8, 5), 3);
        ASSERT_EQ(len(8, 9), 3);                                                                            // hill to hill
        ASSERT_EQ(len(8, 12), 3);
        ASSERT_EQ(len(9, 11), 2);                                                                           // hill 3 to the satellite east of it (two piles)
        ASSERT_EQ(len(13, 19), 2);                                                                          // hill 1 to the satellite south of it (two piles)
        ASSERT_EQ(len(12, 14), 2);                                                                          // hill 2 to the satellite west of it (two piles)
        ASSERT_EQ(len(3, 11), 1);                                                                           // the eastern part of the ring to that satellite
        ASSERT_EQ(len(0, 14), 2);
        ASSERT_EQ(len(0, 1), 1);                                                                            // the western part of the ring to the strip of the top flower
        ASSERT_EQ(len(1, 3), 2);
        ASSERT_EQ(len(0, 20), 2);                                                                           // and to the strip of the bottom flower
        ASSERT_EQ(len(3, 20), 1);
        ASSERT_EQ(len(2, 3), -1);                                                                           // (a single tile at the top is joined to nothing within the limit)
    } TEST_END();

    TEST_CASE("AI14.4 The Flights Of ISLANDS: A Hill's Island Flies Only To Its Neighbours (Hill 0: Six Flights To The West Island, Three Straight And Three Diagonal), Each Flight's Landing Is A Walkable Tile Of Another Island, And Nothing Lands In Water")
    {
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x0F);
        MapInfo map(sim);
        const IslandInfo info = IslandInfo::analyse(map, sim.grid(), 0);
        ASSERT_EQ(info.flights().size(), 285u);
        for (const Flight& f : info.flights()) {
            ASSERT_TRUE(f.from_comp != f.to_comp);
            ASSERT_EQ(map.component(0, f.from), f.from_comp);
            ASSERT_EQ(map.component(0, f.bomb), f.from_comp);
            ASSERT_EQ(map.component(0, f.land), f.to_comp);                                                 // a tile that an ant can stand on: water is no landing
            ASSERT_EQ(f.from.chebyshev_dist(f.bomb), 1);
            ASSERT_TRUE(bomb_ground(sim.grid(), f.bomb));
            ASSERT_TRUE(flight_landing_ok(sim.grid(), f.land));
            ASSERT_EQ(f.land.x, f.from.x - 3 * (f.bomb.x - f.from.x));                                      // L = B - 4 (B - S) = S - 3 (B - S)
            ASSERT_EQ(f.land.y, f.from.y - 3 * (f.bomb.y - f.from.y));
        }
        // team 0: the hill's island flies to the island west of it, nowhere else
        const std::vector<const Flight*> out = info.flights_from(8);
        ASSERT_EQ(out.size(), 6u);
        size_t diagonal = 0;
        for (const Flight* f : out) {
            ASSERT_EQ(f->to_comp, 10);
            diagonal += f->diagonal() ? 1u : 0u;
        }
        ASSERT_EQ(diagonal, 3u);
        const Flight* straight = nullptr;
        for (const Flight* f : out) {
            if (f->from == tc(18, 24)) straight = f;
        }
        ASSERT_TRUE(straight != nullptr);
        ASSERT_TRUE(straight->bomb == tc(19, 24));
        ASSERT_TRUE(straight->land == tc(15, 24));
        // the other hills: the satellites next to them (hill 1: 6 + 8, hill 2: 2 + 2 + 9, hill 3: 9 + 1)
        const int32_t hills[4] = {8, 13, 12, 9};
        const size_t total[4] = {6, 14, 13, 10};
        for (uint8_t t = 0; t < 4; ++t) {
            const IslandInfo each = IslandInfo::analyse(map, sim.grid(), t);
            ASSERT_EQ(each.flights_from(hills[t]).size(), total[t]);
        }
        // a flight from the west island to the ring, and the walk by flights from hill 0's island to the flower strip at the top and the one at the bottom
        ASSERT_EQ(info.flights_between(10, 0).size(), 2u);
        const std::vector<int32_t> to_top = info.flight_path(8, 1);
        ASSERT_TRUE(to_top.size() >= 2);
        ASSERT_EQ(to_top.front(), 10);
        ASSERT_EQ(to_top.back(), 1);
        const std::vector<int32_t> none = info.flight_path(8, 8);
        ASSERT_TRUE(none.empty());
        const std::vector<int32_t> d = info.flight_distances(8);
        ASSERT_EQ(d[8], 0);
        ASSERT_EQ(d[10], 1);
        ASSERT_EQ(d[1], static_cast<int32_t>(to_top.size()));
    } TEST_END();

    TEST_CASE("AI14.5 Every One Of The 285 Flights Of ISLANDS Against The Engine: A Bomb Is Planted On B, An Ant On S Walks Onto It, And It Lands On L (A Fifth Of The Bombs Are Duds That Burn The Ant Where It Stands: Every Flight Is Tried With Other Engine Seeds Until A Bomb Goes Off)")
    {
        sim::SimulationEngine probe;
        start_match(probe, "ISLANDS", 1, 0x0F);
        MapInfo map(probe);
        const IslandInfo info = IslandInfo::analyse(map, probe.grid(), 0);
        size_t proven = 0;
        size_t skipped = 0;
        size_t duds = 0;
        for (const Flight& f : info.flights()) {
            bool landed = false;
            bool skip = false;
            for (uint32_t seed = 1; seed <= 12 && !landed && !skip; ++seed) {
                const Trial r = fly(f, seed);
                if (r.skipped) {
                    skip = true;
                    break;
                }
                ASSERT_TRUE(r.planted);                                                                     // the bomb tile takes a bomb, from some neighbour tile
                if (r.dud) {
                    ++duds;
                    continue;
                }
                ASSERT_TRUE(r.alive);                                                                       // nobody drowns
                ASSERT_TRUE(r.at == f.land);                                                                // the engine lands the ant where the analysis says
                landed = true;
            }
            if (skip) ++skipped;
            else {
                ASSERT_TRUE(landed);
                ++proven;
            }
        }
        ASSERT_TRUE(proven >= 270);                                                                         // (a flight whose bomb tile has no free neighbour of the island for the bomber is skipped: a handful at most)
        ASSERT_TRUE(skipped <= 15);
        (void)duds;                                                                                         // (a fifth of the bombs are duds, and an engine seed decides for every bomb it draws at the same point: the count says nothing)
    } TEST_END();

    TEST_CASE("AI14.6 Every Channel Of At Most Three Tiles Against The Engine: A Swimmer Digs The Chain Tile By Tile From The Shore (Each Tile Becomes A Finished Bridge), Steps Off, And A Worker Walks Across To The Other Island")
    {
        sim::SimulationEngine probe;
        start_match(probe, "ISLANDS", 1, 0x0F);
        MapInfo map(probe);
        const IslandInfo info = IslandInfo::analyse(map, probe.grid(), 0);
        size_t tried = 0;
        for (const Channel& ch : info.channels()) {
            if (ch.length() > 3) continue;
            bool built = false;
            const bool crossed = cross(ch, built);
            ASSERT_TRUE(built);
            ASSERT_TRUE(crossed);
            ++tried;
        }
        ASSERT_EQ(tried, 44u);                                                                              // 7 of one tile, 24 of two, 13 of three
    } TEST_END();

    TEST_CASE("AI14.7 A Row Of Power-Ups Is Walked By One Ant At A Time: An Ant That Takes Fire, Fire And Swimmer Ends A Swimmer That Cannot Leave (The Fire Tokens It Dropped Stand Between It And The Entrance); Three Plain Ants, Each Taking One Token And Walking Out, Leave The Swimmer Free")
    {
        {
            sim::SimulationEngine sim;
            empty_match(sim, "ISLANDS", 1);
            const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{4, 1});
            run_ticks(sim, 2);
            for (const TileCoord t : {tc(3, 1), tc(2, 1), tc(1, 1)}) {
                sim.apply_command(order(CommandType::GroupMove, 0, ant, t));
                run_ticks(sim, 90);
            }
            const sim::AntSnapshot* a = snap_of(sim, ant);
            ASSERT_TRUE(a != nullptr);
            ASSERT_TRUE(a->type == sim::AntType::Swimmer);
            ASSERT_TRUE(tile_of(*a) == tc(1, 1));
            ASSERT_TRUE(sim.grid().get_cell(2, 1).has_powerup());                                           // what it dropped: Fire, on the tile it came from, and Fire behind that
            ASSERT_TRUE(sim.grid().get_cell(3, 1).has_powerup());
            sim.apply_command(order(CommandType::GroupMove, 0, ant, tc(6, 3)));
            run_ticks(sim, 200);
            a = snap_of(sim, ant);
            ASSERT_TRUE(a != nullptr);
            ASSERT_TRUE(tile_of(*a) == tc(1, 1));                                       // it cannot leave
            ASSERT_TRUE(a->type == sim::AntType::Swimmer);
        }
        {
            sim::SimulationEngine sim;
            empty_match(sim, "ISLANDS", 1);
            const uint32_t first = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{4, 1});
            run_ticks(sim, 2);
            std::vector<uint32_t> crew{first};
            const TileCoord row[3] = {tc(3, 1), tc(2, 1), tc(1, 1)};
            for (int i = 0; i < 3; ++i) {
                if (i > 0) crew.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{4, 1}));
                const uint32_t ant = crew.back();
                sim.apply_command(order(CommandType::GroupMove, 0, ant, row[i]));
                run_ticks(sim, 90);
                const sim::AntSnapshot* a = snap_of(sim, ant);
                ASSERT_TRUE(a != nullptr);
                ASSERT_TRUE(tile_of(*a) == row[i]);
                sim.apply_command(order(CommandType::GroupMove, 0, ant, tc(6, 3)));                         // each plain ant walks out: nothing stands behind it
                run_ticks(sim, 140);
                a = snap_of(sim, ant);
                ASSERT_TRUE(a != nullptr);
                ASSERT_TRUE(a->tile_x >= 4);
            }
            ASSERT_TRUE(snap_of(sim, crew[0])->type == sim::AntType::Fire);
            ASSERT_TRUE(snap_of(sim, crew[1])->type == sim::AntType::Fire);
            ASSERT_TRUE(snap_of(sim, crew[2])->type == sim::AntType::Swimmer);                              // the swimmer is outside and free
            ASSERT_FALSE(sim.grid().get_cell(1, 1).has_powerup());
            ASSERT_TRUE(sim.grid().get_cell(0, 1).has_powerup());                                           // (two Swimmers are still in the row)
            ASSERT_TRUE(sim.grid().get_cell(0, 2).has_powerup());
        }
    } TEST_END();

    TEST_CASE("AI14.8 SMALL: The Lake Cuts Off One Island (50 Units Of 20, 1,000 Points) By A Chain Of Three Tiles; The Two Swimmers Lie On Its Banks And Every Hill Reaches Them On Foot; The Two Flowers Are In The Hill's Component; Nothing Flies")
    {
        sim::SimulationEngine sim;
        start_match(sim, "SMALL", 1, 0x0F);
        MapInfo map(sim);
        for (uint8_t t = 0; t < 4; ++t) {
            const IslandInfo info = IslandInfo::analyse(map, sim.grid(), t);
            ASSERT_EQ(info.components().size(), 2u);
            ASSERT_EQ(map.hill_component(t), 0);
            ASSERT_TRUE(info.components()[0].hill);
            ASSERT_FALSE(info.components()[1].hill);
            ASSERT_EQ(info.components()[1].piles.size(), 1u);
            ASSERT_EQ(static_cast<uint64_t>(map.piles()[info.components()[1].piles[0]].units) * map.piles()[info.components()[1].piles[0]].value, 1000u);
            ASSERT_EQ(info.components()[0].piles.size(), 4u);                                               // 3,000 points on foot
            ASSERT_EQ(info.channels().size(), 1u);
            ASSERT_EQ(info.channels()[0].length(), 3);
            ASSERT_TRUE(info.flights().empty());
            ASSERT_EQ(info.components()[0].flowers.size(), 2u);
            const TileCoord f0 = info.components()[0].flowers[0];
            const TileCoord f1 = info.components()[0].flowers[1];
            ASSERT_TRUE((f0 == tc(2, 19) && f1 == tc(37, 19)) || (f0 == tc(37, 19) && f1 == tc(2, 19)));
            ASSERT_EQ(info.components()[0].powerups.size(), 2u);
            for (const uint32_t p : info.components()[0].powerups) {
                ASSERT_TRUE(map.powerups()[p].type == sim::AntType::Swimmer);
                ASSERT_TRUE(map.powerups()[p].approach[t].reachable());
            }
            for (const TokenGroup& g : info.token_groups()) ASSERT_EQ(g.members.size(), 1u);
        }
        const auto& drops = sim.get_world_state().flower_droppers;
        ASSERT_EQ(drops.size(), 2u);
        ASSERT_TRUE(drops[0].drop_x == 2 || drops[0].drop_x == 37);
        ASSERT_EQ(drops[0].drop_y, 20);
        ASSERT_EQ(drops[1].drop_y, 20);
    } TEST_END();

    TEST_CASE("AI14.9 Nothing Lies Beyond Water On TINY, MEDIUM, GAUNTLET And TREASURE: No Channel At All (The Guard Of The Island Task: TREASURE Has Five Components And Eighteen Flights Over Its Rocks, GAUNTLET One), And A Bridge Is Never Dug Where No Water Is")
    {
        for (const std::string name : {"TINY", "MEDIUM", "GAUNTLET", "TREASURE"}) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            MapInfo map(sim);
            for (uint8_t t = 0; t < 4; ++t) {
                const IslandInfo info = IslandInfo::analyse(map, sim.grid(), t);
                ASSERT_TRUE(info.channels().empty());
                ASSERT_EQ(info.components().size(), name == "TREASURE" ? 5u : 1u);
                ASSERT_EQ(info.flights().size(), name == "TREASURE" ? 18u : 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI14.10 The Tile Tests Of The Analysis Are The Engine's: bomb_ground And bridge_water Agree With The Engine's Own Cursor Rule (is_special_target_valid) On Every Tile Without An Ant Of Six Maps, And flight_landing_ok Refuses A Rock, A Hill, The Tiles Of A Hill's Queue Row And Raid Tile, And The Edge, And Accepts Water And A Fire Wall")
    {
        for (const std::string name : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"}) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const sim::Grid& grid = sim.grid();
            std::set<std::pair<int32_t, int32_t>> occupied;
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) occupied.insert({a.tile_x, a.tile_y});
            size_t compared = 0;
            for (int32_t y = 0; y < static_cast<int32_t>(grid.height()); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(grid.width()); ++x) {
                    if (occupied.count({x, y}) != 0) continue;
                    const TileCoord t{x, y};
                    ASSERT_EQ(bomb_ground(grid, t), sim.is_special_target_valid(sim::AntType::Bomber, t, false, 0));
                    ASSERT_EQ(bridge_water(grid, t), sim.is_special_target_valid(sim::AntType::Swimmer, t, false, 0));
                    ++compared;
                }
            }
            ASSERT_TRUE(compared > 900);
        }
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x0F);
        const sim::Grid& grid = sim.grid();
        ASSERT_FALSE(flight_landing_ok(grid, tc(-1, 5)));
        ASSERT_FALSE(flight_landing_ok(grid, tc(60, 5)));
        ASSERT_FALSE(flight_landing_ok(grid, tc(0, 0)));                                                    // a rock
        ASSERT_FALSE(flight_landing_ok(grid, tc(22, 23)));                                                  // inside hill 0's mound
        ASSERT_FALSE(flight_landing_ok(grid, tc(22, 21)));                                                  // its queue row
        ASSERT_FALSE(flight_landing_ok(grid, tc(24, 24)));                                                  // its raid tile (bx + 3, by + 2) is (24, 24)
        ASSERT_TRUE(flight_landing_ok(grid, tc(30, 30)));                                                   // water
        ASSERT_TRUE(flight_landing_ok(grid, tc(26, 22)));                                                   // ground
        ASSERT_FALSE(flight_landing_ok(grid, tc(20, 21)));                                                  // a power-up
        sim.grid_mut().set_fire_at(tc(26, 22), 100);
        ASSERT_TRUE(flight_landing_ok(sim.grid(), tc(26, 22)));                                             // a fire wall counts as free (the engine's rule)
        ASSERT_FALSE(bomb_ground(sim.grid(), tc(26, 22)));
    } TEST_END();
}
