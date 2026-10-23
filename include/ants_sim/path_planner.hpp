#pragma once

// Exact port of the original 1998 path planner: the PATHMGR task and its PathRequest A* searches.
//
// Ground truth (Capstone disassembly of Ants.exe, image base 0x01000000):
//   PathRequest ctor 0x10197ed / Init 0x10198d1 / dtor 0x10199e1   grid slot + heap + start node
//   PathRequest::Step 0x1019a66                                     A* slice (budget of expansions)
//   Neighbours 0x1019c31, BuildPath 0x1019d28                       N..NW order, sentinel row 0x5a
//   Heap Push 0x1019e20 / Pop 0x1019f0a                             0-rooted heap, live f reads
//   Dir 0x1017531 (table 0x1002b28), Cheb16 0x1020911, delta table 0x1004950
//   Grid pool 0x104b360 (4 slots, reset 0x10197b5)
//   PathMgr::Request 0x10246e8 / PathMgr::Run 0x1024786             FIFO round robin, 1000 per run
//
// The search is reproduced bit for bit, quirks included:
//   * 32-bit search-grid cells: g = bits 0..13, h = bits 14..26, parent direction (from the node TO
//     its parent) = bits 27..29, bit 30 = opened (ever pushed), bit 31 = closed (expanded).
//   * The open list is a binary min-heap of tiles whose comparisons read f = g + h live from the grid.
//     There is NO decrease-key: a node whose g improves is rewritten in place and never re-pushed, so
//     the heap order can go stale. This occasionally yields non-optimal paths and even a premature
//     "no path" (see the stale-heap tests); both are original behaviour.
//   * A popped node with f >= 8000 ends the search with failure, and that test precedes the goal test.
//   * Invalid neighbours at the map edge are marked by writing row 0x5a (90) and skipped by testing
//     row == 90, so a real tile in row 90 is never generated as a neighbour (no shipped map has more
//     than 60 rows). A map of rules 1 (Extended maps, docs/MAP_FORMAT.md) takes this wall down: its
//     searches mark an invalid neighbour with row -1 instead, which is never a real row, so every
//     row of a tall map can be walked. Rules 0 (every map that exists today, at most 81 rows) is
//     untouched: the wall is the default of every constructor below.
//
// The WIDE search (map rules word 2, docs/MAP_FORMAT.md "Orders on a big map"): the same A*, the same open list, the same neighbours, the same costs and the same tie-breaks, with
// a cell that has no 14-bit cost and no 13-bit estimate (g has 24 bits, h is computed from the tile and the goal, so the cell is still one dword) and a give-up rule that is the
// original's until a node has passed the original's limit: while no ordinary node has had f >= 8000 ("beyond" is clear) a popped node that was reached over a blocked step
// (a step of cost 8000 or more) ends the search exactly as the original's f >= 8000 test does, so every search the original finishes (a path, or no path before its limit)
// runs here pop for pop the same; once a node has passed the limit, such a node is dropped and the search goes on. The same holds for TIME: a slice has the original's 1,000
// expansions until the search has popped a node with f >= 8000 (the pop at which the original would have given up, limit_hit()); only the slices after that one get the larger
// budget of a big map (PathManager::wide_budget), so a rules-2 map that the original can play delivers every path in the same 50 ms run as rules 1 does. No map reaches rules 2 yet
// (the map files and their readers come with the next release); the engine takes this search only for a Grid of rules 2, and rules 0 and 1 run the search above unchanged.
//
// Coordinates: the original's tile is {int16 row, int16 col}. This port uses ants::sim::TileCoord with
// x = column and y = row throughout.
//
// Remake-only guards (unreachable with the original's inputs; they only turn out-of-range memory
// accesses into defined behaviour): start tiles outside the grid, maps with a single row or column,
// grid slots reused with different map dimensions, parent chains longer than the map, and a PATHMGR
// run in which no request can obtain a grid.

#include "ants_sim/grid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ants::sim {

/**
 * @brief The four global search-grid slots of the original (0x104b360, 8 bytes each: {grid, free}).
 *
 * A slot's cell block is allocated lazily by the first search that takes it and kept for reuse when the
 * search releases the slot.
 */
class PathGridPool {
public:
    static constexpr int kSlots = 4;

    PathGridPool() = default;
    /**
     * @brief Value copy (a copy of the simulation engine, SimulationEngine's copy constructor): the slots keep their state, and the cell blocks of the slots that a search holds
     *        are copied. The block of a FREE slot is not: its content is never read (the search that takes the slot zeroes the whole block, PathSearch::init), so the copy
     *        leaves such a slot unallocated and acquire() allocates it on the next use. Nothing is shared between a pool and its copy.
     */
    PathGridPool(const PathGridPool& other);
    PathGridPool& operator=(const PathGridPool& other);

    /**
     * @brief Takes the first free slot (PathRequest::Init, 0x10198e3-0x101994f).
     *
     * Allocates the slot's rows x cols cell block if it has none yet (the original allocates once per
     * slot; this port also reallocates when the dimensions differ, which the original never needs because
     * PATHMGR is recreated for every map). The cells are NOT cleared here; the search zeroes them.
     * @return the slot index, or -1 when all four slots are in use.
     */
    int acquire(uint16_t rows, uint16_t cols);

    /** @brief Marks a slot free again (PathRequest dtor, 0x1019a42). Invalid indices are ignored. */
    void release(int slot) noexcept;

    /** @brief Row-major cell block of a slot (rows x cols dwords), or nullptr for an invalid index. */
    uint32_t* cells(int slot) noexcept;

    /** @brief True while a search holds the slot. */
    bool in_use(int slot) const noexcept;

    /** @brief Number of free slots. */
    int free_count() const noexcept;

private:
    struct Slot {
        std::vector<uint32_t> cells;
        uint16_t rows{0};
        uint16_t cols{0};
        bool allocated{false};
        bool free{true};
    };

    std::array<Slot, kSlots> slots_{};
};

/**
 * @brief One path request: the original PathRequest object and its sliced A* search.
 *
 * The constructor runs Init immediately (0x101989e): it takes a grid slot, zeroes the grid, marks the start
 * opened with g = 0 and h = Cheb16(start, goal) and pushes it. Without a free slot the request simply has no
 * grid yet; step() retries Init.
 */
class PathSearch {
public:
    using StepCostFn = std::function<uint32_t(TileCoord from, TileCoord to)>;

    // Search-grid cell layout (one dword per tile).
    static constexpr uint32_t kGMask = 0x00003fffu;       ///< bits 0..13: g
    static constexpr uint32_t kHMask = 0x00001fffu;       ///< bits 14..26 (after the shift): h
    static constexpr uint32_t kHShift = 14u;
    static constexpr uint32_t kDirShift = 27u;             ///< bits 27..29: direction to the parent
    static constexpr uint32_t kOpenedBit = 0x40000000u;    ///< bit 30: pushed at least once
    static constexpr uint32_t kClosedBit = 0x80000000u;    ///< bit 31: expanded
    static constexpr uint32_t kKeepNotH = 0xf8003fffu;     ///< mask clearing the h field
    static constexpr uint32_t kKeepNotDir = 0xc7ffffffu;   ///< mask clearing the direction field

    // The wide search's cell (one dword as well): g = bits 0..23, bit 24 = "reached over a blocked step", the parent direction, opened and closed bits where they are above.
    static constexpr uint32_t kWideGMask = 0x00ffffffu;    ///< bits 0..23: g (16,777,215)
    static constexpr uint32_t kWideTaintBit = 0x01000000u; ///< bit 24: the last step into this node was blocked (cost >= kFailF)

    static constexpr uint32_t kFailF = 8000u;              ///< popped f >= 8000 -> no path (0x1019ae0)
    static constexpr uint16_t kDefaultBudget = 1000u;      ///< expansions per PATHMGR run (0x10247ac)
    static constexpr uint32_t kWideBudgetFloor = 1000u;    ///< the wide search's budget never goes below the original's
    static constexpr uint32_t kWideBudgetCellsPerStep = 32u; ///< ... and grows with the map: one step per 32 cells (512 x 512 = 8,192 steps)
    static constexpr int32_t kSentinelRow = 0x5a;          ///< invalid-neighbour marker (0x1019cbd)
    static constexpr int32_t kNoSentinelRow = -1;          ///< the marker when the wall is down (rules 1): never a real row

    /**
     * @param ant_id  id of the requesting ant (passed back with the result)
     * @param start   the ant's current tile (becomes path[0])
     * @param goal    destination tile (becomes path[count-1])
     * @param rows    map rows (the original copies map+0xd0 at creation)
     * @param cols    map columns (map+0xd2)
     * @param pool    grid pool; must outlive this search
     * @param wall_at_row_90  the default true means the wall is UP (rules 0): every engine caller must pass grid_.rules() == 0. True (the original, rules 0): a real tile in row 90 is never entered; false (rules 1): the wall is down and every row can be walked
     * @param wide  false (the default, rules 0 and 1): the original's search, bit for bit. True (rules 2): the wide search described at the top of this file; every engine caller must pass grid_.rules() == 2
     */
    PathSearch(uint32_t ant_id, TileCoord start, TileCoord goal, uint16_t rows, uint16_t cols, PathGridPool& pool, bool wall_at_row_90 = true, bool wide = false);
    ~PathSearch();

    PathSearch(const PathSearch&) = delete;
    PathSearch& operator=(const PathSearch&) = delete;

    /**
     * @brief A copy of a search for the copy of its manager: the same state (the open list, the finished path), attached to @p pool, the copy's OWN pool, whose slot
     *        `other` holds is already in use in that pool (PathGridPool's copy) and whose cell block holds the same cells. A plain copy of the pointers would leave
     *        the two searches working on one block of cells; this constructor is the only way to copy a search, and PathManager's copy is the only caller.
     */
    PathSearch(const PathSearch& other, PathGridPool& pool);

    /**
     * @brief Runs one A* slice of at most @p budget node expansions (PathRequest::Step, 0x1019a66). The wide search takes at most the original's 1,000 of them until its own
     *        limit_hit() (a smaller @p budget is always kept).
     *
     * @return 0 when no grid slot could be obtained (nothing ran), 1 otherwise. After a return of 1 the
     *         search is either finished (see finished(), path()) or still has open nodes for a later slice.
     *         Stepping a finished search does nothing and returns 1 (the original never does this).
     */
    int step(uint16_t budget, const StepCostFn& cost);

    /** @brief True once the search has completed, successfully or not (the original's heap is freed). */
    bool finished() const noexcept { return finished_; }

    /** @brief True for the wide search (rules 2). */
    bool wide() const noexcept { return wide_; }

    /** @brief The wide search only: true once a node with f >= 8000 (the original's limit) has been given to the open list. Always false for the original's search. */
    bool beyond() const noexcept { return beyond_; }

    /** @brief The wide search only: true once a node with f >= 8000 has been popped, the pop at which the original's search would have given up. From the next slice on the search gets the big budget. Always false for the original's search. */
    bool limit_hit() const noexcept { return limit_hit_; }

    /** @brief True while the search holds a grid slot. */
    bool has_grid() const noexcept { return slot_ >= 0; }

    uint32_t ant_id() const noexcept { return ant_id_; }
    TileCoord start() const noexcept { return start_; }
    TileCoord goal() const noexcept { return goal_; }

    /** @brief Resulting waypoints: start ... goal, both included. Empty = no path (original count 0). */
    const std::vector<TileCoord>& path() const noexcept { return path_; }

private:
    void init();
    bool in_grid(TileCoord t) const noexcept;
    uint32_t& cell(TileCoord t) noexcept;
    uint32_t f_of(TileCoord t) const noexcept;
    void heap_push(TileCoord t);
    TileCoord heap_pop();
    void neighbours(TileCoord cur, std::array<TileCoord, 8>& out) const noexcept;
    void build_path();
    void finish() noexcept;
    int step_wide(uint16_t budget, const StepCostFn& cost);

    uint32_t ant_id_;
    TileCoord start_;
    TileCoord goal_;
    uint16_t rows_;
    uint16_t cols_;
    PathGridPool* pool_;
    int32_t sentinel_row_{kSentinelRow};  // kSentinelRow (rules 0) or kNoSentinelRow (rules 1): the row that marks an invalid neighbour
    int slot_{-1};
    uint32_t* grid_{nullptr};
    std::vector<TileCoord> heap_;
    bool finished_{false};
    std::vector<TileCoord> path_;
    bool wide_{false};     // the wide search (rules 2)
    bool beyond_{false};   // wide search: a node with f >= kFailF has been given to the open list (the original's limit is passed)
    bool limit_hit_{false}; // wide search: a node with f >= kFailF has been popped (the original would have ended here): the slices after this one get the big budget
};

/**
 * @brief The PATHMGR task: a FIFO queue of path requests sharing one four-slot grid pool.
 *
 * The simulation calls run() once per PATHMGR period (50 ms). Each run gives exactly one request (one that
 * has or can obtain a grid) a slice of up to 1000 expansions; unfinished requests go to the back of the
 * queue, so at most one path is delivered per run. The remake creates one manager per team, mirroring one
 * PATHMGR per player machine.
 */
class PathManager {
public:
    struct Delivery {
        uint32_t ant_id;
        std::vector<TileCoord> path;  ///< start ... goal; empty = search failed (original count 0)
    };

    using CostProvider = std::function<uint32_t(uint32_t ant_id, TileCoord from, TileCoord to)>;

    /** @brief The budget of a wide search that has passed the original's limit (limit_hit()), for a map of @p rows x @p cols: max(1000, cells / 32), at most 65,535 (exactly 1,000 up to 32,031 cells). Before that a wide search gets the original's 1,000. */
    static constexpr uint16_t wide_budget(uint32_t rows, uint32_t cols) noexcept {
        const uint64_t steps = static_cast<uint64_t>(rows) * cols / PathSearch::kWideBudgetCellsPerStep;
        return static_cast<uint16_t>(steps < PathSearch::kWideBudgetFloor ? PathSearch::kWideBudgetFloor : (steps > 65535u ? 65535u : steps));
    }

    PathManager();
    ~PathManager() = default;

    // Moving keeps the searches valid: the pool is heap-allocated and moves with its queue. A moved-from
    // manager is empty and creates a fresh pool on its next request().
    PathManager(PathManager&& other) = default;
    PathManager& operator=(PathManager&& other) noexcept;
    /**
     * @brief Deep copy (a copy of the simulation engine): a new pool with the same slots, and a copy of every queued search, in queue order, attached to the new pool, so
     *        the copy goes on with the half-done searches exactly where the original is and delivers the same paths in the same runs. Nothing is shared. The assignment
     *        keeps the memory that the target's pool and queue already hold where the shapes fit.
     */
    PathManager(const PathManager& other);
    PathManager& operator=(const PathManager& other);

    /**
     * @brief PathMgr::Request (0x10246e8): removes the first queued request of the same ant (releasing its
     *        grid slot), then appends a new request, which takes a free grid slot immediately if there is one.
     *        @p wall_at_row_90 is PathSearch's. Its default true means the wall is up: every engine caller must pass grid_.rules() == 0.
     *        @p wide is PathSearch's: every engine caller must pass grid_.rules() == 2.
     */
    void request(uint32_t ant_id, TileCoord from, TileCoord to, uint16_t rows, uint16_t cols, bool wall_at_row_90 = true, bool wide = false);

    /**
     * @brief One PATHMGR run (0x1024786).
     * @param budget  expansions for the one request that runs (the original's 1,000; a map of rules 2 passes wide_budget(rows, cols), which only a wide search that has passed the original's limit gets: until then it takes 1,000)
     * @return the finished search's result if this run completed one, otherwise std::nullopt.
     */
    std::optional<Delivery> run(const CostProvider& cost, uint16_t budget = PathSearch::kDefaultBudget);

    /** @brief Drops every queued request and releases their grid slots. */
    void clear();

    /** @brief Number of queued requests. */
    size_t pending() const noexcept { return queue_.size(); }

    /** @brief True if a request of this ant is queued. */
    bool has_request_for(uint32_t ant_id) const noexcept;

    /** @brief Calls fn(ant_id, start, goal, has_grid, finished) for every queued request in queue order (state hash, diagnostics). */
    template <typename Fn>
    void for_each_request(Fn&& fn) const {
        for (const auto& r : queue_) fn(r->ant_id(), r->start(), r->goal(), r->has_grid(), r->finished());
    }

private:
    // Declared before the queue so that it is destroyed after the searches that reference it.
    std::unique_ptr<PathGridPool> pool_;
    std::deque<std::unique_ptr<PathSearch>> queue_;
};

} // namespace ants::sim
