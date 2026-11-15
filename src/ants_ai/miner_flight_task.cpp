#include "ants_ai/miner_flight_task.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iterator>

#include "ants_ai/map_info.hpp"

namespace ants::ai {

namespace {

constexpr uint64_t kGap = 40;                    // ticks between two orders to the same ant
constexpr uint64_t kClock = 1500;                // a flight that has not landed this long after it began is given up
constexpr uint64_t kHopWait = 160;               // an ant that is still on its island this long after the hop order: the bomb was a dud
constexpr uint32_t kMaxAttempts = 4;
constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

bool occupied(const BotView& v, sim::TileCoord t, uint32_t except) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == t && a.id != except) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == t) return true;
    }
    return false;
}

// The points that lie on the piles of an island
int64_t island_points(const BotView& v, const MapInfo& map, const IslandComponent& comp) {
    int64_t points = 0;
    for (const PileView& p : v.piles()) {
        if (p.lunchbox || map.pile(p.index) == nullptr) continue;
        for (const uint32_t idx : comp.piles) {
            if (idx == p.index) points += static_cast<int64_t>(p.value) * p.remaining;
        }
    }
    return points;
}

}  // namespace

void MinerFlightTask::finish(AntLedger& ledger) {
    if (job_.ant != 0) ledger.release(job_.ant, id());
    job_ = Job{};
    Task::finish(ledger);
}

void MinerFlightTask::end_job(TaskContext& c, bool landed) {
    c.ledger.release(job_.ant, id());
    if (landed) {
        ++landed_;
        if (first_landing_ == 0) first_landing_ = c.view.tick();
    }
    else {
        ++gave_up_;
        shunned_[job_.ant] = c.view.tick() + 1200u;
    }
    job_ = Job{};
}

// The Bomber to fly (free, healthy, on an island that flights join to the island of a pile) and the island to fly to: the pile island with the most points, over the fewest flights
bool MinerFlightTask::begin(TaskContext& c) {
    const BotView& v = c.view;
    const IslandInfo& info = islands_->info();
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    // a Bomber that is already on an island with food (no hill's) is the miner: no other flies
    uint32_t miners = 0;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Bomber) continue;
        const IslandComponent* here = info.component(c.map.ant_component(c.seat, a.tile));
        if (here != nullptr && !here->hill && island_points(v, c.map, *here) >= 400) ++miners;
    }
    if (miners >= plan.island_miners) return false;
    const AntView* best = nullptr;
    int32_t best_target = -1;
    size_t best_len = 1u << 20;
    int64_t best_points = -1;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Bomber || a.hp < 6 || !a.takes_orders() || a.holding || c.ledger.owner(a.id) != kNoTask) continue;
        const auto sh = shunned_.find(a.id);
        if (sh != shunned_.end() && sh->second > now) continue;
        const int32_t from = c.map.ant_component(c.seat, a.tile);
        if (from < 0) continue;
        for (const IslandComponent& comp : info.components()) {
            if (comp.id == from || comp.hill) continue;
            const int64_t points = island_points(v, c.map, comp);
            if (points < 400) continue;
            const size_t len = info.flight_path(from, comp.id).size();
            if (len == 0) continue;
            if (len < best_len || (len == best_len && points > best_points)) {
                best = &a;
                best_target = comp.id;
                best_len = len;
                best_points = points;
            }
        }
    }
    if (best == nullptr || !c.ledger.take(best->id, id())) return false;
    job_ = Job{};
    job_.ant = best->id;
    job_.target = best_target;
    job_.here = c.map.ant_component(c.seat, best->tile);
    job_.begun = now;
    ++started_;
    if (first_start_ == 0) first_start_ = now;
    return true;
}

// The flight of the next hop: from the island the ant is on to the next of the way, the shore tile S nearest to it, with B and the landing free
bool MinerFlightTask::choose(TaskContext& c, const AntView& a) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const IslandInfo& info = islands_->info();
    const std::vector<int32_t> path = info.flight_path(job_.here, job_.target);
    if (path.empty()) return false;
    const Flight* pick = nullptr;
    int32_t best = 1 << 20;
    for (const Flight* f : info.flights_between(job_.here, path.front())) {
        if (!grid.in_bounds(f->bomb) || !grid.get_cell(f->bomb).can_place_bomb() || grid.has_bomb_at(f->bomb) || occupied(v, f->bomb, a.id)) continue;
        if (!MapInfo::walkable(grid, c.seat, f->from, v.walk_context()) || occupied(v, f->from, a.id)) continue;
        const int32_t d = a.tile.chebyshev_dist(f->from);
        if (d < best) {
            best = d;
            pick = f;
        }
    }
    if (pick == nullptr) return false;
    sim::TileCoord station{-1, -1};
    int32_t station_d = 1 << 20;
    for (int k = 0; k < 8; k += 2) {                                                      // a free orthogonal neighbour of B that is no S: the Bomber plants from there
        const sim::TileCoord t{pick->bomb.x + kDx[k], pick->bomb.y + kDy[k]};
        if (t == pick->from || !grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || c.map.component(c.seat, t) != job_.here) continue;
        if (occupied(v, t, a.id) || grid.has_bomb_at(t)) continue;
        const int32_t d = a.tile.chebyshev_dist(t);
        if (d < station_d) {
            station_d = d;
            station = t;
        }
    }
    if (station.x < 0) return false;
    job_.flight = *pick;
    job_.station = station;
    job_.chosen = true;
    job_.attempts = 0;
    job_.hop_at = 0;
    return true;
}

void MinerFlightTask::drive(TaskContext& c) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    const AntView* a = find_ant(v.mine(), job_.ant);
    if (a == nullptr || c.ledger.owner(job_.ant) != id() || a->hp < 4 || now > job_.begun + kClock) {
        if (a != nullptr && c.ledger.owner(job_.ant) == id()) end_job(c, false);
        else job_ = Job{};
        return;
    }
    const int32_t here = c.map.ant_component(c.seat, a->tile);
    if (here == job_.target) {                                                           // landed: the mines are MineTask's
        end_job(c, true);
        return;
    }
    if (here >= 0 && here != job_.here) {                                                // landed on an island of the way
        job_.here = here;
        job_.chosen = false;
    }
    if (!job_.chosen && !choose(c, *a)) {
        if (now > job_.begun + 200u) end_job(c, false);
        return;
    }
    const Flight& f = job_.flight;
    if (grid.has_bomb_at(f.bomb)) {                                                       // the bomb is there: from S the ant steps onto it
        bool busy = false;                                                                 // (an ant on or beside the landing would be thrown into the water)
        for (const AntView& o : v.mine()) busy = busy || (o.id != a->id && o.tile.chebyshev_dist(f.land) <= 1);
        for (const AntView& o : v.others()) busy = busy || o.tile.chebyshev_dist(f.land) <= 1;
        if (a->tile == f.from && a->idle() && !busy && now >= job_.ordered + kGap) {
            c.orders.move({a->id}, f.bomb, Priority::Urgent);
            job_.ordered = now;
            job_.hop_at = now;
            ++hops_;
        } else if (a->tile != f.from && a->idle() && now >= job_.ordered + kGap && !occupied(v, f.from, a->id)) {
            c.orders.move({a->id}, f.from);
            job_.ordered = now;
        }
        return;
    }
    if (job_.hop_at != 0 && now >= job_.hop_at + kHopWait) {                              // thrown nowhere: a dud (the ant burns on B), or the order was lost
        job_.hop_at = 0;
        if (++job_.attempts >= kMaxAttempts) {
            end_job(c, false);
            return;
        }
    }
    if (!(a->idle() || (a->tile == f.bomb && a->takes_orders()))) return;
    const bool at_station = std::abs(a->tile.x - f.bomb.x) + std::abs(a->tile.y - f.bomb.y) == 1 && a->tile != f.from;
    if (!at_station) {
        if (now >= job_.ordered + kGap) {
            c.orders.move({a->id}, job_.station);
            job_.ordered = now;
        }
        return;
    }
    if (occupied(v, f.bomb, a->id) || now < job_.plant_at + 50u) return;
    c.orders.special(a->id, f.bomb);
    job_.plant_at = now;
    job_.ordered = now;
    ++planted_;
}

void MinerFlightTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = shunned_.begin(); it != shunned_.end();) it = it->second <= now ? shunned_.erase(it) : std::next(it);
    if (!plan.island_fly_miner || islands_ == nullptr || !islands_->active() || !v.has_grid() || tactics_.guard_stance) {
        if (job_.ant != 0) end_job(c, false);
        return;
    }
    if (job_.ant != 0) {
        drive(c);
        return;
    }
    if (now < plan.island_mine_from || v.ticks_left() < 3600 || (expedition_ != nullptr && expedition_->active()) || islands_->info().empty()) return;
    if (begin(c)) drive(c);
}

}  // namespace ants::ai
