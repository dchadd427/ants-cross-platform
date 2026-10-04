#include "ants_net/prediction.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace ants::net {

namespace {

uint64_t ns_since(std::chrono::steady_clock::time_point t0) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}

}  // namespace

Prediction::Prediction(const sim::SimulationEngine& confirmed, const LockstepRunner& runner, Config config)
    : confirmed_(&confirmed), runner_(&runner), cfg_(config) {
    cfg_.max_lead_ticks = std::max<uint32_t>(cfg_.max_lead_ticks, 1u);
    cfg_.min_lead_ticks = std::min(cfg_.min_lead_ticks, cfg_.max_lead_ticks);
    lead_ = target_lead_ = std::clamp<uint32_t>(3u, cfg_.min_lead_ticks, cfg_.max_lead_ticks);
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
    target_lead_ = std::clamp<uint32_t>(ticks, cfg_.min_lead_ticks, cfg_.max_lead_ticks);
}

// The lead rises at once and falls slowly: a lead that followed the phase of the server's seals (the delay of an order varies by one tick with the moment it was given) would move the whole
// picture back and forth by a tick
void Prediction::update_lead() {
    if (target_lead_ > lead_) {
        lead_ = target_lead_;
        lead_low_ticks_ = 0;
    } else if (target_lead_ < lead_) {
        if (++lead_low_ticks_ >= cfg_.lead_fall_after_ticks) {
            --lead_;
            lead_low_ticks_ = 0;
        }
    } else {
        lead_low_ticks_ = 0;
    }
}

uint64_t Prediction::ticks_ahead() const noexcept {
    if (!running_) return 0;
    const uint64_t c = confirmed_tick();
    return display_ > c ? display_ - c : 0u;
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

void Prediction::begin(uint64_t c) {
    running_ = true;
    stale_ = false;
    pending_.clear();
    ++stats_.starts;
    lead_ = target_lead_;
    lead_low_ticks_ = 0;
    rebuild(c + lead_, true);
}

void Prediction::stop() {
    running_ = false;
    stale_ = false;
    assumed_.clear();
    pending_.clear();
    audio_.clear();
    news_.clear();
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
    const auto t0 = Clock::now();
    while (display_ < target) advance_one();
    const uint64_t ns = ns_since(t0);
    stats_.advance_ns_total += ns;
    stats_.advance_ns_max = std::max(stats_.advance_ns_max, ns);
}

// A copy of the confirmed engine, run to the display tick with the turns in hand and the waiting orders: the prediction as it should be now.
void Prediction::rebuild(uint64_t target, bool is_start) {
    const auto t0 = Clock::now();
    std::vector<sim::AntSnapshot> before;
    const bool measure = measure_corrections_ && !is_start && running_;
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
        capture_events(t, true);
    }
    apply_commands_for(target);
    if (measure && !have_after && target == shown_before) {
        after = pred_.get_world_state().ants;
        have_after = true;
    }
    display_ = target;
    stale_ = false;

    const uint64_t ns = ns_since(t0);
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
        ++stats_.corrections;
        uint32_t moved = 0;
        for (const sim::AntSnapshot& a : after) {
            for (const sim::AntSnapshot& b : before) {
                if (b.id != a.id) continue;
                const uint32_t dx = static_cast<uint32_t>(std::abs(a.px - b.px));
                const uint32_t dy = static_cast<uint32_t>(std::abs(a.py - b.py));
                if (dx != 0 || dy != 0) {
                    ++moved;
                    stats_.max_move_px = std::max(stats_.max_move_px, std::max(dx, dy));
                }
                break;
            }
        }
        if (moved > 0) ++stats_.corrections_visible;
        stats_.ants_moved += moved;
    }
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
    // The order is given to the state that the screen shows. That state is never behind a turn that is in hand (such a turn was sealed before this order existed), so the prediction first runs
    // up to the turns that it knows (it does so anyway when the buffer is deeper than the lead: the buffer is longer than the delay that was measured, a moment after a burst of turns)
    const uint64_t next_receive = runner_->next_turn_expected();
    if (display_ < next_receive) advance_to(next_receive);
    result = pred_.apply_command(command);
    assumed_.back().push_back(command);
    pending_.push_back(Pending{command, display_, confirmed_tick()});
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
        if (pending_[i].tick != n) own_timing = true;                // sealed at another tick than the one at which the prediction applied it
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
    // The orders that this turn should have carried and did not are sealed later: they stand at the display tick, as if they had just been given (a correction that left them where they were
    // would be repeated by every turn that comes: one rebuild a turn until they are sealed)
    for (Pending& p : pending_) {
        if (p.tick <= n) p.tick = display_;
    }
}

void Prediction::on_tick() {
    if (suspended_) return;
    const uint64_t c = confirmed_tick();
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
