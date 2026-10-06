#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
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
#include "ants_net/cue_router.hpp"
#include "ants_net/latency.hpp"
#include "ants_net/netgame.hpp"
#include "ants_replay/recorder.hpp"
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
#include "ants_app/local_teams.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/rejoin_store.hpp"
#include "ants_app/host_lookup.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_app/touch_control.hpp"
#include "ants_app/room_chat.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"
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
    int window_width{1280};  // the classic canvas at 2x: the size that the window is created with; a 16:9 game is given its own first window right after (apply_window_layout)
    int window_height{960};
    bool fullscreen{false};
    /// --aspect 16:9 | 4:3 (the settings key `aspect` when the command line does not say): the shape of the picture. 4:3 is the original's fixed 640 x 480 canvas; 16:9 is a fixed
    /// 960 x 540 canvas, in which the match screen is the wide frame (a 762 x 500 map view, the right panel pinned to the right edge: shell_layout.hpp) and every screen outside a match is
    /// composed for the whole canvas from the original's own art: the setup screen and the room (setup_layout.hpp, with a map preview), the loading screen and the quick help at the start
    /// (page_layout.hpp), the results (results_layout.hpp) and the desktop start menu (start_menu.hpp), all on the wide pages' clay and frame (wide_page.hpp); the options window and the quick help of
    /// a match sit over the map view with the frame around them. SDL scales the canvas into the window by
    /// the largest scale that fits (whole when the window is a multiple of the canvas, else fractional), centred, with bars; a window of an aspect opens at the largest scale in steps of 0.5
    /// of the canvas that fits the display's usable area, at least 1x (--window-size and --grid still win; a game that starts in fullscreen has this size for the way back, Alt+Enter),
    /// and fullscreen is the same canvas filling as much of the monitor as fits. A desktop game that is
    /// started from the command line (parse_arguments) is 16:9 unless it says otherwise (kPlatformDefaultAspect: 16:9 in the web build too, whose page passes `--aspect`); a config that is made
    /// by hand keeps the 4:3 that it is built with. `aspect_given` is true when the command line said it (the settings key then does not count).
    Aspect aspect{Aspect::Classic4x3};
    bool aspect_given{false};
    /// --zoom N, 0.05 .. 2 (the settings key `zoom` when the command line does not say): the zoom of the map view that a match starts with, the nearest level that its map offers (view_zoom.hpp;
    /// the mouse wheel changes it and the new level is remembered). 1 is the original's picture. A match of the network has the same levels as a local game. `zoom_given` is true when the command
    /// line said it (the settings key then does not count).
    float zoom{zoom::kNormal};
    bool zoom_given{false};
    /// --prediction on | off (--no-prediction is --prediction off; the settings key `prediction` when the command line does not say): whether a match of the network shows the player's own
    /// orders at once (net::Prediction, docs/NETWORK_PORT.md). OFF by default (opt-in); a game of one machine never predicts. `prediction_given`: the command line said it (the settings key then does not count).
    bool prediction{false};
    bool prediction_given{false};
    bool headless{false};
    std::string chd_path{"Original-Ants/ants.chd"};
    std::string maps_dir{"Original-Ants/Maps"};     // the folder whose `*.lvl` files are the map list (the original searches its Maps folder)
    std::string default_map_path;                   // --map: the map of a game that starts without the setup screen; empty: the map that the setup screen highlights (TREASURE.LVL, else the first of the list)
    std::string midi_path{"Original-Ants/INTRO.MID"};
    /// --settings: where the options are remembered (a file; the original keeps them in the registry). Empty: the per-user application folder (the browser's local
    /// storage in the web build), and nowhere in a headless run, which is what the tests use.
    std::string settings_path;
    uint32_t random_seed{1337};
    bool start_in_map_select{true};
    /// --play: the setup screen's own START at its first visit. A game that was chosen elsewhere (the web page's Play button, with a map and opponents) goes through the loading screen and the
    /// quick help as every game does, and then starts `default_map_path` (--map; else the map that the setup screen highlights) the way the setup screen's START does: the "Get ready" dialog,
    /// the start sound, the music, --bot's seats (--map alone is the direct start of the tests and the screenshots, which has none of that). Only that first visit is skipped (the way back to the
    /// setup screen after a match is the setup screen), and only for a game of this machine without the start menu: --host, --join, --join-url and the menu leave it alone. parse_arguments
    /// gives a game that names its map with --map the setup screen's way (start_in_map_select).
    bool play_at_once{false};
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
    /// --start-when N (1 .. 4; 0 = off, the default): when this game leads a server's room it presses START itself (the S key of the setup screen, the same path as a click) once N
    /// players are in the room, again every second until the match starts. The web front page's card gives it to the game of a match (1 + the card's Friend rows: the START of the
    /// card was the click) and a headless test client has nobody to click at all. 1 is for a leader with --fill-bots (one person is enough then; without a fill the START of one
    /// person is the can't-go cue, every second). A game started from the menus never uses it.
    uint8_t net_start_when{0};
    /// --fill-bots none|easy|medium|hard, or four of them joined by commas for the seats 0 - 3 (protocol 11; a level for each seat since 13): the bots that this player's START seats in the empty
    /// seats of its room when it can start one: the leader of a server's room (the request goes to the server, which seats them and runs them) and the host of a room on the local network
    /// (this machine runs them, as for --bot). None, the default, is the START of every earlier version. A game that is not a room ignores it. The start menu's Host panel ("Empty seats at START") sets it for the room that it makes (a Join sets none), `--start-menu --fill-bots hard` starts that panel at Hard, and the web page's address (`?fill=`, web/shell.html) gives it to the game of the room's leader.
    net::FillPlan fill_bots;
    /// A test hook (--say TEXT): this client says the line once in the waiting room, as soon as two players are in it (so that somebody hears it). The lines that arrive in the room go to the
    /// log of the program (stderr, "Room chat: Name: text") and to NetGame::take_pregame_chat().
    std::string net_say;
    /// Where the window goes (native builds): an explicit position and size (--window-pos X,Y, --window-size WxH or W,H), or a cell of a grid over the display's usable
    /// area (--grid CxR --cell N: the start scripts lay four games out as a 2 x 2 grid, each window the largest rectangle of the canvas's shape, 16:9 by default, of its cell); --display N picks the
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
    /// --teams ffa | A+B (docs/BOTS.md, "Alliances"): ffa (the default) is free for all; A+B (two seats, 0 - 3) makes them a team, the two others too when both play. Made at the match start with the
    /// original's commands: in a game on this computer by Application::form_start_teams (a pair that cannot be made starts the game without teams and says why), in a room (protocol 13) by every machine
    /// from the Start message, which the room's START puts them into: this player's START (the leader of a server's room, the host of a room on the local network) carries the choice.
    LocalTeams teams;
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
/// The text of a switch: on / off, yes / no, true / false, 1 / 0, in any case (the command line's --prediction and the settings key `prediction`). False, and `out` untouched, for any other text.
bool parse_switch(const std::string& text, bool& out);

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
    /// A match is being played right now: the match screen is up and its results are not (the browser page asks before it restarts the game for another picture, see ants_match_running)
    bool match_running() const noexcept { return state_ == AppState::Playing && !scorecard_.is_open(); }
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
    /// The replay of the match that ended (or was left) last (docs/REPLAYS.md): the bytes of a .antsrep file, empty until a match has ended that could be recorded. Every match is recorded as it is played; the
    /// web page offers the file as a download (its "Download replay" button), the desktop game writes it into the `replays` folder beside its settings file, and the file is kept here too.
    const std::vector<uint8_t>& last_replay() const noexcept { return last_replay_; }
    /// What that file is called when it is saved or downloaded ("ants-TREASURE-20261006-143209.antsrep")
    const std::string& last_replay_name() const noexcept { return last_replay_name_; }
    /// The recording of the match that is being played (null: none is made, or the match has ended)
    const replay::Recorder* recorder() const noexcept { return recorder_.get(); }
    /// The engine that the match screen SHOWS and that the HUD asks (the picture, the minimap, the panel, the selection, the pointer's cursor, what a click picks): `sim()`, the confirmed
    /// engine, except in a match of the network that predicts the player's own orders (net::Prediction, docs/NETWORK_PORT.md "Prediction of one's own orders"), where it is the predicted
    /// engine: the player's orders show at once and everything else is the confirmed state a few ticks ahead. The cues, the news, the scores, the end of the match and the hashes stay the
    /// confirmed engine's. One call may rebuild a predicted engine that a turn has shown to be wrong, so a frame asks once.
    ants::sim::SimulationEngine& view_sim();
    Renderer& renderer() noexcept { return *renderer_; }
    /// The geometry of the picture that the HUD, the renderer, the edge scroll and the pointer work in (screen_layout.hpp)
    const ScreenLayout& layout() const noexcept { return layout_; }
    /// The aspect the game runs in (the command line's, else the settings', else the platform's default: 16:9 on a desktop), the canvas that the window shows (SDL's logical size) and
    /// where the picture on screen sits in it (the pointer's coordinates are the picture's, the canvas's bars around it count as its nearest edge pixel): a match is the picture of
    /// `layout()`, the whole canvas of its aspect, and so is every other screen: in the 960 x 540 canvas each is composed for it (its own wide version), in the 640 x 480 canvas each is
    /// the original's own page
    Aspect aspect() const noexcept { return aspect_; }
    CanvasLayout canvas() const noexcept { return renderer_ ? CanvasLayout{renderer_->canvas_w(), renderer_->canvas_h()} : CanvasLayout::of(aspect_); }
    const LayoutRect& picture() const noexcept { return picture_; }
    /// THE ZOOM OF THE MAP VIEW (view_zoom.hpp, Renderer::zoomed): a level from the map's limit up to 2, four to a doubling (2, 1.68, 1.41, 1.19, 1 (the original's picture), 0.84, 0.71, 0.59, 0.5 ...),
    /// the last zoom-out level being the exact limit of the map: the view never shows anything outside it (the map's width or its height just fills the view). The mouse wheel up zooms in, down zooms
    /// out, one level a notch, towards the pointer (the world point under it stays under it as far as the map's edges allow); the middle button goes back to 1. They act only over the map view, and
    /// not while a dialog or a page is open (options, quick help, quit, alliance, "get ready"), a rubber band or a button holds the mouse, or the results are up (`view_zoom_allowed`). The level is
    /// remembered in the settings (key `zoom`). THE API THAT OTHER INPUT USES (a touch screen's pinch calls it): `zoom_levels()` says what is offered now, `set_zoom(level, anchor)` goes to the
    /// nearest of them (the anchor is a point of the picture, the pointer's own coordinates: the screen pixel that keeps its world point), `step_zoom(direction, anchor)` goes one level in or out.
    /// FAIRNESS: the zoom is the player's own view and nothing else (the simulation, the network, the bots and every state hash never see it), and every kind of match has the same levels for the
    /// same picture, a match of the network included (up to v0.2.0 it had no zoom-out). With Fog of War on every level draws exactly what the zoom 1 draws (one world pass, the same culling and
    /// gates; the minimap respects the fog too), so the zoom-out adds breadth and no new kind of information: the types and states of the ants, bombs, effects and hit point digits that are visible,
    /// over a wider area at once (positions were already on the minimap). THE PICTURE'S SHAPE decides the limit: on TREASURE a player in the 4:3 picture can zoom out to the whole map (0.230), one in
    /// the 16:9 picture to two thirds of it (0.397; at the zoom 1 that picture already shows about twice the area), and every player can choose either shape.
    float zoom() const noexcept { return renderer_ ? renderer_->camera().zoom : zoom::kNormal; }
    /// What the renderer allows now: while it cannot make the offscreen target of a zoom (Renderer::world_target_failed) only the zoom 1 can be drawn, and only that level is offered.
    /// enforce_zoom_limits() holds the camera to this and to the map's limit at every frame (a camera that is outside it, by whatever way, is taken to the nearest level that is allowed, anchored at
    /// the view's centre, and what the player chose last stays remembered).
    zoom::Limits zoom_limits() const noexcept;
    /// What decides the levels now: the map view, the map and what the renderer's texture can hold (view_zoom.hpp Fit)
    zoom::Fit zoom_fit() const noexcept;
    std::vector<float> zoom_levels() const;
    bool set_zoom(float level, int32_t anchor_x, int32_t anchor_y);
    bool step_zoom(int direction, int32_t anchor_x, int32_t anchor_y);
    /// Is the pointer position one where the wheel and the middle button may zoom now? (A match is on, over the map view, no dialog, no captured press, no results screen.)
    bool view_zoom_allowed(int32_t x, int32_t y) const;
    /// The wheel (SDL_MOUSEWHEEL): the precise deltas add up to whole steps (zoom::WheelAccumulator), one step is one level; public for the tests
    void handle_mouse_wheel(const SDL_MouseWheelEvent& wheel);
    /// The level that the player chose last (what the next match starts with: the nearest level that its map offers); the match's own level is `zoom()`
    float remembered_zoom() const noexcept { return zoom_wanted_; }

    /// TOUCH CONTROLS (touch_control.hpp): a touch screen's fingers (SDL_FINGERDOWN, SDL_FINGERMOTION, SDL_FINGERUP: SDL's own touch-to-mouse emulation is switched off, so that nothing reaches the game
    /// twice) go to the model, which says what to do: the same mouse events that SDL's emulation made for the first finger (a tap, a drag), the right button for a hold, and for two fingers on the map view a pan
    /// (`pan_view`) and a zoom (`set_zoom`). Nothing here is a command that a mouse cannot give, and the simulation never sees any of it. Only the touch screens (SDL_TOUCH_DEVICE_DIRECT) count: a trackpad's
    /// touches are not positions on the picture. The model and its clock are public for the tests; the clock is SDL's millisecond ticks (the events' own), which a test replaces.
    const TouchControl& touch() const noexcept { return touch_; }
    void set_touch_clock(std::function<uint32_t()> clock) { touch_clock_ = std::move(clock); }
    uint32_t touch_now() const { return touch_clock_ ? touch_clock_() : SDL_GetTicks(); }
    /// The size of a finger's wobble that is still a tap or a hold, in picture pixels: 8 CSS pixels of the game box (touch::slop_pixels), whatever the window or the page's box makes of the picture
    double touch_slop() const;
    /// Scrolls the map view by (dx, dy) screen pixels of the picture, the way fingers that drag the world do: the world follows them, so the view moves the opposite way, by the same distance on the screen
    /// at every zoom (as the minimap and the edge scroll move it), held inside the map. True when the view moved.
    bool pan_view(int32_t dx, int32_t dy);
    /// The browser took the touch away, the window lost the focus, the page was hidden: no finger is tracked any more and a press that a finger held ends with no act (the next frame hands it to the HUD)
    void cancel_touch();
    /// The page's touchcancel (a call that comes from outside the event loop): the same, behind the events that SDL already holds. A finger that went down in the same frame begins and is
    /// cancelled in order, and the lift that SDL makes of a cancel finds it unknown (done at once, as cancel_touch(), when SDL has no event to give). Public for the web build's export and the tests.
    void cancel_touch_queued();
    /// Is a touch device's finger one of the model's? Not a platform's own touch made from the mouse, and not a trackpad's (a touch screen counts, and so does a device that SDL does not know: a test's)
    static bool counts_as_finger(SDL_TouchID device, SDL_TouchDeviceType kind) noexcept;
    /// How many holds have fired their feedback (the buzz of the web build): for the tests
    uint32_t touch_feedbacks() const noexcept { return touch_feedbacks_; }

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
    /// The bots that this player's START seats in the empty seats of its room (protocol 11; a level for each seat since 13; --fill-bots at the start): the start menu's Host panel calls it when the
    /// player hosts (and a Join with none), and anything that lets the player choose later can call it any time before START. It takes effect for the next START of a leader (the request carries it)
    /// and of a LAN host (net_start_from_setup). One level is every seat's.
    void set_fill_bots(const net::FillPlan& plan) {
        config_.fill_bots = plan;
        if (net_) net_->set_fill_bots(plan);
    }
    const net::FillPlan& fill_bots() const noexcept { return config_.fill_bots; }
    /// The teams that this player's START asks for (protocol 13; --teams at the start), as set_fill_bots: any time before START
    void set_start_teams(const LocalTeams& teams) {
        config_.teams = teams;
        if (net_) net_->set_start_teams(teams);
    }
    const LocalTeams& start_teams() const noexcept { return config_.teams; }

    // ---- the setup screen of a room: keys, text and clicks as the event loop gets them (public so that the tests can drive them) ----
    /// A key on the setup screen. On the screen of a ROOM (a network game in its waiting room or while the map loads) T opens the chat input (RoomChatInput); while the input is open every key
    /// goes to it and the screen's own keys (S, Q, X, Enter, the arrows) do nothing; Enter sends the line to the room and closes the input, Esc closes it. Closed, the key is the screen's
    /// (MapSelectScreen::handle_key_down). A local game's screen and a room whose connection is gone have no chat: T does nothing there.
    void room_key_down(SDL_Keycode key, uint16_t modifiers, bool repeat);
    /// Typed text (SDL_TEXTINPUT): goes into the chat input while it is open, else nowhere
    void room_text_input(const std::string& text);
    /// A left press on the setup screen: a click on the status line of a room opens the chat input (true: the click is that and nothing else); on the 16:9 setup screen, which has a chat box
    /// (room_chat_box), the places that open it are the box's INPUT box and its lines box, and the status line does not; while the input is open ANY left press closes
    /// it, sends nothing and is no click on the screen (true: a touch screen has no Esc, and a click on START must not start a match through an input that is open); any other press is the
    /// screen's
    bool room_mouse_down(int32_t x, int32_t y, uint8_t button);
    /// A left release on the setup screen: true when it belongs to a press that room_mouse_down took (the buttons are let go of, nothing fires); false: the screen's
    bool room_mouse_up(int32_t x, int32_t y, uint8_t button);
    const RoomChatInput& room_chat() const noexcept { return room_chat_; }
    /// True when a chat line can be said from the setup screen now: a seated player of a room that is waiting or loading
    bool room_chat_available() const noexcept;
    /// The chat box of the 16:9 setup screen (SetupLayout::chat of the Online variant for the host of a room and the leader of a server's room, of the Guest variant for everybody else in a
    /// room): where its lines, its input box and its label stand. nullptr when this screen has none: the classic 640 x 480 setup screen (its status line carries the chat), a game on
    /// this machine, no room. While there is a box the status line shows the room's prompt only (the box shows the lines and the typed text; NetGame::set_chat_status_mirror is off).
    const SetupChatLayout* room_chat_box() const noexcept;

    /// Where the keys of this player's seats are kept (rejoin_store.hpp): a file beside the settings file, the browser's local storage, or memory alone in a headless run without a settings file.
    /// The application writes a key when a server's room hands it out and lets go of it when it is of no more use (NetGame::set_on_key / set_on_forget_key); a join that names a room, and the
    /// start menu's "Rejoin your match", look a key up here. Public for the tests.
    RejoinStore* rejoin_store() noexcept { return rejoin_store_.get(); }
    /// The network of a room or a match (nullptr unless started with --host / --join)
    net::NetGame* net() noexcept { return net_.get(); }
    /// True while a room or a network match exists
    bool network_active() const noexcept { return net_ && net_->active(); }
    /// Who plays which cue of a network match that predicts (net::CueRouter): the own ants' own actions from the predicted engine, in step with the picture, everything else from the confirmed
    /// engine. Public for the tests (what was played from where, what was dropped)
    const net::CueRouter& cue_router() const noexcept { return cue_router_; }
    /// Advances the network by `dt` seconds of game time and handles what it reports (run once per frame; the tests call it directly). `gap_seconds` is real time that
    /// `dt` did not count (a hidden page that was not woken for a while, see background_run): once the connection has been read, a host that said nothing has been silent
    /// for that time as well (NetGame::note_gap).
    void pump_network(float dt, double gap_seconds = 0.0);
    /// What the player feels of an order while the prediction is on, from the frame that took it to the end of the frame that shows it (net::FeltDelayMeter); public for the tests
    const net::FeltDelayMeter& felt_delay() const noexcept { return felt_delay_; }
    /// Whether the matches of the network that this application makes predict the player's own orders (--prediction, the settings key `prediction`)
    bool prediction_wanted() const noexcept { return prediction_wanted_; }
    /// The number that the corner's "delay" shows now (nothing: a dash): what the player feels of an order while the prediction is on, the network's delay otherwise
    std::optional<uint32_t> corner_delay_ms() const;

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
    /// The player leaves a network match ON PURPOSE and the page is about to navigate away (the web build's Menu button and link, the picture selector's "Leave the match?": ants_leave_match): the
    /// server is told (Leave: the seat is dropped at once and the others do not wait for it) and the key is let go of. Does nothing without a network game. A closed tab or a reload never gets here:
    /// its seat is held and its key kept, which is what lets it come back.
    void leave_network_match();
    /// The seat that the room gave this machine is known (after the Welcome, in the waiting room or at the start of a rejoin) or changed: `fn` is told, once for each seat. The web build tells its page, which puts
    /// the seat into its address (a reload of this window takes this seat: window.antsSeatKnown in web/shell.html); the tests give a function of their own. Never called for a game that has no seat (a game of one machine).
    void set_on_seat_known(std::function<void(uint8_t)> fn) { on_seat_known_ = std::move(fn); }
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
    /// Called once for each game on this computer, when its first tick runs (never for a match of the network, whichever machine it is): the web build tells its page, which has the server count
    /// the game for the front page's numbers (web/shell.html antsReportLocalGame, docs/NETWORK_PORT.md "Site statistics"). A native game sets nothing and so reports nothing.
    void set_on_local_match_started(std::function<void()> hook) { on_local_match_started_ = std::move(hook); }
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
    /// The same rule for the click that closes the chat input of a room (room_mouse_down: a click anywhere while the input is open closes it and does nothing else): the player typed, then double
    /// clicked START; the first click closes the input, and the second must not start the match. It holds in every room that has a chat input, the host's included.
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
    /// The line that a match of the network shows at the top of the playfield right now (an empty text: nothing, and always for a game that is not a match of the network): the one that
    /// render_net_overlay draws, from what the network layer reports. Public for the tests (the seconds before the first turn of a match must say nothing)
    NetOverlayLine net_overlay_now() const;
    /// What net_overlay_now is made from: the network layer's state as the overlay's model takes it (the tests add states that no rig reaches, a desync or a lag notice, and see which line wins)
    NetOverlayInput net_overlay_input() const;
    /// The renderer's width of every text of the overlay (what the drawing and the mouse lay it out with): for the tests
    NetOverlayMetrics net_overlay_metrics_for_test(const NetOverlayLine& line) const { return net_overlay_metrics(line); }
    /// The catch-up screen is up instead of the match: the machine is given the match from the server's log (a game started again, a page reloaded, a link lost) and runs it without drawing it; the
    /// loading picture shows "Catching up N%" (catch_up_percent) until Event::Rejoined. Public for the tests.
    bool catch_up_screen_active() const;
    /// A test hook: the catch-up screen is up in a match that has no network layer to stand in that phase (the screens' gates are tested without a server)
    void force_catch_up_screen_for_test(bool on) noexcept { catch_up_forced_ = on; }
    int32_t catch_up_percent() const;
    /// The vote block's buttons where the screen draws them now (net_overlay_layout over the lines of net_overlay_now): `open` is false, and the rectangles are empty, when no vote is on screen.
    /// A click inside one is that choice (F2 keeps waiting, F3 goes on without the seat: NetGame::vote). Public for the tests.
    struct NetVoteButtons {
        bool open{false};
        LayoutRect keep;
        LayoutRect go_on;
    };
    NetVoteButtons net_vote_buttons() const;
    /// Where the window is now (client area, screen coordinates)
    WindowRect window_rect() const;
    /// The window as it was CREATED (client area; x and y are SDL's centred position where the place was not known): its size already has the shape of the picture (16:9 by default, 4:3 with
    /// --aspect 4:3), so that no frame is ever shown in another shape before apply_window_layout runs. For the tests.
    WindowRect window_created_rect() const noexcept { return window_created_; }
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
    void note_orders();                                   // the orders that input gave the predicted engine since the last look begin to be felt (net::FeltDelayMeter)
    struct OrdersNoted {                                  // (the end of an input handler: whatever it ordered is looked at, by whichever way the handler leaves)
        Application& app;
        ~OrdersNoted() { app.note_orders(); }
    };
    uint32_t frame_ms() const { return static_cast<uint32_t>(static_cast<double>(now_counter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency())); }   // ... in milliseconds
    // Touch controls (application_touch.cpp)
    struct TouchEnv final : TouchEnvironment {
        explicit TouchEnv(Application& application) : app(application) {}
        TouchZone zone_at(double x, double y) const override { return app.touch_zone_at(x, y); }
        bool two_fingers_allowed(double x, double y) const override { return app.touch_two_fingers_allowed(x, y); }
        float zoom() const override { return app.zoom(); }
        std::vector<float> zoom_levels() const override { return app.zoom_levels(); }
        bool press_held() const override { return app.touch_press_held(); }
        Application& app;
    };
    TouchZone touch_zone_at(double x, double y) const;
    bool touch_two_fingers_allowed(double x, double y) const;
    bool touch_press_held() const;                         // the press that the first finger made holds a control (a button, the minimap, the chat log; anything on a dialog or another screen)
    static Uint32 touch_cancel_event_type() noexcept;       // the type of the event that carries a queued cancel through SDL's queue (registered once; (Uint32)-1 when SDL has none)
    bool touch_match_screen() const;                       // a match with no dialog or results over it (its catch-up picture too): the HUD's presses are what a cancel ends
    bool touch_view_open() const;                          // a match's map view with nothing over it, and not the catch-up picture: what a pan or a zoom needs, at every move
    void feed_touch(const SDL_TouchFingerEvent& finger);   // a finger's event: the model, and what it says is queued
    void queue_touch(const TouchControl::Actions& actions);
    bool take_touch_event(SDL_Event& event);                // the next queued action as the mouse event that it is; the others (pan, zoom, cancel, feedback) are done here, and false is returned
    void run_touch_action(const TouchAction& action);
    bool touch_late_press_still_fits(const TouchAction& action) const;
    void touch_feedback();                                   // the hold's feedback (a buzz where the browser has one)
    void render_touch_feedback();                            // the ring around a finger that may become a hold and the pulse after one (touch_feedback.hpp), over the match and under the pointer
    void apply_pending_music();                           // what a hidden page's steps left for the ears: the music of a match that began, ended or was lost meanwhile
    void note_hidden_period();                            // the page is shown again: the console's line about the period
    void play_effect(uint32_t sound_id, uint32_t owner = 0);   // a sound effect that no tick makes: none in a background step
    void play_ui_sound(uint32_t sound_id);
    void release_ui_sounds();
    static constexpr uint32_t kUiPressOwner = 0x80000001u;      // the sound owner of the button that is being pressed (there is only one at a time)

    ApplicationConfig config_{};
    ScreenLayout layout_{ScreenLayout::classic()};       // the picture that the match screen is: the HUD, the renderer's view, the edge scroll, the pointer's limits
    Aspect aspect_{Aspect::Classic4x3};
    float zoom_wanted_{zoom::kNormal};                    // the level the player chose last: remembered in the settings (key `zoom`), what the next match starts with
    zoom::WheelAccumulator wheel_;                        // the wheel's precise deltas
    LayoutRect picture_{0, 0, ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight};     // where the picture that is on screen sits in the canvas (picture_for_state: the whole canvas, in every screen)
    AppState state_{AppState::MapSelect};
    bool is_running_{false};
    bool match_started_{false};                           // a match screen was built (the original writes the chat transcript only then)
    bool is_paused_{false};
    bool show_tile_grid_{false};

    SDL_Window* window_{nullptr};
    WindowRect window_created_{};                          // the window as it was created (initial_window_rect)
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
    std::unique_ptr<replay::Recorder> recorder_;          // the recording of the match that is on (replay.hpp; empty: none is made, or the match has ended); the sinks below hold a reference to it
    std::unique_ptr<sim::CommandSink> local_sink_;        // where the HUD's orders of a game on this computer go: into the engine and into the recording (a network game's go to net_)
    std::vector<uint8_t> last_replay_;                    // the file of the match that ended last, and its name (finish_recording)
    std::string last_replay_name_;
    std::vector<std::unique_ptr<sim::CommandSink>> bot_sinks_;   // where the bots' commands go (declared before bots_: the controller is destroyed first)
    std::unique_ptr<ai::BotController> bots_;
    std::unique_ptr<RejoinStore> rejoin_store_;           // the keys of the seats (before net_: its callbacks write here, and leave() writes last)
    std::unique_ptr<net::NetGame> net_;
    net::CueRouter cue_router_;                           // which of the two engines each cue is heard from (a match of the network that predicts); reset with every match
    net::FeltDelayMeter felt_delay_;                      // what the player feels of an order while the prediction is on (the corner's "delay"); reset with every match
    bool prediction_wanted_{false};                       // --prediction, else the settings' key `prediction`, else off: given to every NetGame that this application makes
    uint64_t prediction_cooldowns_reported_{0};           // the cool-downs of this match's prediction that the console has said so of
    uint64_t orders_seen_{0};                             // the orders that the prediction has taken and the felt delay has been told of
    double net_time_ms_{0.0};
    double start_when_pressed_ms_{-1.0e9};                // --start-when: when the hook last pressed START
    std::function<void(uint8_t)> on_seat_known_;           // (set_on_seat_known)
    uint8_t seat_told_{255};                               // the seat that on_seat_known_ was last told (255: none, or the session ended)
    bool page_hidden_{false};                              // the browser's page is hidden (set_page_hidden): a network match belongs to the wake-ups, see background_pump
    bool advancing_{false};                                // a frame or a background step is advancing the clocks and the match: no other one may start inside it
    uint32_t background_pumps_{0};                         // the wake-ups that made a step, in all
    bool background_stepping_{false};                      // inside a background step: it makes no sound and starts no music (pending_music_ keeps what the music should do)
    enum class PendingMusic : uint8_t { None, InGame, Intro, Closed };
    PendingMusic pending_music_{PendingMusic::None};       // what a background step wanted of the music, done by the next frame or when the page is shown (the last wish counts)
    std::function<uint64_t()> clock_;                      // set_clock: a virtual clock for the tests (empty: SDL's performance counter)
    std::function<void()> on_local_match_started_;         // set_on_local_match_started: told at the first tick of a game on this computer
    bool local_match_reported_{false};                     // ... and it was told for this match (enter_match starts the next match with it false)
    uint64_t last_frame_run_{0};                           // when a frame last ran (performance counter; 0: none since the page was hidden): the wake-ups stand down while frames come
    uint64_t hidden_since_{0};                             // this hidden period: when it began (performance counter), the wake-ups that stepped in it and the ticks that ran in it
    uint32_t hidden_wakes_{0};
    uint64_t hidden_ticks_{0};
    uint64_t hidden_line_at_{0};                           // when the console last got a line about a hidden period (performance counter; 0: never)
    std::string hidden_line_;                              // (the web build prints it when the page is shown again; see hidden_period_line)
    bool say_sent_{false};                                // --say: the line has been said
    RoomChatInput room_chat_;                             // the waiting room's chat input (T or a click on the status line opens it)
    RoomChatLog room_chat_log_;                           // what of the room's lines goes to stderr (a budget: a flooded room must not flood the log)
    double room_chat_closed_ms_{-1.0e9};                  // when the input last closed (net_time_ms_): START keys right after it are not for the screen (RoomChatInput::kGuardMs)
    bool room_chat_press_taken_{false};                   // a left press was taken by room_mouse_down while the input was open (or closed it): its release is not the screen's either
    void close_room_chat();                               // closes the input and starts the guard
    std::vector<ai::BotSpec> fill_specs_;                 // the bots that this machine's START seated in the empty seats of a room on the local network (taken out again when the start is cancelled)
    bool match_over_handled_{false};
    double held_since_ms_{-1.0};                          // net_time_ms_ when the match began to be held on this screen (a seat missing, or the countdown after a pause): -1 when it is not (track_net_state)
    std::vector<net::PauseInfo::Seat> prev_missing_;      // the seats that were missing at the last look: who came back when the list empties
    std::string back_name_;                               // the seat that came back, while the countdown that follows its return runs ("" when nobody did)
    uint8_t vote_press_{0};                               // a left press went down on a button of the vote block: 1 keep waiting, 2 go on without the seat (its release is the choice)
    bool catch_up_backlog_{false};                        // this machine has caught up: the sounds that the engine queued meanwhile are dropped with the first live tick (post_tick)
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
        bool rejoin{false};                               // "Rejoin your match": the join shows the key of the seat that this machine had (the room is `room`, the server is `server`, which need not be the menu's own)
        uint8_t seat{255};                                // ... that seat
        net::SeatKey key{};                               // ... that key (kept here until the join is made, never printed)
    } menu_conn_;
    TouchEnv touch_env_{*this};
    TouchControl touch_{touch_env_};                       // what one finger or two do on the match screen
    std::deque<TouchAction> touch_queue_;                  // what the model said and the event loop has not done yet (always the actions of one finger event, and what a cancel adds)
    std::function<uint32_t()> touch_clock_;                // set_touch_clock: a test's clock (empty: SDL's ticks)
    uint32_t touch_feedbacks_{0};
    bool catch_up_forced_{false};                          // force_catch_up_screen_for_test
    int32_t mouse_screen_x_{320};
    int32_t mouse_screen_y_{240};
    bool mouse_has_moved_{false};
    bool pointer_outside_{false};                          // the pointer is not over the window (the window has been left, see handle_window_event)

    // The match set-up shared by the local game and the network game
    bool load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog);   // level, simulation, renderer (no HUD, no sound)
    void enter_match(bool rejoin = false);                // music, start sound, camera, HUD reset, "Get ready", state Playing (a match that this machine rejoins: no start sound, no dialog)
    void post_tick();                                     // what every simulation tick shows: HUD, events, audio, the end of the match
    void check_match_over();                              // the match is over and not yet shown: the results screen opens (waiting), the music closes
    void note_seat();                                     // the seat that the room gave this machine, told once to on_seat_known_
    void confirm_quit();                                  // the quit dialog's Yes (FUN_0101453f): the quit ends the match while one other side is left, else the player leaves

    // The desktop start menu (application_menu.cpp)
    void init_start_menu();                               // the menu's settings, server, clipboard and callbacks (when this run has a menu)
    void enter_start_menu(const std::string& notice = std::string());   // state StartMenu, the first panel (with a line of notice when there is one)
    void update_start_menu(float dt);                     // the menu's clock, what it asked for, its connection (once per frame, from pump_network)
    void process_menu_request(const MenuRequest& request);
    void begin_menu_connection(bool hosting, const std::string& room, const std::string& name, int players, int map, const RejoinEntry* rejoin = nullptr, const ServerAddress* server = nullptr);
    void begin_menu_rejoin(const MenuRequest& request);   // "Rejoin your match": the key of the offer's seat, from the store, joins that room on that server
    std::optional<RejoinOffer> rejoin_offer();            // the newest fresh key that the menu can use (a server of host:port: the browser's entries are for its page), or none
    void pump_menu_connection();                          // the name lookup, the join, the room: what became of them
    void menu_connection_failed(const std::string& message);
    void abort_menu_connection();                         // Cancel, Back from the room, a failure: nothing of the connection stays
    void menu_connected();                                // the player is in the server's room
    void menu_rejoined();                                 // a rejoin is under way: the match begins by itself (the net's events), not through the room's screens
    void menu_start_single(const MenuRequest& request);
    std::string menu_failure_text() const;                // what a failed join says, in the menu's words
    bool room_has_chosen_map() const;                     // hosting: the room that the server made is on the map that the player chose
    void show_opening_screens();                          // after the menu (or the loading screen of a game without one): the quick help when the option asks for it, else the setup screen
    void return_to_start_menu(const std::string& notice); // a network game is over (left, ended, lost): back to the menu, nothing of it stays
    void leave_game();                                    // Leave of a network game's screens: back to the menu when this run has one, else the program ends as always
    void attach_net();                                    // the tick, chat and HUD hooks of a NetGame that the application owns
    void make_rejoin_store();                             // where the keys of the seats are kept (rejoin_store.hpp)
    void hook_rejoin_store();                             // the NetGame's key callbacks write to it (before the join: a Hello that shows a key may be refused at once)
    void apply_player_name(const std::string& name);      // the name that the HUD, the chat, the results and the setup screen show for this player
    void set_window_title(const std::string& title);

    // Network play
    void handle_net_events();
    void net_load_match();                                // the host said Start: load the map, initialise the simulation, report
    void net_begin_match(bool rejoin = false);            // everybody loaded: the match runs on this machine (a rejoin: it was given a match that runs, see NetGame::Event::rejoin)
    void net_end_session(const std::string& notice);      // leave the room / the match and return to the local setup screen
    void net_start_from_setup(const std::string& map_path);
    void net_request_start();                             // START of the leader of a server's room: the request goes to the server; the can't-go cue when there is nobody to play with
    void sync_room_view();
    void render_net_overlay();
    void render_catch_up_screen();                        // the loading screen's picture with "Catching up N%" instead of the match (page_layout.hpp)
    bool handle_way_back_key(const SDL_KeyboardEvent& key);   // Esc on the way back opens the quit dialog; on the catch-up screen no other key does anything
    void track_net_state();                               // every frame of a match of the network: how long the match has been held, who came back (the overlay's seconds and names)
    NetOverlayMetrics net_overlay_metrics(const NetOverlayLine& line) const;    // the renderer's width of every text of the overlay
    bool handle_vote_key(const SDL_KeyboardEvent& key);   // F2 / F3 while a vote is open
    bool handle_vote_mouse(const SDL_MouseButtonEvent& button);   // a click on a button of the vote block
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
    bool form_start_teams();                                           // --teams: the pairs of the plan become teams with the original's commands, before the first tick of a local match; true when teams were made
    void stop_bots();
    const ai::BotSpec* bot_spec_of(uint8_t seat) const;                // the --bot spec of a seat, null for the others
    bool is_bot_seat(uint8_t seat) const;                              // a computer player holds the seat: a local game's specs, in a room the slots that the room calls bots
    std::string declined_team_up_note(const sim::NewsEvent& event) const;   // the line that says why a bot declined the local player's invitation (the HUD's news note); "" for any other event

    // Replays (replay.hpp, docs/REPLAYS.md; application_replay.cpp): every match is recorded as it is played, and the file is made when it ends
    void use_local_sink();                                             // the HUD's orders of a game on this computer go through local_sink_, which the recording sees (they went straight into the engine)
    void begin_local_recording(uint32_t seed, bool teams_made);        // a match on this computer began: the engine is initialised and the teams are made, no command was given yet
    void begin_net_recording();                                        // the Start of a network match was loaded (net_load_match)
    void begin_recording(replay::Header head);                         // the recording of the match that begins (what was not finished of an earlier one is dropped), and the offer of the last file goes
    void finish_recording();                                           // the match is over or left: the file is made, kept in last_replay_ and handed on (offer_replay); a match left at once leaves none
    void offer_replay();                                               // last_replay_: the web page's Download replay button gets it; the desktop game writes it beside its settings
    void withdraw_replay();                                            // a match begins: the page's button goes away

    bool wide_setup() const;                              // the canvas is the 960 x 540 one that the setup screen's wide version is made for
    bool wide_pages() const;                              // ... and the loading screen, the quick help, the results and the start menu (the same canvas: wide_page.hpp)
    LayoutRect picture_for_state() const;                 // where the picture on screen sits in the canvas: the match is the layout's picture (the whole canvas, the original's own 640 x 480 in a 4:3 canvas), and so is every other screen
    void update_picture();                                // the screen changed (a match starts, the results open, the setup screen is back): the picture and the pointer's coordinates follow
    WindowRect initial_window_rect() const;                // what the window is created as: the picture's shape (see the definition)
    void apply_window_layout();                           // --grid / --cell, --window-pos, --window-size, the aspect's first size (native builds)
    void choose_aspect();                                 // --aspect, else the settings' key `aspect`, else the config's (the platform's default from parse_arguments: 16:9, on a desktop and in the web build)
    void choose_zoom();                                   // --zoom, else the settings' key `zoom`, else 1: the level that a match starts with when it is offered
    void choose_prediction();                             // --prediction, else the settings' key `prediction`, else off: whether the matches of the network predict the player's own orders
    void apply_match_zoom();                              // a match starts: the camera takes the remembered level if the kind of match and the map offer it, else 1
    void enforce_zoom_limits();                           // every frame: the camera's zoom inside zoom_limits() and the map's limit (no zoom while the offscreen target cannot be made), one report when that failure begins
    bool zoom_failure_reported_{false};                   // enforce_zoom_limits has said that the target cannot be made (for this failure)
    void update_mouse_grab();                             // fullscreen (SDL's or a macOS Space): SDL keeps the pointer in the window while it has the focus (native builds)
#if defined(__APPLE__)
    bool presentation_hidden_{false};                     // macOS: the game has made the Dock and the menu bar leave the fullscreen Space that the window is in (update_mouse_grab gives them back)
#endif
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
    bool quick_help_wide_{false};                          // ... at the wide page's place (page_layout.hpp: the bottom right corner), not the original's
    bool closing_click_pending_{false};                    // the quick help was closed by a click, on a machine that joined a room, or a click closed the chat input of a room: the rest of that click sequence is not for the screen
    uint32_t closing_click_ms_{0};                         // ... closed at this time (SDL's ticks)
    bool menu_gesture_pending_{false};                     // a click changed the screen of a run with a menu: the rest of that click sequence is not for the screen that is up now
    uint32_t menu_gesture_ms_{0};                          // ... at this time (SDL's ticks)
    bool play_pending_{false};                             // --play: the first visit of the setup screen starts the chosen map at once (enter_map_select), once

    // 20 Hz Discrete Simulation Timing
    uint64_t last_frame_time_{0};
    int headless_frame_count_{0};
    float tick_accumulator_{0.0f};
    double start_dialog_clock_ms_{0.0};  // a match of the network: the real milliseconds of the "Get ready" dialog that are not handed to the HUD yet (its portrait moves in real time while it waits for the first turn)
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
