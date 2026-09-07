# Task Assignment: M1 Challenger 2

## Objective
Empirically challenge the 5-to-8 directional mirroring engine and level decoder across boundary and corner cases.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Worker Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md`
- Codebase under test: `include/ants_assets/`, `src/ants_assets/`, `Original-Ants/`

## Challenge Scope
1. Verify 5-to-8 Directional Mirroring Invariants:
   - Check all 8 compass headings (0..7) and ensure mathematical involution $dx'' = dx$ and $p''(x, y) = p(x, y)$.
   - Verify that mirrored sprites 8, 9, 2 (NW, W, SW) properly align visually and have inverted horizontal offsets $dx' = -(dx+W)$.
   - Verify lunchbox carrying sprites mapping for all directions.
2. Stress-test Level Parser:
   - Verify all 6 maps (`TINY`, `SMALL`, `MEDIUM`, `GAUNTLET`, `ISLANDS`, `TREASURE`) parse with exactly 0 remaining bytes.
   - Verify that coordinates in trailing blocks 1, 2, 4 map validly to the grid bounds without integer overflow.
   - Test corrupted map buffers (e.g. truncated tile dictionary, truncated trailing blocks) for graceful error handling without segfaults.
3. Compile and run your challenge test harness.
4. Render verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2/handoff.md`.

## 2026-09-06T22:50:00Z
You are M1 Challenger 2 for Milestone 1 (ants-assets).
Your identity: challenger_m1_2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md

Your mission:
1. Empirically stress-test the 5-to-8 directional mirroring engine and level decoder across boundaries:
   - Check all 8 compass facings, involution (double mirror yields original), offset math dx'=-(dx+W), and lunchbox suite.
   - Verify all 6 maps (TINY, SMALL, MEDIUM, GAUNTLET, ISLANDS, TREASURE) decode with exactly 0 remaining bytes.
   - Verify coordinate serialization in trailing blocks 1, 2, 4.
   - Test corrupted map buffers for graceful error rejection without crashes.
2. Compile and execute your adversarial test harness against libants_assets.a.
3. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2/handoff.md
   and send a completion message to your parent.
