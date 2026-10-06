#include "ants_net/lockstep.hpp"

#include <algorithm>

namespace ants::net {

bool LockstepRunner::enqueue(TurnMsg turn, bool live) {
    if (turn.turn != next_receive_ || turn.commands.size() > kMaxTurnCommands) return false;
    if (live) {                                                 // (a turn of the catch-up is no arrival of the link: the jitter buffer never hears of it)
        if (fresh_count_ == 0) fresh_first_ = turn.turn;
        ++fresh_count_;
    }
    ++next_receive_;
    log_.push_back(turn);
    if (log_.size() > kTurnLogTurns) log_.pop_front();
    queue_.push_back(std::move(turn));
    if (live && on_turn_) on_turn_(queue_.back());              // (queued: the observer finds the turn in the queue; a catch-up turn is no arrival)
    return true;
}

bool LockstepRunner::on_turn(TurnMsg turn) { return enqueue(std::move(turn), true); }

bool LockstepRunner::on_catch_up_turn(TurnMsg turn) { return enqueue(std::move(turn), false); }

const TurnMsg* LockstepRunner::logged_turn(uint32_t turn) const noexcept {
    if (log_.empty() || turn >= next_receive_) return nullptr;
    const uint32_t first = next_receive_ - static_cast<uint32_t>(log_.size());
    return turn >= first ? &log_[turn - first] : nullptr;
}

namespace {
uint32_t saturating_add(uint32_t a, uint32_t b) noexcept { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }
}  // namespace

// The newest queued turn is due when the turns before it have run: the next tick is due in 50 ms less what has gathered toward it (and the frame that is about to be added),
// and every turn between that one and the newest takes 50 ms more
uint32_t LockstepRunner::slack_ms(uint32_t dt_ms) const noexcept {
    if (queue_.empty()) return 0;
    const int64_t gathered = static_cast<int64_t>(acc_q_ / 4u) + static_cast<int64_t>(dt_ms);
    const int64_t slack = static_cast<int64_t>(kTickMs) - gathered + static_cast<int64_t>(kTickMs) * static_cast<int64_t>(queue_.size() - 1);
    return slack <= 0 ? 0u : static_cast<uint32_t>(slack);
}

// Normal speed (4 quarters) while the slack is what the buffer asks for (target * 50 ms, and a little for the frames); every 50 ms of slack beyond that runs a quarter faster,
// up to max_speed_x4. The turns are counted in time, not in whole turns: a turn's width of dead zone would leave the queue up to 49 ms longer than the buffer for good.
uint32_t LockstepRunner::speed_x4(uint32_t dt_ms) const noexcept {
    const uint32_t wanted = jitter_.target() * kTickMs + kSlackDeadMs;
    const uint32_t slack = slack_ms(dt_ms);
    if (slack <= wanted) return 4;
    const uint32_t over = slack - wanted;
    return std::min(std::max(cfg_.max_speed_x4, 4u), 4u + (over + kTickMs - 1u) / kTickMs);
}

static_assert(kTicksPerTurn == 1, "update() and fast_forward() run one tick for every turn: a turn of more ticks needs both changed, and a half-done turn at_boundary() can report");

// The runner as it is at the start of a match: nothing owed, nothing waited for, the jitter buffer at its steady value with no turn read, the next turns collected before the first of them
// runs. The turns that are queued when the next update comes (live turns that arrived while the catch-up ran, or in the frame that ended it) waited for the runner: the first update
// after a restart, like the very first update of a runner (updates_ is 0 again), does not take their read times for the link's lateness.
void LockstepRunner::restart_pacing() noexcept {
    started_ = false;
    rebuilding_ = false;
    in_stall_ = false;
    stall_told_ = false;
    stall_ms_ = 0;
    acc_q_ = 0;
    updates_ = 0;
    jitter_.reset();
}

// The match is held by the server (a pause, a countdown), or goes on. Nothing that was waited for in the pause is a stall, and nothing that was measured before it describes the link after it:
// when it ends the lateness read so far is forgotten (the first turns after a pause come all at their own pace; a runner that has used its buffer up collects it again: update() does that while the match is held).
void LockstepRunner::set_held(bool held) {
    if (held == held_) return;
    held_ = held;
    in_stall_ = false;
    stall_told_ = false;
    stall_ms_ = 0;
    if (held) return;
    jitter_.forget();                                           // (a runner whose turns in hand ran out during the pause is collecting its buffer already: update() does it while the match is held)
    fresh_count_ = 0;                                           // (a turn that was read before the end of the pause is not an arrival after it: nothing is measured from the pause)
    updates_ = 0;                                               // the first update after a pause reads what waited for the runner: how long it waited is not the link's lateness
}

uint32_t LockstepRunner::fast_forward(uint32_t max_ticks) {
    uint32_t turns = 0;
    while (turns < max_ticks && !queue_.empty()) {
        TurnMsg turn = std::move(queue_.front());
        queue_.pop_front();
        in_turn_ = true;
        for (const sim::Command& c : turn.commands) sim_.apply_command(c);        // (no presentation hook: nothing is presented, nobody is asked what the engine said)
        sim_.tick();
        if (on_executed_) on_executed_(turn);                   // (the watcher of the match sees the catch-up's turns too)
        ++next_execute_;
        in_turn_ = false;
        ++turns;
    }
    if (turns > 0) restart_pacing();
    return turns;
}

std::vector<LockstepRunner::Executed> LockstepRunner::update(uint32_t dt_ms) {
    std::vector<Executed> out;
    now_ms_ += dt_ms;
    const uint32_t hitch_ms = jitter_.config().hitch_ms;
    if (dt_ms >= kClockCutMs) jitter_.forget();                 // (a frame that long may have been cut: the clock jumped, see the header)
    if (in_stall_ && !stall_told_ && fresh_count_ > 0 && dt_ms <= hitch_ms) {       // the late turn has come: the buffer is told how long the wait was (before the turns of this frame are counted)
        jitter_.on_stall(now_ms_, saturating_add(stall_ms_, dt_ms));
        stall_told_ = true;
    }
    if (fresh_count_ > 0) {                                     // the turns read since the last update were read now (a frame's own length is told: see JitterBuffer)
        // (the first update reads whatever waited for the runner to exist: how long it waited is not the link's lateness)
        jitter_.on_arrivals(fresh_first_, fresh_count_, now_ms_, updates_ == 0 ? UINT32_MAX : dt_ms);
        fresh_count_ = 0;
    }
    ++updates_;
    if (!started_ || rebuilding_) {
        // The buffer is collected again after a stall (or for the first time): target + 1 turns are wanted. After a stall the wait is bounded: on a healthy stream the first turn
        // that is queued is followed by one more every 50 ms, so when the oldest has waited target + 1 turns' time and the buffer is still not full, the stream is not going to
        // fill it (the host froze when the match ended: nothing comes after the decisive turn; a link that is down) and the runner goes on with what it has. Without the bound the
        // last turns of a stream that the stall found behind a grown buffer were never run (the player never saw the match end). The turns were read at the start of this update,
        // so the wait counts from its clock.
        const size_t wanted_turns = static_cast<size_t>(jitter_.target()) + 1;
        if (rebuilding_ && !queue_.empty() && !rebuild_has_first_) {
            rebuild_has_first_ = true;
            rebuild_first_ms_ = now_ms_;
        }
        const bool waited_enough = rebuild_has_first_ && now_ms_ - rebuild_first_ms_ >= static_cast<uint64_t>(wanted_turns) * kTickMs;      // (only a rebuilding runner has a first turn)
        if (queue_.size() < wanted_turns && !waited_enough) {                  // still collecting the jitter buffer
            if (rebuilding_ && in_stall_) stall_ms_ = saturating_add(stall_ms_, dt_ms);    // (the wait goes on being counted: "Waiting for the other players..." stays up until the turns run; the buffer that is collected after a pause is no wait for the link)
            return out;
        }
        started_ = true;
        rebuilding_ = false;
        rebuild_has_first_ = false;
        acc_q_ = kTickQuarters;                                 // the first tick is due at once; what was waited is not owed
    } else {
        // The speed-up eats the turns that stand in the queue beyond the buffer, a little each frame, in every frame, long or short. The speed is a function of the slack that
        // is left AFTER the frame's own time has passed at normal speed (slack_ms(dt_ms)): a long frame (a hitch, a window in the background) whose time covers every turn that
        // is queued asks for no speed-up and runs exactly the turns that its time holds, leaving no buffer behind it (the next turns come on their own); one that finds more
        // than its time covers (a backlog: a window that is drawn once a second after it was frozen) runs the rest down at up to 4x. At normal speed the long frame ran no more
        // than the turns that arrived while it stood, so a backlog that it found stayed for ever (a page drawn once a second was 2 - 15 s behind the others for the rest of the match).
        acc_q_ = saturating_add(acc_q_, static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(dt_ms) * speed_x4(dt_ms), UINT32_MAX)));
    }
    // A frame may run the ticks that its time stands for at the fastest speed: max_ticks_per_update for each 100 ms of it (a window that is drawn once in 0.75 s must be able to run
    // the 15 ticks that the time holds, and the 80 that a second at 4x holds, or it falls behind the others for ever)
    const uint32_t max_ticks = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(cfg_.max_ticks_per_update) * std::max<uint64_t>(1u, (static_cast<uint64_t>(dt_ms) + 99u) / 100u), uint64_t{1} << 20));
    uint32_t ticks = 0;
    while (acc_q_ >= kTickQuarters && ticks < max_ticks && !queue_.empty()) {
        TurnMsg turn = std::move(queue_.front());
        queue_.pop_front();
        in_turn_ = true;
        for (const sim::Command& c : turn.commands) {
            const sim::CommandResult r = sim_.apply_command(c);
            if (on_applied_) on_applied_(c);
            if (on_command_) on_command_(c, r);
        }
        sim_.tick();
        if (on_executed_) on_executed_(turn);                   // (before on_tick: the application ends the match from there, and the watcher has this turn by then)
        if (on_tick_) on_tick_();
        Executed e;
        e.turn = turn.turn;
        if ((turn.turn + 1) % kHashEveryTurns == 0) {
            e.has_hash = true;
            e.hash = sim_.state_hash();
        }
        out.push_back(e);
        ++next_execute_;
        in_turn_ = false;
        acc_q_ -= kTickQuarters;
        ++ticks;
    }
    // Owed game time is bounded: a burst of ticks after a hitch is never longer than max_ticks_per_update
    acc_q_ = std::min(acc_q_, kTickQuarters * cfg_.max_ticks_per_update);
    if (ticks > 0) {                                            // every tick starts the count again, so the count never outlives the stall that it measures
        stall_ms_ = 0;
        in_stall_ = false;
        stall_told_ = false;
    }
    // A tick is due and there is no turn: nothing is owed for the wait (a late turn runs the moment it comes), and the buffer is collected again
    if (queue_.empty() && acc_q_ >= kTickQuarters) {
        acc_q_ = kTickQuarters;
        if (held_) {                                            // the server holds the match: no turn is due, so the wait is no stall; the buffer is collected for the turns that follow
            stall_ms_ = 0;
            if (started_ && !rebuilding_) {
                rebuilding_ = true;
                rebuild_has_first_ = false;
                acc_q_ = 0;
            }
            return out;
        }
        if (ticks == 0) stall_ms_ = saturating_add(stall_ms_, dt_ms);
        if (!in_stall_) {
            in_stall_ = true;
            stall_told_ = false;
            if (dt_ms <= hitch_ms) {                            // (a long frame ends empty-handed when it runs every turn that came: not the link's fault)
                rebuilding_ = true;
                rebuild_has_first_ = false;
                acc_q_ = 0;
            }
        }
    }
    return out;
}

}  // namespace ants::net
