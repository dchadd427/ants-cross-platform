#pragma once

// The rule that sizes the jitter buffer of a lock-step client (docs/NETWORK_PORT.md, "The jitter buffer"). A pure piece of logic: it is told when the host's turns
// were read and when the runner had none to run, and it says how many turns the runner should keep in hand. No clock, no queue, no simulation (the runner in
// lockstep.hpp owns those), so that every property can be tested with made-up arrival times.
//
// What the buffer is for. The host seals a turn every 50 ms; every machine must run it at the same tick, one tick per turn, so a machine that runs a turn the moment
// it has arrived stalls whenever a later turn comes late. Keeping a few turns in hand (running a turn `B` turns after it arrived) lets a late turn still be on time.
// Every turn of delay costs 50 ms between a click and its result, so the buffer is as small as the link allows: ONE turn on a steady link, up to FOUR on a rough one.
//
// How the rule decides. The turn numbered k is sealed at k * 50 ms of the host's clock, so `read_at - k * 50` is the same number for every turn if the link never
// changes (its value is the link's delay and the offset of the two clocks, which are not known and do not matter). Its variation is the lateness of a turn: how much
// later than the earliest of the last 200 turns (10 s of play) it was read. The 95th percentile of the lateness of those turns plus a margin (a frame of a 60 Hz window is
// 17 ms), rounded up to whole turns, is the buffer that keeps nineteen turns in twenty on time. The one in twenty that is later than that (a lost packet that the link
// resends: 300 ms late, a few turns at a time) is not worth a buffer that would cost every command 150 ms: it is a stall (below).
//
//   grow     at once: the lateness of the last turns needs more turns than the buffer has; or the runner had no turn to run and the late turn came within 100 ms (a stall
//            that one more turn would have bridged, or nearly): one more at once, whatever the numbers say (they may have been taken in the shadow of the stall). A turn that
//            is later than that (a lost packet that the link resends, a host that paused) is not bridged by one more turn, and a buffer for it would cost every command
//            50 ms more for as long as it stays: the stall is the price, as with the 5 % of turns that the percentile leaves out. A wait of 1.5 s or more (a frozen link, a
//            host that stopped sealing) starts the measuring afresh: what was seen before it describes a link that is gone, and the turns that come after it show the new one
//   shrink   slowly: a step of one turn after 10 s without a stall during which the lateness would have needed fewer; the next step 10 s later (a spike that was
//            seen once is not forgotten at once: the next one may be near)
//   hitches  a frame that stood for more than 100 ms (a window that is hardly drawn, a hitch of the machine) reads all the turns that came meanwhile at once: their read
//            times say how long the machine slept, not how late the link was, and they are not counted. A frame of a second or more may have been cut by whoever drives the
//            clock (the application hands the network at most a second of a frame): the clock jumped, and the runner tells the rule to forget() what it has read

#include <array>
#include <cstdint>

#include "ants_net/protocol.hpp"

namespace ants::net {

class JitterBuffer {
public:
    struct Config {
        uint32_t turn_ms = kTurnMs;
        uint32_t min_turns = 1;            // the buffer on a steady link
        uint32_t max_turns = 4;            // the buffer on the roughest link that is still played (4 turns: a turn runs 200 ms after it was read)
        uint32_t margin_ms = 10;           // kept in hand beyond the lateness that was seen
        uint32_t percentile = 95;          // the lateness that the buffer covers: this share of the last kSamples turns
        uint32_t shrink_after_ms = 10000;  // a step down after this long without a stall (and with the lateness asking for less)
        uint32_t hitch_ms = 100;           // a frame longer than this is not a measurement of the link (see above)
        uint32_t stall_grow_max_ms = 100;  // a stall that lasted longer than this (the late turn came more than two turns after it was due) does not grow the buffer
        uint32_t reset_after_ms = 1500;    // a stall of this length forgets the turns read before it
    };

    JitterBuffer() : JitterBuffer(Config{}) {}
    explicit JitterBuffer(Config config);

    /// `count` turns, numbered `first_turn` and on, were read by a frame at `now_ms` (a monotone clock in ms) that stood for `frame_ms`. They came in one bunch: the first
    /// one waited longest. Frames longer than hitch_ms are not counted.
    void on_arrivals(uint32_t first_turn, uint32_t count, uint64_t now_ms, uint32_t frame_ms);
    /// The runner had to run a turn and had none, and the turn that came after `stall_ms` of waiting (0: not known) is the first since. The buffer grows by a turn at once
    /// (up to max_turns) when the wait was short (stall_grow_max_ms); a wait of reset_after_ms or more forgets the turns that were read before it.
    void on_stall(uint64_t now_ms, uint32_t stall_ms = 0);

    /// The turns read so far are forgotten (the buffer itself stays as it is, and shrinks at its own pace). For a clock that jumped: what was measured before and what comes
    /// after do not refer to the same clock any more.
    void forget() noexcept {
        filled_ = 0;
        next_ = 0;
    }

    /// How many turns the runner keeps in hand: it runs a turn once that many are queued behind it (that is: it begins when `target() + 1` turns are queued)
    uint32_t target() const noexcept { return target_; }
    /// The lateness that the buffer covers, in ms: the `percentile`th percentile, over the last kSamples turns, of how much later than the earliest of them a turn was read
    /// (0 before two have been read)
    uint32_t lateness_ms() const noexcept;
    static constexpr size_t kSamples = 200;          // the turns that count: 10 s of play
    static constexpr size_t kMinSamples = 20;        // fewer than this (the first second of a match) say nothing about the link: the lateness is 0 until there are as many
    /// Turns the lateness plus the margin asks for (min_turns .. max_turns), before the slow shrink: what target() would be if it followed the numbers
    uint32_t wanted() const noexcept;
    const Config& config() const noexcept { return cfg_; }

private:
    uint32_t turns_for(int64_t lateness_ms) const noexcept;
    void add_sample(int64_t read_minus_sealed_ms) noexcept;

    Config cfg_;
    uint32_t target_;
    std::array<int64_t, kSamples> samples_{};        // "read time - turn * turn_ms" of the last turns, a ring
    size_t filled_{0};
    size_t next_{0};
    uint64_t last_change_ms_{0};                     // the last time the target grew or shrank, or a stall was seen: the shrink waits shrink_after_ms from there
    bool seen_{false};
};

}  // namespace ants::net
