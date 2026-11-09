// The picture of a frame in the application (application.hpp): the render of the match and of every screen, the music that goes with them, the loading screen and the quick help.

#include <algorithm>
#include <cstdlib>

#include "ants_app/application.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/page_layout.hpp"
#include "ants_app/version.hpp"

namespace ants::app {

// The numbers grow on a small canvas in the browser only (a phone: the CSS-pixel density); a native window of any size keeps the original's digits
#if defined(__EMSCRIPTEN__)
static constexpr bool kPhoneDigits = true;
#else
static constexpr bool kPhoneDigits = false;
#endif

void Application::render_frame() {
    renderer_->begin_frame();
    last_latency_layout_.reset();                        // (set again when this frame draws the network's readout)
    update_picture();
    renderer_->set_picture(picture_);                    // the screens are the picture (the whole canvas; a match of a smaller layout is centred in it)

    if (state_ == AppState::Loading) {
        render_loading_screen();
    } else if (state_ == AppState::QuickHelp) {
        render_quick_help_screen();
    } else if (state_ == AppState::StartMenu) {
        render_start_menu(*renderer_, assets_, start_menu_);
    } else if (state_ == AppState::MapSelect) {
        map_select_.render(*renderer_, assets_);
    } else if (scorecard_.is_open()) {
        scorecard_.render(*renderer_, assets_);
    } else if (catch_up_screen_active()) {
        render_catch_up_screen();
    } else {
        sim::SimulationEngine& view = view_sim();            // (the engine that is shown: the predicted one in a match that predicts the player's own orders)
        const auto& world = view.get_world_state();
        sync_game_menu_settings();                           // (Ctrl+L flipped the health marks: remembered)
        renderer_->set_health_marks(hud_.ant_health(), hud_.ant_health().when == health::When::Off ? 1.0 : canvas_density().per_css, kPhoneDigits);
        renderer_->render_world(world, view.grid(), static_cast<int32_t>(hud_.get_selected_ant_id()),
                                hud_.get_selected_ant_ids(), false, show_tile_grid_,
                                mouse_screen_x_, mouse_screen_y_,
                                hud_.get_selected_base_team_id(),
                                tick_accumulator_);
        hud_.render(*renderer_, assets_, world, renderer_->camera());
        render_net_overlay();
    }

    // Frame rate counter and frametime sparkline in the bottom right hand corner of the canvas (the viewport: the version stands next to the counter in every layout):
    // the plate is the canvas's, not the picture's (a page is the picture's own corner: corner_area)
    const LayoutRect corner = corner_area();
    renderer_->set_picture(corner);
    const CornerPlate plate = CornerPlate::for_canvas(corner.w, corner.h);
    int fps_val = std::max(1, static_cast<int>(std::round(fps_display_value_)));
    std::string fps_text = std::to_string(fps_val) + " FPS";
    int32_t text_w = renderer_->get_text_width(fps_text, FontSize::Px12);
    int32_t text_h = renderer_->get_text_height(FontSize::Px12);
    constexpr int32_t spark_w = static_cast<int32_t>(SPARKLINE_SAMPLES);
    constexpr int32_t spark_h = FPS_OVERLAY_SPARK_H;
    std::string ver_text(ants::VERSION_STRING);
    int32_t ver_w = renderer_->get_text_width(ver_text, FontSize::Px12);
    const CornerRow row = CornerRow::of(plate, text_w, ver_w, spark_w, spark_h, text_h);          // (the version, the sparkline and the frame rate: one row from the right edge)
    const int32_t text_x = row.fps_x;
    const int32_t spark_x = row.spark_x;
    const int32_t spark_y = row.spark_y;
    const int32_t text_y = row.text_y;
    renderer_->draw_text(fps_text, text_x, text_y, {255, 255, 255, 255}, FontSize::Px12);

    // Version number display (bottom-right next to FPS sparkline)
    const int32_t ver_x = row.version_x;
    const int32_t ver_y = text_y;
    renderer_->draw_text(ver_text, ver_x, ver_y, {180, 190, 200, 220}, FontSize::Px12);

    // Dark translucent background plate + subtle border
    renderer_->fill_rect(spark_x - 1, plate.top, spark_w + 2, plate.bottom - plate.top, ants::assets::ColorRGBA{0, 0, 0, 160});
    renderer_->draw_rect(spark_x - 1, plate.top, spark_w + 2, plate.bottom - plate.top, ants::assets::ColorRGBA{80, 85, 90, 180});

    // 60 FPS reference guide line (16.67ms -> 5 pixels from bottom)
    int32_t ref_line_y = spark_y + spark_h - 5;
    renderer_->fill_rect(spark_x, ref_line_y, spark_w, 1, ants::assets::ColorRGBA{50, 80, 70, 150});

    // Render each frame history sample (oldest on left, newest on right)
    for (int32_t i = 0; i < spark_w; ++i) {
        size_t sample_idx = (frametime_index_ + static_cast<size_t>(i)) % SPARKLINE_SAMPLES;
        float ft = frametime_history_[sample_idx];

        // 33.3ms (30 FPS) is full box height (11px); 16.67ms (60 FPS) is ~5.5px
        int32_t bar_h = static_cast<int32_t>(std::round((ft / 33.333f) * static_cast<float>(spark_h)));
        bar_h = std::clamp(bar_h, 1, spark_h);
        int32_t bar_y = spark_y + spark_h - bar_h;

        // Color coding based on frame pacing:
        // Green: <= 18.0ms (55-60+ FPS, solid)
        // Yellow: 18.1 - 25.0ms (40-55 FPS, minor drop)
        // Red: > 25.0ms (< 40 FPS, stutter spike)
        ants::assets::ColorRGBA bar_color;
        if (ft <= 18.0f) {
            bar_color = ants::assets::ColorRGBA{60, 230, 90, 220};
        } else if (ft <= 25.0f) {
            bar_color = ants::assets::ColorRGBA{240, 210, 50, 220};
        } else {
            bar_color = ants::assets::ColorRGBA{240, 60, 60, 240};
        }

        renderer_->fill_rect(spark_x + i, bar_y, 1, bar_h, bar_color);
    }

    render_latency_corner(ver_x, text_y, plate);

    // Authentic Software Cursor (Matching Ants.exe 0x1026c5c / 0x1027e65): the pointer is the picture's
    renderer_->set_picture(picture_);
    render_touch_feedback();                                 // (the ring of a coming hold: over everything of the picture, under the pointer)
    CursorType cur = CursorType::Normal;
    if (state_ == AppState::Playing && !scorecard_.is_open() && !catch_up_screen_active()) {         // (the catch-up screen has no map to point at)
        sim::SimulationEngine& view = view_sim();
        hud_.set_sim_query(&view);                           // (the cursor's special-target question is asked of the picture's own engine)
        cur = hud_.evaluate_cursor(mouse_screen_x_, mouse_screen_y_, view.get_world_state(), view.grid(), renderer_->camera());
    }
    if (!pointer_outside_) renderer_->render_software_cursor(cur, mouse_screen_x_, mouse_screen_y_, static_cast<uint32_t>(sim_.current_tick()));

    renderer_->end_frame();
    felt_delay_.on_frame_end(frame_ms());                    // (an order whose effect is in this frame is felt now)
}

// FUN_0100e6ce: the intro (intro.mid, `play AntsMidi from 0 notify`) plays ONCE; its end (MM_MCINOTIFY) starts the random in-game pieces (update_music).
void Application::start_intro_music() {
    music_open_ = true;
    midi_player_.load_file(config_.midi_path);
    audio_mixer_.play_music("Original-Ants/INTRO.mp3", false);
    if (config_.headless) {
        midi_player_.play(false);
    }
    if (music_held_) hold_music(true);                       // a window without the focus starts nothing audible
}

// `close AntsMidi`: the device is closed and stays closed until a match start, a piece's end is not waited for any more, the activation of the program or the music slider starts it again
void Application::close_music() {
    music_open_ = false;
    audio_mixer_.stop_music();
    midi_player_.stop();
}

// FUN_0100e6da: a random in-game piece: rand() % 3, never the one that played last (the first pick is uniform: the stored index starts at 3)
void Application::play_next_ingame_music() {
    static const std::string IN_GAME_TRACKS[3] = {
        "ANTS2A",
        "ANTS2B",
        "ANTSFUN3"
    };

    int track = std::rand() % 3;
    if (track == last_music_track_) {
        track = (track + 1) % 3;
    }
    last_music_track_ = track;

    music_open_ = true;
    audio_mixer_.play_music("Original-Ants/" + IN_GAME_TRACKS[track] + ".mp3", false);
    midi_player_.load_file("Original-Ants/" + IN_GAME_TRACKS[track] + ".MID");
    if (config_.headless) {
        midi_player_.play(false);
    }
    if (music_held_) hold_music(true);
}

// MM_MCINOTIFY (0x100e8cc): when the piece that plays has ended, the next random in-game piece starts, on the setup screen as well as in a match
void Application::update_music(float dt) {
    audio_mixer_.update_music(dt);
    midi_player_.update(dt);
    if (music_open_ && !music_held_ && !audio_mixer_.is_music_playing() && !midi_player_.is_playing()) {       // (a held piece has not ended: it is paused)
        play_next_ingame_music();
    }
}

// WM_ACTIVATEAPP: deactivating closes the music (and remembers that it was open), activating starts a NEW random piece.
// --audio-focus (several games on one machine, the start scripts; the original ran one game per machine): a window without the input focus is silent, and its music
// is HELD, not closed: with the original's rule every click on another window would close a piece and the click back would start a new one, so that no piece
// is ever heard to its end. The piece goes on where it was left when the window has the focus again; a piece that starts meanwhile (a match begins in a
// window behind the others) starts held.
void Application::set_app_active(bool active) {
    if (config_.audio_follows_focus) {
        audio_mixer_.set_sound_volume(active ? hud_.get_sound_volume() : 0);      // every sound, playing or new, is silent without the focus
        hold_music(!active);
        return;
    }
    if (!active) {
        if (music_open_) {
            music_resume_on_activate_ = true;
            close_music();
        }
    } else if (music_resume_on_activate_) {
        music_resume_on_activate_ = false;
        play_next_ingame_music();
    }
}

void Application::hold_music(bool hold) {
    music_held_ = hold;
    if (hold) {
        audio_mixer_.pause_music();
        midi_player_.pause();
    } else {
        audio_mixer_.resume_music();
        midi_player_.resume();
    }
}

void Application::render_loading_screen() {
    draw_loading_screen(*renderer_, assets_, wide_pages(), static_cast<int32_t>(intro_ticks_));       // (page_layout.hpp: the original's page, or the wide one)
}

void Application::render_quick_help_screen() {
    // qh_screen and the START button animations qh_start1 / qh_start2 (hover) / qh_start3 (pressed): the original's page, or the wide one (page_layout.hpp)
    draw_quick_help_screen(*renderer_, assets_, wide_pages(), quick_help_start_.pressed() ? QuickHelpStart::Pressed : (quick_help_start_.hovered() ? QuickHelpStart::Hover : QuickHelpStart::Up));
}

// The pointer is one global in the original (GetCursorPos): every screen's events record it, and the next screen replays it into its buttons
void Application::note_pointer(int32_t x, int32_t y) {
    mouse_screen_x_ = x;
    mouse_screen_y_ = y;
    mouse_has_moved_ = true;
    pointer_outside_ = false;
}
// The START! button of the quick help is the original's button class: the press captures it (qh_start3 carries no sound), the release runs the callback
// (FUN_01014802: the window closes and the setup screen is created) while the capture is still there, leaving cancels it for good
void Application::quick_help_move(int32_t x, int32_t y) {
    note_pointer(x, y);
    quick_help_start_.on_move(x, y);
}
void Application::quick_help_press(int32_t x, int32_t y) {
    note_pointer(x, y);
    quick_help_start_.on_press(x, y);
}
void Application::quick_help_release(int32_t x, int32_t y) {
    note_pointer(x, y);
    if (quick_help_start_.on_release(x, y)) close_quick_help(true);
}

// The quick help is over. Its button is let go of (a key can close the window while the button is held: nothing stays pressed in a window that is gone), and the setup
// screen takes over with fresh buttons: a release of a press that began on the quick help finds no pressed button there and does nothing. A click that closed the window may
// be the first of a double click (see closing_click_pending()).
void Application::close_quick_help(bool by_click) {
    quick_help_start_.reset();
    enter_map_select();
    closing_click_pending_ = by_click && joined_a_room();
    closing_click_ms_ = SDL_GetTicks();
}

// A left press on the setup screen. After a click closed the quick help of a machine that joined a room, a press that continues that click is not for the screen: the second
// click of a double click on START!, which SDL counts (`clicks` above 1) or which comes within SDL's double-click time of the click that closed the quick help (a press that was
// queued before that click was handled has a time before it, which counts as within). The first press that does neither begins a new click and ends the rule.
bool Application::swallow_closing_click(uint8_t clicks, uint32_t timestamp_ms) {
    if (!closing_click_pending_) return false;
    const bool within = static_cast<int32_t>(timestamp_ms - closing_click_ms_) <= static_cast<int32_t>(kDoubleClickMs);
    if (clicks > 1 || within) return true;
    closing_click_pending_ = false;
    return false;
}

// The setup screen's left press: the rest of the click that closed the quick help of a joined room, or of the click that left the start menu (both rules are asked: each ends by itself)
bool Application::swallow_the_rest_of_a_click(uint8_t clicks, uint32_t timestamp_ms) {
    const bool closing = swallow_closing_click(clicks, timestamp_ms);
    const bool gesture = swallow_menu_gesture(clicks, timestamp_ms);
    return closing || gesture;
}

// FUN_010147c2: Enter (0x18), Esc (0x1a), C (0x43), X (0x58), c (0x63) and x (0x78) run the same callback; M and m open the More Help dialog (not built); no other key does anything
void Application::quick_help_key(SDL_Keycode sym) {
    if (state_ != AppState::QuickHelp) return;
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_ESCAPE || sym == SDLK_c || sym == SDLK_x) close_quick_help(false);
}

void Application::play_startup_sound() {
    // Authentic game start sound: picks one of 6 random voice clips (Sound IDs 7..12)
    // rndm6.wav (7), rndm5.wav (8), rndm4.wav (9), rndm3.wav (10), rndm2.wav (11), rndm1.wav (12)
    // Includes "We love this game!", "Spike and I are buds", etc.
    uint32_t s_id = 7 + static_cast<uint32_t>(std::rand() % 6);
    audio_mixer_.play_sfx(s_id, 1.0f, 255);
}

void Application::set_local_player(uint8_t team_id) {
    if (team_id >= 4) return;
    local_player_id_ = team_id;
    sim_.set_viewing_player_id(local_player_id_);
    map_select_.set_player_team(local_player_id_);
    hud_.set_game_mode(sim_.game_mode());
    hud_.init(local_player_id_);
    hud_.clear_selection();
    if (renderer_) {
        renderer_->set_hud_team(local_player_id_);
        show_start_view();
    }
}

} // namespace ants::app
