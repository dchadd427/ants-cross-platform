// The selection and the mouse of the match screen (hud.hpp): selecting ants and bases, the rubber band, the mouse buttons, the pointer's motion and the step of the edge scroll.
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ants::app {

// =========================================================================
// Selection Controls
// =========================================================================

void HUD::select_ant(uint32_t ant_id, bool is_multi) {
    selected_ant_id_ = ant_id;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = is_multi;
    if (ant_id != 0) {
        selected_ant_ids_.push_back(ant_id);
    }
    selection_status_pending_ = true;
}

void HUD::select_base(int32_t team_id) noexcept {
    selected_base_team_id_ = team_id;
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;
}

void HUD::clear_selection() noexcept {
    selected_ant_id_ = 0;
    selected_ant_ids_.clear();
    selected_base_team_id_ = -1;
    is_multi_select_mode_ = false;
    selection_status_pending_ = true;      // every deselect clears the status text (FUN_01028c44)
}

bool HUD::is_ant_selected(uint32_t id) const noexcept {
    if (id == 0) return false;
    return std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) != selected_ant_ids_.end();
}

bool HUD::is_shift_held() const noexcept {
    return shift_held_ || ((static_cast<uint16_t>(SDL_GetModState()) & KMOD_SHIFT) != 0);
}


void HUD::select_all_friendly(const sim::WorldState& world) {
    // Ctrl+A (0x1026237): FUN_01028c44(0), then every ant of the player's own table is added (no state filter); panel 3 for one, 4 for several,
    // and the ready voice of the first one
    selected_base_team_id_ = -1;
    selected_ant_ids_.clear();
    for (const auto& ant : world.ants) {
        if (ant.player_id == local_player_id_) selected_ant_ids_.push_back(ant.id);
    }
    unlatch_pedestals();
    is_multi_select_mode_ = (selected_ant_ids_.size() > 1);
    if (!selected_ant_ids_.empty()) {
        selected_ant_id_ = selected_ant_ids_.front();
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_) {
                voice_ready(ant.type);
                break;
            }
        }
    } else {
        selected_ant_id_ = 0;
    }
    selection_status_pending_ = true;
}

void HUD::select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world, bool additive) {
    // FUN_01027530 (drag select): the local team's ants, no state filter, whose hit box (FUN_01026a39) overlaps the rectangle with a positive
    // area (IntersectRect + non-empty test). The rectangle is [left, right) x [top, bottom) in map pixels.
    const int32_t rl = std::min(x1, x2);
    const int32_t rr = std::max(x1, x2);
    const int32_t rt = std::min(y1, y2);
    const int32_t rb = std::max(y1, y2);
    std::vector<uint32_t> picked;
    for (const auto& ant : world.ants) {
        if (ant.player_id != local_player_id_) continue;
        const bool combat = (ant.raw_type == sim::AntType::Combat);       // the own type field, as the hit box reads it (0x1026a3d)
        const int32_t al = ant.px - (combat ? 32 : 20);
        const int32_t ar = ant.px + (combat ? 26 : 20);
        const int32_t at = ant.py - (combat ? 46 : 32);
        const int32_t ab = ant.py + 16;
        if (std::max(rl, al) < std::min(rr, ar) && std::max(rt, at) < std::min(rb, ab)) picked.push_back(ant.id);
    }

    // Shift adds only to a selection of own ants (panels 3 and 4): nothing is cleared, nothing is said, an empty pick changes nothing
    const PanelMode panel = panel_mode(world);
    if (additive && (panel == PanelMode::OneAnt || panel == PanelMode::Ants)) {
        if (picked.empty()) return;
        for (uint32_t id : picked) {
            if (std::find(selected_ant_ids_.begin(), selected_ant_ids_.end(), id) == selected_ant_ids_.end()) selected_ant_ids_.push_back(id);
        }
        selected_base_team_id_ = -1;
        selected_ant_id_ = selected_ant_ids_.front();
        is_multi_select_mode_ = true;                      // panel 4 whatever the count: 0x1027731 calls FUN_01027f07 with mode 4 for any non-empty pick, and the
                                                           // group is then a group even when it holds the one bomber that was selected already
        selection_status_pending_ = true;                  // a shift-add rebuilds the panel and posts its text (FUN_01027f07 with its last argument 0, 0x1027950)
        unlatch_pedestals();
        return;
    }

    // Otherwise the old selection is cleared first, even when nothing is picked (dragging over empty ground deselects)
    selected_ant_ids_ = std::move(picked);
    selected_base_team_id_ = -1;
    selected_ant_id_ = selected_ant_ids_.empty() ? 0 : selected_ant_ids_.front();
    is_multi_select_mode_ = selected_ant_ids_.size() > 1;
    selection_status_pending_ = true;
    unlatch_pedestals();
    if (selected_ant_id_ != 0) {
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_) {
                voice_ready(ant.type);                     // the first ant answers (FUN_0101b5f9)
                break;
            }
        }
    }
}

// =========================================================================
// Input Dispatcher
// =========================================================================

bool HUD::handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                            sim::SimulationEngine& sim, ViewportCamera& camera, [[maybe_unused]] uint16_t mod) {
    if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT) return false;
    mouse_x_ = x;                                       // the event carries the pointer: the polled position of the original
    mouse_y_ = y;

    // 0. Overlays and Modals intercept clicks first
    // (the windows and the dialogs have the original's own numbers, drawn moved by the layout's window / modal offset: the pointer is taken back to them)
    const LayoutPoint page = open_window_offset();
    const LayoutPoint modal = layout_.modal_offset();
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT) quick_help_return_.on_press(x - page.x, y - page.y);     // the Return button captures (qh_return3 carries no sound); nothing else reacts
        return true;
    }
    if (options_.is_open()) {
        // The window takes every press; only the left button acts (the controls' mouse handlers test the left button message): the start of a slider
        // drag, a captured button, the focus of an edit field. Nothing acts before the release, and a press outside the controls does nothing.
        if (button == SDL_BUTTON_LEFT) options_.on_press(x - page.x, y - page.y, clock_ms());
        return true;
    }
    if (show_match_start_modal_ && !match_countdown_) {
        return true; // Consume all clicks while match start modal is open
    }
    if (alliance_dialog_ != AllianceDialog::None) {
        if (button == SDL_BUTTON_LEFT) {
            for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
                if (b->w > 0 && b->contains(x - modal.x, y - modal.y)) {
                    b->is_pressed = true;
                    play_sfx(sim::SoundID::ButtonClick);
                    return true;
                }
            }
        }
        return true;                                           // a dialog gets every click
    }
    if (show_quit_dialog_) {
        if (button == SDL_BUTTON_LEFT) {
            if (yes_button_.contains(x - modal.x, y - modal.y)) {
                yes_button_.is_pressed = true;
                play_sfx(sim::SoundID::ButtonClick);   // yes3 / no3 carry sound 0
                return true;
            }
            if (no_button_.contains(x - modal.x, y - modal.y)) {
                no_button_.is_pressed = true;
                play_sfx(sim::SoundID::ButtonClick);
                return true;
            }
        }
        return true; // Consume all clicks while dialog is open
    }
    if (show_match_start_modal_) return handle_countdown_mouse_down(x, y, button);          // (protocol 21: the countdown card, hud_match_start.cpp)

    // ProcessInput (0x102737e) ignores mouse events while the input is locked (after Stop, FUN_01028bdd) and while the cursor is a scroll
    // arrow (mode 6); the strip test does not run while a button is captured
    if (input_lock_ticks_ > 0) return true;
    if (!is_input_captured() &&
        edge_step(camera, x, y, 0, static_cast<int32_t>(sim.grid().width()), static_cast<int32_t>(sim.grid().height())).dir >= 0) {
        return true;
    }

    // The chat log window's own mouse handler (FUN_01012015, the left button): a press inside its view starts the drag
    if (button == SDL_BUTTON_LEFT && in_chat_view(x, y)) {
        start_chat_drag(x, y);
        return true;
    }

    if (radar_map_w_ == 0 && radar_map_h_ == 0 && sim.grid().width() > 0 && sim.grid().height() > 0) {
        radar_map_w_ = sim.grid().width();
        radar_map_h_ = sim.grid().height();
    }
    if (button == SDL_BUTTON_RIGHT) {
        // The press saves its point and captures the view under it (FUN_01028751); the order is given at the release (FUN_01027b51)
        right_press_x_ = x;
        right_press_y_ = y;
        right_capture_ = over_minimap(x, y) ? 2 : (over_map(x, y) ? 1 : 0);
        return right_capture_ != 0;
    }

    // The buttons of the top bar and the chat (the button class FUN_01011206): a press inside captures the button (pressed art, the click sound of its
    // animation); the callback runs at the release while the pointer is still on it (handle_mouse_up)
    if (help_button_.contains(x, y)) {
        help_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);   // the pressed top-bar animations carry sound 0
        return true;
    }
    if (options_button_.contains(x, y)) {
        options_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }
    if (quit_button_.contains(x, y)) {
        quit_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // The command pedestals fire on the press outside the map rectangle (FUN_010274be)
    if (pedestal_press(sim, x, y)) return true;

    // [All] button (532, 443, 44x24): hidden while chat is off
    if (options_.state().chat && send_to_button_.contains(x, y)) {
        send_to_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // [Team] button (579, 443, 46x24): exists while the local player has an ally
    if (options_.state().chat && is_on_team_ && team_button_.contains(x, y)) {
        team_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
        return true;
    }

    // The press captures the view under it (FUN_01028751): the minimap (the view follows in the input ticks while the button is held),
    // then the map (a rubber band that is decided at the release)
    if (over_minimap(x, y)) {
        is_radar_dragging_ = true;
        return true;
    }
    if (over_map(x, y)) {
        if (view_only_) return false;                           // (a replay: no rubber band)
        is_dragging_ = true;
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_curr_x_ = x;
        drag_curr_y_ = y;
        return true;
    }
    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod) {
    mouse_x_ = x;
    mouse_y_ = y;
    const bool captured_before = is_input_captured();      // [5534] != 0 while the event is processed
    if (button == SDL_BUTTON_LEFT) end_chat_drag();        // FUN_01012015: the release of the left button ends a drag of the chat log
    // The button class (FUN_01011206 / FUN_01011281): the callback runs at the release when the button is still captured, that is when the
    // pointer has not left it (leaving cancels the capture for good)
    const bool left_release = (button == SDL_BUTTON_LEFT);
    const bool fire_help = left_release && help_button_.is_pressed && help_button_.contains(x, y);
    const bool fire_options = left_release && options_button_.is_pressed && options_button_.contains(x, y);
    const bool fire_quit = left_release && quit_button_.is_pressed && quit_button_.contains(x, y);
    const bool fire_all = left_release && options_.state().chat && send_to_button_.is_pressed && send_to_button_.contains(x, y);
    const bool fire_team = left_release && options_.state().chat && is_on_team_ && team_button_.is_pressed && team_button_.contains(x, y);
    help_button_.is_pressed = false;
    options_button_.is_pressed = false;
    quit_button_.is_pressed = false;
    move_pedestal_button_.is_pressed = false;
    ability_pedestal_button_.is_pressed = false;
    stop_button_.is_pressed = false;
    send_to_button_.is_pressed = false;
    team_button_.is_pressed = false;
    hatch_button_.is_pressed = false;
    team_up_button_.is_pressed = false;
    is_radar_dragging_ = false;
    const LayoutPoint page = open_window_offset();                        // (the windows and the dialogs: see handle_mouse_down)
    const LayoutPoint modal = layout_.modal_offset();
    if (show_quick_help_) {
        if (button == SDL_BUTTON_LEFT && quick_help_return_.on_release(x - page.x, y - page.y)) close_quick_help();        // the callback runs at the release, on the button
        return true;
    }
    if (options_.is_open()) {
        // The callbacks run at the release: Return, the pairs of switches and the sliders' drags (FUN_010115ca, FUN_01011206)
        if (button == SDL_BUTTON_LEFT) options_.on_release(x - page.x, y - page.y);
        return true;
    }

    if (show_match_start_modal_ && !match_countdown_) {
        return true;
    }

    if (alliance_dialog_ != AllianceDialog::None) {
        for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
            if (!b->is_pressed) continue;
            b->is_pressed = false;
            if (b->contains(x - modal.x, y - modal.y)) answer_alliance_dialog(sim, b == &alliance_button_a_);      // the action runs at the release, on the button
            return true;
        }
        return true;
    }

    if (show_quit_dialog_) {
        if (yes_button_.is_pressed) {
            yes_button_.is_pressed = false;
            if (yes_button_.contains(x - modal.x, y - modal.y)) {
                close_quit_dialog();
                if (on_quit_) on_quit_();
            }
            return true;
        }
        if (no_button_.is_pressed) {
            no_button_.is_pressed = false;
            if (no_button_.contains(x - modal.x, y - modal.y)) {
                close_quit_dialog();
            }
            return true;
        }
        return true;
    }

    if (show_match_start_modal_) return handle_countdown_mouse_up(x, y, left_release, fire_quit, fire_all, fire_team);          // (protocol 21: the countdown card)

    // ProcessInput ignores the button events of an input lock and of a scroll arrow (the strip test is off while a button is captured)
    const bool right = (button == SDL_BUTTON_RIGHT);
    if (input_lock_ticks_ > 0) {
        is_dragging_ = false;
        right_capture_ = 0;
        return true;
    }
    const bool no_edge = captured_before || (right && right_capture_ != 0);
    const bool in_strip = !no_edge &&
        edge_step(camera, x, y, 0, static_cast<int32_t>(sim.grid().width()), static_cast<int32_t>(sim.grid().height())).dir >= 0;

    if (button == SDL_BUTTON_LEFT) {
        if (!in_strip) {
            if (fire_help) open_quick_help();
            else if (fire_options) open_options();
            else if (fire_quit) open_quit_dialog();
            else if (fire_all) send_chat(false);
            else if (fire_team) send_chat(true);
        }
        const bool was_dragging = is_dragging_;
        if (was_dragging) {                       // the band's last update is the release position (the original polls it every 50 ms)
            drag_curr_x_ = x;
            drag_curr_y_ = y;
        }
        if (!in_strip) pointer_release(sim, camera, x, y, is_shift_held() || (mod & KMOD_SHIFT) != 0);
        is_dragging_ = false;
        return was_dragging || over_map(x, y);
    }
    if (right) {
        const int capture = right_capture_;
        if (!in_strip) pointer_right_click(sim, camera, capture, right_press_x_, right_press_y_, x, y);
        right_capture_ = 0;
        return capture != 0;
    }
    return false;
}

bool HUD::handle_mouse_motion(int32_t x, int32_t y,
                              [[maybe_unused]] sim::SimulationEngine& sim, [[maybe_unused]] ViewportCamera& camera) {
    mouse_x_ = x;
    mouse_y_ = y;
    for (UIButton* b : {&help_button_, &options_button_, &quit_button_, &send_to_button_, &team_button_}) {
        if (b->is_pressed && !b->contains(x, y)) b->is_pressed = false;      // FUN_01011281: leaving cancels the capture for good
    }
    send_to_button_.is_hovered = send_to_button_.contains(x, y);
    team_button_.is_hovered = is_on_team_ && team_button_.contains(x, y);
    help_button_.is_hovered = help_button_.contains(x, y);
    options_button_.is_hovered = options_button_.contains(x, y);
    quit_button_.is_hovered = quit_button_.contains(x, y);

    const LayoutPoint page = open_window_offset();                        // (the windows and the dialogs: see handle_mouse_down)
    const LayoutPoint modal = layout_.modal_offset();
    if (show_quick_help_) {
        quick_help_return_.on_move(x - page.x, y - page.y);
        return true;
    }
    if (options_.is_open()) {
        options_.on_move(x - page.x, y - page.y);                   // hover pictures; a dragged thumb follows the pointer (only the thumb: the value is applied at the release)
        return true;
    }

    // The dialogs' buttons are the button class too (FUN_01011281): leaving a pressed button cancels its capture for good, coming back only hovers
    if (alliance_dialog_ != AllianceDialog::None) {
        const int32_t mx = x - modal.x;
        const int32_t my = y - modal.y;
        for (UIButton* b : {&alliance_button_a_, &alliance_button_b_}) {
            if (b->is_pressed && !b->contains(mx, my)) b->is_pressed = false;
        }
        alliance_button_a_.is_active = alliance_button_a_.w > 0 && alliance_button_a_.contains(mx, my);
        alliance_button_b_.is_active = alliance_button_b_.w > 0 && alliance_button_b_.contains(mx, my);
        return true;
    }

    if (show_quit_dialog_) {
        const int32_t mx = x - modal.x;
        const int32_t my = y - modal.y;
        for (UIButton* b : {&yes_button_, &no_button_}) {
            if (b->is_pressed && !b->contains(mx, my)) b->is_pressed = false;
        }
        yes_button_.is_active = yes_button_.contains(mx, my);
        no_button_.is_active = no_button_.contains(mx, my);
        return true;
    }

    if (chat_dragging_) {                          // FUN_01012096
        move_chat_drag(x, y);
        return true;
    }

    if (is_dragging_) {
        drag_curr_x_ = x;
        drag_curr_y_ = y;
        return true;
    }

    if (is_radar_dragging_) return true;

    return false;
}

void HUD::cancel_press() noexcept {
    release_capture();
    options_.cancel_press();                                // (a slider's drag of the options window ends with no act: nothing is written)
    for (UIButton* b : {&help_button_, &options_button_, &quit_button_, &send_to_button_, &team_button_, &move_pedestal_button_, &ability_pedestal_button_, &stop_button_, &hatch_button_, &team_up_button_}) {
        b->is_pressed = false;
    }
}

bool HUD::is_input_captured() const noexcept {
    // [5534] != 0: the map, the minimap (left or right button) or a button holds the mouse; the pedestals do not capture
    return is_dragging_ || is_radar_dragging_ || right_capture_ != 0 || help_button_.is_pressed || options_button_.is_pressed ||
           quit_button_.is_pressed || send_to_button_.is_pressed || team_button_.is_pressed;
}

// The step of the edge scroll for the camera's zoom (edge_scroll.hpp): the origin and the map in SCREEN pixels at the zoom, the step in screen pixels. At the zoom 1 these are the
// camera's world origin and the map's own size: the function that was always called.
EdgeScroll HUD::edge_step(const ViewportCamera& camera, int32_t x, int32_t y, int32_t rate, int32_t map_tiles_w, int32_t map_tiles_h) const noexcept {
    if (camera.zoom == zoom::kNormal) return edge_scroll_step(x, y, rate, camera.world_x, camera.world_y, map_tiles_w, map_tiles_h, layout_);
    return edge_scroll_step_px(x, y, rate, camera.origin_screen_x(), camera.origin_screen_y(), zoom::map_screen(static_cast<int64_t>(map_tiles_w) * 32, camera.zoom),
                               zoom::map_screen(static_cast<int64_t>(map_tiles_h) * 32, camera.zoom), layout_);
}

} // namespace ants::app
