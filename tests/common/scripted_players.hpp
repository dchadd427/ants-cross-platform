// The scripted players of the headless plays: what a plausible player does with its ants, once a game second per team, from a fixed-seed generator. Used by the map
// sweep (tools/map_sweep.cpp: every map of a folder is played with them) and by the round-trip proof of the map writer (tests/test_sim/test_map_roundtrip.cpp: a map
// and its rewrite play the same). Header-only, no state outside the object.
#pragma once

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ants_test {

// A tiny deterministic generator (the plays must not depend on the library's own PRNG; the same as the tests of the command layer)
struct Lcg {
    uint32_t s{1};
    Lcg() = default;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// What a plausible player does with its ants, once a second per team: hatch, stop, or send a handful of ants to food (harvest, and the points hatch
// more ants), an enemy hill (raid), an enemy ant (attack), its own hill or a random tile (special orders: plant, light, build). Every order is
// issued through SimulationEngine::apply_command like the lock-step host does.
class ScriptedPlayers {
public:
    ScriptedPlayers(const ants::assets::LevelData& level, uint8_t roster, uint32_t seed, bool top_up)
        : roster_(roster), top_up_(top_up), width_(static_cast<int32_t>(level.width.val)), height_(static_cast<int32_t>(level.height.val)) {
        for (const ants::assets::FoodSchedule& f : level.food_schedules) food_.push_back(clamp(f.x, f.y));
        for (const ants::assets::AnthillSpawn& sp : level.anthill_spawns) {
            if (sp.team_id < 4) hills_[sp.team_id].push_back(clamp(sp.x, sp.y));
        }
        for (uint32_t p = 0; p < 4; ++p) rng_[p] = Lcg(seed + p * 7919u);
    }

    /// Appends this second's commands; returns the number of ants alive now (every team)
    uint32_t orders(ants::sim::SimulationEngine& engine, std::vector<ants::sim::Command>& out) {
        namespace sim = ants::sim;
        if (top_up_) {      // a hatch costs 200 points, which a real player earns by harvesting: without them most plays of a short sweep would never hatch
            for (uint8_t p = 0; p < 4; ++p) {
                if ((roster_ & (1u << p)) != 0 && engine.get_player_score(p) < static_cast<int32_t>(sim::HATCH_COST_POINTS)) {
                    engine.set_player_score(p, static_cast<int32_t>(sim::HATCH_COST_POINTS));
                }
            }
        }
        std::array<std::vector<const sim::AntSnapshot*>, 4> by_team;
        uint32_t alive = 0;
        for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
            if (a.hp == 0 || a.player_id >= 4) continue;
            ++alive;
            by_team[a.player_id].push_back(&a);
        }
        for (uint8_t p = 0; p < 4; ++p) {
            if ((roster_ & (1u << p)) == 0) continue;
            Lcg& rng = rng_[p];
            const std::vector<const sim::AntSnapshot*>& mine = by_team[p];
            const uint32_t roll = rng.below(100);
            sim::Command c;
            c.issuer = p;
            if (roll < 20) {
                c.type = sim::CommandType::Hatch;
                out.push_back(c);
                continue;
            }
            if (mine.empty()) continue;
            for (const sim::AntSnapshot* a : mine) {
                if (c.ants.size() < sim::kMaxCommandAnts && rng.below(2) == 0) c.ants.push_back(a->id);
            }
            if (c.ants.empty()) c.ants.push_back(mine[rng.below(static_cast<uint32_t>(mine.size()))]->id);
            if (roll < 30) {
                c.type = sim::CommandType::Stop;
            } else if (roll < 45) {
                c.type = sim::CommandType::GroupSpecial;
                aim(c, target(p, 2, by_team, rng));
            } else if (roll < 65) {
                c.type = sim::CommandType::GroupAttack;
                aim(c, target(p, 1, by_team, rng));
            } else {
                c.type = sim::CommandType::GroupMove;
                aim(c, target(p, 0, by_team, rng));
            }
            out.push_back(c);
        }
        return alive;
    }

private:
    struct Tile {
        int16_t x{0};
        int16_t y{0};
    };
    Tile clamp(int32_t x, int32_t y) const {
        Tile t;
        t.x = static_cast<int16_t>(std::min(std::max(x, 0), width_ - 1));
        t.y = static_cast<int16_t>(std::min(std::max(y, 0), height_ - 1));
        return t;
    }
    static void aim(ants::sim::Command& c, const Tile& t) {
        c.tile_x = t.x;
        c.tile_y = t.y;
    }
    Tile random_tile(Lcg& rng) const {
        return clamp(static_cast<int32_t>(rng.below(static_cast<uint32_t>(width_))), static_cast<int32_t>(rng.below(static_cast<uint32_t>(height_))));
    }
    // kind: 0 move, 1 attack, 2 special
    Tile target(uint8_t p, int kind, const std::array<std::vector<const ants::sim::AntSnapshot*>, 4>& by_team, Lcg& rng) {
        namespace sim = ants::sim;
        const uint32_t r = rng.below(100);
        std::vector<const sim::AntSnapshot*> enemies;
        for (uint8_t t = 0; t < 4; ++t) {
            if (t == p || (roster_ & (1u << t)) == 0) continue;
            enemies.insert(enemies.end(), by_team[t].begin(), by_team[t].end());
        }
        std::vector<Tile> enemy_hills;
        for (uint8_t t = 0; t < 4; ++t) {
            if (t == p || (roster_ & (1u << t)) == 0) continue;
            enemy_hills.insert(enemy_hills.end(), hills_[t].begin(), hills_[t].end());
        }
        auto enemy_ant = [&]() { const sim::AntSnapshot* a = enemies[rng.below(static_cast<uint32_t>(enemies.size()))]; return clamp(a->tile_x, a->tile_y); };
        auto enemy_hill = [&]() { return enemy_hills[rng.below(static_cast<uint32_t>(enemy_hills.size()))]; };
        if (kind == 1) {
            if (r < 70 && !enemies.empty()) return enemy_ant();
            if (!enemy_hills.empty()) return enemy_hill();
        } else if (kind == 0) {
            if (r < 35 && !food_.empty()) return food_[rng.below(static_cast<uint32_t>(food_.size()))];
            if (r < 50 && !enemy_hills.empty()) return enemy_hill();
            if (r < 65 && !hills_[p].empty()) return hills_[p][rng.below(static_cast<uint32_t>(hills_[p].size()))];
        } else if (r < 50 && !by_team[p].empty()) {
            const sim::AntSnapshot* a = by_team[p][rng.below(static_cast<uint32_t>(by_team[p].size()))];
            return clamp(a->tile_x + static_cast<int32_t>(rng.below(5)) - 2, a->tile_y + static_cast<int32_t>(rng.below(5)) - 2);
        }
        return random_tile(rng);
    }

    uint8_t roster_;
    bool top_up_;
    int32_t width_;
    int32_t height_;
    std::vector<Tile> food_;
    std::array<std::vector<Tile>, 4> hills_;
    std::array<Lcg, 4> rng_;
};

}  // namespace ants_test
