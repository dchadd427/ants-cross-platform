#pragma once

// The reach guard (docs/BOTS.md, "The can't-go loop", orders over water): the last look of a standard bot at the orders of a look. An ant is not ordered to a place that its own walk
// cannot reach on the map as it is now (a pile, a power-up, an enemy ant or a tile for a bomb or a fire wall on another island): the engine answers such a click with "Can't go there."
// and the ant says it aloud. The orders that stay are the orders that can be carried out; for those that go, the tasks are told like for a click that the controller refuses
// (Bot::Fate::Filtered), so none of them proposes the same click again at every look.

#include <cstdint>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"

namespace ants::ai {

class ReachGuard {
public:
    /// Takes the orders of `orders` that no walk of the ant can carry out out of it and returns them (as they were proposed: the ants that cannot go, the tile). A group order keeps the ants that
    /// can go; one with none left goes whole; of a timed chain a refused step takes the whole chain. The goal of a click is the engine's (the tile, or the first tile of its ring scan, so a click on
    /// water is a walk to a shore) and a goal that no walk can end next to (a pile in a ring of rocks) is refused. A Swimmer is never refused (it swims); a Fire Ant walks over fire walls; a power-up
    /// does not cut a place off (the island tasks walk their rows of them); an ant whose place the walk map does not know (an ant in a mound) and a click with no tile to walk to (the engine stops
    /// the ant without a word) are never refused: the engine decides those.
    static std::vector<sim::Command> filter(const BotView& view, uint8_t seat, Orders& orders);
};

}  // namespace ants::ai
