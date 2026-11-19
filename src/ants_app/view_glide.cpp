#include "ants_app/view_glide.hpp"

#include "ants_app/health_changes.hpp"
#include "ants_sim/movement_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ants::app {

int32_t ViewGlide::scaled(int32_t value, uint32_t left_ms, uint32_t total_ms) noexcept {
    if (total_ms == 0) return 0;
    const int64_t scaled = static_cast<int64_t>(value) * static_cast<int64_t>(left_ms);
    const int64_t half = static_cast<int64_t>(total_ms) / 2;
    return static_cast<int32_t>(scaled >= 0 ? (scaled + half) / total_ms : -((-scaled + half) / total_ms));
}

uint32_t ViewGlide::duration_ms(int32_t dx, int32_t dy, bool own) noexcept {
    if (own) return kDurationMs;
    const double length = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);        // (the distance along the line, not the larger axis: a diagonal is carried at the pace of a walk too)
    const double ms = std::ceil(length * 1000.0 / static_cast<double>(kWalkPxPerSecond));
    return static_cast<uint32_t>(std::clamp(ms, static_cast<double>(kDurationMs), static_cast<double>(kMaxDurationMs)));
}

void ViewGlide::add(uint32_t ant, int32_t dx, int32_t dy, bool own) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].ant != ant) continue;
        const int32_t sum_x = scaled(entries_[i].dx, entries_[i].left_ms, entries_[i].total_ms) + dx;
        const int32_t sum_y = scaled(entries_[i].dy, entries_[i].left_ms, entries_[i].total_ms) + dy;
        if (std::max(std::abs(sum_x), std::abs(sum_y)) > kMaxShiftPx || (sum_x == 0 && sum_y == 0)) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            const uint32_t total = duration_ms(sum_x, sum_y, own);
            entries_[i] = Entry{ant, sum_x, sum_y, total, total, own};
        }
        return;
    }
    if (std::max(std::abs(dx), std::abs(dy)) > kMaxShiftPx || (dx == 0 && dy == 0)) return;
    const uint32_t total = duration_ms(dx, dy, own);
    entries_.push_back(Entry{ant, dx, dy, total, total, own});
}

void ViewGlide::advance(uint32_t ms) {
    for (size_t i = 0; i < entries_.size();) {
        if (entries_[i].left_ms <= ms) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        else {
            entries_[i].left_ms -= ms;
            ++i;
        }
    }
}

bool ViewGlide::offset_of(uint32_t ant, int32_t& dx, int32_t& dy) const {
    dx = 0;
    dy = 0;
    for (const Entry& e : entries_) {
        if (e.ant != ant) continue;
        dx = scaled(e.dx, e.left_ms, e.total_ms);
        dy = scaled(e.dy, e.left_ms, e.total_ms);
        return dx != 0 || dy != 0;
    }
    return false;
}

bool ViewGlide::velocity_of(uint32_t ant, int32_t& vx, int32_t& vy) const {
    vx = 0;
    vy = 0;
    for (const Entry& e : entries_) {
        if (e.ant != ant || e.total_ms == 0) continue;
        vx = static_cast<int32_t>(std::lround(-1000.0 * e.dx / static_cast<double>(e.total_ms)));         // (towards the engine's place: against the offset)
        vy = static_cast<int32_t>(std::lround(-1000.0 * e.dy / static_cast<double>(e.total_ms)));
        return vx != 0 || vy != 0;
    }
    return false;
}

bool ViewGlide::is_own(uint32_t ant) const {
    for (const Entry& e : entries_) {
        if (e.ant == ant) return e.own;
    }
    return false;
}

int32_t ViewGlide::carried_px(uint32_t ant) const {
    for (const Entry& e : entries_) {
        if (e.ant != ant || e.total_ms == 0) continue;
        const double length = std::sqrt(static_cast<double>(e.dx) * e.dx + static_cast<double>(e.dy) * e.dy);
        return static_cast<int32_t>(std::lround(length * (1.0 - static_cast<double>(e.left_ms) / static_cast<double>(e.total_ms))));
    }
    return 0;
}

bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy) {
    int32_t whole_x = 0;
    int32_t whole_y = 0;
    dx = 0;
    dy = 0;
    if (!glide.offset_of(ant.id, whole_x, whole_y)) return false;
    if (!health::ant_drawn(world, viewer_team, ant)) return false;       // (an ant that the picture does not draw where the engine has it is drawn nowhere, and nothing picks it: its slide may have begun on explored ground)
    for (int32_t quarters = 4; quarters > 0; --quarters) {               // the whole slide, then three quarters of it, a half, a quarter: the most of it that does not draw the ant on covered ground
        sim::AntSnapshot drawn = ant;
        drawn.px += whole_x * quarters / 4;
        drawn.py += whole_y * quarters / 4;
        drawn.tile_x = drawn.px / 32;                                    // (the gate of the picture looks at the tile: the one that the ant is drawn on)
        drawn.tile_y = drawn.py / 32;
        if (!health::ant_drawn(world, viewer_team, drawn)) continue;
        dx = drawn.px - ant.px;
        dy = drawn.py - ant.py;
        return dx != 0 || dy != 0;
    }
    return false;
}

sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant) {
    sim::AntSnapshot drawn = ant;                                        // (everything but the place reads the engine's ant: its tile too)
    int32_t dx = 0;
    int32_t dy = 0;
    if (glide_offset(glide, world, viewer_team, ant, dx, dy)) {
        drawn.px += dx;
        drawn.py += dy;
    }
    return drawn;
}

namespace {

// The compass heading (0 north, 1 north-east, ... 7 north-west, the ants.chd directions) of a velocity on the screen (x right, y down)
uint8_t heading_of(double vx, double vy) {
    const double pi = 3.14159265358979323846;
    double turns = std::atan2(vx, -vy) / (pi / 4.0);                       // 0 at north, growing clockwise
    int step = static_cast<int>(std::lround(turns));
    step = ((step % 8) + 8) % 8;
    return static_cast<uint8_t>(step);
}

void heading_vector(uint8_t heading, double& ux, double& uy) {
    static const double kUnit[8][2] = {{0.0, -1.0}, {0.70710678, -0.70710678}, {1.0, 0.0}, {0.70710678, 0.70710678}, {0.0, 1.0}, {-0.70710678, 0.70710678}, {-1.0, 0.0}, {-0.70710678, -0.70710678}};
    ux = kUnit[heading & 7u][0];
    uy = kUnit[heading & 7u][1];
}

}  // namespace

sim::AntSnapshot slid_pose(const ViewGlide& glide, const sim::AntSnapshot& engine, const sim::AntSnapshot& drawn) {
    using S = sim::UnitState;
    const sim::AntSnapshot& ant = engine;
    int32_t svx = 0;
    int32_t svy = 0;
    if (!glide.velocity_of(ant.id, svx, svy) || glide.is_own(ant.id)) return drawn;       // it does not slide, or it is the player's own
    int32_t ox = 0;
    int32_t oy = 0;
    glide.offset_of(ant.id, ox, oy);
    if (drawn.px - engine.px != ox || drawn.py - engine.py != oy) return drawn;           // the fog cut the slide: the ant is not carried as the slide says
    if (ant.state != S::Walking && ant.state != S::Idle && ant.state != S::GuardIdle) return drawn;         // a blow, a flinch, a throw, a swim, an action: the engine's picture
    if (ant.loco_clip == 0x7FFE || ant.frozen || ant.is_swimming) return drawn;           // another system animates it, or it swims (the swim clips are the engine's)
    const uint8_t type = static_cast<uint8_t>(ant.type);
    const bool carrying = ant.is_holding;
    const bool walking = ant.state == S::Walking && ant.action == 1;                        // (action 0 of a walking ant is its three ticks of start-up: it stands)
    // The engine's walk: the clip it plays now (found by its terrain: grass, sand, mud, dirt) carries the ant by the sum of its steps over its time (80 px/s straight on grass, 33 on mud ...). An ant that
    // stands is drawn on grass or mud: this model has no grid to ask for the ground it stands on
    uint8_t terrain = ant.is_on_mud ? sim::movement::kTerrainMud : sim::movement::kTerrainGrass;
    double vx = static_cast<double>(svx);
    double vy = static_cast<double>(svy);
    if (walking) {
        bool known = false;
        for (const uint8_t t : {sim::movement::kTerrainGrass, sim::movement::kTerrainSand, sim::movement::kTerrainMud, sim::movement::kTerrainDirt}) {
            const sim::movement::MotionClip own = sim::movement::walk_clip(type, t, static_cast<uint8_t>(ant.facing & 7u), carrying);
            if (!own.valid() || own.chd_index != ant.loco_clip || own.mirrored != ant.loco_mirrored || own.total_duration_ms() == 0) continue;
            terrain = t;
            known = true;
            int64_t steps_x = 0;
            int64_t steps_y = 0;
            for (uint16_t i = 0; i < own.count; ++i) {
                steps_x += own.dx(i);
                steps_y += own.dy(i);
            }
            vx += 1000.0 * static_cast<double>(steps_x) / static_cast<double>(own.total_duration_ms());
            vy += 1000.0 * static_cast<double>(steps_y) / static_cast<double>(own.total_duration_ms());
            break;
        }
        if (!known) {                                                                       // (a clip that is none of these: the pace of a walk along its facing)
            double ux = 0.0;
            double uy = 0.0;
            heading_vector(static_cast<uint8_t>(ant.facing), ux, uy);
            vx += ux * ViewGlide::kWalkPxPerSecond;
            vy += uy * ViewGlide::kWalkPxPerSecond;
        }
    }
    sim::AntSnapshot posed = drawn;
    const double speed = std::sqrt(vx * vx + vy * vy);
    if (speed < static_cast<double>(kSlideStandsBelowPxPerSecond)) {                        // it goes nowhere: drawn standing (a walk cycle on the spot is the thing not to show)
        if (!walking) return drawn;
        const sim::movement::MotionClip idle = sim::movement::idle_clip(type, static_cast<uint8_t>(ant.facing & 7u), carrying);
        if (!idle.valid()) return drawn;
        posed.loco_clip = idle.chd_index;
        posed.loco_mirrored = idle.mirrored;
        posed.loco_frame = 0;
        posed.loco_left_ms = 0xFFFFu;
        return posed;
    }
    const uint8_t heading = heading_of(vx, vy);
    const sim::movement::MotionClip clip = sim::movement::walk_clip(type, terrain, heading, carrying);
    if (!clip.valid()) return drawn;
    posed.facing = heading;
    posed.loco_clip = clip.chd_index;
    posed.loco_mirrored = clip.mirrored;
    // the walk cycle: the engine's frame while the engine walks it (its legs and its ground agree), else a frame for every 4 px that the slide has carried the ant
    const uint16_t frame = walking ? ant.loco_frame : static_cast<uint16_t>(glide.carried_px(ant.id) / 4);
    posed.loco_frame = static_cast<uint16_t>(frame % clip.count);
    posed.loco_left_ms = 0xFFFFu;                                                           // (no frame of the clip ends before the next tick: the drawn ant is not moved by the clip's own steps)
    return posed;
}

}  // namespace ants::app
