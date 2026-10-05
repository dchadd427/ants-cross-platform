#include "ants_app/rejoin_store.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
#elif !defined(_WIN32)
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace ants::app {

namespace {

constexpr size_t kMaxServerChars = 255;
constexpr size_t kMaxFileBytes = 64 * 1024;                       // eight lines of a few hundred bytes: a file beyond this is not ours
constexpr size_t kHexChars = net::kKeyBytes * 2;

bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7E; }

bool printable_text(const std::string& s, size_t max_chars) noexcept {
    if (s.empty() || s.size() > max_chars) return false;
    for (const char c : s) {
        if (!printable(c)) return false;
    }
    return true;
}

bool storable_room(const std::string& room) noexcept { return !room.empty() && net::valid_room_code(room); }

// The age of an entry against the clock: a key of a match that a day has passed over is of no use; a time far in the future is a broken clock or a broken file, and is no entry either
bool fresh_at(int64_t written_ms, int64_t now_ms) noexcept {
    if (written_ms < 0) return false;
    if (now_ms - written_ms > kRejoinMaxAgeMs) return false;
    if (written_ms - now_ms > kRejoinMaxAgeMs) return false;
    return true;
}

// Newest first (the first of two with the same time stays first), at most kRejoinMaxEntries
void order_and_cap(std::vector<RejoinEntry>& entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const RejoinEntry& a, const RejoinEntry& b) { return a.written_ms > b.written_ms; });
    if (entries.size() > kRejoinMaxEntries) entries.resize(kRejoinMaxEntries);
}

bool same_seat(const RejoinEntry& e, const net::RejoinKey& k) noexcept { return e.server == k.server && e.room == k.room && e.seat == k.seat; }

RejoinEntry entry_of(const net::RejoinKey& k, int64_t now_ms) {
    RejoinEntry e;
    e.server = k.server;
    e.room = k.room;
    e.seat = k.seat;
    e.key = k.key;
    e.written_ms = now_ms;
    return e;
}

// The entries without the one of this key's seat, and the key in front: what a file or a memory holds after a put
std::vector<RejoinEntry> with_key(std::vector<RejoinEntry> entries, const net::RejoinKey& key, int64_t now_ms) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&key](const RejoinEntry& e) { return same_seat(e, key); }), entries.end());
    entries.insert(entries.begin(), entry_of(key, now_ms));
    order_and_cap(entries);
    return entries;
}

// The entries without the one that holds this very key at this seat
std::vector<RejoinEntry> without_key(std::vector<RejoinEntry> entries, const net::RejoinKey& key) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&key](const RejoinEntry& e) { return same_seat(e, key) && net::key_matches(e.key, key.key); }), entries.end());
    return entries;
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool digits_to_int(const std::string& s, size_t max_digits, int64_t& out) noexcept {
    if (s.empty() || s.size() > max_digits) return false;
    int64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

}  // namespace

int64_t wall_clock_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string rejoin_server_text(const std::string& address, uint16_t port, const std::string& url) {
    if (!url.empty()) return url;
    return (address.find(':') != std::string::npos ? "[" + address + "]" : address) + ":" + std::to_string(port);
}

std::string rejoin_key_hex(const net::SeatKey& key) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kHexChars);
    for (const uint8_t b : key) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0Fu]);
    }
    return out;
}

bool rejoin_key_from_hex(const std::string& hex, net::SeatKey& out) {
    if (hex.size() != kHexChars) return false;
    net::SeatKey key{};
    for (size_t i = 0; i < net::kKeyBytes; ++i) {
        const int hi = hex_value(hex[2 * i]);
        const int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    if (net::key_is_zero(key)) return false;
    out = key;
    return true;
}

bool rejoin_storable(const net::RejoinKey& key) {
    return storable_room(key.room) && key.seat < 4 && !net::key_is_zero(key.key) && printable_text(key.server, kMaxServerChars);
}

// ---- the lookups ------------------------------------------------------------------------------------------------------------------------------------

std::optional<RejoinEntry> RejoinStore::find(const std::string& server, const std::string& room, uint8_t seat) {
    for (RejoinEntry& e : entries()) {
        if (e.server == server && e.room == room && (seat >= 4 || e.seat == seat)) return std::move(e);
    }
    return std::nullopt;
}

std::optional<RejoinEntry> RejoinStore::newest() {
    std::vector<RejoinEntry> all = entries();
    if (all.empty()) return std::nullopt;
    return std::move(all.front());
}

// ---- memory -----------------------------------------------------------------------------------------------------------------------------------------

bool MemoryRejoinStore::put(const net::RejoinKey& key) {
    if (!rejoin_storable(key)) return false;
    kept_ = with_key(entries(), key, clock_());
    return true;
}

void MemoryRejoinStore::forget(const net::RejoinKey& key) { kept_ = without_key(entries(), key); }

std::vector<RejoinEntry> MemoryRejoinStore::entries() {
    const int64_t now = clock_();
    kept_.erase(std::remove_if(kept_.begin(), kept_.end(), [now](const RejoinEntry& e) { return !fresh_at(e.written_ms, now); }), kept_.end());
    return kept_;
}

// ---- the desktop's file -----------------------------------------------------------------------------------------------------------------------------

std::string FileRejoinStore::serialise(const std::vector<RejoinEntry>& entries) {
    std::string out;
    for (const RejoinEntry& e : entries) {
        out += e.server + '\t' + e.room + '\t' + std::to_string(static_cast<unsigned>(e.seat)) + '\t' + rejoin_key_hex(e.key) + '\t' + std::to_string(e.written_ms / 1000) + '\n';
    }
    return out;
}

std::vector<RejoinEntry> FileRejoinStore::parse(const std::string& text, int64_t now_ms) {
    std::vector<RejoinEntry> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> fields;                                  // exactly five, split at the tabs
        size_t from = 0;
        for (;;) {
            const size_t tab = line.find('\t', from);
            if (tab == std::string::npos || fields.size() == 4) {
                fields.push_back(line.substr(from));
                break;
            }
            fields.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (fields.size() != 5) continue;
        RejoinEntry e;
        int64_t seat = 0;
        int64_t seconds = 0;
        if (!printable_text(fields[0], kMaxServerChars)) continue;
        if (!storable_room(fields[1])) continue;
        if (!digits_to_int(fields[2], 1, seat) || seat > 3) continue;
        if (!rejoin_key_from_hex(fields[3], e.key)) continue;
        if (!digits_to_int(fields[4], 12, seconds)) continue;
        e.server = fields[0];
        e.room = fields[1];
        e.seat = static_cast<uint8_t>(seat);
        e.written_ms = seconds * 1000;
        if (!fresh_at(e.written_ms, now_ms)) continue;
        out.push_back(std::move(e));
    }
    order_and_cap(out);
    return out;
}

std::vector<RejoinEntry> FileRejoinStore::read(int64_t now_ms) const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return {};
    std::string text(kMaxFileBytes, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    return parse(text, now_ms);
}

std::vector<RejoinEntry> FileRejoinStore::entries() { return read(clock_()); }

// All at once: the whole file is written next to the final one and then given its name (a replace), so that a program that dies in the middle of a write, or a second window that reads at that
// moment, never sees half a file. The file is the owner's alone from its first byte on: it is made with mode 0600 (and chmod'ed to it, whatever the umask says), and so is what it replaces.
bool FileRejoinStore::write(const std::vector<RejoinEntry>& entries) const {
    std::error_code ec;
    if (entries.empty()) {                                                // nothing left to keep: no file
        std::filesystem::remove(path_, ec);
        return !std::filesystem::exists(path_, ec);
    }
    const std::string text = serialise(entries);
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(std::random_device()()));
    const std::string tmp = path_ + "." + hex + ".tmp";
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = ::fchmod(fd, 0600) == 0;
    size_t done = 0;
    while (ok && done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
        } else {
            done += static_cast<size_t>(n);
        }
    }
    if (ok) ::fsync(fd);                                                  // (best effort: the bytes are in the system already; this is for a power cut)
    ::close(fd);
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
#else
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);       // (Windows: the per-user folder is private already)
        if (!out) return false;
        out << text;
        out.close();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
#endif
    if (before_rename_) before_rename_(tmp);
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool FileRejoinStore::put(const net::RejoinKey& key) {
    if (!rejoin_storable(key)) return false;
    const int64_t now = clock_();
    return write(with_key(read(now), key, now));
}

void FileRejoinStore::forget(const net::RejoinKey& key) {
    const int64_t now = clock_();
    const std::vector<RejoinEntry> before = read(now);
    const std::vector<RejoinEntry> after = without_key(before, key);
    if (after.size() != before.size()) write(after);                      // (a key that is not in the file changes nothing: the file is not touched)
}

// ---- the browser's local storage ------------------------------------------------------------------------------------------------------------------

std::optional<std::string> MapStorage::get(const std::string& key) {
    const auto it = items_.find(key);
    if (it == items_.end()) return std::nullopt;
    return it->second;
}

bool MapStorage::set(const std::string& key, const std::string& value) {
    items_[key] = value;
    return true;
}

void MapStorage::remove(const std::string& key) { items_.erase(key); }

std::vector<std::string> MapStorage::keys(const std::string& prefix) {
    std::vector<std::string> out;
    for (const auto& kv : items_) {
        if (kv.first.compare(0, prefix.size(), prefix) == 0) out.push_back(kv.first);
    }
    return out;
}

#if defined(__EMSCRIPTEN__)
namespace {

// The text goes into a JavaScript string literal in single quotes (the same as the settings of the web build, ConfigStore)
std::string js_literal(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

}  // namespace

std::optional<std::string> BrowserStorage::get(const std::string& key) {
    const std::string script = "(function(){try{var v=window.localStorage.getItem('" + js_literal(key) + "');return v===null?'N':'V'+v;}catch(e){return 'N';}})()";
    const char* text = emscripten_run_script_string(script.c_str());
    if (text == nullptr || text[0] != 'V') return std::nullopt;
    return std::string(text + 1);
}

bool BrowserStorage::set(const std::string& key, const std::string& value) {
    const std::string script = "(function(){try{window.localStorage.setItem('" + js_literal(key) + "','" + js_literal(value) + "');return 1;}catch(e){return 0;}})()";
    return emscripten_run_script_int(script.c_str()) == 1;
}

void BrowserStorage::remove(const std::string& key) {
    const std::string script = "try{window.localStorage.removeItem('" + js_literal(key) + "');}catch(e){}";
    emscripten_run_script(script.c_str());
}

std::vector<std::string> BrowserStorage::keys(const std::string& prefix) {
    const std::string script = "(function(){try{var out=[];for(var i=0;i<window.localStorage.length;i++){var k=window.localStorage.key(i);if(k&&k.indexOf('" + js_literal(prefix) +
                               "')===0)out.push(k);}return out.join('\\n');}catch(e){return '';}})()";
    const char* text = emscripten_run_script_string(script.c_str());
    std::vector<std::string> out;
    std::string all = text != nullptr ? text : "";
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos) end = all.size();
        if (end > pos) out.push_back(all.substr(pos, end - pos));
        pos = end + 1;
    }
    return out;
}
#endif

std::string LocalStorageRejoinStore::storage_key(const std::string& room, uint8_t seat) { return std::string(kKeyPrefix) + room + "." + std::to_string(static_cast<unsigned>(seat)); }

// {"k":"<hex>","s":"<server>","t":<epoch ms>}
std::string LocalStorageRejoinStore::value_of(const RejoinEntry& entry) {
    std::string server;
    for (const char c : entry.server) {
        if (c == '"' || c == '\\') server.push_back('\\');
        server.push_back(c);
    }
    return "{\"k\":\"" + rejoin_key_hex(entry.key) + "\",\"s\":\"" + server + "\",\"t\":" + std::to_string(entry.written_ms) + "}";
}

namespace {

struct JsonReader {
    const std::string& s;
    size_t i{0};

    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool take(char c) {
        skip();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool string(std::string& out) {
        skip();
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        out.clear();
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                if (!printable(c)) return false;
                out.push_back(c);
                continue;
            }
            if (i >= s.size()) return false;
            const char e = s[i++];
            char decoded = 0;
            switch (e) {
                case '"': decoded = '"'; break;
                case '\\': decoded = '\\'; break;
                case '/': decoded = '/'; break;
                case 'u': {                                               // \u00XX of a printable ASCII character only
                    if (i + 4 > s.size()) return false;
                    int v = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int d = hex_value(s[i + static_cast<size_t>(k)]);
                        if (d < 0) return false;
                        v = v * 16 + d;
                    }
                    i += 4;
                    if (v < 0x20 || v > 0x7E) return false;
                    decoded = static_cast<char>(v);
                    break;
                }
                default: return false;
            }
            out.push_back(decoded);
        }
        return false;
    }
    bool number(int64_t& out) {
        skip();
        const size_t from = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return digits_to_int(s.substr(from, i - from), 15, out);
    }
};

}  // namespace

bool LocalStorageRejoinStore::parse_value(const std::string& text, std::string& hex, std::string& server, int64_t& written_ms) {
    JsonReader r{text};
    if (!r.take('{')) return false;
    bool have_k = false;
    bool have_s = false;
    bool have_t = false;
    std::string k;
    std::string srv;
    int64_t t = 0;
    if (!r.take('}')) {
        for (;;) {
            std::string name;
            if (!r.string(name) || !r.take(':')) return false;
            if (name == "k") have_k = r.string(k);
            else if (name == "s") have_s = r.string(srv);
            else if (name == "t") have_t = r.number(t);
            else {                                                        // an unknown member must be a string or a number, and is ignored
                std::string ignored_text;
                int64_t ignored_number = 0;
                r.skip();
                if (!(r.i < text.size() && text[r.i] == '"' ? r.string(ignored_text) : r.number(ignored_number))) return false;
            }
            if ((name == "k" && !have_k) || (name == "s" && !have_s) || (name == "t" && !have_t)) return false;
            if (r.take(',')) continue;
            if (r.take('}')) break;
            return false;
        }
    }
    r.skip();
    if (r.i != text.size() || !have_k || !have_s || !have_t) return false;
    hex = k;
    server = srv;
    written_ms = t;
    return true;
}

bool LocalStorageRejoinStore::put(const net::RejoinKey& key) {
    if (!rejoin_storable(key)) return false;
    const RejoinEntry entry = entry_of(key, clock_());
    if (!storage_.set(storage_key(key.room, key.seat), value_of(entry))) return false;
    const std::vector<RejoinEntry> all = read_all();                      // (newest first: what is beyond the cap, the oldest, goes)
    for (size_t i = kRejoinMaxEntries; i < all.size(); ++i) storage_.remove(storage_key(all[i].room, all[i].seat));
    return true;
}

void LocalStorageRejoinStore::forget(const net::RejoinKey& key) {
    if (!storable_room(key.room) || key.seat >= 4) return;
    const std::string name = storage_key(key.room, key.seat);
    const std::optional<std::string> value = storage_.get(name);
    if (!value) return;
    std::string hex;
    std::string server;
    int64_t written = 0;
    net::SeatKey stored{};
    if (!parse_value(*value, hex, server, written) || !rejoin_key_from_hex(hex, stored)) return;
    if (net::key_matches(stored, key.key)) storage_.remove(name);          // (a newer key of the seat stays)
}

std::vector<RejoinEntry> LocalStorageRejoinStore::read_all() {
    const int64_t now = clock_();
    const size_t prefix = std::string(kKeyPrefix).size();
    std::vector<RejoinEntry> out;
    for (const std::string& name : storage_.keys(kKeyPrefix)) {
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos || dot < prefix || name.size() != dot + 2) continue;
        const std::string room = name.substr(prefix, dot - prefix);
        int64_t seat = 0;
        if (!storable_room(room) || !digits_to_int(name.substr(dot + 1), 1, seat) || seat > 3) continue;
        const std::optional<std::string> value = storage_.get(name);
        if (!value) continue;
        RejoinEntry e;
        std::string hex;
        if (!parse_value(*value, hex, e.server, e.written_ms) || !rejoin_key_from_hex(hex, e.key) || !printable_text(e.server, kMaxServerChars)) continue;
        if (now - e.written_ms > kRejoinMaxAgeMs) {                       // a day old: removed when read
            storage_.remove(name);
            continue;
        }
        if (!fresh_at(e.written_ms, now)) continue;
        e.room = room;
        e.seat = static_cast<uint8_t>(seat);
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const RejoinEntry& a, const RejoinEntry& b) { return a.written_ms > b.written_ms; });
    return out;
}

std::vector<RejoinEntry> LocalStorageRejoinStore::entries() {
    std::vector<RejoinEntry> out = read_all();
    if (out.size() > kRejoinMaxEntries) out.resize(kRejoinMaxEntries);
    return out;
}

}  // namespace ants::app
