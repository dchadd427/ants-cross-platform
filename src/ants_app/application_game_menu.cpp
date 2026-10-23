// The settings menu in the application (game_menu.hpp is the model and the one table of rows; docs/GAME_MENU.md): what the page's panel (web/game_menu.js) reads and changes through
// the exports at the end of this file, what the game does while the panel is up (its keys, text, clicks and finger are not the game's), and the three settings that did not exist before the menu.
// Nothing here touches the simulation: sound, display and the HUD's own flags are local, and Leave game is the quit dialog's Yes (confirm_quit), so the Quit command, a drop out, the recording and the
// hashes are what they were.

#include <algorithm>
#include <string>

#include "ants_app/application.hpp"
#include "ants_app/game_menu.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>

extern "C" {
// Esc found nothing to cancel: the page's panel opens (a page without it, or an embedded frame, has no function of this name and Esc stays what it was). EM_JS and not EM_ASM: see application.cpp.
EM_JS(void, ants_ask_game_menu, (), {
    try { if (window.antsGameMenuAsk) window.antsGameMenuAsk(); } catch (e) {}
});
}
#endif

namespace ants::app {

// The three new settings come back from the settings file; the hit point numbers are the HUD's flag (on, as ever, until a player turns them off)
void Application::load_game_menu_settings() {
    menu_settings_.load(config_store_);
    hud_.set_show_hp(menu_settings_.hp_numbers);
    audio_mixer_.set_output_gain(menu_settings_.gain());
}

// Ctrl+L flips the HUD's flag: the menu's setting follows it, and is written, once a frame
void Application::sync_game_menu_settings() {
    if (hud_.is_show_hp() == menu_settings_.hp_numbers) return;
    menu_settings_.hp_numbers = hud_.is_show_hp();
    menu_settings_.write(config_store_, game_menu::kHpNumbers);
}

// Esc found nothing to cancel (the HUD's rule): where there is a panel it opens: the page's in the browser (a page without one, or an embedded frame, leaves Esc what it was), the function that a skin
// of the game itself sets (the tests'; the desktop's later). The desktop game has none yet: Esc stays what it was there.
void Application::request_game_menu() {
    if (game_menu_where() < 0 || game_menu_needs_answer()) return;
    if (on_game_menu_) {
        on_game_menu_();
        return;
    }
#if defined(__EMSCRIPTEN__)
    ants_ask_game_menu();
#endif
}

// -1: no match screen (the loading, the room, the results); 0: a match; 1: a recording or a live match that is watched
int Application::game_menu_where() const {
    if (state_ != AppState::Playing || scorecard_.is_open()) return -1;
    return replay_mode_ ? 1 : 0;
}

// A dialog of the match, a vote, a lost link or the catch-up screen needs its answer first: the panel does not open over it and closes when one comes up
bool Application::game_menu_needs_answer() const {
    if (hud_.is_modal_open() || catch_up_screen_active()) return true;
    if (!network_active()) return false;
    const net::PauseInfo info = net_->pause_info();
    return info.vote_open || info.reconnecting;
}

// What Leave game would do (the rules of confirm_quit, in the same order)
int Application::game_menu_leave_kind() const {
    const bool held = network_active() && (net_->paused() || catch_up_screen_active() || hud_.is_match_start_modal_active());
    const bool running = state_ == AppState::Playing && !sim_.is_match_over();
    return static_cast<int>(game_menu::leave_kind(replay_mode_, held, network_active(), sim_.other_sides(local_player_id_), running));
}

void Application::game_menu_set_open(bool open) {
    if (open) {
        game_menu_open_ = true;
        game_menu_closing_ = false;
        hud_.release_input();                                     // (a drag or a held button that began before the panel is not finished under it)
        cancel_touch_queued();
    } else if (game_menu_open_) {
        game_menu_closing_ = true;                                // the events that are queued now (the key that closed the panel) are the panel's: the game takes input again after this frame's events
    }
}

// What the game does with an event while the panel is up: its keys, its text, its clicks and its wheel are not the game's (a lift of a button still ends what the button began)
bool Application::game_menu_takes(const SDL_Event& event) const noexcept {
    if (!game_menu_open_) return false;
    switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEWHEEL:
            return true;
        default:
            return false;
    }
}

void Application::end_game_menu_frame() {
    if (game_menu_closing_) {
        game_menu_open_ = false;
        game_menu_closing_ = false;
    }
}

// The value of a row, or of a fact (game_menu::Fact), as the number that the page's exports carry; -1 for what is not the game's
int Application::game_menu_get(int what) {
    namespace gm = game_menu;
    switch (what) {
        case gm::kFactWhere:       return game_menu_where();
        case gm::kFactNeedsAnswer: return game_menu_needs_answer() ? 1 : 0;
        case gm::kFactLeave:       return game_menu_leave_kind();
        case gm::kMaster:          return menu_settings_.master;
        case gm::kMusic:           return hud_.get_music_volume();
        case gm::kEffects:         return hud_.get_sound_volume();
        case gm::kMute:            return menu_settings_.mute ? 1 : 0;
        case gm::kHpNumbers:       return hud_.is_show_hp() ? 1 : 0;
        case gm::kScroll:          return hud_.get_scroll_speed();
        case gm::kChat:            return hud_.is_chat_enabled() ? 1 : 0;
        case gm::kQuickHelpAtStart: return hud_.is_quick_help_enabled() ? 1 : 0;
        default:                   return -1;
    }
}

// A change from the panel: stored and applied as the original's options window does it (the same state, so that the two always agree). True when the row is the game's and the value was taken.
bool Application::game_menu_set(int id, int value) {
    namespace gm = game_menu;
    switch (id) {
        case gm::kMaster:
            menu_settings_.master = std::clamp(value, 0, gm::MenuSettings::kMasterMax);
            menu_settings_.write(config_store_, id);
            audio_mixer_.set_output_gain(menu_settings_.gain());
            return true;
        case gm::kMute:
            menu_settings_.mute = value != 0;
            menu_settings_.write(config_store_, id);
            audio_mixer_.set_output_gain(menu_settings_.gain());
            return true;
        case gm::kHpNumbers:
            hud_.set_show_hp(value != 0);
            sync_game_menu_settings();
            return true;
        case gm::kMusic:           hud_.set_option(OptionSetting::MusicVolume, value); return true;
        case gm::kEffects:         hud_.set_option(OptionSetting::SoundVolume, value); return true;
        case gm::kScroll:          hud_.set_option(OptionSetting::ScrollSpeed, value); return true;
        case gm::kChat:            hud_.set_option(OptionSetting::Chat, value); return true;
        case gm::kQuickHelpAtStart: hud_.set_option(OptionSetting::QuickHelp, value); return true;
        default:                   return false;
    }
}

// What the panel asks of the game: Leave game (the quit dialog's Yes), the original's quick help (F1) and options window (Ctrl+O). Only on a match screen that no dialog covers.
bool Application::game_menu_do(int action) {
    namespace gm = game_menu;
    if (game_menu_where() < 0) return false;
    if (action == gm::kActLeave) {
        game_menu_open_ = false;
        game_menu_closing_ = false;
        confirm_quit();
        return true;
    }
    if (game_menu_needs_answer() || replay_mode_) return false;
    if (action == gm::kActQuickHelp) hud_.open_quick_help();
    else if (action == gm::kActOptionsWindow) hud_.open_options();
    else return false;
    return true;
}

const std::string& Application::game_menu_describe() {
#if defined(__EMSCRIPTEN__)
    constexpr bool kWeb = true;
#else
    constexpr bool kWeb = false;
#endif
    game_menu_json_ = game_menu::describe_json(replay_mode_, kWeb);
    return game_menu_json_;
}

}  // namespace ants::app

#if defined(__EMSCRIPTEN__)
extern ants::app::Application* g_web_app;

// For the page's panel (web/game_menu.js): what the settings menu is made of, as one JSON text (game_menu::describe_json) for the screen that is up, a match or a recording; read with UTF8ToString. The
// text is the game's and stays where it is until the next call. "" when there is no game.
extern "C" EMSCRIPTEN_KEEPALIVE const char* ants_menu_describe() {
    static const std::string none;
    return g_web_app == nullptr ? none.c_str() : g_web_app->game_menu_describe().c_str();
}

// ... a row's value or a fact (game_menu::RowId, game_menu::Fact); -1 for anything else or no game
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_get(int what) {
    return g_web_app == nullptr ? -1 : g_web_app->game_menu_get(what);
}

// ... a change of a row that is the game's: 1 when it was taken
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_set(int id, int value) {
    return g_web_app != nullptr && g_web_app->game_menu_set(id, value) ? 1 : 0;
}

// ... an act (game_menu::Action): Leave game, the quick help, the options window; 1 when the game did it
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_do(int action) {
    return g_web_app != nullptr && g_web_app->game_menu_do(action) ? 1 : 0;
}

// ... and the panel is up (1) or gone (0): while it is up the game ignores its keys, text, clicks, wheel and fingers, and does not scroll at the edges
extern "C" EMSCRIPTEN_KEEPALIVE void ants_menu_open(int open) {
    if (g_web_app != nullptr) g_web_app->game_menu_set_open(open != 0);
}
#endif
