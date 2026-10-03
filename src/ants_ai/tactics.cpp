#include "ants_ai/tactics.hpp"

#include <algorithm>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

// ---- what a level does ---------------------------------------------------------------------------------------------------------------------------------

LevelPlan plan_for(Level level) noexcept {
    LevelPlan p;
    p.level = level;
    switch (level) {
        case Level::Easy:
            p.defenders = 1;
            p.leash_tiles = 8;
            p.fight_linger_ticks = 200;
            p.wall_trigger = WallTrigger::ThiefSeen;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 0;
            break;
        case Level::Medium:
            p.defenders = 2;
            p.leash_tiles = 10;
            p.fight_linger_ticks = 100;
            p.wall_trigger = WallTrigger::ThiefPossible;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 200;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = true;
            p.guards = true;
            p.raids = true;
            p.max_combat = 1;
            p.max_thief = 1;
            break;
        case Level::Hard:
            p.defenders = 3;
            p.leash_tiles = 12;
            p.fight_linger_ticks = 60;
            p.wall_trigger = WallTrigger::Always;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 130;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = true;
            p.guards = true;
            p.raids = true;
            p.max_combat = 2;
            p.max_thief = 1;
            p.squads = true;
            p.harasses = true;
            p.avoids_guarded_hills = true;
            break;
    }
    return p;
}

// ---- the thief hole -------------------------------------------------------------------------------------------------------------------------------------

std::array<sim::TileCoord, 3> east_tiles(const HillInfo& hill) noexcept {
    return {sim::TileCoord{hill.origin.x + 4, hill.origin.y + 1}, sim::TileCoord{hill.origin.x + 4, hill.origin.y + 2}, sim::TileCoord{hill.origin.x + 4, hill.origin.y + 3}};
}

namespace {

EastTile classify_east(const sim::Grid& grid, sim::TileCoord t) noexcept {
    if (!grid.in_bounds(t)) return EastTile::Blocked;
    const sim::TileCell& c = grid.get_cell(t);
    if (c.has_fire()) return EastTile::Wall;
    if (c.has_bomb()) return EastTile::Bomb;
    const uint8_t cls = grid.terrain_class_at(t);
    if (!sim::movement::terrain_walkable(cls)) return EastTile::Blocked;                // water (a bridge is mud: walkable)
    if (grid.is_solid_object(t)) return EastTile::Blocked;                              // rocks and every other solid object: a pile, a lunchbox, a power-up, a wall that is being lit
    if (cls == sim::movement::kTerrainMud) return EastTile::Bare;                       // the engine lights no fire wall and plants no bomb on mud
    return EastTile::Open;
}

}  // namespace

EastState east_state(const sim::Grid& grid, const HillInfo& hill) noexcept {
    EastState st;
    if (!hill.present) return st;
    const std::array<sim::TileCoord, 3> tiles = east_tiles(hill);
    for (size_t i = 0; i < 3; ++i) st.tile[i] = classify_east(grid, tiles[i]);
    return st;
}

// ---- the memory ---------------------------------------------------------------------------------------------------------------------------------------

uint64_t Memory::wall_seen(sim::TileCoord tile) const noexcept {
    const auto it = wall_seen_.find({tile.x, tile.y});
    return it != wall_seen_.end() ? it->second : 0u;
}

uint8_t Memory::hp_before(uint32_t ant) const noexcept {
    const auto it = own_.find(ant);
    return it != own_.end() ? it->second.hp : uint8_t{0};
}

void Memory::update(const BotView& v, const MapInfo& map) {
    now_ = v.tick();
    hits_.clear();
    thieves_.clear();

    // the seat's own ants: who lost hit points since the last look (an ant that is not in the previous view cannot be compared), and who is gone
    std::map<uint32_t, Last> now_own;
    for (const AntView& a : v.mine()) {
        Last l;
        l.hp = a.hp;
        l.tile = a.tile;
        l.carried = a.holding || a.carried_points > 0;
        const auto it = own_.find(a.id);
        if (it != own_.end() && a.hp < it->second.hp) hits_.push_back(Hit{a.id, a.tile, it->second.hp, a.hp, it->second.carried});
        now_own[a.id] = l;
    }
    for (const auto& e : own_) {
        if (now_own.count(e.first) == 0) hits_.push_back(Hit{e.first, e.second.tile, e.second.hp, 0, e.second.carried});     // gone: it was killed (or drowned)
    }
    own_ = std::move(now_own);

    // the other teams: who moves, and the Thief ants in sight
    std::map<uint32_t, sim::TileCoord> thief_now;
    for (const AntView& a : v.others()) {
        if (a.state != sim::UnitState::Idle && a.state != sim::UnitState::GuardIdle && seen_moving_[a.team] == 0) seen_moving_[a.team] = v.tick();
        if (a.type != sim::AntType::Thief) continue;
        ThiefSighting t;
        t.ant = a.id;
        t.team = a.team;
        t.tile = a.tile;
        const auto it = enemy_tile_.find(a.id);
        if (it != enemy_tile_.end()) t.before = it->second;
        t.carrying = a.holding;
        thieves_.push_back(t);
        thief_now[a.id] = a.tile;
        thief_last_seen_ = v.tick();
    }
    enemy_tile_ = std::move(thief_now);

    // the walls in front of the own thief hole: the tick of the first look that saw each
    const HillInfo& hill = map.hill(v.seat());
    if (hill.present && v.has_grid()) {
        for (const sim::TileCoord& t : east_tiles(hill)) {
            const std::pair<int32_t, int32_t> key{t.x, t.y};
            if (v.grid().has_fire_at(t)) wall_seen_.emplace(key, v.tick());              // (keeps the first sighting)
            else wall_seen_.erase(key);
        }
    }
}

}  // namespace ants::ai
