#include "ants_app/options_state.hpp"

#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {
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

std::string OptionsState::clean_quick_chat(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c >= 0x20 && c <= 0x7e && out.size() < QUICK_CHAT_MAX) out += c;
    }
    return out;
}

}  // namespace ants::app
