// ============================================================================
// Movement ground-truth tables: parity of the generated data
// (src/ants_sim/movement_tables_data.inc, written by
// tools/extract_movement_tables.py) with the original game files, and the
// verified locomotion facts of the 1998 game.
//
// Suite 5 reads the static tables of the original program. The program is not
// part of the repository: with a local copy in Original-Ants/Ants.exe the bytes
// come from it, without one from the remake's tables serialised in the
// program's layout; either way every region is checked against its pinned
// SHA-256 digest first (tests/common/original_program_bytes.hpp, test 5.0).
// ============================================================================

#include "ants_sim/movement_tables.hpp"
#include "movement_tables_data.inc"  // generated tables (include directory set by CMake)
#include "original_program_bytes.hpp"  // the bytes of Ants.exe that suite 5 reads, with their pinned digests (tests/common)

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/chd_parser.hpp"
#include "ants_assets/mirroring.hpp"
#include "ants_assets/object_footprint.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "ants_test_paths.hpp"

namespace fs = std::filesystem;
namespace mv = ants::sim::movement;
namespace md = ants::sim::movement::data;
using ants::assets::AnimationSequence;
using ants::assets::AssetArchive;
using ants::assets::Direction;

// ============================================================================
// Lightweight zero-dependency test framework (same style as test_assets.cpp)
// ============================================================================

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;
static int g_skip_count = 0;  // test cases that need Ants.exe itself and are skipped without it
static std::string g_ctx;  // optional context printed with a failure (e.g. "clip 816 frame 3")

#define TEST_SUITE(name)                                                          \
    std::cout << "\n=======================================================\n" \
              << " [SUITE] " << name << "\n"                                      \
              << "=======================================================\n"

static void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    g_ctx.clear();
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    const int prev_fails = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {  // e.g. a read outside the pinned regions of Ants.exe
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev_fails) std::cout << "PASS\n";
}

static void skip_test_case(const std::string& name, const std::string& why) {
    ++g_skip_count;
    std::cout << "  SKIPPED: " << name << " (" << why << ")\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );

static void report_failure(const std::string& what, const char* file, int line) {
    std::cout << "FAILED!\n    Assertion failed: " << what << " at " << file << ":" << line;
    if (!g_ctx.empty()) std::cout << " [" << g_ctx << "]";
    std::cout << "\n";
    ++g_test_failures;
}

#define ASSERT_TRUE(cond)                                   \
    do {                                                    \
        ++g_assert_count;                                   \
        if (!(cond)) {                                      \
            report_failure(#cond, __FILE__, __LINE__);      \
            return;                                         \
        }                                                   \
    } while (0)

// Integer equality; values are printed with integral promotion (so int8_t shows as a number).
#define ASSERT_EQ(a, b)                                                                     \
    do {                                                                                    \
        ++g_assert_count;                                                                   \
        const auto lhs_ = (a);                                                              \
        const auto rhs_ = (b);                                                              \
        if (!(lhs_ == rhs_)) {                                                              \
            report_failure(std::string(#a " == " #b " (") + std::to_string(+lhs_) + " vs " + \
                               std::to_string(+rhs_) + ")",                                 \
                           __FILE__, __LINE__);                                             \
            return;                                                                         \
        }                                                                                   \
    } while (0)

#define ASSERT_STREQ(a, b)                                                                          \
    do {                                                                                            \
        ++g_assert_count;                                                                           \
        const std::string lhs_ = (a);                                                               \
        const std::string rhs_ = (b);                                                               \
        if (lhs_ != rhs_) {                                                                         \
            report_failure(std::string(#a " == " #b " (\"") + lhs_ + "\" vs \"" + rhs_ + "\")",     \
                           __FILE__, __LINE__);                                                     \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

// The folder of the original's data: ants.chd alone decides (Ants.exe is a local copy that a clone does not have).
static std::string locate_assets_dir() {
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

// ============================================================================
// Shared data
// ============================================================================

static const char* const kTypeLetters = "gbftcs";   // CHD name letter per ant type
static const char* const kTerrainLetters = "gswmd"; // per terrain class
static const char* const kDirDigits = "78923";      // CHD name digit per stored direction

// Unit step per direction N, NE, E, SE, S, SW, W, NW (screen: +x east, +y south).
static const int kStepX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int kStepY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

static std::string chd_name(const AssetArchive& archive, uint16_t chd_index) {
    return archive.get_animation(chd_index).name;
}

static uint16_t tile_id(const AssetArchive& archive, const std::string& name) {
    const int32_t id = archive.find_animation_id(name);
    return id < 0 ? uint16_t{0xFFFF} : static_cast<uint16_t>(id);
}

// ============================================================================
// SUITE 1: generated clips == ants.chd Table 4
// ============================================================================
static void suite_chd_parity(const AssetArchive& archive) {
    TEST_SUITE("Suite 1: Generated clips match ants.chd Table 4");

    TEST_CASE("1.1 kClips is sorted and tiles kFrames exactly") {
        size_t next = 0;
        for (size_t i = 0; i < std::size(md::kClips); ++i) {
            const md::ClipRec& c = md::kClips[i];
            g_ctx = "clip #" + std::to_string(i);
            if (i > 0) ASSERT_TRUE(md::kClips[i - 1].chd_index < c.chd_index);
            ASSERT_EQ(static_cast<size_t>(c.first_frame), next);
            ASSERT_TRUE(c.frame_count > 0);
            next += c.frame_count;
        }
        ASSERT_EQ(next, std::size(md::kFrames));
        ASSERT_EQ(mv::clip_count(), std::size(md::kClips));
        ASSERT_EQ(std::size(md::kClips), size_t{520});
        ASSERT_EQ(std::size(md::kFrames), size_t{6123});
    } TEST_END();

    TEST_CASE("1.2 Every frame (dx, dy, duration, event, sound) and clip flags equal the CHD") {
        for (const md::ClipRec& c : md::kClips) {
            const AnimationSequence& anim = archive.get_animation(c.chd_index);
            g_ctx = "CHD " + std::to_string(c.chd_index) + " " + anim.name;
            ASSERT_EQ(anim.subitems.size(), static_cast<size_t>(c.frame_count));
            const uint8_t flags = static_cast<uint8_t>((anim.flag1 != 0 ? 1 : 0) | (anim.flag2 != 0 ? 2 : 0) |
                                                       (anim.flag3 != 0 ? 4 : 0));
            ASSERT_EQ(c.flags, flags);
            for (size_t i = 0; i < c.frame_count; ++i) {
                const md::FrameRec& fr = md::kFrames[c.first_frame + i];
                const auto& sub = anim.subitems[i];
                g_ctx = "CHD " + std::to_string(c.chd_index) + " " + anim.name + " frame " + std::to_string(i);
                ASSERT_EQ(static_cast<int32_t>(fr.dx), sub.val1);
                ASSERT_EQ(static_cast<int32_t>(fr.dy), sub.val2);
                ASSERT_EQ(static_cast<uint32_t>(fr.duration_ms), sub.val3);
                const uint32_t event = (sub.flags == 1111111u) ? 11111u : sub.flags;
                ASSERT_EQ(static_cast<uint32_t>(fr.event), event);
                ASSERT_EQ(static_cast<int32_t>(fr.sound), static_cast<int32_t>(sub.default_sp));
            }
        }
    } TEST_END();

    TEST_CASE("1.3 MotionClip accessors expose the generated frames") {
        for (size_t k = 0; k < mv::clip_count(); ++k) {
            const mv::MotionClip clip = mv::clip_at(k);
            const md::ClipRec& c = md::kClips[k];
            g_ctx = "CHD " + std::to_string(c.chd_index);
            ASSERT_TRUE(clip.valid());
            ASSERT_EQ(clip.chd_index, c.chd_index);
            ASSERT_EQ(clip.count, c.frame_count);
            ASSERT_EQ(clip.flags, c.flags);
            ASSERT_TRUE(!clip.mirrored);
            uint32_t total = 0;
            for (uint16_t i = 0; i < clip.count; ++i) {
                const md::FrameRec& fr = md::kFrames[c.first_frame + i];
                ASSERT_EQ(clip.dx(i), fr.dx);
                ASSERT_EQ(clip.dy(i), fr.dy);
                ASSERT_EQ(clip.duration(i), fr.duration_ms);
                ASSERT_EQ(clip.event(i), fr.event);
                ASSERT_EQ(clip.sound(i), fr.sound);
                total += fr.duration_ms;
            }
            ASSERT_EQ(clip.total_duration_ms(), total);
            ASSERT_EQ(clip.dx(clip.count), int16_t{0});  // out of range
            ASSERT_EQ(clip.sound(clip.count), mv::kSoundNone);
            const mv::MotionClip same = mv::clip_by_chd(c.chd_index);
            ASSERT_TRUE(same.frames == clip.frames && same.count == clip.count);
        }
        ASSERT_TRUE(!mv::clip_by_chd(mv::kNoAnimation).valid());
        ASSERT_TRUE(!mv::clip_by_chd(0).valid());  // not a locomotion animation
        ASSERT_TRUE(!mv::clip_at(mv::clip_count()).valid());
        const mv::MotionClip none;
        ASSERT_EQ(none.total_duration_ms(), 0u);
    } TEST_END();

    TEST_CASE("1.4 Table entries name the expected CHD animations") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (int carry = 0; carry < 2; ++carry) {
                const std::string p = std::string(carry ? "h" : "a") + kTypeLetters[t];
                for (uint8_t tr = 0; tr < mv::kTerrainClassCount; ++tr) {
                    for (uint8_t d = 0; d < 5; ++d) {
                        const uint16_t idx = carry ? md::kCarryWalk[t][tr][d] : md::kWalk[t][tr][d];
                        g_ctx = p + " walk terrain " + std::to_string(tr) + " dir " + std::to_string(d);
                        if (tr == mv::kTerrainWater) {
                            ASSERT_EQ(idx, mv::kNoAnimation);
                        } else {
                            ASSERT_STREQ(chd_name(archive, idx),
                                         p + "w" + kTerrainLetters[tr] + kDirDigits[d] + "01");
                        }
                    }
                }
                for (uint8_t d = 0; d < 5; ++d) {
                    const uint16_t idx = carry ? md::kCarryIdle[t][d] : md::kIdle[t][d];
                    ASSERT_STREQ(chd_name(archive, idx), p + "st" + kDirDigits[d] + "01");
                }
                ASSERT_STREQ(chd_name(archive, carry ? md::kCarryCantGo[t] : md::kCantGo[t]), p + "cg301");
            }
        }
        for (uint8_t d = 0; d < 5; ++d) {
            ASSERT_STREQ(chd_name(archive, md::kSwim[d]), std::string("assw") + kDirDigits[d] + "01");
            ASSERT_STREQ(chd_name(archive, md::kDive[d]), std::string("asdi") + kDirDigits[d] + "01");
            ASSERT_STREQ(chd_name(archive, md::kClimb[d]), std::string("asgo") + kDirDigits[d] + "01");
        }
        ASSERT_EQ(md::kIdleWater, uint16_t{1006});
        ASSERT_STREQ(chd_name(archive, md::kIdleWater), "astw301");
        ASSERT_EQ(md::kBump, uint16_t{0xDC});
        ASSERT_STREQ(chd_name(archive, md::kBump), "bump");
        // Spot values from the reverse-engineering report (worker grass N..S, combat carry idle SE).
        ASSERT_EQ(md::kWalk[0][0][0], uint16_t{817});
        ASSERT_EQ(md::kWalk[0][0][4], uint16_t{816});
        ASSERT_EQ(md::kCarryIdle[4][3], uint16_t{939});
    } TEST_END();

    TEST_CASE("1.5 Action clip tables name the expected CHD animations") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            const std::string a = std::string("a") + kTypeLetters[t];
            const std::string h = std::string("h") + kTypeLetters[t];
            g_ctx = "type " + std::to_string(t);
            ASSERT_STREQ(chd_name(archive, md::kEnter[t]), a + "h0");
            ASSERT_STREQ(chd_name(archive, md::kCarryEnter[t]), h + "h0");
            ASSERT_STREQ(chd_name(archive, md::kBurn[t]), a + "bu301");
            ASSERT_STREQ(chd_name(archive, md::kHatch[t]), a + "hatch");
            ASSERT_STREQ(chd_name(archive, md::kStun[t]), a + "sd301");
            ASSERT_STREQ(chd_name(archive, md::kCarryStun[t]), h + "sd301");
            // the swimmer's row of the drowning table holds the worker clip in the original
            ASSERT_STREQ(chd_name(archive, md::kDrown[t]), (t == mv::kAntSwimmer ? std::string("ag") : a) + "dr301");
            for (uint8_t d = 0; d < 5; ++d) {
                g_ctx = "type " + std::to_string(t) + " dir " + std::to_string(d);
                ASSERT_STREQ(chd_name(archive, md::kHarvest[t][d]), a + "gf" + kDirDigits[d] + "01");
                ASSERT_STREQ(chd_name(archive, md::kAttack[t][d]), a + "at" + kDirDigits[d] + "01");
                ASSERT_STREQ(chd_name(archive, md::kHit[t][d]), a + "gh" + kDirDigits[d] + "01");
                ASSERT_STREQ(chd_name(archive, md::kBlown[t][d]), a + "gb" + kDirDigits[d] + "01");
            }
        }
        for (int d : {0, 2, 4}) {
            g_ctx = "dir " + std::to_string(d);
            const std::string dd = std::string(1, kDirDigits[d]) + "01";
            ASSERT_STREQ(chd_name(archive, md::kIgnite[d]), "afsf" + dd);
            ASSERT_STREQ(chd_name(archive, md::kExtinguish[d]), "afxf" + dd);
            ASSERT_STREQ(chd_name(archive, md::kBridgeBuildWater[d]), "asbbw" + dd);
            ASSERT_STREQ(chd_name(archive, md::kBridgeDemolishWater[d]), "asdbw" + dd);
            ASSERT_STREQ(chd_name(archive, md::kBridgeBuildLand[d]), "asbbl" + dd);
            ASSERT_STREQ(chd_name(archive, md::kBridgeDemolishLand[d]), "asdbl" + dd);
            ASSERT_STREQ(chd_name(archive, md::kPlant[d]), "absb" + dd);
            ASSERT_STREQ(chd_name(archive, md::kDefuse[d]), "abdb" + dd);
        }
        for (int d : {1, 3}) {  // no NE / SE clip for the cardinal-only actions
            ASSERT_EQ(md::kIgnite[d], mv::kNoAnimation);
            ASSERT_EQ(md::kPlant[d], mv::kNoAnimation);
            ASSERT_EQ(md::kBridgeBuildLand[d], mv::kNoAnimation);
        }
        ASSERT_STREQ(chd_name(archive, md::kInfiltrate), "atcr501");
        ASSERT_STREQ(chd_name(archive, md::kGetPow), "getpow");
    } TEST_END();

    TEST_CASE("1.6 action_clip returns the clips with their original durations, events and sounds") {
        using AC = mv::ActionClip;
        // enter the hill (action 2): total ms at full health, the event-5 (heal) frame and the carry variant
        struct Enter { uint8_t type; uint32_t total; uint16_t heal_frame; };
        const Enter enters[6] = {{0, 1000, 8}, {1, 1240, 6}, {2, 1240, 5}, {3, 1000, 8}, {4, 1160, 7}, {5, 880, 7}};
        for (const auto& e : enters) {
            for (int carry = 0; carry < 2; ++carry) {
                const mv::MotionClip c = mv::action_clip(AC::Enter, e.type, 4, carry != 0);
                g_ctx = "enter type " + std::to_string(e.type) + (carry ? " carry" : "");
                ASSERT_TRUE(c.valid());
                ASSERT_EQ(c.total_duration_ms(), e.total);
                ASSERT_EQ(c.event(e.heal_frame), uint16_t{5});
                ASSERT_STREQ(chd_name(archive, c.chd_index).substr(0, 1), std::string(carry ? "h" : "a"));
            }
        }
        // melee attack (action 0x12) facing S: durations, the event-4 (hit) frame and the first sound
        struct Attack { uint8_t type; uint32_t total; uint16_t hit_frame; };
        const Attack attacks[6] = {{0, 420, 3}, {1, 360, 3}, {2, 360, 4}, {3, 720, 6}, {4, 540, 2}, {5, 760, 7}};
        for (const auto& a : attacks) {
            const mv::MotionClip c = mv::action_clip(AC::Attack, a.type, 4, false);
            g_ctx = "attack type " + std::to_string(a.type);
            ASSERT_TRUE(c.valid());
            ASSERT_EQ(c.total_duration_ms(), a.total);
            ASSERT_EQ(c.event(a.hit_frame), uint16_t{4});
        }
        // mirrored directions reuse the stored clip with dx negated
        const mv::MotionClip se = mv::action_clip(AC::Blown, 0, 3, false);
        const mv::MotionClip sw = mv::action_clip(AC::Blown, 0, 5, false);
        ASSERT_TRUE(se.valid() && sw.valid());
        ASSERT_TRUE(!se.mirrored && sw.mirrored);
        ASSERT_EQ(se.chd_index, sw.chd_index);
        ASSERT_EQ(sw.dx(0), static_cast<int16_t>(-se.dx(0)));
        // flight: the blown clip moves 128 px in one step when frame 0 ends
        ASSERT_EQ(action_clip(AC::Blown, 0, 4, false).dy(0), int16_t{-128});
        // other single clips
        ASSERT_EQ(mv::action_clip(AC::GetPow, 0, 0, false).total_duration_ms(), 770u);
        ASSERT_EQ(mv::action_clip(AC::Infiltrate, 3, 0, false).total_duration_ms(), 3510u);
        ASSERT_EQ(mv::action_clip(AC::Drown, 0, 0, false).total_duration_ms(), 2370u);
        ASSERT_EQ(mv::action_clip(AC::Stun, 1, 0, false).total_duration_ms(), 3835u);
        ASSERT_EQ(mv::action_clip(AC::Burn, 1, 0, false).total_duration_ms(), 1610u);
        ASSERT_EQ(mv::action_clip(AC::Hatch, 4, 0, false).total_duration_ms(), 640u);
        ASSERT_EQ(mv::action_clip(AC::Plant, 1, 4, false).total_duration_ms(), 1360u);
        ASSERT_EQ(mv::action_clip(AC::Defuse, 1, 4, false).total_duration_ms(), 1100u);
        ASSERT_EQ(mv::action_clip(AC::Ignite, 2, 4, false).total_duration_ms(), 1760u);
        ASSERT_EQ(mv::action_clip(AC::Extinguish, 2, 4, false).total_duration_ms(), 1200u);
        // death (action 0xC): the variant 0..3 is death1..death4 with their original lengths
        const uint32_t death_ms[4] = {920, 1000, 980, 600};
        for (uint8_t v = 0; v < 4; ++v) {
            const mv::MotionClip c = mv::action_clip(AC::Death, 0, v, false);
            g_ctx = "death " + std::to_string(v);
            ASSERT_TRUE(c.valid());
            ASSERT_EQ(c.total_duration_ms(), death_ms[v]);
            ASSERT_STREQ(chd_name(archive, c.chd_index), "death" + std::to_string(v + 1));
        }
        ASSERT_TRUE(!mv::action_clip(AC::Death, 0, 4, false).valid());
        // cardinal-only actions have no diagonal clip; unknown types are rejected
        ASSERT_TRUE(!mv::action_clip(AC::Plant, 1, 3, false).valid());
        ASSERT_TRUE(!mv::action_clip(AC::Attack, 9, 4, false).valid());
        ASSERT_TRUE(mv::action_clip(AC::Plant, 1, 6, false).valid());
    } TEST_END();
}

// ============================================================================
// SUITE 2: locomotion facts
// ============================================================================
static void suite_locomotion_facts() {
    TEST_SUITE("Suite 2: Verified locomotion facts");

    TEST_CASE("2.1 Walk: grass/sand/dirt 12 x (4 px or (3,3)) at 50/40/60 ms, every type, carrying or not") {
        const uint8_t terrains[3] = {mv::kTerrainGrass, mv::kTerrainSand, mv::kTerrainDirt};
        const uint16_t durations[3] = {50, 40, 60};
        for (int k = 0; k < 3; ++k) {
            for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
                for (int carry = 0; carry < 2; ++carry) {
                    for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
                        const mv::MotionClip clip = mv::walk_clip(t, terrains[k], d, carry != 0);
                        g_ctx = "type " + std::to_string(t) + " terrain " + std::to_string(terrains[k]) +
                                " carry " + std::to_string(carry) + " dir " + std::to_string(d);
                        ASSERT_TRUE(clip.valid());
                        ASSERT_EQ(clip.count, uint16_t{12});
                        const bool diagonal = (d % 2) == 1;
                        for (uint16_t i = 0; i < clip.count; ++i) {
                            ASSERT_EQ(static_cast<int>(clip.dx(i)), kStepX[d] * (diagonal ? 3 : 4));
                            ASSERT_EQ(static_cast<int>(clip.dy(i)), kStepY[d] * (diagonal ? 3 : 4));
                            ASSERT_EQ(clip.duration(i), durations[k]);
                            ASSERT_EQ(clip.event(i), mv::kEventNone);
                            ASSERT_EQ(clip.sound(i), mv::kSoundNone);
                        }
                    }
                }
            }
        }
    } TEST_END();

    TEST_CASE("2.2 Walk: mud 2 px orthogonal, diagonal alternating (1,1)/(2,2), 60 ms") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (int carry = 0; carry < 2; ++carry) {
                for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
                    const mv::MotionClip clip = mv::walk_clip(t, mv::kTerrainMud, d, carry != 0);
                    g_ctx = "type " + std::to_string(t) + " carry " + std::to_string(carry) + " dir " +
                            std::to_string(d);
                    ASSERT_TRUE(clip.valid());
                    ASSERT_EQ(clip.count, uint16_t{12});
                    int ones = 0;
                    int twos = 0;
                    for (uint16_t i = 0; i < clip.count; ++i) {
                        ASSERT_EQ(clip.duration(i), uint16_t{60});
                        ASSERT_EQ(clip.event(i), mv::kEventNone);
                        ASSERT_EQ(clip.sound(i), mv::kSoundNone);
                        if ((d % 2) == 0) {
                            ASSERT_EQ(static_cast<int>(clip.dx(i)), kStepX[d] * 2);
                            ASSERT_EQ(static_cast<int>(clip.dy(i)), kStepY[d] * 2);
                        } else {
                            const int m = clip.dy(i) * kStepY[d];  // magnitude along the direction
                            ASSERT_TRUE(m == 1 || m == 2);
                            ASSERT_EQ(static_cast<int>(clip.dx(i)), kStepX[d] * m);
                            (m == 1 ? ones : twos) += 1;
                            if (i > 0) ASSERT_TRUE(clip.dy(i) != clip.dy(static_cast<uint16_t>(i - 1)));
                        }
                    }
                    if ((d % 2) == 1) {
                        ASSERT_EQ(ones, 6);
                        ASSERT_EQ(twos, 6);
                    }
                }
            }
        }
    } TEST_END();

    TEST_CASE("2.3 Walk: mud diagonal phase per type (1 = starts with the 1 px step)") {
        // Phase table of the reverse-engineering report (section 7.1); SW/NW inherit SE/NE.
        const int walk_ne[6] = {1, 2, 2, 1, 1, 2};
        const int walk_se[6] = {1, 1, 1, 2, 1, 1};
        const int carry_ne[6] = {2, 1, 2, 2, 1, 2};
        const int carry_se[6] = {2, 1, 2, 2, 1, 2};
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            g_ctx = "type " + std::to_string(t);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 1, false).dy(0)), walk_ne[t]);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 7, false).dy(0)), walk_ne[t]);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 3, false).dy(0)), walk_se[t]);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 5, false).dy(0)), walk_se[t]);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 1, true).dy(0)), carry_ne[t]);
            ASSERT_EQ(std::abs(mv::walk_clip(t, mv::kTerrainMud, 3, true).dy(0)), carry_se[t]);
        }
    } TEST_END();

    TEST_CASE("2.4 Walk: no clip for water or out-of-range arguments") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
                ASSERT_TRUE(!mv::walk_clip(t, mv::kTerrainWater, d, false).valid());
                ASSERT_TRUE(!mv::walk_clip(t, mv::kTerrainWater, d, true).valid());
            }
        }
        ASSERT_TRUE(!mv::walk_clip(6, mv::kTerrainGrass, 0, false).valid());
        ASSERT_TRUE(!mv::walk_clip(0, 5, 0, false).valid());
        ASSERT_TRUE(!mv::walk_clip(0, mv::kTerrainGrass, 8, false).valid());
        ASSERT_TRUE(!mv::idle_clip(6, 0, false).valid());
        ASSERT_TRUE(!mv::idle_clip(0, 8, false).valid());
        ASSERT_TRUE(!mv::swim_clip(8).valid());
        ASSERT_TRUE(!mv::cant_go_clip(6, false).valid());
    } TEST_END();

    TEST_CASE("2.5 Swimmer: swim 12 x (3 px or (2,2)) at 40 ms; dive/climb move exactly one tile") {
        for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
            g_ctx = "dir " + std::to_string(d);
            const bool diagonal = (d % 2) == 1;
            const mv::MotionClip swim = mv::swim_clip(d);
            ASSERT_TRUE(swim.valid());
            ASSERT_EQ(swim.count, uint16_t{12});
            for (uint16_t i = 0; i < swim.count; ++i) {
                ASSERT_EQ(static_cast<int>(swim.dx(i)), kStepX[d] * (diagonal ? 2 : 3));
                ASSERT_EQ(static_cast<int>(swim.dy(i)), kStepY[d] * (diagonal ? 2 : 3));
                ASSERT_EQ(swim.duration(i), uint16_t{40});
                ASSERT_EQ(swim.sound(i), mv::kSoundNone);
            }
            const mv::MotionClip dive = mv::dive_clip(d);
            const mv::MotionClip climb = mv::climb_clip(d);
            ASSERT_EQ(dive.count, uint16_t{16});
            ASSERT_EQ(climb.count, uint16_t{7});
            ASSERT_EQ(dive.total_duration_ms(), 1040u);
            ASSERT_EQ(climb.total_duration_ms(), 420u);
            int sx = 0;
            int sy = 0;
            for (uint16_t i = 0; i < dive.count; ++i) {
                sx += dive.dx(i);
                sy += dive.dy(i);
                ASSERT_EQ(dive.sound(i), static_cast<int16_t>(i == 9 ? 80 : -1));
            }
            ASSERT_EQ(sx, kStepX[d] * 32);
            ASSERT_EQ(sy, kStepY[d] * 32);
            sx = 0;
            sy = 0;
            for (uint16_t i = 0; i < climb.count; ++i) {
                sx += climb.dx(i);
                sy += climb.dy(i);
                ASSERT_EQ(climb.sound(i), mv::kSoundNone);
            }
            ASSERT_EQ(sx, kStepX[d] * 32);
            ASSERT_EQ(sy, kStepY[d] * 32);
        }
    } TEST_END();

    TEST_CASE("2.6 Idle clips stand still and play no sounds") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (int carry = 0; carry < 2; ++carry) {
                for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
                    const mv::MotionClip clip = mv::idle_clip(t, d, carry != 0);
                    g_ctx = "type " + std::to_string(t) + " carry " + std::to_string(carry) + " dir " +
                            std::to_string(d);
                    ASSERT_TRUE(clip.valid());
                    for (uint16_t i = 0; i < clip.count; ++i) {
                        ASSERT_EQ(clip.dx(i), int16_t{0});
                        ASSERT_EQ(clip.dy(i), int16_t{0});
                        ASSERT_TRUE(clip.duration(i) > 0);
                        ASSERT_EQ(clip.sound(i), mv::kSoundNone);
                    }
                }
            }
        }
        // First idle frame = walk start latency: 150 ms worker/swimmer/combat (combat facing S 125 ms),
        // 100 ms bomber/fire/thief.
        ASSERT_EQ(mv::idle_clip(mv::kAntWorker, 4, false).duration(0), uint16_t{150});
        ASSERT_EQ(mv::idle_clip(mv::kAntSwimmer, 4, false).duration(0), uint16_t{150});
        ASSERT_EQ(mv::idle_clip(mv::kAntCombat, 4, false).duration(0), uint16_t{125});
        ASSERT_EQ(mv::idle_clip(mv::kAntCombat, 0, false).duration(0), uint16_t{150});
        ASSERT_EQ(mv::idle_clip(mv::kAntBomber, 4, false).duration(0), uint16_t{100});
        ASSERT_EQ(mv::idle_clip(mv::kAntFire, 4, false).duration(0), uint16_t{100});
        ASSERT_EQ(mv::idle_clip(mv::kAntThief, 4, false).duration(0), uint16_t{100});
        const mv::MotionClip water = mv::idle_water_clip();
        ASSERT_TRUE(water.valid());
        ASSERT_EQ(water.chd_index, uint16_t{1006});
        ASSERT_EQ(water.count, uint16_t{85});
        ASSERT_EQ(water.duration(0), uint16_t{40});
        for (uint16_t i = 0; i < water.count; ++i) {
            ASSERT_EQ(water.dx(i), int16_t{0});
            ASSERT_EQ(water.dy(i), int16_t{0});
            ASSERT_EQ(water.sound(i), mv::kSoundNone);
        }
    } TEST_END();

    TEST_CASE("2.7 Can't-go clips play sound 63 on frame 0; bump is 1 frame, 1000 ms, sound 47") {
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (int carry = 0; carry < 2; ++carry) {
                const mv::MotionClip clip = mv::cant_go_clip(t, carry != 0);
                g_ctx = "type " + std::to_string(t) + " carry " + std::to_string(carry);
                ASSERT_TRUE(clip.valid());
                ASSERT_TRUE(!clip.mirrored);
                ASSERT_EQ(clip.sound(0), int16_t{63});
                for (uint16_t i = 0; i < clip.count; ++i) {
                    ASSERT_EQ(clip.dx(i), int16_t{0});
                    ASSERT_EQ(clip.dy(i), int16_t{0});
                    if (i > 0) ASSERT_EQ(clip.sound(i), mv::kSoundNone);
                }
            }
        }
        const mv::MotionClip bump = mv::bump_clip();
        ASSERT_TRUE(bump.valid());
        ASSERT_EQ(bump.chd_index, uint16_t{0xDC});
        ASSERT_EQ(bump.count, uint16_t{1});
        ASSERT_EQ(bump.duration(0), uint16_t{1000});
        ASSERT_EQ(bump.sound(0), int16_t{47});
        ASSERT_EQ(bump.dx(0), int16_t{0});
        ASSERT_EQ(bump.dy(0), int16_t{0});
    } TEST_END();

    TEST_CASE("2.8 Directions 5..7 mirror 3..1: same frames, dx negated") {
        const uint8_t source[3] = {3, 2, 1};  // for dirs 5, 6, 7
        std::vector<std::pair<std::string, std::function<mv::MotionClip(uint8_t)>>> families;
        for (uint8_t t = 0; t < mv::kAntTypeCount; ++t) {
            for (int carry = 0; carry < 2; ++carry) {
                const std::string c = std::to_string(t) + "/" + std::to_string(carry);
                for (uint8_t tr : {mv::kTerrainGrass, mv::kTerrainSand, mv::kTerrainMud, mv::kTerrainDirt}) {
                    families.emplace_back("walk " + c + " terrain " + std::to_string(tr),
                                          [t, carry, tr](uint8_t d) { return mv::walk_clip(t, tr, d, carry != 0); });
                }
                families.emplace_back("idle " + c, [t, carry](uint8_t d) { return mv::idle_clip(t, d, carry != 0); });
            }
        }
        families.emplace_back("swim", [](uint8_t d) { return mv::swim_clip(d); });
        families.emplace_back("dive", [](uint8_t d) { return mv::dive_clip(d); });
        families.emplace_back("climb", [](uint8_t d) { return mv::climb_clip(d); });
        for (const auto& family : families) {
            for (uint8_t d = 0; d < 5; ++d) {
                g_ctx = family.first + " dir " + std::to_string(d);
                ASSERT_TRUE(!family.second(d).mirrored);
            }
            for (int k = 0; k < 3; ++k) {
                const uint8_t d = static_cast<uint8_t>(5 + k);
                const mv::MotionClip m = family.second(d);
                const mv::MotionClip s = family.second(source[k]);
                g_ctx = family.first + " dir " + std::to_string(d);
                ASSERT_TRUE(m.valid() && s.valid());
                ASSERT_TRUE(m.mirrored);
                ASSERT_EQ(m.chd_index, s.chd_index);
                ASSERT_EQ(m.count, s.count);
                ASSERT_EQ(m.flags, s.flags);
                ASSERT_TRUE(m.frames == s.frames);
                for (uint16_t i = 0; i < m.count; ++i) {
                    ASSERT_EQ(static_cast<int>(m.dx(i)), -static_cast<int>(s.dx(i)));
                    ASSERT_EQ(m.dy(i), s.dy(i));
                    ASSERT_EQ(m.duration(i), s.duration(i));
                    ASSERT_EQ(m.event(i), s.event(i));
                    ASSERT_EQ(m.sound(i), s.sound(i));
                }
            }
        }
    } TEST_END();
}

// ============================================================================
// SUITE 3: terrain classes and tile flags
// ============================================================================
static void suite_tiles(const AssetArchive& archive) {
    TEST_SUITE("Suite 3: Terrain classes and tile flags");

    TEST_CASE("3.1 Terrain class of sample tiles") {
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "g01a")), mv::kTerrainGrass);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "s01a")), mv::kTerrainSand);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "w01a")), mv::kTerrainWater);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "M01a")), mv::kTerrainMud);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "d01a")), mv::kTerrainDirt);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "wm05a")), mv::kTerrainWater);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "ms02a")), mv::kTerrainMud);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "sg09a")), mv::kTerrainSand);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "dm04a")), mv::kTerrainDirt);
        // Tiles that are not in the class table are grass (0).
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "fdburgr1")), mv::kTerrainGrass);
        ASSERT_EQ(mv::terrain_class_of_tile(tile_id(archive, "bridge1")), mv::kTerrainGrass);
        ASSERT_EQ(mv::terrain_class_of_tile(mv::kTileIdCount), mv::kTerrainGrass);
        ASSERT_EQ(mv::terrain_class_of_tile(mv::kNoAnimation), mv::kTerrainGrass);
    } TEST_END();

    TEST_CASE("3.2 Every classed tile is named after its terrain; class counts") {
        int per_class[5] = {0, 0, 0, 0, 0};
        for (uint16_t id = 0; id < mv::kTileIdCount; ++id) {
            const uint8_t c = mv::terrain_class_of_tile(id);
            ASSERT_TRUE(c < 5);
            per_class[c] += 1;
            if (c != 0) {
                const std::string name = chd_name(archive, id);
                g_ctx = "tile " + std::to_string(id) + " " + name;
                ASSERT_TRUE(!name.empty());
                ASSERT_EQ(static_cast<char>(std::tolower(static_cast<unsigned char>(name[0]))),
                          kTerrainLetters[c]);
            }
        }
        ASSERT_EQ(per_class[1], 27);
        ASSERT_EQ(per_class[2], 9);
        ASSERT_EQ(per_class[3], 47);
        ASSERT_EQ(per_class[4], 25);
    } TEST_END();

    TEST_CASE("3.3 Bridges and map-cell terrain (bridge on layer 2 -> mud)") {
        const uint16_t g = tile_id(archive, "g01a");
        const uint16_t w = tile_id(archive, "w01a");
        for (const char* name : {"bridge1", "bridge2", "bridge3", "bridge4"}) {
            const uint16_t b = tile_id(archive, name);
            g_ctx = name;
            ASSERT_TRUE(b >= 0x22 && b <= 0x25);
            ASSERT_TRUE(mv::is_bridge_tile(b));
            ASSERT_EQ(mv::terrain_class_of_cell(w, b), mv::kTerrainMud);
            ASSERT_EQ(mv::terrain_class_of_cell(g, b), mv::kTerrainMud);
        }
        ASSERT_TRUE(!mv::is_bridge_tile(0x21));
        ASSERT_TRUE(!mv::is_bridge_tile(0x26));
        ASSERT_TRUE(!mv::is_bridge_tile(mv::kNoAnimation));
        ASSERT_EQ(mv::terrain_class_of_cell(w, mv::kNoAnimation), mv::kTerrainWater);
        ASSERT_EQ(mv::terrain_class_of_cell(g, tile_id(archive, "fdburgr1")), mv::kTerrainGrass);
    } TEST_END();

    TEST_CASE("3.4 Tile flags of sample tiles") {
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "fdburgr1")), uint8_t{0x03});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "pu_bomb")), uint8_t{0x05});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "bigrock1")), uint8_t{0x09});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "flower1")), uint8_t{0x10});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "bombex")), uint8_t{0x20});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "lunchbox")), uint8_t{0x03});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "bridge1")), uint8_t{0x00});
        ASSERT_EQ(mv::tile_flags_of(tile_id(archive, "g01a")), uint8_t{0x00});
        ASSERT_EQ(mv::tile_flags_of(mv::kTileIdCount), uint8_t{0});
        ASSERT_EQ(mv::tile_flags_of(mv::kNoAnimation), uint8_t{0});
        ASSERT_TRUE((mv::tile_flags_of(tile_id(archive, "fdburgr1")) & mv::kTileFlagFood) != 0);
        ASSERT_TRUE((mv::tile_flags_of(tile_id(archive, "pu_bomb")) & mv::kTileFlagPowerUp) != 0);
        ASSERT_TRUE((mv::tile_flags_of(tile_id(archive, "bigrock1")) & mv::kTileFlagSolid) != 0);
    } TEST_END();
}

// ============================================================================
// SUITE 4: passability, path weights, directions
// ============================================================================
static void suite_path_tables() {
    TEST_SUITE("Suite 4: Passability, step weights and direction tables");

    TEST_CASE("4.1 Passability by terrain class") {
        const bool expected[8] = {true, true, false, true, true, false, false, false};
        for (uint8_t c = 0; c < 8; ++c) {
            g_ctx = "class " + std::to_string(c);
            ASSERT_EQ(mv::terrain_walkable(c), expected[c]);
        }
        ASSERT_TRUE(!mv::terrain_walkable(200));
    } TEST_END();

    TEST_CASE("4.2 Step-cost weights (swimmer water 21)") {
        const uint32_t expected[6] = {20, 16, 8000, 48, 24, 40};
        for (uint8_t c = 0; c < 6; ++c) {
            g_ctx = "class " + std::to_string(c);
            ASSERT_EQ(md::kStepWeight[c], expected[c]);
            ASSERT_EQ(mv::terrain_step_weight(c, false), expected[c]);
            ASSERT_EQ(mv::terrain_step_weight(c, true), c == mv::kTerrainWater ? 21u : expected[c]);
        }
        ASSERT_EQ(md::kSwimmerWaterWeight, 21u);
        ASSERT_EQ(mv::terrain_step_weight(9, false), 8000u);
    } TEST_END();

    TEST_CASE("4.3 Direction of a step and neighbour deltas") {
        ASSERT_EQ(mv::dir_from_delta(-1, 0), 0);
        ASSERT_EQ(mv::dir_from_delta(-1, 1), 1);
        ASSERT_EQ(mv::dir_from_delta(0, 1), 2);
        ASSERT_EQ(mv::dir_from_delta(1, 1), 3);
        ASSERT_EQ(mv::dir_from_delta(1, 0), 4);
        ASSERT_EQ(mv::dir_from_delta(1, -1), 5);
        ASSERT_EQ(mv::dir_from_delta(0, -1), 6);
        ASSERT_EQ(mv::dir_from_delta(-1, -1), 7);
        ASSERT_EQ(mv::dir_from_delta(0, 0), 0);
        ASSERT_EQ(mv::dir_from_delta(5, -3), 5);  // reduced to the sign
        for (uint8_t d = 0; d < mv::kDirectionCount; ++d) {
            const mv::TileDelta delta = mv::dir_delta(d);
            g_ctx = "dir " + std::to_string(d);
            ASSERT_EQ(static_cast<int>(delta.drow), kStepY[d]);
            ASSERT_EQ(static_cast<int>(delta.dcol), kStepX[d]);
            ASSERT_EQ(mv::dir_from_delta(delta.drow, delta.dcol), static_cast<int>(d));
        }
        const mv::TileDelta none = mv::dir_delta(8);
        ASSERT_EQ(none.drow, int8_t{0});
        ASSERT_EQ(none.dcol, int8_t{0});
    } TEST_END();
}

// ============================================================================
// SUITE 5: generated tables == static tables inside Ants.exe
// ============================================================================
// `pe` reads Ants.exe when the program is there, else the remake's tables laid out like the program's; 5.0 checks
// either source against the pinned digests (tests/common/original_program_bytes.hpp).
static void suite_exe_parity(const original_program::ProgramBytes& pe) {
    TEST_SUITE("Suite 5: Generated tables match the static tables in Ants.exe");

    TEST_CASE("5.0 The bytes this suite reads equal the pinned SHA-256 digests of Ants.exe (layout, every region, the hash's known answers)") {
        ASSERT_TRUE(pe.loaded());
        for (const std::string& failure : pe.failures()) std::cout << "\n    " << failure << "\n    ";
        g_assert_count += static_cast<int>(pe.checks());  // one check per known-answer set, layout field and region
        ASSERT_EQ(pe.failures().size(), size_t{0});
        if (!pe.from_exe()) {
            // without the program, only the lists of FUN_0100724c are missing (5.3 checks their per-tile results instead)
            ASSERT_EQ(pe.unavailable().size(), size_t{7});
            for (const std::string& region : pe.unavailable()) ASSERT_TRUE(region == "terrain pairs" || region.rfind("flag list", 0) == 0);
        }
    } TEST_END();

    // Ants.exe: the PE32 image was read (as before); the remake's tables: only when they are the original's bytes.
    const bool loaded = pe.from_exe() ? pe.loaded() : pe.verified();

    if (pe.from_exe()) {
        TEST_CASE("5.1 Ants.exe is a PE32 image based at 0x01000000") {
            ASSERT_TRUE(loaded);
            ASSERT_EQ(pe.image_base(), 0x01000000u);
        } TEST_END();
    } else {
        skip_test_case("5.1 Ants.exe is a PE32 image based at 0x01000000",
                       "needs Ants.exe: the PE header is not part of the remake's data; the layout the suite reads through is "
                       "pinned and checked against the header when the program is there");
    }
    if (!loaded) return;

    TEST_CASE("5.2 Animation-index tables (colour-0 block, dirs 0..4)") {
        ASSERT_TRUE(pe.mapped(0x1002FB8, 480) && pe.mapped(0x1003738, 480) && pe.mapped(0x1002CB8, 96) &&
                    pe.mapped(0x1002E38, 96) && pe.mapped(0x1004548, 12) && pe.mapped(0x1004578, 12) &&
                    pe.mapped(0x1004838, 16) && pe.mapped(0x1004878, 16) && pe.mapped(0x10048C0, 16) &&
                    pe.mapped(0x10048B8, 2));
        for (uint32_t t = 0; t < 6; ++t) {
            for (uint32_t tr = 0; tr < 5; ++tr) {
                for (uint32_t d = 0; d < 5; ++d) {
                    const uint32_t off = 2 * (t * 40 + tr * 8 + d);
                    g_ctx = "type " + std::to_string(t) + " terrain " + std::to_string(tr) + " dir " +
                            std::to_string(d);
                    ASSERT_EQ(md::kWalk[t][tr][d], pe.u16(0x1002FB8 + off));
                    ASSERT_EQ(md::kCarryWalk[t][tr][d], pe.u16(0x1003738 + off));
                }
            }
            for (uint32_t d = 0; d < 5; ++d) {
                ASSERT_EQ(md::kIdle[t][d], pe.u16(0x1002CB8 + 2 * (t * 8 + d)));
                ASSERT_EQ(md::kCarryIdle[t][d], pe.u16(0x1002E38 + 2 * (t * 8 + d)));
            }
            ASSERT_EQ(md::kCantGo[t], pe.u16(0x1004548 + 2 * t));
            ASSERT_EQ(md::kCarryCantGo[t], pe.u16(0x1004578 + 2 * t));
        }
        for (uint32_t d = 0; d < 5; ++d) {
            ASSERT_EQ(md::kSwim[d], pe.u16(0x1004838 + 2 * d));
            ASSERT_EQ(md::kDive[d], pe.u16(0x1004878 + 2 * d));
            ASSERT_EQ(md::kClimb[d], pe.u16(0x10048C0 + 2 * d));
        }
        ASSERT_EQ(md::kIdleWater, pe.u16(0x10048B8));
        // push 0xdc in FUN_0101c4f2 (the blocked-walk "bump" effect)
        ASSERT_EQ(pe.u8(0x101CAE3), uint8_t{0x68});
        ASSERT_EQ(static_cast<uint32_t>(md::kBump), pe.u32(0x101CAE4));
    } TEST_END();

    TEST_CASE("5.2b Action clip tables (colour-0 blocks, dirs 0..4)") {
        ASSERT_TRUE(pe.mapped(0x1003EB8, 0x1004918 - 0x1003EB8));
        for (uint32_t t = 0; t < 6; ++t) {
            g_ctx = "type " + std::to_string(t);
            ASSERT_EQ(md::kEnter[t], pe.u16(0x1003EB8 + 2 * t));
            ASSERT_EQ(md::kCarryEnter[t], pe.u16(0x1003EE8 + 2 * t));
            ASSERT_EQ(md::kBurn[t], pe.u16(0x1004518 + 2 * t));
            ASSERT_EQ(md::kHatch[t], pe.u16(0x10045A8 + 2 * t));
            ASSERT_EQ(md::kStun[t], pe.u16(0x10045D8 + 2 * t));
            ASSERT_EQ(md::kCarryStun[t], pe.u16(0x1004608 + 2 * t));
            ASSERT_EQ(md::kDrown[t], pe.u16(0x1004910 + 2 * t));
            for (uint32_t d = 0; d < 5; ++d) {
                const uint32_t off = 2 * (t * 8 + d);
                ASSERT_EQ(md::kHarvest[t][d], pe.u16(0x1003F18 + off));
                ASSERT_EQ(md::kAttack[t][d], pe.u16(0x1004098 + off));
                ASSERT_EQ(md::kHit[t][d], pe.u16(0x1004218 + off));
                ASSERT_EQ(md::kBlown[t][d], pe.u16(0x1004398 + off));
            }
        }
        for (uint32_t d = 0; d < 5; ++d) {
            g_ctx = "dir " + std::to_string(d);
            ASSERT_EQ(md::kIgnite[d], pe.u16(0x1004638 + 2 * d));
            ASSERT_EQ(md::kExtinguish[d], pe.u16(0x1004678 + 2 * d));
            ASSERT_EQ(md::kBridgeBuildWater[d], pe.u16(0x10046B8 + 2 * d));
            ASSERT_EQ(md::kBridgeDemolishWater[d], pe.u16(0x10046F8 + 2 * d));
            ASSERT_EQ(md::kBridgeBuildLand[d], pe.u16(0x1004738 + 2 * d));
            ASSERT_EQ(md::kBridgeDemolishLand[d], pe.u16(0x1004778 + 2 * d));
            ASSERT_EQ(md::kPlant[d], pe.u16(0x10047B8 + 2 * d));
            ASSERT_EQ(md::kDefuse[d], pe.u16(0x10047F8 + 2 * d));
        }
        ASSERT_EQ(md::kInfiltrate, pe.u16(0x1004900));
        ASSERT_EQ(md::kGetPow, pe.u16(0x1004908));
    } TEST_END();

    TEST_CASE("5.3 Terrain-class pairs and tile-flag lists (FUN_0100724c)") {
        ASSERT_TRUE(pe.mapped(0x1001360, 0x1001574 - 0x1001360));
        struct FlagList {
            uint8_t bit;
            uint32_t va;
            uint32_t count;
            uint32_t stride;
        };
        const FlagList lists[6] = {{0x01, 0x1001838, 0xC0, 2}, {0x02, 0x10019C0, 0x57, 2},
                                   {0x04, 0x1001AD8, 0x05, 2}, {0x08, 0x1001578, 0x61, 2},
                                   {0x10, 0x1001AF8, 0x0E, 12}, {0x20, 0x1001818, 0x0B, 2}};
        if (pe.has("terrain pairs")) {  // Ants.exe: the original's lists
            uint8_t terrain[1344] = {};
            for (uint32_t va = 0x1001360; va < 0x1001574; va += 4) {
                const uint16_t id = pe.u16(va);
                ASSERT_TRUE(id < 1344);
                terrain[id] = static_cast<uint8_t>(pe.u16(va + 2));
            }
            uint8_t flags[1344] = {};
            for (const FlagList& l : lists) {
                ASSERT_TRUE(pe.mapped(l.va, l.count * l.stride));
                for (uint32_t k = 0; k < l.count; ++k) {
                    const uint16_t id = pe.u16(l.va + k * l.stride);
                    ASSERT_TRUE(id < 1344);
                    flags[id] = static_cast<uint8_t>(flags[id] | l.bit);
                }
            }
            for (uint32_t id = 0; id < 1344; ++id) {
                g_ctx = "tile " + std::to_string(id);
                ASSERT_EQ(md::kTileTerrain[id], terrain[id]);
                ASSERT_EQ(md::kTileFlags[id], flags[id]);
            }
            // the pins that the run without the program checks: the per-tile arrays these lists produce
            g_ctx.clear();
            ASSERT_STREQ(original_program::sha256_hex(terrain, sizeof terrain), original_program::kTileTerrainSha256);
            ASSERT_STREQ(original_program::sha256_hex(flags, sizeof flags), original_program::kTileFlagsSha256);
        } else {
            // Without the program the lists cannot be read: the remake keeps their per-tile results, not the lists (their
            // order and the pair list's 25 explicit grass entries are not in its data). The per-tile arrays are checked
            // against the pinned digests of the arrays that the original's lists produce (verified by the run with Ants.exe).
            std::cout << "[without Ants.exe the original's terrain-pair and tile-flag lists are not read; kTileTerrain and "
                         "kTileFlags are checked against the pinned digests of the per-tile arrays those lists produce] ";
            for (const FlagList& l : lists) ASSERT_TRUE(pe.mapped(l.va, l.count * l.stride));
            ASSERT_STREQ(original_program::sha256_hex(md::kTileTerrain, sizeof md::kTileTerrain), original_program::kTileTerrainSha256);
            ASSERT_STREQ(original_program::sha256_hex(md::kTileFlags, sizeof md::kTileFlags), original_program::kTileFlagsSha256);
        }
        // Bridge ids: cmp ax, imm16 at 0x1008b95 / 9b / a1 / a7 (FUN_01008b90).
        const uint32_t cmps[4] = {0x1008B95, 0x1008B9B, 0x1008BA1, 0x1008BA7};
        for (int k = 0; k < 4; ++k) {
            ASSERT_EQ(pe.u16(cmps[k]), uint16_t{0x3D66});  // 66 3d = cmp ax, imm16
            ASSERT_EQ(md::kBridgeTiles[k], pe.u16(cmps[k] + 2));
        }
    } TEST_END();

    TEST_CASE("5.4 Passability, step weights, swimmer weight, direction and neighbour tables") {
        ASSERT_TRUE(pe.mapped(0x10049B8, 16) && pe.mapped(0x10049C8, 24) && pe.mapped(0x1002B28, 18) &&
                    pe.mapped(0x1004950, 64));
        for (uint32_t c = 0; c < 8; ++c) ASSERT_EQ(static_cast<uint16_t>(md::kPassableByTerrain[c]), pe.u16(0x10049B8 + 2 * c));
        for (uint32_t c = 0; c < 6; ++c) ASSERT_EQ(md::kStepWeight[c], pe.u32(0x10049C8 + 4 * c));
        // FUN_010208e8: cmp ax, 5; jne +5; push imm8
        ASSERT_EQ(pe.u16(0x10208FB), uint16_t{0x3D66});
        ASSERT_EQ(pe.u8(0x1020901), uint8_t{0x6A});
        ASSERT_EQ(md::kSwimmerWaterWeight, static_cast<uint32_t>(pe.u8(0x1020902)));
        for (uint32_t i = 0; i < 9; ++i) ASSERT_EQ(static_cast<int16_t>(md::kDirTable[i]), pe.i16(0x1002B28 + 2 * i));
        for (uint32_t d = 0; d < 8; ++d) {
            ASSERT_EQ(static_cast<int32_t>(md::kNeighbour[d][0]), pe.i32(0x1004950 + 8 * d));
            ASSERT_EQ(static_cast<int32_t>(md::kNeighbour[d][1]), pe.i32(0x1004954 + 8 * d));
        }
    } TEST_END();
}

// ============================================================================
// SUITE 6: signed per-frame displacement in the CHD parser / asset archive
// ============================================================================
static void suite_archive_signed_dx(const AssetArchive& archive) {
    TEST_SUITE("Suite 6: AssetArchive per-frame displacement is signed and mirrored");

    TEST_CASE("6.1 val1/val2 are signed dx/dy") {
        const AnimationSequence* north = archive.find_animation("agwg701");
        ASSERT_TRUE(north != nullptr);
        ASSERT_EQ(north->subitems.size(), size_t{12});
        ASSERT_EQ(north->subitems[0].val1, 0);
        ASSERT_EQ(north->subitems[0].val2, -4);
        const AnimationSequence* ne = archive.find_animation("agwg801");
        ASSERT_TRUE(ne != nullptr);
        ASSERT_EQ(ne->subitems[0].val1, 3);
        ASSERT_EQ(ne->subitems[0].val2, -3);
    } TEST_END();

    TEST_CASE("6.2 Mirrored directional copies negate val1 and keep val2 and val3") {
        const struct {
            Direction mirrored;
            Direction source;
        } pairs[3] = {{Direction::SouthWest, Direction::SouthEast},
                      {Direction::West, Direction::East},
                      {Direction::NorthWest, Direction::NorthEast}};
        for (const char* prefix : {"agwg", "hgwm", "assw", "asdi"}) {
            for (const auto& p : pairs) {
                const AnimationSequence* m = archive.get_directional_animation(prefix, p.mirrored);
                const AnimationSequence* s = archive.get_directional_animation(prefix, p.source);
                g_ctx = std::string(prefix) + " dir " + std::to_string(static_cast<int>(p.mirrored));
                ASSERT_TRUE(m != nullptr && s != nullptr);
                ASSERT_EQ(m->subitems.size(), s->subitems.size());
                for (size_t i = 0; i < s->subitems.size(); ++i) {
                    ASSERT_EQ(m->subitems[i].val1, -s->subitems[i].val1);
                    ASSERT_EQ(m->subitems[i].val2, s->subitems[i].val2);
                    ASSERT_EQ(m->subitems[i].val3, s->subitems[i].val3);
                }
            }
        }
        const AnimationSequence* west = archive.get_directional_animation("agwg", Direction::West);
        ASSERT_TRUE(west != nullptr);
        ASSERT_EQ(west->subitems[0].val1, -4);
        ASSERT_EQ(west->subitems[0].val2, 0);
    } TEST_END();
}

// ============================================================================
// Suite 7: food footprints (src/ants_sim/food_footprints_data.inc) vs ants.chd
// ============================================================================

static void suite_food_footprints(const AssetArchive& archive) {
    TEST_SUITE("Suite 7: Food footprints (cells of a food tile around its anchor)");

    TEST_CASE("7.1 The committed footprint table equals the cells computed from ants.chd, for every food tile") {
        size_t food_tiles = 0;
        size_t total_cells = 0;
        for (uint32_t tile = 0; tile < mv::kTileIdCount; ++tile) {
            const mv::FootprintSpan span = mv::food_footprint(static_cast<uint16_t>(tile));
            const bool is_food = (mv::tile_flags_of(static_cast<uint16_t>(tile)) & mv::kTileFlagFood) != 0;
            g_ctx = "tile " + std::to_string(tile);
            if (!is_food) {
                ASSERT_EQ(span.count, size_t{0});
                continue;
            }
            ++food_tiles;
            const auto cells = ants::assets::compute_object_footprint(archive, tile);
            ASSERT_EQ(span.count, cells.size());
            total_cells += cells.size();
            for (size_t k = 0; k < cells.size(); ++k) {
                ASSERT_EQ(span.cells[k].dcol, cells[k].dcol);
                ASSERT_EQ(span.cells[k].drow, cells[k].drow);
            }
        }
        g_ctx.clear();
        ASSERT_EQ(food_tiles, size_t{87});
        ASSERT_EQ(total_cells, size_t{545});
    } TEST_END();

    TEST_CASE("7.2 Sample footprints: lunchbox 1 cell, crackers 2x2 up-left of the anchor, burger 4x4 around it") {
        const mv::FootprintSpan lunch = mv::food_footprint(356);
        ASSERT_EQ(lunch.count, size_t{1});
        ASSERT_EQ(lunch.cells[0].dcol, 0);
        ASSERT_EQ(lunch.cells[0].drow, 0);
        const mv::FootprintSpan crackers = mv::food_footprint(369);
        ASSERT_EQ(crackers.count, size_t{4});
        const int expect_cr[4][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}};
        for (size_t k = 0; k < 4; ++k) {
            ASSERT_EQ(crackers.cells[k].dcol, expect_cr[k][0]);
            ASSERT_EQ(crackers.cells[k].drow, expect_cr[k][1]);
        }
        const mv::FootprintSpan burger = mv::food_footprint(253);
        ASSERT_EQ(burger.count, size_t{16});
        ASSERT_EQ(burger.cells[0].dcol, -2);
        ASSERT_EQ(burger.cells[0].drow, -2);
        ASSERT_EQ(burger.cells[15].dcol, 1);
        ASSERT_EQ(burger.cells[15].drow, 1);
    } TEST_END();
}

int main() {
    std::cout << "=======================================================\n"
              << " Ants Movement Ground-Truth Table Test Suite\n"
              << "=======================================================\n";

    const std::string assets_dir = locate_assets_dir();
    const std::string chd_path = assets_dir + "/ants.chd";
    const std::string exe_path = assets_dir + "/Ants.exe";  // optional: a local copy of the original program
    std::cout << "Assets directory: " << assets_dir << "\n";
    if (!fs::exists(chd_path)) {
        std::cerr << "ERROR: ants.chd not found in " << assets_dir << "\n";
        return 1;
    }

    AssetArchive archive;
    if (!archive.load_from_file(chd_path)) {
        std::cerr << "ERROR: failed to load " << chd_path << "\n";
        return 1;
    }

    // The bytes of Ants.exe that suite 5 reads: from the program when it is there, else from the remake's tables.
    const original_program::ProgramBytes program =
        original_program::ProgramBytes::open(exe_path, original_program::all_region_names());
    std::cout << "Bytes of the original program: " << program.summary() << "\n";

    suite_chd_parity(archive);
    suite_locomotion_facts();
    suite_tiles(archive);
    suite_path_tables();
    suite_exe_parity(program);
    suite_archive_signed_dx(archive);
    suite_food_footprints(archive);

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n";
    if (g_skip_count > 0) {
        std::cout << " Skipped:          " << g_skip_count << " (they need Ants.exe itself: put your own copy of the original Ants.exe in Original-Ants/)\n";
    }
    std::cout << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL MOVEMENT TABLE TESTS PASSED (100% PASS) <<<\n";
        return 0;
    }
    std::cerr << " >>> MOVEMENT TABLE TESTS FAILED WITH " << g_test_failures << " FAILURES <<<\n";
    return 1;
}
