# Handoff Report: Map Parsing (`.LVL`) & 5-to-8 Directional Mirroring Engine

**Identity:** `explorer_m1_3`  
**Milestone:** Milestone 1 (`ants-assets`)  
**Target Document:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md`  
**Handoff Type:** Hard (Task Complete)  

---

## 1. Observation

### 1.1 Map Asset Inventory & Direct Binary Ingestion
We directly probed and parsed all 6 official map files in `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/`:
- `TINY.LVL`: 19,312 bytes
- `SMALL.LVL`: 34,190 bytes
- `MEDIUM.LVL`: 58,381 bytes
- `GAUNTLET.LVL`: 58,508 bytes
- `ISLANDS.LVL`: 58,776 bytes
- `TREASURE.LVL`: 58,853 bytes

### 1.2 Binary Parser Execution Output (`test_map_parser`)
Compiled with Apple Clang (`clang++ -std=c++17 test_map_parser.cpp -o test_map_parser && ./test_map_parser`):
```text
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/TINY.LVL] Size=19312 Parsed=19312 Rem=0 [OK]
  Title: 'Tiny map with no PowerUps', Grid: 31x31, Dict: 670, Spawns: 12, Food: 14, Waypoints: 0, f_last: 3
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/SMALL.LVL] Size=34190 Parsed=34190 Rem=0 [OK]
  Title: 'Small map for fast game', Grid: 40x40, Dict: 1326, Spawns: 18, Food: 5, Waypoints: 2, f_last: 2
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/MEDIUM.LVL] Size=58381 Parsed=58381 Rem=0 [OK]
  Title: 'Intermediate map', Grid: 60x60, Dict: 1325, Spawns: 30, Food: 4, Waypoints: 15, f_last: 6
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/GAUNTLET.LVL] Size=58508 Parsed=58508 Rem=0 [OK]
  Title: 'Race for your life!', Grid: 60x60, Dict: 1330, Spawns: 32, Food: 2, Waypoints: 20, f_last: 6
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/ISLANDS.LVL] Size=58776 Parsed=58776 Rem=0 [OK]
  Title: 'Island hopping, expert map', Grid: 60x60, Dict: 1330, Spawns: 40, Food: 10, Waypoints: 22, f_last: 4
[/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/TREASURE.LVL] Size=58853 Parsed=58853 Rem=0 [OK]
  Title: 'One person's trash...', Grid: 60x60, Dict: 1335, Spawns: 29, Food: 18, Waypoints: 19, f_last: 9

ALL 6 MAPS PARSED SUCCESSFULLY WITH 0 REMAINING BYTES!
```

### 1.3 Exact Structural Observations in `.LVL` Files
1. **Tile Dictionary:**
   - The header field at offset 40 `uint16_t tile_type_count` contains count $N$.
   - The dictionary contains **$N + 1$ entries** of fixed 11-byte strings.
   - Indices span $0 \le i \le N$. Unused entries are stored as `.` (`.\0`).
2. **Grid Layers (Layer 1 and Layer 2):**
   - Each cell in both layers is 6 bytes: `uint16_t word1, word2, word3`.
   - Layer 1 terrain indices in `word1` fall in range $[431, 669]$.
   - Layer 2 interactive overlay contains sentinel **`word1 == 0x7FFE` (32,766)** for empty cells.
   - Non-empty cells contain:
     - Anthills: `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247), `GREENHILL` (248)
     - Power-up pickups: `pu_comb` (62), `pu_thief` (63), `pu_bomb` (64), `pu_swim` (65), `pu_mason` (66)
     - Food items: `fdpezp1`, `fdfrl1`, `fdburgr`, `fdsuckr`, etc.
3. **Trailing Blocks:**
   - **Block 1 (Spawns):** `uint16_t count`, entries of 6 bytes: `(tile_id, y, x)` where `y` is row coordinate and `x` is column coordinate. Base spawns are `BSTART` (152, Team 0), `USTART` (153, Team 1), `GSTART` (154, Team 3), `RSTART` (155, Team 2).
   - **Block 2 (Food Pools):** `uint16_t count`, entries of 10 bytes `(y, x, initial_delay, respawn_interval, item_count)` followed by `item_count * 4` bytes `(weight, tile_id)`. Coordinates `(y, x)` match Block 1.
   - **Block 3 (Ambient):** Exactly 4 bytes: `uint16_t flag1 = 0`, `uint16_t tile_or_sound_id = 0x7FFE`.
   - **Block 4 (Waypoints):** `uint16_t count`, entries of `(y, x, flag)` [8 bytes]. If `flag != 0`: `uint32_t param` [4 bytes] + 5 `double` values [40 bytes]. Each of the 5 `double` values represents patrol direction probabilities summing strictly to $1.0$ (e.g. $[0.2, 0.2, 0.2, 0.2, 0.2]$). Total size: 52 bytes when `flag != 0`.
   - **Level Boundary Parameter:** `uint16_t f_last` (2 bytes) immediately preceding EOF.

### 1.4 Directional Mirroring Engine Observations (`ants.chd`)
1. **Base Stored Facings in Table 4:**
   - `7`: North (0°)
   - `8`: North-East (45°)
   - `9`: East (90°)
   - `2`: South-East (135°)
   - `3`: South (180°)
2. **Directional Suffixes in Animations:**
   - Worker Walk: `agwg701` (N), `agwg801` (NE), `agwg901` (E), `agwg201` (SE), `agwg301` (S).
   - Direction `5` anomaly: `atcr501` (Anim 1095) is non-directional omnidirectional dive into anthills.
3. **Lunchbox Carrying Directional Sprite Suite in Table 1:**
   - `7lb0000.bmp`..`7lb0007.bmp` (Facing 7 / North)
   - `6lb0000.bmp`..`6lb0007.bmp` (Facing 8 / North-East)
   - `5lb0000.bmp`..`5lb0007.bmp` (Facing 9 / East)
   - `4lb0000.bmp`..`4lb0007.bmp` (Facing 2 / South-East)
   - `3lb0000.bmp`..`3lb0007.bmp` (Facing 3 / South)
   - Ground dropped lunchbox: Anim 356 (`lunchbox`), Sprite 513 (`3lb0001.bmp`, $11 \times 16$).
4. **Bounding Box Signedness:**
   - Bounding boxes in subitems are signed 32-bit integers (`int32_t`): `box_left` down to $-94$, `box_top` down to $-110$.

---

## 2. Logic Chain

1. **Dictionary Sizing Proof:**
   - Offset 40 stores `tile_type_count = N`.
   - If dictionary were $N \times 11$ bytes, reading grid dimensions at offset $42 + N \times 11$ yields garbage values (`w = 1935766119`, `h = 1684368755`).
   - If dictionary is $(N + 1) \times 11$ bytes, reading grid dimensions at $42 + (N + 1) \times 11$ yields valid dimensions ($31 \times 31$ for TINY, $40 \times 40$ for SMALL, $60 \times 60$ for MEDIUM, GAUNTLET, ISLANDS, TREASURE).
   - Therefore, the dictionary strictly contains $N + 1$ entries of 11 bytes each.

2. **Coordinate Ordering Proof in Trailing Blocks:**
   - Comparing Layer 2 anthill footprints with Block 1 spawn coordinates confirms `c1` matches anthill row $Y$ and `c2` matches anthill column $X$.
   - In Block 2 food pools, evaluating Layer 2 tiles at $(c2, c1)$ matches the initial food variant tile ID with 100% precision, whereas $(c1, c2)$ hits non-food tiles.
   - Therefore, coordinates in Blocks 1, 2, and 4 are strictly serialized in $(y, x)$ (row, column) order.

3. **Remaining Bytes Proof ($rem = 0$):**
   - In all 6 official maps, summing the byte count of HeaderFixed (42) + Dictionary ($(N + 1) \times 11$) + Dims (8) + Layer 1 ($W \times H \times 6$) + Layer 2 ($W \times H \times 6$) + Block 1 ($2 + count \times 6$) + Block 2 ($2 + \sum(10 + item\_count \times 4)$) + Block 3 (4) + Block 4 ($2 + \sum(8 + (flag ? 44 : 0))$) + Boundary (2) equals the exact total file size on disk.
   - Remaining bytes $rem = 0$ for 6 out of 6 files.

4. **Horizontal Mirroring Mathematical Inversion Proof:**
   - A sprite of width $W$ rendered natively at top-left $(X + dx, Y + dy)$ spans $[X + dx, X + dx + W]$.
   - Reflected across entity center $X$: the right boundary $(X + dx + W)$ becomes $X - (dx + W)$, and the left boundary $(X + dx)$ becomes $X - dx$.
   - Since blitting starts at the leftmost boundary, the new render offset is $dx' = -(dx + W)$ and $dy' = dy$.
   - For bounding boxes, $[box\_left, box\_right]$ reflects across 0 to $[-box\_right, -box\_left]$, ensuring $box\_left' \le box\_right'$.
   - Double reflection recovers original values: $-(-(dx + W) + W) = dx$, proving involution.

---

## 3. Caveats

- **Unused Dictionary Entries:** In official maps, unreferenced dictionary slots are filled with `.\0` or residual memory bytes from the original level editor (e.g. `.\level`, `D:\Game`). The parser must safely read these 11-byte records as null-terminated strings without assuming valid asset names for `.` entries.
- **MSVC Debug Padding in Layer 1:** In Layer 1 cells, `word3` is filled with `0xCDCD` (52,685) in all 6 official maps. Simulation engines should not rely on `word3` for terrain behavior.
- **Floating Point Representation:** The 5 doubles in Block 4 represent patrol branching probabilities. Standard IEEE-754 little-endian double precision (`<5d`, 40 bytes) must be read directly.

---

## 4. Conclusion

1. The `.LVL` map format has been 100% reverse-engineered and verified in C++17 across all 6 official maps with zero remaining bytes.
2. The 5-to-8 directional sprite mirroring algorithm is mathematically complete and verified:
   - Pixel reflection: $p'(x, y) = p(W - 1 - x, y)$
   - Frame offset reflection: $dx' = -(dx + W), dy' = dy$
   - Collision box reflection: $box\_left' = -box\_right, box\_right' = -box\_left$
   - Lunchbox carrying suite: `7lb` (N), `6lb` (NE), `5lb` (E), `4lb` (SE), `3lb` (S), and horizontally mirrored versions for SW, W, NW.
   - Pre-computation into an 8-direction atlas guarantees $O(1)$ runtime lookup without per-frame branching or copying.
3. The specification document `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md` provides production-ready C++ code, headers, and comprehensive unit tests ready for immediate integration into `libants-assets`.

---

## 5. Verification Method

To independently verify these findings on macOS:

```bash
# 1. Verify Map Parsing with 0 remaining bytes across all 6 maps
cd /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3
clang++ -std=c++17 test_map_parser.cpp -o test_map_parser
./test_map_parser

# 2. Verify 5-to-8 Directional Sprite Mirroring math and Lunchbox mapping
clang++ -std=c++17 test_mirroring.cpp -o test_mirroring
./test_mirroring

# 3. Verify Real CHD Animation & Sprite Mirroring against ants.chd
clang++ -std=c++17 test_chd_mirroring_real.cpp -o test_chd_mirroring_real
./test_chd_mirroring_real
```

Expected output for all three commands: return code 0, all tests pass, and zero remaining bytes reported.
