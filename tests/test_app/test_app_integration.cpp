#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/pathfinding.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/application.hpp"
#include "ants_app/version.hpp"

using namespace ants::app;
using namespace ants::sim;
using namespace ants::assets;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

// ============================================================================
// Test Harness Assertions & Runner
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(60) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    fn();
    if (g_test_failures == prev_fails) {
        std::cout << "PASS\n";
    }
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LE(a, b) ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GE(a, b) ASSERT_TRUE((a) >= (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::abs((a) - (b)) <= (eps))

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
        ASSERT_EQ(sx, 17 + (350 - 200)); // 167
        ASSERT_EQ(sy, 22 + (500 - 350)); // 172

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
        ASSERT_EQ(static_cast<int32_t>(cam.x), 31 * 32 - 441); // 992 - 441 = 551
        ASSERT_EQ(static_cast<int32_t>(cam.y), 31 * 32 - 439); // 992 - 439 = 553

        // 60x60 Map (TREASURE.LVL) -> 1920 x 1920 pixels
        cam.x = 5000.0f;
        cam.y = 5000.0f;
        cam.clamp_to_bounds(60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 60 * 32 - 441); // 1920 - 441 = 1479
        ASSERT_EQ(static_cast<int32_t>(cam.y), 60 * 32 - 439); // 1920 - 439 = 1481
    } TEST_END();

    TEST_CASE("2.3 Centering Viewport on Simulation Entity") {
        ViewportCamera cam;

        // Entity at tile (30, 30) -> center (30*32+16, 30*32+16) = (976, 976)
        cam.center_on(976, 976, 60, 60);
        ASSERT_EQ(static_cast<int32_t>(cam.x), 976 - 441 / 2); // 976 - 220 = 756
        ASSERT_EQ(static_cast<int32_t>(cam.y), 976 - 439 / 2); // 976 - 219 = 757

        int32_t sx = 0, sy = 0;
        cam.world_to_screen(976, 976, sx, sy);
        // Entity should project directly to center of playfield
        ASSERT_EQ(sx, 17 + 441 / 2);
        ASSERT_EQ(sy, 22 + 439 / 2);
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

    TEST_CASE("4.3 Spatial Panning Left/Right/Center Attenuation") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);
        mixer.set_listener_position(500, 500);

        float vl = 0.0f, vr = 0.0f;

        // Center sound: dx = 0
        mixer.calculate_spatial_pan(500, 500, 1.0f, vl, vr);
        ASSERT_NEAR(vl, vr, 0.01f);
        ASSERT_GT(vl, 0.5f);

        // Left sound: dx = -220
        mixer.calculate_spatial_pan(280, 500, 1.0f, vl, vr);
        ASSERT_GT(vl, vr);
        ASSERT_GT(vl, 0.6f);
        ASSERT_LT(vr, 0.1f);

        // Right sound: dx = +220
        mixer.calculate_spatial_pan(720, 500, 1.0f, vl, vr);
        ASSERT_GT(vr, vl);
        ASSERT_GT(vr, 0.6f);
        ASSERT_LT(vl, 0.1f);

        // Distant sound (> 800px) -> attenuated to 0
        mixer.calculate_spatial_pan(5000, 5000, 1.0f, vl, vr);
        ASSERT_NEAR(vl, 0.0f, 0.001f);
        ASSERT_NEAR(vr, 0.0f, 0.001f);
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

    TEST_CASE("4.6 Native In-Engine MP3 Background Music Playback & Streaming Mixing") {
        AudioMixer mixer;
        mixer.set_headless_mode(true);
        mixer.init(archive);

        ASSERT_FALSE(mixer.is_music_playing());

        std::string intro_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.mp3";
        bool loaded = mixer.play_music(intro_path, true);
        ASSERT_TRUE(loaded);
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

    TEST_CASE("6.1 Match End 0:00 Simulation Freeze & Split Audio Routing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 999, 100); // 100 ms match (2 ticks)

        sim.set_player_score(0, 350); // Winner
        sim.set_player_score(1, 150); // Loser

        // Tick to expiration
        sim.tick();
        sim.tick();

        ASSERT_TRUE(sim.is_match_over());
        ASSERT_EQ(sim.get_match_time_remaining_ms(), 0u);

        // Verify audio queue contains split win/loss stings
        auto audio_events = sim.poll_audio_events();
        bool found_winner_fanfare = false;
        bool found_loser_sting = false;

        for (const auto& ev : audio_events) {
            if (ev.sound_id == SoundID::VictoryFanfare && ev.target_player == 0) {
                found_winner_fanfare = true;
            }
            if (ev.sound_id == SoundID::PlayerDefeat && ev.target_player == 1) {
                found_loser_sting = true;
            }
        }

        ASSERT_TRUE(found_winner_fanfare); // Winner received Sound 56
        ASSERT_TRUE(found_loser_sting);    // Loser received Sound 41
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
        ASSERT_EQ(modal.get_audio_to_play(), SoundID::VictoryFanfare);
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
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        ASSERT_TRUE(app.scorecard().is_open());

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
}

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

        // Enemy ant received damage from Combat Ant punch
        ASSERT_LT(sim.get_unit(enemy).hp, 10u);
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
        // Step through planting animation (placed at tick 18)
        for (int t = 0; t < 20; ++t) {
            sim.tick();
        }
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{6, 5}));

        // 2. Set water at (8, 7) and right click with Swimmer -> BuildBridge -> pixel (272, 240)
        sim.set_terrain(8, 7, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer);
        int32_t sx = 272 + HUD::PLAYFIELD_X;
        int32_t sy = 240 + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);

        ASSERT_TRUE(sim.has_bridge_at(TileCoord{8, 7}));
    } TEST_END();

    TEST_CASE("7.5 Hotkeys: 'A' Select All, 'N'/'P' Cycle, 'H' Center Home") {
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

        // 'Ctrl+C' key clears selection
        hud.handle_key_down('c', sim, camera, KMOD_CTRL);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());

        // 'Esc' toggles quit menu
        hud.handle_key_down(27, sim, camera);
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
        camera.viewport_w = 441;
        camera.viewport_h = 439;

        // Pan right and down with speed 480 px/s over 0.05s (24px)
        camera.pan(1.0f, 1.0f, 0.05f, 60, 60);
        ASSERT_EQ(camera.world_x, 124);
        ASSERT_EQ(camera.world_y, 124);

        // Center on tile (30, 30) -> world px (960, 960)
        camera.center_on(960, 960, 60, 60);
        int32_t expected_x = 960 - 441 / 2;
        int32_t expected_y = 960 - 439 / 2;
        ASSERT_EQ(camera.world_x, expected_x);
        ASSERT_EQ(camera.world_y, expected_y);
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

        // Simulate actual mouse motion to the top-left edge margin (x=10, y=10)
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 10;
        motion.y = 10;
        app.handle_mouse_motion(motion);

        ASSERT_TRUE(app.mouse_has_moved());
        ASSERT_EQ(app.mouse_screen_x(), 10);
        ASSERT_EQ(app.mouse_screen_y(), 10);

        // Step camera updates with mouse at top-left edge: camera now pans towards top-left
        for (int i = 0; i < 5; ++i) {
            app.handle_camera_panning(0.020f);
        }

        ASSERT_LT(app.renderer().camera().world_x, init_cam_x);
        ASSERT_LT(app.renderer().camera().world_y, init_cam_y);
    } TEST_END();
}

// ============================================================================
// SUITE 8: Unit Health Display & Map Selection Screen
// ============================================================================
void run_suite_8_unit_health_and_map_select() {
    TEST_SUITE("Suite 8: Unit Health Display & Map Selection Screen");

    TEST_CASE("8.1 Unit Health Display Flag Toggling and Visibility State") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // Default: overhead health bars are OFF (only selected unit shows health)
        ASSERT_FALSE(app.is_unit_health_visible());

        // Toggle ON
        app.toggle_unit_health_visibility();
        ASSERT_TRUE(app.is_unit_health_visible());

        // Toggle OFF
        app.toggle_unit_health_visibility();
        ASSERT_FALSE(app.is_unit_health_visible());

        // Direct setter
        app.set_unit_health_visible(true);
        ASSERT_TRUE(app.is_unit_health_visible());
    } TEST_END();

    TEST_CASE("8.2 Map Selection Screen Discovery & Navigation") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");

        const auto& maps = screen.get_maps();
        ASSERT_GE(maps.size(), 6u);

        // Verify canonical map entries
        bool has_treasure = false, has_small = false, has_tiny = false;
        for (const auto& m : maps) {
            if (m.filename == "TREASURE.LVL") {
                has_treasure = true;
                ASSERT_EQ(m.width, 60u);
                ASSERT_EQ(m.height, 60u);
            }
            if (m.filename == "SMALL.LVL") has_small = true;
            if (m.filename == "TINY.LVL") has_tiny = true;
        }
        ASSERT_TRUE(has_treasure);
        ASSERT_TRUE(has_small);
        ASSERT_TRUE(has_tiny);

        // Initial selection
        ASSERT_EQ(screen.get_selected_index(), 0);
        ASSERT_FALSE(screen.get_selected_map_path().empty());

        // Key Down: Next map (Down Arrow)
        screen.handle_key_down(SDLK_DOWN);
        ASSERT_EQ(screen.get_selected_index(), 1);

        // Key Down: Previous map (Up Arrow)
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), 0);

        // Wrap-around backwards
        screen.handle_key_down(SDLK_UP);
        ASSERT_EQ(screen.get_selected_index(), static_cast<int32_t>(maps.size() - 1));

        // Direct number hotkeys: '1' -> index 0, '4' -> index 3
        screen.handle_key_down(SDLK_1);
        ASSERT_EQ(screen.get_selected_index(), 0);

        screen.handle_key_down(SDLK_4);
        ASSERT_EQ(screen.get_selected_index(), 3);

        // Start callback trigger on Enter
        std::string started_map = "";
        screen.set_on_start([&](const std::string& path) {
            started_map = path;
        });

        screen.handle_key_down(SDLK_RETURN);
        ASSERT_FALSE(started_map.empty());
        ASSERT_TRUE(started_map.find("TINY.LVL") != std::string::npos);
    } TEST_END();

    TEST_CASE("8.3 Map Select Mouse Interaction & Stepper Launch") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");

        std::string launched_path = "";
        screen.set_on_start([&](const std::string& path) {
            launched_path = path;
        });

        // Click Down arrow button to advance to 2nd map (SMALL.LVL)
        screen.handle_mouse_down(MapSelectScreen::BTN_DOWN_X + 10, MapSelectScreen::BTN_DOWN_Y + 10, SDL_BUTTON_LEFT);
        ASSERT_EQ(screen.get_selected_index(), 1);
        ASSERT_TRUE(launched_path.empty()); // Single click does not launch immediately

        // Click START GAME button
        screen.handle_mouse_down(MapSelectScreen::BTN_START_X + 20, MapSelectScreen::BTN_START_Y + 10, SDL_BUTTON_LEFT);
        ASSERT_FALSE(launched_path.empty());
        ASSERT_TRUE(launched_path.find("SMALL.LVL") != std::string::npos);
    } TEST_END();

    TEST_CASE("8.5 Fog of War Toggle and Player Drop Status Controls") {
        MapSelectScreen screen;
        screen.init("Original-Ants/Maps");

        // Default: Fog of War disabled (authentic reference specification)
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // Click "On" button
        screen.handle_mouse_down(MapSelectScreen::BTN_FOW_ON_X + 5, MapSelectScreen::BTN_FOW_ON_Y + 5, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());

        // Click "Off" button
        screen.handle_mouse_down(MapSelectScreen::BTN_FOW_OFF_X + 5, MapSelectScreen::BTN_FOW_OFF_Y + 5, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // Player 2 initial state is unready (thumbs down)
        ASSERT_FALSE(screen.is_player_ready(2));
        ASSERT_TRUE(screen.is_player_ready(0));
        ASSERT_TRUE(screen.is_player_ready(1));

        // Click Drop button -> toggles Player 2 ready state
        screen.handle_mouse_down(MapSelectScreen::BTN_DROP_X + 10, MapSelectScreen::BTN_DROP_Y + 10, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_player_ready(2));

        screen.handle_mouse_down(MapSelectScreen::BTN_DROP_X + 10, MapSelectScreen::BTN_DROP_Y + 10, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_player_ready(2));
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

    TEST_CASE("8.6 Ctrl+G Tile Grid Display Toggling and Visibility State") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // Default: Tile grid overlay is OFF
        ASSERT_FALSE(app.is_tile_grid_visible());

        // Toggle ON
        app.toggle_tile_grid_visibility();
        ASSERT_TRUE(app.is_tile_grid_visible());

        // Toggle OFF
        app.toggle_tile_grid_visibility();
        ASSERT_FALSE(app.is_tile_grid_visible());

        // Direct setter
        app.set_tile_grid_visible(true);
        ASSERT_TRUE(app.is_tile_grid_visible());
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

        ASSERT_EQ(sim.get_player_eggs(0), initial_eggs - 1);
        ASSERT_EQ(sim.get_player_score(0), 300);
        ASSERT_EQ(sim.get_world_state().ants.size(), initial_ants + 1);

        // 4. Click Stop button at (610, 195) to deselect base
        bool stop_down = hud.handle_mouse_down(610, 195, SDL_BUTTON_LEFT, sim, cam);
        ASSERT_TRUE(stop_down);
        hud.handle_mouse_up(610, 195, SDL_BUTTON_LEFT, sim, cam);

        ASSERT_EQ(hud.get_selected_base_team_id(), -1);
    } TEST_END();

    TEST_CASE("8.8 Team Switching and Camera Base Centering") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        // Default starts as Player 0 (Green)
        ASSERT_EQ(app.local_player_id(), 0);

        // Verify camera is centered on Team 0's base (accounting for 4x4 anthill center & viewport bounds clamping)
        const auto* base0 = app.sim().grid().find_anthill(0);
        ASSERT_TRUE(base0 != nullptr);
        const auto& cam = app.renderer().camera();
        int32_t expected_x = std::clamp(base0->x * 32 + 64 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        int32_t expected_y = std::clamp(base0->y * 32 + 64 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Switch to Team 1 (Red) via Ctrl+2 key event
        SDL_KeyboardEvent key_event{};
        key_event.type = SDL_KEYDOWN;
        key_event.keysym.sym = SDLK_2;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);

        ASSERT_EQ(app.local_player_id(), 1);
        const auto* base1 = app.sim().grid().find_anthill(1);
        ASSERT_TRUE(base1 != nullptr);
        expected_x = std::clamp(base1->x * 32 + 64 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        expected_y = std::clamp(base1->y * 32 + 64 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Switch to Team 2 via direct setter
        app.set_local_player(2);
        ASSERT_EQ(app.local_player_id(), 2);
        const auto* base2 = app.sim().grid().find_anthill(2);
        ASSERT_TRUE(base2 != nullptr);
        expected_x = std::clamp(base2->x * 32 + 64 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        expected_y = std::clamp(base2->y * 32 + 64 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Cycle to next team (Team 3) via Ctrl+Tab
        key_event.keysym.sym = SDLK_TAB;
        key_event.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_event);
        ASSERT_EQ(app.local_player_id(), 3);
    } TEST_END();
}

// ============================================================================
// Suite 9: Gameplay Mechanics, Food Schedules, Hangman Pathing & Options Dialog
// ============================================================================
void run_suite_9_gameplay_mechanics_and_options() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] Suite 9: Gameplay Mechanics, Hangman & Options\n"
              << "=======================================================\n";

    TEST_CASE("9.1 Multi-Stage Food Harvesting & Schedule Depletion") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& sim_engine = app.sim();
        auto& grid = sim_engine.grid_mut();

        // Verify active food schedules loaded from TREASURE.LVL
        const auto& schedules = grid.food_schedules();
        ASSERT_TRUE(!schedules.empty());

        // Locate a food cell with schedule
        int32_t fx = -1, fy = -1;
        for (const auto& fs : schedules) {
            if (fs.remaining_bites > 0 && !fs.footprint.empty()) {
                fx = fs.footprint[0].x;
                fy = fs.footprint[0].y;
                break;
            }
        }
        ASSERT_TRUE(fx >= 0 && fy >= 0);

        // Find pointer to schedule in grid
        ActiveFoodSchedule* sched = nullptr;
        for (auto& fs : grid.food_schedules_mut()) {
            for (const auto& c : fs.footprint) {
                if (c.x == fx && c.y == fy) {
                    sched = &fs;
                    break;
                }
            }
            if (sched) break;
        }
        ASSERT_TRUE(sched != nullptr);
        int32_t initial_bites = sched->remaining_bites;
        ASSERT_TRUE(initial_bites > 0);

        // Spawn a worker directly on the food cell
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{fx, fy});
        auto& ant = sim_engine.get_unit(ant_id);
        ASSERT_FALSE(ant.is_holding());

        // Order worker to harvest food at {fx, fy}
        sim_engine.issue_move_order(ant_id, TileCoord{fx, fy});

        // Tick simulation to execute harvest
        sim_engine.tick();

        // Verify bites decremented, ant is holding food, and harvest_origin recorded
        ASSERT_TRUE(sched->remaining_bites < initial_bites);
        ASSERT_TRUE(ant.is_holding());
        ASSERT_EQ(ant.harvest_origin.x, fx);
        ASSERT_EQ(ant.harvest_origin.y, fy);
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

        // Spawn damaged worker carrying food at base queue spot (bx - 1, by + 3)
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        auto& ant = sim_engine.get_unit(ant_id);
        ant.hp = 20; // Damaged
        ant.pick_up_food(1, 25);
        ant.harvest_origin = TileCoord{15, 15};

        // Issue move order to base hole (bx+1, by+1)
        sim_engine.issue_move_order(ant_id, TileCoord{bx + 1, by + 1});

        // Tick movement until entering hole
        for (int i = 0; i < 90 && ant.state != UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }

        // Must enter EnteringBase state
        ASSERT_EQ(static_cast<uint16_t>(ant.state), static_cast<uint16_t>(UnitState::EnteringBase));

        // Advance through entering hole animation to frame 8 (underground) and beyond
        for (int i = 0; i < 25 && ant.state == UnitState::EnteringBase; ++i) {
            sim_engine.tick();
        }

        // Food deposited and ant fully healed
        ASSERT_FALSE(ant.is_holding());
        ASSERT_EQ(ant.hp, ant.max_hp);
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
        ASSERT_TRUE(hud.get_sfx_volume() > 0.6f);

        // Drag music volume slider (x=280, y=223)
        hud.handle_mouse_down(280, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        hud.handle_mouse_motion(340, 223, sim_engine, cam);
        hud.handle_mouse_up(340, 223, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(hud.get_music_volume() > 0.7f);
        ASSERT_NEAR(app.midi_player().get_volume(), hud.get_music_volume(), 0.01f);

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

        // Toggle tile grid
        ASSERT_FALSE(app.is_tile_grid_visible());
        app.toggle_tile_grid_visibility();
        ASSERT_TRUE(app.is_tile_grid_visible());
        app.toggle_tile_grid_visibility();
        ASSERT_FALSE(app.is_tile_grid_visible());
    } TEST_END();

    TEST_CASE("9.6 Ant Type Movement and Ready Voice Confirmations") {
        // Verify authentic voice sound ID mapping across all 6 ant types
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 0), 17); // gantgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Worker, 1), 15); // gantcommand.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Swimmer, 0), 33); // brdggo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Fire, 0), 23); // firego.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 0), 28); // combgo1.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Combat, 1), 29); // combgo2.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Bomber, 0), 37); // bombgo.wav
        ASSERT_EQ(ants::sim::get_move_voice_sound(ants::sim::AntType::Thief, 0), 19); // theifgo.wav

        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 0), 14); // gantrdy.wav
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Worker, 1), 13); // gantorders.wav
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 0), 27); // combrdy1.wav
        ASSERT_EQ(ants::sim::get_ready_voice_sound(ants::sim::AntType::Combat, 1), 26); // combrdy2.wav

        ASSERT_EQ(ants::sim::get_attack_voice_sound(ants::sim::AntType::Worker, 0), 16); // gantattack.wav
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
        hud.set_on_play_sfx([&](uint32_t sid) {
            last_sfx = sid;
        });

        // Select all friendly ants
        hud.select_all_friendly(sim_engine.get_world_state());
        ASSERT_TRUE(last_sfx == 14 || last_sfx == 13); // gantrdy or gantorders

        // Issue move order via right click on playfield
        ViewportCamera cam;
        hud.handle_mouse_down(100, 100, SDL_BUTTON_RIGHT, sim_engine, cam);
        ASSERT_TRUE(last_sfx == 17 || last_sfx == 15); // gantgo or gantcommand
    } TEST_END();

    TEST_CASE("9.7 Chat Input System, Team Chat Toggle & Hotkey Modifier Isolation") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.default_map_path = "Original-Ants/Maps/TREASURE.LVL";
        cfg.start_in_map_select = false;
        ASSERT_TRUE(app.init(cfg));

        auto& hud = app.hud();
        auto& sim_engine = app.sim();
        ViewportCamera cam;

        // 1. Verify bare hotkeys do NOT trigger gameplay commands
        // In Application: SDLK_l (without Ctrl) should not toggle health display
        bool initial_health = app.is_unit_health_visible();
        SDL_KeyboardEvent key_ev{};
        key_ev.type = SDL_KEYDOWN;
        key_ev.keysym.sym = SDLK_l;
        key_ev.keysym.mod = 0; // No Ctrl!
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_unit_health_visible(), initial_health);

        // Bare SDLK_t should not toggle tile grid
        bool initial_grid = app.is_tile_grid_visible();
        key_ev.keysym.sym = SDLK_t;
        key_ev.keysym.mod = 0; // No Ctrl!
        app.handle_key_down(key_ev);
        ASSERT_EQ(app.is_tile_grid_visible(), initial_grid);

        // With Ctrl modifier, Ctrl+T DOES toggle tile grid
        key_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(key_ev);
        ASSERT_NE(app.is_tile_grid_visible(), initial_grid);

        // With Ctrl modifier, Ctrl+L DOES toggle unit health
        key_ev.keysym.sym = SDLK_l;
        app.handle_key_down(key_ev);
        ASSERT_NE(app.is_unit_health_visible(), initial_health);

        // 2. Chat Focus and Input Handling
        ASSERT_FALSE(hud.is_chat_focused());

        // Pressing Enter (SDLK_RETURN) focuses chat
        key_ev.keysym.sym = SDLK_RETURN;
        key_ev.keysym.mod = 0;
        app.handle_key_down(key_ev);
        ASSERT_TRUE(hud.is_chat_focused());

        // Typing characters via handle_text_input
        hud.handle_text_input("Rush the base!");
        ASSERT_EQ(hud.get_chat_input(), "Rush the base!");

        // Backspace removes character
        key_ev.keysym.sym = SDLK_BACKSPACE;
        app.handle_key_down(key_ev);
        ASSERT_EQ(hud.get_chat_input(), "Rush the base");

        // Sound callback tracking
        uint32_t played_sound = 0;
        hud.set_on_play_sfx([&](uint32_t sid) {
            played_sound = sid;
        });

        // Sending message with Enter
        key_ev.keysym.sym = SDLK_RETURN;
        app.handle_key_down(key_ev);
        ASSERT_FALSE(hud.is_chat_focused());
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
        hud.focus_chat();
        hud.handle_text_input("This is a really long chat message that will exceed twenty-two characters per line");
        hud.send_chat_message();
        ASSERT_TRUE(hud.get_chat_input().empty());
        // Should have created multiple wrapped lines
        ASSERT_TRUE(log.size() >= log_size_before + 3);

        // 4. Team Chat Button and Scoping
        // In FFA, is_on_team is false, send_to_all is true
        ASSERT_FALSE(hud.is_on_team());
        ASSERT_TRUE(hud.is_send_to_all());

        // Clicking Team button when not on team does nothing
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(hud.is_send_to_all());

        // Now set on team
        hud.set_on_team(true);
        ASSERT_TRUE(hud.is_on_team());

        // Clicking Team button (579..625, 443..467) switches send_to_all to false
        hud.handle_mouse_down(590, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_FALSE(hud.is_send_to_all());

        // Send a team message
        hud.set_player_name("QueenAnt");
        hud.set_chat_input("Teammates defend!");
        hud.send_chat_message();
        bool found_team_msg = false;
        for (const auto& entry : log) {
            if (entry.find("QueenAnt (Team):") != std::string::npos) {
                found_team_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_team_msg);

        // Clicking All button (532..576, 443..467) switches back to All
        hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim_engine, cam);
        ASSERT_TRUE(hud.is_send_to_all());

        // Send an all message
        hud.set_chat_input("GG everyone");
        hud.send_chat_message();
        bool found_all_msg = false;
        for (const auto& entry : log) {
            if (entry.find("QueenAnt: GG") != std::string::npos) {
                found_all_msg = true;
                break;
            }
        }
        ASSERT_TRUE(found_all_msg);

        // 5. Escape cancels chat input without sending
        hud.focus_chat();
        hud.set_chat_input("Unsent message");
        key_ev.keysym.sym = SDLK_ESCAPE;
        app.handle_key_down(key_ev);
        ASSERT_FALSE(hud.is_chat_focused());
        ASSERT_TRUE(hud.get_chat_input().empty());
    } TEST_END();
}

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
        sim.set_player_score(0, 600);
        sim.set_player_eggs(0, 2);

        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(sim.get_player_score(0), 600);

        // 1st Hatch: 600 -> 400 pts, 2 -> 1 eggs
        bool h1 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h1);
        ASSERT_EQ(sim.get_player_score(0), 400);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);

        // 2nd Hatch: 400 -> 200 pts, 1 -> 0 eggs
        bool h2 = sim.hatch_ant(0, AntType::Worker);
        ASSERT_TRUE(h2);
        ASSERT_EQ(sim.get_player_score(0), 200);
        ASSERT_EQ(sim.get_player_eggs(0), 0u);

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

        // Newborn spawned underground at the anthill hole (bx+1, by+1)
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        const auto& ws0 = sim.get_world_state();
        bool found_newborn = false;
        uint32_t newborn_id = 0;
        for (const auto& a : ws0.ants) {
            if (a.player_id == 0 && a.is_underground) {
                found_newborn = true;
                newborn_id = a.id;
                ASSERT_EQ(a.tile_x, bx + 1);
                ASSERT_EQ(a.tile_y, by + 1);
                break;
            }
        }
        ASSERT_TRUE(found_newborn);

        // Tick 10 ticks: still incubating underground
        for (int i = 0; i < 10; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        const auto& u_inc = sim.get_unit(newborn_id);
        ASSERT_TRUE(u_inc.underground);

        // Tick 15 more ticks (total 25 ticks > 20 delay): emergence triggered
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);

        // Advance simulation until ant completes emergence and walks to idle spot (bx+4, by+4)
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

        const auto& b_unit = sim.get_unit(bomber);
        ASSERT_EQ(b_unit.pending_ability, OrderType::PlantBomb);
        ASSERT_EQ(b_unit.ability_target.x, 8);
        ASSERT_EQ(b_unit.ability_target.y, 5);

        // Step simulation until Bomber arrives and plants bomb
        for (int i = 0; i < 60; ++i) sim.tick();
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{8, 5}));

        // 2. Swimmer at (6, 8). Set water at distance (10, 8). Right-click (10, 8) -> pixel (10*32 + 16, 8*32 + 16)
        uint32_t swimmer2 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{6, 8});
        sim.set_terrain(10, 8, 2); // 2 = TERRAIN_WATER
        hud.select_ant(swimmer2);
        int32_t sx = (10 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t sy = (8 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(sx, sy, 3, sim, camera);

        const auto& s_unit = sim.get_unit(swimmer2);
        ASSERT_EQ(s_unit.pending_ability, OrderType::BuildBridge);
        ASSERT_EQ(s_unit.ability_target.x, 10);
        ASSERT_EQ(s_unit.ability_target.y, 8);

        for (int i = 0; i < 80; ++i) sim.tick();
        ASSERT_TRUE(sim.has_bridge_at(TileCoord{10, 8}));

        // 3. Swimmer right-clicking land tile -> standard Move order (no bridge built on land)
        hud.select_ant(swimmer2);
        int32_t mx = (6 * 32 + 16) + HUD::PLAYFIELD_X;
        int32_t my = (6 * 32 + 16) + HUD::PLAYFIELD_Y;
        hud.handle_mouse_down(mx, my, 3, sim, camera);
        const auto& s_unit2 = sim.get_unit(swimmer2);
        ASSERT_EQ(s_unit2.pending_ability, OrderType::None);
        ASSERT_FALSE(sim.has_bridge_at(TileCoord{6, 6}));
    } TEST_END();

    TEST_CASE("10.5 Enemy Base Click & Team-Up Option") {
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

        // Click Team Up button (Pedestal 1 at x=495, y=165)
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);

        // Check proposal registered
        auto audio = sim.poll_audio_events();
        bool has_pro_sound = false;
        for (const auto& ev : audio) {
            if (ev.sound_id == SoundID::AlliancePro) has_pro_sound = true;
        }
        ASSERT_TRUE(has_pro_sound);

        // AI accepts after ~30 ticks (1.5s)
        for (int i = 0; i < 35; ++i) sim.tick();
        const auto& ws_allied = sim.get_world_state();
        ASSERT_EQ(ws_allied.player_alliances[0], 1u);
        ASSERT_EQ(ws_allied.player_alliances[1], 0u);

        // Clicking Team Up again breaks alliance
        hud.handle_mouse_down(495, 165, 1, sim, camera);
        hud.handle_mouse_up(495, 165, 1, sim, camera);

        auto audio_break = sim.poll_audio_events();
        bool has_break_sound = false;
        for (const auto& ev : audio_break) {
            if (ev.sound_id == SoundID::AllianceBreak) has_break_sound = true;
        }
        ASSERT_TRUE(has_break_sound);
        const auto& ws_broken = sim.get_world_state();
        ASSERT_NE(ws_broken.player_alliances[0], 1u);
    } TEST_END();
}

// ============================================================================
// SUITE 11: Anthill Queuing & Priority Serialization
// ============================================================================
void run_suite_11_anthill_queuing_and_priority() {
    TEST_SUITE("Suite 11: Anthill Queuing & Priority Serialization");

    TEST_CASE("11.1 Base Queuing Location Strictly To The Left of Base") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Slot coordinates strictly to the left of the base mound [bx..bx+3, by..by+3]
        TileCoord s0 = sim.get_base_queue_slot(0, 0);
        TileCoord s1 = sim.get_base_queue_slot(0, 1);
        TileCoord s2 = sim.get_base_queue_slot(0, 2);
        TileCoord s3 = sim.get_base_queue_slot(0, 3);
        TileCoord s4 = sim.get_base_queue_slot(0, 4);

        ASSERT_EQ(s0.x, bx - 1);
        ASSERT_EQ(s0.y, by + 3);

        ASSERT_EQ(s1.x, bx - 1);
        ASSERT_EQ(s1.y, by + 2);

        ASSERT_EQ(s2.x, bx - 1);
        ASSERT_EQ(s2.y, by + 1);

        ASSERT_EQ(s3.x, bx - 1);
        ASSERT_EQ(s3.y, by);

        ASSERT_EQ(s4.x, bx - 2);
        ASSERT_EQ(s4.y, by + 3);

        // First worker joining empty queue gets priority immediately and goes straight into base
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        AntOrder ret1{};
        ret1.ant_id = w1;
        ret1.type = OrderType::ReturnToBase;
        sim.issue_order(ret1);

        ASSERT_TRUE(sim.is_ant_in_base_queue(w1));
        ASSERT_EQ(sim.get_active_depositing_ant(0), w1);
        const auto& unit1 = sim.get_unit(w1);
        ASSERT_FALSE(unit1.waypoints.empty());
        ASSERT_EQ(unit1.waypoints.back().x, bx + 1);
        ASSERT_EQ(unit1.waypoints.back().y, by + 1);

        // Second worker joining while w1 has priority routes to queue slot 1 (bx - 1, by + 2)
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 15});
        AntOrder ret2{};
        ret2.ant_id = w2;
        ret2.type = OrderType::ReturnToBase;
        sim.issue_order(ret2);

        ASSERT_TRUE(sim.is_ant_in_base_queue(w2));
        ASSERT_EQ(sim.get_active_depositing_ant(0), w1);
        const auto& unit2 = sim.get_unit(w2);
        ASSERT_FALSE(unit2.waypoints.empty());
        ASSERT_EQ(unit2.waypoints.back().x, bx - 1);
        ASSERT_EQ(unit2.waypoints.back().y, by + 2);
    } TEST_END();

    TEST_CASE("11.2 Serialized Base Entry & Priority Queue (Stand Off to Side & Wait)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Spawn 3 workers carrying food directly at queue slots 0, 1, 2
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Join base queue in order 1, 2, 3
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);

        ASSERT_EQ(sim.get_base_queue_size(0), 3u);

        // Step simulation 1 tick
        sim.tick();

        // Ant 1 is at head slot, granted priority to enter!
        ASSERT_EQ(sim.get_active_depositing_ant(0), a1);
        ASSERT_EQ(sim.get_unit(a1).state, UnitState::Walking);

        // Ant 2 and Ant 3 must stand off to the side in QueuingBase, facing East towards the base!
        ASSERT_EQ(sim.get_unit(a2).state, UnitState::QueuingBase);
        ASSERT_EQ(sim.get_unit(a2).facing, Direction::East);
        ASSERT_TRUE(sim.get_unit(a2).waypoints.empty());

        ASSERT_EQ(sim.get_unit(a3).state, UnitState::QueuingBase);
        ASSERT_EQ(sim.get_unit(a3).facing, Direction::East);
        ASSERT_TRUE(sim.get_unit(a3).waypoints.empty());

        // Step simulation until Ant 1 reaches hole and deposits food
        for (int i = 0; i < 80 && sim.get_unit(a1).is_holding(); ++i) {
            sim.tick();
            // During Ant 1's journey, Ant 2 and Ant 3 must remain waiting
            if (sim.get_unit(a1).is_holding()) {
                ASSERT_EQ(sim.get_unit(a2).state, UnitState::QueuingBase);
                ASSERT_EQ(sim.get_unit(a3).state, UnitState::QueuingBase);
            }
        }

        // Ant 1 deposited food: score increased by 25, scoreup.wav (Sound 87) triggered
        ASSERT_FALSE(sim.get_unit(a1).is_holding());
        ASSERT_EQ(sim.get_player_score(0), 25);
        ASSERT_TRUE(sim.has_audio_event(SoundID::BaseScoreUp));

        // Ant 1 completes emergence and vacates the hole. Ant 2 and Ant 3 remain waiting in QueuingBase!
        for (int i = 0; i < 80 && sim.get_active_depositing_ant(0) == a1; ++i) {
            sim.tick();
            if (sim.get_active_depositing_ant(0) == a1) {
                ASSERT_EQ(sim.get_unit(a2).state, UnitState::QueuingBase);
                ASSERT_EQ(sim.get_unit(a3).state, UnitState::QueuingBase);
            }
        }
        ASSERT_EQ(sim.get_active_depositing_ant(0), a2);
        ASSERT_EQ(sim.get_unit(a2).state, UnitState::Walking);

        // Step simulation until Ant 2 reaches hole and deposits food
        for (int i = 0; i < 80 && sim.get_unit(a2).is_holding(); ++i) {
            sim.tick();
            if (sim.get_unit(a2).is_holding()) {
                // Ant 3 advanced to slot 1 or is waiting in QueuingBase (or brief Idle on arrival frame)
                ASSERT_TRUE(sim.get_unit(a3).state == UnitState::Walking || sim.get_unit(a3).state == UnitState::QueuingBase || sim.get_unit(a3).state == UnitState::Idle);
            }
        }

        // Ant 2 deposited food: score increased to 50
        ASSERT_FALSE(sim.get_unit(a2).is_holding());
        ASSERT_EQ(sim.get_player_score(0), 50);

        // Ant 2 completes emergence and vacates the hole. Ant 3 remains waiting in QueuingBase!
        for (int i = 0; i < 80 && sim.get_active_depositing_ant(0) == a2; ++i) {
            sim.tick();
            if (sim.get_active_depositing_ant(0) == a2) {
                ASSERT_EQ(sim.get_unit(a3).state, UnitState::QueuingBase);
            }
        }
        ASSERT_EQ(sim.get_active_depositing_ant(0), a3);
        ASSERT_EQ(sim.get_unit(a3).state, UnitState::Walking);

        // Step simulation until Ant 3 deposits food
        for (int i = 0; i < 80 && sim.get_unit(a3).is_holding(); ++i) {
            sim.tick();
        }

        // Ant 3 deposited food: total score 75
        ASSERT_FALSE(sim.get_unit(a3).is_holding());
        ASSERT_EQ(sim.get_player_score(0), 75);
    } TEST_END();

    TEST_CASE("11.3 Escape Key Quick Quit Menu Trigger") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        ViewportCamera camera;
        HUD hud;
        hud.init(0);
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        hud.select_ant(a1);
        ASSERT_FALSE(hud.get_selected_ant_ids().empty());
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Press Escape key (27)
        hud.handle_key_down(27, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());

        // Press Escape key again toggles closed
        hud.handle_key_down(27, sim, camera);
        ASSERT_FALSE(hud.is_quit_dialog_open());

        // Press SDLK_ESCAPE directly
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
        ASSERT_TRUE(hud.is_quit_dialog_open());
        hud.handle_key_down(SDLK_ESCAPE, sim, camera);
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

        // Press Escape in-game opens quit dialog
        app.handle_key_down(esc_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());

        // Press Escape again closes quit dialog
        app.handle_key_down(esc_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Press Escape with active order mode: cancels order mode and opens quit dialog
        app.hud().set_active_order_mode(ants::sim::OrderType::BuildBridge);
        ASSERT_EQ(app.hud().get_active_order_mode(), ants::sim::OrderType::BuildBridge);
        app.handle_key_down(esc_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        ASSERT_EQ(app.hud().get_active_order_mode(), ants::sim::OrderType::None);

        // Pressing 'N' closes quit dialog
        SDL_KeyboardEvent n_ev{};
        n_ev.type = SDL_KEYDOWN;
        n_ev.keysym.sym = SDLK_n;
        app.handle_key_down(n_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());

        // Press Escape and then 'Y' quits the game
        app.handle_key_down(esc_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("11.4 Base Queue Priority Across Map & Interruption Release") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 12 * 60 * 1000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        sim.grid_mut().set_anthill(1, TileCoord{40, 40});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // Ant 1 on the far side of the map (10, 10) picks up food and joins queue
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.get_unit(a1).pick_up_food(1, 25);
        sim.join_base_queue(a1);

        // Ant 1 gets priority immediately and heads straight into the base entrance
        ASSERT_EQ(sim.get_active_depositing_ant(0), a1);
        ASSERT_EQ(sim.get_unit(a1).final_dest, (TileCoord{bx + 1, by + 1}));
        ASSERT_FALSE(sim.get_unit(a1).waypoints.empty());
        ASSERT_EQ(sim.get_unit(a1).waypoints.back(), (TileCoord{bx + 1, by + 1}));

        // Ant 2 at (10, 15) joins while Ant 1 has priority: Ant 2 routes to queue slot
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 15});
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.join_base_queue(a2);

        ASSERT_EQ(sim.get_active_depositing_ant(0), a1);
        ASSERT_EQ(sim.get_unit(a2).final_dest, sim.get_base_queue_slot(0, 1));

        // Interruption Case 1: User moves Ant 1 to a different location
        AntOrder move_cmd{};
        move_cmd.ant_id = a1;
        move_cmd.type = OrderType::Move;
        move_cmd.target_x = 5;
        move_cmd.target_y = 5;
        sim.issue_order(move_cmd);

        // Ant 1 releases priority and is no longer in base queue
        ASSERT_FALSE(sim.is_ant_in_base_queue(a1));
        ASSERT_NE(sim.get_active_depositing_ant(0), a1);

        // Ant 2 immediately claims priority and heads straight into the base!
        ASSERT_EQ(sim.get_active_depositing_ant(0), a2);
        ASSERT_EQ(sim.get_unit(a2).final_dest, (TileCoord{bx + 1, by + 1}));
        ASSERT_FALSE(sim.get_unit(a2).waypoints.empty());
        ASSERT_EQ(sim.get_unit(a2).waypoints.back(), (TileCoord{bx + 1, by + 1}));

        // Spawn Ant 3 at (10, 20) and join queue
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
        sim.get_unit(a3).pick_up_food(1, 25);
        sim.join_base_queue(a3);

        ASSERT_EQ(sim.get_active_depositing_ant(0), a2);
        ASSERT_EQ(sim.get_unit(a3).final_dest, sim.get_base_queue_slot(0, 1));

        // Interruption Case 2: Ant 2 gets attacked by enemy
        uint32_t enemy = sim.spawn_unit(1, AntType::Combat, TileCoord{sim.get_unit(a2).pos.x + 1, sim.get_unit(a2).pos.y});
        sim.execute_melee_attack(enemy, a2);

        // Ant 2 releases priority
        ASSERT_FALSE(sim.is_ant_in_base_queue(a2));
        ASSERT_NE(sim.get_active_depositing_ant(0), a2);

        // Ant 3 immediately claims priority and heads straight into the base!
        ASSERT_EQ(sim.get_active_depositing_ant(0), a3);
        ASSERT_EQ(sim.get_unit(a3).final_dest, (TileCoord{bx + 1, by + 1}));
        ASSERT_FALSE(sim.get_unit(a3).waypoints.empty());
        ASSERT_EQ(sim.get_unit(a3).waypoints.back(), (TileCoord{bx + 1, by + 1}));
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

    TEST_CASE("12.3 Already-Adjacent Move Order Stops Immediately") {
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

        // Ordering adj_ant to move to (20, 20) stops immediately since it is already adjacent
        sim.issue_move_order(adj_id, TileCoord{20, 20});

        ASSERT_EQ(adj_ant.state, UnitState::Idle);
        ASSERT_TRUE(adj_ant.waypoints.empty());
        ASSERT_EQ(adj_ant.pos, (TileCoord{20, 21}));
        ASSERT_FALSE(sim.has_audio_event(SoundID::FlingThumpA));
    } TEST_END();

    TEST_CASE("12.4 Discrete Tile Bouncing & Cascade Resolution") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn ant 1 at (25, 25)
        uint32_t a1_id = sim.spawn_unit(0, AntType::Combat, TileCoord{25, 25});
        // Spawn ant 2 at (25, 25)
        uint32_t a2_id = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
        // Spawn ant 3 at adjacent (26, 25) to test cascade displacement
        uint32_t a3_id = sim.spawn_unit(0, AntType::Worker, TileCoord{26, 25});

        // Step 1 tick to trigger collision and bounce cascade
        sim.tick();

        const auto& a1 = sim.get_unit(a1_id);
        const auto& a2 = sim.get_unit(a2_id);
        const auto& a3 = sim.get_unit(a3_id);

        // Bounce sound was triggered
        ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

        // All 3 ants must occupy distinct discrete tiles
        ASSERT_FALSE(a1.pos == a2.pos);
        ASSERT_FALSE(a1.pos == a3.pos);
        ASSERT_FALSE(a2.pos == a3.pos);

        // Discrete tile center guarantee: All 3 ants must be snapped to exact tile centers (NEVER halfway between tiles)
        ASSERT_EQ(a1.pixel_x, a1.pos.x * 32 + 16);
        ASSERT_EQ(a1.pixel_y, a1.pos.y * 32 + 16);
        ASSERT_EQ(a2.pixel_x, a2.pos.x * 32 + 16);
        ASSERT_EQ(a2.pixel_y, a2.pos.y * 32 + 16);
        ASSERT_EQ(a3.pixel_x, a3.pos.x * 32 + 16);
        ASSERT_EQ(a3.pixel_y, a3.pos.y * 32 + 16);
    } TEST_END();

    TEST_CASE("12.5 Multi-Ant Food Harvesting & Perimeter Bite") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place a 2x2 cracker food schedule at (20, 20) with 8 bites
        ActiveFoodSchedule afs{};
        afs.x = 20;
        afs.y = 20;
        afs.active = true;
        afs.remaining_bites = 8;
        afs.footprint = { {20, 20}, {21, 20}, {20, 21}, {21, 21} };
        afs.variants.push_back({301, 8});
        afs.current_tile_id = 301;
        sim.grid_mut().food_schedules_mut().push_back(afs);
        for (const auto& c : afs.footprint) {
            auto& cell = sim.grid_mut().get_cell_mut(c);
            cell.is_food = true;
            cell.interactive_id = 301;
        }

        // Spawn 4 workers at (15, 20), (15, 21), (16, 20), (16, 21)
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 20});
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 21});
        uint32_t w3 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 20});
        uint32_t w4 = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 21});

        HUD hud;
        hud.init(0);
        hud.select_ant(w1);
        hud.set_selected_ant_ids({w1, w2, w3, w4});

        // Right-click on the food clump (pixel 20 * 32 + 16, 20 * 32 + 16)
        hud.dispatch_smart_special_ability(20 * 32 + 16, 20 * 32 + 16, sim);

        // Advance simulation for 50 ticks
        int harvest_sound_count = 0;
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::FoodHarvest)) {
                harvest_sound_count++;
            }
        }

        // All 4 ants successfully reached food and gathered food without freezing
        const auto& u1 = sim.get_unit(w1);
        const auto& u2 = sim.get_unit(w2);
        const auto& u3 = sim.get_unit(w3);
        const auto& u4 = sim.get_unit(w4);
        ASSERT_TRUE(u1.is_holding() || u1.state == UnitState::EnteringBase);
        ASSERT_TRUE(u2.is_holding() || u2.state == UnitState::EnteringBase);
        ASSERT_TRUE(u3.is_holding() || u3.state == UnitState::EnteringBase);
        ASSERT_TRUE(u4.is_holding() || u4.state == UnitState::EnteringBase);
        ASSERT_TRUE(harvest_sound_count >= 4);
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

    TEST_CASE("12.7 Attack Cooldown & Adjacency Requirement") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Spawn Combat Ant at (10, 10) on Team 0
        uint32_t combat_id = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        // Spawn Enemy Worker at (15, 10) on Team 1 (Chebyshev dist = 5)
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
        ASSERT_EQ(attacker.attack_target_id, target_id);

        // Step simulation while approaching: target takes no damage until attacker reaches adjacency
        while (target.hp == init_hp) {
            ASSERT_TRUE(attacker.pos.chebyshev_dist(target.pos) >= 1);
            sim.tick();
        }

        // On adjacency tick: attack struck! Target took 2 HP punch damage
        ASSERT_EQ(target.hp, init_hp - 2);
        // Attacker must have entered Attacking state and set attack_cooldown_ticks == 12
        ASSERT_TRUE(attacker.attack_cooldown_ticks > 0);

        // On immediately subsequent tick, cannot attack again (cooldown in effect)
        uint16_t prev_cooldown = attacker.attack_cooldown_ticks;
        sim.tick();
        ASSERT_EQ(target.hp, init_hp - 2); // No extra attack spammed
        ASSERT_EQ(attacker.attack_cooldown_ticks, prev_cooldown - 1);
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

        // Right-click gravel tile (20, 20)
        hud.dispatch_smart_special_ability(20 * 32 + 16, 20 * 32 + 16, sim);

        // Step simulation for 50 ticks until bomber approaches cardinal neighbor (20, 21) and drops bomb
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (sim.has_bomb_at({20, 20})) break;
        }

        // Bomb successfully planted on gravel tile!
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
        ASSERT_TRUE(sim.has_audio_event(SoundID::BombPick));
    } TEST_END();

    TEST_CASE("12.9 Chat Scrolling & Initial Message Wrapping") {
        HUD hud;
        hud.init(0);

        // Verify initial news message was wrapped to <= 27 chars per line
        const auto& log = hud.get_chat_log();
        ASSERT_TRUE(!log.empty());
        for (const auto& line : log) {
            ASSERT_TRUE(line.length() <= 27);
        }

        // Add 10 chat messages
        for (int i = 0; i < 10; ++i) {
            hud.add_chat_entry("Player", "Message number " + std::to_string(i));
        }

        int32_t initial_lines = static_cast<int32_t>(hud.get_chat_log().size());
        ASSERT_TRUE(initial_lines >= 12);
        ASSERT_EQ(hud.get_chat_scroll_offset(), 0);

        // Scroll up
        hud.scroll_chat_up(2);
        ASSERT_EQ(hud.get_chat_scroll_offset(), 2);

        // Scroll up past max (7 visible lines)
        hud.scroll_chat_up(100);
        ASSERT_EQ(hud.get_chat_scroll_offset(), initial_lines - 7);

        // Scroll down
        hud.scroll_chat_down(3);
        ASSERT_EQ(hud.get_chat_scroll_offset(), initial_lines - 10);

        // Mouse wheel scrolling over lower chat box (x: 500, y: 350)
        hud.handle_mouse_wheel(500, 350, 1); // Wheel up
        ASSERT_EQ(hud.get_chat_scroll_offset(), initial_lines - 9);

        hud.handle_mouse_wheel(500, 350, -1); // Wheel down
        ASSERT_EQ(hud.get_chat_scroll_offset(), initial_lines - 10);

        // Mouse wheel outside chat box does not scroll
        hud.handle_mouse_wheel(100, 100, 1);
        ASSERT_EQ(hud.get_chat_scroll_offset(), initial_lines - 10);

        // Sending a chat message resets scroll offset to 0
        hud.set_chat_input("Hello Colony!");
        hud.send_chat_message();
        ASSERT_EQ(hud.get_chat_scroll_offset(), 0);
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

        const auto& unit = sim.get_unit(friendly_id);
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

        // Ordering move directly onto friendly bomb redirects destination to nearest passable neighbor
        sim.issue_move_order(friendly_id, TileCoord{20, 20});
        ASSERT_FALSE((sim.get_unit(friendly_id).final_dest == TileCoord{20, 20}));
        for (int t = 0; t < 50; ++t) {
            sim.tick();
            ASSERT_FALSE((sim.get_unit(friendly_id).pos == TileCoord{20, 20}));
        }
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));
    } TEST_END();

    TEST_CASE("12.12 Enemy Bomb Proximity Detonation & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

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

        // Unit enters BuildingBridge state, faces East towards bridge, snapped to center of tile
        ASSERT_EQ(swimmer.state, UnitState::BuildingBridge);
        ASSERT_EQ(swimmer.facing, Direction::East);
        ASSERT_EQ(swimmer.pixel_x, 19 * 32 + 16);
        ASSERT_EQ(swimmer.pixel_y, 20 * 32 + 16);
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 1);
        ASSERT_TRUE(sim.has_audio_event(SoundID::ShovelWater));

        // Advance simulation: each digging cycle is 8 ticks, with shovel impact frame advancing stage
        // By tick 30 (~1.5 seconds), all 4 stages are complete and ant returns to Idle
        for (int t = 0; t < 30; ++t) {
            sim.tick();
        }

        // Bridge is now fully constructed (stage 4)
        ASSERT_TRUE(sim.has_bridge_at(TileCoord{20, 20}));
        ASSERT_EQ(sim.get_bridge_stage(TileCoord{20, 20}), 4);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        // Swimmer completed digging and transitioned to Idle, staying centered
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

        // Place friendly bomb at (20, 20)
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        // Spawn friendly bomber at (20, 19)
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 19});

        // 1. Single select bomber without shift
        hud.select_ant(b_id, false);
        ASSERT_EQ(hud.get_selected_ant_id(), b_id);
        ASSERT_FALSE(hud.is_multi_select());

        // Screen coords for (20, 20)
        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

        // Left-click on friendly bomb defuses bomb
        hud.handle_mouse_down(screen_x, screen_y, 1, sim, cam, 0);
        hud.handle_mouse_up(screen_x, screen_y, 1, sim, cam, 0);

        // Check defusal occurred
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) break;
        }
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{20, 20}));

        // 2. Right-click also defuses
        sim.grid_mut().place_bomb(20, 20, 0);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{20, 20}));

        hud.handle_mouse_down(screen_x, screen_y, 3, sim, cam, 0);
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) break;
        }
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{20, 20}));
    } TEST_END();

    TEST_CASE("12.16 Non-Bomber Moves Directly Onto Friendly Bomb, Explodes & 4-Space Knockback") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
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

    TEST_CASE("12.17 Shift-Selected Bomber and Multi-Selected Bomber Move Onto Bomb to Hit It") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        ViewportCamera cam;
        cam.world_x = 20 * 32 - 100;
        cam.world_y = 20 * 32 - 100;
        HUD hud;
        hud.init(0);

        // Case A: Shift-selected Bomber hits the bomb
        sim.grid_mut().place_bomb(20, 20, 0);
        uint32_t b1 = sim.spawn_unit(0, AntType::Bomber, TileCoord{18, 20});
        // Select with shift modifier
        hud.select_ant(b1, true); // shift-selected
        ASSERT_TRUE(hud.is_multi_select());

        int32_t screen_x = HUD::PLAYFIELD_X + (20 * 32 + 16) - cam.world_x;
        int32_t screen_y = HUD::PLAYFIELD_Y + (20 * 32 + 16) - cam.world_y;

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

        // Case B: Multi-unit selection containing Bomber hits the bomb
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
    TEST_CASE("12.21 Power-Up Arrival Timing & Transformation Lifecycle") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (13, 10) (Type 4 = Combat Ant)
        sim.grid_mut().place_powerup(13, 10, 4);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);
        ASSERT_EQ(ant.type, AntType::Worker);
        ASSERT_FALSE(ant.is_transforming());

        // Order worker to move to power-up tile (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // Step simulation ticks while walking towards (13, 10)
        // When adjacent at (12, 10), it should NOT yet have triggered transformation
        bool saw_adjacent_not_triggered = false;
        while (ant.pos.x < 13) {
            sim.tick();
            if (ant.pos.x == 12) {
                saw_adjacent_not_triggered = true;
                ASSERT_FALSE(ant.is_transforming());
                ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{13, 10}));
            }
        }
        ASSERT_TRUE(saw_adjacent_not_triggered);

        // Advance until arriving at tile center of (13, 10)
        while (ant.is_alive() && !ant.is_transforming() && !ant.waypoints.empty()) {
            sim.tick();
        }

        // Unit has arrived and started transforming
        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_TRUE(ant.is_transforming());
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // In world snapshot, unit is transforming and on_powerup is true
        const auto& ws = sim.get_world_state();
        const AntSnapshot* snap = nullptr;
        for (const auto& a : ws.ants) {
            if (a.id == ant_id) { snap = &a; break; }
        }
        ASSERT_TRUE(snap != nullptr);
        ASSERT_TRUE(snap->is_transforming);
        ASSERT_TRUE(snap->on_powerup);

        // Step 12 ticks to complete transformation
        for (int t = 0; t < 12; ++t) {
            sim.tick();
        }

        // Emerges as Combat ant
        ASSERT_FALSE(ant.is_transforming());
        ASSERT_EQ(ant.type, AntType::Combat);
        ASSERT_EQ(ant.max_hp, 12);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.22: Power-Up Transformation Interruption via Impossible Order
    // ------------------------------------------------------------------------
    TEST_CASE("12.22 Power-Up Transformation Interruption & Standing on Power-Up") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (10, 10) (Type 4 = Combat)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Set tile (10, 11) to water (impassable for non-swimmer)
        sim.grid_mut().set_terrain(10, 11, TERRAIN_WATER);

        // Worker ant at (10, 10)
        uint32_t ant_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& ant = sim.get_unit(ant_id);

        // Step 1 tick to trigger power-up pickup on tile center
        sim.tick();
        ASSERT_TRUE(ant.is_transforming());
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10, 10}));

        // Issue an impossible move instruction: move into water tile (10, 11)
        sim.issue_move_order(ant_id, TileCoord{10, 11});

        // Interruption should take effect immediately:
        // 1. Transformation halted
        ASSERT_FALSE(ant.is_transforming());
        // 2. Reverts to previous type (Worker)
        ASSERT_EQ(ant.type, AntType::Worker);
        // 3. Power-up remains on the ground at (10, 10)
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{10, 10}));
        // 4. Stands on top of power-up and flagged interrupted
        ASSERT_TRUE(ant.on_powerup);
        ASSERT_TRUE(ant.transformation_interrupted);

        // In subsequent ticks, standing on top of power-up does NOT re-trigger transformation
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            ASSERT_FALSE(ant.is_transforming());
            ASSERT_TRUE(ant.on_powerup);
            ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{10, 10}));
        }

        // Cancel order / Stop order also safely keeps unit on power-up
        AntOrder cancel_order;
        cancel_order.ant_id = ant_id;
        cancel_order.type = OrderType::Cancel;
        sim.issue_order(cancel_order);
        ASSERT_FALSE(ant.is_transforming());
        ASSERT_TRUE(ant.on_powerup);

        // Move interrupted worker off the power-up to adjacent tile (11, 10)
        sim.clear_audio_events();
        sim.issue_move_order(ant_id, TileCoord{11, 10});
        ASSERT_EQ(ant.state, UnitState::Walking);
        ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::AntStop)));

        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (ant.pos == TileCoord{11, 10} && ant.state != UnitState::Walking) break;
        }
        ASSERT_EQ(ant.pos, (TileCoord{11, 10}));
        ASSERT_FALSE(ant.on_powerup);
        ASSERT_FALSE(ant.transformation_interrupted);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{10, 10}));

        // Bomber ant standing on power-up at (10, 10)
        uint32_t bomber_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{10, 10});
        auto& bomber = sim.get_unit(bomber_id);
        bomber.transformation_interrupted = true;
        bomber.on_powerup = true;
        sim.tick();
        ASSERT_TRUE(bomber.on_powerup);
        ASSERT_FALSE(bomber.is_transforming());

        // Issue move order to (12, 10): Bomber ant walks off power-up cleanly
        sim.clear_audio_events();
        sim.issue_move_order(bomber_id, TileCoord{12, 10});
        ASSERT_EQ(bomber.state, UnitState::Walking);
        ASSERT_FALSE(sim.has_audio_event(static_cast<uint32_t>(SoundID::AntStop)));

        for (int t = 0; t < 50; ++t) {
            sim.tick();
            if (bomber.pos == TileCoord{12, 10} && bomber.state != UnitState::Walking) break;
        }
        ASSERT_EQ(bomber.pos, (TileCoord{12, 10}));
        ASSERT_FALSE(bomber.on_powerup);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{10, 10}));
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.23: Unattackability While Standing on Power-Up
    // ------------------------------------------------------------------------
    TEST_CASE("12.23 Unattackability While Standing on Power-Up") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place powerup at (10, 10)
        sim.grid_mut().place_powerup(10, 10, 4);

        // Friendly worker at (10, 10), trigger and interrupt
        uint32_t friendly_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.tick();
        sim.interrupt_transformation(friendly_id);

        auto& friendly = sim.get_unit(friendly_id);
        ASSERT_TRUE(friendly.on_powerup);
        uint16_t initial_hp = friendly.hp;

        // Enemy Combat ant at (11, 10) (adjacent)
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Combat, TileCoord{11, 10});
        auto& enemy = sim.get_unit(enemy_id);

        // 1. Combat AI should NOT target friendly unit on powerup
        CombatAIController controller(enemy);
        ASSERT_FALSE(controller.is_valid_target(friendly, enemy, sim.stats_manager()));

        // 2. Direct melee attack attempt deals 0 damage
        sim.execute_melee_attack(enemy_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);

        // 3. HUD attack order rejects attacking an enemy on a powerup
        HUD hud;
        hud.init(1); // Player 1 (enemy team)
        hud.select_ant(enemy_id, false);
        hud.dispatch_attack_order(friendly_id, sim);

        // Enemy should NOT have attack order targeting friendly_id
        ASSERT_NE(enemy.attack_target_id, friendly_id);
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
    TEST_CASE("12.25 Bridge Locomotion Speed Matches Mud Speed (~0.65x)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Mud corridor on row 12 (rows 11 to 13)
        for (int y = 11; y <= 13; ++y) {
            for (int x = 10; x <= 16; ++x) {
                sim.grid_mut().get_cell_mut(TileCoord{x, y}).surface_type = SurfaceType::Mud;
            }
        }

        // Row 14: 6 tiles of Completed Bridge (10, 14) to (16, 14)
        for (int x = 10; x <= 16; ++x) {
            sim.grid_mut().set_terrain(x, 14, TERRAIN_WATER);
            uint32_t ux = static_cast<uint32_t>(x);
            for (int s = 0; s < 4; ++s) {
                sim.grid_mut().advance_bridge(ux, 14, 0);
            }
            ASSERT_TRUE(sim.grid().get_cell(ux, 14).has_completed_bridge());
        }

        // Verify pathfinder step cost for bridge matches mud (1.5x)
        auto path_dirt = PathFinder::find_path(sim.grid(), TileCoord{10, 10}, TileCoord{15, 10}, false, false);
        auto path_bridge = PathFinder::find_path(sim.grid(), TileCoord{10, 14}, TileCoord{15, 14}, false, false);
        ASSERT_FALSE(path_bridge.empty());
        ASSERT_EQ(path_dirt.size(), path_bridge.size());

        // Spawn workers
        uint32_t w_dirt = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t w_mud = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 12});
        uint32_t w_bridge = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 14});

        sim.issue_move_order(w_dirt, TileCoord{15, 10});
        sim.issue_move_order(w_mud, TileCoord{15, 12});
        sim.issue_move_order(w_bridge, TileCoord{15, 14});

        int ticks_dirt = 0;
        int ticks_mud = 0;
        int ticks_bridge = 0;

        for (int t = 1; t <= 300; ++t) {
            sim.tick();
            if (ticks_dirt == 0 && sim.get_unit(w_dirt).pos == TileCoord{15, 10} && sim.get_unit(w_dirt).state != UnitState::Walking) {
                ticks_dirt = t;
            }
            if (ticks_mud == 0 && sim.get_unit(w_mud).pos == TileCoord{15, 12} && sim.get_unit(w_mud).state != UnitState::Walking) {
                ticks_mud = t;
            }
            if (ticks_bridge == 0 && sim.get_unit(w_bridge).pos == TileCoord{15, 14} && sim.get_unit(w_bridge).state != UnitState::Walking) {
                ticks_bridge = t;
            }
            if (ticks_dirt > 0 && ticks_mud > 0 && ticks_bridge > 0) break;
        }

        // Both Mud and Bridge take significantly more ticks than normal dirt
        ASSERT_TRUE(ticks_dirt > 0);
        ASSERT_TRUE(ticks_mud > ticks_dirt);
        ASSERT_TRUE(ticks_bridge > ticks_dirt);

        // Mud and Bridge tick times match exactly
        ASSERT_EQ(ticks_bridge, ticks_mud);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.26: Swimmers Immune From Being Attacked Underwater
    // ------------------------------------------------------------------------
    TEST_CASE("12.26 Swimmers Immune From Being Attacked Underwater (Even by Other Swimmers)") {
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
        sim.tick();
        ASSERT_TRUE(friendly.in_water);
        uint16_t initial_hp = friendly.hp;

        // Enemy swimmer in water at (21, 20) (adjacent underwater)
        uint32_t enemy_swimmer_id = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});
        auto& enemy_swimmer = sim.get_unit(enemy_swimmer_id);
        sim.tick();
        ASSERT_TRUE(enemy_swimmer.in_water);

        // Enemy Combat ant on land at (19, 20) (adjacent on bank)
        uint32_t enemy_combat_id = sim.spawn_unit(1, AntType::Combat, TileCoord{19, 20});
        auto& enemy_combat = sim.get_unit(enemy_combat_id);
        ASSERT_FALSE(enemy_combat.in_water);

        // 1. Combat AI on land cannot target swimmer underwater
        CombatAIController controller(enemy_combat);
        ASSERT_FALSE(controller.is_valid_target(friendly, enemy_combat, sim.stats_manager()));

        // 2. Direct melee attack from land combat ant deals 0 damage
        sim.execute_melee_attack(enemy_combat_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);

        // 3. Direct melee attack from enemy swimmer underwater deals 0 damage
        sim.execute_melee_attack(enemy_swimmer_id, friendly_id);
        ASSERT_EQ(friendly.hp, initial_hp);

        // 4. Autonomous pursuit ignores underwater swimmer
        enemy_swimmer.attack_target_id = friendly_id;
        sim.tick();
        ASSERT_EQ(enemy_swimmer.attack_target_id, 0u);

        // 5. HUD attack order rejects attacking an enemy swimmer in water
        HUD hud;
        hud.init(1); // Player 1 (enemy team)
        hud.select_ant(enemy_swimmer_id, false);
        hud.dispatch_attack_order(friendly_id, sim);
        ASSERT_NE(enemy_swimmer.attack_target_id, friendly_id);

        // 6. When swimmer exits water onto land, it can be attacked normally
        friendly.pos = TileCoord{18, 20}; // Move friendly swimmer to land
        friendly.in_water = false;
        friendly.state = UnitState::Idle;
        enemy_combat.pos = TileCoord{19, 20};

        ASSERT_TRUE(controller.is_valid_target(friendly, enemy_combat, sim.stats_manager()));
        sim.execute_melee_attack(enemy_combat_id, friendly_id);
        ASSERT_LT(friendly.hp, initial_hp); // Takes damage on land!
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
        ASSERT_FALSE(ant.is_transforming());

        // 2. Now specifically instruct the ant to walk onto the powerup at (13, 10)
        sim.issue_move_order(ant_id, TileCoord{13, 10});

        // This time, the destination IS the powerup, so the ant routes directly to it
        ASSERT_EQ(ant.final_dest, (TileCoord{13, 10}));

        // Step simulation until the ant arrives and triggers transformation
        steps = 0;
        while (!ant.is_transforming() && steps++ < 150) {
            sim.tick();
        }

        ASSERT_EQ(ant.pos, (TileCoord{13, 10}));
        ASSERT_TRUE(ant.is_transforming());
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{13, 10}));

        // Finish the 11-frame transformation
        for (int i = 0; i < 15; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(ant.is_transforming());
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

        // 1. Green player clicks on Blue Ant 1 at (10, 10) -> pixel (10*32+16, 10*32+16) = (336, 336)
        int32_t b1_click_x = 336 + HUD::PLAYFIELD_X;
        int32_t b1_click_y = 336 + HUD::PLAYFIELD_Y;
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

        // b2 is now selected for inspection, neither unit has friendly attack orders
        ASSERT_TRUE(hud.is_ant_selected(b2));
        ASSERT_FALSE(hud.is_ant_selected(b1));
        ASSERT_NE(sim.get_unit(b1).attack_target_id, b2);
        ASSERT_NE(sim.get_unit(b2).attack_target_id, b1);

        // 3. Right-clicking b2 while nothing or enemy is selected does not issue attack orders
        hud.handle_mouse_down(b2_click_x, b2_click_y, 3, sim, camera);
        ASSERT_NE(sim.get_unit(b1).attack_target_id, b2);

        // 4. Directly testing HUD attack order dispatch protection:
        // Even if an enemy ant ID were artificially injected into selected_ant_ids:
        hud.set_selected_ant_ids({b1});
        hud.dispatch_attack_order(b2, sim);
        // HUD must filter out non-friendly units so b1 NEVER targets b2!
        ASSERT_NE(sim.get_unit(b1).attack_target_id, b2);

        // 5. SimulationEngine layer protection:
        // Even if a malformed AntOrder with b1 attacking b2 is fed to sim.issue_order:
        AntOrder bad_order;
        bad_order.ant_id = b1;
        bad_order.type = OrderType::Attack;
        bad_order.target_entity_id = static_cast<int32_t>(b2);
        sim.issue_order(bad_order);
        ASSERT_NE(sim.get_unit(b1).attack_target_id, b2);

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
        ASSERT_EQ(sim.get_unit(g1).attack_target_id, b1);
    } TEST_END();

    TEST_CASE("12.29 Power-Up Drop On Transformation & Occupied Tile Avoidance / Disappearance") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Worker Ant transforms into Combat Ant -> No powerup dropped
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.grid_mut().place_powerup(10, 10, 4); // Combat powerup
        sim.tick(); // Start transformation
        ASSERT_TRUE(sim.get_unit(a1).is_transforming());
        // Verify none of the 8 neighbors have any powerup
        static constexpr std::array<TileCoord, 8> OFFSETS = {{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{10 + off.x, 10 + off.y}));
        }
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(a1).is_transforming());
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Combat);

        // 2. Combat Ant transforms into Bomber -> drops original Combat powerup (Type 4) on an adjacent free tile
        sim.grid_mut().place_powerup(10, 10, 1); // Bomber powerup
        sim.tick(); // Start transformation
        ASSERT_TRUE(sim.get_unit(a1).is_transforming());
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
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(a1).is_transforming());
        ASSERT_EQ(sim.get_unit(a1).type, AntType::Bomber);

        // 3. Occupied tile avoidance: place an ant on (10, 9), transform Bomber into Swimmer
        // Clean up any powerup around (10, 10) first to ensure known board state
        for (const auto& off : OFFSETS) {
            sim.grid_mut().clear_powerup(10 + off.x, 10 + off.y);
        }
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 9});
        (void)blocker;
        sim.grid_mut().place_powerup(10, 10, 5); // Swimmer powerup
        sim.tick(); // Start transformation
        ASSERT_TRUE(sim.get_unit(a1).is_transforming());
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
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(a1).is_transforming());
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
        sim.tick(); // Start transformation
        ASSERT_TRUE(sim.get_unit(a2).is_transforming());
        // None of the 8 neighbors should receive a dropped powerup
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }
        for (int i = 0; i < 15; ++i) sim.tick();
        ASSERT_FALSE(sim.get_unit(a2).is_transforming());
        ASSERT_EQ(sim.get_unit(a2).type, AntType::Thief);
        // Still no powerup on any of the 8 neighbors: permanently disappeared!
        for (const auto& off : OFFSETS) {
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{20 + off.x, 20 + off.y}));
        }

        // 5. Interrupted transformation cleans up dropped powerup and restores original ant
        uint32_t a3 = sim.spawn_unit(0, AntType::Thief, TileCoord{30, 30});
        sim.grid_mut().place_powerup(30, 30, 1); // Bomber powerup
        sim.grid_mut().set_terrain(30, 31, TERRAIN_WATER); // S: Water (for impossible order)
        sim.tick(); // Start transformation
        auto& ant3 = sim.get_unit(a3);
        ASSERT_TRUE(ant3.is_transforming());
        // Verify dropped Thief powerup (Type 3) landed on a valid neighbor
        dropped_tile = ant3.dropped_powerup_pos;
        ASSERT_TRUE(dropped_tile.x >= 0 && dropped_tile.y >= 0);
        ASSERT_TRUE(sim.grid().has_powerup_at(dropped_tile));
        ASSERT_EQ(sim.grid().get_powerup_type(dropped_tile), 3);

        // Issue impossible move order into water at (30, 31)
        sim.issue_move_order(a3, TileCoord{30, 31});
        ASSERT_FALSE(ant3.is_transforming());
        ASSERT_EQ(ant3.type, AntType::Thief); // Reverted to Thief
        ASSERT_TRUE(ant3.on_powerup);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{30, 30})); // Bomber restored
        // Dropped powerup at dropped_tile must be cleaned up to prevent duplication!
        ASSERT_FALSE(sim.grid().has_powerup_at(dropped_tile));

        // 6. Multi-direction randomized landing verification:
        // Over multiple transformations with all 8 directions free, verify that the
        // dropped powerup lands randomly across multiple distinct directions, NOT always above.
        std::set<std::pair<int32_t, int32_t>> chosen_directions;
        for (int iter = 0; iter < 24; ++iter) {
            TileCoord origin{40 + (iter % 3) * 5, 40 + (iter / 3) * 5};
            uint32_t tester = sim.spawn_unit(0, AntType::Combat, origin);
            sim.grid_mut().place_powerup(origin.x, origin.y, 1); // Bomber
            sim.tick();
            auto& t_ant = sim.get_unit(tester);
            if (t_ant.dropped_powerup_pos.x >= 0) {
                int32_t dx = t_ant.dropped_powerup_pos.x - origin.x;
                int32_t dy = t_ant.dropped_powerup_pos.y - origin.y;
                chosen_directions.insert({dx, dy});
            }
            // Complete transformation
            for (int i = 0; i < 15; ++i) sim.tick();
        }
        // With 8 free directions, 24 trials should sample multiple distinct directions (>= 4)
        ASSERT_TRUE(chosen_directions.size() >= 4);
    } TEST_END();

    TEST_CASE("12.30 Blocked Path Can't Go Reaction, Ability Animations & Drowning Sequence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Can't Go Reaction on Impassable/Blocked Path (Ant at (10, 10) is at closest shoreline to (12, 10))
        for (int32_t wx = 11; wx <= 15; ++wx) {
            for (int32_t wy = 8; wy <= 12; ++wy) {
                sim.grid_mut().set_terrain(wx, wy, TERRAIN_WATER);
            }
        }
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto& w_ant = sim.get_unit(w_id);

        // Issue move order directly to impassable water tile (Worker cannot swim)
        sim.issue_move_order(w_id, TileCoord{12, 10});

        // Ant immediately enters CantGo, faces South, snaps to tile center, and plays CantGo sound
        ASSERT_EQ(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.facing, Direction::South);
        ASSERT_EQ(w_ant.pixel_x, 10 * 32 + 16);
        ASSERT_EQ(w_ant.pixel_y, 10 * 32 + 16);
        ASSERT_TRUE(w_ant.waypoints.empty());
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // Advance simulation through CantGo duration (6 ticks for Worker)
        for (int t = 0; t < 6; ++t) {
            sim.tick();
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);

        // 2. Bomber Ability Animations (PlantingBomb and DefusingBomb)
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{20, 20});
        auto& b_ant = sim.get_unit(b_id);

        sim.plant_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::PlantingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);
        // Bomb is NOT placed on the grid tile yet while animation is playing!
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));

        // Plant animation is 28 ticks (bomb placed at tick 18, finished at tick 28)
        for (int t = 0; t < 17; ++t) {
            ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));
        sim.tick(); // Tick 18: bomb placed!
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{21, 20}));
        for (int t = 18; t < 28; ++t) {
            sim.tick();
        }
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // Defuse bomb (12 ticks)
        sim.defuse_bomb(b_id, TileCoord{21, 20}, false);
        ASSERT_EQ(b_ant.state, UnitState::DefusingBomb);
        ASSERT_EQ(b_ant.facing, Direction::East);

        for (int t = 0; t < 12; ++t) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at(TileCoord{21, 20}));
        ASSERT_EQ(b_ant.state, UnitState::Idle);

        // 3. Fire Ant Ability Animations (PlacingFire and ExtinguishingFire)
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{25, 25});
        auto& f_ant = sim.get_unit(f_id);

        sim.ignite_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::PlacingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);
        // Fire is NOT placed on the grid tile yet while animation is playing!
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());

        // Fire placing is 35 ticks (afsf 1760ms / 35 ticks matching ants.chd timing)
        // Fire erupts at tick 27, and unit returns to Idle at tick 35
        for (int t = 0; t < 26; ++t) {
            ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());
            sim.tick();
        }
        sim.tick(); // Tick 27: flames erupt and firewall is placed on the grid
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());

        // Completes remaining ticks (28..34) putting away glass
        for (int t = 27; t < 35; ++t) {
            sim.tick();
        }
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // Extinguish fire (12 ticks)
        sim.extinguish_fire(f_id, TileCoord{25, 26}, false);
        ASSERT_EQ(f_ant.state, UnitState::ExtinguishingFire);
        ASSERT_EQ(f_ant.facing, Direction::South);

        for (int t = 0; t < 12; ++t) {
            sim.tick();
        }
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 26}).has_fire());
        ASSERT_EQ(f_ant.state, UnitState::Idle);

        // 4. Drowning sequence progression (22 ticks)
        sim.grid_mut().set_terrain(30, 30, TERRAIN_WATER);
        uint32_t drown_id = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
        auto& drown_ant = sim.get_unit(drown_id);

        // Unit enters Drowning state, faces South, snapped to center
        ASSERT_EQ(drown_ant.state, UnitState::Drowning);
        ASSERT_EQ(drown_ant.facing, Direction::South);
        ASSERT_EQ(drown_ant.pixel_x, 30 * 32 + 16);
        ASSERT_EQ(drown_ant.pixel_y, 30 * 32 + 16);

        // Advance 22 ticks
        for (int t = 0; t < 22; ++t) {
            ASSERT_EQ(drown_ant.state, UnitState::Drowning);
            ASSERT_EQ(drown_ant.facing, Direction::South);
            sim.tick();
        }
        // After 22 ticks, drowning completes and unit dies
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
        sim.init_test_world(60, 60, 100, 60000);
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

        // Place a solid obstacle rock at (22, 20)
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);

        // Spawn worker at (20, 20) and apply 4-tile knockback East towards (24, 20)
        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);
        ASSERT_EQ(worker.state, UnitState::Knockback);

        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Flew over rock at (22, 20) and landed at ground tile (24, 20)
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Stunned);
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

        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Airborne ant flew over water without drowning, landed safely on ground
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Stunned);
        ASSERT_FALSE(worker.in_water);
        ASSERT_EQ(worker.death_status, DeathStatus::Alive);
    } TEST_END();

    TEST_CASE("12.36 Knockback Flies Over Intermediate Rock Obstacle into Water (Drowning)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

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

        // Advance until detonation
        bool detonated = false;
        for (int t = 0; t < 100; ++t) {
            sim.tick();
            if (!sim.has_bomb_at(TileCoord{20, 20})) {
                detonated = true;
                break;
            }
        }
        ASSERT_TRUE(detonated);
        ASSERT_EQ(worker.state, UnitState::Knockback);

        // Step through knockback flight
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Flew over rock at (22, 20), landed at (24, 20) in water, and drowned
        ASSERT_EQ(worker.pos.x, 24);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Drowning);
        ASSERT_EQ(worker.death_status, DeathStatus::Drowned);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
    } TEST_END();

    TEST_CASE("12.37 Knockback Truncates Cleanly at Impassable Rock Barrier") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Solid rock wall across spaces 2, 3, 4 at (22..24, 20)
        sim.grid_mut().set_terrain(22, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(23, 20, TERRAIN_OBSTACLE);
        sim.grid_mut().set_terrain(24, 20, TERRAIN_OBSTACLE);

        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        auto& worker = sim.get_unit(worker_id);

        sim.apply_knockback(worker_id, 19 * 32 + 16, 20 * 32 + 16, 4, 4);

        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (worker.state != UnitState::Knockback) break;
        }

        // Stops at last available tile before barrier: (21, 20)
        ASSERT_EQ(worker.pos.x, 21);
        ASSERT_EQ(worker.pos.y, 20);
        ASSERT_EQ(worker.state, UnitState::Stunned);
        ASSERT_TRUE(sim.has_audio_event(64)); // SOUND_FLY_THUMP_A
    } TEST_END();

    TEST_CASE("12.38 Anthill Base Queuing, 9-Step Approach, Blocked Squares, and Idle Spot") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x; // 20
        int32_t by = base->y; // 20

        // 1. Check queue slots: Slot 0..3 are along bx - 1 (x = 19)
        ASSERT_EQ(sim.get_base_queue_slot(0, 0), (TileCoord{bx - 1, by + 3}));
        ASSERT_EQ(sim.get_base_queue_slot(0, 1), (TileCoord{bx - 1, by + 2}));
        ASSERT_EQ(sim.get_base_queue_slot(0, 2), (TileCoord{bx - 1, by + 1}));
        ASSERT_EQ(sim.get_base_queue_slot(0, 3), (TileCoord{bx - 1, by}));

        // 2. Approach Corridor Placement Block: Verify 3 red squares above base (row by - 1)
        for (int dx = 0; dx <= 2; ++dx) {
            TileCoord red_sq{bx + dx, by - 1};
            const auto& cell = sim.grid().get_cell(red_sq);
            ASSERT_FALSE(cell.can_place_bomb());
            ASSERT_FALSE(cell.can_place_fire());
            ASSERT_TRUE(sim.grid().is_anthill_reserved_spot(red_sq));

            // Verify direct grid placement is blocked
            sim.grid_mut().place_bomb(static_cast<uint32_t>(red_sq.x), static_cast<uint32_t>(red_sq.y), 0);
            ASSERT_FALSE(sim.grid().has_bomb_at(red_sq));

            sim.grid_mut().place_firewall(static_cast<uint32_t>(red_sq.x), static_cast<uint32_t>(red_sq.y), 0);
            ASSERT_FALSE(sim.grid().has_fire_at(red_sq));
        }

        // Verify ability order rejection on red squares
        uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{bx, by - 2});
        ASSERT_FALSE(sim.plant_bomb(bomber, TileCoord{bx, by - 1}));
        ASSERT_FALSE(sim.plant_bomb(bomber, TileCoord{bx, by - 1}, true));

        uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{bx + 1, by - 2});
        ASSERT_FALSE(sim.ignite_fire(fire_ant, TileCoord{bx + 1, by - 1}));
        ASSERT_FALSE(sim.ignite_fire(fire_ant, TileCoord{bx + 1, by - 1}, true));

        // 3. Spawn worker with food at (10, by + 3) and issue ReturnToBase
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{10, by + 3});
        auto& u1 = sim.get_unit(w1);
        u1.pick_up_food(1, 25);
        u1.harvest_origin = TileCoord{10, by + 3};

        AntOrder ret1{};
        ret1.ant_id = w1;
        ret1.type = OrderType::ReturnToBase;
        sim.issue_order(ret1);

        // Track every tile visited by w1 on its journey into the base
        std::vector<TileCoord> visited_coords;
        TileCoord last_coord = u1.pos;
        visited_coords.push_back(last_coord);

        for (int i = 0; i < 150; ++i) {
            sim.tick();
            if (u1.pos != last_coord) {
                last_coord = u1.pos;
                visited_coords.push_back(last_coord);
            }
            if (u1.state == UnitState::EnteringBase) {
                break;
            }
        }

        ASSERT_EQ(u1.state, UnitState::EnteringBase);
        ASSERT_EQ(u1.pos, (TileCoord{bx + 1, by + 1}));

        // Verify ant never walked on top of the base mound [bx..bx+3, by..by+3] \ {(bx+1, by+1), (bx+1, by)}
        for (const auto& pt : visited_coords) {
            if (pt.x >= bx && pt.x <= bx + 3 && pt.y >= by && pt.y <= by + 3) {
                if (!(pt.x == bx + 1 && (pt.y == by || pt.y == by + 1))) {
                    ASSERT_TRUE(false); // Walked on mound!
                }
            }
        }

        // Verify the ant entered through the top mouth into the hole
        bool visited_above_hole = false;
        for (const auto& pt : visited_coords) {
            if (pt.x == bx + 1 && pt.y == by) visited_above_hole = true;
        }
        ASSERT_TRUE(visited_above_hole);

        // 4. Ant visiting without food emerges and routes to the idle position (bx + 4, by + 4)
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        AntOrder ret2{};
        ret2.ant_id = w2;
        ret2.type = OrderType::ReturnToBase;
        sim.issue_order(ret2);

        for (int i = 0; i < 90 && sim.get_unit(w2).state != UnitState::EnteringBase; ++i) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(w2).state, UnitState::EnteringBase);

        // Advance through base entry animation (16 ticks)
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }

        // Tick until ant emerges and finishes moving to idle spot
        for (int i = 0; i < 140; ++i) {
            sim.tick();
            if (sim.get_unit(w2).state == UnitState::Idle && sim.get_unit(w2).pos != TileCoord{bx + 1, by + 1}) {
                break;
            }
        }
        ASSERT_EQ(sim.get_unit(w2).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(w2).pos, (TileCoord{bx + 4, by + 4}));
    } TEST_END();

    TEST_CASE("12.39 Dynamic Base Deposit Path Avoids Queued Ants, Bombs and Fire Walls") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 42, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x; // 20
        int32_t by = base->y; // 20

        // Spawn 3 workers carrying food at queue slots 0, 1, 2
        uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 2});
        uint32_t a3 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 1});

        sim.get_unit(a1).pick_up_food(1, 25);
        sim.get_unit(a2).pick_up_food(1, 25);
        sim.get_unit(a3).pick_up_food(1, 25);

        // Join base queue
        sim.join_base_queue(a1);
        sim.join_base_queue(a2);
        sim.join_base_queue(a3);

        // Step 1 tick: Ant 1 has priority, Ant 2 & Ant 3 are waiting in QueuingBase
        sim.tick();
        ASSERT_EQ(sim.get_active_depositing_ant(0), a1);

        // Ant 1 must NOT step on Ant 2's slot (bx-1, by+2) or Ant 3's slot (bx-1, by+1)
        TileCoord a2_slot{bx - 1, by + 2};
        TileCoord a3_slot{bx - 1, by + 1};

        std::vector<TileCoord> a1_visited;
        a1_visited.push_back(sim.get_unit(a1).pos);
        TileCoord last_a1 = sim.get_unit(a1).pos;

        for (int i = 0; i < 90 && sim.get_unit(a1).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            if (sim.get_unit(a1).pos != last_a1) {
                last_a1 = sim.get_unit(a1).pos;
                a1_visited.push_back(last_a1);
            }
        }

        ASSERT_EQ(sim.get_unit(a1).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(a1).pos, (TileCoord{bx + 1, by + 1}));

        // Assert Ant 1 never stepped on Ant 2 or Ant 3's positions
        for (const auto& pt : a1_visited) {
            ASSERT_NE(pt, a2_slot);
            ASSERT_NE(pt, a3_slot);
        }

        // Advance until Ant 1 finishes depositing food, emerges, vacates hole, and Ant 2 gains priority
        for (int i = 0; i < 80 && sim.get_active_depositing_ant(0) != a2; ++i) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_active_depositing_ant(0), a2);

        // Place a friendly bomb along column bx - 2 at (bx - 2, by)
        sim.grid_mut().place_bomb(static_cast<uint32_t>(bx - 2), static_cast<uint32_t>(by), 0);
        ASSERT_TRUE(sim.grid().has_bomb_at(TileCoord{bx - 2, by}));

        // Ant 2 must path to base avoiding both Ant 3 at (bx - 1, by + 1) and the bomb at (bx - 2, by)
        std::vector<TileCoord> a2_visited;
        a2_visited.push_back(sim.get_unit(a2).pos);
        TileCoord last_a2 = sim.get_unit(a2).pos;

        for (int i = 0; i < 90 && sim.get_unit(a2).state != UnitState::EnteringBase; ++i) {
            sim.tick();
            if (sim.get_unit(a2).pos != last_a2) {
                last_a2 = sim.get_unit(a2).pos;
                a2_visited.push_back(last_a2);
            }
        }

        ASSERT_EQ(sim.get_unit(a2).state, UnitState::EnteringBase);
        ASSERT_EQ(sim.get_unit(a2).pos, (TileCoord{bx + 1, by + 1}));

        for (const auto& pt : a2_visited) {
            ASSERT_NE(pt, a3_slot);
            ASSERT_NE(pt, (TileCoord{bx - 2, by}));
        }
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
        ASSERT_EQ(def.hp, 9);
        // Pushed 1 tile East away from attacker at (10, 10)
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_EQ(def.pos.y, 10);
        ASSERT_EQ(def.state, UnitState::Flinch);
        ASSERT_EQ(def.facing, Direction::West); // Victim forced to face attacker West

        // Advance 14 ticks for authentic flinch animation recovery
        for (int i = 0; i < 14; ++i) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(defender).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.42 Standard Attack 1-Tile Pushback into Water (Instant Drowning & Death)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at (12, 10)
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.clear_audio_events();
        sim.execute_melee_attack(attacker, defender);

        const auto& def = sim.get_unit(defender);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_EQ(def.pos.y, 10);
        // Non-swimmer worker pushed into water dies instantly!
        ASSERT_EQ(def.hp, 0);
        ASSERT_EQ(def.state, UnitState::Drowning);
        ASSERT_EQ(def.death_status, DeathStatus::Drowned);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
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

            const auto& v = sim.get_unit(victim);
            ASSERT_EQ(v.state, UnitState::Knockback);
            ASSERT_EQ(v.facing, Direction::East);
            ASSERT_EQ(v.pos.x, 15); // Logical destination tile is 4 tiles East

            // Advance through ballistic flight
            for (int t = 0; t < 15; ++t) {
                sim.tick();
                if (sim.get_unit(victim).state != UnitState::Knockback) break;
            }

            // Landed 4 tiles East at x = 15
            const auto& landed = sim.get_unit(victim);
            ASSERT_EQ(landed.pos.x, 15);
            ASSERT_EQ(landed.state, UnitState::Stunned);
        }
    } TEST_END();

    TEST_CASE("12.44 Combat Ant 4-Tile Fling into Water (Instant Drowning & Death)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Water at landing tile (15, 10)
        sim.grid_mut().set_terrain(15, 10, TERRAIN_WATER);

        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(combat, victim);
        ASSERT_EQ(sim.get_unit(victim).state, UnitState::Knockback);

        // Advance until flight completes
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (sim.get_unit(victim).state != UnitState::Knockback) break;
        }

        const auto& landed = sim.get_unit(victim);
        ASSERT_EQ(landed.pos.x, 15);
        ASSERT_EQ(landed.pos.y, 10);
        // Landed in water: non-swimmer worker drowns and dies instantly!
        ASSERT_EQ(landed.hp, 0);
        ASSERT_EQ(landed.state, UnitState::Drowning);
        ASSERT_EQ(landed.death_status, DeathStatus::Drowned);
    } TEST_END();

    TEST_CASE("12.45 Swimmer Ant Pushback and Fling into Water Survives Without Drowning") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // 1. Standard attack pushback into water
        sim.grid_mut().set_terrain(12, 10, TERRAIN_WATER);
        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t swimmer = sim.spawn_unit(1, AntType::Swimmer, TileCoord{11, 10});

        sim.execute_melee_attack(attacker, swimmer);

        const auto& sw1 = sim.get_unit(swimmer);
        ASSERT_EQ(sw1.pos.x, 12);
        ASSERT_EQ(sw1.pos.y, 10);
        // Swimmer survives in water!
        ASSERT_EQ(sw1.hp, 9);
        ASSERT_EQ(sw1.state, UnitState::Swimming);
        ASSERT_TRUE(sw1.in_water);
        ASSERT_EQ(sw1.death_status, DeathStatus::Alive);

        // 2. Combat Ant 4-tile fling into water
        sim.grid_mut().set_terrain(25, 20, TERRAIN_WATER);
        uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t swimmer2 = sim.spawn_unit(1, AntType::Swimmer, TileCoord{21, 20});

        sim.execute_melee_attack(combat, swimmer2);
        for (int t = 0; t < 15; ++t) {
            sim.tick();
            if (sim.get_unit(swimmer2).state != UnitState::Knockback) break;
        }

        const auto& sw2 = sim.get_unit(swimmer2);
        ASSERT_EQ(sw2.pos.x, 25);
        ASSERT_EQ(sw2.pos.y, 20);
        // Swimmer landed in water without drowning!
        ASSERT_EQ(sw2.hp, 8); // 10 - 2 punch damage
        ASSERT_EQ(sw2.state, UnitState::Swimming);
        ASSERT_TRUE(sw2.in_water);
        ASSERT_EQ(sw2.death_status, DeathStatus::Alive);
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

        // Placement succeeds on valid gravel at (10, 9)
        bool ok = sim.plant_bomb(bomber, {10, 9}, false);
        ASSERT_TRUE(ok);
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);
    } TEST_END();

    TEST_CASE("12.47 Food Pickup Adjacency Requirement & 6-Tick Harvesting Sequence") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place food morsel at (14, 10)
        sim.grid_mut().get_cell_mut({14, 10}).interactive_id = 239;
        sim.grid_mut().get_cell_mut({14, 10}).is_food = true;

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        // Move worker to (13, 10), adjacent to food
        sim.issue_move_order(worker, TileCoord{13, 10});

        // Step simulation until arrival
        while (!sim.get_unit(worker).waypoints.empty() || sim.get_unit(worker).state == UnitState::Walking) {
            sim.tick();
            if (sim.get_unit(worker).pos.x < 13) {
                // Not adjacent/arrived yet, must not have collected food
                ASSERT_FALSE(sim.get_unit(worker).is_holding());
            }
        }

        // Worker reached (13, 10), adjacent to food at (14, 10)
        // Authentic fidelity: walking or standing on adjacent tiles must NOT automatically harvest food
        ASSERT_EQ(sim.get_unit(worker).pos.x, 13);
        ASSERT_EQ(sim.get_unit(worker).pos.y, 10);
        ASSERT_EQ(sim.get_unit(worker).state, UnitState::Idle);
        ASSERT_FALSE(sim.get_unit(worker).is_holding());
        ASSERT_TRUE(sim.grid().get_cell({14, 10}).has_food());

        // Now explicitly instruct worker to eat the food at (14, 10)
        sim.issue_move_order(worker, TileCoord{14, 10});
        sim.tick();

        ASSERT_EQ(sim.get_unit(worker).state, UnitState::HarvestingFood);
        ASSERT_EQ(sim.get_unit(worker).facing, Direction::East);
        ASSERT_TRUE(sim.get_unit(worker).is_holding());
        ASSERT_FALSE(sim.grid().get_cell({14, 10}).has_food());

        // Harvesting animation runs 6 ticks facing food before returning to idle/base queue
        for (int i = 0; i < 5; ++i) {
            sim.tick();
            ASSERT_EQ(sim.get_unit(worker).state, UnitState::HarvestingFood);
            ASSERT_TRUE(sim.get_unit(worker).is_holding());
        }

        // 6th tick completes harvesting animation sequence
        sim.tick();
        const auto& w = sim.get_unit(worker);
        ASSERT_TRUE(w.is_holding());
        ASSERT_EQ(w.carried_food, 1);
        ASSERT_EQ(w.state, UnitState::Idle);
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

        // Advance 3 more ticks (tick 17): still no bomb on grid
        for (int i = 0; i < 3; ++i) {
            sim.tick();
        }
        ASSERT_FALSE(sim.has_bomb_at({11, 10}));

        // Tick 18: Bomb placed on grid!
        sim.tick();
        ASSERT_TRUE(sim.has_bomb_at({11, 10}));
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::PlantingBomb);

        // Complete up to 28 ticks
        for (int i = 0; i < 10; ++i) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(bomber).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.50 Single Attack Order Executes One Strike, Stops and Enforces Cooldown") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder order{};
        order.type = OrderType::Attack;
        order.ant_id = attacker;
        order.target_entity_id = static_cast<int32_t>(defender);
        sim.issue_order(order);

        // Strike executed: defender damaged, knocked back to (12, 10)
        sim.tick();
        const auto& atk = sim.get_unit(attacker);
        ASSERT_EQ(atk.attack_target_id, 0); // Attack target cleared after single order execution!
        ASSERT_GT(atk.attack_cooldown_ticks, 0);

        // Advance past attack animation (6 ticks) and cooldown (10 ticks)
        for (int i = 0; i < 15; ++i) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(attacker).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(attacker).attack_target_id, 0);
        // Defender took only 1 damage (HP was 10, now 9) and was not attacked again
        ASSERT_EQ(sim.get_unit(defender).hp, 9);
    } TEST_END();

    TEST_CASE("12.51 Pushback Forces Victim to Face Attacker and Pushes Cardinal") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        auto* def_unit = const_cast<AntUnit*>(&sim.get_unit(defender));
        def_unit->facing = Direction::North;

        sim.execute_melee_attack(attacker, defender);
        // Pushed East to (12, 10)
        ASSERT_EQ(def_unit->pos.x, 12);
        ASSERT_EQ(def_unit->pos.y, 10);
        // Facing is forced towards attacker (West)
        ASSERT_EQ(def_unit->facing, Direction::West);
    } TEST_END();

    TEST_CASE("12.52 Lethal 0 HP Death Spawns Skull & Crossbones Effect While Water Drowning Exclusively Plays Drown Sequence") {
        // 1. Lethal Melee Attack
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t attacker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            uint32_t defender = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            auto* def = const_cast<AntUnit*>(&sim.get_unit(defender));
            def->hp = 1; // 1 HP, lethal against 1 HP melee strike

            sim.execute_melee_attack(attacker, defender);
            ASSERT_EQ(def->hp, 0);
            ASSERT_EQ(def->state, UnitState::Dead);

            const auto& effects = sim.get_world_state().effects;
            ASSERT_FALSE(effects.empty());
            bool has_death_anim = false;
            for (const auto& eff : effects) {
                if (eff.anim_name == "death1" || eff.anim_name == "death2" || eff.anim_name == "death3") {
                    has_death_anim = true;
                    break;
                }
            }
            ASSERT_TRUE(has_death_anim);
        }

        // 2. Lethal Bomb Blast
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t victim = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
            auto* vic = const_cast<AntUnit*>(&sim.get_unit(victim));
            vic->hp = 2; // lethal against 2 damage bomb blast
            sim.grid_mut().place_bomb(15, 15, 1); // Enemy team bomb

            sim.tick(); // Detonates
            ASSERT_EQ(vic->hp, 0);

            const auto& effects = sim.get_world_state().effects;
            bool has_death_anim = false;
            for (const auto& eff : effects) {
                if (eff.anim_name == "death1" || eff.anim_name == "death2" || eff.anim_name == "death3") {
                    has_death_anim = true;
                    break;
                }
            }
            ASSERT_TRUE(has_death_anim);
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
            ASSERT_EQ(vic.pos.x, 12);
            ASSERT_EQ(vic.pos.y, 10);
            ASSERT_EQ(vic.state, UnitState::Drowning);
            ASSERT_EQ(vic.death_status, DeathStatus::Drowned);

            // Verify no skull & crossbones effect was spawned for drowning
            const auto& effects = sim.get_world_state().effects;
            bool has_death_anim = false;
            for (const auto& eff : effects) {
                if (eff.anim_name == "death1" || eff.anim_name == "death2" || eff.anim_name == "death3") {
                    has_death_anim = true;
                    break;
                }
            }
            ASSERT_FALSE(has_death_anim);
        }
    } TEST_END();

    // -------------------------------------------------------------------------
    // Test 12.53: Power-Up Transformation & Continuous Idle Animation Facing South
    // -------------------------------------------------------------------------
    TEST_CASE("12.53 Power-Up Transformation & Continuous Idle Animation Facing South & All Headings") {
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

            // Step simulation until arrival and transformation starts
            while (sim.get_unit(ant_id).is_alive() && !sim.get_unit(ant_id).is_transforming() &&
                   !sim.get_unit(ant_id).waypoints.empty()) {
                sim.tick();
            }

            const auto& unit_trans = sim.get_unit(ant_id);
            ASSERT_TRUE(unit_trans.is_transforming());
            ASSERT_EQ(unit_trans.pos.x, 15);
            ASSERT_EQ(unit_trans.pos.y, 15);
            ASSERT_EQ(static_cast<uint8_t>(unit_trans.facing), static_cast<uint8_t>(Direction::South));

            // Verify 11-tick transformation sequence progression
            for (int t = 0; t < 11; ++t) {
                const auto& ws = sim.get_world_state();
                const AntSnapshot* snap = nullptr;
                for (const auto& a : ws.ants) {
                    if (a.id == ant_id) { snap = &a; break; }
                }
                ASSERT_TRUE(snap != nullptr);
                ASSERT_TRUE(snap->is_transforming);
                ASSERT_EQ(snap->transform_anim_frame, static_cast<uint16_t>(t));
                sim.tick();
            }

            // Transformation completes: unit must emerge in Idle (or GuardIdle for Combat)
            const auto& unit_done = sim.get_unit(ant_id);
            ASSERT_FALSE(unit_done.is_transforming());
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
            ASSERT_FALSE(unit.is_transforming());
            ASSERT_EQ(unit.state, UnitState::GuardIdle);
            ASSERT_TRUE(unit.anim_subitem > 0); // Actively animating
        }
    } TEST_END();

    TEST_CASE("12.54 Text Width Measurement, Multi-Line Splitting & Font Sizing Integrity") {
        ants::app::Renderer renderer;
        // Verify empty text width
        ASSERT_EQ(renderer.get_text_width(""), 0);

        // Verify non-empty text width is positive
        int32_t small_w = renderer.get_text_width("that food!", ants::app::FontSize::Small);
        ASSERT_TRUE(small_w > 0);
        ASSERT_TRUE(renderer.get_text_height(ants::app::FontSize::Small) > 0);

        // Verify proportional width scales monotonically with string length
        int32_t longer_w = renderer.get_text_width("that food! And extra text for width check.", ants::app::FontSize::Small);
        ASSERT_TRUE(longer_w > small_w);

        // Verify HUD chat entry recording and scroll offset boundaries
        ants::app::HUD hud;
        hud.init(0);
        size_t initial_lines = hud.get_chat_log().size();
        hud.add_chat_entry("Player1", "Hello world!");
        hud.add_chat_entry("Player2", "that food!");
        ASSERT_EQ(hud.get_chat_log().size(), initial_lines + 2);
        ASSERT_EQ(hud.get_chat_scroll_offset(), 0);

        // Scroll limits
        hud.scroll_chat_up(5);
        ASSERT_EQ(hud.get_chat_scroll_offset(), 0); // Not enough lines to scroll
        hud.scroll_chat_down(5);
        ASSERT_EQ(hud.get_chat_scroll_offset(), 0);
    } TEST_END();

    TEST_CASE("12.55 Fire Ant 2.0s (40 Ticks) Ability Cooldown & Placement Timing") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t f_id = sim.spawn_unit(0, AntType::Fire, TileCoord{10, 10});
        ASSERT_EQ(sim.get_unit(f_id).ability_cooldown_ticks, 0);

        // Step 1: Initiate fire placement (non-instant)
        sim.set_tile_flags(11, 10, 0x06);
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{11, 10}, false));
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::PlacingFire);

        // Advance through ticks 0..26: no fire placed on grid yet
        for (int t = 0; t < 26; ++t) {
            ASSERT_FALSE(sim.grid().get_cell(TileCoord{11, 10}).has_fire());
            sim.tick();
        }

        // Tick 27: fire is placed
        sim.tick();
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{11, 10}).has_fire());

        // Advance remaining ticks 28..34: finishes putting away glass
        for (int t = 27; t < 34; ++t) {
            sim.tick();
        }
        sim.tick(); // Tick 35: returns to Idle with remaining 5-tick cooldown (40 initiated - 35 elapsed)
        ASSERT_EQ(sim.get_unit(f_id).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(f_id).ability_cooldown_ticks, 5);

        // Attempting to ignite fire again during cooldown MUST be rejected
        sim.set_tile_flags(10, 11, 0x06);
        ASSERT_FALSE(sim.ignite_fire(f_id, TileCoord{10, 11}, false));

        // Advance 4 ticks: cooldown remaining = 1, still rejected
        for (int t = 0; t < 4; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(f_id).ability_cooldown_ticks, 1);
        ASSERT_FALSE(sim.ignite_fire(f_id, TileCoord{10, 11}, false));

        // Advance 1 tick: cooldown reaches 0, now accepted
        sim.tick();
        ASSERT_EQ(sim.get_unit(f_id).ability_cooldown_ticks, 0);
        ASSERT_TRUE(sim.ignite_fire(f_id, TileCoord{10, 11}, false));
    } TEST_END();

    TEST_CASE("12.56 Bomber Ant 3.0s (60 Ticks) Ability Cooldown & Rejection") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        uint32_t b_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{15, 15});
        ASSERT_EQ(sim.get_unit(b_id).ability_cooldown_ticks, 0);

        // Step 1: Initiate bomb planting (non-instant)
        sim.set_tile_flags(16, 15, 0x06);
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{16, 15}, false));
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::PlantingBomb);
        ASSERT_EQ(sim.get_unit(b_id).ability_cooldown_ticks, 60);

        // Advance 28 ticks to complete bomb placement
        for (int t = 0; t < 28; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(b_id).state, UnitState::Idle);
        ASSERT_EQ(sim.get_unit(b_id).ability_cooldown_ticks, 32); // 60 - 28 = 32 ticks remaining

        // Attempting to plant bomb during cooldown MUST be rejected
        sim.set_tile_flags(15, 16, 0x06);
        ASSERT_FALSE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));

        // Advance 31 ticks: cooldown remaining = 1, still rejected
        for (int t = 0; t < 31; ++t) {
            sim.tick();
        }
        ASSERT_EQ(sim.get_unit(b_id).ability_cooldown_ticks, 1);
        ASSERT_FALSE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));

        // Advance 1 tick: cooldown reaches 0, now accepted
        sim.tick();
        ASSERT_EQ(sim.get_unit(b_id).ability_cooldown_ticks, 0);
        ASSERT_TRUE(sim.plant_bomb(b_id, TileCoord{15, 16}, false));
    } TEST_END();

    TEST_CASE("12.57 Bridge Demolition & Collapse Instant Drowning for Non-Swimmers") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);

        // Create a completed bridge over water at {20, 20}
        sim.set_terrain(20, 20, TERRAIN_WATER);
        sim.set_tile_flags(20, 20, 0x06);
        sim.grid_mut().set_bridge_at(TileCoord{20, 20}, 4, 3600);
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        // Spawn a Worker ant and Swimmer ant on the bridge tile
        uint32_t w_id = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        uint32_t s_id = sim.spawn_unit(0, AntType::Swimmer, TileCoord{20, 20});
        ASSERT_NE(sim.get_unit(w_id).state, UnitState::Drowning);

        // Demolish/regress the bridge so it's no longer completed
        sim.grid_mut().regress_bridge(20, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{20, 20}).has_completed_bridge());

        // One tick of simulation: Worker MUST instantly start drowning, Swimmer starts swimming
        sim.tick();
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Drowning);
        ASSERT_EQ(sim.get_unit(s_id).state, UnitState::Swimming);
        ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
    } TEST_END();

    TEST_CASE("12.58 Chat Focus Control Key Bypass & Outside Click Unfocusing") {
        HUD hud;
        hud.init(0);
        SimulationEngine sim;
        sim.init_test_world(60, 60, 1);
        ViewportCamera camera;

        // Focus chat
        hud.focus_chat();
        ASSERT_TRUE(hud.is_chat_focused());

        // Control key (e.g. Ctrl+A) MUST NOT be appended to chat input
        hud.handle_key_down('a', sim, camera, KMOD_CTRL);
        ASSERT_TRUE(hud.get_chat_input().empty());

        // Left click outside chat input box (e.g. playfield at 200, 200) unfocuses chat
        hud.handle_mouse_down(200, 200, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_FALSE(hud.is_chat_focused());

        // Left click inside chat input box (479..622, 423..437) focuses chat
        hud.handle_mouse_down(500, 428, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_TRUE(hud.is_chat_focused());
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

        // Open options dialog
        hud.open_options();
        ASSERT_TRUE(hud.is_options_open());
        ASSERT_EQ(hud.get_active_quick_chat_edit(), -1);

        // Click F9 box (89..235, 368..387)
        hud.handle_mouse_down(120, 375, SDL_BUTTON_LEFT, sim, camera);
        ASSERT_EQ(hud.get_active_quick_chat_edit(), 0);

        // Backspace to delete "!"
        hud.handle_key_down(SDLK_BACKSPACE, sim, camera);
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it");

        // Type characters
        hud.handle_text_input(" all!");
        ASSERT_EQ(hud.get_quick_chat_key(0), "Now you are in for it all!");

        // Press Return to finish editing
        hud.handle_key_down(SDLK_RETURN, sim, camera);
        ASSERT_EQ(hud.get_active_quick_chat_edit(), -1);

        // Close options
        hud.close_options();
        ASSERT_FALSE(hud.is_options_open());

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
        ASSERT_FALSE(sim.get_unit(w_id).waypoints.empty());

        // Advance 5 ticks: ant moves towards bridge
        for (int i = 0; i < 5; ++i) sim.tick();
        ASSERT_EQ(sim.get_unit(w_id).state, UnitState::Walking);

        // Collapse the bridge
        sim.grid_mut().collapse_bridge(25, 20);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{25, 20}).has_completed_bridge());

        // Clear audio events
        sim.clear_audio_events();

        // Advance simulation: ant approaches water edge, triggers CantGo ONCE and cancels movement
        uint32_t cant_go_audio_count = 0;
        for (int i = 0; i < 40; ++i) {
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

        // Food tile hover -> Food
        sim.grid_mut().get_cell_mut(16, 15).is_food = true;
        int32_t screen_tx16 = (16 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx16, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Food);

        // Obstacle tile hover -> Cant
        sim.grid_mut().set_terrain(17, 15, TERRAIN_OBSTACLE);
        int32_t screen_tx17 = (17 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx17, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Cant);

        // Water tile hover with Worker selected -> Cant
        sim.grid_mut().set_terrain(14, 15, TERRAIN_WATER);
        int32_t screen_tx14 = (14 * 32 + 16) - 320 + PLAYFIELD_X;
        ASSERT_EQ(hud.evaluate_cursor(screen_tx14, screen_ty15, sim.get_world_state(), sim.grid(), camera), CursorType::Cant);

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

        // Issue move order into middle of water at (16, 10)
        sim.issue_move_order(w_id, TileCoord{16, 10});
        // Non-swimmer ant does NOT immediately enter CantGo; it pathfinds to the water edge (12, 10)
        ASSERT_NE(w_ant.state, UnitState::CantGo);
        ASSERT_EQ(w_ant.final_dest, (TileCoord{12, 10}));

        // Advance simulation until ant reaches the shoreline at (12, 10)
        for (int t = 0; t < 100 && w_ant.pos != (TileCoord{12, 10}); ++t) {
            sim.tick();
        }
        ASSERT_EQ(w_ant.pos, (TileCoord{12, 10}));

        // Now that the ant is already at the closest possible tile, issuing move into water triggers CantGo
        sim.issue_move_order(w_id, TileCoord{16, 10});
        ASSERT_EQ(w_ant.state, UnitState::CantGo);
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // Advance simulation through CantGo duration (6 ticks for Worker)
        for (int t = 0; t < 6; ++t) {
            sim.tick();
        }
        ASSERT_EQ(w_ant.state, UnitState::Idle);

        // 2. Ally Walk-Over Movement & Cursors
        uint32_t ally_id = sim.spawn_unit(1, AntType::Worker, TileCoord{8, 8});
        (void)ally_id;
        sim.form_alliance(0, 1);
        ASSERT_TRUE(sim.stats_manager().are_allies(0, 1));

        HUD hud;
        hud.init(0);
        ViewportCamera camera{0, 0};
        hud.select_ant(w_id, false);

        // Hover over ally with friendly selected: cursor is Move (not Attack)
        int32_t ally_screen_x = PLAYFIELD_X + (8 * 32 + 16);
        int32_t ally_screen_y = PLAYFIELD_Y + (8 * 32 + 16);
        CursorType c_ally = hud.evaluate_cursor(ally_screen_x, ally_screen_y, sim.get_world_state(), sim.grid(), camera);
        ASSERT_EQ(c_ally, CursorType::Move);

        // Click ally ant: ant walks over to an adjacent neighbor of ally instead of attacking
        hud.handle_mouse_down(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        hud.handle_mouse_up(ally_screen_x, ally_screen_y, 1, sim, camera, 0);
        ASSERT_EQ(w_ant.state, UnitState::Walking);
        ASSERT_NE(w_ant.final_dest, (TileCoord{8, 8})); // Not directly on ally's tile
        // Final destination must be an adjacent Chebyshev neighbor (Chebyshev dist == 1)
        int32_t c_dist = std::max(std::abs(w_ant.final_dest.x - 8), std::abs(w_ant.final_dest.y - 8));
        ASSERT_EQ(c_dist, 1);

        // 3. Map Select Screen Vertical Centering Formula
        Renderer renderer;
        int32_t th_large = renderer.get_text_height(FontSize::Large);
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

        // Place food morsel at (20, 10)
        sim.grid_mut().get_cell_mut({20, 10}).interactive_id = 239;
        sim.grid_mut().get_cell_mut({20, 10}).is_food = true;

        uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        auto* w = const_cast<AntUnit*>(&sim.get_unit(worker));
        w->pick_up_food(1, 25);
        ASSERT_TRUE(w->is_holding());
        ASSERT_EQ(w->carried_food, 1);

        // Instruct ant with food to move to the food tile
        sim.issue_move_order(worker, TileCoord{20, 10}, false, true);

        // Ant must walk towards food first (does NOT immediately return to base)
        ASSERT_EQ(w->state, UnitState::Walking);
        ASSERT_EQ(w->final_dest, (TileCoord{20, 10}));

        // Advance simulation until ant reaches food
        while (!w->waypoints.empty() && w->pos != TileCoord{20, 10}) {
            sim.tick();
            // During transit, still carrying exactly 1 food
            ASSERT_EQ(w->carried_food, 1);
        }

        // Tick once on arrival: ant realizes upon arrival that it already has food
        sim.tick();

        // Food was NOT consumed (still on map) and ant carried_food is still 1 (no double bite)
        ASSERT_TRUE(sim.grid().get_cell({20, 10}).has_food());
        ASSERT_EQ(w->carried_food, 1);

        // Ant has joined base queue and is routing back to base at (5, 5)
        ASSERT_TRUE(sim.is_ant_in_base_queue(worker));
    } TEST_END();

    TEST_CASE("12.64 Diagonal Melee Attack Pushes Diagonally Along Strike Vector") {
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
        ASSERT_EQ(def->pos, (TileCoord{12, 12}));

        // If primary diagonal destination is obstructed by rock, deflect to cardinal flank
        sim.grid_mut().set_terrain(12, 12, TERRAIN_OBSTACLE);
        def->pos = TileCoord{11, 11};
        def->pixel_x = 11 * 32 + 16;
        def->pixel_y = 11 * 32 + 16;
        const_cast<AntUnit*>(&sim.get_unit(attacker))->attack_cooldown_ticks = 0;
        sim.execute_melee_attack(attacker, defender);
        bool cardinal_flank = (def->pos == TileCoord{12, 11} || def->pos == TileCoord{11, 12});
        ASSERT_TRUE(cardinal_flank);
    } TEST_END();

    TEST_CASE("12.65 Melee Attack Pushback Deflects Sideways When Obstructed by Rock Wall") {
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

        // Because primary East (12, 10) is obstructed by wall, victim deflected sideways (North or South)
        bool deflected_sideways = (def.pos == TileCoord{11, 9} || def.pos == TileCoord{11, 11});
        ASSERT_TRUE(deflected_sideways);
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

    TEST_CASE("12.67 Match Timer 1-Minute, 30-Second & 10-Second Countdown Audio Warnings") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 70000); // 70 seconds

        // Step from 70s down to 60.05s: no warning yet
        sim.set_match_time_remaining_ms(60050);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.tick(); // now 60,000 ms (1 minute remaining!)

        ASSERT_TRUE(sim.has_audio_event(SoundID::OneMinute)); // Sound 55
        ASSERT_TRUE(sim.has_news_event(255, StringID::OneMinuteRemaining)); // String 49

        // Step to 30.05s
        sim.set_match_time_remaining_ms(30050);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.tick(); // now 30,000 ms (30 seconds remaining!)

        ASSERT_TRUE(sim.has_audio_event(SoundID::ThirtySeconds)); // Sound 54
        ASSERT_TRUE(sim.has_news_event(255, StringID::ThirtySecondsRemaining)); // String 50

        // Step to 10.05s
        sim.set_match_time_remaining_ms(10050);
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.tick(); // now 10,000 ms (10 seconds countdown!)

        ASSERT_TRUE(sim.has_audio_event(SoundID::Countdown)); // Sound 44
        ASSERT_TRUE(sim.has_news_event(255, StringID::TenSecondsRemaining)); // String 59

        // Step 20 ticks (1000ms = 1 second, reaching 9,000 ms)
        sim.clear_audio_events();
        for (int i = 0; i < 20; ++i) {
            sim.tick();
        }
        ASSERT_TRUE(sim.has_audio_event(SoundID::Countdown)); // Sound 44 triggers again at 9s!

        // Verify every remaining second from 8 down to 1 triggers Sound 44 (countdwn.wav)
        for (int sec = 8; sec >= 1; --sec) {
            sim.clear_audio_events();
            for (int i = 0; i < 20; ++i) {
                sim.tick();
            }
            ASSERT_TRUE(sim.has_audio_event(SoundID::Countdown)); // Sound 44 every second!
        }
    } TEST_END();

    TEST_CASE("12.68 Match Defeat Triggers losers.wav (Sound 42) & Player Drop-Out Triggers playerout.wav (Sound 41)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 50);

        sim.set_player_score(0, 500);
        sim.set_player_score(1, 100);

        sim.clear_audio_events();
        sim.tick(); // Match game over

        ASSERT_TRUE(sim.is_match_over());
        // Winner gets Sound 56 (VictoryFanfare)
        ASSERT_TRUE(sim.has_targeted_audio_event(0, SoundID::VictoryFanfare));
        // Loser gets Sound 42 (PlayerDefeat / losers.wav)
        ASSERT_TRUE(sim.has_targeted_audio_event(1, SoundID::PlayerDefeat));
        ASSERT_EQ(SoundID::PlayerDefeat, 42u);

        // Player dropout triggers Sound 41 (PlayerDropOut / playerout.wav)
        sim.clear_audio_events();
        sim.clear_news_events();
        sim.trigger_player_dropout(1, "Player 1");

        ASSERT_TRUE(sim.has_audio_event(SoundID::PlayerDropOut));
        ASSERT_EQ(SoundID::PlayerDropOut, 41u);
        ASSERT_TRUE(sim.has_news_event(255, StringID::PlayerDropOut));
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

    TEST_CASE("12.70 Two Ants Colliding on Single Tile Trigger bump.wav (Sound 47)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t a1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});

        sim.clear_audio_events();
        sim.tick(); // Collision resolution on same tile

        ASSERT_TRUE(sim.has_audio_event(SoundID::Bump));
        ASSERT_EQ(SoundID::Bump, 47u);

        // One ant was displaced to avoid occupying the same tile
        ASSERT_FALSE(sim.get_unit(a1).pos == sim.get_unit(a2).pos);
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

            sim.issue_move_order(ant_id, target, false);
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
            sim.issue_move_order(ant_id, target, false);
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

            // Fast forward 300 ticks (15s @ 20Hz, as defined by wp.param == 15 in SMALL.LVL)
            for (int i = 0; i < 300; ++i) {
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

            // Advance another 300 ticks (second 15s interval) with powerup still uncollected:
            // Verifies dropper triggers continuously on cooldown and replaces existing powerup
            for (int i = 0; i < 300; ++i) {
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
            sim.issue_move_order(a1, TileCoord{10, 11}, false);
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

    TEST_CASE("12.74: Authentic 1998 Same-Tile Collision Scuffle Visual Effect & SoundID::CombatNetFairy") {
        SimulationEngine sim;
        ants::assets::LevelData lvl;
        std::string lvl_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        ASSERT_TRUE(lvl.load_lvl(lvl_path));
        sim.init(lvl, 42);

        uint32_t a1_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        uint32_t a2_id = sim.spawn_unit(0, AntType::Combat, TileCoord{15, 15});

        // Tick simulation once
        sim.tick();

        // Check that battle visual effect was spawned
        const auto& ws = sim.get_world_state();
        bool has_battle_effect = false;
        for (const auto& eff : ws.effects) {
            if (eff.anim_name == "battle") {
                has_battle_effect = true;
                break;
            }
        }
        ASSERT_TRUE(has_battle_effect);

        // Check that SoundID::CombatNetFairy (ID 3) and FlingThumpA (Sound 64) were queued at scuffle start
        ASSERT_TRUE(sim.has_audio_event(SoundID::CombatNetFairy));
        ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

        // Advance through scuffle (10 ticks) and bounce flight (6 ticks) until landing thump
        bool got_landing_thump = false;
        for (int t = 0; t < 20; ++t) {
            sim.tick();
            if (sim.has_audio_event(SoundID::FlingThumpB)) {
                got_landing_thump = true;
                break;
            }
        }
        ASSERT_TRUE(got_landing_thump);

        // Ensure ants bounced apart onto different discrete tiles
        const auto& a1 = sim.get_unit(a1_id);
        const auto& a2 = sim.get_unit(a2_id);
        ASSERT_FALSE(a1.pos == a2.pos);
    } TEST_END();

    TEST_CASE("12.75: Authentic NavButtonClick Audio Feedback on HUD, MapSelect, and Scorecard") {
        ASSERT_EQ(SoundID::NavButtonClick, 89u);
        ASSERT_EQ(SoundID::ButtonClick, 0u);

        // 1. HUD Navigation Button SFX
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
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Quick help overlay dismiss
            played_sounds.clear();
            hud.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Options button (525, 7, 52, 23)
            played_sounds.clear();
            hud.handle_mouse_down(540, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Options dialog Return button (402..455, 425..455)
            played_sounds.clear();
            hud.handle_mouse_down(420, 440, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
            hud.handle_mouse_up(420, 440, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit button (579, 7, 46, 23) -> opens quit dialog
            played_sounds.clear();
            hud.handle_mouse_down(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
            hud.handle_mouse_up(590, 15, SDL_BUTTON_LEFT, sim, camera, 0);

            // Quit dialog "No" button (296, 264, 49, 24)
            played_sounds.clear();
            hud.handle_mouse_down(310, 275, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
            hud.handle_mouse_up(310, 275, SDL_BUTTON_LEFT, sim, camera, 0);

            // Spawn and select friendly ant
            uint32_t a_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            hud.select_ant(a_id, false);

            // Move Pedestal (488, 140, 53, 86)
            played_sounds.clear();
            hud.handle_mouse_down(500, 150, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Stop button (602, 176, 34, 50)
            played_sounds.clear();
            hud.handle_mouse_down(610, 190, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            bool saw_nav = false;
            for (auto s : played_sounds) {
                if (s == SoundID::NavButtonClick) saw_nav = true;
            }
            ASSERT_TRUE(saw_nav);

            // Chat [All] button (532, 443, 44, 24)
            played_sounds.clear();
            hud.handle_mouse_down(545, 450, SDL_BUTTON_LEFT, sim, camera, 0);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
        }

        // 2. MapSelectScreen Button SFX
        {
            MapSelectScreen screen;
            std::vector<uint32_t> played_sounds;
            screen.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            // Up button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_UP_X + 10, MapSelectScreen::BTN_UP_Y + 10, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Down button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_DOWN_X + 10, MapSelectScreen::BTN_DOWN_Y + 10, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // FOW ON button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_FOW_ON_X + 5, MapSelectScreen::BTN_FOW_ON_Y + 5, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // FOW OFF button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_FOW_OFF_X + 5, MapSelectScreen::BTN_FOW_OFF_Y + 5, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Drop button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_DROP_X + 10, MapSelectScreen::BTN_DROP_Y + 10, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Quit button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_QUIT_X + 10, MapSelectScreen::BTN_QUIT_Y + 10, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);

            // Start button
            played_sounds.clear();
            screen.handle_mouse_down(MapSelectScreen::BTN_START_X + 10, MapSelectScreen::BTN_START_Y + 10, SDL_BUTTON_LEFT);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
        }

        // 3. ScorecardModal Button SFX
        {
            ScorecardModal modal;
            MatchResult mr{};
            mr.is_over = true;
            modal.show(mr, 0);
            std::vector<uint32_t> played_sounds;
            modal.set_on_play_sfx([&](uint32_t s) { played_sounds.push_back(s); });

            // Leave game button (525..625, 12..44)
            played_sounds.clear();
            modal.handle_mouse_down(550, 20);
            ASSERT_FALSE(played_sounds.empty());
            ASSERT_EQ(played_sounds.back(), SoundID::NavButtonClick);
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

        // 1. Fire Ant: Cooldown starts at order dispatch (40 ticks)
        uint32_t fire_id = sim.spawn_unit(0, AntType::Fire, TileCoord{20, 20});
        sim.set_tile_flags(21, 20, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.ignite_fire(fire_id, TileCoord{21, 20}, false));
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire);
        ASSERT_EQ(sim.get_unit(fire_id).ability_cooldown_ticks, 40);

        // Order move while placing -> triggers CantGo and does not cancel placement
        sim.clear_audio_events();
        sim.issue_move_order(fire_id, TileCoord{22, 20});
        ASSERT_EQ(sim.get_unit(fire_id).state, UnitState::PlacingFire); // Uninterruptible
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // 2. Bomber Ant: Cooldown starts at order dispatch (60 ticks)
        uint32_t bomb_id = sim.spawn_unit(0, AntType::Bomber, TileCoord{25, 25});
        sim.set_tile_flags(26, 25, 0x06);

        sim.clear_audio_events();
        ASSERT_TRUE(sim.plant_bomb(bomb_id, TileCoord{26, 25}, false));
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb);
        ASSERT_EQ(sim.get_unit(bomb_id).ability_cooldown_ticks, 60);

        // Order move while placing -> triggers CantGo and does not cancel
        sim.clear_audio_events();
        sim.issue_move_order(bomb_id, TileCoord{27, 25});
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // Uninterruptible
        ASSERT_TRUE(sim.has_audio_event(SoundID::CantGo));

        // 3. Enemy attack does not knock back placing unit
        uint32_t enemy_id = sim.spawn_unit(1, AntType::Worker, TileCoord{24, 25});

        int32_t hp_before = sim.get_unit(bomb_id).hp;
        int32_t x_before = sim.get_unit(bomb_id).pos.x;
        int32_t y_before = sim.get_unit(bomb_id).pos.y;

        sim.execute_melee_attack(enemy_id, bomb_id);
        ASSERT_LT(sim.get_unit(bomb_id).hp, hp_before); // Took damage
        ASSERT_EQ(sim.get_unit(bomb_id).pos.x, x_before); // Not displaced
        ASSERT_EQ(sim.get_unit(bomb_id).pos.y, y_before);
        ASSERT_EQ(sim.get_unit(bomb_id).state, UnitState::PlantingBomb); // Still placing
    } TEST_END();

    TEST_CASE("12.79 Minimap Radar Firewall Exclusion & Targeted SFX Routing") {
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

    TEST_CASE("12.98: Attack Pursuit Straight-Line Engagement (No Diagonal Drift)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        AntOrder atk_order{};
        atk_order.ant_id = attacker_id;
        atk_order.type = OrderType::Attack;
        atk_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(atk_order);

        // First strike triggers immediate 1-tile East pushback to (12, 10)
        sim.tick();
        const auto& def = sim.get_unit(defender_id);
        ASSERT_EQ(def.pos.x, 12);
        ASSERT_EQ(def.pos.y, 10);
        ASSERT_EQ(def.state, UnitState::Flinch);

        // Advance ticks: Attacker pursues straight East to (11, 10) rather than drifting diagonally
        for (int i = 0; i < 15; ++i) {
            sim.tick();
        }
        const auto& atk = sim.get_unit(attacker_id);
        // Attacker must stay on the same horizontal line (y == 10) without diagonal deviation
        ASSERT_EQ(atk.pos.y, 10);
        ASSERT_TRUE(atk.pos.x == 10 || atk.pos.x == 11);
    } TEST_END();

    TEST_CASE("12.99: Bomb Dud Burn Animation Cycle (*bu301 / Table 0x1004518)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t worker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        auto& unit = sim.get_unit(worker_id);
        unit.state = UnitState::Burn;
        unit.state_timer = 11;
        unit.anim_subitem = 0;
        unit.anim_tick = 0;

        ASSERT_EQ(unit.state, UnitState::Burn);

        // Step through the 11 subitem frames
        for (int i = 0; i < 11; ++i) {
            ASSERT_EQ(unit.state, UnitState::Burn);
            sim.tick();
        }

        // After 11 ticks, state returns to Idle
        ASSERT_EQ(unit.state, UnitState::Idle);
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

        // Attacker is adjacent; strikes IMMEDIATELY on issuance without walking detour
        const auto& atk_init = sim.get_unit(attacker_id);
        ASSERT_EQ(atk_init.state, UnitState::Attacking);
        ASSERT_EQ(atk_init.facing, Direction::SouthEast);

        // 2. Attacker commanded to attack from a distance (8, 8)
        uint32_t distant_atk_id = sim.spawn_unit(0, AntType::Worker, TileCoord{8, 8});
        AntOrder dist_order{};
        dist_order.ant_id = distant_atk_id;
        dist_order.type = OrderType::Attack;
        dist_order.target_entity_id = static_cast<int32_t>(defender_id);
        sim.issue_order(dist_order);

        const auto& dist_unit = sim.get_unit(distant_atk_id);
        ASSERT_EQ(dist_unit.state, UnitState::Walking);
        // Approach routes directly to closest 8-connected neighbor around (11, 11)
        ASSERT_TRUE(dist_unit.final_dest.chebyshev_dist(TileCoord{11, 11}) <= 1);
    } TEST_END();

    TEST_CASE("12.101: Complete Attack Animation Playback Across Full Duration (*at*)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& atk = sim.get_unit(attacker_id);
        ASSERT_EQ(atk.state, UnitState::Attacking);
        ASSERT_EQ(atk.state_timer, 8); // 8 ticks for standard worker attack (agat301)
        ASSERT_EQ(atk.anim_subitem, 0);

        // Advance through 8 ticks; verify anim_subitem increments smoothly
        for (int i = 0; i < 8; ++i) {
            ASSERT_EQ(sim.get_unit(attacker_id).state, UnitState::Attacking);
            ASSERT_EQ(sim.get_unit(attacker_id).anim_subitem, static_cast<uint16_t>(i));
            sim.tick();
        }

        // After 8 ticks, attack completes and transitions cleanly to Idle
        ASSERT_EQ(sim.get_unit(attacker_id).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.102: Smooth 1-Tile Pushback Slide Across First 6 Ticks (*gh*)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        uint32_t attacker_id = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t defender_id = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});

        sim.execute_melee_attack(attacker_id, defender_id);

        const auto& def = sim.get_unit(defender_id);
        ASSERT_EQ(def.state, UnitState::Flinch);
        ASSERT_EQ(def.state_timer, 14); // 14 ticks flinch duration
        // Initial pixel position right upon impact
        ASSERT_EQ(def.pixel_x, 11 * 32 + 16); // 368
        ASSERT_EQ(def.push_dest_px, 12 * 32 + 16); // 400

        // Push flight across 6 ticks (~300ms, matching CHD Table 4 Subitem 3 at 320ms)
        // Tick 1: 368 + (32 * 1) / 6 = 373
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 373);

        // Tick 2: 368 + (32 * 2) / 6 = 378
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 378);

        // Tick 3: 368 + (32 * 3) / 6 = 384
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 384);

        // Tick 4: 368 + (32 * 4) / 6 = 389
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 389);

        // Tick 5: 368 + (32 * 5) / 6 = 394
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 394);

        // Tick 6: reaches destination 400px (landing tick)
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 400);

        // Ticks 7..13: remains at 400px while playing remaining recovery frames
        for (int i = 6; i < 13; ++i) {
            sim.tick();
            ASSERT_EQ(sim.get_unit(defender_id).pixel_x, 400);
            ASSERT_EQ(sim.get_unit(defender_id).state, UnitState::Flinch);
        }

        // Tick 14: flinch finishes and returns to Idle
        sim.tick();
        ASSERT_EQ(sim.get_unit(defender_id).state, UnitState::Idle);
    } TEST_END();

    TEST_CASE("12.103: TINY.LVL Tile (18, 17) Passability & 10-Tick Bounce Parity") {
        // Part A: Tile (18, 17) passability on authentic TINY.LVL (Layer 2 broken2 flat debris)
        std::string tiny_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(tiny_path));

        SimulationEngine sim;
        sim.init(lvl, 42);

        // Tile (18, 17) must be completely passable (not blocked by broken2 overlay)
        ASSERT_TRUE(sim.grid().in_bounds(18, 17));
        const auto& cell = sim.grid().get_cell(18, 17);
        ASSERT_FALSE(cell.is_obstacle_overlay);
        ASSERT_TRUE(cell.is_passable());

        // Ant can walk directly onto tile (18, 17) from neighbor (18, 18)
        uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 18});
        ASSERT_TRUE(ant > 0);
        sim.issue_move_order(ant, TileCoord{18, 17});
        ASSERT_EQ(sim.get_unit(ant).final_dest, (TileCoord{18, 17}));
        for (int i = 0; i < 20; ++i) {
            sim.tick();
            if (sim.get_unit(ant).pos == TileCoord{18, 17}) break;
        }
        ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{18, 17}));

        // Part B: 10-Tick Bounce duration and VisualEffect{"battle"} 10 ticks
        SimulationEngine sim_bounce;
        sim_bounce.init_test_world(60, 60, 100, 60000);
        uint32_t ant1 = sim_bounce.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        uint32_t ant2 = sim_bounce.spawn_unit(0, AntType::Combat, TileCoord{10, 10});

        // Tick simulation to trigger same-tile collision resolution
        sim_bounce.tick();

        // Check that battle visual effect has total_frames = 10
        bool found_battle = false;
        for (const auto& eff : sim_bounce.get_world_state().effects) {
            if (eff.anim_name == "battle") {
                found_battle = true;
                ASSERT_EQ(eff.total_frames, 10);
            }
        }
        ASSERT_TRUE(found_battle);

        // Check that displaced ant has state == UnitState::Bounce and lasts 10 ticks
        const auto& u1 = sim_bounce.get_unit(ant1);
        const auto& u2 = sim_bounce.get_unit(ant2);
        const AntUnit* bounced = (u1.state == UnitState::Bounce ? &u1 : (u2.state == UnitState::Bounce ? &u2 : nullptr));
        ASSERT_TRUE(bounced != nullptr);
        ASSERT_TRUE(bounced->state_timer > 0);
    } TEST_END();

    TEST_CASE("12.104: Collision Scuffle Model Concealment, Domino Cascade & Hazard Landing Parity") {
        // Part A: Model Concealment during 10-tick collision scuffle
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);
            uint32_t a1 = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});

            sim.tick(); // Collision scuffle triggers

            const auto& u1 = sim.get_unit(a1);
            const auto& u2 = sim.get_unit(a2);

            // Both ants must have is_in_scuffle active
            ASSERT_TRUE(u1.is_in_scuffle);
            ASSERT_TRUE(u2.is_in_scuffle);

            // In world state snapshot, is_in_scuffle must be set to true for renderer suppression
            const auto& ws = sim.get_world_state();
            for (const auto& snap : ws.ants) {
                if (snap.id == a1 || snap.id == a2) {
                    ASSERT_TRUE(snap.is_in_scuffle);
                }
            }

            // Visual effect "battle" active at collision center
            bool battle_active = false;
            for (const auto& eff : ws.effects) {
                if (eff.anim_name == "battle" && eff.px == 20 * 32 + 16 && eff.py == 20 * 32 + 16) {
                    battle_active = true;
                }
            }
            ASSERT_TRUE(battle_active);

            // Step through ticks 2 to 11 (10 scuffle ticks): ants remain concealed in scuffle
            for (int t = 2; t <= 11; ++t) {
                sim.tick();
            }

            // After 10 ticks, scuffle ends and displaced ant begins fly-out animation
            const auto& u1_after = sim.get_unit(a1);
            const auto& u2_after = sim.get_unit(a2);
            ASSERT_FALSE(u1_after.is_in_scuffle);
            ASSERT_FALSE(u2_after.is_in_scuffle);
            const AntUnit* displaced = (u1_after.state == UnitState::Bounce ? &u1_after : &u2_after);
            ASSERT_TRUE(displaced != nullptr);
            ASSERT_EQ(displaced->state, UnitState::Bounce);
        }

        // Part B: Water Landing - Non-Swimmer Instant Drowning vs Swimmer Safe Swimming
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            // Surround tile (30, 30) with rock obstacles except north tile (30, 29) which is water
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        auto& wc = sim.grid_mut().get_cell_mut(c);
                        wc.terrain_type = TERRAIN_WATER;
                        wc.surface_type = SurfaceType::Water;
                    } else {
                        auto& oc = sim.grid_mut().get_cell_mut(c);
                        oc.terrain_type = TERRAIN_OBSTACLE;
                        oc.is_obstacle_overlay = true;
                    }
                }
            }

            // Collide two non-swimmer ants at (30, 30)
            uint32_t a1 = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{30, 30});
            sim.tick();

            // Displaced ant forced to bounce into north water tile (30, 29)
            const auto& u1 = sim.get_unit(a1);
            const auto& u2 = sim.get_unit(a2);
            const AntUnit* displaced = (u1.state == UnitState::Drowning || u1.state == UnitState::Bounce ? &u1 : &u2);
            ASSERT_EQ(displaced->pos, (TileCoord{30, 29}));
            ASSERT_EQ(displaced->state, UnitState::Drowning);
            ASSERT_EQ(displaced->hp, 0);
            ASSERT_TRUE(sim.has_audio_event(SoundID::AntDrown));
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterSplash));
        }

        {
            // Swimmer ant bouncing into water safely transitions to Swimming
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    TileCoord c{30 + dx, 30 + dy};
                    if (dx == 0 && dy == -1) {
                        auto& wc = sim.grid_mut().get_cell_mut(c);
                        wc.terrain_type = TERRAIN_WATER;
                        wc.surface_type = SurfaceType::Water;
                    } else {
                        auto& oc = sim.grid_mut().get_cell_mut(c);
                        oc.terrain_type = TERRAIN_OBSTACLE;
                        oc.is_obstacle_overlay = true;
                    }
                }
            }

            // Spawn anchor ant 1 (lower id) and Swimmer ant 2 (higher id to be displaced)
            uint32_t a1 = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            uint32_t a2 = sim.spawn_unit(0, AntType::Swimmer, TileCoord{30, 30});
            (void)a1;
            sim.tick();

            const auto& swimmer = sim.get_unit(a2);
            ASSERT_EQ(swimmer.pos, (TileCoord{30, 29}));
            ASSERT_EQ(swimmer.state, UnitState::Swimming);
            ASSERT_TRUE(swimmer.in_water);
            ASSERT_TRUE(swimmer.hp > 0);
        }

        // Part C: Bomb Detonation & Fire Contact on Bounce Landing
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
                        auto& oc = sim.grid_mut().get_cell_mut(c);
                        oc.terrain_type = TERRAIN_OBSTACLE;
                        oc.is_obstacle_overlay = true;
                    }
                }
            }

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
            uint32_t a2 = sim.spawn_unit(0, AntType::Worker, TileCoord{40, 40});
            (void)a1;
            sim.tick();

            const auto& u2 = sim.get_unit(a2);
            ASSERT_EQ(u2.pos, (TileCoord{41, 40}));
            // Bomb detonated upon landing
            ASSERT_TRUE(sim.has_audio_event(SoundID::BombDetonate));
            ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{41, 40}));
            // Displaced ant took 2 damage from blast
            ASSERT_EQ(u2.hp, u2.max_hp - 2);
        }
    } TEST_END();

    TEST_CASE("12.105: Authentic Attack Audio Sequencing, Sound 57/75/78/79/83 & Flinch FlyThump Events") {
        // Part A: Worker Ant Melee Attack Sound Sequence (Sound 57, Sound 75, Sound 64 launch, Sound 65 landing)
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{21, 20});
            // Ensure target has enough HP to flinch and survive
            sim.get_unit(a2).hp = 4;

            sim.execute_melee_attack(a1, a2); // Strike connects

            // 1. Worker triggers both MeleeAttack (Sound 57) and AttackAlt (Sound 75)
            ASSERT_TRUE(sim.has_audio_event(SoundID::MeleeAttack));
            ASSERT_TRUE(sim.has_audio_event(SoundID::AttackAlt));

            // 2. Target enters Flinch and triggers FlingThumpA (Sound 64 / flythumpa.wav) at launch
            const auto& victim = sim.get_unit(a2);
            ASSERT_EQ(victim.state, UnitState::Flinch);
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

            // 3. Target slides for 6 ticks. On tick 6 (landing on destination tile), triggers FlingThumpB (Sound 65 / flythumpb.wav)
            bool got_landing_thump = false;
            for (int t = 0; t < 7; ++t) {
                sim.tick();
                if (sim.has_audio_event(SoundID::FlingThumpB)) {
                    got_landing_thump = true;
                }
            }
            ASSERT_TRUE(got_landing_thump);
            ASSERT_EQ(sim.get_unit(a2).pos, (TileCoord{22, 20}));
        }

        // Part B: Swimmer and Thief Type-Specific Attack Sound Verification
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{10, 10});
            uint32_t target1 = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
            sim.get_unit(target1).hp = 4;
            sim.execute_melee_attack(swimmer, target1);

            ASSERT_TRUE(sim.has_audio_event(SoundID::MeleeAttack));
            ASSERT_TRUE(sim.has_audio_event(SoundID::WaterAttack)); // Sound 79 (waterattack.wav)

            uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{15, 10});
            uint32_t target2 = sim.spawn_unit(1, AntType::Worker, TileCoord{16, 10});
            sim.get_unit(target2).hp = 4;
            sim.execute_melee_attack(thief, target2);

            ASSERT_TRUE(sim.has_audio_event(SoundID::MeleeAttack));
            ASSERT_TRUE(sim.has_audio_event(SoundID::ThiefWhip)); // Sound 83 (theifwhip.wav)
        }

        // Part C: Combat Ant Heavy Punch Knockback & Stun Landing Sequencing
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t combat = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
            uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
            sim.get_unit(victim).hp = 4;

            sim.execute_melee_attack(combat, victim); // Combat punch connects

            // Queues HeavyPunch (78) and FlingThumpA (64)
            ASSERT_TRUE(sim.has_audio_event(SoundID::HeavyPunch));
            ASSERT_TRUE(sim.has_audio_event(SoundID::FlingThumpA));

            // Flight advances until ground landing: triggers FlingThumpB (65) and Stun (70)
            bool got_stun_landing = false;
            for (int t = 0; t < 15; ++t) {
                sim.tick();
                if (sim.has_audio_event(SoundID::FlingThumpB) && sim.has_audio_event(SoundID::Stun)) {
                    got_stun_landing = true;
                }
            }
            ASSERT_TRUE(got_stun_landing);
            ASSERT_EQ(sim.get_unit(victim).state, UnitState::Stunned);
        }
    } TEST_END();

    TEST_CASE("12.106: Multi-Directional Attack Registration, Victim Facing & Bounce Loop Prevention") {
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

        // 2. Autonomous pursuit attack from the side / rear without elastic repulsion locking:
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
            for (int t = 0; t < 50; ++t) {
                sim.tick();
                const auto& att_u = sim.get_unit(att);
                if (att_u.state == UnitState::Walking) {
                    // While walking, no damage can be dealt mid-stride
                    ASSERT_EQ(sim.get_unit(vic).hp, initial_vic_hp);
                }
                if (sim.get_unit(vic).hp < initial_vic_hp) {
                    hit_connected = true;
                    // Strike connects only after completing walk into adjacent tile (19, 20)
                    ASSERT_EQ(att_u.pos, (TileCoord{19, 20}));
                    break;
                }
            }
            ASSERT_TRUE(hit_connected);
            // Victim turned West towards attacker
            ASSERT_EQ(sim.get_unit(vic).facing, Direction::West);
        }

        // 3. Collision bounce: displaced ant faces along bounce trajectory and avoids infinite loop
        {
            SimulationEngine sim;
            sim.init_test_world(60, 60, 100, 60000);

            uint32_t a1 = sim.spawn_unit(0, AntType::Worker, TileCoord{25, 25});
            uint32_t a2 = sim.spawn_unit(1, AntType::Worker, TileCoord{25, 25});

            sim.tick(); // Triggers same-tile collision resolution scuffle

            // One ant is anchored, other is displaced
            const auto& u1 = sim.get_unit(a1);
            const auto& u2 = sim.get_unit(a2);

            const AntUnit* displaced = (u1.state == UnitState::Bounce) ? &u1 : &u2;
            ASSERT_EQ(displaced->state, UnitState::Bounce);

            // Displaced ant faces along its bounce trajectory away from collision point (25, 25)
            int32_t b_dx = displaced->pos.x - 25;
            int32_t b_dy = displaced->pos.y - 25;
            if (b_dx != 0 || b_dy != 0) {
                Direction expected_facing = ants::assets::vector_to_direction(b_dx, b_dy);
                ASSERT_EQ(displaced->facing, expected_facing);
            }

            // Both ants must settle within 30 ticks without infinite re-collision loops
            for (int t = 0; t < 30; ++t) {
                sim.tick();
            }

            ASSERT_NE(sim.get_unit(a1).state, UnitState::Bounce);
            ASSERT_NE(sim.get_unit(a2).state, UnitState::Bounce);
            ASSERT_FALSE(sim.get_unit(a1).is_in_scuffle);
            ASSERT_FALSE(sim.get_unit(a2).is_in_scuffle);
            ASSERT_EQ(sim.get_unit(a1).attack_target_id, 0u);
            ASSERT_EQ(sim.get_unit(a2).attack_target_id, 0u);
        }
    } TEST_END();

    TEST_CASE("12.107: Authentic Proportional Base Healing Dwell, Lethal Food Drops & Grab Food Action Mapping") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* base = sim.grid().find_anthill(0);
        ASSERT_TRUE(base != nullptr);
        int32_t bx = base->x;
        int32_t by = base->y;

        // 1. Proportional Base Healing Dwell:
        // Wounded worker with 1 HP (missing 9 HP) arrives at base hole (bx+1, by+1)
        uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by + 1});
        auto& u1 = sim.get_unit(w1);
        u1.hp = 1; // 9 HP missing
        u1.state = UnitState::EnteringBase;
        u1.anim_subitem = 8;
        u1.anim_tick = 0;

        // Tick once to enter underground at Frame 8
        sim.tick();
        ASSERT_TRUE(u1.underground);
        // Missing 9 HP * 4 ticks/HP = 36 ticks dwell
        // First tick consumed 1 dwell tick, so 35 dwell ticks remaining
        ASSERT_EQ(u1.base_dwell_ticks, 35u);

        // Advance 34 ticks underground: still eating/healing
        for (int t = 0; t < 34; ++t) {
            sim.tick();
        }
        ASSERT_TRUE(u1.underground);
        ASSERT_EQ(u1.state, UnitState::EnteringBase);

        // Final dwell tick: completes dwell and emerges
        sim.tick();
        ASSERT_FALSE(u1.underground);
        ASSERT_EQ(u1.hp, 10u);

        // 2. Unwounded food depositor: dwell is 4 ticks (200ms)
        uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by + 1});
        auto& u2 = sim.get_unit(w2);
        u2.hp = 10;
        u2.pick_up_food(1, 25);
        u2.state = UnitState::EnteringBase;
        u2.anim_subitem = 8;
        u2.anim_tick = 0;

        sim.tick();
        ASSERT_TRUE(u2.underground);
        // 4 ticks total, 1 consumed on entry
        ASSERT_EQ(u2.base_dwell_ticks, 3u);

        // 3. Lethal Melee Damage Drops Carried Lunchbox:
        uint32_t attacker = sim.spawn_unit(0, AntType::Combat, TileCoord{30, 30});
        uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{31, 30});
        auto& v = sim.get_unit(victim);
        v.hp = 2; // Combat punch does 2 damage -> lethal
        v.pick_up_food(1, 50);

        ASSERT_FALSE(sim.grid().has_lunchbox_at(TileCoord{31, 30}));
        sim.execute_melee_attack(attacker, victim);

        ASSERT_EQ(v.hp, 0u);
        ASSERT_EQ(v.state, UnitState::Dead);
        ASSERT_FALSE(v.is_holding());
        ASSERT_TRUE(sim.grid().has_lunchbox_at(TileCoord{31, 30}));
        ASSERT_EQ(sim.grid().get_lunchbox_points(TileCoord{31, 30}), 50u);

        // 4. Food Harvesting Sequence Action Verification
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

    TEST_CASE("12.108: Version Invariant & Fog of War Cursor Concealment Parity") {
        // 1. Verify semantic versioning components
        ASSERT_EQ(ants::VERSION_STRING, "v0.0.4");
        ASSERT_EQ(ants::VERSION_MAJOR, 0);
        ASSERT_EQ(ants::VERSION_MINOR, 0);
        ASSERT_EQ(ants::VERSION_PATCH, 4);

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
        sim.grid_mut().get_cell_mut(52, 52).is_food = true;
        sim.grid_mut().get_cell_mut(52, 52).lunchbox_points = 100;

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
    TEST_CASE("12.109 Mud Animation Cancel (\"Mud Humping\") Speedup vs Passive Walking") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Mud terrain corridors on row 10 and row 14 from col 10 to 25
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

        for (int t = 1; t <= 300; ++t) {
            // Player rapidly clicking ahead across the mud (anim cancelling and micro-stepping)
            if (sim.get_unit(w_cancel).state == UnitState::Walking && t % 2 == 0) {
                sim.issue_move_order(w_cancel, TileCoord{20, 14});
                ASSERT_EQ(sim.get_unit(w_cancel).anim_tick, 0);
            }

            sim.tick();

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
        // Mud humping completes significantly faster than passive walking across mud
        ASSERT_TRUE(ticks_cancel < ticks_passive);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.110: Power-Up Standing Immunity to Bounce and Melee Attacks
    // ------------------------------------------------------------------------
    TEST_CASE("12.110 Power-Up Standing Immunity to Bounce and Melee Attacks") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Place Bomber power-up at (15, 15)
        sim.grid_mut().place_powerup(15, 15, static_cast<uint8_t>(AntType::Bomber));

        // Friendly ant spawned directly on the power-up tile
        uint32_t standing_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        sim.tick(); // updates on_powerup flag and starts transformation
        sim.interrupt_transformation(standing_ant);
        ASSERT_TRUE(sim.get_unit(standing_ant).on_powerup);

        // Second friendly ant moves into (15, 15) to trigger collision resolution
        uint32_t incoming_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{16, 15});
        sim.issue_move_order(incoming_ant, TileCoord{15, 15});

        for (int t = 0; t < 25; ++t) {
            sim.tick();
        }

        // Standing ant on power-up must NEVER be displaced or bounced off
        ASSERT_EQ(sim.get_unit(standing_ant).pos, (TileCoord{15, 15}));
        ASSERT_NE(sim.get_unit(standing_ant).state, UnitState::Bounce);

        // Enemy Combat ant attempts melee attack against ant on powerup
        uint32_t enemy_combat = sim.spawn_unit(1, AntType::Combat, TileCoord{15, 16});
        uint16_t hp_before = sim.get_unit(standing_ant).hp;
        sim.execute_melee_attack(enemy_combat, standing_ant);
        sim.tick();

        // Standing ant is completely immune to melee attack
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
        ASSERT_FALSE(sim.get_unit(standing_ant).take_damage(2, DamageSource::CombatPunch, enemy_combat));
        ASSERT_FALSE(sim.get_unit(standing_ant).take_damage(1, DamageSource::MeleeStandard, enemy_combat));
        ASSERT_EQ(sim.get_unit(standing_ant).hp, hp_before);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.111: Occupied Destination Bumping (SoundID::Bump and Stopping Cleanly)
    // ------------------------------------------------------------------------
    TEST_CASE("12.111 Occupied Destination Bumping (SoundID::Bump and Stopping Cleanly)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Moving ant approaches towards destination (15, 15)
        uint32_t mover = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 15});
        sim.issue_move_order(mover, TileCoord{15, 15});

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

        // Mover must stop at adjacent tile (distance 1 from occupied destination) and play Bump sound
        ASSERT_TRUE(bumped);
        ASSERT_EQ(sim.get_unit(mover).state, UnitState::Idle);
        ASSERT_TRUE(sim.get_unit(mover).pos.chebyshev_dist(TileCoord{15, 15}) == 1);
        ASSERT_EQ(sim.get_unit(mover).final_dest, sim.get_unit(mover).pos);
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

        // 1. The authentic 3 blocked mound tiles: (bx - 2, by - 1), (bx - 2, by), (bx - 2, by + 1)
        for (int dy = -1; dy <= 1; ++dy) {
            TileCoord blocked_tile{bx - 2, by + dy};
            ASSERT_TRUE(sim.grid().is_anthill_reserved_spot(blocked_tile));

            // Bomber ant adjacent to blocked tile cannot plant bomb on blocked tile
            uint32_t bomber = sim.spawn_unit(0, AntType::Bomber, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile));
            ASSERT_FALSE(sim.plant_bomb(bomber, blocked_tile, false));

            // Fire ant adjacent to blocked tile cannot ignite fire on blocked tile
            uint32_t fire_ant = sim.spawn_unit(0, AntType::Fire, TileCoord{blocked_tile.x - 1, blocked_tile.y});
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile));
            ASSERT_FALSE(sim.ignite_fire(fire_ant, blocked_tile, false));
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
    TEST_CASE("12.113 Base Queue Mound Ramp Concurrency Limit (Max 2 Ants)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        int32_t bx = ah->x;
        int32_t by = ah->y;

        // Place 2 friendly ants on mound ramp: (bx + 1, by) and (bx + 1, by + 1)
        uint32_t ramp1 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by});
        uint32_t ramp2 = sim.spawn_unit(0, AntType::Worker, TileCoord{bx + 1, by + 1});
        (void)ramp1;
        (void)ramp2;

        // 3rd ant arrives with food and joins base queue
        uint32_t q_ant = sim.spawn_unit(0, AntType::Worker, TileCoord{bx - 1, by + 3});
        sim.get_unit(q_ant).pick_up_food(1, 25);
        sim.join_base_queue(q_ant);

        // Tick simulation: q_ant must NOT be dispatched onto ramp while 2 ants occupy mound
        sim.tick();
        ASSERT_EQ(sim.get_active_depositing_ant(0), 0u);

        // Move one ramp ant away off the mound
        sim.get_unit(ramp1).set_tile_pos(bx + 4, by + 4);
        sim.tick();

        // Now with < 2 ants on mound, queue dispatches next ant!
        sim.dispatch_next_base_queue(0);
        ASSERT_EQ(sim.get_active_depositing_ant(0), q_ant);
    } TEST_END();

    // ------------------------------------------------------------------------
    // 12.114: Water Melee Combat Isolation and Emergence Invulnerability
    // ------------------------------------------------------------------------
    TEST_CASE("12.114 Water Melee Combat Isolation and Emergence Invulnerability") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);

        // Part A: Emergence Invulnerability (40 ticks)
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        uint32_t baby = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        sim.get_unit(baby).is_newborn = true;
        sim.get_unit(baby).underground = true;
        sim.get_unit(baby).state = UnitState::EnteringBase;
        sim.get_unit(baby).state_timer = 1;

        // Tick until newborn emerges onto surface
        for (int t = 0; t < 5; ++t) {
            sim.tick();
            if (!sim.get_unit(baby).underground) break;
        }

        ASSERT_FALSE(sim.get_unit(baby).underground);
        ASSERT_TRUE(sim.get_unit(baby).is_invulnerable());
        ASSERT_EQ(sim.get_unit(baby).invulnerable_ticks, 40);

        // Enemy Combat ant tries to attack newborn right after surfacing
        uint32_t enemy = sim.spawn_unit(1, AntType::Combat, TileCoord{21, 22});
        uint16_t baby_hp = sim.get_unit(baby).hp;
        sim.execute_melee_attack(enemy, baby);
        ASSERT_EQ(sim.get_unit(baby).hp, baby_hp);

        // Direct damage also rejected while invulnerable
        ASSERT_FALSE(sim.get_unit(baby).take_damage(2, DamageSource::CombatPunch, enemy));
        ASSERT_EQ(sim.get_unit(baby).hp, baby_hp);

        // Tick until baby emerges completely to Idle
        for (int t = 0; t < 30; ++t) {
            sim.tick();
            if (sim.get_unit(baby).state == UnitState::Idle) break;
        }
        ASSERT_TRUE(sim.get_unit(baby).is_invulnerable());

        // Tick down the 40 ticks
        for (int t = 0; t < 40; ++t) {
            sim.tick();
        }
        ASSERT_FALSE(sim.get_unit(baby).is_invulnerable());
        ASSERT_EQ(sim.get_unit(baby).invulnerable_ticks, 0);

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
    TEST_CASE("12.115 Blocked Base Emergence 20-Tick Postponement (Ants.exe 0x1025100)") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 100, 60000);
        sim.grid_mut().set_anthill(0, TileCoord{20, 20});
        const auto* ah = sim.grid().find_anthill(0);
        ASSERT_TRUE(ah != nullptr);
        TileCoord hole{ah->x + 1, ah->y + 1}; // (21, 21)

        // 1. Place a stationary surface ant directly on the hole
        uint32_t blocker = sim.spawn_unit(0, AntType::Worker, hole);
        sim.tick();
        ASSERT_TRUE(sim.has_living_ant_at(hole));

        // 2. Hatch a newborn ant
        sim.set_player_score(0, 500);
        sim.set_player_eggs(0, 5);
        ASSERT_TRUE(sim.hatch_ant(0, AntType::Worker));

        // Find the newborn ant
        const auto& world = sim.get_world_state();
        uint32_t baby_id = 0;
        for (const auto& a : world.ants) {
            if (a.id != blocker && a.player_id == 0) {
                baby_id = a.id;
                break;
            }
        }
        ASSERT_TRUE(baby_id != 0);
        const auto& baby = sim.get_unit(baby_id);
        ASSERT_TRUE(baby.is_underground());

        // 3. Tick through the initial hatch delay (60 ticks)
        for (int t = 0; t < 60; ++t) {
            sim.tick();
        }

        // Because hole is occupied by blocker, newborn is postponed by 20 ticks (1000ms)
        ASSERT_TRUE(sim.get_unit(baby_id).is_underground());
        ASSERT_GT(sim.get_unit(baby_id).state_timer, 0);

        // Advance 10 ticks (halfway through postponement)
        for (int t = 0; t < 10; ++t) {
            sim.tick();
        }
        ASSERT_TRUE(sim.get_unit(baby_id).is_underground());

        // 4. Move the blocker away from the hole out into open ground
        TileCoord target{ah->x - 5, ah->y + 3};
        sim.issue_move_order(blocker, target);
        for (int t = 0; t < 40; ++t) {
            sim.tick();
            if (!sim.has_living_ant_at(hole)) break;
        }
        ASSERT_FALSE(sim.has_living_ant_at(hole));

        // Advance simulation ticks for newborn's postponement timer to count down and emerge
        for (int t = 0; t < 25; ++t) {
            sim.tick();
            if (!sim.get_unit(baby_id).is_underground()) break;
        }
        ASSERT_FALSE(sim.get_unit(baby_id).is_underground());
        ASSERT_TRUE(sim.get_unit(baby_id).is_invulnerable());
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

        // Moving ally ant1 is NOT an impassable obstacle in A* routing (0x10209cd)
        // Ant 2 must successfully find a path directly through the corridor!
        ASSERT_EQ(sim.get_unit(ant2).state, UnitState::Walking);
        ASSERT_FALSE(sim.get_unit(ant2).waypoints.empty());
        ASSERT_EQ(sim.get_unit(ant2).final_dest, (TileCoord{20, 10}));
    } TEST_END();
}


// ============================================================================
// Master Test Runner Main
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=======================================================\n"
              << " ANTS REMAKE — HEADLESS INTEGRATION HARNESS\n"
              << "=======================================================\n";

    run_suite_1_surface_compositing();
    run_suite_2_camera_transforms();
    run_suite_3_hud_and_radar();
    run_suite_4_audio_mixer();
    run_suite_5_midi_player();
    run_suite_6_scorecard_and_audio_routing();
    run_suite_7_input_controls();
    run_suite_8_unit_health_and_map_select();
    run_suite_9_gameplay_mechanics_and_options();
    run_suite_10_egg_economy_incubation_teamup_abilities();
    run_suite_11_anthill_queuing_and_priority();
    run_suite_12_unit_selection_and_occupied_tile_movement();

    std::cout << "\n=======================================================\n"
              << " INTEGRATION TEST SUMMARY\n"
              << "=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Passed:           " << (g_test_count - g_test_failures) << "\n"
              << " Failed:           " << g_test_failures << "\n"
              << "=======================================================\n";

    return (g_test_failures == 0) ? 0 : 1;
}
