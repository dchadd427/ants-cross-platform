#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/**
 * @brief Pedestal kinds of the original command panel (Ants.exe table at 0x1002b68, one animation per kind and role).
 * The left slot shows Move, Ally or Egg (hatch); the right slot shows the ability of the selected ant.
 */
enum class PedestalKind : uint8_t { Hidden = 0, Move, Bomb, Attack, Fire, Thief, Ally, Swim, Egg };

// Animation names of one kind (all parts carry absolute screen coordinates)
std::string pedestal_up_anim(PedestalKind kind);        // resting, raised            e.g. butmovu
std::string pedestal_down_anim(PedestalKind kind);      // resting, pressed           e.g. butmovd
std::string pedestal_press_anim(PedestalKind kind);     // 2-frame press              e.g. butmov2d
std::string pedestal_swap_out_anim(PedestalKind kind);  // icon swap out (trnaXd)     e.g. trnamovd
std::string pedestal_swap_in_anim(PedestalKind kind);   // icon swap in  (trnaXu)     e.g. trnamovu
std::string pedestal_sink_anim(PedestalKind kind);      // pedestal sinks (trnbXd)    e.g. trnbmovd
std::string pedestal_rise_anim(PedestalKind kind);      // pedestal rises (trnbXu)    e.g. trnbmovu (fire: trnbwalu)

/**
 * @brief Chain of animations the original plays when a slot changes from (current kind, mode) to (next kind, mode)
 * (Ants.exe FUN_01028360 -> FUN_01028491; mode 1 = raised, 2 = pressed). The last animation is the resting state;
 * chains that end in Hidden remove the pedestal after their last animation.
 */
struct PedestalChain {
    std::vector<std::string> animations;
    bool remove_after_last{false};
    PedestalKind result_kind{PedestalKind::Hidden};
    int result_mode{1};
};

PedestalChain pedestal_chain(PedestalKind current, int current_mode, PedestalKind next, int next_mode);

/**
 * @brief One pedestal slot: plays the original transition chains in real time and rests on the last animation.
 */
class PedestalSlot {
public:
    // Requests the steady state (kind, mode); the slot plays the transition chain from its current state.
    void request(const assets::AssetArchive& archive, PedestalKind kind, int mode, uint32_t now_ms);
    // Draws the current frame (parts last-first at their absolute coordinates). Returns true if anything was drawn.
    bool draw(IRenderer& renderer, const assets::AssetArchive& archive, uint32_t now_ms);

    bool is_animating() const noexcept { return running_; }
    PedestalKind resting_kind() const noexcept { return kind_; }
    bool is_settled_and_visible() const noexcept { return !running_ && kind_ != PedestalKind::Hidden; }
    void reset() noexcept { *this = PedestalSlot{}; }

private:
    void advance(const assets::AssetArchive& archive, uint32_t now_ms);
    void start_if_needed(uint32_t now_ms);

    PedestalKind kind_{PedestalKind::Hidden};   // state the slot is in once the running chain ends
    int mode_{1};
    PedestalKind want_kind_{PedestalKind::Hidden};
    int want_mode_{1};
    PedestalChain chain_;
    size_t index_{0};
    uint32_t start_ms_{0};
    bool running_{false};
    std::string rest_anim_;                     // animation shown when no chain runs (empty: nothing)
};

} // namespace ants::app
