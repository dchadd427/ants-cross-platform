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
#include <functional>
#include <thread>
#include <atomic>
#include <random>

namespace fs = std::filesystem;
using namespace ants::assets;

// ============================================================================
// Lightweight Adversarial Test Harness
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [ADVERSARIAL SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    try {
        fn();
        if (g_test_failures == prev_fails) {
            std::cout << "PASS\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "CRASHED/THREW: " << ex.what() << "\n";
        ++g_test_failures;
    } catch (...) {
        std::cout << "CRASHED WITH UNKNOWN EXCEPTION!\n";
        ++g_test_failures;
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

#define ASSERT_GE(a, b) \
    do { \
        ++g_assert_count; \
        if (!((a) >= (b))) { \
            std::cout << "FAILED!\n    Assertion failed: " #a " >= " #b \
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

#define ASSERT_NEAR(a, b, eps) \
    do { \
        ++g_assert_count; \
        if (std::fabs((a) - (b)) > (eps)) { \
            std::cout << "FAILED!\n    Assertion failed: |" #a " - " #b "| <= " #eps \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

static std::string locate_assets_dir() {
    std::vector<std::string> candidates = {
#ifdef ORIGINAL_ASSETS_DIR
        ORIGINAL_ASSETS_DIR,
#endif
        "Original-Ants",
        "../Original-Ants",
        "../../Original-Ants",
        "../../../Original-Ants",
        "/Users/dchadd/Desktop/Ants-Mac/Original-Ants"
    };

    for (const auto& path : candidates) {
        if (fs::exists(path) && fs::exists(path + "/ants.chd")) {
            return path;
        }
    }
    return "Original-Ants";
}

// ============================================================================
// SUITE 1: Exhaustive Verification of all 2,794 Sprites (RGBA32 & Mirroring)
// ============================================================================
void test_suite_1_exhaustive_sprites(const std::string& chd_path) {
    TEST_SUITE("Suite 1: Exhaustive Verification of all 2,794 Sprites (RGBA32 & Mirroring)");

    AssetArchive archive;
    ASSERT_TRUE(archive.load_from_file(chd_path));
    ASSERT_EQ(archive.sprite_count(), 2794u);
    const auto& pal = archive.get_palette();

    TEST_CASE("1.1 All 2,794 Sprites Produce Valid 32-Bit RGBA Buffers & Non-Unaligned Reads") {
        for (uint32_t i = 0; i < 2794; ++i) {
            const Sprite& sp = archive.get_sprite(i);
            ASSERT_EQ(sp.id, i);
            ASSERT_GT(sp.width, 0u);
            ASSERT_GT(sp.height, 0u);
            ASSERT_GE(sp.pitch, sp.width);
            ASSERT_EQ(sp.pixels.size(), static_cast<size_t>(sp.pitch) * sp.height);

            // Alignment check: pixels buffer must be properly aligned
            ASSERT_TRUE(reinterpret_cast<uintptr_t>(sp.pixels.data()) % alignof(uint8_t) == 0);

            // Generate 32-bit tightly-packed RGBA buffer
            std::vector<uint8_t> rgba = sp.to_rgba32(pal);
            size_t expected_size = static_cast<size_t>(sp.width) * sp.height * 4;
            ASSERT_EQ(rgba.size(), expected_size);

            // Pointer alignment check for RGBA buffer (4-byte alignment standard for 32-bit pixels)
            ASSERT_TRUE(reinterpret_cast<uintptr_t>(rgba.data()) % alignof(uint32_t) == 0);

            // Generate ColorRGBA vector
            std::vector<ColorRGBA> colors = sp.to_rgba(pal);
            ASSERT_EQ(colors.size(), static_cast<size_t>(sp.width) * sp.height);

            // Pixel verification: check corners, center, and sample points
            // (1) Top-Left (0, 0)
            uint8_t p_tl = sp.get_pixel(0, 0);
            ColorRGBA c_tl = pal[p_tl];
            ASSERT_EQ(rgba[0], c_tl.r);
            ASSERT_EQ(rgba[1], c_tl.g);
            ASSERT_EQ(rgba[2], c_tl.b);
            ASSERT_EQ(rgba[3], c_tl.a);
            ASSERT_EQ(colors[0], c_tl);

            // (2) Bottom-Right (width-1, height-1)
            uint32_t br_x = sp.width - 1;
            uint32_t br_y = sp.height - 1;
            uint8_t p_br = sp.get_pixel(br_x, br_y);
            ColorRGBA c_br = pal[p_br];
            size_t br_idx = (static_cast<size_t>(br_y) * sp.width + br_x) * 4;
            ASSERT_EQ(rgba[br_idx + 0], c_br.r);
            ASSERT_EQ(rgba[br_idx + 1], c_br.g);
            ASSERT_EQ(rgba[br_idx + 2], c_br.b);
            ASSERT_EQ(rgba[br_idx + 3], c_br.a);

            // (3) Alpha check: pure transparency key (254) must have Alpha == 0
            if (p_tl == CHD_COLOR_KEY_INDEX) {
                ASSERT_EQ(rgba[3], 0u);
            } else {
                ASSERT_EQ(rgba[3], 255u);
            }
        }
    } TEST_END();

    TEST_CASE("1.2 Exhaustive Mirroring Symmetry & Involution for all 2,794 Sprites") {
        for (uint32_t i = 0; i < 2794; ++i) {
            const Sprite& orig = archive.get_sprite(i);
            const Sprite& mir = archive.get_mirrored_sprite(i);

            ASSERT_EQ(mir.id, orig.id);
            ASSERT_EQ(mir.width, orig.width);
            ASSERT_EQ(mir.height, orig.height);
            ASSERT_EQ(mir.pitch, orig.pitch);
            ASSERT_EQ(mir.pixels.size(), orig.pixels.size());

            // Check horizontal pixel symmetry across rows
            for (uint32_t y = 0; y < orig.height; ++y) {
                for (uint32_t x = 0; x < orig.width; ++x) {
                    uint8_t orig_px = orig.get_pixel(x, y);
                    uint8_t mir_px = mir.get_pixel(orig.width - 1 - x, y);
                    ASSERT_EQ(orig_px, mir_px);
                }

                // Check stride padding: padding bytes must be zero
                for (uint32_t x = orig.width; x < orig.pitch; ++x) {
                    ASSERT_EQ(mir.pixels[y * mir.pitch + x], 0u);
                }
            }

            // Involution test: double-flipping mirrored sprite restores original pixels
            Sprite double_flipped = mir.create_horizontal_flip(orig.id);
            for (uint32_t y = 0; y < orig.height; ++y) {
                for (uint32_t x = 0; x < orig.width; ++x) {
                    ASSERT_EQ(double_flipped.get_pixel(x, y), orig.get_pixel(x, y));
                }
            }
        }
    } TEST_END();
}

// ============================================================================
// SUITE 2: Exhaustive Verification of all 91 RIFF WAV Audio Buffers
// ============================================================================
void test_suite_2_exhaustive_audio(const std::string& chd_path) {
    TEST_SUITE("Suite 2: Exhaustive Verification of all 91 RIFF WAV Audio Buffers");

    AssetArchive archive;
    ASSERT_TRUE(archive.load_from_file(chd_path));
    ASSERT_EQ(archive.sound_count(), 91u);

    TEST_CASE("2.1 All 91 Audio Clips Produce Valid Standard RIFF WAV Buffers") {
        for (uint32_t i = 0; i < 91; ++i) {
            const SoundClip& snd = archive.get_sound(i);
            ASSERT_EQ(snd.id, i);
            ASSERT_GT(snd.pcm_data.size(), 0u);

            std::vector<uint8_t> wav = snd.build_wav();
            size_t expected_total_size = 44 + snd.pcm_data.size();
            ASSERT_EQ(wav.size(), expected_total_size);

            const uint8_t* p = wav.data();

            // 1. "RIFF" chunk descriptor
            ASSERT_EQ(std::memcmp(p + 0, "RIFF", 4), 0);

            // Chunk Size = 36 + pcm_len
            uint32_t chunk_size = static_cast<uint32_t>(p[4]) |
                                  (static_cast<uint32_t>(p[5]) << 8) |
                                  (static_cast<uint32_t>(p[6]) << 16) |
                                  (static_cast<uint32_t>(p[7]) << 24);
            ASSERT_EQ(chunk_size, static_cast<uint32_t>(36 + snd.pcm_data.size()));
            ASSERT_EQ(wav.size(), static_cast<size_t>(8 + chunk_size));

            // Magic "WAVE"
            ASSERT_EQ(std::memcmp(p + 8, "WAVE", 4), 0);

            // 2. "fmt " subchunk
            ASSERT_EQ(std::memcmp(p + 12, "fmt ", 4), 0);
            uint32_t sub1_size = static_cast<uint32_t>(p[16]) |
                                 (static_cast<uint32_t>(p[17]) << 8) |
                                 (static_cast<uint32_t>(p[18]) << 16) |
                                 (static_cast<uint32_t>(p[19]) << 24);
            ASSERT_EQ(sub1_size, 16u);

            uint16_t format_tag = static_cast<uint16_t>(p[20] | (p[21] << 8));
            ASSERT_EQ(format_tag, 1u); // WAVE_FORMAT_PCM

            uint16_t channels = static_cast<uint16_t>(p[22] | (p[23] << 8));
            ASSERT_EQ(channels, snd.format.channels);
            ASSERT_TRUE(channels == 1u || channels == 2u);

            uint32_t sample_rate = static_cast<uint32_t>(p[24]) |
                                   (static_cast<uint32_t>(p[25]) << 8) |
                                   (static_cast<uint32_t>(p[26]) << 16) |
                                   (static_cast<uint32_t>(p[27]) << 24);
            ASSERT_EQ(sample_rate, snd.format.samples_per_sec);
            ASSERT_TRUE(sample_rate == 11025u || sample_rate == 22050u);

            uint32_t byte_rate = static_cast<uint32_t>(p[28]) |
                                 (static_cast<uint32_t>(p[29]) << 8) |
                                 (static_cast<uint32_t>(p[30]) << 16) |
                                 (static_cast<uint32_t>(p[31]) << 24);
            uint32_t expected_byte_rate = sample_rate * channels * 1; // 8-bit = 1 byte
            ASSERT_EQ(byte_rate, expected_byte_rate);

            uint16_t block_align = static_cast<uint16_t>(p[32] | (p[33] << 8));
            uint16_t expected_block_align = static_cast<uint16_t>(channels * 1);
            ASSERT_EQ(block_align, expected_block_align);

            uint16_t bits_per_sample = static_cast<uint16_t>(p[34] | (p[35] << 8));
            ASSERT_EQ(bits_per_sample, 8u);

            // 3. "data" subchunk
            ASSERT_EQ(std::memcmp(p + 36, "data", 4), 0);
            uint32_t data_size = static_cast<uint32_t>(p[40]) |
                                 (static_cast<uint32_t>(p[41]) << 8) |
                                 (static_cast<uint32_t>(p[42]) << 16) |
                                 (static_cast<uint32_t>(p[43]) << 24);
            ASSERT_EQ(data_size, static_cast<uint32_t>(snd.pcm_data.size()));

            // Verify PCM payload match
            ASSERT_EQ(std::memcmp(p + 44, snd.pcm_data.data(), snd.pcm_data.size()), 0);

            // Verify create_riff_wav() alias
            std::vector<uint8_t> wav_alias = snd.create_riff_wav();
            ASSERT_EQ(wav, wav_alias);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: Out-of-Bounds & Invalid Lookups Stress Testing
// ============================================================================
void test_suite_3_oob_lookups(const std::string& chd_path, const std::string& map_path) {
    TEST_SUITE("Suite 3: Out-of-Bounds & Invalid Lookups Stress Testing");

    AssetArchive archive;
    ASSERT_TRUE(archive.load_from_file(chd_path));
    const auto& pal = archive.get_palette();

    TEST_CASE("3.1 Extreme Sprite Out-of-Bounds Queries") {
        std::vector<uint32_t> bad_ids = {
            2794, 2795, 3000, 10000, 100000, 0x7FFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFFFu
        };

        for (uint32_t bad_id : bad_ids) {
            // Direct sprite lookup
            const Sprite& sp = archive.get_sprite(bad_id);
            ASSERT_EQ(sp.width, 0u);
            ASSERT_EQ(sp.height, 0u);
            ASSERT_EQ(sp.pixels.size(), 0u);

            // Coordinate pixel access on out-of-bounds sprite
            ASSERT_EQ(sp.get_pixel(0, 0), CHD_COLOR_KEY_INDEX);
            ASSERT_EQ(sp.get_pixel(100, 100), CHD_COLOR_KEY_INDEX);

            // to_rgba32 on out-of-bounds sprite must be safe and empty
            std::vector<uint8_t> rgba = sp.to_rgba32(pal);
            ASSERT_EQ(rgba.size(), 0u);

            // Mirrored lookup
            const Sprite& mir = archive.get_mirrored_sprite(bad_id);
            ASSERT_EQ(mir.width, 0u);
            ASSERT_EQ(mir.height, 0u);

            // Directional sprite lookup with bad id
            const Sprite& dir_n = archive.get_directional_sprite(bad_id, Direction::North);
            ASSERT_EQ(dir_n.width, 0u);
            const Sprite& dir_w = archive.get_directional_sprite(bad_id, Direction::West);
            ASSERT_EQ(dir_w.width, 0u);
        }
    } TEST_END();

    TEST_CASE("3.2 Invalid Direction Enum Values") {
        // Test casting out-of-range integer values to Direction and Direction8
        std::vector<uint8_t> bad_dirs = { 8, 9, 15, 50, 127, 128, 200, 255 };

        for (uint8_t bd : bad_dirs) {
            Direction dir = static_cast<Direction>(bd);
            DirectionMapping m = get_direction_mapping(dir);
            // Must wrap cleanly via & 7 into valid compass codes
            ASSERT_TRUE(m.chd_code == 7 || m.chd_code == 8 || m.chd_code == 9 ||
                        m.chd_code == 2 || m.chd_code == 3);

            // Valid sprite with out-of-range direction
            const Sprite& sp = archive.get_directional_sprite(0, dir);
            ASSERT_EQ(sp.id, 0u);
            ASSERT_GT(sp.width, 0u);

            // get_lunchbox_prefix with out-of-range direction
            const char* prefix = get_lunchbox_prefix(dir);
            ASSERT_NE(prefix, nullptr);
            ASSERT_EQ(std::strlen(prefix), 3u);

            // Direction string
            const char* dir_str = direction_to_string(dir);
            ASSERT_NE(dir_str, nullptr);
        }
    } TEST_END();

    TEST_CASE("3.3 Extreme Audio & Tag Out-of-Bounds Queries") {
        std::vector<uint32_t> bad_sound_ids = { 91, 92, 100, 500, 0xFFFFFFFFu };
        for (uint32_t bad_id : bad_sound_ids) {
            const SoundClip& snd = archive.get_sound(bad_id);
            ASSERT_EQ(snd.pcm_data.size(), 0u);
            std::vector<uint8_t> wav = snd.build_wav();
            ASSERT_EQ(wav.size(), 44u); // 44-byte empty container
            ASSERT_EQ(std::memcmp(wav.data(), "RIFF", 4), 0);
        }

        std::vector<uint32_t> bad_tag_ids = { 4, 5, 10, 100, 0xFFFFFFFFu };
        for (uint32_t bad_id : bad_tag_ids) {
            const EventTag& tag = archive.get_tag(bad_id);
            ASSERT_TRUE(tag.name.empty());
        }
    } TEST_END();

    TEST_CASE("3.4 Extreme Animation Out-of-Bounds & Invalid String Lookups") {
        std::vector<uint32_t> bad_anim_ids = { 1344, 1345, 2000, 0xFFFFFFFFu };
        for (uint32_t bad_id : bad_anim_ids) {
            const AnimationSequence& anim = archive.get_animation(bad_id);
            ASSERT_TRUE(anim.name.empty());
            ASSERT_EQ(anim.subitems.size(), 0u);
        }

        // Empty and non-existent name queries
        ASSERT_EQ(archive.find_sprite(""), nullptr);
        ASSERT_EQ(archive.find_sprite("totally_non_existent_sprite.bmp"), nullptr);
        ASSERT_EQ(archive.find_sprite_id(""), -1);
        ASSERT_EQ(archive.find_sprite_id("totally_non_existent_sprite.bmp"), -1);

        ASSERT_EQ(archive.find_sound(""), nullptr);
        ASSERT_EQ(archive.find_sound("totally_non_existent_sound.wav"), nullptr);
        ASSERT_EQ(archive.find_sound_id(""), -1);
        ASSERT_EQ(archive.find_sound_id("totally_non_existent_sound.wav"), -1);

        ASSERT_EQ(archive.find_animation(""), nullptr);
        ASSERT_EQ(archive.find_animation("totally_non_existent_animation"), nullptr);
        ASSERT_EQ(archive.find_animation_id(""), -1);
        ASSERT_EQ(archive.find_animation_id("totally_non_existent_animation"), -1);

        ASSERT_EQ(archive.get_directional_animation("", Direction::North), nullptr);
        ASSERT_EQ(archive.get_directional_animation("non_existent_prefix", Direction::West), nullptr);
    } TEST_END();

    TEST_CASE("3.5 LevelData Out-of-Bounds Coordinates & Index Lookups") {
        LevelData lvl;
        ASSERT_TRUE(lvl.load_from_file(map_path));

        // Coordinate bounds: width = lvl.width, height = lvl.height
        const MapCell& c1 = lvl.get_cell_layer1(lvl.width, lvl.height);
        ASSERT_TRUE(c1.is_empty());
        ASSERT_EQ(c1.tile_index, LVL_EMPTY_TILE);

        const MapCell& c2 = lvl.get_cell_layer2(lvl.width + 100, lvl.height + 100);
        ASSERT_TRUE(c2.is_empty());
        ASSERT_EQ(c2.tile_index, LVL_EMPTY_TILE);

        const MapCell& c_max = lvl.get_cell_layer1(0xFFFFFFFFu, 0xFFFFFFFFu);
        ASSERT_TRUE(c_max.is_empty());

        // Tile dictionary lookups
        const std::string& name_oob = lvl.get_tile_name(static_cast<uint16_t>(lvl.tile_dictionary.size()));
        ASSERT_TRUE(name_oob.empty());

        const std::string& name_max = lvl.get_tile_name(0xFFFFu);
        ASSERT_TRUE(name_max.empty());

        ASSERT_EQ(lvl.find_tile_index(""), -1);
        ASSERT_EQ(lvl.find_tile_index("NON_EXISTENT_TILE_NAME"), -1);
    } TEST_END();
}

// ============================================================================
// SUITE 4: Header Fuzzing & Malformed CHD Archive Stress Testing
// ============================================================================
void test_suite_4_header_fuzzing(const std::string& chd_path) {
    TEST_SUITE("Suite 4: Header Fuzzing & Malformed CHD Archive Stress Testing");

    std::ifstream f(chd_path, std::ios::binary);
    ASSERT_TRUE(f.is_open());
    std::vector<uint8_t> valid_hdr(1052);
    f.read(reinterpret_cast<char*>(valid_hdr.data()), 1052);
    ASSERT_EQ(f.gcount(), 1052);

    TEST_CASE("4.1 Truncated Header Sizes (< 28 bytes)") {
        std::vector<size_t> trunc_sizes = { 0, 1, 4, 12, 20, 24, 27 };
        for (size_t sz : trunc_sizes) {
            CHDHeader hdr;
            ASSERT_FALSE(CHDParser::parse_header(valid_hdr.data(), sz, hdr));

            AssetArchive archive;
            ASSERT_FALSE(archive.load_from_memory(valid_hdr.data(), sz));
            ASSERT_FALSE(archive.is_loaded());
        }
    } TEST_END();

    TEST_CASE("4.2 Invalid CHD Header Version Numbers") {
        std::vector<uint32_t> bad_versions = { 0, 1, 2, 7, 8 };
        for (uint32_t bad_v : bad_versions) {
            std::vector<uint8_t> buf = valid_hdr;
            std::memcpy(buf.data(), &bad_v, 4);

            CHDHeader hdr;
            ASSERT_FALSE(CHDParser::parse_header(buf.data(), buf.size(), hdr));

            AssetArchive archive;
            ASSERT_FALSE(archive.load_from_memory(buf.data(), buf.size()));
        }
    } TEST_END();

    TEST_CASE("4.3 Invalid Palette Byte Sizes") {
        std::vector<uint32_t> bad_pal_sizes = { 0, 256, 512, 1023, 1025, 2048, 0xFFFFFFFFu };
        for (uint32_t bad_pal : bad_pal_sizes) {
            std::vector<uint8_t> buf = valid_hdr;
            std::memcpy(buf.data() + 24, &bad_pal, 4);

            CHDHeader hdr;
            ASSERT_FALSE(CHDParser::parse_header(buf.data(), buf.size(), hdr));

            AssetArchive archive;
            ASSERT_FALSE(archive.load_from_memory(buf.data(), buf.size()));
        }
    } TEST_END();

    TEST_CASE("4.4 Table Offset Inversion & Monotonicity Violations") {
        // Non-monotonic offsets
        struct OffsetMutation {
            uint32_t t1, t2, t3, t4;
        };

        std::vector<OffsetMutation> mutations = {
            { 1053, 6835937, 7903773, 7903835 }, // Table 1 offset != 28 + 1024
            { 1052, 1052, 7903773, 7903835 },    // Table 2 == Table 1
            { 1052, 1000, 7903773, 7903835 },    // Table 2 < Table 1
            { 1052, 6835937, 6835937, 7903835 }, // Table 3 == Table 2
            { 1052, 6835937, 7903835, 7903835 }, // Table 4 == Table 3
            { 1052, 6835937, 7903835, 7903773 }, // Table 4 < Table 3
            { 1052, 6835937, 7903773, 0xFFFFFFFFu } // Table 4 overflow
        };

        for (const auto& mut : mutations) {
            std::vector<uint8_t> buf = valid_hdr;
            std::memcpy(buf.data() + 8, &mut.t1, 4);
            std::memcpy(buf.data() + 12, &mut.t2, 4);
            std::memcpy(buf.data() + 16, &mut.t3, 4);
            std::memcpy(buf.data() + 20, &mut.t4, 4);

            CHDHeader hdr;
            ASSERT_FALSE(CHDParser::parse_header(buf.data(), buf.size(), hdr));
        }
    } TEST_END();

    TEST_CASE("4.5 Buffer Smaller than Table 4 Offset") {
        // Header says table 4 is at offset 7903835, but buffer is only 5000 bytes
        CHDHeader hdr;
        ASSERT_FALSE(CHDParser::parse_header(valid_hdr.data(), 5000, hdr));
    } TEST_END();
}

// ============================================================================
// SUITE 5: Table Deserializer Fuzzing & Mutation
// ============================================================================
void test_suite_5_table_fuzzing(const std::string& chd_path) {
    TEST_SUITE("Suite 5: Table Deserializer Fuzzing & Mutation");

    TEST_CASE("5.1 Corrupted Table 1 Sprite Headers") {
        // Create synthetic Table 1 buffer with invalid sprite headers
        std::vector<uint8_t> data(10000, 0);

        // Case A: Count != 2794
        uint32_t bad_count = 10;
        std::memcpy(data.data(), &bad_count, 4);
        std::vector<Sprite> sprites;
        ASSERT_FALSE(CHDParser::parse_table1_sprites(data.data(), data.size(), 0, sprites));

        // Case B: Count == 2794, but offset exceeds buffer size
        bad_count = 2794;
        std::memcpy(data.data(), &bad_count, 4);
        uint32_t oob_offset = 50000;
        std::memcpy(data.data() + 4, &oob_offset, 4);
        ASSERT_FALSE(CHDParser::parse_table1_sprites(data.data(), data.size(), 0, sprites));

        // Case C: Valid offset table pointing to sprite with pitch < width
        uint32_t spr_offset = 2794 * 4 + 4;
        data.resize(spr_offset + 1000, 0);
        for (uint32_t i = 0; i < 2794; ++i) {
            std::memcpy(data.data() + 4 + i * 4, &spr_offset, 4);
        }
        uint32_t pitch = 10, width = 20, height = 10, fn_len = 5;
        std::memcpy(data.data() + spr_offset + 0, &pitch, 4);
        std::memcpy(data.data() + spr_offset + 4, &width, 4);
        std::memcpy(data.data() + spr_offset + 8, &height, 4);
        std::memcpy(data.data() + spr_offset + 12, &fn_len, 4);
        ASSERT_FALSE(CHDParser::parse_table1_sprites(data.data(), data.size(), 0, sprites));
    } TEST_END();

    TEST_CASE("5.2 Corrupted Table 2 Sound Headers") {
        std::vector<uint8_t> data(5000, 0);

        // Case A: Count != 91
        uint32_t bad_count = 90;
        std::memcpy(data.data(), &bad_count, 4);
        std::vector<SoundClip> sounds;
        ASSERT_FALSE(CHDParser::parse_table2_sounds(data.data(), data.size(), 0, sounds));

        // Case B: Count == 91, but offset exceeds buffer size
        bad_count = 91;
        std::memcpy(data.data(), &bad_count, 4);
        uint32_t oob_offset = 999999;
        std::memcpy(data.data() + 4, &oob_offset, 4);
        ASSERT_FALSE(CHDParser::parse_table2_sounds(data.data(), data.size(), 0, sounds));

        // Case C: Format length < 16
        uint32_t snd_offset = 91 * 4 + 4;
        data.resize(snd_offset + 500, 0);
        for (uint32_t i = 0; i < 91; ++i) {
            std::memcpy(data.data() + 4 + i * 4, &snd_offset, 4);
        }
        uint32_t fmt_len = 10; // Must be >= 16
        std::memcpy(data.data() + snd_offset, &fmt_len, 4);
        ASSERT_FALSE(CHDParser::parse_table2_sounds(data.data(), data.size(), 0, sounds));
    } TEST_END();

    TEST_CASE("5.3 Corrupted Table 4 Animation Sequences") {
        std::vector<uint8_t> data(10000, 0);

        // Case A: Count != 1344
        uint32_t bad_count = 500;
        std::memcpy(data.data(), &bad_count, 4);
        std::vector<AnimationSequence> anims;
        ASSERT_FALSE(CHDParser::parse_table4_animations(data.data(), data.size(), 0, anims));

        // Case B: Count == 1344, offset out of bounds
        bad_count = 1344;
        std::memcpy(data.data(), &bad_count, 4);
        uint32_t oob_offset = 0xFFFFFFFFu;
        std::memcpy(data.data() + 4, &oob_offset, 4);
        ASSERT_FALSE(CHDParser::parse_table4_animations(data.data(), data.size(), 0, anims));

        // Case C: Name length == 0
        uint32_t anim_offset = 1344 * 4 + 4;
        data.resize(anim_offset + 500, 0);
        for (uint32_t i = 0; i < 1344; ++i) {
            std::memcpy(data.data() + 4 + i * 4, &anim_offset, 4);
        }
        uint32_t name_len = 0;
        std::memcpy(data.data() + anim_offset, &name_len, 4);
        ASSERT_FALSE(CHDParser::parse_table4_animations(data.data(), data.size(), 0, anims));
    } TEST_END();
}

// ============================================================================
// SUITE 6: Level Deserializer Fuzzing & Strict rem=0 Residual Guarantee
// ============================================================================
void test_suite_6_lvl_fuzzing(const std::string& map_path) {
    TEST_SUITE("Suite 6: Level Deserializer Fuzzing & Strict rem=0 Residual Guarantee");

    std::ifstream f(map_path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open());
    std::streamsize sz = f.tellg();
    ASSERT_GT(sz, 42);
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> valid_lvl(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(valid_lvl.data()), sz);

    TEST_CASE("6.1 Truncated LVL Buffers (< 42 bytes)") {
        std::vector<size_t> trunc_sizes = { 0, 10, 25, 41 };
        for (size_t tsz : trunc_sizes) {
            LevelData lvl;
            ASSERT_FALSE(LVLParser::load_from_memory(valid_lvl.data(), tsz, lvl));
        }
    } TEST_END();

    TEST_CASE("6.2 Corrupted LVL Header Version & Game Mode") {
        // Bad version != 8
        std::vector<uint8_t> bad_v = valid_lvl;
        uint32_t v = 7;
        std::memcpy(bad_v.data(), &v, 4);
        LevelData lvl;
        ASSERT_FALSE(LVLParser::load_from_memory(bad_v.data(), bad_v.size(), lvl));

        // Bad game_mode != 1
        std::vector<uint8_t> bad_gm = valid_lvl;
        uint32_t gm = 2;
        std::memcpy(bad_gm.data() + 4, &gm, 4);
        ASSERT_FALSE(LVLParser::load_from_memory(bad_gm.data(), bad_gm.size(), lvl));
    } TEST_END();

    TEST_CASE("6.3 Zero Grid Dimensions (width == 0 or height == 0)") {
        std::vector<uint8_t> bad_dim = valid_lvl;
        // Tile dictionary count is at offset 40 (uint16_t)
        uint16_t tile_count = static_cast<uint16_t>(bad_dim[40] | (bad_dim[41] << 8));
        size_t dim_offset = 42 + static_cast<size_t>(tile_count + 1) * 11;
        ASSERT_LT(dim_offset + 8, bad_dim.size());

        uint32_t zero = 0;
        std::memcpy(bad_dim.data() + dim_offset, &zero, 4); // width = 0
        LevelData lvl;
        ASSERT_FALSE(LVLParser::load_from_memory(bad_dim.data(), bad_dim.size(), lvl));
    } TEST_END();

    TEST_CASE("6.4 Strict rem=0 Guarantee: Trailing Junk Byte Injection Rejection") {
        // Valid map must parse with rem == 0
        LevelData clean_lvl;
        ASSERT_TRUE(LVLParser::load_from_memory(valid_lvl.data(), valid_lvl.size(), clean_lvl));

        // Appending 1 trailing byte (rem = 1) MUST cause parser to reject!
        std::vector<uint8_t> junk_lvl = valid_lvl;
        junk_lvl.push_back(0xAA); // Trailing junk byte
        LevelData rejected_lvl;
        ASSERT_FALSE(LVLParser::load_from_memory(junk_lvl.data(), junk_lvl.size(), rejected_lvl));

        // Appending 16 trailing bytes (rem = 16)
        junk_lvl.resize(valid_lvl.size() + 16, 0x00);
        ASSERT_FALSE(LVLParser::load_from_memory(junk_lvl.data(), junk_lvl.size(), rejected_lvl));
    } TEST_END();
}

// ============================================================================
// SUITE 7: Mirroring Mathematics & Directional Invariants
// ============================================================================
void test_suite_7_mirroring_math() {
    TEST_SUITE("Suite 7: Mirroring Mathematics & Directional Invariants");

    TEST_CASE("7.1 Exhaustive Continuous Angle Sweep [-720, 720] Degrees") {
        for (float deg = -720.0f; deg <= 720.0f; deg += 0.5f) {
            Direction d = angle_to_direction(deg);
            uint8_t raw = static_cast<uint8_t>(d);
            ASSERT_LT(raw, 8u);
        }

        // Specific cardinal and diagonal boundaries
        ASSERT_EQ(angle_to_direction(0.0f), Direction::North);
        ASSERT_EQ(angle_to_direction(22.4f), Direction::North);
        ASSERT_EQ(angle_to_direction(22.6f), Direction::NorthEast);
        ASSERT_EQ(angle_to_direction(45.0f), Direction::NorthEast);
        ASSERT_EQ(angle_to_direction(67.4f), Direction::NorthEast);
        ASSERT_EQ(angle_to_direction(67.6f), Direction::East);
        ASSERT_EQ(angle_to_direction(90.0f), Direction::East);
        ASSERT_EQ(angle_to_direction(135.0f), Direction::SouthEast);
        ASSERT_EQ(angle_to_direction(180.0f), Direction::South);
        ASSERT_EQ(angle_to_direction(225.0f), Direction::SouthWest);
        ASSERT_EQ(angle_to_direction(270.0f), Direction::West);
        ASSERT_EQ(angle_to_direction(315.0f), Direction::NorthWest);
    } TEST_END();

    TEST_CASE("7.2 Exhaustive 2D Vector Sweep [-50, 50] x [-50, 50]") {
        for (int dy = -50; dy <= 50; ++dy) {
            for (int dx = -50; dx <= 50; ++dx) {
                Direction d = vector_to_direction(dx, dy);
                uint8_t raw = static_cast<uint8_t>(d);
                ASSERT_LT(raw, 8u);
            }
        }
        // Zero vector defaults to South
        ASSERT_EQ(vector_to_direction(0, 0), Direction::South);
    } TEST_END();

    TEST_CASE("7.3 Involution and Mathematical Symmetry of Reflection") {
        // mirror_dx(dx, W) = -(dx + W)
        // mirror_dx(mirror_dx(dx, W), W) = -(-(dx + W) + W) = (dx + W) - W = dx
        for (int32_t dx = -500; dx <= 500; dx += 17) {
            for (uint32_t W = 1; W <= 640; W += 23) {
                int32_t m = mirror_dx(dx, W);
                int32_t inv = mirror_dx(m, W);
                ASSERT_EQ(inv, dx);
            }
        }

        // Bounding box: [left, right] -> [-right, -left]
        for (int32_t l = -100; l <= 100; l += 7) {
            for (int32_t r = l; r <= l + 100; r += 11) {
                int32_t ml = 0, mr = 0;
                mirror_bounding_box(l, r, ml, mr);
                ASSERT_EQ(ml, -r);
                ASSERT_EQ(mr, -l);
                int32_t rl = 0, rr = 0;
                mirror_bounding_box(ml, mr, rl, rr);
                ASSERT_EQ(rl, l);
                ASSERT_EQ(rr, r);
            }
        }
    } TEST_END();
}

// ============================================================================
// SUITE 8: Concurrent Multi-Threaded Query Stress Test
// ============================================================================
void test_suite_8_concurrency(const std::string& chd_path) {
    TEST_SUITE("Suite 8: Concurrent Multi-Threaded Query Stress Test");

    AssetArchive archive;
    ASSERT_TRUE(archive.load_from_file(chd_path));

    TEST_CASE("8.1 8 Concurrent Threads x 1,000 Randomized Lookups (8,000 Total)") {
        constexpr int NUM_THREADS = 8;
        constexpr int QUERIES_PER_THREAD = 1000;
        std::atomic<int> atomic_fails{0};
        std::vector<std::thread> workers;

        for (int t = 0; t < NUM_THREADS; ++t) {
            workers.emplace_back([&, t]() {
                std::mt19937 rng(1337 + t);
                std::uniform_int_distribution<uint32_t> spr_dist(0, 2793);
                std::uniform_int_distribution<uint32_t> snd_dist(0, 90);
                std::uniform_int_distribution<uint32_t> anim_dist(0, 1343);
                std::uniform_int_distribution<uint8_t> dir_dist(0, 7);

                const auto& pal = archive.get_palette();

                for (int q = 0; q < QUERIES_PER_THREAD; ++q) {
                    uint32_t sid = spr_dist(rng);
                    const Sprite& sp = archive.get_sprite(sid);
                    if (sp.id != sid || sp.width == 0) {
                        ++atomic_fails;
                    }

                    // Mirrored
                    const Sprite& mir = archive.get_mirrored_sprite(sid);
                    if (mir.width != sp.width) {
                        ++atomic_fails;
                    }

                    // Directional
                    Direction dir = static_cast<Direction>(dir_dist(rng));
                    const Sprite& dir_sp = archive.get_directional_sprite(sid, dir);
                    if (dir_sp.width != sp.width) {
                        ++atomic_fails;
                    }

                    // RGBA32 conversion on sample
                    if (q % 50 == 0) {
                        auto rgba = sp.to_rgba32(pal);
                        if (rgba.size() != static_cast<size_t>(sp.width) * sp.height * 4) {
                            ++atomic_fails;
                        }
                    }

                    // Audio
                    uint32_t snd_id = snd_dist(rng);
                    const SoundClip& snd = archive.get_sound(snd_id);
                    if (snd.id != snd_id || snd.pcm_data.empty()) {
                        ++atomic_fails;
                    }

                    // Animation
                    uint32_t aid = anim_dist(rng);
                    const AnimationSequence& anim = archive.get_animation(aid);
                    if (anim.id != aid) {
                        ++atomic_fails;
                    }
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }

        ASSERT_EQ(atomic_fails.load(), 0);
    } TEST_END();
}

// ============================================================================
// Main Runner
// ============================================================================
int main() {
    std::cout << "=======================================================\n"
              << " Microsoft Ants Native Asset Decoder ADVERSARIAL Suite\n"
              << " Target: libants-assets (Milestone 1 Challenger)\n"
              << "=======================================================\n";

    std::string assets_dir = locate_assets_dir();
    std::string chd_path = assets_dir + "/ants.chd";
    std::string map_dir = assets_dir + "/Maps";
    std::string tiny_map = map_dir + "/TINY.LVL";

    std::cout << "Located Assets Directory: " << assets_dir << "\n";
    std::cout << "Target CHD Path:          " << chd_path << "\n";
    std::cout << "Target Sample Map:        " << tiny_map << "\n";

    if (!fs::exists(chd_path)) {
        std::cerr << "ERROR: ants.chd not found at " << chd_path << "\n";
        return 1;
    }
    if (!fs::exists(tiny_map)) {
        std::cerr << "ERROR: TINY.LVL not found at " << tiny_map << "\n";
        return 1;
    }

    test_suite_1_exhaustive_sprites(chd_path);
    test_suite_2_exhaustive_audio(chd_path);
    test_suite_3_oob_lookups(chd_path, tiny_map);
    test_suite_4_header_fuzzing(chd_path);
    test_suite_5_table_fuzzing(chd_path);
    test_suite_6_lvl_fuzzing(tiny_map);
    test_suite_7_mirroring_math();
    test_suite_8_concurrency(chd_path);

    std::cout << "\n=======================================================\n"
              << " ADVERSARIAL TEST SUMMARY\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL ADVERSARIAL TEST SUITES PASSED CLEANLY (100% PASS) <<<\n";
        return 0;
    } else {
        std::cerr << " >>> ADVERSARIAL TEST SUITE FAILED WITH " << g_test_failures << " FAILURES <<<\n";
        return 1;
    }
}
