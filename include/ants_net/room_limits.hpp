#pragma once

// How big a map an ONLINE match may have: the numbers of a room's size limits. They belong to the protocol, because the same numbers are told to the leader who chooses a map that is over one of them
// (kNoticeMapTooBig, kNoticeMapTooManyAnts, kNoticeMapTooManyPairs, kNoticeMapTooManyFood, kNoticeMapTooManyPowerUps in protocol.hpp), and because the Map Maker page warns before a map is sent and keeps a copy of
// each number for it (web/front/mapmaker_play.js; a test keeps the copies equal to these). A room plays a map only up to kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that
// is played online), kMaxStartAnts starting ants (all four colors together), kMaxPlantWaypointPairs pairs of a plant and a waypoint, kMaxFoodObjects food objects and kMaxPowerUps power-ups. A bigger map can be
// opened and edited in the Map Maker and played against bots on the player's own computer or in the browser's test game, but a room refuses it with one sentence. Above kHeavyStartAnts a map is also marked
// heavy, and the Map Maker warns that a game on a small device may run slowly (a room refuses such a map first, it is over the cap).
// The numbers are here and nowhere else: a game server's own limits (how many places a match takes, how a door judges a map) are built on them.

#include <cstdint>

namespace ants::net {

struct RoomLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;

    /// Above this many starting ants (the start markers of the four colors added up: 600 a color on a map of four) a map is heavy: a game on it may run slowly on some devices. Only a warning, for a game on the player's own
    /// device (the Map Maker's Play this map dialog, which keeps a copy of the number, `HEAVY_START_ANTS` in mapmaker_play.js); a single-player game is never refused for its ants. Such a map is over kMaxStartAnts, so a room refuses it.
    static constexpr uint32_t kHeavyStartAnts = 2400;

    /// The most starting ants (the start markers of the four colors added up) of a map that a room plays. A match costs the one thread that serves every room more the more ants it has
    /// (setting the ants up grows with the square of their number, measured natively: 6,000 ants about 50 ms, 10,000 about 0.5 s, 40,000 about 9 s), so a map above this is refused before its match is made. 1,000 is
    /// above every map of the library (at most 248); a bigger map of the players stays on the lists and is played on the player's own device.
    static constexpr uint32_t kMaxStartAnts = 1000;

    /// The most pairs of a plant and a waypoint of a map that a room plays (the Map Maker's Play this map dialog keeps a copy of the number, `MAX_ONLINE_PAIRS` in mapmaker_play.js, and a test pins the two
    /// equal). Setting a match up looks at every plant that drops flowers for the first waypoint on its cell (SimulationEngine::init), so the work is the plants times the waypoints: a map within
    /// kMaxCells and kMaxStartAnts can still hold far more of both than any real map does (block 1 and block 4 each hold up to 65,535 records on their own), so this is checked on its own. 1,000,000 is 78
    /// times the largest map of the library (112 plants and 114 waypoints); a map above it stays on the lists and is played on the player's own device, as for the other limits.
    static constexpr uint64_t kMaxPlantWaypointPairs = 1000000;

    /// The most food objects of a map that a room plays (`MAX_ONLINE_FOOD` in mapmaker_play.js is the Map Maker's copy, pinned equal by a test). The engine finds the object of a food cell by looking at every object
    /// (Grid::food_object_at_cell), and setting a match up and searching paths ask for it once for every food cell and every blocked step, so the work grows with the square of the number of objects (a block 2 holds
    /// up to 65,535 of them whatever the grid's size). 250 is three times the largest map of the library (84).
    static constexpr uint64_t kMaxFoodObjects = 250;

    /// The most power-ups (cells of layer 2 that hold a power-up tile) of a map that a room plays (`MAX_ONLINE_POWERUPS` in mapmaker_play.js is the Map Maker's copy, pinned equal by a test). A bot groups the
    /// power-ups that touch each other when it first looks at a map with more than one walkable part, and plans a walk through a group from every one of its entrances, so what it does grows faster than
    /// the number of power-ups (a layer 2 holds as many as the grid has cells). 1,000 is above every map of the library (at most 896); a map above it stays on the lists and is played on the player's own
    /// device, as for the other limits.
    static constexpr uint64_t kMaxPowerUps = 1000;
};

}  // namespace ants::net
