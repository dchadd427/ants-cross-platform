#pragma once

// The messages of a lock-step match. A message is one datagram of the transport (WebRTC data channels, WebSocket and the TCP framing all deliver
// whole messages): u8 type, then the payload. Every decoder checks every length and count before it uses it (the original trusted them: an
// unbounded 2 KB stack receive, unchecked type / count / index fields), and rejects trailing bytes.
//
// Star topology: the room owner (host) is the sequencer. Clients send Command, TurnAck, Hash, Ping and Chat to it; it answers with Turn (the
// sealed commands of one 50 ms turn, which is one tick, in canonical order, issuer stamped from the connection), Desync, Pong and Chat, and a dedicated
// server tells the room with Lag when a player is far behind the match.
//
// Coming back (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A dedicated server gives every seat a KEY (128 random bits, in the Welcome). A player whose connection is lost
// shows the key in a new Hello and gets its seat back. Meanwhile the server tells the others who is missing and what the vote says (Presence), they vote (Vote: keep
// waiting / continue without), and the player who returns is given the match from the server's turn log (CatchUp, then TurnBatch messages of consecutive turns, each as
// big as a message may be) and reports its state hash when it has executed them all (CaughtUp). Old-layout Hellos (protocols 6 to 9: protocol 9 changed the rules of the match, not a message) are still answered "version mismatch":
// decode_hello_prefix reads the version and the name, which lead every layout. Who sends what: the room's door takes a Hello with a key to the running room (ants_server, RoomManager), the host's session
// (HostSession::accept_rejoin, session.hpp) answers Welcome (flags 1), Start and CatchUp, or a Reject (a key that fits no seat is told MatchRunning, like a Hello without a key: nothing is revealed), and
// the returning client's session (ClientSession, Mode::Rejoining / CatchingUp) answers Loaded, TurnAck and CaughtUp. A host that holds no seats (a LAN or direct host, a room without reconnect) never sends
// Presence, CatchUp or TurnBatch and answers a key like any Hello.

// Bots fill the empty seats, and chat in the waiting room (protocol 11, docs/NETWORK_PORT.md "Protocol 11"). The StartRequest of a server's room leader carries a FILL LEVEL (none / easy
// / medium / hard): when the server starts the match on that request, every seat that is still empty (up to the room's player count) gets a bot of that level, named "Bot (Easy)" and so on
// (a person can never take such a name: the lobby renames it), and the server runs the bots as virtual clients. Chat works in the waiting room too, and while the map loads: the Chat message is
// the match's, unchanged (sender, team flag, text of at most 100 printable characters); a line from a guest is relayed to everybody in the room with the sender's seat stamped (team chat does
// not exist before the match: the flag is cleared), a line with sender kRoomSender (255) is the room itself speaking (a notice to the leader).

// The start of a match (protocol 12, docs/NETWORK_PORT.md "Protocol 12"). Every match opens with the original's "Get ready to play!" dialog, and the remake's simulation WAITS for it (a
// deliberate deviation from the original, which runs the clock behind the dialog): the host seals the FIRST TURN kMatchStartDelayMs after the match begins (HostSession::Config::start_delay_ms,
// which the LAN host's NetGame and the server's Room set), so that every simulation starts together at the same turn and nobody's clock runs behind a dialog, and every machine opens its dialog
// when the match begins and closes it when its first turn EXECUTES (the application's tick hook), the moment its ants can move. Nothing is sealed before the first turn, so the wait is no
// stall, no lag and no pause of the link: the runner is not started yet (it begins when its buffer has its first turns), the lag policy has nothing that waits for a player, and the
// sequencer has no turn that a seat could be behind. The number is a constant of the protocol, not a field of a message: no machine needs it (a client closes its dialog on an event, not at a
// time), and a message would be one more thing for a decoder to refuse.

// A level for each seat of the fill, and teams chosen before the start (protocol 13, docs/NETWORK_PORT.md "Protocol 13"). The StartRequest of a server's room leader carries a fill level FOR EACH SEAT (seats 0 - 3:
// none / easy / medium / hard; protocol 11 had one level for all of them) and the TEAMS that the leader chose: a pair of different seats, or 255 255 for free for all. The room seats the bots of the
// levels in the empty seats (the seat of a person who took it meanwhile is skipped), checks the teams against the seats that play, and puts them into the Start message, which every engine of the match
// starts from (the machines, the referee, a machine that comes back from nothing, a restored room): each calls sim::apply_start_teams right after init() and before its first tick, so the alliances are
// the original's own commands applied the same way everywhere. A match without teams is byte for byte what it was.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"

namespace ants::net {

// THE RULE of this number: it changes with every change that two peers of a match must share. That is the messages (a layout, a type, a rule of a decoder) AND the simulation's rules:
// any fix or feature that changes the state hash of some play (a rule of the engine, a table or a flag that the engine reads, the way a level is read), even one that shows only on some maps
// or in some rare sequence of orders, because peers that run different rules desynchronise in the first play where they differ, and the door checks nothing else: a Hello of another
// number is refused ("version mismatch") by a LAN host and by a server's room, and a LAN announcement of another number is listed as another version. A release that cannot say "no
// state hash of any play changed" raises it, and the history below says why (nothing in the wire changed in 9: the rules of the engine did; nothing in 12: when the match starts did).
inline constexpr uint16_t kProtocolVersion = 13;         // 2: the Room message carries each seat's round trip (the thumbs); 3: host migration (mesh, election); 4: the Quit command (Drop moved from 11 to 12); 5: Hello carries the seat that the guest asks for; 6: map names may hold any printable character that cannot leave the maps folder (up to 64), Hello carries a room code and a token, the slot state Bot, the rejection NoSuchRoom; 7: the Room message names the room's leader (a dedicated server's room: the first player who joined), the message StartRequest (the leader asks the server to start now); 8: turns of 50 ms with one tick each (they were 100 ms with two; a client keeps a jitter buffer of 1 to 4 turns, ants_net/jitter.hpp), a state hash every 20 turns (one second, as before), and the Lag message, type 25 (a dedicated server never waits for a player that falls behind: it tells the room instead); 9: the community-map rules (default ant types, power-ups by tile, the attack clip); 10: rejoin keys, presence, votes, the catch-up stream (every seat of a server's room has a key that the Welcome hands out, and a Hello that shows it takes the seat back: Hello carries the key and the number of turns the client has, Welcome the key and flags; the rejections Dropped, RejoinFailed and Superseded; the messages Presence, Vote, CatchUp, TurnBatch and CaughtUp, types 26 - 30; Presence ends with the seconds left of the resume countdown that follows a pause); 11: bots fill empty seats at the leader's START, chat in the waiting room (StartRequest carries a fill level: two bytes now; the room's own notices travel as Chat with sender 255); 12: the match clock waits for the "Get ready to play!" dialog (the host seals the first turn kMatchStartDelayMs after the match begins, a client's dialog ends when its first turn executes: no message changed, but a client of 11 would count its dialog in simulation ticks and be blocked for 100 ticks of the running match after the late first turn); 13: a level for each seat of the leader's fill and teams in the start data (StartRequest is [24][fill0 .. fill3][team_a][team_b], seven bytes; Start ends with team_a and team_b; every engine makes the teams before its first tick)
/// Protocol 12: the host seals the first turn of a match this long after the match begins (Begin sent, the sessions started): the length of the "Get ready to play!" dialog, sim::kMatchStartDialogMs.
/// A host with a seat (a game on the local network) and a dedicated server's room both do it; the simulation does not run on any machine before that first turn.
inline constexpr uint32_t kMatchStartDelayMs = sim::kMatchStartDialogMs;
inline constexpr size_t kMaxMessageBytes = 64 * 1024;
inline constexpr size_t kMaxTurnCommands = 512;
inline constexpr size_t kMaxChatChars = 100;        // the original's chat entry
inline constexpr size_t kMaxNameChars = 32;
/// A turn is one tick: the host seals one every 50 ms, every machine applies its commands and runs one tick (protocol 8; before, 100 ms and two ticks). Everything that is
/// counted in turns is derived from a time with these (a second is kTurnsPerSecond turns): never write a number of turns that stands for a time.
inline constexpr uint32_t kTurnMs = 50;
inline constexpr uint32_t kTicksPerTurn = 1;
inline constexpr uint32_t kTurnsPerSecond = 1000 / kTurnMs;
inline constexpr uint32_t turns_for_ms(uint32_t ms) noexcept { return (ms + kTurnMs - 1) / kTurnMs; }
inline constexpr uint32_t kHashEveryTurns = 20;     // a state hash every 20 ticks: one second

enum class MsgType : uint8_t {
    None = 0,
    Hello = 1,      // client -> host: protocol version, display name
    Welcome = 2,    // host -> client: the player slot it plays
    Reject = 3,     // host -> client: why it cannot join
    Command = 4,    // client -> host: one player command
    Turn = 5,       // host -> everybody: a sealed turn
    TurnAck = 6,    // client -> host: the highest turn the client has executed
    Hash = 7,       // client -> host: the state hash after a turn
    Desync = 8,     // host -> everybody: two peers disagree
    Chat = 9,       // both ways (the host relays)
    Ping = 10,      // either way
    Pong = 11,      // the answer, echoing the nonce
    Room = 12,      // host -> everybody: who sits where, the map, the fog option (sent on every change)
    Start = 13,     // host -> everybody: load this map with this seed now
    Loaded = 14,    // client -> host: the map is loaded (or could not be)
    Begin = 15,     // host -> everybody: everybody is loaded, the match begins
    Cancel = 16,    // host -> everybody: the start failed, back to the room
    Leave = 17,     // client -> host: I leave
    Propose = 18,   // survivor -> peers: the host is gone, I take over as host (host migration)
    Accept = 19,    // peer -> candidate: agreed, this is how far I have got
    Refuse = 20,    // peer -> candidate: not you (a lower seat lives, or my host is alive)
    Resume = 21,    // new host -> peers: I am the host from this turn on
    Request = 22,   // peer -> peer: send me the turns from this one on
    PeerHello = 23, // guest -> guest on a new link between guests: who I am
    StartRequest = 24,   // leader -> server (protocol 7): start the match now with the players who are here, with the bots of the fill (11, a level for each seat since 13) and the teams (13); only the leader of a server's room is heard
    Lag = 25,       // dedicated server -> the other players (protocol 8): a player is more than 3 s behind the match (or is not any more)
    Presence = 26,  // dedicated server -> the players (protocol 10): who is missing from the match and for how long, the vote about the seat that has been away longest (or flaps), the cap on the pauses, the resume countdown
    Vote = 27,      // player -> dedicated server (protocol 10): keep waiting for the seat that is missing / continue without it
    CatchUp = 28,   // dedicated server -> a player who came back (protocol 10): the turns of the match follow, this many in all (first_turn ..)
    TurnBatch = 29, // dedicated server -> a player who came back (protocol 10): consecutive sealed turns of the match, packed (the stream that CatchUp announces)
    CaughtUp = 30,  // a player who came back -> dedicated server (protocol 10): I have executed every turn of the stream, my state hash is this
    Last = CaughtUp
};

/// Longest map file name that travels (a plain name of the maps folder, ending in ".lvl" / ".LVL")
inline constexpr size_t kMaxMapNameChars = 64;
/// The name of a map file of the maps folder, as it travels in the Room, Start and LAN messages: 5 .. kMaxMapNameChars printable ASCII characters (0x20 - 0x7E: the
/// community's names hold spaces, '!', '~', '#', '&', ... and even ".."), none of `/ \ : * ? " < > |` (so a name can never name a path), not starting with '.',
/// ending in ".lvl" or ".LVL"
bool valid_map_name(const std::string& name) noexcept;
/// A room code (a server hosts many rooms): empty = no room (a LAN or direct host), else 1 .. kMaxRoomCodeChars of letters, digits, '_' and '-'
inline constexpr size_t kMaxRoomCodeChars = 32;
inline constexpr size_t kMaxTokenChars = 64;                     // an opaque credential that a lobby hands out with the room code (printable, never interpreted by the game)
bool valid_room_code(const std::string& code) noexcept;

/// The prefix of the codes that a server makes a room for on the first Hello when it is run with demo rooms ("demo-<map>-<n>p-<random>"); the server's choice of map and players is read from the words after it
inline constexpr const char* kDemoRoomPrefix = "demo-";
/// The teams that a room's own code names (protocol 13): a word `t<a><b>` of a demo room's code (two different seats 0 - 3, the lower first: `t01`, `t13`), between dashes or at the code's end, in any
/// case, and never the first word after the prefix (that is the map's or the player count's). "demo-treasure-4p-t01-k7m2xq": seats 0 and 1 are a team (the other two too when both play). The
/// first such word counts; no word (every code made before this: the pages' random part is six characters), no teams. The room makes them for EVERY start, the automatic start of a full room
/// included (docs/NETWORK_PORT.md, "Protocol 13"). The server and every client read the code with this one function; whether the seats can make them is sim::plan_start_teams's business at the start.
sim::StartTeams room_code_teams(const std::string& code) noexcept;
/// The word for a pair, `t01`: empty for free for all and for a pair that is no word (equal or unordered seats, a seat above 3)
std::string room_code_team_word(const sim::StartTeams& teams);

/// Dropped (protocol 10): the key is right and the seat was dropped (by the others' vote, by the cap on the pauses, by a violation): the player is told it is out ("Sorry, you
/// have been dropped from the game", the original's own text). RejoinFailed: the key is right but the way back is closed (the server's turn log is not usable, or it cannot tell a
/// machine that has nothing how to load the match, or that machine could not load the map, or the seat has used up what a key holder may ask of the server: the catch-up time of the
/// absence, three attempts a minute, three times the log's size in ten minutes; and on the machine's side, a stream that does not fit its announcement or the server's verdict that its
/// state differs from the referee's: the session ends with this reason). The seat stays held: the others may vote and the cap applies as for any absent seat.
/// Superseded: a newer connection with the key took the seat; sent to the older one just before it is closed, so that the older window stops trying.
enum class RejectReason : uint8_t { Full = 1, VersionMismatch = 2, MatchRunning = 3, Kicked = 4, BadRequest = 5, NoSuchRoom = 6, Dropped = 7, RejoinFailed = 8, Superseded = 9 };

/// A seat's KEY (protocol 10): 128 random bits that a server hands out in the Welcome of the seat (the server makes them, ants_net never reads the operating system's generator:
/// this library is built for the web too). Whoever shows the key of a seat in a Hello gets the seat back. All zero = no key (a LAN or direct host, a room without a way back).
inline constexpr size_t kKeyBytes = 16;
using SeatKey = std::array<uint8_t, kKeyBytes>;
inline bool key_is_zero(const SeatKey& k) noexcept {
    uint8_t any = 0;
    for (const uint8_t b : k) any = static_cast<uint8_t>(any | b);
    return any == 0;
}
/// True when two keys are the same key. Compared in constant time (the loop has no early exit: the time does not tell how many bytes of a guess were right), and "no key" is
/// never the same key as anything, not even another "no key".
inline bool key_matches(const SeatKey& a, const SeatKey& b) noexcept {
    uint8_t diff = 0;
    for (size_t i = 0; i < kKeyBytes; ++i) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0 && !key_is_zero(a);
}

struct HelloMsg {
    uint16_t version{kProtocolVersion};
    std::string name;
    uint16_t listen_port{0};    // the port on which this guest accepts the other guests' connections during the match (0: none)
    uint8_t want_seat{255};     // the seat (0 .. 3) this guest asks for, 255: any; a seat that is taken (or the host's own) gives the first free one (protocol 5)
    std::string room;           // the room of a server that this guest wants ("" for a LAN / direct host: valid_room_code); a room that does not exist is Rejected NoSuchRoom (protocol 6)
    std::string token;          // the credential that came with the room code (kMaxTokenChars printable characters; "" when none): carried, never interpreted here (protocol 6)
    SeatKey key{};              // protocol 10: the key of the seat that this client had (all zero: a new player). The server answers a key that fits a seat with that seat, whatever want_seat says
    uint32_t have_turns{0};     // protocol 10: with a key: how many turns of the match this client has executed already (0: it starts from nothing and is sent Start first). Zero without a key
};
struct WelcomeMsg {
    uint8_t player{255};        // the slot (0 .. 3) this client plays
    uint8_t players{0};         // how many slots the room has
    SeatKey key{};              // protocol 10: the key of this seat (all zero: this room offers no way back: a LAN or direct host, a room that does not hold seats)
    uint8_t flags{0};           // protocol 10: bit 0 (kWelcomeRejoin) = a Hello with a key was accepted in a running match: Start (from nothing) or CatchUp follow, no Room message does; 0 or 1 only, and 1 needs a key
};
inline constexpr uint8_t kWelcomeRejoin = 1;
struct RejectMsg {
    RejectReason reason{RejectReason::BadRequest};
};
struct CommandMsg {
    sim::Command command;       // the issuer field is ignored: the host stamps the connection's player
};
struct TurnMsg {
    uint32_t turn{0};
    std::vector<sim::Command> commands;   // canonical order, issuers stamped
};
struct AckMsg {
    uint32_t turn{0};
};
struct HashMsg {
    uint32_t turn{0};
    sim::StateHash hash;
};
struct DesyncMsg {
    uint32_t turn{0};
    uint8_t player{255};        // the peer whose hash differs from the host's
    sim::StateHash host;
    sim::StateHash peer;
};
struct ChatMsg {
    uint8_t sender{255};        // stamped by the host on relay
    bool team{false};
    std::string text;           // at most kMaxChatChars
};
struct PingMsg {
    uint32_t nonce{0};
    uint32_t sent_ms{0};
};

/// What a seat of the room holds
enum class SlotState : uint8_t { Empty = 0, Host = 1, Client = 2, Bot = 3 };       // Bot: a computer player (docs/BOTS.md): no connection behind the seat

/// The connection quality shown as a thumb beside a player's name on the setup screen (animations netgood, netok, netbad, netunk). The thresholds are
/// the original's (Ants.exe 0x1013289): a measured latency below 1200 ms is good, below 1800 ms is ok, anything more is bad; a peer that is connected
/// but not measured yet shows the question mark.
enum class LinkQuality : uint8_t { Good = 0, Ok = 1, Bad = 2, Unknown = 3 };
inline constexpr uint16_t kRttUnknown = 0xFFFF;
inline constexpr uint32_t kQualityGoodBelowMs = 1200;
inline constexpr uint32_t kQualityOkBelowMs = 1800;
inline LinkQuality link_quality(uint16_t rtt_ms) noexcept {
    if (rtt_ms == kRttUnknown) return LinkQuality::Unknown;
    return rtt_ms < kQualityGoodBelowMs ? LinkQuality::Good : (rtt_ms < kQualityOkBelowMs ? LinkQuality::Ok : LinkQuality::Bad);
}

/// The room's leader in a RoomMsg when the room has none (every LAN / direct room, and a server's room that does not allow an early start)
inline constexpr uint8_t kNoLeader = 255;

/// The sender of a Chat message that the room itself says (protocol 11): a notice to a player ("Bots cannot play with Fog of War."), not a player's line. What a client writes in the sender
/// byte of its own Chat messages does not matter (the host stamps the seat of the connection on relay), so the value only means "the room speaks" in a message that a client RECEIVES.
inline constexpr uint8_t kRoomSender = 255;
/// The notices that a server's room sends to its leader when it cannot do what the START asked (a Chat message from kRoomSender; each is at most kMaxChatChars)
inline constexpr const char* kNoticeFillFog = "Bots cannot play with Fog of War.";
inline constexpr const char* kNoticeFillMap = "This map cannot be played by every seat: no bots in the empty seats.";
/// The room's notice when the teams that the leader chose cannot be made for the seats that play (protocol 13): "No teams: " and the reason (sim::StartTeamsPlan::short_why), sent to every person in the room
/// as the match starts without teams; at most kMaxChatChars (the reasons are written to fit).
inline constexpr const char* kNoticeNoTeams = "No teams: ";

struct RoomMsg {
    struct Slot {
        SlotState state{SlotState::Empty};
        std::string name;
        uint16_t rtt_ms{kRttUnknown};   // the host's measured round trip to this seat (0 for the host's own seat), kRttUnknown before the first answer
    };
    std::array<Slot, sim::MAX_PLAYERS> slots{};
    std::string map_name;         // the file name of the host's map ("" until the host has chosen one)
    bool fog{false};
    uint8_t you{255};             // the receiver's own seat (set per recipient by the host)
    /// The seat of the room's leader, kNoLeader (255) when there is none; the same value goes to everybody (protocol 7). Only a dedicated server's room has a leader: the first
    /// player who joined (the earliest Welcome), and when the leader leaves the earliest of the players who are left. The leader may ask the server to start early (StartRequest).
    /// A leader is always a seat that a person holds as a guest (SlotState::Client): the decoder refuses anything else.
    uint8_t leader{kNoLeader};
};
/// Which bots a leader's START asks the server to seat in the empty seats of its room (protocol 11). None: the match starts with the people who are there, as in protocol 7.
enum class FillLevel : uint8_t { None = 0, Easy = 1, Medium = 2, Hard = 3 };
inline constexpr uint8_t kFillLevelLast = 3;
/// "none", "easy", "medium", "hard"
const char* fill_level_name(FillLevel level) noexcept;
/// The words of fill_level_name, in either case; false (and `out` unchanged) for anything else
bool parse_fill_level(std::string_view text, FillLevel& out) noexcept;
/// "Easy", "Medium", "Hard" (the level as a person is told: "Empty seats will be Medium bots"); empty for None
std::string fill_level_title(FillLevel level);
/// What the room calls the bot that a fill seats: "Bot (Easy)", "Bot (Medium)", "Bot (Hard)" (the name of the standard bot of that level: ants_ai's bot_display_name says the same; a
/// person can never take a name that starts with "Bot (": the lobby renames it). Empty for None.
std::string fill_bot_name(FillLevel level);
/// The bots that a START asks for: the level of each seat (protocol 13; protocol 11 had one level for all of them). A single level converts to the plan that gives every seat that level, so a
/// choice of one level ("--fill-bots hard", the one-level calls of protocol 11) still means what it meant.
struct FillPlan {
    std::array<FillLevel, sim::MAX_PLAYERS> level{};
    constexpr FillPlan() noexcept = default;
    constexpr FillPlan(FillLevel all) noexcept : level{all, all, all, all} {}                                                     // (implicit on purpose)
    constexpr FillPlan(const std::array<FillLevel, sim::MAX_PLAYERS>& per_seat) noexcept : level(per_seat) {}                     // (implicit on purpose)
    /// Some seat has a level
    constexpr bool any() const noexcept {
        for (const FillLevel l : level) {
            if (l != FillLevel::None) return true;
        }
        return false;
    }
    /// Every seat has the same level (none included)
    constexpr bool uniform() const noexcept { return level[0] == level[1] && level[1] == level[2] && level[2] == level[3]; }
    friend bool operator==(const FillPlan& a, const FillPlan& b) noexcept { return a.level == b.level; }
    friend bool operator!=(const FillPlan& a, const FillPlan& b) noexcept { return !(a == b); }
};
/// "none", "easy", "medium", "hard" (one word: every seat has that level) or four of them joined by commas, for the seats 0 - 3 ("none,none,easy,hard"), in any case; false (`out` unchanged, `why` says
/// what is wrong) for anything else
bool parse_fill_plan(std::string_view text, FillPlan& out, std::string& why);
/// One word when every seat has the same level, else four words: what parse_fill_plan reads back
std::string fill_plan_text(const FillPlan& plan);
/// The bots that a START seats in `room`: for the seats 0 - 3 in order, an empty seat whose level is not none gets it, while fewer than `players` seats are taken (the cap is the room's player count;
/// a seat that a person took is skipped and its level ignored). The one rule of the server's Room and of a LAN host's START, and what the leader's screens show.
std::vector<std::pair<uint8_t, FillLevel>> plan_fill_seats(const FillPlan& plan, const RoomMsg& room, uint8_t players);
/// What a person is told about those bots: "Red gets an Easy bot, Black a Hard bot" (the colour words of the seats; "an Easy", "a Medium", "a Hard")
std::string fill_seats_sentence(const std::vector<std::pair<uint8_t, FillLevel>>& seats);
/// ... and in the short form of a narrow place: "Red Easy, Black Hard"
std::string fill_seats_short(const std::vector<std::pair<uint8_t, FillLevel>>& seats);
/// "No team" in the team bytes of StartRequest and Start (protocol 13): both bytes are kNoTeam for free for all, else they are two different seats, 0 - 3
inline constexpr uint8_t kNoTeam = 255;
/// The leader's request to start the match now with the players who are in the room (protocol 7, client -> server), to seat bots in the seats that are still empty (up to the room's player count)
/// since protocol 11, with a level for each seat since protocol 13 (`fill[s]`: the bot of seat s, None: leave it empty), and to start with the teams `team_a` + `team_b` (kNoTeam both: free for all).
/// Seven bytes on the wire: the type, the four levels (0 .. 3), the two team bytes; nothing else is a StartRequest. The decoder refuses a level above 3, a team byte that is not kNoTeam for both or
/// two different seats 0 - 3, a missing or an extra byte.
struct StartRequestMsg {
    std::array<FillLevel, sim::MAX_PLAYERS> fill{};
    uint8_t team_a{kNoTeam};
    uint8_t team_b{kNoTeam};
    /// Every seat gets `level` (the single choice of protocol 11)
    static StartRequestMsg all(FillLevel level) {
        StartRequestMsg m;
        m.fill.fill(level);
        return m;
    }
    sim::StartTeams teams() const noexcept { return team_a == kNoTeam && team_b == kNoTeam ? sim::StartTeams{} : sim::StartTeams{true, team_a, team_b}; }
    void set_teams(const sim::StartTeams& t) noexcept {
        team_a = t.set ? t.a : kNoTeam;
        team_b = t.set ? t.b : kNoTeam;
    }
};
/// Where a guest accepts connections from the other guests (the host fills it from the address it saw and the port the guest announced)
struct Endpoint {
    std::string address;
    uint16_t port{0};
    bool operator==(const Endpoint& o) const noexcept { return address == o.address && port == o.port; }
};
struct StartMsg {
    uint32_t seed{1};
    std::string map_name;
    uint64_t map_hash{0};         // FNV-1a 64 of the map file: a client whose file differs cannot play
    bool fog{false};
    uint8_t roster{0};            // bit p: seat p takes part
    std::array<std::string, sim::MAX_PLAYERS> names;
    std::array<Endpoint, sim::MAX_PLAYERS> endpoints;   // guests: how the other guests reach this seat (host migration); the host's seat is empty
    uint8_t team_a{kNoTeam};      // protocol 13: the teams the match starts with: both kNoTeam (free for all) or the pair that sim::plan_start_teams accepts for `roster` (the decoder refuses anything else);
    uint8_t team_b{kNoTeam};      // every engine calls sim::apply_start_teams(teams()) right after init() and before its first tick
    sim::StartTeams teams() const noexcept { return team_a == kNoTeam && team_b == kNoTeam ? sim::StartTeams{} : sim::StartTeams{true, team_a, team_b}; }
    void set_teams(const sim::StartTeams& t) noexcept {
        team_a = t.set ? t.a : kNoTeam;
        team_b = t.set ? t.b : kNoTeam;
    }
};
struct LoadedMsg {
    bool ok{true};
};
struct CancelMsg {
    enum class Reason : uint8_t { PlayerLeft = 1, LoadFailed = 2, HostCancelled = 3 };
    Reason reason{Reason::HostCancelled};
    uint8_t player{255};          // who caused it (PlayerLeft, LoadFailed)
};

// Host migration (docs/NETWORK_PORT.md): when the host is gone the survivors elect the lowest living seat, which resumes sealing turns where the
// history stops. All of these travel on the links between guests.
struct ProposeMsg {
    uint8_t epoch{0};             // the election (1 for the first host change, 2 for the second, ...)
    uint8_t candidate{255};       // the seat that offers to become the host
};
struct AcceptMsg {
    uint8_t epoch{0};
    uint32_t next_receive{0};     // the first turn the sender has not received yet
    uint32_t next_execute{0};     // the first turn it has not executed yet
};
struct RefuseMsg {
    uint8_t epoch{0};
    uint8_t lowest{255};          // the lowest seat the sender sees alive, or its host when the host is alive
};
struct ResumeMsg {
    uint8_t epoch{0};
    uint8_t host{255};            // the new host
    uint32_t resume_turn{0};      // it seals turn `resume_turn` next; the turns before it follow as ordinary Turn messages
};
struct RequestMsg {
    uint32_t from_turn{0};
};
struct PeerHelloMsg {
    uint8_t seat{255};
};
/// A dedicated server never stops sealing for a player that falls behind (the others' game goes on): it tells the room who it is, once a second while it lasts, and when
/// the player is back within a second (`behind_ms` 0). The notice goes to everybody, the player itself included: a player whose own link is the slow one has its backlog on
/// the way, not in its queue, and would otherwise be the only one in the room that does not know it is behind (its game says "You are lagging (N s behind)").
struct LagMsg {
    uint8_t seat{255};        // the player that lags
    uint32_t behind_ms{0};    // how far behind the match it is, in ms of turns it has not executed yet; 0: it is not lagging any more
};

// ---- protocol 10: presence, votes and the catch-up stream (docs/NETWORK_PORT.md "Reconnect") -----------------------------------------------------------------

/// Who is missing from the match, sent by a dedicated server to the players that are there (one per recipient: `your_vote` is the recipient's own), on every change and once a
/// second while the match is held (a pause, and the countdown that follows it). A seat is MISSING when its connection was lost (the link closed, a send failed, or nothing at all
/// arrived for 10 s: a player who lags but keeps talking is never missing, only announced with Lag) or when it came back and is being given the match. Nothing is sealed while a
/// seat is missing: the match is paused for everybody, chat goes on. `missing` empty means that nobody is missing: the match runs, or (`resume_s` above 0) it is held for the countdown
/// that follows a pause.
///
/// Wire layout: u8 n (0 .. 4), n x { u8 seat, u8 state (1 absent, 2 catching up), u16 waited_s, u8 progress }, u8 vote_seat (255 none), u8 votes_continue, u8 voters, u8 your_vote
/// (0 none, 1 keep waiting, 2 continue), u16 cap_s, u8 resume_s (0 none, 1 .. 60: the seconds of the resume countdown that are left, rounded up). The decoder refuses anything that an
/// honest server would not say: a seat listed twice, a seat above 3, a state other than 1 and 2, a progress above 100 or one that is not 0 for an absent seat, entries that are not
/// sorted longest away first, a vote about a seat above 3, votes that are more than the voters or more than 4 voters, a vote count or a choice of the receiver without a vote, a
/// countdown above 60 seconds or one that runs while a seat is missing, a trailing byte, a missing byte.
struct PresenceMsg {
    enum class State : uint8_t { Absent = 1, CatchingUp = 2 };
    struct Entry {
        uint8_t seat{255};
        State state{State::Absent};
        uint16_t waited_s{0};          // how long the seat has been away in this match, all its absences added up, in whole seconds (saturates at 0xFFFF); not growing along the list
        uint8_t progress{0};           // CatchingUp: the percent of the match's turns that it has executed (0 .. 100); 0 for an absent seat
    };
    std::vector<Entry> missing;        // longest away first (the seat of a tie below)
    uint8_t vote_seat{255};            // the seat the open vote is about (255: no vote is open): an absent seat that has been away long enough to be put to the vote (the one that has been away
                                       // longest), or a seat that FLAPS (lost three times in a minute) in any state, missing or back
    uint8_t votes_continue{0};         // the connected players who chose "continue without it" (0 without a vote)
    uint8_t voters{0};                 // the connected players that may vote about it (not the seat itself): the vote is won by MORE THAN HALF of them (2 * votes_continue > voters)
    uint8_t your_vote{0};              // the receiver's own choice: 0 none, 1 keep waiting, 2 continue (0 without a vote)
    uint16_t cap_s{0xFFFF};            // seconds of pause that the match has left before the cap drops every seat that is not present (the cap is the match's total paused time); 0xFFFF: that or more
    uint8_t resume_s{0};               // the match goes on after this many seconds (the countdown that follows a pause of a few seconds or more); 0: no countdown (nobody is missing then, or the match runs)
};
inline constexpr uint8_t kMaxResumeSeconds = 60;
inline constexpr uint16_t kCapSecondsMore = 0xFFFF;
/// A connected player's choice about the seat that the vote is about. 3 bytes on the wire: type, seat (0 .. 3), choice (0 keep waiting, 1 continue without it). A vote that does not
/// fit (no vote is open, the seat is not its subject, the voter is not connected) is ignored by the server: it crossed a state change on the wire, it is no offence.
struct VoteMsg {
    uint8_t seat{255};                 // the missing seat that the vote is about
    bool continue_without{false};      // true: continue the match without it; false: keep waiting for it
};
/// The server announces the stream of turns that a player who came back is given: the turns first_turn .. total_turns - 1 follow in TurnBatch messages (nothing follows when the two
/// are equal: the player has all of them), and the player answers with CaughtUp when it has executed them. 9 bytes on the wire; first_turn is never above total_turns.
struct CatchUpMsg {
    uint32_t first_turn{0};            // the first turn of the stream: what the client already has (Hello::have_turns) is not sent again
    uint32_t total_turns{0};           // the turns sealed so far: the stream ends with turn total_turns - 1
};
/// A run of consecutive sealed turns of the match, in canonical order with the issuers stamped, as they were sealed. On the wire: u8 type, u32 first_turn, u16 count (1 ..
/// kMaxBatchTurns), then count x { u16 commands (at most kMaxTurnCommands), the commands in their wire form }; the whole message is at most kMaxMessageBytes and the host fills
/// one up to kBatchBytes (a single turn that is bigger than that is a batch of its own). The `turn` of the i-th TurnMsg is not sent: it is first_turn + i. The last turn of the
/// batch is numbered at most 0xFFFFFFFF.
struct TurnBatchMsg {
    uint32_t first_turn{0};
    std::vector<TurnMsg> turns;        // consecutive from first_turn
};
/// A player who came back has executed every turn that CatchUp announced (`turns` of them: it must equal total_turns) and its state is this: the server compares the hash with its
/// own (it is the referee) and gives the seat back, or answers that client alone with Desync and keeps the seat away. 69 bytes on the wire.
struct CaughtUpMsg {
    uint32_t turns{0};
    sim::StateHash hash;
};
inline constexpr size_t kMaxBatchTurns = 4096;                  // turns in one TurnBatch (the u16 count has room for 65535; a batch of empty turns is 2 bytes a turn)
inline constexpr size_t kBatchBytes = 48 * 1024;                // the host stops filling a batch at this size (after the first turn): a message is at most kMaxMessageBytes (64 KB)
inline constexpr size_t kBatchHeaderBytes = 1 + 4 + 2;          // type, first_turn, count

/// The type byte of a message, MsgType::None when the message is empty or the type is unknown.
MsgType peek_type(const uint8_t* data, size_t size) noexcept;
inline MsgType peek_type(const std::vector<uint8_t>& m) noexcept { return peek_type(m.data(), m.size()); }

// Encoders return the whole message. Decoders return false for anything malformed (wrong type byte, short, long, out of range counts).
std::vector<uint8_t> encode(const HelloMsg&);
std::vector<uint8_t> encode(const WelcomeMsg&);
std::vector<uint8_t> encode(const RejectMsg&);
std::vector<uint8_t> encode(const CommandMsg&);
std::vector<uint8_t> encode(const TurnMsg&);
std::vector<uint8_t> encode(const AckMsg&);
std::vector<uint8_t> encode(const HashMsg&);
std::vector<uint8_t> encode(const DesyncMsg&);
std::vector<uint8_t> encode(const ChatMsg&);
std::vector<uint8_t> encode(const RoomMsg&);
std::vector<uint8_t> encode(const StartMsg&);
std::vector<uint8_t> encode(const LoadedMsg&);
std::vector<uint8_t> encode(const CancelMsg&);
std::vector<uint8_t> encode(const ProposeMsg&);
std::vector<uint8_t> encode(const AcceptMsg&);
std::vector<uint8_t> encode(const RefuseMsg&);
std::vector<uint8_t> encode(const ResumeMsg&);
std::vector<uint8_t> encode(const RequestMsg&);
std::vector<uint8_t> encode(const PeerHelloMsg&);
std::vector<uint8_t> encode(const StartRequestMsg&);
std::vector<uint8_t> encode(const LagMsg&);
std::vector<uint8_t> encode(const PresenceMsg&);
std::vector<uint8_t> encode(const VoteMsg&);
std::vector<uint8_t> encode(const CatchUpMsg&);
/// At most kMaxBatchTurns turns are encoded (the rest of `turns` is cut, as encode(TurnMsg) cuts the commands above kMaxTurnCommands); the message is only a message that decode()
/// accepts when it is at most kMaxMessageBytes and the turns are not empty
std::vector<uint8_t> encode(const TurnBatchMsg&);
std::vector<uint8_t> encode(const CaughtUpMsg&);
/// The same TurnBatch message made from turns that are packed already (the host's TurnLog keeps them so, turnlog.hpp): `packed` holds exactly `count` turns, each u16 command count
/// and the commands in their wire form. For 1 .. kMaxBatchTurns turns; an empty vector (no message) for any other count.
std::vector<uint8_t> encode_turn_batch_packed(uint32_t first_turn, uint32_t count, const uint8_t* packed, size_t size);
std::vector<uint8_t> encode_begin();
std::vector<uint8_t> encode_leave();
std::vector<uint8_t> encode_ping(const PingMsg&);
std::vector<uint8_t> encode_pong(const PingMsg&);

bool decode(const uint8_t* data, size_t size, HelloMsg& out);
/// Only the version and the name of a Hello (they lead every version's layout): a host answers "version mismatch" to a guest of another version instead of
/// "bad request", although the rest of that guest's Hello has another layout. Fills `version` and `name` only.
bool decode_hello_prefix(const uint8_t* data, size_t size, HelloMsg& out);
bool decode(const uint8_t* data, size_t size, WelcomeMsg& out);
bool decode(const uint8_t* data, size_t size, RejectMsg& out);
bool decode(const uint8_t* data, size_t size, CommandMsg& out);
bool decode(const uint8_t* data, size_t size, TurnMsg& out);
bool decode(const uint8_t* data, size_t size, AckMsg& out);
bool decode(const uint8_t* data, size_t size, HashMsg& out);
bool decode(const uint8_t* data, size_t size, DesyncMsg& out);
bool decode(const uint8_t* data, size_t size, ChatMsg& out);
bool decode(const uint8_t* data, size_t size, RoomMsg& out);
bool decode(const uint8_t* data, size_t size, StartMsg& out);
bool decode(const uint8_t* data, size_t size, LoadedMsg& out);
bool decode(const uint8_t* data, size_t size, CancelMsg& out);
bool decode(const uint8_t* data, size_t size, ProposeMsg& out);
bool decode(const uint8_t* data, size_t size, AcceptMsg& out);
bool decode(const uint8_t* data, size_t size, RefuseMsg& out);
bool decode(const uint8_t* data, size_t size, ResumeMsg& out);
bool decode(const uint8_t* data, size_t size, RequestMsg& out);
bool decode(const uint8_t* data, size_t size, PeerHelloMsg& out);
/// Exactly the type byte, four levels (0 .. 3) and two team bytes (kNoTeam both, or two different seats 0 - 3): anything else is no StartRequest
bool decode(const uint8_t* data, size_t size, StartRequestMsg& out);
bool decode(const uint8_t* data, size_t size, LagMsg& out);
bool decode(const uint8_t* data, size_t size, PresenceMsg& out);
bool decode(const uint8_t* data, size_t size, VoteMsg& out);
bool decode(const uint8_t* data, size_t size, CatchUpMsg& out);
bool decode(const uint8_t* data, size_t size, TurnBatchMsg& out);
bool decode(const uint8_t* data, size_t size, CaughtUpMsg& out);
/// Ping and Pong share the payload; the type byte tells them apart (peek_type).
bool decode_ping(const uint8_t* data, size_t size, PingMsg& out);

template <typename Msg>
bool decode(const std::vector<uint8_t>& m, Msg& out) {
    return decode(m.data(), m.size(), out);
}

}  // namespace ants::net
