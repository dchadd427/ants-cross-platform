#pragma once

// The Yard's world on the client (docs/YARD.md): an ordinary SimulationEngine that never ticks. Its ants are a pool of ordinary AntUnits whose public fields are rewritten from each batch of the
// server, so that get_world_state() builds the AntSnapshots that the renderer, the HUD's picking and the cursor already read: nothing of the picture, the pointer or the orders is written again
// for the Yard. Only public calls of the engine are used (spawn_unit, get_unit, grid, grid_mut, set_fog_of_war_enabled); src/ants_sim is not changed.
//
// A batch names its ants by the server's ids; the pool gives each the slot (the id of a unit) that the HUD and the engine know it by, and the sink (yard_sink.hpp) turns the slots of an order
// back into the server's ids. The server is not trusted with the client's health: a record that the tables or the map cannot place is not shown, an id that comes twice is shown once, and
// more ants than the pool holds push out the others' ants, never the visitor's own.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ants_sim/sim_engine.hpp"
#include "ants_yard/yard_types.hpp"

namespace ants::app {

class YardPuppet {
public:
    /// The pool: all of the visitor's own ants and the most that a batch holds of the others' (yard::kMaxOwnAnts + yard::kMaxForeignAnts), with room to spare
    static constexpr size_t kSlots = 160;

    /// `engine` is the Yard's world: loaded with the Yard's map, and never ticked. The pool is made at once (units that are removed until a batch uses them).
    explicit YardPuppet(sim::SimulationEngine& engine, size_t slots = kSlots);
    YardPuppet(const YardPuppet&) = delete;
    YardPuppet& operator=(const YardPuppet&) = delete;

    /// The world as the batch says. An ant that is in it keeps its slot, a new one takes a free slot, the others are removed. `held` are the slots that the caller still points at (the HUD's
    /// selection): such a slot is never given to another ant, so that nothing that is selected turns into a stranger. Returns how many ants are shown; the picture sees them at once.
    size_t apply(const std::vector<yard::AntRecord>& records, const std::vector<uint32_t>& held = {});
    /// The places with an open game, drawn as the game's own hills: at most yard::kMaxHills, a hill that does not lie on the map or whose id is 0 or taken is left out. Returns how many are set.
    size_t set_hills(const std::vector<yard::Hill>& hills);

    /// The server's id of the ant in a slot (the id of a unit), 0 when the slot holds none or is not one
    uint32_t server_id(uint32_t slot_id) const noexcept;
    /// ... and the slot of an ant, 0 when the ant is not shown
    uint32_t slot_id(uint32_t server_id) const noexcept;
    /// The ant in the slot is the visitor's own (color 0)
    bool is_own(uint32_t slot_id) const noexcept;
    /// The tile is on the Yard's map
    bool on_map(int32_t tile_x, int32_t tile_y) const noexcept;
    /// The server's id of the other visitor's ant on a tile, 0 when none stands there; when several do, the last in the world's order (the one that the HUD's pointer takes when it hits both)
    uint32_t foreign_ant_at(int32_t tile_x, int32_t tile_y) const noexcept;
    /// How many of the visitor's own ants are shown, and where their middle is (false when none is)
    size_t own_shown() const noexcept;
    bool own_center(int32_t& world_x, int32_t& world_y) const noexcept;

    /// The ants that are shown now, and the records of the last apply() that were left out (a clip that the tables do not have, a color that is none, a repeated id, no slot left)
    size_t shown() const noexcept { return by_server_.size(); }
    size_t left_out() const noexcept { return left_out_; }
    size_t slots() const noexcept { return slots_.size(); }

private:
    struct Slot {
        uint32_t unit{0};        // the id of the AntUnit
        uint16_t server{0};      // the server's id of the ant it shows; 0: free
        uint8_t color{0};
        int32_t px{0};           // where it stands, in world pixels
        int32_t py{0};
    };

    sim::SimulationEngine& engine_;
    std::vector<Slot> slots_;                                  // the pool, made once
    std::unordered_map<uint32_t, size_t> by_unit_;             // the id of a unit -> its place in slots_
    std::unordered_map<uint16_t, size_t> by_server_;           // the server's id of a shown ant -> its place in slots_
    size_t cursor_{0};                                         // where the search for a free slot goes on: slots are used round and round, so that a freed one is the last to come back
    size_t left_out_{0};
};

}  // namespace ants::app
