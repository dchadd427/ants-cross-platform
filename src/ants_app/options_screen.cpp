#include "ants_app/options_screen.hpp"

#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/window_art.hpp"
#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {

// The text of the edit fields: color 0xdfe7ef (a COLORREF: R 239, G 231, B 223), 12 px letters (the label default FUN_0102ad16: nothing sets another height)
const assets::ColorRGBA kEditColor{239, 231, 223, 255};
constexpr FontSize kEditFont = FontSize::Px12;

// The pictures of a latching button: up, hover, down, down + hover (the animations 9 - 0xc of the constructor's argument lists, by name)
struct ToggleArt {
    const char* up;
    const char* hover;
    const char* down;
    const char* down_hover;
};
constexpr ToggleArt kChatOnArt{"op_conu", "op_conr", "op_cond", "op_condr"};
constexpr ToggleArt kChatOffArt{"op_coffu", "op_coffr", "op_coffd", "op_coffdr"};
constexpr ToggleArt kHelpOnArt{"op_honu", "op_honr", "op_hond", "optondr"};
constexpr ToggleArt kHelpOffArt{"op_hoffu", "op_hoffr", "op_hoffd", "op_hoffdr"};

const char* art_name(const ToggleArt& art, ScreenToggle::Art state) {
    switch (state) {
        case ScreenToggle::Art::Up:        return art.up;
        case ScreenToggle::Art::Hover:     return art.hover;
        case ScreenToggle::Art::Down:      return art.down;
        case ScreenToggle::Art::DownHover: return art.down_hover;
    }
    return art.up;
}

size_t quick_chat_index(OptionSetting setting) { return static_cast<size_t>(setting) - static_cast<size_t>(OptionSetting::QuickChat1); }

}  // anonymous namespace

OptionsState::OptionsState() {
    quick_chat = {sim::strings::text(sim::strings::kQuickChat1), sim::strings::text(sim::strings::kQuickChat2), sim::strings::text(sim::strings::kQuickChat3),
                  sim::strings::text(sim::strings::kQuickChat4)};
}

const char* OptionsState::key(OptionSetting setting) noexcept {
    switch (setting) {
        case OptionSetting::SoundVolume: return "Sound Volume";
        case OptionSetting::MusicVolume: return "Music Volume";
        case OptionSetting::ScrollSpeed: return "Scroll Speed";
        case OptionSetting::Chat:        return "Participate In Chat";
        case OptionSetting::QuickHelp:   return "Show Quick Help at Startup";
        case OptionSetting::QuickChat1:  return "Quick Chat F9";
        case OptionSetting::QuickChat2:  return "Quick Chat F10";
        case OptionSetting::QuickChat3:  return "Quick Chat F11";
        case OptionSetting::QuickChat4:  return "Quick Chat F12";
    }
    return "";
}

void OptionsState::load(const ConfigStore& store) {
    const OptionsState defaults;
    sound_volume = store.get_int(key(OptionSetting::SoundVolume), DEFAULT_SOUND_VOLUME, 0, 100);
    music_volume = store.get_int(key(OptionSetting::MusicVolume), DEFAULT_MUSIC_VOLUME, 0, 100);
    scroll_speed = store.get_int(key(OptionSetting::ScrollSpeed), DEFAULT_SCROLL_SPEED, 0, 100);
    // The two switches are read with the range [0, 1): only a stored 0 is valid, everything else (a stored 1 included) is the default, which is 1 (on)
    chat = store.get_int(key(OptionSetting::Chat), 1, 0, 1) != 0;
    quick_help = store.get_int(key(OptionSetting::QuickHelp), 1, 0, 1) != 0;
    for (size_t i = 0; i < quick_chat.size(); ++i) {
        quick_chat[i] = store.get_string(key(static_cast<OptionSetting>(static_cast<size_t>(OptionSetting::QuickChat1) + i)), defaults.quick_chat[i], QUICK_CHAT_MAX);
    }
}

void OptionsState::write(ConfigStore& store, OptionSetting setting) const {
    switch (setting) {
        case OptionSetting::SoundVolume: store.set_int(key(setting), sound_volume); break;
        case OptionSetting::MusicVolume: store.set_int(key(setting), music_volume); break;
        case OptionSetting::ScrollSpeed: store.set_int(key(setting), scroll_speed); break;
        case OptionSetting::Chat:        store.set_int(key(setting), chat ? 1 : 0); break;
        case OptionSetting::QuickHelp:   store.set_int(key(setting), quick_help ? 1 : 0); break;
        case OptionSetting::QuickChat1:
        case OptionSetting::QuickChat2:
        case OptionSetting::QuickChat3:
        case OptionSetting::QuickChat4:  store.set_string(key(setting), quick_chat[quick_chat_index(setting)]); break;
    }
}

OptionsScreen::OptionsScreen() = default;

// FUN_0101487c: the window reads the settings and builds its controls; the first edit field gets the focus
void OptionsScreen::open(uint32_t now_ms) {
    open_ = true;
    return_.reset();
    sliders_[0] = ScreenSlider(SLIDER_X, SLIDER_Y[0], state_.sound_volume);
    sliders_[1] = ScreenSlider(SLIDER_X, SLIDER_Y[1], state_.music_volume);
    sliders_[2] = ScreenSlider(SLIDER_X, SLIDER_Y[2], state_.scroll_speed);
    chat_on_ = ScreenToggle(CHAT_ON_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H);
    chat_off_ = ScreenToggle(CHAT_OFF_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H);
    help_on_ = ScreenToggle(HELP_ON_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H);
    help_off_ = ScreenToggle(HELP_OFF_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H);
    (state_.chat ? chat_on_ : chat_off_).set_latched(true);
    (state_.quick_help ? help_on_ : help_off_).set_latched(true);
    for (size_t i = 0; i < edits_.size(); ++i) edits_[i] = ScreenEdit(EDIT_X[i], EDIT_Y[i], EDIT_W, EDIT_H, state_.quick_chat[i], OptionsState::QUICK_CHAT_MAX);
    edits_[0].focus(true, now_ms);
}

// The window's dispatch gives every control the pointer first (the move handlers), then the event, in the order of the list
void OptionsScreen::on_move(int32_t x, int32_t y) {
    if (!open_) return;
    pointer_x_ = x;
    pointer_y_ = y;
    return_.on_move(x, y);
    for (ScreenSlider& s : sliders_) s.on_move(x, y);
    chat_on_.on_move(x, y);
    chat_off_.on_move(x, y);
    help_on_.on_move(x, y);
    help_off_.on_move(x, y);
}

void OptionsScreen::on_press(int32_t x, int32_t y, uint32_t now_ms) {
    if (!open_) return;
    on_move(x, y);
    return_.on_press(x, y);
    for (ScreenSlider& s : sliders_) s.on_press(x, y);
    chat_on_.on_press(x, y);
    chat_off_.on_press(x, y);
    help_on_.on_press(x, y);
    help_off_.on_press(x, y);
    for (ScreenEdit& e : edits_) e.on_press(x, y, now_ms);
}

// FUN_01014f5a / FUN_01014fa0: the Participate In Chat pair (and FUN_01014fe6 / FUN_0101501f for Show Quick Help at Startup): both latches are set, the
// profile is written, and the chat is switched on or off at once
void OptionsScreen::choose_chat(bool on) {
    chat_on_.set_latched(on);
    chat_off_.set_latched(!on);
    state_.chat = on;
    notify(OptionSetting::Chat);
}

void OptionsScreen::choose_quick_help(bool on) {
    help_on_.set_latched(on);
    help_off_.set_latched(!on);
    state_.quick_help = on;
    notify(OptionSetting::QuickHelp);
}

void OptionsScreen::cancel_press() noexcept {
    if (!open_) return;
    return_.reset();
    const int32_t values[3] = {state_.sound_volume, state_.music_volume, state_.scroll_speed};
    for (size_t i = 0; i < sliders_.size(); ++i) {
        if (sliders_[i].dragging()) sliders_[i] = ScreenSlider(SLIDER_X, SLIDER_Y[i], values[i]);        // (the thumb where the window put it when it opened: the value in effect)
    }
    for (ScreenToggle* toggle : {&chat_on_, &chat_off_, &help_on_, &help_off_}) toggle->cancel_press();
}

void OptionsScreen::on_release(int32_t x, int32_t y) {
    if (!open_) return;
    on_move(x, y);
    if (return_.on_release(x, y)) {                       // callback FUN_01016de9: the window closes
        close();
        return;
    }
    static constexpr OptionSetting kSliderSetting[3] = {OptionSetting::SoundVolume, OptionSetting::MusicVolume, OptionSetting::ScrollSpeed};
    for (size_t i = 0; i < sliders_.size(); ++i) {
        if (!sliders_[i].on_release()) continue;
        // FUN_01015058 / FUN_01015092 / FUN_010150bc: the value (what the thumb stands for) is written and applied
        int32_t& target = i == 0 ? state_.sound_volume : (i == 1 ? state_.music_volume : state_.scroll_speed);
        target = sliders_[i].value();
        notify(kSliderSetting[i]);
    }
    if (chat_on_.on_release(x, y)) choose_chat(true);
    if (chat_off_.on_release(x, y)) choose_chat(false);
    if (help_on_.on_release(x, y)) choose_quick_help(true);
    if (help_off_.on_release(x, y)) choose_quick_help(false);
}

// FUN_01014f12: Enter (0x18) closes the window; every other key is offered to all the controls, and only the edit field with the focus takes it
void OptionsScreen::on_key(int32_t key_id) {
    if (!open_) return;
    if (key_id == ScreenEdit::KEY_ENTER) {
        close();
        return;
    }
    for (size_t i = 0; i < edits_.size(); ++i) {
        if (!edits_[i].on_char(key_id)) continue;
        state_.quick_chat[i] = edits_[i].text();           // FUN_010150e6 .. FUN_01015122 -> FUN_0100bbf2(index, text): at most 100 characters, written to the profile
        notify(static_cast<OptionSetting>(static_cast<size_t>(OptionSetting::QuickChat1) + i));
    }
}

void OptionsScreen::on_text(const std::string& text) {
    for (char c : text) {
        if (c >= 0x20 && c <= 0x7e) on_key(c);
    }
}

void OptionsScreen::draw_edit(IRenderer& renderer, const ScreenEdit& edit, uint32_t now_ms) const {
    draw_edit_line(renderer, edit.text(), edit.x(), edit.y(), edit.w(), edit.tail_aligned(), edit.caret_visible(now_ms), kEditColor, kEditFont);
}

// The window in the new look: the set's panel and lettering where the original's composite is, and the set's pictures for the controls, at the places and with the states of the original's
// (the original's own pictures are the fallback of the caller: it passes the art only when the set has every piece, window_art::check). The latching pairs show a chosen switch sunk and lit, and
// one that is only held sunk and plain; the thumb of a slider has a hover picture and a held one; the field with the focus has a thin gold line round its ground.
void OptionsScreen::render_new(IRenderer& renderer, const hud_art::Art& art, uint32_t now_ms) {
    const ScreenLayout own = ScreenLayout::classic();                       // (the pieces are in the window's own numbers: no anchor moves them)
    hud_art::draw_all(renderer, art, hud_art::panel_draws(art, window_art::kOptionsRect));
    hud_art::draw_animation(renderer, art, "op_window", 0, own);
    const auto control = [&](const char* name) { hud_art::draw_animation(renderer, art, name, 0, own); };
    control(return_.pressed() ? "breturn3" : (return_.hovered() ? "breturn2" : "breturn1"));
    for (size_t i = 0; i < sliders_.size(); ++i) {
        const ScreenSlider& s = sliders_[i];
        const bool over = pointer_x_ >= s.thumb_left() && pointer_x_ < s.thumb_left() + ScreenSlider::THUMB_W + 3 && pointer_y_ >= s.thumb_top() && pointer_y_ < s.thumb_top() + 16;
        const int32_t sprite = art.find_sprite(s.dragging() ? "op_thumb_held" : (over ? "op_thumb_hover" : "op_thumb_up"));
        if (const hud_art::Sprite* sp = art.sprite_at(sprite)) hud_art::draw_all(renderer, art, {hud_art::Draw{sprite, LayoutRect{s.thumb_left(), s.thumb_top(), sp->w, sp->h}, LayoutRect{0, 0, sp->w, sp->h}}});
    }
    struct Switch {
        const ScreenToggle& toggle;
        const ToggleArt& names;
        const char* held;
    };
    for (const Switch& sw : {Switch{chat_on_, kChatOnArt, "op_conh"}, Switch{chat_off_, kChatOffArt, "op_coffh"}, Switch{help_on_, kHelpOnArt, "op_honh"}, Switch{help_off_, kHelpOffArt, "op_hoffh"}}) {
        const ScreenToggle::Art state = sw.toggle.art();
        const bool held = !sw.toggle.latched() && (state == ScreenToggle::Art::Down || state == ScreenToggle::Art::DownHover);     // pressed, not yet chosen
        control(held ? sw.held : art_name(sw.names, state));
    }
    static constexpr int32_t kGround[4][2] = {{89, 370}, {89, 401}, {298, 370}, {298, 401}};                    // the dark ground of each field (144 x 15), where the original's picture of it stands
    for (size_t i = 0; i < edits_.size(); ++i) {
        draw_edit(renderer, edits_[i], now_ms);
        if (edits_[i].focused()) renderer.draw_rect(kGround[i][0], kGround[i][1], 144, 15, assets::ColorRGBA{243, 219, 55, 255});
    }
}

void OptionsScreen::render(IRenderer& renderer, const assets::AssetArchive& assets, uint32_t now_ms, const hud_art::Art* art) {
    if (!open_) return;
    if (art != nullptr) {
        render_new(renderer, *art, now_ms);
        return;
    }

    // The window's picture: the op_screen composite (210 parts; it carries its own dither around the card)
    const auto* anim = assets.find_animation("op_screen");
    if (anim && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) renderer.draw_sprite(frames[i].sprite_index, frames[i].dx, frames[i].dy);
    } else {
        renderer.draw_named_sprite("optcap1.bmp", 44, 39);
    }

    // The controls, in the order of the original's list
    draw_animation_frame0(renderer, assets, return_.pressed() ? "breturn3" : (return_.hovered() ? "breturn2" : "breturn1"));
    for (const ScreenSlider& s : sliders_) renderer.draw_named_sprite("slidd.bmp", s.thumb_left(), s.thumb_top());
    draw_animation_frame0(renderer, assets, art_name(kChatOnArt, chat_on_.art()));
    draw_animation_frame0(renderer, assets, art_name(kChatOffArt, chat_off_.art()));
    draw_animation_frame0(renderer, assets, art_name(kHelpOnArt, help_on_.art()));
    draw_animation_frame0(renderer, assets, art_name(kHelpOffArt, help_off_.art()));
    for (const ScreenEdit& e : edits_) draw_edit(renderer, e, now_ms);
}

}  // namespace ants::app
