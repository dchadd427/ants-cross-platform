#include "ants_server/restart_record.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

#include "ants_net/attendance.hpp"
#include "ants_net/wire.hpp"
#include "ants_server/secret.hpp"
#include "ants_sim/command.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ants::server {

// ---------------------------------------------------------------------------------------------------------------------------------
// CRC-32
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

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

}  // namespace

uint32_t restart_crc32(const uint8_t* data, size_t size, uint32_t crc) noexcept {
    const std::array<uint32_t, 256>& table = crc_table();
    uint32_t c = ~crc;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The head
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kMaxVersionChars = 32;
constexpr size_t kMaxBuildIdChars = 64;
constexpr size_t kMaxStartBytes = 2048;
constexpr uint16_t kLastProtocolWithoutStartTeams = 12;           // (protocol 13 added the two team bytes to the Start message)
constexpr uint16_t kLastProtocolWithoutStartPlatforms = 14;       // (protocol 15 added the four platform bytes at the end of it)
constexpr size_t kMaxBots = sim::MAX_PLAYERS;

bool printable(const std::string& s, size_t max_chars, bool allow_empty) {
    if (s.size() > max_chars || (s.empty() && !allow_empty)) return false;
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) return false;
    }
    return true;
}

unsigned popcount4(uint8_t mask) {
    unsigned n = 0;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) n += (mask >> s) & 1u;
    return n;
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
}

uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t get_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// A whole frame: type, length, payload, CRC of those three
std::vector<uint8_t> make_frame(RestartFrame type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(payload.size() + kRestartFrameOverhead);
    out.push_back(static_cast<uint8_t>(type));
    put_u32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put_u32(out, restart_crc32(out.data(), out.size()));
    return out;
}

}  // namespace

std::vector<uint8_t> encode_restart_head(const RestartHead& h) {
    std::vector<uint8_t> payload;
    net::ByteWriter w(payload);
    w.u16(kRestartFormat);
    w.str8(h.identity.game_version);
    w.u16(h.identity.protocol);
    w.str8(h.identity.build_id);
    w.str8(h.code);
    w.str8(h.map);
    w.u64(h.map_hash);
    w.u8(h.players);
    w.u8(static_cast<uint8_t>((h.fog ? 1u : 0u) | (h.early_start ? 2u : 0u) | (h.public_room ? 4u : 0u)));
    w.u32(h.wait_ms);
    w.u32(h.load_ms);
    w.u32(h.keep_ms);
    w.u32(h.run_ms);
    w.u32(h.vote_after_ms);
    w.u32(h.max_pause_ms);
    w.u32(h.max_catch_up_ms);
    w.u32(h.resume_countdown_ms);
    w.u64(h.max_log_bytes);
    w.u32(h.max_connections);
    w.u8(static_cast<uint8_t>(std::min<size_t>(h.bots.size(), kMaxBots)));
    for (size_t i = 0; i < h.bots.size() && i < kMaxBots; ++i) {
        const ai::BotSpec& b = h.bots[i];
        w.u8(b.seat);
        w.str8(b.kind);
        w.u8(static_cast<uint8_t>(b.level));
        w.u8(b.seat < sim::MAX_PLAYERS && (h.fill_mask & (1u << b.seat)) != 0 ? 1 : 0);
    }
    const std::vector<uint8_t> start = net::encode(h.start);
    w.u16(static_cast<uint16_t>(std::min<size_t>(start.size(), 0xFFFF)));
    w.bytes(start.data(), std::min<size_t>(start.size(), 0xFFFF));
    for (const net::SeatKey& k : h.keys) w.bytes(k.data(), k.size());
    return make_frame(RestartFrame::Head, payload);
}

bool decode_restart_head(const uint8_t* payload, size_t size, RestartHead& out, std::string& why) {
    const auto bad = [&](const char* what) {
        why = what;
        return false;
    };
    if (payload == nullptr || size == 0 || size > kRestartMaxHeadBytes) return bad("the head of the record has an impossible size");
    net::ByteReader r(payload, size);
    RestartHead h;
    if (r.u16() != kRestartFormat) return bad("the head of the record is of another format");
    h.identity.game_version = r.str8();
    h.identity.protocol = r.u16();
    h.identity.build_id = r.str8();
    h.code = r.str8();
    h.map = r.str8();
    h.map_hash = r.u64();
    h.players = r.u8();
    const uint8_t flags = r.u8();
    h.wait_ms = r.u32();
    h.load_ms = r.u32();
    h.keep_ms = r.u32();
    h.run_ms = r.u32();
    h.vote_after_ms = r.u32();
    h.max_pause_ms = r.u32();
    h.max_catch_up_ms = r.u32();
    h.resume_countdown_ms = r.u32();
    h.max_log_bytes = r.u64();
    h.max_connections = r.u32();
    const uint8_t bot_count = r.u8();
    if (!r.ok()) return bad("the head of the record is cut short");
    if (!printable(h.identity.game_version, kMaxVersionChars, false) || !printable(h.identity.build_id, kMaxBuildIdChars, true)) return bad("the head of the record names a build that cannot be one");
    if (h.code.empty() || !net::valid_room_code(h.code)) return bad("the head of the record has no valid room code");
    if (!net::valid_map_name(h.map)) return bad("the head of the record has no valid map name");
    if (h.players < 2 || h.players > sim::MAX_PLAYERS) return bad("the head of the record has an impossible number of players");
    if ((flags & ~7u) != 0) return bad("the head of the record has flags that this build does not know");
    h.fog = (flags & 1u) != 0;
    h.early_start = (flags & 2u) != 0;
    h.public_room = (flags & 4u) != 0;
    if (bot_count > kMaxBots) return bad("the head of the record has too many bots");
    uint8_t bot_seats = 0;
    for (uint8_t i = 0; i < bot_count; ++i) {
        ai::BotSpec b;
        b.seat = r.u8();
        b.kind = r.str8();
        const uint8_t level = r.u8();
        const uint8_t fill = r.u8();
        if (!r.ok()) return bad("the head of the record is cut short");
        if (b.seat >= sim::MAX_PLAYERS || (bot_seats & (1u << b.seat)) != 0) return bad("the head of the record seats a bot twice or outside the room");
        if (level > static_cast<uint8_t>(ai::Level::Hard) || fill > 1 || !ai::known_bot_kind(b.kind)) return bad("the head of the record names a bot that does not exist");
        b.level = static_cast<ai::Level>(level);
        bot_seats = static_cast<uint8_t>(bot_seats | (1u << b.seat));
        if (fill != 0) h.fill_mask = static_cast<uint8_t>(h.fill_mask | (1u << b.seat));
        h.bots.push_back(std::move(b));
    }
    const uint16_t start_len = r.u16();
    if (!r.ok() || start_len == 0 || start_len > kMaxStartBytes) return bad("the head of the record has a start message of an impossible size");
    const uint8_t* start_bytes = r.take(start_len);
    if (start_bytes == nullptr) return bad("the head of the record is cut short");
    if (!net::decode(start_bytes, start_len, h.start)) {
        // the Start of an older protocol lacks what later ones added at its end (protocol 13: the two team bytes, 15: four platform bytes): with them added as "no teams" and "not told" it reads,
        // and judge_record then refuses the record for its protocol (kept for a day, the room fails and says why). Only the layouts that a release wrote are read, each for its own protocols: the
        // Start of protocol 12 has neither, the Start of protocols 13 and 14 has the team bytes only
        bool read = false;
        const uint16_t protocol = h.identity.protocol;
        const bool lacks_teams = protocol == kLastProtocolWithoutStartTeams;
        if (lacks_teams || (protocol > kLastProtocolWithoutStartTeams && protocol <= kLastProtocolWithoutStartPlatforms)) {
            std::vector<uint8_t> widened(start_bytes, start_bytes + start_len);
            if (lacks_teams) {
                widened.push_back(net::kNoTeam);
                widened.push_back(net::kNoTeam);
            }
            for (size_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) widened.push_back(net::kPlatformUnknown);
            read = net::decode(widened.data(), widened.size(), h.start);
        }
        if (!read) return bad("the start message in the record is not one");
    }
    for (net::SeatKey& k : h.keys) {
        const uint8_t* key = r.take(k.size());
        if (key == nullptr) return bad("the head of the record is cut short");
        std::memcpy(k.data(), key, k.size());
    }
    if (!r.done()) return bad("the head of the record has bytes after its end");
    // the parts must agree: the start message is the match of this room
    if (h.start.map_name != h.map || h.start.map_hash != h.map_hash || h.start.fog != h.fog) return bad("the start message in the record is not that of the room in its head");
    if ((bot_seats & ~h.start.roster) != 0) return bad("the head of the record seats a bot outside the match's roster");
    if (popcount4(h.start.roster) > h.players) return bad("the head of the record has a roster of more seats than the room's players");
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        const net::SeatKey& k = h.keys[s];
        const bool in_roster = (h.start.roster & (1u << s)) != 0;
        const bool is_bot = (bot_seats & (1u << s)) != 0;
        if (!net::key_is_zero(k) && (!in_roster || is_bot)) return bad("the head of the record gives a key to a seat that has no person");
        for (uint8_t t = 0; t < s; ++t) {
            if (!net::key_is_zero(k) && net::key_matches(k, h.keys[t])) return bad("the head of the record gives one key to two seats");
        }
    }
    out = std::move(h);
    return true;
}

uint32_t restart_steady_ms() noexcept {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

// One turn of a TURNS frame, as `payload` (`length` bytes) holds it from `off` on (its command count, then its commands): `turn` is made of it as turn number `number` and `off` moves past it. Null when
// it is good, else what is wrong with it. The parser and the RestartTurnReader decode with this one function.
const char* decode_one_turn(const uint8_t* payload, size_t length, size_t& off, uint32_t number, net::TurnMsg& turn) {
    if (length - off < 2) return "a turn is cut short";
    const uint16_t commands = get_u16(payload + off);
    off += 2;
    if (commands > net::kMaxTurnCommands) return "a turn has more commands than a turn may";
    turn.turn = number;
    turn.commands.clear();
    turn.commands.reserve(commands);
    for (uint16_t c = 0; c < commands; ++c) {
        sim::Command cmd;
        size_t used = 0;
        if (sim::decode(payload + off, length - off, cmd, &used) != sim::DecodeError::None || cmd.issuer >= sim::MAX_PLAYERS) return "a command of a turn is not one";
        off += used;
        turn.commands.push_back(std::move(cmd));
    }
    return nullptr;
}

// The turns of one TURNS frame (the caller has checked its first_turn and count): each is handed to `emit` as it is decoded, and `emit` says false to stop. Null when the frame is good (or `emit`
// stopped: `stopped`), else what is wrong with it.
template <class Emit>
const char* decode_turns_frame(const uint8_t* payload, size_t length, uint32_t first, uint16_t count, Emit&& emit, bool& stopped) {
    size_t off = 6;
    for (uint32_t i = 0; i < count; ++i) {
        net::TurnMsg turn;
        if (const char* bad = decode_one_turn(payload, length, off, first + i, turn)) return bad;
        if (!emit(std::move(turn))) {
            stopped = true;
            return nullptr;
        }
    }
    return off != length ? "a turns frame has bytes after its last turn" : nullptr;
}

}  // namespace

RestartLoaded parse_restart_record(const uint8_t* data, size_t size, bool keep_turns) {
    RestartLoaded r;
    r.file_bytes = size;
    const auto refuse = [&](RestartLoaded::Status status, std::string why) {
        r.status = status;
        r.why = std::move(why);
        r.head = RestartHead{};
        r.turn_count = 0;
        r.turns.clear();
        r.checks.clear();
        r.good_bytes = 0;
        r.torn = false;
        return r;
    };
    if (data == nullptr || size < sizeof(kRestartMagic)) return refuse(RestartLoaded::Status::NotARecord, "the file is too short to be a restart record");
    if (std::memcmp(data, kRestartMagic, sizeof(kRestartMagic) - 1) != 0) return refuse(RestartLoaded::Status::NotARecord, "the file is no restart record");
    if (data[sizeof(kRestartMagic) - 1] != static_cast<uint8_t>(kRestartMagic[sizeof(kRestartMagic) - 1])) return refuse(RestartLoaded::Status::UnknownFormat, "the record is of another format than this build reads");
    size_t pos = sizeof(kRestartMagic);
    bool have_head = false;
    int64_t last_check = -1;
    const auto corrupt = [&](const std::string& what) { return refuse(RestartLoaded::Status::Corrupt, what + " (at byte " + std::to_string(pos) + ")"); };
    while (pos < size) {
        const size_t left = size - pos;
        const bool zeros_to_the_end = [&] {
            for (size_t i = pos; i < size; ++i) {
                if (data[i] != 0) return false;
            }
            return true;
        }();
        if (zeros_to_the_end || left < 5) {                                       // nothing but a zero fill (the file grew and its data did not reach the disk), or a header that was cut short
            r.torn = true;
            break;
        }
        const uint8_t type = data[pos];
        const uint32_t length = get_u32(data + pos + 1);
        if (type < static_cast<uint8_t>(RestartFrame::Head) || type > static_cast<uint8_t>(RestartFrame::Check) || length > kRestartMaxFramePayload) {
            return corrupt("a frame has a type or a length that no writer produces");
        }
        const uint64_t end = uint64_t{pos} + 5 + length + 4;
        if (end > size) {                                                         // the frame does not fit the file: a write that was cut short
            r.torn = true;
            break;
        }
        const uint32_t stored = get_u32(data + end - 4);
        if (restart_crc32(data + pos, 5 + length) != stored) {
            if (end == size) {                                                    // the last frame, and it is not whole: the write of it was interrupted
                r.torn = true;
                break;
            }
            return corrupt("a frame's checksum is wrong");
        }
        const uint8_t* payload = data + pos + 5;
        if (type == static_cast<uint8_t>(RestartFrame::Head)) {
            if (have_head) return corrupt("a second head");
            std::string why;
            if (!decode_restart_head(payload, length, r.head, why)) return corrupt(why);
            have_head = true;
        } else if (!have_head) {
            return corrupt("the first frame is not the head");
        } else if (type == static_cast<uint8_t>(RestartFrame::Turns)) {
            if (length < 6) return corrupt("a turns frame is too short");
            const uint32_t first = get_u32(payload);
            const uint16_t count = get_u16(payload + 4);
            if (first != r.turn_count) return corrupt("the turns are not numbered one after the other");
            if (count == 0 || count > kRestartMaxBatchTurns) return corrupt("a turns frame holds an impossible number of turns");
            if (uint64_t{first} + count > kRestartMaxTurns) return corrupt("the record holds more turns than a match can (" + std::to_string(kRestartMaxTurns) + " at the most)");     // (from the header: nothing of the frame is decoded)
            bool stopped = false;
            if (const char* bad = decode_turns_frame(payload, length, first, count, [&](net::TurnMsg&& turn) {
                    if (keep_turns) r.turns.push_back(std::move(turn));
                    return true;
                }, stopped)) {
                return corrupt(bad);
            }
            r.turn_count = first + count;
        } else {                                                                  // a checkpoint
            if (length != 12) return corrupt("a checkpoint has the wrong size");
            RestartCheck c;
            c.turn = get_u32(payload);
            c.hash = static_cast<uint64_t>(get_u32(payload + 4)) | (static_cast<uint64_t>(get_u32(payload + 8)) << 32);
            if ((uint64_t{c.turn} + 1) % net::kHashEveryTurns != 0 || c.turn >= r.turn_count || static_cast<int64_t>(c.turn) <= last_check) return corrupt("a checkpoint names a turn that it cannot");
            last_check = static_cast<int64_t>(c.turn);
            r.checks.push_back(c);
        }
        pos = static_cast<size_t>(end);
    }
    if (!have_head) return refuse(RestartLoaded::Status::Corrupt, "the head of the record is missing or cut short");
    r.status = RestartLoaded::Status::Ok;
    r.good_bytes = pos;
    return r;
}

RestartTurnReader::RestartTurnReader(const RestartLoaded& record) : rec_(record) {
    if (!record.ok()) {
        fail("the record was not read as a good one");
        return;
    }
    kept_ = record.bytes.empty();                                                 // a Whole record keeps its turns; a Streaming one keeps its bytes
    pos_ = sizeof(kRestartMagic);
    end_ = static_cast<size_t>(std::min<uint64_t>(record.good_bytes, record.bytes.size()));
}

bool RestartTurnReader::fail(const char* why) {
    failed_ = true;
    why_ = why;
    return false;
}

// The turns are all given: the record must have held as many as it was judged to (a Streaming record whose bytes were cut at the end of a frame has fewer)
bool RestartTurnReader::finish() {
    if (given_ != rec_.turn_count) return fail("the record's bytes hold fewer turns than the record was judged to");
    done_ = true;
    return false;
}

bool RestartTurnReader::next(net::TurnMsg& out) {
    if (failed_ || done_) return false;
    if (kept_) {
        if (given_ >= rec_.turns.size()) return finish();
        out = rec_.turns[given_++];
        return true;
    }
    for (;;) {
        if (frame_left_ > 0) {                                                    // a turn of the frame that is open
            const uint8_t* payload = rec_.bytes.data() + frame_pos_ + 5;
            if (const char* bad = decode_one_turn(payload, frame_length_, frame_off_, given_, out)) return fail(bad);
            ++given_;
            --frame_left_;
            return true;
        }
        if (in_frame_) {                                                          // its last turn was given: nothing may follow it in the frame
            in_frame_ = false;
            if (frame_off_ != frame_length_) return fail("a turns frame has bytes after its last turn");
            pos_ = frame_end_;
        }
        if (pos_ >= end_) return finish();                                        // the end of the good frames (a torn tail after them is not read)
        if (end_ - pos_ < kRestartFrameOverhead) return fail("the record's bytes end inside a frame");
        const uint8_t* frame = rec_.bytes.data() + pos_;
        const size_t length = get_u32(frame + 1);
        const uint64_t end = uint64_t{pos_} + 5 + length + 4;
        if (end > end_) return fail("the record's bytes end inside a frame");
        if (frame[0] != static_cast<uint8_t>(RestartFrame::Turns)) {              // the head and the checkpoints hold no turns
            pos_ = static_cast<size_t>(end);
            continue;
        }
        if (length < 6) return fail("a turns frame is too short");
        const uint32_t first = get_u32(frame + 5);
        if (first != given_) return fail("the turns are not numbered one after the other");
        frame_pos_ = pos_;
        frame_end_ = static_cast<size_t>(end);
        frame_length_ = length;
        frame_off_ = 6;
        frame_left_ = get_u16(frame + 9);
        in_frame_ = true;
    }
}

bool for_each_restart_turn(const RestartLoaded& rec, const std::function<bool(const net::TurnMsg&)>& fn) {
    RestartTurnReader reader(rec);
    net::TurnMsg turn;
    while (reader.next(turn)) {
        if (!fn(turn)) return false;
    }
    return !reader.failed();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Files: the few operations that differ between POSIX and Windows
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {

#ifdef _WIN32
std::string windows_error(const char* what) { return std::string(what) + " (Windows error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ")"; }
#else
std::string errno_text(int code) { return std::error_code(code, std::generic_category()).message(); }
#endif

struct NativeFile {
    int fd{-1};
    void* handle{nullptr};
#ifdef _WIN32
    bool is_open() const { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }
#else
    bool is_open() const { return fd >= 0; }
#endif
};

void native_close(NativeFile& f) noexcept {
#ifdef _WIN32
    if (f.is_open()) CloseHandle(static_cast<HANDLE>(f.handle));
    f.handle = nullptr;
#else
    if (f.fd >= 0) ::close(f.fd);
    f.fd = -1;
#endif
}

// Makes a new file (it must not exist), for its owner only
bool native_create_exclusive(const std::string& path, NativeFile& f, std::string& why) {
#ifdef _WIN32
    HANDLE h = CreateFileW(fs::path(path).wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = windows_error("cannot create the file");
        return false;
    }
    f.handle = h;
    return true;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        why = "cannot create the file (" + errno_text(errno) + ")";
        return false;
    }
    f.fd = fd;
    return true;
#endif
}

// Opens an existing file for appending (never follows a symbolic link at the end of the path)
bool native_open_append(const std::string& path, NativeFile& f, std::string& why) {
#ifdef _WIN32
    HANDLE h = CreateFileW(fs::path(path).wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = windows_error("cannot open the file");
        return false;
    }
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(h, zero, nullptr, FILE_END)) {
        why = windows_error("cannot move to the end of the file");
        CloseHandle(h);
        return false;
    }
    f.handle = h;
    return true;
#else
    int flags = O_WRONLY | O_APPEND | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        why = "cannot open the file (" + errno_text(errno) + ")";
        return false;
    }
    f.fd = fd;
    return true;
#endif
}

bool native_write_all(NativeFile& f, const uint8_t* data, size_t size, std::string& why) {
#ifdef _WIN32
    size_t done = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(static_cast<HANDLE>(f.handle), data + done, static_cast<DWORD>(std::min<size_t>(size - done, 1u << 20)), &written, nullptr) || written == 0) {
            why = windows_error("cannot write");
            return false;
        }
        done += static_cast<size_t>(written);
    }
    return true;
#else
    size_t done = 0;
    while (done < size) {
        const ssize_t w = ::write(f.fd, data + done, size - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            why = "cannot write (" + errno_text(w < 0 ? errno : EIO) + ")";
            return false;
        }
        done += static_cast<size_t>(w);
    }
    return true;
#endif
}

// Makes what was written durable. Linux: fdatasync (the data and the size, which is all an appended record needs); elsewhere fsync (macOS has no fdatasync).
bool native_sync(NativeFile& f, std::string& why) {
#ifdef _WIN32
    if (!FlushFileBuffers(static_cast<HANDLE>(f.handle))) {
        why = windows_error("cannot flush");
        return false;
    }
    return true;
#else
    for (;;) {
#if defined(__linux__)
        const int rc = ::fdatasync(f.fd);
#else
        const int rc = ::fsync(f.fd);
#endif
        if (rc == 0) return true;
        if (errno == EINTR) continue;
        why = "cannot flush (" + errno_text(errno) + ")";
        return false;
    }
#endif
}

bool native_truncate(NativeFile& f, uint64_t size, std::string& why) {
#ifdef _WIN32
    LARGE_INTEGER to;
    to.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(static_cast<HANDLE>(f.handle), to, nullptr, FILE_BEGIN) || !SetEndOfFile(static_cast<HANDLE>(f.handle))) {
        why = windows_error("cannot cut the file");
        return false;
    }
    LARGE_INTEGER end;
    end.QuadPart = 0;
    SetFilePointerEx(static_cast<HANDLE>(f.handle), end, nullptr, FILE_END);
    return true;
#else
    if (::ftruncate(f.fd, static_cast<off_t>(size)) != 0) {
        why = "cannot cut the file (" + errno_text(errno) + ")";
        return false;
    }
    return true;
#endif
}

// The rename that gives the record its name: it replaces what was there, all at once
bool native_rename_replace(const std::string& from, const std::string& to, std::string& why) {
#ifdef _WIN32
    if (!MoveFileExW(fs::path(from).wstring().c_str(), fs::path(to).wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        why = windows_error("cannot give the file its name");
        return false;
    }
    return true;
#else
    if (::rename(from.c_str(), to.c_str()) != 0) {
        why = "cannot give the file its name (" + errno_text(errno) + ")";
        return false;
    }
    return true;
#endif
}

// Makes the folder's entries durable (a rename or a new file): POSIX only, and best effort (some file systems refuse a sync of a folder)
void native_sync_dir(const std::string& dir) {
#ifndef _WIN32
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    (void)::fsync(fd);
    ::close(fd);
#else
    (void)dir;
#endif
}

// Opens a file for reading so that the file that is looked at is the file that is read. POSIX: O_NOFOLLOW (a link is not followed), O_NONBLOCK (a pipe cannot hold the open), and the DESCRIPTOR is
// asked what it is and how big (fstat). Windows: a link (a reparse point) is opened as itself and not followed, and the handle is asked. False, with nothing left open, when the file cannot be
// opened; `regular` is false when it can and is no regular file (`why` says what it is).
bool native_open_read(const std::string& path, NativeFile& f, uint64_t& size, bool& regular, std::string& why) {
    regular = true;
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 100; ++tries) {                       // (a virus scanner may hold a file for a moment: a sharing violation is tried again for up to half a second)
        h = CreateFileW(fs::path(path).wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
        Sleep(5);
    }
    if (h == INVALID_HANDLE_VALUE) {
        why = windows_error("cannot open the file");
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        why = windows_error("cannot look at the file");
        CloseHandle(h);
        return false;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        CloseHandle(h);
        regular = false;
        why = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? "a link" : "a folder";
        return false;
    }
    size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | static_cast<uint64_t>(info.nFileSizeLow);
    f.handle = h;
    return true;
#else
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        if (errno == ELOOP || errno == EMLINK) {                      // (what O_NOFOLLOW says of a symbolic link)
            regular = false;
            why = "a link";
        } else {
            why = "cannot open the file (" + errno_text(errno) + ")";
        }
        return false;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        why = "cannot look at the file (" + errno_text(errno) + ")";
        ::close(fd);
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        regular = false;
        why = S_ISDIR(st.st_mode) ? "a folder" : "not a file of data";
        return false;
    }
    size = static_cast<uint64_t>(st.st_size);
    f.fd = fd;
    return true;
#endif
}

// Reads exactly `size` bytes; a file that has become shorter than it was when it was opened fails
bool native_read_exact(NativeFile& f, uint8_t* out, size_t size, std::string& why) {
    size_t done = 0;
#ifdef _WIN32
    while (done < size) {
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(f.handle), out + done, static_cast<DWORD>(std::min<size_t>(size - done, size_t{1} << 20)), &got, nullptr)) {
            why = windows_error("cannot read");
            return false;
        }
        if (got == 0) {
            why = "the file is shorter than it was";
            return false;
        }
        done += static_cast<size_t>(got);
    }
#else
    while (done < size) {
        const ssize_t n = ::read(f.fd, out + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            why = n < 0 ? "cannot read (" + errno_text(errno) + ")" : "the file is shorter than it was";
            return false;
        }
        done += static_cast<size_t>(n);
    }
#endif
    return true;
}

// Deletes a file; false, with the reason, when the delete failed (a file that is not there is deleted already)
bool native_remove(const std::string& path, std::string* why = nullptr) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec && why != nullptr) *why = ec.message();
    return !ec;
}

// The path still names something (a path that cannot be looked at is taken to be there: a delete that failed leaves it so)
bool still_there(const std::string& path) {
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    if (st.type() == fs::file_type::not_found) return false;
    return ec ? true : fs::exists(st);
}

// The folder's lock: the file is made if it is not there and kept locked until the descriptor (the handle) is closed, which the end of the process does too. `busy` says that another holder has it (an error
// of the lock itself is something else). POSIX: flock, which locks the open file and not the process, so another store of this process is refused as well; Windows: opened with no sharing.
bool native_lock_file(const std::string& path, NativeFile& f, bool& busy, std::string& why) {
    busy = false;
#ifdef _WIN32
    HANDLE h = CreateFileW(fs::path(path).wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        busy = code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
        why = windows_error("cannot lock the folder");
        return false;
    }
    f.handle = h;
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        why = "cannot make the lock file (" + errno_text(errno) + ")";
        return false;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int code = errno;
        ::close(fd);
        busy = code == EWOULDBLOCK || code == EAGAIN;
        why = "cannot lock the folder (" + errno_text(code) + ")";
        return false;
    }
    f.fd = fd;
    return true;
#endif
}

std::string hex_digits(const uint8_t* bytes, size_t n) {
    static const char kHex[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < n; ++i) {
        text.push_back(kHex[bytes[i] >> 4]);
        text.push_back(kHex[bytes[i] & 15u]);
    }
    return text;
}

uint32_t fnv1a32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (const char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

constexpr const char* kRecordPrefix = "room-";
constexpr const char* kTempSuffix = ".tmp";
constexpr const char* kReplayingSuffix = ".replaying";
constexpr const char* kLockName = ".lock";
constexpr const char* kRefusedName = "refused";

bool has_suffix(const std::string& s, const std::string& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

}  // namespace

RestartLoaded read_restart_record(const std::string& path, uint64_t max_bytes, RestartRead mode) {
    RestartLoaded r;
    r.path = path;
    const auto fail = [&r](RestartLoaded::Status status, std::string reason) {
        r.status = status;
        r.why = std::move(reason);
        return r;
    };
    // The file that is opened is the file that is looked at and read: no path is looked at twice, so there is nothing to swap in between.
    NativeFile f;
    uint64_t size = 0;
    bool regular = true;
    std::string why;
    if (!native_open_read(path, f, size, regular, why)) return fail(RestartLoaded::Status::Unreadable, regular ? "the record cannot be opened: " + why : "the record is not a regular file (" + why + ")");
    if (size > max_bytes) {
        native_close(f);
        return fail(RestartLoaded::Status::TooBig, "the record is bigger than a record may be (" + std::to_string(size) + " bytes)");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    const bool read_ok = size == 0 || native_read_exact(f, bytes.data(), bytes.size(), why);
    native_close(f);
    if (!read_ok) return fail(RestartLoaded::Status::Unreadable, "the record cannot be read: " + why);
    RestartLoaded loaded = parse_restart_record(bytes.data(), bytes.size(), mode == RestartRead::Whole);
    loaded.path = path;
    if (mode == RestartRead::Streaming && loaded.ok()) loaded.bytes = std::move(bytes);        // (the replay decodes the turns from these, one at a time)
    return loaded;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The writer
// ---------------------------------------------------------------------------------------------------------------------------------

RestartWriter::RestartWriter(RestartStore* store, std::string path, int fd, uint64_t bytes, uint32_t turns) : store_(store), path_(std::move(path)), fd_(fd), bytes_(bytes), turns_(turns) {
    ++store_->open_;
}

RestartWriter::~RestartWriter() {
    close_file();
}

// Closes the file (the record stays on disk) and gives the writer's place back
void RestartWriter::close_file() noexcept {
    NativeFile f;
    f.fd = fd_;
    f.handle = handle_;
    const bool was_open = f.is_open();
    native_close(f);
    fd_ = -1;
    handle_ = nullptr;
    if (was_open && store_ != nullptr) {
        store_->budget_.give(bytes_);
        if (store_->open_ > 0) --store_->open_;
    }
}

bool RestartWriter::fail(const std::string& why) {
    if (!failed_) {
        failed_ = true;
        error_ = why;
        if (store_ != nullptr) ++store_->failed_;
    }
    return false;
}

void RestartWriter::discard() {
    close_file();
    if (store_ != nullptr) store_->remove_file(path_);       // (a delete that fails is not forgotten: the store keeps the file as stale and tries again)
    else native_remove(path_);
    if (!failed_) {                                          // (what the writer does not do any more is not an error: it was told to stop)
        failed_ = true;
        error_ = "the record was deleted";
    }
}

bool RestartWriter::write_frame(RestartFrame type, const std::vector<uint8_t>& payload) {
    if (failed_) return false;
    NativeFile f;
    f.fd = fd_;
    f.handle = handle_;
    if (!f.is_open()) return fail("the record is closed");
    if (payload.size() > kRestartMaxFramePayload) return fail("a frame is bigger than a record's frame may be");
    scratch_.clear();
    scratch_.push_back(static_cast<uint8_t>(type));
    put_u32(scratch_, static_cast<uint32_t>(payload.size()));
    scratch_.insert(scratch_.end(), payload.begin(), payload.end());
    put_u32(scratch_, restart_crc32(scratch_.data(), scratch_.size()));
    const uint64_t frame = scratch_.size();
    if (bytes_ + frame > store_->cfg_.max_record_bytes) return fail("the record passed the size that a record may have (" + std::to_string(store_->cfg_.max_record_bytes) + " bytes)");
    if (!store_->budget_.take(frame)) return fail("the server's disk budget for restart records is used up (" + std::to_string(store_->budget_.limit()) + " bytes)");
    std::string why;
    if (!native_write_all(f, scratch_.data(), scratch_.size(), why)) {
        store_->budget_.give(frame);
        return fail(why);
    }
    bytes_ += frame;
    dirty_ = true;
    return true;
}

bool RestartWriter::append_turn(const net::TurnMsg& turn) {
    if (failed_) return false;
    if (turn.turn != turns_) return fail("a turn came out of order");
    std::vector<uint8_t> payload;
    payload.reserve(16 + 24 * turn.commands.size());
    net::ByteWriter w(payload);
    w.u32(turn.turn);
    w.u16(1);
    const size_t commands = std::min(turn.commands.size(), net::kMaxTurnCommands);
    w.u16(static_cast<uint16_t>(commands));
    for (size_t i = 0; i < commands; ++i) sim::encode(turn.commands[i], payload);
    if (!write_frame(RestartFrame::Turns, payload)) return false;
    ++turns_;
    return true;
}

bool RestartWriter::append_check(uint32_t turn, uint64_t hash) {
    if (failed_) return false;
    std::vector<uint8_t> payload;
    net::ByteWriter w(payload);
    w.u32(turn);
    w.u64(hash);
    return write_frame(RestartFrame::Check, payload);
}

bool RestartWriter::sync() {
    if (failed_) return false;
    NativeFile f;
    f.fd = fd_;
    f.handle = handle_;
    if (!f.is_open()) return fail("the record is closed");
    std::string why;
    if (!native_sync(f, why)) return fail(why);
    dirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------------------------------------------------------------

RestartStore::RestartStore(RestartConfig config) : budget_(config.budget_bytes), cfg_(std::move(config)) {}

RestartStore::~RestartStore() {
    release_lock();
}

void RestartStore::release_lock() noexcept {
    NativeFile f;
    f.fd = lock_fd_;
    f.handle = lock_handle_;
    native_close(f);
    lock_fd_ = -1;
    lock_handle_ = nullptr;
}

// The folder is this server's alone, for as long as the store lives
bool RestartStore::take_lock(std::string& why) {
    NativeFile held;
    held.fd = lock_fd_;
    held.handle = lock_handle_;
    if (held.is_open()) return true;                         // (a second prepare of the same store)
    NativeFile f;
    bool busy = false;
    std::string reason;
    if (!native_lock_file((fs::path(cfg_.dir) / kLockName).string(), f, busy, reason)) {
        why = busy ? "another server is using the restart folder '" + cfg_.dir + "' (its lock is held): two servers would write each other's records" : "the restart folder '" + cfg_.dir + "' cannot be used: " + reason;
        return false;
    }
    lock_fd_ = f.fd;
    lock_handle_ = f.handle;
    return true;
}

std::vector<std::string> RestartStore::take_notes() {
    std::vector<std::string> out;
    out.swap(notes_);
    return out;
}

std::string RestartStore::path_for(const std::string& code) const {
    const uint32_t h = fnv1a32(code);
    const uint8_t digest[4] = {static_cast<uint8_t>(h >> 24), static_cast<uint8_t>(h >> 16), static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h)};
    return (fs::path(cfg_.dir) / (std::string(kRecordPrefix) + code + "-" + hex_digits(digest, 4) + kRestartExtension)).string();
}

bool RestartStore::prepare(std::string& why) {
    if (!enabled()) return true;
    std::error_code ec;
    const bool existed = fs::is_directory(cfg_.dir, ec);
    if (!existed) {
        fs::create_directories(cfg_.dir, ec);
        if (ec) {
            why = "the folder for the restart records cannot be made (" + ec.message() + ")";
            return false;
        }
#ifndef _WIN32
        ::chmod(cfg_.dir.c_str(), 0700);                     // (the folder is the server's own: its records hold the keys of the seats)
#endif
    }
    if (!fs::is_directory(cfg_.dir, ec)) {
        why = "'" + cfg_.dir + "' is not a folder";
        return false;
    }
    if (!take_lock(why)) return false;
    {   // a folder that cannot be written is found out now (a read-only volume, a quota, a full disk), not at the first match that needs a record
        uint8_t random[8];
        if (!random_bytes(random, sizeof random)) {
            why = "no random numbers for a temporary name";
            release_lock();
            return false;
        }
        const std::string probe = (fs::path(cfg_.dir) / (std::string(kRecordPrefix) + "probe-" + hex_digits(random, sizeof random) + kTempSuffix)).string();
        NativeFile f;
        std::string reason;
        const uint8_t bytes[8] = {'p', 'r', 'o', 'b', 'e', 0, 0, 0};
        const bool wrote = native_create_exclusive(probe, f, reason) && native_write_all(f, bytes, sizeof bytes, reason);
        native_close(f);
        native_remove(probe);
        if (!wrote) {
            why = "the restart folder '" + cfg_.dir + "' cannot be written: " + reason;
            release_lock();                                         // (a store that could not prepare the folder holds nothing)
            return false;
        }
    }
    // the temporary files of a record whose making was cut short: ours (the name says so), and nothing can be using them: the lock says that no other server has this folder
    // (and the markers of a record that is not there any more: a record of this name made later would be taken for one that crashed the server)
    for (fs::directory_iterator it(cfg_.dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.compare(0, std::strlen(kRecordPrefix), kRecordPrefix) != 0) continue;
        if (has_suffix(name, kTempSuffix)) native_remove(it->path().string());
        else if (has_suffix(name, kReplayingSuffix) && !still_there(it->path().string().substr(0, it->path().string().size() - std::strlen(kReplayingSuffix)))) native_remove(it->path().string());
    }
    purge_refused();                                                // (what was refused a day ago is not wanted any more)
    return true;
}

size_t RestartStore::purge_refused() {
    const fs::path dir = fs::path(cfg_.dir) / kRefusedName;
    std::error_code ec;
    if (!enabled() || !fs::is_directory(dir, ec)) return 0;
    const auto now = fs::file_time_type::clock::now();
    std::vector<fs::path> old;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || entry_ec) continue;
        const fs::file_time_type written = it->last_write_time(entry_ec);
        if (entry_ec) continue;
        const auto age = now - written;
        const uint64_t age_ms = age.count() < 0 ? 0u : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(age).count());
        if (age_ms > cfg_.refused_keep_ms) old.push_back(it->path());
    }
    size_t purged = 0;
    for (const fs::path& p : old) purged += native_remove(p.string()) ? 1u : 0u;
    return purged;
}

bool RestartStore::mark_replaying(const std::string& record_path) {
    const std::string marker = record_path + kReplayingSuffix;
    NativeFile f;
    std::string why;
    const bool made = native_create_exclusive(marker, f, why);
    native_close(f);
    return made || still_there(marker);                             // (one that is there is a marker as well)
}

void RestartStore::unmark_replaying(const std::string& record_path) { native_remove(record_path + kReplayingSuffix); }

bool RestartStore::was_replaying(const std::string& record_path) const { return still_there(record_path + kReplayingSuffix); }

bool RestartStore::refuse_file(const std::string& path) {
    const std::string name = fs::path(path).filename().string();
    unmark_replaying(path);                                         // (the record is judged: whatever stopped the server is dealt with)
    const auto drop = [&](const std::string& why) {
        remove_file(path);
        note("restart record " + name + " was refused and could not be kept (" + why + "): it was deleted");
        return false;
    };
    std::error_code ec;
    const uint64_t size = static_cast<uint64_t>(fs::file_size(path, ec));
    if (ec) return drop("it cannot be looked at: " + ec.message());
    if (size > cfg_.budget_bytes) return drop("it is bigger than the folder of refused records may be, " + std::to_string(cfg_.budget_bytes) + " bytes");
    const fs::path dir = fs::path(cfg_.dir) / kRefusedName;
    fs::create_directories(dir, ec);
    if (ec) return drop("the folder '" + std::string(kRefusedName) + "' cannot be made: " + ec.message());
#ifndef _WIN32
    ::chmod(dir.c_str(), 0700);                                     // (the records hold the keys of the seats: this folder is the server's own as well)
#endif
    purge_refused();
    // the folder is held under the budget: the oldest files go until this record fits
    struct Kept {
        fs::file_time_type at;
        uint64_t size;
        fs::path path;
    };
    const fs::path to = dir / name;
    std::vector<Kept> kept;
    uint64_t total = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || entry_ec || it->path() == to) continue;               // (a file of this name is replaced below)
        Kept k{it->last_write_time(entry_ec), static_cast<uint64_t>(it->file_size(entry_ec)), it->path()};
        if (entry_ec) continue;
        total += k.size;
        kept.push_back(std::move(k));
    }
    std::sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) { return a.at < b.at; });
    for (const Kept& k : kept) {
        if (total + size <= cfg_.budget_bytes) break;
        if (native_remove(k.path.string())) total -= k.size;
    }
    std::string why;
    native_remove(to.string());                                     // (what was there under this name goes: a rename that replaces is not the same everywhere)
    if (!native_rename_replace(path, to.string(), why)) return drop(why);
    note("restart record " + name + " was refused and is kept in the folder '" + kRefusedName + "' for " + std::to_string(cfg_.refused_keep_ms / 3600000u) + " hours: the owner may move it back to the restart folder");
    return true;
}

std::vector<std::string> RestartStore::records() const {
    std::vector<std::string> out;
    if (!enabled()) return out;
    std::error_code ec;
    for (fs::directory_iterator it(cfg_.dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code type_ec;
        if (name.compare(0, std::strlen(kRecordPrefix), kRecordPrefix) != 0 || !has_suffix(name, kRestartExtension)) continue;
        if (!it->is_regular_file(type_ec) || type_ec) continue;
        out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool RestartStore::remove_file(const std::string& path) {
    std::string why;
    native_remove(path, &why);
    if (!still_there(path)) {
        stale_.erase(std::remove(stale_.begin(), stale_.end(), path), stale_.end());
        return true;
    }
    if (!is_stale(path)) {
        stale_.push_back(path);
        note("restart record " + fs::path(path).filename().string() + " could not be deleted (" + (why.empty() ? std::string("it is still there") : why) + "): a restart would bring its room back from it; the server tries again every 10 s");
    }
    return false;
}

bool RestartStore::is_stale(const std::string& path) const {
    return std::find(stale_.begin(), stale_.end(), path) != stale_.end();
}

size_t RestartStore::retry_stale() {
    std::vector<std::string> still;
    for (const std::string& path : stale_) {
        native_remove(path);
        if (still_there(path)) still.push_back(path);
        else note("restart record " + fs::path(path).filename().string() + " was deleted at last");
    }
    stale_.swap(still);
    return stale_.size();
}

std::unique_ptr<RestartWriter> RestartStore::create(const RestartHead& head, std::string& why) {
    why.clear();
    if (!enabled()) {
        why = "this server keeps no restart records";
        return nullptr;
    }
    const std::vector<uint8_t> frame = encode_restart_head(head);
    const uint64_t total = sizeof(kRestartMagic) + frame.size();
    if (frame.size() > kRestartMaxHeadBytes + kRestartFrameOverhead || total > cfg_.max_record_bytes) {
        why = "the head of the record is bigger than a record may be";
        return nullptr;
    }
    if (!budget_.take(total)) {
        why = "the server's disk budget for restart records is used up (" + std::to_string(budget_.limit()) + " bytes)";
        return nullptr;
    }
    const std::string path = path_for(head.code);
    uint8_t random[8];
    if (!random_bytes(random, sizeof random)) {
        budget_.give(total);
        why = "no random numbers for a temporary name";
        return nullptr;
    }
    const std::string tmp = path + "." + hex_digits(random, sizeof random) + kTempSuffix;
    NativeFile f;
    if (!native_create_exclusive(tmp, f, why)) {
        budget_.give(total);
        return nullptr;
    }
    std::vector<uint8_t> bytes(kRestartMagic, kRestartMagic + sizeof(kRestartMagic));
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    const bool written = native_write_all(f, bytes.data(), bytes.size(), why) && native_sync(f, why);
    native_close(f);
    if (!written) {
        native_remove(tmp);
        budget_.give(total);
        return nullptr;
    }
    if (!native_rename_replace(tmp, path, why)) {
        native_remove(tmp);
        budget_.give(total);
        return nullptr;
    }
    native_sync_dir(cfg_.dir);
    NativeFile a;
    if (!native_open_append(path, a, why)) {
        native_remove(path);
        budget_.give(total);
        return nullptr;
    }
    // (the writer takes over the budget that was taken for the file: give it back when it closes)
#ifdef _WIN32
    auto writer = std::unique_ptr<RestartWriter>(new RestartWriter(this, path, -1, total, 0));
    writer->handle_ = a.handle;
#else
    auto writer = std::unique_ptr<RestartWriter>(new RestartWriter(this, path, a.fd, total, 0));
#endif
    return writer;
}

std::unique_ptr<RestartWriter> RestartStore::reopen(const std::string& path, uint64_t good_bytes, uint32_t turns, std::string& why) {
    why.clear();
    if (!enabled()) {
        why = "this server keeps no restart records";
        return nullptr;
    }
    if (good_bytes > cfg_.max_record_bytes || !budget_.take(good_bytes)) {
        why = "the server's disk budget for restart records does not hold this record";
        return nullptr;
    }
    NativeFile f;
    if (!native_open_append(path, f, why)) {
        budget_.give(good_bytes);
        return nullptr;
    }
    std::error_code ec;
    const uint64_t size = static_cast<uint64_t>(fs::file_size(path, ec));
    if (ec || size < good_bytes || (size > good_bytes && !native_truncate(f, good_bytes, why))) {      // a torn tail is cut off: what is written next follows the last good frame
        if (why.empty()) why = "the record is shorter than what was read from it";
        native_close(f);
        budget_.give(good_bytes);
        return nullptr;
    }
    if (size > good_bytes && !native_sync(f, why)) {
        native_close(f);
        budget_.give(good_bytes);
        return nullptr;
    }
#ifdef _WIN32
    auto writer = std::unique_ptr<RestartWriter>(new RestartWriter(this, path, -1, good_bytes, turns));
    writer->handle_ = f.handle;
#else
    auto writer = std::unique_ptr<RestartWriter>(new RestartWriter(this, path, f.fd, good_bytes, turns));
#endif
    return writer;
}

}  // namespace ants::server
