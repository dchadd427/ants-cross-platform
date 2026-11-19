#pragma once

// The glide of an ant that the prediction has moved (docs/NETWORK_PORT.md, "Prediction of one's own orders"). When a turn of another player arrives, or an order of the player's own is sealed at
// another tick than the one it was applied at, the predicted engine is rebuilt and an ant that it concerns stands elsewhere than the picture showed it: by what it walked in the ticks that the
// assumption got wrong, up to two tiles. The picture does not jump: the ant is drawn where it was shown and is carried to where the engine has it. How fast: an ant of ANOTHER player (the bot's, an
// opponent's, an ally's) is carried at about the pace of a walk, so that no correction moves it faster than it walks (a correction of a tile takes 0.4 s, one of a tile and a half 0.6 s at most: a longer
// one goes a little faster, never slower than that); the player's OWN ants are never slowed (they are put right in the quick slide: his orders must read at once). Drawing and picking only (a click
// picks the ant where it is drawn, and its order names the tile where the engine has it): the engine, the turns and the hashes are untouched; a model without SDL, so that the renderer, the HUD
// and the tests use the same rule.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::app {

class ViewGlide {
public:
    /// How long the quick slide lasts (ms): the slide of the player's own ants, and the shortest slide of any ant. Long enough to read as walking, short enough that the picture is where the engine has the ant before the next order is given
    static constexpr uint32_t kDurationMs = 150;
    /// The pace that another player's ant is carried at (pixels a second): an ant's walk, 4 px a tick of 50 ms
    static constexpr int32_t kWalkPxPerSecond = 80;
    /// The longest a slide lasts (ms). A correction that would take longer at the pace of a walk (more than 48 px along the line) is carried a little faster instead, so that a far correction does not drag
    static constexpr uint32_t kMaxDurationMs = 600;
    /// The largest shift that is slid (pixels, the larger of the two axes): two tiles. A larger one is no correction of a walk (a throw, a teleport into a hill): it is shown at once
    static constexpr int32_t kMaxShiftPx = 64;

    /// The time that a shift of (dx, dy) pixels takes: the quick slide for an own ant, else the distance at the pace of a walk, at least kDurationMs and at most kMaxDurationMs
    static uint32_t duration_ms(int32_t dx, int32_t dy, bool own) noexcept;

    /// The ant was drawn at the engine's position plus (dx, dy) and the engine now has it at its position: it is drawn there and slides back. `own`: the ant is the player's own (the quick slide; no default: an ant whose owner is not told would be slowed). A shift
    /// that comes while another is sliding is added to what is left of the first, and the slide starts again (its time is the new sum's)
    void add(uint32_t ant, int32_t dx, int32_t dy, bool own);
    /// `ms` of the slide have passed (a frame)
    void advance(uint32_t ms);
    /// What is added to the ant's engine position in the picture now (pixels); false (and 0, 0) when the ant is not sliding
    bool offset_of(uint32_t ant, int32_t& dx, int32_t& dy) const;
    /// How fast and which way the slide pulls the ant now (pixels a second, towards where the engine has it); false (and 0, 0) when the ant is not sliding. The renderer draws a sliding ant walking the way it moves
    bool velocity_of(uint32_t ant, int32_t& vx, int32_t& vy) const;
    /// How far the slide of the ant has carried it since it began (pixels along the line; 0 when it is not sliding): the walk cycle of the drawn ant follows it
    int32_t carried_px(uint32_t ant) const;
    /// The ant slides and is the player's own (the quick slide)
    bool is_own(uint32_t ant) const;
    /// Nothing slides (the prediction is off: the picture is the confirmed engine's)
    void clear() noexcept { entries_.clear(); }
    size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        uint32_t ant{0};
        int32_t dx{0};              // the offset when the slide began ...
        int32_t dy{0};
        uint32_t left_ms{0};        // ... the time that is left of it ...
        uint32_t total_ms{0};       // ... and the time that it takes in all: the offset now is (dx, dy) * left / total
        bool own{false};
    };
    static int32_t scaled(int32_t value, uint32_t left_ms, uint32_t total_ms) noexcept;

    std::vector<Entry> entries_;
};

/// How far from where the engine has it the picture draws an ant: what is left of its slide. An ant of another team (not the viewer's, not its ally's) is drawn only on ground that Fog of War has
/// uncovered: when the whole slide would put it on covered ground, it is drawn with the largest quarter of the slide that does not (none: where the engine has it): the slide never shows what the fog
/// hides. False (and 0, 0) when the ant is drawn where the engine has it, and for an ant that the picture does not draw at all (its engine tile is covered: it has no place in the picture, whatever
/// slide it has left). The renderer draws by this and the HUD picks by it (a click on a sliding ant picks that ant, and nothing picks an ant that is not drawn).
bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy);
/// The ant as the picture draws it: where the engine has it plus glide_offset() (its tile stays the engine's)
sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant);

/// How a sliding ant of another player is drawn. The engine's picture of the ant is the engine's own: its walk clip runs and faces the way the ENGINE moves it, while the slide carries the drawn ant
/// somewhere else (often back along its walk: the corrected engine stands it on the middle of its tile for three ticks), a walk cycle that runs forward on an ant that is carried backward. So such an ant
/// is drawn as it MOVES: its drawn velocity is the engine's walk (the steps of the walk clip it plays over its time: 80 px/s straight on grass, slower on mud, when the ant walks) plus the slide's pull; at kSlideStandsBelowPxPerSecond and more it is
/// drawn walking in the compass heading of that velocity (the walk clip of its terrain; its walk cycle is the engine's while the engine walks, else it follows what the slide has carried it); below
/// that it is drawn standing, facing the way the engine has it (an ant that walks on the spot is never drawn so). `drawn` is `engine` as glided_ant() made it. Everything stays the engine's, and the
/// place stays `drawn`'s (the pointer picks the ant there), for an ant that does not slide, one of the player's own (the quick slide), one that does something else than walk or stand (a blow, a flinch,
/// a throw, a power-up, a swim), and one that the fog cut short (its drawn place is not the whole slide: it does not move as the slide does).
inline constexpr int32_t kSlideStandsBelowPxPerSecond = 24;
sim::AntSnapshot slid_pose(const ViewGlide& glide, const sim::AntSnapshot& engine, const sim::AntSnapshot& drawn);

}  // namespace ants::app
