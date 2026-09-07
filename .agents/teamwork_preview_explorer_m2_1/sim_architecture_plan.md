# Milestone 2 Architectural Blueprint: Simulation Engine, Grid & Public APIs

**Author:** explorer_m2_1 (Simulation Architecture & Public Engine APIs Explorer)  
**Target Milestone:** M2 (`libants-sim`)  
**Working Directory:** `.agents/teamwork_preview_explorer_m2_1/`  
**Date:** 2026-09-06  
**Status:** Approved Architectural Specification  

---

## 1. Executive Summary & Architectural Topology

Milestone 2 establishes `libants-sim`, the deterministic headless simulation engine for the *Microsoft Ants* remake. In accordance with `PROJECT.md` and the project's decoupled 3-tier architecture:
- `libants-sim` has **zero external dependencies** (no SDL2, no DirectX, no OS audio/rendering libraries). It is pure Modern C++17.
- It consumes level geometry, dictionaries, and spawn tables from `libants-assets` (`ants::assets::LevelData`).
- It produces an immutable, complete snapshot of the world (`ants::sim::WorldState`), discrete event streams (`AudioEvent`, `NewsEvent`), and 4-stat scorecards (`PlayerMatchStats`) consumed by `ants-app` and automated test suites.
- All simulation progression is locked to a fixed **20 Hz discrete tick rate (50 ms per tick)** using **pure integer math** (no `float` or `double` arithmetic anywhere in simulation state calculation) to guarantee 100% bitwise determinism across macOS, Linux, and Windows.

```
+-----------------------------------------------------------------------------------------+
|                                        ants-app                                         |
|                 (Renderer, Viewport, HUD, Minimap, AudioMixer, Scorecard)               |
+-----------------------------------------------------------------------------------------+
                    ^                                            |
      get_world_state() / poll_events()           issue_order() / hatch_ant() / tick()
                    |                                            v
+-----------------------------------------------------------------------------------------+
|                                      libants-sim                                        |
|  +-----------------------------------------------------------------------------------+  |
|  |                             SimulationEngine                                      |  |
|  |  - 20 Hz (50 ms) Tick Step Pipeline                                                |  |
|  |  - Match Lifecycle & Expiration Freeze                                             |  |
|  |  - Order Dispatch & Validation                                                    |  |
|  |  - Event Queues (AudioQueue, NewsQueue)                                           |  |
|  +-----------------------------------------------------------------------------------+  |
|         |                        |                         |                      |     |
|         v                        v                         v                      v     |
|  +---------------+      +-----------------+      +-------------------+      +--------+  |
|  |     Grid      |      |   MatchStats    |      |  AntUnitManager   |      |  PRNG  |  |
|  | (32x32 Tiles, |      | (4 Stats/Player,|      | (6 Ant Classes,   |      | (MSVC  |  |
|  |  L1 Terrain,  |      |  Dynamic Teams, |      |  Damage Matrix,   |      |  LCG,  |  |
|  |  L2 Overlays, |      |  Alliances,     |      |  Guard AI,        |      | latseed|  |
|  |  Food Sched,  |      |  Freeze Audio)  |      |  Physics/Knockback|      | bit-det|  |
|  |  180s Timers) |      +-----------------+      +-------------------+      +--------+  |
+-----------------------------------------------------------------------------------------+
                                           ^
                                           | load_lvl()
+-----------------------------------------------------------------------------------------+
|                                      libants-assets                                     |
|                       (LevelData, MapCell, AnthillSpawn, FoodSchedule)                  |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Blueprint 1: `include/ants_sim/prng.hpp`

### 2.1 Technical Analysis & Disassembly Match
Disassembly of `Original-Ants/Ants.exe` (`0x10345b0` & `0x10345c0`) confirms the exact standard Microsoft Visual C++ 32-bit Linear Congruential Generator:
$$\text{holdrand} = \text{holdrand} \times 214013 + 2531011 \pmod{2^{32}}$$
$$\text{sim\_rand}() = (\text{holdrand} \gg 16) \ \& \ \text{0x7FFF} \quad (\text{range } [0 \dots 32767])$$

The C++ design below provides both:
1. An encapsulated, movable, seedable `PRNG` class suitable for instance-specific execution and replay serialization.
2. Direct command-line `latseed:<uint32>` parsing as mandated by Section 1.3 of `survey_sim.md`.

### 2.2 Header Specification (`include/ants_sim/prng.hpp`)

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace ants::sim {

/**
 * @brief Deterministic Linear Congruential Generator matching MSVC CRT rand()/srand().
 * 
 * Verified against Ants.exe disassembly at 0x10345b0 and 0x10345c0:
 *   holdrand = holdrand * 214013 + 2531011;
 *   return (holdrand >> 16) & 0x7FFF;
 */
class PRNG {
public:
    static constexpr uint32_t MULTIPLIER = 214013u;
    static constexpr uint32_t INCREMENT  = 2531011u;
    static constexpr uint32_t MASK_15BIT = 0x7FFFu;
    static constexpr uint16_t RAND_MAX_VAL = 32767u;

    explicit constexpr PRNG(uint32_t seed = 1u) noexcept : state_(seed) {}

    /**
     * @brief Re-seeds the generator state.
     */
    constexpr void srand(uint32_t seed) noexcept {
        state_ = seed;
    }

    /**
     * @brief Advances generator by one iteration and returns a 15-bit pseudo-random integer.
     * @return Integer in range [0, 32767].
     */
    constexpr uint16_t rand() noexcept {
        state_ = state_ * MULTIPLIER + INCREMENT;
        return static_cast<uint16_t>((state_ >> 16) & MASK_15BIT);
    }

    /**
     * @brief Helper to generate an integer in the inclusive range [min_val, max_val].
     * Uses pure integer arithmetic to prevent floating-point divergence.
     */
    constexpr int32_t rand_range(int32_t min_val, int32_t max_val) noexcept {
        if (min_val >= max_val) return min_val;
        uint32_t range = static_cast<uint32_t>(max_val - min_val + 1);
        return min_val + static_cast<int32_t>(rand() % range);
    }

    /**
     * @brief Roll probability check: returns true if roll(1..100) <= percentage.
     */
    constexpr bool roll_chance(uint32_t percentage) noexcept {
        if (percentage == 0) return false;
        if (percentage >= 100) return true;
        return (static_cast<uint32_t>(rand() % 100u) + 1u) <= percentage;
    }

    /**
     * @brief Direct state accessors for serialization and deterministic replay verification.
     */
    constexpr uint32_t get_state() const noexcept { return state_; }
    constexpr void set_state(uint32_t state) noexcept { state_ = state; }

    /**
     * @brief Parses command-line latseed string (e.g. "latseed:12345" or "12345").
     * Returns a PRNG initialized with the parsed seed, or fallback_seed if invalid/empty.
     */
    static PRNG from_latseed(const std::string& latseed_str, uint32_t fallback_seed = 1u) noexcept {
        if (latseed_str.empty()) return PRNG(fallback_seed);
        size_t pos = latseed_str.find("latseed:");
        std::string num_str = (pos != std::string::npos) ? latseed_str.substr(pos + 8) : latseed_str;
        try {
            unsigned long s = std::stoul(num_str);
            return PRNG(static_cast<uint32_t>(s));
        } catch (...) {
            return PRNG(fallback_seed);
        }
    }

private:
    uint32_t state_{1u};
};

} // namespace ants::sim
```

---

## 3. Blueprint 2: `include/ants_sim/match_stats.hpp`

### 3.1 Technical Analysis & Specifications
- **4 Tracked Columns:** Authentic results modal (`re_screen`, Animation 25) displays 4 exact columns per player:
  1. `score`: Net accumulated points (food deposits + stolen - lost).
  2. `friendly_lost`: Friendly ants lost across all causes (combat, bombs, drowning).
  3. `enemy_killed`: Enemy units eliminated by player's ants or mines.
  4. `ants_hatched`: New units hatched from egg inventory.
- **Dynamic In-Game Alliances:**
  - FFA default: each player is independent (`ally_id = ALLIANCE_NONE = 4`).
  - Propose via enemy anthill (`allypro.wav` / Sound 51).
  - Target accepts (`allyyes.wav` / Sound 53, `allyon.wav` / Sound 50) -> alliance established.
  - Target denies (`allynot.wav` / Sound 52) -> remains FFA.
  - Break alliance (`allyoff.wav` / Sound 49) -> uncouples back to FFA.
  - **Memory Discipline:** Individual player statistics remain discrete in memory (`PlayerMatchStats`). Displayed HUD and scoreboard score aggregates team total (`Score[A] + Score[B]`), but dissolving the alliance immediately uncouples back to true personal stats without data loss or corruption.
- **Match Freeze & End-Game Audio Split:**
  - At clock `0:00`, simulation halts immediately.
  - Winning player/team hears **Sound 56 (`winner.wav`)**.
  - Defeated players/teams hear **Sound 41 (`playerout.wav`)**. Losers never hear `winner.wav`.

### 3.2 Header Specification (`include/ants_sim/match_stats.hpp`)

```cpp
#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace ants::sim {

constexpr uint8_t MAX_PLAYERS = 4;
constexpr uint8_t ALLIANCE_NONE = 4; // Value 4 indicates independent FFA faction
constexpr uint8_t PLAYER_NEUTRAL = 255;

constexpr uint32_t HATCH_COST_POINTS = 200; // Cost deducted per egg hatched
constexpr int32_t  MAX_THIEF_STEAL   = 50;  // Maximum points stolen per infiltration

/**
 * @brief 4-stat structure faithfully tracking end-game metrics for re_screen.
 */
struct PlayerMatchStats {
    int32_t  score{0};         // Column 1 (X ~ 496): Net match score
    uint32_t friendly_lost{0}; // Column 2 (X ~ 536): Friendly units killed
    uint32_t enemy_killed{0};  // Column 3 (X ~ 557): Enemy units destroyed
    uint32_t ants_hatched{0};  // Column 4 (X ~ 578): Units hatched from base

    // Diagnostic sub-counters (preserved in memory for balance audits)
    uint32_t food_deposited{0};
    uint32_t food_stolen{0};
    uint32_t food_lost{0};
    uint32_t bombs_planted{0};
    uint32_t bombs_defused{0};
    uint32_t fires_lit{0};
    uint32_t bridges_built{0};

    constexpr bool operator==(const PlayerMatchStats& o) const noexcept {
        return score == o.score && friendly_lost == o.friendly_lost &&
               enemy_killed == o.enemy_killed && ants_hatched == o.ants_hatched;
    }
};

/**
 * @brief Match progression lifecycle state.
 */
enum class MatchState : uint8_t {
    NotStarted = 0,
    Running    = 1,
    Paused     = 2,
    GameOver   = 3
};

/**
 * @brief Alliance invitation status tracking.
 */
struct AllianceInvite {
    uint8_t  from_player{PLAYER_NEUTRAL};
    uint8_t  to_player{PLAYER_NEUTRAL};
    uint32_t expiry_tick{0};
    bool     active{false};
};

/**
 * @brief Complete match outcome structure computed at clock 0:00.
 */
struct MatchResult {
    bool is_over{false};
    bool is_tie{false};
    std::vector<uint8_t> winning_players;
    std::vector<uint8_t> losing_players;
    std::array<int32_t, MAX_PLAYERS> final_scores{};
    std::array<PlayerMatchStats, MAX_PLAYERS> stats{};

    bool is_winner(uint8_t player_id) const noexcept {
        for (uint8_t p : winning_players) {
            if (p == player_id) return true;
        }
        return false;
    }
};

/**
 * @brief Manager class encapsulating 4-player standings, alliances, and economy.
 */
class MatchStatsManager {
public:
    MatchStatsManager() noexcept {
        reset();
    }

    void reset() noexcept {
        for (size_t i = 0; i < MAX_PLAYERS; ++i) {
            stats_[i] = PlayerMatchStats{};
            eggs_[i] = 0;
            alliances_[i] = ALLIANCE_NONE;
            pending_invite_[i] = AllianceInvite{};
        }
    }

    // Individual Player Stats
    const PlayerMatchStats& get_player_stats(uint8_t player_id) const noexcept {
        return stats_[player_id < MAX_PLAYERS ? player_id : 0];
    }
    PlayerMatchStats& get_player_stats_mut(uint8_t player_id) noexcept {
        return stats_[player_id < MAX_PLAYERS ? player_id : 0];
    }

    // Egg Inventory
    uint32_t get_egg_count(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? eggs_[player_id] : 0;
    }
    void set_egg_count(uint8_t player_id, uint32_t count) noexcept {
        if (player_id < MAX_PLAYERS) eggs_[player_id] = count;
    }

    // Scoring (Individual vs Allied Combined)
    int32_t get_individual_score(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? stats_[player_id].score : 0;
    }

    int32_t get_display_score(uint8_t player_id) const noexcept {
        if (player_id >= MAX_PLAYERS) return 0;
        uint8_t ally = alliances_[player_id];
        if (ally < MAX_PLAYERS && ally != player_id) {
            return stats_[player_id].score + stats_[ally].score;
        }
        return stats_[player_id].score;
    }

    void add_score(uint8_t player_id, int32_t points) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score += points;
            if (stats_[player_id].score < 0) stats_[player_id].score = 0;
        }
    }

    void deduct_score(uint8_t player_id, int32_t points) noexcept {
        if (player_id < MAX_PLAYERS) {
            stats_[player_id].score -= points;
            if (stats_[player_id].score < 0) stats_[player_id].score = 0;
        }
    }

    // Alliance Relationships
    uint8_t get_alliance(uint8_t player_id) const noexcept {
        return (player_id < MAX_PLAYERS) ? alliances_[player_id] : ALLIANCE_NONE;
    }

    bool are_allies(uint8_t p1, uint8_t p2) const noexcept {
        if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return false;
        if (p1 == p2) return true;
        return alliances_[p1] == p2 && alliances_[p2] == p1;
    }

    void set_alliance(uint8_t p1, uint8_t p2) noexcept {
        if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS && p1 != p2) {
            alliances_[p1] = p2;
            alliances_[p2] = p1;
        }
    }

    void break_alliance(uint8_t player_id) noexcept {
        if (player_id < MAX_PLAYERS) {
            uint8_t ally = alliances_[player_id];
            alliances_[player_id] = ALLIANCE_NONE;
            if (ally < MAX_PLAYERS) {
                alliances_[ally] = ALLIANCE_NONE;
            }
        }
    }

    // Evaluation at Match Expiry
    MatchResult evaluate_victory() const noexcept {
        MatchResult result{};
        result.is_over = true;
        int32_t best_score = -1;

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            result.stats[i] = stats_[i];
            result.final_scores[i] = get_display_score(i);
            if (result.final_scores[i] > best_score) {
                best_score = result.final_scores[i];
            }
        }

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (result.final_scores[i] == best_score) {
                result.winning_players.push_back(i);
            } else {
                result.losing_players.push_back(i);
            }
        }

        result.is_tie = (result.winning_players.size() > 1);
        return result;
    }

private:
    std::array<PlayerMatchStats, MAX_PLAYERS> stats_{};
    std::array<uint32_t, MAX_PLAYERS> eggs_{};
    std::array<uint8_t, MAX_PLAYERS> alliances_{};
    std::array<AllianceInvite, MAX_PLAYERS> pending_invite_{};
};

} // namespace ants::sim
```

---

## 4. Blueprint 3: `include/ants_sim/grid.hpp`

### 4.1 Technical Analysis & Grid Representation
- **Grid Dimensions & Sizing:**
  Original maps support 31×31 (`SMALL.LVL`, `TINY.LVL`), 40×40 (`MEDIUM.LVL`), and 60×60 (`GAUNTLET.LVL`, `ISLANDS.LVL`, `TREASURE.LVL`).
  Each cell is exactly **32 × 32 pixels**.
- **Pure Integer Coordinate Transformations:**
  - Tile column: `tx = px / 32` (right-shift `px >> 5` for non-negative coords).
  - Tile row: `ty = py / 32` (right-shift `py >> 5` for non-negative coords).
  - Cell bounding box: `[tx * 32, ty * 32]` to `[(tx + 1) * 32 - 1, (ty + 1) * 32 - 1]`.
  - Cell center: `px = tx * 32 + 16`, `py = ty * 32 + 16`.
- **Distance Metrics:**
  - Chebyshev distance (used by Combat Ant Guard scan and Anthill queuing rings):
    $$\text{dist}_{\text{Chebyshev}}(p_1, p_2) = \max(|x_1 - x_2|, |y_1 - y_2|)$$
  - Manhattan distance (used for strict cardinal adjacency checks):
    $$\text{dist}_{\text{Manhattan}}(p_1, p_2) = |x_1 - x_2| + |y_1 - y_2|$$
- **Layer 1 Terrain Categories:**
  - `0`: Walkable Ground (Dirt, Grass, Clay).
  - `1`: Solid Obstacle (Rocks, Boulders, Trees, Cliffs).
  - `2`: Deep Water (instant drowning for non-swimmers unless bridged).
- **Layer 1 Placement Flags:**
  - Bit `0x02`: `CAN_PLACE_BOMB` (87 tile types).
  - Bit `0x04`: `CAN_PLACE_FIRE` (5 tile types, subset of bombable tiles).
- **Layer 2 Interactive Structures & Timers:**
  - `LVL_EMPTY_TILE = 0x7FFE`: Empty slot.
  - Active firewall: Tile 134 (`wallup04`), registered with 180s timer (3,600 ticks).
  - Active bridge: Progressive stages (`bridge1`..`bridge4`), completes at `bridge4` with 180s collapse timer (3,600 ticks).
  - Food schedules: Block 2 respawn schedules with initial delay and respawn timer.
  - Dropped lunchboxes: Tile 356 (`lunchbox`, Sprite 513) dropped on carrier death with universal pickup.

### 4.2 Header Specification (`include/ants_sim/grid.hpp`)

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <cstdlib>

#include "ants_assets/lvl_parser.hpp"

namespace ants::sim {

constexpr int32_t TILE_PIXELS = 32;
constexpr uint16_t TILE_EMPTY = 0x7FFEu;

// Layer 1 Terrain Categories
constexpr uint8_t TERRAIN_WALKABLE = 0;
constexpr uint8_t TERRAIN_OBSTACLE = 1;
constexpr uint8_t TERRAIN_WATER    = 2;

// Layer 1 Tile Property Flag Bits (Disasm 0x10071dd, 0x1007202)
constexpr uint16_t FLAG_CAN_PLACE_BOMB = 0x0002u;
constexpr uint16_t FLAG_CAN_PLACE_FIRE = 0x0004u;

// Layer 2 Key Tile Indices
constexpr uint16_t TILE_FIREWALL = 134u; // wallup04
constexpr uint16_t TILE_BRIDGE1  = 34u;
constexpr uint16_t TILE_BRIDGE2  = 35u;
constexpr uint16_t TILE_BRIDGE3  = 36u;
constexpr uint16_t TILE_BRIDGE4  = 37u;  // Completed bridge
constexpr uint16_t TILE_LUNCHBOX = 356u; // Dropped food lunchbox

// Power-Up Tile IDs (Disasm 0x1021087)
constexpr uint16_t PU_COMBAT  = 62u;
constexpr uint16_t PU_THIEF   = 63u;
constexpr uint16_t PU_BOMBER  = 64u;
constexpr uint16_t PU_SWIMMER = 65u;
constexpr uint16_t PU_FIRE    = 66u;

// Team Bombs
constexpr uint16_t BOMB_BLACK = 100u;
constexpr uint16_t BOMB_BLUE  = 101u;
constexpr uint16_t BOMB_RED   = 102u;
constexpr uint16_t BOMB_GREEN = 103u;

// Structure Lifetime: 180 seconds @ 20 Hz = 3,600 ticks
constexpr uint32_t LIFETIME_180S_TICKS = 3600u;

/**
 * @brief Discrete 2D integer tile coordinate.
 */
struct TileCoord {
    int32_t x{0};
    int32_t y{0};

    constexpr bool operator==(const TileCoord& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool operator!=(const TileCoord& o) const noexcept { return !(*this == o); }

    constexpr int32_t chebyshev_dist(const TileCoord& o) const noexcept {
        return std::max(std::abs(x - o.x), std::abs(y - o.y));
    }

    constexpr int32_t manhattan_dist(const TileCoord& o) const noexcept {
        return std::abs(x - o.x) + std::abs(y - o.y);
    }

    constexpr bool is_cardinal_adjacent(const TileCoord& o) const noexcept {
        int32_t dx = std::abs(x - o.x);
        int32_t dy = std::abs(y - o.y);
        return (dx + dy == 1);
    }
};

/**
 * @brief Discrete 2D integer pixel coordinate in world space.
 */
struct WorldCoord {
    int32_t px{0};
    int32_t py{0};

    constexpr bool operator==(const WorldCoord& o) const noexcept { return px == o.px && py == o.py; }
    constexpr bool operator!=(const WorldCoord& o) const noexcept { return !(*this == o); }

    constexpr TileCoord to_tile() const noexcept {
        return TileCoord{
            (px >= 0) ? (px / TILE_PIXELS) : ((px - (TILE_PIXELS - 1)) / TILE_PIXELS),
            (py >= 0) ? (py / TILE_PIXELS) : ((py - (TILE_PIXELS - 1)) / TILE_PIXELS)
        };
    }
};

/**
 * @brief Single grid cell representation combining Layer 1 terrain and Layer 2 interactive objects.
 */
struct TileCell {
    uint16_t terrain_id{0};               // Layer 1 terrain dictionary index
    uint8_t  terrain_type{TERRAIN_WALKABLE}; // 0 = Walkable, 1 = Obstacle, 2 = Water
    uint16_t flags{0};                    // Layer 1 property flags (0x02 bomb, 0x04 fire)

    uint16_t interactive_id{TILE_EMPTY};  // Layer 2 overlay item (or 0x7FFE)
    uint8_t  interactive_owner{255};      // Player ID owner of bomb/structure (0..3, or 255)
    uint32_t timer_ticks{0};              // Active ticks remaining for firewall / bridge (180s)

    int32_t  occupant_ant_id{-1};         // Ant occupying this cell (-1 = none)

    constexpr bool is_empty_overlay() const noexcept { return interactive_id == TILE_EMPTY; }
    constexpr bool has_fire() const noexcept { return interactive_id == TILE_FIREWALL; }
    constexpr bool has_completed_bridge() const noexcept { return interactive_id == TILE_BRIDGE4; }
    constexpr bool has_partial_bridge() const noexcept {
        return interactive_id >= TILE_BRIDGE1 && interactive_id < TILE_BRIDGE4;
    }
    constexpr bool has_bomb() const noexcept {
        return interactive_id >= BOMB_BLACK && interactive_id <= BOMB_GREEN;
    }
    constexpr bool has_food() const noexcept {
        // Non-empty, not fire, not bridge, not bomb
        return !is_empty_overlay() && !has_fire() && !has_completed_bridge() &&
               !has_partial_bridge() && !has_bomb();
    }
    constexpr bool has_lunchbox() const noexcept { return interactive_id == TILE_LUNCHBOX; }

    constexpr bool can_place_bomb() const noexcept {
        return (flags & FLAG_CAN_PLACE_BOMB) != 0 && is_empty_overlay();
    }
    constexpr bool can_place_fire() const noexcept {
        return (flags & FLAG_CAN_PLACE_FIRE) != 0 && is_empty_overlay();
    }

    /**
     * @brief Passability check for pathfinding and locomotion.
     */
    constexpr bool is_passable(bool is_swimmer, bool is_fire_ant) const noexcept {
        // Solid wall or boulder
        if (terrain_type == TERRAIN_OBSTACLE) return false;

        // Water: passable only if swimmer ant OR completed bridge
        if (terrain_type == TERRAIN_WATER) {
            if (has_completed_bridge()) return true;
            return is_swimmer;
        }

        // Fire: passable only if fire ant
        if (has_fire()) {
            return is_fire_ant;
        }

        return true;
    }
};

/**
 * @brief Dynamic food respawn tracker linked to LevelData Block 2.
 */
struct ActiveFoodSchedule {
    uint16_t x{0};
    uint16_t y{0};
    uint32_t respawn_interval_ticks{0};
    uint32_t countdown_ticks{0};
    bool     active{true};
    std::vector<ants::assets::FoodItemVariant> variants;
};

/**
 * @brief 32x32 integer tile grid representation for Microsoft Ants.
 */
class Grid {
public:
    Grid() = default;

    /**
     * @brief Initializes the grid from parsed LevelData.
     */
    bool init_from_level(const ants::assets::LevelData& level) {
        width_ = level.width;
        height_ = level.height;
        if (width_ == 0 || height_ == 0) return false;

        cells_.resize(static_cast<size_t>(width_ * height_));

        // Populate Layer 1
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const auto& c1 = level.get_cell_layer1(x, y);
                auto& cell = get_cell_mut(x, y);
                cell.terrain_id = c1.tile_index;
                cell.flags = c1.flags;
                cell.terrain_type = determine_terrain_type(c1.tile_index, c1.flags);
                cell.occupant_ant_id = -1;
            }
        }

        // Populate Layer 2
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const auto& c2 = level.get_cell_layer2(x, y);
                auto& cell = get_cell_mut(x, y);
                cell.interactive_id = c2.tile_index;
                cell.interactive_owner = 255;
                cell.timer_ticks = 0;
            }
        }

        // Copy anthill spawns
        anthills_ = level.anthill_spawns;

        // Initialize food respawn schedules
        food_schedules_.clear();
        for (const auto& fs : level.food_schedules) {
            ActiveFoodSchedule afs{};
            afs.x = fs.x;
            afs.y = fs.y;
            afs.respawn_interval_ticks = static_cast<uint32_t>(fs.respawn_interval) * 20u; // 20 Hz
            afs.countdown_ticks = static_cast<uint32_t>(fs.initial_delay) * 20u;
            afs.variants = fs.variants;
            afs.active = true;
            food_schedules_.push_back(afs);
        }

        return true;
    }

    // Grid Dimensions
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    int32_t  pixel_width() const noexcept { return static_cast<int32_t>(width_ * TILE_PIXELS); }
    int32_t  pixel_height() const noexcept { return static_cast<int32_t>(height_ * TILE_PIXELS); }

    bool in_bounds(int32_t x, int32_t y) const noexcept {
        return x >= 0 && static_cast<uint32_t>(x) < width_ &&
               y >= 0 && static_cast<uint32_t>(y) < height_;
    }

    bool in_bounds(const TileCoord& c) const noexcept {
        return in_bounds(c.x, c.y);
    }

    // Cell Accessors
    const TileCell& get_cell(uint32_t x, uint32_t y) const noexcept {
        return cells_[y * width_ + x];
    }
    TileCell& get_cell_mut(uint32_t x, uint32_t y) noexcept {
        return cells_[y * width_ + x];
    }
    const TileCell& get_cell(const TileCoord& c) const noexcept {
        return get_cell(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y));
    }
    TileCell& get_cell_mut(const TileCoord& c) noexcept {
        return get_cell_mut(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y));
    }

    const std::vector<TileCell>& cells() const noexcept { return cells_; }
    std::vector<TileCell>& cells_mut() noexcept { return cells_; }

    // Anthills & Food Schedules
    const std::vector<ants::assets::AnthillSpawn>& anthills() const noexcept { return anthills_; }
    const std::vector<ActiveFoodSchedule>& food_schedules() const noexcept { return food_schedules_; }
    std::vector<ActiveFoodSchedule>& food_schedules_mut() noexcept { return food_schedules_; }

    const ants::assets::AnthillSpawn* find_anthill(uint8_t team_id) const noexcept {
        for (const auto& a : anthills_) {
            if (a.team_id == team_id) return &a;
        }
        return nullptr;
    }

    // Interactive Structure Modifications
    void place_firewall(uint32_t x, uint32_t y, uint8_t owner_player) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = TILE_FIREWALL;
        cell.interactive_owner = owner_player;
        cell.timer_ticks = LIFETIME_180S_TICKS;
    }

    void clear_firewall(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        if (cell.interactive_id == TILE_FIREWALL) {
            cell.interactive_id = TILE_EMPTY;
            cell.timer_ticks = 0;
        }
    }

    void place_bomb(uint32_t x, uint32_t y, uint8_t team_id) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = static_cast<uint16_t>(BOMB_BLACK + (team_id % 4u));
        cell.interactive_owner = team_id;
        cell.timer_ticks = 0;
    }

    void clear_bomb(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        if (cell.has_bomb()) {
            cell.interactive_id = TILE_EMPTY;
            cell.interactive_owner = 255;
        }
    }

    void advance_bridge(uint32_t x, uint32_t y, uint8_t owner_player) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        if (cell.interactive_id < TILE_BRIDGE1 || cell.interactive_id > TILE_BRIDGE4) {
            cell.interactive_id = TILE_BRIDGE1;
        } else if (cell.interactive_id < TILE_BRIDGE4) {
            cell.interactive_id++;
        }
        cell.interactive_owner = owner_player;
        if (cell.interactive_id == TILE_BRIDGE4) {
            cell.timer_ticks = LIFETIME_180S_TICKS;
        }
    }

    void collapse_bridge(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = TILE_EMPTY;
        cell.timer_ticks = 0;
    }

    void drop_lunchbox(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = TILE_LUNCHBOX;
    }

private:
    uint8_t determine_terrain_type(uint16_t tile_index, uint16_t flags) const noexcept {
        // If water flag or designated water tile IDs
        if (tile_index == 2 || (flags & 0x0100u) != 0) {
            return TERRAIN_WATER;
        }
        if (tile_index == 1 || (flags & 0x0001u) != 0) {
            return TERRAIN_OBSTACLE;
        }
        return TERRAIN_WALKABLE;
    }

    uint32_t width_{0};
    uint32_t height_{0};
    std::vector<TileCell> cells_;
    std::vector<ants::assets::AnthillSpawn> anthills_;
    std::vector<ActiveFoodSchedule> food_schedules_;
};

} // namespace ants::sim
```

---

## 5. Blueprint 4: `include/ants_sim/sim_engine.hpp`

### 5.1 Technical Analysis & Requirements
`sim_engine.hpp` defines the master simulation API adhering directly to `PROJECT.md` lines 120–132:
- `init(const LevelData& level, uint32_t random_seed)`
- `tick()` -> discrete advance by 1 tick (50 ms, 20 Hz)
- `issue_order(const AntOrder& order)`
- `hatch_ant(uint8_t player_id, AntType type) -> bool`
- `propose_alliance(uint8_t from_player, uint8_t to_player)`
- `get_world_state() -> const WorldState&`
- `get_match_time_remaining_ms() -> uint32_t`
- `is_match_over() -> bool`
- `get_player_stats(uint8_t player_id) -> PlayerMatchStats`
- `poll_audio_events() -> std::vector<AudioEvent>`
- `poll_news_events() -> std::vector<NewsEvent>`

In addition, it provides:
- Response and break handlers for alliances (`respond_alliance`, `break_alliance`).
- Complete `WorldState` snapshot structures for `ants-app` rendering.
- Sound ID and String ID enumeration constants matching `ants.chd` tables.

### 5.2 Sound IDs & String IDs Mapping

```cpp
// Digital Audio Sound IDs from Table 2 in ants.chd
namespace SoundID {
    constexpr uint32_t BombDetonate   = 4;  // bombexp.wav (22kHz, 1.14s)
    constexpr uint32_t FireBurnout    = 5;  // fireburnout.wav
    constexpr uint32_t PlayerDefeat   = 41; // playerout.wav (11kHz, 0.94s)
    constexpr uint32_t AllianceBreak  = 49; // allyoff.wav
    constexpr uint32_t AllianceOn     = 50; // allyon.wav
    constexpr uint32_t AlliancePro    = 51; // allypro.wav
    constexpr uint32_t AllianceNot    = 52; // allynot.wav
    constexpr uint32_t AllianceYes    = 53; // allyyes.wav
    constexpr uint32_t VictoryFanfare = 56; // winner.wav (22kHz, 4.67s)
    constexpr uint32_t MeleeAttack    = 57; // attack.wav
    constexpr uint32_t BaseAlarmSiren = 58; // underattack.wav (2566 Hz alarm)
    constexpr uint32_t FlingThumpA    = 64; // flythumpa.wav
    constexpr uint32_t FlingThumpB    = 65; // flythumpb.wav
    constexpr uint32_t FireBeam       = 67; // firestarta.wav
    constexpr uint32_t FireErupt      = 68; // firestartb.wav
    constexpr uint32_t FireExtinguish = 69; // fireextinguish.wav
    constexpr uint32_t StunRecover    = 70; // stun.wav
    constexpr uint32_t WaterSplash    = 71; // splash.wav
    constexpr uint32_t AntDrown       = 72; // antdrown.wav
    constexpr uint32_t BombDefuseGrab = 73; // bombdrop.wav
    constexpr uint32_t BombBodySquash = 74; // bombmuffle.wav
    constexpr uint32_t HeavyPunch     = 78; // attack2.wav
    constexpr uint32_t ShovelGravel   = 81; // shovelgravel.wav
    constexpr uint32_t ShovelWater    = 82; // shovelwater.wav
    constexpr uint32_t ThiefCrawl     = 83; // theifwhip.wav
    constexpr uint32_t ThiefDive      = 84; // steala.wav
    constexpr uint32_t ThiefRummage   = 85; // stealb.wav
    constexpr uint32_t ThiefEmerge    = 86; // stealc.wav
    constexpr uint32_t BaseScoreUp    = 87; // scoreup.wav
    constexpr uint32_t BaseScoreDn    = 88; // scoredn.wav
    constexpr uint32_t BombPick       = 90; // bombpick.wav
}

// In-Game String Table IDs from Ants.exe .rsrc
namespace StringID {
    constexpr uint16_t AllianceInvitePrompt = 1;  // "%s (%s) invites you to form a team..."
    constexpr uint16_t AllianceFormedBroadcast = 39; // "%s and %s have formed an alliance!"
    constexpr uint16_t AllianceBrokenBroadcast = 40; // "%s broke their alliance with %s!"
    constexpr uint16_t ThiefAlarmWarning    = 53; // "A ThiefAnt is at your anthill!"
    constexpr uint16_t FoodStolenStatus     = 62; // "Food stolen..."
    constexpr uint16_t AllianceDeclined     = 80; // "%s declined the alliance invitation."
}
```

### 5.3 Header Specification (`include/ants_sim/sim_engine.hpp`)

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/grid.hpp"

namespace ants::sim {

constexpr uint32_t TICK_RATE_HZ = 20u;
constexpr uint32_t TICK_MS      = 50u;

/**
 * @brief Ant unit classes matching original Ants.exe type ID mapping.
 */
enum class AntType : uint8_t {
    Worker  = 0, // General Ant (ag)
    Bomber  = 1, // Bomber Ant (ab)
    Fire    = 2, // Fire Ant / Mason (af)
    Thief   = 3, // Thief Ant (at)
    Combat  = 4, // Combat Ant (ac)
    Swimmer = 5  // Swimmer Ant (as)
};

/**
 * @brief Player and AI action orders.
 */
enum class OrderType : uint8_t {
    None = 0,
    Move,              // Move to (target_x, target_y)
    Attack,            // Melee strike target ant (target_entity_id)
    PlantBomb,         // Bomber: Plant mine on cardinal tile (target_x, target_y)
    DefuseBomb,        // Bomber: Body crush defuse mine (target_x, target_y)
    IgniteFire,        // Fire Ant: Magnifying glass ignition (target_x, target_y)
    ExtinguishFire,    // Fire Ant: Smother firewall (target_x, target_y)
    BuildBridge,       // Swimmer: Shovel water to build bridge (target_x, target_y)
    InfiltrateAnthill, // Thief: Infiltrate enemy base at (target_x, target_y)
    ReturnToBase,      // Return to home anthill to deposit food/heal
    Cancel             // Cancel current order, return to idle
};

/**
 * @brief Concrete command order issued to an ant unit.
 */
struct AntOrder {
    uint32_t  ant_id{0};
    OrderType type{OrderType::None};
    int32_t   target_x{0};          // Tile coordinate X
    int32_t   target_y{0};          // Tile coordinate Y
    int32_t   target_entity_id{-1}; // Target unit or object ID (-1 = none)
};

/**
 * @brief Audio event emitted during simulation for playback by ants-app mixer.
 */
struct AudioEvent {
    uint32_t sound_id{0};        // Sound index from Table 2 in ants.chd (0..90)
    int32_t  world_x{0};         // Sub-tile world pixel X of emitter
    int32_t  world_y{0};         // Sub-tile world pixel Y of emitter
    uint8_t  priority{0};        // 0 = Normal, 1 = Combat/Action, 2 = Alarm/Fanfare
    uint8_t  target_player{255}; // 255 = Broadcast, 0..3 = Specific recipient
};

/**
 * @brief News flash / banner event displayed on HUD.
 */
struct NewsEvent {
    uint8_t     target_player{255}; // 255 = Broadcast, 0..3 = Specific recipient
    std::string message_text;       // Formatted notification text
    uint32_t    timestamp_ms{0};    // Match elapsed milliseconds
    uint16_t    string_id{0};       // Original game string table ID
};

/**
 * @brief Complete snapshot of an ant entity for rendering and HUD display.
 */
struct AntSnapshot {
    uint32_t id{0};
    uint8_t  player_id{0};
    AntType  type{AntType::Worker};

    // Spatial Position
    int32_t px{0};         // World pixel X
    int32_t py{0};         // World pixel Y
    int32_t tile_x{0};     // Tile column X
    int32_t tile_y{0};     // Tile row Y
    uint8_t facing{0};     // Direction (0..7 compass heading)

    // Vitality & Animation
    uint16_t hp{10};       // Current HP (max cap 10)
    uint16_t max_hp{10};   // Cap: 10 HP
    uint16_t anim_state{7};// 7 = Idle, 2 = Walk, 3 = Attack, 4 = Ability, 12 = Stun
    uint16_t anim_frame{0};

    // Inventory & Held Loot
    bool     is_holding{false};
    uint16_t held_item_id{TILE_EMPTY};
    int32_t  carried_points{0};

    // Physical Flags
    bool is_airborne{false};   // In ballistic knockback tumble
    bool is_stunned{false};    // In 12-tick stun recovery
    bool is_swimming{false};   // Swimmer ant traversing water
    bool is_underground{false}; // Inside base shaft or anthill
    bool is_drowning{false};   // In 22-subitem drowning death sequence
};

/**
 * @brief Immutable complete snapshot of the game world at the current tick.
 */
struct WorldState {
    uint64_t   tick_number{0};
    uint32_t   match_time_remaining_ms{0};
    MatchState match_state{MatchState::NotStarted};

    uint32_t width{0};
    uint32_t height{0};

    std::vector<TileCell>    cells;
    std::vector<AntSnapshot> ants;

    std::array<PlayerMatchStats, MAX_PLAYERS> player_stats{};
    std::array<int32_t, MAX_PLAYERS>          player_scores{};
    std::array<uint32_t, MAX_PLAYERS>         player_eggs{};
    std::array<uint8_t, MAX_PLAYERS>          player_alliances{};

    std::vector<ants::assets::AnthillSpawn> anthills;
    MatchResult match_result;
};

// Forward declaration of internal simulation state container
class SimulationEngineImpl;

/**
 * @brief Master deterministic simulation engine for Microsoft Ants remake.
 * 
 * Provides discrete 20 Hz lockstep progression, order dispatch, dynamic alliances,
 * audio/news event queues, and immutable world state inspection.
 */
class SimulationEngine {
public:
    SimulationEngine();
    ~SimulationEngine();

    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;
    SimulationEngine(SimulationEngine&&) noexcept;
    SimulationEngine& operator=(SimulationEngine&&) noexcept;

    // --- Core Interface Contract (PROJECT.md) ---

    /**
     * @brief Initializes engine with level data and deterministic PRNG seed.
     */
    void init(const ants::assets::LevelData& level, uint32_t random_seed);

    /**
     * @brief Discrete advance by 1 simulation tick (50 ms, 20 Hz).
     * If match time reaches 0:00, simulation freezes and end audio dispatches.
     */
    void tick();

    /**
     * @brief Issues a command order to an ant unit.
     */
    void issue_order(const AntOrder& order);

    /**
     * @brief Hatches a new ant of the requested class from player's anthill.
     * Deducts 200 points from player score and decrements egg count.
     * @return true if successfully queued/hatched, false if score < 200 or eggs == 0.
     */
    bool hatch_ant(uint8_t player_id, AntType type);

    /**
     * @brief Initiates an alliance proposal from from_player to to_player.
     */
    void propose_alliance(uint8_t from_player, uint8_t to_player);

    /**
     * @brief Responds to a pending alliance proposal.
     */
    void respond_alliance(uint8_t responding_player, uint8_t proposing_player, bool accept);

    /**
     * @brief Breaks an active alliance, reverting both factions to independent FFA.
     */
    void break_alliance(uint8_t player_id);

    /**
     * @brief Returns an immutable snapshot of the current world state.
     */
    const WorldState& get_world_state() const;

    /**
     * @brief Returns the remaining match clock in milliseconds.
     */
    uint32_t get_match_time_remaining_ms() const;

    /**
     * @brief Returns true if the match has terminated (clock reached 0:00).
     */
    bool is_match_over() const;

    /**
     * @brief Retrieves 4-column match statistics for the specified player.
     */
    PlayerMatchStats get_player_stats(uint8_t player_id) const;

    /**
     * @brief Polls and drains pending audio events emitted during ticks.
     */
    std::vector<AudioEvent> poll_audio_events();

    /**
     * @brief Polls and drains pending news flash events emitted during ticks.
     */
    std::vector<NewsEvent> poll_news_events();

    // --- Extended Convenience & Testing Methods ---

    /**
     * @brief Resets engine back to uninitialized state.
     */
    void reset();

    /**
     * @brief Returns current total tick count executed since initialization.
     */
    uint64_t current_tick() const noexcept;

    /**
     * @brief Directly sets remaining match time in milliseconds (useful for testing clock expiry).
     */
    void set_match_time_remaining_ms(uint32_t ms);

    /**
     * @brief Read-only reference to underlying tile grid.
     */
    const Grid& grid() const;

    /**
     * @brief Read-only reference to PRNG engine.
     */
    const PRNG& prng() const;

    /**
     * @brief Read-only reference to MatchStatsManager.
     */
    const MatchStatsManager& stats_manager() const;

private:
    std::unique_ptr<SimulationEngineImpl> impl_;
};

} // namespace ants::sim
```

---

## 6. Implementation Architecture for `SimulationEngineImpl`

The pimpl implementation class (`src/ants_sim/sim_engine_impl.hpp` and `sim_engine.cpp`) coordinates the tick pipeline:

```cpp
namespace ants::sim {

class SimulationEngineImpl {
public:
    PRNG prng_{1u};
    Grid grid_;
    MatchStatsManager stats_;
    MatchState match_state_{MatchState::NotStarted};

    uint64_t current_tick_{0};
    uint32_t match_time_remaining_ms_{0};

    // Entity lists
    std::vector<AntEntity> ants_;
    uint32_t next_ant_id_{1};

    // Event Queues
    std::vector<AudioEvent> audio_queue_;
    std::vector<NewsEvent>  news_queue_;

    // World snapshot
    mutable WorldState world_state_cache_;
    mutable bool       world_state_dirty_{true};

    // Active field structure timers
    struct StructureTimer {
        uint32_t x;
        uint32_t y;
        uint16_t type; // TILE_FIREWALL or TILE_BRIDGE4
        uint32_t ticks_remaining;
    };
    std::vector<StructureTimer> structure_timers_;

    // Queued Anthill Hatch Tasks
    struct HatchQueueItem {
        uint8_t player_id;
        AntType type;
        uint32_t ticks_remaining;
    };
    std::vector<HatchQueueItem> hatch_queue_;

    // Subsystem Tick Steps
    void tick_match_timer();
    void tick_food_schedules();
    void tick_structure_timers();
    void tick_hatch_queue();
    void tick_combat_guard_ai();
    void tick_ant_movement_and_actions();
    void tick_ballistic_physics_and_ricochets();
    void tick_base_queues_and_healing();
    void handle_game_over();
};

} // namespace ants::sim
```

### 6.1 Order of Execution in `tick()`
To ensure bitwise determinism and eliminate order-of-operation race conditions, `tick()` executes subsystems in this strict order:
1. **Match Clock Step:** If `match_time_remaining_ms_ <= 50`, clamp to 0 and invoke `handle_game_over()`. Freeze all further progression.
2. **Dynamic Timers Step:** Decrement firewall and bridge timers.
   - On firewall expiry: clear Layer 2 to `0x7FFE`, enqueue Sound 5 (`fireburnout.wav`).
   - On bridge expiry: clear Layer 2 to `0x7FFE`. Scan all units on the tile. Any non-swimmer ant drowns instantly (`death_status = 0xF`, HP = 0, Sound 71/72, rising bubble animation). Swimmers survive unharmed.
3. **Food Respawn Step:** Decrement food countdowns; on timeout, roll PRNG variant and spawn food item on Layer 2.
4. **Autonomous AI Step:** Combat Ants in Guard Idle scan 3-tile Chebyshev perimeter, acquire closest live hostile intruder, and commit intercept order.
5. **Entity Order & Movement Step:** Advance pathfinding, steps, and melee strikes. Standard units deal 1 HP (Sound 57); Combat Ants deal 2 HP (Sound 78) and initiate ballistic knockback.
6. **Knockback, Collision & Ricochet Step:** Update parabolic flight. Obstacle impact halts flight and stuns (12 ticks). Water landing drowns non-swimmers. Fire landing applies +1 fire damage, deflects reflection trajectory, and chains multi-fire ricochets without extinguishing fire.
7. **Anthill & Queuing Step:** Chebyshev ring queuing progression. 17-frame entry sequence (Frame 4 deposit score, Frame 8 100% full heal to 10 HP, Frame 16 emergence).
8. **Egg Incubation Step:** Advance hatch timers, emerge new ants from base.
9. **Snapshot Assembly:** If `world_state_dirty_`, assemble immutable `WorldState` snapshot for `ants-app`.

---

## 7. Submodule Collaboration Matrix

`libants-sim` divides cleanly across three modular components:

| Module Header | Primary Explorer | Responsibilities & Invariants |
|---|---|---|
| `sim_engine.hpp`, `grid.hpp`, `prng.hpp`, `match_stats.hpp` | **`explorer_m2_1`** (Us) | Master engine lifecycle, 20 Hz tick loop, tile coordinate math, Layer 1 & 2 grid management, deterministic MSVC PRNG, 4-stat scorecards, dynamic alliances, event queues. |
| `ant_unit.hpp`, `combat_ai.hpp`, `physics.hpp` | **`explorer_m2_2`** | `CAntUnit` entity state machine, 6 ant class definitions, 10 HP universal metric, 3-tile Chebyshev Guard AI, heavy punch 4–5 tile knockback, 12-tick stun recovery, water plunge & 22-subitem drowning sequence. |
| `sim_rules.hpp`, `test_sim_rules.cpp` | **`explorer_m2_3`** | Cardinal placement validation, bomb planting & defusal, fire ignition & pinball ricochet damage, bridge construction & collapse, thief 50-pt stealth steal, test suite execution. |

---

## 8. CMake Configuration Blueprint

### 8.1 Library Definition: `src/ants_sim/CMakeLists.txt`

```cmake
add_library(ants_sim STATIC
    sim_engine.cpp
    grid.cpp
    match_stats.cpp
    ant_unit.cpp
    combat_ai.cpp
    physics.cpp
)

target_include_directories(ants_sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ants_sim PUBLIC
    ants_assets
)

set_target_properties(ants_sim PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_sim"
)
```

### 8.2 Root `CMakeLists.txt` Modifications

```cmake
# Add ants_sim library
add_subdirectory(src/ants_sim)

# Testing
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/test_assets)
    add_subdirectory(tests/test_sim)
endif()
```

### 8.3 Test Target: `tests/test_sim/CMakeLists.txt`

```cmake
add_executable(test_sim_rules
    test_sim_rules.cpp
)

target_link_libraries(test_sim_rules PRIVATE
    ants_sim
    ants_assets
)

target_compile_definitions(test_sim_rules PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_sim_rules COMMAND test_sim_rules)
```

---

## 9. Verification & Acceptance Criteria

To ensure 100% compliance with `ORIGINAL_REQUEST.md`, `PROJECT.md`, and Sentinel directives:

1. **Deterministic PRNG Test:**
   Verify `PRNG(12345)` matches identical 100-number sequence produced by MSVC CRT `rand()` on x86 Windows.
2. **Integer Grid Coordinate Invariants:**
   Verify `(px / 32, py / 32)` maps pixel bounds `[0..31, 0..31]` to `(0, 0)`, and center is `(16, 16)`.
3. **Cardinal Adjacency Verification:**
   Verify `(x, y - 1)`, `(x + 1, y)`, `(x, y + 1)`, `(x - 1, y)` pass, while `(x + 1, y + 1)` and distance > 1 are rejected.
4. **Damage Matrix Verification:**
   Non-combat melee hits deal exactly 1 HP. Combat Ant heavy punch deals exactly 2 HP and triggers 4–5 tile knockback.
5. **Pinball Ricochet & Fire Invariance:**
   Non-fire ant knocked into fire takes 1 damage, bounces off, fire is not extinguished.
6. **180-Second Timers:**
   Firewall and bridge expire at tick 3600 (180,000 ms). Bridge collapse instantly drowns non-swimmers on tile (`death_status = 0xF`); swimmers survive.
7. **Thief Infiltration & Alarm Siren:**
   Thief steals `min(50, score)`. Sound 58 and News String 53 emit exclusively to victim.
8. **Alliance & Scoring Isolation:**
   Combined scores on HUD; individual stats discrete in memory. Decouples cleanly on break.
9. **Match Freeze & Game Over Audio:**
   At 0:00, simulation halts. Winner hears Sound 56; losers hear Sound 41. Losers never hear Sound 56.
