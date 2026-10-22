#pragma once

// Where a blow throws its victim, and what waits there (docs/BOT_FIGHT.md). The engine's rule (combat_system.cpp, FUN_0101d8ed KnockDir and FUN_0101c1e2 / FUN_0101de7e): a blow that
// lands on an ant makes it fly from the contact tile in the direction attacker -> victim, 1 tile for a worker's blow and 4 for a Combat Ant's punch; when the landing tile is refused
// (outside the map, a solid object that is not a fire wall, a special tile of a hill, inside a mound) the directions +1, -1, +2, -2 are tried at the same range, and with none left the
// victim is not thrown at all (it is stunned: cornered). Water, ants and bombs are NO reasons to refuse a landing:
//   water    the victim drowns (a non-Swimmer: at once, whatever its hit points; a Swimmer only splashes)
//   a bomb   it goes off: the victim loses 2 hit points and is thrown 4 tiles again (a dud one time in five burns it on the spot, with the same 2 hit points lost)
//   a fire wall  the victim loses 1 hit point and is thrown one tile (a Fire Ant is immune)
// The blow itself takes 1 hit point (a Combat Ant's punch 2), and an ant with no hit points left dies. The direction depends on the tile the attacker stands on when it strikes, so a
// bot that wants a certain direction first walks to that tile and strikes from there.

#include "ants_ai/bot_view.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai {

/// The flight of a blow: `flies` is false for a victim that is cornered (no landing is accepted: it is stunned where it stands)
struct KnockFlight {
    bool flies{false};
    sim::TileCoord land{-1, -1};
};

/// The range of the flight of a blow of an ant of this type: 4 for a Combat Ant's punch, 1 for every other blow
int knock_range(sim::AntType striker) noexcept;
/// The hit points that a blow of an ant of this type takes: 2 for a Combat Ant, 1 for every other
uint32_t blow_hit_points(sim::AntType striker) noexcept;

/// The engine's KnockDir: the victim at `victim`, struck by an ant on `attacker` (adjacent), the flight of `range` tiles
KnockFlight knock_flight(const sim::Grid& grid, sim::TileCoord victim, sim::TileCoord attacker, int range) noexcept;

/// How a blow can kill
enum class KillKind : uint8_t { None, Blow, Water, Bomb, Fire };

struct KillShot {
    KillKind kind{KillKind::None};
    sim::TileCoord land{-1, -1};         // where the victim lands (the victim's own tile for a blow that kills outright)
    bool kills() const noexcept { return kind != KillKind::None; }
};

/// Whether ONE blow of an ant of type `striker`, struck from `stand` (a tile next to the victim) on `victim` with `hp` hit points and of `victim_type`, kills it, and how: the blow alone when
/// the hit points are not more than it takes, the water where it lands (never a Swimmer), a bomb or a fire wall there that finishes what is left. Fire Ants are immune to fire walls.
KillShot kill_shot(const sim::Grid& grid, sim::TileCoord victim, uint8_t hp, sim::AntType victim_type, sim::TileCoord stand, sim::AntType striker) noexcept;

}  // namespace ants::ai
