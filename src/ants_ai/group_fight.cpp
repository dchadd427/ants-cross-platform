#include "ants_ai/group_fight.hpp"

#include <algorithm>

namespace ants::ai {

int32_t recruit_reach(const LevelPlan& plan) noexcept { return plan.group_fight ? plan.group_reach : std::min(plan.defend_reach, plan.leash_tiles); }

}  // namespace ants::ai
