#pragma once

// IslandInfo: the water analysis of one team's map (docs/BOTS.md, "Islands"). Where MapInfo says which tiles the team's ants can walk to and how far food is on foot, this says what lies
// BEYOND water: which walker components exist (islands), what each holds (piles, power-ups, flowers), and the ways over the channels between them that the original's rules allow:
//
//   a bridge   a chain of water tiles that a Swimmer ant digs (special order onto a water tile, three passes each; a finished bridge tile is mud for everybody and collapses 3,600 ticks
//              after it was finished: docs/GAME_REVERSE_ENGINEERING.md 5.3 and 5.37). Channel: the shortest chain between two components.
//   a flight   a Bomber plants a bomb on a tile B next to a shore tile S, an ant that stands on S walks onto the bomb and is thrown four tiles the way it came: onto L = B - 4 * (B - S), which
//              must be a tile the engine lets a flight land on (5.36: only the landing tile is tested, never the tiles in between, never water as a refusal: an ant that lands in water
//              drowns, so a flight is only listed when L is ground that the team can walk on). Flight: every (S, B, L) between two different components.
//
// What is here is the map AT THE START (the start analysis of MapInfo, which it is built from): a bridge that stands later, a bomb, a fire wall or an ant on a tile is read from the grid at
// the moment of use by the tasks (island_tasks.hpp). No threads, no clock, no randomness: the same map gives the same analysis, bit for bit.

#include <cstdint>
#include <vector>

#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai {

class MapInfo;

/// The engine's FUN_0101d762 for a bomb or a fire wall on `tile` (SimulationEngineImpl::valid_ground without the occupant test): grass, sand or dirt (never mud, water or a bridge),
/// nothing on layer 2, not a solid object, not one of a live hill's special tiles (the raid tile, the entrance, the three tiles of the queue row), not inside the 4 x 4 mound of a hill
bool bomb_ground(const sim::Grid& grid, sim::TileCoord tile) noexcept;

/// The engine's FUN_0101d6d6 for a bridge (SimulationEngineImpl::valid_water without the occupant test): a water tile with nothing on layer 2, not a solid object and not one of a live
/// hill's special tiles. (A completed bridge is demolished, never dug again; a swimmer's order onto one is a demolition.)
bool bridge_water(const sim::Grid& grid, sim::TileCoord tile) noexcept;

/// The engine's landing test of a flight (SimulationEngineImpl::landing_ok, FUN_0101d8ed / FUN_0101df5d): inside the map, not solid (a fire wall counts as free), not one of a hill's
/// special tiles, not inside a mound. Water and ants and bombs on the tile are NOT reasons to refuse it (an ant that lands in water drowns)
bool flight_landing_ok(const sim::Grid& grid, sim::TileCoord tile) noexcept;

/// The tile of the flower's droplet: the plant of a flower dropper (the daisy, tile id 421) lets its drops fall one tile below it (docs/GAME_REVERSE_ENGINEERING.md 5.29, 5.62)
inline sim::TileCoord flower_drop_tile(sim::TileCoord plant) noexcept { return sim::TileCoord{plant.x, plant.y + 1}; }
/// The tile id of the daisy (flower1) that all the shipped maps' flower droppers are made of
inline constexpr uint16_t kDaisyTile = 421;

/// One way over water by a bomb (see the top). `from` is the shore tile S the ant stands on, `bomb` the tile B the Bomber plants on (a neighbour of S: the ant's last step onto it is
/// orthogonal or diagonal), `land` the tile L the flight ends on. The ant is thrown opposite its facing, and its facing is the direction S -> B
struct Flight {
    sim::TileCoord from{-1, -1};
    sim::TileCoord bomb{-1, -1};
    sim::TileCoord land{-1, -1};
    int32_t from_comp{-1};
    int32_t to_comp{-1};
    bool diagonal() const noexcept { return from.x != bomb.x && from.y != bomb.y; }
};

/// The shortest bridge between two components: the water tiles of the chain (8-connected, a bridge piece is walkable like mud, the engine has no corner rule), from the shore of `a` to the
/// shore of `b`; its length is what a Swimmer has to dig
struct Channel {
    int32_t a{-1};
    int32_t b{-1};
    std::vector<sim::TileCoord> tiles;
    int32_t length() const noexcept { return static_cast<int32_t>(tiles.size()); }
};

/// What one walker component of the team holds
struct IslandComponent {
    int32_t id{-1};
    int32_t tiles{0};
    sim::TileCoord min{0, 0};            // the bounding box
    sim::TileCoord max{0, 0};
    bool hill{false};                    // the team's hill is in it
    std::vector<uint32_t> piles;         // the piles (MapInfo::piles() index) with a cell next to the component: food that an ant of the component can walk to, hill or no hill
    std::vector<uint32_t> powerups;      // the power-ups (MapInfo::powerups() index) next to the component: an ant of it can step onto them (the first of a group, see TokenGroup)
    std::vector<sim::TileCoord> flowers; // the plants of tile id 421 whose DROP TILE is a tile of the component: the flower droppers of the shipped maps, a person sees the daisy
};

/// Power-ups that stand shoulder to shoulder (8-connected): the ones no walker can reach at all except through the others. The corners of ISLANDS are such rows (Fire, Fire, Swimmer, Swimmer,
/// Swimmer, Fire, Fire along the edge of the map): a pick-up empties its tile and leaves the old type of the ant on a free tile next to it, so one ant that walks a whole row ends as the
/// last type in it with a row of tokens behind it that it cannot walk through. `members` are MapInfo::powerups() indices in the order of a walk from the first entrance; `entrances` are the
/// walkable tiles next to a member that is the first of a walk (component, the tile, the member it leads to)
struct TokenGroup {
    struct Entrance {
        int32_t comp{-1};
        sim::TileCoord tile{-1, -1};
        uint32_t member{0};              // index into `members`
    };
    std::vector<uint32_t> members;
    std::vector<Entrance> entrances;
};

class IslandInfo {
public:
    IslandInfo() = default;
    /// Analyses the map for the ants of `team` (its walker components, with the queue rows of the other hills closed): everything is derived from `map` and its grid. A team without a hill
    /// gets an empty analysis.
    static IslandInfo analyse(const MapInfo& map, const sim::Grid& grid, uint8_t team);

    bool empty() const noexcept { return comps_.empty(); }
    const std::vector<IslandComponent>& components() const noexcept { return comps_; }
    const IslandComponent* component(int32_t id) const noexcept { return id >= 0 && static_cast<size_t>(id) < comps_.size() ? &comps_[static_cast<size_t>(id)] : nullptr; }
    const std::vector<Flight>& flights() const noexcept { return flights_; }
    const std::vector<Channel>& channels() const noexcept { return channels_; }
    const std::vector<TokenGroup>& token_groups() const noexcept { return groups_; }
    /// The shortest bridge between the components (either order), null when no chain of at most kMaxChannel tiles joins them
    const Channel* channel(int32_t a, int32_t b) const noexcept;
    /// The flights that leave component `from` (to any other component) / those from `from` to `to`
    std::vector<const Flight*> flights_from(int32_t from) const;
    std::vector<const Flight*> flights_between(int32_t from, int32_t to) const;
    /// The components in the order of a walk by flights from `from` to `to`: the fewest flights (ties: the lower component number first); `to` is the last, `from` is not in it; empty when
    /// `to` cannot be reached by flights (or equals `from`)
    std::vector<int32_t> flight_path(int32_t from, int32_t to) const;
    /// The least number of flights from `from` to every component (-1: none)
    std::vector<int32_t> flight_distances(int32_t from) const;

    /// A chain of more water tiles than this is not looked for: nobody digs a bridge that long (the map is at most 256 tiles wide, the shipped ones have channels of 1 to 5 tiles between neighbouring
    /// islands and none above 8)
    static constexpr int32_t kMaxChannel = 8;

private:
    std::vector<IslandComponent> comps_;
    std::vector<Flight> flights_;
    std::vector<Channel> channels_;
    std::vector<TokenGroup> groups_;
};

}  // namespace ants::ai
