// The keys and the options of the match screen (hud.hpp): the input tick of the view, the key presses, the typed text, the send of the chat line, the options' callbacks and the quick chats.
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ants::app {

// The INPUT task (0x100ae26, every 50 ms) as far as the view is concerned: FUN_01026aa3 for the edge strips, or, while the left button
// is held on the minimap, FUN_01009850. The step is in screen pixels (the same distance on the screen at every zoom): the camera moves by step / zoom world pixels.
bool HUD::input_tick(ViewportCamera& camera, uint32_t map_w, uint32_t map_h, int32_t mouse_x, int32_t mouse_y) {
    if (is_modal_open() || map_w == 0 || map_h == 0) return false;
    const int32_t rate = options_.state().scroll_speed;       // the profile's Scroll Speed 0 .. 99 (FUN_01027329: the half extent is rate + 10)
    EdgeScroll step;
    if (is_radar_dragging_) {
        step = minimap_scroll_step_px(mouse_x, mouse_y, camera.origin_screen_x(), camera.origin_screen_y(), zoom::map_screen(static_cast<int64_t>(map_w) * 32, camera.zoom),
                                      zoom::map_screen(static_cast<int64_t>(map_h) * 32, camera.zoom), layout_,
                                      layout_.minimap_for_map(static_cast<int32_t>(map_w), static_cast<int32_t>(map_h)));
    } else if (!is_input_captured()) {
        step = edge_step(camera, mouse_x, mouse_y, rate, static_cast<int32_t>(map_w), static_cast<int32_t>(map_h));
    }
    if (step.dx == 0 && step.dy == 0) return false;
    camera.scroll_screen(step.dx, step.dy, map_w, map_h);
    return true;
}

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod, bool repeat) {
    // FUN_0102609a. A dialog takes every key first.
    if (show_match_start_modal_ && !match_countdown_) return true;

    if (alliance_dialog_ != AllianceDialog::None) {            // the dialogs' own key handlers (0x1016015, 0x10163d5, 0x1016761): a dialog takes every key
        const bool esc = key == SDLK_ESCAPE;
        switch (alliance_dialog_) {
            case AllianceDialog::Invitation:                   // A = Accept, D and Esc = Decline
                if (key == 'a' || key == 'A') answer_alliance_dialog(sim, true);
                else if (key == 'd' || key == 'D' || esc) answer_alliance_dialog(sim, false);
                break;
            case AllianceDialog::Waiting:                      // W and Esc = Withdraw
                if (key == 'w' || key == 'W' || esc) answer_alliance_dialog(sim, true);
                break;
            case AllianceDialog::BreakConfirm:                 // Y = Yes, N and Esc = No
                if (key == 'y' || key == 'Y') answer_alliance_dialog(sim, true);
                else if (key == 'n' || key == 'N' || esc) answer_alliance_dialog(sim, false);
                break;
            case AllianceDialog::None:
                break;
        }
        return true;
    }

    if (show_quit_dialog_) {                                   // Y = Yes, N and Esc = No, Enter does nothing
        if (key == 'y' || key == 'Y') {
            close_quit_dialog();
            if (on_quit_) on_quit_();
        } else if (key == 'n' || key == 'N' || key == SDLK_ESCAPE) {
            close_quit_dialog();
        }
        return true;
    }

    if (show_quick_help_) {                                    // C, X, Enter and Esc close it
        if (key == 'c' || key == 'C' || key == 'x' || key == 'X' || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            close_quick_help();
        }
        return true;
    }

    if (options_.is_open()) {
        // FUN_01014f12: Enter closes the window whatever has the focus; Backspace goes to the edit field that has it; Esc does nothing. Printable keys arrive
        // as text input (SDL_TEXTINPUT), not as key events.
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) options_.on_key(ScreenEdit::KEY_ENTER);
        else if (key == SDLK_BACKSPACE) options_.on_key(ScreenEdit::KEY_BACKSPACE);
        else if (key == SDLK_ESCAPE) options_.on_key(ScreenEdit::KEY_ESCAPE);
        return true;
    }

    if (show_match_start_modal_) return handle_countdown_key(key, mod, repeat);          // (protocol 21: the countdown card takes the keys of the chat)

    const bool ctrl = (mod & KMOD_CTRL) != 0 || (mod & KMOD_GUI) != 0;
    const auto& world = sim.get_world_state();
    if (view_only_ && ctrl && (key == 'a' || key == 'A' || key == 'h' || key == 'H' || key == 'n' || key == 'N' || key == 'p' || key == 'P' || key == 's' || key == 'S')) return true;     // (a replay picks nothing)

    // The chat edit control is always active while chat is on: it takes 0x20 - 0x7E and Backspace unless Ctrl is held
    if (options_.state().chat && !ctrl) {
        if (key >= 32 && key <= 126) {
            if (chat_input_.size() < kChatInputMax) chat_input_.push_back(static_cast<char>(key));
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            if (!chat_input_.empty()) chat_input_.pop_back();
            return true;
        }
    }

    // Keys of any modifier state
    switch (key) {
        case SDLK_F1:                                          // quick help (page 0)
            open_quick_help();
            return true;
        case SDLK_F9: case SDLK_F10: case SDLK_F11: case SDLK_F12:
            if (!repeat && options_.state().chat) trigger_quick_chat(static_cast<size_t>(key - SDLK_F9));
            return true;
        case SDLK_RETURN: case SDLK_KP_ENTER:                  // FUN_010103eb: the team when the player has an ally, else everybody
            send_chat_message();
            return true;
        case SDLK_ESCAPE: {                                    // FUN_01028c44(0): deselect everything, no quit dialog; with nothing to deselect, a first press asks for the settings menu
            const bool nothing_to_cancel = !has_selection() && !slot_latched_[0] && !slot_latched_[1];
            clear_selection();
            unlatch_pedestals();
            if (nothing_to_cancel && !repeat && on_menu_request_) on_menu_request_();
            return true;
        }
        default:
            break;
    }

    if (!ctrl) return false;
    switch (key) {
        case 'a': case 'A':                                    // select all own ants, panel 3 or 4, the voice of the first
            select_all_friendly(world);
            return true;
        case 'h': case 'H':                                    // the home hill (panel 2); no hatch, no scrolling
            clear_selection();
            unlatch_pedestals();
            select_base(local_player_id_);
            return true;
        case 'l': case 'L':                                    // the ant health marks: off, or on as they were before
            set_ant_health({health_.when == health::When::Off ? health_resume_ : health::When::Off, health_.style});
            return true;
        case 'o': case 'O':
            open_options();
            return true;
        case 'q': case 'Q':
            open_quit_dialog();
            return true;
        case 's': case 'S': {                                  // the stop order, without flash, lock or deselect (panels 3 and 4)
            const PanelMode panel = panel_mode(world);
            // (a held key is not a stream of orders: a Stop of a big selection is up to 48 commands, and 20 repeats a second of it would fill the sequencer's carry and cost the player its seat)
            if (!repeat && (panel == PanelMode::OneAnt || panel == PanelMode::Ants)) stop_selected(sim);
            return true;
        }
        case 'n': case 'N': case 'p': case 'P': {
            // Next / previous own ant (FUN_0102609a 0x1026330): the search starts at the lowest selected slot (nothing selected: the last one), steps
            // by one in the direction, the selection is replaced (panel 3, no voice) and the view scrolls just far enough to show the +-128 px square
            std::vector<const sim::AntSnapshot*> mine;
            for (const auto& a : world.ants) {
                if (a.player_id == local_player_id_) mine.push_back(&a);
            }
            if (mine.empty()) return true;
            const int64_t count = static_cast<int64_t>(mine.size());
            int64_t start = count - 1;
            for (int64_t i = 0; i < count; ++i) {
                if (is_ant_selected(mine[static_cast<size_t>(i)]->id)) { start = i; break; }
            }
            const int64_t dir = (key == 'n' || key == 'N') ? 1 : -1;
            const sim::AntSnapshot* ant = mine[static_cast<size_t>((start + count + dir) % count)];
            select_ant(ant->id, false);
            unlatch_pedestals();
            const int32_t map_w = static_cast<int32_t>(world.width) * 32;
            const int32_t map_h = static_cast<int32_t>(world.height) * 32;
            int32_t dx = 0;
            int32_t dy = 0;
            // (at a zoom the view shows view / zoom world pixels, and its origin may lie half a pixel inside a world pixel: the scroll counts from the whole part, so the left / top edge of
            // the square, a margin around the ant, may end up half a world pixel = one screen pixel outside the view)
            const bool plain = camera.zoom == zoom::kNormal;
            const int32_t ox = plain ? camera.world_x : static_cast<int32_t>(std::floor(camera.x));
            const int32_t oy = plain ? camera.world_y : static_cast<int32_t>(std::floor(camera.y));
            detail::scroll_to_show(std::max(ant->px - 128, 0), std::max(ant->py - 128, 0), std::min(ant->px + 128, map_w), std::min(ant->py + 128, map_h),
                                   ox, oy, dx, dy, zoom::visible(layout_.view().w, camera.zoom), zoom::visible(layout_.view().h, camera.zoom));
            if (plain) {
                camera.x = static_cast<float>(camera.world_x);          // (both fields describe the same origin)
                camera.y = static_cast<float>(camera.world_y);
            }
            camera.scroll_pixels(dx, dy, world.width, world.height);
            return true;
        }
        default:
            return false;                                      // Ctrl+B / C / F / M / T ... do nothing in the original
    }
}

void HUD::handle_text_input(const std::string& text) {
    if (text.empty()) return;
    if (options_.is_open()) {
        options_.on_text(text);                   // the focused edit field takes the printable characters (at most 100)
        return;
    }
    if (show_quit_dialog_ || show_quick_help_ || (show_match_start_modal_ && !match_countdown_) || !options_.state().chat) return;     // a dialog or the chat cover takes the keys (the countdown card does not: protocol 21)
    for (char c : text) {
        if (c >= 32 && c <= 126) {
            if (chat_input_.size() < kChatInputMax) {
                chat_input_.push_back(c);
            }
        }
    }
}

void HUD::send_chat(bool to_team) {
    if (chat_input_.empty()) return;
    if (!options_.state().chat) {                     // "Participate In Chat" off: the box is covered and nothing is sent
        chat_input_.clear();
        return;
    }
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, chat_input_, to_team && is_on_team_);
    if (on_chat_send_) on_chat_send_(chat_input_, to_team && is_on_team_);
    chat_input_.clear();
}

// The callbacks of the options screen (FUN_01015058 / FUN_01015092 / FUN_010150bc, FUN_01014f5a .. FUN_0101501f, FUN_0100bbf2): the profile is written first, then the
// setting is applied. The Sound Volume sets the volume and plays the test voice (gantrdy, 0x102bd7e), the Music Volume sets the music volume and restarts the
// track (the owner's callback). The Scroll Speed is read by the input tick, the two switches and the quick chats by the chat box and the start of the program.
void HUD::apply_option(OptionSetting setting) {
    const OptionsState& state = options_.state();
    if (config_store_ != nullptr) state.write(*config_store_, setting);
    switch (setting) {
        case OptionSetting::SoundVolume:
            if (on_sfx_volume_) on_sfx_volume_(state.sound_volume);
            play_sfx(sim::SoundID::GeneralReady);
            break;
        case OptionSetting::MusicVolume:
            if (on_music_volume_) on_music_volume_(state.music_volume);
            break;
        default:
            break;
    }
}

void HUD::set_option(OptionSetting setting, int32_t value) {
    OptionsState& state = options_.state();
    switch (setting) {
        case OptionSetting::SoundVolume: state.sound_volume = std::clamp<int32_t>(value, 0, 100); break;
        case OptionSetting::MusicVolume: state.music_volume = std::clamp<int32_t>(value, 0, 100); break;
        case OptionSetting::ScrollSpeed: state.scroll_speed = std::clamp<int32_t>(value, 0, 99); break;
        case OptionSetting::Chat:        state.chat = value != 0; break;
        case OptionSetting::QuickHelp:   state.quick_help = value != 0; break;
        default: return;
    }
    apply_option(setting);
}

void HUD::trigger_quick_chat(size_t index) {
    if (index >= 4 || !options_.state().chat) return;
    if (options_.state().quick_chat[index].empty()) return;
    add_chat_entry(player_name_.empty() ? "Player" : player_name_, options_.state().quick_chat[index], false);   // F9 - F12 always go to all
    if (on_chat_send_) on_chat_send_(options_.state().quick_chat[index], false);
}

} // namespace ants::app
