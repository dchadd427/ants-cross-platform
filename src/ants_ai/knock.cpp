#include "ants_ai/knock.hpp"

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

int knock_range(sim::AntType striker) noexcept { return striker == sim::AntType::Combat ? 4 : 1; }

uint32_t blow_hit_points(sim::AntType striker) noexcept { return striker == sim::AntType::Combat ? 2u : 1u; }

KnockFlight knock_flight(const sim::Grid& grid, sim::TileCoord victim, sim::TileCoord attacker, int range) noexcept {
    const int d0 = sim::movement::dir_from_delta(victim.y - attacker.y, victim.x - attacker.x);
    static constexpr int kOffsets[5] = {0, 1, -1, 2, -2};
    for (const int offset : kOffsets) {
        const int d = (d0 + offset + 8) % 8;
        const sim::movement::TileDelta delta = sim::movement::dir_delta(static_cast<uint8_t>(d));
        const sim::TileCoord land{victim.x + delta.dcol * range, victim.y + delta.drow * range};
        if (flight_landing_ok(grid, land)) return KnockFlight{true, land};
    }
    return KnockFlight{};
}

KillShot kill_shot(const sim::Grid& grid, sim::TileCoord victim, uint8_t hp, sim::AntType victim_type, sim::TileCoord stand, sim::AntType striker) noexcept {
    KillShot out;
    const uint32_t dmg = blow_hit_points(striker);
    if (hp == 0) return out;
    if (hp <= dmg) {                                               // the blow itself finishes it
        out.kind = KillKind::Blow;
        out.land = victim;
        return out;
    }
    const KnockFlight flight = knock_flight(grid, victim, stand, knock_range(striker));
    if (!flight.flies) return out;
    out.land = flight.land;
    if (grid.terrain_class_at(flight.land) == sim::movement::kTerrainWater) {
        if (victim_type != sim::AntType::Swimmer) out.kind = KillKind::Water;
        return out;
    }
    const uint32_t left = hp - dmg;
    if (grid.has_bomb_at(flight.land) && left <= 2u) out.kind = KillKind::Bomb;
    else if (grid.has_fire_at(flight.land) && victim_type != sim::AntType::Fire && left <= 1u) out.kind = KillKind::Fire;
    return out;
}

}  // namespace ants::ai
