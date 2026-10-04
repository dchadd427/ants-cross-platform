#include "ants_net/netgame.hpp"

#include <fstream>
#include <random>

#include "ants_sim/game_strings.hpp"

#ifndef __EMSCRIPTEN__
#include "ants_net/lan.hpp"
#include "ants_net/tcp.hpp"
#else
#include "ants_net/wasm_ws.hpp"
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

std::string match_lost_text(ClientSession::LostReason reason) {
    return reason == ClientSession::LostReason::AwayTooLong ? "You were away too long and were dropped from the match." : "The connection to the other players was lost.";
}

struct NetGame::Transport {
    std::unique_ptr<Connection> uplink;                     // client: the connection to the host (TCP) or to a server (TCP natively, a WebSocket in the browser)
#ifndef __EMSCRIPTEN__
    struct PendingPeer {
        std::unique_ptr<TcpConnection> conn;
        uint8_t seat{255};                                  // outbound: the seat we connect to; inbound: known after its PeerHello
        bool inbound{true};
        uint32_t since_ms{0};
    };
    struct PeerLink {
        std::unique_ptr<TcpConnection> conn;
        uint8_t seat{255};
        bool attached{false};                               // handed to the ClientSession
    };
    std::unique_ptr<TcpListener> listener;                  // host: the room's door (closed when the match begins: no late join)
    std::vector<std::unique_ptr<TcpConnection>> guests;     // host: every accepted connection (the lobby and the session borrow them)
    std::unique_ptr<TcpListener> peer_listener;             // client: where the other guests connect (host migration)
    std::vector<PendingPeer> pending_peers;                 // client: links to the other guests that are being made
    std::vector<PeerLink> peers;                            // client: the links that are made (the session and, after a host change, the new HostSession borrow them)
    std::unique_ptr<LanAnnouncer> announcer;                // host: tells the local network that the room is open (only while it is)
#endif
};

namespace {

namespace str = ants::sim::strings;

#ifndef __EMSCRIPTEN__
constexpr uint32_t kPeerLinkTimeoutMs = 20000;             // a link between guests that is not made in this time is given up

// The address of "a.b.c.d:port" or "[v6]:port" without the port and the brackets
std::string host_part(const std::string& peer) {
    if (peer.empty()) return std::string();
    if (peer[0] == '[') {
        const size_t end = peer.find(']');
        return end == std::string::npos ? std::string() : peer.substr(1, end - 1);
    }
    const size_t colon = peer.rfind(':');
    return colon == std::string::npos ? peer : peer.substr(0, colon);
}
#endif

#ifdef __EMSCRIPTEN__
constexpr bool kInBrowser = true;          // the game runs in a web page (the page's own Reload is a click away)
#else
constexpr bool kInBrowser = false;
#endif

}  // namespace

// Why a join failed: the original's words where it has them (dropped from the game, unable to connect), the remake's for the rest. In a web page the refusal for another version says what a player
// can do about it: the game that is open is the one that was loaded when the tab was opened, and after an update of the server only a reload fetches the current one (the desktop game has its own
// text for this, the start menu's: "Update the game, or wait until the server is updated").
std::string NetGame::reject_text(RejectReason r, bool in_browser) {
    switch (r) {
        case RejectReason::Full: return "The room is full.";
        case RejectReason::VersionMismatch:
            return in_browser ? "This version cannot play with the host's version. Reload the page to update." : "This version cannot play with the host's version.";
        case RejectReason::MatchRunning: return "The match has already started.";
        case RejectReason::Kicked: return str::text(str::kDroppedFromGame);
        case RejectReason::NoSuchRoom: return "There is no such room on this server.";
        case RejectReason::Dropped: return str::text(str::kDroppedFromGame);                       // (protocol 10) a seat that was dropped while its player was away: the original's one text for a dropped machine, string 94
        case RejectReason::RejoinFailed: return "The game could not be rejoined.";                // (protocol 10; the remake's own: the original has no way back)
        case RejectReason::Superseded: return "This game was taken over by another window.";      // (protocol 10; the remake's own)
        default: return "The host refused the connection.";
    }
}

NetGame::NetGame(sim::SimulationEngine& sim) : sim_(sim) {}

NetGame::~NetGame() { shutdown_transport(); }

void NetGame::shutdown_transport() {
    host_session_.reset();
    client_session_.reset();
    host_lobby_.reset();
    client_lobby_.reset();
    if (transport_) {
#ifndef __EMSCRIPTEN__
        if (transport_->announcer) transport_->announcer->goodbye();        // the room is gone: the lists of the other machines drop it at once
        for (auto& c : transport_->guests) {
            if (c) c->close();
        }
        close_peer_links();
#endif
        if (transport_->uplink) transport_->uplink->close();
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
    room_loopback_only_ = loopback_only;
    {
        std::random_device entropy;
        room_id_ = (static_cast<uint32_t>(entropy()) ^ (static_cast<uint32_t>(entropy()) << 16)) | 1u;     // never 0
    }
    seat_ = cfg.host_seat;
    room_ = host_lobby_->room();
    room_.you = seat_;
    phase_since_ms_ = now_;
    refresh_status();
    desync_reported_ = false;
    return true;
#endif
}

bool NetGame::join(const std::string& address, uint16_t port, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token) {
#ifdef __EMSCRIPTEN__
    (void)address;
    (void)port;
    (void)name;
    (void)want_seat;
    (void)room;
    (void)token;
    return false;
#else
    if (role_ != Role::None) return false;
    auto conn = TcpConnection::connect(address, port);
    if (!conn) return false;
    auto peer_listener = TcpListener::listen(0, false);                 // where the other guests reach us during the match (host migration)
    const uint16_t peer_port = peer_listener ? peer_listener->port() : uint16_t{0};
    begin_client(std::move(conn), peer_port, name, want_seat, room, token);
    transport_->peer_listener = std::move(peer_listener);
    return true;
#endif
}

bool NetGame::join_url(const std::string& url, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token) {
#ifdef __EMSCRIPTEN__
    if (role_ != Role::None) return false;
    auto conn = WasmWsConnection::connect(url);
    if (!conn) return false;
    WasmWsConnection* raw = conn.get();
    begin_client(std::move(conn), 0, name, want_seat, room, token);       // no port for the other guests: a server's room has no links between guests
    raw->set_on_open([this]() {                                           // the Hello goes out when the socket opens, not at the next frame: a page that is not drawn
        if (client_lobby_) client_lobby_->send_hello();                   // runs no frames, and the server closes a connection that says nothing for 10 s
    });
    raw->set_on_wake([this]() {                                           // news from the server (a message, an error, the close) reaches the application at once, also
        const std::function<void()> wake = on_wake_;                      // while the page is hidden; the application's step may end the session and with it this very
        if (wake) wake();                                                 // connection and its callbacks: so the function runs from a copy
    });
    return true;
#else
    (void)url;
    (void)name;
    (void)want_seat;
    (void)room;
    (void)token;
    return false;                                                         // (a native client joins with TCP)
#endif
}

void NetGame::begin_client(std::unique_ptr<Connection> uplink, uint16_t peer_port, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token) {
    transport_ = std::make_unique<Transport>();
    transport_->uplink = std::move(uplink);
    peer_port_ = peer_port;
    ClientLobby::Config cfg;
    cfg.name = name;
    cfg.listen_port = peer_port_;
    cfg.want_seat = want_seat;
    cfg.room = room;
    cfg.token = token;
    client_lobby_ = std::make_unique<ClientLobby>(transport_->uplink.get(), cfg);
    role_ = Role::Client;
    phase_ = Phase::Connecting;
    phase_since_ms_ = now_;
    refresh_status();
    desync_reported_ = false;
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
    fail_reason_ = FailReason::None;
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
            if ((role_ == Role::Host || is_leader()) && fill_ != FillLevel::None) status_ = start_prompt(fill_, room_.fog);        // (the bots make up the seats: no thumbs to wait for)
            else status_ = str::text(role_ == Role::Host || is_leader() ? str::kPressStart : str::kWaitingForHost);      // the leader of a server's room has START: the host's prompt
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

std::string NetGame::start_prompt(FillLevel level, bool fog) {
    if (level == FillLevel::None) return str::text(str::kPressStart);
    if (fog) return "Fog of War is on, so START seats no bots.";
    return "Press START: the empty seats get " + fill_level_title(level) + " bots.";
}

LinkQuality NetGame::seat_quality(uint8_t seat) const noexcept {
    if (seat >= sim::MAX_PLAYERS || room_.slots[seat].state == SlotState::Empty) return LinkQuality::Unknown;
    if (room_.slots[seat].state == SlotState::Host) return LinkQuality::Good;
    return link_quality(room_.slots[seat].rtt_ms);
}

bool NetGame::is_leader() const noexcept {
    return role_ == Role::Client && (phase_ == Phase::Room || phase_ == Phase::Loading) && seat_ < sim::MAX_PLAYERS && room_.leader == seat_;
}

bool NetGame::request_start() {
    if (!is_leader() || phase_ != Phase::Room || !client_lobby_) return false;
    size_t players = 0;
    for (const auto& slot : room_.slots) players += slot.state != SlotState::Empty ? 1u : 0u;
    if (players < 2 && fill_ == FillLevel::None) return false;      // "too few players": as the host's START (with a fill the bots make up the rest: one person is enough)
    return client_lobby_->request_start(fill_);
}

// ---- the waiting room's chat (protocol 11) ----------------------------------------------------------------------------------------------------------

const std::vector<ChatLine>& NetGame::pregame_chat() const noexcept {
    static const std::vector<ChatLine> kNone;
    if (host_lobby_) return host_lobby_->chat_log();
    if (client_lobby_) return client_lobby_->chat_log();
    return kNone;
}

std::vector<ChatLine> NetGame::take_pregame_chat() {
    std::vector<ChatLine> out;
    out.swap(pending_chat_);
    return out;
}

// The lines that the lobby has not handed over yet: queued for the application (bounded, like the log), announced as an event, and the latest of somebody else's shown on the status line
// for a few seconds (the notice mechanism of the setup screen: "Ann: hello"; a room's own notice is its text) unless the mirror is off (set_chat_status_mirror: the 16:9 setup screen's chat
// box shows the lines). Nothing is shown for a line of this machine's own: the player knows it.
void NetGame::collect_room_chat() {
    std::vector<ChatLine> lines;
    if (host_lobby_) lines = host_lobby_->take_chat();
    else if (client_lobby_) lines = client_lobby_->take_chat();
    for (ChatLine& line : lines) {
        if (!line.notice() && line.seat == seat_) {
            events_.push_back(Event{Event::Type::Chat, line.seat});
        } else {
            if (chat_status_mirror_) {                                                                         // (the 16:9 setup screen's chat box shows the line: no copy on the status line)
                std::string shown = line.notice() ? line.text : (line.name.empty() ? "Seat " + std::to_string(static_cast<unsigned>(line.seat) + 1u) : line.name) + ": " + line.text;
                if (shown.size() > kStatusNoticeChars) shown = shown.substr(0, kStatusNoticeChars - 3) + "...";       // (a bound on what the notice keeps; the setup screen fits what it draws to the label's two lines by pixels)
                set_notice(std::move(shown));
            }
            events_.push_back(Event{Event::Type::Chat, line.seat});
        }
        pending_chat_.push_back(std::move(line));
    }
    if (pending_chat_.size() > ChatLog::kMaxLines) pending_chat_.erase(pending_chat_.begin(), pending_chat_.begin() + static_cast<std::ptrdiff_t>(pending_chat_.size() - ChatLog::kMaxLines));
}

std::optional<uint32_t> NetGame::ping_ms() const {
    if (role_ == Role::None) return std::nullopt;
    if (is_host()) return 0u;                                                    // the room's owner, or the guest that took over: no link to measure
    const PingMeter* meter = nullptr;
    if (client_session_ && client_session_->ping().measured()) meter = &client_session_->ping();
    else if (client_lobby_ && client_lobby_->ping().measured()) meter = &client_lobby_->ping();       // in the room, and in the first moments of the match
    if (meter == nullptr || meter->stale(now_)) return std::nullopt;       // a reading older than three seconds is not shown (the host answers nothing, the link is stuck)
    return meter->ping_ms();
}

std::optional<uint32_t> NetGame::command_delay_ms() const {
    const CommandDelayMeter* meter = host_session_ ? &host_session_->command_delay() : (client_session_ ? &client_session_->command_delay() : nullptr);
    if (meter == nullptr || meter->stale(now_)) return std::nullopt;       // none yet, or the last one was applied more than ten seconds ago: "delay -"
    return meter->delay_ms();
}

void NetGame::update(uint32_t now_ms) {
    now_ = now_ms;
    if (role_ == Role::Host) {
        update_host();
        announce_room();
    } else if (role_ == Role::Client) {
        update_client();
    }
    refresh_status();
}

void NetGame::note_gap(uint32_t ms) {
    if (phase_ != Phase::Playing || client_session_ == nullptr || !client_session_->note_gap(ms)) return;
    update(now_);                                              // the same reading of the session as every update: a host that stayed silent too long is gone
}

void NetGame::set_discovery(uint16_t udp_port, bool loopback_only) {
    discovery_port_ = udp_port;
    discovery_loopback_only_ = loopback_only;
}

bool NetGame::announcing() const noexcept {
#ifndef __EMSCRIPTEN__
    return transport_ != nullptr && transport_->announcer != nullptr;
#else
    return false;
#endif
}

// The open room tells the local network about itself (lan.hpp) while it waits for players; the start of the match, a host that left and a
// closed room end it with a goodbye (no late join: a running match is not offered to anybody)
void NetGame::announce_room() {
#ifndef __EMSCRIPTEN__
    if (!transport_) return;
    const bool open = role_ == Role::Host && phase_ == Phase::Room && host_session_ == nullptr && discovery_port_ != 0;
    if (!open) {
        if (transport_->announcer) {
            transport_->announcer->goodbye();
            transport_->announcer.reset();
        }
        return;
    }
    if (!transport_->announcer) {
        transport_->announcer = LanAnnouncer::open(discovery_port_, discovery_loopback_only_ || room_loopback_only_);
        if (!transport_->announcer) return;                   // no UDP socket: the room works all the same, the guests type the address
    }
    LanRoomInfo info;
    info.room_id = room_id_;
    info.tcp_port = listen_port_;
    info.version = game_version_;
    info.seats = static_cast<uint8_t>(room_.slots.size());
    uint8_t players = 0;
    for (const auto& slot : room_.slots) players = static_cast<uint8_t>(players + (slot.state != SlotState::Empty ? 1 : 0));
    info.players = players;
    if (seat_ < room_.slots.size()) info.host_name = room_.slots[seat_].name;
    info.map_name = room_.map_name;
    transport_->announcer->set_room(info);
    transport_->announcer->update(now_);
#endif
}

void NetGame::update_host() {
#ifndef __EMSCRIPTEN__
    if ((phase_ == Phase::Room || phase_ == Phase::Loading) && transport_ && transport_->listener) {
        while (auto conn = transport_->listener->accept()) {
            host_lobby_->add_connection(conn.get(), now_, host_part(conn->peer()));
            transport_->guests.push_back(std::move(conn));
        }
    }
#endif
    if ((phase_ == Phase::Room || phase_ == Phase::Loading) && host_lobby_) {
        host_lobby_->update(now_);
        room_ = host_lobby_->room();                     // the thumbs move with the measurements
        room_.you = seat_;
        collect_room_chat();
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
    if (phase_ == Phase::Playing && host_session_) update_host_session();
}

void NetGame::update_host_session() {
    host_session_->update(now_);
    if (!desync_reported_ && !host_session_->desyncs().empty()) {
        desync_reported_ = true;
        status_ = "The game is out of sync.";
        events_.push_back(Event{Event::Type::Desync, host_session_->desyncs()[0].player});
    }
}

void NetGame::update_client() {
    if ((phase_ == Phase::Connecting || phase_ == Phase::Room || phase_ == Phase::Loading) && client_lobby_) {
        client_lobby_->update(now_);
        collect_room_chat();
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
                    begin_peer_links();                          // the links between guests are made while the map loads
                    events_.push_back(Event{Event::Type::StartRequested, 255});
                    break;
                case ClientLobby::Event::Type::Begun:
                    begin_match();
                    break;
                case ClientLobby::Event::Type::Cancelled: {
                    phase_ = Phase::Room;
                    phase_since_ms_ = now_;
                    loaded_reported_ = false;
                    close_peer_links();                          // a new Start makes new ones
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
                    fail_reason_ = FailReason::Rejected;
                    reject_reason_ = client_lobby_->reject_reason();
                    status_ = reject_text(client_lobby_->reject_reason(), kInBrowser);
                    events_.push_back(Event{Event::Type::Failed, 255});
                    break;
                case ClientLobby::Event::Type::Disconnected:
                    fail_reason_ = phase_ != Phase::Connecting ? FailReason::Closed
                                   : (client_lobby_->welcome_timed_out() ? FailReason::NoAnswer : (client_lobby_->was_open() ? FailReason::Lost : FailReason::Unreachable));
                    status_ = phase_ == Phase::Connecting ? str::text(str::kUnableToConnect) : std::string("The host closed the room.");
                    phase_ = Phase::Failed;
                    events_.push_back(Event{Event::Type::Failed, 255});
                    break;
                case ClientLobby::Event::Type::Chat:             // (a line: collect_room_chat above has it already)
                    break;
            }
            if (phase_ == Phase::Playing || phase_ == Phase::Failed) break;
        }
    }
    if (phase_ == Phase::Loading || phase_ == Phase::Playing) pump_peers();
    if (phase_ == Phase::Playing && client_session_) {
        client_session_->update(now_);
        if (!desync_reported_ && client_session_->desynced()) {
            desync_reported_ = true;
            status_ = "The game is out of sync.";
            events_.push_back(Event{Event::Type::Desync, client_session_->desync().player});
        }
        if (client_session_->promoted()) {
            promote();                                            // the host is gone and this machine is the lowest seat left
        } else if (client_session_->lost()) {
            phase_ = Phase::Over;
            status_ = match_lost_text(client_session_->lost_reason());
            events_.push_back(Event{Event::Type::HostLeft, 255});
        } else if (client_session_->host_seat() != known_host_) {   // another guest took over and this one follows it
            known_host_ = client_session_->host_seat();
            const std::string& who = start_.names[known_host_];
            set_notice((who.empty() ? std::string("Another player") : who) + " is the host now.");
            events_.push_back(Event{Event::Type::HostChanged, known_host_});
        }
    }
    if (phase_ == Phase::Playing && host_session_) update_host_session();    // a guest that took over
}

// This machine is the new host: the session keeps the runner (the simulation goes on where it stands), tells the guests that follow, and the old host
// and the seats that did not follow are dropped by its first turn.
void NetGame::promote() {
    host_session_ = promote_to_host(*client_session_, sim_, HostSession::Config{}, now_, [this](HostSession& h) {
        h.set_on_chat([this](const ChatMsg& m) {
            if (on_chat_) on_chat_(m);
        });
    });
    client_session_.reset();
    if (!host_session_) {
        phase_ = Phase::Over;
        status_ = "The connection to the other players was lost.";
        events_.push_back(Event{Event::Type::HostLeft, 255});
        return;
    }
    known_host_ = seat_;
#ifndef __EMSCRIPTEN__
    if (transport_) {
        transport_->peer_listener.reset();                       // nobody joins a match that runs
        transport_->pending_peers.clear();
    }
#endif
    set_notice("You are the host now.");
    events_.push_back(Event{Event::Type::HostChanged, seat_});
}

// ---- the links between guests --------------------------------------------------------------------------------------------------------------------

// A guest connects to every guest above its own seat (the ones below connect to it): one link per pair. The endpoints come with the roster.
void NetGame::begin_peer_links() {
#ifndef __EMSCRIPTEN__
    if (!transport_) return;
    close_peer_links();
    uint8_t host = kNoSeat;                                      // (a dedicated server's room has no Host slot and no endpoints: no links are made)
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (room_.slots[s].state == SlotState::Host) host = s;
    }
    for (uint8_t s = static_cast<uint8_t>(seat_ + 1); s < sim::MAX_PLAYERS; ++s) {
        const Endpoint& e = start_.endpoints[s];
        if (s == host || (start_.roster & (1u << s)) == 0 || e.address.empty() || e.port == 0) continue;
        if (auto conn = TcpConnection::connect(e.address, e.port)) {
            transport_->pending_peers.push_back(Transport::PendingPeer{std::move(conn), s, false, now_});
        }
    }
#endif
}

void NetGame::close_peer_links() {
#ifndef __EMSCRIPTEN__
    if (!transport_) return;
    for (auto& p : transport_->pending_peers) {
        if (p.conn) p.conn->close();
    }
    for (auto& l : transport_->peers) {
        if (l.conn) l.conn->close();
    }
    transport_->pending_peers.clear();
    transport_->peers.clear();
#endif
}

void NetGame::pump_peers() {
#ifndef __EMSCRIPTEN__
    if (!transport_) return;
    Transport& t = *transport_;
    if (t.peer_listener) {
        while (auto conn = t.peer_listener->accept()) t.pending_peers.push_back(Transport::PendingPeer{std::move(conn), 255, true, now_});
    }
    const bool know_roster = phase_ == Phase::Loading || phase_ == Phase::Playing;      // an inbound link is judged by the roster, which arrives with Start
    for (size_t i = 0; i < t.pending_peers.size();) {
        Transport::PendingPeer& p = t.pending_peers[i];
        bool done = false;
        bool made = false;
        if (p.inbound) {
            std::vector<uint8_t> msg;
            if (know_roster && p.conn->poll(msg)) {
                PeerHelloMsg hello;
                done = true;
                if (decode(msg, hello)) {
                    bool taken = false;
                    for (const auto& l : t.peers) taken = taken || l.seat == hello.seat;
                    // only the seats below ours connect to us, only a seat of the roster, only from the address the host reported for it
                    made = !taken && hello.seat < seat_ && (start_.roster & (1u << hello.seat)) != 0 && start_.endpoints[hello.seat].port != 0 &&
                           host_part(p.conn->peer()) == start_.endpoints[hello.seat].address;
                    if (made) p.seat = hello.seat;
                }
                if (!made) p.conn->close();
            } else if (!p.conn->is_open() && p.conn->state() != Connection::State::Connecting) {
                done = true;
            } else if (now_ - p.since_ms > kPeerLinkTimeoutMs) {
                p.conn->close();
                done = true;
            }
        } else {
            std::vector<uint8_t> nothing;
            p.conn->poll(nothing);                              // lets the connection that is being made progress
            if (p.conn->is_open()) {
                PeerHelloMsg hello;
                hello.seat = seat_;
                made = p.conn->send(encode(hello));
                done = true;
                if (!made) p.conn->close();
            } else if (p.conn->state() != Connection::State::Connecting || now_ - p.since_ms > kPeerLinkTimeoutMs) {
                p.conn->close();
                done = true;
            }
        }
        if (made) t.peers.push_back(Transport::PeerLink{std::move(p.conn), p.seat, false});
        if (done) t.pending_peers.erase(t.pending_peers.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    if (client_session_) {                                      // links made before the match began are handed over once the session exists
        for (auto& l : t.peers) {
            if (l.attached) continue;
            client_session_->set_peer(l.seat, l.conn.get());
            l.attached = true;
        }
    }
#endif
}

void NetGame::begin_match() {
#ifndef __EMSCRIPTEN__
    if (role_ == Role::Host) {
        HostSession::Config hc;
        hc.host_player = seat_;
        hc.start_delay_ms = kMatchStartDelayMs;                 // protocol 12: the first turn is sealed when the "Get ready to play!" dialog of every machine has had its 5 s (session.hpp)
        host_session_ = std::make_unique<HostSession>(sim_, hc);
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (Connection* c = host_lobby_->connection_of(s)) host_session_->add_client(s, c);
            if (host_lobby_->room().slots[s].state == SlotState::Bot) host_session_->add_bot_seat(s);      // the computer players: no connection, acknowledged by this host
        }
        transport_->listener.reset();                       // no late join: the door closes when the match begins
        known_host_ = seat_;
        install_hooks();
        host_session_->start(now_);
    } else
#endif
    {                                                           // a guest (the browser build only ever is one)
        ClientSession::Config cc;
        cc.player = seat_;
        cc.host = kNoSeat;
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (room_.slots[s].state == SlotState::Host) cc.host = s;
        }
        cc.migration = cc.host != kNoSeat;                      // a room without a Host slot is a dedicated server's: nobody can take over from it
        known_host_ = cc.host;
        client_session_ = std::make_unique<ClientSession>(sim_, cc);
        client_session_->set_connection(transport_->uplink.get());
        install_hooks();
        client_session_->start(now_);
        pump_peers();                                           // hands over the links that were made while the map loaded (none in the browser)
    }
    phase_ = Phase::Playing;
    loaded_reported_ = false;
    status_.clear();
    events_.push_back(Event{Event::Type::Begun, 255});
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
    if (fog && host_lobby_->has_bot()) {
        set_notice("Bots cannot play with Fog of War.");
        return;
    }
    host_lobby_->set_fog(fog);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

bool NetGame::add_bot(uint8_t seat, const std::string& name) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return false;
    if (host_lobby_->fog()) {
        set_notice("Bots cannot play with Fog of War.");
        return false;
    }
    if (!host_lobby_->add_bot(seat, name)) return false;
    room_ = host_lobby_->room();
    room_.you = seat_;
    return true;
}

void NetGame::remove_bot(uint8_t seat) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->remove_bot(seat);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

size_t NetGame::fill_bots(FillLevel level, std::vector<uint8_t>* seats) {
    if (level == FillLevel::None || role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return 0;
    if (host_lobby_->fog()) {
        set_notice(kNoticeFillFog);
        refresh_status();                                            // (the status line says why at once, not with the next update)
        return 0;
    }
    size_t seated = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (host_lobby_->occupied(seat)) continue;
        if (!host_lobby_->add_bot(seat, fill_bot_name(level))) break;
        if (seats != nullptr) seats->push_back(seat);
        ++seated;
    }
    room_ = host_lobby_->room();
    room_.you = seat_;
    return seated;
}

bool NetGame::submit_bot(uint8_t seat, const sim::Command& command) {
    if (phase_ != Phase::Playing || !host_session_ || !sim::is_client_command(command.type)) return false;
    return host_session_->submit_bot(seat, command);
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
    refresh_status();
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
            close_peer_links();
            set_notice(str::format(str::kMapFileMissing, start_.map_name));
            events_.push_back(Event{Event::Type::Cancelled, seat_});
        }
    }
    refresh_status();                                             // the status line follows at once, not with the next update
}

// ---- the match -----------------------------------------------------------------------------------------------------------------------------------

sim::CommandResult NetGame::submit(const sim::Command& command) {
    sim::CommandResult result;
    if (phase_ != Phase::Playing || !sim::is_client_command(command.type)) return result;      // status Ignored
    sim::Command c = command;
    c.issuer = seat_;
    result.ack_ant = sim_.predict_order_ack(c, &result.needing_order);                          // the immediate feedback of the click
    result.status = sim::CommandResult::Status::Applied;                                        // optimistic: the turn decides
    if (host_session_) {
        host_session_->submit_local(std::move(c));
    } else if (client_session_ && !client_session_->submit(std::move(c))) {
        return sim::CommandResult{};                                                            // no host to send it to (a new one is being chosen): Ignored
    }
    return result;
}

void NetGame::show_notice(std::string text) {
    if (text.size() > kStatusNoticeChars) text = text.substr(0, kStatusNoticeChars - 3) + "...";               // (see collect_room_chat)
    set_notice(std::move(text));
}

bool NetGame::chat(const std::string& text, bool team) {
    if (text.empty()) return false;
    if (phase_ == Phase::Room || phase_ == Phase::Loading) {             // the waiting room (protocol 11): nobody has a team yet, everybody hears it
        if (host_lobby_) return host_lobby_->chat(text);
        return client_lobby_ && client_lobby_->chat(text);
    }
    if (phase_ != Phase::Playing) return false;
    const std::string body = text.size() > kMaxChatChars ? text.substr(0, kMaxChatChars) : text;
    if (host_session_) {
        host_session_->chat_local(body, team);
        return true;
    }
    return client_session_ && client_session_->chat(body, team);
}

void NetGame::freeze() {
    if (host_session_) host_session_->freeze();
    if (client_session_) client_session_->finish();              // the match is over: a host that leaves now is no loss
}

bool NetGame::stalled() const {
    const LockstepRunner* r = runner();
    return phase_ == Phase::Playing && r != nullptr && r->stalled();
}

uint32_t NetGame::stalled_ms() const {
    const LockstepRunner* r = runner();
    return phase_ == Phase::Playing && r != nullptr ? r->stalled_ms() : 0u;
}

uint8_t NetGame::laggard() const { return host_session_ ? host_session_->laggard() : uint8_t{255}; }

std::optional<NetGame::LagNotice> NetGame::lag_notice() const {
    if (phase_ != Phase::Playing || !client_session_) return std::nullopt;
    const uint8_t seat = client_session_->lagging_seat();
    if (seat >= sim::MAX_PLAYERS) return std::nullopt;
    return LagNotice{seat, client_session_->lagging_behind_ms()};
}

bool NetGame::catching_up() const { return phase_ == Phase::Playing && client_session_ && client_session_->catching_up(); }

std::optional<uint32_t> NetGame::self_lag_behind_ms() const {
    if (phase_ != Phase::Playing || !client_session_) return std::nullopt;
    const uint32_t ms = client_session_->self_lag_behind_ms();
    if (ms == 0) return std::nullopt;
    return ms;
}

bool NetGame::desynced() const {
    if (host_session_) return !host_session_->desyncs().empty();
    if (client_session_) return client_session_->desynced();
    return false;
}

uint32_t NetGame::sub_tick_ms() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->sub_tick_ms() : 0u;
}


bool NetGame::electing() const { return client_session_ && client_session_->electing(); }

std::string NetGame::match_notice() const { return phase_ == Phase::Playing && now_ < notice_until_ms_ ? notice_ : std::string(); }

uint32_t NetGame::turns_executed() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->next_turn_to_execute() : 0u;
}

}  // namespace ants::net
