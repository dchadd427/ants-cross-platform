// The tests of the site statistics (ants_server/site_stats.hpp): the counting rules, the window of 24 hours with a clock that the test moves, the cap on single-player reports, the file
// (kept, atomic, put aside when it is no usable file), the JSON of GET /stats, a real room manager (a match that ran and ended counts once, a room that never began counts nothing) and the
// door over real sockets. Included by test_server.cpp, which holds the harness (TEST_CASE, ASSERT_*, World, Client) and calls run_site_stats_tests().
#pragma once

namespace {

// A clock that the test moves: seconds since the epoch. T0 is 2026-10-04 12:34:56 UTC (the hour number 497532).
constexpr int64_t kT0 = 1791117296;
constexpr int64_t kH = 3600;

struct StatsClock {
    int64_t now{kT0};
    SiteStats::Clock fn() { return [this]() { return now; }; }
};

std::string stats_slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void stats_write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// A statistics file as the program writes it (the pieces can be replaced to make a broken one)
std::string stats_file_text(const std::string& since = "2026-10-04", const std::string& online = "{\"total\":5,\"hours\":[[497532,3],[497531,2]]}",
                            const std::string& local = "{\"total\":0,\"hours\":[]}", const std::string& format = "1") {
    return "{\"format\":" + format + ",\"since\":\"" + since + "\",\"online\":" + online + ",\"local\":" + local + "}\n";
}

RoomStatus ended_room(const std::string& code, RoomState state, uint32_t ticks) {
    RoomStatus s;
    s.code = code;
    s.state = state;
    s.ticks = ticks;
    return s;
}

bool notice_has(const std::vector<std::string>& notices, const std::string& text) {
    for (const std::string& n : notices) {
        if (n.find(text) != std::string::npos) return true;
    }
    return false;
}

std::vector<fs::path> stats_files_in(const fs::path& dir, const std::string& prefix) {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().filename().string().rfind(prefix, 0) == 0) out.push_back(e.path());
    }
    return out;
}

#ifndef _WIN32
// One request to a listener on this machine: sent whole, the answer read until the server closes (or 3 s), while the listener is polled (the server and this client share a thread)
std::string http_exchange(net::WsListener& listener, const std::string& request) {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return "";
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(listener.port());
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return "";
    }
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));                         // (macOS: no SIGPIPE on a dead peer)
#endif
    ::send(s, request.data(), request.size(), flags);
    std::string answer;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool closed = false;
    while (!closed && std::chrono::steady_clock::now() < deadline) {
        while (listener.accept() != nullptr) {
        }
        char buf[2048];
        const ssize_t n = ::recv(s, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) answer.append(buf, static_cast<size_t>(n));
        else if (n == 0) closed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ::close(s);
    return answer;
}
#endif

}  // namespace

// The program's own fsync, for the tests that want to see it called: every call goes on to the real one (the system call); while a test has armed the spy, a call for the watched file is
// recorded (what that file and the one beside it held at that moment) and can be made to fail. Linux only, and not under a sanitizer (which has an fsync of its own).
#if defined(__linux__)
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#elif defined(__has_feature)
#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer)
#define ANTS_FSYNC_SPY 1
#endif
#else
#define ANTS_FSYNC_SPY 1
#endif
#endif

#ifdef ANTS_FSYNC_SPY
#include <sys/syscall.h>

struct FsyncSpy {
    static inline std::atomic<bool> armed{false};
    static inline std::string watch;                // the real path of the file whose sync is recorded
    static inline std::string beside;               // a file whose text is read at the same moment
    static inline int calls = 0;
    static inline std::string watched_text;
    static inline std::string beside_text;
    static inline bool fail = false;                // the sync of the watched file fails (EIO)
    static inline bool interrupt_first = false;     // the first sync of the watched file is interrupted by a signal (EINTR)
};

extern "C" int fsync(int fd) {
    if (FsyncSpy::armed.load()) {
        char link[64];
        std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        char target[4096];
        const ssize_t n = ::readlink(link, target, sizeof(target) - 1);
        if (n > 0 && std::string(target, static_cast<size_t>(n)) == FsyncSpy::watch) {
            ++FsyncSpy::calls;
            FsyncSpy::watched_text = stats_slurp(FsyncSpy::watch);
            FsyncSpy::beside_text = stats_slurp(FsyncSpy::beside);
            if (FsyncSpy::fail) {
                errno = EIO;
                return -1;
            }
            if (FsyncSpy::interrupt_first && FsyncSpy::calls == 1) {
                errno = EINTR;
                return -1;
            }
        }
    }
    return static_cast<int>(::syscall(SYS_fsync, fd));
}
#endif

void run_site_stats_tests() {
    TEST_CASE("S3.140 What counts online: a room whose match ran at least 600 ticks (30 s of play) and ended is one game, whatever ended it, public rooms too; a shorter match and a room that never began are nothing (a start-and-quit loop cannot pad the number); the day and the total agree and the local counter is not touched") {
        StatsClock clock;
        SiteStats stats(clock.fn());
        ASSERT_EQ(SiteStats::kMinTicks, 600u);                                                  // 30 seconds at 20 ticks a second
        ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 0 && stats.local().day == 0 && stats.local().total == 0);
        stats.count_ended(ended_room("MATCH-1", RoomState::Finished, 1200));                  // a match that was played to its end
        ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 1);
        stats.count_ended(ended_room("k7m2xq9p", RoomState::Finished, 5000));                  // a public room (every web match is one)
        ASSERT_TRUE(stats.online().day == 2 && stats.online().total == 2);
        stats.count_ended(ended_room("MATCH-2", RoomState::Failed, 777));                      // it ran and then failed (closed by the owner, a desync, the room's limit): it was played
        ASSERT_TRUE(stats.online().day == 3 && stats.online().total == 3);
        stats.count_ended(ended_room("MATCH-3", RoomState::Failed, 600));                      // exactly 30 s is a game ...
        ASSERT_TRUE(stats.online().day == 4 && stats.online().total == 4);
        stats.count_ended(ended_room("SHORT-1", RoomState::Finished, 599));                    // ... one tick less is not
        stats.count_ended(ended_room("SHORT-2", RoomState::Failed, 1));                        // a match that was quit at once
        stats.count_ended(ended_room("SHORT-3", RoomState::Finished, 77));
        stats.count_ended(ended_room("short001", RoomState::Finished, 300));                   // a public room quit after 15 s
        stats.count_ended(ended_room("NOBODY-1", RoomState::Failed, 0));                       // nobody came
        stats.count_ended(ended_room("lonely01", RoomState::Failed, 0));
        stats.count_ended(ended_room("SHUT-1", RoomState::Failed, 0));                         // closed while it waited, or during the dialog before the first tick
        stats.count_ended(ended_room("ODD-1", RoomState::Finished, 0));
        ASSERT_TRUE(stats.online().day == 4 && stats.online().total == 4);
        ASSERT_TRUE(stats.local().day == 0 && stats.local().total == 0);
        stats.count_online();                                                                   // (the plain count is one game, whatever it is told)
        ASSERT_TRUE(stats.online().day == 5 && stats.online().total == 5);
        // the numbers of a status are the only thing that decides: a name, a code or a map never does
        RoomStatus named = ended_room("", RoomState::Finished, 600);
        named.names[0] = "Ann";
        named.map = "TINY.LVL";
        stats.count_ended(named);
        ASSERT_EQ(stats.online().total, uint64_t{6});
        RoomStatus unnamed = ended_room("", RoomState::Finished, 599);
        unnamed.names[0] = "Ann";
        unnamed.map = "TINY.LVL";
        stats.count_ended(unnamed);
        ASSERT_EQ(stats.online().total, uint64_t{6});
        stats.count_ended(ended_room("LONG-1", RoomState::Finished, UINT32_MAX));              // the largest clock is a game too
        ASSERT_EQ(stats.online().total, uint64_t{7});
    } TEST_END();

    TEST_CASE("S3.141 The last 24 hours are 24 buckets of an hour on the server's clock: the hour that runs and the 23 before it; a game leaves the window when its hour is 24 hours old, the total never forgets, a bucket is used again a day later, and a clock that goes back loses nothing") {
        StatsClock clock;
        {
            SiteStats stats(clock.fn());
            stats.count_online();                                                               // 12:34:56, hour 497532
            ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 1);
            clock.now = kT0 + 23 * kH;                                                          // the hour 497555: still in the window of 497532 .. 497555
            ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 1);
            clock.now = kT0 + 24 * kH;                                                          // the hour 497556: the window is 497533 .. 497556
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 1);
            clock.now = kT0 + 1000 * kH;
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 1);
        }
        {   // the edge to the second: a game at the last second of an hour leaves with the first second of the hour a day later (the window is 23 hours and the part of this one)
            SiteStats stats(clock.fn());
            clock.now = 1791115200 + 3599;                                                      // 12:59:59
            stats.count_online();
            clock.now = 1791201599;                                                             // the next day 11:59:59
            ASSERT_EQ(stats.online().day, uint64_t{1});
            clock.now = 1791201600;                                                             // the next day 12:00:00
            ASSERT_EQ(stats.online().day, uint64_t{0});
            SiteStats first(clock.fn());                                                        // a game at the first second of an hour: the same bucket, the same leave time
            clock.now = 1791115200;
            first.count_online();
            clock.now = 1791201599;
            ASSERT_EQ(first.online().day, uint64_t{1});
            clock.now = 1791201600;
            ASSERT_EQ(first.online().day, uint64_t{0});
        }
        {   // one game an hour for 30 hours: the last 24 count; a bucket is used again by the hour a day later
            SiteStats stats(clock.fn());
            for (int i = 0; i < 30; ++i) {
                clock.now = kT0 + i * kH;
                stats.count_online();
                ASSERT_EQ(stats.online().day, static_cast<uint64_t>(std::min(i + 1, 24)));
                ASSERT_EQ(stats.online().total, static_cast<uint64_t>(i + 1));
            }
            clock.now = kT0 + 29 * kH;
            ASSERT_TRUE(stats.online().day == 24 && stats.online().total == 30);
            for (int k = 0; k < 4; ++k) stats.count_online();                                   // four more in this hour (a bucket holds many)
            ASSERT_TRUE(stats.online().day == 28 && stats.online().total == 34);
            clock.now = kT0 + 29 * kH + 24 * kH;                                                 // a day on: the hour of the four is out, the 23 hours before it are not there any more
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 34);
            stats.count_online();                                                               // the slot of the old hour holds the new one, not the sum
            ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 35);
        }
        {   // the two counters have their own buckets
            SiteStats stats(clock.fn());
            clock.now = kT0;
            stats.count_online();
            stats.count_online();
            ASSERT_TRUE(stats.local().day == 0 && stats.local().total == 0);
            ASSERT_TRUE(stats.count_local());
            clock.now = kT0 + 12 * kH;
            ASSERT_TRUE(stats.count_local());
            ASSERT_TRUE(stats.online().day == 2 && stats.local().day == 2 && stats.local().total == 2);
            clock.now = kT0 + 24 * kH;                                                           // the two online games (hour 497532) are out, the first local report too, the second is not
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 2 && stats.local().day == 1 && stats.local().total == 2);
        }
        {   // a clock that goes back: the games of the hours that have not come yet are not in the window (they are in the total), new ones count, and when the clock is back they are all there
            SiteStats stats(clock.fn());
            clock.now = kT0;
            stats.count_online();
            clock.now = kT0 - 3 * kH;
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 1);
            stats.count_online();
            ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 2);
            clock.now = kT0;
            ASSERT_TRUE(stats.online().day == 2 && stats.online().total == 2);
        }
        {   // before 1970 and at the epoch: the buckets of negative hours work, and the date is held to 1970-01-01
            clock.now = -1;
            SiteStats stats(clock.fn());
            ASSERT_EQ(stats.since(), std::string("1970-01-01"));
            stats.count_online();                                                               // the hour -1
            ASSERT_TRUE(stats.online().day == 1 && stats.online().total == 1);
            clock.now = 0;
            ASSERT_TRUE(stats.online().day == 1);
            clock.now = 22 * kH;                                                                 // the hour 22: the window is -1 .. 22
            ASSERT_TRUE(stats.online().day == 1);
            clock.now = 23 * kH;                                                                 // the hour 23: it is 0 .. 23
            ASSERT_TRUE(stats.online().day == 0);
            clock.now = -25 * kH;                                                                // (the bucket of the hour -1 is in the future now)
            ASSERT_TRUE(stats.online().day == 0 && stats.online().total == 1);
        }
    } TEST_END();

    TEST_CASE("S3.142 The `since` date is the UTC day on which the counters were made, in the one shape YYYY-MM-DD, through leap days, century years and the new year") {
        struct Row {
            int64_t seconds;
            const char* date;
        };
        const Row rows[] = {{kT0, "2026-10-04"},
                            {1791115200, "2026-10-04"},
                            {1791158399, "2026-10-04"},                                          // the last second of the day
                            {1791158400, "2026-10-05"},                                          // midnight
                            {1798761599, "2026-12-31"},
                            {1798761600, "2027-01-01"},
                            {1709251199, "2024-02-29"},                                          // a leap day
                            {1709251200, "2024-03-01"},
                            {951782400, "2000-02-29"},                                           // 2000 is a leap year
                            {4107542399, "2100-02-28"},                                          // 2100 is not
                            {4107542400, "2100-03-01"},
                            {0, "1970-01-01"},
                            {86399, "1970-01-01"},
                            {86400, "1970-01-02"},
                            {2147483648, "2038-01-19"},
                            {253402300799, "9999-12-31"},
                            {253402300800, "9999-12-31"},                                        // (held to the year 9999)
                            {-86400, "1970-01-01"}};                                             // (and to 1970)
        for (const Row& row : rows) {
            StatsClock clock;
            clock.now = row.seconds;
            SiteStats stats(clock.fn());
            ASSERT_MSG(stats.since() == row.date, std::to_string(row.seconds) + " gave " + stats.since());
        }
        ASSERT_EQ(SiteStats().since().size(), size_t{10});                                       // the system clock: a date of the same shape
        ASSERT_TRUE(SiteStats().since() >= std::string("2026-01-01"));
    } TEST_END();

    TEST_CASE("S3.143 The cap on single-player reports: at most 120 count in any 60 seconds, the others are refused (the answer is the same: S3.149), the oldest leaves the minute exactly 60 s later, a slow stream is never refused, a clock that goes back opens a fresh minute, and online games have no cap") {
        StatsClock clock;
        SiteStats stats(clock.fn());
        for (size_t i = 0; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(stats.count_local());
        ASSERT_TRUE(stats.local().total == 120 && stats.local().day == 120);
        ASSERT_FALSE(stats.count_local());                                                       // the 121st in the same second
        ASSERT_FALSE(stats.count_local());
        ASSERT_TRUE(stats.local().total == 120);                                                 // (not counted: nothing changed)
        clock.now = kT0 + 59;
        ASSERT_FALSE(stats.count_local());                                                       // 59 s later the first is still in the minute
        clock.now = kT0 + 60;
        for (size_t i = 0; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(stats.count_local());   // 60 s later all 120 are out of it: a new 120
        ASSERT_FALSE(stats.count_local());
        ASSERT_EQ(stats.local().total, uint64_t{240});
        // the cap leaves online games alone, and they leave it alone
        for (int i = 0; i < 1000; ++i) stats.count_online();
        ASSERT_TRUE(stats.online().total == 1000 && stats.local().total == 240);
        ASSERT_FALSE(stats.count_local());
        // a stream of one a second never reaches 120 in a minute (60 do): all are counted, for hours
        {
            StatsClock slow;
            SiteStats s(slow.fn());
            for (int i = 0; i < 5000; ++i) {
                slow.now = kT0 + i;
                ASSERT_TRUE(s.count_local());
            }
            ASSERT_EQ(s.local().total, uint64_t{5000});
            for (int i = 0; i < 59; ++i) ASSERT_TRUE(s.count_local());                          // (59 more in the last second: 60 + 59 = 119 in the minute, and one more makes 120)
            ASSERT_TRUE(s.count_local());
            ASSERT_FALSE(s.count_local());
        }
        // against a model: a random mix of bursts and quiet, the answer of every report is "fewer than 120 counted in the last 60 seconds" and nothing else
        {
            StatsClock c2;
            SiteStats s(c2.fn());
            std::vector<int64_t> counted;
            uint32_t rng = 12345;
            int64_t t = kT0;
            size_t refused = 0;
            for (int i = 0; i < 6000; ++i) {
                rng = rng * 1664525u + 1013904223u;
                const bool dense = ((i / 250) % 2) == 0;                                         // 250 reports in a burst (about 4 a second), then 250 slow ones
                t += dense ? static_cast<int64_t>((rng >> 16) % 4 == 0 ? 1 : 0) : static_cast<int64_t>(1 + (rng >> 16) % 3);
                c2.now = t;
                size_t in_minute = 0;
                for (const int64_t at : counted) in_minute += (at > t - 60 && at <= t) ? 1u : 0u;
                const bool expect = in_minute < 120;
                ASSERT_MSG(s.count_local() == expect, "report " + std::to_string(i) + " at " + std::to_string(t - kT0) + " s: " + std::to_string(in_minute) + " counted in the last minute");
                if (expect) counted.push_back(t);
                else ++refused;
            }
            ASSERT_EQ(s.local().total, static_cast<uint64_t>(counted.size()));
            ASSERT_TRUE(refused > 1000 && counted.size() > 3000);                                 // (the mix did both: it is a test of the cap, not of a quiet day)
        }
        // a clock that goes back: the minute starts afresh (120 more, no more than that), and the totals keep what was counted
        {
            StatsClock c3;
            SiteStats s(c3.fn());
            for (size_t i = 0; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(s.count_local());
            ASSERT_FALSE(s.count_local());
            c3.now = kT0 - 1000;
            for (size_t i = 0; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(s.count_local());
            ASSERT_FALSE(s.count_local());
            ASSERT_EQ(s.local().total, uint64_t{240});
        }
    } TEST_END();

    TEST_CASE("S3.144 The answer of GET /stats: exactly now, online, local and since, in that order, numbers and a date only; `now` is what /busy says; the longest it can be fits the status answer") {
        StatsClock clock;
        SiteStats stats(clock.fn());
        for (int i = 0; i < 3; ++i) stats.count_online();
        for (int i = 0; i < 7; ++i) ASSERT_TRUE(stats.count_local());
        clock.now = kT0 + 5 * kH;
        stats.count_online();
        BusyCounts busy;
        busy.matches = 2;
        busy.players = 5;
        const std::string text = stats.json(busy);
        ASSERT_EQ(text, std::string("{\"now\":{\"matches\":2,\"players\":5},\"online\":{\"day\":4,\"total\":4},\"local\":{\"day\":7,\"total\":7},\"since\":\"2026-10-04\"}"));
        ctl::JsonValue j;
        std::string why;
        ASSERT_TRUE(ctl::parse_json(text, j, &why));
        ASSERT_TRUE(j.is_object() && j.size() == 4 && j.keys() == std::vector<std::string>({"now", "online", "local", "since"}));
        ASSERT_TRUE(j.get("now").keys() == std::vector<std::string>({"matches", "players"}) && j.get("online").keys() == std::vector<std::string>({"day", "total"}) && j.get("local").keys() == std::vector<std::string>({"day", "total"}));
        ASSERT_TRUE(j.get("since").is_string());
        clock.now = kT0 + 40 * kH;                                                               // the day is read at the time of the question
        ASSERT_EQ(stats.json(BusyCounts()), std::string("{\"now\":{\"matches\":0,\"players\":0},\"online\":{\"day\":0,\"total\":4},\"local\":{\"day\":0,\"total\":7},\"since\":\"2026-10-04\"}"));
        // the longest answer: the largest numbers that a file can hold, the busiest `now`, the last date
        const fs::path dir = temp_dir_for("stats-json");
        stats_write(dir / "site-stats.json", stats_file_text("9999-12-31", "{\"total\":9223372036854775807,\"hours\":[]}", "{\"total\":9223372036854775807,\"hours\":[]}"));
        SiteStats big(clock.fn());
        big.open((dir / "site-stats.json").string());
        BusyCounts most;
        most.matches = 4294967295u;
        most.players = 4294967295u;
        const std::string longest = big.json(most);
        ASSERT_TRUE(longest.find("9223372036854775807") != std::string::npos && longest.find("4294967295") != std::string::npos && longest.find("9999-12-31") != std::string::npos);
        ASSERT_TRUE(longest.size() < net::kWsMaxStatusBytes);
        big.count_online();                                                                     // one more than a signed number holds: shown as the largest, never as a negative one
        ASSERT_TRUE(big.json(BusyCounts()).find("\"online\":{\"day\":1,\"total\":9223372036854775807}") != std::string::npos);
    } TEST_END();

    TEST_CASE("S3.145 The file: made at the first save (so that `since` outlives a restart), written whole and renamed (no temporary file is left), read back by the next start with every number, the day read at the new time, only the buckets of the window written; a save is due every 10 s while something changed, and at a stop") {
        const fs::path dir = temp_dir_for("stats-file");
        const std::string path = (dir / SiteStats::kFileName).string();
        StatsClock clock;
        {
            SiteStats stats(clock.fn());
            ASSERT_FALSE(stats.dirty());
            stats.open(path);
            std::vector<std::string> notices = stats.take_notices();
            ASSERT_TRUE(notices.size() == 1 && notice_has(notices, "is new, counting from 2026-10-04"));
            ASSERT_TRUE(stats.dirty() && !fs::exists(path));                                    // a new counter is written at its first save
            ASSERT_TRUE(stats.save_if_due());
            ASSERT_TRUE(fs::exists(path) && !stats.dirty());
            ASSERT_EQ(stats_slurp(path), std::string("{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":0,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}\n"));
            ASSERT_TRUE(stats_files_in(dir, "site-stats.json.").empty());                       // (no temporary file stays)
            ASSERT_FALSE(stats.save_if_due());                                                  // nothing changed: nothing is written
            // the throttle: the first change is written at the next look, the following ones at most every 10 s
            stats.count_online();
            ASSERT_TRUE(stats.dirty());
            ASSERT_FALSE(stats.save_if_due());                                                  // (the file was written this very second)
            clock.now += 9;
            ASSERT_FALSE(stats.save_if_due());
            clock.now += 1;
            ASSERT_TRUE(stats.save_if_due());                                                   // 10 s after the last write
            ASSERT_EQ(stats_slurp(path), std::string("{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":1,\"hours\":[[497532,1]]},\"local\":{\"total\":0,\"hours\":[]}}\n"));
            stats.count_online();
            ASSERT_TRUE(stats.count_local());
            clock.now += 3;
            ASSERT_FALSE(stats.save_if_due());
            ASSERT_TRUE(stats.dirty());
            ASSERT_TRUE(stats.save());                                                          // a stop writes at once, whatever the last 10 s did
            ASSERT_FALSE(stats.dirty());
            ASSERT_TRUE(stats.save());                                                          // (and again: nothing to do)
            ASSERT_TRUE(notice_has({stats_slurp(path)}, "\"online\":{\"total\":2,\"hours\":[[497532,2]]},\"local\":{\"total\":1,\"hours\":[[497532,1]]}"));
        }
        {   // the next start: every number comes back; the day is that of the new time
            SiteStats stats(clock.fn());
            clock.now = kT0 + 20 * kH;
            stats.open(path);
            ASSERT_TRUE(notice_has(stats.take_notices(), "read 2 online and 1 single-player games since 2026-10-04"));
            ASSERT_FALSE(stats.dirty());
            ASSERT_TRUE(stats.online().total == 2 && stats.online().day == 2 && stats.local().total == 1 && stats.local().day == 1 && stats.since() == "2026-10-04");
            clock.now = kT0 + 24 * kH;
            ASSERT_TRUE(stats.online().total == 2 && stats.online().day == 0 && stats.local().day == 0);
            stats.count_online();
            ASSERT_TRUE(stats.online().total == 3 && stats.online().day == 1);
            ASSERT_TRUE(stats.save());
            // only the buckets of the window are written: the two games of the old hour are in the total, not in the file's buckets
            ASSERT_EQ(stats_slurp(path), std::string("{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":3,\"hours\":[[497556,1]]},\"local\":{\"total\":1,\"hours\":[]}}\n"));
        }
        {   // `since` is the day of the first start and stays, a file that is there is not "new"
            SiteStats later(clock.fn());
            clock.now = kT0 + 90 * 24 * kH;
            later.open(path);
            ASSERT_EQ(later.since(), std::string("2026-10-04"));
            ASSERT_TRUE(notice_has(later.take_notices(), "since 2026-10-04"));
        }
        {   // without a file the counters live in memory: nothing is written, a save is nothing to do
            const fs::path quiet = fs::path(temp_dir_for("stats-memory"));
            SiteStats stats(clock.fn());
            stats.count_online();
            ASSERT_TRUE(stats.online().total == 1 && stats.dirty());
            ASSERT_FALSE(stats.save_if_due());
            ASSERT_TRUE(stats.save());
            ASSERT_TRUE(stats.take_notices().empty());
            ASSERT_TRUE(fs::is_empty(quiet));
        }
        {   // a file that was written by hand, with room to read: white space, another order, a key that this version does not know
            const fs::path hand = fs::path(temp_dir_for("stats-hand")) / "site-stats.json";
            stats_write(hand, "{\n  \"extra\": [1, 2],\n  \"local\": {\"hours\": [[497530, 4]], \"total\": 9},\n  \"online\": {\"total\": 0, \"hours\": []},\n  \"since\": \"2025-01-31\",\n  \"format\": 1\n}\n");
            clock.now = kT0;
            SiteStats stats(clock.fn());
            stats.open(hand.string());
            ASSERT_TRUE(stats.local().total == 9 && stats.local().day == 4 && stats.online().total == 0 && stats.since() == "2025-01-31" && !stats.dirty());
        }
        {   // two hours of one slot (a day apart) in a file: the later one is kept; a bucket of an hour to come is in the total and not in the day
            const fs::path hand = fs::path(temp_dir_for("stats-slot")) / "site-stats.json";
            stats_write(hand, stats_file_text("2026-10-04", "{\"total\":20,\"hours\":[[497532,3],[497556,5],[497540,7]]}"));
            clock.now = kT0 + 24 * kH;                                                          // the hour 497556
            SiteStats stats(clock.fn());
            stats.open(hand.string());
            ASSERT_TRUE(stats.online().total == 20 && stats.online().day == 12 && !stats.dirty());      // 497556 (5) and 497540 (7); 497532 gave its slot to 497556
            clock.now = kT0 - 5 * kH;                                                           // (all of them are in the future of this clock)
            ASSERT_TRUE(stats.online().total == 20 && stats.online().day == 0);
        }
        {   // a save with nothing changed writes nothing (a file that somebody removed is not made again), and a clock that goes back never holds a save back
            const std::string own = (fs::path(temp_dir_for("stats-due")) / SiteStats::kFileName).string();
            StatsClock back;
            SiteStats stats(back.fn());
            stats.open(own);
            ASSERT_TRUE(stats.save());
            ASSERT_TRUE(fs::exists(own));
            back.now += 30;
            ASSERT_FALSE(stats.save_if_due());                                                  // nothing changed, however long ago the last write was: nothing is written
            fs::remove(own);
            ASSERT_TRUE(stats.save());                                                          // (a save with nothing changed: nothing to do, the file that was removed is not made again)
            ASSERT_FALSE(fs::exists(own));
            stats.count_online();
            ASSERT_TRUE(stats.save_if_due());                                                   // something changed and the last write is 30 s old: due
            ASSERT_TRUE(fs::exists(own) && !stats.dirty());
            stats.count_online();
            ASSERT_FALSE(stats.save_if_due());                                                  // (written this very second)
            back.now -= 100;                                                                    // the clock was set back: the last write is in the future, the next is not held back for 100 s
            ASSERT_TRUE(stats.save_if_due());
            ASSERT_FALSE(stats.dirty());
        }
        {   // a report that counts makes the counters dirty (the file is written for it); one that the cap refuses changes nothing, so there is nothing to write
            const std::string own = (fs::path(temp_dir_for("stats-local-dirty")) / SiteStats::kFileName).string();
            StatsClock c;
            SiteStats stats(c.fn());
            stats.open(own);
            ASSERT_TRUE(stats.save());
            ASSERT_FALSE(stats.dirty());
            ASSERT_TRUE(stats.count_local());
            ASSERT_TRUE(stats.dirty());
            for (size_t i = 1; i < SiteStats::kLocalPerMinute; ++i) ASSERT_TRUE(stats.count_local());
            c.now += 20;
            ASSERT_TRUE(stats.save_if_due());
            ASSERT_FALSE(stats.dirty());
            ASSERT_FALSE(stats.count_local());                                                  // the 121st of the minute
            ASSERT_FALSE(stats.dirty());
            ASSERT_EQ(stats.local().total, uint64_t{120});
        }
        {   // the file holds the buckets of the window only: the bucket of an hour that has not come yet (the clock was set back) stays out of it
            const fs::path own = fs::path(temp_dir_for("stats-future")) / SiteStats::kFileName;
            StatsClock c;
            SiteStats stats(c.fn());
            stats.open(own.string());
            stats.count_online();                                                               // the hour 497532
            c.now = kT0 - 3 * kH;
            ASSERT_TRUE(stats.save());
            ASSERT_EQ(stats_slurp(own), std::string("{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":1,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}\n"));
        }
    } TEST_END();

    TEST_CASE("S3.146 A file that is no usable file is put aside whole (as <name>.broken-<seconds>), never overwritten and never read halfway: the counters start at 0 from today, the log says so, and the next save writes a good file beside it") {
        struct Bad {
            const char* label;
            std::string text;
        };
        const std::string big_spaces(20000, ' ');
        const std::vector<Bad> table = {
            {"empty", ""},
            {"garbage", "this is not json"},
            {"half a file", "{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":5,\"hours\":[[497532,3]"},
            {"not an object", "[1,2,3]"},
            {"null", "null"},
            {"trailing text", stats_file_text() + "x"},
            {"two documents", stats_file_text() + stats_file_text()},
            {"a byte order mark", std::string("\xEF\xBB\xBF") + stats_file_text()},
            {"too big", stats_file_text().substr(0, stats_file_text().size() - 2) + big_spaces + "}\n"},
            {"another format", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[]}", "{\"total\":0,\"hours\":[]}", "2")},
            {"no format", "{\"since\":\"2026-10-04\",\"online\":{\"total\":5,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}"},
            {"format as text", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[]}", "{\"total\":0,\"hours\":[]}", "\"1\"")},
            {"no date", "{\"format\":1,\"online\":{\"total\":5,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}"},
            {"a date of the wrong shape", stats_file_text("2026-10-4")},
            {"a date with a month 13", stats_file_text("2026-13-01")},
            {"a 31st of February", stats_file_text("2026-02-31")},
            {"the 29th of February of a year that has none", stats_file_text("2026-02-29")},
            {"a day 0", stats_file_text("2026-10-00")},
            {"a date before 1970", stats_file_text("1969-12-31")},
            {"a date with letters", stats_file_text("2026-1O-04")},
            {"a date with a digit too many", stats_file_text("2026-10-041")},
            {"a date with a blank behind it", stats_file_text("2026-10-04 ")},
            {"a date with a blank before it", stats_file_text(" 2026-10-04")},
            {"a date with a sign", stats_file_text("+026-10-04")},
            {"a date as a number", "{\"format\":1,\"since\":20261004,\"online\":{\"total\":5,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}"},
            {"no online counter", "{\"format\":1,\"since\":\"2026-10-04\",\"local\":{\"total\":0,\"hours\":[]}}"},
            {"no local counter", "{\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":0,\"hours\":[]}}"},
            {"a counter that is no object", stats_file_text("2026-10-04", "5")},
            {"a negative total", stats_file_text("2026-10-04", "{\"total\":-1,\"hours\":[]}")},
            {"a total with a fraction", stats_file_text("2026-10-04", "{\"total\":1.5,\"hours\":[]}")},
            {"a total as text", stats_file_text("2026-10-04", "{\"total\":\"5\",\"hours\":[]}")},
            {"no total", stats_file_text("2026-10-04", "{\"hours\":[]}")},
            {"no hours", stats_file_text("2026-10-04", "{\"total\":5}")},
            {"hours that are no list", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":{}}")},
            {"25 buckets", stats_file_text("2026-10-04", "{\"total\":100,\"hours\":[[1,1],[2,1],[3,1],[4,1],[5,1],[6,1],[7,1],[8,1],[9,1],[10,1],[11,1],[12,1],[13,1],[14,1],[15,1],[16,1],[17,1],[18,1],[19,1],[20,1],[21,1],[22,1],[23,1],[24,1],[25,1]]}")},
            {"a bucket of three numbers", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532,3,1]]}")},
            {"a bucket of one number", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532]]}")},
            {"a bucket that is no list", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[497532]}")},
            {"a bucket with a text", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[\"497532\",3]]}")},
            {"a bucket with a fraction", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532,2.5]]}")},
            {"a negative hour", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[-1,3]]}")},
            {"a negative count", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532,-3]]}")},
            {"buckets that hold more than the total", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532,3],[497531,3]]}")},
            {"one bucket that holds more than the total", stats_file_text("2026-10-04", "{\"total\":2,\"hours\":[[497532,3]]}")},
            {"the same hour twice", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[[497532,1],[497532,1]]}")},
            {"a bad second counter", stats_file_text("2026-10-04", "{\"total\":5,\"hours\":[]}", "{\"total\":-5,\"hours\":[]}")},
            {"a repeated key", "{\"format\":1,\"format\":1,\"since\":\"2026-10-04\",\"online\":{\"total\":5,\"hours\":[]},\"local\":{\"total\":0,\"hours\":[]}}"},
        };
        StatsClock clock;
        for (const Bad& bad : table) {
            const fs::path dir = temp_dir_for("stats-bad");
            const fs::path path = dir / SiteStats::kFileName;
            stats_write(path, bad.text);
            SiteStats stats(clock.fn());
            clock.now = kT0 + 3 * 86400;                                                       // (three days after the file was made: today is not the day in it)
            stats.open(path.string());
            const std::vector<std::string> notices = stats.take_notices();
            const std::string aside = path.string() + ".broken-" + std::to_string(clock.now);
            ASSERT_MSG(notices.size() == 1 && notice_has(notices, "cannot be used") && notice_has(notices, "put aside as " + aside) && notice_has(notices, "the counters start at 0 from 2026-10-07"), std::string(bad.label) + ": " + (notices.empty() ? "no notice" : notices[0]));
            ASSERT_MSG(fs::exists(aside) && stats_slurp(aside) == bad.text, std::string(bad.label) + ": the file is not kept whole beside");
            ASSERT_MSG(!fs::exists(path), std::string(bad.label) + ": the broken file is still in place");
            ASSERT_MSG(stats.online().total == 0 && stats.online().day == 0 && stats.local().total == 0 && stats.local().day == 0 && stats.since() == "2026-10-07", bad.label);
            ASSERT_TRUE(stats.dirty());                                                        // the counters are new: a good file is written at the first save
            stats.count_online();
            ASSERT_TRUE(stats.save());
            ASSERT_MSG(stats_slurp(aside) == bad.text, std::string(bad.label) + ": the save touched the broken file");
            SiteStats again(clock.fn());                                                       // and the next start reads the good file
            again.open(path.string());
            ASSERT_MSG(again.online().total == 1 && again.since() == "2026-10-07" && notice_has(again.take_notices(), "read 1 online"), bad.label);
            ASSERT_EQ(stats_files_in(dir, "site-stats.json.broken-").size(), size_t{1});
        }
        {   // a folder where the file belongs is put aside too (it is no file), and a link to a good file is followed
            const fs::path dir = temp_dir_for("stats-odd");
            fs::create_directories(dir / SiteStats::kFileName / "inside");
            clock.now = kT0;
            SiteStats stats(clock.fn());
            stats.open((dir / SiteStats::kFileName).string());
            ASSERT_TRUE(notice_has(stats.take_notices(), "it is not a regular file") && fs::is_directory(dir / (std::string(SiteStats::kFileName) + ".broken-" + std::to_string(kT0))));
            ASSERT_TRUE(stats.save() && fs::is_regular_file(dir / SiteStats::kFileName));
#ifndef _WIN32
            const fs::path other = fs::path(temp_dir_for("stats-link"));
            stats_write(other / "real.json", stats_file_text("2026-09-01", "{\"total\":8,\"hours\":[]}"));
            fs::create_symlink(other / "real.json", other / SiteStats::kFileName);
            SiteStats linked(clock.fn());
            linked.open((other / SiteStats::kFileName).string());
            ASSERT_TRUE(linked.online().total == 8 && linked.since() == "2026-09-01");
            // a pipe that nobody writes to would block an open for ever: it is put aside without being opened (the alarm ends a hang)
            const fs::path pipe_dir = fs::path(temp_dir_for("stats-fifo"));
            ASSERT_EQ(::mkfifo((pipe_dir / SiteStats::kFileName).c_str(), 0600), 0);
            const pid_t pid = ::fork();
            ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                ::alarm(5);
                SiteStats in_child(clock.fn());
                in_child.open((pipe_dir / SiteStats::kFileName).string());
                ::_exit(notice_has(in_child.take_notices(), "it is not a regular file") ? 0 : 3);
            }
            int wait_status = 0;
            ASSERT_EQ(::waitpid(pid, &wait_status, 0), pid);
            ASSERT_TRUE(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0);
#endif
        }
    } TEST_END();

    TEST_CASE("S3.147 A save that fails is said once, retried every 10 s (never every pass), and said to work again; nothing half written is left, the old file stays whole, and a failed put-aside does not stop the counters") {
        const fs::path dir = temp_dir_for("stats-fail");
        const std::string path = (dir / SiteStats::kFileName).string();
        StatsClock clock;
        SiteStats stats(clock.fn());
        stats.open(path);
        ASSERT_TRUE(stats.save());
        const std::string first = stats_slurp(path);
        stats.take_notices();
        // a folder in the place of the temporary file: the write fails
        fs::create_directories(path + ".tmp");
        stats.count_online();
        clock.now += 20;
        ASSERT_FALSE(stats.save_if_due());                                                      // it tried and failed
        std::vector<std::string> notices = stats.take_notices();
        ASSERT_TRUE(notices.size() == 1 && notice_has(notices, "could not be saved"));
        ASSERT_TRUE(stats.dirty() && stats_slurp(path) == first);                               // the old file is whole
        for (int pass = 0; pass < 1000; ++pass) ASSERT_FALSE(stats.save_if_due());              // (a pass every 2 ms: the disk is not asked again until 10 s have passed)
        clock.now += 9;
        ASSERT_FALSE(stats.save_if_due());
        ASSERT_TRUE(stats.take_notices().empty());
        clock.now += 1;
        ASSERT_FALSE(stats.save_if_due());                                                      // asked again after 10 s, failed again, not said again
        ASSERT_TRUE(stats.take_notices().empty());
        fs::remove_all(path + ".tmp");                                                          // the disk is well again
        clock.now += 10;
        ASSERT_TRUE(stats.save_if_due());
        notices = stats.take_notices();
        ASSERT_TRUE(notices.size() == 1 && notice_has(notices, "saved again"));
        ASSERT_TRUE(!stats.dirty() && stats_slurp(path) != first && stats_files_in(dir, "site-stats.json.").empty());
        // a failure again after the good time is a new first failure
        stats.count_online();
        fs::create_directories(path + ".tmp");
        clock.now += 10;
        ASSERT_FALSE(stats.save_if_due());
        ASSERT_TRUE(notice_has(stats.take_notices(), "could not be saved"));
        // a stop saves even when the last try was a moment ago, and says so when it cannot
        ASSERT_FALSE(stats.save());
        fs::remove_all(path + ".tmp");
        ASSERT_TRUE(stats.save());
        ASSERT_TRUE(notice_has(stats.take_notices(), "saved again"));
        // a rename that fails removes the temporary file: a folder where the file belongs
        {
            const fs::path odd = fs::path(temp_dir_for("stats-rename"));
            SiteStats s(clock.fn());
            s.open((odd / SiteStats::kFileName).string());
            s.take_notices();
            fs::create_directories(odd / SiteStats::kFileName / "inside");                      // (made after the open: the file is "new", and then something sits in its place)
            ASSERT_FALSE(s.save());
            ASSERT_TRUE(notice_has(s.take_notices(), "could not be saved") && !fs::exists(odd / (std::string(SiteStats::kFileName) + ".tmp")));
        }
        // a folder that is not there: the counters count in memory, the file is tried every 10 s, the first failure is said once
        {
            SiteStats s(clock.fn());
            s.open((dir / "no-such-folder" / SiteStats::kFileName).string());
            ASSERT_TRUE(notice_has(s.take_notices(), "is new"));
            s.count_online();
            ASSERT_FALSE(s.save());
            ASSERT_TRUE(notice_has(s.take_notices(), "could not be saved"));
            ASSERT_TRUE(s.online().total == 1);
            fs::create_directories(dir / "no-such-folder");
            clock.now += 10;
            ASSERT_TRUE(s.save_if_due());
            ASSERT_TRUE(fs::exists(dir / "no-such-folder" / SiteStats::kFileName));
        }
    } TEST_END();

#ifdef ANTS_FSYNC_SPY
    TEST_CASE("S3.150 The counters' file is on the disk before it has its name: the temporary file holds the whole new text and is fsync'ed (once), and only then renamed; a sync that fails removes the temporary file, leaves the old file whole and is said, and the next try saves; a sync that a signal interrupts is asked again") {
        const fs::path dir = fs::canonical(temp_dir_for("stats-sync"));
        const std::string path = (dir / SiteStats::kFileName).string();
        StatsClock clock;
        SiteStats stats(clock.fn());
        stats.open(path);
        ASSERT_TRUE(stats.save());
        const std::string first = stats_slurp(path);
        stats.take_notices();
        stats.count_online();
        FsyncSpy::calls = 0;
        FsyncSpy::fail = false;
        FsyncSpy::watch = path + ".tmp";
        FsyncSpy::beside = path;
        FsyncSpy::armed = true;
        const bool saved = stats.save();
        FsyncSpy::armed = false;
        ASSERT_TRUE(saved);
        ASSERT_EQ(FsyncSpy::calls, 1);
        const std::string second = stats_slurp(path);
        ASSERT_TRUE(!second.empty() && second != first);
        ASSERT_TRUE(FsyncSpy::watched_text == second);                                          // when it was synced the temporary file held the whole new text (written and flushed first) ...
        ASSERT_TRUE(FsyncSpy::beside_text == first);                                            // ... and the file still had its old text: the rename came after the sync
        ASSERT_TRUE(!fs::exists(path + ".tmp") && !stats.dirty());
        // a sync that fails: nothing is renamed, the temporary file is removed, the old file is whole, the counters stay unsaved, the log says so
        stats.count_online();
        FsyncSpy::calls = 0;
        FsyncSpy::fail = true;
        FsyncSpy::armed = true;
        const bool failed = stats.save();
        FsyncSpy::armed = false;
        ASSERT_FALSE(failed);
        ASSERT_EQ(FsyncSpy::calls, 1);
        ASSERT_TRUE(stats_slurp(path) == second && !fs::exists(path + ".tmp") && stats.dirty());
        const std::vector<std::string> said = stats.take_notices();
        ASSERT_TRUE(notice_has(said, "could not be saved") && notice_has(said, "cannot write") && notice_has(said, ".tmp"));
        FsyncSpy::fail = false;
        ASSERT_TRUE(stats.save() && !stats.dirty() && stats_slurp(path) != second && !fs::exists(path + ".tmp"));
        ASSERT_TRUE(notice_has(stats.take_notices(), "saved again"));
        // a sync that a signal interrupts is not a failure: it is asked again
        stats.count_online();
        FsyncSpy::calls = 0;
        FsyncSpy::interrupt_first = true;
        FsyncSpy::armed = true;
        const bool interrupted = stats.save();
        FsyncSpy::armed = false;
        FsyncSpy::interrupt_first = false;
        ASSERT_TRUE(interrupted && FsyncSpy::calls == 2 && !stats.dirty() && !fs::exists(path + ".tmp"));
        ASSERT_TRUE(stats.take_notices().empty());
    } TEST_END();
#endif

#ifndef _WIN32
    TEST_CASE("S3.151 A write that fails (a full disk: here the process may write no file) removes the temporary file, leaves the old file whole and is said (where); a temporary file that a crash left is written over; a folder in its place is not ours to remove; the file is made with the umask's mode") {
        const fs::path dir = temp_dir_for("stats-full");
        const std::string path = (dir / SiteStats::kFileName).string();
        StatsClock clock;
        SiteStats stats(clock.fn());
        stats.open(path);
        ASSERT_TRUE(stats.save());
        const std::string first = stats_slurp(path);
        stats.take_notices();
        stats.count_online();
        struct rlimit before_limit;
        ASSERT_EQ(::getrlimit(RLIMIT_FSIZE, &before_limit), 0);
        struct sigaction ignore;
        std::memset(&ignore, 0, sizeof(ignore));
        ignore.sa_handler = SIG_IGN;
        struct sigaction before_action;
        ASSERT_EQ(::sigaction(SIGXFSZ, &ignore, &before_action), 0);                              // (a write past the limit is a signal, which ends a process that does not ignore it)
        struct rlimit none = before_limit;
        none.rlim_cur = 0;
        ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &none), 0);
        const bool saved = stats.save();
        ::setrlimit(RLIMIT_FSIZE, &before_limit);
        ::sigaction(SIGXFSZ, &before_action, nullptr);
        ASSERT_FALSE(saved);
        ASSERT_TRUE(!fs::exists(path + ".tmp") && stats_slurp(path) == first && stats.dirty());   // nothing half written is left, under any name
        const std::vector<std::string> notices = stats.take_notices();
        ASSERT_TRUE(notice_has(notices, "could not be saved") && notice_has(notices, "cannot write") && notice_has(notices, SiteStats::kFileName) && notice_has(notices, ".tmp"));   // the log says where
        stats_write(path + ".tmp", std::string(500, 'x'));                                        // (a temporary file that a crash left behind: written over, never added to)
        ASSERT_TRUE(stats.save() && stats_slurp(path) != first && !fs::exists(path + ".tmp"));    // the disk is well again
        ASSERT_TRUE(stats_slurp(path).rfind("{\"format\":1,", 0) == 0 && stats_slurp(path).find("xxxx") == std::string::npos);
        // a folder where the temporary file goes cannot be opened: it is not removed (it is not what this program made)
        fs::create_directories(path + ".tmp");
        stats.count_online();
        ASSERT_FALSE(stats.save());
        ASSERT_TRUE(fs::is_directory(path + ".tmp"));
        fs::remove_all(path + ".tmp");
        // the mode is that of any new file: 0666 less the umask
        const mode_t old_mask = ::umask(022);
        fs::remove(path);
        ASSERT_TRUE(stats.save());
        struct stat st;
        ASSERT_EQ(::stat(path.c_str(), &st), 0);
        const mode_t mode_022 = st.st_mode & 0777;
        ::umask(077);
        fs::remove(path);
        stats.count_online();
        ASSERT_TRUE(stats.save());
        ASSERT_EQ(::stat(path.c_str(), &st), 0);
        const mode_t mode_077 = st.st_mode & 0777;
        ::umask(old_mask);
        ASSERT_TRUE(mode_022 == 0644 && mode_077 == 0600);
    } TEST_END();
#endif

    TEST_CASE("S3.148 A real room manager: a public room that played 30 s and ended counts once (also when its code is asked for again before anybody looked), a match that was quit sooner, a room closed during the dialog and one closed while it waited count nothing, a match that played 30 s and was closed by the owner counts, and nothing counts twice") {
        ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.demo_wait_ms = 3000;                                                              // a public room that nobody completes fails after 3 s
        World w(limits);
        StatsClock clock;
        SiteStats stats(clock.fn());
        std::string seen;                                                                        // the codes that were told ended, for the message of a failure
        const auto look = [&]() {
            size_t ended = 0;
            for (const RoomStatus& s : w.mgr.take_ended(w.now)) {
                stats.count_ended(s);
                seen += s.code + "(" + std::to_string(s.ticks) + ") ";
                ++ended;
            }
            return ended;
        };
        const auto ticking = [&](const std::string& code) { return w.status(code).state == RoomState::Running && w.status(code).ticks > 20; };
        const auto played = [&](const std::string& code) { return w.status(code).state == RoomState::Running && w.status(code).ticks >= SiteStats::kMinTicks; };   // 30 s of play
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        const auto play_until = [&](const std::function<bool()>& done, uint32_t max_ms) {
            for (uint32_t t = 0; t < max_ms && !done(); t += 250) w.run(250);
            return done();
        };
        // a public room that never fills: it fails, with no tick
        w.connect_creating("Lone", "lone0001", block_of());
        w.run(4500);
        ASSERT_TRUE(w.status("lone0001").state == RoomState::Failed && w.status("lone0001").ticks == 0);
        ASSERT_EQ(look(), size_t{1});
        ASSERT_TRUE(stats.online().total == 0);
        // two players, and one quits after a few seconds of play: the match ends and is told, and it was too short to count
        Client& sue = w.connect_creating("Sue", "short001", block_of());
        w.connect("Tom", "short001");
        ASSERT_TRUE(play_until([&]() { return ticking("short001"); }, 30000));
        quit.issuer = sue.lobby->my_seat();
        ASSERT_TRUE(sue.session->submit(quit));
        ASSERT_TRUE(play_until([&]() { return w.status("short001").state == RoomState::Finished; }, 20000));
        ASSERT_TRUE(w.status("short001").ticks > 20 && w.status("short001").ticks < SiteStats::kMinTicks);
        ASSERT_EQ(look(), size_t{1});
        ASSERT_TRUE(stats.online().total == 0 && stats.online().day == 0);
        // two players: the match runs 30 s; one quits and the match ends; counted when the end is looked at, once
        Client& ann = w.connect_creating("Ann", "ran00001", block_of());
        w.connect("Bob", "ran00001");
        ASSERT_TRUE(play_until([&]() { return ticking("ran00001"); }, 30000));
        ASSERT_EQ(look(), size_t{0});                                                            // (it runs: it has not ended)
        ASSERT_TRUE(play_until([&]() { return played("ran00001"); }, 60000));
        ASSERT_EQ(look(), size_t{0});
        ASSERT_TRUE(stats.online().total == 0);
        quit.issuer = ann.lobby->my_seat();
        ASSERT_TRUE(ann.session->submit(quit));
        ASSERT_TRUE(play_until([&]() { return w.status("ran00001").state == RoomState::Finished; }, 20000));
        ASSERT_TRUE(w.status("ran00001").ticks >= SiteStats::kMinTicks);
        ASSERT_EQ(look(), size_t{1});
        ASSERT_TRUE(stats.online().total == 1 && stats.online().day == 1);
        ASSERT_EQ(look(), size_t{0});                                                            // (told once: nothing more to count)
        w.run(40000);                                                                            // forgotten after its keep time: still one
        ASSERT_EQ(look(), size_t{0});
        ASSERT_EQ(stats.online().total, uint64_t{1});
        // a match that ended and whose code is asked for before anybody looked: the manager forgets the room at once and still tells its end, once
        w.connect_creating("Cat", "twice001", block_of());
        Client& dan = w.connect("Dan", "twice001");
        ASSERT_TRUE(play_until([&]() { return played("twice001"); }, 60000));
        quit.issuer = dan.lobby->my_seat();
        ASSERT_TRUE(dan.session->submit(quit));
        ASSERT_TRUE(play_until([&]() { return w.status("twice001").state == RoomState::Finished; }, 20000));
        w.connect_creating("Eve", "twice001", block_of());                                       // a late friend with the same link (it carries the block): a new room for the code
        w.run(500);
        ASSERT_TRUE(w.status("twice001").state == RoomState::Waiting);
        ASSERT_EQ(look(), size_t{1});                                                            // the end of the old one (and only that)
        ASSERT_TRUE(stats.online().total == 2);
        ASSERT_EQ(look(), size_t{0});
        // a room that the control interface made, closed while it waits: nothing. Closed during the dialog before the first tick: nothing. Closed after a few seconds of play: nothing. Closed after 30 s: counted.
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-wait", 2), w.now).ok);
        ASSERT_TRUE(w.mgr.close_room("CTL-wait", w.now));
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-dialog", 2), w.now).ok);
        w.connect("Fay", "CTL-dialog");
        w.connect("Gus", "CTL-dialog");
        ASSERT_TRUE(play_until([&]() { return w.status("CTL-dialog").state == RoomState::Running; }, 20000));
        ASSERT_EQ(w.status("CTL-dialog").ticks, 0u);                                             // the match began, the 5 s of the dialog are not over: no tick yet
        ASSERT_TRUE(w.mgr.close_room("CTL-dialog", w.now));
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-play", 2), w.now).ok);
        w.connect("Hal", "CTL-play");
        w.connect("Ida", "CTL-play");
        ASSERT_TRUE(play_until([&]() { return ticking("CTL-play"); }, 30000));
        ASSERT_TRUE(w.mgr.close_room("CTL-play", w.now));                                        // closed after a few seconds of play: nothing
        ASSERT_TRUE(w.status("CTL-play").state == RoomState::Failed && w.status("CTL-play").ticks > 20 && w.status("CTL-play").ticks < SiteStats::kMinTicks);
        ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-long", 2), w.now).ok);                       // and one that is closed after 30 s of play: counted
        w.connect("Jan", "CTL-long");
        w.connect("Kim", "CTL-long");
        ASSERT_TRUE(play_until([&]() { return played("CTL-long"); }, 60000));
        ASSERT_TRUE(w.mgr.close_room("CTL-long", w.now));
        ASSERT_TRUE(w.status("CTL-long").state == RoomState::Failed && w.status("CTL-long").ticks >= SiteStats::kMinTicks);
        ASSERT_MSG(look() == 5, seen);                                                           // five rooms ended (the new twice001, which nobody joined, failed meanwhile); only the one that played 30 s is a game
        ASSERT_TRUE(stats.online().total == 3 && stats.online().day == 3);
        ASSERT_EQ(look(), size_t{0});
        ASSERT_TRUE(stats.local().total == 0);
    } TEST_END();

#ifndef _WIN32
    TEST_CASE("S3.149 The statistics through a real listener (the wiring of the server's main): GET /stats answers the numbers and the busy counts, POST /stats/local is counted and answered 204, the 121st report of a minute gets the same 204 as the first (the cap cannot be seen), a body or a query is refused and counted for nothing, and nothing reaches the game") {
        StatsClock clock;
        SiteStats stats(clock.fn());
        BusyCounts busy;
        busy.matches = 1;
        busy.players = 3;
        auto listener = net::WsListener::listen(0, true);
        ASSERT_TRUE(listener != nullptr);
        listener->set_status("/busy", [&busy]() { return "{\"matches\":" + std::to_string(busy.matches) + ",\"players\":" + std::to_string(busy.players) + "}"; });
        listener->add_status("/stats", [&]() { return stats.json(busy); });
        listener->set_post("/stats/local", [&]() { stats.count_local(); });
        const std::string get = "GET /stats HTTP/1.1\r\nHost: play.example.org\r\n\r\n";
        const std::string post = "POST /stats/local HTTP/1.1\r\nHost: play.example.org\r\nContent-Length: 0\r\n\r\n";
        const auto body_of = [](const std::string& answer) {
            const size_t split = answer.find("\r\n\r\n");
            return split == std::string::npos ? std::string() : answer.substr(split + 4);
        };
        std::string answer = http_exchange(*listener, get);
        ASSERT_TRUE(answer.find("HTTP/1.1 200 OK\r\n") == 0 && answer.find("Cache-Control: no-store\r\n") != std::string::npos && answer.find("Content-Type: application/json\r\n") != std::string::npos);
        ASSERT_EQ(body_of(answer), std::string("{\"now\":{\"matches\":1,\"players\":3},\"online\":{\"day\":0,\"total\":0},\"local\":{\"day\":0,\"total\":0},\"since\":\"2026-10-04\"}"));
        stats.count_online();
        answer = http_exchange(*listener, post);                                                 // the first report: counted
        const std::string counted_answer = answer;
        ASSERT_TRUE(answer.find("HTTP/1.1 204 No Content\r\n") == 0 && body_of(answer).empty() && answer.find("Content-Length") == std::string::npos);
        ASSERT_TRUE(stats.local().total == 1);
        ASSERT_EQ(body_of(http_exchange(*listener, get)), std::string("{\"now\":{\"matches\":1,\"players\":3},\"online\":{\"day\":1,\"total\":1},\"local\":{\"day\":1,\"total\":1},\"since\":\"2026-10-04\"}"));
        for (int i = 1; i < 120; ++i) ASSERT_EQ(http_exchange(*listener, post), counted_answer);
        ASSERT_EQ(stats.local().total, uint64_t{120});
        const std::string capped_answer = http_exchange(*listener, post);                         // the 121st of the minute: the same answer, not counted
        ASSERT_EQ(capped_answer, counted_answer);
        ASSERT_EQ(http_exchange(*listener, post), counted_answer);
        ASSERT_EQ(stats.local().total, uint64_t{120});
        ASSERT_TRUE(body_of(http_exchange(*listener, get)).find("\"local\":{\"day\":120,\"total\":120}") != std::string::npos);
        clock.now += 60;                                                                         // a minute later the next one counts
        ASSERT_EQ(http_exchange(*listener, post), counted_answer);
        ASSERT_EQ(stats.local().total, uint64_t{121});
        // what is refused counts for nothing
        ASSERT_TRUE(http_exchange(*listener, "POST /stats/local HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\nabcd").find("HTTP/1.1 400 ") == 0);
        ASSERT_TRUE(http_exchange(*listener, "POST /stats/local?a=1 HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n").find("HTTP/1.1 405 ") == 0);
        ASSERT_TRUE(http_exchange(*listener, "POST /stats HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n").find("HTTP/1.1 405 ") == 0);
        ASSERT_TRUE(http_exchange(*listener, "GET /stats/local HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 405 ") == 0);
        ASSERT_TRUE(http_exchange(*listener, "GET /stats?x=1 HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 426 ") == 0);
        ASSERT_EQ(stats.local().total, uint64_t{121});
        ASSERT_TRUE(http_exchange(*listener, "GET /busy HTTP/1.1\r\nHost: x\r\n\r\n").find("{\"matches\":1,\"players\":3}") != std::string::npos);      // (the busy answer is as it was)
        ASSERT_TRUE(listener->accept() == nullptr);                                              // nothing reached the game
        ASSERT_EQ(listener->pending(), size_t{0});
    } TEST_END();
#endif
}
