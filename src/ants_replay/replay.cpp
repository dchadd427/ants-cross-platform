#include "ants_replay/replay.hpp"

#include <algorithm>
#include <cstring>

namespace ants::replay {

namespace {

constexpr std::array<uint8_t, 8> kMagic = {0x89, 'A', 'R', 'P', 'L', 0x0D, 0x0A, 0x1A};
constexpr size_t kChunkOverhead = 4 + 4 + 4;           // tag, length, checksum
constexpr size_t kEndsBytes = 4 + 1 + 8;
constexpr size_t kMinCommandBytes = 1 + sim::kCommandHeaderBytes;   // a gap of one byte and a command without ants

// The fields of the head
constexpr uint32_t kFieldGameVersion = 1;
constexpr uint32_t kFieldBuildId = 2;
constexpr uint32_t kFieldMapName = 3;
constexpr uint32_t kFieldMapHash = 4;
constexpr uint32_t kFieldSeed = 5;
constexpr uint32_t kFieldRoster = 6;
constexpr uint32_t kFieldFog = 7;
constexpr uint32_t kFieldName0 = 8;                    // 8 - 11: the names of seats 0 - 3
constexpr uint32_t kFieldTeams = 12;
constexpr uint32_t kFieldRecorderSeat = 13;
constexpr uint32_t kFieldVenue = 14;
constexpr uint32_t kFieldHashPeriod = 15;
constexpr uint32_t kFieldSimRules = 16;               // optional (a reader that does not know it skips it)
constexpr uint32_t kFieldMode = 17;                   // the game mode, one byte; written only when it is not 0 (a reader that does not know it skips it and refuses the file by its sim_rules, which is kSimRulesMode187 then)

const std::array<uint32_t, 256>& crc_table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

void put_u16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>((v >> shift) & 0xFFu));
}

void put_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<uint8_t>((v >> shift) & 0xFFu));
}

void put_varint(std::vector<uint8_t>& out, uint32_t v) {
    while (v >= 0x80u) {
        out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

void put_field(std::vector<uint8_t>& out, uint32_t id, const uint8_t* bytes, size_t size) {
    put_varint(out, id);
    put_varint(out, static_cast<uint32_t>(size));
    out.insert(out.end(), bytes, bytes + size);
}

void put_text_field(std::vector<uint8_t>& out, uint32_t id, const std::string& text) {
    if (text.empty()) return;
    put_field(out, id, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void put_byte_field(std::vector<uint8_t>& out, uint32_t id, uint8_t value) { put_field(out, id, &value, 1); }

void put_chunk(std::vector<uint8_t>& out, const char (&tag)[5], const std::vector<uint8_t>& payload) {
    const uint8_t* t = reinterpret_cast<const uint8_t*>(tag);
    out.insert(out.end(), t, t + 4);
    put_u32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put_u32(out, crc32(payload.data(), payload.size(), crc32(t, 4)));
}

/// Printable text: ASCII from the space to the tilde and nothing else, at most kMaxTextBytes. No control character and no byte of 0x80 and up: a name in a file that came from somewhere else cannot
/// make the tool that shows it print a line that is not there, hide a character, turn the text around or send the terminal an escape sequence.
bool printable(const std::string& text) {
    if (text.size() > kMaxTextBytes) return false;
    for (const char ch : text) {
        const uint8_t b = static_cast<uint8_t>(ch);
        if (b < 0x20u || b > 0x7Eu) return false;
    }
    return true;
}

bool valid_teams(const sim::StartTeams& teams, uint8_t roster) {
    if (!teams.set) return true;
    return teams.a < sim::MAX_PLAYERS && teams.b < sim::MAX_PLAYERS && teams.a != teams.b && ((roster >> teams.a) & 1u) != 0 && ((roster >> teams.b) & 1u) != 0;
}

/// The checks of the head that the writer and the reader share: what a head may hold
bool valid_head(const Header& h, std::string& error) {
    if (h.roster == 0 || h.roster > 0x0Fu) {
        error = "the head names no seat that plays, or a seat that does not exist";
    } else if (h.map_name.empty() || !net::valid_map_name(h.map_name)) {
        error = "the head's map name is empty or cannot name a map file";
    } else if (!printable(h.game_version) || !printable(h.build_id) || !printable(h.venue)) {
        error = "a text of the head is not printable or is too long";
    } else if (!valid_teams(h.teams, h.roster)) {
        error = "the head's teams name seats that do not play";
    } else if (h.recorder_seat != kNoSeat && h.recorder_seat >= sim::MAX_PLAYERS) {
        error = "the head's recorder seat does not exist";
    } else if (h.hash_period == 0 || h.hash_period > kMaxTurns) {
        error = "the head's hash period is out of range";
    } else if (!sim::valid_game_mode(h.mode)) {
        error = "the head's game mode is not one that this build knows (the file may be of a newer build)";
    } else if (h.mode != 0 && h.sim_rules < kSimRulesMode187) {
        error = "the head names a game mode but not the simulation rules that go with it (a build that does not know the mode would play the match as another game)";
    } else if (h.sim_rules == kSimRulesMode187 && h.mode != static_cast<uint8_t>(sim::GameMode::Kills187)) {
        error = "the head names the simulation rules of a game mode but not the mode";
    } else {
        for (const std::string& name : h.names) {
            if (!printable(name)) {
                error = "a name in the head is not printable or is too long";
                return false;
            }
        }
        return true;
    }
    return false;
}

/// A command that the wire form holds and sim::decode accepts again
bool encodable(const sim::Command& c) {
    if (c.type == sim::CommandType::None || c.type > sim::CommandType::Last) return false;
    if (c.ants.size() > sim::kMaxCommandAnts) return false;
    return sim::has_ant_list(c.type) ? !c.ants.empty() : c.ants.empty();
}

std::vector<uint8_t> head_payload(const Header& h) {
    std::vector<uint8_t> p;
    put_u16(p, kFormatVersion);
    put_u16(p, h.engine_rules);
    put_text_field(p, kFieldGameVersion, h.game_version);
    put_text_field(p, kFieldBuildId, h.build_id);
    put_text_field(p, kFieldMapName, h.map_name);
    std::vector<uint8_t> hash;
    put_u64(hash, h.map_hash);
    put_field(p, kFieldMapHash, hash.data(), hash.size());
    std::vector<uint8_t> seed;
    put_u32(seed, h.seed);
    put_field(p, kFieldSeed, seed.data(), seed.size());
    put_byte_field(p, kFieldRoster, h.roster);
    put_byte_field(p, kFieldFog, h.fog ? 1 : 0);
    for (uint32_t s = 0; s < sim::MAX_PLAYERS; ++s) put_text_field(p, kFieldName0 + s, h.names[s]);
    if (h.teams.set) {
        const uint8_t pair[2] = {h.teams.a, h.teams.b};
        put_field(p, kFieldTeams, pair, 2);
    }
    if (h.recorder_seat != kNoSeat) put_byte_field(p, kFieldRecorderSeat, h.recorder_seat);
    put_text_field(p, kFieldVenue, h.venue);
    std::vector<uint8_t> period;
    put_u32(period, h.hash_period);
    put_field(p, kFieldHashPeriod, period.data(), period.size());
    if (h.mode != 0) put_byte_field(p, kFieldMode, h.mode);       // (before sim_rules, only for a mode that is not the original's game: every other file is what it was)
    if (h.sim_rules != 0) {
        std::vector<uint8_t> rules;
        put_u16(rules, h.sim_rules);
        put_field(p, kFieldSimRules, rules.data(), rules.size());
    }
    return p;
}

// ---- reading -----------------------------------------------------------------------------------------------------------------------------------------

struct Cursor {
    const uint8_t* data;
    size_t size;
    size_t pos{0};
    size_t left() const noexcept { return size - pos; }
    bool u8(uint8_t& v) {
        if (left() < 1) return false;
        v = data[pos++];
        return true;
    }
    bool u16(uint16_t& v) {
        if (left() < 2) return false;
        v = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return true;
    }
    bool u32(uint32_t& v) {
        if (left() < 4) return false;
        v = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) | (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!u32(lo) || !u32(hi)) return false;
        v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }
    /// A number of at most 32 bits in its shortest form (what put_varint writes)
    bool varint(uint32_t& v) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 5; ++i) {
            uint8_t b = 0;
            if (!u8(b)) return false;
            if (i == 4 && (b & 0xF0u) != 0) return false;                  // more than 32 bits
            value |= static_cast<uint32_t>(b & 0x7Fu) << (7 * i);
            if ((b & 0x80u) == 0) {
                if (i > 0 && b == 0) return false;                         // a zero that a shorter form would not have
                v = value;
                return true;
            }
        }
        return false;
    }
};

std::string tag_text(const uint8_t* tag) { return std::string(reinterpret_cast<const char*>(tag), 4); }

bool known_text(const uint8_t* bytes, size_t size, std::string& out) {
    out.assign(reinterpret_cast<const char*>(bytes), size);
    return printable(out);
}

bool read_head(const uint8_t* payload, size_t size, Header& h, std::string& error) {
    Cursor c{payload, size};
    uint16_t format = 0;
    uint16_t rules = 0;
    if (!c.u16(format) || !c.u16(rules)) {
        error = "HEAD: too short";
        return false;
    }
    if (format == 0 || format > kFormatVersion) {
        error = format == 0 ? std::string("HEAD: format 0 does not exist") : "made by a newer game (format " + std::to_string(format) + "): update the game";
        return false;
    }
    h = Header{};
    h.sim_rules = 0;                                                       // (unless the file says)
    h.format_version = format;
    h.engine_rules = rules;
    bool have_name = false;
    bool have_hash = false;
    bool have_seed = false;
    bool have_roster = false;
    bool have_fog = false;
    while (c.left() > 0) {
        uint32_t id = 0;
        uint32_t length = 0;
        if (!c.varint(id) || !c.varint(length) || length > c.left()) {
            error = "HEAD: a field is cut or its length is wrong";
            return false;
        }
        const uint8_t* bytes = payload + c.pos;
        c.pos += length;
        const std::string field = "HEAD: field " + std::to_string(id);
        switch (id) {
            case kFieldGameVersion:
                if (!known_text(bytes, length, h.game_version)) { error = field + " is not printable text"; return false; }
                break;
            case kFieldBuildId:
                if (!known_text(bytes, length, h.build_id)) { error = field + " is not printable text"; return false; }
                break;
            case kFieldVenue:
                if (!known_text(bytes, length, h.venue)) { error = field + " is not printable text"; return false; }
                break;
            case kFieldMapName:
                if (!known_text(bytes, length, h.map_name)) { error = field + " is not printable text"; return false; }
                have_name = true;
                break;
            case kFieldMapHash: {
                Cursor f{bytes, length};
                if (length != 8 || !f.u64(h.map_hash)) { error = field + " has the wrong length"; return false; }
                have_hash = true;
                break;
            }
            case kFieldSeed: {
                Cursor f{bytes, length};
                if (length != 4 || !f.u32(h.seed)) { error = field + " has the wrong length"; return false; }
                have_seed = true;
                break;
            }
            case kFieldRoster:
                if (length != 1) { error = field + " has the wrong length"; return false; }
                h.roster = bytes[0];
                have_roster = true;
                break;
            case kFieldFog:
                if (length != 1 || bytes[0] > 1) { error = field + " is not 0 or 1"; return false; }
                h.fog = bytes[0] == 1;
                have_fog = true;
                break;
            case kFieldTeams:
                if (length != 2) { error = field + " has the wrong length"; return false; }
                h.teams.set = true;
                h.teams.a = bytes[0];
                h.teams.b = bytes[1];
                break;
            case kFieldRecorderSeat:
                if (length != 1) { error = field + " has the wrong length"; return false; }
                h.recorder_seat = bytes[0];
                break;
            case kFieldHashPeriod: {
                Cursor f{bytes, length};
                if (length != 4 || !f.u32(h.hash_period)) { error = field + " has the wrong length"; return false; }
                break;
            }
            case kFieldSimRules: {
                Cursor f{bytes, length};
                if (length != 2 || !f.u16(h.sim_rules) || h.sim_rules == 0) { error = field + " has the wrong length or is 0"; return false; }
                break;
            }
            case kFieldMode:
                if (length != 1 || bytes[0] == 0 || !sim::valid_game_mode(bytes[0])) { error = field + " is not a game mode that this build knows (the original's game has no field)"; return false; }
                h.mode = bytes[0];
                break;
            default:
                if (id >= kFieldName0 && id < kFieldName0 + sim::MAX_PLAYERS) {
                    if (!known_text(bytes, length, h.names[id - kFieldName0])) { error = field + " is not printable text"; return false; }
                }
                break;                                                     // an id that this reader does not know is skipped
        }
    }
    if (!have_name || !have_hash || !have_seed || !have_roster || !have_fog) {
        error = std::string("HEAD: the ") + (!have_name ? "map name" : !have_hash ? "map hash" : !have_seed ? "seed" : !have_roster ? "roster" : "fog setting") + " is missing";
        return false;
    }
    std::string why;
    if (!valid_head(h, why)) {
        error = "HEAD: " + why;
        return false;
    }
    return true;
}

}  // namespace

namespace {
// The protocol numbers of files that were made before the head said its sim_rules, and the rules number of the simulation they were recorded on. 15 and 16 share one: protocol 16 (the lobby
// rooms) changed messages only; src/ants_sim, the map reader and the assets are the same files in both (a comment aside) and every golden hash is the same. Add a line only for a number that
// has been proved the same way (a reference file recorded under the older number, played to its final hash under this build).
struct ProtocolRules {
    uint16_t protocol;
    uint16_t sim_rules;
};
constexpr ProtocolRules kKnownProtocols[] = {{15, 1}, {16, 1}};
}  // namespace

uint16_t sim_rules_of(const Header& head) noexcept {
    if (head.sim_rules != 0) return head.sim_rules;
    for (const ProtocolRules& known : kKnownProtocols) {
        if (known.protocol == head.engine_rules) return known.sim_rules;
    }
    return 0;
}

bool plays_here(const Header& head) noexcept {
    const uint16_t wanted = sim_rules_for_mode(head.mode);                  // (0 for a mode that this build does not know: nothing equals it)
    return wanted != 0 && sim_rules_of(head) == wanted;
}

std::string played_rules_text() { return std::to_string(kSimRules) + " and " + std::to_string(kSimRulesMode187); }

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) noexcept {
    const std::array<uint32_t, 256>& table = crc_table();
    uint32_t c = ~crc;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

namespace {

/// The writer of both kinds of file: `live_turns` null writes a replay as it is (complete or cut), else a snapshot (an incomplete replay and its live chunk)
std::vector<uint8_t> encode_file(const Replay& r, const uint32_t* live_turns, std::string& error) {
    error.clear();
    if (!valid_head(r.head, error)) return {};
    if (r.total_turns > kMaxTurns) {
        error = "the replay has more turns than a match can have";
        return {};
    }
    uint32_t last = 0;
    for (size_t i = 0; i < r.commands.size(); ++i) {
        const TimedCommand& tc = r.commands[i];
        if (tc.turn < last || tc.turn > kMaxTurns || (r.complete && tc.turn > r.total_turns)) {
            error = "command " + std::to_string(i) + " is out of turn order or after the last turn";
            return {};
        }
        if (!encodable(tc.command)) {
            error = "command " + std::to_string(i) + " cannot be written in the wire form";
            return {};
        }
        last = tc.turn;
    }
    if (r.complete && static_cast<uint64_t>(r.hashes.size()) * r.head.hash_period > r.total_turns) {
        error = "the replay has more hashes than turns";
        return {};
    }
    if (live_turns != nullptr && (*live_turns > kMaxTurns || *live_turns < last || static_cast<uint64_t>(r.hashes.size()) * r.head.hash_period > *live_turns)) {
        error = "the snapshot's turn count is more than a match can have, or less than its last command or its last hash";
        return {};
    }

    std::vector<uint8_t> out(kMagic.begin(), kMagic.end());
    const std::vector<uint8_t> head = head_payload(r.head);
    if (head.size() > kMaxHeadBytes) {
        error = "the head is too long";
        return {};
    }
    put_chunk(out, "HEAD", head);

    std::vector<uint8_t> chunk;
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t previous = 0;
    const auto flush = [&]() {
        if (count == 0) return;
        std::vector<uint8_t> payload;
        put_u32(payload, first);
        put_u32(payload, count);
        payload.insert(payload.end(), chunk.begin(), chunk.end());
        put_chunk(out, "CMDS", payload);
        chunk.clear();
        count = 0;
    };
    for (const TimedCommand& tc : r.commands) {
        if (count != 0 && (count >= kMaxChunkCommands || tc.turn - first >= kChunkTurns)) flush();
        if (count == 0) {
            first = tc.turn;
            previous = tc.turn;
        }
        put_varint(chunk, tc.turn - previous);
        sim::encode(tc.command, chunk);
        previous = tc.turn;
        ++count;
    }
    flush();

    if (!r.hashes.empty()) {
        std::vector<uint8_t> payload;
        put_u32(payload, r.head.hash_period);
        for (const uint32_t h : r.hashes) put_u32(payload, h);
        put_chunk(out, "hash", payload);
    }
    if (live_turns != nullptr) {
        std::vector<uint8_t> payload;
        put_u32(payload, *live_turns);
        put_chunk(out, "live", payload);
    }
    if (r.complete) {
        std::vector<uint8_t> payload;
        put_u32(payload, r.total_turns);
        payload.push_back(r.match_over ? 1 : 0);
        put_u64(payload, r.final_hash);
        put_chunk(out, "ENDS", payload);
    }
    if (out.size() > kMaxFileBytes) {
        error = "the replay is longer than " + std::to_string(kMaxFileBytes / 1024) + " KiB";
        return {};
    }
    return out;
}

}  // namespace

std::vector<uint8_t> encode(const Replay& r, std::string& error) { return encode_file(r, nullptr, error); }

std::vector<uint8_t> encode_snapshot(const Replay& r, uint32_t turns, std::string& error) {
    if (r.complete) {
        error = "a snapshot is a replay without its end, and this one has it";
        return {};
    }
    return encode_file(r, &turns, error);
}

bool decode(const uint8_t* data, size_t size, Replay& out, std::string& error) {
    error.clear();
    if (data == nullptr || size < kMagic.size() || std::memcmp(data, kMagic.data(), kMagic.size()) != 0) {
        error = "not a replay file (the first bytes are not those of one)";
        return false;
    }
    if (size > kMaxFileBytes) {
        error = "the file is larger than " + std::to_string(kMaxFileBytes / 1024) + " KiB";
        return false;
    }
    Replay r;
    bool have_head = false;
    bool have_hashes = false;
    bool have_ends = false;
    bool have_live = false;
    uint32_t live_turns = 0;
    uint32_t last_turn = 0;
    size_t pos = kMagic.size();
    while (pos < size) {
        if (have_ends) {
            error = size - pos >= 4 && std::memcmp(data + pos, "live", 4) == 0 ? "live: a live chunk after the ENDS chunk (a snapshot has no end)" : "bytes after the ENDS chunk";
            return false;
        }
        const size_t at = pos;
        if (size - pos < kChunkOverhead) {                                 // the file is cut inside a chunk header
            if (!have_head) {
                error = "the file is cut before its HEAD chunk is complete";
                return false;
            }
            break;
        }
        const uint8_t* tag = data + pos;
        Cursor header{data + pos + 4, 4};
        uint32_t length = 0;
        header.u32(length);
        for (int i = 0; i < 4; ++i) {
            const bool letter = (tag[i] >= 'A' && tag[i] <= 'Z') || (tag[i] >= 'a' && tag[i] <= 'z');
            if (!letter) {
                error = "a chunk at byte " + std::to_string(at) + " has no valid tag";
                return false;
            }
        }
        const std::string name = tag_text(tag);
        if (length > size - pos - kChunkOverhead) {                        // its length runs past the end of the file: the file was cut here
            if (!have_head) {
                error = "chunk " + name + ": its length runs past the end of the file";
                return false;
            }
            break;
        }
        const uint8_t* payload = data + pos + 8;
        Cursor stored{payload + length, 4};
        uint32_t crc = 0;
        stored.u32(crc);
        if (crc32(payload, length, crc32(tag, 4)) != crc) {
            error = "chunk " + name + " at byte " + std::to_string(at) + ": the checksum is wrong (the file is damaged)";
            return false;
        }
        pos += kChunkOverhead + length;

        if (name == "HEAD") {
            if (have_head || at != kMagic.size()) {
                error = "HEAD is not the first chunk, or there are two";
                return false;
            }
            if (length > kMaxHeadBytes) {
                error = "HEAD: longer than a head can be";
                return false;
            }
            if (!read_head(payload, length, r.head, error)) return false;
            have_head = true;
            continue;
        }
        if (!have_head) {
            error = "the first chunk is not HEAD";
            return false;
        }
        if (name == "CMDS") {
            Cursor c{payload, length};
            uint32_t first = 0;
            uint32_t count = 0;
            if (!c.u32(first) || !c.u32(count)) {
                error = "CMDS: too short";
                return false;
            }
            if (count > kMaxChunkCommands || count > c.left() / kMinCommandBytes || first > kMaxTurns || first < last_turn) {
                error = "CMDS at byte " + std::to_string(at) + ": the count or the first turn is out of range";
                return false;
            }
            uint32_t turn = first;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t gap = 0;
                if (!c.varint(gap) || gap > kMaxTurns - turn) {
                    error = "CMDS: command " + std::to_string(i) + " has a bad turn gap";
                    return false;
                }
                turn += gap;
                TimedCommand tc;
                tc.turn = turn;
                size_t used = 0;
                if (sim::decode(payload + c.pos, c.left(), tc.command, &used) != sim::DecodeError::None) {
                    error = "CMDS: command " + std::to_string(i) + " is not a valid command";
                    return false;
                }
                c.pos += used;
                r.commands.push_back(std::move(tc));
            }
            if (c.left() != 0) {
                error = "CMDS: bytes after its last command";
                return false;
            }
            if (count != 0) last_turn = turn;
        } else if (name == "hash") {
            Cursor c{payload, length};
            uint32_t period = 0;
            if (have_hashes || !c.u32(period) || period == 0 || period != r.head.hash_period || c.left() % 4 != 0) {
                error = "hash: two hash chunks, a period that is not the head's, or a cut value";
                return false;
            }
            have_hashes = true;
            const size_t n = c.left() / 4;
            if (static_cast<uint64_t>(n) * period > kMaxTurns) {
                error = "hash: more values than a match has turns";
                return false;
            }
            for (size_t i = 0; i < n; ++i) {
                uint32_t v = 0;
                c.u32(v);
                r.hashes.push_back(v);
            }
        } else if (name == "live") {
            Cursor c{payload, length};
            if (have_live) {
                error = "live: two live chunks";
                return false;
            }
            if (length != 4 || !c.u32(live_turns)) {
                error = "live: the wrong length";
                return false;
            }
            if (live_turns > kMaxTurns) {
                error = "live: more turns than a match can have";
                return false;
            }
            have_live = true;
        } else if (name == "ENDS") {
            Cursor c{payload, length};
            uint8_t flags = 0;
            if (length != kEndsBytes || !c.u32(r.total_turns) || !c.u8(flags) || !c.u64(r.final_hash) || flags > 1) {
                error = "ENDS: the wrong length or an unknown flag";
                return false;
            }
            r.match_over = (flags & 1u) != 0;
            have_ends = true;
        } else if (tag[0] >= 'A' && tag[0] <= 'Z') {
            error = "a chunk " + name + " that this reader does not know and cannot skip (a newer file?)";
            return false;
        }                                                                  // a lower case chunk that is not known is skipped
    }
    if (!have_head) {
        error = "the file has no HEAD chunk";
        return false;
    }
    if (have_live && have_ends) {
        error = "live: a live chunk in a file that has its ENDS chunk (a snapshot has no end)";
        return false;
    }
    if (have_ends) {
        if (r.total_turns > kMaxTurns || last_turn > r.total_turns || static_cast<uint64_t>(r.hashes.size()) * r.head.hash_period > r.total_turns) {
            error = "ENDS: the turn count does not fit the commands and the hashes";
            return false;
        }
        r.complete = true;
    } else {
        const uint32_t hashed = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(r.hashes.size()) * r.head.hash_period, kMaxTurns));
        if (have_live && (live_turns < last_turn || live_turns < hashed)) {
            error = "live: fewer turns than the file's last command or last hash";
            return false;
        }
        r.total_turns = std::max({last_turn, hashed, live_turns});
        r.complete = false;
    }
    out = std::move(r);
    return true;
}

}  // namespace ants::replay
