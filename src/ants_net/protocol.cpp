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

// An address as text: an IPv4 / IPv6 literal or a host name (no spaces, no control characters), short
bool valid_address(const std::string& a) {
    if (a.size() > 64) return false;
    for (char c : a) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == ':' || c == '-' || c == '_' ||
                        c == '%' || c == '[' || c == ']';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

bool valid_map_name(const std::string& name) noexcept {
    if (name.size() < 5 || name.size() > kMaxMapNameChars) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) return false;                                       // printable ASCII only: no control character, no NUL, no multi-byte text
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;      // never a path, never a Windows-forbidden name
    }
    if (name[0] == '.') return false;                                                 // a hidden file; (a ".." INSIDE a name is harmless: the name cannot hold a separator and ends in ".lvl")
    const std::string tail = name.substr(name.size() - 4);
    return tail == ".LVL" || tail == ".lvl";
}

bool valid_room_code(const std::string& code) noexcept {
    if (code.empty()) return true;                                                    // no room: a LAN or direct host
    if (code.size() > kMaxRoomCodeChars) return false;
    for (char c : code) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

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
    w.u16(m.listen_port);
    w.u8(m.want_seat);
    w.str8(clip(m.room, kMaxRoomCodeChars));
    w.str8(clip(m.token, kMaxTokenChars));
    return out;
}
bool decode(const uint8_t* data, size_t size, HelloMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Hello, r, storage)) return false;
    HelloMsg m;
    m.version = r->u16();
    m.name = r->str8();
    m.listen_port = r->u16();
    m.want_seat = r->u8();
    m.room = r->str8();
    m.token = r->str8();
    if (!r->done() || m.name.size() > kMaxNameChars || m.token.size() > kMaxTokenChars || !valid_room_code(m.room)) return false;
    for (char c : m.name) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
    }
    for (char c : m.token) {
        if (static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7E) return false;       // a token has no spaces and no control characters
    }
    out = std::move(m);
    return true;
}
bool decode_hello_prefix(const uint8_t* data, size_t size, HelloMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Hello, r, storage)) return false;
    HelloMsg m;
    m.version = r->u16();
    m.name = r->str8();
    if (!r->ok() || m.name.size() > kMaxNameChars) return false;       // what follows is the layout of some version: not read
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
    if (!r->done() || reason < static_cast<uint8_t>(RejectReason::Full) || reason > static_cast<uint8_t>(RejectReason::NoSuchRoom)) return false;
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

namespace {
bool printable_name(const std::string& s, size_t max) {
    if (s.size() > max) return false;
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
    }
    return true;
}
}  // namespace

std::vector<uint8_t> encode(const RoomMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Room));
    for (const auto& slot : m.slots) {
        w.u8(static_cast<uint8_t>(slot.state));
        w.str8(clip(slot.name, kMaxNameChars));
        w.u16(slot.rtt_ms);
    }
    w.str8(m.map_name);
    w.u8(m.fog ? 1 : 0);
    w.u8(m.you);
    w.u8(m.leader);
    return out;
}
bool decode(const uint8_t* data, size_t size, RoomMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Room, r, storage)) return false;
    RoomMsg m;
    for (auto& slot : m.slots) {
        const uint8_t st = r->u8();
        slot.name = r->str8();
        slot.rtt_ms = r->u16();
        if (st > static_cast<uint8_t>(SlotState::Bot) || !printable_name(slot.name, kMaxNameChars)) return false;
        slot.state = static_cast<SlotState>(st);
    }
    m.map_name = r->str8();
    const uint8_t fog = r->u8();
    m.you = r->u8();
    m.leader = r->u8();
    // an empty map name = the host has not chosen a map yet
    if (!r->done() || fog > 1 || (!m.map_name.empty() && !valid_map_name(m.map_name)) || (m.you != 255 && m.you >= sim::MAX_PLAYERS)) return false;
    // the leader: nobody (255), or a seat that a person holds as a guest (a server's room has no host in a seat; a bot, an empty seat or the host of a LAN room never leads)
    if (m.leader != kNoLeader && (m.leader >= sim::MAX_PLAYERS || m.slots[m.leader].state != SlotState::Client)) return false;
    m.fog = fog == 1;
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const StartMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Start));
    w.u32(m.seed);
    w.str8(m.map_name);
    w.u64(m.map_hash);
    w.u8(m.fog ? 1 : 0);
    w.u8(m.roster);
    for (const auto& n : m.names) w.str8(clip(n, kMaxNameChars));
    for (const auto& e : m.endpoints) {
        w.str8(e.address.size() > 64 ? std::string() : e.address);
        w.u16(e.port);
    }
    return out;
}
bool decode(const uint8_t* data, size_t size, StartMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Start, r, storage)) return false;
    StartMsg m;
    m.seed = r->u32();
    m.map_name = r->str8();
    m.map_hash = r->u64();
    const uint8_t fog = r->u8();
    m.roster = r->u8();
    for (auto& n : m.names) {
        n = r->str8();
        if (!printable_name(n, kMaxNameChars)) return false;
    }
    for (auto& e : m.endpoints) {
        e.address = r->str8();
        e.port = r->u16();
        if (!valid_address(e.address)) return false;
    }
    int players = 0;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) players += (m.roster >> p) & 1;
    if (!r->done() || fog > 1 || !valid_map_name(m.map_name) || (m.roster & 0xF0) != 0 || players < 2) return false;
    m.fog = fog == 1;
    out = std::move(m);
    return true;
}

std::vector<uint8_t> encode(const LoadedMsg& m) {
    return {static_cast<uint8_t>(MsgType::Loaded), static_cast<uint8_t>(m.ok ? 1 : 0)};
}
bool decode(const uint8_t* data, size_t size, LoadedMsg& out) {
    if (data == nullptr || size != 2 || data[0] != static_cast<uint8_t>(MsgType::Loaded) || data[1] > 1) return false;
    out.ok = data[1] == 1;
    return true;
}

std::vector<uint8_t> encode(const CancelMsg& m) {
    return {static_cast<uint8_t>(MsgType::Cancel), static_cast<uint8_t>(m.reason), m.player};
}
bool decode(const uint8_t* data, size_t size, CancelMsg& out) {
    if (data == nullptr || size != 3 || data[0] != static_cast<uint8_t>(MsgType::Cancel)) return false;
    if (data[1] < static_cast<uint8_t>(CancelMsg::Reason::PlayerLeft) || data[1] > static_cast<uint8_t>(CancelMsg::Reason::HostCancelled)) return false;
    if (data[2] != 255 && data[2] >= sim::MAX_PLAYERS) return false;
    out.reason = static_cast<CancelMsg::Reason>(data[1]);
    out.player = data[2];
    return true;
}

std::vector<uint8_t> encode_begin() { return {static_cast<uint8_t>(MsgType::Begin)}; }
std::vector<uint8_t> encode(const ProposeMsg& m) {
    return {static_cast<uint8_t>(MsgType::Propose), m.epoch, m.candidate};
}
bool decode(const uint8_t* data, size_t size, ProposeMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Propose, r, storage)) return false;
    ProposeMsg m;
    m.epoch = r->u8();
    m.candidate = r->u8();
    if (!r->done() || m.candidate >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const AcceptMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Accept));
    w.u8(m.epoch);
    w.u32(m.next_receive);
    w.u32(m.next_execute);
    return out;
}
bool decode(const uint8_t* data, size_t size, AcceptMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Accept, r, storage)) return false;
    AcceptMsg m;
    m.epoch = r->u8();
    m.next_receive = r->u32();
    m.next_execute = r->u32();
    if (!r->done() || m.next_execute > m.next_receive) return false;         // nobody executes a turn it has not received
    out = m;
    return true;
}

std::vector<uint8_t> encode(const RefuseMsg& m) {
    return {static_cast<uint8_t>(MsgType::Refuse), m.epoch, m.lowest};
}
bool decode(const uint8_t* data, size_t size, RefuseMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Refuse, r, storage)) return false;
    RefuseMsg m;
    m.epoch = r->u8();
    m.lowest = r->u8();
    if (!r->done() || m.lowest >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const ResumeMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Resume));
    w.u8(m.epoch);
    w.u8(m.host);
    w.u32(m.resume_turn);
    return out;
}
bool decode(const uint8_t* data, size_t size, ResumeMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Resume, r, storage)) return false;
    ResumeMsg m;
    m.epoch = r->u8();
    m.host = r->u8();
    m.resume_turn = r->u32();
    if (!r->done() || m.host >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const RequestMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Request));
    w.u32(m.from_turn);
    return out;
}
bool decode(const uint8_t* data, size_t size, RequestMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Request, r, storage)) return false;
    RequestMsg m;
    m.from_turn = r->u32();
    if (!r->done()) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const PeerHelloMsg& m) {
    return {static_cast<uint8_t>(MsgType::PeerHello), m.seat};
}
bool decode(const uint8_t* data, size_t size, PeerHelloMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::PeerHello, r, storage)) return false;
    PeerHelloMsg m;
    m.seat = r->u8();
    if (!r->done() || m.seat >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode(const StartRequestMsg&) { return {static_cast<uint8_t>(MsgType::StartRequest)}; }
bool decode(const uint8_t* data, size_t size, StartRequestMsg&) {
    return data != nullptr && size == 1 && data[0] == static_cast<uint8_t>(MsgType::StartRequest);
}

std::vector<uint8_t> encode(const LagMsg& m) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.u8(static_cast<uint8_t>(MsgType::Lag));
    w.u8(m.seat);
    w.u32(m.behind_ms);
    return out;
}
bool decode(const uint8_t* data, size_t size, LagMsg& out) {
    ByteReader storage(nullptr, 0);
    ByteReader* r = nullptr;
    if (!open(data, size, MsgType::Lag, r, storage)) return false;
    LagMsg m;
    m.seat = r->u8();
    m.behind_ms = r->u32();
    if (!r->done() || m.seat >= sim::MAX_PLAYERS) return false;
    out = m;
    return true;
}

std::vector<uint8_t> encode_leave() { return {static_cast<uint8_t>(MsgType::Leave)}; }

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
