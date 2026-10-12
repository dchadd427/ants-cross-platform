// Application integration tests: suites 7 and 8 (input scheme, marquee selection, right-click abilities, unit health, map select)
#include "app_integration_fixtures.hpp"

// ============================================================================
// SUITE 7: Input Scheme, Multi-Unit Marquee Selection & Right-Click Abilities
// ============================================================================

void run_suite_7_input_controls() {
    TEST_SUITE("Suite 7: Input Scheme, Multi-Unit Marquee Selection & Right-Click Abilities");

    TEST_CASE("7.1 Single Click Friendly Selection vs Ground Click Move Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Click on ant at (5, 5) -> pixel (176, 176)
        int32_t sx = 176 + HUD::PLAYFIELD_X;
        int32_t sy = 176 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(sx, sy, 1, sim, camera);
        hud.handle_mouse_up(sx, sy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_ant_id(), a1);
        ASSERT_TRUE(hud.is_ant_selected(a1));

        // Click ground at tile (8, 8) -> pixel (272, 272)
        int32_t gx = 272 + HUD::PLAYFIELD_X;
        int32_t gy = 272 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(gx, gy, 1, sim, camera);
        hud.handle_mouse_up(gx, gy, 1, sim, camera);

        ASSERT_EQ(hud.get_selected_ant_id(), a1);
        const auto& u = sim.get_unit(a1);
        ASSERT_TRUE(!u.waypoints.empty() || u.state == UnitState::Walking);
    } TEST_END();

    TEST_CASE("7.2 Marquee Drag Box (>4px) Multi-Unit Friendly Selection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{6, 6});
        uint32_t a3 = sim.spawn_unit(0, AntType::Bomber, TileCoord{7, 7});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{6, 5}); // Enemy in box

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Drag marquee from (4*32, 4*32) to (8*32, 8*32)
        int32_t sx1 = 4 * 32 + HUD::PLAYFIELD_X;
        int32_t sy1 = 4 * 32 + HUD::PLAYFIELD_Y;
        int32_t sx2 = 8 * 32 + HUD::PLAYFIELD_X;
        int32_t sy2 = 8 * 32 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(sx1, sy1, 1, sim, camera);
        hud.handle_mouse_motion(sx2, sy2, sim, camera);
        hud.handle_mouse_up(sx2, sy2, 1, sim, camera);

        const auto& sel = hud.get_selected_ant_ids();
        ASSERT_EQ(sel.size(), 3u);
        ASSERT_TRUE(hud.is_ant_selected(a1));
        ASSERT_TRUE(hud.is_ant_selected(a2));
        ASSERT_TRUE(hud.is_ant_selected(a3));
        ASSERT_FALSE(hud.is_ant_selected(enemy)); // Enemy not selected by marquee

        // 7.2b: Partial sprite intersection test (marquee grazes top of sprite without encompassing center px/py)
        // a1 at TileCoord{5, 5}: px = 176, py = 176, sprite top is ~150.
        // Drag marquee that only covers y from 100 to 160 (center y=176 is outside, but head is inside):
        hud.clear_selection();
        int32_t p_sx1 = 150 + HUD::PLAYFIELD_X;
        int32_t p_sy1 = 100 + HUD::PLAYFIELD_Y;
        int32_t p_sx2 = 200 + HUD::PLAYFIELD_X;
        int32_t p_sy2 = 160 + HUD::PLAYFIELD_Y; // Stops at 160, before ant center 176

        hud.handle_mouse_down(p_sx1, p_sy1, 1, sim, camera);
        hud.handle_mouse_motion(p_sx2, p_sy2, sim, camera);
        hud.handle_mouse_up(p_sx2, p_sy2, 1, sim, camera);

        ASSERT_TRUE(hud.is_ant_selected(a1)); // Any part of sprite inside marquee selects unit
    } TEST_END();

    TEST_CASE("7.3 Enemy Click with Friendly Selected Dispatches Attack Order") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t friendly = sim.spawn_unit(0, AntType::Combat, TileCoord{5, 5});
        uint32_t enemy = sim.spawn_unit(1, AntType::Worker, TileCoord{6, 5});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);
        hud.select_ant(friendly);

        // Click enemy ant at (6, 5) -> pixel (208, 176)
        int32_t ex = 208 + HUD::PLAYFIELD_X;
        int32_t ey = 176 + HUD::PLAYFIELD_Y;

        hud.handle_mouse_down(ex, ey, 1, sim, camera);
        hud.handle_mouse_up(ex, ey, 1, sim, camera);

        // The click is a move order onto the enemy's tile that the classification turns into the attack order 3;
        // nothing is hit at the click, the punch lands when the step crosses the tile border
        ASSERT_EQ(sim.get_unit(friendly).orig_order, AntUnit::kOrderAttack);
        ASSERT_EQ(sim.get_unit(enemy).hp, 10u);
        ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(enemy).hp < 10u; }) >= 0);
        ASSERT_EQ(sim.get_unit(enemy).hp, 8u);   // a combat ant punches for 2 hit points
    } TEST_END();

    TEST_CASE("7.4 Right Click Special Abilities (Bomber/Fire/Swimmer/Thief)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{5, 5});
        uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{7, 7});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // 1. Right click with Bomber -> PlantBomb at (6, 5) -> pixel (208, 176)
        hud.select_ant(bomber);
        int32_t bx = 208 + HUD::PLAYFIELD_X;
        int32_t by = 176 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(bx, by, 3, sim, camera);
        hud.handle_mouse_up(bx, by, 3, sim, camera);     // the right button gives its order at the release (FUN_01027b51)
        ASSERT_EQ(sim.get_unit(bomber).orig_order, AntUnit::kOrderPlant);
        // The plant clip plays (1360-1400 ms) and the bomb appears when it ends
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.has_bomb_at(TileCoord{6, 5}); }) >= 1000);

        // 2. Set water at (8, 7) and right click with Swimmer -> BuildBridge -> pixel (272, 240)
        sim.set_terrain(8, 7, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer);
        int32_t sx = 272 + HUD::PLAYFIELD_X;
        int32_t sy = 240 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);
        hud.handle_mouse_up(sx, sy, 3, sim, camera);
        ASSERT_EQ(sim.get_unit(swimmer).orig_order, AntUnit::kOrderBridgeBuild);
        // stage 0x22 exists as soon as the swimmer starts to dig; three passes later the bridge is done
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_bridge_at(TileCoord{8, 7}); }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 4000, [&]() { return sim.grid().get_cell(TileCoord{8, 7}).has_completed_bridge(); }) >= 0);
    } TEST_END();

    TEST_CASE("7.5 Hotkeys: Ctrl+A Select All, Ctrl+N / Ctrl+P Cycle, Ctrl+H Home Hill, Esc Deselects, Ctrl+Q Quit Dialog") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{5, 5});
        uint32_t a2 = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 15});
        uint32_t a3 = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});

        ViewportCamera camera;
        camera.x = 0.0f; camera.y = 0.0f;
        camera.world_x = 0; camera.world_y = 0;

        HUD hud;
        hud.init(0);

        // Bare 'a' key without Ctrl does NOT trigger hotkey (does not select units)
        hud.handle_key_down('a', sim, camera, 0);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 0u);

        // 'Ctrl+A' key selects all friendly units
        hud.handle_key_down('a', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 3u);

        // 'Ctrl+N' key cycles to next ant
        hud.select_ant(a1);
        hud.handle_key_down('n', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a2);

        hud.handle_key_down('n', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        hud.handle_key_down('n', sim, camera, KMOD_CTRL); // Wrap around
        ASSERT_EQ(hud.get_selected_ant_id(), a1);

        // 'Ctrl+P' key cycles to previous ant
        hud.handle_key_down('p', sim, camera, KMOD_CTRL); // Wrap around backwards
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        // 'Ctrl+C' does nothing: the original has no such key (FUN_0102609a)
        hud.handle_key_down('c', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        // 'Esc' deselects everything (FUN_01028c44(0)); it does not open the quit dialog
        hud.handle_key_down(27, sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // 'Ctrl+H' selects the home hill (no hatch, no scrolling)
        hud.handle_key_down('h', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);

        // 'Ctrl+Q' starts the quit flow like the Quit button; in its dialog Enter does nothing, Esc answers No
        hud.handle_key_down('q', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(27, sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());
    } TEST_END();

    TEST_CASE("7.6 Camera Viewport Edge Panning & Minimap Jump Navigation") {
        ViewportCamera camera;
        camera.x = 100.0f;
        camera.y = 100.0f;
        camera.world_x = 100;
        camera.world_y = 100;
        camera.viewport_w = 442;
        camera.viewport_h = 440;

        // The view moves in whole pixel steps (the original's 50 ms input task) and stays inside the map
        camera.scroll_pixels(24, 24, 60, 60);
        ASSERT_EQ(camera.world_x, 124);
        ASSERT_EQ(camera.world_y, 124);
        camera.scroll_pixels(-500, -500, 60, 60);
        ASSERT_EQ(camera.world_x, 0);
        ASSERT_EQ(camera.world_y, 0);

        // Center on tile (30, 30) -> world px (960, 960)
        camera.center_on(960, 960, 60, 60);
        int32_t expected_x = 960 - 442 / 2;
        int32_t expected_y = 960 - 440 / 2;
        ASSERT_EQ(camera.world_x, expected_x);
        ASSERT_EQ(camera.world_y, expected_y);
    } TEST_END();

    TEST_CASE("7.6b A Map With A Start Marker Outside The Grid Is Not Started (Application::load_match Asks LevelData::validate For The Teams Of The Match; The Original Has No Behaviour For It)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // the file loads, the match with all four teams is refused
        const std::filesystem::path unplayable = write_marker_outside_grid_map();
        ASSERT_FALSE(unplayable.empty());
        const bool started = app.start_game(unplayable.string());
        std::error_code ignore;
        std::filesystem::remove(unplayable, ignore);
        ASSERT_FALSE(started);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());                                  // the setup screen is not a running match

        // the shipped map starts as ever
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TINY.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running());
    } TEST_END();

    TEST_CASE("7.6c A Game That Starts Straight Into Its Match (--map) Refuses The Same Map: Application::init Asks LevelData::validate For All Four Teams; The Setup Screen Starts Without Asking (The Match Is Checked When It Starts)") {
        const std::filesystem::path unplayable = write_marker_outside_grid_map();
        ASSERT_FALSE(unplayable.empty());
        {
            Application direct;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = unplayable.string();
            ASSERT_FALSE(direct.init(cfg));
            direct.shutdown();
        }
        {
            Application direct;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
            ASSERT_TRUE(direct.init(cfg));
            ASSERT_EQ(direct.state(), AppState::Playing);
            direct.shutdown();
        }
        {
            Application setup;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = unplayable.string();
            ASSERT_TRUE(setup.init(cfg));
            ASSERT_EQ(setup.state(), AppState::MapSelect);
            ASSERT_FALSE(setup.start_game(unplayable.string()));
            ASSERT_EQ(setup.state(), AppState::MapSelect);
            setup.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(unplayable, ignore);
    } TEST_END();

    TEST_CASE("7.6d --play: the first visit of the setup screen starts the chosen map at once, through the setup screen's own START path (the \"get ready\" dialog, --bot's seats), in the web page's order too (loading screen, quick help, the match); nothing else changes: no flag keeps the setup screen, the direct start of --map has no dialog, and the way back to the setup screen after a match is the setup screen") {
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        std::vector<ants::ai::BotSpec> bots(3);
        for (uint8_t i = 0; i < 3; ++i) {
            bots[i].seat = static_cast<uint8_t>(i + 1);
            bots[i].level = ants::ai::Level::Medium;
        }
        const auto config = [&](bool play, bool with_bots) {
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = tiny;
            cfg.play_at_once = play;
            if (with_bots) cfg.bots = bots;
            return cfg;
        };
        {   // without the flag: the setup screen, as ever
            Application app;
            ASSERT_TRUE(app.init(config(false, true)));
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.match_running());
            ASSERT_TRUE(app.bots() == nullptr);
            app.shutdown();
        }
        {   // with it, alone: the match of the chosen map, through START's path: its dialog is up
            Application app;
            ASSERT_TRUE(app.init(config(true, false)));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.match_running());
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_TRUE(app.bots() == nullptr);
            ASSERT_EQ(app.sim().grid().width(), 31);                                  // TINY.LVL is 31 x 31 tiles (the setup screen's own choice would have been TREASURE)
            // the one visit is the first: the way back to the setup screen is the setup screen, and a second visit starts nothing
            app.return_to_map_select();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.match_running());
            app.shutdown();
        }
        {   // with it and the three bots: they are seated and run
            Application app;
            ASSERT_TRUE(app.init(config(true, true)));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.match_running());
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_TRUE(app.bots() != nullptr);
            app.shutdown();
        }
        {   // the direct start of --map (the tests' and the screenshots'): the match at once, without that dialog
            Application app;
            ApplicationConfig cfg = config(false, false);
            cfg.start_in_map_select = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_FALSE(app.hud().is_match_start_modal_active());
            app.shutdown();
        }
        {   // the web page's order: the loading screen, then the quick help, then the match, with no setup screen in between
            const std::filesystem::path settings = temp_path_of_this_run("ants_play_settings").replace_extension(".ini");
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            Application app;
            ApplicationConfig cfg = config(true, true);
            cfg.headless = false;
            cfg.skip_intro = false;
            cfg.settings_path = settings.string();
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Loading);
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            app.quick_help_key(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.match_running());
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_TRUE(app.bots() != nullptr);
            app.shutdown();
            std::error_code ignore;
            std::filesystem::remove(settings, ignore);
        }
    } TEST_END();

    TEST_CASE("7.6h The site statistics' hook (the web page has the server count a game on this computer): told ONCE for each game, at its first tick (not at the start, not while the \"get ready\" dialog is up, not at the later ticks), again for the next match of the same application, the same for a game with bots and for the direct start; a hook that is cleared or was never set says nothing") {
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        const auto config = [&](bool direct, bool with_bot) {
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = !direct;
            cfg.default_map_path = tiny;
            cfg.play_at_once = !direct;
            if (with_bot) {
                ants::ai::BotSpec bot;
                bot.seat = 1;
                bot.level = ants::ai::Level::Medium;
                cfg.bots.push_back(bot);
            }
            return cfg;
        };
        {   // the setup screen's own START (the page's Play): told on the step after the dialog's 100 steps, where the first tick runs, and never again in the match
            int told = 0;
            Application app;
            app.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(app.init(config(false, false)));
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_EQ(told, 0);                                                       // the match began: nothing is told yet
            int steps = 0;
            while (app.sim().current_tick() == 0 && steps < 300) {
                ASSERT_EQ(told, 0);                                                   // (the dialog is up: no tick, no word)
                app.update_simulation(0.05f);
                ++steps;
            }
            ASSERT_EQ(app.sim().current_tick(), 1u);
            ASSERT_EQ(steps, 101);                                                    // the dialog's 100 steps and the one that runs the tick
            ASSERT_EQ(told, 1);
            for (int i = 0; i < 400; ++i) app.update_simulation(0.05f);
            ASSERT_TRUE(app.sim().current_tick() > 300);
            ASSERT_EQ(told, 1);                                                       // once, however long the match runs
            // the next match of this application (the way back to the setup screen and START again): its own first tick is told, once
            app.return_to_map_select();
            ASSERT_TRUE(app.start_game(tiny));
            ASSERT_EQ(told, 1);                                                       // (the start is not the word)
            app.hud().dismiss_match_start_modal();
            app.update_simulation(0.05f);
            ASSERT_EQ(told, 2);
            for (int i = 0; i < 50; ++i) app.update_simulation(0.05f);
            ASSERT_EQ(told, 2);
            app.shutdown();
        }
        {   // a game with a bot, and the direct start of --map (no dialog: the first step runs the first tick)
            int told = 0;
            Application bots;
            bots.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(bots.init(config(false, true)));
            ASSERT_TRUE(bots.bots() != nullptr);
            bots.hud().dismiss_match_start_modal();
            ASSERT_EQ(told, 0);
            for (int i = 0; i < 40; ++i) bots.update_simulation(0.05f);
            ASSERT_EQ(told, 1);
            bots.shutdown();
            Application direct;
            direct.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(direct.init(config(true, false)));
            ASSERT_EQ(told, 1);
            ASSERT_FALSE(direct.hud().is_match_start_modal_active());
            direct.update_simulation(0.05f);
            ASSERT_EQ(told, 2);
            for (int i = 0; i < 40; ++i) direct.update_simulation(0.05f);
            ASSERT_EQ(told, 2);
            direct.shutdown();
        }
        {   // a hook that is cleared before the first tick says nothing; one that is set after the match began (before its first tick) is told
            int told = 0;
            Application cleared;
            cleared.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(cleared.init(config(false, false)));
            cleared.set_on_local_match_started(nullptr);
            cleared.hud().dismiss_match_start_modal();
            for (int i = 0; i < 10; ++i) cleared.update_simulation(0.05f);
            ASSERT_EQ(told, 0);
            Application late;
            ASSERT_TRUE(late.init(config(false, false)));
            late.hud().dismiss_match_start_modal();
            late.set_on_local_match_started([&told]() { ++told; });
            for (int i = 0; i < 10; ++i) late.update_simulation(0.05f);
            ASSERT_EQ(told, 1);
            cleared.shutdown();
            late.shutdown();
        }
    } TEST_END();

    TEST_CASE("7.6e A bot that declines the player's invitation to team up says why: ONE line in the chat log right after the original's \"... rejected teaming up\" (only two teams left, the player has a teammate, the bot has one, the worker bot never teams up), nothing for a bot that accepts or for a person's answer, and never twice") {
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        const auto bot = [](uint8_t seat, const char* kind = "standard") {
            ants::ai::BotSpec spec;
            spec.seat = seat;
            spec.kind = kind;
            spec.level = ants::ai::Level::Medium;
            return spec;
        };
        const std::string two_left = "Bots team up only while three or more teams play.";
        const std::string you_have = "You already have a teammate.";
        const std::string never = "This bot never teams up.";
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        const auto transcript = [](Application& app) { return app.hud().chat_transcript(std::string()); };
        // A local game of the setup screen's own START (as the web page's Play makes it): the dialog is dismissed and the simulation runs by hand, one tick for each call, the bots acting in post_tick
        const auto start = [&](Application& app, const std::vector<ants::ai::BotSpec>& bots, const std::array<std::string, 4>& names = {}) {
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = tiny;
            cfg.play_at_once = true;
            cfg.bots = bots;
            cfg.team_names = names;
            if (!app.init(cfg) || app.state() != AppState::Playing) return false;
            app.hud().dismiss_match_start_modal();
            return true;
        };
        // Ticks until `done()` holds (at most `limit`); returns whether it did. The check is made after every tick: the status line and the chat log are looked at in the very tick of the news
        const auto run_until = [](Application& app, const std::function<bool()>& done, int limit) {
            for (int t = 0; t < limit; ++t) {
                app.update_simulation(0.05f);
                if (done()) return true;
            }
            return false;
        };
        const auto invite = [](Application& app, uint8_t from, uint8_t to) {          // the invitation as a command, whatever the HUD would have asked first
            Command c;
            c.type = CommandType::AllianceInvite;
            c.issuer = from;
            c.other_player = to;
            return app.sim().apply_command(c).accepted();
        };
        const std::vector<std::string> all_notes = {two_left, you_have, never, "already has a teammate."};
        const auto notes_in = [&](const std::string& log) {
            size_t n = 0;
            for (const std::string& note : all_notes) n += count_of(log, note);
            return n + count_of(log, "News Flash: \n");                                                   // (a flash with no text is a line of the log too: it counts as a note that must not be there)
        };

        {   // one bot: two teams play, the alliance would end the match at once. The player asks through the HUD (the pedestal's request), the bot says no, the game says why
            Application app;
            ASSERT_TRUE(start(app, {bot(1)}));
            app.hud().request_team_up(app.sim(), 1);
            ASSERT_EQ(app.sim().get_world_state().pending_invite_from[1], 0);
            ASSERT_TRUE(run_until(app, [&] { return count_of(transcript(app), two_left) > 0; }, 400));
            ASSERT_EQ(app.hud().status_line().text(), std::string("Bot (Medium) rejected teaming up"));    // the original's text, unchanged, is the status of this very tick
            ASSERT_EQ(count_of(transcript(app), "News Flash: " + two_left), 1u);                           // ... and the line is a News Flash of the chat log
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            for (int t = 0; t < 300; ++t) app.update_simulation(0.05f);                                    // nothing says it again
            ASSERT_EQ(count_of(transcript(app), two_left), 1u);
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            app.shutdown();
        }
        {   // the player already has a teammate (a command that the HUD would have asked about first: it breaks the team before it invites; here the team stands when the bot answers)
            Application app;
            ASSERT_TRUE(start(app, {bot(1), bot(2), bot(3)}));
            app.sim().form_alliance(0, 1);
            ASSERT_TRUE(invite(app, 0, 2));
            ASSERT_TRUE(run_until(app, [&] { return count_of(transcript(app), you_have) > 0; }, 400));
            ASSERT_EQ(app.hud().status_line().text(), std::string("Bot (Medium) rejected teaming up"));
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            for (int t = 0; t < 200; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            app.shutdown();
        }
        {   // the bot already has a teammate (two bots that are a team): the line names the bot as the game names it (-N / --team-name wins over "Bot (Medium)")
            Application app;
            ASSERT_TRUE(start(app, {bot(1), bot(2), bot(3)}, {"", "", "Maple", ""}));
            app.sim().form_alliance(2, 3);
            app.hud().request_team_up(app.sim(), 2);
            ASSERT_TRUE(run_until(app, [&] { return count_of(transcript(app), "Maple already has a teammate.") > 0; }, 400));
            ASSERT_EQ(app.hud().status_line().text(), std::string("Maple rejected teaming up"));
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            app.shutdown();
        }
        {   // the worker bot (the tournaments' yardstick) never teams up: its own line, although the standard rule would accept in this world of four teams
            Application app;
            ASSERT_TRUE(start(app, {bot(1, "worker"), bot(2), bot(3)}));
            app.hud().request_team_up(app.sim(), 1);
            ASSERT_TRUE(run_until(app, [&] { return count_of(transcript(app), never) > 0; }, 400));
            ASSERT_EQ(app.hud().status_line().text(), std::string("Bot (Worker) rejected teaming up"));
            ASSERT_EQ(notes_in(transcript(app)), 1u);
            app.shutdown();
        }
        {   // a bot that accepts: the original's texts only (the team is made), no line of ours
            Application app;
            ASSERT_TRUE(start(app, {bot(1), bot(2), bot(3)}));
            app.hud().request_team_up(app.sim(), 1);
            ASSERT_TRUE(run_until(app, [&] { return app.sim().get_world_state().player_alliances[0] == 1; }, 400));
            for (int t = 0; t < 20; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(notes_in(transcript(app)), 0u);
            ASSERT_EQ(count_of(transcript(app), "are a team now!"), 1u);
            app.shutdown();
        }
        {   // the same where the system's user and machine make a long default name (a macOS runner's machine name is about 50 characters): the name is cut as a typed one (32 printable
            // characters), so the original's "... are a team now!" stays whole in its chat entry (100 characters)
            struct LongUser {
                std::string saved;
                bool had{false};
                static void put(const char* value) {
#if defined(_WIN32)
                    _putenv_s("USER", value);                                         // ("" removes it)
#else
                    if (*value != '\0') ::setenv("USER", value, 1);
                    else ::unsetenv("USER");
#endif
                }
                LongUser() {
                    const char* old = std::getenv("USER");
                    had = old != nullptr;
                    if (had) saved = old;
                    put(std::string(60, 'u').c_str());
                }
                ~LongUser() { put(had ? saved.c_str() : ""); }
            } long_user;
            Application app;
            ASSERT_TRUE(start(app, {bot(1), bot(2), bot(3)}));
            ASSERT_EQ(app.sim().get_player_name(0), std::string(32, 'u'));
            app.hud().request_team_up(app.sim(), 1);
            ASSERT_TRUE(run_until(app, [&] { return app.sim().get_world_state().player_alliances[0] == 1; }, 400));
            for (int t = 0; t < 20; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(count_of(transcript(app), "are a team now!"), 1u);
            app.shutdown();
        }
        {   // a person's answer is the person's own business: seat 1 of a game with no bots says no, the status line carries the original's text and the chat log gets no line (although the rule would
            // have a reason for a bot: seat 1 has a teammate, so a bot there would have said "... already has a teammate.")
            Application app;
            ASSERT_TRUE(start(app, {}));
            ASSERT_TRUE(app.bots() == nullptr);
            app.sim().form_alliance(1, 2);
            ASSERT_TRUE(invite(app, 0, 1));
            Command deny;
            deny.type = CommandType::AllianceDeny;
            deny.issuer = 1;
            deny.other_player = 0;
            ASSERT_TRUE(app.sim().apply_command(deny).accepted());
            app.update_simulation(0.05f);
            ASSERT_EQ(app.hud().status_line().text(), std::string("Red rejected teaming up"));
            for (int t = 0; t < 40; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(notes_in(transcript(app)), 0u);
            app.shutdown();
        }
        {   // a bot seat answers a person's invitation to ANOTHER player's seat in no way: only the local player's own invitation is explained (a refusal that is told to somebody else is not this screen's news)
            Application app;
            ASSERT_TRUE(start(app, {bot(1), bot(2), bot(3)}));
            app.sim().apply_command([] { Command c; c.type = CommandType::AllianceInvite; c.issuer = 2; c.other_player = 3; return c; }());
            Command deny;                                                                                 // seat 3 (a bot seat, answered by hand) refuses seat 2: the news goes to seat 2, not to the local seat 0
            deny.type = CommandType::AllianceDeny;
            deny.issuer = 3;
            deny.other_player = 2;
            ASSERT_TRUE(app.sim().apply_command(deny).accepted());
            for (int t = 0; t < 10; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(notes_in(transcript(app)), 0u);
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("7.6f The teams of a game on this computer, as a model: --teams ffa | A+B (two different seats, any case of ffa, nothing else), what a match of a roster makes of them (the pair, then the two other seats when both play; nothing, with the reason, for a seat that does not play or when the teams would be the whole match), and the choices that a screen offers (only with two or more bots)") {
        LocalTeams t;
        std::string why;
        ASSERT_TRUE(parse_local_teams("0+1", t, why) && t.set && t.a == 0 && t.b == 1);
        ASSERT_TRUE(parse_local_teams("3+2", t, why) && t.set && t.a == 3 && t.b == 2);                    // the order is the player's: the first seat is the one that invites
        ASSERT_TRUE(parse_local_teams("ffa", t, why) && !t.set);
        ASSERT_TRUE(parse_local_teams("FFA", t, why) && !t.set && parse_local_teams("Ffa", t, why) && !t.set);
        for (const char* bad : {"", "0", "01", "0+", "+1", "0+0", "2+2", "0+4", "4+0", "9+1", "a+b", "0-1", "0 1", "0 + 1", " 0+1", "0+1 ", "0+1+2", "0+10", "ff", "ffaa", "free for all", "none", "-1+0"}) {
            t = LocalTeams{true, 1, 2};
            why.clear();
            ASSERT_FALSE(parse_local_teams(bad, t, why));
            ASSERT_TRUE(t.set && t.a == 1 && t.b == 2);                                                    // (nothing changed)
            ASSERT_FALSE(why.empty());
        }
        parse_local_teams("1+1", t, why);
        ASSERT_TRUE(why.find("two different seats") != std::string::npos);
        ASSERT_EQ(local_teams_text(LocalTeams{}), std::string("ffa"));
        ASSERT_EQ(local_teams_text(LocalTeams{true, 0, 1}), std::string("0+1"));
        ASSERT_EQ(local_teams_text(LocalTeams{true, 3, 2}), std::string("3+2"));
        ASSERT_TRUE(parse_local_teams(local_teams_text(LocalTeams{true, 2, 0}), t, why) && t == LocalTeams({true, 2, 0}));

        const auto pairs_of = [](const LocalTeams& teams, uint8_t roster) {
            std::string out;
            const LocalTeamsPlan plan = plan_local_teams(teams, roster);
            for (const auto& p : plan.pairs) out += std::to_string(p[0]) + "+" + std::to_string(p[1]) + " ";
            return out + "[" + plan.why + "]";
        };
        ASSERT_EQ(pairs_of(LocalTeams{}, 0x0F), std::string("[]"));                                          // free for all: nothing to make, nothing to say
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 1}, 0x0F), std::string("0+1 2+3 []"));                       // four seats: two teams
        ASSERT_EQ(pairs_of(LocalTeams{true, 1, 0}, 0x0F), std::string("1+0 2+3 []"));                       // (the first seat of the pair invites)
        ASSERT_EQ(pairs_of(LocalTeams{true, 2, 3}, 0x0F), std::string("2+3 0+1 []"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 1, 3}, 0x0F), std::string("1+3 0+2 []"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 1}, 0x07), std::string("0+1 []"));                           // three seats: one team, the third seat alone
        ASSERT_EQ(pairs_of(LocalTeams{true, 1, 2}, 0x07), std::string("1+2 []"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 3}, 0x0B), std::string("0+3 []"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 3}, 0x07), std::string("[seat 3 does not play in this match.]"));      // a seat that does not play: no team, the reason
        ASSERT_EQ(pairs_of(LocalTeams{true, 3, 0}, 0x07), std::string("[seat 3 does not play in this match.]"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 1, 2}, 0x0D), std::string("[seat 1 does not play in this match.]"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 1}, 0x03), std::string("[these are the only two seats that play, and a match in which every team is allied ends at once.]"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 2}, 0x05), std::string("[these are the only two seats that play, and a match in which every team is allied ends at once.]"));
        ASSERT_EQ(pairs_of(LocalTeams{true, 0, 1}, 0x00), std::string("[seat 0 does not play in this match.]"));

        const auto choices_of = [](uint8_t own, uint8_t filled) {
            std::string out;
            for (const LocalTeams& c : local_team_choices(own, filled)) out += local_teams_text(c) + " ";
            return out;
        };
        ASSERT_EQ(choices_of(0, 0x0E), std::string("ffa 0+1 0+2 0+3 "));                                    // three bots
        ASSERT_EQ(choices_of(0, 0x06), std::string("ffa 0+1 0+2 "));                                        // two bots
        ASSERT_EQ(choices_of(0, 0x0A), std::string("ffa 0+1 0+3 "));
        ASSERT_EQ(choices_of(0, 0x02), std::string("ffa "));                                                // one bot: the team would be the whole match, nothing to choose
        ASSERT_EQ(choices_of(0, 0x00), std::string("ffa "));
        ASSERT_EQ(choices_of(0, 0x0F), std::string("ffa 0+1 0+2 0+3 "));                                    // (the own seat's bit means nothing)
        ASSERT_EQ(choices_of(2, 0x0B), std::string("ffa 2+0 2+1 2+3 "));                                    // the player at another seat
        ASSERT_EQ(choices_of(2, 0x01), std::string("ffa "));
        ASSERT_EQ(choices_of(7, 0x0F), std::string("ffa "));
        for (uint8_t own = 0; own < 4; ++own) {                                                             // every choice that is offered can be made by a match of exactly those seats
            for (uint8_t filled = 0; filled < 16; ++filled) {
                const uint8_t roster = static_cast<uint8_t>((filled & 0x0Fu) | (1u << own));
                for (const LocalTeams& c : local_team_choices(own, filled)) {
                    ASSERT_TRUE(plan_local_teams(c, roster).why.empty());
                }
            }
        }
    } TEST_END();

    TEST_CASE("7.6g --teams in a local game: the teams are made with the original's own commands when the match starts (before its first tick, no invitation dialog, the engine's News Flash in the chat log), the standard bots keep them for the whole match, two bots make one team and a lone third seat, a seat that does not play or a match of two seats makes no team and says why (stderr, the setup screen), free for all makes none; the direct start of --map and the player's own seat too") {
        const std::string treasure = "Original-Ants/Maps/TREASURE.LVL";
        const auto bot = [](uint8_t seat) {
            ants::ai::BotSpec spec;
            spec.seat = seat;
            return spec;
        };
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        const auto allies_of = [](Application& app) {
            std::string out;
            for (uint8_t a : app.sim().get_world_state().player_alliances) out += std::to_string(a);
            return out;                                                                                    // by seat: the ally's seat, 4 for none
        };
        const auto config = [&](const std::vector<ants::ai::BotSpec>& bots, const LocalTeams& teams) {
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = treasure;
            cfg.play_at_once = true;
            cfg.player_name = "Alice";
            cfg.bots = bots;
            cfg.teams = teams;
            return cfg;
        };
        {   // three bots, --teams 0+1: the player and Red against Blue and Black
            Application app;
            ASSERT_TRUE(app.init(config({bot(1), bot(2), bot(3)}, LocalTeams{true, 0, 1})));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.hud().is_match_start_modal_active());                                          // the "Get ready" dialog is up and no tick has run
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_EQ(allies_of(app), std::string("1032"));                                                // 0 + 1 and 2 + 3, before the first tick
            ASSERT_TRUE(app.hud().alliance_dialog() == HUD::AllianceDialog::None);                         // the invitation was answered in the same breath: no dialog, nothing waits
            ASSERT_TRUE(app.sim().get_world_state().pending_invite_from[0] == 255 && app.sim().get_world_state().pending_invite_from[1] == 255);
            ASSERT_TRUE(app.sim().get_world_state().pending_invite_from[2] == 255 && app.sim().get_world_state().pending_invite_from[3] == 255);
            app.hud().dismiss_match_start_modal();
            app.update_simulation(0.05f);                                                                  // the first tick: the news of the engine reach the chat log
            ASSERT_TRUE(app.hud().alliance_dialog() == HUD::AllianceDialog::None);
            const std::string log = app.hud().chat_transcript(std::string());
            ASSERT_EQ(count_of(log, "are a team now!"), 2u);
            ASSERT_EQ(count_of(log, "Alice (Green) and Bot (Medium) (Red) are a team now!"), 1u);          // the proposer first, as the engine words it
            ASSERT_EQ(count_of(log, "Bot (Medium) (Blue) and Bot (Medium) (Black) are a team now!"), 1u);
            ASSERT_EQ(count_of(log, "rejected teaming up"), 0u);
            for (int t = 0; t < 3000; ++t) app.update_simulation(0.05f);                                   // the bots never break a team and never invite
            ASSERT_FALSE(app.sim().is_match_over());
            ASSERT_EQ(allies_of(app), std::string("1032"));
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 2u);
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "no longer a team"), 0u);
            ASSERT_TRUE(app.bots() != nullptr && app.bots()->stats(1).released > 0);                       // (the bots played)
            app.shutdown();
        }
        {   // the same game in 187 (--game-mode 187): the teams are made at the start there too, on the engine that has no food; the match goes on with them
            ApplicationConfig cfg = config({bot(1), bot(2), bot(3)}, LocalTeams{true, 0, 1});
            cfg.game_mode = 1;
            cfg.game_mode_given = true;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.sim().game_mode() == ants::sim::GameMode::Kills187);
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_EQ(allies_of(app), std::string("1032"));                                                // 0 + 1 and 2 + 3, before the first tick
            for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
            app.hud().dismiss_match_start_modal();
            app.update_simulation(0.05f);
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 2u);
            for (int t = 0; t < 200; ++t) app.update_simulation(0.05f);
            ASSERT_TRUE(app.sim().current_tick() > 100);
            ASSERT_TRUE(app.sim().game_mode() == ants::sim::GameMode::Kills187);
            ASSERT_EQ(allies_of(app), std::string("1032"));                                                // (the bots never break a team)
            app.shutdown();
        }
        {   // the first seat of the pair is the one that invites: 3+2 makes the same two teams, and the News Flash names Black first
            Application app;
            ASSERT_TRUE(app.init(config({bot(1), bot(2), bot(3)}, LocalTeams{true, 3, 2})));
            ASSERT_EQ(allies_of(app), std::string("1032"));
            app.hud().dismiss_match_start_modal();
            app.update_simulation(0.05f);
            const std::string log = app.hud().chat_transcript(std::string());
            ASSERT_EQ(count_of(log, "Bot (Medium) (Black) and Bot (Medium) (Blue) are a team now!"), 1u);
            ASSERT_EQ(count_of(log, "Alice (Green) and Bot (Medium) (Red) are a team now!"), 1u);
            app.shutdown();
        }
        {   // two bots (seat 3 empty): the player and Red are a team, Blue plays alone
            Application app;
            ASSERT_TRUE(app.init(config({bot(1), bot(2)}, LocalTeams{true, 0, 1})));
            ASSERT_EQ(app.sim().roster_mask(), 0x07);
            ASSERT_EQ(allies_of(app), std::string("1044"));
            app.hud().dismiss_match_start_modal();
            for (int t = 0; t < 400; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(allies_of(app), std::string("1044"));
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 1u);
            app.shutdown();
        }
        {   // a pair with a seat that does not play (the empty seat 3): no team, and the game says why on the setup screen's status line (and on stderr)
            Application app;
            ASSERT_TRUE(app.init(config({bot(1), bot(2)}, LocalTeams{true, 0, 3})));
            ASSERT_EQ(app.state(), AppState::Playing);                                                     // the game starts all the same
            ASSERT_EQ(allies_of(app), std::string("4444"));
            ASSERT_TRUE(app.map_select().room().status.find("--teams 0+3: seat 3 does not play in this match. The game starts without teams.") != std::string::npos);
            app.hud().dismiss_match_start_modal();
            for (int t = 0; t < 100; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(allies_of(app), std::string("4444"));
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 0u);
            app.shutdown();
        }
        {   // two seats only: the team would be the whole match (the original ends such a match at the first point): no team, the reason
            Application app;
            ASSERT_TRUE(app.init(config({bot(1)}, LocalTeams{true, 0, 1})));
            ASSERT_EQ(allies_of(app), std::string("4444"));
            ASSERT_TRUE(app.map_select().room().status.find("only two seats that play") != std::string::npos);
            app.shutdown();
        }
        {   // free for all (the default): nobody is a team, and the setup screen says nothing about it
            Application app;
            ASSERT_TRUE(app.init(config({bot(1), bot(2), bot(3)}, LocalTeams{})));
            ASSERT_EQ(allies_of(app), std::string("4444"));
            ASSERT_TRUE(app.map_select().room().status.find("--teams") == std::string::npos);
            app.hud().dismiss_match_start_modal();
            for (int t = 0; t < 50; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 0u);
            app.shutdown();
        }
        {   // the player at another seat (--player 2), bots at 0, 1 and 3, --teams 2+3: the same two teams
            Application app;
            ApplicationConfig cfg = config({bot(0), bot(1), bot(3)}, LocalTeams{true, 2, 3});
            cfg.local_player_id = 2;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(allies_of(app), std::string("1032"));
            app.shutdown();
        }
        {   // the direct start of --map (no setup screen, no dialog): the teams are there at tick 0 of the running game
            Application app;
            ApplicationConfig cfg = config({bot(1), bot(2), bot(3)}, LocalTeams{true, 0, 2});
            cfg.start_in_map_select = false;
            cfg.play_at_once = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_FALSE(app.hud().is_match_start_modal_active());
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_EQ(allies_of(app), std::string("2301"));                                                // 0 + 2 and 1 + 3
            ASSERT_TRUE(app.hud().alliance_dialog() == HUD::AllianceDialog::None);
            app.update_simulation(0.05f);
            ASSERT_EQ(count_of(app.hud().chat_transcript(std::string()), "are a team now!"), 2u);
            app.shutdown();
        }
        {   // a game without bots (the original's single player: the other colours stand still): the teams are made among the four seats, as asked
            Application app;
            ASSERT_TRUE(app.init(config({}, LocalTeams{true, 0, 1})));
            ASSERT_TRUE(app.bots() == nullptr);
            ASSERT_EQ(allies_of(app), std::string("1032"));
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("7.6i A game for one (--alone): only the local player's colony exists (a seat that nobody takes has no hill, no ants and no eggs, as with --bot), in the setup screen's way, in the direct start and at any seat; without the option the other three colonies stand there") {
        const std::string treasure = "Original-Ants/Maps/TREASURE.LVL";
        const auto config = [&](bool alone) {
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            cfg.default_map_path = treasure;
            cfg.play_at_once = true;
            cfg.player_name = "Alice";
            cfg.alone = alone;
            return cfg;
        };
        const auto ants_of = [](Application& app, uint8_t seat) {
            size_t n = 0;
            for (const ants::sim::AntSnapshot& ant : app.sim().get_world_state().ants) n += ant.player_id == seat ? 1u : 0u;
            return n;
        };
        const auto others_of = [&](Application& app, uint8_t own) {      // what the seats other than `own` have: ants, eggs, hills
            size_t n = 0;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (seat == own) continue;
                n += ants_of(app, seat) + app.sim().get_player_eggs(seat);
            }
            return n;
        };
        {   // without --alone (the original's single-player game): all four colonies, the other three stand still
            Application app;
            ASSERT_TRUE(app.init(config(false)));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{4});
            ASSERT_TRUE(others_of(app, 0) > 0);
            app.shutdown();
        }
        {   // --alone, from the setup screen's START (what the web page does): the player's colony only
            Application app;
            ASSERT_TRUE(app.init(config(true)));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.bots() == nullptr);                                                          // no bot, and no bot code
            ASSERT_EQ(app.sim().roster_mask(), 0x01);
            ASSERT_EQ(app.hud().roster_mask(), 0x01);                                                    // (one label, no "Red:", "Blue:", "Black:")
            ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{1});                           // one hill
            ASSERT_TRUE(ants_of(app, 0) > 0 && app.sim().get_player_eggs(0) > 0);                        // ... and the player's own ants and eggs
            ASSERT_EQ(others_of(app, 0), size_t{0});                                                     // nothing of the other three
            app.hud().dismiss_match_start_modal();
            for (int t = 0; t < 1200; ++t) app.update_simulation(0.05f);                                 // a minute of play: the match goes on, and no colony comes up
            ASSERT_FALSE(app.sim().is_match_over());
            ASSERT_EQ(others_of(app, 0), size_t{0});
            ASSERT_TRUE(ants_of(app, 0) > 0);
            app.shutdown();
        }
        {   // the player's seat is not Green (--player 2): the player's colony is Blue's, and only that
            Application app;
            ApplicationConfig cfg = config(true);
            cfg.local_player_id = 2;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.sim().roster_mask(), 0x04);
            ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{1});
            ASSERT_TRUE(ants_of(app, 2) > 0);
            ASSERT_EQ(others_of(app, 2), size_t{0});
            app.shutdown();
        }
        {   // the direct start of --map (no setup screen): the same game
            Application app;
            ApplicationConfig cfg = config(true);
            cfg.start_in_map_select = false;
            cfg.play_at_once = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().roster_mask(), 0x01);
            ASSERT_EQ(app.hud().roster_mask(), 0x01);
            ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{1});
            ASSERT_EQ(others_of(app, 0), size_t{0});
            for (int t = 0; t < 200; ++t) app.update_simulation(0.05f);
            ASSERT_EQ(others_of(app, 0), size_t{0});
            app.shutdown();
        }
        {   // a map of the players (the web page's START for one on any map of the community fetches the file and plays it like this): Green's colony alone, and the match goes on with nobody else on the map (it
            // ends with the clock or when this colony has nothing left, never because no other team is left), also on one of the shortest (every map of the players has the four seats since the 1 v 1 maps were taken out)
            for (const char* name : {"Grid.LVL", "Killing Spree.LVL", "SWIMTEAM.LVL", "Bombz Away.lvl"}) {
                Application app;
                ApplicationConfig cfg = config(true);
                cfg.start_in_map_select = false;
                cfg.play_at_once = false;
                cfg.default_map_path = std::string("Community-Maps/") + name;
                ASSERT_TRUE(app.init(cfg));
                ASSERT_EQ(app.state(), AppState::Playing);
                ASSERT_EQ(app.sim().roster_mask(), 0x01);
                ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{1});
                ASSERT_TRUE(ants_of(app, 0) > 0 && app.sim().get_player_eggs(0) > 0);
                ASSERT_EQ(others_of(app, 0), size_t{0});
                app.hud().dismiss_match_start_modal();
                for (int t = 0; t < 1200; ++t) app.update_simulation(0.05f);
                ASSERT_FALSE(app.sim().is_match_over());
                ASSERT_TRUE(ants_of(app, 0) > 0);
                app.shutdown();
            }
        }
        {   // --teams cannot be made with one seat: the game starts without teams and says why (the setup screen's status line)
            Application app;
            ApplicationConfig cfg = config(true);
            cfg.teams = LocalTeams{true, 0, 1};
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().roster_mask(), 0x01);
            ASSERT_EQ(app.sim().get_world_state().player_alliances[0], uint8_t{4});
            ASSERT_TRUE(app.map_select().room().status.find("does not play in this match") != std::string::npos);
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("7.6j A game that starts straight into its match (--map) asks the level about the teams that play, not about all four: the start marker of a seat that nobody takes does not refuse the map (--alone, or a roster of bots), the marker of a seat that plays does, and the original's four colonies refuse either") {
        const std::filesystem::path black_out = write_marker_outside_grid_map(152);     // the black team's start marker is outside the grid
        const std::filesystem::path red_out = write_marker_outside_grid_map(155);       // the red team's
        const std::filesystem::path green_out = write_marker_outside_grid_map(154);     // the green team's
        ASSERT_FALSE(black_out.empty());
        ASSERT_FALSE(red_out.empty());
        ASSERT_FALSE(green_out.empty());
        const auto config = [](const std::filesystem::path& map, bool alone, uint8_t bot_seats, uint8_t own) {      // bot_seats: the seats that have a bot (a mask)
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.default_map_path = map.string();
            cfg.alone = alone;
            cfg.local_player_id = own;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (((bot_seats >> seat) & 1u) == 0) continue;
                ants::ai::BotSpec spec;
                spec.seat = seat;
                spec.level = ants::ai::Level::Medium;
                cfg.bots.push_back(spec);
            }
            return cfg;
        };
        const auto refused = [&](const ApplicationConfig& cfg) {
            Application app;
            const bool started = app.init(cfg);
            app.shutdown();
            return !started;
        };
        ASSERT_TRUE(refused(config(black_out, false, 0, 0)));                         // the original's single player (four colonies stand there): the black marker refuses the map, as ever
        ASSERT_TRUE(refused(config(green_out, false, 0, 0)));
        {   // a game for one: Green alone plays, the black and the red markers are not looked at
            for (const std::filesystem::path& map : {black_out, red_out}) {
                Application app;
                ASSERT_TRUE(app.init(config(map, true, 0, 0)));
                ASSERT_EQ(app.state(), AppState::Playing);
                ASSERT_EQ(app.sim().roster_mask(), 0x01);
                ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{1});
                app.shutdown();
            }
        }
        ASSERT_TRUE(refused(config(green_out, true, 0, 0)));                          // ... the player's own marker is looked at
        {   // the player sits at Red (--player 1): Red's marker is the one that counts, and Green's is not looked at
            Application app;
            ASSERT_TRUE(app.init(config(green_out, true, 0, 1)));
            ASSERT_EQ(app.sim().roster_mask(), 0x02);
            app.shutdown();
        }
        ASSERT_TRUE(refused(config(red_out, true, 0, 1)));
        {   // bots at Red and Blue: three colonies play, Black's marker is not looked at (before: the map was refused for all four)
            Application app;
            ASSERT_TRUE(app.init(config(black_out, false, 0x06, 0)));
            ASSERT_EQ(app.sim().roster_mask(), 0x07);
            ASSERT_TRUE(app.bots() != nullptr);
            app.shutdown();
        }
        ASSERT_TRUE(refused(config(black_out, false, 0x0E, 0)));                      // ... a bot at the seat whose marker is outside: refused
        std::error_code ignore;
        std::filesystem::remove(black_out, ignore);
        std::filesystem::remove(red_out, ignore);
        std::filesystem::remove(green_out, ignore);
    } TEST_END();

    TEST_CASE("7.7 Cursor Simulated in Screen Middle On Game Start (No Unwanted Edge Panning)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true; // Start in map select screen
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // Pick map and start game
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);

        // Cursor simulated in middle of screen (320, 240) and has not yet moved
        ASSERT_EQ(app.mouse_screen_x(), 320);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.mouse_has_moved());

        // The "get ready" modal (at least 5 s) is a dialog: no edge scrolling while it is open (Ants.exe: a dialog gets all input)
        ASSERT_TRUE(app.hud().is_modal_open());
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());

        // Set camera away from boundaries using center_on so panning in any direction is unclamped
        app.renderer().camera().center_on(600, 600, app.sim().grid().width(), app.sim().grid().height());
        int32_t init_cam_x = app.renderer().camera().world_x;
        int32_t init_cam_y = app.renderer().camera().world_y;

        // Step camera updates without any mouse motion event
        for (int i = 0; i < 20; ++i) {
            app.handle_camera_panning(0.020f);
        }

        // Camera must NOT have panned (stays centered at 600, 600)
        ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
        ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);

        // Keyboard keys (W, S, Up, Down, Left, Right) must NOT pan the camera at all
        const int32_t pan_test_keys[] = { SDLK_w, SDLK_s, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, 'w', 's', 'a', 'd' };
        for (int32_t k : pan_test_keys) {
            SDL_KeyboardEvent key{};
            key.type = SDL_KEYDOWN;
            key.keysym.sym = k;
            app.handle_key_down(key);
            for (int i = 0; i < 5; ++i) {
                app.handle_camera_panning(0.020f);
            }
            ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
            ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);
        }

        // Simulate actual mouse motion into the 12 px band of the top-left corner (x=10, y=10): the arrow shows, but only the 5 px
        // inner strip scrolls (Ants.exe FUN_01026aa3), so the view stays
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 10;
        motion.y = 10;
        app.handle_mouse_motion(motion);

        ASSERT_TRUE(app.mouse_has_moved());
        ASSERT_EQ(app.mouse_screen_x(), 10);
        ASSERT_EQ(app.mouse_screen_y(), 10);
        for (int i = 0; i < 6; ++i) {
            app.handle_camera_panning(0.020f);
        }
        ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
        ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);

        // In the 5 px inner strip (x=2, y=2) the view scrolls at the 50 ms rate of the original's input task (two ticks = 100 ms)
        motion.x = 2;
        motion.y = 2;
        app.handle_mouse_motion(motion);
        for (int i = 0; i < 6; ++i) {
            app.handle_camera_panning(0.020f);
        }
        ASSERT_LT(app.renderer().camera().world_x, init_cam_x);
        ASSERT_LT(app.renderer().camera().world_y, init_cam_y);
    } TEST_END();

    TEST_CASE("7.8 A Pointer Beyond The Picture (What SDL Reports Over A Black Bar Of A Wide Window) Is The Pointer On The Edge Pixel: The Map Scrolls And The Game's Cursor Is Drawn There (On A One-Monitor Machine The Original's Exclusive 640 x 480 Mode Keeps Its Pointer On The Picture)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        app.hud().update(app.sim().get_world_state(), 100);                        // the get-ready dialog is gone: the edges scroll
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(rig.window, &win_w, &win_h);
        ASSERT_EQ(win_w, 1280);                                                    // the headless window: the picture at scale 2, no bars (SDL maps a window
        ASSERT_EQ(win_h, 960);                                                     // position to the picture: x / 2, y / 2)

        // far to the left of the picture (what SDL reports for a pointer over the left bar: a negative x): the view scrolls west, the cursor is drawn on x = 0
        rig.center();
        const int32_t x0 = rig.view_x();
        rig.motion(-4000, 480);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.pointer_outside());                                       // (render_frame draws the game's cursor unless the pointer is outside)
        ASSERT_LT(rig.view_x(), x0);
        // far to the right: east, the cursor on x = 639
        rig.center();
        const int32_t x1 = rig.view_x();
        rig.motion(40000, 480);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_x(), 639);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_GT(rig.view_x(), x1);
        // above and below the picture (a window that is taller than 4:3: bars at the top and bottom)
        rig.center();
        const int32_t y0 = rig.view_y();
        rig.motion(640, -4000);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_y(), 0);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_LT(rig.view_y(), y0);
        rig.center();
        const int32_t y1 = rig.view_y();
        rig.motion(640, 40000);
        rig.deliver();
        rig.scroll(12);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_GT(rig.view_y(), y1);
        // a button event beyond the picture lands on the edge too (here the press and the release are also beyond the window: see 7.8c for what the release does)
        rig.button(SDL_MOUSEBUTTONDOWN, -4000, 40000);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        rig.button(SDL_MOUSEBUTTONUP, -4000, 40000);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 479);
        // a pointer that is on the picture is left alone: window (300, 200) is picture (150, 100)
        rig.motion(300, 200);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 150);
        ASSERT_EQ(app.mouse_screen_y(), 100);
        ASSERT_FALSE(app.pointer_outside());
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8b The Setup Screen And The Quick Help Get The Same Clamped Pointer: The Clamp Is At The Top Of The Event Loop, Before Any Screen Takes The Event") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        for (int screen = 0; screen < 2; ++screen) {
            if (screen == 1) {
                app.finish_loading();                                              // the quick help (the stored option is on), as 9.14 reaches it
                ASSERT_EQ(app.state(), AppState::QuickHelp);
            }
            rig.motion(-4000, 40000);                                              // beyond the bottom left corner of the picture
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 0);
            ASSERT_EQ(app.mouse_screen_y(), 479);
            rig.motion(300, 200);
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 150);
            rig.button(SDL_MOUSEBUTTONDOWN, 40000, -4000);                         // beyond the top right corner
            rig.button(SDL_MOUSEBUTTONUP, 40000, -4000);
            rig.deliver();
            ASSERT_EQ(app.mouse_screen_x(), 639);
            ASSERT_EQ(app.mouse_screen_y(), 0);
            ASSERT_EQ(app.state(), screen == 0 ? AppState::MapSelect : AppState::QuickHelp);   // (the corner pixel is on no button)
        }
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8c A Button Released Outside The Window Ends The Pointer's Stay Although No LEAVE Comes (macOS After A Drag That Left The Window; The Web After Its LEAVE): The Map Does Not Scroll On Its Own. While The Button Is Held The Pointer Stays On The Edge; A Release Over A Black Bar Is A Release In The Window") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(rig.window, &win_w, &win_h);
        ASSERT_EQ(win_w, 1280);
        ASSERT_EQ(win_h, 960);

        // the press on the map, a drag out of the window to the right, the release out there; SDL on macOS sends no LEAVE after it (the capture of the drag
        // ends with the release)
        rig.motion(500, 400);                                                      // picture (250, 200): the map
        rig.button(SDL_MOUSEBUTTONDOWN, 500, 400);
        rig.motion(1000, 450, SDL_BUTTON_LMASK);
        rig.motion(1900, 480, SDL_BUTTON_LMASK);                                   // beyond the window's right edge: picture (950, 240)
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 639);                                      // while the button is held the pointer stays on the edge pixel, as the
        ASSERT_EQ(app.mouse_screen_y(), 240);                                      // one-monitor original's pointer on its screen
        ASSERT_FALSE(app.pointer_outside());
        rig.button(SDL_MOUSEBUTTONUP, 1900, 480);
        rig.deliver();
        ASSERT_TRUE(app.pointer_outside());                                        // the pointer has gone: no cursor, no hover, no edge scroll
        ASSERT_EQ(app.mouse_screen_x(), 639);                                      // (its last position is kept: the pointer is one global)
        rig.center();
        const int32_t x0 = rig.view_x();
        const int32_t y0 = rig.view_y();
        rig.scroll(100);                                                           // 2 s of the 50 ms input task
        ASSERT_EQ(rig.view_x(), x0);
        ASSERT_EQ(rig.view_y(), y0);
        rig.motion(600, 400);                                                      // the pointer comes back: its first motion in the window ends it
        rig.deliver();
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_EQ(app.mouse_screen_x(), 300);
        ASSERT_EQ(app.mouse_screen_y(), 200);

        // the web build: the pointer leaves the canvas while the button is held (a LEAVE: no capture there), the release comes afterwards and names no window
        // (SDL has no mouse focus then, so it did not map the position to the picture either)
        rig.motion(4, 240);                                                        // picture (2, 120): the west scroll strip
        rig.button(SDL_MOUSEBUTTONDOWN, 4, 240);
        rig.window_event(SDL_WINDOWEVENT_LEAVE);
        rig.button(SDL_MOUSEBUTTONUP, 4, 240, 0, 0);                               // window position (4, 240), windowID 0
        rig.deliver();
        ASSERT_TRUE(app.pointer_outside());
        rig.center();
        const int32_t x1 = rig.view_x();
        rig.scroll(100);
        ASSERT_EQ(rig.view_x(), x1);

        // a release over a black bar is a release in the window: the pointer stays there, on the edge pixel, and the map scrolls on
        SDL_SetWindowSize(rig.window, 1920, 960);                                  // a wide window: the picture 1280 x 960 in the middle, bars of 320 px
        rig.deliver();
        rig.motion(100, 480);                                                      // the left bar: picture (-110, 240), the pointer on (0, 240)
        rig.button(SDL_MOUSEBUTTONDOWN, 100, 480);
        rig.button(SDL_MOUSEBUTTONUP, 100, 480);
        rig.deliver();
        ASSERT_EQ(app.mouse_screen_x(), 0);
        ASSERT_EQ(app.mouse_screen_y(), 240);
        ASSERT_FALSE(app.pointer_outside());
        rig.center();
        const int32_t x2 = rig.view_x();
        rig.scroll(12);
        ASSERT_LT(rig.view_x(), x2);
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8d A Lifted Finger Leaves No Pointer Behind (Touch Arrives As Mouse Events With which = SDL_TOUCH_MOUSEID): A Tap On A Black Bar Or In The Edge Strip Does Not Keep The Map Scrolling; A Mouse Click In The Same Places Still Leaves The Pointer There") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().update(app.sim().get_world_state(), 100);
        ASSERT_FALSE(app.hud().is_modal_open());
        const PointerRig rig(app);
        ASSERT_TRUE(rig.window != nullptr);
        SDL_SetWindowSize(rig.window, 1920, 960);                                  // a wide window (a phone in landscape): bars of 320 px on both sides
        rig.deliver();
        auto tap = [&](int x, int y, Uint32 which) {                               // what SDL sends for a tap: a motion, the press, the release
            rig.motion(x, y, 0, which);
            rig.button(SDL_MOUSEBUTTONDOWN, x, y, which);
            rig.button(SDL_MOUSEBUTTONUP, x, y, which);
            rig.deliver();
        };
        // the places: the left bar (window x 100: the pointer on picture x 0) and the picture's own 5 px strip (window x 324: picture x 2)
        for (const int x : {100, 324}) {
            const int32_t px = x == 100 ? 0 : 2;
            // a finger: once it is lifted the pointer is gone and the view stays (without that rule it ran to the map's edge until the next touch)
            tap(x, 480, SDL_TOUCH_MOUSEID);
            ASSERT_EQ(app.mouse_screen_x(), px);
            ASSERT_EQ(app.mouse_screen_y(), 240);
            ASSERT_TRUE(app.pointer_outside());
            rig.center();
            const int32_t t0 = rig.view_x();
            rig.scroll(100);                                                       // 2 s
            ASSERT_EQ(rig.view_x(), t0);
            // the mouse: its pointer is still there, so the view scrolls west as before
            tap(x, 480, 0);
            ASSERT_EQ(app.mouse_screen_x(), px);
            ASSERT_FALSE(app.pointer_outside());
            rig.center();
            const int32_t m0 = rig.view_x();
            rig.scroll(12);
            ASSERT_LT(rig.view_x(), m0);
        }
        app.shutdown();
    } TEST_END();

    TEST_CASE("7.8e The Pointer Grab: --fullscreen Asks For It, A Window Does Not; Entering And Leaving Fullscreen Ask And Let Go; SDL Holds The Pointer While The Window Has The Focus, Lets Go For Another Window, Takes It Back With The Focus, And Not For A Window That Left Fullscreen Meanwhile (A Real Window Of SDL's Dummy Video Driver, No Match)") {
        // A real window (headless = false) without a screen: SDL's dummy video driver. The settings go to a file of this run (never the player's own), no
        // match starts (so nothing is written at the end) and the sound goes to SDL's dummy audio driver. What the game asks for is SDL's grab flag; what SDL
        // makes of it (SDL_GetWindowMouseGrab) needs the input focus. SDL 2.32's dummy driver gives a window that is shown the focus; SDL 2.26 (Debian 12) and
        // 2.28 give none, so SDL holds nothing there: the requests are checked everywhere, the focus part only where the driver moves the focus.
        const std::filesystem::path settings = temp_path_of_this_run("ants_grab_settings").replace_extension(".ini");
        auto open = [&](Application& app, bool fullscreen) {
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                            // (SDL_Quit clears the hints: set before every start)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg;
            cfg.headless = false;
            cfg.skip_intro = true;                                                 // the setup screen at once
            cfg.fullscreen = fullscreen;
            cfg.settings_path = settings.string();
            return app.init(cfg);
        };
        auto frame = [](Application& app) { app.run_frame_with_delta(0.001f); };
        auto asked = [](SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_MOUSE_GRABBED) != 0; };     // the game's request
        auto held = [](SDL_Window* w) { return SDL_GetWindowMouseGrab(w) == SDL_TRUE; };                      // SDL's grab
        auto focused = [](SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0; };
        {
            Application app;
            ASSERT_TRUE(open(app, true));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
            ASSERT_TRUE(asked(window));                                            // --fullscreen: asked for from the start
            ASSERT_EQ(held(window), focused(window));                              // ... and held while the window has the focus
            SDL_SetWindowFullscreen(window, 0);                                    // back to a window
            frame(app);
            ASSERT_FALSE(asked(window));
            ASSERT_FALSE(held(window));
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);        // and fullscreen again
            frame(app);
            ASSERT_TRUE(asked(window));
            ASSERT_EQ(held(window), focused(window));
            if (focused(window)) {                                                 // (a driver that moves the focus: SDL 2.32's dummy driver)
                auto focus_elsewhere = [&]() {
                    SDL_Window* other = SDL_CreateWindow("other", 0, 0, 64, 64, SDL_WINDOW_SHOWN);   // (the dummy driver gives a window that is shown the focus)
                    frame(app);
                    return other;
                };
                auto focus_back = [&](SDL_Window* other) {
                    SDL_DestroyWindow(other);
                    SDL_HideWindow(window);
                    SDL_ShowWindow(window);
                    frame(app);
                };
                // another window takes the focus: SDL lets go of the pointer, the request stands; the focus returns: SDL holds it again
                SDL_Window* other = focus_elsewhere();
                ASSERT_TRUE(other != nullptr);
                ASSERT_FALSE(focused(window));
                ASSERT_FALSE(held(window));
                ASSERT_TRUE(asked(window));
                focus_back(other);
                ASSERT_TRUE(focused(window));
                ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
                ASSERT_TRUE(held(window));
                // the window leaves fullscreen while another window has the focus: when the focus returns it is a plain window, and nothing holds the pointer
                other = focus_elsewhere();
                ASSERT_TRUE(other != nullptr);
                SDL_SetWindowFullscreen(window, 0);
                frame(app);
                ASSERT_FALSE(asked(window));
                focus_back(other);
                ASSERT_TRUE(focused(window));
                ASSERT_FALSE(held(window));
            } else {
                std::cout << "(7.8e: this SDL's dummy driver gives no window the focus: the focus part is not run) ";
            }
            app.shutdown();
        }
        {
            Application app;
            ASSERT_TRUE(open(app, false));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_FALSE(asked(window));                                           // a window: the pointer is free (four games on one screen, other programs)
            ASSERT_FALSE(held(window));
            frame(app);
            ASSERT_FALSE(asked(window));
            ASSERT_FALSE(held(window));
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            frame(app);
            ASSERT_TRUE(asked(window));
            ASSERT_EQ(held(window), focused(window));
            app.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    } TEST_END();

    TEST_CASE("7.8f A FULLSCREEN Window Whose Shape Is Not The Picture's Has Bars Inside The Window (SDL Maps A Position Over A Bar To One Outside The Picture): The Pointer Over A Bar Is On The Picture's Nearest Edge Pixel, Corners Included, And Is Not Gone; A Window Of Another Shape (4:3 Picture In A Wide Window) Does The Same At The Sides (A Real Fullscreen Window Of SDL's Dummy Video Driver, The 16:9 Canvas, The Setup Screen)") {
        // The same clamp as 7.8 .. 7.8d, but in a window that is FULLSCREEN (SDL_WINDOW_FULLSCREEN_DESKTOP, which is how a 16:10 screen shows the 16:9 picture: bars of 45 rows above and
        // below at 1440 x 900, of 66 at 1512 x 982): the clamp does not look at fullscreen, so the bars of a window and the bars of a fullscreen screen are the same rule. The dummy driver's
        // display is 4:3 (1024 x 768 with SDL 2.32), so a 16:9 canvas has bars above and below (96 rows) and the 4:3 canvas none; what the window really is decides the geometry (CanvasLayout::fit is
        // SDL's own arithmetic, pinned in test_canvas_layout), and a driver whose display happens to have the canvas's shape has no bars to test.
        const std::filesystem::path settings = temp_path_of_this_run("ants_bars_settings").replace_extension(".ini");
        auto open = [&](Application& app, Aspect aspect) {
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                            // (SDL_Quit clears the hints: set before every start)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg;
            cfg.headless = false;
            cfg.skip_intro = true;                                                 // the setup screen at once (no match: nothing is written at the end)
            cfg.fullscreen = true;
            cfg.aspect = aspect;
            cfg.aspect_given = true;
            cfg.settings_path = settings.string();
            return app.init(cfg);
        };
        {
            Application app;
            ASSERT_TRUE(open(app, Aspect::Wide16x9));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            ASSERT_TRUE((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
            app.run_frame_with_delta(0.001f);
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(window, &w, &h);
            const CanvasFit fit = app.canvas().fit(w, h);
            ASSERT_EQ(app.picture(), (LayoutRect{0, 0, 960, 540}));               // (the wide setup screen is the whole canvas)
            if (fit.bar_top() >= 4 && fit.bar_bottom() >= 4) {
                auto at = [&](int wx, int wy) {
                    SDL_Event e{};
                    e.type = SDL_MOUSEMOTION;
                    e.motion.windowID = SDL_GetWindowID(window);
                    e.motion.x = wx;
                    e.motion.y = wy;
                    SDL_PushEvent(&e);
                    app.run_frame_with_delta(0.001f);
                };
                const int mid_x = w / 2;
                const int mid_picture = static_cast<int>(static_cast<double>(mid_x - fit.viewport.x) / fit.scale);
                at(mid_x, fit.bar_top() / 2);                                      // in the middle of the top bar, level with the window's middle
                ASSERT_EQ(app.mouse_screen_y(), 0);
                ASSERT_TRUE(std::abs(app.mouse_screen_x() - mid_picture) <= 1);
                ASSERT_FALSE(app.pointer_outside());
                at(mid_x, fit.out_h - fit.bar_bottom() / 2);                       // the bottom bar
                ASSERT_EQ(app.mouse_screen_y(), 539);
                ASSERT_TRUE(std::abs(app.mouse_screen_x() - mid_picture) <= 1);
                ASSERT_FALSE(app.pointer_outside());
                at(0, 1);                                                          // the window's top left corner, over the top bar: the picture's first pixel
                ASSERT_TRUE(app.mouse_screen_x() <= 1 && app.mouse_screen_y() == 0);
                at(w - 1, h - 1);                                                  // the window's bottom right corner: the picture's last pixel
                ASSERT_EQ(app.mouse_screen_x(), 959);
                ASSERT_EQ(app.mouse_screen_y(), 539);
                ASSERT_FALSE(app.pointer_outside());
            } else {
                std::cout << "(7.8f: this driver's display has the 16:9 canvas's shape: no bars to test) ";
            }
            app.shutdown();
        }
        {                                                                          // the other way: the original's 4:3 picture in a wide window has its bars at the sides
            Application app;
            ASSERT_TRUE(open(app, Aspect::Classic4x3));
            SDL_Window* window = SDL_GetWindowFromID(1);
            ASSERT_TRUE(window != nullptr);
            app.run_frame_with_delta(0.001f);
            SDL_SetWindowFullscreen(window, 0);                                    // a window of the shape of a 16:10 screen (what the way out of fullscreen can give, and the shape that fullscreen has there)
            SDL_SetWindowSize(window, 1440, 900);
            app.run_frame_with_delta(0.001f);
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(window, &w, &h);
            const CanvasFit fit = app.canvas().fit(w, h);
            if (w == 1440 && h == 900 && fit.bar_left() > 4) {
                auto at = [&](int wx, int wy) {
                    SDL_Event e{};
                    e.type = SDL_MOUSEMOTION;
                    e.motion.windowID = SDL_GetWindowID(window);
                    e.motion.x = wx;
                    e.motion.y = wy;
                    SDL_PushEvent(&e);
                    app.run_frame_with_delta(0.001f);
                };
                ASSERT_EQ(fit.viewport.w, 1200);                                   // the picture 1200 x 900 (scale 1.875), bars of 120 columns at the sides
                ASSERT_EQ(fit.bar_left(), 120);
                at(40, 450);                                                       // the left bar, level with the middle
                ASSERT_EQ(app.mouse_screen_x(), 0);
                ASSERT_EQ(app.mouse_screen_y(), 240);
                ASSERT_FALSE(app.pointer_outside());
                at(1400, 450);                                                     // the right bar
                ASSERT_EQ(app.mouse_screen_x(), 639);
                ASSERT_EQ(app.mouse_screen_y(), 240);
                at(10, 899);                                                       // the bottom left corner of the window
                ASSERT_EQ(app.mouse_screen_x(), 0);
                ASSERT_EQ(app.mouse_screen_y(), 479);
            } else {
                std::cout << "(7.8f: this driver would not give the window a 16:10 shape: the pillarbox part is not run) ";
            }
            app.shutdown();
        }
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    } TEST_END();
}

// ============================================================================
// SUITE 8: Map Selection Screen (the original has no in-world health bar)
// ============================================================================
void run_suite_8_unit_health_and_map_select() {
    TEST_SUITE("Suite 8: Unit Health Display & Map Selection Screen");

    TEST_CASE("8.2 Setup Screen: The List Is Every .lvl Of The Maps Folder In The Byte Order Of The File Names (GAUNTLET First); Up / Down Step And Wrap; Enter, S And s Start; Q, q, X And x Leave; Nothing Else Does Anything (FUN_01013de7, FUN_01014076)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");

        const auto& maps = screen.get_maps();
        ASSERT_EQ(maps.size(), 6u);
        ASSERT_EQ(maps[0].filename, "GAUNTLET.LVL");                              // the first of the sorted names
        for (size_t i = 1; i < maps.size(); ++i) ASSERT_TRUE(maps[i - 1].filename < maps[i].filename);
        const char* order[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
        for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, order[i]);
        for (const auto& m : maps) {
            if (m.filename == "TREASURE.LVL") {
                ASSERT_EQ(m.width, 60u);
                ASSERT_EQ(m.height, 60u);
            }
        }

        // Initial selection: TREASURE.LVL, the last entry of this list (the one deliberate deviation of the screen, as requested: the original highlights the first entry; 8.9 has it)
        ASSERT_EQ(screen.get_selected_index(), 5);
        ASSERT_EQ(maps[5].filename, "TREASURE.LVL");
        ASSERT_FALSE(screen.get_selected_map_path().empty());
        // The rest of this test walks the list from its first entry, as the original's screen starts
        screen.set_selected_index(0);
        ASSERT_EQ(screen.get_selected_index(), 0);

        // Down: next map, Up: previous map, both wrap round
        screen.handle_key_down(SDLK_DOWN);
        ASSERT_EQ(screen.get_selected_index(), 1);
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), 0);
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), static_cast<int32_t>(maps.size() - 1));
        screen.handle_key_down(SDLK_DOWN);
        ASSERT_EQ(screen.get_selected_index(), 0);

        std::string started_map;
        int starts = 0;
        int leaves = 0;
        screen.set_on_start([&](const std::string& path) { started_map = path; ++starts; });
        screen.set_on_quit([&]() { ++leaves; });

        // The keys that the original's handler does not know do nothing: digits, Left / Right, Space, F, D, Esc, Tab, letters
        const SDL_Keycode dead[] = {SDLK_1, SDLK_4, SDLK_6, SDLK_LEFT, SDLK_RIGHT, SDLK_SPACE, SDLK_f, SDLK_d, SDLK_ESCAPE, SDLK_TAB, SDLK_a, SDLK_y, SDLK_n, SDLK_F1, SDLK_BACKSPACE};
        for (SDL_Keycode k : dead) screen.handle_key_down(k);
        ASSERT_EQ(screen.get_selected_index(), 0);
        ASSERT_EQ(starts, 0);
        ASSERT_EQ(leaves, 0);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // START: Enter, the keypad's Enter, S and s
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);                                          // SMALL.LVL
        for (SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) {
            started_map.clear();
            screen.handle_key_down(k);
            ASSERT_TRUE(started_map.find("SMALL.LVL") != std::string::npos);
        }
        ASSERT_EQ(starts, 3);
        // LEAVE: Q, q, X and x (SDL reports the letter's lower case key whatever the shift state)
        for (SDL_Keycode k : {SDLK_q, SDLK_x}) screen.handle_key_down(k);
        ASSERT_EQ(leaves, 2);
    } TEST_END();

    TEST_CASE("8.3 Setup Screen Buttons Are The Original's Button Class: The Press Captures (Pressed Art, Click Sound), The Release Acts, Leaving The Button Cancels For Good") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        screen.set_selected_index(0);                                               // (the screen starts on TREASURE.LVL, see 8.9; this test steps through the list from its first entry)
        std::vector<uint32_t> sounds;
        screen.set_on_play_sfx([&](uint32_t s) { sounds.push_back(s); });

        std::string launched_path;
        screen.set_on_start([&](const std::string& path) { launched_path = path; });

        const int32_t dx = MapSelectScreen::BTN_DOWN_X + 10;
        const int32_t dy = MapSelectScreen::BTN_DOWN_Y + 10;
        // the press alone changes nothing but plays the click of the pressed picture
        screen.handle_mouse_motion(dx, dy);
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 0);
        ASSERT_EQ(sounds.size(), 1u);
        ASSERT_EQ(sounds.back(), SoundID::ButtonClick);
        // the release inside runs the action: the second map
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        ASSERT_TRUE(launched_path.empty());                                         // stepping does not start

        // leaving the button while it is held cancels it for good: coming back before the release does not bring it back
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_motion(dx + 200, dy);
        screen.handle_mouse_motion(dx, dy);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // a release elsewhere does nothing either
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx + 200, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // a press elsewhere and a release on the button: no capture, no action
        screen.handle_mouse_down(dx + 200, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        // the right button does nothing
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_RIGHT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_RIGHT);
        ASSERT_EQ(screen.get_selected_index(), 1);

        // START: press and release on the button
        const int32_t sx = MapSelectScreen::BTN_START_X + 20;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 10;
        screen.handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(launched_path.empty());
        screen.handle_mouse_up(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(launched_path.find("ISLANDS.LVL") != std::string::npos);          // the second entry of the sorted list

        // the pressed art follows the state: hover, pressed, up again
        ASSERT_FALSE(screen.is_locked());
        screen.lock();                                                              // START ran in the application: the screen is locked (+0x130)
        ASSERT_TRUE(screen.is_locked());
        launched_path.clear();
        screen.handle_mouse_down(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(dx, dy, SDL_BUTTON_LEFT);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        screen.handle_mouse_up(sx, sy, SDL_BUTTON_LEFT);
        screen.handle_key_down(SDLK_RETURN);
        ASSERT_EQ(screen.get_selected_index(), 1);                                  // nothing moves, nothing starts twice
        ASSERT_TRUE(launched_path.empty());
        screen.enter();                                                             // a new screen is unlocked
        ASSERT_FALSE(screen.is_locked());
    } TEST_END();

    TEST_CASE("8.5 Setup Screen: The Fog Of War Pair Is Silent, Off By Default And Acts At The Release; There Are No Other Click Targets (Map Box, Description Box, Thumbs, Drop)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        const int32_t start_index = screen.get_selected_index();                    // (TREASURE.LVL, see 8.9: what matters here is that no click target below moves it)
        std::vector<uint32_t> sounds;
        screen.set_on_play_sfx([&](uint32_t s) { sounds.push_back(s); });
        int starts = 0;
        int leaves = 0;
        screen.set_on_start([&](const std::string&) { ++starts; });
        screen.set_on_quit([&]() { ++leaves; });

        // Default: Fog of War disabled
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // "On": the press does nothing (and plays nothing: d_on3 is silent), the release switches it on
        const int32_t on_x = MapSelectScreen::BTN_FOW_ON_X + 5;
        const int32_t on_y = MapSelectScreen::BTN_FOW_ON_Y + 5;
        screen.handle_mouse_motion(on_x, on_y);
        screen.handle_mouse_down(on_x, on_y, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_TRUE(sounds.empty());
        screen.handle_mouse_up(on_x, on_y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());

        // "Off" likewise
        const int32_t off_x = MapSelectScreen::BTN_FOW_OFF_X + 5;
        const int32_t off_y = MapSelectScreen::BTN_FOW_OFF_Y + 5;
        screen.handle_mouse_motion(off_x, off_y);
        screen.handle_mouse_down(off_x, off_y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());
        screen.handle_mouse_up(off_x, off_y, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_TRUE(sounds.empty());

        // A press that leaves the button before the release changes nothing
        screen.handle_mouse_down(on_x, on_y, SDL_BUTTON_LEFT);
        screen.handle_mouse_motion(on_x, on_y + 100);
        screen.handle_mouse_up(on_x, on_y + 100, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // The places that used to do something in the remake (the map name box, the description box, the thumbs, the Drop button) are not buttons of the original
        const int32_t places[][2] = {{100, 325}, {100, 390}, {550, 105}, {550, 155}, {550, 205}, {590, 200}, {38, 380}};
        for (const auto& pt : places) {
            screen.handle_mouse_motion(pt[0], pt[1]);
            screen.handle_mouse_down(pt[0], pt[1], SDL_BUTTON_LEFT);
            screen.handle_mouse_up(pt[0], pt[1], SDL_BUTTON_LEFT);
        }
        ASSERT_EQ(screen.get_selected_index(), start_index);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());
        ASSERT_EQ(starts, 0);
        ASSERT_EQ(leaves, 0);
        ASSERT_TRUE(sounds.empty());
    } TEST_END();

    TEST_CASE("8.7 Setup Screen: Every Label, Portrait And Thumb Appears With The Refresh, 500 ms After The Screen Was Created (FUN_010133ef's first run)") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.499f);
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.001f);
        ASSERT_TRUE(screen.refreshed());
        screen.enter();                                                             // a screen that is created again starts empty again
        ASSERT_FALSE(screen.refreshed());
        screen.update(0.5f);
        ASSERT_TRUE(screen.refreshed());
    } TEST_END();

    TEST_CASE("8.8 Setup Screen: The Map List Is Searched, Not Built In - Every .lvl / .LVL File Of The Folder (Spaces, Mixed Case), Nothing Else, In Byte Order; The Labels Come From The File's Header") {
        namespace fs = std::filesystem;
        struct TempDir {
            fs::path path;
            ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
        } dir;
        dir.path = temp_path_of_this_run("ants_map_list_test");
        std::error_code ec;
        fs::create_directories(dir.path / "inner", ec);
        const fs::path source = fs::path("Original-Ants/Maps/TINY.LVL");
        ASSERT_TRUE(fs::exists(source));
        for (const char* name : {"POPcOrN.lvl", "OCEAN.LVL", "Bombz Away.lvl", "zeta.Lvl", "Mid.LVL"}) {
            fs::copy_file(source, dir.path / name, fs::copy_options::overwrite_existing, ec);
            ASSERT_FALSE(static_cast<bool>(ec));
        }
        fs::copy_file(source, dir.path / "inner" / "Hidden.LVL", fs::copy_options::overwrite_existing, ec);     // a folder inside is not searched
        std::ofstream(dir.path / "readme.txt") << "not a map";
        std::ofstream(dir.path / "bad.lvl.bak") << "not a map either";

        MapSelectScreen screen;
        screen.init(dir.path.string());
        const auto& maps = screen.get_maps();
        const char* expected[] = {"Bombz Away.lvl", "Mid.LVL", "OCEAN.LVL", "POPcOrN.lvl", "zeta.Lvl"};        // the byte order of the names: capitals before small letters
        ASSERT_EQ(maps.size(), 5u);
        for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, expected[i]);
        ASSERT_EQ(maps[0].display_name, "Bombz Away");
        ASSERT_EQ(maps[3].display_name, "POPcOrN");
        for (const auto& m : maps) {                                                  // everything shown about a map is the file's own header (these are copies of TINY)
            ASSERT_EQ(m.description, "Tiny map with no PowerUps");
            ASSERT_EQ(m.minutes, 6u);
            ASSERT_EQ(m.width, 31u);
            ASSERT_EQ(m.height, 31u);
            ASSERT_TRUE(fs::exists(m.full_path));
        }
        // Down steps through the list in that order and the start gets the chosen file
        std::string started;
        screen.set_on_start([&](const std::string& path) { started = path; });
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_DOWN);
        screen.handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(started.find("POPcOrN.lvl") != std::string::npos);
        // a folder without maps has an empty list, and nothing starts
        MapSelectScreen empty;
        empty.init((dir.path / "inner" / "nothing").string());
        ASSERT_TRUE(empty.get_maps().empty());
        started.clear();
        empty.set_on_start([&](const std::string& path) { started = path; });
        empty.handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(started.empty());
        ASSERT_TRUE(empty.get_selected_map_path().empty());
    } TEST_END();

    TEST_CASE("8.9 Setup Screen: The List Highlights TREASURE.LVL When The Folder Holds It (Any Case), Else The First Entry; Only The Highlight Differs From The Original (The List, Its Order, Up / Down With The Wrap, START); A Room's Leader And Guest Show The Room's Map") {
        namespace fs = std::filesystem;
        struct TempDir {
            fs::path path;
            ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
        } dir;
        dir.path = temp_path_of_this_run("ants_map_default_test");
        std::error_code ec;
        fs::create_directories(dir.path, ec);
        const fs::path source = fs::path("Original-Ants/Maps/TINY.LVL");
        ASSERT_TRUE(fs::exists(source));
        const auto copies = [&](const std::string& folder_name, const std::vector<std::string>& names) {
            const fs::path folder = dir.path / folder_name;
            fs::create_directories(folder, ec);
            for (const std::string& name : names) fs::copy_file(source, folder / name, fs::copy_options::overwrite_existing, ec);
            return folder.string();
        };

        // 1. The six maps of the original: the list is the original's (byte order of the names, GAUNTLET first, TREASURE last) and TREASURE.LVL is the highlighted entry
        {
            MapSelectScreen screen;
            screen.init("Original-Ants/Maps");
            const auto& maps = screen.get_maps();
            const char* order[] = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
            ASSERT_EQ(maps.size(), 6u);
            for (size_t i = 0; i < maps.size(); ++i) ASSERT_EQ(maps[i].filename, order[i]);
            ASSERT_EQ(std::string(MapSelectScreen::DEFAULT_MAP_FILE), "TREASURE.LVL");
            ASSERT_EQ(screen.get_selected_index(), 5);
            ASSERT_EQ(maps[static_cast<size_t>(screen.get_selected_index())].filename, "TREASURE.LVL");
            ASSERT_TRUE(screen.get_selected_map_path().find("TREASURE.LVL") != std::string::npos);
            // a player who only presses START plays it (a local screen: Enter starts)
            std::string started;
            screen.set_on_start([&](const std::string& path) { started = path; });
            screen.handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(started.find("TREASURE.LVL") != std::string::npos);
            // everything else is the original's: Down from the last entry is the first (the wrap), Up goes back, nothing else moves the list
            screen.handle_key_down(SDLK_DOWN);
            ASSERT_EQ(screen.get_selected_index(), 0);
            screen.handle_key_down(SDLK_UP);
            ASSERT_EQ(screen.get_selected_index(), 5);
            screen.handle_key_down(SDLK_UP);
            ASSERT_EQ(screen.get_selected_index(), 4);                                // TINY.LVL: the entry in front of it
        }
        // 2. A folder without TREASURE.LVL: the first entry, as the original does (a name that only starts like it is not it)
        {
            MapSelectScreen screen;
            screen.init(copies("without", {"POPCORN.lvl", "OCEAN.LVL", "Mid.LVL", "TREASURE2.LVL", "TREASURE ISLAND.LVL"}));
            ASSERT_EQ(screen.get_maps().size(), 5u);
            ASSERT_EQ(screen.get_selected_index(), 0);
            ASSERT_EQ(screen.get_maps()[0].filename, "Mid.LVL");
        }
        // 3. A folder with its own spelling of the name: found whatever the case, highlighted where the list puts it (capitals sort before small letters: TREASURE2.LVL is in front of Treasure.lvl)
        {
            MapSelectScreen screen;
            screen.init(copies("mixed", {"ZULU.LVL", "Treasure.lvl", "TREASURE2.LVL", "ALPHA.LVL"}));
            ASSERT_EQ(screen.get_maps().size(), 4u);
            ASSERT_EQ(screen.get_maps()[2].filename, "Treasure.lvl");
            ASSERT_EQ(screen.get_selected_index(), 2);
            MapSelectScreen lower;
            lower.init(copies("lower", {"alpha.lvl", "treasure.lvl"}));
            ASSERT_EQ(lower.get_maps().size(), 2u);
            ASSERT_EQ(lower.get_selected_index(), 1);
        }
        // 4. No maps at all: nothing is highlighted and nothing starts (as before)
        {
            MapSelectScreen screen;
            screen.init((dir.path / "nothing").string());
            ASSERT_TRUE(screen.get_maps().empty());
            ASSERT_TRUE(screen.get_selected_map_path().empty());
        }
        // 5. A guest and the leader of a server's room show the room's map, never the highlight of the list: the application follows the room (follow_host_choice), and the leader's
        // Up / Down change nothing
        {
            MapSelectScreen guest;
            guest.init("Original-Ants/Maps");
            MapSelectScreen::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.my_seat = 1;
            view.seats[0] = {true, "Ana", MapSelectScreen::Thumb::Good};
            view.seats[1] = {true, "Ben", MapSelectScreen::Thumb::Good};
            view.map_file = "SMALL.LVL";
            guest.set_room(view);
            ASSERT_TRUE(guest.is_guest());
            ASSERT_TRUE(guest.follow_host_choice("SMALL.LVL", false));
            ASSERT_EQ(guest.get_maps()[static_cast<size_t>(guest.get_selected_index())].filename, "SMALL.LVL");
            MapSelectScreen leader;
            leader.init("Original-Ants/Maps");
            view.leader = true;
            view.my_seat = 0;
            leader.set_room(view);
            ASSERT_TRUE(leader.leads_server_room());
            ASSERT_TRUE(leader.follow_host_choice("SMALL.LVL", false));
            const int32_t room_index = leader.get_selected_index();
            ASSERT_EQ(leader.get_maps()[static_cast<size_t>(room_index)].filename, "SMALL.LVL");
            leader.handle_key_down(SDLK_DOWN);
            leader.handle_key_down(SDLK_UP);
            leader.handle_key_down(SDLK_UP);
            ASSERT_EQ(leader.get_selected_index(), room_index);
            ASSERT_FALSE(leader.follow_host_choice("NOSUCH.LVL", false));              // a room on a map that this machine does not have: the highlight is left where it was
            ASSERT_EQ(leader.get_selected_index(), room_index);
        }
    } TEST_END();

    TEST_CASE("8.4 INTRO.MID Lifecycle & In-Game Music") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true; // Test map select lifecycle
        ASSERT_TRUE(app.init(cfg));

        // Starts in MapSelect with INTRO.MID active
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // When starting game, state transitions to Playing and in-game MIDI track starts
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.midi_player().is_playing()); // In-game music shuffle is playing!

        // When returning to map select, MIDI reloads INTRO.MID and resumes looping
        app.return_to_map_select();
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.midi_player().is_playing());
    } TEST_END();

    TEST_CASE("8.6 The Tile Grid Overlay Is A Command Line Option Only: No Key Toggles It (the original has no such key)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // Default: Tile grid overlay is OFF
        ASSERT_FALSE(app.is_tile_grid_visible());

        // Ctrl+T, Cmd+T and F3 do nothing
        SDL_KeyboardEvent key{};
        key.type = SDL_KEYDOWN;
        key.keysym.sym = SDLK_t;
        key.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key);
        key.keysym.mod = KMOD_LGUI;
        app.handle_key_down(key);
        key.keysym.sym = SDLK_F3;
        key.keysym.mod = 0;
        app.handle_key_down(key);
        ASSERT_FALSE(app.is_tile_grid_visible());

        // --show-grid turns it on
        Application with_grid;
        ApplicationConfig grid_cfg = cfg;
        grid_cfg.show_tile_grid = true;
        ASSERT_TRUE(with_grid.init(grid_cfg));
        ASSERT_TRUE(with_grid.is_tile_grid_visible());
    } TEST_END();

    TEST_CASE("8.7 Anthill Selection and Hatch Action") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim = app.sim();
        auto& hud = app.hud();

        // 1. Select Home Base (Player 0)
        hud.select_base(0);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);

        // 2. Set score for Player 0 to 500
        sim.set_player_score(0, 500);
        uint32_t initial_eggs = sim.get_player_eggs(0);
        size_t initial_ants = sim.get_world_state().ants.size();

        // 3. Click Hatch button at (500, 165)
        ViewportCamera cam;
        bool down_res = hud.handle_mouse_down(500, 165, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_TRUE(down_res);
        hud.handle_mouse_up(500, 165, SDL_BUTTON_LEFT, sim, cam);

        // The click costs 200 points and one egg at once; the egg incubates for 8000 ms without any ant (HATCHTSK)
        ASSERT_EQ(sim.get_player_eggs(0), initial_eggs - 1);
        ASSERT_EQ(sim.get_player_score(0), 300);
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        ASSERT_EQ(sim.get_world_state().ants.size(), initial_ants);
        for (int t = 0; t < 165; ++t) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
        ASSERT_EQ(sim.get_world_state().ants.size(), initial_ants + 1);

        // 4. Click the Stop pedestal at (610, 195): it flashes, the mouse input is locked for 250 ms (5 ticks), then the selection is dropped
        bool stop_down = hud.handle_mouse_down(610, 195, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_TRUE(stop_down);
        hud.handle_mouse_up(610, 195, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        hud.update(sim.get_world_state(), 4);
        ASSERT_EQ(hud.get_selected_base_team_id(), 0);
        hud.update(sim.get_world_state(), 1);
        ASSERT_EQ(hud.get_selected_base_team_id(), -1);
    } TEST_END();

    TEST_CASE("8.8 Team Switching and the Start View (Ants.exe 0x100e458 - 0x100e4b1: Just Far Enough To Show The Square Around The Hill's Anchor Tile)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        cfg.default_map_path = "Original-Ants/Maps/GAUNTLET.LVL";                  // (a run without --map plays TREASURE.LVL now, see 8.9: the numbers below are GAUNTLET's, so it is named)
        ASSERT_TRUE(app.init(cfg));

        // Default starts as Player 0 (Green)
        ASSERT_EQ(app.local_player_id(), 0);

        // The start view is the original's (HUD constructor 0x100e458 - 0x100e4b1): the fresh view scrolls just far enough to show the square (-160, +192) around the
        // anchor tile of Team 0's hill, it does not centre it. On GAUNTLET (the first map of the original's list) green's anchor is (30, 8), so (726, 24)
        // (a centred view would be (772, 69)).
        const auto* base0 = app.sim().grid().find_anthill(0);
        ASSERT_TRUE(base0 != nullptr);
        ASSERT_EQ(base0->x + 1, 30);
        ASSERT_EQ(base0->y + 1, 8);
        const auto& cam = app.renderer().camera();
        ASSERT_EQ(cam.world_x, 726);
        ASSERT_EQ(cam.world_y, 24);
        int32_t expected_x = 726;
        int32_t expected_y = 24;

        // Ctrl+2 does nothing: the original has no key that switches the controlled team
        SDL_KeyboardEvent key_event{};
        key_event.type = SDL_KEYDOWN;
        key_event.keysym.sym = SDLK_2;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);
        ASSERT_EQ(app.local_player_id(), 0);

        // Switch to Team 1 (Red) through the setter (what the command line's -pnum= does)
        app.set_local_player(1);
        ASSERT_EQ(app.local_player_id(), 1);
        const auto* base1 = app.sim().grid().find_anthill(1);
        ASSERT_TRUE(base1 != nullptr);
        ASSERT_EQ(base1->x + 1, 7);                    // red's anchor tile on GAUNTLET is (7, 30): the square (80, 816) - (432, 1168) needs the view 728 px down, none right
        ASSERT_EQ(base1->y + 1, 30);
        expected_x = 0;
        expected_y = 728;
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Switch to Team 2 via direct setter
        app.set_local_player(2);
        ASSERT_EQ(app.local_player_id(), 2);
        const auto* base2 = app.sim().grid().find_anthill(2);
        ASSERT_TRUE(base2 != nullptr);
        ASSERT_EQ(base2->x + 1, 20);                   // blue's anchor tile is (20, 11): the square (496, 208) - (848, 560): 406 right, 120 down
        ASSERT_EQ(base2->y + 1, 11);
        expected_x = 406;
        expected_y = 120;
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Ctrl+Tab does nothing either
        key_event.keysym.sym = SDLK_TAB;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);
        ASSERT_EQ(app.local_player_id(), 2);
    } TEST_END();
}
