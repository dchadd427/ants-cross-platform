# Milestone 2 Iteration 2 Review Handoff Report

**Author**: `reviewer_m2_it2_2`  
**Role**: Reviewer & Adversarial Critic  
**Working Directory**: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m2_it2_2`  
**Target Milestone**: Milestone 2 (`ants-sim`)  
**Verdict**: **APPROVE**  
**Date**: 2026-09-06  

---

## 1. Observation

### 1.1 Integer-Only Math and Zero Floating-Point Progression
- Static analysis via pattern matching across `src/ants_sim` and `include/ants_sim` revealed zero occurrences of `float` or `double` data types or floating-point expressions in execution logic:
  - `grep -rn "float" src/ants_sim/` returned 0 matches.
  - `grep -rn "double" src/ants_sim/` returned 0 matches.
  - `grep -rn "float" include/ants_sim/` returned 1 match: a descriptive comment in `include/ants_sim/prng.hpp:49` ("*Uses pure integer arithmetic to prevent floating-point divergence*").
  - `grep -rn "double" include/ants_sim/` returned 0 matches.
- In `src/ants_sim/sim_engine.cpp:146-320` (`SimulationEngine::tick()`):
  - Discrete timing uses integer subtraction: `impl_->match_time_remaining_ms_ -= 50; impl_->current_tick_++;` (lines 156-157).
  - Structure timers decrement integer ticks (`cell.timer_ticks--;`, lines 165, 171).
  - Spatial coordinates and distances are pure `int32_t` / `uint32_t` integer tile and pixel grids (`x * 32 + 16`, `TILE_PIXELS = 32`).
- In `src/ants_sim/ant_unit.cpp:75-134` (`AntUnit::tick_movement`):
  - Locomotion uses fixed-point 16.16 integer arithmetic (`FixedPointMath::mul(speed_fx, DIAG_SCALE_FX)` where `DIAG_SCALE_FX = 46341` represents $1/\sqrt{2} \approx 0.7071$ in 16.16).
  - Position updates use integer additions/subtractions with clamped steps: `fx_x += std::min(step_fx, dx << 16)`.
- In `src/ants_sim/physics.cpp:73-77`:
  - Ballistic flight displacement and altitude use exact integer formulations:
    ```cpp
    int32_t cur_px = f.start_px + ((f.target_px - f.start_px) * f.current_tick) / f.total_ticks;
    int32_t cur_py = f.start_py + ((f.target_py - f.start_py) * f.current_tick) / f.total_ticks;
    int32_t z = (4 * f.apex_height_px * f.current_tick * (f.total_ticks - f.current_tick)) /
                (f.total_ticks * f.total_ticks);
    ```
- In `src/ants_sim/grid.cpp` and `include/ants_sim/grid.hpp`:
  - Chebyshev distance `max(|dx|, |dy|)`, Manhattan distance `|dx| + |dy|`, and integer coordinate calculations are 100% integer arithmetic.

### 1.2 MSVC LCG PRNG Reproducibility
- In `include/ants_sim/prng.hpp:17-38`:
  ```cpp
  static constexpr uint32_t MULTIPLIER   = 214013u;
  static constexpr uint32_t INCREMENT    = 2531011u;
  static constexpr uint32_t MASK_15BIT   = 0x7FFFu;
  ...
  constexpr uint16_t rand() noexcept {
      state_ = state_ * MULTIPLIER + INCREMENT;
      return static_cast<uint16_t>((state_ >> 16) & MASK_15BIT);
  }
  ```
- Evaluated with initial seed `state = 1`:
  $\text{state}_1 = 1 \times 214013 + 2531011 = 2745024 = \text{0x29E2C0}$.
  $(\text{0x29E2C0} \gg 16) = \text{0x29} = 41$.
  $41 \ \& \ \text{0x7FFF} = 41$.
  Verified directly by test case 2.1 in `tests/test_sim/test_sim_rules.cpp:116-120`.
- All random number usage across `SimulationEngine`, `PhysicsEngine`, and `CombatAIController` flows exclusively through `PRNG` instances seeded at initialization (`SimulationEngine::init`). No calls to host standard library `::rand()`, `std::random_device`, or system time were detected.

### 1.3 Complete Eradication of Synthetic `simulate_ballistic_flight` Facade
- Full repository search: `grep -rn "simulate_ballistic_flight" include/ src/ tests/` returned 0 matches.
- In `tests/test_sim/test_sim_rules.cpp:454-467` (Test 7.5) and `tests/test_sim/test_challenger_m2_1.cpp:519-539` (Test 3.3), tests invoke `sim.resolve_fire_contact(victim, 1, 0);` directly, engaging genuine `PhysicsEngine::resolve_fire_contact` simulation rather than the removed synthetic stepping facade.

### 1.4 Multi-Fire Ricochet Vector Updates
- In `src/ants_sim/physics.cpp:150-207` (`PhysicsEngine::resolve_fire_contact`):
  - On backward reflection:
    ```cpp
    unit.set_tile_pos(nx, ny);
    incoming_dx = b_dx;
    incoming_dy = b_dy;
    ```
  - On forward deflection:
    ```cpp
    unit.set_tile_pos(def_x, def_y);
    incoming_dx = -b_dx;
    incoming_dy = -b_dy;
    ```
  - On perpendicular escape:
    ```cpp
    unit.set_tile_pos(perp_x, perp_y);
    incoming_dx = b_dy;
    incoming_dy = b_dx;
    ```
  - Subsequent iterations of the `while (grid.get_cell(unit.pos).has_fire())` loop recalculate reflections using the updated `incoming_dx` and `incoming_dy`, correctly chaining angle changes across consecutive fire contacts. Loop termination is guarded by `loop_guard < 10`.

### 1.5 Combat AI Knockback Raycasting and Boundary Safety
- In `src/ants_sim/combat_ai.cpp:210-225`:
  ```cpp
  int32_t dist_tiles = 4 + (static_cast<int32_t>(random_seed) & 1);
  TileCoord land_pos = target->pos;
  for (int32_t s = 1; s <= dist_tiles; ++s) {
      TileCoord next{target->pos.x + step_x * s, target->pos.y + step_y * s};
      if (!grid.in_bounds(next) || grid.is_solid_obstacle(next.x, next.y)) {
          break;
      }
      land_pos = next;
  }

  int32_t max_x = (grid.width() > 0) ? static_cast<int32_t>(grid.width() - 1) : 0;
  int32_t max_y = (grid.height() > 0) ? static_cast<int32_t>(grid.height() - 1) : 0;
  int32_t clamped_x = std::clamp(land_pos.x, 0, max_x);
  int32_t clamped_y = std::clamp(land_pos.y, 0, max_y);

  target->set_tile_pos(clamped_x, clamped_y);
  target->start_stun(AntUnit::STUN_TICKS);
  ```
  - Target displacement stops at solid obstacles and map boundaries along the raycast trajectory.
  - Final coordinates are strictly clamped within $[0, \text{width} - 1]$ and $[0, \text{height} - 1]$, preventing teleportation out of bounds.

### 1.6 AddressSanitizer and UndefinedBehaviorSanitizer Clean Execution
- Invocation: `./run_tests.sh --clean --asan`
- Compilation with `-fsanitize=address,undefined -fno-omit-frame-pointer`: Clean with 0 warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor`.
- Execution results:
  - `test_assets`: PASSED (exit code 0)
  - `test_sim_rules`: PASSED (62/62 test cases, exit code 0)
  - `test_challenger_m2_1`: PASSED (36/36 test cases, 273 assertions, exit code 0)
  - `test_challenger_m2_2`: PASSED (30/30 test cases, 269 assertions, exit code 0)
  - `e2e_runner`: PASSED (506/506 test cases, exit code 0)
- AddressSanitizer output: 0 memory leaks, 0 heap buffer overflows, 0 stack/global overflows, 0 use-after-free errors.
- UndefinedBehaviorSanitizer output: 0 runtime errors, 0 misaligned accesses, 0 integer overflow reports.

### 1.7 Integrity and Anti-Cheat Audit
- Searched codebase for hardcoded expected test values, dummy stubs, and shortcuts:
  - No fake test assertions, dummy classes, or hardcoded return sequences matching test IDs.
  - No bypassing of genuine simulation pipelines.
  - Full authentic state progression verified in `SimulationEngine::tick()`.

---

## 2. Logic Chain

1. **Determinism Logic**:
   - Observations in Section 1.1 establish that all simulation state updates (`SimulationEngine::tick()`, `ant_unit.cpp`, `physics.cpp`, `grid.cpp`) exclusively use integer and 16.16 fixed-point arithmetic. Because IEEE 754 floating-point rounding modes, FMA contraction, and compiler-specific excess precision cannot influence integer operations, the simulation is cross-platform and compiler mathematically deterministic.
   - Observations in Section 1.2 prove that pseudo-random decisions rely solely on the authentic MSVC LCG generator `holdrand * 214013 + 2531011`, producing byte-for-byte identical streams given identical seeds.

2. **Authenticity & Integrity Logic**:
   - Observations in Section 1.3 confirm that the previous iteration's synthetic facade `simulate_ballistic_flight` was deleted from the codebase.
   - Observations in Sections 1.4 and 1.5 confirm that fire ricochets compute real reflections with updated directional vectors, and Combat Ant knockback performs legitimate obstacle-aware raycasting with coordinate clamping.
   - Observation 1.7 confirms the absence of dummy facades, bypasses, or hardcoded cheating.

3. **Memory Safety & Robustness Logic**:
   - Observations in Section 1.6 show that compiling with AddressSanitizer and UndefinedBehaviorSanitizer and running all test suites (comprising 634 total tests and over 1,000 assertions across unit and E2E suites) completes with zero sanitizer violations, memory leaks, or runtime traps.

---

## 3. Caveats

- An unintegrated scratch test file `tests/test_sim/test_challenger_m2_it2_deep_stress.cpp` created by a peer challenger agent exists on disk but was not wired into `CMakeLists.txt` (and contains local macro naming discrepancies). Its presence does not impact the active build or test pipeline, and all scenarios it sketches (Chebyshev queuing, Frame 8 healing, post-match freeze, thief alarms, alliance shifts) are comprehensively covered and passing in `test_challenger_m2_2.cpp`.
- No other caveats.

---

## 4. Conclusion

Milestone 2 Iteration 2 fully satisfies all architectural, physical, mathematical, and memory-safety requirements:
- Mathematical determinism is guaranteed via 100% integer and fixed-point state advancement.
- MSVC LCG PRNG faithfully reproduces authentic sequences.
- Synthetic facades have been eliminated and replaced with genuine physics simulation.
- Multi-fire ricochets properly propagate approach vector updates.
- Combat AI knockback raycasts and clamps coordinates without clipping.
- The entire test suite runs cleanly under AddressSanitizer and UndefinedBehaviorSanitizer with zero defects.

**Verdict**: **APPROVE**

---

## 5. Verification Method

To independently reproduce this verification:

1. **Verify Complete Absence of Synthetic Facade**:
   ```bash
   grep -rn "simulate_ballistic_flight" include/ src/ tests/
   ```
   *Expected*: 0 matches.

2. **Verify Zero Floating-Point Types in Core Simulation**:
   ```bash
   grep -rn "float" src/ants_sim/
   grep -rn "double" src/ants_sim/
   ```
   *Expected*: 0 matches in execution logic.

3. **Build and Run Clean ASan/UBSan Test Run**:
   ```bash
   ./run_tests.sh --clean --asan
   ```
   *Expected*:
   - `test_assets`: PASSED
   - `test_sim_rules`: PASSED (62/62)
   - `test_challenger_m2_1`: PASSED (36/36)
   - `test_challenger_m2_2`: PASSED (30/30)
   - `e2e_runner`: PASSED (506/506)
   - 0 AddressSanitizer reports, 0 UndefinedBehaviorSanitizer reports, exit code 0.
