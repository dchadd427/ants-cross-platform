// Application integration tests: suite 12 goes on (unit selection and occupied tile collision handling, part 3)
#include "app_integration_fixtures.hpp"

void run_suite_12_unit_selection_and_occupied_tile_movement_3() {
    TEST_CASE("12.74: A Pile-Up Is Thrown Apart With The gh Clip; Only Ants That Are Not The Viewer's Show The Dust Cloud (battle) And Sound 3, Briefly") {
        for (int viewer = 0; viewer < 2; ++viewer) {
            SimulationEngine sim;
            ants::assets::LevelData lvl;
            std::string lvl_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
            ASSERT_TRUE(lvl.load_lvl(lvl_path));
            sim.init(lvl, 42);
            sim.set_viewing_player_id(static_cast<uint8_t>(viewer));

            // The victim of a melee blow lands on a tile where an enemy ant stands (pile-up)
            uint32_t a1_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 15});
            uint32_t a2_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 15});
            uint32_t a3_id = sim.spawn_unit(1, AntType::Combat, TileCoord{12, 15});
            sim.execute_melee_attack(a1_id, a2_id);

            bool has_battle_effect = false;
            bool got_landing_thump = false;
            int battle_ticks = 0;
            for (int t = 0; t < 60; ++t) {
                sim.tick();
                bool now = false;
                for (const auto& eff : sim.get_world_state().effects) {
                    if (eff.anim_name == "battle") { has_battle_effect = true; now = true; ASSERT_TRUE(eff.looping); }
                }
                if (now) ++battle_ticks;
                if (sim.has_audio_event(SoundID::FlingThumpB)) got_landing_thump = true;
            }
            // Blast puts the dust ball (Cloud54) on the tile of a pile-up of ants that are not the viewer's own (the viewer is
            // player 1 in the second run, whose ants are the ones thrown apart: no cloud); it is gone within about a second
            ASSERT_EQ(has_battle_effect, viewer == 0);
            ASSERT_EQ(sim.has_audio_event(SoundID::CombatNetFairy), viewer == 0);
            ASSERT_TRUE(battle_ticks < 30);
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));
            ASSERT_TRUE(got_landing_thump);
            if (viewer == 0) {                                                        // the cloud owns its sound (622 ms long, the clip loops every 270 ms) and takes it along when it is removed
                uint32_t cloud_owner = 0;
                std::vector<AudioEvent> timeline = sim.poll_audio_events();
                for (const auto& e : timeline) {
                    if (!e.stop && e.sound_id == SoundID::CombatNetFairy) cloud_owner = e.owner;
                }
                ASSERT_TRUE(cloud_owner != 0);
                bool cloud_stop = false;
                for (const auto& e : timeline) {
                    if (e.stop && e.owner == cloud_owner) cloud_stop = true;
                }
                ASSERT_TRUE(cloud_stop);
            }

            // Ensure the ants ended on different discrete tiles
            const auto& a2 = sim.get_unit(a2_id);
            const auto& a3 = sim.get_unit(a3_id);
            ASSERT_FALSE(a2.pos == a3.pos);
            ASSERT_FALSE(a2.pos == sim.get_unit(a1_id).pos);
        }
    } TEST_END();

    TEST_CASE("12.75: Original Click Sounds: buttonclick (0) on Buttons, navbuttonclick (89) on Pedestals Only, Silent Toggles") {
        // The pressed-state animations of the original carry the click: sound 0 (buttonclick.wav) for the top bar, the
        // send-to buttons, the quit dialog, the setup screen and the results screen; sound 89 (navbuttonclick.wav) only
        // in the pedestal press animations; the option toggles, breturn3, qh_return3 and qh_start3 are silent; the stop
        // button carries antstop (61) only.
        ASSERT_EQ(SoundID::NavButtonClick, 89u);
        ASSERT_EQ(SoundID::ButtonClick, 0u);

        // 1. HUD buttons
        {
            HUD hud;
            hud.init(0);
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            ViewportCamera camera{0, 0};

            std::vector<uint32_t> played_sounds;
            hud.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            // Help button (476, 7, 46, 23)
            played_sounds.clear();
            hud.handle_mouse_down(485, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);

            // Quick help overlay dismiss: qh_return3 is silent
            played_sounds.clear();
            hud.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_TRUE(played_sounds.empty());

            // Options button (525, 7, 52, 23)
            played_sounds.clear();
            hud.handle_mouse_down(540, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);

            // Options screen: the Chat ON / OFF toggles and the Return button (breturn3) are silent
            played_sounds.clear();
            hud.handle_mouse_down(126, 300, SDL_BUTTON_LEFT, sim, camera, 0);    // chat ON toggle
            hud.handle_mouse_down(175, 300, SDL_BUTTON_LEFT, sim, camera, 0);    // chat OFF toggle
            hud.handle_mouse_down(400, 440, SDL_BUTTON_LEFT, sim, camera, 0);    // Return button (351..449, 425..451)
            ASSERT_TRUE(played_sounds.empty());
            hud.handle_mouse_up(400, 440, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit button (579, 7, 46, 23) -> opens quit dialog
            played_sounds.clear();
            hud.handle_mouse_down(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
            hud.handle_mouse_up(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit dialog "No" button (292, 260, 49, 24): no3 carries sound 0
            played_sounds.clear();
            hud.handle_mouse_down(310, 270, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
            hud.handle_mouse_up(310, 270, SDL_BUTTON_LEFT, sim, camera, 0);

            // Spawn and select friendly ant
            uint32_t a_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            hud.select_ant(a_id, false);

            // Move Pedestal (slot 1: (482, 152) - (525, 225)): the press animation butmov2d carries sound 89
            played_sounds.clear();
            hud.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Stop button (595, 180, 32, 50): antstop (61) and no click
            played_sounds.clear();
            hud.handle_mouse_down(610, 190, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            bool saw_nav = false;
            bool saw_stop = false;
            for (auto s : played_sounds) {
                if (s == SoundID::NavButtonClick || s == SoundID::ButtonClick) saw_nav = true;
                if (s == SoundID::AntStop) saw_stop = true;
            }
            ASSERT_FALSE(saw_nav);
            ASSERT_TRUE(saw_stop);

            // The Stop pedestal locks the mouse for 250 ms (5 ticks, FUN_01028bdd); afterwards the buttons answer again
            hud.handle_mouse_up(610, 190, SDL_BUTTON_LEFT, sim, camera, 0);
            hud.update(sim.get_world_state(), 5);

            // Chat [All] button (532, 443, 44, 24): butalld carries sound 0
            played_sounds.clear();
            hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
        }

        // 2. MapSelectScreen buttons: the click of a pressed picture plays at the PRESS (the action comes with the release); the fog pair is silent, and the places
        // of the invented targets (the Drop button) are no buttons
        {
            MapSelectScreen screen;
            std::vector<uint32_t> played_sounds;
            screen.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            struct Case { int32_t x; int32_t y; bool silent; };
            const Case cases[] = {
                { MapSelectScreen::BTN_UP_X + 10, MapSelectScreen::BTN_UP_Y + 10, false },
                { MapSelectScreen::BTN_DOWN_X + 10, MapSelectScreen::BTN_DOWN_Y + 10, false },
                { MapSelectScreen::BTN_FOW_ON_X + 5, MapSelectScreen::BTN_FOW_ON_Y + 5, true },
                { MapSelectScreen::BTN_FOW_OFF_X + 5, MapSelectScreen::BTN_FOW_OFF_Y + 5, true },
                { 576 + 10, 192 + 10, true },                                       // where the remake's invented Drop button was
                { MapSelectScreen::BTN_QUIT_X + 10, MapSelectScreen::BTN_QUIT_Y + 10, false },
                { MapSelectScreen::BTN_START_X + 10, MapSelectScreen::BTN_START_Y + 10, false },
            };
            for (const auto& c : cases) {
                played_sounds.clear();
                screen.handle_mouse_motion(c.x, c.y);
                screen.handle_mouse_down(c.x, c.y, SDL_BUTTON_LEFT);
                if (c.silent) {
                    ASSERT_TRUE(played_sounds.empty());
                } else {
                    ASSERT_FALSE(played_sounds.empty());
                    ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
                }
                screen.handle_mouse_up(c.x, c.y, SDL_BUTTON_LEFT);
            }
        }

        // 3. ScorecardModal leave button: leave3 carries sound 0
        {
            ScorecardModal modal;
            MatchResult mr{};
            mr.is_over = true;
            modal.show(mr, 0);
            modal.update(0.25f);                                            // the Leave button exists once the rows are built
            std::vector<uint32_t> played_sounds;
            modal.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            played_sounds.clear();
            modal.handle_mouse_down(550, 20);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::ButtonClick);
        }
    } TEST_END();

    TEST_CASE("12.76b Fog Of War Reveals Only From Ant Position Updates Of The Viewer And Its Teammate: No Hill Box, Nothing When A Team Forms (Ants.exe FUN_0101a93a -> FUN_01006af4)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        sim.set_fog_of_war_enabled(true);
        sim.set_viewing_player_id(0);
        const uint32_t mine = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        const uint32_t friend_ant = sim.spawn_unit(2, AntType::Worker, TileCoord{50, 10});
        sim.spawn_unit(1, AntType::Worker, TileCoord{30, 45});
        sim.tick();
        {
            const auto& w = sim.get_world_state();
            // the own ant reveals its 13 x 13 square; the own hill at (20, 20) reveals nothing by itself (it used to reveal 16 x 16 tiles around it)
            ASSERT_TRUE(w.is_tile_revealed(5, 5) && w.is_tile_revealed(11, 11) && w.is_tile_revealed(0, 0));
            ASSERT_FALSE(w.is_tile_revealed(12, 5));
            ASSERT_FALSE(w.is_tile_revealed(20, 20));
            ASSERT_FALSE(w.is_tile_revealed(22, 22));
            // another team's ant reveals nothing
            ASSERT_FALSE(w.is_tile_revealed(50, 10));
            ASSERT_FALSE(w.is_tile_revealed(30, 45));
        }
        // a team forms: the ally's ant stands where it stood, so nothing is revealed (the original reveals only when the ally's ant SetPos runs)
        sim.form_alliance(0, 2);
        sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_FALSE(w.is_tile_revealed(50, 10));
            ASSERT_FALSE(w.is_tile_revealed(47, 10));
        }
        // it moves: its position update now reveals the square around its new tile (and only that one)
        sim.issue_move_order(friend_ant, TileCoord{50, 12});
        for (int i = 0; i < 60; ++i) sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_TRUE(w.is_tile_revealed(50, 12) && w.is_tile_revealed(56, 18) && w.is_tile_revealed(44, 6));
            ASSERT_FALSE(w.is_tile_revealed(30, 45));
        }
        // the own ant walks: every tile change reveals a new square, what was revealed stays
        sim.issue_move_order(mine, TileCoord{9, 5});
        for (int i = 0; i < 80; ++i) sim.tick();
        {
            const auto& w = sim.get_world_state();
            ASSERT_TRUE(w.is_tile_revealed(0, 0) && w.is_tile_revealed(15, 5));
            ASSERT_FALSE(w.is_tile_revealed(17, 5));
        }
    } TEST_END();

    TEST_CASE("12.76 Authentic Fog of War Sight Radius 6 & Exploration Persistence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_fog_of_war_enabled(true);
        ASSERT_TRUE(sim.is_fog_of_war_enabled());

        sim.set_viewing_player_id(0);
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        ASSERT_GT(ant_id, 0u);

        sim.tick();
        const auto& world = sim.get_world_state();
        ASSERT_TRUE(world.fog_of_war_enabled);

        // Unit tile and radius 6 tiles must be revealed
        ASSERT_TRUE(world.is_tile_revealed(30, 30));
        ASSERT_TRUE(world.is_tile_revealed(30 + 6, 30));
        ASSERT_TRUE(world.is_tile_revealed(30 - 6, 30));
        ASSERT_TRUE(world.is_tile_revealed(30, 30 + 6));
        ASSERT_TRUE(world.is_tile_revealed(30, 30 - 6));

        // Beyond radius 6 must remain shrouded
        ASSERT_FALSE(world.is_tile_revealed(30 + 7, 30));
        ASSERT_FALSE(world.is_tile_revealed(30 - 7, 30));
        ASSERT_FALSE(world.is_tile_revealed(30, 30 + 7));
        ASSERT_FALSE(world.is_tile_revealed(30, 30 - 7));
        ASSERT_FALSE(world.is_tile_revealed(5, 5));

        // Move unit to (31, 30) - previous tiles must stay permanently revealed
        sim.issue_move_order(ant_id, TileCoord{31, 30});
        for (int i = 0; i < 15; ++i) sim.tick();
        const auto& world2 = sim.get_world_state();
        ASSERT_TRUE(world2.is_tile_revealed(30, 30)); // Permanent exploration
        ASSERT_TRUE(world2.is_tile_revealed(31, 30));

        // Authentic 1998 Fog of War Autotiling & Dither Mapping Verification (Ants.exe 0x1008760..0x10087da)
        // c = 1 if neighbor is Fog, 0 if neighbor is Revealed
        auto compute_idx = [](int32_t c0, int32_t c1, int32_t c2, int32_t c3) {
            return ((c0 * 2 + c1) * 2 + 2 + c2) * 2 + c3;
        };

        // Deep interior fog: all neighbors are fog (1, 1, 1, 1) -> idx 19 -> dither0.bmp (352, solid 50% stipple)
        ASSERT_EQ(compute_idx(1, 1, 1, 1), 19);

        // Boundary fog: North neighbor is revealed (0, 1, 1, 1) -> idx 11 -> dither1.bmp (353, top edge fade)
        ASSERT_EQ(compute_idx(0, 1, 1, 1), 11);

        // Isolated fog island: all neighbors are revealed (0, 0, 0, 0) -> idx 4 -> dither13.bmp (365, rounded island dot)
        ASSERT_EQ(compute_idx(0, 0, 0, 0), 4);
    } TEST_END();

    TEST_CASE("12.77 Hatching Emergence Isolation of exithill.wav") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        sim.set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 10);
        sim.set_hatch_delay_ticks(4);

        // Hatch egg with 200 points
        sim.clear_audio_events();
        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // Advance simulation through incubation delay until newborn surfaces
        bool hatched_exit_sound = false;
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                hatched_exit_sound = true;
                break;
            }
        }
        ASSERT_TRUE(hatched_exit_sound);

        // Now move a unit to base to heal/eat
        sim.clear_audio_events();
        uint32_t a_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto* a = const_cast<AntUnit*>(&sim.get_unit(a_id));
        a->hp = 5; // Damaged to trigger healing dwell

        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        sim.join_base_queue(a_id);

        // Run until unit enters, dwells, heals, and exits
        bool regular_exit_sound = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                regular_exit_sound = true;
            }
        }
        // Exiting after heal/eating must NOT play ExitHill
        ASSERT_FALSE(regular_exit_sound);
    } TEST_END();

    TEST_CASE("12.78 Ability Cooldown Initiation & Uninterruptible Placement") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // 1. Fire Ant: the ignite clip is uninterruptible (afsf)
        uint32_t fire_id = sim.spawn_unit(0, AntType::Fire, TileCoord{20, 20});
        sim.set_tile_flags(21, 20, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.ignite_fire(fire_id, TileCoord{21, 20}, false));
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire);

        // Order move while placing -> silently ignored without CantGo per Ants.exe (uninterruptible)
        sim.clear_audio_events();
        sim.issue_move_order(fire_id, TileCoord{22, 20});
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire); // Uninterruptible
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 2. Bomber Ant: the plant clip is uninterruptible (absb)
        uint32_t bomb_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{25, 25});
        sim.set_tile_flags(26, 25, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.plant_bomb(bomb_id, TileCoord{26, 25}, false));
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb);

        // Order move while placing -> silently ignored without CantGo per Ants.exe (uninterruptible)
        sim.clear_audio_events();
        sim.issue_move_order(bomb_id, TileCoord{27, 25});
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // Uninterruptible
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 3. An enemy attack is not refused: the bomber loses its hit point at the contact, the placing is cancelled
        //    without any change of the world (nothing is planted) and the bomber is thrown like any other victim
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Worker, TileCoord{24, 25});

        int32_t hp_before = sim.get_unit(bomb_id).hp;
        int32_t x_before = sim.get_unit(bomb_id).pos.x;

        sim.execute_melee_attack(enemy_id, bomb_id);
        ASSERT_LT(sim.get_unit(bomb_id).hp, hp_before); // Took damage
        ASSERT_NE(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // The placing is cancelled
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(bomb_id).pos.x != x_before; }) >= 0); // and it is thrown
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{26, 25}));
    } TEST_END();

    TEST_CASE("12.79 A Fire Wall Stands On Its Tile; An Audio Event Addressed To One Player Reaches Only That Player") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Ignite fire at (10, 10)
        uint32_t fire_ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 9});
        sim.set_tile_flags(10, 10, 0x06);
        ASSERT_TRUE(sim.ignite_fire(fire_ant_id, TileCoord{10, 10}, false));
        for (int t = 0; t < 40; ++t) sim.tick();
        ASSERT_TRUE(sim.grid().has_fire_at(TileCoord{10, 10}));

        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        archive.load_chd(chd_path);
        HUD hud;
        hud.init(0);

        // Targeted audio event routing in AudioMixer
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        std::vector<AudioEvent> events;
        // AlliancePro targeted to recipient player 1
        events.push_back(AudioEvent{SoundID::AlliancePro, 0, 0, 1, 1});

        // Player 0 should ignore targeted event
        mixer.ingest_simulation_events(events, 0);
        ASSERT_EQ(mixer.active_channel_count(), 0u);

        // Player 1 should receive targeted event
        mixer.ingest_simulation_events(events, 1);
        ASSERT_EQ(mixer.active_channel_count(), 1u);
    } TEST_END();

    TEST_CASE("12.98: An Attack Order Ends After The One Blow: No Pursuit, The Attacker Stays In Its Row") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder atk_order{};
        atk_order.ant_id = attacker_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(atk_order);

        // The blow throws the defender one tile East to (12, 10)
        const auto& def = sim.get_unit(defender_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.state == UnitState::Flinch; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.pos.x == 12; }) >= 0);
        ASSERT_EQ(def.pos.y, 10);

        // The attacker does not pursue: it stays on the same tile and in the same row
        run_ms(sim, 3000);
        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_EQ(atk.pos.y, 10);
        ASSERT_EQ(atk.pos.x, 10);
        ASSERT_EQ(def.hp, 9);
    } TEST_END();

    TEST_CASE("12.99: Bomb Dud Burn Animation Cycle (?bu overlay, 1150 ms for a worker, then the stun)") {
        bool tested = false;
        for (uint32_t seed = 1; seed < 100 && !tested; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            auto& unit = sim.get_unit(worker_id);
            sim.grid_mut().place_bomb(15, 15, 1);
            sim.trigger_bomb_detonation(worker_id, TileCoord{15, 15});
            if (!unit.knock_flag) continue;   // a real blast
            tested = true;

            // The dud freezes the ant under the burn overlay; the world snapshot tells the renderer how long it burns
            ASSERT_TRUE(unit.frozen);
            ASSERT_EQ(unit.hp, 8);

            // The overlay clip (11 frames, 1150 ms) plays; the ant is frozen all along
            run_ms(sim, 1100);
            ASSERT_TRUE(unit.frozen);
            run_ms(sim, 100);

            // The end of the burn frees the ant and stuns it (a3 stun clip)
            ASSERT_FALSE(unit.frozen);
            ASSERT_EQ(unit.state, UnitState::Stunned);
            ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return unit.state == UnitState::Idle; }) >= 0);
        }
        ASSERT_TRUE(tested);
    } TEST_END();

    TEST_CASE("12.100: 8-Directional Diagonal Attack Adjacency & Approach Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Attacker already diagonally adjacent at (10, 10) to defender at (11, 11)
        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});

        AntOrder atk_order{};
        atk_order.ant_id = attacker_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(atk_order);

        // The attack order walks one step onto the defender's tile: the contact makes the attacker face it
        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return atk.state == UnitState::Attacking; }) >= 0);
        ASSERT_EQ(atk.facing, Direction::SouthEast);
        ASSERT_EQ(atk.pos, (TileCoord{10, 10}));
        ASSERT_EQ(sim.get_unit(defender_id).hp, 9);

        // 2. Attacker commanded to attack from a distance (8, 8): it walks up to a neighbour tile first
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t defender2_id = sim2.spawn_unit(1, AntType::Worker, TileCoord{11, 11});
        uint32_t distant_atk_id = sim2.spawn_unit(0, AntType::Worker, TileCoord{8, 8});
        AntOrder dist_order{};
        dist_order.ant_id = distant_atk_id;
        dist_order.type = OrderType::Attack;
        dist_order.target_entity_id = static_cast<int32_t>(defender2_id);
        sim2.issue_order(dist_order);

        const auto& dist_unit = sim2.get_unit(distant_atk_id);
        ASSERT_TRUE(wait_ms(sim2, 3000, [&]() { return dist_unit.state == UnitState::Walking; }) >= 0);
        ASSERT_EQ(sim2.get_unit(defender2_id).hp, 10);   // nothing is hit while it walks
        ASSERT_TRUE(wait_ms(sim2, 6000, [&]() { return sim2.get_unit(defender2_id).hp < 10; }) >= 0);
        // The blow came from a tile next to the defender (Chebyshev distance 1)
        ASSERT_TRUE(dist_unit.pos.chebyshev_dist(TileCoord{11, 11}) == 1);
    } TEST_END();

    TEST_CASE("12.101: Complete Attack Animation Playback Across Full Duration (a?at clip, 6 frames)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_EQ(atk.state, UnitState::Attacking);
        ASSERT_EQ(atk.loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(atk.facing, Direction::East);

        // The attack clip of a worker (6 frames) plays; its length is the sum of the frame durations
        const auto clip = movement::action_clip(movement::ActionClip::Attack, static_cast<uint8_t>(AntType::Worker), 2, false);
        ASSERT_TRUE(clip.valid());
        ASSERT_EQ(clip.count, 6u);
        const int clip_ms = static_cast<int>(clip.total_duration_ms());

        // The attacker stays in the attack action for the whole clip (the first frame is booked twice), then idles
        const int done = wait_ms(sim, 3000, [&]() { return sim.get_unit(attacker_id).state != UnitState::Attacking; });
        ASSERT_TRUE(done >= clip_ms - 100);
        ASSERT_TRUE(done <= clip_ms + 150);
        ASSERT_EQ(sim.get_unit(attacker_id).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.102: 1-Tile Pushback Slide Follows The gh Clip (24 px at 300 ms, 8 px at 400 ms after the contact)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& def = sim.get_unit(defender_id);
        // At the contact the victim stands still on its tile facing the attacker
        ASSERT_EQ(def.pixel_x, 11 * 32 + 16); // 368
        ASSERT_NE(def.state, UnitState::Flinch);

        // Strike frame (attack clip event 4, 200 ms): the gh clip starts, the ant has not moved yet
        for (int i = 0; i < 4; ++i) sim.tick();
        ASSERT_EQ(def.state, UnitState::Flinch);
        ASSERT_EQ(def.pixel_x, 368);

        // The clip moves the ant in two steps (frame displacements of the table): 24 px at 300 ms, 8 px at 400 ms
        sim.tick(); sim.tick();
        ASSERT_EQ(def.pixel_x, 392);
        sim.tick(); sim.tick();
        ASSERT_EQ(def.pixel_x, 400); // 12 * 32 + 16, the landing tile centre

        // The ant stays at 400 px while the remaining recovery frames play, then it is idle again
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pixel_x, 400);
    } TEST_END();

    TEST_CASE("12.103: TINY.LVL Tile (18, 17) Passability & 10-Tick Bounce Parity") {
        // Part A: Tile (18, 17) passability on authentic TINY.LVL (Layer 2 broken2 flat debris)
        std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(tiny_path));

        SimulationEngine sim;
        sim.init(lvl, 42);

        // The remake draws broken2 as flat debris (no obstacle overlay), but tile (18, 17) carries the layer-1
        // solid bit in TINY.LVL (it is part of the footprint of the broken2 object anchored at (18, 15)), and
        // the original's CanEnter / StepCost (FUN_0100cf0f) never let an ant onto it.
        ASSERT_TRUE(sim.grid().in_bounds(18, 17));
        const auto& cell = sim.grid().get_cell(18, 17);
        ASSERT_FALSE(cell.is_obstacle_overlay);
        ASSERT_TRUE(cell.is_passable());
        ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{18, 17}));

        // Ordering an ant onto it from neighbour (18, 18) sends it to the first enterable tile of the ring scan
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 18});
        ASSERT_TRUE(ant > 0);
        sim.issue_move_order(ant, TileCoord{18, 17});
        ASSERT_EQ(sim.get_unit(ant).final_dest, (TileCoord{17, 16}));
        bool entered = false;
        for (int i = 0; i < 60; ++i) {
            sim.tick();
            if (sim.get_unit(ant).pos == TileCoord{18, 17}) entered = true;
        }
        ASSERT_FALSE(entered);
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{17, 16}));

        // Part B: an ant that lands on an occupied tile causes a pile-up (Blast 0, 7): both ants are thrown to distinct free
        // neighbours with the gh clip; the dust ball (Cloud54) shows for ants that are not the viewer's own (here player 1's)
        SimulationEngine sim_pile;
        sim_pile.init_test_world(60, 60, 100, 60000);
        uint32_t hitter = sim_pile.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t ant1 = sim_pile.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        uint32_t ant2 = sim_pile.spawn_unit(1, AntType::Worker, TileCoord{12, 10});
        sim_pile.execute_melee_attack(hitter, ant1);

        bool found_battle = false;
        bool dispersed = false;
        for (int i = 0; i < 80; ++i) {
            sim_pile.tick();
            for (const auto& eff : sim_pile.get_world_state().effects) {
                if (eff.anim_name == "battle") found_battle = true;
            }
            const auto& u1 = sim_pile.get_unit(ant1);
            const auto& u2 = sim_pile.get_unit(ant2);
            if (u1.state == UnitState::Flinch && u2.state == UnitState::Flinch && !(u1.pos == u2.pos)) dispersed = true;
        }
        ASSERT_TRUE(found_battle);
        ASSERT_TRUE(dispersed);
        ASSERT_FALSE(sim_pile.get_unit(ant1).pos == sim_pile.get_unit(ant2).pos);
    } TEST_END();

    TEST_CASE("12.74b: The Dust Ball Belongs To The Pile-Up Block Alone: A Foreign Ant Thrown Onto A Fire Wall Hops And Loses 1 hp Without It (Ants.exe 0x101bbdd - 0x101bbf4: the fire wall block runs for the viewer's own ants only)") {
        auto battle_cloud_seen = [](SimulationEngine& sim, uint32_t ticks) {
            bool seen = false;
            for (uint32_t t = 0; t < ticks; ++t) {
                sim.tick();
                for (const auto& eff : sim.get_world_state().effects) if (eff.anim_name == "battle") seen = true;
            }
            return seen;
        };
        // a fire wall under player 1's ant, the viewer is player 0: Blast(1, owner) runs without the dust ball, the ant hops and loses 1 hp
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            sim.grid_mut().place_firewall(15, 15, 1);
            const uint32_t foreign = sim.spawn_unit(1, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(foreign, 1, 0);
            ASSERT_EQ(sim.get_unit(foreign).hp, 9);
            ASSERT_EQ(sim.get_unit(foreign).state, UnitState::Flinch);
            ASSERT_FALSE(battle_cloud_seen(sim, 60));
            ASSERT_FALSE(sim.has_audio_event(SoundID::CombatNetFairy));
        }
        // and the viewer's own ant makes none either
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            sim.grid_mut().place_firewall(15, 15, 1);
            const uint32_t own = sim.spawn_unit(0, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(own, 1, 0);
            ASSERT_FALSE(battle_cloud_seen(sim, 60));
        }
        // the pile-up block keeps it for ants that are not the viewer's own
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.set_viewing_player_id(0);
            const uint32_t a = sim.spawn_unit(1, AntType::Worker, {30, 30});
            sim.spawn_unit(2, AntType::Worker, {30, 30});
            (void)a;
            sim.blast_tile_for_test(TileCoord{30, 30});
            bool seen = false;
            for (const auto& eff : sim.get_world_state().effects) if (eff.anim_name == "battle") seen = true;
            ASSERT_TRUE(seen || battle_cloud_seen(sim, 5));
        }
    } TEST_END();

    TEST_CASE("12.104: Pile-Up Dispersal Never Hides An Ant; Hazard Landings Of The Thrown Ants (water, bomb)") {
        // Part A: no ant is ever concealed (the original has no scuffle ball): all ants stay visible while thrown apart
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            uint32_t hitter = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 20});
            uint32_t a1 = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
            sim.execute_melee_attack(hitter, a1);

            for (int t = 0; t < 80; ++t) {
                sim.tick();
                // no ant is ever hidden: every living ant of the pile-up stays in the world snapshot
                const auto& ws = sim.get_world_state();
                int visible = 0;
                for (const auto& snap : ws.ants) {
                    if (snap.id == hitter || snap.id == a1 || snap.id == a2) ++visible;
                }
                ASSERT_EQ(visible, 3);
            }
            ASSERT_FALSE(sim.get_unit(a1).pos == sim.get_unit(a2).pos);
        }

        // Part B: Water Landing - the ant thrown by a pile-up into water: non-swimmer drowns, swimmer splashes
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Surround tile (30, 30) with rock obstacles except north tile (30, 29) which is water
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_WATER);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            // Two ants on (30, 30): the blast (Blast 0) throws each into the only free tile: the water north of them
            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
            sim.blast_tile_for_test(TileCoord{30, 30});

            // The blast throws the ant onto its only free neighbour, the water tile north of it: the landing drowns it
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).state == UnitState::Drowning; }) >= 0);
            ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{30, 29}));
            run_ms(sim, 250);
            ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        }

        {
            // A swimmer thrown into the water splashes and survives
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_WATER);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t a1 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{30, 30});
            (void)a1;
            sim.blast_tile_for_test(TileCoord{30, 30});
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).pos == TileCoord{30, 29} &&
                                                          sim.get_unit(a1).state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(sim.get_unit(a1).in_water);
            ASSERT_TRUE(sim.get_unit(a1).hp > 0);
        }

        // Part C: Bomb Detonation on the landing tile of a thrown ant
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Surround tile (40, 40) with obstacles except east tile (41, 40) which has a bomb
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{40 + dx, 40 + dy};
                    if (dx == 1 && dy == 0) {
                        sim.grid_mut().place_bomb(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), 1);
                    } else {
                        sim.grid_mut().set_terrain(c.x, c.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
            sim.blast_tile_for_test(TileCoord{40, 40});

            // The bomb goes off when the flight ends on it (the bomb block comes first at the landing)
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.grid().has_bomb_at(TileCoord{41, 40}); }) >= 0);
            ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));
            // The ant that landed on the bomb took the 2 hits of the blast
            ASSERT_EQ(sim.get_unit(a1).hp, 8);
        }
    } TEST_END();

    TEST_CASE("12.105: Authentic Attack Audio Sequencing, Sound 75/78/79/83/57 From The Attack Clip & Flinch FlyThump Events") {
        // Part A: Worker Ant Melee Attack Sound Sequence: the sound of the attack clip (75) plays at its frame 1,
        // the victim's gh clip plays 64 when it starts (strike frame) and 65 at its landing frame
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
            // Ensure target has enough HP to flinch and survive
            sim.get_unit(a2).hp = 4;

            sim.execute_melee_attack(a1, a2); // Contact
            ASSERT_FALSE(sim.has_audio_event(SoundID::AttackAlt));   // the clip has not reached its sound frame
            ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));

            // 1. The attack clip plays AttackAlt (Sound 75) at its second frame
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::AttackAlt); }) >= 0);

            // 2. Target enters Flinch and triggers FlingThumpA (Sound 64 / flythumpa.wav) at launch
            const auto& victim = sim.get_unit(a2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return victim.state == UnitState::Flinch; }) >= 0);
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

            // 3. The clip triggers FlingThumpB (Sound 65 / flythumpb.wav) at its landing frame (500 ms after the contact)
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_audio_event(SoundID::FlingThumpB); }) >= 0);
            ASSERT_EQ(sim.get_unit(a2).pos, (TileCoord{22, 20}));
        }

        // Part B: Swimmer and Thief Type-Specific Attack Sound Verification (sound of the attacker's clip)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
            uint32_t target1 = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            sim.get_unit(target1).hp = 4;
            sim.execute_melee_attack(swimmer, target1);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::WaterAttack); }) >= 0); // Sound 79 (waterattack.wav)

            uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{15, 10});
            uint32_t target2 = sim.spawn_unit(1, AntType::Worker, TileCoord{16, 10});
            sim.get_unit(target2).hp = 4;
            sim.execute_melee_attack(thief, target2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::ThiefWhip); }) >= 0); // Sound 83 (theifwhip.wav)
        }

        // Part C: Combat Ant Heavy Punch Knockback: 78 in the punch clip, 64 / 65 in the gb flight, no stun after it
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
            sim.get_unit(victim).hp = 4;

            sim.execute_melee_attack(combat, victim); // Combat punch connects
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_audio_event(SoundID::HeavyPunch) &&
                                                          sim.has_audio_event(SoundID::FlingThumpA); }) >= 0);

            // The flight lands: FlingThumpB (65); the ant is thrown 4 tiles East and stands up again (no stun state)
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_audio_event(SoundID::FlingThumpB); }) >= 0);
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(victim).state == UnitState::Idle; }) >= 0);
            ASSERT_EQ(sim.get_unit(victim).pos, (TileCoord{35, 30}));
        }
    } TEST_END();

    TEST_CASE("12.106: Multi-Directional Attack Registration, Victim Facing & No Re-Collision Loop After The Blow") {
        // 1. Attack registration from all 8 directions against victim facing away:
        // When attacked, regardless of which direction victim was facing, victim takes damage and turns to face attacker!
        struct AttackDirectionTest {
            int32_t att_dx;
            int32_t att_dy;
            Direction victim_initial_facing;
            Direction expected_victim_turned_facing;
        };

        std::vector<AttackDirectionTest> dirs = {
            {-1,  0, Direction::North, Direction::West},       // Attacker to West, victim faces North -> turns West
            { 1,  0, Direction::North, Direction::East},       // Attacker to East, victim faces North -> turns East
            { 0, -1, Direction::South, Direction::North},      // Attacker to North, victim faces South -> turns North
            { 0,  1, Direction::North, Direction::South},      // Attacker to South, victim faces North -> turns South
            {-1, -1, Direction::South, Direction::NorthWest},  // Attacker NorthWest, victim faces South -> turns NorthWest
            { 1, -1, Direction::South, Direction::NorthEast},  // Attacker NorthEast, victim faces South -> turns NorthEast
            {-1,  1, Direction::North, Direction::SouthWest},  // Attacker SouthWest, victim faces North -> turns SouthWest
            { 1,  1, Direction::North, Direction::SouthEast},  // Attacker SouthEast, victim faces North -> turns SouthEast
        };

        for (const auto& d : dirs) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            TileCoord victim_pos{20, 20};
            TileCoord att_pos{20 + d.att_dx, 20 + d.att_dy};

            uint32_t att = sim.spawn_unit(0, AntType::Worker, att_pos);
            uint32_t vic = sim.spawn_unit(1, AntType::Combat, victim_pos);

            sim.get_unit(vic).facing = d.victim_initial_facing;
            uint32_t initial_hp = sim.get_unit(vic).hp;

            // Worker strikes victim from side / behind:
            sim.execute_melee_attack(att, vic);

            // Victim took damage despite facing away:
            ASSERT_LT(sim.get_unit(vic).hp, initial_hp);

            // Victim turned and is now facing directly towards the attacker:
            ASSERT_EQ(sim.get_unit(vic).facing, d.expected_victim_turned_facing);
        }

        // 2. Attack order from the side / rear: the attacker walks up, the contact comes with the step into the tile
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Friendly attacker at (18, 20), enemy stationary at (20, 20) facing South
            uint32_t att = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
            uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            sim.get_unit(vic).facing = Direction::South;

            AntOrder order;
            order.ant_id = att;
            order.type = OrderType::Attack;
            order.target_x = 20;
            order.target_y = 20;
            order.target_entity_id = static_cast<int32_t>(vic);
            sim.issue_order(order);

            uint32_t initial_vic_hp = sim.get_unit(vic).hp;
            bool hit_connected = false;
            for (int t = 0; t < 100; ++t) {
                sim.tick();
                const auto& att_u = sim.get_unit(att);
                if (att_u.state == UnitState::Walking) {
                    // While walking, no damage can be dealt mid-stride
                    ASSERT_EQ(sim.get_unit(vic).hp, initial_vic_hp);
                }
                if (sim.get_unit(vic).hp < initial_vic_hp) {
                    hit_connected = true;
                    // Strike connects only after completing the walk into the adjacent tile (19, 20)
                    ASSERT_EQ(att_u.pos, (TileCoord{19, 20}));
                    break;
                }
            }
            ASSERT_TRUE(hit_connected);
            // Victim turned West towards attacker
            ASSERT_EQ(sim.get_unit(vic).facing, Direction::West);
        }

        // 3. After a blow both ants settle: nothing collides again and no order stays behind
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});
            sim.execute_melee_attack(a1, a2);

            // Both ants must settle within 4 s without any re-collision loop
            run_ms(sim, 4000);
            ASSERT_EQ(sim.get_unit(a1).state, UnitState::Idle);
            ASSERT_EQ(sim.get_unit(a2).state, UnitState::Idle);
            ASSERT_FALSE(sim.get_unit(a2).engaged);
            ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderNone);
            ASSERT_EQ(sim.get_unit(a2).orig_order, AntUnit::kOrderNone);
            ASSERT_EQ(sim.get_unit(a2).hp, 9);   // only the one blow
        }
    } TEST_END();

    TEST_CASE("12.107: Lethal Food Drops & Grab Food Action Mapping") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});

        // (The healing time of the enter clip and the deposit at its end are golden-tested in test_hill_actions.)

        // 1. Lethal Melee Damage Drops Carried Lunchbox (at the removal of the ant, after its death clip):
        uint32_t attacker = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        auto& v = sim.get_unit(victim);
        v.hp = 2; // Combat punch does 2 damage -> lethal
        v.pick_up_food(1, 50);

        ASSERT_FALSE(sim.grid().has_lunchbox_at(TileCoord{31, 30}));
        sim.execute_melee_attack(attacker, victim);

        // Death is deferred: hp is 0 at the contact, the victim is still thrown 4 tiles East and carries the food
        ASSERT_EQ(v.hp, 0u);
        ASSERT_TRUE(v.is_holding());
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return v.state == UnitState::Dead; }) >= 0);
        ASSERT_TRUE(v.is_holding());
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !v.is_alive(); }) >= 0);
        ASSERT_FALSE(v.is_holding());
        ASSERT_TRUE(sim.grid().has_lunchbox_at(TileCoord{35, 30}));
        ASSERT_EQ(sim.grid().get_lunchbox_points(TileCoord{35, 30}), 50u);

        // 2. Food Harvesting Sequence Action Verification
        uint32_t harvester = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
        auto& h = sim.get_unit(harvester);
        h.state = UnitState::HarvestingFood;
        h.anim_tick = 3;
        const auto& ws = sim.get_world_state();
        const AntSnapshot* snap = nullptr;
        for (const auto& a : ws.ants) {
            if (a.id == harvester) { snap = &a; break; }
        }
        ASSERT_TRUE(snap != nullptr);
        ASSERT_EQ(snap->anim_state, static_cast<uint16_t>(UnitState::HarvestingFood));
    } TEST_END();

    TEST_CASE("12.108: Version Format, Build Id & Fog of War Cursor Concealment Parity") {
        // 1. Verify the FORMAT of the version, not its value. The value lives in one place, the file VERSION (CMake generates ants_app/version.hpp from it) and moves with
        // each change that goes live (docs/WORKFLOW.md), so a test that pinned "v0.1.0" failed at every release for no reason. What stays pinned: the text is
        // "vMAJOR.MINOR.PATCH" (digits only, no leading zeros of a longer number, nothing after the third number), the three numbers agree with it, and the build id names the build.
        {
            const std::string text(ants::VERSION_STRING);
            ASSERT_TRUE(text.size() >= 6);
            ASSERT_EQ(text[0], 'v');
            int numbers[3] = {0, 0, 0};
            int field = 0;
            bool digit_seen = false;
            bool format_ok = true;
            for (size_t i = 1; i < text.size(); ++i) {
                const char c = text[i];
                if (c >= '0' && c <= '9') {
                    if (digit_seen && numbers[field] == 0) format_ok = false;     // "01": a leading zero
                    numbers[field] = numbers[field] * 10 + (c - '0');
                    if (numbers[field] > 100000) format_ok = false;
                    digit_seen = true;
                } else if (c == '.' && digit_seen && field < 2) {
                    ++field;
                    digit_seen = false;
                } else {
                    format_ok = false;
                }
            }
            ASSERT_TRUE(format_ok);
            ASSERT_EQ(field, 2);
            ASSERT_TRUE(digit_seen);                                            // not "v1.2." and not "v1.2"
            ASSERT_EQ(numbers[0], ants::VERSION_MAJOR);
            ASSERT_EQ(numbers[1], ants::VERSION_MINOR);
            ASSERT_EQ(numbers[2], ants::VERSION_PATCH);
            ASSERT_TRUE(ants::VERSION_MAJOR >= 0 && ants::VERSION_MINOR >= 0 && ants::VERSION_PATCH >= 0);

            const std::string build(ants::BUILD_ID);                            // a short commit, the Docker build argument, or "unknown": never empty
            ASSERT_TRUE(!build.empty());
            ASSERT_TRUE(build.size() <= 40);
            for (const char c : build) {
                const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
                ASSERT_TRUE(allowed);
            }
        }

        // 2. Setup simulation world with Fog of War enabled
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_fog_of_war_enabled(true);
        sim.set_viewing_player_id(0);

        // Spawn friendly unit at (10, 10)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        // Spawn enemy unit deep in fog at (50, 50)
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Worker, TileCoord{50, 50});
        (void)enemy_id;
        // Place food deep in fog at (52, 52)
        sim.grid_mut().drop_lunchbox(52, 52, 100);

        // Tick once to update fog reveals around (10, 10)
        sim.tick();
        const auto& ws = sim.get_world_state();
        ASSERT_TRUE(ws.fog_of_war_enabled);
        ASSERT_TRUE(ws.is_tile_revealed(10, 10));
        ASSERT_FALSE(ws.is_tile_revealed(50, 50));
        ASSERT_FALSE(ws.is_tile_revealed(52, 52));

        HUD hud;
        hud.init(0);
        ViewportCamera cam;
        cam.world_x = 0;
        cam.world_y = 0;

        // Position camera so (50, 50) and (52, 52) are cleanly on screen
        cam.world_x = 50 * 32 - 100;
        cam.world_y = 50 * 32 - 100;

        int32_t enemy_screen_x = HUD::PLAYFIELD_X + (50 * 32 + 16) - cam.world_x;
        int32_t enemy_screen_y = HUD::PLAYFIELD_Y + (50 * 32 + 16) - cam.world_y;

        int32_t food_screen_x = HUD::PLAYFIELD_X + (52 * 32 + 16) - cam.world_x;
        int32_t food_screen_y = HUD::PLAYFIELD_Y + (52 * 32 + 16) - cam.world_y;

        // Case A: Friendly unit is selected
        hud.select_ant(friendly_id, false);
        // Hovering over enemy in fog returns Move (c_mov1), NOT Attack (c_attack)!
        CursorType cur_enemy = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_enemy), static_cast<int>(CursorType::Move));

        // Hovering over food in fog returns Move (c_mov1), NOT Food (c_food)!
        CursorType cur_food = hud.evaluate_cursor(food_screen_x, food_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_food), static_cast<int>(CursorType::Move));

        // Case B: No units selected
        hud.clear_selection();
        // Hovering over enemy in fog returns Normal (c_normal), NOT Select (c_select)!
        CursorType cur_no_sel = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_no_sel), static_cast<int>(CursorType::Normal));

        // Case C: When Fog of War is disabled, hovering over enemy returns Attack (when unit selected)
        sim.set_fog_of_war_enabled(false);
        hud.select_ant(friendly_id, false);
        const auto& ws_no_fog = sim.get_world_state();
        CursorType cur_revealed = hud.evaluate_cursor(enemy_screen_x, enemy_screen_y, ws_no_fog, sim.grid(), cam);
        ASSERT_EQ(static_cast<int>(cur_revealed), static_cast<int>(CursorType::Attack));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.109: Mud Animation Cancel ("Mud Humping") Speedup vs Passive Walking
    // ------------------------------------------------------------------------
    TEST_CASE("12.109 Authentic Mud \"Humping\": Re-Ordering Just After Each Tile Boundary Beats Passive Walking") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Mud terrain corridors on row 10 and row 14 from col 10 to 25 bordered by obstacles
        for (int x = 9; x <= 25; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 9}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 11}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 13}).terrain_type = TERRAIN_OBSTACLE;
            sim.grid_mut().get_cell_mut(TileCoord{x, 15}).terrain_type = TERRAIN_OBSTACLE;
        }
        for (int x = 10; x <= 25; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 10}).surface_type = SurfaceType::Mud;
            sim.grid_mut().get_cell_mut(TileCoord{x, 10}).is_mud = true;
            sim.grid_mut().get_cell_mut(TileCoord{x, 14}).surface_type = SurfaceType::Mud;
            sim.grid_mut().get_cell_mut(TileCoord{x, 14}).is_mud = true;
        }

        uint32_t w_passive = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t w_cancel = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 14});

        sim.issue_move_order(w_passive, TileCoord{20, 10});
        sim.issue_move_order(w_cancel, TileCoord{20, 14});

        int ticks_passive = 0;
        int ticks_cancel = 0;
        int forward_snaps = 0;

        for (int t = 1; t <= 600; ++t) {
            sim.tick();

            // The exploit: every order snaps the ant to the centre of the tile under it (GoTo, FUN_0101fc50).
            // Right after crossing into the next tile the ant is still up to 16 px short of that tile's centre,
            // so re-ordering then jumps it forward, which outweighs the restart of the walk on slow mud.
            AntUnit& c = sim.get_unit(w_cancel);
            if (ticks_cancel == 0 && c.loco_action == AntUnit::kActionWalk && (c.pixel_x % 32) < 16) {
                const int32_t before = c.pixel_x;
                sim.issue_move_order(w_cancel, TileCoord{20, 14});
                ASSERT_GT(c.pixel_x, before);
                ++forward_snaps;
            }

            if (ticks_passive == 0 && sim.get_unit(w_passive).pos == TileCoord{20, 10} &&
                sim.get_unit(w_passive).state != UnitState::Walking) {
                ticks_passive = t;
            }
            if (ticks_cancel == 0 && sim.get_unit(w_cancel).pos == TileCoord{20, 14} &&
                sim.get_unit(w_cancel).state != UnitState::Walking) {
                ticks_cancel = t;
            }

            if (ticks_passive > 0 && ticks_cancel > 0) break;
        }

        ASSERT_TRUE(ticks_passive > 0);
        ASSERT_TRUE(ticks_cancel > 0);
        ASSERT_TRUE(forward_snaps >= 5);
        // Mud humping completes faster than passive walking across mud
        ASSERT_TRUE(ticks_cancel < ticks_passive);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.110: Power-Up Standing Immunity to Bounce and Melee Attacks
    // ------------------------------------------------------------------------
    TEST_CASE("12.110 Power-Up Standing Immunity: Not Displaced By A Friendly Ant, Immune To Attack Orders And Auto-Engage") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place Bomber power-up at (15, 15); a friendly ant walks onto it and stands on it (the can't-go trick)
        sim.grid_mut().place_powerup(15, 15, static_cast<uint8_t>(AntType::Bomber));
        make_water_island(sim, 40, 40);
        uint32_t standing_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 15});
        ASSERT_TRUE(stand_on_powerup(sim, standing_ant, TileCoord{15, 15}, TileCoord{40, 40}));
        ASSERT_TRUE(sim.grid().has_powerup_at(sim.get_unit(standing_ant).pos));

        // Second friendly ant orders a move onto (15, 15): the goal ring scan picks a free tile
        uint32_t incoming_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 15});
        sim.issue_move_order(incoming_ant, TileCoord{15, 15});
        run_ms(sim, 2000);

        // Standing ant on power-up must NEVER be displaced
        ASSERT_EQ(sim.get_unit(standing_ant).pos, (TileCoord{15, 15}));

        // Enemy Combat ant next to it: the auto-engage walks up but its last step onto the solid power-up tile is refused
        // (CanEnter), and an attack order fails with "Can't go there."
        uint32_t enemy_combat = sim.spawn_unit(1, AntType::Combat, TileCoord{15, 17});
        uint16_t hp_before = sim.get_unit(standing_ant).hp;
        run_ms(sim, 6000);
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
        AntOrder o{};
        o.ant_id = enemy_combat;
        o.type = OrderType::Attack;
        o.target_entity_id = static_cast<int32_t>(standing_ant);
        sim.issue_order(o);
        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
        ASSERT_EQ(sim.get_unit(standing_ant).pos, (TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.111: Occupied Destination Bumping (SoundID::Bump and Stopping Cleanly)
    // ------------------------------------------------------------------------
    TEST_CASE("12.111 Destination Taken While Walking: Stop One Tile Short Without a Bump (TryEnterTile)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Moving ant approaches towards destination (15, 15)
        uint32_t mover = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 15});
        sim.issue_move_order(mover, TileCoord{15, 15});
        tick_until_paths_delivered(sim, {mover});

        // Later, blocker occupies destination (15, 15)
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        sim.get_unit(blocker).state = UnitState::Idle;

        bool bumped = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::Bump)) {
                bumped = true;
            }
            if (sim.get_unit(mover).state == UnitState::Idle && sim.get_unit(mover).pos != TileCoord{12, 15}) {
                break;
            }
        }

        // A taken final tile makes the walker stop where it is (StopSync at 0x101caf4); only a re-plan around
        // an obstacle shows the bump effect. The mover waits on the tile next to its destination.
        ASSERT_FALSE(bumped);
        ASSERT_EQ(sim.get_unit(mover).state, UnitState::Idle);
        ASSERT_TRUE(sim.get_unit(mover).pos.chebyshev_dist(TileCoord{15, 15}) == 1);
        ASSERT_TRUE(sim.get_unit(mover).waypoints.empty());
        ASSERT_EQ(sim.get_unit(blocker).pos, (TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.112: Anthill 3-Tile Ability Block and Occupied Tile Ability Block
    // ------------------------------------------------------------------------
    TEST_CASE("12.112 Anthill 3-Tile Ability Block and Occupied Tile Ability Block") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        int32_t bx = ah->x;
        int32_t by = ah->y;

        // 1. The original's three tiles that refuse bombs and fire walls next to a hill (HillSpecial, 0x101d858 / 0x101d8a4: the player's
        //    tile pairs +0x36, +0x3a and +0x3e, stored as (row, col)): the three tiles ABOVE the mound, row by - 1, columns bx .. bx + 2
        for (int dx = 0; dx <= 2; ++dx) {
            TileCoord blocked_tile{bx + dx, by - 1};

            // Bomber ant adjacent to blocked tile cannot plant bomb on blocked tile
            uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile));
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile, false));

            // Fire ant adjacent to blocked tile cannot ignite fire on blocked tile
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile));
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile, false));
        }

        // The three tiles two columns to the left of the mound, (bx - 2, by - 1 .. by + 1), were refused by the remake until v0.0.53
        // (the same data read with rows and columns swapped); the original has no such rule: they are ordinary ground
        for (int dy = -1; dy <= 1; ++dy) {
            TileCoord open_tile{bx - 2, by + dy};
            uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{open_tile.x - 1, open_tile.y});
            ASSERT_TRUE(sim.plant_bomb(bomber, open_tile, false));
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{open_tile.x - 1, open_tile.y + 3});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{open_tile.x, open_tile.y + 3}, false));
        }

        // 2. Tile occupied by any living ant cannot have a bomb or fire planted on it
        TileCoord occ{30, 30};
        uint32_t occupant = sim.spawn_unit(0, AntType::Worker, occ);
        sim.tick();
        ASSERT_TRUE(sim.has_living_ant_at(occ));

        uint32_t b2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{29, 30});
        ASSERT_FALSE(sim.plant_bomb(b2, occ));
        ASSERT_FALSE(sim.plant_bomb(b2, occ, false));

        uint32_t f2 = sim.spawn_unit(0, AntType::Fire, TileCoord{29, 30});
        ASSERT_FALSE(sim.ignite_fire(f2, occ));
        ASSERT_FALSE(sim.ignite_fire(f2, occ, false));
        (void)occupant;
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.113: Base Queue Mound Ramp Concurrency Limit (Max 2 Ants)
    // ------------------------------------------------------------------------

    // ------------------------------------------------------------------------
    // 12.114: Water Melee Combat Isolation and Emergence Invulnerability
    // ------------------------------------------------------------------------
    TEST_CASE("12.114 Water Melee Combat Isolation and Hatch Clip Refusal") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Part A: an ant that plays the hatch clip cannot be attacked (the melee refusal list holds actions 2 and 0x14),
        // but there is no invulnerability after the clip (the original has none)
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        sim.set_hatch_delay_ticks(2);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        uint32_t baby = 0;
        for (int t = 0; t < 10 && baby == 0; ++t) {
            sim.tick();
            for (const auto& a : sim.get_world_state().ants) {
                if (a.player_id == 0) baby = a.id;
            }
        }
        ASSERT_TRUE(baby != 0);
        ASSERT_EQ(sim.get_unit(baby).state, UnitState::EnteringBase);
        ASSERT_TRUE(sim.get_unit(baby).in_hill_action());

        // Enemy Combat ant tries to attack the newborn right after it appeared
        uint32_t enemy = sim.spawn_unit(1, AntType::Combat, TileCoord{21, 22});
        uint16_t baby_hp = sim.get_unit(baby).hp;
        sim.execute_melee_attack(enemy, baby);
        ASSERT_EQ(sim.get_unit(baby).hp, baby_hp);

        // Tick until the newborn has finished the clip
        for (int t = 0; t < 40 && sim.get_unit(baby).state == UnitState::EnteringBase; ++t) sim.tick();
        ASSERT_FALSE(sim.get_unit(baby).in_hill_action());

        // Right after the clip the ant can be hurt like any other
        const TileCoord bp = sim.get_unit(baby).pos;
        uint32_t enemy2 = sim.spawn_unit(1, AntType::Combat, TileCoord{bp.x + 1, bp.y});
        sim.execute_melee_attack(enemy2, baby);
        ASSERT_TRUE(sim.get_unit(baby).hp < baby_hp);

        // Part B: Water Melee Combat Isolation
        sim.grid_mut().set_terrain(10, 10, TERRAIN_WATER);
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
        sim.get_unit(swimmer).in_water = true;
        uint32_t land_ant = sim.spawn_unit(1, AntType::Combat, TileCoord{10, 11});

        // Land ant cannot melee attack water ant
        sim.execute_melee_attack(land_ant, swimmer);
        ASSERT_NE(sim.get_unit(land_ant).state, UnitState::Attacking);

        // Water ant cannot melee attack land ant
        sim.execute_melee_attack(swimmer, land_ant);
        ASSERT_NE(sim.get_unit(swimmer).state, UnitState::Attacking);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.115: Blocked Base Emergence 20-Tick Postponement (Ants.exe 0x1025100)
    // ------------------------------------------------------------------------
    TEST_CASE("12.115 Blocked Base Emergence Waits For The Entrance (HATCHTSK re-runs every slot)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        TileCoord hole{ah->x + 1, ah->y + 1}; // (21, 21)

        // 1. Place a stationary own ant directly on the hole
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, hole);
        sim.tick();
        ASSERT_TRUE(sim.has_living_ant_at(hole));

        // 2. Hatch a newborn ant
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));

        // 3. Tick through the 8000 ms incubation and some more: the hole is occupied, so no newborn appears
        for (int t = 0; t < 200; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        size_t own_ants = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) ++own_ants;
        }
        ASSERT_EQ(own_ants, 1u);

        // 4. Move the blocker away from the hole: the newborn appears as soon as the entrance is free
        TileCoord target{ah->x - 5, ah->y + 3};
        sim.issue_move_order(blocker, target);
        for (int t = 0; t < 60 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        own_ants = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) ++own_ants;
        }
        ASSERT_EQ(own_ants, 2u);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.116: A* Pathfinding Moving-Ally Passability (Ants.exe 0x10209cd)
    // ------------------------------------------------------------------------
    TEST_CASE("12.116 A* Pathfinding Moving-Ally Passability (Ants.exe 0x10209cd)") {
        SimulationEngine sim;
        sim.init_test_world(40, 40, 100, 60000);

        // Build a narrow 1-tile corridor at y=10 from x=5 to x=25
        // Flank with solid rocks above and below
        for (int x = 5; x <= 25; ++x) {
            sim.grid_mut().set_terrain(x, 9, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(x, 11, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(x, 10, TERRAIN_WALKABLE);
        }

        // Spawn friendly ant 1 marching east inside corridor
        uint32_t ant1 = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 10});
        sim.issue_move_order(ant1, TileCoord{22, 10});
        sim.tick();
        ASSERT_EQ(sim.get_unit(ant1).state, UnitState::Walking);

        // Spawn friendly ant 2 behind ant 1 inside corridor
        uint32_t ant2 = sim.spawn_unit(0, AntType::Worker, TileCoord{8, 10});
        ASSERT_EQ(sim.get_unit(ant2).state, UnitState::Idle);

        // Issue move order to ant 2 targeting (20, 10) ahead of ant 1
        sim.issue_move_order(ant2, TileCoord{20, 10});
        tick_until_paths_delivered(sim, {ant2});

        // Moving ally ant1 is NOT an impassable obstacle in A* routing (Ant::StepCost FUN_01020951: only a
        // team-mate that is waiting, or idle without a path and without an order target, costs 8000).
        // Ant 2 must successfully find a path directly through the corridor!
        ASSERT_EQ(sim.get_unit(ant2).state, UnitState::Walking);
        ASSERT_FALSE(sim.get_unit(ant2).waypoints.empty());
        ASSERT_EQ(sim.get_unit(ant2).final_dest, (TileCoord{20, 10}));
    } TEST_END();

    TEST_CASE("12.117 Thief Raid On A Hill Without Food: Idle And Empty-Handed On The Raid Tile, Still Attackable") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{5, 5});
        sim.grid_mut().set_anthill(1, TileCoord{20, 20});

        // Team 0 has a Thief next to the raid tile (bx + 3, by + 2) = (23, 22) of team 1's hill (0 food points)
        uint32_t thief_id = sim.spawn_unit(0, AntType::Thief, TileCoord{24, 22});
        const auto* enemy_base = sim.grid().find_anthill(1);
        ASSERT_TRUE(enemy_base != nullptr);
        ASSERT_EQ(sim.get_player_score(1), 0);

        // Order Thief to infiltrate Team 1's base
        AntOrder infil_order;
        infil_order.ant_id = thief_id;
        infil_order.type = OrderType::InfiltrateAnthill;
        infil_order.target_x = enemy_base->x + 1;
        infil_order.target_y = enemy_base->y + 1;
        infil_order.target_entity_id = 1;
        sim.issue_order(infil_order);

        // Step simulation until the thief arrives and plays the raid clip (atcr501) on the raid tile centre
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.get_unit(thief_id).state == UnitState::Infiltrating) break;
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
        ASSERT_EQ(sim.get_unit(thief_id).pos, (TileCoord{enemy_base->x + 3, enemy_base->y + 2}));
        // (the message handler puts the thief on the tile centre, then the last walk step's snap is still added)
        ASSERT_TRUE(std::abs(sim.get_unit(thief_id).pixel_x - ((enemy_base->x + 3) * 32 + 16)) <= 8);
        ASSERT_EQ(sim.get_unit(thief_id).pixel_y, (enemy_base->y + 2) * 32 + 16);

        // The clip lasts 3510 ms (71 ticks)
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }

        // Empty steal: the victim had 0 food, so the thief got nothing (the loot flag is set nevertheless)
        const auto& thief = sim.get_unit(thief_id);
        ASSERT_EQ(thief.carried_points, 0);
        ASSERT_FALSE(thief.is_holding());
        ASSERT_TRUE(thief.is_thief_steal);

        // Authentic 1998 behavior: with nothing to carry the thief does NOT return to base; it stands idle on the raid tile
        ASSERT_EQ(thief.state, UnitState::Idle);
        ASSERT_EQ(thief.pixel_x, (enemy_base->x + 3) * 32 + 16);
        ASSERT_EQ(thief.pixel_y, (enemy_base->y + 2) * 32 + 16);

        // The raid tile is a cell of the hill mound: A* step cost and CanEnter refuse every hill cell to all ants but the
        // ones heading home, and the owner's click on it is classified as a "go home" order, so nobody can attack the thief
        // there (it is not attackable by melee; the other rules like CanBeAttackedFrom are never reached)
        uint32_t combat_id = sim.spawn_unit(1, AntType::Combat, TileCoord{enemy_base->x + 4, enemy_base->y + 2});
        AntOrder atk_order;
        atk_order.ant_id = combat_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(thief_id);
        sim.issue_order(atk_order);
        run_ms(sim, 4000);
        ASSERT_EQ(sim.get_unit(thief_id).hp, 10);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.118: 5.0-Second Non-Dismissable Match Start Ready Modal (Ants.exe 0x1017127)
    // ------------------------------------------------------------------------
    TEST_CASE("12.118 5.0-Second Non-Dismissable Match Start Ready Modal (Ants.exe 0x1017127)") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera camera;

        // Starts inactive in headless tests until match launch
        ASSERT_FALSE(hud.is_match_start_modal_active());

        // Launch match modal
        hud.start_match_modal();
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Clicks on playfield are consumed / blocked (non-dismissable hold)
        bool consumed_lmb = hud.handle_mouse_down(200, 200, 1, sim, camera);
        ASSERT_TRUE(consumed_lmb);
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Keyboard inputs are consumed / blocked
        bool consumed_key = hud.handle_key_down(SDLK_SPACE, sim, camera);
        ASSERT_TRUE(consumed_key);
        ASSERT_TRUE(hud.is_match_start_modal_active());

        // Advance 99 steps of 50 ms (4.95 s: the application steps the HUD in real time while the dialog is up, the simulation waits for it): modal remains active
        WorldState dummy_world{};
        for (uint32_t t = 1; t < 100; ++t) {
            hud.update(dummy_world, 1);
            ASSERT_TRUE(hud.is_match_start_modal_active());
        }

        // At step 100 (exactly 5.0s elapsed: the first run of the original's timer task), modal auto-dismisses
        hud.update(dummy_world, 1);
        ASSERT_FALSE(hud.is_match_start_modal_active());

        // The dialog of a match of the network (start_match_modal(true)) does not close by itself, however long it waits for the first turn: only dismiss_match_start_modal() ends it
        hud.start_match_modal(true);
        ASSERT_TRUE(hud.is_match_start_modal_active());
        for (uint32_t t = 0; t < 1000; ++t) hud.update(dummy_world, 1);
        ASSERT_TRUE(hud.is_match_start_modal_active() && hud.match_start_modal_ticks() == 1000u);
        ASSERT_TRUE(hud.handle_mouse_down(200, 200, 1, sim, camera) && hud.is_match_start_modal_active());            // (and it takes the clicks as long as it is up)
        hud.dismiss_match_start_modal();
        ASSERT_FALSE(hud.is_match_start_modal_active());
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.118b: The Dialog Lasts Exactly sim::kMatchStartDialogMs (100 Steps Of 50 Ms Of HUD Time: The Simulation Waits For It, The Host's Start Delay Is The Same Number)
    // ------------------------------------------------------------------------
    TEST_CASE("12.118b The \"Get Ready\" Dialog Closes On The Very Step That sim::kMatchStartDialogMs Names (Open After 99 Updates, Closed After 100): The Number The Hosts Of A Match Of The Network Wait For Before They Seal The First Turn; A Click On An Own Ant Is Swallowed On Step 99 And Selects On Step 100") {
        ASSERT_EQ(ants::sim::kMatchStartDialogMs, 5000u);                                    // the original's task KWFO (Ants.exe 0x10254b0), first run 5000 ms after the dialog was made
        ASSERT_EQ(HUD::kMatchStartModalSteps * ants::sim::TICK_MS, ants::sim::kMatchStartDialogMs);
        ASSERT_EQ(HUD::kMatchStartModalSteps, 100u);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;
        HUD hud;
        hud.init(0);
        hud.start_match_modal();
        WorldState world{};
        const int32_t sx = 176 + HUD::PLAYFIELD_X;                                           // the middle of the ant at tile (5, 5), as test 7.1 clicks it
        const int32_t sy = 176 + HUD::PLAYFIELD_Y;
        for (uint32_t t = 1; t < HUD::kMatchStartModalSteps; ++t) {                          // 99 updates: the dialog is up after each of them
            hud.update(world, 1);
            ASSERT_TRUE(hud.is_match_start_modal_active());
        }
        hud.handle_mouse_down(sx, sy, 1, sim, camera);                                        // the click of step 99 is swallowed whole
        hud.handle_mouse_up(sx, sy, 1, sim, camera);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty() && hud.get_selected_ant_id() == 0u && !hud.is_ant_selected(ant));
        hud.update(world, 1);                                                                 // the 100th: the dialog is gone
        ASSERT_FALSE(hud.is_match_start_modal_active());
        hud.handle_mouse_down(sx, sy, 1, sim, camera);                                        // and the same click selects
        hud.handle_mouse_up(sx, sy, 1, sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), ant);
        ASSERT_TRUE(hud.is_ant_selected(ant));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.119: Mutual Friendly Collision Bouncing (Neither Ant Static)
    // ------------------------------------------------------------------------
    TEST_CASE("12.119 A Friendly Ant Hit Into A Friendly Ant: Pile-Up Throws Both Apart (Neither Ant Static)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Friendly Ant 1 at (20, 20), Friendly Ant 2 at (21, 20)
        uint32_t f1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t f2 = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});

        // Enemy Worker at (19, 20) strikes friendly ant 1 eastwards into friendly ant 2
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20});
        sim.clear_audio_events();
        sim.execute_melee_attack(enemy, f1);

        const auto& u1 = sim.get_unit(f1);
        const auto& u2 = sim.get_unit(f2);
        ASSERT_EQ(u1.hp, 9);

        // The gh flight of f1 ends its landing event (500 ms) on f2's tile (21, 20): both are thrown (Blast 0, 7)
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return u1.state == UnitState::Flinch && u2.state == UnitState::Flinch &&
                                                       u1.pos != u2.pos; }) >= 0);

        // Neither ant stands static: BOTH ants are thrown onto distinct tiles, nobody loses hit points to the pile-up
        ASSERT_EQ(u2.hp, 10);
        ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

        // ZERO battle dust cloud effects (no scuffle in the original)
        bool has_battle_cloud = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            for (const auto& eff : sim.get_world_state().effects) {
                if (eff.anim_name == "battle") has_battle_cloud = true;
            }
        }
        ASSERT_FALSE(has_battle_cloud);
        ASSERT_FALSE(u1.pos == u2.pos);

        // No CombatNetFairy sound effect
        ASSERT_FALSE(sim.has_audio_event(SoundID::CombatNetFairy));
    } TEST_END();

    // 12.113: 2 HP Ant Struck into Friendly Ant Triggers Mutual Bounce Before 1 HP Retreat
    TEST_CASE("12.113: 2 HP Ant Struck into Friendly Ant: Both Are Thrown Apart, Then The 1 HP Ant Retreats Home") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.set_anthill(0, {30, 30});

        // Friendly Ant 1 at (20, 20) with 2 HP, Friendly Ant 2 at (21, 20) with 10 HP
        uint32_t f1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t f2 = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});

        auto* u1_mut = const_cast<AntUnit*>(&sim.get_unit(f1));
        u1_mut->hp = 2;

        // Enemy Worker at (19, 20) strikes friendly ant 1 eastwards into friendly ant 2
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20});
        sim.clear_audio_events();
        sim.execute_melee_attack(enemy, f1);

        const auto& u1 = sim.get_unit(f1);
        const auto& u2 = sim.get_unit(f2);
        // Ant 1 took 1 damage: drops to 1 HP, but it does not run away before the hit is over
        ASSERT_EQ(u1.hp, 1);
        ASSERT_NE(u1.orig_order, AntUnit::kOrderHome);

        // Pile-up on f2's tile: both ants are thrown to distinct tiles
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return u1.state == UnitState::Flinch && u2.state == UnitState::Flinch &&
                                                       u1.pos != u2.pos; }) >= 0);
        ASSERT_FALSE(u1.pos == u2.pos);
        ASSERT_NE(u1.orig_order, AntUnit::kOrderHome);

        // Once the hit is over, Ant 1 with 1 HP is ordered home to heal (FUN_0101dded: the enter order)
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(f1).orig_order == AntUnit::kOrderHome; }) >= 0);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.120: Multi-Ant Food Harvesting & 1998 Duplication Exploit
    // ------------------------------------------------------------------------
    TEST_CASE("12.120 Multi-Ant Food Harvesting & 1998 Duplication Exploit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // The last unit of a pile: 1 unit of 25 points (the crackers' last stage), anchor (21, 21)
        const int32_t pile = place_pile(sim, 21, 21, 1, 25, {{1, 372}, {0, 0x7FFE}});

        // Two worker ants on opposite sides of the pile
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 20});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{22, 21});

        // Direct both ants to eat the food
        sim.issue_move_order(w1, TileCoord{21, 21});
        sim.issue_move_order(w2, TileCoord{21, 21});

        // Both ants play the grab clip at the same time: the arrival only looks whether a unit is left (one is)
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() {
            return sim.get_unit(w1).loco_action == AntUnit::kActionHarvest && sim.get_unit(w2).loco_action == AntUnit::kActionHarvest;
        }) >= 0);
        ASSERT_FALSE(sim.get_unit(w1).is_holding());
        ASSERT_FALSE(sim.get_unit(w2).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // At the end of the clips BOTH concurrent biting ants receive 25 points (duplication exploit!): the bite takes
        // min(1, units left) and gives the object's value without a second check
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(w1).is_holding() && sim.get_unit(w2).is_holding(); }) >= 0);
        const auto& u1 = sim.get_unit(w1);
        const auto& u2 = sim.get_unit(w2);
        ASSERT_EQ(u1.carried_food, 1);
        ASSERT_EQ(u1.carried_points, 25u);
        ASSERT_EQ(u2.carried_food, 1);
        ASSERT_EQ(u2.carried_points, 25u);

        // The pile is gone: no units, no tile on the map
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 0u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{21, 21}), -1);
        ASSERT_EQ(sim.grid().get_cell({21, 21}).interactive_id, TILE_EMPTY);
        ASSERT_EQ(sim.grid().get_cell({20, 20}).interactive_id, TILE_EMPTY);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.121: Ground Lunchbox Multi-Ant Collection and Duplication Exploit
    // ------------------------------------------------------------------------
    TEST_CASE("12.121 Ground Lunchbox Multi-Ant Collection and Duplication Exploit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a dropped lunchbox at (15, 15) with 50 points
        sim.grid_mut().drop_lunchbox(15, 15, 50);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{15, 15}));

        // Two worker ants at (14, 15) and (16, 15)
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 15});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 15});

        // Move both to the dropped lunchbox
        sim.issue_move_order(w1, TileCoord{15, 15});
        sim.issue_move_order(w2, TileCoord{15, 15});

        // Both ants play the grab clip (the lunchbox still has its unit at both arrivals) and take the 50 points each
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.get_unit(w1).is_holding() && sim.get_unit(w2).is_holding(); }) >= 0);

        const auto& u1 = sim.get_unit(w1);
        const auto& u2 = sim.get_unit(w2);

        // Both ants received 50 points before lunchbox is cleared
        ASSERT_TRUE(u1.is_holding());
        ASSERT_EQ(u1.carried_points, 50u);
        ASSERT_TRUE(u2.is_holding());
        ASSERT_EQ(u2.carried_points, 50u);

        // Lunchbox is cleared
        ASSERT_FALSE(sim.has_lunchbox_at(TileCoord{15, 15}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.122: Combat Ant AI Smooth Locomotion, Cooldown Enforcement & Physics Knockback
    // ------------------------------------------------------------------------
    TEST_CASE("12.122 Combat Ant Auto-Engage: Approach, Punch, Knockback And Return To The Saved Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Friendly Combat Ant standing on (20, 20); it got no order for a long time
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});

        // Enemy Worker invades the scan radius at (22, 20)
        uint32_t intruder = sim.spawn_unit(1, AntType::Worker, TileCoord{22, 20});

        // The idle hook finds the intruder (auto-engage starts, the saved order is "stay on this tile")
        bool engaged = false;
        for (int i = 0; i < 60 && !engaged; ++i) {
            sim.tick();
            engaged = sim.get_unit(combat).auto_engage;
        }
        ASSERT_TRUE(engaged);
        ASSERT_EQ(sim.get_unit(combat).ae_target, (TileCoord{20, 20}));

        // The combat ant walks up to the intruder and punches it: 2 hp (TakeHit twice)
        for (int i = 0; i < 100 && sim.get_unit(intruder).hp == 10; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(intruder).hp, 8);
        ASSERT_EQ(sim.get_unit(combat).loco_action, AntUnit::kActionAttack);

        // Thrown four tiles away without any stun afterwards; the combat ant goes back to its saved tile
        for (int i = 0; i < 200; ++i) sim.tick();
        ASSERT_TRUE(sim.get_unit(intruder).pos.x >= 24);
        ASSERT_FALSE(sim.get_unit(intruder).is_stunned());
        ASSERT_FALSE(sim.get_unit(combat).auto_engage);
        ASSERT_EQ(sim.get_unit(combat).pos, (TileCoord{20, 20}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.123: Anthill Bottlecap Thief Infiltration and Mound Passability
    // ------------------------------------------------------------------------
    TEST_CASE("12.123 Anthill Bottlecap Thief Infiltration and Mound Passability") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Setup base 1 at (20, 20)
        sim.set_anthill(1, {20, 20});

        // Verify corridor tile (24, 21) allows firewall and bomb placement
        const auto& corridor_cell = sim.grid().get_cell(TileCoord{24, 21});
        ASSERT_TRUE(corridor_cell.is_passable());
        ASSERT_TRUE(corridor_cell.can_place_fire());
        ASSERT_TRUE(corridor_cell.can_place_bomb());

        // Spawn a Thief ant for team 0
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{18, 22});

        // 1. Non-flank approach: Thief standing on left side does not infiltrate
        sim.tick();
        ASSERT_NE(sim.get_unit(thief).state, UnitState::Infiltrating);

        // 2. Bottlecap approach: Move thief onto bottlecap entrance tile (23, 22)
        sim.issue_move_order(thief, TileCoord{23, 22});
        for (int i = 0; i < 200; ++i) {
            sim.tick();
            if (sim.get_unit(thief).state == UnitState::Infiltrating) break;
        }

        // Thief entering from bottlecap tile (23, 22) infiltrates successfully!
        ASSERT_EQ(sim.get_unit(thief).state, UnitState::Infiltrating);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.124: Bomb Explosion bombex, Bomb Dud a*bu Scorch & Landing Stun a*sd Recovery
    // ------------------------------------------------------------------------
    TEST_CASE("12.124 Bomb Explosion bombex, Bomb Dud a*bu Scorch & Landing Stun a*sd Recovery") {
        // A. Full explosion branch and dud branch: 2 hit points either way, then the stun clip, then idle
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            // Plant bomb at (15, 15): the ant ends its path on it and sets it off
            sim.grid_mut().place_bomb(15, 15, 1);
            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            sim.trigger_bomb_detonation(victim, TileCoord{15, 15});

            const auto& u = sim.get_unit(victim);
            // Both dud and full blast deal 2 damage: HP drops from 10 to 8
            ASSERT_EQ(u.hp, 8);
            ASSERT_TRUE(u.state == UnitState::Knockback || u.state == UnitState::Burn);   // the blast flight, or (dud) the frozen ant under ?bu
            ASSERT_EQ(u.frozen, u.knock_flag);

            // The flight (or the burn) is followed by the stun clip; then the ant is idle
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return u.state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(u.is_stunned());
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return u.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(u.is_stunned());
        }

        // B. Dud specific sequence: frozen under the burn overlay for 1150 ms, stun clip afterwards, then idle
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 100 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(20, 20, 1);
                uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
                sim.trigger_bomb_detonation(ant_id, TileCoord{20, 20});
                if (!sim.get_unit(ant_id).knock_flag) continue;
                tested = true;

                ASSERT_TRUE(sim.get_unit(ant_id).frozen);
                for (int i = 0; i < 22; ++i) {
                    sim.tick();
                    ASSERT_TRUE(sim.get_unit(ant_id).frozen);      // 1100 ms
                }
                sim.tick(); sim.tick();                             // 1200 ms: the overlay has ended
                ASSERT_FALSE(sim.get_unit(ant_id).frozen);
                ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Stunned);
                ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(ant_id).state == UnitState::Idle; }) >= 0);
                ASSERT_FALSE(sim.get_unit(ant_id).is_stunned());
            }
            ASSERT_TRUE(tested);
        }
    } TEST_END();

}
