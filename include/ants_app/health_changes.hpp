#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ants_app/health_marks.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app::health {

// Damage and heal numbers (docs/VIEW_AND_HUD.md, "Damage and heal numbers"): a small "-N" over an ant that loses hit points and "+N" over an ant that is healed at its hill. This file is the rule with no window and no
// SDL, so that a test can run it: which ants make a number, how numbers merge, how long they live and where they stand. Renderer::draw_health_changes (src/ants_app/renderer_health.cpp) draws them. View only, like the
// ant health marks: the tracker READS the world state that every client already has (the confirmed engine's, once after every executed tick) and compares the hit points of each ant id with the ones it saw the tick
// before. Nothing here is read by the simulation, a recording, a hash or the network.

/// Is the ant drawn in the world pass? The one gate of the picture: the local player's ants, the ally's, and every other ant only on explored ground (Fog of War). Renderer::collect_ant_units and the tracker both ask it, so
/// a number is made only for an ant that the picture shows.
inline bool ant_drawn(const ants::sim::WorldState& world, uint8_t team, const ants::sim::AntSnapshot& a) noexcept {
    const bool allied = team < world.player_alliances.size() && a.player_id < world.player_alliances.size() &&
                        world.player_alliances[team] == a.player_id && world.player_alliances[a.player_id] == team;
    return !(world.fog_of_war_enabled && a.player_id != team && !allied && !world.is_tile_revealed(a.tile_x, a.tile_y));
}

// ---- the numbers' own rules ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kLifeMs = 600;            // a number lives this long: it rises, is solid for kSolidMs, then fades to nothing
constexpr int32_t kSolidMs = 300;
constexpr int32_t kMergeMs = 250;           // a new loss within this long of the start of the ant's live damage number is added to it
constexpr int32_t kRisePx = 8;              // the rise over the life, at an even pace
constexpr int32_t kLineHeight = 12;         // a second number of the same ant stands one line above the first
constexpr int32_t kMaxLive = 48;            // on the whole screen (the oldest goes first)
constexpr int32_t kMaxPerSign = 2;          // for one ant
constexpr uint32_t kJumpTicks = 10;         // more ticks than this between two looks is a jump: nothing is shown for it
constexpr int32_t kGlyphAdvance = 7;        // a sign and a digit take 7 pixels each (the font's cell is 8 wide)
constexpr int32_t kGlyphHeight = 10;
constexpr int32_t kAboveBar = 2;            // the glyphs' bottom stands this far above where the health bar's top would be
constexpr int32_t kChangeScaleMax = 2;       // on a canvas that is shown small (a phone) the numbers grow with the health numbers' rule, but not beyond this
constexpr uint8_t kOutlineAlpha = 217;      // the outline's near black at about 85 %

constexpr Color kDamageColor{255, 255, 255};     // the color of the original's Ctrl+L numbers
constexpr Color kHealColor{132, 236, 120};

/// The opacity (0 .. 255) of a number of this age: solid for kSolidMs, then a linear fade to nothing at kLifeMs
constexpr int32_t alpha_at(int32_t age_ms) noexcept {
    if (age_ms < 0 || age_ms >= kLifeMs) return 0;
    if (age_ms <= kSolidMs) return 255;
    return (255 * (kLifeMs - age_ms) + (kLifeMs - kSolidMs) / 2) / (kLifeMs - kSolidMs);
}
/// How far the number has risen at this age (screen pixels before the scale), at an even pace
constexpr int32_t rise_at(int32_t age_ms) noexcept {
    if (age_ms <= 0) return 0;
    if (age_ms >= kLifeMs) return kRisePx;
    return (kRisePx * age_ms + kLifeMs / 2) / kLifeMs;
}

/// One live number
struct Number {
    uint32_t id{0};             // the ant
    bool heal{false};
    int32_t amount{0};          // always positive: "-amount" or "+amount" is what is drawn
    int64_t start_ms{0};        // the clock when it began (a merge starts it again)
    int32_t wx{0};              // where the ant's sprite stands, in world pixels (the picture of the ant moves it, the last place is kept for a dying ant)
    int32_t wy{0};
    uint8_t level{0};           // 0 for the first number of an ant, 1 one line above it, and so on
    bool hidden{false};         // the ant is in the game but the picture does not draw it this frame (fog): its number is not drawn either, whatever the tracker believed
};

class ChangeTracker {
public:
    /// Forget every starting value and every live number. Called when a match begins, when a jump is seen and when the setting goes Off
    void reset() noexcept {
        seen_.clear();
        count_ = 0;
        have_tick_ = false;
    }

    /// Look at the world after one executed tick. `tick` is the engine's tick number, `now_ms` the numbers' own clock, `team` the player whose picture it is (the fog gate).
    void on_tick(const ants::sim::WorldState& world, uint32_t tick, int64_t now_ms, uint8_t team) {
        const bool jump = have_tick_ && (tick < last_tick_ || tick - last_tick_ > kJumpTicks);
        if (jump) reset();                                              // (a catch-up screen, a rejoin, a seek in a replay: nothing is shown for it)
        const bool baseline = !have_tick_;
        have_tick_ = true;
        last_tick_ = tick;
        for (const ants::sim::AntSnapshot& a : world.ants) {
            if (a.id >= kMaxIds) continue;
            if (a.id >= seen_.size()) seen_.resize(std::max<size_t>(static_cast<size_t>(a.id) + 1u, seen_.size() * 2u));
            Seen& s = seen_[a.id];
            if (!s.known || baseline) {                                // the first look at an id (a hatch, an ant that comes into view) only sets its starting hit points
                s = Seen{a.hp, 0, true};
                continue;
            }
            const bool shown = ant_drawn(world, team, a);
            if (a.hp < s.hp) {
                if (shown) add(a.id, false, static_cast<int32_t>(s.hp - a.hp), now_ms, a.px, a.py);
                s.held = 0;
            } else if (a.hp > s.hp) {
                s.held = static_cast<uint16_t>(s.held + (a.hp - s.hp));      // (0 -> 1 -> 10 is one +10)
            }
            s.hp = a.hp;
            if (s.held > 0 && a.state != ants::sim::UnitState::EnteringBase) {   // the ant is out of the hill: the whole sum is one number
                if (shown) add(a.id, true, static_cast<int32_t>(s.held), now_ms, a.px, a.py);
                s.held = 0;
            }
        }
    }

    /// Drop the numbers that have lived their time
    void expire(int64_t now_ms) noexcept {
        size_t keep = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (now_ms - items_[i].start_ms < kLifeMs) items_[keep++] = items_[i];
        }
        count_ = keep;
    }

    /// The picture's place of the ant `id` this frame (the sprite's world position); a number whose ant is gone keeps the last one
    void note_position(uint32_t id, int32_t wx, int32_t wy) noexcept {
        for (size_t i = 0; i < count_; ++i) {
            if (items_[i].id == id) {
                items_[i].wx = wx;
                items_[i].wy = wy;
                items_[i].hidden = false;
            }
        }
    }

    /// The ant `id` is in the game and the picture does not draw it this frame: its number is not drawn (a safety net behind the gate of on_tick: a number can never show where a hidden ant is). An ant that is gone from the
    /// game is neither noted here nor by note_position: its number plays out where it was last drawn.
    void note_hidden(uint32_t id) noexcept {
        for (size_t i = 0; i < count_; ++i) {
            if (items_[i].id == id) items_[i].hidden = true;
        }
    }

    size_t count() const noexcept { return count_; }
    const Number& at(size_t i) const noexcept { return items_[i]; }

private:
    static constexpr uint32_t kMaxIds = 1u << 22;      // (a table by id: no match comes near it)
    struct Seen {
        uint16_t hp{0};
        uint16_t held{0};       // hit points gained while the ant is in the hill, shown when it comes out
        bool known{false};
    };

    void add(uint32_t id, bool heal, int32_t amount, int64_t now_ms, int32_t wx, int32_t wy) {
        if (!heal) {
            for (size_t i = 0; i < count_; ++i) {
                Number& n = items_[i];
                if (n.id == id && !n.heal && now_ms - n.start_ms <= kMergeMs) {   // a loss close to the last one is the same number
                    n.amount += amount;
                    n.start_ms = now_ms;
                    return;
                }
            }
        }
        int32_t same_sign = 0;
        size_t oldest_same = count_;
        for (size_t i = 0; i < count_; ++i) {
            if (items_[i].id != id || items_[i].heal != heal) continue;
            ++same_sign;
            if (oldest_same == count_ || items_[i].start_ms < items_[oldest_same].start_ms) oldest_same = i;
        }
        if (same_sign >= kMaxPerSign) remove(oldest_same);
        if (count_ >= static_cast<size_t>(kMaxLive)) {
            size_t oldest = 0;
            for (size_t i = 1; i < count_; ++i) {
                if (items_[i].start_ms < items_[oldest].start_ms) oldest = i;
            }
            remove(oldest);
        }
        uint8_t level = 0;                                           // the lowest line that no live number of this ant stands on (a damage and a heal number never merge: the newer one is above)
        for (bool free_line = false; !free_line; ++level) {
            free_line = true;
            for (size_t i = 0; i < count_; ++i) {
                if (items_[i].id == id && items_[i].level == level) free_line = false;
            }
            if (free_line) break;
        }
        Number n;
        n.id = id;
        n.heal = heal;
        n.amount = amount;
        n.start_ms = now_ms;
        n.wx = wx;
        n.wy = wy;
        n.level = level;
        items_[count_++] = n;
    }
    void remove(size_t i) noexcept {
        for (; i + 1 < count_; ++i) items_[i] = items_[i + 1];
        --count_;
    }

    std::vector<Seen> seen_;
    std::array<Number, kMaxLive> items_{};
    size_t count_{0};
    uint32_t last_tick_{0};
    bool have_tick_{false};
};

}  // namespace ants::app::health
