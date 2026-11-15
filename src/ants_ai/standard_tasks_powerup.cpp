#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;
using standard_detail::tile_index;

// ---- PowerUpTask ----------------------------------------------------------------------------------------------------------------------------------------

namespace {

// A worker that may be sent for a power-up: empty hands, healthy, takes orders, is not on a power-up tile
bool usable_for_powerup(const BotView& view, const AntView& a, bool fire_too = false) {
    if ((a.type != view.default_ant_type() && !(fire_too && a.type == sim::AntType::Fire)) || a.holding || a.carried_points > 0 || a.hp < 5) return false;
    if (!a.takes_orders() || a.state == sim::UnitState::Stunned) return false;
    return view.powerup_at(a.tile) == nullptr;
}

}  // namespace

bool PowerUpTask::blacklisted(uint32_t ant, sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = black_.find({ant, key_of(tile)});
    return it != black_.end() && it->second > tick;
}

bool PowerUpTask::tile_blacklisted(sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = tile_black_.find(key_of(tile));
    return it != tile_black_.end() && it->second > tick;
}

// The drop tile of a flower strip that the flower link joins to the ring half of the crew (Tactics::flower_links)
bool PowerUpTask::is_link_flower(sim::TileCoord drop) const noexcept {
    if (!tactics_.plan.island_flower_link) return false;
    for (const Tactics::FlowerLink& l : tactics_.flower_links) {
        if (l.drop == drop) return true;
    }
    return false;
}

// The Swimmers that the bot wants beyond the level's own number while a flower link is set: the drops of the strip are taken for them
size_t PowerUpTask::extra_want(sim::AntType kind) const noexcept {
    if (kind == sim::AntType::Thief && tactics_.plan.island_flower_link && tactics_.plan.island_flower_thief && !tactics_.flower_links.empty()) return 1;       // (one Thief of the strip: the plan's own want of the Thief, where it has one, counts first)
    if (kind != sim::AntType::Swimmer || !tactics_.plan.island_flower_link || tactics_.flower_links.empty() || tactics_.wants[static_cast<size_t>(kind)] == 0) return 0;
    return tactics_.plan.island_flower_swimmers;
}

std::vector<std::pair<uint32_t, sim::AntType>> PowerUpTask::on_their_way() const {
    std::vector<std::pair<uint32_t, sim::AntType>> out;
    for (const auto& t : takes_) out.emplace_back(t.first, t.second.kind);
    return out;
}

void PowerUpTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = takes_.find(command.ants[0]);
    if (it == takes_.end() || it->second.sent != kPending || command.tile_x != it->second.tile.x || command.tile_y != it->second.tile.y) return;
    if (fate == Bot::Fate::Sent) it->second.sent = tick;              // the clock of the walk starts when the order LEFT
    else takes_.erase(it);                                            // it never left: the ant was not told anything (its claim is given back at the next look)
}

const std::vector<int32_t>& PowerUpTask::field_for(TaskContext& c, sim::TileCoord tile) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (mask_tick_ != now) {
        mask_ = MapInfo::walkable_mask(v.grid(), c.seat, v.walk_context());
        for (size_t i = 0; i < mask_.size() && i < tactics_.shut_tiles.size(); ++i) {
            if (tactics_.shut_tiles[i] != 0) mask_[i] = 0;                                         // (the bridges that will not last the trip: the island task)
        }
        if (tactics_.gate_map) {
            for (const BombView& b : v.bombs()) {
                if (b.owner != c.seat && !(v.ally() < sim::MAX_PLAYERS && b.owner == v.ally()) && v.grid().in_bounds(b.tile)) {
                    mask_[tile_index(v.grid(), b.tile)] = 0;                                        // (map bombs are not walked into on a pick-up trip while gate goals run)
                }
            }
        }
        mask_tick_ = now;
    }
    Cached& e = fields_[key_of(tile)];
    if (e.field.empty() || now >= e.tick + params_.field_ttl_ticks) {
        e.field = MapInfo::cost_field_onto(v.grid(), mask_, c.seat, tile, v.walk_context());
        e.tick = now;
        ++searches_;
    }
    return e.field;
}

bool PowerUpTask::try_start(TaskContext& c, sim::AntType kind) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    struct Cand {
        const PowerUpView* p;
        int side;                                                                                   // whose side of the map it is (power_up_side: asked once per power-up, the sort compares often)
    };
    std::vector<Cand> cands;
    uint8_t present = 0;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (v.rows()[t].present ? 1u << t : 0u));
    for (const PowerUpView& p : v.powerups()) {
        if (p.kind != kind || p.standing_ant != 0 || tile_blacklisted(p.tile, now)) continue;
        if (kind == sim::AntType::Thief && !tactics_.plan.island_flower_thief && is_link_flower(p.tile)) continue;       // (a Thief of the strip: nothing uses it there yet)
        const int side = power_up_side(c.map, p.tile, present);
        if (side >= 0 && side != c.seat && (!(tactics_.plan.steals || tactics_.gate_map) || (v.ally() < sim::MAX_PLAYERS && side == v.ally()))) continue;       // another side's power-up is a theft: Hard's alone (and a gate map's: the start ants of a walled map stand beside the power-ups of the hill that cannot reach them), and never the ally's
        bool taken = false;
        for (const auto& t : takes_) taken = taken || t.second.tile == p.tile;
        if (!taken) cands.push_back(Cand{&p, side});
    }
    if (cands.empty()) return false;
    std::vector<const AntView*> ants;
    for (const AntView& a : v.mine()) {
        if (!usable_for_powerup(v, a, a.id == watch_.ant && watch_.fire_ok) || takes_.count(a.id) != 0) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;     // the ants of a fight, the walls and the raids are not taken
        ants.push_back(&a);
    }
    if (ants.empty()) return false;
    const auto nearest = [&](const PowerUpView* p) {
        int32_t best = 1 << 20;
        for (const AntView* a : ants) best = std::min(best, a->tile.chebyshev_dist(p->tile));
        return best;
    };
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& x, const Cand& y) {
        const bool ox = x.side == c.seat || x.side < 0;                                             // the own side's first, then by the distance
        const bool oy = y.side == c.seat || y.side < 0;
        if (ox != oy) return ox;
        return nearest(x.p) < nearest(y.p);
    });
    const sim::Grid& grid = v.grid();
    const size_t limit = std::min(cands.size(), tactics_.gate_map ? gate_candidates(static_cast<uint64_t>(grid.width()) * grid.height()) : size_t{3});     // (a gate map's bot looks further, as far as the searches stay within a budget)
    for (size_t i = 0; i < limit; ++i) {
        const PowerUpView& p = *cands[i].p;
        const std::vector<int32_t>& f = field_for(c, p.tile);
        if (f.empty()) continue;
        const AntView* best = nullptr;
        int32_t best_cost = 0;
        for (const AntView* a : ants) {                                                 // the closest ant by the walking cost from its own tile (a lower id wins a tie)
            if (blacklisted(a->id, p.tile, now) || !grid.in_bounds(a->tile)) continue;
            const int32_t cost = f[tile_index(grid, a->tile)];
            if (cost < 0 || (best != nullptr && cost >= best_cost)) continue;
            best = a;
            best_cost = cost;
        }
        if (best == nullptr) continue;
        const uint32_t walk = static_cast<uint32_t>(MapInfo::walking_ticks(best_cost)) + 5u;           // the pick-up comes 8 d + 5 ticks after the order on grass (measured in the engine)
        if (walk > (is_link_flower(p.tile) ? params_.link_trip_ticks : params_.max_trip_ticks)) continue;
        if (static_cast<uint64_t>(v.ticks_left()) < walk + 17u + 2u * c.profile.reaction_delay + 100u) continue;
        // a contest: an enemy ant whose walk to the power-up is no longer than ours (the engine's walking speed, a tenth quicker for the diagonal runs of the estimate).
        // On a gate map Medium does not give a trip up for an enemy that is as near, when the power-up lies on the hill's side or in the component of the ant (skip_contest): its longer reaction delay
        // loses the estimate to the start ants of the enemy at the central power-ups (without the skip it gave up many more trips on Ghetto and lost about a quarter of its score there). Hard (the level that
        // steals) judges the race as everywhere: the skip cost it !-Midz-! and ANTS_PARTY.
        const bool skip_contest = tactics_.gate_map && !tactics_.plan.steals && takes_.empty() &&
                                  (c.map.ant_component(c.seat, p.tile) == c.map.hill_component(c.seat) ||
                                   c.map.ant_component(c.seat, p.tile) == c.map.ant_component(c.seat, best->tile));
        bool lost = false;
        const bool link_drop = is_link_flower(p.tile);                                  // (a strip that two hills of a ring half share: the watchers stand side by side, and each would give the drop up to the other; the first click wins)
        for (const AntView& e : v.others()) {
            if (skip_contest || link_drop || e.holding || !grid.in_bounds(e.tile) || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) continue;
            if (e.state == sim::UnitState::EnteringBase || e.state == sim::UnitState::PoweringUp) continue;
            const int32_t cost = f[tile_index(grid, e.tile)];
            if (cost < 0) continue;
            const uint32_t theirs = static_cast<uint32_t>(MapInfo::walking_ticks(cost)) * 9u / 10u + 5u;
            if (theirs <= walk + c.profile.reaction_delay) {
                lost = true;
                break;
            }
        }
        if (lost) {
            tile_black_[key_of(p.tile)] = now + 200;
            ++contested_;
            continue;
        }
        if (!c.ledger.take(best->id, id())) continue;
        c.orders.pick_up(best->id, p.tile, Priority::Normal);
        Take t;
        t.tile = p.tile;
        t.kind = kind;
        t.before = best->type;
        t.decided = now;
        t.walk_ticks = walk;
        takes_[best->id] = t;
        ++started_;
        return true;
    }
    return false;
}

// The watcher (Hard): an ant waits beside the drop tile of the own side's flower, arrives about plan.flower_watch_early ticks before the landing that Memory::flower expects, takes a kind that
// the plan lacks like any trip does, and goes home when the plan lacks nothing the flower is likely to drop or the wait is longer than a trip there and back (docs/BOTS.md, "The flowers").
void PowerUpTask::watch(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const auto give_up = [&]() {
        if (watch_.ant == 0 || takes_.count(watch_.ant) != 0) return;                   // (a trip that is under way ends by itself, and a typed ant then steps off)
        c.ledger.release(watch_.ant, id());
        watch_ = Watch{};
    };
    if (watch_.ant != 0) {                                                              // is the watcher still the watcher
        const AntView* w = find_ant(v.mine(), watch_.ant);
        const TaskId owner = w != nullptr ? c.ledger.owner(watch_.ant) : kNoTask;
        const bool hit = w != nullptr && w->hp < watch_.hp;
        if (hit) watch_off_until_ = now + kWatchHoldAfterHit;                           // (a hit ends the wait, and nobody waits there for a while: an enemy is near, and a fight never takes an ant of this rank)
        if (w == nullptr || w->type != v.default_ant_type() || (owner != id() && owner != kNoTask) || w->holding || w->carried_points > 0) {
            if (w != nullptr && owner == id() && takes_.count(watch_.ant) == 0) c.ledger.release(watch_.ant, id());
            watch_ = Watch{};
        } else if (owner == kNoTask && !c.ledger.take(watch_.ant, id())) {
            watch_ = Watch{};
        }
    }
    if (!plan.flower_watch || !plan.flower_sides || !plan.secure_side || v.ticks_left() < 900u || now < watch_off_until_) {
        give_up();
        return;
    }
    uint8_t present = 0;
    for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) present = static_cast<uint8_t>(present | (v.rows()[team].present ? 1u << team : 0u));
    // what the plan lacks of the kinds that it takes for its own side (no ant of the kind, none on its way)
    uint32_t lacking = 0;
    for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief}) {
        if (((plan.secure_kinds >> static_cast<unsigned>(kind)) & 1u) == 0) continue;
        bool have = false;
        for (const AntView& a : v.mine()) have = have || a.type == kind;
        for (const auto& take : takes_) have = have || take.second.kind == kind;
        if (!have) lacking |= 1u << static_cast<unsigned>(kind);
    }
    // the flower: the own side's (no other hill is nearer to its drop tile), the nearest of those to the hill
    const FlowerInfo* flower = nullptr;
    for (const FlowerInfo& f : c.map.flowers()) {
        if (drop_side(c.map, f.drop, present) != static_cast<int>(c.seat)) continue;
        if (flower == nullptr || f.approach[c.seat].cost < flower->approach[c.seat].cost) flower = &f;
    }
    const FlowerLog* log = flower != nullptr ? tactics_.memory.flower(flower->drop) : nullptr;
    if (lacking == 0 || log == nullptr || log->cycle() < 60u || log->chance(lacking) < plan.flower_watch_chance) {
        give_up();                                                                      // nothing to wait for, or the flower is not known well enough yet (a cycle takes two droplets seen)
        return;
    }
    if (watch_.ant != 0 && watch_.drop != flower->drop) give_up();
    const std::vector<int32_t>& to_drop = field_for(c, flower->drop);
    if (to_drop.empty()) {
        give_up();
        return;
    }
    const sim::Grid& grid = v.grid();
    const HillInfo& hill = c.map.hill(c.seat);
    sim::TileCoord spot = watch_.spot;
    if (watch_.ant == 0) {                                                              // the tile beside the drop tile that the ant stands on: a free one, the nearest to the hill
        bool found = false;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const sim::TileCoord n{flower->drop.x + dx, flower->drop.y + dy};
                if ((dx == 0 && dy == 0) || !grid.in_bounds(n) || to_drop[tile_index(grid, n)] < 0 || v.powerup_at(n) != nullptr) continue;
                bool occupied = false;
                for (const AntView& a : v.others()) occupied = occupied || a.tile == n;
                for (const AntView& a : v.mine()) occupied = occupied || a.tile == n;
                if (occupied) continue;
                if (!found || n.chebyshev_dist(hill.queue) < spot.chebyshev_dist(hill.queue)) spot = n;
                found = true;
            }
        }
        if (!found) return;
    }
    const std::vector<int32_t>& to_spot = field_for(c, spot);
    if (to_spot.empty() || !grid.in_bounds(hill.queue) || to_spot[tile_index(grid, hill.queue)] < 0) {
        give_up();                                                                      // (a place that no walk joins to the hill: the watcher would stand there for good)
        return;
    }
    // the next landing the bot expects (a landing that did not come, the tile was held, moves it one cycle on) and the walk of the trip there
    uint64_t next = log->next();
    while (next + 60u < now) next += log->cycle();
    const uint64_t round_trip = 2u * (static_cast<uint64_t>(MapInfo::walking_ticks(to_spot[tile_index(grid, hill.queue)])) + 5u);
    if (watch_.ant == 0) {
        const AntView* best = nullptr;
        int32_t best_cost = 0;
        for (const AntView& a : v.mine()) {
            if (!usable_for_powerup(v, a) || takes_.count(a.id) != 0 || !grid.in_bounds(a.tile)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            const int32_t cost = to_spot[tile_index(grid, a.tile)];
            if (cost < 0 || (best != nullptr && cost >= best_cost)) continue;
            best = &a;
            best_cost = cost;
        }
        if (best == nullptr) return;
        const uint64_t walk = static_cast<uint64_t>(MapInfo::walking_ticks(best_cost)) + 5u;
        if (walk > params_.max_trip_ticks || now + walk + plan.flower_watch_early < next) return;          // not yet: it leaves so as to arrive flower_watch_early ticks before the landing
        if (!c.ledger.take(best->id, id())) return;
        watch_ = Watch{best->id, flower->drop, spot, 0, best->hp};
        ++watched_;
    }
    const AntView* w = find_ant(v.mine(), watch_.ant);
    if (w == nullptr) return;
    if (w->tile == watch_.spot) {
        const bool landing_pending = now <= log->last + 60u;                              // a droplet falls or has just landed: the watcher is there for that one
        if (!landing_pending && next > now + round_trip + plan.flower_watch_early + 30u) give_up();   // the wait for the next one is longer than a trip there and back: it goes home
        return;
    }
    if (takes_.count(watch_.ant) == 0 && w->takes_orders() && (watch_.ordered == 0 || (w->idle() && now >= watch_.ordered + 80u))) {      // (the first order goes to an ant that walks too: the engine's harvest loop leads it to a pile)
        c.orders.move({watch_.ant}, watch_.spot, Priority::Normal);
        watch_.ordered = now;
    }
}

// The free tile beside a drop tile that the ant walks to (the nearest to where it stands): a tile that the field reaches, with no power-up and no other ant on it (-1, -1: none)
sim::TileCoord PowerUpTask::free_spot(TaskContext& c, sim::TileCoord drop, const std::vector<int32_t>& field, sim::TileCoord from, uint32_t self) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    sim::TileCoord spot{-1, -1};
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const sim::TileCoord n{drop.x + dx, drop.y + dy};
            if ((dx == 0 && dy == 0) || !grid.in_bounds(n) || field.empty() || field[tile_index(grid, n)] < 0 || v.powerup_at(n) != nullptr) continue;
            bool occupied = false;
            for (const AntView& a : v.others()) occupied = occupied || a.tile == n;
            for (const AntView& a : v.mine()) occupied = occupied || (a.tile == n && a.id != self);
            if (occupied) continue;
            if (spot.x < 0 || n.chebyshev_dist(from) < spot.chebyshev_dist(from)) spot = n;
        }
    }
    return spot;
}

// The watcher of a flower strip that the flower link opens (island_link.hpp): a plain ant of the crew that stands idle on the ring half walks over the finished tiles to the tile beside the drop tile
// and stays there, whatever the time to the next landing is (nothing waits for it at home: the island has no way back), and takes the Swimmer that falls next. No landing is predicted.
void PowerUpTask::watch_link(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const auto give_up = [&]() {
        if (watch_.ant == 0 || takes_.count(watch_.ant) != 0) return;
        c.ledger.release(watch_.ant, id());
        watch_ = Watch{};
    };
    if (watch_.ant != 0) {
        const AntView* w = find_ant(v.mine(), watch_.ant);
        const TaskId owner = w != nullptr ? c.ledger.owner(watch_.ant) : kNoTask;
        if (w != nullptr && w->hp < watch_.hp) watch_off_until_ = now + kWatchHoldAfterHit;
        if (w == nullptr || (w->type != v.default_ant_type() && w->type != sim::AntType::Fire) || (owner != id() && owner != kNoTask) || w->holding || w->carried_points > 0) {
            if (w != nullptr && owner == id() && takes_.count(watch_.ant) == 0) c.ledger.release(watch_.ant, id());
            watch_ = Watch{};
        } else if (owner == kNoTask && !c.ledger.take(watch_.ant, id())) {
            watch_ = Watch{};
        }
    }
    if (v.ticks_left() < 900u || now < watch_off_until_) {
        give_up();
        return;
    }
    // what the bot lacks: Swimmers up to the level's number and the flower link's, and (when the plan takes it) a Thief
    uint32_t swimmers = 0;
    bool thief = false;
    for (const AntView& a : v.mine()) {
        swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
        thief = thief || a.type == sim::AntType::Thief;
    }
    for (const auto& t : takes_) {
        swimmers += t.second.kind == sim::AntType::Swimmer ? 1u : 0u;
        thief = thief || t.second.kind == sim::AntType::Thief;
    }
    const bool want_swimmer = swimmers < plan.island_swimmers + plan.island_flower_swimmers;
    const bool want_thief = plan.island_flower_thief && !thief;
    if (!want_swimmer && !want_thief) {
        give_up();
        return;
    }
    const sim::Grid& grid = v.grid();
    // the strip and the ant: the link whose drop tile some free plain ant reaches on the map as it is now (the walk from the ant's own tile over the tiles that stand), the nearest ant
    const AntView* best = nullptr;
    const Tactics::FlowerLink* link = nullptr;
    const std::vector<int32_t>* best_field = nullptr;
    int32_t best_cost = 0;
    for (const Tactics::FlowerLink& l : tactics_.flower_links) {
        if (watch_.ant != 0 && watch_.drop != l.drop) continue;
        const std::vector<int32_t>& f = field_for(c, l.drop);
        if (f.empty()) continue;
        for (const AntView& a : v.mine()) {
            if (watch_.ant != 0 && a.id != watch_.ant) continue;
            if (!usable_for_powerup(v, a, true) || (takes_.count(a.id) != 0 && a.id != watch_.ant) || !grid.in_bounds(a.tile)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            const int32_t cost = f[tile_index(grid, a.tile)];
            if (cost < 0 || (best != nullptr && cost >= best_cost)) continue;
            best = &a;
            best_cost = cost;
            link = &l;
            best_field = &f;
        }
    }
    if (best == nullptr) {
        if (watch_.ant != 0 && find_ant(v.mine(), watch_.ant) != nullptr && find_ant(v.mine(), watch_.ant)->tile == watch_.spot) return;     // (it stands at its place: the bridge may be gone, it stays)
        give_up();
        return;
    }
    if (watch_.ant == 0) {
        const uint64_t walk = static_cast<uint64_t>(MapInfo::walking_ticks(best_cost)) + 5u;
        if (walk > params_.link_trip_ticks) return;
        const sim::TileCoord spot = free_spot(c, link->drop, *best_field, best->tile, best->id);
        if (spot.x < 0 || !c.ledger.take(best->id, id())) return;
        watch_ = Watch{best->id, link->drop, spot, 0, best->hp, true};
        ++watched_;
    }
    const AntView* w = find_ant(v.mine(), watch_.ant);
    if (w == nullptr || w->tile == watch_.spot) return;
    if (takes_.count(watch_.ant) == 0 && w->takes_orders() && (watch_.ordered == 0 || (w->idle() && now >= watch_.ordered + 80u))) {
        bool free = v.powerup_at(watch_.spot) == nullptr;                                          // (a typed ant leaves its old hat on a free tile beside the drop tile: no click names a hat but a pick-up)
        for (const AntView& a : v.others()) free = free && a.tile != watch_.spot;
        if (!free) {
            const sim::TileCoord spot = free_spot(c, watch_.drop, field_for(c, watch_.drop), w->tile, w->id);
            if (spot.x < 0) return;
            watch_.spot = spot;
        }
        c.orders.move({watch_.ant}, watch_.spot, Priority::Normal);
        watch_.ordered = now;
    }
}

// A typed ant that took a drop from the watcher's place and still stands on the drop tile steps off it once (a standing ant locks the flower: no drop is made on a tile that an ant stands on)
void PowerUpTask::step_off(TaskContext& c) {
    const uint64_t now = c.view.tick();
    for (auto it = step_off_.begin(); it != step_off_.end();) {
        const AntView* a = find_ant(c.view.mine(), it->ant);
        if (a == nullptr || now >= it->until || a->tile != it->drop) {
            it = step_off_.erase(it);
        } else if (a->takes_orders()) {
            sim::TileCoord spot = it->spot;
            if (c.view.powerup_at(spot) != nullptr) spot = free_spot(c, it->drop, field_for(c, it->drop), a->tile, a->id);       // (a Fire Ant that took the drop left its hat on a free tile beside the drop tile, maybe this one: no click names a hat)
            if (spot.x >= 0) c.orders.move({it->ant}, spot, Priority::Urgent);
            it = step_off_.erase(it);
        } else {
            ++it;
        }
    }
}

void PowerUpTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const Profile& profile = c.profile;
    for (const uint32_t ant : c.ledger.ants_of(id())) {                                 // an ant whose trip is over (or never left) is free again (the watcher waits for a landing)
        if (takes_.count(ant) == 0 && ant != watch_.ant) c.ledger.release(ant, id());
    }
    for (auto it = black_.begin(); it != black_.end();) it = it->second <= now ? black_.erase(it) : std::next(it);
    for (auto it = tile_black_.begin(); it != tile_black_.end();) it = it->second <= now ? tile_black_.erase(it) : std::next(it);
    for (auto it = fields_.begin(); it != fields_.end();) it = now >= it->second.tick + params_.field_ttl_ticks ? fields_.erase(it) : std::next(it);        // (a field that is older than its time is searched again when it is asked for)

    // 1. the trips that are under way
    const uint64_t stale_after = profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u + profile.intent_ttl + profile.decision_interval + 8u;
    std::vector<uint32_t> call_back;
    for (auto it = takes_.begin(); it != takes_.end();) {
        const uint32_t ant = it->first;
        Take& t = it->second;
        const AntView* a = find_ant(v.mine(), ant);
        bool over = false;
        if (a == nullptr) {
            over = true;                                                                // it died
        } else if (a->type != t.before) {
            ++taken_;                                                                   // it is a typed ant now: the task of its role claims it
            over = true;
            if (ant == watch_.ant) {
                step_off_.push_back(StepOff{ant, watch_.drop, watch_.spot, now + 200u});
                watch_ = Watch{};
            }
        } else if (c.ledger.owner(ant) != id()) {
            over = true;                                                                // a task of a higher rank (a fight) took it
        } else if (t.sent == kPending && now >= t.decided + stale_after) {
            over = true;                                                                // the order never left, and no fate was reported (a bot driven without the controller)
        } else {
            const PowerUpView* p = v.powerup_at(t.tile);
            if (p == nullptr) {
                over = true;                                                            // somebody took it, or the dropper replaced it with nothing
                tile_black_[key_of(t.tile)] = now + 60;
            } else if (p->standing_ant != 0 && p->standing_ant != ant) {
                over = true;                                                            // somebody stands on it: nobody else can take it
                tile_black_[key_of(t.tile)] = now + 400;
                ++contested_;
            } else if (p->kind != t.kind && a->tile.chebyshev_dist(t.tile) >= (tactics_.plan.flower_recall ? 1 : static_cast<int32_t>(params_.call_back_tiles))) {
                over = true;                                                            // a dropper changed what lies there: a plain order elsewhere cancels the walk before it crosses (the recall: from any distance, else the ant would take a kind that the plan does not want)
                call_back.push_back(ant);
                tile_black_[key_of(t.tile)] = now + 100;
                ++called_back_;
            } else if (tactics_.plan.flower_recover && (a->holding || a->carried_points > 0) && c.map.flower_at(t.tile) != nullptr) {
                over = true;                                                            // the ant took food before the order reached it (a bite does not hear an order): the gate has it now, and the want is tried again with another ant
                ++failed_;
            } else if (t.sent != kPending) {
                const uint64_t due = t.sent + t.walk_ticks + 17u + 40u;
                const bool refused = a->state == sim::UnitState::CantGo && now >= t.sent + 8u;
                if (refused || (now > due && a->idle())) {
                    black_[{ant, key_of(t.tile)}] = now + params_.blacklist_ticks;      // this ant could not take it: the pair is left alone
                    tile_black_[key_of(t.tile)] = now + params_.tile_blacklist_ticks;
                    ++failed_;
                    over = true;
                } else if (now > due + 300u) {
                    call_back.push_back(ant);                                           // a walk that does not end: stop it
                    black_[{ant, key_of(t.tile)}] = now + params_.blacklist_ticks;
                    ++failed_;
                    over = true;
                }
            }
        }
        if (over) {
            c.ledger.release(ant, id());
            it = takes_.erase(it);
        } else {
            ++it;
        }
    }
    if (!call_back.empty()) c.orders.stop(call_back);
    if (tactics_.plan.island_flower_link && !tactics_.flower_links.empty()) watch_link(c);
    else watch(c);
    step_off(c);

    // 2. new trips for what is wanted and missing, in the order of value (Fire, Bomber, Thief, Combat, Swimmer), as many as max_active allows: the opening sends them all at the first look
    for (const sim::AntType kind : tactics_.plan.opening_order) {
        const size_t want = tactics_.wants[static_cast<size_t>(kind)] + extra_want(kind);
        if (want == 0) continue;
        size_t have = 0;
        for (const AntView& a : v.mine()) have += a.type == kind ? 1u : 0u;
        for (const auto& t : takes_) have += t.second.kind == kind ? 1u : 0u;
        while (have < want && takes_.size() < params_.max_active) {
            if (!try_start(c, kind)) break;
            ++have;
        }
    }
}

}  // namespace ants::ai
