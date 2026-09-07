# Task Assignment: Milestone 1 Iteration 2 Worker (`libants-assets` Remediation)

## Scope & Mandate
You are the M1 Iteration 2 Worker. Apply the ready, verified remediation patches from Iteration 2 Explorers to achieve 100% gate compliance for Milestone 1 (`libants-assets`).

## Inputs & Ready Patches
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Explorer 1 Remediation Diff (LevelData accessors):
  `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1/remediation.diff`
- Explorer 2 Proposed Source Files (Input-bounded allocation & CWE-789 fixes):
  `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/proposed_lvl_parser.cpp`
  `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/proposed_chd_parser.cpp`
- Explorer 3 Cleanliness Migration, Test Runner & Mirroring Patch:
  `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3/proposed_run_tests.sh`
  `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3/mirroring.patch`

## File Ownership
You exclusively own and will modify/create:
- `/Users/dchadd/Desktop/Ants-Mac/include/ants_assets/lvl_parser.hpp`
- `/Users/dchadd/Desktop/Ants-Mac/src/ants_assets/lvl_parser.cpp`
- `/Users/dchadd/Desktop/Ants-Mac/src/ants_assets/chd_parser.cpp`
- `/Users/dchadd/Desktop/Ants-Mac/include/ants_assets/mirroring.hpp`
- `/Users/dchadd/Desktop/Ants-Mac/src/ants_assets/mirroring.cpp`
- `/Users/dchadd/Desktop/Ants-Mac/tests/test_assets/test_assets.cpp`
- `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_INFRA.md` (moved from root)
- `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_READY.md` (moved from root)
- `/Users/dchadd/Desktop/Ants-Mac/run_tests.sh` (executable test runner in root)

## Tasks
1. Update `include/ants_assets/lvl_parser.hpp`:
   Implement all 6 required public accessor methods with exact signatures matching `PROJECT.md`:
   - `uint32_t width() const noexcept`
   - `uint32_t height() const noexcept`
   - `uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept`
   - `uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept`
   - `const std::vector<AnthillSpawn>& anthill_spawns() const noexcept`
   - `const std::vector<FoodSchedule>& food_schedules() const noexcept`
   Keep companion cell accessors `get_cell_layer1()`, `get_cell_layer2()`, `layer1_cells()`, `layer2_cells()`.
2. Update `src/ants_assets/lvl_parser.cpp`:
   - Add dimension bounds check: `width > 256 || height > 256` returns `false`.
   - Add capacity check: `r.remaining() < cell_count * 12` returns `false` before `layer1_terrain.resize(cell_count)`.
   - Wrap in `try/catch` returning `false` on allocation errors.
3. Update `src/ants_assets/chd_parser.cpp`:
   - Verify `size >= 28` header length check.
   - Enforce stream capacity validation before allocating sprite pixel buffers, PCM sound buffers, and animation subitems.
4. Update `include/ants_assets/mirroring.hpp` and `src/ants_assets/mirroring.cpp`:
   - In `mirror_bounding_box`, use local temporaries to prevent in-place reference aliasing bugs.
   - In `mirror_pixel_buffer`, handle `src == dst` in-place reflection by swapping pixels across `x < width / 2`.
5. Update `tests/test_assets/test_assets.cpp`:
   - Ensure all tests compile and pass using the updated `LevelData` accessors.
   - Add regression tests for `mirror_bounding_box` aliasing, in-place pixel mirroring, and malformed dimension bounds rejection.
6. Execute Root Cleanliness Directive:
   - Move `/Users/dchadd/Desktop/Ants-Mac/TEST_INFRA.md` into `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_INFRA.md` (remove root file).
   - Move `/Users/dchadd/Desktop/Ants-Mac/TEST_READY.md` into `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_READY.md` (remove root file).
   - Create `/Users/dchadd/Desktop/Ants-Mac/run_tests.sh` from `proposed_run_tests.sh` and make it executable (`chmod +x /Users/dchadd/Desktop/Ants-Mac/run_tests.sh`).
7. Build and verify:
   - Run `./run_tests.sh`
   - Run `./build_asan/tests/test_assets/test_assets`
   - Verify 0 failures, 0 warnings, 0 leaks, and project root contains no leftover test docs/logs.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Deliver your handoff report to:
`/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_it2/handoff.md`
and send a completion message to your parent.

## 2026-09-06T23:01:33Z
You are the M1 Iteration 2 Worker for the Microsoft Ants remake project.
Your identity: worker_m1_it2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_it2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

