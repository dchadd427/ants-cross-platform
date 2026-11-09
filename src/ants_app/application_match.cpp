// The start and the end of a match in the application (application.hpp): the map that loads, the computer players of a game on this computer and of a room, the teams, the way back to the setup screen.

#include <filesystem>
#include <iostream>

#include "ants_app/application.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_ai/team_up.hpp"
#include "application_internal.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>

// Whether this game is a frame of another page (web/lobby.html's seats): it has no menu of its own, the page that holds it does
extern "C" {
EM_JS(int, ants_page_is_a_frame, (), {
    try { return window.top !== window ? 1 : 0; } catch (e) { return 1; }
});
}

// The tab goes to the front page, the site's menu: the address that the page's Menu button has too
extern "C" {
EM_JS(void, ants_go_to_front_page, (), {
    window.location.assign('/');
});
}
#endif

namespace ants::app {

using namespace application_detail;

namespace {

// Where the commands of a game on this computer go, the HUD's orders and the computer players' alike: straight into the simulation, as a click of the local player does, and into the recording of the
// match when there is one (replay.hpp). The recording is told a command before the engine is given it, so that the command stands at the turn whose tick runs next.
class LocalSink final : public sim::CommandSink {
public:
    LocalSink(sim::SimulationEngine& sim, const std::unique_ptr<replay::Recorder>& recorder) : sim_(sim), recorder_(recorder) {}
    sim::CommandResult submit(const sim::Command& command) override {
        if (recorder_) recorder_->on_command(command);
        return sim_.apply_command(command);
    }

private:
    sim::SimulationEngine& sim_;
    const std::unique_ptr<replay::Recorder>& recorder_;     // (the application's: a match that ends or begins changes it, and the sink follows)
};

// ... and of a room's host: into the sequencer of the session, for the seat of the bot (the verdict arrives with the turn, like every command's: a bot ignores it)
class NetBotSink final : public sim::CommandSink {
public:
    NetBotSink(net::NetGame& net, uint8_t seat) : net_(net), seat_(seat) {}
    sim::CommandResult submit(const sim::Command& command) override {
        sim::CommandResult result;
        if (net_.submit_bot(seat_, command)) result.status = sim::CommandResult::Status::Applied;
        return result;
    }
    uint64_t applied_at(uint64_t now) const override { return net_.bot_apply_tick(now); }

private:
    net::NetGame& net_;
    uint8_t seat_;
};

constexpr uint8_t seat_bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }

} // anonymous namespace

bool Application::start_game(const std::string& map_path) {
    stop_bots();
    uint8_t roster = 0x0F;
    if (!config_.bots.empty()) {                                      // a game with bots: re-checked at START (Fog of War is chosen on this screen)
        map_select_.set_room(MapSelectScreen::RoomView{});            // (a refusal of the last START is gone)
        const uint8_t own = local_player_id_ < 4 ? local_player_id_ : uint8_t{0};
        const std::string why = bot_setup_problem(own, map_select_.is_fog_of_war_enabled());
        if (!why.empty()) {
            show_setup_notice(why);
            return false;
        }
        roster = bot_roster(own);
        local_roster_ = roster;
    } else if (game_for_one()) {                                      // --alone: no other seat plays, so no other colony has a hill, ants or eggs
        roster = bot_roster(local_player_id_ < 4 ? local_player_id_ : uint8_t{0});
        local_roster_ = roster;
    }
    if (!load_match(map_path, config_.random_seed, roster, map_select_.is_fog_of_war_enabled(), config_.game_mode)) return false;
    apply_team_names(config_.bots.empty() ? config_.team_names : local_team_names(), roster);
    enter_match();
    const bool teams_made = form_start_teams();                       // --teams: the teams are made before the first tick (the dialog of the start is up)
    use_local_sink();
    begin_local_recording(config_.random_seed, teams_made);
    if (!config_.bots.empty()) start_local_bots(config_.random_seed);
    return true;
}

// The level, the simulation and the renderer of a match (the map file, the seed, the teams that play, the Fog of War option and the game mode are the whole
// shared state: every machine of a network match calls this with the same values).
bool Application::load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog, uint8_t game_mode) {
    ants::assets::LevelValidation verdict;
    if (!current_level_.load_from_file(map_path, &verdict, ants::assets::LevelLimits::for_play_extended())) {
        std::cerr << "[Application] Failed to load level: " << map_path << std::endl;
        if (verdict.first_fatal()) std::cerr << "[Application]   " << verdict.reason() << std::endl;
        return false;
    }
    // Is the map playable by the teams of this match? (a start marker of a team that plays must lie inside the grid: the original has no behaviour for one outside)
    verdict = current_level_.validate(roster, ants::assets::LevelLimits::for_play_extended());
    if (!verdict.playable) {
        std::cerr << "[Application] The level cannot be played: " << map_path << std::endl;
        std::cerr << "[Application]   " << verdict.reason() << std::endl;
        return false;
    }
    config_.default_map_path = map_path;
    if ((roster & 0x0Fu) != 0x0Fu) current_level_ = current_level_.for_roster(roster);      // no hill art for a team without a player

    sim_.set_fog_of_war_enabled(fog);
    sim_.set_game_mode(static_cast<sim::GameMode>(game_mode));               // (before init, like the fog: the rules of the match, the same on every machine; the Start decoder and parse_arguments let no other byte through)
    sim_.set_viewing_player_id(local_player_id_);
    sim_.init(current_level_, seed, roster);

    if (renderer_) {
        renderer_->set_level(current_level_);
        apply_match_zoom();                                                   // (the remembered level, or the nearest that this map offers)
        show_start_view();
    }
    uint8_t labelled = roster;                                                // the teams that get a score label
    if (!network_active() && !config_.label_unnamed_teams) {
        labelled = 0;
        for (uint8_t p = 0; p < 4; ++p) {
            if (((roster >> p) & 1u) != 0 && (p == local_player_id_ || !local_team_names()[p].empty())) labelled = static_cast<uint8_t>(labelled | (1u << p));
        }
    }
    hud_.set_roster_mask(labelled);
    scorecard_.set_shown_teams(labelled);                                     // the results list the same teams as the score labels
    return true;
}

// The moment a match starts for the player: music and the start sound, the HUD, the "get ready" modal, the Playing state.
void Application::enter_match(bool rejoin) {
    // In-Game Music: Shuffle between ANTS2A, ANTS2B, ANTSFUN3. A match that begins while the page is hidden (a background step) starts no music and no sound effect: the page
    // is not heard; the music starts when it is shown (apply_pending_music). A match that is rejoined is not announced: no start sound, no "Get ready" dialog below.
    if (background_stepping_) {
        pending_music_ = PendingMusic::InGame;
    } else {
        play_next_ingame_music();
        // Play authentic random game startup sound (rndm1..6.wav / Sound IDs 7..12)
        if (!rejoin) play_startup_sound();
    }

    // Reset HUD & Scorecard
    hud_.set_game_mode(sim_.game_mode());
    hud_.init(local_player_id_, !rejoin);
    hud_.reset(!rejoin);
    if (!rejoin) hud_.start_match_modal(network_active());                  // (the simulation waits for it: update_simulation, post_tick)
    held_since_ms_ = -1.0;                                                  // (nothing of an earlier match's pauses and votes)
    catch_up_backlog_ = false;
    prev_missing_.clear();
    back_name_.clear();
    vote_press_ = 0;
    cue_router_.reset();                                                    // (no cue of an earlier match is waited for)
    felt_delay_ = net::FeltDelayMeter();
    orders_seen_ = 0;                                                       // (the match's own prediction counts its orders from none)
    prediction_cooldowns_reported_ = 0;
    tick_accumulator_ = 0.0f;                                               // the dialog and the match are counted from now
    start_dialog_clock_ms_ = 0.0;
    countdown_deadline_ms_ = -1.0;                                           // (net_begin_match starts the countdown card after this)
    countdown_status_shown_ = -1;
    scorecard_.hide();
    match_over_handled_ = false;
    local_match_reported_ = network_active();                               // (a game on this computer tells the page at its first tick: once; a match of a room never does, also when it is left before the page is gone)
    state_ = AppState::Playing;
    update_picture();                                                       // (the match screen replaces whatever page was up)

    // Reset simulated cursor to middle of screen until actually seen moving
    mouse_screen_x_ = layout_.width / 2;
    mouse_screen_y_ = layout_.height / 2;
    mouse_has_moved_ = false;

    if (config_.select_ant_id > 0) {
        hud_.select_ant(static_cast<uint32_t>(config_.select_ant_id));
        if (renderer_) {
            for (const auto& a : sim_.get_world_state().ants) {
                if (a.id == static_cast<uint32_t>(config_.select_ant_id)) {
                    renderer_->camera().center_on(a.px, a.py, current_level_.width(), current_level_.height());
                    break;
                }
            }
        }
    } else if (config_.select_base_team >= 0) {
        hud_.select_base(config_.select_base_team);
    }

    state_ = AppState::Playing;
    match_started_ = true;
}

// The names of the teams reach every place that shows one: the simulation's texts (alliances, drop-outs, the chat log), the HUD's score labels,
// the results rows and the local player's own label. An empty name keeps the color word.
void Application::apply_team_names(const std::array<std::string, 4>& names, uint8_t roster) {
    std::array<std::string, 4> all = names;
    if (!network_active() && local_player_id_ < 4) all[local_player_id_] = player_name_;      // a local game: the own name (--name) belongs to the own team
    for (uint8_t p = 0; p < 4; ++p) {
        sim_.set_player_name(p, ((roster >> p) & 1u) != 0 ? all[p] : std::string());       // an empty name is the color word again
    }
    hud_.set_team_names(all);
    scorecard_.set_player_names(all);
    if (local_player_id_ < 4 && !all[local_player_id_].empty()) {
        hud_.set_player_name(all[local_player_id_]);
        scorecard_.set_local_player_name(all[local_player_id_]);
    }
}

// ------------------------------------------------------------------------------------------------
// Computer players (docs/BOTS.md): the virtual clients of ants_ai. A game without --bot never gets here and runs no bot code.
// ------------------------------------------------------------------------------------------------

// "" when the game may start with the bots of the command line, else why it may not. `own_seat` is the person's seat (a room's host sits at seat 0).
std::string Application::bot_setup_problem(uint8_t own_seat, bool fog) const {
    if (config_.bots.empty()) return std::string();
    if (config_.net_role == ApplicationConfig::NetRole::Join) return "--bot cannot be used with --join: a guest never runs bots, the host's machine does.";
    ai::SetupInfo info;
    info.bots = config_.bots;
    info.fog = fog;
    info.human_mask = seat_bit(own_seat);
    info.roster = bot_roster(own_seat);
    return ai::check_setup(info);
}

bool Application::game_for_one() const {
    return config_.alone && config_.net_role == ApplicationConfig::NetRole::None;
}

uint8_t Application::bot_roster(uint8_t own_seat) const {
    uint8_t roster = seat_bit(own_seat);
    for (const ai::BotSpec& b : config_.bots) {
        if (b.seat < 4) roster = static_cast<uint8_t>(roster | seat_bit(b.seat));
    }
    return roster;
}

std::array<std::string, 4> Application::local_team_names() const {
    std::array<std::string, 4> names = config_.team_names;
    for (const ai::BotSpec& b : config_.bots) {
        if (b.seat < 4 && names[b.seat].empty()) names[b.seat] = ai::bot_display_name(b);      // a seat is shown as a bot; an explicit name wins
    }
    return names;
}

void Application::show_setup_notice(const std::string& text) {
    std::cerr << "[Application] " << text << std::endl;
    MapSelectScreen::RoomView view;                                   // the local setup screen: its prompt line is the status
    view.status = text;
    map_select_.set_room(view);
}

// The bots of a local game, built after the simulation was initialised: their commands go straight into the simulation, like the local player's clicks
bool Application::start_local_bots(uint32_t match_seed) {
    stop_bots();
    if (config_.bots.empty() || network_active()) return false;
    bots_ = std::make_unique<ai::BotController>(sim_, match_seed);
    bool all = true;
    for (const ai::BotSpec& spec : config_.bots) {
        bot_sinks_.push_back(std::make_unique<LocalSink>(sim_, recorder_));
        std::string why;
        if (!add_bot(spec, *bot_sinks_.back(), why)) {
            std::cerr << "[Application] No bot at seat " << static_cast<unsigned>(spec.seat) << ": " << why << std::endl;
            all = false;
        }
    }
    return all;
}

// --teams: each pair of the plan becomes a team with the original's own commands, applied straight to the simulation before its first tick: the first seat invites, the second accepts (so the News Flash
// "... are a team now!" is in the chat log and no dialog opens). What cannot be made is said, and the game starts without teams.
bool Application::form_start_teams() {
    if (!config_.teams.set) return false;                              // (a room never gets here: its machines make the teams of the Start message, net_load_match)
    const LocalTeamsPlan plan = plan_local_teams(config_.teams, sim_.roster_mask());
    if (!plan.why.empty()) {
        show_setup_notice("--teams " + local_teams_text(config_.teams) + ": " + plan.why + " The game starts without teams.");
        return false;
    }
    sim::apply_start_teams(sim_, config_.teams);
    return true;
}

// The HUD's orders of a game on this computer reach the engine through the sink that the recording sees too (a network game's reach net_, which hud_ is given when its match begins)
void Application::use_local_sink() {
    if (!local_sink_) local_sink_ = std::make_unique<LocalSink>(sim_, recorder_);
    hud_.set_command_sink(local_sink_.get());
}

// The recording of a match on this computer (replay.hpp), made when its engine has been initialised and its teams made, before any command is given. The engine says the seats that play and the Fog of War
// setting; the map is the one that load_match (or init) put in default_map_path. A match whose map a file cannot name is not recorded. The names are the ones that were typed or given: a bot's, --name,
// -N, a name typed in the start menu; what the program makes of the system user ("user@machine") stays out of a file that may be sent to somebody.
void Application::begin_local_recording(uint32_t seed, bool teams_made) {
    recorder_.reset();
    withdraw_replay();
    replay::Header head;
    head.map_name = std::filesystem::path(config_.default_map_path).filename().string();
    if (!net::valid_map_name(head.map_name) || !net::hash_file(config_.default_map_path, head.map_hash)) return;
    head.venue = "local game";
    head.seed = seed;
    head.roster = sim_.roster_mask();
    head.fog = sim_.is_fog_of_war_enabled();
    head.set_mode(static_cast<uint8_t>(sim_.game_mode()));        // (the rules the engine plays by: field 17 and the rules number of the file when it is not the original's game)
    std::array<std::string, 4> names = config_.bots.empty() ? config_.team_names : local_team_names();
    if (local_player_id_ < 4 && player_name_ != get_system_username()) names[local_player_id_] = player_name_;
    for (uint8_t p = 0; p < 4; ++p) head.names[p] = ((head.roster >> p) & 1u) != 0 ? names[p] : std::string();
    if (teams_made) head.teams = config_.teams;
    head.recorder_seat = local_player_id_ < sim::MAX_PLAYERS ? local_player_id_ : replay::kNoSeat;
    begin_recording(std::move(head));
}

// The host of a room runs the room's bots: their commands go into the host's sequencer. A guest (and a guest that took over as host) never does.
void Application::start_net_bots() {
    stop_bots();
    if ((config_.bots.empty() && fill_specs_.empty()) || !net_ || !net_->is_host()) return;
    const net::RoomMsg& room = net_->room();
    bots_ = std::make_unique<ai::BotController>(sim_, net_->start_info().seed);
    std::vector<ai::BotSpec> specs = config_.bots;                   // --bot's seats, and the seats that this machine's START filled
    specs.insert(specs.end(), fill_specs_.begin(), fill_specs_.end());
    for (const ai::BotSpec& spec : specs) {
        if (spec.seat >= 4 || room.slots[spec.seat].state != net::SlotState::Bot) continue;
        bot_sinks_.push_back(std::make_unique<NetBotSink>(*net_, spec.seat));
        std::string why;
        if (!add_bot(spec, *bot_sinks_.back(), why)) std::cerr << "[Application] No bot at seat " << static_cast<unsigned>(spec.seat) << ": " << why << std::endl;
    }
}

bool Application::add_bot(const ai::BotSpec& spec, sim::CommandSink& sink, std::string& why) {
    if (config_.bot_factory) return bots_->add(spec, config_.bot_factory(spec), sink, why);
    return bots_->add(spec, sink, why);
}

void Application::stop_bots() {
    bots_.reset();                                                    // (the controller first: it holds the sinks)
    bot_sinks_.clear();
}

const ai::BotSpec* Application::bot_spec_of(uint8_t seat) const {
    for (const ai::BotSpec& b : config_.bots) {
        if (b.seat == seat) return &b;
    }
    return nullptr;
}

// A computer player holds the seat: in a local game the seats of the specs, in a room the slots that the room itself calls bots (a guest knows no more than that)
bool Application::is_bot_seat(uint8_t seat) const {
    if (seat >= 4) return false;
    if (network_active()) return net_->room().slots[seat].state == net::SlotState::Bot;
    return bot_spec_of(seat) != nullptr;
}

// Why a bot declined the local player's invitation: the original's "... rejected teaming up" says that it did, never why, so ONE chat-log line follows it (docs/BOTS.md, "Alliances"), from the rule
// the bot answers by (ai::team_up_answer) asked of the world as it is now. Nothing for any other event, a person's answer, or a rule that would accept now.
std::string Application::declined_team_up_note(const sim::NewsEvent& event) const {
    if (event.string_id != sim::strings::kTeamRejected || !is_bot_seat(event.subject)) return std::string();      // (the HUD hands over only the local player's events)
    const ai::BotSpec* spec = bot_spec_of(event.subject);
    if (spec != nullptr && spec->kind == "worker") return ai::kWorkerNeverTeamsUpText;
    const ai::BotView view = ai::BotView::build(sim_, event.subject);
    return ai::team_up_decline_text(ai::team_up_answer(view, local_player_id_), sim_.get_player_name(event.subject));
}

void Application::leave_network_match() {
    if (net_) net_->leave();                                   // (Leave is sent, the key is let go of; the page navigates away in the same breath)
}

void Application::quit() {
    if (replay_mode_) {                                        // (the results' Leave Game, the quit dialog's Yes: back to the page's list of matches)
        leave_replay();
        return;
    }
    if (menu_enabled_) start_menu_.flush();                    // (a name that was typed and not written yet)
#if defined(__EMSCRIPTEN__)
    // The browser leaves a game for the site's front page, where the page's Menu button goes too: a room is left at once (the seat is dropped, not held for a page that is gone). A game in a frame of
    // another page (web/lobby.html) has no menu to go to: it is back at its setup screen (return_to_map_select leaves the room), or the frame loads again.
    if (!ants_page_is_a_frame()) {
        leave_network_match();
        ants_go_to_front_page();
        return;
    }
    if (state_ == AppState::Playing || scorecard_.is_open()) {
        return_to_map_select();
    } else {
        if (net_) net_->leave();
        emscripten_run_script("window.location.reload();");
    }
#else
    if (net_) net_->leave();
    is_running_ = false;
#endif
}

// The setup screen is created (again): its labels stay empty until its refresh, 500 ms later
void Application::enter_map_select() {
    state_ = AppState::MapSelect;
    update_picture();                                        // (the whole canvas, as every screen)
    closing_click_pending_ = false;                          // (close_quick_help() sets it after this call)
    map_select_.enter();
    // The new screen's buttons are fresh objects in the up state; the INPUT task of the original sends the pointer to the top window in the very input run that
    // follows (FUN_0102653f), so a button under the pointer shows its hover picture before the first frame. Nothing is reset or moved: the pointer is one global.
    if (mouse_has_moved_ && !pointer_outside_) map_select_.handle_mouse_motion(mouse_screen_x_, mouse_screen_y_);
    if (play_pending_) {                                     // --play: the first visit of the setup screen is its START (the match, the dialog, the sounds); the way back after it is the screen
        play_pending_ = false;
        start_game(config_.default_map_path);
    }
}

// The loading screen ends: the start menu when this run has one, else the screens that follow it
void Application::finish_loading() {
    if (menu_enabled_) {
        enter_start_menu();
        return;
    }
    show_opening_screens();
}

// The quick help when the option asks for it, else the setup screen: what follows the loading screen of a game without a menu, and the menu's Continue
void Application::show_opening_screens() {
    if (hud_.is_quick_help_enabled()) {
        state_ = AppState::QuickHelp;
        update_picture();                                    // (the quick help is the whole canvas too: its wide page, or the original's own page)
        quick_help_start_.reset();
        if (mouse_has_moved_ && !pointer_outside_) quick_help_start_.on_move(mouse_screen_x_, mouse_screen_y_);       // the pointer goes to the new window at once (see enter_map_select)
    } else {
        enter_map_select();
    }
}

void Application::return_to_map_select() {
    stop_bots();
    if (network_active() || (net_ && map_select_.room().networked)) {
        net_end_session(net_notice_);                     // leaving a match leaves the room: the local setup screen follows (and the match's file is made there); the setup screen after a room is the local game's: also when the room was left first
    } else {
        finish_recording();                               // (a match that is left keeps its file when something happened in it; the file of one that ended stays offered until the next match begins)
    }
    net_notice_.clear();
    enter_map_select();
    scorecard_.hide();
    update_picture();
    hud_.close_quit_dialog();
    hud_.close_quick_help();
    hud_.close_options();
    if (background_stepping_) pending_music_ = PendingMusic::Intro;      // (a hidden page starts no music: it does when the page is shown)
    else start_intro_music();                                  // (the pointer is one global in the original: it is neither reset nor moved here)
}

} // namespace ants::app
