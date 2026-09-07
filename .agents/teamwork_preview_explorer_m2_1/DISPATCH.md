## 2026-09-06T23:12:59Z

# Dispatch Assignment: explorer_m2_1

**Identity**: explorer_m2_1 (M2 Sim Architecture & Core Engine Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_1  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/lvl_parser.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/asset_archive.hpp

## Objectives
1. Design the public C++ API headers under `include/ants_sim/`:
   - `sim_engine.hpp`: Master `SimulationEngine` class providing:
     - `init(const LevelData& level, uint32_t random_seed)`
     - `tick()` -> discrete advance by 1 tick (50 ms, 20 Hz)
     - `issue_order(const AntOrder& order)`
     - `hatch_ant(uint8_t player_id, AntType type) -> bool`
     - `propose_alliance(uint8_t from_player, uint8_t to_player)`
     - `get_world_state() -> const WorldState&`
     - `get_match_time_remaining_ms() -> uint32_t`
     - `is_match_over() -> bool`
     - `get_player_stats(uint8_t player_id) -> PlayerMatchStats` (Score, Friendly Lost, Enemy Killed, Hatched)
     - `poll_audio_events() -> std::vector<AudioEvent>` (sound_id, world_x, world_y, priority)
     - `poll_news_events() -> std::vector<NewsEvent>` (target_player, message_text)
   - `grid.hpp`: 32x32 integer tile grid representation, Layer 1 terrain passability, Layer 2 interactive objects/structures, food respawn schedules, anthill spawn locations.
   - `prng.hpp`: Deterministic MSVC LCG PRNG (`latseed`, `sim_rand`, `sim_srand`).
   - `match_stats.hpp`: 4-stat tracking structures and match lifecycle.
2. Define exact data structures for `WorldState`, `AntOrder`, `AudioEvent`, `NewsEvent`, `TileCell`, and `PlayerMatchStats`.
3. Formulate CMake configuration for library `ants_sim`.
4. Provide full header specifications and architectural blueprint in `sim_architecture_plan.md`.
5. Deliver your handoff report to `handoff.md` and send a message to parent.
