## 2026-09-06T23:12:59Z

# Dispatch Assignment: explorer_m2_2

**Identity**: explorer_m2_2 (M2 Units, Combat AI & Physics Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_2  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

## Objectives
1. Design the unit, combat, and physical interaction headers under `include/ants_sim/`:
   - `ant_unit.hpp`: Unit class hierarchy or entity representation:
     - 6 unit types (Worker 0, Bomber 1, Fire 2, Thief 3, Combat 4, Swimmer 5).
     - HP tracking: starting 10 HP, max 10 HP.
     - Movement state machine: integer pixel coordinates, direction (8 compass facings), speeds (standard 2.5 tps, thief 3.5 tps, swimmer aquatic speed).
     - Carried food/points and holding state (`ht*` / `h*` suites).
   - `combat_ai.hpp`: Combat Ant Autonomous Guard AI:
     - Guard anchor coordinate `(x_g, y_g)`.
     - 3-tile Chebyshev scan perimeter: `max(abs(dx), abs(dy)) <= 3`.
     - Target filtering: ignore allies/friends, target live enemies not underground.
     - Autonomous intercept pathfinding.
     - Melee strike delivery (2 HP damage + 4–5 tile knockback, Sound 78).
     - Autonomous disengagement and return to anchor post.
   - `physics.hpp`: Ballistic knockback, collisions & drowning:
     - Heavy punch knockback (4–5 tiles) and bomb blast knockback (2–3 tiles).
     - Parabolic flight trajectory and obstacle collision termination.
     - Landing on water: non-swimmers drown instantly (`death_status = 0xF`, HP = 0, Sound 71/72, rising bubbles); Swimmer Ants survive unharmed into swim mode.
     - Stun recovery: 12-tick (600 ms) immobilization before returning to ready.
2. Provide exact C++ implementation algorithms, formulas, and data structures in `combat_and_physics_plan.md`.
3. Deliver your handoff report to `handoff.md` and send a message to parent.
