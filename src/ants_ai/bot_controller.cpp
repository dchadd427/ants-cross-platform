#include "ants_ai/bot_controller.hpp"

#include <algorithm>
#include <functional>
#include <utility>

#include "ants_ai/bot_view.hpp"

namespace ants::ai {

namespace {

constexpr int64_t kToken = 1000;                // one command, in thousandths of a command
constexpr uint32_t kTicksPerSecond = 20;        // the refill is per second, the controller runs per tick (50 ms)

bool is_rejection(sim::CommandResult::Status status) noexcept {
    using S = sim::CommandResult::Status;
    return status == S::RejectedIssuer || status == S::RejectedMalformed || status == S::RejectedNotAllowed;
}

}  // namespace

BotController::BotController(const sim::SimulationEngine& sim, uint64_t match_seed) : sim_(&sim), match_seed_(match_seed), map_(sim) {}

BotController::~BotController() = default;

BotController::Seat* BotController::find(uint8_t seat) noexcept {
    for (auto& s : seats_) {
        if (s->seat == seat) return s.get();
    }
    return nullptr;
}

const BotController::Seat* BotController::find(uint8_t seat) const noexcept {
    for (const auto& s : seats_) {
        if (s->seat == seat) return s.get();
    }
    return nullptr;
}

bool BotController::has_seat(uint8_t seat) const noexcept { return find(seat) != nullptr; }

uint8_t BotController::seat_mask() const noexcept {
    uint8_t mask = 0;
    for (const auto& s : seats_) mask = static_cast<uint8_t>(mask | (1u << s->seat));
    return mask;
}

const BotController::SeatStats& BotController::stats(uint8_t seat) const noexcept {
    static const SeatStats none{};
    const Seat* s = find(seat);
    return s != nullptr ? s->stats : none;
}

size_t BotController::pending(uint8_t seat) const noexcept {
    const Seat* s = find(seat);
    return s != nullptr ? s->queue.size() : 0u;
}

Bot* BotController::bot(uint8_t seat) noexcept {
    Seat* s = find(seat);
    return s != nullptr ? s->bot.get() : nullptr;
}

const Profile* BotController::profile(uint8_t seat) const noexcept {
    const Seat* s = find(seat);
    return s != nullptr ? &s->profile : nullptr;
}

bool BotController::add(const BotSpec& spec, sim::CommandSink& sink, std::string& error) {
    std::unique_ptr<Bot> bot = make_bot(spec);
    if (!bot) {
        error = "unknown bot kind '" + spec.kind + "'";
        return false;
    }
    return add(spec, std::move(bot), sink, error);
}

bool BotController::add(const BotSpec& spec, std::unique_ptr<Bot> bot, sim::CommandSink& sink, std::string& error) {
    error.clear();
    if (!bot) {
        error = "there is no bot to seat";
        return false;
    }
    if (spec.seat >= sim::MAX_PLAYERS || ((sim_->roster_mask() >> spec.seat) & 1u) == 0) {
        error = "seat " + std::to_string(static_cast<unsigned>(spec.seat)) + " is not in the match";
        return false;
    }
    if (sim_->is_fog_of_war_enabled()) {
        error = "bots cannot play with Fog of War: a bot would see through it";
        return false;
    }
    if (has_seat(spec.seat)) {
        error = "seat " + std::to_string(static_cast<unsigned>(spec.seat)) + " has a bot already";
        return false;
    }
    auto s = std::make_unique<Seat>();
    s->seat = spec.seat;
    s->spec = spec;
    s->profile = profile_for(spec.level);
    s->sink = &sink;
    s->rng = BotRng(seat_seed(match_seed_, spec.seat, bot->kind()));
    // The opening bucket. With the start hold (the default in every product path) ONE token: the first orders come one by one, where a full bucket was a burst of up to ten commands in one
    // tick. Without the hold (set_start_hold(0), the tests only) the opening of v0.1.0, a full bucket.
    s->tokens_milli = hold_end_ != 0 ? kToken : static_cast<int64_t>(s->profile.burst) * kToken;
    // The first look is on the hold's first tick or the next tick, whichever is later (set_start_hold): a seat that is seated after the hold has no hold. Either way the seats do not all think
    // on the same tick.
    s->next_decision = std::max<uint64_t>(sim_->current_tick() + 1u, hold_end_) + spec.seat;
    s->bot = std::move(bot);
    s->bot->start(BotContext{spec.seat, s->profile, s->rng.next(), &map_});
    const auto where = std::find_if(seats_.begin(), seats_.end(), [&](const std::unique_ptr<Seat>& o) { return o->seat > spec.seat; });
    seats_.insert(where, std::move(s));
    return true;
}

void BotController::on_tick(const sim::SimulationEngine& sim) {
    if (sim.is_match_over()) return;
    const uint64_t tick = sim.current_tick();
    for (auto& sp : seats_) {
        Seat& s = *sp;
        if (sim.is_player_dropped(s.seat)) {                       // the team is out of the match: its bot has nothing left to say
            s.queue.clear();
            continue;
        }
        if (tick >= s.next_decision) decide(sim, s, tick);
        refill(s, tick);
        release(sim, s, tick);
    }
}

// Whether a command may be proposed at all: what a person could click
bool BotController::allowed(const sim::SimulationEngine& sim, const Seat& s, const Intent& in) const {
    using sim::CommandType;
    const sim::Command& c = in.command;
    if (!sim::is_client_command(c.type) || c.type == CommandType::Quit) return false;       // None, Drop (the sequencer's own) and Quit are never a bot's
    if (sim::has_ant_list(c.type)) {
        if (c.ants.empty() || c.ants.size() > sim::kMaxCommandAnts) return false;
    } else if (!c.ants.empty()) {
        return false;
    }
    if (c.type == CommandType::GroupSpecial && c.ants.size() != 1) return false;           // the HUD sends a special order for one selected ant only
    if (in.pickup && (c.type != CommandType::GroupMove || c.ants.size() != 1)) return false;   // a planned pick-up is ONE ant's plain click (never a group, never an attack or a special order)
    if (sim::is_group_order(c.type)) {
        if (!sim.grid().in_bounds(c.tile_x, c.tile_y)) return false;
        const sim::TileCoord tile{c.tile_x, c.tile_y};
        const bool powerup = sim.grid().has_powerup_at(tile);
        if (in.pickup && !powerup) return false;                                            // there is nothing to take on that tile (any more)
        if (powerup && !in.pickup) return false;                                            // a click on a power-up takes it: only a planned pick-up may name one (and no special order, no attack)
        if (c.type == CommandType::GroupSpecial) {
            // A special order (extinguish, defuse, plant, ignite, bridge, a thief's raid) is a click that shows the target cursor, and the cursor shows it only where NO ant is under the pointer: a
            // click on an ant selects it or attacks it (src/ants_app/hud_input.cpp, evaluate_cursor, whose pick lists every ant that is not removed: one that dies on its clip is still there). So no
            // special order names a tile that an ant of any team stands on, the seat's own included (the engine itself refuses fire and bombs on a tile with a living ant and says nothing about
            // putting a fire out; the simulation is not changed for it).
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.tile_x == c.tile_x && a.tile_y == c.tile_y) return false;
            }
        }
        if (c.type == CommandType::GroupAttack) {
            // who stands on the tile: an ant of another team that is still an ant on the screen (an ally's: the attack needs the break of the alliance first)
            const uint8_t ally = sim.get_ally_id(s.seat);
            bool foreign = false;
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.tile_x != c.tile_x || a.tile_y != c.tile_y || a.player_id == s.seat) continue;
                if (a.hp == 0 || a.state == sim::UnitState::Dead || a.state == sim::UnitState::Drowning) continue;
                if (a.player_id == ally && ally < sim::MAX_PLAYERS) return false;
                foreign = true;
            }
            if (!foreign) return false;                                                     // an attack click is made on an enemy ant: with nobody there a person's click is a move
            for (const assets::AnthillSpawn& hill : sim.grid().anthills()) {                 // an ant of another colour on a hill tile gets the plain move cursor
                if (tile.x >= static_cast<int32_t>(hill.x) && tile.x <= static_cast<int32_t>(hill.x) + 3 && tile.y >= static_cast<int32_t>(hill.y) && tile.y <= static_cast<int32_t>(hill.y) + 3) return false;
            }
        }
        // (A plain move onto the tile of an enemy ant is an attack in the engine, and a person can click it: on a food cell the pointer's cursor is the food cursor, which comes
        // before the attack cursor. The worker's harvest clicks do that to an enemy harvester that stands on the pile's click tile, so this is NOT refused: the economy stays what it was.)
    }
    switch (c.type) {
        case CommandType::AllianceInvite:
        case CommandType::AllianceAccept:
        case CommandType::AllianceDeny:
        case CommandType::AllianceWithdraw: {
            const uint8_t other = c.other_player;
            if (other >= sim::MAX_PLAYERS || other == s.seat || ((sim.roster_mask() >> other) & 1u) == 0 || sim.is_player_dropped(other)) return false;
            const sim::WorldState& ws = sim.get_world_state();
            if (c.type == CommandType::AllianceInvite) return true;
            if (c.type == CommandType::AllianceWithdraw) return ws.pending_invite_from[other] == s.seat;     // only an offer that was made can be taken back
            return ws.pending_invite_from[s.seat] == other;                                                  // only an invitation that came can be answered
        }
        case CommandType::AllianceBreak:
            return sim.get_ally_id(s.seat) < sim::MAX_PLAYERS;                                               // the HUD offers a break to a team that has an ally
        default:
            return true;
    }
}

void BotController::decide(const sim::SimulationEngine& sim, Seat& s, uint64_t tick) {
    const BotView view = BotView::build(sim, s.seat, &map_);
    Orders orders;
    s.bot->think(view, orders);
    ++s.stats.decisions;
    s.next_decision = tick + std::max<uint32_t>(1u, s.profile.decision_interval);
    const uint32_t cap = std::clamp<uint32_t>(s.profile.max_ants_per_command, 1u, kHudAntCap);
    bool release_drawn = false;
    uint64_t release = 0;
    for (const Intent& in : orders.intents()) {
        if (!allowed(sim, s, in)) {
            ++s.stats.filtered;
            s.bot->on_command(in.command, Bot::Fate::Filtered, tick);                       // the bot is told, or a task would propose the same refused click at every look
            continue;
        }
        if (!release_drawn) {
            // ONE delay for the whole decision: the reaction time, +- jitter_percent, from the seat's own generator. The commands of one look leave in the order they
            // were proposed (a per-command draw could turn "stop, then move" around), and a later look never leaves before an earlier one.
            const uint32_t d = s.profile.reaction_delay;
            const uint32_t spread = d * s.profile.jitter_percent / 100u;
            release = std::max<uint64_t>(tick + (d - spread) + s.rng.below(2u * spread + 1u), s.last_release);
            release = std::max<uint64_t>(release, hold_end_);                              // the start hold: nothing leaves before the hold's first tick (set_start_hold: the product's hold is tick 1, since the simulation does not run behind the dialog), whatever tick the look was on
            s.last_release = release;
            release_drawn = true;
        }
        const size_t total = in.command.ants.size();
        const size_t step = sim::has_ant_list(in.command.type) ? cap : std::max<size_t>(total, 1u);
        for (size_t from = 0; from < std::max<size_t>(total, 1u); from += step) {
            Pending p;
            p.command = in.command;
            if (sim::has_ant_list(in.command.type)) {
                const size_t to = std::min(total, from + step);
                p.command.ants.assign(in.command.ants.begin() + static_cast<std::ptrdiff_t>(from), in.command.ants.begin() + static_cast<std::ptrdiff_t>(to));
            }
            p.priority = in.priority;
            p.release = release;
            p.expires = release + s.profile.intent_ttl;
            p.seq = s.next_seq++;
            s.queue.push_back(std::move(p));
            ++s.stats.intents;
        }
    }
}

// The token bucket: rate_milli_cps thousandths of a command per second = rate / 20 per tick (the remainder is carried, so the rate is exact). It does not fill before the hold's first tick
// (set_start_hold: the seat holds its one token): with the product's hold of 1 it fills from the first tick of the match.
void BotController::refill(Seat& s, uint64_t tick) const {
    if (hold_end_ != 0 && tick < hold_end_) return;
    const uint32_t sum = s.profile.rate_milli_cps + s.refill_carry;
    s.refill_carry = sum % kTicksPerSecond;
    s.tokens_milli = std::min<int64_t>(s.tokens_milli + sum / kTicksPerSecond, static_cast<int64_t>(s.profile.burst) * kToken);
}

void BotController::release(const sim::SimulationEngine& sim, Seat& s, uint64_t tick) {
    for (auto it = s.last_order.begin(); it != s.last_order.end();) {                 // the cool-down of an ant is over after reissue_cooldown ticks
        if (it->second + s.profile.reissue_cooldown <= tick) it = s.last_order.erase(it);
        else ++it;
    }
    if (s.queue.empty()) return;

    // 1. what could not be paid in time is dropped: the world has moved on, the bot finds out in its next look
    for (size_t i = 0; i < s.queue.size();) {
        if (tick <= s.queue[i].expires) {
            ++i;
            continue;
        }
        sim::Command gone = std::move(s.queue[i].command);
        s.queue.erase(s.queue.begin() + static_cast<std::ptrdiff_t>(i));
        ++s.stats.expired;
        s.bot->on_command(gone, Bot::Fate::Expired, tick);
    }

    // 2. ants that died (or never were the seat's) leave the commands that are due; a command that is left with none is never sent
    std::vector<uint32_t> alive;
    bool alive_known = false;
    for (size_t i = 0; i < s.queue.size();) {
        Pending& p = s.queue[i];
        if (p.release > tick || !sim::has_ant_list(p.command.type)) {
            ++i;
            continue;
        }
        if (!alive_known) {
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.player_id == s.seat && a.hp > 0 && a.state != sim::UnitState::Dead && a.state != sim::UnitState::Drowning) alive.push_back(a.id);
            }
            std::sort(alive.begin(), alive.end());
            alive_known = true;
        }
        std::vector<uint32_t> keep;
        for (const uint32_t id : p.command.ants) {
            if (std::binary_search(alive.begin(), alive.end(), id)) keep.push_back(id);
        }
        if (keep.size() != p.command.ants.size() && !p.trimmed) {
            p.trimmed = true;
            ++s.stats.pruned;
        }
        if (keep.empty()) {
            sim::Command gone = std::move(p.command);
            s.queue.erase(s.queue.begin() + static_cast<std::ptrdiff_t>(i));
            s.bot->on_command(gone, Bot::Fate::Pruned, tick);
            continue;
        }
        p.command.ants = std::move(keep);
        ++i;
    }

    // 2b. the newest order wins among the orders that are due (the later click of a person wins): an ant that a newer due order names leaves every older due order, and an
    // older order that is left with no ant never leaves (Superseded). An order that is not due yet takes nothing from the ones before it: those leave first, in the order
    // they were proposed, and the anti-thrash cool-down spaces the next one out (a bot that re-issues an order at every look is not starved by its own newer looks).
    {
        std::vector<size_t> due;
        for (size_t i = 0; i < s.queue.size(); ++i) {
            if (s.queue[i].release <= tick && sim::has_ant_list(s.queue[i].command.type)) due.push_back(i);
        }
        if (due.size() > 1) {
            std::sort(due.begin(), due.end(), [&](size_t a, size_t b) { return s.queue[a].seq > s.queue[b].seq; });      // newest first
            std::vector<uint32_t> claimed;                                                                              // sorted
            std::vector<size_t> emptied;
            for (const size_t i : due) {
                std::vector<uint32_t>& ants = s.queue[i].command.ants;
                ants.erase(std::remove_if(ants.begin(), ants.end(), [&](uint32_t id) { return std::binary_search(claimed.begin(), claimed.end(), id); }), ants.end());
                if (ants.empty()) {
                    emptied.push_back(i);
                    continue;
                }
                for (const uint32_t id : ants) claimed.insert(std::upper_bound(claimed.begin(), claimed.end(), id), id);
            }
            std::sort(emptied.begin(), emptied.end(), std::greater<size_t>());                                           // erase from the back
            std::vector<sim::Command> gone;
            for (const size_t i : emptied) {
                gone.push_back(std::move(s.queue[i].command));
                s.queue.erase(s.queue.begin() + static_cast<std::ptrdiff_t>(i));
                ++s.stats.superseded;
            }
            for (sim::Command& c : gone) s.bot->on_command(c, Bot::Fate::Superseded, tick);
        }
    }

    // 3. release what is due and paid for: the highest priority first, first come first served inside a class; an ant that was ordered a moment ago is left
    // alone unless the order is urgent
    while (s.tokens_milli >= kToken) {
        size_t best = s.queue.size();
        for (size_t i = 0; i < s.queue.size(); ++i) {
            const Pending& p = s.queue[i];
            if (p.release > tick) continue;
            if (best != s.queue.size()) {
                const Pending& b = s.queue[best];
                if (!(p.priority > b.priority || (p.priority == b.priority && p.seq < b.seq))) continue;
            }
            if (p.priority != Priority::Urgent) {
                bool held = false;
                for (const uint32_t id : p.command.ants) held = held || s.last_order.count(id) != 0;
                if (held) continue;
            }
            best = i;
        }
        if (best == s.queue.size()) break;
        sim::Command c = std::move(s.queue[best].command);
        s.queue.erase(s.queue.begin() + static_cast<std::ptrdiff_t>(best));
        c.issuer = s.seat;                                                              // whatever the bot wrote: the seat speaks
        s.tokens_milli -= kToken;
        for (const uint32_t id : c.ants) s.last_order[id] = tick;
        const sim::CommandResult result = s.sink->submit(c);
        ++s.stats.released;
        if (is_rejection(result.status)) ++s.stats.rejected;
        s.bot->on_command(c, Bot::Fate::Sent, tick);
    }
}

}  // namespace ants::ai
