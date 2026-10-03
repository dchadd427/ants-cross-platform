#include "ants_ai/arena.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

#include "ants_ai/map_info.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

namespace {

constexpr uint64_t kTurnTicks = 2;                       // a turn of a lock-step room is 100 ms
constexpr uint64_t kEndSlackTicks = 200;                 // the engine ends a match by its clock; this only bounds a loop that never would

/// Writes down what the engine is given, with the step it came after (null log: nothing is kept)
struct Recorder {
    std::vector<RecordedCommand>* log{nullptr};
    const uint64_t* steps{nullptr};
    const sim::SimulationEngine* sim{nullptr};
    void applied(const sim::Command& c) const {
        if (log != nullptr) log->push_back(RecordedCommand{*steps, sim->current_tick(), c});
    }
};

/// The sink of a local game: the command goes into the engine at once
class DirectSink final : public sim::CommandSink {
public:
    DirectSink(sim::SimulationEngine& sim, Recorder rec) : sim_(sim), rec_(rec) {}
    sim::CommandResult submit(const sim::Command& c) override {
        const sim::CommandResult r = sim_.apply_command(c);
        rec_.applied(c);
        return r;
    }

private:
    sim::SimulationEngine& sim_;
    Recorder rec_;
};

/// The sink of a room as the engine sees it: a command released now reaches the engine at the first turn boundary at least `delay` ticks later, in canonical order. The answer is the
/// optimistic Applied of NetGame::submit (a bot never reads it).
class DelayedSink final : public sim::CommandSink {
public:
    DelayedSink(sim::SimulationEngine& sim, uint32_t delay, Recorder rec) : sim_(sim), delay_(delay), rec_(rec) {}
    sim::CommandResult submit(const sim::Command& c) override {
        pending_.push_back(Waiting{sim_.current_tick() + delay_, c});
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        return r;
    }
    /// After every tick, BEFORE the bots look: applies what is due at a turn boundary
    void flush() {
        const uint64_t now = sim_.current_tick();
        if (now % kTurnTicks != 0 || pending_.empty()) return;
        std::vector<sim::Command> due;
        std::vector<Waiting> later;
        for (Waiting& w : pending_) {
            if (w.due <= now) due.push_back(std::move(w.command));
            else later.push_back(std::move(w));
        }
        pending_ = std::move(later);
        sim::canonical_order(due);
        for (const sim::Command& c : due) {
            sim_.apply_command(c);
            rec_.applied(c);
        }
    }

private:
    struct Waiting {
        uint64_t due;
        sim::Command command;
    };
    sim::SimulationEngine& sim_;
    uint32_t delay_;
    Recorder rec_;
    std::vector<Waiting> pending_;
};

/// The seats of the specs as a roster mask. A seat that does not exist (check_setup refuses it, with a reason) adds nothing: shifting by it would be undefined.
uint8_t roster_of(const std::vector<BotSpec>& bots) {
    uint8_t roster = 0;
    for (const BotSpec& b : bots) {
        if (b.seat < sim::MAX_PLAYERS) roster = static_cast<uint8_t>(roster | (1u << b.seat));
    }
    return roster;
}

std::string refusal(const ArenaSpec& spec) {
    if (spec.level == nullptr) return "there is no map";
    if (spec.bots.empty()) return "no seat has a bot";
    SetupInfo info;
    info.roster = roster_of(spec.bots);
    info.human_mask = 0;
    info.bots = spec.bots;
    info.fog = false;
    info.allow_all_bots = true;                          // the arena is the one place where nobody is a person
    const std::string why = check_setup(info);
    if (!why.empty()) return why;
    // Is the map playable by the teams that sit? (a start marker of a team that plays must lie inside the grid: the engine places no ant for one outside and the team would start
    // short of an ant.) The server and the application ask the same question before they start a match.
    const assets::LevelValidation verdict = spec.level->validate(info.roster);
    if (!verdict.playable) return "the map cannot be played by these seats: " + verdict.reason();
    return "";
}

}  // namespace

void ScoreLedger::start(const sim::SimulationEngine& sim) {
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        last_[t] = sim.get_player_score(t);
        hatched_[t] = sim.get_player_hatched(t);
    }
}

void ScoreLedger::sample(const sim::SimulationEngine& sim) {
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const int32_t now = sim.get_player_score(t);
        const uint32_t hatched = sim.get_player_hatched(t);
        int32_t before = last_[t];
        for (uint32_t egg = hatched_[t]; egg != hatched; ++egg) {          // an egg was started for the seat since the last sample: the engine charged min(score, 200) for it
            before -= std::min<int32_t>(static_cast<int32_t>(sim::HATCH_COST_POINTS), std::max<int32_t>(0, before));
        }
        hatched_[t] = hatched;
        if (now > before) banked_[t] += static_cast<uint32_t>(now - before);
        else if (now < before) raided_[t] += static_cast<uint32_t>(before - now);
        last_[t] = now;
    }
}

void read_seat_result(const sim::SimulationEngine& sim, uint8_t seat, ArenaSeatResult& out) {
    out.score = sim.get_player_score(seat);
    out.shown_score = sim.get_display_score(seat);
    out.eggs = sim.get_player_eggs(seat);
    out.hatched = sim.get_player_hatched(seat);
    const sim::PlayerMatchStats st = sim.get_player_stats(seat);
    out.kills = st.enemy_killed;
    out.losses = st.friendly_lost;
    out.ants = 0;
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.player_id == seat && a.hp > 0 && a.state != sim::UnitState::Dead && a.state != sim::UnitState::Drowning) ++out.ants;
    }
}

ArenaResult play_match(const ArenaSpec& spec) {
    ArenaResult out;
    out.error = refusal(spec);
    if (!out.error.empty()) return out;

    const uint8_t roster = roster_of(spec.bots);
    sim::SimulationEngine sim;
    sim.init(*spec.level, spec.seed, roster);
    for (const BotSpec& b : spec.bots) {
        if (sim.grid().find_anthill(b.seat) == nullptr) {
            out.error = "the map has no hill for seat " + std::to_string(static_cast<unsigned>(b.seat));
            return out;
        }
    }
    out.initial_ticks = sim.get_match_time_remaining_ms() / sim::TICK_MS;

    uint64_t steps = 0;
    ScoreLedger ledger;
    ledger.start(sim);
    Recorder rec;
    rec.log = spec.record ? &out.log : nullptr;
    rec.steps = &steps;
    rec.sim = &sim;
    DirectSink direct(sim, rec);
    DelayedSink delayed(sim, spec.latency_ticks, rec);
    sim::CommandSink& sink = spec.latency_ticks == 0 ? static_cast<sim::CommandSink&>(direct) : static_cast<sim::CommandSink&>(delayed);

    BotController controller(sim, spec.seed);
    controller.set_start_hold(spec.start_hold);                // (before the seats: it shapes their first look and their bucket)
    for (const BotSpec& b : spec.bots) {
        std::string why;
        const bool ok = spec.factory ? controller.add(b, spec.factory(b), sink, why) : controller.add(b, sink, why);
        if (!ok) {
            out.error = "seat " + std::to_string(static_cast<unsigned>(b.seat)) + ": " + why;
            return out;
        }
    }

    const uint64_t limit = spec.max_ticks != 0 ? spec.max_ticks : out.initial_ticks + kEndSlackTicks;
    while (sim.current_tick() < limit) {
        sim.tick();
        ++steps;                                         // the call that ends the match does not advance current_tick(): count the calls
        ledger.sample(sim);
        if (spec.latency_ticks != 0) delayed.flush();
        controller.on_tick(sim);
        // nobody polls the news and audio queues here, and they would grow for the whole match: empty them every tick (and count what they held)
        const size_t news = sim.poll_news_events().size();
        const size_t audio = sim.poll_audio_events().size();
        out.news_events += news;
        out.audio_events += audio;
        out.peak_queue = std::max(out.peak_queue, static_cast<uint32_t>(std::max(news, audio)));
        if (sim.current_tick() % kArenaHashPeriod == 0) out.checkpoints.push_back(sim.state_hash().total);
        if (sim.is_match_over()) {
            out.match_over = true;
            break;
        }
    }
    out.ticks = sim.current_tick();
    out.steps = steps;
    out.hash = sim.state_hash().total;
    {
        std::set<uint32_t> counted;
        const std::vector<sim::FoodObject>& objects = sim.grid().food_objects();
        for (const PileInfo& p : controller.map().piles()) {
            bool reach = false;
            for (const BotSpec& b : spec.bots) reach = reach || p.approach[b.seat].reachable();
            if (reach && p.bite_index < objects.size() && counted.insert(p.bite_index).second) out.reachable_units_left += objects[p.bite_index].remaining;
        }
    }
    for (const BotSpec& b : spec.bots) {
        ArenaSeatResult r;
        r.spec = b;
        const Bot* bot = controller.bot(b.seat);
        r.runs = bot != nullptr ? bot->kind() : "";
        read_seat_result(sim, b.seat, r);
        r.banked = ledger.banked(b.seat);
        r.raided = ledger.raided(b.seat);
        r.stats = controller.stats(b.seat);
        out.seats.push_back(r);
    }
    std::sort(out.seats.begin(), out.seats.end(), [](const ArenaSeatResult& a, const ArenaSeatResult& b) { return a.spec.seat < b.spec.seat; });
    if (spec.inspect) spec.inspect(sim);
    return out;
}

ReplayResult replay_commands(const ArenaSpec& spec, const ArenaResult& played) {
    ReplayResult out;
    if (!played.error.empty() || spec.level == nullptr) {
        out.error = "there is no match to replay";
        return out;
    }
    sim::SimulationEngine sim;
    sim.init(*spec.level, spec.seed, roster_of(spec.bots));                 // no controller, no bot: the commands alone
    size_t next = 0;
    size_t checkpoint = 0;
    for (uint64_t step = 1; step <= played.steps; ++step) {
        sim.tick();
        while (next < played.log.size() && played.log[next].step == step) {
            sim.apply_command(played.log[next].command);
            ++next;
        }
        sim.clear_news_events();
        sim.clear_audio_events();
        if (sim.current_tick() % kArenaHashPeriod == 0) {
            if (checkpoint >= played.checkpoints.size() || played.checkpoints[checkpoint] != sim.state_hash().total) {
                out.first_bad_tick = sim.current_tick();
                out.hash = sim.state_hash().total;
                return out;
            }
            ++checkpoint;
        }
    }
    out.hash = sim.state_hash().total;
    if (next != played.log.size()) {
        out.first_bad_tick = played.ticks;                                    // recorded commands that no step of the replay applied: the log is not of this match
        out.error = "the log holds commands after the last step";
        return out;
    }
    if (out.hash != played.hash || checkpoint != played.checkpoints.size()) {
        out.first_bad_tick = played.ticks;
        return out;
    }
    out.ok = true;
    return out;
}

}  // namespace ants::ai
