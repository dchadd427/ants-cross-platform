#include "ants_sim/pathfinding.hpp"
#include <queue>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ants::sim {

namespace {

struct Node {
    TileCoord pos;
    int32_t g;
    int32_t f;

    bool operator>(const Node& o) const noexcept {
        if (f != o.f) return f > o.f;
        return g < o.g; // Prefer larger g (closer to goal) for straight diagonal paths
    }
};

// Octile distance heuristic scaled for admissibility across fastest terrain (Slate: 7/10)
inline int32_t octile_heuristic(TileCoord a, TileCoord b) noexcept {
    int32_t dx = std::abs(a.x - b.x);
    int32_t dy = std::abs(a.y - b.y);
    int32_t base = 10 * (dx + dy) + (14 - 20) * std::min(dx, dy);
    return (base * 7) / 10;
}

} // anonymous namespace

TileCoord PathFinder::find_nearest_passable(
    const Grid& grid,
    TileCoord origin,
    TileCoord impassable_target,
    bool is_swimmer,
    bool is_fire_ant,
    const std::vector<TileCoord>& hard_obstacles)
{
    auto is_hard_obstacle = [&](TileCoord c) {
        for (const auto& h : hard_obstacles) {
            if (h == c) return true;
        }
        return false;
    };

    if (grid.in_bounds(impassable_target) &&
        grid.get_cell(impassable_target).is_passable(is_swimmer, is_fire_ant) &&
        !is_hard_obstacle(impassable_target)) {
        return impassable_target;
    }

    TileCoord best{-1, -1};
    int32_t min_score = INT32_MAX;

    // Search outward in Chebyshev rings up to radius 5
    for (int32_t r = 1; r <= 5; ++r) {
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                TileCoord candidate{impassable_target.x + dx, impassable_target.y + dy};
                if (!grid.in_bounds(candidate)) continue;
                if (!grid.get_cell(candidate).is_passable(is_swimmer, is_fire_ant)) continue;
                if (is_hard_obstacle(candidate)) continue;

                int32_t cd = std::max(std::abs(candidate.x - origin.x), std::abs(candidate.y - origin.y));
                int32_t ed = (candidate.x - origin.x) * (candidate.x - origin.x) +
                             (candidate.y - origin.y) * (candidate.y - origin.y);
                int32_t score = cd * 1000 + ed;
                if (score < min_score) {
                    min_score = score;
                    best = candidate;
                }
            }
        }
        if (best.x >= 0) return best;
    }

    return (grid.in_bounds(origin) && grid.get_cell(origin).is_passable(is_swimmer, is_fire_ant) && !is_hard_obstacle(origin))
               ? origin
               : impassable_target;
}

std::vector<TileCoord> PathFinder::find_path(
    const Grid& grid,
    TileCoord start,
    TileCoord target,
    bool is_swimmer,
    bool is_fire_ant,
    size_t max_nodes,
    const std::vector<TileCoord>& obstacles,
    const std::vector<TileCoord>& hard_obstacles)
{
    if (!grid.in_bounds(start)) return {};
    if (start == target) return {start};

    uint32_t w = grid.width();
    uint32_t h = grid.height();
    if (w == 0 || h == 0) return {};

    auto is_hard_obstacle = [&](TileCoord c) {
        for (const auto& h : hard_obstacles) {
            if (h == c) return true;
        }
        return false;
    };

    // If target is impassable or in hard_obstacles, find nearest passable neighbor
    TileCoord real_target = target;
    if (!grid.in_bounds(target) || !grid.get_cell(target).is_passable(is_swimmer, is_fire_ant) || is_hard_obstacle(target)) {
        real_target = find_nearest_passable(grid, start, target, is_swimmer, is_fire_ant, hard_obstacles);
        if (real_target.x < 0 || real_target == start) return {};
    }

    size_t total_cells = static_cast<size_t>(w) * h;
    std::vector<int32_t> g_score(total_cells, INT32_MAX);
    std::vector<TileCoord> came_from(total_cells, TileCoord{-1, -1});

    auto coord_to_idx = [w](TileCoord c) -> size_t {
        return static_cast<size_t>(c.y) * w + static_cast<size_t>(c.x);
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_set;

    size_t start_idx = coord_to_idx(start);
    g_score[start_idx] = 0;
    open_set.push(Node{start, 0, octile_heuristic(start, real_target)});

    // 8 movement directions: prioritize diagonals first for natural shortest paths
    static const int32_t dir_dx[8] = {  1,  1, -1, -1,  0,  1,  0, -1 };
    static const int32_t dir_dy[8] = {  1, -1,  1, -1,  1,  0, -1,  0 };
    static const int32_t dir_cost[8] = { 14, 14, 14, 14, 10, 10, 10, 10 };

    size_t visited_count = 0;
    bool found = false;

    while (!open_set.empty() && visited_count < max_nodes) {
        Node current = open_set.top();
        open_set.pop();

        if (current.pos == real_target) {
            found = true;
            break;
        }

        size_t cur_idx = coord_to_idx(current.pos);
        if (current.g > g_score[cur_idx]) continue;
        visited_count++;

        for (int i = 0; i < 8; ++i) {
            int32_t nx = current.pos.x + dir_dx[i];
            int32_t ny = current.pos.y + dir_dy[i];
            TileCoord neighbor{nx, ny};

            if (!grid.in_bounds(neighbor)) continue;
            if (!grid.get_cell(neighbor).is_passable(is_swimmer, is_fire_ant)) continue;
            if (is_hard_obstacle(neighbor)) continue;

            // Avoid dynamic obstacles (e.g. enemy units) unless destination itself
            if (neighbor != real_target && !obstacles.empty()) {
                bool is_obs = false;
                for (const auto& obs : obstacles) {
                    if (obs == neighbor) {
                        is_obs = true;
                        break;
                    }
                }
                if (is_obs) continue;
            }

            // Diagonal corner-cutting check: only block if BOTH orthogonal sides are solid obstacles
            if (dir_dx[i] != 0 && dir_dy[i] != 0) {
                TileCoord ortho1{current.pos.x + dir_dx[i], current.pos.y};
                TileCoord ortho2{current.pos.x, current.pos.y + dir_dy[i]};
                auto is_solid_corner = [&](TileCoord c) {
                    if (!grid.in_bounds(c)) return true;
                    const auto& cell = grid.get_cell(c);
                    return cell.terrain_type == TERRAIN_OBSTACLE || cell.is_obstacle_overlay;
                };
                if (is_solid_corner(ortho1) && is_solid_corner(ortho2)) {
                    continue;
                }
            }

            size_t n_idx = coord_to_idx(neighbor);
            int32_t step_cost = dir_cost[i];
            const auto& n_cell = grid.get_cell(neighbor);
            if (n_cell.surface_type == SurfaceType::Slate) {
                step_cost = (step_cost * 7) / 10; // Slate is fastest (~1.4x speed)
            } else if (n_cell.surface_type == SurfaceType::Gravel) {
                step_cost = (step_cost * 8) / 10; // Gravel is fast (~1.2x speed)
            } else if (n_cell.surface_type == SurfaceType::Mud || n_cell.is_mud || n_cell.has_completed_bridge()) {
                step_cost = (step_cost * 15) / 10; // Mud and bridges are slow (~0.65x speed)
            }
            int32_t tentative_g = current.g + step_cost;

            if (tentative_g < g_score[n_idx]) {
                g_score[n_idx] = tentative_g;
                came_from[n_idx] = current.pos;
                int32_t f = tentative_g + octile_heuristic(neighbor, real_target);
                open_set.push(Node{neighbor, tentative_g, f});
            }
        }
    }

    if (!found) {
        return {};
    }

    // Reconstruct path backward from real_target to start
    std::vector<TileCoord> path;
    TileCoord curr = real_target;
    while (curr != start) {
        path.push_back(curr);
        size_t idx = coord_to_idx(curr);
        curr = came_from[idx];
        if (curr.x < 0) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace ants::sim
