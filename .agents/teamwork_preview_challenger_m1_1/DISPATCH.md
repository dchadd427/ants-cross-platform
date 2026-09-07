# Task Assignment: M1 Challenger 1

## Objective
Empirically stress-test and challenge Milestone 1 (`libants-assets`) via adversarial test harnesses, edge cases, and fuzzing.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Worker Handoff: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md`
- Codebase under test: `include/ants_assets/`, `src/ants_assets/`, `Original-Ants/`

## Challenge Scope
1. Adversarially stress-test `ants.chd` parsing:
   - Fuzz corrupted headers (invalid version, truncated offsets, negative table lengths).
   - Test out-of-bounds sprite lookups, sound clip lookups, animation lookups.
   - Validate that all 2,794 sprites produce valid 32-bit RGBA buffers without crashes or unaligned reads.
   - Verify that all 91 RIFF WAV buffers start with `"RIFF"`, have valid chunk sizes, and can be read by standard audio decoders.
2. Compile and run your adversarial tests against `libants_assets.a`.
3. Render verdict: `APPROVE` or `REQUEST_CHANGES` in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1/handoff.md`.

## 2026-09-06T22:50:00Z
Received dispatch from parent:
You are M1 Challenger 1 for Milestone 1 (ants-assets).
Your identity: challenger_m1_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md

Your mission:
1. Empirically stress-test and challenge libants-assets:
   - Fuzz corrupted headers, invalid offsets, and buffer limits.
   - Test out-of-bounds lookups for sprites, sounds, and animations.
   - Verify that all 2,794 sprites produce valid 32-bit RGBA buffers without crashes or unaligned reads.
   - Verify that all 91 RIFF WAV buffers start with "RIFF" and have valid chunk sizes.
2. Compile and execute your adversarial test harness against libants_assets.a.
3. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_1/handoff.md
   and send a completion message to your parent.
