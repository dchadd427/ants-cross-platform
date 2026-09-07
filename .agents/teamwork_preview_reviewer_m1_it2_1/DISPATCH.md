# Task Assignment: M1 Iteration 2 Reviewer 1

## Objective
Re-verify Milestone 1 (`libants-assets`) after Iteration 2 remediation:
1. Verify `LevelData` interface contract in `include/ants_assets/lvl_parser.hpp`:
   - `width() -> uint32_t`
   - `height() -> uint32_t`
   - `layer1_terrain(uint32_t x, uint32_t y) -> uint16_t`
   - `layer2_item(uint32_t x, uint32_t y) -> uint16_t`
   - `anthill_spawns() -> const std::vector<AnthillSpawn>&`
   - `food_schedules() -> const std::vector<FoodSchedule>&`
2. Verify master test runner `./run_tests.sh --all` and AddressSanitizer `./run_tests.sh --asan`.
3. Verify project root cleanliness: `TEST_INFRA.md` and `TEST_READY.md` are now in `tests/`, project root is clean.
4. Render your verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_it2_1/handoff.md`.

## 2026-09-06T23:08:36Z
You are M1 It2 Reviewer 1 for Milestone 1 (ants-assets).
Your identity: reviewer_m1_it2_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_it2_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_it2_1/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_it2/handoff.md

Your mission:
1. Re-verify LevelData interface contract in include/ants_assets/lvl_parser.hpp:
   - width(), height(), layer1_terrain(x, y), layer2_item(x, y), anthill_spawns(), food_schedules().
2. Re-verify root cleanliness (TEST_INFRA.md and TEST_READY.md moved to tests/).
3. Execute ./run_tests.sh --all and ./run_tests.sh --asan.
4. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_it2_1/handoff.md
   and send a completion message to your parent.
