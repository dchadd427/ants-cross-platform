# Handoff Report: Units, Combat AI & Physics Architecture (`ants-sim`)

**From:** M2 Explorer 2 (`explorer_m2_2`)  
**To:** M2 Implementers & Orchestrator (`orchestrator_1` / `a28dfa55-5a82-453d-a21b-99459a66b340`)  
**Target Milestone:** M2 (`ants-sim`)  
**Date:** 2026-09-06T23:16:00Z  

---

## 1. Observation

1. **Unit Classes, Health & Speeds (`GAME_REVERSE_ENGINEERING.md` §2.3, §5.1; `survey_sim.md` §2):**
   - Class ID mapping: Worker (`0`), Bomber (`1`), Fire (`2`), Thief (`3`), Combat (`4`), Swimmer (`5`).
   - Health tracking: Universal 10 HP starting and maximum cap (`CAntUnit + 0x74`). Standard melee attack deals strictly 1 HP damage for Worker, Bomber, Fire, Thief, Swimmer.
   - Movement speeds at 20 Hz discrete ticks (50 ms per tick, 32×32 pixel tiles):
     * Base ground speed: 2.5 tiles/sec = 80 pixels/sec = 4.0 pixels/tick.
     * Thief scout boost: 3.5 tiles/sec = 112 pixels/sec = 5.6 pixels/tick.
     * Swimmer aquatic speed: 2.0 tiles/sec = 64 pixels/sec = 3.2 pixels/tick.
   - Holding state suites: Empty-handed ants use `a*` prefixes (`ag`, `ab`, `af`, `at`, `ac`, `as`). Carrying ants switch to `h*` suites (`hg`, `hb`, `hf`, `ht`, `hc`, `hs`) with layered directional lunchbox sprites (`0lb0000`..`7lb0007`).

2. **Combat Ant Autonomous Guard AI (`GAME_REVERSE_ENGINEERING.md` §5.10; `survey_sim.md` §3):**
   - Combat Ant (`ac`) is the **sole unit type** with autonomous AI behavior.
   - Guard Anchor Post: Commits tile coordinates `(x_g, y_g)` upon entering Idle state.
   - Proximity Scan: Evaluates a 3-tile Chebyshev radius (`max(|x_e - x_g|, |y_e - y_g|) <= 3`, 7×7 square area) on every tick while idle.
   - Target Filtering: Targets live enemies (`team_id != local_team && !is_allied`). Ignores allies and submerged underground units (e.g. Thief inside anthill or unit in Frame 8 heal).
   - Strike Delivery: Heavy punch (`acat301`, Subitem 2) inflicts **2 HP damage** and triggers **4–5 tile ballistic knockback** with Sound 78 (`attack2.wav`).
   - Autonomous Return: Immediately upon strike completion or target loss/escape, pathfinds back to `(x_g, y_g)` and resumes idle guard stance.

3. **Ballistic Knockback & Obstacle Collision Physics (`survey_sim.md` §2.3, §6; `GAME_REVERSE_ENGINEERING.md` §5.7):**
   - Combat Ant punch propels victim 4–5 tiles; bomb blast propels caught units 2–3 tiles.
   - Parabolic trajectory in 2D top-down space with apex height $H_{\text{apex}} \approx 36$ pixels over 8–10 ticks.
   - Solid obstacle collision (rock, tree, stone wall, cliff, map boundary) terminates flight immediately at impact, triggers impact audio (Sound 64/65), and transitions into ground bounce (`*gb*`).
   - Stun Recovery: Unit enters stunned recovery state (Action 12, Sound 70 `stun.wav`) for **12 simulation ticks (600 ms)**, during which it is immobilized and unreceptive to commands.

4. **Deep Water Landing & Ant Drowning (`ORIGINAL_REQUEST.md` Directive 22:37:34Z; `GAME_REVERSE_ENGINEERING.md` §5.12):**
   - Deep water landing (or 180s bridge collapse under an ant) triggers fatal instant drowning for non-swimmers:
     * `death_status = 0x0F`, `HP = 0`.
     * Sound 71 (`splash.wav`) at Subitem 0; Sound 72 (`antdrown.wav`) at Subitem 1.
     * 22-subitem drowning animation (`agdr301`, `afdr301`, `abdr301`, `acdr301`, `atdr301`) with rising air bubbles (Sprites 1223..1227) before permanent deallocation.
   - Swimmer Ant Exception: Swimmer Ant is immune, takes 0 damage, plays Sound 71 (`splash.wav`), and enters swimming mode (`assw*`).

5. **Fire Ricochet Physics (`survey_sim.md` §6.2; Disasm `0x01021627`, `0x0101c221`):**
   - Landing on active fire tile (`wallup04`, tile 134) inflicts **+1 fire damage** (`DamageSource::FireBurn = 7`).
   - Non-fire ants cannot occupy fire; they bounce off along reflected trajectory `(incoming_dir + 4 + offset) % 8`.
   - Multi-fire chains: Bouncing onto adjacent fire deals another +1 damage and rebounds again.
   - Fire is **never** extinguished by ants landing on or bouncing off it.

---

## 2. Logic Chain

1. **Deterministic Fixed-Point Spatial Model:**
   - Floating-point calculations cause cross-platform divergence across macOS, Linux, and Windows compilers.
   - Using 16.16 fixed-point arithmetic (`fx_x`, `fx_y`) allows exact representation of fractional pixel speeds per 20 Hz tick:
     * Standard: 4.0 px/tick (`4 << 16 = 262144`).
     * Thief: 5.6 px/tick (`5.6 * 65536 = 367001`).
     * Aquatic: 3.2 px/tick (`3.2 * 65536 = 209715`).
   - Diagonal scaling via fixed-point multiplication with $46341/65536 \approx 1/\sqrt{2}$ guarantees isotropic speed in all 8 compass directions.

2. **Decoupled Combat AI Controller:**
   - Embedding complex AI state machines inside a generic `AntUnit` struct bloats entity memory for the 5 passive unit types.
   - Encapsulating the Combat Ant Guard behavior in `CombatAIController` creates a clean, modular lifecycle (`GuardIdle` $\to$ `Intercepting` $\to$ `Striking` $\to$ `Returning` $\to$ `GuardIdle`) that operates seamlessly alongside manual player orders.

3. **Dedicated Ballistic Physics Engine:**
   - Parabolic knockback affects all unit types from two distinct origins (Combat punches and Landmine blasts).
   - Centralizing ballistic trajectory progression, obstacle collision raycasting, water landing checks, fire ricochets, and stun timer management into `PhysicsEngine` eliminates duplication and guarantees identical physical interactions across all scenarios.

4. **Integration with Milestone 1 (`ants-assets`):**
   - Aliasing `Direction` directly from `ants::assets::Direction` in `include/ants_assets/mirroring.hpp` eliminates type mismatches and enables direct integration with sprite mirroring and animation systems.

---

## 3. Caveats

1. **Pursuit Distance Limit:**
   - To prevent Combat Ants from chasing enemy scouts across the entire map, an autonomous disengagement threshold of 5 tiles from `guard_anchor` is implemented. If the intruder flees beyond 5 tiles, the Combat Ant abandons pursuit and returns to post.
2. **Submerged State Immunity:**
   - Target filtering explicitly checks `is_underground()`. Units inside anthills (e.g. Thief during `atcr501` rummage or units undergoing Frame 8 full heal) cannot be targeted by Combat Ants.
3. **No External Modifications:**
   - In strict compliance with the Explorer persona, no source code was directly committed to `include/` or `src/`. Complete header specifications, class definitions, and algorithmic formulas are documented in `combat_and_physics_plan.md`.

---

## 4. Conclusion

1. The architectural blueprint and complete C++17 header designs for ant units, combat AI, and physics are finalized and documented in:
   `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_2/combat_and_physics_plan.md`
2. Deliverables provided:
   - `ant_unit.hpp`: 6 ant types, universal 10 HP tracking, 20 Hz fixed-point movement, 8 compass directions, and `a*`/`h*` holding states.
   - `combat_ai.hpp`: Combat Ant autonomous guard AI with anchor post retention, 3-tile Chebyshev scan, target filtering, intercept pathing, 2 HP punch, and automatic return.
   - `physics.hpp`: Ballistic knockback (4–5 tiles punch, 2–3 tiles bomb), parabolic trajectory, obstacle collisions, instant water drowning (`death_status = 0xF`, Sound 71/72) vs Swimmer survival, fire ricochet (+1 damage, never extinguish), and 12-tick stun.
3. All components are ready for immediate implementation by M2 Workers.

---

## 5. Verification Method

1. **Inspect Blueprint:**
   - Review `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_2/combat_and_physics_plan.md` to confirm complete C++ header declarations, formulas, and state machines.
2. **Deterministic Speed & Fixed-Point Verification:**
   - Verify integer speed conversions:
     * Standard: $262144 / 65536 = 4.0\text{ px/tick} \times 20\text{ ticks} = 80\text{ px/sec} / 32 = 2.5\text{ tiles/sec}$.
     * Thief: $367001 / 65536 \approx 5.6\text{ px/tick} \times 20\text{ ticks} = 112\text{ px/sec} / 32 = 3.5\text{ tiles/sec}$.
     * Aquatic: $209715 / 65536 \approx 3.2\text{ px/tick} \times 20\text{ ticks} = 64\text{ px/sec} / 32 = 2.0\text{ tiles/sec}$.
3. **Automated Headless Test Invalidation Conditions (`test_sim_rules.cpp`):**
   - Non-combat melee strike $\ne 1\text{ HP}$ or Combat punch $\ne 2\text{ HP}$.
   - Idle Combat Ant fails to detect enemy within 3-tile Chebyshev radius or fails to return to anchor post after strike.
   - Non-swimmer ant landing in deep water does not set `death_status = 0x0F` or does not trigger Sound 71/72.
   - Swimmer ant landing in deep water takes $> 0$ damage.
   - Fire landing does not apply $+1$ damage or extinguishes the fire.
   - Stun timer $\ne 12\text{ ticks}$.
