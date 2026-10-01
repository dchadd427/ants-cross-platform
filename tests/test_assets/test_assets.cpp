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

namespace fs = std::filesystem;

// ============================================================================
// Lightweight Zero-Dependency Test Framework
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

#define TEST_SUITE(name) \
    std::cout << "\n=======================================================\n" \
              << " [SUITE] " << name << "\n" \
              << "=======================================================\n"

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    int prev_fails = g_test_failures;
    fn();
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

#define ASSERT_NEAR(a, b, eps) \
    do { \
        ++g_assert_count; \
        if (std::fabs((a) - (b)) > (eps)) { \
            std::cout << "FAILED!\n    Assertion failed: |" #a " - " #b "| <= " #eps \
                      << " (diff=" << std::fabs((a) - (b)) << ")" \
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
        "../../../Original-Ants"
    };

    for (const auto& path : candidates) {
        if (fs::exists(path) && fs::exists(path + "/ants.chd")) {
            return path;
        }
    }
    return "Original-Ants";
}

using namespace ants::assets;

// ============================================================================
// SUITE 1: CHD Header & Master Palette Validation
// ============================================================================
void test_suite_1_header_and_palette(const std::string& chd_path) {
    TEST_SUITE("Suite 1: CHD Header & Master Palette Validation");

    TEST_CASE("1.1 Header Verification") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_TRUE(archive.is_loaded());

        // Low-level header validation
        std::ifstream f(chd_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        std::vector<uint8_t> hdr_bytes(28);
        f.read(reinterpret_cast<char*>(hdr_bytes.data()), 28);
        ASSERT_EQ(f.gcount(), 28);

        CHDHeader hdr;
        ASSERT_TRUE(CHDParser::parse_header(hdr_bytes.data(), 28, hdr));
        ASSERT_EQ(hdr.version, 9u);
        ASSERT_EQ(hdr.timestamp, 0x378D661Cu);
        ASSERT_EQ(hdr.table1_offset, 1052u);
        ASSERT_EQ(hdr.table2_offset, 6835937u);
        ASSERT_EQ(hdr.table3_offset, 7903773u);
        ASSERT_EQ(hdr.table4_offset, 7903835u);
        ASSERT_EQ(hdr.palette_bytes, 1024u);
    } TEST_END();

    TEST_CASE("1.2 Palette Color Extraction & Color Key 254 Transparency") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        const auto& pal = archive.get_palette();

        // Index 254 must be pure magenta with Alpha = 0
        const ColorRGBA& key = pal[254];
        ASSERT_EQ(key.r, 255u);
        ASSERT_EQ(key.g, 0u);
        ASSERT_EQ(key.b, 255u);
        ASSERT_EQ(key.a, 0u);

        // Index 0 must be opaque slate RGB(119, 119, 127) with Alpha = 255
        const ColorRGBA& c0 = pal[0];
        ASSERT_EQ(c0.r, 119u);
        ASSERT_EQ(c0.g, 119u);
        ASSERT_EQ(c0.b, 127u);
        ASSERT_EQ(c0.a, 255u);

        // All other 255 indices must have Alpha = 255
        for (size_t i = 0; i < 256; ++i) {
            if (i == 254) {
                ASSERT_EQ(pal[i].a, 0u);
            } else {
                ASSERT_EQ(pal[i].a, 255u);
            }
        }
    } TEST_END();

    TEST_CASE("1.3 Team Color Range Dominance Checks") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        const auto& pal = archive.get_palette();

        // Team 0: Black (237..239) -> R, G, B all low and balanced
        for (size_t i = TEAM_BLACK_START; i <= TEAM_BLACK_END; ++i) {
            ASSERT_LT(pal[i].r, 100u);
            ASSERT_LT(pal[i].g, 100u);
            ASSERT_LT(pal[i].b, 120u);
        }

        // Team 1: Blue (34..39) -> Blue channel dominant
        for (size_t i = TEAM_BLUE_START; i <= TEAM_BLUE_END; ++i) {
            ASSERT_GT(pal[i].b, pal[i].r);
            ASSERT_GT(pal[i].b, pal[i].g);
        }

        // Team 2: Red (178..181) -> Red channel dominant
        for (size_t i = TEAM_RED_START; i <= TEAM_RED_END; ++i) {
            ASSERT_GT(pal[i].r, pal[i].g);
            ASSERT_GT(pal[i].r, pal[i].b);
        }

        // Team 3: Green (49..52) -> Green channel dominant
        for (size_t i = TEAM_GREEN_START; i <= TEAM_GREEN_END; ++i) {
            ASSERT_GT(pal[i].g, pal[i].r);
            ASSERT_GT(pal[i].g, pal[i].b);
        }
    } TEST_END();
}

// ============================================================================
// SUITE 2: Table 1 Sprite Bitmaps Validation
// ============================================================================
void test_suite_2_sprites(const std::string& chd_path) {
    TEST_SUITE("Suite 2: Table 1 Sprite Bitmaps Validation");

    TEST_CASE("2.1 Total Sprite Count & Invariant Checks") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_EQ(archive.sprite_count(), 2794u);

        for (uint32_t i = 0; i < 2794; ++i) {
            const Sprite& sp = archive.get_sprite(i);
            ASSERT_EQ(sp.id, i);
            ASSERT_GT(sp.width, 0u);
            ASSERT_GT(sp.height, 0u);
            ASSERT_GE(sp.pitch, sp.width);
            ASSERT_EQ(sp.pixels.size(), static_cast<size_t>(sp.pitch) * sp.height);
            ASSERT_FALSE(sp.name.empty());
        }
    } TEST_END();

    TEST_CASE("2.2 Sprite Spot Checks (dclay48, qh2, x0y0)") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Sprite 0: dclay48.bmp
        const Sprite& sp0 = archive.get_sprite(0);
        ASSERT_EQ(sp0.name, "dclay48.bmp");
        ASSERT_EQ(sp0.width, 45u);
        ASSERT_EQ(sp0.height, 47u);
        ASSERT_EQ(sp0.pitch, 48u);

        // RGBA tightly packed conversion (strips row padding)
        auto rgba0 = sp0.to_rgba32(archive.get_palette());
        ASSERT_EQ(rgba0.size(), 45u * 47u * 4u);

        // Sprite 231: qh2.bmp (Area maximum: 362x463)
        const Sprite& sp231 = archive.get_sprite(231);
        ASSERT_EQ(sp231.name, "qh2.bmp");
        ASSERT_EQ(sp231.width, 362u);
        ASSERT_EQ(sp231.height, 463u);
        ASSERT_EQ(sp231.pitch, 368u);

        // Sprite 2709: x0y0.bmp (HUD top border: 640x22)
        const Sprite& sp2709 = archive.get_sprite(2709);
        ASSERT_EQ(sp2709.name, "x0y0.bmp");
        ASSERT_EQ(sp2709.width, 640u);
        ASSERT_EQ(sp2709.height, 22u);
        ASSERT_EQ(sp2709.pitch, 640u);

        // Named lookups
        const Sprite* found_qh2 = archive.find_sprite("qh2.bmp");
        ASSERT_NE(found_qh2, nullptr);
        ASSERT_EQ(found_qh2->id, 231u);
        ASSERT_EQ(archive.find_sprite_id("qh2.bmp"), 231);
    } TEST_END();

    TEST_CASE("2.3 Pitch Stride Padding Discipline") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Sprite 0 has pitch=48, width=45. Stride padding = 3 bytes per scanline.
        const Sprite& sp0 = archive.get_sprite(0);
        for (uint32_t y = 0; y < sp0.height; ++y) {
            for (uint32_t x = sp0.width; x < sp0.pitch; ++x) {
                // Pitch padding bytes are 0x00
                ASSERT_EQ(sp0.pixels[y * sp0.pitch + x], 0u);
            }
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: Table 2 Digital Audio Clips Validation
// ============================================================================
void test_suite_3_audio(const std::string& chd_path) {
    TEST_SUITE("Suite 3: Table 2 Digital Audio Clips Validation");

    TEST_CASE("3.1 Total Sound Count & Format Invariants") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_EQ(archive.sound_count(), 91u);

        size_t mono_count = 0;
        size_t stereo_count = 0;

        for (uint32_t i = 0; i < 91; ++i) {
            const SoundClip& snd = archive.get_sound(i);
            ASSERT_EQ(snd.id, i);
            ASSERT_EQ(snd.format.format_tag, 1u); // PCM
            ASSERT_EQ(snd.format.bits_per_sample, 8u); // 8-bit
            ASSERT_TRUE(snd.format.samples_per_sec == 11025u || snd.format.samples_per_sec == 22050u);
            ASSERT_GT(snd.pcm_data.size(), 0u);

            if (snd.format.channels == 1) {
                ++mono_count;
            } else if (snd.format.channels == 2) {
                ++stereo_count;
            }

            // RIFF WAV reconstruction
            std::vector<uint8_t> wav = snd.build_wav();
            ASSERT_EQ(wav.size(), 44u + snd.pcm_data.size());
            ASSERT_EQ(std::memcmp(wav.data(), "RIFF", 4), 0);
            ASSERT_EQ(std::memcmp(wav.data() + 8, "WAVE", 4), 0);
            ASSERT_EQ(std::memcmp(wav.data() + 12, "fmt ", 4), 0);
            ASSERT_EQ(std::memcmp(wav.data() + 36, "data", 4), 0);
        }

        ASSERT_EQ(mono_count, 88u);
        ASSERT_EQ(stereo_count, 3u);
    } TEST_END();

    TEST_CASE("3.2 Stereo Clips (IDs 6, 45, 46)") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        const SoundClip& s6 = archive.get_sound(6);
        ASSERT_EQ(s6.format.channels, 2u);
        ASSERT_EQ(s6.pcm_data.size(), 65190u);

        const SoundClip& s45 = archive.get_sound(45);
        ASSERT_EQ(s45.format.channels, 2u);
        ASSERT_EQ(s45.pcm_data.size(), 11026u);

        const SoundClip& s46 = archive.get_sound(46);
        ASSERT_EQ(s46.format.channels, 2u);
        ASSERT_EQ(s46.pcm_data.size(), 11026u);
    } TEST_END();

    TEST_CASE("3.3 Key Gameplay Audio Spot Checks") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Sound 56: winner.wav (22,050 Hz, 102,860 bytes)
        const SoundClip& s56 = archive.get_sound(56);
        ASSERT_EQ(s56.name, "winner.wav");
        ASSERT_EQ(s56.format.samples_per_sec, 22050u);
        ASSERT_EQ(s56.pcm_data.size(), 102860u);

        // Sound 58: underattack.wav (22,050 Hz, 15,540 bytes, 2566 Hz alarm)
        const SoundClip& s58 = archive.get_sound(58);
        ASSERT_EQ(s58.name, "underattack.wav");
        ASSERT_EQ(s58.format.samples_per_sec, 22050u);
        ASSERT_EQ(s58.pcm_data.size(), 15540u);

        // Sound 71: splash.wav (11,025 Hz, 21,203 bytes)
        const SoundClip& s71 = archive.get_sound(71);
        ASSERT_EQ(s71.name, "splash.wav");
        ASSERT_EQ(s71.format.samples_per_sec, 11025u);
        ASSERT_EQ(s71.pcm_data.size(), 21203u);

        // Sound 72: antdrown.wav (11,025 Hz, 13,899 bytes)
        const SoundClip& s72 = archive.get_sound(72);
        ASSERT_EQ(s72.name, "antdrown.wav");
        ASSERT_EQ(s72.format.samples_per_sec, 11025u);
        ASSERT_EQ(s72.pcm_data.size(), 13899u);

        // Sound 88: scoredn.wav
        const SoundClip& s88 = archive.get_sound(88);
        ASSERT_EQ(s88.name, "scoredn.wav");
        ASSERT_EQ(s88.format.samples_per_sec, 11025u);
        ASSERT_EQ(s88.pcm_data.size(), 3293u);
    } TEST_END();
}

// ============================================================================
// SUITE 4: Table 3 Event Tag Descriptors Validation
// ============================================================================
void test_suite_4_event_tags(const std::string& chd_path) {
    TEST_SUITE("Suite 4: Table 3 Event Tag Descriptors Validation");

    TEST_CASE("4.1 Event Tag IDs and Names") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_EQ(archive.tag_count(), 4u);

        const EventTag& t0 = archive.get_tag(0);
        ASSERT_EQ(t0.id, 3u);
        ASSERT_EQ(t0.name, "HITGROUND");

        const EventTag& t1 = archive.get_tag(1);
        ASSERT_EQ(t1.id, 4u);
        ASSERT_EQ(t1.name, "ATTACKHIT");

        const EventTag& t2 = archive.get_tag(2);
        ASSERT_EQ(t2.id, 5u);
        ASSERT_EQ(t2.name, "HEAL");

        const EventTag& t3 = archive.get_tag(3);
        ASSERT_EQ(t3.id, 10u);
        ASSERT_TRUE(t3.name.empty());
    } TEST_END();
}

// ============================================================================
// SUITE 5: Table 4 Animations & Audio Triggers Validation
// ============================================================================
void test_suite_5_animations(const std::string& chd_path) {
    TEST_SUITE("Suite 5: Table 4 Animations & Audio Triggers Validation");

    TEST_CASE("5.1 Total Count, Frame Sums, and Sound Triggers") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));
        ASSERT_EQ(archive.animation_count(), 1344u);

        size_t total_subitems = 0;
        size_t total_frames = 0;
        size_t active_sound_triggers = 0;

        for (uint32_t i = 0; i < 1344; ++i) {
            const AnimationSequence& anim = archive.get_animation(i);
            ASSERT_EQ(anim.id, i);
            ASSERT_FALSE(anim.name.empty());

            total_subitems += anim.subitems.size();
            for (const auto& sub : anim.subitems) {
                total_frames += sub.frames.size();
                if (sub.has_sound_trigger()) {
                    ++active_sound_triggers;
                    ASSERT_LT(sub.default_sp, 91u);
                }
                for (const auto& fr : sub.frames) {
                    ASSERT_LT(fr.sprite_index, 2794u);
                }
            }
        }

        ASSERT_EQ(total_subitems, 8010u);
        ASSERT_EQ(total_frames, 12210u);
        ASSERT_EQ(active_sound_triggers, 365u);
    } TEST_END();

    TEST_CASE("5.2 Key Animation Lookups & Reverse-Engineered Sequences") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Anim 0: d_shell
        const AnimationSequence* d_shell = archive.find_animation("d_shell");
        ASSERT_NE(d_shell, nullptr);
        ASSERT_EQ(d_shell->id, 0u);
        ASSERT_EQ(d_shell->subitems.size(), 1u);
        ASSERT_EQ(d_shell->subitems[0].frames.size(), 68u);

        // Anim 25: re_screen (Scorecard modal)
        const AnimationSequence* re_screen = archive.find_animation("re_screen");
        ASSERT_NE(re_screen, nullptr);
        ASSERT_EQ(re_screen->id, 25u);
        ASSERT_EQ(re_screen->subitems.size(), 1u);
        ASSERT_EQ(re_screen->subitems[0].frames.size(), 149u);

        // Anim 40: dsplash (Water splash)
        const AnimationSequence* dsplash = archive.find_animation("dsplash");
        ASSERT_NE(dsplash, nullptr);
        ASSERT_EQ(dsplash->id, 40u);
        ASSERT_EQ(dsplash->subitems.size(), 5u);

        // Anim 1134: agdr301 (Worker Ant 22-subitem drowning sequence)
        const AnimationSequence* agdr = archive.find_animation("agdr301");
        ASSERT_NE(agdr, nullptr);
        ASSERT_EQ(agdr->id, 1134u);
        ASSERT_EQ(agdr->subitems.size(), 22u);
        ASSERT_EQ(agdr->subitems[0].default_sp, 71u); // Sound 71 splash.wav
        ASSERT_EQ(agdr->subitems[1].default_sp, 72u); // Sound 72 antdrown.wav

        // Anim 1095: atcr501 (Thief Ant infiltration dive)
        const AnimationSequence* atcr = archive.find_animation("atcr501");
        ASSERT_NE(atcr, nullptr);
        ASSERT_EQ(atcr->id, 1095u);
        ASSERT_EQ(atcr->subitems.size(), 33u);
        ASSERT_EQ(atcr->subitems[19].default_sp, 84u); // steala.wav
        ASSERT_EQ(atcr->subitems[26].default_sp, 85u); // stealb.wav
        ASSERT_EQ(atcr->subitems[31].default_sp, 86u); // stealc.wav

        // Anim 789: abdb301 (Bomber Ant defuse)
        const AnimationSequence* abdb = archive.find_animation("abdb301");
        ASSERT_NE(abdb, nullptr);
        ASSERT_EQ(abdb->id, 789u);
        ASSERT_EQ(abdb->subitems.size(), 12u);
        ASSERT_EQ(abdb->subitems[3].default_sp, 73u); // bombdrop.wav
        ASSERT_EQ(abdb->subitems[6].default_sp, 74u); // bombmuffle.wav

        // Anim 1317: absb301 (Bomber Ant plant mine)
        const AnimationSequence* absb = archive.find_animation("absb301");
        ASSERT_NE(absb, nullptr);
        ASSERT_EQ(absb->id, 1317u);
        ASSERT_EQ(absb->subitems.size(), 17u);
        ASSERT_EQ(absb->subitems[9].default_sp, 90u); // bombpick.wav
    } TEST_END();
}

// ============================================================================
// SUITE 6: Map Loading & Trailing Block Validation (Maps/*.LVL)
// ============================================================================
void test_suite_6_maps(const std::string& map_dir) {
    TEST_SUITE("Suite 6: Map Loading & Trailing Block Validation (Maps/*.LVL)");

    struct ExpectedMap {
        std::string filename;
        uint32_t width;
        uint32_t height;
        uint16_t tile_dict_count;
        uint16_t spawns_count;
        uint16_t food_pools_count;
        uint16_t waypoints_count;
        uint16_t f_last;
        std::string desc;
    };

    std::vector<ExpectedMap> expected = {
        { "TINY.LVL",     31, 31, 670,  12, 14, 0,  3, "Tiny map with no PowerUps" },
        { "SMALL.LVL",    40, 40, 1326, 18, 5,  2,  2, "Small map for fast game" },
        { "MEDIUM.LVL",   60, 60, 1325, 30, 4,  15, 6, "Intermediate map" },
        { "GAUNTLET.LVL", 60, 60, 1330, 32, 2,  20, 6, "Race for your life!" },
        { "ISLANDS.LVL",  60, 60, 1330, 40, 10, 22, 4, "Island hopping, expert map" },
        { "TREASURE.LVL", 60, 60, 1335, 29, 18, 19, 9, "One person's trash..." }
    };

    for (const auto& em : expected) {
        std::string path = map_dir + "/" + em.filename;
        TEST_CASE("6. " + em.filename + " Exact Decoding & rem=0") {
            LevelData map;
            ASSERT_TRUE(map.load_from_file(path));

            ASSERT_EQ(map.version, 8u);
            ASSERT_EQ(map.game_mode, 1u);
            ASSERT_EQ(map.width(), em.width);
            ASSERT_EQ(map.height(), em.height);
            ASSERT_EQ(map.width, em.width);
            ASSERT_EQ(map.height, em.height);
            ASSERT_EQ(map.description, em.desc);
            ASSERT_EQ(map.tile_dictionary.size(), em.tile_dict_count);

            size_t cell_count = static_cast<size_t>(em.width) * em.height;
            ASSERT_EQ(map.layer1_cells().size(), cell_count);
            ASSERT_EQ(map.layer2_cells().size(), cell_count);
            ASSERT_EQ(map.layer1_terrain.size(), cell_count);
            ASSERT_EQ(map.layer2_interactive.size(), cell_count);

            ASSERT_EQ(map.anthill_spawns().size(), em.spawns_count);
            ASSERT_EQ(map.anthill_spawns.size(), em.spawns_count);
            ASSERT_EQ(map.food_schedules().size(), em.food_pools_count);
            ASSERT_EQ(map.food_schedules.size(), em.food_pools_count);
            ASSERT_EQ(map.ambient_flag, 0u);
            ASSERT_EQ(map.ambient_tile_or_sound, 0x7FFEu);
            ASSERT_EQ(map.waypoints.size(), em.waypoints_count);
            ASSERT_EQ(map.boundary_param, em.f_last);

            // Layer 2 empty sentinel check
            size_t empty_count = 0;
            for (const auto& cell : map.layer2_interactive) {
                if (cell.is_empty()) {
                    ++empty_count;
                }
            }
            ASSERT_GT(empty_count, 0u);

            // Interface Contract: verify layer1_terrain(x, y) and layer2_item(x, y)
            for (uint32_t y = 0; y < map.height(); ++y) {
                for (uint32_t x = 0; x < map.width(); ++x) {
                    ASSERT_EQ(map.layer1_terrain(x, y), map.get_cell_layer1(x, y).tile_index);
                    ASSERT_EQ(map.layer2_item(x, y), map.get_cell_layer2(x, y).tile_index);
                }
            }

            // Waypoints probabilities summation check (sum == 1.0)
            for (const auto& wp : map.waypoints) {
                if (wp.flag != 0) {
                    double sum = 0.0;
                    for (double p : wp.probabilities) {
                        sum += p;
                    }
                    ASSERT_NEAR(sum, 1.0, 1e-5);
                }
            }
        } TEST_END();
    }
}

// A small map built in memory: `rows` x `columns` cells whose fields say where they are (tile = (y * 16 + x) % 4, flags = y, properties = x), a hill marker, a food object,
// a waypoint and a final word (eggs) at the corner (x = columns - 1, y = rows - 1), followed by `filler` bytes
static std::vector<uint8_t> build_test_lvl(uint32_t rows, uint32_t columns, uint16_t eggs, size_t filler) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint32_t v) { b.push_back(static_cast<uint8_t>(v & 0xFF)); b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    u32(8);                                   // version
    u32(1);                                   // game mode
    u16(7);                                   // minutes
    const std::string desc = "Synthetic map";
    for (size_t i = 0; i < 30; ++i) b.push_back(i < desc.size() ? static_cast<uint8_t>(desc[i]) : 0);
    u16(3);                                   // tile dictionary: 4 names
    for (int t = 0; t < 4; ++t) {
        const std::string name = "tile" + std::to_string(t);
        for (size_t i = 0; i < 11; ++i) b.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
    }
    u32(rows);                                // the FIRST dimension dword is the number of rows ...
    u32(columns);                             // ... the second the number of columns
    for (uint32_t y = 0; y < rows; ++y) {     // layer 1, rows outer
        for (uint32_t x = 0; x < columns; ++x) {
            u16((y * 16 + x) % 4);
            u16(y);
            u16(x);
        }
    }
    for (uint32_t y = 0; y < rows; ++y) {     // layer 2: empty but for one cell
        for (uint32_t x = 0; x < columns; ++x) {
            const bool marked = (y == 2 && x == columns - 2);
            u16(marked ? 1u : 0x7FFEu);
            u16(0);
            u16(0);
        }
    }
    u16(1);                                   // block 1: one start marker (tile, y, x)
    u16(2);
    u16(rows - 1);
    u16(columns - 1);
    u16(1);                                   // block 2: one food object (y, x, units, points, stages)
    u16(rows - 2);
    u16(columns - 1);
    u16(5);
    u16(7);
    u16(1);
    u16(5);
    u16(3);
    u16(0);                                   // block 3: ambient flag, tile
    u16(0x7FFE);
    u16(1);                                   // block 4: one waypoint (y, x, flag 0)
    u16(3);
    u16(columns - 1);
    u32(0);
    u16(eggs);                                // the final word
    for (size_t i = 0; i < filler; ++i) b.push_back(static_cast<uint8_t>(0x7F + (i % 3)));
    return b;
}

void test_suite_6b_map_layout() {
    TEST_SUITE("Suite 6b: Map Header Order (Rows First), Trailing Filler, Final Word");

    TEST_CASE("6b.1 The First Dimension Dword Is The Number Of Rows, The Second The Number Of Columns (Non-Square Maps: OCEAN.LVL Has 81 Rows Of 100 Columns)") {
        const uint32_t rows = 7;
        const uint32_t columns = 10;
        const std::vector<uint8_t> lvl_bytes = build_test_lvl(rows, columns, 4, 0);
        LevelData map;
        ASSERT_TRUE(map.load_from_memory(lvl_bytes.data(), lvl_bytes.size()));
        ASSERT_EQ(map.width(), columns);
        ASSERT_EQ(map.height(), rows);
        ASSERT_EQ(map.layer1_terrain.size(), static_cast<size_t>(rows) * columns);
        // every cell is where its own fields say: the rows are the outer loop of the file
        for (uint32_t y = 0; y < rows; ++y) {
            for (uint32_t x = 0; x < columns; ++x) {
                const MapCell& c = map.get_cell_layer1(x, y);
                ASSERT_EQ(c.tile_index, (y * 16 + x) % 4);
                ASSERT_EQ(c.flags, y);
                ASSERT_EQ(c.properties, x);
            }
        }
        ASSERT_FALSE(map.get_cell_layer2(columns - 2, 2).is_empty());
        ASSERT_TRUE(map.get_cell_layer2(2, columns - 2).is_empty());               // not transposed
        // the records use (y, x) inside the map: a hill marker, a food object and a waypoint at the far corner
        ASSERT_EQ(map.anthill_spawns.size(), 1u);
        ASSERT_EQ(map.anthill_spawns[0].y, rows - 1);
        ASSERT_EQ(map.anthill_spawns[0].x, columns - 1);
        ASSERT_LT(map.anthill_spawns[0].y, map.height());
        ASSERT_LT(map.anthill_spawns[0].x, map.width());
        ASSERT_EQ(map.food_schedules.size(), 1u);
        ASSERT_EQ(map.food_schedules[0].y, rows - 2);
        ASSERT_EQ(map.food_schedules[0].x, columns - 1);
        ASSERT_EQ(map.waypoints.size(), 1u);
        ASSERT_EQ(map.waypoints[0].x, columns - 1);
        ASSERT_EQ(map.boundary_param, 4u);
    } TEST_END();

    TEST_CASE("6b.2 The Final Word Is The Egg Stock Whatever It Holds; Filler After It Is Never Read (The Community Map Editor's Template Ends Its Maps With 206 Bytes Of It)") {
        for (uint16_t eggs : {static_cast<uint16_t>(0), static_cast<uint16_t>(2), static_cast<uint16_t>(9), static_cast<uint16_t>(32766)}) {
            const std::vector<uint8_t> plain = build_test_lvl(6, 6, eggs, 0);
            const std::vector<uint8_t> filled = build_test_lvl(6, 6, eggs, 206);
            LevelData a;
            LevelData b;
            ASSERT_TRUE(a.load_from_memory(plain.data(), plain.size()));
            ASSERT_TRUE(b.load_from_memory(filled.data(), filled.size()));
            ASSERT_EQ(a.boundary_param, eggs);
            ASSERT_EQ(b.boundary_param, eggs);
            ASSERT_EQ(b.waypoints.size(), a.waypoints.size());
            ASSERT_EQ(b.layer1_terrain.size(), a.layer1_terrain.size());
            ASSERT_EQ(b.description, std::string("Synthetic map"));
        }
        // a file that stops inside the final word loads (the original catches the read error: its loader's try block holds block 4 and the final word); the egg
        // stock is then the one thing it has not read, and the loader says so (the earlier assertion here, that such a file is refused, was the remake's own rule)
        const std::vector<uint8_t> whole = build_test_lvl(6, 6, 5, 0);
        LevelData c;
        LevelValidation report;
        ASSERT_TRUE(c.load_from_memory(whole.data(), whole.size() - 1, &report));
        ASSERT_EQ(c.boundary_param, 0u);
        ASSERT_TRUE(report.has(LevelProblemKind::EggStockMissing));
        ASSERT_EQ(c.waypoints.size(), 1u);
        // ... but a file that stops before block 4 is refused (the tail is block 3: 4 bytes, block 4: the count and one waypoint without trigger: 2 + 8, the final word: 2)
        LevelData d;
        ASSERT_FALSE(d.load_from_memory(whole.data(), whole.size() - 13, &report));
    } TEST_END();
}

// ============================================================================
// SUITE 6c: The Loader Takes What Ants.exe Takes And Refuses What It Refuses (FUN_01006349 at 0x1006349, docs/GAME_REVERSE_ENGINEERING.md 4.5)
// ============================================================================

// Synthetic maps are made by editing bytes of the shipped TINY.LVL and SMALL.LVL: the helpers find the parts of a file the way the loader walks it.
namespace lvl_edit {

struct Spots {
    size_t dims{0};                    // the rows dword; the columns dword follows
    uint32_t rows{0};
    uint32_t columns{0};
    size_t layer1{0};
    size_t layer2{0};
    size_t b1_count{0};                // block 1: a word count, then (tile, row, column) words
    size_t b1_records{0};
    uint16_t b1{0};
    size_t b2_count{0};                // block 2: a word count, then per object row, column, units, points, stages (words) and `stages` pairs of words
    uint16_t b2{0};
    std::vector<size_t> food;          // the offset of each object
    size_t b3{0};                      // block 3: two words
    size_t b4_count{0};                // block 4: a word count, then per waypoint row, column (words), a flag dword and, when it is not 0, 44 more bytes
    uint16_t b4{0};
    std::vector<size_t> waypoints;     // the offset of each record
    size_t final_word{0};              // the egg stock
};

uint16_t rd16(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); }
uint32_t rd32(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint32_t>(rd16(d, p)) | (static_cast<uint32_t>(rd16(d, p + 2)) << 16); }
void wr16(std::vector<uint8_t>& d, size_t p, uint32_t v) { d[p] = static_cast<uint8_t>(v & 0xFF); d[p + 1] = static_cast<uint8_t>((v >> 8) & 0xFF); }
void wr32(std::vector<uint8_t>& d, size_t p, uint32_t v) { wr16(d, p, v & 0xFFFF); wr16(d, p + 2, v >> 16); }
void put16(std::vector<uint8_t>& d, uint32_t v) { d.push_back(static_cast<uint8_t>(v & 0xFF)); d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); }
void put32(std::vector<uint8_t>& d, uint32_t v) { put16(d, v & 0xFFFF); put16(d, v >> 16); }

std::vector<uint8_t> read_map(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const std::streamsize size = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

bool locate(const std::vector<uint8_t>& d, Spots& s) {
    if (d.size() < 44) return false;
    s = Spots{};
    size_t p = 42 + (static_cast<size_t>(rd16(d, 40)) + 1) * 11;
    s.dims = p;
    s.rows = rd32(d, p);
    s.columns = rd32(d, p + 4);
    p += 8;
    s.layer1 = p;
    p += static_cast<size_t>(s.rows) * s.columns * 6;
    s.layer2 = p;
    p += static_cast<size_t>(s.rows) * s.columns * 6;
    s.b1_count = p;
    s.b1 = rd16(d, p);
    s.b1_records = p + 2;
    p += 2 + static_cast<size_t>(s.b1) * 6;
    s.b2_count = p;
    s.b2 = rd16(d, p);
    p += 2;
    for (size_t i = 0; i < s.b2; ++i) {
        s.food.push_back(p);
        p += 10 + static_cast<size_t>(rd16(d, p + 8)) * 4;
    }
    s.b3 = p;
    p += 4;
    s.b4_count = p;
    s.b4 = rd16(d, p);
    p += 2;
    for (size_t i = 0; i < s.b4; ++i) {
        s.waypoints.push_back(p);
        p += 8 + (rd32(d, p + 4) != 0 ? 44 : 0);
    }
    s.final_word = p;
    return p + 2 <= d.size();
}

// The first Block 1 record of the tile id (152 BSTART, 153 USTART, 154 GSTART, 155 RSTART, 421 flower1): its offset, or 0
size_t record_of_tile(const std::vector<uint8_t>& d, const Spots& s, uint16_t tile) {
    for (size_t i = 0; i < s.b1; ++i) {
        const size_t at = s.b1_records + i * 6;
        if (rd16(d, at) == tile) return at;
    }
    return 0;
}

std::vector<uint8_t> prefix(const std::vector<uint8_t>& d, size_t n) { return std::vector<uint8_t>(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(n)); }

// A flat map from nothing: the header with a dictionary of `names` names, the two dimension dwords as given, `data_rows` x `data_columns` cells of data in each layer
// (empty overlay), and the blocks behind the layers (no start marker, no food, no waypoint, an egg stock of 3)
std::vector<uint8_t> flat_map(uint32_t names, uint32_t rows_dword, uint32_t columns_dword, uint32_t data_rows, uint32_t data_columns) {
    std::vector<uint8_t> b;
    put32(b, 8);
    put32(b, 1);
    put16(b, 6);
    for (int i = 0; i < 30; ++i) b.push_back(0);
    put16(b, names - 1);
    for (uint32_t i = 0; i < names * 11; ++i) b.push_back(0);
    put32(b, rows_dword);
    put32(b, columns_dword);
    for (uint32_t i = 0; i < data_rows * data_columns; ++i) { put16(b, 0); put16(b, 0); put16(b, 0); }              // layer 1: tile 0
    for (uint32_t i = 0; i < data_rows * data_columns; ++i) { put16(b, 0x7FFE); put16(b, 0); put16(b, 0); }        // layer 2: empty
    put16(b, 0);                                                                                                      // block 1
    put16(b, 0);                                                                                                      // block 2
    put16(b, 0);
    put16(b, 0x7FFE);                                                                                                 // block 3
    put16(b, 0);                                                                                                      // block 4
    put16(b, 3);                                                                                                      // the final word
    return b;
}

bool has_fatal(const LevelValidation& v, LevelProblemKind kind) {
    for (const LevelProblem& p : v.problems) {
        if (p.kind == kind && p.severity == LevelProblemSeverity::Fatal) return true;
    }
    return false;
}

bool mentions(const LevelValidation& v, const char* text) { return v.reason().find(text) != std::string::npos; }

bool is_empty_level(const LevelData& m) {
    return m.width() == 0u && m.height() == 0u && m.layer1_terrain.empty() && m.layer2_interactive.empty() && m.anthill_spawns.empty() && m.food_schedules.empty() &&
           m.waypoints.empty() && m.load_notes.empty();
}

}  // namespace lvl_edit

void test_suite_6c_loader_parity(const std::string& map_dir) {
    TEST_SUITE("Suite 6c: The Loader Takes What Ants.exe Takes And Refuses What It Refuses (FUN_01006349, Synthetic Maps Made By Byte Edits Of TINY.LVL And SMALL.LVL)");
    using namespace lvl_edit;

    const std::vector<uint8_t> tiny = read_map(map_dir + "/TINY.LVL");
    const std::vector<uint8_t> small = read_map(map_dir + "/SMALL.LVL");
    Spots t;
    Spots s;
    const bool located = locate(tiny, t) && locate(small, s);

    TEST_CASE("6c.0 The Parts Of The Shipped Maps Are Found Where The Loader Walks (TINY: 31 x 31, 12 start markers, 14 food objects, no waypoint; SMALL: 40 x 40, 2 waypoints)") {
        ASSERT_TRUE(located);
        ASSERT_EQ(t.rows, 31u);
        ASSERT_EQ(t.columns, 31u);
        ASSERT_EQ(t.b1, 12u);
        ASSERT_EQ(t.b2, 14u);
        ASSERT_EQ(t.b4, 0u);
        ASSERT_EQ(t.final_word + 2, tiny.size());
        ASSERT_EQ(s.rows, 40u);
        ASSERT_EQ(s.b4, 2u);
        ASSERT_EQ(s.waypoints.size(), 2u);
        ASSERT_EQ(s.final_word + 2, small.size());
        ASSERT_NE(record_of_tile(tiny, t, 154), 0u);                      // GSTART
        ASSERT_NE(record_of_tile(small, s, 421), 0u);                     // flower1
    } TEST_END();
    if (!located) return;

    TEST_CASE("6c.1 The Mode Dword Is Stored And Never Read (0x100639f): Version 8 With Any Mode Loads, The Editor's Garbage Included") {
        for (uint32_t mode : {0u, 2u, 11047u, 17703u, 26132u, 0xFFFFFFFFu}) {
            std::vector<uint8_t> d = tiny;
            wr32(d, 4, mode);
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_EQ(m.game_mode, mode);
            ASSERT_TRUE(v.parsed);
            ASSERT_TRUE(v.playable);
            ASSERT_TRUE(v.clean());                                       // a note, no warning
            ASSERT_TRUE(v.has(LevelProblemKind::ModeIgnored));
            ASSERT_EQ(m.width(), 31u);                                    // everything else is read as ever
            ASSERT_EQ(m.anthill_spawns.size(), 12u);
            ASSERT_EQ(m.food_schedules.size(), 14u);
            ASSERT_EQ(m.boundary_param, 3u);
            LevelValidation answer = LVLParser::check_memory(d.data(), d.size(), 0x0F);
            ASSERT_TRUE(answer.playable);
        }
        LevelData plain;
        LevelValidation none;
        ASSERT_TRUE(plain.load_from_memory(tiny.data(), tiny.size(), &none));
        ASSERT_TRUE(none.problems.empty());                               // mode 1: nothing to say
    } TEST_END();

    TEST_CASE("6c.2 Only Version 8 Loads (0x1006386: The Original Shows 'The MAP You Tried To Play Is The Wrong Version' And Goes On Without A Map)") {
        for (uint32_t version : {0u, 1u, 6u, 7u, 9u, 66012u, 0xFFFFFFFFu}) {
            std::vector<uint8_t> d = tiny;
            wr32(d, 0, version);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_FALSE(v.parsed);
            ASSERT_FALSE(v.playable);
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::WrongVersion));
            ASSERT_TRUE(mentions(v, "wrong version"));
            ASSERT_TRUE(is_empty_level(m));                               // nothing of the file is left behind
        }
        // the version is the first thing read: it decides before the size does
        std::vector<uint8_t> ten = prefix(tiny, 10);
        wr32(ten, 0, 7);
        LevelData m;
        LevelValidation v;
        ASSERT_FALSE(m.load_from_memory(ten.data(), ten.size(), &v));
        ASSERT_TRUE(has_fatal(v, LevelProblemKind::WrongVersion));
        std::vector<uint8_t> three = prefix(tiny, 3);
        ASSERT_FALSE(m.load_from_memory(three.data(), three.size(), &v));
        ASSERT_TRUE(has_fatal(v, LevelProblemKind::Truncated));           // not even a version to judge
    } TEST_END();

    TEST_CASE("6c.3 A File That Ends Before Block 4 Is Refused Where It Ends (Every Read Outside The Loader's One Try Block Throws 'System Read File Error.')") {
        struct Cut { size_t at; const char* where; };
        const Cut cuts[] = {
            {3, "header"}, {41, "header"},
            {42 + 100, "tile dictionary"}, {t.dims - 1, "tile dictionary"},
            {t.dims + 3, "grid size"}, {t.dims + 6, "grid size"},
            {t.layer1 + 100, "layers"}, {t.layer2, "layers"}, {t.layer2 + 5000, "layers"}, {t.b1_count - 1, "layers"},
            {t.b1_count, "block 1"}, {t.b1_count + 1, "block 1"}, {t.b1_records + 7, "block 1"}, {t.b2_count - 1, "block 1"},
            {t.b2_count, "block 2"}, {t.b2_count + 1, "block 2"}, {t.food[0] + 5, "block 2"}, {t.food[0] + 10 + 1, "block 2"}, {t.food[5] + 12, "block 2"},
            {t.b3 - 1, "block 2"},
            {t.b3, "block 3"}, {t.b3 + 1, "block 3"}, {t.b3 + 2, "block 3"}, {t.b3 + 3, "block 3"},
        };
        for (const Cut& c : cuts) {
            std::vector<uint8_t> d = prefix(tiny, c.at);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_FALSE(v.playable);
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::Truncated));
            ASSERT_TRUE(mentions(v, c.where));
            ASSERT_TRUE(is_empty_level(m));
            ASSERT_FALSE(LVLParser::check_memory(d.data(), d.size(), 0x0F).playable);
        }
        // the first cut that loads is the one that leaves blocks 1 to 3 whole (block 4's count word is the first byte in the loader's try block)
        std::vector<uint8_t> whole_blocks = prefix(tiny, t.b4_count);
        LevelData m;
        ASSERT_TRUE(m.load_from_memory(whole_blocks.data(), whole_blocks.size()));
    } TEST_END();

    TEST_CASE("6c.4 A File That Ends Inside Block 4 Or The Final Word Loads With What Was Read (0x1006510 .. 0x100655e: The Loader Catches The Read Error And Returns 1)") {
        LevelData whole;
        LevelValidation whole_report;
        ASSERT_TRUE(whole.load_from_memory(small.data(), small.size(), &whole_report));
        ASSERT_TRUE(whole_report.clean());
        ASSERT_EQ(whole.waypoints.size(), 2u);
        ASSERT_EQ(whole.boundary_param, 2u);
        const size_t w0 = s.waypoints[0];
        const size_t w1 = s.waypoints[1];

        struct Cut { size_t at; size_t waypoints; bool half; };
        const Cut cuts[] = {
            {s.b4_count, 0, false},                     // the count word is missing
            {s.b4_count + 1, 0, false},                 // half of it
            {s.b4_count + 2, 0, false},                 // the count, no record
            {w0 + 2, 0, false},                         // a row, no column
            {w0 + 4, 1, true},                          // the cell of a record, no flag: its cell is marked solid, the record is dropped
            {w0 + 8, 1, true},                          // the flag, no interval
            {w0 + 8 + 4 + 39, 1, true},                 // the interval and 39 bytes of the five doubles
            {w1, 1, false},                             // the first record complete, the second absent
            {w1 + 6, 2, true},                          // the second record cut inside its flag dword (its cell is whole)
        };
        for (const Cut& c : cuts) {
            std::vector<uint8_t> d = prefix(small, c.at);
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.parsed);
            ASSERT_TRUE(v.playable);                                      // the original plays it, and so does the remake (with defined values)
            ASSERT_FALSE(v.clean());
            ASSERT_TRUE(v.has(LevelProblemKind::WaypointBlockTruncated));
            ASSERT_TRUE(v.has(LevelProblemKind::EggStockMissing));
            ASSERT_EQ(m.waypoints.size(), c.waypoints);
            ASSERT_EQ(m.boundary_param, 0u);                              // the egg stock is uninitialised memory in the original: the remake uses 0
            ASSERT_EQ(m.width(), 40u);                                    // everything before block 4 is whole
            ASSERT_EQ(m.food_schedules.size(), 5u);
            ASSERT_EQ(m.anthill_spawns.size(), 18u);
            if (c.waypoints >= 1 && !(c.half && c.waypoints == 1)) {      // a complete first record is the file's record
                ASSERT_EQ(m.waypoints[0].x, whole.waypoints[0].x);
                ASSERT_EQ(m.waypoints[0].y, whole.waypoints[0].y);
                ASSERT_EQ(m.waypoints[0].flag, whole.waypoints[0].flag);
                ASSERT_EQ(m.waypoints[0].param, whole.waypoints[0].param);
                ASSERT_TRUE(m.waypoints[0].probabilities == whole.waypoints[0].probabilities);
            }
            if (c.half) {                                                 // the half record: the cell, no trigger (so no dropper), as the original's table never holds it
                const Waypoint& h = m.waypoints.back();
                const Waypoint& real = whole.waypoints[c.waypoints - 1];
                ASSERT_EQ(h.x, real.x);
                ASSERT_EQ(h.y, real.y);
                ASSERT_EQ(h.flag, 0u);
            }
        }
        // all records, no (or half a) final word: only the egg stock is missing
        for (size_t at : {s.final_word, s.final_word + 1}) {
            std::vector<uint8_t> d = prefix(small, at);
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.playable);
            ASSERT_TRUE(v.has(LevelProblemKind::EggStockMissing));
            ASSERT_FALSE(v.has(LevelProblemKind::WaypointBlockTruncated));
            ASSERT_EQ(m.waypoints.size(), 2u);
            ASSERT_EQ(m.boundary_param, 0u);
            ASSERT_TRUE(m.waypoints[1].probabilities == whole.waypoints[1].probabilities);
        }
        // TINY has no waypoint: its count word is the last but two bytes
        for (size_t at : {t.b4_count + 1, t.final_word, t.final_word + 1}) {
            std::vector<uint8_t> d = prefix(tiny, at);
            LevelData m;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size()));
            ASSERT_EQ(m.boundary_param, 0u);
            ASSERT_EQ(m.waypoints.size(), 0u);
        }
        // and the whole file with filler behind it is as clean as ever
        std::vector<uint8_t> more = small;
        more.insert(more.end(), 206, 0x55);
        LevelValidation again;
        LevelData m;
        ASSERT_TRUE(m.load_from_memory(more.data(), more.size(), &again));
        ASSERT_TRUE(again.clean());
        ASSERT_EQ(m.boundary_param, 2u);
    } TEST_END();

    TEST_CASE("6c.5 A Food Object Without Stages Ends Block 2 On The Spot (0x1006dcf); Blocks 3, 4 And The Final Word Are Read From The Bytes Behind Its Stage Count") {
        // two good objects of TINY, a third without stages, then bytes that are valid blocks 3 and 4 and a final word, then filler
        std::vector<uint8_t> d = prefix(tiny, t.b2_count);
        put16(d, 9);                                                       // announces nine objects
        d.insert(d.end(), tiny.begin() + static_cast<std::ptrdiff_t>(t.food[0]), tiny.begin() + static_cast<std::ptrdiff_t>(t.food[2]));
        put16(d, 4); put16(d, 5); put16(d, 6); put16(d, 7); put16(d, 0);   // the object without stages
        put16(d, 7); put16(d, 0x7FFE);                                     // block 3 (as read from there)
        put16(d, 1); put16(d, 2); put16(d, 3); put32(d, 0);                // block 4: one waypoint at row 2, column 3, no trigger
        put16(d, 33);                                                      // the final word
        for (int i = 0; i < 10; ++i) d.push_back(0x99);
        LevelData m;
        LevelValidation v;
        ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
        ASSERT_TRUE(v.playable);
        ASSERT_TRUE(v.has(LevelProblemKind::FoodBlockEndedEarly));
        ASSERT_FALSE(v.has(LevelProblemKind::WaypointBlockTruncated));
        ASSERT_FALSE(v.has(LevelProblemKind::EggStockMissing));
        ASSERT_FALSE(v.clean());
        ASSERT_EQ(m.food_schedules.size(), 2u);                            // the two before it stay; it does not
        ASSERT_EQ(m.food_schedules[0].y, 14u);                             // TINY's first two objects (rows 14 and 16)
        ASSERT_EQ(m.food_schedules[1].y, 16u);
        ASSERT_EQ(m.ambient_flag, 7u);
        ASSERT_EQ(m.ambient_tile_or_sound, 0x7FFEu);
        ASSERT_EQ(m.waypoints.size(), 1u);
        ASSERT_EQ(m.waypoints[0].y, 2u);
        ASSERT_EQ(m.waypoints[0].x, 3u);
        ASSERT_EQ(m.boundary_param, 33u);
        ASSERT_EQ(m.anthill_spawns.size(), 12u);                           // block 1 is whole

        // the same with no object before it, and a stage count of 0 on the first object of the shipped TINY (the bytes behind it are no blocks: the original reads them anyway)
        std::vector<uint8_t> e = tiny;
        wr16(e, t.food[0] + 8, 0);
        LevelData n;
        LevelValidation w;
        ASSERT_TRUE(n.load_from_memory(e.data(), e.size(), &w));
        ASSERT_EQ(n.food_schedules.size(), 0u);
        ASSERT_TRUE(w.has(LevelProblemKind::FoodBlockEndedEarly));
        ASSERT_EQ(n.ambient_flag, rd16(e, t.food[0] + 10));                // block 3 starts right behind that stage count word
        ASSERT_EQ(n.ambient_tile_or_sound, rd16(e, t.food[0] + 12));
    } TEST_END();

    TEST_CASE("6c.6 The Grid: The Original Keeps Both Dimensions As 16 Bit Words And Loads An Empty One; The Remake Refuses A Grid Without Cells Or Beyond 256") {
        // 0 x 0, 31 x 0 and 0 x 31: the original loads them (no layer to read) and plays on a map without a cell
        const uint32_t empties[3][2] = {{0, 0}, {31, 0}, {0, 31}};
        for (const auto& dims : empties) {
            std::vector<uint8_t> d = flat_map(4, dims[0], dims[1], 0, 0);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.parsed);                                         // the original's loader completes
            ASSERT_FALSE(v.playable);
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::GridEmpty));
            ASSERT_TRUE(is_empty_level(m));
        }
        // 257 rows: more than the remake hosts (the anchor bytes of a layer-2 object are 8 bit), 256 rows loads
        {
            std::vector<uint8_t> big = flat_map(4, 257, 1, 257, 1);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(big.data(), big.size(), &v));
            ASSERT_TRUE(v.parsed);
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::GridTooLarge));
            std::vector<uint8_t> edge = flat_map(4, 256, 1, 256, 1);
            LevelData n;
            ASSERT_TRUE(n.load_from_memory(edge.data(), edge.size()));
            ASSERT_EQ(n.height(), 256u);
            ASSERT_EQ(n.width(), 1u);
        }
        // the dimension words are 16 bit in the original (0x10006d3 stores `ax`): a dword of 0x10005 is 5 rows, and the file holds 5 rows of data
        {
            std::vector<uint8_t> d = flat_map(4, 0x10005u, 0x20003u, 5, 3);
            LevelData m;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size()));
            ASSERT_EQ(m.height(), 5u);
            ASSERT_EQ(m.width(), 3u);
            ASSERT_EQ(m.layer1_terrain.size(), 15u);
        }
        // a grid that claims more cells than the file holds is a short file (no allocation first)
        {
            std::vector<uint8_t> d = flat_map(4, 60000, 60000, 0, 0);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::Truncated));
            ASSERT_TRUE(mentions(v, "layers"));
        }
    } TEST_END();

    TEST_CASE("6c.7 The Dictionary: 1344 Names Fill The Original's 0x1500 Byte Buffer, More Overrun It (0x100674e); The Remake Refuses The Overrun") {
        for (uint32_t names : {1u, 670u, 1344u}) {
            std::vector<uint8_t> d = flat_map(names, 1, 1, 1, 1);
            LevelData m;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size()));
            ASSERT_EQ(m.tile_dictionary.size(), static_cast<size_t>(names));
        }
        for (uint32_t names : {1345u, 5917u}) {
            std::vector<uint8_t> d = flat_map(names, 1, 1, 1, 1);
            LevelData m;
            LevelValidation v;
            ASSERT_FALSE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.parsed);
            ASSERT_TRUE(has_fatal(v, LevelProblemKind::DictionaryTooLarge));
        }
    } TEST_END();

    TEST_CASE("6c.8 A Start Marker Outside The Grid Has No Behaviour To Copy (0x100ef18 -> 0x100f17f Index The Row Table Unchecked): The Map Is Refused For A Roster That Contains The Team") {
        const size_t g = record_of_tile(tiny, t, 154);                     // GSTART: the green team
        ASSERT_NE(g, 0u);
        const struct { uint32_t row; uint32_t column; bool outside; } places[] = {
            {31, 9, true}, {8, 31, true}, {200, 9, true}, {21512, 21, true}, {65535, 65535, true}, {30, 30, false}, {0, 0, false},
        };
        for (const auto& pl : places) {
            std::vector<uint8_t> d = tiny;
            wr16(d, g + 2, pl.row);
            wr16(d, g + 4, pl.column);
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));       // the original loads it: so does the loader
            ASSERT_TRUE(v.parsed);
            ASSERT_EQ(v.playable, !pl.outside);                            // all four teams
            ASSERT_EQ(m.validate(0x0F).playable, !pl.outside);
            ASSERT_EQ(m.validate(0x01).playable, !pl.outside);             // the green team alone
            ASSERT_TRUE(m.validate(0x0E).playable);                        // the team does not play: its markers are not used
            ASSERT_TRUE(m.validate(0x06).playable);
            ASSERT_EQ(LVLParser::check_memory(d.data(), d.size(), 0x0F).playable, !pl.outside);
            ASSERT_TRUE(LVLParser::check_memory(d.data(), d.size(), 0x0E).playable);
            if (pl.outside) {
                const LevelValidation all = m.validate(0x0F);
                ASSERT_TRUE(has_fatal(all, LevelProblemKind::StartMarkerOutsideGrid));
                const LevelProblem* p = all.first_fatal();
                ASSERT_TRUE(p != nullptr);
                ASSERT_EQ(static_cast<int>(p->team), 0);
                ASSERT_EQ(p->count, 1u);
                ASSERT_TRUE(all.reason().find("green") != std::string::npos);
                ASSERT_TRUE(all.reason().find("outside") != std::string::npos);
                // without that team the same finding is a note
                const LevelValidation rest = m.validate(0x0E);
                ASSERT_TRUE(rest.has(LevelProblemKind::StartMarkerOutsideGrid));
                ASSERT_TRUE(rest.first_fatal() == nullptr);
                ASSERT_TRUE(m.for_roster(0x0E).validate(0x0E).clean());     // the roster's level has dropped the team's markers altogether
            }
        }
        // two teams with a bad marker: each is named; a plant outside the grid is a note (no start marker, nothing is placed through it)
        {
            std::vector<uint8_t> d = small;
            const size_t flower = record_of_tile(d, s, 421);
            const size_t blue = record_of_tile(d, s, 153);
            const size_t black = record_of_tile(d, s, 152);
            ASSERT_TRUE(flower != 0u && blue != 0u && black != 0u);
            wr16(d, flower + 2, 100);
            wr16(d, blue + 4, 77);
            wr16(d, black + 2, 99);
            LevelData m;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size()));
            const LevelValidation all = m.validate(0x0F);
            ASSERT_FALSE(all.playable);
            size_t fatal = 0;
            for (const LevelProblem& p : all.problems) fatal += p.severity == LevelProblemSeverity::Fatal ? 1u : 0u;
            ASSERT_EQ(fatal, 2u);
            ASSERT_TRUE(all.has(LevelProblemKind::ObjectOutsideGrid));
            ASSERT_TRUE(m.validate(0x03).playable);                         // green and red play
            ASSERT_FALSE(m.validate(0x04).playable);                        // blue
            ASSERT_FALSE(m.validate(0x08).playable);                        // black
            ASSERT_TRUE(m.validate(0x03).clean());                          // notes only
        }
    } TEST_END();

    TEST_CASE("6c.9 Objects And Tiles Outside What The Original Checks Are Findings, Not Refusals: Food Outside The Grid, Tile Indexes Outside The Dictionary") {
        {
            std::vector<uint8_t> d = small;
            wr16(d, s.food[0], 100);                                        // food object 0: its row
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.playable);
            ASSERT_TRUE(v.has(LevelProblemKind::FoodOutsideGrid));
            ASSERT_FALSE(v.clean());
        }
        {
            std::vector<uint8_t> d = tiny;                                  // T = 669: 670 names
            wr16(d, t.layer1 + 6 * 40, 700);                                 // a layer-1 cell
            wr16(d, t.layer2 + 6 * 41, 0xFFFF);                              // a layer-2 cell (0x7FFE is the empty tile, this is not)
            wr16(d, t.b1_records, 60000);                                    // a block 1 record
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_TRUE(v.playable);
            ASSERT_TRUE(v.has(LevelProblemKind::TileOutsideDictionary));
            uint32_t count = 0;
            for (const LevelProblem& p : v.problems) {
                if (p.kind == LevelProblemKind::TileOutsideDictionary) count = p.count;
            }
            ASSERT_EQ(count, 3u);
            ASSERT_EQ(m.get_cell_layer1(40 % 31, 40 / 31).tile_index, 700u);   // the raw index is kept
        }
        {
            std::vector<uint8_t> d = tiny;                                  // the empty tile (0x7FFE) is no finding in a layer
            wr16(d, t.layer2 + 6 * 7, 0x7FFE);
            LevelData m;
            LevelValidation v;
            ASSERT_TRUE(m.load_from_memory(d.data(), d.size(), &v));
            ASSERT_FALSE(v.has(LevelProblemKind::TileOutsideDictionary));
        }
    } TEST_END();

    TEST_CASE("6c.10 The Six Shipped Maps Raise No Finding For Any Roster (The Question A Server Asks: One Call, A Yes Or No And The Reasons)") {
        for (const char* name : {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"}) {
            for (uint8_t roster = 1; roster <= 0x0F; ++roster) {
                const LevelValidation v = LVLParser::check_file(map_dir + "/" + name, roster);
                ASSERT_TRUE(v.parsed);
                ASSERT_TRUE(v.playable);
                ASSERT_TRUE(v.problems.empty());
                ASSERT_TRUE(v.clean());
                ASSERT_EQ(v.roster_mask, roster);
                ASSERT_TRUE(v.reason().empty());
                ASSERT_TRUE(v.describe().empty());
            }
        }
        const LevelValidation missing = LVLParser::check_file(map_dir + "/NO_SUCH_MAP.LVL");
        ASSERT_FALSE(missing.playable);
        ASSERT_FALSE(missing.parsed);
        ASSERT_TRUE(has_fatal(missing, LevelProblemKind::Unreadable));
        ASSERT_FALSE(missing.reason().empty());
        ASSERT_TRUE(missing.describe().find("fatal unreadable") != std::string::npos);
        const LevelValidation nothing = LVLParser::check_memory(nullptr, 0);
        ASSERT_FALSE(nothing.playable);
        ASSERT_TRUE(has_fatal(nothing, LevelProblemKind::Unreadable));
        std::vector<uint8_t> empty;
        ASSERT_FALSE(LVLParser::check_memory(empty.data(), empty.size()).playable);
        // the names of the findings and of their severities are distinct and fixed (they travel in reports)
        std::vector<std::string> names;
        for (uint8_t k = 0; k <= static_cast<uint8_t>(LevelProblemKind::ObjectOutsideGrid); ++k) {
            const std::string n = to_string(static_cast<LevelProblemKind>(k));
            ASSERT_FALSE(n.empty());
            ASSERT_NE(n, std::string("?"));
            for (const std::string& other : names) ASSERT_NE(n, other);
            names.push_back(n);
        }
        ASSERT_EQ(std::string(to_string(LevelProblemSeverity::Fatal)), std::string("fatal"));
        ASSERT_EQ(std::string(to_string(LevelProblemSeverity::Warning)), std::string("warning"));
        ASSERT_EQ(std::string(to_string(LevelProblemSeverity::Info)), std::string("note"));
    } TEST_END();

    TEST_CASE("6c.11 A Failed Load Leaves Nothing Behind; A Copy Or A Move Of A Level Keeps What The Loader Noticed") {
        LevelData m;
        ASSERT_TRUE(m.load_from_memory(tiny.data(), tiny.size()));
        ASSERT_EQ(m.width(), 31u);
        std::vector<uint8_t> garbage(500, 0xAB);
        ASSERT_FALSE(m.load_from_memory(garbage.data(), garbage.size()));
        ASSERT_TRUE(is_empty_level(m));
        ASSERT_EQ(m.layer1_cells().size(), 0u);

        std::vector<uint8_t> d = tiny;
        wr32(d, 4, 77);
        LevelData a;
        ASSERT_TRUE(a.load_from_memory(d.data(), d.size()));
        ASSERT_TRUE(a.validate().has(LevelProblemKind::ModeIgnored));
        LevelData copy = a;
        ASSERT_TRUE(copy.validate().has(LevelProblemKind::ModeIgnored));
        LevelData moved = std::move(a);
        ASSERT_TRUE(moved.validate().has(LevelProblemKind::ModeIgnored));
        LevelData assigned;
        assigned = copy;
        ASSERT_TRUE(assigned.validate().has(LevelProblemKind::ModeIgnored));
        LevelData move_assigned;
        move_assigned = std::move(copy);
        ASSERT_TRUE(move_assigned.validate().has(LevelProblemKind::ModeIgnored));
        ASSERT_TRUE(move_assigned.for_roster(0x05).validate().has(LevelProblemKind::ModeIgnored));
    } TEST_END();
}

// ============================================================================
// SUITE 7: 5-to-8 Directional Mirroring Engine Validation
// ============================================================================
void test_suite_7_mirroring(const std::string& chd_path) {
    TEST_SUITE("Suite 7: 5-to-8 Directional Mirroring Engine Validation");

    TEST_CASE("7.1 Angle and Vector to Direction Conversion") {
        // Angles
        ASSERT_EQ(angle_to_direction(0.0f), Direction::North);
        ASSERT_EQ(angle_to_direction(45.0f), Direction::NorthEast);
        ASSERT_EQ(angle_to_direction(90.0f), Direction::East);
        ASSERT_EQ(angle_to_direction(135.0f), Direction::SouthEast);
        ASSERT_EQ(angle_to_direction(180.0f), Direction::South);
        ASSERT_EQ(angle_to_direction(225.0f), Direction::SouthWest);
        ASSERT_EQ(angle_to_direction(270.0f), Direction::West);
        ASSERT_EQ(angle_to_direction(315.0f), Direction::NorthWest);
        ASSERT_EQ(angle_to_direction(360.0f), Direction::North);

        // Vectors: dx = East, -dy = North (+dy = South)
        ASSERT_EQ(vector_to_direction(0, -10), Direction::North);
        ASSERT_EQ(vector_to_direction(10, -10), Direction::NorthEast);
        ASSERT_EQ(vector_to_direction(10, 0), Direction::East);
        ASSERT_EQ(vector_to_direction(10, 10), Direction::SouthEast);
        ASSERT_EQ(vector_to_direction(0, 10), Direction::South);
        ASSERT_EQ(vector_to_direction(-10, 10), Direction::SouthWest);
        ASSERT_EQ(vector_to_direction(-10, 0), Direction::West);
        ASSERT_EQ(vector_to_direction(-10, -10), Direction::NorthWest);
    } TEST_END();

    TEST_CASE("7.2 Horizontal Reflection Transformation Math & Involution") {
        // Frame offset: dx' = -(dx + W), dy' = dy
        int32_t dx = 15;
        uint32_t W = 40;
        int32_t mirrored_dx = mirror_dx(dx, W);
        ASSERT_EQ(mirrored_dx, -(15 + 40)); // -55

        // Involution: mirror_dx(mirrored_dx, W) == dx
        int32_t restored_dx = mirror_dx(mirrored_dx, W);
        ASSERT_EQ(restored_dx, dx);

        // Vertical offset invariant
        ASSERT_EQ(mirror_dy(-12), -12);

        // Bounding box: [left, right] -> [-right, -left]
        int32_t left = -25, right = 10;
        int32_t out_left = 0, out_right = 0;
        mirror_bounding_box(left, right, out_left, out_right);
        ASSERT_EQ(out_left, -10);
        ASSERT_EQ(out_right, 25);

        // Involution
        int32_t restored_left = 0, restored_right = 0;
        mirror_bounding_box(out_left, out_right, restored_left, restored_right);
        ASSERT_EQ(restored_left, left);
        ASSERT_EQ(restored_right, right);

        // In-place aliased bounding box invocation
        int32_t alias_left = -25, alias_right = 10;
        mirror_bounding_box(alias_left, alias_right, alias_left, alias_right);
        ASSERT_EQ(alias_left, -10);
        ASSERT_EQ(alias_right, 25);
    } TEST_END();

    TEST_CASE("7.3 Pixel Reflection Symmetry & Involution") {
        constexpr uint32_t W = 5, H = 2, P = 8;
        std::vector<uint8_t> src = {
            10, 20, 30, 40, 50, 0, 0, 0,
            60, 70, 80, 90, 99, 0, 0, 0
        };
        std::vector<uint8_t> dst(16, 0);
        mirror_pixel_buffer(src.data(), dst.data(), W, H, P);

        std::vector<uint8_t> expected = {
            50, 40, 30, 20, 10, 0, 0, 0,
            99, 90, 80, 70, 60, 0, 0, 0
        };
        ASSERT_EQ(dst, expected);

        // Involution
        std::vector<uint8_t> roundtrip(16, 0);
        mirror_pixel_buffer(dst.data(), roundtrip.data(), W, H, P);
        ASSERT_EQ(roundtrip, src);

        // In-place mirroring test (src == dst)
        std::vector<uint8_t> in_place = src;
        mirror_pixel_buffer(in_place.data(), in_place.data(), W, H, P);
        ASSERT_EQ(in_place, expected);

        // In-place involution test
        mirror_pixel_buffer(in_place.data(), in_place.data(), W, H, P);
        ASSERT_EQ(in_place, src);
    } TEST_END();

    TEST_CASE("7.4 Lunchbox Directional Mapping Suite") {
        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::North)), "7lb");
        ASSERT_FALSE(is_lunchbox_mirrored(Direction::North));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::NorthEast)), "6lb");
        ASSERT_FALSE(is_lunchbox_mirrored(Direction::NorthEast));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::East)), "5lb");
        ASSERT_FALSE(is_lunchbox_mirrored(Direction::East));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::SouthEast)), "4lb");
        ASSERT_FALSE(is_lunchbox_mirrored(Direction::SouthEast));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::South)), "3lb");
        ASSERT_FALSE(is_lunchbox_mirrored(Direction::South));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::SouthWest)), "4lb");
        ASSERT_TRUE(is_lunchbox_mirrored(Direction::SouthWest));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::West)), "5lb");
        ASSERT_TRUE(is_lunchbox_mirrored(Direction::West));

        ASSERT_EQ(std::string(get_lunchbox_prefix(Direction::NorthWest)), "6lb");
        ASSERT_TRUE(is_lunchbox_mirrored(Direction::NorthWest));
    } TEST_END();

    TEST_CASE("7.5 Real Asset Precomputed In-Memory Atlas Lookups") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Test directional sprite lookups
        uint32_t test_sprite_id = 0;
        const Sprite& orig = archive.get_sprite(test_sprite_id);
        const Sprite& mirrored = archive.get_mirrored_sprite(test_sprite_id);

        ASSERT_EQ(orig.width, mirrored.width);
        ASSERT_EQ(orig.height, mirrored.height);
        ASSERT_EQ(orig.pitch, mirrored.pitch);

        // Check horizontal pixel inversion
        for (uint32_t y = 0; y < orig.height; ++y) {
            for (uint32_t x = 0; x < orig.width; ++x) {
                ASSERT_EQ(mirrored.get_pixel(x, y), orig.get_pixel(orig.width - 1 - x, y));
            }
        }

        // Test directional animation lookups for Worker Walk (agwg)
        const AnimationSequence* agwg_e = archive.get_directional_animation("agwg", Direction::East);
        ASSERT_NE(agwg_e, nullptr);
        ASSERT_EQ(agwg_e->name, "agwg901");

        const AnimationSequence* agwg_w = archive.get_directional_animation("agwg", Direction::West);
        ASSERT_NE(agwg_w, nullptr);
        ASSERT_EQ(agwg_w->name, "agwg_mirrored_6");

        // Verify mirrored frame math on real animation
        ASSERT_EQ(agwg_e->subitems.size(), agwg_w->subitems.size());
        for (size_t s = 0; s < agwg_e->subitems.size(); ++s) {
            const auto& e_sub = agwg_e->subitems[s];
            const auto& w_sub = agwg_w->subitems[s];
            ASSERT_EQ(w_sub.box_left, -e_sub.box_right);
            ASSERT_EQ(w_sub.box_right, -e_sub.box_left);

            ASSERT_EQ(e_sub.frames.size(), w_sub.frames.size());
            for (size_t f = 0; f < e_sub.frames.size(); ++f) {
                const auto& ef = e_sub.frames[f];
                const auto& wf = w_sub.frames[f];
                uint32_t spr_w = archive.get_sprite(ef.sprite_index).width;
                ASSERT_EQ(wf.dx, -(ef.dx + static_cast<int32_t>(spr_w)));
                ASSERT_EQ(wf.dy, ef.dy);
            }
        }
    } TEST_END();
}

// ============================================================================
// SUITE 8: Boundary, Corrupted Buffers & Fuzzing Validation
// ============================================================================
void test_suite_8_fuzzing(const std::string& chd_path, const std::string& map_dir) {
    TEST_SUITE("Suite 8: Boundary, Corrupted Buffers & Fuzzing Validation");

    TEST_CASE("8.1 Malformed Header Detection") {
        AssetArchive archive;

        // Buffer smaller than header
        std::vector<uint8_t> tiny(20, 0);
        ASSERT_FALSE(archive.load_from_memory(tiny.data(), tiny.size()));

        // Invalid version
        std::vector<uint8_t> bad_ver(28, 0);
        uint32_t ver = 8; // version < 9 must be rejected
        std::memcpy(bad_ver.data(), &ver, 4);
        ASSERT_FALSE(archive.load_from_memory(bad_ver.data(), bad_ver.size()));

        // Invalid palette byte size
        std::ifstream f(chd_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        std::vector<uint8_t> valid_hdr(1052);
        f.read(reinterpret_cast<char*>(valid_hdr.data()), 1052);
        ASSERT_EQ(f.gcount(), 1052);

        uint32_t bad_pal = 512;
        std::memcpy(valid_hdr.data() + 24, &bad_pal, 4);
        CHDHeader hdr;
        ASSERT_FALSE(CHDParser::parse_header(valid_hdr.data(), valid_hdr.size(), hdr));
    } TEST_END();

    TEST_CASE("8.2 Out of Bounds Safety") {
        AssetArchive archive;
        ASSERT_TRUE(archive.load_from_file(chd_path));

        // Sprite out of bounds
        const Sprite& oob_sprite = archive.get_sprite(99999);
        ASSERT_EQ(oob_sprite.width, 0u);
        ASSERT_EQ(oob_sprite.height, 0u);

        // Sound out of bounds
        const SoundClip& oob_snd = archive.get_sound(99999);
        ASSERT_EQ(oob_snd.pcm_data.size(), 0u);

        // Tag out of bounds
        const EventTag& oob_tag = archive.get_tag(99999);
        ASSERT_TRUE(oob_tag.name.empty());

        // Animation out of bounds
        const AnimationSequence& oob_anim = archive.get_animation(99999);
        ASSERT_TRUE(oob_anim.name.empty());
    } TEST_END();

    TEST_CASE("8.3 Malformed LVL Dimensions & Allocation Bounds") {
        std::string tiny_path = map_dir + "/TINY.LVL";
        std::ifstream lf(tiny_path, std::ios::binary);
        ASSERT_TRUE(lf.is_open());
        std::vector<uint8_t> valid_lvl((std::istreambuf_iterator<char>(lf)),
                                       std::istreambuf_iterator<char>());
        ASSERT_GT(valid_lvl.size(), 42u);

        // Find dimension offset: 42 + (tile_type_count + 1) * 11
        uint16_t tile_count = static_cast<uint16_t>(valid_lvl[40] | (valid_lvl[41] << 8));
        size_t dim_offset = 42 + static_cast<size_t>(tile_count + 1) * 11;
        ASSERT_LT(dim_offset + 8, valid_lvl.size());

        // Test width > 256
        {
            std::vector<uint8_t> bad_lvl = valid_lvl;
            uint32_t big_w = 500;
            std::memcpy(bad_lvl.data() + dim_offset, &big_w, 4);
            LevelData map;
            ASSERT_FALSE(LVLParser::load_from_memory(bad_lvl.data(), bad_lvl.size(), map));
        }

        // Test height > 256
        {
            std::vector<uint8_t> bad_lvl = valid_lvl;
            uint32_t big_h = 500;
            std::memcpy(bad_lvl.data() + dim_offset + 4, &big_h, 4);
            LevelData map;
            ASSERT_FALSE(LVLParser::load_from_memory(bad_lvl.data(), bad_lvl.size(), map));
        }

        // Test adversarial dimensions with truncated buffer (CWE-789 protection)
        {
            std::vector<uint8_t> bad_lvl = valid_lvl;
            bad_lvl.resize(dim_offset + 8); // Truncate right after width/height
            LevelData map;
            ASSERT_FALSE(LVLParser::load_from_memory(bad_lvl.data(), bad_lvl.size(), map));
            ASSERT_EQ(map.layer1_cells().size(), 0u);
        }
    } TEST_END();
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main() {
    std::cout << "=======================================================\n"
              << " Ants Native Asset Decoder Test Suite\n"
              << " Target: libants-assets (Milestone 1)\n"
              << "=======================================================\n";

    std::string assets_dir = locate_assets_dir();
    std::string chd_path = assets_dir + "/ants.chd";
    std::string map_dir = assets_dir + "/Maps";

    std::cout << "Located Assets Directory: " << assets_dir << "\n";
    std::cout << "Target CHD Path:          " << chd_path << "\n";
    std::cout << "Target Maps Directory:    " << map_dir << "\n";

    if (!fs::exists(chd_path)) {
        std::cerr << "ERROR: ants.chd does not exist at " << chd_path << "\n";
        return 1;
    }

    test_suite_1_header_and_palette(chd_path);
    test_suite_2_sprites(chd_path);
    test_suite_3_audio(chd_path);
    test_suite_4_event_tags(chd_path);
    test_suite_5_animations(chd_path);
    test_suite_6_maps(map_dir);
    test_suite_6b_map_layout();
    test_suite_6c_loader_parity(map_dir);
    test_suite_7_mirroring(chd_path);
    test_suite_8_fuzzing(chd_path, map_dir);

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";

    if (g_test_failures == 0) {
        std::cout << " >>> ALL 8 TEST SUITES PASSED CLEANLY (100% PASS) <<<\n";
        return 0;
    } else {
        std::cerr << " >>> TEST SUITE FAILED WITH " << g_test_failures << " FAILURES <<<\n";
        return 1;
    }
}
