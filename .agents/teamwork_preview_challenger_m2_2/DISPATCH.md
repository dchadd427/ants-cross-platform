# Dispatch Assignment: challenger_m2_2

**Identity**: challenger_m2_2 (M2 Challenger 2 - Economy, Base, Alliances & Game Lifecycle)  
**Role**: teamwork_preview_challenger  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_2  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

## Objectives
1. Empirically stress-test economy, base lifecycle, alliances, and game over sequences:
   - Base entry & queuing: concentric Chebyshev rings, 17-frame sequence, food deposit at frame 4 (Sound 87), 100% full heal at frame 8 (Sound 36), emergence at frame 16.
   - Egg hatching: 200 points deducted per egg hatched, inventory check.
   - Thief infiltration: 33-frame sequence, Sound 58 alarm siren and News Flash to victim client, loot `min(50, score)`, victim Sound 88 score drop, physical lunchbox drop on carrier death, universal pickup.
   - Dynamic alliances: FFA default, propose Sound 51, accept Sounds 53+50, deny Sound 52, break Sound 49, combined HUD scoreboard while preserving discrete individual stats in memory.
   - Match countdown to 0:00: immediate simulation freeze, winner Sound 56 vs loser Sound 41 (losers never hear winner.wav), 4-stat scorecard tracking (Score, Friendly Lost, Enemy Killed, New Hatched).
2. Author and compile an empirical stress test program against `libants_sim.a`.
3. Run under AddressSanitizer and verify clean execution.
4. Render your verdict (APPROVE or REQUEST_CHANGES) in:
   `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_2/handoff.md`
   and send a completion message to your parent.
