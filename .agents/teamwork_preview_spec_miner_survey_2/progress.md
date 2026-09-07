# Progress - miner_survey_2 (Simulation Rules Spec Miner)

Last visited: 2026-09-06T22:37:30Z

## Status
Completed comprehensive specification mining for Simulation Rules. `survey_sim.md` and `handoff.md` prepared.

## Milestones & Tasks
- [x] Initial setup: BRIEFING.md, DISPATCH.md, progress.md
- [x] Deep examination of authoritative specifications:
  - [x] Section 2 & 5 of GAME_REVERSE_ENGINEERING.md
  - [x] Analysis of disassembly routines, object models, and offsets in Ants.exe
  - [x] 1. Simulation Grid & Tick Rate (ticks, 32x32 tiles, integer math, determinism, MSVC LCG PRNG latseed)
  - [x] 2. Unit Types & Damage Matrix (10 HP, speeds, universal 1 HP strike, Combat Ant 2 HP + 4-5 tile knockback)
  - [x] 3. Combat Ant Guard AI (idle, 3-tile Chebyshev aggro scan, intercept punch, return to post)
  - [x] 4. Placement Rules (cardinal-only, rejection of diagonals, tile flags 0x02 bomb / 0x04 fire)
  - [x] 5. Bomb & Mine Mechanics (planting absb301, arming, 2 HP explosion, squash defusal abdb301 with sounds 73+74)
  - [x] 6. Fire & Ricochet Physics (fire damage +1, bouncing, multi-fire chains, non-extinguishing landing, Fire Ant extinguish afxf301)
  - [x] 7. Timers & Expirations (180s bridge/firewall, universal bridge traversal, collapse drowning for non-swimmers)
  - [x] 8. Base Mechanics (Chebyshev ring queue, 17-frame entry hgen301, deposit, 100% heal at frame 8, emergence, 200 pt hatch)
  - [x] 9. Thief Ant Infiltration (33-frame dive atcr501, sound 58 alarm siren, 50-pt steal, sound 88, lunchbox drop on death)
  - [x] 10. Alliances & Teaming (FFA-to-alliance, sounds 49-53, shared scores vs preserved discrete stats in memory)
  - [x] 11. Game Over Scorecard (freeze at 0:00, audio winner sound 56 / loser sound 41, re_screen anim 25, 4 stats)
- [x] Synthesized all findings into `survey_sim.md` with required tables (40 Features Discovered & 24 Edge Cases)
- [ ] Complete `handoff.md` with 5-component report
- [ ] Update `BRIEFING.md`
- [ ] Send completion message to parent (`a28dfa55-5a82-453d-a21b-99459a66b340`)
