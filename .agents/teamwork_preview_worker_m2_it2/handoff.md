# Milestone 2 Remediation Handoff Report

**Author**: `worker_m2_it2`  
**Role**: M2 Remediation Worker  
**Working Directory**: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_it2`  
**Target Milestone**: Milestone 2 (`libants-sim`, `tests/test_sim`, `run_tests.sh`)  
**Date**: 2026-09-06  

---

## 1. Observation

1. **Base Queuing Geometry & Lifecycle**:
   - `sim_engine.cpp:819-824` previously returned `{bx + 1, by}` statically, ignoring approach direction and queue occupancy (`from_pos` was marked `[[maybe_unused]]`).
   - `step_base_entry_animation` (`sim_engine.cpp:830-842`) used `if (target_frame >= 8)` without a one-shot gate, pushing Sound 36 (`PowerUpHeal`) to `audio_queue_` on every frame from 8 to 16 (9 audio events per entering unit).
   - `hatch_ant` (`sim_engine.cpp:289-307`) lacked game-over guards, enabling point deduction, egg decrementation, and ant spawning after match time expiration (`GameOver`).

2. **Thief Infiltration, Dynamic Alliances, and Combat AI Boundaries**:
   - `start_thief_infiltration` discarded `target_team_id`, and `step_thief_animation` hardcoded `uint8_t victim = (u->player_id == 0) ? 1 : 0;`, routing Sound 58 and News String 53 to incorrect factions in multiplayer.
   - `match_stats.hpp:189-194` set alliances without breaking former alliances for either party, leaving former allies in asymmetric "zombie" alliances where scores duplicated across multiple alliances.
   - `combat_ai.cpp:203-207` added `kdx * dist_tiles` without boundary checking or obstacle raycasting, teleporting units outside map boundaries (e.g. $x \ge 60$) or into solid obstacles.

3. **Physics Multi-Fire Ricochets & Integration Shortcuts**:
   - `physics.cpp:178-187` failed to update `incoming_dx` and `incoming_dy` across successive ricochets, causing consecutive bounces to reflect using stale initial approach directions.
   - `sim_engine.cpp:887-898` and `sim_engine.hpp:245` contained a synthetic facade `simulate_ballistic_flight(ant_id, t1, t2, t3)` that stepped units across 3 manually hardcoded tiles to bypass genuine physics simulation in Test 7.5.
   - `issue_order` did not handle `OrderType::ReturnToBase` or pathing to target anthills in `OrderType::InfiltrateAnthill`.
   - `tick()` did not autonomously progress base entry, underground healing, food deposit, or thief infiltration.

4. **Test Suites & Runner Integration**:
   - `run_tests.sh` only executed `test_sim_rules`, omitting `test_challenger_m2_1` and `test_challenger_m2_2` from `./run_tests.sh --sim` and `./run_tests.sh --all`.

---

## 2. Logic Chain

1. **Base Lifecycle Remediation**:
   - Based on Observation 1, `assign_queue_slot` was rewritten to search concentric Chebyshev rings $R = 1, 2, \dots, 5$ centered on the anthill $(b_x, b_y)$. Candidates are filtered by `grid.in_bounds`, ground passability (`is_passable(false, false)`), non-reservation, and ant non-occupancy. The optimal candidate minimizes Manhattan distance to `from_pos`, tie-broken by squared Euclidean distance and coordinate ordering. Assigned slots are tracked in `reserved_queue_slots_` and can be inspected or cleared via `release_queue_slot`, `clear_reserved_queue_slots`, and `is_queue_slot_reserved`.
   - In `step_base_entry_animation`, the check was restricted to `if (target_frame == 8)` so Sound 36 triggers strictly once.
   - In `hatch_ant`, `if (is_match_over() || impl_->match_state_ == MatchState::GameOver) return false;` was added to enforce the post-match simulation freeze.

2. **Thief Routing, Alliances, and Combat AI Remediation**:
   - Based on Observation 2, `target_team_id` was added to `AntUnit`. In `start_thief_infiltration(ant_id, target_team_id)`, `u->target_team_id` is set, and `step_thief_animation` routes Sound 58 and News String 53 directly to `u->target_team_id` (with fallback to enemy base if unset).
   - In `MatchStatsManager::set_alliance`, former alliance partners for both `p1` and `p2` are reset to `ALLIANCE_NONE` before establishing the new mutual link, preventing asymmetric score duplication.
   - In `CombatAIController::update_striking`, knockback is raycast step-by-step along the normalized direction vector up to `dist_tiles = 4 + (random_seed & 1)`, halting if an obstacle or boundary is reached, and clamped to `[0, width - 1]` and `[0, height - 1]`.

3. **Physics Multi-Fire Ricochets & Authentic Integration**:
   - Based on Observation 3, in `PhysicsEngine::resolve_fire_contact`, `incoming_dx` and `incoming_dy` are updated upon backward bounce (`b_dx, b_dy`), forward deflection (`-b_dx, -b_dy`), or orthogonal escape (`b_dy, b_dx`).
   - The synthetic `simulate_ballistic_flight` was deleted from `sim_engine.hpp` and `sim_engine.cpp`. Authentic delegation methods `apply_knockback` and `resolve_fire_contact` were exposed on `SimulationEngine`. Tests 7.5 and 3.3 were updated to use genuine physics ricochets.
   - In `SimulationEngine::tick()`, autonomous base entry (food deposit at frame 4, full heal at frame 8, re-emergence at frame 16) and autonomous thief infiltration (alarm at frame 19, loot at frame 32, return home) were implemented. `OrderType::ReturnToBase` and `OrderType::InfiltrateAnthill` were added to `issue_order()`.

4. **Runner & Test Integration**:
   - Based on Observation 4, `run_tests.sh` was updated to execute `test_sim_rules`, `test_challenger_m2_1`, and `test_challenger_m2_2` with exit code checking and master dashboard reporting.

---

## 3. Caveats

- No caveats. All 10 owned files were cleanly updated, no files outside ownership were touched, no synthetic shortcuts remain, and all test suites pass with zero sanitizer warnings or memory leaks.

---

## 4. Conclusion

All 4 remediation areas and 9 specific defects identified across Milestone 2 reviews and explorer blueprints have been fully resolved with genuine, authentic implementations:
- Concentric Chebyshev queuing ($R=1..5$) with reservation lifecycle.
- Single-event Frame 8 underground healing and audio trigger.
- Post-match simulation freeze protection on egg hatching.
- Multi-faction targeted thief alarm and news banner routing.
- Symmetric former-partner dissociation on dynamic alliance shifts.
- Combat Ant knockback obstacle raycasting and map boundary clamping.
- Physics multi-fire ricochet angle updates and elimination of all synthetic flight facades.
- Autonomous base entry, heal/deposit, and infiltration tick progression.
- Test runner integration for all simulation and challenger suites.

All test suites pass 100% under standard build, `./run_tests.sh --sim`, `./run_tests.sh --all`, and `./run_tests.sh --clean --asan`.

---

## 5. Verification Method

To independently verify the implementation:

1. **Build the project**:
   ```bash
   cmake -B build -S .
   cmake --build build -j
   ```
2. **Execute Simulation Tests**:
   ```bash
   ./run_tests.sh --sim
   ```
   *Expected*:
   - `test_sim_rules`: PASSED (62/62 test cases)
   - `test_challenger_m2_1`: PASSED (36/36 test cases)
   - `test_challenger_m2_2`: PASSED (30/30 test cases, including all 5 adversarial defect challenges)
3. **Execute All Test Suites**:
   ```bash
   ./run_tests.sh --all
   ```
   *Expected*:
   - Asset Decoder Tests (`test_assets`): PASSED
   - Simulation Rules Tests (`test_sim_rules`, `test_challenger_m2_1`, `test_challenger_m2_2`): PASSED
   - Opaque-Box E2E Tests (`e2e_runner`): PASSED (506/506 test cases)
4. **Execute Clean ASan/LSan Test Run**:
   ```bash
   ./run_tests.sh --clean --asan
   ```
   *Expected*: Clean execution with 0 memory leaks, 0 undefined behavior, and 0 failures.
5. **Verify Elimination of Synthetic Facades**:
   ```bash
   grep -rn "simulate_ballistic_flight" include/ src/ tests/
   ```
   *Expected*: 0 matches found across the entire repository.
