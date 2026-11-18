#pragma once

// The network handicap of a room's computer players (docs/BOTS.md, "Fairness in detail"). A bot of a server's room reads the referee's own engine and its commands go straight into the
// sequencer: it pays no network, while a person in the room sees the match one way trip late and orders one way trip behind it. The room therefore holds each bot command back by what the
// people are behind: the median round trip of the turn stream of the persons who are connected (ants_net/turn_trip.hpp), in whole ticks, never more than kBotLagMaxTicks.
//
// Only WHEN a command goes into the sequencer moves. The commands keep their order and none is dropped, the bot's budget is spent when the controller releases a command (before this),
// the sealed turns carry the commands as before (so a recording of the match plays back without the handicap and without any bot), and nothing of the simulation, the lock-step rules or
// the protocol is touched. With nobody connected, or a round trip under about 35 ms, the hold is 0 and a command goes through exactly as it did before.
//
// Pure logic like the sequencer: no clock, no sockets; the room tells it the tick and the readings.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

#include "ants_sim/command.hpp"

namespace ants::server {

inline constexpr uint32_t kBotLagMaxTicks = 6;         // 300 ms: the most that a room holds its bots back, whatever the readings say
inline constexpr uint32_t kBotLagTickMs = 50;          // a tick and a turn
inline constexpr uint32_t kBotLagBandMs = 10;          // the reading must pass the boundary between two holds by this much before the hold moves (the number does not flicker)
inline constexpr uint32_t kBotLagDwellTicks = 20;      // after a change the hold stays at least one second (so it rarely changes in the middle of a timed chain of a bot, which times its clicks by the turn that applied_at names)
inline constexpr uint32_t kBotLagBlindTicks = 200;     // people are connected and none can be read for this long (10 s): the hold goes to 0

/// The hold, in whole ticks, that a round trip of `ms` stands for: the nearest whole number of ticks (halves up), at most kBotLagMaxTicks
uint32_t bot_lag_ticks_for(uint32_t ms) noexcept;

/// The median of `n` readings: the middle one, or the mean of the two in the middle (halves up) when n is even; 0 for none
uint32_t bot_lag_median(const uint32_t* ms, size_t n) noexcept;

/// The hold of one room, in ticks. `update` is called once for every tick that the referee runs.
class BotLag {
public:
    /// `humans`: the persons who are connected now; `trips_ms`: the round trip of the turn stream of those among them who have a fresh reading (`n` values). Nobody connected: the hold is 0 at once.
    /// People connected and no reading among them (a window that was hidden, a machine that says nothing): the hold stays what it was for kBotLagBlindTicks and then goes to 0 (ticks do not run while
    /// a match is paused, so a pause does not count). Otherwise the hold moves to the median's whole ticks when the median is past the boundary by kBotLagBandMs, and no sooner than
    /// kBotLagDwellTicks after its last change.
    void update(uint64_t tick, size_t humans, const uint32_t* trips_ms, size_t n) noexcept;
    uint32_t hold_ticks() const noexcept { return hold_; }

private:
    uint32_t hold_{0};
    uint64_t changed_at_{0};
    bool changed_{false};
    uint64_t blind_since_{0};
    bool blind_{false};
};

/// The sink of one bot seat of a room: holds each command back for the room's hold and puts it into the sequencer in the order it came. With a hold of 0 and nothing waiting, a command goes
/// through at once, as the room's sink did before the hold existed.
class LaggedSink final : public sim::CommandSink {
public:
    struct Line {
        std::function<bool()> accepts;                              // the session would take this seat's command now (HostSession::accepts_bot)
        std::function<bool(const sim::Command&)> deliver;           // into the sequencer (HostSession::submit_bot)
        std::function<uint64_t()> turns_sealed;                     // the number of the turn that the sequencer seals next (HostSession::turns_sealed)
        std::function<uint32_t()> hold_ticks;                       // the room's hold now (BotLag::hold_ticks)
    };
    explicit LaggedSink(Line line) : line_(std::move(line)) {}

    sim::CommandResult submit(const sim::Command& command) override;
    /// The turn that a command submitted now will be sealed into when the room seals on time: the sequencer's next turn plus the hold, and never before the command that is last in line. A room
    /// that was late and seals several turns in one pass puts a command in a turn or two later than this; a timed chain whose second click would then miss its window is dropped by the controller
    uint64_t applied_at(uint64_t now) const override;
    /// Called after every tick that the referee has run, before the bot looks: puts every command whose time has come into the sequencer, in order. While the session does not accept a bot's
    /// command (the match waits for a person who is away) the commands wait; the world does not move meanwhile.
    void release();
    size_t held() const noexcept { return queue_.size(); }
    /// What the sink has done: the commands that had to wait (cumulative), and the longest that one waited, in turns (at most kBotLagMaxTicks, and a turn or two more when the room was late sealing)
    uint64_t held_total() const noexcept { return held_total_; }
    uint32_t longest_wait() const noexcept { return longest_wait_; }

private:
    struct Held {
        uint64_t due;                                               // the turn counter at which it goes in
        uint64_t at;                                                // ... and the one it was handed over at
        sim::Command command;
    };
    Line line_;
    std::deque<Held> queue_;
    uint64_t last_due_{0};
    uint64_t held_total_{0};
    uint32_t longest_wait_{0};
};

}  // namespace ants::server
