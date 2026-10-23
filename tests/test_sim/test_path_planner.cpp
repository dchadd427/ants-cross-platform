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
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

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
                         const PathSearch::StepCostFn& cost, uint16_t budget = PathSearch::kDefaultBudget, bool wall_at_row_90 = true) {
    PathGridPool pool;
    PathSearch search(1, start, goal, rows, cols, pool, wall_at_row_90);
    SearchOutcome out;
    while (!search.finished() && out.slices < 1000000) {
        search.step(budget, cost);
        ++out.slices;
    }
    out.path = search.path();
    return out;
}

SearchOutcome run_search(const TerrainMap& map, TileCoord start, TileCoord goal,
                         uint16_t budget = PathSearch::kDefaultBudget, bool wall_at_row_90 = true) {
    return run_search(start, goal, map.height(), map.width(), map.step_cost(), budget, wall_at_row_90);
}

// A slice with budget 1 performs exactly one expansion (heap pop), so the slice count is the expansion count.
int count_expansions(const TerrainMap& map, TileCoord start, TileCoord goal, bool wall_at_row_90 = true) {
    return run_search(map, start, goal, 1, wall_at_row_90).slices;
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
        // Verified table of docs/reverse_engineering/movement/05_path_manager_and_astar.md 3.11 (x87 53-bit double: 40 * 1.4 rounds to exactly 56.0).
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

    TEST_CASE("5.4b Rules 1: the wall at row 90 is down (a map of rules 1 can be crossed at row 90 and walked below it)") {
        const TerrainMap m = TerrainMap::filled(5, 100);
        const bool down = false;
        // across row 90, and onto it, and from row 90 on: every one of them is empty with the wall up (5.4)
        ASSERT_PATH(run_search(m, {2, 85}, {2, 95}, PathSearch::kDefaultBudget, down).path,
                    {{2, 85}, {2, 86}, {2, 87}, {2, 88}, {2, 89}, {2, 90}, {2, 91}, {2, 92}, {2, 93}, {2, 94}, {2, 95}});
        ASSERT_PATH(run_search(m, {2, 88}, {2, 90}, PathSearch::kDefaultBudget, down).path, {{2, 88}, {2, 89}, {2, 90}});
        ASSERT_PATH(run_search(m, {2, 90}, {2, 92}, PathSearch::kDefaultBudget, down).path, {{2, 90}, {2, 91}, {2, 92}});
        // along the left and the right edge (the edge neighbours are still invalid: the marker is row -1 now) and down to the last row
        ASSERT_EQ(run_search(m, {0, 80}, {0, 99}, PathSearch::kDefaultBudget, down).path.size(), 20u);
        ASSERT_EQ(run_search(m, {4, 99}, {4, 80}, PathSearch::kDefaultBudget, down).path.size(), 20u);
        ASSERT_EQ(run_search(m, {2, 0}, {2, 99}, PathSearch::kDefaultBudget, down).path.size(), 100u);
        // the way up from the last row to row 0 and the corners are found too; no tile outside the map is ever in a path
        for (const TileCoord goal : {TileCoord{0, 0}, TileCoord{4, 0}, TileCoord{4, 99}, TileCoord{0, 99}}) {
            const SearchOutcome r = run_search(m, {2, 50}, goal, PathSearch::kDefaultBudget, down);
            ASSERT_FALSE(r.path.empty());
            ASSERT_TRUE(r.path.front() == (TileCoord{2, 50}) && r.path.back() == goal && is_connected(r.path));
            for (const TileCoord& t : r.path) ASSERT_TRUE(t.x >= 0 && t.x < 5 && t.y >= 0 && t.y < 100);
        }
        // a wall of rock across row 90 still stops it (the wall that is down is the sentinel, not the terrain)
        TerrainMap rock = TerrainMap::filled(5, 100);
        for (int x = 0; x < 5; ++x) rock.set(x, 90, '#');
        ASSERT_TRUE(run_search(rock, {2, 85}, {2, 95}, PathSearch::kDefaultBudget, down).path.empty());
    } TEST_END();

    TEST_CASE("5.4c The wall flag changes nothing on a map of at most 90 rows (the same path, the same number of expansions)") {
        const TerrainMap wall = wall_map();
        ASSERT_TRUE(wall.height() <= 90);
        ASSERT_EQ(count_expansions(wall, {5, 30}, {54, 30}, false), count_expansions(wall, {5, 30}, {54, 30}, true));
        ASSERT_TRUE(run_search(wall, {5, 30}, {54, 30}, PathSearch::kDefaultBudget, false).path == run_search(wall, {5, 30}, {54, 30}).path);
        const TerrainMap tall = TerrainMap::filled(7, 90);       // 90 rows: rows 0..89, the last row has no row 90 to meet
        for (const TileCoord goal : {TileCoord{0, 0}, TileCoord{6, 89}, TileCoord{3, 89}}) {
            ASSERT_EQ(count_expansions(tall, {3, 45}, goal, false), count_expansions(tall, {3, 45}, goal, true));
            ASSERT_TRUE(run_search(tall, {3, 45}, goal, PathSearch::kDefaultBudget, false).path == run_search(tall, {3, 45}, goal).path);
        }
    } TEST_END();

    TEST_CASE("5.4d The manager and its copy: the flag is carried to the search (request with the wall down delivers a path across row 90, the default does not, a copy of the manager does as the original does)") {
        const TerrainMap m = TerrainMap::filled(5, 100);
        const PathManager::CostProvider cost = m.provider();
        const auto drain_one = [&](PathManager& pm) {
            for (int i = 0; i < 20; ++i) {
                if (const std::optional<PathManager::Delivery> d = pm.run(cost)) return d->path;
            }
            return std::vector<TileCoord>{{-9, -9}};
        };
        PathManager down;
        down.request(7, {2, 85}, {2, 95}, m.height(), m.width(), false);
        PathManager copy(down);                                                      // a copy made before the search ran
        PathManager up;
        up.request(7, {2, 85}, {2, 95}, m.height(), m.width());                      // the default: the wall is up
        PathManager up_explicit;
        up_explicit.request(7, {2, 85}, {2, 95}, m.height(), m.width(), true);
        const std::vector<TileCoord> crossed = drain_one(down);
        ASSERT_EQ(crossed.size(), 11u);
        ASSERT_TRUE(drain_one(copy) == crossed);
        ASSERT_TRUE(drain_one(up).empty() && drain_one(up_explicit).empty());
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

// ============================================================================
// SUITE 9: The wide search (map rules word 2)
// ============================================================================
namespace {

// A small deterministic generator (xorshift64): the maps and orders of this suite are the same on every compiler.
struct Xor {
    uint64_t s;
    explicit Xor(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 16);
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

enum class Flavor { Open, Mixed, Walls };

// Open grass; mixed (lakes, mud, sand and dirt patches from integer value noise); walls (mixed, plus long rock walls with a 3-tile gap, which force detours).
TerrainMap make_terrain(int w, int h, Flavor flavor, uint64_t seed) {
    TerrainMap m = TerrainMap::filled(w, h);
    if (flavor == Flavor::Open) return m;
    Xor rng(seed);
    constexpr int G = 12;
    std::vector<int> a((G + 2) * (G + 2)), b((G + 2) * (G + 2));
    for (int& v : a) v = static_cast<int>(rng.below(256));
    for (int& v : b) v = static_cast<int>(rng.below(256));
    const auto sample = [&](const std::vector<int>& g, int x, int y) {
        const int fx = x * G * 256 / w;
        const int fy = y * G * 256 / h;
        const int ix = fx >> 8, tx = fx & 255, iy = fy >> 8, ty = fy & 255;
        const auto at = [&](int i, int j) { return g[static_cast<size_t>(j * (G + 2) + i)]; };
        const int top = at(ix, iy) * (256 - tx) + at(ix + 1, iy) * tx;
        const int bottom = at(ix, iy + 1) * (256 - tx) + at(ix + 1, iy + 1) * tx;
        return (top * (256 - ty) + bottom * ty) >> 16;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int n = sample(a, x, y);
            const int n2 = sample(b, x, y);
            char c = '.';
            if (n > 179) c = '~';
            else if (n > 158) c = 'm';
            else if (n2 > 184) c = 's';
            else if (n2 < 56) c = 'd';
            m.set(x, y, c);
        }
    }
    if (flavor == Flavor::Walls) {
        const int count = w / 16;
        for (int k = 0; k < count; ++k) {
            const bool vertical = (rng.next() & 1u) != 0;
            const int pos = static_cast<int>(rng.below(static_cast<uint32_t>(vertical ? w : h)));
            const int len = w / 3 + static_cast<int>(rng.below(static_cast<uint32_t>(w / 2)));
            const int start = static_cast<int>(rng.below(static_cast<uint32_t>(vertical ? h : w)));
            const int gap = static_cast<int>(rng.below(static_cast<uint32_t>(len)));
            for (int i = 0; i < len; ++i) {
                if (i >= gap && i < gap + 3) continue;
                const int x = vertical ? pos : start + i;
                const int y = vertical ? start + i : pos;
                if (x < w && y < h) m.set(x, y, '#');
            }
        }
    }
    return m;
}

bool walkable(const TerrainMap& m, int x, int y) {
    if (x < 0 || y < 0 || x >= m.width() || y >= m.height()) return false;
    const char c = m.at({x, y});
    return c != '~' && c != '#';
}

TileCoord random_walkable(const TerrainMap& m, Xor& rng) {
    for (int i = 0; i < 100000; ++i) {
        const int x = static_cast<int>(rng.below(m.width()));
        const int y = static_cast<int>(rng.below(m.height()));
        if (walkable(m, x, y)) return {x, y};
    }
    return {0, 0};
}

uint64_t fnv_step(uint64_t h, uint64_t v) {
    h ^= v;
    return h * 0x100000001b3ull;
}
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull;

uint64_t path_digest(const std::vector<TileCoord>& p) {
    uint64_t h = fnv_step(kFnvBasis, p.size());
    for (const TileCoord& t : p) h = fnv_step(fnv_step(h, static_cast<uint32_t>(t.x)), static_cast<uint32_t>(t.y));
    return h;
}

// What a search did: its path, how many times it asked for the cost of a step, and a digest of every (from, to) it asked about in order. Two searches with the same
// trace expanded the same nodes in the same order (the neighbours are asked in order from each node that is expanded).
struct Trace {
    std::vector<TileCoord> path;
    uint64_t calls{0};
    uint64_t digest{kFnvBasis};
    bool beyond{false};
    bool limit_hit{false};   // a node with f >= 8000 was popped: the original would have given up
    uint32_t slices{0};      // calls of step() until the search finished (with the budget that run_trace was given)
};

Trace run_trace(uint16_t rows, uint16_t cols, TileCoord start, TileCoord goal, const PathSearch::StepCostFn& cost, bool wide, bool wall_at_row_90 = false,
                uint16_t budget = 60000) {
    Trace t;
    const PathSearch::StepCostFn counted = [&](TileCoord a, TileCoord b) {
        ++t.calls;
        t.digest = fnv_step(fnv_step(fnv_step(fnv_step(t.digest, static_cast<uint32_t>(a.x)), static_cast<uint32_t>(a.y)), static_cast<uint32_t>(b.x)), static_cast<uint32_t>(b.y));
        return cost(a, b);
    };
    PathGridPool pool;
    PathSearch search(1, start, goal, rows, cols, pool, wall_at_row_90, wide);
    for (int i = 0; i < 100000 && !search.finished(); ++i) {
        search.step(budget, counted);
        ++t.slices;
    }
    t.path = search.path();
    t.beyond = search.beyond();
    t.limit_hit = search.limit_hit();
    return t;
}

Trace run_trace(const TerrainMap& m, TileCoord start, TileCoord goal, bool wide, bool wall_at_row_90 = false, uint16_t budget = 60000) {
    return run_trace(m.height(), m.width(), start, goal, m.step_cost(), wide, wall_at_row_90, budget);
}

bool path_is_walkable(const TerrainMap& m, const std::vector<TileCoord>& p, TileCoord start, TileCoord goal) {
    if (p.empty() || p.front() != start || p.back() != goal || !is_connected(p)) return false;
    for (size_t i = 1; i < p.size(); ++i) {
        if (m.cost(p[i - 1], p[i]) >= kBlocked) return false;
    }
    return true;
}

// ---- the model: the wide search as the header describes it, written out with a field for each thing and no bit packing, for the small cost fields of 9.2 ------------------
struct WideModel {
    struct Node {
        bool opened{false}, closed{false}, taint{false};
        int64_t g{0};
        int dir{0};
    };
    uint16_t rows{0}, cols{0};
    TileCoord start{0, 0}, goal{0, 0};
    PathSearch::StepCostFn cost;
    std::vector<Node> nodes;
    std::vector<int> heap;  // node indices
    bool beyond{false};
    bool limit_hit{false};   // a node with f >= 8000 was popped (the pop at which the original gives up)
    uint64_t pops{0};
    std::vector<TileCoord> path;

    int idx(TileCoord t) const { return t.y * cols + t.x; }
    TileCoord tile(int i) const { return {i % cols, i / cols}; }
    int64_t h_of(TileCoord t) const { return 16 * std::max(std::abs(t.x - goal.x), std::abs(t.y - goal.y)); }
    int64_t f(int i) const { return nodes[static_cast<size_t>(i)].g + h_of(tile(i)); }

    void push(int i) {
        heap.push_back(i);
        size_t k = heap.size() - 1;
        const int64_t key = f(i);
        while (k != 0) {
            const size_t p = k >> 1;
            if (f(heap[p]) <= key) break;
            std::swap(heap[p], heap[k]);
            k = p;
        }
    }
    int pop() {
        const int top = heap[0];
        heap[0] = heap.back();
        heap.pop_back();
        const size_t count = heap.size();
        if (count >= 1) {
            const int64_t key = f(heap[0]);
            size_t i = 0, c = 0;
            for (;;) {
                size_t best = c;
                int64_t fb = f(heap[c]);
                if (c + 2 <= count) {
                    const int64_t fr = f(heap[c + 1]);
                    if (fr < fb) {
                        best = c + 1;
                        fb = fr;
                    }
                }
                if (key <= fb) break;
                std::swap(heap[i], heap[best]);
                i = best;
                c = 2 * best;
                if (c + 1 > count) break;
            }
        }
        return top;
    }

    // Runs to the end; fills path (empty = no path), pops, beyond.
    void run() {
        nodes.assign(static_cast<size_t>(rows) * cols, Node{});
        nodes[static_cast<size_t>(idx(start))].opened = true;
        beyond = h_of(start) >= 8000;
        push(idx(start));
        while (!heap.empty()) {
            const int cur = pop();
            ++pops;
            Node& cn = nodes[static_cast<size_t>(cur)];
            const TileCoord ct = tile(cur);
            if (cn.g + h_of(ct) >= 8000) limit_hit = true;
            if (cn.taint) {
                if (!beyond) { path.clear(); return; }
                continue;
            }
            if (ct == goal) {
                for (TileCoord t = goal;; ) {
                    path.insert(path.begin(), t);
                    if (t == start) break;
                    const Node& n = nodes[static_cast<size_t>(idx(t))];
                    static const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1}, dc[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                    t = {t.x + dc[n.dir], t.y + dr[n.dir]};
                }
                return;
            }
            for (int d = 0; d < 8; ++d) {   // N, NE, E, SE, S, SW, W, NW
                static const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1}, dc[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                const TileCoord n{ct.x + dc[d], ct.y + dr[d]};
                if (n.x < 0 || n.y < 0 || n.x >= cols || n.y >= rows) continue;
                const uint32_t step = cost(ct, n);
                const int64_t newg = static_cast<int64_t>(step) + cn.g;
                if (newg > 16777215) continue;
                Node& nn = nodes[static_cast<size_t>(idx(n))];
                if ((nn.opened || nn.closed) && nn.g <= newg) continue;
                nn.closed = false;
                nn.g = newg;
                nn.dir = (d + 4) % 8;       // the direction from the node to its parent
                nn.taint = step >= 8000;
                if (!nn.taint && newg + h_of(n) >= 8000) beyond = true;
                if (!nn.opened) {
                    nn.opened = true;
                    push(idx(n));
                }
            }
            cn.closed = true;
        }
        path.clear();
    }
};

// A cost field made of nothing but a seed: every step (from, to) has a cost of its own, mostly hundreds (so that f reaches 8000 on a few tiles), some small, some blocked, some
// exactly on the limits
uint32_t synthetic_cost(uint64_t seed, TileCoord a, TileCoord b) {
    uint64_t h = fnv_step(fnv_step(fnv_step(fnv_step(fnv_step(kFnvBasis, seed), static_cast<uint32_t>(a.x)), static_cast<uint32_t>(a.y)), static_cast<uint32_t>(b.x)), static_cast<uint32_t>(b.y));
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    const uint32_t kind = static_cast<uint32_t>(h % 100);
    const uint32_t v = static_cast<uint32_t>((h >> 8) & 0xffffff);
    const uint32_t blocked = 3 + 14 * static_cast<uint32_t>(seed % 4);   // 3, 17, 31 or 45 steps in 100 are blocked
    if (kind < blocked) return v % 3 == 0 ? 8000 : (v % 3 == 1 ? 8000 + v % 7 : 7999);
    if (kind < blocked + 12) return 1 + v % 60;
    return 250 + v % 1250;
}

// The runs that a search of `pops` pops takes when each run has the original's 1,000 pops until the run in which the pop number `limit_pop` (the first pop with f >= 8000, 0 = never)
// happened, and `budget` after it (a search is stepped slice by slice: PathManager::run gives it the budget, the search takes 1,000 of it until its limit_hit()).
uint32_t scheduled_runs(uint64_t pops, uint64_t limit_pop, uint16_t budget) {
    uint64_t done = 0;
    uint32_t runs = 0;
    bool hit = false;
    while (done < pops) {
        done = std::min<uint64_t>(pops, done + (hit ? budget : std::min<uint16_t>(budget, PathSearch::kDefaultBudget)));
        ++runs;
        if (limit_pop != 0 && limit_pop <= done) hit = true;
    }
    return runs;
}

// How many pops a search takes (stepped one pop at a time) and the number of the first pop with f >= 8000 (0 = none).
struct PopCount {
    uint64_t pops{0};
    uint64_t limit_pop{0};
    bool found{false};
};
PopCount count_pops(uint16_t rows, uint16_t cols, TileCoord start, TileCoord goal, const PathSearch::StepCostFn& cost) {
    PathGridPool pool;
    PathSearch search(1, start, goal, rows, cols, pool, false, true);
    PopCount c;
    while (!search.finished() && c.pops < 5000000) {
        search.step(1, cost);
        ++c.pops;
        if (c.limit_pop == 0 && search.limit_hit()) c.limit_pop = c.pops;
    }
    c.found = !search.path().empty();
    return c;
}

}  // namespace

void run_suite_9_wide_search() {
    TEST_SUITE("Suite 9: The Wide Search (Map Rules Word 2)");

    TEST_CASE("9.1 Differential: wherever the original search finds a path the wide search finds the same path with the same expansions; where it fails and the limit was not passed, the same failure") {
        struct Size { int w; int orders; };
        const Size sizes[] = {{48, 200}, {64, 160}, {100, 120}, {160, 80}, {256, 48}, {384, 20}, {512, 12}};
        uint64_t digest = kFnvBasis;
        int same_found = 0, same_found_beyond = 0, same_failure = 0, rescued = 0, still_failed_beyond = 0;
        for (const Flavor flavor : {Flavor::Open, Flavor::Mixed, Flavor::Walls}) {
            for (const Size& sz : sizes) {
                for (int batch = 0; batch < 4; ++batch) {
                    const uint64_t seed = static_cast<uint64_t>(sz.w) * 1000 + static_cast<uint64_t>(batch) * 17 + static_cast<uint64_t>(flavor);
                    const TerrainMap m = make_terrain(sz.w, sz.w, flavor, seed);
                    Xor rng(seed ^ 0xABCDEFu);
                    for (int i = 0; i < sz.orders / 4; ++i) {
                        const TileCoord s = random_walkable(m, rng);
                        TileCoord g = random_walkable(m, rng);
                        if (i % 7 == 3) g = {static_cast<int>(rng.below(static_cast<uint32_t>(sz.w))), static_cast<int>(rng.below(static_cast<uint32_t>(sz.w)))};   // sometimes blocked
                        else if (sz.w >= 256 && i % 3 != 0) {                  // on the big maps most orders are long ones: the ones that go past the original's limit
                            for (int tries = 0; tries < 200 && s.chebyshev_dist(g) < sz.w * 5 / 8; ++tries) g = random_walkable(m, rng);
                        }
                        const bool wall = sz.w <= 90 && (i & 1) != 0;       // (at most 90 rows there is no row 90: the flag changes nothing, as 5.4c)
                        const Trace classic = run_trace(m, s, g, false, wall);
                        const Trace wide = run_trace(m, s, g, true, wall);
                        digest = fnv_step(fnv_step(fnv_step(digest, path_digest(classic.path)), path_digest(wide.path)), fnv_step(fnv_step(classic.digest, wide.digest), wide.beyond ? 1 : 0));
                        if (!classic.path.empty()) {
                            ASSERT_TRUE(wide.path == classic.path);
                            ASSERT_EQ(wide.calls, classic.calls);
                            ASSERT_EQ(wide.digest, classic.digest);
                            ASSERT_FALSE(wide.limit_hit);                   // (the original found a path, so no pop had f >= 8000: the big budget was never given)
                            ++same_found;
                            if (wide.beyond) ++same_found_beyond;
                        } else if (!wide.beyond) {
                            ASSERT_TRUE(wide.path.empty());
                            ASSERT_EQ(wide.calls, classic.calls);
                            ASSERT_EQ(wide.digest, classic.digest);
                            ++same_failure;
                        } else if (!wide.path.empty()) {
                            ASSERT_TRUE(path_is_walkable(m, wide.path, s, g));
                            ++rescued;
                        } else {
                            ++still_failed_beyond;
                        }
                    }
                }
            }
        }
        std::cout << "[" << same_found << " found alike (" << same_found_beyond << " past the limit on the way), " << same_failure << " failed alike, " << rescued << " rescued, "
                  << still_failed_beyond << " still no path] digest " << std::hex << digest << std::dec << " ... " << std::flush;
        ASSERT_GT(same_found, 1200);
        ASSERT_GT(same_failure, 60);
        ASSERT_GT(rescued, 20);
        ASSERT_GT(still_failed_beyond, 10);
        ASSERT_EQ(digest, 0xc60152d41c3a9860ull);   // the digest of the whole run: the same on every compiler, optimization level and sanitizer
    } TEST_END();

    TEST_CASE("9.2 The model: the wide search against the rules written out, on 100,000 small cost fields with steps on and around the limit (path, expansions, the flag)") {
        int found = 0, failed = 0, past_limit = 0, rescue = 0;
        for (uint64_t seed = 1; seed <= 100000; ++seed) {
            Xor rng(seed);
            const uint16_t rows = static_cast<uint16_t>(1 + rng.below(5));
            const uint16_t cols = static_cast<uint16_t>(2 + rng.below(7));
            const TileCoord start{static_cast<int>(rng.below(cols)), static_cast<int>(rng.below(rows))};
            const TileCoord goal{static_cast<int>(rng.below(cols)), static_cast<int>(rng.below(rows))};
            const PathSearch::StepCostFn cost = [seed](TileCoord a, TileCoord b) { return synthetic_cost(seed, a, b); };
            WideModel model;
            model.rows = rows;
            model.cols = cols;
            model.start = start;
            model.goal = goal;
            model.cost = cost;
            model.run();
            PathGridPool pool;
            PathSearch search(1, start, goal, rows, cols, pool, false, true);
            uint64_t slices = 0;
            while (!search.finished() && slices < 100000) {
                search.step(1, cost);                          // budget 1: one pop a slice
                ++slices;
            }
            ASSERT_TRUE(search.path() == model.path);
            ASSERT_EQ(slices, model.pops);
            ASSERT_EQ(search.beyond(), model.beyond);
            ASSERT_EQ(search.limit_hit(), model.limit_hit);
            if (model.limit_hit && !model.beyond) ASSERT_TRUE(model.path.empty());   // (a pop at f >= 8000 with the flag clear is the original's give-up: a blocked node is popped and the search ends)
            if (model.path.empty()) ++failed; else ++found;
            if (model.beyond) ++past_limit;
            if (model.beyond && !model.path.empty()) ++rescue;
        }
        std::cout << "[" << found << " paths, " << failed << " none, " << past_limit << " past the limit, " << rescue << " paths past the limit] ... " << std::flush;
        ASSERT_GT(found, 10000);
        ASSERT_GT(failed, 3000);
        ASSERT_GT(past_limit, 10000);
        ASSERT_GT(rescue, 1000);
    } TEST_END();

    TEST_CASE("9.3 Orders beyond the original's limit on open grass: 450 tiles straight, 480 (d, d/2), 330 diagonal (the original gives up at 400, 334 and 286)") {
        const TerrainMap m = TerrainMap::filled(512, 512);
        const auto cost = m.step_cost();
        const struct { TileCoord from; TileCoord to; uint32_t length; uint32_t path_cost; } orders[] = {
            {{20, 256}, {470, 256}, 451, 9000},       // straight: 450 x 20
            {{20, 20}, {500, 260}, 481, 11520},       // (480, 240): 240 diagonal steps of 28 and 240 straight steps of 20
            {{20, 20}, {350, 350}, 331, 9240},        // diagonal: 330 x 28
        };
        for (const auto& o : orders) {
            const Trace classic = run_trace(m, o.from, o.to, false);
            ASSERT_TRUE(classic.path.empty());                                  // the original gives up: "Can't go there"
            const Trace wide = run_trace(m, o.from, o.to, true);
            ASSERT_TRUE(path_is_walkable(m, wide.path, o.from, o.to));
            ASSERT_EQ(wide.path.size(), static_cast<size_t>(o.length));
            ASSERT_EQ(path_cost(wide.path, cost), o.path_cost);                 // the cost of the best path
            ASSERT_TRUE(wide.beyond);
        }
        // the straight order is the straight line
        const Trace straight = run_trace(m, {20, 256}, {470, 256}, true);
        for (size_t i = 0; i < straight.path.size(); ++i) ASSERT_TRUE((straight.path[i] == TileCoord{20 + static_cast<int>(i), 256}));
        // up to the original's limit nothing changes: 399 tiles straight is found by both (cost 7,980, f at the goal 7,980), 400 is the first the original refuses
        for (int d : {399, 400}) {
            const Trace classic = run_trace(m, {20, 256}, {20 + d, 256}, false);
            const Trace wide = run_trace(m, {20, 256}, {20 + d, 256}, true);
            ASSERT_EQ(classic.path.empty(), d == 400);
            ASSERT_FALSE(wide.path.empty());
            if (d == 400) ASSERT_TRUE(wide.beyond);
            if (d == 399) {
                ASSERT_TRUE(wide.path == classic.path);
                ASSERT_EQ(wide.digest, classic.digest);
            }
        }
    } TEST_END();

    TEST_CASE("9.4 Past the limit by cost, not by distance: a 220-tile mud road (48 a step, the original gives up at 166); a detour around a long wall") {
        TerrainMap road = TerrainMap::filled(230, 3, 'm');
        const Trace classic = run_trace(road, {5, 1}, {225, 1}, false);
        const Trace wide = run_trace(road, {5, 1}, {225, 1}, true);
        ASSERT_TRUE(classic.path.empty());
        ASSERT_EQ(wide.path.size(), static_cast<size_t>(221));
        ASSERT_EQ(path_cost(wide.path, road.step_cost()), 220u * 48u);
        // a wall across a 300 x 40 field, its gap at the far bottom: the way round is longer than the original's limit
        TerrainMap field = TerrainMap::filled(400, 120);
        for (int y = 0; y < 117; ++y) field.set(200, y, '#');
        const Trace c2 = run_trace(field, {10, 2}, {390, 2}, false);
        const Trace w2 = run_trace(field, {10, 2}, {390, 2}, true);
        ASSERT_TRUE(c2.path.empty());
        ASSERT_TRUE(path_is_walkable(field, w2.path, {10, 2}, {390, 2}));
        ASSERT_LE(path_cost(w2.path, field.step_cost()), dijkstra_cost({10, 2}, {390, 2}, 120, 400, field.step_cost()) * 101u / 100u);
        ASSERT_TRUE(w2.beyond);
    } TEST_END();

    TEST_CASE("9.5 A blocked node that is taken before the limit is passed ends the search as in the original: the same 144, 120 and 136 expansions as 4.1 to 4.4, and a flag that stays clear") {
        TerrainMap moat = TerrainMap::filled(12, 12);
        for (int y = 6; y <= 10; ++y) {
            for (int x = 6; x <= 10; ++x) {
                if (std::max(std::abs(x - 8), std::abs(y - 8)) == 2) moat.set(x, y, '~');
            }
        }
        TerrainMap pool_goal = TerrainMap::filled(12, 12);
        pool_goal.set(3, 2, '~');
        TerrainMap rocks = TerrainMap::filled(12, 12);
        for (int y = 7; y <= 9; ++y) {
            for (int x = 7; x <= 9; ++x) {
                if (x != 8 || y != 8) rocks.set(x, y, '#');
            }
        }
        const struct { const TerrainMap* map; TileCoord s; TileCoord g; int expansions; } cases[] = {{&moat, {2, 2}, {8, 8}, 120}, {&pool_goal, {2, 2}, {3, 2}, 144}, {&rocks, {2, 2}, {8, 8}, 136}};
        for (const auto& c : cases) {
            const Trace wide = run_trace(*c.map, c.s, c.g, true);
            ASSERT_TRUE(wide.path.empty());
            ASSERT_FALSE(wide.beyond);
            ASSERT_EQ(wide.calls, run_trace(*c.map, c.s, c.g, false).calls);
            PathGridPool pool;
            PathSearch search(1, c.s, c.g, c.map->height(), c.map->width(), pool, true, true);
            int slices = 0;
            while (!search.finished()) {
                search.step(1, c.map->step_cost());
                ++slices;
            }
            ASSERT_EQ(slices, c.expansions);
        }
    } TEST_END();

    TEST_CASE("9.6 An unreachable goal past the limit: the wide search sweeps everything it can reach and drops the blocked nodes (the original gives up at the first of them)") {
        const auto sweep = [](const TerrainMap& field, TileCoord s, TileCoord g, bool wide, bool* beyond) {
            PathGridPool pool;
            PathSearch search(1, s, g, field.height(), field.width(), pool, false, wide);
            const PathSearch::StepCostFn cost = field.step_cost();
            int slices = 0;
            while (!search.finished() && slices < 1000000) {
                search.step(1, cost);
                ++slices;
            }
            *beyond = search.beyond();
            return search.path().empty() ? slices : -1;
        };
        bool beyond = false;
        {   // a start that is 500 columns from the goal has f = 8,000 at once: past the limit before the first expansion (a start 499 columns away is not)
            PathGridPool pool;
            const TerrainMap strip = TerrainMap::filled(512, 1);
            ASSERT_TRUE(PathSearch(1, {0, 0}, {500, 0}, 1, 512, pool, false, true).beyond());
            ASSERT_FALSE(PathSearch(1, {0, 0}, {499, 0}, 1, 512, pool, false, true).beyond());
            ASSERT_FALSE(PathSearch(1, {0, 0}, {511, 0}, 1, 512, pool, false, false).beyond());       // (the original's search never sets it)
        }
        // 512 x 12 grass cut by a rock wall at column 250: the goal, 500 columns from the start, is already past the limit (16 x 500 = 8,000) at the first node
        TerrainMap field = TerrainMap::filled(512, 12);
        for (int y = 0; y < 12; ++y) field.set(250, y, '#');
        ASSERT_EQ(sweep(field, {5, 5}, {505, 5}, false, &beyond), 1);
        ASSERT_FALSE(beyond);
        ASSERT_EQ(sweep(field, {5, 5}, {505, 5}, true, &beyond), 250 * 12 + 12);        // the 3,000 tiles west of the wall and the 12 rocks, each once
        ASSERT_TRUE(beyond);
        // 300 x 12 of mud (48 a step) with the same kind of wall at column 150: the limit is passed on the way, at about column 120
        TerrainMap mud = TerrainMap::filled(300, 12, 'm');
        for (int y = 0; y < 12; ++y) mud.set(150, y, '#');
        const int classic_pops = sweep(mud, {10, 5}, {290, 5}, false, &beyond);
        ASSERT_FALSE(beyond);
        const int wide_pops = sweep(mud, {10, 5}, {290, 5}, true, &beyond);
        ASSERT_TRUE(beyond);
        ASSERT_LT(classic_pops, 1800);
        ASSERT_EQ(wide_pops, 150 * 12 + 12);                                            // every tile west of the wall (150 columns of 12) and the 12 rocks, once each
    } TEST_END();

    TEST_CASE("9.7 g has 24 bits: a road of steps that cost 7,999 is walked to 16,000,000 and no further; a road of 1,000 steps is not cut") {
        const auto strip = [](int n, uint32_t step) {
            return [n, step](TileCoord a, TileCoord b) -> uint32_t { return (a.x < 0 || b.x < 0 || a.x >= n || b.x >= n) ? kBlocked : step; };
        };
        const PathSearch::StepCostFn cost = strip(3000, 7999);
        const Trace short_road = run_trace(1, 3000, {0, 0}, {1000, 0}, cost, true);
        ASSERT_EQ(short_road.path.size(), static_cast<size_t>(1001));
        const Trace long_road = run_trace(1, 3000, {0, 0}, {2000, 0}, cost, true);         // g = 15,998,000 at the goal: below 16,777,215
        ASSERT_EQ(long_road.path.size(), static_cast<size_t>(2001));
        const Trace too_long = run_trace(1, 3000, {0, 0}, {2200, 0}, cost, true);          // 17,597,800: does not fit
        ASSERT_TRUE(too_long.path.empty());
        ASSERT_EQ(too_long.calls, 4195u);                                                   // the search stops where g stops fitting (node 2,098); without the guard it runs on over wrapped costs
        ASSERT_TRUE(run_trace(1, 3000, {0, 0}, {2200, 0}, cost, false).path.empty());       // (the original gives up at the first step)
    } TEST_END();

    TEST_CASE("9.8 The wide flag and the limit are state of a search: a copy of a manager made in the middle of a wide search (constructor and assignment) finishes it as the original does, at the same run") {
        const TerrainMap field = make_terrain(256, 256, Flavor::Walls, 77);
        Xor rng(5);
        const PathManager::CostProvider provider = field.provider();
        constexpr uint16_t kSlice = 300;
        int copied = 0;
        for (int round = 0; round < 12; ++round) {
            TileCoord s = random_walkable(field, rng);
            TileCoord g = random_walkable(field, rng);
            while (s.chebyshev_dist(g) < 120) g = random_walkable(field, rng);
            PathManager reference;
            reference.request(9, s, g, field.height(), field.width(), false, true);
            std::optional<PathManager::Delivery> want;
            int want_runs = 0;
            while (!want && want_runs < 2000) {
                want = reference.run(provider, kSlice);
                ++want_runs;
            }
            ASSERT_TRUE(want.has_value());
            ASSERT_GT(want_runs, 4);
            PathManager original;
            original.request(9, s, g, field.height(), field.width(), false, true);
            for (int r = 0; r < 4; ++r) ASSERT_FALSE(original.run(provider, kSlice).has_value());
            PathManager by_constructor(original);
            PathManager by_assignment;
            by_assignment.request(1, {0, 0}, {1, 1}, field.height(), field.width());   // (it has a search of its own, which the assignment replaces)
            by_assignment = original;
            for (PathManager* pm : {&original, &by_constructor, &by_assignment}) {
                std::optional<PathManager::Delivery> got;
                int runs = 4;
                while (!got && runs < 2000) {
                    got = pm->run(provider, kSlice);
                    ++runs;
                }
                ASSERT_TRUE(got.has_value());
                ASSERT_EQ(runs, want_runs);
                ASSERT_TRUE(got->path == want->path);
            }
            ++copied;
        }
        ASSERT_EQ(copied, 12);
        // a start that is 500 columns from the goal has passed the limit before the first step; it is shut in by rocks, so every node it makes is blocked: with the flag
        // (the copy has it, as the original) the 5 rocks are dropped one after the other, without it the first of them would end the search
        {
            TerrainMap shut = TerrainMap::filled(512, 3);
            shut.set(0, 0, '#');
            shut.set(1, 0, '#');
            shut.set(1, 1, '#');
            shut.set(1, 2, '#');
            shut.set(0, 2, '#');
            const PathManager::CostProvider shut_provider = shut.provider();
            PathManager original;
            original.request(4, {0, 1}, {500, 1}, 3, 512, false, true);
            PathManager copy(original);
            PathManager assigned;
            assigned = original;
            const auto runs_until_delivery = [&](PathManager& pm) {
                for (int r = 1; r < 100; ++r) {
                    if (const std::optional<PathManager::Delivery> d = pm.run(shut_provider, 2)) return d->path.empty() ? r : -r;
                }
                return 0;
            };
            const int runs = runs_until_delivery(original);
            ASSERT_EQ(runs, 3);                                   // 6 pops (the start and its 5 rocks) at 2 a run
            ASSERT_EQ(runs_until_delivery(copy), runs);
            ASSERT_EQ(runs_until_delivery(assigned), runs);
        }
        // a copy made after the limit was passed: the flag is part of what is copied (without it the first blocked node ends the search)
        TerrainMap wall = TerrainMap::filled(400, 120);
        for (int y = 0; y < 117; ++y) wall.set(200, y, '#');
        const PathManager::CostProvider wall_provider = wall.provider();
        PathGridPool probe_pool;
        PathSearch probe(1, {10, 2}, {390, 2}, 120, 400, probe_pool, false, true);
        int slices_to_limit = 0;
        while (!probe.beyond() && !probe.finished()) {
            probe.step(kSlice, wall.step_cost());
            ++slices_to_limit;
        }
        ASSERT_TRUE(probe.beyond() && !probe.finished());
        PathManager m1;
        m1.request(1, {10, 2}, {390, 2}, 120, 400, false, true);
        for (int r = 0; r < slices_to_limit; ++r) ASSERT_FALSE(m1.run(wall_provider, kSlice).has_value());
        PathManager m2(m1);
        std::optional<PathManager::Delivery> d2, d1;
        for (int r = 0; r < 2000 && !d2; ++r) d2 = m2.run(wall_provider, kSlice);
        for (int r = slices_to_limit; r < 2000 && !d1; ++r) d1 = m1.run(wall_provider, kSlice);
        ASSERT_TRUE(d1.has_value() && d2.has_value());
        ASSERT_FALSE(d1->path.empty());
        ASSERT_TRUE(d2->path == d1->path);
    } TEST_END();

    TEST_CASE("9.9 The budget: a wide search that has passed the original's limit gets max(1000, cells / 32) steps a run, before that (and at every other rules word) 1,000") {
        ASSERT_EQ(PathManager::wide_budget(60, 60), 1000u);
        ASSERT_EQ(PathManager::wide_budget(81, 129), 1000u);                 // the biggest map of the library
        ASSERT_EQ(PathManager::wide_budget(250, 128), 1000u);                // 32,000 cells
        ASSERT_EQ(PathManager::wide_budget(1, 32001), 1000u);                // 1,000.03
        ASSERT_EQ(PathManager::wide_budget(1, 32032), 1001u);
        ASSERT_EQ(PathManager::wide_budget(1, 32063), 1001u);
        ASSERT_EQ(PathManager::wide_budget(1, 32064), 1002u);
        ASSERT_EQ(PathManager::wide_budget(256, 256), 2048u);
        ASSERT_EQ(PathManager::wide_budget(400, 400), 5000u);
        ASSERT_EQ(PathManager::wide_budget(512, 512), 8192u);
        ASSERT_EQ(PathManager::wide_budget(1024, 256), 8192u);
        ASSERT_EQ(PathManager::wide_budget(2048, 2048), 65535u);             // (the manager's budget is 16 bits)
        ASSERT_EQ(static_cast<unsigned>(PathSearch::kDefaultBudget), 1000u);
        // a search of 5,400 expansions that stays below f = 8,000 (the original's limit is not passed) takes 1,000 a run whatever it is given: owner's rule, a search that the original
        // can finish is delivered in the run in which the original delivers it (before, it took the big budget from the first run on: 6 runs of 1,000 became one of 8,192)
        const TerrainMap open = TerrainMap::filled(512, 512);
        const PathManager::CostProvider provider = open.provider();
        for (const uint16_t budget : {uint16_t{1000}, uint16_t{2048}, uint16_t{8192}}) {
            PathManager pm;
            pm.request(3, {10, 10}, {60, 40}, 512, 512, false, true);
            int runs = 0;
            std::optional<PathManager::Delivery> d;
            while (!d && runs < 1000) {
                d = pm.run(provider, budget);
                ++runs;
            }
            ASSERT_TRUE(d.has_value() && !d->path.empty());
            PathGridPool pool;
            PathSearch ps(1, {10, 10}, {60, 40}, 512, 512, pool, false, true);
            int pops = 0;
            while (!ps.finished()) {
                ps.step(1, open.step_cost());
                ++pops;
            }
            ASSERT_FALSE(ps.limit_hit());
            ASSERT_EQ(runs, (pops + 1000 - 1) / 1000);                       // the manager gave each run the original's 1,000 expansions, not `budget`
        }
        // the default of run() is the original's 1,000
        PathManager a, b;
        a.request(3, {10, 10}, {60, 40}, 512, 512, false, true);
        b.request(3, {10, 10}, {60, 40}, 512, 512, false, true);
        int ra = 0, rb = 0;
        std::optional<PathManager::Delivery> da, db;
        while (!da) { da = a.run(provider); ++ra; }
        while (!db) { db = b.run(provider, PathSearch::kDefaultBudget); ++rb; }
        ASSERT_TRUE(ra == rb && da->path == db->path);
    } TEST_END();

    TEST_CASE("9.10 A search that is not wide is the original's: the flag is off by default, and request() without it never goes beyond the limit") {
        const TerrainMap m = TerrainMap::filled(512, 512);
        PathGridPool pool;
        PathSearch plain(1, {20, 256}, {470, 256}, 512, 512, pool);
        ASSERT_FALSE(plain.wide());
        while (!plain.finished()) plain.step(PathSearch::kDefaultBudget, m.step_cost());
        ASSERT_TRUE(plain.path().empty());
        ASSERT_FALSE(plain.beyond());
        PathManager pm;
        pm.request(1, {20, 256}, {470, 256}, 512, 512, false);
        std::optional<PathManager::Delivery> d;
        for (int i = 0; i < 400 && !d; ++i) d = pm.run(m.provider(), 8192);
        ASSERT_TRUE(d.has_value() && d->path.empty());
        PathManager wide;
        wide.request(1, {20, 256}, {470, 256}, 512, 512, false, true);
        std::optional<PathManager::Delivery> dw;
        for (int i = 0; i < 400 && !dw; ++i) dw = wide.run(m.provider(), 8192);
        ASSERT_TRUE(dw.has_value() && dw->path.size() == 451);
    } TEST_END();

    TEST_CASE("9.11 A step cost that does not fit 32 bits is not added with a wrap: 0xFFFFFFFF on one step (with g > 0) is refused, and the search goes round it") {
        // a 4 x 2 field, grass at 20 a step; the one step (1, 0) -> (2, 0) is said to cost 0xFFFFFFFF. g(1, 0) is 20, so g + step wraps to 19 in 32 bits: without the test of the sum the node
        // (2, 0) would get g = 19, the best cost of any node, and (a step of 8000 or more) a taint: the search would end there with no path. The engine's costs are at most 8,000 (the guard
        // is for a provider that is not the engine's), so this is the only place that reaches it.
        const PathSearch::StepCostFn cost = [](TileCoord a, TileCoord b) -> uint32_t {
            if (b.x < 0 || b.y < 0 || b.x >= 4 || b.y >= 2) return kBlocked;
            if (a == TileCoord{1, 0} && b == TileCoord{2, 0}) return 0xFFFFFFFFu;
            return a.x != b.x && a.y != b.y ? 28u : 20u;
        };
        const Trace wide = run_trace(2, 4, {0, 0}, {3, 0}, cost, true);
        ASSERT_FALSE(wide.path.empty());
        ASSERT_TRUE((wide.path.front() == TileCoord{0, 0}) && (wide.path.back() == TileCoord{3, 0}));
        for (size_t i = 1; i < wide.path.size(); ++i) ASSERT_FALSE((wide.path[i - 1] == TileCoord{1, 0} && wide.path[i] == TileCoord{2, 0}));
        ASSERT_FALSE(wide.beyond);
        ASSERT_FALSE(wide.limit_hit);
        // the same step, taken from the start (g = 0): 0 + 0xFFFFFFFF fits 32 bits but not the 24 of g, and is refused the same way
        const PathSearch::StepCostFn from_start = [](TileCoord a, TileCoord b) -> uint32_t {
            if (b.x < 0 || b.y < 0 || b.x >= 4 || b.y >= 2) return kBlocked;
            if (a == TileCoord{0, 0} && b == TileCoord{1, 0}) return 0xFFFFFFFFu;
            return a.x != b.x && a.y != b.y ? 28u : 20u;
        };
        const Trace round_it = run_trace(2, 4, {0, 0}, {3, 0}, from_start, true);
        ASSERT_FALSE(round_it.path.empty());
        for (size_t i = 1; i < round_it.path.size(); ++i) ASSERT_FALSE((round_it.path[i - 1] == TileCoord{0, 0} && round_it.path[i] == TileCoord{1, 0}));
    } TEST_END();

    TEST_CASE("9.12 A path found while the flag is set: the original finds it, the wide search finds the same path with the same expansions, and the big budget is never given (tolls of 7,990 push nodes past f = 8,000 without being popped)") {
        // a toll tile costs 7,990 to enter (not blocked: below 8,000); the node behind it has f >= 8,000, which sets the flag when it is pushed, but it stays in the open list because the goal
        // is found at a lower f. 9.1 meets this case about once in a thousand orders; here every order has a toll beside the way.
        int found_with_flag = 0, flag_orders = 0;
        uint64_t digest = kFnvBasis;
        for (uint64_t seed = 1; seed <= 600; ++seed) {
            Xor rng(seed * 7919);
            const uint16_t side = static_cast<uint16_t>(16 + rng.below(40));
            const TileCoord s{static_cast<int>(rng.below(side)), static_cast<int>(rng.below(side))};
            TileCoord g{static_cast<int>(rng.below(side)), static_cast<int>(rng.below(side))};
            const uint32_t toll_modulus = 3 + static_cast<uint32_t>(seed % 9);   // 1 tile in 3 to 11 is a toll tile
            const auto toll = [seed, toll_modulus, g](TileCoord t) { return !(t == g) && (fnv_step(fnv_step(seed, static_cast<uint32_t>(t.x)), static_cast<uint32_t>(t.y)) >> 7) % toll_modulus == 0; };
            const PathSearch::StepCostFn cost = [&](TileCoord a, TileCoord b) -> uint32_t {
                if (b.x < 0 || b.y < 0 || b.x >= side || b.y >= side) return kBlocked;
                if (toll(b)) return 7990u;
                return a.x != b.x && a.y != b.y ? 28u : 20u;
            };
            if (toll(s) || s == g) continue;
            ++flag_orders;
            const Trace classic = run_trace(side, side, s, g, cost, false, false, PathSearch::kDefaultBudget);
            const Trace wide = run_trace(side, side, s, g, cost, true, false, PathSearch::kDefaultBudget);
            digest = fnv_step(fnv_step(fnv_step(digest, path_digest(classic.path)), path_digest(wide.path)), wide.beyond ? 1 : 0);
            if (classic.path.empty()) continue;                                 // (the tolls shut the goal in: not a case of this test)
            ASSERT_TRUE(wide.path == classic.path);
            ASSERT_EQ(wide.calls, classic.calls);
            ASSERT_EQ(wide.digest, classic.digest);
            ASSERT_FALSE(wide.limit_hit);
            ASSERT_EQ(wide.slices, classic.slices);                              // (the same number of 1,000-pop slices: the timing is the original's)
            if (wide.beyond) ++found_with_flag;
        }
        std::cout << "[" << found_with_flag << " of " << flag_orders << " orders found with the flag set] digest " << std::hex << digest << std::dec << " ... " << std::flush;
        ASSERT_GT(found_with_flag, 400);
        ASSERT_EQ(digest, 0x0a8341a6f46413bcull);   // the digest of the whole run, as in 9.1
    } TEST_END();

    TEST_CASE("9.13 The budget schedule: 1,000 a run until the pop at which the original would have given up, the budget of the map after; a map of 190 x 190 delivers what the original can handle in the original's runs") {
        // (a) 190 x 190 open grass, 36,100 cells: wide_budget is 1,128. Orders of 40 to 90 tiles take about 700 to 3,500 pops (48 to 50 tiles: 1,001 to 1,128); a run of 1,128 would deliver one that takes 1,001 to 1,128 a run
        // earlier than the original's 1,000 does.
        const TerrainMap field = TerrainMap::filled(190, 190);
        ASSERT_EQ(PathManager::wide_budget(190, 190), 1128u);
        const PathManager::CostProvider provider = field.provider();
        int took_more_than_one_run = 0, in_the_window = 0;
        for (int dist = 40; dist <= 90; ++dist) {
            PathManager classic, wide;
            classic.request(1, {20, 95}, {20 + dist, 95}, 190, 190, false, false);
            wide.request(1, {20, 95}, {20 + dist, 95}, 190, 190, false, true);
            std::optional<PathManager::Delivery> dc, dw;
            int rc = 0, rw = 0;
            while (!dc && rc < 100) { dc = classic.run(provider, PathSearch::kDefaultBudget); ++rc; }
            while (!dw && rw < 100) { dw = wide.run(provider, PathManager::wide_budget(190, 190)); ++rw; }
            ASSERT_TRUE(dc.has_value() && dw.has_value());
            ASSERT_FALSE(dc->path.empty());
            ASSERT_TRUE(dw->path == dc->path);
            ASSERT_EQ(rw, rc);
            if (rc > 1) ++took_more_than_one_run;
            const PopCount pc = count_pops(190, 190, {20, 95}, {20 + dist, 95}, field.step_cost());
            if (pc.pops > 1000 && pc.pops <= 1128) ++in_the_window;          // (the orders for which a run of 1,128 would have been one run too few)
        }
        ASSERT_GT(took_more_than_one_run, 35);
        ASSERT_GE(in_the_window, 1);
        // (b) the same with a toll beside the way (the flag is set on the way, no pop reaches f = 8,000): the original's runs
        const PathManager::CostProvider toll_provider = [&](uint32_t id, TileCoord a, TileCoord b) -> uint32_t { return b == TileCoord{50, 97} ? 7990u : provider(id, a, b); };
        for (int dist = 40; dist <= 90; ++dist) {
            PathManager classic, wide;
            classic.request(1, {20, 95}, {20 + dist, 95}, 190, 190, false, false);
            wide.request(1, {20, 95}, {20 + dist, 95}, 190, 190, false, true);
            std::optional<PathManager::Delivery> dc, dw;
            int rc = 0, rw = 0;
            while (!dc && rc < 100) { dc = classic.run(toll_provider, PathSearch::kDefaultBudget); ++rc; }
            while (!dw && rw < 100) { dw = wide.run(toll_provider, PathManager::wide_budget(190, 190)); ++rw; }
            ASSERT_TRUE(dc.has_value() && dw.has_value() && !dc->path.empty() && dw->path == dc->path);
            ASSERT_EQ(rw, rc);
        }
        // (c) after the limit: 450 tiles straight on 512 x 512 (the original gives up): the schedule is 1,000 a run up to and including the run of the first pop with f >= 8000, then the
        // budget of the map, however it is given
        const TerrainMap big = TerrainMap::filled(512, 512);
        const PopCount pc = count_pops(512, 512, {20, 256}, {470, 256}, big.step_cost());
        ASSERT_TRUE(pc.found);
        ASSERT_GT(pc.limit_pop, static_cast<uint64_t>(10000));                   // (a long way of searching before the limit is met)
        ASSERT_GT(pc.pops, pc.limit_pop);
        const PathManager::CostProvider big_provider = big.provider();
        for (const uint16_t budget : {uint16_t{1000}, uint16_t{2048}, uint16_t{8192}, uint16_t{30000}}) {
            PathManager pm;
            pm.request(1, {20, 256}, {470, 256}, 512, 512, false, true);
            uint32_t runs = 0;
            std::optional<PathManager::Delivery> d;
            while (!d && runs < 5000) { d = pm.run(big_provider, budget); ++runs; }
            ASSERT_TRUE(d.has_value() && d->path.size() == 451);
            ASSERT_EQ(runs, scheduled_runs(pc.pops, pc.limit_pop, budget));
        }
        ASSERT_LT(scheduled_runs(pc.pops, pc.limit_pop, 8192), scheduled_runs(pc.pops, 0, 8192) / 2);    // (the big budget does shorten the search)
        ASSERT_GT(scheduled_runs(pc.pops, pc.limit_pop, 8192), (static_cast<uint32_t>(pc.pops) + 8191) / 8192);   // ... but it is not given from the first run
        // (d) a copy of a manager made after the search has passed the limit (the constructor and the assignment) keeps the big budget: it delivers in the run of the original
        {
            const uint32_t runs_to_limit = static_cast<uint32_t>((pc.limit_pop + 999) / 1000);   // the run in which the first pop at f >= 8,000 happens
            const uint32_t want = scheduled_runs(pc.pops, pc.limit_pop, 8192);
            PathManager original;
            original.request(1, {20, 256}, {470, 256}, 512, 512, false, true);
            for (uint32_t r = 0; r < runs_to_limit; ++r) ASSERT_FALSE(original.run(big_provider, 8192).has_value());
            PathManager by_constructor(original);
            PathManager by_assignment;
            by_assignment = original;
            for (PathManager* pm : {&original, &by_constructor, &by_assignment}) {
                uint32_t runs = runs_to_limit;
                std::optional<PathManager::Delivery> d;
                while (!d && runs < 5000) { d = pm->run(big_provider, 8192); ++runs; }
                ASSERT_TRUE(d.has_value() && d->path.size() == 451);
                ASSERT_EQ(runs, want);
            }
        }
        // a budget below 1,000 is kept (the wide search never takes more than it is given): two runs of 100 pops do not deliver a search of thousands
        PathManager small;
        small.request(1, {20, 256}, {470, 256}, 512, 512, false, true);
        ASSERT_FALSE(small.run(big_provider, 100).has_value());
        ASSERT_FALSE(small.run(big_provider, 100).has_value());
        const TerrainMap tiny = TerrainMap::filled(8, 3);
        PathManager one_pop;
        one_pop.request(1, {0, 1}, {7, 1}, 3, 8, false, true);
        int tiny_runs = 0;
        std::optional<PathManager::Delivery> dt;
        while (!dt && tiny_runs < 1000) { dt = one_pop.run(tiny.provider(), 1); ++tiny_runs; }
        ASSERT_EQ(static_cast<uint64_t>(tiny_runs), count_pops(3, 8, {0, 1}, {7, 1}, tiny.step_cost()).pops);   // one pop a run when it is given one
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
    run_suite_9_wide_search();

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
