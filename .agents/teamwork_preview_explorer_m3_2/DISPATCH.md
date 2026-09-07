## 2026-09-06T23:43:48Z
You are M3 Explorer 2 (HUD, UI Controls, Input Dispatch & Scorecard Modal) for Milestone 3 (ants-app).
Your identity: explorer_m3_2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3/survey_architecture.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/sim_engine.hpp

Your mission:
1. Design the complete in-game HUD and modal UI architecture for ants-app:
   - Radar / Minimap: 32x32 overview widget, team color dots for live ants, anthill icons, food indicators, active camera viewport frustum rectangle, click-to-navigate.
   - Ant Selection Card: selected unit portrait icon, type name, HP bar (green/yellow/red), holding food/points indicator, order status.
   - Hatch Controls: Hatch button with click/press frame states, egg pile visualization, incubation counter, 200 pt deduction check.
   - Action Order Buttons: Move, Attack, Bomb, Fire, Bridge, Thief, Cancel buttons with active selection highlighting.
   - News Flash Banner: text alert queue, timer countdown, dismiss transitions.
   - Match Clock: formatted mm:ss countdown to 0:00 with color warning under 1 minute.
   - Scoreboard HUD: live multi-faction scores, dynamic alliance grouping display.
   - Scorecard Modal: full-screen re_screen (Anim 25) upon GameOver freeze, 4 player columns, 4 stats (Score, Friendly Lost, Enemy Killed, Hatched), winner Sound 56 vs loser Sound 41 routing, replay/quit buttons.
   - Mouse & Keyboard Input Dispatch: click selection, box selection, order dispatch to SimulationEngine::issue_order, keyboard shortcuts.
2. Formulate public C++ headers and implementation blueprints:
   - include/ants_app/hud.hpp
   - include/ants_app/scorecard.hpp
   - src/ants_app/hud.cpp
   - src/ants_app/scorecard.cpp
3. Document full specifications and drop-in code blueprints in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_2/hud_and_ui_plan.md
4. Deliver handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_2/handoff.md
   and send a completion message to your parent.
