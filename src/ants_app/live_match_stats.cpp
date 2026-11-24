#include "ants_app/live_match_stats.hpp"

namespace ants::app {

void LiveMatchStats::start(const sim::SimulationEngine& engine) {
    reset();
    collector_ = std::make_unique<replay::MatchStatsCollector>(engine.roster_mask());
    state_ = State::Counting;
    last_tick_ = engine.current_tick();
    if (last_tick_ != 0) fail("the match was not played from its first turn");
}

void LiveMatchStats::fail(const char* why) {
    if (collector_) collector_->fail(why);
}

// The one place that a tick is looked at. The engine's own count of ticks says whether this pass saw them all: it goes up by one for each tick, except for the tick that ends a match on its clock and for any tick after the end, which the
// engine does not count (SimulationEngine::tick leaves before it does), so a match that is over may stand still.
void LiveMatchStats::see(sim::SimulationEngine& engine) {
    const uint64_t tick = engine.current_tick();
    if (tick != last_tick_ + 1 && !(tick == last_tick_ && engine.is_match_over())) fail("a tick was not seen");
    last_tick_ = tick;
    ++turns_;
    collector_->on_tick(turns_, engine);                 // (never throws: a world that cannot be read makes the pass fail)
}

void LiveMatchStats::on_tick(sim::SimulationEngine& engine) {
    if (state_ != State::Counting) return;
    see(engine);
}

void LiveMatchStats::on_turn(const net::TurnMsg& turn, sim::SimulationEngine& engine) {
    if (state_ != State::Counting) return;
    if (turn.turn != turns_) fail("a turn was not seen");
    see(engine);
}

void LiveMatchStats::abandon() {
    if (state_ != State::Counting) return;
    collector_.reset();
    state_ = State::Idle;
    turns_ = 0;
    last_tick_ = 0;
}

void LiveMatchStats::reset() {
    abandon();
    report_ = MatchReport{};
    stats_ = replay::MatchStats{};
}

const MatchReport& LiveMatchStats::finish(sim::SimulationEngine& engine, uint8_t local_seat) {
    if (state_ != State::Counting) return none_;                // (nothing was counted: not the report of an earlier match, which report() holds until the next match starts)
    if (engine.current_tick() != last_tick_) fail("a tick ran that was not seen");
    stats_ = collector_->take(turns_);
    collector_.reset();
    state_ = State::Idle;
    report_ = make_match_report(stats_, engine.get_world_state().match_result, engine.game_mode(), local_seat);
    turns_ = 0;
    last_tick_ = 0;
    return report_;
}

}  // namespace ants::app
