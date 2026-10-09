#include "ants_ai/island_expedition.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>

namespace ants::ai {

namespace {

constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

int32_t index_of(const sim::Grid& grid, sim::TileCoord t) noexcept { return t.y * static_cast<int32_t>(grid.width()) + t.x; }

/// The tiles between two tiles on one straight line (the walk of a timed chain)
int32_t line_dist(sim::TileCoord a, sim::TileCoord b) noexcept { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

/// The ticks from a click to the moment the ant's tile is the hat's tile, for a hat `tiles` tiles away from an ant at rest on a tile centre (or on the centre that the click snaps it to): 10 for the
/// next tile and 8 for each tile more (measured at all eight entrances of the corner rows of ISLANDS, docs/BOTS.md "Timed clicks"; 9 for the next tile when the walk goes east or south, the cancel
/// then takes one tick more: the window used is from this to one tick more, which is inside both). The next click must be APPLIED in the window that starts there: earlier, the ant has not entered
/// the hat's tile yet and the click finds no way across the hat; three ticks later, the ant takes the hat.
constexpr uint32_t leg_ticks(int32_t tiles) noexcept { return 10u + 8u * static_cast<uint32_t>(tiles > 1 ? tiles - 1 : 0); }

/// Whether the tiles lie on one straight line (one column or one row) and each is farther from the first than the one before: the walk of a timed chain
bool straight_run(const std::vector<sim::TileCoord>& pts) noexcept {
    if (pts.size() < 2) return true;
    bool same_x = true;
    bool same_y = true;
    for (const sim::TileCoord& t : pts) {
        same_x = same_x && t.x == pts[0].x;
        same_y = same_y && t.y == pts[0].y;
    }
    if (!same_x && !same_y) return false;
    int32_t last = 0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const int32_t d = line_dist(pts[0], pts[i]);
        if (d <= last) return false;
        last = d;
    }
    return true;
}

/// The members of a row in the order of a walk from the entrance
std::vector<uint32_t> walk_order(const TokenGroup& group, const TokenGroup::Entrance& entrance) {
    std::vector<uint32_t> order = group.members;
    if (entrance.member != 0) std::reverse(order.begin(), order.end());
    return order;
}

/// Whether an ant of any team stands on the tile, other than `except`
bool occupied(const BotView& v, sim::TileCoord t, uint32_t except = 0) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == t && a.id != except) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == t) return true;
    }
    return false;
}

}  // namespace

// The members of a row in the order of a walk from the entrance. The order of the analysis is that of a walk from the first end, and its reverse for the other; at the corner of the row in the south
// east corner of ISLANDS (tokens at (59,58), (58,59) and (59,59)) the reverse is not the order of a walk from the west end, which meets (58,59) before (59,59). Timed: the tokens are ordered
// by the steps of a walk through the tokens from the entrance (a step to a token beside it, diagonals too; ties keep the order of the analysis), which is the order of a walk at every end. It is the order of a Hard bot (`timed_row`) for the whole
// attempt, also after the row has gone back to one ant for every hat: it is the better order there too, and `itimed=0` keeps the old one
std::vector<uint32_t> ExpeditionTask::walk_of(const MapInfo& map, const TokenGroup& group, const TokenGroup::Entrance& entrance) const {
    std::vector<uint32_t> order = walk_order(group, entrance);
    if (!params_.timed_row) return order;
    constexpr int32_t kFar = 1 << 20;
    std::vector<sim::TileCoord> tiles;
    for (const uint32_t m : order) tiles.push_back(map.powerups()[m].tile);
    std::vector<int32_t> steps(order.size(), kFar);
    std::vector<size_t> queue;
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i].chebyshev_dist(entrance.tile) > 1) continue;
        steps[i] = 1;
        queue.push_back(i);
    }
    for (size_t head = 0; head < queue.size(); ++head) {
        for (size_t j = 0; j < tiles.size(); ++j) {
            if (steps[j] != kFar || tiles[j].chebyshev_dist(tiles[queue[head]]) > 1) continue;
            steps[j] = steps[queue[head]] + 1;
            queue.push_back(j);
        }
    }
    std::vector<size_t> by(order.size());
    for (size_t i = 0; i < by.size(); ++i) by[i] = i;
    std::stable_sort(by.begin(), by.end(), [&](size_t a, size_t b) { return steps[a] < steps[b]; });
    std::vector<uint32_t> out;
    for (const size_t i : by) out.push_back(order[i]);
    return out;
}

uint32_t ExpeditionTask::swimmers_wanted() const { return tactics_.wants[static_cast<size_t>(sim::AntType::Swimmer)]; }

// The Swimmers that the bot still wants (one at least: the row is worked while a Swimmer lies in it)
uint32_t ExpeditionTask::swimmers_more(const BotView& v) const {
    uint32_t have = 0;
    for (const AntView& a : v.mine()) have += a.type == sim::AntType::Swimmer ? 1u : 0u;
    const uint32_t want = std::max<uint32_t>(1u, swimmers_wanted());
    return want > have ? want - have : 1u;
}

// A crew ant with the hit points to hop that stands on an island of the route up to the one the leg flies from: the leg is still wanted
bool ExpeditionTask::crew_to_come(const TaskContext& c, size_t leg) const {
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(c.view.mine(), ant);
        if (a == nullptr || a->hp < params_.min_hp) continue;
        const int32_t comp = c.map.ant_component(c.seat, a->tile);
        for (size_t j = 0; j <= leg; ++j) {
            if (route_[j] == comp) return true;
        }
    }
    return false;
}

// The tokens of the row that are still there up to the last Swimmer that is wanted: the ants that it takes to be done
uint32_t ExpeditionTask::tokens_needed(const BotView& v) const {
    const uint32_t more = swimmers_more(v);
    uint32_t need = 0;
    uint32_t swimmers = 0;
    for (const sim::TileCoord t : row_) {
        const PowerUpView* p = v.powerup_at(t);
        if (p == nullptr) continue;
        if (chains_now() && p->kind != sim::AntType::Swimmer) continue;                       // (timed: the hats stay where they are, one ant for every Swimmer)
        ++need;
        if (p->kind == sim::AntType::Swimmer && ++swimmers >= more) break;
    }
    return need;
}

// Whether a crew ant can still take a token: it has the hit points to hop, or it stands on the island of the row (it walks)
bool ExpeditionTask::able_to_take(const TaskContext& c, const AntView& a) const {
    return a.hp >= params_.min_hp || c.map.ant_component(c.seat, a.tile) == route_.back();
}

// The plain ants of the crew that can still take a token are one fewer than the tokens, and no Bomber that has flown on is left for the last one (it takes it, drive_row; one that cannot hop
// and is not on the island of the row takes none): the Bomber of this leg takes it, so one ant is all that it can make up
bool ExpeditionTask::crew_one_short(const TaskContext& c) const {
    uint32_t able = 0;
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(c.view.mine(), ant);
        if (a == nullptr) continue;
        if (a->type != c.view.default_ant_type()) {
            if (able_to_take(c, *a)) return false;
        } else if (able_to_take(c, *a)) {
            ++able;
        }
    }
    return able + 1u == tokens_needed(c.view);
}

// No plain ant of the crew can take a token any more, and a Bomber takes only the last one: what is left of the crew waits for nothing
bool ExpeditionTask::crew_hopeless(const TaskContext& c) const {
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(c.view.mine(), ant);
        if (a != nullptr && a->type == c.view.default_ant_type() && able_to_take(c, *a)) return false;
    }
    // (a Bomber takes the last token and no other; timed: not behind hats, where the power-up that it drops would shut the way out)
    const bool bomber = params_.fly_on && !(chains_now() && hats_before_swimmer(c.view));
    return tokens_needed(c.view) > (bomber ? 1u : 0u);
}

bool ExpeditionTask::may_order(uint32_t ant, uint64_t now) const {
    const auto it = ordered_.find(ant);
    return it == ordered_.end() || now >= it->second + gap_;
}

// An order that never left (dropped for lack of budget, refused by the controller): the ant may be ordered again at the next look
void ExpeditionTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t) {
    if (command.ants.empty()) return;
    const bool click = command.type == sim::CommandType::GroupMove && command.ants.size() == 1 && chained_.count(command.ants[0]) != 0;      // (a click of a chain that is ordered for the ant)
    if (fate == Bot::Fate::Sent) {
        if (click) chain_sent_.insert(command.ants[0]);
        return;
    }
    for (const uint32_t ant : command.ants) ordered_.erase(ant);
    if (click && chain_sent_.count(command.ants[0]) == 0) {                                           // the chain never left (refused, superseded, too old): the ant stands where it stood and is free for the next look
        end_chain(command.ants[0]);
        if (row_ant_.erase(command.ants[0]) != 0) row_before_.erase(command.ants[0]);
    }
    for (Leg& l : legs_) {
        if (command.type == sim::CommandType::GroupSpecial && l.bomber == command.ants[0] && l.chosen && command.tile_x == l.flight.bomb.x && command.tile_y == l.flight.bomb.y) l.plant_ordered = 0;
        if (command.type == sim::CommandType::GroupMove && l.hopper == command.ants[0] && l.chosen && command.tile_x == l.flight.bomb.x && command.tile_y == l.flight.bomb.y) {
            l.hopper = 0;
            l.hop_ordered = 0;
        }
    }
}

void ExpeditionTask::finish(AntLedger& ledger) {
    for (const uint32_t ant : crew_) ledger.release(ant, id());
    for (const Leg& l : legs_) {
        if (l.bomber != 0) ledger.release(l.bomber, id());
    }
    ledger.release_all(id());
}

void ExpeditionTask::release_all(TaskContext& c) {
    finish(c.ledger);
    crew_.clear();
    done_.clear();
    landed_.clear();
    legs_.clear();
    route_.clear();
    row_.clear();
    row_ant_.clear();
    row_before_.clear();
    chained_.clear();
    chain_sent_.clear();
    stage_ant_ = 0;
    row_timed_ = false;
    group_ = -1;
    entrance_ = -1;
}

// A Bomber for a leg of the island: an own Bomber that is free is found by the caller; here a Bomber power-up that nobody stands on (its tile in `token`)
bool ExpeditionTask::bomber_source(const TaskContext& c, int32_t island, sim::TileCoord* token) const {
    const IslandComponent* comp = islands_->info().component(island);
    if (comp == nullptr) return false;
    for (const uint32_t idx : comp->powerups) {
        const PowerUpInfo& info = c.map.powerups()[idx];
        const PowerUpView* p = c.view.powerup_at(info.tile);
        if (p == nullptr || p->kind != sim::AntType::Bomber || p->standing_ant != 0) continue;
        if (token != nullptr) *token = info.tile;
        return true;
    }
    return false;
}

// ---- the plan ---------------------------------------------------------------------------------------------------------------------------------------

// The row to go for, the way there and the crew. False when there is nothing to do: no row with a Swimmer in it that flights reach (and a Bomber to take on every island but the last), or
// not enough plain ants on the hill's island
bool ExpeditionTask::plan(TaskContext& c) {
    const BotView& v = c.view;
    const IslandInfo& info = islands_->info();
    const uint64_t now = v.tick();
    if (info.empty()) return false;
    const std::vector<int32_t> dist = info.flight_distances(hill_comp_);
    const uint32_t more = swimmers_more(v);
    const auto free_for_us = [&](const AntView& a) {
        const TaskId owner = c.ledger.owner(a.id);
        return owner == kNoTask || owner == id() || c.ledger.rank(owner) < c.ledger.rank(id());
    };
    const auto own_bomber_on = [&](int32_t island) {
        for (const AntView& a : v.mine()) {
            if (a.type == sim::AntType::Bomber && a.takes_orders() && c.map.ant_component(c.seat, a.tile) == island && free_for_us(a)) return true;
        }
        return false;
    };

    // the cheapest way by flights: 2 for a flight, one more from an island that has no Bomber to take (then the Bomber of the leg before flies on with the crew, which makes the legs wait for
    // it); the hill's island needs a Bomber of its own, and without `fly_on` so does every island of the route but the last
    const size_t n_comps = info.components().size();
    std::vector<int32_t> from_of(n_comps, -2);
    std::vector<int32_t> cost(n_comps, 1 << 20);
    {
        std::vector<int32_t> queue{hill_comp_};
        from_of[static_cast<size_t>(hill_comp_)] = -1;
        cost[static_cast<size_t>(hill_comp_)] = 0;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int32_t a = queue[head];
            const bool source = own_bomber_on(a) || bomber_source(c, a, nullptr);
            if (!source && !(params_.fly_on && a != hill_comp_)) continue;
            const int32_t step = source ? 2 : 3;
            for (const Flight* f : info.flights_from(a)) {
                const size_t b = static_cast<size_t>(f->to_comp);
                if (b >= n_comps || cost[static_cast<size_t>(a)] + step >= cost[b]) continue;
                cost[b] = cost[static_cast<size_t>(a)] + step;
                from_of[b] = a;
                queue.push_back(f->to_comp);
            }
        }
    }
    const auto route_to = [&](int32_t target) {                                                      // the islands from the hill's to `target`; empty when there is none
        std::vector<int32_t> route;
        if (target < 0 || static_cast<size_t>(target) >= n_comps || from_of[static_cast<size_t>(target)] == -2) return route;
        for (int32_t cur = target; cur >= 0; cur = from_of[static_cast<size_t>(cur)]) route.push_back(cur);
        std::reverse(route.begin(), route.end());
        return route;
    };

    struct Choice {
        int32_t group{-1};
        int32_t entrance{-1};
        int64_t score{std::numeric_limits<int64_t>::max()};
        uint32_t ants{0};
        std::vector<int32_t> route;
    } best;
    for (size_t gi = 0; gi < info.token_groups().size(); ++gi) {
        const TokenGroup& g = info.token_groups()[gi];
        for (size_t ei = 0; ei < g.entrances.size(); ++ei) {
            const TokenGroup::Entrance& e = g.entrances[ei];
            if (e.comp < 0 || e.comp == hill_comp_ || static_cast<size_t>(e.comp) >= dist.size() || dist[static_cast<size_t>(e.comp)] <= 0) continue;
            uint32_t ants = 0;                                                                     // the ants that it takes to reach `more` Swimmers from this end
            uint32_t swimmers = 0;
            std::vector<sim::TileCoord> walk;
            for (const uint32_t m : walk_of(c.map, g, e)) {
                const PowerUpView* p = v.powerup_at(c.map.powerups()[m].tile);
                if (p == nullptr) continue;                                                        // (taken already)
                walk.push_back(c.map.powerups()[m].tile);
                ++ants;
                if (p->kind == sim::AntType::Swimmer && ++swimmers >= more) break;
            }
            if (swimmers == 0) continue;
            if (timed_on() && chain_row(v, walk, e.tile)) ants = swimmers;                         // (timed clicks: one ant for every Swimmer, the Fire hats stay)
            std::vector<int32_t> route = route_to(e.comp);
            if (route.size() < 2) continue;
            int32_t land = 1 << 20;                                                                // where the last flight lands, and how far that is from the entrance
            for (const Flight* f : info.flights_between(route[route.size() - 2], route.back())) land = std::min(land, f->land.chebyshev_dist(e.tile));
            const int64_t score = static_cast<int64_t>(cost[static_cast<size_t>(e.comp)]) * 1000000 + static_cast<int64_t>(land) * 100 + ants;
            if (score < best.score) best = Choice{static_cast<int32_t>(gi), static_cast<int32_t>(ei), score, ants, std::move(route)};
        }
    }
    if (best.group < 0) return false;

    // the crew: the plain ants of the hill's island with the hit points for every hop (nearest to the first shore first); one more ant is the Bomber of the first leg when it has none
    const size_t legs = best.route.size() - 1;
    std::vector<const AntView*> plain;
    for (const AntView& a : v.mine()) {
        if (a.type != v.default_ant_type() || a.holding || a.carried_points > 0 || !a.takes_orders() || !free_for_us(a)) continue;
        if (c.map.ant_component(c.seat, a.tile) != hill_comp_ || a.hp < params_.min_hp + 2u * static_cast<uint32_t>(legs - 1)) continue;
        plain.push_back(&a);
    }
    const uint32_t spare = own_bomber_on(hill_comp_) ? 0u : 1u;
    if (plain.size() < params_.crew_min + spare) return false;
    const auto shore_dist = [&](const AntView* a) {
        int32_t d = 1 << 20;
        for (const Flight* f : info.flights_between(best.route[0], best.route[1])) d = std::min(d, a->tile.chebyshev_dist(f->from));
        return d;
    };
    std::stable_sort(plain.begin(), plain.end(), [&](const AntView* x, const AntView* y) { return shore_dist(x) < shore_dist(y); });
    // (a row whose near end starts with a Swimmer needs one ant, a lone Swimmer token too: the crew is never smaller than crew_min, else the same group would be chosen and refused at every look)
    const uint32_t crew_n = std::min<uint32_t>(std::max(best.ants + params_.crew_spare, params_.crew_min), static_cast<uint32_t>(plain.size()) - spare);
    std::set<uint32_t> crew;
    for (const AntView* a : plain) {
        if (crew.size() >= crew_n) break;
        if (c.ledger.take(a->id, id())) crew.insert(a->id);
    }
    if (crew.size() < params_.crew_min) {
        for (const uint32_t ant : crew) c.ledger.release(ant, id());
        return false;
    }
    crew_ = std::move(crew);
    route_ = best.route;
    legs_.assign(legs, Leg{});
    group_ = best.group;
    entrance_ = best.entrance;
    row_.clear();
    const TokenGroup& g = info.token_groups()[static_cast<size_t>(group_)];
    for (const uint32_t m : walk_of(c.map, g, g.entrances[static_cast<size_t>(entrance_)])) row_.push_back(c.map.powerups()[m].tile);
    done_.clear();
    landed_.clear();
    row_ant_.clear();
    row_before_.clear();
    chained_.clear();
    chain_sent_.clear();
    stage_ant_ = 0;
    row_timed_ = false;
    if (timed_on()) {
        std::vector<sim::TileCoord> walk;
        for (const sim::TileCoord t : row_) {
            if (v.powerup_at(t) != nullptr) walk.push_back(t);
        }
        row_timed_ = chain_row(v, walk, g.entrances[static_cast<size_t>(entrance_)].tile);
    }
    progress_ = now;
    ++planned_;
    return true;
}

// ---- the legs ---------------------------------------------------------------------------------------------------------------------------------------

// The flight of a leg: of those the analysis lists between the two islands, one whose bomb tile is still bomb ground, whose landing the engine accepts, whose shore tile can be stood on and is
// not held by a stranger, with nobody (but the crew) on the bomb tile, and a free tile beside the bomb tile for the Bomber to stand on. The shore tile nearest to the crew is the best
bool ExpeditionTask::choose_flight(TaskContext& c, size_t leg, const std::vector<const AntView*>& hoppers) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    Leg& l = legs_[leg];
    const int32_t here = route_[leg];
    int64_t best_score = std::numeric_limits<int64_t>::min();
    for (const Flight* f : islands_->info().flights_between(here, route_[leg + 1])) {
        if (!(bomb_ground(grid, f->bomb) || grid.has_bomb_at(f->bomb)) || !flight_landing_ok(grid, f->land) || !MapInfo::walkable(grid, c.seat, f->from, v.walk_context())) continue;
        bool stranger = false;                                                                      // somebody else on S or B (a crew ant on B is a bomb that was a dud: it moves)
        for (const AntView& a : v.others()) stranger = stranger || a.tile == f->from || a.tile == f->bomb;
        for (const AntView& a : v.mine()) stranger = stranger || ((a.tile == f->from || a.tile == f->bomb) && crew_.count(a.id) == 0 && a.id != l.bomber);
        if (stranger || grid.has_bomb_at(f->from)) continue;
        bool station = false;
        for (int k = 0; k < 8 && !station; k += 2) {                                                // (an orthogonal neighbour of B: dx, dy of the even directions)
            const sim::TileCoord t{f->bomb.x + kDx[k], f->bomb.y + kDy[k]};
            station = t != f->from && grid.in_bounds(t) && MapInfo::walkable(grid, c.seat, t, v.walk_context()) && c.map.component(c.seat, t) == here && !grid.has_bomb_at(t);
        }
        if (!station) continue;
        int32_t nearest = 1 << 20;
        for (const AntView* a : hoppers) nearest = std::min(nearest, a->tile.chebyshev_dist(f->from));
        const int64_t score = (grid.has_bomb_at(f->bomb) ? 1000000 : 0) + 100000 - static_cast<int64_t>(nearest) * 100 - (f->diagonal() ? 1 : 0);   // (a bomb that lies there is used)
        if (score <= best_score) continue;
        best_score = score;
        l.flight = *f;
        l.chosen = true;
    }
    return best_score != std::numeric_limits<int64_t>::min();
}

void ExpeditionTask::drive_leg(TaskContext& c, size_t leg) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    Leg& l = legs_[leg];
    const int32_t here = route_[leg];
    const auto island_of = [&](const AntView& a) { return c.map.ant_component(c.seat, a.tile); };
    const auto free_for_us = [&](const AntView& a) {
        const TaskId owner = c.ledger.owner(a.id);
        return owner == kNoTask || owner == id() || c.ledger.rank(owner) < c.ledger.rank(id());
    };

    // ---- the Bomber of the leg: an own Bomber of the island that is free, else a Bomber that flew here with the crew, else a plain ant that takes the island's Bomber power-up (a crew ant when no
    // other ant is there); it is made ready while the crew is still on its way
    const AntView* bm = l.bomber != 0 ? find_ant(v.mine(), l.bomber) : nullptr;
    if (bm != nullptr && island_of(*bm) != here) {                                                  // (it flew on, as one of the crew, and may be a later leg's by now: this leg is over, its own hop was the last)
        l.bomber = 0;
        l.hopper = 0;
        l.flown = true;
    }
    if (l.flown) return;
    if (bm == nullptr || c.ledger.owner(bm->id) != id()) {
        l.bomber = 0;
        bm = nullptr;
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Bomber || crew_.count(a.id) != 0 || island_of(a) != here || !a.takes_orders() || !free_for_us(a)) continue;
            if (!c.ledger.take(a.id, id())) continue;
            l.bomber = a.id;
            bm = &a;
            break;
        }
    }
    for (const AntView& a : v.mine()) {
        if (bm != nullptr) break;
        if (a.type != sim::AntType::Bomber || crew_.count(a.id) == 0 || island_of(a) != here || !a.takes_orders() || c.ledger.owner(a.id) != id()) continue;
        crew_.erase(a.id);
        l.bomber = a.id;
        bm = &a;
    }
    if (bm == nullptr) {
        sim::TileCoord token{-1, -1};
        if (!bomber_source(c, here, &token)) return;                                                // no Bomber to take: the leg waits (the plan looked for one)
        const AntView* pick = nullptr;
        int32_t best = 1 << 20;
        for (int pass = 0; pass < 2 && pick == nullptr; ++pass) {                                   // the ants that are not of the crew first
            for (const AntView& a : v.mine()) {
                const bool of_crew = crew_.count(a.id) != 0;
                if (of_crew != (pass == 1) || a.type != v.default_ant_type() || island_of(a) != here || !a.takes_orders() || a.holding || !free_for_us(a)) continue;
                if (of_crew && a.hp < params_.min_hp) continue;
                const int32_t d = a.tile.chebyshev_dist(token);
                if (d < best) {
                    best = d;
                    pick = &a;
                }
            }
        }
        if (pick == nullptr || !c.ledger.take(pick->id, id())) return;
        crew_.erase(pick->id);
        l.bomber = pick->id;
        c.orders.pick_up(pick->id, token);
        l.bomber_ordered = now;
        ordered_[pick->id] = now;
        return;
    }
    const bool bomber_ready = bm->type == sim::AntType::Bomber;
    if (!bomber_ready) {                                                                            // on its way to the power-up (or its order was lost)
        sim::TileCoord token{-1, -1};
        if (bm->idle() && now >= l.bomber_ordered + gap_ && bomber_source(c, here, &token)) {
            c.orders.pick_up(bm->id, token);
            l.bomber_ordered = now;
        }
    }

    // ---- the landing must be free: when an ant lands on a tile where another stands, the engine throws every ant of the tile to a free neighbour tile, and water is no reason to refuse one (the
    // pile-up of the landing rules, docs/GAME_REVERSE_ENGINEERING.md 5.36): the ant that stood there drowns. So a crew ant that stands near the landing of this leg walks off it at once (an
    // order ends the stun of a flight), toward where it is wanted next, and nobody is thrown while an ant of another team is on or beside the landing, or an own ant stands on it (one that
    // walks off it is gone before the next lands: a flight takes longer than a step)
    const auto landing_busy = [&]() {
        if (!l.chosen) return false;
        for (const AntView& a : v.mine()) {
            if (a.tile == l.flight.land && !(a.state == sim::UnitState::Walking && crew_.count(a.id) != 0)) return true;
        }
        for (const AntView& a : v.others()) {
            if (a.tile.chebyshev_dist(l.flight.land) <= 1) return true;
        }
        return false;
    };
    if (l.chosen) {
        for (const AntView& a : v.mine()) {
            if (a.tile.chebyshev_dist(l.flight.land) > 1 || crew_.count(a.id) == 0 || !a.takes_orders() || a.state == sim::UnitState::Walking || !may_order(a.id, now)) continue;
            sim::TileCoord toward = l.flight.land;                                                   // where the ants of this island are wanted next: the next leg's shore, or the row
            const bool last = leg + 1 == legs_.size();
            Stations st;
            if (leg + 1 < legs_.size() && legs_[leg + 1].chosen) {
                toward = legs_[leg + 1].flight.from;
            } else if (last) {
                stations(c, st);                                                                     // (the park tile, never the throat: the ants that took a token walk out through it)
                toward = st.park;
            }
            sim::TileCoord to{-1, -1};
            int32_t to_d = 1 << 20;
            for (int dy = -6; dy <= 6; ++dy) {
                for (int dx = -6; dx <= 6; ++dx) {
                    const sim::TileCoord t{l.flight.land.x + dx, l.flight.land.y + dy};
                    if (!grid.in_bounds(t) || t.chebyshev_dist(l.flight.land) < 3 || occupied(v, t, a.id) || grid.has_bomb_at(t)) continue;
                    if (last && (st.steps_of(grid, t) < Stations::kThroat || std::find(row_.begin(), row_.end(), t) != row_.end())) continue;
                    const int32_t d = t.chebyshev_dist(toward) * 8 + t.chebyshev_dist(l.flight.land);
                    if (d >= to_d || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || c.map.component(c.seat, t) != route_[leg + 1]) continue;
                    to_d = d;
                    to = t;
                }
            }
            if (to.x >= 0) {
                c.orders.move({a.id}, to);
                ordered_[a.id] = now;
            }
            break;
        }
    }

    // ---- the Bomber flies on, last, as one of the crew (on a bomb of its own: it plants, walks to S and steps on it): when no crew ant that can hop is left on this island or an earlier one,
    // and the next leg has no Bomber to take (and none that has flown on: that leg is over), or the crew that is left is one ant short of the tokens that are wanted (the Bomber takes the last one)
    if (params_.fly_on && bomber_ready && bm->hp >= params_.min_hp && l.hopper == 0 && (leg == 0 || legs_[leg - 1].hopper == 0) && !crew_to_come(c, leg)) {
        const bool next_needs = leg + 1 < legs_.size() && legs_[leg + 1].bomber == 0 && !legs_[leg + 1].flown && !bomber_source(c, route_[leg + 1], nullptr);
        if (next_needs || crew_one_short(c)) crew_.insert(bm->id);
    }

    // ---- the crew that is on this island now (the ant that was thrown last is not asked again while it flies)
    std::vector<const AntView*> hoppers;
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(v.mine(), ant);
        if (a != nullptr && done_.count(ant) == 0 && island_of(*a) == here && c.ledger.owner(ant) == id()) hoppers.push_back(a);
    }
    // a hop that was ordered: the ant lands on the next island; or the bomb was a dud and the ant stands on B (it is at once free again, and ordered off B), or it is still on the island when
    // the time for a flight is over
    if (l.hopper != 0) {
        const AntView* h = find_ant(v.mine(), l.hopper);
        const bool still_here = h != nullptr && island_of(*h) == here;
        const bool burnt = h != nullptr && (h->state == sim::UnitState::Burn || h->state == sim::UnitState::Stunned);   // (a bomb that goes off throws the ant: a dud leaves it on B, burnt)
        if (still_here && burnt && h->tile == l.flight.bomb) {
            ++duds_;
            l.hopper = 0;
        } else if (still_here && now < l.hop_ordered + params_.hop_wait + islands_->latency()) {
            if (h->tile == l.flight.from && h->idle() && grid.has_bomb_at(l.flight.bomb) && now >= l.hop_ordered + gap_ && may_order(h->id, now)) {   // (the order was lost: told again)
                c.orders.move({h->id}, l.flight.bomb, Priority::Urgent);
                ordered_[h->id] = now;
                l.hop_ordered = now;
                return;
            }
            hoppers.erase(std::remove(hoppers.begin(), hoppers.end(), h), hoppers.end());
        } else {
            l.hopper = 0;
        }
    }
    // the Bomber of a later leg prepares the bomb for the ant that is on its way (thrown by the leg before)
    const bool expected = leg > 0 && legs_[leg - 1].hopper != 0;
    if (hoppers.empty() && !expected) return;
    // the flight stays the same for every hop of the leg: it is chosen again only when it cannot be flown (while a bomb is planted or being planted the bomb tile is no bomb ground)
    const bool planting = l.plant_ordered != 0 && now < l.plant_ordered + params_.plant_wait + islands_->latency();
    if (!l.chosen || !flight_landing_ok(grid, l.flight.land) || !MapInfo::walkable(grid, c.seat, l.flight.from, v.walk_context()) ||
        !(grid.has_bomb_at(l.flight.bomb) || planting || bomb_ground(grid, l.flight.bomb))) {
        l.chosen = false;
        if (!choose_flight(c, leg, hoppers)) return;
    }
    const Flight& f = l.flight;

    // ---- the ant that hops next: the crew ant nearest to S that has the hit points (the one that stands on S is the first)
    const AntView* next = nullptr;
    int32_t best = 1 << 20;
    for (const AntView* a : hoppers) {
        if (a->hp < params_.min_hp || !a->takes_orders()) continue;
        const int32_t d = a->tile.chebyshev_dist(f.from);
        if (d < best) {
            best = d;
            next = a;
        }
    }
    if (next == nullptr && !expected) return;

    // ---- the ants that wait: off B (a bomb that was a dud leaves its ant there) and near S, where the next hop is (one order a look); so does the next ant while S is held by the ant that
    // is being thrown
    const auto wait_tile = [&](const AntView& a) {                                                   // the free tile on the island nearest to S that is not S and two or more tiles from B
        sim::TileCoord wait{-1, -1};
        int32_t wait_d = 1 << 20;
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const sim::TileCoord t{f.from.x + dx, f.from.y + dy};
                if (!grid.in_bounds(t) || t == f.from || t.chebyshev_dist(f.bomb) < 2 || occupied(v, t, a.id) || grid.has_bomb_at(t)) continue;
                const int32_t d = t.chebyshev_dist(f.from) * 4 + t.chebyshev_dist(a.tile);
                if (d >= wait_d || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || c.map.component(c.seat, t) != here) continue;
                wait_d = d;
                wait = t;
            }
        }
        return wait;
    };
    const bool s_held = next != nullptr && occupied(v, f.from, next->id);
    for (const AntView* a : hoppers) {
        const bool waits = a != next || (s_held && a->tile.chebyshev_dist(f.from) > 3);
        const bool on_b = a->tile == f.bomb;                                                         // (a dud leaves the ant stunned on B: an order ends the stun)
        const bool on_s = a != next && a->tile == f.from;                                            // (an ant that cannot hop must not shut S for the one that can)
        if (!waits || !(a->idle() || (on_b && a->takes_orders())) || !may_order(a->id, now) || !(on_b || on_s || a->tile.chebyshev_dist(f.from) > 3)) continue;
        const sim::TileCoord wait = wait_tile(*a);
        if (wait.x < 0) break;
        c.orders.move({a->id}, wait);
        ordered_[a->id] = now;
        break;
    }

    // ---- the bomb is there: the ant on S steps onto it
    if (grid.has_bomb_at(f.bomb)) {
        if (next == nullptr) return;
        // (one ant is thrown at a time: the ant that was thrown before has landed and walked off the landing before the next one is sent, or it would be thrown into the water)
        if (next->tile == f.from && next->idle() && may_order(next->id, now) && l.hopper == 0 && !landing_busy()) {
            c.orders.move({next->id}, f.bomb, Priority::Urgent);
            ordered_[next->id] = now;
            l.hopper = next->id;
            l.hop_ordered = now;
            l.hopper_from = f.from;
            l.plant_ordered = 0;                                                                     // (the bomb goes with the hop: the next one may be planted at once)
            ++hops_;
            progress_ = now;
        } else if (next->tile != f.from && next->idle() && may_order(next->id, now) && !occupied(v, f.from, next->id)) {
            c.orders.move({next->id}, f.from);
            ordered_[next->id] = now;
        }
        return;
    }

    // ---- no bomb: the next ant walks to S, and the Bomber to a tile beside B; when it is there it plants (also before the ant is there: the bomb waits for it)
    if (next != nullptr && next != bm && next->tile != f.from && (next->idle() || (next->tile == f.bomb && next->takes_orders())) && may_order(next->id, now) && !occupied(v, f.from, next->id)) {
        c.orders.move({next->id}, f.from);
        ordered_[next->id] = now;
    }
    if (!bomber_ready) return;
    sim::TileCoord station{-1, -1};
    int32_t station_dist = 1 << 20;
    for (int k = 0; k < 8; k += 2) {                                                                 // the free orthogonal neighbour of B that the Bomber reaches first
        const sim::TileCoord t{f.bomb.x + kDx[k], f.bomb.y + kDy[k]};
        if (t == f.from || !grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || c.map.component(c.seat, t) != here) continue;
        if (occupied(v, t, bm->id) || grid.has_bomb_at(t)) continue;
        const int32_t d = bm->tile.chebyshev_dist(t);
        if (d < station_dist) {
            station_dist = d;
            station = t;
        }
    }
    if (station.x < 0) return;
    const bool at_station = std::abs(bm->tile.x - f.bomb.x) + std::abs(bm->tile.y - f.bomb.y) == 1 && bm->tile != f.from;
    if (!at_station) {
        if (bm->idle() && may_order(bm->id, now)) {
            c.orders.move({bm->id}, station);
            ordered_[bm->id] = now;
        }
        return;
    }
    if (occupied(v, f.bomb, bm->id)) return;                                                          // (B is held by an ant: it walks off)
    if (bm->idle() && !planting) {
        c.orders.special(bm->id, f.bomb);
        l.plant_ordered = now;
        ++planted_;
        progress_ = now;
        if (first_plant_ == 0) first_plant_ = now;
    }
}

// ---- the row ----------------------------------------------------------------------------------------------------------------------------------------

// The ant that went for a token: when its type changed it took it and walks out of the row; when the token is gone (another team's) it is free to go for the next
void ExpeditionTask::note_taken(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = row_ant_.begin(); it != row_ant_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr) {
            end_chain(it->first);
            row_before_.erase(it->first);
            it = row_ant_.erase(it);
            continue;
        }
        if (a->type != row_before_[it->first]) {
            ++taken_;
            progress_ = now;
            if (chained_.count(a->id) != 0) {                                                         // (a chain took it: a Swimmer ends the run of misses, a hat is a click that came late)
                if (a->type == sim::AntType::Swimmer) {
                    miss_run_ = 0;
                } else {
                    ++chain_misses_;
                    if (++miss_run_ >= params_.chain_misses_max) timed_off_ = true;
                }
            }
            if (a->type == sim::AntType::Swimmer) {
                ++swimmers_taken_;
                if (first_swimmer_ == 0) first_swimmer_ = now;
            }
            done_.insert(a->id);
            end_chain(a->id);                                                                         // (the chain in is over with the pick-up: the way out is another)
            crew_.erase(a->id);
            if (!exit_chains()) c.ledger.release(a->id, id());                                        // (timed: it is the task's until it is out of the row, nobody else may send it anywhere while it crosses the hats)
            row_before_.erase(it->first);
            it = row_ant_.erase(it);
            continue;
        }
        if (v.powerup_at(it->second) == nullptr) {                                                    // (the token is gone, another team's: an ant that stands behind hats comes out over them)
            strand(*a);
            row_before_.erase(it->first);
            it = row_ant_.erase(it);
            continue;
        }
        ++it;
    }
}

// The tiles round the entrance of the row by the steps of a walk (see Stations)
void ExpeditionTask::stations(const TaskContext& c, Stations& out) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const TokenGroup::Entrance& entrance = islands_->info().token_groups()[static_cast<size_t>(group_)].entrances[static_cast<size_t>(entrance_)];
    std::set<int32_t> in_row;                                                                        // the tunnel: the tiles of the tokens and the tile in front of the first
    for (const sim::TileCoord t : row_) in_row.insert(index_of(grid, t));
    in_row.insert(index_of(grid, entrance.tile));
    out = Stations{};
    out.park = entrance.tile;
    out.leave = entrance.tile;
    std::vector<sim::TileCoord> queue{entrance.tile};
    std::vector<int32_t> depth{0};
    std::set<int32_t> reached{index_of(grid, entrance.tile)};
    bool have_park = false;
    for (size_t head = 0; head < queue.size() && head < 600u; ++head) {
        out.steps[index_of(grid, queue[head])] = depth[head];
        if (depth[head] >= 3 && !have_park) {
            out.park = queue[head];
            have_park = true;
        }
        if (depth[head] >= 6) {
            out.leave = queue[head];
            break;
        }
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord t{queue[head].x + kDx[k], queue[head].y + kDy[k]};
            if (!grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context())) continue;
            const int32_t ti = index_of(grid, t);
            if (in_row.count(ti) != 0 || !reached.insert(ti).second) continue;
            queue.push_back(t);
            depth.push_back(depth[head] + 1);
        }
    }
    if (!have_park) out.leave = out.park = entrance.tile;
}

// ---- timed clicks -----------------------------------------------------------------------------------------------------------------------------------

// Whether an ant is left alone because a chain of clicks is under way for it
bool ExpeditionTask::chain_guarded(uint32_t ant, uint64_t now) const {
    const auto it = chained_.find(ant);
    return it != chained_.end() && now < it->second + params_.chain_ticks;
}

void ExpeditionTask::start_chain(uint32_t ant, uint64_t now) {
    chained_[ant] = now;
    chain_sent_.erase(ant);
}

void ExpeditionTask::end_chain(uint32_t ant) {
    chained_.erase(ant);
    chain_sent_.erase(ant);
}

// An ant that is left in the row without a token (the one it went for is gone, or its chain ran out) stands behind the hats that are still there: it is brought out over them like an ant that took
// one, and stays the task's until it is out. One that stands in front of the row is free at once
void ExpeditionTask::strand(const AntView& ant) {
    end_chain(ant.id);
    if (!exit_chains() || std::find(row_.begin(), row_.end(), ant.tile) == row_.end()) return;
    crew_.erase(ant.id);
    done_.insert(ant.id);
}

// Whether a hat lies before the first Swimmer of the row (in the order of a walk from the tile in front of it)
bool ExpeditionTask::hats_before_swimmer(const BotView& v) const {
    bool hat = false;
    for (const sim::TileCoord t : row_) {
        const PowerUpView* p = v.powerup_at(t);
        if (p == nullptr) continue;
        if (p->kind == sim::AntType::Swimmer) return hat;
        hat = true;
    }
    return hat;
}

// Whether the row can be worked by chains: the hats before its first Swimmer lie on one straight line with the tile in front of it (`walk`: the tokens of the row in the order of a walk from there).
// A row with no hat before its first Swimmer needs no chain (the plain click takes it) and counts as one
bool ExpeditionTask::chain_row(const BotView& v, const std::vector<sim::TileCoord>& walk, sim::TileCoord entrance) const {
    std::vector<sim::TileCoord> run{entrance};
    for (const sim::TileCoord t : walk) {
        const PowerUpView* p = v.powerup_at(t);
        if (p == nullptr) continue;
        if (p->kind == sim::AntType::Swimmer) return straight_run(run);
        run.push_back(t);
    }
    return false;                                                                                    // (no Swimmer in it)
}

// The clicks that take an ant at rest on `from` over the hats of `along` (the tiles in front of it, nearest first; the hats are those with a power-up now) to `goal`: one click for every hat, each
// applied when the ant's tile has become the hat's tile before it, and the click for `goal` last. False when there is no hat or the hats do not lie on one straight line with `from`
bool ExpeditionTask::chain_steps(const BotView& v, sim::TileCoord from, const std::vector<sim::TileCoord>& along, sim::TileCoord goal, std::vector<ChainStep>& steps) const {
    std::vector<sim::TileCoord> run{from};
    for (const sim::TileCoord t : along) {
        if (v.powerup_at(t) != nullptr) run.push_back(t);
    }
    if (run.size() < 2 || !straight_run(run)) return false;
    std::vector<sim::TileCoord> targets(run.begin() + 1, run.end());
    targets.push_back(goal);
    steps.clear();
    for (size_t i = 0; i < targets.size(); ++i) {
        ChainStep st;
        st.tile = targets[i];
        st.pickup = v.powerup_at(targets[i]) != nullptr;
        if (i >= 1) {                                                                                // the ant is on its way to targets[i - 1]: that many ticks after the click before
            st.gap_lo = leg_ticks(line_dist(i == 1 ? from : targets[i - 2], targets[i - 1]));
            st.gap_hi = st.gap_lo + params_.chain_slack;
        }
        steps.push_back(st);
    }
    return true;
}

// The chain that takes the next Swimmer of the row: from the tile in front of it over the hats before the first Swimmer token to that token. False when there is no hat to cross (the plain click does)
bool ExpeditionTask::steps_in(const TaskContext& c, sim::TileCoord entrance, std::vector<ChainStep>& steps, sim::TileCoord* target) const {
    std::vector<sim::TileCoord> along;
    for (const sim::TileCoord t : row_) {
        const PowerUpView* p = c.view.powerup_at(t);
        if (p != nullptr && p->kind == sim::AntType::Swimmer) {
            *target = t;
            return chain_steps(c.view, entrance, along, t, steps);
        }
        along.push_back(t);
    }
    return false;
}

// The way of an ant that took a Swimmer out of the row, sent to `goal`: the tile `leave` beyond the tile in front of the row, or the tile in front of the other end. Of the two ends the one the ant
// is in line with and that has no Swimmer token between it and the ant (the ant would take it) is taken, the near one first; the hats between are crossed by a chain (true, `steps` holds it:
// the planner treats hats as solid, a plain click across them finds no way), and with no hat between a plain click does (false). False with `goal` untouched when it is in line with no end
bool ExpeditionTask::steps_out(const TaskContext& c, const AntView& ant, sim::TileCoord leave, std::vector<ChainStep>& steps, sim::TileCoord* goal) const {
    steps.clear();
    const std::vector<TokenGroup::Entrance>& ends = islands_->info().token_groups()[static_cast<size_t>(group_)].entrances;
    std::vector<size_t> order{static_cast<size_t>(entrance_)};
    for (size_t i = 0; i < ends.size(); ++i) {
        if (static_cast<int32_t>(i) != entrance_) order.push_back(i);
    }
    for (const size_t i : order) {
        const sim::TileCoord end = ends[i].tile;
        const int32_t dx = end.x - ant.tile.x;
        const int32_t dy = end.y - ant.tile.y;
        if ((dx != 0 && dy != 0) || (dx == 0 && dy == 0)) continue;                                  // (the rows of ISLANDS are straight between the ends and the corner)
        const int32_t sx = (dx > 0) - (dx < 0);
        const int32_t sy = (dy > 0) - (dy < 0);
        std::vector<sim::TileCoord> between;
        bool swimmer_between = false;
        for (sim::TileCoord t{ant.tile.x + sx, ant.tile.y + sy}; t != end; t = sim::TileCoord{t.x + sx, t.y + sy}) {
            const PowerUpView* p = c.view.powerup_at(t);
            swimmer_between = swimmer_between || (p != nullptr && p->kind == sim::AntType::Swimmer);
            between.push_back(t);
        }
        if (swimmer_between) continue;
        *goal = static_cast<int32_t>(i) == entrance_ ? leave : end;
        return chain_steps(c.view, ant.tile, between, *goal, steps);
    }
    return false;
}

void ExpeditionTask::drive_row(TaskContext& c) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    const int32_t target = route_.back();
    const TokenGroup::Entrance& entrance = islands_->info().token_groups()[static_cast<size_t>(group_)].entrances[static_cast<size_t>(entrance_)];
    std::set<int32_t> in_row;                                                                        // the tunnel: the tiles of the tokens and the tile in front of the first
    for (const sim::TileCoord t : row_) in_row.insert(index_of(grid, t));
    in_row.insert(index_of(grid, entrance.tile));
    Stations st;
    stations(c, st);
    const sim::TileCoord park = st.park;
    const sim::TileCoord leave = st.leave;
    const bool timed = chains_now();
    const bool out_chains = exit_chains();
    if (stage_ant_ != 0 && (now > stage_since_ + 300 || crew_.count(stage_ant_) == 0)) stage_ant_ = 0;      // (the ant that was sent to the tile in front of the row did not get there, or is gone)

    // the ants that took their token walk out (told again when they stand still in the row); nobody goes in while one is in the tunnel. Timed: the ant that took a Swimmer behind hats that are
    // still there is brought out by a chain over them, and stays the task's until it is out
    bool tunnel_busy = false;
    for (auto it = done_.begin(); it != done_.end();) {
        const AntView* a = find_ant(v.mine(), *it);
        if (a == nullptr || in_row.count(index_of(grid, a->tile)) == 0) {
            if (a != nullptr && out_chains) c.ledger.release(a->id, id());
            end_chain(*it);
            it = done_.erase(it);
            continue;
        }
        tunnel_busy = true;
        if (out_chains && chain_guarded(a->id, now)) {
            ++it;
            continue;
        }
        if (a->idle() && may_order(a->id, now)) {
            std::vector<ChainStep> steps;
            sim::TileCoord goal = leave;
            if (out_chains && steps_out(c, *a, leave, steps, &goal)) {                                 // (it stands in the row again after a chain: the chain is ordered again)
                c.orders.chain(a->id, steps);
                start_chain(a->id, now);
                ++chains_out_;
            } else {
                c.orders.move({a->id}, goal, Priority::Urgent);
            }
            ordered_[a->id] = now;
        }
        ++it;
    }
    // the ant that goes in is watched: told again when it stands still without its token. Timed: an ant that a chain was ordered for is left alone while the chain runs; when the time is over and
    // it has no token the chain missed, and the ant is free for the next one (after chain_misses_max misses in a row, a hat taken by a click that came late counts as one, the row goes back to one ant
    // for every hat)
    std::vector<uint32_t> missed;
    for (const auto& e : row_ant_) {
        tunnel_busy = true;
        const AntView* a = find_ant(v.mine(), e.first);
        if (timed && chained_.count(e.first) != 0) {
            if (!chain_guarded(e.first, now) && a != nullptr && a->idle()) missed.push_back(e.first);
            continue;
        }
        if (a != nullptr && a->idle() && may_order(a->id, now)) {
            c.orders.pick_up(a->id, e.second);
            ordered_[a->id] = now;
        }
    }
    for (const uint32_t ant : missed) {
        ++chain_misses_;
        const AntView* a = find_ant(v.mine(), ant);
        if (a != nullptr) strand(*a);
        else end_chain(ant);
        row_ant_.erase(ant);
        row_before_.erase(ant);
        if (++miss_run_ >= params_.chain_misses_max) timed_off_ = true;
    }
    // the crew that waits on the island walks to the park tile meanwhile (one order a look), so that the next ant is there when the tunnel is free; an ant that stands idle in the throat
    // (the first steps from the entrance, one tile wide on ISLANDS) while the tunnel is in use shuts the way of the ant that walks out, and goes to the park tile as well
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(v.mine(), ant);
        if (a == nullptr || c.map.ant_component(c.seat, a->tile) != target || row_ant_.count(ant) != 0 || ant == stage_ant_ || !a->idle() || !may_order(ant, now)) continue;
        const bool near = a->tile.chebyshev_dist(park) <= 2;
        const bool throat = st.steps_of(grid, a->tile) < Stations::kThroat;
        if (near && !throat) continue;
        if (!near || tunnel_busy) {
            c.orders.move({ant}, park);
            ordered_[ant] = now;
            break;
        }
    }
    if (tunnel_busy) return;
    // the next token: the first one that lies in the row, taken by the crew ant on the island that is nearest to the entrance. Timed: the next Swimmer behind hats (a chain from the tile in front of
    // the row, by the ant that was sent there), else the first token as before
    std::vector<ChainStep> steps;
    sim::TileCoord goal{-1, -1};
    uint32_t have = 0;
    for (const AntView& a : v.mine()) have += a.type == sim::AntType::Swimmer ? 1u : 0u;
    if (timed && have >= swimmers_wanted()) return;                                                  // (the last Swimmer is out: the task is over with it)
    const bool chain = timed && timed_on() && steps_in(c, entrance.tile, steps, &goal);
    sim::TileCoord first{-1, -1};
    for (const sim::TileCoord t : row_) {
        if (v.powerup_at(t) == nullptr) continue;
        first = t;
        break;
    }
    if (first.x < 0) return;
    // a typed ant leaves its old power-up on a free tile beside the token, which in the row is the way back and shuts it: a Bomber of the crew (it flew on when the crew was one ant short) takes the
    // last token that is needed and no other, so that the expedition is over with it
    const bool bomber_may = !chain && tokens_needed(v) == 1u;
    const AntView* taker = nullptr;
    int32_t best = 1 << 20;
    for (const uint32_t ant : crew_) {
        const AntView* a = find_ant(v.mine(), ant);
        const bool plain = a != nullptr && a->type == v.default_ant_type();
        if (a == nullptr || c.map.ant_component(c.seat, a->tile) != target || !(plain || (bomber_may && a->type == sim::AntType::Bomber)) || !a->takes_orders() || c.ledger.owner(ant) != id()) continue;
        // (a plain ant goes before a Bomber that is there; the one that was sent to the entrance stays the one. Timed: the ant that is nearest by the steps of a walk, for an ant that stands in a
        // dead end behind another cannot get out, which the distance in a straight line does not tell)
        const int32_t near = params_.timed_row ? st.steps_of(grid, a->tile) * 16 : 0;
        const int32_t d = near + a->tile.chebyshev_dist(entrance.tile) + (plain ? 0 : 1000) - (ant == stage_ant_ ? 500 : 0);
        if (d < best) {
            best = d;
            taker = a;
        }
    }
    if (taker == nullptr || !may_order(taker->id, now)) return;
    if (chain) {
        if (taker->tile != entrance.tile) {                                                          // to the tile in front of the row first: the model counts from an ant at rest there
            if (taker->idle()) {
                c.orders.move({taker->id}, entrance.tile);
                ordered_[taker->id] = now;
                stage_ant_ = taker->id;
                stage_since_ = now;
            }
            return;
        }
        if (!taker->idle() || taker->state != sim::UnitState::Idle) return;                           // (it has arrived: the look after it stands)
        c.orders.chain(taker->id, steps);
        start_chain(taker->id, now);
        ordered_[taker->id] = now;
        row_ant_[taker->id] = goal;
        row_before_[taker->id] = taker->type;
        stage_ant_ = 0;
        ++chains_in_;
        return;
    }
    c.orders.pick_up(taker->id, first);
    ordered_[taker->id] = now;
    row_ant_[taker->id] = first;
    row_before_[taker->id] = taker->type;
}

// ---- every look -------------------------------------------------------------------------------------------------------------------------------------

void ExpeditionTask::step(TaskContext& c) {
    const BotView& v = c.view;
    if (islands_ == nullptr || !islands_->active() || !v.has_grid()) return;
    const uint64_t now = v.tick();
    if (hill_comp_ < 0) hill_comp_ = c.map.hill_component(c.seat);
    if (hill_comp_ < 0) return;
    gap_ = islands_->latency() + 20u;
    uint32_t have = 0;
    for (const AntView& a : v.mine()) have += a.type == sim::AntType::Swimmer ? 1u : 0u;
    const uint32_t want = swimmers_wanted();
    for (auto it = crew_.begin(); it != crew_.end();) {                                              // an ant that died, or that a task of a higher rank took
        if (find_ant(v.mine(), *it) == nullptr || c.ledger.owner(*it) != id()) it = crew_.erase(it);
        else ++it;
    }
    if (route_.empty()) {
        if (now < retry_at_ || have >= want || v.ticks_left() < params_.min_ticks_left) return;
        if (!plan(c)) {
            retry_at_ = now + 200u;
            return;
        }
    }
    note_taken(c);                                                                                   // (before the end is looked at: the last token taken counts)
    bool swimmer_left = false;
    for (const sim::TileCoord t : row_) {
        const PowerUpView* p = v.powerup_at(t);
        swimmer_left = swimmer_left || (p != nullptr && p->kind == sim::AntType::Swimmer);
    }
    const bool exiting = exit_chains() && !done_.empty();                                             // (timed: a Swimmer that is on its way out over the hats is not left alone)
    const bool finished = (have >= want || !swimmer_left) && !exiting;
    const bool lost = crew_.empty() && row_ant_.empty() && !exiting;
    const uint32_t patience = row_ant_.empty() && crew_hopeless(c) ? std::min(params_.hopeless_ticks, params_.stuck_ticks) : params_.stuck_ticks;
    if (finished || lost || now > progress_ + patience) {
        if (!finished) ++given_up_;
        giveups_in_a_row_ = finished ? 0u : giveups_in_a_row_ + 1u;
        release_all(c);
        // an attempt that came to nothing is made again after retry_ticks, the next one after twice that, then four and eight times (and then at that rate): a row that nothing reaches (a stranger keeps
        // the shore tile, no flight has the station tile that it needs) does not hold the crew and a Bomber for two thirds of the match
        retry_at_ = now + (finished ? 0u : pause_after(params_.retry_ticks, giveups_in_a_row_));
        return;
    }
    for (const uint32_t ant : crew_) {                                                               // the landings: a crew ant that stands on a later island of the route
        const AntView* a = find_ant(v.mine(), ant);
        if (a == nullptr) continue;
        const int32_t comp = c.map.ant_component(c.seat, a->tile);
        for (size_t j = 1; j < route_.size(); ++j) {
            if (comp != route_[j] || !landed_.insert(std::make_pair(ant, static_cast<uint32_t>(j))).second) continue;
            ++landings_;
            progress_ = now;
            if (first_landing_ == 0) first_landing_ = now;
        }
    }
    for (size_t i = legs_.size(); i-- > 0;) drive_leg(c, i);
    drive_row(c);
}

}  // namespace ants::ai
