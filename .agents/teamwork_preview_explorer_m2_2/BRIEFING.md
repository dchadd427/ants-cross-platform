# BRIEFING — 2026-09-06T23:15:00Z

## Mission
Design the unit, combat, and physical interaction headers (`ant_unit.hpp`, `combat_ai.hpp`, `physics.hpp`), algorithms, and data structures for Milestone 2 (`ants-sim`).

## 🔒 My Identity
- Archetype: Explorer
- Roles: Units, Combat AI & Physics Explorer
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_2
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: M2 (ants-sim)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement outside agent folder
- Deterministic 20 Hz simulation with pure integer/fixed-point math (zero floats in sim state)
- Universal 10 HP max metric, 1 HP melee strike standard, Combat Ant 2 HP heavy punch
- Combat Ant 3-tile Chebyshev autonomous guard AI
- Ballistic knockback (4-5 tiles combat, 2-3 tiles bomb), parabolic trajectory, obstacle bounce
- Water landing drowning (death_status = 0xF, HP = 0, Sound 71/72, rising bubbles) vs Swimmer survival
- 12-tick (600 ms) stun recovery

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: not yet

## Investigation State
- **Explored paths**: `ORIGINAL_REQUEST.md`, `PROJECT.md`, `survey_sim.md`, `GAME_REVERSE_ENGINEERING.md`, `include/ants_assets/mirroring.hpp`, other M2 explorer dispatches.
- **Key findings**:
  - Full C++ headers designed: `ant_unit.hpp`, `combat_ai.hpp`, `physics.hpp`.
  - Exact 16.16 fixed-point speed math for 20 Hz tick step (standard 4.0 px/tick, thief 5.6 px/tick, aquatic 3.2 px/tick, diagonal scaling 46341/65536).
  - Combat Ant autonomous guard AI 4-state cycle (`GuardIdle` -> `Intercepting` -> `Striking` -> `Returning`) with 3-tile Chebyshev aggro perimeter.
  - Parabolic ballistic knockback formula with obstacle raycasting, instant deep water drowning (`death_status = 0xF`, Sound 71/72, 22 subitems), Swimmer aquatic immunity, fire ricochet (+1 damage, reflection, non-occupancy, never extinguished by landing), and 12-tick stun recovery.
- **Unexplored areas**: Header designs and algorithmic plans completed and documented in `combat_and_physics_plan.md`. Ready for worker implementation.

## Key Decisions Made
- `AntType` strongly-typed enum mapping: Worker=0, Bomber=1, Fire=2, Thief=3, Combat=4, Swimmer=5.
- Interoperability with M1 `ants_assets`: reuse `ants::assets::Direction`.
- All movement calculations use 16.16 fixed-point math to preserve 100% deterministic cross-platform behavior.
- Documented full C++17 header definitions in `combat_and_physics_plan.md` to guide M2 implementers directly.

## Artifact Index
- `.agents/teamwork_preview_explorer_m2_2/combat_and_physics_plan.md` — Full architectural blueprint, exact algorithms, formulas, and header declarations
- `.agents/teamwork_preview_explorer_m2_2/handoff.md` — 5-component handoff report
