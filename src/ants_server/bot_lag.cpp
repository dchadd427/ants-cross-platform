#include "ants_server/bot_lag.hpp"

#include <algorithm>
#include <utility>

namespace ants::server {

uint32_t bot_lag_ticks_for(uint32_t ms) noexcept {
    if (ms >= kBotLagMaxTicks * kBotLagTickMs) return kBotLagMaxTicks;       // (also keeps the sum below from wrapping)
    return std::min<uint32_t>(kBotLagMaxTicks, (ms + kBotLagTickMs / 2) / kBotLagTickMs);
}

uint32_t bot_lag_median(const uint32_t* ms, size_t n) noexcept {
    if (n == 0 || ms == nullptr) return 0;
    uint32_t sorted[16];                                            // (a room has four seats: more is not asked)
    n = std::min<size_t>(n, 16);
    std::copy(ms, ms + n, sorted);
    std::sort(sorted, sorted + n);
    if (n % 2 == 1) return sorted[n / 2];
    return static_cast<uint32_t>((static_cast<uint64_t>(sorted[n / 2 - 1]) + sorted[n / 2] + 1) / 2);
}

void BotLag::update(uint64_t tick, size_t humans, const uint32_t* trips_ms, size_t n) noexcept {
    if (humans == 0) {                                              // nobody to be level with: a bot-only match, a room whose people all left
        hold_ = 0;
        changed_ = false;
        return;
    }
    if (n == 0) return;                                             // people are here and none can be read now: what was known stays
    const uint32_t ms = std::min<uint32_t>(bot_lag_median(trips_ms, n), 2 * kBotLagMaxTicks * kBotLagTickMs);       // (what is past the cap makes no difference, and the sums below cannot wrap)
    const uint32_t half = kBotLagTickMs / 2;
    const uint32_t up = std::min<uint32_t>(kBotLagMaxTicks, (ms + half - kBotLagBandMs) / kBotLagTickMs);        // the most the reading holds up to, with the band against the boundary
    const uint32_t down = std::min<uint32_t>(kBotLagMaxTicks, (ms + half + kBotLagBandMs) / kBotLagTickMs);      // and the least
    uint32_t next = hold_;
    if (up > hold_) next = up;
    else if (down < hold_) next = down;
    if (next == hold_ || (changed_ && tick - changed_at_ < kBotLagDwellTicks)) return;
    hold_ = next;
    changed_ = true;
    changed_at_ = tick;
}

sim::CommandResult LaggedSink::submit(const sim::Command& command) {
    sim::CommandResult result;
    const uint64_t sealed = line_.turns_sealed();
    const uint64_t due = std::max<uint64_t>(sealed + line_.hold_ticks(), last_due_);
    if (queue_.empty() && due <= sealed) {                          // nothing to wait for and nothing in front of it: as the sink did before there was a hold
        if (line_.deliver(command)) result.status = sim::CommandResult::Status::Applied;
        return result;
    }
    if (!line_.accepts()) return result;                            // the session refuses it now, as it would have at once
    queue_.push_back(Held{due, sealed, command});
    last_due_ = due;
    ++held_total_;
    result.status = sim::CommandResult::Status::Applied;
    return result;
}

uint64_t LaggedSink::applied_at(uint64_t) const {
    return std::max<uint64_t>(line_.turns_sealed() + line_.hold_ticks(), last_due_);
}

void LaggedSink::release() {
    const uint64_t sealed = line_.turns_sealed();
    while (!queue_.empty() && queue_.front().due <= sealed) {
        if (!line_.accepts()) return;
        line_.deliver(queue_.front().command);
        longest_wait_ = std::max<uint32_t>(longest_wait_, static_cast<uint32_t>(sealed - queue_.front().at));
        queue_.pop_front();
    }
}

}  // namespace ants::server
