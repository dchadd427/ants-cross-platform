#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>
#include <array>

namespace ants::assets {

constexpr uint32_t LVL_EXPECTED_VERSION = 8;      // the Classic map: the only version the original reads
constexpr uint32_t LVL_EXTENDED_VERSION = 9;      // the Extended map: this remake only (docs/MAP_FORMAT.md, "Version 9")
constexpr uint32_t LVL_GAME_MODE_STANDARD = 1;   // what the shipped maps hold; the original stores the dword (map + 0x74) and never reads it again, so it is no condition
constexpr uint16_t LVL_EMPTY_TILE = 0x7FFE; // 32,766 = Empty interactive overlay sentinel
constexpr uint32_t LVL_MAX_GRID_SIDE = 256;       // rows and columns of a grid the remake hosts (the anchor bytes of a layer-2 object are 8 bit)
// What no LevelLimits can exceed once a place accepts Extended maps (LevelLimits::allow_extended): the hard ceiling of the version 9 format. A reader refuses a header that
// announces more before it allocates anything, and a file of more bytes before it reads one.
constexpr uint32_t LVL_FORMAT_CEILING_SIDE = 2048;
constexpr uint32_t LVL_FORMAT_CEILING_CELLS = LVL_FORMAT_CEILING_SIDE * LVL_FORMAT_CEILING_SIDE;   // 4,194,304
constexpr size_t   LVL_FORMAT_CEILING_FILE_BYTES = 64u * 1024u * 1024u;
constexpr size_t   LVL_EXTENDED_MAX_DESCRIPTION = 255;       // bytes of a version 9 description (the word before it holds the length)
constexpr size_t   LVL_CLASSIC_MAX_DESCRIPTION = 29;         // characters of a version 8 description that the original can end with a zero byte (the field holds 30 bytes)
constexpr uint32_t LVL_KNOWN_FORMAT_FLAGS = 0;               // the `format flags` bits this reader knows (none yet): a file with any other bit set is refused
constexpr uint16_t LVL_RULES_ORIGINAL = 0;                   // `rules` 0: the original's rules exactly
constexpr uint16_t LVL_RULES_EXTENDED = 1;                   // `rules` 1: extended rules (the simulation does not read the word yet)
constexpr uint16_t LVL_BOMB_TILE_FIRST = 129;                // the original's bomb tiles 129 .. 132: the third word of their layer-2 cell is the owner, not an anchor
constexpr uint16_t LVL_BOMB_TILE_LAST = 132;
// Hills and start markers are TILE IDS, never dictionary names (Ants.exe FUN_0100ecac, the table at 0x1002468): the color ids are 0 black, 1 blue, 2 red, 3 green, the hill of a color is
// the layer-2 id 245 + color (245 black, 246 blue, 247 red, 248 green) and its start marker the block-1 id 152 + color (152 black, 153 blue, 154 green, 155 red). The remake numbers its teams
// 0 green, 1 red, 2 blue, 3 black. Both functions answer -1 for any other id.
constexpr uint16_t LVL_HILL_FIRST = 245;
constexpr uint16_t LVL_HILL_LAST = 248;
constexpr uint16_t LVL_START_MARKER_FIRST = 152;
constexpr uint16_t LVL_START_MARKER_LAST = 155;
constexpr int hill_team_of_id(uint16_t id) noexcept {
    return id == 245 ? 3 : id == 246 ? 2 : id == 247 ? 1 : id == 248 ? 0 : -1;
}
constexpr int start_marker_team_of_id(uint16_t id) noexcept {
    return id == 152 ? 3 : id == 153 ? 2 : id == 154 ? 0 : id == 155 ? 1 : -1;
}
constexpr size_t   LVL_MAX_DICTIONARY_ENTRIES = 0x540;   // the original's tile tables (a 0x1500 byte buffer of dwords is filled with one entry per dictionary name)
// The tile ids whose pictures the original loads whatever the dictionary calls them (FUN_010068be, the 62 words at 0x1001ba8): the bridges, effects, power-ups,
// bombs, the fire wall, the placeholder, 162 .. 178, the hills, the lunchbox, the plants and the droplets. Every other id is loaded only when its dictionary entry
// is not "." (FUN_0100674e mode 0); the grid (ants_sim) and the original-safe profile (lvl_checker.hpp) read this one table.
constexpr uint16_t LVL_ALWAYS_LOADED_TILES[62] = {34,  35,  36,  37,  40,  53,  62,  63,  64,  65,  66,  129, 130, 131, 132, 133, 134, 135, 160, 162, 163,
                                                  164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 230, 245, 246, 247, 248, 356,
                                                  404, 405, 406, 407, 408, 410, 411, 412, 413, 414, 415, 416, 420, 421, 422, 423, 424, 425, 426, 1177};
constexpr bool tile_always_loaded(uint16_t id) noexcept {
    for (const uint16_t t : LVL_ALWAYS_LOADED_TILES) {
        if (t == id) return true;
    }
    return false;
}

/**
 * @brief The limits that a reader holds a file to and the writer (lvl_writer.hpp) a level to. The defaults are what the game reads: the original loader's tables
 * (LVL_MAX_DICTIONARY_ENTRIES names), the grid the remake hosts (LVL_MAX_GRID_SIDE a side) and no limit on the size of a file, so that they refuse no map of the
 * library. A limit can only be made tighter: a value above a default counts as the default. A place that takes a map from a stranger or makes a new one passes
 * for_new_maps() (docs/MAP_FORMAT.md, "Limits").
 *
 * A place reads and writes Classic maps (version 8) only until it sets `allow_extended`: a version 9 file is then refused with the finding version_not_supported
 * (and a version 9 level is not written). With it the grid may be as large as the hard ceiling of the format (LVL_FORMAT_CEILING_*) and no more, whatever the
 * values below say; a version 8 file or level keeps the 256 a side of its format either way.
 */
struct LevelLimits {
    uint32_t max_rows{LVL_MAX_GRID_SIDE};
    uint32_t max_columns{LVL_MAX_GRID_SIDE};
    size_t max_dictionary_names{LVL_MAX_DICTIONARY_ENTRIES};
    size_t max_read_bytes{std::numeric_limits<size_t>::max()};        // a file as it is, before it is read (the reader)
    uint32_t max_cells{LVL_MAX_GRID_SIDE * LVL_MAX_GRID_SIDE};       // rows x columns
    size_t max_write_bytes{std::numeric_limits<size_t>::max()};       // the canonical file of a level (the writer)
    bool allow_extended{false};                                       // may a version 9 file or level pass (an Extended map)? false for LevelLimits{} and for_new_maps()
    size_t max_description_bytes{LVL_EXTENDED_MAX_DESCRIPTION};       // the description of a version 9 level (a version 8 description is the file's 30 bytes)

    /// A map that is made or brought in now (the map maker, a map brought into one game, a map submitted for the library): at most 10,000 cells and 130 a side,
    /// so that every map of the library fits (Bomb Races has 129 columns); a file of at most 512 KB is read as it is (every file of the library: BOMBERWORLD's
    /// 235,594 bytes are mostly bytes after its egg word, which a rewrite drops), and its canonical file may have 144 KB, so that a 100 x 100 map with the largest
    /// dictionary fits with room for its blocks
    static constexpr LevelLimits for_new_maps() noexcept { return LevelLimits{130, 130, LVL_MAX_DICTIONARY_ENTRIES, 512u * 1024u, 10000, 144u * 1024u, false, LVL_EXTENDED_MAX_DESCRIPTION}; }

    /// An Extended map (version 9) that is made or brought in now: at most 262,144 cells (512 x 512, any shape) and 1,024 a side, a file of at most 4 MiB is read and
    /// its canonical file may have 3.5 MiB (a 512 x 512 map is 3.1 MiB), the dictionary of 1,344 names, a description of 255 bytes. A Classic map (version 8) is
    /// still held to 256 a side. Never above the hard ceiling of the format (LVL_FORMAT_CEILING_*).
    static constexpr LevelLimits for_extended_maps() noexcept {
        return LevelLimits{1024, 1024, LVL_MAX_DICTIONARY_ENTRIES, 4u * 1024u * 1024u, 262144, 3584u * 1024u, true, LVL_EXTENDED_MAX_DESCRIPTION};
    }

    /// Everything that the version 9 format holds and nothing a place adds (the hard ceiling: 2,048 a side, 4,194,304 cells, a file of 64 MiB): for a tool that reads or converts
    /// a map and for the tests. A place that takes maps from other people uses for_extended_maps().
    static constexpr LevelLimits format_ceiling() noexcept {
        return LevelLimits{LVL_FORMAT_CEILING_SIDE, LVL_FORMAT_CEILING_SIDE, LVL_MAX_DICTIONARY_ENTRIES, LVL_FORMAT_CEILING_FILE_BYTES, LVL_FORMAT_CEILING_CELLS,
                           LVL_FORMAT_CEILING_FILE_BYTES, true, LVL_EXTENDED_MAX_DESCRIPTION};
    }

    // the limits as they are applied (a value above the default is the default; with allow_extended the default is the hard ceiling of the format)
    constexpr uint32_t side_cap() const noexcept { return allow_extended ? LVL_FORMAT_CEILING_SIDE : LVL_MAX_GRID_SIDE; }
    constexpr uint32_t rows() const noexcept { return max_rows < side_cap() ? max_rows : side_cap(); }
    constexpr uint32_t columns() const noexcept { return max_columns < side_cap() ? max_columns : side_cap(); }
    constexpr uint32_t cells() const noexcept {
        const uint32_t cap = allow_extended ? LVL_FORMAT_CEILING_CELLS : LVL_MAX_GRID_SIDE * LVL_MAX_GRID_SIDE;
        return max_cells < cap ? max_cells : cap;
    }
    constexpr size_t dictionary_names() const noexcept { return max_dictionary_names < LVL_MAX_DICTIONARY_ENTRIES ? max_dictionary_names : LVL_MAX_DICTIONARY_ENTRIES; }
    constexpr size_t read_bytes() const noexcept { return allow_extended && max_read_bytes > LVL_FORMAT_CEILING_FILE_BYTES ? LVL_FORMAT_CEILING_FILE_BYTES : max_read_bytes; }
    constexpr size_t write_bytes() const noexcept { return allow_extended && max_write_bytes > LVL_FORMAT_CEILING_FILE_BYTES ? LVL_FORMAT_CEILING_FILE_BYTES : max_write_bytes; }
    constexpr size_t description_bytes() const noexcept { return max_description_bytes < LVL_EXTENDED_MAX_DESCRIPTION ? max_description_bytes : LVL_EXTENDED_MAX_DESCRIPTION; }
    // the same for a level of `version`: a Classic map (anything but 9) keeps the 256 a side of its format whatever the limits say
    constexpr uint32_t rows_for(uint32_t version) const noexcept { return version == LVL_EXTENDED_VERSION || rows() < LVL_MAX_GRID_SIDE ? rows() : LVL_MAX_GRID_SIDE; }
    constexpr uint32_t columns_for(uint32_t version) const noexcept { return version == LVL_EXTENDED_VERSION || columns() < LVL_MAX_GRID_SIDE ? columns() : LVL_MAX_GRID_SIDE; }
    constexpr uint32_t cells_for(uint32_t version) const noexcept {
        return version == LVL_EXTENDED_VERSION || cells() < LVL_MAX_GRID_SIDE * LVL_MAX_GRID_SIDE ? cells() : LVL_MAX_GRID_SIDE * LVL_MAX_GRID_SIDE;
    }
};

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
    ObjectOutsideGrid,       // plants and waypoints outside the grid: harmless in the original (bounds checked or never looked up)
    // a limit of the place that reads the file (LevelLimits), not of the original
    FileTooLarge,            // more bytes than LevelLimits::max_read_bytes: nothing of it is read
    // version 9 (the Extended map), which only a place with LevelLimits::allow_extended reads
    VersionNotSupported,     // a version 9 file or level where Extended maps are not accepted (LevelLimits::allow_extended is false): nothing of it is read
    FormatFlagsUnknown,      // a version 9 `format flags` dword with a bit this reader does not know: a later version of the format, so the file is refused
    RulesOutOfRange,         // a version 9 `rules` word other than 0 (the original's rules) or 1 (extended rules)
    DescriptionTooLong       // a version 9 description of more bytes than the limits allow (at most 255)
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
    uint16_t tile_id{0}; // Tile id (the dictionary index is the animation id): 152 .. 155 are the start markers
    uint16_t y{0};       // Grid row coordinate
    uint16_t x{0};       // Grid column coordinate
    uint8_t  team_id{0}; // the remake's team of a start marker tile (start_marker_team_of_id: 0 green, 1 red, 2 blue, 3 black), 255 = any other record (a plant ...)
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
    uint32_t version{0};         // 8 (a Classic map) or 9 (an Extended map)
    uint32_t format_flags{0};    // version 9 only (0 in a Classic map): the file's `format flags` dword; the reader refuses any bit it does not know, so this is 0 today
    uint16_t rules{0};           // version 9 only: 0 = the original's rules exactly, 1 = extended rules (the simulation does not read it yet)
    uint32_t game_mode{0};       // 1 in the shipped maps (standard); the original stores it and never reads it again
    uint16_t default_minutes{0}; // Match duration (6, 8, 10, 12)
    std::string description;     // the description: at most 30 bytes in version 8 (the file's field), at most 255 in version 9
    uint16_t tile_type_count{0}; // Tile dictionary count
    std::vector<std::string> tile_dictionary; // 11-byte ASCII tile names
    // Dimension property supporting both .width and .width()
    // Both conversions return a reference, as the non-const one always did: with a const one that returned a value, GCC 12's -Wconversion rejected
    // every implicit use of `.width` / `.height` on a non-const object ("choosing operator uint32_t&() over operator uint32_t() const"), which stopped
    // a GCC build with warnings as errors (the tests use the property form on purpose, the application the call form).
    struct DimensionProp {
        uint32_t val{0};
        constexpr operator const uint32_t&() const noexcept { return val; }
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
    LevelData(const LevelData& o) : version(o.version), format_flags(o.format_flags), rules(o.rules), game_mode(o.game_mode),
        default_minutes(o.default_minutes), description(o.description),
        tile_type_count(o.tile_type_count), tile_dictionary(o.tile_dictionary),
        width(o.width), height(o.height), layer1_terrain(o.layer1_terrain),
        layer2_interactive(o.layer2_interactive), anthill_spawns(o.anthill_spawns),
        food_schedules(o.food_schedules), ambient_flag(o.ambient_flag),
        ambient_tile_or_sound(o.ambient_tile_or_sound), waypoints(o.waypoints),
        boundary_param(o.boundary_param), load_notes(o.load_notes) {
        layer1_terrain.parent = this;
    }
    LevelData(LevelData&& o) noexcept : version(o.version), format_flags(o.format_flags), rules(o.rules), game_mode(o.game_mode),
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
            version = o.version; format_flags = o.format_flags; rules = o.rules; game_mode = o.game_mode; default_minutes = o.default_minutes;
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
            version = o.version; format_flags = o.format_flags; rules = o.rules; game_mode = o.game_mode; default_minutes = o.default_minutes;
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

    /// Is the level an Extended map (version 9)?
    constexpr bool is_extended() const noexcept { return version == LVL_EXTENDED_VERSION; }

    /// Does the layer-2 cell at (`row`, `column`) hold an object anchor in its third word? Every cell with a tile does, except the original's bomb tiles (their third
    /// word is the owner) and the empty cell. false for a cell outside the grid.
    bool has_object_anchor(uint32_t row, uint32_t column) const noexcept;

    /// The absolute anchor (row and column of the object's first cell) of the layer-2 cell at (`row`, `column`): false when the cell has none (has_object_anchor).
    /// Version 8: the low byte of the third word is the row and the high byte the column. Version 9: the third word holds the anchor RELATIVE to the cell, the low byte a
    /// signed row offset and the high byte a signed column offset, so that a cell far from its anchor needs no wide number; in a map of at most 256 rows and 256 columns
    /// the sum is taken modulo 256 (an anchor byte of a Classic map keeps its value through a conversion even when it points outside the grid, as some library
    /// maps' do), in a larger map it is the plain sum (which may lie outside the grid or the 0 .. 255 of a Classic map; convert_to_classic says so).
    bool object_anchor(uint32_t row, uint32_t column, int32_t& anchor_row, int32_t& anchor_column) const noexcept;

    // Dictionary Lookups
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;

    /// The level as it is played by the teams of `roster_mask` (bit t = team t takes part): a team without a player has no hill, no start marker
    /// (so no starting ant) and no eggs. The original keeps a NULL entry in its team table for such a team (CHECKGO skips it), so nothing of it exists.
    /// Team t is the color of the hill tile 248 (green, 0), 247 (red, 1), 246 (blue, 2), 245 (black, 3) and of the start marker 154, 155, 153, 152 (by tile id, not by name).
    LevelData for_roster(uint8_t roster_mask) const;

    /// Is this level playable by the teams of `roster_mask`, and what is odd about it? The loader's own notes (load_notes) plus the checks that need the whole level:
    /// the shape of the grid, tile indexes outside the dictionary, objects outside the grid, and the start markers of the teams that play (the original indexes
    /// its row table with the record's row and column without a check, so a start marker outside the grid is a crash or a heap overwrite there; the remake
    /// refuses the map for a roster that contains that team). A team outside the roster is not looked at: its markers are never used. `limits`: the grid and
    /// the dictionary are held to them.
    LevelValidation validate(uint8_t roster_mask = 0x0F, const LevelLimits& limits = LevelLimits{}) const;

    // Loading Convenience. true = the original's loader would read the file to its end (a file that ends inside block 4 or the final word is read as far as it goes,
    // as the original does) and the remake can hold the result; the optional report lists every finding of a file that does not load, and of one that does
    // (for all four teams). Whether a MATCH can be played is validate(roster) / LVLParser::check_*. A file beyond `limits` is not loaded (FileTooLarge,
    // GridTooLarge, DictionaryTooLarge).
    bool load_from_file(const std::string& filepath, LevelValidation* report = nullptr, const LevelLimits& limits = LevelLimits{});
    bool load_from_memory(const uint8_t* data, size_t size, LevelValidation* report = nullptr, const LevelLimits& limits = LevelLimits{});
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
    static bool load_from_file(const std::string& filepath, LevelData& out_level, LevelValidation* report = nullptr, const LevelLimits& limits = LevelLimits{});
    static bool load_from_memory(const uint8_t* data, size_t size, LevelData& out_level, LevelValidation* report = nullptr, const LevelLimits& limits = LevelLimits{});

    /// The question of a server: may this file be hosted for the teams of `roster_mask`? One call, a yes or no (`playable`) and the reasons (`problems`).
    static LevelValidation check_file(const std::string& filepath, uint8_t roster_mask = 0x0F, const LevelLimits& limits = LevelLimits{});
    static LevelValidation check_memory(const uint8_t* data, size_t size, uint8_t roster_mask = 0x0F, const LevelLimits& limits = LevelLimits{});
};

} // namespace ants::assets
