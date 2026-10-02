#include "ants_net/lockstep.hpp"

#include <algorithm>

namespace ants::net {

bool LockstepRunner::on_turn(TurnMsg turn) {
    if (turn.turn != next_receive_ || turn.commands.size() > kMaxTurnCommands) return false;
    ++next_receive_;
    log_.push_back(turn);
    if (log_.size() > kTurnLogTurns) log_.pop_front();
    queue_.push_back(std::move(turn));
    return true;
}

const TurnMsg* LockstepRunner::logged_turn(uint32_t turn) const noexcept {
    if (log_.empty() || turn >= next_receive_) return nullptr;
    const uint32_t first = next_receive_ - static_cast<uint32_t>(log_.size());
    return turn >= first ? &log_[turn - first] : nullptr;
}

namespace {
uint32_t saturating_add(uint32_t a, uint32_t b) noexcept { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }
}  // namespace

// Whether the runner runs at double speed: more turns are queued than the buffer and the catch-up allowance, or fewer but more than the buffer at every one of the last
// standing_window turns that were started (a standing queue)
bool LockstepRunner::behind() const noexcept {
    if (queue_.size() > cfg_.buffer_turns + cfg_.catch_up_turns) return true;
    if (cfg_.standing_window == 0 || started_with_.size() < cfg_.standing_window) return false;
    return *std::min_element(started_with_.begin(), started_with_.end()) > cfg_.buffer_turns;
}

std::vector<LockstepRunner::Executed> LockstepRunner::update(uint32_t dt_ms) {
    std::vector<Executed> out;
    if (!started_ || rebuilding_) {
        if (queue_.size() < cfg_.buffer_turns) {                // still collecting the jitter buffer
            if (rebuilding_) idle_ms_ = saturating_add(idle_ms_, dt_ms);   // (the wait goes on being counted: "Waiting for the other players..." stays up until the turns run)
            return out;
        }
        const bool was_rebuilding = rebuilding_;
        started_ = true;
        rebuilding_ = false;
        started_with_.clear();
        acc_ms_ = kTickMs;                                      // the first tick is due at once
        if (was_rebuilding) acc_ms_ += behind() ? dt_ms * 2u : dt_ms;      // (a buffer that is rebuilt resumes like a running one: the time of this frame counts; the stall is not owed)
    } else {
        acc_ms_ += behind() ? dt_ms * 2u : dt_ms;
    }
    // A frame that stood for a long time (a window that is hardly drawn, a hitch) may also run the ticks that its time beyond 100 ms stands for: with the allowance of a
    // frame alone, a window that is drawn once in 0.75 s could run no more than 8 of the 15 ticks that the time holds, its game would run at half speed, and the other
    // players' games would wait for it (the host does not seal more than 30 turns ahead of the slowest peer)
    const uint32_t max_ticks = cfg_.max_ticks_per_update + (dt_ms > 100u ? (dt_ms - 100u) / kTickMs : 0u);
    uint32_t ticks = 0;
    while (acc_ms_ >= kTickMs && ticks < max_ticks) {
        if (phase_ == 0) {
            if (queue_.empty()) break;                          // the next turn is due but has not arrived: stall at the boundary
            if (cfg_.standing_window != 0) {
                started_with_.push_back(static_cast<uint32_t>(queue_.size()));
                if (started_with_.size() > cfg_.standing_window) started_with_.pop_front();
            }
            current_ = std::move(queue_.front());
            queue_.pop_front();
            for (const sim::Command& c : current_.commands) {
                const sim::CommandResult r = sim_.apply_command(c);
                if (on_applied_) on_applied_(c);
                if (on_command_) on_command_(c, r);
            }
            sim_.tick();                                        // first tick of the turn
            if (on_tick_) on_tick_();
            phase_ = 1;
        } else {
            sim_.tick();                                        // second tick: the turn is complete
            if (on_tick_) on_tick_();
            Executed e;
            e.turn = current_.turn;
            if ((current_.turn + 1) % kHashEveryTurns == 0) {
                e.has_hash = true;
                e.hash = sim_.state_hash();
            }
            out.push_back(e);
            ++next_execute_;
            phase_ = 0;
        }
        acc_ms_ -= kTickMs;
        ++ticks;
    }
    // While stalled the accumulator must not grow without bound, or the late turns would all run in one burst
    acc_ms_ = std::min(acc_ms_, kTickMs * cfg_.max_ticks_per_update);
    // how long nothing has run: every tick starts the count again, so the count never outlives the stall that it measures (the accumulator does: see stalled_ms)
    idle_ms_ = ticks > 0 ? 0u : saturating_add(idle_ms_, dt_ms);
    // The buffer is gone and the next turn has been missing for a while: stop and collect it again (see the header). The time that was waited is not owed, and a queue of
    // the turns that come in a bunch afterwards is run down by the catch-up like any other.
    if (cfg_.rebuild_after_ms != 0 && !rebuilding_ && phase_ == 0 && queue_.empty() && idle_ms_ >= cfg_.rebuild_after_ms) {
        rebuilding_ = true;
        acc_ms_ = 0;
        started_with_.clear();
    }
    return out;
}

}  // namespace ants::net
