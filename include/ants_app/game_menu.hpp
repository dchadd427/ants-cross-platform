#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/health_marks.hpp"

namespace ants::app {
class ConfigStore;
}

namespace ants::app::game_menu {

// The settings menu that Esc, Ctrl+O and the Options button open in a match (docs/GAME_MENU.md): the model, with no window and no SDL, so that a test can run it. It is the ONE list of rows (what a row is called,
// what it says, its range and default, which screens show it) that every skin of the menu draws: the page's panel (web/game_menu.js) gets it as one JSON text from describe_json(), and the desktop game's
// drawn panel (game_menu_panel.hpp) reads the same table. The values live where they always did (the original's sound, scroll and chat options, the four quick chat texts, the zoom, the picture's shape, the
// fullscreen of the page): a row names its value, the skin shows it and asks the game to change it. Nine values are new: Master volume, Mute, the two of the ant health marks, the damage and heal numbers,
// Show FPS, Show ping and the HUD art, kept in MenuSettings.

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
    kVersion = 14,          // the version of the game (the skin knows it)
    kHealthStyle = 15,      // what the mark looks like (Bar, Number: health::Style), remembered
    kHealthChanges = 16,    // the damage and heal numbers over the ants (On, Off), remembered; no key
    kHudArt = 17,           // the look of the match screen's frame, buttons and pedestals (New, Classic: hud_art.hpp), remembered; no key
    kShowPing = 18,         // "ping NN ms" in the corner of a network game (On, Off: latency_corner.hpp), remembered; no key; in a match of a seat, not in a recording
    kShowFps = 19,          // the frame rate and its graph in the corner (On, Off), remembered; no key; off until a player switches it on; the version stays
    kQuickChat1 = 20,       // the four quick chat texts that F9 - F12 say to everybody (the original's "Quick Chat F9" .. "Quick Chat F12"), up to 100 printable ASCII characters each; ids 20 - 23: text rows
    kQuickChat4 = 23,
    kQuickChatNote = 24,    // the line at the top of the Quick Chat section (no value)
};
inline constexpr int kQuickChatMax = 100;           // the longest quick chat text (the original's profile reader takes 100 characters)

// What the skin may ask of the game besides reading and changing a row (ants_menu_do): Leave game, the quick help (F1), and, for the desktop's drawn panel, the window's fullscreen (the page does its own)
enum Action : int { kActLeave = 1, kActQuickHelp = 2, kActFullscreen = 3 };

// What the skin may read besides a row (ants_menu_get): 100 where the menu stands (-1: not now, 0: a match, 1: a recording or a live match that is watched), 101 whether something needs an answer
// first (a dialog of the match, a vote, a lost link, the results: the menu closes), 102 what Leave game would do (LeaveKind)
enum Fact : int { kFactWhere = 100, kFactNeedsAnswer = 101, kFactLeave = 102 };

enum class Section : uint8_t { Sound, Video, Controls, Game, QuickChat };
enum class Kind : uint8_t { Slider, Switch, Choice, Button, Info, Text };

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

/// What a screen shows of the table: the sections that a match (or a recording that is watched) has, in order, and the rows of one section that this build (`web`) shows there. The page's panel gets the same from
/// describe_json; the desktop's drawn panel asks here.
struct SectionInfo {
    Section section;
    const char* id;
    const char* label;
};
std::vector<SectionInfo> sections_for(bool watching);
std::vector<const Row*> rows_for(Section section, bool watching, bool web);

/// The Controls list (the keys and the mouse, docs/CONTROLS.md): the lines of each group. `keys` of a line: for the mouse and touch groups the words of the input ("Left click"); for the Keys group the keys
/// separated by '|', where a part that starts with '~' is the word between two keys ("Ctrl|N|~/|P" is Ctrl, N, "/", P). `play_only` leaves the line out of a recording.
enum class ControlGroup : uint8_t { Mouse, MouseWatch, Touch, TouchWatch, Keys };
struct ControlLine {
    const char* keys;
    const char* what;
    bool play_only;
};
const ControlLine* control_lines(ControlGroup group, size_t& count) noexcept;
const char* controls_note() noexcept;


/// What the skin of a screen is shown: a match (watching = false) or a recording / live match that is watched. `web` is the build.
/// One JSON text: {"v":1,"sections":[{"id","label"}],"rows":[{"id","key","sec","kind","label","hint","min","max","def","words":[...],"live","new","page"}], "controls":{...}, "words":{...}} (every text is escaped; no markup)
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
/// The words of the question that Leave game asks, for a kind that asks
struct LeaveWords {
    const char* title;
    const char* body;
    const char* go;
};
LeaveWords leave_words(LeaveKind kind) noexcept;
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
    bool health_changes{true};      // the damage and heal numbers (docs/VIEW_AND_HUD.md, "Damage and heal numbers"): on until a player switches them off
    bool hud_art_new{true};         // the HUD art (docs/VIEW_AND_HUD.md, "HUD art"): the new pictures until a player chooses the original's own
    bool show_ping{false};          // "ping NN ms" in the corner of a network game (docs/MULTIPLAYER.md): off until a player switches it on
    bool show_fps{false};           // the frame rate and its graph in the corner: off until a player switches it on (the version stays)

    static const char* key(int row_id) noexcept;                        // "master", "mute", "ant_health", "ant_health_style", "health_changes", "show_ping", "hud_art" ("" for any other row)
    /// An old `hp_numbers=off` (the numbers that this setting replaces) is read as Off when `ant_health` was never written; every other old value, and none, is the default (hurt ants, a bar)
    void load(const ConfigStore& store);
    void write(ConfigStore& store, int row_id) const;                   // writes one of the seven
    /// The gain that master and mute give the whole mix: 0 when muted, else master (99 and 100 are the full volume) over 99
    float gain() const noexcept;
};

}  // namespace ants::app::game_menu
