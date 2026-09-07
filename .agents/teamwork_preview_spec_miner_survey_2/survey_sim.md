# Microsoft Ants - Simulation Rules & Deterministic Engine Specification (`survey_sim.md`)

**Document Version:** 1.0  
**Author:** Specification Miner (`miner_survey_2`)  
**Scope:** Complete Simulation Rules, Math Models, State Machines, Timers, Combat Physics, and Game Lifecycles  
**Authoritative Sources:**  
- Disassembly & PE Analysis of `Original-Ants/Ants.exe` (PE32 Intel 80386)
- Binary Archive Data of `Original-Ants/ants.chd` (Tables 1, 2, 4)
- Level Geometry & Entities of `Original-Ants/Maps/*.LVL`
- Reverse Engineering Specification: `GAME_REVERSE_ENGINEERING.md`
- Sentinel Clarifications: `ORIGINAL_REQUEST.md` (2026-09-06T22:34:55Z)

---

## Table of Contents
1. [Simulation Grid, Coordinates & Tick Engine](#1-simulation-grid-coordinates--tick-engine)
2. [Unit Types, Stats & Damage Matrix](#2-unit-types-stats--damage-matrix)
3. [Combat Ant Autonomous Guard AI](#3-combat-ant-autonomous-guard-ai)
4. [Placement Rules & Cardinal Geometry](#4-placement-rules--cardinal-geometry)
5. [Bomb & Mine Mechanics](#5-bomb--mine-mechanics)
6. [Fire Physics, Ricochet Dynamics & Extinguishing](#6-fire-physics-ricochet-dynamics--extinguishing)
7. [Timers, Expirations & Bridge Collapse Drowning](#7-timers-expirations--bridge-collapse-drowning)
8. [Colony Anthill, Queuing & Healing Mechanics](#8-colony-anthill-queuing--healing-mechanics)
9. [Thief Ant Infiltration & Theft Architecture](#9-thief-ant-infiltration--theft-architecture)
10. [Dynamic In-Game Alliances & Scoring Architecture](#10-dynamic-in-game-alliances--scoring-architecture)
11. [Game End Sequence & Scorecard Presentation](#11-game-end-sequence--scorecard-presentation)
12. [Features Discovered Table](#features-discovered)
13. [Edge Cases Table](#edge-cases)

---

## 1. Simulation Grid, Coordinates & Tick Engine

### 1.1 Grid Topography & Spatial Representation
- **Grid Dimensions:** Defined per `.LVL` map header:
  - 60 × 60 cells: `GAUNTLET.LVL`, `ISLANDS.LVL`, `TREASURE.LVL`
  - 40 × 40 cells: `MEDIUM.LVL`
  - 31 × 31 cells: `SMALL.LVL`, `TINY.LVL`
- **Cell Dimensions:** Each tile cell is strictly **32 × 32 pixels**.
- **Coordinate Systems:**
  - **Tile Coordinates:** Integer indices `(tx, ty)` where `0 <= tx < width` and `0 <= ty < height`.
  - **World / Sub-Tile Coordinates:** Integer pixel coordinates `(px, py)`. Conversion from pixel position to tile index uses discrete integer arithmetic:
    ```c
    tx = px / 32;  // Disasm: movsx eax, word ptr [esi + 0x38]; push 0x20; cdq; idiv ecx
    ty = py / 32;  // Disasm: movsx eax, word ptr [esi + 0x3a]; push 0x20; cdq; idiv edi
    ```
- **Arithmetic Discipline (Fixed-Point vs Integer Math):**
  - All simulation logic, distance metrics, collision checks, combat health updates, and movement vectors use **pure integer math** (signed/unsigned 16-bit and 32-bit integers).
  - Floating-point arithmetic (`float` / `double` / x87 FPU) is **strictly forbidden** in simulation state progression to guarantee cross-platform bitwise determinism.

### 1.2 Tick Frequency & Simulation Loop
- **Tick Frequency:**
  - Logical simulation ticks run at a fixed discrete rate of **20 Hz** (50 ms per tick step, `dt = 50 ms`).
  - Action timers and millisecond threshold checks evaluate against `timeGetTime()` / integer elapsed milliseconds (`0xc8` = 200 ms, `0x3e8` = 1,000 ms, `0x1388` = 5,000 ms, `0x2bf20` = 180,000 ms).
  - Rendering operates decoupled with integer pixel scaling and sprite frame interpolation, but gameplay state advances strictly in discrete lockstep ticks.
- **State Determinism:**
  - Given an identical initial world state, identical player command input stream, and identical PRNG seed, the simulation produces 100% identical game states across macOS, Linux, and Windows.

### 1.3 PRNG Seed & Algorithm
- **Algorithm (Disasm `0x10345b0` & `0x10345c0`):**
  The original engine uses the standard Microsoft Visual C++ Linear Congruential Generator (LCG):
  ```c
  static uint32_t holdrand = 1;

  void sim_srand(uint32_t seed) {
      holdrand = seed;
  }

  uint16_t sim_rand(void) {
      holdrand = holdrand * 214013 + 2531011;
      return (uint16_t)((holdrand >> 16) & 0x7FFF); // Returns [0 .. 32767]
  }
  ```
  *Disassembly Confirmation:*
  ```assembly
  0x10345c5: mov ecx, dword ptr [eax + 0x14] ; holdrand
  0x10345c8: lea edx, [ecx + ecx*2]          ; 3 * ecx
  0x10345cb: lea edx, [ecx + edx*4]          ; 13 * ecx
  0x10345ce: shl edx, 4                      ; 208 * ecx
  0x10345d1: add edx, ecx                    ; 209 * ecx
  0x10345d3: shl edx, 8                      ; 53504 * ecx
  0x10345d6: sub edx, ecx                    ; 53503 * ecx
  0x10345d8: lea ecx, [ecx + edx*4 + 0x269ec3] ; holdrand * 214013 + 2531011
  0x10345df: mov dword ptr [eax + 0x14], ecx
  0x10345e2: mov eax, ecx
  0x10345e4: shr eax, 0x10
  0x10345e7: and eax, 0x7fff                 ; (holdrand >> 16) & 0x7fff
  ```
- **Seed Initialization Logic (`0x100adde..0x100adef`):**
  - If the command line argument `latseed:<value>` is specified, `seed = atoi(value)`.
  - If `latseed` is `0` or omitted, the engine seeds from `timeGetTime()`.
  - For deterministic multiplayer replays and automated headless unit tests, `seed` is explicitly provided.

---

## 2. Unit Types, Stats & Damage Matrix

### 2.1 Unit Classes & Power-Up Mapping Table
Internal type dispatch (`0x1021087`, `0x10210c1`) maps units and power-up pickup tiles:

| Type ID | Class Name | Sprite Prefix | Power-Up Tile | Tile Asset Name | Starting HP | Max HP | Melee Strike Damage | Knockback Distance | Autonomous AI Guard? |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **0** | Worker Ant (General) | `ag` | None | N/A | 10 HP | 10 HP | **1 HP** | 0 tiles | No (Manual only) |
| **1** | Bomber Ant | `ab` | Tile 64 | `pu_bomb` | 10 HP | 10 HP | **1 HP** | 0 tiles | No (Manual only) |
| **2** | Fire Ant (Mason) | `af` | Tile 66 | `pu_mason` | 10 HP | 10 HP | **1 HP** | 0 tiles | No (Manual only) |
| **3** | Thief Ant | `at` | Tile 63 | `pu_thief` | 10 HP | 10 HP | **1 HP** | 0 tiles | No (Manual only) |
| **4** | Combat Ant | `ac` | Tile 62 | `pu_comb` | 10 HP | 10 HP | **2 HP** | **4–5 tiles** | **Yes** (3-tile aggro guard) |
| **5** | Swimmer Ant | `as` | Tile 65 | `pu_swim` | 10 HP | 10 HP | **1 HP** | 0 tiles | No (Manual only) |

### 2.2 Health & Movement Metrics
- **Health Tracking:**
  - Health is stored as a 16-bit integer at `CAntUnit + 0x74`.
  - Initial Spawn HP: `10`.
  - Maximum Cap: `10`. Damage reduces HP; zero HP triggers unit death.
- **Movement Speeds:**
  - **Standard Walk Speed:** `Worker`, `Bomber`, `Fire`, and `Combat` ants move at base ground speed (~2.5 tiles per second).
  - **Scout Speed:** `Thief Ant` possesses an innate speed boost (~3.5 tiles per second) for hit-and-run raiding.
  - **Aquatic Speed:** `Swimmer Ant` moves at base speed on land and slightly reduced paddling speed in water.

### 2.3 Damage Matrix & Combat Knockback Physics
- **Universal 1 HP Melee Strike Standard:**
  - Melee attack action (`*at*`, Action 1) triggers Sound 57 (`attack.wav`).
  - Worker, Thief, Fire, Bomber, and Swimmer ants all deal strictly **1 HP damage** per melee strike.
- **Combat Ant 2 HP Heavy Strike Exception:**
  - Heavy attack action (`acat301`, Subitem 2) triggers Sound 78 (`attack2.wav`).
  - Deals strictly **2 HP damage** per hit.
  - Generates an immediate ballistic knockback vector along the line connecting attacker and victim.
- **Ballistic Knockback Mechanics:**
  - **Displacement Distance:** Victim is propelled backward **4 to 5 tiles** at high parabolic velocity.
  - **Audio Sequence:** Sound 64 (`flythumpa.wav`) or Sound 65 (`flythumpb.wav`) plays on launch and terrain impact; Sound 70 (`stun.wav`) plays upon landing.
  - **Animation Pipeline:**
    1. Airborne Fling: `*gf*` (Action 14, e.g. `aggf`, `abgf`, `afgf`, `acgf`, `asgf`, `atgf`) - spinning tumble.
    2. Ground Bounce / Skid: `*gb*` (Action 19, 12 subitems) - impacts dirt, skids forward, rolls to rest.
    3. Stunned Recovery: Action 12 (`stun.wav`) - unit remains immobilized and unreceptive to commands for **12 simulation ticks** (600 ms) before returning to ready stance.
- **Obstacle Collision During Knockback:**
  - If an airborne ant collides with a solid obstacle (rock, tree, stone wall, cliff, or map boundary), ballistic flight immediately terminates at the impact cell, playing impact audio and transitioning directly into the bounce/stun recovery sequence.
- **Water Landing During Knockback:**
  - If a non-swimmer ant lands in deep water (Layer 1 terrain category 2), the ant **drowns instantly** (`death_status = 0xF`, HP = 0, playing drowning audio).
  - If a Swimmer Ant lands in deep water, it plunges in (`splash.wav`, Sound 71) and enters swim mode (`assw*`) unharmed.

---

## 3. Combat Ant Autonomous Guard AI

Combat Ants (`ac`) are the **sole unit type** in the game with autonomous behavioral logic. All other 5 ant types are strictly passive, executing only direct player orders.

### 3.1 Guard Anchor State Machine
1. **Idle Post Establishment (`guard_tile`):**
   - Whenever a Combat Ant completes a move command and enters Idle state (State 7), its current grid coordinates `(x_g, y_g)` are committed as its `guard_anchor`.
2. **Aggro Perimeter Scan:**
   - Evaluated on every simulation tick while in Guard Idle.
   - **Scan Metric:** Chebyshev distance metric:
     ```c
     int dist = max(abs(enemy_x - guard_x), abs(enemy_y - guard_y));
     if (dist <= 3) {
         // Target within 7x7 square aggro perimeter
     }
     ```
   - **Target Filtering:**
     - Must be a live enemy ant (`team_id != local_team && team_id != ally_id`).
     - Ignores friendly and allied ants.
     - Ignores ants that are underground (e.g. Thief Ant rummaging inside anthill) or dead.
3. **Autonomous Intercept:**
   - The Combat Ant breaks Idle state and pathfinds directly toward the intruder.
   - Upon achieving melee contact (`dist <= 1`), it delivers its heavy punch (`acat301`, 2 HP damage + 4–5 tile ballistic knockback, Sound 78 `attack2.wav`).
4. **Automatic Return to Post:**
   - Immediately after striking the target (or if the target dies, escapes beyond the 3-tile boundary, or disappears), the Combat Ant disengages.
   - It autonomously pathfinds back to `guard_anchor` `(x_g, y_g)`.
   - Upon reaching `guard_anchor`, it resumes its Idle Guard stance and re-arms its 3-tile scan.

---

## 4. Placement Rules & Cardinal Geometry

### 4.1 Strict Cardinal Adjacency Rule (No Diagonal Placement)
Fire Ants placing firewalls and Bomber Ants planting landmines are subject to strict directional placement constraints:
- **Allowed Placement Offsets:** Only the 4 orthogonal cardinal directions are valid:
  - **North:** `(x, y - 1)` (`dx = 0, dy = -1`)
  - **East:** `(x + 1, y)` (`dx = 1, dy = 0`)
  - **South:** `(x, y + 1)` (`dx = 0, dy = 1`)
  - **West:** `(x - 1, y)` (`dx = -1, dy = 0`)
- **Prohibited Diagonal Placement:**
  ```c
  if (dx != 0 && dy != 0) {
      // REJECT: Diagonal placement is strictly prohibited
      return PLACEMENT_ERR_DIAGONAL;
  }
  ```
- **Distance Requirement:** Manhattan distance must equal 1: `abs(dx) + abs(dy) == 1`.
- **Order Handling for Distant/Diagonal Orders:**
  - If a player clicks a diagonal or non-adjacent tile, the ant does not immediately place; it first paths to an adjacent orthogonal cell before initiating placement.

### 4.2 Tile Placement Validity Flags (Disasm `0x10071dd` & `0x1007202`)
The map manager stores tile attributes initialized at startup:
- **Bomb Placement Check (`0x10071dd`):**
  - Evaluates `(tile_flags & 0x02) != 0` (`CAN_PLACE_BOMB`).
  - Set on 87 ground tile types (grass, dirt, walkable clay).
- **Fire Placement Check (`0x1007202`):**
  - Evaluates `(tile_flags & 0x04) != 0` (`CAN_PLACE_FIRE`).
  - Set on 5 clear ground tile types.
  - **Subset Principle:** `CAN_PLACE_FIRE` implies `CAN_PLACE_BOMB`. Anywhere fire can be placed, a bomb can also be placed.
  - **Invalid Terrain:** Mud, water, deep water, stone walls, and boulders lack these bits and unconditionally reject placement.

---

## 5. Bomb & Mine Mechanics

### 5.1 Planting & Arming Sequence
- **Planting Ability (`absb301`, 17 subitems / 23 frames):**
  - Initiated by Bomber Ant (`ab`) facing the target cardinal tile.
  - Subitems 0–8: Ant crouches down.
  - Subitem 9: Draws bomb from pack, firing Sound 90 (`bombpick.wav`).
  - Subitems 10–14: Places bomb on Layer 2 (`redbomb`, `bluebomb`, `blackbomb`, `greenbomb` matching team color) and arms fuse.
  - Subitems 15–16: Stands up; live landmine is active.

### 5.2 Mine Detonation & Blast Radius
- **Detonation Trigger:** Stepped on by any enemy ant (or adjacent proximity trigger).
- **Damage & Knockback:**
  - Detonation inflicts **2 HP explosive damage** to the triggering ant and units in the immediate blast.
  - Propels caught units **2 to 3 tiles** in explosive ballistic knockback.
  - Audio: Sound 4 (`bombexp.wav`, 22,050 Hz, 1.14s).
  - Layer 2 tile is cleared to empty (`0x7FFE`).

### 5.3 Defusing / Body Squash Neutralization
- **Bomber Ant Exclusive Ability:** The Bomber Ant is the **only** unit capable of defusing enemy landmines.
- **Animation Sequence (`abdb301`, 12 subitems / 15 frames):**
  - Subitems 0–2: Leans forward over the active mine.
  - Subitem 3: Pins down the casing, firing Sound 73 (`bombdrop.wav`).
  - Subitems 4–5: Rears up over the mine.
  - Subitems 6–8: Slams down with full body weight directly onto the bomb! Triggers Sound 74 (`bombmuffle.wav`). Visual: squashed bomb pops in a muffled puff of smoke and flattened fragments (`9difuse1..3.bmp`).
  - Subitems 9–11: Ant rises upright. Bomb is safely removed from Layer 2 without exploding.

---

## 6. Fire Physics, Ricochet Dynamics & Extinguishing

### 6.1 Ignition & Pathfinding Barrier
- **Ignition Sequence (`afsf301`, 22 subitems / 32 frames):**
  - Fire Ant focuses magnifying glass sunbeam onto cardinally adjacent tile.
  - Subitem 5: Sound 67 (`firestarta.wav`) plays as sunbeam focuses.
  - Subitem 17: Sound 68 (`firestartb.wav`) plays as flames ignite.
  - Layer 2 tile becomes `wallup04` (tile 134) with an exact **180-second** lifetime timer.
- **A* Pathfinding Obstacle:**
  - Fire tiles (`wallup04`) are treated as impassable solid obstacles by standard ants. Ants will never pathfind onto fire voluntarily.
  - **Fire Ant Exception:** Friendly and enemy Fire Ants possess the fire-walking exception and traverse fire tiles freely.

### 6.2 Involuntary Knockback, Damage & Ricochets (`0x0101e9cf`, `0x0101c221`)
- **Entering Fire:** Non-fire ants can only enter a fire tile via external ballistic knockback (Combat punch, bomb blast, or melee hit).
- **Cumulative Damage Rule (+1 Fire Damage):**
  - Contact with fire immediately inflicts **1 point of fire damage** (`damage_source = 7` at `0x01021627`).
  - *Standard Hit (1 HP) + Fire Landing:* 2 total damage.
  - *Combat Punch (2 HP) + Fire Landing:* 3 total damage.
  - *Bomb Blast (2 HP) + Fire Landing:* 3 total damage.
- **Non-Occupancy & Ricochet Deflection:**
  - Non-fire ants cannot occupy a fire tile; they bounce off immediately.
  - **Reflection Trajectory:** `bounce_dir = (incoming_dir + 4 + random_offset) % 8`.
  - **Multi-Fire Chains:** If the bounce sends the ant onto another fire tile, the ant immediately takes **another 1 point of fire damage** and bounces off that fire tile as well. Bounces continue chaining until the ant lands on clear ground or dies.
  - **Ant-to-Ant Deflection:** If a bouncing ant lands on an occupied cell (friendly or enemy), spatial occupancy rejection deflects it in another direction. If deflected back into fire, it takes another fire damage point.
  - **Slapstick Tumble:** Each bounce retriggers tumble animation `*gf*` / `*gb*`, plays `flythumpa.wav` / `flythumpb.wav`, and resets the 12-tick stun timer (`stun.wav`).

### 6.3 Fire Extinguishing Rules
- **Ants NEVER Extinguish Fire by Bouncing:** Landing on or bouncing off fire never extinguishes or degrades the fire.
- **Only Two Ways to Extinguish Fire:**
  1. **180-Second Timeout:** The 180-second timer expires, triggering Sound 5 (`fireburnout.wav`) and clearing the tile (`0x7FFE`).
  2. **Fire Ant Extinguish Action (`afxf301`, 12 subitems):** A Fire Ant (`af`) smothers the flame (friendly or enemy), triggering Sound 69 (`fireextinguish.wav`), sputtering smoke (`sputter`, Anim 135), clearing Layer 2 (`0x7FFE`), and deleting the 180s timer.

---

## 7. Timers, Expirations & Bridge Collapse Drowning

### 7.1 180-Second Lifetimes (`0x101e8d5`, `0x101ebd3`, `0x1024d85`)
Both temporary field structures created by specialized ants have an exact lifetime of **180,000 ms (180 seconds / 3 minutes)**:
- **Firewall:**
  - Placed at `0x0101e864`. Timer registered with threshold `0x2bf20` ms.
  - When timer reaches zero (`0x01024de7`), Layer 2 tile 134 is erased (`0x7FFE`) and Sound 5 (`fireburnout.wav`) plays.
  - If the match has less than 180 seconds remaining, the firewall persists until match end.

### 7.2 Bridge Construction, Universal Traversal & Collapse Drowning
- **Construction:** Swimmer Ant builds across water through 4 progressive stages (`bridge1` -> `bridge2` -> `bridge3` -> `bridge4`) with shovel (Sound 82 `shovelwater.wav`).
- **Universal Traversal (Clarification 2026-09-06T22:34:55Z):**
  - Once fully constructed (`bridge4`), the water tile becomes passable ground for **ANY ant in the game** (friendly, allied, or hostile enemy ants alike).
  - Traversal is not restricted to the builder's team.
- **180-Second Collapse & Catastrophic Drowning (`0x0100f8fc` -> `0x0100f948`):**
  - When exactly 180 seconds elapse from bridge completion, the bridge collapses and is erased from Layer 2 (`0x7FFE`).
  - **Drowning Scan:** The engine checks all ants currently occupying the bridge tile.
  - **Instant Drowning:** Any non-swimmer ant (`ant_type != 5`, friendly or enemy) standing on the bridge at the exact moment of collapse falls into deep water and **drowns instantly** (`death_status = 0xF`, HP = 0, playing drowning audio).
  - **Swimmer Ant Immunity:** Swimmer ants (`ant_type == 5`) survive unaffected, transitioning to swimming mode (`assw*`).

---

## 8. Colony Anthill, Queuing & Healing Mechanics

### 8.1 Concentric Chebyshev Ring Queuing (`0x01019900`)
Ants returning to base to deposit food or heal cannot all occupy the entrance simultaneously:
- **Chebyshev Ring Metric:**
  ```c
  ring = max(abs(x - base_x), abs(y - base_y));
  ```
- **Queue Slots:** Prioritized by radius: Ring 1 (8 cells), Ring 2 (16 cells), Ring 3 (24 cells)...
- **FIFO Progression:** Incoming ants reserve the lowest available ring slot in FIFO order and step inward as the center entrance clears.

### 8.2 17-Frame Base Entry & Emergence Sequence (`hgen301` / Anim 867)
- **Frames 0–3:** Ant approaches hole carrying lunchbox (`3lb0000..0003` composited over `agen302..304`).
- **Frame 4 (Deposit Instant):** Lunchbox disappears into the base. Team score increments (Sound 87 `scoreup.wav`), and carried inventory is cleared (`carried_food = 0`, `carried_points = 0`).
- **Frames 5–7:** Empty-handed ant dives down the entrance shaft (`agen305..308`).
- **Frame 8 (Underground Full Heal):** Ant is submerged in underground chamber (`empty.bmp`, Sprite 155). **100% full heal to 10 HP occurs** (`powerupc.wav`), resetting all damage wounds whether the ant arrived with food or empty-handed.
- **Frames 9–15:** Ant climbs back up tunnel shaft (`agen308` back to `agen302`).
- **Frame 16 (Emergence):** Ant surfaces ready in upright stance (`agst301.bmp`), fully healed, receptive to commands.

### 8.3 Egg Incubation & Hatching Mechanics
- Production occurs at the colony anthill (no queen ant).
- **Point Cost:** Exactly **200 points** deducted from team score per egg hatched.
- Requires available egg inventory.
- Hatching is prohibited if score < 200 or egg inventory is 0.

---

## 9. Thief Ant Infiltration & Theft Architecture

### 9.1 33-Frame Diving & Stealing Animation (`atcr501` / Anim 1095)
- Phase 1 (Frames 0–18): Low sneak crawl (`atcr501..510.bmp`).
- Phase 2 (Frames 19–25): Leaping into enemy base cutout (`9hillh2.bmp` / Sprite 2503). Frame 19 plays Sound 84 (`steala.wav`). Plunges into hole.
- Phase 3 (Frames 26–29): Rummaging inside enemy storehouse. Frame 26 plays Sound 85 (`stealb.wav`). Frame 29 submerged underground (`empty.bmp`).
- Phase 4 (Frames 30–32): Climbing out. Frame 31 plays Sound 86 (`stealc.wav`). Frame 32 surfaces holding stolen lunchbox.

### 9.2 Victim Base Siren & Score Drain (Disasm `0x0101d57c`, `0x0101b757`)
- **Alarm Siren (`underattack.wav` / Sound 58):**
  - The instant an enemy Thief Ant dives into an anthill, a high-priority siren (2,566 Hz alarm) sounds on the victim's client.
  - Victim screen displays News Flash banner: `[mm:ss] A ThiefAnt is at your anthill!` (String ID 53).
- **Loot Calculation (`0x0101d57c`):**
  ```c
  points_stolen = min(50, victim_team.score);
  victim_team.score -= points_stolen;
  thief_ant.carried_points = points_stolen;
  thief_ant.holding = 1; // Switches to ht* suite
  ```
- **Victim Score Loss Audio (`scoredn.wav` / Sound 88):**
  - Plays descending tone on victim's client as score drains.
  - Victim HUD status reads `"Food stolen..."` (String ID 62).

### 9.3 Lunchbox Carrying, Death Drop & Universal Pickup
- **Visual Suite:** Carrying ants switch from empty-handed `a*` tables to holding `h*` tables (`hg*`, `hf*`, `hb*`, `hc*`, `hs*`, `ht*`) with directional lunchbox sprites (`0lb0000`..`7lb0007`) layered in front.
- **Death Drop (`Anim 356: lunchbox`, Sprite 513: `3lb0001.bmp`):**
  - If a carrier ant dies (combat, bomb, or drowning), a physical lunchbox drops onto Layer 2.
- **Universal Pickup:** ANY ant (friendly, allied, or enemy) can walk over the lunchbox to pick it up and carry it back to their own anthill for score.

---

## 10. Dynamic In-Game Alliances & Scoring Architecture

### 10.1 Alliance Proposal Protocol (`0x01023f9f`, `0x01028f04`)
- Default state: Free-For-All (FFA), `ally_id = 4`.
- **Initiation:** Click directly on an enemy anthill.
- **Target Notification:** Target hears Sound 51 (`allypro.wav`) and sees modal prompt: `"%s (%s) invites you to form a team. Do you want to join forces?"` (String ID 1) with Accept/Deny buttons.
- **Accept Flow:** Both players hear Sound 53 (`allyyes.wav`) followed by Sound 50 (`allyon.wav`). Broadcast: `"%s and %s have formed an alliance!"` (String ID 39). `ally_id` linked.
- **Deny Flow:** Proposer hears Sound 52 (`allynot.wav`). Notification: `"%s declined the alliance invitation."` (String ID 80). Remains FFA.
- **Break Alliance:** Click allied anthill. Both hear Sound 49 (`allyoff.wav`). Broadcast: `"%s broke their alliance with %s!"` (String ID 40). Reverts to `ally_id = 4`.

### 10.2 Combined Scoring vs Discrete Memory Records
- **Scoreboard & Victory:** HUD and standings display combined team score:
  ```c
  allied_score = score[player_a] + score[player_b];
  ```
- **Discrete Memory Preservation:**
  - Individual player structs strictly track raw stats separately in memory (`[esi + 0xf2a]`, `[esi + 0x54f0]`).
  - Dissolving an alliance immediately uncouples scores to exact personal totals without corruption.
- **Rules of Engagement:**
  - Friendly fire disabled between allies.
  - Combat Ant AI ignores allied units in guard perimeter.
  - Thief Ants cannot infiltrate allied anthills.
  - Allies pass freely across each other's bridges.

---

## 11. Game End Sequence & Scorecard Presentation

### 11.1 Match Expiry & Audio Routing
- At match clock `0:00`, simulation halts immediately (ant movement, attacks, timers, click orders freeze).
- **Winner Audio:** Winning player/team hears Sound 56 (`winner.wav`, 22,050 Hz, 4.67s triumphant fanfare).
- **Loser Audio:** Defeated players hear Sound 41 (`playerout.wav`, 11,025 Hz, 0.94s descending defeat sting). Defeated players **never** hear `winner.wav`.

### 11.2 Scorecard Layout (`re_screen` / Animation 25)
Composites authentic 640×480 full-screen results modal:
- Top Banner: Sprite 99 (`resbanr.bmp`, 340×34 at 140, 0) - *"Game Results"*
- Title Art: Sprite 98 (`yoscore.bmp`, 302×127 at 41, 55) - *"YOUR SCORE"*
- Stats Header: Sprite 97 (`newstats.bmp`, 259×133 at 342, 84) - column headers with green indicator arrows
- Winner Pane: Sprite 96 (`winnr.bmp` at 40, 195) + Box (Sprites 92, 90, 87, 91, 93 at 40, 222)
- Other Players Pane: Sprite 95 (`otherp.bmp` at 40, 280) + Box (Sprites 92, 90, 87, 91, 94 at 40, 310)
- Backdrop: Sprites 0..14 (clay texture and beveled borders)

### 11.3 Tracked Statistics (4 Columns)
Aligned under `newstats.bmp` arrow tips:
1. **Score (X ≈ 496):** Final accumulated points (deposits + steals - victim losses).
2. **Friendly Ants Lost (X ≈ 536):** Total friendly ants killed (combat, bombs, drowning).
3. **Enemy Ants Killed (X ≈ 557):** Total enemy units eliminated by player's ants or mines.
4. **New Ants Hatched (X ≈ 578):** Total ants hatched from base.

---

## Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|---|---|---|---|---|---|---|
| 1 | Simulation Grid | Discrete Tile & Pixel Grid | 32×32 pixel tiles; maps sized 60×60, 40×40, 31×31 | Pixel coords `(px, py)` | Tile coords `(tx, ty)` via integer `/ 32` | Out of bounds clamped or returns blocked (`0x100cf0f`) | Disasm `0x100fc89`, `0x100cf0f`, `.LVL` files |
| 2 | Tick Engine | Fixed 20 Hz Tick Loop | Discrete 50 ms tick updates with millisecond action timers | Tick timer / `timeGetTime` | Deterministic simulation step | Frame drops maintain lockstep tick count | `Ants.exe` main loop `0x1031926`, timers |
| 3 | PRNG | MSVC LCG Algorithm | Deterministic linear congruential generator: `holdrand = holdrand * 214013 + 2531011` | Seed (uint32) | Pseudo-random 15-bit uint16 `[0..32767]` | Defaults to `timeGetTime()` if seed omitted | Disasm `0x10345b0`, `0x10345c0`, `latseed:` |
| 4 | Units & Stats | Universal 10 HP Metric | All 6 ant units possess 10 max HP; health stored as uint16 | Damage events | Reduced HP; death at 0 HP | Death code `0xF` on drowning, combat code otherwise | Disasm `0x1004be0 + 0x74`, `0x101b86e` |
| 5 | Combat | Universal 1 HP Melee | Standard melee strike deals 1 HP damage for Worker, Thief, Fire, Bomber, Swimmer | Attack order / proximity | -1 HP to target, Sound 57 (`attack.wav`) | None; standard attack | `GAME_REVERSE_ENGINEERING.md` §5.1 |
| 6 | Combat | Combat Ant 2 HP Heavy Punch | Exclusive heavy punch deals 2 HP damage + 4-5 tile ballistic knockback | Melee contact (`dist <= 1`) | -2 HP, Sound 78 (`attack2.wav`), ballistic flight | None | Anim 902 `acat301`, Sound 78 |
| 7 | Combat Physics | Ballistic Knockback & Tumble | Displaces unit 4-5 tiles backwards into airborne spin and skid | Heavy punch strike | Displaced pos, Action 14 `*gf*`, Action 19 `*gb*` | Halts on solid obstacle collision | Disasm `0x0101c221`, Table 4 anims |
| 8 | Combat Physics | Stun Recovery | Immobilizes knocked-back unit for 12 simulation ticks before resuming | Landing on ground | Action 12 state, Sound 70 (`stun.wav`) | Ignores orders while stunned | `GAME_REVERSE_ENGINEERING.md` §5.7 |
| 9 | Guard AI | Combat Ant Autonomous Scan | Only autonomous unit; scans 3-tile Chebyshev radius while idle | Simulation tick while idle | Aggro trigger on enemy detection | Ignores allies and underground units | Disasm `0x1004be0`, §5.10 |
| 10 | Guard AI | Autonomous Return to Post | Combat Ant returns to guard anchor after delivering punch or target loss | Punch completed / target lost | Paths back to `(x_g, y_g)` | Path recalculation if blocked | `GAME_REVERSE_ENGINEERING.md` §5.10 |
| 11 | Placement | Cardinal-Only Placement | Firewalls and bombs strictly restricted to N, E, S, W adjacent tiles | Placement target `(tx, ty)` | Tile placement initiation | Rejects diagonal (`dx!=0 && dy!=0`) | Disasm `0x10071dd`, `0x1007202`, §5.2 |
| 12 | Placement | Tile Validity Flag Bits | Tile property bit 0x02 required for bombs; bit 0x04 required for fire | Tile ID lookup in properties | Success / Rejection | Placement rejected on invalid terrain | Disasm `0x10071dd`, `0x1007202`, `0x10072bb` |
| 13 | Bombs | Bomber Ant Landmine Planting | Places armed team mine on Layer 2; Sound 90 (`bombpick.wav`) | Ability order on cardinal tile | Active mine on Layer 2 | Rejection if tile invalid | Anim 789 `absb301`, Sound 90 |
| 14 | Bombs | Landmine Detonation | Stepped on by enemy ant; deals 2 HP explosive damage + 2-3 tile knockback | Proximity step trigger | -2 HP, blast knockback, Sound 4 (`bombexp.wav`) | Bomb cleared from Layer 2 | Table 2 Sound 4, §5.1 |
| 15 | Bombs | Body Squash Defusal | Bomber Ant slams body onto active enemy bomb, crushing it safely | Defuse order by Bomber Ant | Mine cleared, Sound 73 (`bombdrop`), Sound 74 (`bombmuffle`) | No explosion / 0 damage taken | Anim 789 `abdb301`, Sounds 73/74 |
| 16 | Fire | Firewall Ignition | Fire Ant focuses magnifying glass; ignites tile 134 (`wallup04`) | Ability order on cardinal tile | Fire on Layer 2, Sounds 67/68, 180s timer | Requires flag bit 0x04 | Anim 703 `afsf301`, Sounds 67/68 |
| 17 | Fire | Fire Walking Trait | Fire Ants are immune to fire damage and traverse fire freely | Pathing onto fire tile | Free traversal | None | Disasm `0x1008bb7`, §5.2 |
| 18 | Fire Physics | Involuntary Fire Damage (+1) | Non-fire ant landing on fire takes +1 fire damage | Ballistic landing on fire | -1 HP (`source = 7`), bounce deflection | Fire persists; never extinguished by landing | Disasm `0x01021627`, `0x0101e9cf` |
| 19 | Fire Physics | Multi-Fire Ricochet | Landing on fire reflects trajectory; chains across adjacent fire tiles | Fire contact | Reflected bounce, +1 damage per contact | Chains until open ground or death | Disasm `0x0101c221`, `0x010229b7` |
| 20 | Fire Physics | Ant Collision Bounce | Bouncing ant collides with occupied unit; deflects trajectory | Landing on occupied tile | Spatial deflection | Deflection into fire takes damage | Disasm `0x0101c221`, §5.2 |
| 21 | Fire | Fire Ant Extinguishing | Fire Ant smothers fire wall; clears Layer 2 and deallocates timer | Extinguish order by Fire Ant | Tile cleared (`0x7FFE`), Sound 69 (`fireextinguish.wav`), Anim 135 (`sputter`) | Non-fire ants cannot extinguish fire | Anim 706 `afxf301`, Sound 69 |
| 22 | Timers | 180s Firewall Expiration | Firewall burns out after exactly 180 seconds | 180,000 ms timer expiry | Tile cleared (`0x7FFE`), Sound 5 (`fireburnout.wav`) | Persists if match ends first | Disasm `0x101e8d5`, `0x1024de7` |
| 23 | Bridges | Multi-Stage Bridge Construction | Swimmer Ant builds bridge across water through 4 progressive stages | Build order on water tile | Stages `bridge1`..`bridge4`, Sound 82 (`shovelwater.wav`) | Water tile required | Anim 1032 `asbbw301`, Sound 82 |
| 24 | Bridges | Universal Traversal | Any ant (friendly, ally, hostile enemy) can traverse completed bridge | Walk command onto bridge | Free passage across water | None | User Clarification 2026-09-06T22:34:55Z |
| 25 | Bridges | 180s Bridge Collapse & Drowning | Bridge collapses after 180s; non-swimmers on bridge drown instantly | 180,000 ms timer expiry | Tile cleared, instant drowning for non-swimmers | Swimmer ants survive unharmed | Disasm `0x101ebd3`, `0x100f8fc`, `0x100f948` |
| 26 | Base | Concentric Chebyshev Queuing | Returning ants queue in Chebyshev rings around anthill in FIFO order | Return to base order | Assigned queue slot in Ring R | Steps forward as entrance clears | Disasm `0x01019900`, §5.6 |
| 27 | Base | 17-Frame Base Entry Sequence | Ant enters anthill, deposits food, dives underground, emerges | Anthill entrance reach | Anim `hgen301`, score increment, emergence | Receptive to commands at Frame 16 | Anim 867 `hgen301`, §5.6 |
| 28 | Base | Underground 100% Full Heal | Ant reaching underground chamber (Frame 8) is fully healed to 10 HP | Frame 8 of entry anim | HP restored to 10 (`powerupc.wav`) | Occurs with or without food | Disasm `0x0101ac8c`, Anim 867 Subitem 8 |
| 29 | Base | Egg Incubation & Hatching | Anthill hatches ant from egg for 200 points | Hatch click order | -200 points, new ant spawned | Prohibited if < 200 pts or 0 eggs | Disasm `0x100ceea`, §1 |
| 30 | Theft | Thief Infiltration Animation | 33-frame sequence diving into enemy base and rummaging | Infiltrate enemy base order | Anim `atcr501`, Sounds 84, 85, 86 | Interrupted if killed before dive | Anim 1095 `atcr501`, Sounds 84-86 |
| 31 | Theft | Victim Alarm Siren & Banner | Enemy base alarm triggers on thief entry; Sound 58 siren + banner | Thief dives into anthill | Sound 58 (`underattack.wav`), String ID 53 banner | Sent only to victim client | Disasm `0x0101b757`, Sound 58 |
| 32 | Theft | 50-Point Score Drain | Thief steals `min(50, score)`; victim hears Sound 88 (`scoredn.wav`) | Theft execution | Points deducted, Sound 88, thief carries loot | If score 0, 0 points stolen | Disasm `0x0101d57c`, Sound 88 |
| 33 | Inventory | Visual Food Carrying Suites | Carrying food switches visual animations to `h*` holding suites | Food pickup or steal | `hg*`, `hf*`, `hb*`, `hc*`, `hs*`, `ht*` anims + lunchbox sprite | None | Disasm `0x0101aefe`, Table 4 |
| 34 | Inventory | Lunchbox Drop on Death | Carrier death drops physical lunchbox on Layer 2; universal pickup | Carrier death | Layer 2 tile `356` (`lunchbox`, Sprite 513) | Universal pickup by any ant | Disasm `0x0100ceb6`, `0x0100fe20` |
| 35 | Alliances | Anthill Click Proposal | Click enemy anthill to propose team; target hears Sound 51 | Click enemy anthill | Sound 51 (`allypro.wav`), modal prompt | FFA default state | Disasm `0x01023f9f`, Sound 51 |
| 36 | Alliances | Acceptance & Dissolution Flow | Accept links teams (Sounds 53/50); break uncouples teams (Sound 49) | Accept / Break click | Sounds 53/50 (`allyyes`/`allyon`) or Sound 49 (`allyoff`) | Broadcasts message strings | Table 2 Sounds 49-53, §5.11 |
| 37 | Alliances | Combined Scoreboard & Discrete Memory | Scores combined for HUD/victory, but tracked individually in memory | Score updates | Combined HUD score; discrete player structs | Clean uncoupling on dissolution | Disasm `[esi + 0xf2a]`, `[esi + 0x54f0]` |
| 38 | Game Over | Match Freeze at 0:00 | Match clock 0:00 halts all simulation, movement, attacks, timers | Timer reaches 0:00 | Simulation frozen | No further inputs processed | Disasm `0x100fa50`, §5.9 |
| 39 | Game Over | Split End-Game Audio | Winner hears Sound 56 (`winner.wav`); losers hear Sound 41 (`playerout.wav`) | Game over state evaluation | Fanfare to winner; defeat sting to losers | Losers never hear `winner.wav` | Table 2 Sounds 41 & 56, §5.9 |
| 40 | Game Over | Full-Screen Scorecard Modal | `re_screen` (149 frames) displays 4 statistics per player | End game transition | 640×480 results overlay with 4 stat columns | Interactive close / return | Anim 25 `re_screen`, Sprites 0-14, 85-99 |

---

## Edge Cases

| # | Feature | Input | Observed Behavior |
|---|---|---|---|
| 1 | Cardinal Placement | Order placement on diagonal tile `(x+1, y+1)` | Order is rejected from current tile; ant pathfinds to adjacent orthogonal tile `(x+1, y)` or `(x, y+1)` before initiating placement. |
| 2 | Cardinal Placement | Order placement on tile with `CAN_PLACE_BOMB=0` (e.g. water or rock) | Placement action is strictly rejected; no bomb or fire is created; ant aborts order and remains idle. |
| 3 | Combat Ant Knockback | Target knocked back into solid stone cliff or tree | Knockback trajectory halts immediately upon impact; unit enters ground bounce tumble, plays `flythumpa.wav`, and enters 12-tick stun state. |
| 4 | Combat Ant Knockback | Non-swimmer ant knocked back into deep water tile | Unit lands in deep water and drowns instantly (`death_status = 0xF`, HP = 0, playing drowning audio). |
| 5 | Combat Ant Knockback | Swimmer Ant knocked back into deep water tile | Swimmer Ant survives landing unharmed, plays `splash.wav` (Sound 71), and transitions smoothly to swim state (`assw*`). |
| 6 | Fire Landing | Non-fire ant knocked onto active fire tile | Takes 1 fire damage immediately (`damage_source = 7`); cannot occupy tile; reflects trajectory and bounces off immediately. Fire is NOT extinguished. |
| 7 | Multi-Fire Ricochet | Ant bounces from one fire tile directly onto another fire tile | Ant takes another 1 point of fire damage upon contact and rebounds again; continues ricocheting until clear ground is reached or HP reaches 0. |
| 8 | Fire Ricochet Collision | Bouncing ant lands on a tile occupied by another ant | Spatial collision occurs: moving ant deflects off occupied unit. If deflected back onto fire, takes another point of fire damage. |
| 9 | Fire Extinguish Attempt | Worker, Bomber, Combat, Swimmer, or Thief ordered to extinguish fire | Only Fire Ants have the extinguish action (`afxf301`); other ants cannot extinguish fire and treat fire as an impassable wall. |
| 10 | Bridge Collapse | Hostile enemy ant standing on bridge when 180s timer expires | Bridge tile is cleared; enemy ant falls into deep water and drowns instantly. |
| 11 | Bridge Collapse | Swimmer Ant standing on bridge when 180s timer expires | Bridge tile is cleared; Swimmer Ant plunges into water, survives unharmed, and switches to swimming mode. |
| 12 | Bridge Traversal | Hostile enemy ant issued move command across newly built friendly bridge | Enemy ant traverses bridge freely across the water gap without impediment. |
| 13 | Anthill Infiltration | Thief Ant dives into victim base with score = 25 | Thief steals `min(50, 25) = 25` points; victim score drops to 0; thief emerges carrying 25 points in lunchbox. |
| 14 | Anthill Infiltration | Thief Ant dives into victim base with score = 0 | Thief steals `min(50, 0) = 0` points; victim score remains 0; thief emerges carrying empty lunchbox. Siren Sound 58 still fires. |
| 15 | Base Healing | Ant enters anthill at 1 HP without carrying any food | Ant descends shaft; upon reaching Frame 8 underground chamber, HP is restored 100% to 10 HP; ant emerges fully healed. |
| 16 | Hatching Under-Score | Player clicks hatch button when team score = 150 (and eggs > 0) | Hatch order rejected; hatching requires minimum 200 points. |
| 17 | Hatching Zero-Eggs | Player clicks hatch button when team score = 500 but egg count = 0 | Hatch order rejected; hatching requires at least 1 egg in inventory. |
| 18 | Carrier Death | Carrier ant holding lunchbox killed by combat punch over water bridge | Ant dies; lunchbox drops onto bridge tile on Layer 2; accessible for universal pickup by any ant until bridge expires. |
| 19 | Carrier Death | Carrier ant holding lunchbox drowns when bridge collapses | Carrier ant drowns; lunchbox drops into deep water and is lost / removed from map. |
| 20 | Alliance Friendly Fire | Player right-clicks attack order onto allied ant | Order rejected or path follows ally without initiating attack strikes; friendly fire between allies is disabled. |
| 21 | Alliance Thief Raiding | Thief Ant ordered to infiltrate allied anthill | Infiltration order rejected; Thief Ants cannot rob allied bases. |
| 22 | Alliance Dissolution | Alliance broken while team scores are aggregated on HUD | HUD immediately decouples; each player's displayed score reverts to their exact individually tracked points in memory. |
| 23 | Game End Tie | Two teams have identical high scores at match clock 0:00 | Simulation freezes; both tied teams evaluate winner status; tie-break rules apply or dual winner fanfare triggers. |
| 24 | PRNG Latseed | Engine launched with `latseed:12345` on command line | MSVC LCG initializes `holdrand = 12345`; all randomized deflection vectors and spawn timings execute 100% identically across runs. |
