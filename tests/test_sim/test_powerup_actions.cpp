// Golden tests of the power-ups of the original game (Ants.exe): the pick-up is the arrival of a move or power-up order on
// the power-up's tile (FUN_0101ccaf cases 1 and 4 -> message 9 -> FUN_01020cdb, synchronous), plays the getpow clip as
// action 4 and changes the ant's type and nothing else; an order given between the crossing into the power-up's tile and
// the landing on its centre cancels the pick-up (the ant stands on the power-up); a power-up is a solid object, so an ant
// standing on it cannot be reached, attacked or thrown onto (docs/GAME_REVERSE_ENGINEERING.md 5.38).
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/movement_tables.hpp"

#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(92) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

namespace {

// Index of the flower dropper whose stem stands in column x (the order of the droppers in the snapshot is the map's object order)
size_t dropper_at_column(const SimulationEngine& sim, int32_t x) {
    const auto& fds = sim.get_world_state().flower_droppers;
    for (size_t i = 0; i < fds.size(); ++i) {
        if (fds[i].x == x) return i;
    }
    return fds.size();
}

// A shipped map as bytes, and where the tail of its file lies (the loader's order: header, dictionary, dimensions, two layers, block 1, block 2, block 3, block 4,
// the final word); synthetic maps of the loader tests are made by editing these bytes
std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const std::streamsize size = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}
uint16_t word_at(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); }
uint32_t dword_at(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint32_t>(word_at(d, p)) | (static_cast<uint32_t>(word_at(d, p + 2)) << 16); }

struct MapTail {
    bool ok{false};
    size_t b1_count{0};          // block 1: a word count, then (tile, row, column) words
    size_t b1_records{0};
    size_t b4_count{0};          // block 4: a word count, then the waypoints
    size_t final_word{0};
};

MapTail find_tail(const std::vector<uint8_t>& d) {
    MapTail t;
    if (d.size() < 44) return t;
    size_t p = 42 + (static_cast<size_t>(word_at(d, 40)) + 1) * 11;
    const size_t rows = dword_at(d, p);
    const size_t columns = dword_at(d, p + 4);
    p += 8 + rows * columns * 12;
    t.b1_count = p;
    t.b1_records = p + 2;
    p += 2 + static_cast<size_t>(word_at(d, p)) * 6;
    const size_t objects = word_at(d, p);
    p += 2;
    for (size_t i = 0; i < objects; ++i) p += 10 + static_cast<size_t>(word_at(d, p + 8)) * 4;
    p += 4;
    t.b4_count = p;
    const size_t waypoints = word_at(d, p);
    p += 2;
    for (size_t i = 0; i < waypoints; ++i) p += 8 + (dword_at(d, p + 4) != 0 ? 44 : 0);
    t.final_word = p;
    t.ok = p + 2 <= d.size();
    return t;
}

constexpr int kTickMs = 50;
constexpr uint16_t kNewsCantGoThere = 0x3A;   // status 58 "Can't go there."

void make_world(SimulationEngine& sim, uint32_t seed = 1) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

// A one-tile island at (30, 30) inside a ring of water: an order to it fails in the path manager ("Can't go there.").
void make_island(SimulationEngine& sim) {
    for (int y = 29; y <= 31; ++y) {
        for (int x = 29; x <= 31; ++x) {
            if (x != 30 || y != 30) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).terrain_type = TERRAIN_WATER;
        }
    }
}

void set_surface(SimulationEngine& sim, TileCoord t, SurfaceType s) {
    sim.grid_mut().get_cell_mut(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y)).surface_type = s;
}

void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / kTickMs; ++t) sim.tick();
}

// Ticks until `pred` holds or `max_ms` passed; returns the elapsed time in ms (-1 = never).
int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / kTickMs; ++t) {
        if (pred()) return t * kTickMs;
        sim.tick();
    }
    return -1;
}

AntOrder order(uint32_t ant, OrderType type, TileCoord target) {
    AntOrder o{};
    o.ant_id = ant;
    o.type = type;
    o.target_x = target.x;
    o.target_y = target.y;
    return o;
}

TileCoord tile_of(const AntUnit& a) { return TileCoord{a.pixel_x / 32, a.pixel_y / 32}; }

// Audio events of the tick that just ran, counted per sound id.
int count_sound(std::vector<AudioEvent>& ev, uint32_t id) {
    int n = 0;
    for (const auto& e : ev) if (e.sound_id == id) ++n;
    return n;
}

// Puts a worker on A, a power-up on B (adjacent, in direction `dir` from A) and gives the player move order at t = 0.
struct Walk {
    SimulationEngine sim;
    uint32_t ant{0};
    TileCoord a{10, 10};
    TileCoord b{11, 10};
    int now{0};                  // ms since the order
    void tick() { sim.tick(); now += kTickMs; }
    void run_to(int ms) { while (now < ms) tick(); }
    const AntUnit& u() const { return const_cast<Walk*>(this)->sim.get_unit(ant); }
};

void start_walk(Walk& w, TileCoord a, TileCoord b, uint8_t hat = 4, AntType type = AntType::Worker) {
    make_world(w.sim);
    w.a = a;
    w.b = b;
    w.sim.grid_mut().place_powerup(b.x, b.y, hat);
    w.ant = w.sim.spawn_unit(0, type, a);
    w.sim.issue_move_order(w.ant, b);
}

} // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Ants power-ups: pick-up, cancel window, immunity\n"
              << "=======================================================\n";

    TEST_CASE("1.1 Pick-up: crossing into the tile at 450 ms, the walk ends at 650 ms and the type changes in that very tick (no dwell)") {
        Walk w;
        start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
        w.sim.get_unit(w.ant).hp = 6;
        w.run_to(400);
        ASSERT_EQ(w.u().pixel_x, 348);
        ASSERT_EQ(tile_of(w.u()), (TileCoord{10, 10}));
        w.run_to(450);                                                       // the walk frame that crosses the tile edge
        ASSERT_EQ(w.u().pixel_x, 352);
        ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        ASSERT_EQ(w.u().type, AntType::Worker);
        w.run_to(600);
        ASSERT_EQ(w.u().pixel_x, 364);
        ASSERT_EQ(w.u().type, AntType::Worker);
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        w.sim.clear_audio_events();
        w.tick();                                                            // t = 650: the arrival frame
        ASSERT_EQ(w.u().type, AntType::Combat);
        ASSERT_FALSE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);
        ASSERT_EQ(w.u().state, UnitState::PoweringUp);
        ASSERT_EQ(w.u().hp, 6u);                                             // no heal
        ASSERT_EQ(w.u().max_hp, 10u);                                        // 10 for every type
        ASSERT_TRUE(w.u().waypoints.empty());
        ASSERT_EQ(w.u().pixel_x, 372);                                       // tile centre + the arrival snap delta
        ASSERT_EQ(w.u().pixel_y, 336);
        auto ev = w.sim.poll_audio_events();
        ASSERT_EQ(count_sound(ev, 1), 1);                                    // powerupc.wav exactly once (once flag)
        ASSERT_EQ(count_sound(ev, 2), 0);
        int chimes = 0;
        for (int ms = 700; ms <= 1500; ms += kTickMs) {
            w.tick();
            auto e2 = w.sim.poll_audio_events();
            chimes += count_sound(e2, 2);
            if (ms < 1150) ASSERT_EQ(count_sound(e2, 2), 0);
            if (ms == 1150) ASSERT_EQ(count_sound(e2, 2), 1);                // 490 ms after the snap
            if (ms < 1500) ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);
        }
        ASSERT_EQ(chimes, 1);
        ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);                  // 840 ms after the snap: idle as the new type
        ASSERT_EQ(w.u().state, UnitState::GuardIdle);
        ASSERT_EQ(w.u().orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(w.u().pixel_x, 372);                                       // no later snap
    } TEST_END();

    TEST_CASE("1.2 Pick-up window per direction and terrain: crossing and arrival ticks, overshoot after the pick-up (audit matrix)") {
        struct Row { const char* name; TileCoord a, b; SurfaceType sa, sb; int cross_ms, arrive_ms, dx, dy; };
        // ms after the walk restart at 200 ms, from the walk clips of ants.chd (crossing frame, arrival frame)
        const Row rows[] = {
            {"grass E", {10, 10}, {11, 10}, SurfaceType::Grass, SurfaceType::Grass, 250, 450, 4, 0},
            {"grass W", {11, 10}, {10, 10}, SurfaceType::Grass, SurfaceType::Grass, 300, 450, -4, 0},
            {"grass S", {10, 10}, {10, 11}, SurfaceType::Grass, SurfaceType::Grass, 250, 450, 0, 4},
            {"grass N", {10, 11}, {10, 10}, SurfaceType::Grass, SurfaceType::Grass, 300, 450, 0, -4},
            {"grass SE", {10, 10}, {11, 11}, SurfaceType::Grass, SurfaceType::Grass, 350, 550, 5, 5},
            {"grass NW", {11, 11}, {10, 10}, SurfaceType::Grass, SurfaceType::Grass, 350, 550, -5, -5},
            {"sand E", {10, 10}, {11, 10}, SurfaceType::Slate, SurfaceType::Slate, 200, 360, 4, 0},
            {"sand W", {11, 10}, {10, 10}, SurfaceType::Slate, SurfaceType::Slate, 240, 360, -4, 0},
            {"dirt E", {10, 10}, {11, 10}, SurfaceType::Gravel, SurfaceType::Gravel, 300, 540, 4, 0},
            {"dirt W", {11, 10}, {10, 10}, SurfaceType::Gravel, SurfaceType::Gravel, 360, 540, -4, 0},
            {"mud E", {10, 10}, {11, 10}, SurfaceType::Mud, SurfaceType::Mud, 540, 960, 4, 0},
            {"grass to dirt E", {10, 10}, {11, 10}, SurfaceType::Grass, SurfaceType::Gravel, 250, 550, 4, 0},
            {"dirt to grass E", {10, 10}, {11, 10}, SurfaceType::Gravel, SurfaceType::Grass, 300, 550, 4, 0},
            {"dirt to grass W", {11, 10}, {10, 10}, SurfaceType::Gravel, SurfaceType::Grass, 360, 560, -4, 0},
            {"sand to grass E", {10, 10}, {11, 10}, SurfaceType::Slate, SurfaceType::Grass, 200, 450, 4, 0},
        };
        for (const Row& r : rows) {
            Walk w;
            make_world(w.sim);
            set_surface(w.sim, r.a, r.sa);
            set_surface(w.sim, r.b, r.sb);
            w.sim.grid_mut().place_powerup(r.b.x, r.b.y, 4);
            w.ant = w.sim.spawn_unit(0, AntType::Worker, r.a);
            w.sim.issue_move_order(w.ant, r.b);
            auto up = [](int ms) { return (ms + kTickMs - 1) / kTickMs * kTickMs; };
            const int cross_tick = up(200 + r.cross_ms);
            const int arrive_tick = up(200 + r.arrive_ms);
            w.run_to(cross_tick - kTickMs);
            if (w.u().type != AntType::Worker || tile_of(w.u()) == r.b) {
                std::cout << "FAILED!\n    " << r.name << ": crossed before " << cross_tick << " ms\n";
                ++g_test_failures;
                return;
            }
            w.tick();
            if (tile_of(w.u()) != r.b || w.u().type != AntType::Worker) {
                std::cout << "FAILED!\n    " << r.name << ": not on the power-up tile at " << cross_tick << " ms\n";
                ++g_test_failures;
                return;
            }
            w.run_to(arrive_tick - kTickMs);
            if (w.u().type != AntType::Worker || !w.sim.grid().has_powerup_at(r.b)) {
                std::cout << "FAILED!\n    " << r.name << ": picked up before " << arrive_tick << " ms\n";
                ++g_test_failures;
                return;
            }
            w.tick();
            const int cx = r.b.x * 32 + 16 + r.dx;
            const int cy = r.b.y * 32 + 16 + r.dy;
            if (w.u().type != AntType::Combat || w.sim.grid().has_powerup_at(r.b) || w.u().pixel_x != cx || w.u().pixel_y != cy) {
                std::cout << "FAILED!\n    " << r.name << ": arrival at " << arrive_tick << " ms: type " << static_cast<int>(w.u().type)
                          << " px (" << w.u().pixel_x << "," << w.u().pixel_y << ") expected (" << cx << "," << cy << ")\n";
                ++g_test_failures;
                return;
            }
            ++g_assert_count;
        }
    } TEST_END();

    TEST_CASE("1.3 Cancel window: an order between the crossing (450) and the landing (650) snaps the ant onto the power-up tile and cancels the pick-up") {
        for (int cancel_ms : {450, 500, 550, 600}) {
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            make_island(w.sim);
            w.run_to(cancel_ms);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});               // "Can't go there." once the path manager has searched
            ASSERT_EQ(w.u().pixel_x, 368);                                  // the centre of the power-up tile (no overshoot)
            ASSERT_EQ(w.u().pixel_y, 336);
            ASSERT_TRUE(w.u().waypoints.empty());
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
            w.sim.clear_audio_events();
            ASSERT_TRUE(wait_ms(w.sim, 800, [&]() { return w.u().loco_action == AntUnit::kActionCantGo; }) >= 0);
            ASSERT_TRUE(w.sim.has_news_event(0, kNewsCantGoThere));
            ASSERT_TRUE(w.sim.has_audio_event(63));                         // the can't clip's cue
            run_ms(w.sim, 3000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_EQ(w.u().hp, 10u);
            ASSERT_EQ(w.u().pixel_x, 368);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));    // the ant stands on an intact power-up
            ASSERT_EQ(w.sim.grid().get_powerup_type(TileCoord{11, 10}), 4);
        }
    } TEST_END();

    TEST_CASE("1.4 Cancel window edges: before the crossing the ant is snapped back to the previous tile, after the landing the order is refused") {
        {   // 400 ms: still on tile A (pixel 348): the snap puts it on the centre of A, the power-up is not reached
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            make_island(w.sim);
            w.run_to(400);
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});
            ASSERT_EQ(w.u().pixel_x, 336);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{10, 10}));
            run_ms(w.sim, 3000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{10, 10}));
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        }
        {   // 650 ms: the pick-up has happened and orders are refused for the whole getpow clip
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            make_island(w.sim);
            w.run_to(650);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});
            w.sim.issue_move_order(w.ant, TileCoord{20, 20});
            w.sim.issue_order(order(w.ant, OrderType::Move, TileCoord{20, 20}));
            ASSERT_EQ(w.u().pixel_x, 372);                                  // no snap
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);
            ASSERT_FALSE(w.sim.has_pending_path(w.ant));
            w.run_to(1450);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);           // refused until 840 ms after the snap
            ASSERT_EQ(w.u().type, AntType::Combat);
            ASSERT_FALSE(w.sim.has_news_event(0, kNewsCantGoThere));
            run_ms(w.sim, 100);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
        }
    } TEST_END();

    TEST_CASE("1.5 Cancel window to the west: crossing at 500 ms, landing at 650 ms (window 150 ms); a valid order to another tile leaves without picking up") {
        for (int cancel_ms : {500, 550, 600}) {
            Walk w;
            start_walk(w, TileCoord{11, 10}, TileCoord{10, 10});
            make_island(w.sim);
            w.run_to(cancel_ms);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{10, 10}));
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});
            ASSERT_EQ(w.u().pixel_x, 336);                                  // the centre of the power-up tile (10, 10)
            ASSERT_TRUE(wait_ms(w.sim, 800, [&]() { return w.u().loco_action == AntUnit::kActionCantGo; }) >= 0);
        }
        {   // 450 ms: still on tile A = (11, 10) (pixel 372 - 4 = 368+..): snapped back to A
            Walk w;
            start_walk(w, TileCoord{11, 10}, TileCoord{10, 10});
            make_island(w.sim);
            w.run_to(450);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});
            ASSERT_EQ(w.u().pixel_x, 368);
        }
        {   // a valid order to a third tile: the ant walks away, the power-up stays
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            w.run_to(500);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            w.sim.issue_move_order(w.ant, TileCoord{11, 14});
            ASSERT_EQ(w.u().pixel_x, 368);
            ASSERT_TRUE(wait_ms(w.sim, 6000, [&]() { return tile_of(w.u()) == TileCoord{11, 14} && w.u().waypoints.empty(); }) >= 0);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        }
    } TEST_END();

    TEST_CASE("1.6 The silent can't-go: a goal with no valid tile within four rings stops the ant on the spot without text, sound or clip") {
        Walk w;
        start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
        for (int y = 24; y <= 36; ++y) {                                    // a lake: every tile within 4 rings of (30, 30) is water
            for (int x = 24; x <= 36; ++x) w.sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).terrain_type = TERRAIN_WATER;
        }
        w.run_to(500);
        ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
        w.sim.clear_audio_events();
        w.sim.clear_news_events();
        w.sim.issue_move_order(w.ant, TileCoord{30, 30});
        ASSERT_EQ(w.u().pixel_x, 368);
        ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
        ASSERT_FALSE(w.sim.has_pending_path(w.ant));
        run_ms(w.sim, 3000);
        ASSERT_FALSE(w.sim.has_news_event(0, kNewsCantGoThere));
        ASSERT_FALSE(w.sim.has_audio_event(63));
        ASSERT_EQ(w.u().type, AntType::Worker);
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
    } TEST_END();

    TEST_CASE("1.7 Ordering an ant that stands on a power-up onto its own tile picks it up (a one-tile path completes on the next idle frame)") {
        Walk w;
        start_walk(w, TileCoord{10, 10}, TileCoord{11, 10}, 3);
        make_island(w.sim);
        w.run_to(500);
        w.sim.issue_move_order(w.ant, TileCoord{30, 30});
        run_ms(w.sim, 3000);
        ASSERT_EQ(w.u().type, AntType::Worker);
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        w.sim.issue_move_order(w.ant, TileCoord{11, 10});
        ASSERT_EQ(w.u().pixel_x, 368);
        const int t = wait_ms(w.sim, 1000, [&]() { return w.u().type == AntType::Thief; });
        ASSERT_TRUE(t >= 100 && t <= 300);                                  // PATHMGR run plus the first idle frame
        ASSERT_EQ(w.u().loco_action, AntUnit::kActionGetPow);
        ASSERT_EQ(w.u().pixel_x, 368);                                      // no snap delta on a one-tile path
        ASSERT_FALSE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
    } TEST_END();

    TEST_CASE("1.8 No pick-up from a distance and none by an ant that does not end its walk on the tile (orders next to it, orders of the remake's own systems)") {
        {   // an order to the tile next to the power-up
            Walk w;
            make_world(w.sim);
            w.sim.grid_mut().place_powerup(15, 15, 4);
            w.ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{10, 15});
            w.sim.issue_move_order(w.ant, TileCoord{14, 15});
            ASSERT_TRUE(wait_ms(w.sim, 8000, [&]() { return tile_of(w.u()) == TileCoord{14, 15} && w.u().waypoints.empty() && !w.sim.has_pending_path(w.ant); }) >= 0);
            run_ms(w.sim, 3000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{15, 15}));
        }
        {   // an order to a tile behind it: the path goes around the solid power-up
            Walk w;
            make_world(w.sim);
            w.sim.grid_mut().place_powerup(15, 15, 4);
            w.ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{13, 15});
            w.sim.issue_move_order(w.ant, TileCoord{17, 15});
            bool on_tile = false;
            ASSERT_TRUE(wait_ms(w.sim, 8000, [&]() {
                if (tile_of(w.u()) == TileCoord{15, 15}) on_tile = true;
                return tile_of(w.u()) == TileCoord{17, 15} && w.u().waypoints.empty();
            }) >= 0);
            ASSERT_FALSE(on_tile);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{15, 15}));
        }
        {   // an order without the player flag (guard AI, hill queue, ability approach) never takes it: the goal is not valid
            Walk w;
            make_world(w.sim);
            w.sim.grid_mut().place_powerup(15, 15, 4);
            w.ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{12, 15});
            w.sim.issue_internal_move_order(w.ant, TileCoord{15, 15});
            run_ms(w.sim, 6000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_NE(tile_of(w.u()), (TileCoord{15, 15}));
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{15, 15}));
        }
    } TEST_END();

    TEST_CASE("1.9 A power-up under an idle or a placed ant is never taken: nothing but the arrival of an order takes it") {
        SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().place_powerup(15, 15, 4);
        const uint32_t on = sim.spawn_unit(0, AntType::Worker, TileCoord{15, 15});
        run_ms(sim, 60000);
        ASSERT_EQ(sim.get_unit(on).type, AntType::Worker);
        ASSERT_EQ(sim.get_unit(on).loco_action, AntUnit::kActionIdle);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{15, 15}));
        // a power-up appearing under a walker (the flower dropper's landing) is not taken by the walker that passes over it
        const uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{20, 20});
        sim.issue_move_order(w2, TileCoord{24, 20});
        run_ms(sim, 600);
        const TileCoord under = tile_of(sim.get_unit(w2));
        sim.grid_mut().place_powerup(under.x, under.y, 5);
        run_ms(sim, 8000);
        ASSERT_EQ(sim.get_unit(w2).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at(under));
    } TEST_END();

    TEST_CASE("1.10 The type table: every power-up id gives its type, hit points stay, the maximum is 10 for all types") {
        for (uint8_t hat = 1; hat <= 5; ++hat) {
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10}, hat);
            w.sim.get_unit(w.ant).hp = 3;
            ASSERT_TRUE(wait_ms(w.sim, 3000, [&]() { return w.u().loco_action == AntUnit::kActionGetPow; }) >= 0);
            ASSERT_EQ(static_cast<uint8_t>(w.u().type), hat);               // grid ids equal the AntType numbers 1 bomber .. 5 swimmer
            ASSERT_EQ(w.u().hp, 3u);
            ASSERT_EQ(w.u().max_hp, 10u);
            run_ms(w.sim, 1500);
            ASSERT_EQ(w.u().hp, 3u);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
        }
    } TEST_END();

    TEST_CASE("1.11 A typed ant drops its old power-up on a free neighbour tile (West 2/9, every other tile 1/9); the picked one is gone") {
        int hits[3][3] = {};
        const int kSeeds = 900;
        for (int seed = 1; seed <= kSeeds; ++seed) {
            Walk w;
            make_world(w.sim, static_cast<uint32_t>(seed));
            w.sim.grid_mut().place_powerup(15, 15, 4);
            w.ant = w.sim.spawn_unit(0, AntType::Bomber, TileCoord{12, 15});
            w.sim.issue_move_order(w.ant, TileCoord{15, 15});
            ASSERT_TRUE(wait_ms(w.sim, 3000, [&]() { return w.u().loco_action == AntUnit::kActionGetPow; }) >= 0);
            ASSERT_EQ(w.u().type, AntType::Combat);
            ASSERT_FALSE(w.sim.grid().has_powerup_at(TileCoord{15, 15}));
            int found = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx || dy) && w.sim.grid().has_powerup_at(TileCoord{15 + dx, 15 + dy})) {
                        ASSERT_EQ(w.sim.grid().get_powerup_type(TileCoord{15 + dx, 15 + dy}), 1);   // the bomber's power-up
                        ++hits[dy + 1][dx + 1];
                        ++found;
                    }
                }
            }
            ASSERT_EQ(found, 1);
        }
        ASSERT_EQ(hits[1][1], 0);
        ASSERT_TRUE(hits[1][0] > 150 && hits[1][0] < 250);                  // West: 2/9 of 900 = 200
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                if ((dx == 1 && dy == 1) || (dx == 0 && dy == 1)) continue;
                ASSERT_TRUE(hits[dy][dx] > 60 && hits[dy][dx] < 145);       // the others: 1/9 of 900 = 100
            }
        }
    } TEST_END();

    TEST_CASE("1.12 Nothing free around the tile: the old power-up is lost with the silent cue (sound 40); the own kind moves; a worker drops nothing") {
        auto surround = [](SimulationEngine& sim, TileCoord centre, TileCoord keep_free) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const TileCoord t{centre.x + dx, centre.y + dy};
                    if ((dx || dy) && t != keep_free) sim.grid_mut().place_powerup(t.x, t.y, 2);
                }
            }
        };
        {   // a swimmer climbs out of a water channel onto the power-up: the only open neighbour is water, which is never free
            SimulationEngine sim;
            make_world(sim);
            sim.grid_mut().place_powerup(15, 15, 4);
            surround(sim, TileCoord{15, 15}, TileCoord{14, 15});
            for (int x = 11; x <= 14; ++x) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), 15u).terrain_type = TERRAIN_WATER;
            const uint32_t sw = sim.spawn_unit(0, AntType::Swimmer, TileCoord{11, 15});
            sim.issue_move_order(sw, TileCoord{15, 15});
            sim.clear_audio_events();
            ASSERT_TRUE(wait_ms(sim, 8000, [&]() { return sim.get_unit(sw).loco_action == AntUnit::kActionGetPow; }) >= 0);
            ASSERT_EQ(sim.get_unit(sw).type, AntType::Combat);
            ASSERT_TRUE(sim.has_audio_event(40));
            {   // the powerupd effect belongs to the owner's machine only (FUN_01020e6e tests IsLocal first, 0x1020e7a)
                int cues = 0;
                for (const auto& e : sim.poll_audio_events()) {
                    if (e.sound_id != 40) continue;
                    ++cues;
                    ASSERT_EQ(e.target_player, 0);
                    ASSERT_TRUE(e.world_x != 0 || e.world_y != 0);                // still positional: an effect of the map view
                }
                ASSERT_EQ(cues, 1);
            }
            int swimmer_hats = 0, fire_hats = 0;
            for (int y = 14; y <= 16; ++y) {
                for (int x = 14; x <= 16; ++x) {
                    if (!sim.grid().has_powerup_at(TileCoord{x, y})) continue;
                    if (sim.grid().get_powerup_type(TileCoord{x, y}) == 5) ++swimmer_hats;
                    if (sim.grid().get_powerup_type(TileCoord{x, y}) == 2) ++fire_hats;
                }
            }
            ASSERT_EQ(swimmer_hats, 0);                                       // the swimmer's own power-up is lost
            ASSERT_EQ(fire_hats, 7);
        }
        {   // a worker has nothing to drop: the cue does not play
            SimulationEngine sim;
            make_world(sim);
            sim.grid_mut().place_powerup(15, 15, 4);
            surround(sim, TileCoord{15, 15}, TileCoord{14, 15});
            const uint32_t wk = sim.spawn_unit(0, AntType::Worker, TileCoord{11, 15});
            sim.issue_move_order(wk, TileCoord{15, 15});
            sim.clear_audio_events();
            ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return sim.get_unit(wk).loco_action == AntUnit::kActionGetPow; }) >= 0);
            ASSERT_EQ(sim.get_unit(wk).type, AntType::Combat);
            ASSERT_FALSE(sim.has_audio_event(40));
        }
        {   // a combat ant taking a combat power-up: the old one is dropped, the ant is a combat ant again
            SimulationEngine sim;
            make_world(sim);
            sim.grid_mut().place_powerup(15, 15, 4);
            const uint32_t c = sim.spawn_unit(0, AntType::Combat, TileCoord{12, 15});
            sim.issue_move_order(c, TileCoord{15, 15});
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return sim.get_unit(c).loco_action == AntUnit::kActionGetPow; }) >= 0);
            ASSERT_EQ(sim.get_unit(c).type, AntType::Combat);
            int hats = 0;
            for (int y = 14; y <= 16; ++y) for (int x = 14; x <= 16; ++x) if (sim.grid().has_powerup_at(TileCoord{x, y})) ++hats;
            ASSERT_EQ(hats, 1);
        }
    } TEST_END();

    TEST_CASE("1.13 The Stop button never picks up: an ant on a power-up (no target) is skipped, one inside the window is sent to a neighbour tile") {
        {   // standing ant without a target
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            make_island(w.sim);
            w.run_to(500);
            w.sim.issue_move_order(w.ant, TileCoord{30, 30});
            run_ms(w.sim, 3000);
            ASSERT_EQ(w.u().loco_action, AntUnit::kActionIdle);
            ASSERT_FALSE(w.sim.stop_ant(w.ant));
            run_ms(w.sim, 2000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        }
        {   // Stop inside the window: an ordinary move to the own tile, which is not a valid goal without the player flag
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            w.run_to(500);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{11, 10}));
            ASSERT_TRUE(w.sim.stop_ant(w.ant));
            ASSERT_EQ(w.u().pixel_x, 368);
            run_ms(w.sim, 4000);
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_NE(tile_of(w.u()), (TileCoord{11, 10}));                  // it moved to a free neighbour tile
            ASSERT_TRUE(w.u().waypoints.empty());
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        }
        {   // Stop before the crossing: the ant stops on the tile it is on
            Walk w;
            start_walk(w, TileCoord{10, 10}, TileCoord{11, 10});
            w.run_to(400);
            ASSERT_TRUE(w.sim.stop_ant(w.ant));
            run_ms(w.sim, 3000);
            ASSERT_EQ(tile_of(w.u()), (TileCoord{10, 10}));
            ASSERT_EQ(w.u().type, AntType::Worker);
            ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{11, 10}));
        }
        {   // an idle ant that has no target is not touched at all
            Walk w;
            make_world(w.sim);
            w.ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            ASSERT_FALSE(w.sim.stop_ant(w.ant));
        }
    } TEST_END();

    TEST_CASE("1.14 The tunnel hop (ISLANDS corners): each next power-up is ordered inside the window of the one before, only the last one is taken") {
        Walk w;
        make_world(w.sim);
        // a column of power-ups going north: (10, 9) bomber, (10, 8) thief, (10, 7) swimmer
        w.sim.grid_mut().place_powerup(10, 9, 1);
        w.sim.grid_mut().place_powerup(10, 8, 3);
        w.sim.grid_mut().place_powerup(10, 7, 5);
        w.ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        w.sim.issue_move_order(w.ant, TileCoord{10, 9});
        auto wait_cross = [&](TileCoord t) {
            return wait_ms(w.sim, 3000, [&]() { return tile_of(w.u()) == t; }) >= 0;
        };
        ASSERT_TRUE(wait_cross(TileCoord{10, 9}));
        w.sim.issue_move_order(w.ant, TileCoord{10, 8});                    // inside the window of the first power-up
        ASSERT_EQ(w.u().pixel_y, 9 * 32 + 16);                              // snapped onto the first power-up's tile
        ASSERT_TRUE(wait_cross(TileCoord{10, 8}));
        ASSERT_EQ(w.u().type, AntType::Worker);
        w.sim.issue_move_order(w.ant, TileCoord{10, 7});                    // inside the window of the second one
        ASSERT_EQ(w.u().pixel_y, 8 * 32 + 16);
        ASSERT_TRUE(wait_ms(w.sim, 3000, [&]() { return w.u().loco_action == AntUnit::kActionGetPow; }) >= 0);
        ASSERT_EQ(w.u().type, AntType::Swimmer);                            // only the last power-up was taken
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{10, 9}));
        ASSERT_TRUE(w.sim.grid().has_powerup_at(TileCoord{10, 8}));
        ASSERT_FALSE(w.sim.grid().has_powerup_at(TileCoord{10, 7}));
        ASSERT_EQ(w.sim.grid().get_powerup_type(TileCoord{10, 9}), 1);
        ASSERT_EQ(w.sim.grid().get_powerup_type(TileCoord{10, 8}), 3);
    } TEST_END();

    TEST_CASE("1.15 Immunity: an attack order (single or group) on an ant standing on a power-up fails with \"Can't go there.\"; nobody is hurt") {
        SimulationEngine sim;
        make_world(sim);
        make_island(sim);
        sim.grid_mut().place_powerup(15, 15, 4);
        const uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{14, 15});
        sim.issue_move_order(victim, TileCoord{15, 15});
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return tile_of(sim.get_unit(victim)) == TileCoord{15, 15}; }) >= 0);
        sim.issue_move_order(victim, TileCoord{30, 30});                    // the can't-go trick inside the window
        run_ms(sim, 3000);
        ASSERT_EQ(sim.get_unit(victim).type, AntType::Worker);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{15, 15}));
        std::vector<uint32_t> attackers;
        for (int i = 0; i < 3; ++i) attackers.push_back(sim.spawn_unit(0, AntType::Combat, TileCoord{11, 14 + i}));
        for (uint32_t a : attackers) {
            AntOrder o = order(a, OrderType::Attack, TileCoord{15, 15});
            o.target_entity_id = static_cast<int32_t>(victim);
            sim.issue_order(o);
        }
        sim.clear_news_events();
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_news_event(0, kNewsCantGoThere); }) >= 0);
        run_ms(sim, 10000);
        ASSERT_EQ(sim.get_unit(victim).hp, 10u);
        ASSERT_EQ(sim.get_unit(victim).pos, (TileCoord{15, 15}));
        for (uint32_t a : attackers) ASSERT_EQ(sim.get_unit(a).hp, 10u);
    } TEST_END();

    TEST_CASE("1.16 The exception of the original: a contact already under way still lands on an ant that a power-up appeared under") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{15, 15});
        const uint32_t att = sim.spawn_unit(0, AntType::Combat, TileCoord{11, 15});
        AntOrder o = order(att, OrderType::Attack, TileCoord{15, 15});
        o.target_entity_id = static_cast<int32_t>(victim);
        sim.issue_order(o);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.get_unit(att).waypoints.size() > 1; }) >= 0);   // the path is installed
        sim.grid_mut().place_powerup(15, 15, 4);                             // the flower dropper lands under the victim
        run_ms(sim, 6000);
        ASSERT_TRUE(sim.get_unit(victim).hp < 10u);                          // the step into its tile is a contact, no object test
    } TEST_END();

    TEST_CASE("1.17 DropPowerup's tile test (FUN_01020de7): never a tile with the solid bit, never one of a hill's special tiles (the three tiles above the mound, the entrance, the raid tile)") {
        int above_hill = 0;
        int dropped = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            SimulationEngine sim;
            sim.init_test_world(60, 60, seed, 720000);
            sim.set_anthill(0, TileCoord{20, 20});
            sim.set_anthill(1, TileCoord{50, 50});
            const uint32_t b = sim.spawn_unit(1, AntType::Bomber, TileCoord{21, 18});            // the three tiles (20 .. 22, 19) above the mound are three of its eight neighbours
            sim.grid_mut().get_cell_mut(20, 17).static_solid = true;                               // one more neighbour with the solid bit
            sim.kill_unit(b);
            for (int x = 20; x <= 22; ++x) {
                if (sim.grid().has_powerup_at(TileCoord{x, 19})) ++above_hill;
            }
            if (sim.grid().has_powerup_at(TileCoord{20, 17})) ASSERT_TRUE(false);                   // never on the solid tile
            for (int y = 17; y <= 19; ++y) {
                for (int x = 20; x <= 22; ++x) {
                    if (sim.grid().has_powerup_at(TileCoord{x, y})) ++dropped;
                }
            }
        }
        ASSERT_EQ(above_hill, 0);
        ASSERT_EQ(dropped, 40);                                                                    // one power-up every time, on one of the four free neighbours
    } TEST_END();

    // ---- the flower droppers: FDTASK (0x100fc0d) polls every 3000 ms (+ its run time), the first poll only stamps, a posting needs more than `interval` seconds
    // since the stamp, the stamp is renewed at the posting, the drop effect lasts 820 ms and its last frame sets the tile (0x100fe50) ---------------------------
    TEST_CASE("2.1 SMALL (interval 15 s): the first poll stamps, the drop is posted at the fifth poll (15005 ms), lands 820 ms later, and every 15005 ms after") {
        SimulationEngine sim;
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL"));
        sim.init(lvl, 42);
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 2u);
        const TileCoord drop{sim.get_world_state().flower_droppers[0].drop_x, sim.get_world_state().flower_droppers[0].drop_y};
        int first = -1, second = -1, landed = -1;
        for (int t = 1; t <= 620 && second < 0; ++t) {
            sim.tick();
            const bool dropping = sim.get_world_state().flower_droppers[0].is_dropping;
            if (dropping && first < 0) first = t;
            if (!dropping && first >= 0 && landed < 0) landed = t;
            if (dropping && landed >= 0) second = t;
        }
        ASSERT_EQ(first, 301);                                                              // 15005 ms: poll 5 of 3001 ms, stamped at poll 0 (strict compare: 15005 > 15000)
        ASSERT_EQ(landed, 317);                                                             // 15005 + 820 = 15825 ms
        ASSERT_EQ(second, 601);                                                             // restamped at the posting: 15005 ms later, not 15 s after the landing
        ASSERT_TRUE(sim.grid().has_powerup_at(drop));
    } TEST_END();

    TEST_CASE("2.2 MEDIUM (interval 8 s): the third poll (9003 ms) posts, then every third poll after 9003 ms: 9 s, not 8 s") {
        SimulationEngine sim;
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/MEDIUM.LVL"));
        sim.init(lvl, 42);
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 1u);
        int first = -1, second = -1;
        for (int t = 1; t <= 400 && second < 0; ++t) {
            sim.tick();
            const bool dropping = sim.get_world_state().flower_droppers[0].is_dropping;
            if (dropping && first < 0) first = t;
            if (dropping && first >= 0 && t > first + 30) second = t;
        }
        ASSERT_EQ(first, 181);                                                              // 9003 ms
        ASSERT_EQ(second, 361);                                                             // 18006 ms
    } TEST_END();

    TEST_CASE("2.3 The drop tile is tested at the poll: a bomb, a fire wall or an ant on it refuses the posting, the stamp stays, the next poll drops; a power-up there is replaced") {
        SimulationEngine sim;
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL"));
        sim.init(lvl, 42);
        const TileCoord drop{2, 20};
        const size_t left = dropper_at_column(sim, 2);
        ASSERT_TRUE(left < 2);
        sim.grid_mut().place_bomb(2, 20, 0);
        for (int t = 0; t < 320; ++t) sim.tick();                                            // 16 s: poll 5 (15005 ms) refused
        ASSERT_FALSE(sim.get_world_state().flower_droppers[left].is_dropping);
        ASSERT_FALSE(sim.grid().has_powerup_at(drop));
        sim.grid_mut().clear_bomb(2, 20);
        int posted = -1;
        for (int t = 320; t < 500 && posted < 0; ++t) {
            sim.tick();
            if (sim.get_world_state().flower_droppers[left].is_dropping) posted = t + 1;
        }
        ASSERT_EQ(posted, 361);                                                             // the next poll (18006 ms): the stamp of 0 still counts, nothing waits another 15 s
        for (int t = 0; t < 20; ++t) sim.tick();
        ASSERT_TRUE(sim.grid().has_powerup_at(drop));                                        // landed and left uncollected
        int again = -1;
        for (int t = 381; t < 700 && again < 0; ++t) {
            sim.tick();
            if (sim.get_world_state().flower_droppers[left].is_dropping) again = t + 1;
        }
        ASSERT_EQ(again, 661);                                                              // poll 11 (33011 ms, 15005 ms after the stamp of 18006): a power-up on the tile does not refuse, the new one replaces it
        ASSERT_TRUE(sim.grid().has_powerup_at(drop));
        for (int t = 661; t < 680; ++t) sim.tick();                                          // landed (33831 ms)
        // an ant on the tile refuses the next posting (poll 16, 48016 ms)
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, drop);
        bool posted_with_ant = false;
        for (int t = 680; t < 975; ++t) {
            sim.tick();
            if (sim.get_world_state().flower_droppers[left].is_dropping) posted_with_ant = true;
        }
        ASSERT_FALSE(posted_with_ant);
        ASSERT_TRUE(sim.get_unit(ant).is_alive());
    } TEST_END();

    TEST_CASE("2.4 The power-up type is drawn at the posting as FUN_01009fd8 does: the first type whose running total of trunc(p * 10000) exceeds rand() % 10000, else rand() % 5") {
        const std::array<double, 5> small{0.45, 0.0, 0.0, 0.1, 0.45};                      // SMALL: bomber, -, -, swimmer, fire
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 0), 0);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 4499), 0);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 4500), 3);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 5499), 3);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 5500), 4);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(small, 9999), 4);
        const std::array<double, 5> even{0.2, 0.2, 0.2, 0.2, 0.2};
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(even, 1999), 0);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(even, 2000), 1);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(even, 9999), 4);
        const std::array<double, 5> islands{0.05, 0.0, 0.2, 0.7, 0.05};
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(islands, 499), 0);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(islands, 500), 2);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(islands, 9499), 3);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(islands, 9500), 4);
        const std::array<double, 5> thin{0.1, 0.0, 0.0, 0.0, 0.0};                         // totals below 10000: no type, the caller draws rand() % 5
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(thin, 999), 0);
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(thin, 1000), 0xFF);
        const std::array<double, 5> none{};
        ASSERT_EQ(SimulationEngine::pick_dropper_powerup(none, 0), 0xFF);
    } TEST_END();

    TEST_CASE("2.5 The drop effect: cue 62 (powerdrip) 100 ms after the posting, the snapshot reports the effect time, the tile is set at 820 ms") {
        SimulationEngine sim;
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL"));
        sim.init(lvl, 42);
        const size_t left = dropper_at_column(sim, 2);
        ASSERT_TRUE(left < 2);
        for (int t = 0; t < 301; ++t) sim.tick();                                            // posted at 15005 ms
        sim.clear_audio_events();
        ASSERT_TRUE(sim.get_world_state().flower_droppers[left].is_dropping);
        ASSERT_TRUE(sim.get_world_state().flower_droppers[left].drop_elapsed_ms < 100);
        ASSERT_FALSE(sim.has_audio_event(62));
        sim.tick();                                                                          // 15100 ms: the cue is due at 15105
        ASSERT_FALSE(sim.has_audio_event(62));
        sim.tick();                                                                          // 15150 ms
        ASSERT_TRUE(sim.has_audio_event(62));
        ASSERT_TRUE(sim.get_world_state().flower_droppers[left].drop_elapsed_ms >= 100);
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{2, 20}));
        for (int t = 0; t < 14; ++t) sim.tick();                                             // 15850 ms
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{2, 20}));
    } TEST_END();

    // ---- where the droppers come from: the plants of block 1 whose cell holds a block 4 record with a trigger, and nothing else (FDTASK 0x100fc0d walks the plants and
    // asks the table of block 4 for the record at each plant's cell, 0x1008d79; the map's size plays no part) ---------------------------------------------------
    TEST_CASE("2.6 A 40 x 40 map without a flower dropper gets none (the remake once gave every such map the two droppers of SMALL.LVL, whose droppers come from its waypoints)") {
        const std::string small_path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/SMALL.LVL";
        const std::vector<uint8_t> bytes = read_bytes(small_path);
        const MapTail tail = find_tail(bytes);
        ASSERT_TRUE(tail.ok);
        // block 4 emptied: a count of 0 and the final word (the two records go)
        std::vector<uint8_t> bare(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(tail.b4_count));
        bare.push_back(0);
        bare.push_back(0);
        bare.push_back(bytes[tail.final_word]);
        bare.push_back(bytes[tail.final_word + 1]);
        ants::assets::LevelData lvl;
        ASSERT_TRUE(lvl.load_from_memory(bare.data(), bare.size()));
        ASSERT_EQ(lvl.width(), 40u);
        ASSERT_EQ(lvl.height(), 40u);
        ASSERT_TRUE(lvl.waypoints.empty());
        SimulationEngine sim;
        sim.init(lvl, 42);
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 0u);
        for (int t = 0; t < 420; ++t) sim.tick();                                            // 21 s: the fallback posted at 15 s and landed at 15.8 s
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{2, 20}));
        ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{37, 20}));

        // the same map with the trigger of its first waypoint taken away (the record shrinks to its 8 bytes) has the second dropper and no other
        const size_t first_waypoint = tail.b4_count + 2;
        std::vector<uint8_t> trimmed(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(first_waypoint + 4));
        trimmed.insert(trimmed.end(), {0, 0, 0, 0});                                          // the flag dword, now 0
        trimmed.insert(trimmed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(first_waypoint + 8 + 44), bytes.end());   // the rest: the second record, the final word
        ants::assets::LevelData half;
        ASSERT_TRUE(half.load_from_memory(trimmed.data(), trimmed.size()));
        ASSERT_EQ(half.waypoints.size(), 2u);
        ASSERT_EQ(half.waypoints[0].flag, 0u);
        ASSERT_EQ(half.waypoints[1].flag, 1u);
        SimulationEngine sim_half;
        sim_half.init(half, 42);
        ASSERT_EQ(sim_half.get_world_state().flower_droppers.size(), 1u);

        // and the shipped SMALL.LVL still has its two
        ants::assets::LevelData shipped;
        ASSERT_TRUE(shipped.load_lvl(small_path));
        SimulationEngine sim_shipped;
        sim_shipped.init(shipped, 42);
        ASSERT_EQ(sim_shipped.get_world_state().flower_droppers.size(), 2u);
    } TEST_END();

    // ---- a start marker outside the grid: the original indexes its row table with the record unchecked (0x100f17f), so the remake refuses such a map for a roster that
    // contains the team (LevelData::validate); an engine that is handed it anyway places no ant for that marker, exactly as if the record were not in the file ------
    TEST_CASE("2.7 A start marker outside the grid places no ant: the engine plays the map as if the record were not there (no ant outside the map, no undefined behaviour)") {
        const std::vector<uint8_t> tiny = read_bytes(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL");
        const MapTail tail = find_tail(tiny);
        ASSERT_TRUE(tail.ok);
        size_t green = 0;                                                                    // the first GSTART record (tile 154)
        const size_t records = word_at(tiny, tail.b1_count);
        for (size_t i = 0; i < records && green == 0; ++i) {
            if (word_at(tiny, tail.b1_records + i * 6) == 154) green = tail.b1_records + i * 6;
        }
        ASSERT_TRUE(green != 0);

        ants::assets::LevelData clean;
        ASSERT_TRUE(clean.load_from_memory(tiny.data(), tiny.size()));
        size_t markers = 0;
        for (const auto& a : clean.anthill_spawns) markers += a.team_id < 4 ? 1u : 0u;
        ASSERT_EQ(markers, 12u);                                                             // TINY: twelve start markers, three ants for each team

        const struct { uint16_t row; uint16_t column; } places[] = {{21512, 21}, {31, 9}, {8, 31}, {65535, 65535}};
        for (const auto& pl : places) {
            std::vector<uint8_t> bad = tiny;                                                 // the marker moved outside the grid
            bad[green + 2] = static_cast<uint8_t>(pl.row & 0xFF);
            bad[green + 3] = static_cast<uint8_t>(pl.row >> 8);
            bad[green + 4] = static_cast<uint8_t>(pl.column & 0xFF);
            bad[green + 5] = static_cast<uint8_t>(pl.column >> 8);
            std::vector<uint8_t> gone = tiny;                                                // the record cut out of the file
            gone.erase(gone.begin() + static_cast<std::ptrdiff_t>(green), gone.begin() + static_cast<std::ptrdiff_t>(green + 6));
            gone[tail.b1_count] = static_cast<uint8_t>((records - 1) & 0xFF);
            gone[tail.b1_count + 1] = static_cast<uint8_t>((records - 1) >> 8);

            ants::assets::LevelData bad_level;
            ants::assets::LevelData gone_level;
            ASSERT_TRUE(bad_level.load_from_memory(bad.data(), bad.size()));
            ASSERT_TRUE(gone_level.load_from_memory(gone.data(), gone.size()));
            ASSERT_FALSE(bad_level.validate(0x0F).playable);                                 // a roster with the green team: refused
            ASSERT_TRUE(bad_level.validate(0x0E).playable);                                  // without it: its markers are not used
            ASSERT_TRUE(gone_level.validate(0x0F).clean());

            SimulationEngine a;
            SimulationEngine b;
            a.init(bad_level, 7);
            b.init(gone_level, 7);
            size_t ants_a = 0;
            for (const auto& ant : a.get_world_state().ants) {
                ASSERT_TRUE(ant.tile_x >= 0 && ant.tile_x < 31 && ant.tile_y >= 0 && ant.tile_y < 31);
                ++ants_a;
            }
            ASSERT_EQ(ants_a, b.get_world_state().ants.size());
            ASSERT_EQ(ants_a, markers - 1);                                                  // every marker but that one has its ant
            ASSERT_TRUE(a.state_hash() == b.state_hash());
            for (int t = 0; t < 120; ++t) {
                a.tick();
                b.tick();
            }
            ASSERT_TRUE(a.state_hash() == b.state_hash());
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL POWER-UP ACTION TESTS PASSED CLEANLY <<<\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n";
    return 1;
}
