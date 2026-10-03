#pragma once

// WebSocket (RFC 6455) SERVER transport for the game server (native builds only): browsers and Electron cannot open a raw TCP socket, so they reach the
// same server through a WebSocket that a reverse proxy terminates TLS for. A WsConnection is one upgraded socket behind the `Connection` interface of
// transport.hpp: every received binary message is one protocol message, every send() is one binary frame, so the lobby and the lock-step code cannot tell
// it from a TcpConnection. Nothing blocks and nothing runs in a thread: the game polls the listener and the connections from its main loop.
//
// What this file holds, from the bottom up (all of it but the last two classes is pure and socket free, so that the tests can feed it bytes):
//   - SHA-1 and base64 (the handshake needs both; small own implementations, no dependency), and the accept key of RFC 6455 section 1.3;
//   - the frame codec: ws_encode_frame, and WsFrameParser, which is fed byte chunks of any size and produces messages, ping / pong / close events or a
//     protocol error carrying the close status to answer with. Every length is checked BEFORE anything is allocated for it, so a hostile peer cannot
//     make the receiver allocate or buffer more than one message (kMaxMessageBytes);
//   - the HTTP upgrade handshake: ws_parse_handshake turns the request bytes into "need more", the 101 answer, or the proper HTTP error;
//   - WsConnection (the upgraded socket) and WsListener (accepts sockets, advances their handshakes, hands out the finished connections).
//
// Rules of the wire (what is accepted and refused):
//   handshake  GET, HTTP/1.1, a Host header, `Upgrade` containing the token websocket, `Connection` containing the token Upgrade, Sec-WebSocket-Version 13,
//              a Sec-WebSocket-Key of 16 bytes of base64, no request body. An optional Origin allow-list (empty = any origin; a request that carries an
//              Origin that is not listed gets 403; a request without one is let through, because only browsers send it and the list defends against
//              pages of other sites) and an optional required path (empty = any path, so that a proxy may use any location). The subprotocol "ants" is
//              echoed when the client offers it. A request over 8 KB, a malformed one, or one that is not complete after 5 s is answered with its HTTP
//              error where possible (431, 400, 408) and dropped.
//   frames     the client must mask (an unmasked frame is close 1002); binary (0x2) and continuation (0x0) frames, fragmented messages are reassembled
//              up to kMaxMessageBytes in total (more is 1009); text (0x1) is refused (1003); ping / pong / close carry at most 125 bytes and are never
//              fragmented, but may sit between the fragments of a message; reserved bits and reserved opcodes are 1002 (no extension is negotiated);
//              lengths are minimal and a 64-bit length with the top bit set is an error (1002). A close frame carries nothing or a valid status (1000 -
//              1014 but 1004 - 1006, or 3000 - 4999) and an UTF-8 reason (1007).
//   close      the peer's close frame is answered with a close frame that echoes its status, then the socket is closed (state Closed); a protocol error is
//              answered with a close frame that carries the status, then the socket is closed (state Failed). A ping is answered with a pong at once.
//   keep-alive this class never closes an idle connection (the game has its own silence timeout), but when nothing has been sent for
//              WsServerOptions::ping_interval_ms (20 s) it sends a ping, so that a proxy does not drop a connection that is merely quiet.
//   write path non-blocking with an output buffer, like TcpConnection: never blocks, never SIGPIPE; when more than kWsMaxBacklogBytes (1 MB) are waiting
//              the peer is stuck and the connection fails.
//
// The proxy side (TLS ends there; this code speaks plain ws on the loopback address, so listen with loopback_only = true). A minimal nginx example,
// with generic names only:
//
//     map $http_upgrade $connection_upgrade { default upgrade; '' close; }
//     server {
//         listen 443 ssl;
//         server_name play.example.org;
//         # ssl_certificate / ssl_certificate_key as usual
//         location /ws {
//             proxy_pass http://127.0.0.1:4002;            # the port of WsListener::listen
//             proxy_http_version 1.1;                      # the upgrade needs HTTP/1.1 to the backend
//             proxy_set_header Upgrade $http_upgrade;      # pass the upgrade request through ...
//             proxy_set_header Connection $connection_upgrade;
//             proxy_set_header Host $host;
//             proxy_read_timeout 3600s;                    # ... and do not drop a quiet game (the default is 60 s)
//             proxy_send_timeout 3600s;
//         }
//     }
//
// Every client then appears to come from 127.0.0.1; the proxy passes the Origin header through unchanged, so WsServerOptions::allowed_origins
// ({"https://play.example.org"}) works behind it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"

namespace ants::net {

inline constexpr size_t kWsMaxRequestBytes = 8 * 1024;          // the HTTP request of the handshake, through the blank line
inline constexpr size_t kWsMaxControlBytes = 125;               // a ping / pong / close frame carries at most this much
inline constexpr size_t kWsMaxBacklogBytes = 1024 * 1024;       // more than this waiting to be written: the peer is stuck, the connection fails
inline constexpr uint32_t kWsHandshakeTimeoutMs = 5000;         // a handshake that takes longer is dropped
inline constexpr uint32_t kWsPingIntervalMs = 20000;            // a connection that sent nothing for this long gets a ping
inline constexpr size_t kWsMaxStatusBytes = 512;                // the body of the status answer (WsServerOptions::status_body)

inline constexpr uint16_t kWsCloseNormal = 1000;
inline constexpr uint16_t kWsCloseGoingAway = 1001;
inline constexpr uint16_t kWsCloseProtocolError = 1002;
inline constexpr uint16_t kWsCloseUnsupportedData = 1003;
inline constexpr uint16_t kWsCloseNoStatus = 1005;              // never on the wire: "the close frame had no status"
inline constexpr uint16_t kWsCloseInvalidPayload = 1007;
inline constexpr uint16_t kWsCloseMessageTooBig = 1009;

// ------------------------------------------------------------------------------------------------
// Primitives

/// SHA-1 of `size` bytes (RFC 3174); only the handshake uses it
std::array<uint8_t, 20> ws_sha1(const uint8_t* data, size_t size);
/// Base64 with padding (RFC 4648)
std::string ws_base64_encode(const uint8_t* data, size_t size);
/// Strict base64: the alphabet and the padding of the encoder only, no whitespace, unused bits zero. False for anything else.
bool ws_base64_decode(const std::string& text, std::vector<uint8_t>& out);
/// The Sec-WebSocket-Accept value for a Sec-WebSocket-Key (RFC 6455 section 1.3): base64(SHA-1(key + the protocol's GUID))
std::string ws_accept_key(const std::string& client_key);

// ------------------------------------------------------------------------------------------------
// Frame codec

enum class WsOpcode : uint8_t { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

/// One frame. A server sends unmasked frames (`mask` = nullptr); a client masks with a random key (the tests are the only client in this code base).
std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const uint8_t* payload, size_t size, bool fin = true, const std::array<uint8_t, 4>* mask = nullptr);
std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const std::vector<uint8_t>& payload, bool fin = true, const std::array<uint8_t, 4>* mask = nullptr);
/// The payload of a close frame: the status (big endian) and an optional UTF-8 reason
std::vector<uint8_t> ws_close_payload(uint16_t status, const std::string& reason = std::string());
/// Whether a status may travel in a close frame
bool ws_valid_close_status(uint16_t status);
/// Strict UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF)
bool ws_valid_utf8(const uint8_t* data, size_t size);

/// Incremental parser of the frames of one direction. Feed it whatever bytes arrive (one at a time or megabytes) and ask for events until it says
/// NeedMore. Memory is bounded: a frame header is judged as soon as it is complete, before its payload is waited for, and a data frame that does not fit
/// into `max_message` together with the fragments before it is an error before one byte of it is buffered. After an Error or a Close the parser is
/// finished: it ignores further input.
class WsFrameParser {
public:
    enum class Result : uint8_t {
        NeedMore,   // no whole event yet
        Message,    // payload = one whole binary message (fragments reassembled)
        Ping,       // payload = the application data (answer with a pong carrying the same)
        Pong,       // payload = the application data
        Close,      // status = the peer's status (kWsCloseNoStatus when it sent none), payload = its reason
        Error       // status = the close status to answer with (1002, 1003, 1007 or 1009); the same on every further call
    };

    /// `from_client`: the bytes come from a client, so every frame must be masked (a server's frames must not be)
    explicit WsFrameParser(bool from_client = true, size_t max_message = kMaxMessageBytes);

    void feed(const uint8_t* data, size_t size);
    Result next(std::vector<uint8_t>& payload, uint16_t& status);
    /// Bytes fed and not yet consumed by an event (diagnostics, and the tests' proof that every event consumes something)
    size_t buffered() const noexcept { return buf_.size() - pos_; }
    bool finished() const noexcept { return done_; }

private:
    Result fail(uint16_t status_code, uint16_t& status);
    void compact();

    bool from_client_;
    size_t max_message_;
    std::vector<uint8_t> buf_;                  // bytes fed, from pos_ on not yet consumed (at most one frame plus the last chunk)
    size_t pos_{0};
    std::vector<uint8_t> message_;              // the fragments of the message being reassembled
    bool fragmenting_{false};                   // a message has started and its last fragment has not come
    bool done_{false};
    bool error_{false};
    uint16_t error_status_{0};
};

// ------------------------------------------------------------------------------------------------
// Handshake

struct WsServerOptions {
    std::vector<std::string> allowed_origins;                   // empty = any origin; compared without regard to case
    std::string path;                                           // empty = any path; else the request path (query ignored) must be exactly this
    uint32_t handshake_timeout_ms{kWsHandshakeTimeoutMs};
    uint32_t ping_interval_ms{kWsPingIntervalMs};               // 0 = never ping
    size_t max_pending{64};                                     // handshakes in progress at once; further sockets are closed at once
    // A public, read-only status (the game server's /busy: how many matches run, so that a deploy can wait for a quiet moment): a plain GET of exactly `status_path` (no query, no Upgrade
    // header, nothing but GET: every other method is 405) is answered 200 with the text that `status_body()` returns (JSON, at most kWsMaxStatusBytes; Cache-Control: no-store), and the
    // connection closes. A reverse proxy routes the path like /ws. Empty path or no function: none.
    std::string status_path;
    std::function<std::string()> status_body;
};

struct WsHandshakeResult {
    enum class Status : uint8_t { NeedMore, Accepted, Rejected, Answered };
    Status status{Status::NeedMore};
    int http_status{0};                 // 101 or the error's status (400, 403, 404, 405, 426, 431)
    std::string response;               // Accepted: the 101 answer; Rejected: the whole HTTP error response; Answered: the whole 200 answer of the status path (both close the connection)
    size_t consumed{0};                 // the request's length through the blank line; what follows belongs to the WebSocket frames
    bool subprotocol{false};            // "ants" was offered and echoed
};

/// Judges the bytes received so far of a handshake request. Pure: the same bytes give the same answer. Stricter than HTTP needs to be (CRLF line ends
/// only, no obsolete line folding, no control characters), because the request comes from the open network.
WsHandshakeResult ws_parse_handshake(const std::string& request, const WsServerOptions& options);

// ------------------------------------------------------------------------------------------------
// Sockets

class WsConnection final : public Connection {
public:
    ~WsConnection() override;
    WsConnection(const WsConnection&) = delete;
    WsConnection& operator=(const WsConnection&) = delete;

    /// One binary frame. False when the connection is not open or the message is over kMaxMessageBytes (the connection stays as it is); a stuck peer
    /// (more than kWsMaxBacklogBytes waiting) fails the connection.
    bool send(const std::vector<uint8_t>& message) override;
    bool poll(std::vector<uint8_t>& message) override;
    /// Open from the moment the handshake is done; Closed after a close frame or an orderly close of the socket (messages that arrived before it can
    /// still be polled); Failed after a protocol error or a socket error
    State state() const override { return state_; }
    /// Says goodbye with a close frame (best effort, nothing is waited for) and closes the socket
    void close() override;

    /// The peer's address as text (diagnostics; behind a proxy it is the proxy's)
    const std::string& peer() const noexcept { return peer_; }
    /// Bytes waiting to be written (diagnostics)
    size_t backlog() const noexcept { return out_.size() - out_pos_; }

private:
    friend class WsListener;
    WsConnection(int fd, std::string peer, uint32_t ping_interval_ms, const std::string& answer, const std::string& early_bytes);
    void pump();
    bool drain_parser();
    bool enqueue(WsOpcode opcode, const uint8_t* data, size_t size);
    bool flush();
    void finish(State end_state);
    void fail();
    void release_socket();

    int fd_{-1};
    State state_{State::Open};
    std::string peer_;
    uint32_t ping_interval_ms_{0};
    uint64_t last_send_ms_{0};
    WsFrameParser parser_;
    std::vector<uint8_t> out_;                      // frames not yet written, from out_pos_ on
    size_t out_pos_{0};
    std::deque<std::vector<uint8_t>> messages_;     // received, not yet polled
    size_t inbox_bytes_{0};
};

class WsListener final {
public:
    /// Listens on `port` (0 = any free port; see port()). `loopback_only` accepts only connections from this machine (what a reverse proxy on the same
    /// machine needs, and the default). nullptr on failure.
    static std::unique_ptr<WsListener> listen(uint16_t port, bool loopback_only = true, const WsServerOptions& options = WsServerOptions());
    ~WsListener();
    WsListener(const WsListener&) = delete;
    WsListener& operator=(const WsListener&) = delete;

    /// Non-blocking. Takes in the sockets that are waiting, advances the HTTP upgrade of every socket that is still pending (a handshake that is
    /// refused, malformed, too big or too slow is answered with its HTTP error where possible and dropped) and returns a connection once its handshake
    /// is complete; call it until it returns nullptr.
    std::unique_ptr<WsConnection> accept();
    uint16_t port() const noexcept { return port_; }
    /// Sets the status path and its text after the listener exists (WsServerOptions::status_path and status_body)
    void set_status(std::string path, std::function<std::string()> body) {
        options_.status_path = std::move(path);
        options_.status_body = std::move(body);
    }
    /// Sockets whose handshake has not finished (diagnostics)
    size_t pending() const noexcept { return pending_.size(); }

private:
    struct Pending {
        int fd;
        std::string peer;
        std::string request;            // the bytes received so far
        uint64_t started_ms;
    };
    struct Closing {                    // an answered, refused socket that is read empty for a moment so that the close does not reset the answer
        int fd;
        uint64_t started_ms;
    };
    WsListener(int fd, uint16_t port, const WsServerOptions& options) : fd_(fd), port_(port), options_(options) {}
    void take_in(uint64_t now);
    bool advance(Pending& p, uint64_t now);
    void refuse(Pending& p, const std::string& response, uint64_t now);
    void drain_closing(uint64_t now);

    int fd_;
    uint16_t port_;
    WsServerOptions options_;
    std::vector<Pending> pending_;
    std::vector<Closing> closing_;
    std::deque<std::unique_ptr<WsConnection>> ready_;
};

}  // namespace ants::net
