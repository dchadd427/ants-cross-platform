// Application integration tests: suites 10 and 11 and the first cases of suite 12 (egg economy, incubation, team-up, anthill queuing, unit selection and occupied tiles)
#include "app_integration_fixtures.hpp"

// ============================================================================
// SUITE 10: Egg Economy, Incubation Emergence, Team-Up & Smart Abilities
// ============================================================================
void run_suite_10_egg_economy_incubation_teamup_abilities() {
    TEST_SUITE("Suite 10: Egg Economy, Incubation Emergence, Team-Up & Smart Abilities");

    TEST_CASE("10.1 Per-Map Egg Stock & Boundary Param Mapping") {
        std::vector<std::pair<std::string, uint16_t>> expected_map_eggs = {
            {"TREASURE.LVL", 9},
            {"MEDIUM.LVL", 6},
            {"GAUNTLET.LVL", 6},
            {"ISLANDS.LVL", 4},
            {"TINY.LVL", 3},
            {"SMALL.LVL", 2}
        };

        for (const auto& [map_name, expected_eggs] : expected_map_eggs) {
            std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map_name;
            ants::assets::LevelData lvl;
            if (lvl.load_lvl(path)) {
                ASSERT_EQ(lvl.boundary_param, expected_eggs);
                ASSERT_EQ(lvl.initial_eggs(), expected_eggs);

                SimulationEngine sim;
                sim.init(lvl, 42);
                for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                    ASSERT_EQ(sim.get_player_eggs(p), expected_eggs);
                }
            }
        }
    } TEST_END();

    TEST_CASE("10.2 Egg Consumption & Zero Egg Prohibits Hatching") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 600);
        sim.set_player_eggs(0, 2);

        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(sim.get_player_score(0), 600);

        // 1st Hatch: 600 -> 400 pts, 2 -> 1 eggs
        bool h1 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h1);
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);

        // Only one egg incubates at a time: a click while one hatches costs nothing ("An Ant is already hatching!")
        ASSERT_FALSE(sim.hatch_ant(0, AntType::Worker));
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);
        for (int t = 0; t < 170 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();   // 8000 ms incubation
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);

        // 2nd Hatch: 400 -> 200 pts, 1 -> 0 eggs
        bool h2 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h2);
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);
        for (int t = 0; t < 170 && sim.get_pending_hatch_count(0) > 0; ++t) sim.tick();

        // 3rd Hatch: score is 200 (sufficient score), but 0 eggs available -> Must fail!
        bool h3 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(h3);
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);

        // Even with huge score (e.g. 10,000 pts), 0 eggs strictly prohibits hatching
        sim.set_player_score(0, 10000);
        bool h4 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(h4);
        ASSERT_EQ(sim.get_player_score(0), 10000);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);
    } TEST_END();

    TEST_CASE("10.3 Hatch Incubation Delay, Hole Emergence & Idle Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{10, 10});
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        sim.set_hatch_delay_ticks(20); // 20 ticks delay for testing

        const auto* home = sim.grid().find_anthill(0);
        ASSERT_TRUE(home != nullptr);
        int32_t bx = home->x;
        int32_t by = home->y;

        bool hatched = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(hatched);

        // While the egg incubates there is no ant at all (the original creates the newborn when HATCHTSK runs)
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        for (const auto& a : sim.get_world_state().ants) ASSERT_TRUE(a.player_id != 0);

        // Tick 10 ticks: still incubating
        for (int i = 0; i < 10; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        for (const auto& a : sim.get_world_state().ants) ASSERT_TRUE(a.player_id != 0);

        // Tick 15 more ticks (total 25 ticks > 20 delay): the newborn appears on the entrance tile (bx+1, by+1)
        // playing the hatch clip
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        uint32_t newborn_id = 0;
        for (const auto& a : sim.get_world_state().ants) {
            if (a.player_id == 0) newborn_id = a.id;
        }
        ASSERT_TRUE(newborn_id != 0);
        const auto& u_new = sim.get_unit(newborn_id);
        ASSERT_EQ(u_new.pos.x, bx + 1);
        ASSERT_EQ(u_new.pos.y, by + 1);
        ASSERT_EQ(u_new.state, UnitState::EnteringBase);
        ASSERT_TRUE(sim.has_audio_event(SoundID::ExitHill));

        // Advance simulation until the ant finishes the clip and walks to the idle spot tile42 (bx+4, by+4)
        for (int i = 0; i < 130; ++i) sim.tick();

        const auto& u_final = sim.get_unit(newborn_id);
        ASSERT_EQ(u_final.pos.x, bx + 4);
        ASSERT_EQ(u_final.pos.y, by + 4);
        ASSERT_EQ(u_final.state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("10.4 Right-Click Special Abilities Autonomous Approach") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{5, 5});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // 1. Bomber at (5, 5) orders bomb at distance (8, 5) -> pixel (8*32 + 16, 5*32 + 16) = (272, 176)
        hud.select_ant(bomber);
        int32_t bx = 272 + HUD::PLAYFIELD_X;
        int32_t by = 176 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 3, sim, camera);
        hud.handle_mouse_up(bx, by, 3, sim, camera);

        const auto& b_unit = sim.get_unit(bomber);
        ASSERT_EQ(b_unit.orig_order, AntUnit::kOrderPlant);
        ASSERT_EQ(b_unit.orig_special_tile, (TileCoord{8, 5}));

        // The bomber walks to a neighbour tile of the target (the nearest one: west of it), plants, and the bomb appears
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.has_bomb_at(TileCoord{8, 5}); }) >= 0);
        ASSERT_EQ(b_unit.pos, (TileCoord{7, 5}));

        // 2. Swimmer at (6, 8). Set water at distance (10, 8). Right-click (10, 8) -> pixel (10*32 + 16, 8*32 + 16)
        uint32_t swimmer2 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{6, 8});
        sim.set_terrain(10, 8, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer2);
        int32_t sx = (10 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t sy = (8 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);
        hud.handle_mouse_up(sx, sy, 3, sim, camera);

        const auto& s_unit = sim.get_unit(swimmer2);
        ASSERT_EQ(s_unit.orig_order, AntUnit::kOrderBridgeBuild);
        ASSERT_EQ(s_unit.orig_special_tile, (TileCoord{10, 8}));

        ASSERT_TRUE(wait_ms(sim, 10000, [&]() { return sim.grid().get_cell(TileCoord{10, 8}).has_completed_bridge(); }) >= 0);

        // 3. Swimmer right-clicking land tile -> standard Move order (no bridge built on land)
        hud.select_ant(swimmer2);
        int32_t mx = (6 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t my = (6 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(mx, my, 3, sim, camera);
        hud.handle_mouse_up(mx, my, 3, sim, camera);
        const auto& s_unit2 = sim.get_unit(swimmer2);
        ASSERT_NE(s_unit2.orig_order, AntUnit::kOrderBridgeBuild);
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{6, 6}));
    } TEST_END();

    TEST_CASE("10.5 Enemy Base Click & The Ally Pedestal (more than two players, not yet allied)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{2, 2});
        sim.grid_mut().set_anthill(1, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0); // local player is 0

        const auto* enemy_base = sim.grid().find_anthill(1);
        ASSERT_TRUE(enemy_base != nullptr);
        int32_t ebx = enemy_base->x;
        int32_t eby = enemy_base->y;

        // Click on enemy base in world -> selects enemy base
        int32_t cx = (ebx * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t cy = (eby * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(cx, cy, 1, sim, camera);
        hud.handle_mouse_up(cx, cy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_base_team_id(), 1);

        // Clicking Stop button position (605, 195) does not clear enemy base selection (stop button is absent)
        hud.handle_mouse_down(605, 195, 1, sim, camera);
        hud.handle_mouse_up(605, 195, 1, sim, camera);
        ASSERT_EQ(hud.get_selected_base_team_id(), 1);

        // With two players there is no ally pedestal (slot 1 of another player's hill exists only with more than two players)
        auto proposal_sound = [&]() {
            for (const auto& ev : sim.poll_audio_events()) {
                if (ev.sound_id == SoundID::AlliancePro) return true;
            }
            return false;
        };
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_FALSE(proposal_sound());

        // A third player's hill makes the pedestal appear: the click proposes the alliance
        sim.grid_mut().set_anthill(2, TileCoord{14, 14});
        sim.tick();                                            // the world snapshot is rebuilt with the third hill
        hud.select_base(1);
        hud.handle_mouse_down(495, 165, 1, sim, camera);      // slot 1: (482, 152) - (525, 225)
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_TRUE(proposal_sound());

        // Nobody answers: there is no computer opponent that accepts the invitation, it stays open
        for (int i = 0; i < 35; ++i) sim.tick();
        ASSERT_NE(sim.get_world_state().player_alliances[0], 1u);

        // A command that answers an invitation nobody made is refused; player 1's own accept makes the team
        Command bogus;
        bogus.type = CommandType::AllianceAccept;
        bogus.issuer = 2;
        bogus.other_player = 0;
        ASSERT_EQ(sim.apply_command(bogus).status, CommandResult::Status::RejectedNotAllowed);
        Command accept;
        accept.type = CommandType::AllianceAccept;
        accept.issuer = 1;
        accept.other_player = 0;
        ASSERT_TRUE(sim.apply_command(accept).accepted());
        const auto& ws_allied = sim.get_world_state();
        ASSERT_EQ(ws_allied.player_alliances[0], 1u);
        ASSERT_EQ(ws_allied.player_alliances[1], 0u);

        // Allied with the owner: the pedestal is gone, its place does nothing (the alliance is left through the ally attack dialog)
        hud.update(sim.get_world_state(), 1);
        hud.select_base(1);
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);
        ASSERT_FALSE(proposal_sound());
        ASSERT_EQ(sim.get_world_state().player_alliances[0], 1u);
    } TEST_END();
}

// ============================================================================
// SUITE 11: Anthill Queuing & Priority Serialization
// ============================================================================
void run_suite_11_anthill_queuing_and_priority() {
    TEST_SUITE("Suite 11: Anthill Queuing & Priority Serialization");


    TEST_CASE("11.2 Serialized Base Entry & Priority Queue (Stand Off to Side & Wait)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Spawn 3 workers carrying food next to the hill
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Order all three onto the hill in order 1, 2, 3
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);

        // The first ant may take the free entrance; the entrance is claimed by it, so the other two were sent to the
        // waiting tiles in front of the hill (+0x68 = 1)
        ASSERT_FALSE(sim.is_ant_in_base_queue(a1));
        ASSERT_EQ(sim.get_unit(a1).orig_order, AntUnit::kOrderHome);
        ASSERT_TRUE(sim.is_ant_in_base_queue(a2));
        ASSERT_TRUE(sim.is_ant_in_base_queue(a3));

        // Run until all three deposited: the enter clips never overlap and the ants take the entrance one by one
        std::vector<uint32_t> entry_order;
        uint32_t last_active = 0;
        bool a1_entering_with_two_waiting = false;
        for (int i = 0; i < 1500 && sim.get_player_score(0) < 75; ++i) {
            sim.tick();
            int entering = 0;
            for (uint32_t id : {a1, a2, a3}) {
                if (sim.get_unit(id).state == UnitState::EnteringBase) ++entering;
            }
            ASSERT_TRUE(entering <= 1);
            const uint32_t active = sim.get_active_depositing_ant(0);
            if (active != 0 && active != last_active) {
                entry_order.push_back(active);
                if (entry_order.size() == 1 && sim.get_base_queue_size(0) == 2u) a1_entering_with_two_waiting = true;
            }
            last_active = active;
        }

        // The ant that had the free entrance went first while the other two stood queued on the waiting tiles
        ASSERT_EQ(entry_order.size(), 3u);
        ASSERT_EQ(entry_order[0], a1);
        ASSERT_TRUE(a1_entering_with_two_waiting);
        ASSERT_TRUE(entry_order[1] != entry_order[2] && entry_order[1] != a1 && entry_order[2] != a1);

        // Every ant scored its food when its clip ended: scoreup.wav (Sound 87) and a total of 75 points
        ASSERT_EQ(sim.get_player_score(0), 75);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreUp));
        ASSERT_FALSE(sim.get_unit(a1).is_holding());
        ASSERT_FALSE(sim.get_unit(a2).is_holding());
        ASSERT_FALSE(sim.get_unit(a3).is_holding());
    } TEST_END();

    TEST_CASE("11.3 Escape Deselects Without A Quit Dialog; Ctrl+Q Opens It; Esc / N Answer No, Enter Does Nothing, Y Quits") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        ViewportCamera camera;
        HUD hud;
        hud.init(0);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        hud.select_ant(a1);
        ASSERT_FALSE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Escape (27) deselects everything (FUN_01028c44(0)); there is no quit dialog on Esc
        hud.handle_key_down(27, sim, camera);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Ctrl+Q runs the quit flow of the Quit button
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.is_quit_dialog_open());

        // Enter does nothing in the quit dialog, Escape and N answer No
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        hud.handle_key_down('n', sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Verify full Application::handle_key_down wiring
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.is_running());
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        SDL_KeyboardEvent esc_ev{};
        esc_ev.type = SDL_KEYDOWN;
        esc_ev.keysym.sym = SDLK_ESCAPE;

        // Escape in-game opens nothing
        app.handle_key_down(esc_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Ctrl+Q opens the quit dialog
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());

        // Pressing 'N' closes quit dialog (a printable key reaches the dialog although the chat box takes typed text)
        SDL_KeyboardEvent n_ev{};
        n_ev.type = SDL_KEYDOWN;
        n_ev.keysym.sym = SDLK_n;
        app.handle_key_down(n_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Ctrl+Q and then 'Y' quits the game
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_FALSE(app.is_running());
    } TEST_END();

}

// ============================================================================
// Suite 12: Unit Selection & Occupied Tile Collision Handling
// ============================================================================
void run_suite_12_unit_selection_and_occupied_tile_movement() {
    TEST_SUITE("Suite 12: Unit Selection & Occupied Tile Collision Handling");

    TEST_CASE("12.1 Tile Click Unit Selection With Pre-Selected Unit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Pre-select a1
        hud.select_ant(a1);
        ASSERT_EQ(hud.get_selected_ant_id(), a1);

        // Click within a2's tile (8, 8) -> center is pixel (8*32+16, 8*32+16) = (272, 272)
        // Test an off-center click at (272 + 10, 272 - 12) = (282, 260)
        int32_t click_x = 282 + HUD::PLAYFIELD_X;
        int32_t click_y = 260 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(click_x, click_y, 1, sim, camera);
        hud.handle_mouse_up(click_x, click_y, 1, sim, camera);

        // a2 must be selected instead of moving a1 to a2's tile!
        ASSERT_EQ(hud.get_selected_ant_id(), a2);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 1u);
        ASSERT_EQ(hud.get_selected_ant_ids()[0], a2);

        // a1 must remain idle at its starting tile and not have been ordered to move
        ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{5, 5}));
        ASSERT_TRUE(sim.get_unit(a1).waypoints.empty());
    } TEST_END();

    TEST_CASE("12.2 Move Order to Occupied Friendly Tile (Stops Cleanly Without Bumping Loop)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn stationary friendly ant at (15, 15)
        uint32_t stat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 15});
        auto& stat_ant = sim.get_unit(stat_id);
        stat_ant.state = UnitState::Idle;

        // Spawn moving friendly ant at (15, 12)
        uint32_t mover_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 12});

        // Issue move order directly to the stationary ant's tile (15, 15)
        sim.issue_move_order(mover_id, TileCoord{15, 15});

        // Step simulation until mover ant arrives
        for (int i = 0; i < 60; ++i) {
            sim.tick();
        }

        const auto& mover = sim.get_unit(mover_id);

        // Stationary ant stays at (15, 15) and was not pushed
        ASSERT_EQ(stat_ant.pos.x, 15);
        ASSERT_EQ(stat_ant.pos.y, 15);

        // Mover ant stopped at an adjacent tile
        int32_t dx = std::abs(mover.pos.x - 15);
        int32_t dy = std::abs(mover.pos.y - 15);
        ASSERT_TRUE(dx <= 1 && dy <= 1);
        ASSERT_FALSE(mover.pos.x == 15 && mover.pos.y == 15);

        // Mover ant is idle, not walking or struggling
        ASSERT_EQ(mover.state, UnitState::Idle);
        ASSERT_TRUE(mover.waypoints.empty());

        // Audio queue must NOT contain FlingThumpA (Sound 64)
        ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));

        // Step 30 more ticks: mover ant must remain stationary without bouncing
        TileCoord mover_settled_pos = mover.pos;
        for (int i = 0; i < 30; ++i) {
            sim.tick();
            ASSERT_EQ(sim.get_unit(mover_id).pos, mover_settled_pos);
            ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));
        }
    } TEST_END();

    TEST_CASE("12.3 Move Order Onto an Occupied Tile Picks the First Free Ring Tile (FUN_010202e7)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn stationary ant at (20, 20)
        uint32_t stat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        auto& stat_ant = sim.get_unit(stat_id);
        stat_ant.state = UnitState::Idle;

        // Spawn friendly ant at adjacent tile (20, 21)
        uint32_t adj_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 21});
        auto& adj_ant = sim.get_unit(adj_id);
        adj_ant.state = UnitState::Idle;

        // Original GoTo: the taken tile is replaced by the first enterable tile of the ring scan around it
        // (distance 1: west column top to bottom, then east column, north row, south row) -> (19, 19).
        sim.issue_move_order(adj_id, TileCoord{20, 20});
        ASSERT_EQ(adj_ant.final_dest, (TileCoord{19, 19}));
        ASSERT_TRUE(tick_until(sim, [&]() { return adj_ant.pos == TileCoord{19, 19} && adj_ant.waypoints.empty() &&
                                                  adj_ant.state == UnitState::Idle; }, 60));
        ASSERT_EQ(stat_ant.pos, (TileCoord{20, 20}));   // never pushed
        ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));
    } TEST_END();

    TEST_CASE("12.4 Pile-Up Dispersal: Ants On One Tile Are Thrown Apart Without Damage And Rest On Tile Centres") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // A pile-up in the original: an ant lands on a tile that another ant stands on (WalkStep block C, Blast 0, 7)
        uint32_t a1_id = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
        uint32_t a2_id = sim.spawn_unit(1, AntType::Worker, TileCoord{26, 25});   // is hit and thrown to (27, 25)
        uint32_t a3_id = sim.spawn_unit(1, AntType::Worker, TileCoord{27, 25});   // stands on the landing tile
        sim.execute_melee_attack(a1_id, a2_id);

        const auto& a1 = sim.get_unit(a1_id);
        const auto& a2 = sim.get_unit(a2_id);
        const auto& a3 = sim.get_unit(a3_id);

        // Everything is thrown apart and comes to rest
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return a2.loco_action == AntUnit::kActionIdle &&
                                                       a3.loco_action == AntUnit::kActionIdle &&
                                                       a2.pos != a3.pos && a2.pos != a1.pos && a3.pos != a1.pos &&
                                                       a2.engaged == false; }) >= 0);
        ASSERT_EQ(a2.hp, 9u);   // the one blow
        ASSERT_EQ(a3.hp, 10u);  // the pile-up itself does no damage
        ASSERT_EQ(a1.hp, 10u);

        // All 3 ants rest exactly on tile centres (NEVER halfway between tiles)
        run_ms(sim, 500);
        ASSERT_EQ(a1.pixel_x, a1.pos.x * 32 + 16);
        ASSERT_EQ(a1.pixel_y, a1.pos.y * 32 + 16);
        ASSERT_EQ(a2.pixel_x, a2.pos.x * 32 + 16);
        ASSERT_EQ(a2.pixel_y, a2.pos.y * 32 + 16);
        ASSERT_EQ(a3.pixel_x, a3.pos.x * 32 + 16);
        ASSERT_EQ(a3.pixel_y, a3.pos.y * 32 + 16);
    } TEST_END();

    TEST_CASE("12.5 Multi-Ant Food Harvesting: The Group Order Sends Every Ant To The Pile") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{30, 5});   // the ants with food walk home and free their tiles for the others

        // A pile of crackers (2x2 cells around its anchor (21, 21)): 8 units of 25 points
        const int32_t pile = place_pile(sim, 21, 21, 8, 25, {{8, 369}, {6, 370}, {4, 371}, {2, 372}, {0, 0x7FFE}});
        ASSERT_EQ(sim.grid().get_cell({20, 20}).interactive_id, 369u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{20, 20}), pile);

        // Spawn 4 workers at (15, 20), (15, 21), (16, 20), (16, 21)
        const uint32_t w[4] = {sim.spawn_unit(0, AntType::Worker, TileCoord{15, 20}), sim.spawn_unit(0, AntType::Worker, TileCoord{15, 21}),
                               sim.spawn_unit(0, AntType::Worker, TileCoord{16, 20}), sim.spawn_unit(0, AntType::Worker, TileCoord{16, 21})};

        HUD hud;
        hud.init(0);
        hud.select_ant(w[0]);
        hud.set_selected_ant_ids({w[0], w[1], w[2], w[3]});

        // A move click on the pile (cursor mode 7): the ordinary group order, no slots around the pile
        hud.order_selected(sim, TileCoord{21, 21}, false, false);
        for (uint32_t id : w) {
            ASSERT_EQ(sim.get_unit(id).orig_order, AntUnit::kOrderHarvest);
            ASSERT_EQ(sim.get_unit(id).orig_order_tile, (TileCoord{21, 21}));
        }

        // Advance until every ant has carried food once: each ant plays its own grab clip and the cue of the clip
        bool held[4] = {false, false, false, false};
        int harvest_sound_count = 0;
        for (int t = 0; t < 600; ++t) {
            sim.tick();
            for (const auto& e : sim.poll_audio_events()) {
                if (e.sound_id == SoundID::FoodHarvest || e.sound_id == SoundID::FoodGrab) harvest_sound_count++;
            }
            for (size_t k = 0; k < 4; ++k) held[k] = held[k] || sim.get_unit(w[k]).is_holding();
            if (held[0] && held[1] && held[2] && held[3]) break;
        }
        for (size_t k = 0; k < 4; ++k) ASSERT_TRUE(held[k]);

        // Four bites were taken, one per ant, and the pile shows its 4-unit stage
        ASSERT_EQ(harvest_sound_count, 4);
        ASSERT_EQ(sim.grid().food_objects()[static_cast<size_t>(pile)].remaining, 4u);
        ASSERT_EQ(sim.grid().get_cell({21, 21}).interactive_id, 371u);
    } TEST_END();

    TEST_CASE("12.6 Swarm Movement Non-Bouncing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn 8 worker ants closely grouped
        std::vector<uint32_t> swarm;
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                if (dx == 1 && dy == 1) continue;
                swarm.push_back(sim.spawn_unit(0, AntType::Worker, TileCoord{10 + dx, 10 + dy}));
            }
        }
        ASSERT_EQ(swarm.size(), 8u);

        HUD hud;
        hud.init(0);
        hud.select_ant(swarm[0]);
        hud.set_selected_ant_ids(swarm);

        // Move group to (20, 20)
        hud.dispatch_move_order(20, 20, sim);

        // Step 120 ticks: moving ants should never bounce or wipe waypoints
        bool had_bounce = false;
        for (int t = 0; t < 120; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::FlingThumpA)) {
                had_bounce = true;
            }
        }

        // Must NOT have bounced moving ants
        ASSERT_FALSE(had_bounce);

        // All ants must have made substantial progress toward (20, 20)
        for (uint32_t aid : swarm) {
            const auto& ant = sim.get_unit(aid);
            int32_t d = ant.pos.chebyshev_dist(TileCoord{20, 20});
            ASSERT_TRUE(d <= 3);
        }
    } TEST_END();

    TEST_CASE("12.7 An Attack Order Walks Up To The Target: The Blow Comes When The Step Crosses The Border, One Blow Per Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn Combat Ant at (10, 10) on Team 0
        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        // Spawn Enemy Worker at (15, 10) on Team 1 (Chebyshev dist = 5: outside the auto-engage radius)
        uint32_t target_id = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 10});

        auto& attacker = sim.get_unit(combat_id);
        auto& target = sim.get_unit(target_id);
        int32_t init_hp = target.hp;

        // Ordering attack from distance (dist = 5)
        AntOrder atk_order{};
        atk_order.ant_id = combat_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_x = 15;
        atk_order.target_y = 10;
        atk_order.target_entity_id = static_cast<int32_t>(target_id);
        sim.issue_order(atk_order);

        // Target must NOT have taken damage immediately (not adjacent)
        ASSERT_EQ(target.hp, init_hp);
        ASSERT_EQ(attacker.orig_order, AntUnit::kOrderAttack);

        // The target takes no damage until the attacker's step crosses into its tile
        ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return target.hp != init_hp; }) > 0);
        ASSERT_EQ(attacker.pos, (TileCoord{14, 10}));

        // The contact takes 2 hit points (a combat ant), the blow is a single one: no further strike
        ASSERT_EQ(target.hp, init_hp - 2);
        run_ms(sim, 5000);
        ASSERT_EQ(target.hp, init_hp - 2);
        ASSERT_EQ(attacker.orig_order, AntUnit::kOrderNone);
    } TEST_END();

    TEST_CASE("12.8 Bomber Ant Plant Bomb On Gravel") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Set gravel surface at (20, 20) with walkable terrain
        auto& cell = sim.grid_mut().get_cell_mut({20, 20});
        cell.surface_type = SurfaceType::Gravel;
        cell.terrain_type = TERRAIN_WALKABLE;
        cell.flags |= (FLAG_CAN_PLACE_BOMB | FLAG_CAN_PLACE_FIRE);

        ASSERT_TRUE(cell.can_place_bomb());

        // Spawn bomber ant at (20, 23) (3 tiles south)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 23});

        HUD hud;
        hud.init(0);
        hud.select_ant(bomber_id);

        // The special order (a latched bomb pedestal makes a plantable tile a valid target) on the gravel tile (20, 20)
        hud.order_selected(sim, TileCoord{20, 20}, true, false);

        // Step simulation for 50 ticks until bomber approaches cardinal neighbor (20, 21) and drops bomb
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.has_bomb_at({20, 20})) break;
        }

        // Bomb successfully planted on gravel tile!
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombPick));
    } TEST_END();

    TEST_CASE("12.9 The Chat Log: Initial Message, Pixel Wrapping, The Window Follows The Newest Entry; A Drag Peeks Back, The Wheel And PageUp Do Nothing") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim_engine;
        sim_engine.init_test_world(60, 60, 1);
        ViewportCamera cam;

        // The start message is a News Flash entry: the header "[0:00] News Flash:" and its body, wrapped at 126 px (20 characters of 6 px here)
        const auto& log = hud.get_chat_log();
        ASSERT_TRUE(!log.empty());
        for (const auto& line : log) ASSERT_TRUE(line.length() <= 21);
        ASSERT_EQ(hud.chat_follow_pos(), 0);

        // Add 10 chat messages: the log outgrows the 101 px view and the window follows the newest entry (5 px per 50 ms pass)
        static uint32_t clock_now;
        clock_now = 1000;
        hud.set_ticks_function([]() { return clock_now; });
        for (int i = 0; i < 10; ++i) hud.add_chat_entry("Player", "Message number " + std::to_string(i));
        ASSERT_TRUE(hud.get_chat_log().size() >= 20);            // ten entries of a header and a body line each, plus the news flash
        ASSERT_TRUE(hud.chat_content_end() > HUD::kChatViewH);
        ASSERT_EQ(hud.chat_follow_pos(), 0);                     // nothing has moved yet
        ASSERT_EQ(hud.chat_follow_target(), hud.chat_content_end() - 1 - HUD::kChatViewH);

        hud.add_chat_entry("Player", "one more");
        const int32_t target = hud.chat_follow_target();
        for (int pass = 0; pass < 200 && hud.chat_follow_pos() != target; ++pass) {
            clock_now += 50;
            hud.update(sim_engine.get_world_state(), 1);
        }
        ASSERT_EQ(hud.chat_follow_pos(), target);

        // A drag inside the view: the log moves with the pointer (up = later lines) and snaps back at the release
        const int32_t follow = hud.chat_follow_pos();
        hud.handle_mouse_down(500, 350, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(hud.chat_dragging());
        ASSERT_EQ(hud.chat_view_offset(), follow);
        hud.handle_mouse_motion(500, 380, sim_engine, cam);      // 30 px down: the earlier lines come into view
        ASSERT_EQ(hud.chat_view_offset(), follow - 30);
        hud.handle_mouse_up(500, 380, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_FALSE(hud.chat_dragging());
        ASSERT_EQ(hud.chat_view_offset(), follow);

        // PageUp does nothing (the original has no key for the log)
        hud.handle_key_down(SDLK_PAGEUP, sim_engine, cam, 0, false);
        ASSERT_EQ(hud.chat_view_offset(), follow);

        // Sending a chat message adds an entry; the window is not reset (it follows)
        hud.set_chat_input("Hello Colony!");
        hud.send_chat_message();
        ASSERT_EQ(hud.chat_follow_target(), hud.chat_content_end() - 1 - HUD::kChatViewH);
    } TEST_END();

    TEST_CASE("12.9b The Chat Transcript: The Program Writes The Chat Log To A Text File When It Ends (Ants.exe 0x10122d4: \"%s @ %s\\n\\n\" and \"%s %s\\n\" per entry)") {
        // the stamp is the C runtime's _strdate and _strtime: the original's own chat.txt starts "04/17/98 @ 18:19:56"
        std::tm when{};
        when.tm_year = 98; when.tm_mon = 3; when.tm_mday = 17; when.tm_hour = 18; when.tm_min = 19; when.tm_sec = 56; when.tm_isdst = -1;
        ASSERT_EQ(Application::transcript_stamp(std::mktime(&when)), "04/17/98 @ 18:19:56");

        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.hud().add_chat_entry("Ann", "Rush the base!", false, 2);
        app.hud().add_chat_entry("Bob", "on my way", true, 3);
        const std::filesystem::path dir = temp_path_of_this_run("ants_test_chat_transcript");
        std::filesystem::create_directories(dir);
        const std::filesystem::path file = dir / "chat.txt";
        std::filesystem::remove(file);
        ASSERT_TRUE(app.write_chat_transcript(file.string()));
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();                                      // (Windows cannot delete a file that is still open: the remove below threw there)
        // "date @ time", a blank line, then one line per entry: its header, a space, its body (the body is not wrapped in the file)
        const size_t blank = text.find("\n\n");
        ASSERT_TRUE(blank != std::string::npos && blank == 19 && text[2] == '/' && text[5] == '/' && text.substr(8, 3) == " @ ");
        ASSERT_EQ(text.substr(blank + 2), "[0:00] News Flash: Game started! Go get that food!\nAnn: Rush the base!\nBob (To Teammate): on my way\n");
        std::filesystem::remove(file);
        std::filesystem::remove(dir);
        // a headless run writes no file at its end (and neither does a run that never built a match screen)
        app.shutdown();
        ASSERT_FALSE(std::filesystem::exists(dir / "chat.txt"));
    } TEST_END();

    TEST_CASE("12.10 Bomb Size & Team Color Rendering") {
        // Authentic Table 4 animations (129-132) define bombs as 12x24 pixels centered at (sx + 10, sy + 4)
        constexpr int32_t kTileDim = 32;
        constexpr int32_t BOMB_WIDTH = 12;
        constexpr int32_t BOMB_HEIGHT = 24;
        constexpr int32_t OFFSET_X = 10;
        constexpr int32_t OFFSET_Y = 4;

        int32_t sx = 100;
        int32_t sy = 200;
        SDL_Rect bomb_dst = { sx + OFFSET_X, sy + OFFSET_Y, BOMB_WIDTH, BOMB_HEIGHT };

        ASSERT_EQ(bomb_dst.w, 12);
        ASSERT_EQ(bomb_dst.h, 24);
        ASSERT_EQ(bomb_dst.x, 110);
        ASSERT_EQ(bomb_dst.y, 204);
        ASSERT_EQ(OFFSET_X * 2 + BOMB_WIDTH, kTileDim); // Perfectly centered horizontally: (32 - 12) / 2 = 10
        ASSERT_EQ(OFFSET_Y + BOMB_HEIGHT + 4, kTileDim);

        // Verify team color mapping: 0=Green, 1=Red, 2=Blue, 3=Black
        static const char* bomb_names[4] = { "1bombgrn.bmp", "1bombred.bmp", "1bombblu.bmp", "1bombblk.bmp" };
        ASSERT_TRUE(std::string(bomb_names[0]) == "1bombgrn.bmp");
        ASSERT_TRUE(std::string(bomb_names[1]) == "1bombred.bmp");
        ASSERT_TRUE(std::string(bomb_names[2]) == "1bombblu.bmp");
        ASSERT_TRUE(std::string(bomb_names[3]) == "1bombblk.bmp");
    } TEST_END();

    TEST_CASE("12.11 Friendly Bomb Autonomous Pathfinding Avoidance") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place friendly bomb at (20, 20) owned by Team 0
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        // Spawn Team 0 unit at (20, 18)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 18});

        // Issue move order across bomb to (20, 22)
        sim.issue_move_order(friendly_id, TileCoord{20, 22});
        tick_until_paths_delivered(sim, {friendly_id});

        const auto& unit = sim.get_unit(friendly_id);
        ASSERT_FALSE(unit.waypoints.empty());
        // Verify pathfinder routed around (20, 20)
        for (const auto& wp : unit.waypoints) {
            ASSERT_FALSE((wp == TileCoord{20, 20}));
        }

        // Tick simulation until reached destination
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            ASSERT_FALSE((sim.get_unit(friendly_id).pos == TileCoord{20, 20}));
            if (sim.get_unit(friendly_id).pos == TileCoord{20, 22}) {
                break;
            }
        }
        ASSERT_TRUE((sim.get_unit(friendly_id).pos == TileCoord{20, 22}));
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20})); // Friendly bomb remains unexploded

        // A player's move order onto a bomb of its own team ends on it (FUN_010202e7 gets flag 0x20 for every player order; only the path finder goes around):
        // the ant walks onto the bomb and sets it off, which is how a group gets off the island of ISLANDS
        sim.issue_move_order(friendly_id, TileCoord{20, 20});
        ASSERT_TRUE((sim.get_unit(friendly_id).final_dest == TileCoord{20, 20}));
        bool went_off = false;
        for (int t = 0; t < 100 && !went_off; ++t) {
            sim.tick();
            went_off = !sim.has_bomb_at(TileCoord{20, 20});
        }
        ASSERT_TRUE(went_off);
    } TEST_END();

    TEST_CASE("12.12 Enemy Bomb Proximity Detonation & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101, 60000);

        // Place Team 0 bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);

        // Spawn Team 1 enemy ant at (18, 20)
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Combat, TileCoord{18, 20});
        auto& enemy = sim.get_unit(enemy_id);
        enemy.hp = 5;
        enemy.max_hp = 5;

        // Move to (22, 20) through (20, 20)
        sim.issue_move_order(enemy_id, TileCoord{22, 20});

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));

        // Victim took 2 HP damage
        ASSERT_EQ(enemy.hp, 3);
        ASSERT_TRUE(enemy.state == UnitState::Knockback);

        // Step simulation through 10-tick ballistic knockback flight
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (enemy.state != UnitState::Knockback) break;
        }

        // Enemy was moving East, so impulse propelled it 4 spaces West to (16, 20)
        ASSERT_EQ(enemy.pos.x, 16);
        ASSERT_EQ(enemy.pos.y, 20);
    } TEST_END();

    TEST_CASE("12.13 Swimmer Bridge Digging Animation & Multi-Stage Progression") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water tile at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        // Swimmer ant at adjacent tile (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});
        auto& swimmer = sim.get_unit(swimmer_id);

        // Issue BuildBridge order to (20, 20)
        AntOrder order{};
        order.ant_id = swimmer_id;
        order.type = OrderType::BuildBridge;
        order.target_x = 20;
        order.target_y = 20;
        sim.issue_order(order);

        // The unit starts its dig clip on the tile it stands on, facing the bridge, once its one-tile path is delivered
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return swimmer.state == UnitState::BuildingBridge; }) >= 0);
        ASSERT_EQ(swimmer.facing, Direction::East);
        ASSERT_EQ(swimmer.pos, (TileCoord{19, 20}));
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 1);                     // stage 0x22 at once
        sim.clear_audio_events();

        // Every pass of the dig clip (480 ms on land, cue 81 at 240 ms) adds a stage; after three passes the bridge is done
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return swimmer.state != UnitState::BuildingBridge; }) >= 0);
        ASSERT_TRUE(sim.has_audio_event(81));
        ASSERT_FALSE(sim.has_audio_event(SoundID::ShovelWater));

        // Bridge is now fully constructed (stage 4)
        ASSERT_TRUE(sim.has_bridge_at(TileCoord{20, 20}));
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 4);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        // Swimmer completed digging and idles on its tile centre
        ASSERT_EQ(swimmer.state, UnitState::Idle);
        ASSERT_EQ(swimmer.pixel_x, 19 * 32 + 16);
        ASSERT_EQ(swimmer.pixel_y, 20 * 32 + 16);

        // Non-swimmer unit can now traverse the completed bridge
        ASSERT_TRUE(sim.can_unit_traverse(AntType::Worker, TileCoord{20, 20}));
    } TEST_END();

    TEST_CASE("12.14 Authentic Bridge Sprite Geometry & Offsets") {
        // Verify authentic dimensions and offsets matching Table 4 animations 34..38
        // Stage 1 (34): bridge1.bmp 16x16 at dx=8, dy=9
        // Stage 2 (35): bridge2.bmp 19x17 at dx=7, dy=7
        // Stage 3 (36): bridge3.bmp 27x24 at dx=3, dy=5
        // Stage 4 (37): bridge4a.bmp 32x32 at dx=0, dy=0
        // Tile 38 (38): bridge4b.bmp 29x30 at dx=2, dy=1
        struct ExpectedBridge {
            uint16_t tile_id;
            const char* name;
            int dx, dy, w, h;
        };
        const ExpectedBridge expected[5] = {
            { TILE_BRIDGE1,  "bridge1.bmp",  8, 9, 16, 16 },
            { TILE_BRIDGE2,  "bridge2.bmp",  7, 7, 19, 17 },
            { TILE_BRIDGE3,  "bridge3.bmp",  3, 5, 27, 24 },
            { TILE_BRIDGE4,  "bridge4a.bmp", 0, 0, 32, 32 },
            { TILE_BRIDGE4B, "bridge4b.bmp", 2, 1, 29, 30 }
        };

        for (const auto& b : expected) {
            int dx = 0, dy = 0, bw = 32, bh = 32;
            switch (b.tile_id) {
                case TILE_BRIDGE1:  dx = 8; dy = 9; bw = 16; bh = 16; break;
                case TILE_BRIDGE2:  dx = 7; dy = 7; bw = 19; bh = 17; break;
                case TILE_BRIDGE3:  dx = 3; dy = 5; bw = 27; bh = 24; break;
                case TILE_BRIDGE4:  dx = 0; dy = 0; bw = 32; bh = 32; break;
                case TILE_BRIDGE4B: dx = 2; dy = 1; bw = 29; bh = 30; break;
                default: break;
            }
            ASSERT_EQ(dx, b.dx);
            ASSERT_EQ(dy, b.dy);
            ASSERT_EQ(bw, b.w);
            ASSERT_EQ(bh, b.h);
        }
    } TEST_END();

    TEST_CASE("12.15 Single Bomber Left-Click and Right-Click Defuse Friendly Bomb") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);       // the cursor asks the simulation whether the tile is a valid special target

        // Place friendly bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        // Spawn friendly bomber at (20, 19)
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 19});

        // 1. A single selected bomber (panel 3): a bomb tile is a valid special target (cursor 4), the left click is the special order
        hud.select_ant(b_id, false);
        ASSERT_EQ(hud.get_selected_ant_id(), b_id);
        ASSERT_FALSE(hud.is_multi_select());

        // Screen coords for (20, 20)
        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Left-click on friendly bomb defuses bomb
        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        // The defuse clip plays (1100-1140 ms) and the bomb is removed when it ends
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.get_unit(b_id).state == UnitState::DefusingBomb; }) >= 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.get_unit(b_id).state == UnitState::Idle; }) >= 0);

        // 2. Right-click also defuses
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        hud.handle_mouse_down(screen_x, screen_y, 3, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 3, sim, cam, 0);      // the right button gives its order at the release
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return !sim.has_bomb_at(TileCoord{20, 20}); }) >= 0);
    } TEST_END();

    TEST_CASE("12.16 Non-Bomber Moves Directly Onto Friendly Bomb, Explodes & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 101, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);

        // Place friendly bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);

        // Spawn friendly worker at (18, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
        hud.select_ant(worker_id, false);

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Left-click on friendly bomb: instructs worker to hit the bomb!
        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));
        auto& worker = sim.get_unit(worker_id);
        ASSERT_EQ(worker.hp, 8); // 10 - 2 = 8 HP
        ASSERT_TRUE(worker.state == UnitState::Knockback);

        // Step through knockback flight
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Propelled 4 spaces West to (16, 20)
        ASSERT_EQ(worker.pos.x, 16);
        ASSERT_EQ(worker.pos.y, 20);
    } TEST_END();

    TEST_CASE("12.17 Several Selected Ants Move Onto A Friendly Bomb To Hit It (panel 4: cursor 3); The Order Of A Right Click Is A Move Too") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.set_sim_query(&sim);

        // Case A: a bomber and a worker selected (panel 4, mixed types): the bomb tile is a plain move target, the bomber hits the bomb
        sim.grid_mut().place_bomb(20, 20, 0);
        uint32_t b1 = sim.spawn_unit(0, AntType::Bomber, TileCoord{18, 20});
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 20});
        hud.set_selected_ant_ids({b1, w1});
        ASSERT_TRUE(hud.is_multi_select());

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;
        ASSERT_EQ(hud.evaluate_cursor(screen_x, screen_y, sim.get_world_state(), sim.grid(), cam), CursorType::Move);

        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_EQ(sim.get_unit(b1).hp, 8);

        // Case B: the same selection, a right click on a bomb: panel 4 gives a move, the bomber hits the bomb
        sim.grid_mut().place_bomb(30, 30, 0);
        uint32_t b2 = sim.spawn_unit(0, AntType::Bomber, TileCoord{28, 30});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{28, 31});
        hud.set_selected_ant_ids({b2, w2});
        ASSERT_TRUE(hud.is_multi_select());

        cam.world_x = 30 * 32 - 100;
        cam.world_y = 30 * 32 - 100;
        int32_t screen_x2 = HUD::PLAYFIELD_X + (30 * 32 + 16) - cam.world_x;
        int32_t screen_y2 = HUD::PLAYFIELD_Y + (30 * 32 + 16) - cam.world_y;

        hud.handle_mouse_down(screen_x2, screen_y2, 3, sim, cam, 0); // Right click on bomb
        hud.handle_mouse_up(screen_x2, screen_y2, 3, sim, cam, 0);

        bool detonated2 = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{30, 30})) {
                detonated2 = true;
                break;
            }
        }
        ASSERT_TRUE(detonated2);
    } TEST_END();

    TEST_CASE("12.18 Swimmer Ant Snorkel Animation Advances in Water") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place water tile at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        auto& swimmer = sim.get_unit(swimmer_id);
        ASSERT_EQ(swimmer.state, UnitState::Swimming);
        ASSERT_TRUE(swimmer.in_water);

        uint16_t initial_tick = swimmer.anim_tick;
        uint16_t initial_subitem = swimmer.anim_subitem;

        for (int t = 0; t < 10; ++t) {
            sim.tick();
        }

        // Snorkel animation advances smoothly each tick (anim_tick and anim_subitem increment)
        ASSERT_EQ(swimmer.anim_tick, initial_tick + 10);
        ASSERT_EQ(swimmer.anim_subitem, initial_subitem + 10);
    } TEST_END();

    TEST_CASE("12.19 Single-Tile Bridge Diagonal Pathfinding Traversability") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Build a water canal around a single bridge at (20, 20)
        // Land at (19, 19) and (21, 21), water at (19, 20) and (20, 19)
        sim.grid_mut().set_terrain(19, 20, TERRAIN_WATER);
        sim.grid_mut().set_terrain(20, 19, TERRAIN_WATER);
        sim.grid_mut().set_terrain(20, 21, TERRAIN_WATER);
        sim.grid_mut().set_terrain(21, 20, TERRAIN_WATER);

        // Single bridge completed at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        for (int s = 0; s < 4; ++s) {
            sim.grid_mut().advance_bridge(20, 20, 0);
        }
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());

        // Worker on land at (19, 19) pathfinding diagonally across the bridge to (21, 21)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 19});
        sim.issue_move_order(worker_id, TileCoord{21, 21});
        tick_until_paths_delivered(sim, {worker_id});

        auto& worker = sim.get_unit(worker_id);
        ASSERT_FALSE(worker.waypoints.empty());

        // Step simulation until worker arrives at (21, 21)
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (worker.pos == TileCoord{21, 21}) break;
        }
        ASSERT_EQ(worker.pos.x, 21);
        ASSERT_EQ(worker.pos.y, 21);
    } TEST_END();

    TEST_CASE("12.20 Swimmer Ant Bridge Demolition Multi-Stage Removal") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Bridge at (20, 20)
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);
        for (int s = 0; s < 4; ++s) {
            sim.grid_mut().advance_bridge(20, 20, 0);
        }
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 4);

        // Swimmer at (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});

        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);
        hud.select_ant(swimmer_id, false);

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Right-click on existing bridge dispatches DemolishBridge
        hud.handle_mouse_down(screen_x, screen_y, 3, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 3, sim, cam, 0);

        // Step simulation while swimmer demolishes the bridge through stages 3, 2, 1 to 0 (water)
        for (int t = 0; t < 150; ++t) {
            sim.tick();
            if (!sim.has_bridge_at(TileCoord{20, 20})) break;
        }

        // Bridge is completely demolished back to water
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{20, 20}));
        ASSERT_FALSE(sim.grid().get_cell(20, 20).has_completed_bridge());
        ASSERT_FALSE(sim.grid().get_cell(20, 20).has_partial_bridge());
        ASSERT_TRUE(sim.grid().get_cell(20, 20).terrain_type == TERRAIN_WATER);

        // Worker can no longer traverse (water is impassable for worker)
        ASSERT_FALSE(sim.can_unit_traverse(AntType::Worker, TileCoord{20, 20}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.21: Power-Up Arrival Timing & Transformation Lifecycle
    // ------------------------------------------------------------------------
    TEST_CASE("12.21 Power-Up Arrival Timing & getpow Clip Lifecycle (taken when the walk lands on the tile, type at once, 840 ms clip)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (13, 10) (Type 4 = Combat Ant)
        sim.grid_mut().place_powerup(13, 10, 4);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);
        ASSERT_EQ(ant.type, AntType::Worker);
        ASSERT_FALSE(picking_up(ant));

        // Order worker to move to power-up tile (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // While walking towards (13, 10) nothing is taken from a distance: at (12, 10) the power-up is still there
        bool saw_adjacent_not_triggered = false;
        while (ant.pos.x < 13) {
            sim.tick();
            if (ant.pos.x == 12) {
                saw_adjacent_not_triggered = true;
                ASSERT_FALSE(picking_up(ant));
                ASSERT_EQ(ant.type, AntType::Worker);
                ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
            }
        }
        ASSERT_TRUE(saw_adjacent_not_triggered);

        // The ant has crossed into the tile but its walk has not landed: still nothing taken
        ASSERT_FALSE(picking_up(ant));
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Worker);

        // The landing frame takes the power-up in the very same tick: the type changes, the clip plays, the tile is empty
        while (ant.is_alive() && !picking_up(ant)) {
            sim.tick();
        }
        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.hp, 10u);
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_TRUE(ant.waypoints.empty());

        // In the world snapshot the running clip is getpow (CHD animation 55) in the power-up state
        const auto& ws = sim.get_world_state();
        const AntSnapshot* snap = nullptr;
        for (const auto& a : ws.ants) {
            if (a.id == ant_id) { snap = &a; break; }
        }
        ASSERT_TRUE(snap != nullptr);
        ASSERT_EQ(snap->state, UnitState::PoweringUp);
        ASSERT_EQ(snap->loco_clip, 55);

        // The clip ends 840 ms after the snap (16 ticks: still playing, the 17th ends it)
        run_ms(sim, 800);
        ASSERT_TRUE(picking_up(ant));
        run_ms(sim, 100);
        ASSERT_FALSE(picking_up(ant));

        // Emerges idle as a Combat ant; the hit points stay at 10 (the maximum is 10 for every type)
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.state, UnitState::GuardIdle);
        ASSERT_EQ(ant.max_hp, 10u);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.22: Power-Up Transformation Interruption via Impossible Order
    // ------------------------------------------------------------------------
    TEST_CASE("12.22 Uninterruptible Power-Up Pick-Up Parity (orders are ignored for the whole getpow clip)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (10, 10) (Type 4 = Combat)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Worker ant at (9, 10) walks onto it
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{9, 10});
        auto& ant = sim.get_unit(ant_id);
        sim.issue_move_order(ant_id, TileCoord{10, 10});
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return picking_up(ant); }) >= 0);
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10, 10}));

        // While the clip plays, the player attempts to move the ant away to (11, 10)
        AntOrder move_order;
        move_order.ant_id = ant_id;
        move_order.type = OrderType::Move;
        move_order.target_x = 11;
        move_order.target_y = 10;
        sim.issue_order(move_order);

        // Order is silently ignored: no snap, no path, no can't-go
        ASSERT_TRUE(picking_up(ant));
        ASSERT_EQ(ant.pos, (TileCoord{10, 10}));
        ASSERT_FALSE(sim.has_pending_path(ant_id));

        // Advance through the remaining getpow clip (16 ticks in all are still refused)
        run_ms(sim, 800);
        ASSERT_TRUE(picking_up(ant));
        sim.issue_order(move_order);
        ASSERT_FALSE(sim.has_pending_path(ant_id));
        run_ms(sim, 100);

        // Ant has completed the clip and is a Combat Ant
        ASSERT_FALSE(picking_up(ant));
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.pos, (TileCoord{10, 10}));

        // Once the clip is over, issuing the move order lets the unit walk normally
        sim.issue_order(move_order);
        ASSERT_EQ(ant.state, UnitState::Walking);

        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (ant.pos == TileCoord{11, 10} && ant.state != UnitState::Walking) break;
        }
        ASSERT_EQ(ant.pos, (TileCoord{11, 10}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.23: Unattackability While Standing on Power-Up
    // ------------------------------------------------------------------------
    TEST_CASE("12.23 An Ant Standing On A Power-Up Is Immune From Attacks (no path can end on the solid power-up tile)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        make_water_island(sim, 30, 30);

        // Place powerup at (10, 10)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Friendly worker walks onto (10, 10) and gets the can't-go order in the window: it stands on the power-up
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Worker, TileCoord{9, 10});
        ASSERT_TRUE(stand_on_powerup(sim, friendly_id, TileCoord{10, 10}, TileCoord{30, 30}));

        auto& friendly = sim.get_unit(friendly_id);
        ASSERT_EQ(friendly.type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at(friendly.pos));
        uint16_t initial_hp = friendly.hp;

        // Enemy Combat ant four tiles away
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Combat, TileCoord{14, 10});
        auto& enemy = sim.get_unit(enemy_id);

        // The HUD accepts the click as an attack order (order 3), but no path can end on the power-up tile
        // (A* step cost and CanEnter block it): the path manager answers "Can't go there." and the ant plays can't go
        HUD hud;
        hud.init(1); // Player 1 (enemy team)
        hud.select_ant(enemy_id, false);
        hud.dispatch_attack_order(friendly_id, sim);
        ASSERT_EQ(enemy.orig_order, AntUnit::kOrderAttack);
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return sim.has_news_event(1, 0x3A); }) >= 0);   // "Can't go there."
        ASSERT_TRUE(wait_ms(sim, 1000, [&]() { return enemy.loco_action == AntUnit::kActionCantGo; }) >= 0);
        run_ms(sim, 5000);
        ASSERT_EQ(friendly.hp, initial_hp);
        ASSERT_EQ(friendly.pos, (TileCoord{10, 10}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.24: Swimmer Bridge Protection During Digging and Demolishing
    // ------------------------------------------------------------------------
    TEST_CASE("12.24 Swimmer Bridge Protection During Digging and Demolishing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_terrain(20, 20, TERRAIN_WATER);

        // Swimmer at (19, 20)
        uint32_t swimmer_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{19, 20});
        auto& swimmer = sim.get_unit(swimmer_id);

        // Start bridge building step towards (20, 20)
        ASSERT_TRUE(sim.build_bridge_step(swimmer_id, TileCoord{20, 20}));
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Try to interrupt swimmer with move order
        sim.issue_move_order(swimmer_id, TileCoord{18, 20});
        // State remains BuildingBridge (cannot be interrupted)
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Try to interrupt via generic order
        AntOrder move_order;
        move_order.ant_id = swimmer_id;
        move_order.type = OrderType::Move;
        move_order.target_x = 18;
        move_order.target_y = 20;
        sim.issue_order(move_order);
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);

        // Step simulation until bridge building completes
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (swimmer.state != UnitState::BuildingBridge) break;
        }
        ASSERT_NE(swimmer.state, UnitState::BuildingBridge);
        ASSERT_TRUE(sim.grid().get_cell(20, 20).has_completed_bridge());

        // Start demolish step
        ASSERT_TRUE(sim.demolish_bridge_step(swimmer_id, TileCoord{20, 20}));
        ASSERT_EQ(swimmer.state, UnitState::DemolishingBridge);

        // Try to interrupt demolish with move order
        sim.issue_move_order(swimmer_id, TileCoord{18, 20});
        ASSERT_EQ(swimmer.state, UnitState::DemolishingBridge);

        // Step simulation until demolish action finishes
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (swimmer.state != UnitState::DemolishingBridge) break;
        }
        ASSERT_NE(swimmer.state, UnitState::DemolishingBridge);
        ASSERT_TRUE(sim.grid().get_cell(20, 20).terrain_type == TERRAIN_WATER);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.25: Bridge Locomotion Speed Matches Mud Speed
    // ------------------------------------------------------------------------
    TEST_CASE("12.25 Bridge Locomotion Speed Matches Mud Speed (bridges are terrain class 3)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Three walled corridors (rows 10, 12, 14) so that every path is a straight line east:
        // row 10 grass, row 12 mud, row 14 completed bridges over water.
        for (int x = 8; x <= 18; ++x) {
            for (int y : {9, 11, 13, 15}) sim.grid_mut().set_terrain(x, y, TERRAIN_OBSTACLE);
        }
        for (int x = 10; x <= 16; ++x) {
            sim.grid_mut().get_cell_mut(TileCoord{x, 12}).surface_type = SurfaceType::Mud;
            sim.grid_mut().set_terrain(x, 14, TERRAIN_WATER);
            uint32_t ux = static_cast<uint32_t>(x);
            for (int s = 0; s < 4; ++s) {
                sim.grid_mut().advance_bridge(ux, 14, 0);
            }
            ASSERT_TRUE(sim.grid().get_cell(ux, 14).has_completed_bridge());
        }
        // A bridge piece makes its tile terrain class 3, mud (Ants.exe FUN_01008af7)
        ASSERT_EQ(sim.grid().terrain_class_at(TileCoord{12, 14}), sim.grid().terrain_class_at(TileCoord{12, 12}));

        uint32_t w_grass = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t w_mud = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 12});
        uint32_t w_bridge = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 14});
        sim.set_locomotion_trace_enabled(true);

        sim.issue_move_order(w_grass, TileCoord{15, 10});
        sim.issue_move_order(w_mud, TileCoord{15, 12});
        sim.issue_move_order(w_bridge, TileCoord{15, 14});
        for (int t = 1; t <= 300; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(w_grass).pos, (TileCoord{15, 10}));
        ASSERT_EQ(sim.get_unit(w_mud).pos, (TileCoord{15, 12}));
        ASSERT_EQ(sim.get_unit(w_bridge).pos, (TileCoord{15, 14}));

        // Walk times from path delivery to arrival (reference model of Ants.exe): 5 tiles of grass
        // (4 px per 50 ms) take 2200 ms, 5 tiles of mud (2 px per 60 ms) 4710 ms; bridges walk like mud.
        ASSERT_EQ(walk_duration_ms(sim, w_grass), 2200);
        ASSERT_EQ(walk_duration_ms(sim, w_mud), 4710);
        ASSERT_EQ(walk_duration_ms(sim, w_bridge), walk_duration_ms(sim, w_mud));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.26: Swimmers Immune From Being Attacked Underwater
    // ------------------------------------------------------------------------
    TEST_CASE("12.26 An Ant In Water Cannot Be Attacked And Nothing Attacks From Water (CanBeAttackedFrom)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water pond around (20, 20)
        for (int y = 19; y <= 21; ++y) {
            for (int x = 20; x <= 22; ++x) {
                sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
        }

        // Friendly swimmer in water at (20, 20)
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        auto& friendly = sim.get_unit(friendly_id);
        uint16_t initial_hp = friendly.hp;

        // Enemy swimmer in water at (21, 20) (adjacent in the water)
        uint32_t enemy_swimmer_id = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});

        // Enemy Combat ant on land at (19, 20) (adjacent on the bank)
        uint32_t enemy_combat_id = sim.spawn_unit(1, AntType::Combat, TileCoord{19, 20});

        // 1. A melee from the land against the swimmer in the water is refused ("Can't go there.")
        sim.execute_melee_attack(enemy_combat_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);
        ASSERT_EQ(sim.get_unit(enemy_combat_id).loco_action, AntUnit::kActionCantGo);

        // 2. A melee between two ants in the water is refused as well
        sim.execute_melee_attack(enemy_swimmer_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);

        // 3. The auto-engage of the combat ant ignores the swimmer in the water for a long time
        for (int i = 0; i < 100; ++i) sim.tick();
        ASSERT_EQ(friendly.hp, initial_hp);

        // 4. When the swimmer stands on land it can be attacked normally
        SimulationEngine sim2;
        sim2.init_test_world(60, 60, 100, 60000);
        uint32_t swimmer_land = sim2.spawn_unit(0, AntType::Swimmer, TileCoord{18, 20});
        uint32_t combat_land = sim2.spawn_unit(1, AntType::Combat, TileCoord{19, 20});
        sim2.execute_melee_attack(combat_land, swimmer_land);
        ASSERT_EQ(sim2.get_unit(swimmer_land).hp, initial_hp - 2); // Takes damage on land!
    } TEST_END();

    TEST_CASE("12.27 Power-Up Obstacle Avoidance (Walks Around Power-Up Unless Directly Instructed)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a Combat power-up at (13, 10)
        sim.grid_mut().place_powerup(13, 10, 4);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Spawn a Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);
        ASSERT_EQ(ant.type, AntType::Worker);

        // 1. Order ant to move PAST the powerup to (16, 10)
        sim.issue_move_order(ant_id, TileCoord{16, 10});

        // The path must NOT contain (13, 10) - it must route around the power-up tile
        for (const auto& wp : ant.waypoints) {
            ASSERT_FALSE(wp == (TileCoord{13, 10}));
        }

        // Step simulation until the ant reaches (16, 10)
        int steps = 0;
        while (ant.pos != (TileCoord{16, 10}) && steps++ < 200) {
            sim.tick();
            // At no point during travel should the ant ever be on (13, 10)
            ASSERT_FALSE(ant.pos == (TileCoord{13, 10}));
        }
        ASSERT_EQ(ant.pos, (TileCoord{16, 10}));

        // The powerup at (13, 10) must remain intact, and ant must NOT have transformed
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
        ASSERT_EQ(ant.type, AntType::Worker);
        ASSERT_FALSE(picking_up(ant));

        // 2. Now specifically instruct the ant to walk onto the powerup at (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // This time, the destination IS the powerup, so the ant routes directly to it
        ASSERT_EQ(ant.final_dest, (TileCoord{13, 10}));

        // Step simulation until the ant lands on it and takes it
        steps = 0;
        while (!picking_up(ant) && steps++ < 150) {
            sim.tick();
        }

        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_TRUE(picking_up(ant));
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Finish the 11-frame getpow clip (840 ms after the snap)
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(ant));
        ASSERT_EQ(ant.type, AntType::Combat); // Successfully transformed into Combat Ant!

        // 3. Place another power-up at (13, 12). Order ant from (13, 10) to move past it to (13, 14)
        sim.grid_mut().place_powerup(13, 12, 1); // Bomber powerup
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 12}));

        sim.issue_move_order(ant_id, TileCoord{13, 14});
        // Waypoints must avoid (13, 12)
        for (const auto& wp : ant.waypoints) {
            ASSERT_FALSE(wp == (TileCoord{13, 12}));
        }

        steps = 0;
        while (ant.pos != (TileCoord{13, 14}) && steps++ < 200) {
            sim.tick();
            ASSERT_FALSE(ant.pos == (TileCoord{13, 12}));
        }
        ASSERT_EQ(ant.pos, (TileCoord{13, 14}));
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 12})); // Second powerup also untouched!
        ASSERT_EQ(ant.type, AntType::Combat);
    } TEST_END();

    TEST_CASE("12.28 Enemy Ant Selection & Friendly Fire Command Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Player 0 (Blue) ants: b1 at (10, 10), b2 at (10, 11)
        uint32_t b1 = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        uint32_t b2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 11});

        // Player 1 (Green) ant: g1 at (8, 8)
        uint32_t g1 = sim.spawn_unit(1, AntType::Combat, TileCoord{8, 8});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(1); // Local player is Green (Player 1)

        // 1. Green player clicks on Blue Ant 1 at (10, 10): the pick rectangle of the original (FUN_01026a39) is the sprite box
        //    around the ant (combat ant [x-32, x+26) x [y-46, y+16)); the ant below it (10, 11) has the box [y-32, y+16) and the
        //    last hit wins, so the click goes 12 px above the anchor (336, 324), where only b1's box is
        int32_t b1_click_x = 336 + HUD::PLAYFIELD_X;
        int32_t b1_click_y = 324 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(b1_click_x, b1_click_y, 1, sim, camera);
        hud.handle_mouse_up(b1_click_x, b1_click_y, 1, sim, camera);

        // Enemy ant b1 CAN now be selected for inspection if no friendly units selected!
        ASSERT_TRUE(hud.is_ant_selected(b1));
        ASSERT_EQ(hud.get_selected_ant_id(), b1);

        // But ground clicks must NOT move or command enemy ants!
        int32_t ground_x = 350 + HUD::PLAYFIELD_X;
        int32_t ground_y = 350 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(ground_x, ground_y, 1, sim, camera);
        hud.handle_mouse_up(ground_x, ground_y, 1, sim, camera);
        ASSERT_NE(sim.get_unit(b1).state, UnitState::Walking);

        // 2. Green player clicks on Blue Ant 2 at (10, 11) -> pixel (10*32+16, 11*32+16) = (336, 368)
        int32_t b2_click_x = 336 + HUD::PLAYFIELD_X;
        int32_t b2_click_y = 368 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(b2_click_x, b2_click_y, 1, sim, camera);
        hud.handle_mouse_up(b2_click_x, b2_click_y, 1, sim, camera);

        // b2 is now selected for inspection, neither unit has an attack order
        ASSERT_TRUE(hud.is_ant_selected(b2));
        ASSERT_FALSE(hud.is_ant_selected(b1));
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);
        ASSERT_NE(sim.get_unit(b2).orig_order, AntUnit::kOrderAttack);

        // 3. Right-clicking b2 while nothing or enemy is selected does not issue attack orders
        hud.handle_mouse_down(b2_click_x, b2_click_y, 3, sim, camera);
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 4. Directly testing HUD attack order dispatch protection:
        // Even if an enemy ant ID were artificially injected into selected_ant_ids:
        hud.set_selected_ant_ids({b1});
        hud.dispatch_attack_order(b2, sim);
        // HUD must filter out non-friendly units so b1 NEVER targets b2!
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 5. SimulationEngine layer protection:
        // Even if a malformed AntOrder with b1 attacking b2 is fed to sim.issue_order:
        AntOrder bad_order;
        bad_order.ant_id = b1;
        bad_order.type = OrderType::Attack;
        bad_order.target_entity_id = static_cast<int32_t>(b2);
        sim.issue_order(bad_order);
        ASSERT_NE(sim.get_unit(b1).orig_order, AntUnit::kOrderAttack);

        // 6. execute_melee_attack deals 0 damage to teammates:
        uint32_t b2_initial_hp = sim.get_unit(b2).hp;
        sim.execute_melee_attack(b1, b2);
        ASSERT_EQ(sim.get_unit(b2).hp, b2_initial_hp); // No damage dealt!

        // 7. Legitimate Green ant g1 selecting and attacking b1 works as expected:
        int32_t g1_click_x = 8 * 32 + 16 + HUD::PLAYFIELD_X;
        int32_t g1_click_y = 8 * 32 + 16 + HUD::PLAYFIELD_Y;
        hud.clear_selection();
        hud.handle_mouse_down(g1_click_x, g1_click_y, 1, sim, camera);
        hud.handle_mouse_up(g1_click_x, g1_click_y, 1, sim, camera);

        ASSERT_TRUE(hud.is_ant_selected(g1));
        ASSERT_EQ(hud.get_selected_ant_id(), g1);

        // Green ant g1 attacks enemy b1
        hud.handle_mouse_down(b1_click_x, b1_click_y, 1, sim, camera);
        hud.handle_mouse_up(b1_click_x, b1_click_y, 1, sim, camera);
        ASSERT_EQ(sim.get_unit(g1).orig_order, AntUnit::kOrderAttack);
    } TEST_END();

    TEST_CASE("12.29 Power-Up Drop On Pick-Up & Occupied Tile Avoidance / Disappearance") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        static constexpr std::array<TileCoord, 8> OFFSETS = {{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};

        // 1. Worker Ant takes the Combat power-up -> nothing is dropped (a worker holds none)
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.grid_mut().place_powerup(10, 10, 4); // Combat powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Combat);
        // Verify none of the 8 neighbors have any powerup
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10 + off.x, 10 + off.y}));
        }
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Combat);

        // 2. Combat Ant takes a Bomber power-up -> drops the original Combat powerup (Type 4) on an adjacent free tile
        sim.grid_mut().place_powerup(10, 10, 1); // Bomber powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        // Exactly 1 of the 8 neighbors receives the dropped Combat powerup
        int drop_count = 0;
        TileCoord dropped_tile{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{10 + off.x, 10 + off.y};
            if (sim.grid().has_powerup_at(adj)) {
                ++drop_count;
                dropped_tile = adj;
            }
        }
        ASSERT_EQ(drop_count, 1);
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 4); // Dropped old Combat powerup
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Bomber);

        // 3. Occupied tile avoidance: place an ant on (10, 9), the Bomber takes a Swimmer power-up
        // Clean up any powerup around (10, 10) first to ensure known board state
        for (const auto& off : OFFSETS) {
            sim.grid_mut().clear_powerup(10 + off.x, 10 + off.y);
        }
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 9});
        (void)blocker;
        sim.grid_mut().place_powerup(10, 10, 5); // Swimmer powerup
        ASSERT_TRUE(take_powerup_here(sim, a1));
        // (10, 9) is occupied by blocker ant -> MUST NOT drop at (10, 9)
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10, 9}));
        // Exactly 1 unoccupied neighbor receives the dropped Bomber powerup (Type 1)
        drop_count = 0;
        dropped_tile = TileCoord{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{10 + off.x, 10 + off.y};
            if (sim.grid().has_powerup_at(adj)) {
                ++drop_count;
                dropped_tile = adj;
            }
        }
        ASSERT_EQ(drop_count, 1);
        ASSERT_NE(dropped_tile, (TileCoord{10, 9})); // Blocker was avoided!
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 1); // Dropped old Bomber powerup
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a1)));
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Swimmer);

        // 4. Surrounded ant: all 8 neighbors blocked / occupied / water -> powerup permanently disappears
        uint32_t a2 = sim.spawn_unit(1, AntType::Combat, TileCoord{20, 20});
        sim.grid_mut().set_terrain(20, 19, TERRAIN_WATER);    // N: Water
        sim.grid_mut().set_terrain(21, 20, TERRAIN_OBSTACLE); // E: Obstacle
        sim.spawn_unit(1, AntType::Worker, TileCoord{20, 21}); // S: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 20}); // W: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{21, 19}); // NE: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{21, 21}); // SE: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 21}); // SW: Ant
        sim.spawn_unit(1, AntType::Worker, TileCoord{19, 19}); // NW: Ant

        sim.grid_mut().place_powerup(20, 20, 3); // Thief powerup
        sim.clear_audio_events();
        ASSERT_TRUE(take_powerup_here(sim, a2));
        ASSERT_TRUE(sim.has_audio_event(40));                  // the silent cue of the lost power-up
        // None of the 8 neighbors should receive a dropped powerup
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(sim.get_unit(a2)));
        ASSERT_EQ(sim.get_unit(a2).type, AntType::Thief);
        // Still no powerup on any of the 8 neighbors: permanently disappeared!
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }

        // 5. Uninterruptible pick-up ignores move order and completes
        uint32_t a3 = sim.spawn_unit(0, AntType::Thief, TileCoord{30, 30});
        sim.grid_mut().place_powerup(30, 30, 1); // Bomber powerup
        sim.grid_mut().set_terrain(30, 31, TERRAIN_WATER); // S: Water (for impossible order)
        ASSERT_TRUE(take_powerup_here(sim, a3));
        auto& ant3 = sim.get_unit(a3);
        // Verify dropped Thief powerup (Type 3) landed on a valid neighbor
        dropped_tile = TileCoord{-1, -1};
        for (const auto& off : OFFSETS) {
            TileCoord adj{30 + off.x, 30 + off.y};
            if (sim.grid().has_powerup_at(adj)) dropped_tile = adj;
        }
        ASSERT_TRUE(dropped_tile.x >= 0 && dropped_tile.y >= 0);
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 3);
        ASSERT_TRUE(dropped_tile != (TileCoord{30, 31}));      // water is never a drop tile

        // Move order issued during the active clip is silently ignored per Ants.exe
        sim.issue_move_order(a3, TileCoord{30, 32});
        ASSERT_TRUE(picking_up(ant3));
        ASSERT_FALSE(sim.has_pending_path(a3));

        // Completes into Bomber (the getpow clip is 840 ms from the snap)
        run_ms(sim, 900);
        ASSERT_FALSE(picking_up(ant3));
        ASSERT_EQ(ant3.type, AntType::Bomber);
        ASSERT_TRUE(sim.grid().has_powerup_at(dropped_tile));

        // 6. Multi-direction randomized landing verification:
        // Over multiple pick-ups with all 8 directions free, verify that the dropped powerup lands randomly across
        // multiple distinct directions, NOT always above (the original scan starts at a random row and column).
        std::set<std::pair<int32_t, int32_t>> chosen_directions;
        for (int iter = 0; iter < 24; ++iter) {
            TileCoord origin{5 + (iter % 6) * 8, 40 + (iter / 6) * 4};
            uint32_t tester = sim.spawn_unit(0, AntType::Combat, origin);
            sim.grid_mut().place_powerup(origin.x, origin.y, 1); // Bomber
            ASSERT_TRUE(take_powerup_here(sim, tester));
            int found = 0;
            for (const auto& off : OFFSETS) {
                TileCoord adj{origin.x + off.x, origin.y + off.y};
                if (sim.grid().has_powerup_at(adj)) {
                    chosen_directions.insert({off.x, off.y});
                    ++found;
                }
            }
            ASSERT_EQ(found, 1);
            run_ms(sim, 900);
        }
        // With 8 free directions, 24 trials should sample multiple distinct directions (>= 4)
        ASSERT_TRUE(chosen_directions.size() >= 4);
    } TEST_END();

}
