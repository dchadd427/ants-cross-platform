#pragma once

// The glide of an ant that the prediction has moved (docs/NETWORK_PORT.md, "Prediction of one's own orders"). When a turn of another player arrives, or an order of the player's own is sealed at
// another tick than the one it was applied at, the predicted engine is rebuilt and an ant that it concerns stands elsewhere than the picture showed it: by what it walked in the ticks that the
// assumption got wrong, up to two tiles. The picture does not jump: the ant is drawn where it was shown and is carried to where the engine has it. How fast: never faster than the ant walks (a
// slide takes the time of its distance at the pace of a walk, at least kMinDurationMs and at most kMaxDurationMs), so that a correction that carries an ant back along its walk reads as the ant
// held up, not as the ant pulled back at two or three times its speed. Drawing and picking only (a click picks the ant where it is drawn, and its order names the tile where the engine has it): the
// engine, the turns and the hashes are untouched; a model without SDL, so that the renderer, the HUD and the tests use the same rule.
//
// The same slide carries the hop of a re-order (`follow`). The engine's own GoTo (the original's, FUN_0101fc50, docs/GAME_REVERSE_ENGINEERING.md "Orders") puts a walking ant that is given a new order on the
// middle of the tile that it is on, at once: up to 12 px back or 16 px forward, in a game of one machine as much as in a network match and for every ant, the player's own and every other's. That is the
// engine and it stays (the snap is what "humping" on mud is made of); the picture draws it as the same slide instead of a jump.
//
// A snap that goes BACK along the way the ant walks (the ant was past the middle of its tile and the order sends it on the same way) is not slid back: the drawn ant would step backward while the engine's ant
// stands for its first ticks, and that reads as a rubber band. The picture HOLDS the ant where the last frame drew it until the engine's ant, which starts walking again after three ticks of standing, has passed that
// place in the direction that it walks (the new order's heading: the engine turns the ant at the end of those ticks), and then goes on with the engine. An ant that does not walk again (a Stop, an order that cannot
// be carried out) is carried to the middle of its tile as before, by the same slide, when the hold ends (the engine says the ant stands, or kHoldMaxMs have passed).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::app {

class ViewGlide {
public:
    /// The pace of a walk (pixels a second): an ant walks 4 px in a tick of 50 ms. A slide is carried at this pace at most
    static constexpr int32_t kWalkPxPerSecond = 80;
    /// The shortest slide (ms): a small correction stays as quick as it was, long enough to read as walking, short enough that the picture is where the engine has the ant before the next order is given
    static constexpr uint32_t kMinDurationMs = 150;
    /// The longest slide (ms). A correction that would take longer at the pace of a walk (more than 56 px along the line) is carried a little faster instead, so that the largest of them does not drag
    static constexpr uint32_t kMaxDurationMs = 700;
    /// The largest shift that is slid (pixels, the larger of the two axes): two tiles. A larger one is no correction of a walk (a throw, a teleport into a hill): it is shown at once
    static constexpr int32_t kMaxShiftPx = 64;
    /// What a walking ant moves in one step of its clip (pixels): a move of the drawn ant between two frames that the walk of the time between them and this one step cover is a walk, not a hop
    static constexpr int32_t kStepPx = 4;
    /// A tile (pixels): an ant that a snap has put on its tile stands on the middle of it, at 16 + 32 n
    static constexpr int32_t kTilePx = 32;
    /// A tick of the engine (ms)
    static constexpr uint32_t kTickMs = 50;
    /// The longest that a held ant stays where it is drawn (ms): the engine's three ticks of standing, its first steps of the new walk (the half tile of a snap is three of them) and a little more. Then it is
    /// carried to the engine's place by the slide, whatever the engine's ant does
    static constexpr uint32_t kHoldMaxMs = 600;

    /// The time that a shift of (dx, dy) pixels takes: its length (along the line, so a diagonal is carried at the pace of a walk too) at kWalkPxPerSecond, between kMinDurationMs and kMaxDurationMs
    static uint32_t duration_ms(int32_t dx, int32_t dy) noexcept;

    /// The ant was drawn at the engine's position plus (dx, dy) and the engine now has it at its position: it is drawn there and slides back. A shift that comes while another is sliding is added to
    /// what is left of the first (the ant goes on from where it is drawn), and the slide starts again, with the time of the sum
    void add(uint32_t ant, int32_t dx, int32_t dy);
    /// `ms` of the slide have passed (a frame)
    void advance(uint32_t ms);
    /// What is added to the ant's engine position in the picture now (pixels); false (and 0, 0) when the ant is not sliding or held
    bool offset_of(uint32_t ant, int32_t& dx, int32_t& dy) const;
    /// True while the picture holds the ant where it was drawn (a snap that went back along its walk, see above)
    bool held(uint32_t ant) const;
    /// Nothing slides and nothing is remembered (the prediction is off, or the page was away: the picture is the confirmed engine's, and the ants have not been seen for a while)
    void clear() noexcept {
        entries_.clear();
        seen_.clear();
        since_follow_ms_ = 0;
        have_match_ms_ = false;
    }
    size_t size() const noexcept { return entries_.size(); }

    /// What the clip of an ant adds to its engine position in the picture (the frames of the clip that have ended since the tick: Renderer::predict_ant_clip)
    using ClipShift = std::function<void(const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy)>;
    /// The look at the ants that the frame is about to draw, once a frame, after the shifts of a rebuild were added and before anything is drawn or picked by them: an ant that the engine has stopped
    /// walking and stood on the middle of its tile since the last look (the snap of a new order, of a stop, of a bump) is carried from where the last frame drew it: the move that the walk of the time
    /// between the two frames does not cover is added as a slide. The ants of every player (what the picture draws from the engine that it draws them from), in the game of one machine and in a network match.
    /// An ant that the picture does not draw is not looked at (nothing is kept of it); an ant that was not in the last look has no last place.
    void follow(const sim::WorldState& world, uint8_t viewer_team, const ClipShift& clip);

private:
    struct Entry {
        uint32_t ant{0};
        int32_t dx{0};              // the offset when the slide began ...
        int32_t dy{0};
        uint32_t left_ms{0};        // ... the time that is left of it ...
        uint32_t total_ms{0};       // ... and the time that it takes in all (duration_ms of the offset): the offset now is (dx, dy) * left / total
        // A hold (an ant that is kept where it is drawn): (dx, dy) is where it is drawn less where the engine's ant is drawn (the engine's place and the clip's frames), made new at every look; it does not slide
        // (left_ms == total_ms), it ages, and it ends when the engine's ant has passed `held_x`, `held_y` in the direction that it walks, or when it stands, or at kHoldMaxMs
        bool hold{false};
        int32_t held_x{0};
        int32_t held_y{0};
        uint32_t age_ms{0};
    };
    Entry* hold_of(uint32_t ant);
    /// A hold ends: the ant is carried from where it is drawn to the engine's place by a slide (at once: nothing when it is there already)
    void end_hold(Entry& e);
    /// What the last look saw of an ant: the engine's place and action, and the place where the frame drew it (the engine's place, the slide and the clip's frames)
    struct Seen {
        uint32_t ant{0};
        int32_t px{0};
        int32_t py{0};
        uint8_t action{0};
        int32_t drawn_x{0};
        int32_t drawn_y{0};
    };
    static int32_t scaled(int32_t value, uint32_t left_ms, uint32_t total_ms) noexcept;

    std::vector<Entry> entries_;
    std::vector<Seen> seen_;                // (by ant id)
    std::vector<Seen> looking_;             // (the look that is made, kept for its memory)
    uint32_t since_follow_ms_{0};           // the time of the frames since the last look
    uint32_t match_ms_{0};                  // the match clock (WorldState::match_time_remaining_ms) at the last look: a replay at speed and a catch-up run more match between two frames than the frames' own time
    bool have_match_ms_{false};
};

/// How far from where the engine has it the picture draws an ant: what is left of its slide. An ant of another team (not the viewer's, not its ally's) is drawn only on ground that Fog of War has
/// uncovered: when the whole slide would put it on covered ground, it is drawn with the largest quarter of the slide that does not (none: where the engine has it): the slide never shows what the fog
/// hides. False (and 0, 0) when the ant is drawn where the engine has it, and for an ant that the picture does not draw at all (its engine tile is covered: it has no place in the picture, whatever
/// slide it has left). The renderer draws by this and the HUD picks by it (a click on a sliding ant picks that ant, and nothing picks an ant that is not drawn).
bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy);
/// The ant as the picture draws it: where the engine has it plus glide_offset() (its tile stays the engine's)
sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant);

}  // namespace ants::app
