// The desktop start menu in the application (start_menu.hpp is its model and its drawing): what it asks for, the connection to the game server that Join and Host
// make, the way into the original's screens, and the way back after a network game. The menu is part of a run only when ApplicationConfig::start_menu says so
// (parse_arguments: a native game started without a mode on the command line); every other run has none of this code on its path.

#include <iostream>
#include <random>

#include "ants_app/application.hpp"
#include "ants_app/version.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

namespace {

SeatChoice seat_choice_of_level(ai::Level level) noexcept {
    switch (level) {
        case ai::Level::Easy: return SeatChoice::Easy;
        case ai::Level::Hard: return SeatChoice::Hard;
        case ai::Level::Medium: break;
    }
    return SeatChoice::Medium;
}

std::string unreachable_text(const std::string& server) { return "Cannot reach " + server + ". Check the server's address and your connection."; }

// One rule for a server that says nothing: the lookup that never ends, a connection that is never made, and a server that accepts and never sends its Welcome all end in this line (after
// 20 seconds for the whole attempt, or the 10 that the room's Welcome may take, whichever comes first)
std::string no_answer_text(const std::string& server) { return "The server " + server + " did not answer. Check the server's address and your connection."; }

}  // anonymous namespace

// The menu's settings, server, clipboard and callbacks (init, when this run has a menu)
void Application::init_start_menu() {
    start_menu_.set_own_seat(config_.local_player_id < 4 ? config_.local_player_id : uint8_t{0});
    MenuSettings settings;
    settings.load(config_store_);
    // The name that the screen proposes: --name, else what was stored, else the name of a network game ("Player": a network game never sends the user and machine name by itself)
    if (!config_.player_name.empty()) settings.name = clean_player_name(config_.player_name);
    if (settings.name.empty()) settings.name = "Player";
    for (const ai::BotSpec& bot : config_.bots) {                    // --start-menu --bot 1:hard: the seats start as the command line says
        if (bot.seat < 4 && bot.kind == "standard") settings.seats[bot.seat] = seat_choice_of_level(bot.level);
    }
    start_menu_.set_settings(settings);

    // The server: --server, else the stored `server`, else the public beta server
    ServerAddress server;
    std::string why;
    if (!config_.server.empty() && parse_server(config_.server, server, why)) {
        // (a command line that did not parse never gets here: parse_arguments refuses it)
    } else if (!settings.server.empty() && parse_server(settings.server, server, why)) {
    } else {
        if (!settings.server.empty()) std::cerr << "[Application] settings: " << why << " The default " << server_label(ServerAddress{}) << " is used." << std::endl;
        server = ServerAddress{};
    }
    start_menu_.set_server(server);

    start_menu_.set_on_change([this](MenuSetting which) { start_menu_.settings().write(config_store_, which); });
    start_menu_.set_on_play_sfx([this](uint32_t sound_id) { play_ui_sound(sound_id); });
    std::function<std::string()> get = config_.clipboard_get;
    std::function<bool(const std::string&)> set = config_.clipboard_set;
    if (!get) {
        get = []() {
            char* text = SDL_GetClipboardText();
            std::string out = text != nullptr ? text : "";
            if (text != nullptr) SDL_free(text);
            return out;
        };
    }
    if (!set) set = [](const std::string& text) { return SDL_SetClipboardText(text.c_str()) == 0; };
    start_menu_.set_clipboard(get, set);
}

// The first panel of the menu: the state, the pointer replayed into it (the pointer is one global, as for every screen)
void Application::enter_start_menu(const std::string& notice) {
    state_ = AppState::StartMenu;
    start_menu_.show_main(notice);
    if (mouse_has_moved_ && !pointer_outside_) start_menu_.on_mouse_move(mouse_screen_x_, mouse_screen_y_);
}

void Application::handle_menu_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEMOTION:
            note_pointer(event.motion.x, event.motion.y);
            start_menu_.on_mouse_move(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            note_pointer(event.button.x, event.button.y);
            start_menu_.on_mouse_move(event.button.x, event.button.y);              // (every press is preceded by a move to the pointer, as the original's input does)
            start_menu_.on_mouse_down(event.button.x, event.button.y, event.button.button);
            break;
        case SDL_MOUSEBUTTONUP:
            note_pointer(event.button.x, event.button.y);
            start_menu_.on_mouse_move(event.button.x, event.button.y);
            start_menu_.on_mouse_up(event.button.x, event.button.y, event.button.button);
            break;
        case SDL_KEYDOWN:
            start_menu_.on_key(event.key.keysym.sym, static_cast<uint16_t>(event.key.keysym.mod), event.key.repeat != 0);
            break;
        case SDL_TEXTINPUT:
            start_menu_.on_text(event.text.text);
            break;
        case SDL_TEXTEDITING:                                                // an input method's composition in progress (SDL_StartTextInput is on for the whole run): it is not text yet, the
            break;                                                           // committed characters arrive as SDL_TEXTINPUT (printable ASCII goes into the field, the rest is refused with a line)
        default:
            break;
    }
}

// Once per frame (pump_network): the menu's clock, what it asked for, and the connection that its Join or Host started
void Application::update_start_menu(float dt) {
    if (state_ != AppState::StartMenu) return;
    start_menu_.update(dt);
    const MenuRequest request = start_menu_.take_request();
    if (request.type != MenuRequest::Type::None) process_menu_request(request);
    if (state_ == AppState::StartMenu && menu_conn_.stage != MenuConnection::Stage::None) {
        menu_conn_.elapsed_ms += static_cast<double>(dt) * 1000.0;
        pump_menu_connection();
    }
}

void Application::process_menu_request(const MenuRequest& request) {
    switch (request.type) {
        case MenuRequest::Type::None:
            break;
        case MenuRequest::Type::Quit:
            quit();
            break;
        case MenuRequest::Type::Single:
            menu_start_single(request.bots);
            break;
        case MenuRequest::Type::Join:
            begin_menu_connection(false, request.room, request.name, 0, 0);
            break;
        case MenuRequest::Type::Host: {
            static std::random_device entropy;
            const std::function<uint32_t()> random = config_.room_code_random ? config_.room_code_random : std::function<uint32_t()>([]() { return static_cast<uint32_t>(entropy()); });
            const std::string code = make_room_code(menu_map(static_cast<size_t>(request.map)), request.players, random);
            begin_menu_connection(true, code, request.name, request.players, request.map);
            break;
        }
        case MenuRequest::Type::Cancel:
            abort_menu_connection();
            start_menu_.connection_cancelled();
            break;
        case MenuRequest::Type::LeaveRoom:
            abort_menu_connection();
            start_menu_.room_left();
            break;
        case MenuRequest::Type::EnterRoom:
            if (menu_conn_.stage == MenuConnection::Stage::InRoom && net_ && net_->phase() == net::NetGame::Phase::Room) {
                menu_conn_.stage = MenuConnection::Stage::None;
                show_opening_screens();                               // the quick help (when the option asks for it), then the room's screen: the leader's, with START
            }
            break;
    }
}

// ---- the connection --------------------------------------------------------------------------------------------------------------------------------

// Join (hosting false: the room is the code that the player typed) and Host (hosting true: the code is a new "demo-<map>-<n>p-<random>" that the server makes on the first Hello): the same
// path as `--join HOST:PORT --room CODE --name NAME`, after the server's name has been looked up on a worker thread (the window stays alive; Esc cancels)
void Application::begin_menu_connection(bool hosting, const std::string& room, const std::string& name, int players, int map) {
    abort_menu_connection();
    menu_conn_ = MenuConnection{};
    menu_conn_.stage = MenuConnection::Stage::Lookup;
    menu_conn_.hosting = hosting;
    menu_conn_.room = room;
    menu_conn_.name = name;
    menu_conn_.server = start_menu_.server();
    menu_conn_.label = server_label(menu_conn_.server);
    menu_conn_.players = players;
    menu_conn_.map = map;
    host_lookup_.start(menu_conn_.server.host, config_.host_resolver, config_.host_launcher);
}

void Application::menu_connection_failed(const std::string& message) {
    const std::string text = message;
    abort_menu_connection();
    start_menu_.connection_failed(text);
}

// Nothing of an attempt stays: the lookup is given up, the connection is closed, the title is the program's again
void Application::abort_menu_connection() {
    host_lookup_.cancel();
    if (net_) {
        net_->leave();
        net_.reset();
    }
    menu_conn_ = MenuConnection{};
    set_window_title(config_.title);
}

void Application::pump_menu_connection() {
    using Stage = MenuConnection::Stage;
    // The most the menu waits for a server that has not answered (a name that does not resolve, a connection that is neither made nor refused, a server that never says Welcome): the
    // player is told and may try again. (A player who is in the room already has nothing to wait for.)
    if ((menu_conn_.stage == Stage::Lookup || menu_conn_.stage == Stage::Joining) && menu_conn_.elapsed_ms > static_cast<double>(config_.menu_connect_timeout_ms)) {
        menu_connection_failed(no_answer_text(menu_conn_.label));
        return;
    }
    switch (menu_conn_.stage) {
        case Stage::None:
            break;
        case Stage::Lookup: {
            const HostLookup::State state = host_lookup_.poll();
            if (state == HostLookup::State::Pending) break;
            if (state != HostLookup::State::Done) {
                if (host_lookup_.start_failed()) menu_connection_failed("Could not start the lookup of " + menu_conn_.server.host + " (the system has no thread to spare). Please try again.");
                else menu_connection_failed("Cannot find " + menu_conn_.server.host + ": the name is not known. Check the server's name and your connection.");
                break;
            }
            net_ = std::make_unique<net::NetGame>(sim_);
            net_time_ms_ = 0.0;
            attach_net();                                                    // (a guest announces nothing on the LAN: only a host's room does, so neither the announcement nor its version is set)
            if (!net_->join(host_lookup_.address(), menu_conn_.server.port, menu_conn_.name, 255, menu_conn_.room, std::string())) {
                menu_connection_failed(unreachable_text(menu_conn_.label));            // (no socket could be made for the address: the same to the player as a server that does not answer)
                break;
            }
            menu_conn_.stage = Stage::Joining;
            break;
        }
        case Stage::Joining: {
            if (!net_) {
                menu_connection_failed("The connection to " + menu_conn_.label + " failed.");
                break;
            }
            switch (net_->phase()) {
                case net::NetGame::Phase::Room:
                case net::NetGame::Phase::Loading:                       // the last seat of a room: Welcome, Room and Start arrive together, the player is in the room and the match is loading
                case net::NetGame::Phase::Playing:
                    menu_connected();
                    break;
                case net::NetGame::Phase::Failed:
                    menu_connection_failed(menu_failure_text());
                    break;
                case net::NetGame::Phase::Connecting:
                    break;                                                   // (the time limit above ends a wait that does not end)
                default:
                    menu_connection_failed("The connection to " + menu_conn_.label + " was lost before you were in the room.");
                    break;
            }
            break;
        }
        case Stage::InRoom: {
            if (!net_ || net_->phase() == net::NetGame::Phase::Failed || net_->phase() == net::NetGame::Phase::Over) {
                const std::string text = !net_ ? std::string("The connection to the server was lost.") : menu_failure_text();
                abort_menu_connection();
                start_menu_.room_left(text);
                break;
            }
            int players = 0;                                                 // the room as the server last said it: who is in
            for (const auto& slot : net_->room().slots) players += slot.state != net::SlotState::Empty ? 1 : 0;
            start_menu_.set_room_players(players, menu_conn_.players);
            break;
        }
    }
}

// The player is in the server's room (the first Room message arrived)
void Application::menu_connected() {
    if (menu_conn_.hosting && !net_->is_leader()) {
        // The server had a room with this code already (a one in ~900 million chance): the player joined somebody else's room instead of making one. Leave it.
        menu_connection_failed("That room code was taken already. Please try again.");
        return;
    }
    if (menu_conn_.hosting && !room_has_chosen_map()) {
        // The server makes a demo room on the map that the code names when it offers that map (--demo-maps), and on its default map when it does not: the player chose a map that this server does
        // not play. The room is left again, and the player is told which map it was.
        const MenuMap& wanted = menu_map(static_cast<size_t>(menu_conn_.map));
        menu_connection_failed(std::string("This server does not offer the ") + wanted.name + " map (its room is on " + net_->room().map_name + "). Choose another map.");
        return;
    }
    apply_player_name(menu_conn_.name);
    set_window_title((config_.title.empty() ? std::string("Ants") : config_.title) + " - room " + menu_conn_.room);
    if (menu_conn_.hosting) {
        menu_conn_.stage = MenuConnection::Stage::InRoom;
        int players = 0;
        for (const auto& slot : net_->room().slots) players += slot.state != net::SlotState::Empty ? 1 : 0;
        start_menu_.show_room(menu_conn_.room, players, menu_conn_.players);
        return;
    }
    menu_conn_.stage = MenuConnection::Stage::None;
    show_opening_screens();                                                  // the quick help, then the room's screen: the guest's, or the leader's with START
}

// The room that the server made is on the map that the menu asked for (the file's name without its extension is the map's word of the room code, in any case)
bool Application::room_has_chosen_map() const {
    if (!net_) return true;
    std::string stem = net_->room().map_name;
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem.resize(dot);
    return stem.empty() || menu_map_index(stem) == menu_conn_.map;                       // (a room that names no map is the server's business)
}

// What a failed join says, in the menu's words: every reason the server can send (Reject) and every way the connection can fail before the room
std::string Application::menu_failure_text() const {
    const std::string& server = menu_conn_.label;
    if (!net_) return "The connection to " + server + " failed.";
    switch (net_->fail_reason()) {
        case net::NetGame::FailReason::Unreachable:
            return unreachable_text(server);
        case net::NetGame::FailReason::NoAnswer:
            return no_answer_text(server);
        case net::NetGame::FailReason::Lost:
            return "The connection to " + server + " was lost before you were in the room. Please try again.";
        case net::NetGame::FailReason::Closed:
            return "The server closed the room.";
        case net::NetGame::FailReason::Rejected:
            switch (net_->reject_reason()) {
                case net::RejectReason::Full:
                    return "The room " + menu_conn_.room + " is full: all its seats are taken.";
                case net::RejectReason::VersionMismatch:
                    return "This game is " + std::string(VERSION_STRING) + ", but the server runs another version of the game. Update the game, or wait until the server is updated.";
                case net::RejectReason::MatchRunning:
                    return "The match in that room has already started. Ask for a new code.";
                case net::RejectReason::Kicked:
                    return "You were removed from the room.";
                case net::RejectReason::BadRequest:
                    return "The server did not accept the request. Check your name and the room code.";
                case net::RejectReason::NoSuchRoom:
                    if (menu_conn_.hosting) return "The server could not make a room now: it is busy, or it does not host online matches. Try again in a few minutes.";
                    return "There is no room with the code " + menu_conn_.room + " on " + server + ". Check the code (capital letters matter).";
            }
            break;
        case net::NetGame::FailReason::None:
            break;
    }
    return "Could not join " + server + ": " + net_->status_text();
}

// ---- the way into the original's screens and the way back ----------------------------------------------------------------------------------

// Continue on the single-player panel: the match will have exactly the bots that `--bot SEAT:LEVEL` gives (same path: config_.bots, checked again at START with the fog option
// that the setup screen has then); an empty list is the original's single-player game, unchanged
void Application::menu_start_single(const std::vector<ai::BotSpec>& bots) {
    config_.bots = bots;
    const uint8_t own = local_player_id_ < 4 ? local_player_id_ : uint8_t{0};
    local_roster_ = config_.bots.empty() ? uint8_t{0x0F} : bot_roster(own);
    if (const std::string why = bot_setup_problem(own, false); !why.empty()) {
        std::cerr << "[Application] " << why << std::endl;
        start_menu_.show_main(why);
        return;
    }
    apply_player_name(local_player_name_);
    apply_team_names(config_.bots.empty() ? config_.team_names : local_team_names(), uint8_t{0x0F});
    show_opening_screens();
}

// A network game is over, left or lost: nothing of it stays (the room, the match, the bots of a host, the results screen, the names of its players), and the player is at the menu again
void Application::return_to_start_menu(const std::string& notice_in) {
    const std::string notice = notice_in;                                    // (a copy: the caller may pass net_notice_ or the status text of the net, which this function clears and destroys)
    if (net_) {
        net_end_session(std::string());                                      // (it stops the bots of the room)
        net_.reset();
    }
    net_notice_.clear();
    menu_conn_ = MenuConnection{};                                           // (a match that began while the room's code was on the screen left its attempt at InRoom; no lookup runs then)
    scorecard_.hide();
    apply_player_name(local_player_name_);
    set_local_player(config_.local_player_id < 4 ? config_.local_player_id : uint8_t{0});      // (the HUD's init closes the quit dialog, the quick help and the options, and forgets the match's chat and selection)
    set_window_title(config_.title);
    enter_start_menu(notice);
    start_intro_music();
}

// Leave of a network game's screens (the setup screen's Leave, the results' Leave, the quit dialog's Yes): with a start menu the player is back at it, otherwise the program ends
void Application::leave_game() {
    if (menu_enabled_ && network_active()) {
        return_to_start_menu(std::string());
        return;
    }
    quit();
}

// The tick, chat and HUD hooks of a NetGame that the application owns (a room or a match)
void Application::attach_net() {
    net_->set_on_tick([this]() { post_tick(); });
    net_->set_on_wake([this]() { background_pump(); });                            // the browser build: a message of the server wakes a hidden page (docs/NETWORK_PORT.md)
    net_->set_on_chat([this](const net::ChatMsg& m) {
        if (m.sender == local_player_id_ || m.sender >= 4) return;                // the own text is in the log already
        hud_.receive_chat_message(m.sender, sim_.get_player_name(m.sender), m.text, m.team, sim_.get_world_state());
    });
    hud_.set_on_chat_send([this](const std::string& text, bool team) {
        if (network_active()) net_->chat(text, team);
    });
}

void Application::apply_player_name(const std::string& name) {
    player_name_ = name;
    map_select_.set_player_name(name);
    scorecard_.set_local_player_name(name);
    hud_.set_player_name(name);
}

void Application::set_window_title(const std::string& title) {
    window_title_ = title;
    if (window_ != nullptr) SDL_SetWindowTitle(window_, title.c_str());
}

// ---- the rest of a click that changed the screen ---------------------------------------------------------------------------------------------------

// Which screen is up, and which panel of the menu: a change of this value by a left press or release is a click that changed the screen. A request that the menu has taken but the
// application has not carried out yet (Continue, Cancel, Continue to the room, Quit) counts: its screen follows in the same frame.
uint32_t Application::screen_signature() const noexcept {
    return static_cast<uint32_t>(state_) | (static_cast<uint32_t>(start_menu_.panel()) << 8) | (start_menu_.has_request() ? 0x10000u : 0u);
}

void Application::begin_menu_gesture(uint32_t at_ms) {
    menu_gesture_pending_ = true;
    menu_gesture_ms_ = at_ms;
}

bool Application::swallow_menu_gesture(uint8_t clicks, uint32_t timestamp_ms) {
    if (!menu_gesture_pending_) return false;
    const bool within = static_cast<int32_t>(timestamp_ms - menu_gesture_ms_) <= static_cast<int32_t>(kDoubleClickMs);
    if (clicks > 1 || within) return true;
    menu_gesture_pending_ = false;                                           // a new click: it is the screen's
    return false;
}

}  // namespace ants::app
