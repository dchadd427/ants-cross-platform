# The specification: early notes

Notes of the first weeks of the reverse engineering and the sketch of the architecture, moved out of [`GAME_REVERSE_ENGINEERING.md`](../GAME_REVERSE_ENGINEERING.md) because section 5 there states the same findings systematically (or corrects them). Where this page and that one disagree, that one is right.

## 6. Target Multi-Platform Architecture

> The tree and the list below are the target as first sketched, not the project's layout (`docs/ARCHITECTURE.md` has that): the libraries are `ants_assets`, `ants_sim`, `ants_app` and others under `src/`, the audio mixer is part of `ants_app`, and the game has no shaders of its own.

To achieve clean, modern, high-performance execution across macOS, Linux, Windows, and the Web (WebAssembly):

```
Ants-Mac/
├── assets/                  # Original raw assets (read at runtime directly from ants.chd and Maps/)
│   ├── ants.chd
│   └── Maps/*.LVL
├── crates/ (or src/)
│   ├── ants-assets/         # Pure Rust/C++ reader for .chd and .lvl files (zero external deps)
│   ├── ants-sim/            # Deterministic simulation & game rules (grid, combat, pathfinding)
│   ├── ants-audio/          # Audio playback engine (WAV synthesis + MIDI playback)
│   └── ants-app/            # High-performance 2D renderer, input handling, UI, camera
```

### Key Architectural Strengths:

1. **Direct Binary Asset Compatibility:** Loads `ants.chd` and `Maps/*.LVL` directly into memory without requiring pre-conversion or asset destruction.

2. **Deterministic Simulation:** Allows perfect multiplayer synchronization (lockstep / client-side prediction) and instant replay saves.

3. **Hardware Acceleration:** Modern GPU vertex/fragment shaders for authentic 256-color palette swaps, crisp integer pixel scaling, and optional CRT/LCD filter shaders.

---

### 6.1 WebAssembly Browser Architecture & Virtual Filesystem Bundling

- **Asset Virtualization (`--preload-file`)**: The folder `Original-Ants/` (`ants.chd`, `Maps/*.LVL`, the `.MID` and `.mp3` music and the bundled font) is bundled at compile time into the 14.7 MB `index.data` virtual filesystem blob. The C++ file access calls (`load_from_file("Original-Ants/ants.chd")`) remain 100% unmodified and read from virtual memory.

- **Web Audio Context Unlocking**: Modern browser autoplay policies suspend Web Audio until a user interaction occurs. The web shell resumes SDL's `AudioContext` at the first click, tap or key press (`web/shell.html`, "Sound and the browsers' autoplay rules"); no click-through card comes first (it was removed in v0.0.96).

- **WebAssembly BGM Architecture (dr_mp3 and SDL's Web Audio)**: Web browsers lack native General MIDI synthesizers. The 4 authentic soundtrack tracks (`INTRO.MID`, `ANTS2A.MID`, `ANTS2B.MID`, `ANTSFUN3.MID`) are pre-rendered into high-fidelity MP3 streams bundled into the virtual filesystem (`--preload-file`). `AudioMixer` reads the MP3 from the in-memory virtual filesystem, decodes it itself with dr_mp3 (6.2, item 8) and mixes it into the SDL audio device, the same path as on the desktop; in the browser that device is SDL's Web Audio context, and the music begins as soon as the browser lets it run (the web shell resumes it at the first user interaction). There is no JavaScript audio bridge, and `MidiPlayer` plays nothing in the web build.

- **Embedded TrueType Font Support in WebAssembly**: Bundles `Original-Ants/LibreFranklin-Medium.ttf` (from v0.0.49; `Arial.ttf` before) directly within the `--preload-file` virtual filesystem. Emscripten compiles with `-sUSE_SDL_TTF=2` and `ANTS_ENABLE_SDL_TTF=1`, ensuring crisp, smooth, high-fidelity ant names, status strings, and chat text across web and desktop without falling back to the 8x8 blocky retro bitmap font.

- **Containerized Web Deployment via Docker**: The project includes a multi-stage `Dockerfile` and `docker-compose.yml` leveraging `emscripten/emsdk` to compile the WebAssembly target and `nginx:alpine` to serve static assets with gzip compression, caching, and modern WebAssembly security headers at `beta.playants.org`.

### 6.2 Authentic Fog of War, SFX Protocol, Special Abilities, Plant Stem Collision & In-Engine Audio Streaming

#### 1. Authentic Fog of War System (`Ants.exe` `0x1006af4`, `0x1008607`, `0x100a11c`, Table `0x1001a78`)

- **Hidden Entities (`0x100823b`, `0x1008914`)**: Enemy units, enemy bases, food morsels, powerups, and bombs on unrevealed tiles are hidden. Base terrain is visible under the stipple dither overlay.

- **Minimap Radar**: Unrevealed tiles are drawn dark/shrouded `{15, 12, 10, 255}`; enemy dots, food, and enemy anthill markers are omitted.

#### 2. `exithill.wav` Incubation Emergence (`Ants.exe` `0x1015a37`, GameSound 32)

- Sound ID 43 (`exithill.wav`) represents an egg hatching and emerging into the colony.

- Triggers **only** when a newly hatched ant (costing 200 food points) finishes its incubation delay and surfaces from the anthill hole into the playfield.

- Units returning to base to deposit food or dwelling in the base hole to heal do **not** trigger `exithill.wav` upon entry or exit.

#### 3. Fire Ant & Bomber Ability Cooldown Timings (`Ants.exe` `0x101ba24`, `0x101bdd1`)

- In `Ants.exe`:

  - Fire Ant cooldown is 2000ms (40 ticks, `push 0x7d0` at `0x101ba24`). *(Differs, unresolved: see 5.37.)*

  - Bomber Ant cooldown is 3000ms (60 ticks, `push 0xbb8` at `0x101bdd1`). *(Differs, unresolved: see 5.37.)*

- Cooldown timer starts at the **moment the order is initiated** (`0x101ba1f` / `0x101bdcc`), rather than accumulating after the placement animation ends.

#### 4. Minimap Radar Firewall Exclusion

- In the original 1998 radar, firewalls are excluded from the radar rasterizer, displaying only terrain, food, anthills, and ants.

#### 5. Diplomacy SFX Protocol (`Ants.exe` `0x1023f9f`, `0x1028f04`, GameSounds 38..41)

- `allypro.wav` (Sound ID 51): Targeted exclusively to the recipient of an alliance proposal (`to_player`).

- `allyon.wav` (Sound ID 50): Broadcast to all players when an alliance is accepted.

- `allynot.wav` (Sound ID 52): played on the machine of the proposing player when the invite is declined (`FUN_0100c36b`, 0x100c4bc) and on the machine of the player who declined (the answer message 0x1c runs there too, 0x1023c53).

- `allyoff.wav` (Sound ID 49): Broadcast when an alliance dissolves.

#### 6. Plant Stem Obstacle Collisions & Flower Dropper Z-Order (`Ants.exe` `0x100e3b0`, `0x1020951`)

- **Flower Plant World Object Instantiation (`0x100e380`..`0x100e436`)**: Block 1 decor entries with `team_id == 255` matching Table `0x1001af8` (`flower1`, Anim ID 421) instantiate interactive world objects registered at the root tile `(wp.x, wp.y)`.

- **Pathfinding & Tile Collision Check (`0x1020951` / `0x100f4ab`)**: `get_tile_info` queries the tile's object pointer. When an object exists on the tile and is not an ant or interactable, execution branches to `0x1020bb7` (`mov eax, 0x1f40`), assigning an impassable traversal cost of 8000.

- **Authentic Root Obstacle vs. Landing Walkability**: On `SMALL.LVL`, the plant root stems at `(2, 19)` and `(37, 19)` are solid obstacles (`is_obstacle_overlay = true`), preventing ants from walking through the stems. The drop target `(wp.x, wp.y + 1)` (`(2, 20)` and `(37, 20)`) remains open and passable.

- **Canopy Z-Ordering**: Falling droplets render in front of the plant canopy (`render_flower_droppers` placed after `render_terrain_layer3_canopy`).

#### 7. Uninterruptible Special Abilities & "Can't Go" Order Rejection

- When executing `PlacingFire`, `PlantingBomb`, `DefusingBomb`, `ExtinguishingFire`, `BuildingBridge`, or `DemolishingBridge`:

  - New orders are rejected, preserving the active action and emitting `SoundID::CantGo` (`cantgo.wav`).

  - Taking non-lethal damage applies HP reduction without triggering flinch or displacement, allowing placement to finish cleanly.

#### 8. Native In-Engine Audio Streaming (`dr_mp3.h`)

- Single-header, zero-dependency MP3 decoder integrated directly into `AudioMixer`.

- Replaces DOM `<audio>` bridges with direct PCM decoding and buffer mixing, enabling unified volume control, looping, and cross-platform streaming across macOS and WebAssembly.

#### 9. Authentic Combat Mechanics, 8-Directional Diagonal Attack & Hit Reaction Parity (`Ants.exe` `0x101c4f2`, `0x101cb0c`, `0x101de7e`, `0x1022c57`)

- **8-Directional Diagonal Adjacency & Approach**:

  - In `Ants.exe`, melee combat natively supports full 8-directional engagement (`chebyshev_dist <= 1`, including all 4 diagonal neighbors: North-East, North-West, South-East, South-West).

  - When an ant is already adjacent to an enemy, it faces the enemy directly and attacks immediately on issuance without any detour.

  - When an ant is ordered to attack from a distance, all 8 neighboring tiles are evaluated; the ant moves along the optimal path to the nearest passable neighbor (cardinal or diagonal).

  - The ant completes its walk to the neighbor tile center (`at_tile_center || waypoints.empty()`) before swinging, preventing premature strikes one tile early while allowing instant strikes when adjacent.

  - Diagonal melee strikes push the victim along the diagonal strike vector `(target->pos.x + p_dx, target->pos.y + p_dy)`, deflecting to cardinal flanks only if obstructed by walls or rock obstacles.

- **Attack Animation Playback (`*at*`)**:

  - `agat301` subitem sequence in `ants.chd` consists of 6 subitems with durations `(60, 60, 60, 90, 60, 90)` ms totaling 420ms (8.4 ticks @ 20Hz).

  - `acat301` (Combat Ant) consists of 6 subitems with durations `(80, 100, 60, 80, 100, 120)` ms totaling 540ms (11 ticks @ 20Hz).

  - Animation ticks (`anim_tick`, `anim_subitem`) reset to 0 upon executing an attack, allowing all 6 subitems (wind-up, strike, hit connect, recovery) to play completely across the full attack cycle.

- **Pushback Slide & Flinch Reaction (`*gh*`)**:

  - `aggh301..aggh901` in `ants.chd` consist of 9 subitems with durations totaling 800ms (16 ticks @ 20Hz) and subitem pixel displacement offsets `vals[1]`: `-24px` at tick 0..1, `-8px` at tick 2..3, `0px` at tick 4.

  - Standard melee attack applies 1 HP damage and sets `target->state = UnitState::Flinch` with duration of 14 ticks (~700ms).

  - During the first 4 ticks (200ms), the victim smoothly slides 32 pixels from origin to destination (8px per tick), completely eliminating abrupt 32px single-tick teleportation.

  - During the remaining ticks (4..14), the victim stays on the destination tile, playing all stagger and recovery subitems of `aggh*`.

  - The victim is forced to orient its facing direction toward the attacking ant at the moment of impact.

#### 10. Authentic 10-Tick Collision Scuffle Ball & Layer 2 Floor Debris Walkability (`Ants.exe` `0x101a86a`, `0x1020951`, `0x10215cb`, `0x102162a`)

- **10-Tick Scuffle Ball & 2-Cycle Spin**:

  - In `Ants.exe`, collision scuffle (State 3) initializes timer `[esi + 0x74] = 0xa` (10 ticks / 500ms @ 20Hz).

  - Visual effect `battle` (Anim 56, `batt001..batt004`) loops for **2 complete cycles** across the 10 ticks, matching the 622ms duration of Sound 3 `combatnetfairy.wav`.

  - `UnitState::Bounce` lasts 10 ticks (500ms), allowing all 12 subitems of `*gb*` (`aggb301`) to render smoothly from airborne recoil to ground impact (`SoundID::FlingThumpB`) and standing recovery.

- **Layer 2 Floor Debris Passability (`TINY.LVL` Tile 18, 17)**:

  - In `Ants.exe` (`0x1020951`, `0x1008af7`), terrain passability queries tile descriptors in table `0x10049c8`.

  - Layer 2 tiles consisting of flat floor debris such as `broken1`, `broken2` (`cerbol3.bmp`, `cerbol4.bmp`), and `broken3` (`spoon.bmp`) are non-obstacle ground decor (`is_obstacle_overlay = false`).

  - This preserves walkability on pathways such as slate tile `(18, 17)` on `TINY.LVL`. *(Superseded: in the original that tile carries the layer-1 solid bit and blocks ants; see §5.32.7.)*

#### 11. Authentic Collision Scuffle Model Concealment, Chaotic Domino Cascade & Hazard Landings (`Ants.exe` `0x10215cb`, `0x1020f60`, `0x1020de7`)

- **Collision Scuffle Anchor Ant Visibility & Displaced Unit Recoil (`0x102151a`, `0x10215cb`, State 3)**:

  - In `Ants.exe` (`0x102151a`), same-tile collision resolution operates specifically on the displaced ant (`State 3` / `Bounce`).

  - The original binary never conceals or suppresses drawing the resident anchor ant standing on the collision tile. The anchor ant was already stationed on the tile and remains completely visible on the playfield under the dust cloud (`battle` / Anim 56) with full health bar and selection brackets, preventing jarring pop-in.

  - The displaced ant resolves its collision clash by transitioning into `UnitState::Bounce` with animation `*gh*` (`aggh*` Ground Hit recoil / flinch flight with horizontal flipping for NW/W/SW directions).

  - Over a 4-tick interpolation window (80ms), the ant smoothly flies out from the collision clash center (`push_start_px, push_start_py`) to its destination tile center (`push_dest_px, push_dest_py`).

- **Terrain-Only Passability Candidate Selection (`0x1020de7`)**:

  - When evaluating candidate bounce destination tiles around the collision point, the engine strictly checks whether tiles are within bounds and non-solid terrain (`terrain_type != TERRAIN_OBSTACLE && !cell.is_obstacle_overlay`).

  - Water, fire walls, bombs, and tiles occupied by other ants **do not** block candidate selection.

- **Chaotic Domino Cascades**:

  - When an ant bounces onto a tile occupied by another ant, the collision immediately cascades into a secondary scuffle clash, displacing the resident ant and enabling multi-ant chain bounces across dense clusters.

- **Hazard Landings**:

  - **Water Drowning vs. Swimming**: Non-swimmer ants bouncing into open water instantly enter `UnitState::Drowning` with `hp = 0`, queuing Sound 72 `antdrown.wav` and Sound 71 `splash.wav`, and playing the 22-subitem drowning sequence (`*dr301`). Swimmer ants landing in water safely transition into `UnitState::Swimming` (`in_water = true`).

  - **Fire Wall Contact**: Ants bouncing onto a burning tile take +1 HP fire damage and trigger fire contact audio/visual feedback.

  - **Bomb Detonation**: Ants bouncing onto a planted bomb instantly trigger detonation (`bombex` animation, Sound 4 `bombexp.wav`, 2 HP explosive blast damage, and clearing the bomb tile).

#### 12. Authentic Attack Audio Sequencing, Sound 57 vs. 75 Differentiation & Flinch FlyThump Events (`ants.chd` Table 4 & `Ants.exe` `0x101dc7f`, `0x101e0ad`, `0x1020756`, `0x1021532`)

- **Flinch Pushback Audio Sequencing (`flythumpa.wav` & `flythumpb.wav`)**:

  - In `ants.chd` Table 4, all flinch animations across all 6 ant classes (`aggh*`, `abgh*`, `afgh*`, `acgh*`, `asgh*`, `atgh*`) define two critical audio event markers:
    - **Subitem 0 (Tick 0 - Strike Impact & Slide Launch)**: Triggers **Sound 64 (`flythumpa.wav` / `SoundID::FlingThumpA`)**.
    - **Subitem 3 (Tick 4 - Arrival & Destination Tile Landing)**: Triggers **Sound 65 (`flythumpb.wav` / `SoundID::FlingThumpB`)**.

  - Binary Disassembly in `Ants.exe`:
    - `0x101dc7f`: `push 0x39; call 0x100def5` -> plays Sound 57 (`attack.wav`) on melee strike connect.
    - `0x101e0ad`: `push 0x40; call 0x100def5` -> plays Sound 64 (`flythumpa.wav`) when pushback / flinch begins.
    - `0x1020756`: `push 0x41; call 0x100def5` -> plays Sound 65 (`flythumpb.wav`) when the ant completes its push slide and lands on the destination tile.

- **Combat Ant Punch & Bomb Knockback Stun Audio**:

  - When struck by a Combat Ant punch or caught in a Bomb blast, the victim is launched into ballistic flight:
    - At launch: Sound 78 (`attack2.wav`) / Sound 4 (`bombexp.wav`) + Sound 64 (`flythumpa.wav`).
    - At ground landing: Sound 65 (`flythumpb.wav`) + Sound 70 (`stun.wav` / `SoundID::StunRecover`), placing the victim into `UnitState::Stunned`.

#### 13. Multi-Directional Attack Registration, Victim Facing Dynamics, Bounce Animation (`gh` vs. `gb`), and Re-Collision Loop Breaking (`Ants.exe` `0x101a86a`, `0x1020de7`, `0x10215cb`)

- **Multi-Directional Attack Registration (All Headings)**:

  - Attacks connect and apply damage reliably regardless of relative orientation (approaching from front, flanks, or rear).

  - Melee attack engagement does not require units to align precisely within sub-tile center tolerances (`off_x <= 6 && off_y <= 6`), eliminating false rejections when striking while walking or from diagonal angles.

  - Hostile units on adjacent tiles do not execute elastic separation pushback (`dist_sq < 22 * 22`), preventing attacker repulsion cycles that previously interrupted strike execution.

- **Victim Facing Dynamics**:

  - In `Ants.exe` (`0x101dc7f`), whenever an ant connects with a melee strike, the victim immediately turns to face directly toward the attacker (`target->facing = vector_to_direction(attacker - target)`).

  - When an ant is displaced by a collision bounce, its facing direction is aligned along its bounce displacement vector away from the collision center (`vector_to_direction(chosen - collision_point)`), authentically allowing bounced units to face away from the collision point while attacked units always face their assailant.

- **Bounce Animation Fidelity (`gh` vs. `gb` vs. `gf`)**:

  - In `ants.chd` Table 4, the three displacement and contact action prefixes decode as:
    - `*gh*` ("Get Hit"): Combat flinch reaction. Subitems 0..2 execute the 1-tile pushback slide with Sound 64 (`flythumpa.wav`) at launch, and Sound 65 (`flythumpb.wav`) at Subitem 3 (arrival).
    - `*gb*` ("Ground Bounce"): Full tumbling collision bounce sequence (`aggb*`, `abgb*`, `afgb*`, `acgb*`, `asgb*`, `atgb*`). Subitem 0 triggers Sound 64 (`flythumpa.wav`), Subitems 0..5 represent airborne tumble flight, and Subitem 6 (tick 6, ~300ms) triggers Sound 65 (`flythumpb.wav`) upon ground impact/landing.
    - `*gf*` ("Grab Food"): Food harvesting / grabbing sequence (`aggf*`, `abgf*`, `afgf*`, `acgf*`, `asgf*`, `atgf*`), triggering Sound 66 (`harvest.wav`) or Sound 77 (`harvest_alt.wav`).

  - Renderer authentically maps `UnitState::Bounce` to action `"gb"`, and `UnitState::Flinch` to action `"gh"`.

- **Re-Collision Loop Breaking & Cascade Preservation**:

  - Upon collision bounce resolution, both the displaced unit and the anchor unit clear their paths (`clear_path()`), reset their attack targets (`attack_target_id = 0`), enforce a 20-tick attack cooldown, and synchronize their Combat AI guard anchors to their respective positions.

  - This completely prevents the infinite bounce loop where units repeatedly walk back into the same collision tile after bouncing, while authentically preserving domino cascade bouncing when a tumbling ant impacts an occupied neighbor.

#### 14. Walking Animation Completion Before Attack, Discrete Adjacency & Scuffle Immunity (`Ants.exe` `0x101a86a`, `0x1020de7`, `0x10215cb`)

- **Walking Locomotion Completion Before Melee Strike**:

  - In authentic 1998 engine behavior, attacking ants traversing towards an adjacent tile must completely finish their walking stride into the tile center before transitioning to `UnitState::Attacking`.

  - Melee attack execution (`execute_melee_attack`) strictly rejects attacks while `attacker->state == UnitState::Walking`.

  - Autonomous pursuit logic in `sim_engine.cpp` allows walking units to complete their waypoint step before evaluating strike eligibility.

  - Strict Chebyshev tile distance (`dist <= 1`) replaces premature sub-tile pixel bounding boxes (`px_dx <= 36 && px_dy <= 36`), preventing mid-stride interruption.

- **Elastic Separation Immunity During Bounce, Knockback & Scuffle**:

  - Units in physical flight or tumbling states (`UnitState::Bounce`, `UnitState::Knockback`, or `is_in_scuffle == true`) are strictly exempted from ground-plane mutual elastic separation.

  - Prevents premature continuous pixel repositioning from overwriting discrete tile coordinates (`set_pixel_pos`) back to the collision source tile when the fight dust cloud clears.

- **Combat AI Guard Anchor Synchronization**:

  - When displaced by a collision bounce cascade or when holding ground as an anchor ant, the unit's `guard_anchor` and associated `CombatAIController::anchor_tx_/anchor_ty_` are immediately updated via `set_guard_anchor()`.

  - Prevents combat guard AI from interpreting collision displacement as a deviation from its post and marching back into the stationary collision partner.

#### 15. Disassembly Routines: Base Healing, Food Drops, and Knockback Deflection Search (`Ants.exe` `0x0101e204`, `0x0101e20d`, `0x0101e165`, `0x0101b5cf`, `0x0101d8ed`, `0x010202e7`)

- **Proportional Base Healing Dwell (`0x0101e204` & `0x0101e20d`)**:

  - `FUN_0101e204`: Computes missing health points `sVar2 = 10 - hp` (`max_hp = 10`).

  - `FUN_0101e20d`: When `sVar2 != 0`, calculates underground dwell duration as `sVar2 * 200ms` (4 simulation ticks per missing HP healed).

  - Unwounded food depositors dwell the default 4 ticks (200ms); severely wounded ants (e.g. 1 HP remaining) dwell `9 * 4 = 36` ticks (1,800ms) while undergoing underground treatment before emerging fully restored to 10 HP.

- **Base Score Deposit and Exit Routine (`0x0101e165`)**:

  - `FUN_0101e165`: Triggered upon completing underground dwell.

  - Adds carried food points (`+0xf0`) to player score and triggers Sound 44 / Sound 87 (`scoreup.wav`).

  - Clears carried inventory (`FUN_0101ac8c(this, 0, 0)`).

  - Restores health: `hp = hp + (10 - hp) = 10`.

- **Lethal Melee Damage Item Drop (`0x0101b5cf`)**:

  - `FUN_0101b5cf`: When an ant takes lethal damage from combat strikes (`0x01022c57` / `FUN_01022c57`), any carried food item in its inventory is immediately spawned as a dropped food lunchbox on the victim's current grid cell.

- **5-Angle Knockback Deflection Search (`0x0101d8ed`)**:

  - `FUN_0101d8ed`: When an ant is pushed or flung by melee attacks or collisions, the engine tests 5 directions in strict priority order:
    1. Primary direction along strike vector: `base_dir`
    2. +45° deflection: `(base_dir + 1) % 8`
    3. -45° deflection: `(base_dir + 7) % 8`
    4. +90° deflection: `(base_dir + 2) % 8`
    5. -90° deflection: `(base_dir + 6) % 8`

  - If all 5 trajectories are obstructed by solid barriers or obstacles, it returns direction index `8` (no knockback displacement).

- **Anthill Ingress & Approach Corridor Routing (`0x010202e7`)**:

  - `FUN_010202e7`: Tries to pathfind directly to the anthill entrance hole `(bx + 1, by + 1)`. If obstructed by queued units or obstacles, falls back to the designated approach coordinate at offset `+0x46` `(bx + 2, by - 2)`, followed by concentric Chebyshev ring expansion outward to queue ants cleanly without gridlock.

#### 16. Authentic Special Abilities & Base Queuing Stationarity (`Ants.exe` `0x0101bdd1`, `0x0101ba24`, `0x010202e7`, and `ants.chd` Table 4)

- **Base Queuing Stationarity Invariant**:

  - Waiting ants queued along the anthill queue slots `(bx - 1, by + 3 - k)` remain stationary in `UnitState::QueuingBase` facing East toward the mound.

  - The waiting ant must NEVER advance toward the anthill entrance while the preceding ant is depositing or emerging.

  - The preceding ant only releases the queue lock and allows the next ant to advance when it has completely finished underground dwell, completed emergence (`anim_subitem >= 16`), and stepped off the entrance hole tile (`pos != hole`).

- **Bomber Bomb Defusal (`abdb`, 22 Ticks / 1,100ms)**:

  - Subitem 3 (Tick 5 / 250ms): Sound 73 (`bombdrop.wav` / `BombDefuseGrab`) fires as the Bomber grabs the bomb.

  - Subitem 6 (Tick 13 / 650ms): Sound 74 (`bombmuffle.wav` / `BombBodySquash`) fires, the ant squashes the bomb with its body, composited with `9difuse1..3.bmp` smoke puff frames, and the bomb object is removed from the grid.

  - Subitems 9..11 (Ticks 14..22): Bomber rises back upright; at Tick 22 returns to `Idle` with zero post-animation cooldown.

- **Fire Ant Fire Extinguishing (`afxf`, 12 Ticks / 1,200ms)**:

  - Subitem 4: Sound 69 (`fireextinguish.wav` / `FireExtinguish`) fires and the firewall object is extinguished from the grid.

  - Subitem 12: Fire Ant returns to Idle.

- **Thief Ant Base Infiltration (`atcr`, 33 Ticks / 3,510ms)**:

  - Subitem 19: Sound 48 (`anthill.wav`) + Sound 84 (`steala.wav` / `ThiefDive`) fire, dispatching the in-game alarm news flash to the victim team.

  - Subitem 26: Sound 85 (`stealb.wav` / `ThiefRummage`) fires as the Thief ransacks the subterranean storehouse.

  - Subitem 31: Sound 86 (`stealc.wav` / `ThiefEmerge`) fires as the Thief resurfaces with loot.

  - Subitem 33: Thief collects up to 50 food points into inventory and begins return march to home base.

- **Worker Ant Food Harvest (`aggf`, 6 Ticks / 420ms)**:

  - Subitem 4: Sound 77 (`harvest_alt.wav`) fires as the worker bites and lifts the food portion.

#### 17. Base Ramp Concurrency, Invulnerability Tasks, Water Melee Isolation & Terrain Passability (`Ants.exe` `0x101f780`, `0x101cb0c`, `0x1024ae4`, `0x10049b8`, `0x1020951`)

- **Base Mound / Ramp Max Concurrency Limit (`0x0101f780` / `FUN_0101f780`)**:

  - `FUN_0101f780` lines 22796–22832: When evaluating if an ant can step onto the anthill base ramp/mound tiles (`+0x36`, `+0x38`, `+0x3a`, `+0x3c`, `+0x3e`, `+0x40`), the engine counts all friendly ants currently in state `1` (Walking/Entering) across those tiles:

    ```c
    if ((uVar6 & 0xffff) + (local_18 & 0xffff) + (uVar4 & 0xffff) == 2) { return 0; }
    ```

  - The original 1998 executable strictly enforces a hard ceiling of **2 friendly ants maximum** concurrently traversing the ramp mound tiles. Any 3rd ant attempting to step onto the ramp is blocked (`return 0`) and must remain in the queuing perimeter until one ant enters the hole or exits.

- **Water Melee Combat Isolation (`0x0101cb0c` / `FUN_0101cb0c`)**:

  - `FUN_0101cb0c` lines 20599–20605: Melee attack adjacency validation inspects the underlying terrain type for both the attacker's coordinates and the target's coordinates:

    ```c
    sVar1 = FUN_01008af7(*(void **)(DAT_0104b350 + 0x494c), *param_1, param_1[1]);
    if (sVar1 != 2) {
      sVar1 = FUN_01008af7(*(void **)(DAT_0104b350 + 0x494c), (ushort)local_c, local_c._2_2_);
      if (sVar1 != 2) return 1;
    }
    ```

  - If either cell is terrain type `2` (water), melee attack registration strictly returns `0` (rejection).

  - Consequently, ants on land cannot execute melee attacks against swimming ants in water, and swimming ants in water cannot execute melee attacks against land units.

- **Hatch & Emergence Temporary Invulnerability (`0x01024ae4` / `s_Invuln_010474c4`)** *(unsupported, found by the B4-1 research of the hatch: `HATCHTSK` never creates an `Invuln` task, and §5.35 says that an ant that emerges is neither hidden nor invulnerable, which the remake follows; the three lines below are kept as they were written)*:

  - Upon emergence from an anthill or completion of state transitions, ants are assigned an `Invuln` task (`s_Invuln_010474c4` at VA `0x1024ae4`) setting `+0x78 = 1`.

  - While `+0x78 == 1`, `FUN_0101cb0c` line 20595 (`*(int *)((int)this + 0x78) != 1`) immediately rejects all incoming attacks and combat damage, preventing spawn-camping at the anthill hole.

  - The invulnerability flag is cleared upon task expiry via `FUN_01024b8a` (`*(undefined4 *)(*(int *)(param_1 + 0x34) + 0x78) = 0`).

- **Terrain Passability Matrix (`WORD_ARRAY_010049b8` at VA `0x10049b8`)**:

  - Binary table at `0x10049b8` defines the 6 terrain layers: `[1, 1, 0, 1, 1, 0]`.
    - Layer 0: Normal land (passable)
    - Layer 1: Mud/sand (passable; bombs prohibited)
    - Layer 2: Water (0; passable only for `AntType::Swimmer` at `FUN_0101f780` line 22739)
    - Layer 3: Paved walkway (passable)
    - Layer 4: Dirt Bridge (passable)
    - Layer 5: Hard obstacle / rock barrier (0; impassable)

- **Pathfinding Infinite Cost Sentinel (`0x01020951` / `FUN_01020951`)**:

  - Original A* routing uses `8000` as the infinite-cost sentinel value for blocked or impassable nodes.

- **Authentic Match Announcement Strings (`0x1047468`)**:

  - Game Start: `"Game started! Go get that food!"` (binary string at VA `0x1047468`).

  - News Flash Header: `[%ld:%02ld] News Flash` (binary string at VA `0x1047258`).

  - Teammate Chat Prefix: `%s (To Teammate):` (binary string at VA `0x1047428`).

#### 18. Anthill 3 Blocked Tiles Forbidden Ability Geometry, Occupied Bumping, Mud Animation Cancel & Power-Up Immunity (`Ants.exe` `0x101d8a4`, `0x101d762`, `0x101cad6`, `0x100ee03`)

  - Additionally, `FUN_0101d762` line 21163 invokes `FUN_0100cf0f`, strictly rejecting bomb and fire placement on **any tile occupied by a living ant**.

- **Occupied Destination Bump Reaction (`0x0101cad6`–`0x0101cb05`)**:

  - When an ant navigates towards a destination that has become occupied by another ant, `0x101cae3` pushes `0xdc` (220 = Animation `bump`), calls `0x10100e5`, triggers `SoundID::Bump` (Sound 47, `bump.wav`), and clears waypoints.

  - The arriving ant stops cleanly on the adjacent available tile without displacing the occupant or becoming stuck in infinite pathing loops.

- **Mud Animation Cancel / "Mud Humping" Locomotion**:

  - Traversing mud naturally runs a struggle animation cycle at ~0.65× speed. *(Superseded: mud walks 2 px per 60 ms, i.e. 0.42x grass, and "humping" is the GoTo snap to the tile centre; see §5.32.)*

  - *(Corrected for the bots' milestone B4-1, 2026-10-03: the "3.0 px micro-propulsion step (`3 << 16`)" that stood here is in neither the program nor the remake.)* What a click on the NEXT tile in the middle of a step does is `GoTo` (`0x101fc50`): every new order **snaps the ant to the centre of the tile it stands on** (the snap `0x101fcce` .. `0x101fcf6`; an order whose goal is the tile the ant is on is skipped, `0x102880d` .. `0x10288dc`) and restarts the walk, whose first step comes 270 ms later (§5.32; the remake's `go_to` does the same and the two agree). The snap goes to the centre of the tile that the ant's pixel position is in, so it is forward once the ant has crossed the edge into the next tile and backward before, and the same click is a gain or a loss. Measured in the remake's engine on mud, where an ant needs 18 ticks (50 ms) per orthogonal tile and 24 per diagonal one (grass 8 and 10: nothing to gain there, the snap only ever costs): a worker ordered onto the next tile, then onto the one after it *c* ticks later, arrives sooner than with one order to the second tile by 3 ticks at c = 15 going east or south (2 and 1 at c = 16 and 17), by 2 at c = 16 going west or north, by 5 at c = 19 on the diagonal walks to the south east, north east and south west and by 4 at c = 20 to the north west; an order before the window snaps the ant BACK and loses c ticks (up to 14), one after it loses c - 18. A click-perfect person reaches 15.8 ticks per mud tile instead of 18.5. Mud is 0 percent of the walking of TINY, SMALL and ISLANDS, 8 percent on MEDIUM, 5 on GAUNTLET and 4 on TREASURE (9.8 for the seat that crosses the causeway of 3-tile mud runs), so even a perfect person gains about 1.5 percent of the walking time of a match. A bot at Hard looks every 4 ticks and its order leaves 6 to 10 ticks later: it cannot hit a window of 3 ticks (reacting to a new tile loses 61 to 97 ticks per 11 tiles, a timed open loop gains at most 1 to 1.5 ticks per mud tile on diagonal runs), so no bot plays it (`docs/BOTS.md`, "Strategy").

- **Power-Up Standing Immunity**:

  - When an ant stands on top of an uncollected or dropped power-up, it is 100% immune to incoming melee attacks (all melee attack orders against it are rejected, and `take_damage` with melee sources deals 0 damage).

  - Standing ants on power-ups are strictly excluded from displacement during same-tile collision resolution and cannot be bounced by `bounce_unit_cascade`.

#### 19. Blocked Emergence Postponement, Moving-Ally A* Passability & Egg Economy Verification (`Ants.exe` `0x1010aca`, `0x1010c14`, `0x10250fe`, `0x10209cd`, `0x101b78a`)

- **Egg Economy & Hatch Requirements Ground Truth (`0x1010aca` & `0x1010c14`)**:

  - In `Ants.exe` Capstone disassembly at `0x1010aca`:
    - `0x1010ae1`: `cmp word ptr [edi + 0x4a], bx` (compares current player egg count with 0).
    - `0x1010ae5`: `jne 0x1010b11` $\rightarrow$ If egg count == 0, calls `0x100e944` ("out of eggs") and jumps to `0x1010b42`, returning immediately without hatching.
    - `0x1010b56`: `cmp dword ptr [edi + 0x54], esi` (compares food score with `0xc8` = 200).
    - `0x1010b59`: `jge 0x1010b9e` $\rightarrow$ If food score < 200, plays `cantgo.wav` (`0x102bd7e`) and returns without hatching.
    - `0x1010c28`: `call 0x1010cc9` with `-200` (deducts 200 food points).
    - `0x1010c37`: `mov word ptr [esi + 0x4a], ax` (decrements egg count by 1).

  - Ground truth: The original 1998 executable strictly requires **both** `eggs > 0` **and** `score >= 200` for every hatch, and deducts both 200 food points and 1 egg.

- **Blocked Base Emergence Postponement (`0x10250fe`–`0x1025108`)**:

  - In `HATCHTSK` emergence routine (`0x1025072`), when an incubating/hatching ant's subterranean timer completes and it is ready to surface onto the anthill hole `(bx + 1, by + 1)`:

    ```asm
    0x10250fe: push 1
    0x1025100: mov dword ptr [ebx + 0x1c], 0x3e8   ; 0x3e8 = 1000 ms (20 ticks at 20Hz)
    0x1025107: pop eax
    0x1025108: jmp 0x102522f                       ; returns 1 (delays emergence)
    ```

  - If any living ant is currently occupying the surface hole tile, emergence is postponed by exactly **1000ms (20 ticks / 1.0s)**. The ant remains cleanly underground until the surface hole clears.

- **A* Pathfinding Moving-Ally Passability (`0x10209cd`)**:

  - In `FUN_01020951` / `0x1020951` (edge traversal passability and cost function):

    ```asm
    0x10209cd: cmp dword ptr [eax + 0xd8], 0        ; check if ally has active movement vector/path
    0x10209d4: jne 0x1020a29                       ; if moving -> PASSABLE!
    ```

  - Actively walking friendly ants (`state == UnitState::Walking`) are excluded from pathfinding dynamic obstacles. Friendly ants plan direct paths knowing marching comrades will vacate intermediate tiles. Only stationary friendly ants and non-target enemies act as impassable obstacles (`cost = 8000`).

- **Ant Order Confirmation Voice Dispatches (`0x101b78a`)**:

  - Capstone disassembly of `0x101b78a` confirms Worker (0) and Combat (4) jump to `ret 4` without vocal move acknowledgments, while Bomber (28), Fire (16), Thief (13), and Swimmer (25) play move lines. All 6 types possess selection "ready" voices (`0x101b711`).

#### 20. Dropped Food Lunchbox Sprite, Walk Cycle Bytecode, Edge Panning & Minimap Geometry (`Ants.exe` `0x1005820`, `0x1026aa3`, Table 4 Anim 356 & Anim 123–128)

  - Previously, the HUD 34×40 icon (`lunchicon.bmp`) was mistakenly rendered on the map ground when an ant carrying food died. Rendering Sprite 513 at `sx + 9, sy + 8` restores authentic 1:1 visual fidelity with the 1998 executable.

- **Walk Cycle Bytecode & Stride Synchronization (Table 4 Anim 123–128)** *(superseded by §5.32: walk animations are the per type / terrain / direction table entries with 40–60 ms frames)*:

  - Table 4 walk animation entries for all ant species (`agwg301`, `abwg301`, `acwg301`, `afwg301`, `aswg301`, `atwg301`):
    - 12 subitems per walk cycle.
    - Each subitem specifies `duration_ms: 50` (exactly 1 simulation tick at 20Hz) and `dy: 4` (4 pixels displacement per subitem).
    - Total cycle: 12 frames × 4 pixels = 48 pixels (exactly 1 standard tile).
    - At 1 tick per frame, an ant advancing at standard speed (4 px/tick) covers exactly 48 pixels in 12 ticks (600ms), perfectly synchronizing leg locomotion 1:1 with ground displacement without artificial slide or skate.

- **Camera Edge Panning & Scroll Speed Geometry (`0x1026aa3` / `FUN_01026aa3`)**:

  - In `FUN_01026aa3`, viewport boundary proximity triggers camera edge panning when the cursor is within `0xc` (12 pixels) of the screen edges:

    ```c
    if (cursor_x < 12) { cam_x -= scroll_step; }
    ```

  - Map scroll speed slider ranges from 120 px/s to 360 px/s (10–25 px/tick at 20Hz), centered at 240 px/s.

- **Minimap Radar Geometry & Viewport Wireframe Clamping (`0x1005820`–`0x1005823`)**:

  - Radar panel rect: X in [480, 599] (width = 119), Y in [35, 126] (height = 91).

  - Authentic minimap draws solid player-colored squares for anthill bases and single-pixel ant dots without artificial white outline borders.

  - Viewport rectangle wireframe clamping enforces `fy2 = ry + rh = 126` when scrolled to the bottom limit of the map, eliminating any 1-pixel gap at the base of the radar display.

- **Mid-Stride Re-Order Forward Routing (`0x101b590` / `FUN_0101b590`)**:

  - When a moving ant receives a new move order while in mid-stride between `pos` and `next_wp`:
    - If the target destination is `next_wp`, the ant finishes its step into `dest`.
    - If `next_wp` is passable, the engine paths forward from `next_wp` and prepends `next_wp`, allowing the ant to step cleanly into `next_wp` and execute a crisp forward turn onto the new path instead of making an artificial 180° backward U-turn or oscillating between tiles.

- **Thief Anthill Infiltration Subterranean Animation & Audio Ground Truth (`0x101f3e0`, Table 4 Anim 1095 `atcr501`)**:

  - Thief infiltration animation `atcr501` (33 subitems, 3,510ms total duration):
    - Subitem 19 (1,050ms): Plays `SoundID::BaseAlarmSiren` (58) + `SoundID::ThiefDive` (`steala.wav`, Sound 84) and overlays `9hillh2.bmp` (2503) & `atcr511.bmp` (2504) as the thief dives into the mound.
    - Subitem 26 (1,950ms): Subterranean rummaging with `SoundID::ThiefRummage` (`stealb.wav`, Sound 85).
    - Subitem 31 (2,850ms): Thief emerges with `SoundID::ThiefEmerge` (`stealc.wav`, Sound 86).
    - Subitem 33 (3,510ms): Point deduction occurs. Crucially, `SoundID::BaseScoreDn` (`scoredn.wav`, Sound 88) is played ONLY if food points were actually stolen (`victim_score > 0`). If the victim base had 0 points, the thief still infiltrates and sits unattackable underground for the full duration, but emerges empty-handed without triggering `scoredn.wav`.

- **Anthill Geometry & Defensive Firewall Placement (`0x101d8a4`, `0x100ee03`)**:

  - The anthill mound sprite `9hill.bmp` spans 4×4 tiles. Bombs and fire walls are refused on the entrance `(bx + 1, by + 1)`, the raid tile `(bx + 3, by + 2)` and the three tiles
    directly above the mound (`(bx, by - 1)`, `(bx + 1, by - 1)`, `(bx + 2, by - 1)`); there is no rule for the left of the mound (corrected in v0.0.53, see above).

  - Fire Ants can place firewalls (`wallup04`) on the other tiles around the mound, e.g. to trap infiltrating thieves or protect the base from theft.

- **Action Pedestal Button Animations (Table 4 Anim 1215 `trnbalyd` & Anim 1216 `trnbmovu`)**:

  - Selection Pop-Up (`trnbmovu` / Table 4 Anim 1216): 9 subitems, 60ms each (540ms total), shifts pedestal up into position.

  - Deselection Retraction (`trnbalyd` / Table 4 Anim 1215): 9 subitems, 60ms each (540ms total), retracts pedestal into the base cavity.

  - Move button is hidden when 0 friendly ants are selected.

#### 21. Match Start "Get Ready!" Modal, Mutual Friendly Bouncing & Snapped Redirection Ground Truth (`Ants.exe` `0x1017127`, `0x1021cb0`, `0x101b938`, `.rsrc` Strings 100–105)

- **Mutual Friendly Bumping (`0x1021cb0`, `Ants.exe.c` lines 25255–25335)**:

  - When two friendly ants collide into the same tile (e.g. from an enemy punch, bomb blast recoil, or navigation collision):
    - In `Ants.exe.c` lines 25304–25335, the engine updates **both** units simultaneously:

      ```c
      FUN_0101ace3(this, 0);
      FUN_0101ab56((int)this);
      FUN_0101ace3(piVar6, 0);
      FUN_0101ab56((int)piVar6);
      FUN_0101da46((int)piVar6);
      FUN_0101da46((int)this);
      ```

    - Unit 1 (`piVar6`) deflects to adjacent tile `(iVar2 + 0x10, 0x12)`.
    - Unit 2 (`this`) deflects to opposing adjacent tile `(iVar2 + 0x14, 0x16)`.
    - Both units clear paths, enter `UnitState::Bounce` (4–6 ticks recoil), and play `SoundID::Bump` (47) and `SoundID::FlingThumpB` (65).
    - **Neither ant remains static!** Both ants bounce away from each other like billiard balls rebounding upon impact.
    - Visuals: **NO `battle` dust cloud** (Anim 56) and **NO `combatnetfairy.wav`** (Sound 3, rolling-around-in-dirt scuffle sound effect).

- **Enemy Collision Scuffle Separation (`Ants.exe.c` lines 19875–19908)**:

  - Checked via `*(short *)(DAT_0104b350 + 0xf2a) != *(short *)((int)this + 0x56)`.

  - Enemy collisions exclusively trigger sequence 56 (`battle`) and Sound 3 (`combatnetfairy.wav`).

  - Overhead health bars and green selection brackets are suppressed while an ant is covered by the `battle` dust cloud.

- **Snapped Movement Redirection & Stride Integrity (`Ants.exe.c` lines 20020–20030 & 20076–20090)**:

  - In `Ants.exe`, movement is discrete tile-to-tile strides (cardinal or 45° diagonal) between tile centers.

  - Waypoint step advances and turns occur when reaching tile center (`(ushort)iVar5 < 3 && (ushort)iVar10 < 3`).

  - Mid-walk redirections cancel the remaining step, snap to the nearest tile center, reset `anim_tick = 0`, and path directly from that tile center, completely eliminating sub-tile axis-decoupled easing curves and detours.

- **1 HP Automatic Return to Base & Hit Recoil Non-Interruptibility (`Ants.exe` `0x01021627`, `0x01021664`, `0x0101b8cb`, `0x0101ee84`, `0x0101ad02`, `0x0101dded`)**:

  - **Damage Handling Disconnection**: In `FUN_01021627`, sustaining damage strictly decrements health (`*(short *)((int)this + 0x74) += -1`) and sets the hit reaction frame. It NEVER issues movement orders or calls `join_base_queue`.

  - **Hit Recoil Non-Interruptibility**: In `FUN_01021664` line 24256 and `FUN_0101b8cb` line 19883, states 10 (`Flinch` / `*gh*`), 14 (`Knockback` / `*gf*`), and 19 (`Bounce` / `*gb*`) are non-interruptible states. All retreat transitions and move orders are strictly blocked while the ant is displaced, sliding, or tumbling.

  - **Mutual Bounce Preservation**: A 2 HP ant struck into another ant drops to 1 HP, completes its full push slide in `Flinch`/`Knockback`, collides with the other ant, triggers mutual bounce into separate tiles, and plays the bounce recovery animation. The retreat command NEVER prematurely cancels the slide or bounce.

  - **Idle Transition Trigger**: In `FUN_0101ee84` line 22579 and `FUN_0101ad02` line 19478, only when `Flinch` (14 simulation ticks / 700ms) or `Bounce` finishes and the unit transitions into `Idle` (state 0) does `FUN_0102151a` -> `FUN_0101dded` evaluate `hp == 1` and pathfind the ant back to base to heal.

### 6.3 Early ground-truth notes (v0.0.9 - v0.0.18, 2026-09-10 to 2026-09-13): Bomb Detonation, Z-Ordering, Dud (`a*bu`), Landing Stun (`a*sd`), Food Duplication & Combat Locomotion

#### 1. Bomb Z-Ordering & Visual Hierarchy (`Ants.exe` `0x1009d49`, `0x1008089`, `0x10088e7`, `0x10089bd`, `0x1010008`)

- **Viewport Frame Rendering Pipeline (`FUN_01009d49`)**:

  ```c
  FUN_01008089(*(void **)((int)this + 0x4c), piVar1, 1); // 1. Layer 1 Terrain
  FUN_01008089(*(void **)((int)this + 0x4c), piVar1, 2); // 2. Layer 2 Structures & Ground Bombs
  FUN_010088e7(*(void **)((int)this + 0x4c), piVar1);    // 3. Dynamic Display List (Entities, Units, Effects)
  FUN_01008607(*(void **)((int)this + 0x4c), piVar1);    // 4. Layer 3 Canopy Overhang
  ```

- **Ground Bomb Sprite (Layer 2)**: Landmines placed by Bomber Ants are written into Layer 2 grid memory with tile IDs `0x81` (Red), `0x82` (Green), `0x83` (Blue), `0x84` (Black). They render on Layer 2 underneath all dynamic entities.

- **Detonation Trigger (`0x101e6b3` / `FUN_01010008`)**:

  - Stepping on a bomb clears the tile on Layer 2 (`FUN_01007352` sets tile to `0x7ffe`).

  - Spawns transient explosion effect `bombex` (Anim ID 133 in Table 4, 10 subitems: `durs=[60, 60, 80, 100, 80, 60, 60, 60, 60, 60]`, total duration 680ms) via `piVar2 = FUN_0101a169()` and registers it into the world's dynamic display list via `FUN_01008829`.

  - In the dynamic display list (`FUN_010088e7`), entities are depth-sorted by `sort_y` (`*(short *)(iVar2 + 0x3a)`).

  - An ant triggered by the blast is launched airborne (`altitude_z > 0`). In 2.5D top-down perspective, the airborne ant sprite is offset upward above the ground explosion plane.

  - Rendering `render_visual_effects(world)` directly **between Layer 2 structures and ant units** places `bombex` in authentic Z-order: `Layer 2 bomb tile` -> `bombex explosion cloud` -> `ant unit animation`.

#### 2. Bomb Dud Scorch (`a*bu301`, Table `0x1004518`) & Landing Stun (`a*sd301`, Table `0x10045d8`)

- **Exact Ground Truth Dud Logic (`Ants.exe` `0x101c208`, `0x102313f`, `0x101df09`, `Ants.exe.c` lines 20931–20961 & 24440–24472)**:

  - When an ant steps onto an active bomb tile (`case 0xa`, lines 20931–20961), the engine executes an authentic PRNG roll:

    ```c
    uVar8 = FUN_010345c0(); // rand() (MSVC LCG: seed * 0x343fd + 0x269ec3)
    if ((int)uVar8 % 100 < 0x14) { // Exactly 20% probability (0x14 == 20)
        // DUD / IN-PLACE SCORCH:
        dest_x = current_x;
        dest_y = current_y; // 0-tile displacement, remains on bomb tile
    } else {
        // FULL DETONATION (80% probability):
        FUN_0101df5d(this, &current_pos, &dest_pos, 4, 0, 0, 0); // Knockback 4 tiles away
    }
    ```

  - **Bomb Damage Invariant (`FUN_01021a6f` at `0x1021ae3` & `0x1021aeb`)**:
    - Stepping on a bomb invokes `FUN_01021a6f`. The function executes `call 0x1021627` **twice unconditionally** before the position comparison:

      ```x86
      0x1021ae0: push ebx
      0x1021ae1: mov ecx, esi
      0x1021ae3: call 0x1021627   ; Damage 1 (HP -= 1)
      0x1021ae8: push ebx
      0x1021ae9: mov ecx, esi
      0x1021aeb: call 0x1021627   ; Damage 2 (HP -= 1)
      ```

    - Therefore, **both dud and full explosion deal exactly 2 HP damage**.

  - **Dud Branch (20% Roll)**:
    - Target destination equals current position (`dest == current_pos`). Flag `this[0x2d] = 1`.
    - In `0x102313f` / `0x101df09`, flag `1` selects reaction type **4**, which transitions the unit into **Action 19 (`0x13`)**: **`a*bu` (Burn / Scorch Dud animation)**.
    - Animation `a*bu301` (`agbu301`, `abbu301`, `afbu301`, `acbu301`, `asbu301`, `atbu301` from Table `0x1004518`) plays in place for 11 ticks (~550ms) with smoke puff sprites (`*bu301..303`), firing **Sound 64 (`flythumpa.wav`)** at subitem 0 and **Sound 65 (`flythumpb.wav`)** at subitem 5.
    - The bomb tile on Layer 2 is cleared (`0x7ffe`).
    - Upon finishing the 11-tick burn sequence (line 22569: `case 0x13:`), the ant recovers into the dazed stun state (`a*sd301`).

  - **Full Detonation Branch (80% Roll)**:
    - Target destination is 4 tiles away (`dest != current_pos`). Flag `this[0x2d] = 0`.
    - Flag `0` selects reaction type **1**, transitioning the unit into **Action 14 (`0xe`)**: **`a*gb` (Ballistic Knockback & Airborne flight)**.
    - Spawns `bombex` (Anim 133) with `bombexp.wav`. Ant flies 4 tiles along parabolic altitude trajectory (`altitude_z > 0`).
    - Upon landing on the ground, the ant enters `UnitState::Stunned` and plays `a*sd301` (dazed spinning stars with Sound 70 `stun.wav`).

#### 3. Multi-Ant Food Access & 1998 Duplication Exploit (`0x102151a`, `0x101fc50`, Action 3 `aggf`, Anim 356 `lunchbox`)

- *(Superseded by 5.40: unverified and contradicted by the disassembly; the classification decides, there is no food flag and no exception for friendly ants.)* In `FUN_0102151a` and `FUN_0101fc50`, when an ant is commanded to eat food (`is_food_order`), friendly ants occupying target cells are not treated as pathfinding obstacles.

- Multiple ants can walk onto or stand around the food node (Chebyshev distance $\le 1$) and enter Action 3 (`aggf`, 7-frame bite cycle over 8 ticks / 420ms).

- When the 8-tick bite completes, each biting ant receives a morsel into its lunchbox. Because remaining bites are checked at the start of a bite rather than each frame, all concurrent biters receive their food morsels even if the food node's counter reaches 0 mid-bite (1998 Food Duplication Exploit).

- Ground dropped lunchboxes (Table 4 Anim 356) award points to all concurrent ants reaching the lunchbox on the collection tick before removal.

#### 4. Combat Ant AI Locomotion & Melee Punch Mechanics (`0x101ace3`, `0x1021494`, `0x1021627`)

- The Combat Ant never teleports. In `Ants.exe`, it paths smoothly along waypoints at standard speed with `acwk`.

- When within distance $\le 1$, it executes `acat` (Table 4 Anim IDs 901–905: `acat201`, `acat301`, `acat701`, `acat801`, `acat901` across 6 subitems: `[80, 100, 60, 80, 100, 120]ms`, total 540ms / 11 simulation ticks).

- Subitem 2 connects with `flag = 4`, dealing 2 HP damage and launching the target with ballistic parabolic trajectory.

- Enforces an authentic **12-tick attack cooldown** (`attack_cooldown_ticks = 12`) with zero aggro or pursuit during cooldown.

#### 5. Anthill Mound Anchor Offsets & Thief Infiltration Corridor (`0x100ee03`, `Ants.exe.c` lines 9656–9690)

- The base mound graphic uses Table 4 offsets from anchor tile `(min_x + 1, min_y + 1)`:

  - Red (`REDHILL`, Anim 247): `dx: -32, dy: -18` (`+14px` vertical shift relative to `min_y * 32`).

  - Green (`GRNHILL`): `dx: -32, dy: -25` (`+7px` shift).

  - Blue (`BLUHILL`): `dx: -32, dy: -20` (`+12px` shift).

  - Black (`BLKHILL`): `dx: -32, dy: -24` (`+8px` shift).

- Shifting Red down 14px aligns the bottlecap thief hole squarely onto grid row 36 (`(24, 36)`).

- Thief ant infiltration is strictly checked on the 4 right-flank tiles `X = base_min_x + 3, Y in [base_min_y, base_min_y + 3]`.

- The column to the right `X = base_min_x + 4, Y in [base_min_y, base_min_y + 3]` comprises open land tiles where Fire Ants can place up to 3 firewalls and Bomber Ants can place landmines.

#### 6. HUD Pedestal Yellow Glow Animations (`butdefl`, `butdefr`), Bomb Targeting Cursor (`c_targ1`), and Dialog Parity (`std_dialg`, `0x1026aa3`, `0x1015b65`)

- **HUD Pedestal Animated Yellow Glow (`Ants.exe.c` lines 29201, 29206, 29571–29595)**:

  - In the 1998 engine, the bottom action buttons on the right-hand sidebar feature contextual highlighting via animated pulsing yellow glow overlays:
    - **Move Pedestal (Left Pedestal at `(477, 163)`)**:
      - Trigger condition `0x5510`: Set when cursor mode is 3 (`c_mov1`, hovering over passable land) or 7 (`c_food`, hovering over food/lunchbox).
      - Graphic: Animation 1190 (`butdefl`), cycling 4 frames using sprites 2591–2599 across a 510ms duration.
      - Sprite pixels contain authentic yellow border glow (`RGB(211, 183, 0)` / `#D3B700`).
    - **Special Ability Pedestal (Right Pedestal at `(538, 163)`)**:
      - Trigger condition `0x5514`: Set when holding right-click to use a special ability, active special ability placement mode, or hovering over a bomb / ability target (`c_targ1`).
      - Graphic: Animation 1222 (`butdefr`), cycling 4 frames using sprites 2591–2599 across a 510ms duration.

- **Bomber Bomb Targeting Cursor Mode 4 (`c_targ1`, Anim 43)**:

  - When hovering over any planted bomb on the map (`grid.has_bomb_at({tx, ty})`), the cursor dynamically switches to the Mode 4 target reticle (`c_targ1`, Anim 43).

  - While a Bomber Ant is actively placing a bomb (`UnitState::PlantingBomb`, playing `absb301`), the cursor remains locked to Mode 4 target reticle across the playfield.

  - When right-clicking with a Bomber Ant selected over valid bomb placement ground, the cursor displays the Mode 4 target reticle.

- **Seamless Bomb Placement Timing (`absb301`, 28 Ticks / 1.4s)**:

  - Table 4 animation `absb301` has 9 subitems: `[100, 160, 160, 200, 180, 200, 200, 100, 100]ms` totaling 1,400ms (exactly 28 simulation ticks @ 20Hz).

  - Subitem 0–4 depicts the bomber crouching and taking the bomb out of its backpack.

  - Subitem 5 (`absb501`) sets down the bomb in the ant's sprite.

  - At tick 28 (when `PlantingBomb` completes and transitions to `Idle`), the map entity bomb is placed onto Layer 2, providing a seamless visual transition with zero double-bomb artifacts.

  - Placing a bomb triggers Sound 90 (`bombpick.wav`) at tick 14 (`flag = 4`).

- **Ability Cardinal Placement Pathing Around Intervening Units**:

  - Special ability placement candidate tiles evaluate orthogonal neighbors ($dx = 0, |dy| = 1$ or $dy = 0, |dx| = 1$).

  - If a neighbor tile is occupied by an ant (`has_living_ant_at`), the placement algorithm filters it out and routes the unit smoothly around the obstacle to an unblocked cardinal tile.

- **Water Bomb Order Rejection**:

  - Right-clicking or issuing a bomb order onto water immediately rejects the order with `SoundID::CantGo` audio feedback and ant refusal.

- **Knockback Ballistic Orientation Parity (`Ants.exe` `0x1002b40` / `Ants.exe.c` lines 16785–16824, 24010–24070)**:

  - In `Ants.exe`, an ant struck by a melee attack or blast wave faces toward the attacker/epicenter prior to impact.

  - Ballistic flight does not reverse the ant's orientation 180°; the ant remains facing the blast origin throughout flight until landing and entering `a*sd301` dizzy stun.

- **Authentic Match Start Modal & Selection Marquee**:

  - Ready modal: Authentic composite `std_dialg` (Animation 67, 320×224) background with dark purple/charcoal text `#1F1733` (`ColorRGBA{31, 23, 51, 255}`).

  - Unit selection drag marquee tool: Authentic bright red border `RGB(220, 0, 0)` (`ColorRGBA{220, 0, 0, 255}`).

  - **Quick Help Screen & START! Navigation**:
    - Main help plate rendered from `qh_screen` (Anim 101: `qh1.bmp` 232, `qh2.bmp` 231, and perimeter border frames).
    - Top-right corner contains no button (empty border).
    - Bottom-right corner renders the authentic green "START!" button from Table 4 Animation 1335 `qh_start1` at `(529, 437)`:
      - Normal: `bstart1.bmp` (Sprite 289, 98×27) at `(529, 437)`.
      - Hovered: `bstart2.bmp` (Sprite 290, 98×27) at `(529, 437)`.
      - Pressed: `bstart3.bmp` (Sprite 291, 97×24) at `(528, 438)`.
    - Clicking the START! button, or pressing Enter, Esc, C or X (docs 5.52, `FUN_010147c2`; Space does nothing), makes no sound (`qh_start3` carries sound -1) and creates the setup screen (`FUN_01014802`).

  - **Overall Flow Sequence**: `Loading` -> `QuickHelp` -> `MapSelect` -> `Playing`.

#### 7. Daisy Flower Dropper Occupancy, Standing Fire Ability Pathing, Bomb Redirection & Chain Detonations, and Shift/HUD Bomb Tile Rules (`FUN_0101df5d`, `0x1021627`, `0x1015c70`)

- **Daisy Flower Dropper Occupancy Preservation (`SMALL.LVL`, `GAUNTLET.LVL`, `ISLANDS.LVL`)**:

  - In `Ants.exe`, if an ant or anything on layer 2 that is not a power-up (a bomb, a fire wall, a lunchbox, food ...) is on the dropper's target tile `(drop_x, drop_y)` when a poll finds the interval elapsed, nothing is posted and the stamp stays; the next poll (3 s later) posts as soon as the tile is free (see 5.29: the poll, the stamp and the landing).

- **Fire Ant Ability Placement Pathing Through Fire**:

  - When standing on fire and placing a fire wall adjacent, and then placing another further away, the Fire Ant treats fire tiles as passable staging neighbors and prioritizes current tile `cand == unit->pos` (distance 0) without self-blocking or detouring north around terrain.

- **Chain Bomb Detonations & Dud Rolls**:

  - When an airborne ant lands on a bomb tile from flight, it detonates the bomb immediately.

  - Both standard and chain bomb detonations evaluate the authentic 20% dud probability (`prng.rand() % 100 < 20`). On a dud, the ant takes 2 HP damage and plays the scorch burn animation in place (`a*bu`). On full detonation, it takes 2 HP damage and launches into another airborne flight trajectory.

- **Bomb Placement Smoothness & 0px Alignment**:

  - Aligned static ground bomb destination rectangle `bomb_dst` to `{ sx + 10, sy + 0, 12, 24 }` to match Table 4 `absb` (Anim 1317) release frame `sy + 0` (`dx = 10, dy = 0` relative to target tile).

- **HUD Right-Side Bomb Tile & Hover Cursor Rules** *(corrected 2026-09-30 against `Ants.exe`, see 5.44: the first version of these notes tied the behaviour to the Shift key, which the cursor and order code never read)*:

  - A bomber that is the only selected ant (panel 3) hovering over a bomb shows `CursorType::Target` (disarm); the click is the defuse order.

  - **A group is a group move.** The stored panel `[54ec]` is 4 after a Shift click that adds an ant or a Shift drag that picks any ant (`0x1027950`, `0x1027769`: `SetPanelMode(4)` whatever the count), so a Shift-selected bomber, even the only one, gets `CursorType::Move` over a bomb and a plain group move, which walks onto the bomb and sets it off (`FUN_010202e7` gets flag 0x20 for every player order). This is how a group gets off the island of ISLANDS.

  - Non-bomber hovering over a bomb shows regular `CursorType::Move`.

  - Pedestal 2 (the ability pedestal) exists in panel 3 only: `SetPanelMode` gives slot 2 the kind 9 (hidden) in panel 4 (`0x1027f07`, `FUN_01028360(1, 1, 9, 1, 0, 1)`), so several ants, same type or not, never have it.

#### 8. Fire Extinguish Timing (`afxf`), Silent Active Action Order Rejection, Zero Ability Cooldown, and Bomber Pathing Around Friendly Bombs (`Ants.exe.c:22938-23085`, `Ants.exe.c:19435`, `Ants.exe.c:10693-10694`)

- **Silent Order Rejection in Active Action States (`Ants.exe.c:22938`, `23077-23080` / `FUN_0101ff5a`)**:

  - `FUN_0101ff5a` inspects `param_1[0x39]` (unit state). If the unit is not in state 0 (`Idle`), state 1 (`Walking`), or state 3 (`Swimming`), it returns 0.

  - In `Ants.exe.c:22938`, when `FUN_0101ff5a` returns 0, execution jumps to `LAB_0101fef5`, which **silently returns 0** without playing `SoundID::CantGo` or triggering a CantGo animation.

  - While a unit is in an active action state (`PlacingFire`, `PlantingBomb`, `BuildingBridge`, `DemolishingBridge`, `ExtinguishingFire`, `DefusingBomb`, or `is_transforming()`), all incoming player orders are completely and silently ignored, allowing the action to finish uninterrupted.

- **Zero Post-Animation Cooldown**:

  - The animation duration itself is the sole pacing mechanism for abilities (e.g. 35 ticks for fire, 28 ticks for bomb, 16 ticks for bridge, 24/26 ticks for extinguish).

  - The instant the placement animation completes and the unit transitions back to `Idle` (state 0), `FUN_0101ff5a` immediately accepts new orders. There is zero artificial cooldown delay blocking subsequent orders once the unit is idle.

- **Uninterruptible `getpow` Transformation (`Ants.exe.c:23694` / `FUN_01020cdb`)**:

  - Once the 11-tick `getpow` transformation sequence begins, the ant commits to the new unit class.

  - The transformation cannot be aborted by player orders; any order issued while transforming is silently ignored. The ant completes the full transformation and emerges as the new unit. To prevent an ant from taking a power-up, it must be redirected *prior* to `getpow` starting.

- **Bomber Pathing Around Intervening Friendly Bombs (`Ants.exe.c:23167-23245` / `FUN_01020128`)**:

  - When ordering a Bomber to plant a bomb at a target tile, candidate cardinal standing spots (`cand`) strictly reject tiles that already contain a bomb (`!grid.has_bomb_at(cand)`).

  - Pathfinding treats friendly/allied bombs as impassable obstacles, enabling the Bomber to navigate *around* intervening friendly mines to reach an open cardinal tile and plant the bomb, instead of stalling in place. Enemy bombs are not treated as pathfinding obstacles.

- **Fire Extinguish Sequence & Sputter Anchor (`Ants.exe.c:19435` -> `FUN_0101e97b`, `10693-10694` / `FUN_010100ab`)**:

  - In `afxf`, the Fire Ant sprays its fire extinguisher canister at tick 8, triggering `SoundID::FireExtinguish` (sound 69, `fireextinguish.wav`).

  - The firewall remains active while being sprayed until animation completion (24 ticks for South `afxf301`, 26 ticks for North/East/West `afxf701`/`afxf901`).

  - At animation completion (`FUN_0101ad02:19435` -> `FUN_0101e97b`), the firewall tile is removed (`0x7ffe`), and `VisualEffect{"sputter", tx * 32, ty * 32, 0, 17}` is spawned.

  - Tile effect positions in `FUN_010100ab` are anchored at `(tx * 32, ty * 32)` (top-left). Table 4 Anim 135 `sputter` frame offsets (`dx: 3, dy: 6`, etc.) are pre-centered within the 32x32 tile. Total duration is 830ms across 10 subitems = 17 simulation ticks @ 20Hz.

- **Bomb Dud & Knockback Zero-Stun Pipeline (`Ants.exe.c:20944-20947`, `22568-22580`)**:

  - **In-Flight & Dud Order Invariant**: During flight (`UnitState::Knockback`) and dud burn (`UnitState::Burn`), `is_stunned()` evaluates to `true`, preventing order inputs and path updates as enforced by `Ants.exe` `FUN_0101b8cb`.

  - **Zero Post-Flight / Post-Dud Stun**: Upon completion of dud burn (`UnitState::Burn`, 11 ticks of `a*bu` scorch), the ant transitions directly to `UnitState::Idle` (`FUN_0101ace3(this, 0)`). No artificial 50-tick stun is applied.

  - Upon landing from bomb knockback (`DamageSource::BombBlast`), the ant lands directly into `UnitState::Idle` with `stun_ticks_remaining = 0`, playing `SOUND_FLY_THUMP_B` (sound 71) without `SOUND_STUN` (sound 66) or 50-tick stun.

- **Chain Bomb Landing Decoupling & Secondary Knockback Pipeline (`Ants.exe.c:20941-20949`, `22568-22580`)**:

  - In `PhysicsEngine::tick()`, flight stepping is strictly decoupled from landing resolution. Completed flights are popped from `active_flights_` *prior* to calling `resolve_landing()`.

  - When an ant lands on a second bomb, `on_bomb_land` triggers `trigger_bomb_detonation()`. If the second bomb is a full detonation, `apply_knockback()` safely registers the secondary ballistic flight in `active_flights_` without double-erasure or iterator invalidation.

  - The ant seamlessly continues into its second flight trajectory, lands on its final destination tile, and cleanly transitions to `Idle` (or recovers from dud scorch), preventing any mid-animation freezing or lost units.

- **Bomber Bomb Defusal 22-Tick Subitem Pacing & Smoke Compositing (Table 4 `abdb301` / `abdb701` / `abdb901`)**:

  - In Table 4, bomb defusal has 12 subitems spanning exactly 1,100ms (22 simulation ticks):
    - Subitems 0..2 (Ticks 0..4): Ant approaches and reaches forward over the bomb.
    - Subitem 3 (Ticks 5..8, 200ms duration): Ant grabs the bomb detonator, playing Sound 73 (`bombdrop.wav` / `BombDefuseGrab`) at Tick 5.
    - Subitems 4..5 (Ticks 9..12): Ant rears up over the mine.
    - Subitem 6 (Ticks 13..14, 100ms duration): Ant drops its body onto the mine, triggering Sound 74 (`bombmuffle.wav` / `BombBodySquash`), clearing the bomb from the grid, and compositing with `9difuse1.bmp` smoke puff.
    - Subitems 7..8 (Ticks 15..18): Ant stays squashed on the ground as the diffuse smoke puff expands (`9difuse2.bmp`, `9difuse3.bmp`).
    - Subitems 9..11 (Ticks 19..22): Ant rises back upright, returning to `Idle` at Tick 22 with zero cooldown.

- **Firewall Landing & Flight Recovery Invariant (`Ants.exe.c:20950-20960`)**:

  - When an ant lands on a firewall from knockback (`resolve_fire_contact`), `unit.altitude_z` is reset to 0 and its flight state is cleanly resolved.

  - Fire Ants (`AntType::Fire`) take 0 fire damage and immediately transition out of `UnitState::Knockback` into `UnitState::Idle` (or `GuardIdle`) with `stun_ticks_remaining = 0`, completely preventing flight freezing.

  - Non-fire ants take contact fire damage, ricochet away from fire according to reflection physics, and transition to `UnitState::Idle` upon completing bomb knockback or stunned state upon punch knockback.

- **Airborne Ballistic Flight Clearance Over Ground Units (`Ants.exe.c:20947`, `21612` / `FUN_0101df5d`)**:

  - In `Ants.exe`, bomb blast knockback creates a 4-tile displacement arc (`param_3 = 4`, spanning 5 tiles total including origin).

  - Units in ballistic flight (`UnitState::Knockback`, `altitude_z > 0`) fly above the 2D ground collision grid. Ground-plane mutual elastic separation and same-tile occupancy resolution (Section 5.5) strictly exempt airborne units (`a1->state == Knockback || a1->altitude_z > 0`).

  - Airborne units fly smoothly *over* intermediate ground units (friendly or enemy) without colliding, bouncing, scuffling, or interrupting flight. Ground occupancy separation only resolves on the landing tile after flight completes.

- **Animated Fire Contact Ricochet & Bounce Pacing (`Ants.exe.c:20193` / `FUN_0101c221`, Table 4 `aggb301` / `agbu301`)**:

  - When a non-fire ant hits fire (via knockback landing, placement, or traversal), it takes 1 contact fire damage and ricochets away along reflection vectors.

  - Instead of instantaneous coordinate teleportation, the ant enters `UnitState::Bounce` (`action == "gb"` tumble animation) with a 6-tick push slide (`push_ticks_total = 6`) and 10-tick total recovery.

  - Impact triggers `SoundID::FlingThumpA` (sound 64, `flythumpa.wav`), spawns a `"bump"` impact visual effect, and plays `SoundID::FlingThumpB` (sound 65, `flythumpb.wav`) upon completing the bounce landing at tick 6.

  - State resolution upon completing bounce (tick 10):
    - Bomb knockback ricochet: Transitions to `UnitState::Idle` (or `GuardIdle`) and accepts orders immediately.
    - Combat punch knockback ricochet: Transitions to `UnitState::Stunned` (50 ticks) with `SoundID::StunRecover` (sound 70, `stun.wav`).
    - Standard contact ricochet: Transitions to `UnitState::Idle`.

#### 9. Anthill Mound Bounds, Team-Locked Approach Corridor, Enemy-Thief Bottlecap, Universal Random Bounces & Bomb Knockback Momentum Preservation (`Ants.exe.c:9670–9690`, `21193–21250`, `21612–21685`, `22787–22895`)

- **Anthill 4x4 Mound Tile Layout & Access Rules**:

  - **Enemy-Thief-Only Bottlecap `(bx + 3, by + 2)`**: Exclusively occupiable by an enemy Thief ant (`AntType::Thief && ant.team != base.team`). Solid impassable obstacle for all friendly ants and enemy non-thief ants. Stepping onto `(bx + 3, by + 2)` triggers the infiltration sequence dive (`FUN_0101fc24` / `FUN_0101ace3`).

  - **Top Approach Corridor `(bx + 0..2, by - 1)`**: Registered at team struct offsets `0x36, 0x3a, 0x3e` (`0x100eda5–0x100ee25`). Strictly blocked from placing bombs or firewalls (`FUN_0101d822`). Team-locked (`FUN_010200ab`): only ants of the base owner's team can traverse and occupy these 3 corridor tiles.

  - **Base Entrance Ramp `(bx + 1, by + 0)` and Hole `(bx + 1, by + 1)`**: Blocked from bombs and firewalls. Passable strictly to friendly ants entering or leaving the base hole (`UnitState::EnteringBase`, `UnitState::ExitingBase`, depositing food, healing, emergence). Normal movement treats them as solid obstacles.

  - **Remaining 13 Mound Tiles**: Impassable solid obstacles for all units.

  - **Occupiable Landing Tiles**: An ant bouncing off fire, from ant-ant collision scuffle, or cascading domino collision can land on:
    - **Another fire tile**: Takes additional 1 fire contact damage and ricochets again.
    - **Water**: Swimmer ant swims safely; non-swimmer begins drowning with splash sound.
    - **Bomb**: Lands on bomb tile and triggers bomb detonation.
    - **Another ant**: Triggers a cascade domino bounce on the standing ant.
    - **Open ground**: Normal ground landing.

- **Bomb Knockback Momentum Preservation**:

  - When an ant bounces or slides into a bomb (e.g. fire ricochet pushes Northeast into a bomb), the bomb detonation preserves the incoming momentum vector (`incoming_dx, incoming_dy`).

  - The bomb blast launches the ant airborne along the continuation vector in the same direction it was traveling/bounced from (e.g. Southwest into bomb throws Northeast).

- **Fire Burn Timing (`*bu301`, 22 Ticks @ 20Hz)**:

  - Authentic Table 4 `a*bu301` timing (1,150ms @ 20Hz, 22 ticks) with smooth 6-tick push slide to destination tile, Sound 64 (`flythumpa.wav`) on tick 0, Sound 65 (`flythumpb.wav`) on tick 11, and transition to `Idle` at tick 22.

#### 10. Authentic 1998 Bomb Blast Flyback, Sidebar Action Buttons, Glow Glitch Fix, Thief Bottlecap Invariants, Dud Stability, Mutual Collision Bouncing, and Base Staging Spots (`Ants.exe.c:9678–9684`, `20280–20309`, `20941–20949`, `24460`, `Table 4`)

- **100% Authentic 1998 Bomb Blast Knockback Start Position & Trajectory (`Ants.exe.c:24460` & `Original-Ants/ants.chd`)**:

  - In 1998 `Ants.exe.c:24460`, upon bomb detonation the ant's logical tile position is anchored immediately at the destination tile: `this_00[0x2c] = *(int *)puVar4;` (4 tiles away along recoil heading `(facing + 4) % 8`).

  - Table 4's 30 `*gb*` animations (`aggb*`, `abgb*`, `afgb*`, etc.) natively embed the entire flight and tumble trajectory relative to this destination tile:
    - Frame 0 (Blast impact): `dy = -17px`
    - Frame 1 (Airborne launch): `dy = -128px` $\rightarrow$ precisely 4 tiles (128 pixels) away, anchoring the visual start frame directly on the bomb tile!
    - Frames 2..4 (Ballistic arc): smoothly ascending and descending through the air.
    - Frame 5 (Ground landing): `dy = -19px` $\rightarrow$ landed on destination tile.
    - Frames 6..11 (Recovery roll): tumbling on destination tile before returning to idle.

  - The ant's logical coordinates are set immediately to `(dest_tx, dest_ty)` with `altitude_z = 0`, keeping the ant's original facing so Table 4 native frame offsets smoothly fly the ant from the bomb to the destination tile without redundant physics displacement.

- **Table 4 Sidebar Action Pedestal Buttons & Drop Shadows (`docs/chd_table4_animations.json`)**:

  - Table 4 defines action pedestal buttons using `butup.bmp` (53×71 @ 477, 157 / 538, 157) and `butdown.bmp` (55×75 @ 476, 156 / 537, 156), featuring authentic molded 3D beveled borders and native drop shadows.

  - Class-specific icons and labels:
    - Move: `butmovu` (ID 1178) / `butmovd` (ID 1184) with `butmov*.bmp` and `labmov.bmp`.
    - Bomb: `butbomu` (ID 1182) / `butbomd` (ID 1188) with `butbom*.bmp` and `labbom.bmp`.
    - Attack: `butattu` (ID 1181) / `butattd` (ID 1187) with `butatt*.bmp` and `labatt.bmp`.
    - Thief: `butthfu` (ID 1180) / `butthfd` (ID 1186) with `butthf*.bmp` and `labthf.bmp`.
    - Fire: `butfiru` (ID 1183) / `butfird` (ID 1189) with `butfireu.bmp` and `labfire.bmp`.
    - Swim: `butswmup` (ID 1223) / `butswmd` (ID 1224) with `swimup.bmp` / `swimd.bmp` and `labswim.bmp`.

  - Hitbox dimensions are standardized to 55×75 at `(476, 156)` and `(537, 156)`.

- **Thief Infiltration Alignment & Bottlecap Mechanics (`Ants.exe.c:21193–21250`)**:

  - Unsuccessful steal leaves the thief resting on the bottlecap at `(bx + 3, by + 2)` (`by * 32 + 90`).

  - While on the bottlecap with `underground = true`:
    - The thief is 100% damage immune (`take_damage` returns `false`) and untargetable by combat AI.
    - Ordering the thief to move to the enemy base/bottlecap re-triggers infiltration in-place.
    - Walking away from the bottlecap resets `underground = false` (vulnerable again). Returning to `(bx + 3, by + 2)` triggers a fresh steal attempt.

- **Fire Bounce Animation Defect Fix**:

  - Non-fire ants contacting fire enter `UnitState::Bounce` (tumbling ground slide `*gb*`) rather than `UnitState::Burn` (stationary smoke puff).

- **Bomb Dud Stationary Position Lock (`Ants.exe.c:20941–20949`, `24461–24468`)**:

  - On a 20% dud roll, the ant stays stationary on the bomb tile (`dest == src`), push ticks are zeroed, and `UnitState::Burn` is excluded from push slide and elastic collisions.

- **Mutual Ant-Ant Collision Bouncing & Base Queue Protection (`Ants.exe.c:20280–20309`)**:

  - When two moving ants collide head-on in the open field, both bounce away into adjacent available tiles (`UnitState::Bounce`) with `SoundID::Bump`. Swarms moving in the same direction do not bounce.

  - Ants actively in the base queue or occupying anthill reserved spots (`is_anthill_reserved_spot`) are exempt from head-on bouncing to ensure uninterrupted food delivery and base exit flow.

- **Anthill Base Queuing Staging Spots (`Ants.exe.c:9678–9684`)**:

  - Queue slots line up strictly to the left of the base at `qy = by + row`:
    - Slot 0: `(bx - 1, by)`
    - Slot 1: `(bx - 1, by + 1)`
    - Slot 2: `(bx - 1, by + 2)`
    - Slot 3: `(bx - 1, by + 3)`

- **Bomb Knockback 5-Direction Deflection & 5-Tile Flight Geometry (`FUN_0101d8ed` / `FUN_0101d9f7`)**:

  - Distance geometry: `FUN_0101d9f7(ant_tile, d, 4)` adds 4 tiles to the ant's tile along direction `d`. Because the ant is 1 tile away from the detonating bomb, this results in a landing tile exactly 5 tiles from the bomb (counting the bomb itself).

  - Authentic deflection: Table 4 `aggb301` has intrinsic displacement `[12 subitems, dx = -128px]`. Knockback trajectory always tests distance 5 from the bomb (4 tiles from the ant, 128px flight). Trajectory never shortens or wraps 180° through the bomb; it checks 5 candidate deflection angles `[primary, +45°, -45°, +90°, -90°]`. If all 5 directions are blocked by obstacles/food/map edges, the ant remains on its current tile in a dazed/stunned state without warping or shortening.

  - Landing recovery: Landing from knockback enters `UnitState::Stunned` playing `*sd301` (orbiting stars + dazed wobble), accompanied by Sound 65 (`flythumpb.wav`) and Sound 70 (`stun.wav`).

  - Stun cancellation: Per `FUN_01021494`, stunned ants are orderable; issuing a command cancels the daze immediately and begins walking.

- **Ant Collision Bumping vs Knockback Parity (`Ants.exe 0x102184e`)**:

  - Ant-to-ant collision triggers Action 11 (`0xb`), which plays Animation 220 (`bump`).

  - Animation 220 contains sprite 155 (`empty.bmp`, 0×0) and Sound 47 (`bump.wav`).

  - Ants do not play `*gb*` (tumble flight) or `*gh*` (recoil flinch); they remain upright in their normal walk/idle sprite, step back to their prior tile, and call `FUN_010214d9` to recalculate their path.

  - Friendly collision cascades play Sound 47 (`bump.wav`) only and strictly do not play `FlingThumpB` (Sound 65).

- **Bridge Demolition & Expiration Drowning Mechanics**:

  - Non-swimming ants survive on bridges during demolition shovel hits across all intermediate decay stages (4 -> 3 -> 2 -> 1).

  - Non-swimmers only drown when the bridge tile completely collapses to `TILE_EMPTY` (`!has_any_bridge()`).

  - Upon full 180s timer expiration, the bridge collapses directly to water per `FUN_0100f8bf`, triggering catastrophic drowning for non-swimming occupants.

- **Anthill Hole Base Entry Alignment**:

  - Base entry animation `*en301` is anchored at `(hill_tx * 32 + 44, hill_ty * 32 + hill_offset_dy[t % 4] + hill_hole_local_y[t % 4])` with `hill_hole_local_y[4] = { 48, 39, 44, 50 }`, perfectly aligning the ant sprite entering the hole.

- **Options Menu Map Scroll Speed Calibration**:

  - Slider track thumb center travel spans 212..397 (185px). Rate is calculated as `std::clamp((static_cast<float>(x) - 212.0f) / 185.0f, 0.0f, 1.0f)`.

  - Camera scroll speed is synchronized on application init and map start via `240.0f + hud_.get_scroll_rate() * 480.0f`.

  - Panning remains strictly mouse-driven with zero keyboard WASD/arrow panning.

- **Minimap Powerup & Base Marker Fidelity**:

  - Powerup morsels and flower droppers are rendered as amber dots (palette index 171 `{231, 147, 11, 255}`).

  - Terrain tiles belonging to base mounds are rendered in team color, preventing dark green boundary artifacts.

- **Single-Bomber Marquee Selection Mode**:

  - Dragging a selection marquee around 1 bomber without Shift maintains single-unit ability mode (Pedestal 2 active, right-click plants bombs). Multi-unit or Shift-marquee enters group move-only mode.

- **Stun Animation Pacing Across Display Refresh Rates (Table 4 Subitem Timing)**:

  - Table 4 defines discrete millisecond durations per subitem for stun stars/wobble (`*sd301`):
    - Worker `agsd301`: 125ms per subitem (`val3 = 125`), 8 subitems = 1000ms loop.
    - Bomber `absd301`: 105ms per subitem (`val3 = 105`), 10 subitems = 1050ms loop.
    - Fire, Thief, Combat, Swimmer: 100ms per subitem (`val3 = 100`), 10 subitems = 1000ms loop.

  - High refresh-rate monitors (120Hz, 144Hz, 240Hz, 360Hz) map elapsed display time via `elapsed_ms = ant.anim_frame * 50u + sub_tick_ms` with `get_anim_subitem_by_time` instead of advancing subitems every simulation tick, ensuring identical 8–10 FPS visual pacing across all platforms.

  - Carried food prefix is preserved (`hgsd301`, `hbsd301`) so harvesting workers holding food render with food held while stunned.

- **Bomb Blast Deflection Origin & 8-Direction Outward Sequence (`FUN_0101df5d`, `FUN_01021a6f`, `FUN_0101ad02`)**:

  - Origin tile coordinates: Blast knockback originates strictly from the bomb tile center (`from_px / 32, from_py / 32`) rather than an empty adjacent tile.

  - Outward deflection order: Evaluates all 8 angles `[initial_dir, +1, +7, +2, +6, +3, +5, +4] % 8`. If an obstruction (e.g. north rock wall) blocks the direct path, the ant deflects sideways outward from the bomb.

  - Heading alignment: `victim.facing = static_cast<Direction>(chosen_dir)` synchronizes the tumble sprite (`aggb`) departing cleanly from the bomb center.

- **Active Bomb Placement Walk-On Detonation & Pre-Placement Occupancy (`Ants.exe 0x1021c30`)**:

  - Bombers cannot initiate bomb placement on a tile already occupied by a living ant.

  - If another ant moves onto the target tile while the bomber is in the 28-tick placement animation, immediate proximity detonation is triggered at tick 28 upon bomb instantiation, eliminating repathing jitter and coordinate conflicts.

- **Combat Ant AI Guard Locomotion, Heavy Punch, Knockback & Water Drowning Parity**:

  - Smooth locomotion: Combat Ant AI uses standard pathfinding locomotion (`issue_move_order` at `SPEED_STANDARD_FX`), eliminating tile teleportation.

  - Heavy Punch strike: Striking an adjacent intruder sets `UnitState::Attacking` with an 11-tick punch animation (`acat301`, Sound 78 `HeavyPunch`).

  - Ballistic flight knockback: Victim is launched into 4-tile flight (`UnitState::Knockback`, tumble animation `aggb`, Sound 64 `FlyThumpA`, 12-tick stun `SoundID::StunRecover`).

  - Instant water drowning: Knockback into deep water without a completed bridge calls `resolve_water_entry`, plunging non-swimmers into `UnitState::Drowning` (Sound 71 `WaterSplash`, Sound 72 `AntDrown`) for an authentic instant kill.

- **Anthill Base Targeting, Perimeter Pathfinding, Knockback Exclusion & Queuing Invariants (`FUN_010202e7`, `FUN_0101fc50`, `FUN_0101dded`)**:

  - Closest available perimeter tile routing: In original 1998 mechanics (`FUN_010202e7`), clicking an impassable structure such as an enemy base mound with non-thief friendly units does not trigger `CantGo`. Instead, the destination is resolved to the closest available passable tile along the base perimeter via Chebyshev distance minimization.

  - Knockback base & queue exclusion: `PhysicsEngine::apply_knockback` explicitly excludes landing spots inside any anthill base footprint (`bx..bx+3, by..by+3`) or queue columns (`bx - 1, by..by+3`). If the nominal 4-tile trajectory lands within a base or queue slot, candidate search steps inward down to 1 tile, preventing knocked or stunned units from inadvertently penetrating bases or entering base queues.

  - Autonomous base queuing restrictions: Units only automatically join the anthill queue upon reaching exactly 1 HP (the authentic critical retreat threshold from `FUN_0101dded` / `FUN_0102151a`) or when a thief completes a successful food heist (`is_thief_steal && is_holding()`). Units with 2+ HP or units knocked near the base never enter the base or queue automatically.

  - Direct queue slot clicking: Directly left-clicking base queue coordinates (`bx - 1, by..by+3`) is treated as a pure ground move order rather than an anthill entry request, allowing free maneuvering around the anthill without involuntary base queuing.

  - Base perimeter walkability: Approach corridors surrounding anthills are passable by all units, restoring authentic 360-degree perimeter walkability while maintaining strict bomb and firewall placement blocks directly on the top corridor.

- **Power-Up Obstacle Navigation, Skill-Based Pickup & CantGo Standing Mechanics**: the earlier text of this entry (an arrival dwell of
  6 ticks, `cantgo_standing_on_powerup`, a 15-tick transformation, `(15 - transform_timer) * 11 / 15`, heal to full HP, 12 HP for combat ants)
  was wrong and is superseded by section 5.38 (verified in `Ants.exe`, implemented in v0.0.35).
