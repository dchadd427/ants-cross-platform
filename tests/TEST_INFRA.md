# E2E Test Infrastructure Specification: Ants Remake

## 1. Test Philosophy: Opaque-Box & Requirement-Driven

The End-to-End (E2E) testing track for the Ants remake adheres to an **opaque-box, requirement-driven testing philosophy**. Tests treat the engine, simulation, and asset decoders as closed units governed strictly by:
1. The authoritative reverse-engineering specification (`GAME_REVERSE_ENGINEERING.md`).
2. The user requirements and directives (`ORIGINAL_REQUEST.md`).
3. The decoupled interface contracts and milestone definitions (`PROJECT.md`).

### Core Testing Tenets
- **Opaque-Box Verification:** Tests assert exclusively on observable inputs, outputs, states, and contractual events (e.g. decoded binary structures, simulation tick outputs, state transitions, audio trigger events, scorecard numbers). Tests never rely on implementation internals or private structures.
- **Specification as Authoritative Oracle:** Expected outputs are derived directly from the reverse-engineered formulas, memory offsets, lookup tables, and audio/animation IDs in `GAME_REVERSE_ENGINEERING.md`. Tests do not fit to passing bugs.
- **Progressive & Independent Testability:** Test suites are completely self-contained. Each test sets up its own preconditions, executes deterministically, asserts strict invariants, and cleans up resources without inter-test dependencies or reliance on execution order.
- **Defect Escalation:** As QA specialists, test writers author only test code. If an implementation failure is detected during execution against specification criteria, the defect is escalated directly to the implementing agent for remediation.

---

## 2. Testing Methodology

The test suite systematically employs four established testing disciplines:

```
+-----------------------------------------------------------------------------+
|                               TESTING TIERS                                 |
+-----------------------------------------------------------------------------+
| Tier 1: Feature Coverage (Category-Partition)                               |
|   - >= 5 test cases per feature across all 49 inventoried features           |
|   - Primary happy paths, operational modalities, return contracts           |
+-----------------------------------------------------------------------------+
| Tier 2: Boundary & Corner Cases (Boundary Value Analysis)                   |
|   - >= 5 boundary/corner test cases per feature                             |
|   - Min/max limits, edge-of-grid, numeric overflow, invalid input rejection |
+-----------------------------------------------------------------------------+
| Tier 3: Cross-Feature Combinations (Pairwise / Orthogonal Arrays)           |
|   - Physical interactions: combat knockback x firewalls x ricochets         |
|   - Temporal & spatial interactions: bridge building x 180s collapse x drown|
|   - Game rules: thief infiltration dive x alarm 58 x score x lunchbox drop  |
+-----------------------------------------------------------------------------+
| Tier 4: Real-World Workload Scenarios (Full Game Lifecycle)                 |
|   - Complete 12-minute match simulation to 0:00 simulation freeze           |
|   - Split audio routing: winner fanfare (Sound 56) vs loser sting (Sound 41)|
|   - End-game 4-stat scorecard tracking and ranking verification             |
+-----------------------------------------------------------------------------+
```

### 2.1 Category-Partition Method (Tier 1)
Each feature's functional domain is partitioned into distinct categories (e.g., ant unit types, direction indices, terrain classifications, player alliance states, audio triggers). Representative test cases are constructed for every partition to ensure thorough, unbiased coverage.

### 2.2 Boundary Value Analysis (BVA) & Corner Cases (Tier 2)
Boundary conditions probe the precise limits of the system:
- **Spatial Boundaries:** Coordinates at `(0, 0)`, `(width-1, height-1)`, negative coordinates, off-grid knockback deflections, boundary clamping.
- **Temporal Boundaries:** Expiration thresholds at exact tick boundaries (e.g. 180,000 ms: `179,950 ms` [active], `180,000 ms` [expired/collapse], `180,050 ms` [post-collapse]). Match clock at `0:01` (active) vs `0:00` (frozen).
- **Economic & Numeric Boundaries:** Food theft `min(50, victim_score)` when score is `0`, `25`, `50`, `75`; egg hatching when score is `< 200` vs `>= 200`; unit HP bounds `[0, 10]`.
- **Constraint Enforcement:** Strict cardinal placement (`|dx| + |dy| == 1`), verifying diagonal placement (`|dx| == 1 && |dy| == 1`) is rejected with error status.

### 2.3 Combinatorial & Pairwise Testing (Tier 3)
Features do not operate in isolation. Tier 3 evaluates multi-system intersections using pairwise combinations:
- Knockback + Firewall Obstacle + Multi-Fire Chain + Ricochet Reflection + Ant Collision Deflection.
- Bridge Construction + Universal Traversal (Enemy + Friendly) + 180s Collapse + Drowning Immunity Check (Swimmer survives vs Non-Swimmers drown with 22-subitem sequence).
- Thief Infiltration Dive (Anim 1095) + Sound 58 Alarm Siren (2,566 Hz) + News Flash + 50-pt Theft + Sound 88 + Carrier Death + Layer 2 Lunchbox Drop (Sprite 513) + Universal Pickup + Return Deposit (Sound 87).
- Alliance Proposal + Acceptance + Shared HUD Score + Discrete Memory Preservation + Alliance Dissolution + Score Uncoupling.

### 2.4 Real-World Workload Profiles (Tier 4)
Simulates end-to-end game scenarios:
- Complete matches on authentic maps (`TREASURE.LVL`, `ISLANDS.LVL`, `GAUNTLET.LVL`, `MEDIUM.LVL`, `SMALL.LVL`, `TINY.LVL`).
- Continuous 20 Hz simulation stepping under heavy entity loads (up to 64 active ants per team).
- Match termination at `0:00`: immediate input lock and simulation freeze, audio split dispatch (Sound 56 for winner, Sound 41 for defeated teams), and accurate calculation of the 4 tracked statistics (Score, Friendly Lost, Enemy Killed, Hatched).

---

## 3. Feature Inventory Coverage Mapping

Every feature from `PROJECT.md` is mapped across the four testing tiers:

| # | Feature Name | M-stone | Tier 1 (Feature >=5) | Tier 2 (Boundary >=5) | Tier 3 (Pairwise) | Tier 4 (Scenario) |
|---|--------------|---------|-----------------------|------------------------|-------------------|-------------------|
| 1 | `ants.chd` Header & Palette | M1 | `test_chd_header_magic`, `test_chd_offsets`, `test_palette_256_entries`, `test_palette_color_key_254`, `test_palette_team_color_ranges` | `test_chd_header_corrupt_size`, `test_chd_offset_out_of_bounds`, `test_palette_channel_order_rgb`, `test_palette_boundary_indices`, `test_palette_alpha_assignment` | Palette applied to sprites across all 4 team palettes | Full game presentation palette fidelity |
| 2 | Table 1 Paletted Sprites | M1 | `test_sprite_count_2794`, `test_sprite_stride_pitch`, `test_sprite_dimensions`, `test_sprite_filename_null_term`, `test_sprite_pixel_unpacking` | `test_sprite_min_dimension_1x1`, `test_sprite_max_dimension_640x480`, `test_sprite_pitch_alignment`, `test_sprite_out_of_range_index`, `test_sprite_truncated_payload` | Sprite rendering under active unit rotation | Real-time viewport entity blitting |
| 3 | Table 2 PCM Audio Clips | M1 | `test_sound_count_91`, `test_sound_waveformat_pcm`, `test_sound_sample_rates`, `test_sound_riff_header_gen`, `test_sound_pcm_buffer_validity` | `test_sound_zero_length_rejection`, `test_sound_max_clip_length`, `test_sound_id_boundary_0_and_90`, `test_sound_invalid_id_rejection`, `test_sound_header_truncation` | Sound dispatch synchronized with animation triggers | Multi-channel audio mixer playback |
| 4 | Table 3 & 4 Animations | M1 | `test_anim_count_1344`, `test_anim_subitem_parsing`, `test_anim_frame_dx_dy`, `test_anim_default_sp_triggers`, `test_anim_bounding_boxes` | `test_anim_zero_frame_handling`, `test_anim_max_subitem_count`, `test_anim_negative_offsets`, `test_anim_trigger_sentinel_ffffffff`, `test_anim_invalid_sprite_ref` | Frame-by-frame sound trigger dispatch | Animation state machine transitions |
| 5 | `Maps/*.LVL` Level Decoder | M1 | `test_lvl_load_all_6_maps`, `test_lvl_dimensions`, `test_lvl_layer1_terrain`, `test_lvl_layer2_interactive`, `test_lvl_trailing_bytes_zero` | `test_lvl_min_map_31x31`, `test_lvl_max_map_60x60`, `test_lvl_out_of_bounds_query`, `test_lvl_corrupt_header_rejection`, `test_lvl_unknown_terrain_id` | Map terrain collision with ant pathfinding | Complete match map loading & setup |
| 6 | 5-to-8 Directional Mirroring | M1 | `test_mirror_directions_7_and_3_unflipped`, `test_mirror_dir_9_to_west`, `test_mirror_dir_8_to_nw`, `test_mirror_dir_2_to_sw`, `test_mirror_dx_prime_formula` | `test_mirror_odd_width_sprite`, `test_mirror_even_width_sprite`, `test_mirror_1px_width_boundary`, `test_mirror_palette_index_integrity`, `test_mirror_cache_hit_rate` | 8-way ant movement animation display | Smooth full-circle ant navigation |
| 7 | Discrete 20 Hz Tick Engine | M2 | `test_tick_rate_50ms_interval`, `test_tick_counter_monotonic`, `test_tick_substep_order`, `test_tick_accumulator`, `test_tick_integer_math_purity` | `test_tick_zero_delta`, `test_tick_large_delta_spiral_clamp`, `test_tick_uint32_overflow_safe`, `test_tick_pause_and_resume`, `test_tick_time_remaining_decrement` | Tick-synchronized entity state updates | Complete match 20 Hz execution |
| 8 | MSVC LCG PRNG Engine | M2 | `test_lcg_msvc_recurrence`, `test_lcg_seed_reproducibility`, `test_lcg_high_bit_distribution`, `test_lcg_rand_max_range`, `test_lcg_state_serialization` | `test_lcg_seed_zero`, `test_lcg_seed_max_uint32`, `test_lcg_100k_period_integrity`, `test_lcg_negative_seed_cast`, `test_lcg_deterministic_lockstep` | PRNG-driven ricochet deflection angles | Replay recording and playback |
| 9 | Universal Unit Attributes | M2 | `test_unit_max_hp_10`, `test_unit_standard_melee_1hp`, `test_unit_death_at_0hp`, `test_unit_state_idle_default`, `test_unit_radius_metrics` | `test_unit_hp_negative_clamping`, `test_unit_hp_overheal_cap`, `test_unit_max_entities_64_per_team`, `test_unit_dead_unit_command_rejection`, `test_unit_invalid_type_rejection` | Unit combat interactions across classes | Army scaling and roster tracking |
| 10 | Combat Ant Heavy Punch | M2 | `test_combat_punch_2hp_damage`, `test_combat_punch_4_to_5_tile_knockback`, `test_combat_punch_sound_78`, `test_combat_punch_victim_stun_12_ticks`, `test_combat_punch_bounding_box` | `test_combat_punch_map_edge_clamping`, `test_combat_punch_obstacle_collision`, `test_combat_punch_diagonal_impulse_vector`, `test_combat_punch_0hp_kill_no_knockback`, `test_combat_punch_friendly_fire_disabled` | Punch knocking target into fire/water | Combat battle frontline dynamics |
| 11 | Combat Ant Guard AI | M2 | `test_guard_anchor_saved_on_idle`, `test_guard_aggro_radius_3_chebyshev`, `test_guard_autonomous_intercept`, `test_guard_return_to_post_after_strike`, `test_guard_re_anchor_on_player_order` | `test_guard_enemy_at_exact_distance_3`, `test_guard_enemy_at_distance_4_ignored`, `test_guard_target_killed_mid_intercept`, `test_guard_post_blocked_repath`, `test_guard_multiple_enemies_nearest_select` | Guard AI reacting to invading Thief Ant | Base defense perimeter simulation |
| 12 | Cardinal-Only Placement | M2 | `test_place_north_success`, `test_place_east_success`, `test_place_south_success`, `test_place_west_success`, `test_place_diagonal_rejected` | `test_place_distance_2_rejected`, `test_place_same_tile_rejected`, `test_place_off_grid_rejected`, `test_place_invalid_terrain_flag_rejected`, `test_place_occupied_tile_rejected` | Pathfinding to cardinal tile before placement | Interactive placement command flow |
| 13 | Bomb Planting Mechanics | M2 | `test_bomb_plant_absb_anim`, `test_bomb_plant_sound_90`, `test_bomb_detonate_2hp_damage`, `test_bomb_detonate_sound_60`, `test_bomb_detonate_2_3_tile_knockback` | `test_bomb_detonate_chain_reaction`, `test_bomb_detonate_edge_of_blast_radius`, `test_bomb_plant_max_bombs_limit`, `test_bomb_non_bomber_plant_rejection`, `test_bomb_layer2_clear_on_detonate` | Bomb blast hurling units into water | Minefield tactical defense |
| 14 | Bomber Squash Defusal | M2 | `test_defuse_abdb_anim`, `test_defuse_sound_73_grab`, `test_defuse_sound_74_muffle`, `test_defuse_bomb_cleared_safely`, `test_defuse_bomber_takes_0_damage` | `test_defuse_friendly_bomb_noop`, `test_defuse_non_bomber_defuse_rejection`, `test_defuse_interrupted_by_damage`, `test_defuse_already_exploding_bomb`, `test_defuse_multiple_bombers_on_single_bomb` | Bomber defusing while under fire | Base mine clearing operation |
| 15 | Fire Ignition & Obstruction | M2 | `test_fire_ignition_afsf_anim`, `test_fire_ignition_sound_67_and_68`, `test_fire_wallup04_layer2_set`, `test_fire_pathfinder_blocks_standard_ants`, `test_fire_fire_ant_pathfinder_pass` | `test_fire_ignition_on_water_rejected`, `test_fire_ignition_on_wall_rejected`, `test_fire_ignition_under_unit_instant_damage`, `test_fire_multiple_fires_adjacent`, `test_fire_max_active_fires_tracking` | Fire barrier funneling enemy pathfinding | Chokepoint tactical lockdown |
| 16 | Fire & Ricochet Physics | M2 | `test_fire_contact_1hp_damage`, `test_fire_ricochet_reflection_vector`, `test_fire_never_extinguished_by_landing`, `test_fire_multi_fire_chain_damage`, `test_fire_ant_collision_deflection` | `test_fire_ricochet_into_map_boundary`, `test_fire_ricochet_into_water_drown`, `test_fire_ricochet_corner_pinball_loop`, `test_fire_lethal_damage_mid_flight`, `test_fire_ricochet_stun_reset` | Combat punch into firewall ricochet chain | Pinball chaos physical stress test |
| 17 | Fire Ant Immunity & Extinguish | M2 | `test_fire_ant_takes_0_fire_damage`, `test_fire_ant_walks_on_fire_tile`, `test_fire_extinguish_afxf_anim`, `test_fire_extinguish_sound_69`, `test_fire_extinguish_sputter_anim_135` | `test_fire_extinguish_timer_cancelled`, `test_fire_extinguish_already_burned_out`, `test_fire_extinguish_non_fire_ant_rejected`, `test_fire_extinguish_friendly_vs_enemy_fire`, `test_fire_extinguish_interrupted` | Fire Ant clearing path through enemy firewall | Controlled firefighting maneuvers |
| 18 | 180-Second Timers | M2 | `test_timer_registration_180s`, `test_timer_firewall_burnout_at_180s`, `test_timer_bridge_collapse_at_180s`, `test_timer_fireburnout_sound_play`, `test_timer_match_end_prior_to_180s` | `test_timer_exact_179950ms_active`, `test_timer_exact_180000ms_trigger`, `test_timer_exact_180050ms_cleared`, `test_timer_multiple_concurrent_180s_timers`, `test_timer_early_removal_deregistration` | Firewall burnout + bridge collapse sync | Match-long temporal integrity |
| 19 | Universal Bridge Traversal | M2 | `test_bridge_construct_4_stages`, `test_bridge_friendly_traversal`, `test_bridge_enemy_traversal`, `test_bridge_allied_traversal`, `test_bridge_water_tile_passable` | `test_bridge_unbuilt_water_impassable`, `test_bridge_construction_interrupted`, `test_bridge_construct_on_land_rejected`, `test_bridge_diagonal_bridge_rejected`, `test_bridge_max_bridges_connected` | Enemy ants flanking over newly built bridge | Island assault & invasion |
| 20 | Bridge Collapse Drowning | M2 | `test_bridge_collapse_at_180s`, `test_bridge_non_swimmer_drowns_instantly`, `test_bridge_swimmer_survives`, `test_bridge_collapse_death_status_0f`, `test_bridge_collapse_sound_71_72` | `test_bridge_collapse_with_no_occupants`, `test_bridge_collapse_with_multiple_ants`, `test_bridge_collapse_friendly_and_enemy_mix`, `test_bridge_collapse_ant_stepping_off_at_179950ms`, `test_bridge_collapse_carrier_drops_lunchbox` | Bridge collapse with mixed units (Swimmer + Worker) | Ambush by timed bridge collapse |
| 21 | Anthill Queuing & 17-Frame Entry | M2 | `test_base_chebyshev_ring_queue_r1`, `test_base_chebyshev_ring_queue_r2`, `test_base_17_frame_entry_anim`, `test_base_entry_fifo_order`, `test_base_step_forward_when_entrance_clears` | `test_base_queue_full_overflow_ring_r3`, `test_base_entry_interrupted_by_attack`, `test_base_enemy_ant_entry_rejected`, `test_base_simultaneous_arrival_tie_break`, `test_base_dead_queued_unit_removal` | Multiple workers returning with food | Heavy traffic base supply lines |
| 22 | Anthill Food Deposit | M2 | `test_food_deposit_at_frame_4`, `test_food_deposit_sound_87_scoreup`, `test_food_deposit_score_increment`, `test_food_deposit_inventory_cleared`, `test_food_deposit_stat_tracking` | `test_food_deposit_empty_handed_no_score`, `test_food_deposit_stolen_points_credit`, `test_food_deposit_max_score_cap`, `test_food_deposit_consecutive_deposits`, `test_food_deposit_allied_score_update` | Worker delivers harvested food -> score rises | Continuous harvesting economy |
| 23 | Underground 100% Heal | M2 | `test_heal_at_frame_8_submerged`, `test_heal_sound_powerupc`, `test_heal_hp_restored_to_10`, `test_heal_applies_to_food_carriers`, `test_heal_applies_to_empty_ants` | `test_heal_already_at_10hp_noop`, `test_heal_at_1hp_near_death`, `test_heal_does_not_heal_enemies`, `test_heal_unit_leaves_at_frame_16`, `test_heal_consecutive_units_healed` | Wounded ant retreats to base -> emerges at 10 HP | Tactical retreat and reinforcement |
| 24 | Ant Hatching & Economy | M2 | `test_hatch_costs_200_points`, `test_hatch_deducts_team_score`, `test_hatch_decrements_egg_count`, `test_hatch_spawns_worker_at_base`, `test_hatch_increments_hatched_stat` | `test_hatch_score_under_200_rejected`, `test_hatch_zero_eggs_remaining_rejected`, `test_hatch_base_entrance_blocked_delay`, `test_hatch_rapid_consecutive_hatches`, `test_hatch_allied_shared_score_deduction` | Hatching army during economic boom | Population replenishment under attack |
| 25 | Thief Infiltration Dive | M2 | `test_thief_dive_33_frames_atcr501`, `test_thief_dive_sound_84_leap`, `test_thief_dive_sound_85_rummage`, `test_thief_dive_sound_86_emerge`, `test_thief_dive_submerged_frame_29` | `test_thief_dive_target_not_anthill_rejected`, `test_thief_dive_own_anthill_rejected`, `test_thief_dive_allied_anthill_rejected`, `test_thief_dive_killed_before_frame_19`, `test_thief_dive_victim_0_score` | Thief sneaking past Combat Ant into base | Heist stealth raid |
| 26 | Thief Alarm Siren & News Flash | M2 | `test_thief_alarm_sound_58_underattack`, `test_thief_alarm_2566hz_siren`, `test_thief_news_flash_string_53`, `test_thief_alert_routed_to_victim_only`, `test_thief_alert_timestamp_formatting` | `test_thief_alert_multiple_simultaneous_dives`, `test_thief_alert_suppressed_if_thief_dies_pre_dive`, `test_thief_alert_news_flash_fifo_queue`, `test_thief_alert_allied_no_alarm`, `test_thief_alert_high_priority_audio_channel` | Victim hears Sound 58 alarm + News flash | Emergency base defense response |
| 27 | Food Theft & Carry Visuals | M2 | `test_thief_steal_min_50_formula`, `test_thief_steal_sound_88_scoredn`, `test_thief_switch_to_ht_holding_anims`, `test_thief_carried_points_assignment`, `test_thief_status_food_stolen_string_62` | `test_thief_steal_victim_score_less_than_50`, `test_thief_steal_victim_score_zero_steals_zero`, `test_thief_steal_victim_score_1000_steals_50`, `test_thief_steal_cannot_steal_twice`, `test_thief_steal_hud_lunchbox_indicator` | Thief carrying stolen loot back to base | Score swing heist mechanics |
| 28 | Lunchbox Physical Drop | M2 | `test_lunchbox_drops_on_carrier_death`, `test_lunchbox_layer2_tile_set_anim_356`, `test_lunchbox_sprite_513_materialization`, `test_lunchbox_universal_pickup_by_any_ant`, `test_lunchbox_pickup_switches_to_holding_suite` | `test_lunchbox_empty_ant_death_no_drop`, `test_lunchbox_carrier_drowns_drops_in_water_sink`, `test_lunchbox_carrier_blown_by_bomb_drop_tile`, `test_lunchbox_pickup_on_already_holding_ant_rejected`, `test_lunchbox_persistence_on_ground` | Carrier killed -> Lunchbox dropped -> Enemy steals it | High-stakes food courier skirmish |
| 29 | Dynamic FFA-to-Alliance Flow | M2 | `test_alliance_propose_sound_51_allypro`, `test_alliance_accept_sound_53_and_50`, `test_alliance_deny_sound_52_allynot`, `test_alliance_break_sound_49_allyoff`, `test_alliance_status_strings_39_40_80` | `test_alliance_propose_to_self_rejected`, `test_alliance_already_allied_rejected`, `test_alliance_propose_to_ally_of_other_rejected`, `test_alliance_break_when_not_allied_rejected`, `test_alliance_simultaneous_mutual_proposals` | Propose -> Accept -> Cooperate -> Break | Complete diplomacy lifecycle |
| 30 | Allied Standings & Structs | M2 | `test_allied_combined_scoreboard_score`, `test_allied_friendly_fire_disabled`, `test_allied_combat_ant_ignores_ally`, `test_allied_thief_cannot_steal_ally`, `test_allied_discrete_individual_stats_preserved` | `test_allied_dissolution_score_uncoupling`, `test_allied_stat_mutation_isolation`, `test_allied_bridge_sharing`, `test_allied_4_player_two_teams_pairing`, `test_allied_victory_shared_status` | Allied team cooperative victory | FFA shifting alliances |
| 31 | Match Timer & Simulation Freeze | M2 | `test_timer_countdown_from_map_minutes`, `test_timer_freeze_at_000`, `test_timer_input_rejection_post_freeze`, `test_timer_entity_velocity_zero_post_freeze`, `test_timer_particle_freeze_post_freeze` | `test_timer_exact_001_still_active`, `test_timer_exact_000_freeze_instant`, `test_timer_negative_time_clamped_to_zero`, `test_timer_zero_duration_instant_freeze`, `test_timer_tick_advance_noop_post_freeze` | Match running down to 0:00 final buzzer | Full match game over transition |
| 32 | Split Game Over Audio | M2 | `test_winner_audio_sound_56_winner_wav`, `test_loser_audio_sound_41_playerout_wav`, `test_audio_routing_winner_team_gets_56`, `test_audio_routing_loser_teams_get_41`, `test_audio_routing_allied_winners_both_get_56` | `test_audio_routing_tie_breaker_highest_score`, `test_audio_routing_zero_score_winner`, `test_audio_routing_all_players_tied_tie_sound`, `test_audio_routing_sound_playback_exclusive`, `test_audio_routing_volume_attenuation_override` | Game ends -> Winning player hears fanfare | Victory / Defeat audio climax |
| 33 | 4-Stat Scorecard Tracking | M2 | `test_stat_score_tracked_accurately`, `test_stat_friendly_lost_incremented`, `test_stat_enemy_killed_incremented`, `test_stat_hatched_incremented`, `test_stat_4_player_roster_matrix` | `test_stat_suicide_friendly_lost_no_enemy_kill`, `test_stat_zero_actions_zero_stats`, `test_stat_large_numbers_no_overflow`, `test_stat_allied_individual_stats_distinct`, `test_stat_arrow_tip_column_alignment` | Match action audit vs final scorecard | Post-game analytics verification |
| 34 | Native macOS C++17 Application | M3 | `test_app_init_sdl_audio_video`, `test_app_clean_shutdown`, `test_app_target_framerate_60fps`, `test_app_sim_substep_sync`, `test_app_event_pump` | `test_app_window_resize_event`, `test_app_quit_signal_handling`, `test_app_headless_mode_toggle`, `test_app_lost_focus_pause`, `test_app_memory_leak_check` | Full application lifecycle | Standalone client deployment |
| 35 | 4:3 Integer Pixel Scaler | M3 | `test_scaler_640x480_base_dimensions`, `test_scaler_integer_multiples_1x_2x_3x`, `test_scaler_pillarbox_on_widescreen`, `test_scaler_letterbox_on_tall_screens`, `test_scaler_aspect_ratio_exact_4_to_3` | `test_scaler_minimum_window_640x480`, `test_scaler_extreme_ultrawide_aspect`, `test_scaler_odd_window_dimension_centering`, `test_scaler_pixel_perfect_nearest_neighbor`, `test_scaler_mouse_coord_translation` | Viewport rendering with letterbox borders | Sharp retro integer presentation |
| 36 | HUD Top Frame & Clock | M3 | `test_hud_top_border_x0y0`, `test_hud_clock_digits_rendering`, `test_hud_clock_format_mm_ss`, `test_hud_clock_colon_blink`, `test_hud_clock_palette_swap` | `test_hud_clock_single_digit_seconds_padded`, `test_hud_clock_zero_minutes_padded`, `test_hud_clock_max_minutes_99_59`, `test_hud_top_frame_clipping_bounds`, `test_hud_top_frame_transparency_pass` | Clock ticking in sync with match simulation | HUD header interface verification |
| 37 | HUD Borders & News Flash | M3 | `test_hud_left_border_x0y22`, `test_hud_bottom_banner_x17y461`, `test_hud_news_flash_display`, `test_hud_news_flash_fade_timer`, `test_hud_news_flash_fifo_queue` | `test_hud_news_flash_empty_string_noop`, `test_hud_news_flash_max_length_wrap`, `test_hud_news_flash_rapid_messages`, `test_hud_border_seamless_tiling`, `test_hud_border_color_key_254` | News flash banner flashing thief alerts | HUD frame compositing |
| 38 | HUD Minimap / Radar | M3 | `test_minimap_radar_x599y35`, `test_minimap_terrain_pixels`, `test_minimap_anthill_markers`, `test_minimap_unit_dots_team_colors`, `test_minimap_camera_rect_overlay` | `test_minimap_click_navigates_camera`, `test_minimap_bounds_clamp_click`, `test_minimap_aspect_ratio_square`, `test_minimap_hidden_areas_fog_check`, `test_minimap_unit_dot_blinking` | Minimap updates as units move across map | Strategic radar surveillance |
| 39 | HUD Selection Card | M3 | `test_hud_card_x480y126`, `test_hud_card_portrait_display`, `test_hud_card_hp_bar_scaling`, `test_hud_card_wtype_text`, `test_hud_card_lunchbox_icon_toggle` | `test_hud_card_deselect_clears_card`, `test_hud_card_dead_unit_portrait_gray`, `test_hud_card_enemy_unit_inspected`, `test_hud_card_hp_1_critical_red`, `test_hud_card_specialist_powerup_icon` | Unit selected -> Card updates -> Ability used | Unit command card telemetry |
| 40 | HUD Hatch Controls & Egg Pile | M3 | `test_hud_hatch_button_labhatch`, `test_hud_egg_pile_render`, `test_hud_hatch_click_triggers_sim`, `test_hud_egg_decrement_visual`, `test_hud_hatch_button_disable_if_no_funds` | `test_hud_hatch_rapid_click_debounce`, `test_hud_egg_pile_empty_visual`, `test_hud_hatch_hover_state_buthatup`, `test_hud_egg_incubation_progress_bar`, `test_hud_hatch_hotkey_dispatch` | Player clicks hatch button -> Egg hatches | Production management UI |
| 41 | HUD Action Order Buttons | M3 | `test_hud_buttons_move_attack_bomb_fire_bridge_thief`, `test_hud_buttons_cancel_order`, `test_hud_buttons_hotkey_bindings`, `test_hud_buttons_active_tool_highlight`, `test_hud_buttons_context_sensitivity` | `test_hud_buttons_worker_only_move_attack`, `test_hud_buttons_bomber_bomb_button_enabled`, `test_hud_buttons_click_outside_cancels_mode`, `test_hud_buttons_cooldown_overlay`, `test_hud_buttons_disabled_gray_render` | Action button clicked -> Order mode active | Tactical order dispatch UI |
| 42 | Multi-Channel Audio Mixer | M3 | `test_audio_mixer_32_pcm_channels`, `test_audio_mixer_volume_attenuation`, `test_audio_mixer_panning_by_world_pos`, `test_audio_mixer_priority_override`, `test_audio_mixer_concurrent_clip_playback` | `test_audio_mixer_overflow_33_clips_drops_lowest`, `test_audio_mixer_zero_volume_mute`, `test_audio_mixer_far_distance_inaudible`, `test_audio_mixer_stop_all_on_freeze`, `test_audio_mixer_channel_reuse_after_finish` | Sound effects mixed and spatialized | Immersive battlefield audio |
| 43 | AudioToolbox MIDI Synthesizer | M3 | `test_midi_load_intro_mid`, `test_midi_audiotoolbox_graph_init`, `test_midi_start_playback`, `test_midi_stop_playback`, `test_midi_volume_control` | `test_midi_missing_file_handled_gracefully`, `test_midi_restart_looping`, `test_midi_concurrent_with_pcm_mixer`, `test_midi_pause_resume`, `test_midi_rapid_start_stop` | Authentic INTRO.MID soundtrack playback | Background music integration |
| 44 | Results Scorecard Modal | M3 | `test_scorecard_re_screen_anim_25`, `test_scorecard_149_frames_layout`, `test_scorecard_top_banner_resbanr`, `test_scorecard_winner_pane_rendering`, `test_scorecard_other_players_pane_rendering` | `test_scorecard_click_dismiss_to_menu`, `test_scorecard_text_alignment_with_arrows`, `test_scorecard_team_color_swapped_names`, `test_scorecard_modal_blocks_sim_input`, `test_scorecard_zero_players_error_handling` | Match ends -> Scorecard displayed with audio | Authentic end-of-game celebration |
| 45 | Complete Interactive Game Loop | M3 | `test_game_loop_title_to_map_select`, `test_game_loop_match_init_and_start`, `test_game_loop_real_time_play`, `test_game_loop_match_end_transition`, `test_game_loop_scorecard_and_exit` | `test_game_loop_restart_match_cleans_state`, `test_game_loop_quit_mid_game`, `test_game_loop_multiple_consecutive_matches`, `test_game_loop_window_close_clean_exit`, `test_game_loop_zero_crash_stress_test` | End-to-end game playthrough | Full software lifecycle stability |
| 46 | Opaque-Box E2E Test Suite | E2E | `test_harness_tier_execution`, `test_harness_assertion_macros`, `test_harness_reporting`, `test_harness_exit_codes`, `test_harness_isolation` | `test_harness_filter_by_tier`, `test_harness_large_test_count`, `test_harness_assert_near_tolerance`, `test_harness_timeout_handling`, `test_harness_signal_safety` | Test suite validating test runner itself | Self-testing verification harness |
| 47 | Full Acceptance Criteria Verification | M4 | `test_acc_asset_parsing_2794_sprites`, `test_acc_asset_parsing_91_sounds`, `test_acc_asset_parsing_6_maps`, `test_acc_sim_damage_and_knockback`, `test_acc_sim_cardinal_and_180s_timers` | `test_acc_sim_thief_dive_and_alarm`, `test_acc_sim_allied_teams_scoring`, `test_acc_sim_audio_split_56_41`, `test_acc_native_macos_clean_build`, `test_acc_interactive_playability` | Final milestone gate verification | Sentinel sign-off verification |
| 48 | Adversarial Coverage Hardening | M4 | `test_adv_palette_corruption_resilience`, `test_adv_sprite_out_of_bounds_clipping`, `test_adv_coordinate_extreme_values`, `test_adv_maximum_entity_saturation`, `test_adv_rapid_packet_injection` | `test_adv_simultaneous_bridge_collapse_and_punch`, `test_adv_thief_dive_into_collapsing_base`, `test_adv_bomb_defuse_while_detonating`, `test_adv_fire_ricochet_infinite_loop_prevention`, `test_adv_allied_split_during_active_theft` | Extreme stress and adversarial scenarios | Challenger audit gap closure |
| 49 | Water Splash & Ant Drowning Sequences | M2 | `test_dsplash_anim_40_sprites_121_125`, `test_dsplash_sound_71_trigger`, `test_drown_22_subitem_agdr301_worker`, `test_drown_22_subitem_afdr301_fire`, `test_drown_sound_72_antdrown_at_subitem_1` | `test_drown_22_subitem_abdr301_bomber`, `test_drown_22_subitem_acdr301_combat`, `test_drown_22_subitem_atdr301_thief`, `test_drown_subitems_6_21_bubbles_1223_1227`, `test_drown_swimmer_immunity_no_drown_anim` | Combat punch into deep water -> Splash & Drown | Deep water hazard simulation |

---

## 4. Coverage Thresholds & Quality Gates

The E2E test suite strictly enforces the following minimum quantitative thresholds:

1. **Tier 1 (Feature Coverage):**
   - Minimum **5 test cases per feature** across all 49 features in the project inventory.
   - Total Tier 1 Test Cases: **>= 245 test cases**.
2. **Tier 2 (Boundary & Corner Cases):**
   - Minimum **5 boundary/corner test cases per feature** across all 49 features.
   - Total Tier 2 Test Cases: **>= 245 test cases**.
3. **Tier 3 (Cross-Feature Combinations):**
   - Pairwise interaction scenarios covering all critical inter-system physics, abilities, and rule combinations.
   - Total Tier 3 Test Cases: **>= 30 scenarios**.
4. **Tier 4 (Real-World Workload Scenarios):**
   - Complete end-to-end game playthroughs, match duration stress runs, and scorecard validations.
   - Total Tier 4 Test Cases: **>= 10 comprehensive scenarios**.
5. **Quality Gate Requirement:**
   - **100% Pass Rate:** Every test in Tiers 1 through 4 must pass with zero crashes, zero assertion failures, zero memory leaks, and zero undefined behavior.

---

## 5. Test Architecture & Runner Invocation

### 5.1 Test Suite Directory Structure
```
tests/e2e/
├── CMakeLists.txt              # CMake build definition for e2e_runner
├── e2e_framework.hpp           # Header-only lightweight E2E test harness & assertions
├── e2e_model.hpp               # Authoritative requirement oracle & contract models
├── tier1_assets.cpp            # Tier 1 tests: Features 1-6
├── tier1_simulation.cpp        # Tier 1 tests: Features 7-33, 49
├── tier1_app_hud.cpp           # Tier 1 tests: Features 34-45
├── tier2_boundaries.cpp        # Tier 2 tests: Boundary & Corner Cases across Features 1-49
├── tier3_pairwise.cpp          # Tier 3 tests: Cross-Feature Combinatorial Interactions
├── tier4_scenarios.cpp         # Tier 4 tests: Real-World Scenarios & Full Match Lifecycle
└── e2e_main.cpp                # CLI entry point, tier filters, timing, summary reporting
```

### 5.2 Build & Execution Commands

#### Standard CMake Build & Run
```bash
# From project root (/Users/dchadd/Desktop/Ants-Mac):
cmake -S tests/e2e -B build_e2e
cmake --build build_e2e --parallel

# Execute all tests across all tiers (Tiers 1-4):
./build_e2e/e2e_runner --all

# Execute individual tiers:
./build_e2e/e2e_runner --tier 1
./build_e2e/e2e_runner --tier 2
./build_e2e/e2e_runner --tier 3
./build_e2e/e2e_runner --tier 4
```

#### Standalone Clang++ One-Step Build & Run
For environments where CMake is bypassed or for instant standalone compilation:
```bash
clang++ -std=c++17 -O2 -Itests/e2e \
    tests/e2e/e2e_main.cpp \
    tests/e2e/tier1_assets.cpp \
    tests/e2e/tier1_simulation.cpp \
    tests/e2e/tier1_app_hud.cpp \
    tests/e2e/tier2_boundaries.cpp \
    tests/e2e/tier3_pairwise.cpp \
    tests/e2e/tier4_scenarios.cpp \
    -o tests/e2e/e2e_runner

./tests/e2e/e2e_runner --all
```

### 5.3 Test Execution Output & Exit Codes
- **Exit Code 0:** All executed tests passed successfully.
- **Exit Code > 0:** Number of failed test cases.
- **Console Reporting:** Prints test names, execution time in milliseconds, tier subtotals, failure diagnostics with file and line numbers, and a grand summary table.
