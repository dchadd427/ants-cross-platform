#pragma once

// How big a map an ONLINE match may have, and what a big map costs the server (docs/SERVER.md, "Online size limit"). Until the server's load with big maps is measured, a room plays a map only up to
// kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that is played online); a bigger approved or test map stays on the lists, can be opened and edited in the Map Maker
// and played against bots on the player's own computer or in the browser's test game, but a room refuses it with one sentence (net::kNoticeMapTooBig). The numbers are here and nowhere else.

#include <cstdint>

namespace ants::server {

struct OnlineLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;
    /// The server's place for a match (--demo-rooms) is sized for a shipped map, which is up to about 10,000 cells; a match costs the server about as much as its map has cells (one tick of a 256 x 256 map takes
    /// roughly 17 times a tick of the TREASURE map, a 512 x 512 map about 100 times), so a match takes one place for every started 10,000 cells of its map: every shipped map one place, the largest online map seven.
    static constexpr uint32_t kCellsPerPlace = 10000u;

    static constexpr bool allows(uint64_t cells) noexcept { return cells <= kMaxCells; }
    /// The places that a match on a map of `cells` cells takes (at least one)
    static constexpr uint32_t weight(uint64_t cells) noexcept { return cells <= kCellsPerPlace ? 1u : static_cast<uint32_t>((cells + kCellsPerPlace - 1) / kCellsPerPlace); }
};

}  // namespace ants::server
