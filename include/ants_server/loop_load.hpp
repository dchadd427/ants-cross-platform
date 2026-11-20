#pragma once

// How busy the server's one loop thread is (docs/SERVER_REPLAYS.md, "The limits"): the main loop adds the wall time of each pass (everything but its sleep), and share() says what part of the last ten seconds that was.
// The matches of computer players ask it before a start (RoomManager::start_bot_match): a start waits while the share is over kBotBusyGate, so a slow host, a heavy map and the matches of people all slow the queue by
// themselves and never the other way round. A clock in milliseconds that the caller gives (the loop's steady clock; the tests give their own), no thread, no lock: it is used from the loop only.

#include <array>
#include <cstdint>

namespace ants::server {

class LoopLoad {
public:
    static constexpr uint32_t kSeconds = 10;      // the window: ten one-second slots

    /// A pass of the loop took `busy_us` microseconds of wall time and ended at `now_ms`
    void add(uint32_t now_ms, uint64_t busy_us) {
        advance(now_ms);
        slots_[head_] += busy_us;
    }

    /// The part of the last ten seconds that the loop was busy: 0 to 1 (slots that are not filled yet count as idle time, so a server that has just started reads low, never high)
    double share(uint32_t now_ms) {
        advance(now_ms);
        uint64_t sum = 0;
        for (const uint64_t s : slots_) sum += s;
        const double v = static_cast<double>(sum) / (static_cast<double>(kSeconds) * 1e6);
        return v > 1.0 ? 1.0 : v;
    }

private:
    void advance(uint32_t now_ms) {
        const uint32_t second = now_ms / 1000u;
        if (!started_) {
            started_ = true;
            second_ = second;
            return;
        }
        uint32_t steps = second - second_;
        if (steps == 0 || steps > 0x80000000u) return;         // (the same second, or a clock that went back: nothing)
        if (steps > kSeconds) steps = kSeconds;
        for (uint32_t i = 0; i < steps; ++i) {
            head_ = (head_ + 1) % kSeconds;
            slots_[head_] = 0;
        }
        second_ = second;
    }

    std::array<uint64_t, kSeconds> slots_{};
    uint32_t head_{0};
    uint32_t second_{0};
    bool started_{false};
};

}  // namespace ants::server
