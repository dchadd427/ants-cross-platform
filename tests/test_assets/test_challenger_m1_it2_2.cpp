#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/mirroring.hpp"
#include "ants_assets/asset_archive.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <random>
#include <memory>

using namespace ants::assets;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define CHALLENGE_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [CHALLENGER M1-IT2] " << name << "\n" \
              << "=======================================================\n"

#define CHALLENGE_CASE(name) \
    do { \
        ++g_test_count; \
        std::cout << "  [RUNNING] " << name << " ... " << std::flush; \
        int prev_fails = g_test_failures; \
        auto test_fn = [&]()

#define CHALLENGE_END() \
        ; \
        try { \
            test_fn(); \
        } catch (const std::exception& e) { \
            std::cout << "FAILED!\n    Unexpected exception: " << e.what() << "\n"; \
            ++g_test_failures; \
        } catch (...) { \
            std::cout << "FAILED!\n    Unknown exception!\n"; \
            ++g_test_failures; \
        } \
        if (g_test_failures == prev_fails) { \
            std::cout << "PASS\n"; \
        } \
    } while (0)

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

#define ASSERT_EQ(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) == (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " == " #b \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        ++g_assert_count; \
        if ((a) == (b)) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " != " #b \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) > (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " > " #b \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_LT(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) < (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " < " #b \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

// ============================================================================
// SUITE 1: Mirror Bounding Box Aliasing & Mathematical Invariants
// ============================================================================
void test_suite_bounding_box_aliasing() {
    CHALLENGE_SUITE("Suite 1: mirror_bounding_box Argument Aliasing & Involution");

    CHALLENGE_CASE("1.1 Out-of-place vs In-place Aliased Equivalency across 40,000 Pairs") {
        for (int32_t l = -100; l <= 100; ++l) {
            for (int32_t r = -100; r <= 100; ++r) {
                int32_t out_l = 0, out_r = 0;
                mirror_bounding_box(l, r, out_l, out_r);

                ASSERT_EQ(out_l, -r);
                ASSERT_EQ(out_r, -l);

                // In-place aliasing: mirror_bounding_box(l, r, l, r)
                int32_t aliased_l = l;
                int32_t aliased_r = r;
                mirror_bounding_box(aliased_l, aliased_r, aliased_l, aliased_r);

                ASSERT_EQ(aliased_l, out_l);
                ASSERT_EQ(aliased_r, out_r);
                ASSERT_EQ(aliased_l, -r);
                ASSERT_EQ(aliased_r, -l);

                // In-place involution: mirroring twice restores original coordinates
                mirror_bounding_box(aliased_l, aliased_r, aliased_l, aliased_r);
                ASSERT_EQ(aliased_l, l);
                ASSERT_EQ(aliased_r, r);
            }
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("1.2 Reversed Aliasing & Single-Variable Aliasing") {
        // Reversed output: out_left is r, out_right is l
        int32_t a = 15, b = 40;
        mirror_bounding_box(a, b, b, a);
        // After call: out_left (which wrote to b) should be -40; out_right (which wrote to a) should be -15
        ASSERT_EQ(b, -40);
        ASSERT_EQ(a, -15);

        // All 4 arguments aliased to the same variable
        int32_t x = 77;
        mirror_bounding_box(x, x, x, x);
        ASSERT_EQ(x, -77);

        x = -123;
        mirror_bounding_box(x, x, x, x);
        ASSERT_EQ(x, 123);
    } CHALLENGE_END();

    CHALLENGE_CASE("1.3 Extreme Boundary Values") {
        constexpr int32_t MAX_COORD = 1000000;
        constexpr int32_t MIN_COORD = -1000000;

        int32_t l = MIN_COORD, r = MAX_COORD;
        mirror_bounding_box(l, r, l, r);
        ASSERT_EQ(l, -MAX_COORD);
        ASSERT_EQ(r, -MIN_COORD);

        mirror_bounding_box(l, r, l, r);
        ASSERT_EQ(l, MIN_COORD);
        ASSERT_EQ(r, MAX_COORD);

        int32_t z1 = 0, z2 = 0;
        mirror_bounding_box(z1, z2, z1, z2);
        ASSERT_EQ(z1, 0);
        ASSERT_EQ(z2, 0);
    } CHALLENGE_END();
}

// ============================================================================
// SUITE 2: In-Place mirror_pixel_buffer (src == dst) Stress Tests
// ============================================================================
void test_suite_mirror_pixel_buffer_in_place() {
    CHALLENGE_SUITE("Suite 2: In-Place mirror_pixel_buffer (src == dst) Equivalence & Stride");

    // Test a wide variety of widths (odd, even, powers of 2, non-powers of 2)
    const std::vector<uint32_t> test_widths = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 31, 32, 33, 47, 48, 49, 60, 63, 64, 65, 127, 128, 255, 256
    };
    const std::vector<uint32_t> test_heights = {
        1, 2, 3, 5, 8, 16, 32
    };

    CHALLENGE_CASE("2.1 In-Place vs Out-of-Place Mirroring Equivalence across Odd and Even Dimensions") {
        for (uint32_t w : test_widths) {
            for (uint32_t h : test_heights) {
                // Test tight pitch, +1, +2, and DWORD aligned pitch
                const std::vector<uint32_t> pitches = {
                    w,
                    w + 1,
                    w + 3,
                    ((w + 3) / 4) * 4,
                    w + 8
                };

                for (uint32_t p : pitches) {
                    if (p < w) continue;

                    size_t buf_size = static_cast<size_t>(p) * h;
                    std::vector<uint8_t> src_buf(buf_size, 0xEE);
                    std::vector<uint8_t> oop_buf(buf_size, 0xEE);
                    std::vector<uint8_t> inp_buf(buf_size, 0xEE);

                    // Initialize source pixels with deterministic unique values
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            uint8_t val = static_cast<uint8_t>((x * 37 + y * 73 + 19) & 0xFF);
                            src_buf[y * p + x] = val;
                            inp_buf[y * p + x] = val;
                        }
                    }

                    // 1. Execute Out-Of-Place reflection
                    mirror_pixel_buffer(src_buf.data(), oop_buf.data(), w, h, p);

                    // 2. Execute In-Place reflection (src == dst)
                    mirror_pixel_buffer(inp_buf.data(), inp_buf.data(), w, h, p);

                    // 3. Compare Out-of-place and In-place byte-for-byte
                    ASSERT_EQ(std::memcmp(oop_buf.data(), inp_buf.data(), buf_size), 0);

                    // 4. Verify horizontal reflection formula: dst[x] == src[w - 1 - x]
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            uint8_t expected = src_buf[y * p + (w - 1 - x)];
                            uint8_t actual = inp_buf[y * p + x];
                            ASSERT_EQ(actual, expected);
                        }
                        // Verify stride padding bytes are strictly 0x00
                        for (uint32_t pad = w; pad < p; ++pad) {
                            ASSERT_EQ(inp_buf[y * p + pad], 0);
                            ASSERT_EQ(oop_buf[y * p + pad], 0);
                        }
                    }

                    // 5. In-Place Involution: reflecting twice in-place must restore the original buffer
                    mirror_pixel_buffer(inp_buf.data(), inp_buf.data(), w, h, p);
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            ASSERT_EQ(inp_buf[y * p + x], src_buf[y * p + x]);
                        }
                    }
                }
            }
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("2.2 Zero and Malformed Dimension Safety") {
        std::vector<uint8_t> buf(64, 0xAA);
        std::vector<uint8_t> original = buf;

        // Nullptr checks
        mirror_pixel_buffer(nullptr, buf.data(), 8, 8, 8);
        ASSERT_TRUE(buf == original);

        mirror_pixel_buffer(buf.data(), nullptr, 8, 8, 8);
        ASSERT_TRUE(buf == original);

        // Zero dimension checks
        mirror_pixel_buffer(buf.data(), buf.data(), 0, 8, 8);
        ASSERT_TRUE(buf == original);

        mirror_pixel_buffer(buf.data(), buf.data(), 8, 0, 8);
        ASSERT_TRUE(buf == original);

        // Invalid pitch (< width)
        mirror_pixel_buffer(buf.data(), buf.data(), 8, 8, 7);
        ASSERT_TRUE(buf == original);
    } CHALLENGE_END();
}

// ============================================================================
// SUITE 3: LevelData Contract Accessors & Dual Syntax
// ============================================================================
void test_level_data_contract_on_map(const std::string& filepath, const std::string& map_name) {
    (void)map_name;
    LevelData level;
    ASSERT_TRUE(level.load_lvl(filepath));

    // 1. Contract Accessors: width() and height()
    uint32_t w = level.width();
    uint32_t h = level.height();
    ASSERT_GT(w, 0u);
    ASSERT_GT(h, 0u);

    // Dual syntax: level.width and level.height as uint32_t
    uint32_t w_field = level.width;
    uint32_t h_field = level.height;
    ASSERT_EQ(w, w_field);
    ASSERT_EQ(h, h_field);

    // Arithmetic on field
    uint32_t w_plus = level.width + 10;
    ASSERT_EQ(w_plus, w + 10);

    // 2. Contract Accessor: layer1_terrain(x, y)
    // Verify 100% agreement with get_cell_layer1 for all cells
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t tile1 = level.layer1_terrain(x, y);
            const MapCell& cell1 = level.get_cell_layer1(x, y);
            ASSERT_EQ(tile1, cell1.tile_index);
            ASSERT_LT(tile1, level.tile_dictionary.size());

            // 3. Contract Accessor: layer2_item(x, y)
            uint16_t tile2 = level.layer2_item(x, y);
            const MapCell& cell2 = level.get_cell_layer2(x, y);
            ASSERT_EQ(tile2, cell2.tile_index);
            if (tile2 != LVL_EMPTY_TILE) {
                ASSERT_LT(tile2, level.tile_dictionary.size());
            }
        }
    }

    // 4. Boundary queries return LVL_EMPTY_TILE (0x7FFE)
    ASSERT_EQ(level.layer1_terrain(w, 0), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer1_terrain(0, h), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer1_terrain(w + 10, h + 10), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer1_terrain(UINT32_MAX, UINT32_MAX), LVL_EMPTY_TILE);

    ASSERT_EQ(level.layer2_item(w, 0), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer2_item(0, h), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer2_item(w + 10, h + 10), LVL_EMPTY_TILE);
    ASSERT_EQ(level.layer2_item(UINT32_MAX, UINT32_MAX), LVL_EMPTY_TILE);

    // 5. Contract Accessor: anthill_spawns()
    const std::vector<AnthillSpawn>& spawns = level.anthill_spawns();
    ASSERT_EQ(spawns.size(), level.anthill_spawns.size());
    ASSERT_FALSE(spawns.empty());
    for (const auto& sp : spawns) {
        ASSERT_LT(sp.x, w);
        ASSERT_LT(sp.y, h);
        ASSERT_LT(sp.tile_id, level.tile_dictionary.size());
    }

    // 6. Contract Accessor: food_schedules()
    const std::vector<FoodSchedule>& schedules = level.food_schedules();
    ASSERT_EQ(schedules.size(), level.food_schedules.size());
    ASSERT_FALSE(schedules.empty());
    for (const auto& fs : schedules) {
        ASSERT_LT(fs.x, w);
        ASSERT_LT(fs.y, h);
        ASSERT_FALSE(fs.variants.empty());
        for (const auto& v : fs.variants) {
            if (v.tile_id != LVL_EMPTY_TILE) {
                ASSERT_LT(v.tile_id, level.tile_dictionary.size());
            }
        }
    }

    // 7. Const-correctness helper invocation
    auto verify_const_contract = [&](const LevelData& clvl) {
        uint32_t cw = clvl.width();
        uint32_t ch = clvl.height();
        ASSERT_EQ(cw, w);
        ASSERT_EQ(ch, h);

        uint16_t ct1 = clvl.layer1_terrain(0, 0);
        uint16_t ct2 = clvl.layer2_item(0, 0);
        ASSERT_EQ(ct1, level.layer1_terrain(0, 0));
        ASSERT_EQ(ct2, level.layer2_item(0, 0));

        const std::vector<AnthillSpawn>& cs = clvl.anthill_spawns();
        const std::vector<FoodSchedule>& cf = clvl.food_schedules();
        ASSERT_EQ(cs.size(), spawns.size());
        ASSERT_EQ(cf.size(), schedules.size());
    };
    verify_const_contract(level);
}

void test_suite_level_data_contract() {
    CHALLENGE_SUITE("Suite 3: LevelData Contract Accessors & Dual-Syntax Compatibility");

    const std::vector<std::string> maps = {
        "TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "ISLANDS.LVL", "TREASURE.LVL"
    };

    for (const auto& m : maps) {
        std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + m;
        CHALLENGE_CASE("3. LevelData Contract Validation: " + m) {
            test_level_data_contract_on_map(path, m);
        } CHALLENGE_END();
    }
}

// ============================================================================
// SUITE 4: Copy, Move, and Lifetime Stress on LevelData
// ============================================================================
void test_suite_level_data_lifetimes() {
    CHALLENGE_SUITE("Suite 4: LevelData Copy/Move & Parent Pointer Lifetime Safety");

    std::string path = std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL";

    CHALLENGE_CASE("4.1 Heap Allocation, Copying, and Destruction of Original") {
        LevelData copy_lvl;
        {
            auto heap_lvl = std::make_unique<LevelData>();
            ASSERT_TRUE(heap_lvl->load_lvl(path));
            uint16_t t00 = heap_lvl->layer1_terrain(0, 0);
            (void)t00;

            // Copy construct
            copy_lvl = *heap_lvl;
            // Now heap_lvl is destroyed at end of scope!
        }

        // Access copy_lvl after original is completely destroyed
        ASSERT_EQ(copy_lvl.width(), 31u);
        ASSERT_EQ(copy_lvl.height(), 31u);
        // Verify layer1_terrain(x, y) correctly uses its own parent pointer!
        uint16_t t00_copy = copy_lvl.layer1_terrain(0, 0);
        ASSERT_NE(t00_copy, LVL_EMPTY_TILE);
        ASSERT_EQ(copy_lvl.layer1_terrain(copy_lvl.width(), 0), LVL_EMPTY_TILE);
    } CHALLENGE_END();

    CHALLENGE_CASE("4.2 Move Semantics and Self-Assignment") {
        LevelData moved_lvl;
        {
            LevelData temp;
            ASSERT_TRUE(temp.load_lvl(path));
            moved_lvl = std::move(temp);
        }

        ASSERT_EQ(moved_lvl.width(), 31u);
        ASSERT_EQ(moved_lvl.height(), 31u);
        uint16_t t = moved_lvl.layer1_terrain(2, 2);
        ASSERT_NE(t, LVL_EMPTY_TILE);

        // Move construction
        LevelData moved_lvl2(std::move(moved_lvl));
        ASSERT_EQ(moved_lvl2.width(), 31u);
        ASSERT_EQ(moved_lvl2.height(), 31u);
        ASSERT_EQ(moved_lvl2.layer1_terrain(2, 2), t);

        // Copy self-assignment via pointer
        LevelData* p = &moved_lvl2;
        *p = *p;
        ASSERT_EQ(moved_lvl2.width(), 31u);
        ASSERT_EQ(moved_lvl2.layer1_terrain(2, 2), t);
    } CHALLENGE_END();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    std::cout << "=======================================================\n"
              << " Microsoft Ants Remake - Challenger M1-IT2 Suite\n"
              << " Target: Aliasing, In-Place Reflection, LevelData Contract\n"
              << "=======================================================\n";

    test_suite_bounding_box_aliasing();
    test_suite_mirror_pixel_buffer_in_place();
    test_suite_level_data_contract();
    test_suite_level_data_lifetimes();

    std::cout << "\n=======================================================\n"
              << " CHALLENGER TEST SUMMARY\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures > 0) {
        std::cout << " >>> VERDICT: CHALLENGER FAILURES DETECTED! <<<\n";
        return 1;
    }

    std::cout << " >>> VERDICT: ALL EMPIRICAL CHALLENGES PASSED! <<<\n";
    return 0;
}
