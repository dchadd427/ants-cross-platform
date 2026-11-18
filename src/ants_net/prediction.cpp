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
        for (const Pending& p : pending_) {
            if (std::min(std::max(p.tick, next_receive), target) == tick) scratch.apply_command(p.command);
        }
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
    uint32_t lowest = 0;
    for (const auto& lag : lags_) {
        if (c <= lag.second + cfg_.lag_fresh_ticks && (lowest == 0 || lag.first < lowest)) lowest = lag.first;
    }
    return lowest;                                                    // (the shortest of them: see Config::learn_lead)
}

uint32_t Prediction::wanted_lead() const noexcept {
    const uint32_t learned = learned_lag_ticks();
    return std::clamp<uint32_t>((learned != 0 ? learned : estimate_lag_) + cfg_.lead_bias_ticks, cfg_.min_lead_ticks, cfg_.max_lead_ticks);
}

// The lead falls at once (a lead that is too long is the worse mistake: the order is sealed BEFORE the tick that the player aimed at) and rises at once by two ticks and more (the link has
// changed); a rise of one tick waits until the delay has asked for it for lead_rise_after_ticks, so that the picture does not move back and forth by a tick with the phase of the host's seals
void Prediction::update_lead() {
    target_lead_ = wanted_lead();
    if (target_lead_ < lead_ || target_lead_ > lead_ + 1u) {
        if (target_lead_ != lead_) ++stats_.lead_changes;
        lead_ = target_lead_;
        lead_high_ticks_ = 0;
    } else if (target_lead_ == lead_ + 1u) {
        if (++lead_high_ticks_ >= cfg_.lead_rise_after_ticks) {
            ++lead_;
            ++stats_.lead_changes;
            lead_high_ticks_ = 0;
        }
    } else {
        lead_high_ticks_ = 0;
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
    return out;
}

void Prediction::begin(uint64_t c) {
    running_ = true;
    stale_ = false;
    pending_.clear();
    shifts_.clear();
    ++stats_.starts;
    lead_ = target_lead_;
    lead_high_ticks_ = 0;
    rebuild(c + lead_, true);
}

void Prediction::stop() {
    running_ = false;
    stale_ = false;
    assumed_.clear();
    pending_.clear();
    audio_.clear();
    news_.clear();
    shifts_.clear();
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
        for (const Pending& p : pending_) {
            if (p.tick != tick) continue;
            pred_.apply_command(p.command);
            applied.push_back(p.command);
        }
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
    const bool measure = (measure_corrections_ || cfg_.track_shifts) && !is_start && running_;
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
        else ++stats_.rebuilds_other;
    }
    stale_foreign_ = false;
    stale_own_timing_ = false;

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
            if (cfg_.track_shifts) add_shift(a.id, b.px - a.px, b.py - a.py);
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
}

bool Prediction::submit(const sim::Command& command, sim::CommandResult& result) {
    if (!running_ || suspended_ || command.issuer != cfg_.seat || !predicts(command.type)) return false;
    if (stale_) rebuild(std::max<uint64_t>(display_, confirmed_tick() + lead_), false);
    if (!running_) return false;                                     // (the rebuild went over the budget once too often: the prediction is over)
    // The order is given to the state that the screen shows. That state is never behind a turn that is in hand (such a turn was sealed before this order existed), so the prediction first runs
    // up to the turns that it knows (it does so anyway when the buffer is deeper than the lead: the buffer is longer than the delay that was measured, a moment after a burst of turns)
    const uint64_t next_receive = runner_->next_turn_expected();
    if (display_ < next_receive) advance_to(next_receive);
    if (!running_) return false;
    result = pred_.apply_command(command);
    assumed_.back().push_back(command);
    pending_.push_back(Pending{command, display_, confirmed_tick(), display_});
    ++stats_.commands_predicted;
    return true;
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
            foreign = true;
            continue;
        }
        size_t i = 0;
        while (i < pending_.size() && !(pending_[i].command == cmd)) ++i;
        if (i == pending_.size()) continue;                          // (an order of a kind that is not predicted, or one that was given while the prediction was off)
        if (cfg_.learn_lead) {                                       // what the lead should have been for this order: the ticks from the confirmed tick that it was given at to its own
            lags_.emplace_back(static_cast<uint32_t>(std::min<uint64_t>(n >= pending_[i].born ? n - pending_[i].born : 0u, cfg_.max_lead_ticks)), confirmed_tick());
            while (lags_.size() > cfg_.lag_samples) lags_.pop_front();
        }
        if (pending_[i].tick != n) own_timing = true;                // sealed at another tick than the one at which the prediction applied it
        ++(n == pending_[i].first_tick ? stats_.sealed_exact : n < pending_[i].first_tick ? stats_.sealed_early : stats_.sealed_late);
        if (i > 0) {                                                 // the orders before it were given earlier and have not been sealed: the host did not take them
            stats_.commands_lost += i;
            wrong = true;
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    }
    if (own_timing) wrong = true;
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
    // The orders that this turn should have carried and did not are sealed later. The first time that happens to an order it is expected in the NEXT turn (an order is most often one tick
    // off, by the phase of the host's seals: the lead is the shortest lag of the last orders); an order that the next turn does not carry either stands at the display tick, as if it had
    // just been given (a correction that left it where it was would be repeated by every turn that comes: one rebuild a turn until it is sealed)
    for (Pending& p : pending_) {
        if (p.tick > n) continue;
        p.tick = p.misses == 0 ? n + 1u : display_;                  // (a rebuild puts it no later than the display tick and never behind a turn in hand)
        ++p.misses;
    }
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
    update_lead();
    if (stale_) return;                                              // (rebuilt when asked for: engine(), submit(): once, with the turns as they are by then)
    advance_to(c + lead_);
}

}  // namespace ants::net
