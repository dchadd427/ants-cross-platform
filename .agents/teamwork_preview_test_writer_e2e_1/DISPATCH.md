# Task Assignment: E2E Testing Track Orchestration & Test Suite Creation

## Scope & Mandate
You are the E2E Test Writer for the Microsoft Ants remake project.
Your mission is to design and write a comprehensive, requirement-driven, opaque-box test suite adhering to the Project Pattern Dual Track specifications.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Project Definition & Feature Inventory: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Reference Specifications: `/Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md`
- Directives in DISPATCH history: Bridge universal traversal & 180s collapse drowning, Water splash (`dsplash`) & 22-subitem ant drowning (`agdr301`, `afdr301`, `abdr301`, `acdr301`, `atdr301`, Sounds 71/72, bubbles).

## Deliverables
1. Design Test Architecture & Write `/Users/dchadd/Desktop/Ants-Mac/TEST_INFRA.md` at project root according to the template in Project Pattern:
   - Test philosophy, methodology (Category-Partition, BVA, Pairwise, Workload), feature inventory mapping, runner commands.
2. Implement Comprehensive Opaque-Box Test Suite in `/Users/dchadd/Desktop/Ants-Mac/tests/e2e/`:
   - Tier 1: Feature Coverage (>=5 test cases per feature across all inventoried features)
   - Tier 2: Boundary & Corner Cases (>=5 test cases per feature covering boundaries, limits, invalid inputs)
   - Tier 3: Cross-Feature Combinations (pairwise interactions: combat knockback into fire, bridge collapse with friendly/enemy non-swimmers drowning while swimmer survives, thief infiltration dive -> Sound 58 alarm -> 50 pt drain -> lunchbox drop -> universal pickup)
   - Tier 4: Real-World Scenarios (complete end-to-end match flow, 4:3 presentation, 0:00 simulation freeze, winner/loser audio routing, 4-stat scorecard tracking)
3. Ensure tests compile with CMake / C++17 test runner executable `tests/e2e/e2e_runner` or standalone verification script.
4. When test suite is complete and passing criteria are defined, write `/Users/dchadd/Desktop/Ants-Mac/TEST_READY.md` at project root with test runner command and coverage summary.
5. Provide completion report in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_test_writer_e2e_1/handoff.md`.

## 2026-09-06T22:39:01Z
You are the E2E Test Writer for the Microsoft Ants remake project.
Your identity: test_writer_e2e_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_test_writer_e2e_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_test_writer_e2e_1/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

Your mission:
1. Write /Users/dchadd/Desktop/Ants-Mac/TEST_INFRA.md at project root per the Project Pattern E2E Testing Track template:
   - Test philosophy (opaque-box, requirement-driven)
   - Methodology (Category-Partition, BVA, Pairwise, Real-World Workloads)
   - Feature inventory coverage mapping across all features
   - Coverage thresholds (Tier 1 >= 5/feat, Tier 2 >= 5/feat, Tier 3 pairwise, Tier 4 scenarios)
   - Test architecture and runner invocation command
2. Write and implement the comprehensive opaque-box test suites in /Users/dchadd/Desktop/Ants-Mac/tests/e2e/ covering:
   - Tier 1: Feature Coverage (>=5 test cases per feature for asset decoding, simulation rules, and app behavior)
   - Tier 2: Boundary & Corner Cases (empty/max maps, bounds, 180s limits, invalid placement rejection)
   - Tier 3: Cross-Feature Combinations (combat knockback into fire, bridge universal traversal and 180s collapse drowning non-swimmers while swimmer survives, thief dive alarm Sound 58 + score deduction + death lunchbox drop + universal pickup)
   - Tier 4: Real-World Scenarios (complete match flow, simulation freeze at 0:00, winner fanfare Sound 56 vs loser sting Sound 41, 4-stat scorecard tracking)
3. Ensure test runner compiles/runs via CMake or standalone verification harness.
4. When complete, publish /Users/dchadd/Desktop/Ants-Mac/TEST_READY.md with runner command and coverage metrics.
5. Write your handoff report to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_test_writer_e2e_1/handoff.md and send a completion message to your parent.

