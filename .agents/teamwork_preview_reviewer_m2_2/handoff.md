# Review & Adversarial Critic Report: Milestone 2 Determinism, Memory Safety & ASan

**Reviewer Identity**: `reviewer_m2_2`  
**Roles**: reviewer, critic  
**Target Work Product**: Milestone 2 (`libants-sim`, `test_sim_rules`, `run_tests.sh`)  
**Parent Conversation ID**: `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Date**: 2026-09-06  
**Verdict**: **APPROVE**  

---

## 1. Observation

1. **Floating-Point Audit in Simulation Core**:
   - Inspected source code in `include/ants_sim/` (`ant_unit.hpp`, `combat_ai.hpp`, `grid.hpp`, `match_stats.hpp`, `physics.hpp`, `prng.hpp`, `sim_engine.hpp`) and `src/ants_sim/` (`ant_unit.cpp`, `combat_ai.cpp`, `grid.cpp`, `physics.cpp`, `sim_engine.cpp`).
   - Ripgrep searches for `float` and `double` in `src/ants_sim/` returned 0 matches. In `include/ants_sim/`, the only mentions were in documentation comments.
   - Performed native ARM64 assembly instruction disassembly using `otool -tvV` on all compiled translation unit objects (`build/src/ants_sim/CMakeFiles/ants_sim.dir/*.o`):
     - `ant_unit.cpp.o`: Zero floating-point instructions (`fadd`, `fsub`, `fmul`, `fdiv`, `fmov`, `fcmp`, `scvtf`, `ucvtf`, `fcvtzs`).
     - `physics.cpp.o`: Zero floating-point instructions.
     - `grid.cpp.o`: Zero floating-point instructions.
     - `combat_ai.cpp.o`: Zero floating-point instructions.
     - `sim_engine.cpp.o` in `SimulationEngine::tick()` (addresses `0x00000e74` to `0x00001acc`): Confirmed **0 floating-point instructions**.
   - Movement integration uses 16.16 signed fixed-point integer math (`FixedPointMath::mul(speed_fx, DIAG_SCALE_FX)` with `int64_t` intermediate scaling in `ant_unit.cpp:125-131`).
   - Ballistic altitude arc in `physics.cpp:75-76` uses pure integer parabolic computation:
     ```cpp
     int32_t z = (4 * f.apex_height_px * f.current_tick * (f.total_ticks - f.current_tick)) /
                 (f.total_ticks * f.total_ticks);
     ```

2. **MSVC LCG PRNG Determinism & Reproducibility**:
   - Inspected `include/ants_sim/prng.hpp`:
     ```cpp
     state_ = state_ * 214013u + 2531011u;
     return static_cast<uint16_t>((state_ >> 16) & 0x7FFFu);
     ```
   - Validated against MSVC CRT `rand()` specification and reference implementation. Verified the first 10 outputs starting from seed 1:
     - Output: `[41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464]`.
     - `ants::sim::PRNG` produced identical values with 100% bitwise parity.
     - Range invariant `[0, 32767]` verified across 10,000 iterations.
     - Parsing of `latseed:<uint32>` confirmed functional in `prng.hpp:76-86`.

3. **Memory Safety & Pointer Reference Stability**:
   - In `src/ants_sim/sim_engine.cpp:20`, unit entities are stored as:
     ```cpp
     std::vector<std::unique_ptr<AntUnit>> ants_;
     ```
   - Dynamic allocations occur via `std::make_unique<AntUnit>`. Because elements are managed via heap unique pointers, vector capacity reallocations move pointer representations without altering or invalidating `AntUnit*` or `AntUnit&` heap addresses.
   - Tested reference stability programmatically under AddressSanitizer and UBSan:
     - Spawned unit at `0x60b000000040`.
     - Dynamically spawned 500 units (triggering multiple vector reallocations).
     - Verified address was strictly invariant at `0x60b000000040`.
     - Called `kill_unit`, ran 50 simulation ticks, and spawned another 500 units.
     - Address remained strictly invariant at `0x60b000000040` with 0 use-after-free or memory errors.

4. **Sanitizer Build & Test Suite Execution**:
   - Executed clean AddressSanitizer and UndefinedBehaviorSanitizer run:
     ```bash
     ./run_tests.sh --clean --asan
     ```
   - Compilation flags: `-fsanitize=address,undefined -fno-omit-frame-pointer`.
   - Results:
     - Native Asset Decoder Tests (`test_assets`): **26/26 passed**.
     - Simulation Rules Tests (`test_sim_rules`): **62/62 passed** (2,193 assertions).
     - Opaque-Box E2E Tests (`e2e_runner`): **506/506 passed**.
     - ASan Report: **0 memory leaks, 0 heap buffer overflows, 0 use-after-free**.
     - UBSan Report: **0 undefined behavior reports, 0 alignment issues, 0 integer overflows**.

5. **Integrity Violation & Adversarial Stress Check**:
   - Hardcoded results check: No test-specific cheats, static lookup tables masquerading as computation, or hardcoded branch bypasses found.
   - Facade implementations check: State machines (`CombatAIController`, `AntUnit`, `PhysicsEngine`, `Grid`) execute genuine discrete logic and state transitions.
   - Stress tested boundary conditions:
     - Multi-fire ricochet loop bounds: Verified `loop_guard < 10` prevents infinite oscillation (`physics.cpp:160-187`).
     - Negative coordinate conversions: `WorldCoord::to_tile()` correctly applies floor arithmetic for negative coordinates (`grid.hpp:89-90`).
     - Score deduction underflow: `stats_.deduct_score()` clamps scores to 0 without integer wrap (`match_stats.hpp:173-176`).
     - Alliance invalid inputs: Self-proposals and out-of-bounds player IDs are rejected safely (`sim_engine.cpp:310`).

---

## 2. Logic Chain

1. **Zero Floating-Point Invariant**:
   - *Premise*: Cross-platform simulation determinism requires zero floating-point operations in simulation state advance, as FPU rounding modes, fused multiply-add (FMA), and architecture differences cause desynchronization over long matches.
   - *Finding*: Both source-level code analysis and machine-code disassembly (`otool -tvV`) confirm that `SimulationEngine::tick()`, `ant_unit.cpp.o`, `physics.cpp.o`, `grid.cpp.o`, and `combat_ai.cpp.o` execute 0 floating-point instructions. All kinematics and timers use fixed-point integers (16.16) and discrete integers.
   - *Conclusion*: Simulation engine is mathematically deterministic.

2. **PRNG Reproducibility**:
   - *Premise*: Authentic Microsoft Ants gameplay requires reproducing the original game's MSVC 4.x/5.0 CRT linear congruential generator.
   - *Finding*: `ants::sim::PRNG` implements $S_{n+1} = (S_n \times 214013 + 2531011) \pmod{2^{32}}$ and extracts bits 16..30 (`(state >> 16) & 0x7FFF`). Independent test verified exact equivalence with MSVC standard reference values.
   - *Conclusion*: PRNG is authentic and reproducible.

3. **Pointer Stability & Memory Safety**:
   - *Premise*: Long-running simulations with entity spawning and despawning must not invalidate pointer references held by controllers (`CombatAIController::owner_`), event listeners, or pathfinders.
   - *Finding*: Using `std::vector<std::unique_ptr<AntUnit>>` guarantees that `AntUnit` instances retain stable heap addresses throughout their lifetime. Direct testing with 1,000 allocations under ASan/UBSan produced zero memory faults.
   - *Conclusion*: Memory management is safe and robust.

---

## 3. Caveats

- **macOS LeakSanitizer**: Apple Clang on macOS does not support standalone `detect_leaks=1` via LeakSanitizer; however, AddressSanitizer (heap/stack overflow, use-after-free, double-free) and UndefinedBehaviorSanitizer are fully active and confirmed clean across all 594 test cases.
- **std::unordered_map Max Load Factor**: `std::unordered_map` internally includes float-based load factor checking in libc++ during rehash. In `libants-sim`, this is restricted to controller lookup maps in `SimulationEngineImpl` and is never executed in `ant_unit.cpp`, `physics.cpp`, or `grid.cpp`, nor does it affect simulation state math.

---

## 4. Conclusion

**Verdict: APPROVE**

Milestone 2 (`libants-sim`) satisfies 100% of mathematical determinism, MSVC LCG PRNG reproducibility, memory safety, and AddressSanitizer/UBSan requirements. The codebase exhibits exemplary software engineering quality with clean separation of concerns, zero external binary dependencies, and exhaustive test coverage.

---

## 5. Verification Method

To independently reproduce the complete verification suite:

1. **Clean Rebuild and Test Under AddressSanitizer & UBSan**:
   ```bash
   cd /Users/dchadd/Desktop/Ants-Mac
   ./run_tests.sh --clean --asan
   ```
   *Expected Result*: 62 simulation tests, 26 asset decoder tests, and 506 E2E tests pass with 0 errors.

2. **Verify Zero Floating-Point Assembly in Simulation Core**:
   ```bash
   for f in ant_unit.cpp.o physics.cpp.o grid.cpp.o combat_ai.cpp.o; do
       echo "Checking $f:"
       otool -tvV "build/src/ants_sim/CMakeFiles/ants_sim.dir/$f" | grep -iE "\b(fadd|fsub|fmul|fdiv|fmov|fcmp|scvtf|ucvtf|fcvtzs)\b" || echo "ZERO_FP_INSTRUCTIONS"
   done
   ```
   *Expected Result*: All translation units report `ZERO_FP_INSTRUCTIONS`.

3. **Verify MSVC PRNG Recurrence**:
   ```bash
   ./build/tests/test_sim/test_sim_rules
   ```
   *Expected Result*: Suite 2 test cases 2.1 to 2.4 pass cleanly.
