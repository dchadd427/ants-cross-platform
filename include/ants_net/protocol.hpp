#pragma once

// The messages of a lock-step match. A message is one datagram of the transport (WebRTC data channels, WebSocket and the TCP framing all deliver
// whole messages): u8 type, then the payload. Every decoder checks every length and count before it uses it (the original trusted them: an
// unbounded 2 KB stack receive, unchecked type / count / index fields), and rejects trailing bytes.
//
// Star topology: the room owner (host) is the sequencer. Clients send Command, TurnAck, Hash, Ping and Chat to it; it answers with Turn (the
// sealed commands of one 50 ms turn, which is one tick, in canonical order, issuer stamped from the connection), Desync, Pong and Chat, and a dedicated
// server tells the room with Lag when a player is far behind the match.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

// THE RULE of this number: it changes with every change that two peers of a match must share. That is the messages (a layout, a type, a rule of a decoder) AND the simulation's rules:
// any fix or feature that changes the state hash of some play (a rule of the engine, a table or a flag that the engine reads, the way a level is read), even one that shows only on some maps
// or in some rare sequence of orders, because peers that run different rules desynchronise in the first play where they differ, and the door checks nothing else: a Hello of another
// number is refused ("version mismatch") by a LAN host and by a server's room, and a LAN announcement of another number is listed as another version. A release that cannot say "no
// state hash of any play changed" raises it, and the history below says why (nothing in the wire changed in 9: the rules of the engine did).
inline constexpr uint16_t kProtocolVersion = 9;         // 2: the Room message carries each seat's round trip (the thumbs); 3: host migration (mesh, election); 4: the Quit command (Drop moved from 11 to 12); 5: Hello carries the seat that the guest asks for; 6: map names may hold any printable character that cannot leave the maps folder (up to 64), Hello carries a room code and a token, the slot state Bot, the rejection NoSuchRoom; 7: the Room message names the room's leader (a dedicated server's room: the first player who joined), the message StartRequest (the leader asks the server to start now); 8: turns of 50 ms with one tick each (they were 100 ms with two; a client keeps a jitter buffer of 1 to 4 turns, ants_net/jitter.hpp), a state hash every 20 turns (one second, as before), and the Lag message, type 25 (a dedicated server never waits for a player that falls behind: it tells the room instead); 9: the community-map rules (default ant types, power-ups by tile, the attack clip)
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
    StartRequest = 24,   // leader -> server (protocol 7): start the match now with the players who are here; no payload, only the leader of a server's room is heard
    Lag = 25,       // dedicated server -> the other players (protocol 8): a player is more than 3 s behind the match (or is not any more)
    Last = Lag
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

enum class RejectReason : uint8_t { Full = 1, VersionMismatch = 2, MatchRunning = 3, Kicked = 4, BadRequest = 5, NoSuchRoom = 6 };

struct HelloMsg {
    uint16_t version{kProtocolVersion};
    std::string name;
    uint16_t listen_port{0};    // the port on which this guest accepts the other guests' connections during the match (0: none)
    uint8_t want_seat{255};     // the seat (0 .. 3) this guest asks for, 255: any; a seat that is taken (or the host's own) gives the first free one (protocol 5)
    std::string room;           // the room of a server that this guest wants ("" for a LAN / direct host: valid_room_code); a room that does not exist is Rejected NoSuchRoom (protocol 6)
    std::string token;          // the credential that came with the room code (kMaxTokenChars printable characters; "" when none): carried, never interpreted here (protocol 6)
};
struct WelcomeMsg {
    uint8_t player{255};        // the slot (0 .. 3) this client plays
    uint8_t players{0};         // how many slots the room has
};
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
/// The leader's request to start the match now with the players who are in the room (protocol 7, client -> server). It has no payload: exactly one byte on the wire.
struct StartRequestMsg {};
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
/// Exactly the type byte: a StartRequest with a payload is no StartRequest
bool decode(const uint8_t* data, size_t size, StartRequestMsg& out);
bool decode(const uint8_t* data, size_t size, LagMsg& out);
/// Ping and Pong share the payload; the type byte tells them apart (peek_type).
bool decode_ping(const uint8_t* data, size_t size, PingMsg& out);

template <typename Msg>
bool decode(const std::vector<uint8_t>& m, Msg& out) {
    return decode(m.data(), m.size(), out);
}

}  // namespace ants::net
