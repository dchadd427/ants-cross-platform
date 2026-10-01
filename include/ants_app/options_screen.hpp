#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "ants_app/config_store.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_app/screen_controls.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/// The settings of the options screen, in the order of the original's profile entries (string ids 0x17 - 0x1f)
enum class OptionSetting : uint8_t { SoundVolume, MusicVolume, ScrollSpeed, Chat, QuickHelp, QuickChat1, QuickChat2, QuickChat3, QuickChat4 };

/**
 * @brief What the options screen holds and the program remembers (docs 5.51).
 *
 * The defaults and the validity rules are the original's: each number is read with FUN_0102950f, which accepts a stored value only when it is a
 * non-negative integer in [0, 100) (the two switches: 0 only), otherwise the default applies; the texts are read with a maximum of 100 characters.
 */
struct OptionsState {
    static constexpr int32_t DEFAULT_SOUND_VOLUME = 100;
    static constexpr int32_t DEFAULT_MUSIC_VOLUME = 65;
    static constexpr int32_t DEFAULT_SCROLL_SPEED = 50;
    static constexpr size_t QUICK_CHAT_MAX = 100;

    int32_t sound_volume{DEFAULT_SOUND_VOLUME};       // "Sound Volume", 0 .. 100
    int32_t music_volume{DEFAULT_MUSIC_VOLUME};       // "Music Volume", 0 .. 100
    int32_t scroll_speed{DEFAULT_SCROLL_SPEED};       // "Scroll Speed", 0 .. 99 (the edge scroll's half extent is this + 10)
    bool chat{true};                                  // "Participate In Chat"
    bool quick_help{true};                            // "Show Quick Help at Startup"
    std::array<std::string, 4> quick_chat{};          // "Quick Chat F9" .. "Quick Chat F12" (defaults: strings 18 - 21)

    OptionsState();

    /// The profile names of the settings
    static const char* key(OptionSetting setting) noexcept;
    /// The program's start (FUN_0100a2c9, FUN_010106a9, FUN_0101487c): every setting is read from the store with the original's validity rule
    void load(const ConfigStore& store);
    /// A callback of the options screen writes its setting at once (FUN_0100c20c / FUN_0100bbf2)
    void write(ConfigStore& store, OptionSetting setting) const;
};

/**
 * @brief The options screen (Ants.exe constructor FUN_0101487c, window `op_screen`; docs 5.51).
 *
 * A window over the whole screen that takes every mouse and key event while it is open. Its controls, in the order of the original's list: the Return to Game
 * button, the Sound Volume, Music Volume and Scroll Speed sliders, the Participate In Chat pair and the Show Quick Help at Startup pair of latching ON / OFF
 * buttons, and the four quick-chat edit fields (F9 - F12; the first one has the focus when the screen opens). Nothing acts at a press except the start of a
 * slider drag and the focus of an edit field: the buttons, the pairs and Return run their callbacks at the release, a slider at the release of its drag, an
 * edit field at every character. Enter closes the screen, whatever has the focus; Esc does nothing; there is no click outside. Every setting is written to
 * the profile by the callback that changes it.
 */
class OptionsScreen {
public:
    // The layout (FUN_0101487c)
    static constexpr int32_t SLIDER_X = 188;
    static constexpr int32_t SLIDER_Y[3] = {179, 216, 253};                     // Sound Volume, Music Volume, Scroll Speed
    static constexpr int32_t EDIT_X[4] = {92, 92, 302, 302};                    // F9, F10, F11, F12
    static constexpr int32_t EDIT_Y[4] = {370, 402, 370, 402};
    static constexpr int32_t EDIT_W = 141;
    static constexpr int32_t EDIT_H = 15;
    // The rectangles of the buttons: breturn1 / breturn2 and the pressed breturn3 (the hit test is the rectangle of the picture that shows); the latching pairs keep the union of their pictures
    static constexpr int32_t RETURN_X = 351, RETURN_Y = 425, RETURN_W = 98, RETURN_H = 26;
    static constexpr ButtonRect RETURN_PRESSED{353, 427, 97, 24};
    static constexpr int32_t CHAT_ON_X = 102, CHAT_OFF_X = 151, HELP_ON_X = 355, HELP_OFF_X = 404, TOGGLE_Y = 289, TOGGLE_W = 49, TOGGLE_H = 24;

    OptionsScreen();

    OptionsState& state() noexcept { return state_; }
    const OptionsState& state() const noexcept { return state_; }

    /// The screen is created (the constructor reads the settings): the sliders and pairs show them, the F9 field has the focus
    void open(uint32_t now_ms);
    void close() noexcept { open_ = false; }
    bool is_open() const noexcept { return open_; }

    /// The pointer moved, or the 50 ms poll ran (FUN_01011281 of every control): hover pictures, a slider drag
    void on_move(int32_t x, int32_t y);
    /// A left press: the start of a drag, a captured button, the focus of an edit field
    void on_press(int32_t x, int32_t y, uint32_t now_ms);
    /// A left release: the callbacks
    void on_release(int32_t x, int32_t y);
    /// A key of the original (docs 5.45): Enter closes the screen, the others go to the field that has the focus
    void on_key(int32_t key_id);
    /// Typed text (SDL's text input): every printable ASCII character is a key of the original
    void on_text(const std::string& text);

    /// Called with the setting that a callback has just changed (the owner applies and stores it)
    void set_on_change(std::function<void(OptionSetting)> cb) { on_change_ = std::move(cb); }

    /// Draws the window composite and its controls; `now_ms` drives the caret
    void render(IRenderer& renderer, const assets::AssetArchive& assets, uint32_t now_ms);

    // The controls (read-only, for the tests and the drawing)
    const ScreenButton& return_button() const noexcept { return return_; }
    const ScreenSlider& slider(size_t index) const { return sliders_[index]; }
    const ScreenToggle& chat_on() const noexcept { return chat_on_; }
    const ScreenToggle& chat_off() const noexcept { return chat_off_; }
    const ScreenToggle& help_on() const noexcept { return help_on_; }
    const ScreenToggle& help_off() const noexcept { return help_off_; }
    const ScreenEdit& edit(size_t index) const { return edits_[index]; }

private:
    void notify(OptionSetting setting) { if (on_change_) on_change_(setting); }
    void choose_chat(bool on);
    void choose_quick_help(bool on);
    void draw_edit(IRenderer& renderer, const ScreenEdit& edit, uint32_t now_ms) const;

    OptionsState state_{};
    bool open_{false};
    ScreenButton return_{ButtonRect{RETURN_X, RETURN_Y, RETURN_W, RETURN_H}, RETURN_PRESSED};
    std::array<ScreenSlider, 3> sliders_{};
    ScreenToggle chat_on_{CHAT_ON_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H};
    ScreenToggle chat_off_{CHAT_OFF_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H};
    ScreenToggle help_on_{HELP_ON_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H};
    ScreenToggle help_off_{HELP_OFF_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H};
    std::array<ScreenEdit, 4> edits_{};
    std::function<void(OptionSetting)> on_change_;
};

}  // namespace ants::app
