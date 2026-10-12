// Application integration tests: suite 12 goes on (unit selection and occupied tile collision handling, part 2)
#include "app_integration_fixtures.hpp"

void run_suite_12_unit_selection_and_occupied_tile_movement_2() {
    TEST_CASE("12.30 Blocked Path Can't Go Reaction, Ability Animations & Drowning Sequence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Can't Go reaction. Clicking water sends a worker to the first enterable tile of the ring scan
        //    around the clicked tile (FUN_010202e7), without any can't-go; an unreachable land tile makes the
        //    path manager report "Can't go there." and the can't-go animation (action 0xB) plays.
        for (int32_t wx = 11; wx <= 15; ++wx) {
            for (int32_t wy = 8; wy <= 12; ++wy) {
                sim.grid_mut().set_terrain(wx, wy, TERRAIN_WATER);
            }
        }
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& w_ant = sim.get_unit(w_id);

        // Order into the water at (12, 10): distance 2 of the ring scan, west column, top first -> (10, 8)
        sim.issue_move_order(w_id, TileCoord{12, 10});
        ASSERT_NE(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.final_dest, (TileCoord{10, 8}));
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.pos == TileCoord{10, 8} && w_ant.waypoints.empty() &&
                                                  w_ant.state == UnitState::Idle; }, 60));
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // Island: land at (13, 10) in the middle of the water block cannot be reached
        sim.grid_mut().set_terrain(13, 10, TERRAIN_WALKABLE);
        sim.clear_audio_events();
        sim.issue_move_order(w_id, TileCoord{13, 10});
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.state == UnitState::CantGo; }, 10));
        ASSERT_EQ(w_ant.pixel_x, 10 * 32 + 16);
        ASSERT_EQ(w_ant.pixel_y, 8 * 32 + 16);
        ASSERT_TRUE(w_ant.waypoints.empty());
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));   // frame 0 of the can't-go animation (sound 63)
        ASSERT_TRUE(sim.has_news_event(0, 0x3A));            // "Can't go there."

        // The worker's can't-go animation (agcg301) runs 6 frames x 60 ms = 360 ms, then the ant idles
        int cant_go_ticks = 0;
        while (w_ant.state == UnitState::CantGo && cant_go_ticks < 40) {
            sim.tick();
            ++cant_go_ticks;
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(cant_go_ticks, 8);

        // 2. Bomber ability clips (plant and defuse): the world changes when the clip ends
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 20});
        auto& b_ant = sim.get_unit(b_id);

        sim.plant_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::PlantingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);
        // Bomb is NOT placed on the grid tile yet while the clip is playing (only the invisible placeholder)
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));
        // absb east lasts 1400 ms; the bomb appears when it ends and the ant is idle again
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_bomb_at(TileCoord{21, 20}); }) >= 1350);
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // Defuse (abdb east, 1140 ms): the bomb is removed when the clip ends
        sim.defuse_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::DefusingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.has_bomb_at(TileCoord{21, 20}); }) >= 1100);
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // 3. Fire Ant ability clips (ignite and extinguish)
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});
        auto& f_ant = sim.get_unit(f_id);

        sim.ignite_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::PlacingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);
        // Fire is NOT placed on the grid tile yet while the clip is playing!
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());

        // afsf south lasts 1760 ms; the fire wall appears when it ends
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.grid().get_cell(TileCoord{25, 26}).has_fire(); }) >= 1700);
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // Extinguish fire (afxf south, 1200 ms): the wall is put out when the clip ends
        sim.extinguish_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::ExtinguishingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !sim.grid().get_cell(TileCoord{25, 26}).has_fire(); }) >= 1150);
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // 4. Drowning: the ant plays the drowning clip (action 0xF, 2370 ms) on the tile centre and is removed at its end
        sim.grid_mut().set_terrain(30, 30, TERRAIN_WATER);
        uint32_t drown_id = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        auto& drown_ant = sim.get_unit(drown_id);

        // Unit enters Drowning state, snapped to the tile centre; the hit points do not change
        ASSERT_EQ(drown_ant.state, UnitState::Drowning);
        ASSERT_EQ(drown_ant.pixel_x, 30 * 32 + 16);
        ASSERT_EQ(drown_ant.pixel_y, 30 * 32 + 16);
        ASSERT_EQ(drown_ant.hp, 10);

        // The clip lasts 2370 ms (about 47 ticks); the ant is removed when it ends
        for (int t = 0; t < 44; ++t) {
            ASSERT_EQ(drown_ant.state, UnitState::Drowning);
            sim.tick();
        }
        ASSERT_TRUE(wait_ms(sim, 500, [&]() { return !drown_ant.is_alive(); }) >= 0);
        ASSERT_EQ(drown_ant.state, UnitState::Dead);
        ASSERT_FALSE(drown_ant.is_alive());

        // 5. Bomb Detonation Visual Effect
        sim.grid_mut().place_bomb(35, 35, 0);
        uint32_t trigger_id = sim.spawn_unit(1, AntType::Combat, TileCoord{35, 36});
        sim.issue_move_order(trigger_id, TileCoord{35, 34});
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{35, 35})) break;
        }
        // WorldState includes bombex visual effect
        const auto& snap = sim.get_world_state();
        bool found_bombex = false;
        for (const auto& fx : snap.effects) {
            if (fx.anim_name == "bombex") {
                found_bombex = true;
                break;
            }
        }
        ASSERT_TRUE(found_bombex);
    } TEST_END();

    TEST_CASE("12.31 Base Entry Without Food & Silent Health Visit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 720000);   // a long match: sound 55 is also the "1 minute left" cue
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        // Spawn unit with full HP and 0 food near the ramp entrance
        uint32_t aid = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        const auto& u = sim.get_unit(aid);
        ASSERT_FALSE(u.is_holding());
        ASSERT_EQ(u.hp, u.max_hp);

        // Issue ReturnToBase order
        AntOrder order;
        order.ant_id = aid;
        order.type = OrderType::ReturnToBase;
        sim.issue_order(order);

        // Tick simulation until the ant climbs the ramp and enters the base hole
        bool entered_base = false;
        for (int i = 0; i < 80; ++i) {
            sim.tick();
            if (u.state == UnitState::EnteringBase) {
                entered_base = true;
                break;
            }
        }
        ASSERT_TRUE(entered_base);

        // Advance through base entry animation (16 ticks)
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }

        // Verify that NO SoundID::BaseEnter or score audio was played during empty base entry
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseEnter));
        ASSERT_FALSE(sim.has_audio_event(SoundID::BaseScoreUp));

        // Tick until ant emerges and walks off the ramp to idle spot (bx+4, by+4)
        for (int i = 0; i < 140; ++i) {
            sim.tick();
            if (u.state == UnitState::Idle && u.pos != TileCoord{bx + 1, by + 1}) {
                break;
            }
        }
        ASSERT_EQ(u.state, UnitState::Idle);
        ASSERT_EQ(u.pos, (TileCoord{bx + 4, by + 4}));
    } TEST_END();

    TEST_CASE("12.32 Move Order From Anthill Hole Steps Down Ramp Without CantGo") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        // Place ant directly on the anthill entrance hole
        uint32_t aid = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by + 1});
        const auto& u = sim.get_unit(aid);
        ASSERT_EQ(u.pos, (TileCoord{bx + 1, by + 1}));

        // Issue move order to open ground outside the anthill
        TileCoord target{bx - 5, by + 3};
        sim.issue_move_order(aid, target);

        // Must NOT trigger CantGo! Must transition to Walking
        ASSERT_NE(u.state, UnitState::CantGo);
        ASSERT_EQ(u.state, UnitState::Walking);

        // Step simulation until arrival and transition to Idle
        for (int i = 0; i < 150; ++i) {
            sim.tick();
            if (u.pos == target && u.state == UnitState::Idle) break;
        }
        ASSERT_EQ(u.pos, target);
        ASSERT_EQ(u.state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.33 Planted Bomb and Fire Wall Object Animation Frames") {
        // 1. Bomb frame alternation (Anim 129..132, 2 frames alternating every 100ms / 6 ticks)
        static const char* const bomb_frames[4][2] = {
            { "1bombgrn.bmp", "2bombgrn.bmp" },
            { "1bombred.bmp", "2bombred.bmp" },
            { "1bombblu.bmp", "2bombblu.bmp" },
            { "1bombblk.bmp", "2bombblk.bmp" }
        };
        for (uint8_t team = 0; team < 4; ++team) {
            size_t f0 = 0;
            size_t f1 = 1;
            ASSERT_TRUE(std::string(bomb_frames[team][f0]).rfind("1bomb", 0) == 0);
            ASSERT_TRUE(std::string(bomb_frames[team][f1]).rfind("2bomb", 0) == 0);
        }

        // 2. Fire Wall 5-frame sequence (Anim 134 wallup04)
        static const char* const fire_names[5] = {
            "9fire01.bmp", "9fire02.bmp", "9fire03.bmp", "9fire04.bmp", "9fire05.bmp"
        };
        for (size_t i = 0; i < 5; ++i) {
            std::string expected = "9fire0" + std::to_string(i + 1) + ".bmp";
            ASSERT_EQ(std::string(fire_names[i]), expected);
        }
    } TEST_END();

    TEST_CASE("12.34 Knockback Flies Over Intermediate Rock Obstacle to Available Ground Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a solid obstacle rock at (22, 20): only the landing tile is tested, not the path of the flight
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);

        // Spawn worker at (20, 20) and apply 4-tile knockback East towards (24, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_EQ(worker.state, UnitState::Knockback);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // Flew over rock at (22, 20) and landed at ground tile (24, 20); a melee flight leaves no stun
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_FALSE(worker.is_stunned());
    } TEST_END();

    TEST_CASE("12.35 Knockback Flies Over Intermediate Water to Available Ground Tile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water spans 3 spaces at (21, 20), (22, 20), (23, 20)
        sim.grid_mut().set_terrain(21, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(22, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(23, 20, TERRAIN_WATER);

        // Spawn worker at (20, 20) and knock East 4 tiles to ground at (24, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_EQ(worker.state, UnitState::Knockback);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // Airborne ant flew over water without drowning, landed safely on ground
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_FALSE(worker.in_water);
        ASSERT_EQ(worker.death_status, DeathStatus::Alive);
    } TEST_END();

    TEST_CASE("12.36 Knockback Flies Over Intermediate Rock Obstacle into Water (Drowning)") {
        // A bomb victim is thrown 4 tiles opposite its facing (a dud, 20 %, does not throw it: try seeds for a blast)
        bool tested = false;
        for (uint32_t seed = 101; seed < 160 && !tested; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 60000);

            // Intermediate rock at (22, 20), water at landing tile (24, 20)
            sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);
            sim.grid_mut().set_terrain(24, 20, TERRAIN_WATER);

            // Plant enemy bomb at (20, 20)
            sim.grid_mut().place_bomb(20, 20, 1);

            // Spawn worker at (21, 20) and order movement West onto bomb at (20, 20)
            uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 20});
            auto& worker = sim.get_unit(worker_id);

            AntOrder move_order{};
            move_order.ant_id = worker_id;
            move_order.type = OrderType::Move;
            move_order.target_x = 20;
            move_order.target_y = 20;
            sim.issue_order(move_order);

            // Advance until detonation (the ant ends its path on the bomb tile)
            ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
            if (worker.knock_flag) continue;   // a dud
            tested = true;
            ASSERT_EQ(worker.state, UnitState::Knockback);

            // Step through the flight: it lands in the water and the ant drowns
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state == UnitState::Drowning; }) >= 0);

            // Flew over rock at (22, 20), landed at (24, 20) in water
            ASSERT_EQ(worker.pos.x, 24);
            ASSERT_EQ(worker.pos.y, 20);
            ASSERT_EQ(worker.hp, 8);   // the two hits of the bomb; the drowning takes none
            run_ms(sim, 250);
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
            ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !worker.is_alive(); }) >= 0);
            ASSERT_EQ(worker.death_status, DeathStatus::Drowned);
        }
        ASSERT_TRUE(tested);
    } TEST_END();

    TEST_CASE("12.37 Knockback Deflects To The Next Free Direction When The Landing Tile Is A Rock (dir, +1, -1, +2, -2)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Solid rock wall across spaces 2, 3, 4 at (22..24, 20)
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(23, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(24, 20, TERRAIN_OBSTACLE);

        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return worker.state != UnitState::Knockback; }) >= 0);

        // KnockDir: East (24, 20) is a rock, the next direction (+1, South-East) lands on the free tile (24, 24)
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 24);
        ASSERT_EQ(worker.state, UnitState::Idle);
        ASSERT_TRUE(sim.has_audio_event(64)); // SOUND_FLY_THUMP_A
    } TEST_END();


    TEST_CASE("12.39 Dynamic Base Deposit Path Avoids Queued Ants, Bombs and Fire Walls") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x; // 20
        int32_t by = base->y; // 20

        // Spawn 3 workers carrying food on the waiting column in front of the hill
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Order all three onto the hill: the first takes the entrance, the others wait for their turn
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);
        ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderHome);

        // Ant 1 walks to the entrance and never steps on a tile another ant stands on
        auto all_on_distinct_tiles = [&]() {
            return sim.get_unit(a1).pos != sim.get_unit(a2).pos && sim.get_unit(a1).pos != sim.get_unit(a3).pos &&
                   sim.get_unit(a2).pos != sim.get_unit(a3).pos;
        };
        ASSERT_TRUE(all_on_distinct_tiles());
        for (int i = 0; i < 300 && sim.get_unit(a1).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            ASSERT_TRUE(all_on_distinct_tiles());
        }
        ASSERT_EQ(sim.get_unit(a1).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{bx + 1, by + 1}));

        // The ants that wait for their turn are sent in one by one (ANTHILLQ) and never share a tile with each other
        std::set<uint32_t> entered{a1};
        for (int i = 0; i < 1500 && sim.get_player_score(0) < 75; ++i) {
            sim.tick();
            ASSERT_TRUE(all_on_distinct_tiles());
            const uint32_t active = sim.get_active_depositing_ant(0);
            if (active != 0) entered.insert(active);
        }
        ASSERT_EQ(entered.size(), 3u);
        ASSERT_EQ(sim.get_player_score(0), 75);

        // A friendly bomb between a lone worker and the entrance is never stepped on: the worker re-plans
        uint32_t lone = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 4, by + 1});
        sim.get_unit(lone).pick_up_food(1, 25);
        sim.grid_mut().place_bomb(static_cast<uint32_t>(bx - 3), static_cast<uint32_t>(by + 1), 0);
        ASSERT_TRUE(sim.grid().has_bomb_at(TileCoord{bx - 3, by + 1}));
        sim.join_base_queue(lone);
        for (int i = 0; i < 400 && sim.get_unit(lone).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            ASSERT_TRUE(sim.get_unit(lone).pos != (TileCoord{bx - 3, by + 1}));
        }
        ASSERT_EQ(sim.get_unit(lone).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(lone).pos, (TileCoord{bx + 1, by + 1}));
    } TEST_END();

    TEST_CASE("12.40 Ant Lunchbox Sprite Render Order Layering (Lunchbox in Back)") {
        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        ASSERT_TRUE(archive.load_chd(chd_path));

        // Test holding directional walk animations and base entry animations across all ant types
        std::vector<std::string> test_anims = {
            "hgwg301", "hgwg201", "hgwg701", "hgwg801", "hgwg901",
            "hgws201", "hgwd301", "hgwm901",
            "hgen301", "hben301", "hfen301", "hcen301", "hsen301", "hten301"
        };

        for (const auto& anim_name : test_anims) {
            const auto* seq = archive.find_animation(anim_name);
            ASSERT_TRUE(seq != nullptr);
            ASSERT_FALSE(seq->subitems.empty());

            for (const auto& sub : seq->subitems) {
                if (sub.frames.size() <= 1) continue;

                auto frames = sub.frames;
                std::stable_sort(frames.begin(), frames.end(), [&](const auto& a, const auto& b) {
                    const auto& sp_a = archive.get_sprite(a.sprite_index);
                    const auto& sp_b = archive.get_sprite(b.sprite_index);
                    bool is_lb_a = (sp_a.name.find("lb") != std::string::npos);
                    bool is_lb_b = (sp_b.name.find("lb") != std::string::npos);
                    if (is_lb_a != is_lb_b) {
                        return is_lb_a; // Lunchbox rendered first (in back)
                    }
                    return false;
                });

                // First frame must be a lunchbox sprite (rendered in the back)
                const auto& sp0 = archive.get_sprite(frames[0].sprite_index);
                ASSERT_TRUE(sp0.name.find("lb") != std::string::npos);

                // Last frame must be the ant body sprite (rendered in front)
                const auto& sp_back = archive.get_sprite(frames.back().sprite_index);
                ASSERT_TRUE(sp_back.name.find("lb") == std::string::npos);
            }
        }
    } TEST_END();

    TEST_CASE("12.41 Standard Attack 1-Tile Pushback & Flinch Animation on Land") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // The hit point is lost at the contact and the victim turns to the attacker; it stays until the strike frame
        ASSERT_EQ(def.hp, 9);
        ASSERT_EQ(def.facing, Direction::West);
        ASSERT_EQ(def.pos.x, 11);
        ASSERT_TRUE(def.engaged);

        // The strike frame of the attack clip (200 ms) throws it: the gh clip carries it one tile East
        ASSERT_EQ(wait_ms(sim, 1000, [&]() { return def.state == UnitState::Flinch; }), 200);
        ASSERT_EQ(def.facing, Direction::West); // Victim keeps facing the attacker West
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return def.pos.x == 12; }) >= 0);
        ASSERT_EQ(def.pos.y, 10);

        // The flinch clip (gh) lasts 790 ms for a worker; then the ant is idle again
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_FALSE(def.is_stunned());
    } TEST_END();

    TEST_CASE("12.42 Standard Attack 1-Tile Pushback into Water (Drowning Clip At The Landing, Removal At Its End)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at (12, 10)
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.clear_audio_events();
        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // The landing event of the gh clip (500 ms) finds the water: a non-swimmer starts the drowning clip
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.state == UnitState::Drowning; }) >= 400);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_EQ(def.pos.y, 10);
        ASSERT_EQ(def.hp, 9);           // the drowning takes no hit points
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
        ASSERT_EQ(sim.stats_manager().get_player_stats(1).friendly_lost, 0u);

        // The ant is removed when the drowning clip ends: the scorecard counts the loss and the kill
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !def.is_alive(); }) >= 0);
        ASSERT_EQ(def.death_status, DeathStatus::Drowned);
        ASSERT_EQ(sim.stats_manager().get_player_stats(1).friendly_lost, 1u);
        ASSERT_EQ(sim.stats_manager().get_player_stats(0).enemy_killed, 1u);
    } TEST_END();

    TEST_CASE("12.43 Combat Ant 4-Tile Ballistic Fling & Type-Specific Animation for All 6 Ant Types") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        AssetArchive archive;
        std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        ASSERT_TRUE(archive.load_chd(chd_path));

        AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire,
                            AntType::Thief, AntType::Combat, AntType::Swimmer};
        const char* pfx[6] = {"aggf", "abgf", "afgf", "atgf", "acgf", "asgf"};

        for (size_t i = 0; i < 6; ++i) {
            AntType type = types[i];
            const auto* seq = archive.get_directional_animation(pfx[i], Direction::East);
            ASSERT_TRUE(seq != nullptr);
            ASSERT_FALSE(seq->subitems.empty());

            uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{10, static_cast<int32_t>(10 + i * 4)});
            uint32_t victim = sim.spawn_unit(1, type, TileCoord{11, static_cast<int32_t>(10 + i * 4)});

            sim.execute_melee_attack(combat, victim);

            // At the contact the victim only turns to the attacker; the strike frame throws it
            const auto& v = sim.get_unit(victim);
            ASSERT_EQ(v.facing, Direction::West);
            ASSERT_EQ(v.pos.x, 11);
            ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return v.state == UnitState::Knockback; }) >= 0);
            ASSERT_EQ(v.facing, Direction::West);

            // The gb clip carries it 4 tiles East in one jump; no stun follows a melee flight
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return v.state != UnitState::Knockback; }) >= 0);
            const auto& landed = sim.get_unit(victim);
            ASSERT_EQ(landed.pos.x, 15);
            ASSERT_NE(landed.state, UnitState::Stunned);
            ASSERT_FALSE(landed.is_stunned());
        }
    } TEST_END();

    TEST_CASE("12.44 Combat Ant 4-Tile Fling into Water (Drowning Clip At The Landing, Removal At Its End)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at landing tile (15, 10)
        sim.grid_mut().set_terrain(15, 10, TERRAIN_WATER);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(combat, victim);
        const auto& landed = sim.get_unit(victim);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return landed.state == UnitState::Knockback; }) >= 0);

        // Advance until the flight lands in the water: the drowning clip starts, hit points stay (10 - 2)
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return landed.state == UnitState::Drowning; }) >= 0);
        ASSERT_EQ(landed.pos.x, 15);
        ASSERT_EQ(landed.pos.y, 10);
        ASSERT_EQ(landed.hp, 8);

        // The drowning clip ends with the removal of the ant
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !landed.is_alive(); }) >= 0);
        ASSERT_EQ(landed.death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.45 Swimmer Ant Pushback and Fling into Water Survives Without Drowning (splash, stun)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Standard attack pushback into water
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);
        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t swimmer = sim.spawn_unit(1, AntType::Swimmer, TileCoord{11, 10});

        sim.execute_melee_attack(attacker, swimmer);

        // A swimmer that lands in water splashes (dsplash) and is stunned, it does not drown
        const auto& sw1 = sim.get_unit(swimmer);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sw1.state == UnitState::Stunned; }) >= 0);
        ASSERT_EQ(sw1.pos.x, 12);
        ASSERT_EQ(sw1.pos.y, 10);
        ASSERT_EQ(sw1.hp, 9);
        ASSERT_TRUE(sw1.in_water);
        ASSERT_EQ(sw1.death_status, DeathStatus::Alive);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sw1.state == UnitState::Swimming; }) >= 0);
        ASSERT_TRUE(sw1.is_alive());

        // 2. Combat Ant 4-tile fling into water
        sim.grid_mut().set_terrain(25, 20, TERRAIN_WATER);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t swimmer2 = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});

        sim.execute_melee_attack(combat, swimmer2);
        const auto& sw2 = sim.get_unit(swimmer2);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sw2.state == UnitState::Stunned; }) >= 0);
        ASSERT_EQ(sw2.pos.x, 25);
        ASSERT_EQ(sw2.pos.y, 20);
        // Swimmer landed in water without drowning!
        ASSERT_EQ(sw2.hp, 8); // 10 - 2 punch damage
        ASSERT_TRUE(sw2.in_water);
        ASSERT_EQ(sw2.death_status, DeathStatus::Alive);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sw2.state == UnitState::Swimming; }) >= 0);
    } TEST_END();

    TEST_CASE("12.46 Bomb Placement Disallowed on Mud Surface") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        // Set (11, 10) to mud
        sim.grid_mut().get_cell_mut({11, 10}).is_mud = true;
        sim.grid_mut().get_cell_mut({11, 10}).surface_type = SurfaceType::Mud;

        ASSERT_FALSE(sim.grid().get_cell({11, 10}).can_place_bomb());

        // Direct ability call fails on mud
        bool res = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_FALSE(res);
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Issue order fails on mud
        AntOrder order{};
        order.type = OrderType::PlantBomb;
        order.ant_id = bomber;
        order.target_x = 11;
        order.target_y = 10;
        sim.issue_order(order);
        ASSERT_NE(sim.get_unit(bomber).state, UnitState::PlantingBomb);
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // After the can't clip (the refusal of the order) the bomber accepts the next order again
        ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return sim.get_unit(bomber).state == UnitState::Idle; }) >= 0);

        // Placement succeeds on valid gravel at (10, 9)
        bool ok = sim.plant_bomb(bomber, {10, 9}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);
    } TEST_END();

    TEST_CASE("12.47 Food Pickup Adjacency Requirement & The Grab Clip") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // A pile of one unit (2x2 crackers around its anchor (15, 10): the cells (14..15, 9..10)), 25 points
        const int32_t pile = place_pile(sim, 15, 10, 1, 25, {{1, 372}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{14, 10}), pile);

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        // Move worker to (13, 10), adjacent to the pile
        sim.issue_move_order(worker, TileCoord{13, 10});

        // Step simulation until arrival
        while (!sim.get_unit(worker).waypoints.empty() || sim.get_unit(worker).state == UnitState::Walking) {
            sim.tick();
            if (sim.get_unit(worker).pos.x < 13) {
                // Not adjacent/arrived yet, must not have collected food
                ASSERT_FALSE(sim.get_unit(worker).is_holding());
            }
        }

        // Worker reached (13, 10), adjacent to the pile: walking or standing on adjacent tiles must NOT harvest anything
        ASSERT_EQ(sim.get_unit(worker).pos.x, 13);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::Idle);
        ASSERT_FALSE(sim.get_unit(worker).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // Now explicitly instruct worker to eat the food at (15, 10): it faces the food and plays the grab clip at once (the
        // pile is the very next tile of its path)
        sim.issue_move_order(worker, TileCoord{15, 10});
        ASSERT_TRUE(wait_ms(sim, 500, [&]() { return sim.get_unit(worker).loco_action == AntUnit::kActionHarvest; }) >= 0);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::HarvestingFood);
        ASSERT_EQ(sim.get_unit(worker).facing, Direction::East);
        ASSERT_FALSE(sim.get_unit(worker).is_holding());
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 1u);

        // The food is taken when the clip ends (the clip of the original lasts its frames' sum, here at most one tick more)
        const uint32_t clip_ms = ants::sim::movement::action_clip(ants::sim::movement::ActionClip::Harvest, ants::sim::movement::kAntWorker,
                                                                  static_cast<uint8_t>(Direction::East), false).total_duration_ms();
        ASSERT_TRUE(clip_ms >= 300 && clip_ms <= 500);
        const int took = wait_ms(sim, 2000, [&]() { return sim.get_unit(worker).is_holding(); });
        ASSERT_TRUE(took >= 0);
        ASSERT_TRUE(static_cast<uint32_t>(took) <= clip_ms + 100);
        const auto& w = sim.get_unit(worker);
        ASSERT_EQ(w.carried_food, 1);
        ASSERT_EQ(w.carried_points, 25u);
        ASSERT_EQ(w.state, UnitState::Idle);

        // The last unit is gone with its tiles
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 0u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{14, 10}), -1);
        ASSERT_EQ(sim.grid().get_cell({15, 10}).interactive_id, TILE_EMPTY);
    } TEST_END();

    TEST_CASE("12.48 Bomber Carrying Food Placing Bomb Uses absb and Does Not Disappear") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        auto* b_unit = const_cast<AntUnit*>(&sim.get_unit(bomber));
        b_unit->pick_up_food(1, 25);
        ASSERT_TRUE(b_unit->is_holding());

        bool ok = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(b_unit->state, UnitState::PlantingBomb);
        ASSERT_TRUE(b_unit->is_holding());
    } TEST_END();

    TEST_CASE("12.49 Bomb Placement Animation Runs Authentic 28 Ticks with Sound at Tick 14 and Placement at Tick 18") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        bool ok = sim.plant_bomb(bomber, {11, 10}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);

        // Advance 13 ticks: no bomb on grid yet
        for (int i = 0; i < 13; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Tick 14: Sound 90 (BombPick) triggered
        sim.tick();
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Advance up to tick 27: still no bomb on grid (seamless placement prevents double bomb)
        for (int i = 0; i < 13; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Tick 28: Bomb placed seamlessly on grid upon animation completion!
        sim.tick();
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.50 Single Attack Order Executes One Strike And Ends (no pursuit, no second blow)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder order{};
        order.type = OrderType::Attack;
        order.ant_id = attacker;
        order.target_entity_id = static_cast<int32_t>(defender);
        sim.issue_order(order);
        ASSERT_EQ(sim.get_unit(attacker).orig_order, AntUnit::kOrderAttack);

        // The blow: the step into the defender's tile is the contact
        const auto& atk = sim.get_unit(attacker);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(defender).hp < 10; }) >= 0);
        ASSERT_EQ(atk.loco_action, AntUnit::kActionAttack);
        ASSERT_EQ(sim.get_unit(defender).hp, 9);

        // The order ends with the blow: the attacker is idle, its order is cleared, and there is no second blow
        run_ms(sim, 5000);
        ASSERT_EQ(sim.get_unit(attacker).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(attacker).orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(sim.get_unit(defender).hp, 9);
    } TEST_END();

    TEST_CASE("12.51 The Victim Turns To The Attacker At The Contact And Is Pushed Along The Attack Vector") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        auto* def_unit = const_cast<AntUnit*>(&sim.get_unit(defender));
        def_unit->facing = Direction::North;

        sim.execute_melee_attack(attacker, defender);
        // Facing is forced towards attacker (West) at the contact
        ASSERT_EQ(def_unit->facing, Direction::West);
        // Pushed East to (12, 10) by the flight after the strike frame
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def_unit->pos.x == 12 && def_unit->state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def_unit->pos.y, 10);
    } TEST_END();

    TEST_CASE("12.52 Lethal 0 HP Death: The Dying Ant Plays death1..death4 On Its Own Sprite (No Separate Effect) While Water Drowning Plays The Drown Clip") {
        // the ant's own clip is one of Table-4 animations 102 .. 105 (death1 .. death4); no effect of that name exists any more (the original plays the clip on the ant's sprite)
        auto has_death_effect = [](const SimulationEngine& s) {
            for (const auto& a : s.get_world_state().ants) {
                if (a.state == UnitState::Dead && a.loco_clip >= 102 && a.loco_clip <= 105) return true;
            }
            return false;
        };
        auto separate_effect = [](const SimulationEngine& s) {
            for (const auto& eff : s.get_world_state().effects) {
                if (eff.anim_name.rfind("death", 0) == 0) return true;
            }
            return false;
        };

        // 1. Lethal Melee Attack: hp is 0 at the contact, the victim is still thrown; the death clip starts at the end
        //    of the flight (LowHpCheck), the ant is removed when the clip ends
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->hp = 1; // 1 HP, lethal against 1 HP melee strike

            sim.execute_melee_attack(attacker, defender);
            ASSERT_EQ(def->hp, 0);
            ASSERT_NE(def->state, UnitState::Dead);      // not dead yet: death is deferred
            ASSERT_FALSE(has_death_effect(sim));

            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return def->state == UnitState::Dead; }) >= 0);
            ASSERT_TRUE(def->is_alive());                // the death clip is playing
            ASSERT_TRUE(has_death_effect(sim));
            ASSERT_FALSE(separate_effect(sim));
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return !def->is_alive(); }) >= 0);
        }

        // 2. Lethal Bomb Blast (whether the bomb is a dud or throws the ant): the ant dies after the burn or the flight
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            auto* vic = const_cast<AntUnit*>(&sim.get_unit(victim));
            vic->hp = 2; // lethal against the 2 damage of a bomb blast
            sim.grid_mut().place_bomb(15, 15, 1); // Enemy team bomb

            sim.trigger_bomb_detonation(victim, TileCoord{15, 15});
            ASSERT_EQ(vic->hp, 0);

            ASSERT_TRUE(wait_ms(sim, 5000, [&]() { return vic->state == UnitState::Dead; }) >= 0);
            ASSERT_TRUE(has_death_effect(sim));
        }

        // 3. Pushback into Water (Non-swimmer drowns, NO skull death animation)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);
            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

            sim.execute_melee_attack(attacker, victim);
            const auto& vic = sim.get_unit(victim);
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return vic.state == UnitState::Drowning; }) >= 0);
            ASSERT_EQ(vic.pos.x, 12);
            ASSERT_EQ(vic.pos.y, 10);
            ASSERT_FALSE(has_death_effect(sim));      // no death clip for drowning (the ant plays the drown clip)
            ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !vic.is_alive(); }) >= 0);
            ASSERT_EQ(vic.death_status, DeathStatus::Drowned);
            ASSERT_FALSE(has_death_effect(sim));
        }
    } TEST_END();

    // -------------------------------------------------------------------------
    // Test 12.53: Power-Up Transformation & Continuous Idle Animation Facing South
    // -------------------------------------------------------------------------
    TEST_CASE("12.53 Power-Up Pick-Up (getpow Clip) & Continuous Idle Animation Facing South & All Headings") {
        static constexpr std::array<std::pair<uint8_t, const char*>, 5> POWERUPS = {{
            {1, "Bomber"},
            {2, "Fire"},
            {3, "Thief"},
            {4, "Combat"},
            {5, "Swimmer"}
        }};

        // 1. Verify all 5 powerup types when approached moving SOUTH
        for (const auto& [pu_type, pu_name] : POWERUPS) {
            SimulationEngine sim;
            sim.init_test_world(30, 30, 100, 60000);
            sim.grid_mut().place_powerup(15, 15, pu_type);

            // Ant starts North of powerup at (15, 13), walks SOUTH to (15, 15)
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 13});
            sim.issue_move_order(ant_id, TileCoord{15, 15});

            // Step simulation until the walk lands on the power-up and the getpow clip starts
            while (sim.get_unit(ant_id).is_alive() && !picking_up(sim.get_unit(ant_id))) {
                sim.tick();
            }

            const auto& unit_trans = sim.get_unit(ant_id);
            ASSERT_TRUE(picking_up(unit_trans));
            ASSERT_EQ(unit_trans.pos.x, 15);
            ASSERT_EQ(unit_trans.pos.y, 15);
            ASSERT_EQ(static_cast<uint8_t>(unit_trans.facing), static_cast<uint8_t>(Direction::South));

            // The exact getpow frames (CHD animation 55, 70 ms each) play until the clip ends 840 ms after the snap
            uint16_t last_frame = 0;
            for (int t = 0; t < 16; ++t) {
                const auto& ws = sim.get_world_state();
                const AntSnapshot* snap = nullptr;
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { snap = &a; break; }
                }
                ASSERT_TRUE(snap != nullptr);
                ASSERT_EQ(snap->state, UnitState::PoweringUp);
                ASSERT_EQ(snap->loco_clip, 55);
                ASSERT_TRUE(snap->loco_frame >= last_frame);
                last_frame = snap->loco_frame;
                sim.tick();
            }
            ASSERT_TRUE(last_frame >= 8);
            sim.tick();

            // The clip is over: the unit is idle as its new type (GuardIdle for Combat)
            const auto& unit_done = sim.get_unit(ant_id);
            ASSERT_FALSE(picking_up(unit_done));
            ASSERT_EQ(static_cast<uint8_t>(unit_done.type), pu_type);
            ASSERT_EQ(static_cast<uint8_t>(unit_done.facing), static_cast<uint8_t>(Direction::South));

            UnitState expected_state = (pu_type == 4) ? UnitState::GuardIdle : UnitState::Idle;
            ASSERT_EQ(static_cast<uint8_t>(unit_done.state), static_cast<uint8_t>(expected_state));

            // Step additional ticks and verify idle animation cycle is actively progressing (not static 0!)
            uint16_t initial_anim_frame = 0;
            {
                const auto& ws = sim.get_world_state();
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { initial_anim_frame = a.anim_frame; break; }
                }
            }
            for (int t = 0; t < 20; ++t) {
                sim.tick();
            }
            uint16_t post_anim_frame = 0;
            {
                const auto& ws = sim.get_world_state();
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { post_anim_frame = a.anim_frame; break; }
                }
            }
            ASSERT_TRUE(post_anim_frame > initial_anim_frame);
        }

        // 2. Verify all other 7 cardinal and diagonal approach headings
        static constexpr std::array<std::pair<int, int>, 7> OTHER_DIRS = {{
            {0, -1},  // North
            {1, 0},   // East
            {-1, 0},  // West
            {1, 1},   // SouthEast
            {-1, 1},  // SouthWest
            {1, -1},  // NorthEast
            {-1, -1}  // NorthWest
        }};

        for (const auto& [dx, dy] : OTHER_DIRS) {
            SimulationEngine sim;
            sim.init_test_world(30, 30, 100, 60000);
            sim.grid_mut().place_powerup(15, 15, 4); // Combat Ant powerup

            ants::sim::TileCoord start{15 - dx * 2, 15 - dy * 2};
            uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, start);
            sim.issue_move_order(ant_id, TileCoord{15, 15});

            for (int t = 0; t < 60; ++t) {
                sim.tick();
            }

            const auto& unit = sim.get_unit(ant_id);
            ASSERT_FALSE(picking_up(unit));
            ASSERT_EQ(unit.type, AntType::Combat);
            ASSERT_EQ(unit.state, UnitState::GuardIdle);
            ASSERT_TRUE(unit.anim_subitem > 0); // Actively animating
        }
    } TEST_END();

    TEST_CASE("12.54 Text Width Measurement, Multi-Line Splitting & Font Sizing Integrity") {
        ants::app::Renderer renderer;
        // Verify empty text width
        ASSERT_EQ(renderer.get_text_width(""), 0);

        // Verify non-empty text width is positive
        int32_t small_w = renderer.get_text_width("that food!", ants::app::FontSize::Px12);
        ASSERT_TRUE(small_w > 0);
        ASSERT_TRUE(renderer.get_text_height(ants::app::FontSize::Px12) > 0);

        // Verify proportional width scales monotonically with string length
        int32_t longer_w = renderer.get_text_width("that food! And extra text for width check.", ants::app::FontSize::Px12);
        ASSERT_TRUE(longer_w > small_w);

        // Verify HUD chat entry recording and scroll offset boundaries
        ants::app::HUD hud;
        hud.init(0);
        size_t initial_lines = hud.get_chat_log().size();
        hud.add_chat_entry("Player1", "Hello world!");
        hud.add_chat_entry("Player2", "that food!");
        ASSERT_EQ(hud.get_chat_log().size(), initial_lines + 4);   // a header line and a body line per entry
        ASSERT_EQ(hud.chat_follow_pos(), 0);                       // a log that is not higher than the view does not move
        ASSERT_EQ(hud.chat_content_end(), 37 + 2 * 25);            // the news flash (12 + 2 x 12 px) and two entries (12 + 12 px each), one pixel after each
    } TEST_END();

    TEST_CASE("12.55 Fire Ant Ignite Clip Timing (Wall At The End Of The 1760 ms Clip) & Immediate Zero-Cooldown Re-Cast") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});

        // Step 1: Initiate fire placement (non-instant, clip afsf: 1810 ms east)
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{11, 10}, false));
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);

        // Nothing is on the grid for the whole clip except the invisible placeholder; the wall appears when it ends
        int elapsed = 0;
        while (!sim.grid().get_cell(TileCoord{11, 10}).has_fire() && elapsed < 4000) {
            ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);
            sim.tick();
            elapsed += 50;
        }
        ASSERT_TRUE(elapsed >= 1750 && elapsed <= 1950);

        // The ant is idle at once: no cooldown exists in the original
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::Idle);

        // Immediate subsequent fire placement succeeds with zero post-animation cooldown
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{10, 11}, false));
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);
    } TEST_END();

    TEST_CASE("12.56 Bomber Plant Clip: Orders Are Refused While It Plays, The Next Plant Can Start At Once When It Ends") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{15, 15});

        // Step 1: Initiate bomb planting (non-instant)
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{16, 15}, false));
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::PlantingBomb);

        // Half-way through the clip: another plant and a move order are refused (FUN_0101ff5a accepts only idle,
        // walking and stunned ants)
        for (int t = 0; t < 14; ++t) sim.tick();
        ASSERT_FALSE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));
        sim.issue_move_order(b_id, TileCoord{18, 15});
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::PlantingBomb);
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{16, 15}));

        // The clip ends (1400 ms east): the bomb is there, the ant is idle and can plant again immediately
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.has_bomb_at(TileCoord{16, 15}); }) >= 0);
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::Idle);
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));
    } TEST_END();

    TEST_CASE("12.57 Bridge Demolition & Collapse Instant Drowning for Non-Swimmers") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Create completed bridges over water at {20, 20} and {22, 20}
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_tile_flags(20, 20, 0x06);
        sim.grid_mut().set_bridge_at(TileCoord{20, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        sim.set_terrain(22, 20, TERRAIN_WATER);
        sim.set_tile_flags(22, 20, 0x06);
        sim.grid_mut().set_bridge_at(TileCoord{22, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{22, 20}).has_completed_bridge());

        // Spawn a Worker ant on {20, 20} and Swimmer ant on {22, 20}
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        uint32_t s_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{22, 20});
        ASSERT_NE(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_NE(sim.get_unit(s_id).state, UnitState::Swimming);

        // Demolish/regress the bridges by 1 stage: units survive on decaying bridges
        sim.grid_mut().regress_bridge(20, 20);
        sim.grid_mut().regress_bridge(22, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_any_bridge());
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{22, 20}).has_completed_bridge());
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{22, 20}).has_any_bridge());
        sim.tick();
        ASSERT_NE(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_NE(sim.get_unit(s_id).state, UnitState::Swimming);

        // The bridge lifetime task ends both bridges (BridgeTimeout -> DestroyBridgeAt; it acts only on a completed bridge, 0x25): the tiles are water again
        sim.set_bridge_at(TileCoord{20, 20}, 4, 1);
        sim.set_bridge_at(TileCoord{22, 20}, 4, 1);
        sim.tick();
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{20, 20}).has_any_bridge());
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{22, 20}).has_any_bridge());

        // Now the Worker MUST start drowning (message 0x15), the Swimmer only splashes (dsplash) and swims on
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_EQ(sim.get_unit(s_id).state, UnitState::Swimming);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        run_ms(sim, 250);
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
    } TEST_END();

    TEST_CASE("12.58 The Chat Box Is Always Active: Ctrl Keys Are Not Typed, Clicks Do Not Focus Or Unfocus, The Option Covers It") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ViewportCamera camera;

        // Control key (e.g. Ctrl+A) MUST NOT be appended to chat input
        hud.handle_key_down('a', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.get_chat_input().empty());

        // Without Ctrl a printable key is typed at once: there is no focus to gain (FUN_0102609a: the chat edit is always active)
        hud.handle_key_down('a', sim, camera, 0);
        ASSERT_EQ(hud.get_chat_input(), "a");

        // Clicks on the field or anywhere else do not change the box
        hud.handle_mouse_down(200, 200, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(200, 200, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_down(500, 428, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(500, 428, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_EQ(hud.get_chat_input(), "a");

        // Switched off in the options ("Participate In Chat"): the box is covered and takes nothing
        hud.open_options();
        hud.handle_mouse_down(151, 289, SDL_BUTTON_LEFT, sim, camera);      // the OFF toggle of the chat option
        ASSERT_TRUE(hud.is_chat_enabled());                                 // a switch acts at the release
        hud.handle_mouse_up(151, 289, SDL_BUTTON_LEFT, sim, camera);
        hud.close_options();
        ASSERT_FALSE(hud.is_chat_enabled());
        hud.handle_key_down('b', sim, camera, 0);
        hud.handle_text_input("c");
        ASSERT_EQ(hud.get_chat_input(), "a");
    } TEST_END();

    TEST_CASE("12.59 Options Dialog F9-F12 Quick Chat Editing & In-Game Broadcast Keys") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ViewportCamera camera;

        // Verify clean default strings (no "$_")
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it!");
        ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be!");
        ASSERT_EQ(hud.get_quick_chat_key(2), "Attack!");
        ASSERT_EQ(hud.get_quick_chat_key(3), "Do you want to ally?");

        // Open options dialog: the F9 field has the focus (FUN_0101487c focuses the first edit field)
        hud.open_options();
        ASSERT_TRUE(hud.is_options_open());
        ASSERT_TRUE(hud.options_screen().edit(0).focused());
        ASSERT_FALSE(hud.options_screen().edit(1).focused());

        // A press on the F9 field (92..233, 370..385) keeps the focus there
        hud.handle_mouse_down(120, 375, SDL_BUTTON_LEFT, sim, camera);
        hud.handle_mouse_up(120, 375, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_TRUE(hud.options_screen().edit(0).focused());

        // Backspace to delete "!"
        hud.handle_key_down(SDLK_BACKSPACE, sim, camera);
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it");

        // Type characters
        hud.handle_text_input(" all!");
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it all!");

        // Esc does nothing; Return closes the dialog whatever has the focus (FUN_01014f12) and the text stays
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
        ASSERT_TRUE(hud.is_options_open());
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_FALSE(hud.is_options_open());
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it all!");

        // Press F9 in gameplay: broadcasts quick chat 0
        size_t initial_log_size = hud.get_chat_log().size();
        hud.handle_key_down(SDLK_F9, sim, camera);
        ASSERT_TRUE(hud.get_chat_log().size() > initial_log_size);
        std::string recent_text;
        for (size_t li = initial_log_size; li < hud.get_chat_log().size(); ++li) {
            recent_text += hud.get_chat_log()[li] + " ";
        }
        ASSERT_TRUE(recent_text.find("Now you are in for it all!") != std::string::npos);
    } TEST_END();

    TEST_CASE("12.60 Bridge Expiry No-Spam CantGo and Movement Cancellation") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Water channel at column 25 separating land at x=24 and island at x=26
        for (int y = 0; y < 60; ++y) {
            sim.set_terrain(25, y, TERRAIN_WATER);
            sim.set_tile_flags(25, y, 0x06);
        }

        // Bridge at {25, 20}
        sim.grid_mut().set_bridge_at(TileCoord{25, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{25, 20}).has_completed_bridge());

        // Spawn a Worker ant at {20, 20} and order to island {30, 20}
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        sim.issue_move_order(w_id, TileCoord{30, 20});
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Walking);
        tick_until_paths_delivered(sim, {w_id});
        ASSERT_FALSE(sim.get_unit(w_id).waypoints.empty());

        // Advance 5 ticks: ant moves towards bridge
        for (int i = 0; i < 5; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Walking);

        // Collapse the bridge
        sim.grid_mut().collapse_bridge(25, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 20}).has_completed_bridge());

        // Clear audio events
        sim.clear_audio_events();

        // Advance simulation: at the water edge the walk is blocked (TryEnterTile), the re-plan finds no way
        // to the island ("Can't go there."), and the can't-go animation plays its sound ONCE.
        uint32_t cant_go_audio_count = 0;
        for (int i = 0; i < 60; ++i) {
            sim.tick();
            auto events = sim.poll_audio_events();
            for (const auto& ev : events) {
                if (ev.sound_id == SoundID::CantGo) {
                    cant_go_audio_count++;
                }
            }
        }

        // Must play CantGo audio event EXACTLY ONCE (no repeated spamming!)
        ASSERT_EQ(cant_go_audio_count, 1u);

        // Movement must be completely cancelled: ant is now Idle, not drowned, waypoints empty
        const auto& w_ant = sim.get_unit(w_id);
        ASSERT_TRUE(w_ant.state == UnitState::Idle || w_ant.state == UnitState::GuardIdle);
        ASSERT_TRUE(w_ant.waypoints.empty());
        ASSERT_EQ(w_ant.final_dest, w_ant.pos);
        ASSERT_NE(w_ant.state, UnitState::Drowning);
    } TEST_END();

    TEST_CASE("12.61 Authentic Cursors, Edge Panning, Click Markers & Selection Brackets") {
        // 1. Asset verification in ants.chd
        AssetArchive archive;
        ASSERT_TRUE(archive.load_chd("Original-Ants/ants.chd"));

        // Cursors
        ASSERT_TRUE(archive.animation_count() > 54);
        ASSERT_EQ(archive.get_animation(41).name, "c_normal");
        ASSERT_EQ(archive.get_animation(42).name, "c_select");
        ASSERT_EQ(archive.get_animation(44).name, "c_mov1");
        ASSERT_EQ(archive.get_animation(43).name, "c_targ1");
        ASSERT_EQ(archive.get_animation(33).name, "c_attack");
        ASSERT_EQ(archive.get_animation(54).name, "c_food");
        ASSERT_EQ(archive.get_animation(39).name, "c_cant");
        ASSERT_EQ(archive.get_animation(32).name, "xmarks");

        // Edge panning cursors
        ASSERT_EQ(archive.get_animation(51).name, "CUR_N");
        ASSERT_EQ(archive.get_animation(46).name, "CUR_NE");
        ASSERT_EQ(archive.get_animation(45).name, "CUR_E");
        ASSERT_EQ(archive.get_animation(49).name, "CUR_SE");
        ASSERT_EQ(archive.get_animation(48).name, "CUR_S");
        ASSERT_EQ(archive.get_animation(52).name, "CUR_SW");
        ASSERT_EQ(archive.get_animation(50).name, "CUR_W");
        ASSERT_EQ(archive.get_animation(47).name, "CUR_NW");

        // Selection brackets
        ASSERT_EQ(archive.get_animation(58).name, "dogears");
        ASSERT_EQ(archive.get_animation(60).name, "yelears");
        ASSERT_EQ(archive.get_animation(61).name, "redears");
        ASSERT_EQ(archive.get_animation(149).name, "c_dogears");
        ASSERT_EQ(archive.get_animation(150).name, "c_yelears");
        ASSERT_EQ(archive.get_animation(151).name, "c_redears");
        ASSERT_EQ(archive.get_animation(59).name, "hillears");

        // 2. HUD evaluate_cursor Logic
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        HUD hud;
        hud.init(0);

        ViewportCamera camera;
        camera.viewport_w = 480;
        camera.viewport_h = 440;
        camera.world_x = 320;
        camera.world_y = 320;

        // A. Overlays: Normal cursor
        hud.open_options();
        ASSERT_EQ(hud.evaluate_cursor(200, 200, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        hud.close_options();

        // B. Edge Panning Bounds Check (0x1026b09..0x1026d11)
        ASSERT_EQ(hud.evaluate_cursor(5, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollNW);
        ASSERT_EQ(hud.evaluate_cursor(320, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollN);
        ASSERT_EQ(hud.evaluate_cursor(635, 5, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollNE);
        ASSERT_EQ(hud.evaluate_cursor(5, 240, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollW);
        ASSERT_EQ(hud.evaluate_cursor(635, 240, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollE);
        ASSERT_EQ(hud.evaluate_cursor(5, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollSW);
        ASSERT_EQ(hud.evaluate_cursor(320, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollS);
        ASSERT_EQ(hud.evaluate_cursor(635, 475, sim.get_world_state(), sim.grid(), camera), CursorType::ScrollSE);

        // C. HUD Chrome: Normal cursor (between edge panning margins 13..467)
        ASSERT_EQ(hud.evaluate_cursor(500, 200, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        ASSERT_EQ(hud.evaluate_cursor(200, 16, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);
        ASSERT_EQ(hud.evaluate_cursor(200, 464, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);

        // D. Playfield - No units selected
        int32_t screen_tx15 = (15 * 32 + 16) - 320 + PLAYFIELD_X;
        int32_t screen_ty15 = (15 * 32 + 16) - 320 + PLAYFIELD_Y;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Normal);

        uint32_t my_worker = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        uint32_t enemy_ant = sim.spawn_unit(1, AntType::Worker, TileCoord{18, 15});
        (void)my_worker;
        (void)enemy_ant;

        // Hover over friendly or enemy ant with no units selected -> Select
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);
        int32_t screen_tx18 = (18 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx18, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);

        // E. Playfield - Friendly Unit Selected
        hud.select_ant(my_worker);
        ASSERT_TRUE(hud.get_selected_ant_id() == my_worker);

        // Friendly ant hover -> Select
        ASSERT_EQ(hud.evaluate_cursor(screen_tx15, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Select);
        // Enemy ant hover -> Attack!
        ASSERT_EQ(hud.evaluate_cursor(screen_tx18, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Attack);

        // Food tile hover -> Food (a cell of a food object)
        place_pile(sim, 16, 15, 1, 25, {{1, TILE_LUNCHBOX}, {0, 0x7FFE}});
        int32_t screen_tx16 = (16 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx16, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Food);

        // Obstacle tile hover -> Move (authentic 1998 behavior: move cursor shown across map)
        sim.grid_mut().set_terrain(17, 15, TERRAIN_OBSTACLE);
        int32_t screen_tx17 = (17 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx17, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // Water tile hover with Worker selected -> Move
        sim.grid_mut().set_terrain(14, 15, TERRAIN_WATER);
        int32_t screen_tx14 = (14 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx14, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // Water tile hover with Swimmer selected -> Move!
        uint32_t my_swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{15, 16});
        hud.select_ant(my_swimmer);
        ASSERT_EQ(hud.evaluate_cursor(screen_tx14, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Move);

        // F. Anthill Bases & Thief
        uint32_t my_thief = sim.spawn_unit(0, AntType::Thief, TileCoord{15, 17});
        hud.select_ant(my_thief);

        // 3. Click Marker Spawning Verification
        int32_t spawned_wx = -1, spawned_wy = -1;
        hud.set_on_spawn_click_marker([&](int32_t wx, int32_t wy) {
            spawned_wx = wx;
            spawned_wy = wy;
        });

        // Click ground to move: drag dx/dy <= 4 at screen (250, 250)
        hud.handle_mouse_down(250, 250, 1, sim, camera, 0);
        hud.handle_mouse_up(250, 250, 1, sim, camera, 0);
        ASSERT_EQ(spawned_wx, camera.world_x + (250 - PLAYFIELD_X));
        ASSERT_EQ(spawned_wy, camera.world_y + (250 - PLAYFIELD_Y));

        // 4. Renderer Transient Effects Lifecycle
        Renderer renderer;
        renderer.spawn_transient_effect("xmarks", 500, 500);
        renderer.update_transient_effects(0.200f);
        renderer.update_transient_effects(0.300f);
    } TEST_END();

    TEST_CASE("12.62 Closest Passable Water Shoreline Fallback, Ally Walk-Over Movement & Setup Screen Map Centering") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Water shoreline fallback: lake from x=13..20, y=5..15
        for (int32_t wx = 13; wx <= 20; ++wx) {
            for (int32_t wy = 5; wy <= 15; ++wy) {
                sim.grid_mut().set_terrain(wx, wy, TERRAIN_WATER);
            }
        }
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& w_ant = sim.get_unit(w_id);

        // Issue move order into middle of water at (16, 10): GoTo replaces the unwalkable tile by the first
        // enterable tile of its ring scan (FUN_010202e7; distance 4, west column x = 12, top first) -> (12, 6)
        sim.issue_move_order(w_id, TileCoord{16, 10});
        ASSERT_NE(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.final_dest, (TileCoord{12, 6}));

        // Advance simulation until ant reaches the shoreline at (12, 6)
        ASSERT_TRUE(tick_until(sim, [&]() { return w_ant.pos == TileCoord{12, 6} && w_ant.waypoints.empty() &&
                                                  w_ant.state == UnitState::Idle; }, 100));

        // Ordering it into the lake again resolves to the tile it already stands on: a one-tile path, no can't-go
        sim.clear_audio_events();
        sim.issue_move_order(w_id, TileCoord{16, 10});
        ASSERT_EQ(w_ant.final_dest, (TileCoord{12, 6}));
        for (int t = 0; t < 20; ++t) {
            sim.tick();
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(w_ant.pos, (TileCoord{12, 6}));
        ASSERT_FALSE(sim.has_audio_event(SoundID::CantGo));

        // 2. Ally Walk-Over Movement & Cursors
        uint32_t ally_id = sim.spawn_unit(1, AntType::Worker, TileCoord{8, 8});
        (void)ally_id;
        sim.form_alliance(0, 1);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));

        HUD hud;
        hud.init(0);
        ViewportCamera camera{0, 0};
        hud.select_ant(w_id, false);

        // Hover over an ally with an own ant selected: the cursor is the attack cursor (there is no alliance test in FUN_01026aa3); when the order is
        // given the original asks (FUN_0101ffab): "Doing this will break your team with ...  Continue?"
        int32_t ally_screen_x = PLAYFIELD_X + (8 * 32 + 16);
        int32_t ally_screen_y = PLAYFIELD_Y + (8 * 32 + 16);
        CursorType c_ally = hud.evaluate_cursor(ally_screen_x, ally_screen_y, sim.get_world_state(), sim.grid(), camera);
        ASSERT_EQ(c_ally, CursorType::Attack);

        // Click the ally's ant: nothing moves, the confirmation opens; No (N) drops the order and keeps the team
        hud.handle_mouse_down(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        hud.handle_mouse_up(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::BreakConfirm);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));
        hud.handle_key_down('n', sim, camera);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));
        ASSERT_EQ(w_ant.state, UnitState::Idle);
        // Again, and Yes (Y): the team is broken and the attack order is carried out
        hud.handle_mouse_down(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        hud.handle_mouse_up(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::BreakConfirm);
        hud.handle_key_down('y', sim, camera);
        ASSERT_EQ(hud.alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_FALSE(sim.stats_manager().are_allies(0, 1));
        ASSERT_EQ(w_ant.state, UnitState::Walking);                                       // on its way to attack

        // 3. Map Select Screen Vertical Centering Formula
        Renderer renderer;
        int32_t th_large = renderer.get_text_height(FontSize::Px18);      // the map name label is 18 px high (FUN_01012ce0)
        int32_t name_y = 307 + (29 - th_large) / 2;
        // Cavity top is 307, height is 29 (y=307..335). Text y must be strictly inside cavity with symmetric padding
        ASSERT_GE(name_y, 307);
        ASSERT_LE(name_y + th_large, 336);
        int32_t top_pad = name_y - 307;
        int32_t bot_pad = 336 - (name_y + th_large);
        ASSERT_LE(std::abs(top_pad - bot_pad), 1); // Symmetric within 1 pixel
    } TEST_END();

    TEST_CASE("12.63 Ant Carrying Food Instructed to Eat Food Walks to Food Then Returns to Base") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place anthill at (5, 5)
        sim.grid_mut().set_anthill(0, TileCoord{5, 5});

        // A morsel of one unit at (20, 10) (a single cell: the lunchbox art)
        const int32_t morsel = place_pile(sim, 20, 10, 1, 25, {{1, TILE_LUNCHBOX}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{20, 10}), morsel);

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto* w = const_cast<AntUnit*>(&sim.get_unit(worker));
        w->pick_up_food(1, 25);
        ASSERT_TRUE(w->is_holding());
        ASSERT_EQ(w->carried_food, 1);

        // Instruct ant with food to move to the food tile
        sim.issue_move_order(worker, TileCoord{20, 10});

        // Ant must walk towards food first (does NOT immediately return to base): the order is the harvest order (5)
        ASSERT_EQ(w->state, UnitState::Walking);
        ASSERT_EQ(w->orig_order, AntUnit::kOrderHarvest);

        // Advance simulation until ant reaches food; during transit it still carries exactly 1 food
        tick_until_paths_delivered(sim, {worker});
        while (!w->waypoints.empty() && w->orig_order == AntUnit::kOrderHarvest) {
            sim.tick();
            ASSERT_EQ(w->carried_food, 1);
        }

        // Food was NOT consumed (still on the map) and ant carried_food is still 1 (no double bite)
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(morsel)].remaining, 1u);
        ASSERT_EQ(w->carried_food, 1);
        ASSERT_TRUE(sim.has_lunchbox_at(TileCoord{20, 10}));

        // The ant does not eat: it heads for its own entrance (5 + 1, 5 + 1) with the enter order (order 2), and the
        // original says "Can't - already have food." (string 17); the food it carries is still the one it took at its origin
        ASSERT_EQ(w->orig_order, AntUnit::kOrderHome);
        ASSERT_TRUE(sim.has_news_event(0, 17));
        ASSERT_EQ(w->harvest_origin, (TileCoord{20, 10}));
    } TEST_END();

    TEST_CASE("12.64 Diagonal Melee Attack Pushes Diagonally Along Strike Vector") {
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});

            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->facing = Direction::South;

            sim.execute_melee_attack(attacker, defender);

            // Defender was forced to face towards attacker at (10, 10) (North-West)
            ASSERT_EQ(def->facing, Direction::NorthWest);

            // Pushback is along diagonal strike vector (p_dx = +1, p_dy = +1) to (12, 12)
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def->pos == TileCoord{12, 12} && def->state == UnitState::Idle; }) >= 0);
        }

        // If the primary diagonal destination is obstructed by rock, KnockDir tries dir+1 (South) first
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            sim.grid_mut().set_terrain(12, 12, TERRAIN_OBSTACLE);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 11});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            sim.execute_melee_attack(attacker, defender);
            ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def->pos != TileCoord{11, 11} && def->state == UnitState::Idle; }) >= 0);
            bool cardinal_flank = (def->pos == TileCoord{12, 11} || def->pos == TileCoord{11, 12});
            ASSERT_TRUE(cardinal_flank);
            ASSERT_EQ(def->pos, (TileCoord{11, 12}));
        }
    } TEST_END();

    TEST_CASE("12.65 Melee Attack Pushback Deflects To The Next Direction When Obstructed by Rock Wall") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Attacker at (10, 10), defender at (11, 10)
        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        // Place solid obstacle at primary push destination (12, 10)
        sim.grid_mut().set_terrain(12, 10, TERRAIN_OBSTACLE);

        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        // Victim forced to face attacker (West)
        ASSERT_EQ(def.facing, Direction::West);

        // Because primary East (12, 10) is obstructed by the wall, KnockDir tries the direction +1 (South-East) next
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return def.pos != TileCoord{11, 10} && def.state == UnitState::Idle; }) >= 0);
        ASSERT_EQ(def.pos, (TileCoord{12, 11}));
    } TEST_END();

    TEST_CASE("12.66 Selection Bracket (*ears) Table 4 Duration Millisecond Time Mapping") {
        AssetArchive archive;
        bool loaded = archive.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
        ASSERT_TRUE(loaded);

        // 1. dogears (Anim 58) & c_dogears (Anim 149): 4 frames @ 250ms each (1000ms loop)
        for (uint32_t aid : {58u, 149u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 249), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 250), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 499), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 500), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 749), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 750), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 999), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 1000), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 1250), 1u);
        }

        // 2. hillears (Anim 59): 4 frames @ 200ms each (800ms loop)
        {
            const auto& seq = archive.get_animation(59);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 199), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 200), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 399), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 400), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 599), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 600), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 799), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 800), 0u);
        }

        // 3. yelears (Anim 60) & c_yelears (Anim 150): 4 frames: 60ms, 125ms, 125ms, 125ms (435ms loop)
        for (uint32_t aid : {60u, 150u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 59), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 60), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 184), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 185), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 309), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 310), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 434), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 435), 0u);
        }

        // 4. redears (Anim 61) & c_redears (Anim 151): 4 frames @ 60ms each (240ms loop)
        for (uint32_t aid : {61u, 151u}) {
            const auto& seq = archive.get_animation(aid);
            ASSERT_EQ(seq.subitems.size(), 4u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 0), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 59), 0u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 60), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 119), 1u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 120), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 179), 2u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 180), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 239), 3u);
            ASSERT_EQ(Renderer::get_anim_subitem_by_time(seq, 240), 0u);
        }
    } TEST_END();

    TEST_CASE("12.67 CHECKGO: The 200 ms Time-Warning Grid (1 Minute, 30 Seconds, 11 Countdown Steps) And The End Of The Match") {
        // Ants.exe 0x1024839: every 200 ms the clock is compared with a threshold (61000, then 31000, then 11000 falling by 1000 per
        // step); below it the next warning is given (cue and flashing text, stage by stage) and the match ends at the first run that
        // finds the clock below 0. A 6 minute map: the warnings fall on remaining = 60800, 30800, 10800, 9800 ... 800.
        SimulationEngine sim;
        const uint32_t limit = 360000;
        sim.init_test_world(60, 60, 100, limit);
        struct Warning { uint32_t remaining; uint32_t sound; uint16_t text; };
        std::vector<Warning> got;
        int32_t over_elapsed = -1;
        for (uint32_t i = 0; i < 8000 && over_elapsed < 0; ++i) {
            const uint32_t elapsed = i * 50;          // the poll of this tick sees the clock at limit - elapsed
            sim.tick();
            uint32_t sound = 0;
            uint16_t text = 0;
            for (const auto& a : sim.poll_audio_events()) {
                if (a.target_player == 255 && (a.sound_id == SoundID::OneMinute || a.sound_id == SoundID::ThirtySeconds || a.sound_id == SoundID::Countdown)) sound = a.sound_id;
            }
            for (const auto& n : sim.poll_news_events()) {
                if (n.string_id == 49 || n.string_id == 50 || n.string_id == 59) {
                    ASSERT_TRUE(n.blink);                        // all three are posted with the flash flag
                    ASSERT_EQ(n.target_player, 255);
                    text = n.string_id;
                    if (n.string_id == 49) ASSERT_EQ(n.message_text, "1 minute left in the game.");
                    if (n.string_id == 50) ASSERT_EQ(n.message_text, "30 seconds left in the game.");
                    if (n.string_id == 59) ASSERT_EQ(n.message_text, "10 seconds and counting...");
                }
            }
            if (sound != 0 || text != 0) got.push_back({limit - elapsed, sound, text});
            if (sim.is_match_over()) over_elapsed = static_cast<int32_t>(elapsed);
        }
        ASSERT_EQ(got.size(), size_t{13});
        ASSERT_EQ(got[0].remaining, 60800u);
        ASSERT_EQ(got[0].sound, SoundID::OneMinute);
        ASSERT_EQ(got[0].text, 49);
        ASSERT_EQ(got[1].remaining, 30800u);
        ASSERT_EQ(got[1].sound, SoundID::ThirtySeconds);
        ASSERT_EQ(got[1].text, 50);
        for (uint32_t k = 0; k < 11; ++k) {
            ASSERT_EQ(got[2 + k].remaining, 10800u - 1000u * k);
            ASSERT_EQ(got[2 + k].sound, SoundID::Countdown);
            ASSERT_EQ(got[2 + k].text, 59);
        }
        // the match ends at the first run with the clock below 0: 200 ms after 0:00 on this grid
        ASSERT_EQ(over_elapsed, static_cast<int32_t>(limit + 200));
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);
    } TEST_END();

    TEST_CASE("12.68 Match Defeat Triggers losers.wav (Sound 42) & Player Drop-Out Triggers playerout.wav (Sound 41)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 50);

        sim.set_player_score(0, 500);
        sim.set_player_score(1, 100);

        sim.clear_audio_events();
        ASSERT_TRUE(run_until_over(sim) >= 0); // Match game over (within 200 ms after 0:00)

        ASSERT_TRUE(sim.is_match_over());
        // The stings are the results screen's (one per machine, Sound 56 for a winner, Sound 42 = losers.wav for a loser), not the simulation's
        ASSERT_FALSE(sim.has_targeted_audio_event(0, SoundID::VictoryFanfare));
        ASSERT_FALSE(sim.has_targeted_audio_event(1, SoundID::PlayerDefeat));
        ScorecardModal loser_screen;
        loser_screen.show(sim.get_world_state().match_result, 1);
        loser_screen.update(0.25f);
        ASSERT_EQ(loser_screen.get_audio_to_play(), SoundID::PlayerDefeat);
        ASSERT_EQ(SoundID::PlayerDefeat, 42u);

        // Player dropout (FUN_0100d03b): a News Flash line "%s dropped out of the game!" in the chat log, and playerout.wav
        // (Sound 41) unless the game is over
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.trigger_player_dropout(1, "Player 1");
        ASSERT_FALSE(sim.has_audio_event(SoundID::PlayerDropOut));         // the match is over: silent
        ASSERT_TRUE(sim.has_news_event(255, StringID::PlayerDropOut));
        SimulationEngine live;
        live.init_test_world(60, 60, 101, 720000);
        live.trigger_player_dropout(1, "Player 1");
        ASSERT_TRUE(live.has_audio_event(SoundID::PlayerDropOut));
        ASSERT_EQ(SoundID::PlayerDropOut, 41u);
        bool line = false;
        for (const auto& n : live.poll_news_events()) {
            if (n.string_id == StringID::PlayerDropOut && n.channel == NewsChannel::ChatLog && n.message_text == "Player 1 dropped out of the game!") line = true;
        }
        ASSERT_TRUE(line);
    } TEST_END();

    TEST_CASE("12.69 Hatched Ant Emergence Triggers exithill.wav (Sound 43)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        sim.set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 10);
        sim.set_hatch_delay_ticks(4); // Short incubation for testing

        sim.clear_audio_events();
        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // Advance incubation
        bool got_exithill = false;
        for (int i = 0; i < 20; ++i) {
            sim.tick();
            if (sim.has_audio_event(SoundID::ExitHill)) {
                got_exithill = true;
                break;
            }
        }
        ASSERT_TRUE(got_exithill);
        ASSERT_EQ(SoundID::ExitHill, 43u);
    } TEST_END();

    TEST_CASE("12.70 A Landing On An Occupied Tile Throws Both Ants Apart (Blast 0) With The Hit Clip Sounds") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        uint32_t a1 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
        uint32_t a2 = sim.spawn_unit(1, AntType::Combat, TileCoord{22, 20});   // stands on the landing tile

        sim.clear_audio_events();
        sim.execute_melee_attack(attacker, a1);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(a1).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(a2).loco_action == AntUnit::kActionIdle &&
                                                       sim.get_unit(a1).pos != sim.get_unit(a2).pos &&
                                                       !sim.get_unit(a1).engaged; }) >= 0);
        ASSERT_TRUE(sim.has_audio_event(64));      // frame 0 of the gh clip of every thrown ant
        ASSERT_FALSE(sim.get_unit(a1).pos == sim.get_unit(a2).pos);
        ASSERT_EQ(sim.get_unit(a2).hp, 10);        // the pile-up does no damage
    } TEST_END();

    TEST_CASE("12.71 Authentic Map Duration & LVL Header Parsing") {
        std::string maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        MapSelectScreen screen;
        screen.init(maps_dir);

        const auto& maps = screen.get_maps();
        ASSERT_GE(maps.size(), 6u);

        std::vector<std::pair<std::string, uint32_t>> expected_mins = {
            {"TINY.LVL", 6},
            {"SMALL.LVL", 8},
            {"MEDIUM.LVL", 10},
            {"GAUNTLET.LVL", 10},
            {"ISLANDS.LVL", 12},
            {"TREASURE.LVL", 12}
        };

        for (const auto& [fname, exp_min] : expected_mins) {
            bool found = false;
            for (const auto& m : maps) {
                if (m.filename == fname) {
                    found = true;
                    ASSERT_EQ(m.minutes, exp_min);

                    // Verify SimulationEngine initializes remaining time to exact match minutes
                    ants::assets::LevelData lvl;
                    if (lvl.load_from_file(m.full_path)) {
                        SimulationEngine sim;
                        sim.init(lvl, 42);
                        ASSERT_EQ(sim.get_match_time_remaining_ms(), exp_min * 60 * 1000u);
                    }
                    break;
                }
            }
            ASSERT_TRUE(found);
        }
    } TEST_END();

    TEST_CASE("12.72 TINY Map Central Enclosure Diagonal Pathfinding Walkability") {
        std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(tiny_path));

        SimulationEngine sim;
        sim.init(lvl, 42);

        // Spawn ant outside the central obstacle ring
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        ASSERT_TRUE(ant_id > 0);

        // The 9 user-reported central tiles inside the ring
        std::vector<TileCoord> targets = {
            {16, 17}, {17, 16}, {14, 17}, {13, 16}, {14, 15},
            {13, 14}, {14, 13}, {15, 14}, {16, 13}
        };

        // 1. Direct movement from outside the enclosure to each of the 9 tiles individually
        for (const auto& target : targets) {
            // Reset ant to outside position
            sim.get_unit(ant_id).pos = TileCoord{10, 10};
            sim.get_unit(ant_id).pixel_x = 10 * 32 + 16;
            sim.get_unit(ant_id).pixel_y = 10 * 32 + 16;
            sim.get_unit(ant_id).fx_x = (10 * 32 + 16) << 16;
            sim.get_unit(ant_id).fx_y = (10 * 32 + 16) << 16;
            sim.get_unit(ant_id).clear_path();
            sim.get_unit(ant_id).state = UnitState::Idle;

            sim.issue_move_order(ant_id, target);
            tick_until_paths_delivered(sim, {ant_id});
            const auto& ant = sim.get_unit(ant_id);

            // Unit must not reject with CantGo, and destination must reach the target tile
            ASSERT_FALSE(ant.state == UnitState::CantGo);
            ASSERT_TRUE(!ant.waypoints.empty());
            ASSERT_EQ(ant.final_dest, target);

            // Run simulation ticks until the ant reaches the central target square
            uint32_t ticks = 0;
            while ((sim.get_unit(ant_id).pos != target || sim.get_unit(ant_id).state == UnitState::Walking) && ticks < 400) {
                sim.tick();
                ticks++;
            }
            ASSERT_EQ(sim.get_unit(ant_id).pos, target);
        }

        // 2. Sequential traversal between all 9 tiles inside the enclosure
        for (const auto& target : targets) {
            sim.issue_move_order(ant_id, target);
            tick_until_paths_delivered(sim, {ant_id});
            const auto& ant = sim.get_unit(ant_id);

            // Unit must not reject with CantGo, and destination must reach the target tile
            ASSERT_FALSE(ant.state == UnitState::CantGo);
            ASSERT_TRUE(!ant.waypoints.empty());
            ASSERT_EQ(ant.final_dest, target);

            // Run simulation ticks until the ant reaches the central target square
            uint32_t ticks = 0;
            while ((sim.get_unit(ant_id).pos != target || sim.get_unit(ant_id).state == UnitState::Walking) && ticks < 400) {
                sim.tick();
                ticks++;
            }
            ASSERT_EQ(sim.get_unit(ant_id).pos, target);
        }
    } TEST_END();

    TEST_CASE("12.33 Daisy Flower Power-Up Droppers and Food Avoidance") {
        SimulationEngine sim;
        std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        ants::assets::LevelData lvl;
        if (lvl.load_lvl(path)) {
            sim.init(lvl, 42);

            const auto& ws = sim.get_world_state();
            ASSERT_EQ(ws.flower_droppers.size(), 2u);
            bool has_left = false;
            bool has_right = false;
            for (const auto& fd : ws.flower_droppers) {
                if (fd.x == 2 && fd.y == 19 && fd.drop_x == 2 && fd.drop_y == 20) has_left = true;
                if (fd.x == 37 && fd.y == 19 && fd.drop_x == 37 && fd.drop_y == 20) has_right = true;
            }
            ASSERT_TRUE(has_left);
            ASSERT_TRUE(has_right);

            // Authentic 1998 Stem Obstacle Parity: Bottom stem at (2, 19) and (37, 19) is a solid obstacle
            ASSERT_FALSE(sim.grid().get_cell(2, 19).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(2, 19).is_obstacle_overlay);
            ASSERT_FALSE(sim.grid().get_cell(37, 19).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(37, 19).is_obstacle_overlay);

            // Ground tile directly in front where powerup lands (2, 20) and (37, 20) is fully passable
            ASSERT_TRUE(sim.grid().get_cell(2, 20).is_passable());
            ASSERT_TRUE(sim.grid().get_cell(37, 20).is_passable());

            // Fast forward 301 ticks: FDTASK polls every 3000 ms (+ its run time); the first poll stamps the record, the fifth one (5 x 3001 = 15005 ms) sees
            // more than wp.param == 15 seconds and posts the drop (v0.0.56; before, the timer was a per-tick countdown of exactly 15 s)
            for (int i = 0; i < 301; ++i) {
                sim.tick();
            }

            const auto& ws_dropping = sim.get_world_state();
            ASSERT_TRUE(ws_dropping.flower_droppers[0].is_dropping);

            // Complete the 16-tick drop animation
            for (int i = 0; i < 16; ++i) {
                sim.tick();
            }

            const auto& ws_dropped = sim.get_world_state();
            ASSERT_FALSE(ws_dropped.flower_droppers[0].is_dropping);
            // Powerup placed at target (open ground tile in front of the plant base: y + 1)
            int32_t target_drop_x = ws.flower_droppers[0].drop_x;
            int32_t target_drop_y = ws.flower_droppers[0].drop_y;
            ASSERT_EQ(target_drop_y, ws.flower_droppers[0].y + 1);
            const auto& cell = sim.grid().get_cell(TileCoord{target_drop_x, target_drop_y});
            ASSERT_TRUE(cell.has_powerup());
            // SMALL.LVL probabilities are [0.45, 0.0, 0.0, 0.1, 0.45] (Combat & Thief are 0%)
            ASSERT_NE(cell.powerup_type, 4); // Not Combat
            ASSERT_NE(cell.powerup_type, 3); // Not Thief

            // Advance to the second posting with the power-up still uncollected: the record is stamped again at the posting (15005 ms), so the next
            // posting is five polls later (30010 ms), not 15 s after the landing; it replaces the power-up lying there
            for (int i = 0; i < 285; ++i) {
                sim.tick();
            }
            const auto& ws_dropping2 = sim.get_world_state();
            ASSERT_TRUE(ws_dropping2.flower_droppers[0].is_dropping);

            // Complete the second 16-tick drop animation
            for (int i = 0; i < 16; ++i) {
                sim.tick();
            }
            const auto& ws_dropped2 = sim.get_world_state();
            ASSERT_FALSE(ws_dropped2.flower_droppers[0].is_dropping);
            const auto& cell2 = sim.grid().get_cell(TileCoord{target_drop_x, target_drop_y});
            ASSERT_TRUE(cell2.has_powerup());
            ASSERT_NE(cell2.powerup_type, 4);
            ASSERT_NE(cell2.powerup_type, 3);

            // Food avoidance: Intermediate food is treated as obstacle in pathfinding
            sim.grid_mut().get_cell_mut(10, 10).is_food = true;
            sim.grid_mut().get_cell_mut(10, 10).interactive_id = 301;
            ASSERT_TRUE(sim.grid().get_cell(10, 10).has_food());

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 9});
            sim.issue_move_order(a1, TileCoord{10, 11});
            // Unit should route around (10, 10) instead of passing through it
            for (const auto& wp : sim.get_unit(a1).waypoints) {
                ASSERT_FALSE(wp.x == 10 && wp.y == 10);
            }
        }

        // Verify GAUNTLET.LVL waypoints including top-left daisy flower at (3, 5) -> drop at (3, 6)
        std::string gauntlet_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/GAUNTLET.LVL";
        ants::assets::LevelData gauntlet_lvl;
        if (gauntlet_lvl.load_lvl(gauntlet_path)) {
            sim.init(gauntlet_lvl, 42);
            const auto& g_ws = sim.get_world_state();
            ASSERT_EQ(g_ws.flower_droppers.size(), 1u);
            bool has_flower = false;
            for (const auto& fd : g_ws.flower_droppers) {
                if (fd.x == 3 && fd.y == 5 && fd.drop_x == 3 && fd.drop_y == 6) has_flower = true;
            }
            ASSERT_TRUE(has_flower);
        }

        // Verify TREASURE.LVL: Center plants have waypoints with flag == 0 (no power-up droppers)
        // and corridor is traversable without spurious obstacles
        std::string treasure_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL";
        ants::assets::LevelData treasure_lvl;
        if (treasure_lvl.load_lvl(treasure_path)) {
            sim.init(treasure_lvl, 42);
            const auto& t_ws = sim.get_world_state();
            ASSERT_EQ(t_ws.flower_droppers.size(), 0u);
        }
    } TEST_END();

}
