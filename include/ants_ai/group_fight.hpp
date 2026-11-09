#pragma once

// The reach of the group batch (LevelPlan::group_fight; docs/BOT_PEOPLE.md, "Step 3: fight as a group"): a fight that answers a blow (defence.hpp, defend_by_strength) calls the ants that can strike from further away
// than before, so that the force at the fight is a group and not the ants that stood near. The fight itself is the defence batch's: the same target for every defender, the struck ant first, a Combat Ant before
// the nearest ant, as many as the enemy has there and one more, the ants that a job of rank 3 or 4 holds only after the free ones, and the fight kept up while the force has defend_keep_percent of the enemy's.
//
//   how far       recruit_reach    the tiles from the target within which an ant is called: group_reach (the leash does not cap it), else defend_reach capped by the leash, as before the batch
//
// It reads the plan alone; the ants that it is asked about are what a person of the seat sees (types, tiles and hit points on the view).

#include <cstdint>

#include "ants_ai/tactics.hpp"

namespace ants::ai {

/// The tiles from the target of a fight that answers a blow within which an own ant is called to it (plan.group_fight: plan.group_reach, which the leash does not cap; else the smaller of plan.defend_reach and plan.leash_tiles)
int32_t recruit_reach(const LevelPlan& plan) noexcept;

}  // namespace ants::ai
