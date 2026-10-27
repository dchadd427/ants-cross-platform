// The order in which a click names a big selection: nearest to the clicked tile first (client side only: the engine, the commands, the wire and the recordings are not touched).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

/// The key of the original's group order (FUN_010287b5, FUN_01020911) as the engine computes it (SimulationEngine::group_order, issue_group_attack_order): 16 x the Chebyshev
/// distance between the tile of the ant's pixel position (pixel / 32) and the clicked tile.
inline uint32_t group_order_distance(int32_t px, int32_t py, sim::TileCoord target) noexcept {
    const int32_t dr = std::abs(py / 32 - target.y);
    const int32_t dc = std::abs(px / 32 - target.x);
    return static_cast<uint32_t>(std::max(dr, dc)) << 4;
}

/// The ants of a MOVE or a group ATTACK click in the order that they are named in the commands. A command holds at most sim::kMaxCommandAnts ants and the engine orders the ants of
/// ONE command nearest first; a bigger selection is cut into several commands, so to have the nearest ants of the WHOLE selection ordered first (their goal tiles are still free), the
/// selection is sorted by the engine's key before it is cut: stable, so ants at the same distance keep their selection order. A selection of at most sim::kMaxCommandAnts is returned as it
/// is (the one command that was always sent, byte for byte). An id that the world does not hold goes last. Special orders (steal, bomb, fire, bridge) and Stop do not use this.
inline std::vector<uint32_t> nearest_first(const std::vector<uint32_t>& selection, const std::vector<sim::AntSnapshot>& world_ants, sim::TileCoord target) {
    if (selection.size() <= sim::kMaxCommandAnts) return selection;
    std::unordered_map<uint32_t, uint32_t> key_of;
    key_of.reserve(selection.size() * 2);
    for (uint32_t id : selection) key_of.emplace(id, UINT32_MAX);
    for (const sim::AntSnapshot& a : world_ants) {
        const auto it = key_of.find(a.id);
        if (it != key_of.end()) it->second = group_order_distance(a.px, a.py, target);
    }
    std::vector<std::pair<uint32_t, uint32_t>> keyed;                      // (key, id), in the selection order
    keyed.reserve(selection.size());
    for (uint32_t id : selection) keyed.emplace_back(key_of[id], id);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<uint32_t> out;
    out.reserve(keyed.size());
    for (const auto& k : keyed) out.push_back(k.second);
    return out;
}

}  // namespace ants::app
