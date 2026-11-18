#pragma once

// Pacific time for the schedules of the server's own matches of computer players (docs/SERVER_REPLAYS.md, "Schedules"). The server's image holds no time zone data, so the clock of the United States is written here
// by hand: Pacific standard time (UTC-8) from 02:00 on the first Sunday of November to 02:00 on the second Sunday of March, Pacific daylight time (UTC-7) between (the rule since 2007; no earlier year is meant).
// Pure functions of the second since 1970 UTC: no clock, no files, no zone database.
//
// The two days of the change are settled once, here:
//  * spring forward, the clock jumps from 02:00 to 03:00: a time of the day inside that hour does not happen, and a schedule that asks for it runs at 03:00, the moment the clock jumps;
//  * fall back, the clock shows 01:00 to 02:00 twice: a time of the day inside that hour happens twice and a schedule runs at the first.

#include <cstdint>
#include <vector>

namespace ants::server {

/// The civil date of a day number (days since 1970-01-01), and back
void civil_from_days(int64_t days, int& year, int& month, int& day);
int64_t days_from_civil(int year, int month, int day);

/// Whether Pacific daylight time is in force at this second (UTC)
bool pacific_is_dst(int64_t utc_s);
/// Seconds to add to UTC for the Pacific clock at this second: -28800 (standard) or -25200 (daylight)
int pacific_offset_s(int64_t utc_s);

/// What the Pacific clock shows at a second
struct PacificClock {
    int year{0}, month{0}, day{0};
    int minute_of_day{0};              // 0 - 1439
};
PacificClock pacific_clock(int64_t utc_s);

/// The first second (UTC) at which the Pacific clock shows `minute_of_day` (0 - 1439) on the date: the first of the two when the clock shows it twice, and the second of the clock's jump when it skips it
int64_t pacific_to_utc(int year, int month, int day, int minute_of_day);

/// The first second (UTC) after `after_utc_s` at which the Pacific clock shows one of the times of the day (minutes since midnight, in any order); 0 when there is none to ask for (an empty list)
int64_t next_pacific_run(int64_t after_utc_s, const std::vector<int>& minutes_of_day);

}  // namespace ants::server
