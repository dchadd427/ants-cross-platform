#pragma once

// How big a map an ONLINE match may have, and what a big map costs the server (docs/SERVER.md, "Online size limit"). Until the server's load with big maps is measured, a room plays a map only up to
// kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that is played online); a bigger approved or test map stays on the lists, can be opened and edited in the Map Maker
// and played against bots on the player's own computer or in the browser's test game, but a room refuses it with one sentence (net::kNoticeMapTooBig). The ants of a map refuse nothing: above kHeavyStartAnts a
// map is only marked heavy, and the pages warn that a game may run slowly. The numbers are here and nowhere else.

#include <cstdint>

namespace ants::assets { class LevelData; }

namespace ants::server {

struct OnlineLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;
    /// The server's place for a match (--demo-rooms) is sized for a shipped map, which is up to about 10,000 cells; a match costs the server about as much as its map has cells (one tick of a 256 x 256 map takes
    /// roughly 17 times a tick of the TREASURE map, a 512 x 512 map about 100 times), so a match takes one place for every started 10,000 cells of its map: every shipped map one place, the largest online map seven.
    static constexpr uint32_t kCellsPerPlace = 10000u;

    /// Above this many starting ants (the start markers of the four colors added up: 75 a color on a map of four) a map is heavy: a game on it may run slowly on some devices. Only a warning (the map picker's row and the
    /// Map Maker's Play this map dialog, which keeps a copy of the number, `HEAVY_START_ANTS` in mapmaker_play.js); no map is refused for its ants, and single-player games, test rooms and public rooms are as they were.
    static constexpr uint32_t kHeavyStartAnts = 300;

    static constexpr bool allows(uint64_t cells) noexcept { return cells <= kMaxCells; }
    static constexpr bool heavy(uint64_t start_ants) noexcept { return start_ants > kHeavyStartAnts; }
    /// The start markers of the four colors of a loaded map, added up: counted from the file that the server itself loaded, never from a number that a client sent
    static uint64_t start_ants(const assets::LevelData& level);
    /// The places that a match on a map of `cells` cells takes (at least one)
    static constexpr uint32_t weight(uint64_t cells) noexcept { return cells <= kCellsPerPlace ? 1u : static_cast<uint32_t>((cells + kCellsPerPlace - 1) / kCellsPerPlace); }
};

}  // namespace ants::server
