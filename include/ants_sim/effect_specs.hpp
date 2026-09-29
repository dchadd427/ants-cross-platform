#pragma once

#include <cstdint>

namespace ants::sim::effect_spec {

// Lifetimes of the Table-4 effect animations, in milliseconds. Each is the sum of the animation's frame durations
// (checked against ants.chd by tests/test_app/test_render_parity.cpp). The original creates these as sprites anchored
// at the tile top-left (Ants.exe FUN_01010008 / FUN_010100ab) that live until their last frame ends.
constexpr uint32_t kBombexMs   = 680;   // bombex   (Table 4 id 133), sound 4 on frame 0
constexpr uint32_t kSputterMs  = 830;   // sputter  (135)
constexpr uint32_t kBsputterMs = 1220;  // bsputter (1177)
constexpr uint32_t kDsplashMs  = 460;   // dsplash  (40)
constexpr uint32_t kBattleMs   = 270;   // battle   (56), one cycle (70 / 60 / 80 / 60 ms), sound 3 at every loop start
// The dust ball that Blast (Ants.exe 0x101c449) puts on the tile of a pile-up of ants that are not the viewer's own: its step
// callback (0x101a329) removes it after 3000 ms, when no ant stands on the tile, or when the crowd has been gone for more than 1000 ms.
constexpr uint32_t kBattleCloudMaxMs   = 3000;
constexpr uint32_t kBattleCloudClearMs = 1000;
constexpr uint32_t kBattleCloudSound   = 3;   // combatnetfairy.wav
constexpr uint32_t kDeathMs[4] = { 920, 1000, 980, 600 };   // death1..death4 (102..105)
constexpr const char* kDeathNames[4] = { "death1", "death2", "death3", "death4" };
constexpr uint32_t kDropperMs  = 820;   // FD_BOMB/COMB/THIEF/SWIM/FIRE (426, 422, 424, 423, 425)
constexpr uint32_t kDropperFrameMs[9] = { 100, 100, 120, 80, 60, 60, 100, 100, 100 };

// Frame shown by a food-dropper animation `elapsed_ms` after it started (holds the last frame).
constexpr uint8_t dropper_frame_at(uint32_t elapsed_ms) noexcept {
    uint32_t end = 0;
    for (uint8_t i = 0; i < 9; ++i) {
        end += kDropperFrameMs[i];
        if (elapsed_ms < end) return i;
    }
    return 8;
}

// Original match-clock rule for the fire-wall and bridge lifetime tasks: they are only created while more than
// 180 s of match time remain (Ants.exe 0x101e8e0, 0x101ec84).
constexpr uint32_t kStructureLifetimeMs = 180000;

} // namespace ants::sim::effect_spec
