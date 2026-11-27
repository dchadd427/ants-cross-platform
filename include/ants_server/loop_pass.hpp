#pragma once

// One pass of the server's loop, timed by part (docs/SERVER.md "The slow-pass log"). The server runs every room on one thread, so a pass that takes longer than a turn (50 ms) holds back every match that
// runs: LoopPass says where a pass spent its time, and SlowPassLog writes one line for a pass that took too long, so that a freeze can be traced to its cause (a look at the folder of the replays, the start of
// a match, the flush of a restart record, a door). The loop calls begin() first and lap() after each of its jobs; the room manager and the rooms add their own laps inside rooms.update(). No thread, no lock: it is
// used from the loop only, and costs a few readings of the clock for each pass (the rooms hold none: they hand their times to the manager, Room::take_pass_work()).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace ants::server {

/// What a pass spent its time on
enum class PassPart : uint8_t {
    Rooms,        // the rooms' own work: the referee's turns, the bots' ticks, the sessions
    Starts,       // a match that begins (its engine, its record and recording, the bots' first look at the map)
    Bots,         // the schedules of the matches of computer players: a due start (BotMatches::update)
    Store,        // the replay store: the look at its folder, the hourly purge
    History,      // the match history's feeder: one step of counting a recording
    Flush,        // the restart records made durable (the fdatasync of a room that holds seats)
    Doors,        // the HTTP doors: control, submissions, counts, test maps, pictures, replays
    Other,        // the rest: the connections that arrive, the Hellos, the restore of records, the end of a match, the statistics
    Count
};

class LoopPass {
public:
    static constexpr size_t kParts = static_cast<size_t>(PassPart::Count);
    /// Microseconds on a steady clock (empty: std::chrono::steady_clock); the tests give their own
    using Clock = std::function<int64_t()>;

    explicit LoopPass(Clock clock = Clock()) : clock_(std::move(clock)) {}

    /// A pass starts: every part is zero
    void begin() {
        parts_.fill(0);
        last_ = began_ = now_us();
    }
    /// The time since the last lap (or begin()) is `part`'s
    void lap(PassPart part) {
        const int64_t now = now_us();
        parts_[index(part)] += now > last_ ? now - last_ : 0;
        last_ = now;
    }
    /// `us` of what `from` was given belonged to `to` (a room that began a match or flushed its record inside its own update)
    void move(PassPart from, PassPart to, int64_t us) {
        int64_t& source = parts_[index(from)];
        const int64_t moved = us < 0 ? 0 : us > source ? source : us;
        source -= moved;
        parts_[index(to)] += moved;
    }
    /// The time since begin(): what the pass has spent so far, laps or not
    int64_t elapsed_us() const {
        const int64_t now = now_us();
        return now > began_ ? now - began_ : 0;
    }
    /// The parts together: the time up to the last lap
    int64_t total_us() const {
        int64_t sum = 0;
        for (const int64_t p : parts_) sum += p;
        return sum;
    }
    int64_t us(PassPart part) const { return parts_[index(part)]; }
    const std::array<int64_t, kParts>& parts() const noexcept { return parts_; }
    static const char* name(PassPart part);

private:
    static size_t index(PassPart part) { return static_cast<size_t>(part); }
    int64_t now_us() const;

    Clock clock_;
    std::array<int64_t, kParts> parts_{};
    int64_t began_{0};
    int64_t last_{0};
};

/// The slow-pass log: one line when a pass took more than the threshold, with the parts by name, and at most one line in `every_ms` (the passes that were slow meanwhile are counted, and told with the longest of
/// them when the time is over). Nothing but one comparison for a pass that is not slow. The line has no name, code or address.
class SlowPassLog {
public:
    static constexpr int64_t kThresholdUs = 25000;       // a quarter of a pass that a turn (50 ms) can spare: over it, a match would feel it
    static constexpr uint32_t kEveryMs = 10000;

    explicit SlowPassLog(int64_t threshold_us = kThresholdUs, uint32_t every_ms = kEveryMs) : threshold_us_(threshold_us), every_ms_(every_ms) {}

    /// The pass that `pass` timed ended at `now_ms` (the loop's steady clock, milliseconds). The line to write, or "" when there is none.
    std::string end(uint32_t now_ms, const LoopPass& pass);

private:
    void hide(const LoopPass& pass);

    int64_t threshold_us_;
    uint32_t every_ms_;
    bool armed_{false};                                  // a line was written, so the next one waits until next_ms_
    uint32_t next_ms_{0};
    uint32_t hidden_{0};                                 // slow passes since the last line, not told yet
    int64_t worst_us_{0};
    std::array<int64_t, LoopPass::kParts> worst_parts_{};
};

}  // namespace ants::server
