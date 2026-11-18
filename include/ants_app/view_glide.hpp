#pragma once

// The glide of an ant that the prediction has moved (docs/NETWORK_PORT.md, "Prediction of one's own orders"). When a turn of another player arrives, or an order of the player's own is sealed at
// another tick than the one it was applied at, the predicted engine is rebuilt and an ant that it concerns stands elsewhere than the picture showed it: by what it walked in the ticks that the
// assumption got wrong, up to a tile. The picture does not jump: the ant is drawn where it was shown and slides to where the engine has it in kDurationMs. Drawing and picking only (a click
// picks the ant where it is drawn, and its order names the tile where the engine has it): the engine, the turns and the hashes are untouched; a model without SDL, so that the renderer, the HUD
// and the tests use the same rule.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::app {

class ViewGlide {
public:
    /// How long the slide lasts (ms): long enough to read as walking, short enough that the picture is where the engine has the ant before the next order is given
    static constexpr uint32_t kDurationMs = 150;
    /// The largest shift that is slid (pixels, the larger of the two axes): two tiles. A larger one is no correction of a walk (a throw, a teleport into a hill): it is shown at once
    static constexpr int32_t kMaxShiftPx = 64;

    /// The ant was drawn at the engine's position plus (dx, dy) and the engine now has it at its position: it is drawn there and slides back. A shift that comes while another is sliding is added to
    /// what is left of the first, and the slide starts again
    void add(uint32_t ant, int32_t dx, int32_t dy);
    /// `ms` of the slide have passed (a frame)
    void advance(uint32_t ms);
    /// What is added to the ant's engine position in the picture now (pixels); false (and 0, 0) when the ant is not sliding
    bool offset_of(uint32_t ant, int32_t& dx, int32_t& dy) const;
    /// Nothing slides (the prediction is off: the picture is the confirmed engine's)
    void clear() noexcept { entries_.clear(); }
    size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        uint32_t ant{0};
        int32_t dx{0};              // the offset when the slide began ...
        int32_t dy{0};
        uint32_t left_ms{0};        // ... and the time that is left of it: the offset now is (dx, dy) * left / kDurationMs
    };
    static int32_t scaled(int32_t value, uint32_t left_ms) noexcept;

    std::vector<Entry> entries_;
};

/// How far from where the engine has it the picture draws an ant: what is left of its slide. An ant of another team (not the viewer's, not its ally's) is drawn only on ground that Fog of War has
/// uncovered: when the whole slide would put it on covered ground, it is drawn with the largest quarter of the slide that does not (none: where the engine has it): the slide never shows what the fog
/// hides. False (and 0, 0) when the ant is drawn where the engine has it. The renderer draws by this and the HUD picks by it (a click on a sliding ant picks that ant).
bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy);
/// The ant as the picture draws it: where the engine has it plus glide_offset() (its tile stays the engine's)
sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant);

}  // namespace ants::app
