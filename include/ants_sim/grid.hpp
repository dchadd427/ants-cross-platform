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
constexpr uint16_t TILE_BRIDGE4  = 37u;  // Completed bridge (bridge4a)
constexpr uint16_t TILE_BRIDGE4B = 38u;  // Completed bridge (bridge4b)
constexpr uint16_t TILE_LUNCHBOX = 356u; // Dropped food lunchbox (Anim 356, Sprite 513)

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
        int32_t dx = (x >= o.x) ? (x - o.x) : (o.x - x);
        int32_t dy = (y >= o.y) ? (y - o.y) : (o.y - y);
        return (dx > dy) ? dx : dy;
    }

    constexpr int32_t manhattan_dist(const TileCoord& o) const noexcept {
        int32_t dx = (x >= o.x) ? (x - o.x) : (o.x - x);
        int32_t dy = (y >= o.y) ? (y - o.y) : (o.y - y);
        return dx + dy;
    }

    constexpr int32_t euclidean_dist_sq(const TileCoord& o) const noexcept {
        int32_t dx = x - o.x;
        int32_t dy = y - o.y;
        return dx * dx + dy * dy;
    }

    constexpr bool is_cardinal_adjacent(const TileCoord& o) const noexcept {
        int32_t dx = (x >= o.x) ? (x - o.x) : (o.x - x);
        int32_t dy = (y >= o.y) ? (y - o.y) : (o.y - y);
        return (dx + dy == 1);
    }
};

using Vec2i = TileCoord;

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

// Surface Types for terrain-dependent locomotion speeds
enum class SurfaceType : uint8_t {
    Grass  = 0, // Normal turf: 1.00x baseline
    Mud    = 1, // Wet mud: ~0.65x (slower than gravel)
    Gravel = 2, // Dirt / Gravel path: ~1.20x (faster than mud)
    Slate  = 3, // Slate / flagstone: ~1.40x (faster than gravel)
    Water  = 4  // Water surface
};

inline constexpr int32_t get_surface_speed_multiplier_fx(SurfaceType surf) noexcept {
    switch (surf) {
        case SurfaceType::Slate:  return 91750; // 1.40x in 16.16
        case SurfaceType::Gravel: return 78643; // 1.20x in 16.16
        case SurfaceType::Mud:    return 42598; // 0.65x in 16.16
        case SurfaceType::Grass:
        default:                  return 65536; // 1.00x in 16.16
    }
}

/**
 * @brief Single grid cell representation combining Layer 1 terrain and Layer 2 interactive objects.
 */
struct TileCell {
    uint16_t terrain_id{0};                  // Layer 1 terrain dictionary index
    uint8_t  terrain_type{TERRAIN_WALKABLE}; // 0 = Walkable, 1 = Obstacle, 2 = Water
    SurfaceType surface_type{SurfaceType::Grass}; // Terrain surface locomotion classification
    uint16_t flags{0};                       // Layer 1 property flags (0x02 bomb, 0x04 fire)

    uint16_t interactive_id{TILE_EMPTY};     // Layer 2 overlay item (or 0x7FFE)
    uint8_t  interactive_owner{255};         // Player ID owner of bomb/structure (0..3, or 255)
    uint32_t timer_ticks{0};                 // Active ticks remaining for firewall / bridge (180s)
    uint32_t lunchbox_points{0};             // Points carried if lunchbox
    bool     is_food{false};                 // True only if genuine food item
    bool     is_mud{false};                  // True if terrain is mud / dirt path
    bool     is_powerup{false};              // True if cell contains a power-up
    uint8_t  powerup_type{0};                // 1=Bomber, 2=Fire, 3=Thief, 4=Combat, 5=Swimmer
    bool     is_obstacle_overlay{false};     // True if Layer 2 item is a solid obstacle (rock, grass clump, etc.)

    int32_t  occupant_ant_id{-1};            // Ant occupying this cell (-1 = none)

    constexpr bool is_empty_overlay() const noexcept {
        return interactive_id == TILE_EMPTY || interactive_id == 0xFFFF || interactive_id == 0x7FFE;
    }
    constexpr bool has_fire() const noexcept { return interactive_id == TILE_FIREWALL; }
    constexpr bool has_completed_bridge() const noexcept {
        return interactive_id == TILE_BRIDGE4 || interactive_id == TILE_BRIDGE4B;
    }
    constexpr bool has_partial_bridge() const noexcept {
        return interactive_id >= TILE_BRIDGE1 && interactive_id < TILE_BRIDGE4;
    }
    constexpr bool has_bomb() const noexcept {
        return interactive_id >= BOMB_BLACK && interactive_id <= BOMB_GREEN;
    }
    constexpr bool has_food() const noexcept {
        return is_food && !is_empty_overlay() && !has_fire() && !has_completed_bridge() &&
               !has_partial_bridge() && !has_bomb() && !has_lunchbox();
    }
    constexpr bool has_powerup() const noexcept {
        return is_powerup && !is_empty_overlay();
    }
    constexpr bool has_lunchbox() const noexcept { return interactive_id == TILE_LUNCHBOX; }

    constexpr bool can_place_bomb() const noexcept {
        return terrain_type == TERRAIN_WALKABLE && !is_obstacle_overlay && !is_mud && surface_type != SurfaceType::Mud && (flags & FLAG_CAN_PLACE_BOMB) != 0 && is_empty_overlay();
    }
    constexpr bool can_place_fire() const noexcept {
        return terrain_type == TERRAIN_WALKABLE && !is_obstacle_overlay && (flags & FLAG_CAN_PLACE_FIRE) != 0 && is_empty_overlay();
    }

    /**
     * @brief Passability check for pathfinding and locomotion.
     */
    constexpr bool is_passable(bool is_swimmer = false, bool is_fire_ant = false) const noexcept {
        if (terrain_type == TERRAIN_OBSTACLE || is_obstacle_overlay) return false;

        if (terrain_type == TERRAIN_WATER) {
            if (has_completed_bridge()) return true;
            return is_swimmer;
        }

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
    int32_t  remaining_bites{0};
    uint16_t current_tile_id{0};
    std::vector<TileCoord> footprint;
};

/**
 * @brief 32x32 integer tile grid representation for Ants.
 */
class Grid {
public:
    Grid() = default;

    bool init_empty(uint32_t w, uint32_t h) {
        width_ = w;
        height_ = h;
        if (width_ == 0 || height_ == 0) return false;
        cells_.assign(static_cast<size_t>(width_ * height_), TileCell{});
        for (auto& cell : cells_) {
            cell.flags = FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE;
            cell.terrain_type = TERRAIN_WALKABLE;
            cell.surface_type = SurfaceType::Gravel;
        }
        anthills_.clear();
        food_schedules_.clear();
        return true;
    }

    /**
     * @brief Initializes the grid from parsed LevelData.
     */
    bool init_from_level(const ants::assets::LevelData& level);

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

    bool is_solid_obstacle(int32_t x, int32_t y) const noexcept {
        if (!in_bounds(x, y)) return true;
        const auto& cell = get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        return cell.terrain_type == TERRAIN_OBSTACLE || cell.is_obstacle_overlay || cell.terrain_type == TERRAIN_WATER;
    }

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

    const std::vector<ants::assets::AnthillSpawn>& anthills() const noexcept { return anthills_; }
    std::vector<ants::assets::AnthillSpawn>& anthills_mut() noexcept { return anthills_; }

    const std::vector<ActiveFoodSchedule>& food_schedules() const noexcept { return food_schedules_; }
    std::vector<ActiveFoodSchedule>& food_schedules_mut() noexcept { return food_schedules_; }

    const ants::assets::AnthillSpawn* find_anthill(uint8_t team_id) const noexcept {
        for (const auto& a : anthills_) {
            if (a.team_id == team_id) return &a;
        }
        return nullptr;
    }

    void configure_anthill_cells(TileCoord pos);
    bool is_anthill_reserved_spot(TileCoord pos) const noexcept;
    void set_anthill(uint8_t team_id, TileCoord pos);
    void place_firewall(uint32_t x, uint32_t y, uint8_t owner_player) noexcept;
    void clear_firewall(uint32_t x, uint32_t y) noexcept;
    void place_bomb(uint32_t x, uint32_t y, uint8_t team_id) noexcept;
    void clear_bomb(uint32_t x, uint32_t y) noexcept;
    void advance_bridge(uint32_t x, uint32_t y, uint8_t owner_player) noexcept;
    void regress_bridge(uint32_t x, uint32_t y) noexcept;
    void collapse_bridge(uint32_t x, uint32_t y) noexcept;
    void drop_lunchbox(uint32_t x, uint32_t y, uint32_t points = 0) noexcept;
    void clear_lunchbox(uint32_t x, uint32_t y) noexcept;

    bool has_fire_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_fire();
    }

    bool has_bomb_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_bomb();
    }

    bool has_food_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_food();
    }

    bool has_bridge_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        uint16_t id = get_cell(pos).interactive_id;
        return (id >= TILE_BRIDGE1 && id <= TILE_BRIDGE4) || id == TILE_BRIDGE4B;
    }

    bool has_lunchbox_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_lunchbox();
    }

    int get_bridge_stage(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return 0;
        uint16_t id = get_cell(pos).interactive_id;
        if (id >= TILE_BRIDGE1 && id <= TILE_BRIDGE4) {
            return static_cast<int>(id - TILE_BRIDGE1 + 1);
        }
        if (id == TILE_BRIDGE4B) return 4;
        return 0;
    }

    uint32_t get_fire_timer(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return 0;
        const auto& cell = get_cell(pos);
        return cell.has_fire() ? cell.timer_ticks : 0;
    }

    uint32_t get_lunchbox_points(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return 0;
        return get_cell(pos).lunchbox_points;
    }

    void set_fire_at(TileCoord pos, uint32_t timer_ticks) noexcept {
        if (!in_bounds(pos)) return;
        auto& cell = get_cell_mut(pos);
        cell.interactive_id = TILE_FIREWALL;
        cell.timer_ticks = timer_ticks;
    }

    void set_bridge_at(TileCoord pos, int stage, uint32_t timer_ticks) noexcept {
        if (!in_bounds(pos)) return;
        auto& cell = get_cell_mut(pos);
        if (stage >= 1 && stage <= 4) {
            cell.interactive_id = static_cast<uint16_t>(TILE_BRIDGE1 + (stage - 1));
            cell.timer_ticks = timer_ticks;
        }
    }

    void set_terrain(int32_t x, int32_t y, uint8_t terrain_type) noexcept {
        if (!in_bounds(x, y)) return;
        get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).terrain_type = terrain_type;
    }

    void set_tile_flags(int32_t x, int32_t y, uint16_t flags) noexcept {
        if (!in_bounds(x, y)) return;
        get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).flags = flags;
    }

    bool has_powerup_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_powerup();
    }

    uint8_t get_powerup_type(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return 0;
        return get_cell(pos).powerup_type;
    }

    void clear_powerup(int32_t x, int32_t y) noexcept;
    void place_powerup(int32_t x, int32_t y, uint8_t powerup_type) noexcept;

private:
    uint8_t determine_terrain_type(uint16_t tile_index, uint16_t flags) const noexcept;

    uint32_t width_{0};
    uint32_t height_{0};
    std::vector<TileCell> cells_;
    std::vector<ants::assets::AnthillSpawn> anthills_;
    std::vector<ActiveFoodSchedule> food_schedules_;
};

} // namespace ants::sim
