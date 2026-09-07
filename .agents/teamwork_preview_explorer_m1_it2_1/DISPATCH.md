# Task Assignment: M1 Iteration 2 Explorer 1 - Interface Contract Remediation

## Context & Previous Gate Failure
Milestone 1 Gate resulted in REQUEST_CHANGES due to `LevelData` interface contract non-conformance with `PROJECT.md`:
- `LevelData` in `include/ants_assets/lvl_parser.hpp` lacks the member functions required by `PROJECT.md`:
  * `uint32_t width() const noexcept`
  * `uint32_t height() const noexcept`
  * `uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept`
  * `uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept`
  * `const std::vector<AnthillSpawn>& anthill_spawns() const noexcept`
  * `const std::vector<FoodSchedule>& food_schedules() const noexcept`
- Calling these methods generated 6 compiler errors during review.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Reviewer 1 Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_1/handoff.md`
- Reviewer 2 Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_2/handoff.md`
- Current Code: `include/ants_assets/lvl_parser.hpp`, `src/ants_assets/lvl_parser.cpp`, `tests/test_assets/test_assets.cpp`

## Objective
1. Formulate the exact C++ design and remediation strategy for `LevelData`:
   - Add all 6 required accessor methods with exact signatures matching `PROJECT.md`.
   - Maintain backward compatibility with existing tests or update internal member variables cleanly.
   - Verify that consumer code calling `level.width()`, `level.height()`, `level.layer1_terrain(x, y)`, `level.layer2_item(x, y)`, `level.anthill_spawns()`, `level.food_schedules()` compiles with zero errors.
2. Produce remediation recommendations in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1/plan.md` and handoff report `handoff.md`.

## 2026-09-06T22:56:43Z
You are M1 It2 Explorer 1 for Milestone 1 remediation.
Your identity: explorer_m1_it2_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_2/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/lvl_parser.hpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/lvl_parser.cpp

Your mission:
Formulate exact C++ remediation diffs and implementation instructions for LevelData in include/ants_assets/lvl_parser.hpp and src/ants_assets/lvl_parser.cpp:
1. Implement the required public accessor methods with exact signatures matching PROJECT.md:
   - uint32_t width() const noexcept
   - uint32_t height() const noexcept
   - uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept
   - uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept
   - const std::vector<AnthillSpawn>& anthill_spawns() const noexcept
   - const std::vector<FoodSchedule>& food_schedules() const noexcept
2. Provide exact refactored header and implementation code.
3. Write your findings to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1/plan.md and handoff.md, then send a completion message to your parent.
