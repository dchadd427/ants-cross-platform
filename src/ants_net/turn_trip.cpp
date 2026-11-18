#include "ants_net/turn_trip.hpp"

#include "ants_net/clock.hpp"

namespace ants::net {

void TurnTripMeter::on_executed(uint32_t turn, uint32_t now_ms) noexcept {
    Run& run = executed_[turn % kTurnsKept];
    run.turn = turn;
    run.at_ms = now_ms;
    run.set = true;
}

void TurnTripMeter::on_ack(uint8_t player, uint32_t turn, uint32_t now_ms) noexcept {
    if (player >= sim::MAX_PLAYERS) return;
    const Run& run = executed_[turn % kTurnsKept];
    if (!run.set || run.turn != turn) return;                      // a turn that the host has not run (yet), or one that is too old to be a reading
    Seat& seat = seats_[player];
    if (seat.any && turn <= seat.last_turn) return;                // (an acknowledgement says "up to this turn": only a newer turn is a new reading, and one that is repeated gives nothing)
    const int32_t trip = static_cast<int32_t>(now_ms - run.at_ms);
    if (trip < 0) return;                                          // (it cannot arrive before the host ran the turn: the clock was set back)
    if (seat.any && now_ms - seat.last_ms > kStaleMs) seat.readings = MedianWindow<kWindow>{};      // the seat was silent for a while: what it said before is not about now
    seat.readings.add(trip > static_cast<int32_t>(kMaxReadingMs) ? kMaxReadingMs : static_cast<uint32_t>(trip));
    seat.last_ms = now_ms;
    seat.last_turn = turn;
    seat.any = true;
}

bool TurnTripMeter::read(uint8_t player, uint32_t now_ms, uint32_t& median_ms) const noexcept {
    if (player >= sim::MAX_PLAYERS) return false;
    const Seat& seat = seats_[player];
    if (!seat.any || seat.readings.size() < kMinReadings || now_ms - seat.last_ms > kStaleMs) return false;
    median_ms = seat.readings.median();
    return true;
}

void TurnTripMeter::reset(uint8_t player) noexcept {
    if (player < sim::MAX_PLAYERS) seats_[player] = Seat{};
}

}  // namespace ants::net
