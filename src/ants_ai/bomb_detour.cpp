#include "ants_ai/bomb_detour.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>

#include "ants_ai/tactics.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr uint32_t kBlocked = static_cast<uint32_t>(MapInfo::kPathCostLimit);   // the engine's cost of a blocked step
constexpr uint64_t kOrderMemory = 1800;                                           // a remembered order older than this (ticks) is forgotten
constexpr uint64_t kPileMemory = 7200;                                            // ... except a walk to a pile: the engine repeats a harvest on its own, so the ant goes on walking to the pile it was sent to
constexpr int kMaxCandidates = 10;                                                // waypoint candidates tried for one detour
constexpr uint64_t kRecheckTicks = 24;                                            // a walk that is under way is looked at again this often (the engine's repeat of a harvest walks on its own)
constexpr uint64_t kIdleForget = 120;                                             // an ant that stands still this long is no longer on the order that it was given

bool walking_type(sim::CommandType t) noexcept { return t == sim::CommandType::GroupMove || t == sim::CommandType::GroupSpecial || t == sim::CommandType::GroupAttack; }

// The orders a level goes round bombs with: level 1 (Medium) the walks of the economy, the power-up trips and the fights (moves and attacks of every ant but a Thief), level 2 (Hard) every
// walking order (a raid, a mine trip) of every ant. A Swimmer swims (its way is water): left to the island tasks.
bool eligible(const LevelPlan& plan, sim::CommandType type, sim::AntType ant) noexcept {
    if (ant == sim::AntType::Swimmer) return false;
    if (plan.bomb_detour >= 2) return walking_type(type);
    if (plan.bomb_detour == 0 || ant == sim::AntType::Thief) return false;
    return type == sim::CommandType::GroupMove || type == sim::CommandType::GroupAttack;
}

sim::Command make_command(sim::CommandType type, sim::TileCoord tile, const std::vector<uint32_t>& ants) {
    sim::Command c;
    c.type = type;
    c.tile_x = static_cast<int16_t>(tile.x);
    c.tile_y = static_cast<int16_t>(tile.y);
    c.ants = ants;
    return c;
}

// An intent that names more ants than a command may (sim::kMaxCommandAnts) goes out as several, with the same priority and pick-up mark: the controller refuses a command that names too many whole
void push_chunked(std::vector<Intent>& into, Intent in) {
    const size_t cap = sim::kMaxCommandAnts;
    if (in.command.ants.size() <= cap) {
        into.push_back(std::move(in));
        return;
    }
    const std::vector<uint32_t> all = in.command.ants;
    for (size_t i = 0; i < all.size(); i += cap) {
        Intent part = in;
        part.command.ants.assign(all.begin() + static_cast<std::ptrdiff_t>(i), all.begin() + static_cast<std::ptrdiff_t>(std::min(all.size(), i + cap)));
        into.push_back(std::move(part));
    }
}


// The world of one look: the walk maps, the bombs of other teams and who stands where
struct DetourWorld {
    const BotView& view;
    const sim::Grid& grid;
    sim::PathGridPool& pool;
    uint8_t seat;
    int w;
    int h;
    std::vector<uint8_t> walk;
    std::vector<uint8_t> fire;
    bool fire_made{false};
    std::vector<uint8_t> bomb;               // 1 on the tile of a bomb of another team
    std::vector<sim::TileCoord> bomb_tiles;
    std::vector<uint8_t> occupied;           // 1 on a tile that an ant stands on
    std::vector<uint8_t> blocking;           // 1 on a tile that the engine's search does not enter for an ant that moves: an ant of another team, an ally, a team-mate that stands still

    DetourWorld(const BotView& v, sim::PathGridPool& p) : view(v), grid(v.grid()), pool(p), seat(v.seat()), w(static_cast<int>(v.grid().width())), h(static_cast<int>(v.grid().height())) {
        walk = MapInfo::walkable_mask(grid, seat, view.walk_context());
        const size_t n = static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0));
        bomb.assign(n, 0);
        occupied.assign(n, 0);
        blocking.assign(n, 0);
        const uint8_t ally = view.ally();
        for (const BombView& b : view.bombs()) {
            if (!inside(b.tile) || b.owner == seat || (ally < sim::MAX_PLAYERS && b.owner == ally)) continue;
            bomb[idx(b.tile)] = 1;
            bomb_tiles.push_back(b.tile);
        }
        for (const AntView& a : view.mine()) {
            if (!inside(a.tile)) continue;
            occupied[idx(a.tile)] = 1;
            switch (a.state) {                                                           // (movement_system.cpp step_cost: a team-mate that waits blocks the search)
                case sim::UnitState::Idle: case sim::UnitState::GuardIdle: case sim::UnitState::Stunned: case sim::UnitState::PoweringUp: case sim::UnitState::PlantingBomb:
                case sim::UnitState::DefusingBomb: case sim::UnitState::PlacingFire: case sim::UnitState::ExtinguishingFire: case sim::UnitState::HarvestingFood:
                    blocking[idx(a.tile)] = 1;
                    break;
                default:
                    break;
            }
        }
        for (const AntView& a : view.others()) {
            if (!inside(a.tile)) continue;
            occupied[idx(a.tile)] = 1;
            blocking[idx(a.tile)] = 1;
        }
    }
    bool inside(sim::TileCoord t) const noexcept { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h; }
    size_t idx(sim::TileCoord t) const noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x); }
    // the walk map of an ant: a Fire Ant also walks over fire walls
    const std::vector<uint8_t>& mask(bool fire_ant) {
        if (!fire_ant) return walk;
        if (!fire_made) {
            fire_made = true;
            fire = walk;
            for (const FireWallView& f : view.fire_walls()) {
                if (inside(f.tile) && sim::movement::terrain_walkable(grid.terrain_class_at(f.tile))) fire[idx(f.tile)] = 1;
            }
        }
        return fire;
    }
    // whether a bomb of another team lies in the box of a walk (with a margin: the walk bends round what is in the way)
    bool bomb_near(sim::TileCoord a, sim::TileCoord b) const noexcept {
        const int margin = 12 + std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)) / 4;
        const int x0 = std::min(a.x, b.x) - margin, x1 = std::max(a.x, b.x) + margin;
        const int y0 = std::min(a.y, b.y) - margin, y1 = std::max(a.y, b.y) + margin;
        for (const sim::TileCoord& t : bomb_tiles) {
            if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1) return true;
        }
        return false;
    }
};


// The goal the engine resolves for a click (go_to, adjust_goal, approach_tile): an own hill is a walk to the queue row, a hill of another team a raid for a Thief (the raid tile) and nothing for
// the rest, a special order a walk to the first of the four sides of the tile that the ant may enter and that is nearest to it, any other click the tile itself (a pile or a power-up too) or the
// first tile of the ring scan round it. `goal_ok` says the goal is entered although the walk map does not (a pile, a power-up, the raid tile). False when there is no walk to plan.
bool resolve_goal(DetourWorld& c, const AntView& a, sim::CommandType type, sim::TileCoord target, sim::TileCoord& goal, bool& goal_ok) {
    const std::vector<uint8_t>& mask = c.mask(a.type == sim::AntType::Fire);
    goal_ok = false;
    if (!c.inside(target)) return false;
    for (const assets::AnthillSpawn& ah : c.grid.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (target.x < bx || target.x > bx + 3 || target.y < by || target.y > by + 3) continue;
        if (ah.team_id == c.seat) {
            if (c.view.map() == nullptr) return false;
            int32_t best = -1;
            for (const sim::TileCoord& s : c.view.map()->hill(c.seat).starts) {
                const int32_t d = a.tile.chebyshev_dist(s);
                if (best < 0 || d < best) {
                    best = d;
                    goal = s;
                }
            }
            return best >= 0;
        }
        if (a.type != sim::AntType::Thief || type == sim::CommandType::GroupAttack) return false;
        goal = sim::TileCoord{bx + 3, by + 2};
        goal_ok = true;
        return true;
    }
    const auto enterable = [&](sim::TileCoord t) { return c.inside(t) && mask[c.idx(t)] != 0; };
    if (type == sim::CommandType::GroupSpecial) {
        static const int kSideX[4] = {0, 0, -1, 1};
        static const int kSideY[4] = {-1, 1, 0, 0};
        int32_t best = -1;
        for (int k = 0; k < 4; ++k) {
            const sim::TileCoord n{target.x + kSideX[k], target.y + kSideY[k]};
            if (!enterable(n)) continue;
            const int32_t d = a.tile.chebyshev_dist(n);
            if (best < 0 || d < best) {
                best = d;
                goal = n;
            }
        }
        return best >= 0;
    }
    if (enterable(target)) {
        goal = target;
        return true;
    }
    if (MapInfo::can_step_onto(c.grid, c.seat, target, c.view.walk_context())) {
        const sim::TileCell& cell = c.grid.get_cell(target);
        if (cell.is_food || cell.has_powerup()) {                                          // a pile or a power-up: the walk ends ON the tile
            goal = target;
            goal_ok = true;
            return true;
        }
    }
    for (int32_t d = 1; d < 5; ++d) {                                                      // the engine's ring scan (adjust_goal)
        const auto first = [&](int32_t x, int32_t y) {
            if (!enterable(sim::TileCoord{x, y})) return false;
            goal = sim::TileCoord{x, y};
            return true;
        };
        if (target.x - d >= 0) {
            for (int32_t y = std::max(0, target.y - d); y <= target.y + d; ++y) {
                if (first(target.x - d, y)) return true;
            }
        }
        for (int32_t y = std::max(0, target.y - d); y <= target.y + d; ++y) {
            if (first(target.x + d, y)) return true;
        }
        if (target.y - d >= 0) {
            for (int32_t x = std::max(0, target.x - d + 1); x <= target.x + d - 1; ++x) {
                if (first(x, target.y - d)) return true;
            }
        }
        for (int32_t x = std::max(0, target.x - d + 1); x <= target.x + d - 1; ++x) {
            if (first(x, target.y + d)) return true;
        }
    }
    return false;
}

// The engine's own search (the port of its A*) from `from` to `goal` on the map as it is now; with `safe` the bombs of other teams are walls. Empty when there is no walk.
std::vector<sim::TileCoord> search(DetourWorld& c, bool fire_ant, sim::TileCoord from, sim::TileCoord goal, bool goal_ok, bool safe) {
    const std::vector<uint8_t>& mask = c.mask(fire_ant);
    const sim::PathSearch::StepCostFn cost = [&](sim::TileCoord a, sim::TileCoord b) -> uint32_t {
        if (!c.inside(b)) return kBlocked;
        if (b == goal && goal_ok) return MapInfo::step_cost(c.grid, a, b);
        if (mask[c.idx(b)] == 0) return kBlocked;
        if (safe && c.bomb[c.idx(b)] != 0) return kBlocked;
        if (c.blocking[c.idx(b)] != 0 && !(b == goal)) return kBlocked;
        return MapInfo::step_cost(c.grid, a, b);
    };
    sim::PathSearch s(0, from, goal, static_cast<uint16_t>(c.h), static_cast<uint16_t>(c.w), c.pool, c.grid.rules() == 0, c.grid.rules() == 2);
    for (int i = 0; i < 100 && !s.finished(); ++i) {
        if (s.step(sim::PathSearch::kDefaultBudget, cost) == 0) break;
    }
    if (!s.finished()) return {};
    return s.path();
}

bool crosses(const DetourWorld& c, const std::vector<sim::TileCoord>& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (c.bomb[c.idx(path[i])] != 0) return true;
    }
    return false;
}

int64_t path_cost(const DetourWorld& c, const std::vector<sim::TileCoord>& path) {
    int64_t total = 0;
    for (size_t i = 1; i < path.size(); ++i) total += MapInfo::step_cost(c.grid, path[i - 1], path[i]);
    return total;
}

enum class Verdict : uint8_t { Clear, Detour, TooLong };

// Whether the walk of `a` to `target` crosses a bomb of another team, and where to send it first. `waypoint` is set for Detour only.
Verdict decide(DetourWorld& c, const AntView& a, sim::CommandType type, sim::TileCoord target, uint32_t extra_percent, sim::TileCoord& waypoint) {
    if (c.bomb_tiles.empty() || !c.inside(a.tile) || !c.inside(target)) return Verdict::Clear;
    if (c.bomb[c.idx(target)] != 0) return Verdict::Clear;                              // the click names a bomb: a set-off or a defuse on purpose (BombTask)
    if (!c.bomb_near(a.tile, target)) return Verdict::Clear;
    sim::TileCoord goal{};
    bool goal_ok = false;
    if (!resolve_goal(c, a, type, target, goal, goal_ok)) return Verdict::Clear;
    if (c.bomb[c.idx(goal)] != 0 || goal == a.tile) return Verdict::Clear;
    const bool fire_ant = a.type == sim::AntType::Fire;
    const std::vector<sim::TileCoord> all = search(c, fire_ant, a.tile, goal, goal_ok, false);
    if (all.size() < 2) return Verdict::Clear;
    if (!crosses(c, all)) return Verdict::Clear;
    const std::vector<sim::TileCoord> safe = search(c, fire_ant, a.tile, goal, goal_ok, true);
    if (safe.size() < 3) return Verdict::Clear;                                         // walled in (or a goal next to the bomb): no walk round, the order goes as it is
    const int64_t cost_all = path_cost(c, all);
    const int64_t cost_safe = path_cost(c, safe);
    if (cost_safe - cost_all > std::max<int64_t>(cost_all * extra_percent / 100, 120)) return Verdict::TooLong;
    // the farthest tile of the safe walk that the engine's own walk reaches without crossing a bomb (tried from the far end, then nearer)
    const int last = static_cast<int>(safe.size()) - 2;                                 // (the goal itself is no waypoint)
    const int stride = std::max(1, last / 6);
    int tried = 0;
    for (int i = last; i >= 1 && tried < kMaxCandidates; i -= (i > stride ? stride : 1), ++tried) {
        const sim::TileCoord w = safe[static_cast<size_t>(i)];
        if (c.occupied[c.idx(w)] != 0 || c.view.powerup_at(w) != nullptr || c.bomb[c.idx(w)] != 0) continue;
        if (w == goal) continue;
        const std::vector<sim::TileCoord> leg = search(c, fire_ant, a.tile, w, false, false);
        if (leg.size() < 2 || crosses(c, leg)) continue;
        waypoint = w;
        return Verdict::Detour;
    }
    return Verdict::Clear;
}

// Whether the order that an ant was given still has a target to walk to: a pile with food left (the walk of an ant on the engine's repeat of a harvest), a power-up that lies there; an order
// that is a few hundred ticks old (an attack, a special order) is what the ant does for that long
bool order_alive(const BotView& view, const sim::CommandType type, sim::TileCoord tile, bool pickup, uint64_t since, uint64_t now) {
    if (type != sim::CommandType::GroupMove) return now < since + kOrderMemory;
    if (pickup) return view.powerup_at(tile) != nullptr;
    const MapInfo* map = view.map();
    if (map == nullptr) return false;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map->pile(p.index);
        if (info == nullptr || p.remaining == 0) continue;
        if (tile.chebyshev_dist(p.anchor) <= 2) return true;
        for (const sim::TileCoord& cell : info->cells) {
            if (cell == tile) return true;
        }
    }
    return now < since + 400;                                                              // (a move to anything else: as an order that was given lately)
}

// Whether the controller would let the order through now (BotController::allowed): an attack needs an enemy ant on the tile, a special order a tile with no ant, a move onto a power-up is a
// planned pick-up only, and no click names the own hill where it cannot be entered. An order that would be refused is not given again: the task that gave it proposes what fits at its next look.
bool still_clickable(const BotView& view, sim::CommandType type, sim::TileCoord tile, bool pickup) {
    if (!view.has_grid() || !view.grid().in_bounds(tile)) return false;
    const sim::Grid& grid = view.grid();
    for (const assets::AnthillSpawn& hill : grid.anthills()) {
        const bool inside = tile.x >= static_cast<int32_t>(hill.x) && tile.x <= static_cast<int32_t>(hill.x) + 3 && tile.y >= static_cast<int32_t>(hill.y) && tile.y <= static_cast<int32_t>(hill.y) + 3;
        if (inside && hill.team_id == view.seat() && !view.hill_enterable()) return false;
        if (inside && type == sim::CommandType::GroupAttack) return false;
    }
    const bool powerup = grid.has_powerup_at(tile);
    if (powerup != pickup) return false;
    if (type == sim::CommandType::GroupSpecial) {
        for (const AntView& a : view.mine()) {
            if (a.tile == tile) return false;
        }
        for (const AntView& a : view.others()) {
            if (a.tile == tile) return false;
        }
    }
    if (type == sim::CommandType::GroupAttack) {
        bool foreign = false;
        for (const AntView& a : view.others()) {
            if (a.tile == tile && !(view.ally() < sim::MAX_PLAYERS && a.team == view.ally())) foreign = true;
        }
        if (!foreign) return false;
    }
    return true;
}

}  // namespace

bool BombDetour::approach_crosses(const BotView& view, const AntView& ant, sim::TileCoord bomb) {
    if (!view.has_grid() || !view.grid().in_bounds(ant.tile) || ant.type == sim::AntType::Swimmer) return false;
    sim::PathGridPool pool;
    DetourWorld world(view, pool);
    if (world.bomb_tiles.empty() || !world.inside(bomb)) return false;
    sim::TileCoord goal{};
    bool goal_ok = false;
    if (!resolve_goal(world, ant, sim::CommandType::GroupSpecial, bomb, goal, goal_ok) || goal == ant.tile) return false;
    const std::vector<sim::TileCoord> walk = search(world, ant.type == sim::AntType::Fire, ant.tile, goal, goal_ok, false);
    return walk.size() >= 2 && crosses(world, walk);
}

void BombDetour::apply(const BotView& view, const LevelPlan& plan, const Profile& profile, Orders& orders) {
    if (plan.bomb_detour == 0 || !view.has_grid() || view.map() == nullptr) {
        finals_.clear();
        vias_.clear();
        carrier_seen_.clear();
        return;
    }
    const uint64_t now = view.tick();
    const sim::Grid& grid = view.grid();
    // the bombs of other teams, and whether one appeared since the last look
    std::set<uint32_t> bombs_now;
    {
        const uint8_t seat = view.seat();
        const uint8_t ally = view.ally();
        const int32_t w = static_cast<int32_t>(grid.width());
        for (const BombView& b : view.bombs()) {
            if (b.owner == seat || (ally < sim::MAX_PLAYERS && b.owner == ally) || !grid.in_bounds(b.tile)) continue;
            bombs_now.insert(static_cast<uint32_t>(b.tile.y * w + b.tile.x));
        }
    }
    bool grew = false;
    for (const uint32_t u : bombs_now) {
        if (bombs_.count(u) == 0) {
            grew = true;
            break;
        }
    }
    if (grew) ++generation_;
    bombs_ = std::move(bombs_now);
    for (size_t i = 0; i < vias_.size();) {
        if (vias_[i].tick + kOrderMemory < now) vias_.erase(vias_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    std::unordered_map<uint32_t, const AntView*> mine;
    for (const AntView& a : view.mine()) mine[a.id] = &a;

    std::unique_ptr<DetourWorld> world;
    const auto get_world = [&]() -> DetourWorld* {
        if (bombs_.empty()) return nullptr;
        if (!world) world = std::make_unique<DetourWorld>(view, pool_);
        return world.get();
    };
    std::vector<Intent>& intents = orders.edit();
    std::set<uint32_t> named;                                                             // the ants that an order of this look names
    std::vector<Intent> result;
    result.reserve(intents.size() + 2);
    std::vector<Intent> extra;                                                            // the orders that this class adds
    const auto add_waypoint = [&](std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>>& go, Priority priority, const sim::Command& original, bool pickup, std::vector<Intent>& into) {
        for (const auto& g : go) {
            const sim::TileCoord via{g.first.first, g.first.second};
            Intent in;
            in.command = make_command(sim::CommandType::GroupMove, via, g.second);
            in.priority = priority;
            push_chunked(into, std::move(in));
            Via v;
            v.ants = g.second;
            v.via = via;
            v.original = original;
            v.pickup = pickup;
            v.tick = now;
            vias_.push_back(std::move(v));
            detours_ += static_cast<uint32_t>(g.second.size());
        }
    };

    // 1. the orders of this look
    for (Intent& in : intents) {
        if (!walking_type(in.command.type) || in.command.ants.empty() || in.chain != 0) {
            result.push_back(std::move(in));
            continue;
        }
        const sim::TileCoord target{in.command.tile_x, in.command.tile_y};
        std::vector<uint32_t> stay;
        std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> go;
        for (const uint32_t id : in.command.ants) {
            const auto it = mine.find(id);
            if (it == mine.end()) {
                stay.push_back(id);
                continue;
            }
            const AntView& a = *it->second;
            named.insert(id);
            Final f;
            f.type = in.command.type;
            f.tile = target;
            f.pickup = in.pickup;
            f.priority = in.priority;
            f.since = now;
            bool sent_round = false;
            if (eligible(plan, in.command.type, a.type)) {
                if (DetourWorld* c = get_world()) {
                    sim::TileCoord w{};
                    const Verdict v = decide(*c, a, in.command.type, target, plan.detour_extra_percent, w);
                    if (v == Verdict::Detour) {
                        sent_round = true;
                        go[{w.x, w.y}].push_back(id);
                        f.via = w;
                        f.legs = 1;
                    } else if (v == Verdict::TooLong) {
                        ++refused_long_;
                    }
                }
            }
            finals_[id] = f;
            if (!sent_round) stay.push_back(id);
        }
        if (go.empty()) {
            result.push_back(std::move(in));
            continue;
        }
        sim::Command original = in.command;
        if (!stay.empty()) {
            Intent rest = in;
            rest.command.ants = stay;
            push_chunked(result, std::move(rest));
        }
        add_waypoint(go, in.priority, original, in.pickup, result);
    }

    // 2. the ants that no order of this look names: the waypoint reached (the order is given again), a bomb that appeared on a walk that is under way, the carrier on its way home
    const uint64_t lead = std::max<uint64_t>(1u, (profile.reaction_delay + profile.decision_interval / 2u) / 9u);     // tiles before the waypoint at which the order that follows is given: it leaves after the reaction time
    std::map<std::tuple<int32_t, int32_t, int, int>, std::pair<sim::Command, std::vector<uint32_t>>> goes;
    std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> go2;
    std::map<std::pair<int32_t, int32_t>, std::pair<sim::Command, bool>> original2;
    for (auto it = finals_.begin(); it != finals_.end();) {
        const uint32_t id = it->first;
        Final& f = it->second;
        const auto ai = mine.find(id);
        const bool pile_walk = f.type == sim::CommandType::GroupMove && !f.pickup;
        if (ai == mine.end() || f.since + (pile_walk ? kPileMemory : kOrderMemory) < now) {
            it = finals_.erase(it);
            continue;
        }
        const AntView& a = *ai->second;
        if (named.count(id) != 0) {
            ++it;
            continue;
        }
        if (f.via.x < 0) {
            if (a.idle()) {                                                                  // standing still: after a while (the hill's queue, a pile that is eaten) the order is no longer what the ant does
                if (f.idle_since == 0) f.idle_since = now;
                if (now > f.idle_since + kIdleForget) {
                    it = finals_.erase(it);
                    continue;
                }
            } else {
                f.idle_since = 0;
            }
            const bool carrying = a.holding || a.carried_points > 0;
            const bool due = grew || now >= f.checked + kRecheckTicks;
            if (due && !carrying && eligible(plan, f.type, a.type) && a.state == sim::UnitState::Walking && order_alive(view, f.type, f.tile, f.pickup, f.since, now)) {
                f.checked = now;
                if (DetourWorld* c = get_world()) {
                    sim::TileCoord w{};
                    const Verdict rv = decide(*c, a, f.type, f.tile, plan.detour_extra_percent, w);
                    if (rv == Verdict::Detour) {
                        sim::Command original = make_command(f.type, f.tile, {id});
                        original2[{w.x, w.y}] = {original, f.pickup};
                        go2[{w.x, w.y}].push_back(id);
                        f.via = w;
                        f.sent = 0;
                        f.since = now;
                        f.legs = 1;
                    }
                }
            }
            ++it;
            continue;
        }
        // an ant on its way to a waypoint (the order to it has to have left first)
        if (f.sent == 0) {
            if (now > f.since + 200) f.via = sim::TileCoord{-1, -1};                             // it never left and nobody said so: forgotten
            ++it;
            continue;
        }
        const bool stopped = a.idle() && now >= f.sent + 6;
        if (a.tile.chebyshev_dist(f.via) > static_cast<int32_t>(lead) && !stopped) {
            ++it;
            continue;
        }
        sim::TileCoord w{};
        Verdict v = Verdict::Clear;
        if (f.legs < kMaxLegs && eligible(plan, f.type, a.type)) {
            if (DetourWorld* c = get_world()) v = decide(*c, a, f.type, f.tile, plan.detour_extra_percent, w);
        }
        if (v == Verdict::Detour && !(w == f.via && !stopped)) {
            sim::Command original = make_command(f.type, f.tile, {id});
            original2[{w.x, w.y}] = {original, f.pickup};
            go2[{w.x, w.y}].push_back(id);
            f.via = w;
            f.sent = 0;
            f.since = now;
            ++f.legs;
            ++it;
            continue;
        }
        if (v == Verdict::Detour) {                                                          // the next leg is the one the ant is on: let it walk
            ++it;
            continue;
        }
        // the walk to the goal crosses nothing (or no walk round is left): the order the task gave goes again
        if (!still_clickable(view, f.type, f.tile, f.pickup)) {
            f.via = sim::TileCoord{-1, -1};
            f.sent = 0;
            ++it;
            continue;
        }
        ++reissued_;
        Intent in;
        in.command = make_command(f.type, f.tile, {id});
        in.priority = f.priority;
        in.pickup = f.pickup;
        if (f.type == sim::CommandType::GroupSpecial || f.pickup) {
            extra.push_back(std::move(in));
        } else {
            auto& g = goes[std::make_tuple(f.tile.x, f.tile.y, static_cast<int>(f.type), static_cast<int>(f.priority))];
            if (g.second.empty()) g.first = in.command;
            g.second.push_back(id);
        }
        f.via = sim::TileCoord{-1, -1};
        f.sent = 0;
        f.since = now;
        ++it;
    }

    // 3. the carriers that walk home (Hard): the engine's own walk home crosses a bomb that is seen now
    if (plan.bomb_detour >= 2 && !bombs_.empty() && view.hill_enterable()) {
        const HillInfo& hill = view.map()->hill(view.seat());
        if (hill.present && hill.entrance.x >= 0) {
            for (const AntView& a : view.mine()) {
                if (a.state != sim::UnitState::Walking || !(a.holding || a.carried_points > 0) || a.type == sim::AntType::Swimmer) continue;
                if (named.count(a.id) != 0) continue;
                const auto fi = finals_.find(a.id);
                if (fi != finals_.end() && fi->second.via.x >= 0) continue;
                const auto cs = carrier_seen_.find(a.id);
                if (cs != carrier_seen_.end() && cs->second == generation_) continue;
                carrier_seen_[a.id] = generation_;
                if (DetourWorld* c = get_world()) {
                    sim::TileCoord w{};
                    if (decide(*c, a, sim::CommandType::GroupMove, hill.entrance, plan.detour_extra_percent, w) == Verdict::Detour) {
                        Final f;
                        f.type = sim::CommandType::GroupMove;
                        f.tile = hill.entrance;
                        f.priority = Priority::Normal;
                        f.since = now;
                        f.via = w;
                        f.legs = 1;
                        finals_[a.id] = f;
                        original2[{w.x, w.y}] = {make_command(sim::CommandType::GroupMove, hill.entrance, {a.id}), false};
                        go2[{w.x, w.y}].push_back(a.id);
                        ++carriers_;
                    }
                }
            }
        }
        for (auto it = carrier_seen_.begin(); it != carrier_seen_.end();) {
            const auto ai = mine.find(it->first);
            if (ai == mine.end() || !(ai->second->holding || ai->second->carried_points > 0)) it = carrier_seen_.erase(it);
            else ++it;
        }
    } else if (bombs_.empty()) {
        carrier_seen_.clear();
    }

    for (auto& g : go2) {
        const auto orig = original2.find(g.first);
        Intent in;
        in.command = make_command(sim::CommandType::GroupMove, sim::TileCoord{g.first.first, g.first.second}, g.second);
        in.priority = Priority::Normal;
        push_chunked(extra, std::move(in));
        Via v;
        v.ants = g.second;
        v.via = sim::TileCoord{g.first.first, g.first.second};
        v.original = orig != original2.end() ? orig->second.first : sim::Command{};
        v.pickup = orig != original2.end() && orig->second.second;
        v.tick = now;
        vias_.push_back(std::move(v));
        detours_ += static_cast<uint32_t>(g.second.size());
    }
    for (auto& g : goes) {
        Intent in;
        in.command = g.second.first;
        in.command.ants = g.second.second;
        in.priority = static_cast<Priority>(std::get<3>(g.first));
        push_chunked(extra, std::move(in));
    }
    for (Intent& in : extra) result.push_back(std::move(in));
    intents.swap(result);
}

void BombDetour::note(const sim::Command& sent, Bot::Fate fate, uint64_t tick) {
    if (sent.type != sim::CommandType::GroupMove || vias_.empty()) return;
    for (const Via& v : vias_) {
        if (v.via.x != sent.tile_x || v.via.y != sent.tile_y) continue;
        for (const uint32_t id : sent.ants) {
            if (std::find(v.ants.begin(), v.ants.end(), id) == v.ants.end()) continue;
            const auto it = finals_.find(id);
            if (it == finals_.end() || it->second.via != v.via || it->second.sent != 0) continue;
            if (fate == Bot::Fate::Sent) {
                it->second.sent = tick;
            } else {
                it->second.via = sim::TileCoord{-1, -1};                                          // the waypoint order never left: the ant keeps its walk
            }
        }
    }
}

std::vector<sim::Command> BombDetour::proposed(const sim::Command& sent) const {
    std::vector<sim::Command> out;
    if (sent.type != sim::CommandType::GroupMove || vias_.empty()) {
        out.push_back(sent);
        return out;
    }
    std::vector<uint32_t> rest = sent.ants;
    for (const Via& v : vias_) {
        if (v.via.x != sent.tile_x || v.via.y != sent.tile_y) continue;
        std::vector<uint32_t> hit;
        for (const uint32_t id : rest) {
            if (std::find(v.ants.begin(), v.ants.end(), id) != v.ants.end()) hit.push_back(id);
        }
        if (hit.empty()) continue;
        sim::Command original = v.original;
        original.ants = hit;
        out.push_back(std::move(original));
        rest.erase(std::remove_if(rest.begin(), rest.end(), [&](uint32_t id) { return std::find(hit.begin(), hit.end(), id) != hit.end(); }), rest.end());
    }
    if (out.empty()) {
        out.push_back(sent);
    } else if (!rest.empty()) {
        sim::Command r = sent;
        r.ants = rest;
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace ants::ai
