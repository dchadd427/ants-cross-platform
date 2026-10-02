#pragma once

// What a player feels of the network, measured on the player's own machine and shown next to the frame rate (ants_app/latency_corner.hpp):
//
//   ping   the round trip to the host: a Ping goes out once a second, the host answers with a Pong that echoes the nonce and the send time (the messages exist in both
//          directions since protocol 1; the room's host and a server's room answer a guest's Ping, and so does a running match, so no protocol change is needed). The
//          number is the mean of the last few answers.
//   delay  the real time from the moment one of the player's own commands is sent (a guest: written to the link; the host: handed to the sequencer) to the moment the
//          tick that applies it is executed on this machine, the mean over the last ten commands. It is the sum of everything between the click and its effect:
//          the way to the host, the wait for the next 100 ms turn to be sealed, the way back and the jitter buffer of the lock-step runner (lockstep.hpp).
//
// Pure logic like the sequencer: no clock, no sockets, no simulation. Every call is given the time it needs, the game's millisecond clock (clock.hpp: the differences
// are wrap-safe). That clock is the clock of the frame, so each end of a measurement is rounded to the frame it happens in (16.7 ms at 60 frames per second): the ping
// includes the frame in which the answer is read, the delay the frame in which the command was sent and the frame in which its tick runs. That is what the player's screen
// can show, so that is what is measured.

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

/// The round trip to the host, from the Ping / Pong timings.
class PingMeter {
public:
    static constexpr size_t kAnswersKept = 5;      // the number that is shown is the mean of the last five answers (five seconds)
    static constexpr size_t kInFlight = 8;         // a slow link has several pings out at once (more than a second of round trip): the last eight are remembered

    /// The next Ping to send at `now_ms`: a nonce that is never 0 (the nonce 0 is what the guests send each other) and the send time, which is remembered: an answer
    /// counts only when it carries the send time that was recorded for its nonce (a made-up echo is not believed)
    PingMsg next(uint32_t now_ms) noexcept;
    /// A Pong arrived at `now_ms`. True when it answers a ping that is still in flight: its round trip (`now_ms` - the send time) is taken, and the ping is not in flight any
    /// more (an echo that comes twice counts once). False for everything else: nonce 0, a nonce that was never sent or is older than the last eight, another send time than
    /// the recorded one, a time before the send (the clock is the caller's: a Pong cannot arrive before its Ping went out).
    bool on_pong(const PingMsg& pong, uint32_t now_ms) noexcept;

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
};

/// The delay of one player's own commands: from the send to the tick that applies the command on this machine.
///
/// A command is matched by what it says (type, tile, other player, the ants in their order) and by its issuer, which is the player's own seat: the host keeps every issuer's
/// commands in the order they were sent, so the command that a turn applies is the oldest sent command of that content. Commands that the host refused or that a host
/// change lost are never applied; they leave the list when a later command of the same player is applied (the host keeps the order, so everything sent before it that is
/// still waiting is lost), and after kForgetAfterMs in any case.
class CommandDelayMeter {
public:
    static constexpr size_t kCommandsKept = 10;           // the number that is shown is the mean over the last ten commands
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
    /// The mean delay of the last kCommandsKept commands; 0 while not measured()
    uint32_t delay_ms() const noexcept { return delays_.mean(); }
    /// The delay of the command applied last; 0 while not measured()
    uint32_t last_ms() const noexcept { return delays_.last(); }
    /// How many delays are held (at most kCommandsKept)
    size_t samples() const noexcept { return delays_.size(); }
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
    MeanWindow<kCommandsKept> delays_;
};

}  // namespace ants::net
