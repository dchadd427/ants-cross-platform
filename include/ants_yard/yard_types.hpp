#pragma once

// The Yard's data as the client and the server both read it (docs/YARD.md): one ant of a batch, a place with an open game, a batch, and an order that a visitor gives. Plain data and constants only,
// no code and no dependency, so that the server can include this file without linking the application. The field widths are the wire's: every value of the movement tables fits (a clip has at
// most 85 frames, a frame lasts at most 1,000 ms, a clip index is below 1,344) and so does a world pixel of any map of up to 2,047 tiles on a side.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace ants::yard {

/// "No clip": the movement tables' own marker (ants::sim::movement::kNoAnimation)
inline constexpr uint16_t kNoClip = 0x7FFE;

/// AntRecord::color of the visitor's own ants; 1 .. 3 are the colors of the others
inline constexpr uint8_t kColorMine = 0;
inline constexpr uint8_t kColorCount = 4;

/// What a batch holds and what an order may name: all of the visitor's own ants (at most kMaxOwnAnts) and the nearest of the others' (at most kMaxForeignAnts)
inline constexpr size_t kMaxOwnAnts = 6;
inline constexpr size_t kMaxForeignAnts = 120;
inline constexpr size_t kMaxOrderAnts = kMaxOwnAnts;

/// The places with an open game: at most kMaxHills, each a square of kHillTiles x kHillTiles tiles. The Yard has no hill of its own.
inline constexpr size_t kMaxHills = 8;
inline constexpr int32_t kHillTiles = 4;

/// Bits of AntRecord::flags. The picture of the ant does not depend on them (its clip says it all): they are for what is drawn over it and for the rules of the server.
inline constexpr uint8_t kFlagNapping = 0x01;     // the visitor is away
inline constexpr uint8_t kFlagShielded = 0x02;    // just placed: the ant takes and deals no damage for a few seconds

/// One ant of a batch: what the picture needs to draw it (shaped like the fields of sim::AntSnapshot that the renderer reads)
struct AntRecord {
    uint16_t id{0};                   // the server's id of the ant, unique in a batch (0 is no ant)
    uint16_t clip{kNoClip};           // the clip it plays: its ants.chd Table-4 index, the stored (not the mirrored) one
    uint16_t next_clip{kNoClip};      // the clip that follows this one when it ends, played with the same `mirrored`; kNoClip when none is known (the last frame stays)
    uint16_t left_ms{0};              // milliseconds until the frame that is shown now ends
    uint16_t px{0};                   // where it stands, in world pixels
    uint16_t py{0};
    uint8_t color{kColorMine};        // 0: the visitor's, 1 .. 3: the colors of the others
    uint8_t frame{0};                 // the frame of the clip that is shown now (0 is the first)
    uint8_t flags{0};                 // kFlag* bits
    bool mirrored{false};             // the clip is shown mirrored (the directions SW, W and NW)
};

/// A place with an open game: the top left tile of its square
struct Hill {
    uint8_t id{0};                    // 1 .. kMaxHills, unique in a list (0 is the game's own hill, which the Yard does not have)
    uint16_t x{0};
    uint16_t y{0};
};

/// What the server sends the visitor at a time: the ants in view and the visitor's own, as they are at one tick of the server's world
struct Batch {
    uint32_t tick{0};                 // the tick of the server's world that the records describe
    std::vector<AntRecord> ants;
};

/// What a visitor can ask of their own ants
enum class IntentKind : uint8_t { Move = 1, Attack = 2, Stop = 3 };

/// An order, as the client hands it to the connection: the ants are the visitor's own, by the server's ids
struct Intent {
    IntentKind kind{IntentKind::Move};
    int16_t tile_x{0};                // Move: the tile to walk to; Attack: the tile of the target when the order was given; Stop: 0
    int16_t tile_y{0};
    uint16_t target{0};               // Attack: the server's id of the ant on that tile, 0 when none stands there; otherwise 0
    std::vector<uint16_t> ants;       // at most kMaxOrderAnts
};

static_assert(sizeof(AntRecord) == 16, "a record of a batch is 16 bytes in memory (the wire's is smaller)");
static_assert(std::is_trivially_copyable<AntRecord>::value && std::is_trivially_copyable<Hill>::value, "records are plain data");

}  // namespace ants::yard
