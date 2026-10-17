#pragma once

// The tuning keys of bot_arena (tools/bot_arena_tune.cpp): the ablations of the standard bot's plan that --tune K=V,... and a seat's `standard+K=V` put over the plan of its level.
#include <cstdint>
#include <string>

#include "ants_ai/tactics.hpp"

/// Puts the key's value over the plan; false (and `err` says why) for a key that does not exist or a value that is out of range
bool apply_tune(ants::ai::LevelPlan& p, const std::string& key, int64_t v, std::string& err);
