#include "ants_server/replay_store.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"

namespace fs = std::filesystem;

namespace ants::server {

namespace {

constexpr int64_t kSecondsPerDay = 86400;
constexpr size_t kMaxStemChars = 24;
constexpr unsigned kMaxSameSecond = 9999;                // "-2" ... "-9999": the matches of one map that end in one second (parse_name reads at most four digits, which is this)
constexpr size_t kStampChars = 16;                       // "YYYYMMDD-HHMMSSZ"

int64_t floor_div(int64_t a, int64_t b) noexcept {
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

// The calendar (Howard Hinnant's days_from_civil and civil_from_days: the proleptic Gregorian calendar, days since 1970-01-01). A time is read and written without the C library's time
// functions, which are neither thread safe nor the same on every system.
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

std::string digits(int64_t value, size_t width) {
    std::string out = std::to_string(value);
    return out.size() >= width ? out : std::string(width - out.size(), '0') + out;
}

// "20261008-143209Z": the UTC time, a year outside 1970 .. 9999 held to it (a clock that is that wrong has more to worry about than a name)
std::string stamp_of(int64_t seconds) {
    int64_t days = floor_div(seconds, kSecondsPerDay);
    int64_t of_day = seconds - days * kSecondsPerDay;
    int64_t y = 0;
    unsigned m = 0;
    unsigned d = 0;
    civil_from_days(days, y, m, d);
    if (y < 1970) {
        y = 1970;
        m = 1;
        d = 1;
        of_day = 0;
    } else if (y > 9999) {
        y = 9999;
        m = 12;
        d = 31;
        of_day = kSecondsPerDay - 1;
    }
    return digits(y, 4) + digits(m, 2) + digits(d, 2) + "-" + digits(of_day / 3600, 2) + digits(of_day / 60 % 60, 2) + digits(of_day % 60, 2) + "Z";
}

bool all_digits(const std::string& text, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

int64_t number_of(const std::string& text, size_t from, size_t to) {
    int64_t v = 0;
    for (size_t i = from; i < to; ++i) v = v * 10 + (text[i] - '0');
    return v;
}

struct ParsedName {
    std::string stem;
    int64_t seconds{0};
    unsigned same_second{1};
};

// `ants-<stem>-YYYYMMDD-HHMMSSZ[-<n>].antsrep` and nothing else
bool parse_name(const std::string& name, ParsedName& out) {
    const std::string prefix = "ants-";
    const std::string ext = ReplayStore::kExtension;
    if (name.size() > ReplayStore::kMaxNameChars || name.size() < prefix.size() + 1 + 1 + kStampChars + ext.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0 || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) return false;
    std::string base = name.substr(0, name.size() - ext.size());
    unsigned same_second = 1;
    if (base.back() != 'Z') {                                              // "...Z-<n>"
        const size_t dash = base.rfind('-');
        if (dash == std::string::npos || dash + 1 >= base.size() || base.size() - dash - 1 > 4 || base[dash + 1] == '0' || !all_digits(base, dash + 1, base.size())) return false;
        same_second = static_cast<unsigned>(number_of(base, dash + 1, base.size()));
        if (same_second < 2) return false;                                 // (and at most 9999: four digits, as above)
        base.erase(dash);
        if (base.empty() || base.back() != 'Z') return false;
    }
    if (base.size() < prefix.size() + 1 + 1 + kStampChars) return false;
    const size_t at = base.size() - kStampChars;                           // the stamp "YYYYMMDD-HHMMSSZ"
    if (base[at - 1] != '-' || !all_digits(base, at, at + 8) || base[at + 8] != '-' || !all_digits(base, at + 9, at + 15)) return false;
    const std::string stem = base.substr(prefix.size(), at - 1 - prefix.size());
    if (stem.empty() || stem.size() > kMaxStemChars) return false;
    for (const char c : stem) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    const int64_t y = number_of(base, at, at + 4);
    const unsigned m = static_cast<unsigned>(number_of(base, at + 4, at + 6));
    const unsigned d = static_cast<unsigned>(number_of(base, at + 6, at + 8));
    const int64_t hh = number_of(base, at + 9, at + 11);
    const int64_t mm = number_of(base, at + 11, at + 13);
    const int64_t ss = number_of(base, at + 13, at + 15);
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 59) return false;
    int64_t y2 = 0;
    unsigned m2 = 0;
    unsigned d2 = 0;
    const int64_t days = days_from_civil(y, m, d);
    civil_from_days(days, y2, m2, d2);                                     // (a 31st of February comes back as the 3rd of March)
    if (y2 != y || m2 != m || d2 != d) return false;
    out.stem = stem;
    out.seconds = days * kSecondsPerDay + hh * 3600 + mm * 60 + ss;
    out.same_second = same_second;
    return true;
}

// "TREASURE.LVL" -> "TREASURE": the letters, digits and '_' of the map's name without its extension, at most kMaxStemChars of them
std::string stem_of(const std::string& map_name) {
    const size_t dot = map_name.rfind('.');
    const std::string without = dot == std::string::npos || dot == 0 ? map_name : map_name.substr(0, dot);
    std::string stem;
    for (const char c : without) {
        if (stem.size() >= kMaxStemChars) break;
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        stem.push_back(safe ? c : '_');
    }
    return stem.empty() ? std::string("match") : stem;
}

// Oldest first: the end time, then the order in which matches of one second were kept (the name's "-2" ...), then the name
bool older(const ReplayEntry& a, const ReplayEntry& b) {
    if (a.ended_s != b.ended_s) return a.ended_s < b.ended_s;
    if (a.sequence != b.sequence) return a.sequence < b.sequence;
    return a.file < b.file;
}

int64_t system_seconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

bool read_whole(const std::string& path, uint64_t expected, std::vector<uint8_t>& out) {
    out.clear();
    if (expected > replay::kMaxFileBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(static_cast<size_t>(expected));
    if (expected > 0) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(expected));
    char extra;
    if (!in || in.gcount() != static_cast<std::streamsize>(expected) || in.read(&extra, 1).gcount() != 0) {       // (the file must be exactly as long as the index says)
        out.clear();
        return false;
    }
    return true;
}

}  // namespace

bool ReplayStore::valid_file_name(const std::string& name) {
    ParsedName parsed;
    return parse_name(name, parsed);
}

std::string ReplayStore::map_stem(const std::string& map_name) { return stem_of(map_name); }

std::string ReplayStore::time_stamp(int64_t seconds) { return stamp_of(seconds); }

bool ReplayStore::time_of_name(const std::string& name, int64_t& seconds) {
    ParsedName parsed;
    if (!parse_name(name, parsed)) return false;
    seconds = parsed.seconds;
    return true;
}

ReplayStore::ReplayStore(ReplayConfig config) : cfg_(std::move(config)) {
    cfg_.keep_days = std::min<uint32_t>(std::max<uint32_t>(cfg_.keep_days, 1), 3650);
    cfg_.max_bytes = std::min<uint64_t>(std::max<uint64_t>(cfg_.max_bytes, 1024), 4096ull * 1024ull * 1024ull);
    if (!cfg_.clock_s) cfg_.clock_s = system_seconds;
}

int64_t ReplayStore::now_s() const { return cfg_.clock_s(); }

std::string ReplayStore::path_of(const std::string& file) const { return (fs::path(cfg_.dir) / file).string(); }

void ReplayStore::note(const std::string& line) {
    if (line == last_note_) {                                                    // (a disk that refuses every match says it once, then counts: the server's loop takes the notes at every pass)
        if (repeats_++ == 0) repeat_report_s_ = now_s() + kRepeatReportEveryS;
        return;
    }
    report_repeats();
    last_note_ = line;
    notes_.push_back(line);
}

void ReplayStore::report_repeats() {
    if (repeats_ == 0) return;
    notes_.push_back("replays: the last line was repeated " + std::to_string(repeats_) + " more time(s)");
    repeats_ = 0;                                                                // (last_note_ stays: the next repeat is counted too)
}

std::vector<std::string> ReplayStore::take_notes() {
    std::vector<std::string> out;
    out.swap(notes_);
    return out;
}

// What the store lists of a file: its head and its end (decode() checks everything in it). Only a whole file is a replay of ours: the server writes a file when the match is over.
bool ReplayStore::summarize(const std::vector<uint8_t>& bytes, ReplayEntry& out, std::string& why) const {
    replay::Replay rep;
    if (!replay::decode(bytes.data(), bytes.size(), rep, why)) return false;
    if (!rep.complete) {
        why = "the file has no end";
        return false;
    }
    out.map = rep.head.map_name;
    out.rules = rep.head.engine_rules;
    out.sim_rules = replay::sim_rules_of(rep.head);
    out.game = rep.head.game_version;
    out.turns = rep.total_turns;
    out.finished = rep.match_over;
    out.players.clear();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((rep.head.roster & (1u << seat)) != 0) out.players.push_back(replay::seat_label(rep.head, seat));
    }
    out.readable = true;
    return true;
}

// What the store knows of a file of its folder with a name of its own kind: the name, the size, and what the head and the end say when the file can be read
ReplayEntry ReplayStore::read_entry(const std::string& name, uint64_t size) const {
    ReplayEntry entry;
    ParsedName parsed;
    if (parse_name(name, parsed)) {
        entry.ended_s = parsed.seconds;
        entry.sequence = parsed.same_second;
    }
    entry.file = name;
    entry.bytes = size;
    std::vector<uint8_t> bytes;
    std::string reason;
    if (!read_whole(path_of(name), size, bytes) || !summarize(bytes, entry, reason)) entry.readable = false;
    return entry;
}

int64_t ReplayStore::folder_stamp() const {
    std::error_code ec;
    const auto stamp = fs::last_write_time(cfg_.dir, ec);
    return ec ? 0 : static_cast<int64_t>(stamp.time_since_epoch().count());
}

namespace {
// True when the file was written less than `settle_s` seconds ago (0: never): a write that is not finished yet, or one that may change again unseen.
bool changed_within(const fs::path& path, uint32_t settle_s) {
    if (settle_s == 0) return false;
    std::error_code ec;
    const auto when = fs::last_write_time(path, ec);
    return !ec && fs::file_time_type::clock::now() - when < std::chrono::seconds(settle_s);
}
}  // namespace

bool ReplayStore::prepare(std::string& why) {
    ready_ = false;
    entries_.clear();
    total_ = 0;
    readable_ = 0;
    if (cfg_.dir.empty()) {
        why = "no folder for the replays";
        return false;
    }
    std::error_code ec;
    fs::create_directories(cfg_.dir, ec);
    if (!fs::is_directory(cfg_.dir, ec)) {
        why = "the replays folder '" + cfg_.dir + "' cannot be made or is not a folder";
        return false;
    }
    size_t temps = 0;
    size_t unreadable = 0;
    folder_stamp_ = folder_stamp();                                                  // (before the folder is read: a file that arrives meanwhile moves it again)
    fs::directory_iterator it(cfg_.dir, ec);
    if (ec) {
        why = "the replays folder '" + cfg_.dir + "' cannot be read: " + ec.message();
        return false;
    }
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code entry_ec;
        const fs::file_status status = it->symlink_status(entry_ec);                // (a link or a folder is not a file of the store)
        if (entry_ec || status.type() != fs::file_type::regular) continue;
        const std::string name = it->path().filename().string();
        const std::string temp_ext = kTempExtension;
        if (name.size() > temp_ext.size() && name.compare(name.size() - temp_ext.size(), temp_ext.size(), temp_ext) == 0 && valid_file_name(name.substr(0, name.size() - temp_ext.size()))) {
            if (!changed_within(it->path(), cfg_.settle_s)) {                        // (one that changed a moment ago is the write of another program in this folder, the bot arena's, that is not done yet)
                fs::remove(it->path(), entry_ec);                                    // the file of a write that a crash interrupted
                ++temps;
            }
            continue;
        }
        ParsedName parsed;
        if (!parse_name(name, parsed)) continue;                                    // (not a name of ours: left alone, not counted)
        const uintmax_t size = it->file_size(entry_ec);
        if (entry_ec) continue;
        ReplayEntry entry = read_entry(name, size);
        if (entry.readable) ++readable_;
        else ++unreadable;
        total_ += entry.bytes;
        entries_.push_back(std::move(entry));
    }
    if (ec) {
        entries_.clear();
        total_ = 0;
        readable_ = 0;
        why = "the replays folder '" + cfg_.dir + "' cannot be read: " + ec.message();
        return false;
    }
    std::sort(entries_.begin(), entries_.end(), older);
    ++revision_;
    ready_ = true;
    next_purge_s_ = now_s() + kPurgeEveryS;
    next_rescan_s_ = now_s() + kRescanEveryS;
    std::string line = "replays in " + cfg_.dir + ": " + std::to_string(entries_.size()) + " file(s), " + std::to_string(total_ / 1024) + " KiB; kept " + std::to_string(cfg_.keep_days) + " days, at most " +
                       std::to_string(cfg_.max_bytes / (1024 * 1024)) + " MiB";
    if (temps > 0) line += "; " + std::to_string(temps) + " half-written file(s) of an interrupted write deleted";
    if (unreadable > 0) line += "; " + std::to_string(unreadable) + " file(s) that this build cannot read (they are deleted when they are old)";
    note(line);
    purge();
    return true;
}

bool ReplayStore::delete_file(ReplayEntry& entry, bool say) {
    std::error_code ec;
    if (cfg_.remove_file) cfg_.remove_file(path_of(entry.file), ec);
    else fs::remove(path_of(entry.file), ec);                                        // (false and no error: the file was gone already, which is what is wanted)
    if (ec) {
        if (say) note("replays: " + entry.file + " could not be deleted: " + ec.message());
        return false;
    }
    return true;
}

// The files that are older than keep_days go, then the oldest ones until `incoming` more bytes would fit under max_bytes. A file that cannot be deleted stays in the index (it is still on the disk)
// and is tried again at the next purge.
size_t ReplayStore::trim(uint64_t incoming) {
    size_t deleted = 0;
    size_t failed = 0;                                                               // (the first file that cannot be deleted is told, the others are counted: a volume that refuses every delete must not fill the log)
    const int64_t now = now_s();
    const int64_t limit = static_cast<int64_t>(cfg_.keep_days) * kSecondsPerDay;
    size_t kept = 0;                                                                 // (one pass, the files that stay move down: an erase from the front for every file would take as long as the index is long, every time)
    for (size_t i = 0; i < entries_.size(); ++i) {
        const bool expired = now - entries_[i].ended_s >= limit;
        const bool over = total_ + incoming > cfg_.max_bytes;
        if ((expired || over) && delete_file(entries_[i], failed == 0)) {
            total_ -= entries_[i].bytes;
            if (entries_[i].readable) --readable_;
            ++deleted;
            continue;
        }
        if (expired || over) ++failed;
        if (kept != i) entries_[kept] = std::move(entries_[i]);
        ++kept;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
    if (deleted > 0) ++revision_;
    if (failed > 1) note("replays: " + std::to_string(failed - 1) + " more file(s) could not be deleted either");
    return deleted;
}

size_t ReplayStore::purge() {
    if (!ready_) return 0;
    const size_t deleted = trim(0);
    if (deleted > 0) note("replays: " + std::to_string(deleted) + " file(s) deleted (older than " + std::to_string(cfg_.keep_days) + " days, or past the " + std::to_string(cfg_.max_bytes / (1024 * 1024)) + " MiB limit); " +
                          std::to_string(entries_.size()) + " kept, " + std::to_string(total_ / 1024) + " KiB");
    return deleted;
}

void ReplayStore::update() {
    if (!ready_) return;
    const int64_t now = now_s();
    if (repeats_ > 0 && (now >= repeat_report_s_ || now + kRepeatReportEveryS < repeat_report_s_)) report_repeats();     // (a clock that went back a long way: now)
    if (now >= next_purge_s_ || now + kPurgeEveryS < next_purge_s_) {              // (a clock that went back a long way is looked at again)
        purge();
        next_purge_s_ = now + kPurgeEveryS;
    }
    if (now >= next_rescan_s_ || now + kRescanEveryS < next_rescan_s_) {
        rescan();
        next_rescan_s_ = now + kRescanEveryS;
    }
}

// A file that somebody else put in the folder (the bot arena's --save-replays, a copy by hand) is listed too, and one that was taken away is not. The folder's change time is looked at (update(), every
// kRescanEveryS) and only a folder that changed is read again; a file that is known already is not read again, and at most kRescanBatch new ones are read at a time.
size_t ReplayStore::rescan() {
    if (!ready_) return 0;
    const int64_t stamp = folder_stamp();
    if (stamp == folder_stamp_ && !rescan_more_) return 0;
    rescan_more_ = false;
    std::error_code ec;
    const auto fs_now = fs::file_time_type::clock::now();
    const auto settle = std::chrono::seconds(cfg_.settle_s);
    const auto young = [&](const fs::path& path) {                                  // (changed a moment ago: it may still be growing, or change again unseen)
        if (cfg_.settle_s == 0) return false;
        std::error_code stat_ec;
        const auto when = fs::last_write_time(path, stat_ec);
        return !stat_ec && fs_now - when < settle;
    };
    std::set<std::string> present;                                                   // the names of the store's own kind that are regular files now
    std::map<std::string, uintmax_t> sizes;
    fs::directory_iterator it(cfg_.dir, ec);
    if (ec) return 0;                                                                // (nothing is remembered of a look that did not end: the next one starts again)
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) return 0;
        std::error_code entry_ec;
        const fs::file_status status = it->symlink_status(entry_ec);
        if (entry_ec || status.type() != fs::file_type::regular) continue;
        const std::string name = it->path().filename().string();
        ParsedName parsed;
        if (!parse_name(name, parsed)) continue;                                     // (a half-written ".tmp" of another writer is not ours to delete or to list)
        const uintmax_t size = it->file_size(entry_ec);
        if (entry_ec) continue;
        present.insert(name);
        sizes[name] = size;
    }
    folder_stamp_ = stamp;
    if (cfg_.settle_s != 0 && young(cfg_.dir)) rescan_more_ = true;                  // (the folder changed within the grain of its time: look again)
    size_t changed = 0;
    size_t kept = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (present.count(entries_[i].file) == 0) {                                  // (taken away: forgotten, nothing is deleted)
            total_ -= entries_[i].bytes;
            if (entries_[i].readable) --readable_;
            ++changed;
            continue;
        }
        if (kept != i) entries_[kept] = std::move(entries_[i]);
        ++kept;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
    size_t added = 0;
    for (const std::string& name : present) {
        const ReplayEntry* known = find(name);
        // a known file is read again only when it cannot have been the whole file: it could not be read, or its size is not the one that was indexed (a copy that was still growing when it was found)
        const bool again = known != nullptr && (!known->readable || known->bytes != sizes[name]);
        if (known != nullptr && !again) continue;
        if (young(path_of(name))) {
            rescan_more_ = true;                                                     // (it may still be growing: the next look)
            continue;
        }
        if (added >= kRescanBatch) {
            rescan_more_ = true;                                                     // (the rest at the next look)
            break;
        }
        ReplayEntry entry = read_entry(name, sizes[name]);
        if (again && entry.readable == known->readable && entry.bytes == known->bytes) continue;      // (still the damaged file that it was: nothing changed)
        if (again) {
            const size_t at = static_cast<size_t>(known - entries_.data());
            total_ -= entries_[at].bytes;
            if (entries_[at].readable) --readable_;
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(at));
        }
        total_ += entry.bytes;
        if (entry.readable) ++readable_;
        entries_.insert(std::upper_bound(entries_.begin(), entries_.end(), entry, older), std::move(entry));
        ++added;
    }
    if (added > 0 || changed > 0) {
        ++revision_;
        note("replays: " + std::to_string(added) + " file(s) found in " + cfg_.dir + " that the store did not know" + (changed > 0 ? ", " + std::to_string(changed) + " that are not there any more" : std::string()) + "; " +
             std::to_string(entries_.size()) + " listed, " + std::to_string(total_ / 1024) + " KiB");
        purge();                                                                     // (the limits hold for files that came from outside too: a file older than keep_days that is put here is deleted again)
    }
    return added + changed;
}

ReplaySave ReplayStore::save(const std::vector<uint8_t>& bytes) {
    ReplaySave result;
    const auto refuse = [&](const std::string& why) {
        result.kept = false;
        result.note = why;
        note("replays: a match was not kept: " + why);
        return result;
    };
    if (!ready_) {
        result.note = "this server keeps no replays";
        return result;
    }
    ReplayEntry entry;
    std::string why;
    if (bytes.empty() || bytes.size() > replay::kMaxFileBytes || !summarize(bytes, entry, why)) return refuse("the recording is not a replay file that can be kept" + (why.empty() ? std::string() : " (" + why + ")"));
    const int64_t now = now_s();
    saved_at_.erase(std::remove_if(saved_at_.begin(), saved_at_.end(), [now](int64_t t) { return now - t >= 3600 || t > now; }), saved_at_.end());
    if (saved_at_.size() >= cfg_.max_saves_per_hour) return refuse("the limit of " + std::to_string(cfg_.max_saves_per_hour) + " kept matches an hour is reached");
    if (bytes.size() > cfg_.max_bytes) return refuse("the match is bigger than the whole store may be");
    // The limits come before the disk: the files that go make room for the new one, which also helps a disk that is nearly full. A match that the disk then refuses has cost at most the old files that
    // the size limit deletes for it (never more than one pass of trim(): the next match finds the room already made).
    trim(bytes.size());
    if (total_ + bytes.size() > cfg_.max_bytes) return refuse("the older replays could not be deleted to make room");
    if (cfg_.min_free_bytes > 0) {
        uint64_t available = 0;
        bool known = false;
        if (cfg_.free_bytes) {
            known = cfg_.free_bytes(cfg_.dir, available);
        } else {
            std::error_code ec;
            const fs::space_info space = fs::space(cfg_.dir, ec);
            known = !ec;
            available = static_cast<uint64_t>(space.available);
        }
        if (known && available < cfg_.min_free_bytes + bytes.size()) return refuse("the disk has less than " + std::to_string(cfg_.min_free_bytes / (1024 * 1024)) + " MiB free");
    }
    // The name: the map and the time the match ended, a "-2", "-3" ... when that is taken (by a file of the store or by anything else in the folder: nothing is replaced)
    const std::string base = "ants-" + stem_of(entry.map) + "-" + stamp_of(now);
    std::string name = base + kExtension;
    std::error_code ec;
    unsigned sequence = 1;
    while (sequence <= kMaxSameSecond && (find(name) != nullptr || fs::exists(path_of(name), ec))) {
        ++sequence;
        name = base + "-" + std::to_string(sequence) + kExtension;
    }
    if (sequence > kMaxSameSecond) return refuse("every name for a match of this second is taken");
    // Written whole to a temporary name, then renamed: a crash leaves a whole file or none
    const std::string temp = path_of(name) + kTempExtension;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            return refuse("the disk refused the write");
        }
    }
    if (cfg_.rename_file) cfg_.rename_file(temp, path_of(name), ec);
    else fs::rename(temp, path_of(name), ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return refuse("the file could not be given its name: " + ec.message());
    }
    entry.file = name;
    entry.bytes = bytes.size();
    ParsedName parsed;
    entry.ended_s = parse_name(name, parsed) ? parsed.seconds : now;             // (the time of the name, as it will be read again: a clock that is far off is held to the calendar)
    entry.sequence = sequence;
    total_ += entry.bytes;
    ++readable_;                                                                 // (summarize() accepted it)
    entries_.insert(std::upper_bound(entries_.begin(), entries_.end(), entry, older), entry);
    ++revision_;
    saved_at_.push_back(now);
    result.kept = true;
    result.file = name;
    result.bytes = entry.bytes;
    return result;
}

std::vector<ReplayEntry> ReplayStore::list() const { return std::vector<ReplayEntry>(entries_.rbegin(), entries_.rend()); }

// The index is in the order of older(), which a name carries all of (its time, its "-n" and itself): a name is found by a bisection, however many files there are
const ReplayEntry* ReplayStore::find(const std::string& file) const {
    ParsedName parsed;
    if (!parse_name(file, parsed)) return nullptr;                               // (a name that the store could not have made is not in the index)
    ReplayEntry key;
    key.file = file;
    key.ended_s = parsed.seconds;
    key.sequence = parsed.same_second;
    const auto at = std::lower_bound(entries_.begin(), entries_.end(), key, older);
    return at != entries_.end() && at->file == file ? &*at : nullptr;
}

bool ReplayStore::read(const std::string& file, std::vector<uint8_t>& out) const {
    out.clear();
    const ReplayEntry* entry = find(file);
    return entry != nullptr && read_whole(path_of(entry->file), entry->bytes, out);
}

bool ReplayStore::remove(const std::string& file) {
    const ReplayEntry* found = find(file);
    if (found == nullptr) return false;
    const size_t i = static_cast<size_t>(found - entries_.data());
    if (!delete_file(entries_[i])) return false;
    total_ -= entries_[i].bytes;
    if (entries_[i].readable) --readable_;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    ++revision_;
    return true;
}

}  // namespace ants::server
