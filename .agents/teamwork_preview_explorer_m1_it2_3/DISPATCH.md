# Task Assignment: M1 Iteration 2 Explorer 3 - Cleanliness Directive, Aliasing & Root Test Runner

## Context & User Directive
1. Sentinel relayed a user cleanliness directive:
   - Root directory must be kept clean: move test documents (`TEST_INFRA.md`, `TEST_READY.md`), test logs, and test artifacts into `tests/` or `.agents/`.
   - Provide a clean, single-command runner script in project root `./run_tests.sh` that executes all test suites (`test_assets` and `e2e_runner`) with formatted output.
2. Reviewers and Challengers identified minor edge-case improvements:
   - Argument aliasing in `mirror_bounding_box`: use local temporaries.
   - In-place mirroring in `mirror_pixel_buffer`: handle `src == dst` by swapping pixels.

## Inputs
- Verbatim User Request & Directives: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Reviewer 1 & 2 Handoffs: aliasing and in-place mirroring findings
- Current repository layout: `/Users/dchadd/Desktop/Ants-Mac/`

## Objective
1. Formulate exact file migration plan to clean project root:
   - Move `/Users/dchadd/Desktop/Ants-Mac/TEST_INFRA.md` -> `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_INFRA.md`
   - Move `/Users/dchadd/Desktop/Ants-Mac/TEST_READY.md` -> `/Users/dchadd/Desktop/Ants-Mac/tests/TEST_READY.md`
2. Formulate the design and contents of `./run_tests.sh`:
   - Executable bash script at project root.
   - Builds project via cmake if not built, runs `./build/tests/test_assets/test_assets` and `./build_e2e/e2e_runner --all`.
   - Returns exit code 0 if all tests pass, non-zero on failure, printing clean formatted summary.
3. Formulate code diffs for `mirror_bounding_box` and `mirror_pixel_buffer`.
4. Produce plan in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3/plan.md` and handoff report `handoff.md`.

## 2026-09-06T22:56:43Z
<USER_REQUEST>
You are M1 It2 Explorer 3 for Milestone 1 remediation.
Your identity: explorer_m1_it2_3
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- User Cleanliness Directive in DISPATCH.md:
  * Move TEST_INFRA.md and TEST_READY.md from project root into tests/ to keep root clean.
  * Provide a root executable script ./run_tests.sh that builds and executes all tests (test_assets and e2e_runner) with clean formatted output.
- Reviewer findings on mirroring.hpp (local temporaries for mirror_bounding_box) and mirroring.cpp (in-place src == dst swap handling).

Your mission:
Formulate exact file migration steps and scripts:
1. Plan moving TEST_INFRA.md and TEST_READY.md to tests/ (updating any references).
2. Design the complete, production-grade root script ./run_tests.sh (executable, tests build, runs test_assets and e2e_runner, formats output, returns exit code).
3. Provide exact code diffs for mirror_bounding_box and mirror_pixel_buffer.
4. Write your findings to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_3/plan.md and handoff.md, then send a completion message to your parent.
</USER_REQUEST>
