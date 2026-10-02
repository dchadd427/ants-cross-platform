#include "ants_ai/arena.hpp"

#include <algorithm>
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
    void add(const sim::Command& c) const {
        if (log != nullptr) log->push_back(RecordedCommand{*steps, sim->current_tick(), c});
    }
};

/// The sink of a local game: the command goes into the engine at once
class DirectSink final : public sim::CommandSink {
public:
    DirectSink(sim::SimulationEngine& sim, Recorder rec) : sim_(sim), rec_(rec) {}
    sim::CommandResult submit(const sim::Command& c) override {
        const sim::CommandResult r = sim_.apply_command(c);
        rec_.add(c);
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
            rec_.add(c);
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

uint8_t roster_of(const std::vector<BotSpec>& bots) {
    uint8_t roster = 0;
    for (const BotSpec& b : bots) roster = static_cast<uint8_t>(roster | (1u << b.seat));
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
    return check_setup(info);
}

}  // namespace

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
    Recorder rec;
    rec.log = spec.record ? &out.log : nullptr;
    rec.steps = &steps;
    rec.sim = &sim;
    DirectSink direct(sim, rec);
    DelayedSink delayed(sim, spec.latency_ticks, rec);
    sim::CommandSink& sink = spec.latency_ticks == 0 ? static_cast<sim::CommandSink&>(direct) : static_cast<sim::CommandSink&>(delayed);

    BotController controller(sim, spec.seed);
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
        if (spec.latency_ticks != 0) delayed.flush();
        controller.on_tick(sim);
        sim.clear_news_events();                         // nobody polls them here, and they would grow for the whole match
        sim.clear_audio_events();
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
    const sim::WorldState& ws = sim.get_world_state();
    for (const BotSpec& b : spec.bots) {
        ArenaSeatResult r;
        r.spec = b;
        const Bot* bot = controller.bot(b.seat);
        r.runs = bot != nullptr ? bot->kind() : "";
        r.score = sim.get_player_score(b.seat);
        r.shown_score = sim.get_display_score(b.seat);
        r.eggs = sim.get_player_eggs(b.seat);
        r.hatched = sim.get_player_hatched(b.seat);
        const sim::PlayerMatchStats st = sim.get_player_stats(b.seat);
        r.food_deposited = st.food_deposited;
        r.food_stolen = st.food_stolen;
        r.food_lost = st.food_lost;
        r.kills = st.enemy_killed;
        r.losses = st.friendly_lost;
        for (const sim::AntSnapshot& a : ws.ants) {
            if (a.player_id == b.seat && a.hp > 0 && a.state != sim::UnitState::Dead && a.state != sim::UnitState::Drowning) ++r.ants;
        }
        r.stats = controller.stats(b.seat);
        out.seats.push_back(r);
    }
    std::sort(out.seats.begin(), out.seats.end(), [](const ArenaSeatResult& a, const ArenaSeatResult& b) { return a.spec.seat < b.spec.seat; });
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
