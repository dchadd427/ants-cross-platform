#ifdef __EMSCRIPTEN__

#include "ants_net/wasm_ws.hpp"

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include "ants_net/message_age.hpp"
#include "ants_net/protocol.hpp"

namespace ants::net {

namespace {
constexpr size_t kMaxQueuedMessages = 4096;     // a page that is not drawn (a hidden tab) receives but does not poll: a peer that floods it fails the connection
constexpr size_t kMaxUrlChars = 512;
}  // namespace

// The browser's callbacks (plain functions with the connection as user data). The socket is deleted in the destructor, which also removes them, so a callback
// never runs for a connection that is gone.
struct WasmWsCallbacks {
    // Tells the game that the connection has news (set_on_wake). The game's step may end the match, which deletes this connection and its callbacks while the
    // browser's callback is still running: so the function runs from a copy, and every callback below calls this LAST and returns without touching `c` again.
    static void wake(WasmWsConnection* c) {
        if (!c->on_wake_) return;
        const std::function<void()> fn = c->on_wake_;
        fn();
    }
    static EM_BOOL on_open(int, const EmscriptenWebSocketOpenEvent*, void* user) {
        auto* c = static_cast<WasmWsConnection*>(user);
        if (c->state_ == Connection::State::Connecting) {
            c->state_ = Connection::State::Open;
            if (c->on_open_) c->on_open_();
        }
        return EM_TRUE;
    }
    static EM_BOOL on_error(int, const EmscriptenWebSocketErrorEvent*, void* user) {
        auto* c = static_cast<WasmWsConnection*>(user);
        if (c->state_ == Connection::State::Connecting) c->state_ = Connection::State::Failed;      // never opened: "unable to connect"
        wake(c);
        return EM_TRUE;
    }
    static EM_BOOL on_close(int, const EmscriptenWebSocketCloseEvent*, void* user) {
        auto* c = static_cast<WasmWsConnection*>(user);
        c->browser_closed_ = true;
        if (c->state_ == Connection::State::Connecting) c->state_ = Connection::State::Failed;
        else if (c->state_ == Connection::State::Open) c->state_ = Connection::State::Closed;
        wake(c);
        return EM_TRUE;
    }
    static EM_BOOL on_message(int, const EmscriptenWebSocketMessageEvent* e, void* user) {
        auto* c = static_cast<WasmWsConnection*>(user);
        if (c->state_ != Connection::State::Open) return EM_TRUE;
        if (e->isText || e->numBytes > kMaxMessageBytes || c->inbox_.size() >= kMaxQueuedMessages) {        // the protocol is binary; a text frame or a too large one is a broken peer
            c->state_ = Connection::State::Failed;
            c->close_socket(4002, "protocol error");                         // (a browser accepts only 1000 and 3000 - 4999 from a script)
            wake(c);
            return EM_TRUE;
        }
        c->inbox_.push_back(WasmWsConnection::Queued{std::vector<uint8_t>(e->data, e->data + e->numBytes), emscripten_get_now()});     // (stamped with the browser's clock: see last_message_age_ms)
        wake(c);                                                             // a hidden page polls the message from here (nobody draws a frame)
        return EM_TRUE;
    }
};

bool WasmWsConnection::valid_url(const std::string& url) {
    if (url.size() > kMaxUrlChars) return false;
    size_t i = 0;
    if (url.compare(0, 5, "ws://") == 0) i = 5;
    else if (url.compare(0, 6, "wss://") == 0) i = 6;
    else return false;
    // the host: a name of letters, digits, dots and hyphens, or an IPv6 address in brackets (anything else makes `new WebSocket` throw, and a script exception
    // would run out through the program's main())
    if (i < url.size() && url[i] == '[') {
        const size_t close = url.find(']', i);
        if (close == std::string::npos || close == i + 1) return false;
        for (size_t k = i + 1; k < close; ++k) {
            const char ch = url[k];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':' || ch == '.')) return false;
        }
        i = close + 1;
    } else {
        const size_t begin = i;
        while (i < url.size() && ((url[i] >= '0' && url[i] <= '9') || (url[i] >= 'a' && url[i] <= 'z') || (url[i] >= 'A' && url[i] <= 'Z') || url[i] == '.' || url[i] == '-')) ++i;
        if (i == begin || url[begin] == '.' || url[begin] == '-') return false;
    }
    if (i < url.size() && url[i] == ':') {                                     // an optional port: 1 - 5 digits, at most 65535
        ++i;
        const size_t begin = i;
        unsigned long port = 0;
        while (i < url.size() && url[i] >= '0' && url[i] <= '9' && i - begin < 6) port = port * 10 + static_cast<unsigned long>(url[i++] - '0');
        if (i == begin || port == 0 || port > 65535) return false;
    }
    if (i < url.size() && url[i] != '/' && url[i] != '?') return false;        // nothing else may follow the authority (no userinfo, no fragment)
    for (; i < url.size(); ++i) {
        const auto u = static_cast<unsigned char>(url[i]);
        if (u <= 0x20 || u >= 0x7f || u == '#') return false;                  // no spaces, no control characters, ASCII only, no fragment
    }
    return true;
}

std::unique_ptr<WasmWsConnection> WasmWsConnection::connect(const std::string& url) {
    if (!valid_url(url) || !emscripten_websocket_is_supported()) return nullptr;
    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = url.c_str();
    const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0) return nullptr;                                                                 // (a negative number is an error code)
    std::unique_ptr<WasmWsConnection> c(new WasmWsConnection());
    c->socket_ = socket;
    emscripten_websocket_set_onopen_callback(socket, c.get(), WasmWsCallbacks::on_open);
    emscripten_websocket_set_onerror_callback(socket, c.get(), WasmWsCallbacks::on_error);
    emscripten_websocket_set_onclose_callback(socket, c.get(), WasmWsCallbacks::on_close);
    emscripten_websocket_set_onmessage_callback(socket, c.get(), WasmWsCallbacks::on_message);
    return c;
}

void WasmWsConnection::close_socket(unsigned short code, const char* reason) {
    if (socket_ <= 0 || browser_closed_) return;
    unsigned short ready = 3;
    if (emscripten_websocket_get_ready_state(socket_, &ready) < 0 || ready >= 2) return;           // already closing or closed
    emscripten_websocket_close(socket_, code, reason);
}

WasmWsConnection::~WasmWsConnection() {
    if (socket_ > 0) {
        close_socket(1000, "bye");                                                                   // whatever state_ says: Failed does not mean the socket is shut
        emscripten_websocket_delete(socket_);                                                        // also removes the callbacks
    }
}

bool WasmWsConnection::send(const std::vector<uint8_t>& message) {
    if (state_ != State::Open || message.size() > kMaxMessageBytes) return false;
    return emscripten_websocket_send_binary(socket_, const_cast<uint8_t*>(message.data()), static_cast<uint32_t>(message.size())) >= 0;
}

bool WasmWsConnection::poll(std::vector<uint8_t>& message) {
    if (inbox_.empty()) return false;
    last_age_ms_ = message_age_ms(emscripten_get_now(), inbox_.front().arrived_ms);
    message = std::move(inbox_.front().data);
    inbox_.pop_front();
    return true;
}

void WasmWsConnection::close() {
    close_socket(1000, "bye");
    if (state_ == State::Open || state_ == State::Connecting) state_ = State::Closed;
}

}  // namespace ants::net

#endif  // __EMSCRIPTEN__
