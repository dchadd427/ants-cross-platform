// The units of the renderer: the software cursor, the ants (the clip that the simulation plays, the sub-tick prediction, the sorted queue), the score numbers, the selection markers and brackets and the burn
// overlays, which are the children of the view that are drawn over the world in the order of their creation (the overlay queue).

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/health_marks.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

void Renderer::render_software_cursor(CursorType type, int32_t screen_x, int32_t screen_y, uint32_t anim_tick) {
    if (!archive_ || !texture_cache_ || !renderer_) return;
    (void)anim_tick;

    uint32_t anim_id = 41; // c_normal
    switch (type) {
        case CursorType::Normal:      anim_id = 41; break;
        case CursorType::Select:      anim_id = 42; break;
        case CursorType::Move:        anim_id = 44; break;
        case CursorType::ThiefTarget: anim_id = 43; break;
        case CursorType::Attack:      anim_id = 33; break;
        case CursorType::ScrollN:     anim_id = 51; break;
        case CursorType::ScrollNE:    anim_id = 46; break;
        case CursorType::ScrollE:     anim_id = 45; break;
        case CursorType::ScrollSE:    anim_id = 49; break;
        case CursorType::ScrollS:     anim_id = 48; break;
        case CursorType::ScrollSW:    anim_id = 52; break;
        case CursorType::ScrollW:     anim_id = 50; break;
        case CursorType::ScrollNW:    anim_id = 47; break;
        case CursorType::Food:        anim_id = 54; break;
        default:                      anim_id = 41; break;
    }

    if (anim_id >= archive_->animation_count()) return;
    const auto& anim = archive_->get_animation(anim_id);
    if (anim.subitems.empty()) return;

    size_t sub_idx = 0;
    if (anim.subitems.size() > 1) {
        uint32_t total_dur_ms = 0;
        for (const auto& sub : anim.subitems) {
            total_dur_ms += (sub.val3 > 0) ? sub.val3 : 100;
        }
        if (total_dur_ms == 0) total_dur_ms = 1000;
        uint32_t now_ms = SDL_GetTicks() % total_dur_ms;
        uint32_t accum = 0;
        for (size_t i = 0; i < anim.subitems.size(); ++i) {
            uint32_t dur = (anim.subitems[i].val3 > 0) ? anim.subitems[i].val3 : 100;
            if (now_ms < accum + dur) {
                sub_idx = i;
                break;
            }
            accum += dur;
            if (i + 1 == anim.subitems.size()) {
                sub_idx = i;
            }
        }
    }

    const auto& sub = anim.subitems[sub_idx];
    draw_frame_parts(sub, screen_x, screen_y);
}

// The stored (or mirrored) Table-4 animation of the clip the simulation plays on the ant; null while another system animates it.
const ants::assets::AnimationSequence* Renderer::ant_loco_sequence(const ants::sim::AntSnapshot& ant) const {
    if (!archive_ || ant.loco_clip == 0x7FFE || ant.loco_clip >= archive_->animation_count()) return nullptr;
    const auto& stored = archive_->get_animation(ant.loco_clip);
    if (!ant.loco_mirrored) return &stored;
    // Mirrored clips (SW, W, NW) use the archive's mirrored copies of the stored SE, E and NE animations.
    if (stored.name.size() > 3) {
        const char digit = stored.name[stored.name.size() - 3];
        const int mirrored_dir = (digit == '2') ? 5 : (digit == '9') ? 6 : (digit == '8') ? 7 : -1;
        if (mirrored_dir >= 0) {
            return archive_->get_directional_animation(stored.name.substr(0, stored.name.size() - 3),
                                                       static_cast<ants::assets::Direction>(mirrored_dir));
        }
    }
    return nullptr;
}

Renderer::AntClipPrediction Renderer::predict_ant_clip(const ants::sim::AntSnapshot& ant,
                                                       const ants::assets::AnimationSequence* seq) const {
    AntClipPrediction p;
    if (!seq || seq->subitems.empty()) return p;
    p.frame = std::min<size_t>(ant.loco_frame, seq->subitems.size() - 1);
    using S = ants::sim::UnitState;
    // The real-time player of the original steps every clip when a frame ends (AnimationStep 0x102b997 with timeGetTime) and the screen is redrawn on every scheduler pass
    // (REFRESH, period 0, 0x102c4d1), so whatever clip the ant plays, walk, idle, swim, dive, climb, harvest or an action, the frames that end before the next tick are shown
    // in advance. A frame's displacement applies when the frame ends. The last frame of a clip stays until the next tick, where the simulation's step callback decides what
    // comes next (the next tile, a snap to the tile centre, another clip); only an idle clip is known to loop.
    if (ant.state == S::Burn) return p;                                // the frozen ant under its burn overlay is not drawn
    const bool loops = (ant.state == S::Idle || ant.state == S::GuardIdle);
    int32_t left = static_cast<int32_t>(ant.loco_left_ms);
    int32_t t = static_cast<int32_t>(sub_tick_ms_);
    for (int guard = 0; guard < 64 && t >= left; ++guard) {
        if (p.frame + 1 < seq->subitems.size()) {
            t -= left;
            p.dx += seq->subitems[p.frame].val1;
            p.dy += seq->subitems[p.frame].val2;
            ++p.frame;
        } else if (loops) {
            t -= left;
            p.frame = 0;
        } else {
            break;
        }
        left = std::max<int32_t>(1, static_cast<int32_t>(seq->subitems[p.frame].val3));
    }
    return p;
}

void Renderer::draw_single_ant(const ants::sim::AntSnapshot& ant) {
    // The hill actions (enter, hatch, raid) are ordinary clips played on the tile centre the simulation puts the ant
    // on, so there is no special anchor: every ant is drawn where it stands.
    int32_t sx = 0, sy = 0;
    camera_.world_to_screen(ant.px, ant.py, sx, sy);
    // The original clips per sprite part (FUN_0102fb6d), never by the ant's anchor: the art of a walking ant reaches 35 px above
    // it and a blown ant's art (aggb / bomb flights) up to ~155 px away from the anchor that jumped 128 px. Only an ant whose art
    // cannot reach the playfield is skipped.
    constexpr int32_t kArtMargin = 160;
    const LayoutRect view = world_view();
    if (sx < view.x - kArtMargin || sx > view.x + view.w + kArtMargin ||
        sy < view.y - kArtMargin || sy > view.y + view.h + kArtMargin) return;

    // The original has neither a shadow nor a hop: a flight is the displacement baked into the aggb / aggh clips.
    const int32_t render_y = sy;

    // The simulation plays the exact ants.chd clip on every ant (idle, walk on each terrain, swim, dive, climb, can't-go and all the actions), so the frame to draw is that
    // clip's current frame. There is no interpolation: as in the 1998 game a sprite moves only when its animation frame ends; the real-time player of the original does that at
    // the moment a frame ends, not at 50 ms simulation ticks, so the frames that end before the next tick are shown in advance (predict_ant_clip). Mirrored clips (SW, W, NW)
    // use the archive's mirrored copies of the stored SE, E and NE animations.
    const ants::assets::AnimationSequence* loco_seq = ant_loco_sequence(ant);
    const AntClipPrediction pred = predict_ant_clip(ant, loco_seq);

    if (ant.frozen) {
        // The display loop (0x10088e7) skips a frozen ant (sprite slot +0x40 is +0xfc): the dud bomb's ?bu clip is a full-body
        // overlay, and drawing the idle ant beneath it would show a ghost body around the flames.
    } else if (loco_seq && !loco_seq->subitems.empty()) {
        // Draws one animation frame's parts at the ant in the original order (last stored part first, so the first stored part - e.g. a carried lunchbox in front of the body -
        // is on top) with the original ant color rule.
        draw_frame_parts(loco_seq->subitems[pred.frame], sx + pred.dx, render_y + pred.dy, ant.loco_mirrored, ant_color(ant.player_id));
    }

}

size_t Renderer::get_anim_subitem_by_time(const ants::assets::AnimationSequence& seq, uint32_t now_ms) {
    if (seq.subitems.empty()) return 0;
    uint32_t total_ms = 0;
    for (const auto& sub : seq.subitems) {
        total_ms += (sub.val3 > 0 ? sub.val3 : 100);
    }
    if (total_ms == 0) return 0;
    uint32_t t = now_ms % total_ms;
    uint32_t accum = 0;
    for (size_t i = 0; i < seq.subitems.size(); ++i) {
        accum += (seq.subitems[i].val3 > 0 ? seq.subitems[i].val3 : 100);
        if (t < accum) return i;
    }
    return 0;
}

void Renderer::draw_anthill_selection_brackets(int32_t cx, int32_t cy, uint32_t elapsed_ms) {
    if (archive_ && texture_cache_ && 59 < archive_->animation_count()) {
        const auto& ears_seq = archive_->get_animation(59); // hillears
        if (!ears_seq.subitems.empty()) {
            size_t sub_idx = get_anim_subitem_by_time(ears_seq, elapsed_ms);
            draw_frame_parts(ears_seq.subitems[sub_idx], cx, cy);
            return;
        }
    }

    // Fallback: draw geometry lines around the 128x128 footprint
    const int32_t w = 128, h = 128;
    const int32_t x = cx - w / 2 + picture_.x, y = cy - h / 2 + picture_.y;      // (drawn straight to SDL: the picture's corner is added here)
    // Fallback: draw geometry lines
    SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
    int32_t arm = 20;
    int32_t thick = 3;

    // Top-Left bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + 2, y + t, x + arm, y + t);
        SDL_RenderDrawLine(renderer_, x + t, y + 2, x + t, y + arm);
    }
    SDL_RenderDrawLine(renderer_, x + 1, y + 1, x + 2, y);
    SDL_RenderDrawLine(renderer_, x, y + 2, x + 1, y + 1);

    // Top-Right bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + w - arm, y + t, x + w - 3, y + t);
        SDL_RenderDrawLine(renderer_, x + w - 1 - t, y + 2, x + w - 1 - t, y + arm);
    }
    SDL_RenderDrawLine(renderer_, x + w - 3, y, x + w - 2, y + 1);
    SDL_RenderDrawLine(renderer_, x + w - 2, y + 1, x + w - 1, y + 2);

    // Bottom-Left bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + 2, y + h - 1 - t, x + arm, y + h - 1 - t);
        SDL_RenderDrawLine(renderer_, x + t, y + h - arm, x + t, y + h - 3);
    }
    SDL_RenderDrawLine(renderer_, x, y + h - 3, x + 1, y + h - 2);
    SDL_RenderDrawLine(renderer_, x + 1, y + h - 2, x + 2, y + h - 1);

    // Bottom-Right bracket
    for (int32_t t = 0; t < thick; ++t) {
        SDL_RenderDrawLine(renderer_, x + w - arm, y + h - 1 - t, x + w - 3, y + h - 1 - t);
        SDL_RenderDrawLine(renderer_, x + w - 1 - t, y + h - arm, x + w - 1 - t, y + h - 3);
    }
    SDL_RenderDrawLine(renderer_, x + w - 1, y + h - 3, x + w - 2, y + h - 2);
    SDL_RenderDrawLine(renderer_, x + w - 2, y + h - 2, x + w - 3, y + h - 1);
}

void Renderer::collect_ant_units(const ants::sim::WorldState& world) {
    for (const auto& a : world.ants) {
        // The original shows the local player's and the allies' ants always (0x101aa0d); every other ant only on explored ground
        if (!health::ant_drawn(world, hud_team_id_, a)) continue;     // Concealed enemy ant under fog of war
        RenderItem item{};
        // The sort key is the sprite's y: a thrown ant's key jumps with its position at the end of the first flight frame
        const AntClipPrediction pred = predict_ant_clip(a, ant_loco_sequence(a));
        item.sort_y = a.py + pred.dy;
        item.draw_func = [this, a, pred](SDL_Renderer*, TextureCache&) { this->draw_single_ant(a); this->draw_ant_number(a, a.px + pred.dx, a.py + pred.dy); };    // (the original's number follows its ant: renderer_health.cpp)
        note_health_mark(a, a.px + pred.dx, a.py + pred.dy);
        if (changes_ != nullptr && changes_->count() > 0) changes_->note_position(a.id, a.px + pred.dx, a.py + pred.dy);       // (the damage and heal numbers stand over the ant as it is drawn)
        render_queue_.push_back(std::move(item));
    }
}

// Signed score number as the original draws it (Ants.exe FUN_01010452 with the sign flag): a 6-slot field of 9 px
// slots, leading zeros skipped but still advancing, the sign glyph in the slot of the first significant digit and
// every digit from there on shifted one slot to the right.
void Renderer::draw_score_number(int32_t amount, int32_t world_x, int32_t world_y) {
    int32_t value = std::clamp(amount < 0 ? -amount : amount, 0, 999999);
    int32_t divisor = 100000;
    bool leading = true;
    bool sign_pending = true;
    int32_t x = world_x;
    while (divisor > 0) {
        const int32_t digit = value / divisor;
        value %= divisor;
        if (digit != 0 || divisor == 1) leading = false;
        if (!leading) {
            if (sign_pending) {
                draw_template_world(amount > 0 ? anim_id_plus_ : anim_id_minus_, x, world_y);
                x += 9;
                sign_pending = false;
            }
            draw_template_world(anim_id_digit_[digit], x, world_y);
        }
        divisor /= 10;
        x += 9;
    }
}

void Renderer::collect_score_bubbles(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    for (const auto& b : world.score_bubbles) {
        // 20 steps, one every 20 ms, 5 px each: up for a gain, down for a loss; the sprite ends after 400 ms
        const uint32_t t = b.elapsed_ms + sub_tick_ms_;
        if (t >= 400 || b.amount == 0) continue;
        const int32_t steps = static_cast<int32_t>(std::min<uint32_t>(20u, t / 20u + 1u));
        const int32_t y = b.y + (b.amount > 0 ? -5 : 5) * steps;
        OverlayItem item{};
        item.created_ms = overlay_now_ms() - static_cast<int64_t>(t);
        const int32_t amount = b.amount, x = b.x;
        item.draw = [this, amount, x, y]() { this->draw_score_number(amount, x, y); };
        overlay_queue_.push_back(std::move(item));
    }
}

void Renderer::collect_hill_brackets(const ants::sim::Grid& grid, int32_t selected_base_team_id) {
    if (selected_base_team_id < 0 || !archive_) {
        hill_marker_team_ = -1;
        return;
    }
    // The selection brackets ("hillears") sit at the top-left of tile (anchor row + 1, anchor col + 1), i.e. the centre
    // of the 4x4 hill footprint (Ants.exe FUN_01028b4c); they are sprites of the sorted list with that y as key.
    int32_t ax = -1, ay = -1;
    if (has_anthill_bases_ && selected_base_team_id < 4 &&
        anthill_bases_[static_cast<size_t>(selected_base_team_id)].x >= 0) {
        ax = anthill_bases_[static_cast<size_t>(selected_base_team_id)].x * TILE_SIZE + 2 * TILE_SIZE;
        ay = anthill_bases_[static_cast<size_t>(selected_base_team_id)].y * TILE_SIZE + 2 * TILE_SIZE;
    } else {
        for (const auto& a : grid.anthills()) {
            if (static_cast<int32_t>(a.team_id) == selected_base_team_id) {
                ax = static_cast<int32_t>(a.x) * TILE_SIZE + TILE_SIZE;
                ay = static_cast<int32_t>(a.y) * TILE_SIZE + TILE_SIZE;
                break;
            }
        }
    }
    if (ax < 0) return;
    if (hill_marker_team_ != selected_base_team_id) {   // a new marker (the template restarts on every selection)
        hill_marker_team_ = selected_base_team_id;
        hill_marker_start_ms_ = overlay_now_ms();
    }
    OverlayItem item{};
    item.created_ms = hill_marker_start_ms_;
    const int64_t start = hill_marker_start_ms_;
    item.draw = [this, ax, ay, start]() {
        const LayoutRect view = world_view();
        const int32_t sx = view.x + (ax - static_cast<int32_t>(camera_.x));
        const int32_t sy = view.y + (ay - static_cast<int32_t>(camera_.y));
        if (sx >= view.x - 128 && sx <= view.x + view.w + 128 &&
            sy >= view.y - 128 && sy <= view.y + view.h + 128) {
            this->draw_anthill_selection_brackets(sx, sy, static_cast<uint32_t>(overlay_now_ms() - start));
        }
    };
    overlay_queue_.push_back(std::move(item));
}

void Renderer::draw_sorted_queue() {
    // Stable sort: sprites with equal keys keep their insertion order (later added = drawn on top)
    std::stable_sort(render_queue_.begin(), render_queue_.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.sort_y < b.sort_y;
    });
    for (auto& item : render_queue_) {
        item.draw_func(renderer_, *texture_cache_);
    }
}

int64_t Renderer::overlay_now_ms() const {
    return anim_clock_pin_ms_ >= 0 ? static_cast<int64_t>(anim_clock_pin_ms_) : static_cast<int64_t>(SDL_GetTicks());
}

// The dud bomb's burn overlay (FUN_01021c68, created by SetAction 0xa at 0x101af27 - 0x101af66): the ?bu clip is a sprite of its own, a CHILD OF THE VIEW CONTAINER
// (AddChild at 0x101af66), so it is drawn after the whole map sprite, over every ant, in creation order with the other children (docs 5.60); it sits on the cell centre of
// its ant and its visibility test (vtable slot +0x3c, 0x101a40f) is the explored test of that cell. The ant underneath is frozen and not drawn.
void Renderer::collect_burn_overlays(const ants::sim::WorldState& world) {
    if (!archive_ || !texture_cache_) return;
    static const char type_letters[6] = { 'g', 'b', 'f', 't', 'c', 's' };
    const int64_t now = overlay_now_ms();
    for (const auto& a : world.ants) {
        if (a.burn_elapsed_ms < 0) continue;
        if (world.fog_of_war_enabled && !world.is_tile_revealed(a.tile_x, a.tile_y)) continue;
        const std::string bu_name = std::string("a") + type_letters[static_cast<size_t>(a.type) % 6] + "bu301";
        const auto* bu_seq = archive_->find_animation(bu_name);
        if (!bu_seq || bu_seq->subitems.empty()) continue;
        OverlayItem item{};
        item.created_ms = now - a.burn_elapsed_ms;
        const int32_t px = a.px, py = a.py;
        const uint32_t elapsed = static_cast<uint32_t>(a.burn_elapsed_ms) + sub_tick_ms_;
        const uint8_t color = ant_color(a.player_id);
        item.draw = [this, bu_seq, px, py, elapsed, color]() {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(px, py, sx, sy);
            const LayoutRect view = world_view();
            if (sx < view.x - 160 || sx > view.x + view.w + 160 || sy < view.y - 160 || sy > view.y + view.h + 160) return;
            this->draw_frame_parts(bu_seq->subitems[get_anim_subitem_by_time(*bu_seq, elapsed)], sx, sy, false, color);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

// Selection markers (Ants.exe FUN_01010373 -> FUN_0101b52f): one base sprite per selected ant (own or inspected enemy),
// a copy of dogears (hp >= 9), yelears (hp 3..8) or redears (hp <= 2) placed at the ant's own position. It is a child of
// the view container, so it is drawn after the whole map sprite (over ants, foliage and the fog) in creation order, and
// it runs its own clock from the moment it is created; it is re-created on every health change.
void Renderer::collect_selection_markers(const ants::sim::WorldState& world, int32_t selected_unit_id,
                                         const std::vector<uint32_t>& selected_unit_ids) {
    std::vector<uint32_t> selected = selected_unit_ids;
    if (selected.empty() && selected_unit_id > 0) selected.push_back(static_cast<uint32_t>(selected_unit_id));
    for (auto it = ears_state_.begin(); it != ears_state_.end();) {
        it = (std::find(selected.begin(), selected.end(), it->first) == selected.end()) ? ears_state_.erase(it) : std::next(it);
    }
    if (!archive_ || !texture_cache_) return;
    const int64_t now = overlay_now_ms();
    for (uint32_t id : selected) {
        const ants::sim::AntSnapshot* ant = nullptr;
        for (const auto& a : world.ants) if (a.id == id) { ant = &a; break; }
        if (!ant) continue;
        // the marker is removed while a thief is raiding a hill (action 0xd)
        if (ant->anim_state == static_cast<uint16_t>(ants::sim::UnitState::Infiltrating)) continue;
        EarsState& st = ears_state_[id];
        if (st.start_ms == 0 || st.hp != ant->hp) {
            st.hp = ant->hp;
            st.start_ms = std::max<int64_t>(1, now);
        }
        const uint32_t ears_id = (ant->hp >= health::kGreenFrom) ? 58u : (ant->hp <= health::kRedUpTo) ? 61u : 60u;
        if (ears_id >= archive_->animation_count()) continue;
        const auto& seq = archive_->get_animation(ears_id);
        if (seq.subitems.empty()) continue;
        OverlayItem item{};
        item.created_ms = st.start_ms;
        const AntClipPrediction pred = predict_ant_clip(*ant, ant_loco_sequence(*ant));     // the marker follows the sprite, which steps when a frame ends
        const int32_t px = ant->px + pred.dx, py = ant->py + pred.dy;
        const int64_t start = st.start_ms;
        item.draw = [this, &seq, px, py, start]() {
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(px, py, sx, sy);
            const LayoutRect view = world_view();
            if (sx < view.x - 160 || sx > view.x + view.w + 160 ||
                sy < view.y - 160 || sy > view.y + view.h + 160) return;
            const size_t f = get_anim_subitem_by_time(seq, static_cast<uint32_t>(std::max<int64_t>(0, overlay_now_ms() - start)));
            this->draw_frame_parts(seq.subitems[f], sx, sy);
        };
        overlay_queue_.push_back(std::move(item));
    }
}

void Renderer::draw_overlay_queue() {
    std::stable_sort(overlay_queue_.begin(), overlay_queue_.end(),
                     [](const OverlayItem& a, const OverlayItem& b) { return a.created_ms < b.created_ms; });
    for (auto& item : overlay_queue_) item.draw();
}

} // namespace ants::app
