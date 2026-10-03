#pragma once

// The desktop start menu of the remake (remake-only: the original has no such screen; its closest relative is the Single / Multi choice screen of
// FUN_01016dfc, whose picture `sm_screen` is the background here). A native game that is started without a mode on the command line shows it after the loading
// screen and before the quick help: Single player (with computer players per seat), Join with a code (a room of the game server), Host an online match
// (a room that this player makes on the server and shares by its code), Quit.
//
// This file is the MODEL: what the screen holds, where its controls lie, what the keys and the mouse do and which action it asks the application for. It has no
// window, no sockets and no clock of its own (the application feeds it the time), so the tests drive it directly, like the HUD layout and the setup screen.
// The drawing is `render_start_menu` (start_menu_view.cpp), a thin function over the elements that the model describes; the connection to the server is the
// application's (application_menu.cpp).

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include "ants_ai/bot.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_net/protocol.hpp"

namespace ants::app {

// ---- what the menu remembers ---------------------------------------------------------------------------------------------------------------------------

/// The game server that Join and Host use when nothing else is said (--server, the settings key `server`): the public beta server's TCP game port
inline constexpr const char* kDefaultServerHost = "beta.playants.org";
inline constexpr uint16_t kDefaultServerPort = 4001;

/// A server as the menu writes it: host[:port]
struct ServerAddress {
    std::string host{kDefaultServerHost};
    uint16_t port{kDefaultServerPort};
    bool operator==(const ServerAddress& o) const noexcept { return host == o.host && port == o.port; }
};

/// HOST, HOST:PORT, an IPv4 address, "[IPv6]" or "[IPv6]:PORT" (a bare IPv6 address without brackets has no port). A host is 1 - 253 letters, digits, '.', '-' and '_'; a port is
/// 1 - 65535. False with the reason in `why` for anything else (the text of --server and of the settings key `server` goes through this).
bool parse_server(const std::string& text, ServerAddress& out, std::string& why);
/// "host:port" ("[v6]:port" for an IPv6 address), what the screen shows and parse_server reads back
std::string server_label(const ServerAddress& server);

/// What a seat of the single-player panel holds: nobody (the original's single-player game), or a computer player of a level (docs/BOTS.md)
enum class SeatChoice : uint8_t { Empty = 0, Easy, Medium, Hard };
inline constexpr size_t kSeatChoiceCount = 4;
/// "Empty", "Easy bot", "Medium bot", "Hard bot"
const char* seat_choice_text(SeatChoice choice) noexcept;

/// A map that a hosted match can be made on: the six maps of the original game, which the game server's demo rooms offer (web/four.html makes the same codes)
struct MenuMap {
    const char* key;       // the word of the room code ("small")
    const char* name;      // what the screen shows ("Small")
};
inline constexpr size_t kMenuMapCount = 6;
/// The map that the Host panel offers when the settings hold no choice (a `host_map` that was stored wins): Treasure, the map that is played most (the owner's request: Treasure is the default
/// of everything). The list keeps the order of web/four.html (by size), so its first entry, Tiny, is not the default; `kDefaultMenuMap` is Treasure's index (start_menu.cpp checks it).
inline constexpr int kDefaultMenuMap = 4;
/// The map at `index`; an index that is not in the list gives the default map, Treasure
const MenuMap& menu_map(size_t index) noexcept;
/// The index of a key (any case), or -1
int menu_map_index(const std::string& key) noexcept;

/// What the Host panel's "Empty seats at START" holds: nothing (the match starts with the people who are there), or bots of a level that the server seats in the empty seats when this
/// player, the room's leader, presses START (network protocol 11; the web page's `?fill=`): "Leave empty", "Easy bots", "Medium bots", "Hard bots"
const char* fill_choice_text(net::FillLevel level) noexcept;
/// The line under the choice on the Host panel (and under the choice on web/four.html): what the bots can do. "Bots gather food; they do not fight yet."
const char* fill_choice_caption() noexcept;
/// What the room's panel tells the leader about START: "Empty seats will be Medium bots." / "Empty seats stay empty."
std::string fill_choice_sentence(net::FillLevel level);

/// The room code of a hosted match, made as web/four.html makes it: "demo-<map>-<n>p-<six characters>", the six from kRoomCodeAlphabet (lower case letters without i, l and o,
/// and the digits 2 - 9: no look-alikes). `random` gives 32 random bits at each call. At most 23 characters, so it always fits the 32 that a room code may hold.
inline constexpr const char* kRoomCodeAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";
inline constexpr size_t kRoomCodeRandomChars = 6;
std::string make_room_code(const MenuMap& map, int players, const std::function<uint32_t()>& random);

/// The player's name as the menu sends it: blanks at both ends cut away, printable ASCII only (what the original's edit field takes), at most `net::kMaxNameChars`
std::string clean_player_name(const std::string& raw);
/// Does the name look like a computer player's ("Bot (Hard)": its first four characters, leaving out blanks and case, are "bot("): the server never lets a person have such a name
/// (it renames the player), so the menu refuses it with the reason
bool looks_like_bot_name(const std::string& name);
/// True when `raw` can be sent as a player's name (`clean` holds it); else false with the reason in `why`
bool check_player_name(const std::string& raw, std::string& clean, std::string& why);
/// True when `raw` (blanks at both ends cut away into `clean`) is a room code of the server: 1 - 32 letters, digits, '-' and '_'. The case is NEVER changed: the server's codes are case
/// sensitive ("demo-small-x7k2" and "DEMO-SMALL-X7K2" are two different rooms, and only the first is a demo room that the server makes on demand).
bool check_room_code(const std::string& raw, std::string& clean, std::string& why);

/// Which of the remembered values changed (the owner stores that one: one file write per change)
enum class MenuSetting : uint8_t { Name, Bots, HostMap, HostPlayers, HostFill };

/// The values of the menu that the program remembers in its settings file (config_store.hpp), under the remake's own keys (the original's nine entries keep their names): `name`,
/// `bots` (four words by seat: off, easy, medium, hard), `host_map` (a map key), `host_players` (2 - 4), `host_fill` (none, easy, medium, hard: the empty seats at START), and `server` (host[:port]; written by hand, the menu shows it and never edits it)
struct MenuSettings {
    std::string name;                                   // "" when none is stored (the menu then proposes the game's default)
    std::string server;                                 // "" when none is stored (the default server)
    std::array<SeatChoice, 4> seats{};                  // by seat: green, red, blue, black
    int host_map{kDefaultMenuMap};                      // index into the six maps (Treasure until a choice is stored)
    int host_players{4};                                // 2 - 4
    net::FillLevel host_fill{net::FillLevel::None};     // the bots that the leader's START seats in the empty seats (none: the match starts with the people who are there)

    static constexpr const char* kKeyName = "name";
    static constexpr const char* kKeyServer = "server";
    static constexpr const char* kKeyBots = "bots";
    static constexpr const char* kKeyHostMap = "host_map";
    static constexpr const char* kKeyHostPlayers = "host_players";
    static constexpr const char* kKeyHostFill = "host_fill";

    /// Reads the stored values with the rules of the store (printable text cut to its maximum, numbers in their range, anything else is the default)
    void load(const ConfigStore& store);
    /// Writes one value (and only that one) to the store, which saves at once
    void write(ConfigStore& store, MenuSetting setting) const;
};

// ---- the model ------------------------------------------------------------------------------------------------------------------------------------------

enum class MenuPanel : uint8_t { Main, Single, Join, Host, Connecting, Room };

/// The controls of the panels (one enumerator each; the seat rows are Seat0 + seat)
enum class MenuId : uint8_t {
    None = 0,
    Single, JoinWithCode, HostOnline, Quit,             // the first panel
    Seat0, Seat1, Seat2, Seat3, Continue,               // single player
    Name, Code, Join,                                   // join with a code
    HostMap, HostPlayers, HostFill, HostName, Host,     // host an online match
    Cancel,                                             // connecting
    Copy, EnterRoom,                                    // the room's code
    Back                                                // every panel but the first (on the room's panel: leave the room)
};

enum class MenuKind : uint8_t {
    Title,      // the banner at the top
    Text,       // a line (or a few) on the background
    Notice,     // a message in a dark box (an error, a note)
    Code,       // the room code, in large letters in a dark box
    Button,
    Cycler,     // a button that shows a value and goes through its values (click, Enter, Left / Right)
    Field,      // an edit field (the original's: appends printable characters, Backspace deletes)
    Portrait    // an ant of a colour (the seat of a row)
};

enum class MenuTone : uint8_t { Normal, Dim, Bad };

/// Where an element hangs in the 16:9 picture (960 x 540): the title plate stays at the top, the hint and the server line are bottom pieces, everything between is one centred group.
/// The model's numbers are the original's 640 x 480 page; a wide menu moves each group by the offsets of StartMenu::kWide* (nothing moves in the classic picture).
enum class MenuGroup : uint8_t { Top, Middle, Bottom };

/// One thing on the screen, as the drawing and the tests see it
struct MenuElement {
    MenuId id{MenuId::None};                  // None for what cannot be selected
    MenuKind kind{MenuKind::Text};
    ButtonRect rect{};
    std::string text;                         // the caption of a button, the text of a line or a field, a cycler's label
    std::string value;                        // a cycler's current value
    FontSize font{FontSize::Px18};
    MenuTone tone{MenuTone::Normal};
    bool centered{false};
    bool selected{false};                     // the pointer is on it, or the keyboard selection
    bool pressed{false};                      // the left button went down on it and is still down
    bool caret{false};                        // a field: the caret is in its blink phase
    bool all_selected{false};                 // a field: its text is selected (the next typed character replaces it)
    uint8_t team{0};                          // a portrait: the colour of the seat
    MenuGroup group{MenuGroup::Middle};       // where it hangs when the menu is the 16:9 picture (its rect is already moved)
};

/// What the menu asks the application to do (take_request)
struct MenuRequest {
    enum class Type : uint8_t { None, Quit, Single, Join, Host, Cancel, EnterRoom, LeaveRoom };
    Type type{Type::None};
    std::string name;                         // Join, Host: the player's name (cleaned)
    std::string room;                         // Join: the room code (cleaned)
    int map{0};                               // Host: the index of the map
    int players{4};                           // Host: 2 - 4
    net::FillLevel fill{net::FillLevel::None};   // Host: the bots that START seats in the empty seats (Application::set_fill_bots before the connection is made)
    std::vector<ai::BotSpec> bots;            // Single: the computer players, by seat
};

class StartMenu {
public:
    // The screen is 640 x 480 like the original's; the frame of the background takes 16 pixels on every side.
    static constexpr int32_t kWidth = 640;
    static constexpr int32_t kHeight = 480;
    static constexpr size_t kNameMax = 32;              // = net::kMaxNameChars: what the room's protocol carries (the original's own peer table keeps 50; its score labels show 15)
    static constexpr size_t kCodeMax = 32;              // = net::kMaxRoomCodeChars
    static constexpr uint32_t kCaretHalfPeriodMs = 150; // as the original's edit field
    static constexpr uint32_t kCopiedMs = 2000;         // how long "Copied!" shows on the Copy button
    /// After a panel appears, Enter and Space do nothing for this long (the menu's own clock), and Esc does not quit from the first panel: a key that is pressed twice in a hurry (Enter, Enter)
    /// is one gesture, and its second press must not act on the panel that the first one opened (its default button would start the game, create the room or cancel the attempt)
    static constexpr uint32_t kSettleMs = 300;
    /// What the Join and Host panels say when a typed or pasted character was refused (a letter that is not A-Z, an accent, a CJK character: the name and the code take printable ASCII)
    static constexpr const char* kRefusedCharsText = "Only letters A-Z, digits and simple punctuation.";
    /// The line of the single-player panel under the seats (the rule-8 text when a computer player is seated, the original's game when nobody is)
    static constexpr const char* kBotsLine = "Bots play without fog of war. Empty seats have no ants.";
    static constexpr const char* kNoBotsLine = "No bots: the original single-player game (the other colours stand still).";

    StartMenu();

    // ---- what the owner tells the menu --------------------------------------------------------------------------------------------------------------
    /// The seat of the person at this machine (0 green, 1 red, 2 blue, 3 black): the single-player panel offers the other three
    void set_own_seat(uint8_t seat) noexcept;
    uint8_t own_seat() const noexcept { return own_seat_; }
    /// The remembered values, or what the command line chose (nothing is written back by this call). The name is the one the screen proposes: the owner puts the right one
    /// there (--name, else the stored one, else the game's default for a network game).
    void set_settings(const MenuSettings& settings);
    const MenuSettings& settings() const noexcept { return settings_; }
    /// The server that Join and Host use, as the screen shows it
    void set_server(const ServerAddress& server) { server_ = server; }
    const ServerAddress& server() const noexcept { return server_; }
    /// Called with the value that just changed (the owner writes it to the store)
    void set_on_change(std::function<void(MenuSetting)> cb) { on_change_ = std::move(cb); }
    /// A UI sound (the click of a pressed button, the cue of a refusal): the owner plays it
    void set_on_play_sfx(std::function<void(uint32_t)> cb) { on_play_sfx_ = std::move(cb); }
    /// The system clipboard: `get` for Ctrl+V / Cmd+V, `set` for Copy (false when it could not be written). Without them nothing is pasted and Copy reports a failure.
    void set_clipboard(std::function<std::string()> get, std::function<bool(const std::string&)> set);

    // ---- what happens to the menu ---------------------------------------------------------------------------------------------------------------------
    /// The first panel, nothing selected but its first entry; `notice` (a line in a dark box, e.g. why a match ended) is shown on it when it is not empty
    void show_main(const std::string& notice = std::string());
    /// A join or a host attempt failed (or the connection was lost): back to the panel it came from with `message` as an error line
    void connection_failed(const std::string& message);
    /// The person cancelled (Esc, Cancel): back to the panel it came from, no message
    void connection_cancelled();
    /// The room was made and the person is in it (Host): the panel with the code; `players_in` of `capacity` seats are taken
    void show_room(const std::string& code, int players_in, int capacity);
    void set_room_players(int players_in, int capacity);
    /// The room's panel is left (the connection is gone): back to the Host panel, with `message` as an error line when it is not empty
    void room_left(const std::string& message = std::string());
    /// The menu's own clock (caret blink, "Copied!", the settling of a new panel), in seconds of game time
    void update(float dt_seconds);
    /// Writes the name that was typed and not written yet (the owner stores it). The name is written when the field is left, a panel changes, Join or Host is pressed, and by the owner when
    /// the program ends: not at every key.
    void flush();

    // ---- input ------------------------------------------------------------------------------------------------------------------------------------------
    /// The pointer moved: the control under it becomes the selection
    void on_mouse_move(int32_t x, int32_t y);
    /// A left press: true when a button or a cycler captured it (the owner plays the click); a field takes the focus. Other buttons do nothing.
    bool on_mouse_down(int32_t x, int32_t y, uint8_t button);
    /// The release runs the action of the button that is still captured and still under the pointer
    void on_mouse_up(int32_t x, int32_t y, uint8_t button);
    /// Keys: Up / Down / Tab / Shift+Tab select, Left / Right change a cycler, Enter (and Space on a button) acts, Esc goes back (on the first panel: Quit; while connecting: cancels),
    /// Backspace deletes in a field (Ctrl / Cmd+Backspace clears it), Ctrl / Cmd+V pastes, Ctrl / Cmd+A selects the field's text, Ctrl / Cmd+C copies the room's code. `repeat` is the
    /// auto-repeat of a held key: Enter, Space and Esc ignore it; Enter and Space also ignore a press within kSettleMs of the last panel change (Esc then does not quit from the first panel).
    void on_key(SDL_Keycode key, uint16_t modifiers, bool repeat = false);
    /// Typed text (SDL's text input): printable ASCII goes into the field that has the focus
    void on_text(const std::string& text);
    /// Pastes `text` into the field that has the focus (control characters dropped, cut to the field's length; a code is cut at its ends of blanks)
    void paste(const std::string& text);

    /// What the menu asks for (None when nothing is pending); taking it clears it
    MenuRequest take_request();
    bool has_request() const noexcept { return request_.type != MenuRequest::Type::None; }

    // ---- what the drawing and the tests read ------------------------------------------------------------------------------------------------------
    MenuPanel panel() const noexcept { return panel_; }
    MenuId selected() const noexcept { return selected_; }
    /// Everything on the current panel, in drawing order; the controls (id != None) in the order of the Tab key
    std::vector<MenuElement> elements() const;
    /// The control under a point (None when there is none)
    MenuId control_at(int32_t x, int32_t y) const;
    /// The element of a control (nullptr-like: id None) on the current panel
    bool find_element(MenuId id, MenuElement& out) const;
    uint32_t now_ms() const noexcept { return static_cast<uint32_t>(elapsed_ms_); }
    /// The text of the name and code fields, the message line, the current seat choices
    const std::string& name() const noexcept { return name_; }
    const std::string& code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    const std::string& room_code() const noexcept { return room_code_; }
    SeatChoice seat(size_t seat) const noexcept { return seat < settings_.seats.size() ? settings_.seats[seat] : SeatChoice::Empty; }
    bool any_bot() const noexcept;
    /// The computer players of the single-player panel as `--bot SEAT:LEVEL` would give them (the standard bot of the level), by seat, never at the own seat
    std::vector<ai::BotSpec> bots() const;
    /// The panel that Join or Host started from (what a failure or a cancel returns to)
    MenuPanel connect_origin() const noexcept { return origin_; }
    bool copied() const noexcept { return copied_ms_ > 0.0; }
    /// True when the current panel has been up for kSettleMs (Enter and Space act)
    bool settled() const noexcept { return elapsed_ms_ - panel_since_ms_ >= static_cast<double>(kSettleMs); }

    // The geometry (the frame of the background is 16 pixels wide)
    static constexpr int32_t kTitleY = 28;
    static constexpr int32_t kTitleH = 44;

    /// THE 16:9 MENU. In a 960 x 540 picture the menu is composed for the whole canvas (the wide pages' clay and frame, wide_page.hpp) and its controls keep their size, look and order: the model
    /// keeps the numbers of the original's 640 x 480 page and moves every element by what its group says: all of them to the middle (+160), the title plate stays at the top (+0), the hint and the
    /// server line are anchored to the bottom (+60), everything between is centred (+30). The classic picture has no move. Clicks and the pointer use the moved rectangles.
    static constexpr int32_t kWideDx = 160;
    static constexpr int32_t kWideTopDy = 0;
    static constexpr int32_t kWideMiddleDy = 30;
    static constexpr int32_t kWideBottomDy = 60;
    void set_wide_layout(bool wide) noexcept { wide_ = wide; }
    bool wide_layout() const noexcept { return wide_; }

private:
    std::vector<MenuId> controls() const;                  // the selectable controls of the panel, in Tab order
    void add_title(std::vector<MenuElement>& out, const std::string& text) const;
    void add_hint(std::vector<MenuElement>& out, const std::string& text) const;
    void add_message(std::vector<MenuElement>& out, int32_t y, int32_t h) const;
    void add_server_line(std::vector<MenuElement>& out, int32_t y) const;
    MenuElement control(MenuId id, MenuKind kind, ButtonRect rect, const std::string& text, FontSize font = FontSize::Px24) const;
    void finish(std::vector<MenuElement>& out) const;       // selection, pressed and caret states

    void go(MenuPanel panel);
    void select_first();
    void move_selection(int delta);
    void activate(MenuId id);
    void cycle(MenuId id, int delta);
    void back();
    void try_join();
    void try_host();
    void request(MenuRequest::Type type);
    void set_selected(MenuId id);
    void notify(MenuSetting setting);
    void play(uint32_t sound_id);
    bool is_field(MenuId id) const noexcept { return id == MenuId::Name || id == MenuId::Code || id == MenuId::HostName; }
    std::string* field_text(MenuId id) noexcept;
    void edit(MenuId id, const std::string& typed);
    void name_changed();                                    // the name field changed: written later (flush)
    void refuse(const std::string& message, MenuId focus);
    std::vector<size_t> other_seats() const;
    void copy_code();

    MenuPanel panel_{MenuPanel::Main};
    MenuPanel origin_{MenuPanel::Join};
    MenuId selected_{MenuId::Single};
    MenuId main_selection_{MenuId::Single};
    MenuId pressed_{MenuId::None};
    uint8_t own_seat_{0};
    MenuSettings settings_{};
    ServerAddress server_{};
    std::string name_;
    std::string code_;
    std::string message_;
    MenuTone message_tone_{MenuTone::Bad};
    std::string connecting_text_;
    std::string room_code_;
    int room_players_{1};
    int room_capacity_{4};
    bool wide_{false};                                      // the picture is the 16:9 one: the elements are moved (set_wide_layout)
    bool all_selected_{false};                              // the focused field's text is selected
    bool name_dirty_{false};                                // the name changed and is not written yet (flush)
    double panel_since_ms_{-1.0e9};                         // when the current panel appeared (the menu's clock)
    double elapsed_ms_{0.0};
    double caret_since_ms_{0.0};
    double copied_ms_{0.0};
    MenuRequest request_{};
    std::function<void(MenuSetting)> on_change_;
    std::function<void(uint32_t)> on_play_sfx_;
    std::function<std::string()> clipboard_get_;
    std::function<bool(const std::string&)> clipboard_set_;
};

/// Draws the current panel: the `sm_screen` background of the original (the 16:9 menu: the wide clay and frame instead), then the elements (banners, buttons and fields in the colours of the
/// original's buttons and boxes, the ants of the seats, text in the game's font). The frame-rate counter and the version are the application's, drawn over it as on every screen.
void render_start_menu(IRenderer& renderer, const assets::AssetArchive& archive, const StartMenu& menu);

}  // namespace ants::app
