#include "ants_net/seal_phase.hpp"

#include <algorithm>

#include "ants_net/protocol.hpp"

namespace ants::net {

namespace {

constexpr int64_t kTurn = static_cast<int64_t>(kTurnMs);

int64_t ceil_div(int64_t a, int64_t b) noexcept { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }

}  // namespace

void SealPhase::clear() {
    arrivals_.clear();
    phase_known_ = false;
    phase_ = 0;
    orders_.clear();
}

void SealPhase::note_arrival(uint64_t turn, uint32_t now_ms) {
    arrivals_.push_back(static_cast<int64_t>(now_ms) - kTurn * static_cast<int64_t>(turn));
    while (arrivals_.size() > cfg_.arrival_samples) arrivals_.pop_front();
    update_phase();
}

void SealPhase::update_phase() {
    phase_known_ = false;
    if (arrivals_.size() < cfg_.min_arrivals) return;
    std::vector<int64_t> v(arrivals_.begin(), arrivals_.end());
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    phase_ = n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
    phase_known_ = v[n - 1 - n / 4] - v[n / 4] <= cfg_.max_spread_ms;
}

void SealPhase::note_order(uint32_t given_ms, uint64_t sealed_turn) {
    orders_.push_back(Order{static_cast<int64_t>(given_ms), sealed_turn});
    while (orders_.size() > cfg_.order_samples) orders_.pop_front();
    while (orders_.size() > 1 && orders_.front().given + static_cast<int64_t>(cfg_.order_fresh_ms) < orders_.back().given) orders_.pop_front();
}

// The delay that fits the most orders. Order i, given at t_i and sealed in turn n_i, allows the delays d with  50 (n_i - 1) < t_i + d - phase <= 50 n_i : an interval of 50 ms. Each order counts for
// 9/10 of the one after it (a link changes: the orders of the new delay outweigh the old ones within a few of them). The weight of the intervals that hold a delay is swept over all delays; the widest
// run of the delays with the highest weight is used, and its middle is the answer. When no delay holds the half of the weight
// there is none: the orders do not agree on any.
std::optional<int64_t> SealPhase::delay_ms() const {
    if (!phase_known_ || orders_.size() < cfg_.min_orders) return std::nullopt;
    struct Event {
        int64_t at;
        int64_t delta;
    };
    std::vector<Event> events;
    int64_t total = 0;
    int64_t weight = 1000;
    for (size_t i = orders_.size(); i-- > 0; weight = weight * 9 / 10) {                      // (the newest first)
        const Order& o = orders_[i];
        total += weight;
        const int64_t lo = std::max<int64_t>(0, kTurn * (static_cast<int64_t>(o.sealed) - 1) + phase_ - o.given + 1);
        const int64_t hi = std::min<int64_t>(cfg_.max_delay_ms, kTurn * static_cast<int64_t>(o.sealed) + phase_ - o.given);
        if (lo > hi) continue;
        events.push_back(Event{lo, weight});
        events.push_back(Event{hi + 1, -weight});
    }
    if (events.empty()) return std::nullopt;
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    struct Run {
        int64_t from;
        int64_t to;
        int64_t weight;
    };
    std::vector<Run> runs;
    int64_t held = 0;
    int64_t best = 0;
    for (size_t i = 0; i < events.size();) {
        const int64_t at = events[i].at;
        while (i < events.size() && events[i].at == at) held += events[i++].delta;
        if (held <= 0 || i >= events.size()) continue;
        if (!runs.empty() && runs.back().weight == held && runs.back().to + 1 == at) {
            runs.back().to = events[i].at - 1;
        } else {
            runs.push_back(Run{at, events[i].at - 1, held});
        }
        best = std::max(best, held);
    }
    if (best * 2 < total) return std::nullopt;
    const Run* chosen = nullptr;
    for (const Run& r : runs) {
        if (r.weight == best && (chosen == nullptr || r.to - r.from > chosen->to - chosen->from)) chosen = &r;
    }
    return (chosen->from + chosen->to) / 2;
}

std::optional<uint64_t> SealPhase::sealed_turn_for(uint32_t given_ms) const {
    const std::optional<int64_t> delay = delay_ms();
    if (!delay) return std::nullopt;
    const int64_t k = ceil_div(static_cast<int64_t>(given_ms) + *delay - phase_, kTurn);
    if (k < 0) return std::nullopt;
    return static_cast<uint64_t>(k);
}

}  // namespace ants::net
