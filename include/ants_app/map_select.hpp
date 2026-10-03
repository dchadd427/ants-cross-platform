#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_app/map_preview.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_app/setup_layout.hpp"

namespace ants::app {

/// One entry of the map list: the file name and what the file's header says (the original reads the header when the entry is chosen)
struct MapSelectEntry {
    std::string filename;
    std::string full_path;
    std::string display_name;
    std::string description;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t minutes{0};
};

/**
 * @brief The setup screen of the original (Ants.exe: base class FUN_01012ce0, host screen FUN_01013b36, refresh FUN_010133ef, map list FUN_01013de7, selection
 * FUN_01013fc9, keys FUN_01014076, START FUN_010140c5; docs 5.50).
 *
 * The maps are every `*.lvl` of the Maps folder in the byte order of their file names. All labels, the portraits and the thumbs appear with the
 * first run of the refresh task, 500 ms after the screen was created. The buttons are the original's button class (ScreenButton: the callback runs at the release);
 * the keys are Up / Down (previous / next map, wrapping), Enter, S and s (START), Q, q, X and x (Leave); nothing else does anything. START locks the screen.
 *
 * A room on a dedicated server has no host: every player has the guest screen, except the room's LEADER (the first player who joined, protocol 7), who has the host's screen in
 * a "server room" mode: the room's map is shown (the list has no choice to make: Up, Down and the wrap change nothing), the Fog of War buttons show the room's choice and change
 * nothing, START asks the server to start the match with the players who are there (`set_on_request_start`), and Leave works as on every screen.
 */
class MapSelectScreen {
public:
    static constexpr double REFRESH_MS = 500.0;    // the refresh task's delay (FUN_01031e92(task, 500, 0)): until then the labels are empty and nobody is listed

    // The labels of the screen (FUN_01012ce0): the map name (36, 312) 179 x 26, its description (36, 380) 293 x 26, the prompt (36, 447) 293 x 35 with 14 px lines,
    // the players' names (415, 95 + 50 i) 120 x 20; every label has 18 px lines (the prompt 14) and the colour (239, 231, 223)
    static constexpr int32_t LABEL_X = 36;
    static constexpr int32_t NAME_Y = 312;
    static constexpr int32_t NAME_W = 179;
    static constexpr int32_t INFO_Y = 380;
    static constexpr int32_t STATUS_Y = 447;
    static constexpr int32_t STATUS_W = 293;
    static constexpr int32_t STATUS_H = 35;       // the prompt label is 35 high (two lines of 14 px): a click on it opens a room's chat input
    static constexpr int32_t PLAYER_NAME_X = 415;
    static constexpr int32_t PLAYER_NAME_W = 120;

    // The rectangles of the buttons' pictures (docs 5.50 / 5.52): the original's hit test is the rectangle of the picture that shows now, so every button has the
    // rectangle of its resting / hovering picture (the BTN_*_X / Y / W / H, = up1 / up2, down1 / down2, start1 / start2, leave1 / leave2) and that of its pressed
    // picture (BTN_*_PRESSED, = up3, down3, start3, leave3). The part offsets are absolute screen coordinates. The Fog of War pair is a latching pair of four
    // pictures each; it keeps the union of its first and third picture (docs 5.52).
    static constexpr int32_t BTN_UP_X = 226;
    static constexpr int32_t BTN_UP_Y = 299;
    static constexpr int32_t BTN_UP_W = 46;
    static constexpr int32_t BTN_UP_H = 22;
    static constexpr ButtonRect BTN_UP_PRESSED{224, 301, 47, 21};

    static constexpr int32_t BTN_DOWN_X = 226;
    static constexpr int32_t BTN_DOWN_Y = 323;
    static constexpr int32_t BTN_DOWN_W = 47;
    static constexpr int32_t BTN_DOWN_H = 20;
    static constexpr ButtonRect BTN_DOWN_PRESSED{225, 324, 47, 21};

    static constexpr int32_t BTN_FOW_ON_X = 522;
    static constexpr int32_t BTN_FOW_ON_Y = 372;
    static constexpr int32_t BTN_FOW_ON_W = 49;
    static constexpr int32_t BTN_FOW_ON_H = 24;

    static constexpr int32_t BTN_FOW_OFF_X = 574;
    static constexpr int32_t BTN_FOW_OFF_Y = 372;
    static constexpr int32_t BTN_FOW_OFF_W = 49;
    static constexpr int32_t BTN_FOW_OFF_H = 24;

    static constexpr int32_t BTN_START_X = 526;
    static constexpr int32_t BTN_START_Y = 439;
    static constexpr int32_t BTN_START_W = 98;
    static constexpr int32_t BTN_START_H = 27;
    static constexpr ButtonRect BTN_START_PRESSED{527, 443, 97, 24};

    static constexpr int32_t BTN_QUIT_X = 525;
    static constexpr int32_t BTN_QUIT_Y = 12;
    static constexpr int32_t BTN_QUIT_W = 99;
    static constexpr int32_t BTN_QUIT_H = 22;
    static constexpr ButtonRect BTN_QUIT_PRESSED{524, 14, 98, 20};

    // The player rows of the Players' Status box: slot i has its portrait origin at (395, 115 + 50 i) and its thumb at
    // (540, 95 + 50 i) (Ants.exe setup screen, SetPos of the AntSlot and thumb sprites)
    static constexpr int32_t PLAYER_PORTRAIT_X = 395;
    static constexpr int32_t PLAYER_PORTRAIT_Y = 115;
    static constexpr int32_t PLAYER_THUMB_X = 540;
    static constexpr int32_t PLAYER_THUMB_Y = 95;
    static constexpr int32_t PLAYER_ROW_PITCH = 50;


    MapSelectScreen();
    ~MapSelectScreen() = default;

    /// Reads the map list: every `*.lvl` of `maps_dir`, sorted by the byte order of the file names (no map is named in the program; an unreadable folder gives an empty list)
    void init(const std::string& maps_dir = "Original-Ants/Maps");
    /// The screen is created (again): the labels are empty until the refresh, nothing is locked, no button is pressed
    void enter();
    /// The screen's clock: 500 ms after `enter` the refresh shows the labels, the portraits and the thumbs
    void update(float dt_seconds);
    bool refreshed() const noexcept { return elapsed_ms_ >= REFRESH_MS; }
    /// START was accepted: up / down, the fog option and a second START do nothing any more (+0x130)
    void lock() noexcept { started_ = true; }
    /// The start was cancelled (a player left, a map did not load): the host is back in the room and may choose again (the original's START is never undone; a mesh
    /// has no cancel, the remake's barrier has)
    void unlock() noexcept { started_ = false; }
    bool is_locked() const noexcept { return started_; }

    /// Left button only. The buttons capture on the press (their pressed picture and click sound) and act at the release (docs 5.45)
    void handle_mouse_down(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_up(int32_t screen_x, int32_t screen_y, uint8_t button);
    void handle_mouse_motion(int32_t screen_x, int32_t screen_y);
    /// FUN_01014076: Up / Down step the map list (wrapping), Enter, S and s start, Q, q, X and x leave, every other key does nothing (Esc included). `repeat` is the auto-repeat
    /// of a held key (SDL_KeyboardEvent::repeat). The original does not tell a repeat from a press (its key translation, FUN_01031bbd, never reads the repeat bit of a key message;
    /// the input queue only thins them to one per 50 ms), so on its own screens a repeat of START acts like a press, and the LOCAL game's screen (no network, no chat input) keeps that exactly: a
    /// held Enter starts it. On the screens of a ROOM (`RoomView::networked`: the LEADER of a server's room, which starts the match of every player there, and the host of a room on the local
    /// network) START needs a PRESS (a deliberate difference from the original, after a review found a held Enter that started a match nobody meant to start: a key that closed the quick help
    /// or the chat input and is still held, or pressed twice, must not press the new screen's START): the repeat of START (Enter, the keypad's Enter, S, s) is ignored there. Every other
    /// key acts on a repeat everywhere (Up and Down scroll the list while they are held).
    void handle_key_down(SDL_Keycode key, bool repeat = false);
    /// Lets go of every button (nothing is pressed or captured any more): the application calls it when the press that was on a button is not the screen's (a click that closed the chat input)
    void release_buttons();

    void render(IRenderer& renderer, const ants::assets::AssetArchive& archive);

    void set_on_start(std::function<void(const std::string& map_path)> cb) {
        on_start_ = std::move(cb);
    }
    void set_on_quit(std::function<void()> cb) {
        on_quit_ = std::move(cb);
    }
    void set_on_play_sfx(std::function<void(uint32_t)> cb) {
        on_play_sfx_ = std::move(cb);
    }
    void play_sfx(uint32_t sound_id) {
        if (on_play_sfx_) on_play_sfx_(sound_id);
    }

    int32_t get_selected_index() const noexcept { return selected_index_; }
    void set_selected_index(int32_t idx) noexcept;

    const std::string& get_selected_map_path() const;
    const std::vector<MapSelectEntry>& get_maps() const noexcept { return maps_; }

    bool is_fog_of_war_enabled() const noexcept { return fog_of_war_; }
    const ScreenButton& up_button() const noexcept { return up_; }
    const ScreenButton& down_button() const noexcept { return down_; }
    const ScreenButton& start_button() const noexcept { return start_; }
    const ScreenButton& quit_button() const noexcept { return quit_; }
    const ScreenButton& fog_on_button() const noexcept { return fow_on_; }
    const ScreenButton& fog_off_button() const noexcept { return fow_off_; }

    /// The thumb beside a player's name (the original's netgood / netok / netbad / netunk animations: connection quality)
    enum class Thumb : uint8_t { Good = 0, Ok = 1, Bad = 2, Unknown = 3 };
    /// One seat of the Players' Status box
    struct RoomSeat {
        bool occupied{false};
        std::string name;
        Thumb thumb{Thumb::Good};
    };
    /// What a network room shows: the seats, who this machine is and whether it may change the setup. `networked` false = the local setup screen.
    struct RoomView {
        bool networked{false};
        bool is_host{true};
        bool leader{false};                    // a player of a dedicated server's room who leads it (is_host is false for it): the host's screen with START, the room's setup shown
        uint8_t my_seat{0};
        std::array<RoomSeat, 4> seats{};
        std::string map_file;                  // the host's choice (its file name), what a guest shows ("" before the host's first message)
        std::string status;                    // replaces the prompt line while networked
        /// A line that the player is TYPING (the room's chat input, room_chat.hpp): `status_prefix` ("Say: ") and `status` (what is typed) are drawn as a typed line, in the prompt's two lines, and
        /// its END shows when it is longer; `status_caret` says whether the blinking caret is on. False: `status` is a text for the prompt, and what does not fit its box ends in "...".
        bool status_input{false};
        std::string status_prefix;
        bool status_caret{false};
    };
    /// The lines that the prompt label (36, 447) 293 x 35 draws for a room view: the original's label holds two lines of 14 px, so whatever the status says is fitted to them (a text that
    /// needs more ends in "...", a typed line shows its end). Public so that the tests measure what is drawn with the real font.
    static constexpr size_t STATUS_LINES = 2;
    static std::vector<std::string> status_lines(const IRenderer& renderer, const RoomView& room);
    void set_room(const RoomView& room) { room_ = room; }
    const RoomView& room() const noexcept { return room_; }
    /// A player of a room who is not its host: the original shows him ANOTHER screen (FUN_01014228: nh_start, no Up / Down / START / Fog buttons, a fixed
    /// "Fog of War?" box with the host's choice, only Leave) and its key handler knows Q / X only. A room on a dedicated server has no host: all are guests, except its leader (leads_server_room()).
    bool is_guest() const noexcept { return room_.networked && !room_.is_host && !room_.leader; }
    /// The leader of a dedicated server's room (RoomView::leader): the host's screen, but the map and the fog option are the room's and cannot be changed, and START is a
    /// request to the server (set_on_request_start)
    bool leads_server_room() const noexcept { return room_.networked && !room_.is_host && room_.leader; }
    /// The map and the fog option belong to the host of a room (and to the local screen); a guest's clicks on them do nothing, and neither do a leader's (it sees the room's)
    bool can_change_setup() const noexcept { return !is_guest() && !leads_server_room(); }
    /// START is on the host's screen, the local screen and the leader's; not on a guest's
    bool has_start_button() const noexcept { return !is_guest(); }
    /// Which seat each of the four rows shows, -1 = the row stays blank (the original's row i is slot i of the machine's own peer table, FUN_010133ef: the local
    /// machine is slot 0 on host and guest alike, a guest's slot 1 is the host, the others follow in the order they were met). A host (and the local screen):
    /// row r = seat r, a seat that is not taken leaves its row blank. A guest, and the leader of a server's room (which has no host that could sit in a
    /// place of its own, and whose rows must not jump when the lead moves): its own seat first, then the other seats in ascending order (the host is seat 0 of a LAN room,
    /// so it comes right after the guest itself), compact.
    static std::array<int8_t, 4> row_seats(const RoomView& room) noexcept;
    /// The host changed the map (its file name) or the fog option through the controls
    void set_on_map_changed(std::function<void(const std::string& filename)> cb) { on_map_changed_ = std::move(cb); }
    void set_on_fog_changed(std::function<void(bool)> cb) { on_fog_changed_ = std::move(cb); }
    /// A guest (and the leader of a server's room) shows the host's / the room's choice (no callbacks fire); false when the map is not in the list
    bool follow_host_choice(const std::string& filename, bool fog);
    /// START on the leader's screen (button, Enter, S, s): the leader asks the server to start now. Nothing else fires on that screen but Leave.
    void set_on_request_start(std::function<void()> cb) { on_request_start_ = std::move(cb); }

    void set_player_name(std::string name) { player_name_ = std::move(name); }
    void set_player_team(uint8_t team) noexcept { player_team_ = team; }

    // ---- The wide setup screen (setup_layout.hpp): the same screen composed for the 16:9 picture of 960 x 540, with a map preview ----
    /// true: the screen is drawn and answers the pointer as the wide screen (the picture is the whole 960 x 540 canvas: the application decides, SetupLayout::supports); false (the default):
    /// the original's 640 x 480 page, exactly as always. The buttons' rectangles follow (the hit tests are the rectangles of the pictures that show).
    void set_wide_layout(bool wide);
    bool wide_layout() const noexcept { return wide_; }
    /// Which variant of the wide screen the room shows: the local game's host screen, the host screen of a network game (a LAN host, the leader of a room) or a guest's screen
    SetupVariant setup_variant() const noexcept { return is_guest() ? SetupVariant::Guest : (room_.networked ? SetupVariant::Online : SetupVariant::Single); }

    /// The chat column of the wide screen's Online and Guest variants (SetupLayout::chat) is the waiting-room chat of the online rooms: nothing is drawn there until the room's UI
    /// turns it on (`visible`) and fills it (the application does while a room is on the screen: Application::sync_room_view). The black chat box (an efram box) holds the lines, the input box (statline's box) the typed text, the label "Chat" is TrueType in the engraved
    /// labels' teal with their ink as shadow; the lines are in the font, size and colour of the players' names (18 px, the labels' cream), a notice (a line of the server: sender 255) in
    /// the colour and size of the screen's status text (14 px). Nothing here applies to the Single variant or the classic page.
    struct ChatPanel {
        bool visible{false};
        struct Line {
            std::string text;               // the whole line as it is shown ("Ben: hi"; a notice: "Ben joined.")
            bool notice{false};
        };
        std::vector<Line> lines;            // oldest first; they are wrapped to the box and as many of the newest as fit stand at its bottom
        std::string typed;                  // the input box's text (its end shows when it is longer than the box)
        bool caret{false};                  // the blinking caret behind the typed text
    };
    void set_chat_panel(ChatPanel panel) { chat_panel_ = std::move(panel); }
    const ChatPanel& chat_panel() const noexcept { return chat_panel_; }
    /// The foot of the Players' Status box on the Online variant, where the leader sees what fills the empty seats at START ("Empty seats at START:" / "Medium bots"): two 18 px lines in the
    /// colour of the players' names. Nothing is drawn while both are empty (the default).
    void set_fill_footer(std::string first_line, std::string second_line) {
        fill_footer_[0] = std::move(first_line);
        fill_footer_[1] = std::move(second_line);
    }
    const std::array<std::string, 2>& fill_footer() const noexcept { return fill_footer_; }

private:
    void render_wide(IRenderer& renderer, const ants::assets::AssetArchive& archive);        // map_select_wide.cpp
    void place_buttons();                                                                      // the buttons' rectangles for the classic or the wide page
    const MapPreview* preview_of(const MapSelectEntry& entry, int32_t inner, IRenderer& renderer, const ants::assets::AssetArchive& archive);
    void trigger_start();
    void trigger_quit();
    void step_map(int32_t delta);          // FUN_01013fc9
    void start();                          // FUN_010140c5 (the application decides whether everybody is ready)

    std::vector<MapSelectEntry> maps_;
    int32_t selected_index_{0};

    ScreenButton up_{ButtonRect{BTN_UP_X, BTN_UP_Y, BTN_UP_W, BTN_UP_H}, BTN_UP_PRESSED};
    ScreenButton down_{ButtonRect{BTN_DOWN_X, BTN_DOWN_Y, BTN_DOWN_W, BTN_DOWN_H}, BTN_DOWN_PRESSED};
    ScreenButton start_{ButtonRect{BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H}, BTN_START_PRESSED};
    ScreenButton quit_{ButtonRect{BTN_QUIT_X, BTN_QUIT_Y, BTN_QUIT_W, BTN_QUIT_H}, BTN_QUIT_PRESSED};
    ScreenButton fow_on_{BTN_FOW_ON_X, BTN_FOW_ON_Y, BTN_FOW_ON_W, BTN_FOW_ON_H};
    ScreenButton fow_off_{BTN_FOW_OFF_X, BTN_FOW_OFF_Y, BTN_FOW_OFF_W, BTN_FOW_OFF_H};

    bool fog_of_war_{false};
    bool started_{false};                 // +0x130: START ran
    double elapsed_ms_{0.0};              // since the screen was created
    std::string player_name_{};
    uint8_t player_team_{0};

    bool wide_{false};                    // the wide 960 x 540 screen (set_wide_layout)
    ChatPanel chat_panel_{};
    std::array<std::string, 2> fill_footer_{};
    struct PreviewSlot {
        std::string path;
        int32_t inner{0};
        MapPreview preview;               // invalid when the file cannot be read
    };
    std::vector<PreviewSlot> previews_;   // the pictures made so far (the newest last, at most kMaxPreviews)
    static constexpr size_t kMaxPreviews = 24;      // 24 pictures of 300 x 300 pixels: 8.6 MB at most
    bool fallback_logged_{false};         // the line that says the preview fell back to the minimap colours is logged once

    RoomView room_{};
    std::function<void(const std::string& filename)> on_map_changed_{nullptr};
    std::function<void(bool)> on_fog_changed_{nullptr};
    void change_fog(bool on);
    std::function<void(const std::string& map_path)> on_start_{nullptr};
    std::function<void()> on_request_start_{nullptr};
    std::function<void()> on_quit_{nullptr};
    std::function<void(uint32_t)> on_play_sfx_{nullptr};
};

} // namespace ants::app
