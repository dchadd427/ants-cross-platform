#pragma once

// What the game asks of the page's data channels (protocol 26, docs/P2P_HINTS.md). The browser build implements it with RTCPeerConnection (wasm_rtc.cpp and web/p2p.js); every other build has none (the
// NetGame is given none and plays as it always did), and the tests give it a link of their own. The transport carries bytes and nothing else: the messages are p2p.hpp's, the rule for what may be shown
// to whom is kept by the page (it never emits or accepts an address of a player who did not allow direct connections) and again by the server.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/p2p.hpp"

namespace ants::net {

/// How this machine is connected to another player's page, for the small mark in the room: nothing (no channel, or one that is not up), on its way, through the relay, or direct (only between two players who both allow it)
enum class PeerLink : uint8_t { None = 0, Connecting = 1, Relay = 2, Direct = 3 };

class P2pTransport {
public:
    virtual ~P2pTransport() = default;
    /// The server gave this player the relay's credential: `seat` is this player's own seat, `direct` whether it allows direct connections. Begins to open channels as P2pPeers says who is ready.
    virtual void open(uint8_t seat, bool direct, const IceConfigMsg& ice) = 0;
    /// Who is ready for channels and who allows direct ones (the server's P2pPeers)
    virtual void peers(uint8_t ready, uint8_t direct) = 0;
    /// A Signal from another player, relayed by the server
    virtual void signal(uint8_t from, SignalKind kind, const std::string& text) = 0;
    /// A hint message (an encoded HintMsg) for every peer that has a channel; the transport may send it more than once
    virtual void send_hint(const std::vector<uint8_t>& message) = 0;
    /// The next Signal that the page has for a peer (to be sent to the server), if any
    virtual bool poll_signal(uint8_t& to, SignalKind& kind, std::string& text) = 0;
    /// The next hint message that a peer's channel delivered, with the seat of the channel (the page stamps it; the message's own issuer must agree), if any
    virtual bool poll_hint(uint8_t& from, std::vector<uint8_t>& message) = 0;
    virtual PeerLink link(uint8_t seat) const = 0;
    /// Closes every channel (the match is over, or the game leaves)
    virtual void close() = 0;
};

/// The browser build's data channels (wasm_rtc.cpp and web/p2p.js), or null in every other build and in a page that has none
std::unique_ptr<P2pTransport> make_browser_channels();

}  // namespace ants::net
