#include "ants_sim/path_planner.hpp"

#include <algorithm>
#include <utility>

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
                       PathGridPool& pool)
    : ant_id_(ant_id), start_(start), goal_(goal), rows_(rows), cols_(cols), pool_(&pool) {
    init();  // the ctor runs Init immediately (0x101989e)
}

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
        out[1].y = out[0].y = out[7].y = kSentinelRow;
    } else if (r == static_cast<int32_t>(rows_) - 1) {
        out[3].y = out[4].y = out[5].y = kSentinelRow;
    }
    if (c == 0) {
        out[5].y = out[6].y = out[7].y = kSentinelRow;
    } else if (c == static_cast<int32_t>(cols_) - 1) {
        out[3].y = out[2].y = out[1].y = kSentinelRow;
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
                if (n.y == kSentinelRow) continue;  // 0x1019b23
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

// ============================================================================
// PathManager
// ============================================================================

PathManager::PathManager() : pool_(std::make_unique<PathGridPool>()) {}

PathManager& PathManager::operator=(PathManager&& other) noexcept {
    if (this != &other) {
        queue_.clear();  // release our searches while our pool still exists
        pool_ = std::move(other.pool_);
        queue_ = std::move(other.queue_);
    }
    return *this;
}

// PathMgr::Request (FUN_010246e8).
void PathManager::request(uint32_t ant_id, TileCoord from, TileCoord to, uint16_t rows, uint16_t cols) {
    if (!pool_) pool_ = std::make_unique<PathGridPool>();  // moved-from manager
    // Remove the FIRST queued request of the same ant (0x10246f8-0x102473b); destroying it frees its slot.
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if ((*it)->ant_id() == ant_id) {
            queue_.erase(it);
            break;
        }
    }
    // Construct (runs Init) and append at the tail (0x1024769).
    queue_.push_back(std::make_unique<PathSearch>(ant_id, from, to, rows, cols, *pool_));
}

// PathMgr::Run (0x1024786).
std::optional<PathManager::Delivery> PathManager::run(const CostProvider& cost) {
    if (queue_.empty()) return std::nullopt;
    // The original loops until one request runs a slice. A request fails to run only while all four grids
    // are held by other queued requests, and those do run, so a single pass over the queue always suffices;
    // the bound just makes termination explicit.
    for (size_t tries = queue_.size(); tries > 0; --tries) {
        std::unique_ptr<PathSearch> r = std::move(queue_.front());
        queue_.pop_front();
        const uint32_t ant_id = r->ant_id();
        const PathSearch::StepCostFn step_cost = [&cost, ant_id](TileCoord from, TileCoord to) {
            return cost(ant_id, from, to);
        };
        if (r->step(PathSearch::kDefaultBudget, step_cost) != 0) {
            if (r->finished()) {
                // Delivered, then released: the request (and its grid slot) goes away.
                return Delivery{ant_id, r->path()};
            }
            queue_.push_back(std::move(r));  // unfinished: back of the queue
            return std::nullopt;
        }
        queue_.push_back(std::move(r));  // no grid free: rotate and try the next request
    }
    return std::nullopt;
}

void PathManager::clear() {
    queue_.clear();
}

bool PathManager::has_request_for(uint32_t ant_id) const noexcept {
    for (const auto& r : queue_) {
        if (r->ant_id() == ant_id) return true;
    }
    return false;
}

} // namespace ants::sim
