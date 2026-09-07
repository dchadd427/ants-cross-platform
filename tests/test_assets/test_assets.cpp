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
