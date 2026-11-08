#pragma once

// HealTask: a hurt ant that is in danger walks into its own hill, which restores all its hit points (the heal frame of its enter clip lasts (10 - hp) * 200 ms: under two seconds from 1 hit
// point; docs/GAMEPLAY.md, "Anthill Enter, Heal, Hatch & Raid"), and goes back to its job. It is built and OFF at every level (LevelPlan::heal; `bot_arena --tune heal=1` turns it on); docs/BOT_PEOPLE.md, "Step 2: hurt ants heal",
// has the measurements. The engine walks an ant home by itself only at 1 hit point; before this task an ant at 2 to 4 hit points went on with its job and was struck again.
//
//   who goes       an own ant that takes orders (not stunned) with at most `heal_hp` hit points, when an enemy ant that can strike stands within `heal_danger_tiles` of it, or it lost hit points within
//                  `heal_recent_ticks`, or (no danger) it has at most `heal_calm_hp` hit points and its walk is at most 6 tiles; and its walk home is at most `heal_walk_tiles` (heal_wanted). Never a Thief
//                  in its raid clip (or one of the raid task near the raid tile of an enemy hill), the Fire Ant that keeps the walls while an enemy Thief stands within 4 tiles of the raid tile, an ant
//                  of the island play, an ant of a task that this one cannot take from (a fight, a gate goal: rank 5 and above), an ant that stands on a power-up, an ant on the hill's mound, and an ant
//                  that the engine already walks home (1 hit point, walking, nearer than at the last look). A carrier goes too: its click on the entrance banks the food and heals; the gate task and the carrier aid leave it to this task
//   the order      one group move onto the hill's entrance, Urgent, for every ant that is due at this look. An ant that walks nearer is not ordered again; one is ordered again after 40 ticks when it
//                  stands idle or its walk grew (and a walk joins it to the hill), at most 8 times; after 600 ticks, or two can't-go reactions, the ant is given back and left alone for 600 more
//   done           the ant has all its hit points again (it entered and healed) or is gone: it is given back, and its task asks for it again
//
// It sits at the rank of a fight (5): it takes ants from the ranks below and never one from a fight, a rush or a gate goal; a fight never takes an ant that it holds. In the game type 187 the hill is
// closed (BotView::hill_enterable): the task does nothing there (the brawl has its own retreat). Everything is read from the BotView and the map's analysis; no order is a promise: the next view shows
// what became of it.

#include <cstdint>
#include <map>

#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// The walk of an ant on `tile` to the entrance of the seat's hill, in tiles: the hill's walking cost field (20 cost units are one step over grass, a diagonal step costs 1.4 of it), rounded up; the
/// Chebyshev distance to the entrance when the map has no field for the seat; -1 when no walk joins the tile to the hill
int32_t walk_home_tiles(const MapInfo& map, uint8_t seat, sim::TileCoord tile) noexcept;

/// Whether an enemy ant that can strike (it carries nothing, and it is no ally's) stands within `radius` tiles of `tile`
bool striker_near(const BotView& view, sim::TileCoord tile, int32_t radius) noexcept;

/// The plan's numbers of who is sent home: `hp` is at most heal_hp, the walk (`walk`, -1: none) is at most heal_walk_tiles, and there is a reason: a striker near (`danger`), a blow within
/// heal_recent_ticks (`recent_blow`), or no danger but hp at most heal_calm_hp with a walk of at most 6 tiles
bool heal_wanted(const LevelPlan& plan, uint8_t hp, bool danger, bool recent_blow, int32_t walk) noexcept;

class HealTask final : public Task {
public:
    HealTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "heal"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;

    // ---- for the tests and the reports ----
    /// The ants that the task holds now, and whether it holds `ant`
    size_t jobs() const noexcept { return jobs_.size(); }
    bool holds(uint32_t ant) const noexcept { return jobs_.count(ant) != 0; }
    /// Ants taken, group orders made, ants named in them (an ant that is ordered again counts again), ants given back healed, ants given up on after 600 ticks
    uint32_t started() const noexcept { return started_; }
    uint32_t orders() const noexcept { return orders_; }
    uint32_t ordered() const noexcept { return ordered_; }
    uint32_t healed() const noexcept { return healed_; }
    uint32_t gave_up() const noexcept { return gave_up_; }

private:
    struct Job {
        uint64_t taken{0};               // the tick of the look that took the ant
        uint64_t last_order{0};          // the tick of its last order (0: none yet: it was already walking nearer)
        uint32_t orders{0};              // the orders it was named in
        bool cant_go{false};             // it shows the can't-go reaction now (counted once for each)
        uint32_t refused{0};             // the can't-go reactions it showed
    };
    struct Seen {                        // an ant that is hurt: where it was at the last look
        uint64_t tick{0};
        int32_t walk{-1};
    };

    void release_all(AntLedger& ledger);

    Tactics& tactics_;
    std::map<uint32_t, Job> jobs_;                    // by own ant
    std::map<uint32_t, Seen> seen_;                   // by own ant: the walk at the last look, for the ants that are hurt
    std::map<uint32_t, uint64_t> hurt_;               // by own ant: the tick of the last look at which it lost hit points
    std::map<uint32_t, uint64_t> left_alone_;         // by own ant: the tick until which an ant that was given up on is not taken again
    uint32_t started_{0};
    uint32_t orders_{0};
    uint32_t ordered_{0};
    uint32_t healed_{0};
    uint32_t gave_up_{0};
};

}  // namespace ants::ai
