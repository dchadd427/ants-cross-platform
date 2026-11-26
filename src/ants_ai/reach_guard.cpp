#include "ants_ai/reach_guard.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// The walker components of a walk map (8 neighbours, as the engine's search steps)
struct Labels {
    int w{0};
    int h{0};
    std::vector<int32_t> comp;
    int32_t at(int x, int y) const noexcept { return x < 0 || y < 0 || x >= w || y >= h ? -1 : comp[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
};

Labels label(const std::vector<uint8_t>& mask, int w, int h) {
    Labels l;
    l.w = w;
    l.h = h;
    l.comp.assign(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), -1);
    if (mask.size() != l.comp.size()) return l;
    int32_t labels = 0;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            if (l.comp[i] >= 0 || mask[i] == 0) continue;
            l.comp[i] = labels;
            stack.assign(1, y * w + x);
            while (!stack.empty()) {
                const int idx = stack.back();
                stack.pop_back();
                for (int k = 0; k < 8; ++k) {
                    const int nx = idx % w + kDx[k];
                    const int ny = idx / w + kDy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t n = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
                    if (mask[n] == 0 || l.comp[n] >= 0) continue;
                    l.comp[n] = labels;
                    stack.push_back(ny * w + nx);
                }
            }
            ++labels;
        }
    }
    return l;
}

// The labels of the walkable tiles on `tile` and next to it
void around(const Labels& l, sim::TileCoord tile, std::set<int32_t>& out) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int32_t c = l.at(tile.x + dx, tile.y + dy);
            if (c >= 0) out.insert(c);
        }
    }
}

}  // namespace

std::vector<sim::Command> ReachGuard::filter(const BotView& view, uint8_t seat, Orders& orders) {
    std::vector<sim::Command> gone;
    if (!view.has_grid() || view.map() == nullptr) return gone;
    std::vector<Intent>& intents = orders.edit();
    const auto walks = [](sim::CommandType t) { return t == sim::CommandType::GroupMove || t == sim::CommandType::GroupSpecial || t == sim::CommandType::GroupAttack; };
    if (std::none_of(intents.begin(), intents.end(), [&](const Intent& in) { return walks(in.command.type) && !in.command.ants.empty(); })) return gone;

    const sim::Grid& grid = view.grid();
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    if (w <= 0 || h <= 0) return gone;
    std::vector<uint8_t> mask = view.walkable_mask(seat);
    for (const PowerUpView& pu : view.powerups()) {                                           // (a power-up is solid for the planner, but a walk onto it takes it and the walk goes on: the island tasks' rows of
        if (pu.tile.x < 0 || pu.tile.y < 0 || pu.tile.x >= w || pu.tile.y >= h) continue;       // power-ups are walked over, so one never cuts a place off here)
        if (MapInfo::can_step_onto(grid, seat, pu.tile, view.walk_context())) mask[static_cast<size_t>(pu.tile.y) * static_cast<size_t>(w) + static_cast<size_t>(pu.tile.x)] = 1;
    }
    for (const BombView& b : view.bombs()) {                                                  // (a bomb of the seat or its ally blocks the walk, but the tasks that dig and hop over bombs defuse or cross their own,
        if (b.tile.x < 0 || b.tile.y < 0 || b.tile.x >= w || b.tile.y >= h) continue;           // so no bomb cuts a place off here either)
        if (MapInfo::can_step_onto(grid, seat, b.tile, view.walk_context())) mask[static_cast<size_t>(b.tile.y) * static_cast<size_t>(w) + static_cast<size_t>(b.tile.x)] = 1;
    }
    const Labels walker = label(mask, w, h);
    Labels fire;                                                                              // (a Fire Ant is not stopped by a fire wall: built when one asks)
    std::vector<uint8_t> fire_open;
    bool fire_made = false;
    const auto fire_labels = [&]() -> const Labels& {
        if (!fire_made) {
            fire_made = true;
            std::vector<uint8_t>& open = fire_open;
            open = mask;
            for (const FireWallView& f : view.fire_walls()) {
                if (f.tile.x >= 0 && f.tile.y >= 0 && f.tile.x < w && f.tile.y < h && sim::movement::terrain_walkable(grid.terrain_class_at(f.tile))) open[static_cast<size_t>(f.tile.y) * static_cast<size_t>(w) + static_cast<size_t>(f.tile.x)] = 1;
            }
            fire = label(open, w, h);
        }
        return fire;
    };

    std::unordered_map<uint32_t, const AntView*> mine;
    for (const AntView& a : view.mine()) mine[a.id] = &a;

    // The goal of a click, as the engine resolves it (movement_system.cpp adjust_goal): the tile if an ant can step onto it, else the first such tile of a ring scan, d = 1 .. 4 (west column
    // top to bottom, east column, north row, south row), so a click on water is a walk to the first shore tile of the scan. Then the labels of the walk map that the goal stands on (or, for a
    // goal that is not on it, a pile or a power-up, the labels next to it); a goal with no such label at all is label -2, which no ant has. A mound is the place where a walk home or a thief's raid ends: an own mound is reached from the queue row of the hill, another one from the ring round it. An empty set: nothing is known (no tile to walk to: the engine stops the ant without a word).
    const auto goal_labels = [&](const std::vector<uint8_t>& open, const Labels& l, sim::TileCoord target) {
        std::set<int32_t> out;
        for (const assets::AnthillSpawn& ah : grid.anthills()) {                                // a click into a mound: a walk home (an ant of the hill's own team: the queue row is where it steps in), or a thief's
            const int32_t bx = static_cast<int32_t>(ah.x);                                      // raid (any other: the ring round the mound, the engine finds its own tile)
            const int32_t by = static_cast<int32_t>(ah.y);
            if (target.x < bx || target.x > bx + 3 || target.y < by || target.y > by + 3) continue;
            if (ah.team_id == seat) {
                for (const sim::TileCoord& t : view.map()->hill(seat).starts) {
                    const int32_t c = l.at(t.x, t.y);
                    if (c >= 0) out.insert(c);
                }
            } else {
                for (int32_t y = by - 1; y <= by + 4; ++y) {
                    for (int32_t x = bx - 1; x <= bx + 4; ++x) {
                        const int32_t c = l.at(x, y);
                        if (c >= 0) out.insert(c);
                    }
                }
            }
            return out;
        }
        const auto enterable = [&](int32_t x, int32_t y, bool click) {
            if (x < 0 || y < 0 || x >= w || y >= h) return false;
            if (open[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] != 0) return true;
            if (!click) return false;
            const sim::TileCell& cell = grid.get_cell(sim::TileCoord{x, y});
            return (cell.is_food || cell.has_powerup()) && MapInfo::can_step_onto(grid, seat, sim::TileCoord{x, y}, view.walk_context());       // (the click on a pile or a power-up)
        };
        sim::TileCoord goal{-1, -1};
        if (enterable(target.x, target.y, true)) {
            goal = target;
        } else {
            const auto first = [&](int32_t x, int32_t y) {
                if (goal.x >= 0 || !enterable(x, y, false)) return;
                goal = sim::TileCoord{x, y};
            };
            for (int32_t d = 1; d < 5 && goal.x < 0; ++d) {
                if (target.x - d >= 0) {
                    for (int32_t y = std::max(0, target.y - d); y <= target.y + d; ++y) first(target.x - d, y);
                }
                for (int32_t y = std::max(0, target.y - d); y <= target.y + d; ++y) first(target.x + d, y);
                if (target.y - d >= 0) {
                    for (int32_t x = std::max(0, target.x - d + 1); x <= target.x + d - 1; ++x) first(x, target.y - d);
                }
                for (int32_t x = std::max(0, target.x - d + 1); x <= target.x + d - 1; ++x) first(x, target.y + d);
            }
        }
        if (goal.x < 0) return out;
        const int32_t own = l.at(goal.x, goal.y);
        if (own >= 0) out.insert(own);
        else around(l, goal, out);
        if (out.empty()) out.insert(-2);                                                        // a goal that no walk can end next to (a pile in a ring of rocks): a place that no ant is in
        return out;
    };

    const auto joined = [&](const AntView& a, const std::set<int32_t>& goal, const Labels& l) {
        std::set<int32_t> here;
        around(l, a.tile, here);
        if (here.empty() || goal.empty()) return true;                                          // not known: the engine decides
        for (const int32_t c : here) {
            if (goal.count(c) != 0) return true;
        }
        return false;
    };

    std::set<uint32_t> chains;                                                                // the chains of which a click is refused: none of their clicks stays
    std::vector<Intent> kept;
    kept.reserve(intents.size());
    for (Intent& in : intents) {
        if (!walks(in.command.type) || in.command.ants.empty() || in.cancel) {                    // (the cancel of a stand is meant to be refused: no walk leads there)
            kept.push_back(std::move(in));
            continue;
        }
        const sim::TileCoord target{in.command.tile_x, in.command.tile_y};
        std::set<int32_t> goal_walk, goal_fire;
        bool have_walk = false, have_fire = false;
        std::vector<uint32_t> stay, go_not;
        for (const uint32_t id : in.command.ants) {
            const auto it = mine.find(id);
            if (it == mine.end() || it->second->type == sim::AntType::Swimmer) {
                stay.push_back(id);
                continue;
            }
            const bool fire_ant = it->second->type == sim::AntType::Fire;
            const Labels& l = fire_ant ? fire_labels() : walker;
            std::set<int32_t>& goal = fire_ant ? goal_fire : goal_walk;
            bool& have = fire_ant ? have_fire : have_walk;
            if (!have) {
                goal = goal_labels(fire_ant ? fire_open : mask, l, target);
                have = true;
            }
            (joined(*it->second, goal, l) ? stay : go_not).push_back(id);
        }
        if (go_not.empty()) {
            kept.push_back(std::move(in));
            continue;
        }
        sim::Command refused = in.command;
        refused.ants = go_not;
        gone.push_back(std::move(refused));
        if (in.chain != 0) {
            chains.insert(in.chain);
            kept.push_back(std::move(in));                                                     // (taken out below, with the other steps of its chain)
            continue;
        }
        if (stay.empty()) continue;
        in.command.ants = stay;
        kept.push_back(std::move(in));
    }
    if (!chains.empty()) {
        std::vector<Intent> rest;
        rest.reserve(kept.size());
        for (Intent& in : kept) {
            if (in.chain != 0 && chains.count(in.chain) != 0) {
                bool reported = false;
                for (const sim::Command& g : gone) reported = reported || (g.tile_x == in.command.tile_x && g.tile_y == in.command.tile_y && !g.ants.empty() && g.ants[0] == in.command.ants[0]);
                if (!reported) gone.push_back(in.command);
                continue;
            }
            rest.push_back(std::move(in));
        }
        kept.swap(rest);
    }
    intents.swap(kept);
    return gone;
}

}  // namespace ants::ai
