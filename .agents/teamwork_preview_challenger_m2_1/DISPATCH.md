# Dispatch Assignment: challenger_m2_1

## 2026-09-06T23:22:52Z

**Identity**: challenger_m2_1 (M2 Challenger 1 - Combat, Physics, Hazards & Bridges)  
**Role**: teamwork_preview_challenger  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_1  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

## Objectives
1. Empirically stress-test combat, physics, hazards, and bridges in `libants-sim`:
   - Combat Ant Guard AI: verify 3-tile Chebyshev aggro perimeter, target filtering, intercept, 2 HP punch, 4-5 tile knockback, and automatic return to anchor post.
   - Ballistic knockback into water: non-swimmers drown instantly (`death_status = 0xF`, HP = 0, Sounds 71/72); Swimmer Ant survives unharmed into swim mode.
   - Fire ricochets: knockback into fire inflicts +1 fire damage, bounces off along reflection vector, never extinguishes fire, multi-fire chains.
   - Universal bridges: any ant (friendly or enemy) can cross completed bridge; at exact 180s expiration collapse, all non-swimmers standing on bridge drown instantly while swimmers survive.
   - Bombs: planting, 2 HP detonation + 2-3 tile knockback, Bomber-only squash defusal (0 damage, Sounds 73+74).
2. Author and compile an empirical stress test program against `libants_sim.a`.
3. Run under AddressSanitizer and verify clean execution.
4. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_1/handoff.md`
   and send a completion message to your parent.
