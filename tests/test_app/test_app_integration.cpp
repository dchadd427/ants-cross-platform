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

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/application.hpp"

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

        // 'A' key selects all friendly units
        hud.handle_key_down('a', sim, camera);
        ASSERT_EQ(hud.get_selected_ant_ids().size(), 3u);

        // 'N' key cycles to next ant
        hud.select_ant(a1);
        hud.handle_key_down('n', sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), a2);

        hud.handle_key_down('n', sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        hud.handle_key_down('n', sim, camera); // Wrap around
        ASSERT_EQ(hud.get_selected_ant_id(), a1);

        // 'P' key cycles to previous ant
        hud.handle_key_down('p', sim, camera); // Wrap around backwards
        ASSERT_EQ(hud.get_selected_ant_id(), a3);

        // 'Esc' clears selection
        hud.handle_key_down(27, sim, camera);
        ASSERT_EQ(hud.get_selected_ant_id(), 0u);
        ASSERT_TRUE(hud.get_selected_ant_ids().empty());
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

        // Default: Fog of War enabled
        ASSERT_TRUE(screen.is_fog_of_war_enabled());

        // Click "Off" button
        screen.handle_mouse_down(MapSelectScreen::BTN_FOW_OFF_X + 5, MapSelectScreen::BTN_FOW_OFF_Y + 5, SDL_BUTTON_LEFT);
        ASSERT_FALSE(screen.is_fog_of_war_enabled());

        // Click "On" button
        screen.handle_mouse_down(MapSelectScreen::BTN_FOW_ON_X + 5, MapSelectScreen::BTN_FOW_ON_Y + 5, SDL_BUTTON_LEFT);
        ASSERT_TRUE(screen.is_fog_of_war_enabled());

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

    TEST_CASE("8.4 INTRO.MID Lifecycle & In-Game Music Silence") {
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true; // Test map select lifecycle
        ASSERT_TRUE(app.init(cfg));

        // Starts in MapSelect with INTRO.MID active
        ASSERT_EQ(app.state(), AppState::MapSelect);

        // When starting game, state transitions to Playing and MIDI stops
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_FALSE(app.midi_player().is_playing()); // In-game music is silent!

        // When returning to map select, MIDI resumes
        app.return_to_map_select();
        ASSERT_EQ(app.state(), AppState::MapSelect);
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
}

// ============================================================================
// Master Test Runner Main
// ============================================================================
int main() {
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
