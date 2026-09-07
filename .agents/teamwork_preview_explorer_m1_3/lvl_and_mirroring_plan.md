# Milestone 1 Asset Specification: Map Parsing (`.LVL`) & 5-to-8 Directional Mirroring Engine

**Document Identifier:** `lvl_and_mirroring_plan.md`  
**Target Milestone:** Milestone 1 (`ants-assets`)  
**Author:** M1 Explorer 3 (`explorer_m1_3`)  
**Authoritative References:** `Original-Ants/Maps/*.LVL`, `Original-Ants/ants.chd`, `Original-Ants/Ants.exe`, `survey_assets.md`, `PROJECT.md`  
**Status:** Verified & Complete  

---

## 1. Executive Summary & Verification Guarantees

This document establishes the exact, byte-verified C++ implementation logic for two core components of `libants-assets`:
1. **The `.LVL` Map File Decoder:** Direct binary decoding of all 6 original levels (`TINY.LVL`, `SMALL.LVL`, `MEDIUM.LVL`, `GAUNTLET.LVL`, `ISLANDS.LVL`, `TREASURE.LVL`), validating headers, tile dictionaries, 6-byte cell structures for Layer 1 & Layer 2, sentinel `0x7FFE`, and all 4 trailing configuration blocks.
   - **Zero Remaining Bytes Guarantee:** Exactly **0 remaining bytes (`rem = 0`)** across every official `.LVL` file.
2. **The 5-to-8 Directional Sprite Mirroring Engine:** Direct mathematical reflection and pre-computation of the 3 Western hemisphere facings (North-West, West, South-West) from stored Eastern hemisphere facings (North-East, East, South-East), including frame anchor offsets, bounding boxes, and the lunchbox carrying sprite suite.
   - **$O(1)$ Runtime Guarantee:** Pre-populated in-memory direct-indexed arrays providing constant-time lookup with zero runtime branching or per-frame pixel copying.

---

## 2. `.LVL` Map File Binary Specification

All 6 official maps adhere to a uniform binary stream layout:

```text
+-------------------------------------------------------------------------+
| Fixed Header (42 bytes)                                                 |
| - version (4B), game_mode (4B), default_minutes (2B), description (30B) |
| - tile_type_count N (2B)                                                |
+-------------------------------------------------------------------------+
| Tile Dictionary ((N + 1) * 11 bytes)                                    |
| - Array of (N + 1) fixed 11-byte ASCII strings                          |
+-------------------------------------------------------------------------+
| Grid Dimensions (8 bytes)                                               |
| - width (4B), height (4B)                                               |
+-------------------------------------------------------------------------+
| Layer 1: Terrain Base Grid (width * height * 6 bytes)                   |
| - Array of 3x uint16_t words per cell                                   |
+-------------------------------------------------------------------------+
| Layer 2: Interactive Overlay Grid (width * height * 6 bytes)            |
| - Array of 3x uint16_t words per cell (sentinel 0x7FFE = empty)         |
+-------------------------------------------------------------------------+
| Trailing Block 1: Anthill Spawns (2 + count * 6 bytes)                  |
| - count (2B) + [tile_id (2B), y (2B), x (2B)]                           |
+-------------------------------------------------------------------------+
| Trailing Block 2: Food Respawn Pools (2 + variable bytes)               |
| - count (2B) + [y (2B), x (2B), init_delay (2B), respawn_int (2B),      |
|                 item_count (2B) + [weight (2B), tile_id (2B)]*item_cnt] |
+-------------------------------------------------------------------------+
| Trailing Block 3: Ambient Parameters (4 bytes)                          |
| - flag1 (2B), tile_or_sound_id (2B)                                     |
+-------------------------------------------------------------------------+
| Trailing Block 4: Waypoints / Patrol Routes (2 + variable bytes)        |
| - count (2B) + [y (2B), x (2B), flag (4B) +                             |
|                 if (flag != 0) { param (4B), double probs[5] (40B) }]   |
+-------------------------------------------------------------------------+
| Level Boundary Parameter (2 bytes)                                      |
| - f_last (2B)                                                           |
+-------------------------------------------------------------------------+
| EOF (Total bytes consumed == Total file size, rem = 0)                  |
+-------------------------------------------------------------------------+
```

### 2.1 File Header & Tile Dictionary

```cpp
#pragma pack(push, 1)

struct LVLHeaderFixed {
    uint32_t version;          // Offset 0x00: Must be 8 (0x00000008)
    uint32_t game_mode;        // Offset 0x04: 1 = Standard Food Gathering
    uint16_t default_minutes;  // Offset 0x08: Match duration (6, 8, 10, or 12)
    char     description[30];  // Offset 0x0A: Fixed 30-byte ASCII description string
    uint16_t tile_type_count;  // Offset 0x28: Tile asset dictionary count N
};

#pragma pack(pop)
```

#### Tile Dictionary Sizing Rule: $(N + 1) \times 11$ Bytes
- The field `tile_type_count` contains integer $N$.
- The dictionary data immediately following offset `0x2A` contains **exactly $N + 1$ entries**, each occupying **11 bytes**.
- Dictionary indices span $0 \le i \le N$.
- Total dictionary byte count:
  $$\text{DictSizeBytes} = (\text{tile\_type\_count} + 1) \times 11$$
- Each entry is an 11-byte ASCII string terminated with `\0` and padded with arbitrary filler. Strings map directly to animation sequence names in `ants.chd` Table 4 (e.g. `g01a`, `d01a`, `fdpezp1`, `BLACKHILL`, `BSTART`). Unused slots are represented as `.` (`.\0`).

#### Grid Dimensions
Immediately following the dictionary:
- `uint32_t width`: Grid width in tiles (31, 40, or 60).
- `uint32_t height`: Grid height in tiles (31, 40, or 60).

---

### 2.2 Grid Layers: Layer 1 & Layer 2 (6 Bytes per Cell)

Each cell in Layer 1 and Layer 2 consists of 3 little-endian 16-bit unsigned words:

```cpp
#pragma pack(push, 1)

struct LVLCellInFile {
    uint16_t word1;   // Base tile index in dictionary (or 0x7FFE for empty Layer 2)
    uint16_t word2;   // Flags and variation (bit 0 is merged into runtime flag)
    uint16_t word3;   // Extra properties / MSVC debug filler (0xCDCD)
};

#pragma pack(pop)
```

#### Grid Sizing
- Total tiles per layer: $T = \text{width} \times \text{height}$.
- Layer 1 byte size: $T \times 6$ bytes.
- Layer 2 byte size: $T \times 6$ bytes.

#### Layer 1 (Terrain Base Grid)
- Represents immutable terrain foundation.
- `word1`: Tile dictionary index (values in range $[431, 669]$).
- `word2`: Walkability / surface variation flag (`0` or `1`).
- `word3`: Terrain property word (contains `0xCDCD` in original maps).
- **Terrain Categorization:**
  - **Walkable Ground:** Grass (`g01a..`), Dirt (`d01a..`), Clay (`m01a..`), Sand (`s01a..`).
  - **Obstacles / Impassable Wall:** Boulders, cliffs, rocks, trees.
  - **Deep Water (`w01a..`):** Non-swimmer ants entering deep water drown immediately; swimmer ants navigate freely.

#### Layer 2 (Interactive Overlay Grid) & Sentinel `0x7FFE`
- Represents dynamic items, anthills, bridges, food, and power-up pickups.
- **Empty Cell Sentinel:**
  $$\text{word1} == \mathbf{0x7FFE} \quad (32,766)$$
  Any Layer 2 cell where `word1 == 0x7FFE` indicates an **empty interactive tile** (no obstacle, anthill, food, or power-up).
- **Non-Empty Cell Entities:**
  - **Anthills:**
    - Team 0 (Black): `BLACKHILL` (Tile 245)
    - Team 1 (Blue): `BLUEHILL` (Tile 246)
    - Team 2 (Red): `REDHILL` (Tile 247)
    - Team 3 (Green): `GREENHILL` (Tile 248)
    *(Occupies a $4 \times 4$ cell footprint centered at the base location).*
  - **Power-Up Pickups:**
    - Combat Ant: `pu_comb` (Tile 62)
    - Thief Ant: `pu_thief` (Tile 63)
    - Bomber Ant: `pu_bomb` (Tile 64)
    - Swimmer Ant: `pu_swim` (Tile 65)
    - Fire Ant: `pu_mason` (Tile 66)
  - **Food Items:** `fdpezp1`, `fdpezb1`, `fdfrl1..6`, `fdgumw1..4`, `fdburgr`, `fdsuckr`, `FOOD`.
  - **Static Obstacles:** `broken1..3`, `bigrock1..4`, `botcap1`, `bolt1..2`, `ball`, etc.

---

### 2.3 Trailing Configuration Blocks

Directly following Layer 2, 4 configuration blocks and a final level boundary parameter appear in strict succession:

#### Block 1: Anthill Base Spawns & Starting Locations
```cpp
#pragma pack(push, 1)

struct LVLSpawnRecord {
    uint16_t tile_id; // Dictionary index (152=BSTART, 153=USTART, 154=GSTART, 155=RSTART, or flowers)
    uint16_t y;       // Grid row coordinate (0 <= y < height)
    uint16_t x;       // Grid column coordinate (0 <= x < width)
};

#pragma pack(pop)
```
- Format: `uint16_t count`, followed by `count * sizeof(LVLSpawnRecord)` (6 bytes per record).
- **Coordinate Order:** `(tile_id, y, x)` where `y` is the row and `x` is the column.
- **Anthill Starting Team IDs:**
  - `BSTART` (Tile 152): Team 0 (Black Anthill)
  - `USTART` (Tile 153): Team 1 (Blue Anthill)
  - `GSTART` (Tile 154): Team 3 (Green Anthill)
  - `RSTART` (Tile 155): Team 2 (Red Anthill)
- In addition to anthill spawns, Block 1 contains starting decorative floral elements (`flower1`, `flower2a`, `clovers`, etc.) clustered near bases.

#### Block 2: Food Respawn Pools & Schedules
```cpp
#pragma pack(push, 1)

struct LVLFoodVariant {
    uint16_t weight;  // Selection / stage weight (0 = terminal / baseline)
    uint16_t tile_id; // Dictionary tile index or 0x7FFE
};

struct LVLFoodSpawnHeader {
    uint16_t y;                // Grid row coordinate
    uint16_t x;                // Grid column coordinate
    uint16_t initial_delay;    // Delay before initial food appearance (seconds)
    uint16_t respawn_interval; // Respawn interval after pickup (seconds)
    uint16_t item_count;       // Number of weighted variants
};

#pragma pack(pop)
```
- Format: `uint16_t count`, followed by `count` variable-length records:
  - Header: 10 bytes (`y, x, initial_delay, respawn_interval, item_count`)
  - Variants: `item_count * 4` bytes (`weight, tile_id`)
- **Coordinate Order:** `(y, x)` matching Block 1.
- Each pool cycles or selects from the weighted variants. Sentinel `tile_id == 0x7FFE` denotes empty / despawn state.

#### Block 3: Ambient Sound & Visual Parameters
```cpp
#pragma pack(push, 1)

struct LVLAmbient {
    uint16_t flag1;            // Ambient flag (always 0 in official maps)
    uint16_t tile_or_sound_id; // Ambient reference (always 0x7FFE in official maps)
};

#pragma pack(pop)
```
- Exactly 4 bytes. In all 6 official maps, `flag1 = 0` and `tile_or_sound_id = 0x7FFE`.

#### Block 4: Waypoints & Patrol Paths
```cpp
#pragma pack(push, 1)

struct LVLWaypointHeader {
    uint16_t y;    // Grid row coordinate
    uint16_t x;    // Grid column coordinate
    uint32_t flag; // 0 = standard point; 1 = extended path with direction probabilities
};

struct LVLWaypointExtended {
    uint32_t param;              // Patrol parameter (e.g. 15, 20, 30)
    double   probabilities[5];   // 5 IEEE-754 64-bit doubles summing to 1.0 (40 bytes)
};

#pragma pack(pop)
```
- Format: `uint16_t count`, followed by `count` variable-length entries:
  - Header: 8 bytes (`y, x, flag`).
  - If `flag != 0`: 44 additional bytes (`param` [4 bytes] + 5 `double` values [40 bytes]). Total entry size: 52 bytes.
  - If `flag == 0`: No extra bytes. Total entry size: 8 bytes.
- **Probability Vectors:** The 5 `double` precision floats define branching weights for entity patrol AI (e.g. $[0.2, 0.2, 0.2, 0.2, 0.2]$ or $[0.45, 0.0, 0.0, 0.1, 0.45]$), strictly summing to $1.0$.

#### Final Level Boundary Field
- `uint16_t f_last`: 2 bytes immediately preceding EOF.
- Values across maps: `TINY` = 3, `SMALL` = 2, `MEDIUM` = 6, `GAUNTLET` = 6, `ISLANDS` = 4, `TREASURE` = 9.

---

### 2.4 Complete Verification Matrix Across All 6 Official Maps

The following table documents the exact, verified metrics obtained by parsing every byte of the original assets:

| Level | Size (Bytes) | Grid Dims | Dict Count ($N$) | Header+Dict Bytes | Layer 1 Bytes | Layer 2 Bytes | Block 1 (Spawns) | Block 2 (Food) | Block 3 (Amb) | Block 4 (WP) | `f_last` | Rem Bytes |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **`TINY.LVL`** | 19,312 | $31 \times 31$ | 669 (+1=670) | 7,420 | 5,766 | 5,766 | 12 (74 B) | 14 (278 B) | 4 B | 0 (2 B) | 3 (2 B) | **0** |
| **`SMALL.LVL`** | 34,190 | $40 \times 40$ | 1,325 (+1=1326) | 14,636 | 9,600 | 9,600 | 18 (110 B) | 5 (132 B) | 4 B | 2 (106 B) | 2 (2 B) | **0** |
| **`MEDIUM.LVL`** | 58,381 | $60 \times 60$ | 1,324 (+1=1325) | 14,625 | 21,600 | 21,600 | 30 (182 B) | 4 (114 B) | 4 B | 15 (254 B) | 6 (2 B) | **0** |
| **`GAUNTLET.LVL`** | 58,508 | $60 \times 60$ | 1,329 (+1=1330) | 14,680 | 21,600 | 21,600 | 32 (194 B) | 2 (46 B) | 4 B | 20 (382 B) | 6 (2 B) | **0** |
| **`ISLANDS.LVL`** | 58,776 | $60 \times 60$ | 1,329 (+1=1330) | 14,680 | 21,600 | 21,600 | 40 (242 B) | 10 (206 B) | 4 B | 22 (442 B) | 4 (2 B) | **0** |
| **`TREASURE.LVL`** | 58,853 | $60 \times 60$ | 1,334 (+1=1335) | 14,735 | 21,600 | 21,600 | 29 (176 B) | 18 (406 B) | 4 B | 19 (330 B) | 9 (2 B) | **0** |

$$\text{Total File Size} = \text{Header+Dict} + \text{Layer 1} + \text{Layer 2} + \text{Block 1} + \text{Block 2} + \text{Block 3} + \text{Block 4} + 2$$
$$\text{Remaining Bytes} = 0 \quad \text{for 100\% of maps}.$$

---

## 3. 5-to-8 Directional Sprite Mirroring Engine

### 3.1 Foundational Architecture & Direction Codes

`ants.chd` minimizes asset duplication by storing animation sequences and sprites for only **5 base compass facings**:
- Facing straight North (`7`)
- Facing straight South (`3`)
- Facing the Eastern hemisphere (`8` for NE, `9` for E, `2` for SE).

The Western hemisphere facings (NW, W, SW) are derived via horizontal reflection (flip along the vertical axis).

```text
               0: North [Dir 7]
                     |
  7: North-West      |      1: North-East [Dir 8]
  (Mirrored NE)      |
          \          |          /
           \         |         /
6: West ----+--------+--------+----> 2: East [Dir 9]
(Mirrored E)         |
           /         |         \
          /          |          \
  5: South-West      |      3: South-East [Dir 2]
  (Mirrored SE)      |
                     v
               4: South [Dir 3]
```

### 3.2 Mathematical Formulation for Horizontal Reflection

#### 1. Pixel Reflection
Let an 8-bit paletted sprite have visible width $W$, visible height $H$, and memory pitch stride $P$ ($P \ge W$):
For every row $y \in [0, H - 1]$:
$$p'_{\text{flipped}}(x, y) = p_{\text{source}}(W - 1 - x, y) \quad \text{for } x \in [0, W - 1]$$
Padding bytes ($x \in [W, P - 1]$) are filled with $0$.

**Involution Property:** Reflecting twice yields the exact original sprite:
$$(p')'(x, y) = p'(W - 1 - x, y) = p(W - 1 - (W - 1 - x), y) = p(x, y)$$

#### 2. Frame Render Offset Inversion ($dx', dy'$)
In `ants.chd`, a frame specifies render offset $(dx, dy)$ relative to the unit world center $(X_{\text{world}}, Y_{\text{world}})$.
When unmirrored, the horizontal span covered by the sprite on screen is:
$$[\, X_{\text{world}} + dx, \quad X_{\text{world}} + dx + W \,]$$
Relative to the unit center ($X = 0$), the sprite extends from $+dx$ to $+(dx + W)$.
Under horizontal reflection across the vertical centerline $x = 0$:
- The right edge at $+(dx + W)$ reflects to $-(dx + W)$.
- The left edge at $+dx$ reflects to $-dx$.
- The reflected sprite spans from $[-(dx + W), -dx]$.

Because 2D renderers blit sprites starting from the top-left coordinate, the mirrored top-left offset $dx'$ is:
$$\mathbf{dx' = -(dx + W)}$$
$$\mathbf{dy' = dy} \quad \text{(vertical axis is invariant under horizontal flip)}$$

**Mathematical Verification:**
- Reflected span: $[\, X + dx', \; X + dx' + W \,] = [\, X - (dx + W), \; X - (dx + W) + W \,] = [\, X - dx - W, \; X - dx \,]$.
- Right edge distance from origin: $|-dx| = dx$.
- Left edge distance from origin: $|-dx - W| = dx + W$.
- The reflected bounding region is identical in size and mirrored about $x = 0$.

#### 3. Bounding Box Inversion ($box\_left', box\_right'$)
Animation subitems specify signed collision bounds $[box\_left, box\_top, box\_right, box\_bottom]$ relative to the unit origin.
Under horizontal reflection:
$$\mathbf{box\_left' = -box\_right}$$
$$\mathbf{box\_right' = -box\_left}$$
$$\mathbf{box\_top' = box\_top}, \quad \mathbf{box\_bottom' = box\_bottom}$$

Because $box\_left \le box\_right$, we have $-box\_right \le -box\_left$, preserving valid rectangle geometry ($box\_left' \le box\_right'$).

---

### 3.3 Lunchbox Carrying Sprite Mapping

Worker, Fire, Swimmer, and Thief ants carry gathered food in lunchboxes held between their mandibles. The lunchbox graphics use a specific directional naming convention in `ants.chd`:

| Heading Angle | Standard Enum | Base CHD Facing | Mirrored? | Lunchbox Sprite Prefix | Source Sprite Example |
|:---:|:---:|:---:|:---:|:---:|:---|
| **0° (N)** | `DIR_N` (0) | `7` | No | `7lb` | `7lb0000.bmp` (Sprite 1678), `7lb0002.bmp` (1689) |
| **45° (NE)** | `DIR_NE` (1) | `8` | No | `6lb` | `6lb0000.bmp` (Sprite 1679), `6lb0002.bmp` (1695) |
| **90° (E)** | `DIR_E` (2) | `9` | No | `5lb` | `5lb0000.bmp` (Sprite 1680), `5lb0002.bmp` (1701) |
| **135° (SE)** | `DIR_SE` (3) | `2` | No | `4lb` | `4lb0000.bmp` (Sprite 1681), `4lb0002.bmp` (1707) |
| **180° (S)** | `DIR_S` (4) | `3` | No | `3lb` | `3lb0000.bmp` (Sprite 1677), `3lb0002.bmp` (1688) |
| **225° (SW)** | `DIR_SW` (5) | `2` | **Yes (X-Flip)** | `4lb` (mirrored) | Flipped `4lb0000.bmp`, `dx' = -(dx + W)` |
| **270° (W)** | `DIR_W` (6) | `9` | **Yes (X-Flip)** | `5lb` (mirrored) | Flipped `5lb0000.bmp`, `dx' = -(dx + W)` |
| **315° (NW)** | `DIR_NW` (7) | `8` | **Yes (X-Flip)** | `6lb` (mirrored) | Flipped `6lb0000.bmp`, `dx' = -(dx + W)` |

**Standalone Ground Pickup:** Dropped lunchbox on the ground uses Anim 356 (`lunchbox`), referencing Sprite 513 (`3lb0001.bmp`, $11 \times 16$).

---

### 3.4 In-Memory 8-Direction Atlas Architecture for $O(1)$ Lookup

To eliminate runtime branching, per-frame pixel copying, and shader uniform toggling, `libants-assets` pre-computes an 8-direction atlas during asset loading.

```cpp
enum Direction8 : uint8_t {
    DIR_N  = 0, //   0 deg (North)
    DIR_NE = 1, //  45 deg (North-East)
    DIR_E  = 2, //  90 deg (East)
    DIR_SE = 3, // 135 deg (South-East)
    DIR_S  = 4, // 180 deg (South)
    DIR_SW = 5, // 225 deg (South-West) -> Mirrored from DIR_SE
    DIR_W  = 6, // 270 deg (West)       -> Mirrored from DIR_E
    DIR_NW = 7  // 315 deg (North-West) -> Mirrored from DIR_NE
};

struct DirectionMapping {
    uint16_t   chd_dir_code; // 7, 8, 9, 2, 3
    bool       is_mirrored;  // true for SW, W, NW
    Direction8 source_dir;   // Source heading
};

static constexpr DirectionMapping DIRECTION_LOOKUP_TABLE[8] = {
    { 7, false, DIR_N  }, // DIR_N
    { 8, false, DIR_NE }, // DIR_NE
    { 9, false, DIR_E  }, // DIR_E
    { 2, false, DIR_SE }, // DIR_SE
    { 3, false, DIR_S  }, // DIR_S
    { 2, true,  DIR_SE }, // DIR_SW (Mirrored SE)
    { 9, true,  DIR_E  }, // DIR_W  (Mirrored E)
    { 8, true,  DIR_NE }  // DIR_NW (Mirrored NE)
};
```

#### Pre-Computation Process
1. **Sprite Table Mirroring:**
   - Table 1 contains 2,794 original sprites.
   - Any sprite referenced by a directional animation in facing `8`, `9`, or `2` is duplicated and horizontally flipped using $p'(x, y) = p(W - 1 - x, y)$.
   - Each flipped sprite is assigned a new sprite ID in an extended sprite table or registered in a parallel `mirrored_sprite_id` lookup array:
     $$\text{mirrored\_id} = \text{mirrored\_lut}[\text{original\_id}]$$
2. **Animation Sequence Mirroring:**
   - For every directional animation suite (e.g. Worker Walk `agwg`), the 5 base sequences are loaded: `agwg701` (N), `agwg801` (NE), `agwg901` (E), `agwg201` (SE), `agwg301` (S).
   - Three synthetic sequences are generated at initialization:
     - `agwg_SW`: Cloned from `agwg201` with $dx' = -(dx + W)$, $box\_left' = -box\_right, box\_right' = -box\_left$, and sprite indices remapped to mirrored sprites.
     - `agwg_W`: Cloned from `agwg901` with identical reflection rules.
     - `agwg_NW`: Cloned from `agwg801` with identical reflection rules.
3. **Runtime Direct Indexing:**
   Entities store:
   ```cpp
   struct DirectionalAnimSet {
       Animation sequences[8]; // Direct index by Direction8 (0..7)
   };
   ```
   Querying the active frame at tick $T$:
   ```cpp
   const RenderFrame& frame = anim_set.sequences[entity.heading].subitems[0].frames[tick % frame_count];
   ```
   Time complexity: **$O(1)$ constant time**.

---

## 4. Complete C++ Implementation Reference

### 4.1 Header: `include/ants/assets/lvl_map.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets {

#pragma pack(push, 1)

struct LVLCell {
    uint16_t word1; // Tile dictionary index (or 0x7FFE for empty)
    uint16_t word2; // Flags / variation
    uint16_t word3; // Properties / uninitialized MSVC filler (0xCDCD)
    
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return word1 == 0x7FFE;
    }
    
    [[nodiscard]] constexpr uint16_t tile_index() const noexcept {
        return word1;
    }
};

struct LVLSpawnRecord {
    uint16_t tile_id; // 152=BSTART, 153=USTART, 154=GSTART, 155=RSTART, etc.
    uint16_t y;       // Grid row coordinate
    uint16_t x;       // Grid column coordinate
};

struct LVLFoodVariant {
    uint16_t weight;  // Probability weight
    uint16_t tile_id; // Tile dictionary index or 0x7FFE
};

struct LVLFoodEntry {
    uint16_t y;
    uint16_t x;
    uint16_t initial_delay;
    uint16_t respawn_interval;
    uint16_t item_count;
    std::vector<LVLFoodVariant> variants;
};

struct LVLAmbient {
    uint16_t flag1;            // Level ambient flag (0)
    uint16_t tile_or_sound_id; // Level ambient audio/tile (0x7FFE)
};

struct LVLWaypoint {
    uint16_t y;
    uint16_t x;
    uint32_t flag;
    uint32_t param;
    double   probabilities[5]; // Branching probabilities summing to 1.0
};

#pragma pack(pop)

class LVLMap {
public:
    LVLMap() = default;
    
    [[nodiscard]] static bool load_from_file(const std::string& path, LVLMap& out_map);
    [[nodiscard]] static bool load_from_memory(const uint8_t* data, size_t size, LVLMap& out_map);
    
    // Accessors
    [[nodiscard]] uint32_t version() const noexcept { return version_; }
    [[nodiscard]] uint32_t game_mode() const noexcept { return game_mode_; }
    [[nodiscard]] uint16_t default_minutes() const noexcept { return default_minutes_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint16_t f_last() const noexcept { return f_last_; }
    
    // Dictionaries & Layers
    [[nodiscard]] const std::vector<std::string>& tile_dictionary() const noexcept { return tile_dictionary_; }
    [[nodiscard]] const std::vector<LVLCell>& layer1() const noexcept { return layer1_; }
    [[nodiscard]] const std::vector<LVLCell>& layer2() const noexcept { return layer2_; }
    
    [[nodiscard]] const LVLCell& get_layer1(uint32_t x, uint32_t y) const {
        return layer1_[y * width_ + x];
    }
    
    [[nodiscard]] const LVLCell& get_layer2(uint32_t x, uint32_t y) const {
        return layer2_[y * width_ + x];
    }
    
    // Trailing Blocks
    [[nodiscard]] const std::vector<LVLSpawnRecord>& spawns() const noexcept { return spawns_; }
    [[nodiscard]] const std::vector<LVLFoodEntry>& food_schedules() const noexcept { return food_schedules_; }
    [[nodiscard]] const LVLAmbient& ambient() const noexcept { return ambient_; }
    [[nodiscard]] const std::vector<LVLWaypoint>& waypoints() const noexcept { return waypoints_; }

private:
    uint32_t version_{0};
    uint32_t game_mode_{0};
    uint16_t default_minutes_{0};
    std::string description_;
    uint16_t tile_type_count_{0};
    std::vector<std::string> tile_dictionary_;
    uint32_t width_{0};
    uint32_t height_{0};
    
    std::vector<LVLCell> layer1_;
    std::vector<LVLCell> layer2_;
    
    std::vector<LVLSpawnRecord> spawns_;
    std::vector<LVLFoodEntry> food_schedules_;
    LVLAmbient ambient_{0, 0};
    std::vector<LVLWaypoint> waypoints_;
    uint16_t f_last_{0};
};

} // namespace ants::assets
```

---

### 4.2 Source: `src/lvl_map.cpp`

```cpp
#include "ants/assets/lvl_map.hpp"

#include <fstream>
#include <cstring>
#include <iostream>

namespace ants::assets {

#pragma pack(push, 1)
struct LVLHeaderFixed {
    uint32_t version;
    uint32_t game_mode;
    uint16_t default_minutes;
    char     description[30];
    uint16_t tile_type_count;
};
#pragma pack(pop)

bool LVLMap::load_from_file(const std::string& path, LVLMap& out_map) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;
    
    return load_from_memory(buffer.data(), buffer.size(), out_map);
}

bool LVLMap::load_from_memory(const uint8_t* data, size_t size, LVLMap& out) {
    if (!data || size < sizeof(LVLHeaderFixed)) return false;
    
    size_t pos = 0;
    const auto* hdr = reinterpret_cast<const LVLHeaderFixed*>(data + pos);
    pos += sizeof(LVLHeaderFixed);
    
    if (hdr->version != 8) return false;
    if (hdr->game_mode != 1) return false;
    
    out.version_ = hdr->version;
    out.game_mode_ = hdr->game_mode;
    out.default_minutes_ = hdr->default_minutes;
    out.description_ = std::string(hdr->description, ::strnlen(hdr->description, 30));
    out.tile_type_count_ = hdr->tile_type_count;
    
    // (N + 1) dictionary entries of 11 bytes each
    const size_t dict_count = static_cast<size_t>(out.tile_type_count_) + 1;
    const size_t dict_bytes = dict_count * 11;
    if (pos + dict_bytes > size) return false;
    
    out.tile_dictionary_.resize(dict_count);
    for (size_t i = 0; i < dict_count; ++i) {
        char buf[12] = {0};
        std::memcpy(buf, data + pos, 11);
        pos += 11;
        out.tile_dictionary_[i] = std::string(buf);
    }
    
    // Dimensions
    if (pos + 8 > size) return false;
    std::memcpy(&out.width_, data + pos, 4); pos += 4;
    std::memcpy(&out.height_, data + pos, 4); pos += 4;
    
    if (out.width_ == 0 || out.height_ == 0) return false;
    const size_t cell_count = static_cast<size_t>(out.width_) * out.height_;
    const size_t layer_bytes = cell_count * sizeof(LVLCell);
    
    // Layer 1
    if (pos + layer_bytes > size) return false;
    out.layer1_.resize(cell_count);
    std::memcpy(out.layer1_.data(), data + pos, layer_bytes);
    pos += layer_bytes;
    
    // Layer 2
    if (pos + layer_bytes > size) return false;
    out.layer2_.resize(cell_count);
    std::memcpy(out.layer2_.data(), data + pos, layer_bytes);
    pos += layer_bytes;
    
    // Block 1: Spawns
    if (pos + 2 > size) return false;
    uint16_t b1_cnt = 0;
    std::memcpy(&b1_cnt, data + pos, 2); pos += 2;
    
    const size_t b1_bytes = b1_cnt * sizeof(LVLSpawnRecord);
    if (pos + b1_bytes > size) return false;
    out.spawns_.resize(b1_cnt);
    std::memcpy(out.spawns_.data(), data + pos, b1_bytes);
    pos += b1_bytes;
    
    // Block 2: Food
    if (pos + 2 > size) return false;
    uint16_t b2_cnt = 0;
    std::memcpy(&b2_cnt, data + pos, 2); pos += 2;
    
    out.food_schedules_.resize(b2_cnt);
    for (size_t i = 0; i < b2_cnt; ++i) {
        if (pos + 10 > size) return false;
        auto& entry = out.food_schedules_[i];
        std::memcpy(&entry.y, data + pos, 2); pos += 2;
        std::memcpy(&entry.x, data + pos, 2); pos += 2;
        std::memcpy(&entry.initial_delay, data + pos, 2); pos += 2;
        std::memcpy(&entry.respawn_interval, data + pos, 2); pos += 2;
        std::memcpy(&entry.item_count, data + pos, 2); pos += 2;
        
        const size_t var_bytes = entry.item_count * sizeof(LVLFoodVariant);
        if (pos + var_bytes > size) return false;
        entry.variants.resize(entry.item_count);
        std::memcpy(entry.variants.data(), data + pos, var_bytes);
        pos += var_bytes;
    }
    
    // Block 3: Ambient
    if (pos + sizeof(LVLAmbient) > size) return false;
    std::memcpy(&out.ambient_, data + pos, sizeof(LVLAmbient));
    pos += sizeof(LVLAmbient);
    
    // Block 4: Waypoints
    if (pos + 2 > size) return false;
    uint16_t b4_cnt = 0;
    std::memcpy(&b4_cnt, data + pos, 2); pos += 2;
    
    out.waypoints_.resize(b4_cnt);
    for (size_t i = 0; i < b4_cnt; ++i) {
        if (pos + 8 > size) return false;
        auto& wp = out.waypoints_[i];
        std::memcpy(&wp.y, data + pos, 2); pos += 2;
        std::memcpy(&wp.x, data + pos, 2); pos += 2;
        std::memcpy(&wp.flag, data + pos, 4); pos += 4;
        
        if (wp.flag != 0) {
            if (pos + 4 + 40 > size) return false;
            std::memcpy(&wp.param, data + pos, 4); pos += 4;
            std::memcpy(wp.probabilities, data + pos, 40); pos += 40;
        } else {
            wp.param = 0;
            std::memset(wp.probabilities, 0, sizeof(wp.probabilities));
        }
    }
    
    // Level boundary parameter
    if (pos + 2 > size) return false;
    std::memcpy(&out.f_last_, data + pos, 2); pos += 2;
    
    // Strictly assert 0 remaining bytes
    return (pos == size);
}

} // namespace ants::assets
```

---

### 4.3 Header: `include/ants/assets/directional_mirroring.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ants::assets {

enum Direction8 : uint8_t {
    DIR_N  = 0, // 0 deg
    DIR_NE = 1, // 45 deg
    DIR_E  = 2, // 90 deg
    DIR_SE = 3, // 135 deg
    DIR_S  = 4, // 180 deg
    DIR_SW = 5, // 225 deg (mirrored from SE)
    DIR_W  = 6, // 270 deg (mirrored from E)
    DIR_NW = 7  // 315 deg (mirrored from NE)
};

struct Sprite {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    std::string filename;
    std::vector<uint8_t> pixels;
};

struct Frame {
    int32_t  dx{0};
    int32_t  dy{0};
    uint32_t sprite_index{0};
};

struct SubItem {
    int32_t box_left{0};
    int32_t box_top{0};
    int32_t box_right{0};
    int32_t box_bottom{0};
    uint32_t default_sp{0xFFFFFFFF};
    std::vector<Frame> frames;
};

struct Animation {
    std::string name;
    uint32_t flag1{0};
    uint32_t flag2{0};
    uint32_t flag3{0};
    std::vector<SubItem> subitems;
};

class DirectionalMirroringEngine {
public:
    // Reflects sprite pixels: p'(x, y) = p(W - 1 - x, y)
    [[nodiscard]] static Sprite mirror_sprite(const Sprite& src, const std::string& name);
    
    // Reflects animation frames and bounding boxes
    [[nodiscard]] static Animation mirror_animation(
        const Animation& src,
        const std::string& mirrored_name,
        const std::vector<Sprite>& sprite_table,
        const std::vector<uint32_t>& sprite_mirror_lut);
        
    // Returns corresponding lunchbox sprite prefix for any of the 8 directions
    [[nodiscard]] static constexpr const char* get_lunchbox_prefix(Direction8 dir) noexcept {
        switch (dir) {
            case DIR_N:  return "7lb";
            case DIR_NE: return "6lb";
            case DIR_E:  return "5lb";
            case DIR_SE: return "4lb";
            case DIR_S:  return "3lb";
            case DIR_SW: return "4lb"; // Mirrored
            case DIR_W:  return "5lb"; // Mirrored
            case DIR_NW: return "6lb"; // Mirrored
        }
        return "7lb";
    }
    
    // Check whether a heading requires mirrored sprites
    [[nodiscard]] static constexpr bool is_mirrored_direction(Direction8 dir) noexcept {
        return (dir == DIR_SW || dir == DIR_W || dir == DIR_NW);
    }
};

} // namespace ants::assets
```

---

### 4.4 Source: `src/directional_mirroring.cpp`

```cpp
#include "ants/assets/directional_mirroring.hpp"
#include <cassert>

namespace ants::assets {

Sprite DirectionalMirroringEngine::mirror_sprite(const Sprite& src, const std::string& name) {
    Sprite dst;
    dst.width = src.width;
    dst.height = src.height;
    dst.pitch = src.pitch;
    dst.filename = name;
    dst.pixels.resize(src.pitch * src.height, 0);
    
    for (uint32_t y = 0; y < src.height; ++y) {
        const uint8_t* src_row = src.pixels.data() + y * src.pitch;
        uint8_t* dst_row = dst.pixels.data() + y * dst.pitch;
        for (uint32_t x = 0; x < src.width; ++x) {
            dst_row[x] = src_row[src.width - 1 - x];
        }
    }
    return dst;
}

Animation DirectionalMirroringEngine::mirror_animation(
    const Animation& src,
    const std::string& mirrored_name,
    const std::vector<Sprite>& sprite_table,
    const std::vector<uint32_t>& sprite_mirror_lut) 
{
    Animation dst;
    dst.name = mirrored_name;
    dst.flag1 = src.flag1;
    dst.flag2 = src.flag2;
    dst.flag3 = src.flag3;
    dst.subitems.resize(src.subitems.size());
    
    for (size_t s = 0; s < src.subitems.size(); ++s) {
        const auto& src_sub = src.subitems[s];
        auto& dst_sub = dst.subitems[s];
        
        // Bounding box reflection
        dst_sub.box_left   = -src_sub.box_right;
        dst_sub.box_right  = -src_sub.box_left;
        dst_sub.box_top    = src_sub.box_top;
        dst_sub.box_bottom = src_sub.box_bottom;
        dst_sub.default_sp = src_sub.default_sp;
        
        dst_sub.frames.resize(src_sub.frames.size());
        for (size_t f = 0; f < src_sub.frames.size(); ++f) {
            const auto& src_f = src_sub.frames[f];
            auto& dst_f = dst_sub.frames[f];
            
            assert(src_f.sprite_index < sprite_table.size());
            const uint32_t W = sprite_table[src_f.sprite_index].width;
            
            // Frame render offset inversion: dx' = -(dx + W), dy' = dy
            dst_f.dx = -(src_f.dx + static_cast<int32_t>(W));
            dst_f.dy = src_f.dy;
            
            // Remap to mirrored sprite ID
            assert(src_f.sprite_index < sprite_mirror_lut.size());
            dst_f.sprite_index = sprite_mirror_lut[src_f.sprite_index];
        }
    }
    return dst;
}

} // namespace ants::assets
```

---

## 5. Comprehensive Unit Test Suite Specifications

The unit tests below specify exact assertions for `libants-assets/tests/test_assets.cpp`:

### 5.1 Test Cases for `.LVL` Map Decoder

1. **`TEST_CASE("LVL Map File Sizing and rem = 0 across all 6 maps")`**
   - Loads `TINY.LVL`, `SMALL.LVL`, `MEDIUM.LVL`, `GAUNTLET.LVL`, `ISLANDS.LVL`, `TREASURE.LVL`.
   - Asserts return code `true`.
   - Asserts remaining bytes `rem == 0` for all 6 maps.
2. **`TEST_CASE("LVL Map Header and Dictionary Integrity")`**
   - Validates `version == 8` and `game_mode == 1`.
   - Validates descriptions:
     - `TINY.LVL`: `"Tiny map with no PowerUps"`, $31 \times 31$, tile dictionary count 670.
     - `SMALL.LVL`: `"Small map for fast game"`, $40 \times 40$, tile dictionary count 1,326.
     - `MEDIUM.LVL`: `"Intermediate map"`, $60 \times 60$, tile dictionary count 1,325.
     - `GAUNTLET.LVL`: `"Race for your life!"`, $60 \times 60$, tile dictionary count 1,330.
     - `ISLANDS.LVL`: `"Island hopping, expert map"`, $60 \times 60$, tile dictionary count 1,330.
     - `TREASURE.LVL`: `"One person's trash..."`, $60 \times 60$, tile dictionary count 1,335.
3. **`TEST_CASE("LVL Layer 2 Sentinel 0x7FFE and Anthill Coordinates")`**
   - Asserts empty cells in Layer 2 have `word1 == 0x7FFE`.
   - Asserts non-empty cell count is within expected bounds:
     - `TINY`: 104 cells
     - `SMALL`: 126 cells
     - `MEDIUM`: 179 cells
     - `GAUNTLET`: 319 cells
     - `ISLANDS`: 575 cells
     - `TREASURE`: 1,357 cells
   - Validates existence of `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247), `GREENHILL` (248) in Layer 2 for all maps.
4. **`TEST_CASE("LVL Trailing Block 1 Starting Anthill Spawns")`**
   - Asserts starting spawn count: `TINY` = 12, `SMALL` = 18, `MEDIUM` = 30, `GAUNTLET` = 32, `ISLANDS` = 40, `TREASURE` = 29.
   - Asserts `BSTART` (152), `USTART` (153), `GSTART` (154), `RSTART` (155) spawn coordinates match anthill bounding regions.
5. **`TEST_CASE("LVL Trailing Block 2 Food Respawn Pools")`**
   - Asserts food pool count: `TINY` = 14, `SMALL` = 5, `MEDIUM` = 4, `GAUNTLET` = 2, `ISLANDS` = 10, `TREASURE` = 18.
   - Asserts food pool coordinates align with Layer 2 food item tiles.
6. **`TEST_CASE("LVL Trailing Block 3 & 4 Ambient and Waypoints")`**
   - Asserts `ambient.flag1 == 0` and `ambient.tile_or_sound_id == 0x7FFE`.
   - Asserts waypoint count: `TINY` = 0, `SMALL` = 2, `MEDIUM` = 15, `GAUNTLET` = 20, `ISLANDS` = 22, `TREASURE` = 19.
   - For all waypoints with `flag != 0`, validates that the 5 `probabilities` elements sum to $1.0 \pm 10^{-6}$.
   - Asserts `f_last`: `TINY` = 3, `SMALL` = 2, `MEDIUM` = 6, `GAUNTLET` = 6, `ISLANDS` = 4, `TREASURE` = 9.

### 5.2 Test Cases for 5-to-8 Directional Sprite Mirroring Engine

1. **`TEST_CASE("Pixel Reflection Symmetry and Involution")`**
   - Constructs test sprite with known asymmetric pixel ramp $[10, 20, 30, 40, 50, 0, 0, 0]$.
   - Mirrored sprite has $[50, 40, 30, 20, 10, 0, 0, 0]$.
   - Involution test: `mirror_sprite(mirror_sprite(sp)) == sp`.
2. **`TEST_CASE("Frame Render Offset Inversion Formula")`**
   - For width $W = 24$, $dx = 6$, $dy = -10$:
     - $dx' = -(6 + 24) = -30$.
     - $dy' = -10$.
   - Asserts reflected bounds $[X - 30, X - 6]$ are symmetric to native bounds $[X + 6, X + 30]$ about $X = 0$.
   - Involution test: $-(-(dx + W) + W) = dx$.
3. **`TEST_CASE("Bounding Box Inversion Formula")`**
   - For subitem bounding box $[-20, 5, 10, 35]$:
     - $box\_left' = -10$.
     - $box\_right' = 20$.
     - $box\_top' = 5, box\_bottom' = 35$.
   - Asserts $box\_left' \le box\_right'$.
4. **`TEST_CASE("Lunchbox Carrying Suite Mapping")`**
   - Verifies 8-direction lunchbox prefixes: `7lb` (N), `6lb` (NE), `5lb` (E), `4lb` (SE), `3lb` (S), mirrored `4lb` (SW), mirrored `5lb` (W), mirrored `6lb` (NW).
5. **`TEST_CASE("Real CHD Asset Mirroring Verification")`**
   - Loads `ants.chd`.
   - Iterates through all frames in `agwg801`, `agwg901`, `agwg201`.
   - Verifies $dx' + W == -dx$ for all 36 subitem frames without crash or overflow.
