#pragma once

// "Others = confirmed" (docs/NETWORK_PORT.md, "Drawing the other players' ants from the confirmed engine"), a TEST way to draw a match of the network that predicts the player's own orders. With
// the prediction on, the whole picture is the predicted engine, `lead` ticks ahead of the confirmed one: the player's own ants answer a click at once, and the ants of every other player (a computer
// player, an opponent, an ally) are the confirmed engine's ants a few ticks ahead, which a turn of theirs that the prediction did not have then puts elsewhere (a correction that slides). In this mode the ants of
// every OTHER player are drawn from the confirmed engine itself, a few ticks in the past and never corrected, while the player's own ants stay the predicted engine's. The picture is one world made of both
// (a model without SDL, so that the frame, the HUD and the tests use the same rule): the shown engine's world, with the ants of every player but the viewer taken from the confirmed engine's.
// Which engine draws what:
//   - the viewer's own ants, and what only they cause (their blows, the food they take, the bridges and bombs and fire walls they make, the power-ups they take): the predicted engine, as before.
//   - every other player's ants: the confirmed engine's, with their health marks, flinches, blows, deaths and the damage numbers over them (which always were the confirmed engine's).
//   - the ground (the tiles with food, power-ups, bombs, fire walls and bridges), the effects, the flower droppers and the scores: the predicted engine's, one world with the viewer's own ants: so what an own ant
//     takes or makes is gone or there at once, and nothing of the viewer's own appears twice. What another player's ant takes or makes shows up to `lead` ticks before that ant is drawn doing it.
//   - the explored ground (Fog of War): the confirmed engine's (the predicted engine adopts it after every tick), so no other player's ant is drawn where the confirmed engine has not explored, and none
//     is drawn earlier than the confirmed engine has it.
// Drawing and picking only: the engines, the turns, the hashes, the recordings and the protocol are untouched, and nothing but this picture reads the world that is made here.
// Expiry (AGENTS.md rule 9): the mode is off by default (`--others confirmed`, `?others=confirmed`); it is retired or removed within two releases of its first one.

#include <cstdint>
#include <vector>

#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

class OthersView {
public:
    /// What a world was made from. Two worlds made from the same stamp are the same world (the engines change only by a tick, a rebuild of the predicted engine, or an order applied to it), so the
    /// frame, the pointer and the keys of one moment share one world
    struct Stamp {
        uint64_t shown_tick{0};           // the predicted engine's tick
        uint64_t confirmed_tick{0};
        uint64_t rebuilds{0};             // the predicted engine's rebuilds, its starts and the orders applied to it
        uint64_t starts{0};
        uint64_t orders{0};
        bool operator==(const Stamp& o) const noexcept {
            return shown_tick == o.shown_tick && confirmed_tick == o.confirmed_tick && rebuilds == o.rebuilds && starts == o.starts && orders == o.orders;
        }
    };

    /// The world of the picture: `shown` (the predicted engine's) with the ants of every player but `own` taken from `confirmed`. An ant that only one engine has counts for its owner's engine: an own ant that
    /// the predicted engine no longer has (it was killed in the ticks ahead) is gone, another player's ant that the confirmed engine does not have yet (hatched in the ticks ahead) is not there. The
    /// ants stay in the engine's order (by id), so the picture's ties are broken as an engine's would. The explored ground is `confirmed`'s. `shown` and `confirmed` stay as they are; the result lives until
    /// the next call.
    const sim::WorldState& compose(const sim::WorldState& shown, const sim::WorldState& confirmed, uint8_t own, const Stamp& stamp);
    /// The same, with the stamp that says "never made": always made again (the tests' reference)
    const sim::WorldState& compose_now(const sim::WorldState& shown, const sim::WorldState& confirmed, uint8_t own);
    /// The next call makes the world again (the match ended, or the prediction stopped)
    void forget() noexcept { valid_ = false; }
    /// How many worlds were made (a call with the stamp of the world that stands makes none)
    uint64_t made() const noexcept { return made_; }

private:
    sim::WorldState world_;
    std::vector<sim::AntSnapshot> ants_;                                // (the ants of the world that is made, kept for their memory)
    Stamp stamp_;
    uint8_t own_{255};
    bool valid_{false};
    uint64_t made_{0};
};

/// The tile that an attack order on a picked ant names. The picked ant is drawn from the confirmed engine, a few ticks behind the engine that the order meets: its tile there is where it will have
/// walked on to when the host runs the order, which is where the predicted engine has it. So the order names that tile (and an attack that was aimed at the drawn tile misses a walking ant: the
/// ant has left it). Only where the picture would draw the ant at that place (`ant_drawn`: explored ground, or the viewer's own or an ally's ant): never a place that the fog hides, which the
/// player has not seen. False (and `tile` untouched) when the predicted engine does not have the ant, or the place is hidden: the order then names the tile where the ant is drawn.
bool aim_at_predicted_ant(const sim::WorldState& predicted, uint8_t viewer, const sim::AntSnapshot& drawn, sim::TileCoord& tile);

}  // namespace ants::app
