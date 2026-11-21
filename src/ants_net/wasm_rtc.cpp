#ifdef __EMSCRIPTEN__

// The browser build's data channels (protocol 26, docs/P2P_HINTS.md): the page's AntsP2p (web/p2p.js) makes the RTCPeerConnections and applies the rule of the address; these calls only hand it the
// relay's credential and the signalling, ask it to send a hint, and take what it has to say. Nothing here can throw into the game: a page that has no p2p.js or a browser without WebRTC gives no
// channel (link() says none, every poll says nothing).
#include <emscripten.h>

#include <cstdio>

#include "ants_net/p2p_transport.hpp"

extern "C" {
EM_JS(int, ants_p2p_open, (int seat, int direct, const char* json), {
    try {
        if (!window.AntsP2p || !window.RTCPeerConnection) return 0;
        if (!window.antsP2p) window.antsP2p = window.AntsP2p.create();
        var ice = JSON.parse(UTF8ToString(json));
        window.antsP2p.open(seat, direct !== 0, ice);
        return 1;
    } catch (e) { console.error("p2p open", e); return 0; }
});
EM_JS(void, ants_p2p_peers, (int ready, int direct), {
    try { if (window.antsP2p) window.antsP2p.peers(ready, direct); } catch (e) { console.error("p2p peers", e); }
});
EM_JS(void, ants_p2p_signal, (int from, int kind, const char* text), {
    try { if (window.antsP2p) window.antsP2p.signal(from, kind, UTF8ToString(text)); } catch (e) { console.error("p2p signal", e); }
});
EM_JS(void, ants_p2p_send_hint, (const unsigned char* data, int size), {
    try { if (window.antsP2p) window.antsP2p.sendHint(HEAPU8.slice(data, data + size)); } catch (e) { console.error("p2p hint", e); }
});
// the next signal for the server: buf[0] = the seat it is for, buf[1] = its kind, then the text; the length, 0 when there is none (a text that does not fit is dropped)
EM_JS(int, ants_p2p_take_signal, (unsigned char* buf, int capacity), {
    try {
        var s = window.antsP2p && window.antsP2p.takeSignal();
        if (!s) return 0;
        var text = s.text, n = lengthBytesUTF8(text);
        if (n + 3 > capacity) return 0;
        HEAPU8[buf] = s.to; HEAPU8[buf + 1] = s.kind;
        stringToUTF8(text, buf + 2, capacity - 2);
        return n + 2;
    } catch (e) { console.error("p2p take signal", e); return 0; }
});
// the next hint that a channel delivered: buf[0] = the seat of the channel, then the message; the length, 0 when there is none
EM_JS(int, ants_p2p_take_hint, (unsigned char* buf, int capacity), {
    try {
        var h = window.antsP2p && window.antsP2p.takeHint();
        if (!h || h.bytes.length + 1 > capacity) return 0;
        HEAPU8[buf] = h.from;
        HEAPU8.set(h.bytes, buf + 1);
        return h.bytes.length + 1;
    } catch (e) { console.error("p2p take hint", e); return 0; }
});
EM_JS(int, ants_p2p_link, (int seat), {
    try { return window.antsP2p ? window.antsP2p.link(seat) : 0; } catch (e) { return 0; }
});
EM_JS(int, ants_p2p_rtt, (int seat), {
    try { return window.antsP2p ? window.antsP2p.rtt(seat) | 0 : -1; } catch (e) { return -1; }
});
EM_JS(void, ants_p2p_close, (), {
    try { if (window.antsP2p) window.antsP2p.close(); } catch (e) { console.error("p2p close", e); }
});
}
EM_JS_DEPS(wasm_rtc, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

namespace ants::net {

namespace {

std::string json_quote(const std::string& s) {                  // (the relay's words are plain printable ASCII without blanks: only the quote and the backslash need care)
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out + "\"";
}

class BrowserChannels final : public P2pTransport {
public:
    void open(uint8_t seat, bool direct, const IceConfigMsg& ice) override {
        std::string json = "{\"expires\":" + std::to_string(ice.expires) + ",\"username\":" + json_quote(ice.username) + ",\"credential\":" + json_quote(ice.credential) + ",\"urls\":[";
        for (size_t i = 0; i < ice.urls.size(); ++i) json += (i == 0 ? "" : ",") + json_quote(ice.urls[i]);
        json += "]}";
        ants_p2p_open(seat, direct ? 1 : 0, json.c_str());
    }
    void peers(uint8_t ready, uint8_t direct) override { ants_p2p_peers(ready, direct); }
    void signal(uint8_t from, SignalKind kind, const std::string& text) override { ants_p2p_signal(from, static_cast<int>(kind), text.c_str()); }
    void send_hint(const std::vector<uint8_t>& message) override { ants_p2p_send_hint(message.data(), static_cast<int>(message.size())); }
    bool poll_signal(uint8_t& to, SignalKind& kind, std::string& text) override {
        std::vector<unsigned char> buf(kMaxSignalChars + 8);
        const int n = ants_p2p_take_signal(buf.data(), static_cast<int>(buf.size()));
        if (n < 2) return false;
        to = buf[0];
        kind = static_cast<SignalKind>(buf[1]);
        text.assign(reinterpret_cast<const char*>(buf.data()) + 2, static_cast<size_t>(n) - 2);
        return true;
    }
    bool poll_hint(uint8_t& from, std::vector<uint8_t>& message) override {
        std::vector<unsigned char> buf(kMaxMessageBytes > 512 ? 512 : kMaxMessageBytes);
        const int n = ants_p2p_take_hint(buf.data(), static_cast<int>(buf.size()));
        if (n < 2) return false;
        from = buf[0];
        message.assign(buf.begin() + 1, buf.begin() + n);
        return true;
    }
    PeerLink link(uint8_t seat) const override {
        const int v = ants_p2p_link(seat);
        return v == 3 ? PeerLink::Direct : v == 2 ? PeerLink::Relay : v == 1 ? PeerLink::Connecting : PeerLink::None;
    }
    int32_t rtt_ms(uint8_t seat) const noexcept override { return static_cast<int32_t>(ants_p2p_rtt(seat)); }
    void close() override { ants_p2p_close(); }
};

}  // namespace

std::unique_ptr<P2pTransport> make_browser_channels() { return std::make_unique<BrowserChannels>(); }

}  // namespace ants::net

#endif  // __EMSCRIPTEN__
