#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "ants_app/config_store.hpp"

namespace ants::app {

/// The original's options that the game keeps (the entries of its profile, string ids 0x17 - 0x1f): the settings menu changes them (game_menu.hpp), the HUD and the audio read them. The original's options
/// window that used to hold them is gone: its rows are in the settings menu, the four quick chat texts in the menu's Quick Chat section.
enum class OptionSetting : uint8_t { SoundVolume, MusicVolume, ScrollSpeed, Chat, QuickHelp, QuickChat1, QuickChat2, QuickChat3, QuickChat4 };

/**
 * @brief What the program remembers of the original's options (docs 5.51).
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
    /// A change of a setting is written to the profile at once (FUN_0100c20c / FUN_0100bbf2)
    void write(ConfigStore& store, OptionSetting setting) const;
    /// A quick chat text as the settings menu may store it: printable ASCII (0x20 - 0x7e), at most QUICK_CHAT_MAX characters, the rest left out (the original's edit field takes no other)
    static std::string clean_quick_chat(const std::string& text);
};

}  // namespace ants::app
