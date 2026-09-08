#include "ants_assets/asset_archive.hpp"
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/mirroring.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <cstdint>
#include <limits>
#include <random>
#include <algorithm>
#include <functional>

namespace fs = std::filesystem;
using namespace ants::assets;

// ============================================================================
// Zero-Dependency Adversarial Test Framework
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define CHALLENGE_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [CHALLENGER SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_challenge_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  [STRESS] " << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED!\n    Unexpected exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    } catch (...) {
        std::cout << "FAILED!\n    Unknown exception thrown!\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) {
        std::cout << "PASS\n";
    }
}

#define CHALLENGE_CASE(name) run_challenge_case(name, [&]()

#define CHALLENGE_END() );

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

#define ASSERT_NEAR(a, b, eps) \
    do { \
        ++g_assert_count; \
        if (std::abs((a) - (b)) > (eps)) { \
            std::cout << "FAILED!\n    Assertion failed: |" #a " - " #b "| <= " #eps \
                      << " (" << (a) << " vs " << (b) << ", diff=" << std::abs((a) - (b)) << ")" \
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
                      << " (" << (a) << " >= " << (b) << ")" \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_LE(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) <= (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " <= " #b \
                      << " (" << (a) << " > " << (b) << ")" \
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
                      << " (" << (a) << " <= " << (b) << ")" \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

#define ASSERT_GE(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) >= (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " >= " #b \
                      << " (" << (a) << " < " << (b) << ")" \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)


static std::string locate_assets_dir() {
#ifdef ORIGINAL_ASSETS_DIR
    if (fs::exists(ORIGINAL_ASSETS_DIR)) {
        return ORIGINAL_ASSETS_DIR;
    }
#endif
    std::vector<std::string> candidates = {
        "Original-Ants",
        "../Original-Ants",
        "../../Original-Ants",
        "../../../Original-Ants",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants"
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p;
        }
    }
    return "Original-Ants";
}

// ============================================================================
// SUITE 1: 8-Way Directional Headings, Boundaries & Extreme Conversions
// ============================================================================
void challenge_suite_1_headings() {
    CHALLENGE_SUITE("Suite 1: 8-Way Compass Facings & Boundary Conversions");

    CHALLENGE_CASE("1.1 Exhaustive 360-degree sweep at 1-degree resolution") {
        for (int deg = 0; deg < 360; ++deg) {
            float angle = static_cast<float>(deg);
            Direction dir = angle_to_direction(angle);
            
            // Expected sector logic:
            // [337.5, 360) or [0, 22.5) -> North (0)
            // [22.5, 67.5) -> NorthEast (1)
            // [67.5, 112.5) -> East (2)
            // [112.5, 157.5) -> SouthEast (3)
            // [157.5, 202.5) -> South (4)
            // [202.5, 247.5) -> SouthWest (5)
            // [247.5, 292.5) -> West (6)
            // [292.5, 337.5) -> NorthWest (7)
            Direction expected;
            if (angle < 22.5f || angle >= 337.5f) expected = Direction::North;
            else if (angle < 67.5f) expected = Direction::NorthEast;
            else if (angle < 112.5f) expected = Direction::East;
            else if (angle < 157.5f) expected = Direction::SouthEast;
            else if (angle < 202.5f) expected = Direction::South;
            else if (angle < 247.5f) expected = Direction::SouthWest;
            else if (angle < 292.5f) expected = Direction::West;
            else expected = Direction::NorthWest;

            ASSERT_EQ(static_cast<uint8_t>(dir), static_cast<uint8_t>(expected));
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("1.2 Sector boundary discontinuity stress (epsilon testing)") {
        const float boundaries[8] = { 22.5f, 67.5f, 112.5f, 157.5f, 202.5f, 247.5f, 292.5f, 337.5f };
        const Direction lower_dirs[8] = {
            Direction::North,     Direction::NorthEast, Direction::East,      Direction::SouthEast,
            Direction::South,     Direction::SouthWest, Direction::West,      Direction::NorthWest
        };
        const Direction upper_dirs[8] = {
            Direction::NorthEast, Direction::East,      Direction::SouthEast, Direction::South,
            Direction::SouthWest, Direction::West,      Direction::NorthWest, Direction::North
        };

        for (int i = 0; i < 8; ++i) {
            float b = boundaries[i];
            ASSERT_EQ(angle_to_direction(b - 0.001f), lower_dirs[i]);
            ASSERT_EQ(angle_to_direction(b), upper_dirs[i]);
            ASSERT_EQ(angle_to_direction(b + 0.001f), upper_dirs[i]);
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("1.3 Multi-revolution, negative, and extreme angle handling") {
        // Negative angles
        ASSERT_EQ(angle_to_direction(-45.0f), Direction::NorthWest); // 315°
        ASSERT_EQ(angle_to_direction(-90.0f), Direction::West);      // 270°
        ASSERT_EQ(angle_to_direction(-180.0f), Direction::South);    // 180°
        ASSERT_EQ(angle_to_direction(-360.0f), Direction::North);    // 0°
        ASSERT_EQ(angle_to_direction(-720.0f), Direction::North);
        ASSERT_EQ(angle_to_direction(-765.0f), Direction::NorthWest); // -45° -> 315°

        // Multi-revolution positive angles
        ASSERT_EQ(angle_to_direction(360.0f), Direction::North);
        ASSERT_EQ(angle_to_direction(405.0f), Direction::NorthEast);
        ASSERT_EQ(angle_to_direction(720.0f), Direction::North);
        ASSERT_EQ(angle_to_direction(360090.0f), Direction::East);
        ASSERT_EQ(angle_to_direction(720180.0f), Direction::South);
    } CHALLENGE_END();

    CHALLENGE_CASE("1.4 Vector to Direction extreme bounds & non-symmetric components") {
        // Center zero vector
        ASSERT_EQ(vector_to_direction(0, 0), Direction::South);

        // Cardinal / Ordinal axes across scales
        const int32_t scales[] = { 1, 2, 7, 32, 256, 4096, 65535, 1000000 };
        for (int32_t s : scales) {
            ASSERT_EQ(vector_to_direction(0, -s), Direction::North);
            ASSERT_EQ(vector_to_direction(s, -s), Direction::NorthEast);
            ASSERT_EQ(vector_to_direction(s, 0), Direction::East);
            ASSERT_EQ(vector_to_direction(s, s), Direction::SouthEast);
            ASSERT_EQ(vector_to_direction(0, s), Direction::South);
            ASSERT_EQ(vector_to_direction(-s, s), Direction::SouthWest);
            ASSERT_EQ(vector_to_direction(-s, 0), Direction::West);
            ASSERT_EQ(vector_to_direction(-s, -s), Direction::NorthWest);
        }

        // Off-axis dominance checks
        ASSERT_EQ(vector_to_direction(100, 1), Direction::East);
        ASSERT_EQ(vector_to_direction(1, 100), Direction::South);
        ASSERT_EQ(vector_to_direction(-100, 1), Direction::West);
        ASSERT_EQ(vector_to_direction(1, -100), Direction::North);

        // Int32 extreme boundaries
        constexpr int32_t MAX_I = std::numeric_limits<int32_t>::max();
        constexpr int32_t MIN_I = std::numeric_limits<int32_t>::min();

        ASSERT_EQ(vector_to_direction(MAX_I, 0), Direction::East);
        ASSERT_EQ(vector_to_direction(MIN_I, 0), Direction::West);
        ASSERT_EQ(vector_to_direction(0, MAX_I), Direction::South);
        ASSERT_EQ(vector_to_direction(0, MIN_I), Direction::North);

        // Diagonals at int32 extremes
        ASSERT_EQ(vector_to_direction(MAX_I / 2, MAX_I / 2), Direction::SouthEast);
        ASSERT_EQ(vector_to_direction(MIN_I / 2, MIN_I / 2), Direction::NorthWest);
        ASSERT_EQ(vector_to_direction(MIN_I / 2, MAX_I / 2), Direction::SouthWest);
        ASSERT_EQ(vector_to_direction(MAX_I / 2, MIN_I / 2), Direction::NorthEast);
    } CHALLENGE_END();

    CHALLENGE_CASE("1.5 Direction mapping invariants, aliases and bitmask safety") {
        // Verify DIRECTION_MAP consistency for all 8 headings
        const uint8_t expected_code[8] = { 7, 8, 9, 2, 3, 2, 9, 8 };
        const bool expected_mirrored[8] = { false, false, false, false, false, true, true, true };
        const uint8_t expected_base[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };

        for (uint8_t d = 0; d < 8; ++d) {
            Direction dir = static_cast<Direction>(d);
            DirectionMapping m = get_direction_mapping(dir);
            ASSERT_EQ(m.chd_code, expected_code[d]);
            ASSERT_EQ(m.mirrored, expected_mirrored[d]);
            ASSERT_EQ(m.base_index, expected_base[d]);

            // Test Direction8 compatibility alias
            Direction8 dir8 = static_cast<Direction8>(d);
            DirectionMapping m8 = get_direction_mapping(dir8);
            ASSERT_EQ(m8.chd_code, m.chd_code);
            ASSERT_EQ(m8.mirrored, m.mirrored);
            ASSERT_EQ(m8.base_index, m.base_index);
        }

        // Bitmask safety (& 7 prevents buffer overrun on out-of-range enums)
        DirectionMapping m_oob1 = get_direction_mapping(static_cast<Direction>(8));
        ASSERT_EQ(m_oob1.chd_code, 7u); // 8 & 7 == 0 (North)
        DirectionMapping m_oob2 = get_direction_mapping(static_cast<Direction>(255));
        ASSERT_EQ(m_oob2.chd_code, 8u); // 255 & 7 == 7 (NorthWest)
    } CHALLENGE_END();
}

// ============================================================================
// SUITE 2: Involution & Horizontal Reflection Mathematics Stress Suite
// ============================================================================
void challenge_suite_2_involution_math() {
    CHALLENGE_SUITE("Suite 2: Involution & Offset Math dx'=-(dx+W)");

    CHALLENGE_CASE("2.1 Rigorous involution proof across wide range of dx and W") {
        // Check mirror_dx(mirror_dx(dx, W), W) == dx across comprehensive Cartesian domain
        for (int32_t dx = -1000; dx <= 1000; dx += 19) {
            for (uint32_t W = 0; W <= 1000; W += 23) {
                int32_t dx1 = mirror_dx(dx, W);
                ASSERT_EQ(dx1, -(dx + static_cast<int32_t>(W)));

                int32_t dx2 = mirror_dx(dx1, W);
                ASSERT_EQ(dx2, dx); // Mathematical involution guaranteed
            }
        }

        // Boundary cases: dx = 0, W = 0
        ASSERT_EQ(mirror_dx(0, 0), 0);
        ASSERT_EQ(mirror_dx(mirror_dx(0, 0), 0), 0);

        // dx = -W
        ASSERT_EQ(mirror_dx(-40, 40), 0);
        ASSERT_EQ(mirror_dx(0, 40), -40);

        // Large numbers
        constexpr int32_t LARGE_DX = 1000000;
        constexpr uint32_t LARGE_W = 50000;
        int32_t m1 = mirror_dx(LARGE_DX, LARGE_W);
        ASSERT_EQ(m1, -1050000);
        ASSERT_EQ(mirror_dx(m1, LARGE_W), LARGE_DX);
    } CHALLENGE_END();

    CHALLENGE_CASE("2.2 Vertical offset dy invariance") {
        for (int32_t dy = -2000; dy <= 2000; dy += 17) {
            ASSERT_EQ(mirror_dy(dy), dy);
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("2.3 Bounding box involution and interval ordering preservation") {
        for (int32_t left = -500; left <= 500; left += 25) {
            for (int32_t width = 0; width <= 500; width += 30) {
                int32_t right = left + width;
                int32_t l1 = 0, r1 = 0;
                mirror_bounding_box(left, right, l1, r1);

                // [-right, -left]
                ASSERT_EQ(l1, -right);
                ASSERT_EQ(r1, -left);
                ASSERT_LE(l1, r1); // Interval ordering is strictly preserved

                int32_t l2 = 0, r2 = 0;
                mirror_bounding_box(l1, r1, l2, r2);
                ASSERT_EQ(l2, left);
                ASSERT_EQ(r2, right);
            }
        }
    } CHALLENGE_END();
}

// ============================================================================
// SUITE 3: Pixel Buffer Mirroring & Authentic Table 1 Involution Oracle
// ============================================================================
void challenge_suite_3_pixel_mirroring(const std::string& chd_path) {
    CHALLENGE_SUITE("Suite 3: Pixel Buffer Mirroring & Table 1 Sprites Involution");

    CHALLENGE_CASE("3.1 Adversarial corrupted inputs to mirror_pixel_buffer") {
        uint8_t buf[16] = { 1, 2, 3, 4 };
        uint8_t out[16] = { 0 };

        // Null source
        mirror_pixel_buffer(nullptr, out, 4, 4, 4);
        // Null dest
        mirror_pixel_buffer(buf, nullptr, 4, 4, 4);
        // Width == 0
        mirror_pixel_buffer(buf, out, 0, 4, 4);
        // Height == 0
        mirror_pixel_buffer(buf, out, 4, 0, 4);
        // Pitch < Width (illegal)
        mirror_pixel_buffer(buf, out, 8, 4, 4);

        // Verify out buffer remained untouched (all zeros)
        for (size_t i = 0; i < sizeof(out); ++i) {
            ASSERT_EQ(out[i], 0u);
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("3.2 Synthetic stride padding and involution test") {
        const uint32_t test_widths[]  = { 1, 2, 3, 5, 8, 13, 21, 32, 63, 64 };
        const uint32_t test_heights[] = { 1, 2, 7, 16 };

        for (uint32_t w : test_widths) {
            for (uint32_t h : test_heights) {
                for (uint32_t extra_pad : { 0u, 1u, 3u, 7u, 16u }) {
                    uint32_t pitch = w + extra_pad;
                    size_t buf_size = static_cast<size_t>(pitch) * h;

                    std::vector<uint8_t> src(buf_size, 0);
                    std::vector<uint8_t> flipped(buf_size, 0xFF); // Fill with dirty 0xFF
                    std::vector<uint8_t> restored(buf_size, 0xEE);

                    // Fill src with deterministic test pattern
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            src[y * pitch + x] = static_cast<uint8_t>((y * 37 + x * 13 + 5) & 0xFF);
                        }
                    }

                    // First flip
                    mirror_pixel_buffer(src.data(), flipped.data(), w, h, pitch);

                    // Check flipped content and padding clearance
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            ASSERT_EQ(flipped[y * pitch + x], src[y * pitch + (w - 1 - x)]);
                        }
                        // Check that stride padding was cleared to 0x00
                        for (uint32_t p = w; p < pitch; ++p) {
                            ASSERT_EQ(flipped[y * pitch + p], 0x00);
                        }
                    }

                    // Second flip (involution)
                    mirror_pixel_buffer(flipped.data(), restored.data(), w, h, pitch);

                    // Verify exact restoration of visible pixels and padding
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            ASSERT_EQ(restored[y * pitch + x], src[y * pitch + x]);
                        }
                        for (uint32_t p = w; p < pitch; ++p) {
                            ASSERT_EQ(restored[y * pitch + p], 0x00);
                        }
                    }
                }
            }
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("3.3 Exhaustive Table 1 sprites (2,794) double-flip involution oracle") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_EQ(archive.sprite_count(), 2794u);

        for (uint32_t id = 0; id < 2794; ++id) {
            const Sprite& orig = archive.get_sprite(id);
            const Sprite& mirr = archive.get_mirrored_sprite(id);

            ASSERT_EQ(mirr.width, orig.width);
            ASSERT_EQ(mirr.height, orig.height);
            ASSERT_EQ(mirr.pitch, orig.pitch);
            ASSERT_EQ(mirr.pixels.size(), orig.pixels.size());

            // Double flip into roundtrip
            Sprite roundtrip = mirr.create_horizontal_flip(id);
            ASSERT_EQ(roundtrip.pixels.size(), orig.pixels.size());

            // Check byte-for-byte pixel identity across all rows and columns
            for (uint32_t y = 0; y < orig.height; ++y) {
                for (uint32_t x = 0; x < orig.width; ++x) {
                    uint8_t orig_px = orig.get_pixel(x, y);
                    uint8_t mirr_px = mirr.get_pixel(orig.width - 1 - x, y);
                    uint8_t rtri_px = roundtrip.get_pixel(x, y);

                    ASSERT_EQ(mirr_px, orig_px);
                    ASSERT_EQ(rtri_px, orig_px);
                }
            }

            // Directional sprite API check
            // For headings 0..4 (N, NE, E, SE, S): should return original sprite
            for (uint8_t d = 0; d <= 4; ++d) {
                const Sprite& s = archive.get_directional_sprite(id, static_cast<Direction>(d));
                ASSERT_EQ(&s, &orig);
            }
            // For headings 5..7 (SW, W, NW): should return mirrored sprite
            for (uint8_t d = 5; d <= 7; ++d) {
                const Sprite& s = archive.get_directional_sprite(id, static_cast<Direction>(d));
                ASSERT_EQ(&s, &mirr);
            }
        }
    } CHALLENGE_END();
}

// ============================================================================
// SUITE 4: Directional Animation Mirroring & Lunchbox Suite
// ============================================================================
void challenge_suite_4_animations_and_lunchbox(const std::string& chd_path) {
    CHALLENGE_SUITE("Suite 4: Directional Animation Mirroring & Lunchbox Suite");

    CHALLENGE_CASE("4.1 Directional animation frame arithmetic across real assets") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Test known directional ant action animation prefixes:
        // agwg (worker walk), afwg (fire walk), abwg (bomber walk), acwg (combat walk), atwg (thief walk),
        // agfg (worker fight), acfg (combat fight)
        const std::vector<std::string> prefixes = {
            "agwg", "afwg", "abwg", "acwg", "atwg",
            "agfg", "acfg"
        };

        // DIAGNOSTIC PRINT: check anim prefixes
        std::cout << "\n    [DEBUG] Checking animation prefixes...\n";
        for (const auto& prefix : prefixes) {
            const AnimationSequence* a = archive.get_directional_animation(prefix, Direction::East);
            std::cout << "      prefix " << prefix << " -> " << (a ? a->name : "NULL") << "\n";
        }

        for (const auto& prefix : prefixes) {
            const AnimationSequence* anim_e = archive.get_directional_animation(prefix, Direction::East);
            const AnimationSequence* anim_w = archive.get_directional_animation(prefix, Direction::West);

            if (!anim_e) continue;
            ASSERT_NE(anim_w, nullptr);

            ASSERT_EQ(anim_w->subitems.size(), anim_e->subitems.size());
            for (size_t s = 0; s < anim_e->subitems.size(); ++s) {
                const auto& sub_e = anim_e->subitems[s];
                const auto& sub_w = anim_w->subitems[s];

                ASSERT_EQ(sub_w.box_left, -sub_e.box_right);
                ASSERT_EQ(sub_w.box_right, -sub_e.box_left);

                ASSERT_EQ(sub_w.frames.size(), sub_e.frames.size());
                for (size_t f = 0; f < sub_e.frames.size(); ++f) {
                    const auto& fe = sub_e.frames[f];
                    const auto& fw = sub_w.frames[f];
                    uint32_t spr_w = archive.get_sprite(fe.sprite_index).width;

                    ASSERT_EQ(fw.dx, -(fe.dx + static_cast<int32_t>(spr_w)));
                    ASSERT_EQ(fw.dy, fe.dy);
                    ASSERT_EQ(fw.sprite_index, fe.sprite_index);
                }
            }

            // NorthEast (1, code 8) and NorthWest (7, mirrored from 8)
            const AnimationSequence* anim_ne = archive.get_directional_animation(prefix, Direction::NorthEast);
            const AnimationSequence* anim_nw = archive.get_directional_animation(prefix, Direction::NorthWest);

            ASSERT_NE(anim_ne, nullptr);
            ASSERT_NE(anim_nw, nullptr);

            ASSERT_EQ(anim_nw->subitems.size(), anim_ne->subitems.size());
            for (size_t s = 0; s < anim_ne->subitems.size(); ++s) {
                const auto& sub_ne = anim_ne->subitems[s];
                const auto& sub_nw = anim_nw->subitems[s];

                ASSERT_EQ(sub_nw.box_left, -sub_ne.box_right);
                ASSERT_EQ(sub_nw.box_right, -sub_ne.box_left);

                ASSERT_EQ(sub_nw.frames.size(), sub_ne.frames.size());
                for (size_t f = 0; f < sub_ne.frames.size(); ++f) {
                    const auto& fne = sub_ne.frames[f];
                    const auto& fnw = sub_nw.frames[f];
                    uint32_t spr_w = archive.get_sprite(fne.sprite_index).width;

                    ASSERT_EQ(fnw.dx, -(fne.dx + static_cast<int32_t>(spr_w)));
                    ASSERT_EQ(fnw.dy, fne.dy);
                }
            }

            // SouthEast (3, code 2) and SouthWest (5, mirrored from 2)
            const AnimationSequence* anim_se = archive.get_directional_animation(prefix, Direction::SouthEast);
            const AnimationSequence* anim_sw = archive.get_directional_animation(prefix, Direction::SouthWest);

            ASSERT_NE(anim_se, nullptr);
            ASSERT_NE(anim_sw, nullptr);

            ASSERT_EQ(anim_sw->subitems.size(), anim_se->subitems.size());
            for (size_t s = 0; s < anim_se->subitems.size(); ++s) {
                const auto& sub_se = anim_se->subitems[s];
                const auto& sub_sw = anim_sw->subitems[s];

                ASSERT_EQ(sub_sw.box_left, -sub_se.box_right);
                ASSERT_EQ(sub_sw.box_right, -sub_se.box_left);

                ASSERT_EQ(sub_sw.frames.size(), sub_se.frames.size());
                for (size_t f = 0; f < sub_se.frames.size(); ++f) {
                    const auto& fse = sub_se.frames[f];
                    const auto& fsw = sub_sw.frames[f];
                    uint32_t spr_w = archive.get_sprite(fse.sprite_index).width;

                    ASSERT_EQ(fsw.dx, -(fse.dx + static_cast<int32_t>(spr_w)));
                    ASSERT_EQ(fsw.dy, fse.dy);
                }
            }
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("4.2 Lunchbox directional prefixes and real asset existence") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Invariant prefix mapping for all 8 compass directions
        const char* expected_prefix[8] = { "7lb", "6lb", "5lb", "4lb", "3lb", "4lb", "5lb", "6lb" };
        const bool expected_mirrored[8] = { false, false, false, false, false, true, true, true };

        for (uint8_t d = 0; d < 8; ++d) {
            Direction dir = static_cast<Direction>(d);
            ASSERT_EQ(std::string(get_lunchbox_prefix(dir)), std::string(expected_prefix[d]));
            ASSERT_EQ(is_lunchbox_mirrored(dir), expected_mirrored[d]);

            Direction8 dir8 = static_cast<Direction8>(d);
            ASSERT_EQ(std::string(get_lunchbox_prefix(dir8)), std::string(expected_prefix[d]));
            ASSERT_EQ(is_lunchbox_mirrored(dir8), expected_mirrored[d]);
        }

        // Verify lunchbox physical animation and directional sprite suites
        const AnimationSequence* anim_lb = archive.find_animation("lunchbox");
        ASSERT_NE(anim_lb, nullptr);
        ASSERT_EQ(anim_lb->id, 356u);

        // Verify directional sprite existence for all prefixes:
        // North (0): 7lb0000.bmp
        // NorthEast (1): 6lb0000.bmp
        // East (2): 5lb0000.bmp
        // SouthEast (3): 4lb0000.bmp
        // South (4): 3lb0000.bmp
        ASSERT_NE(archive.find_sprite("7lb0000.bmp"), nullptr);
        ASSERT_NE(archive.find_sprite("6lb0000.bmp"), nullptr);
        ASSERT_NE(archive.find_sprite("5lb0000.bmp"), nullptr);
        ASSERT_NE(archive.find_sprite("4lb0000.bmp"), nullptr);
        ASSERT_NE(archive.find_sprite("3lb0000.bmp"), nullptr);
        ASSERT_NE(archive.find_sprite("3lb0001.bmp"), nullptr); // Sprite 513

        // Verify horizontal mirroring on lunchbox sprites
        for (const std::string& spr_name : { "7lb0000.bmp", "6lb0000.bmp", "5lb0000.bmp", "4lb0000.bmp", "3lb0000.bmp" }) {
            const Sprite* spr = archive.find_sprite(spr_name);
            ASSERT_NE(spr, nullptr);
            const Sprite& mirr = archive.get_mirrored_sprite(spr->id);
            ASSERT_EQ(mirr.width, spr->width);
            ASSERT_EQ(mirr.height, spr->height);
            // Verify involution
            Sprite roundtrip = mirr.create_horizontal_flip(spr->id);
            for (uint32_t y = 0; y < spr->height; ++y) {
                for (uint32_t x = 0; x < spr->width; ++x) {
                    ASSERT_EQ(roundtrip.get_pixel(x, y), spr->get_pixel(x, y));
                }
            }
        }

    } CHALLENGE_END();
}

// ============================================================================
// SUITE 5: Level Decoder Exact Decoding & Coordinate Serialization Invariants
// ============================================================================
void challenge_suite_5_level_coordinates(const std::string& map_dir) {
    CHALLENGE_SUITE("Suite 5: Level Decoder Exact 0-Remaining & Coordinate Invariants");

    const std::vector<std::string> map_files = {
        "TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "ISLANDS.LVL", "TREASURE.LVL"
    };

    for (const auto& map_file : map_files) {
        std::string full_path = map_dir + "/" + map_file;
        CHALLENGE_CASE("5. " + map_file + " Exact 0-Rem & Coordinate Bounds Validation") {
            LevelData map;
            ASSERT_TRUE(map.load_from_file(full_path));

            // Fixed header checks
            ASSERT_EQ(map.version, 8u);
            ASSERT_EQ(map.game_mode, 1u);
            ASSERT_GT(map.width, 0u);
            ASSERT_GT(map.height, 0u);
            ASSERT_FALSE(map.description.empty());
            ASSERT_GT(map.tile_dictionary.size(), 0u);

            size_t cell_count = static_cast<size_t>(map.width) * map.height;
            ASSERT_EQ(map.layer1_terrain.size(), cell_count);
            ASSERT_EQ(map.layer2_interactive.size(), cell_count);

            // Layer 1 validation: every tile index must be within dictionary bounds
            for (uint32_t y = 0; y < map.height; ++y) {
                for (uint32_t x = 0; x < map.width; ++x) {
                    const MapCell& c1 = map.get_cell_layer1(x, y);
                    ASSERT_LT(c1.tile_index, map.tile_dictionary.size());

                    const MapCell& c2 = map.get_cell_layer2(x, y);
                    if (!c2.is_empty()) {
                        ASSERT_LT(c2.tile_index, map.tile_dictionary.size());
                    }
                }
            }

            // ================================================================
            // Trailing Block 1: Anthill Starting Spawns
            // ================================================================
            ASSERT_GT(map.anthill_spawns.size(), 0u);
            std::array<int, 4> team_spawn_counts = {0, 0, 0, 0};

            for (size_t i = 0; i < map.anthill_spawns.size(); ++i) {
                const AnthillSpawn& sp = map.anthill_spawns[i];
                // Coordinate bounds check
                ASSERT_LT(sp.x, map.width);
                ASSERT_LT(sp.y, map.height);
                // Dictionary index check
                ASSERT_LT(sp.tile_id, map.tile_dictionary.size());
                
                const std::string& tname = map.get_tile_name(sp.tile_id);
                // Valid team ID is 0..3 for base spawns, or 255 for non-team map spawns (flowers, clovers)
                ASSERT_TRUE(sp.team_id <= 3u || sp.team_id == 255u);
                if (sp.team_id <= 3u) {
                    team_spawn_counts[sp.team_id]++;
                    ASSERT_NE(tname.find("START"), std::string::npos);
                }
            }

            // Every 4-player map must feature base spawns for all 4 teams
            for (size_t t = 0; t < 4; ++t) {
                ASSERT_GT(team_spawn_counts[t], 0);
            }

            // ================================================================
            // Trailing Block 2: Food Respawn Pools
            // ================================================================
            ASSERT_GT(map.food_schedules.size(), 0u);
            for (size_t i = 0; i < map.food_schedules.size(); ++i) {
                const FoodSchedule& fs = map.food_schedules[i];
                // Coordinate bounds check
                ASSERT_LT(fs.x, map.width);
                ASSERT_LT(fs.y, map.height);
                // Variant pool check
                ASSERT_GT(fs.variants.size(), 0u);

                uint32_t total_weight = 0;
                for (size_t v = 0; v < fs.variants.size(); ++v) {
                    const auto& var = fs.variants[v];
                    // Weight is non-negative; empty variants can have weight 0
                    total_weight += var.weight;
                    if (var.tile_id != LVL_EMPTY_TILE) {
                        ASSERT_LT(var.tile_id, map.tile_dictionary.size());
                    }
                }
                ASSERT_GT(total_weight, 0u);
            }

            // ================================================================
            // Trailing Block 3: Ambient Parameters
            // ================================================================
            ASSERT_EQ(map.ambient_flag, 0u);
            ASSERT_EQ(map.ambient_tile_or_sound, LVL_EMPTY_TILE);

            // ================================================================
            // Trailing Block 4: Waypoints
            // ================================================================
            for (size_t i = 0; i < map.waypoints.size(); ++i) {
                const Waypoint& wp = map.waypoints[i];
                // Coordinate bounds check
                ASSERT_LT(wp.x, map.width);
                ASSERT_LT(wp.y, map.height);

                if (wp.flag != 0) {
                    double prob_sum = 0.0;
                    for (double p : wp.probabilities) {
                        ASSERT_GE(p, 0.0);
                        ASSERT_LE(p, 1.0);
                        prob_sum += p;
                    }
                    ASSERT_NEAR(prob_sum, 1.0, 1e-4);
                } else {
                    for (double p : wp.probabilities) {
                        ASSERT_EQ(p, 0.0);
                    }
                }
            }

            // ================================================================
            // Boundary Parameter (f_last)
            // ================================================================
            ASSERT_GT(map.boundary_param, 0u);
        } CHALLENGE_END();
    }
}

// ============================================================================
// SUITE 6: Level Decoder Corrupted Buffer & Fuzzing Stress Harness
// ============================================================================
void challenge_suite_6_level_fuzzing(const std::string& map_dir) {
    CHALLENGE_SUITE("Suite 6: Level Decoder Corrupted Buffers & Fuzzing Defense");

    CHALLENGE_CASE("6.1 Under-sized buffers (< 42 bytes) rejection") {
        LevelData lvl;
        ASSERT_FALSE(lvl.load_from_memory(nullptr, 0));
        ASSERT_FALSE(lvl.load_from_memory(nullptr, 100));

        std::vector<uint8_t> dummy(41, 0);
        for (size_t s = 0; s < 42; ++s) {
            ASSERT_FALSE(lvl.load_from_memory(dummy.data(), s));
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("6.2 Truncated valid level at every critical structural boundary") {
        std::string tiny_path = map_dir + "/TINY.LVL";
        std::ifstream file(tiny_path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file.is_open());
        size_t full_size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> full_data(full_size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(full_data.data()), static_cast<std::streamsize>(full_size));

        LevelData lvl;
        // Verify original loads cleanly
        ASSERT_TRUE(lvl.load_from_memory(full_data.data(), full_size));

        // Test truncations at structural markers:
        const size_t truncation_points[] = {
            42,      // Header only
            50,      // Partial first tile name
            100,     // Partial dictionary
            7000,    // Inside dictionary
            7420,    // Dimensions only
            7430,    // Beginning of Layer 1
            10000,   // Middle of Layer 1
            13200,   // Middle of Layer 2
            15000,   // Beginning of Block 1
            16000,   // Middle of Block 1
            17000,   // Block 2
            18000,   // Block 3
            full_size - 3, // 1 byte before f_last
            full_size - 2, // At f_last
            full_size - 1  // 1 byte missing from f_last
        };

        for (size_t trunc_size : truncation_points) {
            if (trunc_size < full_size) {
                ASSERT_FALSE(lvl.load_from_memory(full_data.data(), trunc_size));
            }
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("6.3 Corrupted header fields (version, game mode, zero dimensions)") {
        std::string tiny_path = map_dir + "/TINY.LVL";
        std::ifstream file(tiny_path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file.is_open());
        size_t full_size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> base_data(full_size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(base_data.data()), static_cast<std::streamsize>(full_size));

        LevelData lvl;

        // Invalid version != 8
        {
            auto bad = base_data;
            uint32_t bad_ver = 7;
            std::memcpy(bad.data(), &bad_ver, 4);
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));

            bad_ver = 9;
            std::memcpy(bad.data(), &bad_ver, 4);
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));

            bad_ver = 0;
            std::memcpy(bad.data(), &bad_ver, 4);
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));
        }

        // Invalid game mode != 1
        {
            auto bad = base_data;
            uint32_t bad_mode = 0;
            std::memcpy(bad.data() + 4, &bad_mode, 4);
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));

            bad_mode = 2;
            std::memcpy(bad.data() + 4, &bad_mode, 4);
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));
        }

        // Zero dimensions
        {
            // Find dimensions offset: 42 + (tile_type_count + 1) * 11
            uint16_t ttc = static_cast<uint16_t>(base_data[40] | (base_data[41] << 8));
            size_t dim_offset = 42 + (static_cast<size_t>(ttc) + 1) * 11;

            auto bad = base_data;
            uint32_t zero = 0;
            std::memcpy(bad.data() + dim_offset, &zero, 4); // width = 0
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));

            bad = base_data;
            std::memcpy(bad.data() + dim_offset + 4, &zero, 4); // height = 0
            ASSERT_FALSE(lvl.load_from_memory(bad.data(), bad.size()));
        }
    } CHALLENGE_END();

    CHALLENGE_CASE("6.4 Strict 0-residual rejection: trailing garbage appended") {
        std::string tiny_path = map_dir + "/TINY.LVL";
        std::ifstream file(tiny_path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file.is_open());
        size_t full_size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> base_data(full_size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(base_data.data()), static_cast<std::streamsize>(full_size));

        LevelData lvl;
        ASSERT_TRUE(lvl.load_from_memory(base_data.data(), full_size));

        // Append single trailing byte (0x00) -> must reject
        auto data_plus_1 = base_data;
        data_plus_1.push_back(0x00);
        ASSERT_FALSE(lvl.load_from_memory(data_plus_1.data(), data_plus_1.size()));

        // Append 4 trailing bytes -> must reject
        auto data_plus_4 = base_data;
        data_plus_4.insert(data_plus_4.end(), { 0xDE, 0xAD, 0xBE, 0xEF });
        ASSERT_FALSE(lvl.load_from_memory(data_plus_4.data(), data_plus_4.size()));

        // Append 100 trailing bytes -> must reject
        auto data_plus_100 = base_data;
        data_plus_100.resize(full_size + 100, 0xAA);
        ASSERT_FALSE(lvl.load_from_memory(data_plus_100.data(), data_plus_100.size()));
    } CHALLENGE_END();

    CHALLENGE_CASE("6.5 Corrupted dimensions memory exhaustion vulnerability test") {
        std::string tiny_path = map_dir + "/TINY.LVL";
        std::ifstream file(tiny_path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file.is_open());
        size_t full_size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> base_data(full_size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(base_data.data()), static_cast<std::streamsize>(full_size));

        // Locate dimensions offset in TINY.LVL: 42 + (tile_type_count + 1) * 11
        uint16_t ttc = static_cast<uint16_t>(base_data[40] | (base_data[41] << 8));
        size_t dim_offset = 42 + (static_cast<size_t>(ttc) + 1) * 11;

        // Corrupted dimensions: width = 500, height = 500 (requires 3,000,000 bytes, buffer has only 19,312 bytes)
        auto bad1 = base_data;
        uint32_t bad_dim = 500;
        std::memcpy(bad1.data() + dim_offset, &bad_dim, 4);
        std::memcpy(bad1.data() + dim_offset + 4, &bad_dim, 4);

        LevelData lvl;
        bool res = lvl.load_from_memory(bad1.data(), bad1.size());
        ASSERT_FALSE(res);

        // A robust parser must NOT allocate 250,000 MapCells (1.5 MB) when the buffer is only 19 KB!
        // If cell_count * 12 > remaining, it should reject before resizing layer1_terrain
        size_t allocated_cells = lvl.layer1_terrain.size();
        if (allocated_cells > 0) {
            std::cout << "\n    [VULNERABILITY CONFIRMED] LVLParser allocated " << allocated_cells
                      << " cells (" << (allocated_cells * sizeof(MapCell))
                      << " bytes) on a 19 KB buffer before failing!\n";
        }
        ASSERT_EQ(allocated_cells, 0u);
    } CHALLENGE_END();

    CHALLENGE_CASE("6.6 Deterministic pseudo-random mutation fuzzing across all 6 maps") {
        const std::vector<std::string> map_files = {
            "TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "ISLANDS.LVL", "TREASURE.LVL"
        };

        std::mt19937 rng(1337); // Fixed seed for 100% deterministic reproducibility
        int caught_exceptions = 0;

        for (const auto& mf : map_files) {
            std::string p = map_dir + "/" + mf;
            std::ifstream file(p, std::ios::binary | std::ios::ate);
            ASSERT_TRUE(file.is_open());
            size_t size = static_cast<size_t>(file.tellg());
            std::vector<uint8_t> orig(size);
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(orig.data()), static_cast<std::streamsize>(size));

            LevelData lvl;

            // 100 fuzz mutations per map
            for (int iter = 0; iter < 100; ++iter) {
                std::vector<uint8_t> mutant = orig;
                uint32_t mutation_type = rng() % 4;

                switch (mutation_type) {
                    case 0: { // Bit flip
                        size_t pos = rng() % mutant.size();
                        uint8_t bit = static_cast<uint8_t>(1 << (rng() % 8));
                        mutant[pos] ^= bit;
                        break;
                    }
                    case 1: { // Byte overwrite with random or sentinel
                        size_t pos = rng() % mutant.size();
                        uint8_t vals[] = { 0x00, 0xFF, 0xFE, 0x7F, static_cast<uint8_t>(rng() & 0xFF) };
                        mutant[pos] = vals[rng() % 5];
                        break;
                    }
                    case 2: { // Random multi-byte corruption
                        size_t len = 1 + (rng() % 32);
                        size_t pos = rng() % (mutant.size() - len);
                        for (size_t k = 0; k < len; ++k) {
                            mutant[pos + k] = static_cast<uint8_t>(rng() & 0xFF);
                        }
                        break;
                    }
                    case 3: { // Slice truncation
                        size_t cut = 42 + (rng() % (mutant.size() - 42));
                        mutant.resize(cut);
                        break;
                    }
                }

                // Check dimensions in mutant before calling to avoid 400GB zero-fill hang
                uint16_t ttc = static_cast<uint16_t>(mutant[40] | (mutant[41] << 8));
                size_t dim_off = 42 + (static_cast<size_t>(ttc) + 1) * 11;
                if (dim_off + 8 <= mutant.size()) {
                    uint32_t mw = static_cast<uint32_t>(mutant[dim_off] | (mutant[dim_off + 1] << 8) | (mutant[dim_off + 2] << 16) | (mutant[dim_off + 3] << 24));
                    uint32_t mh = static_cast<uint32_t>(mutant[dim_off + 4] | (mutant[dim_off + 5] << 8) | (mutant[dim_off + 6] << 16) | (mutant[dim_off + 7] << 24));
                    if (static_cast<uint64_t>(mw) * mh > 2000000ULL) {
                        // Skip mutations that cause multi-gigabyte OS zero-fill hangs
                        continue;
                    }
                }

                try {
                    lvl.load_from_memory(mutant.data(), mutant.size());
                } catch (const std::exception&) {
                    ++caught_exceptions;
                }
            }
        }

        if (caught_exceptions > 0) {
            std::cout << "\n    [VULNERABILITY CONFIRMED] Fuzz mutations triggered " << caught_exceptions
                      << " uncaught exceptions in LVLParser!\n";
        }
        ASSERT_EQ(caught_exceptions, 0);
    } CHALLENGE_END();
}

// ============================================================================
// Main Challenger Runner
// ============================================================================
int main() {
    std::cout << "=======================================================\n"
              << " Ants Challenger 2 Adversarial Test Suite\n"
              << " Target: libants_assets (5-to-8 Mirroring & Level Decoder)\n"
              << "=======================================================\n";

    std::string assets_dir = locate_assets_dir();
    std::string chd_path = assets_dir + "/ants.chd";
    std::string map_dir = assets_dir + "/Maps";

    std::cout << "Target Assets Directory: " << assets_dir << "\n";
    std::cout << "Target CHD Path:         " << chd_path << "\n";
    std::cout << "Target Maps Directory:   " << map_dir << "\n";

    if (!fs::exists(chd_path) || !fs::exists(map_dir)) {
        std::cerr << "ERROR: Assets directory missing!\n";
        return 1;
    }

    challenge_suite_1_headings();
    challenge_suite_2_involution_math();
    challenge_suite_3_pixel_mirroring(chd_path);
    challenge_suite_4_animations_and_lunchbox(chd_path);
    challenge_suite_5_level_coordinates(map_dir);
    challenge_suite_6_level_fuzzing(map_dir);

    std::cout << "\n=======================================================\n"
              << " CHALLENGE TEST SUMMARY\n"
              << " Total Challenge Cases: " << g_test_count << "\n"
              << " Total Assertions:      " << g_assert_count << "\n"
              << " Failures:              " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL CHALLENGE TEST SUITES PASSED CLEANLY (100% PASS) <<<\n";
        return 0;
    } else {
        std::cerr << " >>> CHALLENGE HARNESS DETECTED " << g_test_failures << " FAILURES <<<\n";
        return 1;
    }
}
