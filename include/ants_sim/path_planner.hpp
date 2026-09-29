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
//     than 60 rows).
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
    PathGridPool(const PathGridPool&) = delete;
    PathGridPool& operator=(const PathGridPool&) = delete;

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

    static constexpr uint32_t kFailF = 8000u;              ///< popped f >= 8000 -> no path (0x1019ae0)
    static constexpr uint16_t kDefaultBudget = 1000u;      ///< expansions per PATHMGR run (0x10247ac)
    static constexpr int32_t kSentinelRow = 0x5a;          ///< invalid-neighbour marker (0x1019cbd)

    /**
     * @param ant_id  id of the requesting ant (passed back with the result)
     * @param start   the ant's current tile (becomes path[0])
     * @param goal    destination tile (becomes path[count-1])
     * @param rows    map rows (the original copies map+0xd0 at creation)
     * @param cols    map columns (map+0xd2)
     * @param pool    grid pool; must outlive this search
     */
    PathSearch(uint32_t ant_id, TileCoord start, TileCoord goal, uint16_t rows, uint16_t cols, PathGridPool& pool);
    ~PathSearch();

    PathSearch(const PathSearch&) = delete;
    PathSearch& operator=(const PathSearch&) = delete;

    /**
     * @brief Runs one A* slice of at most @p budget node expansions (PathRequest::Step, 0x1019a66).
     *
     * @return 0 when no grid slot could be obtained (nothing ran), 1 otherwise. After a return of 1 the
     *         search is either finished (see finished(), path()) or still has open nodes for a later slice.
     *         Stepping a finished search does nothing and returns 1 (the original never does this).
     */
    int step(uint16_t budget, const StepCostFn& cost);

    /** @brief True once the search has completed, successfully or not (the original's heap is freed). */
    bool finished() const noexcept { return finished_; }

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

    uint32_t ant_id_;
    TileCoord start_;
    TileCoord goal_;
    uint16_t rows_;
    uint16_t cols_;
    PathGridPool* pool_;
    int slot_{-1};
    uint32_t* grid_{nullptr};
    std::vector<TileCoord> heap_;
    bool finished_{false};
    std::vector<TileCoord> path_;
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

    PathManager();
    ~PathManager() = default;

    PathManager(const PathManager&) = delete;
    PathManager& operator=(const PathManager&) = delete;
    // Moving keeps the searches valid: the pool is heap-allocated and moves with its queue. A moved-from
    // manager is empty and creates a fresh pool on its next request().
    PathManager(PathManager&& other) = default;
    PathManager& operator=(PathManager&& other) noexcept;

    /**
     * @brief PathMgr::Request (0x10246e8): removes the first queued request of the same ant (releasing its
     *        grid slot), then appends a new request, which takes a free grid slot immediately if there is one.
     */
    void request(uint32_t ant_id, TileCoord from, TileCoord to, uint16_t rows, uint16_t cols);

    /**
     * @brief One PATHMGR run (0x1024786).
     * @return the finished search's result if this run completed one, otherwise std::nullopt.
     */
    std::optional<Delivery> run(const CostProvider& cost);

    /** @brief Drops every queued request and releases their grid slots. */
    void clear();

    /** @brief Number of queued requests. */
    size_t pending() const noexcept { return queue_.size(); }

    /** @brief True if a request of this ant is queued. */
    bool has_request_for(uint32_t ant_id) const noexcept;

private:
    // Declared before the queue so that it is destroyed after the searches that reference it.
    std::unique_ptr<PathGridPool> pool_;
    std::deque<std::unique_ptr<PathSearch>> queue_;
};

} // namespace ants::sim
