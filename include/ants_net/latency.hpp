#pragma once

// What a player feels of the network, measured on the player's own machine and shown next to the frame rate (ants_app/latency_corner.hpp):
//
//   ping   the round trip to the host: a Ping goes out once a second, the host answers with a Pong that echoes the nonce and the send time (the messages exist in both
//          directions since protocol 1; the room's host and a server's room answer a guest's Ping, and so does a running match, so no protocol change is needed). The
//          number is the mean of the last five answers, and is not shown (a dash) when the last answer is older than three seconds.
//   delay  the real time from the moment one of the player's own commands is sent (a guest: written to the link; the host: handed to the sequencer) to the moment the
//          tick that applies it is executed on this machine, the median of the last five commands. It is the sum of everything between the click and its effect:
//          the way to the host, the wait for the next 50 ms turn to be sealed, the way back and the jitter buffer of the lock-step runner (lockstep.hpp). It is not shown (a
//          dash) when the last command was applied more than ten seconds ago: orders are given now and then, so its limit is longer than the ping's.
//
// Pure logic like the sequencer: no clock, no sockets, no simulation. Every call is given the time it needs, the game's millisecond clock (clock.hpp: the differences
// are wrap-safe). That clock is the clock of the frame, so each end of a measurement is rounded to the frame it happens in (16.7 ms at 60 frames per second): the delay
// includes the frame in which the command was sent and the frame in which its tick runs, which is what the player's screen can show. The ping is better than that where the
// transport can say when a message really came (Connection::last_message_age_ms: the browser's WebSocket stamps each message when the page's event delivers it): the time
// that the Pong waited for the frame that read it is taken off, and the readout is the link's round trip at any frame rate (it read up to a frame, a second in a hidden tab,
// too high). A native socket is read when the frame polls it: a native window at 60 frames per second reads up to one frame (16.7 ms, 8 on average) high.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"

namespace ants::net {

/// The mean of the last N values that were added, rounded to the nearest whole number (halves up). Holds nothing until the first add().
template <size_t N>
class MeanWindow {
    static_assert(N > 0, "a window holds at least one value");

public:
    void add(uint32_t value) noexcept {
        if (count_ == N) sum_ -= values_[next_];                     // the oldest value leaves
        else ++count_;
        values_[next_] = value;
        sum_ += value;
        next_ = (next_ + 1) % N;
    }
    bool empty() const noexcept { return count_ == 0; }
    size_t size() const noexcept { return count_; }
    /// The mean of the values held; 0 when there are none (ask empty() first)
    uint32_t mean() const noexcept { return count_ == 0 ? 0u : static_cast<uint32_t>((sum_ + count_ / 2) / count_); }
    /// The value added last; 0 when there is none
    uint32_t last() const noexcept { return count_ == 0 ? 0u : values_[(next_ + N - 1) % N]; }

private:
    std::array<uint32_t, N> values_{};
    uint64_t sum_{0};
    size_t next_{0};
    size_t count_{0};
};

/// The median of the last N values that were added: the middle one of an odd number, the mean of the two in the middle (rounded to the nearest whole number, halves up) of an
/// even number, which is what it is while fewer than N have come. One odd value never shows, and a change shows when N / 2 + 1 values of the new kind are in. Holds nothing
/// until the first add().
template <size_t N>
class MedianWindow {
    static_assert(N > 0, "a window holds at least one value");

public:
    void add(uint32_t value) noexcept {
        values_[next_] = value;
        next_ = (next_ + 1) % N;
        if (count_ < N) ++count_;
    }
    bool empty() const noexcept { return count_ == 0; }
    size_t size() const noexcept { return count_; }
    /// The median of the values held; 0 when there are none (ask empty() first)
    uint32_t median() const noexcept {
        const size_t n = std::min(count_, N);
        if (n == 0) return 0u;
        std::array<uint32_t, N> sorted = values_;                    // (until the window is full the values held are the first n of the ring)
        for (size_t i = 1; i < n; ++i) {                             // an insertion sort: N is small, and std::sort's threshold of 16 made GCC 12 read a window of five as possibly longer
            const uint32_t v = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j - 1] > v) {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = v;
        }
        if (n % 2 == 1) return sorted[n / 2];
        return static_cast<uint32_t>((static_cast<uint64_t>(sorted[n / 2 - 1]) + sorted[n / 2] + 1) / 2);
    }
    /// The value added last; 0 when there is none
    uint32_t last() const noexcept { return count_ == 0 ? 0u : values_[(next_ + N - 1) % N]; }

private:
    std::array<uint32_t, N> values_{};
    size_t next_{0};
    size_t count_{0};
};

/// The round trip to the host, from the Ping / Pong timings.
class PingMeter {
public:
    static constexpr size_t kAnswersKept = 5;      // the number that is shown is the mean of the last five answers (five seconds)
    static constexpr size_t kInFlight = 8;         // a slow link has several pings out at once (more than a second of round trip): the last eight are remembered

    /// The next Ping to send at `now_ms`: a nonce that is never 0 (the nonce 0 is what the guests send each other) and the send time, which is remembered: an answer
    /// counts only when it carries the send time that was recorded for its nonce (a made-up echo is not believed)
    PingMsg next(uint32_t now_ms) noexcept;
    /// A Pong was read at `now_ms` (the frame's clock) after it had waited `age_ms` to be read (Connection::last_message_age_ms: what the transport knows of when the message
    /// really came; 0 when it does not know). True when it answers a ping that is still in flight: its round trip (`now_ms` - the send time - `age_ms`: the time that the
    /// message spent in the page's queue is not the link's) is taken, and the ping is not in flight any more (an echo that comes twice counts once). False for everything else:
    /// nonce 0, a nonce that was never sent or is older than the last eight, another send time than the recorded one, a time before the send (the clock is the caller's: a Pong
    /// cannot arrive before its Ping went out), and an age that is more than the frame clock has run since the send (a window that was not drawn for a while hands the network
    /// at most a second of it, so the clock and the messages' ages do not refer to the same time any more: the answer measures nothing).
    bool on_pong(const PingMsg& pong, uint32_t now_ms, uint32_t age_ms = 0) noexcept;
    /// True when the last answer is older than `max_age_ms` at `now_ms` (the frame's clock), or there was none: the number is no longer a reading of the link. The answer's time
    /// is the time at which it came (`now_ms` of the frame that read it, less its age).
    bool stale(uint32_t now_ms, uint32_t max_age_ms = kStaleAfterMs) const noexcept { return !measured() || now_ms - last_answer_ms_ > max_age_ms; }
    /// A ping reading that is older than this is not shown (the corner says "ping -"): the host answers nothing, the link is stuck, or the page was not drawn
    static constexpr uint32_t kStaleAfterMs = 3000;

    /// True once a Pong has been taken
    bool measured() const noexcept { return !answers_.empty(); }
    /// The mean of the last kAnswersKept round trips; 0 while not measured()
    uint32_t ping_ms() const noexcept { return answers_.mean(); }
    /// The last round trip taken; 0 while not measured()
    uint32_t last_ms() const noexcept { return answers_.last(); }
    /// How many round trips are held (at most kAnswersKept)
    size_t answers() const noexcept { return answers_.size(); }

private:
    struct InFlight {
        uint32_t sent_ms{0};
        bool open{false};
    };
    uint32_t nonce_{0};
    std::array<InFlight, kInFlight> in_flight_{};
    MeanWindow<kAnswersKept> answers_;
    uint32_t last_answer_ms_{0};                   // when the last answer came (the frame's clock less its age)
};

/// The delay of one player's own commands: from the send to the tick that applies the command on this machine.
///
/// A command is matched by what it says (type, tile, other player, the ants in their order) and by its issuer, which is the player's own seat: the host keeps every issuer's
/// commands in the order they were sent, so the command that a turn applies is the oldest sent command of that content. Commands that the host refused or that a host
/// change lost are never applied; they leave the list when a later command of the same player is applied (the host keeps the order, so everything sent before it that is
/// still waiting is lost), and after kForgetAfterMs in any case.
class CommandDelayMeter {
public:
    static constexpr size_t kCommandsKept = 5;            // the number that is shown is the median of the last five commands
    static constexpr size_t kMaxPending = 64;             // commands sent and not applied yet; the oldest is forgotten beyond this
    static constexpr uint32_t kForgetAfterMs = 20000;     // a command that was never applied is forgotten after this long

    /// `own_seat`: the player whose commands are measured (the commands of the other seats that turns apply are ignored); kNoSeat measures nothing
    explicit CommandDelayMeter(uint8_t own_seat = 255) noexcept : own_seat_(own_seat) {}

    /// The player's command has been sent. Its send time is the time of the next on_frame(): the application handles the player's input at the start of a frame, before the
    /// clock of that frame is taken, so a command that is sent between two frames went out at the moment of the next one. `command.issuer` is overwritten with the own seat.
    void on_sent(const sim::Command& command);
    /// The frame's clock. Commands sent since the last call are stamped with it; commands that have waited longer than kForgetAfterMs are forgotten.
    void on_frame(uint32_t now_ms);
    /// A turn applies `command` at `now_ms`. Commands of other issuers are ignored. True when it was one of the player's own sent commands (its delay is taken).
    bool on_applied(const sim::Command& command, uint32_t now_ms);

    /// True once a command has been applied
    bool measured() const noexcept { return !delays_.empty(); }
    /// The median delay of the last kCommandsKept commands; 0 while not measured(). (A median and five commands, not a mean over ten: after the game stood still for a moment the
    /// commands that were given meanwhile waited for it, and the mean of ten kept showing a second for as long as it took ten more commands to be given, 25 s for a player who
    /// gives one every 2.5 s; the median shows one such command not at all and is back after three.)
    uint32_t delay_ms() const noexcept { return delays_.median(); }
    /// The delay of the command applied last; 0 while not measured()
    uint32_t last_ms() const noexcept { return delays_.last(); }
    /// How many delays are held (at most kCommandsKept)
    size_t samples() const noexcept { return delays_.size(); }
    /// True when the last command was applied more than `max_age_ms` before `now_ms` (the frame's clock), or none was: what the number says is no longer about now (a player who
    /// has not given an order for a while, a page that was not run). Orders are given now and then, so the limit is much longer than the ping's.
    bool stale(uint32_t now_ms, uint32_t max_age_ms = kStaleAfterMs) const noexcept { return !measured() || now_ms - last_applied_ms_ > max_age_ms; }
    static constexpr uint32_t kStaleAfterMs = 10000;      // the corner says "delay -" when the last command was applied more than this long ago
    /// Commands sent and neither applied nor forgotten yet
    size_t pending() const noexcept { return pending_.size(); }

private:
    struct Pending {
        sim::Command command;
        uint32_t sent_ms{0};
        bool stamped{false};
    };
    uint8_t own_seat_;
    std::deque<Pending> pending_;
    MedianWindow<kCommandsKept> delays_;
    uint32_t last_applied_ms_{0};                         // when the last own command was applied
};

/// The delay that the player FEELS of an order while the prediction is on (net::Prediction): from the frame that took the order (the click) to the end of the first frame that shows what it did,
/// the median of the last five. It is not the network's delay: with the prediction on, the network's delay (CommandDelayMeter: from the send to the tick that applies the order in the
/// CONFIRMED engine) is the time that the confirmed engine waits, which the picture no longer does; this is the time that the picture waits: the order is in the predicted engine at once, and
/// what it does shows when the predicted engine has run its next tick (the ants' first step) and a frame has been drawn after it. Like the other meters it reads no clock: the owner tells it
/// the time. The clock is the frame's real one, so the number includes the frame that took the click and the frame that shows it, and not the display's own scan-out or the wait of the
/// event before the frame polled it (nothing in the program can see that).
class FeltDelayMeter {
public:
    static constexpr size_t kOrdersKept = 5;              // the number that is shown is the median of the last five orders
    static constexpr size_t kMaxPending = 16;             // orders whose effect has not been drawn yet; the oldest is forgotten beyond this
    static constexpr uint32_t kForgetAfterMs = 5000;      // an order whose effect is never drawn (the page was hidden, the prediction ended) is forgotten after this long
    static constexpr uint32_t kStaleAfterMs = 10000;      // the corner says "delay -" when the last order was felt more than this long ago

    /// An order of the player was taken by the predicted engine in the frame whose clock is `now_ms`
    void on_order(uint32_t now_ms) {
        if (pending_.size() >= kMaxPending) pending_.pop_front();
        pending_.push_back(Pending{now_ms, false});
    }
    /// The predicted engine has run a tick: what the orders that are waiting did is in the picture from now on
    void on_tick_shown() noexcept {
        for (Pending& p : pending_) p.shown = true;
    }
    /// A frame has been drawn at `now_ms`: the orders whose effect is in the picture are felt now, the ones that have waited too long are forgotten
    void on_frame_end(uint32_t now_ms) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->shown) {
                delays_.add(now_ms - it->order_ms);
                last_felt_ms_ = now_ms;
                it = pending_.erase(it);
            } else if (now_ms - it->order_ms > kForgetAfterMs) {
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    /// The orders that are waiting for their effect to be drawn (the prediction ended: they never will be)
    void forget_pending() noexcept { pending_.clear(); }

    bool measured() const noexcept { return !delays_.empty(); }
    /// The median felt delay of the last kOrdersKept orders; 0 while not measured()
    uint32_t felt_ms() const noexcept { return delays_.median(); }
    uint32_t last_ms() const noexcept { return delays_.last(); }
    size_t samples() const noexcept { return delays_.size(); }
    size_t pending() const noexcept { return pending_.size(); }
    /// True when the last order was felt more than `max_age_ms` before `now_ms`, or none was: what the number says is no longer about now
    bool stale(uint32_t now_ms, uint32_t max_age_ms = kStaleAfterMs) const noexcept { return !measured() || now_ms - last_felt_ms_ > max_age_ms; }

private:
    struct Pending {
        uint32_t order_ms{0};
        bool shown{false};
    };
    std::deque<Pending> pending_;
    MedianWindow<kOrdersKept> delays_;
    uint32_t last_felt_ms_{0};
};

}  // namespace ants::net
