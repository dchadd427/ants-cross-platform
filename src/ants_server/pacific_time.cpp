#include "ants_server/pacific_time.hpp"

#include <algorithm>

namespace ants::server {

namespace {

constexpr int64_t kDay = 86400;
constexpr int64_t kStandardOffset = -8 * 3600;
constexpr int64_t kDaylightOffset = -7 * 3600;

int weekday_of(int64_t days) {                        // 0 Sunday .. 6 Saturday (1970-01-01 was a Thursday)
    const int64_t w = (days + 4) % 7;
    return static_cast<int>(w < 0 ? w + 7 : w);
}

// The day number of the n-th Sunday (1 - 5) of a month
int64_t nth_sunday(int year, int month, int n) {
    const int64_t first = days_from_civil(year, month, 1);
    const int to_sunday = (7 - weekday_of(first)) % 7;
    return first + to_sunday + 7 * (n - 1);
}

// The second (UTC) at which daylight time begins in a year (02:00 standard time, the second Sunday of March), and ends (02:00 daylight time, the first Sunday of November)
int64_t dst_begins(int year) { return nth_sunday(year, 3, 2) * kDay + 2 * 3600 - kStandardOffset; }
int64_t dst_ends(int year) { return nth_sunday(year, 11, 1) * kDay + 2 * 3600 - kDaylightOffset; }

int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

}  // namespace

void civil_from_days(int64_t days, int& year, int& month, int& day) {
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

int64_t days_from_civil(int year, int month, int day) {
    const int64_t y = year - (month <= 2 ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool pacific_is_dst(int64_t utc_s) {
    int year = 0, month = 0, day = 0;
    civil_from_days(floor_div(utc_s + kStandardOffset, kDay), year, month, day);          // (the year of the standard clock: the changes are in March and November, far from the new year)
    return utc_s >= dst_begins(year) && utc_s < dst_ends(year);
}

int pacific_offset_s(int64_t utc_s) { return static_cast<int>(pacific_is_dst(utc_s) ? kDaylightOffset : kStandardOffset); }

PacificClock pacific_clock(int64_t utc_s) {
    const int64_t local = utc_s + pacific_offset_s(utc_s);
    PacificClock c;
    const int64_t days = floor_div(local, kDay);
    civil_from_days(days, c.year, c.month, c.day);
    c.minute_of_day = static_cast<int>((local - days * kDay) / 60);
    return c;
}

int64_t pacific_to_utc(int year, int month, int day, int minute_of_day) {
    const int64_t local = days_from_civil(year, month, day) * kDay + static_cast<int64_t>(minute_of_day) * 60;
    const int64_t as_daylight = local - kDaylightOffset;          // the second if the clock was on daylight time (earlier than the other)
    const int64_t as_standard = local - kStandardOffset;
    if (pacific_is_dst(as_daylight)) return as_daylight;          // (the clock shows it twice: the first)
    if (!pacific_is_dst(as_standard)) return as_standard;
    return dst_begins(year);                                       // (the clock skips it: the second of the jump)
}

int64_t next_pacific_run(int64_t after_utc_s, const std::vector<int>& minutes_of_day) {
    if (minutes_of_day.empty()) return 0;
    const PacificClock now = pacific_clock(after_utc_s);
    const int64_t today = days_from_civil(now.year, now.month, now.day);
    int64_t best = 0;
    for (int64_t offset = 0; offset <= 2; ++offset) {              // (today, tomorrow and the day after: some time of the list is always in the first two)
        int year = 0, month = 0, day = 0;
        civil_from_days(today + offset, year, month, day);
        for (const int minute : minutes_of_day) {
            const int64_t at = pacific_to_utc(year, month, day, std::clamp(minute, 0, 1439));
            if (at > after_utc_s && (best == 0 || at < best)) best = at;
        }
        if (best != 0) break;
    }
    return best;
}

}  // namespace ants::server
