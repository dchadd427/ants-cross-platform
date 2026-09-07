# Dispatch Assignment: explorer_m2_it2_2

**Identity**: explorer_m2_it2_2 (M2 It2 Thief, Alliances & AI Knockback Remediation Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_2  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_2/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/sim_engine.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/combat_ai.cpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/match_stats.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/ant_unit.hpp

## Objectives
Formulate exact C++ remediation diffs and implementation code for:
1. Thief Infiltration Targeted Victim Alarm & News Routing:
   - In `AntUnit`, add field `uint8_t target_team_id{4};` or record it in engine state.
   - In `start_thief_infiltration(uint32_t ant_id, uint8_t target_team_id)`, store `target_team_id`.
   - In `step_thief_animation(uint32_t ant_id, uint16_t target_frame)`, remove hardcoded `uint8_t victim = (u->player_id == 0) ? 1 : 0;` and dispatch Sound 58 and News String 53 directly to `u->target_team_id`.
2. Dynamic Alliance Asymmetric Desynchronization:
   - In `include/ants_sim/match_stats.hpp:189`, in `set_alliance(p1, p2)`:
     - If `alliances_[p1] != ALLIANCE_NONE && alliances_[p1] != p2`, break alliance for former partner `alliances_[alliances_[p1]] = ALLIANCE_NONE`.
     - If `alliances_[p2] != ALLIANCE_NONE && alliances_[p2] != p1`, break alliance for former partner `alliances_[alliances_[p2]] = ALLIANCE_NONE`.
     - Then set `alliances_[p1] = p2; alliances_[p2] = p1;`.
3. Combat Ant AI Knockback Boundary Safety:
   - In `src/ants_sim/combat_ai.cpp:203–207` (`update_striking`):
     - Raycast from `target->pos` step-by-step along `(kdx, kdy)` up to `dist_tiles`, checking `grid.in_bounds(next)` and `!grid.is_solid_obstacle(next)`.
     - Clamp to valid in-bounds coordinates so ants are never knocked outside grid boundaries.
4. Document exact C++ replacement code and diffs in `thief_alliances_ai_remediation.md`.
5. Deliver handoff report to `handoff.md` and send message to parent.
