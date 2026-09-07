# Task Assignment: M1 Iteration 2 Explorer 2 - Robustness & Input-Bounded Allocations

## Context & Previous Gate Failure
Milestone 1 Gate identified CWE-789 uncontrolled memory allocation vulnerabilities:
- In `src/ants_assets/lvl_parser.cpp:160`, `layer1_terrain.resize(cell_count)` is called immediately after reading dimensions without checking if `r.remaining() >= cell_count * 12`. Fuzzed dimensions triggered `std::bad_alloc` crashes / ASan aborts.
- In `src/ants_assets/chd_parser.cpp`, ensure similar allocation-before-validation checks are added for sprite pixel buffers, PCM audio samples, and animation subitems.
- In `chd_parser.cpp:207`, verify `size >= 28` header size guard.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Challenger 1 Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1/handoff.md`
- Challenger 2 Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2/handoff.md`
- Current Code: `src/ants_assets/lvl_parser.cpp`, `src/ants_assets/chd_parser.cpp`

## Objective
1. Formulate defensive input validation and bounded allocation strategy:
   - Dimension sanity bounds in `LVLParser`: max width/height <= 256 (official maps are 31, 40, 60).
   - Capacity check: `r.remaining() < cell_count * 12` returns `false`.
   - Wrap vector resizing in `try / catch` blocks to gracefully return `false` on allocation errors.
   - Header guard: verify `size >= 28` in `chd_parser.cpp`.
   - Stream validation before resizing sprite pixels and PCM sound buffers.
2. Produce remediation recommendations in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/plan.md` and handoff report `handoff.md`.

## 2026-09-06T22:56:43Z
You are M1 It2 Explorer 2 for Milestone 1 remediation.
Your identity: explorer_m1_it2_2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/lvl_parser.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/chd_parser.cpp

Your mission:
Formulate exact C++ remediation diffs and implementation instructions for input-bounded allocation and CWE-789 mitigation:
1. In src/ants_assets/lvl_parser.cpp:
   - Validate dimension bounds: max width/height <= 256.
   - Stream validation: r.remaining() < cell_count * 12 returns false before any vector resize.
   - Wrap in try / catch blocks to return false on any memory allocation error.
2. In src/ants_assets/chd_parser.cpp:
   - Enforce stream capacity checks before allocating sprite pixel buffers, PCM sound buffers, and animation subitems.
   - Ensure header size check size >= 28.
3. Write your findings to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/plan.md and handoff.md, then send a completion message to your parent.
