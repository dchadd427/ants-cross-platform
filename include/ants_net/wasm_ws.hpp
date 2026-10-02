#pragma once

// The browser's side of the game server's WebSocket door (docs/NETWORK_PORT.md): a Connection over the page's own WebSocket (Emscripten's websocket API), one
// binary message = one protocol message, the same bytes as everywhere else. The web build has no threads and no blocking calls: the browser delivers the events
// between two frames of the main loop, and the game polls the connection from there.
//
// Only the WebAssembly build has this class (a native client uses TCP).

#ifdef __EMSCRIPTEN__

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/transport.hpp"

namespace ants::net {

class WasmWsConnection final : public Connection {
public:
    /// Opens a WebSocket to `url` ("ws://host/path" or "wss://host/path"); nullptr when the browser has none, the address is no such URL, or it cannot be started.
    /// The connection is Connecting until the browser says it is open.
    static std::unique_ptr<WasmWsConnection> connect(const std::string& url);

    ~WasmWsConnection() override;
    WasmWsConnection(const WasmWsConnection&) = delete;
    WasmWsConnection& operator=(const WasmWsConnection&) = delete;

    bool send(const std::vector<uint8_t>& message) override;
    bool poll(std::vector<uint8_t>& message) override;
    State state() const override { return state_; }
    void close() override;
    /// The browser delivers a message in an event between two frames; it is stamped with emscripten_get_now() then, and this is how long the message that poll() returned last
    /// lay in the queue until the frame took it (a page drawn at 30 frames a second: up to 33 ms, a hidden tab: about a second). The ping readout subtracts it.
    uint32_t last_message_age_ms() const override { return last_age_ms_; }

    /// Called (from the browser's event loop, between frames) when the connection has opened. A page that is not drawn runs no frames, so the Hello that opens a
    /// session is sent from here and not from the frame loop (the server closes a connection that says nothing for 10 s).
    void set_on_open(std::function<void()> fn) { on_open_ = std::move(fn); }

    /// The kinds of address the game accepts, the ones that cannot make the browser throw: ws:// or wss://, a host name (letters, digits, dots, hyphens) or a
    /// bracketed IPv6 address, an optional port of at most 65535, then an optional path and query without spaces, controls or '#'; at most 512 characters
    static bool valid_url(const std::string& url);

private:
    WasmWsConnection() = default;

    void close_socket(unsigned short code, const char* reason);

    int socket_{0};
    State state_{State::Connecting};
    bool browser_closed_{false};                // the browser reported the close: nothing is left to close
    std::function<void()> on_open_;
    struct Queued {
        std::vector<uint8_t> data;
        double arrived_ms;                          // emscripten_get_now() when the browser delivered it
    };
    std::deque<Queued> inbox_;
    uint32_t last_age_ms_{0};

    friend struct WasmWsCallbacks;
};

}  // namespace ants::net

#endif  // __EMSCRIPTEN__
