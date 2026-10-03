#include "ants_ai/tactics.hpp"

#include <algorithm>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

// ---- what a level does ---------------------------------------------------------------------------------------------------------------------------------

namespace {
// the own side's power-ups that Medium and Hard take in the opening, in the order of value: the Fire Ant (walls, and what an enemy would burn the piles with), the Bomber (what an
// enemy would mine the base with), the Thief (the enemy's raids, and the own: a thief that steals the own Thief power-up has two thieves at the start)
constexpr uint8_t kSecureOpening = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)) | (1u << static_cast<unsigned>(sim::AntType::Thief)));
}  // namespace

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
            p.contest_opening_ants = 0;
            break;
        case Level::Medium:
            p.defenders = 2;
            p.leash_tiles = 10;
            p.fight_linger_ticks = 100;
            p.wall_trigger = WallTrigger::ThiefPossible;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 200;
            p.contest_opening_ants = 1;
            p.secure_side = true;
            p.secure_kinds = kSecureOpening;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = false;
            p.guards = true;
            p.raids = true;
            p.max_combat = 1;
            p.max_thief = 1;
            break;
        case Level::Hard:
            p.defenders = 3;
            p.leash_tiles = 12;
            p.fight_linger_ticks = 60;
            p.wall_trigger = WallTrigger::Early;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 130;
            p.contest_opening_ants = 2;
            p.secure_side = true;
            p.secure_kinds = kSecureOpening;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = false;
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

// ---- the sides of the map -----------------------------------------------------------------------------------------------------------------------------

int power_up_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept {
    for (const PowerUpInfo& p : map.powerups()) {
        if (p.tile != tile) continue;
        int best = -1;
        int32_t best_cost = 0;
        bool tie = false;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            if (((present >> t) & 1u) == 0 || !p.approach[t].reachable()) continue;
            const int32_t cost = p.approach[t].cost;
            if (best < 0 || cost < best_cost) {
                best = t;
                best_cost = cost;
                tie = false;
            } else if (cost == best_cost) {
                tie = true;
            }
        }
        return tie ? -1 : best;
    }
    return -1;
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
    if (!hits_.empty()) last_hit_ = v.tick();

    // the other teams: who moves, and the Thief ants in sight
    std::map<uint32_t, sim::TileCoord> thief_now;
    for (const AntView& a : v.others()) {
        if (a.state != sim::UnitState::Idle && a.state != sim::UnitState::GuardIdle && seen_moving_[a.team] == 0) seen_moving_[a.team] = v.tick();
        if (a.type == sim::AntType::Combat) combat_last_seen_ = v.tick();
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

// ---- the threat to the thief hole ---------------------------------------------------------------------------------------------------------------------

bool reachable_by(const MapInfo& map, uint8_t team, sim::TileCoord tile) noexcept {
    const int32_t home = map.hill_component(team);
    if (home < 0) return false;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (map.component(team, sim::TileCoord{tile.x + dx, tile.y + dy}) == home) return true;
        }
    }
    return false;
}

bool wall_demand(const Tactics& tactics, const BotView& view, const MapInfo& map) {
    const HillInfo& hill = map.hill(view.seat());
    if (!hill.present || !view.has_grid() || view.ticks_left() < 400) return false;
    if (!east_state(view.grid(), hill).shuttable()) return false;                       // a mud tile cannot take a wall: the hole cannot be shut
    const Memory& m = tactics.memory;
    const uint64_t now = view.tick();
    const LevelPlan& plan = tactics.plan;
    if (plan.wall_trigger == WallTrigger::Never) return false;
    const bool seen = m.thief_last_seen() != 0 && now <= m.thief_last_seen() + plan.wall_latch_ticks;
    if (seen) return true;
    if (plan.wall_trigger == WallTrigger::ThiefSeen) return false;
    // an enemy that can reach a Thief power-up that lies on the map: Medium waits until the enemy plays (the idle bot's ants never move: nothing of it is a threat), Hard does not wait
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (t == view.seat() || !row.present || row.dropped || (view.ally() < sim::MAX_PLAYERS && t == view.ally())) continue;
        if (plan.wall_trigger == WallTrigger::ThiefPossible && !m.plays(t)) continue;
        for (const PowerUpView& p : view.powerups()) {
            if (p.kind == sim::AntType::Thief && reachable_by(map, t, p.tile)) return true;
        }
    }
    return false;
}

}  // namespace ants::ai
