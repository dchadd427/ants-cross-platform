#pragma once

// Hints and the signalling of direct channels (protocol 26, docs/P2P_HINTS.md).
//
// The server stays the sealer of the turns, the referee and the recorder. What these messages add is only a way for a machine to learn another player's order EARLIER than its sealed turn: a HINT is an
// order sent ahead of its turn, either by the other player's browser over a data channel (the two browsers are connected through WebRTC; this file has the signalling that the server relays) or, for a computer
// player, by the server itself the moment the bot decides (Hint on the server's own connection). The receiver gives it to Prediction::hint(); the sealed turn is the truth, and a lost or wrong hint costs
// nothing. Nothing here reaches the simulation, the turns, the hashes or the record.
//
// Who may know whose address. Every player's browser connects through the operator's relay (ICE transport policy "relay": only relay candidates are gathered, offered and accepted), so that no player's
// address reaches another player. A player may turn on DIRECT connections (P2pConfig.direct); a pair of players is connected directly only when BOTH turned it on, and only then may a candidate that is not
// a relay candidate travel between them. The rule is kept in three places: the sending page never emits such a candidate to a peer that has not opted in, the receiving page never accepts one, and the server
// does not pass one on (signal_allowed below), so that a page that breaks the rule is thrown out by the server's violation count instead of showing its address.
//
// Messages (types after Visibility; every decoder checks every length, count and range and refuses trailing bytes):
//   Hint       [36][seq u32][tick u32][a Command]               a player's order ahead of its turn: server -> player (a bot's order, the issuer is the bot's seat), player <-> player over a channel (the
//                                                                issuer is stamped by the channel and must equal the byte in the command). Only the kinds that Prediction::predicts() names.
//   P2pConfig  [37][direct u8]                                  player -> server, once the match has begun: "I can open channels" and whether this player allows direct ones
//   IceConfig  [38][expires u32][username][credential][n u8][url]*n   server -> that player: the relay's short-lived credential and its addresses; n = 0: this server has no relay, or none for this player
//                                                                (a match of one person, a player who has had its credentials): the player opens nothing
//   P2pPeers   [39][ready u8][direct u8]                        server -> every player that sent P2pConfig, whenever they change: the seats that sent P2pConfig and are here, and those of them that allow direct
//   Signal     [40][peer u8][kind u8][len u16][text]            player -> server: for seat `peer`; server -> player: from seat `peer`. Offer / Answer (SDP text), Candidate ("index|candidate line"), Bye.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

inline constexpr size_t kMaxSignalChars = 4096;
inline constexpr size_t kMaxIceUrls = 6;
inline constexpr size_t kMaxIceUrlChars = 120;
inline constexpr size_t kMaxIceNameChars = 80;
/// Ticks that a hint's own tick may stand ahead of anything that a screen could show (a decoder refuses more: no honest sender is a minute ahead)
inline constexpr uint32_t kMaxHintTick = 0x7FFFFFFFu;

struct HintMsg {
    uint32_t seq{0};            // the sender's number for this order, 1, 2, 3 ...: a copy of a hint has the same one
    uint32_t tick{0};           // the turn that the sender expects the server to seal it in (Prediction::last_order_hint_tick)
    sim::Command command;       // the order; the issuer is the sender's seat
};
struct P2pConfigMsg {
    bool direct{false};
};
struct IceConfigMsg {
    uint32_t expires{0};                    // unix seconds at which the credential ends
    std::string username;
    std::string credential;
    std::vector<std::string> urls;          // "stun:host:port", "turn:host:port?transport=udp" ...; empty: no relay here
};
struct P2pPeersMsg {
    uint8_t ready{0};                       // a bit for each seat that sent P2pConfig and is here
    uint8_t direct{0};                      // ... and of those, the ones that allow direct connections
};
enum class SignalKind : uint8_t { Offer = 1, Answer = 2, Candidate = 3, Bye = 4 };
struct SignalMsg {
    uint8_t peer{0};                        // to (client -> server) or from (server -> client)
    SignalKind kind{SignalKind::Offer};
    std::string text;
};

std::vector<uint8_t> encode(const HintMsg&);
std::vector<uint8_t> encode(const P2pConfigMsg&);
std::vector<uint8_t> encode(const IceConfigMsg&);
std::vector<uint8_t> encode(const P2pPeersMsg&);
std::vector<uint8_t> encode(const SignalMsg&);
bool decode(const uint8_t* data, size_t size, HintMsg& out);
bool decode(const uint8_t* data, size_t size, P2pConfigMsg& out);
bool decode(const uint8_t* data, size_t size, IceConfigMsg& out);
bool decode(const uint8_t* data, size_t size, P2pPeersMsg& out);
bool decode(const uint8_t* data, size_t size, SignalMsg& out);
inline bool decode(const std::vector<uint8_t>& m, HintMsg& out) { return decode(m.data(), m.size(), out); }
inline bool decode(const std::vector<uint8_t>& m, P2pConfigMsg& out) { return decode(m.data(), m.size(), out); }
inline bool decode(const std::vector<uint8_t>& m, IceConfigMsg& out) { return decode(m.data(), m.size(), out); }
inline bool decode(const std::vector<uint8_t>& m, P2pPeersMsg& out) { return decode(m.data(), m.size(), out); }
inline bool decode(const std::vector<uint8_t>& m, SignalMsg& out) { return decode(m.data(), m.size(), out); }

/// The relay's side of a candidate. `text` is a signalling payload (SDP text, or "index|candidate line"). True when it carries an ICE candidate that is not a relay candidate ("typ host", "typ srflx",
/// "typ prflx", or a candidate line with no type), or an address in a connection line ("c=IN IP4 a.b.c.d") other than the blank 0.0.0.0 / ::: those are what show a player's own address.
bool carries_non_relay_candidate(std::string_view text);
/// May the server pass `text` from a player to another? A relay candidate (and an SDP with no address) always; anything else only when both players allow direct connections.
bool signal_allowed(bool from_direct, bool to_direct, std::string_view text);

/// The credentials that a player may be given in one match: the first ask, and one more for a page that was reloaded and took its seat back. A player's ask after that is answered as a server with no relay answers.
inline constexpr uint32_t kMaxP2pCredentials = 2;

/// What the server tells its players about its relay (set by main(): the secret never leaves the closure `credential`). `urls` empty: no relay (nobody is given a credential, nobody opens a channel).
struct TurnSettings {
    std::vector<std::string> urls;
    /// base64(HMAC-SHA1(secret, username)): the credential that the relay checks (coturn's shared-secret mode)
    std::function<std::string(const std::string& username)> credential;
    uint32_t ttl_s{3u * 3600u};             // the longest a credential lives; a room asks for less (HostSession::Config::relay_ttl_s: its match's longest run and a margin)
    bool usable() const { return !urls.empty() && static_cast<bool>(credential); }
};
/// A username in coturn's shared-secret form: "<expiry unix time>:<label>" (the label is random, never anything that names a person)
std::string turn_username(uint32_t expires, const std::string& label);
/// The username's expiry, or 0 when `username` is not of that form
uint32_t turn_username_expiry(const std::string& username);

}  // namespace ants::net
