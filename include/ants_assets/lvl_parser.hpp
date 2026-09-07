#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace ants::assets {

constexpr uint32_t LVL_EXPECTED_VERSION = 8;
constexpr uint32_t LVL_GAME_MODE_STANDARD = 1;
constexpr uint16_t LVL_EMPTY_TILE = 0x7FFE; // 32,766 = Empty interactive overlay sentinel

/**
 * @brief 6-byte cell representation as stored in Maps/ *.LVL files.
 */
struct MapCell {
    uint16_t tile_index{LVL_EMPTY_TILE}; // Index into tile dictionary (or 0x7FFE)
    uint16_t flags{0};                   // Word 2 from file
    uint16_t properties{0};              // Word 3 from file

    constexpr bool is_empty() const noexcept { return tile_index == LVL_EMPTY_TILE; }
    constexpr uint16_t word1() const noexcept { return tile_index; }
    constexpr uint16_t word2() const noexcept { return flags; }
    constexpr uint16_t word3() const noexcept { return properties; }
};

/**
 * @brief Anthill starting spawn position (Block 1).
 */
struct AnthillSpawn {
    uint16_t tile_id{0}; // Tile dictionary index: BSTART, USTART, RSTART, GSTART
    uint16_t y{0};       // Grid row coordinate
    uint16_t x{0};       // Grid column coordinate
    uint8_t  team_id{0}; // 0 = Black, 1 = Blue, 2 = Red, 3 = Green, 255 = Other
};

/**
 * @brief Weighted food/item variant inside a respawn pool.
 */
struct FoodItemVariant {
    uint16_t weight{0};  // Relative probability weight
    uint16_t tile_id{LVL_EMPTY_TILE}; // Tile dictionary index (or 0x7FFE for null)
};

/**
 * @brief Dynamic food/item respawn schedule pool (Block 2).
 */
struct FoodSchedule {
    uint16_t y{0};                // Grid row coordinate
    uint16_t x{0};                // Grid column coordinate
    uint16_t initial_delay{0};    // Delay before initial spawn in seconds
    uint16_t respawn_interval{0}; // Respawn interval after collection in seconds
    std::vector<FoodItemVariant> variants;
};

/**
 * @brief 2D coordinate point for patrol paths.
 */
struct WaypointPoint {
    uint32_t px{0};
    uint32_t py{0};
};

/**
 * @brief Waypoint / patrol trigger (Block 4).
 */
struct Waypoint {
    uint16_t y{0};
    uint16_t x{0};
    uint32_t flag{0};
    uint32_t param{0};
    std::array<double, 5> probabilities{}; // 5 IEEE-754 64-bit doubles summing to 1.0
};

/**
 * @brief Complete parsed level structure for a Maps/ *.LVL file.
 */
class LevelData {
public:
    uint32_t version{0};         // Must be 8
    uint32_t game_mode{0};       // 1 = Standard
    uint16_t default_minutes{0}; // Match duration (6, 8, 10, 12)
    std::string description;     // 30-byte ASCII description
    uint16_t tile_type_count{0}; // Tile dictionary count
    std::vector<std::string> tile_dictionary; // 11-byte ASCII tile names
    // Dimension property supporting both .width and .width()
    struct DimensionProp {
        uint32_t val{0};
        constexpr operator uint32_t() const noexcept { return val; }
        constexpr operator uint32_t&() noexcept { return val; }
        constexpr uint32_t operator()() const noexcept { return val; }
        DimensionProp& operator=(uint32_t v) noexcept { val = v; return *this; }
    };

    DimensionProp width{0};           // Grid width (31, 40, 60)
    DimensionProp height{0};          // Grid height (31, 40, 60)

    // Grids (size = width * height)
    struct Layer1TerrainProp : public std::vector<MapCell> {
        const LevelData* parent{nullptr};
        uint16_t operator()(uint32_t x, uint32_t y) const noexcept;
    } layer1_terrain;                         // Base walkable/obstacle/water grid

    struct Layer2InteractiveProp : public std::vector<MapCell> {
        const std::vector<MapCell>& operator()() const noexcept { return *this; }
        std::vector<MapCell>& operator()() noexcept { return *this; }
    } layer2_interactive;                     // Interactive items/food/spawns/bridges

    // Trailing Configuration Blocks supporting both .block and .block()
    struct AnthillSpawnsProp : public std::vector<AnthillSpawn> {
        const std::vector<AnthillSpawn>& operator()() const noexcept { return *this; }
        std::vector<AnthillSpawn>& operator()() noexcept { return *this; }
    } anthill_spawns;                         // Block 1

    struct FoodSchedulesProp : public std::vector<FoodSchedule> {
        const std::vector<FoodSchedule>& operator()() const noexcept { return *this; }
        std::vector<FoodSchedule>& operator()() noexcept { return *this; }
    } food_schedules;                         // Block 2

    uint16_t ambient_flag{0};                 // Block 3
    uint16_t ambient_tile_or_sound{LVL_EMPTY_TILE}; // Block 3
    std::vector<Waypoint> waypoints;          // Block 4
    uint16_t boundary_param{0};               // f_last (final parameter before EOF: per-player egg stock)

    constexpr uint16_t initial_eggs() const noexcept { return boundary_param; }

    LevelData() {
        layer1_terrain.parent = this;
    }
    LevelData(const LevelData& o) : version(o.version), game_mode(o.game_mode),
        default_minutes(o.default_minutes), description(o.description),
        tile_type_count(o.tile_type_count), tile_dictionary(o.tile_dictionary),
        width(o.width), height(o.height), layer1_terrain(o.layer1_terrain),
        layer2_interactive(o.layer2_interactive), anthill_spawns(o.anthill_spawns),
        food_schedules(o.food_schedules), ambient_flag(o.ambient_flag),
        ambient_tile_or_sound(o.ambient_tile_or_sound), waypoints(o.waypoints),
        boundary_param(o.boundary_param) {
        layer1_terrain.parent = this;
    }
    LevelData(LevelData&& o) noexcept : version(o.version), game_mode(o.game_mode),
        default_minutes(o.default_minutes), description(std::move(o.description)),
        tile_type_count(o.tile_type_count), tile_dictionary(std::move(o.tile_dictionary)),
        width(o.width), height(o.height), layer1_terrain(std::move(o.layer1_terrain)),
        layer2_interactive(std::move(o.layer2_interactive)), anthill_spawns(std::move(o.anthill_spawns)),
        food_schedules(std::move(o.food_schedules)), ambient_flag(o.ambient_flag),
        ambient_tile_or_sound(o.ambient_tile_or_sound), waypoints(std::move(o.waypoints)),
        boundary_param(o.boundary_param) {
        layer1_terrain.parent = this;
    }
    LevelData& operator=(const LevelData& o) {
        if (this != &o) {
            version = o.version; game_mode = o.game_mode; default_minutes = o.default_minutes;
            description = o.description; tile_type_count = o.tile_type_count;
            tile_dictionary = o.tile_dictionary; width = o.width; height = o.height;
            layer1_terrain = o.layer1_terrain; layer1_terrain.parent = this;
            layer2_interactive = o.layer2_interactive; anthill_spawns = o.anthill_spawns;
            food_schedules = o.food_schedules; ambient_flag = o.ambient_flag;
            ambient_tile_or_sound = o.ambient_tile_or_sound; waypoints = o.waypoints;
            boundary_param = o.boundary_param;
        }
        return *this;
    }
    LevelData& operator=(LevelData&& o) noexcept {
        if (this != &o) {
            version = o.version; game_mode = o.game_mode; default_minutes = o.default_minutes;
            description = std::move(o.description); tile_type_count = o.tile_type_count;
            tile_dictionary = std::move(o.tile_dictionary); width = o.width; height = o.height;
            layer1_terrain = std::move(o.layer1_terrain); layer1_terrain.parent = this;
            layer2_interactive = std::move(o.layer2_interactive); anthill_spawns = std::move(o.anthill_spawns);
            food_schedules = std::move(o.food_schedules); ambient_flag = o.ambient_flag;
            ambient_tile_or_sound = o.ambient_tile_or_sound; waypoints = std::move(o.waypoints);
            boundary_param = o.boundary_param;
        }
        return *this;
    }

    // --- Interface Contract (PROJECT.md) ---
    // width() -> width.operator()()
    // height() -> height.operator()()
    // layer1_terrain(x, y) -> layer1_terrain.operator()(x, y)
    uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept {
        return get_cell_layer2(x, y).tile_index;
    }
    // anthill_spawns() -> anthill_spawns.operator()()
    // food_schedules() -> food_schedules.operator()()

    // --- Grid Cell & Container Accessors ---
    const MapCell& get_cell_layer1(uint32_t x, uint32_t y) const;
    const MapCell& get_cell_layer2(uint32_t x, uint32_t y) const;
    const std::vector<MapCell>& layer1_cells() const noexcept { return layer1_terrain; }
    std::vector<MapCell>& layer1_cells() noexcept { return layer1_terrain; }
    const std::vector<MapCell>& layer2_cells() const noexcept { return layer2_interactive; }
    std::vector<MapCell>& layer2_cells() noexcept { return layer2_interactive; }

    // Dictionary Lookups
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;

    // Loading Convenience
    bool load_from_file(const std::string& filepath);
    bool load_from_memory(const uint8_t* data, size_t size);
    bool load_lvl(const std::string& filepath) { return load_from_file(filepath); }
};

inline uint16_t LevelData::Layer1TerrainProp::operator()(uint32_t x, uint32_t y) const noexcept {
    return parent ? parent->get_cell_layer1(x, y).tile_index : LVL_EMPTY_TILE;
}

using LVLMap = LevelData;

/**
 * @brief Deserializer for Maps/ *.LVL files.
 */
class LVLParser {
public:
    static bool load_from_file(const std::string& filepath, LevelData& out_level);
    static bool load_from_memory(const uint8_t* data, size_t size, LevelData& out_level);
};

} // namespace ants::assets
