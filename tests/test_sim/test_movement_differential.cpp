// Differential regression tests for the original movement and path search.
//
// Two independent models of the original executable are compared with the remake on many random scenarios. The
// models were written from the disassembly of Ants.exe for the movement audit (docs/history/AUDIT_ONE_TO_ONE.md, batch 8)
// and read only the raw data of the game files: the static animation tables of Ants.exe and the Table-4 frames of
// ants.chd. They share no code with src/ants_sim, so a change of a table, of the stepping rules or of the search
// cannot pass unnoticed even when the hand-written golden cases (test_movement_golden, test_path_planner) do not
// happen to exercise it.
//
// The program's tables: the original program is not part of the repository. With a local copy in
// Original-Ants/Ants.exe the models read its bytes; without one they read the remake's generated tables laid out like
// the program's. Either way the bytes are first checked against the pinned SHA-256 digests of the program's bytes
// (tests/common/original_program_bytes.hpp, test 0.1), so what the models read is byte for byte the original's: a
// changed remake table fails that check instead of passing unnoticed.
//
//   * A* model   PathRequest::Step FUN_01019a66 with its 0-rooted heap FUN_01019e20 / FUN_01019f0a: 32-bit search
//                cells (g bits 0..13, h bits 14..26, parent direction bits 27..29, opened bit 30, closed bit 31),
//                live f reads, no decrease-key, failure when a popped f >= 8000 (tested before the goal), the
//                neighbours N..NW with the map-edge rules, step cost (Ca + Cb) >> 1 or trunc(1.4 * (Ca + Cb)) >> 1.
//                Compared with ants::sim::PathSearch, slice budgets 1000 / 37 / 5.
//   * Walk model the walk of a delivered path: animation stepper FUN_0102b997 (millisecond clock, a clip started
//                inside a step callback books its first frame twice), WalkStep FUN_0101b8cb (idiv tile detection,
//                the +-1 nudge into a tile that is not the next waypoint, the <= 2 px snap, arrival, the terrain
//                restart at a tile boundary), SetAction FUN_0101ad02 (direction change restarts the clip) and the
//                end of the path (StopSync / StopAt). Compared with the locomotion trace of the SimulationEngine:
//                every position change, with its time, of ant types x terrains x facings x carried food.
//
// Coordinates: the models use (row, column) like the original; TileCoord is {x = column, y = row}.

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/path_planner.hpp"
#include "ants_sim/sim_engine.hpp"
#include "original_program_bytes.hpp"  // the bytes of Ants.exe that the models read, with their pinned digests (tests/common)

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ants_test_paths.hpp"

namespace fs = std::filesystem;
using namespace ants::sim;
using ants::assets::AssetArchive;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name)                                                        \
    std::cout << "\n=======================================================\n" \
              << " [MOVEMENT DIFFERENTIAL SUITE] " << name << "\n"               \
              << "=======================================================\n"

static void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(66) << name << " ... " << std::flush;
    const int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

#define ASSERT_TRUE(cond)                                                                                              \
    do {                                                                                                               \
        ++g_assert_count;                                                                                              \
        if (!(cond)) {                                                                                                 \
            std::cout << "FAILED!\n    Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << "\n";     \
            ++g_test_failures;                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ(a, b)                                                                                                \
    do {                                                                                                               \
        ++g_assert_count;                                                                                              \
        const auto lhs_ = (a);                                                                                         \
        const auto rhs_ = (b);                                                                                         \
        if (!(lhs_ == rhs_)) {                                                                                         \
            std::cout << "FAILED!\n    Assertion failed: " #a " == " #b " (" << lhs_ << " vs " << rhs_ << ") at "       \
                      << __FILE__ << ":" << __LINE__ << "\n";                                                          \
            ++g_test_failures;                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

namespace {

// The folder of the original's data: ants.chd alone decides (Ants.exe is a local copy that a clone does not have).
std::string locate_assets_dir() {
    const std::vector<std::string> candidates = {
#ifdef ORIGINAL_ASSETS_DIR
        ORIGINAL_ASSETS_DIR,
#endif
        "Original-Ants", "../Original-Ants", "../../Original-Ants", "../../../Original-Ants",
    };
    for (const auto& path : candidates) {
        if (fs::exists(path + "/ants.chd")) return path;
    }
    return "Original-Ants";
}

// ---------------------------------------------------------------------------------------------------------------
// Raw data of the original
// ---------------------------------------------------------------------------------------------------------------

// The bytes of Ants.exe at virtual addresses (image base 0x01000000): from the program, or from the remake's tables laid
// out like the program's, either way checked against the pinned digests (tests/common/original_program_bytes.hpp).
using ProgramBytes = original_program::ProgramBytes;

// The pinned regions of Ants.exe that the models read.
const std::vector<std::string> kModelRegions = {"walk", "carry walk", "idle", "carry idle", "swim", "idle in water"};

struct Frame {
    int32_t dx;
    int32_t dy;
    uint32_t dur;
};

// An entry the model never reads: the directions 5..7 of a row hold the virtual ids of the mirrored copies that the
// original makes at start (FUN_01018a7a); the model mirrors the clips of 3, 2, 1 itself (src()).
constexpr uint16_t kNotStored = 0xFFFF;

// The rows of a colour-0 animation table: 8 directions per row (16 bytes), of which the original stores 0..4.
std::vector<uint16_t> stored_rows(const ProgramBytes& exe, uint32_t va, uint32_t rows) {
    std::vector<uint16_t> out(8u * rows, kNotStored);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t d = 0; d < 5; ++d) out[8u * r + d] = exe.u16(va + 16u * r + 2u * d);
    }
    return out;
}

// The animation tables of the walk model, read straight from the executable (colour-0 blocks), and the Table-4
// frames of the archive.
class Original {
public:
    Original(const ProgramBytes& exe, const AssetArchive& chd)
        : chd_(chd),
          walk_(stored_rows(exe, 0x1002FB8, 30)),   // [type 6][terrain 5][dir 8]
          carry_(stored_rows(exe, 0x1003738, 30)),  // the same while carrying food
          idle_(stored_rows(exe, 0x1002CB8, 6)),    // [type 6][dir 8]
          carry_idle_(stored_rows(exe, 0x1002E38, 6)),
          swim_(stored_rows(exe, 0x1004838, 1)),    // swimmer on water [dir 8]
          idle_water_(exe.u16(0x10048B8)) {}

    // The stored direction of the table for a facing 0..7 (5, 6, 7 use the clips of 3, 2, 1 mirrored).
    static int src(int d) {
        static const int kSrc[8] = {0, 1, 2, 3, 4, 3, 2, 1};
        return kSrc[d];
    }

    std::vector<Frame> frames(uint16_t index, bool mirrored) const {
        if (index == kNotStored) throw std::logic_error("the model read a direction 5..7 entry of an animation table");
        std::vector<Frame> out;
        for (const auto& sub : chd_.get_animation(index).subitems) {
            out.push_back({mirrored ? -sub.val1 : sub.val1, sub.val2, sub.val3});
        }
        return out;
    }

    std::vector<Frame> walk_frames(int type, int terrain, int dir, bool carrying) const {
        if (terrain == 2 && type == 5) return frames(swim_.at(static_cast<size_t>(src(dir))), dir > 4);
        if (terrain == 2) terrain = 3;
        const auto& table = carrying ? carry_ : walk_;
        return frames(table.at(static_cast<size_t>(40 * type + 8 * terrain + src(dir))), dir > 4);
    }

    std::vector<Frame> idle_frames(int type, int dir, bool carrying, int terrain) const {
        if (terrain == 2) return frames(idle_water_, false);
        const auto& table = carrying ? carry_idle_ : idle_;
        return frames(table.at(static_cast<size_t>(8 * type + src(dir))), dir > 4);
    }

private:
    const AssetArchive& chd_;
    std::vector<uint16_t> walk_, carry_, idle_, carry_idle_, swim_;
    uint16_t idle_water_;
};

// ---------------------------------------------------------------------------------------------------------------
// A* model (PathRequest::Step)
// ---------------------------------------------------------------------------------------------------------------

using Tile = std::pair<int, int>;  // (row, column)

constexpr uint32_t kClassWeight[6] = {20, 16, 8000, 48, 24, 8000};
constexpr int kDRow[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kDCol[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDirTable[9] = {7, 0, 1, 6, 0, 2, 5, 4, 3};  // 0x1002b28: the direction by [drow + 1][dcol + 1]

int dir_between(Tile a, Tile b) {
    const int dr = b.first - a.first + 1;
    const int dc = b.second - a.second + 1;
    if (dr < 0 || dr > 2 || dc < 0 || dc > 2) throw std::runtime_error("the two tiles are not neighbours");
    return kDirTable[dr * 3 + dc];
}

uint32_t cheb16(Tile a, Tile b) {
    const int dr = a.first > b.first ? a.first - b.first : b.first - a.first;
    const int dc = a.second > b.second ? a.second - b.second : b.second - a.second;
    return static_cast<uint32_t>(dr > dc ? dr : dc) << 4;
}

struct AStarMap {
    int rows{0};
    int cols{0};
    Tile start{0, 0};
    Tile goal{0, 0};
    uint16_t budget{1000};
    std::vector<std::string> cls;  // terrain class digits, '5' = wall
};

class AStarModel {
public:
    explicit AStarModel(const AStarMap& map) : map_(map), cell_(static_cast<size_t>(map.rows * map.cols), 0u) {}

    // The path start ... goal, or empty when the original finds none.
    std::vector<Tile> run() {
        C(map_.start) |= 0x40000000u;
        C(map_.start) = (C(map_.start) & 0xf8003fffu) | ((cheb16(map_.start, map_.goal) & 0x1fffu) << 14);
        push(map_.start);
        for (;;) {  // one PATHMGR run per pass of this loop: at most `budget` expansions
            int it = 0;
            if (!heap_.empty()) {
                while (it < map_.budget) {
                    ++it;
                    const Tile cur = pop();
                    const uint32_t cc = C(cur);
                    if ((cc & 0x3fffu) + ((cc >> 14) & 0x1fffu) >= 8000u) return {};
                    if (cur == map_.goal) return build(cur);
                    expand(cur);
                    C(cur) |= 0x80000000u;
                    if (heap_.empty()) break;
                }
            }
            if (!heap_.empty()) continue;  // the budget is used up: the next slice
            return {};
        }
    }

private:
    uint32_t& C(Tile t) { return cell_.at(static_cast<size_t>(t.first * map_.cols + t.second)); }
    uint32_t f(Tile t) {
        const uint32_t c = C(t);
        return (c & 0x3fffu) + ((c >> 14) & 0x1fffu);
    }

    void push(Tile t) {
        heap_.push_back(t);
        size_t i = heap_.size() - 1;
        const uint32_t key = f(t);
        while (i != 0) {
            const size_t p = i >> 1;
            if (f(heap_[p]) <= key) break;
            std::swap(heap_[p], heap_[i]);
            i = p;
        }
    }

    Tile pop() {
        const Tile top = heap_.front();
        heap_.front() = heap_.back();
        heap_.pop_back();
        const size_t count = heap_.size();
        if (count >= 1) {
            const uint32_t key = f(heap_[0]);
            size_t i = 0;
            size_t c = 0;
            for (;;) {
                size_t best = c;
                uint32_t fb = f(heap_.at(c));
                if (c + 2 <= count) {
                    const uint32_t fr = f(heap_.at(c + 1));
                    if (fr < fb) {
                        best = c + 1;
                        fb = fr;
                    }
                }
                if (key <= fb) break;
                std::swap(heap_[i], heap_.at(best));
                i = best;
                c = 2 * best;
                if (c + 1 > count) break;
            }
        }
        return top;
    }

    uint32_t cost(Tile a, Tile b) const {
        const uint32_t ca = kClassWeight[map_.cls.at(static_cast<size_t>(a.first)).at(static_cast<size_t>(a.second)) - '0'];
        const uint32_t cb = kClassWeight[map_.cls.at(static_cast<size_t>(b.first)).at(static_cast<size_t>(b.second)) - '0'];
        if (ca == 8000u || cb == 8000u) return 8000u;
        uint32_t s = ca + cb;
        if (a.first != b.first && a.second != b.second) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
        return s >> 1;
    }

    void expand(Tile cur) {
        const int r = cur.first;
        const int c = cur.second;
        bool valid[8] = {true, true, true, true, true, true, true, true};
        if (r == 0) {
            valid[0] = valid[1] = valid[7] = false;
        } else if (r == map_.rows - 1) {
            valid[3] = valid[4] = valid[5] = false;
        }
        if (c == 0) {
            valid[5] = valid[6] = valid[7] = false;
        } else if (c == map_.cols - 1) {
            valid[1] = valid[2] = valid[3] = false;
        }
        for (int k = 0; k < 8; ++k) {
            if (!valid[k]) continue;
            const Tile n{r + kDRow[k], c + kDCol[k]};
            const uint32_t new_g = cost(cur, n) + (C(cur) & 0x3fffu);
            uint32_t nc = C(n);
            if ((nc & 0xc0000000u) != 0u && (nc & 0x3fffu) <= new_g) continue;
            nc &= 0x7fffffffu;
            nc = (nc & ~0x3fffu) | (new_g & 0x3fffu);
            nc = (nc & 0xf8003fffu) | ((cheb16(n, map_.goal) & 0x1fffu) << 14);
            nc = (nc & 0xc7ffffffu) | ((static_cast<uint32_t>(dir_between(n, cur)) & 7u) << 27);
            const bool opened = (nc & 0x40000000u) != 0u;
            C(n) = nc;
            if (!opened) {
                C(n) = nc | 0x40000000u;
                push(n);
            }
        }
    }

    std::vector<Tile> build(Tile goal) {
        std::vector<Tile> path{goal};
        Tile t = goal;
        while (t != map_.start) {
            const int d = static_cast<int>((C(t) >> 27) & 7u);
            t = {t.first + kDRow[d], t.second + kDCol[d]};
            path.push_back(t);
            if (path.size() > cell_.size() + 1) throw std::runtime_error("the parent chain does not end at the start");
        }
        return {path.rbegin(), path.rend()};
    }

    const AStarMap& map_;
    std::vector<uint32_t> cell_;
    std::vector<Tile> heap_;
};

// Deterministic random maps: 12..40 rows and columns, 0..40 % walls, uniform / mixed / costly terrain mixes,
// start and goal mostly on open tiles, and the three slice budgets of the search.
std::vector<AStarMap> make_astar_maps(uint32_t seed, int n) {
    std::mt19937 rng(seed);
    const auto pick = [&rng](uint32_t count) { return static_cast<size_t>(rng() % count); };
    static const int kSizes[5] = {12, 20, 30, 31, 40};
    static const uint32_t kWallPermille[6] = {0, 0, 100, 200, 300, 400};
    static const uint16_t kBudgets[4] = {1000, 1000, 37, 5};
    std::vector<AStarMap> maps;
    for (int s = 0; s < n; ++s) {
        AStarMap m;
        m.rows = kSizes[pick(5)];
        m.cols = kSizes[pick(5)];
        const uint32_t walls = kWallPermille[pick(6)];
        const size_t mode = pick(4);  // 0 grass, 1 and 2 mixed, 3 costly
        for (int r = 0; r < m.rows; ++r) {
            std::string row;
            for (int c = 0; c < m.cols; ++c) {
                if (rng() % 1000 < walls) {
                    row += '5';
                } else if (mode == 0) {
                    row += '0';
                } else if (mode == 3) {
                    row += "0000033344"[pick(10)];
                } else {
                    row += "0134"[pick(4)];
                }
            }
            m.cls.push_back(row);
        }
        const auto open_tile = [&]() {
            for (;;) {
                const Tile t{static_cast<int>(rng() % static_cast<uint32_t>(m.rows)), static_cast<int>(rng() % static_cast<uint32_t>(m.cols))};
                if (m.cls[static_cast<size_t>(t.first)][static_cast<size_t>(t.second)] != '5') return t;
            }
        };
        m.start = open_tile();
        m.goal = open_tile();
        if (rng() % 100 >= 95) {  // now and then any tile, walls included
            m.goal = {static_cast<int>(rng() % static_cast<uint32_t>(m.rows)), static_cast<int>(rng() % static_cast<uint32_t>(m.cols))};
        }
        m.budget = kBudgets[pick(4)];
        maps.push_back(std::move(m));
    }
    return maps;
}

// ---------------------------------------------------------------------------------------------------------------
// Walk model
// ---------------------------------------------------------------------------------------------------------------

struct Step {
    uint32_t time;
    int32_t x;
    int32_t y;
    bool operator==(const Step& o) const { return time == o.time && x == o.x && y == o.y; }
    bool operator!=(const Step& o) const { return !(*this == o); }
};

constexpr int kWalkSide = 30;
using TerrainGrid = std::array<std::array<uint8_t, kWalkSide>, kWalkSide>;  // [row][column] terrain class

struct WalkScenario {
    int type{0};        // ant type 0..5 (5 = swimmer)
    int facing{0};      // 0..7
    bool carrying{false};
    Tile start{0, 0};
    Tile goal{0, 0};
    TerrainGrid terrain{};
    std::vector<Tile> path;   // (row, column), as delivered
    uint32_t delivered{0};    // time of the delivery
};

// The rules of the walk model that the self-check of suite 2 breaks one at a time (the defaults are the original's).
struct WalkRules {
    int snap{2};               // arrival: the landing point is snapped to the tile centre when it is within this many pixels
    bool double_book{true};    // a clip started inside a step callback books its first frame twice
    bool nudge{true};          // a step into a tile that is not the next waypoint is pushed one pixel further
};

// The positions the original's walk produces after the path was delivered at time T0 (idle restarted at T0).
std::vector<Step> walk_model(const Original& o, const WalkScenario& s, const WalkRules& rules = WalkRules()) {
    const auto terrain_at = [&](Tile t) { return static_cast<int>(s.terrain.at(static_cast<size_t>(t.first)).at(static_cast<size_t>(t.second))); };
    const auto centre_x = [](Tile t) { return t.second * 32 + 16; };
    const auto centre_y = [](Tile t) { return t.first * 32 + 16; };
    const std::vector<Tile>& path = s.path;
    std::vector<Step> ev;
    if (path.empty()) return ev;

    int x = centre_x(path[0]);
    int y = centre_y(path[0]);
    Tile tile5a = path[0];
    const std::vector<Frame> idle = o.idle_frames(s.type, s.facing, s.carrying, terrain_at(path[0]));
    // The idle clip was restarted at the delivery (outside a callback): its start step runs at T0 and the first
    // animation step is due when frame 0 has lasted.
    uint32_t nxt = s.delivered + idle[0].dur;
    if (idle.size() <= 1) return ev;  // a one-frame idle clip never steps
    uint32_t now = nxt;

    enum class State { Idle, Walk } state = State::Idle;
    enum class Result { Done, Restart, Continue };
    std::vector<Frame> frames;
    int cur_dir = s.facing;
    int clip_dir = -1;  // the direction of the playing walk clip (no clip yet)
    size_t idx = 0;
    // ARRIVE at a waypoint: the next path step is taken; the clip restarts when it is a new direction (SetAction
    // with the restart flag 1 only restarts on a change of direction) or when the ant stood idle.
    const auto arrive = [&](Tile cur) -> Result {
        ++idx;
        if (idx >= path.size()) return Result::Done;
        const int d = dir_between(cur, path[idx]);
        if (state == State::Idle || clip_dir < 0 || d != clip_dir) {
            frames = o.walk_frames(s.type, terrain_at(cur), d, s.carrying);
            clip_dir = d;
            cur_dir = d;
            return Result::Restart;
        }
        return Result::Continue;
    };

    // The first step of the idle clip is the ARRIVE at the start tile.
    if (arrive(tile5a) == Result::Done) return ev;  // a path of one tile: StopAt, idle again, no movement
    state = State::Walk;
    size_t cursor = 0;
    nxt = now + frames[0].dur + (rules.double_book ? frames[0].dur : 0u);  // the walk clip is played from inside the step callback: frame 0 is booked twice

    for (int steps = 0; steps < 400; ++steps) {
        now = nxt;
        const Frame fr = frames.at(cursor);
        int dx = fr.dx;
        int dy = fr.dy;
        const size_t cursor_next = (cursor == frames.size() - 1) ? 0 : cursor + 1;
        int nx = x + dx;
        int ny = y + dy;
        Tile nt{ny / 32, nx / 32};  // idiv: truncation, like the original
        const bool new_tile = nt != tile5a;
        const Tile waypoint = path.at(idx);
        if (!new_tile && nt != waypoint) {  // still inside the tile, which is not the next waypoint: move and go on
            x = nx;
            y = ny;
            ev.push_back({now, x, y});
            cursor = cursor_next;
            nxt += frames.at(cursor).dur;
            continue;
        }
        if (rules.nudge && new_tile && nt != waypoint) {  // the nudge: one pixel further into the tile
            const int sx = dx > 0 ? 1 : -1;
            const int sy = dy > 0 ? 1 : -1;
            dx += sx;
            nx += sx;
            dy += sy;
            ny += sy;
            nt = {ny / 32, nx / 32};
        }
        const int cx = centre_x(nt);
        const int cy = centre_y(nt);
        if (std::abs(nx - cx) <= rules.snap && std::abs(ny - cy) <= rules.snap) {  // arrival: snap to the tile centre
            dx = cx - x;
            dy = cy - y;
            const Result res = arrive(nt);
            if (res == Result::Done) {  // PathComplete -> StopSync: snap to the centre of the pixel tile (the position before this step)
                const Tile pixel_tile{y / 32, x / 32};
                x = centre_x(pixel_tile);
                y = centre_y(pixel_tile);
                ev.push_back({now, x, y});
                return ev;
            }
            x += dx;
            y += dy;
            tile5a = nt;
            ev.push_back({now, x, y});
            if (res == Result::Restart) {
                cursor = 0;
                nxt = now + frames[0].dur + (rules.double_book ? frames[0].dur : 0u);  // the nested start step books frame 0, the outer step again
            } else {
                cursor = cursor_next;
                nxt += frames.at(cursor).dur;
            }
            continue;
        }
        const Tile old_tile = tile5a;  // no arrival: the (nudged) displacement is applied
        x += dx;
        y += dy;
        tile5a = {y / 32, x / 32};
        ev.push_back({now, x, y});
        if (new_tile && terrain_at(old_tile) != terrain_at(nt)) {  // SetAction(walk) restarts when the terrain class changed
            frames = o.walk_frames(s.type, terrain_at(nt), cur_dir, s.carrying);
            cursor = 0;
            nxt = now + frames[0].dur + (rules.double_book ? frames[0].dur : 0u);
            continue;
        }
        cursor = cursor_next;
        nxt += frames.at(cursor).dur;
    }
    return ev;
}

// Runs the remake for one scenario: the same set-up as the original's walk (a 30 x 30 world, one ant, a move order
// one tick before the path is delivered, 400 ticks) and collects what the model compares.
struct RemakeWalk {
    WalkScenario scenario;
    std::vector<Step> steps;
};

std::vector<RemakeWalk> run_remake_walks(uint32_t seed, int n) {
    std::mt19937 rng(seed);
    std::vector<RemakeWalk> out;
    for (int s = 0; s < n; ++s) {
        WalkScenario sc;
        sc.type = static_cast<int>(rng() % 6);
        static const int kTerrains[4] = {0, 1, 3, 4};
        const int terrain = (sc.type == 5) ? static_cast<int>(rng() % 2 == 0 ? 2 : 0) : kTerrains[rng() % 4];
        sc.carrying = (sc.type == 5 && terrain == 2) ? false : (rng() % 2 != 0);
        sc.facing = static_cast<int>(rng() % 8);
        const int sx = 10 + static_cast<int>(rng() % 3);
        const int sy = 10 + static_cast<int>(rng() % 3);
        const int gx = sx + static_cast<int>(rng() % 9) - 4;
        const int gy = sy + static_cast<int>(rng() % 9) - 4;
        sc.start = {sy, sx};
        sc.goal = {gy, gx};
        // classes 0, 1, 3, 4 chosen per 2 x 2 block for ants that walk on land; swimmers get the uniform terrain
        static const int kClasses[4] = {0, 1, 3, 4};
        int block[16][16];
        for (int by = 0; by < 16; ++by) {
            for (int bx = 0; bx < 16; ++bx) block[by][bx] = kClasses[rng() % 4];
        }

        SimulationEngine sim;
        sim.init_test_world(kWalkSide, kWalkSide, 7, 600000);
        for (int y = 0; y < kWalkSide; ++y) {
            for (int x = 0; x < kWalkSide; ++x) {
                const int c = (sc.type == 5) ? terrain : block[y / 2][x / 2];
                sim.grid_mut().set_terrain_class(x, y, static_cast<uint8_t>(c));
                sc.terrain[static_cast<size_t>(y)][static_cast<size_t>(x)] = static_cast<uint8_t>(c);
            }
        }
        const uint32_t id = sim.spawn_unit(0, static_cast<AntType>(sc.type), TileCoord{sx, sy});
        AntUnit& a = sim.get_unit(id);
        a.facing = static_cast<Direction>(sc.facing);
        if (sc.carrying) a.pick_up_food(1, 25);
        sim.set_locomotion_trace_enabled(true);
        sim.issue_move_order(id, TileCoord{gx, gy});
        sim.tick();
        for (const TileCoord& t : a.waypoints) sc.path.push_back({t.y, t.x});
        for (int t = 0; t < 400; ++t) sim.tick();

        RemakeWalk rw;
        bool delivered = false;
        int lx = sx * 32 + 16;
        int ly = sy * 32 + 16;
        for (const LocoTraceEvent& ev : sim.locomotion_trace()) {
            if (ev.ant_id != id) continue;
            if (ev.kind == LocoTraceEvent::Kind::PathDelivered && !delivered) {
                delivered = true;
                sc.delivered = ev.time_ms;
            } else if (ev.kind == LocoTraceEvent::Kind::Step && (ev.px != lx || ev.py != ly)) {
                rw.steps.push_back({ev.time_ms, ev.px, ev.py});
                lx = ev.px;
                ly = ev.py;
            }
        }
        rw.scenario = std::move(sc);
        out.push_back(std::move(rw));
    }
    return out;
}

std::string describe(const WalkScenario& s) {
    std::ostringstream os;
    os << "type " << s.type << " facing " << s.facing << (s.carrying ? " carrying" : "") << " from (" << s.start.first << "," << s.start.second << ") to ("
       << s.goal.first << "," << s.goal.second << ") path of " << s.path.size() << " tiles";
    return os.str();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Suites
// ---------------------------------------------------------------------------------------------------------------

static void suite_astar() {
    TEST_SUITE("Suite 1: PathSearch against the independent model of PathRequest::Step");

    TEST_CASE("1.1 1500 random maps: the remake's path equals the model's path, tile for tile") {
        const std::vector<AStarMap> maps = make_astar_maps(777u, 1500);
        int mismatches = 0;
        int with_path = 0;
        int without_path = 0;
        int multi_slice = 0;
        size_t tiles = 0;
        for (size_t i = 0; i < maps.size(); ++i) {
            const AStarMap& m = maps[i];
            AStarModel model(m);
            const std::vector<Tile> expected = model.run();

            static const uint32_t kW[6] = {20, 16, 8000, 48, 24, 8000};
            const auto cost = [&](TileCoord from, TileCoord to) -> uint32_t {
                const uint32_t ca = kW[m.cls[static_cast<size_t>(from.y)][static_cast<size_t>(from.x)] - '0'];
                const uint32_t cb = kW[m.cls[static_cast<size_t>(to.y)][static_cast<size_t>(to.x)] - '0'];
                if (ca == 8000u || cb == 8000u) return 8000u;
                uint32_t s = ca + cb;
                if (from.x != to.x && from.y != to.y) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
                return s >> 1;
            };
            PathGridPool pool;
            PathSearch search(1, TileCoord{m.start.second, m.start.first}, TileCoord{m.goal.second, m.goal.first}, static_cast<uint16_t>(m.rows),
                              static_cast<uint16_t>(m.cols), pool);
            int slices = 0;
            while (!search.finished() && slices < 100000) {
                search.step(m.budget, cost);
                ++slices;
            }
            if (slices > 1) ++multi_slice;

            std::vector<Tile> actual;
            for (const TileCoord& t : search.path()) actual.push_back({t.y, t.x});
            if (expected.empty()) {
                ++without_path;
            } else {
                ++with_path;
                tiles += expected.size();
            }
            if (actual != expected) {
                if (++mismatches <= 3) {
                    std::cout << "\n    map " << i << " (" << m.rows << "x" << m.cols << ", budget " << m.budget << ") start (" << m.start.first << ","
                              << m.start.second << ") goal (" << m.goal.first << "," << m.goal.second << "): model " << expected.size() << " tiles, remake "
                              << actual.size() << " tiles";
                    for (size_t k = 0; k < expected.size() && k < actual.size(); ++k) {
                        if (expected[k] != actual[k]) {
                            std::cout << ", first difference at step " << k << ": model (" << expected[k].first << "," << expected[k].second << ") remake ("
                                      << actual[k].first << "," << actual[k].second << ")";
                            break;
                        }
                    }
                    std::cout << "\n    ";
                }
            }
        }
        std::cout << "[" << with_path << " paths, " << tiles << " tiles, " << without_path << " without a path, " << multi_slice << " multi-slice] ";
        ASSERT_EQ(mismatches, 0);
        // the scenarios must be varied enough to mean something
        ASSERT_TRUE(with_path > 1000);
        ASSERT_TRUE(without_path >= 10);
        ASSERT_TRUE(multi_slice > 100);
        ASSERT_TRUE(tiles > 15000);
    } TEST_END();
}

static void suite_walk(const Original& original) {
    TEST_SUITE("Suite 2: the walk of a delivered path against the independent model of the original locomotion");

    TEST_CASE("2.1 1000 random walks: every position change and its time equal the model's (6 ant types, 4 terrains, 8 facings, food)") {
        const std::vector<RemakeWalk> walks = run_remake_walks(12345u, 1000);
        int mismatches = 0;
        int compared = 0;
        size_t events = 0;
        int by_type[6] = {0, 0, 0, 0, 0, 0};
        for (size_t i = 0; i < walks.size(); ++i) {
            const RemakeWalk& w = walks[i];
            if (w.scenario.path.empty()) continue;  // a move order onto the ant's own tile delivers no path
            ++compared;
            ++by_type[w.scenario.type];
            events += w.steps.size();
            ASSERT_EQ(w.scenario.delivered, 50u);  // the nominal PATHMGR period: the model's time base
            const std::vector<Step> expected = walk_model(original, w.scenario);
            if (expected != w.steps) {
                if (++mismatches <= 3) {
                    std::cout << "\n    walk " << i << " (" << describe(w.scenario) << "): model " << expected.size() << " position changes, remake "
                              << w.steps.size();
                    for (size_t k = 0; k < expected.size() || k < w.steps.size(); ++k) {
                        const bool has_m = k < expected.size();
                        const bool has_r = k < w.steps.size();
                        if (!has_m || !has_r || expected[k] != w.steps[k]) {
                            std::cout << ", first difference at change " << k << ": model ";
                            if (has_m) std::cout << expected[k].time << " ms (" << expected[k].x << "," << expected[k].y << ")"; else std::cout << "none";
                            std::cout << " remake ";
                            if (has_r) std::cout << w.steps[k].time << " ms (" << w.steps[k].x << "," << w.steps[k].y << ")"; else std::cout << "none";
                            break;
                        }
                    }
                    std::cout << "\n    ";
                }
            }
        }
        std::cout << "[" << compared << " walks, " << events << " position changes] ";
        ASSERT_EQ(mismatches, 0);
        ASSERT_TRUE(compared > 900);
        ASSERT_TRUE(events > 20000);
        for (int t = 0; t < 6; ++t) ASSERT_TRUE(by_type[t] > 100);  // every ant type is exercised
    } TEST_END();

    TEST_CASE("2.2 The comparison is live: breaking one rule of the model at a time is detected (every rule is exercised)") {
        // Guards against a comparison that can never fail and against a scenario set that never reaches a rule:
        // the same scenarios against a model with one rule broken must differ from the remake.
        const std::vector<RemakeWalk> walks = run_remake_walks(12345u, 300);
        const auto differing = [&](const WalkRules& rules) {
            int n = 0;
            for (const RemakeWalk& w : walks) {
                if (w.scenario.path.empty()) continue;
                try {
                    if (walk_model(original, w.scenario, rules) != w.steps) ++n;
                } catch (const std::exception&) {
                    ++n;  // a broken rule can walk the model off the map: that is a difference too
                }
            }
            return n;
        };
        ASSERT_EQ(differing(WalkRules()), 0);
        WalkRules no_double_booking;
        no_double_booking.double_book = false;
        WalkRules no_nudge;
        no_nudge.nudge = false;
        WalkRules tight_snap;
        tight_snap.snap = 1;
        WalkRules wide_snap;
        wide_snap.snap = 3;
        const int d1 = differing(no_double_booking);
        const int d2 = differing(no_nudge);
        const int d3 = differing(tight_snap);
        const int d4 = differing(wide_snap);
        std::cout << "[differing walks: no double booking " << d1 << ", no nudge " << d2 << ", snap 1 " << d3 << ", snap 3 " << d4 << "] ";
        ASSERT_TRUE(d1 > 100);
        ASSERT_TRUE(d2 >= 5);
        ASSERT_TRUE(d3 >= 5);
        ASSERT_TRUE(d4 >= 5);
    } TEST_END();
}

int main() {
    std::cout << "=======================================================\n"
              << " ANTS - ORIGINAL MOVEMENT DIFFERENTIAL TEST SUITE\n"
              << "=======================================================\n";

    const std::string assets_dir = locate_assets_dir();
    const std::string chd_path = assets_dir + "/ants.chd";
    const std::string exe_path = assets_dir + "/Ants.exe";  // optional: a local copy of the original program
    std::cout << "Assets directory: " << assets_dir << "\n";
    AssetArchive archive;
    if (!archive.load_from_file(chd_path)) {
        std::cerr << "ERROR: ants.chd could not be loaded from " << assets_dir << "\n";
        return 1;
    }
    // The tables the models read: from Ants.exe when the program is there, else from the remake's tables.
    const ProgramBytes exe = ProgramBytes::open(exe_path, kModelRegions);
    std::cout << "Bytes of the original program: " << exe.summary() << "\n";

    TEST_SUITE("Suite 0: the bytes of Ants.exe that the models read");
    TEST_CASE("0.1 The tables the models read equal the pinned SHA-256 digests of Ants.exe (layout, every region, the hash's known answers)") {
        ASSERT_TRUE(exe.loaded());
        for (const std::string& failure : exe.failures()) std::cout << "\n    " << failure << "\n    ";
        g_assert_count += static_cast<int>(exe.checks());  // one check per known-answer set, layout field and region
        ASSERT_EQ(exe.failures().size(), size_t{0});
        ASSERT_EQ(exe.unavailable().size(), size_t{0});  // the remake holds every table the models read
    } TEST_END();
    if (!exe.verified()) {
        // The models must read the original's bytes and nothing else: a different program, or remake tables that are not
        // the original's, would make the comparison meaningless.
        std::cout << "\n >>> the bytes are not the original's (see 0.1): the model suites are not run <<< \n\n";
        return 1;
    }
    const Original original(exe, archive);

    suite_astar();
    suite_walk(original);

    std::cout << "\n=======================================================\n"
              << " MOVEMENT DIFFERENTIAL TEST SUMMARY\n"
              << "=======================================================\n"
              << "  Total Test Cases: " << g_test_count << "\n"
              << "  Total Assertions: " << g_assert_count << "\n"
              << "  Failed Tests:     " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL MOVEMENT DIFFERENTIAL TESTS PASSED CLEANLY <<< \n\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED! <<< \n\n";
    return 1;
}
