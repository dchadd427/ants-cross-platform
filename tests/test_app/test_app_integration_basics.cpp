// Application integration tests: suites 1 - 6 and 9 (software surface, camera, HUD and radar, audio mixer, MIDI player, scorecard, gameplay mechanics and options)
#include "app_integration_fixtures.hpp"

// ============================================================================
// SUITE 1: 640x480 Software Surface & Compositing
// ============================================================================
void run_suite_1_surface_compositing() {
    TEST_SUITE("Suite 1: 640x480 Software Surface & Pixel Compositing");

    TEST_CASE("1.1 Virtual 640x480 Framebuffer Allocation & Clearing") {
        constexpr uint32_t VIRTUAL_WIDTH = 640;
        constexpr uint32_t VIRTUAL_HEIGHT = 480;
        std::vector<uint32_t> framebuffer(VIRTUAL_WIDTH * VIRTUAL_HEIGHT, 0);

        ASSERT_EQ(framebuffer.size(), 640u * 480u);

        // Fill with magenta color key (0xFF00FF)
        uint32_t magenta_key = 0xFFFF00FF;
        std::fill(framebuffer.begin(), framebuffer.end(), magenta_key);
        ASSERT_EQ(framebuffer[0], magenta_key);
        ASSERT_EQ(framebuffer[640 * 480 - 1], magenta_key);
    } TEST_END();

    TEST_CASE("1.2 HUD Frame Border Rectangles & Clipping") {
        constexpr uint32_t VIRTUAL_WIDTH = 640;
        constexpr uint32_t VIRTUAL_HEIGHT = 480;
        std::vector<uint32_t> fb(VIRTUAL_WIDTH * VIRTUAL_HEIGHT, 0);

        // Helper to draw filled rect with boundary clipping
        auto draw_rect = [&](int rx, int ry, int rw, int rh, uint32_t color) {
            for (int y = ry; y < ry + rh; ++y) {
                if (y < 0 || y >= static_cast<int>(VIRTUAL_HEIGHT)) continue;
                for (int x = rx; x < rx + rw; ++x) {
                    if (x < 0 || x >= static_cast<int>(VIRTUAL_WIDTH)) continue;
                    fb[static_cast<size_t>(y) * VIRTUAL_WIDTH + static_cast<size_t>(x)] = color;
                }
            }
        };

        // Draw top bar: (0, 0, 640, 22)
        draw_rect(0, 0, 640, 22, 0xFF111111);
        // Draw left border: (0, 22, 17, 439)
        draw_rect(0, 22, 17, 439, 0xFF222222);
        // Draw playfield divider: (458, 22, 22, 439)
        draw_rect(458, 22, 22, 439, 0xFF333333);
        // Draw bottom bar: (17, 461, 623, 19)
        draw_rect(17, 461, 623, 19, 0xFF444444);

        ASSERT_EQ(fb[0], 0xFF111111);                      // Top bar origin
        ASSERT_EQ(fb[22 * 640 + 0], 0xFF222222);           // Left border
        ASSERT_EQ(fb[22 * 640 + 458], 0xFF333333);         // Divider
        ASSERT_EQ(fb[461 * 640 + 17], 0xFF444444);         // Bottom bar
        ASSERT_EQ(fb[100 * 640 + 100], 0u);                // Inside playfield (untouched)

        // Test clipping resilience: outside coordinates should not crash
        draw_rect(-50, -50, 100, 100, 0xFF999999);
        draw_rect(630, 470, 50, 50, 0xFF888888);
        ASSERT_EQ(fb[0], 0xFF999999);
        ASSERT_EQ(fb[479 * 640 + 639], 0xFF888888);
    } TEST_END();

    TEST_CASE("1.3 Authentic 4:3 Integer Scaling Matrix") {
        auto compute_integer_scale = [](int win_w, int win_h) -> int {
            int scale_x = win_w / 640;
            int scale_y = win_h / 480;
            return std::max(1, std::min(scale_x, scale_y));
        };

        ASSERT_EQ(compute_integer_scale(640, 480), 1);
        ASSERT_EQ(compute_integer_scale(1280, 960), 2);
        ASSERT_EQ(compute_integer_scale(1920, 1080), 2); // 1080p -> max 2x integer scale with pillarbox
        ASSERT_EQ(compute_integer_scale(1920, 1440), 3); // 3x scale
        ASSERT_EQ(compute_integer_scale(2560, 1440), 3); // 1440p -> max 3x integer scale
        ASSERT_EQ(compute_integer_scale(3840, 2160), 4); // 4K -> 4x scale
    } TEST_END();
}

// ============================================================================
// SUITE 2: Camera Viewport, Panning & Coordinate Transforms
// ============================================================================
void run_suite_2_camera_transforms() {
    TEST_SUITE("Suite 2: Camera Viewport, Panning & Coordinate Transforms");

    TEST_CASE("2.1 World-to-Screen and Screen-to-World Inversion Invariant") {
        ViewportCamera cam;
        cam.x = 200.0f;
        cam.y = 350.0f;

        int32_t test_wx = 350;
        int32_t test_wy = 500;

        int32_t sx = 0, sy = 0;
        cam.world_to_screen(test_wx, test_wy, sx, sy);
        ASSERT_EQ(sx, 16 + (350 - 200)); // 166: the view's origin is the screen pixel (16, 21) (Ants.exe 0x100a32b)
        ASSERT_EQ(sy, 21 + (500 - 350)); // 171

        int32_t back_wx = 0, back_wy = 0;
        bool in_bounds = cam.screen_to_world(sx, sy, back_wx, back_wy);
        ASSERT_TRUE(in_bounds);
        ASSERT_EQ(back_wx, test_wx);
        ASSERT_EQ(back_wy, test_wy);
    } TEST_END();

    TEST_CASE("2.2 Camera Boundary Clamping Across Small/Medium/Large Maps") {
        ViewportCamera cam;

        // 31x31 Map (TINY.LVL) -> 992 x 992 pixels
        cam.x = -100.0f;
        cam.y = -50.0f;
        cam.clamp_to_bounds(31, 31);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 0);
        ASSERT_EQ(static_cast<int32_t>(cam.y), 0);

        cam.x = 2000.0f;
        cam.y = 2000.0f;
        cam.clamp_to_bounds(31, 31);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 31 * 32 - 442); // 992 - 442 = 550: the view is 442 x 440
        ASSERT_EQ(static_cast<int32_t>(cam.y), 31 * 32 - 440); // 992 - 440 = 552

        // 60x60 Map (TREASURE.LVL) -> 1920 x 1920 pixels
        cam.x = 5000.0f;
        cam.y = 5000.0f;
        cam.clamp_to_bounds(60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 60 * 32 - 442); // 1920 - 442 = 1478
        ASSERT_EQ(static_cast<int32_t>(cam.y), 60 * 32 - 440); // 1920 - 440 = 1480
    } TEST_END();

    TEST_CASE("2.4 The Audio Listener Is The Centre Of The 442 x 440 View (Ants.exe 0x1030249: origin + 221 / + 220)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.renderer().camera().world_x = 100;
        app.renderer().camera().world_y = 200;
        app.update_simulation(0.05f);
        ASSERT_EQ(app.audio_mixer().listener_x(), 100 + 221);
        ASSERT_EQ(app.audio_mixer().listener_y(), 200 + 220);
    } TEST_END();

    TEST_CASE("2.3 Centering Viewport on Simulation Entity") {
        ViewportCamera cam;

        // Entity at tile (30, 30) -> center (30*32+16, 30*32+16) = (976, 976)
        cam.center_on(976, 976, 60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 976 - 442 / 2); // 976 - 221 = 755
        ASSERT_EQ(static_cast<int32_t>(cam.y), 976 - 440 / 2); // 976 - 220 = 756

        int32_t sx = 0, sy = 0;
        cam.world_to_screen(976, 976, sx, sy);
        // Entity should project directly to center of playfield
        ASSERT_EQ(sx, 16 + 442 / 2);
        ASSERT_EQ(sy, 21 + 440 / 2);
    } TEST_END();
}

// ============================================================================
// SUITE 3: HUD Element Layout, Radar Dots & Selection Card
// ============================================================================
void run_suite_3_hud_and_radar() {
    TEST_SUITE("Suite 3: HUD Element Layout, Radar Dots & Selection Card");

    TEST_CASE("3.1 Authentic Radar Coordinate Projection & Dot Placement") {
        constexpr int32_t RADAR_X = 480;
        constexpr int32_t RADAR_Y = 22;
        constexpr int32_t RADAR_W = 160;
        constexpr int32_t RADAR_H = 104;

        constexpr int32_t MAP_TILES = 60;

        auto tile_to_radar = [&](int32_t tx, int32_t ty, int32_t& rx, int32_t& ry) {
            rx = RADAR_X + (tx * RADAR_W) / MAP_TILES;
            ry = RADAR_Y + (ty * RADAR_H) / MAP_TILES;
        };

        int32_t rx = 0, ry = 0;
        tile_to_radar(0, 0, rx, ry);
        ASSERT_EQ(rx, 480);
        ASSERT_EQ(ry, 22);

        tile_to_radar(30, 30, rx, ry);
        ASSERT_EQ(rx, 480 + 80); // 560
        ASSERT_EQ(ry, 22 + 52);  // 74

        tile_to_radar(59, 59, rx, ry);
        ASSERT_GE(rx, 480);
        ASSERT_LT(rx, 480 + 160);
        ASSERT_GE(ry, 22);
        ASSERT_LT(ry, 22 + 104);
    } TEST_END();

    TEST_CASE("3.2 Radar Team Color Fidelity") {
        // Authentic team palette definitions:
        // Team 0 (Green): RGB(83, 147, 43)
        // Team 1 (Red):   RGB(251, 51, 91)
        // Team 2 (Blue):  RGB(119, 175, 239)
        // Team 3 (Black): RGB(79, 87, 111)
        uint32_t team_colors[4] = {
            0xFF2B9353, // Team 0 (Green)
            0xFF5B33FB, // Team 1 (Red)
            0xFFEFAF77, // Team 2 (Blue)
            0xFF6F574F  // Team 3 (Black)
        };

        ASSERT_NE(team_colors[0], team_colors[1]);
        ASSERT_NE(team_colors[1], team_colors[2]);
        ASSERT_NE(team_colors[2], team_colors[3]);
    } TEST_END();

    TEST_CASE("3.3 Selection Card Health Bar HP Segments") {
        auto compute_hp_color = [](int hp) -> uint32_t {
            if (hp >= 8) return 0xFF00FF00;      // Green (8-10 HP)
            if (hp >= 4) return 0xFF00FFFF;      // Yellow (4-7 HP)
            return 0xFF0000FF;                  // Red (1-3 HP)
        };

        ASSERT_EQ(compute_hp_color(10), 0xFF00FF00); // Full HP -> Green
        ASSERT_EQ(compute_hp_color(8), 0xFF00FF00);
        ASSERT_EQ(compute_hp_color(7), 0xFF00FFFF);  // Damaged -> Yellow
        ASSERT_EQ(compute_hp_color(4), 0xFF00FFFF);
        ASSERT_EQ(compute_hp_color(3), 0xFF0000FF);  // Critical -> Red
        ASSERT_EQ(compute_hp_color(1), 0xFF0000FF);
    } TEST_END();

    TEST_CASE("3.4 Hatch Button Cost Deduction & Egg Stock Logic") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);

        // Attempt hatch with sufficient score
        bool success = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(success);
        ASSERT_EQ(sim.get_player_score(0), 300); // 500 - 200 = 300
        ASSERT_EQ(sim.get_player_eggs(0), 4u);

        // Deduct remaining score so score < 200
        sim.set_player_score(0, 150);
        bool fail_no_funds = sim.hatch_ant(0, AntType::Worker);
        ASSERT_FALSE(fail_no_funds);
        ASSERT_EQ(sim.get_player_score(0), 150);
        ASSERT_EQ(sim.get_player_eggs(0), 4u);
    } TEST_END();

    TEST_CASE("3.5 Authentic Team HUD Palettes & Score Background Colors") {
        // Ground-truth COLORREF values from Ants.exe VA 0x100DA90..0x100DAD0
        // COLORREF 0x00bbggrr -> RGBA
        constexpr ColorRGBA SCORE_BG[4] = {
            {  7,  67,  47, 255}, // Team 0 (Green): COLORREF 0x002F4307
            {119,   0,   0, 255}, // Team 1 (Red):   COLORREF 0x00000077
            { 43,  39, 107, 255}, // Team 2 (Blue):  COLORREF 0x006B272B
            { 39,  39,  59, 255}  // Team 3 (Black): COLORREF 0x003B2727
        };

        ASSERT_EQ(SCORE_BG[0].r, 7);
        ASSERT_EQ(SCORE_BG[0].g, 67);
        ASSERT_EQ(SCORE_BG[0].b, 47);

        ASSERT_EQ(SCORE_BG[1].r, 119);
        ASSERT_EQ(SCORE_BG[1].g, 0);
        ASSERT_EQ(SCORE_BG[1].b, 0);

        ASSERT_EQ(SCORE_BG[2].r, 43);
        ASSERT_EQ(SCORE_BG[2].g, 39);
        ASSERT_EQ(SCORE_BG[2].b, 107);

        ASSERT_EQ(SCORE_BG[3].r, 39);
        ASSERT_EQ(SCORE_BG[3].g, 39);
        ASSERT_EQ(SCORE_BG[3].b, 59);

        // Sidebar backing fill colors (Authentic Index 10 from Ants.exe VA 0x100EA70 tables)
        constexpr ColorRGBA SIDEBAR_BG[4] = {
            { 43, 104,  95, 255}, // Team 0 (Green)
            {143,  35,  99, 255}, // Team 1 (Red)
            { 51,  87, 163, 255}, // Team 2 (Blue)
            { 87,  87,  91, 255}  // Team 3 (Black)
        };

        ASSERT_EQ(SIDEBAR_BG[0].r, 43);
        ASSERT_EQ(SIDEBAR_BG[0].g, 104);
        ASSERT_EQ(SIDEBAR_BG[0].b, 95);

        ASSERT_EQ(SIDEBAR_BG[1].r, 143);
        ASSERT_EQ(SIDEBAR_BG[1].g, 35);
        ASSERT_EQ(SIDEBAR_BG[1].b, 99);

        ASSERT_EQ(SIDEBAR_BG[2].r, 51);
        ASSERT_EQ(SIDEBAR_BG[2].g, 87);
        ASSERT_EQ(SIDEBAR_BG[2].b, 163);

        ASSERT_EQ(SIDEBAR_BG[3].r, 87);
        ASSERT_EQ(SIDEBAR_BG[3].g, 87);
        ASSERT_EQ(SIDEBAR_BG[3].b, 91);

        // Score slot rects from Ants.exe VA 0x10021B8..0x1002230
        struct SlotRect {
            int32_t label_left, label_right;
            int32_t box_left, box_right;
            int32_t top, bottom;
        };

        SlotRect top_slot = { 312, 399, 402, 455, 4, 17 };
        ASSERT_EQ(top_slot.box_right - top_slot.box_left + 1, 54);
        ASSERT_EQ(top_slot.bottom - top_slot.top + 1, 14);
        ASSERT_EQ(top_slot.box_left - top_slot.label_right, 3);

        SlotRect bot_slots[3] = {
            {   5, 101, 105, 158, 464, 477 },
            { 163, 251, 254, 307, 464, 477 },
            { 312, 399, 402, 455, 464, 477 }
        };

        for (int i = 0; i < 3; ++i) {
            ASSERT_EQ(bot_slots[i].box_right - bot_slots[i].box_left + 1, 54);
            ASSERT_EQ(bot_slots[i].bottom - bot_slots[i].top + 1, 14);
            ASSERT_GE(bot_slots[i].box_left, bot_slots[i].label_right);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 4: 32-Channel Audio Mixer & Spatial Audio
// ============================================================================
void run_suite_4_audio_mixer() {
    TEST_SUITE("Suite 4: 32-Channel Audio Mixer & Spatial Audio");

    AssetArchive archive;
    std::string chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
    bool loaded = archive.load_chd(chd_path);
    if (!loaded) {
        std::cout << "  SKIPPED: ants.chd not found at " << chd_path << "\n";
        return;
    }

    TEST_CASE("4.1 32-Channel Saturation Allocation") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        ASSERT_EQ(mixer.active_channel_count(), 0u);

        // Allocate 32 sounds
        for (uint32_t i = 0; i < 32; ++i) {
            int ch = mixer.play_sfx(0, 1.0f, 10, false);
            ASSERT_GE(ch, 0);
        }
        ASSERT_EQ(mixer.active_channel_count(), 32u);
    } TEST_END();

    TEST_CASE("4.2 Priority Channel Preemption (Sound 58 Preemption)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        // Fill all 32 channels with priority 10
        for (uint32_t i = 0; i < 32; ++i) {
            mixer.play_sfx(0, 1.0f, 10, false);
        }
        ASSERT_EQ(mixer.active_channel_count(), 32u);

        // Play lower priority sound (priority 5) -> should be dropped (-1)
        int dropped = mixer.play_sfx(4, 1.0f, 5, false);
        ASSERT_EQ(dropped, -1);
        ASSERT_EQ(mixer.active_channel_count(), 32u);

        // Play critical alarm siren Sound 58 (priority 200) -> must preempt a channel
        int preempted = mixer.play_sfx(58, 1.0f, 200, false);
        ASSERT_GE(preempted, 0);
        ASSERT_LT(preempted, 32);
        ASSERT_TRUE(mixer.is_channel_active(preempted));
    } TEST_END();

    TEST_CASE("4.3 The Sound Law Of The Original: Chebyshev Percent Over 2500 px, Pan On The Far Channel Only, DirectSound Hundredths Of A dB (0x102e8e4, 0x102d803)") {
        // the integer arithmetic of the binary
        ASSERT_EQ(AudioMixer::distance_percent(0, 0), 100);
        ASSERT_EQ(AudioMixer::distance_percent(50, 0), 98);
        ASSERT_EQ(AudioMixer::distance_percent(221, 0), 92);                       // trunc(22100 / 2500) = 8
        ASSERT_EQ(AudioMixer::distance_percent(300, 400), 84);                     // Chebyshev: the larger of the two distances
        ASSERT_EQ(AudioMixer::distance_percent(0, -1000), 60);
        ASSERT_EQ(AudioMixer::distance_percent(800, 0), 68);
        ASSERT_EQ(AudioMixer::distance_percent(2500, 2500), 0);
        ASSERT_EQ(AudioMixer::pan_centibels(0), 0);
        ASSERT_EQ(AudioMixer::pan_centibels(50), 50);                              // 25 per step of 25 px
        ASSERT_EQ(AudioMixer::pan_centibels(221), 200);
        ASSERT_EQ(AudioMixer::pan_centibels(-221), -200);
        ASSERT_EQ(AudioMixer::pan_centibels(2500), 2500);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 100), 0);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 92), -200);
        ASSERT_EQ(AudioMixer::attenuation_centibels(100, 0), -2500);
        ASSERT_EQ(AudioMixer::attenuation_centibels(50, 100), -1250);              // the option at 50: -12.5 dB for every sound, positional or not
        ASSERT_EQ(AudioMixer::attenuation_centibels(75, 100), -625);
        ASSERT_EQ(AudioMixer::attenuation_centibels(1, 100), -2475);
        ASSERT_EQ(AudioMixer::attenuation_centibels(0, 100), -10000);              // muted
        ASSERT_NEAR(AudioMixer::gain_from_centibels(-200), 0.7943f, 0.0005f);
        ASSERT_NEAR(AudioMixer::gain_from_centibels(-2500), 0.0562f, 0.0005f);
        ASSERT_EQ(AudioMixer::gain_from_centibels(-10000), 0.0f);

        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(500, 500);
        float vl = 0.0f, vr = 0.0f;

        mixer.calculate_spatial_pan(500, 500, 1.0f, vl, vr);                       // centre: no attenuation, no pan
        ASSERT_NEAR(vl, 1.0f, 0.001f);
        ASSERT_NEAR(vr, 1.0f, 0.001f);
        mixer.calculate_spatial_pan(721, 500, 1.0f, vl, vr);                       // 221 px to the right: -2 dB for both channels, the LEFT channel another -2 dB
        ASSERT_NEAR(vr, 0.794f, 0.001f);
        ASSERT_NEAR(vl, 0.631f, 0.001f);
        mixer.calculate_spatial_pan(279, 500, 1.0f, vl, vr);                       // the mirror image: the RIGHT channel is the far one
        ASSERT_NEAR(vl, 0.794f, 0.001f);
        ASSERT_NEAR(vr, 0.631f, 0.001f);
        mixer.calculate_spatial_pan(900, 500, 1.0f, vl, vr);                       // 400 px: -4 dB, far channel -8 dB
        ASSERT_NEAR(vr, 0.631f, 0.001f);
        ASSERT_NEAR(vl, 0.398f, 0.001f);
        mixer.calculate_spatial_pan(500, 1500, 1.0f, vl, vr);                      // 1000 px straight below: -10 dB, no pan: nothing is culled inside the map
        ASSERT_NEAR(vl, 0.316f, 0.001f);
        ASSERT_NEAR(vr, 0.316f, 0.001f);
        mixer.calculate_spatial_pan(1300, 500, 1.0f, vl, vr);                      // 800 px: -8 dB (the old law was silent here)
        ASSERT_NEAR(vr, 0.398f, 0.001f);
        ASSERT_NEAR(vl, 0.158f, 0.001f);
        mixer.calculate_spatial_pan(2500 + 500, 500, 1.0f, vl, vr);                // the edge of the radius: -25 dB, far channel -50 dB
        ASSERT_NEAR(vr, 0.0562f, 0.0005f);
        ASSERT_NEAR(vl, 0.0032f, 0.0005f);
    } TEST_END();

    TEST_CASE("4.3b The Sound Volume Option Enters Every Sound And Follows The Playing Ones; Positional Sounds Follow The Moving View (FUN_0102f777)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(500, 500);
        ASSERT_EQ(mixer.sound_volume(), 100);

        const int cue = mixer.play_sfx(SoundID::AntStop, 1.0f, 10, false);         // a cue: plain volume, only the option applies
        const int hit = mixer.play_spatial(SoundID::BombDetonate, 721, 500, 10);   // 221 px to the right of the view centre
        ASSERT_GE(cue, 0);
        ASSERT_GE(hit, 0);
        float l = 0.0f, r = 0.0f;
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_NEAR(l, 1.0f, 0.001f);
        ASSERT_NEAR(r, 1.0f, 0.001f);
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));
        ASSERT_NEAR(l, 0.631f, 0.001f);
        ASSERT_NEAR(r, 0.794f, 0.001f);

        mixer.set_sfx_volume(0.5f);                                                // the slider at 50: -12.5 dB for everything that plays, now
        ASSERT_EQ(mixer.sound_volume(), 50);
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_NEAR(l, 0.237f, 0.001f);
        ASSERT_NEAR(r, 0.237f, 0.001f);
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));                             // 25 * (50 * 92 / 100 - 100) = -1350: 0.211; the far channel another -2 dB
        ASSERT_NEAR(r, 0.2113f, 0.001f);
        ASSERT_NEAR(l, 0.1679f, 0.001f);

        mixer.set_listener_position(721, 500);                                     // the view moves: the source is at its centre
        ASSERT_TRUE(mixer.channel_volumes(hit, l, r));
        ASSERT_NEAR(l, 0.237f, 0.001f);
        ASSERT_NEAR(r, 0.237f, 0.001f);

        mixer.set_sfx_volume(0.0f);                                                // 0: muted, and nothing new occupies a channel
        ASSERT_TRUE(mixer.channel_volumes(cue, l, r));
        ASSERT_EQ(l, 0.0f);
        ASSERT_EQ(mixer.play_spatial(SoundID::BombDetonate, 721, 500, 10), -1);
    } TEST_END();

    TEST_CASE("4.3c Tracked Sounds: A Stop Event Cuts The Sounds Of Its Owner And Only Those (FUN_0102bdab)") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(400, 400);
        AudioEvent a{SoundID::MeleeAttack, 400, 400, 1, 255};
        a.owner = 7;
        AudioEvent a2{SoundID::FoodHarvest, 400, 410, 1, 255};                   // a second sound of the same sprite (the duplicates of a clip are all tracked)
        a2.owner = 7;
        AudioEvent b{SoundID::MeleeAttack, 420, 400, 1, 255};
        b.owner = 8;
        AudioEvent cue{SoundID::AntStop, 0, 0, 1, 255};                          // a cue: nobody owns it
        mixer.ingest_simulation_events({a, a2, b, cue}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 4u);

        AudioEvent stop;
        stop.sound_id = 0xFFFFFFFFu;
        stop.owner = 7;
        stop.stop = true;
        mixer.ingest_simulation_events({stop}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 2u);                             // the sprite 7's two sounds are cut, the sprite 8's and the cue play on
        mixer.ingest_simulation_events({stop}, 0);                               // nothing of it plays any more: nothing changes
        ASSERT_EQ(mixer.active_channel_count(), 2u);
        stop.owner = 0;
        mixer.ingest_simulation_events({stop}, 0);                               // owner 0 is nobody
        ASSERT_EQ(mixer.active_channel_count(), 2u);
        // events in order: a sound that is started and stopped within one tick does not play; a sound after a stop does
        AudioEvent again = a;
        stop.owner = 7;
        mixer.ingest_simulation_events({a, stop, again}, 0);
        ASSERT_EQ(mixer.active_channel_count(), 3u);
        mixer.stop_owner(8);
        ASSERT_EQ(mixer.active_channel_count(), 2u);
    } TEST_END();

    TEST_CASE("4.4 Ingestion of Simulation Audio Events & Targeted Filtering") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        std::vector<AudioEvent> events;
        // Broadcast sound: VictoryFanfare
        events.push_back(AudioEvent{SoundID::VictoryFanfare, 0, 0, 2, 255});
        // Targeted alarm: targeted to victim player 1
        events.push_back(AudioEvent{SoundID::BaseAlarmSiren, 100, 100, 2, 1});

        // Player 0 (not the victim) should only receive the broadcast sound
        mixer.ingest_simulation_events(events, 0);
        ASSERT_EQ(mixer.active_channel_count(), 1u);
        mixer.stop_all();

        // Player 1 (the victim) should receive both sounds
        mixer.ingest_simulation_events(events, 1);
        ASSERT_EQ(mixer.active_channel_count(), 2u);
    } TEST_END();

    TEST_CASE("4.5 Software PCM Mixing & Non-Zero Dynamic Waveform Output") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        // Play bombexp.wav (Sound 4, 22050 Hz) and scoreup.wav (Sound 87, 11025 Hz)
        mixer.play_sfx(4, 0.8f, 10, false);
        mixer.play_sfx(87, 0.8f, 10, false);

        // Render 1024 frames of 44.1kHz stereo
        auto pcm = mixer.render_frames(1024);
        ASSERT_EQ(pcm.size(), 1024u * 2u);

        int non_zero_count = 0;
        int16_t max_sample = 0;
        for (int16_t s : pcm) {
            if (s != 0) ++non_zero_count;
            if (std::abs(s) > max_sample) max_sample = static_cast<int16_t>(std::abs(s));
        }

        ASSERT_GT(non_zero_count, 1000);
        ASSERT_GT(max_sample, 5000);
    } TEST_END();

    TEST_CASE("4.5b The settings menu's Master volume and Mute: one gain on the whole mix, reached by a ramp, the two volumes of the options stay, and a muted sound goes on playing") {
        const auto render = [&](AudioMixer& m, size_t frames) {
            m.play_sfx(4, 0.8f, 10, false);
            return m.render_frames(frames);
        };
        AudioMixer plain;
        plain.set_headless_mode(true);
        plain.init(archive);
        const std::vector<int16_t> full = render(plain, 4096);
        AudioMixer half;
        half.set_headless_mode(true);
        half.init(archive);
        half.set_output_gain(0.5f);
        const std::vector<int16_t> halved = render(half, 4096);
        ASSERT_EQ(full.size(), halved.size());
        int16_t loud = 0;
        for (size_t i = 2048 * 2; i < full.size(); ++i) {                            // (past the ramp: 12 ms is about 530 frames)
            ASSERT_TRUE(std::abs(halved[i] - full[i] / 2) <= 2);
            loud = std::max<int16_t>(loud, static_cast<int16_t>(std::abs(full[i])));
        }
        ASSERT_GT(loud, 500);                                                        // (the sound was there to be halved)
        ASSERT_TRUE(std::abs(halved[0]) <= std::abs(full[0]) && std::abs(halved[2]) >= 0);
        ASSERT_EQ(half.sound_volume(), plain.sound_volume());                       // (the original's Sound Volume is not touched)
        ASSERT_NEAR(half.get_music_volume(), plain.get_music_volume(), 0.0001f);     // (nor the Music Volume)

        // the ramp: the first frames are not the new gain at once (no step in the wave)
        AudioMixer steep;
        steep.set_headless_mode(true);
        steep.init(archive);
        steep.set_output_gain(0.0f);
        const std::vector<int16_t> fading = render(steep, 2048);
        int on_the_ramp = 0;
        for (size_t f = 0; f < 400; ++f) {                                           // (the gain falls by 12 ms of the way a frame at a time: 1000 / (12 * 44100) per frame, the same wave scaled by it)
            const double gain = 1.0 - static_cast<double>(f + 1) * 1000.0 / (12.0 * 44100.0);
            const double want = full[f * 2] * gain;
            ASSERT_TRUE(std::abs(fading[f * 2] - want) <= 4.0 + 0.02 * std::abs(want));
            if (std::abs(full[f * 2]) > 200) ++on_the_ramp;
        }
        ASSERT_GT(on_the_ramp, 5);                                                   // (the wave was there to be scaled)
        for (size_t f = 1500; f < 2048; ++f) ASSERT_EQ(fading[f * 2], 0);            // (then it is silent)

        // a muted sound goes on playing: unmuted later, the mix is what the mixer that was never muted has at the same place
        AudioMixer muted;
        muted.set_headless_mode(true);
        muted.init(archive);
        muted.set_output_gain(0.0f);
        muted.play_sfx(4, 0.8f, 10, false);
        const std::vector<int16_t> silent = muted.render_frames(2048);
        for (size_t f = 1500; f < 2048; ++f) ASSERT_EQ(silent[f * 2], 0);
        muted.set_output_gain(1.0f);
        const std::vector<int16_t> back = muted.render_frames(2048);
        const std::vector<int16_t> reference = full;                                  // (4096 frames of the same sound, never muted)
        for (size_t f = 1500; f < 2048; ++f) ASSERT_TRUE(std::abs(back[f * 2] - reference[(2048 + f) * 2]) <= 2);
    } TEST_END();

    TEST_CASE("4.6 Native In-Engine MP3 Background Music Playback & Streaming Mixing") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        ASSERT_FALSE(mixer.is_music_playing());

        std::string intro_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.mp3";
        bool music_loaded = mixer.play_music(intro_path, true);
        ASSERT_TRUE(music_loaded);
        ASSERT_TRUE(mixer.is_music_playing());

        // Volume control
        mixer.set_music_volume(0.75f);
        ASSERT_NEAR(mixer.get_music_volume(), 0.75f, 0.01f);

        // Render frames with both SFX and MP3 music mixed
        mixer.play_sfx(4, 0.5f, 10, false); // bomb explosion
        auto pcm = mixer.render_frames(1024);
        ASSERT_EQ(pcm.size(), 1024u * 2u);

        int non_zero_count = 0;
        for (int16_t s : pcm) {
            if (s != 0) ++non_zero_count;
        }
        ASSERT_GT(non_zero_count, 500);

        // Pause and resume
        mixer.pause_music();
        ASSERT_FALSE(mixer.is_music_playing());
        mixer.resume_music();
        ASSERT_TRUE(mixer.is_music_playing());

        // Smooth fade out
        mixer.fade_out_music(0.5f);
        mixer.update_music(0.25f);
        ASSERT_TRUE(mixer.is_music_playing());
        mixer.update_music(0.30f); // Completed fade out
        ASSERT_FALSE(mixer.is_music_playing());

        // Stop music
        mixer.stop_music();
        ASSERT_FALSE(mixer.is_music_playing());
    } TEST_END();
}

// ============================================================================
// SUITE 5: AudioToolbox MIDI Lifecycle & Playback
// ============================================================================
void run_suite_5_midi_player() {
    TEST_SUITE("Suite 5: AudioToolbox MIDI Lifecycle & Playback");

    std::string midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";

    TEST_CASE("5.1 Loading INTRO.MID & Sequence Properties") {
        MidiPlayer player;
        player.set_headless_mode(false); // Test native AudioToolbox loading
        bool ok = player.load_file(midi_path);
        ASSERT_TRUE(ok);
        ASSERT_TRUE(player.is_loaded());
        ASSERT_EQ(player.get_track_count(), 38u);
        ASSERT_GT(player.get_duration(), 90.0); // ~96.01 beats
    } TEST_END();

    TEST_CASE("5.2 MIDI Playback Lifecycle & Volume Fading") {
        MidiPlayer player;
        player.load_file(midi_path);

        ASSERT_FALSE(player.is_playing());
        player.play(true);
        ASSERT_TRUE(player.is_playing());
        ASSERT_EQ(player.state(), MidiState::Playing);

        player.pause();
        ASSERT_FALSE(player.is_playing());
        ASSERT_EQ(player.state(), MidiState::Paused);

        player.resume();
        ASSERT_TRUE(player.is_playing());

        // Volume control
        player.set_volume(0.6f);
        ASSERT_NEAR(player.get_volume(), 0.6f, 0.01f);

        // Fade out over 0.5s
        player.fade_out(0.5f);
        player.update(0.25f);
        ASSERT_LT(player.get_volume(), 0.6f);

        player.update(0.30f); // Complete fade out
        ASSERT_EQ(player.get_volume(), 0.0f);
        ASSERT_FALSE(player.is_playing());

        player.stop();
        ASSERT_EQ(player.state(), MidiState::Stopped);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Scorecard Modal & Win/Loss Audio Routing
// ============================================================================
void run_suite_6_scorecard_and_audio_routing() {
    TEST_SUITE("Suite 6: Scorecard Modal & Win/Loss Audio Routing");

    TEST_CASE("6.1 Match End After 0:00 Simulation Freeze & Split Audio Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 999, 100); // 100 ms match (2 ticks to 0:00)

        sim.set_player_score(0, 350); // Winner
        sim.set_player_score(1, 150); // Loser

        // Tick to expiration: CHECKGO ends the match at its first run with a clock below 0 (within 200 ms after 0:00)
        sim.tick();
        sim.tick();
        ASSERT_FALSE(sim.is_match_over());
        ASSERT_TRUE(run_until_over(sim, 8) >= 1);

        ASSERT_TRUE(sim.is_match_over());
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);

        // The simulation queues no sting (it used to, and the results screen played its own on top: the sting was heard twice). The original plays ONE
        // cue per machine when the results rows are built (0x1015a4a); the screen's rule picks it: winner when the local team is the top row
        auto audio_events = sim.poll_audio_events();
        for (const auto& ev : audio_events) {
            ASSERT_TRUE(ev.sound_id != SoundID::VictoryFanfare);
            ASSERT_TRUE(ev.sound_id != SoundID::PlayerDefeat);
        }
        ScorecardModal winner_screen;
        winner_screen.show(sim.get_world_state().match_result, 0);
        ASSERT_EQ(winner_screen.get_audio_to_play(), 0u);                          // the cue plays when the rows are built, 250 ms after the screen opens
        winner_screen.update(0.25f);
        ASSERT_EQ(winner_screen.get_audio_to_play(), SoundID::VictoryFanfare);     // Winner hears Sound 56
        ScorecardModal loser_screen;
        loser_screen.show(sim.get_world_state().match_result, 1);
        loser_screen.update(0.25f);
        ASSERT_EQ(loser_screen.get_audio_to_play(), SoundID::PlayerDefeat);        // Loser hears Sound 42
    } TEST_END();

    TEST_CASE("6.2 Scorecard Modal 4 Discrete Tracked Statistics") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        sim.set_player_score(0, 420);
        sim.record_player_stat(0, StatType::FriendlyLost, 5);
        sim.record_player_stat(0, StatType::EnemyKilled, 14);
        sim.record_player_stat(0, StatType::NewHatched, 22);

        auto stats = sim.get_player_stats(0);
        ASSERT_EQ(sim.get_player_score(0), 420);        // Stat 1: Score
        ASSERT_EQ(stats.friendly_lost, 5u);            // Stat 2: Friendly Lost
        ASSERT_EQ(stats.enemy_killed, 14u);            // Stat 3: Enemy Killed
        ASSERT_EQ(stats.new_hatched, 22u);             // Stat 4: New Hatched

        // Verify ScorecardModal state matches
        ScorecardModal modal;
        modal.show(sim.get_world_state().match_result, 0);
        ASSERT_TRUE(modal.is_open());
        modal.update(0.25f);
        ASSERT_FALSE(modal.is_waiting());
        ASSERT_EQ(modal.get_audio_to_play(), SoundID::VictoryFanfare);
        ASSERT_EQ(modal.rows().size(), 4u);                                         // every team of the match has a row
        ASSERT_EQ(modal.rows()[0].first, 0);                                        // team 0 scored 420
        ASSERT_EQ(modal.rows()[0].numbers[0], "420");
        ASSERT_EQ(modal.rows()[0].numbers[1], "5");
        ASSERT_EQ(modal.rows()[0].numbers[2], "14");
        ASSERT_EQ(modal.rows()[0].numbers[3], "22");
        modal.clear_audio_to_play();
        ASSERT_EQ(modal.get_audio_to_play(), 0u);
        modal.hide();
        ASSERT_FALSE(modal.is_open());
    } TEST_END();

    TEST_CASE("6.3 Scorecard Modal Leave Game Closes Application / Triggers Quit") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        ScorecardModal modal;
        modal.show(sim.get_world_state().match_result, 0);
        ASSERT_TRUE(modal.is_open());

        bool quit_called = false;
        bool replay_called = false;
        modal.set_on_quit([&]() { quit_called = true; });
        modal.set_on_replay([&]() { replay_called = true; });

        // The Leave button is created with the rows (FUN_010155ac): while the screen waits for the scores it is not there
        modal.handle_mouse_motion(550, 20);
        ASSERT_FALSE(modal.is_quit_hovered());
        modal.handle_mouse_down(550, 20);
        modal.handle_mouse_up(550, 20);
        ASSERT_FALSE(quit_called);
        modal.update(0.25f);

        // Hover over Leave Game button at (525, 12)
        modal.handle_mouse_motion(550, 20);
        ASSERT_TRUE(modal.is_quit_hovered());

        // Mouse down on Leave Game button
        modal.handle_mouse_down(550, 20);
        ASSERT_TRUE(modal.is_quit_pressed());

        // Mouse up on Leave Game button
        modal.handle_mouse_up(550, 20);
        ASSERT_FALSE(modal.is_quit_pressed());
        ASSERT_TRUE(quit_called);
        ASSERT_FALSE(replay_called); // Must NOT trigger replay/restart

        // Verify with Application instance: clicking Leave Game on end screen calls quit()
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.is_running());

        // Trigger scorecard to show
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.match_running());                                   // (the page's Menu button asks before it leaves a running match: ants_match_running)
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_FALSE(app.match_running());                                  // the results are not a running match
        app.scorecard().update(0.25f);                                      // the rows (and the Leave button) appear after 250 ms

        // Click Leave Game button via Application input handler
        SDL_MouseButtonEvent down_ev{};
        down_ev.type = SDL_MOUSEBUTTONDOWN;
        down_ev.button = SDL_BUTTON_LEFT;
        down_ev.x = 550;
        down_ev.y = 20;
        app.handle_mouse_button(down_ev);

        SDL_MouseButtonEvent up_ev{};
        up_ev.type = SDL_MOUSEBUTTONUP;
        up_ev.button = SDL_BUTTON_LEFT;
        up_ev.x = 550;
        up_ev.y = 20;
        app.handle_mouse_button(up_ev);

        // Application must have received quit() and stopped running
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("6.3b Results Leave: The Left Button Only (FUN_01011206); A Pointer That Rests On It Hovers As Soon As The Rows Are There (The INPUT Task's Poll, FUN_0102653f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.note_pointer(550, 20);                                                // the pointer rests where the Leave button will be
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        app.update_results(0.1f);                                                 // waiting for the scores: no button yet
        ASSERT_FALSE(app.scorecard().is_quit_hovered());
        app.update_results(0.2f);                                                 // the rows, and the Leave button under the resting pointer
        ASSERT_TRUE(app.scorecard().is_quit_hovered());
        for (uint8_t b : {static_cast<uint8_t>(SDL_BUTTON_RIGHT), static_cast<uint8_t>(SDL_BUTTON_MIDDLE)}) {
            SDL_MouseButtonEvent ev{};
            ev.type = SDL_MOUSEBUTTONDOWN;
            ev.button = b;
            ev.x = 550;
            ev.y = 20;
            app.handle_mouse_button(ev);
            ASSERT_FALSE(app.scorecard().is_quit_pressed());                      // another button does not press it ...
            ev.type = SDL_MOUSEBUTTONUP;
            app.handle_mouse_button(ev);
            ASSERT_TRUE(app.is_running());                                        // ... and does not leave
        }
        SDL_MouseButtonEvent left{};
        left.type = SDL_MOUSEBUTTONDOWN;
        left.button = SDL_BUTTON_LEFT;
        left.x = 550;
        left.y = 20;
        app.handle_mouse_button(left);
        ASSERT_TRUE(app.scorecard().is_quit_pressed());
        left.type = SDL_MOUSEBUTTONUP;
        app.handle_mouse_button(left);
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("6.4 Quit Dialog Yes With Exactly One Other Side Left Ends The Match (FUN_0101453f): No Exit, The Results Open With The Quitter Last") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.local_player_id(), 0);
        app.sim().drop_player(2);
        app.sim().drop_player(3);
        ASSERT_FALSE(app.sim().is_match_over());                             // two enemies are left: team 0 and team 1
        ASSERT_EQ(app.sim().other_sides(0), 1u);
        app.sim().set_player_score(0, 900);                                  // the quitter's score does not help: its row is the last
        app.sim().set_player_score(1, 50);
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_TRUE(app.is_running());                                       // the quit is the end of the match: the results come first
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_EQ(app.sim().quitter(), 0);
        ASSERT_TRUE(app.scorecard().is_open());
        const auto rows = app.sim().get_world_state().match_result.rows(0);
        ASSERT_EQ(rows.size(), 2u);
        ASSERT_EQ(rows[0].first, 1);
        ASSERT_EQ(rows[1].first, 0);
        ASSERT_EQ(app.scorecard().get_audio_to_play(), 0u);                  // nothing is pending: the cue plays once, 250 ms after the results opened
    } TEST_END();

    TEST_CASE("6.5 Results Screen Keys (FUN_01015b17): Enter, C, Q And X Leave (Either Case) At Any Time, Even While It Waits; Esc And Every Other Key Do Nothing") {
        const SDL_Keycode leaving[] = {SDLK_RETURN, SDLK_KP_ENTER, SDLK_c, SDLK_q, SDLK_x};
        const SDL_Keycode staying[] = {SDLK_ESCAPE, SDLK_SPACE, SDLK_a, SDLK_y, SDLK_n, SDLK_l, SDLK_TAB, SDLK_F1, SDLK_BACKSPACE, SDLK_1, SDLK_UP};
        for (int rows_built = 0; rows_built < 2; ++rows_built) {
            for (SDL_Keycode sym : leaving) {
                for (uint16_t mod : {static_cast<uint16_t>(KMOD_NONE), static_cast<uint16_t>(KMOD_LSHIFT), static_cast<uint16_t>(KMOD_LCTRL)}) {
                    Application app;
                    ApplicationConfig cfg;
                    cfg.headless = true;
                    cfg.start_in_map_select = false;
                    ASSERT_TRUE(app.init(cfg));
                    app.scorecard().show(app.sim().get_world_state().match_result, 0);
                    if (rows_built != 0) app.scorecard().update(0.25f);
                    ASSERT_EQ(app.scorecard().is_waiting(), rows_built == 0);
                    SDL_KeyboardEvent key{};
                    key.type = SDL_KEYDOWN;
                    key.keysym.sym = sym;
                    key.keysym.mod = mod;
                    app.handle_key_down(key);
                    ASSERT_FALSE(app.is_running());                               // Leave = quit()
                }
            }
            for (SDL_Keycode sym : staying) {
                Application app;
                ApplicationConfig cfg;
                cfg.headless = true;
                cfg.start_in_map_select = false;
                ASSERT_TRUE(app.init(cfg));
                app.scorecard().show(app.sim().get_world_state().match_result, 0);
                if (rows_built != 0) app.scorecard().update(0.25f);
                SDL_KeyboardEvent key{};
                key.type = SDL_KEYDOWN;
                key.keysym.sym = sym;
                app.handle_key_down(key);
                ASSERT_TRUE(app.is_running());
                ASSERT_TRUE(app.scorecard().is_open());
                ASSERT_EQ(app.state(), AppState::Playing);                        // Esc does not send the player to the setup screen
            }
        }
    } TEST_END();

    TEST_CASE("6.6 The Results Cue Plays Once, 250 ms After The Screen Opens, When The Rows Appear (Not When The Match Ends)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        app.sim().drop_player(1);
        app.sim().drop_player(2);
        app.sim().drop_player(3);                                                // team 0 is alone: the drop-out decides the match
        ASSERT_TRUE(app.sim().is_match_over());
        app.audio_mixer().stop_all();
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        app.update_results(0.016f);                                              // the frame that notices the end opens the screen
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_TRUE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);                 // no cue yet
        for (int i = 0; i < 10; ++i) app.update_results(0.016f);                 // 176 ms in all
        ASSERT_TRUE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        for (int i = 0; i < 6; ++i) app.update_results(0.016f);                  // 272 ms
        ASSERT_FALSE(app.scorecard().is_waiting());
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 1u);                 // one cue: winner.wav
        for (int i = 0; i < 20; ++i) app.update_results(0.016f);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 1u);                 // still one: nothing plays it again
        ASSERT_EQ(app.scorecard().get_audio_to_play(), 0u);
    } TEST_END();
}


// ============================================================================
// Suite 9: Gameplay Mechanics, Food Schedules, Hangman Pathing & Options Dialog
// ============================================================================
void run_suite_9_gameplay_mechanics_and_options() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] Suite 9: Gameplay Mechanics, Hangman & Options\n"
              << "=======================================================\n";

    TEST_CASE("9.1 Food Objects of TREASURE.LVL: Harvest Takes One Unit Worth The Object's Points") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();
        auto& grid = sim_engine.grid_mut();

        // Every Block 2 entry of the level is a food object that shows its first stage at its anchor
        const auto& objs = grid.food_objects();
        ASSERT_TRUE(!objs.empty());
        for (const FoodObject& o : objs) {
            ASSERT_EQ(o.remaining, o.units);
            ASSERT_TRUE(o.units > 0);
        }

        // A pile with a free tile close to it (not a cell of any pile): a worker put there harvests it
        int32_t obj = -1;
        TileCoord start{-1, -1};
        for (size_t i = 0; i < objs.size() && obj < 0; ++i) {
            const TileCoord anchor{objs[i].col, objs[i].row};
            int32_t best = 99;
            for (int32_t dy = -3; dy <= 3; ++dy) {
                for (int32_t dx = -3; dx <= 3; ++dx) {
                    const TileCoord t{anchor.x + dx, anchor.y + dy};
                    if (!grid.in_bounds(t) || grid.food_object_at_cell(t) >= 0) continue;
                    if (!grid.get_cell(t).is_passable() || grid.get_cell(t).has_food()) continue;
                    const int32_t d = std::max(std::abs(dx), std::abs(dy));
                    if (d < best) { best = d; start = t; }
                }
            }
            if (best < 99) obj = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(obj >= 0);
        const FoodObject& pile = grid.food_objects()[static_cast<size_t>(obj)];
        const TileCoord anchor{pile.col, pile.row};
        const uint16_t units = pile.units;
        const uint16_t value = pile.value;
        ASSERT_TRUE(grid.food_object_at_cell(anchor) >= 0);

        // Order the worker onto the pile; it walks to the pile, plays the grab clip and carries the object's points
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, start);
        const auto& ant = sim_engine.get_unit(ant_id);
        ASSERT_FALSE(ant.is_holding());
        sim_engine.clear_news_events();
        sim_engine.issue_move_order(ant_id, anchor);
        ASSERT_TRUE(wait_ms(sim_engine, 20000, [&]() { return ant.is_holding(); }) >= 0);

        ASSERT_EQ(grid.food_objects()[static_cast<size_t>(obj)].remaining, units - 1);
        ASSERT_EQ(ant.carried_points, static_cast<uint32_t>(value));
        ASSERT_EQ(ant.harvest_origin, anchor);
        ASSERT_TRUE(sim_engine.has_news_event(0, 60));   // "Got Food!"
    } TEST_END();

    TEST_CASE("9.2 Anthill Hangman Pathing, Lifecycle & Sound 55") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/SMALL.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();
        const auto* base = sim_engine.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Spawn damaged worker carrying food on the waiting tile (bx - 1, by + 3)
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        auto& ant = sim_engine.get_unit(ant_id);
        ant.hp = 4; // Damaged
        ant.pick_up_food(1, 25);
        ant.harvest_origin = TileCoord{15, 15};

        // An order onto the hill is the enter order (order 2): the ant walks to the entrance tile (bx+1, by+1)
        sim_engine.issue_move_order(ant_id, TileCoord{bx + 1, by + 1});
        ASSERT_EQ(ant.orig_order, AntUnit::kOrderHome);

        // Tick movement until the enter clip starts on the entrance tile
        for (int i = 0; i < 90 && ant.state != UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }
        ASSERT_EQ(static_cast<uint16_t>(ant.state), static_cast<uint16_t>(UnitState::EnteringBase));
        ASSERT_EQ(ant.pos, (TileCoord{bx + 1, by + 1}));

        // Food and health change only when the clip is cleaned up: still carrying and wounded inside the clip
        ASSERT_TRUE(ant.is_holding());
        ASSERT_EQ(ant.hp, 4u);

        // The clip ends: the food scores, the ant is healed to full health and sets out for the food source
        for (int i = 0; i < 200 && ant.state == UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }
        ASSERT_FALSE(ant.is_holding());
        ASSERT_EQ(ant.hp, ant.max_hp);
        ASSERT_EQ(sim_engine.get_player_score(0), 25);
    } TEST_END();

    TEST_CASE("9.3 Friendly Ant Pathing Around Stationary Ant (No Pushing)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/SMALL.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();

        // Spawn stationary ant at (10, 8)
        uint32_t stationary_id = sim_engine.spawn_unit(0, AntType::Combat, TileCoord{10, 8});
        auto& stat_ant = sim_engine.get_unit(stationary_id);
        stat_ant.state = UnitState::Idle;

        // Spawn moving ant at (9, 8) heading to (11, 8)
        uint32_t mover_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{9, 8});
        sim_engine.issue_move_order(mover_id, TileCoord{11, 8});

        // Tick simulation
        for (int i = 0; i < 40; ++i) {
            sim_engine.tick();
        }

        // Stationary ant stays anchored at (10, 8) and is NOT pushed
        const auto& mover = sim_engine.get_unit(mover_id);
        ASSERT_EQ(stat_ant.pos.x, 10);
        ASSERT_EQ(stat_ant.pos.y, 8);
        ASSERT_EQ(mover.pos.x, 11);
        ASSERT_EQ(mover.pos.y, 8);
    } TEST_END();

    TEST_CASE("9.4 Options Menu Navigation, Sliders and Return Button") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();
        ViewportCamera cam;

        // Open options dialog
        ASSERT_FALSE(hud.is_options_open());
        hud.open_options();
        ASSERT_TRUE(hud.is_options_open());

        // Drag sound volume slider (x=280, y=185)
        hud.handle_mouse_down(280, 185, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(320, 185, sim_engine, cam);
        hud.handle_mouse_up(320, 185, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(hud.get_sound_volume(), 58);                       // the thumb stands for (320 - 211) 100 / 185
        ASSERT_EQ(app.audio_mixer().sound_volume(), 58);

        // Drag music volume slider (x=280, y=223)
        hud.handle_mouse_down(280, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(340, 223, sim_engine, cam);
        hud.handle_mouse_up(340, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(hud.get_music_volume(), 69);                       // (340 - 211) 100 / 185
        ASSERT_NEAR(app.midi_player().get_volume(), 0.69f, 0.001f);

        // Click Return to Game button at (355, 427, bounds 345..455, 423..455)
        hud.handle_mouse_down(380, 435, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(380, 435, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_FALSE(hud.is_options_open());
    } TEST_END();

    TEST_CASE("9.5 Frame Rate Counter and Tile Grid Visibility") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // FPS getter returns non-negative
        ASSERT_TRUE(app.get_current_fps() >= 0.0f);

        // The counter is the real rate, not the rate of a clamped frame: a window drawn four times a second reads 4 (every rate under ten frames a second read "10 FPS" when
        // the frame was cut at 100 ms for the local simulation), one drawn once a second reads 1; a frame counts for at most a second. (A headless application stops after ten
        // frames unless it is taking a screenshot far away: this one does.)
        Application timed;
        ApplicationConfig tcfg = cfg;
        tcfg.screenshot_path = "fps_counter_test.png";
        tcfg.screenshot_frames = 1000000;
        ASSERT_TRUE(timed.init(tcfg));
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(0.25f);
        ASSERT_TRUE(timed.get_current_fps() > 3.5f && timed.get_current_fps() < 4.5f);
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(1.0f);
        ASSERT_TRUE(timed.get_current_fps() > 0.9f && timed.get_current_fps() < 1.3f);
        for (int i = 0; i < 80; ++i) timed.run_frame_with_delta(5.0f);                   // (a frame of 5 s is counted as the second that the network gets of it)
        ASSERT_TRUE(timed.get_current_fps() > 0.9f && timed.get_current_fps() < 1.3f);
        for (int i = 0; i < 200; ++i) timed.run_frame_with_delta(1.0f / 60.0f);
        ASSERT_TRUE(timed.get_current_fps() > 58.0f && timed.get_current_fps() < 62.0f);

        // The tile grid is off unless --show-grid asks for it (test 8.6): no runtime toggle exists
        ASSERT_FALSE(app.is_tile_grid_visible());
    } TEST_END();

    TEST_CASE("9.6 Ant Type Movement and Ready Voice Confirmations") {
        // Verify authentic voice sound ID mapping across all 6 ant types
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 0), 17); // gantgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 1), 15); // gantcommand.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Swimmer, 0), 33); // brdggo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Fire, 0), 23); // firego.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 0), 29); // combgo2.wav (cue index 20 of the original)
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 1), 28); // combgo1.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Bomber, 0), 37); // bombgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Thief, 0), 19); // theifgo.wav

        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 0), 13); // gantorders.wav for rand() % 3 == 0 ...
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 1), 14); // ... gantrdy.wav for the two other values
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 2), 14);
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 3), 13);
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 0), 27); // combrdy1.wav
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 1), 26); // combrdy2.wav

        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Worker, 0), 16); // gantattack.wav
        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Combat, 0), 59); // combat1.wav
        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Combat, 1), 60); // combat2.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Worker), ants::sim::NoVoice);   // no special voice
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Combat), ants::sim::NoVoice);
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Bomber), 39); // bombdo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Swimmer), 35); // brdgdo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Fire), 25); // firedo.wav
        ASSERT_EQ(ants::sim::get_ability_voice_sound(ants::sim::AntType::Thief), 21); // theifdo.wav

        // Test HUD order triggering sfx callback
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();

        uint32_t last_sfx = 0;
        uint32_t last_voice = 0;                                               // the last sound that is not the pedestal click (89)
        hud.set_on_play_sfx([&](uint32_t sid) {
            last_sfx = sid;
            if (sid != SoundID::NavButtonClick) last_voice = sid;
        });

        // Select all friendly ants
        hud.select_all_friendly(sim_engine.get_world_state());
        ASSERT_TRUE(last_sfx == 14 || last_sfx == 13); // gantrdy or gantorders

        // Issue move order via right click on playfield
        ViewportCamera cam;
        hud.handle_mouse_down(100, 100, SDL_BUTTON_RIGHT, sim_engine, cam);
        hud.handle_mouse_up(100, 100, SDL_BUTTON_RIGHT, sim_engine, cam);     // the order is given at the release
        ASSERT_TRUE(last_voice == 17 || last_voice == 15); // gantgo or gantcommand
        ASSERT_EQ(last_sfx, SoundID::NavButtonClick);                          // the flashing move pedestal clicks after the voice (BTNPUSH)
    } TEST_END();

    TEST_CASE("9.6b The Click Of A Pressed Button Is Cut At Its Release (the raised clip replaces the pressed one: StopTracked); A Cue Is Not") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        const size_t before = app.audio_mixer().active_channel_count();         // (the start voice of the match may be playing: nothing mixes without a device)
        SDL_MouseButtonEvent down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button = SDL_BUTTON_LEFT;
        down.x = 490;                                                            // the Help button of the top bar
        down.y = 15;
        app.handle_mouse_button(down);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);        // buttonclick.wav (277 ms) of the pressed picture
        SDL_MouseButtonEvent up = down;
        up.type = SDL_MOUSEBUTTONUP;
        app.handle_mouse_button(up);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before);            // released after a moment: the click stops with the pressed picture
        // the other UI sounds and the cues are not tied to a release
        app.audio_mixer().play_sfx(SoundID::AntStop, 1.0f, 255);
        app.handle_mouse_button(up);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);
    } TEST_END();

    // ---- the music of the original: one MCI sequencer device (0x100e627 .. 0x100e8cc). The intro plays once; every piece that ends is followed by a random in-game piece (never the one
    // before), also on the setup screen; a match start, the activation of the program and the release of the music slider start a random piece; the deactivation of the program and the
    // end of a match close the device, and a closed device is not started by anything else ---------------------------------------------------------------------------------------
    TEST_CASE("9.8 Music: The Intro Plays Once, Then Random In-Game Pieces Follow, Never The Same Twice In A Row, Also On The Setup Screen (MM_MCINOTIFY 0x100e8cc)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto ends_with = [](const std::string& s, const char* tail) { const std::string t(tail); return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; };
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(ends_with(mixer.music_filepath(), "INTRO.mp3"));
        ASSERT_FALSE(mixer.music_loops());                                       // `play AntsMidi from 0 notify`: once
        std::string previous;
        for (int i = 0; i < 40; ++i) {
            mixer.stop_music();                                                  // the piece ends (nothing advances a stream without an audio device)
            app.midi_player().stop();
            app.update_music(0.016f);
            ASSERT_TRUE(mixer.is_music_playing());
            const std::string now = mixer.music_filepath();
            ASSERT_TRUE(ends_with(now, "ANTS2A.mp3") || ends_with(now, "ANTS2B.mp3") || ends_with(now, "ANTSFUN3.mp3"));
            ASSERT_FALSE(mixer.music_loops());
            if (!previous.empty()) ASSERT_TRUE(now != previous);
            previous = now;
        }
        ASSERT_EQ(static_cast<int>(app.state()), static_cast<int>(AppState::MapSelect));   // the setup screen all the time
    } TEST_END();

    TEST_CASE("9.9 Music: Deactivating The Program Closes It, Activating It Starts A New Random Piece; A Closed Music Is Not Started By The Activation (0x100e875 .. 0x100e8bc)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        app.set_app_active(false);                                                // WM_ACTIVATEAPP(false): `close AntsMidi`
        ASSERT_FALSE(mixer.is_music_playing());
        app.update_music(0.016f);
        ASSERT_FALSE(mixer.is_music_playing());                                   // the closed device does not play on
        app.set_app_active(true);                                                 // a NEW random piece, not the intro again
        ASSERT_TRUE(mixer.is_music_playing());
        const std::string piece = mixer.music_filepath();
        ASSERT_TRUE(piece.find("INTRO") == std::string::npos);
        // a second deactivation / activation pair: again a new piece
        app.set_app_active(false);
        app.set_app_active(false);                                                // (a repeated event changes nothing)
        ASSERT_FALSE(mixer.is_music_playing());
        app.set_app_active(true);
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
    } TEST_END();

    TEST_CASE("9.9b Music With --audio-focus (Several Games On One Machine): A Window Without The Focus HOLDS Its Music, The Focus Continues The Same Piece; A Piece That Starts Meanwhile Starts Held") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.audio_follows_focus = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.is_music_playing());
        const std::string intro = mixer.music_filepath();
        ASSERT_TRUE(intro.find("INTRO") != std::string::npos);
        app.set_app_active(false);                                                // the focus goes to another game: its music is paused, not closed
        ASSERT_FALSE(mixer.is_music_playing());
        for (int i = 0; i < 20; ++i) app.update_music(0.016f);                    // a held piece has not ended: no new piece starts
        ASSERT_TRUE(mixer.music_filepath() == intro);
        app.set_app_active(true);                                                 // the focus is back: the SAME piece goes on (the original starts a new random one)
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() == intro);
        // switching back and forth any number of times never changes the piece
        for (int i = 0; i < 10; ++i) {
            app.set_app_active(false);
            ASSERT_FALSE(mixer.is_music_playing());
            app.set_app_active(true);
            ASSERT_TRUE(mixer.is_music_playing());
            ASSERT_TRUE(mixer.music_filepath() == intro);
        }
        // a match that starts while the window is behind the others: its piece is there, but silent until the window gets the focus
        app.set_app_active(false);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TREASURE.LVL"));
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);
        const std::string piece = mixer.music_filepath();
        ASSERT_FALSE(mixer.is_music_playing());
        app.update_music(0.016f);
        ASSERT_TRUE(mixer.music_filepath() == piece);
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() == piece);
        // the end of the piece still starts the next one (with the focus)
        mixer.stop_music();
        app.midi_player().stop();
        app.update_music(0.016f);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath() != piece);
        // the setup screen's intro, started while the window is behind the others, is held too, and the focus plays it
        app.set_app_active(false);
        app.return_to_map_select();
        ASSERT_EQ(static_cast<int>(app.state()), static_cast<int>(AppState::MapSelect));
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
        ASSERT_FALSE(mixer.is_music_playing());
        app.set_app_active(true);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
    } TEST_END();

    TEST_CASE("9.10 Music: The Release Of The Music Slider Starts A New Random Piece (0x100e714, 0x100e795); The Sound Slider Does Not") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);
        ViewportCamera cam;
        app.hud().open_options();
        app.hud().handle_mouse_down(300, 185, 1, app.sim(), cam);                 // the Sound slider
        app.hud().handle_mouse_up(300, 185, 1, app.sim(), cam);
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // still the intro
        app.hud().handle_mouse_down(300, 222, 1, app.sim(), cam);                 // the Music slider
        app.hud().handle_mouse_motion(330, 222, app.sim(), cam);
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // dragging changes nothing
        app.hud().handle_mouse_up(330, 222, 1, app.sim(), cam);
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);   // the running piece was closed and a random in-game piece started
        ASSERT_EQ(app.hud().get_music_volume(), 64);                              // (330 - 211) 100 / 185
        ASSERT_NEAR(mixer.get_music_volume(), 0.64f, 0.001f);
    } TEST_END();

    TEST_CASE("9.11 Music: A Match Start Starts A Random Piece; The End Of The Match Closes The Music At Once And Nothing Starts It Again (0x1022714)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        auto& mixer = app.audio_mixer();
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") != std::string::npos);   // the setup screen plays the intro
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TREASURE.LVL"));           // a match start: a random in-game piece replaces it
        ASSERT_TRUE(mixer.is_music_playing());
        ASSERT_TRUE(mixer.music_filepath().find("INTRO") == std::string::npos);
        app.sim().init_test_world(60, 60, 5, 100);                                // a match of 100 ms
        for (int i = 0; i < 100 + 12 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);      // (the first 100 steps are the "Get ready" dialog, in which the simulation waits)
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_FALSE(mixer.is_music_playing());                                   // closed at once, not faded out over a second
        app.update_music(0.016f);
        ASSERT_FALSE(mixer.is_music_playing());                                   // nothing restarts it
        app.set_app_active(false);
        app.set_app_active(true);
        ASSERT_FALSE(mixer.is_music_playing());                                   // nor does the activation of the program: the device was already closed
    } TEST_END();

    TEST_CASE("9.12 Options: Every Setting Is Written When Its Callback Runs And The Next Start Reads It (FUN_0100c20c, FUN_0100a2c9)") {
        const std::filesystem::path dir = temp_path_of_this_run("ants_test_settings");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const std::string file = (dir / "settings.ini").string();
        ViewportCamera cam;
        {
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.settings_path = file;
            ASSERT_TRUE(app.init(cfg));
            auto& hud = app.hud();
            ASSERT_EQ(hud.get_sound_volume(), 100);                   // nothing stored yet: the defaults
            ASSERT_EQ(hud.get_music_volume(), 65);
            ASSERT_EQ(hud.get_scroll_speed(), 50);
            hud.open_options();
            auto drag = [&](int32_t y, int32_t x_to) {
                hud.handle_mouse_down(300, y, SDL_BUTTON_LEFT, app.sim(), cam);
                hud.handle_mouse_motion(x_to, y, app.sim(), cam);
                hud.handle_mouse_up(x_to, y, SDL_BUTTON_LEFT, app.sim(), cam);
            };
            drag(185, 260);                                           // Sound Volume: (260 - 211) 100 / 185 = 26
            drag(222, 380);                                           // Music Volume: (380 - 211) 100 / 185 = 91
            drag(260, 240);                                           // Scroll Speed: (240 - 211) 100 / 185 = 15
            ASSERT_EQ(hud.get_sound_volume(), 26);
            ASSERT_EQ(hud.get_music_volume(), 91);
            ASSERT_EQ(hud.get_scroll_speed(), 15);
            ASSERT_EQ(app.audio_mixer().sound_volume(), 26);
            hud.handle_mouse_down(151, 289, SDL_BUTTON_LEFT, app.sim(), cam);            // Chat OFF
            hud.handle_mouse_up(151, 289, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_mouse_down(404, 289, SDL_BUTTON_LEFT, app.sim(), cam);            // Quick Help OFF
            hud.handle_mouse_up(404, 289, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_mouse_down(150, 410, SDL_BUTTON_LEFT, app.sim(), cam);            // the F10 field
            hud.handle_mouse_up(150, 410, SDL_BUTTON_LEFT, app.sim(), cam);
            hud.handle_text_input(" Now!");
            ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be! Now!");
        }
        {
            std::ifstream in(file);
            ASSERT_TRUE(in.good());
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ASSERT_TRUE(text.find("Sound Volume=26\n") != std::string::npos);
            ASSERT_TRUE(text.find("Music Volume=91\n") != std::string::npos);
            ASSERT_TRUE(text.find("Scroll Speed=15\n") != std::string::npos);
            ASSERT_TRUE(text.find("Participate In Chat=0\n") != std::string::npos);
            ASSERT_TRUE(text.find("Show Quick Help at Startup=0\n") != std::string::npos);
            ASSERT_TRUE(text.find("Quick Chat F10=Let me be! Now!\n") != std::string::npos);
            ASSERT_TRUE(text.find("Quick Chat F9") == std::string::npos);        // only what a callback wrote is stored
        }
        {
            Application app;                                          // the next start
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = false;
            cfg.settings_path = file;
            ASSERT_TRUE(app.init(cfg));
            auto& hud = app.hud();
            ASSERT_EQ(hud.get_sound_volume(), 26);
            ASSERT_EQ(hud.get_music_volume(), 91);
            ASSERT_EQ(hud.get_scroll_speed(), 15);
            ASSERT_FALSE(hud.is_chat_enabled());
            ASSERT_FALSE(hud.is_quick_help_enabled());
            ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it!");      // not stored: the default
            ASSERT_EQ(hud.get_quick_chat_key(1), "Let me be! Now!");
            ASSERT_EQ(app.audio_mixer().sound_volume(), 26);                     // and applied at the start
            ASSERT_NEAR(app.audio_mixer().get_music_volume(), 0.91f, 0.001f);
            ASSERT_NEAR(app.midi_player().get_volume(), 0.91f, 0.001f);
            hud.open_options();                                                  // the screen is built from the settings: pos = 211 + 185 v / 99, thumb at pos - 23
            ASSERT_EQ(hud.options_screen().slider(0).thumb_left(), 236);
            ASSERT_EQ(hud.options_screen().slider(1).thumb_left(), 358);
            ASSERT_EQ(hud.options_screen().slider(2).thumb_left(), 216);
            ASSERT_EQ(hud.options_screen().slider(0).value(), 25);               // the mapping is not its own inverse: 26 is placed where 25 is read back
            ASSERT_TRUE(hud.options_screen().chat_off().latched() && hud.options_screen().help_off().latched());
        }
        std::filesystem::remove_all(dir);
    } TEST_END();

    TEST_CASE("9.13 Options: A Headless Run Keeps Its Settings In Memory; An Invalid Stored Value Is The Default (FUN_0102950f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.hud().options().quick_chat[2] == "Attack!");
        app.hud().open_options();
        app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera());
        ASSERT_FALSE(app.hud().is_options_open());
        ASSERT_TRUE(ConfigStore::default_location().empty() || !std::filesystem::exists(std::filesystem::path(ConfigStore::default_location()).parent_path() / "headless_marker"));

        const std::filesystem::path dir = temp_path_of_this_run("ants_test_settings2");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const std::string file = (dir / "settings.ini").string();
        {
            std::ofstream out(file);
            out << "Sound Volume=100\nMusic Volume=abc\nScroll Speed=100\nParticipate In Chat=1\nShow Quick Help at Startup=\nQuick Chat F9=" << std::string(130, 'x') << "\n";
        }
        Application other;
        ApplicationConfig cfg2;
        cfg2.headless = true;
        cfg2.start_in_map_select = false;
        cfg2.settings_path = file;
        ASSERT_TRUE(other.init(cfg2));
        ASSERT_EQ(other.hud().get_sound_volume(), 100);      // 100 is not below the maximum 100
        ASSERT_EQ(other.hud().get_music_volume(), 65);       // not a number
        ASSERT_EQ(other.hud().get_scroll_speed(), 50);       // 100 again
        ASSERT_TRUE(other.hud().is_chat_enabled());          // 1 is not the one valid value (0) of a switch
        ASSERT_TRUE(other.hud().is_quick_help_enabled());    // empty
        ASSERT_EQ(other.hud().get_quick_chat_key(0).size(), 100u);
        std::filesystem::remove_all(dir);
    } TEST_END();

    TEST_CASE("9.14 Startup Quick Help: START! Is The Button Class, The Keys Are Enter Esc C X (FUN_010145d2, FUN_010147c2); The Stored Switch Skips It") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        auto show = [&]() { app.finish_loading(); };

        // the stored option is on: the loading screen leads to the quick help
        show();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        // clicks elsewhere and the wrong keys do nothing
        app.quick_help_press(300, 200);
        app.quick_help_release(300, 200);
        for (SDL_Keycode k : {SDLK_SPACE, SDLK_m, SDLK_q, SDLK_s, SDLK_a, SDLK_TAB, SDLK_F1}) {
            app.quick_help_key(k);
            ASSERT_EQ(app.state(), AppState::QuickHelp);
        }
        // the button: captured at the press, acts at the release, leaving cancels for good
        app.quick_help_press(580, 450);
        ASSERT_TRUE(app.quick_help_start_button().pressed());
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::MapSelect);

        show();
        app.quick_help_press(580, 450);
        app.quick_help_move(10, 10);
        app.quick_help_move(580, 450);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_press(580, 450);
        app.quick_help_release(300, 200);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        // the hit test is the rectangle of the picture that shows (0x10112e9): a press must hit the up / hover picture [529, 627) x [437, 464), and every move until the
        // release must stay on the pressed picture [528, 625) x [438, 462): the click zone is the intersection [529, 625) x [438, 462) (docs 5.52)
        ASSERT_TRUE(app.quick_help_start_button().up_rect() == ButtonRect({529, 437, 98, 27}));
        ASSERT_TRUE(app.quick_help_start_button().pressed_rect() == ButtonRect({528, 438, 97, 24}));
        auto zone = [&](int32_t x, int32_t y) {
            show();
            app.quick_help_move(x, y);
            app.quick_help_press(x, y);
            app.quick_help_release(x, y);
            return app.state() == AppState::MapSelect;
        };
        ASSERT_TRUE(zone(529, 438));
        ASSERT_TRUE(zone(624, 461));
        ASSERT_TRUE(zone(580, 450));
        ASSERT_FALSE(zone(528, 450));                                          // the pressed picture only: no capture at all
        ASSERT_FALSE(zone(528, 437));
        ASSERT_FALSE(zone(529, 437));                                          // the up picture only: captured, the move that ends the input run cancels it
        ASSERT_FALSE(zone(625, 450));
        ASSERT_FALSE(zone(529, 462));
        ASSERT_FALSE(zone(626, 463));
        ASSERT_FALSE(zone(627, 450));
        show();
        app.quick_help_move(529, 437);
        app.quick_help_press(529, 437);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                 // a press in a dead strip never shows the pressed picture
        // the keys Enter, Esc, C and X (either case) are the same callback
        for (SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_ESCAPE, SDLK_c, SDLK_x}) {
            show();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            app.quick_help_key(k);
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }

        // the stored switch "Show Quick Help at Startup" = 0 skips the screen
        app.hud().options().quick_help = false;
        show();
        ASSERT_EQ(app.state(), AppState::MapSelect);
    } TEST_END();

    TEST_CASE("9.14b Quick Help To Setup Screen: The Pointer Is One Global (The New START Is In Hover At Once, Nothing Is Reset); START Is Where The Original Puts It On Each Screen (0x10110a4, 0x102653f)") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        ASSERT_TRUE(app.init(cfg));
        // the setup screen's START: up / hover [526, 624) x [439, 466), pressed [527, 624) x [443, 467)
        ASSERT_TRUE(app.map_select().start_button().up_rect() == ButtonRect({526, 439, 98, 27}));
        ASSERT_TRUE(app.map_select().start_button().pressed_rect() == ButtonRect({527, 443, 97, 24}));
        // pointer on both buttons: the hand-over leaves the new START in the hover picture before any mouse move arrives
        app.finish_loading();
        app.quick_help_move(580, 450);
        app.quick_help_press(580, 450);
        app.quick_help_release(580, 450);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.map_select().start_button().hovered());
        ASSERT_FALSE(app.map_select().start_button().pressed());
        // pointer on the quick help START only (x = 628, 529 + ... is outside the setup START): the new START is up
        app.finish_loading();
        app.quick_help_move(625, 440);
        app.quick_help_press(625, 440);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                 // (625, 440) is in the up picture only
        app.quick_help_move(530, 437);
        app.quick_help_press(530, 437);
        app.quick_help_release(530, 437);                                      // inside the up picture, outside the pressed one: nothing
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        app.quick_help_move(529, 462);
        ASSERT_TRUE(app.quick_help_start_button().hovered());
        app.quick_help_press(560, 460);
        app.quick_help_release(560, 460);                                      // y = 460 is inside both pictures: fires
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.map_select().start_button().hovered());                // (560, 460) is inside [526, 624) x [439, 466)
        // a pointer outside both buttons: up
        app.finish_loading();
        app.quick_help_move(300, 200);
        app.quick_help_key(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().start_button().hovered());
    } TEST_END();

    TEST_CASE("9.14c Quick Help To Setup Screen, The Original's Own Screens: The Second Click Of A Double Click On START! And A Held Enter Press The Local Game's START As Any Press Does (The Original Has No Double-Click Messages And Passes Key Repeats On); Releases That Began On The Quick Help Start Nothing") {
        {   // the second click of a double click (SDL: clicks 2), over both START buttons: a press on the new screen, the game starts (a local game is the original's screen, not guarded)
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            ASSERT_TRUE(rig.window != nullptr);
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900, 0, -1, 1);                  // picture (580, 450), the window is twice the picture
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900, 0, -1, 1);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_EQ(app.mouse_screen_x(), 580);
            ASSERT_EQ(app.mouse_screen_y(), 450);
            ASSERT_FALSE(app.closing_click_pending());                              // nothing is held back on a local game's screen
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900, 0, -1, 2);
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900, 0, -1, 2);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::Playing);
            app.shutdown();
        }
        {   // a held Enter: the press closes the quick help, its repeat presses START (the original's key handler does not tell them apart)
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            rig.key(SDL_KEYDOWN, SDLK_RETURN, false);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            rig.key(SDL_KEYDOWN, SDLK_RETURN, true);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::Playing);
            app.shutdown();
        }
        {   // a press that began on the quick help and is released after the quick help was closed by a key (the button held, Enter pressed): the release finds nothing pressed on the
            // new START, whatever the machine is, and does nothing; the quick help's own button is let go of
            Application app;
            ApplicationConfig cfg;
            cfg.headless = true;
            cfg.start_in_map_select = true;
            ASSERT_TRUE(app.init(cfg));
            const PointerRig rig(app);
            app.finish_loading();
            rig.button(SDL_MOUSEBUTTONDOWN, 1160, 900);
            rig.deliver();
            ASSERT_TRUE(app.quick_help_start_button().pressed());
            rig.key(SDL_KEYDOWN, SDLK_RETURN, false);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.quick_help_start_button().pressed());                  // (the window is gone: nothing stays pressed in it)
            ASSERT_FALSE(app.map_select().start_button().pressed());
            rig.button(SDL_MOUSEBUTTONUP, 1160, 900);
            rig.deliver();
            ASSERT_EQ(app.state(), AppState::MapSelect);                            // nothing started
            ASSERT_FALSE(app.map_select().start_button().pressed());
            app.shutdown();
        }
    } TEST_END();

    TEST_CASE("9.7 The Chat Box Is Always Active; Enter, [All] And [Team] Send; Hotkey Modifier Isolation") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();
        ViewportCamera cam;

        // 1. Verify that neither bare nor Ctrl hotkeys trigger any developer shortcut (there are none)
        SDL_KeyboardEvent key_ev{};
        key_ev.type = SDL_KEYDOWN;

        // Bare SDLK_t should not toggle tile grid
        bool initial_grid = app.is_tile_grid_visible();
        key_ev.keysym.sym = SDLK_t;
        key_ev.keysym.mod = 0; // No Ctrl!
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_tile_grid_visible(), initial_grid);

        // With Ctrl Ctrl+T does not either: there is no such key in the original
        key_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_tile_grid_visible(), initial_grid);

        // 2. The chat box is always active: the text of the keys (SDL_TEXTINPUT) lands in it without any focus
        ASSERT_TRUE(hud.get_chat_input().empty());
        hud.handle_text_input("Rush the base!");
        ASSERT_EQ(hud.get_chat_input(), "Rush the base!");

        // Backspace removes a character
        key_ev.keysym.sym = SDLK_BACKSPACE;
        key_ev.keysym.mod = 0;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Rush the base");

        // The key event of a printable key is not typed a second time (its text arrives as a text input event)
        key_ev.keysym.sym = SDLK_x;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Rush the base");

        // Sound callback tracking
        uint32_t played_sound = 0;
        hud.set_on_play_sfx([&](uint32_t sid) {
            played_sound = sid;
        });

        // Enter sends the text (to everybody: the local player has no ally) and clears the box
        key_ev.keysym.sym = SDLK_RETURN;
        app.handle_key_down(key_ev);
        ASSERT_TRUE(hud.get_chat_input().empty());
        ASSERT_EQ(played_sound, 0u); // Chat sound effect disabled per user request

        // Verify message in chat log (wrapped across lines)
        const auto& log = hud.get_chat_log();
        ASSERT_FALSE(log.empty());
        bool found_msg = false;
        for (const auto& entry : log) {
            if (entry.find("Rush the") != std::string::npos) {
                found_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_msg);

        // 3. Word Wrapping Test for Long Chat Messages
        size_t log_size_before = log.size();
        hud.handle_text_input("This is a really long chat message that will exceed twenty-two characters per line");
        hud.send_chat_message();
        ASSERT_TRUE(hud.get_chat_input().empty());
        // Should have created multiple wrapped lines
        ASSERT_TRUE(log.size() >= log_size_before + 3);

        // 4. [All] and [Team] send the text at once; [Team] exists only while the local player has an ally
        ASSERT_FALSE(hud.is_on_team());
        hud.set_chat_input("Nobody hears this as a team message");
        size_t lines_before = log.size();
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);      // the Team button's place: no button without an ally
        hud.handle_mouse_up(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);
        ASSERT_EQ(hud.get_chat_input(), "Nobody hears this as a team message");

        // [All] sends when it is released on the button (a press that leaves the button and is released elsewhere sends nothing)
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(300, 300, sim_engine, cam);
        hud.handle_mouse_up(300, 300, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(546, 451, sim_engine, cam);
        ASSERT_EQ(log.size(), lines_before);                                     // the callback runs at the release, not at the press
        hud.handle_mouse_up(546, 451, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(log.size() > lines_before);
        ASSERT_TRUE(hud.get_chat_input().empty());

        // Now set on team
        hud.set_on_team(true);
        ASSERT_TRUE(hud.is_on_team());

        // [Team] (579..625, 443..467) sends to the team
        hud.set_player_name("QueenAnt");
        hud.set_chat_input("Teammates defend!");
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        bool found_team_msg = false;
        for (const auto& entry : log) {
            if (entry.find("QueenAnt (To Teammate):") != std::string::npos) {
                found_team_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_team_msg);

        // Enter with an ally goes to the team as well
        hud.set_chat_input("Enter is a team message too");
        hud.handle_key_down(SDLK_RETURN, sim_engine, cam);
        bool found_enter_team = false;
        for (size_t li = 0; li + 1 < log.size(); ++li) {
            if (log[li] == "QueenAnt (To Teammate):" && log[li + 1].find("Enter is a team") == 0) found_enter_team = true;
        }
        ASSERT_TRUE(found_enter_team);

        // [All] (532..576, 443..467) sends to everybody even with an ally
        hud.set_chat_input("GG everyone");
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_up(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        // an entry is a header line "QueenAnt:" (in the color of the sender's team) and its body lines
        bool found_all_msg = false;
        for (size_t li = 0; li + 1 < log.size(); ++li) {
            if (log[li] == "QueenAnt:" && log[li + 1] == "GG everyone") {
                found_all_msg = true;
                ASSERT_TRUE(hud.get_chat_line_color(li) <= 3);      // a team color
                ASSERT_EQ(hud.get_chat_line_color(li + 1), 5);      // the body color (7, 11, 15)
                break;
            }
        }
        ASSERT_TRUE(found_all_msg);

        // 5. Escape is not a chat key: it deselects (FUN_01028c44) and leaves the typed text alone
        hud.set_chat_input("Unsent message");
        hud.select_ant(sim_engine.get_world_state().ants.front().id, false);
        key_ev.keysym.sym = SDLK_ESCAPE;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Unsent message");
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_FALSE(hud.is_quit_dialog_open());
    } TEST_END();
}
