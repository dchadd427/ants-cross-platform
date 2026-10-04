#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <functional>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_app/pedestal.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/shell_layout.hpp"
#include "ants_app/status_line.hpp"

namespace ants::app {


/**
 * @brief Interactive button state descriptor.
 */
struct UIButton {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};
    bool is_pressed{false};
    bool is_active{false}; // Highlighted when mode is armed
    bool is_hovered{false};

    bool contains(int32_t px, int32_t py) const noexcept {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }
};

/**
 * @brief Master In-Game HUD subsystem for Ants Remake.
 */
class HUD {
public:
    // The CLASSIC picture's numbers (the original's 640x480 screen; the HUD itself works in the layout it was given, see set_layout below).
    // Where the map view is drawn on the virtual 640x480 screen (docs 5.44: the original's view is (16, 21) - (458, 461), see MAP_LEFT .. below)
    static constexpr int32_t PLAYFIELD_X       = ScreenLayout::kClassicViewX;
    static constexpr int32_t PLAYFIELD_Y       = ScreenLayout::kClassicViewY;

    // The map view rectangle of the original (0x1026d6a), half-open: pointers outside it are a plain arrow and pedestals fire on presses there
    static constexpr int32_t MAP_LEFT   = 16;
    static constexpr int32_t MAP_TOP    = 21;
    static constexpr int32_t MAP_RIGHT  = 458;
    static constexpr int32_t MAP_BOTTOM = 461;
    static constexpr bool in_map_rect(int32_t x, int32_t y) noexcept { return x >= MAP_LEFT && x < MAP_RIGHT && y >= MAP_TOP && y < MAP_BOTTOM; }
    /// The minimap (480, 35) - (599, 126)
    static constexpr bool in_minimap_rect(int32_t x, int32_t y) noexcept { return x >= 480 && x < 599 && y >= 35 && y < 126; }


    HUD();
    ~HUD() = default;
    HUD(const HUD&) = delete;                     // the options screen calls back into its HUD
    HUD& operator=(const HUD&) = delete;

    /// The geometry of the picture that the HUD works in (screen_layout.hpp; the original's 640 x 480 until it is told otherwise). Every rectangle that used to be a number of the
    /// original's screen is placed from it by ONE function (apply_layout), which `init` (so every new match) and `set_layout` both run: a layout that was set earlier is not lost
    /// when the next match resets the HUD. The map view, the minimap, the chat log, the top bar's buttons, the pedestals, the [All] / [Team] buttons, the score slots, the status
    /// line and the chat input follow it, and so do the pointer's zones (over_map, over_minimap, in_chat_view). The dialogs (quit, alliance, the "get ready" modal) and the pages (options,
    /// quick help) are pictures of the original's screen with their own numbers: render() draws them moved by layout().modal_offset() / options_offset() / quick_help_offset() (IRenderer::set_origin: over
    /// the map view, the rest of the HUD visible around them) and the pointer handlers take the pointer back to those numbers.
    void set_layout(const ScreenLayout& layout);
    const ScreenLayout& layout() const noexcept { return layout_; }
    /// The map view and the minimap of the layout (a press there goes to the map / the minimap): in_map_rect and in_minimap_rect are the original's, these follow the layout
    bool over_map(int32_t x, int32_t y) const noexcept { return layout_.view().contains(x, y); }
    /// The point is on the map's ground: in the view, and not on the black around a map that is smaller than the view (the camera's origin is negative there). Nothing on the black
    /// acts: no order, no selection, no marker; the pointer over it is the plain arrow.
    bool over_ground(int32_t x, int32_t y, const ViewportCamera& camera, const sim::Grid& grid) const noexcept;
    /// Where the numbers of the open window (the options screen, the quick help) are moved to in this picture: layout().options_offset() / quick_help_offset(), (0, 0) in the
    /// original's own picture. Every pointer handler, the 50 ms poll of update() and the drawing use this one place.
    LayoutPoint open_window_offset() const noexcept { return show_quick_help_ ? layout_.quick_help_offset() : layout_.options_offset(); }
    bool over_minimap(int32_t x, int32_t y) const noexcept { return layout_.minimap().contains(x, y); }
    /// The rectangles that the layout places (for the widescreen work and the tests); the pedestal buttons are the pedestal slots' rectangles too
    const UIButton& help_button() const noexcept { return help_button_; }
    const UIButton& options_button() const noexcept { return options_button_; }
    const UIButton& quit_button() const noexcept { return quit_button_; }
    const UIButton& stop_button() const noexcept { return stop_button_; }
    const UIButton& send_to_button() const noexcept { return send_to_button_; }
    const UIButton& team_button() const noexcept { return team_button_; }
    const UIButton& hatch_button() const noexcept { return hatch_button_; }
    const UIButton& team_up_button() const noexcept { return team_up_button_; }
    const UIButton& quit_yes_button() const noexcept { return yes_button_; }
    const UIButton& quit_no_button() const noexcept { return no_button_; }

    // Test hook: replaces the millisecond clock used by the pedestal transitions
    void set_ticks_function(uint32_t (*fn)()) noexcept { ticks_fn_ = fn; }

    void init(uint8_t local_player_id = 0);
    uint8_t local_player_id() const noexcept { return local_player_id_; }       // the seat that init() was given (the tests ask what a screen was left with)
    void reset();

    // Per-tick / per-frame update
    void update(const sim::WorldState& world, uint32_t delta_ticks);
    void poll_sim_events(sim::SimulationEngine& sim);

    // Rendering pipeline
    void render(IRenderer& renderer, const assets::AssetArchive& assets,
                const sim::WorldState& world, const ViewportCamera& camera);

    /// The frame of the match screen alone (what render() draws first, after the panel's fill): the 14 pieces of the animation uishell, last stored piece first, each placed by its
    /// anchor in the layout's picture and the stretched ones in parts around one repeated line (shell_layout.hpp). In the original's own picture each is one plain copy at its place.
    void render_shell(IRenderer& renderer, const assets::AssetArchive& assets);

    // Mouse & Keyboard Input Dispatch
    bool handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                           sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod = 0);
    bool handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                         sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod = 0);
    bool handle_mouse_motion(int32_t x, int32_t y,
                             sim::SimulationEngine& sim, ViewportCamera& camera);
    /// The keyboard of the original (Ants.exe FUN_0102609a): a dialog takes every key; with chat on the always-active chat box takes printable keys
    /// and Backspace unless Ctrl is held; F1 quick help, F9 - F12 quick messages (fresh presses only, `repeat` = key repeat), Enter sends the text,
    /// Esc deselects; with Ctrl: A select all, H home hill, L hit point digits, N / P next / previous ant, O options, Q quit, S stop. Nothing else.
    bool handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera, uint16_t mod = 0, bool repeat = false);

    // Cursor Evaluation & Ground Click Indicators
    CursorType evaluate_cursor(int32_t screen_x, int32_t screen_y,
                               const sim::WorldState& world,
                               const sim::Grid& grid,
                               const ViewportCamera& camera) const;
    /// The cursor that the last evaluate_cursor chose (the frame's, or the one of the click that was handled last)
    CursorType current_cursor() const noexcept { return current_cursor_; }
    void set_on_spawn_click_marker(std::function<void(int32_t, int32_t)> cb) { on_spawn_click_marker_ = std::move(cb); }
    void spawn_click_marker(int32_t world_x, int32_t world_y) {
        if (on_spawn_click_marker_) on_spawn_click_marker_(world_x, world_y);
    }

    // Chat System & Text Input: the chat box is always active while the option "Participate In Chat" is on (there is no focus)
    const std::string& get_chat_input() const noexcept { return chat_input_; }
    void set_chat_input(const std::string& input) { chat_input_ = input; }
    void handle_text_input(const std::string& text);
    /// Enter (FUN_010103eb with hasAlly): sends the text to the team when the local player has an ally, else to everybody, and clears the box.
    void send_chat_message() { send_chat(is_on_team_); }
    /// The [All] / [Team] buttons and Enter: sends the text of the chat box (FUN_010103eb) and clears it; nothing when it is empty or chat is off.
    void send_chat(bool to_team);
    /// A network match: called with the text and the team flag of every message the player sends (the entry is also added to the own log at once)
    void set_on_chat_send(std::function<void(const std::string&, bool)> fn) { on_chat_send_ = std::move(fn); }
    /// AddLine (Ants.exe 0x10120e9): an entry is a header ("Name:" or "Name (To Teammate):", in the colour of the sender's team, `colour_index` 0 black, 1 blue,
    /// 2 red, 3 green; -1 = the local player's; a 138 px label that does not wrap) and a body of at most 100 characters (a 126 px label that wraps by pixels, 10 px
    /// right of the header, colour (7, 11, 15)). The entries are stacked with one pixel between them and the log follows the newest (docs 5.56).
    void add_chat_entry(const std::string& sender, const std::string& message, bool team_only = false, int colour_index = -1);
    /// AddNewsFlash (0x100e9bb): the header "[m:ss] News Flash:" in colour (79, 0, 143) and the text as body.
    void add_news_flash(uint32_t elapsed_ms, const std::string& text);
    /// A chat message that arrives from another player (0x102411a): dropped when the option "Participate In Chat" is off; a team
    /// message is shown only to its sender and to the players whose ally the sender is.
    void receive_chat_message(uint8_t sender, const std::string& name, const std::string& text, bool to_team, const sim::WorldState& world);
    void trigger_quick_chat(size_t index);
    /// The display lines of the chat log (an entry's header line, then its body lines); the colour of a line is in get_chat_line_colour.
    const std::deque<std::string>& get_chat_log() const noexcept { return chat_log_; }
    /// Colour index of a display line: 0..3 header of that team colour, 4 news flash header, 5 body text.
    uint8_t get_chat_line_colour(size_t line) const noexcept { return line < chat_line_colour_.size() ? chat_line_colour_[line] : uint8_t{5}; }
    static constexpr size_t kChatInputMax = 100;         // the input box holds 100 characters (0x100dd85)

    /// The text is measured with this renderer (the original measures a label with GDI when it is made); without one every character is 6 px wide. The entries are laid
    /// out again when it changes. The pointer is not owned and must outlive its use.
    void set_text_metrics(const IRenderer* metrics);

    // The chat log window (object [W + 0x4acc], docs 5.56). The view is (482, 299) - (620, 400) = 138 x 101 px; an entry's header is at the view's left edge and its body
    // 10 px to the right; entry k starts one pixel below entry k - 1.
    static constexpr int32_t kChatViewX = ScreenLayout::kChatViewX, kChatViewY = ScreenLayout::kChatViewY, kChatViewW = ScreenLayout::kChatViewW, kChatViewH = ScreenLayout::kChatViewH;
    static constexpr int32_t kChatBodyX = 10, kChatBodyW = 126;
    static constexpr uint32_t kChatFollowPeriodMs = 50;   // CHATAPPD (0x1025282): the follow step of 5 px
    static constexpr int32_t kChatFollowStep = 5;
    static constexpr uint32_t kChatScrollPeriodMs = 100;  // CHATSCRL (0x1025234): 15 px while the pointer is held outside the view
    static constexpr int32_t kChatScrollStep = 15;
    /// The height of the whole log plus one (the `+0x2c` of the window)
    int32_t chat_content_end() const noexcept { return chat_content_end_; }
    /// The top of the window into the log that follows the newest entry (`+0x24`) and the one it moves to (`+0x28`)
    int32_t chat_follow_pos() const noexcept { return chat_follow_pos_; }
    int32_t chat_follow_target() const noexcept { return chat_follow_target_; }
    /// True while the left button drags the log; then the log shows `chat_drag_offset()` instead of `chat_follow_pos()`
    bool chat_dragging() const noexcept { return chat_dragging_; }
    int32_t chat_drag_offset() const noexcept { return chat_drag_offset_; }
    /// The top of the log as it is drawn now
    int32_t chat_view_offset() const noexcept { return chat_dragging_ ? chat_drag_offset_ : chat_follow_pos_; }
    bool in_chat_view(int32_t x, int32_t y) const noexcept { return layout_.chat_view().contains(x, y); }
    /// The transcript the original writes to chat.txt when the program ends: "date @ time", a blank line, then "header body" per entry
    std::string chat_transcript(const std::string& date_time) const;

    // Team state: the [Team] button and the team destination of Enter exist while the local player has an ally
    bool is_on_team() const noexcept { return is_on_team_; }
    void set_on_team(bool on_team) noexcept { is_on_team_ = on_team; }
    void set_player_name(std::string name) { player_name_ = std::move(name); }
    /// The names of the four teams (the labels of the other players' scores); an empty name shows the colour word. The roster says which teams exist.
    void set_team_names(const std::array<std::string, 4>& names) { team_names_ = names; }
    void set_roster_mask(uint8_t mask) noexcept { roster_mask_ = static_cast<uint8_t>(mask & 0x0Fu); }
    const std::array<std::string, 4>& team_names() const noexcept { return team_names_; }
    uint8_t roster_mask() const noexcept { return roster_mask_; }
    const std::string& get_player_name() const noexcept { return player_name_; }

    // Selection controls
    void select_ant(uint32_t ant_id, bool is_multi = false);
    void select_base(int32_t team_id) noexcept;
    void clear_selection() noexcept;
    uint32_t get_selected_ant_id() const noexcept { return selected_ant_id_; }
    const std::vector<uint32_t>& get_selected_ant_ids() const noexcept { return selected_ant_ids_; }
    void set_selected_ant_ids(std::vector<uint32_t> ids) {
        selected_ant_ids_ = std::move(ids);
        if (!selected_ant_ids_.empty()) selected_ant_id_ = selected_ant_ids_[0];
        else selected_ant_id_ = 0;
        is_multi_select_mode_ = (selected_ant_ids_.size() > 1);
    }
    int32_t get_selected_base_team_id() const noexcept { return selected_base_team_id_; }
    bool is_ant_selected(uint32_t id) const noexcept;
    void select_all_friendly(const sim::WorldState& world);
    void select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world, bool additive = false);
    bool is_multi_select() const noexcept { return is_multi_select_mode_ || selected_ant_ids_.size() > 1; }

    // Input task (Ants.exe INPUT, every 50 ms): edge scrolling with the pointer and the minimap drag; see edge_scroll.hpp. `map_w` / `map_h`
    // are the map's size in tiles. Returns true when the view moved. Nothing happens while a dialog is open or the left button is captured.
    bool input_tick(ViewportCamera& camera, uint32_t map_w, uint32_t map_h, int32_t mouse_x, int32_t mouse_y);
    /// A dialog (options, quit, quick help, the "get ready" modal) is open: it gets all input, the hover and scroll logic does not run.
    bool is_modal_open() const noexcept {
        return options_.is_open() || show_quit_dialog_ || show_quick_help_ || show_match_start_modal_ || alliance_dialog_ != AllianceDialog::None;
    }
    /// The left button is captured by the map (a rubber band), the minimap or a button (`[5534]` != 0): no edge scrolling then.
    bool is_input_captured() const noexcept;
    /// The step of the edge scroll at a pointer position (edge_scroll.hpp), in SCREEN pixels, for the camera's zoom: the same distance on the screen at every zoom (the camera moves by
    /// step / zoom world pixels). `rate` is the scroll setting 0 .. 99; the map is map_tiles_w x map_tiles_h tiles. At the zoom 1 it is edge_scroll_step with the camera's world origin.
    EdgeScroll edge_step(const ViewportCamera& camera, int32_t x, int32_t y, int32_t rate, int32_t map_tiles_w, int32_t map_tiles_h) const noexcept;
    /// May the wheel or the middle button change the zoom of the view now? Not while a dialog or a page is open (options, quick help, quit, alliance, "get ready"), not while a press holds
    /// the mouse (a rubber band, the minimap, a button, the right button, the chat log's drag): a zoom in the middle of a drag would move the world under it. The position is the
    /// caller's (over_map: the pointer must be over the map view, not over a panel).
    bool view_zoom_allowed() const noexcept { return !is_modal_open() && !is_input_captured() && !chat_dragging_; }

    // Status line (Ants.exe PostStatus): one slot, 5 s life, flash flag; see status_line.hpp
    void post_status(const std::string& text, bool flash = false) { status_line_.post(text, flash); }
    /// Posts the original's text of a string id (flash flag from the table); `arg` fills its `%s`.
    void post_status_id(uint16_t string_id, const std::string& arg = {});
    void clear_status() { status_line_.clear(); }
    const StatusLine& status_line() const noexcept { return status_line_; }

    // Pointer model (Ants.exe FUN_0102737e / FUN_01027530 / FUN_010277f4 / FUN_01027b51 / FUN_01026aa3): the cursor mode decides what a
    // click does. Panel mode [54ec]: 1 nothing selected, 2 a hill, 3 one own ant, 4 several own ants, 5 another player's ant (inspect).
    enum class PanelMode : uint8_t { None = 1, Base = 2, OneAnt = 3, Ants = 4, Other = 5 };
    PanelMode panel_mode(const sim::WorldState& world) const;
    /// FUN_010282e0: true and the type when all selected own ants have the same type (the ability pedestal and the panel text ask it with flag 0: what the ant IS, FUN_0100f9cb(ant, 0)).
    /// `for_orders` is the flag 1 of the cursor (FUN_01026f91, 0x1026f9d) and of the right click (0x1027da6, 0x1027db3): the getter's own-type branch for Bomber, Fire and Swimmer
    /// (0x100f9eb - 0x100fa27) answers Worker for such an ant while its action is anything but idle, walking or stunned (0, 1, 3), so a busy bomber is no bomber for the special target
    /// and the right click; every other ant (a thief, a combat ant, a worker of any default type) answers as with flag 0.
    bool homogeneous_type(const sim::WorldState& world, sim::AntType& type, bool for_orders = false) const;
    /// The simulation that answers the cursor's special-target question (FUN_01026f91); without it no tile is a special target.
    void set_sim_query(const sim::SimulationEngine* sim) noexcept { sim_query_ = sim; }
    const sim::SimulationEngine* sim_query() const noexcept { return sim_query_; }
    /// Where the HUD sends the player's commands (group orders, Stop, hatch, alliance offers): a single-player game applies them at once (null =
    /// the engine itself), a network match hands them to the turn manager, which applies them at the agreed turn.
    void set_command_sink(sim::CommandSink* sink) noexcept { command_sink_ = sink; }

    /// The two command pedestals are latched visually (slot modes [54f8] / [54fc] = 2): the move pedestal only suppresses the rubber band,
    /// the ability pedestal makes valid targets show the target cursor; an accepted order, the other pedestal, Stop or a deselect release them.
    bool is_move_latched() const noexcept { return slot_latched_[0]; }
    bool is_ability_latched() const noexcept { return slot_latched_[1]; }
    /// BTNPUSH (FUN_01028ffe): the pedestal of slot 0 / 1 / 2 (move-hatch-ally / ability / stop) shows pressed for 125 ms after an order
    bool is_pedestal_flashing(int slot) const noexcept {
        if (slot < 0 || slot > 2) return false;
        const uint32_t now = ticks_fn_ ? ticks_fn_() : SDL_GetTicks();
        return now < btnpush_until_ms_[slot];
    }
    void unlatch_pedestals() noexcept { slot_latched_[0] = false; slot_latched_[1] = false; }

    /// The group order of the original (FUN_010287b5) for the selected own ants: `special` and `attack` are its two flags. Returns the ant that
    /// acknowledged (0 = none).
    uint32_t order_selected(sim::SimulationEngine& sim, sim::TileCoord tile, bool special, bool attack);
    /// A left click at the release point (FUN_010277f4): the cursor mode there decides.
    void pointer_click(sim::SimulationEngine& sim, ViewportCamera& camera, int32_t x, int32_t y, bool shift);
    /// A right release (FUN_01027b51): `capture` is what the press captured (0 nothing, 1 the map view, 2 the minimap); the order goes to the
    /// tile of the press point, the cursor mode is the one of the release point.
    void pointer_right_click(sim::SimulationEngine& sim, ViewportCamera& camera, int capture, int32_t press_x, int32_t press_y, int32_t x, int32_t y);
    /// A left release (FUN_01027530): a rubber band of at most 4 px in both directions is a click at the release point, a bigger one selects.
    void pointer_release(sim::SimulationEngine& sim, ViewportCamera& camera, int32_t x, int32_t y, bool shift);
    /// The stop order of the selected own ants (FUN_01028a60) with its status text.
    void stop_selected(sim::SimulationEngine& sim);
    /// Convenience used by the tests and the pointer code: a plain group move order to a tile (special 0, attack 0).
    void dispatch_move_order(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim);
    /// A group attack order at the tile of the given ant.
    void dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim);
    /// The ant under a click at a world position (Ants.exe FUN_01026904 with FUN_01026a39): the 3x3 tiles around the clicked
    /// tile are scanned (rows outer, columns inner) and an ant registered on one of them is hit when the click lies in the
    /// half-open rectangle around its sprite position (combat ant [x-32, x+26) x [y-46, y+16), every other type
    /// [x-20, x+20) x [y-32, y+16)); the last hit wins. There is no filter: dying, flying, drowning, scuffling and underground ants
    /// are hit as well, and fog is only looked at for the tile under the pointer.
    const sim::AntSnapshot* pick_ant_at(const sim::WorldState& world, int32_t world_x, int32_t world_y) const;


    // Pedestal Action Buttons
    const UIButton& get_move_pedestal_button() const noexcept { return move_pedestal_button_; }
    const UIButton& get_ability_pedestal_button() const noexcept { return ability_pedestal_button_; }

    // Dialog and Modal overlays
    void open_quit_dialog() noexcept {
        release_capture();
        show_quit_dialog_ = true;
    }
    void close_quit_dialog() noexcept { show_quit_dialog_ = false; }
    bool is_quit_dialog_open() const noexcept { return show_quit_dialog_; }
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }

    /// The quick help (F1, the Help button; Ants.exe FUN_010145d2): it closes only by its Return button (the button class: captured at the press, acts at the release)
    /// or by the keys Enter, Esc, C and X (either case); a click anywhere else does nothing. (`M` would open the More Help dialog: not built, docs 5.52.)
    void open_quick_help() noexcept { release_capture(); quick_help_return_.reset(); show_quick_help_ = true; }
    void close_quick_help() noexcept { show_quick_help_ = false; quick_help_return_.reset(); }
    bool is_quick_help_open() const noexcept { return show_quick_help_; }
    const ScreenButton& quick_help_return_button() const noexcept { return quick_help_return_; }

    /// The options screen (FUN_0101487c, docs 5.51): a window that takes every event while it is open. Its settings are `options()`; a setting that a callback of
    /// the screen changes is written to the config store (when there is one) and applied at once: the Sound Volume slider calls `set_on_sfx_volume` and plays the
    /// test voice, the Music Volume slider calls `set_on_music_volume`, the Scroll Speed is read by the input tick, the chat switch by the chat box.
    void open_options() { release_capture(); options_.open(clock_ms()); }
    void close_options() noexcept { options_.close(); }
    bool is_options_open() const noexcept { return options_.is_open(); }
    const OptionsScreen& options_screen() const noexcept { return options_; }
    OptionsState& options() noexcept { return options_.state(); }
    const OptionsState& options() const noexcept { return options_.state(); }
    /// Where the profile is written (null: nowhere); the program's start loads `options()` from the same store
    void set_config_store(ConfigStore* store) noexcept { config_store_ = store; }

    /// The Sound Volume and Music Volume sliders' callbacks get the value 0 .. 100 (the integer of the original's profile)
    void set_on_sfx_volume(std::function<void(int32_t)> cb) { on_sfx_volume_ = std::move(cb); }
    void set_on_music_volume(std::function<void(int32_t)> cb) { on_music_volume_ = std::move(cb); }
    void set_on_play_sfx(std::function<void(uint32_t)> cb) { on_play_sfx_ = std::move(cb); }
    void play_sfx(uint32_t sound_id) { if (on_play_sfx_) on_play_sfx_(sound_id); }

    int32_t get_sound_volume() const noexcept { return options_.state().sound_volume; }
    int32_t get_music_volume() const noexcept { return options_.state().music_volume; }
    /// The Scroll Speed 0 .. 99 that the edge scroll uses (its half extent is this + 10)
    int32_t get_scroll_speed() const noexcept { return options_.state().scroll_speed; }
    bool is_chat_enabled() const noexcept { return options_.state().chat; }
    bool is_quick_help_enabled() const noexcept { return options_.state().quick_help; }
    const std::string& get_quick_chat_key(size_t index) const {
        static const std::string empty;
        return (index < 4) ? options_.state().quick_chat[index] : empty;
    }

    /// Ctrl+L (0x1026440): the ant draw prints the hit points of every ant as text at its sprite position. Owner's tweak of the original: ON by default
    /// (the original starts with it off); Ctrl+L toggles it as in the original.
    bool is_show_hp() const noexcept { return show_hp_; }

    /// The alliance dialogs (Ants.exe FUN_01015b65 invitation, FUN_010160e2 waiting, FUN_01016438 confirmation; docs 5.42). They follow from the state of the
    /// simulation, never from an event: the invitee sees the question while an offer to it is pending (Accept / Decline; keys A, D, Esc), the proposer sees the
    /// waiting dialog while its offer is pending (Withdraw; keys W, Esc), and a team that is about to be broken (a new offer while allied, an attack on the
    /// ally) asks first (Yes / No; keys Y, N, Esc). Only one dialog is open at a time and none opens while another dialog is.
    enum class AllianceDialog : uint8_t { None, Invitation, Waiting, BreakConfirm };
    AllianceDialog alliance_dialog() const noexcept { return alliance_dialog_; }
    /// The other team of the open dialog: the proposer (Invitation), the invitee (Waiting), the ally that would be left (BreakConfirm)
    uint8_t alliance_dialog_team() const noexcept { return alliance_other_; }
    const std::string& alliance_dialog_text() const noexcept { return alliance_text_; }
    /// The ally pedestal's click (FUN_0100c7ac): without an ally the offer goes out at once, with one the confirmation comes first
    void request_team_up(sim::SimulationEngine& sim, uint8_t target);

    /// The "Get ready to play!" dialog that every match opens with (Ants.exe 0x1017127: the original's picture, text and footer, and it takes every click and key while it is up). Its clock is
    /// the HUD's own: update() counts it in steps of 50 ms. The application calls update() for every simulation tick AND for every 50 ms of real time while the dialog is up, because the simulation
    /// does not run while it is (the remake's deliberate deviation from the original, which runs the match clock behind the dialog: sim::kMatchStartDialogMs). A local game's dialog (the default)
    /// closes itself after kMatchStartDialogMs of those steps; the dialog of a match of the network (`until_dismissed`) stays until dismiss_match_start_modal(), which the application calls when
    /// the match's first turn has executed (the host seals it kMatchStartDelayMs after the match began). No click or key closes it.
    bool is_match_start_modal_active() const noexcept { return show_match_start_modal_; }
    void start_match_modal(bool until_dismissed = false) noexcept {
        release_capture();
        show_match_start_modal_ = true;
        match_start_modal_ticks_ = 0;
        match_start_modal_until_dismissed_ = until_dismissed;
    }
    /// The 50 ms steps that the dialog has been up (update() counts them; 0 once it was opened)
    uint32_t match_start_modal_ticks() const noexcept { return match_start_modal_ticks_; }
    void dismiss_match_start_modal() noexcept { show_match_start_modal_ = false; }
    /// The dialog's length in steps of 50 ms (a local game's dialog closes itself after this many)
    static constexpr uint32_t kMatchStartModalSteps = sim::kMatchStartDialogMs / sim::TICK_MS;
    bool is_shift_held() const noexcept;
    void set_shift_held(bool held) noexcept { shift_held_ = held; }

private:
    void render_top_bar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_radar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, const ViewportCamera& camera);
    void render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    /// One team's label and score box (Ants.exe FUN_0100dbe2 labels, FUN_01021e36 boxes; docs 5.53): the slot follows the team index (the local team has the top bar's,
    /// the others the three bottom slots in index order, absent teams leave theirs), a team that is absent or has dropped out has its box covered (scorcovr), an allied team's
    /// box is half its colour and half its ally's and shows the two scores added
    void render_score_team(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, uint8_t team);
    void render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_alliance_dialog(IRenderer& renderer, const assets::AssetArchive& assets);
    void update_alliance_dialog(const sim::WorldState& world);
    void open_alliance_dialog(AllianceDialog kind, uint8_t other, std::string text);
    void close_alliance_dialog() noexcept;
    /// The answer of the open dialog: Accept / Withdraw / Yes (`yes` true) or Decline / No; sends the commands of the original's callbacks
    void answer_alliance_dialog(sim::SimulationEngine& sim, bool yes);
    std::string alliance_name(uint8_t team) const;
    std::string alliance_colour_word(uint8_t team) const;
    /// The ally pedestal of another player's hill (FUN_01027f07 mode 2, 0x1028188): more than two live players (FUN_0100c58a counts the teams whose +0x64 is
    /// clear) and the local team has not dropped out itself
    bool ally_pedestal_possible(const sim::WorldState& world) const;
    void render_quick_help(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_options_dim(IRenderer& renderer);          // a bigger picture: the checker dither over everything outside the options card, as the original's pieces do in its own picture
    void render_match_start_modal(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_marquee_box(IRenderer& renderer);
    void render_pedestal_glow(IRenderer& renderer, const assets::AssetArchive& assets, int pedestal_idx);
    /// Places every rectangle of the layout (the buttons of the top bar and the panel, the pedestal slots); run by init() and set_layout()
    void apply_layout();
    /// Is the point on pedestal slot 0 (Move / hatch / ally), 1 (ability) or 2 (Stop)? The slots are the three pedestal buttons' rectangles (FUN_01028d30, half-open)
    bool in_slot(int slot, int32_t x, int32_t y) const noexcept;

    ScreenLayout layout_{ScreenLayout::classic()};
    uint8_t local_player_id_{0};
    uint32_t selected_ant_id_{0};
    std::vector<uint32_t> selected_ant_ids_{};
    // The original's panel [54ec] is 4 ("several ants"). It is stored by the selection operations and never recounted: a shift add or shift drag sets it
    // even when one ant is selected, which makes the next click a group move (no automatic target cursor, no ability pedestal).
    bool is_multi_select_mode_{false};
    int32_t selected_base_team_id_{-1};
    std::function<void(const std::string&, bool)> on_chat_send_;
    // The chat log (docs 5.56): the entries with their layout, the display lines that derive from them, and the state of the window
    struct ChatEntry {
        std::string header;                    // "Name:" ...
        std::string body;                      // the message, at most 100 characters
        uint8_t colour{5};                     // 0 .. 3 the team colours, 4 a news flash
        int32_t top{0};                        // the top in the log: the bottom of the entry before plus one
        int32_t header_h{0};                   // the header label's height (one line)
        int32_t body_h{0};                     // the body label's height (its wrapped lines)
        std::vector<std::string> body_lines;   // the body wrapped at 126 px
        int32_t bottom() const noexcept { return top + header_h + body_h; }
    };
    std::vector<ChatEntry> chat_entries_{};
    std::deque<std::string> chat_log_{};       // the display lines: a header line, then the body lines, of every entry
    std::deque<uint8_t> chat_line_colour_{};   // parallel to chat_log_ (see get_chat_line_colour)
    const IRenderer* text_metrics_{nullptr};
    int32_t chat_content_end_{0};              // +0x2c
    int32_t chat_follow_pos_{0};               // +0x24
    int32_t chat_follow_target_{0};            // +0x28
    bool chat_follow_task_{false};             // CHATAPPD is scheduled
    uint32_t chat_follow_due_ms_{0};
    bool chat_dragging_{false};                // +0x34
    int32_t chat_drag_offset_{0};              // +0x48
    int32_t chat_drag_x_{0};                   // +0x40, +0x44: the pointer of the last press or drag event
    int32_t chat_drag_y_{0};
    bool chat_scroll_task_{false};             // CHATSCRL is scheduled
    uint32_t chat_scroll_due_ms_{0};
    const sim::SimulationEngine* sim_query_{nullptr};
    sim::CommandSink* command_sink_{nullptr};
    sim::CommandResult submit_command(sim::SimulationEngine& sim, const sim::Command& command) {
        return command_sink_ != nullptr ? command_sink_->submit(command) : sim.apply_command(command);
    }
    bool slot_latched_[2]{false, false};        // pedestal slots 1 and 2 latched
    uint32_t btnpush_until_ms_[3]{0, 0, 0};     // BTNPUSH (FUN_01028ffe): a pedestal shows pressed for 125 ms after an accepted order
    uint32_t input_lock_ticks_{0};              // after Stop (FUN_01028bdd): mouse input is ignored for 250 ms, then the selection is dropped
    bool pending_deselect_{false};
    int32_t right_press_x_{0};
    int32_t right_press_y_{0};
    int right_capture_{0};                      // what the right press captured: 0 nothing, 1 the map view, 2 the minimap
    bool slot2_attack_kind_{false};             // the selection's ability pedestal is the attack pedestal (a combat ant), set by order_selected
    void flash_pedestal(int slot) noexcept;
    bool pedestal_press(sim::SimulationEngine& sim, int32_t x, int32_t y);
    /// Accepted-order feedback of FUN_010277f4 / FUN_01027b51: a latched pedestal pops up, otherwise the pedestal of the order flashes
    void order_feedback(bool special, bool attack);
    /// The rubber band (FUN_0102653f) in screen pixels: the bounding box of the press point and the pointer (kept 1 px inside the view), widened
    /// by 1 px on both sides in a direction without extent. The rectangle is half-open, `right` / `bottom` are the largest coordinates.
    struct BandRect { int32_t left, top, right, bottom; };
    BandRect band_rect() const noexcept;
    /// Opening a dialog removes a displayed rubber band and releases the captures
    void release_capture() noexcept { is_dragging_ = false; is_radar_dragging_ = false; right_capture_ = 0; end_chat_drag(); }
    /// The cursor's special-target question (FUN_01026f91) for the selected ants' common type
    bool special_target(const sim::SimulationEngine* query, const sim::WorldState& world, sim::TileCoord tile, PanelMode panel) const;

    // Last known pointer position (drives the hover art of the animation-based controls)
    int32_t mouse_x_{-1};
    int32_t mouse_y_{-1};

    // Marquee drag selection
    bool show_hp_{true};                 // owner tweak: the original starts with the digits off
    bool is_dragging_{false};
    bool shift_held_{false};
    int32_t drag_start_x_{0};
    int32_t drag_start_y_{0};
    int32_t drag_curr_x_{0};
    int32_t drag_curr_y_{0};

    // Hatch & Incubation
    UIButton hatch_button_{};

    // Action buttons
    UIButton move_pedestal_button_{};
    UIButton ability_pedestal_button_{};
    UIButton stop_button_{};
    UIButton send_to_button_{};
    UIButton team_button_{};
    UIButton team_up_button_{};
    bool is_on_team_{false};

    // Command panel pedestals: left slot (Move / Ally / Hatch) and right slot (ability), original transition chains
    // Minimap terrain speckle (palette indices, 119x91), regenerated when the map size changes
    std::vector<uint8_t> radar_terrain_;
    uint32_t radar_map_w_{0};
    uint32_t radar_map_h_{0};

    PedestalSlot left_pedestal_;
    PedestalSlot right_pedestal_;
    uint32_t (*ticks_fn_)(){nullptr}; // millisecond clock (SDL_GetTicks when null); tests inject a fixed clock

    // Chat text input state
    std::string chat_input_{};
    uint32_t chat_focus_ms_{0};                   // when the chat edit control got the focus: the origin of its caret's blinking
    std::string player_name_{"Player"};
    std::array<std::string, 4> team_names_{};
    uint8_t roster_mask_{0x0F};

    // News Flash FIFO queue
    StatusLine status_line_{};
    // A selection change decides the status text at the next update(): 6..11 for exactly one own ant, 12 for several, otherwise
    // the text is cleared (SetPanelMode, Ants.exe FUN_01027f07). Every selection change posts it, the additions and removals with
    // shift included (they pass 0 as the last argument); the death of a selected ant and the alliance refresh keep the text.
    bool selection_status_pending_{false};
    void apply_selection_status(const sim::WorldState& world);
    // FUN_0100cd40 (the last act of every power-up pick-up, FUN_01020cdb, at 0x1020dd2; it plays no sound): when the picking ant is the local player's and selected ([ant + 0x50]), the panel is
    // rebuilt (SetPanelMode 3, or 4 when the stored panel is not 3), which posts the text of the type the lone ant is (string 12 for a group) and raises BOTH command pedestals
    // (FUN_01028360(1, 1, kind, 1, 0, 1): the slot states [54f8] and [54fc] go back to 1, a latch of either is released). Every pick-up does it, also one that changes nothing about the ant: its
    // own type again, or the type that the level already makes of a worker. The pick-up is seen as the rising edge of the ant's action 4 (the only SetAction call that pushes 4 is the one at 0x1020d2a, in the same
    // call), so what is remembered is the action of each selected own ant at the last update.
    std::vector<std::pair<uint32_t, uint8_t>> selected_actions_;
    void check_selected_pickups(const sim::WorldState& world);
    // The voices of the ordering commands and the status text that goes with them (Ants.exe FUN_0101b5f9 / FUN_0101b67b /
    // FUN_0101b711 / FUN_0101b78a)
    uint32_t voice_rand() noexcept;
    void voice_ready(sim::AntType type);                               // the ant that was selected answers
    void voice_go(sim::AntType type);                                  // move order: "On my way." / "Movin' out." / "Here I go..."
    void voice_attack(sim::AntType type);                              // attack order: "Attack!"
    void voice_special(sim::AntType type, size_t ants_ordered);        // special order: text only for exactly one thief or fire ant
    void render_status_line(IRenderer& renderer) const;
    void push_chat_entry(std::string header, const std::string& message, uint8_t header_colour);
    void layout_chat_entry(ChatEntry& entry, int32_t top) const;
    void append_chat_display_lines(const ChatEntry& entry);
    void relayout_chat();
    void update_chat_tasks();
    void chat_scroll_by(int32_t delta) noexcept;
    void start_chat_drag(int32_t x, int32_t y);
    void move_chat_drag(int32_t x, int32_t y);
    void end_chat_drag() noexcept;
    void render_chat_log(IRenderer& renderer);

    // Minimap drag navigation state
    bool is_radar_dragging_{false};

    // Top Header Buttons
    UIButton help_button_{};
    UIButton options_button_{};
    UIButton quit_button_{};

    // Dialog & Modal State
    bool show_match_start_modal_{false};
    uint32_t match_start_modal_ticks_{0};                // 50 ms steps since the dialog opened (update)
    bool match_start_modal_until_dismissed_{false};      // a match of the network: the dialog does not close by itself (its first turn closes it, dismiss_match_start_modal)

    bool show_quit_dialog_{false};
    UIButton yes_button_{};
    UIButton no_button_{};
    std::function<void()> on_quit_{nullptr};

    AllianceDialog alliance_dialog_{AllianceDialog::None};
    uint8_t alliance_other_{255};
    bool alliance_replaces_team_{false};         // Invitation: accepting ends the invitee's present team (string 2): the team is broken first
    std::string alliance_text_;
    UIButton alliance_button_a_{};               // Accept / Withdraw / Yes
    UIButton alliance_button_b_{};               // Decline / No
    /// What a confirmed BreakConfirm goes on with (the callbacks FUN_0100c838 and FUN_01020076 of the original)
    struct PendingBreak {
        enum class Action : uint8_t { None, Invite, Attack };
        Action action{Action::None};
        uint8_t target{255};
        sim::TileCoord tile{};
        std::vector<uint32_t> ants;
    };
    PendingBreak pending_break_;
    uint8_t suppressed_invite_from_{255};        // an offer that was just answered: its question does not come back until the simulation has cleared it
    uint8_t suppressed_wait_for_{255};           // the same for the waiting dialog of an offer that was just withdrawn
    uint32_t issue_group_order(sim::SimulationEngine& sim, sim::TileCoord tile, bool special, bool attack, const std::vector<uint32_t>& targets);

    bool show_quick_help_{false};
    ScreenButton quick_help_return_{ButtonRect{529, 437, 98, 26}, ButtonRect{527, 437, 97, 24}};      // the pictures qh_return1 / 2 and qh_return3 (the hit test is the rectangle of the picture that shows)

    // The options screen and what it changes
    OptionsScreen options_;
    ConfigStore* config_store_{nullptr};
    void apply_option(OptionSetting setting);
    uint32_t clock_ms() const noexcept { return ticks_fn_ ? ticks_fn_() : SDL_GetTicks(); }
    uint32_t voice_seed_{0x2545F491u};      // the original's rand() for the choice of a voice; never feeds the simulation
    std::function<void(int32_t)> on_sfx_volume_{nullptr};
    std::function<void(int32_t)> on_music_volume_{nullptr};
    std::function<void(uint32_t)> on_play_sfx_{nullptr};
    std::function<void(int32_t, int32_t)> on_spawn_click_marker_{nullptr};
    mutable CursorType current_cursor_{CursorType::Normal};
};

} // namespace ants::app
