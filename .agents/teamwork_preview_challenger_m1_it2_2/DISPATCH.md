# Task Assignment: M1 Iteration 2 Challenger 2

## Objective
Empirically stress-test the mirroring aliasing fix, in-place reflection, and `LevelData` interface contract accessors:
1. Verify `mirror_bounding_box(l, r, l, r)` does not corrupt coordinates under in-place argument aliasing.
2. Verify in-place pixel reflection `mirror_pixel_buffer` when `src == dst` produces identical results to out-of-place reflection across odd and even widths.
3. Verify that code using `level.width()`, `level.height()`, `level.layer1_terrain(x, y)`, `level.layer2_item(x, y)`, `level.anthill_spawns()`, `level.food_schedules()` compiles and executes with 100% accuracy.
4. Run all tests via `./run_tests.sh --all` and `./run_tests.sh --asan`.
5. Render your verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_2/handoff.md`.

## 2026-09-06T23:08:36Z
You are M1 It2 Challenger 2 for Milestone 1 (ants-assets).
Your identity: challenger_m1_it2_2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_2/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_it2/handoff.md

Your mission:
1. Empirically verify mirroring aliasing fix, in-place reflection, and LevelData contract:
   - Verify mirror_bounding_box(l, r, l, r) with aliased args.
   - Verify in-place mirror_pixel_buffer (src == dst).
   - Verify level.width(), level.height(), level.layer1_terrain(x,y), level.layer2_item(x,y), level.anthill_spawns(), level.food_schedules().
2. Run test suites via ./run_tests.sh --all and ./run_tests.sh --asan.
3. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_2/handoff.md
   and send a completion message to your parent.

