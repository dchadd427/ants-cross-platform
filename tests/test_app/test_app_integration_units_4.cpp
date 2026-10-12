// Application integration tests: suite 12 goes on (unit selection and occupied tile collision handling, part 4)
#include "app_integration_fixtures.hpp"

void run_suite_12_unit_selection_and_occupied_tile_movement_4() {
    TEST_CASE("12.125: Bomb Cursor, HUD Pedestal Glow, Water CantGo, Red Marquee & Intro Flow") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        ViewportCamera camera;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);

        // 1. Bomber Ant cursor evaluation: target reticle CursorType::Target (3 / c_targ1)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        hud.select_ant(bomber_id);
        const auto& ws = sim.get_world_state();

        // Place a bomb at (11, 10) (adjacent cardinal neighbor)
        bool bomb_planted = sim.plant_bomb(bomber_id, TileCoord{11, 10}, true /* instant */);
        ASSERT_TRUE(bomb_planted);

        // Hover over the bomb at (11, 10) -> CursorType::Target
        int32_t screen_bx = (11 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t screen_by = (10 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        CursorType bomb_cursor = hud.evaluate_cursor(screen_bx, screen_by, ws, sim.grid(), camera);
        ASSERT_EQ(static_cast<uint8_t>(bomb_cursor), static_cast<uint8_t>(CursorType::Target));

        // Order bomb placement at (10, 11) -> the cursor is not locked while the bomber plants: plain ground is the move cursor again
        bool planting_started = sim.plant_bomb(bomber_id, TileCoord{10, 11}, false);
        ASSERT_TRUE(planting_started);
        ASSERT_EQ(sim.get_unit(bomber_id).state, UnitState::PlantingBomb);
        const auto& ws_planting = sim.get_world_state();
        CursorType planting_cursor = hud.evaluate_cursor(100, 100, ws_planting, sim.grid(), camera);
        ASSERT_EQ(static_cast<uint8_t>(planting_cursor), static_cast<uint8_t>(CursorType::Move));

        // Wait for planting to complete so bomber returns to Idle
        while (sim.get_unit(bomber_id).state == UnitState::PlantingBomb) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(bomber_id).state, UnitState::Idle);

        // 2. Water Bomb Order -> instant CantGo
        sim.set_terrain(10, 9, 2); // 2 = water
        sim.clear_audio_events();
        AntOrder water_bomb_order;
        water_bomb_order.ant_id = bomber_id;
        water_bomb_order.type = OrderType::PlantBomb;
        water_bomb_order.target_x = 10;
        water_bomb_order.target_y = 9;
        sim.issue_order(water_bomb_order);
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // 3. Application Intro Flow: Loading -> QuickHelp -> MapSelect
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.skip_intro = false;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // 4. Quick Help START! button (Anim 1335 / Sprite 289 at 529, 437)
        const auto* qh_start = app.assets().find_animation("qh_start1");
        ASSERT_TRUE(qh_start != nullptr);
        ASSERT_FALSE(qh_start->subitems.empty());
        ASSERT_FALSE(qh_start->subitems[0].frames.empty());
        ASSERT_EQ(qh_start->subitems[0].frames[0].sprite_index, 289u);
        ASSERT_EQ(qh_start->subitems[0].frames[0].dx, 529);
        ASSERT_EQ(qh_start->subitems[0].frames[0].dy, 437);

        // 5. Loading screen logo, credits, and masking strip sprites
        const auto* logo_sp = app.assets().find_sprite("logo.bmp");
        ASSERT_TRUE(logo_sp != nullptr);
        ASSERT_EQ(logo_sp->width, 593u);
        ASSERT_EQ(logo_sp->height, 270u);

        const auto* cred_sp = app.assets().find_sprite("credits.bmp");
        ASSERT_TRUE(cred_sp != nullptr);
        ASSERT_EQ(cred_sp->width, 573u);
        ASSERT_EQ(cred_sp->height, 172u);

        const auto* strip_sp = app.assets().find_sprite("strip.bmp");
        ASSERT_TRUE(strip_sp != nullptr);
        ASSERT_EQ(strip_sp->width, 478u);
        ASSERT_EQ(strip_sp->height, 31u);
    } TEST_END();

    TEST_CASE("12.126: Daisy Dropper Occupancy Preservation, Standing Fire Ability Pathing, Bomb Knockback Redirection, Chain Bombs & HUD Bomb Tile Parity") {
        // 1. Daisy Flower Dropper Occupancy Preservation
        SimulationEngine sim;
        std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(path));
        sim.init(lvl, 42);

        // Flower dropper 0 drops onto (2, 20)
        TileCoord drop_pos{2, 20};
        // Place bomb at (2, 20)
        sim.grid_mut().place_bomb(2, 20, 0);
        ASSERT_TRUE(sim.grid().has_bomb_at(drop_pos));

        // Advance simulation 15 s: the polls find the tile occupied by the bomb (layer 2 must be empty or a power-up), so nothing is posted
        for (int t = 0; t < 300; ++t) {
            sim.tick();
        }
        ASSERT_TRUE(sim.grid().has_bomb_at(drop_pos));
        ASSERT_FALSE(sim.grid().has_powerup_at(drop_pos));

        // Now remove the bomb
        sim.grid_mut().clear_bomb(2, 20);
        // The stamp of the first poll is still the old one, so the next poll (every 3 s) posts and the drop lands 820 ms later
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.grid().has_powerup_at(drop_pos)) break;
        }
        ASSERT_TRUE(sim.grid().has_powerup_at(drop_pos));

        // 2. Fire Ant Standing On Fire Ability Pathing: does not detour around terrain
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 42, 60000);
        sim2.set_fire_at({15, 15}, 3600);
        uint32_t fire_ant_id = sim2.spawn_unit(0, AntType::Fire, {15, 15});
        // Issue IgniteFire order at adjacent tile (14, 15)
        AntOrder fire_order{};
        fire_order.ant_id = fire_ant_id;
        fire_order.type = OrderType::IgniteFire;
        fire_order.target_x = 14;
        fire_order.target_y = 15;
        sim2.issue_order(fire_order);
        // Ant is already adjacent on (15, 15), so it should NOT path away to North; it stays on (15, 15)
        const auto& fire_ant = sim2.get_unit(fire_ant_id);
        ASSERT_EQ(fire_ant.pos.x, 15);
        ASSERT_EQ(fire_ant.pos.y, 15);

        // 3. Bomb Knockback 8-Way Redirection Around Power-Ups (FUN_0101df5d)
        // Bomb at (20, 20), worker approaches from (21, 20) walking West
        // Recoil starts East towards (24, 20).
        // If (24, 20) has a power-up, recoil MUST rotate clockwise away from (24, 20)
        sim2.grid_mut().place_powerup(24, 20, 1);
        sim2.grid_mut().place_bomb(20, 20, 1);
        uint32_t w1 = sim2.spawn_unit(0, AntType::Worker, {21, 20});
        AntOrder w1_order{};
        w1_order.ant_id = w1;
        w1_order.type = OrderType::Move;
        w1_order.target_x = 20;
        w1_order.target_y = 20;
        sim2.issue_order(w1_order);
        for (int t = 0; t < 100; ++t) {
            sim2.tick();
            if (!sim2.grid().has_bomb_at({20, 20})) break;
        }
        for (int t = 0; t < 15; ++t) {
            sim2.tick();
            if (sim2.get_unit(w1).state != UnitState::Knockback) break;
        }
        const auto& landed_w1 = sim2.get_unit(w1);
        // Did not land on (24, 20) because of powerup redirection!
        ASSERT_FALSE(landed_w1.pos.x == 24 && landed_w1.pos.y == 20);
        // Powerup at (24, 20) was preserved
        ASSERT_TRUE(sim2.grid().has_powerup_at({24, 20}));

        // 4. Bomb Knockback Landing on Fire Wall Allowed
        sim2.set_fire_at({34, 30}, 3600);
        sim2.grid_mut().place_bomb(30, 30, 1);
        uint32_t w2 = sim2.spawn_unit(0, AntType::Worker, {31, 30});
        AntOrder w2_order{};
        w2_order.ant_id = w2;
        w2_order.type = OrderType::Move;
        w2_order.target_x = 30;
        w2_order.target_y = 30;
        sim2.issue_order(w2_order);
        for (int t = 0; t < 100; ++t) {
            sim2.tick();
            if (!sim2.grid().has_bomb_at({30, 30})) break;
        }
        for (int t = 0; t < 15; ++t) {
            sim2.tick();
            if (sim2.get_unit(w2).state != UnitState::Knockback) break;
        }
        // Took bomb blast damage (2 HP) + fire damage (1 HP) = 3 HP damage (10 - 3 = 7 HP)
        ASSERT_EQ(sim2.get_unit(w2).hp, 7);

        // 5. Chain Bomb Detonation
        // Bomb at (40, 40) recoils East to (44, 40). Place another bomb at (44, 40).
        // Using seed 1 guarantees deterministic non-dud rolls (call 0: 41% >= 20%, call 2: 34% >= 20%).
        SimulationEngine sim_chain;
        sim_chain.init_test_world(60, 60, 1, 60000);
        sim_chain.grid_mut().place_bomb(40, 40, 1);
        sim_chain.grid_mut().place_bomb(44, 40, 1);
        uint32_t w3 = sim_chain.spawn_unit(0, AntType::Worker, {41, 40});
        AntOrder w3_order{};
        w3_order.ant_id = w3;
        w3_order.type = OrderType::Move;
        w3_order.target_x = 40;
        w3_order.target_y = 40;
        sim_chain.issue_order(w3_order);
        for (int t = 0; t < 100; ++t) {
            sim_chain.tick();
            if (!sim_chain.grid().has_bomb_at({40, 40})) break;
        }
        for (int t = 0; t < 30; ++t) {
            sim_chain.tick();
        }
        // Chain bomb at (44, 40) was triggered and cleared!
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({44, 40}));

        // 6. HUD Bomb Cursor (FUN_01026f91: a bomb tile is a special target of a single selected bomber; Shift plays no part)
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim2);
        sim2.grid_mut().place_bomb(50, 50, 1);
        uint32_t bomber_id = sim2.spawn_unit(0, AntType::Bomber, {48, 50});
        uint32_t worker_id2 = sim2.spawn_unit(0, AntType::Worker, {48, 51});

        ViewportCamera cam;
        cam.center_on(50 * 32 + 16, 50 * 32 + 16);
        int32_t screen_bx = 0;
        int32_t screen_by = 0;
        cam.world_to_screen(50 * 32 + 16, 50 * 32 + 16, screen_bx, screen_by);

        // (a) Bomber selected (panel 3), shift NOT held -> Target reticle
        hud.select_ant(bomber_id, false);
        hud.set_shift_held(false);
        CursorType c1 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c1, CursorType::Target);

        // (b) Bomber selected, shift HELD -> the same Target reticle (the original's cursor code never looks at Shift)
        hud.set_shift_held(true);
        CursorType c2 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c2, CursorType::Target);

        // (c) Worker selected (non-bomber) -> regular Move cursor
        hud.select_ant(worker_id2, false);
        hud.set_shift_held(false);
        CursorType c3 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c3, CursorType::Move);

        // (d) A bomber and a worker (panel 4, mixed types) -> regular Move cursor
        hud.set_selected_ant_ids({bomber_id, worker_id2});
        CursorType c4 = hud.evaluate_cursor(screen_bx, screen_by, sim2.get_world_state(), sim2.grid(), cam);
        ASSERT_EQ(c4, CursorType::Move);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.127: Fire Extinguish Timing & Sputter, Multi-Direction Placement Queueing, and Bomb Parity
    // ------------------------------------------------------------------------
    TEST_CASE("12.127 Fire Extinguish Timing, Silent Casting Rejection, Zero Cooldown, and Bomb Dud Parity") {
        // A. Fire Extinguish Timing: 1200 ms (South) or 1300 ms (North/East/West), cue 69 at 400 ms, the wall is put out (with
        //    the sputter puff) when the clip ends
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Place firewall at (20, 21)
            sim.set_fire_at({20, 21}, 3600);
            ASSERT_TRUE(sim.has_fire_at({20, 21}));

            // Fire Ant at (20, 20) facing Down (South) to extinguish
            uint32_t ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{20, 20});
            AntOrder o;
            o.type = OrderType::ExtinguishFire;
            o.ant_id = ant_id;
            o.target_x = 20;
            o.target_y = 21;
            sim.issue_order(o);

            // Ant enters ExtinguishingFire once its one-tile path is delivered
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::ExtinguishingFire; }) >= 0);
            sim.clear_audio_events();

            // The firewall stays for the whole clip; the extinguisher cue 69 plays at 400 ms
            int elapsed = 0;
            while (sim.has_fire_at({20, 21}) && elapsed < 3000) {
                if (elapsed == 300) ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::FireExtinguish)));
                if (elapsed == 500) ASSERT_TRUE(sim.has_audio_event(static_cast<uint32_t>(SoundID::FireExtinguish)));
                ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::ExtinguishingFire);
                sim.tick();
                elapsed += 50;
            }
            ASSERT_TRUE(elapsed >= 1150 && elapsed <= 1350);

            // Fire cleared and sputter effect spawned at top-left anchor (20 * 32, 21 * 32)
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);
            ASSERT_FALSE(sim.has_fire_at({20, 21}));
            bool found_sputter = false;
            for (const auto& fx : sim.get_world_state().effects) {
                if (fx.anim_name == "sputter" && fx.px == 20 * 32 && fx.py == 21 * 32 && fx.total_frames == 17) {
                    found_sputter = true;
                    break;
                }
            }
            ASSERT_TRUE(found_sputter);
        }

        // B. Silent Order Rejection During Active Action & Zero Post-Animation Cooldown
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Fire ant at (25, 25)
            uint32_t ant_id = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});

            // Order 1: Ignite Fire North (25, 24)
            AntOrder o1;
            o1.type = OrderType::IgniteFire;
            o1.ant_id = ant_id;
            o1.target_x = 25;
            o1.target_y = 24;
            sim.issue_order(o1);

            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::PlacingFire; }) >= 0);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::North);

            // While placing North, player tries to issue another order (East 26, 25)
            sim.clear_audio_events();
            AntOrder o2;
            o2.type = OrderType::IgniteFire;
            o2.ant_id = ant_id;
            o2.target_x = 26;
            o2.target_y = 25;
            sim.issue_order(o2);

            // Order is SILENTLY IGNORED: no CantGo sound, placement continues uninterrupted
            ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::CantGo)));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::PlacingFire);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::North);
            ASSERT_EQ(sim.get_unit(ant_id).orig_special_tile, (TileCoord{25, 24}));

            // Let the North placement finish (afsf: 1810 ms)
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_fire_at({25, 24}); }) >= 0);

            // First firewall at (25, 24) exists and ant is back in Idle
            ASSERT_TRUE(sim.has_fire_at({25, 24}));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);

            // Immediately upon Idle, issuing East placement succeeds with ZERO cooldown delay!
            sim.issue_order(o2);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(ant_id).state == UnitState::PlacingFire; }) >= 0);
            ASSERT_EQ(sim.get_unit(ant_id).facing, ants::assets::Direction::East);

            // Finish East placement
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_fire_at({26, 25}); }) >= 0);
            ASSERT_TRUE(sim.has_fire_at({26, 25}));
            ASSERT_EQ(sim.get_unit(ant_id).state, UnitState::Idle);
        }

        // C. Bomb Dud & Knockback Stun Parity: is_stunned() is true during the flight / burn and the stun clip, false afterwards
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 10, 60000);

            sim.grid_mut().place_bomb(10, 10, 1);
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            const auto& ant = sim.get_unit(ant_id);
            ASSERT_FALSE(ant.is_stunned());
            sim.trigger_bomb_detonation(ant_id, TileCoord{10, 10});

            // In flight or burn, ant is considered stunned (rejects orders)
            ASSERT_TRUE(ant.is_stunned());
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return ant.state == UnitState::Stunned; }) >= 0);
            ASSERT_TRUE(ant.is_stunned());

            // After the stun clip the unit is Idle with 0 stun ticks remaining
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return ant.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(ant.is_stunned());
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.128: Bomber Pathing Around Intervening Friendly Bombs
    // ------------------------------------------------------------------------
    TEST_CASE("12.128 Bomber Pathing Around Intervening Friendly Bombs") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a friendly bomb at (11, 10) owned by Player 0
        sim.grid_mut().place_bomb(11, 10, 0);
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));

        // Bomber Ant at (10, 10)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        auto& bomber = sim.get_unit(bomber_id);
        ASSERT_EQ(bomber.pos, (TileCoord{10, 10}));

        // Issue order to plant a bomb at (12, 10)
        // (11, 10) already has a friendly bomb between the ant and target
        AntOrder o;
        o.type = OrderType::PlantBomb;
        o.ant_id = bomber_id;
        o.target_x = 12;
        o.target_y = 10;
        sim.issue_order(o);

        // The Bomber must NOT stall or pick (11, 10) as its candidate standing spot.
        // It must route around (11, 10) (e.g. via y=9 or y=11) to an open cardinal neighbor of (12, 10).
        ASSERT_TRUE(bomber.state == UnitState::Walking || bomber.state == UnitState::PlantingBomb);

        // Step simulation up to 100 ticks to allow navigation and bomb placement
        bool planted = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.has_bomb_at({12, 10})) {
                planted = true;
                break;
            }
        }

        // Successfully placed the new bomb at (12, 10)!
        ASSERT_TRUE(planted);
        ASSERT_TRUE(sim.has_bomb_at({12, 10}));

        // The original friendly bomb at (11, 10) was never detonated or stepped on!
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
    } TEST_END();

    TEST_CASE("12.129 Chain Bomb Detonation Flight Decoupling, Firewall Knockback Landing & 22-Tick Defusal Parity") {
        // -------------------------------------------------------------------------
        // Part 1: Chain Bomb Knockback Secondary Flight Decoupling
        // An ant detonates Bomb 1 at (40, 40) and is launched into Bomb 2 at (44, 40).
        // Bomb 2 detonates, triggering a secondary ballistic flight that must NOT be
        // double-erased or leave the ant frozen in UnitState::Knockback.
        // -------------------------------------------------------------------------
        SimulationEngine sim_chain;
        sim_chain.init_test_world(60, 60, 1, 60000);
        sim_chain.grid_mut().place_bomb(40, 40, 1);
        sim_chain.grid_mut().place_bomb(44, 40, 1);

        uint32_t w_chain = sim_chain.spawn_unit(0, AntType::Worker, {41, 40});
        AntOrder move_to_bomb1{};
        move_to_bomb1.ant_id = w_chain;
        move_to_bomb1.type = OrderType::Move;
        move_to_bomb1.target_x = 40;
        move_to_bomb1.target_y = 40;
        sim_chain.issue_order(move_to_bomb1);

        // Step until Bomb 1 detonates
        for (int t = 0; t < 100; ++t) {
            sim_chain.tick();
            if (!sim_chain.grid().has_bomb_at({40, 40})) break;
        }
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({40, 40}));

        // Advance simulation for chain detonation and secondary flight completion
        for (int t = 0; t < 40; ++t) {
            sim_chain.tick();
        }
        // Both bombs cleared!
        ASSERT_FALSE(sim_chain.grid().has_bomb_at({44, 40}));
        auto& chain_ant = sim_chain.get_unit(w_chain);
        // Ant survived (took 2 + 2 = 4 damage out of 10 HP = 6 HP remaining)
        ASSERT_TRUE(chain_ant.is_alive());
        ASSERT_EQ(chain_ant.hp, 6);
        // Ant successfully completed both flights and, after the stun of the second one, is idle again (NOT frozen in Knockback!)
        ASSERT_TRUE(wait_ms(sim_chain, 12000, [&]() { return chain_ant.state == UnitState::Idle; }) >= 0);
        ASSERT_FALSE(chain_ant.is_stunned());

        // Ant can immediately accept new move orders
        AntOrder move_after_chain{};
        move_after_chain.ant_id = w_chain;
        move_after_chain.type = OrderType::Move;
        move_after_chain.target_x = 45;
        move_after_chain.target_y = 40;
        sim_chain.issue_order(move_after_chain);
        ASSERT_EQ(chain_ant.state, UnitState::Walking);

        // -------------------------------------------------------------------------
        // Part 2: Bomb Knockback into Firewall (Non-Fire Ant & Fire Ant)
        // -------------------------------------------------------------------------
        SimulationEngine sim_fire;
        sim_fire.init_test_world(60, 60, 1, 60000);
        // Place bomb at (30, 30) and firewall at (34, 30)
        sim_fire.grid_mut().place_bomb(30, 30, 1);
        sim_fire.grid_mut().place_firewall(34, 30, 0);

        uint32_t w_fire = sim_fire.spawn_unit(0, AntType::Worker, {31, 30});
        AntOrder move_to_bomb_fire{};
        move_to_bomb_fire.ant_id = w_fire;
        move_to_bomb_fire.type = OrderType::Move;
        move_to_bomb_fire.target_x = 30;
        move_to_bomb_fire.target_y = 30;
        sim_fire.issue_order(move_to_bomb_fire);

        for (int t = 0; t < 100; ++t) {
            sim_fire.tick();
            if (!sim_fire.grid().has_bomb_at({30, 30})) break;
        }
        ASSERT_FALSE(sim_fire.grid().has_bomb_at({30, 30}));

        // Advance through the bomb flight, the landing on the fire wall (a second, one tile flight) and the recovery
        auto& fire_hit_ant = sim_fire.get_unit(w_fire);
        ASSERT_TRUE(wait_ms(sim_fire, 12000, [&]() { return fire_hit_ant.hp == 7 && fire_hit_ant.state == UnitState::Idle; }) >= 0);
        ASSERT_TRUE(fire_hit_ant.is_alive());
        // Worker took 2 blast + 1 fire burn damage = 7 HP remaining
        ASSERT_EQ(fire_hit_ant.hp, 7);
        // Ant returned to Idle (NOT frozen in Knockback!)
        ASSERT_EQ(fire_hit_ant.state, UnitState::Idle);
        ASSERT_FALSE(fire_hit_ant.is_stunned());

        // Worker can receive move orders (order to move away from firewall to safe ground)
        AntOrder move_after_fire{};
        move_after_fire.ant_id = w_fire;
        move_after_fire.type = OrderType::Move;
        move_after_fire.target_x = 25;
        move_after_fire.target_y = 30;
        sim_fire.issue_order(move_after_fire);
        ASSERT_EQ(fire_hit_ant.state, UnitState::Walking);

        // Fire Ant landing on fire wall from bomb knockback: takes 0 fire damage, does not freeze
        SimulationEngine sim_fire_ant;
        sim_fire_ant.init_test_world(60, 60, 1, 60000);
        sim_fire_ant.grid_mut().place_bomb(20, 20, 1);
        sim_fire_ant.grid_mut().place_firewall(24, 20, 0);

        uint32_t f_id = sim_fire_ant.spawn_unit(0, AntType::Fire, {21, 20});
        AntOrder move_fire_ant{};
        move_fire_ant.ant_id = f_id;
        move_fire_ant.type = OrderType::Move;
        move_fire_ant.target_x = 20;
        move_fire_ant.target_y = 20;
        sim_fire_ant.issue_order(move_fire_ant);

        for (int t = 0; t < 100; ++t) {
            sim_fire_ant.tick();
            if (!sim_fire_ant.grid().has_bomb_at({20, 20})) break;
        }
        ASSERT_FALSE(sim_fire_ant.grid().has_bomb_at({20, 20}));

        // The fire ant lands on the wall without any damage from it (it is stunned like after any bomb flight)
        auto& f_unit = sim_fire_ant.get_unit(f_id);
        ASSERT_TRUE(wait_ms(sim_fire_ant, 12000, [&]() { return f_unit.state == UnitState::Idle && f_unit.pos.x >= 24; }) >= 0);
        ASSERT_TRUE(f_unit.is_alive());
        // Fire Ant took only bomb damage (2 HP), immune to fire: 10 - 2 = 8 HP
        ASSERT_EQ(f_unit.hp, 8);
        ASSERT_EQ(f_unit.state, UnitState::Idle);
        ASSERT_FALSE(f_unit.is_stunned());

        // Fire Ant can immediately move after landing on fire
        AntOrder move_after_fire_ant{};
        move_after_fire_ant.ant_id = f_id;
        move_after_fire_ant.type = OrderType::Move;
        move_after_fire_ant.target_x = 25;
        move_after_fire_ant.target_y = 20;
        sim_fire_ant.issue_order(move_after_fire_ant);
        ASSERT_EQ(f_unit.state, UnitState::Walking);

        // -------------------------------------------------------------------------
        // Part 3: Bomb Defusal Authentic Clip Timing (abdb east: 1140 ms, cues 73 at 220 and 74 at 620 ms)
        // -------------------------------------------------------------------------
        SimulationEngine sim_defuse;
        sim_defuse.init_test_world(60, 60, 1, 60000);
        sim_defuse.grid_mut().place_bomb(11, 10, 1); // Enemy bomb
        uint32_t b_defuser = sim_defuse.spawn_unit(0, AntType::Bomber, {10, 10});
        auto& b_unit = sim_defuse.get_unit(b_defuser);

        sim_defuse.clear_audio_events();
        ASSERT_TRUE(sim_defuse.defuse_bomb(b_defuser, {11, 10}, false));
        ASSERT_EQ(b_unit.state, UnitState::DefusingBomb);
        ASSERT_EQ(b_unit.facing, Direction::East);

        // The bomb stays until the clip ends; the two cues come at 220 ms and 620 ms
        int defuse_elapsed = 0;
        while (sim_defuse.grid().has_bomb_at({11, 10}) && defuse_elapsed < 3000) {
            if (defuse_elapsed == 150) ASSERT_FALSE(sim_defuse.has_audio_event(SoundID::BombDefuseGrab));
            if (defuse_elapsed == 350) {
                ASSERT_TRUE(sim_defuse.has_audio_event(SoundID::BombDefuseGrab));       // Sound 73
                ASSERT_FALSE(sim_defuse.has_audio_event(SoundID::BombBodySquash));
            }
            ASSERT_EQ(b_unit.state, UnitState::DefusingBomb);
            sim_defuse.tick();
            defuse_elapsed += 50;
        }
        ASSERT_TRUE(defuse_elapsed >= 1100 && defuse_elapsed <= 1300);
        ASSERT_TRUE(sim_defuse.has_audio_event(SoundID::BombBodySquash));                // Sound 74
        ASSERT_EQ(sim_defuse.get_player_stats(0).bombs_defused, 1u);

        // The bomber is idle at once
        ASSERT_EQ(b_unit.state, UnitState::Idle);

        // Immediately order move without cooldown delay
        AntOrder move_defuser{};
        move_defuser.ant_id = b_defuser;
        move_defuser.type = OrderType::Move;
        move_defuser.target_x = 11;
        move_defuser.target_y = 10;
        sim_defuse.issue_order(move_defuser);
        ASSERT_EQ(b_unit.state, UnitState::Walking);
    } TEST_END();

    TEST_CASE("12.130 Airborne Trajectory Clearance Over Ground Ants & Fire Landing (Blast 1) / State Transitions") {
        // -------------------------------------------------------------------------
        // Part 1: Airborne Ballistic Trajectory Clearance Over Intermediate Ants
        // An ant sets off a bomb at (20, 20) while facing West. The blast throws it
        // East towards (24, 20) (4 tiles). Station friendly and enemy ants along the
        // flight path at (21, 20), (22, 20), and (23, 20). The airborne ant must fly
        // completely OVER them without colliding: only the landing tile is tested.
        // -------------------------------------------------------------------------
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 60 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(20, 20, 1);

                // Ground ants stationed along trajectory
                uint32_t standing_friendly1 = sim.spawn_unit(0, AntType::Worker, {21, 20});
                uint32_t standing_enemy     = sim.spawn_unit(1, AntType::Worker, {22, 20});
                uint32_t standing_friendly2 = sim.spawn_unit(0, AntType::Worker, {23, 20});

                // Ant spawned directly at bomb tile facing West -> blast recoils East across tiles 21, 22, 23
                uint32_t flying_ant = sim.spawn_unit(0, AntType::Worker, {20, 20});
                sim.get_unit(flying_ant).facing = Direction::West;

                sim.trigger_bomb_detonation(flying_ant, {20, 20});
                if (sim.get_unit(flying_ant).knock_flag) continue;   // a dud burns the ant on the spot
                tested = true;
                ASSERT_FALSE(sim.grid().has_bomb_at({20, 20}));

                // Step through the airborne flight and into the stun that follows a bomb flight
                const auto& fa = sim.get_unit(flying_ant);
                ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return fa.state == UnitState::Stunned; }) >= 0);

                // Intermediate ants must remain completely undisturbed at their coordinates!
                const auto& sf1 = sim.get_unit(standing_friendly1);
                ASSERT_EQ(sf1.pos.x, 21);
                ASSERT_EQ(sf1.pos.y, 20);
                ASSERT_EQ(sf1.hp, 10);
                ASSERT_EQ(sf1.state, UnitState::Idle);

                const auto& se = sim.get_unit(standing_enemy);
                ASSERT_EQ(se.pos.x, 22);
                ASSERT_EQ(se.pos.y, 20);
                ASSERT_EQ(se.hp, 10);
                ASSERT_EQ(se.state, UnitState::Idle);

                const auto& sf2 = sim.get_unit(standing_friendly2);
                ASSERT_EQ(sf2.pos.x, 23);
                ASSERT_EQ(sf2.pos.y, 20);
                ASSERT_EQ(sf2.hp, 10);
                ASSERT_EQ(sf2.state, UnitState::Idle);

                // Flying ant has flown cleanly over all 3 ants and landed on tile 4 (24, 20)!
                ASSERT_TRUE(fa.is_alive());
                ASSERT_EQ(fa.pos.x, 24);
                ASSERT_EQ(fa.pos.y, 20);
                ASSERT_TRUE(fa.is_stunned());

                // Stunned ant can be ordered to walk immediately (Ants.exe FUN_01021494), cancelling stun
                AntOrder move_after_flight{};
                move_after_flight.ant_id = flying_ant;
                move_after_flight.type = OrderType::Move;
                move_after_flight.target_x = 25;
                move_after_flight.target_y = 20;
                sim.issue_order(move_after_flight);
                ASSERT_EQ(fa.state, UnitState::Walking);
                ASSERT_FALSE(fa.is_stunned());
            }
            ASSERT_TRUE(tested);
        }

        // -------------------------------------------------------------------------
        // Part 2: Bomb Blast Knockback Landing on Fire Wall
        // The landing on a fire wall costs 1 hit point and throws the ant one tile away
        // (Blast 1); the hit clip ends without a stun and the ant is idle.
        // -------------------------------------------------------------------------
        {
            bool tested = false;
            for (uint32_t seed = 1; seed < 60 && !tested; ++seed) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, seed, 60000);
                sim.grid_mut().place_bomb(10, 10, 1);
                sim.grid_mut().place_firewall(14, 10, 0);

                uint32_t w_id = sim.spawn_unit(0, AntType::Worker, {11, 10});
                AntOrder o{};
                o.ant_id = w_id;
                o.type = OrderType::Move;
                o.target_x = 10;
                o.target_y = 10;
                sim.issue_order(o);

                ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return !sim.grid().has_bomb_at({10, 10}); }) >= 0);
                if (sim.get_unit(w_id).knock_flag) continue;   // a dud
                tested = true;

                // Step through the flight, the landing on the wall and the recovery
                const auto& u_landed = sim.get_unit(w_id);
                ASSERT_TRUE(wait_ms(sim, 12000, [&]() { return u_landed.hp == 7 && u_landed.state == UnitState::Idle; }) >= 0);
                ASSERT_TRUE(u_landed.is_alive());
                // 10 HP - 2 blast - 1 fire = 7 HP
                ASSERT_EQ(u_landed.hp, 7);
                // Ricocheted away from fire at (14, 10) to an adjacent tile
                ASSERT_TRUE(std::abs(u_landed.pos.x - 14) <= 1 && std::abs(u_landed.pos.y - 10) <= 1);
                ASSERT_FALSE(u_landed.pos.x == 14 && u_landed.pos.y == 10);
                ASSERT_EQ(u_landed.state, UnitState::Idle);
                ASSERT_FALSE(u_landed.is_stunned());

                // Can immediately walk
                AntOrder move_after{};
                move_after.ant_id = w_id;
                move_after.type = OrderType::Move;
                move_after.target_x = 5;
                move_after.target_y = 5;
                sim.issue_order(move_after);
                ASSERT_EQ(u_landed.state, UnitState::Walking);
            }
            ASSERT_TRUE(tested);
        }

        // -------------------------------------------------------------------------
        // Part 3: Combat Ant Punch Knockback Landing on Fire Wall
        // The victim takes the 2 hp of the punch and 1 hp from the wall, is thrown one tile away
        // and ends idle (a hit clip is not followed by a stun).
        // -------------------------------------------------------------------------
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.grid_mut().place_firewall(34, 30, 0);

            // Puncher at (29, 30), Target at (30, 30), Firewall at (34, 30) (4 tiles distance)
            uint32_t combat_ant = sim.spawn_unit(0, AntType::Combat, {29, 30});
            uint32_t enemy_ant  = sim.spawn_unit(1, AntType::Worker, {30, 30});

            // Combat ant attacks enemy ant
            sim.execute_melee_attack(combat_ant, enemy_ant);

            // Step through the punch flight onto the firewall and the landing resolution
            const auto& u_after = sim.get_unit(enemy_ant);
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return u_after.hp == 7 && u_after.state == UnitState::Idle; }) >= 0);
            ASSERT_TRUE(u_after.is_alive());
            // 10 HP - 2 punch - 1 fire = 7 HP
            ASSERT_EQ(u_after.hp, 7);
            // Ricocheted away from fire at (34, 30) to an adjacent tile
            ASSERT_TRUE(std::abs(u_after.pos.x - 34) <= 1 && std::abs(u_after.pos.y - 30) <= 1);
            ASSERT_FALSE(u_after.pos.x == 34 && u_after.pos.y == 30);
            ASSERT_FALSE(u_after.is_stunned());
        }

        // -------------------------------------------------------------------------
        // Part 4: General Fire Contact (Stepping on Fire)
        // Non-fire ant takes 1 fire damage and is thrown one tile away with the hit clip
        // -------------------------------------------------------------------------
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            sim.grid_mut().place_firewall(15, 15, 0);

            // Non-fire ant on the fire tile
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, {15, 15});
            sim.resolve_fire_contact(ant_id, 1, 0);

            const auto& ant_flying = sim.get_unit(ant_id);
            // Takes 1 fire damage (10 - 1 = 9 HP)
            ASSERT_EQ(ant_flying.hp, 9);
            ASSERT_EQ(ant_flying.state, UnitState::Flinch);

            // Advance through the hit clip
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return ant_flying.state == UnitState::Idle; }) >= 0);
            ASSERT_FALSE(sim.grid().has_fire_at(ant_flying.pos));
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.131: Anthill Mound Bounds & Access Invariants, 22-Tick Fire Burn & 8-Dir Random Bounces
    // ------------------------------------------------------------------------
    TEST_CASE("12.131 Anthill Mound Bounds & Access Invariants, 22-Tick Fire Burn & 8-Dir Random Bounces") {
        // Part 1: Anthill Mound Bounds & Access (4x4 Mound + Top Corridor)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            // Setup Team 1 base at (36, 20) matching user screenshot
            sim.set_anthill(1, {36, 20});
            const auto& grid = sim.grid();

            // 1. Top approach corridor (36, 19), (37, 19), (38, 19)
            // Blocked from bombs and firewalls
            for (int32_t x = 36; x <= 38; ++x) {
                const auto& cell = grid.get_cell(TileCoord{x, 19});
                ASSERT_FALSE(cell.can_place_bomb());
                ASSERT_FALSE(cell.can_place_fire());
                // Team 1 ants can pass
                ASSERT_TRUE(cell.is_passable(false, false, false, 1, false));
                // Enemy ants (Team 0) can pass freely (corridor team lock removed for authentic base perimeter walkability)
                ASSERT_TRUE(cell.is_passable(false, false, false, 0, false));
            }

            // 2. Bottlecap tile (39, 22) (bx + 3, by + 2)
            const auto& bcap = grid.get_cell(TileCoord{39, 22});
            // Enemy thief (Team 0) can occupy
            ASSERT_TRUE(bcap.is_passable(false, false, true, 0, false));
            // Friendly thief (Team 1) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, true, 1, false));
            // Non-thief enemy (Team 0) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, false, 0, false));
            // Friendly non-thief (Team 1) cannot occupy
            ASSERT_FALSE(bcap.is_passable(false, false, false, 1, false));

            // 3. Ramp (37, 20) and Hole (37, 21)
            // Blocked from bombs and firewalls
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).can_place_bomb());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).can_place_fire());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).can_place_bomb());
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).can_place_fire());
            // Only passable when entering or leaving base
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 20}).is_passable(false, false, false, 1, false));
            ASSERT_TRUE(grid.get_cell(TileCoord{37, 20}).is_passable(false, false, false, 1, true));
            ASSERT_FALSE(grid.get_cell(TileCoord{37, 21}).is_passable(false, false, false, 1, false));
            ASSERT_TRUE(grid.get_cell(TileCoord{37, 21}).is_passable(false, false, false, 1, true));

            // 4. Remaining 13 mound tiles must be impassable for all
            for (int32_t dy = 0; dy < 4; ++dy) {
                for (int32_t dx = 0; dx < 4; ++dx) {
                    if ((dx == 1 && dy == 0) || (dx == 1 && dy == 1) || (dx == 3 && dy == 2)) {
                        continue;
                    }
                    const auto& cell = grid.get_cell(TileCoord{36 + dx, 20 + dy});
                    ASSERT_FALSE(cell.is_passable(false, false, false, 1, false));
                    ASSERT_FALSE(cell.is_passable(false, false, true, 0, false));
                    ASSERT_TRUE(cell.is_obstacle());
                }
            }
        }

        // Part 2: Fire contact (Blast 1): 1 hp, the gh clip carries the ant one tile away, sounds 64 (frame 0) and 65
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            sim.grid_mut().place_firewall(10, 10, 0);

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            sim.resolve_fire_contact(ant, 1, 0);

            const auto& u0 = sim.get_unit(ant);
            ASSERT_EQ(u0.loco_action, AntUnit::kActionHit);
            ASSERT_EQ(u0.hp, 9);
            ASSERT_TRUE(sim.has_audio_event(64));
            bool heard_sound65 = false;
            for (int t = 0; t < 60; ++t) {
                sim.tick();
                if (sim.has_audio_event(65)) heard_sound65 = true;
            }
            ASSERT_TRUE(heard_sound65);

            const auto& u_done = sim.get_unit(ant);
            ASSERT_EQ(u_done.loco_action, AntUnit::kActionIdle);
            ASSERT_EQ(std::max(std::abs(u_done.pos.x - 10), std::abs(u_done.pos.y - 10)), 1);
            ASSERT_TRUE(sim.grid().has_fire_at(TileCoord{10, 10}));   // the fire wall stays
        }

        // Part 3: The thrown ant lands in a random one of the free directions across trials
        {
            std::set<std::pair<int32_t, int32_t>> distinct_landing_tiles;
            for (int trial = 0; trial < 40; ++trial) {
                SimulationEngine sim;
                sim.init_test_world(60, 60, 100 + static_cast<uint32_t>(trial * 31), 60000);
                sim.grid_mut().place_firewall(30, 30, 0);
                uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
                sim.resolve_fire_contact(ant, 1, 0);
                for (int t = 0; t < 60; ++t) sim.tick();
                const auto& u = sim.get_unit(ant);
                distinct_landing_tiles.insert({u.pos.x, u.pos.y});
            }
            // Over 40 randomized trials, the ant must be thrown in multiple directions, not just 1 fixed tile
            ASSERT_TRUE(distinct_landing_tiles.size() >= 4);
        }

        // Part 4: A landing on a bomb tile sets the bomb off (the flight of a hit ends on the bomb)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 1, 60000);
            TileCoord bomb_pos{25, 25};
            TileCoord fire_pos{24, 26}; // Southwest of bomb (dx = -1, dy = +1)
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);
            sim.grid_mut().place_firewall(static_cast<uint32_t>(fire_pos.x), static_cast<uint32_t>(fire_pos.y), 0);

            // Block all other neighbors of fire_pos except bomb_pos so the throw is forced towards the bomb (northeast)
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord cand{fire_pos.x + dx, fire_pos.y + dy};
                    if (cand != bomb_pos) {
                        sim.grid_mut().set_terrain(cand.x, cand.y, TERRAIN_OBSTACLE);
                    }
                }
            }

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, fire_pos);
            sim.resolve_fire_contact(ant, 1, 0);
            ASSERT_EQ(sim.get_unit(ant).loco_action, AntUnit::kActionHit);
            for (int t = 0; t < 120; ++t) sim.tick();

            // The bomb went off when the flight ended on it (the ant lost 2 more hp)
            ASSERT_FALSE(sim.grid().has_bomb_at(bomb_pos));
            ASSERT_TRUE(sim.get_unit(ant).hp <= 7);
        }
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.63 Authentic 1998 Bomb Blast Flyback Starts Directly at Bomb Tile
    // ------------------------------------------------------------------------
    TEST_CASE("12.63 Authentic 1998 Bomb Blast Flyback Starts Directly at Bomb Tile") {
        bool tested_flight = false;
        for (uint32_t seed = 0; seed < 100 && !tested_flight; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);
            TileCoord bomb_pos{20, 20};
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);

            uint32_t ant = sim.spawn_unit(0, AntType::Worker, bomb_pos);
            auto& u = sim.get_unit(ant);
            u.facing = ants::assets::Direction::North;
            u.hp = 10;
            u.max_hp = 10;

            sim.trigger_bomb_detonation(ant, bomb_pos, 0, 0);

            const auto& u_post = sim.get_unit(ant);
            if (u_post.state != UnitState::Knockback || u_post.knock_flag) continue;   // a dud burns the ant on the spot
            tested_flight = true;

            // The gb clip starts ON the bomb tile centre (the blast puts the ant there) ...
            ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
            ASSERT_EQ(u_post.pixel_y, bomb_pos.y * 32 + 16);

            // ... and carries it 4 tiles opposite its facing (facing North: recoil South, dest_ty == 24) in one
            // 128 px jump of the clip (Table 4 frame 1 dy = +128) at about 300 ms
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return u_post.pixel_y != bomb_pos.y * 32 + 16; }) >= 0);
            ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
            ASSERT_EQ(u_post.pixel_y, (bomb_pos.y + 4) * 32 + 16);
            ASSERT_EQ(u_post.pos, (TileCoord{bomb_pos.x, bomb_pos.y + 4}));

            // The ant stays at the destination while the rest of the clip plays
            for (int t = 0; t < 10; ++t) {
                sim.tick();
                ASSERT_EQ(u_post.pixel_x, bomb_pos.x * 32 + 16);
                ASSERT_EQ(u_post.pixel_y, (bomb_pos.y + 4) * 32 + 16);
            }
        }
        ASSERT_TRUE(tested_flight);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.64 Thief Infiltration Bottlecap Row Alignment, Untargetability & In-Place Re-Trigger
    // ------------------------------------------------------------------------
    TEST_CASE("12.64 Thief Raid Clip Alignment, Re-Trigger By Order And Raid On Arrival") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(1, {10, 10});

        uint32_t thief_id = sim.spawn_unit(0, AntType::Thief, TileCoord{13, 12});
        auto& thief = sim.get_unit(thief_id);
        ASSERT_EQ(thief.type, AntType::Thief);

        // Step 1: Infiltrate enemy base with 0 enemy score (unsuccessful steal)
        sim.start_thief_infiltration(thief_id, 1);
        ASSERT_EQ(thief.state, UnitState::Infiltrating);

        // Advance ticks for the 3510 ms raid clip to complete
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }

        // On an empty steal the thief stands idle on the raid tile (bx + 3, by + 2) = (13, 12) at its centre
        const auto& u_idle = sim.get_unit(thief_id);
        ASSERT_EQ(u_idle.state, UnitState::Idle);
        ASSERT_EQ(u_idle.pos.x, 13);
        ASSERT_EQ(u_idle.pos.y, 12);
        ASSERT_EQ(u_idle.pixel_x, 13 * 32 + 16);
        ASSERT_EQ(u_idle.pixel_y, 12 * 32 + 16);

        // Step 2: In-place steal re-trigger while standing on the raid tile
        AntOrder inf_order;
        inf_order.ant_id = thief_id;
        inf_order.type = OrderType::InfiltrateAnthill;
        inf_order.target_entity_id = 1;
        inf_order.target_x = 13;
        inf_order.target_y = 12;
        sim.issue_order(inf_order);
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);

        // The raid clip refuses orders while it runs
        sim.issue_move_order(thief_id, TileCoord{14, 12});
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
        for (int t = 0; t < 75; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Idle);

        // Step 3: Moving away and back onto the enemy hill raids again upon arrival
        sim.issue_move_order(thief_id, TileCoord{14, 12});
        for (int t = 0; t < 40 && sim.get_unit(thief_id).pos != (TileCoord{14, 12}); ++t) sim.tick();
        ASSERT_EQ(sim.get_unit(thief_id).pos, (TileCoord{14, 12}));
        sim.issue_move_order(thief_id, TileCoord{13, 12});
        for (int t = 0; t < 40; ++t) {
            sim.tick();
            if (sim.get_unit(thief_id).state == UnitState::Infiltrating) break;
        }
        ASSERT_EQ(sim.get_unit(thief_id).state, UnitState::Infiltrating);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.65 Fire Contact Bounce Plays Ground Bounce Animation State
    // ------------------------------------------------------------------------
    TEST_CASE("12.65 Fire Contact Plays The Hit Clip (gh): One Hit Point, A One Tile Flight") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        TileCoord fire_pos{15, 15};
        sim.grid_mut().place_firewall(static_cast<uint32_t>(fire_pos.x), static_cast<uint32_t>(fire_pos.y), 0);

        uint32_t ant = sim.spawn_unit(0, AntType::Worker, fire_pos);
        sim.resolve_fire_contact(ant, 1, 0);

        const auto& u = sim.get_unit(ant);
        // Contact with fire is Blast(1): the ant plays the gh clip (action 0xE) and loses one hit point
        ASSERT_EQ(u.state, UnitState::Flinch);
        ASSERT_EQ(u.loco_action, AntUnit::kActionHit);
        ASSERT_EQ(u.hp, 9);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.66 Bomb Dud Stationary Position Lock
    // ------------------------------------------------------------------------
    TEST_CASE("12.66 Bomb Dud Stationary Position Lock") {
        bool found_dud = false;
        for (uint32_t seed = 0; seed < 100; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);
            TileCoord bomb_pos{20, 20};
            sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb_pos.x), static_cast<uint32_t>(bomb_pos.y), 1);
            uint32_t ant = sim.spawn_unit(0, AntType::Worker, bomb_pos);
            sim.get_unit(ant).hp = 10;
            sim.trigger_bomb_detonation(ant, bomb_pos, 0, 0);

            if (sim.get_unit(ant).state == UnitState::Burn) {
                found_dud = true;
                const auto& u = sim.get_unit(ant);
                // Ant is locked firmly at bomb tile without pushing or sliding
                ASSERT_EQ(u.pos.x, bomb_pos.x);
                ASSERT_EQ(u.pos.y, bomb_pos.y);
                ASSERT_EQ(u.pixel_x, bomb_pos.x * 32 + 16);
                ASSERT_EQ(u.pixel_y, bomb_pos.y * 32 + 16);

                // Advance several ticks: ant must remain firmly locked at bomb position
                for (int t = 0; t < 10; ++t) {
                    sim.tick();
                    const auto& u_burn = sim.get_unit(ant);
                    if (u_burn.state == UnitState::Burn) {
                        ASSERT_EQ(u_burn.pixel_x, bomb_pos.x * 32 + 16);
                        ASSERT_EQ(u_burn.pixel_y, bomb_pos.y * 32 + 16);
                    }
                }
                break;
            }
        }
        ASSERT_TRUE(found_dud);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.67 Mutual Ant-Ant Collision Bouncing
    // ------------------------------------------------------------------------
    TEST_CASE("12.67 Head-On Walkers Wait, Re-Plan and Pass Without Bouncing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        uint32_t ant1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t ant2 = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 10});

        sim.issue_move_order(ant1, TileCoord{15, 10});
        sim.issue_move_order(ant2, TileCoord{10, 10});
        // Each clicked tile holds the other ant, so GoTo takes the first free tile of the ring scan around it
        // (west column first, top first): ant 1 heads for (14, 9), ant 2 for (9, 9).
        ASSERT_EQ(sim.get_unit(ant1).final_dest, (TileCoord{14, 9}));
        ASSERT_EQ(sim.get_unit(ant2).final_dest, (TileCoord{9, 9}));

        // Walking ants never bounce off each other in the original: the occupancy grid stops them at the tile
        // boundary, they wait (ANTPAUSE, 300 ms) while the other ant is moving, and re-plan around it (bump)
        // once it stands still.
        bool bounced = false;
        bool shared_tile = false;
        bool waited = false;
        for (int t = 0; t < 200; ++t) {
            sim.tick();
            const auto& u1 = sim.get_unit(ant1);
            const auto& u2 = sim.get_unit(ant2);
            if (u1.state == UnitState::Knockback || u2.state == UnitState::Knockback ||
                u1.state == UnitState::Flinch || u2.state == UnitState::Flinch) {
                bounced = true;
            }
            if (u1.pause_active || u2.pause_active) waited = true;
            if (u1.occ_tile == u2.occ_tile) shared_tile = true;
            if (u1.pos == TileCoord{14, 9} && u2.pos == TileCoord{9, 9} &&
                u1.waypoints.empty() && u2.waypoints.empty()) {
                break;
            }
        }
        ASSERT_FALSE(bounced);
        ASSERT_FALSE(shared_tile);
        ASSERT_TRUE(waited);
        ASSERT_TRUE(sim.has_audio_event(SoundID::Bump));   // the re-plan around the waiting ant
        ASSERT_EQ(sim.get_unit(ant1).pos, (TileCoord{14, 9}));
        ASSERT_EQ(sim.get_unit(ant2).pos, (TileCoord{9, 9}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.68 Sidebar Pedestal Buttons Dimensions and Drop Shadows
    // ------------------------------------------------------------------------
    TEST_CASE("12.68 Sidebar Pedestal Buttons Dimensions and Drop Shadows") {
        HUD hud;
        hud.init(0);
        const auto& move_btn = hud.get_move_pedestal_button();
        const auto& abil_btn = hud.get_ability_pedestal_button();

        // The pedestal slots of FUN_01028d30 (half-open): slot 1 (482, 152) - (525, 225), slot 2 (539, 152) - (582, 225)
        ASSERT_EQ(move_btn.x, 482);
        ASSERT_EQ(move_btn.y, 152);
        ASSERT_EQ(move_btn.w, 43);
        ASSERT_EQ(move_btn.h, 73);

        ASSERT_EQ(abil_btn.x, 539);
        ASSERT_EQ(abil_btn.y, 152);
        ASSERT_EQ(abil_btn.w, 43);
        ASSERT_EQ(abil_btn.h, 73);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.132 Authentic 1998 Parity Suite: Stun Pacing, Bomb Deflection Facing, Placement Detonation & Combat AI Punch/Knockback/Water Kill
    // ------------------------------------------------------------------------
    TEST_CASE("12.132 Authentic 1998 Parity: Stun Pacing, Bomb Deflection, Placement Detonation & Combat AI") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // 1. Bomb Placement Pre-Check & Placeholder While The Plant Clip Plays
        uint32_t occupied_ant = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 10});
        uint32_t bomber1 = sim.spawn_unit(0, AntType::Bomber, TileCoord{11, 10});
        ASSERT_EQ(sim.get_unit(occupied_ant).pos, (TileCoord{12, 10}));
        // Bomber cannot initiate placement on a tile already occupied by an ant
        ASSERT_FALSE(sim.plant_bomb(bomber1, TileCoord{12, 10}));

        // Now initiate valid bomb placement on open tile (15, 15) with the plant clip (instant = false)
        uint32_t bomber2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{14, 15});
        ASSERT_TRUE(sim.plant_bomb(bomber2, TileCoord{15, 15}, false));
        ASSERT_EQ(sim.get_unit(bomber2).state, UnitState::PlantingBomb);

        // The reserved tile is solid: a walker ordered onto it stops next to it and never sets the bomb off
        uint32_t walker = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 17});
        sim.issue_move_order(walker, TileCoord{15, 15});
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.grid().has_bomb_at({15, 15}); }) >= 1300);
        run_ms(sim, 2000);
        const auto& w_ant = sim.get_unit(walker);
        ASSERT_EQ(w_ant.hp, 10);                                  // untouched: nobody stepped on the reserved tile
        ASSERT_TRUE(sim.grid().has_bomb_at({15, 15}));

        // A walker ordered onto the finished bomb sets it off: two hit points, a bomb flight (or the dud burn)
        sim.issue_move_order(walker, TileCoord{15, 15});
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return !sim.grid().has_bomb_at({15, 15}); }) >= 0);
        ASSERT_EQ(w_ant.hp, 8);                                   // Took 2 HP bomb blast damage
        ASSERT_TRUE(w_ant.state == UnitState::Knockback || w_ant.state == UnitState::Burn);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));

        // 2. Bomb Knockback Deflection: the ant is thrown opposite its facing; a solid landing tile makes PickLanding
        //    try the next direction (d + 1)
        bool tested_deflection = false;
        for (uint32_t s = 1; s <= 40 && !tested_deflection; ++s) {
            sim.init_test_world(60, 60, s, 60000);
            sim.grid_mut().place_bomb(20, 20, 0);
            // Block the South landing tile (20, 24) with an obstacle
            sim.grid_mut().get_cell_mut(20, 24).terrain_type = ants::sim::TERRAIN_OBSTACLE;

            // Ant at (20, 20) steps on the bomb moving North
            uint32_t bomb_victim = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});
            auto* bv_unit = const_cast<AntUnit*>(&sim.get_unit(bomb_victim));
            bv_unit->facing = Direction::North;

            // Trigger detonation
            sim.trigger_bomb_detonation(bomb_victim, TileCoord{20, 20}, 0, -1);
            const auto& bv_post = sim.get_unit(bomb_victim);
            if (bv_post.state == UnitState::Knockback && !bv_post.knock_flag) {
                tested_deflection = true;
                // Deflected from South because (20, 24) is blocked; the clip faces the direction of the deflected throw
                ASSERT_NE(bv_post.facing, Direction::North);
                ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return bv_post.pos != TileCoord{20, 20}; }) >= 0);
                // Destination is 4 tiles from bomb
                int32_t dist_from_bomb = std::max(std::abs(bv_post.pos.x - 20), std::abs(bv_post.pos.y - 20));
                ASSERT_EQ(dist_from_bomb, 4);
                ASSERT_NE(bv_post.pos, (TileCoord{20, 24})); // Blocked South was avoided
                ASSERT_EQ(bv_post.pos, (TileCoord{16, 24})); // the next direction: South-West
            }
        }
        ASSERT_TRUE(tested_deflection);

        // 3. Table 4 Stun Animation Duration & Pacing
        AssetArchive archive;
        ASSERT_TRUE(archive.load_chd("Original-Ants/ants.chd"));
        const auto* worker_sd = archive.find_animation("agsd301");
        ASSERT_TRUE(worker_sd != nullptr);
        ASSERT_EQ(worker_sd->subitems.size(), 25u);
        ASSERT_EQ(worker_sd->subitems[0].val3, 125u); // Authentic 125ms per subitem (8 FPS)

        const auto* bomber_sd = archive.find_animation("absd301");
        ASSERT_TRUE(bomber_sd != nullptr);
        ASSERT_EQ(bomber_sd->subitems[0].val3, 105u); // Authentic 105ms per subitem

        const auto* carrying_sd = archive.find_animation("hgsd301");
        ASSERT_TRUE(carrying_sd != nullptr);
        ASSERT_EQ(carrying_sd->subitems[0].val3, 125u); // Preserves 125ms duration for carrying variant

        // 4. Combat ant auto-engage punch (2 hp), knockback into water drowns the victim
        sim.init_test_world(60, 60, 42, 60000);
        // Make tile (24, 20) water
        sim.grid_mut().get_cell_mut(24, 20).terrain_type = ants::sim::TERRAIN_WATER;

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});

        // Enemy Worker at (20, 20)
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        // The enemy is adjacent: the idle hook starts the attack at once; the punch sound is frame 2 of the clip
        bool punch_sound = false;
        for (int t = 0; t < 40 && sim.get_unit(victim_id).hp == 10; ++t) {
            sim.tick();
        }
        for (int t = 0; t < 10; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::HeavyPunch)) punch_sound = true;
        }
        ASSERT_TRUE(punch_sound);
        ASSERT_EQ(sim.get_unit(combat_id).loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(sim.get_unit(victim_id).hp, 8); // 2 HP punch damage

        // Punch directly into water test: Worker on (23, 20) punched East into water (24..30, 20)
        sim.init_test_world(60, 60, 42, 60000);
        for (int tx = 24; tx <= 30; ++tx) {
            sim.grid_mut().get_cell_mut(static_cast<uint32_t>(tx), 20).terrain_type = ants::sim::TERRAIN_WATER;
        }
        uint32_t puncher = sim.spawn_unit(0, AntType::Combat, TileCoord{22, 20});
        uint32_t water_victim = sim.spawn_unit(1, AntType::Worker, TileCoord{23, 20});
        sim.execute_melee_attack(puncher, water_victim);

        // Advance through the flight until the water landing
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            if (sim.get_unit(water_victim).state == UnitState::Drowning) break;
        }
        const auto& wv_landed = sim.get_unit(water_victim);
        ASSERT_EQ(wv_landed.state, UnitState::Drowning);
        for (int t = 0; t < 5; ++t) sim.tick();   // the drowning sound is frame 1 of the clip (100 ms)
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        for (int t = 0; t < 120 && !sim.get_unit(water_victim).removed; ++t) sim.tick();
        ASSERT_TRUE(sim.get_unit(water_victim).removed);
        ASSERT_EQ(sim.get_unit(water_victim).death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.133 Combat Ant Walk Animation Completion Before Attack") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Combat Ant spawned at (15, 20), target enemy Worker at (18, 20): the scan finds it (ring 3)
        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 20});
        uint32_t initial_vic_hp = sim.get_unit(victim_id).hp;

        // Advance simulation: the combat ant walks towards (17, 20)
        bool hit_occurred = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            const auto& c_u = sim.get_unit(combat_id);
            if (c_u.state == UnitState::Walking) {
                // While walking mid-stride, no damage can be dealt
                ASSERT_EQ(sim.get_unit(victim_id).hp, initial_vic_hp);
            }
            if (sim.get_unit(victim_id).hp < initial_vic_hp) {
                hit_occurred = true;
                // Attack connected: Combat Ant must have finished walking into adjacent tile (17, 20)
                ASSERT_EQ(c_u.pos, (TileCoord{17, 20}));
                ASSERT_EQ(c_u.pixel_x, 17 * 32 + 16);
                ASSERT_EQ(c_u.pixel_y, 20 * 32 + 16);
                break;
            }
        }
        ASSERT_TRUE(hit_occurred);
    } TEST_END();

    TEST_CASE("12.134 Knockback Trajectory Origin: The gb Clip Jumps 128 px In One Frame Step From The Contact Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        int32_t expected_start_px = 20 * 32 + 16; // 656
        int32_t expected_start_py = 20 * 32 + 16; // 656
        int32_t expected_target_px = 24 * 32 + 16; // 784

        sim.execute_melee_attack(combat_id, victim_id);

        // At the contact the victim stands on its tile until the strike frame
        const auto& vic = sim.get_unit(victim_id);
        ASSERT_EQ(vic.pixel_x, expected_start_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);
        ASSERT_NE(vic.state, UnitState::Knockback);

        // Strike frame (200 ms): the flight clip starts on the original tile
        ASSERT_EQ(wait_ms(sim, 1000, [&]() { return vic.state == UnitState::Knockback; }), 200);
        ASSERT_EQ(vic.pixel_x, expected_start_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);

        // The clip then moves it by 128 px in one step (no smooth interpolation in the original)
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.pixel_x != expected_start_px; }) >= 0);
        ASSERT_EQ(vic.pixel_x, expected_target_px);
        ASSERT_EQ(vic.pixel_y, expected_start_py);
        ASSERT_EQ(vic.state, UnitState::Knockback);

        // The rest of the clip plays on the landing tile; the flight ends without a stun
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(vic.pixel_x, expected_target_px);
        ASSERT_FALSE(vic.is_stunned());
    } TEST_END();

    TEST_CASE("12.135 Combat Ant Punches Ant Over Small Terrain Barrier to Open Ground") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Create 1-tile rock obstacle at (21, 20)
        sim.grid_mut().get_cell_mut(21, 20).terrain_type = ants::sim::TERRAIN_OBSTACLE;

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        sim.execute_melee_attack(combat_id, victim_id);

        const auto& vic = sim.get_unit(victim_id);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.state == UnitState::Knockback; }) >= 0);
        // Flight destination cleared the rock at (21, 20) and anchored at (24, 20)
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return vic.pos == TileCoord{24, 20}; }) >= 0);

        // Advance through the flight until the landing frames end
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(vic.pos, (TileCoord{24, 20}));
        ASSERT_EQ(vic.pixel_x, 24 * 32 + 16);
        ASSERT_FALSE(vic.is_stunned());
    } TEST_END();

    TEST_CASE("12.136 Combat Ant Punches Ant Over Small Terrain Barrier Into Water Drowning") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);

        // Create 1-tile rock obstacle at (21, 20)
        sim.grid_mut().get_cell_mut(21, 20).terrain_type = ants::sim::TERRAIN_OBSTACLE;
        // Create water tiles on the other side at (22..30, 20)
        for (int tx = 22; tx <= 30; ++tx) {
            sim.grid_mut().get_cell_mut(static_cast<uint32_t>(tx), 20).terrain_type = ants::sim::TERRAIN_WATER;
        }

        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{19, 20});
        uint32_t victim_id = sim.spawn_unit(1, AntType::Worker, TileCoord{20, 20});

        sim.execute_melee_attack(combat_id, victim_id);

        // Advance through the flight until the water landing
        const auto& vic_drowned = sim.get_unit(victim_id);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return vic_drowned.state == UnitState::Drowning; }) >= 0);
        ASSERT_EQ(vic_drowned.pos, (TileCoord{24, 20}));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !vic_drowned.is_alive(); }) >= 0);
        ASSERT_EQ(vic_drowned.death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.137 Non-Thief Friendly Unit Clicking Enemy Base Stops Without CantGo (GoTo, FUN_0101fc50)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{2, 2});
        sim.set_anthill(1, TileCoord{8, 8});

        ViewportCamera camera{};
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Spawn friendly worker at (5, 8)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 8});

        // Select worker
        hud.select_ant(worker);
        ASSERT_TRUE(hud.is_ant_selected(worker));

        // Click on enemy base at (8, 8)
        int32_t bx = (8 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t by = (8 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 1, sim, camera);
        hud.handle_mouse_up(bx, by, 1, sim, camera);

        // Verify CantGo was NOT triggered
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        const auto& u = sim.get_unit(worker);
        // The original GoTo stops a non-thief that is ordered onto an enemy hill (StopSync) and requests no path
        ASSERT_FALSE(sim.has_pending_path(worker));
        ASSERT_TRUE(u.waypoints.empty());
        ASSERT_NE(u.state, UnitState::Infiltrating);

        for (int t = 0; t < 40; ++t) {
            sim.tick();
        }

        const auto& u_done = sim.get_unit(worker);
        ASSERT_EQ(u_done.state, UnitState::Idle);
        ASSERT_EQ(u_done.pos, (TileCoord{5, 8}));
        // Never inside the 4x4 enemy base footprint (8..11, 8..11)
        bool inside_enemy_base = (u_done.pos.x >= 8 && u_done.pos.x < 12 && u_done.pos.y >= 8 && u_done.pos.y < 12);
        ASSERT_FALSE(inside_enemy_base);
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));
    } TEST_END();

    TEST_CASE("12.138 Clicking Base Queue Slots Issues Standard Move Without Auto-Queueing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{5, 5});

        ViewportCamera camera{};
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Spawn friendly worker with 2 HP (damaged, max 10) at (2, 5)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{2, 5});
        sim.get_unit(worker).hp = 2;

        // Select worker
        hud.select_ant(worker);
        ASSERT_TRUE(hud.is_ant_selected(worker));

        // Click directly on queue slot (bx - 1, by + 3) -> (4, 8)
        int32_t qx = (4 * 32 + 16) - camera.world_x + HUD::PLAYFIELD_X;
        int32_t qy = (8 * 32 + 16) - camera.world_y + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(qx, qy, 1, sim, camera);
        hud.handle_mouse_up(qx, qy, 1, sim, camera);

        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (sim.get_unit(worker).state == UnitState::Idle && sim.get_unit(worker).pos == (TileCoord{4, 8})) break;
        }

        const auto& u = sim.get_unit(worker);
        ASSERT_EQ(u.pos, (TileCoord{4, 8}));
        ASSERT_EQ(u.state, UnitState::Idle);
        // Must NOT be queued into base queue!
        ASSERT_FALSE(sim.is_ant_in_base_queue(worker));
        ASSERT_NE(u.state, UnitState::EnteringBase);
    } TEST_END();

    TEST_CASE("12.139 Combat Ant Knockback Near Base Never Lands Inside Base Footprint or Queue Locations") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{20, 20});

        // Spawn Player 0 worker at (18, 21)
        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 21});
        // Spawn Player 1 combat ant at (17, 21) facing east directly towards base
        uint32_t combat = sim.spawn_unit(1, AntType::Combat, TileCoord{17, 21});

        // Combat ant punches worker eastward towards anthill
        sim.execute_melee_attack(combat, worker);

        const auto& w_knocked = sim.get_unit(worker);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return w_knocked.state == UnitState::Knockback; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return w_knocked.pos.x != 18 || w_knocked.pos.y != 21; }) >= 0);

        // Landing pos must NOT be inside 4x4 base footprint or queue slots
        int32_t lx = w_knocked.pos.x;
        int32_t ly = w_knocked.pos.y;
        bool in_base = (lx >= 20 && lx < 24 && ly >= 20 && ly < 24);
        bool in_queue = (lx == 19 && ly >= 20 && ly <= 23);
        ASSERT_FALSE(in_base);
        ASSERT_FALSE(in_queue);

        // Step through knockback flight
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            ASSERT_NE(sim.get_unit(worker).state, UnitState::EnteringBase);
        }

        const auto& w_landed = sim.get_unit(worker);
        int32_t fx = w_landed.pos.x;
        int32_t fy = w_landed.pos.y;
        bool landed_in_base = (fx >= 20 && fx < 24 && fy >= 20 && fy < 24);
        bool landed_in_queue = (fx == 19 && fy >= 20 && fy <= 23);
        ASSERT_FALSE(landed_in_base);
        ASSERT_FALSE(landed_in_queue);
        ASSERT_NE(w_landed.state, UnitState::EnteringBase);
    } TEST_END();

    TEST_CASE("12.140 A 1 HP Survivor Of A Hit Goes Home, A Thief With Loot Goes Home, Nobody Else Does") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.set_anthill(0, TileCoord{20, 20});
        sim.set_anthill(1, TileCoord{40, 40});

        // 1. A worker at 2 HP just standing there does not go home
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
        sim.get_unit(w1).hp = 2;
        for (int t = 0; t < 10; ++t) sim.tick();
        ASSERT_FALSE(sim.is_ant_in_base_queue(w1));
        ASSERT_EQ(sim.get_unit(w1).orig_order, AntUnit::kOrderNone);

        // 2. Hit down to 1 HP it goes home once the hit is over (LowHpCheck FUN_0101dded at the end of the flight)
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});
        sim.execute_melee_attack(enemy, w1);
        ASSERT_EQ(sim.get_unit(w1).hp, 1u);
        ASSERT_NE(sim.get_unit(w1).orig_order, AntUnit::kOrderHome);      // not before the hit is over
        bool ordered_home = false;
        for (int t = 0; t < 80 && !ordered_home; ++t) {
            sim.tick();
            ordered_home = (sim.get_unit(w1).orig_order == AntUnit::kOrderHome);
        }
        ASSERT_TRUE(ordered_home);

        // 3. A thief that finished a raid with loot starts for its own entrance
        sim.set_player_score(1, 100);
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);
        bool thief_home = false;
        for (int t = 0; t < 100 && !thief_home; ++t) {
            sim.tick();
            // heading for the entrance, or for the waiting tile while the entrance is claimed by another ant
            thief_home = (sim.get_unit(thief).orig_order == AntUnit::kOrderHome || sim.is_ant_in_base_queue(thief));
        }
        ASSERT_TRUE(thief_home);
        ASSERT_TRUE(sim.get_unit(thief).is_thief_steal);
        ASSERT_TRUE(sim.get_unit(thief).is_holding());
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.141: ISLANDS.LVL Corner Power-Up Hats Skill-Based Walk-On / Walk-Off
    // ------------------------------------------------------------------------
    TEST_CASE("12.141: ISLANDS.LVL Corner Power-Up Hats Skill-Based Walk-On / Walk-Off (each next hat is ordered inside the window of the one before)") {
        auto build = [](SimulationEngine& sim) {
            sim.init_test_world(30, 30, 100, 60000);
            // The top-left corner of ISLANDS.LVL: a tunnel one tile wide whose only way is a row of hats, the tiles beside
            // the column (col 1, rows 2-4) blocked:
            // (0, 4) and (0, 3): Mason Hats (type 2), (0, 2) and (0, 1): Swimmer Hats (type 5)
            sim.grid_mut().set_terrain(1, 2, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(1, 3, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(1, 4, TERRAIN_OBSTACLE);
            sim.grid_mut().place_powerup(0, 4, 2);
            sim.grid_mut().place_powerup(0, 3, 2);
            sim.grid_mut().place_powerup(0, 2, 5);
            sim.grid_mut().place_powerup(0, 1, 5);
        };

        {   // Ordering the second hat before the first one has been crossed: no path (the first hat is solid), "Can't go there."
            SimulationEngine sim;
            build(sim);
            uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{0, 5});
            sim.issue_move_order(ant, {0, 3});
            ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.has_news_event(0, 0x3A); }) >= 0);
            ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));
            ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));
            ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        }

        SimulationEngine sim;
        build(sim);
        // Worker starts at (0, 5)
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{0, 5});
        auto crossed = [&](TileCoord t) {
            return wait_ms(sim, 3000, [&]() { return sim.get_unit(ant).pos == t; }) >= 0;
        };

        // Step 1: Order to (0, 4) and wait until the ant has crossed into its tile (the walk has not landed yet)
        sim.issue_move_order(ant, {0, 4});
        ASSERT_TRUE(crossed(TileCoord{0, 4}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));

        // Step 2: Quick click inside the window! Redirect to (0, 3): the ant is snapped onto (0, 4), its walk is dropped
        sim.issue_move_order(ant, {0, 3});
        ASSERT_EQ(sim.get_unit(ant).pixel_y, 4 * 32 + 16);
        ASSERT_TRUE(sim.get_unit(ant).waypoints.empty());
        ASSERT_TRUE(crossed(TileCoord{0, 3}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker); // Still Worker!
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));      // Hat at (0, 4) untouched!

        // Step 3: Quick click to (0, 2) (the target Swimmer hat) inside the next window
        sim.issue_move_order(ant, {0, 2});
        ASSERT_EQ(sim.get_unit(ant).pixel_y, 3 * 32 + 16);
        ASSERT_TRUE(crossed(TileCoord{0, 2}));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));      // Hat at (0, 3) untouched!

        // Step 4: Player now lets the ant land on (0, 2): the pick-up happens in the tick of the landing
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return picking_up(sim.get_unit(ant)); }) >= 0);
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Swimmer); // Transformed into Swimmer!
        ASSERT_FALSE(sim.grid().has_powerup_at({0, 2}));     // Hat at (0, 2) consumed!
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 1}));      // the next hat is still there
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 3}));
        ASSERT_TRUE(sim.grid().has_powerup_at({0, 4}));

        // The clip ends 840 ms after the snap
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(ant)));
        ASSERT_EQ(sim.get_unit(ant).type, AntType::Swimmer);
    } TEST_END();

    // -------------------------------------------------------------------------
    // Effects: original creators (Ants.exe FUN_01010008 call sites), anchors and lifetimes
    // -------------------------------------------------------------------------
    TEST_CASE("12.114 Bombex Explosion Sprite Exists For Every Detonation At The Bomb Tile Top-Left For 680 ms") {
        int duds = 0, blasts = 0;
        for (uint32_t seed = 1; seed <= 120; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 600000);
            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            sim.grid_mut().place_bomb(15, 15, 1);
            sim.trigger_bomb_detonation(victim, TileCoord{15, 15}); // the ant sets the bomb off (20 % dud)

            const auto& u = sim.get_unit(victim);
            if (u.knock_flag) ++duds; else ++blasts;

            const VisualEffect* fx = nullptr;
            const auto& effects = sim.get_world_state().effects;
            for (const auto& e : effects) if (e.anim_name == "bombex") { fx = &e; break; }
            ASSERT_TRUE(fx != nullptr);                       // dud and full blast alike
            ASSERT_EQ(fx->px, 15 * 32);                       // tile top-left, not the tile centre
            ASSERT_EQ(fx->py, 15 * 32);
            ASSERT_EQ(fx->y_key, 15 * 32);                    // sorts with row*32
            ASSERT_EQ(fx->duration_ms, 680u);                 // sum of the ten Table-4 frames
            ASSERT_TRUE(fx->fog_gated);
        }
        ASSERT_TRUE(duds > 0);
        ASSERT_TRUE(blasts > 0);

        // Lethal hit: the sprite still exists and lives until its last frame ends (650 ms yes, 700 ms no)
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        const_cast<AntUnit*>(&sim.get_unit(victim))->hp = 2;
        sim.grid_mut().place_bomb(15, 15, 1);
        sim.trigger_bomb_detonation(victim, TileCoord{15, 15});
        auto has_bombex = [&]() {
            for (const auto& e : sim.get_world_state().effects) if (e.anim_name == "bombex") return true;
            return false;
        };
        ASSERT_TRUE(has_bombex());
        for (int t = 0; t < 13; ++t) sim.tick();
        ASSERT_TRUE(has_bombex());   // 650 ms
        sim.tick();
        ASSERT_FALSE(has_bombex());  // 700 ms >= 680 ms
    } TEST_END();

    TEST_CASE("12.115 Fire Wall Burnout Puff And The 180 Second Lifetime Task Arming Rule") {
        // Placed with 600 s left: the wall burns out after 180 s and leaves a sputter puff at the tile top-left
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 600000);
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{10, 11}));
            ASSERT_EQ(sim.grid().get_cell(10, 11).timer_ticks, 3600u);
            int ticks = 0;
            while (sim.has_fire_at(TileCoord{10, 11}) && ticks < 4000) { sim.tick(); ++ticks; }
            ASSERT_EQ(ticks, 3600);
            const VisualEffect* fx = nullptr;
            const auto& effects = sim.get_world_state().effects;
            for (const auto& e : effects) if (e.anim_name == "sputter") { fx = &e; break; }
            ASSERT_TRUE(fx != nullptr);
            ASSERT_EQ(fx->px, 10 * 32);
            ASSERT_EQ(fx->py, 11 * 32);
            ASSERT_EQ(fx->duration_ms, 830u);
        }
        // Placed with 180 s or less left: the lifetime task is never created, the wall does not burn out
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 600000);
            sim.set_match_time_remaining_ms(170000);
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
            ASSERT_TRUE(sim.ignite_fire(fire_ant, TileCoord{10, 11}));
            ASSERT_EQ(sim.grid().get_cell(10, 11).timer_ticks, 0u);
        }
    } TEST_END();

    TEST_CASE("12.116 Bridge Collapse Leaves A bsputter Puff And Swimmers Splash With dsplash At The Tile Top-Left") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        sim.set_bridge_at(TileCoord{20, 20}, 4, 3);
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        (void)swimmer;
        for (int t = 0; t < 3; ++t) sim.tick(); // timer expires
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{20, 20}));
        const VisualEffect* puff = nullptr;
        const VisualEffect* splash = nullptr;
        const auto& effects = sim.get_world_state().effects;
        for (const auto& e : effects) {
            if (e.anim_name == "bsputter") puff = &e;
            if (e.anim_name == "dsplash") splash = &e;
        }
        ASSERT_TRUE(puff != nullptr);
        ASSERT_EQ(puff->px, 20 * 32);
        ASSERT_EQ(puff->py, 20 * 32);
        ASSERT_EQ(puff->duration_ms, 1220u);
        ASSERT_TRUE(splash != nullptr);
        ASSERT_EQ(splash->px, 20 * 32);
        ASSERT_EQ(splash->py, 20 * 32);
        ASSERT_EQ(splash->duration_ms, 460u);
    } TEST_END();

    TEST_CASE("12.117 Death Animation Is One Of death1..death4, Played On The Ant's Own Sprite For Its Full Original Duration (The First Frame Is Booked Twice)") {
        std::set<uint16_t> seen;
        // the clips' frame sums and first frames (ants.chd): death1 920 ms / 100, death2 1000 / 100, death3 980 / 100, death4 600 / 60
        static const uint32_t kSum[4] = { 920, 1000, 980, 600 };
        static const uint32_t kFirst[4] = { 100, 100, 100, 60 };
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 600000);
            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->hp = 1;
            sim.execute_melee_attack(attacker, defender);
            // the death clip starts when the flight of the victim ends (deferred death), not at the contact
            const int dying = wait_ms(sim, 3000, [&]() { return def->state == UnitState::Dead; });
            ASSERT_TRUE(dying >= 0);
            uint16_t clip = 0;
            for (const auto& a : sim.get_world_state().ants) if (a.id == defender) clip = a.loco_clip;
            ASSERT_TRUE(clip >= 102 && clip <= 105);
            seen.insert(clip);
            for (const auto& e : sim.get_world_state().effects) ASSERT_TRUE(e.anim_name.rfind("death", 0) != 0);   // the clip is the ant's, there is no effect
            // the ant stays (alive and drawn) until the clip has ended: the frame sum plus the doubled first frame, to the 50 ms tick
            const int gone = wait_ms(sim, 2000, [&]() { return def->removed; });
            ASSERT_TRUE(gone >= 0);
            const uint32_t expect = kSum[clip - 102] + kFirst[clip - 102];
            ASSERT_TRUE(static_cast<uint32_t>(gone) + 50u >= expect && static_cast<uint32_t>(gone) <= expect + 50u);
        }
        ASSERT_EQ(seen.size(), 4u); // rand() % 4 reaches every death animation
    } TEST_END();

    TEST_CASE("12.121 Score Change Bubbles At The Home Tile (Hatch Cost And Theft), 400 ms") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 600000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        sim.set_player_score(0, 500);
        sim.set_player_score(1, 300);

        // Hatching costs 200 points: a "-200" bubble at the hill exit tile top-left and the scoredn cue
        sim.clear_audio_events();
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));
        sim.tick();
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreDn));
        const auto& bubbles = sim.get_world_state().score_bubbles;
        ASSERT_EQ(bubbles.size(), 1u);
        ASSERT_EQ(bubbles[0].amount, -200);
        ASSERT_EQ(bubbles[0].x, 21 * 32);
        ASSERT_EQ(bubbles[0].y, 21 * 32);
        ASSERT_EQ(bubbles[0].elapsed_ms, 0u);

        // Theft: the loot moves when the 3510 ms raid clip ends; the victim's home tile then shows "-stolen"
        uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);
        bool found_theft = false;
        for (int t = 0; t < 90 && !found_theft; ++t) {
            sim.tick();
            for (const auto& b : sim.get_world_state().score_bubbles) {
                if (b.amount == -MAX_THIEF_STEAL && b.x == 41 * 32 && b.y == 41 * 32) found_theft = true;
            }
        }
        ASSERT_TRUE(found_theft);

        // A bubble lives 400 ms: still shown 350 ms after it appeared, gone at 400 ms
        for (int t = 0; t < 7; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().score_bubbles.size(), 1u);
        sim.tick();
        ASSERT_EQ(sim.get_world_state().score_bubbles.size(), 0u);
    } TEST_END();

    TEST_CASE("12.118 Food Dropper Animation Timeline (9 Frames, 820 ms)") {
        static const uint8_t expected_by_tick[17] = { 0, 0, 1, 1, 2, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8 };
        for (uint32_t tick = 0; tick < 17; ++tick) {
            ASSERT_EQ(effect_spec::dropper_frame_at(tick * 50u), expected_by_tick[tick]);
        }
        uint32_t total = 0;
        for (uint32_t d : effect_spec::kDropperFrameMs) total += d;
        ASSERT_EQ(total, effect_spec::kDropperMs);
    } TEST_END();

    // -------------------------------------------------------------------------
    // Group attack order (Ants.exe FUN_010287b5 with the attack flag), armed attack mode, pick rectangles (FUN_01026904)
    // -------------------------------------------------------------------------
    TEST_CASE("12.142 A Repeated Attack Click Changes Nothing: Ants That Already Attack The Tile Are Skipped (contact time equals one click)") {
        auto contact_ms = [&](int click_every_ms) -> int {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            uint32_t att = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
            uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{16, 20});
            HUD hud;
            hud.init(0);
            hud.select_ant(att, false);
            hud.dispatch_attack_order(vic, sim);
            for (int t = 0; t < 400; ++t) {
                if (click_every_ms > 0 && t > 0 && (t * 50) % click_every_ms == 0) hud.dispatch_attack_order(vic, sim);
                sim.tick();
                if (sim.get_unit(vic).hp < 10) return (t + 1) * 50;
            }
            return -1;
        };
        const int single = contact_ms(0);
        ASSERT_TRUE(single > 1000 && single < 6000);
        for (int every : {100, 300, 600}) {
            const int spam = contact_ms(every);
            ASSERT_TRUE(spam > 0);
            ASSERT_TRUE(spam == single);   // the skipped clicks do not even touch the ant
        }
    } TEST_END();

    TEST_CASE("12.143 Group Attack: Nearest Ant Answers (once), A Second Click Skips Every Ant That Already Attacks That Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t far_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
        uint32_t near_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{14, 20});
        uint32_t mid_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 22});
        uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 20});
        const std::vector<uint32_t> ids = {far_ant, near_ant, mid_ant};
        const uint32_t ack = sim.issue_group_attack_order(ids, TileCoord{18, 20});
        ASSERT_EQ(ack, near_ant);                                       // 16 x Chebyshev: near 4, mid 6, far 8
        for (uint32_t id : ids) {
            ASSERT_EQ(sim.get_unit(id).orig_order, AntUnit::kOrderAttack);
            ASSERT_EQ(sim.get_unit(id).orig_order_tile, (TileCoord{18, 20}));
            ASSERT_EQ(sim.get_unit(id).orig_target_ant, vic);
        }
        const uint32_t serial_far = sim.get_unit(far_ant).move_serial;
        run_ms(sim, 200);
        ASSERT_EQ(sim.issue_group_attack_order(ids, TileCoord{18, 20}), 0u);   // all skipped: no order, no voice
        ASSERT_EQ(sim.get_unit(far_ant).move_serial, serial_far);
        // An ant that is not attacking that tile is not skipped: it gets the order and can acknowledge
        uint32_t idle_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 18});
        ASSERT_EQ(sim.issue_group_attack_order({idle_ant}, TileCoord{18, 20}), idle_ant);
        // Move orders to the same tile are NOT skipped by an attack click (only order 3 is)
        uint32_t mover = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 26});
        sim.issue_move_order(mover, TileCoord{18, 26});
        ASSERT_EQ(sim.get_unit(mover).orig_order, AntUnit::kOrderMove);
        ASSERT_EQ(sim.issue_group_attack_order({mover}, TileCoord{18, 26}), mover);
    } TEST_END();

    TEST_CASE("12.144 A Click On An Enemy Ant Attacks It With The Selected Ants (Cursor Mode 5) And Moves When Nothing Is There") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t att = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 20});
        uint32_t vic = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 20});
        sim.tick();
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);
        hud.select_ant(att, false);
        ViewportCamera camera;
        camera.world_x = 400;
        camera.world_y = 500;
        // the victim's body (12 px above its anchor) is under the pointer: the cursor is the attack cursor
        const int32_t vx = 15 * 32 + 16 - camera.world_x + HUD::PLAYFIELD_X;
        const int32_t vy = 20 * 32 + 16 - 12 - camera.world_y + HUD::PLAYFIELD_Y;
        ASSERT_EQ(hud.evaluate_cursor(vx, vy, sim.get_world_state(), sim.grid(), camera), CursorType::Attack);
        hud.handle_mouse_down(vx, vy, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(vx, vy, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_EQ(sim.get_unit(att).orig_order, AntUnit::kOrderAttack);
        ASSERT_EQ(sim.get_unit(att).orig_target_ant, vic);
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(vic).hp < 10; }) >= 0);
        // click on empty ground: an ordinary move
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t att2 = sim2.spawn_unit(0, AntType::Combat, TileCoord{10, 20});
        sim2.tick();
        HUD hud2;
        hud2.init(0);
        hud2.set_sim_query(&sim2);
        hud2.select_ant(att2, false);
        const int32_t gx = 20 * 32 + 16 - camera.world_x + HUD::PLAYFIELD_X;
        const int32_t gy = 20 * 32 + 16 - camera.world_y + HUD::PLAYFIELD_Y;
        hud2.handle_mouse_down(gx, gy, SDL_BUTTON_LEFT, sim2, camera);
        hud2.handle_mouse_up(gx, gy, SDL_BUTTON_LEFT, sim2, camera);
        ASSERT_EQ(sim2.get_unit(att2).orig_order, AntUnit::kOrderMove);
    } TEST_END();

    TEST_CASE("12.145 Ant Pick Rectangles Of The Original (FUN_01026a39): Worker [x-20, x+20) x [y-32, y+16), Combat [x-32, x+26) x [y-46, y+16), Last Hit Wins") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t w = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 10});
        sim.tick();
        HUD hud;
        hud.init(0);
        const auto& world = sim.get_world_state();
        auto pick = [&](int32_t x, int32_t y) -> uint32_t {
            const AntSnapshot* a = hud.pick_ant_at(world, x, y);
            return a ? a->id : 0u;
        };
        const int32_t wx = 10 * 32 + 16, wy = 10 * 32 + 16;
        ASSERT_EQ(pick(wx - 20, wy - 32), w);           // top-left corner: inside
        ASSERT_EQ(pick(wx + 19, wy + 15), w);           // bottom-right corner: inside
        ASSERT_EQ(pick(wx - 21, wy), 0u);
        ASSERT_EQ(pick(wx + 20, wy), 0u);               // right edge is exclusive
        ASSERT_EQ(pick(wx, wy - 33), 0u);
        ASSERT_EQ(pick(wx, wy + 16), 0u);               // bottom edge is exclusive
        const int32_t cx = 20 * 32 + 16, cy = 10 * 32 + 16;
        ASSERT_EQ(pick(cx - 32, cy - 46), c);
        ASSERT_EQ(pick(cx + 25, cy + 15), c);
        ASSERT_EQ(pick(cx - 33, cy), 0u);
        ASSERT_EQ(pick(cx + 26, cy), 0u);
        ASSERT_EQ(pick(cx, cy - 47), 0u);
        // a click on the head of a combat ant (37 px above the anchor) still hits it: 21.6 % of its pixels lay outside the old box
        ASSERT_EQ(pick(cx, cy - 40), c);
        // two ants one tile apart: the click inside both boxes goes to the last one scanned (the lower ant, drawn in front)
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 11});
        sim.tick();
        const auto& world2 = sim.get_world_state();
        const AntSnapshot* both = hud.pick_ant_at(world2, wx, wy + 8 - 32 + 26);   // y = 346: inside both boxes ([304,352) and [336,384))
        ASSERT_TRUE(both != nullptr);
        ASSERT_EQ(both->id, w2);
    } TEST_END();

    TEST_CASE("12.146 The ants that a level starts with never face North: SpawnAnt draws the direction as rand() % 7 + 1 (0x100ef18)") {
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/GAUNTLET.LVL"));
        size_t ants_seen = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            SimulationEngine sim;
            sim.init(lvl, seed);
            for (const auto& a : sim.get_world_state().ants) {
                ++ants_seen;
                ASSERT_TRUE(a.facing != 0);                                          // Direction::North
            }
        }
        ASSERT_TRUE(ants_seen >= 20 * 4);                                            // every seed starts with at least one ant per team
    } TEST_END();
}
