#include "ants_net/jitter.hpp"

#include <algorithm>

namespace ants::net {

JitterBuffer::JitterBuffer(Config config) : cfg_(config), target_(config.min_turns) {
    if (cfg_.turn_ms == 0) cfg_.turn_ms = kTurnMs;
    if (cfg_.max_turns < cfg_.min_turns) cfg_.max_turns = cfg_.min_turns;
    cfg_.percentile = std::clamp<uint32_t>(cfg_.percentile, 1, 100);
    target_ = cfg_.min_turns;
}

uint32_t JitterBuffer::turns_for(int64_t lateness_ms) const noexcept {
    const int64_t need = std::max<int64_t>(0, lateness_ms) + static_cast<int64_t>(cfg_.margin_ms);
    const int64_t turns = (need + static_cast<int64_t>(cfg_.turn_ms) - 1) / static_cast<int64_t>(cfg_.turn_ms);
    return static_cast<uint32_t>(std::clamp<int64_t>(turns, cfg_.min_turns, cfg_.max_turns));
}

void JitterBuffer::add_sample(int64_t v) noexcept {
    samples_[next_] = v;
    next_ = (next_ + 1) % kSamples;
    if (filled_ < kSamples) ++filled_;
}

uint32_t JitterBuffer::lateness_ms() const noexcept {
    if (filled_ < kMinSamples) return 0;
    std::array<int64_t, kSamples> sorted = samples_;
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(filled_));
    // the nearest-rank percentile of the turns, measured from the earliest read one
    const size_t rank = std::max<size_t>(1, (filled_ * cfg_.percentile + 99) / 100);
    const int64_t late = sorted[rank - 1] - sorted[0];
    return late <= 0 ? 0u : (late > static_cast<int64_t>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(late));
}

uint32_t JitterBuffer::wanted() const noexcept { return turns_for(static_cast<int64_t>(lateness_ms())); }

void JitterBuffer::on_arrivals(uint32_t first_turn, uint32_t count, uint64_t now_ms, uint32_t frame_ms) {
    if (!seen_) {
        seen_ = true;
        last_change_ms_ = now_ms;                          // (the first shrink is due shrink_after_ms after the first look at the link)
    }
    const bool freeze_bunch = skipping_ && now_ms < skip_until_ms_;            // the turns that a freeze held back (on_stall): not the link's lateness
    if (skipping_ && !freeze_bunch) skipping_ = false;
    if (count > 0 && frame_ms <= cfg_.hitch_ms && !freeze_bunch) {
        // Every turn of the bunch was read now: the turn numbered k has "read time - k * turn_ms" = now - k * turn_ms. Only the last kSamples of a long bunch can count.
        const uint32_t skip = count > kSamples ? count - static_cast<uint32_t>(kSamples) : 0u;
        for (uint32_t i = skip; i < count; ++i) {
            add_sample(static_cast<int64_t>(now_ms) - (static_cast<int64_t>(first_turn) + static_cast<int64_t>(i)) * static_cast<int64_t>(cfg_.turn_ms));
        }
    }
    const uint32_t want = wanted();
    if (want > target_) {
        target_ = want;
        last_change_ms_ = now_ms;
    } else if (filled_ >= kMinSamples && want < target_ && now_ms - last_change_ms_ >= cfg_.shrink_after_ms) {      // (no evidence, no step down: a forgotten link asks for nothing)
        --target_;
        last_change_ms_ = now_ms;
    }
}

void JitterBuffer::on_stall(uint64_t now_ms, uint32_t stall_ms) {
    if (stall_ms >= cfg_.reset_after_ms) forget();         // the link that was measured is gone (a frozen link, a host that paused): measure the one that comes
    if (stall_ms >= cfg_.freeze_ms) {                      // a freeze, not jitter: the bunch that it held back says how long it was, not how rough the link is
        skipping_ = true;
        skip_until_ms_ = now_ms + cfg_.freeze_skip_ms;
    }
    if (stall_ms > cfg_.stall_grow_max_ms) return;         // no buffer of this system bridges it (see the head of this file): the stall is its price
    target_ = std::min(cfg_.max_turns, std::max(target_ + 1, wanted()));
    last_change_ms_ = now_ms;
    seen_ = true;
}

}  // namespace ants::net
