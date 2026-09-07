# Ants (1995/1998) — Asset & Format Specification
**Document Version:** 1.0  
**Author:** Asset Specification Miner (`miner_survey_1`)  
**Target Project:** Ants Remake (`ants-assets`, `ants-sim`, `ants-audio`, `ants-app`)  
**Authoritative References:** `Original-Ants/ants.chd`, `Original-Ants/Maps/*.LVL`, `Original-Ants/Ants.exe`, `GAME_REVERSE_ENGINEERING.md`  

---

## Executive Summary
This document provides the exact, byte-level specification for all raw binary assets utilized by Ants (1995/1998). It details the internal container structure of `ants.chd` (header, 256-color palette, 2,794 raw paletted sprites, 91 PCM audio clips, 4 event tags, and 1,344 animation sequences with frame sound triggers), the complete `.LVL` map binary layout (header, tile dictionary, 6-byte terrain Layer 1, 6-byte interactive Layer 2, and 4 trailing configuration blocks validated across all 6 original levels), the 5-to-8 directional horizontal mirroring algorithm for O(1) rendering, and the complete audio mapping for Sound IDs 0 through 90.

---

## Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Container | CHD Header | 28-byte primary header indexing all asset tables and palette | `ants.chd` offset `0x00000000` | 7 `uint32` fields (version=9, timestamp, 4 table offsets, palette size=1024) | Version check rejects version < 9 (`0x102dabe`) | Capstone disasm `0x102da16` & binary probe |
| 2 | Graphics | 256-Color Palette | Master 1024-byte color table stored as `[R, G, B, Flags]` per entry | `ants.chd` offset `0x0000001C` | 254 color values, Index 254 DirectDraw magenta key, 4 team ranges | Swapping R and B channels reverses team colors (e.g. Red becomes Blue) | PE disasm `0x102da16` & palette analysis |
| 3 | Graphics | Paletted Sprite Table | Table 1 containing 2,794 raw 8-bit paletted sprite bitmaps | `ants.chd` offset `0x0000041C` (1052) | `count` (2,794) + offsets array + sprite records (`pitch`, `width`, `height`, `filename`, pixels) | Stride padding `pitch > width` padded with 0x00 | Binary probe & `0x1008e91` disassembly |
| 4 | Audio | Digital Audio Table | Table 2 containing 91 uncompressed 8-bit PCM audio waveforms | `ants.chd` offset `0x00684EE1` (6,835,937) | `count` (91) + offsets array + sound records (`WAVEFORMATEX`, PCM bytes, filename) | Missing filenames (15 unnamed clips) require numeric ID fallback | Direct extraction & `0x10065b1` stream reader |
| 5 | Animation | Event Tag Descriptors | Table 3 defining symbolic event tag IDs for animation synchronization | `ants.chd` offset `0x00789A1D` (7,903,773) | Tag ID 3 (`HITGROUND`), ID 4 (`ATTACKHIT`), ID 5 (`HEAL`), ID 10 terminator | Unrecognized tag IDs ignored by state dispatcher | Table 3 binary parse (`0x102da16`) |
| 6 | Animation | Animation Sequences | Table 4 containing 1,344 animation sequences with frame sound triggers | `ants.chd` offset `0x00789A5B` (7,903,835) | `count` (1,344) + offsets array + animation records (subitems, bounding boxes, `default_sp`, frames) | `default_sp == 0xFFFFFFFF` denotes silent frame; values < 91 trigger sound | Table 4 parse & disasm `0x101ad02` |
| 7 | Direction | 5-to-8 Sprite Mirroring | Horizontal reflection algorithm generating 8 compass facings from 5 stored facings | Stored directions: 7 (N), 8 (NE), 9 (E), 2 (SE), 3 (S) | Pre-generated directions: SW (from 2), W (from 9), NW (from 8) | Rendering unmirrored offsets causes left-side visual misalignment | Directional analysis of Table 4 & disasm |
| 8 | Maps | LVL Header & Dictionary | Primary map header with embedded 11-byte ASCII tile name dictionary | `Maps/*.LVL` offset `0x00000000` | Version (8), mode (1), minutes, description, tile count, tile names array, width, height | Version != 8 or tile count mismatch aborts map load (`0x1006386`) | Disassembly `0x1006349` & map binary parse |
| 9 | Maps | Layer 1 Terrain Grid | 6-byte cell array representing foundational map terrain | LVL offset `hdr_size` | `width * height * 6` bytes: `terrain_id` (16-bit), `variation` (16-bit), `flags` (16-bit) | Out-of-bounds terrain IDs wrap or clamp to default grass | Disassembly `0x10069d8` & cell dissection |
| 10 | Maps | Layer 2 Interactive Grid | 6-byte cell array representing interactive structures and items | LVL offset `hdr_size + w*h*6` | `width * height * 6` bytes: empty tile sentinel `0x7FFE` (32,766), items, anthills, bridges | Corrupted tile IDs result in invisible/blocking barriers | Disassembly `0x10069d8` & cell dissection |
| 11 | Maps | Trailing Block 1: Spawns | List of starting anthill coordinates for all teams | Trailing offset after Layer 2 | `count` (16-bit) + records (`tile_id`, `y`, `x`) for Black, Blue, Red, Green bases | Incomplete base spawns prevent team egg incubation | Disassembly `0x1006c7d` & map validation |
| 12 | Maps | Trailing Block 2: Food | Dynamic food and item respawn schedule pools | Trailing offset after Block 1 | `count` (16-bit) + pools (`x`, `y`, `init_delay`, `respawn_int`, item variants array) | Missing food schedules halts match scoring progression | Disassembly `0x1006d19` & map validation |
| 13 | Maps | Trailing Block 3: Ambient | Level ambient sound / visual configuration flags | Trailing offset after Block 2 | 2 `uint16` values (`[ebp - 4]`, `[ebp - 2]` tile/sound identifier) | If tile ID != `0x7FFE`, resolves via dictionary | Disassembly `0x1007025` & map validation |
| 14 | Maps | Trailing Block 4: Waypoints | Level pathing waypoints and patrol boundary triggers | Trailing offset after Block 3 | `count` (16-bit) + records (`x`, `y`, `flag`, optional 5-point coordinate array) | Zero count (e.g. `TINY.LVL`) skips loop | Disassembly `0x1006f0e` & map validation |
| 15 | Audio | Base Infiltration Alert | 2,566 Hz alarm siren dispatched when enemy Thief Ant dives into base | Sound ID 58 (`underattack.wav`, 22,050 Hz 8-bit mono) | High-priority audio interrupt + News Flash HUD banner | Audio buffer starvation drops alarm tone | Disassembly `0x0101b757` & CHD Table 2 |
| 16 | Audio | Food Theft Score Drain | Descending score penalty chord dispatched as points drain | Sound ID 88 (`scoredn.wav`, 11,025 Hz 8-bit mono) | Deducts `min(50, score)` from victim, plays Sound 88 | Victim score capped at 0 (non-negative) | Disassembly `0x0101d57c` & CHD Table 2 |
| 17 | Audio | End-of-Game Fanfare Split | Match conclusion audio split: victory fanfare vs. defeat sting | Sound ID 56 (`winner.wav`) vs. Sound ID 41 (`playerout.wav`) | 4.67s victory fanfare (winning team) or 0.94s defeat sting (losing teams) | Playing winner audio to defeated players violates spec | Disassembly `0x1024d85` & `re_screen` |
| 18 | Audio | Bomb Disarm Squash Sound | Dual-sound audio sequence for Bomber Ant crushing enemy landmines | Sound ID 73 (`bombdrop.wav`) + Sound ID 74 (`bombmuffle.wav`) | Sound 73 @ Subitem 3, Sound 74 @ Subitem 6 of `abdb*` animation suite | Neutralizes bomb, clearing tile to empty `0x7FFE` | Animation 789-791 parse & disasm |

---

## Edge Cases

| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | Palette Index 0 vs 254 | Sprite rendering with index `0` vs index `254` (`0xFE`) | Index `254` is the true DirectDraw color key (`DDCOLORKEY` Magenta `RGB(255,0,255)`) rendered as `Alpha = 0`. Index `0` is an opaque dark grayish-blue (`RGB(119,119,127)`). However, sprite row pitch padding bytes at row ends (`pitch > width`) are filled with `0x00`. Treat row padding as discarded stride, and index 254 as transparent. |
| 2 | Palette RGB/BGR Channel Order | Reading palette entries as `[B, G, R, Flags]` instead of `[R, G, B, Flags]` | Swaps red and blue channels: Red team sprites (Team 2, indices 178..181) appear cyan/blue, and Blue team sprites (Team 1, indices 34..39) appear orange/red. File stores `[R, G, B, Flags]`. |
| 3 | Sprite Row Stride Padding | Sprite 0 `dclay48.bmp` with `width = 45`, `pitch = 48` | Row buffer contains 45 visible pixel indices followed by 3 bytes of padding (`0x00`). Reading `pitch * height` is required to advance the file pointer correctly, but rendering must use `x + y * pitch` with row stride clamping to `width`. |
| 4 | Unnamed Sound Clips in Table 2 | Sound IDs 6, 17, 40, 42, 43, 45, 46, 48, 54, 55, 59, 60, 75, 77, 80 | Sound record has `filename_len = 1` and `filename = "\0"`. The engine relies strictly on numeric Sound ID (0..90). Sound 6 is the Ants startup fanfare (65,190 bytes stereo). Sound 75 is an attack hit sound. Sound 77 is airborne fling whoosh. Sound 80 is water dive splash. |
| 5 | Stereo vs Mono PCM in Table 2 | Sound 6 (Ants logo), Sound 45, Sound 46 | Contain `nChannels = 2` (Stereo), whereas all other 88 clips are `nChannels = 1` (Mono). Mixer must handle 2-channel 8-bit unsigned PCM interleaved `(L, R, L, R...)`. |
| 6 | Horizontal Mirroring Offset Inversion | Mirrored direction NW (from NE Dir 8, `width = W`, frame offset `dx`) | Naive horizontal flipping without adjusting `dx` causes sprite to render shifted by `W` pixels to the right. Correct anchor transformation requires $dx' = -(dx + W)$ and $box\_left' = -box\_right, box\_right' = -box\_left$. |
| 7 | Directional Digit Anomaly: `atcr501` | Animation 1095 Thief Ant crawl / infiltration has direction digit `5` | Unlike the 5 standard directional digits (2, 3, 7, 8, 9), `atcr501` represents the non-directional underground dive into an enemy anthill. It is an omnidirectional / non-mirrored animation. |
| 8 | Map Layer 2 Empty Tile Sentinel | Layer 2 cell with `tile_id = 0x7FFE` (32,766) | In `0x10069d8`, `0x7FFE` is shifted left by 1 to yield `0xFFFC`. Any cell with `0x7FFE` is recognized as an empty interactive tile (no obstacle, bridge, food, or bomb). |
| 9 | Map Layer Cell Flag Bit 0 | Word 1 and Word 2 of 6-byte map cell | Word 1 contains `(tile_dictionary_id << 1) | flag_bit_0`. Bit 0 is extracted and combined with the tile properties during runtime map loading. |
| 10 | Multi-Item Food Spawn Schedule | Block 2 food spawn entry with `item_count > 1` (e.g. `TINY.LVL` Entry 12) | Contains multiple weighted food variants ending with null sentinel `weight = 0, tile_id = 0x7FFE`. Spawner chooses active food based on cumulative stage weight. |
| 11 | Map Trailing Block 4 in Tiny Maps | `TINY.LVL` with `c4 = 0` | Block 4 count is 0. Parser must handle count = 0 without attempting to read records, immediately advancing to final 2-byte field `f_last`. |

---

## 1. `ants.chd` Binary Structure

The master asset container `Original-Ants/ants.chd` has a file size of **8,411,866 bytes** (`0x00805B5A`). It contains all palettes, raw sprite bitmaps, digitized audio clips, and animation sequence scripts required by the game.

### 1.1 Header Format (28 Bytes)
Located at absolute file offset `0x00000000`:

```c
struct CHDHeader {
    uint32_t version;          // Offset 0x00: Value = 9 (0x00000009)
    uint32_t timestamp;        // Offset 0x04: Value = 0x378D661C (932,013,596)
    uint32_t table1_offset;    // Offset 0x08: Value = 1,052 (0x0000041C) -> Sprite Bitmaps Table
    uint32_t table2_offset;    // Offset 0x0C: Value = 6,835,937 (0x00684EE1) -> Sound Effects Table
    uint32_t table3_offset;    // Offset 0x10: Value = 7,903,773 (0x00789A1D) -> Event Tag Descriptors
    uint32_t table4_offset;    // Offset 0x14: Value = 7,903,835 (0x00789A5B) -> Animation Sequences Table
    uint32_t palette_bytes;    // Offset 0x18: Value = 1,024 (0x00000400) -> Master Palette Byte Count
};
```

**Header Verification:**
- Version must be $\ge 9$. In `Ants.exe` disassembly at `0x102da5d`, if `version < 9`, error `0x2717` is raised.
- `palette_bytes` is exactly 1,024 bytes (256 entries $\times$ 4 bytes).
- Table 1 immediately follows the palette at offset $28 + 1024 = 1052$ (`0x0000041C`).

---

### 1.2 Master 256-Color Palette (`0x0000001C` – `0x0000041C`)
Directly follows the 28-byte header. Contains 256 color quad structures matching the Windows `PALETTEENTRY` format:

```c
struct PaletteEntry {
    uint8_t r;       // Red channel (0 - 255)
    uint8_t g;       // Green channel (0 - 255)
    uint8_t b;       // Blue channel (0 - 255)
    uint8_t flags;   // DirectDraw flags (always 0x00 in file)
};
```

#### Color Key & Transparency Mechanics
- **DirectDraw Transparency Key:** **Index 254 (`0xFE`)** is the authentic color key (`DDCOLORKEY`).
  - RGB Value: `(255, 0, 255)` (Pure Magenta).
  - Target Rendering Engine: Must be mapped to `RGBA(255, 0, 255, 0)` (Alpha = 0).
- **Index 0:** Stored as `RGB(119, 119, 127)`. Used for opaque background elements and boundary fills.
- **Row Padding Zeroes:** Bitmaps with `pitch > width` have row padding bytes filled with byte `0x00`. In a 2D rendering pipeline, these padding bytes must be skipped based on `pitch`, not drawn as transparent pixels.

#### Team Color Palette Indices
Teams swap palette indices to display identical ant sprites in distinct team colors:

| Team ID | Team Color | Palette Indices | RGB Color Ramp (Lightest to Darkest) |
|:---:|:---:|:---:|:---|
| **0** | **Black** | `237 .. 239` | `RGB(79, 87, 111)` $\rightarrow$ `RGB(43, 59, 75)` $\rightarrow$ `RGB(19, 35, 39)` |
| **1** | **Blue** | `34 .. 39` | `RGB(119, 175, 239)` $\rightarrow$ `RGB(95, 139, 231)` $\rightarrow$ `RGB(75, 99, 223)` $\rightarrow$ `RGB(59, 59, 191)` $\rightarrow$ `RGB(59, 43, 159)` $\rightarrow$ `RGB(59, 31, 131)` |
| **2** | **Red** | `178 .. 181` | `RGB(251, 51, 91)` $\rightarrow$ `RGB(255, 0, 0)` $\rightarrow$ `RGB(187, 0, 0)` $\rightarrow$ `RGB(119, 0, 0)` |
| **3** | **Green** | `49 .. 52` | `RGB(83, 147, 43)` $\rightarrow$ `RGB(63, 119, 47)` $\rightarrow$ `RGB(35, 91, 51)` $\rightarrow$ `RGB(7, 63, 51)` |

---

### 1.3 Table 1: Sprite Bitmaps (Offset 1,052 / `0x0000041C`)
Stores 2,794 individual raw 8-bit paletted sprite bitmaps.

#### Table Layout
- `uint32_t count`: Always **2,794** (`0x00000AEA`).
- `uint32_t offsets[2794]`: 32-bit absolute file offsets to each sprite entry.
  - First offset: `12,232` (`0x00002FC8`)
  - Last offset: `6,834,308` (`0x00684884`)

#### Sprite Entry Structure
```c
struct CHDSpriteEntry {
    uint32_t pitch;            // Row stride in bytes (width + padding, e.g. 48)
    uint32_t width;            // Visible bounding width in pixels (e.g. 45)
    uint32_t height;           // Visible bounding height in pixels (e.g. 47)
    uint32_t filename_len;     // Length of original ASCII filename string including null terminator
    char     filename[filename_len]; // e.g. "dclay48.bmp\0"
    uint8_t  pixels[pitch * height]; // Raw 8-bit uncompressed paletted pixel data
};
```

**Row Pitch Stride Rule:**
Visible pixels per row are `width`. If `pitch > width`, the trailing `pitch - width` bytes at the end of each scanline are padding bytes (`0x00`).
Pixel coordinate $(x, y)$ within the sprite data buffer is located at:
$$\text{offset} = y \times \text{pitch} + x \quad (0 \le x < \text{width}, \; 0 \le y < \text{height})$$

---

### 1.4 Table 2: Digital Audio Clips (Offset 6,835,937 / `0x00684EE1`)
Stores 91 sound effect waveforms in raw PCM format.

#### Table Layout
- `uint32_t count`: Always **91** (`0x0000005B`).
- `uint32_t offsets[91]`: 32-bit absolute file offsets to each sound entry.
  - First offset: `6,836,305` (`0x00685051`)
  - Last offset: `7,898,210` (`0x00788462`)

#### Sound Entry Structure
```c
struct CHDSoundEntry {
    uint32_t format_len;       // Length of wave_format struct (typically 18 bytes)
    struct WAVEFORMATEX {
        uint16_t wFormatTag;       // 1 = WAVE_FORMAT_PCM
        uint16_t nChannels;        // 1 = Mono, 2 = Stereo
        uint32_t nSamplesPerSec;   // 11,025 Hz or 22,050 Hz
        uint32_t nAvgBytesPerSec;  // nSamplesPerSec * nBlockAlign
        uint16_t nBlockAlign;      // nChannels * (wBitsPerSample / 8)
        uint16_t wBitsPerSample;   // 8 bits per sample
        uint16_t cbSize;           // Extra format bytes (0)
    } wave_format;
    uint32_t pcm_data_len;     // Size of raw audio data buffer in bytes
    uint8_t  pcm_data[pcm_data_len]; // Unsigned 8-bit PCM audio samples (center = 128)
    uint32_t filename_len;     // Length of ASCII filename string including null terminator
    char     filename[filename_len]; // Original WAV filename, e.g. "bombexp.wav\0" (or "\0" if unnamed)
};
```

#### Synthesizing Standard RIFF WAV Files
Each entry converts directly to a 100% compliant `.wav` file by prepending a 44-byte standard RIFF header:
1. `ChunkID`: `"RIFF"` (4 bytes)
2. `ChunkSize`: `36 + pcm_data_len` (4 bytes, little-endian)
3. `Format`: `"WAVE"` (4 bytes)
4. `Subchunk1ID`: `"fmt "` (4 bytes)
5. `Subchunk1Size`: `16` (4 bytes, little-endian)
6. `AudioFormat`: `1` (2 bytes, PCM)
7. `NumChannels`: `wave_format.nChannels` (2 bytes)
8. `SampleRate`: `wave_format.nSamplesPerSec` (4 bytes)
9. `ByteRate`: `wave_format.nAvgBytesPerSec` (4 bytes)
10. `BlockAlign`: `wave_format.nBlockAlign` (2 bytes)
11. `BitsPerSample`: `wave_format.wBitsPerSample` (2 bytes)
12. `Subchunk2ID`: `"data"` (4 bytes)
13. `Subchunk2Size`: `pcm_data_len` (4 bytes)
14. Followed by `pcm_data`.

---

### 1.5 Table 3: Event Tag Descriptors (Offset 7,903,773 / `0x00789A1D`)
Defines synchronization event tag strings used by the animation engine:
- Length: **62 bytes** (`0x00789A1D` to `0x00789A5B`).
- Format:
  - `uint32_t max_tag_id`: `10`
  - Series of records: `uint32_t tag_id`, `uint32_t name_len`, `char name[name_len]`
- Entries:
  - Tag ID `3`: `"HITGROUND\0"` (name_len = 10)
  - Tag ID `4`: `"ATTACKHIT\0"` (name_len = 10)
  - Tag ID `5`: `"HEAL\0"` (name_len = 5)
  - Tag ID `10`: `"\0"` (name_len = 1) — Null terminator sentinel

---

### 1.6 Table 4: Animation Sequences (Offset 7,903,835 / `0x00789A5B`)
Stores 1,344 animation sequences defining visual playback and frame sound triggers.

#### Table Layout
- `uint32_t count`: Always **1,344** (`0x00000540`).
- `uint32_t offsets[1344]`: 32-bit absolute file offsets to each animation entry.
  - First offset: `7,909,215` (`0x0078AF5F`)
  - Last offset: `8,409,836` (`0x0080536C`)

#### Animation Entry Structure
```c
struct CHDAnimationEntry {
    uint32_t name_len;         // Length of ASCII animation identifier name
    char     name[name_len];   // e.g. "agwg301\0", "resbanr\0", "op_screen\0"
    uint32_t flag1;            // Playback flags (typically 0)
    uint32_t flag2;            // Loop flags (typically 1)
    uint32_t flag3;            // Interpolation flags (typically 1)
    uint32_t subitem_count;    // Number of subitem tracks / layers (1 to 85)
    
    struct SubItem {
        uint32_t val1;         // Parameter 1 (typically 0)
        uint32_t val2;         // Parameter 2 (typically 0)
        uint32_t val3;         // Duration / time delta (typically 1000)
        uint32_t box_left;     // Interaction bounding box Left coordinate
        uint32_t box_top;      // Interaction bounding box Top coordinate
        uint32_t box_right;    // Interaction bounding box Right coordinate
        uint32_t box_bottom;   // Interaction bounding box Bottom coordinate
        uint32_t v8;           // Internal layer state flag (e.g. 1111111)
        uint32_t default_sp;   // Sound trigger: 0xFFFFFFFF = None, 0..90 = Sound ID in Table 2
        uint32_t frame_count;  // Number of visual frame renders in this subitem
        
        struct Frame {
            int32_t  dx;            // Horizontal render offset relative to unit origin
            int32_t  dy;            // Vertical render offset relative to unit origin
            uint32_t sprite_index;  // Index into Table 1 (0 to 2,793)
        } frames[frame_count];
    } subitems[subitem_count];
};
```

#### Audio Synchronization Architecture (`default_sp`)
Each animation subitem carries a 32-bit `default_sp` field:
- `default_sp == 0xFFFFFFFF` (4,294,967,295): No audio is triggered during this subitem.
- `default_sp < 91`: Automatically dispatches Sound ID `default_sp` from Table 2 at the exact frame tick when this subitem begins execution.
- Exactly **365 subitems** across the 1,344 animations feature active audio triggers.

---

## 2. Map File Format (`Maps/*.LVL`)

All 6 official maps included with Ants follow the exact binary layout detailed below.

### 2.1 The 6 Official Maps

| Level Filename | Version | Game Mode | Default Time | Grid Dimensions | Tile Dict Count | Description String | Total File Size |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---:|
| `TINY.LVL` | 8 | 1 (Standard) | 6 min | **31 × 31** | 669 tiles | `"Tiny map with no PowerUps"` | 19,312 bytes |
| `SMALL.LVL` | 8 | 1 (Standard) | 8 min | **40 × 40** | 1,325 tiles | `"Small map for fast game"` | 34,190 bytes |
| `MEDIUM.LVL` | 8 | 1 (Standard) | 10 min | **60 × 60** | 1,324 tiles | `"Intermediate map"` | 58,381 bytes |
| `GAUNTLET.LVL` | 8 | 1 (Standard) | 10 min | **60 × 60** | 1,329 tiles | `"Race for your life!"` | 58,508 bytes |
| `ISLANDS.LVL` | 8 | 1 (Standard) | 12 min | **60 × 60** | 1,329 tiles | `"Island hopping, expert map"` | 58,776 bytes |
| `TREASURE.LVL` | 8 | 1 (Standard) | 12 min | **60 × 60** | 1,334 tiles | `"One person's trash..."` | 58,853 bytes |

---

### 2.2 Header & Tile Dictionary Structure

```c
struct LVLHeader {
    uint32_t version;          // Offset 0x00: Must be 8 (0x00000008)
    uint32_t game_mode;        // Offset 0x04: 1 = Standard Food Gathering
    uint16_t default_minutes;  // Offset 0x08: Match duration in minutes (e.g. 6, 8, 10, 12)
    char     description[30];  // Offset 0x0A: 30-byte ASCII description string
    uint16_t tile_type_count;  // Offset 0x28: Number of tile dictionary entries (e.g. 1,334)
    char     tile_names[tile_type_count + 1][11]; // Array of 11-byte fixed-width ASCII strings
    uint32_t width;            // Map grid width (31, 40, or 60)
    uint32_t height;           // Map grid height (31, 40, or 60)
};
```

**Dictionary String Format:**
Each tile name is an 11-byte ASCII string referencing an animation sequence in `ants.chd` Table 4 (e.g. `"g01a\0"`, `"fdpezp1\0"`, `"BLACKHILL\0"`). If shorter than 11 bytes, it is terminated by `\0` and padded with arbitrary bytes.

---

### 2.3 Grid Layers: Layer 1 & Layer 2 (`width * height * 6` Bytes Each)

Following the header, the map stores two consecutive full-resolution grid layers:
1. **Layer 1: Terrain Base Grid** (size = $\text{width} \times \text{height} \times 6$ bytes)
2. **Layer 2: Interactive Overlay Grid** (size = $\text{width} \times \text{height} \times 6$ bytes)

#### Cell Data Structure (6 Bytes per Tile in File)
Each cell consists of 3 little-endian 16-bit integers:
```c
struct LVLCellInFile {
    uint16_t word1;   // Base tile index in tile dictionary (or 0x7FFE for empty)
    uint16_t word2;   // Flags and variation (bit 0 is merged into runtime flag)
    uint16_t word3;   // Extra properties / variation index
};
```

#### Engine In-Memory Translation (`0x10069d8`)
During map load, `Ants.exe` translates each 6-byte cell into a 4-byte runtime cell:
```c
// Word 1 bit 0 is packed with flag bit 0 of Word 2
runtime_cell.tile_and_flag = (dictionary_lookup(file_cell.word1) << 1) | (file_cell.word2 & 1);
runtime_cell.properties    = file_cell.word3;
```

#### Layer 1 Ground Categories
- **Walkable Ground (Grass, Dirt, Clay):** Base terrain tiles (`g01a`, `d01a`, `m01a`). Standard traversal.
- **Wall / Obstacle (Boulders, Rocks, Trees):** Terrain tiles with obstacle property bit set (`0x01`). Blocks ant pathfinding.
- **Deep Water (`w01a`..):** Requires `pu_swim` (Swimmer Ant). All non-swimmer ants drown immediately upon entering deep water.

#### Layer 2 Interactive Grid & Empty Cell Sentinel
- **Empty Cell Sentinel:** `word1 == 0x7FFE` (`32,766`). Indicates no object occupies Layer 2 at $(x, y)$.
- **Interactive Objects on Layer 2:**
  - **Anthills:** `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247), `GREENHILL` (248).
  - **Bridges:** `bridge1` (34), `bridge2` (35), `bridge3` (36), `bridge4` (37).
  - **Fire Walls:** `wallup04` (134).
  - **Mines / Bombs:** `blackbomb`, `bluebomb`, `redbomb`, `greenbomb`.
  - **Food Items:** `fdpezp1`, `fdpezb1`, `fdfrl1..6`, `fdgumw1..4`, `fdburgr`, `fdsuckr`, `FOOD`.
  - **Power-Ups:** `pu_comb` (Tile 62), `pu_thief` (Tile 63), `pu_bomb` (Tile 64), `pu_swim` (Tile 65), `pu_mason` / `pu_fire` (Tile 66).

---

### 2.4 Trailing Configuration Blocks

Directly following Layer 2, each map contains 4 sequential configuration blocks. Every official map parses to exactly 0 remaining bytes (`rem = 0`):

```text
[Header & Dictionary] -> [Layer 1 Grid] -> [Layer 2 Grid] -> [Block 1] -> [Block 2] -> [Block 3] -> [Block 4] -> [f_last] -> EOF
```

#### Block 1: Anthill Starting Spawn Locations (`0x1006c7d`)
```c
struct LVLBlock1_Spawns {
    uint16_t count;            // Number of starting spawn records (e.g. 12, 18, 29, 30, 32, 40)
    struct SpawnRecord {
        uint16_t tile_id;      // Tile dictionary index: BSTART, USTART, GSTART, RSTART
        uint16_t y;            // Grid row coordinate
        uint16_t x;            // Grid column coordinate
    } records[count];
};
```
- `BSTART`: Team 0 (Black Anthill)
- `USTART`: Team 1 (Blue Anthill)
- `RSTART`: Team 2 (Red Anthill)
- `GSTART`: Team 3 (Green Anthill)

#### Block 2: Food & Item Respawn Schedule Pools (`0x1006d19`)
```c
struct LVLBlock2_FoodSchedules {
    uint16_t count;            // Number of food spawn points (e.g. 2, 4, 5, 10, 14, 18)
    struct FoodSpawnPoint {
        uint16_t x;                // Grid column coordinate
        uint16_t y;                // Grid row coordinate
        uint16_t initial_delay;    // Delay before initial spawn (seconds)
        uint16_t respawn_interval; // Respawn interval after collection (seconds)
        uint16_t item_count;       // Number of weighted food item variants
        struct ItemVariant {
            uint16_t weight;       // Probability / stage weight
            uint16_t tile_id;      // Tile dictionary index (or 0x7FFE for null)
        } variants[item_count];
    } spawns[count];
};
```

#### Block 3: Ambient & Audio Parameters (`0x1007025`)
```c
struct LVLBlock3_Ambient {
    uint16_t flag1;            // Level ambient flag (typically 0)
    uint16_t tile_or_sound_id; // Level ambient audio/tile reference (typically 0x7FFE)
};
```

#### Block 4: Waypoints & Obstacle Patrol Paths (`0x1006f0e`)
```c
struct LVLBlock4_Waypoints {
    uint16_t count;            // Number of waypoint entries (0 to 22)
    struct WaypointRecord {
        uint16_t x;            // Grid column coordinate
        uint16_t y;            // Grid row coordinate
        uint32_t flag;         // Path flag: 0 = point only; != 0 = extended path
        // Present ONLY if flag != 0:
        uint32_t param;        // Path parameter
        struct Point {
            uint32_t px;
            uint32_t py;
        } points[5];           // 5-point coordinate array (40 bytes)
    } waypoints[count];
};
```

#### Final Field
- `uint16_t f_last`: Level boundary parameter (e.g. 3, 2, 6, 4, 9). Immediately precedes EOF.

---

## 3. 5-to-8 Directional Sprite Mirroring Algorithm

To conserve RAM and container storage, `ants.chd` stores animation sequences for only **5 base compass directions** (facing South, North, and the Eastern hemisphere). The Western hemisphere directions are produced via **horizontal reflection (X-axis flip)**.

### 3.1 Base Direction IDs in CHD
Every directional animation sequence in Table 4 uses a 3-digit numeric suffix where the first digit denotes the heading:

| Direction ID | Compass Facing | Heading Angle | Stored / Generated | Description |
|:---:|:---:|:---:|:---:|:---|
| **`7`** | **North (N)** | 0° / 360° | **Stored in CHD** | Facing straight Up (back of ant towards viewer) |
| **`8`** | **North-East (NE)** | 45° | **Stored in CHD** | Angled Up-Right |
| **`9`** | **East (E)** | 90° | **Stored in CHD** | Facing straight Right |
| **`2`** | **South-East (SE)** | 135° | **Stored in CHD** | Angled Down-Right |
| **`3`** | **South (S)** | 180° | **Stored in CHD** | Facing straight Down (front of ant towards viewer) |
| *SW* | **South-West (SW)** | 225° | **Mirrored** | Horizontally flipped copy of **`2` (SE)** |
| *W* | **West (W)** | 270° | **Mirrored** | Horizontally flipped copy of **`9` (E)** |
| *NW* | **North-West (NW)** | 315° | **Mirrored** | Horizontally flipped copy of **`8` (NE)** |

---

### 3.2 Mathematical Formulation for Horizontal Mirroring

#### 1. Pixel Bit-Matrix Inversion
Given an 8-bit sprite with width $W$, height $H$, and row pitch stride $P$:
For each scanline $y \in [0, H - 1]$ and pixel column $x \in [0, W - 1]$:
$$p'_{\text{flipped}}(x, y) = p_{\text{source}}(W - 1 - x, y)$$
Row padding bytes ($x \ge W$) are filled with $0$.

#### 2. Frame Render Offset Inversion ($dx, dy$)
In `ants.chd` Table 4, each frame defines render offsets $(dx, dy)$ relative to the ant's tile center $(X_{\text{world}}, Y_{\text{world}})$.
When a sprite of width $W$ is rendered natively, its horizontal span is $[dx, \; dx + W]$.
Upon reflecting across the vertical axis ($x = 0$):
The horizontal span transforms to $[-(dx + W), \; -dx]$.
Therefore, the top-left render offset $dx'$ for the mirrored sprite is:
$$dx' = -(dx + W)$$
$$dy' = dy \quad (\text{vertical position is invariant under horizontal reflection})$$

#### 3. Collision / Bounding Box Inversion
Subitem interaction boxes $[box\_left, box\_top, box\_right, box\_bottom]$ mirror across the center:
$$box\_left' = -box\_right$$
$$box\_right' = -box\_left$$
$$box\_top' = box\_top, \quad box\_bottom' = box\_bottom$$

---

### 3.3 Lunchbox Directional Mapping Table
Carrier ants holding food display directional lunchboxes composited over their mandibles:

| Ant Heading | Base Direction | Mirrored? | Lunchbox Sprite Prefix | Example Sprite |
|:---:|:---:|:---:|:---:|:---|
| **North (0°)** | `7` | No | `7lb` | `7lb0000.bmp` .. `7lb0003.bmp` |
| **North-East (45°)** | `8` | No | `6lb` | `6lb0000.bmp` .. `6lb0003.bmp` |
| **East (90°)** | `9` | No | `5lb` | `5lb0000.bmp` .. `5lb0003.bmp` |
| **South-East (135°)** | `2` | No | `4lb` | `4lb0000.bmp` .. `4lb0003.bmp` |
| **South (180°)** | `3` | No | `3lb` | `3lb0000.bmp` .. `3lb0004.bmp` |
| **South-West (225°)** | `2` | **Yes (X-Flip)** | `4lb` (flipped) | Horizontally mirrored `4lb0000.bmp` |
| **West (270°)** | `9` | **Yes (X-Flip)** | `5lb` (flipped) | Horizontally mirrored `5lb0000.bmp` |
| **North-West (315°)** | `8` | **Yes (X-Flip)** | `6lb` (flipped) | Horizontally mirrored `6lb0000.bmp` |

---

### 3.4 Runtime O(1) 8-Directional Lookup Architecture

The runtime simulation represents entity headings as discrete integer indices $0..7$:

```text
       0 (N)
   7 (NW)  1 (NE)
 6 (W)       2 (E)
   5 (SW)  3 (SE)
       4 (S)
```

```c
// Pre-computed lookup table mapping runtime direction (0..7) to CHD source direction and flip flag
struct DirectionMapping {
    uint16_t chd_dir_code; // 7, 8, 9, 2, 3
    bool     h_flip;       // true = apply horizontal mirroring
};

static const struct DirectionMapping DIRECTION_MAP[8] = {
    [0] = { .chd_dir_code = 7, .h_flip = false }, // North (0°)
    [1] = { .chd_dir_code = 8, .h_flip = false }, // North-East (45°)
    [2] = { .chd_dir_code = 9, .h_flip = false }, // East (90°)
    [3] = { .chd_dir_code = 2, .h_flip = false }, // South-East (135°)
    [4] = { .chd_dir_code = 3, .h_flip = false }, // South (180°)
    [5] = { .chd_dir_code = 2, .h_flip = true  }, // South-West (225°) -> Flipped SE
    [6] = { .chd_dir_code = 9, .h_flip = true  }, // West (270°)       -> Flipped E
    [7] = { .chd_dir_code = 8, .h_flip = true  }, // North-West (315°) -> Flipped NE
};
```

**Atlas Pre-Generation Optimization:**
At startup, `libants-assets` pre-generates the mirrored versions of all sprites referenced by directions 8, 9, and 2 directly into the GPU texture atlas. This eliminates runtime branching, per-frame pixel copying, and shader flipping logic, granting pure $O(1)$ direct array indexing.

---

## 4. Complete Audio Mapping (Sound IDs 0 to 90)

The following table documents every sound effect entry in `ants.chd` Table 2:

| Sound ID | CHD Offset | Original WAV Filename | Sample Rate | Bit Depth | Channels | PCM Buffer Size | Duration | Primary In-Game Trigger & Usage |
|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---|
| **0** | `0x00685051` | `buttonclick.wav` | 11,025 Hz | 8-bit | Mono | 3,057 B | 0.28 s | UI button click (`op_okd`, `re_okd`, modal dialogs) |
| **1** | `0x00685C70` | `powerupc.wav` | 11,025 Hz | 8-bit | Mono | 3,505 B | 0.32 s | Picking up power-up pickup (`getpow` subitem 0); Anthill full heal |
| **2** | `0x00686A4C` | `powerupc2.wav` | 11,025 Hz | 8-bit | Mono | 3,505 B | 0.32 s | Power-up sparkle secondary tone (`getpow` subitem 6) |
| **3** | `0x00687829` | `combatnetfairy.wav` | 11,025 Hz | 8-bit | Mono | 6,860 B | 0.62 s | Combat Ant power-up transformation chord (`battle` subitem 0) |
| **4** | `0x00689326` | `bombexp.wav` | 22,050 Hz | 8-bit | Mono | 25,216 B | 1.14 s | Bomber Ant mine detonation blast (`bombex` subitem 0) |
| **5** | `0x0068F5D0` | `fireburnout.wav` | 11,025 Hz | 8-bit | Mono | 6,387 B | 0.58 s | Fire wall 180s natural burnout sputter (`sputter` subitem 0) |
| **6** | `0x00690EF1` | *(unnamed)* | 11,025 Hz | 8-bit | **Stereo** | 65,190 B | 5.91 s | Ants startup logo musical fanfare (`mslogo` subitem 0) |
| **7** | `0x006A0DB6` | `rndm6.wav` | 22,050 Hz | 8-bit | Mono | 15,754 B | 0.71 s | Random ambient sound bite 6 |
| **8** | `0x006A4B68` | `rndm5.wav` | 22,050 Hz | 8-bit | Mono | 23,441 B | 1.06 s | Random ambient sound bite 5 |
| **9** | `0x006AA721` | `rndm4.wav` | 22,050 Hz | 8-bit | Mono | 23,212 B | 1.05 s | Random ambient sound bite 4 |
| **10** | `0x006B01F5` | `rndm3.wav` | 22,050 Hz | 8-bit | Mono | 12,671 B | 0.57 s | Random ambient sound bite 3 |
| **11** | `0x006B339C` | `rndm2.wav` | 22,050 Hz | 8-bit | Mono | 17,162 B | 0.78 s | Random ambient sound bite 2 |
| **12** | `0x006B76CE` | `rndm1.wav` | 22,050 Hz | 8-bit | Mono | 15,754 B | 0.71 s | Random ambient sound bite 1 |
| **13** | `0x006BB480` | `gantorders.wav` | 22,050 Hz | 8-bit | Mono | 13,394 B | 0.61 s | Worker Ant selection acknowledgments ("Orders?") |
| **14** | `0x006BE8FF` | `gantrdy.wav` | 22,050 Hz | 8-bit | Mono | 16,126 B | 0.73 s | Worker Ant hatch ready vocalization ("Ready!") |
| **15** | `0x006C2827` | `gantcommand.wav` | 22,050 Hz | 8-bit | Mono | 14,041 B | 0.64 s | Worker Ant move order confirmation |
| **16** | `0x006C5F2E` | `gantattack.wav` | 22,050 Hz | 8-bit | Mono | 10,592 B | 0.48 s | Worker Ant attack confirmation cry |
| **17** | `0x006C88BB` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 6,484 B | 0.59 s | UI / feedback tone |
| **18** | `0x006CA22E` | `theifrdy.wav` | 22,050 Hz | 8-bit | Mono | 19,079 B | 0.87 s | Thief Ant ready vocalization ("Hehehe...") |
| **19** | `0x006CECE0` | `theifgo.wav` | 22,050 Hz | 8-bit | Mono | 17,316 B | 0.79 s | Thief Ant move order confirmation |
| **20** | `0x006D30AE` | `theifattack.wav` | 22,050 Hz | 8-bit | Mono | 12,194 B | 0.55 s | Thief Ant attack command vocalization |
| **21** | `0x006D607E` | `theifdo.wav` | 22,050 Hz | 8-bit | Mono | 21,710 B | 0.98 s | Thief Ant action acknowledgment |
| **22** | `0x006DB576` | `firerdy.wav` | 22,050 Hz | 8-bit | Mono | 11,188 B | 0.51 s | Fire Ant ready acknowledgment |
| **23** | `0x006DE154` | `firego.wav` | 22,050 Hz | 8-bit | Mono | 11,990 B | 0.54 s | Fire Ant move command confirmation |
| **24** | `0x006E1053` | `fireattack.wav` | 22,050 Hz | 8-bit | Mono | 11,046 B | 0.50 s | Fire Ant attack order confirmation |
| **25** | `0x006E3BA6` | `firedo.wav` | 22,050 Hz | 8-bit | Mono | 10,340 B | 0.47 s | Fire Ant ability trigger confirmation |
| **26** | `0x006E6433` | `combrdy2.wav` | 22,050 Hz | 8-bit | Mono | 11,007 B | 0.50 s | Combat Ant ready grunt 2 |
| **27** | `0x006E8F5D` | `combrdy1.wav` | 22,050 Hz | 8-bit | Mono | 19,199 B | 0.87 s | Combat Ant ready grunt 1 |
| **28** | `0x006EDA87` | `combgo1.wav` | 22,050 Hz | 8-bit | Mono | 6,175 B | 0.28 s | Combat Ant move grunt 1 |
| **29** | `0x006EF2D0` | `combgo2.wav` | 22,050 Hz | 8-bit | Mono | 14,783 B | 0.67 s | Combat Ant move grunt 2 |
| **30** | `0x006F2CB9` | `combdo2.wav` | 22,050 Hz | 8-bit | Mono | 15,615 B | 0.71 s | Combat Ant attack order vocalization 2 |
| **31** | `0x006F69E2` | `combdo1.wav` | 22,050 Hz | 8-bit | Mono | 22,679 B | 1.03 s | Combat Ant attack order vocalization 1 |
| **32** | `0x006FC2A3` | `brdgrdy.wav` | 22,050 Hz | 8-bit | Mono | 9,363 B | 0.42 s | Swimmer Ant ready vocalization |
| **33** | `0x006FE760` | `brdggo.wav` | 22,050 Hz | 8-bit | Mono | 9,363 B | 0.42 s | Swimmer Ant move order confirmation |
| **34** | `0x00700C1C` | `brdgat.wav` | 22,050 Hz | 8-bit | Mono | 8,895 B | 0.40 s | Swimmer Ant attack order confirmation |
| **35** | `0x00702F04` | `brdgdo.wav` | 22,050 Hz | 8-bit | Mono | 9,059 B | 0.41 s | Swimmer Ant bridge build order confirmation |
| **36** | `0x00705290` | `bombrdy.wav` | 22,050 Hz | 8-bit | Mono | 6,879 B | 0.31 s | Bomber Ant ready vocalization |
| **37** | `0x00706D99` | `bombgo.wav` | 22,050 Hz | 8-bit | Mono | 11,423 B | 0.52 s | Bomber Ant move order confirmation |
| **38** | `0x00709A61` | `bombattack.wav` | 22,050 Hz | 8-bit | Mono | 16,063 B | 0.73 s | Bomber Ant attack order vocalization |
| **39** | `0x0070D94D` | `bombdo.wav` | 22,050 Hz | 8-bit | Mono | 11,743 B | 0.53 s | Bomber Ant plant mine order confirmation |
| **40** | `0x00710755` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 3,752 B | 0.34 s | UI warning chirp |
| **41** | `0x0071161C` | `playerout.wav` | 11,025 Hz | 8-bit | Mono | 10,329 B | 0.94 s | Defeat sting played for defeated players at match end |
| **42** | `0x00713EA1` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 21,441 B | 1.94 s | UI notification chime |
| **43** | `0x00719281` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 2,605 B | 0.24 s | Short tick feedback |
| **44** | `0x00719CCD` | `countdwn.wav` | 11,025 Hz | 8-bit | Mono | 3,968 B | 0.36 s | Match timer final countdown warning chime |
| **45** | `0x0071AC78` | *(unnamed)* | 11,025 Hz | 8-bit | **Stereo** | 11,026 B | 0.50 s | Stereo panning notification left-to-right |
| **46** | `0x0071D7A9` | *(unnamed)* | 11,025 Hz | 8-bit | **Stereo** | 11,026 B | 0.50 s | Stereo panning notification right-to-left |
| **47** | `0x007202DA` | `bump.wav` | 11,025 Hz | 8-bit | Mono | 2,864 B | 0.26 s | Ant collision bump feedback |
| **48** | `0x00720E31` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 11,592 B | 1.05 s | Medium fanfare chime |
| **49** | `0x00723B98` | `allyoff.wav` | 11,025 Hz | 8-bit | Mono | 20,816 B | 1.89 s | Alliance dissolved / broken notification tone |
| **50** | `0x00728D12` | `allyon.wav` | 11,025 Hz | 8-bit | Mono | 20,782 B | 1.88 s | Alliance formed confirmation chord |
| **51** | `0x0072DE69` | `allypro.wav` | 11,025 Hz | 8-bit | Mono | 13,760 B | 1.25 s | Alliance proposal invitation incoming tone |
| **52** | `0x00731453` | `allynot.wav` | 11,025 Hz | 8-bit | Mono | 11,713 B | 1.06 s | Alliance invitation rejected denial chord |
| **53** | `0x0073423E` | `allyyes.wav` | 11,025 Hz | 8-bit | Mono | 17,600 B | 1.60 s | Alliance invitation accepted chord |
| **54** | `0x00738728` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 1,368 B | 0.12 s | Short UI click |
| **55** | `0x00738C9F` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 2,026 B | 0.18 s | Short UI blip |
| **56** | `0x007394A8` | `winner.wav` | 22,050 Hz | 8-bit | Mono | 102,860 B | 4.67 s | Triumphant victory fanfare played at match end (winner only) |
| **57** | `0x0075269D` | `attack.wav` | 11,025 Hz | 8-bit | Mono | 4,032 B | 0.37 s | Standard ant melee attack strike hit sound |
| **58** | `0x00753686` | `underattack.wav` | 22,050 Hz | 8-bit | Mono | 15,540 B | 0.70 s | High-priority 2,566 Hz alarm siren for enemy base invasion |
| **59** | `0x00757368` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 7,808 B | 0.71 s | Feedback chime |
| **60** | `0x00759207` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 11,340 B | 1.03 s | Feedback tone |
| **61** | `0x0075BE72` | `antstop.wav` | 11,025 Hz | 8-bit | Mono | 1,272 B | 0.12 s | Ant halt / stop command audio |
| **62** | `0x0075C394` | `powerdrip.wav` | 11,025 Hz | 8-bit | Mono | 4,992 B | 0.45 s | Power-up expiration / power drain effect |
| **63** | `0x0075D740` | `cantgo.wav` | 11,025 Hz | 8-bit | Mono | 3,020 B | 0.27 s | Invalid command / path blocked buzzer |
| **64** | `0x0075E335` | `flythumpa.wav` | 11,025 Hz | 8-bit | Mono | 6,272 B | 0.57 s | Ballistic knockback launch impact sound A (`*gh*`, `*gb*`) |
| **65** | `0x0075FBE1` | `flythumpb.wav` | 11,025 Hz | 8-bit | Mono | 5,679 B | 0.52 s | Ballistic knockback landing impact sound B |
| **66** | `0x0076123C` | `harvest.wav` | 11,025 Hz | 8-bit | Mono | 4,730 B | 0.43 s | Food item pickup / harvesting sound |
| **67** | `0x007624E0` | `firestarta.wav` | 11,025 Hz | 8-bit | Mono | 5,408 B | 0.49 s | Fire Ant magnifying glass sunbeam focus (`afsf*` subitem 5) |
| **68** | `0x00763A2D` | `firestartb.wav` | 11,025 Hz | 8-bit | Mono | 9,696 B | 0.88 s | Flame ignition burst (`afsf*` subitem 17) |
| **69** | `0x0076603A` | `fireextinguish.wav` | 11,025 Hz | 8-bit | Mono | 5,537 B | 0.50 s | Fire Ant fire smother / extinguishing sound (`afxf*` subitem 4) |
| **70** | `0x0076760C` | `stun.wav` | 11,025 Hz | 8-bit | Mono | 21,440 B | 1.94 s | Stun recovery wobble sound after ballistic bounce |
| **71** | `0x0076C9F3` | `splash.wav` | 11,025 Hz | 8-bit | Mono | 21,203 B | 1.92 s | Water splash entering deep water (`asdi*`) |
| **72** | `0x00771CEF` | `antdrown.wav` | 11,025 Hz | 8-bit | Mono | 13,899 B | 1.26 s | Non-swimmer ant drowning audio |
| **73** | `0x00775365` | `bombdrop.wav` | 11,025 Hz | 8-bit | Mono | 1,277 B | 0.12 s | Bomber Ant pins active enemy mine (`abdb*` subitem 3) |
| **74** | `0x0077588D` | `bombmuffle.wav` | 11,025 Hz | 8-bit | Mono | 8,704 B | 0.79 s | Bomber Ant body-crush mine squash (`abdb*` subitem 6) |
| **75** | `0x00777ABA` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 4,032 B | 0.37 s | Worker melee attack strike hit sound variant (`agat*` subitem 1) |
| **76** | `0x00778A99` | `FlyThumpB.wav` | 11,025 Hz | 8-bit | Mono | 5,679 B | 0.52 s | Alternate landing thump (identical byte payload to Sound 65) |
| **77** | `0x0077A0F4` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 4,730 B | 0.43 s | Airborne knockback whoosh (`aggf*` subitem 4) |
| **78** | `0x0077B38D` | `attack2.wav` | 11,025 Hz | 8-bit | Mono | 4,240 B | 0.38 s | Combat Ant heavy punch strike (`acat*` subitem 2, 2 HP + launch) |
| **79** | `0x0077C447` | `waterattack.wav` | 11,025 Hz | 8-bit | Mono | 2,817 B | 0.26 s | Aquatic melee attack strike |
| **80** | `0x0077CF76` | *(unnamed)* | 11,025 Hz | 8-bit | Mono | 10,944 B | 0.99 s | Water plunge / aquatic splash sound (`asdi*` subitem 9) |
| **81** | `0x0077FA55` | `shovelgravel.wav` | 11,025 Hz | 8-bit | Mono | 3,400 B | 0.31 s | Swimmer Ant shovels dirt/gravel on land (`asbbl*` subitem 4) |
| **82** | `0x007807CC` | `shovelwater.wav` | 11,025 Hz | 8-bit | Mono | 6,279 B | 0.57 s | Swimmer Ant shovels into water to build bridge (`asbbw*` subitem 3) |
| **83** | `0x00782081` | `theifwhip.wav` | 11,025 Hz | 8-bit | Mono | 5,112 B | 0.46 s | Thief Ant stealth crawl / whip sound |
| **84** | `0x007834A5` | `steala.wav` | 11,025 Hz | 8-bit | Mono | 2,512 B | 0.23 s | Thief Ant leaps above anthill hole (`atcr501` frame 19) |
| **85** | `0x00783E9E` | `stealb.wav` | 11,025 Hz | 8-bit | Mono | 3,008 B | 0.27 s | Thief Ant snatches enemy food supplies (`atcr501` frame 26) |
| **86** | `0x00784A87` | `stealc.wav` | 11,025 Hz | 8-bit | Mono | 3,400 B | 0.31 s | Thief Ant pops out of enemy hole with loot (`atcr501` frame 31) |
| **87** | `0x007857F8` | `scoreup.wav` | 11,025 Hz | 8-bit | Mono | 6,496 B | 0.59 s | Food deposited into home anthill (ascending score chime) |
| **88** | `0x00787182` | `scoredn.wav` | 11,025 Hz | 8-bit | Mono | 3,293 B | 0.30 s | Food stolen from anthill (descending score loss tone) |
| **89** | `0x00787E89` | `navbuttonclick.wav` | 11,025 Hz | 8-bit | Mono | 1,448 B | 0.13 s | Navigation button click |
| **90** | `0x00788462` | `bombpick.wav` | 11,025 Hz | 8-bit | Mono | 5,520 B | 0.50 s | Bomber Ant readies bomb from equipment pack (`absb*` subitem 9) |

---

## 5. Verification Method & Evidence

The findings in this specification were independently verified using programmatic binary parsing directly against the original assets:

1. **`ants.chd` Structural Verification:**
   - 28-byte header unpacked: `version = 9`, `table1 = 1,052`, `table2 = 6,835,937`, `table3 = 7,903,773`, `table4 = 7,903,835`, `palette_bytes = 1,024`.
   - Table 1 verified: exactly 2,794 sprite bitmaps parsed and dimensions verified.
   - Table 2 verified: exactly 91 audio clips extracted and parsed with valid `WAVEFORMATEX` headers.
   - Table 3 verified: 4 event tags (`HITGROUND`, `ATTACKHIT`, `HEAL`, null sentinel) parsed over 62 bytes.
   - Table 4 verified: exactly 1,344 animation sequences parsed, with 365 subitem audio triggers mapped to Table 2 Sound IDs.
2. **`Maps/*.LVL` Complete Parsing:**
   - All 6 map files (`GAUNTLET.LVL`, `ISLANDS.LVL`, `MEDIUM.LVL`, `SMALL.LVL`, `TINY.LVL`, `TREASURE.LVL`) parsed through headers, tile dictionaries, Layer 1 grids, Layer 2 grids, and all 4 trailing configuration blocks with **0 bytes remaining** (`consumed == file_size`).
3. **5-to-8 Directional Consistency:**
   - Evaluated 1,344 animation names: 100% of directional animations use digits 2, 3, 7, 8, 9 (and non-directional 5 for `atcr501`).
   - Verified that lunchbox directional sprites use matching sets `3lb` (Dir 3), `4lb` (Dir 2), `5lb` (Dir 9), `6lb` (Dir 8), and `7lb` (Dir 7).
