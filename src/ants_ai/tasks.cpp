#include "ants_ai/tasks.hpp"

#include <algorithm>
#include <set>

namespace ants::ai {

namespace {

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

const PileView* find_pile(const std::vector<PileView>& piles, uint32_t index) noexcept {
    const auto it = std::lower_bound(piles.begin(), piles.end(), index, [](const PileView& p, uint32_t want) { return p.index < want; });
    return it != piles.end() && it->index == index ? &*it : nullptr;
}


// The walker component an ant stands in: that of its own tile, or, when the analysis of the start does not know the tile as walkable (the footprint of a pile that has been
// eaten, where the idle ants of a finished pile stand), that of the nearest tiles that it does know. -1 when there is none within a few tiles.
int32_t standing_component(const MapInfo& map, uint8_t seat, sim::TileCoord tile) noexcept {
    const int32_t own = map.ant_component(seat, tile);
    if (own >= 0) return own;
    for (int32_t r = 2; r <= 6; ++r) {
        int32_t best = -1;
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != r) continue;
                const int32_t c = map.component(seat, sim::TileCoord{tile.x + dx, tile.y + dy});
                if (c >= 0 && (best < 0 || c < best)) best = c;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

// Whether `tile` is further than `ring` tiles from the 4 x 4 mound of the hill (the ants that queue for the gate stand on the ring around it)
bool far_from_hill(const HillInfo& hill, sim::TileCoord tile, int32_t ring) noexcept {
    const int32_t dx = std::max({hill.origin.x - tile.x, tile.x - (hill.origin.x + 3), 0});
    const int32_t dy = std::max({hill.origin.y - tile.y, tile.y - (hill.origin.y + 3), 0});
    return std::max(dx, dy) > ring;
}

// Whether `tile` is within a few tiles of the pile: the ants that work a pile queue up at its approach tiles, and wait there for the ant in front to bite and walk home
bool near_pile(const MapInfo& map, uint32_t pile, sim::TileCoord tile) noexcept {
    const PileInfo* info = map.pile(pile);
    return info != nullptr && tile.chebyshev_dist(info->anchor) <= 4;
}

// Whether a pile touches the walker component `comp` (a cell of the pile has a neighbour in it). An ant whose component is not known is not told "no".
bool pile_touches(const MapInfo& map, uint8_t seat, int32_t comp, const PileInfo& pile) noexcept {
    if (comp < 0) return true;
    for (const sim::TileCoord& cell : pile.cells) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (map.component(seat, sim::TileCoord{cell.x + dx, cell.y + dy}) == comp) return true;
            }
        }
    }
    return false;
}

}  // namespace

// ---- AntLedger ------------------------------------------------------------------------------------------------------------------------------------

bool AntLedger::claim(uint32_t ant, TaskId task) {
    if (task == kNoTask) return false;
    const auto it = owner_.find(ant);
    if (it != owner_.end()) return it->second == task;
    owner_.emplace(ant, task);
    return true;
}

void AntLedger::set_rank(TaskId task, uint8_t rank) { rank_[task] = rank; }

uint8_t AntLedger::rank(TaskId task) const noexcept {
    const auto it = rank_.find(task);
    return it != rank_.end() ? it->second : uint8_t{0};
}

bool AntLedger::take(uint32_t ant, TaskId task) {
    if (task == kNoTask) return false;
    const auto it = owner_.find(ant);
    if (it == owner_.end()) {
        owner_.emplace(ant, task);
        return true;
    }
    if (it->second == task) return true;
    if (rank(it->second) >= rank(task)) return false;                // only a task of a higher rank takes an ant from another
    it->second = task;
    return true;
}

bool AntLedger::release(uint32_t ant, TaskId task) {
    const auto it = owner_.find(ant);
    if (it == owner_.end() || it->second != task) return false;
    owner_.erase(it);
    return true;
}

size_t AntLedger::release_all(TaskId task) {
    size_t freed = 0;
    for (auto it = owner_.begin(); it != owner_.end();) {
        if (it->second == task) {
            it = owner_.erase(it);
            ++freed;
        } else {
            ++it;
        }
    }
    return freed;
}

TaskId AntLedger::owner(uint32_t ant) const noexcept {
    const auto it = owner_.find(ant);
    return it != owner_.end() ? it->second : kNoTask;
}

size_t AntLedger::count(TaskId task) const noexcept {
    size_t n = 0;
    for (const auto& e : owner_) n += e.second == task ? 1u : 0u;
    return n;
}

std::vector<uint32_t> AntLedger::ants_of(TaskId task) const {
    std::vector<uint32_t> out;
    for (const auto& e : owner_) {
        if (e.second == task) out.push_back(e.first);
    }
    return out;
}

void AntLedger::forget_missing(const std::vector<AntView>& alive) {
    for (auto it = owner_.begin(); it != owner_.end();) {
        if (find_ant(alive, it->first) == nullptr) it = owner_.erase(it);
        else ++it;
    }
}

// ---- HarvestTask ----------------------------------------------------------------------------------------------------------------------------------

bool HarvestTask::blacklisted(uint32_t pile, uint64_t tick) const noexcept {
    const auto it = black_.find(pile);
    return it != black_.end() && it->second > tick;
}

bool HarvestTask::excluded(uint32_t ant, uint32_t pile, uint64_t tick) const noexcept {
    const auto it = excluded_.find(std::make_pair(ant, pile));
    return it != excluded_.end() && it->second > tick;
}

void HarvestTask::sync_ledger(AntLedger& ledger) const {
    for (const uint32_t ant : ledger.ants_of(id())) {
        if (recs_.count(ant) == 0) ledger.release(ant, id());
    }
}

void HarvestTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove) return;
    if (fate == Bot::Fate::Filtered && rescue_tile_.x >= 0 && command.tile_x == rescue_tile_.x && command.tile_y == rescue_tile_.y) rescue_blocked_until_ = tick + params_.blacklist_ticks;     // the entrance cannot be clicked
    for (const uint32_t ant : command.ants) {
        const auto it = recs_.find(ant);
        if (it == recs_.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) {
            it->second.sent = tick;                                   // the clock of this order starts when it LEFT, not when it was decided
        } else {
            if (fate == Bot::Fate::Filtered) black_[it->second.pile] = tick + params_.blacklist_ticks;   // the click itself is refused: the pile is left alone for a while, or it is proposed again at every look
            recs_.erase(it);                                          // it never left (expired, its ant died, a newer order took the ant, refused): the ant is idle and empty, so it is in the pool again
        }
    }
}

bool HarvestTask::connected_now(const MapInfo& map, sim::TileCoord tile) const noexcept {
    if (now_field_.empty()) return false;
    const int32_t w = map.width();
    const int32_t h = map.height();
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t x = tile.x + dx;
            const int32_t y = tile.y + dy;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            if (now_field_[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] >= 0) return true;
        }
    }
    return false;
}

bool HarvestTask::held_by_army(const TaskContext& c, sim::TileCoord anchor) const {
    if (params_.race_army_weight == 0) return false;
    const BotView& v = c.view;
    uint32_t enemy = 0;
    for (const AntView& e : v.others()) {
        if (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) continue;
        if (e.tile.chebyshev_dist(anchor) > 4) continue;
        if (e.type == sim::AntType::Combat) enemy += 8u;
        else if (e.state == sim::UnitState::Attacking) enemy += 4u;
    }
    if (enemy < params_.race_army_weight) return false;
    uint32_t own = 0;                                                                        // what the seat has there already (its Combat Ants count double)
    for (const AntView& a : v.mine()) {
        if (a.tile.chebyshev_dist(anchor) <= 6) own += a.type == sim::AntType::Combat ? 8u : 4u;
    }
    return static_cast<uint64_t>(own) * 100u < static_cast<uint64_t>(enemy) * params_.race_army_percent;
}

uint8_t HarvestTask::tier_for(const TaskContext& c, const PileInfo& pile, int32_t own_cost, bool first_wins) const {
    const BotView& v = c.view;
    const uint8_t ally = v.ally();
    size_t competitors = 0;                                                          // live enemy teams that reach the pile at a comparable cost
    bool first = false;                                                              // an enemy that is far nearer than the seat
    bool ally_near = false;
    const uint64_t mine = static_cast<uint64_t>(std::max<int32_t>(own_cost, 1));
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped) continue;                  // a team that dropped out competes for nothing
        const int32_t cost = pile.approach[t].cost;
        if (cost < 0) continue;                                                   // its hill cannot walk there
        const uint64_t theirs = static_cast<uint64_t>(cost) * 100u;
        if (ally < sim::MAX_PLAYERS && t == ally) {
            if (theirs <= mine * params_.contest_high) ally_near = true;          // the ally is as near as the seat, or nearer
            continue;
        }
        if (theirs < mine * params_.contest_low) first = true;
        else if (theirs <= mine * params_.contest_high) ++competitors;
    }
    if (competitors >= 2) return static_cast<uint8_t>(PileClass::Multi);
    if (first && first_wins) return static_cast<uint8_t>(PileClass::Hopeless);          // (the race) one competitor and an enemy that is far nearer: the pile is eaten before the ants get there
    if (competitors == 1 && params_.contest_one_first) return static_cast<uint8_t>(PileClass::One);
    if (competitors == 1) return static_cast<uint8_t>(PileClass::Safe);
    if (first) return static_cast<uint8_t>(PileClass::Hopeless);
    if (ally_near) return static_cast<uint8_t>(PileClass::Shared);
    return static_cast<uint8_t>(PileClass::Safe);
}

void HarvestTask::reask(const TaskContext& c) {
    reach_now_.clear();
    now_field_.clear();
    const BotView& v = c.view;
    if (!v.has_grid()) return;
    if (!c.map.hill(c.seat).present) return;
    // ONE walking field of the map as it is now, with the engine's rules of the moment for this seat (its own and its ally's bombs block, the queue row of a team that dropped
    // out is open ground: BotView::walk_context) and from every walkable tile of the hill's queue row, as the hill's ants leave it; then one cheap question per pile
    MapInfo::NowField field = c.map.field_now(v.grid(), c.seat, v.walk_context());
    for (const PileView& p : v.piles()) {
        const PileInfo* info = c.map.pile(p.index);
        if (info == nullptr || info->approach[c.seat].reachable()) continue;                // the start analysis could reach it: nothing to ask again
        const Approach ap = c.map.approach_now(v.grid(), p.index, field);
        if (ap.reachable()) reach_now_[p.index] = ap;
    }
    now_field_ = std::move(field.cost);
}

void HarvestTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const Profile& profile = c.profile;
    if (!start_ants_known_ && !v.mine().empty()) {                                          // the opening's contest needs a team that can spare an ant (Params::contest_opening_min_ants)
        start_ants_ = v.mine().size();
        start_ants_known_ = true;
    }
    const uint32_t opening_ants = start_ants_ >= params_.contest_opening_min_ants ? params_.contest_opening_ants : 0u;
    const bool race_on = params_.race && start_ants_ >= params_.contest_opening_min_ants && (params_.race_ticks == 0 || now < params_.race_ticks);         // a team that cannot spare an ant for a trip across the map has no race (TINY has 3 ants, SMALL 4)
    const auto in_pool_type = [&](sim::AntType type, sim::AntType def) { return type == def || ((params_.extra_types >> static_cast<unsigned>(type)) & 1u) != 0; };
    // The longest an order of ours can take to leave: the reaction delay plus its jitter, then the time to live in the controller's queue. A record that was never answered
    // by a fate (it cannot happen unless a bot is driven without the controller) is dropped after that, so that no ant stays out of the pool for ever.
    const uint64_t max_delay = profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u;
    const uint64_t stale_after = max_delay + profile.intent_ttl + profile.decision_interval + 8u;

    sync_ledger(c.ledger);
    // an ant that a task of a higher rank took from us (AntLedger::take) is not ours any more: its order of ours is forgotten (nothing else ever takes an ant of the worker bot)
    for (auto it = recs_.begin(); it != recs_.end();) {
        if (c.ledger.owner(it->first) != id()) {
            watch_.erase(it->first);
            it = recs_.erase(it);
        } else {
            ++it;
        }
    }

    // 1. what became of the ants that were ordered
    //    first the evidence: a pile that another ant of the task walks to, bites at or carries from is not a pile that cannot be reached
    std::set<uint32_t> works;
    for (const auto& e : recs_) {
        if (e.second.sent == kPending) continue;
        const AntView* a = find_ant(v.mine(), e.first);
        if (a == nullptr) continue;
        const bool moving = (a->state == sim::UnitState::Walking && !v.has_pending_path(e.first)) || a->state == sim::UnitState::HarvestingFood || a->state == sim::UnitState::EnteringBase;
        if (moving || a->holding || a->carried_points > 0) works.insert(e.second.pile);
    }
    std::vector<uint32_t> stalled;
    for (auto it = recs_.begin(); it != recs_.end();) {
        const uint32_t ant_id = it->first;
        const Rec& r = it->second;
        const AntView* a = find_ant(v.mine(), ant_id);
        bool erase = false;
        if (a == nullptr) {
            erase = true;                                                                   // it died (or drowned)
        } else if (r.sent == kPending) {
            erase = now >= r.decided + stale_after;
        } else {
            const bool carrying = a->holding || a->carried_points > 0;
            const bool settled = now >= r.sent + params_.fail_after_ticks;
            // The "can't go" reaction is evidence about the order only in the first moments after the order LEFT, and while the ant still stands where it stood: later it is the
            // reaction to a traffic jam on the way (a blocked doorway, an ant that stopped in front of it), and the pile is not to blame.
            const bool in_window = now <= r.sent + params_.fail_after_ticks + profile.decision_interval;
            const bool near_origin = r.origin.chebyshev_dist(a->tile) <= 1;
            const PileView* bite = find_pile(v.piles(), r.bite);
            bool failed = false;
            if (!carrying && !v.has_pending_path(ant_id) && near_origin) {
                // The "can't go" reaction lasts 8 ticks and starts once the order has been applied; or the ant has not moved at all by the time it should be on its way
                if (a->state == sim::UnitState::CantGo && now >= r.sent + 8u && in_window) failed = true;
                else if (settled && a->idle() && bite != nullptr) failed = true;
            }
            if (failed) {
                // Whose fault is it, the ant's or the pile's? An ant that is shut in (a nook whose door a stranger blocks) fails on every pile it is sent to, and blacklisting
                // the pile for the whole seat on its word costs the others a pile that they work well. So the ant is kept away from the pile it failed on in any case, and the
                // pile is given up for everybody only when the evidence points at it: nobody else walks to it, bites at it or carries from it, and either a second ant failed on
                // it too or no ant of the task makes progress anywhere (a seat that gets nowhere cannot tell, and the pile is the likelier cause: this is how a wall that came
                // up after the analysis of the map is learned).
                excluded_[std::make_pair(ant_id, r.pile)] = now + params_.blacklist_ticks;
                failers_[r.pile][ant_id] = now;
                // an ant that failed on ANOTHER pile lately is not a witness against this one: it fails wherever it is sent
                const auto piles_failed = [&](uint32_t ant) {
                    size_t n = 0;
                    for (const auto& f : failers_) n += f.second.count(ant);
                    return n;
                };
                size_t witnesses = 0;
                for (const auto& f : failers_[r.pile]) witnesses += piles_failed(f.first) == 1 ? 1u : 0u;
                const bool pile_works = works.count(r.pile) != 0 || (bite != nullptr && bite->remaining < r.units);                     // (or somebody else took a unit from it since the order)
                if (!pile_works && (witnesses >= 2 || works.empty())) {
                    if (!blacklisted(r.pile, now)) {                                        // the ants of one order fail together: the pile is learned once
                        black_[r.pile] = now + params_.blacklist_ticks;
                        ++failures_;
                    }
                } else {
                    ++ant_failures_;
                }
                erase = true;
            } else if (a->idle() && settled) {
                erase = true;                                                               // idle again for another reason: the pile is empty, the ant was hit and healed
            } else if (!carrying && (a->state == sim::UnitState::Walking || v.has_pending_path(ant_id)) && !near_pile(c.map, r.pile, a->tile)) {
                // the watchdog: an ant on its way to a pile that walks, or waits for its path, on the same tile for stall_ticks is jammed (a carrier on its way home is not stopped: it would
                // stand with its food and lose its place in the queue)
                const auto w = watch_.find(ant_id);
                if (w == watch_.end() || w->second.tile != a->tile) {
                    watch_[ant_id] = Watch{a->tile, now};
                } else if (now >= w->second.since + params_.stall_ticks) {
                    stalled.push_back(ant_id);
                    erase = true;
                }
            } else {
                watch_.erase(ant_id);
            }
        }
        if (erase) {
            c.ledger.release(ant_id, id());
            watch_.erase(ant_id);
            it = recs_.erase(it);
        } else {
            ++it;
        }
    }
    if (!stalled.empty()) {
        c.orders.stop(stalled);                                                             // they are ordered again from the pool when they stand, and in smaller groups
        stalls_ += static_cast<uint32_t>(stalled.size());
        throttle_ = std::max<uint32_t>(2u, throttle_ / 2u);
        last_stall_ = now;
    } else if (throttle_ < params_.max_ants_per_look && now >= last_stall_ + 4u * params_.stall_ticks) {
        throttle_ = std::max<uint32_t>(1u, params_.max_ants_per_look);                      // a long quiet time: back to the full pace
    }
    for (auto it = watch_.begin(); it != watch_.end();) {
        if (recs_.count(it->first) == 0) it = watch_.erase(it);
        else ++it;
    }
    for (auto it = excluded_.begin(); it != excluded_.end();) {
        if (it->second <= now) it = excluded_.erase(it);
        else ++it;
    }
    for (auto it = failers_.begin(); it != failers_.end();) {
        for (auto f = it->second.begin(); f != it->second.end();) {
            if (now >= f->second + params_.blacklist_ticks) f = it->second.erase(f);
            else ++f;
        }
        if (it->second.empty()) it = failers_.erase(it);
        else ++it;
    }

    // 1b. a carrier that stands idle with its food for longer than a legitimate wait for the gate: its walk home failed. A person would click it onto the hill.
    if (params_.rescue) {
        std::map<uint32_t, uint64_t> stuck_now;
        std::vector<uint32_t> home;
        const HillInfo& hill = c.map.hill(c.seat);
        std::vector<const AntView*> idle_carriers;
        size_t queued = 0;                                                                  // idle carriers within the ring of the gate
        if (hill.present) {
            for (const AntView& a : v.mine()) {
                if (!in_pool_type(a.type, v.default_ant_type()) || !a.idle() || !(a.holding || a.carried_points > 0)) continue;
                idle_carriers.push_back(&a);
                queued += far_from_hill(hill, a.tile, params_.ring_tiles) ? 0u : 1u;
            }
        }
        for (const AntView* a : idle_carriers) {
            const auto was = stuck_.find(a->id);
            stuck_now[a->id] = was != stuck_.end() ? was->second : now;
        }
        // With many carriers idle at once the gate is the bottleneck: one idle carrier more or less costs nothing, and a rescue sends an ant through the crowd of the queue (it
        // stirs it: 12 to 24 workers on one hill scored up to 10 percent less with rescues than without)
        const bool saturated = idle_carriers.size() >= params_.rescue_max_carriers;
        const bool refused = now < rescue_blocked_until_;                                   // the controller refused a click onto the entrance lately: it is not tried again before the time is up
        for (const AntView* a : idle_carriers) {
            if (saturated || refused) break;
            const uint64_t since = stuck_now[a->id];
            // far from the hill, with nobody queueing at the gate, an idle carrier cannot be waiting for its turn
            const bool far_alone = far_from_hill(hill, a->tile, params_.ring_tiles) && queued == 0;
            const uint64_t need = far_alone ? params_.rescue_far_ticks : params_.rescue_after_ticks;
            if (now < since + need || v.has_pending_path(a->id)) continue;
            const auto last = rescued_.find(a->id);
            if (last != rescued_.end() && now < last->second + params_.rescue_cooldown_ticks) continue;
            rescued_[a->id] = now;
            home.push_back(a->id);
        }
        for (const uint32_t id : home) stuck_now[id] = now;                                 // the ant is on its way (a short walk can happen between two looks): its wait starts again
        stuck_ = std::move(stuck_now);
        for (auto it = rescued_.begin(); it != rescued_.end();) {
            if (now >= it->second + params_.rescue_cooldown_ticks) it = rescued_.erase(it);
            else ++it;
        }
        if (!home.empty()) {
            c.orders.move(home, hill.entrance, Priority::Normal);
            rescue_tile_ = hill.entrance;
            rescues_ += static_cast<uint32_t>(home.size());
        }
    }

    // 2. the pool: idle workers with empty hands that nobody holds and that are not waiting for an order of ours
    std::vector<const AntView*> pool;
    for (const AntView& a : v.mine()) {
        if (!in_pool_type(a.type, v.default_ant_type()) || !a.idle() || a.holding || a.carried_points > 0) continue;   // a carrier walks home by itself: never order it (F12b)
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id()) continue;
        if (recs_.count(a.id) != 0) continue;
        pool.push_back(&a);
    }
    unplaced_ = 0;
    if (pool.empty()) return;

    // 2b. the map as it is now, for what the analysis of the start took for shut off: asked again every reask_ticks, when there is a pile of that kind or there was an ant
    if (v.has_grid() && now >= next_reask_) {
        bool any_shut = want_reask_;
        for (const PileView& p : v.piles()) {
            const PileInfo* info = c.map.pile(p.index);
            if (info != nullptr && !info->approach[c.seat].reachable()) any_shut = true;
        }
        if (any_shut) {
            reask(c);
            want_reask_ = false;
        }
        next_reask_ = now + params_.reask_ticks;
    }

    // 3. the piles that are worth a trip
    // The map as it is now is asked for the piles that were shut off at the start and for those that have been eaten into: with ONE walking field per look, made when the first
    // pile needs it (the seat's rules of the moment: BotView::walk_context), and one cheap question per pile
    MapInfo::NowField now_field;
    bool now_made = false;
    const auto ask_now = [&](uint32_t pile) {
        if (!v.has_grid()) return Approach{};
        if (!now_made) {
            now_field = c.map.field_now(v.grid(), c.seat, v.walk_context());
            now_made = true;
        }
        return c.map.approach_now(v.grid(), pile, now_field);
    };
    const bool watch_fire = params_.fire_aware && v.has_grid() && !v.fire_walls().empty();
    const auto fire_near = [&](sim::TileCoord anchor) {
        for (const FireWallView& w : v.fire_walls()) {
            if (w.tile.chebyshev_dist(anchor) <= params_.fire_radius) return true;
        }
        return false;
    };
    std::vector<Candidate> cands;
    for (const PileView& p : v.piles()) {
        if (blacklisted(p.index, now) || closed_.count(p.index) != 0) continue;
        const PileInfo* info = c.map.pile(p.index);
        if (info == nullptr) continue;                                                      // a lunchbox, or a pile made after the start: not in the analysis
        const PileView* bite = info->bite_index == p.index ? &p : find_pile(v.piles(), info->bite_index);
        if (bite == nullptr) continue;                                                      // the object that a bite takes its units from is empty: nothing to harvest
        Approach ap = info->approach[c.seat];
        bool shut = false;
        if (!ap.reachable()) {
            if (reach_now_.count(p.index) == 0) continue;                                   // the hill could not walk there at the start, nor at the last look at the map
            shut = true;
            ap = ask_now(p.index);                                                          // a pile of that kind has been worked on, or has not: ask for its click tile as it is now
            if (!ap.reachable()) continue;
        } else if (v.has_grid() && (p.remaining < info->units || (watch_fire && fire_near(info->anchor)))) {
            ap = ask_now(p.index);                                                          // a pile that has been eaten into has another footprint: its click tile of the start may no longer be food; one with a fire wall near it may be cut off
            if (!ap.reachable()) continue;
        }
        if (v.has_grid() && v.grid().has_powerup_at(ap.click)) continue;                  // (review experiment) the controller refuses a plain click onto a power-up tile: such a pile cannot be worked
        const int32_t trip = MapInfo::trip_ticks_for_cost(ap.cost);
        if (trip < 0) continue;
        // The endgame veto: points that an ant carries when the clock runs out are lost, and the order leaves up to max_delay ticks from now
        if (static_cast<uint64_t>(v.ticks_left()) < static_cast<uint64_t>(trip) + params_.endgame_margin_ticks + max_delay) continue;
        Candidate k;
        k.pile = p.index;
        k.click = ap.click;
        k.cost = ap.cost;
        k.trip = trip;
        k.rank = profile.value_aware_piles ? static_cast<int64_t>(p.value) * 100000 / trip : -static_cast<int64_t>(ap.cost);
        if (params_.rank_by_remaining && profile.value_aware_piles) k.rank = static_cast<int64_t>(p.value) * static_cast<int64_t>(bite->remaining) * 100000 / trip;
        k.cap = std::max<uint32_t>(1u, std::min<uint32_t>(profile.max_ants_per_pile, static_cast<uint32_t>(trip) / std::max<uint32_t>(1u, params_.gate_gap_ticks) + 2u));
        for (const auto& e : recs_) k.load += e.second.pile == p.index ? 1u : 0u;
        const auto limit = limits_.find(p.index);
        if (limit != limits_.end()) {                                                       // (the island task: a pile over a bridge gets so many ants and no more)
            if (k.load >= limit->second) continue;
            k.cap = std::min<uint32_t>(k.cap, limit->second);
            k.limit = limit->second;
        }
        k.bite = info->bite_index;
        k.units = bite->remaining;
        k.shut_at_start = shut;
        if (params_.contest_aware || params_.contest_reactive || (opening_ants > 0 && !race_on)) {
            const uint8_t cls = tier_for(c, *info, ap.cost);
            const uint8_t safe = static_cast<uint8_t>(PileClass::Safe);
            const uint8_t multi = static_cast<uint8_t>(PileClass::Multi);
            if (params_.contest_aware) {
                k.tier = cls;
            } else if (params_.contest_reactive) {
                bool enemy_there = false;
                for (const AntView& e : v.others()) {
                    if (e.team == c.seat || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) continue;
                    if (e.tile.chebyshev_dist(info->anchor) <= 3) enemy_there = true;
                }
                k.tier = enemy_there && (cls == multi || cls == static_cast<uint8_t>(PileClass::One)) ? multi : safe;
            } else if (now < params_.contest_opening_ticks && cls == multi) {
                k.tier = multi;
                k.cap = std::min<uint32_t>(k.cap, opening_ants);
            } else {
                k.tier = safe;
            }
        }
        if (race_on) {
            const uint8_t cls = tier_for(c, *info, ap.cost, true);
            k.cls = cls;
            k.race = (cls == static_cast<uint8_t>(PileClass::Multi) || (params_.race_one && cls == static_cast<uint8_t>(PileClass::One))) && !held_by_army(c, info->anchor);
            if (k.race && params_.race_ants > 0) k.cap = std::max<uint32_t>(1u, std::min<uint32_t>(k.cap, params_.race_ants));
        }
        cands.push_back(k);
    }
    if (cands.empty()) {
        unplaced_ = pool.size();
        return;
    }
    if (params_.contest_aware || params_.contest_reactive || (opening_ants > 0 && !race_on)) {
        tiers_.clear();
        for (const Candidate& k : cands) tiers_[k.pile] = k.tier;
    } else if (race_on) {
        tiers_.clear();
        for (const Candidate& k : cands) tiers_[k.pile] = k.race ? k.cls : static_cast<uint8_t>(PileClass::Safe);     // (the class of a pile that is a race now; the others read as Safe)
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) { return x.tier != y.tier ? x.tier < y.tier : x.rank > y.rank; });     // ties: the lower pile index (the view's order)

    // 4. every ant of the pool to the first pile with room that it can walk to, else to the best one; the groups are cut at max_group_ants
    const int32_t hill_component = c.map.hill_component(c.seat);
    std::vector<std::vector<uint32_t>> groups(cands.size());
    size_t new_this_look = 0;
    // The race: the gate is full when the ants at work deliver one deposit per race_gap_ticks; the ants after that are surplus and take the piles where a race is open first (Multi before
    // One, each by points per trip), then the plain order
    std::vector<size_t> plain_order(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) plain_order[i] = i;
    std::vector<size_t> race_order;
    int64_t fill_micro = 0;                                                                 // deposits per tick of the ants at work, times a million
    if (race_on) {
        for (const size_t i : plain_order) {
            if (cands[i].race) race_order.push_back(i);
        }
        std::stable_sort(race_order.begin(), race_order.end(), [&](size_t x, size_t y) { return cands[x].cls != cands[y].cls ? cands[x].cls < cands[y].cls : cands[x].rank > cands[y].rank; });
        for (const size_t i : plain_order) {
            if (!cands[i].race) race_order.push_back(i);
        }
        for (const auto& e : recs_) {
            int32_t cost = -1;
            if (const PileInfo* info = c.map.pile(e.second.pile); info != nullptr && info->approach[c.seat].reachable()) cost = info->approach[c.seat].cost;
            else if (const auto rn = reach_now_.find(e.second.pile); rn != reach_now_.end()) cost = rn->second.cost;
            const int32_t trip = MapInfo::trip_ticks_for_cost(cost);
            fill_micro += 1000000 / std::max<int32_t>(trip > 0 ? trip : 400, 1);
        }
    }
    for (const AntView* a : pool) {
        if (new_this_look >= throttle_) break;                                              // the rest of the pool is ordered at the next looks
        const int32_t stands_in = standing_component(c.map, c.seat, a->tile);
        // An ant on another island than its hill's cannot deliver what it bites (and a pile of its own island is not one of the hill's): never send it, unless the map as it is
        // now lets it walk to the hill (the food or the wall that shut it in is gone)
        bool free_now = false;
        if (hill_component >= 0 && stands_in >= 0 && stands_in != hill_component) {
            want_reask_ = true;
            free_now = connected_now(c.map, a->tile);
            if (!free_now) {
                ++unplaced_;
                continue;
            }
        }
        size_t pick = cands.size();
        size_t best = cands.size();
        uint32_t at_race = 0;                                                               // the ants that work (or are sent to) a pile where a race is open
        for (const Candidate& k : cands) at_race += k.race ? k.load : 0u;
        const bool surplus = race_on && (fill_micro * static_cast<int64_t>(params_.race_gap_ticks) >= 1000000 * static_cast<int64_t>(params_.race_slack_percent) / 100 || at_race < params_.race_floor);
        const std::vector<size_t>& order = surplus ? race_order : plain_order;
        for (const size_t k : order) {
            if (excluded(a->id, cands[k].pile, now)) continue;                              // this ant failed there lately
            if (cands[k].load >= cands[k].limit) continue;                                  // (a limit holds for the fallback below as well)
            if (!free_now && !cands[k].shut_at_start && !pile_touches(c.map, c.seat, stands_in, *c.map.pile(cands[k].pile))) continue;
            if (best == cands.size()) best = k;
            if (cands[k].load < cands[k].cap) {
                pick = k;
                break;
            }
        }
        if (pick == cands.size()) pick = best;
        if (pick == cands.size()) {
            ++unplaced_;
            continue;
        }
        ++new_this_look;
        if (race_on) fill_micro += 1000000 / std::max<int32_t>(cands[pick].trip, 1);
        Rec r;
        r.pile = cands[pick].pile;
        r.decided = now;
        r.origin = a->tile;
        r.bite = cands[pick].bite;
        r.units = cands[pick].units;
        recs_[a->id] = r;
        c.ledger.claim(a->id, id());
        ++cands[pick].load;
        groups[pick].push_back(a->id);
    }
    // the orders leave one by one: with the race the longest trips go first (the contested piles), then the plain order
    for (size_t i = 0; i < cands.size(); ++i) {
        const size_t k = race_on ? race_order[i] : i;
        if (groups[k].empty()) continue;
        c.orders.move(groups[k], cands[k].click, Priority::Normal);
        ++orders_issued_;
        ants_sent_ += static_cast<uint32_t>(groups[k].size());
    }
}

}  // namespace ants::ai
