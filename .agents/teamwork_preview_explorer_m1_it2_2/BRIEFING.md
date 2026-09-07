# BRIEFING — 2026-09-06T23:00:00Z

## Mission
Investigate input-bounded allocation vulnerabilities (CWE-789) in lvl_parser.cpp and chd_parser.cpp and formulate exact C++ remediation diffs and verification instructions.

## 🔒 My Identity
- Archetype: explorer
- Roles: investigation, synthesis
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: Milestone 1 Remediation (Iteration 2)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement directly in source files
- Formulate exact C++ remediation diffs and implementation instructions for input-bounded allocation and CWE-789 mitigation in lvl_parser.cpp and chd_parser.cpp
- Write findings to plan.md and handoff.md, notify parent via send_message

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: not yet

## Investigation State
- **Explored paths**: `src/ants_assets/lvl_parser.cpp`, `src/ants_assets/chd_parser.cpp`, `include/ants_assets/lvl_parser.hpp`, `include/ants_assets/chd_parser.hpp`, `tests/test_assets/test_challenger_m1_2.cpp`, `tests/test_assets/test_adversarial.cpp`, `ORIGINAL_REQUEST.md`, `PROJECT.md`, Challenger 1 & 2 handoffs.
- **Key findings**:
  1. `lvl_parser.cpp`: `layer1_terrain.resize(cell_count)` executes without validating `r.remaining() >= cell_count * 12` and without dimension upper bounds (`width <= 256 && height <= 256`), causing CWE-789 memory exhaustion / ASan aborts.
  2. `chd_parser.cpp`: `sp.pixels.resize(pixel_bytes)`, `snd.pcm_data.resize(pcm_len)`, and animation subitems/frames allocate without stream capacity checks.
  3. Header size guard `size >= 28` and Table 4 offset guard verified.
  4. Formulated exact unified diff patches (`lvl_parser.patch`, `chd_parser.patch`) and verified with `patch --dry-run` and `clang++ -fsyntax-only`.
- **Unexplored areas**: None within scope. Investigation complete.

## Key Decisions Made
- Enforced dimension bounds: `max(width, height) <= 256` in `LVLParser`.
- Enforced stream capacity check: `r.remaining() < cell_count * 12` returns `false` before any terrain layer resize.
- Enforced capacity checks for sprite pixels (`pitch * height > r.remaining()`), PCM sounds (`pcm_len > r.remaining()`), and animations (`subitem_count * 40 > r.remaining()`).
- Wrapped all deserializer routines in fail-safe `try / catch` returning `false` and clearing outputs on memory allocation errors.
- Created standalone patch files (`lvl_parser.patch`, `chd_parser.patch`) and full replacement files (`proposed_lvl_parser.cpp`, `proposed_chd_parser.cpp`).

## Artifact Index
- `plan.md` — Detailed remediation plan, specifications, before/after snippets, and implementer guide
- `handoff.md` — 5-component handoff report
- `lvl_parser.patch` — Unified diff patch for `lvl_parser.cpp`
- `chd_parser.patch` — Unified diff patch for `chd_parser.cpp`
- `proposed_lvl_parser.cpp` — Complete verified replacement file for `lvl_parser.cpp`
- `proposed_chd_parser.cpp` — Complete verified replacement file for `chd_parser.cpp`
- `progress.md` — Progress status and heartbeat
- `DISPATCH.md` — Task assignment and input records
