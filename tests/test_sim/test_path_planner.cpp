// Unit tests for the exact port of the original PATHMGR / PathRequest A* (include/ants_sim/path_planner.hpp).
//
// Expected paths, expansion counts and PATHMGR schedules were produced by an independent reference model of
// the disassembled algorithm and cross-checked against this port on thousands of random grids (identical
// paths, expansion counts and slice counts). Coordinates are TileCoord{x = column, y = row}.
//
// The cost callback used here mimics the terrain part of the original Ant::StepCost (FUN_01020951):
//   cost(a, b) = (Ca + Cb) >> 1 orthogonally, (uint32)((double)(Ca + Cb) * 1.4) >> 1 diagonally,
//   8000 if either end is impassable, with weights grass 20, sand 16, mud 48, dirt 24, water 8000.
// Map legend: '.' grass, 's' sand, 'm' mud, 'd' dirt, '~' water, '#' rock (a blocking object standing on
// grass: entering it costs 8000, like the object test on the destination tile; leaving it costs as grass).

#include "ants_sim/path_planner.hpp"
#include "ants_sim/grid.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [PATH PLANNER SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(66) << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    } catch (...) {
        std::cout << "FAILED! Unknown exception thrown\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) {
        std::cout << "PASS\n";
    }
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

static std::string fmt_path(const std::vector<TileCoord>& p) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < p.size(); ++i) {
        os << (i ? " " : "") << "(" << p[i].x << "," << p[i].y << ")";
    }
    os << "]";
    return os.str();
}

// Compares a path with an expected tile list, printing both on mismatch.
#define ASSERT_PATH(actual, ...) \
    do { \
        ++g_assert_count; \
        const std::vector<TileCoord> expected_path_ = __VA_ARGS__; \
        if ((actual) != expected_path_) { \
            std::cout << "FAILED!\n    Path mismatch at " << __FILE__ << ":" << __LINE__ \
                      << "\n      expected " << fmt_path(expected_path_) \
                      << "\n      actual   " << fmt_path(actual) << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

// ============================================================================
// Fixtures
// ============================================================================

namespace {

constexpr uint32_t kBlocked = 8000;

struct TerrainMap {
    std::vector<std::string> rows;  // rows[y][x]

    static TerrainMap filled(int w, int h, char c = '.') {
        TerrainMap m;
        m.rows.assign(static_cast<size_t>(h), std::string(static_cast<size_t>(w), c));
        return m;
    }

    uint16_t height() const { return static_cast<uint16_t>(rows.size()); }
    uint16_t width() const { return static_cast<uint16_t>(rows.empty() ? 0 : rows[0].size()); }
    char at(TileCoord t) const { return rows[static_cast<size_t>(t.y)][static_cast<size_t>(t.x)]; }
    void set(int x, int y, char c) { rows[static_cast<size_t>(y)][static_cast<size_t>(x)] = c; }

    static uint32_t weight(char c) {
        switch (c) {
            case 's': return 16;
            case 'm': return 48;
            case 'd': return 24;
            case '~': return kBlocked;
            case '.':
            case '#':
            default:  return 20;
        }
    }

    uint32_t cost(TileCoord from, TileCoord to) const {
        if (at(to) == '#') return kBlocked;
        const uint32_t ca = weight(at(from));
        const uint32_t cb = weight(at(to));
        if (ca == kBlocked || cb == kBlocked) return kBlocked;
        uint32_t s = ca + cb;
        if (from.x != to.x && from.y != to.y) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
        return s >> 1;
    }

    PathSearch::StepCostFn step_cost() const {
        return [this](TileCoord a, TileCoord b) { return cost(a, b); };
    }

    PathManager::CostProvider provider() const {
        return [this](uint32_t, TileCoord a, TileCoord b) { return cost(a, b); };
    }
};

struct SearchOutcome {
    std::vector<TileCoord> path;
    int slices{0};
};

// Runs one PathSearch to completion with the given per-slice budget.
SearchOutcome run_search(TileCoord start, TileCoord goal, uint16_t rows, uint16_t cols,
                         const PathSearch::StepCostFn& cost, uint16_t budget = PathSearch::kDefaultBudget) {
    PathGridPool pool;
    PathSearch search(1, start, goal, rows, cols, pool);
    SearchOutcome out;
    while (!search.finished() && out.slices < 1000000) {
        search.step(budget, cost);
        ++out.slices;
    }
    out.path = search.path();
    return out;
}

SearchOutcome run_search(const TerrainMap& map, TileCoord start, TileCoord goal,
                         uint16_t budget = PathSearch::kDefaultBudget) {
    return run_search(start, goal, map.height(), map.width(), map.step_cost(), budget);
}

// A slice with budget 1 performs exactly one expansion (heap pop), so the slice count is the expansion count.
int count_expansions(const TerrainMap& map, TileCoord start, TileCoord goal) {
    return run_search(map, start, goal, 1).slices;
}

uint32_t path_cost(const std::vector<TileCoord>& path, const PathSearch::StepCostFn& cost) {
    uint32_t total = 0;
    for (size_t i = 1; i < path.size(); ++i) total += cost(path[i - 1], path[i]);
    return total;
}

bool is_connected(const std::vector<TileCoord>& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i - 1].chebyshev_dist(path[i]) != 1) return false;
    }
    return true;
}

// Plain Dijkstra over the same 8-connected graph (edges costing >= 8000 are impassable). Used only to show
// what an exact shortest-path search would return.
uint32_t dijkstra_cost(TileCoord start, TileCoord goal, uint16_t rows, uint16_t cols,
                       const PathSearch::StepCostFn& cost) {
    constexpr uint32_t kInf = std::numeric_limits<uint32_t>::max();
    const auto index = [cols](TileCoord t) { return static_cast<size_t>(t.y) * cols + static_cast<size_t>(t.x); };
    std::vector<uint32_t> dist(static_cast<size_t>(rows) * cols, kInf);
    using Item = std::pair<uint32_t, TileCoord>;
    const auto greater = [](const Item& a, const Item& b) { return a.first > b.first; };
    std::priority_queue<Item, std::vector<Item>, decltype(greater)> open(greater);
    dist[index(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const Item top = open.top();
        open.pop();
        if (top.first != dist[index(top.second)]) continue;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const TileCoord n{top.second.x + dx, top.second.y + dy};
                if (n.x < 0 || n.y < 0 || n.x >= cols || n.y >= rows) continue;
                const uint32_t w = cost(top.second, n);
                if (w >= kBlocked) continue;
                if (top.first + w < dist[index(n)]) {
                    dist[index(n)] = top.first + w;
                    open.push({top.first + w, n});
                }
            }
        }
    }
    return dist[index(goal)];
}

// 60 x 60 grass with a rock wall at x = 30 for y = 0..57 (gap at y = 58..59): searches from one side to
// the other must expand well over 1000 nodes.
TerrainMap wall_map() {
    TerrainMap m = TerrainMap::filled(60, 60);
    for (int y = 0; y <= 57; ++y) m.set(30, y, '#');
    return m;
}

// Runs the manager for `runs` PATHMGR runs; entry i is the ant id delivered by run i + 1, or 0 if none.
std::vector<uint32_t> run_schedule(PathManager& mgr, const PathManager::CostProvider& cost, int runs,
                                   std::vector<PathManager::Delivery>* deliveries = nullptr) {
    std::vector<uint32_t> out;
    for (int i = 0; i < runs; ++i) {
        const std::optional<PathManager::Delivery> d = mgr.run(cost);
        out.push_back(d ? d->ant_id : 0u);
        if (d && deliveries) deliveries->push_back(*d);
    }
    return out;
}

const PathManager::Delivery* find_delivery(const std::vector<PathManager::Delivery>& ds, uint32_t ant) {
    for (const auto& d : ds) {
        if (d.ant_id == ant) return &d;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// SUITE 1: Cost fixture and basic paths
// ============================================================================
void run_suite_1_basic_paths() {
    TEST_SUITE("Suite 1: Cost Fixture, Straight / Diagonal Paths, Corner Cutting");

    TEST_CASE("1.1 Terrain cost fixture matches the StepCost table (grass diag 28, mud diag 67)") {
        // Verified table of re_pathfinder.md 3.11 (x87 53-bit double: 40 * 1.4 rounds to exactly 56.0).
        struct Pair { char a; char b; uint32_t orth; uint32_t diag; };
        const Pair table[] = {
            {'.', '.', 20, 28}, {'.', 's', 18, 25}, {'.', 'm', 34, 47}, {'.', 'd', 22, 30},
            {'s', 's', 16, 22}, {'s', 'm', 32, 44}, {'s', 'd', 20, 28}, {'m', 'm', 48, 67},
            {'m', 'd', 36, 50}, {'d', 'd', 24, 33},
        };
        for (const Pair& p : table) {
            TerrainMap m;
            m.rows = {std::string{p.a, p.b}, std::string{p.b, p.b}};
            ASSERT_EQ(m.cost({0, 0}, {1, 0}), p.orth);
            ASSERT_EQ(m.cost({0, 0}, {1, 1}), p.diag);
        }
        TerrainMap w;
        w.rows = {".~", "#."};
        ASSERT_EQ(w.cost({0, 0}, {1, 0}), kBlocked);   // into water
        ASSERT_EQ(w.cost({1, 0}, {1, 1}), kBlocked);   // out of water
        ASSERT_EQ(w.cost({1, 1}, {0, 1}), kBlocked);   // onto a rock
        ASSERT_EQ(w.cost({0, 1}, {1, 1}), 20u);        // off a rock (grass underneath)
    } TEST_END();

    TEST_CASE("1.2 Straight east path on open grass includes start and goal") {
        const TerrainMap m = TerrainMap::filled(20, 12);
        const SearchOutcome r = run_search(m, {2, 5}, {9, 5});
        ASSERT_PATH(r.path, {{2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 5}, {8, 5}, {9, 5}});
        ASSERT_EQ(r.path.size(), 8u);
        ASSERT_EQ(r.slices, 1);
        ASSERT_EQ(count_expansions(m, {2, 5}, {9, 5}), 24);
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 140u);
    } TEST_END();

    TEST_CASE("1.3 Straight south path on open grass") {
        const TerrainMap m = TerrainMap::filled(20, 12);
        const SearchOutcome r = run_search(m, {4, 1}, {4, 9});
        ASSERT_PATH(r.path, {{4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}, {4, 6}, {4, 7}, {4, 8}, {4, 9}});
        ASSERT_EQ(count_expansions(m, {4, 1}, {4, 9}), 31);
    } TEST_END();

    TEST_CASE("1.4 Pure diagonal path on open grass (5 x 28)") {
        const TerrainMap m = TerrainMap::filled(20, 12);
        const SearchOutcome r = run_search(m, {2, 2}, {7, 7});
        ASSERT_PATH(r.path, {{2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}});
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 140u);
        ASSERT_EQ(count_expansions(m, {2, 2}, {7, 7}), 24);
    } TEST_END();

    TEST_CASE("1.5 start == goal gives a one-tile path after one expansion") {
        const TerrainMap m = TerrainMap::filled(16, 16);
        const SearchOutcome r = run_search(m, {4, 4}, {4, 4});
        ASSERT_PATH(r.path, {{4, 4}});
        ASSERT_EQ(count_expansions(m, {4, 4}, {4, 4}), 1);
        // Even on an impassable tile: f(start) = 0 and the cost callback is never consulted.
        TerrainMap w = TerrainMap::filled(4, 4);
        w.set(1, 1, '~');
        ASSERT_PATH(run_search(w, {1, 1}, {1, 1}).path, {{1, 1}});
    } TEST_END();

    TEST_CASE("1.6 Corner cutting is allowed past blocked orthogonal tiles (rock)") {
        TerrainMap m = TerrainMap::filled(8, 8);
        m.set(5, 4, '#');  // north of the start
        m.set(6, 5, '#');  // east of the start
        const SearchOutcome r = run_search(m, {5, 5}, {6, 4});
        ASSERT_PATH(r.path, {{5, 5}, {6, 4}});
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 28u);
        ASSERT_EQ(count_expansions(m, {5, 5}, {6, 4}), 2);
    } TEST_END();

    TEST_CASE("1.7 Corner cutting is allowed between two water tiles") {
        TerrainMap m = TerrainMap::filled(8, 8);
        m.set(5, 4, '~');
        m.set(6, 5, '~');
        ASSERT_PATH(run_search(m, {5, 5}, {6, 4}).path, {{5, 5}, {6, 4}});
    } TEST_END();
}

// ============================================================================
// SUITE 2: Neighbour-order tie-breaking
// ============================================================================
void run_suite_2_tie_breaking() {
    TEST_SUITE("Suite 2: Neighbour Order N, NE, E, SE, S, SW, W, NW and Heap Tie-Breaking");

    // Hand-traced example (x, y): start (5,5), goal (6,7). The first expansion pushes the eight neighbours
    // with f = S 36, SE 44, E 52, W 52, SW 60, N 68, NE 76, NW 76. S (5,6) is expanded next and reaches the
    // goal diagonally with g = 20 + 28 = 48. When SE (6,6) is expanded it offers 28 + 20 = 48, which is not
    // strictly better (cells are only rewritten when g(nb) > newg), so the goal keeps its first parent.
    const TerrainMap field = TerrainMap::filled(16, 16);

    TEST_CASE("2.1 (5,5)->(6,7): S first, then SE (first equal-cost parent is kept)") {
        ASSERT_PATH(run_search(field, {5, 5}, {6, 7}).path, {{5, 5}, {5, 6}, {6, 7}});
        ASSERT_EQ(count_expansions(field, {5, 5}, {6, 7}), 4);
    } TEST_END();

    TEST_CASE("2.2 (5,5)->(8,5): straight east") {
        ASSERT_PATH(run_search(field, {5, 5}, {8, 5}).path, {{5, 5}, {6, 5}, {7, 5}, {8, 5}});
        ASSERT_EQ(count_expansions(field, {5, 5}, {8, 5}), 6);
    } TEST_END();

    TEST_CASE("2.3 (5,5)->(8,8): straight south-east diagonal") {
        ASSERT_PATH(run_search(field, {5, 5}, {8, 8}).path, {{5, 5}, {6, 6}, {7, 7}, {8, 8}});
        ASSERT_EQ(count_expansions(field, {5, 5}, {8, 8}), 10);
    } TEST_END();

    TEST_CASE("2.4 (5,5)->(2,9): one S step first, then three SW steps") {
        // Four equal-cost (104) routes exist; the original picks this one.
        const SearchOutcome r = run_search(field, {5, 5}, {2, 9});
        ASSERT_PATH(r.path, {{5, 5}, {5, 6}, {4, 7}, {3, 8}, {2, 9}});
        ASSERT_EQ(path_cost(r.path, field.step_cost()), 104u);
        ASSERT_EQ(count_expansions(field, {5, 5}, {2, 9}), 15);
    } TEST_END();

    TEST_CASE("2.5 (5,5)->(9,2): one E step first, then three NE steps") {
        ASSERT_PATH(run_search(field, {5, 5}, {9, 2}).path, {{5, 5}, {6, 5}, {7, 4}, {8, 3}, {9, 2}});
        ASSERT_EQ(count_expansions(field, {5, 5}, {9, 2}), 15);
    } TEST_END();

    TEST_CASE("2.6 (5,5)->(10,7): three E steps first, then two SE steps") {
        const SearchOutcome r = run_search(field, {5, 5}, {10, 7});
        ASSERT_PATH(r.path, {{5, 5}, {6, 5}, {7, 5}, {8, 5}, {9, 6}, {10, 7}});
        ASSERT_EQ(path_cost(r.path, field.step_cost()), 116u);
        ASSERT_EQ(count_expansions(field, {5, 5}, {10, 7}), 20);
    } TEST_END();

    TEST_CASE("2.7 (5,5)->(2,2): straight north-west diagonal") {
        ASSERT_PATH(run_search(field, {5, 5}, {2, 2}).path, {{5, 5}, {4, 4}, {3, 3}, {2, 2}});
    } TEST_END();
}

// ============================================================================
// SUITE 3: Terrain cost trade-offs
// ============================================================================
void run_suite_3_cost_tradeoffs() {
    TEST_SUITE("Suite 3: Terrain Cost Trade-Offs (Mud Avoidance, Sand Roads)");

    TEST_CASE("3.1 A single mud tile in the way is avoided (detour 136 < straight 148)") {
        TerrainMap m = TerrainMap::filled(12, 11);
        m.set(5, 5, 'm');
        const SearchOutcome r = run_search(m, {2, 5}, {8, 5});
        ASSERT_PATH(r.path, {{2, 5}, {3, 5}, {4, 5}, {5, 4}, {6, 4}, {7, 4}, {8, 5}});
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 136u);
        ASSERT_EQ(count_expansions(m, {2, 5}, {8, 5}), 32);
    } TEST_END();

    TEST_CASE("3.2 A five-tile mud wall is crossed (straight 148 < detour)") {
        TerrainMap m = TerrainMap::filled(12, 11);
        for (int y = 3; y <= 7; ++y) m.set(5, y, 'm');
        const SearchOutcome r = run_search(m, {2, 5}, {8, 5});
        ASSERT_PATH(r.path, {{2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 5}, {8, 5}});
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 148u);
        ASSERT_EQ(count_expansions(m, {2, 5}, {8, 5}), 36);
    } TEST_END();

    TEST_CASE("3.3 A parallel sand road is taken (178 < 200 on grass)") {
        TerrainMap m = TerrainMap::filled(16, 11);
        for (int x = 0; x < 16; ++x) m.set(x, 6, 's');
        const SearchOutcome r = run_search(m, {2, 5}, {12, 5});
        ASSERT_PATH(r.path, {{2, 5}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6}, {10, 6}, {11, 6}, {12, 5}});
        ASSERT_EQ(path_cost(r.path, m.step_cost()), 178u);
    } TEST_END();
}

// ============================================================================
// SUITE 4: Search failure (count 0)
// ============================================================================
void run_suite_4_failures() {
    TEST_SUITE("Suite 4: No Path (f >= 8000 Cut-Off, Exhausted Open List)");

    TEST_CASE("4.1 Goal walled off by rocks: empty path after exploring the reachable area") {
        TerrainMap m = TerrainMap::filled(12, 12);
        for (int y = 7; y <= 9; ++y) {
            for (int x = 7; x <= 9; ++x) {
                if (x != 8 || y != 8) m.set(x, y, '#');
            }
        }
        const SearchOutcome r = run_search(m, {2, 2}, {8, 8});
        ASSERT_TRUE(r.path.empty());
        // 135 reachable tiles, then a rock tile (f >= 8000) reaches the top of the heap.
        ASSERT_EQ(count_expansions(m, {2, 2}, {8, 8}), 136);
    } TEST_END();

    TEST_CASE("4.2 Goal inside a water moat: empty path") {
        TerrainMap m = TerrainMap::filled(12, 12);
        for (int y = 6; y <= 10; ++y) {
            for (int x = 6; x <= 10; ++x) {
                const int d = std::max(std::abs(x - 8), std::abs(y - 8));
                if (d == 2) m.set(x, y, '~');
            }
        }
        ASSERT_TRUE(run_search(m, {2, 2}, {8, 8}).path.empty());
        ASSERT_EQ(count_expansions(m, {2, 2}, {8, 8}), 120);
    } TEST_END();

    TEST_CASE("4.3 Blocked goal (water) fails: the f >= 8000 test precedes the goal test") {
        // The goal is adjacent to the start and enters the heap at once, but with g = 8000. Every tile with
        // f < 8000 (the whole 12 x 12 map) is expanded before the search gives up.
        TerrainMap m = TerrainMap::filled(12, 12);
        m.set(3, 2, '~');
        ASSERT_TRUE(run_search(m, {2, 2}, {3, 2}).path.empty());
        ASSERT_EQ(count_expansions(m, {2, 2}, {3, 2}), 144);
    } TEST_END();

    TEST_CASE("4.4 Blocked goal (rock) fails the same way") {
        TerrainMap m = TerrainMap::filled(12, 12);
        m.set(3, 2, '#');
        ASSERT_TRUE(run_search(m, {2, 2}, {3, 2}).path.empty());
        ASSERT_EQ(count_expansions(m, {2, 2}, {3, 2}), 144);
    } TEST_END();

    TEST_CASE("4.5 Non-swimmer standing on water has no path (every edge out costs 8000)") {
        TerrainMap m = TerrainMap::filled(8, 8);
        m.set(3, 3, '~');
        ASSERT_TRUE(run_search(m, {3, 3}, {6, 3}).path.empty());
        ASSERT_EQ(count_expansions(m, {3, 3}, {6, 3}), 2);
    } TEST_END();
}

// ============================================================================
// SUITE 5: Grid edges and the row-90 sentinel
// ============================================================================
void run_suite_5_edges() {
    TEST_SUITE("Suite 5: Grid-Edge Neighbours and the Row-90 Sentinel");

    TEST_CASE("5.1 U-shaped route along the bottom row, right column and top row") {
        TerrainMap m;
        m.rows = {"..........", "#########.", ".........."};
        const SearchOutcome r = run_search(m, {0, 2}, {0, 0});
        ASSERT_PATH(r.path, {{0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 1},
                             {8, 0}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}, {0, 0}});
        ASSERT_EQ(count_expansions(m, {0, 2}, {0, 0}), 21);
    } TEST_END();

    TEST_CASE("5.2 Corner to corner, both directions") {
        const TerrainMap m = TerrainMap::filled(8, 6);
        ASSERT_PATH(run_search(m, {0, 0}, {7, 5}).path,
                    {{0, 0}, {1, 0}, {2, 0}, {3, 1}, {4, 2}, {5, 3}, {6, 4}, {7, 5}});
        ASSERT_PATH(run_search(m, {7, 5}, {0, 0}).path,
                    {{7, 5}, {6, 5}, {5, 5}, {4, 4}, {3, 3}, {2, 2}, {1, 1}, {0, 0}});
    } TEST_END();

    TEST_CASE("5.3 Along the top row and down the left column") {
        const TerrainMap m = TerrainMap::filled(8, 6);
        ASSERT_PATH(run_search(m, {7, 0}, {0, 0}).path,
                    {{7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}, {0, 0}});
        ASSERT_EQ(count_expansions(m, {7, 0}, {0, 0}), 16);
        ASSERT_PATH(run_search(m, {0, 5}, {0, 0}).path, {{0, 5}, {0, 4}, {0, 3}, {0, 2}, {0, 1}, {0, 0}});
        ASSERT_EQ(count_expansions(m, {0, 5}, {0, 0}), 10);
    } TEST_END();

    TEST_CASE("5.4 Row 90 is never generated as a neighbour (sentinel 0x5a quirk)") {
        // Invalid neighbours get row 0x5a and are skipped by testing row == 90, so on a map taller than 90
        // rows (none shipped; the largest is 60) row 90 splits the map.
        const TerrainMap m = TerrainMap::filled(5, 100);
        ASSERT_TRUE(run_search(m, {2, 85}, {2, 95}).path.empty());
        ASSERT_EQ(count_expansions(m, {2, 85}, {2, 95}), 450);  // all 90 x 5 tiles above row 90
        ASSERT_TRUE(run_search(m, {2, 88}, {2, 90}).path.empty());
        ASSERT_PATH(run_search(m, {2, 85}, {2, 89}).path, {{2, 85}, {2, 86}, {2, 87}, {2, 88}, {2, 89}});
        // A start on row 90 is expanded normally and can leave it.
        ASSERT_PATH(run_search(m, {2, 90}, {2, 92}).path, {{2, 90}, {2, 91}, {2, 92}});
    } TEST_END();

    TEST_CASE("5.5 Remake guards: 1-row / 1-column maps and a start outside the map") {
        // On a single-row map the original's "else if" leaves the south neighbours unmarked and reads
        // outside its grid; the port skips them instead.
        const TerrainMap row = TerrainMap::filled(5, 1);
        ASSERT_PATH(run_search(row, {0, 0}, {4, 0}).path, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}});
        const TerrainMap col = TerrainMap::filled(1, 5);
        ASSERT_PATH(run_search(col, {0, 4}, {0, 0}).path, {{0, 4}, {0, 3}, {0, 2}, {0, 1}, {0, 0}});
        const TerrainMap one = TerrainMap::filled(1, 1);
        ASSERT_PATH(run_search(one, {0, 0}, {0, 0}).path, {{0, 0}});
        const TerrainMap m = TerrainMap::filled(6, 6);
        const SearchOutcome off = run_search(m, {-1, 2}, {3, 3});
        ASSERT_TRUE(off.path.empty());
        ASSERT_EQ(off.slices, 1);
    } TEST_END();
}

// ============================================================================
// SUITE 6: Budget slicing and the grid pool
// ============================================================================
void run_suite_6_slicing_and_pool() {
    TEST_SUITE("Suite 6: 1000-Expansion Slices and the Four-Slot Grid Pool");

    const TerrainMap wall = wall_map();
    const TileCoord a_start{5, 30};
    const TileCoord a_goal{54, 30};

    TEST_CASE("6.1 Long search: 2272 expansions take three 1000-expansion slices") {
        ASSERT_EQ(count_expansions(wall, a_start, a_goal), 2272);
        PathGridPool pool;
        PathSearch s(1, a_start, a_goal, wall.height(), wall.width(), pool);
        const auto cost = wall.step_cost();
        ASSERT_EQ(s.step(1000, cost), 1);
        ASSERT_FALSE(s.finished());
        ASSERT_TRUE(s.path().empty());
        ASSERT_EQ(s.step(1000, cost), 1);
        ASSERT_FALSE(s.finished());
        ASSERT_EQ(s.step(1000, cost), 1);
        ASSERT_TRUE(s.finished());
        ASSERT_EQ(s.path().size(), 57u);
        ASSERT_EQ(s.path().front(), a_start);
        ASSERT_EQ(s.path().back(), a_goal);
        ASSERT_TRUE(is_connected(s.path()));
        ASSERT_EQ(path_cost(s.path(), cost), 1512u);
        ASSERT_EQ(path_cost(s.path(), cost), dijkstra_cost(a_start, a_goal, wall.height(), wall.width(), cost));
        // The route squeezes through the gap at the bottom of the wall.
        ASSERT_EQ(s.path()[28], (TileCoord{30, 58}));
        // A finished search ignores further slices.
        const std::vector<TileCoord> before = s.path();
        ASSERT_EQ(s.step(1000, cost), 1);
        ASSERT_TRUE(s.path() == before);
    } TEST_END();

    TEST_CASE("6.2 The goal pop counts against the budget; slicing never changes the result") {
        const std::vector<TileCoord> reference = run_search(wall, a_start, a_goal, 1000).path;
        ASSERT_EQ(run_search(wall, a_start, a_goal, 2272).slices, 1);
        ASSERT_EQ(run_search(wall, a_start, a_goal, 2271).slices, 2);
        ASSERT_EQ(run_search(wall, a_start, a_goal, 1136).slices, 2);
        ASSERT_EQ(run_search(wall, a_start, a_goal, 1135).slices, 3);
        for (uint16_t budget : {uint16_t{1}, uint16_t{7}, uint16_t{999}, uint16_t{2271}, uint16_t{2272}, uint16_t{65535}}) {
            ASSERT_TRUE(run_search(wall, a_start, a_goal, budget).path == reference);
        }
    } TEST_END();

    TEST_CASE("6.3 PathGridPool hands out the first free slot, at most four") {
        PathGridPool pool;
        ASSERT_EQ(pool.free_count(), 4);
        ASSERT_EQ(pool.acquire(10, 10), 0);
        ASSERT_EQ(pool.acquire(10, 10), 1);
        ASSERT_EQ(pool.acquire(10, 10), 2);
        ASSERT_EQ(pool.acquire(10, 10), 3);
        ASSERT_EQ(pool.acquire(10, 10), -1);
        ASSERT_EQ(pool.free_count(), 0);
        pool.release(2);
        ASSERT_FALSE(pool.in_use(2));
        ASSERT_EQ(pool.acquire(10, 10), 2);
        pool.release(1);
        pool.release(3);
        ASSERT_EQ(pool.acquire(10, 10), 1);
        pool.release(-1);  // ignored
        pool.release(4);   // ignored
        ASSERT_EQ(pool.free_count(), 1);
        ASSERT_TRUE(pool.cells(0) != nullptr);
        ASSERT_TRUE(pool.cells(4) == nullptr);
    } TEST_END();

    TEST_CASE("6.4 A fifth search has no grid until a slot is released; step() then retries Init") {
        const TerrainMap m = TerrainMap::filled(16, 16);
        const auto cost = m.step_cost();
        PathGridPool pool;
        std::vector<std::unique_ptr<PathSearch>> held;
        for (uint32_t i = 0; i < 4; ++i) {
            held.push_back(std::make_unique<PathSearch>(i, TileCoord{1, 1}, TileCoord{9, 9}, m.height(), m.width(), pool));
            ASSERT_TRUE(held.back()->has_grid());
        }
        PathSearch fifth(4, {5, 5}, {2, 9}, m.height(), m.width(), pool);
        ASSERT_FALSE(fifth.has_grid());
        ASSERT_EQ(fifth.step(1000, cost), 0);  // nothing ran
        ASSERT_FALSE(fifth.finished());
        held[1].reset();  // destroying a search frees its slot
        ASSERT_EQ(pool.free_count(), 1);
        ASSERT_EQ(fifth.step(1000, cost), 1);
        ASSERT_TRUE(fifth.has_grid());
        ASSERT_TRUE(fifth.finished());
        ASSERT_TRUE(pool.in_use(1));  // a finished search keeps its slot until it is destroyed
        ASSERT_PATH(fifth.path(), {{5, 5}, {5, 6}, {4, 7}, {3, 8}, {2, 9}});
    } TEST_END();

    TEST_CASE("6.5 Reused grid slots are cleared for each search (also across map sizes)") {
        PathGridPool pool;
        const auto wall_cost = wall.step_cost();
        {
            PathSearch dirty(1, a_start, a_goal, wall.height(), wall.width(), pool);
            while (!dirty.finished()) dirty.step(1000, wall_cost);
        }
        PathSearch again(2, a_start, a_goal, wall.height(), wall.width(), pool);
        while (!again.finished()) again.step(1000, wall_cost);
        ASSERT_TRUE(again.path() == run_search(wall, a_start, a_goal).path);
        const TerrainMap small = TerrainMap::filled(16, 16);
        PathSearch other(3, {5, 5}, {10, 7}, small.height(), small.width(), pool);
        ASSERT_EQ(other.step(1000, small.step_cost()), 1);
        ASSERT_PATH(other.path(), {{5, 5}, {6, 5}, {7, 5}, {8, 5}, {9, 6}, {10, 7}});
    } TEST_END();
}

// ============================================================================
// SUITE 7: PATHMGR scheduling
// ============================================================================
void run_suite_7_manager() {
    TEST_SUITE("Suite 7: PATHMGR Round Robin, Replacement and Pool Rotation");

    const TerrainMap wall = wall_map();
    const auto cost = wall.provider();
    const uint16_t H = wall.height();
    const uint16_t W = wall.width();

    TEST_CASE("7.1 Empty manager: run() delivers nothing") {
        PathManager mgr;
        ASSERT_EQ(mgr.pending(), 0u);
        ASSERT_FALSE(mgr.run(cost).has_value());
    } TEST_END();

    TEST_CASE("7.2 Long request is sliced; other requests are served round robin in between") {
        PathManager mgr;
        mgr.request(1, {5, 30}, {54, 30}, H, W);   // 2272 expansions: three slices
        mgr.request(2, {5, 5}, {12, 5}, H, W);     // one slice
        mgr.request(3, {50, 50}, {55, 55}, H, W);  // one slice
        ASSERT_EQ(mgr.pending(), 3u);
        std::vector<PathManager::Delivery> ds;
        const std::vector<uint32_t> sched = run_schedule(mgr, cost, 6, &ds);
        ASSERT_TRUE((sched == std::vector<uint32_t>{0, 2, 3, 0, 1, 0}));
        ASSERT_EQ(mgr.pending(), 0u);
        ASSERT_PATH(find_delivery(ds, 2)->path, {{5, 5}, {6, 5}, {7, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5}, {12, 5}});
        ASSERT_PATH(find_delivery(ds, 3)->path, {{50, 50}, {51, 51}, {52, 52}, {53, 53}, {54, 54}, {55, 55}});
        const PathManager::Delivery* a = find_delivery(ds, 1);
        ASSERT_EQ(a->path.size(), 57u);
        ASSERT_EQ(path_cost(a->path, wall.step_cost()), 1512u);
    } TEST_END();

    TEST_CASE("7.3 At most one path is delivered per run") {
        PathManager mgr;
        mgr.request(4, {5, 5}, {12, 5}, H, W);
        mgr.request(5, {50, 50}, {55, 55}, H, W);
        mgr.request(6, {5, 30}, {12, 30}, H, W);
        const auto d = mgr.run(cost);
        ASSERT_TRUE(d.has_value());
        ASSERT_EQ(d->ant_id, 4u);
        ASSERT_EQ(mgr.pending(), 2u);
        ASSERT_FALSE(mgr.has_request_for(4));
        ASSERT_TRUE(mgr.has_request_for(5));
        ASSERT_TRUE(mgr.has_request_for(6));
    } TEST_END();

    TEST_CASE("7.4 A new request replaces the ant's queued one and goes to the back") {
        PathManager mgr;
        mgr.request(7, {5, 5}, {12, 5}, H, W);
        mgr.request(9, {50, 50}, {55, 55}, H, W);
        mgr.request(7, {5, 5}, {5, 12}, H, W);  // replaces the first request of ant 7
        ASSERT_EQ(mgr.pending(), 2u);
        mgr.request(7, {5, 5}, {5, 12}, H, W);  // still a single request per ant
        ASSERT_EQ(mgr.pending(), 2u);
        const auto d1 = mgr.run(cost);
        ASSERT_TRUE(d1.has_value());
        ASSERT_EQ(d1->ant_id, 9u);
        const auto d2 = mgr.run(cost);
        ASSERT_TRUE(d2.has_value());
        ASSERT_EQ(d2->ant_id, 7u);
        ASSERT_PATH(d2->path, {{5, 5}, {5, 6}, {5, 7}, {5, 8}, {5, 9}, {5, 10}, {5, 11}, {5, 12}});
        ASSERT_EQ(mgr.pending(), 0u);
    } TEST_END();

    TEST_CASE("7.5 Replacing an unfinished search restarts it from scratch") {
        PathManager mgr;
        mgr.request(1, {5, 30}, {54, 30}, H, W);
        mgr.request(2, {5, 5}, {12, 5}, H, W);
        ASSERT_FALSE(mgr.run(cost).has_value());   // ant 1: first slice
        mgr.request(1, {5, 30}, {12, 30}, H, W);   // new order while the old search is half done
        std::vector<PathManager::Delivery> ds;
        const std::vector<uint32_t> sched = run_schedule(mgr, cost, 3, &ds);
        ASSERT_TRUE((sched == std::vector<uint32_t>{2, 1, 0}));
        ASSERT_PATH(find_delivery(ds, 1)->path, {{5, 30}, {6, 30}, {7, 30}, {8, 30}, {9, 30}, {10, 30}, {11, 30}, {12, 30}});
    } TEST_END();

    // Five requests that each need exactly two slices (1000 < expansions <= 2000).
    struct Req { uint32_t ant; TileCoord from; TileCoord to; size_t len; };
    const std::vector<Req> five = {
        {1, {20, 40}, {40, 40}, 37}, {2, {28, 30}, {33, 30}, 57}, {3, {29, 30}, {31, 30}, 57},
        {4, {24, 35}, {36, 35}, 47}, {5, {22, 40}, {38, 40}, 37},
    };

    TEST_CASE("7.6 Five concurrent requests rotate through the four grid slots") {
        // Requests 1-4 take the four grids at creation; request 5 has none. Runs 1-4 give requests 1-4 their
        // first slice. In run 5, request 5 still finds no free grid, is rotated to the back, and the same run
        // continues with request 1, whose second slice finishes it (an unlimited pool would have given run 5
        // to request 5 and delivered request 1 only in run 6). Request 5 gets a grid in run 9.
        PathManager mgr;
        for (const Req& r : five) mgr.request(r.ant, r.from, r.to, H, W);
        std::vector<PathManager::Delivery> ds;
        const std::vector<uint32_t> sched = run_schedule(mgr, cost, 11, &ds);
        ASSERT_TRUE((sched == std::vector<uint32_t>{0, 0, 0, 0, 1, 2, 3, 4, 0, 5, 0}));
        for (const Req& r : five) {
            const PathManager::Delivery* d = find_delivery(ds, r.ant);
            ASSERT_TRUE(d != nullptr);
            ASSERT_EQ(d->path.size(), r.len);
            ASSERT_EQ(d->path.front(), r.from);
            ASSERT_EQ(d->path.back(), r.to);
            ASSERT_TRUE(is_connected(d->path));
        }
    } TEST_END();

    TEST_CASE("7.7 Replacing a request releases its grid slot for the new request") {
        // Requests 1-4 hold the grids; 5 has none. Re-requesting ant 1 destroys its request (slot 0 freed)
        // and the new request takes that slot at once, so request 5 is still the one rotated in run 4.
        PathManager mgr;
        for (const Req& r : five) mgr.request(r.ant, r.from, r.to, H, W);
        mgr.request(1, five[0].from, five[0].to, H, W);
        ASSERT_EQ(mgr.pending(), 5u);
        const std::vector<uint32_t> sched = run_schedule(mgr, cost, 11);
        ASSERT_TRUE((sched == std::vector<uint32_t>{0, 0, 0, 0, 2, 3, 4, 0, 1, 5, 0}));
    } TEST_END();

    TEST_CASE("7.8 A failed search is delivered with an empty path") {
        TerrainMap m = TerrainMap::filled(12, 12);
        for (int y = 7; y <= 9; ++y) {
            for (int x = 7; x <= 9; ++x) {
                if (x != 8 || y != 8) m.set(x, y, '#');
            }
        }
        PathManager mgr;
        mgr.request(11, {2, 2}, {8, 8}, m.height(), m.width());
        const auto d = mgr.run(m.provider());
        ASSERT_TRUE(d.has_value());
        ASSERT_EQ(d->ant_id, 11u);
        ASSERT_TRUE(d->path.empty());
        ASSERT_EQ(mgr.pending(), 0u);
    } TEST_END();

    TEST_CASE("7.9 The cost provider receives the requesting ant's id") {
        const TerrainMap m = TerrainMap::filled(16, 16);
        PathManager mgr;
        mgr.request(21, {2, 2}, {6, 2}, m.height(), m.width());
        mgr.request(22, {2, 8}, {6, 8}, m.height(), m.width());
        std::vector<uint32_t> seen;
        const PathManager::CostProvider spy = [&](uint32_t ant, TileCoord a, TileCoord b) {
            if (seen.empty() || seen.back() != ant) seen.push_back(ant);
            return m.cost(a, b);
        };
        ASSERT_EQ(mgr.run(spy)->ant_id, 21u);
        ASSERT_EQ(mgr.run(spy)->ant_id, 22u);
        ASSERT_TRUE((seen == std::vector<uint32_t>{21, 22}));
    } TEST_END();

    TEST_CASE("7.10 clear() drops every request and frees the grids") {
        PathManager mgr;
        for (const Req& r : five) mgr.request(r.ant, r.from, r.to, H, W);
        ASSERT_FALSE(mgr.run(cost).has_value());
        mgr.clear();
        ASSERT_EQ(mgr.pending(), 0u);
        ASSERT_FALSE(mgr.run(cost).has_value());
        for (const Req& r : five) mgr.request(r.ant, r.from, r.to, H, W);
        ASSERT_TRUE((run_schedule(mgr, cost, 11) == std::vector<uint32_t>{0, 0, 0, 0, 1, 2, 3, 4, 0, 5, 0}));
    } TEST_END();

    TEST_CASE("7.11 Moving a manager keeps its in-flight searches") {
        PathManager a;
        a.request(1, {5, 30}, {54, 30}, H, W);
        a.request(2, {5, 5}, {12, 5}, H, W);
        ASSERT_FALSE(a.run(cost).has_value());  // ant 1: first slice
        PathManager b(std::move(a));
        ASSERT_EQ(b.pending(), 2u);
        PathManager c;
        c.request(3, {50, 50}, {55, 55}, H, W);
        c = std::move(b);  // drops c's own request
        ASSERT_EQ(c.pending(), 2u);
        ASSERT_FALSE(c.has_request_for(3));
        ASSERT_TRUE((run_schedule(c, cost, 4) == std::vector<uint32_t>{2, 0, 1, 0}));
        a.clear();  // a moved-from manager is reusable
        a.request(4, {5, 5}, {12, 5}, H, W);
        const auto d = a.run(cost);
        ASSERT_TRUE(d.has_value());
        ASSERT_EQ(d->ant_id, 4u);
    } TEST_END();
}

// ============================================================================
// SUITE 8: Stale heap (no decrease-key) versus textbook A*
// ============================================================================
void run_suite_8_stale_heap() {
    TEST_SUITE("Suite 8: Stale-Heap Quirks (No Decrease-Key)");

    TEST_CASE("8.1 Stale heap returns a non-optimal path (78 instead of 63)") {
        // Found by brute force over small random terrain grids (x = column, y = row):
        //     y0: m . m .      start (1,0) grass, goal (0,2) sand
        //     y1: m m s s
        //     y2: s s m m
        // Expansion 1 (start) pushes S (1,1) f 50, SE (2,1) f 57, SW (0,1) f 63, E (2,0) f 66, W (0,0) f 66.
        // Expansion 2, (1,1) g 34: pushes (1,2) with g 66 / f 82 and the goal with g 34 + 44 = 78 / f 78,
        // which ends up above (1,2) in the heap.
        // Expansion 3, (2,1) g 25: improves (1,2) to g 25 + 22 = 47 / f 63. The cell is rewritten in place but
        // never re-sifted, so (1,2) stays below the goal.
        // After (0,1), (2,0) and (0,0) are expanded, the goal (f 78) is popped while (1,2) (f 63) is still
        // waiting, and the path keeps the goal's first parent (1,1).
        // With decrease-key, (1,2) would move up, be expanded first and give the goal g = 47 + 16 = 63.
        TerrainMap m;
        m.rows = {"m.m.", "mmss", "ssmm"};
        const auto cost = m.step_cost();
        const SearchOutcome r = run_search(m, {1, 0}, {0, 2});
        ASSERT_PATH(r.path, {{1, 0}, {1, 1}, {0, 2}});
        ASSERT_EQ(path_cost(r.path, cost), 78u);
        ASSERT_EQ(count_expansions(m, {1, 0}, {0, 2}), 7);
        const std::vector<TileCoord> optimal = {{1, 0}, {2, 1}, {1, 2}, {0, 2}};
        ASSERT_EQ(path_cost(optimal, cost), 63u);
        ASSERT_EQ(dijkstra_cost({1, 0}, {0, 2}, m.height(), m.width(), cost), 63u);
    } TEST_END();

    TEST_CASE("8.2 Stale heap reports 'no path' although one exists (one-way entry tile)") {
        // Direction-dependent 8000 edges occur in the original StepCost, e.g. an anthill tile may only be
        // entered from the entrance tile. Here the door (2,1) may only be entered from (1,2):
        //     y0: . . .      start (2,2), goal (1,0)
        //     y1: # # .      door (2,1)
        //     y2: . . .      entrance (1,2)
        // Expansion 1 (start): the door is first reached from the start, so it is pushed with g = 8000.
        // Expansion 2, (1,2): the door is improved to g = 28 in place (not re-sifted) and stays below the rock
        // (1,1), which is now at the root with f = 8016, so expansion 3 pops it and the search fails. A
        // textbook A* (and Dijkstra) finds (2,2) -> (1,2) -> (2,1) -> (1,0) with cost 76.
        TerrainMap m;
        m.rows = {"...", "##.", "..."};
        const TileCoord door{2, 1};
        const TileCoord entrance{1, 2};
        const PathSearch::StepCostFn one_way = [&m, door, entrance](TileCoord a, TileCoord b) -> uint32_t {
            if (b == door && a != entrance) return kBlocked;
            return m.cost(a, b);
        };
        const SearchOutcome r = run_search({2, 2}, {1, 0}, m.height(), m.width(), one_way);
        ASSERT_TRUE(r.path.empty());
        ASSERT_EQ(run_search({2, 2}, {1, 0}, m.height(), m.width(), one_way, 1).slices, 3);
        ASSERT_EQ(dijkstra_cost({2, 2}, {1, 0}, m.height(), m.width(), one_way), 76u);
    } TEST_END();
}

int main() {
    std::cout << "=======================================================\n"
              << " ANTS - ORIGINAL PATH PLANNER (PATHMGR A*) TEST SUITE  \n"
              << "=======================================================\n";

    run_suite_1_basic_paths();
    run_suite_2_tie_breaking();
    run_suite_3_cost_tradeoffs();
    run_suite_4_failures();
    run_suite_5_edges();
    run_suite_6_slicing_and_pool();
    run_suite_7_manager();
    run_suite_8_stale_heap();

    std::cout << "\n=======================================================\n"
              << " PATH PLANNER TEST SUMMARY\n"
              << "=======================================================\n"
              << "  Total Test Cases: " << g_test_count << "\n"
              << "  Total Assertions: " << g_assert_count << "\n"
              << "  Failed Tests:     " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL PATH PLANNER TESTS PASSED CLEANLY <<< \n\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED! <<< \n\n";
    return 1;
}
