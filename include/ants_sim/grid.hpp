#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/movement_tables.hpp"

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
constexpr uint16_t TILE_RESERVED = 160u; // 0xa0: invisible, solid placeholder while a bomb or fire wall is being placed

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


};


// Surface Types for terrain-dependent locomotion speeds
// Surface of a tile = its original terrain class (Ants.exe tile-info table, see movement_tables.hpp).
// Walking speed is not a per-surface multiplier: each class has its own walk animations whose frame
// durations and displacements set the pace (grass 4 px / 50 ms, sand 4 px / 40 ms, dirt 4 px / 60 ms,
// mud 2 px / 60 ms).
enum class SurfaceType : uint8_t {
    Grass  = 0, // terrain class 0 (grass)
    Mud    = 1, // terrain class 3 (mud)
    Gravel = 2, // terrain class 4 (dirt)
    Slate  = 3, // terrain class 1 (sand)
    Water  = 4  // terrain class 2 (water)
};

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
    int16_t  anchor_x{-1};                   // Layer-2 object this cell belongs to: its anchor column (-1 = none), Ants.exe cell byte 3
    int16_t  anchor_y{-1};                   // ... and its anchor row (cell byte 2)
    bool     is_food{false};                 // True only if genuine food item
    bool     is_mud{false};                  // True if terrain is mud / dirt path
    bool     is_powerup{false};              // True if cell contains a power-up
    uint8_t  powerup_type{0};                // 1=Bomber, 2=Fire, 3=Thief, 4=Combat, 5=Swimmer
    bool     is_obstacle_overlay{false};     // True if Layer 2 item is a solid obstacle (rock, grass clump, etc.)

    bool     is_thief_only{false};           // Only enemy thief ant can occupy (bottlecap bx + 3, by + 2)
    bool     is_corridor_team_locked{false}; // Top approach corridor locked to base_owner_team (bx + 0..2, by - 1)
    bool     is_base_hole{false};            // Ramp (bx + 1, by + 0) and hole (bx + 1, by + 1)
    uint8_t  base_owner_team{255};           // Team ID (0..3) of anthill owner (255 = none)

    int32_t  occupant_ant_id{-1};            // Ant occupying this cell (-1 = none)

    // Original-engine layer-1 "solid object" bit (Ants.exe map word0 bit0): object footprints from the LVL
    // file, Block-4 entries and row 0, minus removable objects, which are tracked dynamically.
    bool     static_solid{false};

    constexpr bool is_obstacle() const noexcept {
        return terrain_type == TERRAIN_OBSTACLE || is_obstacle_overlay;
    }
    constexpr bool is_empty_overlay() const noexcept {
        return interactive_id == TILE_EMPTY || interactive_id == 0xFFFF || interactive_id == 0x7FFE;
    }
    constexpr bool has_fire() const noexcept { return interactive_id == TILE_FIREWALL; }
    constexpr bool has_completed_bridge() const noexcept {
        return interactive_id == TILE_BRIDGE4 || interactive_id == TILE_BRIDGE4B;
    }
    constexpr bool has_any_bridge() const noexcept {
        return (interactive_id >= TILE_BRIDGE1 && interactive_id <= TILE_BRIDGE4) || interactive_id == TILE_BRIDGE4B;
    }
    constexpr bool has_partial_bridge() const noexcept {
        return interactive_id >= TILE_BRIDGE1 && interactive_id < TILE_BRIDGE4;
    }
    constexpr bool has_bomb() const noexcept {
        return interactive_id >= BOMB_BLACK && interactive_id <= BOMB_GREEN;
    }
    constexpr bool has_food() const noexcept {
        return is_food && !is_empty_overlay() && !has_fire() && !has_any_bridge() &&
               !has_bomb() && !has_lunchbox();
    }
    constexpr bool has_powerup() const noexcept {
        return is_powerup && !is_empty_overlay();
    }
    constexpr bool has_lunchbox() const noexcept { return interactive_id == TILE_LUNCHBOX; }
    constexpr bool has_reserved() const noexcept { return interactive_id == TILE_RESERVED; }

    constexpr bool can_place_bomb() const noexcept {
        return terrain_type == TERRAIN_WALKABLE && !is_obstacle_overlay && !is_mud && surface_type != SurfaceType::Mud && (flags & FLAG_CAN_PLACE_BOMB) != 0 && is_empty_overlay();
    }
    constexpr bool can_place_fire() const noexcept {
        return terrain_type == TERRAIN_WALKABLE && !is_obstacle_overlay && (flags & FLAG_CAN_PLACE_FIRE) != 0 && is_empty_overlay();
    }

    /**
     * @brief Passability check for pathfinding and locomotion.
     */
    constexpr bool is_passable(bool is_swimmer = false,
                               bool is_fire_ant = false,
                               bool is_thief = false,
                               uint8_t ant_team = 255,
                               bool is_entering_or_leaving_base = false) const noexcept {
        // 1. Thief-only bottlecap (bx + 3, by + 2):
        // Exclusively occupiable by enemy thief ant
        if (is_thief_only) {
            return is_thief && ant_team != 255 && base_owner_team != 255 && ant_team != base_owner_team;
        }

        // 2. Base entrance ramp (bx + 1, by + 0) and hole (bx + 1, by + 1):
        // Only occupiable by friendly ant entering or leaving base
        if (is_base_hole) {
            return is_entering_or_leaving_base && (ant_team == 255 || base_owner_team == 255 || ant_team == base_owner_team);
        }

        // 3. Top approach corridor (bx + 0..2, by - 1):
        // (Corridor team lock removed to permit free perimeter movement around base)

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
 * @brief A plant of LVL Block 1 (a flower or a clover): a world object at the centre of its cell (Ants.exe 0x100e3b0 - 0x100e436).
 */
struct MapPlant {
    uint16_t tile_id{0};                   // the animation (tile) id of the plant, which the minimap's colour table is indexed by
    uint16_t x{0};                         // the column of its cell
    uint16_t y{0};                         // the row of its cell
};

/**
 * @brief A food pile or lunchbox (Ants.exe food object, class 0x10020d8, built from a LVL Block 2 entry or by FUN_01008ca0).
 *
 * The file's "delay" is the number of units (bites) of the pile and its "interval" the points every unit is worth (10..50);
 * there is no respawn. The stage list is a set of (threshold, tile) pairs: the pile shows the tile of the LAST pair whose
 * threshold is not below the remaining units (FUN_01009ed3), so the tile changes as bites are taken.
 */
struct FoodObject {
    uint16_t row{0};                       // +0x08: anchor row
    uint16_t col{0};                       // +0x0a: anchor column
    uint16_t units{0};                     // +0x0c: units at the start
    uint16_t value{0};                     // +0x0e: points per unit
    uint16_t remaining{0};                 // +0x12: units left
    std::vector<uint16_t> thresholds;      // +0x14, one per stage
    std::vector<uint16_t> stage_tiles;     // +0x18, 0x7ffe = the pile is gone

    /// FUN_01009ed3 StageTile: 0x7ffe when there is no stage
    uint16_t stage_tile() const noexcept {
        uint16_t tile = 0x7FFEu;
        for (size_t i = 0; i < thresholds.size() && i < stage_tiles.size(); ++i) {
            if (remaining <= thresholds[i]) tile = stage_tiles[i];
        }
        return tile;
    }
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
            cell.surface_type = SurfaceType::Grass; // unlisted tiles are terrain class 0 (grass) in the original
            cell.static_solid = false;
        }
        exact_solid_bits_ = false;
        anthills_.clear();
        plants_.clear();
        food_objects_.clear();
        default_ant_tile_ = TILE_EMPTY;
        return true;
    }

    /**
     * @brief Initializes the grid from parsed LevelData.
     */
    bool init_from_level(const ants::assets::LevelData& level);

    /// True once the solid bits come from an LVL file (init_from_level); part of the state hash
    bool exact_solid_bits() const noexcept { return exact_solid_bits_; }

    /// The level's DEFAULT ANT TYPE (LVL block 3, Ants.exe FUN_01007025 -> level+0x70): the tile id of one of the five power-up tiles (62 Combat, 63 Thief,
    /// 64 Bomber, 65 Swimmer, 66 Fire), or 0x7FFE (none: every shipped map). Every ant whose own type is Worker (0) is of that type wherever the game asks
    /// the ant type getter FUN_0100f9cb(ant, 0): the ants a level starts with, the ants that hatch, and the ant that took a power-up and swapped it away.
    uint16_t default_ant_tile() const noexcept { return default_ant_tile_; }
    /// FUN_01021087(level+0x70): that tile as an AntType number (0 Worker when there is no default, 1 Bomber, 2 Fire, 3 Thief, 4 Combat, 5 Swimmer)
    uint8_t default_ant_type() const noexcept { return movement::ant_type_of_powerup_tile(default_ant_tile_); }
    /// The block-3 store of the original (0x1007048 - 0x100706f): a tile other than 0x7FFE is kept only when it has the power-up flag (the id is the tile index:
    /// the remap is the identity), otherwise the level has no default (0x7FFE). Tests and tools use it to give a level a default type.
    void set_default_ant_tile(uint16_t tile) noexcept {
        default_ant_tile_ = movement::is_powerup_tile(tile) ? tile : TILE_EMPTY;
    }

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }

    bool in_bounds(int32_t x, int32_t y) const noexcept {
        return x >= 0 && static_cast<uint32_t>(x) < width_ &&
               y >= 0 && static_cast<uint32_t>(y) < height_;
    }

    bool in_bounds(const TileCoord& c) const noexcept {
        return in_bounds(c.x, c.y);
    }

    /**
     * @brief Terrain class used for locomotion and path costs (Ants.exe FUN_01008af7): 0 grass, 1 sand,
     * 2 water, 3 mud, 4 dirt. A layer-2 bridge piece (0x22..0x25, any build stage) makes the tile mud.
     *
     * The level loader stores each layer-1 tile's class in the surface fields (exactly the original
     * tile-info table; see movement::terrain_class_of_tile), so terrain edited at run time by tests or
     * tools through those fields is honoured as well.
     */
    uint8_t terrain_class_at(TileCoord t) const noexcept {
        if (!in_bounds(t)) return movement::kTerrainGrass;
        const auto& c = get_cell(t);
        if (movement::is_bridge_tile(c.interactive_id)) return movement::kTerrainMud;
        if (c.terrain_type == TERRAIN_WATER || c.surface_type == SurfaceType::Water) return movement::kTerrainWater;
        if (c.is_mud || c.surface_type == SurfaceType::Mud) return movement::kTerrainMud;
        if (c.surface_type == SurfaceType::Slate) return movement::kTerrainSand;
        if (c.surface_type == SurfaceType::Gravel) return movement::kTerrainDirt;
        return movement::kTerrainGrass;
    }

    /**
     * @brief Original layer-1 "solid object" bit (FUN_0100cf0f): object footprints (LVL cell flag bit0),
     * Block-4 entries, row 0 and the removable objects (food, power-ups, lunchboxes, fire walls).
     * Walls added at run time as TERRAIN_OBSTACLE count too; in worlds without LVL solid bits (test worlds)
     * the remake's obstacle overlays stand in for them. Out-of-bounds tiles count as solid.
     */
    bool is_solid_object(TileCoord t) const noexcept {
        if (!in_bounds(t)) return true;
        const auto& c = get_cell(t);
        if (c.static_solid || c.has_fire() || c.has_lunchbox() || c.has_powerup() || c.has_food() || c.has_reserved()) return true;
        if (c.is_base_hole) return false;
        return c.terrain_type == TERRAIN_OBSTACLE || (!exact_solid_bits_ && c.is_obstacle_overlay);
    }

    bool is_solid_obstacle(int32_t x, int32_t y) const noexcept {
        if (!in_bounds(x, y)) return true;
        const auto& cell = get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        return cell.terrain_type == TERRAIN_OBSTACLE || cell.is_obstacle_overlay ||
               (cell.terrain_type == TERRAIN_WATER && !cell.has_any_bridge());
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

    const std::vector<ants::assets::AnthillSpawn>& anthills() const noexcept { return anthills_; }
    std::vector<ants::assets::AnthillSpawn>& anthills_mut() noexcept { return anthills_; }

    /// The plants of Block 1: the records whose tile id has property bit 0x10 or 0x20 (flowers and clovers), each of which the original's match screen makes a
    /// world object of (0x100e3b0 - 0x100e436), drawn at the centre of its cell. They stay for the whole match.
    const std::vector<MapPlant>& plants() const noexcept { return plants_; }

    // ---- Food objects (Ants.exe FUN_01008c63 / FUN_010076b6 / FUN_01009f06 / FUN_01008ca0 / FUN_0100fdf8) ----
    const std::vector<FoodObject>& food_objects() const noexcept { return food_objects_; }
    /// FUN_01008c63: the FIRST object (table order) whose anchor is `anchor`; -1 when there is none.
    int32_t food_object_first_at(TileCoord anchor) const noexcept;
    /// The object a cell belongs to (TileInfo mask 0x80): the cell's layer-2 tile must be a food tile; its anchor bytes give the
    /// anchor; the LAST object (table order) with that anchor is the answer; -1 when there is none.
    int32_t food_object_at_cell(TileCoord cell) const noexcept;
    /// FUN_01009f06 TakeFood: takes min(units, remaining) units; `stage_changed` tells whether the pile's tile changed;
    /// returns the points of the units taken.
    uint32_t take_food(int32_t object, uint16_t units, bool& stage_changed) noexcept;
    /// FUN_01008ca0 AddFoodObject: appends the object (the table never shrinks) and puts its stage tile on the map.
    int32_t add_food_object(FoodObject object);
    /// SetTile(2, anchor, tile) for a food object (FUN_01007352): the old tile's cells are cleared (FUN_0100744f) and the new
    /// tile's cells are set (FUN_01007a22); the anchor cell always gets the tile.
    void set_food_tile(TileCoord anchor, uint16_t tile) noexcept;

    const ants::assets::AnthillSpawn* find_anthill(uint8_t team_id) const noexcept {
        for (const auto& a : anthills_) {
            if (a.team_id == team_id) return &a;
        }
        return nullptr;
    }

    void configure_anthill_cells(TileCoord pos, uint8_t team_id = 255);
    void clear_anthill_entrance_solid(TileCoord base) noexcept;
    void set_anthill(uint8_t team_id, TileCoord pos);
    void place_firewall(uint32_t x, uint32_t y, uint8_t owner_player) noexcept;
    void clear_firewall(uint32_t x, uint32_t y) noexcept;
    void place_bomb(uint32_t x, uint32_t y, uint8_t team_id) noexcept;
    void clear_bomb(uint32_t x, uint32_t y) noexcept;
    void advance_bridge(uint32_t x, uint32_t y, uint8_t owner_player) noexcept;
    void regress_bridge(uint32_t x, uint32_t y) noexcept;
    void collapse_bridge(uint32_t x, uint32_t y) noexcept;
    /// FUN_01007352 SetTile(layer 2, tile, id) + owner: the raw write the abilities of the original use.
    void set_layer2(uint32_t x, uint32_t y, uint16_t id, uint8_t owner) noexcept;
    /// FUN_0100fdf8: a lunchbox is a food object of one unit worth `points` with the stages {1 -> lunchbox, 0 -> gone}.
    void drop_lunchbox(uint32_t x, uint32_t y, uint32_t points = 0) noexcept;

    bool has_fire_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_fire();
    }

    bool has_bomb_at(TileCoord pos) const noexcept {
        if (!in_bounds(pos)) return false;
        return get_cell(pos).has_bomb();
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

    /// Points of the lunchbox on a tile (its food object's value), 0 without one.
    uint32_t get_lunchbox_points(TileCoord pos) const noexcept {
        if (!in_bounds(pos) || !get_cell(pos).has_lunchbox()) return 0;
        const int32_t o = food_object_at_cell(pos);
        return o >= 0 ? food_objects_[static_cast<size_t>(o)].value : 0u;
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
        auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        cell.terrain_type = terrain_type;
        // Keep the original-engine data consistent with the test/editor helper: water is terrain class 2,
        // a wall is a solid layer-1 object and walkable ground clears the solid bit.
        if (terrain_type == TERRAIN_WATER) {
            cell.surface_type = SurfaceType::Water;
            cell.is_mud = false;
            cell.static_solid = false;
        } else {
            if (cell.surface_type == SurfaceType::Water) cell.surface_type = SurfaceType::Grass;
            cell.static_solid = (terrain_type == TERRAIN_OBSTACLE);
        }
    }

    /// Sets a tile's original terrain class (0 grass, 1 sand, 2 water, 3 mud, 4 dirt) through the remake fields.
    void set_terrain_class(int32_t x, int32_t y, uint8_t terrain_class) noexcept {
        if (!in_bounds(x, y)) return;
        auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        cell.terrain_type = (terrain_class == movement::kTerrainWater) ? TERRAIN_WATER : TERRAIN_WALKABLE;
        cell.surface_type = (terrain_class == movement::kTerrainMud) ? SurfaceType::Mud
                          : (terrain_class == movement::kTerrainSand) ? SurfaceType::Slate
                          : (terrain_class == movement::kTerrainDirt) ? SurfaceType::Gravel
                          : (terrain_class == movement::kTerrainWater) ? SurfaceType::Water : SurfaceType::Grass;
        cell.is_mud = (terrain_class == movement::kTerrainMud);
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
    bool exact_solid_bits_{false};   // true once solid bits come from an LVL file (init_from_level)

    uint32_t width_{0};
    uint32_t height_{0};
    std::vector<TileCell> cells_;
    std::vector<ants::assets::AnthillSpawn> anthills_;
    std::vector<MapPlant> plants_;
    std::vector<FoodObject> food_objects_;
    uint16_t default_ant_tile_{TILE_EMPTY};   // level+0x70 (block 3), 0x7FFE = none
};

} // namespace ants::sim
