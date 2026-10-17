#pragma once

// The gate goal (docs/reverse_engineering/bomb_gates.md, "Gate goals"): what a bot whose food is walled off by fire walls or bombs sets out to do, chosen from the gate analysis (GateInfo) and the
// look at the world. It is a pure function of both: no ants are asked for, nothing is ordered, no clock. GateTask (gate_task.hpp) works the goal.
//
//   None         there is food that the hill's ants reach with fire walls and bombs closed (or no food at all, or no analysis): normal play
//   Extinguish   a pile lies behind fire walls only: a Fire ant puts out the first fire cell of the walk with the fewest of them
//   Hop          a Bomber plants a bomb on a free tile of the home side and bombs itself over the wall (GateInfo's hops, the real rule: opposite the facing, 4 tiles, clockwise retry, 2 hit points
//                and a 20 percent dud per bomb, a landing on a bomb throws again), onto a power-up of a kind the bot wants (a Swimmer, a Fire ant) or in reach of a pile
//
// The order of preference is the cost: putting a wall out (100 + 10 per fire cell), a hop to a wanted power-up (200 + 40 per bomb), a hop in reach of a pile (400 + 40 per bomb). A hop of more
// bombs than the rules allow is not planned.

#include <cstdint>
#include <vector>

#include "ants_ai/bot_view.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_sim/ant_unit.hpp"

namespace ants::ai {

enum class GoalKind : uint8_t { None, Extinguish, Hop, Defuse };

struct GateGoal {
    GoalKind kind{GoalKind::None};
    sim::AntType need{sim::AntType::Worker};     // the kind of ant the goal needs: Fire for Extinguish, Bomber for Hop/Defuse
    bool powerup{false};                         // the target is a power-up (MapInfo::powerups() index), else a pile (MapInfo::piles() index)
    uint32_t target{0};
    sim::TileCoord cell{-1, -1};                 // Extinguish: the fire cell to put out; Hop: the tile the bomb is planted on; Defuse: the bomb cell to defuse
    BombHop hop;                                 // Hop: the chain (GateInfo::hops() entry, copied)
    GateKind gate{GateKind::None};               // the gate of the target
    int32_t cost{0};                             // the lower the better
    bool active() const noexcept { return kind != GoalKind::None; }
};

struct GoalRules {
    uint32_t hop_bombs{3};                       // the most bombs of a hop chain (LevelPlan::gate_hop_bombs)
    uint8_t kinds{static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Swimmer)) | (1u << static_cast<unsigned>(sim::AntType::Fire)))};   // power-up kinds worth a hop
    bool shore_breach{false};                    // plan multi-step shore breach (Fire hat -> extinguish -> defuse -> Swimmer) for water-moated Sealed piles
    std::vector<uint32_t> field_piles;           // the piles the field breach plays (Tactics::field_piles, MapInfo::piles() indices): no goal for them
};

/// The goal for the look `view` shows, given the analysis of the map as it is (GateInfo::analyse of the current grid). GoalKind::None when the analysis is empty or some pile with units left is
/// free of every gate.
GateGoal plan_gate_goal(const GateInfo& gates, const BotView& view, const GoalRules& rules);

}  // namespace ants::ai
