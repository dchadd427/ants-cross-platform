#pragma once

// The messages of a lock-step match. A message is one datagram of the transport (WebRTC data channels, WebSocket and the TCP framing all deliver
// whole messages): u8 type, then the payload. Every decoder checks every length and count before it uses it (the original trusted them: an
// unbounded 2 KB stack receive, unchecked type / count / index fields), and rejects trailing bytes.
//
// Star topology: the room owner (host) is the sequencer. Clients send Command, TurnAck, Hash, Ping and Chat to it; it answers with Turn (the
// sealed commands of one 100 ms turn, in canonical order, issuer stamped from the connection), Desync, Pong and Chat.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr size_t kMaxMessageBytes = 64 * 1024;
inline constexpr size_t kMaxTurnCommands = 512;
inline constexpr size_t kMaxChatChars = 100;        // the original's chat entry
inline constexpr size_t kMaxNameChars = 32;
inline constexpr uint32_t kTicksPerTurn = 2;        // a turn is 2 ticks = 100 ms
inline constexpr uint32_t kTurnMs = 100;
inline constexpr uint32_t kHashEveryTurns = 10;     // a state hash every 20 ticks

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
    Last = Pong
};

enum class RejectReason : uint8_t { Full = 1, VersionMismatch = 2, MatchRunning = 3, Kicked = 4, BadRequest = 5 };

struct HelloMsg {
    uint16_t version{kProtocolVersion};
    std::string name;
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
std::vector<uint8_t> encode_ping(const PingMsg&);
std::vector<uint8_t> encode_pong(const PingMsg&);

bool decode(const uint8_t* data, size_t size, HelloMsg& out);
bool decode(const uint8_t* data, size_t size, WelcomeMsg& out);
bool decode(const uint8_t* data, size_t size, RejectMsg& out);
bool decode(const uint8_t* data, size_t size, CommandMsg& out);
bool decode(const uint8_t* data, size_t size, TurnMsg& out);
bool decode(const uint8_t* data, size_t size, AckMsg& out);
bool decode(const uint8_t* data, size_t size, HashMsg& out);
bool decode(const uint8_t* data, size_t size, DesyncMsg& out);
bool decode(const uint8_t* data, size_t size, ChatMsg& out);
/// Ping and Pong share the payload; the type byte tells them apart (peek_type).
bool decode_ping(const uint8_t* data, size_t size, PingMsg& out);

template <typename Msg>
bool decode(const std::vector<uint8_t>& m, Msg& out) {
    return decode(m.data(), m.size(), out);
}

}  // namespace ants::net
