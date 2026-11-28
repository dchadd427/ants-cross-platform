// The settings menu in the application (game_menu.hpp is the model and the one table of rows; docs/GAME_MENU.md): what the page's panel (web/game_menu.js) reads and changes through
// the exports at the end of this file, what the game does while the panel is up (its keys, text, clicks and finger are not the game's), and the settings that did not exist before the menu.
// Nothing here touches the simulation: sound, display and the HUD's own flags are local, and Leave game is the quit dialog's Yes (confirm_quit), so the Quit command, a drop out, the recording and the
// hashes are what they were.

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include "ants_app/application.hpp"
#include "ants_app/game_menu.hpp"
#include "ants_app/version.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>

extern "C" {
// Esc found nothing to cancel: the page's panel opens (a page without it, or an embedded frame, has no function of this name and Esc stays what it was). EM_JS and not EM_ASM: see application_sim.cpp.
EM_JS(void, ants_ask_game_menu, (), {
    try { if (window.antsGameMenuAsk) window.antsGameMenuAsk(); } catch (e) {}
});
}
#endif

namespace ants::app {

// The new settings come back from the settings file; the ant health marks are the HUD's (hurt ants with a bar, until a player chooses otherwise)
void Application::load_game_menu_settings() {
    menu_settings_.load(config_store_);
    if (config_.hud_art_given) menu_settings_.hud_art_new = config_.hud_art_new;            // (--hud-art says it for this run; the settings file is not changed by it)
    if (config_.show_ping_given) menu_settings_.show_ping = config_.show_ping;              // (--show-ping likewise)
    if (config_.show_fps_given) menu_settings_.show_fps = config_.show_fps;                 // (--show-fps likewise)
    hud_.set_ant_health(menu_settings_.ant_health);
    audio_mixer_.set_output_gain(menu_settings_.gain());
}

// The HUD art: the manifest is read once; the sheets are decoded when the renderer first needs the sheet of its output's scale (renderer_art.cpp), which preload_art does now, before the first match frame.
// A folder without the art (a build that was moved without it) leaves the original's art, with a word on stderr; the setting is not changed by that.
void Application::load_hud_art() {
    if (!hud_art_.load(config_.hud_art_dir) && menu_settings_.hud_art_new) {
        std::cerr << "[Application] The HUD art is not available (" << hud_art_.what() << "): the original's art is drawn" << std::endl;
    }
    apply_hud_art();
}

void Application::apply_hud_art() {
    const bool show = menu_settings_.hud_art_new && hud_art_.loaded();
    hud_.set_art(show ? &hud_art_ : nullptr);
    check_hud_art();
}

// The new art needs the texture of the sheet of the output's scale. A sheet whose file is damaged after its header (a cut copy), or that the device cannot make a texture of, would draw nothing at all, not even
// the original's pieces: the HUD is given the original's art instead, with a word on stderr (the renderer says which sheet). Once a frame (a window that moves to another screen needs another sheet), and when the art is given.
// The setting is not changed by it; choosing New again tries again.
void Application::check_hud_art() {
    if (renderer_ && hud_art_.loaded()) renderer_->set_dialog_font_path(hud_art_.folder() + "LibreBaskerville-Regular.ttf");        // (the face of the new windows' sentences ships with the art; without it they are the original's text)
    if (hud_.art() == nullptr || !renderer_ || renderer_->preload_art(hud_art_)) return;
    std::cerr << "[Application] The HUD art's sheet of this output cannot be used: the original's art is drawn" << std::endl;
    hud_.set_art(nullptr);
}

// Ctrl+L flips the HUD's marks: the menu's setting follows it, and what changed is written, once a frame
void Application::sync_game_menu_settings() {
    const health::Setting now = hud_.ant_health();
    const health::Setting before = menu_settings_.ant_health;
    if (now == before) return;
    menu_settings_.ant_health = now;
    if (now.when != before.when) menu_settings_.write(config_store_, game_menu::kAntHealth);
    if (now.style != before.style) menu_settings_.write(config_store_, game_menu::kHealthStyle);
}

// Esc found nothing to cancel (the HUD's rule), or Ctrl+O, or the Options button: where there is a panel it opens: the page's in the browser (a page without one, or an embedded frame, leaves them what they were),
// the function that a skin of the game itself sets (the tests'), else the desktop's own drawn panel (game_menu_panel.hpp).
void Application::request_game_menu() {
    if (game_menu_where() < 0 || game_menu_needs_answer()) return;
    if (on_game_menu_) {
        on_game_menu_();
        return;
    }
#if defined(__EMSCRIPTEN__)
    ants_ask_game_menu();
#else
    open_game_menu_panel();
#endif
}

// ---- the desktop's drawn panel ------------------------------------------------------------------------------------------------------------------------------

namespace {

/// What the drawn panel asks of the game: the Application's own functions that the page's panel uses through its exports
class AppMenuHost final : public GameMenuHost {
public:
    explicit AppMenuHost(Application& app) : app_(app) {}
    int get(int what) override { return app_.game_menu_get(what); }
    bool set(int id, int value, bool save) override { return app_.game_menu_set(id, value, save); }
    std::string text(int id) override { return app_.game_menu_text(id); }
    void set_text(int id, const std::string& text) override { app_.game_menu_set_text(id, text); }
    bool act(int action) override { return app_.game_menu_do(action); }
    bool fullscreen() override { return app_.fullscreen_now(); }
    std::string version() override { return std::string("v") + std::string(ants::VERSION_STRING); }

private:
    Application& app_;
};

}  // namespace

void Application::make_game_menu_panel() {
    menu_host_ = std::make_unique<AppMenuHost>(*this);
    menu_panel_ = std::make_unique<GameMenuPanel>(*menu_host_);
    menu_panel_->set_on_close([this]() {
        game_menu_set_open(false);
        hud_.set_menu_latched(false);
    });
}

void Application::open_game_menu_panel() {
    if (!menu_panel_ || menu_panel_->is_open()) return;
    menu_panel_->open(replay_mode_);
    game_menu_set_open(true);
    hud_.set_menu_latched(true);
}

// Once a frame: the panel lies in the picture as the picture is now, and goes when there is no match on the screen or a dialog, a vote or a lost link needs its answer (the page's panel does the same)
void Application::update_game_menu_panel() {
    if (!menu_panel_ || !menu_panel_->is_open()) return;
    if (game_menu_where() < 0 || game_menu_needs_answer()) {
        menu_panel_->close();
        return;
    }
    menu_panel_->set_picture(picture_.w, picture_.h, renderer_ ? renderer_->output_scale() : 1.0);
}

// The events of the window while the panel is up. The pointer is the picture's (the cursor is drawn there), and a press that the window loses ends without an act.
bool Application::feed_game_menu_panel(const SDL_Event& event) {
    if (!menu_panel_ || !menu_panel_->is_open()) return false;
    menu_panel_->set_picture(picture_.w, picture_.h, renderer_ ? renderer_->output_scale() : 1.0);
    switch (event.type) {
        case SDL_KEYDOWN:
            menu_panel_->on_key(event.key.keysym.sym, event.key.repeat != 0);
            return true;
        case SDL_TEXTINPUT:
            menu_panel_->on_text(event.text.text);
            return true;
        case SDL_MOUSEMOTION:
            mouse_screen_x_ = event.motion.x;
            mouse_screen_y_ = event.motion.y;
            mouse_has_moved_ = true;
            pointer_outside_ = false;
            menu_panel_->on_mouse_move(event.motion.x, event.motion.y);
            return true;
        case SDL_MOUSEBUTTONDOWN:
            mouse_screen_x_ = event.button.x;
            mouse_screen_y_ = event.button.y;
            mouse_has_moved_ = true;
            pointer_outside_ = false;
            menu_panel_->on_mouse_down(event.button.x, event.button.y, event.button.button);
            return true;
        case SDL_MOUSEBUTTONUP:
            mouse_screen_x_ = event.button.x;
            mouse_screen_y_ = event.button.y;
            menu_panel_->on_mouse_up(event.button.x, event.button.y, event.button.button);
            return true;
        case SDL_MOUSEWHEEL:
            menu_panel_->on_wheel(event.wheel.y);
            return true;
        default:
            return false;
    }
}

void Application::render_game_menu_panel() {
    if (!menu_panel_ || !menu_panel_->is_open() || !renderer_) return;
    menu_panel_->set_picture(picture_.w, picture_.h, renderer_->output_scale());
    menu_panel_->render(*renderer_, hud_art_.loaded() ? &hud_art_ : nullptr, SDL_GetTicks());
}

// The text of a Quick Chat row, the original's "Quick Chat F9" .. "Quick Chat F12": the HUD keeps it (F9 - F12 say it)
std::string Application::game_menu_text(int id) const {
    if (id < game_menu::kQuickChat1 || id > game_menu::kQuickChat4) return std::string();
    return hud_.get_quick_chat_key(static_cast<size_t>(id - game_menu::kQuickChat1));
}

void Application::game_menu_set_text(int id, const std::string& text) {
    if (id < game_menu::kQuickChat1 || id > game_menu::kQuickChat4) return;
    hud_.set_quick_chat(static_cast<size_t>(id - game_menu::kQuickChat1), text);
}

bool Application::game_menu_text_edit(int id, int index, int code) {
    if (id < game_menu::kQuickChat1 || id > game_menu::kQuickChat4) return false;
    std::string& text = menu_text_[static_cast<size_t>(id - game_menu::kQuickChat1)];
    if (index < 0) {                                                  // the edit is over: the text is kept and written once
        game_menu_set_text(id, text);
        return true;
    }
    if (index == 0) text.clear();
    if (code >= 0x20 && code <= 0x7e && text.size() < static_cast<size_t>(game_menu::kQuickChatMax)) text += static_cast<char>(code);
    return true;
}

// -1: no match screen (the loading, the room, the results); 0: a match; 1: a recording or a live match that is watched
int Application::game_menu_where() const {
    if (state_ != AppState::Playing || scorecard_.is_open()) return -1;
    return replay_mode_ ? 1 : 0;
}

// A dialog of the match, a vote, a lost link or the catch-up screen needs its answer first: the panel does not open over it and closes when one comes up
bool Application::game_menu_needs_answer() const {
    const bool networked = network_active();
    const net::PauseInfo info = networked ? net_->pause_info() : net::PauseInfo{};
    return game_menu::needs_answer(hud_.is_modal_open(), catch_up_screen_active(), networked, info.vote_open, info.reconnecting);
}

// The match is held: confirm_quit and Leave game's kind read the same rule
bool Application::match_held() const {
    const bool networked = network_active();
    return game_menu::match_is_held(networked, networked && net_->paused(), catch_up_screen_active(), hud_.is_match_start_modal_active());
}

// What Leave game would do (the rules of confirm_quit, in the same order)
int Application::game_menu_leave_kind() const {
    const bool running = state_ == AppState::Playing && !sim_.is_match_over();
    return static_cast<int>(game_menu::leave_kind(replay_mode_, match_held(), network_active(), sim_.other_sides(local_player_id_), running));
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
        case gm::kAntHealth:       return static_cast<int>(hud_.ant_health().when);
        case gm::kHealthStyle:     return static_cast<int>(hud_.ant_health().style);
        case gm::kHealthChanges:   return menu_settings_.health_changes ? 1 : 0;
        case gm::kShowPing:        return menu_settings_.show_ping ? 1 : 0;
        case gm::kShowFps:         return menu_settings_.show_fps ? 1 : 0;
        case gm::kHudArt:          return menu_settings_.hud_art_new ? 0 : 1;
        case gm::kScroll:          return hud_.get_scroll_speed();
        case gm::kChat:            return hud_.is_chat_enabled() ? 1 : 0;
        case gm::kQuickHelpAtStart: return hud_.is_quick_help_enabled() ? 1 : 0;
        default:                   return -1;
    }
}

// A change from the panel: stored and applied as the HUD's own option setter does it (one state, one write). True when the row is the game's and the value was taken.
bool Application::game_menu_set(int id, int value, bool save) {
    namespace gm = game_menu;
    switch (id) {
        case gm::kMaster:
            menu_settings_.master = std::clamp(value, 0, gm::MenuSettings::kMasterMax);
            if (save) menu_settings_.write(config_store_, id);
            audio_mixer_.set_output_gain(menu_settings_.gain());
            return true;
        case gm::kMute:
            menu_settings_.mute = value != 0;
            menu_settings_.write(config_store_, id);
            audio_mixer_.set_output_gain(menu_settings_.gain());
            return true;
        case gm::kHealthChanges:
            if (value != 0 && value != 1) return false;                                  // (an On / Off word that the row does not have)
            menu_settings_.health_changes = value != 0;
            menu_settings_.write(config_store_, id);
            if (!menu_settings_.health_changes) health_changes_.reset();                 // (Off: nothing is kept, and what comes back starts from the hit points of that moment)
            return true;
        case gm::kShowPing:
            if (value != 0 && value != 1) return false;                                  // (neither On nor Off)
            menu_settings_.show_ping = value != 0;
            menu_settings_.write(config_store_, id);
            if (!menu_settings_.show_ping) {                                             // (Off: the ping text that opens the list of connections is gone, and so is the list)
                connections_open_ = false;
                connections_press_ = false;
            }
            return true;
        case gm::kShowFps:
            if (value != 0 && value != 1) return false;                                  // (neither On nor Off)
            menu_settings_.show_fps = value != 0;
            menu_settings_.write(config_store_, id);
            return true;
        case gm::kHudArt:
            if (value != 0 && value != 1) return false;                                  // (a word that the row does not have)
            menu_settings_.hud_art_new = value == 0;
            menu_settings_.write(config_store_, id);
            apply_hud_art();
            return true;
        case gm::kAntHealth:
        case gm::kHealthStyle: {
            const gm::Row* row = gm::find_row(id);
            if (value < row->min || value > row->max) return false;                    // (a word that the row does not have)
            health::Setting setting = hud_.ant_health();
            if (id == gm::kAntHealth) setting.when = static_cast<health::When>(value);
            else setting.style = static_cast<health::Style>(value);
            hud_.set_ant_health(setting);
            sync_game_menu_settings();
            return true;
        }
        case gm::kMusic:           hud_.set_option(OptionSetting::MusicVolume, value); return true;
        case gm::kEffects:         hud_.set_option(OptionSetting::SoundVolume, value); return true;
        case gm::kScroll:          hud_.set_option(OptionSetting::ScrollSpeed, value); return true;
        case gm::kChat:            hud_.set_option(OptionSetting::Chat, value); return true;
        case gm::kQuickHelpAtStart: hud_.set_option(OptionSetting::QuickHelp, value); return true;
        default:                   return false;
    }
}

// What the panel asks of the game: Leave game (the quit dialog's Yes), the original's quick help (F1) and, for the desktop's drawn panel, the window's fullscreen. Only on a match screen that no dialog covers.
bool Application::game_menu_do(int action) {
    namespace gm = game_menu;
    if (game_menu_where() < 0) return false;
    if (action == gm::kActLeave) {
        game_menu_open_ = false;
        game_menu_closing_ = false;
        confirm_quit();
        return true;
    }
    if (action == gm::kActFullscreen) {                              // (the window's, not a seat's: a recording has it too)
#if defined(__EMSCRIPTEN__)
        return false;                                                // (the page's own: its Fullscreen button and the browser's permission)
#else
        toggle_fullscreen();
        return true;
#endif
    }
    if (game_menu_needs_answer() || replay_mode_) return false;
    if (action != gm::kActQuickHelp) return false;
    hud_.open_quick_help();
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
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_set(int id, int value, int save) {
    return g_web_app != nullptr && g_web_app->game_menu_set(id, value, save != 0) ? 1 : 0;
}

// ... a Quick Chat text (game_menu::kQuickChat1 .. kQuickChat4): the code of its character `index` (-1 past the end) and, one character at a time, a new text: index 0 starts it, -1 keeps it; 1 when taken
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_text(int id, int index) {
    if (g_web_app == nullptr || index < 0) return -1;
    const std::string text = g_web_app->game_menu_text(id);
    return static_cast<size_t>(index) < text.size() ? static_cast<unsigned char>(text[static_cast<size_t>(index)]) : -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_text_set(int id, int index, int code) {
    return g_web_app != nullptr && g_web_app->game_menu_text_edit(id, index, code) ? 1 : 0;
}

// ... an act (game_menu::Action): Leave game, the quick help; 1 when the game did it
extern "C" EMSCRIPTEN_KEEPALIVE int ants_menu_do(int action) {
    return g_web_app != nullptr && g_web_app->game_menu_do(action) ? 1 : 0;
}

// ... and the panel is up (1) or gone (0): while it is up the game ignores its keys, text, clicks, wheel and fingers, and does not scroll at the edges
extern "C" EMSCRIPTEN_KEEPALIVE void ants_menu_open(int open) {
    if (g_web_app != nullptr) g_web_app->game_menu_set_open(open != 0);
}
#endif
