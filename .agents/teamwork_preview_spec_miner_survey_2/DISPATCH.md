## 2026-09-06T22:32:23Z

# Task Assignment: Simulation Rules Specification Mining

## Target Scope
Survey the authoritative reference specification for Deterministic Simulation & Game Rules:
- Specification: `/Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md`
- User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`

## Objective
Extract and document the exact, complete specification for:
1. Simulation Grid & Tick Rate: tick frequency (e.g. 10 Hz / 20 Hz / discrete ticks), fixed-point vs integer math, state determinism, PRNG seed/algorithm.
2. Unit Types & Damage Matrix: Worker, Thief, Fire, Bomber, Swimmer, Combat Ant. HP stats (starting HP, max HP), movement speeds. Universal 1 HP melee strike for standard units. Combat Ant 2 HP per strike + 4-5 tile ballistic knockback physics (arc, obstacle collision, water/fire landing).
3. Combat Ant Guard AI: autonomous behavior specification (idle state, 3-tile Chebyshev/Euclidean aggro perimeter scan, autonomous intercept punch, automatic return to post/home anchor).
4. Placement Rules: Cardinal-only placement (N, E, S, W; rejection of diagonals) for firewalls and bombs.
5. Bomb & Mine Mechanics: Bomber Ant planting (delay, arming, 2 HP explosive radius/damage), defusing mechanics (squash/abdb animation, sounds 73 + 74, body crush interaction).
6. Fire & Ricochet Physics: Landing on fire deals 1 fire damage and bounces non-fire ants; multi-fire chains and ant collision deflections create ricochets. Fire extinguishing rules (ants NEVER extinguish fire by landing on it; only Fire Ants are immune and can extinguish fire).
7. Timers & Expirations: Exactly 180-second lifetime for firewalls (burnout) and bridges (collapse causing non-swimmers to drown instantly).
8. Base Mechanics: Concentric Chebyshev ring queuing around anthill; 17-frame base entry, food deposit logic/score addition, underground 100% heal, and emergence.
9. Thief Ant Infiltration: Dives into enemy anthill, triggers Sound 58 (underattack.wav, 2,566 Hz alarm siren) and News Flash on victim's screen, steals min(50, score), triggers Sound 88 (scoredn.wav), drops lunchbox on death with universal pickup.
10. Alliances & Teaming: Dynamic FFA-to-alliance flow initiated by clicking an enemy anthill (allypro.wav, allyyes.wav/allyon.wav, allynot.wav, allyoff.wav). Combined scores for HUD/standings with strictly preserved individual stats in memory.
11. Game Over Scorecard: Immediate simulation freeze at 0:00; winner audio (winner.wav / Sound 56) vs loser audio (playerout.wav / Sound 41); full-screen results scorecard (re_screen) tracking 4 statistics per player (Score, Friendly Ants Lost, Enemy Ants Killed, New Ants Hatched).

## Output
Write your findings to `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md` and write your completion `handoff.md`.

## 2026-09-06T22:35:13Z
**Context**: User Clarification on Bridge Mechanics
**Content**: Sentinel has provided a clarification appended to ORIGINAL_REQUEST.md & GAME_REVERSE_ENGINEERING.md §5.3:
1. Universal Traversal: Once a bridge has been dug/built by a swimmer ant on a water tile, ANY ant in the game can walk across it (friendly, allied, or hostile enemy ants alike).
2. Expiration and Drowning: Bridges expire after exactly 180 seconds. If ANY non-swimmer ant (friendly or enemy) is standing on top of the bridge when it collapses, they fall into deep water and drown instantly. Only Swimmer Ants survive.
**Action**: Ensure this specification and exact drowning/traversal rules are thoroughly documented in your `survey_sim.md` and handoff report.
