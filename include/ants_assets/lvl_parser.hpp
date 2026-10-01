#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace ants::assets {

constexpr uint32_t LVL_EXPECTED_VERSION = 8;
constexpr uint32_t LVL_GAME_MODE_STANDARD = 1;   // what the shipped maps hold; the original stores the dword (map + 0x74) and never reads it again, so it is no condition
constexpr uint16_t LVL_EMPTY_TILE = 0x7FFE; // 32,766 = Empty interactive overlay sentinel
constexpr uint32_t LVL_MAX_GRID_SIDE = 256;       // rows and columns of a grid the remake hosts (the anchor bytes of a layer-2 object are 8 bit)
constexpr size_t   LVL_MAX_DICTIONARY_ENTRIES = 0x540;   // the original's tile tables (a 0x1500 byte buffer of dwords is filled with one entry per dictionary name)

/**
 * @brief One finding about a map file: what is wrong (or odd), how the original treats it and what the remake does.
 *
 * Severity: Fatal = the map cannot be played (the original refuses it, aborts on it or reads outside its own tables, so there is no behaviour to copy);
 * Warning = the original loads the file and goes on with partial or indeterminate data, the remake plays it with the defined behaviour the text names;
 * Info = a remark that changes nothing.
 */
enum class LevelProblemSeverity : uint8_t { Info, Warning, Fatal };

enum class LevelProblemKind : uint8_t {
    // the original's loader refuses the file or aborts on it
    Unreadable,              // no bytes (cannot be opened or read, no memory)
    WrongVersion,            // header version other than 8: message box "wrong version", no map (0x1006386)
    Truncated,               // the file ends inside the header, the dictionary, the layers or blocks 1 .. 3: CBPException "System read file error." outside the loader's try block
    // limits of the remake where the original does not check and reads or writes outside its tables
    GridEmpty,               // 0 rows or 0 columns
    GridTooLarge,            // more than LVL_MAX_GRID_SIDE rows or columns
    DictionaryTooLarge,      // more than LVL_MAX_DICTIONARY_ENTRIES names
    StartMarkerOutsideGrid,  // a start record of a team lies outside the grid (Fatal when the team plays, Info when it does not)
    // the original loads these files
    ModeIgnored,             // the mode dword is not 1
    FoodBlockEndedEarly,     // a food object without stages ends block 2 where it stands; blocks 3, 4 and the final word are read from there
    WaypointBlockTruncated,  // the file ends inside block 4: the original keeps what it read
    EggStockMissing,         // the file ends before the final word: the egg stock is uninitialised memory in the original
    TileOutsideDictionary,   // tile indexes beyond the dictionary: the original reads its remap table out of bounds (a tile of heap garbage)
    FoodOutsideGrid,         // a food object anchored outside the grid: the original writes beside its tile table
    ObjectOutsideGrid        // plants and waypoints outside the grid: harmless in the original (bounds checked or never looked up)
};

const char* to_string(LevelProblemSeverity severity) noexcept;
const char* to_string(LevelProblemKind kind) noexcept;

struct LevelProblem {
    LevelProblemSeverity severity{LevelProblemSeverity::Info};
    LevelProblemKind kind{LevelProblemKind::Unreadable};
    int8_t team{-1};         // the team a StartMarkerOutsideGrid finding is about (0 green, 1 red, 2 blue, 3 black), -1 otherwise
    uint32_t count{1};       // how many records or cells the finding covers
    std::string message;     // one sentence for a human: what, where, what the original does, what the remake does
};

/**
 * @brief The answer to "is this map playable?": yes or no, and every problem found, so that a server can say why.
 *
 * `parsed` = the original's loader would return 1 for these bytes (it may keep partial data, see the Warning findings).
 * `playable` = parsed and no Fatal finding for the teams of `roster_mask`.
 */
struct LevelValidation {
    bool parsed{false};
    bool playable{false};
    uint8_t roster_mask{0x0F};                 // the teams the answer is about (bit t = team t takes part)
    std::vector<LevelProblem> problems;

    bool has(LevelProblemKind kind) const noexcept;
    bool clean() const noexcept;               // playable and not a single Warning: a file the original reads to its end
    const LevelProblem* first_fatal() const noexcept;
    std::string reason() const;                // empty when playable, else the message of the first Fatal finding
    std::string describe() const;              // every finding, one line each: "fatal|warning|note <kind>: <message>"
};

/**
 * @brief 6-byte cell representation as stored in Maps/ *.LVL files.
 */
struct MapCell {
    uint16_t tile_index{LVL_EMPTY_TILE}; // Index into tile dictionary (or 0x7FFE)
    uint16_t flags{0};                   // Word 2 from file
    uint16_t properties{0};              // Word 3 from file

    constexpr bool is_empty() const noexcept { return tile_index == LVL_EMPTY_TILE; }
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
 * @brief One stage of a food object (Block 2): the pile shows `tile_id` while at most `weight` units are left (Ants.exe
 * StageTile FUN_01009ed3 takes the LAST stage whose threshold is >= the units left); 0x7FFE = the pile is gone.
 * (The field names are those of an earlier reading of the file, which took the entries for respawn pools.)
 */
struct FoodItemVariant {
    uint16_t weight{0};  // Threshold: units left
    uint16_t tile_id{LVL_EMPTY_TILE}; // Tile dictionary index (or 0x7FFE for null)
};

/**
 * @brief Food object (Block 2; Ants.exe 0x1006d19 reads it into FUN_01008ca0): the anchor tile, the number of units, the
 * points each unit is worth and the stage list. Nothing respawns: an object only loses units (docs 5.40).
 */
struct FoodSchedule {
    uint16_t y{0};                // Anchor row
    uint16_t x{0};                // Anchor column
    uint16_t initial_delay{0};    // Number of units of the object
    uint16_t respawn_interval{0}; // Points per unit
    std::vector<FoodItemVariant> variants;
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
    uint32_t game_mode{0};       // 1 in the shipped maps (standard); the original stores it and never reads it again
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
    };

    DimensionProp width{0};           // Grid width = number of columns (the file's SECOND dimension dword; 31, 40, 60 in the shipped maps, 100 in OCEAN.LVL)
    DimensionProp height{0};          // Grid height = number of rows (the file's FIRST dimension dword; 31, 40, 60 in the shipped maps, 81 in OCEAN.LVL)

    // Grids (size = width * height)
    struct Layer1TerrainProp : public std::vector<MapCell> {
        const LevelData* parent{nullptr};
        uint16_t operator()(uint32_t x, uint32_t y) const noexcept;
    } layer1_terrain;                         // Base walkable/obstacle/water grid

    std::vector<MapCell> layer2_interactive;  // Interactive items/food/spawns/bridges

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
    uint16_t boundary_param{0};               // f_last (final parameter before EOF: per-player egg stock; 0 when the file ends before it)
    std::vector<LevelProblem> load_notes;     // what the loader noticed while reading (never Fatal: a file with a Fatal problem does not load); see validate()

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
        boundary_param(o.boundary_param), load_notes(o.load_notes) {
        layer1_terrain.parent = this;
    }
    LevelData(LevelData&& o) noexcept : version(o.version), game_mode(o.game_mode),
        default_minutes(o.default_minutes), description(std::move(o.description)),
        tile_type_count(o.tile_type_count), tile_dictionary(std::move(o.tile_dictionary)),
        width(o.width), height(o.height), layer1_terrain(std::move(o.layer1_terrain)),
        layer2_interactive(std::move(o.layer2_interactive)), anthill_spawns(std::move(o.anthill_spawns)),
        food_schedules(std::move(o.food_schedules)), ambient_flag(o.ambient_flag),
        ambient_tile_or_sound(o.ambient_tile_or_sound), waypoints(std::move(o.waypoints)),
        boundary_param(o.boundary_param), load_notes(std::move(o.load_notes)) {
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
            boundary_param = o.boundary_param; load_notes = o.load_notes;
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
            boundary_param = o.boundary_param; load_notes = std::move(o.load_notes);
        }
        return *this;
    }

    // --- Interface Contract (the accessors that the tests pin) ---
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
    std::vector<MapCell>& layer1_cells() noexcept { return layer1_terrain; }
    std::vector<MapCell>& layer2_cells() noexcept { return layer2_interactive; }

    // Dictionary Lookups
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;

    /// The level as it is played by the teams of `roster_mask` (bit t = team t takes part): a team without a player has no hill, no start marker
    /// (so no starting ant) and no eggs. The original keeps a NULL entry in its team table for such a team (CHECKGO skips it), so nothing of it exists.
    /// Team t is the colour of the hill tile GREENHILL (0), REDHILL (1), BLUEHILL (2), BLACKHILL (3) and of the start markers of the same id.
    LevelData for_roster(uint8_t roster_mask) const;

    /// Is this level playable by the teams of `roster_mask`, and what is odd about it? The loader's own notes (load_notes) plus the checks that need the whole level:
    /// the shape of the grid, tile indexes outside the dictionary, objects outside the grid, and the start markers of the teams that play (the original indexes
    /// its row table with the record's row and column without a check, so a start marker outside the grid is a crash or a heap overwrite there; the remake
    /// refuses the map for a roster that contains that team). A team outside the roster is not looked at: its markers are never used.
    LevelValidation validate(uint8_t roster_mask = 0x0F) const;

    // Loading Convenience. true = the original's loader would read the file to its end (a file that ends inside block 4 or the final word is read as far as it goes,
    // as the original does) and the remake can hold the result; the optional report lists every finding of a file that does not load, and of one that does
    // (for all four teams). Whether a MATCH can be played is validate(roster) / LVLParser::check_*.
    bool load_from_file(const std::string& filepath, LevelValidation* report = nullptr);
    bool load_from_memory(const uint8_t* data, size_t size, LevelValidation* report = nullptr);
    bool load_lvl(const std::string& filepath) { return load_from_file(filepath); }
};

inline uint16_t LevelData::Layer1TerrainProp::operator()(uint32_t x, uint32_t y) const noexcept {
    return parent ? parent->get_cell_layer1(x, y).tile_index : LVL_EMPTY_TILE;
}


/**
 * @brief Deserializer for Maps/ *.LVL files.
 */
class LVLParser {
public:
    static bool load_from_file(const std::string& filepath, LevelData& out_level, LevelValidation* report = nullptr);
    static bool load_from_memory(const uint8_t* data, size_t size, LevelData& out_level, LevelValidation* report = nullptr);

    /// The question of a server: may this file be hosted for the teams of `roster_mask`? One call, a yes or no (`playable`) and the reasons (`problems`).
    static LevelValidation check_file(const std::string& filepath, uint8_t roster_mask = 0x0F);
    static LevelValidation check_memory(const uint8_t* data, size_t size, uint8_t roster_mask = 0x0F);
};

} // namespace ants::assets
