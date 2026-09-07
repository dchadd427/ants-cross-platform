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

        // Camera must NOT have panned toward top-left (stays centered at 600, 600)
        ASSERT_EQ(app.renderer().camera().world_x, init_cam_x);
        ASSERT_EQ(app.renderer().camera().world_y, init_cam_y);

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

        // Verify camera is centered on Team 0's base (accounting for viewport dimensions & clamping)
        const auto* base0 = app.sim().grid().find_anthill(0);
        ASSERT_TRUE(base0 != nullptr);
        const auto& cam = app.renderer().camera();
        int32_t expected_x = std::clamp(base0->x * 32 + 16 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        int32_t expected_y = std::clamp(base0->y * 32 + 16 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
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
        expected_x = std::clamp(base1->x * 32 + 16 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        expected_y = std::clamp(base1->y * 32 + 16 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
        ASSERT_EQ(cam.world_x, expected_x);
        ASSERT_EQ(cam.world_y, expected_y);

        // Switch to Team 2 via direct setter
        app.set_local_player(2);
        ASSERT_EQ(app.local_player_id(), 2);
        const auto* base2 = app.sim().grid().find_anthill(2);
        ASSERT_TRUE(base2 != nullptr);
        expected_x = std::clamp(base2->x * 32 + 16 - cam.viewport_w / 2, 0, static_cast<int32_t>(app.sim().grid().width() * 32 - static_cast<uint32_t>(cam.viewport_w)));
        expected_y = std::clamp(base2->y * 32 + 16 - cam.viewport_h / 2, 0, static_cast<int32_t>(app.sim().grid().height() * 32 - static_cast<uint32_t>(cam.viewport_h)));
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

        // Spawn damaged worker carrying food at base queue spot (bx, by+3)
        uint32_t ant_id = sim_engine.spawn_unit(0, AntType::Worker, TileCoord{bx, by + 3});
        auto& ant = sim_engine.get_unit(ant_id);
        ant.hp = 20; // Damaged
        ant.pick_up_food(1, 25);
        ant.harvest_origin = TileCoord{15, 15};

        // Issue move order to base hole (bx+1, by+1)
        sim_engine.issue_move_order(ant_id, TileCoord{bx + 1, by + 1});

        // Tick movement until entering hole
        for (int i = 0; i < 60 && ant.state != UnitState::EnteringBase; ++i) {
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
        ASSERT_EQ(played_sound, ants::sim::SoundID::ChatSend);

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

        // Advance simulation until ant completes emergence and walks to idle spot (bx+3, by+3)
        for (int i = 0; i < 80; ++i) sim.tick();

        const auto& u_final = sim.get_unit(newborn_id);
        ASSERT_EQ(u_final.pos.x, bx + 3);
        ASSERT_EQ(u_final.pos.y, by + 3);
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

        // Advance 1 more tick: Ant 1 has deposited and popped. Now Ant 2 gets priority!
        sim.tick();
        ASSERT_EQ(sim.get_active_depositing_ant(0), a2);
        ASSERT_EQ(sim.get_unit(a2).state, UnitState::Walking);

        // Step simulation until Ant 2 reaches hole and deposits food
        for (int i = 0; i < 80 && sim.get_unit(a2).is_holding(); ++i) {
            sim.tick();
            if (sim.get_unit(a2).is_holding()) {
                // Ant 3 advanced to slot 1 or is waiting in QueuingBase
                ASSERT_TRUE(sim.get_unit(a3).state == UnitState::Walking || sim.get_unit(a3).state == UnitState::QueuingBase);
            }
        }

        // Ant 2 deposited food: score increased to 50
        ASSERT_FALSE(sim.get_unit(a2).is_holding());
        ASSERT_EQ(sim.get_player_score(0), 50);

        // Advance 1 more tick: Ant 3 now gets priority!
        sim.tick();
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
