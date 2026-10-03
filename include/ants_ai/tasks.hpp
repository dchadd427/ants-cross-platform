#pragma once

// The task model of the bots (docs/BOTS.md, "Architecture"): what a bot wants becomes TASKS that claim ants, put orders into Orders, check from the next
// view whether the world did what was meant, and end with success or failure.
//
//   AntLedger     which ant is claimed by which task (an ant belongs to at most one task; a task that wants an ant that another holds must ask again later)
//   TaskContext   what a task is given at every look: the view, the door out (Orders), the ledger, the profile of the level and the analysis of the map
//   Task          a state machine: step() at every look, on_command() when a command it proposed has a fate; Running, Done or Failed
//   HarvestTask   the economy (B3): idle empty ants are sent, a group move per pile (at most 8 ants newly ordered at a look), onto a click tile of a pile; the engine's own
//                 harvest loop (bite, walk home, deliver, walk back) runs by itself until the pile is empty, so the whole economy needs a few hundredths of a command per second
//
// Principle: every piece of task state is SOFT. An ant that was hit, healed, born or stranded is simply idle and empty-handed and re-enters the pool; a pile that
// is empty is no candidate; nothing a task knows is a fact that the world could not give back, so a bot may be restarted at any moment and a recorded match
// needs nothing from the bot. What a task remembers (which ant was told what, when the order LEFT, which pile did not start) is a cache and a piece of learning.
//
// A bot never sees the answer of the simulation to a command (a lock-step room gives none), so every task is open loop: it learns from the NEXT view, and it counts
// the time of an order from the tick it was RELEASED (Bot::on_command, Fate::Sent), not from the tick it was decided: the reaction delay and the budget hide the order
// from the bot for a while, and a timer that started at the decision blacklists every pile before its first order has even left (the first prototype did).
//
// Integer arithmetic, ordered containers, no clock, no randomness: the same world always gives the same orders.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/command.hpp"

namespace ants::ai {

/// The number of a task in the ledger of one bot: 0 is nobody
using TaskId = uint32_t;
inline constexpr TaskId kNoTask = 0;

/// Which ant is claimed by which task. An ant has at most one owner. A task may have a RANK (set_rank; the standard bot gives every task one): a task of a higher rank may TAKE an
/// ant from a task of a lower rank (take), which then finds out at its next look that the ant is no longer its own. Tasks without a rank all have rank 0 and take nothing.
class AntLedger {
public:
    /// True when the ant was free or already the task's (and now is); false when another task holds it, or `task` is kNoTask
    bool claim(uint32_t ant, TaskId task);
    /// The rank of a task (0 until set_rank): who may take an ant from whom
    void set_rank(TaskId task, uint8_t rank);
    uint8_t rank(TaskId task) const noexcept;
    /// claim(), and when another task holds the ant: take it if that task's rank is LOWER than `task`'s. True when the ant is the task's now. The previous owner is not told: it
    /// notices at its next look that the ledger gives the ant to another (HarvestTask forgets its order of the ant).
    bool take(uint32_t ant, TaskId task);
    /// True when the ant was the task's and is free now
    bool release(uint32_t ant, TaskId task);
    /// Frees every ant of the task; the number of ants that were freed
    size_t release_all(TaskId task);
    /// The task that holds the ant, kNoTask when it is free (or unknown)
    TaskId owner(uint32_t ant) const noexcept;
    bool is_free(uint32_t ant) const noexcept { return owner(ant) == kNoTask; }
    size_t count(TaskId task) const noexcept;
    /// The ants of the task, by id
    std::vector<uint32_t> ants_of(TaskId task) const;
    size_t size() const noexcept { return owner_.size(); }
    /// Drops the claims of ants that are not in `alive` (the seat's own living ants, sorted by id: BotView::mine())
    void forget_missing(const std::vector<AntView>& alive);

private:
    std::map<uint32_t, TaskId> owner_;
    std::map<TaskId, uint8_t> rank_;
};

/// What a task gets at every look
struct TaskContext {
    const BotView& view;
    Orders& orders;
    AntLedger& ledger;
    const Profile& profile;
    const MapInfo& map;
    uint8_t seat;
};

class Task {
public:
    enum class State : uint8_t { Running, Done, Failed };

    explicit Task(TaskId id) noexcept : id_(id) {}
    virtual ~Task() = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    TaskId id() const noexcept { return id_; }
    State state() const noexcept { return state_; }
    virtual const char* name() const noexcept = 0;

    /// At every look of the bot: read the view, claim and release ants, put orders into the context
    virtual void step(TaskContext& context) = 0;
    /// What became of a command that the bot proposed (every command of the bot goes to every task: a task looks for its own ants). `tick` is the release tick for Sent.
    virtual void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
        (void)command;
        (void)fate;
        (void)tick;
    }
    /// The task ends (or the bot stops): its ants are free again
    virtual void finish(AntLedger& ledger) { ledger.release_all(id_); }

protected:
    void set_state(State state) noexcept { state_ = state; }

private:
    TaskId id_;
    State state_{State::Running};
};

// ---- the economy -------------------------------------------------------------------------------------------------------------------------------------

/// The standing task of the economy: it takes every idle, empty-handed worker that no other task holds and sends it to the best pile. It never ends while the match
/// runs (Running), and has no ant of its own to keep: the ledger holds the ants that were ordered until they are idle again.
///
///   pool        own Workers that stand idle with nothing in their hands and no order of this task that is pending or younger than fail_after_ticks. An ant that holds food
///               is NEVER ordered: a carrier ordered onto another pile walks home and delivers at its own hill with the order replacing the pile of its loop (a bite that never
///               reaches the pile it was taken from); it walks home by itself.
///   candidates  piles with units left, that the seat's hill can walk to (MapInfo), that the ant can walk to as well (its walker component, so an ant stranded on another
///               island is never sent to the pile of the hill's island), that did not fail lately (blacklist), that the ant did not fail on lately (see "learning") and whose trip
///               can still be finished before the clock runs out. MapInfo is the map of the START: a pile that was shut off then (a wall, food that plugs a corridor) is asked again
///               every reask_ticks with the map as it is now (MapInfo::field_now and approach_now, with the seat's rules of the moment from BotView::walk_context: its own and its
///               ally's bombs block, a dropped team's queue row is open), and so are the ants that the start analysis took for shut in.
///   rank        value-aware levels: points per tick of the trip (value * 100000 / trip), the best first; Easy: the nearest first
///   caps        at most Profile::max_ants_per_pile (8 at Medium and Hard, 4 at Easy) and trip / gate_gap + 2 ants per pile, whichever is less (the hill banks about one
///               deposit per 90 to 110 ticks, so more ants than that cannot bring more): an ant goes to the first pile with room, else to the best one
///   order       one group move per pile onto a cell of the pile (the click tile of the analysis; of the pile as it is now when it has been eaten into), and at most
///               max_ants_per_look ants are newly ordered at one look (the rest of the pool at the next looks), so no order names more than that. This is the engine's doing: the
///               path manager of the original (PATHMGR: one request per 50 ms, four search grids) never recovers from one order of 24 ants that stand packed together (they block
///               each other's paths and 15 requests stay pending for the rest of the match, with no bot involved), and ants ordered in groups of 8 at once still box each other in
///               for hundreds of ticks. Shipped maps start with at most 8 ants per team, so the limit changes nothing there.
///   watchdog    an ordered ant on its way to a pile (not one that queues at the pile, not a carrier) that walks, or waits for a path, on the same tile for stall_ticks is jammed:
///               it gets a Stop and is ordered again from the pool, and the number of ants ordered at one look is halved until a quiet time has passed
///   rescue      a carrier that stands idle with its food is stuck, not queueing, when it has stood so for longer than any legitimate wait for the gate. The engine's walk home
///               ended in "Can't go there." (an ant of the hill's own team stood paused, or idle with no order, on the doorway in front of the entrance when the path was searched:
///               the mound is solid except the entrance and the doorway above it, so one team-mate there makes the hill unreachable for that moment) or a blast or a blow cancelled
///               the walk; the original never retries (the count-0 branch only stops the ant), so the carrier stays with its food for the rest of the match, wherever it stands: far
///               from the hill after a bite (TINY seat 3) or on the tile in front of the gate (TREASURE seat 0); docs/GAME_REVERSE_ENGINEERING.md, 5.40 has the code. The ants that
///               queue for the gate are idle with food too, for about 100 ticks per ant ahead of them: 772 ticks at the longest in 100,000 waits on the five shipped maps with food (up
///               to 6 ants on a hill there), and 870 / 1,439 / 1,800 / 2,384 ticks with 12 / 16 / 20 / 24 workers on one hill. So distance from the hill tells nothing; time does: after
///               rescue_after_ticks (900) of standing idle with food the ant is sent to the hill entrance by hand, as a person would click it: it queues for the gate and delivers (its
///               wait starts again). A carrier more than ring_tiles from the mound, with nobody queueing within that ring, is helped after rescue_far_ticks. Nobody is helped while
///               rescue_max_carriers or more carriers stand idle at once: the gate is the bottleneck then, an idle carrier more or less costs nothing, and a rescue sends an ant
///               through the crowd of the queue (12 to 24 workers on one hill scored up to 10 percent less with rescues). The rescue never sends a carrier to a pile.
///   learning    an order that came back Applied and did nothing is a failure: the ant stands where it stood, empty, fail_after_ticks after the order LEFT; or it shows the "can't
///               go" reaction within one look of those ticks while it is still within a tile of where it stood (later it is a jam on the way, no failure of the pile). Whose fault
///               it is: the ANT's when another ant of the task walks to the pile, bites at it or carries from it, or when an ant that failed on another pile lately is the only
///               one that failed here (an ant that is shut in, or whose nook has a blocked door, fails on every pile it is sent to): the ant is kept away from that pile for
///               blacklist_ticks and the pile stays as it was. The PILE's otherwise, when two ants that fail nowhere else failed on it, or when no ant of the task makes
///               progress anywhere (a seat that gets nowhere cannot tell, and a wall that came up after the analysis of the map is learned this way): it is blacklisted for
///               blacklist_ticks for every ant.
/// The classes of a pile for the contest-aware order (HarvestTask::Params::contest_aware), in the order they are served
enum class PileClass : uint8_t { Multi = 0, One = 1, Safe = 2, Shared = 3, Hopeless = 4 };

class HarvestTask final : public Task {
public:
    struct Params {
        uint32_t gate_gap_ticks{90};         // the hill banks about one deposit per 90 to 110 ticks (docs/BOTS.md)
        uint32_t blacklist_ticks{900};       // a pile that did not start is left alone this long (45 s), then tried again
        uint32_t fail_after_ticks{24};       // an ordered ant that is still where it was this many ticks after its order LEFT did not obey (path 6+ ticks, "can't go" 8, a room 4)
        uint32_t endgame_margin_ticks{10};   // a new trip needs trip + this + the reaction delay of the level to finish before the clock runs out
        uint32_t reask_ticks{200};           // piles that were shut off at the start, and ants that were shut in, are asked again this often (10 s)
        uint32_t max_ants_per_look{8};       // the most ants newly ordered at one look, and so the most one group order names (see "order" above)
        uint32_t stall_ticks{300};           // an ordered ant that walks or waits for a path on the same tile this long (15 s) is jammed
        uint32_t rescue_after_ticks{900};    // an idle carrier is helped after this many ticks (45 s; the longest legitimate wait on the shipped maps was 772)
        uint32_t rescue_far_ticks{40};       // a carrier far from the hill with nobody queueing at the gate is helped after this long (2 s)
        uint32_t rescue_max_carriers{8};     // nobody is helped while this many carriers or more stand idle with food (the gate is saturated: see "rescue")
        uint32_t rescue_cooldown_ticks{200}; // and not again for this long (the walk takes about 80 ticks, the queue longer)
        bool rescue{true};                   // false: no carrier is ever sent home by this task (the standard bot's gate task owns every carrier)
        int32_t ring_tiles{4};               // "far from the hill" is more than this many tiles from the 4 x 4 mound (the queue stands within 3 with up to 8 ants on a hill)
        /// Ants of these types (bit t = AntType t) join the pool besides the level's default type: a typed ant harvests like a worker (a Combat Ant punches the enemy that comes within
        /// two tiles of its way as well), so the standard bot lets its Fire and Bomber ants harvest between their jobs. 0 (the worker bot): the default type only.
        uint8_t extra_types{0};
        /// Contest-aware piles (the standard bot; false for the worker, whose order and pinned numbers stay as they are): the candidates are ordered by CLASS first (the owner's playbook:
        /// "the center food first, then the contested food on one of the sides, depending on who is teamed up"), then within a class by the rank below. A team COMPETES for a pile when its
        /// hill reaches it at a cost between contest_low and contest_high percent of the own cost (the start analysis' costs of every team's hill, PileInfo::approach); an enemy that is
        /// nearer than contest_low percent will have the pile before the ants get there. The classes, in the order they are sent to:
        ///   Multi     at least two live teams that are not allies compete (the centre of TREASURE)
        ///   One       exactly one competes (a side that is shared with a neighbour)
        ///   Safe      nobody competes (the piles near the own hill)
        ///   Shared    only the ally competes, or is as near: the score box adds both scores, so what the ally takes costs the team nothing
        ///   Hopeless  an enemy is far nearer than the seat and nobody else competes: it is gone before the ants arrive
        /// The classes are made again at every look from the alliance state of the view: an alliance that forms moves the ally's side to Shared and a neighbour's to One.
        bool contest_aware{false};
        uint32_t contest_low{70};            // an enemy whose cost is below this percentage of the own cost is there first
        uint32_t contest_high{130};          // ... and above this percentage it is no competitor
        /// Within a class: the points that the pile still holds per tick of the trip (points of a unit times the units left; a richer pile first), not the points of one unit per tick of
        /// the trip. Only with value_aware_piles (Medium, Hard): Easy ranks by distance, as before.
        bool rank_by_remaining{false};
        /// false: a pile that only ONE enemy competes for is served with the safe piles (only the piles that several enemies reach come first)
        bool contest_one_first{true};
        /// The reactive variant (the tournaments' experiment): a pile is served first only while an enemy ant is at it (within three tiles of its anchor) and it is one that several
        /// enemies or one enemy compete for; every other pile is ranked by value per trip as before
        bool contest_reactive{false};
        /// The opening variant (the tournaments' experiment): until contest_opening_ticks, at most this many ants go to a pile that several enemies compete for (the centre), first; the
        /// rest harvest by value per trip as before; 0: off
        uint32_t contest_opening_ants{0};
        uint64_t contest_opening_ticks{1200};
        /// Fire-aware piles (the standard bot; false for the worker): a pile with a fire wall within fire_radius tiles of its anchor is asked again with the map as it is now (the
        /// start analysis does not know the walls): a pile that the walls have cut off is no candidate (no ant is sent into fire), one that they only made longer is ranked by its
        /// real cost. The engine's own path finder goes round a wall that leaves a way.
        bool fire_aware{false};
        int32_t fire_radius{4};
    };

    explicit HarvestTask(TaskId id) : HarvestTask(id, Params{}) {}
    HarvestTask(TaskId id, const Params& params) : Task(id), params_(params), throttle_(std::max<uint32_t>(1u, params.max_ants_per_look)) {}

    const char* name() const noexcept override { return "harvest"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    /// Piles that did not start, learned from the view (the ants of one order fail together: a pile counts once each time it is learned)
    uint32_t failures() const noexcept { return failures_; }
    /// Failed orders that were held against the ant (it is kept away from the pile) and not against the pile
    uint32_t ant_failures() const noexcept { return ant_failures_; }
    /// Ants sent to a pile (not commands: one command names a group)
    uint32_t ants_sent() const noexcept { return ants_sent_; }
    /// Group orders put into Orders
    uint32_t orders_issued() const noexcept { return orders_issued_; }
    /// Ants that hold an order of this task now (pending, on their way or at work)
    size_t working() const noexcept { return recs_.size(); }
    /// Whether the pile is blacklisted at `tick`
    bool blacklisted(uint32_t pile, uint64_t tick) const noexcept;
    /// Whether the ant is kept away from the pile at `tick` (an order of it failed there lately)
    bool excluded(uint32_t ant, uint32_t pile, uint64_t tick) const noexcept;
    /// The class (PileClass as a number) of a pile for the seat as of the last look (contest-aware option; -1 when the pile was not a candidate or the option is off): for the tests and the reports
    int tier_of(uint32_t pile) const noexcept {
        const auto it = tiers_.find(pile);
        return it != tiers_.end() ? it->second : -1;
    }
    /// Carriers sent home by hand (see the top of the class)
    uint32_t rescues() const noexcept { return rescues_; }
    /// Jammed ants that were stopped (see "watchdog")
    uint32_t stalls() const noexcept { return stalls_; }
    /// Ants that stood idle and empty and could not be sent anywhere at the last look (no pile left that they can reach in time)
    size_t unplaced() const noexcept { return unplaced_; }
    const Params& params() const noexcept { return params_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Rec {
        uint32_t pile{0};                    // the engine's index of the pile that was clicked
        uint64_t decided{0};                 // the tick of the look that proposed the order
        uint64_t sent{kPending};             // the tick the order LEFT (Bot::on_command), kPending until it did
        sim::TileCoord origin{};             // where the ant stood when it was ordered
        uint32_t bite{0};                    // the engine's index of the object whose units a bite takes (differs from `pile` only for a duplicate anchor)
        uint16_t units{0};                   // the units that object had when the order was proposed
    };
    struct Candidate {
        uint32_t pile{0};
        sim::TileCoord click{};
        int32_t cost{0};
        int32_t trip{0};
        int64_t rank{0};                     // bigger is better
        uint32_t cap{0};
        uint32_t load{0};                    // ants of this task that work the pile already
        uint32_t bite{0};                    // the object whose units a bite takes
        uint16_t units{0};                   // its units now
        bool shut_at_start{false};           // the analysis of the start could not reach it: the walker components of the start say nothing about it
        uint8_t tier{0};                     // contest-aware: the PileClass (always 0 when the option is off)
    };
    struct Watch {
        sim::TileCoord tile{};               // where an ordered ant that walks (or waits for a path) stood at the last look ...
        uint64_t since{0};                   // ... and since when
    };

    /// The ledger and the records agree again (a record that on_command dropped has no ant in the ledger any more)
    void sync_ledger(AntLedger& ledger) const;
    /// Asks the map as it is now again (see "candidates"): the piles that were shut off at the start and the hill's walking field for the ants that were shut in
    void reask(const TaskContext& context);
    /// The class of a pile for the seat (the contest-aware option), from the start analysis' cost of every team's hill to it
    uint8_t tier_for(const TaskContext& context, const PileInfo& pile, int32_t own_cost) const;
    /// Whether an ant standing on `tile` can walk to the hill now (the field of the last reask)
    bool connected_now(const MapInfo& map, sim::TileCoord tile) const noexcept;

    Params params_;
    std::map<uint32_t, Rec> recs_;           // ant id -> its order
    std::map<uint32_t, uint64_t> black_;     // pile index -> the tick until which it is left alone
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> excluded_;   // (ant id, pile index) -> the tick until which the ant is not sent to the pile
    std::map<uint32_t, std::map<uint32_t, uint64_t>> failers_;     // pile index -> the ants whose orders to it failed lately, and when
    std::map<uint32_t, Watch> watch_;        // ordered ants that walk or wait for a path: the watchdog's memory
    std::map<uint32_t, uint64_t> stuck_;     // ant id -> the tick it was first seen idle with food
    std::map<uint32_t, uint64_t> rescued_;   // ant id -> the tick it was last sent home by hand
    std::map<uint32_t, int> tiers_;          // pile index -> its class at the last look (contest-aware option)
    std::map<uint32_t, Approach> reach_now_; // piles that were shut off at the start and can be walked to now (as of the last reask)
    std::vector<int32_t> now_field_;         // the hill's walking field now (as of the last reask: MapInfo::field_now, from every walkable tile of the queue row), for the ants that the start analysis took for shut in
    uint64_t next_reask_{0};
    bool want_reask_{false};                 // an ant was seen that the start analysis took for shut in: look at the map again
    uint32_t rescues_{0};
    uint32_t failures_{0};
    uint32_t ant_failures_{0};
    uint64_t last_stall_{0};                 // the tick of the last jam the watchdog found
    uint32_t throttle_{0};                   // the ants that may be newly ordered at one look now: max_ants_per_look, halved at every jam the watchdog finds
    uint32_t stalls_{0};
    uint32_t ants_sent_{0};
    uint32_t orders_issued_{0};
    size_t unplaced_{0};
};

}  // namespace ants::ai
