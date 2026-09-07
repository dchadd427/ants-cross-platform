## 2026-09-06T23:43:48Z

<USER_REQUEST>
You are M3 Explorer 1 (Graphics, Windowing, Viewport & Sprite Rendering) for Milestone 3 (ants-app).
Your identity: explorer_m3_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3/survey_architecture.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/asset_archive.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/mirroring.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/sim_engine.hpp

Your mission:
1. Design the graphics, windowing, viewport, and sprite rendering architecture for ants-app:
   - SDL2 window initialization and authentic 640x480 virtual canvas with integer pixel scaling and aspect-ratio preservation (pillarboxing/letterboxing 4:3) via SDL_RenderSetLogicalSize.
   - Viewport camera management: world coordinate translation, smooth scrolling (arrow keys, WASD, mouse edge panning, minimap click centering), bounds clamping.
   - Terrain rendering: Layer 1 base tiles + Layer 2 interactive objects/structures (anthills, food, bridges at stages 1-4, bombs, fires) compositing from LevelData / Grid.
   - Animated ant sprite rendering: 6 unit types, 8 directional facings using 5-to-8 directional mirroring (get_mirrored_sprite), state animations (idle, walk, strike, bomb plant/defuse, fire ignite/extinguish, bridge build, base entry/exit, thief infiltration, drowning), elevation offset rendering (36px apex parabolic knockback altitude).
2. Formulate public C++ headers and implementation blueprints:
   - include/ants_app/renderer.hpp
   - include/ants_app/application.hpp
   - src/ants_app/renderer.cpp
   - src/ants_app/application.cpp
3. Document full specifications and drop-in code blueprints in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1/graphics_and_renderer_plan.md
4. Deliver handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1/handoff.md
   and send a completion message to your parent.
</USER_REQUEST>
