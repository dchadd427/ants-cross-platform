#pragma once

// How big a map an ONLINE match may have, and what a big map costs the server (docs/SERVER.md, "Online size limit"). Until the server's load with big maps is measured, a room plays a map only up to
// kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that is played online), only up to kMaxStartAnts starting ants (all four colors together) and only up to
// kMaxPlantWaypointPairs pairs of a plant and a waypoint; a bigger approved or test map stays on the lists, can be opened and edited in the Map Maker and played against bots on the player's own computer or
// in the browser's test game, but a room refuses it with one sentence (net::kNoticeMapTooBig, net::kNoticeMapTooManyAnts, net::kNoticeMapTooManyPairs). Above kHeavyStartAnts a map is also marked heavy, and
// the Map Maker warns that a game on a small device may run slowly (a room refuses such a map first, it is over the cap). The numbers are here and nowhere else.

#include <cstdint>

namespace ants::assets { class LevelData; }

namespace ants::server {

struct OnlineLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;
    /// The server's place for a match (--demo-rooms) is sized for a shipped map, which is up to about 10,000 cells; a match costs the server about as much as its map has cells (one tick of a 256 x 256 map takes
    /// roughly 17 times a tick of the TREASURE map, a 512 x 512 map about 100 times), so a match takes one place for every started 10,000 cells of its map: every shipped map one place, the largest online map seven.
    static constexpr uint32_t kCellsPerPlace = 10000u;

    /// Above this many starting ants (the start markers of the four colors added up: 600 a color on a map of four) a map is heavy: a game on it may run slowly on some devices. Only a warning, for a game on the player's own
    /// device (the Map Maker's Play this map dialog, which keeps a copy of the number, `HEAVY_START_ANTS` in mapmaker_play.js); a single-player game is never refused for its ants. Such a map is over kMaxStartAnts, so a room refuses it.
    static constexpr uint32_t kHeavyStartAnts = 2400;

    /// The most starting ants (the start markers of the four colors added up, as start_ants counts them) of a map that a room plays. A match costs the one thread that serves every room more the more ants it has
    /// (setting the ants up grows with the square of their number, measured natively: 6,000 ants about 50 ms, 10,000 about 0.5 s, 40,000 about 9 s), so a map above this is refused before its match is made. 1,000 is
    /// above every map of the library (at most 248); a bigger map of the players stays on the lists and is played on the player's own device.
    static constexpr uint32_t kMaxStartAnts = 1000;
    /// A match takes one more place for every started kAntsPerPlace starting ants (see weight): 400 ants one place, 1,000 ants three
    static constexpr uint32_t kAntsPerPlace = 400u;

    /// The most pairs of a plant and a waypoint of a map that a room plays (the number the replay viewer sets a recording up with too, include/ants_replay/replay.hpp kMaxEmbeddedPlantWaypointPairs; a test
    /// pins the two equal). Setting a match up looks at every plant that drops flowers for the first waypoint on its cell (SimulationEngine::init), so the work is the plants times the waypoints: a map within
    /// kMaxCells and kMaxStartAnts can still hold far more of both than any real map does (block 1 and block 4 each hold up to 65,535 records on their own), so this is checked on its own. 1,000,000 is 80
    /// times the largest map of the library (112 plants and 114 waypoints); a map above it stays on the lists and is played on the player's own device, as for the other two limits.
    static constexpr uint64_t kMaxPlantWaypointPairs = 1000000;

    static constexpr bool allows(uint64_t cells) noexcept { return cells <= kMaxCells; }
    /// Whether a room plays a map with this many starting ants
    static constexpr bool allows_ants(uint64_t start_ants) noexcept { return start_ants <= kMaxStartAnts; }
    /// Whether a room plays a map with this many pairs of a plant and a waypoint
    static constexpr bool allows_pairs(uint64_t pairs) noexcept { return pairs <= kMaxPlantWaypointPairs; }
    /// Whether a room plays a map of `cells` cells that starts `start_ants` ants and has `pairs` pairs of a plant and a waypoint: every limit holds
    static constexpr bool allows(uint64_t cells, uint64_t start_ants, uint64_t pairs) noexcept { return allows(cells) && allows_ants(start_ants) && allows_pairs(pairs); }
    static constexpr bool heavy(uint64_t start_ants) noexcept { return start_ants > kHeavyStartAnts; }
    /// The start markers of the four colors of a loaded map, added up: counted from the file that the server itself loaded, never from a number that a client sent
    static uint64_t start_ants(const assets::LevelData& level);
    /// The plants of the level (the records of block 1 whose tile has the plant flag, sim::movement::is_dropper_plant_tile: the same predicate SimulationEngine::init tests) times its waypoints
    /// (level.waypoints.size()): the pairs that SimulationEngine::init's lookup looks at when it sets a match up. Counted from the file that the server itself loaded, never from a number that a client sent.
    static uint64_t plant_waypoint_pairs(const assets::LevelData& level);
    /// The places that a match on a map of `cells` cells takes (at least one)
    /// (the most of the places for its cells, one for every started kCellsPerPlace, and the places for its ants, one for every started kAntsPerPlace; the ants default to none)
    static constexpr uint32_t weight(uint64_t cells, uint64_t start_ants = 0) noexcept {
        const uint64_t by_cells = cells <= kCellsPerPlace ? 1u : (cells + kCellsPerPlace - 1) / kCellsPerPlace;
        const uint64_t by_ants = (start_ants + kAntsPerPlace - 1) / kAntsPerPlace;
        return static_cast<uint32_t>(by_cells > by_ants ? by_cells : by_ants);
    }
};

}  // namespace ants::server
