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

std::vector<LockstepRunner::Executed> LockstepRunner::update(uint32_t dt_ms) {
    std::vector<Executed> out;
    if (!started_) {
        if (queue_.size() < cfg_.buffer_turns) return out;      // still collecting the jitter buffer
        started_ = true;
        acc_ms_ = kTickMs;                                      // the first tick is due at once
    } else {
        const bool behind = queue_.size() > cfg_.buffer_turns + cfg_.catch_up_turns;
        acc_ms_ += behind ? dt_ms * 2u : dt_ms;
    }
    uint32_t ticks = 0;
    while (acc_ms_ >= kTickMs && ticks < cfg_.max_ticks_per_update) {
        if (phase_ == 0) {
            if (queue_.empty()) break;                          // the next turn is due but has not arrived: stall at the boundary
            current_ = std::move(queue_.front());
            queue_.pop_front();
            for (const sim::Command& c : current_.commands) {
                const sim::CommandResult r = sim_.apply_command(c);
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
    return out;
}

}  // namespace ants::net
