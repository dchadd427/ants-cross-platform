#pragma once

// The phase of the server's seals (docs/P2P_HINTS.md, "The tick that a hint names").
//
// The server seals one turn every 50 ms and sends it to every player. Turn k reaches THIS machine at about  A(k) = phase + 50 * k  (the arrival times of the last turns, each less 50 ms times
// its number, have one value, the median of them is the phase). An order that this player gives at time t reaches the server a way later and is sealed in the first turn that the server seals after
// it came; that turn is the first one that arrives here after  t + delay,  where the delay is the whole round trip (the way up, the way back; the wait for the seal is the grid itself):
//
//     turn of the order  =  the first k with  A(k) >= t + delay        =  ceil((t + delay - phase) / 50)
//
// The delay is learned from the orders themselves: an order that was given at t and came back in turn n says  50 * (n - 1) < t + delay - phase <= 50 * n,  an interval for the delay; the
// delay that fits the most of the last orders (the middle of the widest run of the best-fitting values; the newer an order the more it counts) is the one used, when it fits the half of them. With
// no orders yet, too few of them or arrivals that do not keep a rhythm, there is no answer, and the owner names the tick that its own screen applies the order at, as it did before.
//
// Nothing here touches the simulation, the turns or the network: it reads two clocks' worth of numbers that the owner hands it and answers one question.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace ants::net {

class SealPhase {
public:
    struct Config {
        size_t arrival_samples{24};        // the arrivals that the phase is the median of (a second and a bit of turns)
        size_t min_arrivals{12};           // fewer than this and the phase is not known
        int64_t max_spread_ms{60};         // the middle half of the arrivals must lie within this many ms (a link that scatters the turns more has no phase that is worth a tick)
        size_t order_samples{16};          // the orders that the delay is learned from
        size_t min_orders{3};              // fewer than this and the delay is not known
        uint32_t order_fresh_ms{60000};    // an order older than this (than the newest one) is not used
        int64_t max_delay_ms{900};         // the delays that are tried (a round trip beyond this is not helped by guessing)
    };

    SealPhase() = default;
    explicit SealPhase(Config config) : cfg_(config) {}

    /// Turn `turn` has reached this machine at `now_ms` (the clock that every call here uses, in ms)
    void note_arrival(uint64_t turn, uint32_t now_ms);
    /// An order that this player gave at `given_ms` was sealed by the server in turn `sealed_turn`
    void note_order(uint32_t given_ms, uint64_t sealed_turn);
        /// Forget everything (the link or the host is not the one that it was)
    void clear();

    /// The turn in which an order given at `given_ms` is sealed, when the phase and the delay are known
    std::optional<uint64_t> sealed_turn_for(uint32_t given_ms) const;

    // ---- what it knows (the tests and the diagnostics) ---------------------------------------------------------------------------------------------
    bool phase_known() const noexcept { return phase_known_; }
    /// The phase in ms (only when phase_known())
    int64_t phase_ms() const noexcept { return phase_; }
    /// The delay in ms, when the orders have taught it
    std::optional<int64_t> delay_ms() const;
    size_t orders() const noexcept { return orders_.size(); }

private:
    struct Order {
        int64_t given{0};
        uint64_t sealed{0};
    };
    void update_phase();

    Config cfg_;
    std::deque<int64_t> arrivals_;     // arrival time less 50 ms times the turn's number, for the last turns
    bool phase_known_{false};
    int64_t phase_{0};
    std::deque<Order> orders_;
};

}  // namespace ants::net
