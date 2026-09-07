# BRIEFING — 2026-09-06T22:37:30Z

## Mission
Extract and document the exact, complete specification for Simulation Rules (11 key areas) for the Ants remake.

## 🔒 My Identity
- Archetype: Specification Miner
- Roles: Teamwork specialist, Specification Miner
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: Simulation Rules Survey (survey_sim.md)

## 🔒 Key Constraints
- Authoritative reference: GAME_REVERSE_ENGINEERING.md, ORIGINAL_REQUEST.md, Original-Ants binary/assets
- Read-only: Do NOT implement anything
- Report findings in required tables: Features Discovered & Edge Cases
- Thorough but organized: cover all 11 assigned areas and any discovered features
- Write findings to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md
- Write completion handoff report to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/handoff.md
- Communicate results via send_message to parent (a28dfa55-5a82-453d-a21b-99459a66b340)

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: 2026-09-06T22:37:30Z

## Task Summary
- **What to build**: Comprehensive simulation rules specification document (survey_sim.md) and handoff report (handoff.md)
- **Status**: COMPLETE. All 11 simulation areas fully extracted and verified against disassembly and binary assets.
- **Interface contracts**: /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md
- **Code layout**: .agents/teamwork_preview_spec_miner_survey_2/

## Loaded Skills
- None required for pure spec extraction

## Key Decisions Made
- Prioritize GAME_REVERSE_ENGINEERING.md disassembly addresses, offsets, constants, and reverse-engineered C++ class layouts as authoritative ground truth.
- Verified MSVC LCG PRNG algorithm (`holdrand * 214013 + 2531011`) from `Ants.exe` disassembly at `0x10345b0`/`0x10345c0` and `latseed:` parsing at `0x100c98c`.
- Incorporated Sentinel bridge clarification (universal traversal by any ant; 180s collapse causes instant drowning for non-swimmers).
- Mapped all 40 discovered features and 24 edge cases in standard table formats in `survey_sim.md`.

## Artifact Index
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md — Comprehensive simulation specification document (40 features, 24 edge cases)
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/handoff.md — 5-component handoff report
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/progress.md — Progress heartbeat
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/BRIEFING.md — Situational awareness
