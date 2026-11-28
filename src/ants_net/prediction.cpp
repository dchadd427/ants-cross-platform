#include "ants_net/prediction.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <time.h>
#endif

namespace ants::net {

namespace {

uint64_t wall_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// One timed block of work: stop() gives its wall time (the statistics) and what the budget counts (work_cost_ns). The CPU reading is taken inside the wall one, so it is never the larger.
// The clocks are the real ones unless the config names others (the tests).
class Timer {
public:
    explicit Timer(const Prediction::Config& cfg) : cfg_(cfg), wall_(wall_ns()), cpu_(cpu_ns()) {}
    uint64_t stop(uint64_t& charged_ns) const {
        const uint64_t cpu = cpu_ns() - cpu_;
        const uint64_t wall = wall_ns() - wall_;
        charged_ns = work_cost_ns(wall, cpu);
        return wall;
    }

private:
    uint64_t wall_ns() const { return cfg_.wall_clock ? cfg_.wall_clock() : wall_now_ns(); }
    uint64_t cpu_ns() const { return cfg_.cpu_clock ? cfg_.cpu_clock() : thread_cpu_ns(); }

    const Prediction::Config& cfg_;
    uint64_t wall_;
    uint64_t cpu_;
};

}  // namespace

uint64_t thread_cpu_ns() {
#if defined(_WIN32)
    FILETIME created, exited, kernel, user;
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0) {
        const auto ticks = [](const FILETIME& t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | static_cast<uint64_t>(t.dwLowDateTime); };
        return (ticks(kernel) + ticks(user)) * 100u;                       // (100 ns units)
    }
#elif !defined(__EMSCRIPTEN__)
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
#endif
    return wall_now_ns();                                                  // (no thread clock: the web build, or a call that failed)
}

Prediction::Prediction(const sim::SimulationEngine& confirmed, const LockstepRunner& runner, Config config)
    : confirmed_(&confirmed), runner_(&runner), cfg_(config) {
    cfg_.max_lead_ticks = std::max<uint32_t>(cfg_.max_lead_ticks, 1u);
    cfg_.min_lead_ticks = std::min(cfg_.min_lead_ticks, cfg_.max_lead_ticks);
    lead_ = target_lead_ = std::clamp<uint32_t>(estimate_lag_ + cfg_.lead_bias_ticks, cfg_.min_lead_ticks, cfg_.max_lead_ticks);
    cooldown_next_ = std::min(cfg_.cooldown_ticks, cfg_.cooldown_max_ticks);
}

const std::vector<sim::Command>& Prediction::applied_at_display() const noexcept {
    static const std::vector<sim::Command> none;
    return assumed_.empty() ? none : assumed_.back();
}

sim::StateHash Prediction::derived_hash() const {
    sim::SimulationEngine scratch(*confirmed_);
    const uint64_t c = scratch.current_tick();
    const uint64_t next_receive = runner_->next_turn_expected();
    uint64_t target = std::max<uint64_t>(display_, c);
    if (!pending_.empty() && target < next_receive) target = next_receive;
    const auto apply = [&](uint64_t tick) {
        if (const TurnMsg* turn = runner_->queued_turn(static_cast<uint32_t>(tick))) {
            for (const sim::Command& cmd : turn->commands) scratch.apply_command(cmd);
            return;
        }
        for (const sim::Command& cmd : commands_without_turn(tick, next_receive, target)) scratch.apply_command(cmd);
    };
    for (uint64_t t = c; t < target; ++t) {
        apply(t);
        scratch.tick();
    }
    apply(target);
    return scratch.state_hash();
}

bool Prediction::predicts(sim::CommandType type) noexcept {
    return type == sim::CommandType::GroupMove || type == sim::CommandType::GroupSpecial || type == sim::CommandType::GroupAttack || type == sim::CommandType::Stop;
}

bool Prediction::SeqWindow::take(uint32_t seq) noexcept {
    if (!any) {
        any = true;
        highest = seq;
        bits = 1u;
        return true;
    }
    if (seq > highest) {
        const uint32_t d = seq - highest;
        bits = d >= 64u ? 0u : bits << d;
        bits |= 1u;
        highest = seq;
        return true;
    }
    const uint32_t d = highest - seq;
    if (d >= 64u || ((bits >> d) & 1u) != 0u) return false;                    // a copy that was seen, or older than the window (every number in it was long since sealed or lost)
    bits |= uint64_t{1} << d;
    return true;
}

// The own orders that wait for `tick` (the tick a rebuild puts them at: not behind a turn in hand, not past the display tick) and the hints at `tick`, in the order that a turn has: by seat, each seat's
// commands in the order they were given (a stable sort of own orders and hints, which are in that order each). A hint is put at its tick or later, never earlier than a turn in hand and never held back
// to the display tick: a hint beyond the display tick waits for it.
std::vector<sim::Command> Prediction::commands_without_turn(uint64_t tick, uint64_t next_receive, uint64_t target) const {
    std::vector<sim::Command> out;
    for (const Pending& p : pending_) {
        if (std::min(std::max(p.tick, next_receive), target) == tick) out.push_back(p.command);
    }
    for (const Foreign& h : hints_) {
        if (std::max(h.tick, next_receive) == tick) out.push_back(h.command);
    }
    std::stable_sort(out.begin(), out.end(), [](const sim::Command& a, const sim::Command& b) { return a.issuer < b.issuer; });
    return out;
}

void Prediction::set_suspended(bool suspended) {
    if (suspended == suspended_) return;
    suspended_ = suspended;
    if (suspended_) stop();
}

void Prediction::set_expected_delay_ms(uint32_t ms) {
    const uint32_t ticks = (ms + sim::TICK_MS / 2u) / sim::TICK_MS;
    estimate_lag_ = ticks;
    target_lead_ = wanted_lead();
}

uint32_t Prediction::learned_lag_ticks() const noexcept {
    const uint64_t c = confirmed_tick();
    std::vector<uint32_t> fresh;
    for (const auto& lag : lags_) {
        if (c <= lag.second + cfg_.lag_fresh_ticks) fresh.push_back(lag.first);
    }
    if (fresh.empty()) return 0;
    std::sort(fresh.begin(), fresh.end());
    return std::min(fresh.back(), fresh[fresh.size() / 2] + cfg_.lag_spike_cap_ticks);       // (the longest of them, no more than the cap above their middle: see Config::learn_lead)
}

uint32_t Prediction::wanted_lead() const noexcept {
    const uint32_t learned = learned_lag_ticks();
    return std::clamp<uint32_t>((learned != 0 ? learned : estimate_lag_) + cfg_.lead_bias_ticks, cfg_.min_lead_ticks, cfg_.max_lead_ticks);
}

// The lead falls one tick at a time, the first tick at once and each further one lead_fall_every_ticks later (the picture stands still for every tick that the lead falls by, and it must never stand for more than
// one). It rises at once by two ticks and more (the link has changed); a rise of one tick waits until the delay has asked for it for lead_rise_after_ticks, so that the picture does not move back and forth by a
// tick with the rounding of a ping. (What the host's answers teach rises at once, in on_order_ack(): an order that was late is the answer, and the next one is stamped with the longer lead.)
void Prediction::update_lead() {
    target_lead_ = wanted_lead();
    if (lead_fall_wait_ > 0) --lead_fall_wait_;
    if (target_lead_ > lead_ + 1u) {
        lead_ = target_lead_;
        ++stats_.lead_changes;
        lead_high_ticks_ = 0;
    } else if (target_lead_ == lead_ + 1u) {
        if (++lead_high_ticks_ >= cfg_.lead_rise_after_ticks) {
            ++lead_;
            ++stats_.lead_changes;
            lead_high_ticks_ = 0;
        }
    } else {
        lead_high_ticks_ = 0;
        if (target_lead_ < lead_ && lead_fall_wait_ == 0) {
            --lead_;
            ++stats_.lead_changes;
            lead_fall_wait_ = cfg_.lead_fall_every_ticks;
        }
    }
}

std::vector<Prediction::PredictedAudio> Prediction::take_audio() {
    std::vector<PredictedAudio> out;
    out.swap(audio_);
    return out;
}

std::vector<Prediction::PredictedNews> Prediction::take_news() {
    std::vector<PredictedNews> out;
    out.swap(news_);
    return out;
}

std::vector<Prediction::Shift> Prediction::take_shifts() {
    std::vector<Shift> out;
    out.swap(shifts_);
    if (!running_ && confirmed_tick() > stopped_at_ + 1u) out.clear();      // (nobody drew a frame when it stopped: the picture of that moment is long gone)
    return out;
}

void Prediction::shift_all(const std::vector<sim::AntSnapshot>& shown, const std::vector<sim::AntSnapshot>& now) {
    std::unordered_map<uint32_t, const sim::AntSnapshot*> was;
    was.reserve(shown.size());
    for (const sim::AntSnapshot& b : shown) was.emplace(b.id, &b);
    for (const sim::AntSnapshot& a : now) {
        const auto it = was.find(a.id);
        if (it == was.end()) continue;
        if (it->second->px != a.px || it->second->py != a.py) add_shift(a.id, it->second->px - a.px, it->second->py - a.py);
    }
}

void Prediction::begin(uint64_t c) {
    if (c > stopped_at_ + 1u) shifts_.clear();                       // (what a stop left that nobody took: about a picture that is gone)
    running_ = true;
    stale_ = false;
    stale_hint_ = false;
    pending_.clear();
    hints_.clear();
    sealed_seen_.clear();
    ++stats_.starts;
    target_lead_ = wanted_lead();
    lead_ = target_lead_;
    lead_high_ticks_ = 0;
    lead_fall_wait_ = 0;
    rebuild(c + lead_, true);
    if (running_) shift_all(confirmed_->get_world_state().ants, pred_.get_world_state().ants);      // the screen showed the confirmed engine: every ant slides to where the predicted one has it, `lead` ticks on
}

void Prediction::stop() {
    shifts_.clear();
    if (running_) {                                                  // the screen showed the predicted engine: every ant slides back to where the confirmed one has it
        shift_all(pred_.get_world_state().ants, confirmed_->get_world_state().ants);
        stopped_at_ = confirmed_tick();
    }
    running_ = false;
    stale_ = false;
    stale_hint_ = false;
    assumed_.clear();
    pending_.clear();
    hints_.clear();
    sealed_seen_.clear();
    audio_.clear();
    news_.clear();
    lags_.clear();                                                   // (what the orders taught is about the link and the buffer as they were: a host change, a rejoin or a pause may end with another lag)
    sent_.clear();                                                   // (and the answers that are still on their way are about orders of a prediction that is gone)
}

sim::SimulationEngine& Prediction::engine() {
    if (running_ && stale_) rebuild(std::max<uint64_t>(display_, confirmed_tick() + lead_), false);
    return pred_;
}

// The commands of the state that stands at `tick`, before its tick runs. The turn of that tick when it is in hand: its commands are certain (the jitter buffer holds turns that the host sealed
// and this machine has not run yet), so the prediction runs them like the engine will. Otherwise the player's own orders that wait for this tick (the prediction applied them at the tick it
// stood at when they were given, and a rebuild puts them there again).
void Prediction::apply_commands_for(uint64_t tick) {
    std::vector<sim::Command> applied;
    if (const TurnMsg* turn = runner_->queued_turn(static_cast<uint32_t>(tick))) {
        for (const sim::Command& c : turn->commands) pred_.apply_command(c);
        applied = turn->commands;
    } else {
        // (the rebuild has put every waiting order and hint at its tick already: `tick` is both the next-receive and the target here)
        for (const Pending& p : pending_) {
            if (p.tick != tick) continue;
            applied.push_back(p.command);
        }
        for (const Foreign& h : hints_) {
            if (h.tick != tick) continue;
            applied.push_back(h.command);
        }
        std::stable_sort(applied.begin(), applied.end(), [](const sim::Command& a, const sim::Command& b) { return a.issuer < b.issuer; });
        for (const sim::Command& c : applied) pred_.apply_command(c);
    }
    assumed_.push_back(std::move(applied));
}

void Prediction::capture_events(uint64_t tick, bool replay) {
    for (sim::AudioEvent& e : pred_.poll_audio_events()) audio_.push_back(PredictedAudio{std::move(e), tick, generation_, replay});
    for (sim::NewsEvent& e : pred_.poll_news_events()) news_.push_back(PredictedNews{std::move(e), tick, generation_, replay});
    if (audio_.size() > cfg_.max_events) audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(audio_.size() - cfg_.max_events));
    if (news_.size() > cfg_.max_events) news_.erase(news_.begin(), news_.begin() + static_cast<std::ptrdiff_t>(news_.size() - cfg_.max_events));
}

void Prediction::advance_one() {
    pred_.tick();
    pred_.adopt_explored_ground_of(*confirmed_);                      // (the picture explores what the confirmed engine has explored: prediction.hpp, "Fog of war")
    capture_events(display_, false);
    ++display_;
    apply_commands_for(display_);
    ++stats_.ticks_advanced;
}

void Prediction::advance_to(uint64_t target) {
    if (display_ >= target) return;
    if (target - display_ > 4u * static_cast<uint64_t>(cfg_.max_lead_ticks) + 64u) {          // (never: the display tick follows the confirmed one tick by tick)
        rebuild(target, false);
        return;
    }
    const Timer timer(cfg_);
    if (cfg_.work_hook) cfg_.work_hook();
    while (display_ < target) advance_one();
    uint64_t charged = 0;
    const uint64_t ns = timer.stop(charged);
    stats_.advance_ns_total += ns;
    stats_.advance_ns_max = std::max(stats_.advance_ns_max, ns);
    note_cost(charged);
}

void Prediction::note_cost(uint64_t ns) {
    if (ns <= cfg_.budget_ns) return;
    ++stats_.over_budget;
    const uint64_t c = confirmed_tick();
    strikes_.push_back(c);
    while (!strikes_.empty() && strikes_.front() + cfg_.budget_window_ticks < c) strikes_.pop_front();
    if (strikes_.size() >= cfg_.budget_strikes) start_cooldown(c);
}

// The prediction is off for the next `cooldown_next_` confirmed ticks, counting from the tick `c`; the strikes start from nothing after it, and the next cool-down is twice as long (up to the cap)
void Prediction::start_cooldown(uint64_t c) {
    ++stats_.cooldowns;
    cooling_down_ = true;
    cooldown_until_ = c + cooldown_next_;
    cooldown_next_ = std::min<uint64_t>(cooldown_next_ * 2u, cfg_.cooldown_max_ticks);
    strikes_.clear();
    stop();
}

uint64_t Prediction::cooldown_ticks_left() const noexcept {
    const uint64_t c = confirmed_tick();
    return cooling_down_ && cooldown_until_ > c ? cooldown_until_ - c : 0u;
}

// A copy of the confirmed engine, run to the display tick with the turns in hand and the waiting orders: the prediction as it should be now.
void Prediction::rebuild(uint64_t target, bool is_start) {
    const Timer timer(cfg_);
    if (cfg_.work_hook) cfg_.work_hook();
    std::vector<sim::AntSnapshot> before;
    const bool measure = !is_start && running_;                      // (the ants that this rebuild moves are told to the picture: take_shifts)
    if (measure) before = pred_.get_world_state().ants;              // the picture that was on screen
    const uint64_t shown_before = display_;

    pred_ = *confirmed_;
    pred_.clear_audio_events();                                      // (what the confirmed engine has queued belongs to its own stream)
    pred_.clear_news_events();
    assumed_.clear();
    const uint64_t c = pred_.current_tick();
    base_tick_ = c;
    const uint64_t next_receive = runner_->next_turn_expected();
    if (target < c) target = c;
    if (!pending_.empty() && target < next_receive) target = next_receive;      // (an order is never applied behind a turn that is in hand: that turn was sealed without it)
    for (Pending& p : pending_) p.tick = std::min(std::max(p.tick, next_receive), target);
    for (Foreign& h : hints_) h.tick = std::max(h.tick, next_receive);        // (a hint is never put behind a turn in hand; one beyond the display tick waits for it)
    ++generation_;
    std::vector<sim::AntSnapshot> after;                               // the rebuilt engine at the tick that was on screen (a rebuild may also catch up the ticks that a stale engine skipped)
    bool have_after = false;
    for (uint64_t t = c; t < target; ++t) {
        apply_commands_for(t);
        if (measure && t == shown_before) {
            after = pred_.get_world_state().ants;
            have_after = true;
        }
        pred_.tick();
        pred_.adopt_explored_ground_of(*confirmed_);
        capture_events(t, true);
    }
    apply_commands_for(target);
    if (measure && !have_after && target == shown_before) {
        after = pred_.get_world_state().ants;
        have_after = true;
    }
    display_ = target;
    stale_ = false;

    uint64_t charged = 0;
    const uint64_t ns = timer.stop(charged);
    if (!is_start) {
        ++stats_.rebuilds;
        stats_.replay_ticks += target - c;
        stats_.rebuild_ns_total += ns;
        stats_.rebuild_ns_max = std::max(stats_.rebuild_ns_max, ns);
        if (stale_foreign_) ++stats_.rebuilds_foreign;
        else if (stale_own_timing_) ++stats_.rebuilds_own_timing;
        else if (stale_hint_) ++stats_.rebuilds_hint;
        else ++stats_.rebuilds_other;
    }
    stale_foreign_ = false;
    stale_own_timing_ = false;
    stale_hint_ = false;

    if (measure && have_after) {                                     // the same tick before and after: what the player would have seen change
        std::unordered_map<uint32_t, const sim::AntSnapshot*> was;
        was.reserve(before.size());
        for (const sim::AntSnapshot& b : before) was.emplace(b.id, &b);
        uint32_t moved = 0;
        for (const sim::AntSnapshot& a : after) {
            const auto it = was.find(a.id);
            if (it == was.end()) continue;
            const sim::AntSnapshot& b = *it->second;
            const uint32_t dx = static_cast<uint32_t>(std::abs(a.px - b.px));
            const uint32_t dy = static_cast<uint32_t>(std::abs(a.py - b.py));
            if (dx == 0 && dy == 0) continue;
            ++moved;
            if (measure_corrections_) {
                stats_.max_move_px = std::max(stats_.max_move_px, std::max(dx, dy));
                stats_.move_px_total += std::max(dx, dy);
            }
            add_shift(a.id, b.px - a.px, b.py - a.py);
        }
        if (measure_corrections_) {
            ++stats_.corrections;
            if (moved > 0) ++stats_.corrections_visible;
            stats_.ants_moved += moved;
        }
    }
    note_cost(charged);                                              // (last: over the budget often enough, it starts a cool-down, and nothing here may use the prediction afterwards)
}

// What a rebuild moved an ant by, added to what an earlier rebuild moved it by that nobody has taken yet
void Prediction::add_shift(uint32_t ant, int32_t dx, int32_t dy) {
    for (Shift& s : shifts_) {
        if (s.ant != ant) continue;
        s.dx += dx;
        s.dy += dy;
        if (s.dx == 0 && s.dy == 0) shifts_.erase(shifts_.begin() + (&s - shifts_.data()));      // (two corrections that cancel: nothing to slide)
        return;
    }
    shifts_.push_back(Shift{ant, dx, dy});
}

// An own order that no turn has carried for pending_timeout_ticks is lost (the server refused it, a host change dropped it): it must not stay in the picture
void Prediction::drop_lost_orders(uint64_t confirmed_tick_now) {
    while (!pending_.empty() && confirmed_tick_now > pending_.front().born + cfg_.pending_timeout_ticks) {
        pending_.pop_front();
        ++stats_.commands_lost;
        stale_ = true;
    }
    for (size_t i = 0; i < hints_.size();) {                         // a hint that no turn has carried for as long is dropped too (the server refused the order, or it never came)
        if (confirmed_tick_now > hints_[i].born + cfg_.pending_timeout_ticks) {
            hints_.erase(hints_.begin() + static_cast<std::ptrdiff_t>(i));
            ++stats_.hints_dropped;
            stale_ = true;
            stale_hint_ = true;
        } else {
            ++i;
        }
    }
}

// The state that an order is given to: the one that the screen shows, brought up to date. That state is never behind a turn that is in hand (such a turn was sealed before this order existed), so the prediction
// first runs up to the turns that it knows (it does so anyway when the buffer is deeper than the lead: the buffer is longer than the delay that was measured, a moment after a burst of turns). False when the
// order cannot be predicted (off, another seat's, a kind that waits) or the work of bringing the engine up to date was the strike that ended the prediction.
bool Prediction::ready_for(const sim::Command& command) {
    if (!running_ || suspended_ || command.issuer != cfg_.seat || !predicts(command.type)) return false;
    if (stale_) rebuild(std::max<uint64_t>(display_, confirmed_tick() + lead_), false);
    if (!running_) return false;                                     // (the rebuild went over the budget once too often: the prediction is over)
    const uint64_t next_receive = runner_->next_turn_expected();
    if (display_ < next_receive) advance_to(next_receive);
    return running_;
}

std::optional<Prediction::OrderPlan> Prediction::plan_order(const sim::Command& command) {
    if (!ready_for(command)) return std::nullopt;
    return OrderPlan{display_, next_seq_};
}

bool Prediction::submit(const sim::Command& command, sim::CommandResult& result) {
    if (!ready_for(command)) return false;
    result = pred_.apply_command(command);
    assumed_.back().push_back(command);
    pending_.push_back(Pending{command, display_, confirmed_tick(), display_});
    last_order_tick_ = display_;                                     // (the tick that the order is stamped with, and the tick that a hint of it names: the host seals it there when that turn is still open)
    last_order_seq_ = next_seq_++;
    if (next_seq_ == 0) next_seq_ = 1;                               // (a number is never 0: the host answers an order that carries one)
    sent_.push_back(Sent{last_order_seq_, confirmed_tick(), display_});
    while (sent_.size() > 64u) sent_.pop_front();                    // (an answer that is not here by now is not coming)
    ++stats_.commands_predicted;
    return true;
}

void Prediction::on_order_ack(uint32_t seq, uint64_t open_turn) {
    const auto it = std::find_if(sent_.begin(), sent_.end(), [seq](const Sent& s) { return s.seq == seq; });
    if (it == sent_.end()) return;
    const Sent order = *it;
    sent_.erase(it);
    ++stats_.order_acks;
    if (open_turn > order.stamp) ++stats_.order_acks_late;
    if (!cfg_.learn_lead) return;
    const uint64_t lag = open_turn > order.born ? open_turn - order.born : 0u;      // the ticks from the confirmed tick at which the order was given to the first turn that was still open when it arrived
    lags_.emplace_back(static_cast<uint32_t>(std::clamp<uint64_t>(lag, 1u, cfg_.max_lead_ticks)), confirmed_tick());
    while (lags_.size() > cfg_.lag_samples) lags_.pop_front();
    target_lead_ = wanted_lead();
    if (running_ && !suspended_ && target_lead_ > lead_) {           // (a lag that is longer than the lead raises it at once: the next tick runs the display on, and the next order is stamped with the longer lead)
        lead_ = target_lead_;
        ++stats_.lead_changes;
        lead_high_ticks_ = 0;
    }
}

// A live turn is in the queue now. If the prediction has already started its tick, the turn must carry exactly what the prediction applied there.
void Prediction::on_turn(const TurnMsg& turn) {
    if (!running_ || suspended_) return;
    ++stats_.turns_checked;
    const uint64_t n = turn.turn;
    bool wrong = false;
    bool foreign = false;
    bool own_timing = false;
    for (const sim::Command& cmd : turn.commands) {
        if (cmd.issuer != cfg_.seat) {
            if (!take_hint_for(cmd, n, wrong)) {                     // (a command that no hint announced: the prediction learns it now, as it always did)
                foreign = true;
                if (predicts(cmd.type)) sealed_seen_.push_back(SealedSeen{cmd, n, false});
            }
            continue;
        }
        size_t i = 0;
        while (i < pending_.size() && !(pending_[i].command == cmd)) ++i;
        if (i == pending_.size()) continue;                          // (an order of a kind that is not predicted, or one that was given while the prediction was off)
        if (pending_[i].tick != n) own_timing = true;                // sealed at another tick than the one at which the prediction applied it
        ++(n == pending_[i].first_tick ? stats_.sealed_exact : n < pending_[i].first_tick ? stats_.sealed_early : stats_.sealed_late);
        if (i > 0) {                                                 // the orders before it were given earlier and have not been sealed: the host did not take them
            stats_.commands_lost += i;
            wrong = true;
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    }
    if (own_timing) wrong = true;
    while (!sealed_seen_.empty() && sealed_seen_.front().turn + 64u < n) sealed_seen_.erase(sealed_seen_.begin());
    if (!stale_ && n >= base_tick_ && n <= display_) {
        const size_t index = static_cast<size_t>(n - base_tick_);
        if (index >= assumed_.size() || !(assumed_[index] == turn.commands)) wrong = true;
    }
    if (!wrong && !stale_) return;
    if (wrong) {
        stale_ = true;
        stale_foreign_ = stale_foreign_ || (foreign && !own_timing);
        stale_own_timing_ = stale_own_timing_ || own_timing;
    }
    // The orders that this turn should have carried and did not are sealed later. The first time that happens to an order it is expected in the NEXT turn (an order that was late is most
    // often one tick late: the host seals it in the first turn that was open, which is the next one when the lag has grown by a tick); an order that the next turn does not carry either stands at the display tick, as if it had
    // just been given (a correction that left it where it was would be repeated by every turn that comes: one rebuild a turn until it is sealed)
    for (Pending& p : pending_) {
        if (p.tick > n) continue;
        p.tick = p.misses == 0 ? n + 1u : display_;                  // (a rebuild puts it no later than the display tick and never behind a turn in hand)
        ++p.misses;
    }
    // A hint is a guess: the first turn that comes without it moves it to the next tick (a late order is most often one tick late), the second one drops it: the sealed stream wins
    for (size_t i = 0; i < hints_.size();) {
        if (hints_[i].tick > n) {
            ++i;
        } else if (hints_[i].misses == 0) {
            hints_[i].tick = n + 1u;
            ++hints_[i].misses;
            ++i;
        } else {
            hints_.erase(hints_.begin() + static_cast<std::ptrdiff_t>(i));
            ++stats_.hints_dropped;
        }
    }
}

// The sealed command `cmd` of another seat meets the hints. True when a hint announced it (the oldest hint of that seat that equals it: the hints are sorted by seat and number); the hints of that seat before
// it were not sealed (the server takes a seat's orders in the order they came), so they are dropped. `wrong` is set when the prediction has to be corrected: a hint was dropped, or it stood at another tick.
bool Prediction::take_hint_for(const sim::Command& cmd, uint64_t n, bool& wrong) {
    size_t i = 0;
    while (i < hints_.size() && !(hints_[i].command == cmd)) ++i;
    if (i == hints_.size()) return false;
    size_t first = i;
    while (first > 0 && hints_[first - 1].command.issuer == cmd.issuer) --first;
    if (first < i) {
        stats_.hints_dropped += i - first;
        wrong = true;
        stale_hint_ = true;
    }
    if (hints_[i].tick == n) {
        ++stats_.hints_confirmed;
    } else {
        ++stats_.hints_confirmed_moved;
        wrong = true;
        stale_hint_ = true;
    }
    hints_.erase(hints_.begin() + static_cast<std::ptrdiff_t>(first), hints_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    return true;
}

Prediction::HintVerdict Prediction::hint(const sim::Command& command, uint32_t seq, uint64_t tick) {
    ++stats_.hints_received;
    if (!running_ || suspended_ || command.issuer >= sim::MAX_PLAYERS || command.issuer == cfg_.seat || !predicts(command.type) || hints_.size() >= cfg_.max_hints) {
        ++stats_.hints_refused;
        return HintVerdict::Refused;
    }
    if (!seen_[command.issuer].take(seq)) {
        ++stats_.hints_duplicate;
        return HintVerdict::Duplicate;
    }
    const uint64_t next_receive = runner_->next_turn_expected();
    bool late = tick + cfg_.hint_max_age_ticks < next_receive;       // older than any order that is not sealed yet
    for (SealedSeen& s : sealed_seen_) {
        if (late || s.matched || !(s.command == command)) continue;
        s.matched = true;                                           // the turn that carries this order came before its hint
        late = true;
    }
    if (late) {
        ++stats_.hints_late;
        return HintVerdict::Late;
    }
    const uint64_t horizon = std::max<uint64_t>(display_, next_receive) + cfg_.max_lead_ticks;      // (a hint that stands far ahead of the display waits there for nothing: a number that no honest sender makes)
    Foreign f{command, seq, std::min(std::max(tick, next_receive), horizon), confirmed_tick(), 0};
    const uint64_t at = f.tick;
    const auto pos = std::upper_bound(hints_.begin(), hints_.end(), f, [](const Foreign& a, const Foreign& b) {
        return a.command.issuer != b.command.issuer ? a.command.issuer < b.command.issuer : a.seq < b.seq;
    });
    hints_.insert(pos, std::move(f));
    ++stats_.hints_accepted;
    if (stale_) return HintVerdict::Accepted;                       // (the rebuild that is due has it)
    if (at < display_) {
        stale_ = true;                                              // the engine is past the tick: a rebuild puts the order there
        stale_hint_ = true;
    } else if (at == display_) {
        bool in_order = !assumed_.empty();                          // applied now, as an own order is, when that keeps the order of a turn (a rebuild would give the same engine); else a rebuild
        if (in_order) {
            for (const sim::Command& c : assumed_.back()) in_order = in_order && c.issuer < command.issuer;
        }
        if (in_order) {
            pred_.apply_command(command);
            assumed_.back().push_back(command);
        } else {
            stale_ = true;
            stale_hint_ = true;
        }
    }
    return HintVerdict::Accepted;
}

void Prediction::on_tick() {
    if (suspended_) return;
    const uint64_t c = confirmed_tick();
    if (cooling_down_) {
        if (c < cooldown_until_) return;
        cooling_down_ = false;                                       // (over: it begins again below, from the confirmed engine, as after any suspension)
    }
    if (!running_) {
        begin(c);
        return;
    }
    if (assumed_.empty() || c != base_tick_ + 1) {                  // a tick that this prediction was not told of (a catch-up, a restart of the runner): its log is about another moment
        stale_ = true;
    } else {
        assumed_.pop_front();
        base_tick_ = c;
    }
    drop_lost_orders(c);
    while (!sent_.empty() && c > sent_.front().born + cfg_.pending_timeout_ticks) sent_.pop_front();     // (an order that the host never answered: it was refused, or a host change lost it)
    update_lead();
    if (stale_) return;                                              // (rebuilt when asked for: engine(), submit(): once, with the turns as they are by then)
    advance_to(c + lead_);
}

}  // namespace ants::net
