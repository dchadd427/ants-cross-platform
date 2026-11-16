#pragma once

// The answer to a raid that needs no fire and no bomb (docs/BOTS.md, "Thieves: the answer to a raid"; rank 5 like a fight, Medium and Hard, plan.thief_guard):
//
// An enemy Thief that comes for the own hole is a target that does not fight back. A blow takes one hit point (a Combat Ant's punch two, and throws it four tiles), cancels the walk of the
// Thief (its raid is over; its owner has to order it again) and leaves it standing for about a second, so even one worker that meets it head on wears it down, and a Combat Ant kills it
// in five punches. The task watches the enemy Thieves that the view shows:
//   coming     an empty-handed Thief within plan.thief_guard_radius tiles of the own raid tile that comes nearer (or stands), while at least one of the three tiles in front of the hole is
//              free: the nearest free fighters (Combat Ants first, then the plain ants, then Swimmers that stand on land; at most plan.thief_guard_force; never the last ants) go to the
//              posts on the column just east of those three tiles (a tile off the Thief's way in: it must pass them) and strike it, one attack order for every blow, as soon as it is
//              within three tiles or stands still
//   standing   a Thief that stands in front of the walls or the hole (it waits for a wall to burn out, or for its owner's next order): struck the same way
//   raiding    the Thief on the raid tile (a tile of the hill: no click names it) is waited for at the posts; when it comes out with the loot it is struck on the tile in front of the hole. The loot
//              drops where it dies (the engine's lunchbox: the economy takes it home), and the victim has lost it already
//   trapped    (plan.thief_trap) the Thief is on the raid tile and the own walls have shut every tile in front of the hole: when the fighters stand ready at the posts, the WallTask puts one wall out
//              (WallTask::request_open) and the Thief is struck as it comes out; with nobody ready the walls stay
// Nothing here names a tile that a living ant stands on or a tile of a hill (the controller refuses both: Fate::Filtered), no fighter stands on a tile in front of the hole (a standing ant there
// shuts the hole for its own Thief too, and for the enemy's way out), and nothing but plain moves and attack orders is sent.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class WallTask;

class ThiefGuardTask final : public Task {
public:
    ThiefGuardTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "thief-guard"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    /// The task that keeps the walls of the own hole: the trap asks it to put one wall out
    void attach(WallTask* walls) noexcept { walls_ = walls; }

    // ---- for the tests and the reports ----
    /// Meetings begun (one for each enemy Thief), attack orders sent for them, walls asked to be put out, and meetings whose Thief left the view (killed, drowned, or gone out of sight)
    uint32_t meets_started() const noexcept { return meets_started_; }
    uint32_t strikes() const noexcept { return strikes_; }
    uint32_t trap_opens() const noexcept { return trap_opens_; }
    uint32_t thieves_gone() const noexcept { return thieves_gone_; }
    /// Meetings under way now, and the fighters of the one against `thief` (empty: none)
    size_t meets() const noexcept { return meets_.size(); }
    std::vector<uint32_t> fighters_of(uint32_t thief) const;
    /// The post that the plan gives the n-th fighter at a hill (the column just east of the three tiles in front of the hole)
    static sim::TileCoord post_of(const HillInfo& hill, size_t n) noexcept;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Fighter {
        bool blocker{false};                 // stands on a tile in front of the hole (the body that shuts it); a striker stands on the column east of them
        sim::TileCoord post{-1, -1};
        uint64_t moved{0};                   // the tick of the last move order (0: none)
        uint64_t struck{0};                  // the tick of the last attack order (0: none)
        uint64_t sent{kPending};             // the tick the last order LEFT (kPending: not left yet)
    };
    struct Meet {
        uint32_t thief{0};
        uint64_t started{0};
        uint64_t last_threat{0};             // the last look at which the Thief was a target
        uint64_t trap_asked{0};              // the tick the wall was asked to be put out (0: none)
        uint64_t stood_since{0};             // the tick since which the Thief has not walked (0: it walks)
        std::map<uint32_t, Fighter> fighters;
    };
    void release_all(TaskContext& context);
    void end_meet(TaskContext& context, std::map<uint32_t, Meet>::iterator it, bool gone);
    bool wanted(const BotView& view, const HillInfo& hill, const AntView& thief, bool gate_closed, uint64_t now) const;
    void top_up(TaskContext& context, Meet& meet, const AntView& thief, const HillInfo& hill);
    void run(TaskContext& context, Meet& meet, const AntView& thief, const HillInfo& hill, bool gate_closed);

    Tactics& tactics_;
    WallTask* walls_{nullptr};
    std::map<uint32_t, Meet> meets_;                       // by the Thief's id
    std::map<uint32_t, uint64_t> rest_;                    // Thief -> the tick until which a meeting with it is not begun again (the last one ran out its time)
    uint32_t meets_started_{0};
    uint32_t strikes_{0};
    uint32_t trap_opens_{0};
    uint32_t thieves_gone_{0};
};

}  // namespace ants::ai
