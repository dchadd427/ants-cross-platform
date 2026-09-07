#pragma once

#include "ants_sim/grid.hpp"
#include <vector>

namespace ants::sim {

/**
 * @brief 8-directional A* pathfinder for Ants grid.
 */
class PathFinder {
public:
    /**
     * @brief Computes shortest passable path from start to target.
     * If target itself is impassable, targets the nearest passable neighbor.
     * Prevents diagonal corner-cutting through solid obstacles.
     */
    static std::vector<TileCoord> find_path(
        const Grid& grid,
        TileCoord start,
        TileCoord target,
        bool is_swimmer = false,
        bool is_fire_ant = false,
        size_t max_nodes = 4000
    );

    /**
     * @brief Finds the closest passable cell to impassable_target that is reachable from origin.
     */
    static TileCoord find_nearest_passable(
        const Grid& grid,
        TileCoord origin,
        TileCoord impassable_target,
        bool is_swimmer = false,
        bool is_fire_ant = false
    );
};

} // namespace ants::sim
