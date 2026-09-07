# Handoff Report: Physics Ricochets, Autonomous Orders & Test Suite Integration (M2 It2)

**Agent Identity**: `explorer_m2_it2_3`  
**Role**: teamwork_preview_explorer (Physics, Autonomous Simulation & Test Integration Specialist)  
**Parent Conversation ID**: `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory**: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_3`  
**Date**: 2026-09-06  
**Status**: COMPLETE  

---

## 1. Observation

### 1.1 Multi-Fire Ricochet Angle Freezing
- **File**: `src/ants_sim/physics.cpp`, Lines 160–187:
  ```cpp
  void PhysicsEngine::resolve_fire_contact(AntUnit& unit,
                                           Grid& grid,
                                           std::vector<AudioEvent>& audio_out,
                                           [[maybe_unused]] PRNG& prng,
                                           int32_t incoming_dx,
                                           int32_t incoming_dy) {
      if (unit.type == AntType::Fire) {
          return;
      }

      int loop_guard = 0;
      while (grid.in_bounds(unit.pos.x, unit.pos.y) &&
             grid.get_cell(unit.pos).has_fire() &&
             unit.is_alive() && loop_guard < 10) {
          loop_guard++;

          // Contact damage (+1 fire damage)
          unit.take_damage(1, DamageSource::FireBurn, 0);
          if (!unit.is_alive()) {
              return;
          }

          // Reflection trajectory: bounce back opposite of incoming movement
          int32_t b_dx = -incoming_dx;
          int32_t b_dy = -incoming_dy;
          if (b_dx == 0 && b_dy == 0) {
              b_dx = -1;
          }

          int32_t nx = unit.pos.x + b_dx;
          int32_t ny = unit.pos.y + b_dy;
          if (grid.in_bounds(nx, ny) && !grid.is_solid_obstacle(nx, ny)) {
              unit.set_tile_pos(nx, ny);
          } else {
              // Deflect
              unit.set_tile_pos(unit.pos.x - b_dx, unit.pos.y - b_dy);
          }
      }
  ```
- **Direct Observation**: Neither branch (lines 182 and 185) updates `incoming_dx` or `incoming_dy`. On any subsequent iteration where `(nx, ny)` contains another fire tile, `b_dx` is recalculated from the initial velocity rather than the bounce vector that brought the unit to `(nx, ny)`.

### 1.2 Synthetic Shortcut Method `simulate_ballistic_flight`
- **File**: `include/ants_sim/sim_engine.hpp`, Line 245:
  ```cpp
  void simulate_ballistic_flight(uint32_t ant_id, TileCoord t1, TileCoord t2, TileCoord t3);
  ```
- **File**: `src/ants_sim/sim_engine.cpp`, Lines 779–790:
  ```cpp
  void SimulationEngine::simulate_ballistic_flight(uint32_t ant_id, TileCoord t1, TileCoord t2, TileCoord t3) {
      AntUnit* u = impl_->find_unit(ant_id);
      if (!u) return;

      TileCoord steps[3] = {t1, t2, t3};
      for (const auto& step : steps) {
          u->set_tile_pos(step.x, step.y);
          if (impl_->grid_.has_fire_at(step)) {
              u->take_damage(1, DamageSource::FireBurn, 0);
          }
      }
  }
  ```
- **Call Sites**:
  - `tests/test_sim/test_sim_rules.cpp:460`: `sim.simulate_ballistic_flight(victim, {12, 10}, {13, 10}, {14, 10});`
  - `tests/test_sim/test_challenger_m2_1.cpp:530`: `sim.simulate_ballistic_flight(victim, {20, 20}, {21, 20}, {22, 20});`
- **Direct Observation**: This method artificially iterates through three coordinates passed by callers, bypassing physics ticks, collision detection, and genuine ricochet resolution.

### 1.3 Missing `OrderType::ReturnToBase` and Inactive Tick Order Progression
- **File**: `src/ants_sim/sim_engine.cpp`, Lines 251–286:
  `switch (order.type)` handles `Move`, `Attack`, `PlantBomb`, `DefuseBomb`, `IgniteFire`, `ExtinguishFire`, `BuildBridge`, `InfiltrateAnthill`, and `Cancel`. `OrderType::ReturnToBase` is completely missing.
- **File**: `src/ants_sim/sim_engine.cpp`, Lines 221–240:
  `SimulationEngine::tick()` moves units and checks lunchbox pickups, but never detects when an ant carrying food reaches friendly anthill to trigger base entry, heal, or deposit. Units set to `UnitState::Infiltrating` by `start_thief_infiltration` remain motionless because `tick_movement()` in `ant_unit.cpp:76` requires `state == UnitState::Walking`.
- **Tool Result**: Executed `adversarial_stress_test` from `reviewer_m2_1`:
  ```
  [Test 4] InfiltrateAnthill & ReturnToBase Order Execution via tick():
    Thief state immediately after order: 16
    Thief pos after 50 ticks: (10, 10)
    Team 1 score after 50 ticks: 100
    >>> CONFIRMED DUMMY / BROKEN WORKFLOW: InfiltrateAnthill order leaves ant frozen; never executes infiltration in tick()!
  ```

### 1.4 Test Suite Integration in `CMakeLists.txt` and `run_tests.sh`
- **File**: `tests/test_sim/CMakeLists.txt`: Lines 20–57 define executables and tests for `test_sim_rules`, `test_challenger_m2_1`, and `test_challenger_m2_2`.
- **File**: `run_tests.sh`: Lines 161–168:
  ```bash
  if [ "$RUN_SIM" -eq 1 ]; then
      "./$BUILD_DIR/tests/test_sim/test_sim_rules"
      SIM_STATUS=$?
  fi
  ```
- **Direct Observation**: `run_tests.sh` only ran `test_sim_rules`. Neither `test_challenger_m2_1` nor `test_challenger_m2_2` was executed during `./run_tests.sh --sim` or `./run_tests.sh --all`.

---

## 2. Logic Chain

1. **Multi-Fire Ricochet Invariance**:
   - `GAME_REVERSE_ENGINEERING.md § 5.2` specifies that when an ant bounces off a fire tile into an adjacent fire tile, it takes an additional fire damage and reflects away again.
   - Because `incoming_dx` and `incoming_dy` were not updated upon reflection, consecutive bounces operated on the initial entry vector instead of the current arrival vector.
   - Updating `incoming_dx = b_dx; incoming_dy = b_dy;` on non-fire bounces, and deflecting forward/away (`incoming_dx = -b_dx; incoming_dy = -b_dy;`) when backward reflection hits a fire tile or obstacle, guarantees proper deflection across consecutive firewalls onto clear ground.

2. **Elimination of Fake Physics Facades**:
   - `simulate_ballistic_flight` violated the project integrity mandate by artificially setting entity positions without physics ticks.
   - Replacing this shortcut with `SimulationEngine::resolve_fire_contact` and `SimulationEngine::apply_knockback` delegates collision resolution directly to `PhysicsEngine`, allowing tests and gameplay to use authentic physics.

3. **Autonomous Simulation Tick Integration**:
   - In Microsoft Ants, ants carrying harvested food return to base and autonomously enter the anthill (`EnteringBase`), deposit food at frame 4 (+score, Sound 87), heal 100% at frame 8 (Sound 36), and emerge at frame 16.
   - Thief ants entering enemy anthills autonomously dive (`Infiltrating`), sound Siren 58 and News String 53 at frame 19, steal up to 50 points at frame 32, and retrieve a lunchbox.
   - Adding `OrderType::ReturnToBase` to `issue_order()` commands units toward their friendly base coordinates (`find_anthill`).
   - Integrating state checks into `SimulationEngine::tick()` enables units to autonomously advance through base entry and infiltration without manual test harness shims.

4. **Test Suite Completeness**:
   - Challenger test suites (`test_challenger_m2_1` and `test_challenger_m2_2`) contain critical empirical stress tests.
   - Integrating them into `run_tests.sh` ensures any future regression or defect will immediately fail the master runner.

---

## 3. Caveats

- **Combat AI Raycasting Boundary Fix**: Finding 4 from `reviewer_m2_1` (knockback boundary clamping in `CombatAIController::update_striking`) is handled in tandem with the physics knockback integration. The remediation document details the coordinate clamping to prevent out-of-bounds positioning.
- **Milestone Scope**: Audio playback and MIDI rendering are reserved for Milestone 3 (`ants-app`); simulation tests correctly verify `AudioEvent` and `NewsEvent` queue dispatching at the engine level.

---

## 4. Conclusion

All four assigned areas have been thoroughly investigated, diagnosed, and fully formulated into exact C++ implementations and unified git diffs in:
`/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_3/physics_and_integration_remediation.md`

Specifically:
1. `src/ants_sim/physics.cpp:173`: Updated `incoming_dx` and `incoming_dy` upon each reflection and deflection in `resolve_fire_contact`.
2. `sim_engine.cpp` / `sim_engine.hpp`: Removed `simulate_ballistic_flight`. Added `apply_knockback` and `resolve_fire_contact` delegating directly to `PhysicsEngine`. Updated `test_sim_rules.cpp` (Test 7.5) and `test_challenger_m2_1.cpp` (Test 3.3).
3. `sim_engine.cpp`: Handled `OrderType::ReturnToBase` in `issue_order`. Added autonomous base entry/heal/deposit on friendly anthill arrival and autonomous thief infiltration/siren/loot on enemy anthill arrival in `SimulationEngine::tick()`. Fixed hardcoded victim player ID and frame 8 heal sound spam.
4. `CMakeLists.txt` & `run_tests.sh`: Registered both challenger suites and updated `run_tests.sh` to execute `test_sim_rules`, `test_challenger_m2_1`, and `test_challenger_m2_2` under `--sim` and `--all`.

---

## 5. Verification Method

### 5.1 Independent Reproduction Commands
```bash
cd /Users/dchadd/Desktop/Ants-Mac

# 1. Compile all targets
cmake -B build -S .
cmake --build build -j

# 2. Run simulation rules tests
./build/tests/test_sim/test_sim_rules

# 3. Run Challenger M2_1 test suite
./build/tests/test_sim/test_challenger_m2_1

# 4. Run Challenger M2_2 test suite
./build/tests/test_sim/test_challenger_m2_2

# 5. Run master runner with AddressSanitizer
./run_tests.sh --sim --asan
./run_tests.sh --all
```

### 5.2 Invalidation Conditions
- Existence of `simulate_ballistic_flight` anywhere in the codebase.
- Consecutive fire ricochets using stale `incoming_dx`/`incoming_dy`.
- Inability to issue `OrderType::ReturnToBase`.
- Thief ants remaining frozen at starting coordinates after `InfiltrateAnthill` order.
- `./run_tests.sh --sim` omitting `test_challenger_m2_1` or `test_challenger_m2_2`.
