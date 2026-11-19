#include "ants_net/p2p.hpp"

#include <algorithm>

#include "ants_net/p2p_transport.hpp"
#include "ants_net/prediction.hpp"
#include "ants_net/wire.hpp"

namespace ants::net {

namespace {

// Printable ASCII with no blank: a user name, a credential and a url as the relay hands them out
bool plain_token(const std::string& s, size_t min_chars, size_t max_chars) {
    if (s.size() < min_chars || s.size() > max_chars) return false;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F) return false;
    }
    return true;
}

// SDP is text with line ends; a signalling payload holds nothing else than that and printable ASCII
bool signal_text(const std::string& s) {
    if (s.size() > kMaxSignalChars) return false;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!(u == 0x09 || u == 0x0A || u == 0x0D || (u >= 0x20 && u < 0x7F))) return false;
    }
    return true;
}

bool open_msg(const uint8_t* data, size_t size, MsgType type, ByteReader& reader) {
    if (data == nullptr || size == 0 || size > kMaxMessageBytes || data[0] != static_cast<uint8_t>(type)) return false;
    reader = ByteReader(data + 1, size - 1);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// The messages
// ---------------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> encode(const HintMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Hint));
    w.u32(m.seq);
    w.u32(m.tick);
    sim::encode(m.command, out);
    return out;
}
bool decode(const uint8_t* data, size_t size, HintMsg& out) {
    ByteReader r(nullptr, 0);
    if (!open_msg(data, size, MsgType::Hint, r)) return false;
    HintMsg m;
    m.seq = r.u32();
    m.tick = r.u32();
    if (!r.ok() || m.seq == 0 || m.tick > kMaxHintTick) return false;
    const size_t used_before = 1 + 4 + 4;
    if (size < used_before + sim::kCommandHeaderBytes) return false;
    size_t used = 0;
    if (sim::decode(data + used_before, size - used_before, m.command, &used) != sim::DecodeError::None || used != size - used_before) return false;
    if (!Prediction::predicts(m.command.type) || m.command.issuer >= sim::MAX_PLAYERS) return false;     // only what a prediction can use, only from a seat
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const P2pConfigMsg& m) { return {static_cast<uint8_t>(MsgType::P2pConfig), static_cast<uint8_t>(m.direct ? 1 : 0)}; }
bool decode(const uint8_t* data, size_t size, P2pConfigMsg& out) {
    ByteReader r(nullptr, 0);
    if (!open_msg(data, size, MsgType::P2pConfig, r)) return false;
    const uint8_t direct = r.u8();
    if (!r.done() || direct > 1) return false;
    out.direct = direct == 1;
    return true;
}

std::vector<uint8_t> encode(const IceConfigMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::IceConfig));
    w.u32(m.expires);
    w.str8(m.username);
    w.str8(m.credential);
    w.u8(static_cast<uint8_t>(std::min(m.urls.size(), kMaxIceUrls)));
    for (size_t i = 0; i < m.urls.size() && i < kMaxIceUrls; ++i) w.str8(m.urls[i]);
    return out;
}
bool decode(const uint8_t* data, size_t size, IceConfigMsg& out) {
    ByteReader r(nullptr, 0);
    if (!open_msg(data, size, MsgType::IceConfig, r)) return false;
    IceConfigMsg m;
    m.expires = r.u32();
    m.username = r.str8();
    m.credential = r.str8();
    const uint8_t n = r.u8();
    if (!r.ok() || n > kMaxIceUrls) return false;
    for (uint8_t i = 0; i < n; ++i) {
        m.urls.push_back(r.str8());
        if (!r.ok() || !plain_token(m.urls.back(), 1, kMaxIceUrlChars)) return false;
    }
    if (!r.done()) return false;
    if (n == 0) {                                                                    // no relay: nothing else is told
        if (m.expires != 0 || !m.username.empty() || !m.credential.empty()) return false;
    } else if (m.expires == 0 || !plain_token(m.username, 1, kMaxIceNameChars) || !plain_token(m.credential, 1, kMaxIceNameChars)) {
        return false;
    }
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const P2pPeersMsg& m) { return {static_cast<uint8_t>(MsgType::P2pPeers), m.ready, m.direct}; }
bool decode(const uint8_t* data, size_t size, P2pPeersMsg& out) {
    ByteReader r(nullptr, 0);
    if (!open_msg(data, size, MsgType::P2pPeers, r)) return false;
    const uint8_t ready = r.u8();
    const uint8_t direct = r.u8();
    if (!r.done() || ready > 0x0F || direct > 0x0F || (direct & ~ready) != 0) return false;      // four seats; a seat that allows direct has sent P2pConfig
    out.ready = ready;
    out.direct = direct;
    return true;
}

std::vector<uint8_t> encode(const SignalMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Signal));
    w.u8(m.peer);
    w.u8(static_cast<uint8_t>(m.kind));
    const size_t n = std::min(m.text.size(), kMaxSignalChars);
    w.u16(static_cast<uint16_t>(n));
    w.bytes(reinterpret_cast<const uint8_t*>(m.text.data()), n);
    return out;
}
bool decode(const uint8_t* data, size_t size, SignalMsg& out) {
    ByteReader r(nullptr, 0);
    if (!open_msg(data, size, MsgType::Signal, r)) return false;
    SignalMsg m;
    m.peer = r.u8();
    const uint8_t kind = r.u8();
    const uint16_t n = r.u16();
    const uint8_t* body = r.take(n);
    if (!r.done() || body == nullptr || m.peer >= sim::MAX_PLAYERS || kind < static_cast<uint8_t>(SignalKind::Offer) || kind > static_cast<uint8_t>(SignalKind::Bye) || n > kMaxSignalChars) return false;
    m.kind = static_cast<SignalKind>(kind);
    m.text.assign(reinterpret_cast<const char*>(body), n);
    if (!signal_text(m.text)) return false;
    out = std::move(m);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// What shows an address
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

// The text of a line from `pos` to the next line end ('\r', '\n') or the end
std::string_view line_from(std::string_view text, size_t pos) {
    size_t end = pos;
    while (end < text.size() && text[end] != '\r' && text[end] != '\n') ++end;
    return text.substr(pos, end - pos);
}

}  // namespace

bool carries_non_relay_candidate(std::string_view text) {
    // 1. every candidate, wherever it stands ("a=candidate:...", the trickled "index|candidate:...", a bare "candidate:..."): it must be a relay one
    size_t at = 0;
    while ((at = text.find("candidate:", at)) != std::string_view::npos) {
        const std::string_view line = line_from(text, at);
        // the type is the word after "typ"; a candidate with no type, or with any type but relay, shows an address
        const size_t typ = line.find(" typ ");
        if (typ == std::string_view::npos) return true;
        std::string_view type = line.substr(typ + 5);
        const size_t blank = type.find(' ');
        if (blank != std::string_view::npos) type = type.substr(0, blank);
        if (type != "relay") return true;
        // a relay candidate names the address that it was made for ("raddr a.b.c.d rport n"): that is the player's own address, which only the blank one hides
        const size_t raddr = line.find(" raddr ");
        if (raddr != std::string_view::npos) {
            std::string_view related = line.substr(raddr + 7);
            const size_t end = related.find(' ');
            if (end != std::string_view::npos) related = related.substr(0, end);
            if (related != "0.0.0.0" && related != "::") return true;
        }
        at += 10;
    }
    // 2. the connection lines: "c=IN IP4 a.b.c.d" and "a=rtcp:9 IN IP4 a.b.c.d" hold the address of the best candidate once it is known
    at = 0;
    while (at < text.size()) {
        const std::string_view line = line_from(text, at);
        const bool connection = line.compare(0, 2, "c=") == 0;
        const bool rtcp = line.compare(0, 7, "a=rtcp:") == 0;
        if (connection || rtcp) {
            const size_t in = line.find("IN IP");
            if (in != std::string_view::npos) {
                const size_t blank = line.find(' ', in + 6);                       // after "IN IP4" / "IN IP6"
                std::string_view address = blank == std::string_view::npos ? std::string_view() : line.substr(blank + 1);
                if (address != "0.0.0.0" && address != "::") return true;
            } else if (connection) {
                return true;                                                       // a connection line in a form that is not understood is not passed on
            }
        }
        at += line.size();
        while (at < text.size() && (text[at] == '\r' || text[at] == '\n')) ++at;
    }
    return false;
}

bool signal_allowed(bool from_direct, bool to_direct, std::string_view text) {
    if (from_direct && to_direct) return true;
    return !carries_non_relay_candidate(text);
}

#ifndef __EMSCRIPTEN__
std::unique_ptr<P2pTransport> make_browser_channels() { return nullptr; }          // (only a page has channels: wasm_rtc.cpp)
#endif

std::string turn_username(uint32_t expires, const std::string& label) { return std::to_string(expires) + ":" + label; }

uint32_t turn_username_expiry(const std::string& username) {
    const size_t colon = username.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 10 || colon + 1 >= username.size()) return 0;
    uint64_t value = 0;
    for (size_t i = 0; i < colon; ++i) {
        if (username[i] < '0' || username[i] > '9') return 0;
        value = value * 10 + static_cast<uint64_t>(username[i] - '0');
    }
    return value > 0xFFFFFFFFull ? 0u : static_cast<uint32_t>(value);
}

}  // namespace ants::net
