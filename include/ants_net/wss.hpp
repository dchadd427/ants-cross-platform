#pragma once

// The desktop game's encrypted way to a game server: a WebSocket (RFC 6455) CLIENT over TLS (native builds only), to wss://HOST[:PORT]/PATH, the door that the site's proxy puts in front of the
// game server (docs/NETWORK_PORT.md): the browser build reaches the same door with the page's own WebSocket (wasm_ws.hpp). A WssConnection is one such link behind the `Connection` interface of
// transport.hpp: every send() is one masked binary frame, every received binary message is one protocol message, so the lobby, the session and the lock-step code cannot tell it from a TcpConnection.
// Nothing blocks and nothing runs in a thread: the game polls it from its main loop.
//
// What it does. TCP connects (to the address that the caller looked up once, so the name is never looked up again by a link of the way back), TLS verifies the server's certificate for the host
// of the URL (tls.hpp: no way to skip it), the HTTP upgrade runs (the request and the answer are the pure functions below), then frames flow. A link that is not open 20 s after it was started
// fails; when the only fault of the server's chain is a root that the system does not trust yet, the system is asked once to learn it (Windows installs the roots of its program on first use, TlsClient: prime_system_roots, on a thread
// of its own so that the window stays alive), its roots are read again and the link is made once more: the chain is still judged by Mbed TLS alone. The codec is the one of ws.hpp (the server's side of the same wire): a client masks every frame with a fresh random key and refuses masked frames, text frames and every other
// protocol error of the server's side as the server does of a client's; a ping is answered with a pong, a close frame is answered with a close frame and ends the link (Closed), a protocol
// error is answered with its close status and ends it (Failed).
//
// Only wss:// is spoken: the game never sends a seat key or a chat line over an unencrypted WebSocket. (LAN games and local servers use TcpConnection.)

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/address.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tls.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/ws.hpp"

namespace ants::net {

inline constexpr uint32_t kWssConnectTimeoutMs = 20000;         // a link that is not open this long after it was started fails
inline constexpr size_t kWssMaxReplyBytes = 8 * 1024;           // the server's answer to the upgrade, through the blank line
/// The upgrade request for `url` with the key `key_b64` (base64 of 16 random bytes): GET, HTTP/1.1, Host, Upgrade, Connection, Sec-WebSocket-Key and -Version. No Origin (only browsers send one), no
/// subprotocol and no extension are asked for.
std::string wss_build_request(const WssUrl& url, const std::string& key_b64);

struct WssReply {
    enum class Status : uint8_t { NeedMore, Accepted, Refused };
    Status status{Status::NeedMore};
    size_t consumed{0};                     // Accepted: the length of the answer through the blank line; what follows is frames
    int http_status{0};                     // the status line's number (101 when accepted)
    std::string why;                        // Refused: a sentence for a player
};
/// Judges the bytes received so far of the answer to wss_build_request(url, key_b64): accepted only for "101", the upgrade to websocket, and the accept value of `key_b64` (ws_accept_key), with no extension and
/// no subprotocol (none was asked for); an answer over kWssMaxReplyBytes without its blank line, a malformed one or another status is refused. Pure.
WssReply wss_parse_reply(const std::string& bytes, const std::string& key_b64);

class WssConnection final : public Connection {
public:
    /// Starts a secure link to `url`. `address` is the numeric address to connect to (the caller looked the name up once); empty: connect by the URL's host name. nullptr with `why` (a sentence for a
    /// player) when no socket can be made or the TLS client cannot be set up (no trusted root, ...). The state is Connecting until the upgrade is accepted, then Open, or Failed.
    /// A link that is not open `timeout_ms` after it was started fails (the tests shorten it).
    static std::unique_ptr<WssConnection> connect(const WssUrl& url, const std::string& address, const TlsTrust& trust, std::string& why, uint32_t timeout_ms = kWssConnectTimeoutMs);

    ~WssConnection() override;
    WssConnection(const WssConnection&) = delete;
    WssConnection& operator=(const WssConnection&) = delete;

    bool send(const std::vector<uint8_t>& message) override;
    bool poll(std::vector<uint8_t>& message) override;
    State state() const override { return state_; }
    /// Says goodbye (a close frame, then the TLS goodbye: best effort, nothing is waited for) and closes
    void close() override;
    /// Why a Failed link failed, in words for a player (empty for a link that did not)
    std::string failure_text() const override { return failure_; }
    /// The kind of TLS failure (None when the TLS link did not fail: the upgrade was refused, a frame broke the rules, the time ran out) and the HTTP status of a refused upgrade (0 when there was none)
    TlsFailure tls_failure() const noexcept { return tls_ ? tls_->failure() : TlsFailure::None; }
    int refused_status() const noexcept { return refused_status_; }

    /// The negotiated protocol and cipher of the TLS link once it is up (diagnostics and the tests)
    std::string tls_description() const { return tls_ ? tls_->describe() : std::string(); }
    /// Bytes waiting to be written (diagnostics)
    size_t backlog() const noexcept { return tls_ ? tls_->flush() : 0; }

private:
    enum class Stage : uint8_t { Tls, Priming, Upgrade, Frames, Done };
    struct Priming;
    WssConnection(const WssUrl& url, std::unique_ptr<TlsClient> tls, uint32_t timeout_ms, const std::string& address, const TlsTrust& trust);
    void pump();
    bool pump_handshakes();
    bool start_priming();
    bool restart_link();
    bool drain_parser();
    bool enqueue(WsOpcode opcode, const uint8_t* data, size_t size);
    void fail(const std::string& why);
    void finish(State end_state);

    WssUrl url_;
    std::unique_ptr<TlsClient> tls_;
    Stage stage_{Stage::Tls};
    State state_{State::Connecting};
    std::string key_b64_;
    std::string reply_;                             // the server's answer to the upgrade, until it is whole
    std::chrono::steady_clock::time_point started_;
    uint32_t timeout_ms_{kWssConnectTimeoutMs};
    std::string address_;                           // the numeric address that the first link went to (empty: the URL's host name): a link that is made again goes there
    TlsTrust trust_;
    bool primed_{false};                            // the roots were primed once for this connection: a chain that is still not trusted after that fails
    std::shared_ptr<Priming> priming_;
    WsFrameParser parser_;                          // the server's frames: unmasked
    std::deque<std::vector<uint8_t>> messages_;     // received, not yet polled
    size_t inbox_bytes_{0};
    std::string failure_;
    int refused_status_{0};
};

}  // namespace ants::net
