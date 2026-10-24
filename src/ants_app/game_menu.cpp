#include "ants_app/game_menu.hpp"

#include <algorithm>
#include <array>

#include "ants_app/config_store.hpp"

namespace ants::app::game_menu {

namespace {

constexpr uint8_t kBoth = kShowWeb | kShowDesktop;

// The table. Texts are plain ASCII with no markup (a skin puts them in with its text calls). The original's own names stay in the hints, so that a player who knows the options window finds them.
constexpr std::array<Row, 14> kRows = {{
    {kMaster, "master", Section::Sound, Kind::Slider, 0, 99, 100, kBoth | kShowWatch | kShowLive | kShowNew, "Master volume", "Everything you hear", ""},
    {kMusic, "music", Section::Sound, Kind::Slider, 0, 99, 65, kBoth | kShowWatch, "Music volume", "The original's \"Music Volume\"", ""},
    {kEffects, "effects", Section::Sound, Kind::Slider, 0, 99, 100, kBoth | kShowWatch, "Sound effects volume", "The original's \"Sound Volume\"", ""},
    {kMute, "mute", Section::Sound, Kind::Switch, 0, 1, 0, kBoth | kShowWatch | kShowNew, "Mute everything", "One switch; your volumes stay as they are", "On|Off"},
    {kFullscreen, "fullscreen", Section::Video, Kind::Button, 0, 0, 0, kBoth | kShowWatch | kShowPage, "Fullscreen", "Same as the Fullscreen button; Alt+Enter in the desktop app", "Enter fullscreen|Leave fullscreen"},
    {kPicture, "picture", Section::Video, Kind::Choice, 0, 3, 2, kShowWeb | kShowWatch | kShowPage, "Picture shape", "Classic 4:3, 16:10, 16:9 or 21:9; the match goes on", "Classic 4:3|16:10|16:9|21:9"},
    {kLockMouse, "lockmouse", Section::Video, Kind::Choice, 0, 1, 0, kShowWeb | kShowWatch | kShowPage, "Fullscreen mouse", "Browser only; holds the mouse inside the picture", "Locked|Free"},
    {kHpNumbers, "hp_numbers", Section::Video, Kind::Switch, 0, 1, 1, kBoth | kShowWatch | kShowNew, "Hit point numbers", "White numbers over every ant (Ctrl+L); remembered", "On|Off"},
    {kScroll, "scroll", Section::Video, Kind::Slider, 0, 99, 50, kBoth | kShowWatch, "Map scroll speed", "Edge scrolling; the original's \"Scroll Speed\"", ""},
    {kChat, "chat", Section::Game, Kind::Switch, 0, 1, 1, kBoth, "Participate in chat", "The original's option", "On|Off"},
    {kQuickHelpAtStart, "quickhelp_start", Section::Game, Kind::Switch, 0, 1, 1, kBoth, "Show quick help at startup", "The original's option", "On|Off"},
    {kQuickHelp, "quickhelp", Section::Game, Kind::Button, 0, 0, 0, kBoth, "Quick help", "", "Open (F1)"},
    {kOptionsWindow, "options_window", Section::Game, Kind::Button, 0, 0, 0, kBoth, "Original options window", "Also where the four quick chat texts are edited", "Open (Ctrl+O)"},
    {kVersion, "version", Section::Game, Kind::Info, 0, 0, 0, kBoth, "Version", "", ""},
}};

struct SectionName {
    Section section;
    const char* id;
    const char* label;
    bool in_watch;
};
constexpr std::array<SectionName, 4> kSections = {{
    {Section::Sound, "sound", "Sound", true},
    {Section::Video, "video", "Video", true},
    {Section::Controls, "controls", "Controls", true},
    {Section::Game, "game", "Game", false},
}};

const char* kind_name(Kind kind) noexcept {
    switch (kind) {
        case Kind::Slider: return "slider";
        case Kind::Switch: return "switch";
        case Kind::Choice: return "choice";
        case Kind::Button: return "button";
        case Kind::Info:   return "info";
    }
    return "info";
}

void put_string(std::string& out, const char* text) {
    out += '"';
    for (const char* p = text; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) out += ' ';
        else out += static_cast<char>(c);
    }
    out += '"';
}

void put_words(std::string& out, const char* words) {
    out += '[';
    bool first = true;
    for (const char* p = words; *p != '\0';) {
        const char* end = p;
        while (*end != '\0' && *end != '|') ++end;
        if (!first) out += ',';
        first = false;
        put_string(out, std::string(p, end).c_str());
        p = *end == '|' ? end + 1 : end;
    }
    out += ']';
}

bool shown(const Row& row, bool watching, bool web) noexcept {
    if (watching && (row.show & kShowWatch) == 0) return false;
    return (row.show & (web ? kShowWeb : kShowDesktop)) != 0;
}

bool section_shown(Section section, bool watching) noexcept {
    for (const SectionName& s : kSections) {
        if (s.section == section) return !watching || s.in_watch;
    }
    return false;
}

}  // namespace

const Row* rows(size_t& count) noexcept {
    count = kRows.size();
    return kRows.data();
}

const Row* find_row(int id) noexcept {
    for (const Row& row : kRows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

std::string describe_json(bool watching, bool web) {
    std::string out = "{\"v\":1,\"sections\":[";
    bool first = true;
    for (const SectionName& s : kSections) {
        if (!section_shown(s.section, watching)) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"id\":";
        put_string(out, s.id);
        out += ",\"label\":";
        put_string(out, s.label);
        out += '}';
    }
    out += "],\"rows\":[";
    first = true;
    for (const Row& row : kRows) {
        if (!shown(row, watching, web) || !section_shown(row.section, watching)) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"id\":" + std::to_string(row.id) + ",\"key\":";
        put_string(out, row.key);
        out += ",\"sec\":";
        for (const SectionName& s : kSections) {
            if (s.section == row.section) put_string(out, s.id);
        }
        out += ",\"kind\":";
        put_string(out, kind_name(row.kind));
        out += ",\"label\":";
        put_string(out, row.label);
        out += ",\"hint\":";
        put_string(out, row.hint);
        out += ",\"min\":" + std::to_string(row.min) + ",\"max\":" + std::to_string(row.max) + ",\"def\":" + std::to_string(row.def) + ",\"words\":";
        put_words(out, row.words);
        out += std::string(",\"live\":") + ((row.show & kShowLive) != 0 ? "1" : "0") + ",\"new\":" + ((row.show & kShowNew) != 0 ? "1" : "0") + ",\"page\":" + ((row.show & kShowPage) != 0 ? "1" : "0") + '}';
    }
    // The words that are not a row's: the footer's notes, the tags of a screen that is watched, Resume and Leave, and the question that Leave asks (one entry for each LeaveKind: "asks" says whether it asks, and the words are only there when it does)
    out += "],\"words\":{\"title\":\"Settings\",\"resume\":\"Resume\",\"leave\":\"Leave game\",\"back\":\"Back to the matches\",\"close\":\"Close the settings\",\"esc\":\"closes\","
           "\"note\":\"The game keeps running while this menu is open.\","
           "\"noteReplay\":\"The replay keeps playing. Pause it with the bar.\",\"noteLive\":\"The match keeps playing.\","
           "\"tagReplay\":\"Watching a replay\",\"tagLive\":\"Watching live\",\"stay\":\"Stay in the game\",\"deciding\":\"The game keeps running while you decide.\","
           "\"ask\":[";
    constexpr std::array<LeaveKind, 5> kKinds = {LeaveKind::EndsMatch, LeaveKind::DropOut, LeaveKind::Forfeit, LeaveKind::Held, LeaveKind::Watch};
    first = true;
    for (const LeaveKind kind : kKinds) {
        if (!first) out += ',';
        first = false;
        out += "{\"kind\":" + std::to_string(static_cast<int>(kind)) + ",\"asks\":" + (leave_needs_confirm(kind) ? "1" : "0");     // (a kind that asks nothing has no words)
        if (!leave_needs_confirm(kind)) {
            out += '}';
            continue;
        }
        out += ",\"title\":";
        put_string(out, kind == LeaveKind::Forfeit ? "Forfeit and leave?" : "Leave the game?");
        out += ",\"body\":";
        put_string(out, kind == LeaveKind::Forfeit ? "You give up this match. It ends now for everybody and counts as a loss for you."
                        : kind == LeaveKind::DropOut ? "You will drop out. Your ants are lost, the others play on, and you cannot come back to this match."
                        : kind == LeaveKind::Held ? "The match is held. You leave it for good and cannot come back to this match."
                                                  : "This match ends. Your game is not saved.");
        out += ",\"go\":";
        put_string(out, kind == LeaveKind::Forfeit ? "Forfeit" : "Leave game");
        out += '}';
    }
    out += "]}}";
    return out;
}

LeaveKind leave_kind(bool watching, bool held, bool networked, uint32_t other_sides, bool match_running) noexcept {
    if (watching) return LeaveKind::Watch;
    if (held) return LeaveKind::Held;
    if (match_running && other_sides == 1) return LeaveKind::Forfeit;
    if (networked && other_sides >= 2) return LeaveKind::DropOut;
    return LeaveKind::EndsMatch;
}

bool match_is_held(bool networked, bool paused, bool catch_up, bool start_modal) noexcept {
    return networked && (paused || catch_up || start_modal);
}

bool needs_answer(bool modal_open, bool catch_up, bool networked, bool vote_open, bool reconnecting) noexcept {
    return modal_open || catch_up || (networked && (vote_open || reconnecting));
}

bool leave_needs_confirm(LeaveKind kind) noexcept {
    return kind != LeaveKind::Watch;
}

const char* MenuSettings::key(int row_id) noexcept {
    switch (row_id) {
        case kMaster:    return "master";
        case kMute:      return "mute";
        case kHpNumbers: return "hp_numbers";
        default:         return "";
    }
}

void MenuSettings::load(const ConfigStore& store) {
    master = store.get_int(key(kMaster), 100, 0, kMasterMax + 1);                         // (0 .. 100: the original's reader takes [min, max), so max is one more)
    mute = store.get_string(key(kMute), "off", 8) == "on";
    hp_numbers = store.get_string(key(kHpNumbers), "on", 8) != "off";
}

void MenuSettings::write(ConfigStore& store, int row_id) const {
    switch (row_id) {
        case kMaster:    store.set_int(key(kMaster), master); break;
        case kMute:      store.set_string(key(kMute), mute ? "on" : "off"); break;
        case kHpNumbers: store.set_string(key(kHpNumbers), hp_numbers ? "on" : "off"); break;
        default: break;
    }
}

float MenuSettings::gain() const noexcept {
    if (mute) return 0.0f;
    return static_cast<float>(std::clamp<int32_t>(master, 0, 99)) / 99.0f;
}

}  // namespace ants::app::game_menu
