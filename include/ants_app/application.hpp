#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <memory>
#include <optional>
#include <array>
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
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/host_lookup.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/window_layout.hpp"

namespace ants::app {

enum class AppState {
    Loading,
    QuickHelp,
    MapSelect,
    Playing,
    StartMenu     // the desktop start menu (start_menu.hpp): after the loading screen, before the quick help, in a native game started without a mode
};

struct ApplicationConfig {
    std::string title{"Ants"};
    int window_width{1280};  // Default 2x integer scale
    int window_height{960};
    bool fullscreen{false};
    /// --aspect 16:9 | 4:3 (the settings key `aspect` when the command line does not say): the shape of the picture. 4:3 is the original's fixed 640 x 480 canvas; 16:9 is a fixed
    /// 960 x 540 canvas, in which the match screen is the wide frame (a 762 x 500 map view, the right panel pinned to the right edge: shell_layout.hpp) and the original's own pages
    /// (the loading screen, the quick help at the start, the results) are still its 640 x 480 pages, centred over a clay margin, the setup screen and the room are the wide setup screen
    /// (setup_layout.hpp: the whole canvas, with a map preview); the options window and the quick help of a match sit over the map view with the frame around them. SDL scales the canvas into the window by
    /// the largest scale that fits (whole when the window is a multiple of the canvas, else fractional), centred, with bars; a window of an aspect opens at the largest scale in steps of 0.5
    /// of the canvas that fits the display's usable area, at least 1x (--window-size and --grid still win; a game that starts in fullscreen has this size for the way back, Alt+Enter),
    /// and fullscreen is the same canvas filling as much of the monitor as fits. A desktop game that is
    /// started from the command line (parse_arguments) is 16:9 unless it says otherwise (kPlatformDefaultAspect: 16:9 in the web build too, whose page passes `--aspect`); a config that is made
    /// by hand keeps the 4:3 that it is built with. `aspect_given` is true when the command line said it (the settings key then does not count).
    Aspect aspect{Aspect::Classic4x3};
    bool aspect_given{false};
    bool headless{false};
    std::string chd_path{"Original-Ants/ants.chd"};
    std::string maps_dir{"Original-Ants/Maps"};     // the folder whose `*.lvl` files are the map list (the original searches its Maps folder)
    std::string default_map_path;                   // --map: the map of a game that starts without the setup screen; empty: the first map of the list
    std::string midi_path{"Original-Ants/INTRO.MID"};
    /// --settings: where the options are remembered (a file; the original keeps them in the registry). Empty: the per-user application folder (the browser's local
    /// storage in the web build), and nowhere in a headless run, which is what the tests use.
    std::string settings_path;
    uint32_t random_seed{1337};
    bool start_in_map_select{true};
    bool skip_intro{false};
    std::string screenshot_path{""};
    int screenshot_frames{5};
    int select_ant_id{-1};
    int select_base_team{-1};
    bool open_options{false};
    bool show_tile_grid{false};
    bool show_scorecard{false};
    uint8_t local_player_id{0};
    /// Network play: host a room, or join one (the room is the setup screen; see docs/NETWORK_PORT.md)
    enum class NetRole : uint8_t { None, Host, Join };
    NetRole net_role{NetRole::None};
    std::string net_address{"127.0.0.1"};       // Join: the host's address
    uint16_t net_port{4001};                    // the original's port
    std::string net_url;                        // Join through a WebSocket (--join-url ws://host/path or wss://...): the browser build's only way, and the one a server's proxy offers
    bool net_loopback_only{false};              // Host: accept only this machine (two copies on one computer, tests)
    uint16_t lan_port{net::kLanDiscoveryPort};  // the UDP port on which an open room announces itself to the local network (--lan-port N; 0 = not at all, --no-lan)
    std::string net_room;                       // Join: the room of a server (--room CODE); "" for a LAN / direct host
    std::string net_token;                      // Join: the credential that came with the room code (--token T)
    uint8_t net_seat{255};                      // Join: the seat asked for (--seat N, 0 .. 3: the colours green, red, blue, black); 255: any free seat
    /// A test hook (--start-when N, 2 .. 4; 0 = off, the default): a headless client has nobody to click START, so when it leads a server's room it presses START itself (the S key
    /// of the setup screen, the same path as a click) once N players are in the room, again every second until the match starts. A game that is played never uses it.
    uint8_t net_start_when{0};
    /// Where the window goes (native builds): an explicit position and size (--window-pos X,Y, --window-size WxH or W,H), or a cell of a grid over the display's usable
    /// area (--grid CxR --cell N: the start scripts lay four games out as a 2 x 2 grid, each window the largest 4:3 rectangle of its cell); --display N picks the
    /// display (default: the one the window opens on). --title sets the window's title.
    bool has_window_pos{false};
    int32_t window_x{0};
    int32_t window_y{0};
    bool has_window_size{false};
    int32_t window_w{0};
    int32_t window_h{0};
    int32_t grid_cols{0};                       // 0: no grid
    int32_t grid_rows{0};
    int32_t grid_cell{0};
    int32_t display_index{-1};
    /// --audio-focus: a window that does not have the input focus is silent (four games on one machine: you hear the one you play)
    bool audio_follows_focus{false};
    std::string player_name;                    // this player's name (--name); empty: the system user, or "Player" in a network game
    std::array<std::string, 4> team_names{};    // names of the teams of a local game (-N<team><name> as in the original, --team-name)
    /// false: a local game shows a score label only for the teams that have a name (and the local player's own). The browser build sets it: there are no
    /// other players in it (no multiplayer yet), so the colour words "Red:", "Blue:", "Black:" would only be placeholders. The original draws a label only for
    /// players that exist (docs 5.42).
    bool label_unnamed_teams{true};
    /// --bot SEAT[:SPEC] (repeatable): computer players at these seats (docs/BOTS.md). A local game then plays the seats that are taken (the local player and the bots); with
    /// --host the room shows the bots as players and the host's machine runs them. Empty by default: a game without --bot runs no bot code at all.
    std::vector<ai::BotSpec> bots;
    /// For the tests: builds the bot of a spec instead of the registry (which has the idle bot and, since B3, the worker bot), so that the application's door for a
    /// bot's commands (the local sink, the room's sink) can be exercised with a bot of the test's own that acts in a way it wants to. Empty in a game that is played.
    std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> bot_factory;
    /// The desktop start menu (README "Start menu"): after the loading screen and before the quick help, Single player (with computer players per seat), Join with a code, Host an online
    /// match, Quit. parse_arguments turns it on for a native game that is started without a mode on the command line (no --map, --map-select, --host, --join, --join-url, --room, --token,
    /// --seat, --bot, --headless, --screenshot, --player / -pnum, --select-ant, --select-base, --open-options, --scorecard) and with --start-menu, which forces it (also headless, for the tests
    /// and the screenshots). A config that is made by hand has it off, which is the game without a menu exactly; the web build never shows it (its page has its own controls).
    bool start_menu{false};
    /// --server HOST[:PORT]: the game server of the menu's Join and Host. Empty: the settings key `server`, else beta.playants.org:4001 (start_menu.hpp parse_server).
    std::string server;
    /// For the tests: the system clipboard that the menu pastes from and copies the room's code to (empty: SDL's), and the name lookup of the menu's connection (empty: the system's)
    std::function<std::string()> clipboard_get;
    std::function<bool(const std::string&)> clipboard_set;
    HostLookup::Resolver host_resolver;
    HostLookup::Launcher host_launcher;                     // (the tests': a worker thread that cannot be started)
    /// How long the menu's Join or Host may take, from the press of the button to the first message of the room (the name lookup, the connection, the server's welcome), before it
    /// gives up with "The server ... did not answer". A test hook (the player's limit is 20 s): a hung lookup or a server that never answers must end in a message, not in a hang.
    uint32_t menu_connect_timeout_ms{20000};
    /// A test hook: the random bits of the code of a hosted room (32 bits per call; empty: the system's entropy). A test that makes the server hold the very code that the menu is about to
    /// make (the one-in-900-million collision) needs it to be known.
    std::function<uint32_t()> room_code_random;
    /// What is wrong with the command line (parse_arguments cannot fail any other way: a --bot or a --server that does not parse). init() refuses to start with it.
    std::string startup_error;
};

/**
 * @brief Master application lifecycle coordinator handling loop, events, sim, and audio.
 */
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// The command line: --map, --seed, --player / -pnum: (or -pnum=), --name, -N<team><name> / --team-name, --host [port], --join host[:port], --join-url URL, --port, --loopback,
    /// --headless, --fullscreen, --screenshot, ... (docs: README, Command-Line Options)
    static ApplicationConfig parse_arguments(int argc, char* argv[]);
    bool init(int argc, char* argv[]);
    bool init(const ApplicationConfig& config);
    int run();
    void run_frame();
    void run_frame_with_delta(float delta_time);
    void shutdown();

    /// What the original does when the program ends with a match screen (FUN_01010210 -> FUN_010122d4, docs 5.56): the chat log is written to a text file,
    /// "date @ time", a blank line and a line "header body" per entry. `shutdown` writes `chat.txt` into the per-user application folder (the original: the current
    /// directory) unless the run is headless, the web build, or no match was started. False when the file cannot be written.
    bool write_chat_transcript(const std::string& path) const;
    /// "mm/dd/yy @ hh:mm:ss" of a time, as the original's `_strdate` and `_strtime` give it
    static std::string transcript_stamp(std::time_t time);

    bool is_running() const noexcept { return is_running_; }
    void quit();


    bool is_tile_grid_visible() const noexcept { return show_tile_grid_; }

    float get_current_fps() const noexcept { return current_fps_; }

    AppState state() const noexcept { return state_; }
    /// The desktop start menu: the model (the tests drive it with keys and the mouse like the window does), whether this run has one, and the window's title (a room's code is in it
    /// from the moment the player is in the room until the player is back at the menu)
    StartMenu& start_menu() noexcept { return start_menu_; }
    bool start_menu_enabled() const noexcept { return menu_enabled_; }
    /// The reason that a lost match gave ("The connection to the other players was lost."); a network game that the menu led to returns to the menu with it and leaves none behind
    const std::string& net_notice() const noexcept { return net_notice_; }
    const std::string& window_title() const noexcept { return window_title_; }
    /// The keys, the text and the mouse of the start menu (what the event loop does with an event while the menu is up; public for the tests)
    void handle_menu_event(const SDL_Event& event);
    bool start_game(const std::string& map_path);
    void return_to_map_select();
    MapSelectScreen& map_select() noexcept { return map_select_; }
    uint8_t local_player_id() const noexcept { return local_player_id_; }
    void set_local_player(uint8_t team_id);

    ants::sim::SimulationEngine& sim() noexcept { return sim_; }
    Renderer& renderer() noexcept { return *renderer_; }
    /// The geometry of the picture that the HUD, the renderer, the edge scroll and the pointer work in (screen_layout.hpp)
    const ScreenLayout& layout() const noexcept { return layout_; }
    /// The aspect the game runs in (the command line's, else the settings', else the platform's default: 16:9 on a desktop), the canvas that the window shows (SDL's logical size) and
    /// where the picture on screen sits in it (the pointer's coordinates are the picture's, the canvas's bars around it count as its nearest edge pixel): a match is the picture of
    /// `layout()`, the whole canvas of its aspect, and so is the setup screen / room when the canvas is 960 x 540 (its own wide version); every other screen (the loading screen,
    /// the quick help, the results, and the setup screen of any other canvas) is the original's own 640 x 480 page, centred in the canvas
    Aspect aspect() const noexcept { return aspect_; }
    CanvasLayout canvas() const noexcept { return renderer_ ? CanvasLayout{renderer_->canvas_w(), renderer_->canvas_h()} : CanvasLayout::of(aspect_); }
    const LayoutRect& picture() const noexcept { return picture_; }
    /// Alt+Enter (native builds): the window leaves fullscreen or enters it (SDL's desktop fullscreen, the same as --fullscreen); true when it is fullscreen afterwards. The web
    /// build has the page's own button and never does; a macOS fullscreen Space (the green button) is left with the operating system's own controls.
    bool toggle_fullscreen();
    /// Gives every consumer a layout: the renderer's view and camera, the HUD's rectangles, and (through `layout()`) the edge scroll, the pointer's limits, the sound listener,
    /// the network overlay and the start view. The original's picture until it is told otherwise; the canvas (`renderer().set_canvas_size`) is a separate choice.
    void set_layout(const ScreenLayout& layout);
    HUD& hud() noexcept { return hud_; }
    ScorecardModal& scorecard() noexcept { return scorecard_; }
    MidiPlayer& midi_player() noexcept { return midi_player_; }
    AudioMixer& audio_mixer() noexcept { return audio_mixer_; }
    const ants::assets::AssetArchive& assets() const noexcept { return assets_; }

    /// The bots of the running game (nullptr without --bot, and on a guest's machine: only the machine that owns a bot runs it)
    const ai::BotController* bots() const noexcept { return bots_.get(); }

    /// The network of a room or a match (nullptr unless started with --host / --join)
    net::NetGame* net() noexcept { return net_.get(); }
    /// True while a room or a network match exists
    bool network_active() const noexcept { return net_ && net_->active(); }
    /// Advances the network by `dt` seconds of game time and handles what it reports (run once per frame; the tests call it directly). `gap_seconds` is real time that
    /// `dt` did not count (a hidden page that was not woken for a while, see background_run): once the connection has been read, a host that said nothing has been silent
    /// for that time as well (NetGame::note_gap).
    void pump_network(float dt, double gap_seconds = 0.0);

    /// A page that is not drawn (a hidden or minimised browser tab, the web build) runs no frames, and the browser slows its timers: but the page's WebSocket events still
    /// arrive. So while the page is hidden a ROOM or a MATCH of the network is driven by those events instead of by the frame loop: every message of the game server wakes
    /// the game (NetGame::set_on_wake), and `background_pump` does what a frame does for the network (the network clock, the session and the lock-step runner with every
    /// tick that is due, the acknowledgements and hashes that go back, the room and start events) and nothing that is drawn or heard: no frame, no sound effect (the
    /// events of the ticks are drained and dropped, not saved up), and no music: a match that begins, ends or is lost in a hidden page leaves its music for when the page is
    /// shown (the piece that plays goes on meanwhile). Without it a hidden seat stopped executing and acknowledging turns, and the server dropped it.
    /// A local game just stands still while the page is hidden, as the native game does when minimised.
    /// The match has ONE driver at a time. `page_hidden` (the browser's visibilitychange, which the web build registers itself; the tests call set_page_hidden) says
    /// whether the wake-ups MAY drive it: a shown page is driven by its frames only. A hidden page is driven by the wake-ups, unless the frame loop keeps up with the turns
    /// by itself (a browser that keeps drawing a page that it calls hidden, an embedder with background throttling off: a frame at least once per turn, kFrameAliveSeconds):
    /// then the frames drive it, with picture and sound, as they always did, and the wake-ups stand down. A frame loop that is slower than the turns (a hidden page that
    /// still gets a frame a few times a second) cannot keep up alone: the wake-ups drive between its frames. A step never starts inside another one, and a frame and a
    /// wake-up share one timer, so that the time between two of them is counted once.
    void set_page_hidden(bool hidden);
    bool page_hidden() const noexcept { return page_hidden_; }
    /// True while the wake-ups MAY drive the match: the page is hidden and a room or a match exists (they stand down while frames come, see above)
    bool background_driven() const noexcept { return page_hidden_ && network_active(); }
    /// One wake-up with the real time that has passed since the clocks were last advanced (by a frame or a wake-up). False, and nothing done, unless the wake-ups drive
    /// the match now and no step is running. The web build asks the browser first whether the page is hidden (so a visibilitychange that was missed cannot leave the
    /// match without a driver). The page's timer for a quiet server calls it too (ants_background_pump).
    bool background_pump();
    /// The same with the time given (seconds; the tests call it)
    bool background_pump_after(float dt);
    /// What a wake-up does with the time that has passed since the clocks last moved (`elapsed`, real time; a clock that went backwards counts for nothing). The network's
    /// clock, and with it the lock-step runner, gets at most kMaxWakeSeconds of it: a page that was asleep must not be paid back more than a second at once. The rest is a
    /// gap that the host's silence still counts (its link is read first: what waited is not silence): a hidden page that the browser wakes once a minute notices a server
    /// that stopped answering at the first wake-up after ten seconds of real time, not after ten minutes.
    static constexpr double kMaxWakeSeconds = 1.0;
    /// A hidden page whose frame loop delivers a frame at least once per turn keeps the match going by itself, with picture and sound: the wake-ups stand down for this long
    /// after each frame (a turn: net::kTurnMs). Later than that the frames are too few to carry the turns and the wake-ups step between them.
    static constexpr double kFrameAliveSeconds = static_cast<double>(net::kTurnMs) / 1000.0;
    /// The line for the browser's console when a hidden page is shown again ("" when there is nothing worth a line: a period of less than a second, no wake-up that stepped,
    /// or another line less than ten seconds ago). `ticks` is what the match advanced in the period; none: the period was spent in a room (or after the match).
    static std::string hidden_period_line(double seconds, uint64_t ticks, uint32_t wakes, double seconds_since_last_line);
    /// The last line that hidden_period_line gave (the web build prints it)
    const std::string& last_hidden_line() const noexcept { return hidden_line_; }
    /// The clock that frames and wake-ups are timed with is SDL's performance counter; a test gives the application a virtual one (in the counter's units:
    /// SDL_GetPerformanceFrequency per second) so that every rule of the time is checked exactly and without waiting. Nothing: the real clock.
    void set_clock(std::function<uint64_t()> counter) { clock_ = std::move(counter); }
    /// The network's clock in ms: what the frames and the wake-ups have given the session so far
    double net_clock_ms() const noexcept { return net_time_ms_; }
    /// How many wake-ups made a step since the application started
    uint32_t background_pumps() const noexcept { return background_pumps_; }

    /// The quick help at the start (Ants.exe FUN_010145d2(1), keys FUN_010147c2): the START! button is the button class (captured at the press, acts at the release, leaving
    /// cancels); the keys Enter, Esc, C and X (either case) do the same; every other key and click does nothing (`M` would open the More Help dialog: not built).
    /// The loading screen ends: the quick help when the stored option asks for it, else the setup screen (public for the tests: a headless run has no loading screen)
    void finish_loading();
    void note_pointer(int32_t x, int32_t y);                 // the pointer's position, whatever screen is up (the original has one global pointer)
    void quick_help_move(int32_t x, int32_t y);
    void quick_help_press(int32_t x, int32_t y);
    void quick_help_release(int32_t x, int32_t y);
    void quick_help_key(SDL_Keycode sym);
    const ScreenButton& quick_help_start_button() const noexcept { return quick_help_start_; }
    /// A machine that joined a room (a server's room: its first player is the leader, whose START starts the match of everybody) takes the rest of the gesture that closed the quick
    /// help away from the setup screen. START! of the quick help lies under the setup screen's START, so a double click on START! has a second click that would press the new
    /// START. A left press that continues the click is swallowed (the screen never sees it; the release of a press that began on the quick help then finds nothing pressed, as it
    /// always does: the new screen's buttons start fresh): one that SDL counts as part of the sequence (`clicks` above 1), and one that comes within SDL's double-click time
    /// (kDoubleClickMs) of the click that closed the quick help, because SDL gives up the count when the pointer moved more than a pixel between the clicks, which a hand does.
    /// A press after that time that SDL counts as a first click (`clicks` 1) begins a new sequence: it is the screen's, and it ends the rule. (The held key that closed the quick
    /// help is the screen's business: the leader's START ignores a key repeat, see MapSelectScreen::handle_key_down.) The original's own screens, the local game and a LAN host, are
    /// not touched: its window class has no double-click messages, so its second click was a press like any other.
    static constexpr uint32_t kDoubleClickMs = 500;
    bool closing_click_pending() const noexcept { return closing_click_pending_; }

    /// The start menu's panels and the screens around it lie on top of each other (Quit under Host an online match's Host button, Cancel's place on the Join panel's Back, the quick
    /// help's START! under the setup screen's, the loading screen under the first panel). A click that changed the screen has its second click, or the rest of it, for the screen that it
    /// opened, so in a run with a menu that second click is not the new screen's: after a left press or release changed the panel or the screen, a left press that SDL counts as part of the
    /// sequence (`clicks` above 1), or that comes within SDL's double-click time (kDoubleClickMs) of the event that changed it, does nothing but move the hover (the same rule as
    /// closing_click_pending() for the quick help; the first press that is neither begins a new click and ends the rule). A release always belongs to a press that began on the screen
    /// that is up (the buttons of a new panel are fresh: nothing is pressed in them). Public for the tests, which give the time; a run without a menu never has the rule.
    bool swallow_menu_gesture(uint8_t clicks, uint32_t timestamp_ms);
    bool menu_gesture_pending() const noexcept { return menu_gesture_pending_; }

    void handle_key_down(const SDL_KeyboardEvent& key);
    void handle_mouse_motion(const SDL_MouseMotionEvent& motion);
    void handle_mouse_button(const SDL_MouseButtonEvent& button);
    void handle_camera_panning(float dt = 0.020f);
    /// What happens to the window (focus, the pointer entering and leaving, minimising, resizing); public for the tests
    void handle_window_event(const SDL_WindowEvent& we);
    /// The pointer is not over the window: the system cursor is shown (the game's own is not drawn) and the edge of the map does not scroll. Whatever the
    /// pointer does in the window (a motion, a press, a release) ends it.
    bool pointer_outside() const noexcept { return pointer_outside_; }
    /// Where the last frame put the network's ping and delay (none when it drew none: a game of one machine, a screen without the readout); for the tests
    const std::optional<LatencyCornerLayout>& last_latency_layout() const noexcept { return last_latency_layout_; }
    /// Where the window is now (client area, screen coordinates)
    WindowRect window_rect() const;
    void update_simulation(float dt);
    /// The music of the original is one sequencer device (docs 5.24e): the intro plays once, every piece that ends is followed by a random in-game piece, the match
    /// start, the activation of the program and the release of the music slider start one, the deactivation of the program and the end of a match close the device.
    /// (With --audio-focus, several games on one machine, the deactivation HOLDS the piece and the activation continues it: see `set_app_active`.)
    /// `update_music` runs once per frame in every state (the tests call it directly).
    void update_music(float dt);
    /// WM_ACTIVATEAPP (0x100e875): the program loses / gets the input focus
    void set_app_active(bool active);
    /// The results screen (docs 5.49): a match that has ended opens it, in its waiting phase; its clock builds the rows 250 ms later and the machine's one winner or loser
    /// cue plays then. Once per frame in every state (the tests call it directly).
    void update_results(float dt);

    int32_t mouse_screen_x() const noexcept { return mouse_screen_x_; }
    int32_t mouse_screen_y() const noexcept { return mouse_screen_y_; }
    bool mouse_has_moved() const noexcept { return mouse_has_moved_; }
    void render_frame();

private:
    void handle_events();
    void refresh_page_visibility();                       // web build: asks the browser whether the page is hidden (set_page_hidden); a native build has no page, nothing to ask
    bool background_step();                               // background_pump without asking the browser first
    bool wake_may_step();                                 // the wake-ups drive the match now: hidden page, network, no step running, and the frame loop slower than the turns
    void background_run(double elapsed);                  // the step itself: the network for `elapsed` seconds (at most kMaxWakeSeconds for the clock, see there), no sound
    uint64_t now_counter() const { return clock_ ? clock_() : SDL_GetPerformanceCounter(); }   // the clock of frames and wake-ups (set_clock)
    void apply_pending_music();                           // what a hidden page's steps left for the ears: the music of a match that began, ended or was lost meanwhile
    void note_hidden_period();                            // the page is shown again: the console's line about the period
    void play_effect(uint32_t sound_id, uint32_t owner = 0);   // a sound effect that no tick makes: none in a background step
    void play_ui_sound(uint32_t sound_id);
    void release_ui_sounds();
    static constexpr uint32_t kUiPressOwner = 0x80000001u;      // the sound owner of the button that is being pressed (there is only one at a time)

    ApplicationConfig config_{};
    ScreenLayout layout_{ScreenLayout::classic()};       // the picture that the match screen is: the HUD, the renderer's view, the edge scroll, the pointer's limits
    Aspect aspect_{Aspect::Classic4x3};
    LayoutRect picture_{0, 0, ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight};     // where the picture that is on screen sits in the canvas (picture_for_state: the match is the whole canvas, a page of the original's is centred)
    AppState state_{AppState::MapSelect};
    bool is_running_{false};
    bool match_started_{false};                           // a match screen was built (the original writes the chat transcript only then)
    bool is_paused_{false};
    bool show_tile_grid_{false};

    SDL_Window* window_{nullptr};
    std::unique_ptr<Renderer> renderer_;

    ants::assets::AssetArchive assets_;
    ants::assets::LevelData current_level_;
    ants::sim::SimulationEngine sim_;

    MapSelectScreen map_select_;
    ConfigStore config_store_;      // the options that the program remembers; the HUD writes to it (before hud_: the HUD is destroyed first)
    HUD hud_;
    ScorecardModal scorecard_;
    AudioMixer audio_mixer_;
    MidiPlayer midi_player_;

    uint8_t local_player_id_{0};
    uint8_t local_roster_{0x0F};                           // the seats of the local game that was started (all four, or the local player and the bots)
    std::vector<std::unique_ptr<sim::CommandSink>> bot_sinks_;   // where the bots' commands go (declared before bots_: the controller is destroyed first)
    std::unique_ptr<ai::BotController> bots_;
    std::unique_ptr<net::NetGame> net_;
    double net_time_ms_{0.0};
    double start_when_pressed_ms_{-1.0e9};                // --start-when: when the hook last pressed START
    bool page_hidden_{false};                              // the browser's page is hidden (set_page_hidden): a network match belongs to the wake-ups, see background_pump
    bool advancing_{false};                                // a frame or a background step is advancing the clocks and the match: no other one may start inside it
    uint32_t background_pumps_{0};                         // the wake-ups that made a step, in all
    bool background_stepping_{false};                      // inside a background step: it makes no sound and starts no music (pending_music_ keeps what the music should do)
    enum class PendingMusic : uint8_t { None, InGame, Intro, Closed };
    PendingMusic pending_music_{PendingMusic::None};       // what a background step wanted of the music, done by the next frame or when the page is shown (the last wish counts)
    std::function<uint64_t()> clock_;                      // set_clock: a virtual clock for the tests (empty: SDL's performance counter)
    uint64_t last_frame_run_{0};                           // when a frame last ran (performance counter; 0: none since the page was hidden): the wake-ups stand down while frames come
    uint64_t hidden_since_{0};                             // this hidden period: when it began (performance counter), the wake-ups that stepped in it and the ticks that ran in it
    uint32_t hidden_wakes_{0};
    uint64_t hidden_ticks_{0};
    uint64_t hidden_line_at_{0};                           // when the console last got a line about a hidden period (performance counter; 0: never)
    std::string hidden_line_;                              // (the web build prints it when the page is shown again; see hidden_period_line)
    bool match_over_handled_{false};
    std::string net_notice_;
    std::string player_name_;
    std::string local_player_name_;                       // the name of a local game (the system user, --name): what a single-player game after a network game shows again
    std::string window_title_;                            // what the window's title says now
    StartMenu start_menu_;                                // the desktop start menu: its model (always there; it is part of a run only when menu_enabled_)
    HostLookup host_lookup_;                              // the menu's name lookup, on a worker thread
    bool menu_enabled_{false};                            // this run shows the start menu: a network game that ends brings the player back to it
    struct MenuConnection {                               // the menu's join or host attempt
        enum class Stage : uint8_t { None, Lookup, Joining, InRoom };
        Stage stage{Stage::None};
        bool hosting{false};
        std::string room;                                 // the code
        std::string name;
        std::string label;                                // the server as the player reads it
        ServerAddress server;
        int players{4};                                   // hosting: the seats of the room
        int map{0};                                       // hosting: the index of the map that the player chose (menu_map)
        double elapsed_ms{0.0};                           // how long the attempt has taken (its time limit, menu_connect_timeout_ms)
    } menu_conn_;
    int32_t mouse_screen_x_{320};
    int32_t mouse_screen_y_{240};
    bool mouse_has_moved_{false};
    bool pointer_outside_{false};                          // the pointer is not over the window (the window has been left, see handle_window_event)

    // The match set-up shared by the local game and the network game
    bool load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog);   // level, simulation, renderer (no HUD, no sound)
    void enter_match();                                   // music, start sound, camera, HUD reset, "Get ready", state Playing
    void post_tick();                                     // what every simulation tick shows: HUD, events, audio, the end of the match
    void check_match_over();                              // the match is over and not yet shown: the results screen opens (waiting), the music closes
    void confirm_quit();                                  // the quit dialog's Yes (FUN_0101453f): the quit ends the match while one other side is left, else the player leaves

    // The desktop start menu (application_menu.cpp)
    void init_start_menu();                               // the menu's settings, server, clipboard and callbacks (when this run has a menu)
    void enter_start_menu(const std::string& notice = std::string());   // state StartMenu, the first panel (with a line of notice when there is one)
    void update_start_menu(float dt);                     // the menu's clock, what it asked for, its connection (once per frame, from pump_network)
    void process_menu_request(const MenuRequest& request);
    void begin_menu_connection(bool hosting, const std::string& room, const std::string& name, int players, int map);
    void pump_menu_connection();                          // the name lookup, the join, the room: what became of them
    void menu_connection_failed(const std::string& message);
    void abort_menu_connection();                         // Cancel, Back from the room, a failure: nothing of the connection stays
    void menu_connected();                                // the player is in the server's room
    void menu_start_single(const std::vector<ai::BotSpec>& bots);
    std::string menu_failure_text() const;                // what a failed join says, in the menu's words
    bool room_has_chosen_map() const;                     // hosting: the room that the server made is on the map that the player chose
    void show_opening_screens();                          // after the menu (or the loading screen of a game without one): the quick help when the option asks for it, else the setup screen
    void return_to_start_menu(const std::string& notice); // a network game is over (left, ended, lost): back to the menu, nothing of it stays
    void leave_game();                                    // Leave of a network game's screens: back to the menu when this run has one, else the program ends as always
    void attach_net();                                    // the tick, chat and HUD hooks of a NetGame that the application owns
    void apply_player_name(const std::string& name);      // the name that the HUD, the chat, the results and the setup screen show for this player
    void set_window_title(const std::string& title);

    // Network play
    void handle_net_events();
    void net_load_match();                                // the host said Start: load the map, initialise the simulation, report
    void net_begin_match();                               // everybody loaded: the match runs on this machine
    void net_end_session(const std::string& notice);      // leave the room / the match and return to the local setup screen
    void net_start_from_setup(const std::string& map_path);
    void net_request_start();                             // START of the leader of a server's room: the request goes to the server; the can't-go cue when there is nobody to play with
    void sync_room_view();
    void render_net_overlay();
    void render_latency_corner(int32_t version_x, int32_t text_y, const CornerPlate& plate);       // "ping NN ms" / "delay NN ms" next to the frame rate, in a room and a match of a network game
    std::optional<LatencyCornerLayout> last_latency_layout_;                                         // where the last frame put the network's readout (none: it drew none)
    void apply_team_names(const std::array<std::string, 4>& names, uint8_t roster);   // simulation texts, HUD labels, results rows

    // Computer players (docs/BOTS.md): a game without --bot never creates any of this
    std::string bot_setup_problem(uint8_t own_seat, bool fog) const;   // "" or why the game may not start with the bots of the command line (ai::check_setup)
    uint8_t bot_roster(uint8_t own_seat) const;                        // the seats that play a local game with bots: the local player's and the bots'
    std::array<std::string, 4> local_team_names() const;               // -N / --team-name, and "Bot (Medium)" for a bot seat that has no name of its own
    void show_setup_notice(const std::string& text);                   // a refusal: stderr and the status line of the local setup screen
    bool start_local_bots(uint32_t match_seed);                        // after the simulation was initialised: the controller, one LocalBotSink per seat
    bool add_bot(const ai::BotSpec& spec, sim::CommandSink& sink, std::string& why);   // seats one bot (the registry's, or the tests' factory's)
    void start_net_bots();                                             // the host of a room: the controller over NetBotSink, for the seats that hold a bot
    void stop_bots();

    bool wide_setup() const;                              // the canvas is the 960 x 540 one that the setup screen's wide version is made for
    LayoutRect picture_for_state() const;                 // where the picture on screen sits in the canvas: the match is the layout's picture, so is the setup screen of a 960 x 540 canvas, every other screen the original's 640 x 480, centred
    void update_picture();                                // the screen changed (a match starts, the results open, the setup screen is back): the picture and the pointer's coordinates follow
    void apply_window_layout();                           // --grid / --cell, --window-pos, --window-size, the aspect's first size (native builds)
    void choose_aspect();                                 // --aspect, else the settings' key `aspect`, else the config's (the platform's default from parse_arguments: 16:9, on a desktop and in the web build)
    void update_mouse_grab();                             // fullscreen (SDL's or a macOS Space): SDL keeps the pointer in the window while it has the focus (native builds)
    bool button_outside_window(const SDL_MouseButtonEvent& button) const;   // the position SDL delivered (before the clamp) lies outside the window, not merely the picture
    void show_start_view();                               // the view at the start of a match: scrolled just far enough to show the square around the hill's anchor tile
    void enter_map_select();                              // the setup screen is created (again): its labels stay empty until its refresh

    void play_next_ingame_music();
    void start_intro_music();
    void close_music();
    void hold_music(bool hold);               // --audio-focus: pause / continue the music with the window's focus
    void play_startup_sound();
    void render_loading_screen();
    void render_quick_help_screen();
    void close_quick_help(bool by_click);                  // the quick help is over (a click on START! or a key): its button is let go of, the setup screen follows
    uint32_t screen_signature() const noexcept;           // which screen and which panel are up (the start menu's gesture rule sees a change of it)
    void begin_menu_gesture(uint32_t at_ms);              // a left press or release changed the screen at this time (SDL's): see swallow_menu_gesture
    bool swallow_the_rest_of_a_click(uint8_t clicks, uint32_t timestamp_ms);   // the setup screen's left press: is it the rest of a click that closed the quick help or left the start menu?
    bool swallow_closing_click(uint8_t clicks, uint32_t timestamp_ms);   // a left press on the setup screen (SDL's count and time of it): is it the rest of the click that closed the quick help?
    bool joined_a_room() const noexcept { return net_ != nullptr && !net_->is_host(); }   // (stable from the start: the room view of the screen follows the first Room message)

    // Intro & Loading state
    uint32_t intro_ticks_{0};
    ScreenButton quick_help_start_{ButtonRect{529, 437, 98, 27}, ButtonRect{528, 438, 97, 24}};   // START!: the pictures qh_start1 / 2 and qh_start3 (the hit test is the rectangle of the picture that shows)
    bool closing_click_pending_{false};                    // the quick help was closed by a click, on a machine that joined a room: the rest of that click sequence is not for the screen
    uint32_t closing_click_ms_{0};                         // ... closed at this time (SDL's ticks)
    bool menu_gesture_pending_{false};                     // a click changed the screen of a run with a menu: the rest of that click sequence is not for the screen that is up now
    uint32_t menu_gesture_ms_{0};                          // ... at this time (SDL's ticks)

    // 20 Hz Discrete Simulation Timing
    uint64_t last_frame_time_{0};
    int headless_frame_count_{0};
    float tick_accumulator_{0.0f};
    float input_accumulator_{0.0f};     // the 50 ms input task (edge scrolling, minimap drag)
    float current_fps_{60.0f};
    int last_music_track_{-1};
    bool music_open_{false};            // the sequencer device is open (the intro or a piece was started and not closed): its end starts the next random piece
    bool music_resume_on_activate_{false};   // the device was open when the program was deactivated (0x5588)
    bool music_held_{false};            // --audio-focus: the window has no focus, its music is held (paused), not closed

    // Smoothed FPS Display and Frametime Sparkline
    static constexpr size_t SPARKLINE_SAMPLES = 36;
    std::array<float, SPARKLINE_SAMPLES> frametime_history_{};
    size_t frametime_index_{0};
    float fps_display_value_{60.0f};
    float fps_time_accumulator_{0.0f};
    int fps_frame_counter_{0};
};

} // namespace ants::app
