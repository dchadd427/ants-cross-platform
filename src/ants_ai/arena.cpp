#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>
#include <utility>

#include "ants_ai/map_info.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

namespace {

constexpr uint64_t kTurnTicks = 2;                       // a turn of a lock-step room is 100 ms
constexpr uint64_t kEndSlackTicks = 200;                 // the engine ends a match by its clock; this only bounds a loop that never would
constexpr uint64_t kFlowerScanPeriod = 4;                // a droplet falls for 16 ticks and a pick-up animation lasts longer than 4: a look every 4 ticks counts each once

/// Writes down what the engine is given, with the step it came after (null log: nothing is kept)
struct Recorder {
    std::vector<RecordedCommand>* log{nullptr};
    const uint64_t* steps{nullptr};
    const sim::SimulationEngine* sim{nullptr};
    CantGoTally* tally{nullptr};
    void applied(const sim::Command& c) const {
        if (log != nullptr) log->push_back(RecordedCommand{*steps, sim->current_tick(), c});
        if (tally != nullptr) tally->command(c, sim->current_tick());
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
    /// The first turn boundary at least `delay` ticks after `now` (what flush() will do with a command submitted now)
    uint64_t applied_at(uint64_t now) const override {
        const uint64_t due = now + delay_;
        return due + (kTurnTicks - due % kTurnTicks) % kTurnTicks;
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
    info.extra_kinds = spec.extra_kinds;
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

void CantGoTally::command(const sim::Command& c, uint64_t tick) {
    if (!sim::is_group_order(c.type) || c.ants.empty()) return;                    // a Stop, a hatch and the rest ask for no walk
    const uint8_t seat = c.issuer < sim::MAX_PLAYERS ? c.issuer : 0;
    ++seats_[seat].orders;
    refused_.push_back(0);
    order_seat_.push_back(seat);
    const uint32_t number = static_cast<uint32_t>(refused_.size());                  // 1, 2, ...
    for (const uint32_t id : c.ants) {
        if (id > (1u << 20)) continue;                                               // (the engine counts the ants from 1: a number like this names no ant)
        if (id >= last_.size()) last_.resize(static_cast<size_t>(id) + 1);
        last_[id] = Last{number, tick};
    }
}

void CantGoTally::scan(const sim::SimulationEngine& sim, const std::vector<sim::NewsEvent>& news) {
    bool entered = false;
    for (const sim::NewsEvent& n : news) {
        if (n.string_id != sim::strings::kCantGoThere && n.string_id != sim::strings::kCantDoThat) continue;
        entered = true;
        if (n.target_player < sim::MAX_PLAYERS) ++seats_[n.target_player].reactions;
    }
    if (!entered && flagged_ == 0) return;                      // an ant enters the state only with one of the two news items (enter_cant_go): the ants are looked at while one shows it
    ++scans_;
    const uint64_t tick = sim.current_tick();
    uint32_t flagged = 0;
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.id > (1u << 20)) continue;
        if (a.id >= in_cantgo_.size()) in_cantgo_.resize(static_cast<size_t>(a.id) + 1, 0);
        const bool now = a.state == sim::UnitState::CantGo;
        flagged += now ? 1u : 0u;
        if (now && in_cantgo_[a.id] == 0 && a.player_id < sim::MAX_PLAYERS) {
            ++seats_[a.player_id].began;
            if (a.id < last_.size() && last_[a.id].order != 0 && tick >= last_[a.id].tick && tick - last_[a.id].tick <= kRefusedWindow && refused_[last_[a.id].order - 1] == 0) {
                refused_[last_[a.id].order - 1] = 1;
                ++seats_[order_seat_[last_[a.id].order - 1]].refused;
            }
        }
        in_cantgo_[a.id] = now ? 1 : 0;
    }
    flagged_ = flagged;
}

void FlowerTally::scan(const sim::SimulationEngine& sim) {
    const sim::WorldState& ws = sim.get_world_state();
    if (dropping_.size() < ws.flower_droppers.size()) dropping_.resize(ws.flower_droppers.size(), 0);
    for (size_t i = 0; i < ws.flower_droppers.size(); ++i) {
        const sim::FlowerDropperSnapshot& d = ws.flower_droppers[i];
        if (dropping_[i] != 0 && !d.is_dropping) {                                 // the droplet's last frame: the power-up is on the tile
            ++landings_;
            // the draws 0 Bomber, 1 Combat, 2 Thief, 3 Swimmer, 4 Fire (FlowerDropperSnapshot::powerup_type) as the AntType numbers
            static constexpr std::array<size_t, 5> kKind = {1, 4, 3, 5, 2};
            ++landed_[kKind[std::min<size_t>(d.powerup_type, 4)]];
        }
        dropping_[i] = d.is_dropping ? 1 : 0;
    }
    for (const sim::AntSnapshot& a : ws.ants) {
        if (a.id > (1u << 20) || a.player_id >= sim::MAX_PLAYERS) continue;
        if (a.id >= raw_type_.size()) raw_type_.resize(static_cast<size_t>(a.id) + 1, 255);
        const uint8_t now = static_cast<uint8_t>(a.raw_type);
        if (raw_type_[a.id] != 255 && raw_type_[a.id] != now && now >= 1 && now <= 5) {
            Seat& seat = seats_[a.player_id];
            ++seat.took[now];
            for (const sim::FlowerDropperSnapshot& d : ws.flower_droppers) {
                if (std::max(std::abs(a.tile_x - d.drop_x), std::abs(a.tile_y - d.drop_y)) <= 1) {
                    ++seat.at_flowers;
                    break;
                }
            }
        }
        raw_type_[a.id] = now;
    }
}

ExpeditionResult read_expedition(const ExpeditionTask& e) noexcept {
    ExpeditionResult r;
    r.planned = e.planned();
    r.given_up = e.given_up();
    r.planted = e.planted();
    r.hops = e.hops();
    r.landings = e.landings();
    r.duds = e.duds();
    r.taken = e.taken();
    r.swimmers_taken = e.swimmers_taken();
    r.first_plant = e.first_plant();
    r.first_landing = e.first_landing();
    r.first_swimmer = e.first_swimmer();
    return r;
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
    CantGoTally tally;
    FlowerTally flowers;
    Recorder rec;
    rec.log = spec.record ? &out.log : nullptr;
    rec.steps = &steps;
    rec.sim = &sim;
    rec.tally = &tally;
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
        const std::vector<sim::NewsEvent> news = sim.poll_news_events();
        const size_t audio = sim.poll_audio_events().size();
        tally.scan(sim, news);
        if (sim.current_tick() % kFlowerScanPeriod == 0) flowers.scan(sim);
        out.news_events += news.size();
        out.audio_events += audio;
        out.peak_queue = std::max(out.peak_queue, static_cast<uint32_t>(std::max(news.size(), audio)));
        if (sim.current_tick() % kArenaHashPeriod == 0) out.checkpoints.push_back(sim.state_hash().total);
        if (sim.is_match_over()) {
            out.match_over = true;
            break;
        }
    }
    out.ticks = sim.current_tick();
    out.steps = steps;
    out.hash = sim.state_hash().total;
    flowers.scan(sim);                                   // (the last ticks, when the match ended between two looks)
    out.landings = flowers.landings();
    for (size_t k = 1; k < out.landed.size(); ++k) out.landed[k] = flowers.landed(static_cast<sim::AntType>(k));
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
        if (const StandardBot* sb = dynamic_cast<const StandardBot*>(bot)) {
            r.style = style_name(sb->style());
            r.stalls = sb->stalls();
            r.expedition = read_expedition(sb->expedition());
        }
        read_seat_result(sim, b.seat, r);
        r.banked = ledger.banked(b.seat);
        r.raided = ledger.raided(b.seat);
        r.cantgo = tally.seat(b.seat).reactions;
        r.cantgo_began = tally.seat(b.seat).began;
        r.orders = tally.seat(b.seat).orders;
        r.refused_orders = tally.seat(b.seat).refused;
        r.took = flowers.seat(b.seat).took;
        r.took_at_flowers = flowers.seat(b.seat).at_flowers;
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
