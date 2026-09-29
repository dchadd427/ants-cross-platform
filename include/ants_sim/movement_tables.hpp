#pragma once

// ============================================================================
// Ground-truth locomotion tables of the original 1998 game.
//
// Movement in the original is animation driven: a walking ant moves by the
// current walk frame's (dx, dy) when that frame's duration expires, so the
// frame data IS the speed. The data behind this API is generated from
// Original-Ants/Ants.exe and Original-Ants/ants.chd by
// tools/extract_movement_tables.py into src/ants_sim/movement_tables_data.inc
// (no game files are needed at runtime).
//
// Conventions (all taken from the original executable):
//   * Directions: 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW. Only 0..4 are
//     stored; 5, 6 and 7 are the 3, 2 and 1 animations mirrored with every
//     frame dx negated (FUN_01018a7a / FUN_01018b9f).
//   * Ant types: 0 worker, 1 bomber, 2 fire, 3 thief, 4 combat, 5 swimmer.
//   * Terrain classes: 0 grass, 1 sand, 2 water, 3 mud, 4 dirt.
//   * Tile ids are CHD Table-4 animation indices (0..1343), as stored in the
//     map layers.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace ants::sim::movement {

namespace data {
struct FrameRec;  // generated, see src/ants_sim/movement_tables_data.inc
}

inline constexpr uint16_t kNoAnimation = 0x7FFE;  // "no animation" marker of the static tables
inline constexpr uint16_t kEventNone = 11111;     // frame event "none" (CHD 1111111, FUN_0102a977)
inline constexpr int16_t kSoundNone = -1;         // frame sound id "none"
inline constexpr uint16_t kTileIdCount = 1344;    // CHD Table-4 animations = tile ids
inline constexpr uint8_t kAntTypeCount = 6;
inline constexpr uint8_t kTerrainClassCount = 5;
inline constexpr uint8_t kDirectionCount = 8;

enum : uint8_t { kAntWorker = 0, kAntBomber = 1, kAntFire = 2, kAntThief = 3, kAntCombat = 4, kAntSwimmer = 5 };
enum : uint8_t { kTerrainGrass = 0, kTerrainSand = 1, kTerrainWater = 2, kTerrainMud = 3, kTerrainDirt = 4 };

// Per-tile flag bits (FUN_0100724c). 0x08, 0x10 and 0x20 exist too; their
// meaning has not been decoded yet.
enum : uint8_t {
    kTileFlagSolid = 0x01,    // object footprint copied into the map's blocking bit
    kTileFlagFood = 0x02,     // food object (FUN_010071dd)
    kTileFlagPowerUp = 0x04,  // power-up (FUN_01007202)
};

// Sequence flags of a clip (ants.chd flag1..flag3, read by FUN_0102be51).
// They only change how frame sounds are played, never timing or movement.
enum : uint8_t {
    kClipFlagSoundOnce = 0x01,       // flag1: each frame's sound plays only on the first loop
    kClipFlagSoundDuplicate = 0x02,  // flag2: duplicate a sound that is still playing
    kClipFlagSoundTrack = 0x04,      // flag3: stop the clip's sounds when it is replaced
};

/**
 * @brief One locomotion animation: a view of its generated frames.
 *
 * Frame i's (dx, dy) is applied when frame i's duration ends. A mirrored clip
 * (directions 5..7) shares the frames of its source direction and negates dx.
 * Accessors return 0 (sound: kSoundNone) for an out-of-range index.
 */
struct MotionClip {
    const data::FrameRec* frames{nullptr};
    uint16_t count{0};
    uint16_t chd_index{0x7FFE};  // CHD Table-4 index of the stored (unmirrored) animation
    bool mirrored{false};
    uint8_t flags{0};            // kClipFlag* bits

    bool valid() const noexcept { return frames != nullptr && count > 0; }
    int16_t dx(uint16_t i) const noexcept;   // mirrored ? -frames[i].dx : frames[i].dx
    int16_t dy(uint16_t i) const noexcept;
    uint16_t duration(uint16_t i) const noexcept;  // milliseconds
    uint16_t event(uint16_t i) const noexcept;     // kEventNone, or 3 hit ground, 4 attack hit, 5 heal
    int16_t sound(uint16_t i) const noexcept;      // CHD sound id, kSoundNone if none
    uint32_t total_duration_ms() const noexcept;   // one loop
};

/// Any generated clip by CHD Table-4 index; invalid clip if it is not one of them.
MotionClip clip_by_chd(uint16_t chd_index, bool mirrored = false) noexcept;

/// Walk (action 1) clip. Terrain 0, 1, 3, 4; terrain 2 (water) has no walk clip
/// in the original (swimmers use swim_clip, other types walk on water with the
/// mud clip), so it returns an invalid clip, as do out-of-range arguments.
MotionClip walk_clip(uint8_t ant_type, uint8_t terrain_class, uint8_t dir, bool carrying) noexcept;
MotionClip swim_clip(uint8_t dir) noexcept;   // swimmer walking on water (assw*)
MotionClip dive_clip(uint8_t dir) noexcept;   // swimmer entering water, moves one tile (asdi*)
MotionClip climb_clip(uint8_t dir) noexcept;  // swimmer leaving water, moves one tile (asgo*)
MotionClip idle_clip(uint8_t ant_type, uint8_t dir, bool carrying) noexcept;
MotionClip idle_water_clip() noexcept;        // idle on water, no direction (astw301)
MotionClip cant_go_clip(uint8_t ant_type, bool carrying) noexcept;  // action 0xB, single direction
MotionClip bump_clip() noexcept;              // "bump" effect on a blocked tile (CHD 0xDC, sound 47)

/// Number of generated clips and access by position (sorted by CHD index).
std::size_t clip_count() noexcept;
MotionClip clip_at(std::size_t i) noexcept;   // invalid clip if i >= clip_count()

/// Terrain class of a CHD tile id (0 for ids >= 1344, including 0x7FFE).
uint8_t terrain_class_of_tile(uint16_t tile_id) noexcept;
/// Terrain class of a map cell (FUN_01008af7): a bridge on layer 2 makes the
/// cell mud (class 3), otherwise the class of the layer-1 tile.
uint8_t terrain_class_of_cell(uint16_t layer1_tile_id, uint16_t layer2_tile_id) noexcept;
/// Tile flag bits of a CHD tile id (0 for ids >= 1344).
uint8_t tile_flags_of(uint16_t tile_id) noexcept;
/// Bridge tiles 0x22..0x25 (FUN_01008b90).
bool is_bridge_tile(uint16_t tile_id) noexcept;

/// Destination walkability by terrain class (CanEnter rule R1). Water (class 2)
/// is not walkable here; the original lets only swimmers enter it.
bool terrain_walkable(uint8_t terrain_class) noexcept;
/// Path step-cost weight by terrain class (FUN_010208e8): 21 for a swimmer on
/// water, otherwise {20, 16, 8000, 48, 24, 40}[class]. Classes above 5 cannot
/// occur in the original; they return 8000 (impassable).
uint32_t terrain_step_weight(uint8_t terrain_class, bool swimmer) noexcept;

/// Direction of a one-tile step (FUN_01017531): table[(drow+1)*3+(dcol+1)].
/// Requires |drow|, |dcol| <= 1 (larger deltas are reduced to their sign);
/// (0, 0) yields 0, as in the original.
int dir_from_delta(int drow, int dcol) noexcept;

struct TileDelta {
    int8_t drow{0};
    int8_t dcol{0};
};
/// {drow, dcol} of a direction (table 0x1004950); {0, 0} for dir >= 8.
TileDelta dir_delta(uint8_t dir) noexcept;

}  // namespace ants::sim::movement
