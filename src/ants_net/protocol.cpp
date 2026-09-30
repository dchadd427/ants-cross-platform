#include "ants_net/protocol.hpp"

#include <algorithm>

#include "ants_net/wire.hpp"

namespace ants::net {

namespace {

void put_hash(ByteWriter& w, const sim::StateHash& h) {
    w.u64(h.total);
    w.u64(h.engine);
    w.u64(h.players);
    w.u64(h.grid);
    w.u64(h.food);
    w.u64(h.ants);
    w.u64(h.paths);
    w.u64(h.droppers);
}

sim::StateHash get_hash(ByteReader& r) {
    sim::StateHash h;
    h.total = r.u64();
    h.engine = r.u64();
    h.players = r.u64();
    h.grid = r.u64();
    h.food = r.u64();
    h.ants = r.u64();
    h.paths = r.u64();
    h.droppers = r.u64();
    return h;
}

// A message starts with its type byte; check it and give the reader for the rest
bool open(const uint8_t* data, size_t size, MsgType type, ByteReader*& out, ByteReader& storage) {
    if (data == nullptr || size == 0 || size > kMaxMessageBytes) return false;
    if (data[0] != static_cast<uint8_t>(type)) return false;
    storage = ByteReader(data + 1, size - 1);
    out = &storage;
    return true;
}

std::string clip(const std::string& s, size_t max_chars) {
    // only printable ASCII travels (the original's chat and names are 0x20 - 0x7e text)
    std::string out;
    for (char c : s) {
        if (out.size() >= max_chars) break;
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u <= 0x7E) out.push_back(c);
    }
    return out;
}

}  // namespace

MsgType peek_type(const uint8_t* data, size_t size) noexcept {
    if (data == nullptr || size == 0) return MsgType::None;
    const uint8_t t = data[0];
    return (t >= static_cast<uint8_t>(MsgType::Hello) && t <= static_cast<uint8_t>(MsgType::Last)) ? static_cast<MsgType>(t) : MsgType::None;
}

std::vector<uint8_t> encode(const HelloMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Hello));
    w.u16(m.version);
    w.str8(clip(m.name, kMaxNameChars));
    return out;
}
bool decode(const uint8_t* data, size_t size, HelloMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Hello, r, storage)) return false;
    HelloMsg m;
    m.version = r->u16();
    m.name = r->str8();
    if (!r->done() || m.name.size() > kMaxNameChars) return false;
    for (char c : m.name) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
    }
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const WelcomeMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Welcome));
    w.u8(m.player);
    w.u8(m.players);
    return out;
}
bool decode(const uint8_t* data, size_t size, WelcomeMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Welcome, r, storage)) return false;
    WelcomeMsg m;
    m.player = r->u8();
    m.players = r->u8();
    if (!r->done() || m.player >= sim::MAX_PLAYERS || m.players < 2 || m.players > sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const RejectMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Reject));
    w.u8(static_cast<uint8_t>(m.reason));
    return out;
}
bool decode(const uint8_t* data, size_t size, RejectMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Reject, r, storage)) return false;
    const uint8_t reason = r->u8();
    if (!r->done() || reason < static_cast<uint8_t>(RejectReason::Full) || reason > static_cast<uint8_t>(RejectReason::BadRequest)) return false;
    out.reason = static_cast<RejectReason>(reason);
    return true;
}

std::vector<uint8_t> encode(const CommandMsg& m) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(MsgType::Command));
    sim::encode(m.command, out);
    return out;
}
bool decode(const uint8_t* data, size_t size, CommandMsg& out) {
    if (data == nullptr || size < 1 + sim::kCommandHeaderBytes || size > kMaxMessageBytes || data[0] != static_cast<uint8_t>(MsgType::Command)) return false;
    sim::Command c;
    size_t used = 0;
    if (sim::decode(data + 1, size - 1, c, &used) != sim::DecodeError::None || used != size - 1) return false;
    out.command = std::move(c);
    return true;
}

std::vector<uint8_t> encode(const TurnMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Turn));
    w.u32(m.turn);
    w.u16(static_cast<uint16_t>(std::min(m.commands.size(), kMaxTurnCommands)));
    for (size_t i = 0; i < m.commands.size() && i < kMaxTurnCommands; ++i) sim::encode(m.commands[i], out);
    return out;
}
bool decode(const uint8_t* data, size_t size, TurnMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Turn, r, storage)) return false;
    TurnMsg m;
    m.turn = r->u32();
    const size_t n = r->u16();
    if (!r->ok() || n > kMaxTurnCommands) return false;
    size_t pos = 1 + 4 + 2;                               // type, turn, count
    m.commands.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        sim::Command c;
        size_t used = 0;
        if (pos >= size || sim::decode(data + pos, size - pos, c, &used) != sim::DecodeError::None) return false;
        pos += used;
        m.commands.push_back(std::move(c));
    }
    if (pos != size) return false;                        // no trailing bytes
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const AckMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::TurnAck));
    w.u32(m.turn);
    return out;
}
bool decode(const uint8_t* data, size_t size, AckMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::TurnAck, r, storage)) return false;
    AckMsg m;
    m.turn = r->u32();
    if (!r->done()) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const HashMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Hash));
    w.u32(m.turn);
    put_hash(w, m.hash);
    return out;
}
bool decode(const uint8_t* data, size_t size, HashMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Hash, r, storage)) return false;
    HashMsg m;
    m.turn = r->u32();
    m.hash = get_hash(*r);
    if (!r->done()) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const DesyncMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Desync));
    w.u32(m.turn);
    w.u8(m.player);
    put_hash(w, m.host);
    put_hash(w, m.peer);
    return out;
}
bool decode(const uint8_t* data, size_t size, DesyncMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Desync, r, storage)) return false;
    DesyncMsg m;
    m.turn = r->u32();
    m.player = r->u8();
    m.host = get_hash(*r);
    m.peer = get_hash(*r);
    if (!r->done() || m.player >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const ChatMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Chat));
    w.u8(m.sender);
    w.u8(m.team ? 1 : 0);
    w.str8(clip(m.text, kMaxChatChars));
    return out;
}
bool decode(const uint8_t* data, size_t size, ChatMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Chat, r, storage)) return false;
    ChatMsg m;
    m.sender = r->u8();
    const uint8_t team = r->u8();
    m.text = r->str8();
    if (!r->done() || team > 1 || m.text.size() > kMaxChatChars) return false;
    for (char c : m.text) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
    }
    m.team = team == 1;
    out = std::move(m);
    return true;
}

static std::vector<uint8_t> encode_ping_type(MsgType t, const PingMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(t));
    w.u32(m.nonce);
    w.u32(m.sent_ms);
    return out;
}
std::vector<uint8_t> encode_ping(const PingMsg& m) { return encode_ping_type(MsgType::Ping, m); }
std::vector<uint8_t> encode_pong(const PingMsg& m) { return encode_ping_type(MsgType::Pong, m); }
bool decode_ping(const uint8_t* data, size_t size, PingMsg& out) {
    if (data == nullptr || size != 9 || (data[0] != static_cast<uint8_t>(MsgType::Ping) && data[0] != static_cast<uint8_t>(MsgType::Pong))) return false;
    ByteReader r(data + 1, size - 1);
    PingMsg m;
    m.nonce = r.u32();
    m.sent_ms = r.u32();
    if (!r.done()) return false;
    out = m;
    return true;
}

}  // namespace ants::net
