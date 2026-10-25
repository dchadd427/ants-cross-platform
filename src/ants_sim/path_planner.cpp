#include "ants_sim/path_planner.hpp"

#include <algorithm>
#include <utility>

#include "ants_sim/lookup_shadow.hpp"

namespace ants::sim {

namespace {

struct RowColDelta {
    int32_t drow;
    int32_t dcol;
};

// 0x1004950: {int32 drow, int32 dcol}[8] = N, NE, E, SE, S, SW, W, NW (direction indices 0..7).
// Used by BuildPath to step from a node to its parent.
constexpr std::array<RowColDelta, 8> kDirDelta{{
    {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1},
}};

// 0x1002b28: int16[3][3] indexed [drow + 1][dcol + 1] -> direction index (centre unused).
constexpr std::array<uint32_t, 9> kDirOfDelta{{7, 0, 1, 6, 0, 2, 5, 4, 3}};

// FUN_01017531(a, b): direction of b - a from the raw deltas. Only ever called for 8-neighbours.
uint32_t direction_from_to(TileCoord a, TileCoord b) noexcept {
    const int32_t drow = b.y - a.y;
    const int32_t dcol = b.x - a.x;
    const int32_t index = (drow + 1) * 3 + (dcol + 1);
    return kDirOfDelta[static_cast<size_t>(index)];
}

// FUN_01020911: 16 * Chebyshev distance (row and column differences, unsigned max, << 4).
uint32_t cheb16(TileCoord a, TileCoord b) noexcept {
    const int32_t drow = a.y - b.y;
    const int32_t dcol = a.x - b.x;
    const uint32_t ar = static_cast<uint32_t>(drow < 0 ? -drow : drow);
    const uint32_t ac = static_cast<uint32_t>(dcol < 0 ? -dcol : dcol);
    return std::max(ar, ac) << 4u;
}

uint32_t cell_f(uint32_t cell) noexcept {
    return (cell & PathSearch::kGMask) + ((cell >> PathSearch::kHShift) & PathSearch::kHMask);
}

} // namespace

// ============================================================================
// PathGridPool
// ============================================================================

PathGridPool::PathGridPool(const PathGridPool& other) {
    *this = other;
}

PathGridPool& PathGridPool::operator=(const PathGridPool& other) {
    if (this == &other) return *this;
    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& d = slots_[i];
        const Slot& s = other.slots_[i];
        d.rows = s.rows;
        d.cols = s.cols;
        d.free = s.free;
        if (s.free) {
            // The block of a free slot is never read: acquire() allocates it again when it is not marked allocated, and the search that takes it zeroes it (PathSearch::init).
            // Copying it would be a megabyte of memory traffic for nothing. (clear() keeps the memory that this slot already has for its next use.)
            d.cells.clear();
            d.allocated = false;
        } else {
            d.cells = s.cells;
            d.allocated = s.allocated;
        }
    }
    return *this;
}

int PathGridPool::acquire(uint16_t rows, uint16_t cols) {
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[static_cast<size_t>(i)];
        if (!s.free) continue;
        if (!s.allocated || s.rows != rows || s.cols != cols) {
            s.cells.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), 0u);
            s.rows = rows;
            s.cols = cols;
            s.allocated = true;
        }
        s.free = false;
        return i;
    }
    return -1;
}

void PathGridPool::release(int slot) noexcept {
    if (slot < 0 || slot >= kSlots) return;
    slots_[static_cast<size_t>(slot)].free = true;
}

uint32_t* PathGridPool::cells(int slot) noexcept {
    if (slot < 0 || slot >= kSlots) return nullptr;
    return slots_[static_cast<size_t>(slot)].cells.data();
}

bool PathGridPool::in_use(int slot) const noexcept {
    if (slot < 0 || slot >= kSlots) return false;
    return !slots_[static_cast<size_t>(slot)].free;
}

int PathGridPool::free_count() const noexcept {
    int n = 0;
    for (const Slot& s : slots_) {
        if (s.free) ++n;
    }
    return n;
}

// ============================================================================
// PathSearch
// ============================================================================

PathSearch::PathSearch(uint32_t ant_id, TileCoord start, TileCoord goal, uint16_t rows, uint16_t cols,
                       PathGridPool& pool, bool wall_at_row_90, bool wide)
    : ant_id_(ant_id), start_(start), goal_(goal), rows_(rows), cols_(cols), pool_(&pool),
      sentinel_row_(wall_at_row_90 ? kSentinelRow : kNoSentinelRow), wide_(wide) {
    init();  // the ctor runs Init immediately (0x101989e)
}

// The copy of a search for the copy of its manager. Every member is copied by value; only the two pointers into the pool are not: they are the copy pool's, and the slot's
// cell block holds the same cells there (PathGridPool's copy copies the blocks of the slots in use). The slot itself is already marked in use in the copy pool, so this
// constructor does not take one (it must not run Init: that would zero the cells).
PathSearch::PathSearch(const PathSearch& other, PathGridPool& pool)
    : ant_id_(other.ant_id_),
      start_(other.start_),
      goal_(other.goal_),
      rows_(other.rows_),
      cols_(other.cols_),
      pool_(&pool),
      sentinel_row_(other.sentinel_row_),
      slot_(other.slot_),
      grid_(other.slot_ >= 0 ? pool.cells(other.slot_) : nullptr),
      heap_(other.heap_),
      finished_(other.finished_),
      path_(other.path_),
      wide_(other.wide_),
      beyond_(other.beyond_),
      limit_hit_(other.limit_hit_) {}

PathSearch::~PathSearch() {
    // 0x1019a39: only a request that holds a grid frees its slot.
    if (slot_ >= 0) pool_->release(slot_);
}

bool PathSearch::in_grid(TileCoord t) const noexcept {
    return t.x >= 0 && t.y >= 0 && t.x < static_cast<int32_t>(cols_) && t.y < static_cast<int32_t>(rows_);
}

uint32_t& PathSearch::cell(TileCoord t) noexcept {
    return grid_[static_cast<size_t>(t.y) * cols_ + static_cast<size_t>(t.x)];
}

uint32_t PathSearch::f_of(TileCoord t) const noexcept {
    if (wide_) return (grid_[static_cast<size_t>(t.y) * cols_ + static_cast<size_t>(t.x)] & kWideGMask) + cheb16(t, goal_);  // wide: h is not stored
    return cell_f(grid_[static_cast<size_t>(t.y) * cols_ + static_cast<size_t>(t.x)]);
}

// PathRequest::Init (FUN_010198d1).
void PathSearch::init() {
    const int slot = pool_->acquire(rows_, cols_);  // first free slot (0x10198e5-0x1019902)
    if (slot < 0) return;                            // no grid now; step() retries
    slot_ = slot;
    grid_ = pool_->cells(slot);
    std::fill_n(grid_, static_cast<size_t>(rows_) * cols_, 0u);  // FUN_0100607b memset
    heap_.clear();
    heap_.reserve(100);  // new Heap(elemSize 4, growBy 100)
    if (!in_grid(start_)) return;  // remake guard: the original always starts on a map tile

    uint32_t& sc = cell(start_);
    if (wide_) {  // g = 0, h is computed; a start that is already 8000 or more from the goal has passed the original's limit
        sc |= kOpenedBit;
        beyond_ = cheb16(start_, goal_) >= kFailF;
        heap_push(start_);
        return;
    }
    sc |= kOpenedBit;                                                            // 0x1019995
    sc = (sc & kKeepNotH) | ((cheb16(start_, goal_) & kHMask) << kHShift);       // g stays 0
    heap_push(start_);
}

// Heap Push (FUN_01019e20): append, then sift up while the parent's live f is strictly greater.
void PathSearch::heap_push(TileCoord t) {
    heap_.push_back(t);
    size_t i = heap_.size() - 1;
    const uint32_t key = f_of(t);
    while (i != 0) {
        const size_t p = i >> 1;  // 0-rooted heap: parent(i) = i >> 1, so the root has one child
        if (f_of(heap_[p]) <= key) break;  // ties stop
        std::swap(heap_[p], heap_[i]);
        i = p;
    }
}

// Heap Pop (FUN_01019f0a): take the root, move the last element to the root and sift it down.
TileCoord PathSearch::heap_pop() {
    const TileCoord top = heap_[0];
    heap_[0] = heap_.back();
    heap_.pop_back();
    const size_t count = heap_.size();
    if (count >= 1) {
        const uint32_t key = f_of(heap_[0]);
        size_t i = 0;
        size_t c = 0;  // first pass compares index 0 (the element itself) with index 1
        for (;;) {
            size_t best = c;
            uint32_t fb = f_of(heap_[c]);
            if (c + 2 <= count) {
                const uint32_t fr = f_of(heap_[c + 1]);
                if (fr < fb) {  // strict: ties keep the left child
                    best = c + 1;
                    fb = fr;
                }
            }
            if (key <= fb) break;
            std::swap(heap_[i], heap_[best]);
            i = best;
            c = 2 * best;
            if (c + 1 > count) break;
        }
    }
    return top;
}

// Neighbours (FUN_01019c31): order N, NE, E, SE, S, SW, W, NW. Edge neighbours are invalidated by writing
// the sentinel into their row only; note the two "else if" branches.
void PathSearch::neighbours(TileCoord cur, std::array<TileCoord, 8>& out) const noexcept {
    const int32_t r = cur.y;
    const int32_t c = cur.x;
    out[0] = TileCoord{c, r - 1};      // N
    out[1] = TileCoord{c + 1, r - 1};  // NE
    out[2] = TileCoord{c + 1, r};      // E
    out[3] = TileCoord{c + 1, r + 1};  // SE
    out[4] = TileCoord{c, r + 1};      // S
    out[5] = TileCoord{c - 1, r + 1};  // SW
    out[6] = TileCoord{c - 1, r};      // W
    out[7] = TileCoord{c - 1, r - 1};  // NW
    if (r == 0) {
        out[1].y = out[0].y = out[7].y = sentinel_row_;
    } else if (r == static_cast<int32_t>(rows_) - 1) {
        out[3].y = out[4].y = out[5].y = sentinel_row_;
    }
    if (c == 0) {
        out[5].y = out[6].y = out[7].y = sentinel_row_;
    } else if (c == static_cast<int32_t>(cols_) - 1) {
        out[3].y = out[2].y = out[1].y = sentinel_row_;
    }
}

// BuildPath (FUN_01019d28): follow the parent directions from the goal back to the start.
// path[0] = start ... path[count-1] = goal; start == goal gives count 1.
void PathSearch::build_path() {
    std::vector<TileCoord> reversed;
    const size_t limit = static_cast<size_t>(rows_) * cols_;
    TileCoord t = goal_;
    for (;;) {
        reversed.push_back(t);
        if (t == start_) break;
        const uint32_t d = (cell(t) >> kDirShift) & 7u;
        t.y += kDirDelta[d].drow;
        t.x += kDirDelta[d].dcol;
        // Remake guard: parent links strictly lead back to the start for any cost <= 8000, so this
        // never triggers; it only prevents an endless walk if a cost callback breaks that invariant.
        if (reversed.size() > limit || !in_grid(t)) {
            reversed.clear();
            break;
        }
    }
    path_.assign(reversed.rbegin(), reversed.rend());
}

// Search finished (success or failure): the original deletes the heap and NULLs its pointer.
void PathSearch::finish() noexcept {
    heap_.clear();
    heap_.shrink_to_fit();
    finished_ = true;
}

// PathRequest::Step (FUN_01019a66).
int PathSearch::step(uint16_t budget, const StepCostFn& cost) {
    if (slot_ < 0) {
        init();
        if (slot_ < 0) return 0;  // still no grid
    }
    if (finished_) return 1;  // remake guard: the original never steps a finished request
    if (wide_) return step_wide(budget, cost);

    uint16_t it = 0;
    if (!heap_.empty()) {
        while (it < budget) {
            ++it;
            const TileCoord cur = heap_pop();
            uint32_t& cc = cell(cur);
            if (cell_f(cc) >= kFailF) {  // 0x1019ae0, tested BEFORE the goal test
                path_.clear();           // count = 0
                finish();
                return 1;
            }
            if (cur == goal_) {  // 0x1019aec
                build_path();
                finish();
                return 1;
            }

            std::array<TileCoord, 8> nb{};
            neighbours(cur, nb);
            for (const TileCoord& n : nb) {
                if (n.y == sentinel_row_) continue;  // 0x1019b23 (rules 1: row -1, which no neighbour of a real tile otherwise has)
                if (!in_grid(n)) continue;          // remake guard: only reachable on 1-row / 1-column maps

                const uint32_t newg = cost(cur, n) + (cc & kGMask);  // StepCost + g(cur), unsigned
                uint32_t& nc = cell(n);
                if ((nc & (kOpenedBit | kClosedBit)) != 0u && (nc & kGMask) <= newg) continue;

                nc &= ~kClosedBit;                                                    // re-open
                nc = (nc & ~kGMask) | (newg & kGMask);                                // g
                nc = (nc & kKeepNotH) | ((cheb16(n, goal_) & kHMask) << kHShift);     // h
                nc = (nc & kKeepNotDir) | ((direction_from_to(n, cur) & 7u) << kDirShift);  // node -> parent
                if ((nc & kOpenedBit) == 0u) {  // pushed at most once: no decrease-key, no re-push
                    nc |= kOpenedBit;
                    heap_push(n);
                }
            }
            cc |= kClosedBit;  // 0x1019bf2
            if (heap_.empty()) break;
        }
    }
    if (!heap_.empty()) return 1;  // budget used up; keep the heap for the next slice

    path_.clear();  // open list exhausted: no path
    finish();
    return 1;
}

// The wide search (rules 2): PathRequest::Step with a g of 24 bits, an h that is computed (16 x Chebyshev, the value that the original stores), and the give-up rule described at
// the top of path_planner.hpp. The order of the pops, the neighbours, the improvement test, the re-opening and the "pushed at most once" quirk are the original's.
int PathSearch::step_wide(uint16_t budget, const StepCostFn& cost) {
    uint16_t it = 0;
    const uint16_t slice = limit_hit_ || budget < kDefaultBudget ? budget : kDefaultBudget;   // the original's 1,000 until a pop has passed its limit (decided at the start of the slice)
    if (!heap_.empty()) {
        while (it < slice) {
            ++it;
            const TileCoord cur = heap_pop();
            uint32_t& cc = cell(cur);
            if (!limit_hit_ && f_of(cur) >= kFailF) limit_hit_ = true;   // the original's f test: it would have given up at this pop
            if ((cc & kWideTaintBit) != 0u) {  // reached over a blocked step: its f is 8000 or more, the original gives up here
                if (!beyond_) {                // ... and so does this search, until a node has passed the limit
                    path_.clear();
                    finish();
                    return 1;
                }
                // Past the limit: a blocked node is a dead end, dropped (not expanded, not closed). It stays opened, so a later cheaper step into the same tile (only a step that depends on where
                // it comes from, a hill entered from its entrance, can be cheaper) updates its g and does not push it again: it is never expanded. This happens only at or after the pop at
                // which the original would have given up, where the original has no behavior to keep (every search it finishes is pop for pop the same here), and it is rare: about 1 random
                // order in 125,000 with hills and walls, where pushing the node again would have found a path that this search misses. Kept: "pushed at most once" is the original's rule.
                if (heap_.empty()) break;
                continue;
            }
            if (cur == goal_) {
                build_path();
                finish();
                return 1;
            }

            std::array<TileCoord, 8> nb{};
            neighbours(cur, nb);
            for (const TileCoord& n : nb) {
                if (n.y == sentinel_row_) continue;
                if (!in_grid(n)) continue;

                const uint32_t step = cost(cur, n);
                const uint32_t newg = step + (cc & kWideGMask);
                if (newg > kWideGMask || newg < step) continue;  // does not fit the 24 bits of g: the node is not reachable at this cost
                uint32_t& nc = cell(n);
                if ((nc & (kOpenedBit | kClosedBit)) != 0u && (nc & kWideGMask) <= newg) continue;

                nc &= ~kClosedBit;                                                              // re-open
                nc = (nc & ~kWideGMask) | newg;                                                 // g
                nc = (nc & kKeepNotDir) | ((direction_from_to(n, cur) & 7u) << kDirShift);      // node -> parent
                if (step >= kFailF) {
                    nc |= kWideTaintBit;
                } else {
                    nc &= ~kWideTaintBit;
                    if (newg + cheb16(n, goal_) >= kFailF) beyond_ = true;
                }
                if ((nc & kOpenedBit) == 0u) {  // pushed at most once: no decrease-key, no re-push
                    nc |= kOpenedBit;
                    heap_push(n);
                }
            }
            cc |= kClosedBit;
            if (heap_.empty()) break;
        }
    }
    if (!heap_.empty()) return 1;  // budget used up; keep the heap for the next slice

    path_.clear();  // open list exhausted: no path
    finish();
    return 1;
}

// ============================================================================
// PathManager
// ============================================================================

PathManager::PathManager() : pool_(std::make_unique<PathGridPool>()) {}

// The queued-ant index (queued_, see the header) is rebuilt from the queue after every copy, never copied: it cannot disagree with the queue it was built from.
void PathManager::set_queued(uint32_t ant_id, bool queued) {
    if (ant_id >= kFlaggedIds) {
        if (queued) ++queued_far_;
        else --queued_far_;
        return;
    }
    if (queued_.size() <= ant_id) {
        if (!queued) return;
        queued_.resize(static_cast<size_t>(ant_id) + 1, 0);
    }
    queued_[ant_id] = queued ? 1 : 0;
}

void PathManager::rebuild_queued() {
    queued_.clear();
    queued_far_ = 0;
    for (const auto& r : queue_) set_queued(r->ant_id(), true);
}

bool PathManager::scan_for(uint32_t ant_id) const noexcept {
    for (const auto& r : queue_) {
        if (r->ant_id() == ant_id) return true;
    }
    return false;
}

// Deep copy. The new pool is a copy of the old one (the slots in use with their cells), and every queued search is copied into the new queue in the same order and attached
// to the new pool, so that nothing is shared and the copy delivers the same paths in the same runs as the original.
PathManager::PathManager(const PathManager& other)
    : pool_(other.pool_ ? std::make_unique<PathGridPool>(*other.pool_) : nullptr) {
    for (const auto& search : other.queue_) queue_.push_back(std::make_unique<PathSearch>(*search, *pool_));
    rebuild_queued();
}

PathManager& PathManager::operator=(const PathManager& other) {
    if (this == &other) return *this;
    queue_.clear();                                    // release our searches while our pool still exists
    if (!other.pool_) {                                // a moved-from manager: no pool, no searches
        pool_.reset();
        return *this;
    }
    if (pool_) *pool_ = *other.pool_;                  // keeps the memory of our slots where it fits
    else pool_ = std::make_unique<PathGridPool>(*other.pool_);
    for (const auto& search : other.queue_) queue_.push_back(std::make_unique<PathSearch>(*search, *pool_));
    rebuild_queued();
    return *this;
}

PathManager::PathManager(PathManager&& other) noexcept
    : pool_(std::move(other.pool_)), queue_(std::move(other.queue_)), queued_(std::move(other.queued_)), queued_far_(other.queued_far_) {
    other.queue_.clear();   // a moved-from manager is empty, whatever the library leaves in a moved-from container
    other.queued_.clear();
    other.queued_far_ = 0;
}

PathManager& PathManager::operator=(PathManager&& other) noexcept {
    if (this != &other) {
        queue_.clear();  // release our searches while our pool still exists
        pool_ = std::move(other.pool_);
        queue_ = std::move(other.queue_);
        queued_ = std::move(other.queued_);
        queued_far_ = other.queued_far_;
        other.queue_.clear();
        other.queued_.clear();
        other.queued_far_ = 0;
    }
    return *this;
}

// PathMgr::Request (FUN_010246e8).
void PathManager::request(uint32_t ant_id, TileCoord from, TileCoord to, uint16_t rows, uint16_t cols, bool wall_at_row_90, bool wide) {
    if (!pool_) pool_ = std::make_unique<PathGridPool>();  // moved-from manager
    // Remove the FIRST queued request of the same ant (0x10246f8-0x102473b); destroying it frees its slot. The scan only runs for an ant that has one.
    if (has_request_for(ant_id)) {
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if ((*it)->ant_id() == ant_id) {
                queue_.erase(it);
                set_queued(ant_id, false);
                break;
            }
        }
    }
    // Construct (runs Init) and append at the tail (0x1024769).
    queue_.push_back(std::make_unique<PathSearch>(ant_id, from, to, rows, cols, *pool_, wall_at_row_90, wide));
    set_queued(ant_id, true);
}

// PathMgr::Run (0x1024786).
std::optional<PathManager::Delivery> PathManager::run(const CostProvider& cost, uint16_t budget) {
    if (queue_.empty()) return std::nullopt;
    // The original loops until one request runs a slice. A request fails to run only while all four grids
    // are held by other queued requests, and those do run, so a single pass over the queue always suffices;
    // the bound just makes termination explicit.
    for (size_t tries = queue_.size(); tries > 0; --tries) {
        std::unique_ptr<PathSearch> r = std::move(queue_.front());
        queue_.pop_front();
        const uint32_t ant_id = r->ant_id();
        set_queued(ant_id, false);   // (the flags say what the queue holds at every moment, also while the cost callback runs)
        const PathSearch::StepCostFn step_cost = [&cost, ant_id](TileCoord from, TileCoord to) {
            return cost(ant_id, from, to);
        };
        if (r->step(budget, step_cost) != 0) {
            if (r->finished()) {
                // Delivered, then released: the request (and its grid slot) goes away.
                return Delivery{ant_id, r->path()};
            }
            queue_.push_back(std::move(r));  // unfinished: back of the queue
            set_queued(ant_id, true);
            return std::nullopt;
        }
        queue_.push_back(std::move(r));  // no grid free: rotate and try the next request
        set_queued(ant_id, true);
    }
    return std::nullopt;
}

void PathManager::clear() {
    queue_.clear();
    queued_.clear();
    queued_far_ = 0;
}

bool PathManager::has_request_for(uint32_t ant_id) const noexcept {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return scan_for(ant_id);
#endif
    const bool queued = ant_id < kFlaggedIds ? (ant_id < queued_.size() && queued_[ant_id] != 0) : (queued_far_ != 0 && scan_for(ant_id));
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_sample(queue_.size())) {
        ++lookup_shadow_checks();
        if (queued != scan_for(ant_id)) lookup_shadow_fail("has_request_for", ant_id);
    }
#endif
    return queued;
}

} // namespace ants::sim
