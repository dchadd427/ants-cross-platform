#pragma once

#include <cstdint>
#include <string>

#include "ants_app/health_marks.hpp"

namespace ants::app {
class ConfigStore;
}

namespace ants::app::game_menu {

// The settings menu that Esc opens in a match (docs/GAME_MENU.md): the model, with no window and no SDL, so that a test can run it. It is the ONE list of rows (what a row is called,
// what it says, its range and default, which screens show it) that every skin of the menu draws: the page's panel (web/game_menu.js) gets it as one JSON text from describe_json(), and a skin of the
// desktop app can read the same table. The values live where they always did (the original's options window, the zoom, the picture's shape, the fullscreen of the page): a row names its value, the
// skin shows it and asks the game to change it. Four values are new: Master volume, Mute and the two of the ant health marks, kept in MenuSettings.

enum RowId : int {
    kMaster = 1,            // Master volume: multiplies every sound and the music; the original's Sound Volume and Music Volume stay as they are
    kMusic = 2,             // the original's Music Volume
    kEffects = 3,           // the original's Sound Volume
    kMute = 4,              // silences everything and keeps every slider where it is
    kFullscreen = 5,        // the page's own: the skin does it
    kPicture = 6,           // the picture's shape: the page's selector does it
    kLockMouse = 7,         // the browser's fullscreen mouse (Locked / Free): the page does it
    kAntHealth = 8,         // which ants wear a health mark (Hurt ants, All ants, Off: health::When; Ctrl+L flips Off and the last other choice), remembered
    kScroll = 9,            // the original's Scroll Speed
    kChat = 10,             // the original's Participate In Chat
    kQuickHelpAtStart = 11, // the original's Show Quick Help at Startup
    kQuickHelp = 12,        // opens the original's quick help (F1)
    kOptionsWindow = 13,    // opens the original's options window (Ctrl+O)
    kVersion = 14,          // the version of the game (the skin knows it)
    kHealthStyle = 15,      // what the mark looks like (Bar, Number: health::Style), remembered
};

// What the skin may ask of the game besides reading and changing a row (ants_menu_do)
enum Action : int { kActLeave = 1, kActQuickHelp = 2, kActOptionsWindow = 3 };

// What the skin may read besides a row (ants_menu_get): 100 where the menu stands (-1: not now, 0: a match, 1: a recording or a live match that is watched), 101 whether something needs an answer
// first (a dialog of the match, a vote, a lost link, the results: the menu closes), 102 what Leave game would do (LeaveKind)
enum Fact : int { kFactWhere = 100, kFactNeedsAnswer = 101, kFactLeave = 102 };

enum class Section : uint8_t { Sound, Video, Controls, Game };
enum class Kind : uint8_t { Slider, Switch, Choice, Button, Info };

enum Show : uint8_t {
    kShowWatch = 1,         // also in a recording or a live match that is watched (no seat, no sides: the Game section stays out)
    kShowWeb = 2,           // the browser build
    kShowDesktop = 4,       // the desktop build
    kShowLive = 8,          // a slider that acts while it is dragged (the others act at the release, as the original's do)
    kShowNew = 16,          // a row that did not exist before the menu: the skin tags it
    kShowPage = 32,         // its value is the page's: the page's own function does it, the game is not asked
};

struct Row {
    int id;
    const char* key;            // the name in the settings file and in the JSON (lower case)
    Section section;
    Kind kind;
    int min;                    // sliders
    int max;
    int def;
    uint8_t show;               // Show
    const char* label;
    const char* hint;           // the second line ("" for none)
    const char* words;          // a choice's words, and a switch's two (On, Off), separated by '|'; a button's own text
};

/// The rows in the order of the menu
const Row* rows(size_t& count) noexcept;
const Row* find_row(int id) noexcept;

/// What the skin of a screen is shown: a match (watching = false) or a recording / live match that is watched. `web` is the build.
/// One JSON text: {"v":1,"sections":[{"id","label"}],"rows":[{"id","key","sec","kind","label","hint","min","max","def","words":[...],"live","new","page"}], "words":{...}} (every text is escaped; no markup)
std::string describe_json(bool watching, bool web);

/// What Leave game does (the rules of the original's quit question, Application::confirm_quit)
enum class LeaveKind : int {
    EndsMatch = 0,      // a game on this computer against several sides, or alone: the match ends and is not saved
    DropOut = 1,        // a network match with two or more other sides: the player drops out, the others play on
    Forfeit = 2,        // exactly one other side is left: the player gives the match up, it ends for everybody
    Held = 3,           // the match is held (a seat is missing, the countdown, a lost link): the player leaves for good (as the quit dialog's Yes does), and is asked first like for any leave
    Watch = 4,          // a recording or a live match that is watched: back to the list, nothing is lost and nothing is asked
};
LeaveKind leave_kind(bool watching, bool held, bool networked, uint32_t other_sides, bool match_running) noexcept;
bool leave_needs_confirm(LeaveKind kind) noexcept;
/// The match is held (a seat is missing, the countdown runs, this machine is on its way back): the one rule of the quit dialog's Yes and of Leave game
bool match_is_held(bool networked, bool paused, bool catch_up, bool start_modal) noexcept;
/// A dialog of the match, the catch-up screen, or (in a network match) a vote or a lost link needs its answer first: the panel does not open over it and closes when one comes up
bool needs_answer(bool modal_open, bool catch_up, bool networked, bool vote_open, bool reconnecting) noexcept;

/// The new settings, remembered under their own names in the settings file (the original's names stay as they are)
struct MenuSettings {
    static constexpr int kMasterMax = 100;
    int32_t master{100};        // 0 .. 100, 100 is every sound and the music as they are (the sliders of the menu show 99 at their end, as the original's do)
    bool mute{false};
    health::Setting ant_health{};   // which ants wear a mark and how: hurt ants, a bar, for everybody until they choose

    static const char* key(int row_id) noexcept;                        // "master", "mute", "ant_health", "ant_health_style" ("" for any other row)
    /// An old `hp_numbers=off` (the numbers that this setting replaces) is read as Off when `ant_health` was never written; every other old value, and none, is the default (hurt ants, a bar)
    void load(const ConfigStore& store);
    void write(ConfigStore& store, int row_id) const;                   // writes one of the four
    /// The gain that master and mute give the whole mix: 0 when muted, else master (99 and 100 are the full volume) over 99
    float gain() const noexcept;
};

}  // namespace ants::app::game_menu
