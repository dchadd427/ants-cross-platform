#include "ants_net/netgame.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <utility>

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
    std::unique_ptr<Connection> relink;                     // client: the link that the way back made last (the session holds it; the one before it is let go when a new one is made). The lobby holds the first,
                                                            // `uplink`, for as long as this object lives, so that one is never replaced
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

// A Hello with a create block is one that the server makes the room of when somebody comes (the front page's card and the start menu send them): the server answers NoSuchRoom when it cannot,
// so it is a place that is missing (the cap of public rooms, the server's limit, a server that makes none), never a room that does not exist
constexpr const char* kTextNoPlace = "The server cannot make a room now: it is busy, or hosts no online matches. Try again in a few minutes.";

// "Green", "Red", "Blue", "Black": the colour word of a seat (seat 0 is green, the engine's own numbering), as every page names a seat
std::string seat_colour(uint8_t seat) { return seat < sim::MAX_PLAYERS ? std::string(str::colour_name(static_cast<uint8_t>(3u - seat))) : std::string(); }

// The leader's tap on a player (request_move_seat). A press within kMoveGapMs of the last request is held back: a double click is one press (its second one would act on the rows as the first one
// left them, and move the next player on, or the same one twice). The press after that waits for the room's answer, and the answer is a room that seats its people otherwise; a request that the
// room cannot do is not answered at all, so the wait ends after a second in any case.
constexpr uint32_t kMoveGapMs = 500;                                                  // (the time of a double click)
constexpr uint32_t kMoveAnswerMs = 1000;
constexpr const char* kTextMoveHint = "Tap a player to change their colour.";

}  // namespace

// Why a join failed: the original's words where it has them (dropped from the game, unable to connect), the remake's for the rest. In a web page the refusal for another version says what a player
// can do about it: the game that is open is the one that was loaded when the tab was opened, and after an update of the server only a reload fetches the current one (the desktop game has its own
// text for this, the start menu's: "Update the game, or wait until the server is updated").
std::string NetGame::reject_text(RejectReason r, bool in_browser, bool made_a_room) {
    switch (r) {
        case RejectReason::Full: return "The room is full.";
        case RejectReason::VersionMismatch:
            return in_browser ? "This version cannot play with the host's version. Reload the page to update." : "This version cannot play with the host's version.";
        case RejectReason::MatchRunning: return "The match has already started.";
        case RejectReason::Kicked: return str::text(str::kDroppedFromGame);
        case RejectReason::NoSuchRoom: return made_a_room ? kTextNoPlace : "There is no such room on this server.";
        case RejectReason::Dropped: return str::text(str::kDroppedFromGame);                       // (protocol 10) a seat that was dropped while its player was away: the original's one text for a dropped machine, string 94
        case RejectReason::RejoinFailed: return "The game could not be rejoined.";                // (protocol 10; the remake's own: the original has no way back)
        case RejectReason::Superseded: return "This game was taken over by another window.";      // (protocol 10; the remake's own)
        default: return "The host refused the connection.";
    }
}

namespace {

constexpr const char* kTextGaveUp = "The match could not wait any longer.";                       // the time of the way back ran out (the cap that the room named and a minute, or half an hour from the loss)
constexpr const char* kTextNewRoom = "Your match has ended. This is a new room.";                // the key fits nothing in the room that took the code: this machine is a new player of its waiting room

// The refusals after which the key is of no use: the seat was dropped (or removed), the match is over, the server does not hold the seat. Superseded (the other window has it), RejoinFailed (the server would
// not take the machine back now: it may use Rejoin later) and the rest leave the key where it is.
bool way_back_forgets(RejectReason reason) {
    return reason == RejectReason::Dropped || reason == RejectReason::Kicked || reason == RejectReason::NoSuchRoom || reason == RejectReason::MatchRunning;
}

}  // namespace

// The words of the way back (docs/NETWORK_PORT.md "Reconnect"): what a player is told when the server refuses a machine that comes back with its key. The reasons that mean something else for a machine
// that holds a seat than for a new player have words of their own (the first-join texts, reject_text, are what they were); Superseded says the same in both.
std::string NetGame::way_back_text(RejectReason reason) {
    switch (reason) {
        case RejectReason::Dropped:
        case RejectReason::Kicked: return "You were dropped from the match.";
        case RejectReason::NoSuchRoom: return "The match is over.";
        case RejectReason::MatchRunning: return "The server does not hold your seat.";
        case RejectReason::RejoinFailed: return "The server would not take you back now. Try Rejoin in a minute.";
        default: return reject_text(reason, kInBrowser);
    }
}

NetGame::NetGame(sim::SimulationEngine& sim) : sim_(sim) {}

NetGame::~NetGame() { shutdown_transport(); }

void NetGame::shutdown_transport() {
    drop_prediction();                                          // (it holds the runner of a session: it goes first)
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
        if (transport_->relink) transport_->relink->close();
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
    cfg.host_platform = platform_;                                // (protocol 15: the host's own seat shows what this machine runs on, as a guest's does)
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

bool NetGame::join(const std::string& address, uint16_t port, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token, const SeatKey& key) {
#ifdef __EMSCRIPTEN__
    (void)address;
    (void)port;
    (void)name;
    (void)want_seat;
    (void)room;
    (void)token;
    (void)key;
    return false;
#else
    if (role_ != Role::None) return false;
    // The name is looked up here, once: every link of the way back goes to this address (a lookup at every attempt would hold up the frame, and a name that points elsewhere by then would get the key)
    const std::string numeric = resolver_ ? resolver_(address) : resolve_host(address);
    if (numeric.empty()) return false;
    auto conn = TcpConnection::connect(numeric, port);
    if (!conn) return false;
    auto peer_listener = TcpListener::listen(0, false);                 // where the other guests reach us during the match (host migration)
    const uint16_t peer_port = peer_listener ? peer_listener->port() : uint16_t{0};
    target_ = JoinTarget{address, numeric, port, std::string(), name, want_seat, room, token};        // (the way back of a server's room makes its links from this)
    begin_client(std::move(conn), peer_port, key);
    transport_->peer_listener = std::move(peer_listener);
    return true;
#endif
}

bool NetGame::join_url(const std::string& url, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token, const SeatKey& key) {
#ifdef __EMSCRIPTEN__
    if (role_ != Role::None) return false;
    auto conn = WasmWsConnection::connect(url);
    if (!conn) return false;
    WasmWsConnection* raw = conn.get();
    target_ = JoinTarget{std::string(), std::string(), 0, url, name, want_seat, room, token};
    begin_client(std::move(conn), 0, key);                                // no port for the other guests: a server's room has no links between guests
    raw->set_on_open([this]() {                                           // the Hello goes out when the socket opens, not at the next frame: a page that is not drawn
        if (client_lobby_) client_lobby_->send_hello();                   // runs no frames, and the server closes a connection that says nothing for 10 s
    });
    raw->set_on_wake(wake_function());                                    // news from the server (a message, an error, the close) reaches the application at once, also while the page is hidden
    return true;
#else
    (void)url;
    (void)name;
    (void)want_seat;
    (void)room;
    (void)token;
    (void)key;
    return false;                                                         // (a native client joins with TCP)
#endif
}

ClientLobby::Config NetGame::lobby_config(const SeatKey& key, uint8_t want_seat) const {
    ClientLobby::Config cfg;
    cfg.name = target_.name;
    cfg.listen_port = peer_port_;
    cfg.want_seat = want_seat;
    cfg.room = target_.room;
    cfg.token = target_.token;
    cfg.key = key;                                            // (all zero: a new player; else the Hello shows the key of the seat that this machine had)
    cfg.platform = platform_;
    cfg.create = create_;                                     // (the lobby leaves a block out that no server would read; a keyed Hello never makes a room, the server ignores it there)
    return cfg;
}

void NetGame::begin_client(std::unique_ptr<Connection> uplink, uint16_t peer_port, const SeatKey& key) {
    transport_ = std::make_unique<Transport>();
    transport_->uplink = std::move(uplink);
    peer_port_ = peer_port;
    client_lobby_ = std::make_unique<ClientLobby>(transport_->uplink.get(), lobby_config(key, target_.want_seat));
    reload_used_ = false;
    join_key_ = key;
    welcomed_ = false;
    pending_key_ = SeatKey{};
    start_key_announced_ = false;
    have_key_ = false;
    rejoin_key_ = RejoinKey{};
    last_mode_ = ClientSession::Mode::Normal;
    move_pending_ = 0;
    move_hint_given_ = false;
    if (!key_is_zero(key)) {                                    // (the place that keeps it gave it to this machine: it has to be told when the key is of no use)
        rejoin_key_ = RejoinKey{target_.room, target_.want_seat, key, server_text()};
        have_key_ = true;
    }
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
    if (phase_ != Phase::Over && phase_ != Phase::Failed) forget_key();      // the player left: the seat is gone, and the key with it (a session that ended by itself decided about its key then)
    have_key_ = false;
    shutdown_transport();
    role_ = Role::None;
    phase_ = Phase::Off;
    seat_ = 255;
    status_.clear();
    fail_reason_ = FailReason::None;
    target_ = JoinTarget{};
    way_back_ = false;
    reload_used_ = false;
    join_key_ = SeatKey{};
    welcomed_ = false;
    pending_key_ = SeatKey{};
    start_key_announced_ = false;
    last_mode_ = ClientSession::Mode::Normal;
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
            prompts_.clear();
            if (role_ == Role::Host || is_leader()) prompts_ = start_prompt_texts(fill_, effective_teams(), room_, room_.fog, room_teams().set);          // (the bots make up the seats: no thumbs to wait for)
            if (!prompts_.empty()) status_ = prompts_.front();
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

namespace {

// What a START with a plan and teams would do in this room, as the leader's screens tell it
struct StartFacts {
    std::vector<std::pair<uint8_t, FillLevel>> seats;     // the bots that it seats (none with Fog of War)
    FillLevel same{FillLevel::None};                      // the one level of every empty seat, when it is one
    bool bots{false};                                     // the plan asks for bots (they are refused with Fog of War)
    bool fog{false};
    uint8_t roster{0};                                    // the seats that would play
    bool teams{false};
    bool can_team{false};                                 // ... and the teams can be made for them
    std::string title;                                    // "Green + Red against Blue + Black"
    std::string vs;                                       // "Green + Red vs Blue + Black"
};

StartFacts start_facts(const FillPlan& plan, const sim::StartTeams& teams, const RoomMsg& room, bool fog) {
    StartFacts f;
    f.bots = plan.any();
    f.fog = fog && f.bots;
    if (!f.fog) f.seats = plan_fill_seats(plan, room, sim::MAX_PLAYERS);       // (the client does not know how many players the room expects: the cap is the server's)
    size_t empty_seats = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (room.slots[seat].state == SlotState::Empty) ++empty_seats;
        else f.roster = static_cast<uint8_t>(f.roster | (1u << seat));
    }
    if (plan.uniform()) {
        f.same = plan.level[0];
    } else if (f.seats.size() >= 2 && f.seats.size() == empty_seats) {            // (the plan gives the same level to every empty seat: "the empty seats get Medium bots"; one seat is named by its colour)
        f.same = f.seats[0].second;
        for (const auto& bot : f.seats) f.same = bot.second == f.same ? f.same : FillLevel::None;
    }
    for (const auto& bot : f.seats) f.roster = static_cast<uint8_t>(f.roster | (1u << bot.first));        // (the seats that the bots take play)
    if (teams.set) {
        f.teams = true;
        f.can_team = sim::plan_start_teams(teams, f.roster).why.empty();
        f.title = sim::start_teams_title(teams, f.roster);
        f.vs = f.title;
        const size_t against = f.vs.find(" against ");
        if (against != std::string::npos) f.vs.replace(against, 9, " vs ");
    }
    return f;
}

}  // namespace

// The plan of a START in one line: the bots that it seats (the one level of the empty seats, or each seat's own) and the teams, longest way first. A room whose START seats nothing and makes no teams has
// no line of its own: the original's prompt stands.
std::vector<std::string> NetGame::start_prompt_texts(const FillPlan& plan, const sim::StartTeams& teams, const RoomMsg& room, bool fog, bool room_teams) {
    const StartFacts f = start_facts(plan, teams, room, fog);
    struct Part {
        std::string full;
        std::string brief;
    };
    Part bots;
    if (f.fog) {
        bots = Part{"Fog of War is on, so START seats no bots", "Fog of War: no bots"};
    } else if (f.bots && f.same != FillLevel::None) {
        bots = Part{"the empty seats get " + fill_level_title(f.same) + " bots", "empty seats: " + fill_level_title(f.same) + " bots"};
    } else if (!f.seats.empty()) {
        bots = Part{fill_seats_sentence(f.seats), "bots: " + fill_seats_short(f.seats)};
    }
    Part team;
    if (f.teams) {
        const std::string note = f.can_team ? "" : " (not with these seats)";
        team = room_teams ? Part{"the room's teams: " + f.title + note, "teams " + f.vs + note} : Part{"teams " + f.title + note, "teams " + f.vs + note};
    }
    if (bots.full.empty() && team.full.empty()) return {};
    std::vector<std::string> out;
    const auto add = [&out](std::string text) {
        for (const std::string& have : out) {
            if (have == text) return;
        }
        out.push_back(std::move(text));
    };
    const std::string press = f.fog ? std::string() : std::string("Press START: ");          // (the Fog of War line is the original-style notice, not an invitation)
    const auto join = [](const std::string& a, const std::string& b) { return a.empty() ? b : (b.empty() ? a : a + "; " + b); };
    add(press + join(bots.full, team.full) + ".");                                            // "Press START: Red gets an Easy bot, Black a Hard bot; teams Green + Red against Blue + Black."
    add(join(bots.full, team.full) + ".");
    add(join(bots.full, team.brief) + ".");
    add(join(bots.brief, team.full) + ".");
    add(join(bots.brief, team.brief) + ".");
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return out;
}

// The foot of the leader's Players' Status box (16:9): two lines, each in the ways it can be said (longest first)
NetGame::FooterTexts NetGame::start_footer(const FillPlan& plan, const sim::StartTeams& teams, const RoomMsg& room, bool fog, bool room_teams) {
    const StartFacts f = start_facts(plan, teams, room, fog);
    FooterTexts out;
    const bool bots = !f.fog && (f.same != FillLevel::None || !f.seats.empty()) && f.bots;
    const std::string bots_detail = f.same != FillLevel::None ? fill_level_title(f.same) + " bots" : fill_seats_short(f.seats);
    if (bots && !f.teams) {
        out.line[0] = {"Empty seats at START:"};                                              // (the footer of protocol 11, for one level in every seat: "Empty seats at START:" / "Medium bots")
        out.line[1] = {bots_detail};
    } else if (!bots && f.teams) {
        out.line[0] = {room_teams ? "Room teams:" : "Teams at START:"};
        out.line[1] = {f.title, f.vs};
    } else if (bots && f.teams) {
        out.line[0] = f.same != FillLevel::None ? std::vector<std::string>{"Empty seats: " + bots_detail} : std::vector<std::string>{"Bots: " + bots_detail, bots_detail};
        out.line[1] = {"Teams: " + f.title, f.title, "Teams: " + f.vs, f.vs};
        if (room_teams) out.line[1].insert(out.line[1].begin(), {"Room teams: " + f.title, "Room teams: " + f.vs});      // ("Room teams: ..." where it fits; the shorter ways keep the colours and the teams)
    }
    return out;
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
    if (move_unanswered()) return false;                                              // (the plan of bots is for the colours as the room shows them: it follows the answer of a SeatMove, so a START waits for it)
    size_t players = 0;
    for (const auto& slot : room_.slots) players += slot.state != SlotState::Empty ? 1u : 0u;
    if (players < 2 && plan_fill_seats(fill_, room_, sim::MAX_PLAYERS).empty()) return false;      // "too few players": as the host's START (with bots to seat they make up the rest: one person is enough)
    return client_lobby_->request_start(fill_.level, effective_teams());              // (the room's own teams, the Room message's, win: it ignores these when it has any)
}

uint8_t NetGame::seat_move_target(const RoomMsg& room, uint8_t seat) noexcept {
    if (seat >= sim::MAX_PLAYERS || room.slots[seat].state != SlotState::Client) return 255;
    for (uint8_t step = 1; step < sim::MAX_PLAYERS; ++step) {                         // the next colour that nobody holds ...
        const uint8_t next = static_cast<uint8_t>((seat + step) % sim::MAX_PLAYERS);
        if (room.slots[next].state == SlotState::Empty) return next;
    }
    for (uint8_t step = 1; step < sim::MAX_PLAYERS; ++step) {                         // ... else the next guest's: the two change places (a bot's colour and the host's stay)
        const uint8_t next = static_cast<uint8_t>((seat + step) % sim::MAX_PLAYERS);
        if (room.slots[next].state == SlotState::Client) return next;
    }
    return 255;                                                                       // no other guest and no free colour: nowhere to go
}

// A SeatMove went out and the room has not answered it: the room still seats its people as it did, and less than a second has passed (a request that the room cannot do is not answered at all)
bool NetGame::move_unanswered() const noexcept {
    return move_pending_ != 0 && move_pending_ == seating_hash(room_) && now_ - move_sent_ms_ < kMoveAnswerMs;
}

bool NetGame::request_move_seat(uint8_t from, uint8_t to) {
    if (!is_leader() || phase_ != Phase::Room || !client_lobby_) return false;
    if (from >= sim::MAX_PLAYERS || to >= sim::MAX_PLAYERS || from == to) return false;
    if (room_.slots[from].state != SlotState::Client) return false;                  // (a person's colour: the host's, a bot's and an empty one do not move)
    if (room_.slots[to].state != SlotState::Empty && room_.slots[to].state != SlotState::Client) return false;      // (a free colour, or a guest's: a bot's stays)
    if (move_pending_ != 0 && now_ - move_sent_ms_ < kMoveGapMs) return false;      // a double click is one press
    if (move_unanswered()) return false;                                              // the last request has not been answered
    if (!client_lobby_->request_seat_move(from, to)) return false;                   // (it carries the guard: the seats as this machine shows them)
    move_pending_ = seating_hash(room_);
    move_sent_ms_ = now_;
    return true;
}

bool NetGame::request_move_seat(uint8_t seat) {
    const uint8_t target = seat_move_target(room_, seat);
    return target < sim::MAX_PLAYERS && request_move_seat(seat, target);
}

// The leader's plan of bots is by colour: a level for each seat, seated in the seats that are empty at START. When the room shows a person in another colour (a colour that a person held is empty
// now, one that was empty is a person's now, nothing else changed: the leader's SeatMove, whenever its answer comes) the bot that was for the colour that the person took is for the colour that it
// left, so the match that START makes has the same people and the same bots as the leader saw. A swap of two guests leaves the same colours empty: nothing changes in any state, and the plan stays.
// The plan follows what the room shows, not what was asked: a request that the room does not do, or does once however many times it was sent, changes nothing here. Each Room message is compared
// with the one before it (a move is one message; a person who comes or goes is another), however many arrive in one update.
void NetGame::follow_moved_player(const RoomMsg& before) {
    uint8_t left = 255;
    uint8_t took = 255;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        const SlotState was = before.slots[seat].state;
        const SlotState is = room_.slots[seat].state;
        if (was == is) continue;
        if (was == SlotState::Client && is == SlotState::Empty && left == 255) left = seat;
        else if (was == SlotState::Empty && is == SlotState::Client && took == 255) took = seat;
        else return;                                                                  // (anything else is no move: somebody came or went, a bot was seated)
    }
    if (left < sim::MAX_PLAYERS && took < sim::MAX_PLAYERS) std::swap(fill_.level[left], fill_.level[took]);
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
    refresh_prediction();
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
        note_lobby_welcome();
        if (!client_lobby_) return;                              // (the function that is told about the key may have ended the session: leave())
        collect_room_chat();
        for (const ClientLobby::Event& ev : client_lobby_->take_events()) {
            switch (ev.type) {
                case ClientLobby::Event::Type::RoomChanged: {
                    const bool first = phase_ == Phase::Connecting;                  // (the first Room message of a session has nothing to be compared with)
                    if (first) {
                        phase_ = Phase::Room;
                        phase_since_ms_ = now_;
                    }
                    const RoomMsg before = room_;
                    room_ = ev.room;                                                 // (this event's Room message: update() can read several, and each is compared with the one before it)
                    seat_ = ev.room.you < sim::MAX_PLAYERS ? ev.room.you : client_lobby_->my_seat();
                    if (!first && is_leader()) follow_moved_player(before);
                    if (!move_hint_given_ && is_leader() && notice_until_ms_ <= now_) {        // (the leader's first company: it can put a player in another colour)
                        size_t people = 0;
                        for (const RoomMsg::Slot& slot : room_.slots) people += slot.state == SlotState::Client ? 1u : 0u;
                        if (people >= 2) {
                            move_hint_given_ = true;
                            set_notice(kTextMoveHint);
                        }
                    }
                    events_.push_back(Event{Event::Type::RoomChanged, 255});
                    break;
                }
                case ClientLobby::Event::Type::StartRequested:
                    phase_ = Phase::Loading;
                    phase_since_ms_ = now_;
                    loaded_reported_ = false;
                    if (notice_ == kTextMoveHint) notice_.clear();     // (the hint is for the waiting room: the screen is locked now, and says that the match loads)
                    seat_ = client_lobby_->my_seat();            // (a machine that is given its match back has no Room message: its seat is the Welcome's)
                    start_ = client_lobby_->start_info();
                    begin_peer_links();                          // the links between guests are made while the map loads
                    events_.push_back(Event{Event::Type::StartRequested, 255, client_lobby_->rejoined()});
                    announce_start_key();                        // (the match has begun: a new player's key is kept now; the function that is told may end the session)
                    if (!client_lobby_) return;
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
                    unannounce_start_key();                      // (the room waits again: it keeps no key; the function that is told may end the session)
                    if (!client_lobby_) return;
                    break;
                }
                case ClientLobby::Event::Type::Rejected:
                    phase_ = Phase::Failed;
                    fail_reason_ = FailReason::Rejected;
                    reject_reason_ = client_lobby_->reject_reason();
                    if (!key_is_zero(join_key_)) {                  // a machine that came with the key of its seat: the words of the way back, and the key goes when the reason says that it is of no use
                        status_ = way_back_text(reject_reason_);
                        if (way_back_forgets(reject_reason_)) forget_key();
                    } else {
                        status_ = reject_text(client_lobby_->reject_reason(), kInBrowser, create_.has_value());
                    }
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
        if (client_session_->wants_connection(now_)) attach_new_link();       // the way back: the session lost its link and asks for a new one (a failed attempt is told to it too)
        note_session_mode();
        if (!client_session_) return;                            // (the same)
        if (!desync_reported_ && client_session_->desynced()) {
            desync_reported_ = true;
            status_ = "The game is out of sync.";
            events_.push_back(Event{Event::Type::Desync, client_session_->desync().player});
        }
        if (client_session_->promoted()) {
            promote();                                            // the host is gone and this machine is the lowest seat left
        } else if (client_session_->lost()) {
            if (!reload_after_bad_request()) end_lost_match();
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
    if (prediction_) prediction_->rebind(host_session_->runner());      // (the runner is the one that the client session had: it went with the promotion)
#ifndef __EMSCRIPTEN__
    if (transport_) {
        transport_->peer_listener.reset();                       // nobody joins a match that runs
        transport_->pending_peers.clear();
    }
#endif
    set_notice("You are the host now.");
    events_.push_back(Event{Event::Type::HostChanged, seat_});
}

// ---- the way back --------------------------------------------------------------------------------------------------------------------------------

std::function<void()> NetGame::wake_function() {
    return [this]() {                                                     // the application's step may end the session and with it the connection that calls this and its callbacks:
        const std::function<void()> wake = on_wake_;                      // so the function runs from a copy
        if (wake) wake();
    };
}

HelloMsg NetGame::way_back_hello() const {
    HelloMsg h;
    h.name = target_.name;                                                // (the encoder of the Hello keeps printable ASCII and at most kMaxNameChars, as the lobby's does)
    h.room = target_.room;
    h.token = target_.token;
    h.want_seat = seat_;                                                  // (the key decides the seat: this is only what the server would be told by anybody)
    h.platform = platform_;                                               // (a Hello with a key never makes a room: no create block)
    return h;
}

std::unique_ptr<Connection> NetGame::make_link(bool for_lobby) {
    if (link_maker_) return link_maker_();
#ifdef __EMSCRIPTEN__
    if (target_.url.empty()) return nullptr;
    auto conn = WasmWsConnection::connect(target_.url);
    if (!conn) return nullptr;
    conn->set_on_wake(wake_function());                                   // as for the first link: news from the server reaches a page that is not drawn
    if (for_lobby) {
        conn->set_on_open([this]() {                                      // the lobby's Hello goes out when the socket opens (a session's own Hello is sent by the session, when its link is open)
            if (client_lobby_) client_lobby_->send_hello();
        });
    }
    return conn;
#else
    (void)for_lobby;
    if (target_.address.empty()) return nullptr;
    return TcpConnection::connect(target_.resolved.empty() ? target_.address : target_.resolved, target_.port);
#endif
}

void NetGame::attach_new_link() {
    if (!transport_ || !client_session_) return;
    std::unique_ptr<Connection> link = make_link();
    Connection* raw = link.get();
    transport_->relink = std::move(link);                                 // (the session holds no pointer to the link before this one: it was let go when the attempt failed)
    client_session_->attach(raw, now_);                                   // null: no link could be made, the next attempt is due in two seconds
}

// ---- the key, the way back's state for the screens -------------------------------------------------------------------------------------------------

std::string NetGame::server_text() const {
    if (!target_.url.empty()) return target_.url;
    return (target_.address.find(':') != std::string::npos ? "[" + target_.address + "]" : target_.address) + ":" + std::to_string(target_.port);
}

void NetGame::announce_key(const SeatKey& key, uint8_t seat) {
    if (key_is_zero(key)) return;
    rejoin_key_ = RejoinKey{target_.room, seat, key, server_text()};
    have_key_ = true;
    if (!on_key_) return;
    const std::function<void(const RejoinKey&)> fn = on_key_;           // (the owner's function may end the session, and with it the callbacks of this object: it runs from copies)
    const RejoinKey given = rejoin_key_;
    fn(given);
}

void NetGame::forget_key() {
    if (!have_key_) return;
    have_key_ = false;
    if (!on_forget_key_) return;
    const std::function<void(const RejoinKey&)> fn = on_forget_key_;
    const RejoinKey gone = rejoin_key_;
    fn(gone);
}

// The lobby's Welcome is the moment that a room hands out a key (none from a game on the local network or a room that holds no seats). A rejoin's Welcome (the flag: the match is this machine's own, it
// is running) announces the key at once. A new player's key waits for the Start (announce_start_key): a visit to a waiting room alone, a tab that is closed in it, leaves no key behind to be offered
// for a match that never began. A Hello that showed a key and was answered as a new player's (the Welcome has no rejoin flag and another key: a room with the same code is waiting for its players, a
// public room that another Hello made; the server never makes one for a Hello that shows a key) found nothing to take the seat of: the old key is of no use, and the machine is a player of the waiting
// room like any other. The same key without the flag is the seat taken back in a waiting room.
void NetGame::note_lobby_welcome() {
    if (welcomed_ || !client_lobby_ || client_lobby_->my_seat() >= sim::MAX_PLAYERS) return;
    welcomed_ = true;
    const SeatKey key = client_lobby_->key();
    const bool showed_key = !key_is_zero(join_key_);
    if (showed_key && !client_lobby_->rejoined() && !key_matches(key, join_key_)) {
        forget_key();
        set_notice(kTextNewRoom);
    }
    // The colour that this player asked for (a link of the front page names one) was taken, so the room gave the first free seat (see join): it says so, once, in colour words. A machine that
    // shows a key sits where the key says and is told nothing of the kind.
    const uint8_t given = client_lobby_->my_seat();
    if (!showed_key && target_.want_seat < sim::MAX_PLAYERS && given != target_.want_seat) set_notice(seat_colour(target_.want_seat) + " was taken: you play " + seat_colour(given) + ".");
    if (client_lobby_->rejoined()) announce_key(key, client_lobby_->my_seat());
    else pending_key_ = key;                                    // (zero for a room that gives none: nothing to announce then)
}

// The Start arrived: the match begins, so the key that the room gave a new player is kept now. A rejoin announced its key at its Welcome already (pending_key_ is zero then).
void NetGame::announce_start_key() {
    if (key_is_zero(pending_key_) || !client_lobby_) return;
    const SeatKey key = pending_key_;
    pending_key_ = SeatKey{};
    start_key_announced_ = true;
    announce_key(key, client_lobby_->my_seat());
}

// A start that is cancelled is no match: the key that it gave out goes again, and the next Start gives it again (a waiting room keeps no key)
void NetGame::unannounce_start_key() {
    if (!start_key_announced_ || !client_lobby_) return;
    start_key_announced_ = false;
    pending_key_ = rejoin_key_.key;
    forget_key();
}

// The session's mode changed (it was looked at after every update). A rejoin's Welcome (Rejoining to CatchingUp, or straight to Normal) says the key again; the end of the way back, in whichever way
// it was taken, is one event for the screen.
void NetGame::note_session_mode() {
    const ClientSession::Mode mode = client_session_->mode();
    if (mode == last_mode_) return;
    using Mode = ClientSession::Mode;
    const bool was_linking = last_mode_ == Mode::Reconnecting || last_mode_ == Mode::Rejoining;
    const bool was_back = was_linking || last_mode_ == Mode::CatchingUp;
    if (was_linking && (mode == Mode::CatchingUp || mode == Mode::Normal)) announce_key(client_session_->key(), seat_);
    if (was_back && mode == Mode::Normal) events_.push_back(Event{Event::Type::Rejoined, 255});
    last_mode_ = mode;
}

bool NetGame::paused() const {
    if (phase_ != Phase::Playing || !client_session_) return false;
    return client_session_->reconnecting() || client_session_->paused();
}

PauseInfo NetGame::pause_info() const {
    PauseInfo info;
    if (phase_ != Phase::Playing || !client_session_) return info;
    const ClientSession& s = *client_session_;
    const ClientSession::Mode mode = s.mode();
    info.reconnecting = mode == ClientSession::Mode::Reconnecting || mode == ClientSession::Mode::Rejoining;
    info.catching_up = mode == ClientSession::Mode::CatchingUp;
    if (info.reconnecting || info.catching_up) {                // this machine's own way back: what the server last said about the match is old
        const uint32_t away = now_ - s.lost_since_ms();
        info.away_s = away / 1000u;
        info.attempts = s.reconnect_attempts();
        if (info.reconnecting) {
            const uint32_t limit = s.give_up_ms();
            info.give_up_s = away < limit ? (limit - away + 999u) / 1000u : 0u;
        } else {
            info.catch_up_percent = s.catch_up_percent();
        }
        return info;
    }
    if (mode != ClientSession::Mode::Normal) return info;
    const auto name_of = [this](uint8_t seat) { return seat < sim::MAX_PLAYERS ? start_.names[seat] : std::string(); };
    const PresenceMsg& p = s.presence();
    for (const PresenceMsg::Entry& e : p.missing) {
        PauseInfo::Seat seat;
        seat.seat = e.seat;
        seat.name = name_of(e.seat);
        seat.away_s = e.waited_s;
        seat.catching_up = e.state == PresenceMsg::State::CatchingUp;
        seat.progress = e.progress;
        info.missing.push_back(std::move(seat));
    }
    if (p.vote_seat < sim::MAX_PLAYERS && p.vote_seat != seat_) {      // (a vote about this machine's own seat is not its to cast, the server ignores it: its screen has no block for it)
        info.vote_open = true;
        info.vote_seat = p.vote_seat;
        info.vote_name = name_of(p.vote_seat);
        info.votes_continue = p.votes_continue;
        info.voters = p.voters;
        info.my_vote = p.your_vote == 1 ? PauseInfo::Choice::KeepWaiting : (p.your_vote == 2 ? PauseInfo::Choice::Continue : PauseInfo::Choice::None);
    }
    info.resume_seconds_left = s.resume_seconds_left();
    return info;
}

bool NetGame::vote(bool keep_waiting) {
    if (phase_ != Phase::Playing || !client_session_) return false;
    const uint8_t seat = client_session_->presence().vote_seat;
    if (seat >= sim::MAX_PLAYERS || seat == seat_) return false;      // no vote is open, or it is about this machine's own seat
    return client_session_->vote(seat, !keep_waiting);          // (the session sends it only while it follows the live match)
}

// A server that restored the match from a record that lost its last second (the death of its machine: docs/NETWORK_PORT.md "Writing") answers the Hello of a machine that is AHEAD of it, one that
// says it has more turns than were ever sealed, with BadRequest. That machine's key is good: it starts from nothing, the way a page that was reloaded does (a new lobby, a Hello with the key and no
// turns, Start, the stream), once per match: a server that keeps answering BadRequest ends the match for this machine as any refusal does.
bool NetGame::reload_after_bad_request() {
    ClientSession& s = *client_session_;
    if (!way_back_ || reload_used_ || !s.rejected() || s.reject_reason() != RejectReason::BadRequest || s.runner().next_turn_expected() == 0) return false;
    begin_reload();
    return true;
}

void NetGame::begin_reload() {
    reload_used_ = true;
    const SeatKey key = client_session_->key();
    drop_prediction();                                          // (it holds the runner of the session: it goes first)
    client_session_.reset();
    client_lobby_.reset();
    if (transport_) {
        transport_->relink.reset();
        if (transport_->uplink) transport_->uplink->close();
        transport_->uplink = make_link(true);
    }
    way_back_ = false;                                          // (begin_match says again, for the session of the new match)
    loaded_reported_ = false;
    desync_reported_ = false;
    phase_since_ms_ = now_;
    if (!transport_ || !transport_->uplink) {                   // no link can be made: the machine is where a join that cannot connect is
        phase_ = Phase::Failed;
        fail_reason_ = FailReason::Unreachable;
        status_ = str::text(str::kUnableToConnect);
        events_.push_back(Event{Event::Type::Failed, 255});
        return;
    }
    client_lobby_ = std::make_unique<ClientLobby>(transport_->uplink.get(), lobby_config(key, seat_));
    join_key_ = key;
    welcomed_ = false;
    pending_key_ = SeatKey{};
    phase_ = Phase::Connecting;
    refresh_status();
}

// The session cannot go on: the host is gone and no other machine could take over, or this machine was cut off for good
void NetGame::end_lost_match() {
    const ClientSession& s = *client_session_;
    phase_ = Phase::Over;
    if (way_back_ && s.rejected()) {                            // the server's word on the way back
        status_ = way_back_text(s.reject_reason());
        if (way_back_forgets(s.reject_reason())) forget_key();
    } else if (way_back_ && !s.desynced()) {                    // nobody refused: the time of the way back ran out
        status_ = kTextGaveUp;
        forget_key();
    } else {
        status_ = match_lost_text(s.lost_reason());
    }
    events_.push_back(Event{Event::Type::HostLeft, 255});
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
    bool rejoin = false;                                        // this machine was given a match that runs (see Event::rejoin)
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
        way_back_ = false;
        const SeatKey key = client_lobby_ ? client_lobby_->key() : SeatKey{};
        if (!cc.migration && !key_is_zero(key) && (!target_.address.empty() || !target_.url.empty())) {
            // A dedicated server's room that holds seats gave this machine a key: a lost link is not the end of the match. The session asks for a new link (update) and says Hello with the key
            cc.reconnect = true;
            cc.key = key;
            cc.hello = way_back_hello();
            cc.rejoin = client_lobby_->rejoined();               // (a machine that starts from nothing is given the match: its lobby did the Hello, Welcome, Start, Loaded, Begin)
            rejoin = cc.rejoin;
            way_back_ = true;
        }
        known_host_ = cc.host;
        client_session_ = std::make_unique<ClientSession>(sim_, cc);
        client_session_->set_connection(transport_->uplink.get());
        install_hooks();
        client_session_->start(now_);
        last_mode_ = client_session_->mode();                   // (CatchingUp for a machine that is given its match, else Normal)
        pump_peers();                                           // hands over the links that were made while the map loaded (none in the browser)
    }
    phase_ = Phase::Playing;
    loaded_reported_ = false;
    status_.clear();
    events_.push_back(Event{Event::Type::Begun, 255, rejoin});
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
        if (prediction_) prediction_->on_tick();                // (the predicted engine runs its tick first: the application's tick hook reads the engine that is shown)
        if (on_tick_) on_tick_();
    });
    r->set_on_turn([this](const TurnMsg& turn) {
        if (prediction_) prediction_->on_turn(turn);
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

// ---- the prediction of one's own orders -----------------------------------------------------------------------------------------------------------

void NetGame::make_prediction() {
    LockstepRunner* r = runner();
    if (r == nullptr || seat_ >= sim::MAX_PLAYERS) return;
    Prediction::Config pc;
    pc.seat = seat_;
    pc.budget_ns = prediction_budget_ns_;
    pc.budget_strikes = prediction_budget_strikes_;
    pc.cooldown_ticks = prediction_cooldown_ticks_;
    pc.work_hook = prediction_work_hook_;
    pc.wall_clock = prediction_wall_clock_;
    pc.cpu_clock = prediction_cpu_clock_;
    prediction_ = std::make_unique<Prediction>(sim_, *r, pc);
}

// What an order of this player takes to reach the engine, in ms. The measured delay of the last orders when there is one (it includes everything: the way to the host, the wait for the
// seal, the way back and the jitter buffer); before the first order, an estimate of the same things.
uint32_t NetGame::expected_command_delay_ms() const {
    uint32_t delay = 0;
    if (const std::optional<uint32_t> measured = command_delay_ms()) {
        delay = *measured;
    } else {
        const LockstepRunner* r = runner();
        const uint32_t buffer = r != nullptr ? r->buffer_turns() : 1u;
        delay = ping_ms().value_or(0u) + kTurnMs / 2u + buffer * kTurnMs;
    }
    // The LAG of an order is the number of ticks from the confirmed tick that it is given at to the tick that runs it, one tick fewer than the delay counts (the delay is measured up to the
    // moment that the tick which applies the order runs, the lag stops at its start), as real orders show: a round trip of 0 ms has a delay of 83 ms and a lag of 1 tick, 60 ms 133 ms
    // and 2, 200 ms 283 ms and 5. The prediction learns the lags themselves once there are orders (Prediction::Config::learn_lead); this is what it takes until then.
    return delay > kTurnMs ? delay - kTurnMs : 0u;
}

void NetGame::drop_prediction() {
    if (!prediction_) return;
    prediction_.reset();
    if (on_prediction_dropped_) on_prediction_dropped_();
}

// Once per update: there is a prediction only where it is wanted (it is made here, when the match plays and the user asked for it, and destroyed when the user switches it off: a default
// match never has one), and it is on only while the match simply follows the live stream. Every state in which the confirmed engine does not (a pause, a catch-up, a rejoin, a host
// change, a desync) and the application's switch turn it off; it begins again with the first tick after they are gone.
// A pause is the runner's `held` (the sessions hold it for as long as the match is paused: a seat that is away, the countdown that follows), and a host change, a rejoin and the like are a
// client session that is not in its Normal mode: the sessions' own `paused()` and `electing()` say the same thing and are not asked again.
void NetGame::refresh_prediction() {
    if (!prediction_enabled_) {
        drop_prediction();
        return;
    }
    if (!prediction_ && phase_ == Phase::Playing) make_prediction();
    if (!prediction_) return;
    bool off = app_suspends_prediction_ || phase_ != Phase::Playing || desynced();
    if (client_session_) off = off || client_session_->mode() != ClientSession::Mode::Normal || client_session_->catching_up();
    if (const LockstepRunner* r = runner()) off = off || r->held();
    prediction_->set_suspended(off);
    if (!off) prediction_->set_expected_delay_ms(expected_command_delay_ms());
}

uint64_t& NetGame::default_prediction_budget_ns() noexcept {
    static uint64_t budget = Prediction::Config{}.budget_ns;
    return budget;
}

sim::SimulationEngine& NetGame::view_engine() {
    return predicting() ? prediction_->engine() : sim_;
}

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

size_t NetGame::fill_bots(const FillPlan& plan, std::vector<uint8_t>* seats) {
    if (!plan.any() || role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return 0;
    if (host_lobby_->fog()) {
        set_notice(kNoticeFillFog);
        refresh_status();                                            // (the status line says why at once, not with the next update)
        return 0;
    }
    size_t seated = 0;
    for (const auto& bot : plan_fill_seats(plan, host_lobby_->room(), sim::MAX_PLAYERS)) {         // (a room on the local network has four seats)
        if (!host_lobby_->add_bot(bot.first, fill_bot_name(bot.second))) break;
        if (seats != nullptr) seats->push_back(bot.first);
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
    // The teams (protocol 13): this machine's choice, when the seats that play can make them; else the match starts without, and everybody is told why once it has started
    uint8_t roster = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) roster = static_cast<uint8_t>(roster | (host_lobby_->occupied(seat) ? 1u << seat : 0u));
    sim::StartTeams teams;
    std::string no_teams;
    if (teams_.set) {
        const sim::StartTeamsPlan plan = sim::plan_start_teams(teams_, roster);
        if (plan.why.empty()) teams = teams_;
        else no_teams = std::string(kNoticeNoTeams) + plan.short_why;
    }
    if (!host_lobby_->start(seed, map_hash, now_, teams)) return false;
    phase_ = Phase::Loading;
    phase_since_ms_ = now_;
    loaded_reported_ = false;
    start_ = host_lobby_->start_info();
    events_.push_back(Event{Event::Type::StartRequested, 255});
    if (!no_teams.empty()) {
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if (host_lobby_->room().slots[seat].state == SlotState::Client) host_lobby_->notify(seat, no_teams);
        }
        set_notice(no_teams);
    }
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
            refresh_status();
            unannounce_start_key();                               // the lobby takes no Cancel of the server's after this: the key that the Start gave goes now (last: the function that is told may end the session)
            return;
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
    if (host_session_) {
        host_session_->submit_local(c);
    } else if (!client_session_ || !client_session_->submit(c)) {
        return sim::CommandResult{};                                                            // no host to send it to (a new one is being chosen): Ignored
    }
    // The order is on its way. With the prediction on, the predicted engine applies it NOW and says what the engine says (the ant that answers, the ants that needed the order); without it,
    // the immediate feedback of the click is predict_order_ack's guess, as it was before the prediction existed.
    if (prediction_ && prediction_->submit(c, result)) return result;
    result.ack_ant = sim_.predict_order_ack(c, &result.needing_order);
    result.status = sim::CommandResult::Status::Applied;                                        // optimistic: the turn decides
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
    forget_key();                                                // ... and the room will finish: no key opens it any more
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
