#include "ants_net/lobby.hpp"

#include "ants_net/clock.hpp"

#include <algorithm>
#include <utility>

namespace ants::net {

namespace {

std::string printable(const std::string& s, size_t max) {
    std::string out;
    for (char c : s) {
        if (out.size() >= max) break;
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u <= 0x7E) out.push_back(c);
    }
    return out;
}

// Whether a name looks like a bot's ("Bot (Hard)"): its first four characters, leaving out the spaces and the case, are "bot(" (so "bot(x)", " Bot (x)" and
// "B o t (x)" all count: none of them may pass for a person, and none may be taken by one)
bool bot_like(const std::string& name) {
    static const char kMark[] = "bot(";
    size_t matched = 0;
    for (const char raw : name) {
        if (raw == ' ') continue;
        const char c = raw >= 'A' && raw <= 'Z' ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c != kMark[matched]) return false;
        if (++matched == sizeof(kMark) - 1) return true;
    }
    return false;
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

// A person's name may not look like a bot's: the bots of a room are shown as bots, and a person is never shown as one. Replaced by `fallback`.
std::string human_name(const std::string& raw, const std::string& fallback) {
    const std::string name = trimmed(printable(raw, kMaxNameChars));
    return bot_like(name) ? fallback : name;
}

// The team pair after two seats changed places (a lobby room's SeatMove): a seat of the pair that was `a` is `b` now and the other way round; the pair is written with the lower seat first (a pair has one spelling)
void swap_team_seats(uint8_t& team_a, uint8_t& team_b, uint8_t a, uint8_t b) {
    if (team_a == kNoTeam && team_b == kNoTeam) return;
    const auto moved = [a, b](uint8_t seat) { return seat == a ? b : seat == b ? a : seat; };
    const uint8_t x = moved(team_a);
    const uint8_t y = moved(team_b);
    team_a = std::min(x, y);
    team_b = std::max(x, y);
}

// What the leader is told when the games did not come in time ("Priya's game did not come in time." / "These games did not come in time: Priya and Sam."); the names are the room's own, printable
std::string late_notice(const std::vector<std::string>& names) {
    if (names.empty()) return std::string();
    if (names.size() == 1) return names[0] + kNoticeGameLate;
    std::string list;
    for (size_t i = 0; i < names.size(); ++i) list += std::string(i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return std::string(kNoticeGamesLate) + list + ".";
}

// A bot's name always carries the marker: a name that does not start with "Bot (" is wrapped ("Zed" becomes "Bot (Zed)"), an empty one is "Bot"
std::string bot_name(const std::string& raw) {
    const std::string name = trimmed(printable(raw, kMaxNameChars));
    if (name.empty()) return "Bot";
    if (bot_like(name)) return name;
    return printable("Bot (" + name + ")", kMaxNameChars);
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// ChatLog
// ------------------------------------------------------------------------------------------------

void ChatLog::add(ChatLine line) {
    lines_.push_back(std::move(line));
    ++total_;
    if (lines_.size() > kMaxLines) lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMaxLines));
}

std::vector<ChatLine> ChatLog::take() {
    const uint64_t fresh = total_ - taken_;                           // lines added since the last take (some may have been pushed out of the log meanwhile: then only what is left)
    const size_t n = static_cast<size_t>(std::min<uint64_t>(fresh, lines_.size()));
    std::vector<ChatLine> out(lines_.end() - static_cast<std::ptrdiff_t>(n), lines_.end());
    taken_ = total_;
    return out;
}

// ------------------------------------------------------------------------------------------------
// HostLobby
// ------------------------------------------------------------------------------------------------

HostLobby::HostLobby(Config config) : cfg_(std::move(config)) {
    if (cfg_.host_seat != 255 && cfg_.host_seat >= sim::MAX_PLAYERS) cfg_.host_seat = 0;
    if (cfg_.host_seat < sim::MAX_PLAYERS) {                   // a dedicated server's host (255) holds no seat
        room_.slots[cfg_.host_seat].state = SlotState::Host;
        room_.slots[cfg_.host_seat].name = human_name(cfg_.host_name, "Host");
        room_.slots[cfg_.host_seat].rtt_ms = 0;                // the host's own thumb is always good
        room_.slots[cfg_.host_seat].platform = valid_platform(cfg_.host_platform) ? cfg_.host_platform : kPlatformUnknown;
    }
    // the room's own rules (protocol 15): told to everybody in every Room message; a pair that no honest client could have sent (two seats, the lower first) is no team
    if (cfg_.room_teams.set && cfg_.room_teams.a < cfg_.room_teams.b && cfg_.room_teams.b < sim::MAX_PLAYERS) {
        room_.team_a = cfg_.room_teams.a;
        room_.team_b = cfg_.room_teams.b;
    }
    room_.flags = cfg_.leader_starts ? kRoomLeaderStarts : uint8_t{0};
    // a lobby room (protocol 16) is a server's room that has a leader, waits for it to start, has all four colours and gives keys (a seat is held for its key); without those it is no lobby room (and
    // the Room message would be one that no decoder takes)
    cfg_.lobby_room = cfg_.lobby_room && leads() && cfg_.leader_starts && cfg_.max_players >= sim::MAX_PLAYERS && static_cast<bool>(cfg_.make_key);
    if (cfg_.lobby_room) room_.flags = static_cast<uint8_t>(room_.flags | kRoomLobby);
    cfg_.silence_ms = std::max<uint32_t>(cfg_.silence_ms, std::max<uint32_t>(3u * cfg_.ping_every_ms, 1000u));        // (a client answers each ping: a link that is alive is never quiet for longer than three of them)
    // room_.map_name stays empty until the host chooses a map (the setup screen lists what the Maps folder holds; no map is named in the program)
}

void HostLobby::set_map(const std::string& map_name) {
    if (phase_ != Phase::Room || !valid_map_name(map_name) || map_name == room_.map_name) return;
    room_.map_name = map_name;
    broadcast_room();
}

void HostLobby::set_fog(bool fog) {
    if (phase_ != Phase::Room || fog == room_.fog) return;
    if (fog && has_bot()) return;                                // a bot would see through the fog: with a bot in the room it stays off
    room_.fog = fog;
    broadcast_room();
}

bool HostLobby::has_bot() const noexcept {
    for (const auto& s : room_.slots) {
        if (s.state == SlotState::Bot) return true;
    }
    return false;
}

bool HostLobby::add_bot(uint8_t seat, const std::string& name) {
    if (phase_ != Phase::Room || seat >= sim::MAX_PLAYERS || room_.slots[seat].state != SlotState::Empty) return false;
    if (room_.fog || players() >= cfg_.max_players) return false;
    guests_[seat] = Guest{};
    room_.slots[seat].state = SlotState::Bot;
    room_.slots[seat].name = bot_name(name);
    room_.slots[seat].rtt_ms = 0;                                // nothing to measure: the thumb of a bot is good
    events_.push_back(Event{Event::Type::Joined, seat});
    broadcast_room();
    return true;
}

void HostLobby::remove_bot(uint8_t seat) {
    if (phase_ == Phase::Begun || seat >= sim::MAX_PLAYERS || room_.slots[seat].state != SlotState::Bot) return;
    room_.slots[seat] = RoomMsg::Slot{};
    events_.push_back(Event{Event::Type::Left, seat});
    if (phase_ == Phase::Loading) cancel_with(CancelMsg::Reason::PlayerLeft, seat);
    else broadcast_room();
}

size_t HostLobby::humans() const noexcept {
    size_t n = 0;
    for (const auto& s : room_.slots) n += (s.state == SlotState::Host || s.state == SlotState::Client) ? 1u : 0u;
    return n;
}

size_t HostLobby::players() const noexcept {
    size_t n = 0;
    for (const auto& s : room_.slots) n += s.state != SlotState::Empty ? 1u : 0u;
    return n;
}

bool HostLobby::measured(uint8_t seat) const noexcept {
    if (seat >= sim::MAX_PLAYERS) return false;
    if (room_.slots[seat].state == SlotState::Bot) return true;                  // nothing to measure
    return seat == cfg_.host_seat ? room_.slots[seat].state == SlotState::Host : guests_[seat].measured;
}

uint32_t HostLobby::rtt_ms(uint8_t seat) const noexcept {
    return seat < sim::MAX_PLAYERS && seat != cfg_.host_seat ? guests_[seat].rtt_ms : 0u;
}

bool HostLobby::all_measured() const noexcept {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (guests_[s].conn != nullptr && !guests_[s].measured) return false;
    }
    return true;
}

void HostLobby::add_connection(Connection* connection, uint32_t now_ms, const std::string& address) {
    if (connection != nullptr) pending_.push_back(Pending{connection, now_ms, address});
}

void HostLobby::add_connection(Connection* connection, uint32_t now_ms, const std::string& address, const std::vector<uint8_t>& hello_message, bool created) {
    if (connection == nullptr) return;
    Pending p{connection, now_ms, address};
    bool consumed = false;
    handle_hello(p, hello_message, now_ms, consumed, created);
}

void HostLobby::broadcast(const std::vector<uint8_t>& msg) {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (guests_[s].conn != nullptr && guests_[s].conn->is_open()) guests_[s].conn->send(msg);
    }
}

// What the Room message of a lobby room shows besides the seats (protocol 16): which seats a game holds, and whether the leader's START waits for the games. Nothing for a room that is no lobby room.
void HostLobby::sync_room_flags() {
    if (!cfg_.lobby_room) return;
    uint8_t games = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) games = static_cast<uint8_t>(games | (in_game(s) ? 1u << s : 0u));
    room_.in_game = games;
    room_.flags = starting_.active ? static_cast<uint8_t>(room_.flags | kRoomStarting) : static_cast<uint8_t>(room_.flags & static_cast<uint8_t>(~kRoomStarting));
}

void HostLobby::broadcast_room() {
    sync_room_flags();
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (guests_[s].conn == nullptr || !guests_[s].conn->is_open()) continue;
        RoomMsg m = room_;
        m.you = s;
        guests_[s].conn->send(encode(m));
    }
}

// A line said in the room goes to EVERYBODY (the sender too: the line that comes back is the room's word that it was heard) with the seat of the connection stamped, never a team (before the match
// nobody has one), and into the log. `sender` is a seat that a person holds.
void HostLobby::relay_chat(uint8_t sender, const std::string& text) {
    ChatMsg out;
    out.sender = sender;
    out.team = false;
    out.text = text;
    broadcast(encode(out));
    chat_.add(ChatLine{sender, room_.slots[sender].name, text});
    events_.push_back(Event{Event::Type::Chat, sender});
}

bool HostLobby::chat(const std::string& text) {
    if (cfg_.host_seat >= sim::MAX_PLAYERS || phase_ == Phase::Begun || room_.slots[cfg_.host_seat].state != SlotState::Host) return false;
    const std::string line = printable(text, kMaxChatChars);
    if (line.empty()) return false;
    relay_chat(cfg_.host_seat, line);
    return true;
}

bool HostLobby::notify(uint8_t seat, const std::string& text) {
    if (seat >= sim::MAX_PLAYERS || guests_[seat].conn == nullptr || !guests_[seat].conn->is_open()) return false;
    const std::string line = printable(text, kMaxChatChars);
    if (line.empty()) return false;
    ChatMsg out;
    out.sender = kRoomSender;
    out.text = line;
    return guests_[seat].conn->send(encode(out));
}

void HostLobby::elect_leader() {
    uint8_t best = kNoLeader;
    if (leads()) {
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (!person(s) || room_.slots[s].state != SlotState::Client) continue;               // (a guest whose connection ended and whose seat is held leads still: it counts as present)
            if (best == kNoLeader || guests_[s].join_order < guests_[best].join_order) best = s;
        }
    }
    room_.leader = best;
}

void HostLobby::remove_guest(uint8_t seat, bool notify_reject, RejectReason reason) {
    if (seat >= sim::MAX_PLAYERS || !person(seat)) return;
    if (Connection* c = guests_[seat].conn) {                    // (a held seat has no connection)
        if (notify_reject && c->is_open()) c->send(encode(RejectMsg{reason}));
        if (c->is_open()) c->close();
    }
    guests_[seat] = Guest{};
    room_.slots[seat] = RoomMsg::Slot{};
    elect_leader();                                              // the leader that left is replaced before anybody is told
    events_.push_back(Event{Event::Type::Left, seat});
    if (phase_ == Phase::Loading) cancel_with(CancelMsg::Reason::PlayerLeft, seat);
    else broadcast_room();
}

void HostLobby::kick(uint8_t seat) {
    if (phase_ == Phase::Begun || seat == cfg_.host_seat) return;
    remove_guest(seat, true, RejectReason::Kicked);
}

// Protocol 14. A guest and its slot are one thing that changes seats: the whole Guest goes (its connection, key, thumb, place in the order of the Welcomes, violations, budgets), so a key still
// finds its guest and the leader is still the earliest guest. The seat that it goes to is empty (the guest goes there) or a guest's (the two guests change places: protocol 15, so that a full room can be
// arranged); a colour that a bot or the host holds is not taken from them. A press that crossed a newcomer's Hello on the wire cannot push the newcomer out of its colour or swap it by mistake: the
// SeatMove carries the guard of the seats as the leader saw them (seating_hash), and the room acts only when they are still so.
bool HostLobby::can_move_seat(uint8_t from, uint8_t to) const noexcept {
    return phase_ == Phase::Room && !starting_.active && from < sim::MAX_PLAYERS && to < sim::MAX_PLAYERS && from != to
        && room_.slots[from].state == SlotState::Client                                                   // (an empty seat, the host's and a bot's do not move)
        && (room_.slots[to].state == SlotState::Empty || room_.slots[to].state == SlotState::Client);     // (a free colour, or a guest's: a bot's and the host's stay)
}

bool HostLobby::move_seat(uint8_t from, uint8_t to) {
    if (!can_move_seat(from, to)) return false;
    const bool swapped = room_.slots[to].state == SlotState::Client;                                      // (two guests change places)
    std::swap(guests_[from], guests_[to]);                                                                // (an empty seat is a Guest{} and a Slot{}: the swap is the move)
    std::swap(room_.slots[from], room_.slots[to]);
    if (cfg_.lobby_room) {                                                                                // (protocol 16: the colours are exchanged completely: what each colour is, and the team pair, go with the person)
        std::swap(room_.plan[from], room_.plan[to]);
        swap_team_seats(room_.team_a, room_.team_b, from, to);
    }
    ++seat_moves_;
    // A START that was heard earlier in this pass was made for the room as it was: its plan of bots is by colour, so when a player takes an empty colour the bot that was to fill that colour fills the
    // colour that the player left (a swap leaves the same colours empty: the plan stays), and its sender, the leader, is where it sits now (the owner compares the seat with leader()): the lead goes
    // with the guest, not with the seat.
    for (Event& ev : events_) {
        if (ev.type != Event::Type::LeaderStart) continue;
        if (!swapped) std::swap(ev.fill[from], ev.fill[to]);
        if (ev.seat == from) ev.seat = to;
        else if (swapped && ev.seat == to) ev.seat = from;
    }
    elect_leader();                                                                                       // (the leader is a guest like the others: its seat may be one of these)
    broadcast_room();
    for (const uint8_t seat : {to, from}) {                                                               // the players are told by the room (the leader sees its own move); the one who went to `to` first
        if (seat == from && !swapped) continue;                                                           // (the seat that was left is empty: nobody to tell)
        if (seat == room_.leader) continue;
        std::string who = "The room";                                                                     // (a host that holds a seat has no leader: the owner of the lobby moved the guest)
        if (room_.leader < sim::MAX_PLAYERS) {
            const std::string& name = room_.slots[room_.leader].name;                                     // (a guest may have no name: "Player 2", the word that the application uses for a seat without one)
            who = name.empty() ? "Player " + std::to_string(static_cast<unsigned>(room_.leader) + 1u) : name;
        }
        notify(seat, who + " moved you to " + seat_colour_word(seat) + ".");
    }
    return true;
}

void HostLobby::violation(uint8_t seat) {
    if (++guests_[seat].violations >= cfg_.violation_limit) remove_guest(seat, true, RejectReason::BadRequest);
}

void HostLobby::cancel_with(CancelMsg::Reason reason, uint8_t player) {
    CancelMsg m;
    m.reason = reason;
    m.player = player;
    broadcast(encode(m));
    phase_ = Phase::Room;
    host_loaded_ = false;
    for (auto& g : guests_) g.loaded = false;
    events_.push_back(Event{Event::Type::Cancelled, player});
    broadcast_room();
}

void HostLobby::cancel() {
    if (phase_ == Phase::Loading) cancel_with(CancelMsg::Reason::HostCancelled, cfg_.host_seat);
}

bool HostLobby::start(uint32_t seed, uint64_t map_hash, uint32_t now_ms, const sim::StartTeams& teams) {
    if (!can_start()) return false;
    if (cfg_.lobby_room && !everyone_in_game()) return false;    // (a page cannot load a map, and a seat that is held has no machine: the owner starts when every person is a game)
    if (room_.fog && has_bot()) return false;                    // (set_fog and add_bot keep this from happening: the last line of defence)
    uint8_t roster = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) roster = static_cast<uint8_t>(roster | (room_.slots[s].state != SlotState::Empty ? 1u << s : 0u));
    if (teams.set && !sim::plan_start_teams(teams, roster).why.empty()) return false;          // (the owner checks them first and says why: this is the last line of defence, the Start would be refused by every decoder)
    start_ = StartMsg{};
    start_.set_teams(teams);
    start_.seed = seed;
    start_.map_name = room_.map_name;
    start_.map_hash = map_hash;
    start_.fog = room_.fog;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (room_.slots[s].state == SlotState::Empty) continue;
        start_.roster = static_cast<uint8_t>(start_.roster | (1u << s));
        start_.names[s] = room_.slots[s].name;
        start_.platforms[s] = room_.slots[s].platform;
        if (guests_[s].conn != nullptr && !guests_[s].address.empty() && guests_[s].listen_port != 0) {
            start_.endpoints[s] = Endpoint{guests_[s].address, guests_[s].listen_port};      // how the other guests reach it
        }
    }
    phase_ = Phase::Loading;
    starting_ = Starting{};                                      // (a lobby room: the leader's request is done)
    room_.flags = static_cast<uint8_t>(room_.flags & static_cast<uint8_t>(~kRoomStarting));
    host_loaded_ = false;
    load_started_ms_ = now_ms;
    for (auto& g : guests_) g.loaded = false;
    if (before_start_) before_start_(start_, now_ms);            // (a server's room makes its restart record: before any machine has the Start)
    broadcast(encode(start_));
    return true;
}

void HostLobby::host_loaded(bool ok) {
    if (phase_ != Phase::Loading) return;
    if (!ok) {
        events_.push_back(Event{Event::Type::LoadFailed, cfg_.host_seat});
        cancel_with(CancelMsg::Reason::LoadFailed, cfg_.host_seat);
        return;
    }
    host_loaded_ = true;
    check_all_loaded();
}

void HostLobby::check_all_loaded() {
    if (phase_ != Phase::Loading || !host_loaded_) return;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (guests_[s].conn != nullptr && !guests_[s].loaded) return;
    }
    phase_ = Phase::Begun;
    broadcast(encode_begin());
    events_.push_back(Event{Event::Type::Begun, cfg_.host_seat});
}

// A key for the guest that is about to be welcomed: none (the zero key) without a key maker, when it fails, or when it only gives keys that cannot be told apart (zero, or the key of a guest
// that is seated: two seats must never share a key). Called before the new guest is seated, so a key never equals the guest's own.
SeatKey HostLobby::new_key() const {
    if (!cfg_.make_key) return SeatKey{};
    for (int attempt = 0; attempt < 4; ++attempt) {
        SeatKey key{};
        if (!cfg_.make_key(key) || key_is_zero(key) || seat_of_key(key) != 255) continue;
        return key;
    }
    return SeatKey{};
}

// The seat whose guest holds `key`, 255 when none does. Every seat is compared, whatever the first comparison said, and the comparison itself does not stop at the first byte
// that differs (key_matches): how long the answer takes says nothing about how much of a guess was right.
uint8_t HostLobby::seat_of_key(const SeatKey& key) const noexcept {
    uint8_t found = 255;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        const bool holds = person(s) && key_matches(guests_[s].key, key);                  // (a seat that is held answers to its key as a connected one does)
        found = holds ? s : found;
    }
    return found;
}

// A Hello with the key of a seated guest while the room is open: the new connection is that guest from now on. The seat keeps everything that is its own (its name, its key, its place in the
// order of the Welcomes, so that the leader stays the leader; its violations and its message budget: a connection that is thrown out cannot start again by coming back), the thumb is
// measured again, the old connection is told that it was replaced (it must not try to come back: Superseded) and closed. The Welcome is the one of the seat, with its key.
void HostLobby::take_over(uint8_t seat, Pending& p, const HelloMsg& hello, uint32_t now_ms) {
    Guest& g = guests_[seat];
    Connection* old = g.conn;
    g.conn = p.conn;
    g.held = false;                                              // (protocol 16: a seat that was held has its person back)
    g.heard_ms = now_ms;
    g.kind = hello.client_kind;                                  // (and a page that goes to the game page comes back as a game)
    g.address = p.address;
    g.listen_port = hello.listen_port;
    g.measured = false;
    g.rtt_ms = 0;
    g.ping_nonce = 0;
    g.next_ping_ms = 0;
    for (uint32_t& sent : g.ping_sent) sent = 0;
    room_.slots[seat].rtt_ms = kRttUnknown;
    room_.slots[seat].platform = hello.platform;                 // (the same guest, maybe on another device: what it says now)
    ++takeovers_;
    if (old != nullptr && old != p.conn && old->is_open()) {
        old->send(encode(RejectMsg{RejectReason::Superseded}));
        old->close();
    }
    WelcomeMsg w;
    w.player = seat;
    w.players = sim::MAX_PLAYERS;
    w.key = g.key;
    p.conn->send(encode(w));
    broadcast_room();
}

void HostLobby::handle_hello(Pending& p, const std::vector<uint8_t>& msg, uint32_t now_ms, bool& consumed, bool created) {
    consumed = true;                                 // whatever happens, this connection is not pending any more
    if (peek_type(msg) != MsgType::Hello) {          // the first message must be Hello
        p.conn->close();
        return;
    }
    auto reject = [&](RejectReason r) {
        p.conn->send(encode(RejectMsg{r}));
        p.conn->close();
        events_.push_back(Event{Event::Type::Rejected, 255});
    };
    HelloMsg hello;
    if (!decode_hello_prefix(msg.data(), msg.size(), hello)) return reject(RejectReason::BadRequest);
    if (hello.version != kProtocolVersion) return reject(RejectReason::VersionMismatch);      // the layout of another version is not read
    if (!decode(msg, hello)) return reject(RejectReason::BadRequest);                        // this version's layout, in full
    if (hello.room != cfg_.room_code) return reject(RejectReason::NoSuchRoom);               // a Hello for another room (or for none)
    if (hello.client_kind == kClientPage && !cfg_.lobby_room) return reject(RejectReason::BadRequest);       // (protocol 16: a page cannot play a match, and only a lobby room seats one)
    if (phase_ == Phase::Room && !key_is_zero(hello.key)) {                                  // the key of a seated guest: that guest, on a new connection (a key that fits no seat: a new player)
        const uint8_t holder = seat_of_key(hello.key);
        if (holder != 255) return take_over(holder, p, hello, now_ms);
    }
    if (phase_ != Phase::Room) return reject(RejectReason::MatchRunning);                    // (a key in a match that loads or runs is the session's business, not the lobby's)
    if (starting_.active) return reject(RejectReason::MatchRunning);                         // (a lobby room whose START waits for the games takes no newcomer)
    uint8_t seat = 255;
    if (cfg_.lobby_room) {
        seat = joiner_seat(hello.want_seat);                                                 // (protocol 16: the colour it asked for or the lowest one that nobody holds and the plan calls Open)
    } else {
        if (players() >= cfg_.max_players) return reject(RejectReason::Full);
        if (hello.want_seat < sim::MAX_PLAYERS && room_.slots[hello.want_seat].state == SlotState::Empty) seat = hello.want_seat;     // the seat it asked for, when it is free
        for (uint8_t s = 0; seat == 255 && s < sim::MAX_PLAYERS; ++s) {
            if (room_.slots[s].state == SlotState::Empty) seat = s;                        // else the first free one
        }
    }
    if (seat == 255) return reject(RejectReason::Full);
    const SeatKey key = new_key();
    guests_[seat] = Guest{};
    guests_[seat].conn = p.conn;
    guests_[seat].address = p.address;
    guests_[seat].listen_port = hello.listen_port;
    guests_[seat].join_order = ++joins_;
    guests_[seat].key = key;
    guests_[seat].kind = hello.client_kind;
    guests_[seat].heard_ms = now_ms;
    room_.slots[seat].state = SlotState::Client;
    room_.slots[seat].name = human_name(hello.name, "Player " + std::to_string(static_cast<unsigned>(seat) + 1u));
    room_.slots[seat].platform = hello.platform;
    elect_leader();                                              // the first guest to be welcomed leads (a server's room that allows an early start)
    WelcomeMsg welcome;
    welcome.player = seat;
    welcome.players = sim::MAX_PLAYERS;
    welcome.key = key;
    welcome.flags = created && !key_is_zero(key) ? kWelcomeCreated : uint8_t{0};             // (protocol 16: this Hello made the room; the flag needs a key)
    p.conn->send(encode(welcome));
    events_.push_back(Event{Event::Type::Joined, seat});
    broadcast_room();
}

void HostLobby::handle_guest_message(uint8_t seat, const std::vector<uint8_t>& msg, uint32_t now_ms) {
    switch (peek_type(msg)) {
        case MsgType::Leave:
            if (msg.size() != 1) return violation(seat);
            remove_guest(seat, false, RejectReason::Kicked);
            return;
        case MsgType::Ping: {
            PingMsg m;
            if (!decode_ping(msg.data(), msg.size(), m)) return violation(seat);
            guests_[seat].conn->send(encode_pong(m));
            return;
        }
        case MsgType::Pong: {
            PingMsg m;
            if (!decode_ping(msg.data(), msg.size(), m)) return violation(seat);
            Guest& g = guests_[seat];
            // an answer to one of the last eight pings (its send time must be the recorded one: a made-up time is not believed)
            if (m.nonce == 0 || m.nonce > g.ping_nonce || g.ping_nonce - m.nonce >= 8u || g.ping_sent[m.nonce % 8u] != m.sent_ms ||
                static_cast<int32_t>(last_update_ms_ - m.sent_ms) < 0) {
                return;
            }
            const LinkQuality before = g.measured ? link_quality(room_.slots[seat].rtt_ms) : LinkQuality::Unknown;
            g.measured = true;
            g.rtt_ms = last_update_ms_ - m.sent_ms;
            room_.slots[seat].rtt_ms = static_cast<uint16_t>(std::min<uint32_t>(g.rtt_ms, 0xFFFEu));
            if (link_quality(room_.slots[seat].rtt_ms) != before) broadcast_room();          // the thumb changed: everybody sees it
            return;
        }
        case MsgType::StartRequest: {
            StartRequestMsg m;
            if (!decode(msg, m)) return violation(seat);                  // exactly the type and a fill level (0 .. 3): anything else is garbage
            if (cfg_.lobby_room) {
                // A lobby room (protocol 16): the PLAN decides what a START seats and which teams it makes, so the request only asks, and the leader's is kept until every person is a game (pump_start).
                // One that cannot be honoured, or a second one while the first waits, is ignored and counted like any other.
                if (phase_ == Phase::Room && !starting_.active && seat == room_.leader && start_possible()) {
                    arm_start(seat, now_ms);
                } else {
                    ++ignored_start_requests_;
                    if (++guests_[seat].ignored_start_requests > kIgnoredStartRequestsAllowed) violation(seat);
                }
                return;
            }
            // Only the leader of a server's room is heard, and only while the room is open and could start now (with the empty seats filled when the request asks for bots: then one person is
            // enough). A request that cannot be honoured is no offence at first (the leader's second click on START arrives when the match is loading already; a host that holds a seat has
            // no leader; a player is told who leads): it is ignored and counted. A connection that sends more of them than a person ever could (kIgnoredStartRequestsAllowed) is flooding:
            // every one after those is a violation.
            bool asks_bots = false;
            for (const FillLevel level : m.fill) asks_bots = asks_bots || level != FillLevel::None;
            const bool could = asks_bots ? can_start_filled() : can_start();
            if (phase_ == Phase::Room && seat == room_.leader && could) {
                Event start{Event::Type::LeaderStart, seat};
                start.fill = m.fill;
                start.teams = m.teams();
                events_.push_back(start);
            } else {
                ++ignored_start_requests_;
                if (++guests_[seat].ignored_start_requests > kIgnoredStartRequestsAllowed) violation(seat);
            }
            return;
        }
        case MsgType::SeatMove: {                                         // (protocol 14) the leader puts a player in another colour
            SeatMoveMsg m;
            if (!decode(msg, m)) return violation(seat);                  // exactly the type, two different seats 0 - 3 and a guard that is not 0: anything else is garbage
            // Only the leader of a server's room is heard, and only while the room is open. A request that cannot be done is no offence at first (the leader's press crossed the Start; the player that it
            // meant has left meanwhile; a guest was told that it leads, and the lead has moved on; the seats are not the ones that the leader's guard was made for): it is ignored and counted, and every
            // one after kIgnoredSeatMovesAllowed is a violation. One that can be done is shown to the whole room, so it has the budget of a person's presses: one beyond it is dropped, and a
            // connection that goes on beyond it is flooding.
            if (seat == room_.leader && m.guard == seating_hash(room_) && can_move_seat(m.from, m.to)) {
                switch (guests_[seat].moves.take(now_ms, kSeatMoveBurst, kSeatMovesPerSecond, kSeatMoveExcessBurst)) {
                    case ChatBudget::Verdict::Relay: move_seat(m.from, m.to); return;     // (the guests may have changed seats: nothing of the sender's is touched after this)
                    case ChatBudget::Verdict::Drop: return;
                    case ChatBudget::Verdict::Offence: return violation(seat);
                }
            }
            ++ignored_seat_moves_;
            if (++guests_[seat].ignored_seat_moves > kIgnoredSeatMovesAllowed) violation(seat);
            return;
        }
        case MsgType::Plan: {                                             // (protocol 16) the leader's plan of a lobby room
            PlanMsg m;
            if (!cfg_.lobby_room || !decode(msg, m)) return violation(seat);       // (a room that is no lobby room has no plan: the message is garbage there)
            // Only the leader is heard, only while the room is open and its START does not wait; the plan is shown to everybody, so it has the budget of a person's clicks. As for the colour moves: one that
            // cannot be heard is no offence at first (the leader's click crossed the Start), every one after kIgnoredPlansAllowed is a violation; one beyond the budget is dropped, and a connection that
            // goes on beyond it is flooding.
            if (phase_ == Phase::Room && !starting_.active && seat == room_.leader) {
                switch (guests_[seat].plans.take(now_ms, kPlanBurst, kPlansPerSecond, kPlanExcessBurst)) {
                    case ChatBudget::Verdict::Relay: apply_plan(m); return;
                    case ChatBudget::Verdict::Drop: return;
                    case ChatBudget::Verdict::Offence: return violation(seat);
                }
            }
            ++ignored_plans_;
            if (++guests_[seat].ignored_plans > kIgnoredPlansAllowed) violation(seat);
            return;
        }
        case MsgType::Chat: {                                             // (protocol 11) a line for the room: in the waiting room and while the map loads
            ChatMsg m;
            if (!decode(msg, m)) return violation(seat);                  // the match's rules: at most kMaxChatChars printable characters, a flag that is 0 or 1
            if (m.text.empty()) return;                                   // (an empty line says nothing; it cost a message of the budget, like every other)
            // The chat budget (a remake protection, the original has none): a line within it is relayed to everybody; one beyond it is dropped, and a connection that keeps on beyond it is flooding
            switch (guests_[seat].chat.take(now_ms)) {
                case ChatBudget::Verdict::Relay: relay_chat(seat, m.text); break;
                case ChatBudget::Verdict::Drop: break;
                case ChatBudget::Verdict::Offence: violation(seat); break;
            }
            return;
        }
        case MsgType::Loaded: {
            LoadedMsg m;
            if (phase_ != Phase::Loading || !decode(msg, m)) return violation(seat);
            if (!m.ok) {
                events_.push_back(Event{Event::Type::LoadFailed, seat});
                cancel_with(CancelMsg::Reason::LoadFailed, seat);
                return;
            }
            guests_[seat].loaded = true;
            check_all_loaded();
            return;
        }
        default:
            return violation(seat);
    }
}

void HostLobby::update(uint32_t now_ms) {
    if (phase_ == Phase::Begun) return;              // the match has taken the connections over
    last_update_ms_ = now_ms;
    // connections that have not said Hello yet
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        bool done = false;
        if (p.conn->state() == Connection::State::Closed || p.conn->state() == Connection::State::Failed) {
            done = true;
        } else if (now_ms - p.since_ms > cfg_.hello_timeout_ms) {
            p.conn->close();
            done = true;
        } else {
            std::vector<uint8_t> msg;
            if (p.conn->poll(msg)) handle_hello(p, msg, now_ms, done);
        }
        if (done) pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    // the seated guests
    for (uint8_t s = 0; s < sim::MAX_PLAYERS && phase_ != Phase::Begun; ++s) {
        if (guests_[s].conn == nullptr) {
            // a held seat (a lobby room): its hold runs out, but not while the leader's START waits (that has its time: the games are on their way, and a page that goes to the game closes its link first)
            if (guests_[s].held && !starting_.active && now_ms - guests_[s].held_since_ms >= cfg_.hold_ms) {
                ++hold_expiries_;
                remove_guest(s, false, RejectReason::Kicked);
            }
            continue;
        }
        Guest& g = guests_[s];
        if (g.conn->is_open() && (g.ping_nonce == 0 || time_reached(now_ms, g.next_ping_ms))) {        // measure the round trip: the thumb beside the name
            PingMsg ping;
            ping.nonce = ++g.ping_nonce;
            ping.sent_ms = now_ms;
            g.ping_sent[ping.nonce % 8u] = now_ms;
            g.conn->send(encode_ping(ping));
            g.next_ping_ms = now_ms + cfg_.ping_every_ms;
        }
        std::vector<uint8_t> msg;
        int budget = 64;                                       // at most this many messages of one guest per update: the rest waits (a flood is held back in the connection)
        while (budget-- > 0 && guests_[s].conn != nullptr && guests_[s].conn->poll(msg)) {
            guests_[s].heard_ms = now_ms;
            if (msg.size() > kMaxMessageBytes) violation(s);
            else if (!guests_[s].talk.take(now_ms, cfg_.message_burst, cfg_.messages_per_second)) violation(s);       // more than a client can have to say: not even looked at
            else handle_guest_message(s, msg, now_ms);
        }
        if (cfg_.lobby_room && phase_ == Phase::Room && guests_[s].conn != nullptr && guests_[s].conn->is_open() && now_ms - guests_[s].heard_ms >= cfg_.silence_ms) {
            ++silences_;                                       // (protocol 16: nothing has come from it for silence_ms, and a live client answers a ping every second: the link is dead, and its seat is held as for any connection that ends)
            guests_[s].conn->close();
        }
        if (guests_[s].conn != nullptr && !guests_[s].conn->is_open()) {
            if (cfg_.lobby_room && phase_ == Phase::Room && !key_is_zero(guests_[s].key)) hold_guest(s, now_ms);       // (protocol 16: the seat waits for its key, when it has one; a match that loads is cancelled for a leaver as ever)
            else remove_guest(s, false, RejectReason::Kicked);
        }
    }
    if (phase_ == Phase::Loading && now_ms - load_started_ms_ > cfg_.load_timeout_ms) {
        uint8_t slow = cfg_.host_seat;
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (guests_[s].conn != nullptr && !guests_[s].loaded) {
                slow = s;
                break;
            }
        }
        events_.push_back(Event{Event::Type::LoadFailed, slow});
        cancel_with(CancelMsg::Reason::LoadFailed, slow);
    }
    if (cfg_.lobby_room) pump_start(now_ms);
}

// ------------------------------------------------------------------------------------------------
// The lobby room (protocol 16)
// ------------------------------------------------------------------------------------------------

// The connection of a guest ended while the room is open. The seat stays for hold_ms with all that is its own (name, colour, key, place in the order of the Welcomes, so a held leader leads still); it counts
// as present, it is no game any more, and its thumb is not known.
void HostLobby::hold_guest(uint8_t seat, uint32_t now_ms) {
    Guest& g = guests_[seat];
    g.conn = nullptr;
    g.held = true;
    g.held_since_ms = now_ms;
    g.measured = false;
    g.rtt_ms = 0;
    g.ping_nonce = 0;
    g.next_ping_ms = 0;
    for (uint32_t& sent : g.ping_sent) sent = 0;
    room_.slots[seat].rtt_ms = kRttUnknown;
    ++holds_;
    broadcast_room();
}

// The colour a joiner takes: the one it asked for when nobody holds it and the plan calls it Open, else the lowest such colour; 255 when there is none (the room is Full for it)
uint8_t HostLobby::joiner_seat(uint8_t want) const noexcept {
    const auto takes = [this](uint8_t s) { return room_.slots[s].state == SlotState::Empty && room_.plan[s] == PlanKind::Open; };
    if (want < sim::MAX_PLAYERS && takes(want)) return want;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (takes(s)) return s;
    }
    return 255;
}

// The plan has a bot for a colour that nobody holds: then one person is enough to start (can_start_filled), as for a leader's fill
bool HostLobby::plan_asks_bots() const noexcept {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (room_.slots[s].state == SlotState::Empty && plan_fill_level(room_.plan[s]) != FillLevel::None) return true;
    }
    return false;
}

// The room could start now with whom it holds (held seats count as players; the plan's bots are counted when they would be seated)
bool HostLobby::start_possible() const noexcept {
    return plan_asks_bots() ? can_start_filled() : can_start();
}

// Every person is a game (a seat that is held has no machine, a page cannot play): the match can be started
bool HostLobby::everyone_in_game() const noexcept {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (room_.slots[s].state == SlotState::Client && !in_game(s)) return false;
    }
    return true;
}

// A plan of the leader (already heard: the leader, an open room, within the budget): the map when the server offers it, what each colour is, the teams. The room is shown to everybody when something changed.
void HostLobby::apply_plan(const PlanMsg& plan) {
    bool changed = false;
    if (!plan.map_name.empty()) {                                // ("" keeps the map; a map that the server does not offer keeps it too)
        const std::string file = map_choice_ ? map_choice_(plan.map_name) : plan.map_name;
        if (!file.empty() && valid_map_name(file) && file != room_.map_name) {
            room_.map_name = file;
            changed = true;
        }
    }
    if (room_.plan != plan.plan) {
        room_.plan = plan.plan;
        changed = true;
    }
    if (room_.team_a != plan.team_a || room_.team_b != plan.team_b) {
        room_.team_a = plan.team_a;
        room_.team_b = plan.team_b;
        changed = true;
    }
    if (!changed) return;
    ++plan_changes_;
    broadcast_room();
}

// The leader asked to start: the request stands until every person is a game, and everybody is shown that the room waits (RoomMsg flag kRoomStarting)
void HostLobby::arm_start(uint8_t seat, uint32_t now_ms) {
    starting_.active = true;
    starting_.leader_order = guests_[seat].join_order;
    starting_.since_ms = now_ms;
    broadcast_room();
}

// The request is looked at at the end of every update of a lobby room. It ends, silently, when its leader does not lead any more or the room could not start with whom it has left; it ends with a notice to the
// leader when it has waited start_wait_ms for a game that did not come; and while every person is a game it is given to the owner in every pass (the owner may be in its pause after a cancelled start,
// or have a map to load: it starts the match, which ends the request, or drops it with end_start).
void HostLobby::pump_start(uint32_t now_ms) {
    if (!starting_.active) return;
    const bool still_leads = phase_ == Phase::Room && room_.leader < sim::MAX_PLAYERS && guests_[room_.leader].join_order == starting_.leader_order;
    if (!still_leads || !start_possible()) return end_start(std::string());
    if (everyone_in_game()) {
        Event start{Event::Type::LeaderStart, room_.leader};
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) start.fill[s] = plan_fill_level(room_.plan[s]);
        start.teams = room_.teams();
        events_.push_back(start);
        return;
    }
    if (now_ms - starting_.since_ms < cfg_.start_wait_ms) return;
    std::vector<std::string> late;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (room_.slots[s].state == SlotState::Client && !in_game(s)) late.push_back(room_.slots[s].name);
    }
    end_start(late_notice(late));
}

void HostLobby::end_start(const std::string& notice) {
    if (!starting_.active) return;
    const uint8_t leader = room_.leader;
    starting_ = Starting{};
    for (Guest& g : guests_) {
        if (g.held) g.held_since_ms = last_update_ms_;           // (a hold that ran during the wait begins again: nobody loses a seat to the wait itself)
    }
    broadcast_room();
    if (!notice.empty() && leader < sim::MAX_PLAYERS) notify(leader, notice);
}

std::vector<HostLobby::Event> HostLobby::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

// ------------------------------------------------------------------------------------------------
// ClientLobby
// ------------------------------------------------------------------------------------------------

void ClientLobby::report_loaded(bool ok) {
    if (phase_ != Phase::Loading || conn_ == nullptr) return;
    conn_->send(encode(LoadedMsg{ok}));
    phase_ = ok ? Phase::Loaded : Phase::InRoom;
}

void ClientLobby::leave() {
    if (conn_ != nullptr && conn_->is_open()) {
        conn_->send(encode_leave());
        conn_->close();
    }
    if (phase_ != Phase::Begun) phase_ = Phase::Closed;
}

bool ClientLobby::request_start(const std::array<FillLevel, sim::MAX_PLAYERS>& fill, const sim::StartTeams& teams) {
    if (conn_ == nullptr || phase_ != Phase::InRoom || !is_leader() || !conn_->is_open()) return false;
    StartRequestMsg m;
    m.fill = fill;
    m.set_teams(teams);
    return conn_->send(encode(m));
}

bool ClientLobby::request_start(FillLevel level) {
    return request_start(StartRequestMsg::all(level).fill);
}

bool ClientLobby::request_seat_move(uint8_t from, uint8_t to) {
    if (conn_ == nullptr || phase_ != Phase::InRoom || !is_leader() || !conn_->is_open()) return false;
    if (from >= sim::MAX_PLAYERS || to >= sim::MAX_PLAYERS || from == to) return false;
    return conn_->send(encode(SeatMoveMsg{from, to, seating_hash(room_)}));            // (the guard: the seats as this machine shows them)
}

bool ClientLobby::request_plan(const PlanMsg& plan) {
    if (conn_ == nullptr || phase_ != Phase::InRoom || !is_leader() || !room_.lobby() || !conn_->is_open()) return false;
    return conn_->send(encode(plan));
}

bool ClientLobby::chat(const std::string& text) {
    if (conn_ == nullptr || !conn_->is_open() || (phase_ != Phase::InRoom && phase_ != Phase::Loading && phase_ != Phase::Loaded)) return false;
    ChatMsg m;
    m.sender = seat_ < sim::MAX_PLAYERS ? seat_ : kRoomSender;       // (whatever it says, the room stamps the seat of the connection)
    m.team = false;
    m.text = printable(text, kMaxChatChars);
    if (m.text.empty()) return false;
    return conn_->send(encode(m));
}

void ClientLobby::send_hello() {
    if (conn_ == nullptr || phase_ != Phase::Connecting || !conn_->is_open()) return;
    was_open_ = true;
    HelloMsg h;
    h.name = printable(cfg_.name, kMaxNameChars);
    h.listen_port = cfg_.listen_port;
    h.want_seat = cfg_.want_seat;
    h.room = cfg_.room;
    h.token = cfg_.token;
    h.key = cfg_.key;                                // (have_turns stays 0: this lobby starts from nothing; a key that is zero makes the Hello that of a new player)
    h.platform = valid_platform(cfg_.platform) ? cfg_.platform : kPlatformUnknown;
    h.client_kind = cfg_.client_kind == kClientPage ? kClientPage : kClientGame;
    if (cfg_.create && valid_create_block(*cfg_.create)) h.create = cfg_.create;             // (a block that no server would read is left out: the Hello then joins)
    conn_->send(encode(h));
    phase_ = Phase::Joining;
    joined_stamp_pending_ = true;
}

void ClientLobby::update(uint32_t now_ms) {
    if (conn_ == nullptr || phase_ == Phase::Begun || phase_ == Phase::Rejected || phase_ == Phase::Closed) return;
    if (phase_ == Phase::Connecting) {
        std::vector<uint8_t> nothing;
        conn_->poll(nothing);                        // lets a connection that is still being made (TCP) progress
        if (conn_->is_open()) {
            send_hello();
            joined_stamp_pending_ = true;
        } else if (conn_->state() != Connection::State::Connecting) {
            phase_ = Phase::Closed;
            events_.push_back(Event{Event::Type::Disconnected});
            return;
        } else {
            return;
        }
    }
    if (joined_stamp_pending_) {
        joined_at_ms_ = now_ms;
        joined_stamp_pending_ = false;
    }
    std::vector<uint8_t> msg;
    int budget = 128;
    while (budget-- > 0 && phase_ != Phase::Rejected && phase_ != Phase::Begun && conn_->poll(msg)) {
        switch (peek_type(msg)) {
            case MsgType::Welcome: {
                WelcomeMsg w;
                if (phase_ == Phase::Joining && decode(msg, w)) {
                    seat_ = w.player;
                    key_ = w.key;
                    rejoined_ = (w.flags & kWelcomeRejoin) != 0;
                    created_ = (w.flags & kWelcomeCreated) != 0;
                    phase_ = Phase::InRoom;
                }
                break;
            }
            case MsgType::Reject: {
                RejectMsg r;
                if (decode(msg, r)) {
                    reject_ = r.reason;
                    phase_ = Phase::Rejected;
                    events_.push_back(Event{Event::Type::Rejected});
                }
                break;
            }
            case MsgType::Room: {
                RoomMsg r;
                if (decode(msg, r) && (phase_ == Phase::InRoom || phase_ == Phase::Joining)) {
                    room_ = r;
                    if (r.you != 255) seat_ = r.you;
                    events_.push_back(Event{Event::Type::RoomChanged, 255, r});
                }
                break;
            }
            case MsgType::Start: {
                StartMsg s;
                if (phase_ == Phase::InRoom && decode(msg, s)) {
                    start_ = s;
                    phase_ = Phase::Loading;
                    events_.push_back(Event{Event::Type::StartRequested});
                }
                break;
            }
            case MsgType::Begin:
                if (phase_ == Phase::Loaded && msg.size() == 1) {
                    phase_ = Phase::Begun;
                    events_.push_back(Event{Event::Type::Begun});
                }
                break;
            case MsgType::Cancel: {
                CancelMsg c;
                if ((phase_ == Phase::Loading || phase_ == Phase::Loaded) && decode(msg, c)) {
                    cancel_reason_ = c.reason;
                    cancel_player_ = c.player;
                    phase_ = Phase::InRoom;
                    events_.push_back(Event{Event::Type::Cancelled});
                }
                break;
            }
            case MsgType::Chat: {                    // (protocol 11) a line of the room: from a player's seat, or from the room itself (kRoomSender: a notice)
                ChatMsg c;
                if ((phase_ == Phase::InRoom || phase_ == Phase::Loading || phase_ == Phase::Loaded) && decode(msg, c) && !c.text.empty() &&
                    (c.sender < sim::MAX_PLAYERS || c.sender == kRoomSender)) {
                    chat_.add(ChatLine{c.sender, c.sender < sim::MAX_PLAYERS ? room_.slots[c.sender].name : std::string(), c.text});
                    events_.push_back(Event{Event::Type::Chat, c.sender});
                }
                break;
            }
            case MsgType::Ping: {
                PingMsg p;
                if (decode_ping(msg.data(), msg.size(), p)) conn_->send(encode_pong(p));
                break;
            }
            case MsgType::Pong: {                    // the answer to this guest's own ping
                PingMsg p;
                if (decode_ping(msg.data(), msg.size(), p)) ping_.on_pong(p, now_ms, conn_->last_message_age_ms());
                break;
            }
            default:
                break;
        }
    }
    if (phase_ != Phase::Begun && phase_ != Phase::Rejected && !conn_->is_open()) {
        phase_ = Phase::Closed;
        events_.push_back(Event{Event::Type::Disconnected});
        return;
    }
    if (phase_ == Phase::Joining && now_ms - joined_at_ms_ > cfg_.welcome_timeout_ms) {
        welcome_timed_out_ = true;
        conn_->close();
        phase_ = Phase::Closed;
        events_.push_back(Event{Event::Type::Disconnected});
        return;
    }
    // The round trip to the host, measured with a Ping of this guest's own once a second while it waits in the room (the host measures its guests the same way for the
    // thumbs, but only tells the room when a thumb changes): the same message that a running match uses
    if ((phase_ == Phase::InRoom || phase_ == Phase::Loading || phase_ == Phase::Loaded) && conn_->is_open()) {
        if (!ping_armed_) {
            ping_armed_ = true;
            next_ping_ms_ = now_ms;
        }
        if (time_reached(now_ms, next_ping_ms_)) {
            conn_->send(encode_ping(ping_.next(now_ms)));
            next_ping_ms_ = now_ms + cfg_.ping_every_ms;
        }
    }
}

std::vector<ClientLobby::Event> ClientLobby::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

}  // namespace ants::net
