#pragma once

// How far a person's machine is behind the host's own engine, as the host can read it in a running match (docs/BOTS.md, "Fairness in detail": a room's computer players
// wait that long). A client acknowledges every turn it has executed (TurnAck: "I have run turn t"); the host knows when its own engine ran turn t. The time from the second
// to the arrival of the acknowledgement is the round trip of the turn stream: the way down to the person's machine, the wait in its jitter buffer and for its next frame, and the
// way back. It is what a computer player that reads the host's engine leads a person of the same reaction by (what the person sees is that old, and what the person orders
// takes the way back to reach the host), so it is the number that a handicap has to match. A Ping and its Pong would give the plain round trip, but the host sends none in a
// match and a client answers none (it pings the host itself), and no message and no client code is added for this: the turns and their acknowledgements are all it reads.
//
// Pure logic like the other meters of latency.hpp: no clock and no sockets; every call is given the host's millisecond clock (clock.hpp: differences are wrap-safe). Nothing
// here changes what is sent, what a turn holds or what any machine computes.
//
// What a machine's own acknowledgements can change: acknowledging late makes a bigger reading (the room's handicap has a cap); acknowledging a turn only once the host has run it (a second
// copy of an early acknowledgement) makes a smaller one, down to about 0, which only lowers the handicap for that person; saying nothing leaves the person out of the median. An acknowledgement
// that comes before the host has run its turn is no reading. None of it can move the handicap past the cap.

#include <array>
#include <cstddef>
#include <cstdint>

#include "ants_net/latency.hpp"
#include "ants_sim/command.hpp"

namespace ants::net {

class TurnTripMeter {
public:
    static constexpr size_t kTurnsKept = 64;           // the host's own execution times of the last 64 turns (3.2 s): an acknowledgement for an older turn is a stall, not a reading
    static constexpr size_t kWindow = 21;              // the readings of one seat that the median is taken over (about one second of acknowledgements)
    static constexpr size_t kMinReadings = 3;          // fewer than this many fresh readings are no number yet
    static constexpr uint32_t kStaleMs = 1500;         // a seat that has acknowledged nothing for this long has no reading, and starts again with the next one (a pause, a hidden window)
    static constexpr uint32_t kMaxReadingMs = 1000;    // a longer trip counts as this long: a stalled machine does not drag the median far, and one late acknowledgement shows not at all

    /// The host's own engine has just run `turn` (at the host's clock `now_ms`)
    void on_executed(uint32_t turn, uint32_t now_ms) noexcept;
    /// `player` says it has run `turn` (at `now_ms`). A reading is taken only for a turn that the host has run and that is newer than the last one this seat was read for.
    void on_ack(uint8_t player, uint32_t turn, uint32_t now_ms) noexcept;
    /// The median of the readings of `player` while it has at least kMinReadings of them and the last is younger than kStaleMs; false otherwise (`median_ms` is left alone)
    bool read(uint8_t player, uint32_t now_ms, uint32_t& median_ms) const noexcept;
    /// Forgets `player` (its seat is taken by another machine)
    void reset(uint8_t player) noexcept;

private:
    struct Run {
        uint32_t turn{0};
        uint32_t at_ms{0};
        bool set{false};
    };
    struct Seat {
        MedianWindow<kWindow> readings;
        uint32_t last_ms{0};          // the clock at the last reading
        uint32_t last_turn{0};        // the turn it was for
        bool any{false};
    };
    std::array<Run, kTurnsKept> executed_{};
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
};

}  // namespace ants::net
