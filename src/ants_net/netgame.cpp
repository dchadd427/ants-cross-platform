#include "ants_net/netgame.hpp"

#include <fstream>

#include "ants_sim/game_strings.hpp"

#ifndef __EMSCRIPTEN__
#include "ants_net/tcp.hpp"
#endif

namespace ants::net {

bool hash_file(const std::string& path, uint64_t& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    uint64_t h = 0xcbf29ce484222325ULL;
    char buf[8192];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        for (std::streamsize i = 0; i < f.gcount(); ++i) {
            h ^= static_cast<uint8_t>(buf[i]);
            h *= 0x100000001b3ULL;
        }
        if (f.eof()) break;
    }
    if (f.bad()) return false;
    out = h;
    return true;
}

struct NetGame::Transport {
#ifndef __EMSCRIPTEN__
    std::unique_ptr<TcpListener> listener;                  // host: the room's door (closed when the match begins: no late join)
    std::vector<std::unique_ptr<TcpConnection>> guests;     // host: every accepted connection (the lobby and the session borrow them)
    std::unique_ptr<TcpConnection> uplink;                  // client: the connection to the host
#endif
};

namespace {

namespace str = ants::sim::strings;

// Why a join failed: the original's words where it has them (dropped from the game, unable to connect), the remake's for the rest
std::string reject_text(RejectReason r) {
    switch (r) {
        case RejectReason::Full: return "The room is full.";
        case RejectReason::VersionMismatch: return "This version cannot play with the host's version.";
        case RejectReason::MatchRunning: return "The match has already started.";
        case RejectReason::Kicked: return str::text(str::kDroppedFromGame);
        default: return "The host refused the connection.";
    }
}

}  // namespace

NetGame::NetGame(sim::SimulationEngine& sim) : sim_(sim) {}

NetGame::~NetGame() { shutdown_transport(); }

void NetGame::shutdown_transport() {
    host_session_.reset();
    client_session_.reset();
    host_lobby_.reset();
    client_lobby_.reset();
    if (transport_) {
#ifndef __EMSCRIPTEN__
        for (auto& c : transport_->guests) {
            if (c) c->close();
        }
        if (transport_->uplink) transport_->uplink->close();
#endif
        transport_.reset();
    }
}

bool NetGame::host(uint16_t port, const std::string& name, bool loopback_only) {
#ifdef __EMSCRIPTEN__
    (void)port;
    (void)name;
    (void)loopback_only;
    return false;
#else
    if (role_ != Role::None) return false;
    auto listener = TcpListener::listen(port, loopback_only);
    if (!listener) return false;
    transport_ = std::make_unique<Transport>();
    listen_port_ = listener->port();
    transport_->listener = std::move(listener);
    HostLobby::Config cfg;
    cfg.host_name = name;
    cfg.host_seat = 0;
    host_lobby_ = std::make_unique<HostLobby>(cfg);
    role_ = Role::Host;
    phase_ = Phase::Room;
    name_ = name;
    seat_ = cfg.host_seat;
    room_ = host_lobby_->room();
    room_.you = seat_;
    phase_since_ms_ = now_;
    refresh_status();
    desync_reported_ = false;
    return true;
#endif
}

bool NetGame::join(const std::string& address, uint16_t port, const std::string& name) {
#ifdef __EMSCRIPTEN__
    (void)address;
    (void)port;
    (void)name;
    return false;
#else
    if (role_ != Role::None) return false;
    auto conn = TcpConnection::connect(address, port);
    if (!conn) return false;
    transport_ = std::make_unique<Transport>();
    transport_->uplink = std::move(conn);
    ClientLobby::Config cfg;
    cfg.name = name;
    client_lobby_ = std::make_unique<ClientLobby>(transport_->uplink.get(), cfg);
    role_ = Role::Client;
    phase_ = Phase::Connecting;
    name_ = name;
    phase_since_ms_ = now_;
    refresh_status();
    desync_reported_ = false;
    return true;
#endif
}

void NetGame::leave() {
    if (role_ == Role::Client) {
        if (client_session_) client_session_->leave();
        else if (client_lobby_) client_lobby_->leave();
    }
    shutdown_transport();
    role_ = Role::None;
    phase_ = Phase::Off;
    seat_ = 255;
    status_.clear();
}

std::vector<NetGame::Event> NetGame::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void NetGame::set_notice(std::string text) {
    notice_ = std::move(text);
    notice_until_ms_ = now_ + 5000;                            // like the status line of a match: five seconds
}

// The status line of the setup screen, from the original's string table (docs 5.47): the state of the room decides the text; a guest that is still
// connecting moves on to "Having trouble..." after 30 s and "Unable to connect..." after 60 s, exactly as the original's 30 s / 60 s timers do.
void NetGame::refresh_status() {
    if (!notice_.empty() && now_ < notice_until_ms_ && (phase_ == Phase::Room || phase_ == Phase::Loading)) {
        status_ = notice_;
        return;
    }
    switch (phase_) {
        case Phase::Connecting: {
            const uint32_t waited = now_ - phase_since_ms_;
            status_ = str::text(waited < 30000 ? str::kConnectingToHost : (waited < 60000 ? str::kTroubleConnecting : str::kUnableToConnect));
            break;
        }
        case Phase::Room:
            status_ = str::text(role_ == Role::Host ? str::kPressStart : str::kWaitingForHost);
            break;
        case Phase::Loading:
            status_ = str::text(loaded_reported_ ? str::kWaitingForOthers : str::kLoadingGame);
            break;
        case Phase::Playing:
            if (!desync_reported_) status_.clear();
            break;
        default:
            break;                                             // Failed / Over keep the message of the transition
    }
}

LinkQuality NetGame::seat_quality(uint8_t seat) const noexcept {
    if (seat >= sim::MAX_PLAYERS || room_.slots[seat].state == SlotState::Empty) return LinkQuality::Unknown;
    if (room_.slots[seat].state == SlotState::Host) return LinkQuality::Good;
    return link_quality(room_.slots[seat].rtt_ms);
}

void NetGame::update(uint32_t now_ms) {
    now_ = now_ms;
    if (role_ == Role::Host) update_host();
    else if (role_ == Role::Client) update_client();
    refresh_status();
    // how long the game has been waiting for the next turn
    const bool stalled_now = phase_ == Phase::Playing && stalled();
    if (stalled_now && !stall_active_) stall_since_ms_ = now_ms;
    stall_active_ = stalled_now;
}

void NetGame::update_host() {
#ifndef __EMSCRIPTEN__
    if ((phase_ == Phase::Room || phase_ == Phase::Loading) && transport_ && transport_->listener) {
        while (auto conn = transport_->listener->accept()) {
            host_lobby_->add_connection(conn.get(), now_);
            transport_->guests.push_back(std::move(conn));
        }
    }
#endif
    if ((phase_ == Phase::Room || phase_ == Phase::Loading) && host_lobby_) {
        host_lobby_->update(now_);
        room_ = host_lobby_->room();                     // the thumbs move with the measurements
        room_.you = seat_;
        for (const HostLobby::Event& ev : host_lobby_->take_events()) {
            switch (ev.type) {
                case HostLobby::Event::Type::Joined:
                case HostLobby::Event::Type::Left:
                    events_.push_back(Event{Event::Type::RoomChanged, ev.seat});
                    break;
                case HostLobby::Event::Type::LoadFailed:
                case HostLobby::Event::Type::Cancelled: {
                    phase_ = Phase::Room;
                    phase_since_ms_ = now_;
                    loaded_reported_ = false;
                    if (ev.type == HostLobby::Event::Type::LoadFailed && ev.seat < sim::MAX_PLAYERS) {
                        const std::string who = start_.names[ev.seat].empty() ? std::string("A player") : start_.names[ev.seat];
                        set_notice(ev.seat == seat_ ? str::format(str::kMapFileMissing, start_.map_name) : str::format(str::kPeerMapMissing, who, start_.map_name));
                    } else if (notice_until_ms_ <= now_) {                 // (a load failure reports both events: the first one's text stays)
                        set_notice("The start was cancelled: a player left.");
                    }
                    events_.push_back(Event{Event::Type::Cancelled, ev.seat});
                    break;
                }
                case HostLobby::Event::Type::Begun:
                    begin_match();
                    break;
                default:
                    break;
            }
            if (phase_ == Phase::Playing) break;
        }
    }
    if (phase_ == Phase::Playing && host_session_) {
        host_session_->update(now_);
        if (!desync_reported_ && !host_session_->desyncs().empty()) {
            desync_reported_ = true;
            status_ = "The game is out of sync.";
            events_.push_back(Event{Event::Type::Desync, host_session_->desyncs()[0].player});
        }
    }
}

void NetGame::update_client() {
    if ((phase_ == Phase::Connecting || phase_ == Phase::Room || phase_ == Phase::Loading) && client_lobby_) {
        client_lobby_->update(now_);
        for (const ClientLobby::Event& ev : client_lobby_->take_events()) {
            switch (ev.type) {
                case ClientLobby::Event::Type::RoomChanged:
                    if (phase_ == Phase::Connecting) {
                        phase_ = Phase::Room;
                        phase_since_ms_ = now_;
                    }
                    room_ = client_lobby_->room();
                    seat_ = client_lobby_->my_seat();
                    events_.push_back(Event{Event::Type::RoomChanged, 255});
                    break;
                case ClientLobby::Event::Type::StartRequested:
                    phase_ = Phase::Loading;
                    phase_since_ms_ = now_;
                    loaded_reported_ = false;
                    start_ = client_lobby_->start_info();
                    events_.push_back(Event{Event::Type::StartRequested, 255});
                    break;
                case ClientLobby::Event::Type::Begun:
                    begin_match();
                    break;
                case ClientLobby::Event::Type::Cancelled: {
                    phase_ = Phase::Room;
                    phase_since_ms_ = now_;
                    loaded_reported_ = false;
                    const uint8_t who = client_lobby_->cancel_player();
                    if (client_lobby_->cancel_reason() == CancelMsg::Reason::LoadFailed && who < sim::MAX_PLAYERS) {
                        const std::string name = start_.names[who].empty() ? std::string("A player") : start_.names[who];
                        set_notice(str::format(str::kPeerMapMissing, name, start_.map_name));
                    } else {
                        set_notice("The start was cancelled.");
                    }
                    events_.push_back(Event{Event::Type::Cancelled, who});
                    break;
                }
                case ClientLobby::Event::Type::Rejected:
                    phase_ = Phase::Failed;
                    status_ = reject_text(client_lobby_->reject_reason());
                    events_.push_back(Event{Event::Type::Failed, 255});
                    break;
                case ClientLobby::Event::Type::Disconnected:
                    status_ = phase_ == Phase::Connecting ? str::text(str::kUnableToConnect) : std::string("The host closed the room.");
                    phase_ = Phase::Failed;
                    events_.push_back(Event{Event::Type::Failed, 255});
                    break;
            }
            if (phase_ == Phase::Playing || phase_ == Phase::Failed) break;
        }
    }
    if (phase_ == Phase::Playing && client_session_) {
        client_session_->update(now_);
        if (!desync_reported_ && client_session_->desynced()) {
            desync_reported_ = true;
            status_ = "The game is out of sync.";
            events_.push_back(Event{Event::Type::Desync, client_session_->desync().player});
        }
        if (!client_session_->connected()) {
            phase_ = Phase::Over;
            status_ = "The host left the game.";
            events_.push_back(Event{Event::Type::HostLeft, 255});
        }
    }
}

void NetGame::begin_match() {
#ifndef __EMSCRIPTEN__
    if (role_ == Role::Host) {
        HostSession::Config hc;
        hc.host_player = seat_;
        host_session_ = std::make_unique<HostSession>(sim_, hc);
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (Connection* c = host_lobby_->connection_of(s)) host_session_->add_client(s, c);
        }
        transport_->listener.reset();                       // no late join: the door closes when the match begins
        install_hooks();
        host_session_->start(now_);
    } else {
        ClientSession::Config cc;
        cc.player = seat_;
        client_session_ = std::make_unique<ClientSession>(sim_, cc);
        client_session_->set_connection(transport_->uplink.get());
        install_hooks();
        client_session_->start(now_);
    }
    phase_ = Phase::Playing;
    loaded_reported_ = false;
    status_.clear();
    events_.push_back(Event{Event::Type::Begun, 255});
#endif
}

LockstepRunner* NetGame::runner() const {
    if (host_session_) return &host_session_->runner();
    if (client_session_) return &client_session_->runner();
    return nullptr;
}

void NetGame::install_hooks() {
    LockstepRunner* r = runner();
    if (r == nullptr) return;
    r->set_on_tick([this]() {
        if (on_tick_) on_tick_();
    });
    r->set_on_command([this](const sim::Command& c, const sim::CommandResult& res) {
        if (c.type == sim::CommandType::Drop) events_.push_back(Event{Event::Type::PlayerLeft, c.issuer});
        if (on_command_) on_command_(c, res);
    });
    auto chat = [this](const ChatMsg& m) {
        if (on_chat_) on_chat_(m);
    };
    if (host_session_) host_session_->set_on_chat(chat);
    if (client_session_) client_session_->set_on_chat(chat);
}

void NetGame::set_on_tick(std::function<void()> fn) { on_tick_ = std::move(fn); }

void NetGame::set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn) { on_command_ = std::move(fn); }

// ---- the host's controls -------------------------------------------------------------------------------------------------------------------------

void NetGame::set_map(const std::string& map_name) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->set_map(map_name);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

void NetGame::set_fog(bool fog) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->set_fog(fog);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

// START needs a second player and "all players' thumbs have appeared": every guest has been measured at least once
bool NetGame::can_start() const {
    return role_ == Role::Host && phase_ == Phase::Room && host_lobby_ && host_lobby_->can_start() && host_lobby_->all_measured();
}

bool NetGame::start_match(uint32_t seed, uint64_t map_hash) {
    if (!can_start()) return false;
    if (!host_lobby_->start(seed, map_hash, now_)) return false;
    phase_ = Phase::Loading;
    phase_since_ms_ = now_;
    loaded_reported_ = false;
    start_ = host_lobby_->start_info();
    events_.push_back(Event{Event::Type::StartRequested, 255});
    return true;
}

void NetGame::report_loaded(bool ok) {
    if (phase_ != Phase::Loading) return;
    loaded_reported_ = ok;
    if (role_ == Role::Host && host_lobby_) {
        host_lobby_->host_loaded(ok);
    } else if (role_ == Role::Client && client_lobby_) {
        client_lobby_->report_loaded(ok);
        if (!ok) {                                                // a guest that cannot load is back in the room at once (the host cancels for everybody)
            phase_ = Phase::Room;
            phase_since_ms_ = now_;
            set_notice(str::format(str::kMapFileMissing, start_.map_name));
            events_.push_back(Event{Event::Type::Cancelled, seat_});
        }
    }
}

// ---- the match -----------------------------------------------------------------------------------------------------------------------------------

sim::CommandResult NetGame::submit(const sim::Command& command) {
    sim::CommandResult result;
    if (phase_ != Phase::Playing || !sim::is_client_command(command.type)) return result;      // status Ignored
    sim::Command c = command;
    c.issuer = seat_;
    result.ack_ant = sim_.predict_order_ack(c);                                                 // the immediate feedback of the click
    result.status = sim::CommandResult::Status::Applied;                                        // optimistic: the turn decides
    if (host_session_) host_session_->submit_local(std::move(c));
    else if (client_session_) client_session_->submit(std::move(c));
    return result;
}

void NetGame::chat(const std::string& text, bool team) {
    if (phase_ != Phase::Playing || text.empty()) return;
    const std::string body = text.size() > kMaxChatChars ? text.substr(0, kMaxChatChars) : text;
    if (host_session_) host_session_->chat_local(body, team);
    else if (client_session_) client_session_->chat(body, team);
}

void NetGame::freeze() {
    if (host_session_) host_session_->freeze();
}

bool NetGame::stalled() const {
    const LockstepRunner* r = runner();
    return r != nullptr && r->stalled();
}

uint32_t NetGame::stalled_ms() const { return stall_active_ ? now_ - stall_since_ms_ : 0u; }

uint8_t NetGame::laggard() const { return host_session_ ? host_session_->laggard() : uint8_t{255}; }

bool NetGame::desynced() const {
    if (host_session_) return !host_session_->desyncs().empty();
    if (client_session_) return client_session_->desynced();
    return false;
}

uint32_t NetGame::sub_tick_ms() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->sub_tick_ms() : 0u;
}

uint32_t NetGame::rtt_ms() const { return client_session_ ? client_session_->rtt_ms() : 0u; }

uint32_t NetGame::turns_executed() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->next_turn_to_execute() : 0u;
}

}  // namespace ants::net
