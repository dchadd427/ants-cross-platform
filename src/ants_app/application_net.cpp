// The Application's side of the network layer's events and of the start of a network match (application.hpp): what the room's events do to the screens, the match that begins on this machine, and the
// countdown card that a room of two or more people opens it with (network protocol 21, docs/VIEW_AND_HUD.md "The countdown card"). The match itself never depends on the card: the host holds the first
// turn, the card is the wait made visible, with the chat open.

#include <algorithm>
#include <cmath>
#include <iostream>

#include "ants_ai/bot_names.hpp"
#include "ants_app/application.hpp"
#include "ants_app/countdown_card.hpp"

namespace ants::app {

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

}  // namespace ants::app
