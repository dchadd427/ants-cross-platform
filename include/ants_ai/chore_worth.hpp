#pragma once

// Whether a chore is worth an ant's time (docs/BOT_FIGHT.md, "Chores"). The fire wall chores of the Hard bot are many (a Fire Ant spends 11 to 19 percent of its time on them on the shipped maps), and the
// measurements say which pay: the three walls in front of the own thief hole stop the raids (without them TREASURE falls from about 2,300 to 500 points a seat) and stay as they are; the ring
// that the fire-in lights round an enemy gate costs that team the deposits of its carriers and nothing else, and it is worth no more than that: with no food for the enemy to bank (no pile its hill
// walks to, no carrier) a ring costs it nothing, and the Fire Ant that lights it and lights it again every minute is an ant that could harvest.

#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"

namespace ants::ai {

/// Whether a ring of fire walls round the gate of `victim` costs it anything now: a pile with units left that its hill can walk to (the analysis of the start; a lunchbox counts), or an ant of
/// the team that carries food (the picture shows it). False: the ring would burn for nothing.
bool fire_in_worth_it(const BotView& view, const MapInfo& map, uint8_t victim) noexcept;

}  // namespace ants::ai
