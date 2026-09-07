## 2026-09-06T23:13:00Z

# Dispatch Assignment: explorer_m2_3

**Identity**: explorer_m2_3 (M2 Game Rules, Abilities & Test Suite Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_3  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

## Objectives
1. Formulate exact specifications and algorithmic models for special abilities, timers, and game mechanics:
   - Cardinal Placement: strict orthogonal adjacency (`dx == 0 || dy == 0`, `abs(dx)+abs(dy) == 1`), diagonal placement rejection, ground flags.
   - Bombs & Defusal: Bomber planting (`absb301`, Sound 90), detonation (2 HP damage, 2-3 tile knockback, Sound 4), Bomber-only body squash defusal (`abdb301`, Sounds 73+74).
   - Fire Mechanics: Fire Ant ignition (`afsf301`, Sounds 67+68), 180s timer, A* obstacle for non-fire ants, ricochet physics (+1 fire damage per bounce, multi-fire chains, never extinguish by landing), Fire Ant walking & extinguishing (`afxf301`, Sound 69).
   - Bridge Mechanics: Swimmer construction across water (4 stages), universal traversal (any ant, friendly or hostile), 180s collapse with instant drowning for non-swimmers (`death_status = 0xF`), Swimmer survival.
   - Anthill & Queuing: concentric Chebyshev rings, FIFO entry, 17-frame sequence (`hgen301`, food deposit at frame 4, 100% full heal at frame 8, emergence at frame 16).
   - Egg Hatching: 200 points deducted per egg hatched, inventory check.
   - Thief Infiltration: 33-frame sequence (`atcr501`), Sound 58 alarm siren on victim client, loot `min(50, victim.score)`, victim Sound 88 score drop, physical lunchbox drop on death, universal pickup.
   - Dynamic Alliances: FFA default, propose (Sound 51), accept (Sounds 53+50), deny (Sound 52), break (Sound 49), combined scoreboard display while preserving discrete individual statistics.
   - 4-Stat Scorecard: Score, Friendly Lost, Enemy Killed, New Hatched.
   - Match countdown to 0:00, simulation freeze, winner Sound 56 vs loser Sound 41.
2. Design the comprehensive automated headless test suite `tests/test_sim/test_sim_rules.cpp` and CMake integration for `libants-sim` and `test_sim`.
3. Provide complete specifications in `sim_rules_and_tests_plan.md`.
4. Deliver your handoff report to `handoff.md` and send a message to parent.
