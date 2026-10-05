#pragma once

// The numbers of the front page (docs/NETWORK_PORT.md "Site statistics"): online games (a match that ran at least 30 seconds and ended) and single-player games that browsers report, each for the last 24
// hours (24 buckets of an hour on the server's clock) and for all time, kept in one small file. Numbers only, never a code, name, address or key. Single threaded like the server's loop.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ants::server {

struct BusyCounts;
struct RoomStatus;

/// One counter: the games of the last 24 hours and of all time
struct SiteCount {
    uint64_t day{0};
    uint64_t total{0};
};

class SiteStats {
public:
    static constexpr size_t kHours = 24;                 // the window: the hour that runs and the 23 before it
    static constexpr uint32_t kMinTicks = 600;           // an online game is a match that ran at least this long: 30 s at 20 ticks a second (a start-and-quit loop cannot pad the number)
    static constexpr size_t kLocalPerMinute = 120;       // the most reports of single-player games that count in any kLocalWindowS seconds
    static constexpr int64_t kLocalWindowS = 60;
    static constexpr int64_t kSaveEveryS = 10;           // the file is written at most this often while something changed
    static constexpr const char* kFileName = "site-stats.json";

    /// Seconds since 1970-01-01 00:00:00 UTC (empty: the system clock); the tests give their own
    using Clock = std::function<int64_t()>;
    explicit SiteStats(Clock clock = Clock());

    /// A room that ended (RoomManager::take_ended tells each once): an online game when its match ran at least kMinTicks (RoomStatus::ticks), nothing otherwise
    void count_ended(const RoomStatus& ended);
    /// One online game
    void count_online();
    /// A browser reported a single-player game. False: it was not counted (kLocalPerMinute were counted in the last minute already); a clock that goes back never blocks the count
    bool count_local();

    SiteCount online() const;
    SiteCount local() const;
    /// The UTC date (YYYY-MM-DD) from which the counters count: the day they were made, or what the file says
    const std::string& since() const noexcept { return since_; }
    /// The answer of GET /stats: {"now":{"matches":N,"players":M},"online":{"day":D,"total":T},"local":{"day":d,"total":t},"since":"YYYY-MM-DD"}; `now` is what GET /busy says
    std::string json(const BusyCounts& now) const;

    /// Keeps the counters in the file `path` from now on: reads it when it exists (a file that is not a usable one is put aside as <path>.broken-<seconds> and the counters start at
    /// 0), else the counters are new and the file is written at the first save. Call it once, before anything is counted. Without it the counters live in memory only.
    void open(const std::string& path);
    /// Writes the file when something changed and kSaveEveryS seconds have passed since the last try (a failed try counts too: the disk is not asked every pass). True when it wrote.
    bool save_if_due();
    /// Writes the file now (the server stops). True when nothing is left unsaved: it wrote, or there is no file or nothing changed.
    bool save();
    /// Something counted has not reached the file yet
    bool dirty() const noexcept { return dirty_; }
    /// Lines for the server's log, once each: what open() found, a save that failed (the first of a run of failures), a save that works again
    std::vector<std::string> take_notices();

private:
    struct Series {
        uint64_t total{0};
        std::array<int64_t, kHours> hour{};              // the hour number of each slot (hour mod 24), kNoHour when empty
        std::array<uint64_t, kHours> count{};
        Series();
    };
    static int64_t hour_of(int64_t seconds) noexcept;
    static uint64_t window(const Series& series, int64_t now_hour) noexcept;
    static void add(Series& series, int64_t now_hour) noexcept;
    bool write_file(std::string& why);
    bool read_file(std::string& why);

    Clock clock_;
    std::string since_;
    Series online_;
    Series local_;
    std::array<int64_t, kLocalPerMinute> local_times_{};  // when the last kLocalPerMinute reports were counted (a ring: local_next_ is the oldest)
    size_t local_next_{0};
    size_t local_filled_{0};
    std::string path_;
    bool dirty_{false};
    bool have_tried_{false};
    int64_t tried_at_{0};
    bool failing_{false};
    std::vector<std::string> notices_;
};

}  // namespace ants::server
