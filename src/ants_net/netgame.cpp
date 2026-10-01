#include "ants_net/netgame.hpp"

#include <fstream>
#include <random>

#include "ants_sim/game_strings.hpp"

#ifndef __EMSCRIPTEN__
#include "ants_net/lan.hpp"
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
    std::unique_ptr<TcpConnection> uplink;                  // client: the connection to the host
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

// Why a join failed: the original's words where it has them (dropped from the game, unable to connect), the remake's for the rest
std::string reject_text(RejectReason r) {
    switch (r) {
        case RejectReason::Full: return "The room is full.";
        case RejectReason::VersionMismatch: return "This version cannot play with the host's version.";
        case RejectReason::MatchRunning: return "The match has already started.";
        case RejectReason::Kicked: return str::text(str::kDroppedFromGame);
        case RejectReason::NoSuchRoom: return "There is no such room on this server.";
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
        if (transport_->announcer) transport_->announcer->goodbye();        // the room is gone: the lists of the other machines drop it at once
        for (auto& c : transport_->guests) {
            if (c) c->close();
        }
        if (transport_->uplink) transport_->uplink->close();
        close_peer_links();
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
    transport_ = std::make_unique<Transport>();
    transport_->uplink = std::move(conn);
    transport_->peer_listener = TcpListener::listen(0, false);          // where the other guests reach us during the match (host migration)
    peer_port_ = transport_->peer_listener ? transport_->peer_listener->port() : uint16_t{0};
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
    if (role_ == Role::Host) {
        update_host();
        announce_room();
    } else if (role_ == Role::Client) {
        update_client();
    }
    refresh_status();
    // how long the game has been waiting for the next turn
    const bool stalled_now = phase_ == Phase::Playing && stalled();
    if (stalled_now && !stall_active_) stall_since_ms_ = now_ms;
    stall_active_ = stalled_now;
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
            status_ = "The connection to the other players was lost.";
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
    uint8_t host = 0;
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
        host_session_ = std::make_unique<HostSession>(sim_, hc);
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (Connection* c = host_lobby_->connection_of(s)) host_session_->add_client(s, c);
        }
        transport_->listener.reset();                       // no late join: the door closes when the match begins
        known_host_ = seat_;
        install_hooks();
        host_session_->start(now_);
    } else {
        ClientSession::Config cc;
        cc.player = seat_;
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (room_.slots[s].state == SlotState::Host) cc.host = s;
        }
        known_host_ = cc.host;
        client_session_ = std::make_unique<ClientSession>(sim_, cc);
        client_session_->set_connection(transport_->uplink.get());
        install_hooks();
        client_session_->start(now_);
        pump_peers();                                           // hands over the links that were made while the map loaded
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

void NetGame::chat(const std::string& text, bool team) {
    if (phase_ != Phase::Playing || text.empty()) return;
    const std::string body = text.size() > kMaxChatChars ? text.substr(0, kMaxChatChars) : text;
    if (host_session_) host_session_->chat_local(body, team);
    else if (client_session_) client_session_->chat(body, team);
}

void NetGame::freeze() {
    if (host_session_) host_session_->freeze();
    if (client_session_) client_session_->finish();              // the match is over: a host that leaves now is no loss
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


bool NetGame::electing() const { return client_session_ && client_session_->electing(); }

std::string NetGame::match_notice() const { return phase_ == Phase::Playing && now_ < notice_until_ms_ ? notice_ : std::string(); }

uint32_t NetGame::turns_executed() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->next_turn_to_execute() : 0u;
}

}  // namespace ants::net
