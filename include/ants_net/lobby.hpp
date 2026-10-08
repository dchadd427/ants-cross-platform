#pragma once

// The room before a match: joining, the roster, the map and fog choice, and the start barrier. The original has no host / join interface (an
// external lobby starts every machine with its roster on the command line); the flow here keeps its rules: the host picks the map and starts, every
// machine loads the same map and reports it, the match begins when everybody is loaded (the original's ROSTER -> LOADED -> map check -> READY
// barriers), no late join, no host migration.
//
//   client: Hello -> Welcome (seat) -> Room updates ... Start -> load -> Loaded -> Begin
//   host:   Room broadcast on every change; start(): Start to all; all Loaded -> Begin; a failure or a leaver -> Cancel, back to the room
//
// A dedicated server's room (a host without a seat) has a LEADER (protocol 7): the first player who joined, and when it leaves the earliest of those who are left. The Room
// message names it to everybody (RoomMsg::leader); it may send StartRequest, and the room (ants_server) then starts the match with the players who are there, when it can.
// A host that holds a seat (LAN / direct) has no leader and ignores StartRequest.
//
// Keys (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A host that is given a key maker (HostLobby::Config::make_key: a dedicated server, which has the operating system's random
// generator; ants_net itself never reads it, the library is built for the web too) gives every guest a KEY with its Welcome, and a guest that shows the key of a seat in its Hello gets
// that seat back: in the room (Phase::Room) the new connection TAKES THE SEAT OVER at once (same seat, same key, same place in the order of the Welcomes, so the leader stays the
// leader; the old connection is told Superseded and closed; the thumb is measured again; the room is broadcast), which is what a page that is reloaded in the waiting room needs before
// the old link is known to be dead. In a match that is loading or running a key is not the lobby's business (the session of the match answers, docs/NETWORK_PORT.md): the lobby says
// MatchRunning to it as to any Hello. A key that fits no seat is no offence: the Hello is the Hello of a new player. Without a key maker (a LAN or direct host) there are no keys,
// every Welcome carries the zero key and every Hello's key is ignored.
//
// Chat and the fill (protocol 11, docs/NETWORK_PORT.md "Protocol 11"). Everybody who is in the room can talk before the match, while it waits for players and while the map loads: a Chat message
// from a guest is relayed to EVERYBODY in the room (the sender included), with the seat of its connection stamped (a team does not exist yet: the team flag is cleared), under the same
// flood budget as every other message of the connection and under a chat budget of its own (ChatBudget: a burst of 5 lines, then one a second; a line beyond it is dropped, and a connection that
// goes on beyond it for long is flooding: a violation each); a line that cannot be decoded is a violation as in the match. A guest who joins later hears only what is said after it joined. Both
// lobbies keep the lines (ChatLog: the last 200) and hand the new ones to their owner (take_chat() and the Chat event), so that the application can show them and start the match's chat log with
// them. The room itself can speak to one guest (HostLobby::notify: a Chat message from kRoomSender), which is how a server's room tells its leader why a fill was refused. The StartRequest of the
// leader carries a fill level for each seat (protocol 13; one level for all of them in protocol 11) and the teams it chose (protocol 13): with a level, the room's owner may seat bots in the empty seats
// and start (Event::LeaderStart carries the levels and the teams; HostLobby::can_start_filled says whether the room could start with them: one person is enough), and start() puts the teams into the
// Start message that every machine starts its match from.
//
// The leader moves the colours (protocol 14, docs/NETWORK_PORT.md "Protocol 14"; the swap and the guard are protocol 15's). A SeatMove of the leader of a server's room, while the room is open, puts the
// guest of seat `from` in seat `to` (HostLobby::move_seat): in an empty seat it goes there, and the guest that holds the seat changes places with it (two guests swap; a bot's seat and the host's never
// move). It is heard only when its guard, the seating_hash of the room as the leader saw it, is the room's own: a press that was made for other seats than these is ignored and counted. Everything that
// is a guest's own goes with it (its key, its name, its place in the order of the Welcomes, so that the leader stays the leader, its violations and its budgets); every guest is sent the Room message
// (each with its own `you`), and a guest whose colour changed is told so with a notice of the room ("Ann moved you to Red.").
//
// The lobby room (protocol 16, docs/NETWORK_PORT.md "Protocol 16"). A server's room that was made with kCreateLobby (HostLobby::Config::lobby_room) is the room that the front page waits in from its first
// second; everything below is for it alone, and a room that is not one is exactly what it was. (1) Joining: a Hello without a key takes the lowest colour that nobody holds and the plan calls Open (the colour
// it asked for when that is one); Easy, Medium, Hard and Nobody colours are never taken by a joiner, no such colour is Full, and a room whose START waits is MatchRunning. (2) The PLAN (PlanMsg, the leader's):
// the map, what each colour is and the teams; every change is shown to everybody in the Room message, and the plan decides the START (a StartRequest's own fill and teams are not looked at). (3) A SeatMove
// exchanges the two colours completely: the person (all that is its own), the colour's kind and the team pair, so that a team goes with its player and a bot with its level. (4) A guest whose connection
// ends keeps its seat for `hold_ms` (it counts as present, a Hello with its key takes the seat back, the leader that is held stays the leader); one that says Leave goes at once. A connection from which nothing at
// all has come for `silence_ms` has ended too (a live client answers the room's ping every second; a link that died without a word is closed, and its seat is held). (5) The START: the leader's
// StartRequest is kept as a request (RoomMsg flag kRoomStarting) until every person is a GAME (a Hello of kClientGame holds the seat: the pages go to the game and take their seats over), and then the owner is
// given the LeaderStart; a request that waits longer than `start_wait_ms`, or whose leader is not the leader any more, is dropped (the leader is told which game did not come in time). A held seat keeps its hold
// while the request waits. (6) Any person can change the name they go by (NameMsg), and the leader can take another person out of the room (RemoveMsg: with the guard of a SeatMove, so that it acts on the person
// that the leader's screen showed; the person is sent the rejection Kicked, their colour is free at once and their key is forgotten; it is heard while the START waits too, for a person whose game never opens).
//
// Both classes are pure logic over Connection, driven from the main loop like the sessions. The connections stay owned by the caller; when the match
// begins the host lobby hands them (seat -> connection) to the HostSession.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ants_net/flood.hpp"
#include "ants_net/latency.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"

namespace ants::net {

/// A lobby room (protocol 16) holds the seat of a guest whose connection ended this long, and its leader's START waits this long for every person's game (HostLobby::Config::hold_ms, start_wait_ms)
inline constexpr uint32_t kLobbyHoldMs = 60u * 1000u;
inline constexpr uint32_t kLobbyStartWaitMs = 90u * 1000u;
/// ... and a connection of a lobby room from which nothing has come for this long is closed (HostLobby::Config::silence_ms): a client that is alive answers a ping every second
inline constexpr uint32_t kLobbySilenceMs = 30u * 1000u;
/// ... and a guest of a lobby room is forgiven one violation and one ignored request of each kind this often (HostLobby::Config::forgive_ms): a lobby lives as long as its people stay
inline constexpr uint32_t kLobbyForgiveMs = 60u * 1000u;

/// One line of the waiting room's chat (protocol 11). `seat` is the sender's seat, kRoomSender (255) for a line that the room itself said (a notice); `name` is the sender's name as the
/// room showed it when the line arrived ("" for a notice).
struct ChatLine {
    uint8_t seat{kRoomSender};
    std::string name;
    std::string text;
    bool notice() const noexcept { return seat >= sim::MAX_PLAYERS; }
};

/// The lines of the waiting room, oldest first, the last kMaxLines of them (the match's chat log can start with them: the application decides), and the ones that nobody took yet.
class ChatLog {
public:
    static constexpr size_t kMaxLines = 200;
    void add(ChatLine line);
    const std::vector<ChatLine>& lines() const noexcept { return lines_; }
    /// The lines added since the last take() (at most kMaxLines: a consumer that never comes does not make the lobby grow)
    std::vector<ChatLine> take();
    /// How many lines were ever added
    uint64_t total() const noexcept { return total_; }

private:
    std::vector<ChatLine> lines_;
    uint64_t total_{0};
    uint64_t taken_{0};
};

class HostLobby {
public:
    struct Config {
        std::string host_name{"Host"};
        std::string room_code;               // "" for a LAN / direct host; a server names its room: a Hello for another room is Rejected NoSuchRoom
        uint8_t host_seat{0};                // the host's own seat; 255 (kNoSeat) for a dedicated server: the host takes no seat, every seat is a guest's
        uint32_t hello_timeout_ms{10000};    // a connection that does not say Hello in this time is closed
        uint32_t load_timeout_ms{60000};     // everybody must report Loaded in this time
        uint32_t violation_limit{8};
        uint32_t ping_every_ms{1000};        // the round trip to every guest is measured this often (the thumbs of the setup screen)
        uint8_t min_players{2};              // can_start() needs this many seats taken (a dedicated server's room: the host holds none of them)
        uint8_t max_players{sim::MAX_PLAYERS};   // a Hello beyond this many seats taken is Rejected Full (a server's room that expects 3 players takes no fourth)
        /// A dedicated server's room only (host_seat = kNoSeat; a host that holds a seat has no leader whatever this says): the first guest to join leads the room
        /// (RoomMsg::leader) and its StartRequest is passed on to the room (Event::LeaderStart) when the room can start. False: the room has no leader and every StartRequest
        /// is ignored (the server's `early_start` option of a room).
        bool early_start{true};
        /// Protocol 15: what a host that holds a seat (a LAN or direct host) runs on (valid_platform; 0: not told): the Room message shows it in the host's seat.
        uint8_t host_platform{0};
        /// Protocol 15: the room's own rules, made known to everybody in the Room message (RoomMsg::team_a / team_b / flags). `room_teams`: the room starts its matches with these teams every
        /// time (none: free for all; the owner of the lobby does the starting, the lobby only tells). `leader_starts`: the room does not start by itself when it is full, only its leader's START
        /// starts it (the owner does that too; it needs a leader, so it is for a server's room that allows an early start).
        sim::StartTeams room_teams{};
        bool leader_starts{false};
        /// Protocol 16: the room is a LOBBY ROOM (see the paragraph above). It needs a server's host that has a leader and waits for it to start (host_seat = kNoSeat, early_start and leader_starts), all four colours
        /// (max_players) and keys (make_key), or no seat could be held: without any of them this is taken for false. `hold_ms`: a guest whose connection ends keeps its seat this long; `start_wait_ms`: the
        /// leader's START waits this long for every person's game.
        bool lobby_room{false};
        uint32_t hold_ms{kLobbyHoldMs};
        uint32_t start_wait_ms{kLobbyStartWaitMs};
        /// A lobby room closes the connection of a guest from which nothing has come for this long while the room is open (its seat is held then, as for any connection that ends): no TCP or proxy tells a server that a
        /// browser which went to sleep is gone, and a room that lives while a person is in it must not live for ever on a link that died.
        uint32_t silence_ms{kLobbySilenceMs};
        /// A lobby room lives as long as its people stay, so what a person got wrong is not held against them for ever: every `forgive_ms` each guest's violations, and each of its counts of ignored requests (START,
        /// colour moves, plans, renames, removals), go down by one. A connection that offends faster than that is thrown out as ever (eight violations are more than one in forgive_ms). Any other room forgives nothing, and so does 0.
        uint32_t forgive_ms{kLobbyForgiveMs};
        /// Flood control (flood.hpp): the messages that one guest may send, a token bucket. A message beyond it is not handled and is a violation (eight throw the guest out).
        uint32_t message_burst{kMessageBurst};
        uint32_t messages_per_second{kMessagesPerSecond};
        /// Makes the key of a seat (a server fills it with 16 random bytes of the operating system): true when `key` was filled. Called once for every guest that is welcomed. A key that is
        /// all zero, that equals the key of another guest, or a maker that returns false, gives the guest NO key (the zero key in its Welcome: no way back for it); the lobby tries a few
        /// times before it gives up. Unset (the default: a LAN or direct host): no guest has a key.
        std::function<bool(SeatKey&)> make_key;
    };
    enum class Phase : uint8_t { Room, Loading, Begun };
    struct Event {
        /// LeaderStart (a dedicated server's room): the leader (`seat`) asked to start now and can_start() holds (with a `fill` level: can_start_filled(): the room could start once the empty
        /// seats were filled); the owner of the lobby decides and calls start(). Chat: a line was said in the room (`seat` is its sender; take_chat() has it)
        enum class Type : uint8_t { Joined, Left, Rejected, LoadFailed, Cancelled, Begun, LeaderStart, Chat };
        Type type{Type::Joined};
        uint8_t seat{255};
        std::array<FillLevel, sim::MAX_PLAYERS> fill{};     // LeaderStart: the bot that the leader asked for in each seat when it is empty (protocol 11: one level for all; 13: a level for each seat)
        sim::StartTeams teams{};                            // LeaderStart: the teams that the leader asked for (protocol 13; free for all when it chose none)
    };

    HostLobby() : HostLobby(Config{}) {}
    explicit HostLobby(Config config);

    void set_map(const std::string& map_name);
    void set_fog(bool fog);
    const std::string& map_name() const noexcept { return room_.map_name; }
    bool fog() const noexcept { return room_.fog; }
    /// The seat of the room's leader (kNoLeader when there is none: a host that holds a seat, a room without early start, nobody has joined yet)
    uint8_t leader() const noexcept { return room_.leader; }
    /// How many StartRequest messages were heard and not acted on: from a guest that is not the leader (every guest of a host that holds a seat), after the room started loading,
    /// from a room that cannot start (too few players). A request is no offence (the leader's second click on START arrives after the Start) as long as a guest does not send more
    /// than kIgnoredStartRequestsAllowed of them: each one after those is a violation. A malformed one is a violation at once.
    uint32_t ignored_start_requests() const noexcept { return ignored_start_requests_; }
    /// How many SeatMove messages were heard and not acted on (protocol 14): from a guest that is not the leader (every guest of a host that holds a seat), after the room started loading, for a
    /// player that is not there, for a seat of a bot or of the host, or with a guard that is not the room's own (protocol 15: the press was made for other seats than these). No offence as long
    /// as a guest does not send more than kIgnoredSeatMovesAllowed of them: each one after those is a violation. A malformed one is a violation at once; one beyond the budget of a leader's presses
    /// (kSeatMoveBurst ...) is dropped and counted by nobody.
    uint32_t ignored_seat_moves() const noexcept { return ignored_seat_moves_; }
    /// How many colour moves were made (move_seat that returned true; a swap of two guests is one)
    uint32_t seat_moves() const noexcept { return seat_moves_; }
    /// The colour of a guest changes (protocol 14): the guest of seat `from` takes seat `to`; when a guest holds `to` the two change places (protocol 15). Everything that is a guest's own goes with it
    /// (its connection, key, name, thumb, place in the order of the Welcomes, violations and budgets); the leader is elected again (it is a guest, whose seat may have been one of these), the Room
    /// message goes to everybody and each guest that moved, unless it is the leader, gets the room's notice "<leader's name> moved you to <colour>.". False (nothing changes) unless the room is open
    /// (Phase::Room), both seats are 0 - 3 and different, the seat `from` holds a guest and the seat `to` is empty or holds a guest (a colour that a bot or the host holds does not move). This is the
    /// rule; who may ask for it, and with what guard, is the lobby's business (the leader's SeatMove) and a host that holds a seat has no leader to ask.
    bool move_seat(uint8_t from, uint8_t to);

    /// A connection that the listener accepted; it becomes a seat when its Hello is accepted. `address` is where the connection came from (the host
    /// part only): with the port the guest announces it tells the other guests where to reach it during the match (host migration).
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address = std::string());
    /// The same for a connection whose first message (its Hello) was read already: a server reads it to find the room by its code, then hands both over. `created`: this very Hello made the room (the
    /// server's door made it for the Hello): the Welcome of a new seat says so (kWelcomeCreated, protocol 16; it needs a key).
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address, const std::vector<uint8_t>& hello_message, bool created = false);
    void update(uint32_t now_ms);

    Phase phase() const noexcept { return phase_; }
    const RoomMsg& room() const noexcept { return room_; }
    size_t players() const noexcept;
    /// The seats that a person holds (the host and the guests, not the bots)
    size_t humans() const noexcept;
    bool occupied(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && room_.slots[seat].state != SlotState::Empty; }
    /// A match starts when enough seats are taken, a map is chosen, and at least one of the players is a person (a room of bots alone has nobody to play for)
    bool can_start() const noexcept { return phase_ == Phase::Room && players() >= cfg_.min_players && humans() >= 1 && !room_.map_name.empty(); }
    /// The same once every empty seat up to max_players has a bot (a leader's START with a fill level): one person is enough, because the bots make up the rest. Fog of War is not looked at
    /// here: the owner refuses the fill itself (add_bot and start refuse a room with fog and a bot) and tells the leader why.
    bool can_start_filled() const noexcept { return phase_ == Phase::Room && humans() >= 1 && !room_.map_name.empty() && cfg_.max_players >= cfg_.min_players; }
    /// The room's chat (protocol 11): every line that was said (a guest's, relayed to everybody; the host's own, see chat()), kept for the match's log, and the new ones for the owner
    const std::vector<ChatLine>& chat_log() const noexcept { return chat_.lines(); }
    std::vector<ChatLine> take_chat() { return chat_.take(); }
    /// How many lines were said in the room in all (the log keeps the last ChatLog::kMaxLines of them)
    uint64_t chat_total() const noexcept { return chat_.total(); }
    /// The host's own line (a host that holds a seat; a server's host has none): printable ASCII, at most kMaxChatChars characters, not empty. Relayed to every guest and kept in the log. False
    /// when nothing was said (no seat, an empty line, the match has begun).
    bool chat(const std::string& text);
    /// The room speaks to ONE guest (a Chat message from kRoomSender): a server's room tells its leader why it could not do what the START asked. Not kept in the log. False when the seat
    /// has no open connection or the text is empty.
    bool notify(uint8_t seat, const std::string& text);
    /// Removes the guest of a seat (Reject Kicked)
    void kick(uint8_t seat);
    /// A computer player takes `seat` (docs/BOTS.md): a slot in state Bot with no connection behind it. It counts as a player, its thumb is always good
    /// (round trip 0) and the start never waits for it. False unless the room is open (Room phase), the seat is free, there is room for another player and
    /// Fog of War is off (a bot would see through the fog). `name` is what the room shows ("Bot (Medium)"); the room's wire format is unchanged.
    bool add_bot(uint8_t seat, const std::string& name);
    /// Takes a bot out of the room (a bot seat of a started match stays)
    void remove_bot(uint8_t seat);
    /// True when a seat holds a bot
    bool has_bot() const noexcept;
    /// The measured round trip to a guest (the host itself: 0); false while nothing has come back yet
    bool measured(uint8_t seat) const noexcept;
    uint32_t rtt_ms(uint8_t seat) const noexcept;
    /// Every seated guest has been measured: "all players' thumbs have appeared"
    bool all_measured() const noexcept;

    /// Sends Start to everybody: the host loads the map itself too and reports host_loaded(). False unless can_start(), and (the last line of defence: the owner checks the teams first and tells the
    /// people why when they cannot be made) False when `teams` cannot be made for the seats that play: every engine would refuse such a Start. The teams go into the Start message (protocol 13).
    bool start(uint32_t seed, uint64_t map_hash, uint32_t now_ms, const sim::StartTeams& teams = sim::StartTeams{});
    /// Called by start() once the start message is built (start_info() is it, the keys are known) and BEFORE the first byte of it is sent to anybody: a server's room makes its restart record here, so that
    /// the record of a match exists before any machine can act on its Start. It must not call start() or cancel(). Unset (the default): nothing is called.
    void set_before_start(std::function<void(const StartMsg& start, uint32_t now_ms)> fn) { before_start_ = std::move(fn); }
    /// Protocol 16: the file name that the server gives a map of a PlanMsg ("" when it offers none by that name: the plan's map is then not taken, the rest of the plan is). Unset (the default): any valid_map_name.
    void set_map_choice(std::function<std::string(const std::string&)> fn) { map_choice_ = std::move(fn); }
    void host_loaded(bool ok);
    /// The host abandons the start (Cancel to everybody, back to the room)
    void cancel();
    const StartMsg& start_info() const noexcept { return start_; }
    /// Once Begun: the connection of a seat (nullptr for the host's own seat and empty seats)
    Connection* connection_of(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? guests_[seat].conn : nullptr; }
    /// The key that the guest of a seat was given with its Welcome: all zero for a seat without a guest (the host's own, a bot's, an empty one), for a lobby without a key maker, and for a guest
    /// that the maker gave none. Stays what it was after Begun (the session of the match is given the keys of the seats from here).
    SeatKey key_of(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? guests_[seat].key : SeatKey{}; }
    /// How many times a connection took a seat over with its key (the old one was closed)
    uint32_t takeovers() const noexcept { return takeovers_; }

    /// Protocol 16 (a lobby room: Config::lobby_room, after the constructor has checked what it needs). The leader's START waits for every person's game (RoomMsg::flags kRoomStarting): the request stands until the
    /// games are in (then the owner is given LeaderStart, again in every pass until it starts the match or drops the request with end_start), until it has waited Config::start_wait_ms, or until it cannot be
    /// honoured any more (the leader is another guest, or the room could not start with whom it has left). While it stands, no newcomer is taken (MatchRunning) and neither the plan nor the colours move.
    bool lobby_room() const noexcept { return cfg_.lobby_room; }
    bool starting() const noexcept { return starting_.active; }
    /// The seat holds a person whose connection ended and whose hold runs (the person counts as present: it can lead, and a Hello with its key takes the seat back) ...
    bool held(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && guests_[seat].held; }
    /// ... and the seat holds a person whose connection is a game (a Hello of kClientGame): the games are what a match is started with
    bool in_game(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && guests_[seat].conn != nullptr && guests_[seat].kind == kClientGame; }
    /// The owner drops the leader's START (it cannot honour it: the map, the colours, a place for the match): the flag goes, everybody is sent the Room message, the leader is sent `notice` (not when it is
    /// empty), and the holds that ran during the wait begin again from now. Nothing happens when no START waits.
    void end_start(const std::string& notice);
    /// PlanMsgs that were heard and not acted on (the sender does not lead, the room is no open lobby room, the START waits): no offence up to kIgnoredPlansAllowed per guest, a violation each after those
    uint32_t ignored_plans() const noexcept { return ignored_plans_; }
    /// NameMsgs that were heard and not acted on (the room is no open lobby room, the START waits): no offence up to kIgnoredNamesAllowed per guest, a violation each after those
    uint32_t ignored_names() const noexcept { return ignored_names_; }
    /// Names that changed (a NameMsg that gave a person a name that was not theirs already)
    uint32_t renames() const noexcept { return renames_; }
    /// RemoveMsgs that were heard and not acted on (the sender does not lead, the room is no open lobby room or its match loads, the seating is not the one the guard was made for, the seat holds nobody to remove or the leader):
    /// no offence up to kIgnoredRemovesAllowed per guest, a violation each after those (a Remove that reaches a match that runs is the running session's, and a violation there)
    uint32_t ignored_removes() const noexcept { return ignored_removes_; }
    /// People that the leader removed (a RemoveMsg that was done)
    uint32_t removals() const noexcept { return removals_; }
    /// Plans that changed the room (a map, a kind, a team), seats that were held after their connection ended, and holds that ran out
    uint32_t plan_changes() const noexcept { return plan_changes_; }
    uint32_t holds() const noexcept { return holds_; }
    uint32_t hold_expiries() const noexcept { return hold_expiries_; }
    /// Connections that were closed for their silence (silence_ms)
    uint32_t silences() const noexcept { return silences_; }
    /// A number that goes up whenever the people of the room do something with it: somebody is welcomed, a colour moves, the plan or a name changes, a person is removed, a line of chat is said, a START is asked for
    /// (a ping, a link that comes back, a seat that is held or gives up are not). A room in which it has stood still for a long time has people who are not using it: a link that answers its pings is alive and no use, and the owner may give
    /// the place of such a room up when it needs it.
    uint32_t activity() const noexcept { return static_cast<uint32_t>(joins_ + seat_moves_ + plan_changes_ + renames_ + removals_ + start_arms_ + chat_.total()); }

    std::vector<Event> take_events();

private:
    struct Guest {
        Connection* conn{nullptr};
        bool loaded{false};
        uint32_t violations{0};
        uint32_t next_ping_ms{0};
        uint32_t ping_nonce{0};                  // the last ping sent; its send time is ping_sent[nonce % 8] (a slow link has several in flight)
        uint32_t ping_sent[8]{};
        bool measured{false};
        uint32_t rtt_ms{0};
        std::string address;                     // where the guest's connection came from
        uint16_t listen_port{0};                 // the port on which it accepts the other guests during the match (0: none)
        uint32_t join_order{0};                  // 1, 2, 3, ... in the order of the Welcomes: the earliest guest still here leads a server's room
        SeatKey key{};                           // protocol 10: the key of the seat, handed out with the Welcome (all zero: none)
        MessageBudget talk;                      // flood control: every message that the guest sends takes one from it
        ChatBudget chat;                         // ... and every line of chat takes one from this one as well (flood.hpp: a burst of 5, then one a second; a line beyond it is dropped)
        uint32_t ignored_start_requests{0};      // the StartRequests of this guest that were ignored (the first kIgnoredStartRequestsAllowed are free)
        ChatBudget moves;                        // the SeatMoves of this guest that could be done (flood.hpp: a burst of kSeatMoveBurst, then kSeatMovesPerSecond a second; the budget of chat, with these numbers)
        uint32_t ignored_seat_moves{0};          // the SeatMoves of this guest that were ignored (the first kIgnoredSeatMovesAllowed are free)
        uint8_t kind{kClientGame};               // protocol 16: what the Hello said (a game, or a lobby page that cannot play a match)
        bool held{false};                        // a lobby room: the connection ended and the seat waits for its key (conn is null) since held_since_ms
        uint32_t held_since_ms{0};
        uint32_t heard_ms{0};                    // a lobby room: when a message of this guest was last read (or it was welcomed): the silence of a link is counted from here
        ChatBudget plans;                        // the PlanMsgs of this guest that could be heard (flood.hpp: a burst of kPlanBurst, then kPlansPerSecond a second)
        uint32_t ignored_plans{0};               // ... and those that were ignored (the first kIgnoredPlansAllowed are free)
        ChatBudget names;                        // the NameMsgs of this guest that could be heard (flood.hpp: a burst of kNameBurst, then kNamesPerSecond a second)
        uint32_t ignored_names{0};               // ... and those that were ignored (the first kIgnoredNamesAllowed are free)
        ChatBudget removes;                      // the RemoveMsgs of this guest that could be done (flood.hpp: a burst of kRemoveBurst, then kRemovesPerSecond a second)
        uint32_t ignored_removes{0};             // ... and those that were ignored (the first kIgnoredRemovesAllowed are free)
        uint32_t forgive_at_ms{0};               // a lobby room: when one violation and one ignored request of each kind are forgiven next
    };
    struct Starting {                            // the leader's START of a lobby room, while it waits for every person's game
        bool active{false};
        uint32_t leader_order{0};                // the join order of the guest whose request it is (a guest keeps it when its seat changes)
        uint32_t since_ms{0};
    };
    struct Pending {
        Connection* conn;
        uint32_t since_ms;
        std::string address;
    };
    void broadcast_room();
    void broadcast(const std::vector<uint8_t>& msg);
    void remove_guest(uint8_t seat, bool notify_reject, RejectReason reason);
    void violation(uint8_t seat);
    void handle_hello(Pending& p, const std::vector<uint8_t>& msg, uint32_t now_ms, bool& consumed, bool created = false);
    SeatKey new_key() const;
    uint8_t seat_of_key(const SeatKey& key) const noexcept;
    void take_over(uint8_t seat, Pending& p, const HelloMsg& hello, uint32_t now_ms);
    void handle_guest_message(uint8_t seat, const std::vector<uint8_t>& msg, uint32_t now_ms);
    void relay_chat(uint8_t sender, const std::string& text);
    void check_all_loaded();
    void cancel_with(CancelMsg::Reason reason, uint8_t player);
    /// A dedicated server's room that allows an early start has a leader
    bool leads() const noexcept { return cfg_.host_seat >= sim::MAX_PLAYERS && cfg_.early_start; }
    /// The leader is the guest with the earliest Welcome among those who are here (recomputed whenever somebody joins or leaves, before the room is broadcast)
    void elect_leader();
    /// Whether move_seat would move a guest now (the rule alone, no guard, no budget): the room is open, both are different seats, `from` holds a guest and `to` is empty or holds a guest
    bool can_move_seat(uint8_t from, uint8_t to) const noexcept;
    /// Whether the leader could take the person of `target` out of the room now (the rule alone, no guard, no budget): the room is open (a START that waits does not forbid it), and the seat holds a person
    /// who is not the leader (an empty seat and a bot's hold nobody)
    bool can_remove(uint8_t target) const noexcept;
    // The lobby room (protocol 16)
    /// A guest sits in the seat: connected or held
    bool person(uint8_t seat) const noexcept { return guests_[seat].conn != nullptr || guests_[seat].held; }
    void hold_guest(uint8_t seat, uint32_t now_ms);
    void forgive(Guest& g, uint32_t now_ms) const noexcept;
    void apply_plan(const PlanMsg& plan);
    void rename(uint8_t seat, const std::string& raw);                    // protocol 16: the person of `seat` goes by this name (a name that looks like a bot's is not taken)
    uint8_t joiner_seat(uint8_t want) const noexcept;
    bool plan_asks_bots() const noexcept;
    bool everyone_in_game() const noexcept;
    bool start_possible() const noexcept;
    void arm_start(uint8_t seat, uint32_t now_ms);
    void pump_start(uint32_t now_ms);
    void sync_room_flags();

    Config cfg_;
    RoomMsg room_;
    StartMsg start_;
    uint32_t last_update_ms_{0};      // the clock of the current update (the measurements use it)
    Phase phase_{Phase::Room};
    std::array<Guest, sim::MAX_PLAYERS> guests_{};
    bool host_loaded_{false};
    uint32_t load_started_ms_{0};
    std::vector<Pending> pending_;
    std::vector<Event> events_;
    uint32_t joins_{0};                      // the Welcomes sent so far (Guest::join_order)
    uint32_t ignored_start_requests_{0};
    uint32_t ignored_seat_moves_{0};
    uint32_t seat_moves_{0};
    uint32_t takeovers_{0};
    Starting starting_;
    uint32_t ignored_plans_{0};
    uint32_t ignored_names_{0};
    uint32_t renames_{0};
    uint32_t ignored_removes_{0};
    uint32_t removals_{0};
    uint32_t plan_changes_{0};
    uint32_t holds_{0};
    uint32_t hold_expiries_{0};
    uint32_t silences_{0};
    uint32_t start_arms_{0};                 // the STARTs that were asked for and stood (arm_start)
    ChatLog chat_;
    std::function<void(const StartMsg&, uint32_t)> before_start_;
    std::function<std::string(const std::string&)> map_choice_;
};

class ClientLobby {
public:
    struct Config {
        std::string name{"Player"};
        uint32_t welcome_timeout_ms{10000};
        uint16_t listen_port{0};             // where this guest accepts the other guests during the match, announced in Hello (0: nowhere)
        uint8_t want_seat{255};              // the seat this guest asks for in Hello (0 .. 3; 255: any). Taken, or the host's: the first free seat
        std::string room;                    // the room of a server (valid_room_code), "" for a LAN / direct host
        std::string token;                   // the credential that came with the room code ("" when none)
        uint32_t ping_every_ms{1000};        // the guest measures its own round trip to the host this often once it has a seat (the "ping" next to the frame rate)
        SeatKey key{};                       // protocol 10: the key of the seat that this machine had (a page that was reloaded, a game that was started again): the Hello shows it and the
                                             // server gives the seat back. All zero: a new player. Its turns (Hello::have_turns) are 0: this lobby starts from nothing
        uint8_t platform{0};                 // protocol 15: what this machine runs on, told in the Hello (valid_platform; 0: not told)
        uint8_t client_kind{kClientGame};    // protocol 16: what this machine is, told in the Hello: a game (kClientGame), or the front page's lobby (kClientPage), which cannot play a match and is only seated in a lobby room
        std::optional<CreateBlock> create;   // protocol 15: the choices of the room that this Hello makes when the room does not exist (a server's public rooms); none: the Hello joins a room
    };
    enum class Phase : uint8_t { Connecting, Joining, InRoom, Loading, Loaded, Begun, Rejected, Closed };
    struct Event {
        enum class Type : uint8_t { RoomChanged, StartRequested, Begun, Cancelled, Rejected, Disconnected, Chat };       // Chat (protocol 11): a line arrived; take_chat() has it
        Type type{Type::RoomChanged};
        uint8_t seat{255};                      // Chat: the sender's seat, kRoomSender (255) for a notice of the room itself
        RoomMsg room{};                         // RoomChanged: the Room message of this event. update() can read several in one call and room() has the last one only; the owner that compares each
                                                // message with the one before it (NetGame's follow of a move) takes them from the events
    };

    ClientLobby(Connection* connection, Config config) : conn_(connection), cfg_(std::move(config)) {}

    /// Says Hello now when the connection is open (and no Hello has been sent): a browser tab that is not drawn runs no frames, so no update() comes, and the
    /// server closes a connection that does not say Hello within seconds. update() does the same on its own; the timer of the Hello starts at the next update().
    void send_hello();
    void update(uint32_t now_ms);
    Phase phase() const noexcept { return phase_; }
    uint8_t my_seat() const noexcept { return seat_; }
    const RoomMsg& room() const noexcept { return room_; }
    /// This machine leads the room (the last Room message names its seat as the leader; a dedicated server's room only)
    bool is_leader() const noexcept {
        return (phase_ == Phase::InRoom || phase_ == Phase::Loading || phase_ == Phase::Loaded) && seat_ < sim::MAX_PLAYERS && room_.leader == seat_;
    }
    /// The leader asks the server to start the match now with the players who are here (StartRequest). False unless this machine leads an open room (InRoom) and the message went
    /// out. The server decides: it starts only when it can (two players at least) and says nothing to a request that it cannot honour. With fill levels (protocol 11; one for each seat since
    /// protocol 13) it also seats a bot of that level in each seat that is still empty (up to the room's player count), and then one person is enough; with Fog of War it refuses the bots, says
    /// why (a notice, see take_chat()) and starts without them if two people are there. `teams` (protocol 13) are the teams the match starts with when the seats that play can make them (else it
    /// starts without and tells everybody why).
    bool request_start(const std::array<FillLevel, sim::MAX_PLAYERS>& fill = {}, const sim::StartTeams& teams = sim::StartTeams{});
    /// The same level in every seat (protocol 11's single choice)
    bool request_start(FillLevel level);
    /// The leader asks the server to put the player of seat `from` in seat `to` (SeatMove; a free colour since protocol 14, a guest's colour, a swap, since 15). False (nothing is sent) unless this
    /// machine leads an open room (InRoom) and the seats are two different ones of 0 - 3. The request carries the guard of the seating as this machine shows it (seating_hash). True means the request
    /// was sent, not that the server did it: it answers with the Room message that shows the new seats, and says nothing to a request that it cannot honour (a bot's or the host's colour, the seating
    /// has changed, the room started meanwhile).
    bool request_seat_move(uint8_t from, uint8_t to);
    /// The leader of a lobby room (protocol 16) tells the server its plan: the map, what each colour is, the teams (PlanMsg). False (nothing is sent) unless this machine leads an open room that the last Room
    /// message shows as a lobby room. True means the request was sent, not that the server did it: the Room message shows the plan that the room has (it says nothing to a plan that it cannot hear).
    bool request_plan(const PlanMsg& plan);
    /// A person of a lobby room (protocol 16) tells the server the name they go by from now on (NameMsg): printable ASCII, at most kMaxNameChars characters, no space at either end (a longer or
    /// otherwise written name is cut or cleaned by the same rule as the Hello's, and an empty one sends nothing). False (nothing is sent) unless this machine sits in an open room that the last Room message shows as
    /// a lobby room. True means the request was sent, not that the server did it: the Room message shows the name that the room has (it says nothing to a name that it cannot hear).
    bool request_name(const std::string& name);
    /// The leader of a lobby room (protocol 16) asks the server to take the person of `seat` out of the room (RemoveMsg, with the guard of the seating as this machine shows it). False (nothing is sent) unless
    /// this machine leads an open room that the last Room message shows as a lobby room and the seat holds a person who is not this machine's own (a bot's and an empty seat hold nobody). True means the request
    /// was sent, not that the server did it: the Room message shows the seat free when it did (it says nothing to a request that it cannot honour: the seating has changed, a match loads).
    bool request_remove(uint8_t seat);
    /// Says a line in the room (protocol 11): in the waiting room, while the map loads and while this machine waits for the match to begin. Printable ASCII, at most kMaxChatChars characters
    /// (a longer line is cut), not empty. The room relays it to everybody, this machine included: the line comes back through take_chat(). False when nothing was sent.
    bool chat(const std::string& text);
    /// Every line that was said in the room since this machine was welcomed (the last ChatLog::kMaxLines), the room's notices to this machine among them, and the new ones (the Chat event)
    const std::vector<ChatLine>& chat_log() const noexcept { return chat_.lines(); }
    std::vector<ChatLine> take_chat() { return chat_.take(); }
    /// The host's Start (valid from Loading on): the map, the seed, the roster
    const StartMsg& start_info() const noexcept { return start_; }
    RejectReason reject_reason() const noexcept { return reject_; }
    /// The connection was open at some time (the Hello went out): a lobby that closed without it never reached its server
    bool was_open() const noexcept { return was_open_; }
    /// The server accepted the connection and sent no Welcome within `welcome_timeout_ms` (10 s): the lobby closed the connection itself (a server that does not answer, not one that hung up)
    bool welcome_timed_out() const noexcept { return welcome_timed_out_; }
    /// The key that the Welcome carried (valid from InRoom on): all zero when the host offers no way back (a LAN or direct host, a room that holds no seats)
    const SeatKey& key() const noexcept { return key_; }
    /// The Welcome said that a Hello with a key was accepted into a match that is running or loading (kWelcomeRejoin): the server goes on with Start (this machine starts from nothing),
    /// no Room message comes, and the flow is Welcome -> Start -> report_loaded -> Begin -> Begun as for anybody. False after the Welcome of a seat in the room (also one that took its
    /// seat over in the waiting room: the room follows).
    bool rejoined() const noexcept { return rejoined_; }
    /// The Welcome said that this very Hello made the room (kWelcomeCreated, protocol 16): a visitor whose link led to no room, and was given a new one
    bool created() const noexcept { return created_; }
    CancelMsg::Reason cancel_reason() const noexcept { return cancel_reason_; }
    /// The seat that caused the cancel (a player who left, a machine that could not load the map), 255 when unknown
    uint8_t cancel_player() const noexcept { return cancel_player_; }

    /// The map named by start_info() is loaded (ok) or cannot be (missing file, a different file)
    void report_loaded(bool ok);
    void leave();
    std::vector<Event> take_events();
    /// This guest's own measurement of its round trip to the host (latency.hpp): it pings the host once a second from the moment it has a seat (the room's host and a
    /// server's room answer a guest's Ping, so the protocol is unchanged); nothing is measured before the first answer
    const PingMeter& ping() const noexcept { return ping_; }

private:
    Connection* conn_;
    Config cfg_;
    Phase phase_{Phase::Connecting};
    RoomMsg room_;
    StartMsg start_;
    uint8_t seat_{255};
    SeatKey key_{};
    bool rejoined_{false};
    bool created_{false};
    RejectReason reject_{RejectReason::BadRequest};
    CancelMsg::Reason cancel_reason_{CancelMsg::Reason::HostCancelled};
    uint8_t cancel_player_{255};
    uint32_t joined_at_ms_{0};
    bool joined_stamp_pending_{false};      // the Hello went out through send_hello(): update() stamps the time
    PingMeter ping_;
    uint32_t next_ping_ms_{0};
    bool ping_armed_{false};                // the first ping goes out as soon as the guest has a seat (the deadline is taken from the clock then: clock.hpp)
    bool was_open_{false};                  // the connection was open when the Hello was sent
    bool welcome_timed_out_{false};         // no Welcome came within the limit (see welcome_timed_out())
    std::vector<Event> events_;
    ChatLog chat_;
};

}  // namespace ants::net
