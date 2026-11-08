// The Application's side of the network layer's events and of the start of a network match (application.hpp): what the room's events do to the screens, the match that begins on this machine, and the
// countdown card that a room of two or more people opens it with (network protocol 21, docs/VIEW_AND_HUD.md "The countdown card"). The match itself never depends on the card: the host holds the first
// turn, the card is the wait made visible, with the chat open.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_names.hpp"
#include "ants_app/application.hpp"
#include "ants_app/countdown_card.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/page_layout.hpp"

namespace ants::app {

namespace {

/// How many of the newest lines of the waiting room the application hands the 16:9 setup screen's chat box: the box shows at most a handful of rows (its lines are wrapped to it and the newest stand
/// at its bottom), so the screen is spared the wrapping of the 200 lines that the room keeps
constexpr size_t kChatBoxLines = 16;

/// The text of a line of the waiting room as the chat box shows it: "Name: text" (a seat that has no name yet: "Seat N"), a room's own notice (ChatLine::notice) is its text alone
std::string chat_box_text(const net::ChatLine& line) {
    if (line.notice()) return line.text;
    return (line.name.empty() ? "Seat " + std::to_string(static_cast<unsigned>(line.seat) + 1u) : line.name) + ": " + line.text;
}

}  // anonymous namespace

void Application::handle_net_events() {
    if (!net_) return;
    for (const std::string& text : room_chat_log_.take({}, static_cast<uint32_t>(net_time_ms_))) std::cerr << "[Application] Room chat: " << text << std::endl;      // (the count of the lines that the log left out, once the budget allows it)
    for (const net::NetGame::Event& ev : net_->take_events()) {
        switch (ev.type) {
            case net::NetGame::Event::Type::StartRequested:
                map_select_.lock();                                  // the start is on (a host's START locks the screen itself; the leader's START is answered by this Start): nothing more to press
                net_load_match();
                break;
            case net::NetGame::Event::Type::Begun:
                net_begin_match(ev.rejoin, ev.countdown_seconds);
                break;
            case net::NetGame::Event::Type::Cancelled:               // the start failed: back in the room, the host's controls work again
                map_select_.unlock();
                for (const ai::BotSpec& spec : fill_specs_) net_->remove_bot(spec.seat);      // the room is as it was before the START: the bots that it seated go again
                fill_specs_.clear();
                break;
            case net::NetGame::Event::Type::RoomChanged:             // (protocol 14) a player that the leader moved takes the bot that was for its color: NetGame moves it in its plan when the room shows the
                if (net_->is_leader()) config_.fill_bots = net_->fill_bots();      // move, and the application's copy of the plan, which the footer of the setup screen shows, follows
                break;
            case net::NetGame::Event::Type::Chat:                    // (protocol 11) a line in the waiting room: the log of the program has it; the status line shows it for a few seconds
                for (const std::string& text : room_chat_log_.take(net_->take_pregame_chat(), static_cast<uint32_t>(net_time_ms_))) {       // (a budget: a flooded room does not flood the log)
                    std::cerr << "[Application] Room chat: " << text << std::endl;
                }
                break;
            case net::NetGame::Event::Type::HostLeft:                // the host is gone and no other machine could take over (or the server dropped this player for being away)
                net_notice_ = net_->status_text();                   // the reason, as the network layer says it: "The connection to the other players was lost." or "You were away too long ..."
                std::cerr << "[Application] " << net_notice_ << std::endl;                    // (for the scripts that run a headless client: nothing else says it)
                if (state_ == AppState::Playing && !scorecard_.is_open()) {
                    if (menu_enabled_) {
                        return_to_start_menu(net_notice_);                   // (the net is gone: the rest of the events belong to it)
                        return;
                    }
                    return_to_map_select();
                }
                break;
            case net::NetGame::Event::Type::Desync:
                std::cerr << "[Application] The network match is out of sync (turn " << net_->turns_executed() << ")" << std::endl;
                break;
            default:
                break;                                        // the rest of the room is shown by sync_room_view, the match by the overlay and the chat log
        }
    }
}

// Everybody has loaded: the match runs here from now on.
void Application::net_begin_match(bool rejoin, uint8_t countdown_seconds) {
    local_player_id_ = net_->my_seat();
    hud_.set_command_sink(net_.get());
    close_room_chat();
    enter_match(rejoin);
    // The match's chat log starts with what was said in the waiting room (protocol 11: the lobby kept the last 200 lines, this player's own among them): the people of a room who talked
    // before the START find their talk in the log of the match. The room's own notices ("Bots cannot play with Fog of War.") were for the setup screen and are not repeated. A machine that
    // rejoins has no waiting room behind it (and its second begin, after a server that lost the last turns, must not say the old lines again).
    for (const net::ChatLine& line : net_->pregame_chat()) {
        if (rejoin) break;
        if (line.notice()) continue;
        // (under the name that the sender had when it spoke: the seat may have another player now, a player who left and a newcomer who took the seat)
        const std::string name = !line.name.empty() ? line.name : (line.seat < sim::MAX_PLAYERS ? sim_.get_player_name(line.seat) : std::string());
        hud_.receive_chat_message(line.seat, name, line.text, false, sim_.get_world_state());
    }
    start_net_bots();
    if (!rejoin && countdown_seconds > 0) begin_net_countdown(countdown_seconds);
}

// The countdown card (protocol 21): the host holds the first turn `seconds` after the Begin that this machine has just received, so the count runs from here, on this machine's own clock (net_time_ms_ is the
// frames' time, which the page's wake-ups keep up while the tab is hidden): the match itself does not depend on it, the first turn comes when it comes and closes the card (post_tick). The people of the
// room and the computer players come from the Start that every machine holds, so every machine shows the same colors.
void Application::begin_net_countdown(uint8_t seconds) {
    std::array<CountdownSeat, 4> seats{};
    const net::StartMsg& start = net_->start_info();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        seats[seat].plays = (start.roster & (1u << seat)) != 0;
        seats[seat].name = start.names[seat];
        seats[seat].computer = seats[seat].plays && ai::is_reserved_bot_name(start.names[seat]);
    }
    countdown_deadline_ms_ = net_time_ms_ + static_cast<double>(seconds) * 1000.0;
    countdown_status_shown_ = -1;
    hud_.start_match_countdown(static_cast<int64_t>(seconds) * 1000, seats);
    tick_net_countdown();
}

void Application::tick_net_countdown() {
    if (countdown_deadline_ms_ < 0.0) return;
    if (!hud_.is_match_countdown_active()) {                 // the first turn has run, a seat was lost (the pause closes the dialog for good), or the match is left
        countdown_deadline_ms_ = -1.0;
        countdown_status_shown_ = -1;
        return;
    }
    const int64_t left = std::max<int64_t>(0, static_cast<int64_t>(std::ceil(countdown_deadline_ms_ - net_time_ms_)));
    hud_.set_match_countdown_left_ms(left);
    const int32_t shown = countdown_seconds_shown(left);
    if (shown != countdown_status_shown_) {                  // the status line lives 5 s: it is said again at every second
        countdown_status_shown_ = shown;
        hud_.post_status(shown > 0 ? "Match starts in " + std::to_string(shown) + " s" : std::string("Starting..."));
    }
}

int32_t Application::countdown_seconds_left() const noexcept {
    return hud_.is_match_countdown_active() ? countdown_seconds_shown(hud_.match_countdown_left_ms()) : -1;
}

LayoutRect Application::web_countdown_button() const noexcept {
    return hud_.countdown_button_rect().moved(picture_.x, picture_.y);
}

// The countdown card for the page (ants_probe 60 - 65, web/countdown_button.js): 60: the seconds that the countdown has left, rounded up (-1 when no card is up), 61 - 64: the place of the Fullscreen
// button on the card, x, y, width and height in the canvas's pixels (the page lays its own button over it), 65: the lines of the match's chat log (-1 outside a match: the browser check reads what the other
// player said). Anything else: -1.
int Application::countdown_probe(int what) const noexcept {
    switch (what) {
        case 60: return countdown_seconds_left();
        case 61: return web_countdown_button().x;
        case 62: return web_countdown_button().y;
        case 63: return web_countdown_button().w;
        case 64: return web_countdown_button().h;
        case 65: return match_running() ? static_cast<int>(hud_.get_chat_log().size()) : -1;
        default: return -1;
    }
}

// ------------------------------------------------------------------------------------------------
// Network play (docs/NETWORK_PORT.md): the setup screen is the room, the lock-step runner drives the ticks
// ------------------------------------------------------------------------------------------------

void Application::pump_network(float dt, double gap_seconds) {
    room_chat_.end_of_frame();                                   // (the text event of the T that opened the chat input has been dealt with: it came in the same batch of events)
    if (menu_enabled_) update_start_menu(dt);                    // the start menu's clock, what it asked for and its connection (application_menu.cpp)
    if (!net_ || !net_->active()) {
        seat_told_ = 255;                                        // (the session is over: the next one tells its seat again)
        return;
    }
    net_->set_prediction_suspended(page_hidden_ || background_stepping_);   // a hidden page shows nothing: its match is stepped by the wake-ups, and the prediction would be work for no picture
    net_->set_chat_status_mirror(room_chat_box() == nullptr);   // (the lines of the room go to the chat box of the 16:9 setup screen when it has one, to the status line otherwise)
    net_time_ms_ += static_cast<double>(dt) * 1000.0;
    net_->update(static_cast<uint32_t>(net_time_ms_));        // (the connection is read here: whatever waited counts as heard)
    if (gap_seconds > 0.0) net_->note_gap(static_cast<uint32_t>(std::min(gap_seconds * 1000.0, 4.0e9)));   // a host that said nothing has been silent for the gap too
    handle_net_events();
    poll_map_fetch();                                            // (application_maps.cpp)
    if (!net_) return;                                           // (a lost game brought the player back to the start menu, which let go of the net)
    track_net_state();
    note_seat();
    // The "Get ready to play!" dialog gives way to a pause (a seat lost before the first turn holds the match): it would hide the overlay and the vote block. It is closed for good.
    if (hud_.is_match_start_modal_active() && net_->phase() == net::NetGame::Phase::Playing && (net_->paused() || net_->pause_info().vote_open)) hud_.dismiss_match_start_modal();
    if (menu_enabled_ && state_ == AppState::MapSelect && (net_->phase() == net::NetGame::Phase::Failed || net_->phase() == net::NetGame::Phase::Over)) {
        return_to_start_menu(net_->status_text());               // the room is dead (the server closed it, the connection is gone): not a room screen that nothing can happen on
        return;
    }
    if (state_ == AppState::MapSelect && net_->active()) {
        sync_room_view();
        // --start-when N (the web front page's card and the headless test clients give it): the leader of a server's room presses START, as a click or the S key would, once N players are in; again every second until the match starts
        // --say TEXT (a test hook): the line is said once, in the waiting room, as soon as two players are there to hear it
        if (!config_.net_say.empty() && !say_sent_ && net_->phase() == net::NetGame::Phase::Room && net_->my_seat() < 4) {
            size_t here = 0;
            for (const auto& slot : net_->room().slots) here += slot.state != net::SlotState::Empty ? 1u : 0u;
            if (here >= 2) say_sent_ = net_->chat(config_.net_say);
        }
        if (config_.net_start_when > 0 && net_->is_leader() && !map_select_.is_locked() && net_time_ms_ - start_when_pressed_ms_ >= 1000.0) {
            size_t players = 0;
            for (const auto& slot : net_->room().slots) players += slot.state != net::SlotState::Empty ? 1u : 0u;
            if (players >= config_.net_start_when) {
                start_when_pressed_ms_ = net_time_ms_;
                map_select_.handle_key_down(SDLK_s);
            }
        }
    }
}

void Application::sync_room_view() {
    if (!net_ || !net_->active()) return;
    MapSelectScreen::RoomView view;
    view.networked = true;
    view.is_host = net_->is_host();
    view.leader = net_->is_leader();                                         // the first player of a server's room: the host's screen with START (protocol 7)
    view.my_seat = net_->my_seat();
    const net::RoomMsg& room = net_->room();
    for (size_t i = 0; i < view.seats.size(); ++i) {
        view.seats[i].occupied = room.slots[i].state != net::SlotState::Empty;
        view.seats[i].name = room.slots[i].name;
        view.seats[i].thumb = static_cast<MapSelectScreen::Thumb>(static_cast<uint8_t>(net_->seat_quality(static_cast<uint8_t>(i))));   // connection quality
    }
    // A guest that has not been welcomed yet has no seat and the room is not known: it shows ITSELF alone (the original's slot 0 is the local machine from the
    // start, with the good thumb; the host's row follows once the connection exists), in the color of its seat (or of the seat it asked for)
    if (net_->phase() == net::NetGame::Phase::Connecting || net_->phase() == net::NetGame::Phase::Failed) {
        for (auto& seat : view.seats) seat = MapSelectScreen::RoomSeat{};
        const uint8_t assigned = net_->my_seat();                              // (known once the host has welcomed it; the requested seat is only a guess before that)
        const uint8_t own = assigned < 4 ? assigned : (config_.net_seat < 4 ? config_.net_seat : (local_player_id_ < 4 ? local_player_id_ : static_cast<uint8_t>(0)));
        view.my_seat = own;
        view.seats[own].occupied = true;
        view.seats[own].name = player_name_;
        view.seats[own].thumb = MapSelectScreen::Thumb::Good;
    }
    view.map_file = room.map_name;                                           // a guest (and the leader of a server's room) shows the host's / the room's choice (an empty name before the first message)
    if (room_chat_.is_open() && !room_chat_available()) close_room_chat();                      // (the room is gone, or the match has begun: nothing to say it to)
    const SetupChatLayout* chat_box = room_chat_box();
    if (room_chat_.is_open() && chat_box == nullptr) {                                           // (the classic page: while a line is typed the status line shows it, as a typed line: its end, in two lines)
        view.status = room_chat_.text();
        view.status_prefix = RoomChatInput::kPrompt;
        view.status_input = true;
        view.status_caret = room_chat_.caret(static_cast<uint32_t>(net_time_ms_));
    } else {
        view.status = net_->status_text();                                                       // (the 16:9 page: the status line keeps the prompt, the typed line is in the chat box)
    }
    {
        // The leader's prompt says what START will do (the bots of each seat, the teams) in several ways, the longest first: the longest that fits this screen's label is the line
        const std::vector<std::string>& prompts = net_->prompt_texts();
        if (renderer_ && !prompts.empty() && view.status == prompts.front()) {
            view.status = prompts.back();
            for (const std::string& text : prompts) {
                if (map_select_.prompt_fits(*renderer_, text)) {
                    view.status = text;
                    break;
                }
            }
        }
    }
    map_select_.set_room(view);
    // The chat box of the 16:9 page (the Online and Guest variants): the newest lines of the waiting room (names in front, a room's notices as they are, styled by the screen), the line that is
    // typed and the caret. And the foot of the leader's Players' Status box: what fills the empty seats at START (the choice of the host panel / --fill-bots; not with Fog of War, which seats no
    // bots, and not for a guest, who cannot START).
    MapSelectScreen::ChatPanel panel;
    std::string fill_first;
    std::string fill_second;
    if (chat_box != nullptr) {
        panel.visible = true;
        const std::vector<net::ChatLine>& said = net_->pregame_chat();
        for (size_t i = said.size() > kChatBoxLines ? said.size() - kChatBoxLines : 0u; i < said.size(); ++i) {
            panel.lines.push_back(MapSelectScreen::ChatPanel::Line{chat_box_text(said[i]), said[i].notice()});
        }
        panel.typed = room_chat_.text();                                                         // (empty while the input is closed, and the caret is off then)
        panel.caret = room_chat_.caret(static_cast<uint32_t>(net_time_ms_));
        if (map_select_.setup_variant() == SetupVariant::Online) {
            // what START will do with the empty seats and the teams (the choices of the host panel / the command line; not for a guest, who cannot START): each line in its ways, the longest that fits the box
            const net::NetGame::FooterTexts footer = net::NetGame::start_footer(config_.fill_bots, net_->effective_teams(), room, room.fog, net_->room_teams().set);
            const auto pick = [this](const std::vector<std::string>& ways) {
                for (const std::string& text : ways) {
                    if (renderer_ && renderer_->get_text_width(text, FontSize::Px12) <= MapSelectScreen::footer_width()) return text;       // (it fits at the smallest size: the screen draws it as big as it can)
                }
                return ways.empty() ? std::string() : ways.back();
            };
            fill_first = pick(footer.line[0]);
            fill_second = pick(footer.line[1]);
        }
    }
    map_select_.set_chat_panel(std::move(panel));
    map_select_.set_fill_footer(std::move(fill_first), std::move(fill_second));
    if (!net_->is_host() && net_->phase() == net::NetGame::Phase::Room) {
        map_select_.follow_host_choice(room.map_name, room.fog);
    }
}

// ---- the setup screen of a room: the chat input (T or a click on the status line), see room_chat.hpp ------------------------------------------------------------

bool Application::room_chat_available() const noexcept {
    return state_ == AppState::MapSelect && net_ && net_->active() && net_->my_seat() < 4 &&
           (net_->phase() == net::NetGame::Phase::Room || net_->phase() == net::NetGame::Phase::Loading);
}

// The chat box of the 16:9 setup screen: the Online variant (the host of a room, the leader of a server's room) and the Guest variant have one (setup_layout.hpp), the classic page has none
const SetupChatLayout* Application::room_chat_box() const noexcept {
    if (state_ != AppState::MapSelect || !net_ || !net_->active() || !wide_setup()) return nullptr;
    const SetupChatLayout& chat = SetupLayout::of(net_->is_host() || net_->is_leader() ? SetupVariant::Online : SetupVariant::Guest).chat;
    return chat.valid() ? &chat : nullptr;
}

void Application::close_room_chat() {
    room_chat_.close();
    room_chat_closed_ms_ = net_time_ms_;                                               // (the guard of the screen's START keys starts here)
}

void Application::room_key_down(SDL_Keycode key, uint16_t modifiers, bool repeat) {
    if (room_chat_.is_open()) {
        if (!room_chat_available()) {
            close_room_chat();                                                         // (nobody to talk to any more: the screen has its keys back)
        } else {
            const RoomChatInput::Result result = room_chat_.on_key(key, modifiers);
            if (result == RoomChatInput::Result::Send) {
                const std::string line = room_chat_.take_line();
                room_chat_closed_ms_ = net_time_ms_;
                if (!line.empty() && net_ && net_->chat(line) && room_chat_box() == nullptr) net_->show_notice("You: " + line);       // (the classic page: the player sees on the status line that it went; the chat box shows the line when the room has relayed it)
            } else if (result == RoomChatInput::Result::Closed) {
                room_chat_closed_ms_ = net_time_ms_;
            }
            return;                                                                    // while the input is open the screen's own keys (S, Q, X, Enter, the arrows) do nothing
        }
    }
    if (!repeat && key == SDLK_t && (modifiers & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) == 0 && room_chat_available()) {
        room_chat_.open(true);
        return;
    }
    // The key that closed the input is still under the player's finger, or its twin follows (a second Enter, the S of "ss" typed from habit): START is not for those. A key that comes
    // 400 ms after the closing is a new decision. (Leave, the map and the other keys are not held back: they cannot start anything.)
    const bool starts = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_s;
    if (starts && net_time_ms_ - room_chat_closed_ms_ < RoomChatInput::kGuardMs) return;
    map_select_.handle_key_down(key, repeat);
}

void Application::room_text_input(const std::string& text) {
    room_chat_.on_text(text);
}

bool Application::room_mouse_down(int32_t x, int32_t y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return false;
    if (room_chat_.is_open()) {                                                        // a click while the input is open: closes it, sends nothing, and is not the screen's click
        close_room_chat();
        room_chat_press_taken_ = true;
        map_select_.release_buttons();
        // ... and neither is the rest of its click sequence: the closing click may be the first of a double click on START (the player typed, then double clicked START, as one does), whose second
        // press would start the match. The rule of the quick help's closing click (swallow_closing_click): a press that SDL counts as part of the sequence (clicks above 1), or that comes within
        // the double-click time of this one, does nothing; the first press after that is a new click, the screen's. (In every room that has a chat input: a host's too.)
        closing_click_pending_ = true;
        closing_click_ms_ = SDL_GetTicks();
        return true;
    }
    if (!room_chat_available()) return false;
    if (const SetupChatLayout* box = room_chat_box()) {                                // the 16:9 page: the chat box's input box opens the input, and so does a click in its lines
        if (!box->input_box.contains(x, y) && !box->lines.contains(x, y)) return false;
    } else if (x < MapSelectScreen::LABEL_X || x >= MapSelectScreen::LABEL_X + MapSelectScreen::STATUS_W || y < MapSelectScreen::STATUS_Y || y >= MapSelectScreen::STATUS_Y + MapSelectScreen::STATUS_H) {
        return false;                                                                  // the classic page: the status line
    }
    room_chat_.open(false);
    room_chat_press_taken_ = true;                                                     // (the release of the click that opened it is nobody's either)
    return true;
}

bool Application::room_mouse_up(int32_t /*x*/, int32_t /*y*/, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return false;
    if (!room_chat_press_taken_ && !room_chat_.is_open()) return false;
    room_chat_press_taken_ = false;
    map_select_.release_buttons();                                                     // (a button that was pressed before the input opened does not fire at this release either)
    return true;
}

// START on the leader's screen: the request goes to the server, which starts the match with the players who are in the room if it can. With fewer than two players the
// answer is the host's: the can't-go cue (and nothing else: no message, no change of the screen); the room of the server has no thumbs to wait for.
void Application::net_request_start() {
    if (!net_ || !net_->request_start()) play_effect(sim::SoundID::CantGo);
}

// A press on a player's row of the leader's screen (protocol 14): the player goes to the next color (NetGame::request_move_seat decides, and says nothing when the request is held back).
void Application::net_move_seat(uint8_t seat) {
    if (net_) net_->request_move_seat(seat);
}

// START on the setup screen of a room (host only): the map file's hash goes with the Start message so that every machine checks its own copy.
void Application::net_start_from_setup(const std::string& map_path) {
    if (!net_ || !net_->is_host()) return;
    std::string filename = map_path;
    const size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    uint64_t hash = 0;
    if (!net::hash_file(map_path, hash)) {
        std::cerr << "[Application] Cannot read the map file: " << map_path << std::endl;
        return;
    }
    net_->set_map(filename);
    // The host's own START with a fill level (protocol 11): the empty seats get bots first, which this machine runs from the moment the match begins (as for --bot); one player is then
    // enough. With Fog of War nothing is seated (the status line says why). A START that does not go through takes them out again.
    std::vector<uint8_t> filled;
    if (config_.fill_bots.any()) net_->fill_bots(config_.fill_bots, &filled);
    std::random_device rd;
    if (!net_->start_match(static_cast<uint32_t>(rd()), hash)) {
        for (const uint8_t seat : filled) net_->remove_bot(seat);
        play_effect(sim::SoundID::CantGo);                                   // not enough players yet
        return;
    }
    fill_specs_.clear();
    for (const uint8_t seat : filled) {                                      // (each seat's bot has the level of its seat)
        const net::FillLevel level = config_.fill_bots.level[seat];
        fill_specs_.push_back(ai::BotSpec{seat, "standard", level == net::FillLevel::Easy ? ai::Level::Easy : level == net::FillLevel::Hard ? ai::Level::Hard : ai::Level::Medium});
    }
    map_select_.lock();                                                      // START ran: the map and the fog option are fixed (+0x130)
}

// The session is over (the player left, the host left, a match ended and its results were closed): back to the local setup screen.
void Application::net_end_session(const std::string& notice) {
    stop_bots();
    held_since_ms_ = -1.0;
    prev_missing_.clear();
    back_name_.clear();
    vote_press_ = 0;
    countdown_deadline_ms_ = -1.0;
    countdown_status_shown_ = -1;
    room_chat_.close();
    room_chat_press_taken_ = false;
    fill_specs_.clear();
    say_sent_ = false;
    if (net_) net_->leave();
    hud_.set_command_sink(nullptr);
    finish_recording();                                       // (a match that was left, or lost, keeps its file when something happened in it)
    hud_.set_roster_mask(0x0F);
    MapSelectScreen::RoomView local;
    local.status = notice;                                    // a notice stays on the setup screen until the next action
    map_select_.set_room(local);
    map_select_.set_chat_panel(MapSelectScreen::ChatPanel{});            // (the local game's screen has no chat box and no fill footer)
    map_select_.set_fill_footer(std::string(), std::string());
    const uint8_t own_team = config_.local_player_id < 4 ? config_.local_player_id : static_cast<uint8_t>(0);      // the local game plays the configured team again, not the seat of the match
    local_player_id_ = own_team;
    map_select_.set_player_team(own_team);
    apply_team_names(config_.team_names, 0x0F);
}

// The lines of a network match (net_overlay.hpp: the way back, the vote, the seats that are missing, the countdown, and today's waiting, lag and out-of-sync lines): remake UI, the original has
// no such text. What the network layer says is looked at here, the rules and the words are the model's.
NetOverlayLine Application::net_overlay_now() const { return net_overlay_line(net_overlay_input()); }

NetOverlayInput Application::net_overlay_input() const {
    NetOverlayInput in;
    if (!network_active() || net_->phase() != net::NetGame::Phase::Playing) return in;
    in.desynced = net_->desynced();
    in.electing = net_->electing();
    in.stalled_ms = net_->stalled_ms();
    const uint8_t slow = net_->laggard();
    if (slow < 4) in.waiting_for = sim_.get_player_name(slow);
    in.catching_up = net_->catching_up();
    if (const std::optional<uint32_t> self_lag = net_->self_lag_behind_ms()) in.self_lag_behind_ms = *self_lag;
    if (const std::optional<net::NetGame::LagNotice> lag = net_->lag_notice()) {
        in.lag_seat = lag->seat;
        in.lag_name = sim_.get_player_name(lag->seat);
        in.lag_behind_ms = lag->behind_ms;
    }
    in.notice = net_->match_notice();                                   // "Bob is the host now." for a few seconds
    // the way back and the held match
    const net::PauseInfo pause = net_->pause_info();
    in.reconnecting = pause.reconnecting;
    in.away_s = pause.away_s;
    in.attempts = pause.attempts;
    in.way_back_catching_up = pause.catching_up;
    in.held_ms = held_since_ms_ >= 0.0 ? static_cast<uint32_t>(std::max(0.0, net_time_ms_ - held_since_ms_)) : 0u;
    for (const net::PauseInfo::Seat& seat : pause.missing) in.missing.push_back(NetOverlaySeat{seat.seat, seat.name, seat.away_s, seat.catching_up, seat.progress});
    in.vote_open = pause.vote_open;
    in.vote_seat = pause.vote_seat;
    in.vote_name = pause.vote_name;
    in.votes_continue = pause.votes_continue;
    in.voters = pause.voters;
    in.my_vote = pause.my_vote == net::PauseInfo::Choice::KeepWaiting ? NetOverlayInput::Choice::KeepWaiting
                 : pause.my_vote == net::PauseInfo::Choice::Continue  ? NetOverlayInput::Choice::Continue
                                                                       : NetOverlayInput::Choice::None;
    in.resume_seconds_left = pause.resume_seconds_left;
    in.back_name = back_name_;
    in.max_width = net_overlay_max_width(layout_.view());               // (a name that does not fit is cut: every line and every label fits the overlay at either picture)
    if (renderer_) in.measure = [this](const std::string& text) { return renderer_->get_text_width(text, FontSize::Px14); };
    return in;
}

NetOverlayMetrics Application::net_overlay_metrics(const NetOverlayLine& line) const {
    NetOverlayMetrics m;
    if (!renderer_) return m;
    for (const std::string& text : line.lines) m.line_w.push_back(renderer_->get_text_width(text, FontSize::Px14));
    m.text_h = renderer_->get_text_height(FontSize::Px14);
    if (line.vote.open) {
        m.count_w = renderer_->get_text_width(line.vote.count, FontSize::Px14);
        m.keep_w = renderer_->get_text_width(line.vote.keep, FontSize::Px14);
        m.go_on_w = renderer_->get_text_width(line.vote.go_on, FontSize::Px14);
    }
    return m;
}

Application::NetVoteButtons Application::net_vote_buttons() const {
    NetVoteButtons out;
    const NetOverlayLine line = net_overlay_now();
    if (!line.vote.open) return out;
    const NetOverlayLayout where = net_overlay_layout(layout_.view(), net_overlay_metrics(line), true);
    out.open = true;
    out.keep = where.keep;
    out.go_on = where.go_on;
    return out;
}

// The catch-up screen (page_layout.hpp): the machine runs the match from the server's log without drawing it. A machine that starts the match again (NetGame's BadRequest fallback) has it from the
// moment the old session ends: the loading picture, never the match that was reset behind it.
bool Application::catch_up_screen_active() const {
    if (state_ != AppState::Playing || scorecard_.is_open()) return false;
    if (catch_up_forced_) return true;
    if (!network_active()) return false;
    switch (net_->phase()) {
        case net::NetGame::Phase::Playing: return net_->pause_info().catching_up;
        case net::NetGame::Phase::Connecting:
        case net::NetGame::Phase::Loading: return true;
        default: return false;
    }
}

int32_t Application::catch_up_percent() const {
    if (!network_active() || net_->phase() != net::NetGame::Phase::Playing) return 0;
    return net_->pause_info().catch_up_percent;
}

void Application::render_catch_up_screen() {
    renderer_->set_picture(canvas().page());                             // (the loading screen is the whole canvas, or the 960 x 540 page centred in a bigger one, whatever picture the match has)
    draw_catch_up_screen(*renderer_, assets_, wide_pages(), catch_up_percent());
    renderer_->set_picture(picture_);
    hud_.render_quit_dialog_alone(*renderer_, assets_);                  // (Esc asked: the dialog is the match's own, over this picture)
}

void Application::render_net_overlay() {
    const NetOverlayLine line = net_overlay_now();
    if (line.lines.empty() && !line.vote.open) return;
    using ants::assets::ColorRGBA;
    const ColorRGBA white{255, 255, 255, 255};
    const ColorRGBA red{255, 90, 90, 255};
    const ColorRGBA shade{0, 0, 0, 170};                                // (the dark box that every line has)
    const NetOverlayLayout where = net_overlay_layout(layout_.view(), net_overlay_metrics(line), line.vote.open);   // centred in the map view, 5 rows below its top (the original's (17 + (441 - w) / 2, 26))
    for (size_t i = 0; i < line.lines.size() && i < where.lines.size(); ++i) {
        renderer_->fill_rect(where.lines[i].box.x, where.lines[i].box.y, where.lines[i].box.w, where.lines[i].box.h, shade);
        renderer_->draw_text(line.lines[i], where.lines[i].text_x, where.lines[i].text_y, i == 0 && line.alarm ? red : white, FontSize::Px14);
    }
    if (!where.has_vote) return;
    renderer_->fill_rect(where.count.box.x, where.count.box.y, where.count.box.w, where.count.box.h, shade);
    renderer_->draw_text(line.vote.count, where.count.text_x, where.count.text_y, white, FontSize::Px14);
    // the two buttons: the choice that this player made is the one that is pressed (a lit face and a white edge); the other is dark
    const auto button = [&](const LayoutRect& rect, const std::string& label, int32_t text_x, bool pressed) {
        renderer_->fill_rect(rect.x, rect.y, rect.w, rect.h, pressed ? ColorRGBA{52, 124, 102, 240} : ColorRGBA{24, 40, 36, 215});
        renderer_->draw_rect(rect.x, rect.y, rect.w, rect.h, pressed ? ColorRGBA{235, 245, 240, 255} : ColorRGBA{90, 130, 110, 255});
        renderer_->draw_text(label, text_x, where.button_text_y, white, FontSize::Px14);
    };
    button(where.keep, line.vote.keep, where.keep_text_x, line.vote.keep_pressed);
    button(where.go_on, line.vote.go_on, where.go_on_text_x, line.vote.go_on_pressed);
}

// The seat that the room gave this machine (the waiting room's Room message, or the Welcome of a rejoin at its Start) is told once, and again when it changes
void Application::note_seat() {
    const net::NetGame::Phase phase = net_->phase();
    if (phase != net::NetGame::Phase::Room && phase != net::NetGame::Phase::Loading && phase != net::NetGame::Phase::Playing) return;
    const uint8_t seat = net_->my_seat();
    if (seat >= sim::MAX_PLAYERS || seat == seat_told_) return;
    seat_told_ = seat;
    if (on_seat_known_) on_seat_known_(seat);
}

// Every frame of a match of the network: how long the match has been held on this screen (the missing seats are shown after a second), and who came back (the countdown that follows a return
// names the seat: "Bob is back: the match goes on in 7"). A seat that was catching up when the list of the missing emptied is back; one that was still absent was dropped by the vote or the
// cap, and nobody is named. This machine's own way back says nothing of the others: what was known of them is old.
void Application::track_net_state() {
    if (!net_ || !net_->active() || net_->phase() != net::NetGame::Phase::Playing) {
        held_since_ms_ = -1.0;
        prev_missing_.clear();
        back_name_.clear();
        return;
    }
    const net::PauseInfo p = net_->pause_info();
    const bool own_way_back = p.reconnecting || p.catching_up;
    if (p.catching_up) catch_up_backlog_ = true;                         // (the turns that it runs now are not heard: see post_tick)
    const bool held = !own_way_back && (!p.missing.empty() || p.resume_seconds_left > 0);
    if (!held) held_since_ms_ = -1.0;
    else if (held_since_ms_ < 0.0) held_since_ms_ = net_time_ms_;
    if (own_way_back) {
        prev_missing_.clear();
        back_name_.clear();
        return;
    }
    if (!p.missing.empty()) {
        prev_missing_ = p.missing;
        back_name_.clear();
        return;
    }
    if (!prev_missing_.empty()) {
        size_t back = 0;
        std::string name;
        for (const net::PauseInfo::Seat& seat : prev_missing_) {
            if (!seat.catching_up) continue;
            ++back;
            name = seat.name.empty() ? "Player " + std::to_string(static_cast<unsigned>(seat.seat) + 1u) : seat.name;
        }
        back_name_ = back == 1 ? name : std::string();
        prev_missing_.clear();
    }
    if (p.resume_seconds_left == 0) back_name_.clear();                  // (the countdown is over, or there was none: a blip)
}

// The network's part of the corner (ants_app/latency_corner.hpp): "ping NN ms" and "delay NN ms" while a room or a match of a network game is on screen. A game of one
// machine, the loading and quick help screens, a connection that is being made, one that failed or is over draw nothing here.
// What the corner's "delay" says. With the prediction on it is what the click FEELS: from the frame that took the order to the frame that shows it (a dash until an order has been felt, and for ten
// seconds after the last one); the network's delay, the time until the CONFIRMED engine applies the order, is what it was before, and is what the player feels when the prediction is off
std::optional<uint32_t> Application::corner_delay_ms() const {
    if (!network_active()) return std::nullopt;
    if (net_->predicting()) {
        if (felt_delay_.stale(frame_ms())) return std::nullopt;
        return felt_delay_.felt_ms();
    }
    return net_->command_delay_ms();
}

void Application::render_latency_corner(int32_t version_x, int32_t text_y, const CornerPlate& plate) {
    if (!network_active()) return;
    const CornerScreen screen = state_ == AppState::MapSelect ? CornerScreen::Setup
                                : state_ == AppState::Playing ? (scorecard_.is_open() ? CornerScreen::Results : CornerScreen::Match)
                                                              : CornerScreen::Other;
    const std::optional<int32_t> left_limit = latency_left_limit(true, net_->phase(), screen, layout_);
    if (!left_limit) return;
    LatencyReadout readout;
    readout.ping_ms = net_->ping_ms();
    readout.delay_ms = corner_delay_ms();
    last_latency_layout_ = latency_corner_layout(*renderer_, readout, version_x, text_y, *left_limit, plate);
    draw_latency_corner(*renderer_, readout, version_x, text_y, *left_limit, plate);
}

}  // namespace ants::app
