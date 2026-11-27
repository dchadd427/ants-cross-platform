#include "ants_server/replay_store.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <unordered_set>

#include "ants_ai/bot_names.hpp"
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
    if (expected > replay::kMaxFileBytesWithMap) return false;           // (decode() holds the match to kMaxFileBytes without its map)
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

// The look at the folder that is under way (rescan(), look_step() below): what it has listed and what it has still to read
struct ReplayStore::Look {
    fs::directory_iterator it;
    int64_t stamp{0};                                                                // the folder's change time when the look began (a file that arrives meanwhile moves it again)
    bool listing{true};                                                              // the folder is being listed (false: the files in `todo` are being read)
    std::unordered_set<std::string> seen;                                            // the names of the store's own kind that are regular files in the folder
    std::vector<std::pair<std::string, uintmax_t>> todo;                             // the ones to read: not known, or known and not what was indexed
    size_t next{0};                                                                  // the next of `todo`
    size_t fresh{0};                                                                 // how many of `todo` count against kRescanBatch
    bool more{false};                                                                // the batch was full: the next look goes on even if the folder did not change
    std::set<std::string> own;                                                       // what the store itself kept or deleted while the look ran: the look has nothing to say about it
    size_t added{0};
    size_t changed{0};
};

ReplayStore::ReplayStore(ReplayConfig config) : cfg_(std::move(config)) {
    cfg_.keep_days = std::min<uint32_t>(std::max<uint32_t>(cfg_.keep_days, 1), 3650);
    cfg_.max_bytes = std::min<uint64_t>(std::max<uint64_t>(cfg_.max_bytes, 1024), 4096ull * 1024ull * 1024ull);
    cfg_.bot_share_percent = std::min<uint32_t>(std::max<uint32_t>(cfg_.bot_share_percent, 1), 100);
    cfg_.max_bot_saves_per_hour = std::min(cfg_.max_bot_saves_per_hour, cfg_.max_saves_per_hour);          // (they count among the hour's saves)
    if (!cfg_.clock_s) cfg_.clock_s = system_seconds;
}

ReplayStore::~ReplayStore() = default;

uint64_t ReplayStore::bot_cap_bytes() const noexcept { return cfg_.max_bytes / 100 * cfg_.bot_share_percent + cfg_.max_bytes % 100 * cfg_.bot_share_percent / 100; }

void ReplayStore::count_in(const ReplayEntry& e) {
    total_ += e.bytes;
    if (e.readable) ++readable_;
    if (e.pending) ++unindexed_;
    if (e.bots_only) {
        bot_total_ += e.bytes;
        ++bot_count_;
    }
}

void ReplayStore::count_out(const ReplayEntry& e) {
    total_ -= e.bytes;
    if (e.readable) --readable_;
    if (e.pending) --unindexed_;
    if (e.bots_only) {
        bot_total_ -= e.bytes;
        --bot_count_;
    }
}

bool ReplayStore::ask_free(uint64_t& available) const {
    if (cfg_.free_bytes) return cfg_.free_bytes(cfg_.dir, available);
    std::error_code ec;
    const fs::space_info space = fs::space(cfg_.dir, ec);
    available = static_cast<uint64_t>(space.available);
    return !ec;
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
    out.mode = rep.head.mode;
    out.turns = rep.total_turns;
    out.finished = rep.match_over;
    out.players.clear();
    bool all_bots = rep.head.roster != 0;                                    // (a name that a person cannot take is a computer player's: ai::is_reserved_bot_name)
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((rep.head.roster & (1u << seat)) == 0) continue;
        out.players.push_back(replay::seat_label(rep.head, seat));
        if (!ai::is_reserved_bot_name(rep.head.names[seat])) all_bots = false;
    }
    out.bots_only = all_bots;
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

int64_t ReplayStore::clock_us() const {
    if (cfg_.clock_us) return cfg_.clock_us();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The store itself wrote or deleted `file`: its list has the change already, so the look at the folder must not run for it. The folder's change time moved with the write; when nothing else had moved it since the last
// look (`stamp_before`, read just before the write, is the one that was looked at), the new time is the looked-at one. When something else had (a file of the bot arena that came meanwhile), it is left as it is and the
// next look finds that file. A look that is under way knows nothing of the file (its listing may have passed the place of it already): it is told to leave it alone.
void ReplayStore::own_change(const std::string& file, int64_t stamp_before) {
    const bool listing = look_ != nullptr && look_->listing;                      // (once the listing is over, folder_stamp_ holds the time that it began at)
    if (look_ != nullptr) look_->own.insert(file);
    int64_t& looked_at = listing ? look_->stamp : folder_stamp_;
    if (stamp_before == looked_at) looked_at = folder_stamp();
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
    index_queue_.clear();
    saved_at_.clear();
    bot_saved_at_.clear();
    total_ = 0;
    readable_ = 0;
    bot_total_ = 0;
    bot_count_ = 0;
    unindexed_ = 0;
    read_since_revision_ = 0;
    look_.reset();
    looks_ = 0;
    if (cfg_.dir.empty()) {
        why = "no folder for the replays";
        return false;
    }
    std::error_code ec;
    if (!cfg_.read_only) fs::create_directories(cfg_.dir, ec);
    if (!fs::is_directory(cfg_.dir, ec)) {
        why = cfg_.read_only ? "the replays folder '" + cfg_.dir + "' is not there or is not a folder" : "the replays folder '" + cfg_.dir + "' cannot be made or is not a folder";
        return false;
    }
    size_t temps = 0;
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
            if (!cfg_.read_only && !changed_within(it->path(), cfg_.settle_s)) {     // (one that changed a moment ago is the write of another program in this folder, the bot arena's, that is not done yet)
                fs::remove(it->path(), entry_ec);                                    // the file of a write that a crash interrupted
                ++temps;
            }
            continue;
        }
        ParsedName parsed;
        if (!parse_name(name, parsed)) continue;                                    // (not a name of ours: left alone, not counted)
        const uintmax_t size = it->file_size(entry_ec);
        if (entry_ec) continue;
        if (cfg_.index_in_background) {                                              // (the name says when it ended; what is in it is read later, a few files at a time)
            ReplayEntry entry;
            entry.file = name;
            entry.bytes = size;
            entry.ended_s = parsed.seconds;
            entry.sequence = parsed.same_second;
            entry.pending = true;
            entries_.push_back(std::move(entry));
        } else {
            entries_.push_back(read_entry(name, size));
        }
    }
    if (ec) {
        entries_.clear();
        why = "the replays folder '" + cfg_.dir + "' cannot be read: " + ec.message();
        return false;
    }
    std::sort(entries_.begin(), entries_.end(), older);
    size_t unreadable = 0;
    for (const ReplayEntry& e : entries_) {
        count_in(e);
        if (e.pending) index_queue_.push_back(e.file);                               // (oldest first: the newest are taken from the back)
        else if (!e.readable) ++unreadable;
    }
    ++revision_;
    ready_ = true;
    next_purge_s_ = now_s() + kPurgeEveryS;
    next_rescan_s_ = now_s() + kRescanEveryS;
    next_safety_s_ = now_s() + kRescanSafetyEveryS;
    std::string line = "replays in " + cfg_.dir + ": " + std::to_string(entries_.size()) + " file(s), " + std::to_string(total_ / 1024) + " KiB; kept " + std::to_string(cfg_.keep_days) + " days, at most " +
                       std::to_string(cfg_.max_bytes / (1024 * 1024)) + " MiB";
    if (temps > 0) line += "; " + std::to_string(temps) + " half-written file(s) of an interrupted write deleted";
    if (unreadable > 0) line += "; " + std::to_string(unreadable) + " file(s) that this build cannot read (they are deleted when they are old)";
    if (unindexed_ > 0) line += "; the heads of " + std::to_string(unindexed_) + " file(s) are read in the background, the newest first";
    note(line);
    purge();
    report_headroom();
    return true;
}

// The room that the disk leaves the store, less the free space that it keeps: said once an hour when it is less than the room that the size limit still gives (a volume smaller than the limit asks for)
void ReplayStore::report_headroom() {
    uint64_t available = 0;
    if (cfg_.read_only || cfg_.min_free_bytes == 0 || !ask_free(available)) return;
    const uint64_t room = available > cfg_.min_free_bytes ? available - cfg_.min_free_bytes : 0;
    const uint64_t left_under_limit = cfg_.max_bytes > total_ ? cfg_.max_bytes - total_ : 0;
    if (room >= left_under_limit) return;
    note("replays: the volume has " + std::to_string(available / (1024 * 1024)) + " MiB free and the store keeps " + std::to_string(cfg_.min_free_bytes / (1024 * 1024)) + " MiB of it free, so the store can grow by " +
         std::to_string(room / (1024 * 1024)) + " MiB, not up to its limit of " + std::to_string(cfg_.max_bytes / (1024 * 1024)) + " MiB (it holds " + std::to_string(total_ / (1024 * 1024)) +
         " MiB): when the disk is short, the files of computer players go first, then the oldest of the others; a bigger volume or a smaller limit ends this line");
}

bool ReplayStore::delete_file(ReplayEntry& entry, bool say) {
    const int64_t stamp_before = folder_stamp();
    std::error_code ec;
    if (cfg_.remove_file) cfg_.remove_file(path_of(entry.file), ec);
    else fs::remove(path_of(entry.file), ec);                                        // (false and no error: the file was gone already, which is what is wanted)
    if (ec) {
        if (say) note("replays: " + entry.file + " could not be deleted: " + ec.message());
        return false;
    }
    own_change(entry.file, stamp_before);
    return true;
}

// One pass over the files of bots (or of people), the oldest first, deleting each one while `wanted()` says that more has to go. A file that cannot be deleted stays in the index (it is still on the disk) and is tried
// again at the next purge. One pass, and the files that stay move down: an erase from the front for every file would take as long as the index is long, every time.
size_t ReplayStore::evict(bool bots, const std::function<bool()>& wanted, size_t& failed) {
    size_t deleted = 0;
    size_t kept = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].bots_only == bots && wanted()) {
            if (delete_file(entries_[i], failed == 0)) {
                count_out(entries_[i]);
                ++deleted;
                continue;
            }
            ++failed;                                                                // (the first file that cannot be deleted is told, the others are counted: a volume that refuses every delete must not fill the log)
        }
        if (kept != i) entries_[kept] = std::move(entries_[i]);
        ++kept;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
    return deleted;
}

// What goes, in this order: the files that are older than keep_days (all of them); then the files of bots, the oldest first, while the bots' share is passed (with the file that comes, when it is a bots' match), the store is
// over max_bytes or the disk still needs `free_needed` bytes; then, only when the file that comes is a person's, the files of people, the oldest first, while the store is over max_bytes or the disk still needs the bytes. A bots'
// match therefore never deletes a person's file.
size_t ReplayStore::trim(uint64_t incoming, bool incoming_bot, uint64_t free_needed) {
    if (cfg_.read_only) return 0;                                                    // (the files belong to the program that keeps them)
    size_t failed = 0;
    const uint64_t total_at_start = total_;
    const int64_t now = now_s();
    const int64_t limit = static_cast<int64_t>(cfg_.keep_days) * kSecondsPerDay;
    size_t deleted = 0;
    size_t kept = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const bool expired = now - entries_[i].ended_s >= limit;
        if (expired && delete_file(entries_[i], failed == 0)) {
            count_out(entries_[i]);
            ++deleted;
            continue;
        }
        if (expired) ++failed;
        if (kept != i) entries_[kept] = std::move(entries_[i]);
        ++kept;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
    const uint64_t bot_incoming = incoming_bot ? incoming : 0;
    const uint64_t bot_cap = bot_cap_bytes();
    const auto over_total = [&]() { return total_ + incoming > cfg_.max_bytes; };
    const auto over_bots = [&]() { return bot_total_ + bot_incoming > bot_cap; };
    const auto disk_short = [&]() { return total_at_start - total_ < free_needed; };          // (what the call has deleted so far is what the disk got back)
    if (over_bots() || over_total() || disk_short()) deleted += evict(true, [&]() { return over_bots() || over_total() || disk_short(); }, failed);
    if (!incoming_bot && (over_total() || disk_short())) deleted += evict(false, [&]() { return over_total() || disk_short(); }, failed);
    if (deleted > 0) ++revision_;
    if (failed > 1) note("replays: " + std::to_string(failed - 1) + " more file(s) could not be deleted either");
    return deleted;
}

size_t ReplayStore::purge() {
    if (!ready_) return 0;
    const size_t deleted = trim(0, false);
    if (deleted > 0) note("replays: " + std::to_string(deleted) + " file(s) deleted (older than " + std::to_string(cfg_.keep_days) + " days, or past the " + std::to_string(cfg_.max_bytes / (1024 * 1024)) + " MiB limit, or past the " +
                          std::to_string(bot_cap_bytes() / (1024 * 1024)) + " MiB that matches of computer players may take); " + std::to_string(entries_.size()) + " kept, " + std::to_string(total_ / 1024) + " KiB");
    return deleted;
}

void ReplayStore::update() {
    if (!ready_) return;
    const int64_t now = now_s();
    if (repeats_ > 0 && (now >= repeat_report_s_ || now + kRepeatReportEveryS < repeat_report_s_)) report_repeats();     // (a clock that went back a long way: now)
    if (now >= next_purge_s_ || now + kPurgeEveryS < next_purge_s_) {              // (a clock that went back a long way is looked at again)
        purge();
        report_headroom();
        next_purge_s_ = now + kPurgeEveryS;
    }
    if (look_ != nullptr) {
        look_step(kRescanBudgetUs);                                                  // (a look that is under way goes on by one slice in each pass)
    } else if (now >= next_rescan_s_ || now + kRescanEveryS < next_rescan_s_) {
        const bool safety = now >= next_safety_s_ || now + kRescanSafetyEveryS < next_safety_s_;
        if (safety) next_safety_s_ = now + kRescanSafetyEveryS;
        next_rescan_s_ = now + kRescanEveryS;
        if (look_begin(safety)) look_step(kRescanBudgetUs);
    }
    if (unindexed_ > 0) index_step();
}

// Reads the head of a file that was only listed. The name and the size are what the listing knew; the rest comes from the file.
bool ReplayStore::index_entry(ReplayEntry& entry) const {
    if (!entry.pending) return false;
    ReplayEntry read = read_entry(entry.file, entry.bytes);
    entry.pending = false;
    --unindexed_;
    entry.readable = read.readable;
    if (read.readable) {
        entry.map = std::move(read.map);
        entry.rules = read.rules;
        entry.sim_rules = read.sim_rules;
        entry.game = std::move(read.game);
        entry.mode = read.mode;
        entry.turns = read.turns;
        entry.finished = read.finished;
        entry.players = std::move(read.players);
        entry.bots_only = read.bots_only;
        ++readable_;
        if (entry.bots_only) {
            bot_total_ += entry.bytes;
            ++bot_count_;
        }
    }
    if (++read_since_revision_ >= kIndexRevisionFiles || unindexed_ == 0) {          // (a reader that works through the index looks again when this moves: not at every file)
        read_since_revision_ = 0;
        ++revision_;
    }
    return true;
}

size_t ReplayStore::index_step() {
    if (!ready_ || unindexed_ == 0) return 0;
    const auto started = std::chrono::steady_clock::now();
    size_t read = 0;
    while (!index_queue_.empty() && read < kIndexBatch) {
        const std::string name = std::move(index_queue_.back());
        index_queue_.pop_back();
        ReplayEntry* entry = raw_find(name);
        if (entry != nullptr && index_entry(*entry)) ++read;
        if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count() >= kIndexBudgetUs) break;
    }
    if (index_queue_.empty() && unindexed_ != 0) {                                    // (a file that went from under the queue: nothing is left to read)
        unindexed_ = 0;
        for (const ReplayEntry& e : entries_) {
            if (e.pending) ++unindexed_;
        }
    }
    return read;
}

// A file that somebody else put in the folder (the bot arena's --save-replays, a copy by hand) is listed too, and one that was taken away is not. The folder's change time is looked at (update(), every
// kRescanEveryS) and only a folder that changed is read again (the store's own writes and deletes do not count: own_change); a file that is known is not read again, and at most kRescanBatch new ones are read at a
// time. A look is made in slices (look_step): it lists the folder, then reads the new files, for about kRescanBudgetUs in each pass of the server's loop; what it forgets and finds it applies to the list as it goes
// (a file that the store itself keeps or deletes meanwhile is left alone: own_change).

namespace {
// A known file is read again only when it cannot have been the whole file: it could not be read, or its size is not the one that was indexed (a copy that was still growing when it was found). One that waits for
// its turn in index_step() is not read here.
bool needs_read(const ReplayEntry* known, uint64_t size) {
    if (known == nullptr) return true;
    if (known->pending && known->bytes == size) return false;
    return !known->readable || known->bytes != size;
}
}  // namespace

bool ReplayStore::look_begin(bool force) {
    if (!ready_ || look_ != nullptr) return false;
    const int64_t stamp = folder_stamp();
    if (!force && stamp == folder_stamp_ && !rescan_more_) return false;
    std::error_code ec;
    auto look = std::make_unique<Look>();
    look->it = fs::directory_iterator(cfg_.dir, ec);
    if (ec) return false;                                                            // (nothing is remembered of a look that did not end: the next one starts again)
    rescan_more_ = false;
    look->stamp = stamp;
    look_ = std::move(look);
    ++looks_;
    return true;
}

void ReplayStore::look_step(int64_t budget_us) {
    if (look_ == nullptr) return;
    Look& look = *look_;
    const int64_t began = clock_us();
    const auto spent = [&]() { return budget_us >= 0 && clock_us() - began >= budget_us; };
    std::error_code ec;
    if (look.listing) {
        const auto fs_now = fs::file_time_type::clock::now();
        const auto settle = std::chrono::seconds(cfg_.settle_s);
        const auto young = [&](const fs::path& path) {                              // (changed a moment ago: it may still be growing, or change again unseen)
            if (cfg_.settle_s == 0) return false;
            std::error_code stat_ec;
            const auto when = fs::last_write_time(path, stat_ec);
            return !stat_ec && fs_now - when < settle;
        };
        const auto list_one = [&](const fs::directory_entry& entry) {
            const std::string name = entry.path().filename().string();
            ParsedName parsed;
            if (!parse_name(name, parsed)) return;                                   // (a half-written ".tmp" of another writer is not ours to delete or to list)
            std::error_code entry_ec;
            const fs::file_status status = entry.symlink_status(entry_ec);
            if (entry_ec || status.type() != fs::file_type::regular) return;
            const uintmax_t size = entry.file_size(entry_ec);
            if (entry_ec) return;
            look.seen.insert(name);
            const ReplayEntry* known = raw_find(name);
            if (!needs_read(known, size)) return;
            if (young(path_of(name))) {
                rescan_more_ = true;                                                 // (it may still be growing: the next look)
            } else if (known != nullptr && known->bytes == size) {
                look.todo.emplace_back(name, size);                                  // (a damaged file of the same size: it may have been replaced by a good one of its size; it does not count against the batch)
            } else if (look.fresh >= kRescanBatch) {
                look.more = true;                                                    // (the rest at the next look)
            } else {
                ++look.fresh;
                look.todo.emplace_back(name, size);
            }
        };
        for (const fs::directory_iterator end; look.it != end;) {
            list_one(*look.it);
            look.it.increment(ec);
            if (ec) {
                look_.reset();                                                       // (a look that did not end: the next one starts again)
                return;
            }
            if (spent()) return;                                                     // (the next slice goes on from here)
        }
        look.listing = false;
        folder_stamp_ = look.stamp;
        if (cfg_.settle_s != 0 && changed_within(cfg_.dir, cfg_.settle_s)) rescan_more_ = true;      // (the folder changed within the grain of its time: look again)
        size_t kept = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (look.seen.count(entries_[i].file) == 0 && look.own.count(entries_[i].file) == 0) {      // (taken away: forgotten, nothing is deleted)
                count_out(entries_[i]);
                ++look.changed;
                continue;
            }
            if (kept != i) entries_[kept] = std::move(entries_[i]);
            ++kept;
        }
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(kept), entries_.end());
        look.seen.clear();
        if (spent()) return;
    }
    while (look.next < look.todo.size()) {
        const std::string name = look.todo[look.next].first;
        const uintmax_t size = look.todo[look.next].second;
        ++look.next;
        if (look.own.count(name) != 0) continue;
        const ReplayEntry* known = raw_find(name);
        if (!needs_read(known, size)) continue;                                      // (something read it meanwhile)
        ReplayEntry entry = read_entry(name, size);
        if (known != nullptr && entry.readable == known->readable && entry.bytes == known->bytes) {
            if (spent()) return;
            continue;                                                                // (still the damaged file that it was: nothing changed)
        }
        if (known != nullptr) {
            const size_t at = static_cast<size_t>(known - entries_.data());
            count_out(entries_[at]);
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(at));
        }
        count_in(entry);
        entries_.insert(std::upper_bound(entries_.begin(), entries_.end(), entry, older), std::move(entry));
        ++look.added;
        if (spent()) return;
    }
    look_end();
}

void ReplayStore::look_end() {
    if (look_->more) rescan_more_ = true;
    const size_t added = look_->added;
    const size_t changed = look_->changed;
    look_.reset();
    last_look_ = added + changed;
    if (added > 0 || changed > 0) {
        ++revision_;
        note("replays: " + std::to_string(added) + " file(s) found in " + cfg_.dir + " that the store did not know" + (changed > 0 ? ", " + std::to_string(changed) + " that are not there any more" : std::string()) + "; " +
             std::to_string(entries_.size()) + " listed, " + std::to_string(total_ / 1024) + " KiB");
        purge();                                                                     // (the limits hold for files that came from outside too: a file older than keep_days that is put here is deleted again)
    }
}

size_t ReplayStore::rescan() {
    if (!ready_) return 0;
    if (look_ == nullptr && !look_begin(false)) return 0;
    last_look_ = 0;
    while (look_ != nullptr) look_step(-1);
    return last_look_;
}

size_t ReplayStore::bot_saves_left() const {
    if (!ready_ || cfg_.read_only) return 0;
    const int64_t now = now_s();
    const auto recent = [now](const std::vector<int64_t>& times) {
        return static_cast<size_t>(std::count_if(times.begin(), times.end(), [now](int64_t t) { return now - t < 3600 && t <= now; }));
    };
    const size_t all = recent(saved_at_), bots = recent(bot_saved_at_);
    const size_t by_bots = cfg_.max_bot_saves_per_hour > bots ? cfg_.max_bot_saves_per_hour - bots : 0;
    const size_t by_hour = cfg_.max_saves_per_hour > all ? cfg_.max_saves_per_hour - all : 0;
    return std::min(by_bots, by_hour);
}

ReplaySave ReplayStore::save(const std::vector<uint8_t>& bytes) {
    ReplaySave result;
    const auto refuse = [&](const std::string& why) {
        result.kept = false;
        result.note = why;
        note("replays: a match was not kept: " + why);
        return result;
    };
    if (!ready_ || cfg_.read_only) {
        result.note = cfg_.read_only ? "this store only reads the folder" : "this server keeps no replays";
        return result;
    }
    ReplayEntry entry;
    std::string why;
    if (bytes.empty() || bytes.size() > replay::kMaxFileBytesWithMap || !summarize(bytes, entry, why)) return refuse("the recording is not a replay file that can be kept" + (why.empty() ? std::string() : " (" + why + ")"));
    const bool bot = entry.bots_only;
    const int64_t now = now_s();
    const auto expired = [now](int64_t t) { return now - t >= 3600 || t > now; };
    saved_at_.erase(std::remove_if(saved_at_.begin(), saved_at_.end(), expired), saved_at_.end());
    bot_saved_at_.erase(std::remove_if(bot_saved_at_.begin(), bot_saved_at_.end(), expired), bot_saved_at_.end());
    if (saved_at_.size() >= cfg_.max_saves_per_hour) return refuse("the limit of " + std::to_string(cfg_.max_saves_per_hour) + " kept matches an hour is reached");
    if (bot && bot_saved_at_.size() >= cfg_.max_bot_saves_per_hour) return refuse("the limit of " + std::to_string(cfg_.max_bot_saves_per_hour) + " kept matches of computer players an hour is reached");
    if (bytes.size() > cfg_.max_bytes) return refuse("the match is bigger than the whole store may be");
    if (bot && bytes.size() > bot_cap_bytes()) return refuse("the match is bigger than the share of the store for matches of computer players");
    if (bot && total_ - bot_total_ + bytes.size() > cfg_.max_bytes) return refuse("the store is full of people's matches, and a match of computer players never takes a place from them");
    // The limits come before the disk: the files that go make room for the new one, which also helps a disk that is nearly full. A match that the disk then refuses has cost at most the old files that
    // the size limit deletes for it (never more than one pass of trim(): the next match finds the room already made).
    trim(bytes.size(), bot);
    if (total_ + bytes.size() > cfg_.max_bytes) return refuse("the older replays could not be deleted to make room");
    if (bot && bot_total_ + bytes.size() > bot_cap_bytes()) return refuse("the older replays of computer players could not be deleted to make room");
    if (cfg_.min_free_bytes > 0) {
        uint64_t available = 0;
        if (ask_free(available) && available < cfg_.min_free_bytes + bytes.size()) {
            // The volume is smaller than the size limit asks for (or something else fills it): the files that may go for this match go, the bots' first, as far as that gives the disk what it needs. When it cannot, nothing is
            // deleted for nothing.
            const uint64_t short_by = cfg_.min_free_bytes + bytes.size() - available;
            const uint64_t may_go = bot ? bot_total_ : total_;
            const std::string free_now = std::to_string(available / (1024 * 1024)) + " MiB free";
            if (may_go < short_by) {
                return refuse("the disk has less than " + std::to_string(cfg_.min_free_bytes / (1024 * 1024)) + " MiB free (" + free_now + "), and the " + (bot ? "files of computer players" : "files of the store") + " that may go for this match hold only " +
                              std::to_string(may_go / (1024 * 1024)) + " MiB, so deleting them would not make room (the volume is too small for the store, or something else fills it)");
            }
            const size_t gone = trim(bytes.size(), bot, short_by);
            uint64_t after = 0;
            if (!ask_free(after) || after < cfg_.min_free_bytes + bytes.size()) {
                return refuse("the disk has less than " + std::to_string(cfg_.min_free_bytes / (1024 * 1024)) + " MiB free (" + free_now + "), even after " + std::to_string(gone) + " older file(s) were deleted");
            }
            note("replays: the disk had " + free_now + " and keeps " + std::to_string(cfg_.min_free_bytes / (1024 * 1024)) + " MiB free: " + std::to_string(gone) + " older file(s) were deleted to keep a match (" +
                 (bot ? "files of computer players only" : "the files of computer players first") + ")");
        }
    }
    // The name: the map and the time the match ended, a "-2", "-3" ... when that is taken (by a file of the store or by anything else in the folder: nothing is replaced)
    const std::string base = "ants-" + stem_of(entry.map) + "-" + stamp_of(now);
    std::string name = base + kExtension;
    std::error_code ec;
    unsigned sequence = 1;
    while (sequence <= kMaxSameSecond && (raw_find(name) != nullptr || fs::exists(path_of(name), ec))) {
        ++sequence;
        name = base + "-" + std::to_string(sequence) + kExtension;
    }
    if (sequence > kMaxSameSecond) return refuse("every name for a match of this second is taken");
    // Written whole to a temporary name, then renamed: a crash leaves a whole file or none
    const int64_t stamp_before = folder_stamp();                                 // (the folder's change time before this write: own_change)
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
    own_change(name, stamp_before);                                              // (the list gets the file just below: the look at the folder has nothing to find in it)
    entry.file = name;
    entry.bytes = bytes.size();
    ParsedName parsed;
    entry.ended_s = parse_name(name, parsed) ? parsed.seconds : now;             // (the time of the name, as it will be read again: a clock that is far off is held to the calendar)
    entry.sequence = sequence;
    count_in(entry);                                                             // (summarize() accepted it: readable)
    entries_.insert(std::upper_bound(entries_.begin(), entries_.end(), entry, older), entry);
    ++revision_;
    saved_at_.push_back(now);
    if (bot) bot_saved_at_.push_back(now);
    result.kept = true;
    result.file = name;
    result.bytes = entry.bytes;
    return result;
}

std::vector<ReplayEntry> ReplayStore::list() const { return std::vector<ReplayEntry>(entries_.rbegin(), entries_.rend()); }

// The index is in the order of older(), which a name carries all of (its time, its "-n" and itself): a name is found by a bisection, however many files there are
ReplayEntry* ReplayStore::raw_find(const std::string& file) const {
    ParsedName parsed;
    if (!parse_name(file, parsed)) return nullptr;                               // (a name that the store could not have made is not in the index)
    ReplayEntry key;
    key.file = file;
    key.ended_s = parsed.seconds;
    key.sequence = parsed.same_second;
    const auto at = std::lower_bound(entries_.begin(), entries_.end(), key, older);
    return at != entries_.end() && at->file == file ? &*at : nullptr;
}

const ReplayEntry* ReplayStore::find(const std::string& file) const {
    ReplayEntry* entry = raw_find(file);
    if (entry != nullptr && entry->pending) index_entry(*entry);                // (somebody asks for this file by its name: it is read now, not when its turn comes)
    return entry;
}

bool ReplayStore::read(const std::string& file, std::vector<uint8_t>& out) const {
    out.clear();
    const ReplayEntry* entry = raw_find(file);
    return entry != nullptr && read_whole(path_of(entry->file), entry->bytes, out);
}

bool ReplayStore::remove(const std::string& file) {
    if (cfg_.read_only) return false;
    const ReplayEntry* found = raw_find(file);
    if (found == nullptr) return false;
    const size_t i = static_cast<size_t>(found - entries_.data());
    if (!delete_file(entries_[i])) return false;
    count_out(entries_[i]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    ++revision_;
    return true;
}

}  // namespace ants::server
