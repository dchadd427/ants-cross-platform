// Tests of the schedules of the server's own matches of computer players (pacific_time.hpp, bot_match_spec.hpp, bot_schedules.hpp, db_schema.cpp version 9; docs/SERVER_REPLAYS.md "Schedules"): the Pacific clock written by hand
// (the changes of 2026 to 2028 against the zone database's own seconds, and the two odd days: the hour that does not happen and the hour that happens twice), when a schedule runs next (every N minutes, at times of the
// day, after a restart), the words that a batch and a schedule are made of, and the rows that the database keeps (a restart reads them back, the default is made once, a deleted default stays deleted, a copy of a
// version 8 database migrates to 9 and keeps what it had). The routes and the limits are in the server suite (bot_admin_tests.hpp). No sockets, no rooms.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>

#include "../common/test_assert.hpp"
#include "ants_server/bot_match_spec.hpp"
#include "ants_server/bot_schedules.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/pacific_time.hpp"
#include "history_fixtures.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name.substr(0, 110) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

namespace {

// The second (UTC) of a date and a time of day
int64_t utc(int y, int m, int d, int h, int mi) { return days_from_civil(y, m, d) * 86400 + h * 3600 + mi * 60; }

ctl::JsonValue parsed(const std::string& text) {
    ctl::JsonValue v;
    std::string err;
    if (!ctl::parse_json(text, v, &err)) std::cout << "bad test json: " << text << " (" << err << ")\n";
    return v;
}

BotSchedulePatch patch_every(const std::string& name, int minutes) {
    BotSchedulePatch p;
    p.has_name = true;
    p.name = name;
    p.has_when = true;
    p.kind = BotSchedule::Kind::Every;
    p.minutes = minutes;
    p.has_what = true;
    return p;
}

BotSchedulePatch patch_times(const std::string& name, std::vector<int> times) {
    BotSchedulePatch p;
    p.has_name = true;
    p.name = name;
    p.has_when = true;
    p.kind = BotSchedule::Kind::Times;
    p.times = std::move(times);
    p.has_what = true;
    return p;
}

int64_t scalar(db::Database& db, const std::string& sql) {
    db::Statement st;
    if (!db.prepare(st, sql).ok()) return -1;
    return st.step() == db::Statement::Step::Row ? st.column_int64(0) : -1;
}

std::string text_of(db::Database& db, const std::string& sql) {
    db::Statement st;
    if (!db.prepare(st, sql).ok()) return "<error>";
    return st.step() == db::Statement::Step::Row ? st.column_text(0) : std::string("<none>");
}

// The changes of the clock, as the zone database has them (checked with its own program): when daylight time begins and ends in a year
struct Change {
    int year;
    int64_t begins, ends;
};
const Change kChanges[] = {{2026, 1772964000, 1793523600}, {2027, 1805018400, 1825578000}, {2028, 1836468000, 1857027600}};

const int64_t kNow = 1800000000;                          // 2027-01-15 08:00 UTC: a winter morning

}  // namespace

int main() {
    std::cout << "=======================================================\n THE SCHEDULES OF THE SERVER'S OWN MATCHES OF COMPUTER PLAYERS\n=======================================================\n";

    TEST_CASE("BSC1.1 The clock of the United States by hand: daylight time begins and ends on the seconds that the zone database has (2026, 2027, 2028), the offset is -8 h or -7 h, and the clock reads 01:59 then 03:00 in spring and 01:59 then 01:00 in autumn") {
        for (const Change& c : kChanges) {
            ASSERT_FALSE(pacific_is_dst(c.begins - 1));
            ASSERT_TRUE(pacific_is_dst(c.begins));
            ASSERT_TRUE(pacific_is_dst(c.ends - 1));
            ASSERT_FALSE(pacific_is_dst(c.ends));
            ASSERT_EQ(pacific_offset_s(c.begins - 1), -8 * 3600);
            ASSERT_EQ(pacific_offset_s(c.begins), -7 * 3600);
            ASSERT_EQ(pacific_offset_s(c.ends - 1), -7 * 3600);
            ASSERT_EQ(pacific_offset_s(c.ends), -8 * 3600);
            ASSERT_EQ(pacific_clock(c.begins - 1).minute_of_day, 1 * 60 + 59);                  // (01:59:59 and then the clock jumps)
            ASSERT_EQ(pacific_clock(c.begins).minute_of_day, 3 * 60);
            ASSERT_EQ(pacific_clock(c.ends - 1).minute_of_day, 1 * 60 + 59);                    // (01:59:59 daylight time, and then the clock goes back to 01:00)
            ASSERT_EQ(pacific_clock(c.ends).minute_of_day, 1 * 60);
            ASSERT_FALSE(pacific_is_dst(utc(c.year, 1, 15, 12, 0)));
            ASSERT_TRUE(pacific_is_dst(utc(c.year, 7, 15, 12, 0)));
        }
        // the date of the clock: the evening of the 31st by the clock is the next day by UTC
        const PacificClock late = pacific_clock(utc(2026, 12, 31, 23, 30));
        ASSERT_TRUE(late.year == 2026 && late.month == 12 && late.day == 31 && late.minute_of_day == 15 * 60 + 30);
        const PacificClock early = pacific_clock(utc(2027, 1, 1, 7, 59));
        ASSERT_TRUE(early.year == 2026 && early.month == 12 && early.day == 31 && early.minute_of_day == 23 * 60 + 59);
    } TEST_END();

    TEST_CASE("BSC1.2 The next time of the day is the first one after now by the Pacific clock, from a list in any order; none for an empty list; a time that has just run is not next") {
        const int64_t morning = utc(2027, 1, 15, 18, 20);                                         // 10:20 Pacific
        ASSERT_EQ(next_pacific_run(morning, {18 * 60, 10 * 60 + 30}), utc(2027, 1, 15, 18, 30));  // (10:30 today)
        ASSERT_EQ(next_pacific_run(morning, {18 * 60, 7 * 60}), utc(2027, 1, 16, 2, 0));          // (18:00 today is 02:00 UTC of the 16th)
        ASSERT_EQ(next_pacific_run(utc(2027, 1, 16, 2, 0), {18 * 60, 7 * 60}), utc(2027, 1, 16, 15, 0));   // (the minute that has run is not next: 07:00 tomorrow)
        ASSERT_EQ(next_pacific_run(utc(2027, 1, 16, 2, 0) - 1, {18 * 60}), utc(2027, 1, 16, 2, 0));
        ASSERT_EQ(next_pacific_run(morning, {}), int64_t{0});
        ASSERT_EQ(next_pacific_run(morning, {0}), utc(2027, 1, 16, 8, 0));                        // (midnight: the clock's 00:00 is 08:00 UTC)
        ASSERT_EQ(next_pacific_run(morning, {23 * 60 + 59}), utc(2027, 1, 16, 7, 59));
        const int64_t summer = utc(2027, 7, 15, 18, 20);                                          // 11:20 Pacific daylight time
        ASSERT_EQ(next_pacific_run(summer, {18 * 60}), utc(2027, 7, 16, 1, 0));                   // (18:00 daylight time is 01:00 UTC: seven hours, not eight)
    } TEST_END();

    TEST_CASE("BSC1.3 Spring forward: the hour that does not happen runs at 03:00 (the second that the clock jumps), once, whatever of that hour the list names, and the next run is the next day's") {
        const Change& c = kChanges[0];                                                            // 2026-03-08, at 02:00 the clock reads 03:00
        const int64_t midnight = utc(2026, 3, 8, 8, 0);                                           // 00:00 Pacific standard time of that day
        ASSERT_EQ(next_pacific_run(midnight, {2 * 60 + 30}), c.begins);                           // (02:30 does not exist: 03:00)
        ASSERT_EQ(next_pacific_run(midnight, {2 * 60}), c.begins);
        ASSERT_EQ(next_pacific_run(midnight, {2 * 60 + 59}), c.begins);
        ASSERT_EQ(next_pacific_run(midnight, {2 * 60 + 30, 3 * 60}), c.begins);                   // (a list that names 02:30 and 03:00 asks for the same second: one run)
        ASSERT_EQ(next_pacific_run(c.begins, {2 * 60 + 30, 3 * 60}), utc(2026, 3, 9, 9, 30));    // (after it: 02:30 of the next day, which exists: 09:30 UTC in daylight time)
        ASSERT_EQ(next_pacific_run(midnight, {1 * 60 + 59}), utc(2026, 3, 8, 9, 59));             // (01:59 happens, in standard time)
        ASSERT_EQ(next_pacific_run(midnight, {3 * 60 + 1}), c.begins + 60);                       // (03:01: a minute after the jump)
    } TEST_END();

    TEST_CASE("BSC1.4 Fall back: the hour that happens twice runs at the first, once: the next run after it is the next day's, not the repeat") {
        const Change& c = kChanges[0];                                                            // 2026-11-01, at 02:00 the clock goes back to 01:00
        const int64_t midnight = utc(2026, 11, 1, 7, 0);                                          // 00:00 Pacific daylight time of that day
        ASSERT_EQ(next_pacific_run(midnight, {1 * 60 + 30}), utc(2026, 11, 1, 8, 30));            // (the first 01:30: daylight time)
        ASSERT_EQ(next_pacific_run(utc(2026, 11, 1, 8, 30), {1 * 60 + 30}), utc(2026, 11, 2, 9, 30));      // (not 09:30 UTC the same day, the repeat: the next day's, in standard time)
        ASSERT_EQ(next_pacific_run(midnight, {1 * 60}), utc(2026, 11, 1, 8, 0));
        ASSERT_EQ(next_pacific_run(utc(2026, 11, 1, 8, 0), {1 * 60}), utc(2026, 11, 2, 9, 0));
        ASSERT_EQ(next_pacific_run(utc(2026, 11, 1, 8, 30), {2 * 60}), c.ends + 3600);            // (02:00 happens once: after the clock came back, 10:00 UTC)
        ASSERT_EQ(next_pacific_run(midnight, {0, 12 * 60}), utc(2026, 11, 1, 20, 0));                  // (the 00:00 that has just passed is not next: 12:00 is, in standard time)
    } TEST_END();

    TEST_CASE("BSC1.5 Through three years of days: a time of the day runs exactly once on every day of the clock (at 03:00 on the day that has no 02:30), in order, a day apart on the clock, 23 hours and 25 hours apart only on the two days of a change") {
        for (const int minute : {1 * 60 + 30, 2 * 60 + 30, 12 * 60, 23 * 60 + 59}) {
            int64_t at = utc(2026, 1, 1, 8, 0) - 1;
            int days = 0;
            int long_days = 0, short_days = 0;
            int64_t previous = 0;
            std::set<std::string> dates;
            while (true) {
                at = next_pacific_run(at, {minute});
                const PacificClock clock = pacific_clock(at);
                if (clock.year > 2028) break;
                ASSERT_TRUE(dates.insert(std::to_string(clock.year) + "-" + std::to_string(clock.month) + "-" + std::to_string(clock.day)).second);           // (never twice on one date)
                const bool skipped_day = minute == 2 * 60 + 30 && ((clock.year == 2026 && clock.month == 3 && clock.day == 8) || (clock.year == 2027 && clock.month == 3 && clock.day == 14) || (clock.year == 2028 && clock.month == 3 && clock.day == 12));
                ASSERT_EQ(clock.minute_of_day, skipped_day ? 3 * 60 : minute);
                if (previous != 0) {
                    ASSERT_GT(at, previous);
                    const int64_t gap = at - previous;
                    if (minute == 12 * 60) {
                        ASSERT_TRUE(gap == 86400 || gap == 82800 || gap == 90000);
                        if (gap == 82800) ++short_days;
                        if (gap == 90000) ++long_days;
                    }
                }
                previous = at;
                ++days;
            }
            ASSERT_EQ(days, 365 * 3 + 1);                                                                     // (2026, 2027 and 2028, a leap year)
            if (minute == 12 * 60) ASSERT_TRUE(short_days == 3 && long_days == 3);                           // (the three springs and the three autumns)
        }
    } TEST_END();

    TEST_CASE("BSC2.1 The name of a schedule is cleaned (controls and direction characters out, the blanks cut and made one, 1 to 40 characters counted as characters); the times of the day are 24-hour clock times") {
        std::string out;
        ASSERT_TRUE(clean_schedule_name("  Evening   swarm ", out) && out == "Evening swarm");
        ASSERT_TRUE(clean_schedule_name(std::string("Bad\x01") + "tab\there", out) && out == "Bad tab here");                // (a control is a blank)
        ASSERT_TRUE(clean_schedule_name("a\xE2\x80\xAEz", out) && out == "az");                                           // (U+202E, a direction control: gone)
        ASSERT_TRUE(clean_schedule_name("zero\xE2\x80\x8Bwidth", out) && out == "zerowidth");
        ASSERT_TRUE(clean_schedule_name(std::string(40, 'x'), out) && out.size() == 40);
        ASSERT_FALSE(clean_schedule_name(std::string(41, 'x'), out));
        std::string forty_wide;
        for (int i = 0; i < 40; ++i) forty_wide += "\xC3\xA9";                                                            // (40 characters of two bytes each: the length counts characters)
        ASSERT_TRUE(clean_schedule_name(forty_wide, out) && out == forty_wide);
        ASSERT_FALSE(clean_schedule_name(forty_wide + "e", out));
        ASSERT_FALSE(clean_schedule_name("", out));
        ASSERT_FALSE(clean_schedule_name("   \xE2\x80\xAE ", out));                                                       // (nothing is left)
        ASSERT_FALSE(clean_schedule_name("\xFF\xFE", out));                                                                // (not UTF-8)
        int minutes = 0;
        ASSERT_TRUE(parse_clock_time("18:30", minutes) && minutes == 18 * 60 + 30);
        ASSERT_TRUE(parse_clock_time("7:05", minutes) && minutes == 7 * 60 + 5);
        ASSERT_TRUE(parse_clock_time("00:00", minutes) && minutes == 0);
        ASSERT_TRUE(parse_clock_time("23:59", minutes) && minutes == 23 * 60 + 59);
        for (const char* bad : {"24:00", "18:60", "18:5", "1830", "18:30:00", "", ":30", "a8:30", "18:3a", "-1:30", " 18:30"}) ASSERT_FALSE(parse_clock_time(bad, minutes));
        ASSERT_EQ(clock_time_text(18 * 60 + 5), std::string("18:05"));
    } TEST_END();

    TEST_CASE("BSC2.2 A batch is read from the page's words and every word that is not one is refused with its own code; a split needs three or four bots; the batch is kept in the words that it came in") {
        BotWhat what;
        std::string error;
        ASSERT_TRUE(parse_bot_what(parsed("{\"count\":3,\"map\":\"Fight Island.lvl\",\"rules\":\"187\",\"bots\":\"4\",\"levels\":[\"hard\",\"hard\",\"medium\",\"easy\"],\"teams\":\"split\",\"split\":\"13-24\"}"), what, error));
        ASSERT_TRUE(what.count == 3 && what.spec.map == "Fight Island.lvl" && what.spec.rules == BotRules::Kills187 && what.spec.bots == 4 && what.spec.teams == BotTeamsChoice::Split && what.spec.split == 1);
        ASSERT_TRUE(what.spec.levels[0] == BotLevelChoice::Hard && what.spec.levels[2] == BotLevelChoice::Medium && what.spec.levels[3] == BotLevelChoice::Easy);
        const ctl::JsonValue back = bot_what_json(what);
        ASSERT_EQ(ctl::to_json(back), std::string("{\"count\":3,\"map\":\"Fight Island.lvl\",\"rules\":\"187\",\"bots\":\"4\",\"levels\":[\"hard\",\"hard\",\"medium\",\"easy\"],\"teams\":\"split\",\"split\":\"13-24\"}"));
        BotWhat again;
        ASSERT_TRUE(parse_bot_what(back, again, error) && ctl::to_json(bot_what_json(again)) == ctl::to_json(back));
        // the default: the rotation, every draw open
        ASSERT_TRUE(parse_bot_what(parsed("{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"random\",\"levels\":[\"rmh\",\"rmh\",\"rmh\",\"rmh\"],\"teams\":\"random\",\"split\":\"\"}"), what, error));
        ASSERT_TRUE(what.spec.map == kBotMapRotation && what.spec.rules == BotRules::Random && what.spec.bots == 0 && what.spec.teams == BotTeamsChoice::Random && what.spec.levels[3] == BotLevelChoice::RandomMediumHard);
        ASSERT_TRUE(parse_bot_what(parsed("{\"count\":8,\"map\":\"random:players\",\"rules\":\"highest-score\",\"bots\":3,\"levels\":[\"random\"],\"teams\":\"split\",\"split\":\"3-12\"}"), what, error));   // (a number for the bots, fewer levels than bots: the rest stay as they were)
        ASSERT_TRUE(what.spec.bots == 3 && what.spec.levels[0] == BotLevelChoice::RandomAny && what.spec.levels[1] == BotLevelChoice::RandomMediumHard && what.spec.split == 2);
        const struct {
            const char* body;
            const char* code;
        } bad[] = {
            {"{\"count\":0,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_count"},
            {"{\"count\":9,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_count"},
            {"{\"count\":\"2\",\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_count"},
            {"{\"count\":1,\"map\":\"random:nothing\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_map"},
            {"{\"count\":1,\"map\":\"../x.lvl\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_map"},
            {"{\"count\":1,\"map\":\"TINY.txt\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_map"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"both\",\"bots\":\"2\",\"levels\":[],\"teams\":\"ffa\"}", "bad_rules"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"5\",\"levels\":[],\"teams\":\"ffa\"}", "bad_bots"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"1\",\"levels\":[],\"teams\":\"ffa\"}", "bad_bots"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[\"hard\",\"loud\"],\"teams\":\"ffa\"}", "bad_levels"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[\"hard\",\"hard\",\"hard\",\"hard\",\"hard\"],\"teams\":\"ffa\"}", "bad_levels"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"pairs\"}", "bad_teams"},
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"2\",\"levels\":[],\"teams\":\"split\",\"split\":\"12-34\"}", "bad_split"},        // (two bots cannot be split)
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"random\",\"levels\":[],\"teams\":\"split\",\"split\":\"12-34\"}", "bad_split"},   // (nor a number that is drawn)
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"3\",\"levels\":[],\"teams\":\"split\",\"split\":\"12-34\"}", "bad_split"},        // (a split of four for three bots)
            {"{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"4\",\"levels\":[],\"teams\":\"split\"}", "bad_split"},
        };
        for (const auto& b : bad) {
            error.clear();
            ASSERT_FALSE(parse_bot_what(parsed(b.body), what, error));
            ASSERT_EQ(error, std::string(b.code));
        }
        ASSERT_TRUE(bot_map_name_shape_ok("Bombz Away.lvl") && bot_map_name_shape_ok("TINY.LVL") && !bot_map_name_shape_ok("a/b.lvl") && !bot_map_name_shape_ok(".lvl") && !bot_map_name_shape_ok(std::string(100, 'a') + ".lvl"));
    } TEST_END();

    TEST_CASE("BSC2.3 The body of a schedule: the name, when (every N minutes from 5 to 1440, or 1 to 12 times of the day, sorted, no repeat) and what; a change may carry any part; every refusal has its own code") {
        BotSchedulePatch p;
        std::string error;
        const std::string what = "\"what\":{\"count\":2,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"random\",\"levels\":[\"rmh\",\"rmh\",\"rmh\",\"rmh\"],\"teams\":\"random\",\"split\":\"\"}";
        ASSERT_TRUE(parse_schedule_patch(parsed("{\"name\":\" Evening swarm\",\"on\":false,\"when\":{\"kind\":\"times\",\"times\":[\"22:00\",\"18:00\",\"18:00\",\"20:30\"]}," + what + "}"), true, p, error));
        ASSERT_TRUE(p.has_name && p.name == "Evening swarm" && p.has_on && !p.on && p.has_when && p.kind == BotSchedule::Kind::Times && p.has_what && p.what.count == 2);
        ASSERT_EQ(p.times, (std::vector<int>{18 * 60, 20 * 60 + 30, 22 * 60}));
        ASSERT_TRUE(parse_schedule_patch(parsed("{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":5}," + what + "}"), true, p, error) && p.kind == BotSchedule::Kind::Every && p.minutes == 5);
        ASSERT_TRUE(parse_schedule_patch(parsed("{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":\"1440\"}," + what + "}"), true, p, error) && p.minutes == 1440);       // (the page's box is text)
        ASSERT_TRUE(parse_schedule_patch(parsed("{\"on\":true}"), false, p, error) && p.has_on && p.on && !p.has_name && !p.has_when && !p.has_what);                      // (a change of one part)
        ASSERT_TRUE(parse_schedule_patch(parsed("{}"), false, p, error) && !p.has_on && !p.has_name);
        const struct {
            const char* body;
            bool create;
            const char* code;
        } bad[] = {
            {"{\"when\":{\"kind\":\"every\",\"minutes\":60}}", true, "bad_name"},                                       // (a new schedule needs a name, when and what)
            {"{\"name\":\"\",\"when\":{\"kind\":\"every\",\"minutes\":60}}", true, "bad_name"},
            {"{\"name\":7}", false, "bad_name"},
            {"{\"on\":\"yes\"}", false, "bad_on"},
            {"{\"name\":\"x\"}", true, "bad_when"},
            {"{\"when\":{\"kind\":\"weekly\"}}", false, "bad_when"},
            {"{\"when\":5}", false, "bad_when"},
            {"{\"when\":{\"kind\":\"every\",\"minutes\":4}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"every\",\"minutes\":1441}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"every\",\"minutes\":\"abc\"}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"every\",\"minutes\":\"\"}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"every\",\"minutes\":60.5}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"every\"}}", false, "bad_every"},
            {"{\"when\":{\"kind\":\"times\",\"times\":[]}}", false, "no_times"},
            {"{\"when\":{\"kind\":\"times\"}}", false, "no_times"},
            {"{\"when\":{\"kind\":\"times\",\"times\":[\"25:00\"]}}", false, "bad_times"},
            {"{\"when\":{\"kind\":\"times\",\"times\":[\"10:00\",\"11:00\",\"12:00\",\"13:00\",\"14:00\",\"15:00\",\"16:00\",\"17:00\",\"18:00\",\"19:00\",\"20:00\",\"21:00\",\"22:00\"]}}", false, "bad_times"},   // (13 times)
            {"{\"what\":{\"count\":0}}", false, "bad_count"},
        };
        for (const auto& b : bad) {
            error.clear();
            ASSERT_FALSE(parse_schedule_patch(parsed(b.body), b.create, p, error));
            ASSERT_EQ(error, std::string(b.code));
        }
        ASSERT_TRUE(parse_schedule_patch(parsed("{\"when\":{\"kind\":\"times\",\"times\":[\"10:00\",\"11:00\",\"12:00\",\"13:00\",\"14:00\",\"15:00\",\"16:00\",\"17:00\",\"18:00\",\"19:00\",\"20:00\",\"21:00\"]}}"), false, p, error) && p.times.size() == kMaxBotScheduleTimes);       // (12 are allowed)
    } TEST_END();

    TEST_CASE("BSC3.1 When a schedule runs: every N minutes counts from the last start (from now when it never started) and never inside the request that saved it; after a run it is N minutes on; after a restart it is the later of what was kept and a minute from now; off is no run") {
        BotSchedule s;
        s.kind = BotSchedule::Kind::Every;
        s.minutes = 60;
        s.on = true;
        ASSERT_EQ(bot_schedule_planned_next(s, kNow), kNow + 3600);                           // (never started: an hour from now)
        s.last_run = kNow - 600;
        ASSERT_EQ(bot_schedule_planned_next(s, kNow), kNow + 3000);                           // (counted from the last start)
        s.last_run = kNow - 7200;
        ASSERT_EQ(bot_schedule_planned_next(s, kNow), kNow + 60);                             // (long past: a minute from now, not now)
        s.minutes = 5;
        s.last_run = kNow - 10;
        ASSERT_EQ(bot_schedule_planned_next(s, kNow), kNow + 290);
        ASSERT_EQ(bot_schedule_next_after_run(s, kNow), kNow + 300);
        s.minutes = 60;
        s.next_run = kNow + 2400;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 2400);                           // (a restart keeps a run that is still ahead)
        s.minutes = 5;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 300);                            // (...but not one that is further off than the schedule's own period: that time was never made by it)
        s.next_run = kNow - 5000;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 60);                             // (a run that was due while the server was down is not made up: a minute after it is up)
        s.next_run = kNow + 10;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 60);
        s.on = false;
        ASSERT_TRUE(bot_schedule_planned_next(s, kNow) == 0 && bot_schedule_next_after_run(s, kNow) == 0 && bot_schedule_resumed_next(s, kNow) == 0);
        BotSchedule t;
        t.kind = BotSchedule::Kind::Times;
        t.times = {18 * 60};
        t.on = true;
        const int64_t morning = utc(2027, 1, 15, 18, 20);                                     // 10:20 Pacific
        ASSERT_EQ(bot_schedule_planned_next(t, morning), utc(2027, 1, 16, 2, 0));
        ASSERT_EQ(bot_schedule_resumed_next(t, morning), utc(2027, 1, 16, 2, 0));             // (a restart at 10:20 does not make up 18:00 of yesterday: the next 18:00)
        ASSERT_EQ(bot_schedule_next_after_run(t, utc(2027, 1, 16, 2, 0) + 7), utc(2027, 1, 17, 2, 0));       // (a run a few seconds late: the next day's)
        ASSERT_EQ(bot_schedule_next_after_run(t, utc(2027, 1, 20, 2, 0) + 90000), utc(2027, 1, 22, 2, 0));  // (a server that was stopped for days: no catching up, the next 18:00 that is still to come)
    } TEST_END();

    TEST_CASE("BSC4.1 The default schedule is made from --bot-match-every-min once: 'Hourly rotation' for 60 minutes, every choice open, the first run a minute away; 5 to 1440 only; a server without a database makes it at every start") {
        BotScheduleStore memory(nullptr);
        std::string why;
        ASSERT_TRUE(memory.open(kNow, why));
        for (const int bad : {0, 4, 1441, -5}) ASSERT_FALSE(memory.seed_default(bad, kNow));
        ASSERT_TRUE(memory.list().empty());
        ASSERT_TRUE(memory.seed_default(60, kNow));
        ASSERT_EQ(memory.list().size(), size_t{1});
        const BotSchedule& d = memory.list()[0];
        ASSERT_TRUE(d.id == 1 && d.name == "Hourly rotation" && d.is_default && d.on && d.kind == BotSchedule::Kind::Every && d.minutes == 60 && d.next_run == kNow + 60 && d.last_run == 0);
        ASSERT_EQ(ctl::to_json(bot_what_json(d.what)), std::string("{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"random\",\"levels\":[\"rmh\",\"rmh\",\"rmh\",\"rmh\"],\"teams\":\"random\",\"split\":\"\"}"));
        ASSERT_FALSE(memory.seed_default(60, kNow));                                           // (a list that has a schedule is not seeded again)
        BotScheduleStore other(nullptr);
        ASSERT_TRUE(other.seed_default(45, kNow) && other.list()[0].name == "Rotation, every 45 minutes" && other.list()[0].minutes == 45);
        BotScheduleStore again(nullptr);                                                       // (a server that starts again with no database: the same schedule, new)
        ASSERT_TRUE(again.seed_default(60, kNow + 99) && again.list()[0].next_run == kNow + 99 + 60);
    } TEST_END();

    TEST_CASE("BSC4.2 The store keeps 20 schedules at most and ids that are never used again; a change keeps what it does not say, a schedule that is turned on or whose time changes is planned again, off has no next run, a delete is gone") {
        BotScheduleStore store(nullptr);
        std::string why;
        ASSERT_TRUE(store.open(kNow, why));
        BotSchedule made;
        for (size_t i = 0; i < kMaxBotSchedules; ++i) {
            ASSERT_TRUE(store.create(patch_every("s" + std::to_string(i), 30), kNow, made, why));
            ASSERT_EQ(made.id, static_cast<int64_t>(i + 1));
        }
        ASSERT_FALSE(store.create(patch_every("one too many", 30), kNow, made, why));
        ASSERT_EQ(why, std::string("too_many"));
        ASSERT_TRUE(store.remove(20, why));
        ASSERT_TRUE(store.create(patch_every("again", 30), kNow, made, why) && made.id == 21);   // (a number that a deleted schedule had is not used again, with a database (BSC5.1) or without one)
        const int64_t id = 3;
        BotSchedulePatch rename;
        rename.has_name = true;
        rename.name = "renamed";
        BotSchedule changed;
        const int64_t planned = store.find(id)->next_run;
        ASSERT_TRUE(store.update(id, rename, kNow + 500, changed, why));
        ASSERT_TRUE(changed.name == "renamed" && changed.next_run == planned && changed.minutes == 30 && changed.on);                 // (a new name does not move the next run)
        BotSchedulePatch off;
        off.has_on = true;
        off.on = false;
        ASSERT_TRUE(store.update(id, off, kNow + 600, changed, why) && !changed.on && changed.next_run == 0);
        BotSchedulePatch on;
        on.has_on = true;
        on.on = true;
        ASSERT_TRUE(store.update(id, on, kNow + 700, changed, why) && changed.on && changed.next_run == kNow + 700 + 1800);          // (turned on: planned from now)
        BotSchedulePatch moved = patch_times("renamed", {18 * 60});
        moved.has_name = false;
        moved.has_what = false;
        ASSERT_TRUE(store.update(id, moved, kNow + 800, changed, why) && changed.kind == BotSchedule::Kind::Times && changed.next_run == next_pacific_run(kNow + 800, {18 * 60}) && changed.name == "renamed");
        ASSERT_TRUE(store.remove(id, why) && store.find(id) == nullptr);
        ASSERT_FALSE(store.remove(id, why));
        ASSERT_EQ(why, std::string("gone"));
        ASSERT_FALSE(store.update(id, rename, kNow, changed, why));
        ASSERT_EQ(why, std::string("gone"));
        store.note_run(4, kNow + 900, kNow + 4500);
        ASSERT_TRUE(store.find(4)->last_run == kNow + 900 && store.find(4)->next_run == kNow + 4500);
        ASSERT_EQ(kMaxBotScheduleTimes, size_t{12});
        ASSERT_TRUE(kMinBotEveryMinutes == 5 && kMaxBotEveryMinutes == 1440);
    } TEST_END();

    TEST_CASE("BSC5.1 The rows are in the database and a restart reads them back (the same name, when, what, last start); the next run of a schedule that is on is the later of what was kept and a minute from now, and a time of the day is the next one by the clock; off stays off; the default is made once and a deleted one stays deleted; ids are never reused") {
        TempDir dir;
        const std::string file = (dir.path / "ants.db").string();
        int64_t ahead_id = 0, past_id = 0, times_id = 0, off_id = 0;
        const int64_t later = kNow + 3 * 3600;                                                   // (three hours later: the server was down)
        {
            db::Database database;
            ASSERT_TRUE(open_server_db(database, file));
            BotScheduleStore store(&database);
            std::string why;
            ASSERT_TRUE(store.open(kNow, why));
            ASSERT_TRUE(store.seed_default(60, kNow));                                           // (the first start: the default is made, with its meta row)
            ASSERT_EQ(scalar(database, "SELECT COUNT(*) FROM meta WHERE key = 'bot_schedules_seeded'"), int64_t{1});
            ASSERT_FALSE(store.seed_default(60, kNow));
            BotSchedule s;
            ASSERT_TRUE(store.create(patch_every("ahead", 120), kNow, s, why));
            ahead_id = s.id;
            ASSERT_TRUE(store.create(patch_every("past", 30), kNow, s, why));
            past_id = s.id;
            ASSERT_TRUE(store.create(patch_times("times", {10 * 60, 18 * 60 + 30}), kNow, s, why));
            times_id = s.id;
            BotSchedulePatch p = patch_every("off", 15);
            p.has_on = true;
            p.on = false;
            ASSERT_TRUE(store.create(p, kNow, s, why));
            off_id = s.id;
            store.note_run(past_id, kNow + 100, kNow + 100 + 1800);
            store.note_run(ahead_id, kNow + 50, later + 5000);
            database.close();
        }
        {
            db::Database database;
            ASSERT_TRUE(open_server_db(database, file));
            BotScheduleStore store(&database);
            std::string why;
            ASSERT_TRUE(store.open(later, why));
            ASSERT_EQ(store.list().size(), size_t{5});
            const BotSchedule* def = store.find(1);
            ASSERT_TRUE(def != nullptr && def->is_default && def->name == "Hourly rotation" && def->minutes == 60 && def->next_run == later + 60);                       // (due while down: a minute after the start, not made up)
            const BotSchedule* ahead = store.find(ahead_id);
            ASSERT_TRUE(ahead != nullptr && ahead->name == "ahead" && ahead->last_run == kNow + 50 && ahead->next_run == later + 5000);                              // (a run that is still ahead stays)
            const BotSchedule* past = store.find(past_id);
            ASSERT_TRUE(past != nullptr && past->last_run == kNow + 100 && past->next_run == later + 60);
            const BotSchedule* times = store.find(times_id);
            ASSERT_TRUE(times != nullptr && times->kind == BotSchedule::Kind::Times && times->times == (std::vector<int>{10 * 60, 18 * 60 + 30}) && times->next_run == next_pacific_run(later, {10 * 60, 18 * 60 + 30}));
            const BotSchedule* off = store.find(off_id);
            ASSERT_TRUE(off != nullptr && !off->on && off->next_run == 0);
            ASSERT_EQ(ctl::to_json(bot_what_json(def->what)), ctl::to_json(bot_what_json(BotWhat())));        // (what was kept is what was made)
            ASSERT_FALSE(store.seed_default(60, later));                                          // (it has rows)
            // the admin deletes the default and everything else; a restart makes none of them again
            for (const int64_t id : {int64_t{1}, ahead_id, past_id, times_id, off_id}) ASSERT_TRUE(store.remove(id, why));
            ASSERT_TRUE(store.list().empty());
            database.close();
        }
        {
            db::Database database;
            ASSERT_TRUE(open_server_db(database, file));
            BotScheduleStore store(&database);
            std::string why;
            ASSERT_TRUE(store.open(kNow + 99999, why));
            ASSERT_TRUE(store.list().empty());
            ASSERT_FALSE(store.seed_default(60, kNow + 99999));                                   // (the default was made once: a schedule that an admin deleted is not made again)
            ASSERT_TRUE(store.list().empty());
            BotSchedule s;
            ASSERT_TRUE(store.create(patch_every("later", 60), kNow + 99999, s, why));
            ASSERT_GT(s.id, off_id);                                                              // (the number of a deleted schedule is not used again)
            database.close();
        }
        {   // a row that cannot be read is left out, with a line for the log, and the rest is as it was
            db::Database database;
            ASSERT_TRUE(open_server_db(database, file));
            ASSERT_TRUE(database.exec("INSERT INTO bot_schedules (name, kind, what, created_at) VALUES ('broken', 'every', 'not json', 1)").ok());
            BotScheduleStore store(&database);
            std::string why;
            ASSERT_TRUE(store.open(kNow, why));
            ASSERT_EQ(store.list().size(), size_t{1});
            ASSERT_EQ(store.take_notices().size(), size_t{1});
            database.close();
        }
    } TEST_END();

    TEST_CASE("BSC5.2 A table that the database does not have is told, not crashed on; a database that refuses a row stops the change that asked for it and leaves the list as it was") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        BotScheduleStore store(&database);
        std::string why;
        ASSERT_TRUE(store.open(kNow, why));
        BotSchedule s;
        ASSERT_TRUE(store.create(patch_every("first", 30), kNow, s, why));
        ASSERT_TRUE(database.exec("DROP TABLE bot_schedules").ok());
        ASSERT_FALSE(store.create(patch_every("second", 30), kNow, s, why));
        ASSERT_EQ(why, std::string("db"));
        ASSERT_EQ(store.list().size(), size_t{1});                                                // (nothing was added to the list)
        BotSchedulePatch rename;
        rename.has_name = true;
        rename.name = "renamed";
        ASSERT_FALSE(store.update(1, rename, kNow, s, why));
        ASSERT_TRUE(why == "db" && store.find(1)->name == "first");                               // (nor changed)
        ASSERT_FALSE(store.remove(1, why));
        ASSERT_TRUE(why == "db" && store.find(1) != nullptr);
        BotScheduleStore fresh(&database);
        ASSERT_FALSE(fresh.open(kNow, why));
        ASSERT_FALSE(why.empty());
    } TEST_END();

    TEST_CASE("BSC6.1 The migration to version 9 from a copy of a version 8 database: the table is there and empty, every row of the old tables is as it was, a second start changes nothing, and an older program that does not know version 9 refuses the file") {
        TempDir dir;
        const std::string live = (dir.path / "live.db").string();
        const std::vector<db::Migration> every = db_migrations();
        ASSERT_TRUE(every.size() >= 9 && every[7].version == 8 && every[8].version == 9);
        const std::vector<db::Migration> v8(every.begin(), every.begin() + 8);
        {
            db::Database old;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(old, live, v8, db::OpenOptions(), &version).ok() && version == 8);
            ASSERT_EQ(scalar(old, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'bot_schedules'"), int64_t{0});
            ASSERT_TRUE(old.exec("INSERT INTO admin_audit (at, actor, actor_ref, action, target, detail) VALUES (10, 'dave', '0123456789abcdef', 'map_approved', 'Run One', '')").ok());
            ASSERT_TRUE(old.exec("INSERT INTO meta (key, value) VALUES ('submission_salt', 'keepme')").ok());
            ASSERT_TRUE(old.exec("INSERT INTO site_stats VALUES ('online', 5)").ok());
            old.close();
        }
        {
            db::Database db9;
            int version = 0;
            ASSERT_TRUE(db::open_and_migrate(db9, live, every, db::OpenOptions(), &version).ok() && version == 9 && version == db_schema_version());
            ASSERT_EQ(scalar(db9, "SELECT COUNT(*) FROM bot_schedules"), int64_t{0});
            ASSERT_EQ(scalar(db9, "SELECT COUNT(*) FROM admin_audit"), int64_t{1});
            ASSERT_EQ(text_of(db9, "SELECT actor || '|' || action || '|' || target FROM admin_audit"), std::string("dave|map_approved|Run One"));
            ASSERT_EQ(text_of(db9, "SELECT value FROM meta WHERE key = 'submission_salt'"), std::string("keepme"));
            ASSERT_EQ(scalar(db9, "SELECT COUNT(*) FROM meta WHERE key = 'bot_schedules_seeded'"), int64_t{0});                   // (the default is not made by the migration: the server makes it when it starts, once)
            ASSERT_EQ(text_of(db9, "PRAGMA integrity_check"), std::string("ok"));
            BotScheduleStore store(&db9);
            std::string why;
            ASSERT_TRUE(store.open(kNow, why) && store.seed_default(60, kNow) && store.list().size() == 1);
            db9.close();
            db::Database again;
            version = 0;
            ASSERT_TRUE(db::open_and_migrate(again, live, every, db::OpenOptions(), &version).ok() && version == 9);              // (idempotent)
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM schema_version"), int64_t{9});
            ASSERT_EQ(scalar(again, "SELECT COUNT(*) FROM bot_schedules"), int64_t{1});
            again.close();
            db::Database old_program;
            const db::Error refused = db::open_and_migrate(old_program, live, v8, db::OpenOptions());
            ASSERT_TRUE(!refused.ok() && refused.code == db::kErrNewerSchema);
        }
    } TEST_END();

    TEST_CASE("BSC6.2 The table refuses rows that no schedule is: a name that is empty, a kind of another word, minutes out of range, a flag that is not 0 or 1") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        const std::string good = "'g', 1, 0, 'every', 60, '[]', '{}', 0, 0, 5";
        ASSERT_TRUE(database.exec("INSERT INTO bot_schedules (name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at) VALUES (" + good + ")").ok());
        for (const char* bad : {"'', 1, 0, 'every', 60, '[]', '{}', 0, 0, 5", "'g', 2, 0, 'every', 60, '[]', '{}', 0, 0, 5", "'g', 1, 0, 'weekly', 60, '[]', '{}', 0, 0, 5", "'g', 1, 0, 'every', 4, '[]', '{}', 0, 0, 5", "'g', 1, 0, 'every', 1441, '[]', '{}', 0, 0, 5"}) {
            ASSERT_FALSE(database.exec(std::string("INSERT INTO bot_schedules (name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at) VALUES (") + bad + ")").ok());
        }
        ASSERT_EQ(scalar(database, "SELECT COUNT(*) FROM bot_schedules"), int64_t{1});
    } TEST_END();


    TEST_CASE("BSC7.1 A time that cannot be true is mended: a last start more than an hour after now was never made and is ignored; a next run later than one period from now (every N minutes) or later than the next time of the list (times of the day) is brought back to it; a schedule that is off has none; when the rows are read and once a minute") {
        db::Database database;
        ASSERT_TRUE(open_server_db(database, ""));
        const int64_t far = kNow + 10 * 365 * 86400;
        const auto row = [&](const std::string& name, const std::string& kind, int minutes, const std::string& times, int enabled, int64_t last, int64_t next) {
            db::Statement st;
            ASSERT_TRUE(database.prepare(st, "INSERT INTO bot_schedules (name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at) VALUES (?, ?, 0, ?, ?, ?, '{\"count\":1,\"map\":\"random:rotation\",\"rules\":\"random\",\"bots\":\"random\",\"levels\":[],\"teams\":\"random\",\"split\":\"\"}', ?, ?, 1)").ok());
            ASSERT_TRUE(st.bind_text(1, name).ok() && st.bind_int64(2, enabled).ok() && st.bind_text(3, kind).ok() && st.bind_int64(4, minutes).ok() && st.bind_text(5, times).ok() && st.bind_int64(6, last).ok() && st.bind_int64(7, next).ok());
            ASSERT_TRUE(st.step() == db::Statement::Step::Done);
        };
        row("far every", "every", 30, "[]", 1, far, far);                                       // 1: both times are ten years away
        row("slightly ahead", "every", 30, "[]", 1, kNow + 100, kNow + 100 + 1800);              // 2: a clock that was a little ahead: the last start is believed
        row("far times", "times", 60, "[\"18:00\"]", 1, far, far);                               // 3
        row("far but off", "every", 30, "[]", 0, far, far);                                      // 4
        row("none", "every", 30, "[]", 1, 0, 0);                                                 // 5: no next run kept
        BotScheduleStore store(&database);
        std::string why;
        ASSERT_TRUE(store.open(kNow, why) && store.list().size() == 5);
        ASSERT_TRUE(store.find(1)->last_run == 0 && store.find(1)->next_run >= kNow + 60 && store.find(1)->next_run <= kNow + 30 * 60);
        ASSERT_TRUE(store.find(2)->last_run == kNow + 100 && store.find(2)->next_run == kNow + 1800);                  // (the last start is believed; the next run is held to one period from now)
        ASSERT_TRUE(store.find(3)->last_run == 0 && store.find(3)->next_run == next_pacific_run(kNow, {18 * 60}));
        ASSERT_TRUE(store.find(4)->next_run == 0);
        ASSERT_TRUE(store.find(5)->next_run >= kNow + 60 && store.find(5)->next_run <= kNow + 30 * 60);
        // while the server runs: a time that went wrong (a clock that jumped) is mended in the minute, and written
        store.note_run(2, far, far);
        store.note_run(3, kNow, far);
        ASSERT_EQ(store.repair_clock(kNow + 10), size_t{2});
        ASSERT_TRUE(store.find(2)->last_run == 0 && store.find(2)->next_run <= kNow + 10 + 30 * 60 && store.find(2)->next_run > kNow);
        ASSERT_TRUE(store.find(3)->next_run == next_pacific_run(kNow + 10, {18 * 60}));
        ASSERT_EQ(store.repair_clock(kNow + 20), size_t{0});                                      // (nothing is wrong now: nothing is touched)
        BotScheduleStore again(&database);
        ASSERT_TRUE(again.open(kNow + 30, why) && again.find(2)->last_run == 0);
        // the pieces
        BotSchedule s;
        s.on = true;
        s.minutes = 5;
        s.last_run = far;
        s.next_run = far;
        ASSERT_TRUE(bot_schedule_repair_clock(s, kNow) && s.last_run == 0 && s.next_run >= kNow + 60 && s.next_run <= kNow + 300);
        s.next_run = kNow + 120;
        ASSERT_FALSE(bot_schedule_repair_clock(s, kNow));
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 120);
        s.next_run = far;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 300);                                // (the latest: one period)
        s.next_run = kNow - 5000;
        ASSERT_EQ(bot_schedule_resumed_next(s, kNow), kNow + 60);                                 // (the earliest: a minute)
    } TEST_END();

    TEST_CASE("BSC7.2 The same save a moment later is the same schedule: a schedule that is the same in every choice as the newest one, made within kSameScheduleWindowS, is not made again (also when the 20 are there), one that differs in any choice or comes later is another; a save that changes nothing writes nothing and keeps the next run") {
        BotScheduleStore store(nullptr);
        std::string why;
        ASSERT_TRUE(store.open(kNow, why));
        BotSchedule a, b;
        bool repeated = true;
        BotSchedulePatch p = patch_every("Swarm", 30);
        p.what.count = 3;
        ASSERT_TRUE(store.create(p, kNow, a, why, &repeated) && !repeated);
        ASSERT_TRUE(store.create(p, kNow + kSameScheduleWindowS, b, why, &repeated) && repeated && b.id == a.id && store.list().size() == 1);
        BotSchedulePatch renamed = p;
        renamed.name = "Swarm 2";
        ASSERT_TRUE(store.create(renamed, kNow + 1, b, why, &repeated) && !repeated && b.id != a.id);
        BotSchedulePatch other_time = p;
        other_time.minutes = 45;
        ASSERT_TRUE(store.create(other_time, kNow + 2, b, why, &repeated) && !repeated);
        BotSchedulePatch other_choice = p;
        other_choice.what.count = 4;
        ASSERT_TRUE(store.create(other_choice, kNow + 3, b, why, &repeated) && !repeated);
        BotSchedulePatch off = p;
        off.has_on = true;
        off.on = false;
        ASSERT_TRUE(store.create(off, kNow + 4, b, why, &repeated) && !repeated);                 // (off is not on: another schedule)
        ASSERT_EQ(store.list().size(), size_t{5});
        ASSERT_TRUE(store.create(p, kNow + 5 + kSameScheduleWindowS + 1, b, why, &repeated) && !repeated && store.list().size() == 6);        // (later: a schedule of its own)
        ASSERT_TRUE(store.create(p, kNow + 5 + kSameScheduleWindowS + 2, b, why, &repeated) && repeated && store.list().size() == 6);
        for (size_t i = store.list().size(); i < kMaxBotSchedules; ++i) ASSERT_TRUE(store.create(patch_every("fill " + std::to_string(i), 30), kNow + 100, b, why, &repeated) && !repeated);
        ASSERT_EQ(store.list().size(), kMaxBotSchedules);
        BotSchedule newest;
        BotSchedulePatch last = patch_every("the last", 30);
        ASSERT_FALSE(store.create(last, kNow + 101, newest, why, &repeated));                       // (the 21st: refused)
        ASSERT_EQ(why, std::string("too_many"));
        // a change that changes nothing: no write, no new plan, and it says so
        BotScheduleStore s2(nullptr);
        ASSERT_TRUE(s2.open(kNow, why) && s2.create(p, kNow, a, why));
        s2.note_run(a.id, kNow + 400, kNow + 400 + 1800);
        BotSchedulePatch same = p;
        bool changed = true;
        BotSchedule out;
        ASSERT_TRUE(s2.update(a.id, same, kNow + 500, out, why, &changed) && !changed && out.next_run == kNow + 400 + 1800);
        BotSchedulePatch on_again;
        on_again.has_on = true;
        on_again.on = true;
        ASSERT_TRUE(s2.update(a.id, on_again, kNow + 501, out, why, &changed) && !changed && out.next_run == kNow + 400 + 1800);
        BotSchedulePatch other_minutes = p;
        other_minutes.minutes = 31;
        ASSERT_TRUE(s2.update(a.id, other_minutes, kNow + 600, out, why, &changed) && changed && out.minutes == 31 && out.next_run == kNow + 400 + 31 * 60);                  // (its time changed: planned again, from the last start)
    } TEST_END();

    TEST_CASE("BSC7.3 Without a database the numbers of the schedules go up and are never used again, as with one: the default is 1, a deleted number stays deleted, a restart of the process starts again (the rows were only in memory)") {
        BotScheduleStore store(nullptr);
        std::string why;
        ASSERT_TRUE(store.open(kNow, why) && store.seed_default(60, kNow) && store.find(1) != nullptr);
        BotSchedule a, b, c;
        ASSERT_TRUE(store.create(patch_every("a", 30), kNow, a, why) && a.id == 2);
        ASSERT_TRUE(store.create(patch_every("b", 30), kNow, b, why) && b.id == 3);
        ASSERT_TRUE(store.remove(3, why) && store.remove(2, why));
        ASSERT_TRUE(store.create(patch_every("c", 30), kNow, c, why) && c.id == 4);
        ASSERT_TRUE(store.remove(4, why));
        ASSERT_TRUE(store.create(patch_every("d", 30), kNow, c, why) && c.id == 5);
    } TEST_END();

    std::cout << "\n" << g_test_count << " cases, " << g_assert_count << " assertions, " << g_test_failures << " failed\n";
    return g_test_failures == 0 ? 0 : 1;
}
