#pragma once

// How big a map an ONLINE match may have, and what a big map costs the server (docs/MAP_SUBMISSIONS.md, "The online limits"). Until the server's load with big maps is measured, a room plays a map only up to
// kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that is played online) and only with up to kMaxStartAnts ants at the start; a bigger approved or test map stays on the lists,
// can be opened and edited in the Map Maker and played against bots on the player's own computer or in the browser's test game, but a room refuses it with one sentence (net::kNoticeMapTooBig, too_many_ants_notice).
// The numbers are here and nowhere else.

#include <cstdint>
#include <string>

namespace ants::assets { class LevelData; }

namespace ants::server {

struct OnlineLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;
    /// The server's place for a match (--demo-rooms) is sized for a shipped map, which is up to about 10,000 cells; a match costs the server about as much as its map has cells (one tick of a 256 x 256 map takes
    /// roughly 17 times a tick of the TREASURE map, a 512 x 512 map about 100 times), so a match takes one place for every started 10,000 cells of its map: every shipped map one place, the largest online map seven.
    static constexpr uint32_t kCellsPerPlace = 10000u;

    /// The most ants that a map starts with, all colors together (the sum of its start markers: 75 a color on a map of four): 300. It is low only while the simulation is slow (a tick of a 6,000-ant map takes about
    /// 90 ms against a budget of 50 ms); RAISE IT TO 2,400 (600 a color) when the speed-up series of the simulation has shipped. Single-player games and the editor's test game have no limit.
    static constexpr uint32_t kMaxStartAnts = 300;

    static constexpr bool allows(uint64_t cells) noexcept { return cells <= kMaxCells; }
    static constexpr bool allows_ants(uint64_t start_ants) noexcept { return start_ants <= kMaxStartAnts; }
    /// The start markers of the four colors of a loaded map, added up: what a room starts with, counted from the file that the server itself loaded (never from a number that a client sent)
    static uint64_t start_ants(const assets::LevelData& level);
    /// The one plain sentence for a map that starts with more ants than a room takes (the lobby's notice, the test upload's refusal and the restart record's reason all say it)
    static std::string too_many_ants_notice(uint64_t start_ants);
    /// The places that a match on a map of `cells` cells takes (at least one)
    static constexpr uint32_t weight(uint64_t cells) noexcept { return cells <= kCellsPerPlace ? 1u : static_cast<uint32_t>((cells + kCellsPerPlace - 1) / kCellsPerPlace); }
};

}  // namespace ants::server
