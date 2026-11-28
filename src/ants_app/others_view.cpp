#include "ants_app/others_view.hpp"

#include "ants_app/health_changes.hpp"

#include <cstddef>

namespace ants::app {

const sim::WorldState& OthersView::compose(const sim::WorldState& shown, const sim::WorldState& confirmed, uint8_t own, const Stamp& stamp) {
    if (valid_ && own == own_ && stamp == stamp_) return world_;
    compose_now(shown, confirmed, own);
    stamp_ = stamp;
    own_ = own;
    valid_ = true;
    return world_;
}

const sim::WorldState& OthersView::compose_now(const sim::WorldState& shown, const sim::WorldState& confirmed, uint8_t own) {
    ++made_;
    world_ = shown;                                                      // (the whole world is the shown engine's, a copy that keeps the memory it has: only the ants and the explored ground differ)
    world_.fog_of_war_enabled = confirmed.fog_of_war_enabled;            // (what is explored is the confirmed engine's: no ant is drawn where it has not explored)
    world_.fog_revealed = confirmed.fog_revealed;

    std::vector<sim::AntSnapshot>& ants = ants_;
    ants.clear();
    ants.reserve(confirmed.ants.size());
    const std::vector<sim::AntSnapshot>& mine = shown.ants;
    const std::vector<sim::AntSnapshot>& theirs = confirmed.ants;
    size_t s = 0;
    size_t c = 0;
    while (s < mine.size() || c < theirs.size()) {                       // (both lists are in the engine's order, by id: one pass, the lesser id first)
        const bool both = s < mine.size() && c < theirs.size() && mine[s].id == theirs[c].id;
        const bool shown_first = c >= theirs.size() || (s < mine.size() && mine[s].id < theirs[c].id);
        if (both) {
            ants.push_back(mine[s].player_id == own ? mine[s] : theirs[c]);
            ++s;
            ++c;
        } else if (shown_first) {
            if (mine[s].player_id == own) ants.push_back(mine[s]);         // (an ant that only the predicted engine has: its own, or nobody's yet)
            ++s;
        } else {
            if (theirs[c].player_id != own) ants.push_back(theirs[c]);     // (an ant that only the confirmed engine has: another player's, else it is gone)
            ++c;
        }
    }
    world_.ants.swap(ants);
    return world_;
}

bool aim_at_predicted_ant(const sim::WorldState& predicted, uint8_t viewer, const sim::AntSnapshot& drawn, sim::TileCoord& tile) {
    for (const sim::AntSnapshot& ant : predicted.ants) {
        if (ant.id != drawn.id) continue;
        if (!health::ant_drawn(predicted, viewer, ant)) return false;      // (the place is one that the picture would not show: the player has not seen the ant go there)
        tile = sim::TileCoord{ant.tile_x, ant.tile_y};
        return true;
    }
    return false;
}

}  // namespace ants::app
