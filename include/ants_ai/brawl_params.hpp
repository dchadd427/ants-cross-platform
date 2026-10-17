#pragma once

// The numbers of the 187 brawl (brawl_task.hpp); LevelPlan holds one set per level.

#include <cstdint>

namespace ants::ai {

struct BrawlParams {
    uint8_t retreat_hp{3};               // an ant with this many hit points or fewer walks home to heal
    uint8_t return_hp{9};                // ... and fights again with this many or more
    uint32_t per_target{2};              // the most ants that strike one victim at a time (one blow lands per hit clip, a third ant only finishes)
    uint32_t crowd{3};                   // ants without a victim of their own join a fight up to this many on one victim
    uint32_t hp_weight{30};              // a victim's hit points count this much against a tile of distance (10) when the victims are ranked
    uint32_t regap_ticks{8};             // a pack is ordered onto its victim again no sooner than this
    int32_t reach_tiles{40};             // enemy ants further than this from an ant are no target of that ant
    uint32_t march_gap_ticks{60};        // an ant that marches is told again no sooner than this
};

}  // namespace ants::ai
