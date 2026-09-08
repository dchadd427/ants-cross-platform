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
        return !is_mud && surface_type != SurfaceType::Mud && (flags & FLAG_CAN_PLACE_BOMB) != 0 && is_empty_overlay();
    }
    constexpr bool can_place_fire() const noexcept {
        return (flags & FLAG_CAN_PLACE_FIRE) != 0 && is_empty_overlay();
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
                cell.lunchbox_points = 0;
                cell.is_mud = false;
                cell.surface_type = SurfaceType::Grass;

                const std::string& l1_name = level.get_tile_name(c1.tile_index);
                if (!l1_name.empty() && l1_name != ".") {
                    char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(l1_name[0])));
                    if (ch == 'w') {
                        cell.terrain_type = TERRAIN_WATER;
                        cell.surface_type = SurfaceType::Water;
                    } else if (ch == 's') {
                        cell.terrain_type = TERRAIN_WALKABLE;
                        cell.surface_type = SurfaceType::Slate;
                    } else if (ch == 'd') {
                        cell.terrain_type = TERRAIN_WALKABLE;
                        cell.surface_type = SurfaceType::Gravel;
                    } else if (ch == 'm') {
                        cell.terrain_type = TERRAIN_WALKABLE;
                        cell.surface_type = SurfaceType::Mud;
                        cell.is_mud = true;
                    } else {
                        cell.terrain_type = TERRAIN_WALKABLE;
                        cell.surface_type = SurfaceType::Grass;
                    }
                }

                if (cell.terrain_type == TERRAIN_WALKABLE) {
                    if (!cell.is_mud && cell.surface_type != SurfaceType::Mud) {
                        cell.flags |= FLAG_CAN_PLACE_BOMB;
                    }
                    cell.flags |= FLAG_CAN_PLACE_FIRE;
                }
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
                cell.is_food = false;
                cell.is_powerup = false;
                cell.powerup_type = 0;
                cell.is_obstacle_overlay = false;

                if (c2.tile_index != TILE_EMPTY && c2.tile_index != 0xFFFF && c2.tile_index != 0x7FFE) {
                    const std::string& tname = level.get_tile_name(c2.tile_index);
                    if (!tname.empty() && tname != ".") {
                        std::string lower_name = tname;
                        for (char& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        if (lower_name.rfind("fd", 0) == 0 || lower_name.rfind("food", 0) == 0) {
                            cell.is_food = true;
                        } else if (lower_name.rfind("pu_", 0) == 0) {
                            cell.is_powerup = true;
                            if (lower_name == "pu_comb") cell.powerup_type = 4; // Combat
                            else if (lower_name == "pu_thief") cell.powerup_type = 3; // Thief
                            else if (lower_name == "pu_bomb") cell.powerup_type = 1; // Bomber
                            else if (lower_name == "pu_swim") cell.powerup_type = 5; // Swimmer
                            else if (lower_name == "pu_mason" || lower_name == "pu_fire") cell.powerup_type = 2; // Fire
                        } else if (lower_name.find("hill") != std::string::npos) {
                            if ((c2.flags & 1) != 0) {
                                cell.terrain_type = TERRAIN_WALKABLE;
                                cell.surface_type = SurfaceType::Gravel;
                                cell.is_obstacle_overlay = false;
                            } else {
                                cell.is_obstacle_overlay = true;
                            }
                        } else if (lower_name.find("start") != std::string::npos) {
                            cell.terrain_type = TERRAIN_WALKABLE;
                            cell.is_obstacle_overlay = false;
                        } else if (lower_name.find("bridge") != std::string::npos ||
                                   (c2.tile_index >= TILE_BRIDGE1 && c2.tile_index <= TILE_BRIDGE4) ||
                                   c2.tile_index == TILE_BRIDGE4B) {
                            cell.is_obstacle_overlay = false;
                        } else {
                            // Solid obstacle overlay: rocks, cans, pencils, grass clusters (grass1..4, grassbig*, grassmed*), toys, flowers, etc.
                            cell.is_obstacle_overlay = true;
                        }
                    } else {
                        cell.is_obstacle_overlay = true;
                    }
                }
            }
        }

        // Identify true 4x4 anthill base origins from Layer 2
        std::array<TileCoord, 4> hill_origins{{{ -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }}};
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const auto& c2 = level.get_cell_layer2(x, y);
                if (c2.tile_index < level.tile_dictionary.size()) {
                    const std::string& tname = level.tile_dictionary[c2.tile_index];
                    int team = -1;
                    if (tname == "GREENHILL" || tname == "greenhill") team = 0;
                    else if (tname == "REDHILL" || tname == "redhill") team = 1;
                    else if (tname == "BLUEHILL" || tname == "bluehill") team = 2;
                    else if (tname == "BLACKHILL" || tname == "blackhill") team = 3;
                    if (team >= 0) {
                        size_t st = static_cast<size_t>(team);
                        if (hill_origins[st].x < 0 || static_cast<int32_t>(x) < hill_origins[st].x) {
                            hill_origins[st].x = static_cast<int32_t>(x);
                        }
                        if (hill_origins[st].y < 0 || static_cast<int32_t>(y) < hill_origins[st].y) {
                            hill_origins[st].y = static_cast<int32_t>(y);
                        }
                    }
                }
            }
        }

        anthills_.clear();
        for (uint8_t t = 0; t < 4; ++t) {
            if (hill_origins[t].x >= 0 && hill_origins[t].y >= 0) {
                ants::assets::AnthillSpawn sp{};
                sp.team_id = t;
                sp.x = static_cast<uint16_t>(hill_origins[t].x);
                sp.y = static_cast<uint16_t>(hill_origins[t].y);
                anthills_.push_back(sp);
                configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)});
            }
        }
        if (anthills_.empty()) {
            anthills_ = level.anthill_spawns;
            for (const auto& sp : anthills_) {
                configure_anthill_cells(TileCoord{static_cast<int32_t>(sp.x), static_cast<int32_t>(sp.y)});
            }
        }

        food_schedules_.clear();
        for (const auto& fs : level.food_schedules) {
            ActiveFoodSchedule afs{};
            afs.x = fs.x;
            afs.y = fs.y;
            afs.respawn_interval_ticks = static_cast<uint32_t>(fs.respawn_interval) * 20u; // 20 Hz
            afs.countdown_ticks = static_cast<uint32_t>(fs.initial_delay) * 20u;
            afs.variants = fs.variants;
            afs.active = true;
            if (!afs.variants.empty()) {
                afs.remaining_bites = static_cast<int32_t>(afs.variants[0].weight);
                afs.current_tile_id = afs.variants[0].tile_id;
            }
            // Populate footprint of all connected cells matching this food item's layer2 tile
            uint16_t anchor_tile = afs.current_tile_id;
            if (anchor_tile != ants::assets::LVL_EMPTY_TILE && anchor_tile != 32766) {
                for (int32_t dy = -4; dy <= 4; ++dy) {
                    for (int32_t dx = -4; dx <= 4; ++dx) {
                        int32_t fx = static_cast<int32_t>(fs.x) + dx;
                        int32_t fy = static_cast<int32_t>(fs.y) + dy;
                        if (in_bounds(fx, fy)) {
                            const auto& c2 = level.get_cell_layer2(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy));
                            if (c2.tile_index == anchor_tile) {
                                afs.footprint.push_back(TileCoord{fx, fy});
                            }
                        }
                    }
                }
            }
            if (afs.footprint.empty()) {
                afs.footprint.push_back(TileCoord{static_cast<int32_t>(fs.x), static_cast<int32_t>(fs.y)});
            }
            food_schedules_.push_back(afs);
        }

        return true;
    }

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

    void configure_anthill_cells(TileCoord pos) {
        // Ensure 4x4 mound cells are obstacles EXCEPT entrance mouth (bx + 1, by) and entrance hole (bx + 1, by + 1)
        for (int dy = 0; dy < 4; ++dy) {
            for (int dx = 0; dx < 4; ++dx) {
                int32_t mx = static_cast<int32_t>(pos.x) + dx;
                int32_t my = static_cast<int32_t>(pos.y) + dy;
                if (in_bounds(mx, my)) {
                    auto& mcell = get_cell_mut(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
                    if (dx == 1 && (dy == 0 || dy == 1)) {
                        mcell.terrain_type = TERRAIN_WALKABLE;
                        mcell.is_obstacle_overlay = false;
                        mcell.flags &= ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);
                    } else {
                        mcell.is_obstacle_overlay = true;
                    }
                }
            }
        }
        // Ensure queuing and entry staging cells along dx = -1 are passable (dy = -1..3)
        for (int dy = -1; dy <= 3; ++dy) {
            int32_t qx = static_cast<int32_t>(pos.x) - 1;
            int32_t qy = static_cast<int32_t>(pos.y) + dy;
            if (in_bounds(qx, qy)) {
                auto& qcell = get_cell_mut(static_cast<uint32_t>(qx), static_cast<uint32_t>(qy));
                qcell.terrain_type = TERRAIN_WALKABLE;
                qcell.is_obstacle_overlay = false;
            }
        }
        // Ensure top approach corridor (bx + dx, by - 1) for dx = 0..2 is passable,
        // but strictly blocked from placing fire or bombs on
        for (int dx = 0; dx <= 2; ++dx) {
            int32_t rx = static_cast<int32_t>(pos.x) + dx;
            int32_t ry = static_cast<int32_t>(pos.y) - 1;
            if (in_bounds(rx, ry)) {
                auto& rcell = get_cell_mut(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
                rcell.terrain_type = TERRAIN_WALKABLE;
                rcell.is_obstacle_overlay = false;
                rcell.flags &= ~(FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);
            }
        }
        // Ensure idle spot (pos.x + 4, pos.y + 4) is passable
        int32_t ix = static_cast<int32_t>(pos.x) + 4;
        int32_t iy = static_cast<int32_t>(pos.y) + 4;
        if (in_bounds(ix, iy)) {
            auto& icell = get_cell_mut(static_cast<uint32_t>(ix), static_cast<uint32_t>(iy));
            icell.terrain_type = TERRAIN_WALKABLE;
            icell.is_obstacle_overlay = false;
        }
    }

    bool is_anthill_reserved_spot(TileCoord pos) const noexcept {
        for (const auto& ah : anthills_) {
            int32_t bx = static_cast<int32_t>(ah.x);
            int32_t by = static_cast<int32_t>(ah.y);
            if (pos.y == by - 1 && pos.x >= bx && pos.x <= bx + 2) return true;
            if (pos.x == bx + 1 && (pos.y == by || pos.y == by + 1)) return true;
        }
        return false;
    }

    void set_anthill(uint8_t team_id, TileCoord pos) {
        for (auto& a : anthills_) {
            if (a.team_id == team_id) {
                a.x = static_cast<uint16_t>(pos.x);
                a.y = static_cast<uint16_t>(pos.y);
                configure_anthill_cells(pos);
                return;
            }
        }
        ants::assets::AnthillSpawn s{};
        s.team_id = team_id;
        s.x = static_cast<uint16_t>(pos.x);
        s.y = static_cast<uint16_t>(pos.y);
        anthills_.push_back(s);
        configure_anthill_cells(pos);
    }

    void place_firewall(uint32_t x, uint32_t y, uint8_t owner_player) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        if (is_anthill_reserved_spot(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)})) return;
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
        if (is_anthill_reserved_spot(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)})) return;
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

    void regress_bridge(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        if (cell.interactive_id == TILE_BRIDGE4 || cell.interactive_id == TILE_BRIDGE4B) {
            cell.interactive_id = TILE_BRIDGE3;
            cell.timer_ticks = 0;
        } else if (cell.interactive_id == TILE_BRIDGE3) {
            cell.interactive_id = TILE_BRIDGE2;
        } else if (cell.interactive_id == TILE_BRIDGE2) {
            cell.interactive_id = TILE_BRIDGE1;
        } else if (cell.interactive_id == TILE_BRIDGE1) {
            cell.interactive_id = TILE_EMPTY;
            cell.timer_ticks = 0;
            cell.interactive_owner = 255;
        }
    }

    void collapse_bridge(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = TILE_EMPTY;
        cell.timer_ticks = 0;
    }

    void drop_lunchbox(uint32_t x, uint32_t y, uint32_t points = 0) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        cell.interactive_id = TILE_LUNCHBOX;
        cell.lunchbox_points = points;
    }

    void clear_lunchbox(uint32_t x, uint32_t y) noexcept {
        if (!in_bounds(static_cast<int32_t>(x), static_cast<int32_t>(y))) return;
        auto& cell = get_cell_mut(x, y);
        if (cell.has_lunchbox()) {
            cell.interactive_id = TILE_EMPTY;
            cell.lunchbox_points = 0;
        }
    }

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

    void clear_powerup(int32_t x, int32_t y) noexcept {
        if (!in_bounds(x, y)) return;
        auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        cell.is_powerup = false;
        cell.powerup_type = 0;
        cell.interactive_id = TILE_EMPTY;
    }

    void place_powerup(int32_t x, int32_t y, uint8_t powerup_type) noexcept {
        if (!in_bounds(x, y)) return;
        auto& cell = get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        cell.is_powerup = true;
        cell.powerup_type = powerup_type;
        cell.interactive_id = 0x8000u | static_cast<uint16_t>(powerup_type);
    }

private:
    uint8_t determine_terrain_type(uint16_t tile_index, uint16_t flags) const noexcept {
        if (tile_index == 2 || (flags & 0x0100u) != 0) {
            return TERRAIN_WATER;
        }
        if (tile_index == 1) {
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
