#include "ants_server/site_stats.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>

#include "ants_ctl/json.hpp"
#include "ants_server/room_manager.hpp"

namespace fs = std::filesystem;

namespace ants::server {

namespace {

constexpr int64_t kNoHour = INT64_MIN;
constexpr int64_t kSecondsPerHour = 3600;
constexpr int64_t kSecondsPerDay = 86400;
constexpr size_t kMaxFileBytes = 16 * 1024;              // the file is a few hundred bytes; anything bigger is not ours
constexpr int64_t kFormat = 1;

int64_t floor_div(int64_t a, int64_t b) noexcept {
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

size_t slot_of(int64_t hour) noexcept { return static_cast<size_t>(((hour % static_cast<int64_t>(SiteStats::kHours)) + static_cast<int64_t>(SiteStats::kHours)) % static_cast<int64_t>(SiteStats::kHours)); }

// The calendar (Howard Hinnant's days_from_civil and civil_from_days: the proleptic Gregorian calendar, days since 1970-01-01)
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

std::string two_digits(unsigned v) { return (v < 10 ? "0" : "") + std::to_string(v); }

// The UTC date of a time as YYYY-MM-DD (a year outside 1970 .. 9999 is held to it: the text always has the one shape)
std::string date_of(int64_t seconds) {
    int64_t y = 0;
    unsigned m = 0;
    unsigned d = 0;
    civil_from_days(floor_div(seconds, kSecondsPerDay), y, m, d);
    if (y < 1970) {
        y = 1970;
        m = 1;
        d = 1;
    } else if (y > 9999) {
        y = 9999;
        m = 12;
        d = 31;
    }
    return std::to_string(y) + "-" + two_digits(m) + "-" + two_digits(d);
}

// Exactly YYYY-MM-DD, a real day of 1970 .. 9999
bool valid_date(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) return false;
    }
    const int64_t y = std::stoll(text.substr(0, 4));
    const unsigned m = static_cast<unsigned>(std::stoul(text.substr(5, 2)));
    const unsigned d = static_cast<unsigned>(std::stoul(text.substr(8, 2)));
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    int64_t y2 = 0;
    unsigned m2 = 0;
    unsigned d2 = 0;
    civil_from_days(days_from_civil(y, m, d), y2, m2, d2);                 // (a 31st of February comes back as the 3rd of March)
    return y2 == y && m2 == m && d2 == d;
}

int64_t system_seconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

ctl::JsonValue int_value(uint64_t v) { return ctl::JsonValue::make_int(static_cast<int64_t>(std::min<uint64_t>(v, static_cast<uint64_t>(INT64_MAX)))); }

}  // namespace

SiteStats::Series::Series() { hour.fill(kNoHour); }

SiteStats::SiteStats(Clock clock) : clock_(clock ? std::move(clock) : Clock(system_seconds)), since_(date_of(clock_())) {}

int64_t SiteStats::hour_of(int64_t seconds) noexcept { return floor_div(seconds, kSecondsPerHour); }

uint64_t SiteStats::window(const Series& series, int64_t now_hour) noexcept {
    uint64_t sum = 0;
    for (size_t i = 0; i < kHours; ++i) {
        if (series.hour[i] != kNoHour && series.hour[i] <= now_hour && series.hour[i] > now_hour - static_cast<int64_t>(kHours)) sum += series.count[i];
    }
    return sum;
}

void SiteStats::add(Series& series, int64_t now_hour) noexcept {
    const size_t slot = slot_of(now_hour);
    if (series.hour[slot] != now_hour) {                                   // the slot of a bucket from a day ago (or none) starts again
        series.hour[slot] = now_hour;
        series.count[slot] = 0;
    }
    ++series.count[slot];
    ++series.total;
}

void SiteStats::count_ended(const RoomStatus& ended) {
    if (ended.ticks > 0) count_online();
}

void SiteStats::count_online() {
    add(online_, hour_of(clock_()));
    dirty_ = true;
}

bool SiteStats::count_local() {
    const int64_t now = clock_();
    if (local_filled_ == kLocalPerMinute) {                                // the oldest of the last 120 counted: still inside the minute means 120 were counted in it
        const int64_t age = now - local_times_[local_next_];
        if (age >= 0 && age < kLocalWindowS) return false;
    }
    local_times_[local_next_] = now;
    local_next_ = (local_next_ + 1) % kLocalPerMinute;
    if (local_filled_ < kLocalPerMinute) ++local_filled_;
    add(local_, hour_of(now));
    dirty_ = true;
    return true;
}

SiteCount SiteStats::online() const { return SiteCount{window(online_, hour_of(clock_())), online_.total}; }

SiteCount SiteStats::local() const { return SiteCount{window(local_, hour_of(clock_())), local_.total}; }

std::string SiteStats::json(const BusyCounts& now) const {
    const SiteCount on = online();
    const SiteCount lo = local();
    const auto counter = [](const SiteCount& c) {
        ctl::JsonValue v = ctl::JsonValue::make_object();
        v.set("day", int_value(c.day));
        v.set("total", int_value(c.total));
        return v;
    };
    ctl::JsonValue busy = ctl::JsonValue::make_object();
    busy.set("matches", int_value(now.matches));
    busy.set("players", int_value(now.players));
    ctl::JsonValue root = ctl::JsonValue::make_object();
    root.set("now", busy);
    root.set("online", counter(on));
    root.set("local", counter(lo));
    root.set("since", ctl::JsonValue::make_string(since_));
    return ctl::to_json(root);
}

// ------------------------------------------------------------------------------------------------
// The file

namespace {

// One counter as the file holds it: {"total":T,"hours":[[hour number, count], ...]}, the buckets of the window only
ctl::JsonValue series_json(uint64_t total, const std::array<int64_t, SiteStats::kHours>& hour, const std::array<uint64_t, SiteStats::kHours>& count, int64_t now_hour) {
    ctl::JsonValue hours = ctl::JsonValue::make_array();
    for (size_t i = 0; i < SiteStats::kHours; ++i) {
        if (hour[i] == kNoHour || hour[i] > now_hour || hour[i] <= now_hour - static_cast<int64_t>(SiteStats::kHours)) continue;
        ctl::JsonValue pair = ctl::JsonValue::make_array();
        pair.push_back(ctl::JsonValue::make_int(hour[i]));
        pair.push_back(int_value(count[i]));
        hours.push_back(std::move(pair));
    }
    ctl::JsonValue v = ctl::JsonValue::make_object();
    v.set("total", int_value(total));
    v.set("hours", std::move(hours));
    return v;
}

}  // namespace

bool SiteStats::write_file(std::string& why) {
    const int64_t now_hour = hour_of(clock_());
    ctl::JsonValue root = ctl::JsonValue::make_object();
    root.set("format", ctl::JsonValue::make_int(kFormat));
    root.set("since", ctl::JsonValue::make_string(since_));
    root.set("online", series_json(online_.total, online_.hour, online_.count, now_hour));
    root.set("local", series_json(local_.total, local_.hour, local_.count, now_hour));
    const std::string text = ctl::to_json(root) + "\n";
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) {
            why = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);                                            // (whole file or the old one: never half)
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        why = "cannot rename " + tmp + ": " + ec.message();
        return false;
    }
    return true;
}

bool SiteStats::read_file(std::string& why) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        why = "it cannot be opened";
        return false;
    }
    std::string text(kMaxFileBytes + 1, '\0');
    in.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    if (text.size() > kMaxFileBytes) {
        why = "it is too big";
        return false;
    }
    ctl::JsonLimits limits;
    limits.max_bytes = kMaxFileBytes;
    limits.max_depth = 6;
    ctl::JsonValue root;
    std::string error;
    if (!ctl::parse_json(text, root, &error, limits)) {
        why = "it is not JSON (" + error + ")";
        return false;
    }
    if (!root.is_object() || root.get("format").as_int_or(0) != kFormat) {
        why = "it is not a statistics file of this format";
        return false;
    }
    if (!root.get("since").is_string() || !valid_date(root.get("since").str())) {
        why = "its date is not a YYYY-MM-DD day";
        return false;
    }
    // Both counters are read before either is kept: the file is whole or it is nothing
    Series read[2];
    const char* names[2] = {"online", "local"};
    for (int s = 0; s < 2; ++s) {
        const ctl::JsonValue& v = root.get(names[s]);
        const ctl::JsonValue& total = v.get("total");
        const ctl::JsonValue& hours = v.get("hours");
        if (!v.is_object() || !total.is_int() || total.as_int_or(-1) < 0 || !hours.is_array() || hours.size() > kHours) {
            why = std::string("its ") + names[s] + " counter is wrong";
            return false;
        }
        const uint64_t all = static_cast<uint64_t>(total.as_int_or(0));
        uint64_t sum = 0;
        read[s].total = all;
        for (size_t i = 0; i < hours.size(); ++i) {
            const ctl::JsonValue& pair = hours.at(i);
            if (!pair.is_array() || pair.size() != 2 || !pair.at(0).is_int() || !pair.at(1).is_int() || pair.at(0).as_int_or(-1) < 0 || pair.at(1).as_int_or(-1) < 0) {
                why = std::string("a bucket of its ") + names[s] + " counter is wrong";
                return false;
            }
            const int64_t hour = pair.at(0).as_int_or(0);
            const uint64_t n = static_cast<uint64_t>(pair.at(1).as_int_or(0));
            if (n > all - sum) {                                           // the buckets together are games that were counted: never more than the total
                why = std::string("the buckets of its ") + names[s] + " counter hold more than its total";
                return false;
            }
            sum += n;
            const size_t slot = slot_of(hour);
            if (read[s].hour[slot] == hour) {
                why = std::string("its ") + names[s] + " counter has an hour twice";
                return false;
            }
            if (read[s].hour[slot] == kNoHour || read[s].hour[slot] < hour) {      // (two hours of one slot are a day apart: the later one is the one that counts)
                read[s].hour[slot] = hour;
                read[s].count[slot] = n;
            }
        }
    }
    since_ = root.get("since").str();
    online_ = read[0];
    local_ = read[1];
    return true;
}

void SiteStats::open(const std::string& path) {
    path_ = path;
    std::error_code ec;
    const fs::file_status status = fs::status(path_, ec);
    if (status.type() == fs::file_type::not_found) {
        dirty_ = true;                                                     // a new file: it is written at the first save, so that `since` outlives a restart before any game
        notices_.push_back("site statistics: " + path_ + " is new, counting from " + since_);
        return;
    }
    std::string why;
    if (ec) why = "it cannot be looked at (" + ec.message() + ")";
    else if (status.type() != fs::file_type::regular) why = "it is not a regular file";
    if (why.empty() && read_file(why)) {
        notices_.push_back("site statistics: read " + std::to_string(online_.total) + " online and " + std::to_string(local_.total) + " single-player games since " + since_ + " from " + path_);
        return;
    }
    const std::string aside = path_ + ".broken-" + std::to_string(clock_());
    std::error_code moved;
    fs::rename(path_, aside, moved);
    online_ = Series();
    local_ = Series();
    since_ = date_of(clock_());
    dirty_ = true;
    notices_.push_back("site statistics: " + path_ + " cannot be used (" + why + "); " + (moved ? "it could not be put aside (" + moved.message() + ")" : "put aside as " + aside) + ", the counters start at 0 from " + since_);
}

std::vector<std::string> SiteStats::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

bool SiteStats::save_if_due() {
    if (path_.empty() || !dirty_) return false;
    if (have_tried_) {
        const int64_t waited = clock_() - tried_at_;
        if (waited >= 0 && waited < kSaveEveryS) return false;
    }
    return save();
}

bool SiteStats::save() {
    if (path_.empty() || !dirty_) return true;
    have_tried_ = true;
    tried_at_ = clock_();
    std::string why;
    if (write_file(why)) {
        dirty_ = false;
        if (failing_) {
            failing_ = false;
            notices_.push_back("site statistics are saved again");
        }
        return true;
    }
    if (!failing_) {
        failing_ = true;
        notices_.push_back("site statistics could not be saved: " + why);
    }
    return false;
}

}  // namespace ants::server
